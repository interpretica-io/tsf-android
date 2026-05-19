#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# A stand-in for the adb server on an agent that has no Android
# device. It speaks the adb server protocol that ta_android uses:
# host:devices, host:transport, shell v2, sync push and pull, reboot.
# One fake device, FAKE0001, answers the shell commands the TAPI
# sends. The state (installed packages, files, the log) lives in a
# directory: FAKE_ADB_STATE, or /tmp/fake_adb_state.
#
# Consumer selftests start it on the agent (for example through
# /agent/process) and point /agent/android/server at it, so that the
# whole path from the test to the agent library is checked without
# hardware. It is not adb: it knows what the TAPI asks and nothing
# else.
#
# Usage: fake_adb_server.py [--port=5037] [--state=<dir>]

import os
import shlex
import socket
import struct
import sys
import threading
import time

SERIAL = 'FAKE0001'

PNG_1X1 = (b'\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\0\x01\0\0\0\x01\x08\x06\0\0\0'
           b'\x1f\x15\xc4\x89\0\0\0\nIDATx\x9cc\0\x01\0\0\x05\0\x01\r\n\x2d'
           b'\xb4\0\0\0\0IEND\xaeB\x60\x82')

UI_DUMP = '''<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<hierarchy rotation="0">
  <node index="0" text="" resource-id="" class="android.widget.FrameLayout" package="com.example.fake" bounds="[0,0][1080,2340]">
    <node index="0" text="Welcome" resource-id="com.example.fake:id/title" class="android.widget.TextView" package="com.example.fake" bounds="[100,300][980,400]" />
    <node index="1" text="" resource-id="com.example.fake:id/name" class="android.widget.EditText" package="com.example.fake" bounds="[100,500][980,620]" />
    <node index="2" text="Sign in" resource-id="com.example.fake:id/sign_in" class="android.widget.Button" package="com.example.fake" bounds="[340,800][740,920]" />
  </node>
</hierarchy>
'''


class State:
    def __init__(self, root):
        self.root = root
        os.makedirs(os.path.join(root, 'sdcard'), exist_ok=True)
        os.makedirs(os.path.join(root, 'data', 'local', 'tmp'), exist_ok=True)
        self.packages = os.path.join(root, 'packages')
        self.logcat = os.path.join(root, 'logcat.log')
        self.events = os.path.join(root, 'events')
        for f in (self.packages, self.logcat, self.events):
            open(f, 'a').close()
        self.lock = threading.Lock()

    def devpath(self, path):
        return os.path.join(self.root, path.lstrip('/'))

    def log(self, msg):
        line = '%s  1234  1234 I FakeAdb : %s\n' % (
            time.strftime('%m-%d %H:%M:%S.000'), msg)
        with self.lock, open(self.logcat, 'a') as f:
            f.write(line)

    def event(self, msg):
        with self.lock, open(self.events, 'a') as f:
            f.write(msg + '\n')

    def installed(self):
        with open(self.packages) as f:
            return [l.strip() for l in f if l.strip()]

    def install(self, pkg):
        if pkg not in self.installed():
            with open(self.packages, 'a') as f:
                f.write(pkg + '\n')

    def uninstall(self, pkg):
        pkgs = [p for p in self.installed() if p != pkg]
        with open(self.packages, 'w') as f:
            f.write(''.join(p + '\n' for p in pkgs))


class Shell:
    """The device shell: runs one command line, returns (out, err, status)."""

    def __init__(self, state, conn):
        self.state = state
        self.conn = conn

    def run(self, line):
        out = []
        err = []
        status = 0
        for part in line.split('&&'):
            part = part.strip()
            quiet = part.endswith('>/dev/null')
            if quiet:
                part = part[:-len('>/dev/null')].strip()
            argv = shlex.split(part)
            if not argv:
                continue
            o, e, status = self.command(argv)
            out.append('' if quiet else o)
            err.append(e)
            if status != 0:
                break
        return ''.join(out), ''.join(err), status

    def command(self, argv):
        st = self.state
        name, args = argv[0], argv[1:]

        if name == 'getprop':
            props = {'ro.build.version.release': '14',
                     'ro.build.version.sdk': '34',
                     'ro.product.model': 'Fake Phone',
                     'sys.boot_completed': '1'}
            return props.get(args[0] if args else '', '') + '\n', '', 0

        if name == 'pm' and args[:2] == ['list', 'packages']:
            return ''.join('package:%s\n' % p for p in st.installed()), '', 0

        if name == 'pm' and args[:1] == ['install']:
            # A fake APK is a text file whose first line is
            # "package: <name>"; a real one carries the name inside too
            path = args[-1]
            if not os.path.isfile(st.devpath(path)):
                return '', 'Failure [INSTALL_FAILED_INVALID_APK]\n', 1
            with open(st.devpath(path)) as f:
                first = f.readline().strip()
            if not first.startswith('package: '):
                return '', 'Failure [INSTALL_PARSE_FAILED_NOT_APK]\n', 1
            pkg = first[len('package: '):]
            st.install(pkg)
            st.log('installed ' + pkg)
            return 'Success\n', '', 0

        if name == 'pm' and args[:1] == ['uninstall']:
            if args[1] not in st.installed():
                return 'Failure [DELETE_FAILED_INTERNAL_ERROR]\n', '', 1
            st.uninstall(args[1])
            st.log('uninstalled ' + args[1])
            return 'Success\n', '', 0

        if name == 'monkey':
            pkg = args[args.index('-p') + 1]
            if pkg not in st.installed():
                return '** No activities found to run, monkey aborted.\n', '', 1
            st.event('start ' + pkg)
            st.log('started ' + pkg)
            return 'Events injected: 1\n', '', 0

        if name == 'am' and args[:1] == ['start']:
            st.event('am start ' + args[-1])
            return 'Starting: Intent { cmp=%s }\n' % args[-1], '', 0

        if name == 'am' and args[:1] == ['force-stop']:
            st.event('force-stop ' + args[1])
            st.log('stopped ' + args[1])
            return '', '', 0

        if name == 'pidof':
            pkg = args[-1]
            events = open(st.events).read().splitlines()
            running = ('start ' + pkg) in events and \
                events[::-1].index('start ' + pkg) < \
                (events[::-1].index('force-stop ' + pkg)
                 if ('force-stop ' + pkg) in events else len(events))
            return ('4242\n', '', 0) if running else ('', '', 1)

        if name == 'input':
            st.event('input ' + ' '.join(args))
            st.log('input ' + ' '.join(args))
            return '', '', 0

        if name == 'screencap':
            path = args[-1]
            os.makedirs(os.path.dirname(st.devpath(path)), exist_ok=True)
            with open(st.devpath(path), 'wb') as f:
                f.write(PNG_1X1)
            return '', '', 0

        if name == 'uiautomator':
            path = args[-1]
            with open(st.devpath(path), 'w') as f:
                f.write(UI_DUMP)
            return 'UI hierchary dumped to: %s\n' % path, '', 0

        if name == 'cat':
            try:
                with open(st.devpath(args[0])) as f:
                    return f.read(), '', 0
            except OSError:
                return '', 'cat: %s: No such file or directory\n' % args[0], 1

        if name == 'rm':
            for p in args:
                if p.startswith('-'):
                    continue
                try:
                    os.remove(st.devpath(p))
                except OSError:
                    pass
            return '', '', 0

        if name == 'logcat':
            self.stream_logcat()
            return '', '', 0

        return '', '/system/bin/sh: %s: inaccessible or not found\n' % name, 127

    def stream_logcat(self):
        """Follow the log until the client closes the connection."""
        conn = self.conn
        conn.send_frame(1, b'--------- beginning of main\n')
        with open(self.state.logcat) as f:
            while True:
                chunk = f.read()
                if chunk:
                    try:
                        conn.send_frame(1, chunk.encode())
                    except OSError:
                        return
                else:
                    try:
                        conn.sock.settimeout(0.2)
                        if conn.sock.recv(1) == b'':
                            return
                    except socket.timeout:
                        pass
                    except OSError:
                        return
                    finally:
                        conn.sock.settimeout(None)


class Connection:
    def __init__(self, sock, state):
        self.sock = sock
        self.state = state

    def read_exact(self, n):
        data = b''
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise EOFError()
            data += chunk
        return data

    def read_request(self):
        length = int(self.read_exact(4), 16)
        return self.read_exact(length).decode()

    def okay(self, payload=None):
        self.sock.sendall(b'OKAY')
        if payload is not None:
            data = payload.encode()
            self.sock.sendall(b'%04x' % len(data) + data)

    def fail(self, msg):
        data = msg.encode()
        self.sock.sendall(b'FAIL' + b'%04x' % len(data) + data)

    def send_frame(self, fid, data):
        self.sock.sendall(struct.pack('<BI', fid, len(data)) + data)

    def serve(self):
        try:
            while True:
                req = self.read_request()
                if req == 'host:version':
                    self.okay('0029')
                elif req == 'host:devices':
                    self.okay('%s\tdevice\n' % SERIAL)
                elif req in ('host:transport:' + SERIAL,
                             'host:transport-any'):
                    self.okay()
                    self.serve_device()
                    return
                elif req.startswith('host:transport:'):
                    self.fail('device \'%s\' not found' % req.split(':')[2])
                    return
                else:
                    self.fail('unknown host service')
                    return
        except (EOFError, OSError):
            pass
        finally:
            self.sock.close()

    def serve_device(self):
        req = self.read_request()
        if req.startswith('shell,v2,raw:'):
            self.okay()
            out, err, status = Shell(self.state, self).run(req[len('shell,v2,raw:'):])
            if out:
                self.send_frame(1, out.encode())
            if err:
                self.send_frame(2, err.encode())
            self.send_frame(3, bytes([status & 0xFF]))
        elif req == 'sync:':
            self.okay()
            self.serve_sync()
        elif req == 'reboot:':
            self.okay()
            self.state.log('reboot')
        else:
            self.fail('unknown service')

    def sync_header(self):
        hdr = self.read_exact(8)
        return hdr[:4].decode(), struct.unpack('<I', hdr[4:])[0]

    def sync_send(self, fid, length=0, payload=b''):
        self.sock.sendall(fid.encode() + struct.pack('<I', length) + payload)

    def serve_sync(self):
        while True:
            fid, length = self.sync_header()
            if fid == 'QUIT':
                return
            if fid == 'SEND':
                spec = self.read_exact(length).decode()
                path = spec.rsplit(',', 1)[0]
                dest = self.state.devpath(path)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, 'wb') as f:
                    while True:
                        did, dlen = self.sync_header()
                        if did == 'DONE':
                            break
                        if did != 'DATA':
                            self.sync_send('FAIL', len(b'bad frame'), b'bad frame')
                            return
                        f.write(self.read_exact(dlen))
                self.sync_send('OKAY')
            elif fid == 'RECV':
                path = self.read_exact(length).decode()
                src = self.state.devpath(path)
                if not os.path.isfile(src):
                    msg = b'remote object does not exist'
                    self.sync_send('FAIL', len(msg), msg)
                    return
                with open(src, 'rb') as f:
                    while True:
                        chunk = f.read(64 * 1024)
                        if not chunk:
                            break
                        self.sync_send('DATA', len(chunk), chunk)
                self.sync_send('DONE')
            else:
                msg = b'unknown sync request'
                self.sync_send('FAIL', len(msg), msg)
                return


def main():
    port = 5037
    root = os.environ.get('FAKE_ADB_STATE', '/tmp/fake_adb_state')
    for a in sys.argv[1:]:
        if a.startswith('--port='):
            port = int(a[len('--port='):])
        elif a.startswith('--state='):
            root = a[len('--state='):]
        else:
            sys.stderr.write('fake_adb_server: bad argument %r\n' % a)
            sys.exit(2)

    state = State(root)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port))
    srv.listen(8)
    sys.stderr.write('fake_adb_server: listening on 127.0.0.1:%d\n' % port)
    sys.stderr.flush()
    while True:
        sock, _ = srv.accept()
        threading.Thread(target=Connection(sock, state).serve,
                         daemon=True).start()


if __name__ == '__main__':
    main()
