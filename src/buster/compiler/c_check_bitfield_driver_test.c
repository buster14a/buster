// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's AnalyzeBitFieldAssignment (#1447): a constant a bit-field's assignment or initialization
// truncates, -Wbitfield-constant-conversion and -Wsingle-bit-bitfield-constant-conversion, with their groups under
// -Wconstant-conversion and -Wconversion. The shared warning harness compares the pinned Clang's complete diagnostics,
// warning policy and object preservation through both kernels and requested lanes.

static void check_test_driver_bitfields(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/bitfield", false);
}
