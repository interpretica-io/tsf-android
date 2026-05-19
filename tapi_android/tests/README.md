# Test fixtures

`fake_adb_server.py` stands in for the adb server on an agent that has
no Android device. It speaks the part of the adb server protocol that
`ta_android` uses (`host:devices`, `host:transport`, shell v2, sync
push and pull, reboot) for one fake device, `FAKE0001`, and answers
the shell commands the TAPI sends the way a device does. Its state
(installed packages, files under `/sdcard` and `/data/local/tmp`, the
logcat) lives in `FAKE_ADB_STATE` (default `/tmp/fake_adb_state`).
An APK for the fake device is a text file whose first line is
`package: <name>`; `pm install` takes the package name from there.

A consumer selftest starts it on the agent, for example through
`/agent/process`, and points `/agent/android/server` at its port. That
checks the whole path from the test through the Configurator and the
RPCs to the agent library, without hardware. It does not check adb.

To try it by hand:

```sh
./fake_adb_server.py --port=15037 &
python3 - <<'EOF'
import socket
s = socket.create_connection(('127.0.0.1', 15037))
s.sendall(b'000chost:devices')
print(s.recv(4), s.recv(4), s.recv(64))
EOF
```
