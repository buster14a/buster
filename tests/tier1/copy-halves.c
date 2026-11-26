// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2818: copies of locals built by eight-byte stores, odd tails, unaligned destinations, cold sources and volatile
// copies. The loops keep the local in memory. A separate byte formula checks every byte and both destination guards.

#include <stdio.h>

#define COPY_SIZES(X) \
    X(33) \
    X(39) \
    X(40) \
    X(47) \
    X(48) \
    X(49) \
    X(63) \
    X(64) \
    X(65) \
    X(95) \
    X(127) \
    X(128) \
    X(129) \
    X(191) \
    X(255) \
    X(256)

#define DEFINE_COPY(N) \
    struct __attribute__((packed)) Copy##N \
    { \
        unsigned long words[(N) / 8]; \
        unsigned char tail[(N) % 8]; \
    }; \
    _Static_assert(sizeof(struct Copy##N) == (N), "copy has no padding"); \
    __attribute__((noinline)) void copy_local_##N(struct Copy##N* output, unsigned long seed) \
    { \
        struct Copy##N local; \
        for (unsigned i = 0; i < (N) / 8; i += 1) \
        { \
            local.words[i] = seed + 0x0807060504030201ul * (i + 1); \
        } \
        for (unsigned i = 0; i < (N) % 8; i += 1) \
        { \
            local.tail[i] = seed + i * 13; \
        } \
        *output = local; \
    } \
    __attribute__((noinline)) void copy_cold_##N(struct Copy##N* output, const struct Copy##N* input) \
    { \
        *output = *input; \
    } \
    __attribute__((noinline)) void copy_volatile_##N(struct Copy##N* output, unsigned long seed) \
    { \
        volatile struct Copy##N local; \
        for (unsigned i = 0; i < (N) / 8; i += 1) \
        { \
            local.words[i] = seed + 0x0807060504030201ul * (i + 1); \
        } \
        for (unsigned i = 0; i < (N) % 8; i += 1) \
        { \
            local.tail[i] = seed + i * 13; \
        } \
        *output = local; \
    }

COPY_SIZES(DEFINE_COPY)

// Final dead-store elimination can leave a paired load whose value is unused. Its accesses still need emission,
// using the reserved vector scratch register. An opaque call keeps the source's bytes in its frame object.
__attribute__((noinline)) void copy_unused_64(unsigned long seed)
{
    struct Copy64 local;
    struct Copy64 unused __attribute__((unused));
    copy_local_64(&local, seed);
    unused = local;
}

// An ordinary external definition is an inline candidate whose own code remains emitted from its cached summary.
// The indirect entry exercises that code; consume_cached_64 expands the same summary into a private destination.
void copy_cached_64(struct Copy64* output, unsigned long seed)
{
    struct Copy64 local;
    for (unsigned i = 0; i < 8; i += 1)
    {
        local.words[i] = seed + 0x0807060504030201ul * (i + 1);
    }
    *output = local;
}

void (*volatile copy_cached_entry)(struct Copy64*, unsigned long) = copy_cached_64;

struct CopySeed
{
    unsigned long value;
};

// The own aggregate ABI invokes SROA in the ordinary pipeline, without +sroa disabling cached own-body reuse.
// A scalar cached summary lets it discard the seven unread destination fields after expansion.
__attribute__((noinline)) unsigned long consume_cached_64(struct CopySeed seed)
{
    struct Copy64 local;
    copy_cached_64(&local, seed.value);
    return local.words[3];
}

__attribute__((noinline)) unsigned long overlapping_assignments(unsigned long* p)
{
    p[1] = p[0];
    p[2] = p[1];
    return p[2];
}

static unsigned char expected_byte(unsigned bytes, unsigned i, unsigned long seed)
{
    unsigned char result;
    if (i < bytes / 8 * 8)
    {
        const unsigned long word = seed + 0x0807060504030201ul * (i / 8 + 1);
        result = word >> (i % 8 * 8);
    }
    else
    {
        result = seed + (i - bytes / 8 * 8) * 13;
    }
    return result;
}

#define CHECK_COPY(N) \
    { \
        struct __attribute__((packed)) \
        { \
            unsigned char prefix; \
            struct Copy##N value; \
            unsigned char suffix; \
        } output __attribute__((aligned(16))); \
        struct Copy##N cold; \
        output.prefix = 0xa5; \
        output.suffix = 0x5a; \
        for (unsigned mode = 0; mode < 4; mode += 1) \
        { \
            if (mode == 0) \
            { \
                copy_local_##N(&output.value, seed); \
                cold = output.value; \
            } \
            else if (mode == 1) \
            { \
                copy_cold_##N(&output.value, &cold); \
            } \
            else if (mode == 2) \
            { \
                copy_cold_##N(&output.value, &output.value); \
            } \
            else \
            { \
                copy_volatile_##N(&output.value, seed); \
            } \
            failures += output.prefix != 0xa5 || output.suffix != 0x5a; \
            const unsigned char* bytes = (const unsigned char*)&output.value; \
            for (unsigned i = 0; i < (N); i += 1) \
            { \
                failures += bytes[i] != expected_byte((N), i, seed); \
                checksum = checksum * 31 + bytes[i]; \
            } \
        } \
    }

int main(void)
{
    unsigned failures = 0;
    unsigned long checksum = 0;
    for (unsigned round = 0; round < 5; round += 1)
    {
        const unsigned long seed = 0x123456789abcdef0ul + round * 0x1111111111111111ul;
        COPY_SIZES(CHECK_COPY)
        copy_unused_64(seed);
        struct Copy64 cached;
        copy_cached_entry(&cached, seed);
        const unsigned char* cached_bytes = (const unsigned char*)&cached;
        for (unsigned i = 0; i < sizeof(cached); i += 1)
        {
            failures += cached_bytes[i] != expected_byte(64, i, seed);
            checksum = checksum * 31 + cached_bytes[i];
        }
        const struct CopySeed aggregate_seed = { seed };
        const unsigned long consumed = consume_cached_64(aggregate_seed);
        failures += consumed != seed + 4 * 0x0807060504030201ul;
        checksum = checksum * 31 + consumed;
        unsigned long words[3] = { seed, ~seed, seed ^ 0x55 };
        failures += overlapping_assignments(words) != seed;
        failures += words[0] != seed || words[1] != seed || words[2] != seed;
    }
    printf("copy-halves: %u failures, %016lx\n", failures, checksum);
    return failures != 0;
}
