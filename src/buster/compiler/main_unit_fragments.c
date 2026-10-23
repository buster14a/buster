// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The fragment experiment's test command compiles the scalar recognizer with its second sink. Production commands
// and the existing parser benchmark are compiled in their own units without that sink or its instrumentation.
#define BUSTER_UNIT BUSTER_UNIT_FRAGMENTS
#define BUSTER_PARSE_FRAGMENT_SINK 1
#include "buster/compiler/main.c"
