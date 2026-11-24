// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wfor-loop-analysis (#1447, CheckForLoopConditionalStatement and
// CheckForRedundantIteration): a for loop's condition variables its loop never changes, and its variable both its
// header and its body's last statement step, under -Wloop-analysis, -Wmost and -Wall. The shared warning harness compares
// the pinned Clang's complete diagnostics through both kernels and requested lanes.

static void check_test_driver_for_loops(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/for-loop", false);
}
