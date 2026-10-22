// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Integer builtin actions for #148, independent of the unfinished typed stream and C selectors. Names resolve once
// at binding; cbuiltin_fold processes independent constant calls in columns, eight per vector, with an exact scalar
// twin. This is not a source evaluator: the caller owns argument checking, conversions, evaluation order, and the
// overflow builtins' destination store. A folded overflow call returns both the stored integer and its boolean.
//
// Bit operations convert the low bits of a to their declared parameter width. Overflow operations instead use each
// operand's own width/signedness and test the exact mathematical result against the destination type. Descriptors
// are a width (1..64) with BUSTER_CBUILTIN_SIGNED optionally set; these actions do not support wider integers yet.
// All output columns are written, even for unsupported/undefined inputs. Input/output columns must not overlap.
// No padding is required, and count zero permits null columns. docs/c-builtins.md records the integration boundary.
#ifndef BUSTER_C_BUILTIN_INCLUDED
#define BUSTER_C_BUILTIN_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_CBUILTIN_SIGNED 128u
#define BUSTER_CBUILTIN_LANES 8u
#define BUSTER_CBUILTIN_NAMES(X) \
    X(BSWAP16, "__builtin_bswap16") \
    X(BSWAP32, "__builtin_bswap32") \
    X(BSWAP64, "__builtin_bswap64") \
    X(CLZ, "__builtin_clz") \
    X(CLZL, "__builtin_clzl") \
    X(CLZLL, "__builtin_clzll") \
    X(CTZ, "__builtin_ctz") \
    X(CTZL, "__builtin_ctzl") \
    X(CTZLL, "__builtin_ctzll") \
    X(POPCOUNT, "__builtin_popcount") \
    X(POPCOUNTL, "__builtin_popcountl") \
    X(POPCOUNTLL, "__builtin_popcountll") \
    X(PARITY, "__builtin_parity") \
    X(PARITYL, "__builtin_parityl") \
    X(PARITYLL, "__builtin_parityll") \
    X(FFS, "__builtin_ffs") \
    X(FFSL, "__builtin_ffsl") \
    X(FFSLL, "__builtin_ffsll") \
    X(ADD_OVERFLOW, "__builtin_add_overflow") \
    X(SUB_OVERFLOW, "__builtin_sub_overflow") \
    X(MUL_OVERFLOW, "__builtin_mul_overflow")

typedef enum CBuiltinId
{
    CBUILTIN_NONE,
#define BUSTER_CBUILTIN_ENUM(id, name) CBUILTIN_##id,
    BUSTER_CBUILTIN_NAMES(BUSTER_CBUILTIN_ENUM)
#undef BUSTER_CBUILTIN_ENUM
    CBUILTIN_COUNT,
} CBuiltinId;

typedef struct CBuiltinName CBuiltinName;
struct CBuiltinName
{
    char bytes[32];
    uint32_t length;
};

static const CBuiltinName cbuiltin_names[CBUILTIN_COUNT] =
{
    { "", 0 },
#define BUSTER_CBUILTIN_NAME(id, name) { name, sizeof(name) - 1 },
    BUSTER_CBUILTIN_NAMES(BUSTER_CBUILTIN_NAME)
#undef BUSTER_CBUILTIN_NAME
};

typedef enum CBuiltinFoldStatus
{
    CBUILTIN_FOLDED,
    CBUILTIN_UNDEFINED,
    CBUILTIN_UNSUPPORTED,
    CBUILTIN_INVALID_TYPE,
} CBuiltinFoldStatus;

typedef struct CBuiltinIntegers CBuiltinIntegers;
struct CBuiltinIntegers
{
    const uint32_t* id;
    const uint64_t* a;
    const uint64_t* b;
    const uint32_t* a_type;
    const uint32_t* b_type;
    const uint32_t* result_type;
};

typedef struct CBuiltinIntegerResults CBuiltinIntegerResults;
struct CBuiltinIntegerResults
{
    uint64_t* value;
    uint32_t* overflow;
    uint32_t* status;
};

static CBuiltinId cbuiltin_lookup_scalar(const char* name, uint32_t length)
{
    CBuiltinId result = CBUILTIN_NONE;
    for (uint32_t i = 1; i < CBUILTIN_COUNT; i += 1)
    {
        if (length == cbuiltin_names[i].length && memcmp(name, cbuiltin_names[i].bytes, length) == 0)
        {
            result = (CBuiltinId)i;
        }
    }
    return result;
}

static CBuiltinId cbuiltin_lookup(const char* name, uint32_t length)
{
    CBuiltinId result = CBUILTIN_NONE;
    if (length != 0 && length < 32)
    {
        const __mmask32 valid = (UINT32_C(1) << length) - 1;
        const __m256i bytes = _mm256_maskz_loadu_epi8(valid, name);
        for (uint32_t i = 1; i < CBUILTIN_COUNT; i += 1)
        {
            const __m256i candidate = _mm256_loadu_si256((const __m256i*)cbuiltin_names[i].bytes);
            if (length == cbuiltin_names[i].length && (_mm256_cmpeq_epi8_mask(bytes, candidate) & valid) == valid)
            {
                result = (CBuiltinId)i;
            }
        }
    }
    return result;
}

static uint64_t cbuiltin_mask(uint32_t bits)
{
    return UINT64_MAX >> (64 - bits);
}

static bool cbuiltin_integer_type(uint32_t type)
{
    const uint32_t width = type & ~BUSTER_CBUILTIN_SIGNED;
    return width >= 1 && width <= 64 && type != (BUSTER_CBUILTIN_SIGNED | 1);
}

static unsigned __int128 cbuiltin_magnitude(uint64_t bits, uint32_t type, bool* negative)
{
    const uint32_t width = type & ~BUSTER_CBUILTIN_SIGNED;
    const uint64_t mask = cbuiltin_mask(width);
    bits &= mask;
    *negative = (type & BUSTER_CBUILTIN_SIGNED) != 0 && (bits >> (width - 1)) != 0;
    return *negative ? ((~bits + 1) & mask) : bits;
}

static void cbuiltin_fold_one(CBuiltinIntegers input, CBuiltinIntegerResults output, uint32_t i)
{
    const uint32_t id = input.id[i];
    uint64_t value = 0;
    uint32_t overflow = 0;
    uint32_t status = CBUILTIN_FOLDED;
    if (id >= CBUILTIN_ADD_OVERFLOW && id <= CBUILTIN_MUL_OVERFLOW)
    {
        const uint32_t at = input.a_type[i];
        const uint32_t bt = input.b_type[i];
        const uint32_t rt = input.result_type[i];
        if (!cbuiltin_integer_type(at) || !cbuiltin_integer_type(bt) || !cbuiltin_integer_type(rt))
        {
            status = CBUILTIN_INVALID_TYPE;
        }
        else
        {
            bool an;
            bool bn;
            const unsigned __int128 a = cbuiltin_magnitude(input.a[i], at, &an);
            const unsigned __int128 b = cbuiltin_magnitude(input.b[i], bt, &bn);
            unsigned __int128 magnitude;
            bool negative;
            if (id == CBUILTIN_MUL_OVERFLOW)
            {
                magnitude = a * b;
                negative = an != bn;
            }
            else
            {
                bn ^= id == CBUILTIN_SUB_OVERFLOW;
                magnitude = an == bn ? a + b : (a >= b ? a - b : b - a);
                negative = an == bn || a >= b ? an : bn;
            }
            negative &= magnitude != 0;
            const uint32_t width = rt & ~BUSTER_CBUILTIN_SIGNED;
            const uint64_t mask = cbuiltin_mask(width);
            const bool signed_result = (rt & BUSTER_CBUILTIN_SIGNED) != 0;
            const uint64_t limit = signed_result ? (mask >> 1) + negative : mask;
            overflow = magnitude > limit || (negative && !signed_result);
            const uint64_t low = (uint64_t)magnitude;
            value = (negative ? 0 - low : low) & mask;
        }
    }
    else if (id > CBUILTIN_NONE && id <= CBUILTIN_FFSLL)
    {
        const uint32_t width = id == CBUILTIN_BSWAP16 ? 16 : (id == CBUILTIN_BSWAP32 || (id >= CBUILTIN_CLZ && (id - CBUILTIN_CLZ) % 3 == 0) ? 32 : 64);
        const uint64_t a = input.a[i] & cbuiltin_mask(width);
        if (id <= CBUILTIN_BSWAP64)
        {
            value = __builtin_bswap64(a) >> (64 - width);
        }
        else if (id <= CBUILTIN_CTZLL)
        {
            if (a == 0)
            {
                status = CBUILTIN_UNDEFINED;
            }
            else if (id <= CBUILTIN_CLZLL)
            {
                value = __builtin_clzll(a) - (64 - width);
            }
            else
            {
                value = __builtin_ctzll(a);
            }
        }
        else if (id <= CBUILTIN_PARITYLL)
        {
            value = __builtin_popcountll(a);
            if (id >= CBUILTIN_PARITY)
            {
                value &= 1;
            }
        }
        else if (a != 0)
        {
            value = __builtin_ctzll(a) + 1;
        }
    }
    else
    {
        status = CBUILTIN_UNSUPPORTED;
    }
    output.value[i] = value;
    output.overflow[i] = overflow;
    output.status[i] = status;
}

static void cbuiltin_fold_scalar(CBuiltinIntegers input, CBuiltinIntegerResults output, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        cbuiltin_fold_one(input, output, i);
    }
}

// Unsigned high half of eight full 64x64 products, using four 32x32 products. The middle sums cannot overflow:
// (2^32-1)^2 + (2^32-1) < 2^64. Signed high halves then subtract the other operand for each negative input.
static __m512i cbuiltin_multiply_high(__m512i a, __m512i b)
{
    const __m512i low32 = _mm512_set1_epi64(UINT32_MAX);
    const __m512i ah = _mm512_srli_epi64(a, 32);
    const __m512i bh = _mm512_srli_epi64(b, 32);
    const __m512i ll = _mm512_mul_epu32(a, b);
    const __m512i middle = _mm512_add_epi64(_mm512_mul_epu32(ah, b), _mm512_srli_epi64(ll, 32));
    const __m512i other = _mm512_add_epi64(_mm512_mul_epu32(a, bh), _mm512_and_si512(middle, low32));
    return _mm512_add_epi64(_mm512_mul_epu32(ah, bh), _mm512_add_epi64(_mm512_srli_epi64(middle, 32), _mm512_srli_epi64(other, 32)));
}

static __mmask8 cbuiltin_vector_type(__m512i type)
{
    const __m512i width = _mm512_andnot_si512(_mm512_set1_epi64(BUSTER_CBUILTIN_SIGNED), type);
    return _mm512_cmp_epu64_mask(_mm512_sub_epi64(width, _mm512_set1_epi64(1)), _mm512_set1_epi64(64), _MM_CMPINT_LT) &
        _mm512_cmpneq_epi64_mask(type, _mm512_set1_epi64(BUSTER_CBUILTIN_SIGNED | 1));
}

static __m512i cbuiltin_vector_extend(__m512i bits, __m512i type)
{
    const __m512i width = _mm512_and_si512(type, _mm512_set1_epi64(127));
    const __m512i shift = _mm512_sub_epi64(_mm512_set1_epi64(64), width);
    const __m512i shifted = _mm512_sllv_epi64(bits, shift);
    const __mmask8 is_signed = _mm512_test_epi64_mask(type, _mm512_set1_epi64(BUSTER_CBUILTIN_SIGNED));
    return _mm512_mask_blend_epi64(is_signed, _mm512_srlv_epi64(shifted, shift), _mm512_srav_epi64(shifted, shift));
}

static void cbuiltin_fold(CBuiltinIntegers input, CBuiltinIntegerResults output, uint32_t count)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi64(1);
    const __m512i ones = _mm512_set1_epi64(-1);
    const __m512i reverse = _mm512_setr_epi64(0x0001020304050607, 0x08090a0b0c0d0e0f, 0x0001020304050607, 0x08090a0b0c0d0e0f,
        0x0001020304050607, 0x08090a0b0c0d0e0f, 0x0001020304050607, 0x08090a0b0c0d0e0f);
    for (uint32_t i = 0; i < count;)
    {
        const uint32_t lanes = count - i < BUSTER_CBUILTIN_LANES ? count - i : BUSTER_CBUILTIN_LANES;
        const __mmask8 active = (1u << lanes) - 1;
        const __m512i id = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(active, input.id + i));
        __m512i a = _mm512_maskz_loadu_epi64(active, input.a + i);
        const __mmask8 bits = _mm512_cmp_epu64_mask(_mm512_sub_epi64(id, one), _mm512_set1_epi64(CBUILTIN_FFSLL), _MM_CMPINT_LT) & active;
        const __mmask8 arithmetic = _mm512_cmp_epu64_mask(_mm512_sub_epi64(id, _mm512_set1_epi64(CBUILTIN_ADD_OVERFLOW)), _mm512_set1_epi64(3), _MM_CMPINT_LT) & active;
        __m512i value = zero;
        __m512i overflow = zero;
        __m512i status = _mm512_mask_mov_epi64(_mm512_set1_epi64(CBUILTIN_UNSUPPORTED), bits | arithmetic, zero);
        if (bits != 0)
        {
            const __mmask8 narrow = _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_BSWAP32)) |
                _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_CLZ)) | _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_CTZ)) |
                _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_POPCOUNT)) | _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_PARITY)) |
                _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_FFS));
            __m512i shift = _mm512_maskz_mov_epi64(narrow, _mm512_set1_epi64(32));
            shift = _mm512_mask_mov_epi64(shift, _mm512_cmpeq_epi64_mask(id, one), _mm512_set1_epi64(48));
            const __m512i x = _mm512_and_si512(a, _mm512_srlv_epi64(ones, shift));
            const __m512i trailing = _mm512_popcnt_epi64(_mm512_andnot_si512(x, _mm512_sub_epi64(x, one)));
            const __m512i population = _mm512_popcnt_epi64(x);
            const __mmask8 clz = bits & _mm512_cmp_epu64_mask(_mm512_sub_epi64(id, _mm512_set1_epi64(CBUILTIN_CLZ)), _mm512_set1_epi64(3), _MM_CMPINT_LT);
            const __mmask8 ctz = bits & _mm512_cmp_epu64_mask(_mm512_sub_epi64(id, _mm512_set1_epi64(CBUILTIN_CTZ)), _mm512_set1_epi64(3), _MM_CMPINT_LT);
            const __mmask8 pop = bits & _mm512_cmp_epu64_mask(_mm512_sub_epi64(id, _mm512_set1_epi64(CBUILTIN_POPCOUNT)), _mm512_set1_epi64(3), _MM_CMPINT_LT);
            const __mmask8 parity = bits & _mm512_cmp_epu64_mask(_mm512_sub_epi64(id, _mm512_set1_epi64(CBUILTIN_PARITY)), _mm512_set1_epi64(3), _MM_CMPINT_LT);
            const __mmask8 ffs = bits & _mm512_cmp_epu64_mask(id, _mm512_set1_epi64(CBUILTIN_FFS), _MM_CMPINT_GE);
            const __mmask8 nonzero = _mm512_cmpneq_epi64_mask(x, zero);
            value = _mm512_mask_mov_epi64(value, bits & _mm512_cmp_epu64_mask(id, _mm512_set1_epi64(CBUILTIN_BSWAP64), _MM_CMPINT_LE), _mm512_srlv_epi64(_mm512_shuffle_epi8(x, reverse), shift));
            value = _mm512_mask_mov_epi64(value, clz & nonzero, _mm512_sub_epi64(_mm512_lzcnt_epi64(x), shift));
            value = _mm512_mask_mov_epi64(value, ctz & nonzero, trailing);
            value = _mm512_mask_mov_epi64(value, pop, population);
            value = _mm512_mask_mov_epi64(value, parity, _mm512_and_si512(population, one));
            value = _mm512_mask_mov_epi64(value, ffs & nonzero, _mm512_add_epi64(trailing, one));
            status = _mm512_mask_mov_epi64(status, (clz | ctz) & ~nonzero, _mm512_set1_epi64(CBUILTIN_UNDEFINED));
        }
        if (arithmetic != 0)
        {
            const __m512i at = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(arithmetic, input.a_type + i));
            const __m512i bt = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(arithmetic, input.b_type + i));
            const __m512i rt = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(arithmetic, input.result_type + i));
            const __mmask8 valid = arithmetic & cbuiltin_vector_type(at) & cbuiltin_vector_type(bt) & cbuiltin_vector_type(rt);
            a = cbuiltin_vector_extend(a, at);
            const __m512i b = cbuiltin_vector_extend(_mm512_maskz_loadu_epi64(arithmetic, input.b + i), bt);
            const __mmask8 an = _mm512_test_epi64_mask(at, _mm512_set1_epi64(BUSTER_CBUILTIN_SIGNED)) & _mm512_cmp_epi64_mask(a, zero, _MM_CMPINT_LT);
            const __mmask8 bn = _mm512_test_epi64_mask(bt, _mm512_set1_epi64(BUSTER_CBUILTIN_SIGNED)) & _mm512_cmp_epi64_mask(b, zero, _MM_CMPINT_LT);
            const __m512i ah = _mm512_maskz_mov_epi64(an, ones);
            const __m512i bh = _mm512_maskz_mov_epi64(bn, ones);
            __m512i low = _mm512_add_epi64(a, b);
            __m512i high = _mm512_add_epi64(_mm512_add_epi64(ah, bh), _mm512_maskz_mov_epi64(_mm512_cmp_epu64_mask(low, a, _MM_CMPINT_LT), one));
            const __mmask8 sub = valid & _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_SUB_OVERFLOW));
            low = _mm512_mask_mov_epi64(low, sub, _mm512_sub_epi64(a, b));
            high = _mm512_mask_mov_epi64(high, sub, _mm512_sub_epi64(_mm512_sub_epi64(ah, bh), _mm512_maskz_mov_epi64(_mm512_cmp_epu64_mask(a, b, _MM_CMPINT_LT), one)));
            const __mmask8 mul = valid & _mm512_cmpeq_epi64_mask(id, _mm512_set1_epi64(CBUILTIN_MUL_OVERFLOW));
            if (mul != 0)
            {
                low = _mm512_mask_mov_epi64(low, mul, _mm512_mullo_epi64(a, b));
                __m512i product_high = cbuiltin_multiply_high(a, b);
                product_high = _mm512_mask_sub_epi64(product_high, an, product_high, b);
                product_high = _mm512_mask_sub_epi64(product_high, bn, product_high, a);
                high = _mm512_mask_mov_epi64(high, mul, product_high);
            }
            const __m512i extended = cbuiltin_vector_extend(low, rt);
            const __mmask8 negative = _mm512_test_epi64_mask(rt, _mm512_set1_epi64(BUSTER_CBUILTIN_SIGNED)) & _mm512_cmp_epi64_mask(extended, zero, _MM_CMPINT_LT);
            const __mmask8 outside = _mm512_cmpneq_epi64_mask(low, extended) | _mm512_cmpneq_epi64_mask(high, _mm512_maskz_mov_epi64(negative, ones));
            const __m512i shift = _mm512_sub_epi64(_mm512_set1_epi64(64), _mm512_and_si512(rt, _mm512_set1_epi64(127)));
            value = _mm512_mask_mov_epi64(value, valid, _mm512_and_si512(low, _mm512_srlv_epi64(ones, shift)));
            overflow = _mm512_maskz_mov_epi64(outside & valid, one);
            status = _mm512_mask_mov_epi64(status, arithmetic & ~valid, _mm512_set1_epi64(CBUILTIN_INVALID_TYPE));
        }
        _mm512_mask_storeu_epi64(output.value + i, active, value);
        _mm256_mask_storeu_epi32(output.overflow + i, active, _mm512_cvtepi64_epi32(overflow));
        _mm256_mask_storeu_epi32(output.status + i, active, _mm512_cvtepi64_epi32(status));
        i += lanes;
    }
}

#endif
