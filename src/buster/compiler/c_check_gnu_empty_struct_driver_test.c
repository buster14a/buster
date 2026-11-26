// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original -Wgnu-empty-struct sources (#2409, c_check_input.c's CHECK_WARNING_EVENT_GNU_EMPTY_STRUCT). Clang supplies the
// external diagnostic oracle. Include after c_check_warning_driver_test.c, whose fixture runner compares complete
// no-caret diagnostics through both kernels and lane requests, under the default, -Werror and -w policies, with
// -fsyntax-only and -c.

static void check_test_driver_gnu_empty_struct(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/gnu-empty-struct", false);
}

// Array and record extension regressions are runtime data, including their option/diagnostic-count inventory.
static void check_test_driver_record_extensions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/record-extensions", false);
}
