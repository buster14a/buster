// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wmissing-variable-declarations sources (#2222). The shared warning runner checks complete Clang
// diagnostics, including declaration and unit-end ordering and type-specifier note locations, under the ordinary,
// -Werror and -w policies, with syntax/object output, requested lanes and the checker's existing alternate route.

static void check_test_driver_missing_variable(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/missing-variable", false);
}
