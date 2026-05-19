/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android RPC server library
 *
 * The android_* RPCs (see android_rpc.x.m4) on top of ta_android.
 * TARPC_FUNC_STATIC() binds an RPC to the function of the same name,
 * so each RPC has a plain C function first and the wrapper after it.
 */

#define TE_LGR_USER     "RPC Android"

#include "te_config.h"

#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "rpc_server.h"

#include "ta_android.h"

/* An empty RPC string means "not given". */
static const char *
opt(const char *s)
{
    return (s == NULL || *s == '\0') ? NULL : s;
}

static te_errno
android_shell(const char *server, const char *serial, const char *cmd,
              char **out, char **err, int *status)
{
    te_errno rc;
    te_string sout = TE_STRING_INIT;
    te_string serr = TE_STRING_INIT;

    rc = ta_android_shell(opt(server), opt(serial), cmd, &sout, &serr,
                          status);
    *out = sout.ptr != NULL ? sout.ptr : TE_STRDUP("");
    *err = serr.ptr != NULL ? serr.ptr : TE_STRDUP("");
    return rc;
}

TARPC_FUNC_STATIC(android_shell, {},
{
    MAKE_CALL(out->retval = func(in->server, in->serial, in->cmd, &out->out,
                                 &out->err, &out->status));
    out->common.errno_changed = false;
})

static te_errno
android_push(const char *server, const char *serial, const char *local,
             const char *remote)
{
    return ta_android_push(opt(server), opt(serial), local, remote);
}

TARPC_FUNC_STATIC(android_push, {},
{
    MAKE_CALL(out->retval = func(in->server, in->serial, in->local,
                                 in->remote));
    out->common.errno_changed = false;
})

static te_errno
android_pull(const char *server, const char *serial, const char *remote,
             const char *local)
{
    return ta_android_pull(opt(server), opt(serial), remote, local);
}

TARPC_FUNC_STATIC(android_pull, {},
{
    MAKE_CALL(out->retval = func(in->server, in->serial, in->remote,
                                 in->local));
    out->common.errno_changed = false;
})

static te_errno
android_reboot(const char *server, const char *serial)
{
    return ta_android_reboot(opt(server), opt(serial));
}

TARPC_FUNC_STATIC(android_reboot, {},
{
    MAKE_CALL(out->retval = func(in->server, in->serial));
    out->common.errno_changed = false;
})

static te_errno
android_logcat_start(const char *server, const char *serial,
                     const char *filter, unsigned int *id)
{
    return ta_android_logcat_start(opt(server), opt(serial), opt(filter),
                                   id);
}

TARPC_FUNC_STATIC(android_logcat_start, {},
{
    MAKE_CALL(out->retval = func(in->server, in->serial, in->filter,
                                 &out->id));
    out->common.errno_changed = false;
})

static te_errno
android_logcat_read(unsigned int id, uint64_t offset, char **data,
                    uint64_t *next)
{
    te_errno rc;
    te_string out = TE_STRING_INIT;

    rc = ta_android_logcat_read(id, offset, &out, next);
    *data = out.ptr != NULL ? out.ptr : TE_STRDUP("");
    return rc;
}

TARPC_FUNC_STATIC(android_logcat_read, {},
{
    MAKE_CALL(out->retval = func(in->id, in->offset, &out->data,
                                 &out->next));
    out->common.errno_changed = false;
})

static te_errno
android_logcat_stop(unsigned int id)
{
    return ta_android_logcat_stop(id);
}

TARPC_FUNC_STATIC(android_logcat_stop, {},
{
    MAKE_CALL(out->retval = func(in->id));
    out->common.errno_changed = false;
})
