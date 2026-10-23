// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "chunk-refinement.c"
#include "buster/compiler/c_refine_test.c"

int main(int argc, char** argv)
{
    return run_refinement_tests(argc - 1, argv + 1);
}
