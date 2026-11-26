// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2755: Buster builds the kernels alone, so their pass counts cannot include a harness loop. Clang builds the same
// kernels as the oracle and the shared harness, which puts zero and partial trips against protected pages and calls
// the copying kernels on overlapping arrays. BUSTER_UNROLL_VECTOR_REDUCTIONS selects the AVX-512 reduction kernels.

#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>

void unroll_vector_copy(uint8_t* destination, const uint8_t* source, size_t count);
void unroll_vector_scale(float* destination, const float* source, size_t count);

#if defined(BUSTER_UNROLL_VECTOR_REDUCTIONS)
uint32_t unroll_vector_sum(const uint32_t* values, size_t count, uint32_t initial);
uint32_t unroll_vector_product(const uint32_t* values, size_t count, uint32_t initial);
uint32_t unroll_vector_and(const uint32_t* values, size_t count, uint32_t initial);
uint32_t unroll_vector_or(const uint32_t* values, size_t count, uint32_t initial);
uint32_t unroll_vector_xor(const uint32_t* values, size_t count, uint32_t initial);
uint32_t unroll_vector_min(const uint32_t* values, size_t count, uint32_t initial);
int32_t unroll_vector_max(const int32_t* values, size_t count, int32_t initial);
#endif

#if defined(BUSTER_UNROLL_VECTOR_KERNELS)

__attribute__((noinline)) void unroll_vector_copy(uint8_t* destination, const uint8_t* source, size_t count)
{
    for (size_t i = 0; i < count; i += 1)
    {
        destination[i] = source[i];
    }
}

__attribute__((noinline)) void unroll_vector_scale(float* destination, const float* source, size_t count)
{
    for (size_t i = 0; i < count; i += 1)
    {
        destination[i] = source[i] * 0.75f;
    }
}

#if defined(BUSTER_UNROLL_VECTOR_REDUCTIONS)
__attribute__((noinline)) uint32_t unroll_vector_sum(const uint32_t* values, size_t count, uint32_t initial)
{
    uint32_t result = initial;
    for (size_t i = 0; i < count; i += 1)
    {
        result += values[i];
    }
    return result;
}

__attribute__((noinline)) uint32_t unroll_vector_product(const uint32_t* values, size_t count, uint32_t initial)
{
    uint32_t result = initial;
    for (size_t i = 0; i < count; i += 1)
    {
        result *= values[i];
    }
    return result;
}

__attribute__((noinline)) uint32_t unroll_vector_and(const uint32_t* values, size_t count, uint32_t initial)
{
    uint32_t result = initial;
    for (size_t i = 0; i < count; i += 1)
    {
        result &= values[i];
    }
    return result;
}

__attribute__((noinline)) uint32_t unroll_vector_or(const uint32_t* values, size_t count, uint32_t initial)
{
    uint32_t result = initial;
    for (size_t i = 0; i < count; i += 1)
    {
        result |= values[i];
    }
    return result;
}

__attribute__((noinline)) uint32_t unroll_vector_xor(const uint32_t* values, size_t count, uint32_t initial)
{
    uint32_t result = initial;
    for (size_t i = 0; i < count; i += 1)
    {
        result ^= values[i];
    }
    return result;
}

__attribute__((noinline)) uint32_t unroll_vector_min(const uint32_t* values, size_t count, uint32_t initial)
{
    uint32_t result = initial;
    for (size_t i = 0; i < count; i += 1)
    {
        const uint32_t value = values[i];
        result = value < result ? value : result;
    }
    return result;
}

__attribute__((noinline)) int32_t unroll_vector_max(const int32_t* values, size_t count, int32_t initial)
{
    int32_t result = initial;
    for (size_t i = 0; i < count; i += 1)
    {
        const int32_t value = values[i];
        result = value > result ? value : result;
    }
    return result;
}
#endif

#else

#include <stdbool.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct UnrollGuarded UnrollGuarded;
struct UnrollGuarded
{
    void* mapping;
    uint8_t* bytes;
    size_t page;
};

static bool unroll_guarded_open(UnrollGuarded* guarded, size_t page)
{
    void* mapping = mmap(NULL, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = mapping != MAP_FAILED;
    if (valid)
    {
        guarded->mapping = mapping;
        guarded->bytes = (uint8_t*)mapping + page;
        guarded->page = page;
        valid = mprotect(guarded->bytes, page, PROT_READ | PROT_WRITE) == 0;
    }
    return valid;
}

static void unroll_guarded_close(UnrollGuarded* guarded)
{
    if (guarded->mapping != NULL)
    {
        munmap(guarded->mapping, guarded->page * 3);
    }
}

static uint64_t unroll_mix(uint64_t digest, uint64_t value)
{
    return (digest ^ value) * UINT64_C(1099511628211);
}

static uint64_t unroll_mix_bytes(uint64_t digest, const uint8_t* bytes, size_t count)
{
    uint64_t result = digest;
    for (size_t i = 0; i < count; i += 1)
    {
        result = unroll_mix(result, bytes[i]);
    }
    return result;
}

static void unroll_fill_bytes(uint8_t* bytes, size_t count, size_t salt)
{
    for (size_t i = 0; i < count; i += 1)
    {
        bytes[i] = (uint8_t)((i * 113 + salt * 37) ^ (i >> 3));
    }
}

static void unroll_fill_floats(float* values, size_t count, size_t salt)
{
    for (size_t i = 0; i < count; i += 1)
    {
        values[i] = (float)((int32_t)((i * 113 + salt * 37) & 1023) - 512) * 0.125f;
    }
}

static uint64_t unroll_run(UnrollGuarded* source, UnrollGuarded* destination, UnrollGuarded* overlap)
{
    // Around the vector widths and eight- and nine-vector boundaries: the conservative unroller leaves at least one
    // original vector iteration for the vectorizer's exit values, before a masked integer or scalar floating tail.
    static const size_t lengths[] = {
        0,
        1,
        2,
        3,
        7,
        8,
        9,
        15,
        16,
        17,
        31,
        32,
        33,
        63,
        64,
        65,
        127,
        128,
        129,
        143,
        144,
        145,
        255,
        256,
        257,
        287,
        288,
        289,
        511,
        512,
        513,
        575,
        576,
        577,
    };
    static const ptrdiff_t byte_offsets[] = {
        -65,
        -64,
        -17,
        -16,
        -1,
        0,
        1,
        16,
        17,
        64,
        65,
    };
    static const ptrdiff_t float_offsets[] = {
        -17,
        -1,
        0,
        1,
        17,
    };
    uint64_t digest = UINT64_C(1469598103934665603);
    const size_t page = source->page;
    for (size_t l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l += 1)
    {
        const size_t count = lengths[l];
        for (uint32_t side = 0; side < 2; side += 1)
        {
            unroll_fill_bytes(source->bytes, page, count);
            unroll_fill_bytes(destination->bytes, page, count + 1);
            const size_t byte_start = side == 0 ? 0 : page - count;
            // At the upper edge, count zero passes pointers into PROT_NONE pages to every kernel.
            unroll_vector_copy(destination->bytes + byte_start, source->bytes + byte_start, count);
            digest = unroll_mix_bytes(digest, destination->bytes, page);

            unroll_fill_floats((float*)source->bytes, page / sizeof(float), count);
            unroll_fill_floats((float*)destination->bytes, page / sizeof(float), count + 1);
            const size_t word_start = side == 0 ? 0 : page - count * sizeof(uint32_t);
            unroll_vector_scale((float*)(destination->bytes + word_start), (const float*)(source->bytes + word_start), count);
            digest = unroll_mix_bytes(digest, destination->bytes, page);

#if defined(BUSTER_UNROLL_VECTOR_REDUCTIONS)
            for (uint32_t negative = 0; negative < 2; negative += 1)
            {
                uint32_t* words = (uint32_t*)(source->bytes + word_start);
                for (size_t i = 0; i < count; i += 1)
                {
                    // Common set and clear bits keep AND/OR from collapsing to their identities. The second run has
                    // only negative signed lanes, which catches a zero identity in the signed maximum.
                    words[i] = UINT32_C(0x4a4a0001) | ((uint32_t)(i * UINT32_C(2654435761)) & UINT32_C(0x8000fffe)) | (negative << 31);
                }
                digest = unroll_mix(digest, unroll_vector_sum(words, count, UINT32_C(0x9e3779b9)));
                digest = unroll_mix(digest, unroll_vector_product(words, count, 3));
                digest = unroll_mix(digest, unroll_vector_and(words, count, UINT32_C(0x7f7f7f7f)));
                digest = unroll_mix(digest, unroll_vector_or(words, count, UINT32_C(0x10101010)));
                digest = unroll_mix(digest, unroll_vector_xor(words, count, UINT32_C(0xdeadbeef)));
                digest = unroll_mix(digest, unroll_vector_min(words, count, UINT32_MAX - 17));
                digest = unroll_mix(digest, (uint32_t)unroll_vector_max((const int32_t*)words, count, INT32_MIN + 17));
            }
#endif
        }
        for (size_t d = 0; d < sizeof(byte_offsets) / sizeof(byte_offsets[0]); d += 1)
        {
            unroll_fill_bytes(overlap->bytes, page, count + d);
            uint8_t* base = overlap->bytes + 128;
            unroll_vector_copy(base + byte_offsets[d], base, count);
            digest = unroll_mix_bytes(digest, overlap->bytes, page);
        }
        for (size_t d = 0; d < sizeof(float_offsets) / sizeof(float_offsets[0]); d += 1)
        {
            unroll_fill_floats((float*)overlap->bytes, page / sizeof(float), count + d);
            float* base = (float*)(overlap->bytes + 256);
            unroll_vector_scale(base + float_offsets[d], base, count);
            digest = unroll_mix_bytes(digest, overlap->bytes, page);
        }
    }
    return digest;
}

int main(void)
{
    const long page = sysconf(_SC_PAGESIZE);
    UnrollGuarded source = { 0 };
    UnrollGuarded destination = { 0 };
    UnrollGuarded overlap = { 0 };
    int result = 1;
    if (page >= 4096 && unroll_guarded_open(&source, (size_t)page) && unroll_guarded_open(&destination, (size_t)page) && unroll_guarded_open(&overlap, (size_t)page))
    {
        printf("%016llx\n", (unsigned long long)unroll_run(&source, &destination, &overlap));
        result = 0;
    }
    unroll_guarded_close(&overlap);
    unroll_guarded_close(&destination);
    unroll_guarded_close(&source);
    return result;
}

#endif
