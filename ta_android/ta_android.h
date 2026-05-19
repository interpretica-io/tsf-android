/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android agent library
 *
 * @defgroup ta_android Android device access on a Test Agent (ta_android)
 * @{
 *
 * Client of the adb server protocol. The library talks to the adb
 * server (adb start-server) over TCP and runs device services
 * itself: shell commands, file transfer, reboot, logcat. No adb
 * process is started per operation.
 *
 * The library keeps no state about the server: every call names
 * it, since the /agent/android subtree (ta_android_conf) and the
 * android_* RPCs (rpcs_android) run in different processes.
 */

#ifndef __TA_ANDROID_H__
#define __TA_ANDROID_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default adb server address. */
#define TA_ANDROID_SERVER_DEFAULT   "127.0.0.1:5037"

/**
 * Check an adb server address.
 *
 * @param addr  "host:port"
 *
 * @return Status code (TE_EINVAL when the address has no port).
 */
extern te_errno ta_android_check_server(const char *addr);

/**
 * List the devices the adb server knows: one "serial\tstate" line
 * per device.
 *
 * @param      server  adb server "host:port" (@c NULL: the default)
 * @param[out] out     Lines (appended)
 *
 * @return Status code.
 */
extern te_errno ta_android_devices(const char *server, te_string *out);

/**
 * Get the state of a device as the adb server reports it.
 *
 * @param      server  adb server "host:port" (@c NULL: the default)
 * @param      serial  Device serial
 * @param[out] state   "device", "offline", "unauthorized", ...
 *                     (appended)
 *
 * @return Status code (TE_ENOENT when the server does not list it).
 */
extern te_errno ta_android_state(const char *server, const char *serial,
                                 te_string *state);

/**
 * Run a shell command on a device with the shell v2 protocol.
 *
 * @param      server  adb server "host:port" (@c NULL: the default)
 * @param      serial  Device serial (@c NULL: the only device)
 * @param      cmd     Command line for the device shell
 * @param[out] out     Standard output (appended; may be @c NULL)
 * @param[out] err     Standard error (appended; may be @c NULL)
 * @param[out] status  Exit status of the command
 *
 * @return Status code.
 */
extern te_errno ta_android_shell(const char *server, const char *serial,
                                 const char *cmd, te_string *out,
                                 te_string *err, int *status);

/**
 * Copy a file from the agent to the device.
 *
 * @param server  adb server "host:port" (@c NULL: the default)
 * @param serial  Device serial (@c NULL: the only device)
 * @param local   Path on the agent
 * @param remote  Path on the device
 *
 * @return Status code.
 */
extern te_errno ta_android_push(const char *server, const char *serial,
                                const char *local, const char *remote);

/**
 * Copy a file from the device to the agent.
 *
 * @param server  adb server "host:port" (@c NULL: the default)
 * @param serial  Device serial (@c NULL: the only device)
 * @param remote  Path on the device
 * @param local   Path on the agent
 *
 * @return Status code.
 */
extern te_errno ta_android_pull(const char *server, const char *serial,
                                const char *remote, const char *local);

/**
 * Reboot a device.
 *
 * @param server  adb server "host:port" (@c NULL: the default)
 * @param serial  Device serial (@c NULL: the only device)
 *
 * @return Status code.
 */
extern te_errno ta_android_reboot(const char *server, const char *serial);

/**
 * Start logcat on a device and collect its lines into a file on the
 * agent from a thread.
 *
 * @param      server  adb server "host:port" (@c NULL: the default)
 * @param      serial  Device serial (@c NULL: the only device)
 * @param      filter  logcat filter spec (@c NULL: everything)
 * @param[out] id      Identifier of the capture
 *
 * @return Status code.
 */
extern te_errno ta_android_logcat_start(const char *server,
                                        const char *serial,
                                        const char *filter,
                                        unsigned int *id);

/**
 * Read the captured logcat lines from an offset.
 *
 * @param      id      Capture identifier
 * @param      offset  Byte offset in the capture
 * @param[out] out     Bytes from the offset (appended)
 * @param[out] next    Offset after the returned bytes
 *
 * @return Status code.
 */
extern te_errno ta_android_logcat_read(unsigned int id, uint64_t offset,
                                       te_string *out, uint64_t *next);

/**
 * Stop a logcat capture and remove its file.
 *
 * @param id    Capture identifier
 *
 * @return Status code.
 */
extern te_errno ta_android_logcat_stop(unsigned int id);

/**
 * Initializer of the /agent/android subtree, registered with
 * TE_RCF_PCH_CONF_EXT().
 *
 * @return Status code.
 */
extern te_errno ta_android_conf_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TA_ANDROID_H__ */

/**@} <!-- END ta_android --> */
