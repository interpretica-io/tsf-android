/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Android TAPI
 *
 * @defgroup tapi_android Android device control (tapi_android)
 * @{
 *
 * Engine-side TAPI for an Android device attached to a Test Agent.
 * The agent runs ta_android, a client of the adb server protocol;
 * the state of the devices is in the /agent/android subtree of the
 * Configurator and the actions go through the android_* RPCs.
 *
 * The agent host needs the adb server running ('adb start-server')
 * and the device attached with USB debugging on. The agent needs
 * ta_android in its libraries and rpcs_android in its RPC server.
 *
 * @code
 * rcf_rpc_server *rpcs;
 * tapi_android *dev;
 *
 * CHECK_RC(rcf_rpc_server_create(ta, "pco_android", &rpcs));
 * CHECK_RC(tapi_android_create(rpcs, "R58M12ABCDE", &dev));
 * CHECK_RC(tapi_android_install(dev, "com.example.app", "/opt/app.apk"));
 * CHECK_RC(tapi_android_start_app(dev, "com.example.app"));
 * CHECK_RC(tapi_android_ui_tap(dev, "Sign in", NULL));
 * CHECK_RC(tapi_android_screenshot(dev, "signed-in", NULL));
 * tapi_android_destroy(dev);
 * @endcode
 */

#ifndef __TAPI_ANDROID_H__
#define __TAPI_ANDROID_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "rcf_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Device handle. */
typedef struct tapi_android tapi_android;

/** Handle of a running logcat capture. */
typedef struct tapi_android_logcat tapi_android_logcat;

/** Screen rectangle of a UI element, in pixels. */
typedef struct tapi_android_bounds {
    int left;
    int top;
    int right;
    int bottom;
} tapi_android_bounds;

/**
 * List the devices the adb server on an agent knows.
 *
 * @param      ta    Test Agent name
 * @param[out] list  Serials, one per line (appended)
 *
 * @return Status code.
 */
extern te_errno tapi_android_devices(const char *ta, te_string *list);

/**
 * Set the adb server address the agent talks to. A handle made by
 * tapi_android_create() keeps the address it was created with.
 *
 * @param ta    Test Agent name
 * @param addr  "host:port" (default "127.0.0.1:5037")
 *
 * @return Status code.
 */
extern te_errno tapi_android_set_server(const char *ta, const char *addr);

/**
 * Create a device handle.
 *
 * @param      rpcs    RPC server on the agent of the device
 * @param      serial  Device serial (@c NULL: the only device the
 *                     agent lists)
 * @param[out] dev     Device handle
 *
 * @return Status code (TE_ENOENT when no such device is listed).
 */
extern te_errno tapi_android_create(rcf_rpc_server *rpcs, const char *serial,
                                    tapi_android **dev);

/**
 * Free a device handle. A running logcat of the device is stopped.
 *
 * @param dev   Device handle (may be @c NULL)
 */
extern void tapi_android_destroy(tapi_android *dev);

/**
 * Get the serial of the device.
 *
 * @param dev   Device handle
 *
 * @return The serial; the string belongs to the handle.
 */
extern const char *tapi_android_serial(const tapi_android *dev);

/**
 * Get the state of the device as adb reports it.
 *
 * @param      dev    Device handle
 * @param[out] state  "device", "offline", "unauthorized", ... (appended)
 *
 * @return Status code.
 */
extern te_errno tapi_android_state(tapi_android *dev, te_string *state);

/**
 * Wait until the device is in the "device" state.
 *
 * @param dev         Device handle
 * @param timeout_ms  Time to wait
 *
 * @return Status code (TE_ETIMEDOUT when the state does not come).
 */
extern te_errno tapi_android_wait_for_device(tapi_android *dev,
                                             unsigned int timeout_ms);

/**
 * Get the model, the Android version and the SDK level.
 *
 * @param      dev      Device handle
 * @param[out] model    ro.product.model (appended; may be @c NULL)
 * @param[out] version  ro.build.version.release (appended; may be @c NULL)
 * @param[out] sdk      ro.build.version.sdk (appended; may be @c NULL)
 *
 * @return Status code.
 */
extern te_errno tapi_android_info(tapi_android *dev, te_string *model,
                                  te_string *version, te_string *sdk);

/**
 * Run a shell command on the device.
 *
 * @param      dev     Device handle
 * @param      cmd     Command line for the device shell
 * @param[out] out     Standard output (appended; may be @c NULL)
 * @param[out] status  Exit status (may be @c NULL: then a non-zero
 *                     status is an error)
 *
 * @return Status code.
 */
extern te_errno tapi_android_shell(tapi_android *dev, const char *cmd,
                                   te_string *out, int *status);

/**
 * Get a system property.
 *
 * @param      dev    Device handle
 * @param      name   Property name
 * @param[out] value  Value (appended)
 *
 * @return Status code.
 */
extern te_errno tapi_android_getprop(tapi_android *dev, const char *name,
                                     te_string *value);

/**
 * Check whether a package is installed.
 *
 * @param      dev        Device handle
 * @param      pkg        Package name
 * @param[out] installed  Result
 *
 * @return Status code.
 */
extern te_errno tapi_android_is_installed(tapi_android *dev,
                                          const char *pkg, bool *installed);

/**
 * Install a package from an APK on the agent. The Configurator
 * records the package, so a rollback uninstalls it.
 *
 * @param dev   Device handle
 * @param pkg   Package name the APK installs
 * @param apk   APK path on the agent
 *
 * @return Status code.
 */
extern te_errno tapi_android_install(tapi_android *dev, const char *pkg,
                                     const char *apk);

/**
 * Copy an APK from the engine to the agent and install it.
 *
 * @param dev   Device handle
 * @param pkg   Package name the APK installs
 * @param apk   APK path on the engine
 *
 * @return Status code.
 */
extern te_errno tapi_android_install_from_engine(tapi_android *dev,
                                                 const char *pkg,
                                                 const char *apk);

/**
 * Uninstall a package.
 *
 * @param dev   Device handle
 * @param pkg   Package name
 *
 * @return Status code.
 */
extern te_errno tapi_android_uninstall(tapi_android *dev, const char *pkg);

/**
 * Check whether a process of a package runs.
 *
 * @param      dev      Device handle
 * @param      pkg      Package name
 * @param[out] running  Result
 *
 * @return Status code.
 */
extern te_errno tapi_android_is_running(tapi_android *dev, const char *pkg,
                                        bool *running);

/**
 * Start the launcher activity of a package.
 *
 * @param dev   Device handle
 * @param pkg   Package name
 *
 * @return Status code.
 */
extern te_errno tapi_android_start_app(tapi_android *dev, const char *pkg);

/**
 * Start an activity by component name.
 *
 * @param dev        Device handle
 * @param component  "package/.Activity"
 *
 * @return Status code.
 */
extern te_errno tapi_android_start_activity(tapi_android *dev,
                                            const char *component);

/**
 * Stop a package ('am force-stop').
 *
 * @param dev   Device handle
 * @param pkg   Package name
 *
 * @return Status code.
 */
extern te_errno tapi_android_force_stop(tapi_android *dev, const char *pkg);

/**
 * Copy a file from the agent to the device.
 *
 * @param dev   Device handle
 * @param src   Path on the agent
 * @param dst   Path on the device
 *
 * @return Status code.
 */
extern te_errno tapi_android_push(tapi_android *dev, const char *src,
                                  const char *dst);

/**
 * Copy a file from the device to the agent.
 *
 * @param dev   Device handle
 * @param src   Path on the device
 * @param dst   Path on the agent
 *
 * @return Status code.
 */
extern te_errno tapi_android_pull(tapi_android *dev, const char *src,
                                  const char *dst);

/**
 * Reboot the device and wait until Android has booted.
 *
 * @param dev   Device handle
 *
 * @return Status code.
 */
extern te_errno tapi_android_reboot(tapi_android *dev);

/**
 * Tap the screen.
 *
 * @param dev   Device handle
 * @param x     X in pixels
 * @param y     Y in pixels
 *
 * @return Status code.
 */
extern te_errno tapi_android_tap(tapi_android *dev, int x, int y);

/**
 * Swipe across the screen.
 *
 * @param dev          Device handle
 * @param x1           Start X
 * @param y1           Start Y
 * @param x2           End X
 * @param y2           End Y
 * @param duration_ms  Duration of the gesture
 *
 * @return Status code.
 */
extern te_errno tapi_android_swipe(tapi_android *dev, int x1, int y1,
                                   int x2, int y2, unsigned int duration_ms);

/**
 * Type text into the focused field.
 *
 * @param dev    Device handle
 * @param text   Text
 *
 * @return Status code.
 */
extern te_errno tapi_android_text(tapi_android *dev, const char *text);

/**
 * Send a key event.
 *
 * @param dev   Device handle
 * @param key   Key code name or number, for example "KEYCODE_HOME"
 *
 * @return Status code.
 */
extern te_errno tapi_android_keyevent(tapi_android *dev, const char *key);

/**
 * Take a screenshot, copy it to the engine and log it as a test
 * artifact. The file goes to TE_LOG_DIR (or TE_TMP without it)
 * under a unique name that ends with "_<name>.png".
 *
 * @param      dev   Device handle
 * @param      name  Name for the log and the file
 * @param[out] path  Path of the copy on the engine (appended; may be
 *                   @c NULL)
 *
 * @return Status code.
 */
extern te_errno tapi_android_screenshot(tapi_android *dev, const char *name,
                                        te_string *path);

/**
 * Dump the UI hierarchy ('uiautomator dump') and return its XML.
 *
 * @param      dev   Device handle
 * @param[out] xml   The dump (appended)
 *
 * @return Status code.
 */
extern te_errno tapi_android_ui_dump(tapi_android *dev, te_string *xml);

/**
 * Find a UI element by its text or resource id in a fresh UI dump.
 *
 * @param      dev          Device handle
 * @param      text         Element text (@c NULL: any)
 * @param      resource_id  Element resource id (@c NULL: any)
 * @param[out] bounds       Screen bounds of the first match
 *
 * @return Status code (TE_ENOENT when no element matches).
 */
extern te_errno tapi_android_ui_find(tapi_android *dev, const char *text,
                                     const char *resource_id,
                                     tapi_android_bounds *bounds);

/**
 * Find a UI element and tap its centre.
 *
 * @param dev          Device handle
 * @param text         Element text (@c NULL: any)
 * @param resource_id  Element resource id (@c NULL: any)
 *
 * @return Status code (TE_ENOENT when no element matches).
 */
extern te_errno tapi_android_ui_tap(tapi_android *dev, const char *text,
                                    const char *resource_id);

/**
 * Start a logcat capture on the agent. The device handle keeps one
 * capture at a time.
 *
 * @param      dev     Device handle
 * @param      filter  logcat filter spec, for example "MyApp:D *:S"
 *                     (@c NULL: everything)
 * @param[out] lc      Capture handle
 *
 * @return Status code.
 */
extern te_errno tapi_android_logcat_start(tapi_android *dev,
                                          const char *filter,
                                          tapi_android_logcat **lc);

/**
 * Wait for a captured line that matches an extended regular
 * expression. Lines read before are not matched again.
 *
 * @param      lc          Capture handle
 * @param      re          POSIX extended regular expression
 * @param      timeout_ms  Time to wait
 * @param[out] line        The matching line (appended; may be @c NULL)
 *
 * @return Status code (TE_ETIMEDOUT when no line matches in time).
 */
extern te_errno tapi_android_logcat_expect(tapi_android_logcat *lc,
                                           const char *re,
                                           unsigned int timeout_ms,
                                           te_string *line);

/**
 * Stop a capture.
 *
 * @param lc    Capture handle (may be @c NULL)
 *
 * @return Status code.
 */
extern te_errno tapi_android_logcat_stop(tapi_android_logcat *lc);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TAPI_ANDROID_H__ */

/**@} <!-- END tapi_android --> */
