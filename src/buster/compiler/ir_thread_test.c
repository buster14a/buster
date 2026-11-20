// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2554's threading fixtures, in the IR's text form, included after ir_optimize_test.c's independent executor. Each is
// parsed, threaded by both candidate paths (which must agree exactly), verified as complete SSA, and executed against
// its input on every parameter below 256 and a few past it, then again after the C driver's following steps
// (ir_truth_simplify and the optimizer). Each one's parameter p supplies two-bit fields (p & 3, (p >> 2) & 3, ...), so
// the trials take every path.

#include "ir_truth.c"
#include "ir_thread.c"

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

typedef struct IrThreadTestFixture IrThreadTestFixture;
struct IrThreadTestFixture
{
    const char* name;
    const char* text;
    uint32_t blocks;
    uint32_t copies;
    uint32_t redirects;
    uint32_t rounds;
    // #2608: the phis read past a threaded block that a successor merges again.
    uint32_t merges;
    // After threading, ir_truth_simplify and the optimizer: whether no phi and no zero extension remain (the truth values
    // all became branches).
    bool flattened;
    // Of values the independent executor doesn't run (vectors): verified, not executed.
    bool structural;
};

#define BUSTER_IR_THREAD_TEST_C(n) "0x000000000000000000000000000000" n

static const IrThreadTestFixture ir_thread_test_fixtures[] = {
    {
        // if (a < b && b < c): the false edge's zero goes to the else block, the right side's comparison is the branch.
        .name = "and",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = icmp.i1 %2, %5 lt\n"
                "  %11 = cbr.void %10, b1, b2\n"
                "b1:\n"
                "  %12 = icmp.i1 %5, %8 lt\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b2\n"
                "b2:\n"
                "  %15 = phi.i32 [%9, b0], [%13, b1]\n"
                "  %16 = icmp.i1 %15, %9 ne\n"
                "  %17 = cbr.void %16, b3, b4\n"
                "b3:\n"
                "  %18 = const.i64 " BUSTER_IR_THREAD_TEST_C("07") "\n"
                "  %19 = ret.void %18\n"
                "b4:\n"
                "  %20 = const.i64 " BUSTER_IR_THREAD_TEST_C("09") "\n"
                "  %21 = ret.void %20\n"
                "}\n",
        .blocks = 1,
        .copies = 1,
        .redirects = 1,
        .rounds = 1,
        .flattened = true,
    },
    {
        // if (!(x < y || y == 2)): the true edge's one, tested by eq.
        .name = "or-not",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %8 = icmp.i1 %2, %5 lt\n"
                "  %9 = cbr.void %8, b2, b1\n"
                "b1:\n"
                "  %10 = icmp.i1 %5, %3 eq\n"
                "  %11 = zext.i32 %10\n"
                "  %12 = br.void b2\n"
                "b2:\n"
                "  %13 = phi.i32 [%6, b0], [%11, b1]\n"
                "  %14 = icmp.i1 %13, %7 eq\n"
                "  %15 = cbr.void %14, b4, b3\n"
                "b3:\n"
                "  %16 = add.i64 %2, %5\n"
                "  %17 = ret.void %16\n"
                "b4:\n"
                "  %18 = ret.void %3\n"
                "}\n",
        .blocks = 1,
        .copies = 1,
        .redirects = 1,
        .rounds = 1,
        .flattened = true,
    },
    {
        // A loop's found flag: the exit's zero and the break's one, both conditional predecessors, each redirected.
        .name = "loop-flag",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("07") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %7 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %8 = lshr.i64 %0, %7\n"
                "  %9 = and.i64 %8, %1\n"
                "  %10 = br.void b1\n"
                "b1:\n"
                "  %11 = phi.i64 [%3, b0], [%16, b3]\n"
                "  %12 = icmp.i1 %11, %2 lt\n"
                "  %13 = cbr.void %12, b2, b4\n"
                "b2:\n"
                "  %14 = icmp.i1 %11, %9 eq\n"
                "  %15 = cbr.void %14, b4, b3\n"
                "b3:\n"
                "  %16 = add.i64 %11, %4\n"
                "  %17 = br.void b1\n"
                "b4:\n"
                "  %18 = phi.i32 [%5, b1], [%6, b2]\n"
                "  %19 = icmp.i1 %18, %5 ne\n"
                "  %20 = cbr.void %19, b5, b6\n"
                "b5:\n"
                "  %21 = ret.void %4\n"
                "b6:\n"
                "  %22 = ret.void %3\n"
                "}\n",
        .blocks = 1,
        .copies = 0,
        .redirects = 2,
        .rounds = 1,
        .flattened = false,
    },
    {
        // The truth value read past the test (its successor's own instruction), which only the test reaches: left alone.
        // #2608 merges again only phis that the test doesn't read (ir_thread_decides).
        .name = "escape",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = icmp.i1 %2, %5 lt\n"
                "  %8 = cbr.void %7, b1, b2\n"
                "b1:\n"
                "  %9 = icmp.i1 %5, %3 lt\n"
                "  %10 = zext.i32 %9\n"
                "  %11 = br.void b2\n"
                "b2:\n"
                "  %12 = phi.i32 [%6, b0], [%10, b1]\n"
                "  %13 = icmp.i1 %12, %6 ne\n"
                "  %14 = cbr.void %13, b3, b4\n"
                "b3:\n"
                "  %15 = zext.i64 %12\n"
                "  %16 = ret.void %15\n"
                "b4:\n"
                "  %17 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // A switch's edge stays; two jumps take copies, and the successors' phis take a copied chain value and the phi's
        // inputs along them.
        .name = "switch-kept",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = lshr.i64 %0, %5\n"
                "  %7 = and.i64 %6, %1\n"
                "  %8 = trunc.i32 %7\n"
                "  %9 = switch.void %2, default b3, [" BUSTER_IR_THREAD_TEST_C("00") ", b1], [" BUSTER_IR_THREAD_TEST_C("01") ", b2]\n"
                "b1:\n"
                "  %10 = icmp.i1 %7, %5 ult\n"
                "  %11 = zext.i32 %10\n"
                "  %12 = br.void b3\n"
                "b2:\n"
                "  %13 = br.void b3\n"
                "b3:\n"
                "  %14 = phi.i32 [%8, b0], [%11, b1], [%4, b2]\n"
                "  %15 = icmp.i1 %14, %3 ne\n"
                "  %16 = zext.i64 %15\n"
                "  %17 = cbr.void %15, b4, b5\n"
                "b4:\n"
                "  %18 = phi.i64 [%16, b3]\n"
                "  %19 = add.i64 %18, %7\n"
                "  %20 = ret.void %19\n"
                "b5:\n"
                "  %21 = phi.i32 [%14, b3]\n"
                "  %22 = zext.i64 %21\n"
                "  %23 = ret.void %22\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .flattened = false,
    },
    {
        // A conditional predecessor whose other edge goes where the block would send it: one jump.
        .name = "collapse",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %5 = icmp.i1 %2, %3 ugt\n"
                "  %6 = cbr.void %5, b1, b2\n"
                "b1:\n"
                "  %7 = icmp.i1 %2, %1 eq\n"
                "  %8 = zext.i32 %7\n"
                "  %9 = br.void b3\n"
                "b2:\n"
                "  %10 = icmp.i1 %2, %3 eq\n"
                "  %11 = cbr.void %10, b3, b4\n"
                "b3:\n"
                "  %12 = phi.i32 [%8, b1], [%4, b2]\n"
                "  %13 = icmp.i1 %12, %4 ne\n"
                "  %14 = cbr.void %13, b5, b4\n"
                "b4:\n"
                "  %15 = ret.void %3\n"
                "b5:\n"
                "  %16 = ret.void %1\n"
                "}\n",
        .blocks = 1,
        .copies = 1,
        .redirects = 1,
        .rounds = 1,
        .flattened = true,
    },
    {
        // if (x < y && y < z && z < w): the second test is threaded in a second round.
        .name = "and-chain",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i64 " BUSTER_IR_THREAD_TEST_C("06") "\n"
                "  %10 = lshr.i64 %0, %9\n"
                "  %11 = and.i64 %10, %1\n"
                "  %12 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %13 = icmp.i1 %2, %5 lt\n"
                "  %14 = cbr.void %13, b1, b2\n"
                "b1:\n"
                "  %15 = icmp.i1 %5, %8 lt\n"
                "  %16 = zext.i32 %15\n"
                "  %17 = br.void b2\n"
                "b2:\n"
                "  %18 = phi.i32 [%12, b0], [%16, b1]\n"
                "  %19 = icmp.i1 %18, %12 ne\n"
                "  %20 = cbr.void %19, b3, b4\n"
                "b3:\n"
                "  %21 = icmp.i1 %8, %11 lt\n"
                "  %22 = zext.i32 %21\n"
                "  %23 = br.void b4\n"
                "b4:\n"
                "  %24 = phi.i32 [%12, b2], [%22, b3]\n"
                "  %25 = icmp.i1 %24, %12 ne\n"
                "  %26 = cbr.void %25, b5, b6\n"
                "b5:\n"
                "  %27 = const.i64 " BUSTER_IR_THREAD_TEST_C("07") "\n"
                "  %28 = ret.void %27\n"
                "b6:\n"
                "  %29 = const.i64 " BUSTER_IR_THREAD_TEST_C("09") "\n"
                "  %30 = ret.void %29\n"
                "}\n",
        .blocks = 2,
        .copies = 2,
        .redirects = 3,
        .rounds = 2,
        .flattened = true,
    },
    {
        // The test's block is a label's (#131's semantic entry): left alone.
        .name = "label",
        .text = "function (i64) -> i64 {\n"
                "  label #0 b2\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = icmp.i1 %2, %5 lt\n"
                "  %8 = cbr.void %7, b1, b2\n"
                "b1:\n"
                "  %9 = icmp.i1 %5, %3 lt\n"
                "  %10 = zext.i32 %9\n"
                "  %11 = br.void b2\n"
                "b2:\n"
                "  %12 = phi.i32 [%6, b0], [%10, b1]\n"
                "  %13 = icmp.i1 %12, %6 ne\n"
                "  %14 = cbr.void %13, b3, b4\n"
                "b3:\n"
                "  %15 = ret.void %1\n"
                "b4:\n"
                "  %16 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .flattened = false,
    },
    {
        // A latch whose test goes back to it: the copy branches to its own block, whose phi takes its own value.
        .name = "self-loop",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("07") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %6 = br.void b1\n"
                "b1:\n"
                "  %7 = phi.i64 [%3, b0], [%8, b2]\n"
                "  %8 = add.i64 %7, %4\n"
                "  %9 = icmp.i1 %8, %2 ult\n"
                "  %10 = zext.i32 %9\n"
                "  %11 = br.void b2\n"
                "b2:\n"
                "  %12 = phi.i32 [%10, b1]\n"
                "  %13 = icmp.i1 %12, %5 ne\n"
                "  %14 = cbr.void %13, b1, b3\n"
                "b3:\n"
                "  %15 = ret.void %8\n"
                "}\n",
        .blocks = 1,
        .copies = 1,
        .redirects = 0,
        .rounds = 1,
        .flattened = false,
    },
};

// #2608's repairs: a phi of the threaded block read past it, which a successor merges again, and the cases left alone.
static const IrThreadTestFixture ir_thread_test_repairs[] = {
    {
        // Lua's `tonumberns`, a `?:` that sets the number and yields 1 or 0: its join's number is read in the true
        // successor's arithmetic, which its only predecessor, the join, dominates. The successor merges the number
        // again.
        .name = "tonumberns",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %5 = lshr.i64 %0, %4\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = undef.i64\n"
                "  %9 = icmp.i1 %2, %3 eq\n"
                "  %10 = cbr.void %9, b1, b2\n"
                "b1:\n"
                "  %11 = mul.i64 %5, %1\n"
                "  %12 = br.void b4\n"
                "b2:\n"
                "  %13 = icmp.i1 %2, %4 eq\n"
                "  %14 = cbr.void %13, b3, b4\n"
                "b3:\n"
                "  %15 = add.i64 %5, %3\n"
                "  %16 = br.void b4\n"
                "b4:\n"
                "  %17 = phi.i32 [%7, b1], [%6, b2], [%7, b3]\n"
                "  %18 = phi.i64 [%11, b1], [%8, b2], [%15, b3]\n"
                "  %19 = icmp.i1 %17, %6 ne\n"
                "  %20 = cbr.void %19, b5, b6\n"
                "b5:\n"
                "  %21 = add.i64 %18, %5\n"
                "  %22 = ret.void %21\n"
                "b6:\n"
                "  %23 = ret.void %1\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 1,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // `if (tonumberns(v1, n1) && tonumberns(v2, n2)) r = n1 + n2;`, as op_arithf_aux has it. The first round
        // threads the `&&` join, n2 merged again in the body; the body is then reached only through the second test, so
        // the second round threads the first `?:` join, n1 read two levels past its true successor, and the second one,
        // which took the `&&` join's test.
        .name = "tonumberns-and",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %5 = lshr.i64 %0, %4\n"
                "  %6 = and.i64 %5, %1\n"
                "  %7 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %8 = lshr.i64 %0, %7\n"
                "  %9 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %11 = undef.i64\n"
                "  %12 = icmp.i1 %2, %3 eq\n"
                "  %13 = cbr.void %12, b1, b2\n"
                "b1:\n"
                "  %14 = mul.i64 %8, %1\n"
                "  %15 = br.void b4\n"
                "b2:\n"
                "  %16 = icmp.i1 %2, %4 eq\n"
                "  %17 = cbr.void %16, b3, b4\n"
                "b3:\n"
                "  %18 = add.i64 %8, %3\n"
                "  %19 = br.void b4\n"
                "b4:\n"
                "  %20 = phi.i32 [%10, b1], [%9, b2], [%10, b3]\n"
                "  %21 = phi.i64 [%14, b1], [%11, b2], [%18, b3]\n"
                "  %22 = icmp.i1 %20, %9 ne\n"
                "  %23 = cbr.void %22, b5, b10\n"
                "b5:\n"
                "  %24 = icmp.i1 %6, %3 eq\n"
                "  %25 = cbr.void %24, b6, b7\n"
                "b6:\n"
                "  %26 = shl.i64 %8, %3\n"
                "  %27 = br.void b9\n"
                "b7:\n"
                "  %28 = icmp.i1 %6, %4 eq\n"
                "  %29 = cbr.void %28, b8, b9\n"
                "b8:\n"
                "  %30 = sub.i64 %8, %3\n"
                "  %31 = br.void b9\n"
                "b9:\n"
                "  %32 = phi.i32 [%10, b6], [%9, b7], [%10, b8]\n"
                "  %33 = phi.i64 [%26, b6], [%11, b7], [%30, b8]\n"
                "  %34 = icmp.i1 %32, %9 ne\n"
                "  %35 = zext.i32 %34\n"
                "  %36 = br.void b10\n"
                "b10:\n"
                "  %37 = phi.i32 [%9, b4], [%35, b9]\n"
                "  %38 = phi.i64 [%11, b4], [%33, b9]\n"
                "  %39 = icmp.i1 %37, %9 ne\n"
                "  %40 = cbr.void %39, b11, b12\n"
                "b11:\n"
                "  %41 = add.i64 %21, %38\n"
                "  %42 = ret.void %41\n"
                "b12:\n"
                "  %43 = ret.void %7\n"
                "}\n",
        .blocks = 3,
        .copies = 5,
        .redirects = 3,
        .rounds = 2,
        .merges = 2,
        .flattened = false,
    },
    {
        // The number read in its successor, two levels past it, by a phi there on an edge from a block between, and on
        // an edge out of the successor's subtree: every reader's place (an entry's predecessor) is in the subtree.
        .name = "subtree",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %11 = icmp.i1 %2, %5 lt\n"
                "  %12 = cbr.void %11, b1, b2\n"
                "b1:\n"
                "  %13 = add.i64 %2, %6\n"
                "  %14 = br.void b3\n"
                "b2:\n"
                "  %15 = mul.i64 %5, %6\n"
                "  %16 = icmp.i1 %5, %8 ult\n"
                "  %17 = zext.i32 %16\n"
                "  %18 = br.void b3\n"
                "b3:\n"
                "  %19 = phi.i32 [%10, b1], [%17, b2]\n"
                "  %20 = phi.i64 [%13, b1], [%15, b2]\n"
                "  %21 = icmp.i1 %19, %9 ne\n"
                "  %22 = cbr.void %21, b4, b8\n"
                "b4:\n"
                "  %23 = add.i64 %20, %8\n"
                "  %24 = icmp.i1 %23, %6 ugt\n"
                "  %25 = cbr.void %24, b5, b6\n"
                "b5:\n"
                "  %26 = br.void b7\n"
                "b6:\n"
                "  %27 = br.void b7\n"
                "b7:\n"
                "  %28 = phi.i64 [%20, b5], [%23, b6]\n"
                "  %29 = mul.i64 %28, %20\n"
                "  %30 = br.void b9\n"
                "b8:\n"
                "  %31 = br.void b9\n"
                "b9:\n"
                "  %32 = phi.i64 [%20, b7], [%2, b8]\n"
                "  %33 = phi.i64 [%29, b7], [%5, b8]\n"
                "  %34 = add.i64 %32, %33\n"
                "  %35 = ret.void %34\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // A value of the chain read past the test, in the successor it alone reaches, beside the phi it tests: only phis
        // are merged again, so the block is left alone.
        .name = "chain-escape",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = icmp.i1 %2, %5 lt\n"
                "  %8 = cbr.void %7, b1, b2\n"
                "b1:\n"
                "  %9 = icmp.i1 %5, %3 lt\n"
                "  %10 = zext.i32 %9\n"
                "  %11 = br.void b2\n"
                "b2:\n"
                "  %12 = phi.i32 [%6, b0], [%10, b1]\n"
                "  %13 = icmp.i1 %12, %6 ne\n"
                "  %14 = zext.i64 %13\n"
                "  %15 = cbr.void %13, b3, b4\n"
                "b3:\n"
                "  %16 = add.i64 %14, %2\n"
                "  %17 = zext.i64 %12\n"
                "  %18 = add.i64 %16, %17\n"
                "  %19 = ret.void %18\n"
                "b4:\n"
                "  %20 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // The number read where both successors' paths meet, which neither dominates: left alone.
        .name = "outside",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = icmp.i1 %2, %5 lt\n"
                "  %9 = cbr.void %8, b1, b2\n"
                "b1:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %5, %3 eq\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%7, b1], [%13, b2]\n"
                "  %16 = phi.i64 [%10, b1], [%5, b2]\n"
                "  %17 = icmp.i1 %15, %6 ne\n"
                "  %18 = cbr.void %17, b4, b5\n"
                "b4:\n"
                "  %19 = add.i64 %16, %1\n"
                "  %20 = br.void b6\n"
                "b5:\n"
                "  %21 = br.void b6\n"
                "b6:\n"
                "  %22 = phi.i64 [%19, b4], [%2, b5]\n"
                "  %23 = mul.i64 %22, %16\n"
                "  %24 = ret.void %23\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // The number read in the true successor, which the false one also reaches: no successor that has the block as
        // its only predecessor dominates the reader, so it is left alone.
        .name = "two-predecessors",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = icmp.i1 %2, %5 lt\n"
                "  %9 = cbr.void %8, b1, b2\n"
                "b1:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %5, %3 eq\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%7, b1], [%13, b2]\n"
                "  %16 = phi.i64 [%10, b1], [%5, b2]\n"
                "  %17 = icmp.i1 %15, %6 ne\n"
                "  %18 = cbr.void %17, b4, b5\n"
                "b4:\n"
                "  %19 = add.i64 %16, %1\n"
                "  %20 = ret.void %19\n"
                "b5:\n"
                "  %21 = icmp.i1 %2, %3 eq\n"
                "  %22 = cbr.void %21, b4, b6\n"
                "b6:\n"
                "  %23 = ret.void %1\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // A loop's header, whose counter is read in the body, which it alone reaches, and whose last sum is read past
        // its exit, which it alone reaches: each successor would merge one, but its test reads the counter, and
        // threading it would rotate the loop. Left alone. The `?:` join past the exit, which tests the sum, is
        // threaded: a candidate that isn't a loop's, so that the CFG is built (the cheap scan would refuse a function
        // whose only candidate is the header before it). later-header and backward-order refuse a header by its own
        // test alone.
        .name = "loop-header",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("07") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %7 = br.void b1\n"
                "b1:\n"
                "  %8 = phi.i64 [%3, b0], [%12, b2]\n"
                "  %9 = phi.i64 [%3, b0], [%13, b2]\n"
                "  %10 = icmp.i1 %8, %2 ult\n"
                "  %11 = cbr.void %10, b2, b3\n"
                "b2:\n"
                "  %12 = add.i64 %8, %4\n"
                "  %13 = add.i64 %8, %0\n"
                "  %14 = br.void b1\n"
                "b3:\n"
                "  %15 = icmp.i1 %9, %1 ugt\n"
                "  %16 = cbr.void %15, b4, b5\n"
                "b4:\n"
                "  %17 = br.void b6\n"
                "b5:\n"
                "  %18 = icmp.i1 %9, %4 eq\n"
                "  %19 = zext.i32 %18\n"
                "  %20 = br.void b6\n"
                "b6:\n"
                "  %21 = phi.i32 [%6, b4], [%19, b5]\n"
                "  %22 = icmp.i1 %21, %5 ne\n"
                "  %23 = cbr.void %22, b7, b8\n"
                "b7:\n"
                "  %24 = ret.void %9\n"
                "b8:\n"
                "  %25 = ret.void %4\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 0,
        .flattened = false,
    },
    {
        // A switch keeps its edge, so the block stays: the successor's merge takes the number from it as well as
        // from the two jumps that took copies, and the false successor's phi still takes the number itself on the
        // block's edge.
        .name = "kept-edge",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = lshr.i64 %0, %5\n"
                "  %7 = and.i64 %6, %1\n"
                "  %8 = trunc.i32 %7\n"
                "  %9 = switch.void %2, default b3, [" BUSTER_IR_THREAD_TEST_C("00") ", b1], [" BUSTER_IR_THREAD_TEST_C("01") ", b2]\n"
                "b1:\n"
                "  %10 = icmp.i1 %7, %5 ult\n"
                "  %11 = zext.i32 %10\n"
                "  %12 = mul.i64 %7, %5\n"
                "  %13 = br.void b3\n"
                "b2:\n"
                "  %14 = add.i64 %7, %1\n"
                "  %15 = br.void b3\n"
                "b3:\n"
                "  %16 = phi.i32 [%8, b0], [%11, b1], [%4, b2]\n"
                "  %17 = phi.i64 [%2, b0], [%12, b1], [%14, b2]\n"
                "  %18 = icmp.i1 %16, %3 ne\n"
                "  %19 = cbr.void %18, b4, b5\n"
                "b4:\n"
                "  %20 = add.i64 %17, %6\n"
                "  %21 = ret.void %20\n"
                "b5:\n"
                "  %22 = phi.i64 [%17, b3]\n"
                "  %23 = add.i64 %22, %7\n"
                "  %24 = ret.void %23\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // One number read in the true successor and another in the false one: each successor merges its own.
        .name = "both-successors",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = icmp.i1 %2, %5 lt\n"
                "  %9 = cbr.void %8, b1, b2\n"
                "b1:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = sub.i64 %5, %2\n"
                "  %12 = br.void b3\n"
                "b2:\n"
                "  %13 = icmp.i1 %5, %3 eq\n"
                "  %14 = zext.i32 %13\n"
                "  %15 = mul.i64 %5, %3\n"
                "  %16 = br.void b3\n"
                "b3:\n"
                "  %17 = phi.i32 [%7, b1], [%14, b2]\n"
                "  %18 = phi.i64 [%10, b1], [%15, b2]\n"
                "  %19 = phi.i64 [%11, b1], [%2, b2]\n"
                "  %20 = icmp.i1 %17, %6 ne\n"
                "  %21 = cbr.void %20, b4, b5\n"
                "b4:\n"
                "  %22 = mul.i64 %18, %18\n"
                "  %23 = ret.void %22\n"
                "b5:\n"
                "  %24 = add.i64 %19, %1\n"
                "  %25 = ret.void %24\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 2,
        .flattened = false,
    },
    {
        // The number read past the true successor by a loop's header phi there, on its entry edge and on its back edge
        // from the latch: both places are in the successor's subtree. The loop's header itself isn't threaded.
        .name = "back-edge",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %9 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %10 = icmp.i1 %2, %5 lt\n"
                "  %11 = cbr.void %10, b1, b2\n"
                "b1:\n"
                "  %12 = add.i64 %2, %3\n"
                "  %13 = br.void b3\n"
                "b2:\n"
                "  %14 = icmp.i1 %5, %3 eq\n"
                "  %15 = zext.i32 %14\n"
                "  %16 = br.void b3\n"
                "b3:\n"
                "  %17 = phi.i32 [%7, b1], [%15, b2]\n"
                "  %18 = phi.i64 [%12, b1], [%5, b2]\n"
                "  %19 = icmp.i1 %17, %6 ne\n"
                "  %20 = cbr.void %19, b4, b8\n"
                "b4:\n"
                "  %21 = br.void b5\n"
                "b5:\n"
                "  %22 = phi.i64 [%8, b4], [%26, b6]\n"
                "  %23 = phi.i64 [%18, b4], [%18, b6]\n"
                "  %24 = icmp.i1 %22, %1 ult\n"
                "  %25 = cbr.void %24, b6, b7\n"
                "b6:\n"
                "  %26 = add.i64 %22, %9\n"
                "  %27 = br.void b5\n"
                "b7:\n"
                "  %28 = add.i64 %23, %22\n"
                "  %29 = ret.void %28\n"
                "b8:\n"
                "  %30 = ret.void %1\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // `if (tonumberns(v, n) && x < 2) r = n + 1;`: the first round threads the `&&` join alone, and the number's
        // reader is then reached only through its `?:` join's true successor, which a second round merges it in: the
        // join was left to it although no role kept it.
        .name = "tonumberns-and-test",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %5 = lshr.i64 %0, %4\n"
                "  %6 = and.i64 %5, %1\n"
                "  %7 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %8 = lshr.i64 %0, %7\n"
                "  %9 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %11 = undef.i64\n"
                "  %12 = icmp.i1 %2, %3 eq\n"
                "  %13 = cbr.void %12, b1, b2\n"
                "b1:\n"
                "  %14 = mul.i64 %8, %1\n"
                "  %15 = br.void b4\n"
                "b2:\n"
                "  %16 = icmp.i1 %2, %4 eq\n"
                "  %17 = cbr.void %16, b3, b4\n"
                "b3:\n"
                "  %18 = add.i64 %8, %3\n"
                "  %19 = br.void b4\n"
                "b4:\n"
                "  %20 = phi.i32 [%10, b1], [%9, b2], [%10, b3]\n"
                "  %21 = phi.i64 [%14, b1], [%11, b2], [%18, b3]\n"
                "  %22 = icmp.i1 %20, %9 ne\n"
                "  %23 = cbr.void %22, b5, b6\n"
                "b5:\n"
                "  %24 = icmp.i1 %6, %4 ult\n"
                "  %25 = zext.i32 %24\n"
                "  %26 = br.void b6\n"
                "b6:\n"
                "  %27 = phi.i32 [%9, b4], [%25, b5]\n"
                "  %28 = icmp.i1 %27, %9 ne\n"
                "  %29 = cbr.void %28, b7, b8\n"
                "b7:\n"
                "  %30 = add.i64 %21, %3\n"
                "  %31 = ret.void %30\n"
                "b8:\n"
                "  %32 = ret.void %7\n"
                "}\n",
        .blocks = 2,
        .copies = 3,
        .redirects = 2,
        .rounds = 2,
        .merges = 1,
        .flattened = false,
    },
    {
        // `if (a < b && (b < c ? c < d : 0))`, nothing read past a test: the `&&` join's copy gives the `?:` join, a
        // block of phis that jumped to it, a candidate's shape, which asks for no second round of its own (only the
        // roles and a repair left to the next round do). The `?:` join's phi stays.
        .name = "copied-shape",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i64 " BUSTER_IR_THREAD_TEST_C("06") "\n"
                "  %10 = lshr.i64 %0, %9\n"
                "  %11 = and.i64 %10, %1\n"
                "  %12 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %13 = icmp.i1 %2, %5 lt\n"
                "  %14 = cbr.void %13, b1, b4\n"
                "b1:\n"
                "  %15 = icmp.i1 %5, %8 lt\n"
                "  %16 = cbr.void %15, b2, b3\n"
                "b2:\n"
                "  %17 = icmp.i1 %8, %11 lt\n"
                "  %18 = zext.i32 %17\n"
                "  %19 = br.void b3\n"
                "b3:\n"
                "  %20 = phi.i32 [%18, b2], [%12, b1]\n"
                "  %21 = icmp.i1 %20, %12 ne\n"
                "  %22 = zext.i32 %21\n"
                "  %23 = br.void b4\n"
                "b4:\n"
                "  %24 = phi.i32 [%12, b0], [%22, b3]\n"
                "  %25 = icmp.i1 %24, %12 ne\n"
                "  %26 = cbr.void %25, b5, b6\n"
                "b5:\n"
                "  %27 = ret.void %6\n"
                "b6:\n"
                "  %28 = ret.void %9\n"
                "}\n",
        .blocks = 1,
        .copies = 1,
        .redirects = 1,
        .rounds = 1,
        .merges = 0,
        .flattened = false,
    },
    {
        // Two repairs in one round, one inside the other's subtree: the inner block's phi takes the outer number on an
        // edge, its chain compares them for its false successor's phi (its test reads its truth value alone), its true
        // successor passes both to a call and its false one's phis take the outer number and the comparison on its
        // edge. Every one of these reads, copied or merged into another predecessor, reads the outer merge.
        .name = "nested",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %11 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %12 = icmp.i1 %2, %5 lt\n"
                "  %13 = cbr.void %12, b1, b2\n"
                "b1:\n"
                "  %14 = add.i64 %2, %6\n"
                "  %15 = br.void b3\n"
                "b2:\n"
                "  %16 = mul.i64 %5, %6\n"
                "  %17 = icmp.i1 %5, %8 ult\n"
                "  %18 = zext.i32 %17\n"
                "  %19 = br.void b3\n"
                "b3:\n"
                "  %20 = phi.i32 [%10, b1], [%18, b2]\n"
                "  %21 = phi.i64 [%14, b1], [%16, b2]\n"
                "  %22 = icmp.i1 %20, %9 ne\n"
                "  %23 = cbr.void %22, b4, b10\n"
                "b4:\n"
                "  %24 = icmp.i1 %8, %11 eq\n"
                "  %25 = cbr.void %24, b5, b6\n"
                "b5:\n"
                "  %26 = br.void b7\n"
                "b6:\n"
                "  %27 = icmp.i1 %5, %11 ugt\n"
                "  %28 = zext.i32 %27\n"
                "  %29 = br.void b7\n"
                "b7:\n"
                "  %30 = phi.i32 [%10, b5], [%28, b6]\n"
                "  %31 = phi.i64 [%21, b5], [%5, b6]\n"
                "  %32 = icmp.i1 %30, %9 ne\n"
                "  %33 = icmp.i1 %31, %21 ule\n"
                "  %34 = zext.i64 %33\n"
                "  %35 = cbr.void %32, b8, b9\n"
                "b8:\n"
                "  %36 = call.i64 @1(%31, %21) fixed=(i64, i64) variadic=0\n"
                "  %37 = ret.void %36\n"
                "b9:\n"
                "  %38 = phi.i64 [%21, b7]\n"
                "  %39 = phi.i64 [%34, b7]\n"
                "  %40 = add.i64 %38, %39\n"
                "  %41 = ret.void %40\n"
                "b10:\n"
                "  %42 = ret.void %6\n"
                "}\n",
        .blocks = 2,
        .copies = 4,
        .redirects = 0,
        .rounds = 1,
        .merges = 2,
        .flattened = false,
    },
    {
        // A vector merged again where a ternary intrinsic reads it as its third operand, which the extra array holds.
        // The executor runs no vectors: the result is verified as complete SSA alone.
        .name = "ternary",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %3 = and.i64 %1, %2\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %7 = load.v4i32 %0 align=16\n"
                "  %8 = icmp.i1 %3, %4 eq\n"
                "  %9 = cbr.void %8, b1, b2\n"
                "b1:\n"
                "  %10 = intrinsic.v4i32 %7, %7, %7 funnel 1\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %3, %2 eq\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%6, b1], [%13, b2]\n"
                "  %16 = phi.v4i32 [%10, b1], [%7, b2]\n"
                "  %17 = icmp.i1 %15, %5 ne\n"
                "  %18 = cbr.void %17, b4, b5\n"
                "b4:\n"
                "  %19 = intrinsic.v4i32 %7, %7, %16 funnel 1\n"
                "  %20 = store.void %0, %19 align=16\n"
                "  %21 = ret.void\n"
                "b5:\n"
                "  %22 = ret.void\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
        .structural = true,
    },
    {
        // `if (x < y && (n = ...)) ...; if (z > x && (m = ...)) r = m + y;`: the first round repairs the first join and
        // leaves the second, its successor, to the next, though its number's reader is already covered: only the
        // threaded join's successor merges a number in that round.
        .name = "deferred-repair",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = icmp.i1 %2, %5 lt\n"
                "  %11 = cbr.void %10, b1, b2\n"
                "b1:\n"
                "  %12 = icmp.i1 %5, %8 lt\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b2\n"
                "b2:\n"
                "  %15 = phi.i32 [%9, b0], [%13, b1]\n"
                "  %16 = phi.i64 [%5, b0], [%8, b1]\n"
                "  %17 = icmp.i1 %15, %9 ne\n"
                "  %18 = cbr.void %17, b3, b4\n"
                "b3:\n"
                "  %19 = icmp.i1 %8, %2 ugt\n"
                "  %20 = zext.i32 %19\n"
                "  %21 = mul.i64 %16, %6\n"
                "  %22 = br.void b4\n"
                "b4:\n"
                "  %23 = phi.i32 [%9, b2], [%20, b3]\n"
                "  %24 = phi.i64 [%2, b2], [%21, b3]\n"
                "  %25 = icmp.i1 %23, %9 ne\n"
                "  %26 = cbr.void %25, b5, b6\n"
                "b5:\n"
                "  %27 = add.i64 %24, %5\n"
                "  %28 = ret.void %27\n"
                "b6:\n"
                "  %29 = ret.void %1\n"
                "}\n",
        .blocks = 2,
        .copies = 2,
        .redirects = 3,
        .rounds = 2,
        .merges = 2,
        .flattened = false,
    },
    {
        // The test is `(1 & 2) != 0` on every edge (Lua's checktab after inlining), so both conditional predecessors
        // would go to the false successor and the block would be left unreachable: the true successor, which merges the
        // number read there, would have no predecessor, an empty merge. Left alone, for SCCP to fold.
        .name = "stranded-merge",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = ret.void %1\n"
                "b4:\n"
                "  %14 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %15 = and.i32 %4, %5\n"
                "  %16 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %17 = icmp.i1 %15, %16 ne\n"
                "  %18 = cbr.void %17, b5, b6\n"
                "b5:\n"
                "  %19 = mul.i64 %14, %3\n"
                "  %20 = ret.void %19\n"
                "b6:\n"
                "  %21 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // As stranded-merge, where the true successor's own phi takes the number on the block's edge: its only entry
        // would go with the block. Left alone (#2607's rule left the phi empty).
        .name = "stranded-phi",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = ret.void %1\n"
                "b4:\n"
                "  %14 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %15 = and.i32 %4, %5\n"
                "  %16 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %17 = icmp.i1 %15, %16 ne\n"
                "  %18 = cbr.void %17, b5, b6\n"
                "b5:\n"
                "  %19 = phi.i64 [%14, b4]\n"
                "  %20 = mul.i64 %19, %3\n"
                "  %21 = ret.void %20\n"
                "b6:\n"
                "  %22 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // As stranded-phi, the test `(1 | 2) != 0` on every edge: the redirects all go to the true successor, and the
        // false one's phi would lose its only entry. Left alone.
        .name = "stranded-phi-false",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = ret.void %1\n"
                "b4:\n"
                "  %14 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %15 = or.i32 %4, %5\n"
                "  %16 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %17 = icmp.i1 %15, %16 ne\n"
                "  %18 = cbr.void %17, b5, b6\n"
                "b5:\n"
                "  %19 = ret.void %3\n"
                "b6:\n"
                "  %20 = phi.i64 [%14, b4]\n"
                "  %21 = mul.i64 %20, %3\n"
                "  %22 = ret.void %21\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // As stranded-merge, where the true successor reads the masked parameter, not the number: it has no phi and
        // merges nothing, so it may lose its only predecessor. Both conditional predecessors go to the false successor,
        // and the block and the true successor are left unreachable.
        .name = "stranded-plain",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = ret.void %1\n"
                "b4:\n"
                "  %14 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %15 = and.i32 %4, %5\n"
                "  %16 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %17 = icmp.i1 %15, %16 ne\n"
                "  %18 = cbr.void %17, b5, b6\n"
                "b5:\n"
                "  %19 = mul.i64 %2, %3\n"
                "  %20 = ret.void %19\n"
                "b6:\n"
                "  %21 = ret.void %3\n"
                "}\n",
        .blocks = 1,
        .copies = 0,
        .redirects = 2,
        .rounds = 1,
        .merges = 0,
        .flattened = true,
    },
    {
        // As stranded-plain, the test true on every edge and the false successor the one left without a predecessor.
        .name = "stranded-plain-false",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = ret.void %1\n"
                "b4:\n"
                "  %14 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %15 = or.i32 %4, %5\n"
                "  %16 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %17 = icmp.i1 %15, %16 ne\n"
                "  %18 = cbr.void %17, b5, b6\n"
                "b5:\n"
                "  %19 = ret.void %3\n"
                "b6:\n"
                "  %20 = mul.i64 %2, %3\n"
                "  %21 = ret.void %20\n"
                "}\n",
        .blocks = 1,
        .copies = 0,
        .redirects = 2,
        .rounds = 1,
        .merges = 0,
        .flattened = true,
    },
    {
        // Both predecessors redirected, one to each successor: the true one, which merges the number, keeps the one
        // that chose it.
        .name = "redirect-merge",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = ret.void %1\n"
                "b4:\n"
                "  %14 = phi.i32 [%4, b1], [%5, b2]\n"
                "  %15 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %16 = icmp.i1 %14, %5 ne\n"
                "  %17 = cbr.void %16, b5, b6\n"
                "b5:\n"
                "  %18 = mul.i64 %15, %3\n"
                "  %19 = ret.void %18\n"
                "b6:\n"
                "  %20 = ret.void %3\n"
                "}\n",
        .blocks = 1,
        .copies = 0,
        .redirects = 2,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // A switch keeps its edge and the conditional predecessor goes to the false successor: the block stays, so the
        // true successor, which merges the number, keeps the block's own edge.
        .name = "kept-redirect",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %5 = lshr.i64 %0, %4\n"
                "  %6 = and.i64 %5, %1\n"
                "  %7 = trunc.i32 %6\n"
                "  %8 = switch.void %2, default b2, [" BUSTER_IR_THREAD_TEST_C("00") ", b1]\n"
                "b1:\n"
                "  %9 = icmp.i1 %6, %1 eq\n"
                "  %10 = cbr.void %9, b3, b2\n"
                "b2:\n"
                "  %11 = phi.i32 [%7, b0], [%3, b1]\n"
                "  %12 = phi.i64 [%5, b0], [%6, b1]\n"
                "  %13 = icmp.i1 %11, %3 ne\n"
                "  %14 = cbr.void %13, b4, b5\n"
                "b3:\n"
                "  %15 = ret.void %4\n"
                "b4:\n"
                "  %16 = mul.i64 %12, %1\n"
                "  %17 = ret.void %16\n"
                "b5:\n"
                "  %18 = ret.void %6\n"
                "}\n",
        .blocks = 1,
        .copies = 0,
        .redirects = 1,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // `n = c ? a : b; if (n < 3) r = n * n;` behind a switch that keeps its edge: the block's only phi is the one
        // tested and read past it, a data select's join, which no edge decides: left alone (ir_thread_decides).
        // select-kept is its repair with a truth value beside the number.
        .name = "single-phi-kept",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = switch.void %2, default b3, [" BUSTER_IR_THREAD_TEST_C("00") ", b1], [" BUSTER_IR_THREAD_TEST_C("01") ", b2]\n"
                "b1:\n"
                "  %7 = add.i64 %5, %3\n"
                "  %8 = br.void b3\n"
                "b2:\n"
                "  %9 = mul.i64 %5, %3\n"
                "  %10 = br.void b3\n"
                "b3:\n"
                "  %11 = phi.i64 [%5, b0], [%7, b1], [%9, b2]\n"
                "  %12 = icmp.i1 %11, %1 ult\n"
                "  %13 = cbr.void %12, b4, b5\n"
                "b4:\n"
                "  %14 = icmp.i1 %2, %3 eq\n"
                "  %15 = select.i64 %14, %3, %11\n"
                "  %16 = mul.i64 %15, %11\n"
                "  %17 = ret.void %16\n"
                "b5:\n"
                "  %18 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // single-phi-kept's number beside a truth value that the switch's edge keeps and the jumps make 1 and 0: every
        // copy reaches the merge besides the block's own edge, and a select reads the number as its false value.
        .name = "select-kept",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = trunc.i32 %5\n"
                "  %9 = switch.void %2, default b3, [" BUSTER_IR_THREAD_TEST_C("00") ", b1], [" BUSTER_IR_THREAD_TEST_C("01") ", b2]\n"
                "b1:\n"
                "  %10 = add.i64 %5, %3\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = mul.i64 %5, %3\n"
                "  %13 = br.void b3\n"
                "b3:\n"
                "  %14 = phi.i32 [%8, b0], [%7, b1], [%6, b2]\n"
                "  %15 = phi.i64 [%5, b0], [%10, b1], [%12, b2]\n"
                "  %16 = icmp.i1 %14, %6 ne\n"
                "  %17 = cbr.void %16, b4, b5\n"
                "b4:\n"
                "  %18 = icmp.i1 %2, %3 eq\n"
                "  %19 = select.i64 %18, %3, %15\n"
                "  %20 = mul.i64 %19, %15\n"
                "  %21 = ret.void %20\n"
                "b5:\n"
                "  %22 = ret.void %3\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // As stranded-phi, where the true successor, whose phi takes the number on the block's edge, has a predecessor
        // of its own: its phi keeps that entry, so the block is threaded and left unreachable.
        .name = "stranded-shared",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = br.void b5\n"
                "b4:\n"
                "  %14 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %15 = and.i32 %4, %5\n"
                "  %16 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %17 = icmp.i1 %15, %16 ne\n"
                "  %18 = cbr.void %17, b5, b6\n"
                "b5:\n"
                "  %19 = phi.i64 [%14, b4], [%1, b3]\n"
                "  %20 = mul.i64 %19, %3\n"
                "  %21 = ret.void %20\n"
                "b6:\n"
                "  %22 = ret.void %3\n"
                "}\n",
        .blocks = 1,
        .copies = 0,
        .redirects = 2,
        .rounds = 1,
        .merges = 0,
        .flattened = false,
    },
    {
        // As stranded-shared, the test true on every edge and the false successor the one with a predecessor and a phi
        // of its own.
        .name = "stranded-shared-false",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i32 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 ult\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = icmp.i1 %2, %1 eq\n"
                "  %9 = cbr.void %8, b3, b4\n"
                "b2:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = icmp.i1 %10, %1 ugt\n"
                "  %12 = cbr.void %11, b4, b3\n"
                "b3:\n"
                "  %13 = br.void b6\n"
                "b4:\n"
                "  %14 = phi.i64 [%2, b1], [%10, b2]\n"
                "  %15 = or.i32 %4, %5\n"
                "  %16 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %17 = icmp.i1 %15, %16 ne\n"
                "  %18 = cbr.void %17, b5, b6\n"
                "b5:\n"
                "  %19 = ret.void %3\n"
                "b6:\n"
                "  %20 = phi.i64 [%14, b4], [%1, b3]\n"
                "  %21 = mul.i64 %20, %3\n"
                "  %22 = ret.void %21\n"
                "}\n",
        .blocks = 1,
        .copies = 0,
        .redirects = 2,
        .rounds = 1,
        .merges = 0,
        .flattened = false,
    },
    {
        // The number read past both successors, each of which the other also reaches (a cycle of two blocks that only
        // the test enters): no successor has the block alone, so no walk runs for it, which the digest's count of the
        // blocks walked pins.
        .name = "no-lone-successor",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %9 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %10 = icmp.i1 %2, %5 lt\n"
                "  %11 = cbr.void %10, b1, b2\n"
                "b1:\n"
                "  %12 = add.i64 %2, %3\n"
                "  %13 = br.void b3\n"
                "b2:\n"
                "  %14 = icmp.i1 %5, %3 eq\n"
                "  %15 = zext.i32 %14\n"
                "  %16 = br.void b3\n"
                "b3:\n"
                "  %17 = phi.i32 [%7, b1], [%15, b2]\n"
                "  %18 = phi.i64 [%12, b1], [%5, b2]\n"
                "  %19 = icmp.i1 %17, %6 ne\n"
                "  %20 = cbr.void %19, b4, b5\n"
                "b4:\n"
                "  %21 = phi.i64 [%3, b3], [%26, b5]\n"
                "  %22 = icmp.i1 %21, %8 eq\n"
                "  %23 = cbr.void %22, b6, b5\n"
                "b5:\n"
                "  %24 = phi.i64 [%9, b3], [%21, b4]\n"
                "  %25 = icmp.i1 %24, %8 eq\n"
                "  %26 = sub.i64 %24, %9\n"
                "  %27 = cbr.void %25, b6, b4\n"
                "b6:\n"
                "  %28 = phi.i64 [%21, b4], [%24, b5]\n"
                "  %29 = add.i64 %28, %18\n"
                "  %30 = ret.void %29\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // The entry as the block's false successor, whose only edge in the CFG is the block's: the function's start
        // enters it too, and it dominates every block, the number's reader in the true successor among them. Only the
        // true successor merges the number (the entry can have no phi, and its cover bit would have kept both). The
        // truth value is 1 on every path, so the edge back to the entry never runs and the executor's trials end.
        .name = "entry-successor",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = icmp.i1 %2, %5 lt\n"
                "  %9 = cbr.void %8, b1, b2\n"
                "b1:\n"
                "  %10 = add.i64 %2, %3\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %5, %1 ule\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%7, b1], [%13, b2]\n"
                "  %16 = phi.i64 [%10, b1], [%5, b2]\n"
                "  %17 = icmp.i1 %15, %6 ne\n"
                "  %18 = cbr.void %17, b4, b0\n"
                "b4:\n"
                "  %19 = add.i64 %16, %1\n"
                "  %20 = ret.void %19\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // The entry as the block's true successor, the false one a loop's header, which its back edge also enters, and
        // which reads the number: no successor that the block alone enters dominates the reader, so it is left alone
        // (the entry's cover bit would have put the merge at the function's start). The truth value is 0 on every path.
        .name = "entry-successor-true",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %8 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %9 = icmp.i1 %2, %5 lt\n"
                "  %10 = cbr.void %9, b1, b2\n"
                "b1:\n"
                "  %11 = add.i64 %2, %3\n"
                "  %12 = br.void b3\n"
                "b2:\n"
                "  %13 = icmp.i1 %5, %1 ugt\n"
                "  %14 = zext.i32 %13\n"
                "  %15 = br.void b3\n"
                "b3:\n"
                "  %16 = phi.i32 [%6, b1], [%14, b2]\n"
                "  %17 = phi.i64 [%11, b1], [%5, b2]\n"
                "  %18 = icmp.i1 %16, %6 ne\n"
                "  %19 = cbr.void %18, b0, b4\n"
                "b4:\n"
                "  %20 = phi.i64 [%7, b3], [%24, b5]\n"
                "  %21 = add.i64 %17, %20\n"
                "  %22 = icmp.i1 %20, %2 ult\n"
                "  %23 = cbr.void %22, b5, b6\n"
                "b5:\n"
                "  %24 = add.i64 %20, %8\n"
                "  %25 = br.void b4\n"
                "b6:\n"
                "  %26 = ret.void %21\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // `long y = x < lo ? lo : x; return y > hi ? hi : y;` (the driver's if_clamp): the first `?:`'s join is a data
        // select, its phi tested and read past it on the second's edge, and no edge decides its test. Left alone, so
        // that if-conversion makes both `?:`s selects (#2608's head threaded it, and the clamp took three branches).
        .name = "if-clamp",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = icmp.i1 %2, %5 lt\n"
                "  %10 = cbr.void %9, b1, b2\n"
                "b1:\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = br.void b3\n"
                "b3:\n"
                "  %13 = phi.i64 [%5, b1], [%2, b2]\n"
                "  %14 = icmp.i1 %13, %8 gt\n"
                "  %15 = cbr.void %14, b4, b5\n"
                "b4:\n"
                "  %16 = br.void b6\n"
                "b5:\n"
                "  %17 = br.void b6\n"
                "b6:\n"
                "  %18 = phi.i64 [%8, b4], [%13, b5]\n"
                "  %19 = ret.void %18\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // `y = x < 1 ? 0 : x; return y > 2 ? 2 : y;`, a clamp to constants: the edge of y's 0 decides the test, but the
        // test reads y, which is read past it. Left alone, for if-conversion.
        .name = "clamp-constant",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %5 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = icmp.i1 %2, %3 lt\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = br.void b3\n"
                "b2:\n"
                "  %9 = br.void b3\n"
                "b3:\n"
                "  %10 = phi.i64 [%4, b1], [%2, b2]\n"
                "  %11 = icmp.i1 %10, %5 gt\n"
                "  %12 = cbr.void %11, b4, b5\n"
                "b4:\n"
                "  %13 = br.void b6\n"
                "b5:\n"
                "  %14 = br.void b6\n"
                "b6:\n"
                "  %15 = phi.i64 [%5, b4], [%10, b5]\n"
                "  %16 = ret.void %15\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // `ok = c ? (n = a, a > 1) : (n = b, b < 2); if (ok) r = n + 1;`: the number isn't tested, but the truth value
        // is a comparison on every edge, so no edge decides the test. Left alone.
        .name = "undecided",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %11 = icmp.i1 %2, %10 ult\n"
                "  %12 = cbr.void %11, b1, b2\n"
                "b1:\n"
                "  %13 = icmp.i1 %5, %10 ugt\n"
                "  %14 = zext.i32 %13\n"
                "  %15 = br.void b3\n"
                "b2:\n"
                "  %16 = icmp.i1 %8, %3 ult\n"
                "  %17 = zext.i32 %16\n"
                "  %18 = br.void b3\n"
                "b3:\n"
                "  %19 = phi.i32 [%14, b1], [%17, b2]\n"
                "  %20 = phi.i64 [%5, b1], [%8, b2]\n"
                "  %21 = icmp.i1 %19, %9 ne\n"
                "  %22 = cbr.void %21, b4, b5\n"
                "b4:\n"
                "  %23 = add.i64 %20, %10\n"
                "  %24 = ret.void %23\n"
                "b5:\n"
                "  %25 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // A loop's header that the entering edge decides (`while (go) { f(n); go = 0; n = b; }`), whose number the body,
        // which it alone reaches, passes to a call: its back edge comes from a later block, the cheap test of a loop's
        // header, though every phi input is the entry's. Left alone.
        .name = "later-header",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = lshr.i64 %0, %5\n"
                "  %7 = and.i64 %6, %1\n"
                "  %8 = br.void b1\n"
                "b1:\n"
                "  %9 = phi.i32 [%4, b0], [%3, b2]\n"
                "  %10 = phi.i64 [%2, b0], [%7, b2]\n"
                "  %11 = icmp.i1 %9, %3 ne\n"
                "  %12 = cbr.void %11, b2, b3\n"
                "b2:\n"
                "  %13 = call.i64 @1(%10, %2) fixed=(i64, i64) variadic=0\n"
                "  %14 = br.void b1\n"
                "b3:\n"
                "  %15 = ret.void %7\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // A `?:` join, not a loop's header, whose number one predecessor takes from a block laid out after it: the
        // cheap test of a loop's header (a phi input at or past the join) refuses it, a false refusal. The plain `?:`
        // join on its false successor's path is threaded, so that the CFG is built: the cheap scan alone would refuse
        // a function whose only candidate takes such a value.
        .name = "backward-order",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = lshr.i64 %0, %5\n"
                "  %7 = and.i64 %6, %1\n"
                "  %8 = icmp.i1 %2, %7 ult\n"
                "  %9 = cbr.void %8, b3, b2\n"
                "b1:\n"
                "  %10 = br.void b2\n"
                "b2:\n"
                "  %11 = phi.i32 [%3, b0], [%4, b1]\n"
                "  %12 = phi.i64 [%7, b0], [%15, b1]\n"
                "  %13 = icmp.i1 %11, %3 ne\n"
                "  %14 = cbr.void %13, b4, b5\n"
                "b3:\n"
                "  %15 = mul.i64 %2, %5\n"
                "  %16 = br.void b1\n"
                "b4:\n"
                "  %17 = add.i64 %12, %1\n"
                "  %18 = ret.void %17\n"
                "b5:\n"
                "  %19 = icmp.i1 %2, %5 eq\n"
                "  %20 = cbr.void %19, b7, b6\n"
                "b6:\n"
                "  %21 = br.void b7\n"
                "b7:\n"
                "  %22 = phi.i32 [%4, b5], [%3, b6]\n"
                "  %23 = icmp.i1 %22, %3 ne\n"
                "  %24 = cbr.void %23, b8, b9\n"
                "b8:\n"
                "  %25 = ret.void %7\n"
                "b9:\n"
                "  %26 = ret.void %5\n"
                "}\n",
        .blocks = 1,
        .copies = 1,
        .redirects = 1,
        .rounds = 1,
        .merges = 0,
        .flattened = false,
    },
    {
        // A loop's header laid out after its body, its only back edge's block, with every phi input the entry's: the
        // cheap test doesn't see it, and it is threaded, rotating the loop exactly. The entry's copy decides it for the
        // body, and the body's copy, its back edge's, for the exit; the body merges the number from both. The number is
        // the block's first phi, before the truth value the test reads.
        .name = "rotated-header",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %4 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %5 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %6 = lshr.i64 %0, %5\n"
                "  %7 = and.i64 %6, %1\n"
                "  %8 = br.void b2\n"
                "b1:\n"
                "  %9 = call.i64 @1(%11, %2) fixed=(i64, i64) variadic=0\n"
                "  %10 = br.void b2\n"
                "b2:\n"
                "  %11 = phi.i64 [%2, b0], [%7, b1]\n"
                "  %12 = phi.i32 [%4, b0], [%3, b1]\n"
                "  %13 = icmp.i1 %12, %3 ne\n"
                "  %14 = cbr.void %13, b1, b3\n"
                "b3:\n"
                "  %15 = ret.void %7\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // `ok = c ? 1 : (n = b, b > 1); if (ok < t) r = n + 1;`: the truth value is 1 on an edge, but the test compares
        // it with a value from outside the block, which no edge makes a constant. Left alone.
        .name = "outside-operand",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = trunc.i32 %5\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %9 = icmp.i1 %2, %8 ult\n"
                "  %10 = cbr.void %9, b1, b2\n"
                "b1:\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %2, %8 ugt\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%7, b1], [%13, b2]\n"
                "  %16 = phi.i64 [%5, b1], [%2, b2]\n"
                "  %17 = icmp.i1 %15, %6 ult\n"
                "  %18 = cbr.void %17, b4, b5\n"
                "b4:\n"
                "  %19 = add.i64 %16, %8\n"
                "  %20 = ret.void %19\n"
                "b5:\n"
                "  %21 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // `n = c ? 0 : b; if (p < q) r = n + 1;`: the number's join, read past its test, and a test that reads none of
        // its phis (it compares values from outside the block). No truth value is joined there: left alone.
        .name = "test-outside",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i64 " BUSTER_IR_THREAD_TEST_C("04") "\n"
                "  %7 = lshr.i64 %0, %6\n"
                "  %8 = and.i64 %7, %1\n"
                "  %9 = const.i64 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %10 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %11 = icmp.i1 %2, %10 ult\n"
                "  %12 = cbr.void %11, b1, b2\n"
                "b1:\n"
                "  %13 = br.void b3\n"
                "b2:\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i64 [%9, b1], [%8, b2]\n"
                "  %16 = icmp.i1 %5, %8 lt\n"
                "  %17 = cbr.void %16, b4, b5\n"
                "b4:\n"
                "  %18 = add.i64 %15, %10\n"
                "  %19 = ret.void %18\n"
                "b5:\n"
                "  %20 = ret.void %3\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // `n = c ? a * 3 : a + 1; if ((3 & 1) == 1) r = n + a;`: a test of constants alone, which every edge decides
        // (SCCP folds it, after threading), reads none of the join's phis: no truth value is joined there, and the
        // number read past it leaves it alone. Read as a phi, the test's first operation (`and %1, %2`) has entries
        // from the extra array's second word, which name the constant %2: a rule that took an operation for the tested
        // phi would find a deciding edge there and repair the join.
        .name = "constant-test",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %3 = and.i64 %0, %1\n"
                "  %4 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %5 = lshr.i64 %0, %4\n"
                "  %6 = icmp.i1 %3, %2 eq\n"
                "  %7 = cbr.void %6, b1, b2\n"
                "b1:\n"
                "  %8 = mul.i64 %5, %1\n"
                "  %9 = br.void b3\n"
                "b2:\n"
                "  %10 = add.i64 %5, %2\n"
                "  %11 = br.void b3\n"
                "b3:\n"
                "  %12 = phi.i64 [%8, b1], [%10, b2]\n"
                "  %13 = and.i64 %1, %2\n"
                "  %14 = icmp.i1 %13, %2 eq\n"
                "  %15 = cbr.void %14, b4, b5\n"
                "b4:\n"
                "  %16 = add.i64 %12, %5\n"
                "  %17 = ret.void %16\n"
                "b5:\n"
                "  %18 = ret.void %1\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // The number read two blocks into the false successor's subtree, whose walk back meets that successor: the false
        // successor merges it.
        .name = "false-subtree",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = icmp.i1 %2, %5 lt\n"
                "  %9 = cbr.void %8, b1, b2\n"
                "b1:\n"
                "  %10 = mul.i64 %5, %3\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %5, %7 ugt\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%6, b1], [%13, b2]\n"
                "  %16 = phi.i64 [%10, b1], [%2, b2]\n"
                "  %17 = icmp.i1 %15, %6 ne\n"
                "  %18 = cbr.void %17, b4, b5\n"
                "b4:\n"
                "  %19 = ret.void %3\n"
                "b5:\n"
                "  %20 = br.void b6\n"
                "b6:\n"
                "  %21 = add.i64 %16, %7\n"
                "  %22 = ret.void %21\n"
                "}\n",
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    },
    {
        // The number read twice where both successors, each of which only the test enters, meet: the first read's walk
        // meets both and leaves the number no successor, so the second read walks no more. Left alone.
        .name = "join-reader",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i64 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = icmp.i1 %2, %5 lt\n"
                "  %9 = cbr.void %8, b1, b2\n"
                "b1:\n"
                "  %10 = mul.i64 %5, %3\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %5, %7 ugt\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%6, b1], [%13, b2]\n"
                "  %16 = phi.i64 [%10, b1], [%2, b2]\n"
                "  %17 = icmp.i1 %15, %6 ne\n"
                "  %18 = cbr.void %17, b4, b5\n"
                "b4:\n"
                "  %19 = br.void b6\n"
                "b5:\n"
                "  %20 = br.void b6\n"
                "b6:\n"
                "  %21 = add.i64 %16, %7\n"
                "  %22 = mul.i64 %21, %16\n"
                "  %23 = ret.void %22\n"
                "}\n",
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    },
    {
        // Two `?:` joins repaired in one round, the second in the first's true successor's subtree, its number its first
        // phi: the first's evaluation knows both its phis, and the second's test must still read its truth value alone.
        .name = "two-candidates",
        .text = "function (i64) -> i64 {\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = const.i64 " BUSTER_IR_THREAD_TEST_C("03") "\n"
                "  %2 = and.i64 %0, %1\n"
                "  %3 = const.i64 " BUSTER_IR_THREAD_TEST_C("02") "\n"
                "  %4 = lshr.i64 %0, %3\n"
                "  %5 = and.i64 %4, %1\n"
                "  %6 = const.i32 " BUSTER_IR_THREAD_TEST_C("00") "\n"
                "  %7 = const.i32 " BUSTER_IR_THREAD_TEST_C("01") "\n"
                "  %8 = const.i64 " BUSTER_IR_THREAD_TEST_C("05") "\n"
                "  %9 = icmp.i1 %2, %5 lt\n"
                "  %10 = cbr.void %9, b1, b2\n"
                "b1:\n"
                "  %11 = br.void b3\n"
                "b2:\n"
                "  %12 = icmp.i1 %5, %3 ugt\n"
                "  %13 = zext.i32 %12\n"
                "  %14 = br.void b3\n"
                "b3:\n"
                "  %15 = phi.i32 [%7, b1], [%13, b2]\n"
                "  %16 = phi.i64 [%8, b1], [%2, b2]\n"
                "  %17 = icmp.i1 %15, %6 ne\n"
                "  %18 = cbr.void %17, b4, b9\n"
                "b4:\n"
                "  %19 = add.i64 %16, %5\n"
                "  %20 = icmp.i1 %19, %3 ult\n"
                "  %21 = cbr.void %20, b5, b6\n"
                "b5:\n"
                "  %22 = br.void b7\n"
                "b6:\n"
                "  %23 = icmp.i1 %2, %5 eq\n"
                "  %24 = zext.i32 %23\n"
                "  %25 = br.void b7\n"
                "b7:\n"
                "  %26 = phi.i64 [%19, b5], [%5, b6]\n"
                "  %27 = phi.i32 [%7, b5], [%24, b6]\n"
                "  %28 = icmp.i1 %27, %6 ne\n"
                "  %29 = cbr.void %28, b8, b9\n"
                "b8:\n"
                "  %30 = mul.i64 %26, %3\n"
                "  %31 = ret.void %30\n"
                "b9:\n"
                "  %32 = ret.void %1\n"
                "}\n",
        .blocks = 2,
        .copies = 4,
        .redirects = 0,
        .rounds = 1,
        .merges = 2,
        .flattened = false,
    },
};

#undef BUSTER_IR_THREAD_TEST_C

// How many of `f`'s instructions are `op`.
static uint32_t ir_thread_test_count(const IrFunction* f, uint32_t op)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        count += f->op[i] == op;
    }
    return count;
}

static void ir_thread_test_fixture(IrOptimizeTest* test, const IrThreadTestFixture* fixture, Arena* input, Arena* vector, Arena* optimized, Arena* scratch)
{
    const uint64_t im = input->length;
    const uint64_t vm = vector->length;
    const uint64_t om = optimized->length;
    const uint64_t tm = scratch->length;
    char message[160] = { 0 };
    IrFunction f = { 0 };
    const String8 text = {
        .pointer = (char*)fixture->text,
        .length = strlen(fixture->text),
    };
    const IrParseResult parsed = ir_parse(text, input, scratch, &f);
    snprintf(message, sizeof(message), "threading %s: the fixture parses as complete SSA", fixture->name);
    const bool valid = parsed.error == IR_PARSE_OK && ir_verify_complete(&f, scratch).error == IR_OK;
    ir_optimize_test_expect(test, valid, message);
    if (valid)
    {
        IrThreadResult v = { 0 };
        const IrCheck vc = ir_thread(&f, vector, scratch, &v);
        snprintf(message, sizeof(message), "threading %s: it's accepted and scratch restored", fixture->name);
        ir_optimize_test_expect(test, vc.error == IR_OK && scratch->length == tm, message);
        if (vc.error == IR_OK)
        {
            snprintf(message, sizeof(message), "threading %s: %u blocks, %u copies, %u redirects in %u rounds, %u merges", fixture->name, fixture->blocks, fixture->copies, fixture->redirects, fixture->rounds,
                     fixture->merges);
            const bool counted = v.blocks == fixture->blocks && v.copies == fixture->copies && v.redirects == fixture->redirects && v.rounds == fixture->rounds && v.merges == fixture->merges;
            ir_optimize_test_expect(test, counted, message);
            if (!counted)
            {
                fprintf(stderr, "  threading %s: %u blocks, %u copies, %u redirects in %u rounds, %u merges\n", fixture->name, v.blocks, v.copies, v.redirects, v.rounds, v.merges);
            }
            ir_optimize_test_fold_function(&test->thread_digest, &v.function);
            ir_optimize_test_fold(&test->thread_digest, v.blocks);
            ir_optimize_test_fold(&test->thread_digest, v.copies);
            ir_optimize_test_fold(&test->thread_digest, v.redirects);
            ir_optimize_test_fold(&test->thread_digest, v.rounds);
            ir_optimize_test_fold(&test->thread_digest, v.merges);
            ir_optimize_test_fold(&test->thread_digest, v.attempts);
            ir_optimize_test_fold(&test->thread_digest, v.walked);
            snprintf(message, sizeof(message), "threading %s: the result is complete SSA", fixture->name);
            const IrCheck complete = ir_verify_complete(&v.function, scratch);
            ir_optimize_test_expect(test, complete.error == IR_OK, message);
            if (complete.error != IR_OK)
            {
                fprintf(stderr, "  threading %s: error=%u instruction=%u detail=%u\n", fixture->name, complete.error, complete.instruction, complete.detail);
            }
            if (fixture->blocks != 0)
            {
                // The rewrite's phi entries, calls' arguments and ternaries' operands fill the extra array it counted: an
                // undercount would write past the arena's used bytes.
                snprintf(message, sizeof(message), "threading %s: the result's extra array is within its arena's bytes", fixture->name);
                ir_optimize_test_expect(test, (const uint8_t*)(v.function.extra + v.function.extra_count) <= vector->base + vector->length, message);
            }
            if (fixture->blocks == 0)
            {
                snprintf(message, sizeof(message), "threading %s: a refusal borrows the unchanged input", fixture->name);
                ir_optimize_test_expect(test, v.function.op == f.op && v.function.extra == f.extra && vector->length == vm, message);
            }
            for (uint32_t trial = 0; !fixture->structural && trial < 260; trial += 1)
            {
                const uint64_t parameter = trial < 256 ? trial : trial == 256 ? UINT64_MAX : trial == 257 ? UINT64_C(1) << 63 : ir_optimize_test_random(test);
                const IrOptimizeExecution before = ir_optimize_test_execute(&f, scratch, parameter, trial & 1);
                const IrOptimizeExecution after = ir_optimize_test_execute(&v.function, scratch, parameter, trial & 1);
                snprintf(message, sizeof(message), "threading %s: independently executed values and effects preserved", fixture->name);
                ir_optimize_test_expect(test, before.status == 1 && before.status == after.status && before.effects == after.effects && before.value == after.value, message);
            }
            // The C driver's next steps: the copies' tests of extended comparisons, then the optimizer.
            ir_truth_simplify(&v.function);
            IrOptimizeResult o = { 0 };
            IrOptimizeOptions options = ir_optimize_options();
            options.passes |= BUSTER_IR_OPTIMIZE_IDENTITY;
            const IrCheck oc = ir_optimize(&v.function, optimized, scratch, options, &o);
            snprintf(message, sizeof(message), "threading %s: the optimizer takes the result", fixture->name);
            ir_optimize_test_expect(test, oc.error == IR_OK, message);
            if (oc.error != IR_OK)
            {
                fprintf(stderr, "  threading %s: optimizer error=%u instruction=%u detail=%u\n", fixture->name, oc.error, oc.instruction, oc.detail);
            }
            if (oc.error == IR_OK && fixture->flattened)
            {
                snprintf(message, sizeof(message), "threading %s: no phi and no extension are left: the comparisons are the branches", fixture->name);
                ir_optimize_test_expect(test, ir_thread_test_count(&o.function, IR_PHI) == 0 && ir_thread_test_count(&o.function, IR_ZEXT) == 0, message);
            }
            for (uint32_t trial = 0; oc.error == IR_OK && !fixture->structural && trial < 260; trial += 1)
            {
                const uint64_t parameter = trial < 256 ? trial : trial == 256 ? UINT64_MAX : trial == 257 ? UINT64_C(1) << 63 : ir_optimize_test_random(test);
                const IrOptimizeExecution before = ir_optimize_test_execute(&f, scratch, parameter, trial & 1);
                const IrOptimizeExecution final = ir_optimize_test_execute(&o.function, scratch, parameter, trial & 1);
                snprintf(message, sizeof(message), "threading %s: then the truth rewrite and the optimizer preserve them", fixture->name);
                ir_optimize_test_expect(test, before.status == final.status && before.effects == final.effects && before.value == final.value, message);
            }
        }
    }
    arena_pop_to(optimized, om);
    arena_pop_to(vector, vm);
    arena_pop_to(input, im);
}

// A candidate of more instructions than a 64-byte mask holds (#2608): 70 phis, the last of which, past the first 64,
// is read in the true successor, so that the scan for the repairable candidates and the cover bits' store reach the
// block's second 64. Each phi is `[p, b1], [p & 3, b2]`.
static void ir_thread_test_many_phis(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* optimized, Arena* scratch)
{
    static const char head[] = "function (i64) -> i64 {\n"
                               "b0:\n"
                               "  %0 = param.i64 0\n"
                               "  %1 = const.i64 0x00000000000000000000000000000003\n"
                               "  %2 = and.i64 %0, %1\n"
                               "  %3 = const.i32 0x00000000000000000000000000000000\n"
                               "  %4 = const.i32 0x00000000000000000000000000000001\n"
                               "  %5 = icmp.i1 %2, %1 ult\n"
                               "  %6 = cbr.void %5, b1, b2\n"
                               "b1:\n"
                               "  %7 = br.void b3\n"
                               "b2:\n"
                               "  %8 = icmp.i1 %0, %1 ugt\n"
                               "  %9 = zext.i32 %8\n"
                               "  %10 = br.void b3\n"
                               "b3:\n"
                               "  %11 = phi.i32 [%4, b1], [%9, b2]\n";
    char text[8192];
    int length = snprintf(text, sizeof(text), "%s", head);
    for (uint32_t k = 12; k < 81; k += 1)
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, "  %%%u = phi.i64 [%%0, b1], [%%2, b2]\n", k);
    }
    snprintf(text + length, sizeof(text) - (uint64_t)length, "  %%81 = icmp.i1 %%11, %%3 ne\n"
                                                             "  %%82 = cbr.void %%81, b4, b5\n"
                                                             "b4:\n"
                                                             "  %%83 = add.i64 %%80, %%2\n"
                                                             "  %%84 = ret.void %%83\n"
                                                             "b5:\n"
                                                             "  %%85 = ret.void %%1\n"
                                                             "}\n");
    const IrThreadTestFixture fixture = {
        .name = "many-phis",
        .text = text,
        .blocks = 1,
        .copies = 2,
        .redirects = 0,
        .rounds = 1,
        .merges = 1,
        .flattened = false,
    };
    ir_thread_test_fixture(test, &fixture, input, vector, optimized, scratch);
}

// A `?:` join whose number is read `chain` blocks of jumps past its true successor (#2608): the reader's walk back to
// the successor visits the reader and the chain, which BUSTER_IR_THREAD_WALK_LIMIT bounds. One block past the limit,
// the walk is refused and the join is left alone; at the limit, the successor merges the number. With `shortcut`, the
// successor also branches to the reader, whose walk meets it at once: a walk past the limit is refused all the same.
static void ir_thread_test_long_walk(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* optimized, Arena* scratch, uint32_t chain, bool shortcut)
{
    static const char head[] = "function (i64) -> i64 {\n"
                               "b0:\n"
                               "  %0 = param.i64 0\n"
                               "  %1 = const.i64 0x00000000000000000000000000000003\n"
                               "  %2 = and.i64 %0, %1\n"
                               "  %3 = const.i32 0x00000000000000000000000000000000\n"
                               "  %4 = const.i32 0x00000000000000000000000000000001\n"
                               "  %5 = icmp.i1 %2, %1 ult\n"
                               "  %6 = cbr.void %5, b1, b2\n"
                               "b1:\n"
                               "  %7 = br.void b3\n"
                               "b2:\n"
                               "  %8 = icmp.i1 %0, %1 ugt\n"
                               "  %9 = zext.i32 %8\n"
                               "  %10 = br.void b3\n"
                               "b3:\n"
                               "  %11 = phi.i32 [%4, b1], [%9, b2]\n"
                               "  %12 = phi.i64 [%0, b1], [%2, b2]\n"
                               "  %13 = icmp.i1 %11, %3 ne\n"
                               "  %14 = cbr.void %13, b4, b5\n"
                               "b4:\n";
    char text[8192];
    int length = snprintf(text, sizeof(text), "%s", head);
    if (shortcut)
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, "  %%15 = cbr.void %%5, b6, b%u\n", 6 + chain);
    }
    else
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, "  %%15 = br.void b6\n");
    }
    length += snprintf(text + length, sizeof(text) - (uint64_t)length, "b5:\n  %%16 = ret.void %%1\n");
    for (uint32_t k = 0; k < chain; k += 1)
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, "b%u:\n  %%%u = br.void b%u\n", 6 + k, 17 + k, 7 + k);
    }
    snprintf(text + length, sizeof(text) - (uint64_t)length, "b%u:\n  %%%u = add.i64 %%12, %%2\n  %%%u = ret.void %%%u\n}\n", 6 + chain, 17 + chain, 18 + chain, 17 + chain);
    const bool repaired = chain + 1 <= BUSTER_IR_THREAD_WALK_LIMIT;
    const IrThreadTestFixture fixture = {
        .name = shortcut ? "walk-past-limit-met" : repaired ? "walk-limit" : "walk-past-limit",
        .text = text,
        .blocks = repaired ? 1 : 0,
        .copies = repaired ? 2 : 0,
        .redirects = 0,
        .rounds = repaired ? 1 : 0,
        .merges = repaired ? 1 : 0,
        .flattened = false,
    };
    ir_thread_test_fixture(test, &fixture, input, vector, optimized, scratch);
}

// A loop's header that 17 jumps from a switch enter, its back edge's block past it the 18th of its predecessors (#2608):
// the cheap test of a loop's header reads them 16 at a time, and the last 16 hold the later one. Left alone.
static void ir_thread_test_header_predecessors(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* optimized, Arena* scratch)
{
    char text[8192];
    int length = snprintf(text, sizeof(text), "function (i64) -> i64 {\n"
                                              "b0:\n"
                                              "  %%0 = param.i64 0\n"
                                              "  %%1 = const.i64 0x0000000000000000000000000000000f\n"
                                              "  %%2 = and.i64 %%0, %%1\n"
                                              "  %%3 = const.i32 0x00000000000000000000000000000000\n"
                                              "  %%4 = const.i32 0x00000000000000000000000000000001\n"
                                              "  %%5 = const.i64 0x00000000000000000000000000000002\n"
                                              "  %%6 = lshr.i64 %%0, %%5\n"
                                              "  %%7 = switch.void %%2, default b17");
    for (uint32_t k = 0; k < 16; k += 1)
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, ", [0x%032x, b%u]", k, k + 1);
    }
    length += snprintf(text + length, sizeof(text) - (uint64_t)length, "\n");
    for (uint32_t k = 1; k <= 17; k += 1)
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, "b%u:\n  %%%u = br.void b18\n", k, 7 + k);
    }
    length += snprintf(text + length, sizeof(text) - (uint64_t)length, "b18:\n  %%25 = phi.i32");
    for (uint32_t k = 1; k <= 17; k += 1)
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, " [%%4, b%u],", k);
    }
    length += snprintf(text + length, sizeof(text) - (uint64_t)length, " [%%3, b20]\n  %%26 = phi.i64");
    for (uint32_t k = 1; k <= 17; k += 1)
    {
        length += snprintf(text + length, sizeof(text) - (uint64_t)length, " [%%2, b%u],", k);
    }
    snprintf(text + length, sizeof(text) - (uint64_t)length, " [%%6, b20]\n"
                                                             "  %%27 = icmp.i1 %%25, %%3 ne\n"
                                                             "  %%28 = cbr.void %%27, b19, b21\n"
                                                             "b19:\n"
                                                             "  %%29 = call.i64 @1(%%26, %%2) fixed=(i64, i64) variadic=0\n"
                                                             "  %%30 = br.void b20\n"
                                                             "b20:\n"
                                                             "  %%31 = br.void b18\n"
                                                             "b21:\n"
                                                             "  %%32 = ret.void %%6\n"
                                                             "}\n");
    const IrThreadTestFixture fixture = {
        .name = "header-predecessors",
        .text = text,
        .blocks = 0,
        .copies = 0,
        .redirects = 0,
        .rounds = 0,
        .merges = 0,
        .flattened = false,
    };
    ir_thread_test_fixture(test, &fixture, input, vector, optimized, scratch);
}

// #2924: the conditional branches' scan against its definition, every count of a batch at several offsets.
static void ir_thread_test_scans(IrOptimizeTest* test)
{
    IrOptimizeTestScan scan;
    bool agreed = true;
    for (uint32_t round = 0; round < 32; round += 1)
    {
        ir_optimize_test_scan_fill(test, &scan);
        for (uint32_t count = 0; count <= BUSTER_IR_THREAD_BATCH; count += 1)
        {
            const uint32_t start = round * 5 % (BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS - BUSTER_IR_THREAD_BATCH);
            uint64_t expected = 0;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                expected |= (uint64_t)(scan.op[start + lane] == IR_CBR) << lane;
            }
            agreed &= ir_thread_candidates(scan.op + start, count) == expected;
            uint64_t comparisons = 0;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                comparisons |= (uint64_t)(scan.op[start + lane] == IR_ICMP && scan.type[start + lane] == IR_I1) << lane;
            }
            agreed &= count == 0 || ir_truth_candidates(scan.op + start, scan.type + start, count) == comparisons;
        }
    }
    ir_optimize_test_expect(test, agreed, "threading's conditional branches' scan and truth simplification's comparisons', against their definitions");
    // The escapes' scan: rows without phis or calls (whose operands the shared walk of the extra array takes), with
    // ternary and binary intrinsics and scatters among them; their fixed operands valid values, their optional ones
    // absent or not, their other columns any word; blocks of 1 to 13 rows, some of them candidates.
    uint32_t a[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t b[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t c[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t extra[2 * BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t blocks[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t candidates[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t escapes[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t expected[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t used[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t offsets[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS + 1];
    uint32_t predecessor_offsets[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS + 1];
    uint32_t predecessors[2 * BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t reachable[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    bool escaped = true;
    bool covered = true;
    uint64_t found = 0;
    uint64_t narrowed = 0;
    uint64_t kept = 0;
    for (uint32_t round = 0; round < 64; round += 1)
    {
        ir_optimize_test_scan_fill(test, &scan);
        const uint32_t count = 1 + round * 37 % BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS;
        uint32_t block = 0;
        uint32_t left = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint64_t r = ir_optimize_test_random(test);
            const uint32_t op = scan.op[i];
            const uint32_t extra_kind = (uint32_t)(r >> 52) % 8;
            scan.op[i] = extra_kind == 0 ? IR_INTRINSIC : extra_kind == 1 ? IR_SCATTER : op == IR_PHI || op == IR_CALL || op == IR_CALL_INDIRECT || op == IR_INTRINSIC ? IR_ADD : (uint8_t)op;
            if (left == 0)
            {
                block += i != 0;
                left = 1 + (uint32_t)(r % 13);
                candidates[block] = (r >> 8) % 3 != 0;
            }
            left -= 1;
            blocks[i] = block;
            uint32_t* const columns[3] = { a, b, c };
            for (uint32_t k = 0; k < 3; k += 1)
            {
                const uint64_t pick = r >> (16 + k * 12);
                const bool fixed = (ir_operand_table[scan.op[i]] >> k & 1) != 0;
                const bool optional = (ir_optional_table[scan.op[i]] >> k & 1) != 0;
                columns[k][i] = fixed || (optional && (pick & 1) != 0) ? (uint32_t)(pick >> 1) % count : optional ? BUSTER_IR_NONE : (uint32_t)(pick >> 1) | UINT32_C(0x40000000);
            }
            // A ternary intrinsic's third operand, or an intrinsic of two, and a scatter's stored vector and mask, in the
            // extra array at c's index.
            if (scan.op[i] == IR_INTRINSIC || scan.op[i] == IR_SCATTER)
            {
                const uint32_t kind = scan.op[i] == IR_SCATTER ? 0 : (r >> 60) != 0 ? BUSTER_IR_INTRINSIC_TERNLOG : 0;
                c[i] = kind | 2 * i << BUSTER_IR_INTRINSIC_EXTRA_SHIFT;
                extra[2 * i] = (uint32_t)(r >> 20) % count;
                extra[2 * i + 1] = (uint32_t)(r >> 36) % count;
            }
        }
        const IrFunction f = {
            .op = scan.op,
            .type = scan.type,
            .flags = scan.flags,
            .a = a,
            .b = b,
            .c = c,
            .extra = extra,
            .instruction_count = count,
            .extra_count = 2 * count,
        };
        IrThreadWork work = {
            .function = &f,
            .candidates = candidates,
            .escapes = escapes,
        };
        work.cfg.instruction_blocks = blocks;
        memset(escapes, 0, sizeof(escapes));
        memset(expected, 0, sizeof(expected));
        ir_thread_escapes(&work, BUSTER_IR_THREAD_TRACK_CANDIDATES);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t mask = ir_value_mask(&f, i);
            const uint32_t operands[3] = { a[i], b[i], c[i] };
            for (uint32_t k = 0; k < 3; k += 1)
            {
                if ((mask >> k & 1) != 0 && candidates[blocks[operands[k]]] != 0 && blocks[operands[k]] != blocks[i])
                {
                    expected[operands[k]] = 1;
                }
            }
            for (uint32_t k = 0; k < ir_extra_value_count(&f, i); k += 1)
            {
                const uint32_t value = ir_extra_value(&f, i, k);
                if (candidates[blocks[value]] != 0 && blocks[value] != blocks[i])
                {
                    expected[value] = 1;
                }
            }
        }
        escaped &= memcmp(escapes, expected, sizeof(escapes)) == 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            found += expected[i];
        }
        // #2608's second pass over the same rows: the repairable blocks' values alone (every other candidate block),
        // each use outside its block narrowing the value's cover bits to the successors of its block that dominate the
        // use's (a walk back from the use's block, ir_thread_cover). A block's last row names two blocks as its
        // successors, which are the CFG's edges; every value starts with all its bits, so an untracked value the scan
        // narrowed would show. A walk takes the bits the value's earlier uses left, so the definition visits the uses
        // in the scan's order: by 16 rows, each column's lanes, then the rows' extra operands. Its choices are the
        // round's and the block's, not draws: the shared random stream stays the first pass's.
        const uint32_t block_count = block + 1;
        for (uint32_t i = count; i > 0; i -= 1)
        {
            offsets[blocks[i - 1]] = i - 1;
        }
        offsets[block_count] = count;
        memset(predecessor_offsets, 0, sizeof(predecessor_offsets));
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            for (uint32_t k = 0; k < block_count; k += 1)
            {
                const uint32_t last = offsets[k + 1] - 1;
                const bool encoded = scan.op[last] == IR_INTRINSIC || scan.op[last] == IR_SCATTER;
                candidates[k] |= pass == 0 && candidates[k] != 0 && !encoded && (k + round) % 2 == 0 ? BUSTER_IR_THREAD_REPAIRABLE : 0;
                b[last] = encoded ? b[last] : (k * 5 + round) % block_count;
                c[last] = encoded ? c[last] : (b[last] + 1 + (k + round) % 3) % block_count;
                reachable[k] = 1;
                for (uint32_t g = 0; !encoded && g < 2; g += 1)
                {
                    const uint32_t successor = g == 0 ? b[last] : c[last];
                    if (pass == 0)
                    {
                        predecessor_offsets[successor + 1] += 1;
                    }
                    else
                    {
                        predecessors[predecessor_offsets[successor]] = k;
                        predecessor_offsets[successor] += 1;
                    }
                }
            }
            for (uint32_t k = 0; pass == 0 && k < block_count; k += 1)
            {
                predecessor_offsets[k + 1] += predecessor_offsets[k];
            }
            for (uint32_t k = block_count; pass == 1 && k > 0; k -= 1)
            {
                predecessor_offsets[k] = predecessor_offsets[k - 1];
            }
            predecessor_offsets[0] = 0;
        }
        IrFunction g = f;
        g.block_offsets = offsets;
        g.block_count = block_count;
        work.function = &g;
        work.reachable = reachable;
        work.cfg.predecessor_offsets = predecessor_offsets;
        work.cfg.predecessors = predecessors;
        memset(escapes, BUSTER_IR_THREAD_ESCAPE | BUSTER_IR_THREAD_COVERS, sizeof(escapes));
        memset(expected, BUSTER_IR_THREAD_ESCAPE | BUSTER_IR_THREAD_COVERS, sizeof(expected));
        memset(used, 0, sizeof(used));
        ir_thread_escapes(&work, BUSTER_IR_THREAD_REPAIRABLE);
        for (uint32_t chunk = 0; chunk < count; chunk += 16)
        {
            const uint32_t lanes = count - chunk < 16 ? count - chunk : 16;
            for (uint32_t k = 0; k < 4; k += 1)
            {
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    const uint32_t i = chunk + lane;
                    const uint32_t mask = ir_value_mask(&g, i);
                    const uint32_t operands[3] = { a[i], b[i], c[i] };
                    for (uint32_t x = 0; x < (k < 3 ? 1 : ir_extra_value_count(&g, i)); x += 1)
                    {
                        const bool present = k < 3 ? (mask >> k & 1) != 0 : true;
                        const uint32_t value = k < 3 ? operands[k] : ir_extra_value(&g, i, x);
                        if (present && (candidates[blocks[value]] & BUSTER_IR_THREAD_REPAIRABLE) != 0 && blocks[value] != blocks[i])
                        {
                            const uint32_t possible = expected[value] & BUSTER_IR_THREAD_COVERS;
                            const uint32_t cover = possible != 0 ? ir_thread_cover(&work, blocks[value], blocks[i], possible) : 0;
                            expected[value] &= (uint8_t)(BUSTER_IR_THREAD_ESCAPE | cover);
                            used[value] = 1;
                        }
                    }
                }
            }
        }
        covered &= memcmp(escapes, expected, sizeof(escapes)) == 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            narrowed += used[i] != 0 && expected[i] != (BUSTER_IR_THREAD_ESCAPE | BUSTER_IR_THREAD_COVERS);
            kept += used[i] != 0 && (expected[i] & BUSTER_IR_THREAD_COVERS) != 0;
        }
    }
    ir_optimize_test_expect(test, escaped && found != 0, "threading's escapes' scan, against its definition");
    ir_optimize_test_expect(test, covered && narrowed != 0 && kept != 0, "threading's second escapes' scan (#2608's covers), against its definition");
}

// Sweep the scratch limit through each aligned allocation of a wide join, including its index and rewrite.
static void ir_thread_test_wide_rollback(IrOptimizeTest* test, const IrFunction* f, Arena* output, Arena* scratch)
{
    const uint64_t capacity = scratch->capacity;
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    bool restored = true;
    bool success = false;
    IrThreadResult sentinel;
    memset(&sentinel, 0x5a, sizeof(sentinel));
    for (uint64_t limit = sm; restored && !success && limit <= capacity; limit += 64)
    {
        scratch->capacity = limit;
        IrThreadResult result;
        memcpy(&result, &sentinel, sizeof(result));
        const IrCheck check = ir_thread(f, output, scratch, &result);
        success = check.error == IR_OK;
        restored = scratch->length == sm && (success || (check.error == IR_ERROR_MEMORY && output->length == om && memcmp(&result, &sentinel, sizeof(result)) == 0));
        arena_pop_to(output, om);
    }
    scratch->capacity = capacity;
    ir_optimize_test_expect(test, restored && success, "wide threading restores both arenas and the result on scratch exhaustion");
}

// A distinct selector reaches the join from each predecessor, alternating true and false edges. The other edge tries
// the next comparison. The second phi lists predecessors in reverse and uses a different bit of the selector.
static void ir_thread_test_wide(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch, uint32_t width, bool two_phis)
{
    const uint64_t im = input->length;
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    const uint32_t none = BUSTER_IR_NONE;
    const uint32_t join = width + 1;
    const uint32_t yes = join + 1;
    const uint32_t no = join + 2;
    const uint32_t first = 4 + 3 * width;
    const uint32_t branch = first + (two_phis ? 3 : 1);
    const IrSizes sizes = {
        .instruction_count = branch + 3 + (uint32_t)two_phis,
        .block_count = no + 1,
        .constant_count = width,
        .extra_count = two_phis ? 4 * width + 2 : 2 * width,
        .parameter_count = 1,
    };
    IrFunction f = { 0 };
    const bool created = ir_create(input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, created, "wide threading fixture allocated");
    if (created)
    {
        f.parameter_types[0] = IR_I32;
        f.result_type = IR_I1;
        ir_optimize_test_instruction(&f, 0, IR_PARAM, IR_I32, 0, none, none);
        ir_optimize_test_instruction(&f, 1, IR_CONST, IR_I1, 0, none, none);
        ir_optimize_test_instruction(&f, 2, IR_CONST, IR_I1, 1, none, none);
        ir_optimize_test_instruction(&f, 3, IR_BR, IR_VOID, 1, none, none);
        for (uint32_t k = 0; k < width; k += 1)
        {
            const uint32_t block = k + 1;
            const uint32_t row = 4 + 3 * k;
            f.constants[k].low = k;
            f.block_offsets[block] = row;
            ir_optimize_test_instruction(&f, row, IR_CONST, IR_I32, k, none, none);
            const uint32_t other = block == width ? no : block + 1;
            const bool reversed = (k & 1) != 0;
            ir_optimize_test_instruction(&f, row + 1, IR_ICMP, IR_I1, 0, row, reversed ? IR_NE : IR_EQ);
            ir_optimize_test_instruction(&f, row + 2, IR_CBR, IR_VOID, row + 1, reversed ? other : join, reversed ? join : other);
            f.extra[2 * k] = block;
            // A nonconstant input on the first edge keeps that edge and its phi entries in the two-phi case.
            f.extra[2 * k + 1] = two_phis && k == 0 ? row + 1 : 1 + (k & 1);
            if (two_phis)
            {
                const uint32_t at = 2 * (2 * width - 1 - k);
                f.extra[at] = block;
                f.extra[at + 1] = 1 + ((k >> 1) & 1);
            }
        }
        f.block_offsets[join] = first;
        f.block_offsets[yes] = branch + 1;
        f.block_offsets[no] = branch + 2 + (uint32_t)two_phis;
        f.block_offsets[no + 1] = f.instruction_count;
        ir_optimize_test_instruction(&f, first, IR_PHI, IR_I1, 0, width, none);
        if (two_phis)
        {
            ir_optimize_test_instruction(&f, first + 1, IR_PHI, IR_I1, 2 * width, width, none);
            ir_optimize_test_instruction(&f, first + 2, IR_XOR, IR_I1, first, first + 1, none);
        }
        ir_optimize_test_instruction(&f, branch, IR_CBR, IR_VOID, branch - 1, yes, no);
        if (two_phis)
        {
            // Redirects to yes also forward the second phi through a successor phi.
            f.extra[4 * width] = join;
            f.extra[4 * width + 1] = first + 1;
            ir_optimize_test_instruction(&f, branch + 1, IR_PHI, IR_I1, 4 * width, 1, none);
        }
        ir_optimize_test_instruction(&f, f.block_offsets[no] - 1, IR_RET, IR_VOID, two_phis ? branch + 1 : 2, none, none);
        ir_optimize_test_instruction(&f, f.block_offsets[no], IR_RET, IR_VOID, 1, none, none);

        const bool valid = ir_verify_complete(&f, scratch).error == IR_OK;
        ir_optimize_test_expect(test, valid, "wide threading input is complete SSA");
        if (valid)
        {
            IrThreadResult result = { 0 };
            const IrCheck check = ir_thread(&f, output, scratch, &result);
            bool exact = check.error == IR_OK && result.blocks == 1 && result.rounds == 1 && result.copies == 0 && result.redirects == width - (uint32_t)two_phis;
            exact = exact && scratch->length == sm && ir_verify_complete(&result.function, scratch).error == IR_OK;
            for (uint32_t k = 0; exact && k < width; k += 1)
            {
                const uint32_t row = result.function.block_offsets[k + 2] - 1;
                const uint32_t bit = (k & 1) ^ (two_phis ? (k >> 1) & 1 : 0);
                const uint32_t target = two_phis && k == 0 ? join : bit != 0 ? yes : no;
                const uint32_t other = k + 1 == width ? no : k + 2;
                const bool reversed = (k & 1) != 0;
                exact = target == other ? result.function.op[row] == IR_BR && result.function.a[row] == target :
                                          result.function.op[row] == IR_CBR && result.function.b[row] == (reversed ? other : target) && result.function.c[row] == (reversed ? target : other);
            }
            if (exact && two_phis)
            {
                const IrFunction* out = &result.function;
                const uint32_t phi = out->block_offsets[yes];
                for (uint32_t p = 0; exact && p < out->b[phi]; p += 1)
                {
                    const uint32_t* pair = out->extra + out->a[phi] + 2 * p;
                    exact = pair[1] == (pair[0] == join ? out->block_offsets[join] + 1 : 1 + (((pair[0] - 1) >> 1) & 1));
                }
            }
            ir_optimize_test_expect(test, exact, "wide threading chooses each edge and forwards its own phi input");
            if (exact && two_phis && width == 32)
            {
                ir_thread_test_wide_rollback(test, &f, output, scratch);
            }
        }
    }
    arena_pop_to(output, om);
    arena_pop_to(input, im);
}

static void ir_thread_tests(IrOptimizeTest* test)
{
    ir_thread_test_scans(test);
    Arena input = arena_reserve(4ull << 20);
    Arena vector = arena_reserve(4ull << 20);
    Arena optimized = arena_reserve(4ull << 20);
    Arena scratch = arena_reserve(16ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && optimized.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "threading fixture arenas");
    for (uint32_t k = 0; ready && k < sizeof(ir_thread_test_fixtures) / sizeof(ir_thread_test_fixtures[0]); k += 1)
    {
        ir_thread_test_fixture(test, &ir_thread_test_fixtures[k], &input, &vector, &optimized, &scratch);
    }
    // #2608's fixtures draw their random trials from a stream of their own, so that the programs generated after them
    // from the shared one (value numbering with memory's, whose digest was frozen where its scalar twin agreed) stay
    // the ones they were.
    const uint64_t shared = test->random;
    test->random = shared != UINT64_C(0x2608) ? shared ^ UINT64_C(0x2608) : UINT64_C(0x2608);
    for (uint32_t k = 0; ready && k < BUSTER_ARRAY_LENGTH(ir_thread_test_repairs); k += 1)
    {
        ir_thread_test_fixture(test, &ir_thread_test_repairs[k], &input, &vector, &optimized, &scratch);
    }
    if (ready)
    {
        ir_thread_test_many_phis(test, &input, &vector, &optimized, &scratch);
        ir_thread_test_long_walk(test, &input, &vector, &optimized, &scratch, BUSTER_IR_THREAD_WALK_LIMIT - 1, false);
        ir_thread_test_long_walk(test, &input, &vector, &optimized, &scratch, BUSTER_IR_THREAD_WALK_LIMIT, false);
        ir_thread_test_long_walk(test, &input, &vector, &optimized, &scratch, BUSTER_IR_THREAD_WALK_LIMIT, true);
        ir_thread_test_header_predecessors(test, &input, &vector, &optimized, &scratch);
    }
    test->random = shared;
    const uint32_t widths[] = { 2, 31, 32, 33, 64, 128, 256, 512 };
    for (uint32_t k = 0; ready && k < BUSTER_ARRAY_LENGTH(widths); k += 1)
    {
        ir_thread_test_wide(test, &input, &vector, &scratch, widths[k], false);
        ir_thread_test_wide(test, &input, &vector, &scratch, widths[k], true);
    }
    arena_release(&scratch);
    arena_release(&optimized);
    arena_release(&vector);
    arena_release(&input);
}

#endif
