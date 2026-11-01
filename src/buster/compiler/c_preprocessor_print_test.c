// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by c_preprocessor_test.c. Original prefix, spelling, splice-check and full-printer boundary fixtures for #649.

static bool pp_test_print_offsets(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 4 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = mapping != MAP_FAILED && mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0 &&
                  mprotect(mapping + 2 * page, page, PROT_READ | PROT_WRITE) == 0;
    uint32_t checked = 0;
    for (uint32_t count = 0; result && count <= 64; count += 1)
    {
        uint32_t* lengths = (uint32_t*)(mapping + page) - count;
        uint16_t* offsets = (uint16_t*)(mapping + 3 * page) - count;
        for (uint32_t pattern = 0; result && pattern < 5; pattern += 1)
        {
            const uint64_t gaps = pattern == 0 ? 0 : pattern == 1 ? ~0ull : pattern == 2 ? 0xaaaaaaaaaaaaaaaaull :
                                  pattern == 3 ? 0x8000000100000001ull : 0x75b6d1028ac94e3full;
            uint32_t expected = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                lengths[i] = pattern == 0 ? 1 : pattern == 1 ? 64 : 1 + (i * 17 + count * 13 + pattern * 7) % 64;
                offsets[i] = 0xfeed;
            }
            offsets[-1] = 0x19cd;
            const uint32_t total = pp_print_offsets(lengths, count, gaps, offsets);
            for (uint32_t i = 0; result && i < count; i += 1)
            {
                result = offsets[i] == expected;
                expected += lengths[i] + (uint32_t)((gaps >> i) & 1);
            }
            result = result && total == expected && offsets[-1] == 0x19cd;
            const uint32_t invalid[] =
            {
                0,
                65,
                UINT32_MAX,
            };
            for (uint32_t value = 0; result && count != 0 && value < BUSTER_ARRAY_LENGTH(invalid); value += 1)
            {
                const uint32_t at = count / 2;
                const uint32_t saved = lengths[at];
                lengths[at] = invalid[value];
                result = pp_print_offsets(lengths, count, gaps, offsets) == 0 && offsets[-1] == 0x19cd;
                lengths[at] = saved;
            }
            checked += 1;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 4 * page);
    }
    printf("printer prefixes: %u batches, %s (guarded tails, inactive gaps, 0/64/65/large lengths)\n", checked, result ? "pass" : "FAIL");
    return result;
}

// Every possible alignment of a printed pragma end, followed by ordinary fast batches and exceptional spellings.
// Compare complete outputs from the independent scalar printer in both marker modes, not merely their tokenization.
static bool pp_test_print_batches(const char* directory)
{
    char path[PATH_MAX];
    char resource[PATH_MAX];
    snprintf(path, sizeof(path), "%s/printer-batch.c", directory);
    snprintf(resource, sizeof(resource), "%s/printer-batch.bin", directory);
    bool result = pp_test_write_file(resource, "\001\177\377");
    for (uint32_t alignment = 0; result && alignment <= 64; alignment += 1)
    {
        FILE* source = fopen(path, "w");
        result = source != 0;
        if (source != 0)
        {
            for (uint32_t i = 0; i < alignment; i += 1)
            {
                fputc(';', source);
            }
            fputs("\n#pragma custom token\n", source);
            const uint32_t ordinary = alignment < 64 ? 160 : 70000;
            for (uint32_t i = 0; i < ordinary; i += 1)
            {
                fputs(i % 11 == 0 ? "\nidentifier + . .5 ; " : "identifier + . .5 ; ", source);
            }
            for (uint32_t width = 63; width <= 65; width += 1)
            {
                fputs("\n\"", source);
                for (uint32_t i = 0; i < width - 2; i += 1)
                {
                    fputc('a', source);
                }
                fputs("\"\n", source);
            }
            fputs("spliced\\\n_name \\u03b1 1?" "?/\n2\n_Pragma(\"custom payload\")\n"
                  "#pragma custom \\u03b1\n#line 450 \"printer-batch.c\"\n"
                  "__LINE__\n#embed \"printer-batch.bin\"\nfinal_token\n", source);
            result = fclose(source) == 0;
        }
        InternTable table = { 0 };
        Preprocessor pp = { 0 };
        const PPOptions options =
        {
            .language =
            {
                .standard = 2017,
                .gnu = true,
            },
            .quiet = true,
            .trigraphs = true,
            .defer_history = true,
        };
        result = result && pp_test_run(&pp, &table, &options, path);
        for (uint32_t markers = 0; result && markers < 2; markers += 1)
        {
            size_t vector_length = 0;
            size_t scalar_length = 0;
            char* vector = pp_test_print(&pp, true, markers != 0, &vector_length);
            char* scalar = pp_test_print(&pp, false, markers != 0, &scalar_length);
            result = vector != 0 && scalar != 0 && vector_length == scalar_length && memcmp(vector, scalar, vector_length) == 0;
            free(vector);
            free(scalar);
        }
        pp_release(&pp);
        intern_release(&table);
    }
    unlink(path);
    unlink(resource);
    printf("printer batches: %s (64 pragma alignments, buffer rollover, marker modes, short/long/spliced/UCN spellings and embeds)\n", result ? "pass" : "FAIL");
    return result;
}

// A splitmix64 step.
static uint64_t pp_print_test_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// pp_print_spellings against a reference of its own: a linear search of the ranges for each other token's location
// (not the printer's directory) and the intern table's span for each identifier. A preprocessor of four ranges, two of
// them adjacent, with gaps between the others, whose bytes end at a PROT_NONE page, as the interned records do; the
// chunk's columns end at PROT_NONE pages too, for every count from 0 to 64, and the lanes past the count must keep
// their canaries. Tokens are file runs of one range, mixed ranges in any order, identifiers among them, spellings past
// 64 bytes and ending at their range's end; and failures, which must return false: a location in a gap or past every
// range, an empty spelling, an identifier whose span is empty. Then chunks whose tokens are in 1 to 64 ranges.
static bool pp_test_print_spellings(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t source_pages = 3;
    const uint64_t record_pages = 2;
    uint8_t* columns = mmap(0, 4 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* source = mmap(0, (source_pages + 1) * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* records = mmap(0, (record_pages + 1) * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = columns != MAP_FAILED && source != MAP_FAILED && records != MAP_FAILED && mprotect(columns, page, PROT_READ | PROT_WRITE) == 0 &&
                  mprotect(columns + 2 * page, page, PROT_READ | PROT_WRITE) == 0 && mprotect(source, source_pages * page, PROT_READ | PROT_WRITE) == 0 &&
                  mprotect(records, record_pages * page, PROT_READ | PROT_WRITE) == 0;
    uint64_t state = 649;
    for (uint64_t b = 0; result && b < source_pages * page; b += 1)
    {
        source[b] = (uint8_t)(' ' + pp_print_test_random(&state) % 95);
    }
    for (uint64_t b = 0; result && b < record_pages * page; b += 1)
    {
        records[b] = (uint8_t)('a' + pp_print_test_random(&state) % 26);
    }
    // The ranges' bases and lengths, their bytes from the source's start, the last ending at the guard page.
    const uint32_t lengths_of[4] =
    {
        300,
        5000,
        40,
        (uint32_t)(source_pages * page) - 5340,
    };
    const uint32_t bases[4] =
    {
        0x10,
        0x400,
        0x400 + 5000,
        0x400 + 5040 + 0x777,
    };
    static PPRange ranges[4];
    uint32_t offset = 0;
    for (uint32_t r = 0; r < 4; r += 1)
    {
        ranges[r] = (PPRange){
            .base = bases[r],
            .length = lengths_of[r],
            .file = r,
            .bytes = source + offset,
        };
        offset += lengths_of[r];
    }
    // Spans: 0 is empty; the others of 1 to 80 bytes, the last ending at the records' guard page.
    static InternSpan spans[64];
    uint32_t record_end = (uint32_t)(record_pages * page);
    for (uint32_t n = 63; n >= 1; n -= 1)
    {
        const uint32_t length = n == 63 ? 80 : 1 + (uint32_t)(pp_print_test_random(&state) % 80);
        record_end -= length;
        spans[n] = (InternSpan){
            .offset = record_end,
            .length = length,
        };
    }
    spans[0] = (InternSpan){
        .offset = 0,
        .length = 0,
    };
    static Preprocessor fake;
    fake.ranges = ranges;
    fake.range_count = 4;
    // Every location the tests give is below the top, as every token's is: the directory covers it.
    fake.location_top = bases[3] + lengths_of[3] + 600;
    Arena arena = arena_reserve(1ull << 24);
    const uint32_t* directory = result && arena.base != 0 ? pp_build_directory(&fake, &arena) : 0;
    result = result && directory != 0;
    uint32_t checked = 0;
    uint32_t refused = 0;
    for (uint32_t count = 0; result && count <= 64; count += 1)
    {
        uint32_t* values = (uint32_t*)(columns + page) - count;
        uint32_t* locations = (uint32_t*)(columns + 3 * page) - count;
        for (uint32_t pattern = 0; result && pattern < 24; pattern += 1)
        {
            // Patterns 0-5: one range, ascending, no identifiers to all identifiers; 6-17: mixed ranges in any order;
            // 18-23: one failure each (a gap, past every range, an empty spelling, an empty span), or none.
            const uint32_t one = pattern % 4;
            uint64_t identifiers = 0;
            uint32_t cursor = 0;
            for (uint32_t k = 0; k < count; k += 1)
            {
                const uint64_t random = pp_print_test_random(&state);
                const bool identifier = pattern < 6 ? random % 5 < pattern : (random >> 8) % 3 == 0;
                if (identifier)
                {
                    identifiers |= 1ull << k;
                    values[k] = 1 + (uint32_t)((random >> 16) % 63);
                    locations[k] = bases[random % 4] + (uint32_t)((random >> 24) % 30);
                }
                else
                {
                    const uint32_t r = pattern < 6 ? one : (uint32_t)((random >> 4) % 4);
                    const uint32_t longest = lengths_of[r] < 100 ? lengths_of[r] : 100;
                    const uint32_t length = 1 + (uint32_t)((random >> 32) % longest);
                    // In a run, ascending; elsewhere anywhere in the range, some ending at its end.
                    const uint32_t start = pattern < 6 ? (cursor + length <= lengths_of[r] ? cursor : 0) :
                                           (random >> 40) % 8 == 0 ? lengths_of[r] - length : (uint32_t)((random >> 44) % (lengths_of[r] - length + 1));
                    cursor = start + length + (uint32_t)((random >> 56) % 4);
                    locations[k] = bases[r] + start;
                    values[k] = locations[k] + length;
                }
            }
            // The failures, at a lane the pattern picks among the others (or the identifiers).
            const uint32_t lane = count != 0 ? (uint32_t)(pp_print_test_random(&state) % count) : 0;
            const uint32_t failure = pattern >= 18 && count != 0 ? pattern - 18 : 6;
            if (failure < 4)
            {
                identifiers &= ~(1ull << lane);
                const uint32_t at = failure == 0 ? bases[0] + lengths_of[0] + 5 : failure == 1 ? bases[3] + lengths_of[3] + 3 : bases[1] + 7;
                locations[lane] = at;
                values[lane] = failure == 2 ? at : at + 1;
                if (failure == 3)
                {
                    identifiers |= 1ull << lane;
                    values[lane] = 0;
                }
            }
            const uint64_t valid = _bzhi_u64(~0ull, count);
            // The reference.
            const uint8_t* expected_spellings[64];
            uint32_t expected_lengths[64];
            bool expected = true;
            for (uint32_t k = 0; k < count; k += 1)
            {
                if ((identifiers >> k) & 1)
                {
                    expected_spellings[k] = records + spans[values[k]].offset;
                    expected_lengths[k] = spans[values[k]].length;
                }
                else
                {
                    uint32_t r = 4;
                    for (uint32_t q = 0; q < 4; q += 1)
                    {
                        r = locations[k] >= bases[q] && locations[k] < bases[q] + lengths_of[q] ? q : r;
                    }
                    expected_spellings[k] = r < 4 ? source + (ranges[r].bytes - source) + (locations[k] - bases[r]) : 0;
                    expected_lengths[k] = values[k] - locations[k];
                    expected = expected && r < 4;
                }
                expected = expected && expected_lengths[k] != 0;
            }
            const uint8_t* spellings[64];
            uint32_t lengths[64];
            uint8_t firsts[64];
            uint8_t lasts[64];
            for (uint32_t k = 0; k < 64; k += 1)
            {
                spellings[k] = (const uint8_t*)"canary";
                lengths[k] = 0xc0ffee;
                firsts[k] = 0xa5;
                lasts[k] = 0x5a;
            }
            // Identifier bits past the count must be ignored.
            const uint64_t stray = count < 64 ? ~valid & pp_print_test_random(&state) : 0;
            const bool got = pp_print_spellings(&fake, directory, values, locations, identifiers | stray, valid, records, spans, spellings, lengths, firsts, lasts);
            result = got == expected;
            for (uint32_t k = 0; result && expected && k < count; k += 1)
            {
                result = spellings[k] == expected_spellings[k] && lengths[k] == expected_lengths[k] && firsts[k] == expected_spellings[k][0] &&
                         lasts[k] == expected_spellings[k][expected_lengths[k] - 1];
            }
            for (uint32_t k = count; result && k < 64; k += 1)
            {
                result = strcmp((const char*)spellings[k], "canary") == 0 && lengths[k] == 0xc0ffee && firsts[k] == 0xa5 && lasts[k] == 0x5a;
            }
            if (!result)
            {
                printf("printer spellings: count %u, pattern %u: %s (expected %s)\n", count, pattern, got ? "taken" : "refused", expected ? "taken" : "refused");
            }
            checked += 1;
            refused += !expected;
        }
    }
    // Then a preprocessor of 64 ranges, and chunks of 64 tokens whose other tokens are in 1 to 64 of them, each used,
    // a third of the rest identifiers: taken with the reference's spellings up to BUSTER_PP_PRINT_RANGES ranges, and
    // refused past them, for the general loop.
    static PPRange many[64];
    for (uint32_t r = 0; r < 64; r += 1)
    {
        many[r] = (PPRange){
            .base = 0x10 + r * 200,
            .length = 150,
            .file = r,
            .bytes = source + r * 150,
        };
    }
    static Preprocessor wide;
    wide.ranges = many;
    wide.range_count = 64;
    wide.location_top = 0x10 + 64 * 200 + 1;
    const uint32_t* spread_directory = result ? pp_build_directory(&wide, &arena) : 0;
    result = result && spread_directory != 0;
    uint32_t spreads = 0;
    for (uint32_t spread = 1; result && spread <= 64; spread += 1)
    {
        uint32_t* values = (uint32_t*)(columns + page) - 64;
        uint32_t* locations = (uint32_t*)(columns + 3 * page) - 64;
        const uint8_t* expected_spellings[64];
        uint32_t expected_lengths[64];
        uint64_t identifiers = 0;
        for (uint32_t k = 0; k < 64; k += 1)
        {
            const uint64_t random = pp_print_test_random(&state);
            const bool identifier = k >= spread && random % 3 == 0;
            const uint32_t r = k < spread ? k : (uint32_t)((random >> 8) % spread);
            const uint32_t start = (uint32_t)((random >> 16) % 140);
            const uint32_t length = 1 + (uint32_t)((random >> 32) % 10);
            identifiers |= (uint64_t)identifier << k;
            values[k] = identifier ? 1 + (uint32_t)((random >> 40) % 63) : many[r].base + start + length;
            locations[k] = many[r].base + start;
            expected_spellings[k] = identifier ? records + spans[values[k]].offset : many[r].bytes + start;
            expected_lengths[k] = identifier ? spans[values[k]].length : length;
        }
        const uint8_t* spellings[64];
        uint32_t lengths[64];
        uint8_t firsts[64];
        uint8_t lasts[64];
        const bool expected = spread <= BUSTER_PP_PRINT_RANGES;
        const bool got = pp_print_spellings(&wide, spread_directory, values, locations, identifiers, ~0ull, records, spans, spellings, lengths, firsts, lasts);
        result = got == expected;
        for (uint32_t k = 0; result && expected && k < 64; k += 1)
        {
            result = spellings[k] == expected_spellings[k] && lengths[k] == expected_lengths[k] && firsts[k] == expected_spellings[k][0] &&
                     lasts[k] == expected_spellings[k][expected_lengths[k] - 1];
        }
        if (!result)
        {
            printf("printer spellings: %u ranges: %s (expected %s)\n", spread, got ? "taken" : "refused", expected ? "taken" : "refused");
        }
        spreads += 1;
    }
    arena_release(&arena);
    munmap(columns, 4 * page);
    munmap(source, (source_pages + 1) * page);
    munmap(records, (record_pages + 1) * page);
    printf("printer spellings: %u chunks, %u refused, and chunks in 1 to %u of 64 ranges, %s (guarded columns, bytes and records; runs, mixed ranges, gaps, "
           "empty spellings, the bound of %u ranges)\n", checked, refused, spreads, result ? "pass" : "FAIL", BUSTER_PP_PRINT_RANGES);
    return result;
}

// pp_print_unspliced against its rule, and the rule against the splices themselves. Random spellings of 1 to 200 bytes
// of letters, backslashes, every blank, '?', '/', '=' and quotes, each ending at a PROT_NONE page, with and without
// trigraphs, in groups of 1 to 64 of which a random subset is checked: the answer must be that no checked spelling has
// a backslash before a blank, at its end or in the last lane of one of its 64-byte blocks, nor with trigraphs a '?';
// and every spelling the check passes must be what pp_remove_splices leaves as it is, for each kind it treats apart.
// Then splices placed at every position from 60 to 132 of spellings of 64 to 200 bytes, which must all be refused.
static bool pp_test_print_unspliced(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 128 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = mapping != MAP_FAILED;
    for (uint32_t n = 0; result && n < 64; n += 1)
    {
        result = mprotect(mapping + 2 * n * page, page, PROT_READ | PROT_WRITE) == 0;
    }
    static const uint8_t alphabet[] = "aaaaaaaaaaaaaaaaa\\\\\\\\ \t\n\r\f\v?" "?/=\"'n0";
    const uint8_t kinds[] =
    {
        C_TOKEN_NUMERIC_CONSTANT,
        C_TOKEN_STRING_LITERAL,
        C_TOKEN_CHAR_CONSTANT,
        C_TOKEN_UNKNOWN,
    };
    uint8_t buffer[256];
    uint64_t state = 6490;
    uint32_t groups = 0;
    uint32_t passed = 0;
    uint32_t spurious = 0;
    for (uint32_t trial = 0; result && trial < 4000; trial += 1)
    {
        const bool trigraphs = (trial & 1) != 0;
        const uint32_t count = 1 + (uint32_t)(pp_print_test_random(&state) % 64);
        const uint8_t* spellings[64];
        uint32_t lengths[64];
        bool possible[64];
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint64_t random = pp_print_test_random(&state);
            const uint32_t length = 1 + (uint32_t)(random % (random % 4 == 0 ? 200 : 70));
            uint8_t* spelling = mapping + 2 * k * page + page - length;
            for (uint32_t b = 0; b < length; b += 1)
            {
                spelling[b] = alphabet[pp_print_test_random(&state) % (sizeof(alphabet) - 1)];
            }
            spellings[k] = spelling;
            lengths[k] = length;
            possible[k] = false;
            for (uint32_t b = 0; b < length; b += 1)
            {
                possible[k] = possible[k] || (spelling[b] == '\\' && (b + 1 == length || spelling[b + 1] <= ' ' || b % 64 == 63)) || (trigraphs && spelling[b] == '?');
            }
        }
        const uint64_t others = _bzhi_u64(pp_print_test_random(&state) | 1, count);
        bool expected = true;
        for (uint32_t k = 0; k < count; k += 1)
        {
            expected = expected && !(((others >> k) & 1) && possible[k]);
        }
        result = pp_print_unspliced(spellings, lengths, others, trigraphs) == expected;
        for (uint32_t k = 0; result && k < count; k += 1)
        {
            const bool alone = pp_print_unspliced(spellings, lengths, 1ull << k, trigraphs);
            result = alone == !possible[k];
            for (uint32_t kind = 0; result && kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
            {
                const uint8_t* spelled = 0;
                const uint32_t length = pp_remove_splices(spellings[k], lengths[k], trigraphs ? C_TOKENIZER_TRIGRAPHS : 0, kinds[kind], buffer, &spelled);
                const bool same = length == lengths[k] && memcmp(spelled, spellings[k], length) == 0;
                result = !alone || same;
                spurious += kind == 0 && !alone && same;
            }
            passed += alone;
        }
        if (!result)
        {
            printf("printer splice check: trial %u failed\n", trial);
        }
        groups += 1;
    }
    // Splices at block edges, as written (a backslash and a line break, through blanks too) and, with trigraphs, through
    // the trigraph that spells a backslash.
    static const char* splices[] =
    {
        "\\\n",
        "\\\r\n",
        "\\ \t\n",
        "?" "?/\n",
    };
    for (uint32_t length = 64; result && length <= 200; length += 17)
    {
        for (uint32_t at = 60; result && at <= 132 && at + 4 <= length; at += 1)
        {
            for (uint32_t form = 0; result && form < BUSTER_ARRAY_LENGTH(splices); form += 1)
            {
                uint8_t* spelling = mapping + page - length;
                memset(spelling, 'x', length);
                memcpy(spelling + at, splices[form], strlen(splices[form]));
                const uint8_t* spellings[1] =
                {
                    spelling,
                };
                const uint32_t lengths[1] =
                {
                    length,
                };
                result = !pp_print_unspliced(spellings, lengths, 1, form == 3);
            }
        }
    }
    munmap(mapping, 128 * page);
    printf("printer splice check: %u groups, %u spellings passed, %u refused that splice nothing, %s (guarded tails, blocks past 64 bytes, trigraphs)\n", groups,
           passed, spurious, result ? "pass" : "FAIL");
    return result;
}

// Both printers' bytes, with markers and without, with trigraphs and without, for 65 alignments of a source whose
// chunks take every path of the vector printer's spellings: tokens of two headers' macros between the file's own (a
// chunk's several ranges), escapes in literals (no splice: the fast path), a backslash before a blank or at a token's
// end and stray backslashes (the general loop), splices in strings, numbers and punctuators, and in spellings around
// 64 and 128 bytes, at their block edges; identifiers past 64 bytes, spliced ones, stringizing, and trigraphs.
static bool pp_test_print_spelling_paths(const char* directory)
{
    char path[PATH_MAX];
    char first[PATH_MAX];
    char second[PATH_MAX];
    snprintf(path, sizeof(path), "%s/printer-spellings.c", directory);
    snprintf(first, sizeof(first), "%s/printer-spellings-a.h", directory);
    snprintf(second, sizeof(second), "%s/printer-spellings-b.h", directory);
    bool result = pp_test_write_file(first, "#define A1 alpha + 1\n#define A2 (a2 * \"x\\ny\")\n#define STR(x) #x\n#define CAT(a, b) a ## b\n") &&
                  pp_test_write_file(second, "#define B1 beta - 2 .5\n#define B2 { b2 , '\\'' }\n#define B3 A1 B1 A2\n");
    uint32_t runs = 0;
    for (uint32_t alignment = 0; result && alignment <= 64; alignment += 1)
    {
        FILE* source = fopen(path, "w");
        result = source != 0;
        if (source != 0)
        {
            fputs("#include \"printer-spellings-a.h\"\n#include \"printer-spellings-b.h\"\n", source);
            for (uint32_t i = 0; i < alignment; i += 1)
            {
                fputc(';', source);
            }
            for (uint32_t i = 0; i < 40; i += 1)
            {
                fputs(i % 7 == 0 ? "\nA1 x B1 y A2 B2 z B3 w\n" : "A1 x B1 y A2 B2 z B3 w ", source);
            }
            fputs("\n\"plain\\n\\t\\\"escapes\\\\\" '\\'' '\\\\' \"\\x41\\101\" L\"wide\\n\" u8\"\\u00e9\"\n", source);
            fputs("\"backslash \\ blank\" a \\ b \\\nc \"end\\\\\" \"spliced \\\n string\" +\\\n= 12\\\n34 .\\\n5\n", source);
            for (uint32_t length = 60; length <= 136; length += 1)
            {
                // A string of `length` bytes, then the same with a splice at its middle, at 63 and at 64.
                fputc('"', source);
                for (uint32_t b = 2; b < length; b += 1)
                {
                    fputc('a' + (int)(b % 26), source);
                }
                fputs("\" ", source);
                const uint32_t places[3] =
                {
                    length / 2,
                    63,
                    64,
                };
                for (uint32_t p = 0; p < 3; p += 1)
                {
                    fputc('"', source);
                    for (uint32_t b = 1; b + 1 < length; b += 1)
                    {
                        fputs(b == places[p] ? "\\\n" : "", source);
                        fputc('a' + (int)(b % 26), source);
                    }
                    fputs("\" ", source);
                }
                fputc('\n', source);
            }
            fputs("an_identifier_of_more_than_sixty_four_bytes_which_the_batch_leaves_to_step_three_copies_x + y\n", source);
            fputs("spl\\\niced CAT(an_identifier_of_more_than_sixty_four_bytes_, which_a_paste_makes_and_the_printer_spells_here) STR(a \"\\n\" \\ b '\\'')\n", source);
            fputs("x ?" "?= y ?" "?! z \"what?" "?!\" ?" "?/\nw ? q\n", source);
            for (uint32_t i = 0; i < 70; i += 1)
            {
                fputs("A1 \"esc\\n\" B1 'c' A2 ", source);
            }
            fputs("\nfinal_token\n", source);
            result = fclose(source) == 0;
        }
        for (uint32_t trigraphs = 0; result && trigraphs < 2; trigraphs += 1)
        {
            InternTable table = { 0 };
            Preprocessor pp = { 0 };
            const PPOptions options =
            {
                .language =
                {
                    .standard = 2017,
                    .gnu = true,
                },
                .quiet = true,
                .trigraphs = trigraphs != 0,
                .defer_history = true,
            };
            result = pp_test_run(&pp, &table, &options, path);
            for (uint32_t markers = 0; result && markers < 2; markers += 1)
            {
                size_t vector_length = 0;
                size_t scalar_length = 0;
                char* vector = pp_test_print(&pp, true, markers != 0, &vector_length);
                char* scalar = pp_test_print(&pp, false, markers != 0, &scalar_length);
                result = vector != 0 && scalar != 0 && vector_length == scalar_length && memcmp(vector, scalar, vector_length) == 0;
                if (!result)
                {
                    printf("printer spelling paths: alignment %u, trigraphs %u, markers %u differ\n", alignment, trigraphs, markers);
                }
                free(vector);
                free(scalar);
                runs += 1;
            }
            pp_release(&pp);
            intern_release(&table);
        }
    }
    unlink(path);
    unlink(first);
    unlink(second);
    printf("printer spelling paths: %u prints, %s (several ranges a chunk, escapes, splices at 64-byte edges, long identifiers, trigraphs on and off)\n", runs,
           result ? "pass" : "FAIL");
    return result;
}
