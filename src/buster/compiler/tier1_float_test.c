// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent native fixtures for floating values in Tier 1 (#141), included after tier1_stack_test.c: two functions of
// original IR through tier1_emit_function, which gives them to the CFG consumer's vector class, by both paths, with
// constants rematerialized and not, with every xmm register and with few, and with and without a kept frame pointer.
// C's own code of the same operations, in the same order, is the oracle: each result is compared bit for bit.
//
// - tier1_float_mixed_function takes thirteen parameters, integers, pointers, F32 and F64 values interleaved, ten of them
//   floating: eight in xmm0 to xmm7 and two past them on the stack, as C passes them. It adds, subtracts, multiplies,
//   divides and negates both widths, converts between them, from 8- to 64-bit integers signed and unsigned and back,
//   compares two of its values by every predicate (equal, unordered and ordered samples among them), moves bits between
//   the classes, and stores every result through its pointer parameter, then loads two of them back, the second into
//   an addition's memory operand, for its F64 result.
// - tier1_float_loop_function runs a loop over an array of doubles, NaNs among them, whose floating values are phis:
//   each element's comparison with 1.5, negated through C's truth values, branches to an addition whose right operand
//   is the element's load or to a subtraction of a constant, and the join calls a C function through a pointer with an
//   F64, an I64 and an F32 argument, a floating value live across the call; the exit calls a variadic C function with
//   three doubles, its al 3. The callees note whether the stack was aligned at their calls.
//
// - tier1_float_undefined_text is a loop, in the IR's text, whose join's phis take undef along one edge (#141): a local
//   a block assigns on one path only and reads on that path, as promotion leaves it after the block's lifetime start.
//   The CFG consumer moves nothing for an undefined operand, and the code computes C's result.
//
// The code runs in a bounded child (tier1_float_tests).

#define BUSTER_TIER1_FLOAT_SAMPLES 24u
#define BUSTER_TIER1_FLOAT_OUT_BYTES 160u
#define BUSTER_TIER1_FLOAT_ELEMENTS 16u

// Whether any C callee found the stack misaligned at its call (tier1_stack_check_alignment's test).
static volatile uint32_t tier1_float_misaligned;

static __attribute__((noinline)) double tier1_float_callee(double s, int64_t i, float t)
{
    tier1_float_misaligned |= ((uintptr_t)__builtin_frame_address(0) & 15) != 0;
    const double half = s * 0.5;
    const double index = (double)i;
    const double sum = half + index;
    return sum - (double)t;
}

static __attribute__((noinline)) double tier1_float_callee_variadic(int64_t count, ...)
{
    tier1_float_misaligned |= ((uintptr_t)__builtin_frame_address(0) & 15) != 0;
    double sum = 0.0;
    __builtin_va_list arguments;
    __builtin_va_start(arguments, count);
    for (int64_t k = 0; k < count && k < 8; k += 1)
    {
        const double value = __builtin_va_arg(arguments, double);
        const double weighted = value * (double)(k + 1);
        sum = sum + weighted;
    }
    __builtin_va_end(arguments);
    return sum;
}

// The mixed fixture's parameters' types: a (F64), n (I64), b (F32), c (F64), k (I32), d (F64), e (F32), f (F64), g, h
// and i (F64), j (F32) and out (PTR). a to h are in xmm0 to xmm7, i and j on the stack, n, k and out in rdi, rsi and rdx.
static const uint8_t tier1_float_mixed_types[13] = { IR_F64, IR_I64, IR_F32, IR_F64, IR_I32, IR_F64, IR_F32, IR_F64, IR_F64, IR_F64, IR_F64, IR_F32, IR_PTR };

// The next node, with an address of `base` plus `offset` (a new IrAddress) where `offset` isn't UINT32_MAX.
static uint32_t tier1_float_address(IrFunction* function, uint32_t* next, uint32_t* addresses, uint32_t base, uint32_t offset)
{
    function->addresses[*addresses] = (IrAddress){ .offset = offset, .scale = 1 };
    const uint32_t node = tier1_select_test_append(function, next, IR_ADDRESS, IR_PTR, base, BUSTER_IR_NONE, *addresses);
    *addresses += 1;
    return node;
}

static void tier1_float_store(IrFunction* function, uint32_t* next, uint32_t* addresses, uint32_t base, uint32_t offset, uint32_t value)
{
    const uint32_t address = tier1_float_address(function, next, addresses, base, offset);
    tier1_select_test_append(function, next, IR_STORE, IR_VOID, address, value, BUSTER_IR_NONE);
}

// The mixed fixture (see the header). Its results' offsets in `out` are tier1_float_mixed_oracle's.
static bool tier1_float_mixed_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 128,
        .block_count = 1,
        .constant_count = 1,
        .address_count = 32,
        .parameter_count = 13,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_F64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 3, .vector_registers = 8, .stack_bytes = 2 * BUSTER_ABI_SLOT_BYTES };
        function->constants[0].low = 0;
        uint32_t next = 0;
        uint32_t addresses = 0;
        for (uint32_t k = 0; k < 13; k += 1)
        {
            function->parameter_types[k] = tier1_float_mixed_types[k];
            tier1_select_test_append(function, &next, IR_PARAM, tier1_float_mixed_types[k], k, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t a = 0;
        const uint32_t n = 1;
        const uint32_t b = 2;
        const uint32_t c = 3;
        const uint32_t k = 4;
        const uint32_t d = 5;
        const uint32_t e = 6;
        const uint32_t f = 7;
        const uint32_t g = 8;
        const uint32_t h = 9;
        const uint32_t i = 10;
        const uint32_t j = 11;
        const uint32_t out = 12;
        const uint32_t x1 = tier1_select_test_append(function, &next, IR_FADD, IR_F64, a, c, BUSTER_IR_NONE);
        const uint32_t x2 = tier1_select_test_append(function, &next, IR_FMUL, IR_F64, x1, d, BUSTER_IR_NONE);
        const uint32_t x3 = tier1_select_test_append(function, &next, IR_FSUB, IR_F64, x2, f, BUSTER_IR_NONE);
        const uint32_t x4 = tier1_select_test_append(function, &next, IR_FDIV, IR_F64, x3, g, BUSTER_IR_NONE);
        const uint32_t eb = tier1_select_test_append(function, &next, IR_FEXT, IR_F64, b, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t ee = tier1_select_test_append(function, &next, IR_FEXT, IR_F64, e, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t s1 = tier1_select_test_append(function, &next, IR_FADD, IR_F64, eb, ee, BUSTER_IR_NONE);
        const uint32_t nf = tier1_select_test_append(function, &next, IR_SITOFP, IR_F64, n, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t kf = tier1_select_test_append(function, &next, IR_SITOFP, IR_F64, k, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t uf = tier1_select_test_append(function, &next, IR_UITOFP, IR_F64, k, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t ng = tier1_select_test_append(function, &next, IR_FNEG, IR_F64, h, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t tr = tier1_select_test_append(function, &next, IR_FTRUNC, IR_F32, x4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bk = tier1_select_test_append(function, &next, IR_FEXT, IR_F64, tr, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t ej = tier1_select_test_append(function, &next, IR_FEXT, IR_F64, j, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t x5 = tier1_select_test_append(function, &next, IR_FSUB, IR_F64, i, ej, BUSTER_IR_NONE);
        const uint32_t q = tier1_select_test_append(function, &next, IR_FPTOSI, IR_I64, s1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t r = tier1_select_test_append(function, &next, IR_FPTOSI, IR_I32, x5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t w = tier1_select_test_append(function, &next, IR_FPTOUI, IR_I32, eb, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t s16 = tier1_select_test_append(function, &next, IR_FPTOSI, IR_I16, ej, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t u8 = tier1_select_test_append(function, &next, IR_FPTOUI, IR_I8, eb, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t compares[6];
        for (uint32_t p = 0; p < 6; p += 1)
        {
            const uint32_t truth = tier1_select_test_append(function, &next, IR_FCMP, IR_I1, a, c, IR_EQ + p);
            compares[p] = tier1_select_test_append(function, &next, IR_ZEXT, IR_I32, truth, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t bits = tier1_select_test_append(function, &next, IR_BITCAST, IR_I64, x4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t fb = tier1_select_test_append(function, &next, IR_BITCAST, IR_F64, q, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t kf32 = tier1_select_test_append(function, &next, IR_SITOFP, IR_F32, k, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t fs = tier1_select_test_append(function, &next, IR_FMUL, IR_F32, b, e, BUSTER_IR_NONE);
        const uint32_t fs2 = tier1_select_test_append(function, &next, IR_FADD, IR_F32, fs, kf32, BUSTER_IR_NONE);
        const uint32_t fq = tier1_select_test_append(function, &next, IR_FDIV, IR_F32, fs2, j, BUSTER_IR_NONE);
        const uint32_t fn = tier1_select_test_append(function, &next, IR_FNEG, IR_F32, fq, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bits32 = tier1_select_test_append(function, &next, IR_BITCAST, IR_I32, fn, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t nb = tier1_select_test_append(function, &next, IR_TRUNC, IR_I8, n, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t sb = tier1_select_test_append(function, &next, IR_SITOFP, IR_F64, nb, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t ub = tier1_select_test_append(function, &next, IR_UITOFP, IR_F64, nb, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t nw = tier1_select_test_append(function, &next, IR_TRUNC, IR_I16, n, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t sw = tier1_select_test_append(function, &next, IR_SITOFP, IR_F32, nw, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t fc = tier1_select_test_append(function, &next, IR_COPY, IR_F64, x5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_F32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t sz = tier1_select_test_append(function, &next, IR_FSUB, IR_F32, zero, fs, BUSTER_IR_NONE);
        const uint32_t stores[][2] = {
            { 0, x4 }, { 8, s1 }, { 16, bk }, { 24, x5 }, { 32, q }, { 40, r }, { 44, w }, { 48, s16 }, { 50, u8 }, { 52, compares[0] }, { 56, compares[1] }, { 60, compares[2] }, { 64, compares[3] },
            { 68, compares[4] }, { 72, compares[5] }, { 80, bits }, { 88, fb }, { 96, fs2 }, { 100, bits32 }, { 104, sb }, { 112, tr }, { 116, sw }, { 120, uf }, { 128, kf }, { 136, nf },
            { 144, ub }, { 152, fc }, { 76, sz },
        };
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(stores); s += 1)
        {
            tier1_float_store(function, &next, &addresses, out, stores[s][0], stores[s][1]);
        }
        // x4 and fs2 back from memory: the first a load of its own, the second, an F32, extended; the result's addition takes
        // the F64 load as its memory operand.
        const uint32_t again = tier1_float_address(function, &next, &addresses, out, 96);
        const uint32_t loaded32 = tier1_select_test_append(function, &next, IR_LOAD, IR_F32, again, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t widened = tier1_select_test_append(function, &next, IR_FEXT, IR_F64, loaded32, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_FADD, IR_F64, ng, widened, BUSTER_IR_NONE);
        const uint32_t first = tier1_float_address(function, &next, &addresses, out, 0);
        const uint32_t loaded = tier1_select_test_append(function, &next, IR_LOAD, IR_F64, first, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t result = tier1_select_test_append(function, &next, IR_FADD, IR_F64, sum, loaded, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[1] = next;
        function->address_count = addresses;
    }
    return created;
}

static void tier1_float_put(uint8_t* out, uint32_t offset, const void* value, uint32_t bytes)
{
    memcpy(out + offset, value, bytes);
}

// What tier1_float_mixed_function stores and returns, by C's own code of its operations, one per statement so that
// none is contracted with another.
static double tier1_float_mixed_oracle(double a, int64_t n, float b, double c, int32_t k, double d, float e, double f, double g, double h, double i, float j, uint8_t* out)
{
    const double x1 = a + c;
    const double x2 = x1 * d;
    const double x3 = x2 - f;
    const double x4 = x3 / g;
    const double eb = (double)b;
    const double ee = (double)e;
    const double s1 = eb + ee;
    const double nf = (double)n;
    const double kf = (double)k;
    const double uf = (double)(uint32_t)k;
    const double ng = -h;
    const float tr = (float)x4;
    const double bk = (double)tr;
    const double ej = (double)j;
    const double x5 = i - ej;
    const int64_t q = (int64_t)s1;
    const int32_t r = (int32_t)x5;
    const uint32_t w = (uint32_t)eb;
    const int16_t s16 = (int16_t)ej;
    const uint8_t u8 = (uint8_t)eb;
    const uint32_t compares[6] = { a == c, a != c, a < c, a <= c, a > c, a >= c };
    uint64_t bits = 0;
    memcpy(&bits, &x4, 8);
    double fb = 0.0;
    memcpy(&fb, &q, 8);
    const float kf32 = (float)k;
    const float fs = b * e;
    const float fs2 = fs + kf32;
    const float fq = fs2 / j;
    const float fn = -fq;
    uint32_t bits32 = 0;
    memcpy(&bits32, &fn, 4);
    const int8_t nb = (int8_t)n;
    const double sb = (double)nb;
    const double ub = (double)(uint8_t)nb;
    const int16_t nw = (int16_t)n;
    const float sw = (float)nw;
    const float sz = 0.0f - fs;
    tier1_float_put(out, 0, &x4, 8);
    tier1_float_put(out, 8, &s1, 8);
    tier1_float_put(out, 16, &bk, 8);
    tier1_float_put(out, 24, &x5, 8);
    tier1_float_put(out, 32, &q, 8);
    tier1_float_put(out, 40, &r, 4);
    tier1_float_put(out, 44, &w, 4);
    tier1_float_put(out, 48, &s16, 2);
    tier1_float_put(out, 50, &u8, 1);
    for (uint32_t p = 0; p < 6; p += 1)
    {
        tier1_float_put(out, 52 + 4 * p, &compares[p], 4);
    }
    tier1_float_put(out, 80, &bits, 8);
    tier1_float_put(out, 88, &fb, 8);
    tier1_float_put(out, 96, &fs2, 4);
    tier1_float_put(out, 100, &bits32, 4);
    tier1_float_put(out, 104, &sb, 8);
    tier1_float_put(out, 112, &tr, 4);
    tier1_float_put(out, 116, &sw, 4);
    tier1_float_put(out, 120, &uf, 8);
    tier1_float_put(out, 128, &kf, 8);
    tier1_float_put(out, 136, &nf, 8);
    tier1_float_put(out, 144, &ub, 8);
    tier1_float_put(out, 152, &x5, 8);
    tier1_float_put(out, 76, &sz, 4);
    const double widened = (double)fs2;
    const double sum = ng + widened;
    return sum + x4;
}

// The loop fixture (see the header): f(p, n, scale, fn, vfn), its blocks entry (0), header (1), body (2), then (3),
// else (4), join (5) and exit (6).
static bool tier1_float_loop_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 64,
        .block_count = 7,
        .extra_count = 32,
        .constant_count = 8,
        .address_count = 2,
        .call_count = 2,
        .call_type_count = 4,
        .parameter_count = 5,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        static const uint8_t types[5] = { IR_PTR, IR_I64, IR_F64, IR_PTR, IR_PTR };
        function->result_type = IR_F64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 4, .vector_registers = 1 };
        const double constants[] = { 0.0, 1.5, 0.25, 1.0009765625 };
        for (uint32_t k = 0; k < 4; k += 1)
        {
            memcpy(&function->constants[k].low, &constants[k], 8);
        }
        function->constants[4].low = 0;
        function->constants[5].low = 1;
        function->constants[6].low = 3;
        function->constants[7].low = 0;
        function->addresses[0] = (IrAddress){ .offset = 0, .scale = 8 };
        function->addresses[1] = (IrAddress){ .offset = 0, .scale = 8 };
        uint32_t next = 0;
        uint32_t extra = 0;
        // Entry.
        for (uint32_t k = 0; k < 5; k += 1)
        {
            function->parameter_types[k] = types[k];
            tier1_select_test_append(function, &next, IR_PARAM, types[k], k, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t p = 0;
        const uint32_t n = 1;
        const uint32_t scale = 2;
        const uint32_t fn = 3;
        const uint32_t vfn = 4;
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_F64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t start = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[1] = next;
        // Header: s, i and t, their back edges' values filled once the join has them.
        const uint32_t s = tier1_select_test_append(function, &next, IR_PHI, IR_F64, extra, 2, BUSTER_IR_NONE);
        const uint32_t s_pairs = extra;
        extra += 4;
        const uint32_t i = tier1_select_test_append(function, &next, IR_PHI, IR_I64, extra, 2, BUSTER_IR_NONE);
        const uint32_t i_pairs = extra;
        extra += 4;
        const uint32_t t = tier1_select_test_append(function, &next, IR_PHI, IR_F64, extra, 2, BUSTER_IR_NONE);
        const uint32_t t_pairs = extra;
        extra += 4;
        const uint32_t more = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, i, n, IR_LT);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, more, 2, 6);
        function->block_offsets[2] = next;
        // Body: !(x > 1.5) through C's truth values, to the else block.
        const uint32_t element = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, p, i, 0);
        const uint32_t x = tier1_select_test_append(function, &next, IR_LOAD, IR_F64, element, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t threshold = tier1_select_test_append(function, &next, IR_CONST, IR_F64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t greater = tier1_select_test_append(function, &next, IR_FCMP, IR_I1, x, threshold, IR_GT);
        const uint32_t extended = tier1_select_test_append(function, &next, IR_ZEXT, IR_I32, greater, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t zero32 = tier1_select_test_append(function, &next, IR_CONST, IR_I32, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t not_greater = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, extended, zero32, IR_EQ);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, not_greater, 4, 3);
        function->block_offsets[3] = next;
        // Then: s plus the element again, a load the addition takes as its memory operand.
        const uint32_t element2 = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, p, i, 1);
        const uint32_t y = tier1_select_test_append(function, &next, IR_LOAD, IR_F64, element2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t added = tier1_select_test_append(function, &next, IR_FADD, IR_F64, s, y, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[4] = next;
        // Else: s less a quarter.
        const uint32_t quarter = tier1_select_test_append(function, &next, IR_CONST, IR_F64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t subtracted = tier1_select_test_append(function, &next, IR_FSUB, IR_F64, s, quarter, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[5] = next;
        // Join: r = fn(sj, i, (float)t), t live across the call; s = sj + r, t = t * (1 + 2^-10), i + 1.
        const uint32_t joined = tier1_select_test_append(function, &next, IR_PHI, IR_F64, extra, 2, BUSTER_IR_NONE);
        function->extra[extra] = 3;
        function->extra[extra + 1] = added;
        function->extra[extra + 2] = 4;
        function->extra[extra + 3] = subtracted;
        extra += 4;
        const uint32_t narrowed = tier1_select_test_append(function, &next, IR_FTRUNC, IR_F32, t, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->extra[extra] = joined;
        function->extra[extra + 1] = i;
        function->extra[extra + 2] = narrowed;
        function->call_types[0] = IR_F64;
        function->call_types[1] = IR_I64;
        function->call_types[2] = IR_F32;
        function->calls[0] = (IrCall){ .argument_first = extra, .argument_count = 3, .type_first = 0, .parameter_count = 3, .result_type = IR_F64 };
        extra += 3;
        const uint32_t returned = tier1_select_test_append(function, &next, IR_CALL_INDIRECT, IR_F64, fn, 0, BUSTER_IR_NONE);
        const uint32_t s2 = tier1_select_test_append(function, &next, IR_FADD, IR_F64, joined, returned, BUSTER_IR_NONE);
        const uint32_t growth = tier1_select_test_append(function, &next, IR_CONST, IR_F64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t t2 = tier1_select_test_append(function, &next, IR_FMUL, IR_F64, t, growth, BUSTER_IR_NONE);
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t i2 = tier1_select_test_append(function, &next, IR_ADD, IR_I64, i, one, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[6] = next;
        // Exit: vfn(3, s, t, (double)n), variadic, its doubles in xmm0 to xmm2.
        const uint32_t count = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t converted = tier1_select_test_append(function, &next, IR_SITOFP, IR_F64, n, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->extra[extra] = count;
        function->extra[extra + 1] = s;
        function->extra[extra + 2] = t;
        function->extra[extra + 3] = converted;
        function->call_types[3] = IR_I64;
        function->calls[1] = (IrCall){ .argument_first = extra, .argument_count = 4, .type_first = 3, .parameter_count = 1, .result_type = IR_F64, .variadic = true };
        extra += 4;
        const uint32_t summed = tier1_select_test_append(function, &next, IR_CALL_INDIRECT, IR_F64, vfn, 1, BUSTER_IR_NONE);
        const uint32_t result = tier1_select_test_append(function, &next, IR_FADD, IR_F64, s, summed, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        // The header's phis: from the entry, and from the join.
        const uint32_t phis[3][3] = { { s_pairs, zero, s2 }, { i_pairs, start, i2 }, { t_pairs, scale, t2 } };
        for (uint32_t k = 0; k < 3; k += 1)
        {
            function->extra[phis[k][0]] = 0;
            function->extra[phis[k][0] + 1] = phis[k][1];
            function->extra[phis[k][0] + 2] = 5;
            function->extra[phis[k][0] + 3] = phis[k][2];
        }
        function->instruction_count = next;
        function->block_offsets[7] = next;
        function->extra_count = extra;
    }
    return created;
}

// What tier1_float_loop_function returns, from C's own calls of the same callees.
static double tier1_float_loop_oracle(const double* p, int64_t n, double scale)
{
    double s = 0.0;
    double t = scale;
    for (int64_t i = 0; i < n; i += 1)
    {
        const double x = p[i];
        double joined = 0.0;
        if (!(x > 1.5))
        {
            joined = s - 0.25;
        }
        else
        {
            joined = s + p[i];
        }
        const double returned = tier1_float_callee(joined, i, (float)t);
        s = joined + returned;
        t = t * 1.0009765625;
    }
    const double summed = tier1_float_callee_variadic(3, s, t, (double)n);
    return s + summed;
}

static __attribute__((no_sanitize("function"))) double tier1_float_call_mixed(const void* code, double a, int64_t n, float b, double c, int32_t k, double d, float e, double f, double g, double h, double i, float j, uint8_t* out)
{
    double (*function)(double, int64_t, float, double, int32_t, double, float, double, double, double, double, float, uint8_t*);
    memcpy(&function, &code, sizeof(function));
    return function(a, n, b, c, k, d, e, f, g, h, i, j, out);
}

static __attribute__((no_sanitize("function"))) double tier1_float_call_loop(const void* code, const double* p, int64_t n, double scale)
{
    double (*function)(const double*, int64_t, double, const void*, const void*);
    memcpy(&function, &code, sizeof(function));
    return function(p, n, scale, (const void*)tier1_float_callee, (const void*)tier1_float_callee_variadic);
}

// A double from the sequence: a moderate value with a fraction, where `nonnegative` asks it to be 0 or more.
// tier1_float_undefined_oracle's loop in the IR, whose x and k are undefined along b2's edge into b4.
static const char tier1_float_undefined_text[] =
    "function (ptr, i64) -> f64 {\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = param.i64 1\n"
    "  %2 = const.i64 0x00000000000000000000000000000000\n"
    "  %3 = const.f64 0x00000000000000000000000000000000\n"
    "  %4 = undef.f64\n"
    "  %5 = undef.i64\n"
    "  %6 = const.i64 0x00000000000000000000000000000001\n"
    "  %7 = br.void b1\n"
    "b1:\n"
    "  %8 = phi.i64 [%2, b0], [%29, b7]\n"
    "  %9 = phi.f64 [%3, b0], [%28, b7]\n"
    "  %10 = icmp.i1 %8, %1 ult\n"
    "  %11 = cbr.void %10, b2, b8\n"
    "b2:\n"
    "  %12 = address.ptr %0 + %8 * 8 offset=0\n"
    "  %13 = load.f64 %12 align=8\n"
    "  %14 = and.i64 %8, %6\n"
    "  %15 = icmp.i1 %14, %2 ne\n"
    "  %16 = cbr.void %15, b3, b4\n"
    "b3:\n"
    "  %17 = fmul.f64 %13, %13\n"
    "  %18 = br.void b4\n"
    "b4:\n"
    "  %19 = phi.f64 [%4, b2], [%17, b3]\n"
    "  %20 = phi.i64 [%5, b2], [%8, b3]\n"
    "  %21 = cbr.void %15, b5, b6\n"
    "b5:\n"
    "  %22 = sitofp.f64 %20\n"
    "  %23 = fadd.f64 %19, %22\n"
    "  %24 = fadd.f64 %9, %23\n"
    "  %25 = br.void b7\n"
    "b6:\n"
    "  %26 = fsub.f64 %9, %13\n"
    "  %27 = br.void b7\n"
    "b7:\n"
    "  %28 = phi.f64 [%24, b5], [%26, b6]\n"
    "  %29 = add.i64 %8, %6\n"
    "  %30 = br.void b1\n"
    "b8:\n"
    "  %31 = ret.void %9\n"
    "}\n";

static double tier1_float_undefined_oracle(const double* p, int64_t n)
{
    double sum = 0.0;
    for (int64_t i = 0; i < n; i += 1)
    {
        const double element = p[i];
        if ((i & 1) != 0)
        {
            const double square = element * element;
            const double term = square + (double)i;
            sum = sum + term;
        }
        else
        {
            sum = sum - element;
        }
    }
    return sum;
}

static __attribute__((no_sanitize("function"))) double tier1_float_call_undefined(const void* code, const double* p, int64_t n)
{
    double (*function)(const double*, int64_t) = (double (*)(const double*, int64_t))code;
    return function(p, n);
}

static double tier1_float_value(uint64_t* random, bool nonnegative)
{
    const uint64_t bits = tier1_emit_test_random(random);
    const double magnitude = (double)(bits & 0xfffff) / 64.0 + (double)((bits >> 20) & 0xff) / 256.0;
    return nonnegative || ((bits >> 40) & 1) == 0 ? magnitude : -magnitude;
}

static bool tier1_float_same(double left, double right)
{
    return memcmp(&left, &right, sizeof(left)) == 0;
}

// One compile of fixture 0 (mixed) or 1 (the loop), by one path and one set of options, run on the samples.
static void tier1_float_compile(Tier1EmitTest* test, uint32_t fixture, bool vector, bool rematerialize, uint32_t vector_mask, bool keep_pointer)
{
    IrFunction function;
    const String8 undefined = { .pointer = (char*)tier1_float_undefined_text, .length = sizeof(tier1_float_undefined_text) - 1 };
    const bool created = fixture == 0 ? tier1_float_mixed_function(&test->input, &function) : fixture == 1 ? tier1_float_loop_function(&test->input, &function) :
                                                                                            ir_parse(undefined, &test->input, &test->scratch, &function).error == IR_PARSE_OK;
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the floating fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selected = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "the floating fixture selects");
    if (selected != TIER1_SELECT_OK && dominated && test->failures < 20)
    {
        fprintf(stderr, "  float selection error=%u node=%u fixture=%u vector=%u\n", selected, node, fixture, vector);
    }
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .vector_register_mask = vector_mask,
        .vector = vector,
        .rematerialize = rematerialize,
        .frame_flags = keep_pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
        .symbol_count = function.symbol_count,
    };
    Tier1EmitResult emitted;
    const uint64_t scratch_mark = test->scratch.length;
    const Tier1EmitError error = selected == TIER1_SELECT_OK ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && test->scratch.length == scratch_mark, "the floating fixture emits through the CFG consumer");
    if (error != TIER1_EMIT_OK && selected == TIER1_SELECT_OK && test->failures < 20)
    {
        fprintf(stderr, "  float emission error=%u node=%u fixture=%u vector=%u remat=%u mask=%x pointer=%u\n", error, node, fixture, vector, rematerialize, vector_mask, keep_pointer);
    }
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_test_expect(test, emitted.general, "a floating value takes the CFG consumer");
        // Every floating value's register is an xmm one of the allowed: its fragments' registers are its class's.
        bool classes = true;
        for (uint32_t k = 0; k < emitted.cfg.intervals.count; k += 1)
        {
            const uint32_t value_node = emitted.value_nodes[emitted.cfg.intervals.values[k]];
            const bool floating = tier1_select_float_width(function.type[value_node]) != 0;
            classes &= floating == (emitted.cfg.intervals.classes[k] == BUSTER_TIER1_ALLOCATE_VECTOR);
            for (uint32_t f = emitted.cfg.allocation.fragment_offsets[k]; floating && f < emitted.cfg.allocation.fragment_offsets[k + 1]; f += 1)
            {
                const uint32_t allowed = vector_mask != 0 ? vector_mask : BUSTER_TIER1_EMIT_VECTOR_REGISTERS;
                classes &= emitted.cfg.allocation.kinds[f] != BUSTER_TIER1_ALLOCATE_REGISTER || ((allowed >> emitted.cfg.allocation.registers[f]) & 1) != 0;
            }
        }
        tier1_emit_test_expect(test, classes, "floating values are the vector class's, in the allowed xmm registers");
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        tier1_float_misaligned = 0;
        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x6a09e667f3bcc908) ^ ((uint64_t)vector_mask << 8) ^ (vector ? 1 : 0) ^ (rematerialize ? 2 : 0) ^ (keep_pointer ? 4 : 0) ^ ((uint64_t)fixture << 40);
            for (uint32_t sample = 0; sample < BUSTER_TIER1_FLOAT_SAMPLES; sample += 1)
            {
                if (fixture == 0)
                {
                    double values[8];
                    for (uint32_t v = 0; v < 8; v += 1)
                    {
                        values[v] = tier1_float_value(&random, false);
                    }
                    // Equal, unordered and signed-zero samples among the random ones.
                    const double c = sample == 0 ? values[0] : sample == 1 ? __builtin_nan("") : values[1];
                    const double h = sample == 2 ? 0.0 : values[5];
                    const int64_t n = (int64_t)tier1_emit_test_random(&random);
                    const int32_t k = (int32_t)tier1_emit_test_random(&random);
                    const float b = (float)(tier1_float_value(&random, true) / 4096.0);
                    const float e = (float)tier1_float_value(&random, false);
                    const float j = (float)tier1_float_value(&random, false) / 512.0f + 1.0f;
                    uint8_t expected[BUSTER_TIER1_FLOAT_OUT_BYTES];
                    uint8_t actual[BUSTER_TIER1_FLOAT_OUT_BYTES];
                    memset(expected, 0xa5, sizeof(expected));
                    memset(actual, 0xa5, sizeof(actual));
                    const double oracle = tier1_float_mixed_oracle(values[0], n, b, c, k, values[2], e, values[3], values[4] == 0.0 ? 1.0 : values[4], h, values[6], j, expected);
                    const double got = tier1_float_call_mixed(executable, values[0], n, b, c, k, values[2], e, values[3], values[4] == 0.0 ? 1.0 : values[4], h, values[6], j, actual);
                    const bool same = tier1_float_same(oracle, got) && memcmp(expected, actual, sizeof(expected)) == 0;
                    if (!same && test->failures < 20)
                    {
                        uint32_t first = 0;
                        while (first < sizeof(expected) && expected[first] == actual[first])
                        {
                            first += 1;
                        }
                        fprintf(stderr, "  float mixed vector=%u remat=%u mask=%x pointer=%u sample=%u result %a vs %a, first differing byte %u\n", vector, rematerialize, vector_mask, keep_pointer, sample, got, oracle, first);
                    }
                    tier1_emit_test_expect(test, same, "every floating rule's result is C's, bit for bit");
                }
                else if (fixture == 2)
                {
                    double elements[BUSTER_TIER1_FLOAT_ELEMENTS];
                    for (uint32_t v = 0; v < BUSTER_TIER1_FLOAT_ELEMENTS; v += 1)
                    {
                        elements[v] = tier1_float_value(&random, false) / 256.0;
                    }
                    const int64_t n = sample % (BUSTER_TIER1_FLOAT_ELEMENTS + 1);
                    const double oracle = tier1_float_undefined_oracle(elements, n);
                    const double got = tier1_float_call_undefined(executable, elements, n);
                    if (!tier1_float_same(oracle, got) && test->failures < 20)
                    {
                        fprintf(stderr, "  float undefined vector=%u remat=%u mask=%x pointer=%u n=%" PRId64 " result %a vs %a\n", vector, rematerialize, vector_mask, keep_pointer, n, got, oracle);
                    }
                    tier1_emit_test_expect(test, tier1_float_same(oracle, got), "phis with undefined operands give C's result, bit for bit");
                }
                else
                {
                    double elements[BUSTER_TIER1_FLOAT_ELEMENTS];
                    for (uint32_t v = 0; v < BUSTER_TIER1_FLOAT_ELEMENTS; v += 1)
                    {
                        elements[v] = (sample + v) % 5 == 0 ? __builtin_nan("") : tier1_float_value(&random, false) / 256.0;
                    }
                    const int64_t n = sample % (BUSTER_TIER1_FLOAT_ELEMENTS + 1);
                    const double scale = tier1_float_value(&random, false) / 16.0;
                    const double oracle = tier1_float_loop_oracle(elements, n, scale);
                    const double got = tier1_float_call_loop(executable, elements, n, scale);
                    if (!tier1_float_same(oracle, got) && test->failures < 20)
                    {
                        fprintf(stderr, "  float loop vector=%u remat=%u mask=%x pointer=%u n=%" PRId64 " result %a vs %a\n", vector, rematerialize, vector_mask, keep_pointer, n, got, oracle);
                    }
                    tier1_emit_test_expect(test, tier1_float_same(oracle, got), "floating phis, branches, calls and their saves give C's result, bit for bit");
                }
            }
            tier1_emit_test_expect(test, tier1_float_misaligned == 0, "every call keeps the stack 16 bytes aligned");
        }
        if (executable != MAP_FAILED)
        {
            munmap(executable, bytes);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_float_cases(Tier1EmitTest* test)
{
    test->terms = 0;
    test->width = 8;
    test->selects = false;
    for (uint32_t fixture = 0; fixture < 3; fixture += 1)
    {
        // Few xmm registers: the mixed fixture's eight parameters, which their definitions take at once, and one more;
        // the loop's three, which its variadic call's doubles take at once (and the undefined operands' fixture's too).
        // The other values go to their slots, and those live across a call too.
        const uint32_t few = fixture == 0 ? 0x1ffu : 0x7u;
        for (uint32_t variant = 0; variant < 16; variant += 1)
        {
            tier1_float_compile(test, fixture, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0 ? few : 0, (variant & 8) != 0);
        }
    }
}

// The cases in a bounded child, as tier1_stack_tests runs its own: a misplaced register or slot can fault or loop, which
// must fail this suite, not stop it. The counter page survives a normal exit.
static void tier1_float_tests(Tier1EmitTest* test)
{
    uint64_t* report = mmap(0, 3 * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const pid_t child = report != MAP_FAILED ? fork() : -1;
    if (child == 0)
    {
        signal(SIGALRM, SIG_DFL);
        alarm(60);
        const uint64_t checks = test->checks;
        const uint64_t failures = test->failures;
        tier1_float_cases(test);
        report[0] = test->checks - checks;
        report[1] = test->failures - failures;
        report[2] = UINT64_C(0x3c6ef372fe94f82b);
        fflush(0);
        _exit(report[1] != 0);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    const bool finished = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1 && report[2] == UINT64_C(0x3c6ef372fe94f82b);
    tier1_emit_test_expect(test, finished, "bounded floating child completes without signal or timeout");
    if (finished)
    {
        test->checks += report[0];
        test->failures += report[1];
        tier1_emit_test_expect(test, WEXITSTATUS(status) == (report[1] != 0), "floating child status agrees with shared test counters");
    }
    else
    {
        fprintf(stderr, "tier1 floating child failed (status=%d)\n", status);
    }
    if (report != MAP_FAILED)
    {
        munmap(report, 3 * sizeof(uint64_t));
    }
}
