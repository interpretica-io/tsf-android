# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# External TE library (see TE_EXT_REPO): the android_* RPCs for the
# RPC server of an agent. List it in the libraries of the
# ta_rpcprovider application together with ta_android, and add
# ta_android/android_rpc.x.m4 to the rpcxdr definitions of both
# platforms.

sources += files(
    'rpcs_android.c',
)

te_libs += [
    'rpcserver',
    'ta_android',
]
