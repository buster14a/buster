// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// main.c compiled as the unit of the direct executables' and the runtime snapshot's tests: main.c, at its top, says how
// its units divide the build.
#define BUSTER_UNIT BUSTER_UNIT_EXECUTABLE
#include "buster/compiler/main.c"
