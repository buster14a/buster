// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by c_preprocessor_test.c. Original prefix and full-printer boundary fixtures for #649.

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
