// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier1-wide's vectors wider than the target's registers (#1624, ir_vector_split.c). Included by
// tier1_wide_test.c, whose helpers it shares.
//
// Each fixture is IR text of 32- and 64-byte vectors through every instruction the split takes: loads and stores
// (aligned, unaligned, through addresses with offsets, a volatile one), constants, a copy, an undef on a path not
// taken, phis at joins and swapped around a loop, and a frame object's stores and loads. Each is
// parsed by both parsers and run by the interpreter on random buffers: the expected buffers. Then it is split at both
// piece widths (16 bytes, x86-64's, and 32, AVX's) by both paths, which must make the same function, with no vector
// wider than a piece, that both verifiers take and the interpreter runs into the same buffers; optimized and run again;
// and, split into 16-byte pieces, planned and emitted by both of Tier 1's paths for the x86-64 baseline (every record
// among its forms), with rematerialization and without, and run natively on every buffer. And the refusals: a wide
// vector's addition, comparison, call argument and parameter, which the split refuses as
// unsupported at 16-byte pieces, and the same functions, no vector in them wider than 64 bytes' pieces, returned as they
// are at 64; a function without a wide vector, returned as it is; and the scan's vector path against its scalar twin on
// type columns of every length to 200 with a wide vector at each place.

#define BUSTER_TIER1_WIDE_VECTOR_CONSTANT_64                                                                                                                                                          \
    "0123456789abcdeffedcba98765432100f1e2d3c4b5a69788796a5b4c3d2e1f0deadbeefcafebabe8000000000000001ffffffffffffffff7fffffffffffffff"
#define BUSTER_TIER1_WIDE_VECTOR_CONSTANT_32 "5555aaaa3333cccc0f0f0f0ff0f0f0f011223344556677889988776655443322"

typedef struct Tier1WideVectorFixture Tier1WideVectorFixture;
struct Tier1WideVectorFixture
{
    const char* name;
    const char* text;
    // The count it loops to, at most this (0 for a function of the pointer alone).
    uint32_t count;
};

static const Tier1WideVectorFixture tier1_wide_vector_fixtures[] = {
    {
        .name = "wide vectors' loads and stores",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.v8i32 %0 align=1\n"
                "  %2 = address.ptr %0 offset=96\n"
                "  %3 = load.v16i32 %2 align=32\n"
                "  %4 = address.ptr %0 offset=33\n"
                "  %5 = load.v16i32 %4 align=1\n"
                "  %6 = address.ptr %0 offset=256\n"
                "  %7 = store.void %6, %3 align=64\n"
                "  %8 = address.ptr %0 offset=320\n"
                "  %9 = store.void %8, %1 align=1\n"
                "  %10 = address.ptr %0 offset=352\n"
                "  %11 = store.void %10, %1 align=32\n"
                "  %12 = address.ptr %0 offset=401\n"
                "  %13 = store.void %12, %5 align=1\n"
                "  %14 = ret.void\n"
                "}\n",
    },
    {
        .name = "wide vector constants, a copy and a volatile store",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = vconst.v16i32 0x" BUSTER_TIER1_WIDE_VECTOR_CONSTANT_64 "\n"
                "  %2 = address.ptr %0 offset=256\n"
                "  %3 = store.void %2, %1 align=16\n"
                "  %4 = vconst.v8i32 0x" BUSTER_TIER1_WIDE_VECTOR_CONSTANT_32 "\n"
                "  %5 = copy.v8i32 %4\n"
                "  %6 = address.ptr %0 offset=384\n"
                "  %7 = store.void %6, %5 align=8\n"
                "  %8 = address.ptr %0 offset=416\n"
                "  %9 = store.void %8, %4 align=32 volatile\n"
                "  %10 = ret.void\n"
                "}\n",
    },
    {
        // A diamond of each of two comparisons of loaded words, whose arms load other vectors, their phis at the join,
        // and an undef on the path the first comparison's constant never takes.
        .name = "wide vectors' phis at joins and an undef",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.i64 %0 align=8\n"
                "  %2 = address.ptr %0 offset=8\n"
                "  %3 = load.i64 %2 align=8\n"
                "  %4 = icmp.i1 %1, %3 ult\n"
                "  %5 = address.ptr %0 offset=64\n"
                "  %6 = load.v16i32 %5 align=64\n"
                "  %7 = undef.v8i32\n"
                "  %8 = cbr.void %4, b1, b2\n"
                "b1:\n"
                "  %9 = address.ptr %0 offset=128\n"
                "  %10 = load.v16i32 %9 align=64\n"
                "  %11 = load.v8i32 %0 align=32\n"
                "  %12 = br.void b3\n"
                "b2:\n"
                "  %13 = address.ptr %0 offset=192\n"
                "  %14 = load.v8i32 %13 align=32\n"
                "  %15 = br.void b3\n"
                "b3:\n"
                "  %16 = phi.v16i32 [%10, b1], [%6, b2]\n"
                "  %17 = phi.v8i32 [%11, b1], [%14, b2]\n"
                "  %18 = const.i64 0x0\n"
                "  %19 = icmp.i1 %18, %18 eq\n"
                "  %20 = cbr.void %19, b5, b4\n"
                "b4:\n"
                "  %21 = br.void b5\n"
                "b5:\n"
                "  %22 = phi.v8i32 [%17, b3], [%7, b4]\n"
                "  %23 = address.ptr %0 offset=256\n"
                "  %24 = store.void %23, %16 align=64\n"
                "  %25 = address.ptr %0 offset=320\n"
                "  %26 = store.void %25, %22 align=32\n"
                "  %27 = ret.void\n"
                "}\n",
    },
    {
        // Two 64-byte vectors and a 32-byte one swapped around a loop `count` times, beside a counter.
        .name = "wide vectors' phis around a loop",
        .count = 9,
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.v16i32 %0 align=64\n"
                "  %3 = address.ptr %0 offset=64\n"
                "  %4 = load.v16i32 %3 align=64\n"
                "  %5 = address.ptr %0 offset=128\n"
                "  %6 = load.v8i32 %5 align=32\n"
                "  %7 = const.i64 0x0\n"
                "  %8 = br.void b1\n"
                "b1:\n"
                "  %9 = phi.v16i32 [%2, b0], [%10, b1]\n"
                "  %10 = phi.v16i32 [%4, b0], [%9, b1]\n"
                "  %11 = phi.v8i32 [%6, b0], [%11, b1]\n"
                "  %12 = phi.i64 [%7, b0], [%14, b1]\n"
                "  %13 = const.i64 0x1\n"
                "  %14 = add.i64 %12, %13\n"
                "  %15 = icmp.i1 %14, %1 ult\n"
                "  %16 = cbr.void %15, b1, b2\n"
                "b2:\n"
                "  %17 = address.ptr %0 offset=256\n"
                "  %18 = store.void %17, %9 align=64\n"
                "  %19 = address.ptr %0 offset=320\n"
                "  %20 = store.void %19, %10 align=64\n"
                "  %21 = address.ptr %0 offset=384\n"
                "  %22 = store.void %21, %11 align=32\n"
                "  %23 = ret.void\n"
                "}\n",
    },
    {
        // A frame object's: a 64-byte vector stored into it and loaded back, its first 32 bytes too, as the typed
        // stream's union of a vector and its lanes is (host_scalar_vectors.h's HostV512).
        .name = "a frame object's wide vectors",
        .text = "function (ptr) -> void {\n"
                "  slot $0 size=64 align=64 type=void flags=0x01\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = stack_addr.ptr $0\n"
                "  %2 = load.v16i32 %0 align=1\n"
                "  %3 = store.void %1, %2 align=64\n"
                "  %4 = address.ptr %1 offset=8\n"
                "  %5 = load.i64 %4 align=8\n"
                "  %6 = const.i64 0x1\n"
                "  %7 = add.i64 %5, %6\n"
                "  %8 = store.void %4, %7 align=8\n"
                "  %9 = load.v16i32 %1 align=64\n"
                "  %10 = load.v8i32 %1 align=32\n"
                "  %11 = address.ptr %0 offset=256\n"
                "  %12 = store.void %11, %9 align=1\n"
                "  %13 = address.ptr %0 offset=320\n"
                "  %14 = store.void %13, %10 align=1\n"
                "  %15 = ret.void\n"
                "}\n",
    },
};

// The functions the split refuses at 16-byte pieces: a wide vector's addition, comparison, call argument and parameter.
// (An atomic one's access, which the split refuses too, no verifier takes.)
static const char* const tier1_wide_vector_refusals[] = {
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.v8i32 %0 align=32\n"
    "  %2 = add.v8i32 %1, %1\n"
    "  %3 = store.void %0, %2 align=32\n"
    "  %4 = ret.void\n"
    "}\n",
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.v16i32 %0 align=64\n"
    "  %2 = vcmp.m16 %1, %1 eq\n"
    "  %3 = store.void %0, %2 align=2\n"
    "  %4 = ret.void\n"
    "}\n",
    "function (ptr) -> void {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = load.v8i32 %0 align=32\n"
    "  %2 = call.void @2(%1) fixed=(v8i32) variadic=0\n"
    "  %3 = ret.void\n"
    "}\n",
    "function (v16i32) -> void {\n"
    "b0:\n"
    "  %0 = param.v16i32 0\n"
    "  %1 = ret.void\n"
    "}\n",
};

// Whether a function has a vector wider than `piece_bytes`.
static bool tier1_wide_vector_any(const IrFunction* function, uint32_t piece_bytes)
{
    bool wide = false;
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        wide |= ir_vector_type(function->type[i]) && ir_type_bits[function->type[i]] > 8 * piece_bytes;
    }
    return wide;
}

// One fixture through every stage (the file's comment), at both piece widths.
static void tier1_wide_vector_program(Tier1WideTest* test, const Tier1WideVectorFixture* fixture)
{
    const uint64_t output_mark = test->output.length;
    IrFunction function;
    bool ok = tier1_wide_test_parse(test, fixture->name, fixture->text, &function);
    // Aligned to 64, as the native runs' mapping is: the interpreter checks the accesses' alignments.
    static uint8_t inputs[BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS] __attribute__((aligned(64)));
    static uint8_t expected[BUSTER_TIER1_WIDE_TEST_BUFFER * BUSTER_TIER1_WIDE_TEST_INPUTS] __attribute__((aligned(64)));
    uint64_t counts[BUSTER_TIER1_WIDE_TEST_INPUTS];
    for (uint32_t input = 0; ok && input < BUSTER_TIER1_WIDE_TEST_INPUTS; input += 1)
    {
        uint8_t* bytes = inputs + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER;
        for (uint32_t byte = 0; byte < BUSTER_TIER1_WIDE_TEST_BUFFER; byte += 1)
        {
            // The first input's first two words equal, so that the diamond's comparison takes each way across inputs.
            bytes[byte] = input == 0 && byte < 16 ? (uint8_t)(byte % 8) : (uint8_t)tier1_wide_test_next(test);
        }
        counts[input] = fixture->count != 0 ? 1 + tier1_wide_test_next(test) % fixture->count : 0;
        ok = tier1_wide_test_interpret(test, &function, bytes, counts[input], expected + (uint64_t)input * BUSTER_TIER1_WIDE_TEST_BUFFER);
    }
    static const uint32_t pieces[2] = { 16, 32 };
    for (uint32_t w = 0; ok && w < 2; w += 1)
    {
        const uint64_t mark = test->output.length;
        IrVectorSplitResult vector = { 0 };
        IrVectorSplitResult scalar = { 0 };
        const IrCheck split = ir_vector_split(&function, pieces[w], &test->output, &test->scratch, &vector);
        const IrCheck twin = ir_vector_split_scalar(&function, pieces[w], &test->output, &test->scratch, &scalar);
        bool piece_ok = tier1_wide_test_expect(test, split.error == IR_OK && twin.error == IR_OK && vector.split && scalar.split, "both paths split the wide vectors");
        if (!piece_ok)
        {
            printf("test-tier1-wide: %s at %u-byte pieces: split error %u, scalar %u, at %u\n", fixture->name, pieces[w], (unsigned)split.error, (unsigned)twin.error, split.instruction);
        }
        piece_ok = piece_ok && tier1_wide_test_expect(test, tier1_wide_test_same(&vector.function, &scalar.function) && vector.wide == scalar.wide && vector.wide != 0 &&
                                                                vector.function.vector_constant_count == scalar.function.vector_constant_count &&
                                                                (vector.function.vector_constant_count == 0 ||
                                                                 memcmp(vector.function.vector_constants, scalar.function.vector_constants, (uint64_t)vector.function.vector_constant_count * sizeof(IrSimdValue)) == 0),
                                                  "both paths split it into the same function");
        piece_ok = piece_ok && tier1_wide_test_expect(test, !tier1_wide_vector_any(&vector.function, pieces[w]), "the split leaves no vector wider than a piece");
        tier1_wide_test_expect(test, test->scratch.length == 0, "the split returns the scratch arena");
        IrDominance dominance = { 0 };
        if (piece_ok)
        {
            piece_ok = tier1_wide_test_dominance(test, &vector.function, &dominance);
            const IrCheck verified = piece_ok ? ir_verify(&vector.function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
            const IrCheck verified_twin = piece_ok ? ir_verify_scalar(&vector.function, &test->scratch, &dominance) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
            piece_ok = piece_ok && tier1_wide_test_expect(test, verified.error == IR_OK && verified_twin.error == IR_OK, "both verifiers take the split function with its dominance");
            if (!piece_ok)
            {
                printf("test-tier1-wide: %s at %u-byte pieces: verifier error %u at %u (%u)\n", fixture->name, pieces[w], (unsigned)verified.error, verified.instruction, verified.detail);
            }
            piece_ok = piece_ok && tier1_wide_test_agrees(test, fixture->name, "the split vectors", &vector.function, inputs, counts, expected);
            test->wide += vector.wide;
            test->halves += vector.function.instruction_count;
        }
        IrOptimizeResult optimized = { 0 };
        if (piece_ok)
        {
            const IrCheck check = ir_optimize(&vector.function, &test->output, &test->scratch, ir_optimize_options(), &optimized);
            piece_ok = tier1_wide_test_expect(test, check.error == IR_OK, "the optimizer takes the split vectors");
            piece_ok = piece_ok && tier1_wide_test_expect(test, !tier1_wide_vector_any(&optimized.function, pieces[w]), "the optimizer makes no vector wider than a piece");
            piece_ok = piece_ok && tier1_wide_test_agrees(test, fixture->name, "the optimized split vectors", &optimized.function, inputs, counts, expected);
            piece_ok = piece_ok && tier1_wide_test_dominance(test, &optimized.function, &dominance);
        }
        // x86-64's pieces natively, every record among the baseline's forms.
        for (uint32_t run = 0; piece_ok && pieces[w] == 16 && run < 4; run += 1)
        {
            const uint64_t before = test->native_runs;
            tier1_wide_test_native(test, fixture->name, &optimized.function, &dominance, (run & 1) == 0, (run & 2) != 0, inputs, counts, expected);
            test->vector_native_runs += test->native_runs - before;
            test->native_runs = before;
        }
        test->vector_programs += piece_ok;
        arena_pop_to(&test->output, mark);
    }
    tier1_wide_test_expect(test, test->scratch.length == 0, "every stage returns the scratch arena");
    arena_pop_to(&test->output, output_mark);
}

// The refusals, a function without a wide vector, and the scan's two paths (the file's comment).
static void tier1_wide_vector_refusals_and_scan(Tier1WideTest* test)
{
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(tier1_wide_vector_refusals); r += 1)
    {
        const uint64_t mark = test->output.length;
        IrFunction function;
        if (tier1_wide_test_parse(test, "a wide vector refusal", tier1_wide_vector_refusals[r], &function))
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                IrVectorSplitResult result;
                memset(&result, 0x5a, sizeof(result));
                const uint64_t before = test->output.length;
                const IrCheck check = path == 0 ? ir_vector_split(&function, 16, &test->output, &test->scratch, &result) : ir_vector_split_scalar(&function, 16, &test->output, &test->scratch, &result);
                const bool refused = check.error == IR_ERROR_UNSUPPORTED && test->output.length == before && test->scratch.length == 0 && !result.split;
                tier1_wide_test_expect(test, refused, "the split refuses a wide vector it doesn't take, both arenas as they were");
                test->vector_refused += refused;
                const IrCheck whole = path == 0 ? ir_vector_split(&function, 64, &test->output, &test->scratch, &result) : ir_vector_split_scalar(&function, 64, &test->output, &test->scratch, &result);
                tier1_wide_test_expect(test, whole.error == IR_OK && !result.split && result.function.op == function.op, "at 64-byte pieces nothing is wide: the function returned as it is");
            }
        }
        arena_pop_to(&test->output, mark);
    }
    // A function without a wide vector at 16-byte pieces: a 16-byte vector's loads and store.
    const char* narrow = "function (ptr) -> void {\n"
                         "b0:\n"
                         "  %0 = param.ptr 0\n"
                         "  %1 = load.v4i32 %0 align=16\n"
                         "  %2 = store.void %0, %1 align=16\n"
                         "  %3 = ret.void\n"
                         "}\n";
    IrFunction function;
    if (tier1_wide_test_parse(test, "a narrow vector", narrow, &function))
    {
        IrVectorSplitResult result;
        const IrCheck check = ir_vector_split(&function, 16, &test->output, &test->scratch, &result);
        tier1_wide_test_expect(test, check.error == IR_OK && !result.split && result.wide == 0 && result.function.op == function.op, "a function without a wide vector is returned as it is");
        const IrCheck bad = ir_vector_split(&function, 24, &test->output, &test->scratch, &result);
        tier1_wide_test_expect(test, bad.error == IR_ERROR_STRUCTURE, "a piece of no vector's width is refused");
    }
    // The scan: every length to 200, a wide vector (of each wide type, at each piece width) at each place or none.
    uint8_t types[200];
    bool agreed = true;
    for (uint32_t length = 0; length <= 200; length += 1)
    {
        for (uint32_t place = 0; place <= length; place += 1)
        {
            for (uint32_t kind = 0; kind < 3; kind += 1)
            {
                static const uint8_t wide[3] = { IR_V8I32, IR_V16I32, IR_V4I32 };
                for (uint32_t t = 0; t < length; t += 1)
                {
                    types[t] = (uint8_t)(t % 7 == 3 ? IR_V4I32 : t % 5 == 1 ? IR_M64 : t % 3 == 2 ? IR_PTR : IR_I32);
                }
                if (place < length)
                {
                    types[place] = wide[kind];
                }
                const IrFunction scan = { .type = types, .instruction_count = length };
                for (uint32_t piece = IR_V4I32; piece <= IR_V8I32; piece += 1)
                {
                    const bool vector = ir_vector_split_present_path(&scan, piece, true);
                    const bool scalar = ir_vector_split_present_path(&scan, piece, false);
                    const bool expected = place < length && wide[kind] > piece;
                    agreed &= vector == scalar && scalar == expected;
                }
            }
        }
    }
    tier1_wide_test_expect(test, agreed, "the scan's vector path and its scalar twin find a wide vector at every place of every length");
}

// The wide vectors' tests: the fixtures, the refusals and the scan.
static void tier1_wide_vectors(Tier1WideTest* test)
{
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(tier1_wide_vector_fixtures); f += 1)
    {
        tier1_wide_vector_program(test, &tier1_wide_vector_fixtures[f]);
    }
    tier1_wide_vector_refusals_and_scan(test);
    tier1_wide_test_expect(test, test->vector_programs == 2 * BUSTER_ARRAY_LENGTH(tier1_wide_vector_fixtures), "every wide vector fixture goes through every stage at both piece widths");
    tier1_wide_test_expect(test, test->vector_native_runs == 4u * BUSTER_ARRAY_LENGTH(tier1_wide_vector_fixtures) * BUSTER_TIER1_WIDE_TEST_INPUTS,
                           "every wide vector fixture runs natively at x86-64's pieces on every input by both paths, with rematerialization and without");
    tier1_wide_test_expect(test, test->vector_refused == 2 * BUSTER_ARRAY_LENGTH(tier1_wide_vector_refusals), "every wide vector refusal on both paths");
    printf("test-tier1-wide: wide vectors: %u fixtures at 16- and 32-byte pieces through the split, the optimizer and the interpreter, %" PRIu64 " native runs at x86-64's; %u refusals\n",
           (unsigned)BUSTER_ARRAY_LENGTH(tier1_wide_vector_fixtures), test->vector_native_runs, test->vector_refused);
}
