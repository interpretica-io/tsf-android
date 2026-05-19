/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android TAPI
 *
 * Client wrappers of the android_* RPCs.
 */

#define TE_LGR_USER     "TAPI Android RPC"

#include "te_config.h"

#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "logger_api.h"
#include "tapi_rpc_internal.h"
#include "tarpc.h"

#include "tapi_android_rpc.h"

/* The RPCs return te_errno; an RPC transport failure is TE_ECORRUPTED. */
#define CHECK_RPC_ERRNO_UNCHANGED(_func, _var) \
    CHECK_RETVAL_VAR_ERR_COND(_func, _var, false,                    \
                              TE_RC(TE_TAPI, TE_ECORRUPTED), false)

/* Append an RPC string result, when there is one. */
static void
take_string(te_string *dst, const char *src)
{
    if (dst != NULL && src != NULL)
        te_string_append(dst, "%s", src);
}

/* See description in tapi_android_rpc.h */
te_errno
rpc_android_shell(rcf_rpc_server *rpcs, const char *server,
                  const char *serial, const char *cmd, te_string *sout,
                  te_string *serr, int *status)
{
    tarpc_android_shell_in in;
    tarpc_android_shell_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.server = (char *)(server != NULL ? server : "");
    in.serial = (char *)(serial != NULL ? serial : "");
    in.cmd = (char *)cmd;

    rcf_rpc_call(rpcs, "android_shell", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(android_shell, out.retval);
    TAPI_RPC_LOG(rpcs, android_shell, "%s, \"%s\"", "%r status=%d",
                 in.serial, cmd, out.retval, out.status);

    if (out.retval == 0)
    {
        take_string(sout, out.out);
        take_string(serr, out.err);
        *status = out.status;
    }
    RETVAL_TE_ERRNO(android_shell, out.retval);
}

/* See description in tapi_android_rpc.h */
te_errno
rpc_android_push(rcf_rpc_server *rpcs, const char *server,
                 const char *serial, const char *local, const char *remote)
{
    tarpc_android_push_in in;
    tarpc_android_push_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.server = (char *)(server != NULL ? server : "");
    in.serial = (char *)(serial != NULL ? serial : "");
    in.local = (char *)local;
    in.remote = (char *)remote;

    rcf_rpc_call(rpcs, "android_push", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(android_push, out.retval);
    TAPI_RPC_LOG(rpcs, android_push, "%s, %s -> %s", "%r", in.serial,
                 local, remote, out.retval);
    RETVAL_TE_ERRNO(android_push, out.retval);
}

/* See description in tapi_android_rpc.h */
te_errno
rpc_android_pull(rcf_rpc_server *rpcs, const char *server,
                 const char *serial, const char *remote, const char *local)
{
    tarpc_android_pull_in in;
    tarpc_android_pull_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.server = (char *)(server != NULL ? server : "");
    in.serial = (char *)(serial != NULL ? serial : "");
    in.remote = (char *)remote;
    in.local = (char *)local;

    rcf_rpc_call(rpcs, "android_pull", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(android_pull, out.retval);
    TAPI_RPC_LOG(rpcs, android_pull, "%s, %s -> %s", "%r", in.serial,
                 remote, local, out.retval);
    RETVAL_TE_ERRNO(android_pull, out.retval);
}

/* See description in tapi_android_rpc.h */
te_errno
rpc_android_reboot(rcf_rpc_server *rpcs, const char *server,
                   const char *serial)
{
    tarpc_android_reboot_in in;
    tarpc_android_reboot_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.server = (char *)(server != NULL ? server : "");
    in.serial = (char *)(serial != NULL ? serial : "");

    rcf_rpc_call(rpcs, "android_reboot", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(android_reboot, out.retval);
    TAPI_RPC_LOG(rpcs, android_reboot, "%s", "%r", in.serial, out.retval);
    RETVAL_TE_ERRNO(android_reboot, out.retval);
}

/* See description in tapi_android_rpc.h */
te_errno
rpc_android_logcat_start(rcf_rpc_server *rpcs, const char *server,
                         const char *serial, const char *filter,
                         unsigned int *id)
{
    tarpc_android_logcat_start_in in;
    tarpc_android_logcat_start_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.server = (char *)(server != NULL ? server : "");
    in.serial = (char *)(serial != NULL ? serial : "");
    in.filter = (char *)(filter != NULL ? filter : "");

    rcf_rpc_call(rpcs, "android_logcat_start", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(android_logcat_start, out.retval);
    TAPI_RPC_LOG(rpcs, android_logcat_start, "%s, \"%s\"", "%r id=%u",
                 in.serial, in.filter, out.retval, out.id);

    if (out.retval == 0)
        *id = out.id;
    RETVAL_TE_ERRNO(android_logcat_start, out.retval);
}

/* See description in tapi_android_rpc.h */
te_errno
rpc_android_logcat_read(rcf_rpc_server *rpcs, unsigned int id,
                        uint64_t offset, te_string *data, uint64_t *next)
{
    tarpc_android_logcat_read_in in;
    tarpc_android_logcat_read_out out;
    bool silent = rpcs->silent;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.id = id;
    in.offset = offset;

    /* Polled in a loop: one log line per poll would swamp the log */
    rpcs->silent = true;
    rcf_rpc_call(rpcs, "android_logcat_read", &in, &out);
    rpcs->silent = silent;
    CHECK_RPC_ERRNO_UNCHANGED(android_logcat_read, out.retval);

    if (out.retval == 0)
    {
        take_string(data, out.data);
        *next = out.next;
    }
    else
    {
        ERROR("android_logcat_read(%u, %llu) failed: %r", id,
              (unsigned long long)offset, out.retval);
    }
    RETVAL_TE_ERRNO(android_logcat_read, out.retval);
}

/* See description in tapi_android_rpc.h */
te_errno
rpc_android_logcat_stop(rcf_rpc_server *rpcs, unsigned int id)
{
    tarpc_android_logcat_stop_in in;
    tarpc_android_logcat_stop_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.id = id;

    rcf_rpc_call(rpcs, "android_logcat_stop", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(android_logcat_stop, out.retval);
    TAPI_RPC_LOG(rpcs, android_logcat_stop, "%u", "%r", id, out.retval);
    RETVAL_TE_ERRNO(android_logcat_stop, out.retval);
}
