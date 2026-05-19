# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# External TE library (see TE_EXT_REPO): agent-side client of the adb
# server protocol. The agent and its RPC server both link it; the
# /agent/android subtree is ta_android_conf, for the agent alone.

headers += files(
    'ta_android.h',
)

sources += files(
    'ta_android.c',
)

te_libs += [
    'logger_core',
    'tools',
]

deps += [ dependency('threads') ]
