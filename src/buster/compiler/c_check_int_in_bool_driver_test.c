// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's DiagnoseIntInBoolContext (#1447): a `<<` and a conditional of integer constants converted
// to _Bool, through truth contexts and conversions, with -Wint-in-bool-context's and -Wtautological-constant-compare's
// groups. The shared warning harness compares the pinned Clang's complete diagnostics, warning policy and object
// preservation through both kernels and requested lanes.

static void check_test_driver_int_in_bool(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/int-in-bool", false);
}
