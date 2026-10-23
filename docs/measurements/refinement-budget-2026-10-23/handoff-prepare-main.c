// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include "buster/compiler/c_preprocessor_benchmark.c"
#include "handoff-prepare-test.c"
int main(void)
{
    const bool passed = pp_test_refine_handoff();
    scratch_release();
    return passed ? 0 : 1;
}
