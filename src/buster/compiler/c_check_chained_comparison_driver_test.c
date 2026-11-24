// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original chained comparison sources (#2264, c_check_common_warnings.c's check_common_chained_comparisons): clang's
// -Wparentheses error by default. Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and lane
// requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static void check_test_driver_chained_comparisons(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/chained-comparison", false);
}
