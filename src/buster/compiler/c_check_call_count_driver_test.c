// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Call argument counts and calls without a prototype (#1860, #1921, #1994), and a call's failed argument conversions (#2413): original fixtures whose whole diagnostic
// output is compared with clang's on both paths, at each warning policy, with -fsyntax-only and -c, on one and three
// lanes. Include after c_check_warning_driver_test.c (check_test_driver_warning_fixtures).

static void check_test_driver_call_counts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/call-count", false);
}
