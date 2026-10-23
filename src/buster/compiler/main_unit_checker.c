// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// main.c compiled as the unit of the semantic checker's commands (check, its tests and its source benchmark), which
// hold the whole pipeline from the lexer to object emission: main.c, at its top, says how its units divide the build.
#define BUSTER_UNIT BUSTER_UNIT_CHECKER
#include "buster/compiler/main.c"
