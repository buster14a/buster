// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1394 source-shape inputs and policies are runtime data. The shared runner compares complete diagnostics,
// source positions and notes with Clang under normal, promoted and suppressed policy and multiple lane requests.

static void check_test_driver_syntax_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/syntax-warnings", true);
}
