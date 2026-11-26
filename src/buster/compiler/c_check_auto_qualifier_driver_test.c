// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original __auto_type qualifier inputs (#1844). Explicit dialect rows preserve their previous per-source GNU17/C23
// order. The shared catalog validates the whole inventory before its independent Clang diagnostic checks run.

static void check_test_driver_auto_qualifiers(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/auto-qualifiers", false);
}
