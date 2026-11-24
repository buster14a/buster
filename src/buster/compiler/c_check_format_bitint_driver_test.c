// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original _BitInt format-argument sources (#1392), and _Atomic ones (#2234), with independent default-warning counts. The shared harness
// compares complete Clang diagnostics, -Werror/output preservation and -w, both kernels and requested lane counts.

static void check_test_driver_format_bitints(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/format-bitint", false);
}
