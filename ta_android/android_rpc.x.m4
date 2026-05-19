/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief RPC for Android device access
 *
 * The RPCs of rpcs_android. Add this file to the rpcxdr definitions
 * of the engine platform and of the agent platform:
 *
 *   TE_LIB_PARMS([rpcxdr], [<platform>], [],
 *                [--with-rpcdefs=tarpc_job.x.m4,../ta_android/android_rpc.x.m4])
 *
 * The RPC server is a process of its own and does not see the
 * /agent/android subtree, so every device RPC names the adb server
 * (an empty string is the default address).
 */

/* android_shell(): run a shell command on a device */
struct tarpc_android_shell_in {
    struct tarpc_in_arg common;

    string server<>;
    string serial<>;
    string cmd<>;
};

struct tarpc_android_shell_out {
    struct tarpc_out_arg common;

    tarpc_int retval;
    string out<>;
    string err<>;
    tarpc_int status;
};

/* android_push(): copy a file from the agent to a device */
struct tarpc_android_push_in {
    struct tarpc_in_arg common;

    string server<>;
    string serial<>;
    string local<>;
    string remote<>;
};

struct tarpc_android_push_out {
    struct tarpc_out_arg common;

    tarpc_int retval;
};

/* android_pull(): copy a file from a device to the agent */
struct tarpc_android_pull_in {
    struct tarpc_in_arg common;

    string server<>;
    string serial<>;
    string remote<>;
    string local<>;
};

struct tarpc_android_pull_out {
    struct tarpc_out_arg common;

    tarpc_int retval;
};

/* android_reboot() */
struct tarpc_android_reboot_in {
    struct tarpc_in_arg common;

    string server<>;
    string serial<>;
};

struct tarpc_android_reboot_out {
    struct tarpc_out_arg common;

    tarpc_int retval;
};

/* android_logcat_start(): capture logcat into a file on the agent */
struct tarpc_android_logcat_start_in {
    struct tarpc_in_arg common;

    string server<>;
    string serial<>;
    string filter<>;
};

struct tarpc_android_logcat_start_out {
    struct tarpc_out_arg common;

    tarpc_int retval;
    tarpc_uint id;
};

/* android_logcat_read(): the captured bytes from an offset */
struct tarpc_android_logcat_read_in {
    struct tarpc_in_arg common;

    tarpc_uint id;
    tarpc_size_t offset;
};

struct tarpc_android_logcat_read_out {
    struct tarpc_out_arg common;

    tarpc_int retval;
    string data<>;
    tarpc_size_t next;
};

/* android_logcat_stop() */
struct tarpc_android_logcat_stop_in {
    struct tarpc_in_arg common;

    tarpc_uint id;
};

struct tarpc_android_logcat_stop_out {
    struct tarpc_out_arg common;

    tarpc_int retval;
};

program android
{
    version ver0
    {
        RPC_DEF(android_shell)
        RPC_DEF(android_push)
        RPC_DEF(android_pull)
        RPC_DEF(android_reboot)
        RPC_DEF(android_logcat_start)
        RPC_DEF(android_logcat_read)
        RPC_DEF(android_logcat_stop)
    } = 1;
} = 20;
