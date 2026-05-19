/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android agent library
 *
 * Client of the adb server protocol: a request is a 4-digit hex
 * length followed by the text; the server answers OKAY or FAIL plus
 * a length-prefixed message. After "host:transport:<serial>" the
 * connection belongs to the device and carries one service:
 * "shell,v2,raw:<cmd>", "sync:", "reboot:".
 */

#define TE_LGR_USER     "TA Android"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "logger_api.h"

#include "ta_android.h"

/** Largest sync DATA payload adb accepts. */
#define SYNC_CHUNK      (64 * 1024)

/** Frame ids of the shell v2 protocol. */
enum {
    SHELL_STDOUT = 1,
    SHELL_STDERR = 2,
    SHELL_EXIT = 3,
};

/** Number of logcat captures that may run at once. */
#define LOGCAT_MAX      8

/** Where the captures go on the agent. */
#define LOGCAT_DIR      "/tmp"

/** Longest adb server address, "host:port". */
#define SERVER_MAX      64

/* See description in ta_android.h */
te_errno
ta_android_check_server(const char *addr)
{
    if (strchr(addr, ':') == NULL || strlen(addr) >= SERVER_MAX)
        return TE_RC(TE_TA_UNIX, TE_EINVAL);
    return 0;
}

/* Read exactly len bytes. */
static te_errno
read_exact(int fd, void *buf, size_t len)
{
    size_t got = 0;

    while (got < len)
    {
        ssize_t n = read(fd, (char *)buf + got, len - got);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return TE_OS_RC(TE_TA_UNIX, errno);
        }
        if (n == 0)
            return TE_RC(TE_TA_UNIX, TE_EPIPE);
        got += n;
    }
    return 0;
}

/* Write all len bytes. */
static te_errno
write_all(int fd, const void *buf, size_t len)
{
    size_t done = 0;

    while (done < len)
    {
        ssize_t n = write(fd, (const char *)buf + done, len - done);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return TE_OS_RC(TE_TA_UNIX, errno);
        }
        done += n;
    }
    return 0;
}

/* Connect to the adb server. */
static te_errno
adb_connect(const char *server, int *fd)
{
    char host[SERVER_MAX];
    char *port;
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *ai;
    int s = -1;
    int err;

    if (server == NULL || *server == '\0')
        server = TA_ANDROID_SERVER_DEFAULT;
    if (ta_android_check_server(server) != 0)
    {
        ERROR("Bad adb server address '%s'", server);
        return TE_RC(TE_TA_UNIX, TE_EINVAL);
    }
    te_strlcpy(host, server, sizeof(host));
    port = strrchr(host, ':');
    *port++ = '\0';

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    err = getaddrinfo(host, port, &hints, &res);
    if (err != 0)
    {
        ERROR("Cannot resolve the adb server %s: %s", server,
              gai_strerror(err));
        return TE_RC(TE_TA_UNIX, TE_EHOSTUNREACH);
    }

    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0)
            continue;
        if (connect(s, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(s);
        s = -1;
    }
    freeaddrinfo(res);

    if (s < 0)
    {
        /* Most agents have no adb server; the caller says if that matters */
        VERB("Cannot connect to the adb server at %s: %s", server,
             strerror(errno));
        return TE_OS_RC(TE_TA_UNIX, errno);
    }

    *fd = s;
    return 0;
}

/* Read a length-prefixed block (4 hex digits, then the bytes). */
static te_errno
adb_read_block(int fd, te_string *out)
{
    te_errno rc;
    char hex[5] = {0};
    unsigned long len;
    char *buf;

    rc = read_exact(fd, hex, 4);
    if (rc != 0)
        return rc;
    len = strtoul(hex, NULL, 16);
    if (len == 0)
        return 0;

    buf = TE_ALLOC(len + 1);
    rc = read_exact(fd, buf, len);
    if (rc == 0)
        te_string_append(out, "%.*s", (int)len, buf);
    free(buf);
    return rc;
}

/*
 * Send a request and check the status word. On FAIL the server's
 * message goes to the log.
 */
static te_errno
adb_request(int fd, const char *req)
{
    te_errno rc;
    te_string msg = TE_STRING_INIT;
    char hdr[16];
    char status[4];

    snprintf(hdr, sizeof(hdr), "%04zx", strlen(req));
    rc = write_all(fd, hdr, 4);
    if (rc == 0)
        rc = write_all(fd, req, strlen(req));
    if (rc == 0)
        rc = read_exact(fd, status, 4);
    if (rc != 0)
    {
        ERROR("adb request '%s' failed: %r", req, rc);
        return rc;
    }

    if (memcmp(status, "OKAY", 4) == 0)
        return 0;

    if (memcmp(status, "FAIL", 4) == 0)
    {
        adb_read_block(fd, &msg);
        ERROR("adb server refused '%s': %s", req, msg.ptr);
        te_string_free(&msg);
        return TE_RC(TE_TA_UNIX, TE_EFAIL);
    }

    ERROR("adb server answered '%s' with garbage", req);
    return TE_RC(TE_TA_UNIX, TE_EPROTO);
}

/* Connect and bind the connection to a device. */
static te_errno
adb_device_connect(const char *server, const char *serial, int *fd)
{
    te_errno rc;
    te_string req = TE_STRING_INIT;

    rc = adb_connect(server, fd);
    if (rc != 0)
        return rc;

    if (serial != NULL && *serial != '\0')
        te_string_append(&req, "host:transport:%s", serial);
    else
        te_string_append(&req, "host:transport-any");

    rc = adb_request(*fd, req.ptr);
    te_string_free(&req);
    if (rc != 0)
    {
        close(*fd);
        *fd = -1;
    }
    return rc;
}

/* See description in ta_android.h */
te_errno
ta_android_devices(const char *server, te_string *out)
{
    te_errno rc;
    int fd;

    rc = adb_connect(server, &fd);
    if (rc != 0)
        return rc;

    rc = adb_request(fd, "host:devices");
    if (rc == 0)
        rc = adb_read_block(fd, out);

    close(fd);
    return rc;
}

/* See description in ta_android.h */
te_errno
ta_android_state(const char *server, const char *serial, te_string *state)
{
    te_errno rc;
    te_string list = TE_STRING_INIT;
    char *line;
    char *save = NULL;
    size_t len = strlen(serial);

    rc = ta_android_devices(server, &list);
    if (rc != 0)
        return rc;

    rc = TE_RC(TE_TA_UNIX, TE_ENOENT);
    for (line = list.ptr != NULL ? strtok_r(list.ptr, "\n", &save) : NULL;
         line != NULL; line = strtok_r(NULL, "\n", &save))
    {
        if (strncmp(line, serial, len) == 0 && line[len] == '\t')
        {
            te_string_append(state, "%s", line + len + 1);
            rc = 0;
            break;
        }
    }

    te_string_free(&list);
    return rc;
}

/* See description in ta_android.h */
te_errno
ta_android_shell(const char *server, const char *serial, const char *cmd,
                 te_string *out,
                 te_string *err, int *status)
{
    te_errno rc;
    int fd;
    te_string req = TE_STRING_INIT;
    unsigned char hdr[5];
    char *buf = NULL;
    size_t buf_len = 0;

    rc = adb_device_connect(server, serial, &fd);
    if (rc != 0)
        return rc;

    te_string_append(&req, "shell,v2,raw:%s", cmd);
    rc = adb_request(fd, req.ptr);
    te_string_free(&req);
    if (rc != 0)
        goto out;

    *status = -1;
    for (;;)
    {
        uint32_t len;

        rc = read_exact(fd, hdr, sizeof(hdr));
        if (rc != 0)
        {
            /* The stream ends without an exit frame on some devices */
            if (TE_RC_GET_ERROR(rc) == TE_EPIPE && *status >= 0)
                rc = 0;
            break;
        }
        len = hdr[1] | (hdr[2] << 8) | (hdr[3] << 16) |
              ((uint32_t)hdr[4] << 24);
        if (len > buf_len)
        {
            free(buf);
            buf = TE_ALLOC(len + 1);
            buf_len = len;
        }
        rc = read_exact(fd, buf, len);
        if (rc != 0)
            break;

        switch (hdr[0])
        {
            case SHELL_STDOUT:
                if (out != NULL)
                    te_string_append(out, "%.*s", (int)len, buf);
                break;
            case SHELL_STDERR:
                if (err != NULL)
                    te_string_append(err, "%.*s", (int)len, buf);
                break;
            case SHELL_EXIT:
                *status = len > 0 ? (unsigned char)buf[0] : 0;
                goto out;
            default:
                break;
        }
    }

out:
    free(buf);
    close(fd);
    return rc;
}

/* Send a sync request: four-letter id plus a little-endian length. */
static te_errno
sync_send(int fd, const char *id, uint32_t len, const void *payload)
{
    te_errno rc;
    unsigned char hdr[8];

    memcpy(hdr, id, 4);
    hdr[4] = len & 0xFF;
    hdr[5] = (len >> 8) & 0xFF;
    hdr[6] = (len >> 16) & 0xFF;
    hdr[7] = (len >> 24) & 0xFF;
    rc = write_all(fd, hdr, sizeof(hdr));
    if (rc == 0 && len > 0 && payload != NULL)
        rc = write_all(fd, payload, len);
    return rc;
}

/* Read a sync response header. */
static te_errno
sync_recv(int fd, char id[5], uint32_t *len)
{
    te_errno rc;
    unsigned char hdr[8];

    rc = read_exact(fd, hdr, sizeof(hdr));
    if (rc != 0)
        return rc;
    memcpy(id, hdr, 4);
    id[4] = '\0';
    *len = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) |
           ((uint32_t)hdr[7] << 24);
    return 0;
}

/* Read a FAIL message of the sync protocol and log it. */
static te_errno
sync_fail(int fd, uint32_t len, const char *what)
{
    char *msg = TE_ALLOC(len + 1);

    if (read_exact(fd, msg, len) == 0)
        ERROR("adb sync %s failed: %s", what, msg);
    free(msg);
    return TE_RC(TE_TA_UNIX, TE_EFAIL);
}

/* See description in ta_android.h */
te_errno
ta_android_push(const char *server, const char *serial, const char *local,
                const char *remote)
{
    te_errno rc;
    int fd = -1;
    int in = -1;
    te_string spec = TE_STRING_INIT;
    char *chunk = NULL;
    struct stat st;
    char id[5];
    uint32_t len;

    in = open(local, O_RDONLY);
    if (in < 0 || fstat(in, &st) != 0)
    {
        ERROR("Cannot open %s: %s", local, strerror(errno));
        rc = TE_OS_RC(TE_TA_UNIX, errno);
        goto out;
    }

    rc = adb_device_connect(server, serial, &fd);
    if (rc != 0)
        goto out;
    rc = adb_request(fd, "sync:");
    if (rc != 0)
        goto out;

    te_string_append(&spec, "%s,%u", remote,
                     (unsigned int)(st.st_mode & 0777));
    rc = sync_send(fd, "SEND", spec.len, spec.ptr);
    if (rc != 0)
        goto out;

    chunk = TE_ALLOC(SYNC_CHUNK);
    for (;;)
    {
        ssize_t n = read(in, chunk, SYNC_CHUNK);

        if (n < 0)
        {
            rc = TE_OS_RC(TE_TA_UNIX, errno);
            goto out;
        }
        if (n == 0)
            break;
        rc = sync_send(fd, "DATA", n, chunk);
        if (rc != 0)
            goto out;
    }
    rc = sync_send(fd, "DONE", (uint32_t)st.st_mtime, NULL);
    if (rc != 0)
        goto out;

    rc = sync_recv(fd, id, &len);
    if (rc != 0)
        goto out;
    if (strcmp(id, "OKAY") != 0)
        rc = sync_fail(fd, len, "push");
    else
        sync_send(fd, "QUIT", 0, NULL);

out:
    free(chunk);
    te_string_free(&spec);
    if (in >= 0)
        close(in);
    if (fd >= 0)
        close(fd);
    return rc;
}

/* See description in ta_android.h */
te_errno
ta_android_pull(const char *server, const char *serial, const char *remote,
                const char *local)
{
    te_errno rc;
    int fd = -1;
    int outf = -1;
    char *chunk = NULL;
    char id[5];
    uint32_t len;

    rc = adb_device_connect(server, serial, &fd);
    if (rc != 0)
        return rc;
    rc = adb_request(fd, "sync:");
    if (rc != 0)
        goto out;

    rc = sync_send(fd, "RECV", strlen(remote), remote);
    if (rc != 0)
        goto out;

    outf = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outf < 0)
    {
        ERROR("Cannot create %s: %s", local, strerror(errno));
        rc = TE_OS_RC(TE_TA_UNIX, errno);
        goto out;
    }

    chunk = TE_ALLOC(SYNC_CHUNK);
    for (;;)
    {
        rc = sync_recv(fd, id, &len);
        if (rc != 0)
            goto out;
        if (strcmp(id, "DONE") == 0)
            break;
        if (strcmp(id, "FAIL") == 0)
        {
            rc = sync_fail(fd, len, "pull");
            goto out;
        }
        if (strcmp(id, "DATA") != 0 || len > SYNC_CHUNK)
        {
            ERROR("adb sync pull: unexpected frame %s", id);
            rc = TE_RC(TE_TA_UNIX, TE_EPROTO);
            goto out;
        }
        rc = read_exact(fd, chunk, len);
        if (rc == 0)
            rc = write_all(outf, chunk, len);
        if (rc != 0)
            goto out;
    }
    sync_send(fd, "QUIT", 0, NULL);

out:
    free(chunk);
    if (outf >= 0)
        close(outf);
    if (fd >= 0)
        close(fd);
    return rc;
}

/* See description in ta_android.h */
te_errno
ta_android_reboot(const char *server, const char *serial)
{
    te_errno rc;
    int fd;

    rc = adb_device_connect(server, serial, &fd);
    if (rc != 0)
        return rc;

    rc = adb_request(fd, "reboot:");
    close(fd);
    return rc;
}

/* A logcat capture: a thread that copies the stream into a file. */
typedef struct logcat_capture {
    bool used;              /**< Slot in use */
    int fd;                 /**< Connection to the adb server */
    pthread_t thread;       /**< Reader thread */
    char path[64];          /**< Capture file */
} logcat_capture;

static logcat_capture captures[LOGCAT_MAX];
static pthread_mutex_t captures_lock = PTHREAD_MUTEX_INITIALIZER;

static void *
logcat_thread(void *arg)
{
    logcat_capture *cap = arg;
    unsigned char hdr[5];
    char *buf = NULL;
    size_t buf_len = 0;
    int out;

    out = open(cap->path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (out < 0)
    {
        ERROR("Cannot create %s: %s", cap->path, strerror(errno));
        return NULL;
    }

    while (read_exact(cap->fd, hdr, sizeof(hdr)) == 0)
    {
        uint32_t len = hdr[1] | (hdr[2] << 8) | (hdr[3] << 16) |
                       ((uint32_t)hdr[4] << 24);

        if (len > buf_len)
        {
            free(buf);
            buf = TE_ALLOC(len);
            buf_len = len;
        }
        if (read_exact(cap->fd, buf, len) != 0)
            break;
        if (hdr[0] == SHELL_STDOUT || hdr[0] == SHELL_STDERR)
            write_all(out, buf, len);
        else if (hdr[0] == SHELL_EXIT)
            break;
    }

    free(buf);
    close(out);
    return NULL;
}

/* See description in ta_android.h */
te_errno
ta_android_logcat_start(const char *server, const char *serial,
                        const char *filter, unsigned int *id)
{
    te_errno rc;
    te_string req = TE_STRING_INIT;
    logcat_capture *cap = NULL;
    unsigned int i;

    pthread_mutex_lock(&captures_lock);
    for (i = 0; i < LOGCAT_MAX; i++)
    {
        if (!captures[i].used)
        {
            cap = &captures[i];
            cap->used = true;
            break;
        }
    }
    pthread_mutex_unlock(&captures_lock);
    if (cap == NULL)
    {
        ERROR("All %u logcat capture slots are in use", LOGCAT_MAX);
        return TE_RC(TE_TA_UNIX, TE_EBUSY);
    }

    rc = adb_device_connect(server, serial, &cap->fd);
    if (rc != 0)
        goto fail;

    te_string_append(&req, "shell,v2,raw:logcat -v threadtime%s%s",
                     filter != NULL ? " " : "", filter != NULL ? filter : "");
    rc = adb_request(cap->fd, req.ptr);
    te_string_free(&req);
    if (rc != 0)
        goto fail;

    snprintf(cap->path, sizeof(cap->path), LOGCAT_DIR "/te_logcat_%u_%u.log",
             (unsigned int)getpid(), i);
    unlink(cap->path);
    if (pthread_create(&cap->thread, NULL, logcat_thread, cap) != 0)
    {
        rc = TE_OS_RC(TE_TA_UNIX, errno);
        ERROR("Cannot start the logcat thread: %r", rc);
        goto fail;
    }

    *id = i;
    return 0;

fail:
    if (cap->fd >= 0)
        close(cap->fd);
    cap->fd = -1;
    cap->used = false;
    return rc;
}

/* See description in ta_android.h */
te_errno
ta_android_logcat_read(unsigned int id, uint64_t offset, te_string *out,
                       uint64_t *next)
{
    logcat_capture *cap;
    int fd;
    char buf[4096];
    ssize_t n;

    if (id >= LOGCAT_MAX || !captures[id].used)
        return TE_RC(TE_TA_UNIX, TE_ENOENT);
    cap = &captures[id];

    fd = open(cap->path, O_RDONLY);
    if (fd < 0)
    {
        /* The thread has not created the file yet */
        *next = offset;
        return 0;
    }
    if (lseek(fd, offset, SEEK_SET) < 0)
    {
        close(fd);
        return TE_OS_RC(TE_TA_UNIX, errno);
    }
    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        te_string_append(out, "%.*s", (int)n, buf);
        offset += n;
    }
    close(fd);

    *next = offset;
    return 0;
}

/* See description in ta_android.h */
te_errno
ta_android_logcat_stop(unsigned int id)
{
    logcat_capture *cap;

    if (id >= LOGCAT_MAX || !captures[id].used)
        return TE_RC(TE_TA_UNIX, TE_ENOENT);
    cap = &captures[id];

    /* Closing the connection ends the stream and the thread */
    shutdown(cap->fd, SHUT_RDWR);
    pthread_join(cap->thread, NULL);
    close(cap->fd);
    cap->fd = -1;
    unlink(cap->path);
    cap->used = false;
    return 0;
}
