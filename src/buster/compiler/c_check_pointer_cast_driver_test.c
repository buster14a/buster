// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's casts from a pointer to a narrower integer type (#1943, CheckCStyleCast,
// c_check_warning_graph.c's check_warning_graph_pointer_cast): -Wpointer-to-int-cast, -Wvoid-pointer-to-int-cast,
// -Wpointer-to-enum-cast and -Wvoid-pointer-to-enum-cast, on by default. The shared warning harness compares the pinned
// Clang's complete diagnostics through both kernels and requested lanes. Include after c_check_warning_driver_test.c
// (check_test_driver_warning_fixtures).

static void check_test_driver_pointer_casts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/pointer-cast", false);
}
