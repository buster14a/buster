// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The vector-only deprecated-key bitmap (#1610) against a per-token scalar oracle, with exact protected column ends.
// Direct calls retain the production host-vector admission. These are semantic/tail controls; a small caller is
// not the large-reader MSan reproducer. The dedicated main.c compile and final audit remain the crash qualification.

static __attribute__((noinline)) void check_test_deprecated_marked(CheckTest* test)
{
    if (BUSTER_VECTOR_PATH(true))
    {
        enum
        {
            KINDS,
            DECLARATIONS,
            OBJECTS,
            MARKS,
            WORDS,
            COLUMNS,
            CAPACITY = (1u << 21) + 15,
        };
        const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
        const uint64_t kind_bytes = ((uint64_t)CAPACITY + page - 1) / page * page;
        const uint64_t table_bytes = ((uint64_t)CAPACITY * sizeof(uint32_t) + page - 1) / page * page;
        const uint64_t word_capacity = ((uint64_t)CAPACITY + 63) / 64;
        const uint64_t word_bytes = ((word_capacity + 2) * sizeof(uint64_t) + page - 1) / page * page;
        const uint64_t bytes[COLUMNS] = { kind_bytes, table_bytes, table_bytes, table_bytes, word_bytes };
        uint8_t* mappings[COLUMNS];
        bool ready = true;
        for (uint32_t i = 0; i < COLUMNS; i += 1)
        {
            mappings[i] = mmap(NULL, bytes[i] + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            ready &= mappings[i] != MAP_FAILED && mprotect(mappings[i] + bytes[i], page, PROT_NONE) == 0;
        }
        check_test_expect(test, ready, "guarded deprecated-mark bitmap columns' allocation");
        static const uint32_t extra_counts[] = { 127, 128, 129, 255, 256, 257, 4095, 4096, 4097, CAPACITY };
        const uint64_t prefix = UINT64_C(0xa55a123456789abc);
        const uint64_t suffix = UINT64_C(0x5aa5fedcba987654);
        uint32_t cases = 0;
        uint32_t differences = 0;
        uint64_t selected = 0;
        for (uint32_t density = 0; ready && density < 3; density += 1)
        {
            // Mode 0 and 1, without objects and with; modes 2 and 3 the same with few keys marked, as a unit's deprecated
            // declarations are, where the marked keys' filter skips most 16 identifiers (#529).
            for (uint32_t object_mode = 0; object_mode < 4; object_mode += 1)
            {
                for (uint32_t round = 0; round < 66 + BUSTER_ARRAY_LENGTH(extra_counts); round += 1)
                {
                    const uint32_t count = round < 66 ? round : extra_counts[round - 66];
                    const uint32_t word_count = (count + 63) / 64;
                    uint8_t* kinds = mappings[KINDS] + kind_bytes - count;
                    uint32_t* declarations = (uint32_t*)(mappings[DECLARATIONS] + table_bytes - (uint64_t)count * sizeof(uint32_t));
                    uint32_t* objects = (uint32_t*)(mappings[OBJECTS] + table_bytes - (uint64_t)count * sizeof(uint32_t));
                    uint32_t* marks = (uint32_t*)(mappings[MARKS] + table_bytes - (uint64_t)count * sizeof(uint32_t));
                    uint64_t* words = (uint64_t*)(mappings[WORDS] + word_bytes - ((uint64_t)word_count + 1) * sizeof(uint64_t));
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        kinds[i] = density == 0 || (density == 1 && i % 3 == 0) ? PARSE_KIND_IDENTIFIER : PARSE_KIND_NUMBER;
                        const uint32_t declared[] = { 0, count - 1, count / 2, count, count + 1, UINT32_MAX, UINT32_C(0x80000000), 1, i * 7 % count };
                        const uint32_t first_objects[] = { 0, 1, count, count + 1, UINT32_MAX, UINT32_C(0x80000001), count / 2 + 1, i * 11 % count + 1 };
                        declarations[i] = declared[i % BUSTER_ARRAY_LENGTH(declared)];
                        objects[i] = first_objects[i % BUSTER_ARRAY_LENGTH(first_objects)];
                        marks[i] = object_mode >= 2 ? (i % 97 == 5 ? i + 1 : 0) :
                                   i % 4 == 0 ? UINT32_C(0x80000000) : i % 4 == 1 ? 0 : i % 4 == 2 ? 1 : UINT32_MAX;
                    }
                    words[-1] = prefix;
                    words[word_count] = suffix;
                    memset(words, 0xcc, (uint64_t)word_count * sizeof(uint64_t));
                    Parser parser = { 0 };
                    parser.tokens.count = count;
                    parser.tokens.kinds = kinds;
                    CheckInput input = { .declarations = declarations };
                    CheckInputReader reader = { .parser = &parser, .output = &input, .objects = object_mode % 2 != 0 ? objects : NULL };
                    check_input_deprecated_marked(&reader, marks, words);
                    bool equal = words[-1] == prefix && words[word_count] == suffix;
                    for (uint32_t word = 0; word < word_count; word += 1)
                    {
                        uint64_t expected = 0;
                        const uint32_t first = word * 64;
                        const uint32_t length = count - first < 64 ? count - first : 64;
                        for (uint32_t lane = 0; lane < length; lane += 1)
                        {
                            const uint32_t token = first + lane;
                            if (kinds[token] == PARSE_KIND_IDENTIFIER && declarations[token] < count)
                            {
                                uint32_t key = declarations[token];
                                if (object_mode % 2 != 0)
                                {
                                    const uint32_t object = objects[key];
                                    if (object != 0 && object - 1 < count)
                                    {
                                        key = object - 1;
                                    }
                                }
                                if (marks[key] != 0)
                                {
                                    expected |= UINT64_C(1) << lane;
                                    selected += 1;
                                }
                            }
                        }
                        equal &= words[word] == expected;
                    }
                    differences += !equal;
                    cases += 1;
                    if (!equal)
                    {
                        fprintf(stderr, "deprecated mark bitmap differs: count %u, density %u, objects %u\n", count, density, object_mode);
                    }
                }
            }
        }
        check_test_expect(test, cases == 912 && differences == 0 && selected != 0,
                          "deprecated mark bitmaps match independent key routing, guarded tails and unused high bits");
        for (uint32_t i = 0; i < COLUMNS; i += 1)
        {
            if (mappings[i] != MAP_FAILED)
            {
                munmap(mappings[i], bytes[i] + page);
            }
        }
        printf("deprecated mark bitmap: %u cases, %u differences (guarded columns, key routing and large input)\n", cases, differences);
    }
    else
    {
        printf("deprecated mark bitmap: scalar host, vector-only kernel not called\n");
    }
}

