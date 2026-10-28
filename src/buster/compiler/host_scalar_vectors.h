// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Scalar definitions for the remaining intrinsic-only shared helpers. The
// ordinary APIs select their existing scalar twins first. Inactive memory
// lanes are never accessed; all integer lanes use explicit-width arithmetic.
#ifndef BUSTER_HOST_SCALAR_VECTORS_H
#define BUSTER_HOST_SCALAR_VECTORS_H
#include <stdbool.h>

typedef union HostV128 HostV128;
union HostV128
{
    __m128i value;
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
    int8_t i8[16];
    int16_t i16[8];
    int32_t i32[4];
    int64_t i64[2];
};

typedef union HostV256 HostV256;
union HostV256
{
    __m256i value;
    uint8_t u8[32];
    uint16_t u16[16];
    uint32_t u32[8];
    uint64_t u64[4];
    int8_t i8[32];
    int16_t i16[16];
    int32_t i32[8];
    int64_t i64[4];
};

typedef union HostV512 HostV512;
union HostV512
{
    __m512i value;
    uint8_t u8[64];
    uint16_t u16[32];
    uint32_t u32[16];
    uint64_t u64[8];
    int8_t i8[64];
    int16_t i16[32];
    int32_t i32[16];
    int64_t i64[8];
};

BUSTER_HOST_INLINE __m256i host_mm256_add_epi16(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u16[i] = (uint16_t)((uint64_t)a_.u16[i] + (uint64_t)b_.u16[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_add_epi32(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = (uint32_t)((uint64_t)a_.u32[i] + (uint64_t)b_.u32[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_add_epi64(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u64[i] = (uint64_t)((uint64_t)a_.u64[i] + (uint64_t)b_.u64[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_and_si256(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u64[i] = a_.u64[i] & b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_blendv_epi8(__m256i a, __m256i b, __m256i index)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    HostV256 index_ __attribute__((unused)) = { .value = index };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u8[i] = (index_.u8[i] & 128) != 0 ? b_.u8[i] : a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_broadcastsi128_si256(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u8[i] = a_.u8[i & 15];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_castsi128_si256(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    memcpy(&r.value, &a, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m256 host_mm256_castsi256_ps(__m256i a)
{
    __m256 r;
    memcpy(&r, &a, sizeof(r));
    return r;
}

BUSTER_HOST_INLINE __m128i host_mm256_castsi256_si128(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    memcpy(&r.value, &a, 16);
    return r.value;
}

BUSTER_HOST_INLINE __mmask8 host_mm256_cmpeq_epi32_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.i32[i] == b_.i32[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm256_cmpeq_epi8_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.i8[i] == b_.i8[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm256_cmpge_epu32_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] >= b_.u32[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __m256i host_mm256_cmpgt_epi32(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = a_.i32[i] > b_.i32[i] ? UINT32_MAX : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask8 host_mm256_cmpgt_epi32_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.i32[i] > b_.i32[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm256_cmple_epu32_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] <= b_.u32[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm256_cmplt_epu8_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] < b_.u8[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __m128i host_mm256_cvtepi16_epi8(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = (uint8_t)a_.i16[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_cvtepu8_epi16(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u16[i] = (uint16_t)a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_cvtepu8_epi32(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = (uint32_t)a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE int64_t host_mm256_extract_epi64(__m256i a, int index)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    return (int64_t)a_.u64[(unsigned)index & 3];
}

BUSTER_HOST_INLINE __m128i host_mm256_extracti128_si256(__m256i a, int index)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    memcpy(&r.value, a_.u8 + ((unsigned)index & 1) * 16, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_inserti128_si256(__m256i a, __m128i b, int index)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    r = a_;
    memcpy(r.u8 + ((unsigned)index & 1) * 16, &b, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_load_si256(const void* memory)
{
    HostV256 r = { 0 };
    memcpy(&r.value, memory, 32);
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_loadu_si256(const void* memory)
{
    HostV256 r = { 0 };
    memcpy(&r.value, memory, 32);
    return r.value;
}

BUSTER_HOST_INLINE __mmask8 host_mm256_cmpneq_epi32_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.i32[i] != b_.i32[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm256_mask_cmpneq_epi32_mask(__mmask8 k, __m256i a, __m256i b)
{
    return (__mmask8)(k & host_mm256_cmpneq_epi32_mask(a, b));
}

BUSTER_HOST_INLINE __m256i host_mm256_mask_mov_epi32(__m256i src, __mmask8 k, __m256i a)
{
    HostV256 src_ __attribute__((unused)) = { .value = src };
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u32[i] : src_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_mask_set1_epi32(__m256i src, __mmask8 k, int32_t a)
{
    HostV256 src_ __attribute__((unused)) = { .value = src };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? (uint32_t)a : src_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm256_mask_storeu_epi32(void* memory, __mmask8 k, __m256i a)
{
    HostV256 a_ = { .value = a };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 4, &a_.u32[i], 4); }
    }
}

BUSTER_HOST_INLINE void host_mm256_mask_storeu_epi64(void* memory, __mmask8 k, __m256i a)
{
    HostV256 a_ = { .value = a };
    for (unsigned i = 0; i < 4; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 8, &a_.u64[i], 8); }
    }
}

BUSTER_HOST_INLINE __m256i host_mm256_maskz_add_epi16(__mmask16 k, __m256i a, __m256i b)
{
    HostV256 result = { .value = host_mm256_add_epi16(a, b) };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[i] = result.u16[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_maskz_compress_epi32(__mmask8 k, __m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[at++] = a_.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_maskz_loadu_epi32(__mmask8 k, const void* memory)
{
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u32[i], (const uint8_t*)memory + i * 4, 4); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_maskz_loadu_epi8(__mmask32 k, const void* memory)
{
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u8[i], (const uint8_t*)memory + i * 1, 1); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_maskz_set1_epi32(__mmask8 k, int32_t a)
{
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? (uint32_t)a : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE int host_mm256_movemask_epi8(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    uint32_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint32_t)(a_.u8[i] >> 7) << i;
    }
    return (int)r;
}

BUSTER_HOST_INLINE int host_mm256_movemask_ps(__m256 a)
{
    HostV256 a_;
    memcpy(&a_, &a, sizeof(a));
    uint32_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint32_t)(a_.u32[i] >> 31) << i;
    }
    return (int)r;
}

BUSTER_HOST_INLINE __m256i host_mm256_mullo_epi16(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u16[i] = (uint16_t)((uint64_t)a_.u16[i] * (uint64_t)b_.u16[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_or_si256(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u64[i] = a_.u64[i] | b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_permutex2var_epi8(__m256i a, __m256i index, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 index_ __attribute__((unused)) = { .value = index };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        unsigned at = (unsigned)index_.u8[i] & 63; r.u8[i] = at < 32 ? a_.u8[at] : b_.u8[at - 32];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_permutexvar_epi32(__m256i index, __m256i a)
{
    HostV256 index_ __attribute__((unused)) = { .value = index };
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = a_.u32[index_.u32[i] & 7];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_set1_epi16(int a)
{
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u16[i] = (uint16_t)a;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_set1_epi32(int32_t a)
{
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = (uint32_t)a;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_set1_epi8(int a)
{
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u8[i] = (uint8_t)a;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_setr_epi32(int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7)
{
    HostV256 r = { 0 };
    r.u32[0] = (uint32_t)a0;
    r.u32[1] = (uint32_t)a1;
    r.u32[2] = (uint32_t)a2;
    r.u32[3] = (uint32_t)a3;
    r.u32[4] = (uint32_t)a4;
    r.u32[5] = (uint32_t)a5;
    r.u32[6] = (uint32_t)a6;
    r.u32[7] = (uint32_t)a7;
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_setr_epi64x(int64_t a0, int64_t a1, int64_t a2, int64_t a3)
{
    HostV256 r = { 0 };
    r.u64[0] = (uint64_t)a0;
    r.u64[1] = (uint64_t)a1;
    r.u64[2] = (uint64_t)a2;
    r.u64[3] = (uint64_t)a3;
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_setzero_si256(void)
{
    HostV256 r = { 0 };
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_shuffle_epi8(__m256i a, __m256i index)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 index_ __attribute__((unused)) = { .value = index };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u8[i] = (index_.u8[i] & 128) != 0 ? 0 : a_.u8[(i & ~15u) + (index_.u8[i] & 15)];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_slli_epi16(__m256i a, int count)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u16[i] = amount >= 16 ? 0 : (uint16_t)((uint64_t)a_.u16[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_slli_epi32(__m256i a, int count)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u32[i] = amount >= 32 ? 0 : (uint32_t)((uint64_t)a_.u32[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_srli_epi16(__m256i a, int count)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u16[i] = amount >= 16 ? 0 : (uint16_t)(a_.u16[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_srli_epi32(__m256i a, int count)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u32[i] = amount >= 32 ? 0 : (uint32_t)(a_.u32[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm256_storeu_si256(void* memory, __m256i a)
{
    memcpy(memory, &a, 32);
}

BUSTER_HOST_INLINE __m256i host_mm256_sub_epi16(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u16[i] = (uint16_t)((uint64_t)a_.u16[i] - (uint64_t)b_.u16[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_sub_epi32(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = (uint32_t)((uint64_t)a_.u32[i] - (uint64_t)b_.u32[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_sub_epi8(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u8[i] = (uint8_t)((uint64_t)a_.u8[i] - (uint64_t)b_.u8[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask16 host_mm256_test_epi16_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)((a_.u16[i] & b_.u16[i]) != 0) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm256_test_epi32_mask(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)((a_.u32[i] & b_.u32[i]) != 0) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __m256i host_mm256_xor_si256(__m256i a, __m256i b)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u64[i] = a_.u64[i] ^ b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm256_zextsi128_si256(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    memcpy(&r.value, &a, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_abs_epi32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = a_.i32[i] < 0 ? 0u - a_.u32[i] : a_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_add_epi16(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = (uint16_t)((uint64_t)a_.u16[i] + (uint64_t)b_.u16[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_add_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)((uint64_t)a_.u32[i] + (uint64_t)b_.u32[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_add_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)((uint64_t)a_.u64[i] + (uint64_t)b_.u64[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_add_epi8(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = (uint8_t)((uint64_t)a_.u8[i] + (uint64_t)b_.u8[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_aesenc_epi128(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        __m128i x, y, z; memcpy(&x, a_.u8 + i * 16, 16); memcpy(&y, b_.u8 + i * 16, 16); z = host_aesenc(x, y); memcpy(r.u8 + i * 16, &z, 16);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_alignr_epi32(__m512i a, __m512i b, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        unsigned at = i + ((unsigned)immediate & 15); r.u32[i] = at < 16 ? b_.u32[at] : a_.u32[at - 16];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_alignr_epi64(__m512i a, __m512i b, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        unsigned at = i + ((unsigned)immediate & 7); r.u64[i] = at < 8 ? b_.u64[at] : a_.u64[at - 8];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_and_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] & b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_and_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] & b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_and_si512(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] & b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_andnot_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (~a_.u64[i]) & b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_andnot_si512(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (~a_.u64[i]) & b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_bitshuffle_epi64_mask(__m512i a, __m512i control)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 control_ __attribute__((unused)) = { .value = control };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= ((a_.u64[i / 8] >> (control_.u8[i] & 63)) & 1) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __m512i host_mm512_broadcast_i32x4(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = a_.u8[i & 15];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_castsi128_si512(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    memcpy(&r.value, &a, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_castsi256_si512(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    memcpy(&r.value, &a, 32);
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm512_castsi512_si128(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    memcpy(&r.value, &a, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm512_castsi512_si256(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    memcpy(&r.value, &a, 32);
    return r.value;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmp_epi64_mask(__m512i a, __m512i b, int predicate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        unsigned p = (unsigned)predicate & 7; bool yes = p == 0 ? a_.i64[i] == b_.i64[i] : p == 1 ? a_.i64[i] < b_.i64[i] : p == 2 ? a_.i64[i] <= b_.i64[i] : p == 3 ? false : p == 4 ? a_.i64[i] != b_.i64[i] : p == 5 ? a_.i64[i] >= b_.i64[i] : p == 6 ? a_.i64[i] > b_.i64[i] : true; r |= (uint64_t)yes << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmp_epu64_mask(__m512i a, __m512i b, int predicate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        unsigned p = (unsigned)predicate & 7; bool yes = p == 0 ? a_.u64[i] == b_.u64[i] : p == 1 ? a_.u64[i] < b_.u64[i] : p == 2 ? a_.u64[i] <= b_.u64[i] : p == 3 ? false : p == 4 ? a_.u64[i] != b_.u64[i] : p == 5 ? a_.u64[i] >= b_.u64[i] : p == 6 ? a_.u64[i] > b_.u64[i] : true; r |= (uint64_t)yes << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_cmpeq_epi16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.i16[i] == b_.i16[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpeq_epi32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.i32[i] == b_.i32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmpeq_epi64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.i64[i] == b_.i64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmpeq_epi8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.i8[i] == b_.i8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpge_epi32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.i32[i] >= b_.i32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmpge_epi64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.i64[i] >= b_.i64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_cmpge_epu16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.u16[i] >= b_.u16[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpge_epu32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] >= b_.u32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmpge_epu64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.u64[i] >= b_.u64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmpge_epu8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] >= b_.u8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmpgt_epi64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.i64[i] > b_.i64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmpgt_epi8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.i8[i] > b_.i8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_cmpgt_epu16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.u16[i] > b_.u16[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpgt_epu32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] > b_.u32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmpgt_epu64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.u64[i] > b_.u64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmpgt_epu8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] > b_.u8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_cmple_epi16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.i16[i] <= b_.i16[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmple_epu32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] <= b_.u32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmple_epu64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.u64[i] <= b_.u64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmple_epu8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] <= b_.u8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmplt_epi8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.i8[i] < b_.i8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_cmplt_epu16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.u16[i] < b_.u16[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmplt_epu32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] < b_.u32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmplt_epu64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.u64[i] < b_.u64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmplt_epu8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] < b_.u8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpneq_epi32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.i32[i] != b_.i32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmpneq_epi64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.i64[i] != b_.i64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_cmpneq_epi8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.i8[i] != b_.i8[i]) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpneq_epu32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] != b_.u32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __m512i host_mm512_conflict_epi32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        for (unsigned j = 0; j < i; j += 1) { r.u32[i] |= (uint32_t)(a_.u32[i] == a_.u32[j]) << j; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm512_cvtepi32_epi16(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u16[i] = (uint16_t)a_.i32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepi32_epi64(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)a_.i32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm512_cvtepi32_epi8(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = (uint8_t)a_.i32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm512_cvtepi64_epi16(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u16[i] = (uint16_t)a_.i64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm512_cvtepi64_epi32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u32[i] = (uint32_t)a_.i64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm512_cvtepi64_epi8(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u8[i] = (uint8_t)a_.i64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepi8_epi32(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)a_.i8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepu16_epi32(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)a_.u16[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepu16_epi64(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)a_.u16[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepu32_epi64(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)a_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepu8_epi16(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = (uint16_t)a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepu8_epi32(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_cvtepu8_epi64(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm512_cvtusepi16_epi8(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u8[i] = (uint8_t)(a_.u16[i] > UINT8_MAX ? UINT8_MAX : a_.u16[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_dpbusd_epi32(__m512i src, __m512i a, __m512i b)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint32_t value = src_.u32[i]; for (unsigned j = 0; j < 4; j += 1) { value += (uint32_t)((int)a_.u8[4 * i + j] * b_.i8[4 * i + j]); } r.u32[i] = value;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm512_extracti32x4_epi32(__m512i a, int index)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    memcpy(&r.value, a_.u8 + ((unsigned)index & 3) * 16, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m256i host_mm512_extracti64x4_epi64(__m512i a, int index)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV256 r = { 0 };
    memcpy(&r.value, a_.u8 + ((unsigned)index & 1) * 32, 32);
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_gf2p8affine_epi64_epi8(__m512i a, __m512i matrix, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 matrix_ __attribute__((unused)) = { .value = matrix };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        uint64_t row = matrix_.u64[i / 8]; uint8_t value = (uint8_t)immediate; for (unsigned bit = 0; bit < 8; bit += 1) { value ^= (uint8_t)(__builtin_parity((unsigned)(a_.u8[i] & (row >> (8 * (7 - bit))))) << bit); } r.u8[i] = value;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_i32gather_epi32(__m512i index, const void* memory, int scale)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        memcpy(&r.u32[i], (const void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i32[i] * (unsigned)scale), 4);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_i32gather_epi64(__m256i index, const void* memory, int scale)
{
    HostV256 index_ __attribute__((unused)) = { .value = index };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        memcpy(&r.u64[i], (const void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i32[i] * (unsigned)scale), 8);
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm512_i32scatter_epi32(void* memory, __m512i index, __m512i a, int scale)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    for (unsigned i = 0; i < 16; i += 1)
    {
        memcpy((void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i32[i] * (unsigned)scale), &a_.u32[i], 4);
    }
}

BUSTER_HOST_INLINE __m512i host_mm512_inserti32x4(__m512i a, __m128i b, int index)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    r = a_;
    memcpy(r.u8 + ((unsigned)index & 3) * 16, &b, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_inserti64x4(__m512i a, __m256i b, int index)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV256 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    r = a_;
    memcpy(r.u8 + ((unsigned)index & 1) * 32, &b, 32);
    return r.value;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_kunpackb(__mmask16 a, __mmask16 b)
{
    return (__mmask16)(((uint64_t)a << 8) | ((uint64_t)b & (UINT64_MAX >> 56)));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_kunpackd(__mmask64 a, __mmask64 b)
{
    return (__mmask64)(((uint64_t)a << 32) | ((uint64_t)b & (UINT64_MAX >> 32)));
}

BUSTER_HOST_INLINE __mmask32 host_mm512_kunpackw(__mmask32 a, __mmask32 b)
{
    return (__mmask32)(((uint64_t)a << 16) | ((uint64_t)b & (UINT64_MAX >> 48)));
}

BUSTER_HOST_INLINE __m512i host_mm512_load_si512(const void* memory)
{
    HostV512 r = { 0 };
    memcpy(&r.value, memory, 64);
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_loadu_si512(const void* memory)
{
    HostV512 r = { 0 };
    memcpy(&r.value, memory, 64);
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_lzcnt_epi32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)(a_.u32[i] == 0 ? 32 : __builtin_clzll(a_.u32[i]) - 32);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_lzcnt_epi64(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)(a_.u64[i] == 0 ? 64 : __builtin_clzll(a_.u64[i]) - 0);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_madd_epi16(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)((int64_t)a_.i16[2 * i] * b_.i16[2 * i] + (int64_t)a_.i16[2 * i + 1] * b_.i16[2 * i + 1]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maddubs_epi16(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        int value = (int)a_.u8[2 * i] * b_.i8[2 * i] + (int)a_.u8[2 * i + 1] * b_.i8[2 * i + 1]; r.i16[i] = (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_add_epi32(__m512i src, __mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_add_epi32(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_add_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_add_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_add_epi8(__m512i src, __mmask64 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_add_epi8(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[i] = result.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_and_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_and_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_andnot_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (~a_.u64[i]) & b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_andnot_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_andnot_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_bitshuffle_epi64_mask(__mmask64 k, __m512i a, __m512i control)
{
    return (__mmask64)(k & host_mm512_bitshuffle_epi64_mask(a, control));
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_blend_epi32(__mmask16 k, __m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? b_.u32[i] : a_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_blend_epi64(__mmask8 k, __m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = ((uint64_t)k >> i & 1) != 0 ? b_.u64[i] : a_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_blend_epi8(__mmask64 k, __m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? b_.u8[i] : a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_mask_cmpeq_epi16_mask(__mmask32 k, __m512i a, __m512i b)
{
    return (__mmask32)(k & host_mm512_cmpeq_epi16_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmpeq_epi32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmpeq_epi32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask8 host_mm512_mask_cmpeq_epi64_mask(__mmask8 k, __m512i a, __m512i b)
{
    return (__mmask8)(k & host_mm512_cmpeq_epi64_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_cmpeq_epi8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_cmpeq_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpeq_epu32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] == b_.u32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmpeq_epu32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmpeq_epu32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmpge_epu32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmpge_epu32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_cmpge_epu8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_cmpge_epu8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_cmpgt_epi32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.i32[i] > b_.i32[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmpgt_epi32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmpgt_epi32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmpgt_epu32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmpgt_epu32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask8 host_mm512_mask_cmpgt_epu64_mask(__mmask8 k, __m512i a, __m512i b)
{
    return (__mmask8)(k & host_mm512_cmpgt_epu64_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_cmpgt_epu8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_cmpgt_epu8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask32 host_mm512_mask_cmple_epi16_mask(__mmask32 k, __m512i a, __m512i b)
{
    return (__mmask32)(k & host_mm512_cmple_epi16_mask(a, b));
}

BUSTER_HOST_INLINE __mmask32 host_mm512_cmple_epu16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)(a_.u16[i] <= b_.u16[i]) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_mask_cmple_epu16_mask(__mmask32 k, __m512i a, __m512i b)
{
    return (__mmask32)(k & host_mm512_cmple_epu16_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmple_epu32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmple_epu32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask8 host_mm512_mask_cmple_epu64_mask(__mmask8 k, __m512i a, __m512i b)
{
    return (__mmask8)(k & host_mm512_cmple_epu64_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_cmplt_epi8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_cmplt_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmplt_epu32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmplt_epu32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_cmplt_epu8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_cmplt_epu8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmpneq_epi32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmpneq_epi32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask8 host_mm512_mask_cmpneq_epi64_mask(__mmask8 k, __m512i a, __m512i b)
{
    return (__mmask8)(k & host_mm512_cmpneq_epi64_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_cmpneq_epi8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_cmpneq_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_cmpneq_epu32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_cmpneq_epu32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask8 host_mm512_cmpneq_epu64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)(a_.u64[i] != b_.u64[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_mask_cmpneq_epu64_mask(__mmask8 k, __m512i a, __m512i b)
{
    return (__mmask8)(k & host_mm512_cmpneq_epu64_mask(a, b));
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_compress_epi16(__m512i src, __mmask32 k, __m512i a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    r = src_;
    unsigned at = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[at++] = a_.u16[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm512_mask_compressstoreu_epi32(void* memory, __mmask16 k, __m512i a)
{
    HostV512 a_ = { .value = a };
    unsigned at = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + at++ * 4, &a_.u32[i], 4); }
    }
}

BUSTER_HOST_INLINE void host_mm512_mask_compressstoreu_epi8(void* memory, __mmask64 k, __m512i a)
{
    HostV512 a_ = { .value = a };
    unsigned at = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + at++ * 1, &a_.u8[i], 1); }
    }
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_expand_epi16(__m512i src, __mmask32 k, __m512i a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    r = src_;
    unsigned at = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[i] = a_.u16[at++]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_i32gather_epi32(__m512i src, __mmask16 k, __m512i index, const void* memory, int scale)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 r = { 0 };
    r = src_;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u32[i], (const void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i32[i] * (unsigned)scale), 4); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_i32gather_epi64(__m512i src, __mmask8 k, __m256i index, const void* memory, int scale)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV256 index_ __attribute__((unused)) = { .value = index };
    HostV512 r = { 0 };
    r = src_;
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u64[i], (const void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i32[i] * (unsigned)scale), 8); }
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm512_mask_i32scatter_epi32(void* memory, __mmask16 k, __m512i index, __m512i a, int scale)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i32[i] * (unsigned)scale), &a_.u32[i], 4); }
    }
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_i64gather_epi64(__m512i src, __mmask8 k, __m512i index, const void* memory, int scale)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 r = { 0 };
    r = src_;
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u64[i], (const void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i64[i] * (unsigned)scale), 8); }
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm512_mask_i64scatter_epi32(void* memory, __mmask8 k, __m512i index, __m256i a, int scale)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV256 a_ __attribute__((unused)) = { .value = a };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i64[i] * (unsigned)scale), &a_.u32[i], 4); }
    }
}

BUSTER_HOST_INLINE void host_mm512_mask_i64scatter_epi64(void* memory, __mmask8 k, __m512i index, __m512i a, int scale)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((void*)((uintptr_t)memory + (uint64_t)(int64_t)index_.i64[i] * (unsigned)scale), &a_.u64[i], 8); }
    }
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_loadu_epi16(__m512i src, __mmask32 k, const void* memory)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 r = { 0 };
    r = src_;
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u16[i], (const uint8_t*)memory + i * 2, 2); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_loadu_epi32(__m512i src, __mmask16 k, const void* memory)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 r = { 0 };
    r = src_;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u32[i], (const uint8_t*)memory + i * 4, 4); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_loadu_epi64(__m512i src, __mmask8 k, const void* memory)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 r = { 0 };
    r = src_;
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u64[i], (const uint8_t*)memory + i * 8, 8); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_loadu_epi8(__m512i src, __mmask64 k, const void* memory)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 r = { 0 };
    r = src_;
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u8[i], (const uint8_t*)memory + i * 1, 1); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_max_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = a_.i32[i] > b_.i32[i] ? a_.i32[i] : b_.i32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_max_epi32(__m512i src, __mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_max_epi32(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_max_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.i64[i] > b_.i64[i] ? a_.i64[i] : b_.i64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_max_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_max_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_max_epu32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = a_.u32[i] > b_.u32[i] ? a_.u32[i] : b_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_max_epu32(__m512i src, __mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_max_epu32(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_max_epu64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] > b_.u64[i] ? a_.u64[i] : b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_max_epu64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_max_epu64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_min_epu32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = a_.u32[i] < b_.u32[i] ? a_.u32[i] : b_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_min_epu32(__m512i src, __mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_min_epu32(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_mov_epi16(__m512i src, __mmask32 k, __m512i a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u16[i] : src_.u16[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_mov_epi32(__m512i src, __mmask16 k, __m512i a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u32[i] : src_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_mov_epi64(__m512i src, __mmask8 k, __m512i a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u64[i] : src_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_mov_epi8(__m512i src, __mmask64 k, __m512i a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u8[i] : src_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mullo_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)((uint64_t)a_.u64[i] * (uint64_t)b_.u64[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_mullo_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_mullo_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_or_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] | b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_or_epi32(__m512i src, __mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_or_epi32(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_or_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] | b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_or_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_or_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutexvar_epi16(__m512i index, __m512i a)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = a_.u16[index_.u16[i] & 31];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_permutexvar_epi16(__m512i src, __mmask32 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi16(index, a) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[i] = result.u16[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutexvar_epi32(__m512i index, __m512i a)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = a_.u32[index_.u32[i] & 15];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_permutexvar_epi32(__m512i src, __mmask16 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi32(index, a) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutexvar_epi64(__m512i index, __m512i a)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[index_.u64[i] & 7];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_permutexvar_epi64(__m512i src, __mmask8 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi64(index, a) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutexvar_epi8(__m512i index, __m512i a)
{
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = a_.u8[index_.u8[i] & 63];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_permutexvar_epi8(__m512i src, __mmask64 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi8(index, a) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[i] = result.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE int32_t host_mm512_mask_reduce_add_epi32(__mmask16 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    uint32_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r += a_.u32[i]; }
    }
    return (int32_t)r;
}

BUSTER_HOST_INLINE int32_t host_mm512_mask_reduce_or_epi32(__mmask16 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    uint32_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r |= a_.u32[i]; }
    }
    return (int32_t)r;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_set1_epi32(__m512i src, __mmask16 k, int32_t a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? (uint32_t)a : src_.u32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_set1_epi64(__m512i src, __mmask8 k, int64_t a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = ((uint64_t)k >> i & 1) != 0 ? (uint64_t)a : src_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_set1_epi8(__m512i src, __mmask64 k, int a)
{
    HostV512 src_ __attribute__((unused)) = { .value = src };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? (uint8_t)a : src_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_sllv_epi64(__m512i a, __m512i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = count_.u64[i]; r.u64[i] = amount >= 64 ? 0 : (uint64_t)((uint64_t)a_.u64[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_sllv_epi64(__m512i src, __mmask8 k, __m512i a, __m512i count)
{
    HostV512 result = { .value = host_mm512_sllv_epi64(a, count) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srai_epi32(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u32[i] = amount >= 32 ? (a_.i32[i] < 0 ? UINT32_MAX : 0) : (uint32_t)(a_.i32[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_srai_epi32(__m512i src, __mmask16 k, __m512i a, int count)
{
    HostV512 result = { .value = host_mm512_srai_epi32(a, count) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srav_epi64(__m512i a, __m512i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = count_.u64[i]; r.u64[i] = amount >= 64 ? (a_.i64[i] < 0 ? UINT64_MAX : 0) : (uint64_t)(a_.i64[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_srav_epi64(__m512i src, __mmask8 k, __m512i a, __m512i count)
{
    HostV512 result = { .value = host_mm512_srav_epi64(a, count) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srli_epi32(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u32[i] = amount >= 32 ? 0 : (uint32_t)(a_.u32[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_srli_epi32(__m512i src, __mmask16 k, __m512i a, int count)
{
    HostV512 result = { .value = host_mm512_srli_epi32(a, count) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srli_epi64(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u64[i] = amount >= 64 ? 0 : (uint64_t)(a_.u64[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_srli_epi64(__m512i src, __mmask8 k, __m512i a, int count)
{
    HostV512 result = { .value = host_mm512_srli_epi64(a, count) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srlv_epi64(__m512i a, __m512i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = count_.u64[i]; r.u64[i] = amount >= 64 ? 0 : (uint64_t)(a_.u64[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_srlv_epi64(__m512i src, __mmask8 k, __m512i a, __m512i count)
{
    HostV512 result = { .value = host_mm512_srlv_epi64(a, count) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm512_mask_store_epi32(void* memory, __mmask16 k, __m512i a)
{
    HostV512 a_ = { .value = a };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 4, &a_.u32[i], 4); }
    }
}

BUSTER_HOST_INLINE void host_mm512_mask_storeu_epi16(void* memory, __mmask32 k, __m512i a)
{
    HostV512 a_ = { .value = a };
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 2, &a_.u16[i], 2); }
    }
}

BUSTER_HOST_INLINE void host_mm512_mask_storeu_epi32(void* memory, __mmask16 k, __m512i a)
{
    HostV512 a_ = { .value = a };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 4, &a_.u32[i], 4); }
    }
}

BUSTER_HOST_INLINE void host_mm512_mask_storeu_epi64(void* memory, __mmask8 k, __m512i a)
{
    HostV512 a_ = { .value = a };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 8, &a_.u64[i], 8); }
    }
}

BUSTER_HOST_INLINE void host_mm512_mask_storeu_epi8(void* memory, __mmask64 k, __m512i a)
{
    HostV512 a_ = { .value = a };
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 1, &a_.u8[i], 1); }
    }
}

BUSTER_HOST_INLINE __m512i host_mm512_sub_epi16(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = (uint16_t)((uint64_t)a_.u16[i] - (uint64_t)b_.u16[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_sub_epi16(__m512i src, __mmask32 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_sub_epi16(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[i] = result.u16[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_sub_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)((uint64_t)a_.u32[i] - (uint64_t)b_.u32[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_sub_epi32(__m512i src, __mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_sub_epi32(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_sub_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)((uint64_t)a_.u64[i] - (uint64_t)b_.u64[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_sub_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_sub_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_sub_epi8(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = (uint8_t)((uint64_t)a_.u8[i] - (uint64_t)b_.u8[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_sub_epi8(__m512i src, __mmask64 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_sub_epi8(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[i] = result.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_test_epi32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)((a_.u32[i] & b_.u32[i]) != 0) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_test_epi32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_test_epi32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask8 host_mm512_test_epi64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)((a_.u64[i] & b_.u64[i]) != 0) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_mask_test_epi64_mask(__mmask8 k, __m512i a, __m512i b)
{
    return (__mmask8)(k & host_mm512_test_epi64_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_test_epi8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)((a_.u8[i] & b_.u8[i]) != 0) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_test_epi8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_test_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm512_testn_epi32_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)((a_.u32[i] & b_.u32[i]) == 0) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm512_mask_testn_epi32_mask(__mmask16 k, __m512i a, __m512i b)
{
    return (__mmask16)(k & host_mm512_testn_epi32_mask(a, b));
}

BUSTER_HOST_INLINE __mmask8 host_mm512_testn_epi64_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r |= (uint64_t)((a_.u64[i] & b_.u64[i]) == 0) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm512_mask_testn_epi64_mask(__mmask8 k, __m512i a, __m512i b)
{
    return (__mmask8)(k & host_mm512_testn_epi64_mask(a, b));
}

BUSTER_HOST_INLINE __mmask64 host_mm512_testn_epi8_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)((a_.u8[i] & b_.u8[i]) == 0) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_mask_testn_epi8_mask(__mmask64 k, __m512i a, __m512i b)
{
    return (__mmask64)(k & host_mm512_testn_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __m512i host_mm512_xor_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] ^ b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mask_xor_epi64(__m512i src, __mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_xor_epi64(a, b) };
    HostV512 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_add_epi32(__mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_add_epi32(a, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_add_epi64(__mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_add_epi64(a, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_add_epi8(__mmask64 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_add_epi8(a, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[i] = result.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_and_epi64(__mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_and_epi64(a, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_compress_epi16(__mmask32 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[at++] = a_.u16[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_compress_epi32(__mmask16 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[at++] = a_.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_compress_epi64(__mmask8 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[at++] = a_.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_compress_epi8(__mmask64 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[at++] = a_.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_conflict_epi32(__mmask16 k, __m512i a)
{
    HostV512 result = { .value = host_mm512_conflict_epi32(a) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_expand_epi32(__mmask16 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = a_.u32[at++]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_expand_epi64(__mmask8 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = a_.u64[at++]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_expand_epi8(__mmask64 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[i] = a_.u8[at++]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_load_epi32(__mmask16 k, const void* memory)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u32[i], (const uint8_t*)memory + i * 4, 4); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_loadu_epi16(__mmask32 k, const void* memory)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u16[i], (const uint8_t*)memory + i * 2, 2); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_loadu_epi32(__mmask16 k, const void* memory)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u32[i], (const uint8_t*)memory + i * 4, 4); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_loadu_epi64(__mmask8 k, const void* memory)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u64[i], (const uint8_t*)memory + i * 8, 8); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_loadu_epi8(__mmask64 k, const void* memory)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u8[i], (const uint8_t*)memory + i * 1, 1); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_mov_epi16(__mmask32 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u16[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_mov_epi32(__mmask16 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u32[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_mov_epi64(__mmask8 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u64[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_mov_epi8(__mmask64 k, __m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u8[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutex2var_epi32(__m512i a, __m512i index, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        unsigned at = (unsigned)index_.u32[i] & 31; r.u32[i] = at < 16 ? a_.u32[at] : b_.u32[at - 16];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_permutex2var_epi32(__mmask16 k, __m512i a, __m512i index, __m512i b)
{
    HostV512 result = { .value = host_mm512_permutex2var_epi32(a, index, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutex2var_epi64(__m512i a, __m512i index, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        unsigned at = (unsigned)index_.u64[i] & 15; r.u64[i] = at < 8 ? a_.u64[at] : b_.u64[at - 8];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_permutex2var_epi64(__mmask8 k, __m512i a, __m512i index, __m512i b)
{
    HostV512 result = { .value = host_mm512_permutex2var_epi64(a, index, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_permutexvar_epi16(__mmask32 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi16(index, a) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[i] = result.u16[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_permutexvar_epi32(__mmask16 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi32(index, a) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_permutexvar_epi64(__mmask8 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi64(index, a) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_permutexvar_epi8(__mmask64 k, __m512i index, __m512i a)
{
    HostV512 result = { .value = host_mm512_permutexvar_epi8(index, a) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[i] = result.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_set1_epi32(__mmask16 k, int32_t a)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? (uint32_t)a : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_set1_epi64(__mmask8 k, int64_t a)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = ((uint64_t)k >> i & 1) != 0 ? (uint64_t)a : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_set1_epi8(__mmask64 k, int a)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? (uint8_t)a : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_sllv_epi32(__m512i a, __m512i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = count_.u32[i]; r.u32[i] = amount >= 32 ? 0 : (uint32_t)((uint64_t)a_.u32[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_sllv_epi32(__mmask16 k, __m512i a, __m512i count)
{
    HostV512 result = { .value = host_mm512_sllv_epi32(a, count) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_sllv_epi64(__mmask8 k, __m512i a, __m512i count)
{
    HostV512 result = { .value = host_mm512_sllv_epi64(a, count) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_sub_epi32(__mmask16 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_sub_epi32(a, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u32[i] = result.u32[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_maskz_sub_epi64(__mmask8 k, __m512i a, __m512i b)
{
    HostV512 result = { .value = host_mm512_sub_epi64(a, b) };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u64[i] = result.u64[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_max_epu8(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = a_.u8[i] > b_.u8[i] ? a_.u8[i] : b_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_min_epi8(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = a_.i8[i] < b_.i8[i] ? a_.i8[i] : b_.i8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_min_epu8(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = a_.u8[i] < b_.u8[i] ? a_.u8[i] : b_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask64 host_mm512_movepi8_mask(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    uint64_t r = 0;
    for (unsigned i = 0; i < 64; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] >> 7) << i;
    }
    return (__mmask64)r;
}

BUSTER_HOST_INLINE __m512i host_mm512_movm_epi8(__mmask64 k)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? UINT8_MAX : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mul_epu32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)a_.u32[2 * i] * b_.u32[2 * i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_mullo_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)((uint64_t)a_.u32[i] * (uint64_t)b_.u32[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_multishift_epi64_epi8(__m512i control, __m512i a)
{
    HostV512 control_ __attribute__((unused)) = { .value = control };
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        uint64_t word = a_.u64[i / 8]; unsigned shift = control_.u8[i] & 63; r.u8[i] = (uint8_t)((word >> shift) | (word << ((64 - shift) & 63)));
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_or_si512(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] | b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutex2var_epi16(__m512i a, __m512i index, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        unsigned at = (unsigned)index_.u16[i] & 63; r.u16[i] = at < 32 ? a_.u16[at] : b_.u16[at - 32];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_permutex2var_epi8(__m512i a, __m512i index, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        unsigned at = (unsigned)index_.u8[i] & 127; r.u8[i] = at < 64 ? a_.u8[at] : b_.u8[at - 64];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_popcnt_epi32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)__builtin_popcountll(a_.u32[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_popcnt_epi64(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)__builtin_popcountll(a_.u64[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE int32_t host_mm512_reduce_add_epi32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    int32_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r = (int32_t)((uint32_t)r + a_.u32[i]);
    }
    return r;
}

BUSTER_HOST_INLINE int64_t host_mm512_reduce_add_epi64(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    int64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r = (int64_t)((uint64_t)r + a_.u64[i]);
    }
    return r;
}

BUSTER_HOST_INLINE int64_t host_mm512_reduce_max_epi64(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    int64_t r = a_.i64[0];
    for (unsigned i = 0; i < 8; i += 1)
    {
        r = r > a_.i64[i] ? r : a_.i64[i];
    }
    return r;
}

BUSTER_HOST_INLINE uint32_t host_mm512_reduce_max_epu32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    uint32_t r = a_.u32[0];
    for (unsigned i = 0; i < 16; i += 1)
    {
        r = r > a_.u32[i] ? r : a_.u32[i];
    }
    return r;
}

BUSTER_HOST_INLINE uint64_t host_mm512_reduce_max_epu64(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    uint64_t r = a_.u64[0];
    for (unsigned i = 0; i < 8; i += 1)
    {
        r = r > a_.u64[i] ? r : a_.u64[i];
    }
    return r;
}

BUSTER_HOST_INLINE uint32_t host_mm512_reduce_min_epu32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    uint32_t r = a_.u32[0];
    for (unsigned i = 0; i < 16; i += 1)
    {
        r = r < a_.u32[i] ? r : a_.u32[i];
    }
    return r;
}

BUSTER_HOST_INLINE int32_t host_mm512_reduce_or_epi32(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    int32_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r = r | a_.i32[i];
    }
    return r;
}

BUSTER_HOST_INLINE int64_t host_mm512_reduce_or_epi64(__m512i a)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    int64_t r = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        r = r | a_.i64[i];
    }
    return r;
}

BUSTER_HOST_INLINE __m512i host_mm512_rol_epi32(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        unsigned n = (unsigned)count & 31; r.u32[i] = (a_.u32[i] << n) | (a_.u32[i] >> ((32 - n) & 31));
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_rorv_epi32(__m512i a, __m512i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        unsigned n = count_.u32[i] & 31; r.u32[i] = (a_.u32[i] >> n) | (a_.u32[i] << ((32 - n) & 31));
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_sad_epu8(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        for (unsigned j = 0; j < 8; j += 1) { int difference = (int)a_.u8[8 * i + j] - b_.u8[8 * i + j]; r.u64[i] += (unsigned)(difference < 0 ? -difference : difference); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set1_epi16(int a)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = (uint16_t)a;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set1_epi32(int32_t a)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = (uint32_t)a;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set1_epi64(int64_t a)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = (uint64_t)a;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set1_epi8(int a)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = (uint8_t)a;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set4_epi32(int32_t a, int32_t b, int32_t c, int32_t d)
{
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = ((uint32_t[]){ (uint32_t)d, (uint32_t)c, (uint32_t)b, (uint32_t)a })[i & 3];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set_epi32(int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7, int32_t a8, int32_t a9, int32_t a10, int32_t a11, int32_t a12, int32_t a13, int32_t a14, int32_t a15)
{
    HostV512 r = { 0 };
    r.u32[0] = (uint32_t)a15;
    r.u32[1] = (uint32_t)a14;
    r.u32[2] = (uint32_t)a13;
    r.u32[3] = (uint32_t)a12;
    r.u32[4] = (uint32_t)a11;
    r.u32[5] = (uint32_t)a10;
    r.u32[6] = (uint32_t)a9;
    r.u32[7] = (uint32_t)a8;
    r.u32[8] = (uint32_t)a7;
    r.u32[9] = (uint32_t)a6;
    r.u32[10] = (uint32_t)a5;
    r.u32[11] = (uint32_t)a4;
    r.u32[12] = (uint32_t)a3;
    r.u32[13] = (uint32_t)a2;
    r.u32[14] = (uint32_t)a1;
    r.u32[15] = (uint32_t)a0;
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set_epi64(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6, int64_t a7)
{
    HostV512 r = { 0 };
    r.u64[0] = (uint64_t)a7;
    r.u64[1] = (uint64_t)a6;
    r.u64[2] = (uint64_t)a5;
    r.u64[3] = (uint64_t)a4;
    r.u64[4] = (uint64_t)a3;
    r.u64[5] = (uint64_t)a2;
    r.u64[6] = (uint64_t)a1;
    r.u64[7] = (uint64_t)a0;
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_set_epi8(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16, int a17, int a18, int a19, int a20, int a21, int a22, int a23, int a24, int a25, int a26, int a27, int a28, int a29, int a30, int a31, int a32, int a33, int a34, int a35, int a36, int a37, int a38, int a39, int a40, int a41, int a42, int a43, int a44, int a45, int a46, int a47, int a48, int a49, int a50, int a51, int a52, int a53, int a54, int a55, int a56, int a57, int a58, int a59, int a60, int a61, int a62, int a63)
{
    HostV512 r = { 0 };
    r.u8[0] = (uint8_t)a63;
    r.u8[1] = (uint8_t)a62;
    r.u8[2] = (uint8_t)a61;
    r.u8[3] = (uint8_t)a60;
    r.u8[4] = (uint8_t)a59;
    r.u8[5] = (uint8_t)a58;
    r.u8[6] = (uint8_t)a57;
    r.u8[7] = (uint8_t)a56;
    r.u8[8] = (uint8_t)a55;
    r.u8[9] = (uint8_t)a54;
    r.u8[10] = (uint8_t)a53;
    r.u8[11] = (uint8_t)a52;
    r.u8[12] = (uint8_t)a51;
    r.u8[13] = (uint8_t)a50;
    r.u8[14] = (uint8_t)a49;
    r.u8[15] = (uint8_t)a48;
    r.u8[16] = (uint8_t)a47;
    r.u8[17] = (uint8_t)a46;
    r.u8[18] = (uint8_t)a45;
    r.u8[19] = (uint8_t)a44;
    r.u8[20] = (uint8_t)a43;
    r.u8[21] = (uint8_t)a42;
    r.u8[22] = (uint8_t)a41;
    r.u8[23] = (uint8_t)a40;
    r.u8[24] = (uint8_t)a39;
    r.u8[25] = (uint8_t)a38;
    r.u8[26] = (uint8_t)a37;
    r.u8[27] = (uint8_t)a36;
    r.u8[28] = (uint8_t)a35;
    r.u8[29] = (uint8_t)a34;
    r.u8[30] = (uint8_t)a33;
    r.u8[31] = (uint8_t)a32;
    r.u8[32] = (uint8_t)a31;
    r.u8[33] = (uint8_t)a30;
    r.u8[34] = (uint8_t)a29;
    r.u8[35] = (uint8_t)a28;
    r.u8[36] = (uint8_t)a27;
    r.u8[37] = (uint8_t)a26;
    r.u8[38] = (uint8_t)a25;
    r.u8[39] = (uint8_t)a24;
    r.u8[40] = (uint8_t)a23;
    r.u8[41] = (uint8_t)a22;
    r.u8[42] = (uint8_t)a21;
    r.u8[43] = (uint8_t)a20;
    r.u8[44] = (uint8_t)a19;
    r.u8[45] = (uint8_t)a18;
    r.u8[46] = (uint8_t)a17;
    r.u8[47] = (uint8_t)a16;
    r.u8[48] = (uint8_t)a15;
    r.u8[49] = (uint8_t)a14;
    r.u8[50] = (uint8_t)a13;
    r.u8[51] = (uint8_t)a12;
    r.u8[52] = (uint8_t)a11;
    r.u8[53] = (uint8_t)a10;
    r.u8[54] = (uint8_t)a9;
    r.u8[55] = (uint8_t)a8;
    r.u8[56] = (uint8_t)a7;
    r.u8[57] = (uint8_t)a6;
    r.u8[58] = (uint8_t)a5;
    r.u8[59] = (uint8_t)a4;
    r.u8[60] = (uint8_t)a3;
    r.u8[61] = (uint8_t)a2;
    r.u8[62] = (uint8_t)a1;
    r.u8[63] = (uint8_t)a0;
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_setr_epi32(int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7, int32_t a8, int32_t a9, int32_t a10, int32_t a11, int32_t a12, int32_t a13, int32_t a14, int32_t a15)
{
    HostV512 r = { 0 };
    r.u32[0] = (uint32_t)a0;
    r.u32[1] = (uint32_t)a1;
    r.u32[2] = (uint32_t)a2;
    r.u32[3] = (uint32_t)a3;
    r.u32[4] = (uint32_t)a4;
    r.u32[5] = (uint32_t)a5;
    r.u32[6] = (uint32_t)a6;
    r.u32[7] = (uint32_t)a7;
    r.u32[8] = (uint32_t)a8;
    r.u32[9] = (uint32_t)a9;
    r.u32[10] = (uint32_t)a10;
    r.u32[11] = (uint32_t)a11;
    r.u32[12] = (uint32_t)a12;
    r.u32[13] = (uint32_t)a13;
    r.u32[14] = (uint32_t)a14;
    r.u32[15] = (uint32_t)a15;
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_setr_epi64(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6, int64_t a7)
{
    HostV512 r = { 0 };
    r.u64[0] = (uint64_t)a0;
    r.u64[1] = (uint64_t)a1;
    r.u64[2] = (uint64_t)a2;
    r.u64[3] = (uint64_t)a3;
    r.u64[4] = (uint64_t)a4;
    r.u64[5] = (uint64_t)a5;
    r.u64[6] = (uint64_t)a6;
    r.u64[7] = (uint64_t)a7;
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_setzero_si512(void)
{
    HostV512 r = { 0 };
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_shuffle_epi32(__m512i a, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u32[i] = a_.u32[(i & ~3u) + (((unsigned)immediate >> (2 * (i & 3))) & 3)];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_shuffle_epi8(__m512i a, __m512i index)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 index_ __attribute__((unused)) = { .value = index };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = (index_.u8[i] & 128) != 0 ? 0 : a_.u8[(i & ~15u) + (index_.u8[i] & 15)];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_shuffle_i32x4(__m512i a, __m512i b, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        unsigned at = ((unsigned)immediate >> (2 * i)) & 3; memcpy(r.u8 + 16 * i, (i < 2 ? a_.u8 : b_.u8) + 16 * at, 16);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_shuffle_i64x2(__m512i a, __m512i b, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        unsigned at = ((unsigned)immediate >> (2 * i)) & 3; memcpy(r.u8 + 16 * i, (i < 2 ? a_.u8 : b_.u8) + 16 * at, 16);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_sll_epi64(__m512i a, __m128i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = count_.u64[0]; r.u64[i] = amount >= 64 ? 0 : (uint64_t)((uint64_t)a_.u64[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_slli_epi16(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u16[i] = amount >= 16 ? 0 : (uint16_t)((uint64_t)a_.u16[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_slli_epi32(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u32[i] = amount >= 32 ? 0 : (uint32_t)((uint64_t)a_.u32[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_slli_epi64(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u64[i] = amount >= 64 ? 0 : (uint64_t)((uint64_t)a_.u64[i] << amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srai_epi64(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u64[i] = amount >= 64 ? (a_.i64[i] < 0 ? UINT64_MAX : 0) : (uint64_t)(a_.i64[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srav_epi32(__m512i a, __m512i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = count_.u32[i]; r.u32[i] = amount >= 32 ? (a_.i32[i] < 0 ? UINT32_MAX : 0) : (uint32_t)(a_.i32[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srl_epi64(__m512i a, __m128i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV128 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        uint64_t amount = count_.u64[0]; r.u64[i] = amount >= 64 ? 0 : (uint64_t)(a_.u64[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srli_epi16(__m512i a, int count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        uint64_t amount = (unsigned)count; r.u16[i] = amount >= 16 ? 0 : (uint16_t)(a_.u16[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_srlv_epi32(__m512i a, __m512i count)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 count_ __attribute__((unused)) = { .value = count };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint64_t amount = count_.u32[i]; r.u32[i] = amount >= 32 ? 0 : (uint32_t)(a_.u32[i] >> amount);
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm512_store_si512(void* memory, __m512i a)
{
    memcpy(memory, &a, 64);
}

BUSTER_HOST_INLINE void host_mm512_storeu_si512(void* memory, __m512i a)
{
    memcpy(memory, &a, 64);
}

BUSTER_HOST_INLINE void host_mm512_stream_si512(void* memory, __m512i a)
{
    memcpy(memory, &a, 64);
}

BUSTER_HOST_INLINE __m512i host_mm512_subs_epu16(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 32; i += 1)
    {
        r.u16[i] = a_.u16[i] > b_.u16[i] ? a_.u16[i] - b_.u16[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_subs_epu8(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 64; i += 1)
    {
        r.u8[i] = a_.u8[i] > b_.u8[i] ? a_.u8[i] - b_.u8[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_ternarylogic_epi32(__m512i a, __m512i b, __m512i c, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 c_ __attribute__((unused)) = { .value = c };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        for (unsigned bit = 0; bit < 8; bit += 1) { if (((unsigned)immediate >> bit & 1) != 0) { r.u64[i] |= ((bit & 4) != 0 ? a_.u64[i] : ~a_.u64[i]) & ((bit & 2) != 0 ? b_.u64[i] : ~b_.u64[i]) & ((bit & 1) != 0 ? c_.u64[i] : ~c_.u64[i]); } }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_ternarylogic_epi64(__m512i a, __m512i b, __m512i c, int immediate)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 c_ __attribute__((unused)) = { .value = c };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        for (unsigned bit = 0; bit < 8; bit += 1) { if (((unsigned)immediate >> bit & 1) != 0) { r.u64[i] |= ((bit & 4) != 0 ? a_.u64[i] : ~a_.u64[i]) & ((bit & 2) != 0 ? b_.u64[i] : ~b_.u64[i]) & ((bit & 1) != 0 ? c_.u64[i] : ~c_.u64[i]); } }
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_test_epi16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)((a_.u16[i] & b_.u16[i]) != 0) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __mmask32 host_mm512_testn_epi16_mask(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 32; i += 1)
    {
        r |= (uint64_t)((a_.u16[i] & b_.u16[i]) == 0) << i;
    }
    return (__mmask32)r;
}

BUSTER_HOST_INLINE __m512i host_mm512_unpackhi_epi16(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        unsigned at = (i / 4) * 8 + i % 4 + 4; r.u16[2 * i] = a_.u16[at]; r.u16[2 * i + 1] = b_.u16[at];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_unpackhi_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        unsigned at = (i / 2) * 4 + i % 2 + 2; r.u32[2 * i] = a_.u32[at]; r.u32[2 * i + 1] = b_.u32[at];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_unpackhi_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        unsigned at = (i / 1) * 2 + i % 1 + 1; r.u64[2 * i] = a_.u64[at]; r.u64[2 * i + 1] = b_.u64[at];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_unpacklo_epi16(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        unsigned at = (i / 4) * 8 + i % 4 + 0; r.u16[2 * i] = a_.u16[at]; r.u16[2 * i + 1] = b_.u16[at];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_unpacklo_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        unsigned at = (i / 2) * 4 + i % 2 + 0; r.u32[2 * i] = a_.u32[at]; r.u32[2 * i + 1] = b_.u32[at];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_unpacklo_epi64(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        unsigned at = (i / 1) * 2 + i % 1 + 0; r.u64[2 * i] = a_.u64[at]; r.u64[2 * i + 1] = b_.u64[at];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_xor_epi32(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] ^ b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_xor_si512(__m512i a, __m512i b)
{
    HostV512 a_ __attribute__((unused)) = { .value = a };
    HostV512 b_ __attribute__((unused)) = { .value = b };
    HostV512 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        r.u64[i] = a_.u64[i] ^ b_.u64[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_zextsi128_si512(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    memcpy(&r.value, &a, 16);
    return r.value;
}

BUSTER_HOST_INLINE __m512i host_mm512_zextsi256_si512(__m256i a)
{
    HostV256 a_ __attribute__((unused)) = { .value = a };
    HostV512 r = { 0 };
    memcpy(&r.value, &a, 32);
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_aesenclast_si128(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        unsigned row = i & 3; unsigned column = i / 4; r.u8[i] = host_aes_sbox[a_.u8[4 * ((column + row) & 3) + row]] ^ b_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask16 host_mm_cmpeq_epi8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.i8[i] == b_.i8[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm_cmpge_epu8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] >= b_.u8[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm_cmpgt_epu8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] > b_.u8[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm_cmple_epu8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] <= b_.u8[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask8 host_mm_cmplt_epu32_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 4; i += 1)
    {
        r |= (uint64_t)(a_.u32[i] < b_.u32[i]) << i;
    }
    return (__mmask8)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm_cmplt_epu8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] < b_.u8[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm_cmpneq_epi8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.i8[i] != b_.i8[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm_cmpneq_epu8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] != b_.u8[i]) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __m128i host_mm_cvtepi32_epi8(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u8[i] = (uint8_t)a_.i32[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_cvtepu8_epi32(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u32[i] = (uint32_t)a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE int host_mm_extract_epi32(__m128i a, int index)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    return (int)a_.u32[(unsigned)index & 3];
}

BUSTER_HOST_INLINE int host_mm_extract_epi8(__m128i a, int index)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    return (int)a_.u8[(unsigned)index & 15];
}

BUSTER_HOST_INLINE __m128i host_mm_insert_epi32(__m128i a, int value, int index)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    r = a_;
    r.u32[(unsigned)index & 3] = (uint32_t)value;
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maddubs_epi16(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        int value = (int)a_.u8[2 * i] * b_.i8[2 * i] + (int)a_.u8[2 * i + 1] * b_.i8[2 * i + 1]; r.i16[i] = (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_mask_blend_epi8(__mmask16 k, __m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? b_.u8[i] : a_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask16 host_mm_mask_cmpeq_epi8_mask(__mmask16 k, __m128i a, __m128i b)
{
    return (__mmask16)(k & host_mm_cmpeq_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm_mask_cmplt_epu8_mask(__mmask16 k, __m128i a, __m128i b)
{
    return (__mmask16)(k & host_mm_cmplt_epu8_mask(a, b));
}

BUSTER_HOST_INLINE __mmask16 host_mm_mask_cmpneq_epi8_mask(__mmask16 k, __m128i a, __m128i b)
{
    return (__mmask16)(k & host_mm_cmpneq_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __m128i host_mm_mask_mov_epi8(__m128i src, __mmask16 k, __m128i a)
{
    HostV128 src_ __attribute__((unused)) = { .value = src };
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u8[i] : src_.u8[i];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_permutexvar_epi8(__m128i index, __m128i a)
{
    HostV128 index_ __attribute__((unused)) = { .value = index };
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = a_.u8[index_.u8[i] & 15];
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_mask_permutexvar_epi8(__m128i src, __mmask16 k, __m128i index, __m128i a)
{
    HostV128 result = { .value = host_mm_permutexvar_epi8(index, a) };
    HostV128 r = { 0 };
    memcpy(&r.value, &src, sizeof(src));
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[i] = result.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE void host_mm_mask_storeu_epi16(void* memory, __mmask8 k, __m128i a)
{
    HostV128 a_ = { .value = a };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 2, &a_.u16[i], 2); }
    }
}

BUSTER_HOST_INLINE void host_mm_mask_storeu_epi8(void* memory, __mmask16 k, __m128i a)
{
    HostV128 a_ = { .value = a };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy((uint8_t*)memory + i * 1, &a_.u8[i], 1); }
    }
}

BUSTER_HOST_INLINE __mmask16 host_mm_testn_epi8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)((a_.u8[i] & b_.u8[i]) == 0) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __mmask16 host_mm_mask_testn_epi8_mask(__mmask16 k, __m128i a, __m128i b)
{
    return (__mmask16)(k & host_mm_testn_epi8_mask(a, b));
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_compress_epi16(__mmask8 k, __m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u16[at++] = a_.u16[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_compress_epi8(__mmask16 k, __m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    unsigned at = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { r.u8[at++] = a_.u8[i]; }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_loadu_epi16(__mmask8 k, const void* memory)
{
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 8; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u16[i], (const uint8_t*)memory + i * 2, 2); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_loadu_epi32(__mmask8 k, const void* memory)
{
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u32[i], (const uint8_t*)memory + i * 4, 4); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_loadu_epi8(__mmask16 k, const void* memory)
{
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        if (((uint64_t)k >> i & 1) != 0) { memcpy(&r.u8[i], (const uint8_t*)memory + i * 1, 1); }
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_mov_epi32(__mmask8 k, __m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u32[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u32[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_mov_epi8(__mmask16 k, __m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? a_.u8[i] : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_maskz_set1_epi8(__mmask16 k, int a)
{
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = ((uint64_t)k >> i & 1) != 0 ? (uint8_t)a : 0;
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask16 host_mm_movepi8_mask(__m128i a)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)(a_.u8[i] >> 7) << i;
    }
    return (__mmask16)r;
}

BUSTER_HOST_INLINE __m128i host_mm_mullo_epi64(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 2; i += 1)
    {
        r.u64[i] = (uint64_t)((uint64_t)a_.u64[i] * (uint64_t)b_.u64[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_packus_epi32(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 4; i += 1)
    {
        r.u16[i] = (uint16_t)(a_.i32[i] < 0 ? 0 : a_.i32[i] > UINT16_MAX ? UINT16_MAX : a_.i32[i]); r.u16[i + 4] = (uint16_t)(b_.i32[i] < 0 ? 0 : b_.i32[i] > UINT16_MAX ? UINT16_MAX : b_.i32[i]);
    }
    return r.value;
}

BUSTER_HOST_INLINE __m128i host_mm_shuffle_epi8(__m128i a, __m128i index)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 index_ __attribute__((unused)) = { .value = index };
    HostV128 r = { 0 };
    for (unsigned i = 0; i < 16; i += 1)
    {
        r.u8[i] = (index_.u8[i] & 128) != 0 ? 0 : a_.u8[(i & ~15u) + (index_.u8[i] & 15)];
    }
    return r.value;
}

BUSTER_HOST_INLINE __mmask16 host_mm_test_epi8_mask(__m128i a, __m128i b)
{
    HostV128 a_ __attribute__((unused)) = { .value = a };
    HostV128 b_ __attribute__((unused)) = { .value = b };
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i += 1)
    {
        r |= (uint64_t)((a_.u8[i] & b_.u8[i]) != 0) << i;
    }
    return (__mmask16)r;
}

#undef _mm256_add_epi16
#define _mm256_add_epi16 host_mm256_add_epi16

#undef _mm256_add_epi32
#define _mm256_add_epi32 host_mm256_add_epi32

#undef _mm256_add_epi64
#define _mm256_add_epi64 host_mm256_add_epi64

#undef _mm256_and_si256
#define _mm256_and_si256 host_mm256_and_si256

#undef _mm256_blendv_epi8
#define _mm256_blendv_epi8 host_mm256_blendv_epi8

#undef _mm256_broadcastsi128_si256
#define _mm256_broadcastsi128_si256 host_mm256_broadcastsi128_si256

#undef _mm256_castsi128_si256
#define _mm256_castsi128_si256 host_mm256_castsi128_si256

#undef _mm256_castsi256_ps
#define _mm256_castsi256_ps host_mm256_castsi256_ps

#undef _mm256_castsi256_si128
#define _mm256_castsi256_si128 host_mm256_castsi256_si128

#undef _mm256_cmpeq_epi32_mask
#define _mm256_cmpeq_epi32_mask host_mm256_cmpeq_epi32_mask

#undef _mm256_cmpeq_epi8_mask
#define _mm256_cmpeq_epi8_mask host_mm256_cmpeq_epi8_mask

#undef _mm256_cmpge_epu32_mask
#define _mm256_cmpge_epu32_mask host_mm256_cmpge_epu32_mask

#undef _mm256_cmpgt_epi32
#define _mm256_cmpgt_epi32 host_mm256_cmpgt_epi32

#undef _mm256_cmpgt_epi32_mask
#define _mm256_cmpgt_epi32_mask host_mm256_cmpgt_epi32_mask

#undef _mm256_cmple_epu32_mask
#define _mm256_cmple_epu32_mask host_mm256_cmple_epu32_mask

#undef _mm256_cmplt_epu8_mask
#define _mm256_cmplt_epu8_mask host_mm256_cmplt_epu8_mask

#undef _mm256_cvtepi16_epi8
#define _mm256_cvtepi16_epi8 host_mm256_cvtepi16_epi8

#undef _mm256_cvtepu8_epi16
#define _mm256_cvtepu8_epi16 host_mm256_cvtepu8_epi16

#undef _mm256_cvtepu8_epi32
#define _mm256_cvtepu8_epi32 host_mm256_cvtepu8_epi32

#undef _mm256_extract_epi64
#define _mm256_extract_epi64 host_mm256_extract_epi64

#undef _mm256_extracti128_si256
#define _mm256_extracti128_si256 host_mm256_extracti128_si256

#undef _mm256_inserti128_si256
#define _mm256_inserti128_si256 host_mm256_inserti128_si256

#undef _mm256_load_si256
#define _mm256_load_si256 host_mm256_load_si256

#undef _mm256_loadu_si256
#define _mm256_loadu_si256 host_mm256_loadu_si256

#undef _mm256_cmpneq_epi32_mask
#define _mm256_cmpneq_epi32_mask host_mm256_cmpneq_epi32_mask

#undef _mm256_mask_cmpneq_epi32_mask
#define _mm256_mask_cmpneq_epi32_mask host_mm256_mask_cmpneq_epi32_mask

#undef _mm256_mask_mov_epi32
#define _mm256_mask_mov_epi32 host_mm256_mask_mov_epi32

#undef _mm256_mask_set1_epi32
#define _mm256_mask_set1_epi32 host_mm256_mask_set1_epi32

#undef _mm256_mask_storeu_epi32
#define _mm256_mask_storeu_epi32 host_mm256_mask_storeu_epi32

#undef _mm256_mask_storeu_epi64
#define _mm256_mask_storeu_epi64 host_mm256_mask_storeu_epi64

#undef _mm256_maskz_add_epi16
#define _mm256_maskz_add_epi16 host_mm256_maskz_add_epi16

#undef _mm256_maskz_compress_epi32
#define _mm256_maskz_compress_epi32 host_mm256_maskz_compress_epi32

#undef _mm256_maskz_loadu_epi32
#define _mm256_maskz_loadu_epi32 host_mm256_maskz_loadu_epi32

#undef _mm256_maskz_loadu_epi8
#define _mm256_maskz_loadu_epi8 host_mm256_maskz_loadu_epi8

#undef _mm256_maskz_set1_epi32
#define _mm256_maskz_set1_epi32 host_mm256_maskz_set1_epi32

#undef _mm256_movemask_epi8
#define _mm256_movemask_epi8 host_mm256_movemask_epi8

#undef _mm256_movemask_ps
#define _mm256_movemask_ps host_mm256_movemask_ps

#undef _mm256_mullo_epi16
#define _mm256_mullo_epi16 host_mm256_mullo_epi16

#undef _mm256_or_si256
#define _mm256_or_si256 host_mm256_or_si256

#undef _mm256_permutex2var_epi8
#define _mm256_permutex2var_epi8 host_mm256_permutex2var_epi8

#undef _mm256_permutexvar_epi32
#define _mm256_permutexvar_epi32 host_mm256_permutexvar_epi32

#undef _mm256_set1_epi16
#define _mm256_set1_epi16 host_mm256_set1_epi16

#undef _mm256_set1_epi32
#define _mm256_set1_epi32 host_mm256_set1_epi32

#undef _mm256_set1_epi8
#define _mm256_set1_epi8 host_mm256_set1_epi8

#undef _mm256_setr_epi32
#define _mm256_setr_epi32 host_mm256_setr_epi32

#undef _mm256_setr_epi64x
#define _mm256_setr_epi64x host_mm256_setr_epi64x

#undef _mm256_setzero_si256
#define _mm256_setzero_si256 host_mm256_setzero_si256

#undef _mm256_shuffle_epi8
#define _mm256_shuffle_epi8 host_mm256_shuffle_epi8

#undef _mm256_slli_epi16
#define _mm256_slli_epi16 host_mm256_slli_epi16

#undef _mm256_slli_epi32
#define _mm256_slli_epi32 host_mm256_slli_epi32

#undef _mm256_srli_epi16
#define _mm256_srli_epi16 host_mm256_srli_epi16

#undef _mm256_srli_epi32
#define _mm256_srli_epi32 host_mm256_srli_epi32

#undef _mm256_storeu_si256
#define _mm256_storeu_si256 host_mm256_storeu_si256

#undef _mm256_sub_epi16
#define _mm256_sub_epi16 host_mm256_sub_epi16

#undef _mm256_sub_epi32
#define _mm256_sub_epi32 host_mm256_sub_epi32

#undef _mm256_sub_epi8
#define _mm256_sub_epi8 host_mm256_sub_epi8

#undef _mm256_test_epi16_mask
#define _mm256_test_epi16_mask host_mm256_test_epi16_mask

#undef _mm256_test_epi32_mask
#define _mm256_test_epi32_mask host_mm256_test_epi32_mask

#undef _mm256_xor_si256
#define _mm256_xor_si256 host_mm256_xor_si256

#undef _mm256_zextsi128_si256
#define _mm256_zextsi128_si256 host_mm256_zextsi128_si256

#undef _mm512_abs_epi32
#define _mm512_abs_epi32 host_mm512_abs_epi32

#undef _mm512_add_epi16
#define _mm512_add_epi16 host_mm512_add_epi16

#undef _mm512_add_epi32
#define _mm512_add_epi32 host_mm512_add_epi32

#undef _mm512_add_epi64
#define _mm512_add_epi64 host_mm512_add_epi64

#undef _mm512_add_epi8
#define _mm512_add_epi8 host_mm512_add_epi8

#undef _mm512_aesenc_epi128
#define _mm512_aesenc_epi128 host_mm512_aesenc_epi128

#undef _mm512_alignr_epi32
#define _mm512_alignr_epi32 host_mm512_alignr_epi32

#undef _mm512_alignr_epi64
#define _mm512_alignr_epi64 host_mm512_alignr_epi64

#undef _mm512_and_epi32
#define _mm512_and_epi32 host_mm512_and_epi32

#undef _mm512_and_epi64
#define _mm512_and_epi64 host_mm512_and_epi64

#undef _mm512_and_si512
#define _mm512_and_si512 host_mm512_and_si512

#undef _mm512_andnot_epi32
#define _mm512_andnot_epi32 host_mm512_andnot_epi32

#undef _mm512_andnot_si512
#define _mm512_andnot_si512 host_mm512_andnot_si512

#undef _mm512_bitshuffle_epi64_mask
#define _mm512_bitshuffle_epi64_mask host_mm512_bitshuffle_epi64_mask

#undef _mm512_broadcast_i32x4
#define _mm512_broadcast_i32x4 host_mm512_broadcast_i32x4

#undef _mm512_castsi128_si512
#define _mm512_castsi128_si512 host_mm512_castsi128_si512

#undef _mm512_castsi256_si512
#define _mm512_castsi256_si512 host_mm512_castsi256_si512

#undef _mm512_castsi512_si128
#define _mm512_castsi512_si128 host_mm512_castsi512_si128

#undef _mm512_castsi512_si256
#define _mm512_castsi512_si256 host_mm512_castsi512_si256

#undef _mm512_cmp_epi64_mask
#define _mm512_cmp_epi64_mask host_mm512_cmp_epi64_mask

#undef _mm512_cmp_epu64_mask
#define _mm512_cmp_epu64_mask host_mm512_cmp_epu64_mask

#undef _mm512_cmpeq_epi16_mask
#define _mm512_cmpeq_epi16_mask host_mm512_cmpeq_epi16_mask

#undef _mm512_cmpeq_epi32_mask
#define _mm512_cmpeq_epi32_mask host_mm512_cmpeq_epi32_mask

#undef _mm512_cmpeq_epi64_mask
#define _mm512_cmpeq_epi64_mask host_mm512_cmpeq_epi64_mask

#undef _mm512_cmpeq_epi8_mask
#define _mm512_cmpeq_epi8_mask host_mm512_cmpeq_epi8_mask

#undef _mm512_cmpge_epi32_mask
#define _mm512_cmpge_epi32_mask host_mm512_cmpge_epi32_mask

#undef _mm512_cmpge_epi64_mask
#define _mm512_cmpge_epi64_mask host_mm512_cmpge_epi64_mask

#undef _mm512_cmpge_epu16_mask
#define _mm512_cmpge_epu16_mask host_mm512_cmpge_epu16_mask

#undef _mm512_cmpge_epu32_mask
#define _mm512_cmpge_epu32_mask host_mm512_cmpge_epu32_mask

#undef _mm512_cmpge_epu64_mask
#define _mm512_cmpge_epu64_mask host_mm512_cmpge_epu64_mask

#undef _mm512_cmpge_epu8_mask
#define _mm512_cmpge_epu8_mask host_mm512_cmpge_epu8_mask

#undef _mm512_cmpgt_epi64_mask
#define _mm512_cmpgt_epi64_mask host_mm512_cmpgt_epi64_mask

#undef _mm512_cmpgt_epi8_mask
#define _mm512_cmpgt_epi8_mask host_mm512_cmpgt_epi8_mask

#undef _mm512_cmpgt_epu16_mask
#define _mm512_cmpgt_epu16_mask host_mm512_cmpgt_epu16_mask

#undef _mm512_cmpgt_epu32_mask
#define _mm512_cmpgt_epu32_mask host_mm512_cmpgt_epu32_mask

#undef _mm512_cmpgt_epu64_mask
#define _mm512_cmpgt_epu64_mask host_mm512_cmpgt_epu64_mask

#undef _mm512_cmpgt_epu8_mask
#define _mm512_cmpgt_epu8_mask host_mm512_cmpgt_epu8_mask

#undef _mm512_cmple_epi16_mask
#define _mm512_cmple_epi16_mask host_mm512_cmple_epi16_mask

#undef _mm512_cmple_epu32_mask
#define _mm512_cmple_epu32_mask host_mm512_cmple_epu32_mask

#undef _mm512_cmple_epu64_mask
#define _mm512_cmple_epu64_mask host_mm512_cmple_epu64_mask

#undef _mm512_cmple_epu8_mask
#define _mm512_cmple_epu8_mask host_mm512_cmple_epu8_mask

#undef _mm512_cmplt_epi8_mask
#define _mm512_cmplt_epi8_mask host_mm512_cmplt_epi8_mask

#undef _mm512_cmplt_epu16_mask
#define _mm512_cmplt_epu16_mask host_mm512_cmplt_epu16_mask

#undef _mm512_cmplt_epu32_mask
#define _mm512_cmplt_epu32_mask host_mm512_cmplt_epu32_mask

#undef _mm512_cmplt_epu64_mask
#define _mm512_cmplt_epu64_mask host_mm512_cmplt_epu64_mask

#undef _mm512_cmplt_epu8_mask
#define _mm512_cmplt_epu8_mask host_mm512_cmplt_epu8_mask

#undef _mm512_cmpneq_epi32_mask
#define _mm512_cmpneq_epi32_mask host_mm512_cmpneq_epi32_mask

#undef _mm512_cmpneq_epi64_mask
#define _mm512_cmpneq_epi64_mask host_mm512_cmpneq_epi64_mask

#undef _mm512_cmpneq_epi8_mask
#define _mm512_cmpneq_epi8_mask host_mm512_cmpneq_epi8_mask

#undef _mm512_cmpneq_epu32_mask
#define _mm512_cmpneq_epu32_mask host_mm512_cmpneq_epu32_mask

#undef _mm512_conflict_epi32
#define _mm512_conflict_epi32 host_mm512_conflict_epi32

#undef _mm512_cvtepi32_epi16
#define _mm512_cvtepi32_epi16 host_mm512_cvtepi32_epi16

#undef _mm512_cvtepi32_epi64
#define _mm512_cvtepi32_epi64 host_mm512_cvtepi32_epi64

#undef _mm512_cvtepi32_epi8
#define _mm512_cvtepi32_epi8 host_mm512_cvtepi32_epi8

#undef _mm512_cvtepi64_epi16
#define _mm512_cvtepi64_epi16 host_mm512_cvtepi64_epi16

#undef _mm512_cvtepi64_epi32
#define _mm512_cvtepi64_epi32 host_mm512_cvtepi64_epi32

#undef _mm512_cvtepi64_epi8
#define _mm512_cvtepi64_epi8 host_mm512_cvtepi64_epi8

#undef _mm512_cvtepi8_epi32
#define _mm512_cvtepi8_epi32 host_mm512_cvtepi8_epi32

#undef _mm512_cvtepu16_epi32
#define _mm512_cvtepu16_epi32 host_mm512_cvtepu16_epi32

#undef _mm512_cvtepu16_epi64
#define _mm512_cvtepu16_epi64 host_mm512_cvtepu16_epi64

#undef _mm512_cvtepu32_epi64
#define _mm512_cvtepu32_epi64 host_mm512_cvtepu32_epi64

#undef _mm512_cvtepu8_epi16
#define _mm512_cvtepu8_epi16 host_mm512_cvtepu8_epi16

#undef _mm512_cvtepu8_epi32
#define _mm512_cvtepu8_epi32 host_mm512_cvtepu8_epi32

#undef _mm512_cvtepu8_epi64
#define _mm512_cvtepu8_epi64 host_mm512_cvtepu8_epi64

#undef _mm512_cvtusepi16_epi8
#define _mm512_cvtusepi16_epi8 host_mm512_cvtusepi16_epi8

#undef _mm512_dpbusd_epi32
#define _mm512_dpbusd_epi32 host_mm512_dpbusd_epi32

#undef _mm512_extracti32x4_epi32
#define _mm512_extracti32x4_epi32 host_mm512_extracti32x4_epi32

#undef _mm512_extracti64x4_epi64
#define _mm512_extracti64x4_epi64 host_mm512_extracti64x4_epi64

#undef _mm512_gf2p8affine_epi64_epi8
#define _mm512_gf2p8affine_epi64_epi8 host_mm512_gf2p8affine_epi64_epi8

#undef _mm512_i32gather_epi32
#define _mm512_i32gather_epi32 host_mm512_i32gather_epi32

#undef _mm512_i32gather_epi64
#define _mm512_i32gather_epi64 host_mm512_i32gather_epi64

#undef _mm512_i32scatter_epi32
#define _mm512_i32scatter_epi32 host_mm512_i32scatter_epi32

#undef _mm512_inserti32x4
#define _mm512_inserti32x4 host_mm512_inserti32x4

#undef _mm512_inserti64x4
#define _mm512_inserti64x4 host_mm512_inserti64x4

#undef _mm512_kunpackb
#define _mm512_kunpackb host_mm512_kunpackb

#undef _mm512_kunpackd
#define _mm512_kunpackd host_mm512_kunpackd

#undef _mm512_kunpackw
#define _mm512_kunpackw host_mm512_kunpackw

#undef _mm512_load_si512
#define _mm512_load_si512 host_mm512_load_si512

#undef _mm512_loadu_si512
#define _mm512_loadu_si512 host_mm512_loadu_si512

#undef _mm512_lzcnt_epi32
#define _mm512_lzcnt_epi32 host_mm512_lzcnt_epi32

#undef _mm512_lzcnt_epi64
#define _mm512_lzcnt_epi64 host_mm512_lzcnt_epi64

#undef _mm512_madd_epi16
#define _mm512_madd_epi16 host_mm512_madd_epi16

#undef _mm512_maddubs_epi16
#define _mm512_maddubs_epi16 host_mm512_maddubs_epi16

#undef _mm512_mask_add_epi32
#define _mm512_mask_add_epi32 host_mm512_mask_add_epi32

#undef _mm512_mask_add_epi64
#define _mm512_mask_add_epi64 host_mm512_mask_add_epi64

#undef _mm512_mask_add_epi8
#define _mm512_mask_add_epi8 host_mm512_mask_add_epi8

#undef _mm512_mask_and_epi64
#define _mm512_mask_and_epi64 host_mm512_mask_and_epi64

#undef _mm512_andnot_epi64
#define _mm512_andnot_epi64 host_mm512_andnot_epi64

#undef _mm512_mask_andnot_epi64
#define _mm512_mask_andnot_epi64 host_mm512_mask_andnot_epi64

#undef _mm512_mask_bitshuffle_epi64_mask
#define _mm512_mask_bitshuffle_epi64_mask host_mm512_mask_bitshuffle_epi64_mask

#undef _mm512_mask_blend_epi32
#define _mm512_mask_blend_epi32 host_mm512_mask_blend_epi32

#undef _mm512_mask_blend_epi64
#define _mm512_mask_blend_epi64 host_mm512_mask_blend_epi64

#undef _mm512_mask_blend_epi8
#define _mm512_mask_blend_epi8 host_mm512_mask_blend_epi8

#undef _mm512_mask_cmpeq_epi16_mask
#define _mm512_mask_cmpeq_epi16_mask host_mm512_mask_cmpeq_epi16_mask

#undef _mm512_mask_cmpeq_epi32_mask
#define _mm512_mask_cmpeq_epi32_mask host_mm512_mask_cmpeq_epi32_mask

#undef _mm512_mask_cmpeq_epi64_mask
#define _mm512_mask_cmpeq_epi64_mask host_mm512_mask_cmpeq_epi64_mask

#undef _mm512_mask_cmpeq_epi8_mask
#define _mm512_mask_cmpeq_epi8_mask host_mm512_mask_cmpeq_epi8_mask

#undef _mm512_cmpeq_epu32_mask
#define _mm512_cmpeq_epu32_mask host_mm512_cmpeq_epu32_mask

#undef _mm512_mask_cmpeq_epu32_mask
#define _mm512_mask_cmpeq_epu32_mask host_mm512_mask_cmpeq_epu32_mask

#undef _mm512_mask_cmpge_epu32_mask
#define _mm512_mask_cmpge_epu32_mask host_mm512_mask_cmpge_epu32_mask

#undef _mm512_mask_cmpge_epu8_mask
#define _mm512_mask_cmpge_epu8_mask host_mm512_mask_cmpge_epu8_mask

#undef _mm512_cmpgt_epi32_mask
#define _mm512_cmpgt_epi32_mask host_mm512_cmpgt_epi32_mask

#undef _mm512_mask_cmpgt_epi32_mask
#define _mm512_mask_cmpgt_epi32_mask host_mm512_mask_cmpgt_epi32_mask

#undef _mm512_mask_cmpgt_epu32_mask
#define _mm512_mask_cmpgt_epu32_mask host_mm512_mask_cmpgt_epu32_mask

#undef _mm512_mask_cmpgt_epu64_mask
#define _mm512_mask_cmpgt_epu64_mask host_mm512_mask_cmpgt_epu64_mask

#undef _mm512_mask_cmpgt_epu8_mask
#define _mm512_mask_cmpgt_epu8_mask host_mm512_mask_cmpgt_epu8_mask

#undef _mm512_mask_cmple_epi16_mask
#define _mm512_mask_cmple_epi16_mask host_mm512_mask_cmple_epi16_mask

#undef _mm512_cmple_epu16_mask
#define _mm512_cmple_epu16_mask host_mm512_cmple_epu16_mask

#undef _mm512_mask_cmple_epu16_mask
#define _mm512_mask_cmple_epu16_mask host_mm512_mask_cmple_epu16_mask

#undef _mm512_mask_cmple_epu32_mask
#define _mm512_mask_cmple_epu32_mask host_mm512_mask_cmple_epu32_mask

#undef _mm512_mask_cmple_epu64_mask
#define _mm512_mask_cmple_epu64_mask host_mm512_mask_cmple_epu64_mask

#undef _mm512_mask_cmplt_epi8_mask
#define _mm512_mask_cmplt_epi8_mask host_mm512_mask_cmplt_epi8_mask

#undef _mm512_mask_cmplt_epu32_mask
#define _mm512_mask_cmplt_epu32_mask host_mm512_mask_cmplt_epu32_mask

#undef _mm512_mask_cmplt_epu8_mask
#define _mm512_mask_cmplt_epu8_mask host_mm512_mask_cmplt_epu8_mask

#undef _mm512_mask_cmpneq_epi32_mask
#define _mm512_mask_cmpneq_epi32_mask host_mm512_mask_cmpneq_epi32_mask

#undef _mm512_mask_cmpneq_epi64_mask
#define _mm512_mask_cmpneq_epi64_mask host_mm512_mask_cmpneq_epi64_mask

#undef _mm512_mask_cmpneq_epi8_mask
#define _mm512_mask_cmpneq_epi8_mask host_mm512_mask_cmpneq_epi8_mask

#undef _mm512_mask_cmpneq_epu32_mask
#define _mm512_mask_cmpneq_epu32_mask host_mm512_mask_cmpneq_epu32_mask

#undef _mm512_cmpneq_epu64_mask
#define _mm512_cmpneq_epu64_mask host_mm512_cmpneq_epu64_mask

#undef _mm512_mask_cmpneq_epu64_mask
#define _mm512_mask_cmpneq_epu64_mask host_mm512_mask_cmpneq_epu64_mask

#undef _mm512_mask_compress_epi16
#define _mm512_mask_compress_epi16 host_mm512_mask_compress_epi16

#undef _mm512_mask_compressstoreu_epi32
#define _mm512_mask_compressstoreu_epi32 host_mm512_mask_compressstoreu_epi32

#undef _mm512_mask_compressstoreu_epi8
#define _mm512_mask_compressstoreu_epi8 host_mm512_mask_compressstoreu_epi8

#undef _mm512_mask_expand_epi16
#define _mm512_mask_expand_epi16 host_mm512_mask_expand_epi16

#undef _mm512_mask_i32gather_epi32
#define _mm512_mask_i32gather_epi32 host_mm512_mask_i32gather_epi32

#undef _mm512_mask_i32gather_epi64
#define _mm512_mask_i32gather_epi64 host_mm512_mask_i32gather_epi64

#undef _mm512_mask_i32scatter_epi32
#define _mm512_mask_i32scatter_epi32 host_mm512_mask_i32scatter_epi32

#undef _mm512_mask_i64gather_epi64
#define _mm512_mask_i64gather_epi64 host_mm512_mask_i64gather_epi64

#undef _mm512_mask_i64scatter_epi32
#define _mm512_mask_i64scatter_epi32 host_mm512_mask_i64scatter_epi32

#undef _mm512_mask_i64scatter_epi64
#define _mm512_mask_i64scatter_epi64 host_mm512_mask_i64scatter_epi64

#undef _mm512_mask_loadu_epi16
#define _mm512_mask_loadu_epi16 host_mm512_mask_loadu_epi16

#undef _mm512_mask_loadu_epi32
#define _mm512_mask_loadu_epi32 host_mm512_mask_loadu_epi32

#undef _mm512_mask_loadu_epi64
#define _mm512_mask_loadu_epi64 host_mm512_mask_loadu_epi64

#undef _mm512_mask_loadu_epi8
#define _mm512_mask_loadu_epi8 host_mm512_mask_loadu_epi8

#undef _mm512_max_epi32
#define _mm512_max_epi32 host_mm512_max_epi32

#undef _mm512_mask_max_epi32
#define _mm512_mask_max_epi32 host_mm512_mask_max_epi32

#undef _mm512_max_epi64
#define _mm512_max_epi64 host_mm512_max_epi64

#undef _mm512_mask_max_epi64
#define _mm512_mask_max_epi64 host_mm512_mask_max_epi64

#undef _mm512_max_epu32
#define _mm512_max_epu32 host_mm512_max_epu32

#undef _mm512_mask_max_epu32
#define _mm512_mask_max_epu32 host_mm512_mask_max_epu32

#undef _mm512_max_epu64
#define _mm512_max_epu64 host_mm512_max_epu64

#undef _mm512_mask_max_epu64
#define _mm512_mask_max_epu64 host_mm512_mask_max_epu64

#undef _mm512_min_epu32
#define _mm512_min_epu32 host_mm512_min_epu32

#undef _mm512_mask_min_epu32
#define _mm512_mask_min_epu32 host_mm512_mask_min_epu32

#undef _mm512_mask_mov_epi16
#define _mm512_mask_mov_epi16 host_mm512_mask_mov_epi16

#undef _mm512_mask_mov_epi32
#define _mm512_mask_mov_epi32 host_mm512_mask_mov_epi32

#undef _mm512_mask_mov_epi64
#define _mm512_mask_mov_epi64 host_mm512_mask_mov_epi64

#undef _mm512_mask_mov_epi8
#define _mm512_mask_mov_epi8 host_mm512_mask_mov_epi8

#undef _mm512_mullo_epi64
#define _mm512_mullo_epi64 host_mm512_mullo_epi64

#undef _mm512_mask_mullo_epi64
#define _mm512_mask_mullo_epi64 host_mm512_mask_mullo_epi64

#undef _mm512_or_epi32
#define _mm512_or_epi32 host_mm512_or_epi32

#undef _mm512_mask_or_epi32
#define _mm512_mask_or_epi32 host_mm512_mask_or_epi32

#undef _mm512_or_epi64
#define _mm512_or_epi64 host_mm512_or_epi64

#undef _mm512_mask_or_epi64
#define _mm512_mask_or_epi64 host_mm512_mask_or_epi64

#undef _mm512_permutexvar_epi16
#define _mm512_permutexvar_epi16 host_mm512_permutexvar_epi16

#undef _mm512_mask_permutexvar_epi16
#define _mm512_mask_permutexvar_epi16 host_mm512_mask_permutexvar_epi16

#undef _mm512_permutexvar_epi32
#define _mm512_permutexvar_epi32 host_mm512_permutexvar_epi32

#undef _mm512_mask_permutexvar_epi32
#define _mm512_mask_permutexvar_epi32 host_mm512_mask_permutexvar_epi32

#undef _mm512_permutexvar_epi64
#define _mm512_permutexvar_epi64 host_mm512_permutexvar_epi64

#undef _mm512_mask_permutexvar_epi64
#define _mm512_mask_permutexvar_epi64 host_mm512_mask_permutexvar_epi64

#undef _mm512_permutexvar_epi8
#define _mm512_permutexvar_epi8 host_mm512_permutexvar_epi8

#undef _mm512_mask_permutexvar_epi8
#define _mm512_mask_permutexvar_epi8 host_mm512_mask_permutexvar_epi8

#undef _mm512_mask_reduce_add_epi32
#define _mm512_mask_reduce_add_epi32 host_mm512_mask_reduce_add_epi32

#undef _mm512_mask_reduce_or_epi32
#define _mm512_mask_reduce_or_epi32 host_mm512_mask_reduce_or_epi32

#undef _mm512_mask_set1_epi32
#define _mm512_mask_set1_epi32 host_mm512_mask_set1_epi32

#undef _mm512_mask_set1_epi64
#define _mm512_mask_set1_epi64 host_mm512_mask_set1_epi64

#undef _mm512_mask_set1_epi8
#define _mm512_mask_set1_epi8 host_mm512_mask_set1_epi8

#undef _mm512_sllv_epi64
#define _mm512_sllv_epi64 host_mm512_sllv_epi64

#undef _mm512_mask_sllv_epi64
#define _mm512_mask_sllv_epi64 host_mm512_mask_sllv_epi64

#undef _mm512_srai_epi32
#define _mm512_srai_epi32 host_mm512_srai_epi32

#undef _mm512_mask_srai_epi32
#define _mm512_mask_srai_epi32 host_mm512_mask_srai_epi32

#undef _mm512_srav_epi64
#define _mm512_srav_epi64 host_mm512_srav_epi64

#undef _mm512_mask_srav_epi64
#define _mm512_mask_srav_epi64 host_mm512_mask_srav_epi64

#undef _mm512_srli_epi32
#define _mm512_srli_epi32 host_mm512_srli_epi32

#undef _mm512_mask_srli_epi32
#define _mm512_mask_srli_epi32 host_mm512_mask_srli_epi32

#undef _mm512_srli_epi64
#define _mm512_srli_epi64 host_mm512_srli_epi64

#undef _mm512_mask_srli_epi64
#define _mm512_mask_srli_epi64 host_mm512_mask_srli_epi64

#undef _mm512_srlv_epi64
#define _mm512_srlv_epi64 host_mm512_srlv_epi64

#undef _mm512_mask_srlv_epi64
#define _mm512_mask_srlv_epi64 host_mm512_mask_srlv_epi64

#undef _mm512_mask_store_epi32
#define _mm512_mask_store_epi32 host_mm512_mask_store_epi32

#undef _mm512_mask_storeu_epi16
#define _mm512_mask_storeu_epi16 host_mm512_mask_storeu_epi16

#undef _mm512_mask_storeu_epi32
#define _mm512_mask_storeu_epi32 host_mm512_mask_storeu_epi32

#undef _mm512_mask_storeu_epi64
#define _mm512_mask_storeu_epi64 host_mm512_mask_storeu_epi64

#undef _mm512_mask_storeu_epi8
#define _mm512_mask_storeu_epi8 host_mm512_mask_storeu_epi8

#undef _mm512_sub_epi16
#define _mm512_sub_epi16 host_mm512_sub_epi16

#undef _mm512_mask_sub_epi16
#define _mm512_mask_sub_epi16 host_mm512_mask_sub_epi16

#undef _mm512_sub_epi32
#define _mm512_sub_epi32 host_mm512_sub_epi32

#undef _mm512_mask_sub_epi32
#define _mm512_mask_sub_epi32 host_mm512_mask_sub_epi32

#undef _mm512_sub_epi64
#define _mm512_sub_epi64 host_mm512_sub_epi64

#undef _mm512_mask_sub_epi64
#define _mm512_mask_sub_epi64 host_mm512_mask_sub_epi64

#undef _mm512_sub_epi8
#define _mm512_sub_epi8 host_mm512_sub_epi8

#undef _mm512_mask_sub_epi8
#define _mm512_mask_sub_epi8 host_mm512_mask_sub_epi8

#undef _mm512_test_epi32_mask
#define _mm512_test_epi32_mask host_mm512_test_epi32_mask

#undef _mm512_mask_test_epi32_mask
#define _mm512_mask_test_epi32_mask host_mm512_mask_test_epi32_mask

#undef _mm512_test_epi64_mask
#define _mm512_test_epi64_mask host_mm512_test_epi64_mask

#undef _mm512_mask_test_epi64_mask
#define _mm512_mask_test_epi64_mask host_mm512_mask_test_epi64_mask

#undef _mm512_test_epi8_mask
#define _mm512_test_epi8_mask host_mm512_test_epi8_mask

#undef _mm512_mask_test_epi8_mask
#define _mm512_mask_test_epi8_mask host_mm512_mask_test_epi8_mask

#undef _mm512_testn_epi32_mask
#define _mm512_testn_epi32_mask host_mm512_testn_epi32_mask

#undef _mm512_mask_testn_epi32_mask
#define _mm512_mask_testn_epi32_mask host_mm512_mask_testn_epi32_mask

#undef _mm512_testn_epi64_mask
#define _mm512_testn_epi64_mask host_mm512_testn_epi64_mask

#undef _mm512_mask_testn_epi64_mask
#define _mm512_mask_testn_epi64_mask host_mm512_mask_testn_epi64_mask

#undef _mm512_testn_epi8_mask
#define _mm512_testn_epi8_mask host_mm512_testn_epi8_mask

#undef _mm512_mask_testn_epi8_mask
#define _mm512_mask_testn_epi8_mask host_mm512_mask_testn_epi8_mask

#undef _mm512_xor_epi64
#define _mm512_xor_epi64 host_mm512_xor_epi64

#undef _mm512_mask_xor_epi64
#define _mm512_mask_xor_epi64 host_mm512_mask_xor_epi64

#undef _mm512_maskz_add_epi32
#define _mm512_maskz_add_epi32 host_mm512_maskz_add_epi32

#undef _mm512_maskz_add_epi64
#define _mm512_maskz_add_epi64 host_mm512_maskz_add_epi64

#undef _mm512_maskz_add_epi8
#define _mm512_maskz_add_epi8 host_mm512_maskz_add_epi8

#undef _mm512_maskz_and_epi64
#define _mm512_maskz_and_epi64 host_mm512_maskz_and_epi64

#undef _mm512_maskz_compress_epi16
#define _mm512_maskz_compress_epi16 host_mm512_maskz_compress_epi16

#undef _mm512_maskz_compress_epi32
#define _mm512_maskz_compress_epi32 host_mm512_maskz_compress_epi32

#undef _mm512_maskz_compress_epi64
#define _mm512_maskz_compress_epi64 host_mm512_maskz_compress_epi64

#undef _mm512_maskz_compress_epi8
#define _mm512_maskz_compress_epi8 host_mm512_maskz_compress_epi8

#undef _mm512_maskz_conflict_epi32
#define _mm512_maskz_conflict_epi32 host_mm512_maskz_conflict_epi32

#undef _mm512_maskz_expand_epi32
#define _mm512_maskz_expand_epi32 host_mm512_maskz_expand_epi32

#undef _mm512_maskz_expand_epi64
#define _mm512_maskz_expand_epi64 host_mm512_maskz_expand_epi64

#undef _mm512_maskz_expand_epi8
#define _mm512_maskz_expand_epi8 host_mm512_maskz_expand_epi8

#undef _mm512_maskz_load_epi32
#define _mm512_maskz_load_epi32 host_mm512_maskz_load_epi32

#undef _mm512_maskz_loadu_epi16
#define _mm512_maskz_loadu_epi16 host_mm512_maskz_loadu_epi16

#undef _mm512_maskz_loadu_epi32
#define _mm512_maskz_loadu_epi32 host_mm512_maskz_loadu_epi32

#undef _mm512_maskz_loadu_epi64
#define _mm512_maskz_loadu_epi64 host_mm512_maskz_loadu_epi64

#undef _mm512_maskz_loadu_epi8
#define _mm512_maskz_loadu_epi8 host_mm512_maskz_loadu_epi8

#undef _mm512_maskz_mov_epi16
#define _mm512_maskz_mov_epi16 host_mm512_maskz_mov_epi16

#undef _mm512_maskz_mov_epi32
#define _mm512_maskz_mov_epi32 host_mm512_maskz_mov_epi32

#undef _mm512_maskz_mov_epi64
#define _mm512_maskz_mov_epi64 host_mm512_maskz_mov_epi64

#undef _mm512_maskz_mov_epi8
#define _mm512_maskz_mov_epi8 host_mm512_maskz_mov_epi8

#undef _mm512_permutex2var_epi32
#define _mm512_permutex2var_epi32 host_mm512_permutex2var_epi32

#undef _mm512_maskz_permutex2var_epi32
#define _mm512_maskz_permutex2var_epi32 host_mm512_maskz_permutex2var_epi32

#undef _mm512_permutex2var_epi64
#define _mm512_permutex2var_epi64 host_mm512_permutex2var_epi64

#undef _mm512_maskz_permutex2var_epi64
#define _mm512_maskz_permutex2var_epi64 host_mm512_maskz_permutex2var_epi64

#undef _mm512_maskz_permutexvar_epi16
#define _mm512_maskz_permutexvar_epi16 host_mm512_maskz_permutexvar_epi16

#undef _mm512_maskz_permutexvar_epi32
#define _mm512_maskz_permutexvar_epi32 host_mm512_maskz_permutexvar_epi32

#undef _mm512_maskz_permutexvar_epi64
#define _mm512_maskz_permutexvar_epi64 host_mm512_maskz_permutexvar_epi64

#undef _mm512_maskz_permutexvar_epi8
#define _mm512_maskz_permutexvar_epi8 host_mm512_maskz_permutexvar_epi8

#undef _mm512_maskz_set1_epi32
#define _mm512_maskz_set1_epi32 host_mm512_maskz_set1_epi32

#undef _mm512_maskz_set1_epi64
#define _mm512_maskz_set1_epi64 host_mm512_maskz_set1_epi64

#undef _mm512_maskz_set1_epi8
#define _mm512_maskz_set1_epi8 host_mm512_maskz_set1_epi8

#undef _mm512_sllv_epi32
#define _mm512_sllv_epi32 host_mm512_sllv_epi32

#undef _mm512_maskz_sllv_epi32
#define _mm512_maskz_sllv_epi32 host_mm512_maskz_sllv_epi32

#undef _mm512_maskz_sllv_epi64
#define _mm512_maskz_sllv_epi64 host_mm512_maskz_sllv_epi64

#undef _mm512_maskz_sub_epi32
#define _mm512_maskz_sub_epi32 host_mm512_maskz_sub_epi32

#undef _mm512_maskz_sub_epi64
#define _mm512_maskz_sub_epi64 host_mm512_maskz_sub_epi64

#undef _mm512_max_epu8
#define _mm512_max_epu8 host_mm512_max_epu8

#undef _mm512_min_epi8
#define _mm512_min_epi8 host_mm512_min_epi8

#undef _mm512_min_epu8
#define _mm512_min_epu8 host_mm512_min_epu8

#undef _mm512_movepi8_mask
#define _mm512_movepi8_mask host_mm512_movepi8_mask

#undef _mm512_movm_epi8
#define _mm512_movm_epi8 host_mm512_movm_epi8

#undef _mm512_mul_epu32
#define _mm512_mul_epu32 host_mm512_mul_epu32

#undef _mm512_mullo_epi32
#define _mm512_mullo_epi32 host_mm512_mullo_epi32

#undef _mm512_multishift_epi64_epi8
#define _mm512_multishift_epi64_epi8 host_mm512_multishift_epi64_epi8

#undef _mm512_or_si512
#define _mm512_or_si512 host_mm512_or_si512

#undef _mm512_permutex2var_epi16
#define _mm512_permutex2var_epi16 host_mm512_permutex2var_epi16

#undef _mm512_permutex2var_epi8
#define _mm512_permutex2var_epi8 host_mm512_permutex2var_epi8

#undef _mm512_popcnt_epi32
#define _mm512_popcnt_epi32 host_mm512_popcnt_epi32

#undef _mm512_popcnt_epi64
#define _mm512_popcnt_epi64 host_mm512_popcnt_epi64

#undef _mm512_reduce_add_epi32
#define _mm512_reduce_add_epi32 host_mm512_reduce_add_epi32

#undef _mm512_reduce_add_epi64
#define _mm512_reduce_add_epi64 host_mm512_reduce_add_epi64

#undef _mm512_reduce_max_epi64
#define _mm512_reduce_max_epi64 host_mm512_reduce_max_epi64

#undef _mm512_reduce_max_epu32
#define _mm512_reduce_max_epu32 host_mm512_reduce_max_epu32

#undef _mm512_reduce_max_epu64
#define _mm512_reduce_max_epu64 host_mm512_reduce_max_epu64

#undef _mm512_reduce_min_epu32
#define _mm512_reduce_min_epu32 host_mm512_reduce_min_epu32

#undef _mm512_reduce_or_epi32
#define _mm512_reduce_or_epi32 host_mm512_reduce_or_epi32

#undef _mm512_reduce_or_epi64
#define _mm512_reduce_or_epi64 host_mm512_reduce_or_epi64

#undef _mm512_rol_epi32
#define _mm512_rol_epi32 host_mm512_rol_epi32

#undef _mm512_rorv_epi32
#define _mm512_rorv_epi32 host_mm512_rorv_epi32

#undef _mm512_sad_epu8
#define _mm512_sad_epu8 host_mm512_sad_epu8

#undef _mm512_set1_epi16
#define _mm512_set1_epi16 host_mm512_set1_epi16

#undef _mm512_set1_epi32
#define _mm512_set1_epi32 host_mm512_set1_epi32

#undef _mm512_set1_epi64
#define _mm512_set1_epi64 host_mm512_set1_epi64

#undef _mm512_set1_epi8
#define _mm512_set1_epi8 host_mm512_set1_epi8

#undef _mm512_set4_epi32
#define _mm512_set4_epi32 host_mm512_set4_epi32

#undef _mm512_set_epi32
#define _mm512_set_epi32 host_mm512_set_epi32

#undef _mm512_set_epi64
#define _mm512_set_epi64 host_mm512_set_epi64

#undef _mm512_set_epi8
#define _mm512_set_epi8 host_mm512_set_epi8

#undef _mm512_setr_epi32
#define _mm512_setr_epi32 host_mm512_setr_epi32

#undef _mm512_setr_epi64
#define _mm512_setr_epi64 host_mm512_setr_epi64

#undef _mm512_setzero_si512
#define _mm512_setzero_si512 host_mm512_setzero_si512

#undef _mm512_shuffle_epi32
#define _mm512_shuffle_epi32 host_mm512_shuffle_epi32

#undef _mm512_shuffle_epi8
#define _mm512_shuffle_epi8 host_mm512_shuffle_epi8

#undef _mm512_shuffle_i32x4
#define _mm512_shuffle_i32x4 host_mm512_shuffle_i32x4

#undef _mm512_shuffle_i64x2
#define _mm512_shuffle_i64x2 host_mm512_shuffle_i64x2

#undef _mm512_sll_epi64
#define _mm512_sll_epi64 host_mm512_sll_epi64

#undef _mm512_slli_epi16
#define _mm512_slli_epi16 host_mm512_slli_epi16

#undef _mm512_slli_epi32
#define _mm512_slli_epi32 host_mm512_slli_epi32

#undef _mm512_slli_epi64
#define _mm512_slli_epi64 host_mm512_slli_epi64

#undef _mm512_srai_epi64
#define _mm512_srai_epi64 host_mm512_srai_epi64

#undef _mm512_srav_epi32
#define _mm512_srav_epi32 host_mm512_srav_epi32

#undef _mm512_srl_epi64
#define _mm512_srl_epi64 host_mm512_srl_epi64

#undef _mm512_srli_epi16
#define _mm512_srli_epi16 host_mm512_srli_epi16

#undef _mm512_srlv_epi32
#define _mm512_srlv_epi32 host_mm512_srlv_epi32

#undef _mm512_store_si512
#define _mm512_store_si512 host_mm512_store_si512

#undef _mm512_storeu_si512
#define _mm512_storeu_si512 host_mm512_storeu_si512

#undef _mm512_stream_si512
#define _mm512_stream_si512 host_mm512_stream_si512

#undef _mm512_subs_epu16
#define _mm512_subs_epu16 host_mm512_subs_epu16

#undef _mm512_subs_epu8
#define _mm512_subs_epu8 host_mm512_subs_epu8

#undef _mm512_ternarylogic_epi32
#define _mm512_ternarylogic_epi32 host_mm512_ternarylogic_epi32

#undef _mm512_ternarylogic_epi64
#define _mm512_ternarylogic_epi64 host_mm512_ternarylogic_epi64

#undef _mm512_test_epi16_mask
#define _mm512_test_epi16_mask host_mm512_test_epi16_mask

#undef _mm512_testn_epi16_mask
#define _mm512_testn_epi16_mask host_mm512_testn_epi16_mask

#undef _mm512_unpackhi_epi16
#define _mm512_unpackhi_epi16 host_mm512_unpackhi_epi16

#undef _mm512_unpackhi_epi32
#define _mm512_unpackhi_epi32 host_mm512_unpackhi_epi32

#undef _mm512_unpackhi_epi64
#define _mm512_unpackhi_epi64 host_mm512_unpackhi_epi64

#undef _mm512_unpacklo_epi16
#define _mm512_unpacklo_epi16 host_mm512_unpacklo_epi16

#undef _mm512_unpacklo_epi32
#define _mm512_unpacklo_epi32 host_mm512_unpacklo_epi32

#undef _mm512_unpacklo_epi64
#define _mm512_unpacklo_epi64 host_mm512_unpacklo_epi64

#undef _mm512_xor_epi32
#define _mm512_xor_epi32 host_mm512_xor_epi32

#undef _mm512_xor_si512
#define _mm512_xor_si512 host_mm512_xor_si512

#undef _mm512_zextsi128_si512
#define _mm512_zextsi128_si512 host_mm512_zextsi128_si512

#undef _mm512_zextsi256_si512
#define _mm512_zextsi256_si512 host_mm512_zextsi256_si512

#undef _mm_aesenclast_si128
#define _mm_aesenclast_si128 host_mm_aesenclast_si128

#undef _mm_cmpeq_epi8_mask
#define _mm_cmpeq_epi8_mask host_mm_cmpeq_epi8_mask

#undef _mm_cmpge_epu8_mask
#define _mm_cmpge_epu8_mask host_mm_cmpge_epu8_mask

#undef _mm_cmpgt_epu8_mask
#define _mm_cmpgt_epu8_mask host_mm_cmpgt_epu8_mask

#undef _mm_cmple_epu8_mask
#define _mm_cmple_epu8_mask host_mm_cmple_epu8_mask

#undef _mm_cmplt_epu32_mask
#define _mm_cmplt_epu32_mask host_mm_cmplt_epu32_mask

#undef _mm_cmplt_epu8_mask
#define _mm_cmplt_epu8_mask host_mm_cmplt_epu8_mask

#undef _mm_cmpneq_epi8_mask
#define _mm_cmpneq_epi8_mask host_mm_cmpneq_epi8_mask

#undef _mm_cmpneq_epu8_mask
#define _mm_cmpneq_epu8_mask host_mm_cmpneq_epu8_mask

#undef _mm_cvtepi32_epi8
#define _mm_cvtepi32_epi8 host_mm_cvtepi32_epi8

#undef _mm_cvtepu8_epi32
#define _mm_cvtepu8_epi32 host_mm_cvtepu8_epi32

#undef _mm_extract_epi32
#define _mm_extract_epi32 host_mm_extract_epi32

#undef _mm_extract_epi8
#define _mm_extract_epi8 host_mm_extract_epi8

#undef _mm_insert_epi32
#define _mm_insert_epi32 host_mm_insert_epi32

#undef _mm_maddubs_epi16
#define _mm_maddubs_epi16 host_mm_maddubs_epi16

#undef _mm_mask_blend_epi8
#define _mm_mask_blend_epi8 host_mm_mask_blend_epi8

#undef _mm_mask_cmpeq_epi8_mask
#define _mm_mask_cmpeq_epi8_mask host_mm_mask_cmpeq_epi8_mask

#undef _mm_mask_cmplt_epu8_mask
#define _mm_mask_cmplt_epu8_mask host_mm_mask_cmplt_epu8_mask

#undef _mm_mask_cmpneq_epi8_mask
#define _mm_mask_cmpneq_epi8_mask host_mm_mask_cmpneq_epi8_mask

#undef _mm_mask_mov_epi8
#define _mm_mask_mov_epi8 host_mm_mask_mov_epi8

#undef _mm_permutexvar_epi8
#define _mm_permutexvar_epi8 host_mm_permutexvar_epi8

#undef _mm_mask_permutexvar_epi8
#define _mm_mask_permutexvar_epi8 host_mm_mask_permutexvar_epi8

#undef _mm_mask_storeu_epi16
#define _mm_mask_storeu_epi16 host_mm_mask_storeu_epi16

#undef _mm_mask_storeu_epi8
#define _mm_mask_storeu_epi8 host_mm_mask_storeu_epi8

#undef _mm_testn_epi8_mask
#define _mm_testn_epi8_mask host_mm_testn_epi8_mask

#undef _mm_mask_testn_epi8_mask
#define _mm_mask_testn_epi8_mask host_mm_mask_testn_epi8_mask

#undef _mm_maskz_compress_epi16
#define _mm_maskz_compress_epi16 host_mm_maskz_compress_epi16

#undef _mm_maskz_compress_epi8
#define _mm_maskz_compress_epi8 host_mm_maskz_compress_epi8

#undef _mm_maskz_loadu_epi16
#define _mm_maskz_loadu_epi16 host_mm_maskz_loadu_epi16

#undef _mm_maskz_loadu_epi32
#define _mm_maskz_loadu_epi32 host_mm_maskz_loadu_epi32

#undef _mm_maskz_loadu_epi8
#define _mm_maskz_loadu_epi8 host_mm_maskz_loadu_epi8

#undef _mm_maskz_mov_epi32
#define _mm_maskz_mov_epi32 host_mm_maskz_mov_epi32

#undef _mm_maskz_mov_epi8
#define _mm_maskz_mov_epi8 host_mm_maskz_mov_epi8

#undef _mm_maskz_set1_epi8
#define _mm_maskz_set1_epi8 host_mm_maskz_set1_epi8

#undef _mm_movepi8_mask
#define _mm_movepi8_mask host_mm_movepi8_mask

#undef _mm_mullo_epi64
#define _mm_mullo_epi64 host_mm_mullo_epi64

#undef _mm_packus_epi32
#define _mm_packus_epi32 host_mm_packus_epi32

#undef _mm_shuffle_epi8
#define _mm_shuffle_epi8 host_mm_shuffle_epi8

#undef _mm_test_epi8_mask
#define _mm_test_epi8_mask host_mm_test_epi8_mask

#endif
