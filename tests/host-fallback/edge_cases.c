// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Standalone baseline-host regression: clang -O2 -march=x86-64 -I src/buster/compiler tests/host-fallback/edge_cases.c -o /tmp/host-edge &&
// /tmp/host-edge
#define _GNU_SOURCE
#include "host_isa.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#if BUSTER_HOST_VECTOR
#error This test must be compiled for the baseline host.
#endif

static void test_memory(void)
{
    const long size = sysconf(_SC_PAGESIZE);
    assert(size > 128);
    uint8_t* p = mmap(NULL, (size_t)size * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    assert(mprotect(p + size, (size_t)size, PROT_READ | PROT_WRITE) == 0);
    uint8_t* end = p + size * 2;
    for (unsigned i = 0; i < 32; i += 1)
    {
        (end - 32)[i] = (uint8_t)(i + 1);
    }
    HostV512 src = {0}, got, indexes = {0}, values = {0};
    for (unsigned i = 0; i < 64; i += 1)
    {
        src.u8[i] = 0xa5;
    }
    got.value = _mm512_mask_loadu_epi8(src.value, UINT64_C(0x5555), end - 16);
    for (unsigned i = 0; i < 64; i += 1)
    {
        assert(got.u8[i] == (i < 16 && (i & 1) == 0 ? (uint8_t)(i + 17) : 0xa5));
    }
    got.value = _mm512_maskz_loadu_epi32(7, end - 12);
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint32_t expected = 0;
        if (i < 3)
        {
            memcpy(&expected, end - 12 + 4 * i, 4);
        }
        assert(got.u32[i] == expected);
    }
    _mm512_mask_storeu_epi8(end - 16, UINT64_C(0xaaaa), src.value);
    for (unsigned i = 0; i < 16; i += 1)
    {
        assert((end - 16)[i] == ((i & 1) ? 0xa5 : (uint8_t)(i + 17)));
    }
    for (unsigned i = 0; i < 16; i += 1)
    {
        indexes.i32[i] = i & 1 ? 0 : -(int32_t)(4 * (i + 1));
        values.u32[i] = 0x12340000u + i;
    }
    // Odd addresses lie in the inaccessible page; even lanes use negative indices.
    got.value = _mm512_mask_i32gather_epi32(src.value, 0x5555, indexes.value, end, 1);
    for (unsigned i = 0; i < 16; i += 1)
    {
        uint32_t expected = src.u32[i];
        if ((i & 1) == 0)
        {
            memcpy(&expected, end - 4 * (i + 1), 4);
        }
        assert(got.u32[i] == expected);
    }
    _mm512_mask_i32scatter_epi32(end, 0x5555, indexes.value, values.value, 1);
    for (unsigned i = 0; i < 16; i += 2)
    {
        uint32_t actual;
        memcpy(&actual, end - 4 * (i + 1), 4);
        assert(actual == values.u32[i]);
    }
    // Hardware gather also permits an absolute address as its index with a NULL base.
    uint64_t absolute_value = UINT64_C(0x8877665544332211);
    HostV512 absolute_indices = {0};
    absolute_indices.u64[0] = (uintptr_t)&absolute_value;
    got.value = _mm512_mask_i64gather_epi64(src.value, 1, absolute_indices.value, NULL, 1);
    assert(got.u64[0] == absolute_value);

    // A zero mask must tolerate an entirely inaccessible address.
    got.value = _mm512_mask_loadu_epi64(src.value, 0, NULL);
    assert(memcmp(&got, &src, 64) == 0);
    got.value = _mm512_mask_i64gather_epi64(src.value, 0, indexes.value, NULL, 8);
    assert(memcmp(&got, &src, 64) == 0);
    _mm512_mask_i64scatter_epi64(NULL, 0, indexes.value, values.value, 8);
    _mm512_mask_compressstoreu_epi8(NULL, 0, values.value);
    _mm512_mask_storeu_epi32(NULL, 0, values.value);
    assert(munmap(p, (size_t)size * 3) == 0);
}

// The inaccessible page begins at end. Each active prefix ends exactly there.
#define BUSTER_TEST_PREFIX(vector_type, member, lanes, bytes, mask_type, load, store)                                                      \
    do                                                                                                                                     \
    {                                                                                                                                      \
        for (unsigned count = 0; count <= (lanes); count += 1)                                                                             \
        {                                                                                                                                  \
            vector_type input = {0}, loaded;                                                                                               \
            uint8_t* tail = end - count * (bytes);                                                                                         \
            uint64_t mask = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;                                                         \
            for (unsigned i = 0; i < (lanes); i += 1)                                                                                      \
            {                                                                                                                              \
                input.member[i] = i + 1;                                                                                                   \
            }                                                                                                                              \
            store(tail, (mask_type)mask, input.value);                                                                                     \
            loaded.value = load((mask_type)mask, tail);                                                                                    \
            for (unsigned i = 0; i < (lanes); i += 1)                                                                                      \
            {                                                                                                                              \
                assert(loaded.member[i] == (i < count ? input.member[i] : 0));                                                             \
            }                                                                                                                              \
            loaded.value = load((mask_type)0, NULL);                                                                                       \
            for (unsigned i = 0; i < (lanes); i += 1)                                                                                      \
            {                                                                                                                              \
                assert(loaded.member[i] == 0);                                                                                             \
            }                                                                                                                              \
            store(NULL, (mask_type)0, input.value);                                                                                        \
        }                                                                                                                                  \
    } while (0)

static void test_prefix_tails(void)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size > 128);
    uint8_t* mapping = mmap(NULL, (size_t)page_size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    assert(mprotect(mapping, (size_t)page_size, PROT_READ | PROT_WRITE) == 0);
    uint8_t* end = mapping + page_size;
    HostV256 zero256 = {.value = _mm256_maskz_loadu_epi8(0, NULL)};
    HostV128 zero128 = {.value = _mm_maskz_loadu_epi32(0, NULL)};
    for (unsigned i = 0; i < 32; i += 1)
    {
        assert(zero256.u8[i] == 0);
    }
    for (unsigned i = 0; i < 4; i += 1)
    {
        assert(zero128.u32[i] == 0);
    }
    BUSTER_TEST_PREFIX(HostV128, u8, 16, 1, __mmask16, _mm_maskz_loadu_epi8, _mm_mask_storeu_epi8);
    BUSTER_TEST_PREFIX(HostV128, u16, 8, 2, __mmask8, _mm_maskz_loadu_epi16, _mm_mask_storeu_epi16);
    for (unsigned count = 0; count <= 32; count += 1)
    {
        uint8_t* tail = end - count;
        uint32_t mask = count == 32 ? UINT32_MAX : (UINT32_C(1) << count) - 1;
        for (unsigned i = 0; i < count; i += 1)
        {
            tail[i] = (uint8_t)(i + 1);
        }
        HostV256 loaded = {.value = _mm256_maskz_loadu_epi8(mask, tail)};
        for (unsigned i = 0; i < 32; i += 1)
        {
            assert(loaded.u8[i] == (i < count ? i + 1 : 0));
        }
    }
    for (unsigned count = 0; count <= 4; count += 1)
    {
        uint8_t* tail = end - count * 4;
        __mmask8 mask = (__mmask8)((1u << count) - 1);
        for (unsigned i = 0; i < count; i += 1)
        {
            uint32_t value = i + 1;
            memcpy(tail + i * 4, &value, 4);
        }
        HostV128 loaded = {.value = _mm_maskz_loadu_epi32(mask, tail)};
        for (unsigned i = 0; i < 4; i += 1)
        {
            assert(loaded.u32[i] == (i < count ? i + 1 : 0));
        }
    }
    BUSTER_TEST_PREFIX(HostV256, u32, 8, 4, __mmask8, _mm256_maskz_loadu_epi32, _mm256_mask_storeu_epi32);
    for (unsigned count = 0; count <= 4; count += 1)
    {
        uint8_t* tail = end - count * 8;
        __mmask8 mask = (__mmask8)((1u << count) - 1);
        HostV256 input = {0};
        for (unsigned i = 0; i < 4; i += 1)
        {
            input.u64[i] = i + 1;
        }
        _mm256_mask_storeu_epi64(tail, mask, input.value);
        for (unsigned i = 0; i < count; i += 1)
        {
            uint64_t actual;
            memcpy(&actual, tail + i * 8, 8);
            assert(actual == input.u64[i]);
        }
        _mm256_mask_storeu_epi64(NULL, 0, input.value);
    }
    BUSTER_TEST_PREFIX(HostV512, u8, 64, 1, __mmask64, _mm512_maskz_loadu_epi8, _mm512_mask_storeu_epi8);
    BUSTER_TEST_PREFIX(HostV512, u16, 32, 2, __mmask32, _mm512_maskz_loadu_epi16, _mm512_mask_storeu_epi16);
    BUSTER_TEST_PREFIX(HostV512, u32, 16, 4, __mmask16, _mm512_maskz_loadu_epi32, _mm512_mask_storeu_epi32);
    BUSTER_TEST_PREFIX(HostV512, u64, 8, 8, __mmask8, _mm512_maskz_loadu_epi64, _mm512_mask_storeu_epi64);
    assert(munmap(mapping, (size_t)page_size * 2) == 0);
}

static void test_i64_address_wrap(void)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size > 128);
    uint8_t* mapping = mmap(NULL, (size_t)page_size * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    assert(mprotect(mapping + page_size, (size_t)page_size, PROT_READ | PROT_WRITE) == 0);
    uint8_t* base = mapping + page_size + 64;
    HostV512 indexes = {0}, values = {0}, source = {0}, result;
    indexes.i64[0] = INT64_MAX;
    for (unsigned i = 1; i < 8; i += 1)
    {
        indexes.i64[i] = (int64_t)(page_size / 2);
        source.u64[i] = UINT64_C(0xfeed0000) + i;
    }
    values.u64[0] = UINT64_C(0x0123456789abcdef);
    memcpy(base - 2, &values.u64[0], 8);
    result.value = _mm512_mask_i64gather_epi64(source.value, 1, indexes.value, base, 2);
    assert(result.u64[0] == values.u64[0]);
    for (unsigned i = 1; i < 8; i += 1)
    {
        assert(result.u64[i] == source.u64[i]);
    }
    values.u64[0] = UINT64_C(0xfedcba9876543210);
    _mm512_mask_i64scatter_epi64(base, 1, indexes.value, values.value, 2);
    uint64_t actual;
    memcpy(&actual, base - 2, 8);
    assert(actual == values.u64[0]);
    HostV256 narrow = {0};
    narrow.u32[0] = 0x76543210u;
    _mm512_mask_i64scatter_epi32(base, 1, indexes.value, narrow.value, 2);
    uint32_t actual32;
    memcpy(&actual32, base - 2, 4);
    assert(actual32 == narrow.u32[0]);
    assert(munmap(mapping, (size_t)page_size * 3) == 0);
}

static void test_arithmetic(void)
{
    assert(_bzhi_u64(UINT64_MAX, 64) == UINT64_MAX);
    assert(_bzhi_u64(UINT64_MAX, 0) == 0);
    assert(_bzhi_u64(UINT64_MAX, 256) == 0);
    assert(_pdep_u64(0xdu, 0x55u) == 0x51u);
    assert(_pext_u64(0x51u, 0x55u) == 0xdu);
    assert(_tzcnt_u64(0) == 64);
    assert(_blsmsk_u64(0) == UINT64_MAX);
    HostV256 a = {0}, r;
    for (unsigned i = 0; i < 16; i += 1)
    {
        a.u16[i] = UINT16_MAX;
    }
    r.value = _mm256_slli_epi16(a.value, 15);
    for (unsigned i = 0; i < 16; i += 1)
    {
        assert(r.u16[i] == 0x8000);
    }
    HostV512 wide = {0}, wide_result;
    for (unsigned i = 0; i < 32; i += 1)
    {
        wide.u16[i] = UINT16_MAX;
    }
    wide_result.value = _mm512_slli_epi16(wide.value, 15);
    for (unsigned i = 0; i < 32; i += 1)
    {
        assert(wide_result.u16[i] == 0x8000);
    }
}

static void test_aes(void)
{
    const uint8_t input[16] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0};
    const uint8_t key[16] = {0xd6, 0xaa, 0x74, 0xfd, 0xd2, 0xaf, 0x72, 0xfa, 0xda, 0xa6, 0x78, 0xf1, 0xd6, 0xab, 0x76, 0xfe};
    const uint8_t expected[16] = {0x89, 0xd8, 0x10, 0xe8, 0x85, 0x5a, 0xce, 0x68, 0x2d, 0x18, 0x43, 0xd8, 0xcb, 0x12, 0x8f, 0xe4};
    __m128i a, k, result;
    memcpy(&a, input, 16);
    memcpy(&k, key, 16);
    result = _mm_aesenc_si128(a, k);
    assert(memcmp(&result, expected, 16) == 0);
}

int main(void)
{
    test_memory();
    test_prefix_tails();
    test_i64_address_wrap();
    test_arithmetic();
    test_aes();
    puts("host fallback edge cases passed");
    return 0;
}
