// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1394 call-warning inputs and policies are runtime data. The common harness retains the complete
// independent Clang diagnostic comparison, status, object publication, warning policies and requested lanes.

static void check_test_driver_call_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/call-warnings", true);
}
