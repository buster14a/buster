// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1394 expression inputs and policies are runtime data. The pinned Clang oracle still checks complete
// diagnostics, notes, evaluated contexts, kernels, lane requests, -Werror and -w in the same dealt order.

static void check_test_driver_expression_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/expression-warnings", true);
}
