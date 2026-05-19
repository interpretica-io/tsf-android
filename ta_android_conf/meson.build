# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# External TE library (see TE_EXT_REPO): the /agent/android configuration
# subtree on top of ta_android. Only the agent links it, wholly, so that
# the constructor in ta_android_conf.c registers the subtree without
# Test Agent modifications; the RPC server links ta_android alone.

link_whole = true

sources += files(
    'ta_android_conf.c',
)

te_libs += [
    'rcfpch',
    'ta_android',
    'logger_core',
    'tools',
]
