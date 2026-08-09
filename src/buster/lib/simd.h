#pragma once

#include <buster/lib/base.h>

// A target-fixed 512-bit byte vocabulary, not a portability layer.
//
// Every operation here names one AVX-512 instruction. That is deliberate: the
// kernels this exists for -- the buster and C lexers, glyph downsampling, long
// text search -- are written for Zen 5's native 512-bit width per the
// microarchitecture rules in AGENTS.md, and an abstraction wide enough to also
// describe NEON or SSE would either lose the operations that make those
// kernels fast (vpermt2b, vpcompressb, first-class masks) or grow into a
// portable vector library nobody asked for.
//
// There are three implementations behind one interface:
//
//   * the self-hosted compiler, through `__builtin_buster_simd_*`, which
//     lowers each call to the same single instruction;
//   * clang and gcc, through <immintrin.h>;
//   * everything else -- MSVC, AArch64, pre-AVX-512 x86 -- through a scalar
//     fallback that is correct and slow.
//
// Callers therefore write the kernel once and guard it with BUSTER_SIMD_512
// only where a *different algorithm* is worth it, not to keep the tree
// building. Performance is only ever quoted from the clang-built binaries, so
// the fallback owes correctness and nothing else.
//
// **Everything below is a macro, and arguments must be free of side effects.**
// That is forced rather than chosen: `ide cc` lowers directly and runs no
// inliner, not even for `always_inline`, so a function wrapper around a single
// instruction would be a real call in the self-hosted stages -- and a call per
// SIMD operation would cost more than the vectorization saves. The scalar
// fallback does use ordinary functions, because there the call is already the
// cheapest thing about it.

// The feature set the vocabulary needs: F and BW for the 512-bit byte lanes
// and their mask compares, VBMI for vpermt2b, VBMI2 for vpcompressb. `ide cc`
// predefines these from its own target, so this one condition decides the same
// way for every compiler that builds this tree.
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_COMPILER_MSVC && defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VBMI__) && defined(__AVX512VBMI2__)
#define BUSTER_SIMD_512 1
#else
#define BUSTER_SIMD_512 0
#endif

#if BUSTER_SIMD_512 && !defined(__BUSTER__)
#include <immintrin.h>
#endif

// A 64-lane mask. It is a plain u64 rather than an opaque handle because the
// mask operations that matter -- shifting a cursor's worth of lanes off the
// bottom, combining classes, counting, finding the first set lane -- are
// ordinary integer arithmetic, and on Zen 4/5 the general-purpose ALUs run
// more of them per cycle than the k unit does. The k registers are where the
// vector instructions need them, and the compiler moves masks in and out with
// a single kmovq at exactly those boundaries.
typedef u64 Mask64;

#if BUSTER_SIMD_512
typedef u8 Simd512 __attribute__((vector_size(64)));
#else
// Deliberately not over-aligned: the fallback only ever reads and writes
// through byte pointers, so alignment buys it nothing, and `_Alignas` on a
// struct member is a construct the self-hosted C frontend does not yet accept.
typedef struct Simd512 Simd512;
struct Simd512
{
    u8 bytes[64];
};
#endif

BUSTER_CT_CHECK(sizeof(Simd512) == 64);

// ---------------------------------------------------------------------------
// Masks. The same on every target: nothing here needs a k register to be
// spelled in C, and keeping them in general-purpose registers is what the
// mask type is for.
//
// Shift counts must be below 64, as C already requires of a u64 shift.
// mask64_first_set is undefined for a zero mask, matching __builtin_ctzll; the
// sentinel discipline in the Validark lexing method (AGENTS.md) means the hot
// callers always have a set lane to find.
// ---------------------------------------------------------------------------

#define mask64_shift_left(mask, count) ((Mask64)(mask) << (count))
#define mask64_shift_right(mask, count) ((Mask64)(mask) >> (count))
#define mask64_and(left, right) ((Mask64)(left) & (Mask64)(right))
#define mask64_and_not(left, right) ((Mask64)(left) & ~(Mask64)(right))
#define mask64_or(left, right) ((Mask64)(left) | (Mask64)(right))
#define mask64_xor(left, right) ((Mask64)(left) ^ (Mask64)(right))
#define mask64_not(mask) (~(Mask64)(mask))

// The low `count` lanes, `count == 64` included -- which a bare
// `(1 << count) - 1` cannot express.
#define mask64_prefix(count) ((count) >= 64 ? ~(Mask64)0 : (((Mask64)1 << (count)) - 1))

// Run starts and run ends, the two mask-arithmetic primitives the Validark
// lexing method is built on: a lane starts a run when it is set and its
// predecessor is not, and ends one when it is set and its successor is not.
#define mask64_run_starts(mask) ((Mask64)(mask) & ~((Mask64)(mask) << 1))
#define mask64_run_ends(mask) ((Mask64)(mask) & ~((Mask64)(mask) >> 1))

#if BUSTER_COMPILER_MSVC
// __popcnt64 and _BitScanForward64 would need a runtime feature check this
// header has no place making, so the portable forms stand in. Every other
// compiler in the tree folds these to one instruction where the target has it.
#define mask64_count(mask) simd_mask64_count_fallback(mask)
#define mask64_first_set(mask) simd_mask64_first_set_fallback(mask)
#else
#define mask64_count(mask) ((u32)__builtin_popcountll(mask))
#define mask64_first_set(mask) ((u32)__builtin_ctzll(mask))
#endif

// How many set lanes precede the first clear one -- one tzcnt of the
// complement, replacing the unpredictable per-byte loop at the end of a run.
#define mask64_leading_ones(mask) mask64_first_set(~(Mask64)(mask))

#if BUSTER_COMPILER_MSVC

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u32 simd_mask64_count_fallback(Mask64 mask)
{
    mask = mask - ((mask >> 1) & UINT64_C(0x5555555555555555));
    mask = (mask & UINT64_C(0x3333333333333333)) + ((mask >> 2) & UINT64_C(0x3333333333333333));
    mask = (mask + (mask >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
    return (u32)((mask * UINT64_C(0x0101010101010101)) >> 56);
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u32 simd_mask64_first_set_fallback(Mask64 mask)
{
    u32 index = 0;
    while (index < 63 && !((mask >> index) & 1))
    {
        index += 1;
    }
    return index;
}

#endif

// ---------------------------------------------------------------------------
// Vectors.
//
//   simd512_load(address)                       -> Simd512
//   simd512_load_masked(address, mask)          -> Simd512, other lanes zero
//                                                  and never read from memory,
//                                                  which is what lets a chunked
//                                                  scan run off the end of a
//                                                  buffer with no tail loop
//   simd512_store(address, value)
//   simd512_store_masked(address, mask, value)  -- other lanes untouched
//   simd512_splat(byte)                         -> Simd512
//   simd512_zero()                              -> Simd512
//   simd512_equal_byte(left, right)             -> Mask64
//   simd512_less_byte(left, right)              -> Mask64, unsigned; a range
//                                                  test is one subtract and one
//                                                  of these
//   simd512_sign_byte(value)                    -> Mask64 of the high bits,
//                                                  i.e. the non-ASCII test
//   simd512_test_byte(left, right)              -> Mask64 where the byte-wise
//                                                  AND is non-zero; folding the
//                                                  AND and the test into one
//                                                  instruction is what makes
//                                                  nibble-table charset
//                                                  membership a single step
//   simd512_permute2_byte(mask, low, indices, high) -> Simd512
//                                                  vpermt2b: a 128-entry lookup
//                                                  across two vectors, indexed
//                                                  per lane by the low seven
//                                                  bits, zero outside the mask
//   simd512_compress_byte(mask, value)          -> Simd512
//                                                  vpcompressb: selected bytes
//                                                  packed down, rest zero
//   simd512_compress_store_byte(address, mask, value)
//                                                  the same compaction straight
//                                                  to memory, writing exactly
//                                                  mask64_count(mask) bytes
//   simd512_widen_byte(value, quarter)          -> Simd512
//                                                  vpmovzxbd: the 16 bytes of
//                                                  one quarter zero-extended
//                                                  into 16 u32 lanes; `quarter`
//                                                  must be a constant below 4
//   simd512_shift_left_word(value, count)       -> Simd512, per u32 lane;
//                                                  `count` must be a constant
//                                                  below 32
//   simd512_ternary_word(a, b, c, table)        -> Simd512
//                                                  vpternlogd: any three-input
//                                                  bitwise function. Bit
//                                                  (a << 2) | (b << 1) | c of
//                                                  `table` is the result for
//                                                  that combination of input
//                                                  bits, so 0xfe is a|b|c, 0x80
//                                                  is a&b&c and 0x96 is a^b^c.
//                                                  One instruction where a
//                                                  chain would be three.
// ---------------------------------------------------------------------------

// Lanewise arithmetic and bitwise operations. On the vector path these are the
// GNU vector operators, which both host compilers and the self-hosted backend
// already lower to one 512-bit instruction, so they need no builtin of their
// own; the fallback spells them out. Bitwise operations are lane-width
// agnostic -- simd512_ternary_word covers any three-input combination in one
// instruction when a chain of these would be several.
#if BUSTER_SIMD_512

#define simd512_and(left, right) ((Simd512)((left) & (right)))
#define simd512_or(left, right) ((Simd512)((left) | (right)))
#define simd512_xor(left, right) ((Simd512)((left) ^ (right)))
#define simd512_add_byte(left, right) ((Simd512)((left) + (right)))
#define simd512_subtract_byte(left, right) ((Simd512)((left) - (right)))

#else

#define simd512_and(left, right) simd512_and_fallback((left), (right))
#define simd512_or(left, right) simd512_or_fallback((left), (right))
#define simd512_xor(left, right) simd512_xor_fallback((left), (right))
#define simd512_add_byte(left, right) simd512_add_byte_fallback((left), (right))
#define simd512_subtract_byte(left, right) simd512_subtract_byte_fallback((left), (right))

#endif

#if BUSTER_SIMD_512 && defined(__BUSTER__)

#define simd512_load(address) __builtin_buster_simd_load(address)
#define simd512_load_masked(address, mask) __builtin_buster_simd_load_masked((address), (mask))
#define simd512_store(address, value) __builtin_buster_simd_store((address), (value))
#define simd512_store_masked(address, mask, value) __builtin_buster_simd_store_masked((address), (mask), (value))
#define simd512_splat(byte) __builtin_buster_simd_splat_byte(byte)
#define simd512_equal_byte(left, right) __builtin_buster_simd_equal_byte((left), (right))
#define simd512_less_byte(left, right) __builtin_buster_simd_less_byte((left), (right))
#define simd512_sign_byte(value) __builtin_buster_simd_sign_byte(value)
#define simd512_test_byte(left, right) __builtin_buster_simd_test_byte((left), (right))
#define simd512_permute2_byte(mask, low, indices, high) __builtin_buster_simd_permute2_byte((mask), (low), (indices), (high))
#define simd512_compress_byte(mask, value) __builtin_buster_simd_compress_byte((mask), (value))
#define simd512_compress_store_byte(address, mask, value) __builtin_buster_simd_compress_store_byte((address), (mask), (value))
#define simd512_widen_byte(value, quarter) __builtin_buster_simd_widen_byte((value), (quarter))
#define simd512_shift_left_word(value, count) __builtin_buster_simd_shift_left_word((value), (count))
#define simd512_ternary_word(a, b, c, table) __builtin_buster_simd_ternary_word((a), (b), (c), (table))

#elif BUSTER_SIMD_512

#define simd512_load(address) ((Simd512)_mm512_loadu_si512(address))
#define simd512_load_masked(address, mask) ((Simd512)_mm512_maskz_loadu_epi8((__mmask64)(mask), (address)))
#define simd512_store(address, value) _mm512_storeu_si512((address), (__m512i)(value))
#define simd512_store_masked(address, mask, value) _mm512_mask_storeu_epi8((address), (__mmask64)(mask), (__m512i)(value))
#define simd512_splat(byte) ((Simd512)_mm512_set1_epi8((char)(byte)))
#define simd512_equal_byte(left, right) ((Mask64)_mm512_cmpeq_epi8_mask((__m512i)(left), (__m512i)(right)))
#define simd512_less_byte(left, right) ((Mask64)_mm512_cmplt_epu8_mask((__m512i)(left), (__m512i)(right)))
#define simd512_sign_byte(value) ((Mask64)_mm512_movepi8_mask((__m512i)(value)))
#define simd512_test_byte(left, right) ((Mask64)_mm512_test_epi8_mask((__m512i)(left), (__m512i)(right)))
#define simd512_permute2_byte(mask, low, indices, high)                                                                                                        \
    ((Simd512)_mm512_maskz_permutex2var_epi8((__mmask64)(mask), (__m512i)(low), (__m512i)(indices), (__m512i)(high)))
#define simd512_compress_byte(mask, value) ((Simd512)_mm512_maskz_compress_epi8((__mmask64)(mask), (__m512i)(value)))
#define simd512_compress_store_byte(address, mask, value) _mm512_mask_compressstoreu_epi8((address), (__mmask64)(mask), (__m512i)(value))
#define simd512_widen_byte(value, quarter) ((Simd512)_mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32((__m512i)(value), (quarter))))
#define simd512_shift_left_word(value, count) ((Simd512)_mm512_slli_epi32((__m512i)(value), (count)))
#define simd512_ternary_word(a, b, c, table) ((Simd512)_mm512_ternarylogic_epi32((__m512i)(a), (__m512i)(b), (__m512i)(c), (table)))

#else

#define simd512_load(address) simd512_load_fallback(address)
#define simd512_load_masked(address, mask) simd512_load_masked_fallback((address), (mask))
#define simd512_store(address, value) simd512_store_fallback((address), (value))
#define simd512_store_masked(address, mask, value) simd512_store_masked_fallback((address), (mask), (value))
#define simd512_splat(byte) simd512_splat_fallback(byte)
#define simd512_equal_byte(left, right) simd512_equal_byte_fallback((left), (right))
#define simd512_less_byte(left, right) simd512_less_byte_fallback((left), (right))
#define simd512_sign_byte(value) simd512_sign_byte_fallback(value)
#define simd512_test_byte(left, right) simd512_test_byte_fallback((left), (right))
#define simd512_permute2_byte(mask, low, indices, high) simd512_permute2_byte_fallback((mask), (low), (indices), (high))
#define simd512_compress_byte(mask, value) simd512_compress_byte_fallback((mask), (value))
#define simd512_compress_store_byte(address, mask, value) simd512_compress_store_byte_fallback((address), (mask), (value))
#define simd512_widen_byte(value, quarter) simd512_widen_byte_fallback((value), (quarter))
#define simd512_shift_left_word(value, count) simd512_shift_left_word_fallback((value), (count))
#define simd512_ternary_word(a, b, c, table) simd512_ternary_word_fallback((a), (b), (c), (table))

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_splat_fallback(u8 value)
{
    Simd512 result = {0};
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_load_fallback(void const* address)
{
    Simd512 result = {0};
    u8 const* source = (u8 const*)address;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = source[lane];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_load_masked_fallback(void const* address, Mask64 mask)
{
    Simd512 result = {0};
    u8 const* source = (u8 const*)address;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = (mask >> lane) & 1 ? source[lane] : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL void simd512_store_fallback(void* address, Simd512 value)
{
    u8* destination = (u8*)address;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        destination[lane] = value.bytes[lane];
    }
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL void simd512_store_masked_fallback(void* address, Mask64 mask, Simd512 value)
{
    u8* destination = (u8*)address;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if ((mask >> lane) & 1)
        {
            destination[lane] = value.bytes[lane];
        }
    }
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Mask64 simd512_equal_byte_fallback(Simd512 left, Simd512 right)
{
    Mask64 result = 0;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result |= left.bytes[lane] == right.bytes[lane] ? (Mask64)1 << lane : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Mask64 simd512_less_byte_fallback(Simd512 left, Simd512 right)
{
    Mask64 result = 0;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result |= left.bytes[lane] < right.bytes[lane] ? (Mask64)1 << lane : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Mask64 simd512_sign_byte_fallback(Simd512 value)
{
    Mask64 result = 0;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result |= value.bytes[lane] & 0x80 ? (Mask64)1 << lane : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Mask64 simd512_test_byte_fallback(Simd512 left, Simd512 right)
{
    Mask64 result = 0;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result |= (left.bytes[lane] & right.bytes[lane]) ? (Mask64)1 << lane : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_permute2_byte_fallback(Mask64 mask, Simd512 low, Simd512 indices, Simd512 high)
{
    Simd512 result = simd512_splat_fallback(0);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        u32 index = indices.bytes[lane] & 127;
        result.bytes[lane] = (mask >> lane) & 1 ? (index < 64 ? low.bytes[index] : high.bytes[index - 64]) : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_compress_byte_fallback(Mask64 mask, Simd512 value)
{
    Simd512 result = simd512_splat_fallback(0);
    u32 next = 0;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if ((mask >> lane) & 1)
        {
            result.bytes[next] = value.bytes[lane];
            next += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL void simd512_compress_store_byte_fallback(void* address, Mask64 mask, Simd512 value)
{
    u8* destination = (u8*)address;
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        if ((mask >> lane) & 1)
        {
            *destination = value.bytes[lane];
            destination += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_widen_byte_fallback(Simd512 value, u32 quarter)
{
    Simd512 result = simd512_splat_fallback(0);
    for (u32 lane = 0; lane < 16; lane += 1)
    {
        result.bytes[lane * 4] = value.bytes[quarter * 16 + lane];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_shift_left_word_fallback(Simd512 value, u32 count)
{
    Simd512 result = simd512_splat_fallback(0);
    for (u32 word = 0; word < 16; word += 1)
    {
        u32 lanes = (u32)value.bytes[word * 4] | ((u32)value.bytes[word * 4 + 1] << 8) | ((u32)value.bytes[word * 4 + 2] << 16) |
                    ((u32)value.bytes[word * 4 + 3] << 24);
        lanes = count >= 32 ? 0 : lanes << count;
        result.bytes[word * 4] = (u8)lanes;
        result.bytes[word * 4 + 1] = (u8)(lanes >> 8);
        result.bytes[word * 4 + 2] = (u8)(lanes >> 16);
        result.bytes[word * 4 + 3] = (u8)(lanes >> 24);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_and_fallback(Simd512 left, Simd512 right)
{
    Simd512 result = {0};
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = left.bytes[lane] & right.bytes[lane];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_or_fallback(Simd512 left, Simd512 right)
{
    Simd512 result = {0};
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = left.bytes[lane] | right.bytes[lane];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_xor_fallback(Simd512 left, Simd512 right)
{
    Simd512 result = {0};
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = left.bytes[lane] ^ right.bytes[lane];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_add_byte_fallback(Simd512 left, Simd512 right)
{
    Simd512 result = {0};
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = (u8)(left.bytes[lane] + right.bytes[lane]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_subtract_byte_fallback(Simd512 left, Simd512 right)
{
    Simd512 result = {0};
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        result.bytes[lane] = (u8)(left.bytes[lane] - right.bytes[lane]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL Simd512 simd512_ternary_word_fallback(Simd512 a, Simd512 b, Simd512 c, u32 table)
{
    Simd512 result = simd512_splat_fallback(0);
    for (u32 lane = 0; lane < 64; lane += 1)
    {
        u8 produced = 0;
        for (u32 bit = 0; bit < 8; bit += 1)
        {
            u32 selector = (u32)(((a.bytes[lane] >> bit) & 1) << 2) | (u32)(((b.bytes[lane] >> bit) & 1) << 1) | (u32)((c.bytes[lane] >> bit) & 1);
            produced |= (u8)(((table >> selector) & 1) << bit);
        }
        result.bytes[lane] = produced;
    }
    return result;
}

#endif

#define simd512_zero() simd512_splat(0)
