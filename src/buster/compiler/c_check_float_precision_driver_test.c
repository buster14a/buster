// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's floating precision warnings (#1875): -Wimplicit-float-conversion's "implicit conversion
// loses floating-point precision" (CheckImplicitConversion) and its compound assignment's "when assigning computation
// result" (AnalyzeCompoundAssignment), off by default, and -Wliteral-range's "floating-point comparison is always
// true/false" (CheckFloatComparison), on by default, with controls that must not warn. The shared warning harness compares
// the pinned clang's complete diagnostics through both readers and requested lanes, at -fsyntax-only and -c. (-Wconversion
// itself is refused until its other families are implemented, #1365; -Wimplicit-float-conversion's child
// -Wimplicit-int-float-conversion is not given, so no source converts an integer wider than a float's significand.)

static void check_test_driver_float_precision(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/float-precision", false);
}
