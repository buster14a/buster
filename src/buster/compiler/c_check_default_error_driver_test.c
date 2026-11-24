// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original sources of clang's conversions that default to errors, demoted or ignored (#1963, c_checker_driver.c's
// check_driver_default_error_warnings). Clang supplies the external diagnostic oracle. Include after
// c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both kernels and
// lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c.

static void check_test_driver_default_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/default-error", false);
}
