// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1390 logical-expression discard regressions. Include after c_check_warning_driver_test.c.
// The shared harness compares complete Clang diagnostics, status and output publication for both kernels,
// lane requests, syntax/object modes, and default, -Werror and -w policy.

static void check_test_driver_logical_unused(CheckTest* test, const char* clang, const char* executable, const char* directory)
{

    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/logical-unused", false);
}
