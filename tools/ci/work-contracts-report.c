// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone entry point for the deterministic work-contract report tests. This is never a compiler build input.

#include "../../src/buster/compiler/work_contract_test.c"

int main(void)
{
    return work_contract_report_tests();
}
