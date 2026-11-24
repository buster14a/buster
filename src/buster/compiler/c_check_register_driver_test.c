// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's "address of register variable requested" (#1447): the & operator on a register object,
// its members and its arrays' elements (getPrimaryDecl), at the &, and a register array's decay where it is named
// unparenthesized (DefaultFunctionArrayConversion), at the name. The shared warning harness compares the pinned Clang's
// complete diagnostics through both kernels and requested lanes.

static void check_test_driver_registers(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/register", false);
}
