// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wtautological-constant-out-of-range-compare sources (#2218, c_check_common_warnings.c's
// check_common_out_of_range). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies, with -fsyntax-only and -c.

static void check_test_driver_out_of_range(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/out-of-range", false);
}
