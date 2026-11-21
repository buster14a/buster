// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Standalone entry point; the compiler executable never includes the contract's filesystem wrappers.

#include "../../src/buster/compiler/c_dependency_work_contract_test.c"

int main(void)
{
    return dependency_work_contract_tests();
}
