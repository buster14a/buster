// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The address-space qualifiers and objc_gc's (#1447, CheckInputAttribute.space and type_extend): original fixtures whose
// whole diagnostic output is compared with clang's on both paths, at each warning policy, with -fsyntax-only and -c.
// Include after c_check_warning_driver_test.c
// (check_test_driver_warning_fixtures).

static void check_test_driver_address_spaces(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/address-space", false);
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/auto-address", false);
}
