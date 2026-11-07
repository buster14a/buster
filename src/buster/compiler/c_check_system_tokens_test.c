// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The system-header bitmap's scalar and vector kernels against independently stated level classes (#1610).
// Protected ends cover every small tail, clamped sources and the gather's three readable padding bytes. A large
// column also covers long conversions; the existing repository self-check is the original MSan
// failing consumer, since a small test caller alone need not cross LLVM's callback-instrumentation threshold.

static __attribute__((noinline)) void check_test_system_tokens(CheckTest* test)
{
    enum
    {
        SOURCES,
        LEVELS,
        VECTOR,
        SCALAR,
        COLUMNS,
        CAPACITY = (1u << 21) + 15,
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t source_bytes = ((uint64_t)CAPACITY * sizeof(uint32_t) + page - 1) / page * page;
    const uint64_t token_bytes = ((uint64_t)CAPACITY + 2 + page - 1) / page * page;
    const uint64_t bytes[COLUMNS] = { source_bytes, page, token_bytes, token_bytes };
    uint8_t* mappings[COLUMNS];
    bool ready = true;
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        mappings[i] = mmap(NULL, bytes[i] + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[i] != MAP_FAILED && mprotect(mappings[i] + bytes[i], page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "guarded system-token columns' allocation");
    static const uint8_t level_values[] =
    {
        BUSTER_PP_SYSTEM_FILE,
        BUSTER_PP_USER_FILE,
        BUSTER_PP_EXTERN_C_FILE,
        0xfe,
        BUSTER_PP_USER_FILE,
        BUSTER_PP_SYSTEM_FILE,
        0xfd,
        BUSTER_PP_EXTERN_C_FILE,
    };
    static const bool expected_levels[] = { true, false, true, false, false, true, false, true };
    static const uint32_t limits[] = { 0, 1, 67 };
    static const uint32_t extra_counts[] = { 127, 128, 129, 255, 256, 257, 1023, 1024, 1025, CAPACITY };
    uint32_t differences = 0;
    uint32_t cases = 0;
    for (uint32_t l = 0; ready && l < BUSTER_ARRAY_LENGTH(limits); l += 1)
    {
        const uint32_t limit = limits[l];
        uint8_t* levels = mappings[LEVELS] + page - limit - 4;
        for (uint32_t i = 0; i < limit; i += 1)
        {
            levels[i] = level_values[i % BUSTER_ARRAY_LENGTH(level_values)];
        }
        levels[limit] = BUSTER_PP_USER_FILE;
        // A four-byte gather ending at the sentinel may read these bytes, but they must not change its low byte.
        levels[limit + 1] = BUSTER_PP_SYSTEM_FILE;
        levels[limit + 2] = BUSTER_PP_EXTERN_C_FILE;
        levels[limit + 3] = 0xff;
        const uint32_t source_values[] = { 0, 1, limit != 0 ? limit - 1 : 0, limit, limit + 1, UINT32_MAX, limit / 2, 2, 17, 66 };
        for (uint32_t round = 0; round < 66 + BUSTER_ARRAY_LENGTH(extra_counts); round += 1)
        {
            const uint32_t count = round < 66 ? round : extra_counts[round - 66];
            uint32_t* sources = (uint32_t*)(mappings[SOURCES] + source_bytes - (uint64_t)count * sizeof(uint32_t));
            uint8_t* vector = mappings[VECTOR] + token_bytes - count - 1;
            uint8_t* scalar = mappings[SCALAR] + token_bytes - count - 1;
            for (uint32_t i = 0; i < count; i += 1)
            {
                sources[i] = source_values[(i * 7 + i / 16) % BUSTER_ARRAY_LENGTH(source_values)];
            }
            vector[-1] = 0x5a;
            scalar[-1] = 0x5a;
            vector[count] = 0xa5;
            scalar[count] = 0xa5;
            memset(vector, 0xcc, count);
            memset(scalar, 0xcc, count);
            check_input_system_token_map(vector, sources, count, levels, limit, false);
            check_input_system_token_map(scalar, sources, count, levels, limit, true);
            bool equal = vector[-1] == 0x5a && scalar[-1] == 0x5a && vector[count] == 0xa5 && scalar[count] == 0xa5;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t source = sources[i];
                const uint8_t expected = source < limit && expected_levels[source % BUSTER_ARRAY_LENGTH(expected_levels)];
                equal &= vector[i] == expected && scalar[i] == expected;
            }
            differences += !equal;
            cases += 1;
            if (!equal)
            {
                fprintf(stderr, "system-token bitmap differs: count %u, output limit %u\n", count, limit);
            }
        }
    }
    check_test_expect(test, differences == 0, "system-token bitmaps match the level classes with guarded tails and clamped sources");
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], bytes[i] + page);
        }
    }
    printf("system-token bitmap: %u cases, %u differences (guarded tails, clamped sources and large columns)\n", cases, differences);
}
