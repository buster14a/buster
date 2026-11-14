// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier1-wide (#141): the 128-bit integers' split into 64-bit halves (ir_wide.c) through Tier 1, natively, against
// the shared IR's interpreter on the unsplit function (#176), whose I128 arithmetic is the host compiler's __int128.
// Include after tier1_pipeline_test.c, whose child-process wait it shares, and ir_wide.c.
//
// Each program is IR text (ir_parse.c's, #175): a function of a pointer to a buffer, and of a count where it loops,
// that loads 128-bit integers from the buffer's first half, computes, and stores into its second half. It is parsed by
// both of ir_parse.c's paths into the same function and run by the interpreter on random buffers: the expected buffers.
// Then it is split by both of ir_wide.c's paths, which must make the same function, without an I128 instruction, that
// both verifiers take with its dominance, and that the interpreter runs into the same buffers; optimized (ir_optimize's
// SCCP, dead code and CFG cleanup, as --tier1 runs it) and run again; then planned and emitted by both of Tier 1's paths
// for the x86-64 baseline, with rematerialization and without, its allocation certified (#939), encoded by both
// encoders into the same bytes, and run natively in a child process on every buffer, which must leave the expected
// bytes. The programs: hand-written fixtures (every operation the split takes, sqlite3Multiply128's product of two
// zero extensions, which must leave one imul and one mul, phis around a loop, unaligned and volatile accesses, addresses
// with an index), eight long double phis rotated around a loop, every edge's copy of all eight through the x87 stack,
// eight long double phis at a join of 200 predecessors, whose copies' records the emitter must make room for, 300
// conversions of a long double to an unsigned 64-bit integer in one block, 20 records each, which it must make room for
// too, and random straight-line programs of every operation on values loaded or computed before. And the
// refusals: an I128 parameter, a division, a conversion to a floating value, a switch, which the split refuses as
// unsupported; a function without an I128 instruction, returned as it is; and the scan's vector path against its scalar
// twin on type columns of every length to 200, each ending at a PROT_NONE page, with an I128 at each place; and nine
// long double phis, an edge's copy past the x87 stack's depth, which the CFG consumer refuses as unsupported; and
// functions whose extra slices overlap, a call's arguments sharing a phi's words, which both split paths keep apart;
// and I128 phis that share one predecessor list, whose halves' lists take the output's extra table past a u32's count,
// which both paths refuse as a limit before they allocate. Then the vectors wider than the target's registers (#1624,
// ir_vector_split.c), tier1_wide_vectors_test.c's.

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_TIER1_WIDE_TEST_BUFFER 512
#define BUSTER_TIER1_WIDE_TEST_HALF 256
#define BUSTER_TIER1_WIDE_TEST_INPUTS 16
#define BUSTER_TIER1_WIDE_TEST_ENTRY 1u
#define BUSTER_TIER1_WIDE_TEST_BINDING 100u
#define BUSTER_TIER1_WIDE_TEST_PROGRAMS 120
#define BUSTER_TIER1_WIDE_TEST_TEXT 65536
// Tier 0's random 128-bit programs (tier0_int128_test.c) that tier1_wide_test_typed takes through Tier 1.
#define BUSTER_TIER1_WIDE_TEST_TYPED 300

typedef struct Tier1WideTest Tier1WideTest;
struct Tier1WideTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
    // The x86-64 baseline's forms, which every record must be among.
    uint8_t* baseline;
    bool print;
    Arena output;
    Arena scratch;
    Arena execution;
    // The typed programs' promotion and optimizer outputs, each in an arena that owns none of its input, as the C
    // driver's --tier1 gives each step one.
    Arena promoted;
    Arena optimized;
    uint64_t native_runs;
    uint64_t interpreted;
    uint64_t records;
    uint64_t wide;
    uint64_t halves;
    uint32_t programs;
    uint32_t refused;
    // The wide vectors' (#1624, tier1_wide_vectors_test.c): their native runs, apart from the I128 programs', the
    // fixtures through every stage at a piece width, and the refusals.
    uint64_t vector_native_runs;
    uint32_t vector_programs;
    uint32_t vector_refused;
};

typedef struct Tier1WideFixture Tier1WideFixture;
struct Tier1WideFixture
{
    const char* name;
    const char* text;
    // The count it loops to, at most this (0 for a function of the pointer alone).
    uint32_t count;
    // After the optimizer: how many I64 muls and mulhus, where the fixture checks them (UINT32_MAX for any).
    uint32_t multiplies;
    uint32_t high_multiplies;
    // Whether the optimizer must leave no select: every shift's count a constant, through its conversions.
    bool constant_counts;
};

static bool tier1_wide_test_expect(Tier1WideTest* test, bool condition, const char* message)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 20)
        {
            printf("test-tier1-wide: %s\n", message);
        }
    }
    return condition;
}

static uint64_t tier1_wide_test_next(Tier1WideTest* test)
{
    test->random = test->random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return test->random >> 17;
}

static const Tier1WideFixture tier1_wide_test_fixtures[] = {
    {
        // sqlite3Multiply128: two u64s' full product, its high and low halves.
        .name = "a product of zero extensions",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i64 %0 align=8\n"
                "  %2 = address.ptr %0 offset=8\n"
                "  %3 = load.i64 %2 align=8\n"
                "  %4 = zext.i128 %1\n"
                "  %5 = zext.i128 %3\n"
                "  %6 = mul.i128 %4, %5\n"
                "  %7 = const.i128 0x40\n"
                "  %8 = lshr.i128 %6, %7\n"
                "  %9 = trunc.i64 %8\n"
                "  %10 = trunc.i64 %6\n"
                "  %11 = address.ptr %0 offset=256\n"
                "  %12 = store.void %11, %9 align=8\n"
                "  %13 = address.ptr %0 offset=264\n"
                "  %14 = store.void %13, %10 align=8\n"
                "  %15 = ret.void\n"
                "}\n",
        .multiplies = 1,
        .high_multiplies = 1,
    },
    {
        // `r >> 64` of an extended product as the typed stream gives it: its count a zero extension of an i32 constant,
        // which the split takes as the constant (no select, the high half itself).
        .name = "a shift by an extended constant",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i128 %0 align=16\n"
                "  %2 = const.i32 0x40\n"
                "  %3 = zext.i128 %2\n"
                "  %4 = lshr.i128 %1, %3\n"
                "  %5 = trunc.i64 %4\n"
                "  %6 = const.i8 0x44\n"
                "  %7 = sext.i64 %6\n"
                "  %8 = zext.i128 %7\n"
                "  %9 = shl.i128 %1, %8\n"
                "  %10 = address.ptr %0 offset=256\n"
                "  %11 = store.void %10, %5 align=8\n"
                "  %12 = address.ptr %0 offset=272\n"
                "  %13 = store.void %12, %9 align=16\n"
                "  %14 = ret.void\n"
                "}\n",
        .multiplies = 0,
        .high_multiplies = 0,
        .constant_counts = true,
    },
    {
        .name = "arithmetic and logic",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i128 %0 align=16\n"
                "  %2 = address.ptr %0 offset=16\n"
                "  %3 = load.i128 %2 align=16\n"
                "  %4 = add.i128 %1, %3\n"
                "  %5 = sub.i128 %1, %3\n"
                "  %6 = mul.i128 %1, %3\n"
                "  %7 = and.i128 %1, %3\n"
                "  %8 = or.i128 %1, %3\n"
                "  %9 = xor.i128 %1, %3\n"
                "  %10 = not.i128 %1\n"
                "  %11 = neg.i128 %3\n"
                "  %12 = copy.i128 %11\n"
                "  %13 = address.ptr %0 offset=256\n"
                "  %14 = store.void %13, %4 align=16\n"
                "  %15 = address.ptr %0 offset=272\n"
                "  %16 = store.void %15, %5 align=16\n"
                "  %17 = address.ptr %0 offset=288\n"
                "  %18 = store.void %17, %6 align=16\n"
                "  %19 = address.ptr %0 offset=304\n"
                "  %20 = store.void %19, %7 align=16\n"
                "  %21 = address.ptr %0 offset=320\n"
                "  %22 = store.void %21, %8 align=16\n"
                "  %23 = address.ptr %0 offset=336\n"
                "  %24 = store.void %23, %9 align=16\n"
                "  %25 = address.ptr %0 offset=352\n"
                "  %26 = store.void %25, %10 align=16\n"
                "  %27 = address.ptr %0 offset=368\n"
                "  %28 = store.void %27, %12 align=16\n"
                "  %29 = add.i128 %4, %6\n"
                "  %30 = mul.i128 %29, %5\n"
                "  %31 = address.ptr %0 offset=384\n"
                "  %32 = store.void %31, %30 align=16\n"
                "  %33 = ret.void\n"
                "}\n",
        .multiplies = UINT32_MAX,
        .high_multiplies = 2,
    },
    {
        .name = "shifts by constants",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i128 %0 align=16\n"
                "  %2 = const.i128 0x0\n"
                "  %3 = const.i128 0x1\n"
                "  %4 = const.i128 0x3f\n"
                "  %5 = const.i128 0x40\n"
                "  %6 = const.i128 0x41\n"
                "  %7 = const.i128 0x7f\n"
                "  %8 = shl.i128 %1, %2\n"
                "  %9 = shl.i128 %1, %3\n"
                "  %10 = shl.i128 %1, %4\n"
                "  %11 = shl.i128 %1, %5\n"
                "  %12 = shl.i128 %1, %6\n"
                "  %13 = shl.i128 %1, %7\n"
                "  %14 = lshr.i128 %1, %3\n"
                "  %15 = lshr.i128 %1, %4\n"
                "  %16 = lshr.i128 %1, %5\n"
                "  %17 = lshr.i128 %1, %6\n"
                "  %18 = lshr.i128 %1, %7\n"
                "  %19 = ashr.i128 %1, %3\n"
                "  %20 = ashr.i128 %1, %4\n"
                "  %21 = ashr.i128 %1, %5\n"
                "  %22 = ashr.i128 %1, %6\n"
                "  %23 = ashr.i128 %1, %7\n"
                "  %24 = address.ptr %0 offset=256\n"
                "  %25 = store.void %24, %8 align=16\n"
                "  %26 = address.ptr %0 offset=272\n"
                "  %27 = store.void %26, %9 align=16\n"
                "  %28 = address.ptr %0 offset=288\n"
                "  %29 = store.void %28, %10 align=16\n"
                "  %30 = address.ptr %0 offset=304\n"
                "  %31 = store.void %30, %11 align=16\n"
                "  %32 = address.ptr %0 offset=320\n"
                "  %33 = store.void %32, %12 align=16\n"
                "  %34 = address.ptr %0 offset=336\n"
                "  %35 = store.void %34, %13 align=16\n"
                "  %36 = address.ptr %0 offset=352\n"
                "  %37 = store.void %36, %14 align=16\n"
                "  %38 = address.ptr %0 offset=368\n"
                "  %39 = store.void %38, %15 align=16\n"
                "  %40 = address.ptr %0 offset=384\n"
                "  %41 = store.void %40, %16 align=16\n"
                "  %42 = address.ptr %0 offset=400\n"
                "  %43 = store.void %42, %17 align=16\n"
                "  %44 = address.ptr %0 offset=416\n"
                "  %45 = store.void %44, %18 align=16\n"
                "  %46 = address.ptr %0 offset=432\n"
                "  %47 = store.void %46, %19 align=16\n"
                "  %48 = address.ptr %0 offset=448\n"
                "  %49 = store.void %48, %20 align=16\n"
                "  %50 = address.ptr %0 offset=464\n"
                "  %51 = store.void %50, %21 align=16\n"
                "  %52 = address.ptr %0 offset=480\n"
                "  %53 = store.void %52, %22 align=16\n"
                "  %54 = address.ptr %0 offset=496\n"
                "  %55 = store.void %54, %23 align=16\n"
                "  %56 = ret.void\n"
                "}\n",
        .multiplies = 0,
        .high_multiplies = 0,
    },
    {
        // Counts of 0 to 127, from a byte (masked as an i32, whose narrow arithmetic the C lowering never makes), an i32
        // and an i128 (each masked), and a narrower value's i128 count.
        .name = "shifts by variable counts",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i128 %0 align=16\n"
                "  %2 = address.ptr %0 offset=16\n"
                "  %3 = load.i8 %2 align=1\n"
                "  %4 = zext.i32 %3\n"
                "  %5 = const.i32 0x7f\n"
                "  %6 = and.i32 %4, %5\n"
                "  %7 = trunc.i8 %6\n"
                "  %8 = address.ptr %0 offset=20\n"
                "  %9 = load.i32 %8 align=4\n"
                "  %10 = and.i32 %9, %5\n"
                "  %11 = address.ptr %0 offset=32\n"
                "  %12 = load.i128 %11 align=16\n"
                "  %13 = const.i128 0x7f\n"
                "  %14 = and.i128 %12, %13\n"
                "  %15 = shl.i128 %1, %7\n"
                "  %16 = lshr.i128 %1, %10\n"
                "  %17 = ashr.i128 %1, %14\n"
                "  %18 = shl.i128 %1, %14\n"
                "  %19 = lshr.i128 %1, %7\n"
                "  %20 = ashr.i128 %1, %10\n"
                "  %21 = address.ptr %0 offset=48\n"
                "  %22 = load.i64 %21 align=8\n"
                "  %23 = const.i128 0x3f\n"
                "  %24 = and.i128 %12, %23\n"
                "  %25 = lshr.i64 %22, %24\n"
                "  %26 = address.ptr %0 offset=256\n"
                "  %27 = store.void %26, %15 align=16\n"
                "  %28 = address.ptr %0 offset=272\n"
                "  %29 = store.void %28, %16 align=16\n"
                "  %30 = address.ptr %0 offset=288\n"
                "  %31 = store.void %30, %17 align=16\n"
                "  %32 = address.ptr %0 offset=304\n"
                "  %33 = store.void %32, %18 align=16\n"
                "  %34 = address.ptr %0 offset=320\n"
                "  %35 = store.void %34, %19 align=16\n"
                "  %36 = address.ptr %0 offset=336\n"
                "  %37 = store.void %36, %20 align=16\n"
                "  %38 = address.ptr %0 offset=352\n"
                "  %39 = store.void %38, %25 align=8\n"
                "  %40 = ret.void\n"
                "}\n",
        .multiplies = 0,
        .high_multiplies = 0,
    },
    {
        // Every predicate, on two loaded values and on a value and itself, and a select by one.
        .name = "comparisons and selects",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i128 %0 align=16\n"
                "  %2 = address.ptr %0 offset=16\n"
                "  %3 = load.i128 %2 align=16\n"
                "  %4 = icmp.i1 %1, %3 eq\n"
                "  %5 = icmp.i1 %1, %3 ne\n"
                "  %6 = icmp.i1 %1, %3 lt\n"
                "  %7 = icmp.i1 %1, %3 le\n"
                "  %8 = icmp.i1 %1, %3 gt\n"
                "  %9 = icmp.i1 %1, %3 ge\n"
                "  %10 = icmp.i1 %1, %3 ult\n"
                "  %11 = icmp.i1 %1, %3 ule\n"
                "  %12 = icmp.i1 %1, %3 ugt\n"
                "  %13 = icmp.i1 %1, %3 uge\n"
                "  %14 = icmp.i1 %1, %1 le\n"
                "  %15 = icmp.i1 %1, %1 ult\n"
                "  %16 = select.i128 %6, %1, %3\n"
                "  %17 = select.i128 %12, %1, %3\n"
                "  %18 = zext.i8 %4\n"
                "  %19 = zext.i8 %5\n"
                "  %20 = zext.i8 %6\n"
                "  %21 = zext.i8 %7\n"
                "  %22 = zext.i8 %8\n"
                "  %23 = zext.i8 %9\n"
                "  %24 = zext.i8 %10\n"
                "  %25 = zext.i8 %11\n"
                "  %26 = zext.i8 %12\n"
                "  %27 = zext.i8 %13\n"
                "  %28 = zext.i8 %14\n"
                "  %29 = zext.i8 %15\n"
                "  %30 = address.ptr %0 offset=256\n"
                "  %31 = store.void %30, %18 align=1\n"
                "  %32 = address.ptr %0 offset=257\n"
                "  %33 = store.void %32, %19 align=1\n"
                "  %34 = address.ptr %0 offset=258\n"
                "  %35 = store.void %34, %20 align=1\n"
                "  %36 = address.ptr %0 offset=259\n"
                "  %37 = store.void %36, %21 align=1\n"
                "  %38 = address.ptr %0 offset=260\n"
                "  %39 = store.void %38, %22 align=1\n"
                "  %40 = address.ptr %0 offset=261\n"
                "  %41 = store.void %40, %23 align=1\n"
                "  %42 = address.ptr %0 offset=262\n"
                "  %43 = store.void %42, %24 align=1\n"
                "  %44 = address.ptr %0 offset=263\n"
                "  %45 = store.void %44, %25 align=1\n"
                "  %46 = address.ptr %0 offset=264\n"
                "  %47 = store.void %46, %26 align=1\n"
                "  %48 = address.ptr %0 offset=265\n"
                "  %49 = store.void %48, %27 align=1\n"
                "  %50 = address.ptr %0 offset=266\n"
                "  %51 = store.void %50, %28 align=1\n"
                "  %52 = address.ptr %0 offset=267\n"
                "  %53 = store.void %52, %29 align=1\n"
                "  %54 = address.ptr %0 offset=272\n"
                "  %55 = store.void %54, %16 align=16\n"
                "  %56 = address.ptr %0 offset=288\n"
                "  %57 = store.void %56, %17 align=16\n"
                "  %58 = ret.void\n"
                "}\n",
        .multiplies = 0,
        .high_multiplies = 0,
    },
    {
        // Extensions from every width, truncations to every width, a pointer's conversions, and unaligned, volatile and
        // indexed accesses.
        .name = "conversions and accesses",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i8 %0 align=1\n"
                "  %2 = address.ptr %0 offset=2\n"
                "  %3 = load.i16 %2 align=2\n"
                "  %4 = address.ptr %0 offset=4\n"
                "  %5 = load.i32 %4 align=4\n"
                "  %6 = address.ptr %0 offset=8\n"
                "  %7 = load.i64 %6 align=8\n"
                "  %8 = sext.i128 %1\n"
                "  %9 = zext.i128 %3\n"
                "  %10 = sext.i128 %5\n"
                "  %11 = sext.i128 %7\n"
                "  %12 = icmp.i1 %7, %7 eq\n"
                "  %13 = zext.i128 %12\n"
                "  %14 = add.i128 %8, %9\n"
                "  %15 = add.i128 %14, %10\n"
                "  %16 = add.i128 %15, %11\n"
                "  %17 = add.i128 %16, %13\n"
                "  %18 = address.ptr %0 offset=17\n"
                "  %19 = load.i128 %18 align=1\n"
                "  %20 = xor.i128 %17, %19\n"
                "  %21 = trunc.i8 %20\n"
                "  %22 = trunc.i16 %20\n"
                "  %23 = trunc.i32 %20\n"
                "  %24 = trunc.i64 %20\n"
                "  %25 = trunc.i16 %20\n"
                "  %26 = trunc.i8 %25\n"
                "  %27 = ptrtoint.i128 %0\n"
                "  %28 = sub.i128 %27, %27\n"
                "  %29 = inttoptr.ptr %28\n"
                "  %30 = icmp.i1 %29, %29 eq\n"
                "  %31 = zext.i8 %30\n"
                "  %32 = address.ptr %0 offset=256\n"
                "  %33 = store.void %32, %21 align=1\n"
                "  %34 = address.ptr %0 offset=258\n"
                "  %35 = store.void %34, %22 align=2\n"
                "  %36 = address.ptr %0 offset=260\n"
                "  %37 = store.void %36, %23 align=4\n"
                "  %38 = address.ptr %0 offset=264\n"
                "  %39 = store.void %38, %24 align=8\n"
                "  %40 = address.ptr %0 offset=272\n"
                "  %41 = store.void %40, %26 align=1\n"
                "  %42 = address.ptr %0 offset=273\n"
                "  %43 = store.void %42, %31 align=1\n"
                "  %44 = address.ptr %0 offset=281\n"
                "  %45 = store.void %44, %20 align=1 volatile\n"
                "  %46 = address.ptr %0 offset=24\n"
                "  %47 = load.i64 %46 align=8\n"
                "  %48 = const.i64 0x70\n"
                "  %49 = and.i64 %47, %48\n"
                "  %50 = address.ptr %0 offset=32\n"
                "  %51 = address.ptr %50 + %49 * 1 offset=0\n"
                "  %52 = load.i128 %51 align=16 volatile\n"
                "  %53 = address.ptr %0 offset=304\n"
                "  %54 = address.ptr %53 + %49 * 1 offset=16\n"
                "  %55 = store.void %54, %52 align=16\n"
                "  %56 = ret.void\n"
                "}\n",
        .multiplies = 0,
        .high_multiplies = 0,
    },
    {
        // Two 128-bit values carried around a loop, which swap each round (a phi cycle of each half), and a product and
        // a sum accumulated, `count` rounds.
        .name = "phis around a loop",
        .count = 40,
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.i128 %0 align=16\n"
                "  %3 = address.ptr %0 offset=16\n"
                "  %4 = load.i128 %3 align=16\n"
                "  %5 = const.i64 0x0\n"
                "  %6 = br.void b1\n"
                "b1:\n"
                "  %7 = phi.i128 [%2, b0], [%8, b1]\n"
                "  %8 = phi.i128 [%4, b0], [%7, b1]\n"
                "  %9 = phi.i128 [%2, b0], [%13, b1]\n"
                "  %10 = phi.i64 [%5, b0], [%16, b1]\n"
                "  %11 = phi.i128 [%4, b0], [%14, b1]\n"
                "  %12 = mul.i128 %9, %7\n"
                "  %13 = add.i128 %12, %8\n"
                "  %14 = sub.i128 %11, %9\n"
                "  %15 = const.i64 0x1\n"
                "  %16 = add.i64 %10, %15\n"
                "  %17 = icmp.i1 %16, %1 ult\n"
                "  %18 = cbr.void %17, b1, b2\n"
                "b2:\n"
                "  %19 = address.ptr %0 offset=256\n"
                "  %20 = store.void %19, %7 align=16\n"
                "  %21 = address.ptr %0 offset=272\n"
                "  %22 = store.void %21, %8 align=16\n"
                "  %23 = address.ptr %0 offset=288\n"
                "  %24 = store.void %23, %13 align=16\n"
                "  %25 = address.ptr %0 offset=304\n"
                "  %26 = store.void %25, %14 align=16\n"
                "  %27 = ret.void\n"
                "}\n",
        .multiplies = UINT32_MAX,
        .high_multiplies = 1,
    },
};

// What the split must refuse: an I128 parameter, a division, a conversion to a floating value, a switch on one.
static const char* const tier1_wide_test_refusals[] = {
    "function (i128) -> void {\n"
    "b0:\n"
    "  %0 = param.i128 0\n"
    "  %1 = ret.void\n"
    "}\n",
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.i128 %0 align=16\n"
    "  %2 = udiv.i128 %1, %1\n"
    "  %3 = store.void %0, %2 align=16\n"
    "  %4 = ret.void\n"
    "}\n",
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.i128 %0 align=16\n"
    "  %2 = sitofp.f64 %1\n"
    "  %3 = store.void %0, %2 align=8\n"
    "  %4 = ret.void\n"
    "}\n",
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.i128 %0 align=16\n"
    "  %2 = switch.void %1, default b1, [0x1, b1]\n"
    "b1:\n"
    "  %3 = ret.void\n"
    "}\n",
};

// A function from IR text, by both of ir_parse.c's paths, which must agree.
static bool tier1_wide_test_parse(Tier1WideTest* test, const char* name, const char* text, IrFunction* function)
{
    const String8 source = { .pointer = (char*)text, .length = strlen(text) };
    IrFunction twin;
    const IrParseResult vector = ir_parse(source, &test->output, &test->scratch, function);
    const IrParseResult scalar = ir_parse_scalar(source, &test->output, &test->scratch, &twin);
    bool ok = tier1_wide_test_expect(test, vector.error == IR_PARSE_OK && scalar.error == IR_PARSE_OK && vector.consumed == scalar.consumed, "both IR parsers take the program");
    if (!ok)
    {
        printf("test-tier1-wide: %s: parse error %u line %" PRIu64 ", scalar %u line %" PRIu64 "\n", name, (unsigned)vector.error, vector.line, (unsigned)scalar.error, scalar.line);
    }
    return ok && tier1_wide_test_expect(test, function->instruction_count == twin.instruction_count && memcmp(function->op, twin.op, function->instruction_count) == 0 && memcmp(function->type, twin.type, function->instruction_count) == 0,
                                        "both parsers make the same function");
}

// A function's dominance, from its CFG, as the verifier and Tier 1's selection take it.
static bool tier1_wide_test_dominance(Tier1WideTest* test, const IrFunction* function, IrDominance* dominance)
{
    IrCfg cfg;
    bool ok = tier1_wide_test_expect(test, ir_build_cfg(function, &test->output, &test->scratch, &cfg) == IR_OK, "build the program's CFG");
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
        ok = tier1_wide_test_expect(test, dominators_analyze(&test->output, &test->scratch, &graph, options, &result) == DOMINATOR_OK, "the program's dominance");
        *dominance = (IrDominance){
            .enter = result.preorder,
            .leave = result.subtree_end,
            .block_count = result.block_count,
        };
    }
    return ok;
}

// The interpreter's buffer after the function, from `input`: the buffer bound as memory, the pointer its address.
static bool tier1_wide_test_interpret(Tier1WideTest* test, const IrFunction* function, const uint8_t* input, uint64_t count, uint8_t* output)
{
    memcpy(output, input, BUSTER_TIER1_WIDE_TEST_BUFFER);
    const IrInterpreterFunction entry = { .symbol = BUSTER_TIER1_WIDE_TEST_ENTRY, .function = function };
    const InterpreterBinding binding = { .symbol = BUSTER_TIER1_WIDE_TEST_BINDING, .bytes = output, .size = BUSTER_TIER1_WIDE_TEST_BUFFER, .writable = true };
    const IrInterpreterProgram program = {
        .functions = &entry,
        .function_count = 1,
        .bindings = &binding,
        .binding_count = 1,
    };
    const InterpreterValue arguments[2] = {
        { .low = (uint64_t)(uintptr_t)output },
        { .low = count },
    };
    const InterpreterResult result = ir_interpret(&program, BUSTER_TIER1_WIDE_TEST_ENTRY, arguments, function->parameter_count, 0, &test->execution);
    arena_pop_to(&test->execution, 0);
    test->interpreted += 1;
    return tier1_wide_test_expect(test, result.status == INTERPRETER_OK, "the interpreter runs the program");
}

// Whether the interpreter leaves the expected buffers on every input.
static bool tier1_wide_test_agrees(Tier1WideTest* test, const char* name, const char* stage, const IrFunction* function, const uint8_t* inputs, const uint64_t* counts, const uint8_t* expected)
{
    bool ok = true;
    // Aligned to 64, as the native runs' mapping is: the interpreter checks the accesses' alignments (#1624's 64-byte
    // vectors').
    uint8_t actual[BUSTER_TIER1_WIDE_TEST_BUFFER] __attribute__((aligned(64)));
    for (uint32_t input = 0; ok && input < BUSTER_TIER1_WIDE_TEST_INPUTS; input += 1)
    {
        ok = tier1_wide_test_interpret(test, function, inputs + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER, counts[input], actual);
        ok = ok && tier1_wide_test_expect(test, memcmp(actual, expected + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER, BUSTER_TIER1_WIDE_TEST_BUFFER) == 0, "the interpreter's buffer is the unsplit function's");
        if (!ok)
        {
            printf("test-tier1-wide: %s: %s, input %u\n", name, stage, input);
        }
    }
    return ok;
}

// Whether two functions are the same, column for column.
static bool tier1_wide_test_same(const IrFunction* a, const IrFunction* b)
{
    const uint32_t n = a->instruction_count;
    bool same = n == b->instruction_count && a->block_count == b->block_count && a->extra_count == b->extra_count && a->constant_count == b->constant_count && a->address_count == b->address_count;
    same = same && memcmp(a->op, b->op, n) == 0 && memcmp(a->type, b->type, n) == 0 && memcmp(a->flags, b->flags, n) == 0 && memcmp(a->alignment_log2, b->alignment_log2, n) == 0;
    same = same && memcmp(a->a, b->a, (uint64_t)n * 4) == 0 && memcmp(a->b, b->b, (uint64_t)n * 4) == 0 && memcmp(a->c, b->c, (uint64_t)n * 4) == 0;
    same = same && memcmp(a->block_offsets, b->block_offsets, ((uint64_t)a->block_count + 1) * 4) == 0 && (a->extra_count == 0 || memcmp(a->extra, b->extra, (uint64_t)a->extra_count * 4) == 0);
    same = same && (a->constant_count == 0 || memcmp(a->constants, b->constants, (uint64_t)a->constant_count * sizeof(IrConstant)) == 0);
    same = same && (a->address_count == 0 || memcmp(a->addresses, b->addresses, (uint64_t)a->address_count * sizeof(IrAddress)) == 0);
    return same;
}

typedef void (*Tier1WideTestEntry)(uint8_t*, uint64_t);

static __attribute__((no_sanitize("function"))) void tier1_wide_test_call(const uint8_t* code, uint8_t* buffer, uint64_t count)
{
    Tier1WideTestEntry entry;
    memcpy(&entry, &code, sizeof(entry));
    entry(buffer, count);
}

// The emitted function's records, sized, checked against the baseline's forms, encoded by both encoders (which must
// agree), in an executable mapping; MAP_FAILED, the failure reported, otherwise.
static uint8_t* tier1_wide_test_load(Tier1WideTest* test, Tier1EmitResult* emitted, uint64_t* mapped)
{
    Tier1SelectCode* code = &emitted->code;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    *mapped = (capacity + 4095) & ~UINT64_C(4095);
    uint8_t* machine = mmap(0, *mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* scalar = malloc(capacity);
    uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(uint32_t));
    bool ok = tier1_wide_test_expect(test, machine != MAP_FAILED && scalar != 0 && offsets != 0, "the encoders' buffers");
    ok = ok && tier1_wide_test_expect(test, emitted->reference_count == 0, "the function references nothing");
    if (ok)
    {
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ok = tier1_wide_test_expect(test, checked.error == BRANCH_SIZING_OK, "the function's branches validate");
    }
    if (ok)
    {
        const BranchSizing sized = size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets);
        ok = tier1_wide_test_expect(test, sized.error == BRANCH_SIZING_OK && x86_target_validate(test->baseline, code->records, code->count) == code->count, "the sized records obey the x86-64 baseline");
    }
    for (uint32_t r = 0; ok && test->print && r < code->count; r += 1)
    {
        printf("    ");
        x86_print_instruction(stdout, &code->records[r], 0);
        printf("\n");
    }
    if (ok)
    {
        const int64_t size = x86_encode(code->records, code->count, machine, capacity, 0, 0);
        const int64_t scalar_size = x86_encode_scalar(code->records, code->count, scalar, capacity, 0, 0);
        ok = tier1_wide_test_expect(test, size > 0 && size == scalar_size && memcmp(machine, scalar, (uint64_t)size) == 0, "both encoders make the same bytes");
        test->records += code->count;
    }
    ok = ok && tier1_wide_test_expect(test, mprotect(machine, *mapped, PROT_READ | PROT_EXEC) == 0, "make the code executable");
    free(scalar);
    free(offsets);
    if (!ok && machine != MAP_FAILED)
    {
        munmap(machine, *mapped);
    }
    return ok ? machine : MAP_FAILED;
}

// The optimized function by one of Tier 1's paths: planned, emitted, its allocation certified, and run natively on every
// input in a child process, which must leave the expected buffers.
static void tier1_wide_test_native(Tier1WideTest* test, const char* name, const IrFunction* function, const IrDominance* dominance, bool vector, bool rematerialize, const uint8_t* inputs, const uint64_t* counts,
                                   const uint8_t* expected)
{
    const uint64_t output_mark = test->output.length;
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selected = tier1_select_plan(function, dominance, &test->output, &test->scratch, vector, &plan, &node);
    bool ok = tier1_wide_test_expect(test, selected == TIER1_SELECT_OK, "Tier 1 selects the split function");
    if (!ok)
    {
        printf("test-tier1-wide: %s, %s path: selection error %u at node %u (%s)\n", name, vector ? "vector" : "scalar", (unsigned)selected, node, node < function->instruction_count ? ir_op_names[function->op[node]] : "-");
    }
    Tier1EmitDiagnostic diagnostic = { 0 };
    const Tier1EmitOptions options = {
        .diagnostic = &diagnostic,
        .certify = true,
        .register_mask = tier1_liveness_register_mask(),
        .vector = vector,
        .rematerialize = rematerialize,
    };
    Tier1EmitResult emitted = { 0 };
    if (ok)
    {
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->baseline, &test->output, &test->scratch, &emitted, &node);
        ok = tier1_wide_test_expect(test, error == TIER1_EMIT_OK, "Tier 1 emits the split function");
        if (!ok)
        {
            printf("test-tier1-wide: %s, %s path%s: emission error %u at node %u (cause %u, error %u)\n", name, vector ? "vector" : "scalar", rematerialize ? ", rematerializing" : "", (unsigned)error, node,
                   (unsigned)diagnostic.cause, diagnostic.error);
            if (test->failures <= 2)
            {
                ir_print(function, &test->scratch, stdout);
            }
        }
    }
    uint64_t mapped = 0;
    const bool print = test->print;
    test->print = print && vector && rematerialize;
    if (test->print && ok)
    {
        printf("test-tier1-wide: %s:\n", name);
    }
    uint8_t* machine = ok ? tier1_wide_test_load(test, &emitted, &mapped) : MAP_FAILED;
    test->print = print;
    ok = machine != MAP_FAILED;
    uint8_t* buffers = ok ? mmap(0, BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    ok = ok && tier1_wide_test_expect(test, buffers != MAP_FAILED, "the native runs' shared buffers");
    if (ok)
    {
        memcpy(buffers, inputs, BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS);
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(10);
            for (uint32_t input = 0; input < BUSTER_TIER1_WIDE_TEST_INPUTS; input += 1)
            {
                tier1_wide_test_call(machine, buffers + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER, counts[input]);
            }
            _exit(0);
        }
        ok = tier1_wide_test_expect(test, child > 0 && tier1_pipeline_wait(child), "the native code exits normally within its budget");
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_WIDE_TEST_INPUTS; input += 1)
        {
            const uint8_t* actual = buffers + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER;
            const uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER;
            const bool equal = memcmp(actual, wanted, BUSTER_TIER1_WIDE_TEST_BUFFER) == 0;
            tier1_wide_test_expect(test, equal, "the native buffer is the interpreter's");
            for (uint32_t byte = 0; !equal && byte < BUSTER_TIER1_WIDE_TEST_BUFFER && test->failures < 20; byte += 1)
            {
                if (actual[byte] != wanted[byte])
                {
                    printf("test-tier1-wide: %s, %s path, input %u: byte %u is %02x, the interpreter's %02x\n", name, vector ? "vector" : "scalar", input, byte, actual[byte], wanted[byte]);
                    byte = BUSTER_TIER1_WIDE_TEST_BUFFER;
                }
            }
            test->native_runs += 1;
        }
    }
    if (buffers != MAP_FAILED)
    {
        munmap(buffers, BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS);
    }
    if (machine != MAP_FAILED)
    {
        munmap(machine, mapped);
    }
    tier1_wide_test_expect(test, test->scratch.length == 0, "selection and emission return the scratch arena");
    arena_pop_to(&test->output, output_mark);
}

// One program through every stage (the file's comment). `multiplies` and `high_multiplies`, unless UINT32_MAX, are how
// many I64 muls and mulhus the optimized function must have.
static void tier1_wide_test_program(Tier1WideTest* test, const char* name, const char* text, uint32_t loop, uint32_t multiplies, uint32_t high_multiplies, bool constant_counts, bool wide)
{
    const uint64_t output_mark = test->output.length;
    IrFunction function;
    bool ok = tier1_wide_test_parse(test, name, text, &function);
    static uint8_t inputs[BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS];
    static uint8_t expected[BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS];
    uint64_t counts[BUSTER_TIER1_WIDE_TEST_INPUTS];
    for (uint32_t input = 0; ok && input < BUSTER_TIER1_WIDE_TEST_INPUTS; input += 1)
    {
        uint8_t* bytes = inputs + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER;
        for (uint32_t byte = 0; byte < BUSTER_TIER1_WIDE_TEST_BUFFER; byte += 1)
        {
            // Every fourth input all zeros or all ones, some with words of the extremes, the others random.
            const uint64_t random = tier1_wide_test_next(test);
            const uint32_t edge = (uint32_t)(random >> 40) % 8;
            bytes[byte] = input % 4 == 1 ? 0 : input % 4 == 2 ? 0xff : input % 4 == 3 && edge < 3 ? (edge == 0 ? 0 : edge == 1 ? 0xff : (byte % 16 == 15 ? 0x80 : 0)) : (uint8_t)random;
        }
        counts[input] = loop != 0 ? 1 + tier1_wide_test_next(test) % loop : 0;
        ok = tier1_wide_test_interpret(test, &function, bytes, counts[input], expected + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER);
    }
    IrWideResult vector = { 0 };
    IrWideResult scalar = { 0 };
    if (ok)
    {
        const IrCheck split = ir_wide_split(&function, &test->output, &test->scratch, &vector);
        const IrCheck twin = ir_wide_split_scalar(&function, &test->output, &test->scratch, &scalar);
        ok = tier1_wide_test_expect(test, split.error == IR_OK && twin.error == IR_OK && vector.split == wide && scalar.split == wide, "both paths split the program");
        if (!ok)
        {
            printf("test-tier1-wide: %s: split error %u, scalar %u, at %u\n", name, (unsigned)split.error, (unsigned)twin.error, split.instruction);
        }
        ok = ok && tier1_wide_test_expect(test, tier1_wide_test_same(&vector.function, &scalar.function) && vector.wide == scalar.wide && (vector.wide != 0) == wide, "both paths split it into the same function");
        tier1_wide_test_expect(test, test->scratch.length == 0, "the split returns the scratch arena");
    }
    IrDominance dominance = { 0 };
    if (ok)
    {
        bool narrow = true;
        for (uint32_t i = 0; i < vector.function.instruction_count; i += 1)
        {
            narrow &= vector.function.type[i] != IR_I128;
        }
        ok = tier1_wide_test_expect(test, narrow, "the split leaves no I128 instruction");
        ok = ok && tier1_wide_test_dominance(test, &vector.function, &dominance);
        const IrCheck verified = ok ? ir_verify(&vector.function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
        const IrCheck twin = ok ? ir_verify_scalar(&vector.function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
        ok = ok && tier1_wide_test_expect(test, verified.error == IR_OK && twin.error == IR_OK, "both verifiers take the split function with its dominance");
        if (!ok)
        {
            printf("test-tier1-wide: %s: verifier error %u at %u (%u)\n", name, (unsigned)verified.error, verified.instruction, verified.detail);
        }
        ok = ok && tier1_wide_test_agrees(test, name, "the split function", &vector.function, inputs, counts, expected);
        test->wide += vector.wide;
        test->halves += vector.function.instruction_count;
    }
    IrOptimizeResult optimized = { 0 };
    if (ok)
    {
        IrOptimizeOptions options = ir_optimize_options();
        const IrCheck check = ir_optimize(&vector.function, &test->output, &test->scratch, options, &optimized);
        ok = tier1_wide_test_expect(test, check.error == IR_OK, "the optimizer takes the split function");
        ok = ok && tier1_wide_test_agrees(test, name, "the optimized function", &optimized.function, inputs, counts, expected);
        uint32_t muls = 0;
        uint32_t highs = 0;
        uint32_t selects = 0;
        for (uint32_t i = 0; ok && i < optimized.function.instruction_count; i += 1)
        {
            muls += optimized.function.op[i] == IR_MUL;
            highs += optimized.function.op[i] == IR_INTRINSIC && optimized.function.c[i] == BUSTER_IR_INTRINSIC_MULHU;
            selects += optimized.function.op[i] == IR_SELECT;
        }
        tier1_wide_test_expect(test, !constant_counts || selects == 0, "a shift by a constant through its conversions leaves no select");
        tier1_wide_test_expect(test, multiplies == UINT32_MAX || muls == multiplies, "the optimized function's muls");
        tier1_wide_test_expect(test, high_multiplies == UINT32_MAX || highs == high_multiplies, "the optimized function's mulhus");
        ok = ok && tier1_wide_test_dominance(test, &optimized.function, &dominance);
    }
    for (uint32_t run = 0; ok && run < 4; run += 1)
    {
        tier1_wide_test_native(test, name, &optimized.function, &dominance, (run & 1) == 0, (run & 2) != 0, inputs, counts, expected);
    }
    test->programs += 1;
    tier1_wide_test_expect(test, test->scratch.length == 0, "every stage returns the scratch arena");
    arena_pop_to(&test->output, output_mark);
}

// A random straight-line program: eight 128-bit values loaded, then `operations` more, each an operation on earlier
// values, every value stored. Shifts by a variable count mask it to 0 to 127; extensions are of a truncation, to 8 to 64
// bits (Tier 1, as C's lowering, makes no truncation to an i1: an i1 comes from a comparison, extended in kind 8).
static uint32_t tier1_wide_test_generate(Tier1WideTest* test, char* text, uint32_t operations)
{
    uint32_t length = (uint32_t)snprintf(text, BUSTER_TIER1_WIDE_TEST_TEXT, "function (ptr) -> void {\nb0:\n  %%0 = param.ptr 0\n");
    uint32_t next = 1;
    uint32_t values[64];
    uint32_t value_count = 0;
    for (uint32_t k = 0; k < 8; k += 1)
    {
        if (k == 0)
        {
            length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = load.i128 %%0 align=16\n", next);
            next += 1;
        }
        else
        {
            length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = address.ptr %%0 offset=%u\n  %%%u = load.i128 %%%u align=8\n", next, k * 16, next + 1, next);
            next += 2;
        }
        values[value_count++] = next - 1;
    }
    static const char* const binary[] = { "add", "sub", "mul", "and", "or", "xor" };
    static const char* const shifts[] = { "shl", "lshr", "ashr" };
    static const char* const predicates[] = { "eq", "ne", "lt", "le", "gt", "ge", "ult", "ule", "ugt", "uge" };
    static const char* const narrow[] = { "i8", "i16", "i32", "i64" };
    for (uint32_t o = 0; o < operations && value_count < 40; o += 1)
    {
        const uint32_t x = values[tier1_wide_test_next(test) % value_count];
        const uint32_t y = values[tier1_wide_test_next(test) % value_count];
        const uint32_t kind = (uint32_t)(tier1_wide_test_next(test) % 9);
        int written = 0;
        if (kind < 3)
        {
            written = snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = %s.i128 %%%u, %%%u\n", next, binary[tier1_wide_test_next(test) % 6], x, y);
            next += 1;
        }
        else if (kind == 3)
        {
            written = snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = %s.i128 %%%u\n", next, (tier1_wide_test_next(test) & 1) != 0 ? "not" : "neg", x);
            next += 1;
        }
        else if (kind == 4)
        {
            // By a constant 0 to 127.
            written = snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = const.i128 0x%x\n  %%%u = %s.i128 %%%u, %%%u\n", next, (unsigned)(tier1_wide_test_next(test) % 128), next + 1,
                               shifts[tier1_wide_test_next(test) % 3], x, next);
            next += 2;
        }
        else if (kind == 5)
        {
            // By a variable count, y's low 7 bits.
            written = snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = const.i128 0x7f\n  %%%u = and.i128 %%%u, %%%u\n  %%%u = %s.i128 %%%u, %%%u\n", next, next + 1, y, next, next + 2,
                               shifts[tier1_wide_test_next(test) % 3], x, next + 1);
            next += 3;
        }
        else if (kind == 6)
        {
            written = snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = icmp.i1 %%%u, %%%u %s\n  %%%u = select.i128 %%%u, %%%u, %%%u\n", next, x, y, predicates[tier1_wide_test_next(test) % 10], next + 1, next, x,
                               y);
            next += 2;
        }
        else if (kind == 7)
        {
            const char* type = narrow[tier1_wide_test_next(test) % 4];
            written = snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = trunc.%s %%%u\n  %%%u = %s.i128 %%%u\n", next, type, x, next + 1, (tier1_wide_test_next(test) & 1) != 0 ? "zext" : "sext", next);
            next += 2;
        }
        else
        {
            // A comparison's truth value as a 128-bit integer.
            written = snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = icmp.i1 %%%u, %%%u %s\n  %%%u = zext.i128 %%%u\n", next, x, y, predicates[tier1_wide_test_next(test) % 10], next + 1, next);
            next += 2;
        }
        length += (uint32_t)written;
        values[value_count++] = next - 1;
    }
    for (uint32_t k = 8; k < value_count && k < 8 + BUSTER_TIER1_WIDE_TEST_HALF / 16; k += 1)
    {
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = address.ptr %%0 offset=%u\n  %%%u = store.void %%%u, %%%u align=16\n", next, BUSTER_TIER1_WIDE_TEST_HALF + (k - 8) * 16, next + 1,
                                     next, values[k]);
        next += 2;
    }
    length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = ret.void\n}\n", next);
    return length;
}

typedef void (*Tier1WideTestTyped)(const void*, const void*, void*);

static __attribute__((no_sanitize("function"))) void tier1_wide_test_typed_call(const uint8_t* code, const void* in, const void* ints, void* out)
{
    Tier1WideTestTyped entry;
    memcpy(&entry, &code, sizeof(entry));
    entry(in, ints, out);
}

// Tier 0's random programs of the 128-bit integers (tier0_int128_test.c's generator, as the typed stream gives them: frame
// objects, ?:'s blocks, conversions both ways, shifts by constant and variable counts) through the C driver's --tier1
// steps: lowered to the IR for the baseline, promoted, split by both paths into the same function, optimized, then
// planned and emitted by both of Tier 1's paths and run natively on random inputs, every output the generator's stack
// machine's (tier0_test_int128_interpret). Each stage after promotion is run by the IR's interpreter too. Returns how
// many programs went through every stage.
static uint32_t tier1_wide_test_typed(Tier1WideTest* test, uint32_t count)
{
    Tier0Tables tables = { 0 };
    Tier0TestInt128Abi abi;
    Tier0TestInt128Program* wide = calloc(1, sizeof(Tier0TestInt128Program));
    const bool ready = wide != 0 && tier0_tables_open(&tables) && tier0_test_int128_abi(&tables, &abi);
    tier1_wide_test_expect(test, ready, "the typed programs' generator");
    uint32_t passed = 0;
    for (uint32_t p = 0; ready && p < count; p += 1)
    {
        const uint64_t output_mark = test->output.length;
        uint64_t state = test->random ^ (UINT64_C(0x128) << 32) ^ p;
        tier0_test_int128_program(wide, &state);
        const Tier0Function source = tier0_test_int128_function(wide, &abi);
        IrFunction lowered = { 0 };
        const IrCheck lowering = wide->overflow ? ir_check(IR_ERROR_LIMIT, 0, 0) : ir_lower_typed_target(&source, IR_VOID, 1, 0, &test->output, &test->scratch, &lowered);
        bool ok = wide->overflow || tier1_wide_test_expect(test, lowering.error == IR_OK, "the typed program lowers");
        ok = ok && !wide->overflow;
        IrFunction promoted = { 0 };
        if (ok)
        {
            const IrCheck promotion = ssa_ir_promote(&lowered, &test->promoted, &test->scratch, &promoted);
            ok = tier1_wide_test_expect(test, promotion.error == IR_OK, "the typed program promotes");
            if (!ok && test->failures < 3)
            {
                printf("test-tier1-wide: typed program %u: promotion error %u at %u (%u)\n", p, (unsigned)promotion.error, promotion.instruction, promotion.detail);
            }
        }
        IrWideResult vector = { 0 };
        IrWideResult scalar = { 0 };
        if (ok)
        {
            const IrCheck split = ir_wide_split(&promoted, &test->output, &test->scratch, &vector);
            const IrCheck twin = ir_wide_split_scalar(&promoted, &test->output, &test->scratch, &scalar);
            ok = tier1_wide_test_expect(test, split.error == IR_OK && twin.error == IR_OK && vector.split == scalar.split, "both paths split the typed program");
            if (!ok)
            {
                printf("test-tier1-wide: typed program %u: split error %u (scalar %u) at %u (%s)\n", p, (unsigned)split.error, (unsigned)twin.error, split.instruction,
                       split.instruction < promoted.instruction_count ? ir_op_names[promoted.op[split.instruction]] : "-");
            }
            ok = ok && tier1_wide_test_expect(test, tier1_wide_test_same(&vector.function, &scalar.function), "both paths split the typed program into the same function");
            test->wide += vector.wide;
            test->halves += vector.split ? vector.function.instruction_count : 0;
        }
        static Tier0TestWide in[BUSTER_TIER0_TEST_INT128_IN];
        static uint64_t ints[BUSTER_TIER0_TEST_INT128_INTS];
        static uint8_t expected[BUSTER_TIER0_TEST_INT128_OUTPUTS * 16];
        static uint8_t actual[sizeof(expected)];
        for (uint32_t k = 0; ok && k < BUSTER_TIER0_TEST_INT128_IN; k += 1)
        {
            in[k] = tier0_test_int128_random(&state);
        }
        for (uint32_t k = 0; ok && k < BUSTER_TIER0_TEST_INT128_INTS; k += 1)
        {
            ints[k] = tier0_test_int128_random64(&state);
        }
        if (ok)
        {
            memset(expected, 0xa5, sizeof(expected));
            const uint64_t parameters[3] = { (uintptr_t)in, (uintptr_t)ints, (uintptr_t)expected };
            static uint8_t visited[BUSTER_TIER0_TEST_INT128_ENTRIES];
            ok = tier1_wide_test_expect(test, tier0_test_int128_interpret(wide, parameters, visited), "the stack machine runs the typed program");
        }
        IrOptimizeResult optimized = { 0 };
        if (ok)
        {
            const IrCheck check = ir_optimize(&vector.function, &test->optimized, &test->scratch, ir_optimize_options(), &optimized);
            ok = tier1_wide_test_expect(test, check.error == IR_OK, "the optimizer takes the split typed program");
        }
        // The IR's interpreter on the split function and on the optimized one.
        const IrFunction* stages[2] = { &vector.function, &optimized.function };
        for (uint32_t stage = 0; ok && stage < 2; stage += 1)
        {
            memset(actual, 0xa5, sizeof(actual));
            uint8_t* buffers[3] = { (uint8_t*)in, (uint8_t*)ints, actual };
            const uint64_t sizes[3] = { sizeof(in), sizeof(ints), sizeof(actual) };
            const bool ran = ir_lower_test_wide_run(stages[stage], buffers, sizes, 3, &test->execution);
            arena_pop_to(&test->execution, 0);
            test->interpreted += 1;
            bool same = ran;
            for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_INT128_OUTPUTS; o += 1)
            {
                same = memcmp(actual + 16 * o, expected + 16 * o, tier0_class_bytes(wide->outputs[o])) == 0;
            }
            ok = tier1_wide_test_expect(test, same, "the IR's interpreter gives the stack machine's outputs after the split and the optimizer");
            if (!ok)
            {
                printf("test-tier1-wide: typed program %u, %s: %s\n", p, stage == 0 ? "split" : "optimized", ran ? "the outputs differ" : "the interpreter refused");
            }
        }
        IrDominance dominance = { 0 };
        ok = ok && tier1_wide_test_dominance(test, &optimized.function, &dominance);
        for (uint32_t path = 0; ok && path < 2; path += 1)
        {
            const uint64_t mark = test->output.length;
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const Tier1SelectError selected = tier1_select_plan(&optimized.function, &dominance, &test->output, &test->scratch, path == 0, &plan, &node);
            ok = tier1_wide_test_expect(test, selected == TIER1_SELECT_OK, "Tier 1 selects the typed program");
            Tier1EmitDiagnostic diagnostic = { 0 };
            const Tier1EmitOptions options = {
                .diagnostic = &diagnostic,
                .certify = true,
                .register_mask = tier1_liveness_register_mask(),
                .vector = path == 0,
                .rematerialize = true,
            };
            Tier1EmitResult emitted = { 0 };
            const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->baseline, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_OK;
            ok = ok && tier1_wide_test_expect(test, error == TIER1_EMIT_OK, "Tier 1 emits the typed program");
            if (!ok)
            {
                printf("test-tier1-wide: typed program %u, %s path: selection %u, emission %u (cause %u, error %u) at node %u (%s)\n", p, path == 0 ? "vector" : "scalar", (unsigned)selected, (unsigned)error,
                       (unsigned)diagnostic.cause, diagnostic.error, node, node < optimized.function.instruction_count ? ir_op_names[optimized.function.op[node]] : "-");
            }
            uint64_t mapped = 0;
            uint8_t* machine = ok ? tier1_wide_test_load(test, &emitted, &mapped) : MAP_FAILED;
            ok = machine != MAP_FAILED;
            uint8_t* shared = ok ? mmap(0, sizeof(actual), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
            ok = ok && tier1_wide_test_expect(test, shared != MAP_FAILED, "the typed program's shared outputs");
            if (ok)
            {
                memset(shared, 0xa5, sizeof(actual));
                fflush(0);
                const pid_t child = fork();
                if (child == 0)
                {
                    alarm(10);
                    tier1_wide_test_typed_call(machine, in, ints, shared);
                    _exit(0);
                }
                ok = tier1_wide_test_expect(test, child > 0 && tier1_pipeline_wait(child), "the typed program's native code exits normally");
                bool same = ok;
                for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_INT128_OUTPUTS; o += 1)
                {
                    same = memcmp(shared + 16 * o, expected + 16 * o, tier0_class_bytes(wide->outputs[o])) == 0;
                    if (!same)
                    {
                        printf("test-tier1-wide: typed program %u, %s path: output %u differs\n", p, path == 0 ? "vector" : "scalar", o);
                    }
                }
                ok = tier1_wide_test_expect(test, same, "the typed program's native outputs are the stack machine's");
                test->native_runs += 1;
            }
            if (shared != MAP_FAILED)
            {
                munmap(shared, sizeof(actual));
            }
            if (machine != MAP_FAILED)
            {
                munmap(machine, mapped);
            }
            arena_pop_to(&test->output, mark);
        }
        passed += ok;
        tier1_wide_test_expect(test, test->scratch.length == 0, "the typed pipeline returns the scratch arena");
        arena_pop_to(&test->output, output_mark);
        arena_pop_to(&test->promoted, 0);
        arena_pop_to(&test->optimized, 0);
    }
    if (ready)
    {
        tier0_tables_close(&tables);
    }
    free(wide);
    return passed;
}

typedef void (*Tier1WideTestX87)(const void*, const void*, const void*, void*);

static __attribute__((no_sanitize("function"))) void tier1_wide_test_x87_call(const uint8_t* code, const void* in, const void* ints, const void* floats, void* out)
{
    Tier1WideTestX87 entry;
    memcpy(&entry, &code, sizeof(entry));
    entry(in, ints, floats, out);
}

// Tier 0's random programs of long double (tier0_x87_test.c's generator, #141: constants, arithmetic, comparisons,
// conversions both ways between floats, doubles and integers of every class, frame objects, ?:'s blocks, and calls of C
// functions through their addresses, whose arguments and results are long doubles) through the C driver's --tier1 steps:
// lowered for the baseline, promoted, split (which takes them as they are), optimized, then planned and emitted by both of
// Tier 1's paths, their allocations certified, and run natively, every output the generator's stack machine's
// (tier0_test_x87_interpret), NaNs alike. The IR's interpreter runs the optimized function too. Returns how many programs
// went through every stage.
static uint32_t tier1_wide_test_x87(Tier1WideTest* test, uint32_t count)
{
    Tier0Tables tables = { 0 };
    Tier0TestX87Abi abi;
    Tier0TestX87Program* program = calloc(1, sizeof(Tier0TestX87Program));
    const bool ready = program != 0 && tier0_tables_open(&tables) && tier0_test_x87_abi(&tables, &abi);
    tier1_wide_test_expect(test, ready, "the long double programs' generator");
    uint32_t passed = 0;
    for (uint32_t p = 0; ready && p < count; p += 1)
    {
        const uint64_t output_mark = test->output.length;
        uint64_t state = test->random ^ (UINT64_C(0x87) << 32) ^ p;
        tier0_test_x87_program(program, &abi, &state);
        const Tier0Function source = tier0_test_x87_function(program, &abi);
        IrFunction lowered = { 0 };
        const IrCheck lowering = program->overflow ? ir_check(IR_ERROR_LIMIT, 0, 0) : ir_lower_typed_target(&source, IR_VOID, 1, 0, &test->output, &test->scratch, &lowered);
        bool ok = !program->overflow && tier1_wide_test_expect(test, lowering.error == IR_OK, "the long double program lowers");
        IrFunction promoted = { 0 };
        if (ok)
        {
            const IrCheck promotion = ssa_ir_promote(&lowered, &test->promoted, &test->scratch, &promoted);
            ok = tier1_wide_test_expect(test, promotion.error == IR_OK, "the long double program promotes");
        }
        IrWideResult split = { 0 };
        if (ok)
        {
            const IrCheck check = ir_wide_split(&promoted, &test->output, &test->scratch, &split);
            ok = tier1_wide_test_expect(test, check.error == IR_OK && !split.split, "the split takes a long double program as it is");
        }
        IrOptimizeResult optimized = { 0 };
        if (ok)
        {
            const IrCheck check = ir_optimize(&split.function, &test->optimized, &test->scratch, ir_optimize_options(), &optimized);
            ok = tier1_wide_test_expect(test, check.error == IR_OK, "the optimizer takes the long double program");
        }
        static long double in[BUSTER_TIER0_TEST_X87_IN];
        static uint64_t ints[BUSTER_TIER0_TEST_X87_INTS];
        static uint64_t floats[BUSTER_TIER0_TEST_X87_FLOATS];
        static uint8_t expected[BUSTER_TIER0_TEST_X87_OUTPUTS * 16];
        static uint8_t actual[sizeof(expected)];
        memset(in, 0, sizeof(in));
        for (uint32_t k = 0; ok && k < BUSTER_TIER0_TEST_X87_IN; k += 1)
        {
            in[k] = tier0_test_x87_random(&state, true);
        }
        for (uint32_t k = 0; ok && k < BUSTER_TIER0_TEST_X87_INTS; k += 1)
        {
            const uint64_t random = tier0_generate_random(&state);
            ints[k] = k < 4 ? random % 9 : random;
        }
        for (uint32_t k = 0; ok && k < BUSTER_TIER0_TEST_X87_FLOATS; k += 1)
        {
            const double d = (double)(int64_t)tier0_generate_random(&state) / 4096.0;
            const float f = (float)d;
            uint32_t f_bits = 0;
            memcpy(&f_bits, &f, sizeof(f_bits));
            floats[k] = 0;
            memcpy(&floats[k], k >= 8 ? (const void*)&f_bits : (const void*)&d, k >= 8 ? 4 : 8);
        }
        if (ok)
        {
            memset(expected, 0xa5, sizeof(expected));
            const uint64_t parameters[4] = { (uintptr_t)in, (uintptr_t)ints, (uintptr_t)floats, (uintptr_t)expected };
            static uint8_t visited[BUSTER_TIER0_TEST_X87_ENTRIES];
            ok = tier1_wide_test_expect(test, tier0_test_x87_interpret(program, parameters, visited), "the stack machine runs the long double program");
        }
        if (ok)
        {
            memset(actual, 0xa5, sizeof(actual));
            uint8_t* buffers[4] = { (uint8_t*)in, (uint8_t*)ints, (uint8_t*)floats, actual };
            const uint64_t sizes[4] = { sizeof(in), sizeof(ints), sizeof(floats), sizeof(actual) };
            const bool ran = ir_lower_test_wide_run(&optimized.function, buffers, sizes, 4, &test->execution);
            arena_pop_to(&test->execution, 0);
            test->interpreted += 1;
            bool same = ran;
            for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_X87_OUTPUTS; o += 1)
            {
                same = tier0_test_x87_same_output(program->outputs[o], actual + 16 * o, expected + 16 * o);
            }
            ok = tier1_wide_test_expect(test, same, "the IR's interpreter gives the stack machine's outputs after the optimizer");
        }
        IrDominance dominance = { 0 };
        ok = ok && tier1_wide_test_dominance(test, &optimized.function, &dominance);
        for (uint32_t path = 0; ok && path < 2; path += 1)
        {
            const uint64_t mark = test->output.length;
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const Tier1SelectError selected = tier1_select_plan(&optimized.function, &dominance, &test->output, &test->scratch, path == 0, &plan, &node);
            ok = tier1_wide_test_expect(test, selected == TIER1_SELECT_OK, "Tier 1 selects the long double program");
            Tier1EmitDiagnostic diagnostic = { 0 };
            const Tier1EmitOptions options = {
                .diagnostic = &diagnostic,
                .certify = true,
                .register_mask = tier1_liveness_register_mask(),
                .vector = path == 0,
                .rematerialize = true,
            };
            Tier1EmitResult emitted = { 0 };
            const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->baseline, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_OK;
            ok = ok && tier1_wide_test_expect(test, error == TIER1_EMIT_OK, "Tier 1 emits the long double program");
            if (!ok)
            {
                printf("test-tier1-wide: long double program %u, %s path: selection %u, emission %u (cause %u, error %u) at node %u (%s)\n", p, path == 0 ? "vector" : "scalar", (unsigned)selected, (unsigned)error,
                       (unsigned)diagnostic.cause, diagnostic.error, node, node < optimized.function.instruction_count ? ir_op_names[optimized.function.op[node]] : "-");
            }
            uint64_t mapped = 0;
            uint8_t* machine = ok ? tier1_wide_test_load(test, &emitted, &mapped) : MAP_FAILED;
            ok = machine != MAP_FAILED;
            uint8_t* shared = ok ? mmap(0, sizeof(actual), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
            ok = ok && tier1_wide_test_expect(test, shared != MAP_FAILED, "the long double program's shared outputs");
            if (ok)
            {
                memset(shared, 0xa5, sizeof(actual));
                fflush(0);
                const pid_t child = fork();
                if (child == 0)
                {
                    alarm(10);
                    tier1_wide_test_x87_call(machine, in, ints, floats, shared);
                    _exit(0);
                }
                ok = tier1_wide_test_expect(test, child > 0 && tier1_pipeline_wait(child), "the long double program's native code exits normally");
                bool same = ok;
                for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_X87_OUTPUTS; o += 1)
                {
                    same = tier0_test_x87_same_output(program->outputs[o], shared + 16 * o, expected + 16 * o);
                    if (!same)
                    {
                        printf("test-tier1-wide: long double program %u, %s path: output %u (class %u) differs\n", p, path == 0 ? "vector" : "scalar", o, program->outputs[o]);
                    }
                }
                ok = tier1_wide_test_expect(test, same, "the long double program's native outputs are the stack machine's");
                test->native_runs += 1;
            }
            if (shared != MAP_FAILED)
            {
                munmap(shared, sizeof(actual));
            }
            if (machine != MAP_FAILED)
            {
                munmap(machine, mapped);
            }
            arena_pop_to(&test->output, mark);
        }
        passed += ok;
        tier1_wide_test_expect(test, test->scratch.length == 0, "the long double pipeline returns the scratch arena");
        arena_pop_to(&test->output, output_mark);
        arena_pop_to(&test->promoted, 0);
        arena_pop_to(&test->optimized, 0);
    }
    if (ready)
    {
        tier0_tables_close(&tables);
    }
    free(program);
    return passed;
}

// Long double phis rotated around a loop (#141's review): `n` values, the buffer's first n words as long doubles, each
// phi taking its neighbour's value on the back edge, so that both of the loop's incoming edges copy n long doubles at
// once through the x87 stack (tier1_emit_cfg_x87_phis); the phis stored back as doubles. The x87 stack holds 8.
static void tier1_wide_test_x87_phi_text(char* text, uint32_t n)
{
    uint32_t length = (uint32_t)snprintf(text, BUSTER_TIER1_WIDE_TEST_TEXT, "function (ptr, i64) -> void {\nb0:\n  %%0 = param.ptr 0\n  %%1 = param.i64 1\n");
    const uint32_t values = 2;
    for (uint32_t k = 0; k < n; k += 1)
    {
        const uint32_t at = values + 3 * k;
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = address.ptr %%0 offset=%u\n  %%%u = load.i64 %%%u align=8\n  %%%u = sitofp.f80 %%%u\n", at, 8 * k,
                                     at + 1, at, at + 2, at + 1);
    }
    const uint32_t zero = values + 3 * n;
    const uint32_t phis = zero + 2;
    const uint32_t counter = phis + n;
    length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = const.i64 0x0\n  %%%u = br.void b1\nb1:\n", zero, zero + 1);
    for (uint32_t k = 0; k < n; k += 1)
    {
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = phi.f80 [%%%u, b0], [%%%u, b1]\n", phis + k, values + 3 * k + 2, phis + (k + 1) % n);
    }
    length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length,
                                 "  %%%u = phi.i64 [%%%u, b0], [%%%u, b1]\n  %%%u = const.i64 0x1\n  %%%u = add.i64 %%%u, %%%u\n  %%%u = icmp.i1 %%%u, %%1 ult\n  %%%u = cbr.void %%%u, b1, b2\nb2:\n", counter,
                                 zero, counter + 2, counter + 1, counter + 2, counter, counter + 1, counter + 3, counter + 2, counter + 4, counter + 3);
    const uint32_t stores = counter + 5;
    for (uint32_t k = 0; k < n; k += 1)
    {
        const uint32_t at = stores + 3 * k;
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = ftrunc.f64 %%%u\n  %%%u = address.ptr %%0 offset=%u\n  %%%u = store.void %%%u, %%%u align=8\n", at, phis + k,
                                     at + 1, 256 + 8 * k, at + 2, at + 1, at);
    }
    snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = ret.void\n}\n", stores + 3 * n);
}

// A join of `n` predecessors with eight long double phis (#1204's review): each edge's copies (tier1_emit_cfg_x87_phis,
// an fld and an fstp per phi) are records past the phis' own, which the records' capacity must count. b0 converts the
// buffer's first eight words to long doubles; blocks 1 to n go to the join, every one but the last only where the count
// is below 2, the last always; phi j takes word (j + k) % 8 from block k, so the first edge and the last copy different
// values; the phis are stored back as doubles.
static void tier1_wide_test_x87_join_text(char* text, uint32_t n)
{
    uint32_t length = (uint32_t)snprintf(text, BUSTER_TIER1_WIDE_TEST_TEXT, "function (ptr, i64) -> void {\nb0:\n  %%0 = param.ptr 0\n  %%1 = param.i64 1\n");
    for (uint32_t j = 0; j < 8; j += 1)
    {
        const uint32_t at = 2 + 3 * j;
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = address.ptr %%0 offset=%u\n  %%%u = load.i64 %%%u align=8\n  %%%u = sitofp.f80 %%%u\n", at, 8 * j, at + 1, at,
                                     at + 2, at + 1);
    }
    length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%26 = const.i64 0x2\n  %%27 = icmp.i1 %%1, %%26 ult\n  %%28 = br.void b1\n");
    const uint32_t join = n + 1;
    for (uint32_t k = 1; k <= n; k += 1)
    {
        const uint32_t at = 28 + k;
        length += k < n ? (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "b%u:\n  %%%u = cbr.void %%27, b%u, b%u\n", k, at, join, k + 1) :
                          (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "b%u:\n  %%%u = br.void b%u\n", k, at, join);
    }
    const uint32_t phis = 29 + n;
    length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "b%u:\n", join);
    for (uint32_t j = 0; j < 8; j += 1)
    {
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = phi.f80", phis + j);
        for (uint32_t k = 1; k <= n; k += 1)
        {
            length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "%s [%%%u, b%u]", k == 1 ? "" : ",", 4 + 3 * ((j + k) % 8), k);
        }
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "\n");
    }
    const uint32_t stores = phis + 8;
    for (uint32_t j = 0; j < 8; j += 1)
    {
        const uint32_t at = stores + 3 * j;
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = ftrunc.f64 %%%u\n  %%%u = address.ptr %%0 offset=%u\n  %%%u = store.void %%%u, %%%u align=8\n", at, phis + j,
                                     at + 1, 256 + 8 * j, at + 2, at + 1, at);
    }
    snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = ret.void\n}\n", stores + 24);
}

// `n` conversions of one long double to an unsigned 64-bit integer in one block (#1204's review), each 20 records
// (tier1_emit_cfg_x87), past a node's share; their sum stored, so that none is dead. The long double is the buffer's
// first word, converted unsigned, so that every conversion back is exact and defined.
static void tier1_wide_test_x87_conversions_text(char* text, uint32_t n)
{
    uint32_t length = (uint32_t)snprintf(text, BUSTER_TIER1_WIDE_TEST_TEXT, "function (ptr) -> void {\nb0:\n  %%0 = param.ptr 0\n  %%1 = load.i64 %%0 align=8\n  %%2 = uitofp.f80 %%1\n  %%3 = fptoui.i64 %%2\n");
    uint32_t sum = 3;
    uint32_t next = 4;
    for (uint32_t k = 1; k < n; k += 1)
    {
        length += (uint32_t)snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = fptoui.i64 %%2\n  %%%u = add.i64 %%%u, %%%u\n", next, next + 1, sum, next);
        sum = next + 1;
        next += 2;
    }
    snprintf(text + length, BUSTER_TIER1_WIDE_TEST_TEXT - length, "  %%%u = address.ptr %%0 offset=256\n  %%%u = store.void %%%u, %%%u align=8\n  %%%u = ret.void\n}\n", next, next + 1, next, sum, next + 2);
}

// The x87 stack's depth on an edge (#141's review): eight long double phis rotated, every edge's copy of all eight
// through the stack, run natively against the interpreter as any program; and nine, which the CFG consumer refuses as
// unsupported (an edge's copy past the stack's depth) on both paths rather than emit.
static void tier1_wide_test_x87_phis(Tier1WideTest* test, char* text)
{
    tier1_wide_test_x87_phi_text(text, 8);
    tier1_wide_test_program(test, "eight long double phis rotated", text, 40, UINT32_MAX, UINT32_MAX, false, false);
    // And a join of 200 predecessors with eight long double phis: 3,200 copies' records, run natively as any program.
    tier1_wide_test_x87_join_text(text, 200);
    tier1_wide_test_program(test, "eight long double phis at a join of 200 predecessors", text, 2, UINT32_MAX, UINT32_MAX, false, false);
    // And 300 unsigned 64-bit conversions of a long double in one block: 6,000 records of their own.
    tier1_wide_test_x87_conversions_text(text, 300);
    tier1_wide_test_program(test, "300 conversions of a long double to an unsigned 64-bit integer", text, 0, UINT32_MAX, UINT32_MAX, false, false);
    const uint64_t output_mark = test->output.length;
    tier1_wide_test_x87_phi_text(text, 9);
    IrFunction function;
    IrDominance dominance = { 0 };
    const bool parsed = tier1_wide_test_parse(test, "nine long double phis rotated", text, &function) && tier1_wide_test_dominance(test, &function, &dominance);
    for (uint32_t path = 0; parsed && path < 2; path += 1)
    {
        const uint64_t mark = test->output.length;
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        const Tier1SelectError selected = tier1_select_plan(&function, &dominance, &test->output, &test->scratch, path == 0, &plan, &node);
        Tier1EmitDiagnostic diagnostic = { 0 };
        const Tier1EmitOptions options = {
            .diagnostic = &diagnostic,
            .certify = true,
            .register_mask = tier1_liveness_register_mask(),
            .vector = path == 0,
        };
        Tier1EmitResult emitted = { 0 };
        const Tier1EmitError error = selected == TIER1_SELECT_OK ? tier1_emit_function(&plan, &options, test->baseline, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
        tier1_wide_test_expect(test, selected == TIER1_SELECT_OK && error == TIER1_EMIT_UNSUPPORTED, "an edge's copy of nine long doubles, past the x87 stack, is refused as unsupported");
        tier1_wide_test_expect(test, test->scratch.length == 0, "the refusal returns the scratch arena");
        arena_pop_to(&test->output, mark);
    }
    arena_pop_to(&test->output, output_mark);
}

// Functions whose extra slices overlap, as the verifier lets them (#1204's review): a call's arguments sharing a phi's
// first word, its predecessor block, which is also the argument's value. The split remaps phis' values and calls'
// arguments into slices of their own; a remap in the source's place made one reader's rewrite the other's (the phi's
// predecessor became the argument's remapped value). One case's phi is an i64 the split copies, the other's an I128 the
// split halves. Each must verify as built, and split by both paths into a function that both verifiers take and that
// the interpreter runs to the source's buffers; the call is on a branch never taken, the interpreter runs no callee.
static const char* const tier1_wide_test_shared[2] = {
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.i128 %0 align=16\n"
    "  %2 = trunc.i64 %1\n"
    "  %3 = br.void b1\n"
    "b1:\n"
    "  %4 = br.void b2\n"
    "b2:\n"
    "  %5 = br.void b3\n"
    "b3:\n"
    "  %6 = phi.i64 [%2, b2]\n"
    "  %7 = const.i1 0x0\n"
    "  %8 = cbr.void %7, b4, b5\n"
    "b4:\n"
    "  %9 = call.void @1(%2) fixed=(i64) variadic=0\n"
    "  %10 = br.void b5\n"
    "b5:\n"
    "  %11 = address.ptr %0 offset=256\n"
    "  %12 = store.void %11, %6 align=8\n"
    "  %13 = ret.void\n"
    "}\n",
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.i128 %0 align=16\n"
    "  %2 = const.i64 0x5\n"
    "  %3 = br.void b1\n"
    "b1:\n"
    "  %4 = br.void b2\n"
    "b2:\n"
    "  %5 = br.void b3\n"
    "b3:\n"
    "  %6 = phi.i128 [%1, b2]\n"
    "  %7 = const.i1 0x0\n"
    "  %8 = cbr.void %7, b4, b5\n"
    "b4:\n"
    "  %9 = call.void @1(%2) fixed=(i64) variadic=0\n"
    "  %10 = br.void b5\n"
    "b5:\n"
    "  %11 = address.ptr %0 offset=256\n"
    "  %12 = store.void %11, %6 align=16\n"
    "  %13 = ret.void\n"
    "}\n",
};

static void tier1_wide_test_shared_slices(Tier1WideTest* test)
{
    static uint8_t inputs[BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS];
    static uint8_t expected[BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS];
    const uint64_t counts[BUSTER_TIER1_WIDE_TEST_INPUTS] = { 0 };
    for (uint32_t shared = 0; shared < 2; shared += 1)
    {
        const uint64_t output_mark = test->output.length;
        const char* name = shared == 0 ? "a call's arguments sharing an i64 phi's list" : "a call's arguments sharing an I128 phi's list";
        IrFunction function;
        IrDominance dominance = { 0 };
        bool ok = tier1_wide_test_parse(test, name, tier1_wide_test_shared[shared], &function);
        // %6 is the phi, %9 the call: the call's arguments start at the phi's first word, its predecessor b2, which is
        // also the argument's value, %2.
        ok = ok && tier1_wide_test_expect(test, function.op[6] == IR_PHI && function.op[9] == IR_CALL && function.extra[function.a[6]] == 2, "the shared slices' fixture parses as written");
        if (ok)
        {
            function.calls[function.b[9]].argument_first = function.a[6];
        }
        ok = ok && tier1_wide_test_dominance(test, &function, &dominance);
        const IrCheck verified = ok ? ir_verify(&function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
        const IrCheck twin = ok ? ir_verify_scalar(&function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
        ok = ok && tier1_wide_test_expect(test, verified.error == IR_OK && twin.error == IR_OK, "both verifiers take a call's arguments sharing a phi's words");
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_WIDE_TEST_INPUTS; input += 1)
        {
            uint8_t* bytes = inputs + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER;
            for (uint32_t byte = 0; byte < BUSTER_TIER1_WIDE_TEST_BUFFER; byte += 1)
            {
                bytes[byte] = (uint8_t)tier1_wide_test_next(test);
            }
            ok = tier1_wide_test_interpret(test, &function, bytes, 0, expected + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER);
        }
        for (uint32_t path = 0; ok && path < 2; path += 1)
        {
            const uint64_t mark = test->output.length;
            IrWideResult result = { 0 };
            const IrCheck split = path == 0 ? ir_wide_split(&function, &test->output, &test->scratch, &result) : ir_wide_split_scalar(&function, &test->output, &test->scratch, &result);
            bool fine = tier1_wide_test_expect(test, split.error == IR_OK && result.split, "both paths split a function whose slices overlap");
            IrDominance split_dominance = { 0 };
            fine = fine && tier1_wide_test_dominance(test, &result.function, &split_dominance);
            const IrCheck checked = fine ? ir_verify(&result.function, &test->scratch, &split_dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
            const IrCheck scalar = fine ? ir_verify_scalar(&result.function, &test->scratch, &split_dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
            fine = fine && tier1_wide_test_expect(test, checked.error == IR_OK && scalar.error == IR_OK, "the split of overlapping slices verifies: each reader keeps its own entries");
            if (!fine)
            {
                printf("test-tier1-wide: %s, %s path: split error %u, verifier error %u at %u\n", name, path == 0 ? "vector" : "scalar", (unsigned)split.error, (unsigned)checked.error, checked.instruction);
            }
            fine = fine && tier1_wide_test_agrees(test, name, "the split function", &result.function, inputs, counts, expected);
            tier1_wide_test_expect(test, function.calls[function.b[9]].argument_first == function.a[6] && function.extra[function.a[6]] == 2, "the split leaves the source's shared slice as it was");
            arena_pop_to(&test->output, mark);
        }
        tier1_wide_test_expect(test, test->scratch.length == 0, "the shared slices' checks return the scratch arena");
        arena_pop_to(&test->output, output_mark);
    }
}

static void tier1_wide_test_set(IrFunction* function, uint32_t id, uint32_t op, uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    function->op[id] = (uint8_t)op;
    function->type[id] = (uint8_t)type;
    function->a[id] = a;
    function->b[id] = b;
    function->c[id] = c;
}

// A function of `phis` I128 phis that share one list of `predecessors` pairs, as the verifier lets them: b0 loads an
// I128 from its pointer; blocks 1 to `predecessors` each go to the join, every one but the last on a false constant,
// to the next block otherwise; the join's phis each take the load from every predecessor, and the last is stored back.
// Built in its columns: as text, each phi would spell its own list.
static bool tier1_wide_test_shared_phis(Tier1WideTest* test, uint32_t phis, uint32_t predecessors, IrFunction* function)
{
    const uint32_t none = BUSTER_IR_NONE;
    const uint32_t join = predecessors + 1;
    const uint32_t first_phi = 4 + predecessors;
    const IrSizes sizes = {
        .instruction_count = (uint64_t)first_phi + phis + 2,
        .block_count = (uint64_t)predecessors + 2,
        .extra_count = (uint64_t)predecessors * 2,
        .constant_count = 1,
        .parameter_count = 1,
    };
    const bool ok = tier1_wide_test_expect(test, ir_create(&test->output, &sizes, function) == IR_OK, "create the shared phis' function");
    if (ok)
    {
        function->result_type = IR_VOID;
        function->parameter_types[0] = IR_PTR;
        function->constants[0] = (IrConstant){ 0 };
        tier1_wide_test_set(function, 0, IR_PARAM, IR_PTR, 0, none, none);
        tier1_wide_test_set(function, 1, IR_LOAD, IR_I128, 0, none, none);
        function->alignment_log2[1] = 4;
        tier1_wide_test_set(function, 2, IR_CONST, IR_I1, 0, none, none);
        tier1_wide_test_set(function, 3, IR_BR, IR_VOID, 1, none, none);
        function->block_offsets[0] = 0;
        for (uint32_t k = 1; k <= predecessors; k += 1)
        {
            function->block_offsets[k] = 3 + k;
            if (k < predecessors)
            {
                tier1_wide_test_set(function, 3 + k, IR_CBR, IR_VOID, 2, join, k + 1);
            }
            else
            {
                tier1_wide_test_set(function, 3 + k, IR_BR, IR_VOID, join, none, none);
            }
            function->extra[(k - 1) * 2] = k;
            function->extra[(k - 1) * 2 + 1] = 1;
        }
        function->block_offsets[join] = first_phi;
        for (uint32_t q = 0; q < phis; q += 1)
        {
            tier1_wide_test_set(function, first_phi + q, IR_PHI, IR_I128, 0, predecessors, none);
        }
        tier1_wide_test_set(function, first_phi + phis, IR_STORE, IR_VOID, 0, first_phi + phis - 1, none);
        function->alignment_log2[first_phi + phis] = 4;
        tier1_wide_test_set(function, first_phi + phis + 1, IR_RET, IR_VOID, none, none, none);
        function->block_offsets[join + 1] = first_phi + phis + 2;
    }
    return ok;
}

// The split's counts past a u32's (#1204's review): phis that share a predecessor list append a list of their own for
// each half whatever the source's table holds. A small instance must verify, and split by both paths into a function
// both verifiers take. At 32,768 phis of 32,768 predecessors the halves' lists are 2^32 words past the source's
// 65,536, which a u32 count wrapped back to the source's own: both paths refuse it as IR_ERROR_LIMIT before they
// allocate. One phi fewer, 65,536 words short of 2^32, is counted as it is and fails only for the test's output arena,
// whose 256 MB can't hold 16 GB of extra words.
static void tier1_wide_test_limit(Tier1WideTest* test)
{
    const uint32_t shapes[3][2] = { { 6, 5 }, { 32768, 32768 }, { 32767, 32768 } };
    const IrError expected[3] = { IR_OK, IR_ERROR_LIMIT, IR_ERROR_MEMORY };
    for (uint32_t s = 0; s < 3; s += 1)
    {
        const uint64_t output_mark = test->output.length;
        IrFunction function;
        bool ok = tier1_wide_test_shared_phis(test, shapes[s][0], shapes[s][1], &function);
        if (ok && s == 0)
        {
            IrDominance dominance = { 0 };
            ok = tier1_wide_test_dominance(test, &function, &dominance);
            const IrCheck verified = ok ? ir_verify(&function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
            const IrCheck twin = ok ? ir_verify_scalar(&function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
            ok = ok && tier1_wide_test_expect(test, verified.error == IR_OK && twin.error == IR_OK, "both verifiers take I128 phis that share one predecessor list");
        }
        for (uint32_t path = 0; ok && path < 2; path += 1)
        {
            const uint64_t mark = test->output.length;
            IrWideResult result = { 0 };
            const IrCheck split = path == 0 ? ir_wide_split(&function, &test->output, &test->scratch, &result) : ir_wide_split_scalar(&function, &test->output, &test->scratch, &result);
            if (s == 0)
            {
                bool fine = tier1_wide_test_expect(test, split.error == IR_OK && result.split && result.function.extra_count == 10 + 6 * 20, "both paths split phis that share a predecessor list, each half's list its own");
                IrDominance split_dominance = { 0 };
                fine = fine && tier1_wide_test_dominance(test, &result.function, &split_dominance);
                const IrCheck checked = fine ? ir_verify(&result.function, &test->scratch, &split_dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
                const IrCheck scalar = fine ? ir_verify_scalar(&result.function, &test->scratch, &split_dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
                tier1_wide_test_expect(test, checked.error == IR_OK && scalar.error == IR_OK, "the split of phis that share a predecessor list verifies");
            }
            else
            {
                const bool refused = split.error == expected[s] && !result.split && test->output.length == mark;
                tier1_wide_test_expect(test, refused, s == 1 ? "both paths refuse an output whose extra table a u32 can't count, before they allocate" : "both paths count an extra table just below a u32's limit as it is");
                if (!refused)
                {
                    printf("test-tier1-wide: %u phis of %u predecessors, %s path: split error %u at %u\n", shapes[s][0], shapes[s][1], path == 0 ? "vector" : "scalar", (unsigned)split.error, split.instruction);
                }
            }
            arena_pop_to(&test->output, mark);
        }
        tier1_wide_test_expect(test, test->scratch.length == 0, "the limit's checks return the scratch arena");
        arena_pop_to(&test->output, output_mark);
    }
}

// The refusals, a function without an I128 instruction, and the scan's twins.
static void tier1_wide_test_boundaries(Tier1WideTest* test)
{
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(tier1_wide_test_refusals); r += 1)
    {
        const uint64_t output_mark = test->output.length;
        IrFunction function;
        if (tier1_wide_test_parse(test, "a refusal", tier1_wide_test_refusals[r], &function))
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                IrWideResult result = { .split = true };
                const IrCheck check = path == 0 ? ir_wide_split(&function, &test->output, &test->scratch, &result) : ir_wide_split_scalar(&function, &test->output, &test->scratch, &result);
                test->refused += tier1_wide_test_expect(test, check.error == IR_ERROR_UNSUPPORTED && !result.split, "the split refuses what it doesn't take as unsupported");
            }
        }
        arena_pop_to(&test->output, output_mark);
    }
    {
        const uint64_t output_mark = test->output.length;
        IrFunction function;
        if (tier1_wide_test_parse(test, "no I128", "function (ptr) -> void {\nb0:\n  %0 = param.ptr 0\n  %1 = load.i64 %0 align=8\n  %2 = store.void %0, %1 align=8\n  %3 = ret.void\n}\n", &function))
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const uint64_t mark = test->output.length;
                IrWideResult result = { .split = true };
                const IrCheck check = path == 0 ? ir_wide_split(&function, &test->output, &test->scratch, &result) : ir_wide_split_scalar(&function, &test->output, &test->scratch, &result);
                tier1_wide_test_expect(test, check.error == IR_OK && !result.split && result.function.op == function.op && test->output.length == mark, "a function without an I128 instruction is returned as it is");
            }
        }
        arena_pop_to(&test->output, output_mark);
    }
    // The scan: type columns of every length to 200 ending at a PROT_NONE page, without an I128 and with one at each place.
    const uint64_t page = 4096;
    uint8_t* pages = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tier1_wide_test_expect(test, pages != MAP_FAILED && mprotect(pages + page, page, PROT_NONE) == 0, "the scan's guarded page"))
    {
        bool agree = true;
        for (uint32_t length = 0; length <= 200; length += 1)
        {
            uint8_t* types = pages + page - length;
            for (uint32_t place = 0; place <= length; place += 1)
            {
                for (uint32_t k = 0; k < length; k += 1)
                {
                    types[k] = (uint8_t)(1 + (k * 7 + length) % (IR_TYPE_COUNT - 1));
                    types[k] = types[k] == IR_I128 ? IR_I64 : types[k];
                }
                if (place < length)
                {
                    types[place] = IR_I128;
                }
                const IrFunction function = { .type = types, .instruction_count = length };
                const bool vector = ir_wide_present_path(&function, true);
                const bool scalar = ir_wide_present_path(&function, false);
                agree &= vector == scalar && scalar == (place < length);
            }
        }
        tier1_wide_test_expect(test, agree, "the scan's vector path is its scalar twin's, and finds each I128");
    }
    if (pages != MAP_FAILED)
    {
        munmap(pages, 2 * page);
    }
}

#include "tier1_wide_vectors_test.c"
#include "tier1_x87_transfer_test.c"

static __attribute__((unused)) int run_tier1_wide_tests(int argc, char** argv)
{
    uint64_t seed = 141;
    uint32_t programs = BUSTER_TIER1_WIDE_TEST_PROGRAMS;
    const char* file = 0;
    bool valid = true;
    bool print = false;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--print") == 0)
        {
            print = true;
        }
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
            i += 1;
            file = argv[i];
        }
        else if ((strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "--programs") == 0) && i + 1 < argc)
        {
            const bool is_seed = strcmp(argv[i], "--seed") == 0;
            i += 1;
            char* end = 0;
            const uint64_t value = strtoull(argv[i], &end, 0);
            valid = argv[i][0] != 0 && *end == 0 && (is_seed || value <= 100000);
            seed = is_seed ? value : seed;
            programs = is_seed ? programs : (uint32_t)value;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-tier1-wide [--seed N] [--programs N] [--print] [--file IR]\n");
    }
    else
    {
        Tier1WideTest test = {
            .random = seed * 2 + 1,
            .print = print,
            .baseline = malloc(X86_FORM_COUNT),
            .output = arena_reserve(256u << 20),
            .scratch = arena_reserve(256u << 20),
            .execution = arena_reserve(64u << 20),
            .promoted = arena_reserve(64u << 20),
            .optimized = arena_reserve(64u << 20),
        };
        char* text = malloc(BUSTER_TIER1_WIDE_TEST_TEXT);
        const bool ready = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && test.baseline != 0 && text != 0 && test.output.base != 0 && test.scratch.base != 0 && test.execution.base != 0 &&
                           test.promoted.base != 0 && test.optimized.base != 0;
        if (tier1_wide_test_expect(&test, ready, "initialize the tables and arenas"))
        {
            const X86Target base = { .architecture = X86_ARCH_BASELINE };
            x86_target_forms(&base, test.baseline);
            // --file: one program's text alone, through every stage (a reproduction of a random program's failure).
            FILE* stream = file != 0 ? fopen(file, "rb") : 0;
            const size_t length = stream != 0 ? fread(text, 1, BUSTER_TIER1_WIDE_TEST_TEXT - 1, stream) : 0;
            if (stream != 0)
            {
                fclose(stream);
                text[length] = 0;
                tier1_wide_test_program(&test, file, text, 0, UINT32_MAX, UINT32_MAX, false, strstr(text, "i128") != 0);
            }
            tier1_wide_test_expect(&test, file == 0 || stream != 0, "read the --file program");
            for (uint32_t f = 0; file == 0 && f < BUSTER_ARRAY_LENGTH(tier1_wide_test_fixtures); f += 1)
            {
                const Tier1WideFixture* fixture = &tier1_wide_test_fixtures[f];
                tier1_wide_test_program(&test, fixture->name, fixture->text, fixture->count, fixture->multiplies, fixture->high_multiplies, fixture->constant_counts, true);
            }
            if (file == 0)
            {
                tier1_wide_test_x87_phis(&test, text);
            }
            for (uint32_t p = 0; file == 0 && p < programs; p += 1)
            {
                tier1_wide_test_generate(&test, text, 4 + (uint32_t)(tier1_wide_test_next(&test) % 28));
                tier1_wide_test_program(&test, "a random program", text, 0, UINT32_MAX, UINT32_MAX, false, true);
            }
            uint32_t typed = 0;
            if (file == 0)
            {
                tier1_wide_test_boundaries(&test);
                tier1_x87_transfer_tests(&test);
                tier1_wide_vectors(&test);
                tier1_wide_test_shared_slices(&test);
                tier1_wide_test_limit(&test);
                typed = tier1_wide_test_typed(&test, BUSTER_TIER1_WIDE_TEST_TYPED);
                printf("test-tier1-wide: Tier 0's 128-bit programs: %u of %u through the split, the optimizer and Tier 1 natively\n", typed, BUSTER_TIER1_WIDE_TEST_TYPED);
                const uint32_t x87 = tier1_wide_test_x87(&test, BUSTER_TIER1_WIDE_TEST_TYPED);
                printf("test-tier1-wide: Tier 0's long double programs: %u of %u through the optimizer and Tier 1 natively\n", x87, BUSTER_TIER1_WIDE_TEST_TYPED);
                typed += x87;
            }
            // The fixtures, the long double phis' two programs and the conversions' (tier1_wide_test_x87_phis) and the
            // random programs.
            const uint64_t expected_runs = 4u * (BUSTER_ARRAY_LENGTH(tier1_wide_test_fixtures) + 3 + programs) * BUSTER_TIER1_WIDE_TEST_INPUTS + 2u * typed;
            tier1_wide_test_expect(&test, file != 0 || test.native_runs == expected_runs, "every program runs natively on every input by both paths, with rematerialization and without");
            tier1_wide_test_expect(&test, file != 0 || test.refused == 2u * BUSTER_ARRAY_LENGTH(tier1_wide_test_refusals), "every refusal on both paths");
        }
        printf("test-tier1-wide: %" PRIu64 " checks, %" PRIu64 " failures; %u programs, %" PRIu64 " I128 instructions split into %" PRIu64 " instructions; %" PRIu64 " interpreter runs, %" PRIu64 " native runs, %" PRIu64
               " records; %u refusals (seed=%" PRIu64 ")\n",
               test.checks, test.failures, test.programs, test.wide, test.halves, test.interpreted, test.native_runs, test.records, test.refused, seed);
        result = test.failures == 0 ? 0 : 1;
        free(text);
        arena_release(&test.optimized);
        arena_release(&test.promoted);
        arena_release(&test.execution);
        arena_release(&test.scratch);
        arena_release(&test.output);
        free(test.baseline);
    }
    return result;
}
