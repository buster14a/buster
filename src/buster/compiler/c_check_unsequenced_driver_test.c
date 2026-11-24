// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wunsequenced (#1953, SequenceChecker, c_check_unsequenced.c): unsequenced modifications
// and reads of a variable, on by default under -Wsequence-point. The shared warning harness compares the pinned Clang's
// complete diagnostics through both kernels and requested lanes. Include after c_check_warning_driver_test.c
// (check_test_driver_warning_fixtures).

static void check_test_driver_unsequenced(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/unsequenced", false);
}
