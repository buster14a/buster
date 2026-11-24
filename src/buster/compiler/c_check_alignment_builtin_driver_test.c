// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original alignment builtin sources (#2221): __builtin_align_up's, __builtin_align_down's and __builtin_is_aligned's
// errors as clang words and places them (check_semantic_alignment), and their -Wtautological-compare of an alignment of 1
// (check_input_alignment_one). Clang supplies the external diagnostic oracle. Include after c_check_warning_driver_test.c,
// whose fixture runner compares complete no-caret diagnostics through both kernels and lane requests, under the default,
// -Werror and -w policies, with -fsyntax-only and -c.

static void check_test_driver_alignment_builtins(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/alignment-builtin", false);
}
