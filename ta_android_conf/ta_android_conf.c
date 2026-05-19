/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android agent library
 *
 * The /agent/android configuration subtree:
 *
 *   /agent/android/server                 adb server address (RW)
 *   /agent/android/device:<serial>        devices the server lists (RO)
 *   /agent/android/device/state           "device", "offline", ... (RO)
 *   /agent/android/device/model           ro.product.model (RO)
 *   /agent/android/device/version         ro.build.version.release (RO)
 *   /agent/android/device/sdk             ro.build.version.sdk (RO)
 *   /agent/android/device/app:<package>   installed packages; add with
 *                                         an APK path on the agent as
 *                                         the value installs, delete
 *                                         uninstalls (read_create)
 *   /agent/android/device/app/running     1 when a process of the
 *                                         package runs (RO)
 *
 * The model is in cm_android.yml of tapi_android.
 */

#define TE_LGR_USER     "TA Android Conf"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_str.h"
#include "te_string.h"
#include "logger_api.h"
#include "rcf_pch.h"
#include "rcf_pch_conf_ext.h"

#include "ta_android.h"

/** Where an APK goes on the device before 'pm install'. */
#define DEVICE_STAGING  "/data/local/tmp"

/** The adb server the subtree talks to, /agent/android/server. */
static char server[64] = TA_ANDROID_SERVER_DEFAULT;

/* Run a shell command on a device and keep its output as a value. */
static te_errno
shell_value(const char *serial, const char *cmd, char *value)
{
    te_errno rc;
    te_string out = TE_STRING_INIT;
    int status = 0;

    rc = ta_android_shell(server, serial, cmd, &out, NULL, &status);
    if (rc == 0 && status != 0)
    {
        ERROR("'%s' on %s exited with status %d", cmd, serial, status);
        rc = TE_RC(TE_TA_UNIX, TE_ESHCMD);
    }
    if (rc == 0)
    {
        while (out.len > 0 && (out.ptr[out.len - 1] == '\n' ||
                               out.ptr[out.len - 1] == '\r'))
            te_string_cut(&out, 1);
        te_strlcpy(value, out.ptr != NULL ? out.ptr : "", RCF_MAX_VAL);
    }

    te_string_free(&out);
    return rc;
}

static te_errno
server_get(unsigned int gid, const char *oid, char *value)
{
    UNUSED(gid);
    UNUSED(oid);

    te_strlcpy(value, server, RCF_MAX_VAL);
    return 0;
}

static te_errno
server_set(unsigned int gid, const char *oid, const char *value)
{
    te_errno rc;

    UNUSED(gid);
    UNUSED(oid);

    rc = ta_android_check_server(value);
    if (rc != 0)
    {
        ERROR("'%s' is not an adb server address (host:port)", value);
        return rc;
    }
    te_strlcpy(server, value, sizeof(server));
    return 0;
}

static te_errno
device_list(unsigned int gid, const char *oid, const char *sub_id,
            char **list)
{
    te_errno rc;
    te_string devices = TE_STRING_INIT;
    te_string names = TE_STRING_INIT;
    char *line;
    char *save = NULL;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(sub_id);

    rc = ta_android_devices(server, &devices);
    if (rc != 0)
    {
        /* No adb server is not an error of the tree: no devices */
        VERB("Cannot list Android devices: %r", rc);
        *list = NULL;
        te_string_free(&devices);
        return 0;
    }

    /* An empty answer leaves the string unallocated */
    for (line = devices.ptr != NULL ?
                strtok_r(devices.ptr, "\n", &save) : NULL;
         line != NULL; line = strtok_r(NULL, "\n", &save))
    {
        char *tab = strchr(line, '\t');

        if (tab == NULL)
            continue;
        *tab = '\0';
        te_string_append(&names, "%s%s", names.len > 0 ? " " : "", line);
    }

    *list = names.ptr;
    te_string_free(&devices);
    return 0;
}

static te_errno
state_get(unsigned int gid, const char *oid, char *value,
          const char *android, const char *serial)
{
    te_errno rc;
    te_string state = TE_STRING_INIT;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);

    rc = ta_android_state(server, serial, &state);
    if (rc == 0)
        te_strlcpy(value, state.ptr, RCF_MAX_VAL);
    te_string_free(&state);
    return rc;
}

static te_errno
model_get(unsigned int gid, const char *oid, char *value,
          const char *android, const char *serial)
{
    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);

    return shell_value(serial, "getprop ro.product.model", value);
}

static te_errno
version_get(unsigned int gid, const char *oid, char *value,
            const char *android, const char *serial)
{
    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);

    return shell_value(serial, "getprop ro.build.version.release", value);
}

static te_errno
sdk_get(unsigned int gid, const char *oid, char *value,
        const char *android, const char *serial)
{
    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);

    return shell_value(serial, "getprop ro.build.version.sdk", value);
}

static te_errno
app_list(unsigned int gid, const char *oid, const char *sub_id, char **list,
         const char *android, const char *serial)
{
    te_errno rc;
    te_string out = TE_STRING_INIT;
    te_string names = TE_STRING_INIT;
    char *line;
    char *save = NULL;
    int status = 0;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(sub_id);
    UNUSED(android);

    rc = ta_android_shell(server, serial, "pm list packages", &out, NULL,
                          &status);
    if (rc != 0 || status != 0)
    {
        te_string_free(&out);
        return rc != 0 ? rc : TE_RC(TE_TA_UNIX, TE_ESHCMD);
    }

    for (line = out.ptr != NULL ? strtok_r(out.ptr, "\n", &save) : NULL;
         line != NULL; line = strtok_r(NULL, "\n", &save))
    {
        const char *pkg = te_str_strip_prefix(line, "package:");

        if (pkg == NULL)
            continue;
        te_string_append(&names, "%s%s", names.len > 0 ? " " : "", pkg);
    }

    *list = names.ptr;
    te_string_free(&out);
    return 0;
}

static te_errno
app_get(unsigned int gid, const char *oid, char *value,
        const char *android, const char *serial, const char *pkg)
{
    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);
    UNUSED(serial);
    UNUSED(pkg);

    /* The APK path is not kept: the package is the fact */
    *value = '\0';
    return 0;
}

static te_errno
app_add(unsigned int gid, const char *oid, const char *value,
        const char *android, const char *serial, const char *pkg)
{
    te_errno rc;
    te_string staged = TE_STRING_INIT;
    te_string cmd = TE_STRING_INIT;
    te_string out = TE_STRING_INIT;
    const char *base;
    int status = 0;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);

    if (value == NULL || *value == '\0')
    {
        ERROR("Adding app %s needs an APK path on the agent as the value",
              pkg);
        return TE_RC(TE_TA_UNIX, TE_EINVAL);
    }

    base = strrchr(value, '/');
    te_string_append(&staged, DEVICE_STAGING "/%s",
                     base != NULL ? base + 1 : value);

    rc = ta_android_push(server, serial, value, staged.ptr);
    if (rc != 0)
        goto out;

    te_string_append(&cmd, "pm install -r %s", staged.ptr);
    rc = ta_android_shell(server, serial, cmd.ptr, &out, &out, &status);
    if (rc == 0 && (status != 0 || strstr(out.ptr, "Success") == NULL))
    {
        ERROR("Installing %s from %s on %s failed: %s", pkg, value, serial,
              out.ptr != NULL ? out.ptr : "");
        rc = TE_RC(TE_TA_UNIX, TE_EFAIL);
    }

    te_string_reset(&cmd);
    te_string_append(&cmd, "rm -f %s", staged.ptr);
    ta_android_shell(server, serial, cmd.ptr, NULL, NULL, &status);

out:
    te_string_free(&staged);
    te_string_free(&cmd);
    te_string_free(&out);
    return rc;
}

static te_errno
app_del(unsigned int gid, const char *oid, const char *android,
        const char *serial, const char *pkg)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;
    te_string out = TE_STRING_INIT;
    int status = 0;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);

    te_string_append(&cmd, "pm uninstall %s", pkg);
    rc = ta_android_shell(server, serial, cmd.ptr, &out, &out, &status);
    if (rc == 0 && (status != 0 || strstr(out.ptr, "Success") == NULL))
    {
        ERROR("Uninstalling %s on %s failed: %s", pkg, serial,
              out.ptr != NULL ? out.ptr : "");
        rc = TE_RC(TE_TA_UNIX, TE_EFAIL);
    }

    te_string_free(&cmd);
    te_string_free(&out);
    return rc;
}

static te_errno
app_running_get(unsigned int gid, const char *oid, char *value,
                const char *android, const char *serial, const char *pkg)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;
    int status = 0;

    UNUSED(gid);
    UNUSED(oid);
    UNUSED(android);

    te_string_append(&cmd, "pidof -s %s", pkg);
    rc = ta_android_shell(server, serial, cmd.ptr, NULL, NULL, &status);
    if (rc == 0)
        te_strlcpy(value, status == 0 ? "1" : "0", RCF_MAX_VAL);

    te_string_free(&cmd);
    return rc;
}

RCF_PCH_CFG_NODE_RO(node_app_running, "running", NULL, NULL,
                    app_running_get);

RCF_PCH_CFG_NODE_COLLECTION(node_app, "app", &node_app_running, NULL,
                            app_add, app_del, app_list, NULL);

RCF_PCH_CFG_NODE_RO(node_sdk, "sdk", NULL, &node_app, sdk_get);
RCF_PCH_CFG_NODE_RO(node_version, "version", NULL, &node_sdk, version_get);
RCF_PCH_CFG_NODE_RO(node_model, "model", NULL, &node_version, model_get);
RCF_PCH_CFG_NODE_RO(node_state, "state", NULL, &node_model, state_get);

RCF_PCH_CFG_NODE_RO_COLLECTION(node_device, "device", &node_state, NULL,
                               NULL, device_list);

RCF_PCH_CFG_NODE_RW(node_server, "server", NULL, &node_device,
                    server_get, server_set);

RCF_PCH_CFG_NODE_NA(node_android, "android", &node_server, NULL);

/* See description in ta_android.h */
te_errno
ta_android_conf_init(void)
{
    /* The app collection reads its value through a getter */
    node_app.get = (rcf_ch_cfg_get)app_get;

    return rcf_pch_add_node("/agent", &node_android);
}

TE_RCF_PCH_CONF_EXT(ta_android_conf_init);
