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
    // After threading, ir_truth_simplify and the optimizer: whether no phi and no zero extension remain (the truth values
    // all became branches).
    bool flattened;
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
        // The truth value read past the test (its successor's own instruction): left alone.
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

static void ir_thread_test_fixture(IrOptimizeTest* test, const IrThreadTestFixture* fixture, Arena* input, Arena* vector, Arena* scalar, Arena* optimized, Arena* scratch)
{
    const uint64_t im = input->length;
    const uint64_t vm = vector->length;
    const uint64_t sm = scalar->length;
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
    const bool valid = parsed.error == IR_PARSE_OK && ir_verify_complete(&f, scratch, false).error == IR_OK;
    ir_optimize_test_expect(test, valid, message);
    if (valid)
    {
        IrThreadResult v = { 0 };
        IrThreadResult s = { 0 };
        const IrCheck vc = ir_thread(&f, vector, scratch, false, &v);
        const IrCheck sc = ir_thread(&f, scalar, scratch, true, &s);
        snprintf(message, sizeof(message), "threading %s: both paths accept it and restore scratch", fixture->name);
        ir_optimize_test_expect(test, vc.error == IR_OK && sc.error == IR_OK && scratch->length == tm, message);
        if (vc.error == IR_OK && sc.error == IR_OK)
        {
            snprintf(message, sizeof(message), "threading %s: %u blocks, %u copies, %u redirects in %u rounds", fixture->name, fixture->blocks, fixture->copies, fixture->redirects, fixture->rounds);
            ir_optimize_test_expect(test, v.blocks == fixture->blocks && v.copies == fixture->copies && v.redirects == fixture->redirects && v.rounds == fixture->rounds &&
                                              s.blocks == v.blocks && s.copies == v.copies && s.redirects == v.redirects && s.rounds == v.rounds,
                                    message);
            snprintf(message, sizeof(message), "threading %s: scalar/vector exact columns and side tables", fixture->name);
            ir_optimize_test_expect(test, ir_optimize_test_equal(&v.function, &s.function), message);
            snprintf(message, sizeof(message), "threading %s: the result is complete SSA", fixture->name);
            const IrCheck complete = ir_verify_complete(&v.function, scratch, true);
            ir_optimize_test_expect(test, complete.error == IR_OK && ir_verify_complete(&s.function, scratch, false).error == IR_OK, message);
            if (complete.error != IR_OK)
            {
                fprintf(stderr, "  threading %s: error=%u instruction=%u detail=%u\n", fixture->name, complete.error, complete.instruction, complete.detail);
            }
            if (fixture->blocks == 0)
            {
                snprintf(message, sizeof(message), "threading %s: a refusal borrows the unchanged input", fixture->name);
                ir_optimize_test_expect(test, v.function.op == f.op && v.function.extra == f.extra && vector->length == vm && scalar->length == sm, message);
            }
            // The C driver's next steps: the copies' tests of extended comparisons, then the optimizer.
            ir_truth_simplify(&v.function, false);
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
            for (uint32_t trial = 0; trial < 260; trial += 1)
            {
                const uint64_t parameter = trial < 256 ? trial : trial == 256 ? UINT64_MAX : trial == 257 ? UINT64_C(1) << 63 : ir_optimize_test_random(test);
                const IrOptimizeExecution before = ir_optimize_test_execute(&f, scratch, parameter, trial & 1);
                const IrOptimizeExecution after = ir_optimize_test_execute(&s.function, scratch, parameter, trial & 1);
                snprintf(message, sizeof(message), "threading %s: independently executed values and effects preserved", fixture->name);
                ir_optimize_test_expect(test, before.status == 1 && before.status == after.status && before.effects == after.effects && before.value == after.value, message);
                if (oc.error == IR_OK)
                {
                    const IrOptimizeExecution final = ir_optimize_test_execute(&o.function, scratch, parameter, trial & 1);
                    snprintf(message, sizeof(message), "threading %s: then the truth rewrite and the optimizer preserve them", fixture->name);
                    ir_optimize_test_expect(test, before.status == final.status && before.effects == final.effects && before.value == final.value, message);
                }
            }
        }
    }
    arena_pop_to(optimized, om);
    arena_pop_to(scalar, sm);
    arena_pop_to(vector, vm);
    arena_pop_to(input, im);
}

static void ir_thread_tests(IrOptimizeTest* test)
{
    Arena input = arena_reserve(4ull << 20);
    Arena vector = arena_reserve(4ull << 20);
    Arena scalar = arena_reserve(4ull << 20);
    Arena optimized = arena_reserve(4ull << 20);
    Arena scratch = arena_reserve(16ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scalar.base != 0 && optimized.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "threading fixture arenas");
    for (uint32_t k = 0; ready && k < sizeof(ir_thread_test_fixtures) / sizeof(ir_thread_test_fixtures[0]); k += 1)
    {
        ir_thread_test_fixture(test, &ir_thread_test_fixtures[k], &input, &vector, &scalar, &optimized, &scratch);
    }
    arena_release(&scratch);
    arena_release(&optimized);
    arena_release(&scalar);
    arena_release(&vector);
    arena_release(&input);
}

#endif
