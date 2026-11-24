// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's alignment builtins and __builtin_expect_with_probability (c_check_builtin_misc.c, c_builtin_misc_lower.c) in
// test-checker: source fixtures of their result types, folds and refusals, each checked by both paths against clang
// -fsyntax-only, then programs run natively against clang -O0's build of the same source on both paths: align_up,
// align_down and is_aligned of every integer width, _BitInt (past 128 bits too, #1447), 128-bit and pointer sources, by
// alignments of other widths (one that truncates to 0 in a char among them), folded and at run time, of addresses in
// static data and frames; expect's
// and expect_with_probability's arguments evaluated once, in order, or not at all where the first argument folds, as
// clang's code does; and the generic builtins of c_check_program_generic_builtins_test.c, which runs with them. Include after
// c_check_program_bits_test.c and c_check_program_generic_builtins_test.c.
#ifndef BUSTER_C_CHECK_PROGRAM_ALIGN_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ALIGN_TEST_INCLUDED

static __attribute__((noinline)) void check_test_align_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/align, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/align", "the alignment builtins' Clang oracle ran");
}

static __attribute__((noinline)) void check_test_align_programs(CheckTest* test, const char* clang)
{
    // Every integer width at run time, by alignments of every width, powers of 2 from n; a char by an int's 256, which
    // truncates to 0 in its width; _BitInt and 128-bit sources; folds beside them.
    check_test_program_suite(test, "checker/programs/align-programs", clang);
}

static __attribute__((noinline)) void check_test_align(CheckTest* test, const char* clang)
{
    check_test_align_sources(test, clang);
    check_test_align_programs(test, clang);
    check_test_generic_builtins(test, clang);
}

#endif
