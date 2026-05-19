# tsf-android

Android device control for the OKTET Labs Test Environment (TE),
packaged as an external TE repository (consumed with the
`TE_EXT_REPO` builder directive).

Four libraries:

- `ta_android` — agent side. A client of the adb server protocol:
  the library talks to the adb server over TCP and runs the device
  services itself (shell v2, sync push and pull, reboot, logcat).
  Every call names the adb server: the library keeps no state, since
  the subtree and the RPCs run in different processes.
- `ta_android_conf` — the `/agent/android` subtree on top of
  `ta_android`, linked into the agent alone: the adb server address,
  the devices the server lists, their state, model, version and SDK
  level, and the installed packages as `read_create` instances, so
  that adding one installs an APK and a Configurator rollback
  uninstalls it.
- `rpcs_android` — the `android_*` RPCs for the RPC server of the
  agent: shell, push, pull, reboot and logcat capture, on top of
  `ta_android`. The RPC server does not see `/agent/android`, so each
  RPC carries the adb server address; a `tapi_android` handle reads it
  from the subtree when it is created.
- `tapi_android` — engine side. `tapi_android.h` gives a test a device
  handle and the operations: `tapi_android_install()` (Configurator),
  `tapi_android_shell()` (RPC), `tapi_android_ui_tap()` (a
  `uiautomator` dump parsed on the engine), `tapi_android_screenshot()`
  (the file goes to the engine and into the log as a test artifact),
  `tapi_android_logcat_expect()` and the rest. `cm_android.yml` is the
  Configurator model of the subtree.

No adb process runs per operation. The adb server itself (`adb
start-server`) must run on the agent host, since the USB transport
lives in it.

## Agent host requirements

- the adb server from the Android platform-tools, started with
  `adb start-server`, listening on `127.0.0.1:5037` (or set
  `/agent/android/server` with `tapi_android_set_server()`);
- the device attached with USB debugging on, or reachable after
  `adb connect`.

## Usage

Declare the repository in an external libraries catalog and pass it to
`dispatcher.sh --external=<catalog.yml>`:

```yaml
repositories:
  - name: tsf_android
    url: https://github.com/interpretica-io/tsf-android.git
    ref: v1.0.0
    libs:
      - ta_android
      - ta_android_conf
      - rpcs_android
      - tapi_android
```

In `builder.conf`, bind `tapi_android` to the engine platform and the
agent libraries to the agent platform, add the RPC definitions to
`rpcxdr` on both, put `ta_android` and `ta_android_conf` into the agent and
`ta_android` with `rpcs_android` into the RPC server:

```
TE_EXT_REPO_USE([tsf_android], [], [tapi_android])
TE_LIB_PARMS([rpcxdr], [], [],
             [--with-rpcdefs=tarpc_job.x.m4,../ta_android/android_rpc.x.m4])

TE_EXT_REPO_USE([tsf_android], [<agent platform>], [ta_android ta_android_conf rpcs_android])
TE_LIB_PARMS([rpcxdr], [<agent platform>], [],
             [--with-rpcdefs=tarpc_job.x.m4,../ta_android/android_rpc.x.m4])
TE_TA_TYPE([<ta type>], [<agent platform>], [unix], [--with-rcf-rpc],
           [], [], [], [comm_net_agent rcfpch ta_android ta_android_conf])
TE_TA_APP([ta_rpcprovider], [<agent platform>], [<ta type>],
          [ta_rpcprovider], [], [],
          [... rpcs_job rpcs_android ta_android rpcserver agentlib rpcxdrta ...],
          [\${EXT_SOURCES}/build.sh], [ta_rpcs], [])
```

`rpcs_android` must come before `rpcxdrta` in the list: `tarpc.c` in
`rpcxdrta` has a weak stub for every RPC and the linker keeps the first
definition it meets, so an `rpcs_*` library after it never gets linked
in and the RPCs fail with `RPC-ERPCNOTSUPP`.

Include `cm_android.yml` in the Configurator configuration of the
suite, add `tapi_android` to the `te_libs` of the suite and write the
test:

```c
#include "tapi_android.h"

    CHECK_RC(rcf_rpc_server_create(ta, "pco_android", &rpcs));
    CHECK_RC(tapi_android_create(rpcs, serial, &dev));
    CHECK_RC(tapi_android_wait_for_device(dev, 60000));

    TEST_STEP("Install and start the app");
    CHECK_RC(tapi_android_install_from_engine(dev, "com.example.app", apk));
    CHECK_RC(tapi_android_logcat_start(dev, "MyApp:D *:S", &lc));
    CHECK_RC(tapi_android_start_app(dev, "com.example.app"));
    CHECK_RC(tapi_android_logcat_expect(lc, "onCreate", 10000, NULL));

    TEST_STEP("Sign in");
    CHECK_RC(tapi_android_ui_tap(dev, NULL, "com.example.app:id/name"));
    CHECK_RC(tapi_android_text(dev, "te"));
    CHECK_RC(tapi_android_ui_tap(dev, "Sign in", NULL));
    CHECK_RC(tapi_android_screenshot(dev, "signed-in", NULL));

cleanup:
    tapi_android_destroy(dev);
```

## Tests

`tapi_android/tests/fake_adb_server.py` stands in for the adb server on an agent
without a device; see `tapi_android/tests/README.md`.

Requires TE with `TE_EXT_REPO` support.
