/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android TAPI
 *
 * Client wrappers of the android_* RPCs, see android_rpc.x.m4. Tests
 * use tapi_android.h; these are the calls behind it.
 */

#ifndef __TAPI_ANDROID_RPC_H__
#define __TAPI_ANDROID_RPC_H__

#include "te_errno.h"
#include "te_string.h"
#include "rcf_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The device RPCs take the adb server address ("host:port", NULL for
 * the default): the RPC server does not see /agent/android/server.
 */

/** Run a shell command on a device, see ta_android_shell(). */
extern te_errno rpc_android_shell(rcf_rpc_server *rpcs, const char *server,
                                  const char *serial, const char *cmd,
                                  te_string *out, te_string *err,
                                  int *status);

/** Copy a file from the agent to a device. */
extern te_errno rpc_android_push(rcf_rpc_server *rpcs, const char *server,
                                 const char *serial, const char *local,
                                 const char *remote);

/** Copy a file from a device to the agent. */
extern te_errno rpc_android_pull(rcf_rpc_server *rpcs, const char *server,
                                 const char *serial, const char *remote,
                                 const char *local);

/** Reboot a device. */
extern te_errno rpc_android_reboot(rcf_rpc_server *rpcs, const char *server,
                                   const char *serial);

/** Start a logcat capture on the agent. */
extern te_errno rpc_android_logcat_start(rcf_rpc_server *rpcs,
                                         const char *server,
                                         const char *serial,
                                         const char *filter,
                                         unsigned int *id);

/** Read captured logcat bytes from an offset. */
extern te_errno rpc_android_logcat_read(rcf_rpc_server *rpcs,
                                        unsigned int id, uint64_t offset,
                                        te_string *data, uint64_t *next);

/** Stop a logcat capture. */
extern te_errno rpc_android_logcat_stop(rcf_rpc_server *rpcs,
                                        unsigned int id);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TAPI_ANDROID_RPC_H__ */
