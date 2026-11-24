// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's checkEnumArithmeticConversions (#1447): the usual arithmetic conversions of enumerations
// of different types, or of an enumeration and a floating type — -Wenum-compare, -Wenum-enum-conversion,
// -Wenum-compare-conditional, -Wenum-float-conversion and -Wanon-enum-enum-conversion, with their groups. The shared
// warning harness compares the pinned Clang's complete diagnostics through both kernels and requested lanes.

static void check_test_driver_enum_arithmetic(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/enum-arithmetic", false);
}
