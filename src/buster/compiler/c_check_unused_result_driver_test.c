// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// A discarded call's result (#1447, c_check_ignored_result.c): original fixtures whose whole diagnostic output is compared
// with clang's on both paths, at each warning policy and with the options that turn the warnings off. Include after
// c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

static void check_test_driver_unused_results(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/unused-result", false);
    check_test_driver_warning_catalog(test, clang, executable, directory, "checker/initializer-comma");
    check_test_driver_warning_catalog(test, clang, executable, directory, "checker/designator-recovery");
}
