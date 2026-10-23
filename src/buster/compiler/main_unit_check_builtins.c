// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// main.c compiled as the unit of the checker's builtin tests and the builtin intrinsics' catalog tests: main.c, at its
// top, says how its units divide the build.
#define BUSTER_UNIT BUSTER_UNIT_CHECK_BUILTINS
#include "buster/compiler/main.c"
