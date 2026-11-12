// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// SIMD SCCP (#1450), included after the scalar lattice publisher. Wide values are sparse: only vector-result rows
// have a 64-byte cell, allocated on the first vector transfer. Scalar functions do not scan or allocate a wide map.
// Integer lanes reuse the independent batched integer transfer, not ir_simd_evaluate (the test oracle).
//
// Floating lanes fold only where every floating environment gives the same bits. The runtime MXCSR chooses the
// rounding, may flush a subnormal result to zero (FTZ) and may read a subnormal operand as zero (DAZ), and the IR
// carries no FENV_ACCESS state, so a lane folds when its result is exact (every rounding agrees), no operand is
// subnormal and the result isn't. A NaN operand decides its lane whatever the controls: x86's rule, the first
// operand's quieted where both are NaNs; an invalid operation gives the default NaN. A comparison folds unless a
// lane without a NaN has a subnormal operand. The rewrite keeps the original row for its flags and traps. Neither path
// reads or writes the compiler's own MXCSR: the scalar twin is integer arithmetic, and the vector path rounds down and
// up by embedded rounding with exceptions suppressed, refusing a zero that only the host's FTZ could have made.
// Layout: ir_optimize_simd_effect, ir_optimize_simd_storage, ir_optimize_simd_value, ir_optimize_simd_meet,
// ir_optimize_simd_float_encode, ir_optimize_simd_float_lane, ir_optimize_simd_float_compare,
// ir_optimize_simd_float_vector, ir_optimize_simd_float, ir_optimize_simd_fold, ir_optimize_simd_transfer,
// ir_optimize_simd_visit.

static bool ir_optimize_simd_effect(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    const bool result = f->flags[i] >= IR_SIMD_LANE_F32 &&
                        (op == IR_VCMP || op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV);
    return result;
}

static bool ir_optimize_simd_storage(IrOptimizeWork* work)
{
    if (work->vector_indices == NULL && work->simd_error == IR_OK)
    {
        const IrFunction* f = work->function;
        const uint32_t count = f->instruction_count;
        const uint8_t* const types = f->type;
        uint32_t vectors = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            vectors += ir_vector_type(types[i]);
        }
        bool ok = true;
        work->vector_indices = ir_allocate(work->scratch, count, sizeof(uint32_t), &ok);
        work->vector_values = ir_allocate(work->scratch, vectors, sizeof(IrSimdValue), &ok);
        if (ok)
        {
            uint32_t next = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                work->vector_indices[i] = ir_vector_type(types[i]) ? next++ : BUSTER_IR_NONE;
            }
        }
        else
        {
            work->simd_error = IR_ERROR_MEMORY;
        }
    }
    return work->simd_error == IR_OK;
}

static IrSimdValue ir_optimize_simd_value(const IrOptimizeWork* work, uint32_t i)
{
    IrSimdValue result = { 0 };
    if (ir_vector_type(work->function->type[i]))
    {
        result = work->vector_values[work->vector_indices[i]];
    }
    else
    {
        result.words[0] = work->values[i].low;
        result.words[1] = work->values[i].high;
    }
    return result;
}

static uint8_t ir_optimize_simd_meet(uint8_t left, const IrSimdValue* a, uint8_t right, const IrSimdValue* b, IrSimdValue* value)
{
    uint8_t result = IROPT_OVERDEFINED;
    if (left == IROPT_UNKNOWN)
    {
        result = right;
        *value = *b;
    }
    else if (right == IROPT_UNKNOWN)
    {
        result = left;
        *value = *a;
    }
    else if (left == IROPT_CONSTANT && right == IROPT_CONSTANT && memcmp(a, b, sizeof(*a)) == 0)
    {
        result = IROPT_CONSTANT;
        *value = *a;
    }
    return result;
}

// sign·m·2^e for m > 0, exactly, in a float's (`width` 32) or a double's (64) encoding: refused where m has more
// significant bits than the format's precision or the value isn't normal (an overflow, a subnormal, an underflow).
static bool ir_optimize_simd_float_encode(uint64_t sign, unsigned __int128 m, int32_t e, uint32_t width, uint64_t* value)
{
    const uint32_t precision = width == 32 ? 24 : 53;
    const int32_t bias = width == 32 ? 127 : 1023;
    const uint64_t low = (uint64_t)m;
    const uint64_t high = (uint64_t)(m >> 64);
    const uint32_t trailing = low != 0 ? (uint32_t)__builtin_ctzll(low) : 64 + (uint32_t)__builtin_ctzll(high);
    const unsigned __int128 odd = m >> trailing;
    const uint64_t odd_high = (uint64_t)(odd >> 64);
    const uint32_t length = odd_high != 0 ? 128 - (uint32_t)__builtin_clzll(odd_high) : 64 - (uint32_t)__builtin_clzll((uint64_t)odd);
    const int32_t biased = e + (int32_t)trailing + (int32_t)length - 1 + bias;
    const bool result = length <= precision && biased >= 1 && biased < 2 * bias + 1;
    if (result)
    {
        const uint64_t fraction = (UINT64_C(1) << (precision - 1)) - 1;
        *value = sign | ((uint64_t)biased << (precision - 1)) | (((uint64_t)odd << (precision - length)) & fraction);
    }
    return result;
}

// One floating lane by integer arithmetic alone (the scalar twin and the vector path's oracle): whether every floating
// environment gives it the same bits, and those bits. A normal operand is M·2^e with M of the format's precision.
static bool ir_optimize_simd_float_lane(uint32_t op, uint64_t a, uint64_t b, uint32_t width, uint64_t* value)
{
    const uint32_t precision = width == 32 ? 24 : 53;
    const int32_t bias = width == 32 ? 127 : 1023;
    const uint64_t sign = UINT64_C(1) << (width - 1);
    const uint64_t fraction = (UINT64_C(1) << (precision - 1)) - 1;
    const uint64_t exponent = (sign - 1) & ~fraction;
    const uint64_t quiet = UINT64_C(1) << (precision - 2);
    const uint64_t default_nan = sign | exponent | quiet;
    const bool a_nan = (a & exponent) == exponent && (a & fraction) != 0;
    const bool b_nan = (b & exponent) == exponent && (b & fraction) != 0;
    const bool a_infinite = (a & ~sign) == exponent;
    const bool b_infinite = (b & ~sign) == exponent;
    const bool a_zero = (a & ~sign) == 0;
    const bool b_zero = (b & ~sign) == 0;
    const bool subnormal = ((a & exponent) == 0 && !a_zero) || ((b & exponent) == 0 && !b_zero);
    // A subtraction is the addition of the negated subtrahend once NaNs, whose own sign is kept, are excluded.
    const uint64_t addend = op == IR_FSUB ? b ^ sign : b;
    const uint64_t product_sign = (a ^ b) & sign;
    const unsigned __int128 a_m = (unsigned __int128)((a & fraction) | (fraction + 1));
    const unsigned __int128 b_m = (unsigned __int128)((b & fraction) | (fraction + 1));
    const int32_t a_e = (int32_t)((a & exponent) >> (precision - 1)) - bias - (int32_t)(precision - 1);
    const int32_t b_e = (int32_t)((b & exponent) >> (precision - 1)) - bias - (int32_t)(precision - 1);
    bool result = true;
    uint64_t bits = 0;
    if (a_nan || b_nan)
    {
        bits = (a_nan ? a : b) | quiet;
    }
    else if (subnormal)
    {
        result = false;
    }
    else if (op == IR_FADD || op == IR_FSUB)
    {
        if (a_infinite && b_infinite)
        {
            bits = ((a ^ addend) & sign) != 0 ? default_nan : a;
        }
        else if (a_infinite || (b_zero && !a_zero))
        {
            bits = a;
        }
        else if (b_infinite || (a_zero && !b_zero))
        {
            bits = addend;
        }
        else if (a_zero)
        {
            // Zeros of different signs sum to +0, but to -0 when rounding down.
            result = ((a ^ addend) & sign) == 0;
            bits = a;
        }
        else
        {
            // Past the precision's distance apart, the smaller operand's lowest bit and the larger's highest are
            // further apart than the format holds, so the sum isn't exact.
            const bool a_larger = a_e >= b_e;
            const uint32_t distance = (uint32_t)(a_larger ? a_e - b_e : b_e - a_e);
            const __int128 larger = (__int128)(a_larger ? a_m : b_m) << (distance <= 64 ? distance : 0);
            const __int128 smaller = (__int128)(a_larger ? b_m : a_m);
            const bool larger_negative = ((a_larger ? a : addend) & sign) != 0;
            const bool smaller_negative = ((a_larger ? addend : a) & sign) != 0;
            const __int128 sum = (larger_negative ? -larger : larger) + (smaller_negative ? -smaller : smaller);
            // An exact cancellation is +0, but -0 when rounding down.
            result = distance <= 64 && sum != 0 &&
                     ir_optimize_simd_float_encode(sum < 0 ? sign : 0, (unsigned __int128)(sum < 0 ? -sum : sum), a_larger ? b_e : a_e, width, &bits);
        }
    }
    else if (op == IR_FMUL)
    {
        if ((a_infinite && b_zero) || (a_zero && b_infinite))
        {
            bits = default_nan;
        }
        else if (a_infinite || b_infinite)
        {
            bits = product_sign | exponent;
        }
        else if (a_zero || b_zero)
        {
            bits = product_sign;
        }
        else
        {
            result = ir_optimize_simd_float_encode(product_sign, a_m * b_m, a_e + b_e, width, &bits);
        }
    }
    else
    {
        if ((a_infinite && b_infinite) || (a_zero && b_zero))
        {
            bits = default_nan;
        }
        else if (a_infinite || b_zero)
        {
            bits = product_sign | exponent;
        }
        else if (b_infinite || a_zero)
        {
            bits = product_sign;
        }
        else
        {
            // a/b is a dyadic rational only where b's odd part divides a's.
            const uint32_t a_trailing = (uint32_t)__builtin_ctzll((uint64_t)a_m);
            const uint32_t b_trailing = (uint32_t)__builtin_ctzll((uint64_t)b_m);
            const uint64_t a_odd = (uint64_t)a_m >> a_trailing;
            const uint64_t b_odd = (uint64_t)b_m >> b_trailing;
            result = a_odd % b_odd == 0 &&
                     ir_optimize_simd_float_encode(product_sign, a_odd / b_odd, a_e + (int32_t)a_trailing - b_e - (int32_t)b_trailing, width, &bits);
        }
    }
    *value = bits;
    return result;
}

// C's comparisons of floating lanes from their bits (oeq, une, olt, ole, ogt, oge): signed zeros compare equal, and
// after NaNs are excluded, complementing a negative encoding and biasing a positive one gives numeric order.
static bool ir_optimize_simd_float_compare(uint64_t a, uint64_t b, uint32_t width, uint32_t predicate)
{
    const uint64_t sign = UINT64_C(1) << (width - 1);
    const uint64_t mask = UINT64_MAX >> (64 - width);
    const uint64_t exponent = width == 32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction = width == 32 ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    const bool unordered = ((a & exponent) == exponent && (a & fraction) != 0) || ((b & exponent) == exponent && (b & fraction) != 0);
    const bool equal = a == b || ((a | b) & ~sign) == 0;
    const uint64_t left = (a & sign) != 0 ? (~a & mask) : (a ^ sign);
    const uint64_t right = (b & sign) != 0 ? (~b & mask) : (b ^ sign);
    bool result = false;
    switch (predicate)
    {
        case IR_EQ: result = !unordered && equal; break;
        case IR_NE: result = unordered || !equal; break;
        case IR_LT: result = !unordered && !equal && left < right; break;
        case IR_LE: result = !unordered && (equal || left < right); break;
        case IR_GT: result = !unordered && !equal && left > right; break;
        case IR_GE: result = !unordered && (equal || left > right); break;
        default: break;
    }
    return result;
}

#if BUSTER_HOST_VECTOR
// Every lane at once (`bits` / `width` of them; the rest of the 512 bits stay zero). The arithmetic rounds down and up
// by embedded rounding, which ignores MXCSR's rounding control and, with exceptions suppressed, sets no flag and
// traps on none; a lane is exact where the two agree. Comparisons are integer operations on the bits. The host's own FTZ and DAZ still apply: a subnormal operand's
// lane is refused whatever DAZ made of it, and a zero is refused unless an operand makes it exact, since FTZ flushes
// a tiny result in both directions alike. NaNs are rebuilt by x86's rule from the operands afterwards, so that the
// host compiler's commuting an addition's or a multiplication's operands can't choose the other payload.
static bool ir_optimize_simd_float_vector(uint32_t op, uint32_t width, uint32_t bits, uint32_t predicate, const IrSimdValue* left, const IrSimdValue* right, IrSimdValue* output)
{
    const __m512i a = _mm512_loadu_si512(left);
    const __m512i b = _mm512_loadu_si512(right);
    const __m512i sign = width == 32 ? _mm512_set1_epi32((int32_t)UINT32_C(0x80000000)) : _mm512_set1_epi64((int64_t)UINT64_C(0x8000000000000000));
    // A magnitude above the exponent field's is a NaN.
    const __m512i exponent = width == 32 ? _mm512_set1_epi32(0x7f800000) : _mm512_set1_epi64(INT64_C(0x7ff0000000000000));
    const __m512i quiet = width == 32 ? _mm512_set1_epi32(0x00400000) : _mm512_set1_epi64(INT64_C(0x0008000000000000));
    const __m512i a_magnitude = _mm512_andnot_si512(sign, a);
    const __m512i b_magnitude = _mm512_andnot_si512(sign, b);
    const __m512i a_exponent = _mm512_and_si512(a, exponent);
    const __m512i b_exponent = _mm512_and_si512(b, exponent);
    const __mmask16 valid = (__mmask16)((UINT32_C(1) << (bits / width)) - 1);
    __mmask16 a_nan;
    __mmask16 b_nan;
    __mmask16 a_zero;
    __mmask16 b_zero;
    __mmask16 b_infinite;
    __mmask16 subnormal;
    if (width == 32)
    {
        a_nan = _mm512_cmpgt_epu32_mask(a_magnitude, exponent);
        b_nan = _mm512_cmpgt_epu32_mask(b_magnitude, exponent);
        a_zero = _mm512_testn_epi32_mask(a_magnitude, a_magnitude);
        b_zero = _mm512_testn_epi32_mask(b_magnitude, b_magnitude);
        b_infinite = _mm512_cmpeq_epi32_mask(b_magnitude, exponent);
        subnormal = (_mm512_testn_epi32_mask(a_exponent, a_exponent) & (__mmask16)~a_zero) | (_mm512_testn_epi32_mask(b_exponent, b_exponent) & (__mmask16)~b_zero);
    }
    else
    {
        a_nan = _mm512_cmpgt_epu64_mask(a_magnitude, exponent);
        b_nan = _mm512_cmpgt_epu64_mask(b_magnitude, exponent);
        a_zero = _mm512_testn_epi64_mask(a_magnitude, a_magnitude);
        b_zero = _mm512_testn_epi64_mask(b_magnitude, b_magnitude);
        b_infinite = _mm512_cmpeq_epi64_mask(b_magnitude, exponent);
        subnormal = (_mm512_testn_epi64_mask(a_exponent, a_exponent) & (__mmask8)~a_zero) | (_mm512_testn_epi64_mask(b_exponent, b_exponent) & (__mmask8)~b_zero);
    }
    const __mmask16 nan = a_nan | b_nan;
    __mmask16 refused = subnormal & (__mmask16)~nan;
    __m512i staged = _mm512_setzero_si512();
    if (op == IR_VCMP)
    {
        // C's predicates from the bits, as ir_optimize_simd_float_compare orders them: Clang drops a comparison's
        // suppressed exceptions, which would set the compiler's own invalid flag, and DAZ would read a subnormal as zero.
        __mmask16 equal;
        __mmask16 less;
        __mmask16 greater;
        if (width == 32)
        {
            const __m512i a_key = _mm512_xor_si512(a, _mm512_or_si512(_mm512_srai_epi32(a, 31), sign));
            const __m512i b_key = _mm512_xor_si512(b, _mm512_or_si512(_mm512_srai_epi32(b, 31), sign));
            const __m512i either = _mm512_andnot_si512(sign, _mm512_or_si512(a, b));
            equal = _mm512_cmpeq_epi32_mask(a, b) | _mm512_testn_epi32_mask(either, either);
            less = _mm512_cmplt_epu32_mask(a_key, b_key);
            greater = _mm512_cmpgt_epu32_mask(a_key, b_key);
        }
        else
        {
            const __m512i a_key = _mm512_xor_si512(a, _mm512_or_si512(_mm512_srai_epi64(a, 63), sign));
            const __m512i b_key = _mm512_xor_si512(b, _mm512_or_si512(_mm512_srai_epi64(b, 63), sign));
            const __m512i either = _mm512_andnot_si512(sign, _mm512_or_si512(a, b));
            equal = _mm512_cmpeq_epi64_mask(a, b) | _mm512_testn_epi64_mask(either, either);
            less = _mm512_cmplt_epu64_mask(a_key, b_key);
            greater = _mm512_cmpgt_epu64_mask(a_key, b_key);
        }
        const __mmask16 ordered = (__mmask16)~nan;
        __mmask16 compared = 0;
        switch (predicate)
        {
            case IR_EQ: compared = ordered & equal; break;
            case IR_NE: compared = nan | (__mmask16)~equal; break;
            case IR_LT: compared = ordered & (__mmask16)~equal & less; break;
            case IR_LE: compared = ordered & (equal | less); break;
            case IR_GT: compared = ordered & (__mmask16)~equal & greater; break;
            case IR_GE: compared = ordered & (equal | greater); break;
            default: refused = valid; break;
        }
        staged = _mm512_zextsi128_si512(_mm_cvtsi32_si128((int32_t)(compared & valid)));
    }
    else
    {
        __m512i down;
        __m512i up;
        if (width == 32)
        {
            const __m512 x = _mm512_castsi512_ps(a);
            const __m512 y = _mm512_castsi512_ps(b);
            if (op == IR_FADD)
            {
                down = _mm512_castps_si512(_mm512_maskz_add_round_ps(valid, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castps_si512(_mm512_maskz_add_round_ps(valid, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
            else if (op == IR_FSUB)
            {
                down = _mm512_castps_si512(_mm512_maskz_sub_round_ps(valid, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castps_si512(_mm512_maskz_sub_round_ps(valid, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
            else if (op == IR_FMUL)
            {
                down = _mm512_castps_si512(_mm512_maskz_mul_round_ps(valid, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castps_si512(_mm512_maskz_mul_round_ps(valid, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
            else
            {
                down = _mm512_castps_si512(_mm512_maskz_div_round_ps(valid, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castps_si512(_mm512_maskz_div_round_ps(valid, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
        }
        else
        {
            const __m512d x = _mm512_castsi512_pd(a);
            const __m512d y = _mm512_castsi512_pd(b);
            const __mmask8 lanes = (__mmask8)valid;
            if (op == IR_FADD)
            {
                down = _mm512_castpd_si512(_mm512_maskz_add_round_pd(lanes, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castpd_si512(_mm512_maskz_add_round_pd(lanes, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
            else if (op == IR_FSUB)
            {
                down = _mm512_castpd_si512(_mm512_maskz_sub_round_pd(lanes, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castpd_si512(_mm512_maskz_sub_round_pd(lanes, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
            else if (op == IR_FMUL)
            {
                down = _mm512_castpd_si512(_mm512_maskz_mul_round_pd(lanes, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castpd_si512(_mm512_maskz_mul_round_pd(lanes, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
            else
            {
                down = _mm512_castpd_si512(_mm512_maskz_div_round_pd(lanes, x, y, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
                up = _mm512_castpd_si512(_mm512_maskz_div_round_pd(lanes, x, y, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
            }
        }
        const __m512i magnitude = _mm512_andnot_si512(sign, down);
        const __m512i down_exponent = _mm512_and_si512(down, exponent);
        // A zero is exact by an operand alone: a sum of two zeros, a product with one, 0/b or a/inf.
        const __mmask16 exact_zero = a_zero | b_zero | (op == IR_FDIV ? b_infinite : 0);
        __mmask16 inexact;
        __mmask16 tiny;
        if (width == 32)
        {
            inexact = _mm512_cmpneq_epi32_mask(down, up);
            const __mmask16 zero = _mm512_testn_epi32_mask(magnitude, magnitude);
            tiny = (_mm512_testn_epi32_mask(down_exponent, down_exponent) & (__mmask16)~zero) | (zero & (__mmask16)~exact_zero);
        }
        else
        {
            inexact = _mm512_cmpneq_epi64_mask(down, up);
            const __mmask8 zero = _mm512_testn_epi64_mask(magnitude, magnitude);
            tiny = (_mm512_testn_epi64_mask(down_exponent, down_exponent) & (__mmask8)~zero) | (zero & (__mmask8)~exact_zero);
        }
        refused |= (inexact | tiny) & (__mmask16)~nan;
        const __m512i first = width == 32 ? _mm512_mask_blend_epi32(a_nan, b, a) : _mm512_mask_blend_epi64((__mmask8)a_nan, b, a);
        const __m512i propagated = _mm512_or_si512(first, quiet);
        staged = width == 32 ? _mm512_mask_mov_epi32(down, nan & valid, propagated) : _mm512_mask_mov_epi64(down, (__mmask8)(nan & valid), propagated);
    }
    const bool result = (refused & valid) == 0;
    if (result)
    {
        _mm512_storeu_si512(output, staged);
    }
    return result;
}
#endif

// A floating operation's or comparison's lanes, or false where one of them depends on the floating environment.
static bool ir_optimize_simd_float(uint32_t op, uint32_t width, uint32_t bits, uint32_t predicate, const IrSimdValue* left, const IrSimdValue* right, bool scalar, IrSimdValue* output)
{
    bool result = true;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        result = ir_optimize_simd_float_vector(op, width, bits, predicate, left, right, output);
    }
    else
#else
    (void)scalar;
#endif
    {
        const uint64_t exponent = width == 32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
        const uint64_t fraction = width == 32 ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
        IrSimdValue staged = { 0 };
        for (uint32_t lane = 0; result && lane < bits / width; lane += 1)
        {
            const uint64_t a = ir_simd_lane_read(left, width, lane);
            const uint64_t b = ir_simd_lane_read(right, width, lane);
            if (op == IR_VCMP)
            {
                const bool a_nan = (a & exponent) == exponent && (a & fraction) != 0;
                const bool b_nan = (b & exponent) == exponent && (b & fraction) != 0;
                const bool subnormal = ((a & exponent) == 0 && (a & fraction) != 0) || ((b & exponent) == 0 && (b & fraction) != 0);
                result = (a_nan || b_nan || !subnormal) && predicate <= IR_GE;
                staged.words[0] |= (uint64_t)ir_optimize_simd_float_compare(a, b, width, predicate) << lane;
            }
            else
            {
                uint64_t value = 0;
                result = ir_optimize_simd_float_lane(op, a, b, width, &value);
                ir_simd_lane_write(&staged, width, lane, value);
            }
        }
        if (result)
        {
            *output = staged;
        }
    }
    return result;
}

static bool ir_optimize_simd_fold(const IrFunction* f, uint32_t i, const IrSimdValue operands[3], bool scalar, IrSimdValue* output)
{
    const uint32_t op = f->op[i];
    const uint32_t bits = ir_type_bits[f->type[op == IR_VCMP ? f->a[i] : i]];
    const uint32_t width = ir_simd_lane_bits((IrSimdLane)f->flags[i]);
    IrSimdValue staged = { 0 };
    bool folded = true;
    if (f->flags[i] >= IR_SIMD_LANE_F32 && (op == IR_VCMP || op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV))
    {
        folded = ir_optimize_simd_float(op, width, bits, op == IR_VCMP ? f->c[i] : IR_EQ, operands, operands + 1, scalar, &staged);
    }
    else if (op == IR_AND || op == IR_OR || op == IR_XOR || op == IR_NOT)
    {
        for (uint32_t word = 0; word < bits / 64; word += 1)
        {
            const uint64_t a = operands[0].words[word];
            const uint64_t b = operands[1].words[word];
            staged.words[word] = op == IR_AND ? a & b : op == IR_OR ? a | b : op == IR_XOR ? a ^ b : ~a;
        }
    }
    else if (op == IR_VSELECT)
    {
        for (uint32_t lane = 0; lane < bits / width; lane += 1)
        {
            const uint32_t selected = (operands[0].words[0] & (UINT64_C(1) << lane)) != 0 ? 1 : 2;
            ir_simd_lane_write(&staged, width, lane, ir_simd_lane_read(operands + selected, width, lane));
        }
    }
    else
    {
        bool swap = false;
        uint32_t operation = ir_optimize_fold_operation(f, i, &swap);
        if (op == IR_VCMP)
        {
            static const uint8_t predicates[IR_PREDICATE_COUNT] = {
                IROPT_FOLD_EQ,
                IROPT_FOLD_NE,
                IROPT_FOLD_SLT,
                IROPT_FOLD_SLE,
                IROPT_FOLD_SLT,
                IROPT_FOLD_SLE,
                IROPT_FOLD_ULT,
                IROPT_FOLD_ULE,
                IROPT_FOLD_ULT,
                IROPT_FOLD_ULE,
            };
            operation = predicates[f->c[i]];
            swap = f->c[i] == IR_GT || f->c[i] == IR_GE || f->c[i] == IR_UGT || f->c[i] == IR_UGE;
        }
        folded = operation != IROPT_FOLD_NONE;
        const uint32_t lanes = bits / width;
        for (uint32_t first = 0; folded && first < lanes; first += BUSTER_IR_OPTIMIZE_BATCH)
        {
            const uint32_t count = lanes - first < BUSTER_IR_OPTIMIZE_BATCH ? lanes - first : BUSTER_IR_OPTIMIZE_BATCH;
            uint32_t operations[BUSTER_IR_OPTIMIZE_BATCH];
            uint32_t widths[BUSTER_IR_OPTIMIZE_BATCH];
            uint64_t left[BUSTER_IR_OPTIMIZE_BATCH];
            uint64_t right[BUSTER_IR_OPTIMIZE_BATCH];
            uint64_t values[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
            uint16_t active = 0;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                operations[lane] = operation;
                widths[lane] = width;
                left[lane] = ir_simd_lane_read(operands + (swap ? 1 : 0), width, first + lane);
                right[lane] = ir_simd_lane_read(operands + (swap ? 0 : 1), width, first + lane);
                const bool shift = op == IR_SHL || op == IR_LSHR || op == IR_ASHR;
                if (shift && right[lane] >= width)
                {
                    const bool negative = (left[lane] & (UINT64_C(1) << (width - 1))) != 0;
                    values[lane] = op == IR_ASHR && negative ? UINT64_MAX >> (64 - width) : 0;
                }
                else
                {
                    active |= UINT32_C(1) << lane;
                }
            }
            const IrOptimizeFoldBatch batch = {
                .count = count,
                .operation = operations,
                .width = widths,
                .left = left,
                .right = right,
                .result = values,
            };
            const uint16_t done = scalar ? ir_optimize_fold_scalar(batch, active) : ir_optimize_fold_vector(batch, active);
            folded = done == active;
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                if (op == IR_VCMP)
                {
                    staged.words[0] |= values[lane] << (first + lane);
                }
                else
                {
                    ir_simd_lane_write(&staged, width, first + lane, values[lane]);
                }
            }
        }
    }
    if (folded)
    {
        *output = staged;
    }
    return folded;
}

static uint8_t ir_optimize_simd_transfer(const IrOptimizeWork* work, uint32_t i, IrSimdValue* value, uint64_t* examined)
{
    const IrFunction* f = work->function;
    const uint32_t op = f->op[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    uint8_t result = IROPT_OVERDEFINED;
    if (op == IR_VCONST)
    {
        result = IROPT_CONSTANT;
        *value = f->vector_constants[a];
    }
    else if (op == IR_INTRINSIC && f->c[i] == BUSTER_IR_INTRINSIC_SPLAT)
    {
        result = work->states[a];
        if (result == IROPT_CONSTANT && !ir_splat_value(f->type[i], f->type[a], work->values[a].low, value))
        {
            result = IROPT_OVERDEFINED;
        }
    }
    else if (op == IR_INTRINSIC && f->c[i] >= BUSTER_IR_INTRINSIC_EXTRACT && f->c[i] <= BUSTER_IR_INTRINSIC_LANE_OP)
    {
        // A width change, a permute, a conversion, a scalar shift or a lane operation (#1840): exact integer and bit moves,
        // folded by the model once every operand is constant (a shift's count a scalar, its shape none).
        static const uint8_t operations[] = { IR_SIMD_EXTRACT, IR_SIMD_WIDEN,  IR_SIMD_INSERT,   IR_SIMD_PERMUTE, IR_SIMD_SHUFFLE,
                                              IR_SIMD_ALIGN,   IR_SIMD_SHUFFLE_LANES, IR_SIMD_EXTEND, IR_SIMD_TRUNCATE, IR_SIMD_SHIFT,
                                              IR_SIMD_LANE_OP };
        const bool binary = f->c[i] != BUSTER_IR_INTRINSIC_EXTRACT && f->c[i] != BUSTER_IR_INTRINSIC_WIDEN && f->c[i] != BUSTER_IR_INTRINSIC_EXTEND &&
                            f->c[i] != BUSTER_IR_INTRINSIC_TRUNCATE;
        const uint8_t left = work->states[a];
        const uint8_t right = binary ? work->states[b] : IROPT_CONSTANT;
        result = left == IROPT_OVERDEFINED || right == IROPT_OVERDEFINED ? IROPT_OVERDEFINED : left == IROPT_UNKNOWN || right == IROPT_UNKNOWN ? IROPT_UNKNOWN : IROPT_CONSTANT;
        if (result == IROPT_CONSTANT)
        {
            const IrSimdValue operands[2] = { ir_optimize_simd_value(work, a), binary ? ir_optimize_simd_value(work, b) : (IrSimdValue){ 0 } };
            const IrSimdValue* pointers[3] = { &operands[0], binary ? &operands[1] : 0, 0 };
            const IrSimdAction action = {
                .operation = (IrSimdOperation)operations[f->c[i] - BUSTER_IR_INTRINSIC_EXTRACT],
                .result = ir_simd_shape(f->type[i]),
                .operands = { ir_simd_shape(f->type[a]), binary ? ir_simd_shape(f->type[b]) : IR_SIMD_NONE, IR_SIMD_NONE },
                .index = f->flags[i],
            };
            result = ir_simd_evaluate(&action, pointers, 0, 0, 0, value) ? IROPT_CONSTANT : IROPT_OVERDEFINED;
        }
    }
    else if (op == IR_INTRINSIC && f->c[i] == BUSTER_IR_INTRINSIC_REDUCE)
    {
        // A reduction (#1840), folded by the model once its vector is constant.
        result = work->states[a];
        if (result == IROPT_CONSTANT)
        {
            const IrSimdValue operand = ir_optimize_simd_value(work, a);
            const IrSimdValue* pointers[3] = { &operand, 0, 0 };
            const IrSimdAction action = { .operation = IR_SIMD_REDUCE, .result = ir_simd_shape(f->type[i]), .operands = { ir_simd_shape(f->type[a]), IR_SIMD_NONE, IR_SIMD_NONE }, .index = f->flags[i] };
            result = ir_simd_evaluate(&action, pointers, 0, 0, 0, value) ? IROPT_CONSTANT : IROPT_OVERDEFINED;
        }
    }
    else if (op == IR_INTRINSIC && f->c[i] == BUSTER_IR_INTRINSIC_AES)
    {
        // An AES round (#1840), folded by the model once both operands are constant.
        const uint8_t left = work->states[a];
        const uint8_t right = work->states[b];
        result = left == IROPT_OVERDEFINED || right == IROPT_OVERDEFINED ? IROPT_OVERDEFINED : left == IROPT_UNKNOWN || right == IROPT_UNKNOWN ? IROPT_UNKNOWN : IROPT_CONSTANT;
        if (result == IROPT_CONSTANT)
        {
            const IrSimdValue operands[2] = { ir_optimize_simd_value(work, a), ir_optimize_simd_value(work, b) };
            const IrSimdValue* pointers[3] = { &operands[0], &operands[1], 0 };
            const IrSimdShape shape = ir_simd_shape(f->type[i]);
            const IrSimdAction action = { .operation = IR_SIMD_AES, .result = shape, .operands = { shape, shape, IR_SIMD_NONE }, .index = f->flags[i] };
            result = ir_simd_evaluate(&action, pointers, 0, 0, 0, value) ? IROPT_CONSTANT : IROPT_OVERDEFINED;
        }
    }
    else if (op == IR_INTRINSIC && ir_intrinsic_ternary(f->c[i]))
    {
        // A permute of two tables or a bitwise function of three vectors (#1840), its third operand in the extra array,
        // folded by the model as the others.
        const uint32_t third = ir_intrinsic_third(f, i);
        const uint8_t states[3] = { work->states[a], work->states[b], work->states[third] };
        const bool over = states[0] == IROPT_OVERDEFINED || states[1] == IROPT_OVERDEFINED || states[2] == IROPT_OVERDEFINED;
        const bool unknown = states[0] == IROPT_UNKNOWN || states[1] == IROPT_UNKNOWN || states[2] == IROPT_UNKNOWN;
        result = over ? IROPT_OVERDEFINED : unknown ? IROPT_UNKNOWN : IROPT_CONSTANT;
        if (result == IROPT_CONSTANT)
        {
            const IrSimdValue operands[3] = { ir_optimize_simd_value(work, a), ir_optimize_simd_value(work, b), ir_optimize_simd_value(work, third) };
            const IrSimdValue* pointers[3] = { &operands[0], &operands[1], &operands[2] };
            static const uint8_t operations[] = { IR_SIMD_PERMUTE2, IR_SIMD_TERNLOG, IR_SIMD_COMPRESS, IR_SIMD_EXPAND };
            const IrSimdAction action = {
                .operation = (IrSimdOperation)operations[ir_intrinsic_kind(f->c[i]) - BUSTER_IR_INTRINSIC_TERNARY],
                .result = ir_simd_shape(f->type[i]),
                .operands = { ir_simd_shape(f->type[a]), ir_simd_shape(f->type[b]), ir_simd_shape(f->type[third]) },
                .index = f->flags[i],
            };
            result = ir_simd_evaluate(&action, pointers, 0, 0, 0, value) ? IROPT_CONSTANT : IROPT_OVERDEFINED;
        }
    }
    else if (op == IR_COPY || op == IR_BITCAST)
    {
        result = work->states[a];
        if (result == IROPT_CONSTANT)
        {
            *value = ir_optimize_simd_value(work, a);
        }
    }
    else if (op == IR_PHI)
    {
        result = IROPT_UNKNOWN;
        uint32_t incoming = 0;
        for (; incoming < b && result != IROPT_OVERDEFINED; incoming += 1)
        {
            const uint32_t predecessor = f->extra[a + incoming * 2];
            const uint32_t source = f->extra[a + incoming * 2 + 1];
            const uint32_t edge = ir_optimize_edge(&work->cfg, predecessor, work->cfg.instruction_blocks[i]);
            if (work->edges[edge] != 0)
            {
                const IrSimdValue incoming_value = ir_optimize_simd_value(work, source);
                result = ir_optimize_simd_meet(result, value, work->states[source], &incoming_value, value);
            }
        }
        *examined += incoming;
    }
    else if (op == IR_SELECT)
    {
        if (work->states[a] == IROPT_CONSTANT)
        {
            const uint32_t source = work->values[a].low != 0 ? b : f->c[i];
            result = work->states[source];
            if (result == IROPT_CONSTANT)
            {
                *value = ir_optimize_simd_value(work, source);
            }
        }
        else
        {
            const IrSimdValue left = ir_optimize_simd_value(work, b);
            const IrSimdValue right = ir_optimize_simd_value(work, f->c[i]);
            if (work->states[b] == IROPT_CONSTANT && work->states[f->c[i]] == IROPT_CONSTANT && memcmp(&left, &right, sizeof(left)) == 0)
            {
                result = IROPT_CONSTANT;
                *value = left;
            }
            else if (work->states[a] == IROPT_UNKNOWN)
            {
                result = IROPT_UNKNOWN;
            }
        }
    }
    else if (op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND || op == IR_OR || op == IR_XOR || op == IR_NOT ||
             op == IR_SHL || op == IR_LSHR || op == IR_ASHR || op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV ||
             op == IR_VCMP || op == IR_VSELECT)
    {
        const uint32_t mask = ir_value_mask(f, i);
        const uint32_t ids[3] = { a, b, f->c[i] };
        IrSimdValue operands[3] = { 0 };
        bool unknown = false;
        bool overdefined = false;
        for (uint32_t operand = 0; operand < 3; operand += 1)
        {
            if ((mask & (1u << operand)) != 0)
            {
                const uint32_t id = ids[operand];
                unknown = unknown || work->states[id] == IROPT_UNKNOWN;
                overdefined = overdefined || work->states[id] == IROPT_OVERDEFINED;
                if (work->states[id] == IROPT_CONSTANT)
                {
                    operands[operand] = ir_optimize_simd_value(work, id);
                }
            }
        }
        if (!overdefined)
        {
            result = unknown ? IROPT_UNKNOWN : ir_optimize_simd_fold(f, i, operands, work->scalar, value) ? IROPT_CONSTANT : IROPT_OVERDEFINED;
        }
    }
    return result;
}

static bool ir_optimize_simd_visit(IrOptimizeWork* work, uint32_t i, bool queue)
{
    bool changed = false;
    if (ir_optimize_simd_storage(work))
    {
        IrSimdValue value = { 0 };
        const uint8_t state = ir_optimize_simd_transfer(work, i, &value, &work->statistics.sccp_phi_incoming);
        if (ir_vector_type(work->function->type[i]))
        {
            IrSimdValue* const current = work->vector_values + work->vector_indices[i];
            IrSimdValue combined = { 0 };
            const uint8_t next = ir_optimize_simd_meet(work->states[i], current, state, &value, &combined);
            changed = next != work->states[i];
            if (changed)
            {
                work->states[i] = next;
                *current = next == IROPT_CONSTANT ? combined : (IrSimdValue){ 0 };
                if (queue)
                {
                    ir_optimize_enqueue_users(work, i);
                }
            }
        }
        else
        {
            const IrConstant scalar = { .low = value.words[0], .high = value.words[1] };
            changed = ir_optimize_publish(work, i, state, scalar, queue);
        }
    }
    return changed;
}
