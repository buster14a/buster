// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original diagnose_if and enable_if sources (#1447, c_check_condition.c). Clang supplies the external diagnostic oracle.
// Include after c_check_warning_driver_test.c, whose fixture runner compares complete no-caret diagnostics through both
// kernels and lane requests, under the default, -Werror and -w policies, with -fsyntax-only and -c. A condition's own
// error drops its attribute and the reader goes on (#2383), as clang's does.

// A long sweep (25 s on a 4-CPU Xeon VM, #3399): each fixture is a unit of test-checker-driver's deal.
static void check_test_driver_conditions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/condition", true);
}
