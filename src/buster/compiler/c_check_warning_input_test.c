// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original range-update oracle: compare every ordered mutation against immediate byte updates, including writes
// after clearing and independent bounds. All four byte columns and the compact index end at protected pages.

static void check_test_warning_ranges(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[5];
    bool ready = true;
    for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(mappings); column += 1)
    {
        mappings[column] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[column] != MAP_FAILED && mprotect(mappings[column] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "warning range guarded allocation");
    uint32_t mismatches = 0;
    static Parser parser;
    for (uint32_t count = 0; ready && count <= 257; count += 1)
    {
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            uint8_t expected[3][257];
            uint8_t* columns[4];
            for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(columns); column += 1)
            {
                columns[column] = mappings[column] + page - count;
                for (uint32_t token = 0; token < count; token += 1)
                {
                    columns[column][token] = column == 3 ? (uint8_t)(check_test_random(test) & BUSTER_CHECK_CONSTANT_BOUND_CONTEXT) : (uint8_t)check_test_random(test);
                    if (column < 3)
                    {
                        expected[column][token] = columns[column][token];
                    }
                }
            }
            parser.options.scalar = scalar != 0;
            parser.tokens.count = count;
            CheckInput input =
            {
                .constant_warnings = columns[0],
                .constant_warning_ignored = columns[1]
            };
            CheckInputReader reader =
            {
                .parser = &parser,
                .output = &input,
                .warning_suppressed = columns[2],
                .constant_contexts = columns[3],
                .warning_index_base = 1,
            };
            while (reader.warning_index_base * BUSTER_CHECK_WARNING_BLOCK < count)
            {
                reader.warning_index_base *= 2;
            }
            reader.warning_index = (uint16_t*)(mappings[4] + page) - reader.warning_index_base * 2;
            for (uint32_t step = 0; step < 128; step += 1)
            {
                uint32_t first = check_test_random(test) % (count + 1);
                uint32_t end = first + check_test_random(test) % (count - first + 1);
                if (step % 4 == 0)
                {
                    first = step / 4 < count ? step / 4 : 0;
                    end = count;
                }
                const uint8_t clear = (uint8_t)check_test_random(test);
                const uint8_t suppress = step % 3 == 0 ? clear : 0;
                check_input_warning_range(&reader, first, end, clear, suppress);
                for (uint32_t token = first; token < end; token += 1)
                {
                    if ((columns[3][token] & BUSTER_CHECK_CONSTANT_BOUND_CONTEXT) == 0)
                    {
                        expected[0][token] &= (uint8_t)~clear;
                        expected[1][token] &= (uint8_t)~clear;
                        expected[2][token] |= suppress;
                    }
                }
                if (count != 0)
                {
                    const uint32_t token = check_test_random(test) % count;
                    const uint8_t warnings = (uint8_t)check_test_random(test);
                    check_input_warning_add(&reader, token, warnings);
                    expected[0][token] |= warnings;
                    // A newly independent bound can only remove pending range work; stale summary bits are safe.
                    if (step % 7 == 0)
                    {
                        columns[3][token] |= BUSTER_CHECK_CONSTANT_BOUND_CONTEXT;
                    }
                }
                for (uint32_t column = 0; column < 3; column += 1)
                {
                    mismatches += memcmp(columns[column], expected[column], count) != 0;
                }
            }
            check_input_warning_range(&reader, count, count + 1, 0xff, 0xff);
            for (uint32_t column = 0; column < 3; column += 1)
            {
                mismatches += memcmp(columns[column], expected[column], count) != 0;
            }
        }
    }
    check_test_expect(test, mismatches == 0, "scalar/vector ordered warning ranges match independent guarded byte oracle");
    for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(mappings); column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], page * 2);
        }
    }
}
