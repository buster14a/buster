// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original runtime shift-count sources (#1394). Independent counts and the shared warning harness compare the
// pinned Clang's complete diagnostics, warning policy and object preservation through both kernels and requested lanes.

// A long sweep (26 s on a 4-CPU Xeon VM, #3399): each fixture is a unit of test-checker-driver's deal.
static void check_test_driver_shift_counts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/shift-count", true);
}
