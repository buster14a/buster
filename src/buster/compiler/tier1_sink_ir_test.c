// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2592's operand sink fixtures (tier1_schedule_ir.c's tier1_schedule_ir_sink), in the IR's text form, included after
// ir_optimize_test.c's independent executor. Each is parsed twice and sunk by both paths, which must agree exactly, the
// vector path's permutation checked by tier1_schedule_ir_verify (--verify's reference), the result verified as complete
// SSA, its expressions where the fixture says (each root right after its anchor, its expression's instructions before
// it), and the function executed against its input on every parameter below 256 and a few past it. The refusals keep
// their order: a list's `pp = &p->next` beside `p = p->next`, whose load wants p's register, and an increment whose last
// reader is a comparison its branch takes from right before it.

#include "tier1_schedule_ir.c"

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

typedef struct Tier1SinkIrTestFixture Tier1SinkIrTestFixture;
struct Tier1SinkIrTestFixture
{
    const char* name;
    const char* text;
    uint32_t trees;
    uint32_t moved;
    // The moved root and its anchor (its last reader), by their places in the text; BUSTER_IR_NONE where none moves.
    uint32_t root;
    uint32_t anchor;
};

#define BUSTER_TIER1_SINK_TEST_C(n) "0x000000000000000000000000000000" n

static const Tier1SinkIrTestFixture tier1_sink_ir_test_fixtures[] = {
    {
        // for (p = a; p < a + 16; ) s = s * 3 + *p++: p + 4 moves past the load of p.
        .name = "pointer",
        .text = "function (i64) -> i64 {\n"
                "  slot $0 size=64 align=8 type=void flags=0x01\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = stack_addr.ptr $0\n"
                "  %2 = store.void %1, %0 align=1\n"
                "  %3 = address.ptr %1 offset=8\n"
                "  %4 = const.i64 " BUSTER_TIER1_SINK_TEST_C("0f") "\n"
                "  %5 = mul.i64 %0, %4\n"
                "  %6 = store.void %3, %5 align=1\n"
                "  %7 = address.ptr %1 offset=16\n"
                "  %8 = ptrtoint.i64 %7\n"
                "  %9 = const.i64 " BUSTER_TIER1_SINK_TEST_C("00") "\n"
                "  %10 = br.void b1\n"
                "b1:\n"
                "  %11 = phi.ptr [%1, b0], [%16, b2]\n"
                "  %12 = phi.i64 [%9, b0], [%21, b2]\n"
                "  %13 = ptrtoint.i64 %11\n"
                "  %14 = icmp.i1 %13, %8 ult\n"
                "  %15 = cbr.void %14, b2, b3\n"
                "b2:\n"
                "  %16 = address.ptr %11 offset=4\n"
                "  %17 = load.i32 %11 align=1\n"
                "  %18 = zext.i64 %17\n"
                "  %19 = const.i64 " BUSTER_TIER1_SINK_TEST_C("03") "\n"
                "  %20 = mul.i64 %12, %19\n"
                "  %21 = add.i64 %20, %18\n"
                "  %22 = br.void b1\n"
                "b3:\n"
                "  %23 = ret.void %12\n"
                "}\n",
        .trees = 1,
        .moved = 1,
        .root = 16,
        .anchor = 17,
    },
    {
        // Lua's dispatch tail, `i = *(pc++)` with pc an integer: the increment's constant, conversions and address move
        // as one expression past the load, which reads pc through a conversion of its own.
        .name = "dispatch",
        .text = "function (i64) -> i64 {\n"
                "  slot $0 size=64 align=8 type=void flags=0x01\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = stack_addr.ptr $0\n"
                "  %2 = store.void %1, %0 align=1\n"
                "  %3 = address.ptr %1 offset=8\n"
                "  %4 = not.i64 %0\n"
                "  %5 = store.void %3, %4 align=1\n"
                "  %6 = ptrtoint.i64 %1\n"
                "  %7 = const.i64 " BUSTER_TIER1_SINK_TEST_C("10") "\n"
                "  %8 = add.i64 %6, %7\n"
                "  %9 = const.i64 " BUSTER_TIER1_SINK_TEST_C("00") "\n"
                "  %10 = br.void b1\n"
                "b1:\n"
                "  %11 = phi.i64 [%6, b0], [%19, b2]\n"
                "  %12 = phi.i64 [%9, b0], [%25, b2]\n"
                "  %13 = icmp.i1 %11, %8 ult\n"
                "  %14 = cbr.void %13, b2, b3\n"
                "b2:\n"
                "  %15 = const.i64 " BUSTER_TIER1_SINK_TEST_C("01") "\n"
                "  %16 = inttoptr.ptr %11\n"
                "  %17 = address.ptr %16 + %15 * 4 offset=0\n"
                "  %18 = ptrtoint.i64 %17\n"
                "  %19 = copy.i64 %18\n"
                "  %20 = inttoptr.ptr %11\n"
                "  %21 = load.i32 %20 align=1\n"
                "  %22 = const.i32 " BUSTER_TIER1_SINK_TEST_C("7f") "\n"
                "  %23 = and.i32 %21, %22\n"
                "  %24 = zext.i64 %23\n"
                "  %25 = add.i64 %12, %24\n"
                "  %26 = br.void b1\n"
                "b3:\n"
                "  %27 = ret.void %12\n"
                "}\n",
        .trees = 1,
        .moved = 5,
        .root = 19,
        .anchor = 21,
    },
    {
        // A handler's join of pc's paths (a conditional jump skips a word) is of pc's family: its increment moves.
        .name = "join",
        .text = "function (i64) -> i64 {\n"
                "  slot $0 size=64 align=8 type=void flags=0x01\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = stack_addr.ptr $0\n"
                "  %2 = store.void %1, %0 align=1\n"
                "  %3 = address.ptr %1 offset=8\n"
                "  %4 = store.void %3, %0 align=1\n"
                "  %5 = address.ptr %1 offset=16\n"
                "  %6 = store.void %5, %0 align=1\n"
                "  %7 = address.ptr %1 offset=24\n"
                "  %8 = ptrtoint.i64 %7\n"
                "  %9 = const.i64 " BUSTER_TIER1_SINK_TEST_C("00") "\n"
                "  %10 = const.i64 " BUSTER_TIER1_SINK_TEST_C("01") "\n"
                "  %11 = br.void b1\n"
                "b1:\n"
                "  %12 = phi.ptr [%1, b0], [%26, b4]\n"
                "  %13 = phi.i64 [%9, b0], [%30, b4]\n"
                "  %14 = phi.i64 [%0, b0], [%31, b4]\n"
                "  %15 = ptrtoint.i64 %12\n"
                "  %16 = icmp.i1 %15, %8 ult\n"
                "  %17 = cbr.void %16, b2, b5\n"
                "b2:\n"
                "  %18 = and.i64 %14, %10\n"
                "  %19 = icmp.i1 %18, %9 ne\n"
                "  %20 = cbr.void %19, b3, b4\n"
                "b3:\n"
                "  %21 = address.ptr %12 offset=4\n"
                "  %22 = br.void b4\n"
                "b4:\n"
                "  %23 = phi.ptr [%12, b2], [%21, b3]\n"
                "  %24 = const.i64 " BUSTER_TIER1_SINK_TEST_C("01") "\n"
                "  %25 = address.ptr %23 + %24 * 4 offset=0\n"
                "  %26 = copy.ptr %25\n"
                "  %27 = load.i32 %23 align=1\n"
                "  %28 = zext.i64 %27\n"
                "  %29 = xor.i64 %13, %28\n"
                "  %30 = add.i64 %29, %10\n"
                "  %31 = lshr.i64 %14, %10\n"
                "  %32 = br.void b1\n"
                "b5:\n"
                "  %33 = ret.void %13\n"
                "}\n",
        .trees = 1,
        .moved = 3,
        .root = 26,
        .anchor = 27,
    },
    {
        // A list walked through `pp = &p->next; p = p->next`: pp's next value reads p, but p's own next value, the load,
        // wants p's register, so nothing moves.
        .name = "list",
        .text = "function (i64) -> i64 {\n"
                "  slot $0 size=64 align=8 type=void flags=0x01\n"
                "  slot $1 size=8 align=8 type=void flags=0x01\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = stack_addr.ptr $0\n"
                "  %2 = stack_addr.ptr $1\n"
                "  %3 = store.void %1, %0 align=1\n"
                "  %4 = address.ptr %1 offset=16\n"
                "  %5 = address.ptr %1 offset=8\n"
                "  %6 = store.void %5, %4 align=1\n"
                "  %7 = address.ptr %1 offset=32\n"
                "  %8 = address.ptr %1 offset=24\n"
                "  %9 = store.void %8, %7 align=1\n"
                "  %10 = const.i64 " BUSTER_TIER1_SINK_TEST_C("00") "\n"
                "  %11 = address.ptr %1 offset=40\n"
                "  %12 = store.void %11, %10 align=1\n"
                "  %13 = br.void b1\n"
                "b1:\n"
                "  %14 = phi.ptr [%1, b0], [%22, b2]\n"
                "  %15 = phi.ptr [%2, b0], [%20, b2]\n"
                "  %16 = ptrtoint.i64 %14\n"
                "  %17 = icmp.i1 %16, %10 ne\n"
                "  %18 = cbr.void %17, b2, b3\n"
                "b2:\n"
                "  %19 = store.void %15, %14 align=1\n"
                "  %20 = address.ptr %14 offset=8\n"
                "  %21 = address.ptr %14 offset=8\n"
                "  %22 = load.ptr %21 align=1\n"
                "  %23 = br.void b1\n"
                "b3:\n"
                "  %24 = load.i64 %2 align=1\n"
                "  %25 = ptrtoint.i64 %15\n"
                "  %26 = add.i64 %24, %25\n"
                "  %27 = ret.void %26\n"
                "}\n",
        .trees = 0,
        .moved = 0,
        .root = BUSTER_IR_NONE,
        .anchor = BUSTER_IR_NONE,
    },
    {
        // A bottom-tested walk whose increment's last reader is the comparison its branch takes from right before it.
        .name = "truth",
        .text = "function (i64) -> i64 {\n"
                "  slot $0 size=64 align=8 type=void flags=0x01\n"
                "b0:\n"
                "  %0 = param.i64 0\n"
                "  %1 = stack_addr.ptr $0\n"
                "  %2 = store.void %1, %0 align=1\n"
                "  %3 = address.ptr %1 offset=8\n"
                "  %4 = store.void %3, %0 align=1\n"
                "  %5 = address.ptr %1 offset=8\n"
                "  %6 = ptrtoint.i64 %5\n"
                "  %7 = const.i64 " BUSTER_TIER1_SINK_TEST_C("00") "\n"
                "  %8 = br.void b1\n"
                "b1:\n"
                "  %9 = phi.ptr [%1, b0], [%14, b1]\n"
                "  %10 = phi.i64 [%7, b0], [%13, b1]\n"
                "  %11 = load.i32 %9 align=1\n"
                "  %12 = zext.i64 %11\n"
                "  %13 = add.i64 %10, %12\n"
                "  %14 = address.ptr %9 offset=4\n"
                "  %15 = ptrtoint.i64 %9\n"
                "  %16 = icmp.i1 %15, %6 ult\n"
                "  %17 = cbr.void %16, b1, b2\n"
                "b2:\n"
                "  %18 = ret.void %13\n"
                "}\n",
        .trees = 0,
        .moved = 0,
        .root = BUSTER_IR_NONE,
        .anchor = BUSTER_IR_NONE,
    },
};

#undef BUSTER_TIER1_SINK_TEST_C

static void tier1_sink_ir_test_fixture(IrOptimizeTest* test, const Tier1SinkIrTestFixture* fixture, Arena* input, Arena* copies, Arena* scratch)
{
    const uint64_t im = input->length;
    const uint64_t cm = copies->length;
    const uint64_t tm = scratch->length;
    char message[192] = { 0 };
    IrFunction vector = { 0 };
    IrFunction scalar = { 0 };
    const String8 text = {
        .pointer = (char*)fixture->text,
        .length = strlen(fixture->text),
    };
    const bool parsed = ir_parse(text, input, scratch, &vector).error == IR_PARSE_OK && ir_parse(text, input, scratch, &scalar).error == IR_PARSE_OK;
    snprintf(message, sizeof(message), "operand sink %s: the fixture parses as complete SSA", fixture->name);
    const bool valid = parsed && ir_verify_complete(&vector, scratch, false).error == IR_OK;
    ir_optimize_test_expect(test, valid, message);
    Tier1ScheduleIrOriginal original = { 0 };
    const uint32_t n = vector.instruction_count;
    uint32_t* vector_order = valid ? BUSTER_ARENA_PUSH_ARRAY(copies, uint32_t, (uint64_t)n + 1) : 0;
    uint32_t* scalar_order = valid ? BUSTER_ARENA_PUSH_ARRAY(copies, uint32_t, (uint64_t)n + 1) : 0;
    uint32_t* place = valid ? BUSTER_ARENA_PUSH_ARRAY(copies, uint32_t, (uint64_t)n + 1) : 0;
    const bool copied = valid && vector_order != 0 && scalar_order != 0 && place != 0 && tier1_schedule_ir_copy(&vector, copies, &original);
    snprintf(message, sizeof(message), "operand sink %s: the original's copy", fixture->name);
    ir_optimize_test_expect(test, !valid || copied, message);
    if (copied)
    {
        const uint64_t before = scratch->length;
        Tier1SinkIrStats v = { 0 };
        Tier1SinkIrStats s = { 0 };
        const IrError ve = tier1_schedule_ir_sink(&vector, scratch, vector_order, false, &v);
        const IrError se = tier1_schedule_ir_sink(&scalar, scratch, scalar_order, true, &s);
        snprintf(message, sizeof(message), "operand sink %s: both paths sink and restore scratch", fixture->name);
        ir_optimize_test_expect(test, ve == IR_OK && se == IR_OK && scratch->length == before, message);
        snprintf(message, sizeof(message), "operand sink %s: %u expressions, %u instructions moved, by both paths", fixture->name, fixture->trees, fixture->moved);
        ir_optimize_test_expect(test, v.trees == fixture->trees && v.moved == fixture->moved && s.trees == v.trees && s.moved == v.moved, message);
        if (v.trees != fixture->trees || v.moved != fixture->moved)
        {
            fprintf(stderr, "  operand sink %s: %u expressions, %u moved (scalar %u, %u)\n", fixture->name, v.trees, v.moved, s.trees, s.moved);
        }
        snprintf(message, sizeof(message), "operand sink %s: scalar/vector exact columns and side tables", fixture->name);
        ir_optimize_test_expect(test, ir_optimize_test_equal(&vector, &scalar), message);
        snprintf(message, sizeof(message), "operand sink %s: the order passes the preallocation schedule's reference", fixture->name);
        ir_optimize_test_expect(test, v.trees == 0 || (tier1_schedule_ir_verify(&original, &vector, vector_order, place) && memcmp(vector_order, scalar_order, (uint64_t)n * sizeof(uint32_t)) == 0), message);
        snprintf(message, sizeof(message), "operand sink %s: a refusal leaves every instruction in place", fixture->name);
        ir_optimize_test_expect(test, v.trees != 0 || ir_optimize_test_equal(&vector, &original.function), message);
        if (fixture->root != BUSTER_IR_NONE && v.trees != 0)
        {
            // place[old] = new, from the vector path's order.
            for (uint32_t i = 0; i < n; i += 1)
            {
                place[vector_order[i]] = i;
            }
            snprintf(message, sizeof(message), "operand sink %s: the root right after its anchor and its expression's other instructions", fixture->name);
            ir_optimize_test_expect(test, place[fixture->root] == place[fixture->anchor] + fixture->moved && place[fixture->anchor] < fixture->anchor, message);
        }
        snprintf(message, sizeof(message), "operand sink %s: the result is complete SSA", fixture->name);
        ir_optimize_test_expect(test, ir_verify_complete(&vector, scratch, true).error == IR_OK, message);
        for (uint32_t trial = 0; trial < 260; trial += 1)
        {
            const uint64_t parameter = trial < 256 ? trial : trial == 256 ? UINT64_MAX : trial == 257 ? UINT64_C(1) << 63 : ir_optimize_test_random(test);
            const IrOptimizeExecution old = ir_optimize_test_execute(&original.function, scratch, parameter, trial & 1);
            const IrOptimizeExecution sunk = ir_optimize_test_execute(&vector, scratch, parameter, trial & 1);
            snprintf(message, sizeof(message), "operand sink %s: independently executed values and effects preserved", fixture->name);
            ir_optimize_test_expect(test, old.status == 1 && old.status == sunk.status && old.effects == sunk.effects && old.value == sunk.value, message);
        }
    }
    arena_pop_to(scratch, tm);
    arena_pop_to(copies, cm);
    arena_pop_to(input, im);
}

static void tier1_sink_ir_tests(IrOptimizeTest* test)
{
    Arena input = arena_reserve(4ull << 20);
    Arena copies = arena_reserve(4ull << 20);
    Arena scratch = arena_reserve(16ull << 20);
    const bool ready = input.base != 0 && copies.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "operand sink fixture arenas");
    for (uint32_t k = 0; ready && k < sizeof(tier1_sink_ir_test_fixtures) / sizeof(tier1_sink_ir_test_fixtures[0]); k += 1)
    {
        tier1_sink_ir_test_fixture(test, &tier1_sink_ir_test_fixtures[k], &input, &copies, &scratch);
    }
    arena_release(&scratch);
    arena_release(&copies);
    arena_release(&input);
}

#endif
