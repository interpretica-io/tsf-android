/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android TAPI
 *
 * State and packages go through the /agent/android subtree of the
 * Configurator; commands, files and logcat go through the android_*
 * RPCs.
 */

#define TE_LGR_USER     "TAPI Android"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "te_sleep.h"
#include "logger_api.h"
#include "conf_api.h"
#include "rcf_api.h"
#include "tapi_file.h"
#include "tapi_test_log.h"

#include "tapi_android.h"
#include "tapi_android_rpc.h"

/** Time to give a device to come back after a reboot. */
#define REBOOT_TIMEOUT_MS   180000

/** Where the device keeps temporary files. */
#define DEVICE_TMP  "/sdcard"

/** Where the agent keeps files on their way to the engine. */
#define AGENT_TMP   "/tmp"

/** Pause between two reads of a logcat capture. */
#define LOGCAT_POLL_MS  500

struct tapi_android {
    rcf_rpc_server *rpcs;           /**< RPC server on the agent */
    const char *ta;                 /**< Agent name */
    char *server;                   /**< adb server, for the RPCs */
    char *serial;                   /**< Device serial */
    tapi_android_logcat *logcat;    /**< Running capture, if any */
};

struct tapi_android_logcat {
    tapi_android *dev;              /**< Device of the capture */
    unsigned int id;                /**< Capture id on the agent */
    uint64_t offset;                /**< Bytes read so far */
    te_string pending;              /**< An unfinished last line */
};

/* See description in tapi_android.h */
te_errno
tapi_android_devices(const char *ta, te_string *list)
{
    te_errno rc;
    cfg_handle *handles = NULL;
    unsigned int n = 0;
    unsigned int i;

    rc = cfg_synchronize_fmt(true, "/agent:%s/android:", ta);
    if (rc != 0)
    {
        ERROR("Cannot synchronize /agent:%s/android: %r", ta, rc);
        return rc;
    }

    rc = cfg_find_pattern_fmt(&n, &handles, "/agent:%s/android:/device:*",
                              ta);
    if (rc != 0)
        return rc;

    for (i = 0; i < n; i++)
    {
        char *name = NULL;

        rc = cfg_get_inst_name(handles[i], &name);
        if (rc != 0)
            break;
        te_string_append(list, "%s\n", name);
        free(name);
    }

    free(handles);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_set_server(const char *ta, const char *addr)
{
    return cfg_set_instance_fmt(CVT_STRING, addr,
                                "/agent:%s/android:/server:", ta);
}

/* See description in tapi_android.h */
te_errno
tapi_android_create(rcf_rpc_server *rpcs, const char *serial,
                    tapi_android **dev)
{
    te_errno rc;
    te_string list = TE_STRING_INIT;
    tapi_android *d;
    char *nl;

    rc = tapi_android_devices(rpcs->ta, &list);
    if (rc != 0)
        return rc;

    if (serial == NULL)
    {
        nl = list.ptr != NULL ? strchr(list.ptr, '\n') : NULL;
        if (nl == NULL || nl[1] != '\0')
        {
            ERROR("%s lists %s Android device, name the serial", rpcs->ta,
                  nl == NULL ? "no" : "more than one");
            te_string_free(&list);
            return TE_RC(TE_TAPI, TE_ENOENT);
        }
        *nl = '\0';
        serial = list.ptr;
    }
    else
    {
        te_string needle = TE_STRING_INIT;

        te_string_append(&needle, "%s\n", serial);
        if (list.ptr == NULL || strstr(list.ptr, needle.ptr) == NULL)
        {
            ERROR("%s does not list the Android device %s", rpcs->ta, serial);
            te_string_free(&needle);
            te_string_free(&list);
            return TE_RC(TE_TAPI, TE_ENOENT);
        }
        te_string_free(&needle);
    }

    d = TE_ALLOC(sizeof(*d));
    d->rpcs = rpcs;
    d->ta = rpcs->ta;
    d->serial = TE_STRDUP(serial);
    te_string_free(&list);

    /* The RPC server does not see the subtree: the handle carries it */
    rc = cfg_get_string(&d->server, "/agent:%s/android:/server:", rpcs->ta);
    if (rc != 0)
    {
        tapi_android_destroy(d);
        return rc;
    }

    *dev = d;
    return 0;
}

/* See description in tapi_android.h */
void
tapi_android_destroy(tapi_android *dev)
{
    if (dev == NULL)
        return;

    if (dev->logcat != NULL)
        tapi_android_logcat_stop(dev->logcat);
    free(dev->server);
    free(dev->serial);
    free(dev);
}

/* See description in tapi_android.h */
const char *
tapi_android_serial(const tapi_android *dev)
{
    return dev->serial;
}

/* Get a string leaf of the device. */
static te_errno
device_get_string(tapi_android *dev, const char *leaf, te_string *value)
{
    te_errno rc;
    char *val = NULL;

    rc = cfg_get_string(&val, "/agent:%s/android:/device:%s/%s:", dev->ta,
                        dev->serial, leaf);
    if (rc == 0)
        te_string_append(value, "%s", val);
    free(val);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_state(tapi_android *dev, te_string *state)
{
    te_errno rc;

    rc = cfg_synchronize_fmt(true, "/agent:%s/android:/device:%s", dev->ta,
                             dev->serial);
    if (rc != 0)
        return rc;
    return device_get_string(dev, "state", state);
}

/* See description in tapi_android.h */
te_errno
tapi_android_wait_for_device(tapi_android *dev, unsigned int timeout_ms)
{
    te_errno rc;
    te_string state = TE_STRING_INIT;
    unsigned int waited = 0;

    for (;;)
    {
        te_string_reset(&state);
        rc = tapi_android_state(dev, &state);
        if (rc == 0 && strcmp(state.ptr, "device") == 0)
            break;
        if (waited >= timeout_ms)
        {
            ERROR("%s is '%s' after %u ms", dev->serial,
                  rc == 0 ? state.ptr : "not listed", timeout_ms);
            rc = TE_RC(TE_TAPI, TE_ETIMEDOUT);
            break;
        }
        te_msleep(1000);
        waited += 1000;
    }

    te_string_free(&state);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_info(tapi_android *dev, te_string *model, te_string *version,
                  te_string *sdk)
{
    te_errno rc = 0;

    if (rc == 0 && model != NULL)
        rc = device_get_string(dev, "model", model);
    if (rc == 0 && version != NULL)
        rc = device_get_string(dev, "version", version);
    if (rc == 0 && sdk != NULL)
        rc = device_get_string(dev, "sdk", sdk);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_shell(tapi_android *dev, const char *cmd, te_string *out,
                   int *status)
{
    te_errno rc;
    te_string err = TE_STRING_INIT;
    int st = 0;

    rc = rpc_android_shell(dev->rpcs, dev->server, dev->serial, cmd, out,
                           &err, &st);
    if (rc != 0)
        goto out;

    if (err.len > 0)
        WARN("'%s' on %s: %s", cmd, dev->serial, err.ptr);
    if (status != NULL)
        *status = st;
    else if (st != 0)
    {
        ERROR("'%s' on %s exited with status %d", cmd, dev->serial, st);
        rc = TE_RC(TE_TAPI, TE_ESHCMD);
    }

out:
    te_string_free(&err);
    return rc;
}

/* Drop trailing newlines. */
static void
chomp(te_string *str)
{
    while (str->len > 0 && (str->ptr[str->len - 1] == '\n' ||
                            str->ptr[str->len - 1] == '\r'))
        te_string_cut(str, 1);
}

/* See description in tapi_android.h */
te_errno
tapi_android_getprop(tapi_android *dev, const char *name, te_string *value)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;
    te_string out = TE_STRING_INIT;

    te_string_append(&cmd, "getprop %s", name);
    rc = tapi_android_shell(dev, cmd.ptr, &out, NULL);
    if (rc == 0)
    {
        chomp(&out);
        te_string_append(value, "%s", out.ptr != NULL ? out.ptr : "");
    }

    te_string_free(&cmd);
    te_string_free(&out);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_is_installed(tapi_android *dev, const char *pkg,
                          bool *installed)
{
    te_errno rc;
    cfg_handle handle;

    rc = cfg_synchronize_fmt(true, "/agent:%s/android:/device:%s/app:",
                             dev->ta, dev->serial);
    if (rc != 0)
        return rc;

    rc = cfg_find_fmt(&handle, "/agent:%s/android:/device:%s/app:%s",
                      dev->ta, dev->serial, pkg);
    if (rc == 0)
        *installed = true;
    else if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
    {
        *installed = false;
        rc = 0;
    }
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_install(tapi_android *dev, const char *pkg, const char *apk)
{
    te_errno rc;

    rc = cfg_add_instance_fmt(NULL, CVT_STRING, apk,
                              "/agent:%s/android:/device:%s/app:%s",
                              dev->ta, dev->serial, pkg);
    if (rc == 0)
        RING("Installed %s from %s on %s", pkg, apk, dev->serial);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_install_from_engine(tapi_android *dev, const char *pkg,
                                 const char *apk)
{
    te_errno rc;
    te_string remote = TE_STRING_INIT;
    te_string suffix = TE_STRING_INIT;
    const char *base = strrchr(apk, '/');

    te_string_append(&suffix, "_%s", base != NULL ? base + 1 : apk);
    tapi_file_make_custom_pathname(&remote, AGENT_TMP, suffix.ptr);

    rc = rcf_ta_put_file(dev->ta, 0, apk, remote.ptr);
    if (rc != 0)
    {
        ERROR("Failed to copy %s to %s:%s: %r", apk, dev->ta, remote.ptr, rc);
        goto out;
    }

    rc = tapi_android_install(dev, pkg, remote.ptr);
    tapi_file_ta_unlink_fmt(dev->ta, "%s", remote.ptr);

out:
    te_string_free(&remote);
    te_string_free(&suffix);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_uninstall(tapi_android *dev, const char *pkg)
{
    return cfg_del_instance_fmt(false, "/agent:%s/android:/device:%s/app:%s",
                                dev->ta, dev->serial, pkg);
}

/* See description in tapi_android.h */
te_errno
tapi_android_is_running(tapi_android *dev, const char *pkg, bool *running)
{
    te_errno rc;
    int32_t val = 0;

    rc = cfg_get_int32(&val, "/agent:%s/android:/device:%s/app:%s/running:",
                       dev->ta, dev->serial, pkg);
    if (rc == 0)
        *running = val != 0;
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_start_app(tapi_android *dev, const char *pkg)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;

    te_string_append(&cmd,
                     "monkey -p %s -c android.intent.category.LAUNCHER 1",
                     pkg);
    rc = tapi_android_shell(dev, cmd.ptr, NULL, NULL);
    te_string_free(&cmd);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_start_activity(tapi_android *dev, const char *component)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;
    te_string out = TE_STRING_INIT;

    te_string_append(&cmd, "am start -n %s", component);
    rc = tapi_android_shell(dev, cmd.ptr, &out, NULL);
    if (rc == 0 && out.ptr != NULL && strstr(out.ptr, "Error") != NULL)
    {
        ERROR("am start -n %s failed: %s", component, out.ptr);
        rc = TE_RC(TE_TAPI, TE_EFAIL);
    }

    te_string_free(&cmd);
    te_string_free(&out);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_force_stop(tapi_android *dev, const char *pkg)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;

    te_string_append(&cmd, "am force-stop %s", pkg);
    rc = tapi_android_shell(dev, cmd.ptr, NULL, NULL);
    te_string_free(&cmd);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_push(tapi_android *dev, const char *src, const char *dst)
{
    return rpc_android_push(dev->rpcs, dev->server, dev->serial, src, dst);
}

/* See description in tapi_android.h */
te_errno
tapi_android_pull(tapi_android *dev, const char *src, const char *dst)
{
    return rpc_android_pull(dev->rpcs, dev->server, dev->serial, src, dst);
}

/* See description in tapi_android.h */
te_errno
tapi_android_reboot(tapi_android *dev)
{
    te_errno rc;
    te_string out = TE_STRING_INIT;
    unsigned int waited = 0;

    rc = rpc_android_reboot(dev->rpcs, dev->server, dev->serial);
    if (rc != 0)
        return rc;

    /* The device drops off adb, then comes back, then finishes booting */
    te_msleep(5000);
    rc = tapi_android_wait_for_device(dev, REBOOT_TIMEOUT_MS);
    if (rc != 0)
        return rc;

    for (;;)
    {
        te_string_reset(&out);
        if (tapi_android_shell(dev, "getprop sys.boot_completed", &out,
                               NULL) == 0 && out.ptr != NULL &&
            out.ptr[0] == '1')
            break;
        if (waited >= REBOOT_TIMEOUT_MS)
        {
            ERROR("%s did not finish booting in %u s", dev->serial,
                  REBOOT_TIMEOUT_MS / 1000);
            rc = TE_RC(TE_TAPI, TE_ETIMEDOUT);
            break;
        }
        te_msleep(5000);
        waited += 5000;
    }

    te_string_free(&out);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_tap(tapi_android *dev, int x, int y)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;

    te_string_append(&cmd, "input tap %d %d", x, y);
    rc = tapi_android_shell(dev, cmd.ptr, NULL, NULL);
    te_string_free(&cmd);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_swipe(tapi_android *dev, int x1, int y1, int x2, int y2,
                   unsigned int duration_ms)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;

    te_string_append(&cmd, "input swipe %d %d %d %d %u", x1, y1, x2, y2,
                     duration_ms);
    rc = tapi_android_shell(dev, cmd.ptr, NULL, NULL);
    te_string_free(&cmd);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_text(tapi_android *dev, const char *text)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;
    const char *p;

    /*
     * 'input text' takes one argument and the device shell splits on
     * spaces, so the text is quoted; 'input' wants a space as %s.
     */
    te_string_append(&cmd, "input text '");
    for (p = text; *p != '\0'; p++)
    {
        switch (*p)
        {
            case ' ':
                te_string_append(&cmd, "%%s");
                break;
            case '\'':
                te_string_append(&cmd, "'\\''");
                break;
            default:
                te_string_append(&cmd, "%c", *p);
        }
    }
    te_string_append(&cmd, "'");

    rc = tapi_android_shell(dev, cmd.ptr, NULL, NULL);
    te_string_free(&cmd);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_keyevent(tapi_android *dev, const char *key)
{
    te_errno rc;
    te_string cmd = TE_STRING_INIT;

    te_string_append(&cmd, "input keyevent %s", key);
    rc = tapi_android_shell(dev, cmd.ptr, NULL, NULL);
    te_string_free(&cmd);
    return rc;
}

/*
 * Bring a file from the device to the engine: pull it to the agent,
 * copy it over with RCF, remove both copies.
 */
static te_errno
fetch_from_device(tapi_android *dev, const char *device_path,
                  const char *suffix, te_string *engine_path)
{
    te_errno rc;
    te_string agent_path = TE_STRING_INIT;
    te_string rm = TE_STRING_INIT;
    const char *dir = getenv("TE_LOG_DIR");

    if (dir == NULL)
        dir = getenv("TE_TMP");
    if (dir == NULL)
    {
        ERROR("Neither TE_LOG_DIR nor TE_TMP is set, nowhere to put "
              "the file");
        return TE_RC(TE_TAPI, TE_ENOENT);
    }

    tapi_file_make_custom_pathname(&agent_path, AGENT_TMP, suffix);
    tapi_file_make_custom_pathname(engine_path, dir, suffix);

    rc = tapi_android_pull(dev, device_path, agent_path.ptr);
    if (rc != 0)
        goto out;

    rc = rcf_ta_get_file(dev->ta, 0, agent_path.ptr, engine_path->ptr);
    if (rc != 0)
    {
        ERROR("Failed to copy %s:%s to %s: %r", dev->ta, agent_path.ptr,
              engine_path->ptr, rc);
    }
    tapi_file_ta_unlink_fmt(dev->ta, "%s", agent_path.ptr);

out:
    te_string_append(&rm, "rm -f %s", device_path);
    tapi_android_shell(dev, rm.ptr, NULL, NULL);
    te_string_free(&agent_path);
    te_string_free(&rm);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_screenshot(tapi_android *dev, const char *name, te_string *path)
{
    te_errno rc;
    te_string device_path = TE_STRING_INIT;
    te_string suffix = TE_STRING_INIT;
    te_string local = TE_STRING_INIT;
    te_string cmd = TE_STRING_INIT;

    te_string_append(&suffix, "_%s.png", name);
    tapi_file_make_custom_pathname(&device_path, DEVICE_TMP, suffix.ptr);

    te_string_append(&cmd, "screencap -p %s", device_path.ptr);
    rc = tapi_android_shell(dev, cmd.ptr, NULL, NULL);
    if (rc != 0)
        goto out;

    rc = fetch_from_device(dev, device_path.ptr, suffix.ptr, &local);
    if (rc != 0)
        goto out;

    RING_ARTIFACT("Screenshot '%s': %s", name, local.ptr);
    if (path != NULL)
        te_string_append(path, "%s", local.ptr);

out:
    te_string_free(&device_path);
    te_string_free(&suffix);
    te_string_free(&local);
    te_string_free(&cmd);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_ui_dump(tapi_android *dev, te_string *xml)
{
    te_errno rc;
    te_string device_path = TE_STRING_INIT;
    te_string cmd = TE_STRING_INIT;

    tapi_file_make_custom_pathname(&device_path, DEVICE_TMP, "_ui.xml");

    /* 'uiautomator dump' prints its own status line, so cat the file */
    te_string_append(&cmd, "uiautomator dump %s >/dev/null && cat %s && "
                     "rm -f %s", device_path.ptr, device_path.ptr,
                     device_path.ptr);
    rc = tapi_android_shell(dev, cmd.ptr, xml, NULL);

    te_string_free(&device_path);
    te_string_free(&cmd);
    return rc;
}

/* Does a node carry the text and the resource id asked for? */
static bool
node_matches(xmlNodePtr node, const char *text, const char *resource_id)
{
    xmlChar *value;
    bool match = true;

    if (text != NULL)
    {
        value = xmlGetProp(node, (const xmlChar *)"text");
        match = value != NULL && strcmp((const char *)value, text) == 0;
        xmlFree(value);
    }
    if (match && resource_id != NULL)
    {
        value = xmlGetProp(node, (const xmlChar *)"resource-id");
        match = value != NULL &&
                strcmp((const char *)value, resource_id) == 0;
        xmlFree(value);
    }
    return match;
}

/* Depth-first search for the first matching node. */
static xmlNodePtr
find_node(xmlNodePtr node, const char *text, const char *resource_id)
{
    xmlNodePtr found;

    for (; node != NULL; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(node->name, (const xmlChar *)"node") == 0 &&
            node_matches(node, text, resource_id))
            return node;
        found = find_node(node->children, text, resource_id);
        if (found != NULL)
            return found;
    }
    return NULL;
}

/* See description in tapi_android.h */
te_errno
tapi_android_ui_find(tapi_android *dev, const char *text,
                     const char *resource_id, tapi_android_bounds *bounds)
{
    te_errno rc;
    te_string xml = TE_STRING_INIT;
    xmlDocPtr doc = NULL;
    xmlNodePtr node;
    xmlChar *value = NULL;

    if (text == NULL && resource_id == NULL)
    {
        ERROR("UI element lookup needs a text or a resource id");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    rc = tapi_android_ui_dump(dev, &xml);
    if (rc != 0)
        goto out;

    doc = xmlReadMemory(xml.ptr, xml.len, "ui.xml", NULL,
                        XML_PARSE_NONET | XML_PARSE_NOWARNING);
    if (doc == NULL)
    {
        ERROR("Cannot parse the UI dump of %s", dev->serial);
        rc = TE_RC(TE_TAPI, TE_EPROTO);
        goto out;
    }

    node = find_node(xmlDocGetRootElement(doc), text, resource_id);
    if (node == NULL)
    {
        ERROR("No UI element with text '%s' and resource id '%s'",
              text != NULL ? text : "*",
              resource_id != NULL ? resource_id : "*");
        rc = TE_RC(TE_TAPI, TE_ENOENT);
        goto out;
    }

    value = xmlGetProp(node, (const xmlChar *)"bounds");
    if (value == NULL ||
        sscanf((const char *)value, "[%d,%d][%d,%d]", &bounds->left,
               &bounds->top, &bounds->right, &bounds->bottom) != 4)
    {
        ERROR("UI element has no usable bounds: %s",
              value != NULL ? (const char *)value : "(none)");
        rc = TE_RC(TE_TAPI, TE_EPROTO);
    }

out:
    xmlFree(value);
    if (doc != NULL)
        xmlFreeDoc(doc);
    te_string_free(&xml);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_ui_tap(tapi_android *dev, const char *text,
                    const char *resource_id)
{
    te_errno rc;
    tapi_android_bounds b;

    rc = tapi_android_ui_find(dev, text, resource_id, &b);
    if (rc != 0)
        return rc;

    RING("Tap '%s' at (%d, %d)", text != NULL ? text : resource_id,
         (b.left + b.right) / 2, (b.top + b.bottom) / 2);
    return tapi_android_tap(dev, (b.left + b.right) / 2,
                            (b.top + b.bottom) / 2);
}

/* See description in tapi_android.h */
te_errno
tapi_android_logcat_start(tapi_android *dev, const char *filter,
                          tapi_android_logcat **lc)
{
    te_errno rc;
    tapi_android_logcat *l;

    if (dev->logcat != NULL)
    {
        ERROR("A logcat capture of %s is running already", dev->serial);
        return TE_RC(TE_TAPI, TE_EALREADY);
    }

    l = TE_ALLOC(sizeof(*l));
    l->dev = dev;
    l->pending = (te_string)TE_STRING_INIT;

    rc = rpc_android_logcat_start(dev->rpcs, dev->server, dev->serial,
                                  filter, &l->id);
    if (rc != 0)
    {
        free(l);
        return rc;
    }

    dev->logcat = l;
    *lc = l;
    return 0;
}

/* See description in tapi_android.h */
te_errno
tapi_android_logcat_expect(tapi_android_logcat *lc, const char *re,
                           unsigned int timeout_ms, te_string *line)
{
    te_errno rc = 0;
    regex_t regex;
    te_string chunk = TE_STRING_INIT;
    unsigned int waited = 0;
    bool found = false;
    int err;

    err = regcomp(&regex, re, REG_EXTENDED | REG_NOSUB);
    if (err != 0)
    {
        char msg[128];

        regerror(err, &regex, msg, sizeof(msg));
        ERROR("Bad regular expression '%s': %s", re, msg);
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    while (!found)
    {
        char *start;
        char *nl;

        te_string_reset(&chunk);
        rc = rpc_android_logcat_read(lc->dev->rpcs, lc->id, lc->offset,
                                     &chunk, &lc->offset);
        if (rc != 0)
            break;
        if (chunk.ptr != NULL)
            te_string_append(&lc->pending, "%s", chunk.ptr);

        /* Match whole lines only; keep an unfinished tail for later */
        start = lc->pending.ptr;
        while (start != NULL && (nl = strchr(start, '\n')) != NULL)
        {
            *nl = '\0';
            if (regexec(&regex, start, 0, NULL, 0) == 0)
            {
                RING("logcat: %s", start);
                if (line != NULL)
                    te_string_append(line, "%s", start);
                found = true;
                start = nl + 1;
                break;
            }
            start = nl + 1;
        }
        if (start != NULL && start != lc->pending.ptr)
        {
            te_string rest = TE_STRING_INIT;

            te_string_append(&rest, "%s", start);
            te_string_free(&lc->pending);
            lc->pending = rest;
        }

        if (!found)
        {
            if (waited >= timeout_ms)
            {
                ERROR("No logcat line matching '%s' within %u ms", re,
                      timeout_ms);
                rc = TE_RC(TE_TAPI, TE_ETIMEDOUT);
                break;
            }
            te_msleep(LOGCAT_POLL_MS);
            waited += LOGCAT_POLL_MS;
        }
    }

    regfree(&regex);
    te_string_free(&chunk);
    return rc;
}

/* See description in tapi_android.h */
te_errno
tapi_android_logcat_stop(tapi_android_logcat *lc)
{
    te_errno rc;

    if (lc == NULL)
        return 0;

    rc = rpc_android_logcat_stop(lc->dev->rpcs, lc->id);
    lc->dev->logcat = NULL;
    te_string_free(&lc->pending);
    free(lc);
    return rc;
}
