# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# External TE library (see TE_EXT_REPO): engine-side TAPI for Android
# devices on agents. Built as a shared library on the engine platform.
# It needs the android_* RPCs in rpcxdr, so add
# ../ta_android/android_rpc.x.m4 to the rpcxdr definitions of the
# engine platform. libxml2 parses the UI dumps.

if get_option('engine')
    build_lib_shared = true
endif
build_lib_static = not get_option('engine')
install_lib = install_dev

headers += files(
    'tapi_android.h',
    'tapi_android_rpc.h',
)

sources += files(
    'tapi_android.c',
    'tapi_android_rpc.c',
)

te_libs += [
    'tapi',
    'tapi_rpc',
    'rcfrpc',
    'confapi',
    'rcfapi',
    'rpcxdr',
    'logger_core',
    'tools',
]

deps += [ dep_libxml2 ]

# The stand-in server that consumer selftests upload to an agent
# without a device, see tests/README.md
if get_option('engine')
    install_data(
        files('tests/fake_adb_server.py'),
        install_dir: join_paths(get_option('datadir'), 'tsf-android'),
    )
endif

# Configurator model of the /agent/android subtree
if get_option('engine')
    install_data(
        files('cm_android.yml'),
        install_dir: join_paths(get_option('datadir'), 'cm'),
    )
endif
