// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier1-masks (#179): hand-written IR (ir_parse.c's text, #175) of AVX-512's masks through Tier 1's selection and
// CFG consumer, whose allocation gives them the opmask class (k1 to k7, k0 its scratch), run natively and checked
// against the shared IR's interpreter (#176). Include after tier1_pipeline_test.c, whose child-process wait it shares.
//
// The IR's masks are m8 to m64 (#732), with no argument or result of a mask and no conversion between a mask and an
// integer, so each fixture is a function of a pointer to a buffer, and of a count where it loops, that loads masks of
// every width from the buffer's first half, computes, and stores masks into its second half. Each is parsed by both of
// ir_parse.c's paths into the same function, planned and emitted by both of Tier 1's paths for x86-64-v4 (AVX512F, BW,
// DQ and VL), its allocation checked by tier1_cfg_verify, its records by the target's allow-list, and encoded by both
// encoders into the same bytes. Where the host has AVX512F, BW and DQ, it runs in a child process on random buffers,
// and the buffer it leaves must be the interpreter's, byte for byte (the bytes no store writes among them).
//
// The fixtures: and, or, xor, not and copies at the four widths; constants (zero, every bit and others); a loop whose
// mask phis swap, a cycle through k0, beside a mask accumulated from the buffer; more masks live at once than k1 to k7
// hold, so that some are spilled and reloaded at their widths; and masks live across calls, outside a loop and in one,
// of a helper that clobbers every opmask register before it returns its argument plus one. The helper is generated
// code too, beside the function: the interpreter calls a C function that returns the same. And the refusals: a vector
// (#141's), a mask's select, a target without AVX-512 (no opmask form), and one with AVX512F alone (kmovw and kandw,
// but no kandb or kmovq); and the selector's vector scan by both paths, which must take masks and refuse a vector at
// any place, its columns ending at a PROT_NONE page. --print prints each fixture's records by the vector path, as GNU
// as reads them.

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_TIER1_MASKS_TEST_BUFFER 256
#define BUSTER_TIER1_MASKS_TEST_INPUTS 16
#define BUSTER_TIER1_MASKS_TEST_HELPER 9u
#define BUSTER_TIER1_MASKS_TEST_ENTRY 1u
#define BUSTER_TIER1_MASKS_TEST_BINDING 100u

typedef struct Tier1MasksTest Tier1MasksTest;
struct Tier1MasksTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
    uint8_t* v4;
    bool native;
    // --print: each fixture's records, by the vector path, as GNU as reads them.
    bool print;
    Arena output;
    Arena scratch;
    Arena execution;
    // What the fixtures did, over both paths: native runs, mask spills and reloads, masks saved around calls, edge and
    // split moves of masks, moves through k0, records.
    uint64_t native_runs;
    uint64_t mask_stores;
    uint64_t mask_loads;
    uint64_t call_saves;
    uint64_t mask_moves;
    uint64_t scratch_moves;
    uint64_t records;
    uint32_t refused;
};

typedef struct Tier1MasksFixture Tier1MasksFixture;
struct Tier1MasksFixture
{
    const char* name;
    const char* text;
    // The count it loops to, at most this (0 for a function of the pointer alone), and what it must reach.
    uint32_t count;
    bool spills;
    bool calls;
    bool cycle;
};

static bool tier1_masks_test_expect(Tier1MasksTest* test, bool condition, const char* message)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 20)
        {
            printf("test-tier1-masks: %s\n", message);
        }
    }
    return condition;
}

static uint64_t tier1_masks_test_next(Tier1MasksTest* test)
{
    test->random = test->random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return test->random >> 17;
}

// The interpreter's helper: what the generated one returns, without its clobbers.
static uint64_t tier1_masks_test_successor(uint64_t value)
{
    return value + 1;
}

static const Tier1MasksFixture tier1_masks_test_fixtures[] = {
    {
        .name = "logic at every width",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.m8 %0 align=1\n"
                "  %2 = address.ptr %0 offset=1\n"
                "  %3 = load.m8 %2 align=1\n"
                "  %4 = and.m8 %1, %3\n"
                "  %5 = or.m8 %1, %3\n"
                "  %6 = xor.m8 %4, %5\n"
                "  %7 = not.m8 %6\n"
                "  %8 = copy.m8 %7\n"
                "  %9 = address.ptr %0 offset=128\n"
                "  %10 = store.void %9, %8 align=1\n"
                "  %11 = address.ptr %0 offset=129\n"
                "  %12 = store.void %11, %4 align=1\n"
                "  %13 = address.ptr %0 offset=2\n"
                "  %14 = load.m16 %13 align=2\n"
                "  %15 = address.ptr %0 offset=4\n"
                "  %16 = load.m16 %15 align=2\n"
                "  %17 = and.m16 %14, %16\n"
                "  %18 = or.m16 %14, %16\n"
                "  %19 = xor.m16 %17, %18\n"
                "  %20 = not.m16 %19\n"
                "  %21 = address.ptr %0 offset=130\n"
                "  %22 = store.void %21, %20 align=2\n"
                "  %23 = address.ptr %0 offset=132\n"
                "  %24 = store.void %23, %18 align=2\n"
                "  %25 = address.ptr %0 offset=8\n"
                "  %26 = load.m32 %25 align=4\n"
                "  %27 = address.ptr %0 offset=12\n"
                "  %28 = load.m32 %27 align=4\n"
                "  %29 = and.m32 %26, %28\n"
                "  %30 = or.m32 %26, %28\n"
                "  %31 = xor.m32 %29, %30\n"
                "  %32 = not.m32 %31\n"
                "  %33 = address.ptr %0 offset=136\n"
                "  %34 = store.void %33, %32 align=4\n"
                "  %35 = address.ptr %0 offset=140\n"
                "  %36 = store.void %35, %30 align=4\n"
                "  %37 = address.ptr %0 offset=16\n"
                "  %38 = load.m64 %37 align=8\n"
                "  %39 = address.ptr %0 offset=24\n"
                "  %40 = load.m64 %39 align=8\n"
                "  %41 = and.m64 %38, %40\n"
                "  %42 = or.m64 %38, %40\n"
                "  %43 = xor.m64 %41, %42\n"
                "  %44 = not.m64 %43\n"
                "  %45 = copy.m64 %44\n"
                "  %46 = address.ptr %0 offset=144\n"
                "  %47 = store.void %46, %45 align=8\n"
                "  %48 = address.ptr %0 offset=152\n"
                "  %49 = store.void %48, %42 align=8\n"
                "  %50 = ret.void\n"
                "}\n",
    },
    {
        .name = "constants",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = const.m8 0x00000000000000000000000000000000\n"
                "  %2 = address.ptr %0 offset=128\n"
                "  %3 = store.void %2, %1 align=1\n"
                "  %4 = const.m8 0x000000000000000000000000000000ff\n"
                "  %5 = address.ptr %0 offset=129\n"
                "  %6 = store.void %5, %4 align=1\n"
                "  %7 = const.m8 0x0000000000000000000000000000005a\n"
                "  %8 = address.ptr %0 offset=130\n"
                "  %9 = store.void %8, %7 align=1\n"
                "  %10 = const.m16 0x0000000000000000000000000000ffff\n"
                "  %11 = address.ptr %0 offset=132\n"
                "  %12 = store.void %11, %10 align=2\n"
                "  %13 = const.m16 0x0000000000000000000000000000a5c3\n"
                "  %14 = address.ptr %0 offset=134\n"
                "  %15 = store.void %14, %13 align=2\n"
                "  %16 = const.m32 0x000000000000000000000000ffffffff\n"
                "  %17 = address.ptr %0 offset=136\n"
                "  %18 = store.void %17, %16 align=4\n"
                "  %19 = const.m32 0x00000000000000000000000080000001\n"
                "  %20 = address.ptr %0 offset=140\n"
                "  %21 = store.void %20, %19 align=4\n"
                "  %22 = const.m64 0x0000000000000000ffffffffffffffff\n"
                "  %23 = address.ptr %0 offset=144\n"
                "  %24 = store.void %23, %22 align=8\n"
                "  %25 = const.m64 0x00000000000000008000000000000001\n"
                "  %26 = address.ptr %0 offset=152\n"
                "  %27 = store.void %26, %25 align=8\n"
                "  %28 = const.m64 0x00000000000000000000000012345678\n"
                "  %29 = address.ptr %0 offset=160\n"
                "  %30 = store.void %29, %28 align=8\n"
                "  %31 = const.m64 0x00000000000000000000000000000000\n"
                "  %32 = address.ptr %0 offset=168\n"
                "  %33 = store.void %32, %31 align=8\n"
                "  %34 = const.m16 0x00000000000000000000000000000000\n"
                "  %35 = address.ptr %0 offset=176\n"
                "  %36 = store.void %35, %34 align=2\n"
                "  %37 = ret.void\n"
                "}\n",
    },
    {
        .name = "a loop whose mask phis swap",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.m16 %0 align=2\n"
                "  %3 = address.ptr %0 offset=2\n"
                "  %4 = load.m16 %3 align=2\n"
                "  %5 = address.ptr %0 offset=8\n"
                "  %6 = load.m32 %5 align=4\n"
                "  %7 = const.i64 0x00000000000000000000000000000000\n"
                "  %8 = br.void b1\n"
                "b1:\n"
                "  %9 = phi.m16 [%2, b0], [%10, b1]\n"
                "  %10 = phi.m16 [%4, b0], [%9, b1]\n"
                "  %11 = phi.m32 [%6, b0], [%16, b1]\n"
                "  %12 = phi.i64 [%7, b0], [%18, b1]\n"
                "  %13 = address.ptr %0 + %12 * 4 offset=16\n"
                "  %14 = load.m32 %13 align=4\n"
                "  %15 = not.m32 %14\n"
                "  %16 = xor.m32 %11, %15\n"
                "  %17 = const.i64 0x00000000000000000000000000000001\n"
                "  %18 = add.i64 %12, %17\n"
                "  %19 = icmp.i1 %18, %1 ult\n"
                "  %20 = cbr.void %19, b1, b2\n"
                "b2:\n"
                "  %21 = address.ptr %0 offset=128\n"
                "  %22 = store.void %21, %9 align=2\n"
                "  %23 = address.ptr %0 offset=130\n"
                "  %24 = store.void %23, %10 align=2\n"
                "  %25 = address.ptr %0 offset=132\n"
                "  %26 = store.void %25, %11 align=4\n"
                "  %27 = address.ptr %0 offset=136\n"
                "  %28 = store.void %27, %16 align=4\n"
                "  %29 = ret.void\n"
                "}\n",
        .count = 20,
        .cycle = true,
    },
    {
        .name = "more masks than k1 to k7",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.m8 %0 align=1\n"
                "  %2 = address.ptr %0 offset=1\n"
                "  %3 = load.m8 %2 align=1\n"
                "  %4 = address.ptr %0 offset=2\n"
                "  %5 = load.m16 %4 align=2\n"
                "  %6 = address.ptr %0 offset=4\n"
                "  %7 = load.m16 %6 align=2\n"
                "  %8 = address.ptr %0 offset=8\n"
                "  %9 = load.m32 %8 align=4\n"
                "  %10 = address.ptr %0 offset=12\n"
                "  %11 = load.m32 %10 align=4\n"
                "  %12 = address.ptr %0 offset=16\n"
                "  %13 = load.m64 %12 align=8\n"
                "  %14 = address.ptr %0 offset=24\n"
                "  %15 = load.m64 %14 align=8\n"
                "  %16 = address.ptr %0 offset=32\n"
                "  %17 = load.m64 %16 align=8\n"
                "  %18 = address.ptr %0 offset=40\n"
                "  %19 = load.m16 %18 align=2\n"
                "  %20 = xor.m8 %1, %3\n"
                "  %21 = and.m16 %5, %7\n"
                "  %22 = or.m32 %9, %11\n"
                "  %23 = xor.m64 %13, %15\n"
                "  %24 = and.m64 %23, %17\n"
                "  %25 = not.m16 %19\n"
                "  %26 = address.ptr %0 offset=128\n"
                "  %27 = store.void %26, %1 align=1\n"
                "  %28 = address.ptr %0 offset=129\n"
                "  %29 = store.void %28, %3 align=1\n"
                "  %30 = address.ptr %0 offset=130\n"
                "  %31 = store.void %30, %5 align=2\n"
                "  %32 = address.ptr %0 offset=132\n"
                "  %33 = store.void %32, %7 align=2\n"
                "  %34 = address.ptr %0 offset=136\n"
                "  %35 = store.void %34, %9 align=4\n"
                "  %36 = address.ptr %0 offset=140\n"
                "  %37 = store.void %36, %11 align=4\n"
                "  %38 = address.ptr %0 offset=144\n"
                "  %39 = store.void %38, %13 align=8\n"
                "  %40 = address.ptr %0 offset=152\n"
                "  %41 = store.void %40, %15 align=8\n"
                "  %42 = address.ptr %0 offset=160\n"
                "  %43 = store.void %42, %17 align=8\n"
                "  %44 = address.ptr %0 offset=168\n"
                "  %45 = store.void %44, %19 align=2\n"
                "  %46 = address.ptr %0 offset=170\n"
                "  %47 = store.void %46, %20 align=1\n"
                "  %48 = address.ptr %0 offset=172\n"
                "  %49 = store.void %48, %21 align=2\n"
                "  %50 = address.ptr %0 offset=176\n"
                "  %51 = store.void %50, %22 align=4\n"
                "  %52 = address.ptr %0 offset=184\n"
                "  %53 = store.void %52, %24 align=8\n"
                "  %54 = address.ptr %0 offset=192\n"
                "  %55 = store.void %54, %25 align=2\n"
                "  %56 = ret.void\n"
                "}\n",
        .spills = true,
    },
    {
        .name = "masks across calls",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.m16 %0 align=2\n"
                "  %3 = address.ptr %0 offset=8\n"
                "  %4 = load.m64 %3 align=8\n"
                "  %5 = address.ptr %0 offset=16\n"
                "  %6 = load.m8 %5 align=1\n"
                "  %7 = call.i64 @9(%1) fixed=(i64) variadic=0\n"
                "  %8 = not.m16 %2\n"
                "  %9 = call.i64 @9(%7) fixed=(i64) variadic=0\n"
                "  %10 = not.m64 %4\n"
                "  %11 = address.ptr %0 offset=128\n"
                "  %12 = store.void %11, %2 align=2\n"
                "  %13 = address.ptr %0 offset=130\n"
                "  %14 = store.void %13, %8 align=2\n"
                "  %15 = address.ptr %0 offset=136\n"
                "  %16 = store.void %15, %4 align=8\n"
                "  %17 = address.ptr %0 offset=144\n"
                "  %18 = store.void %17, %10 align=8\n"
                "  %19 = address.ptr %0 offset=152\n"
                "  %20 = store.void %19, %6 align=1\n"
                "  %21 = address.ptr %0 offset=160\n"
                "  %22 = store.void %21, %9 align=8\n"
                "  %23 = ret.void\n"
                "}\n",
        .count = 1000,
        .calls = true,
    },
    {
        .name = "a mask phi across a call in a loop",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.m32 %0 align=4\n"
                "  %3 = address.ptr %0 offset=4\n"
                "  %4 = load.m8 %3 align=1\n"
                "  %5 = const.i64 0x00000000000000000000000000000000\n"
                "  %6 = br.void b1\n"
                "b1:\n"
                "  %7 = phi.m32 [%2, b0], [%10, b1]\n"
                "  %8 = phi.i64 [%5, b0], [%9, b1]\n"
                "  %9 = call.i64 @9(%8) fixed=(i64) variadic=0\n"
                "  %10 = not.m32 %7\n"
                "  %11 = icmp.i1 %9, %1 ult\n"
                "  %12 = cbr.void %11, b1, b2\n"
                "b2:\n"
                "  %13 = address.ptr %0 offset=128\n"
                "  %14 = store.void %13, %10 align=4\n"
                "  %15 = address.ptr %0 offset=132\n"
                "  %16 = store.void %15, %4 align=1\n"
                "  %17 = address.ptr %0 offset=136\n"
                "  %18 = store.void %17, %9 align=8\n"
                "  %19 = ret.void\n"
                "}\n",
        .count = 9,
        .calls = true,
    },
};

// Refusals: a vector, a mask's select.
static const char tier1_masks_test_vector[] = "function (ptr) -> void {\n"
                                              "b0:\n"
                                              "  %0 = param.ptr 0\n"
                                              "  %1 = load.v4i32 %0 align=16\n"
                                              "  %2 = address.ptr %0 offset=128\n"
                                              "  %3 = store.void %2, %1 align=16\n"
                                              "  %4 = ret.void\n"
                                              "}\n";

static const char tier1_masks_test_select[] = "function (ptr, i64) -> void {\n"
                                              "b0:\n"
                                              "  %0 = param.ptr 0\n"
                                              "  %1 = param.i64 1\n"
                                              "  %2 = load.m16 %0 align=2\n"
                                              "  %3 = not.m16 %2\n"
                                              "  %4 = const.i64 0x00000000000000000000000000000000\n"
                                              "  %5 = icmp.i1 %1, %4 ne\n"
                                              "  %6 = select.m16 %5, %2, %3\n"
                                              "  %7 = address.ptr %0 offset=128\n"
                                              "  %8 = store.void %7, %6 align=2\n"
                                              "  %9 = ret.void\n"
                                              "}\n";

// A word mask alone: kmovw and kandw, AVX512F's.
static const char tier1_masks_test_words[] = "function (ptr) -> void {\n"
                                             "b0:\n"
                                             "  %0 = param.ptr 0\n"
                                             "  %1 = load.m16 %0 align=2\n"
                                             "  %2 = address.ptr %0 offset=2\n"
                                             "  %3 = load.m16 %2 align=2\n"
                                             "  %4 = and.m16 %1, %3\n"
                                             "  %5 = address.ptr %0 offset=128\n"
                                             "  %6 = store.void %5, %4 align=2\n"
                                             "  %7 = ret.void\n"
                                             "}\n";

// A function from IR text, by both of ir_parse.c's paths, which must agree, and its dominance (dominators.c's), in the
// test's output arena.
static bool tier1_masks_test_parse(Tier1MasksTest* test, const char* text, IrFunction* function, IrDominance* dominance)
{
    const String8 source = { .pointer = (char*)text, .length = strlen(text) };
    IrFunction twin;
    const IrParseResult vector = ir_parse(source, &test->output, &test->scratch, function);
    const IrParseResult scalar = ir_parse_scalar(source, &test->output, &test->scratch, &twin);
    bool ok = tier1_masks_test_expect(test, vector.error == IR_PARSE_OK && scalar.error == IR_PARSE_OK && vector.consumed == scalar.consumed, "both IR parsers take the fixture");
    if (!ok)
    {
        printf("test-tier1-masks: parse error %u line %" PRIu64 ", scalar %u line %" PRIu64 "\n", (unsigned)vector.error, vector.line, (unsigned)scalar.error, scalar.line);
    }
    ok = ok && tier1_masks_test_expect(test, function->instruction_count == twin.instruction_count && memcmp(function->op, twin.op, function->instruction_count) == 0 && memcmp(function->type, twin.type, function->instruction_count) == 0, "both parsers make the same function");
    IrCfg cfg;
    ok = ok && tier1_masks_test_expect(test, ir_build_cfg(function, &test->output, &test->scratch, &cfg) == IR_OK, "build the fixture's CFG");
    if (ok)
    {
        const DominatorCfg graph = {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.outputs = 0;
        Dominators result;
        ok = tier1_masks_test_expect(test, dominators_analyze(&test->output, &test->scratch, &graph, options, &result) == DOMINATOR_OK, "the fixture's dominance");
        *dominance = (IrDominance){
            .enter = result.preorder,
            .leave = result.subtree_end,
            .block_count = result.block_count,
        };
    }
    return ok;
}

// The helper's records: kxnorq into every opmask register, then rdi + 1 into rax, and ret.
static uint32_t tier1_masks_test_helper(X86Instruction* records)
{
    uint32_t count = 0;
    for (uint8_t k = 0; k < 8; k += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_KXNORQ_K_K_K, .reg = k, .vvvv = k, .rm = k };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 1 };
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    return count;
}

typedef void (*Tier1MasksTestEntry)(uint8_t*, uint64_t);

static __attribute__((no_sanitize("function"))) void tier1_masks_test_call(const uint8_t* code, uint8_t* buffer, uint64_t count)
{
    Tier1MasksTestEntry entry;
    memcpy(&entry, &code, sizeof(entry));
    entry(buffer, count);
}

// The emitted function's records, sized, encoded by both encoders (which must agree) with the helper after them, its
// calls resolved to the helper, in an executable mapping; MAP_FAILED, the failure reported, otherwise.
static uint8_t* tier1_masks_test_load(Tier1MasksTest* test, Tier1EmitResult* emitted, uint64_t* mapped)
{
    Tier1SelectCode* code = &emitted->code;
    X86Instruction helper[16];
    const uint32_t helper_count = tier1_masks_test_helper(helper);
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t helper_start = (capacity + 63) & ~UINT64_C(63);
    *mapped = (helper_start + BUSTER_X86_ENCODER_CAPACITY(helper_count) + 4095) & ~UINT64_C(4095);
    uint8_t* machine = mmap(0, *mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* scalar = malloc(capacity);
    uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(uint32_t));
    uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count) * sizeof(uint32_t));
    uint32_t* scalar_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count) * sizeof(uint32_t));
    bool ok = tier1_masks_test_expect(test, machine != MAP_FAILED && scalar != 0 && offsets != 0 && ends != 0 && scalar_ends != 0, "the encoders' buffers");
    if (ok)
    {
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ok = tier1_masks_test_expect(test, checked.error == BRANCH_SIZING_OK, "the function's branches validate");
    }
    if (ok)
    {
        const BranchSizing sized = size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets);
        ok = tier1_masks_test_expect(test, sized.error == BRANCH_SIZING_OK && x86_target_validate(test->v4, code->records, code->count) == code->count, "the sized records obey x86-64-v4");
        bool covered = true;
        for (uint32_t r = 0; ok && r < code->count; r += 1)
        {
            covered &= x86_effects(code->records[r].form)->operation != X86_OPERATION_NONE;
        }
        ok = ok && tier1_masks_test_expect(test, covered, "every record has a form x86_semantics.c covers");
    }
    for (uint32_t r = 0; ok && test->print && r < code->count; r += 1)
    {
        printf("    ");
        x86_print_instruction(stdout, &code->records[r], 0);
        printf("\n");
    }
    int64_t size = -1;
    if (ok)
    {
        size = x86_encode(code->records, code->count, machine, capacity, 0, ends);
        const int64_t scalar_size = x86_encode_scalar(code->records, code->count, scalar, capacity, 0, scalar_ends);
        ok = tier1_masks_test_expect(test, size > 0 && size == scalar_size && memcmp(machine, scalar, (uint64_t)size) == 0 && memcmp(ends, scalar_ends, (uint64_t)emitted->reference_count * sizeof(uint32_t)) == 0, "both encoders make the same bytes");
        const int64_t helper_size = x86_encode(helper, helper_count, machine + helper_start, BUSTER_X86_ENCODER_CAPACITY(helper_count), 0, 0);
        ok = ok && tier1_masks_test_expect(test, helper_size > 0, "encode the helper");
        test->records += code->count;
    }
    for (uint32_t reference = 0; ok && reference < emitted->reference_count; reference += 1)
    {
        const LinkReference* fixup = &emitted->references[reference];
        const uint32_t end = ends[reference];
        const uint32_t distance = x86_fixup_distance(code->records[fixup->instruction].form, fixup->field);
        ok = tier1_masks_test_expect(test, fixup->kind == BUSTER_LINK_FIXUP_CALL && fixup->target == BUSTER_TIER1_MASKS_TEST_HELPER && distance == 4 && end >= distance, "each call is the helper's");
        if (ok)
        {
            const int32_t relative = (int32_t)((int64_t)helper_start - (int64_t)end);
            memcpy(machine + end - distance, &relative, sizeof(relative));
        }
    }
    ok = ok && tier1_masks_test_expect(test, mprotect(machine, *mapped, PROT_READ | PROT_EXEC) == 0, "make the code executable");
    free(scalar);
    free(offsets);
    free(ends);
    free(scalar_ends);
    if (!ok && machine != MAP_FAILED)
    {
        munmap(machine, *mapped);
    }
    return ok ? machine : MAP_FAILED;
}

// The interpreter's buffer after the function, from `input`: the buffer bound as memory, the pointer its address.
static bool tier1_masks_test_interpret(Tier1MasksTest* test, const IrFunction* function, const uint8_t* input, uint64_t count, uint8_t* output)
{
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    const IrInterpreterFunction entry = { .symbol = BUSTER_TIER1_MASKS_TEST_ENTRY, .function = function };
    const InterpreterBinding binding = { .symbol = BUSTER_TIER1_MASKS_TEST_BINDING, .bytes = output, .size = BUSTER_TIER1_MASKS_TEST_BUFFER, .writable = true };
    const InterpreterNativeBinding native = { .symbol = BUSTER_TIER1_MASKS_TEST_HELPER, .address = (void*)tier1_masks_test_successor };
    const IrInterpreterProgram program = {
        .functions = &entry,
        .function_count = 1,
        .bindings = &binding,
        .binding_count = 1,
        .natives = &native,
        .native_count = 1,
    };
    const InterpreterValue arguments[2] = {
        { .low = (uint64_t)(uintptr_t)output },
        { .low = count },
    };
    const InterpreterResult result = ir_interpret(&program, BUSTER_TIER1_MASKS_TEST_ENTRY, arguments, function->parameter_count, 0, &test->execution);
    return tier1_masks_test_expect(test, result.status == INTERPRETER_OK, "the interpreter runs the fixture");
}

// One fixture by one path: planned, emitted, its allocation checked, and where the host can, run on every input.
static void tier1_masks_test_path(Tier1MasksTest* test, const Tier1MasksFixture* fixture, const IrFunction* function, const IrDominance* dominance, bool vector, const uint8_t* inputs, const uint64_t* counts, const uint8_t* expected)
{
    const uint64_t output_mark = test->output.length;
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selected = tier1_select_plan(function, dominance, &test->output, &test->scratch, vector, &plan, &node);
    bool ok = tier1_masks_test_expect(test, selected == TIER1_SELECT_OK, fixture->name);
    if (!ok)
    {
        printf("test-tier1-masks: %s, %s path: selection error %u at node %u\n", fixture->name, vector ? "vector" : "scalar", (unsigned)selected, node);
    }
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .vector = vector,
    };
    Tier1EmitResult emitted = { 0 };
    if (ok)
    {
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->v4, &test->output, &test->scratch, &emitted, &node);
        ok = tier1_masks_test_expect(test, error == TIER1_EMIT_OK && emitted.general, fixture->name);
        if (!ok)
        {
            printf("test-tier1-masks: %s, %s path: emission error %u at node %u\n", fixture->name, vector ? "vector" : "scalar", (unsigned)error, node);
        }
    }
    if (ok)
    {
        // The opmask class's allocation, by the independent checker; its moves through k0 and of masks.
        Tier1CfgCheck check;
        ok = tier1_masks_test_expect(test, tier1_cfg_verify(&emitted.cfg_input, &emitted.cfg, &test->scratch, &check), "the CFG checker accepts the masks' allocation");
        if (!ok)
        {
            printf("test-tier1-masks: %s: %s (block %u value %u position %u)\n", fixture->name, check.reason, check.block, check.value, check.position);
        }
        uint64_t scratch_moves = 0;
        for (uint32_t m = 0; m < emitted.cfg.move_count; m += 1)
        {
            const uint32_t k0 = BUSTER_TIER1_EMIT_MASK_LOCATION;
            const bool mask = emitted.cfg_input.classes[emitted.cfg.move_values[m]] == BUSTER_TIER1_ALLOCATE_MASK;
            test->mask_moves += mask;
            scratch_moves += mask && (emitted.cfg.move_sources[m] == k0 || emitted.cfg.move_destinations[m] == k0);
        }
        test->scratch_moves += scratch_moves;
        uint32_t mask_values = 0;
        for (uint32_t v = 0; v < emitted.cfg_input.value_count; v += 1)
        {
            mask_values += emitted.cfg_input.classes[v] == BUSTER_TIER1_ALLOCATE_MASK;
        }
        tier1_masks_test_expect(test, mask_values != 0, "the fixture's masks are the opmask class's");
        tier1_masks_test_expect(test, !fixture->cycle || scratch_moves != 0, "the swap's cycle goes through k0");
        tier1_masks_test_expect(test, !fixture->spills || (emitted.spill_stores != 0 && emitted.spill_loads != 0), "masks past k1 to k7 are spilled and reloaded");
        tier1_masks_test_expect(test, !fixture->calls || (emitted.call_stores != 0 && emitted.call_loads != 0), "masks live across a call are saved and reloaded");
        test->mask_stores += emitted.spill_stores;
        test->mask_loads += emitted.spill_loads;
        test->call_saves += emitted.call_stores;
    }
    uint64_t mapped = 0;
    const bool print = test->print;
    test->print = print && vector;
    if (test->print && ok)
    {
        printf("test-tier1-masks: %s:\n", fixture->name);
    }
    uint8_t* machine = ok ? tier1_masks_test_load(test, &emitted, &mapped) : MAP_FAILED;
    test->print = print;
    ok = machine != MAP_FAILED;
    uint8_t* buffers = ok && test->native ? mmap(0, BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    if (ok && test->native)
    {
        ok = tier1_masks_test_expect(test, buffers != MAP_FAILED, "the native runs' shared buffers");
    }
    if (ok && test->native)
    {
        memcpy(buffers, inputs, BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS);
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(10);
            for (uint32_t input = 0; input < BUSTER_TIER1_MASKS_TEST_INPUTS; input += 1)
            {
                tier1_masks_test_call(machine, buffers + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER, counts[input]);
            }
            _exit(0);
        }
        ok = tier1_masks_test_expect(test, child > 0 && tier1_pipeline_wait(child), "the native code exits normally within its budget");
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_INPUTS; input += 1)
        {
            const uint8_t* actual = buffers + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER;
            const uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER;
            const bool equal = memcmp(actual, wanted, BUSTER_TIER1_MASKS_TEST_BUFFER) == 0;
            tier1_masks_test_expect(test, equal, "the native buffer is the interpreter's");
            for (uint32_t byte = 0; !equal && byte < BUSTER_TIER1_MASKS_TEST_BUFFER && test->failures < 20; byte += 1)
            {
                if (actual[byte] != wanted[byte])
                {
                    printf("test-tier1-masks: %s, %s path, input %u: byte %u is %02x, the interpreter's %02x\n", fixture->name, vector ? "vector" : "scalar", input, byte, actual[byte], wanted[byte]);
                    byte = BUSTER_TIER1_MASKS_TEST_BUFFER;
                }
            }
            test->native_runs += 1;
        }
    }
    if (buffers != MAP_FAILED)
    {
        munmap(buffers, BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS);
    }
    if (machine != MAP_FAILED)
    {
        munmap(machine, mapped);
    }
    tier1_masks_test_expect(test, test->scratch.length == 0, "selection and emission return the scratch arena");
    arena_pop_to(&test->output, output_mark);
}

static void tier1_masks_test_fixture(Tier1MasksTest* test, const Tier1MasksFixture* fixture)
{
    const uint64_t output_mark = test->output.length;
    IrFunction function;
    IrDominance dominance;
    if (tier1_masks_test_parse(test, fixture->text, &function, &dominance))
    {
        uint8_t inputs[BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS];
        uint8_t expected[BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS];
        uint64_t counts[BUSTER_TIER1_MASKS_TEST_INPUTS];
        bool ok = true;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_INPUTS; input += 1)
        {
            uint8_t* bytes = inputs + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER;
            for (uint32_t byte = 0; byte < BUSTER_TIER1_MASKS_TEST_BUFFER; byte += 1)
            {
                // Every fourth input's masks all zeros or all ones, the others random.
                const uint64_t random = tier1_masks_test_next(test);
                bytes[byte] = input % 4 == 1 ? 0 : input % 4 == 2 ? 0xff : (uint8_t)random;
            }
            counts[input] = fixture->count != 0 ? 1 + tier1_masks_test_next(test) % fixture->count : 0;
            ok = tier1_masks_test_interpret(test, &function, bytes, counts[input], expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER);
        }
        for (uint32_t path = 0; ok && path < 2; path += 1)
        {
            tier1_masks_test_path(test, fixture, &function, &dominance, path == 0, inputs, counts, expected);
        }
    }
    tier1_masks_test_expect(test, test->scratch.length == 0, "parsing returns the scratch arena");
    arena_pop_to(&test->output, output_mark);
}

// tier1_select_has_vectors' vector path against its scalar twin and the expected answer: operation and type columns of
// every length to 200, each ending at a PROT_NONE page, of masks' operations and types, which it must take, then with a
// vector type or operation at each place, which it must refuse.
static void tier1_masks_test_scan(Tier1MasksTest* test)
{
    const long page_result = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_result > 0 ? (uint64_t)page_result : 4096;
    uint8_t* columns[2] = { MAP_FAILED, MAP_FAILED };
    bool ready = true;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        columns[k] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = ready && columns[k] != MAP_FAILED && mprotect(columns[k] + page, page, PROT_NONE) == 0;
    }
    tier1_masks_test_expect(test, ready, "guarded vector scan columns");
    static const uint8_t vectors[6][2] = {
        { IR_ADD, IR_V4I32 },
        { IR_LOAD, IR_V8I32 },
        { IR_PHI, IR_V16I32 },
        { IR_VCONST, IR_M16 },
        { IR_VCMP, IR_M8 },
        { IR_VSELECT, IR_I32 },
    };
    static const uint8_t masks[4] = { IR_M8, IR_M16, IR_M32, IR_M64 };
    for (uint32_t count = 0; ready && count <= 200; count += 1)
    {
        uint8_t* op = columns[0] + page - count;
        uint8_t* type = columns[1] + page - count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            op[i] = i % 3 == 0 ? IR_AND : i % 3 == 1 ? IR_LOAD : IR_NOT;
            type[i] = masks[i % 4];
        }
        const IrFunction function = { .op = op, .type = type, .instruction_count = count };
        tier1_masks_test_expect(test, !tier1_select_has_vectors(&function, true) && !tier1_select_has_vectors(&function, false), "masks' operations hold no vector");
        for (uint32_t place = 0; place < count; place += 1)
        {
            const uint8_t saved_op = op[place];
            const uint8_t saved_type = type[place];
            for (uint32_t v = 0; v < 6; v += 1)
            {
                op[place] = vectors[v][0];
                type[place] = vectors[v][1];
                tier1_masks_test_expect(test, tier1_select_has_vectors(&function, true) && tier1_select_has_vectors(&function, false), "a vector type or operation at any place, the tail's last among them");
            }
            op[place] = saved_op;
            type[place] = saved_type;
        }
    }
    for (uint32_t k = 0; k < 2; k += 1)
    {
        if (columns[k] != MAP_FAILED)
        {
            munmap(columns[k], page * 2);
        }
    }
}

// What the selector or the consumer must refuse: a vector and a mask's select at selection, a target without the forms
// at emission, on both paths.
static void tier1_masks_test_refusals(Tier1MasksTest* test, const uint8_t* baseline, const uint8_t* foundation)
{
    const char* const texts[] = {
        tier1_masks_test_vector,
        tier1_masks_test_select,
        tier1_masks_test_fixtures[0].text,
        tier1_masks_test_fixtures[0].text,
        tier1_masks_test_words,
    };
    // The target each is emitted for (selection refusals stop first), and whether it is refused.
    const uint8_t* const targets[] = {
        test->v4,
        test->v4,
        baseline,
        foundation,
        foundation,
    };
    const bool refused[] = {
        true,
        true,
        true,
        true,
        false,
    };
    const char* const names[] = {
        "a vector is #141's, refused at selection",
        "a mask's select is refused at selection",
        "the baseline target has no opmask form",
        "AVX512F alone has no kandb, kmovd or kmovq",
        "AVX512F alone has kmovw and kandw",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(texts); c += 1)
    {
        const uint64_t output_mark = test->output.length;
        IrFunction function;
        IrDominance dominance;
        if (tier1_masks_test_parse(test, texts[c], &function, &dominance))
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                Tier1SelectPlan plan;
                uint32_t node = BUSTER_IR_NONE;
                const Tier1SelectError selected = tier1_select_plan(&function, &dominance, &test->output, &test->scratch, path == 0, &plan, &node);
                const Tier1EmitOptions options = {
                    .register_mask = tier1_liveness_register_mask(),
                    .vector = path == 0,
                };
                Tier1EmitResult emitted = { 0 };
                const Tier1EmitError error = selected == TIER1_SELECT_OK ? tier1_emit_function(&plan, &options, targets[c], &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_UNSUPPORTED;
                const bool selection = c < 2;
                const bool expected = selection ? selected == TIER1_SELECT_ERROR_UNSUPPORTED : refused[c] ? selected == TIER1_SELECT_OK && error == TIER1_EMIT_TARGET : selected == TIER1_SELECT_OK && error == TIER1_EMIT_OK;
                tier1_masks_test_expect(test, expected, names[c]);
                if (!expected)
                {
                    printf("test-tier1-masks: %s, %s path: selection %u, emission %u, node %u\n", names[c], path == 0 ? "vector" : "scalar", (unsigned)selected, (unsigned)error, node);
                }
                test->refused += refused[c] && expected;
            }
        }
        tier1_masks_test_expect(test, test->scratch.length == 0, "refusals return the scratch arena");
        arena_pop_to(&test->output, output_mark);
    }
}

static __attribute__((unused)) int run_tier1_mask_tests(int argc, char** argv)
{
    uint64_t seed = 179;
    bool valid = true;
    bool print = false;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--print") == 0)
        {
            print = true;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            char* end = 0;
            seed = strtoull(argv[i], &end, 0);
            valid = argv[i][0] != 0 && *end == 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-tier1-masks [--seed N] [--print]\n");
    }
    else
    {
        Tier1MasksTest test = {
            .random = seed * 2 + 1,
            .print = print,
            .v4 = malloc(X86_FORM_COUNT),
            .output = arena_reserve(64u << 20),
            .scratch = arena_reserve(64u << 20),
            .execution = arena_reserve(64u << 20),
        };
        uint8_t* baseline = malloc(X86_FORM_COUNT);
        uint8_t* foundation = malloc(X86_FORM_COUNT);
        const bool ready = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && test.v4 != 0 && baseline != 0 && foundation != 0 && test.output.base != 0 && test.scratch.base != 0 && test.execution.base != 0;
        if (tier1_masks_test_expect(&test, ready, "initialize the tables and arenas"))
        {
            X86Target v4 = { 0 };
            const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
            tier1_masks_test_expect(&test, parsed, "the x86-64-v4 target");
            x86_target_forms(&v4, test.v4);
            const X86Target base = { .architecture = X86_ARCH_BASELINE };
            x86_target_forms(&base, baseline);
            X86Target v3 = { 0 };
            x86_target_parse("x86-64-v3", 9, &v3);
            const X86Target f = { .features = v3.features | BUSTER_X86_FEATURE(AVX512F), .architecture = X86_ARCH_OTHER };
            x86_target_forms(&f, foundation);
            // The native runs need the forms the fixtures have: AVX512F's words, DQ's bytes, BW's doublewords and quadwords.
            const X86Target host = x86_target_native();
            const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ);
            test.native = (host.features & needed) == needed;
            for (uint32_t f_index = 0; f_index < BUSTER_ARRAY_LENGTH(tier1_masks_test_fixtures); f_index += 1)
            {
                tier1_masks_test_fixture(&test, &tier1_masks_test_fixtures[f_index]);
            }
            tier1_masks_test_refusals(&test, baseline, foundation);
            tier1_masks_test_scan(&test);
            const uint64_t expected_runs = test.native ? 2u * BUSTER_ARRAY_LENGTH(tier1_masks_test_fixtures) * BUSTER_TIER1_MASKS_TEST_INPUTS : 0;
            tier1_masks_test_expect(&test, test.native_runs == expected_runs, "every fixture runs natively on every input by both paths, where the host can");
            tier1_masks_test_expect(&test, test.refused == 8, "every refusal on both paths");
        }
        printf("test-tier1-masks: %" PRIu64 " checks, %" PRIu64 " failures; %s: %" PRIu64 " runs against the interpreter; %" PRIu64 " records, %" PRIu64 " mask moves, %" PRIu64 " through k0, %" PRIu64 " mask spills, %" PRIu64 " reloads, %" PRIu64 " masks saved around calls; %u refusals (seed=%" PRIu64 ")\n",
               test.checks, test.failures, test.native ? "native" : "no AVX-512 here, compiled only", test.native_runs, test.records, test.mask_moves, test.scratch_moves, test.mask_stores, test.mask_loads, test.call_saves, test.refused, seed);
        result = test.failures == 0 ? 0 : 1;
        arena_release(&test.execution);
        arena_release(&test.scratch);
        arena_release(&test.output);
        free(foundation);
        free(baseline);
        free(test.v4);
    }
    return result;
}
