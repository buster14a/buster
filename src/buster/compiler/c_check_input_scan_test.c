// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The reader's scans of its token columns (#529), against the test's own reading of the columns: the masks the passes
// that act on few tokens take (check_input_scan_*), and check_input_count_tokens' counts and syntax bits, by both of
// its paths. Every column ends at a PROT_NONE page, for every length to 200, so a load or a store past a column's end
// faults. Included by c_checker_test.c.

// A column of `count` entries of `size` bytes that ends at a PROT_NONE page: `page_end` minus the column.
static void* check_test_scan_column(uint8_t* page_end, uint32_t count, uint32_t size)
{
    return page_end - (uint64_t)count * size;
}

static __attribute__((noinline)) void check_test_input_scans(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    // Five columns, each in two pages, the second PROT_NONE: kinds, roles, names (or 32-bit values), pointers and the
    // syntax bits count_tokens writes.
    enum { COLUMNS = 5 };
    uint8_t* mappings[COLUMNS];
    bool ready = true;
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        mappings[i] = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[i] != MAP_FAILED && mprotect(mappings[i] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "guarded scan columns' allocation");
    // The names count_tokens looks for: two absent ones (0) among them, which count no token, though tokens without a
    // name have 0.
    const uint32_t sought[] = { 7, 9, 0, 12, 0, 15, 21, 22, 30 };
    const uint8_t groups[] = { 0, 0, 1, 1, 2, 2, 3, 4, 4 };
    const uint8_t kind_values[] = { PARSE_KIND_L_SQUARE, PARSE_KIND_ASM, PARSE_KIND_SIZEOF, PARSE_KIND_PERIOD, PARSE_KIND_IDENTIFIER, PARSE_KIND_ATTRIBUTE,
                                    PARSE_KIND_CASE, PARSE_KIND_STATIC_ASSERT, PARSE_KIND_COMMA, PARSE_KIND_ALIGNAS, PARSE_KIND_CALLING_CONVENTION, PARSE_KIND_STRING,
                                    PARSE_KIND_L_SQUARE };
    // The names whose tokens the syntax bits mark, each as its bit.
    const uint32_t syntax_names[] = { C_NAME_BUILTIN_VA_ARG, C_NAME_BUILTIN_CONVERTVECTOR, C_NAME_BUILTIN_BIT_CAST, C_NAME_GENERIC, C_NAME_BUILTIN_CHOOSE_EXPR,
                                      C_NAME_BUILTIN_TYPES_COMPATIBLE_P, C_NAME_BUILTIN_OFFSETOF, C_NAME_NORETURN, C_NAME_NONNULL, C_NAME_NULLABLE,
                                      C_NAME_NULLABLE_RESULT, C_NAME_NULL_UNSPECIFIED, C_NAME_FUNC, C_NAME_FUNCTION, C_NAME_PRETTY_FUNCTION };
    const uint8_t syntax_bits[] = { BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED,
                                    BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_ATTRIBUTE,
                                    BUSTER_CHECK_SYNTAX_ATTRIBUTE, BUSTER_CHECK_SYNTAX_ATTRIBUTE, BUSTER_CHECK_SYNTAX_ATTRIBUTE,
                                    BUSTER_CHECK_SYNTAX_ATTRIBUTE, BUSTER_CHECK_SYNTAX_STRING, BUSTER_CHECK_SYNTAX_STRING, BUSTER_CHECK_SYNTAX_STRING };
    const uint8_t role_values[] = { PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_OPERAND, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_SEPARATOR };
    uint32_t mismatches = 0;
    for (uint32_t count = 0; ready && count <= 200; count += 1)
    {
        uint8_t* kinds = check_test_scan_column(mappings[0] + page, count, 1);
        uint8_t* roles = check_test_scan_column(mappings[1] + page, count, 1);
        uint32_t* names = check_test_scan_column(mappings[2] + page, count, 4);
        void** pointers = check_test_scan_column(mappings[3] + page, count, 8);
        uint8_t* syntax = check_test_scan_column(mappings[4] + page, count + 1, 1);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t draw = check_test_random(test);
            const uint32_t pick = check_test_random(test);
            kinds[i] = (draw & 3) == 0 ? (uint8_t)(draw >> 8) : kind_values[(draw >> 2) % BUSTER_ARRAY_LENGTH(kind_values)];
            roles[i] = (draw & 0x30) == 0 ? (uint8_t)(draw >> 16) : role_values[(draw >> 6) % BUSTER_ARRAY_LENGTH(role_values)];
            names[i] = (pick & 7) == 0 ? syntax_names[(pick >> 3) % BUSTER_ARRAY_LENGTH(syntax_names)] : (draw >> 24) % 34;
            pointers[i] = (draw & 0xc0000) == 0 ? (void*)&pointers : 0;
        }
        // An attribute's `[[` across two blocks of 64 tokens, and a `[` without a successor.
        if (count > 64)
        {
            kinds[63] = PARSE_KIND_L_SQUARE;
            kinds[64] = PARSE_KIND_L_SQUARE;
        }
        if (count % 2 == 1)
        {
            kinds[count - 1] = PARSE_KIND_L_SQUARE;
        }
        // Each scan from each first token of its width: the tokens it gives must be the test's own.
        for (uint32_t first = 0; first < count; first += 64)
        {
            uint64_t equal = 0;
            uint64_t nonzero = 0;
            for (uint32_t i = first; i < count && i < first + 64; i += 1)
            {
                equal |= (uint64_t)(kinds[i] == PARSE_KIND_IDENTIFIER) << (i - first);
                nonzero |= (uint64_t)(roles[i] != 0) << (i - first);
            }
            mismatches += check_input_scan_equal(kinds, first, count, PARSE_KIND_IDENTIFIER) != equal;
            mismatches += check_input_scan_nonzero(roles, first, count) != nonzero;
            mismatches += check_input_scan_all(first, count) != (count - first < 64 ? (UINT64_C(1) << (count - first)) - 1 : UINT64_MAX);
        }
        for (uint32_t first = 0; first < count; first += 16)
        {
            uint32_t below = 0;
            uint32_t present = 0;
            for (uint32_t i = first; i < count && i < first + 16; i += 1)
            {
                below |= (uint32_t)(names[i] < 20) << (i - first);
                present |= (uint32_t)(names[i] != 0) << (i - first);
            }
            mismatches += check_input_scan_below(names, first, count, 20) != below;
            mismatches += check_input_scan_nonzero32(names, first, count) != present;
        }
        for (uint32_t first = 0; first < count; first += 8)
        {
            uint32_t present = 0;
            for (uint32_t i = first; i < count && i < first + 8; i += 1)
            {
                present |= (uint32_t)(pointers[i] != 0) << (i - first);
            }
            mismatches += check_input_scan_pointers((const void* const*)pointers, first, count) != present;
        }
        // count_tokens on a parser of these columns, by both paths, against the test's own counts.
        Parser parser = { 0 };
        parser.tokens.count = count;
        parser.tokens.kinds = kinds;
        parser.tokens.names = names;
        parser.tree.roles = roles;
        CheckInputTokenCounts expected = { 0 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            expected.subscripts += kinds[i] == PARSE_KIND_L_SQUARE;
            expected.asms += kinds[i] == PARSE_KIND_ASM;
            expected.size_queries += kinds[i] == PARSE_KIND_SIZEOF;
            expected.vector_members += kinds[i] == PARSE_KIND_PERIOD && roles[i] == PARSE_ROLE_MEMBER_OPERATOR;
            expected.operands += roles[i] == PARSE_ROLE_OPERAND;
            expected.calls += roles[i] == PARSE_ROLE_CLOSE_CALL || roles[i] == PARSE_ROLE_CLOSE_FUNCTION;
            switch (names[i])
            {
                case 7: case 9: expected.groups[0] += 1; break;
                case 12: expected.groups[1] += 1; break;
                case 15: expected.groups[2] += 1; break;
                case 21: expected.groups[3] += 1; break;
                case 22: case 30: expected.groups[4] += 1; break;
                default: break;
            }
        }
        uint32_t syntax_differences = 0;
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            memset(syntax, 0xa5, (uint64_t)count + 1);
            const CheckInputTokenCounts counts = check_input_count_tokens(&parser, sought, groups, BUSTER_ARRAY_LENGTH(sought), syntax, scalar != 0);
            char description[200];
            snprintf(description, sizeof(description), "token counts of %u tokens by the %s path: subscripts %u (%u), operands %u (%u), group 0 %u (%u)", count,
                     scalar != 0 ? "scalar" : "vector", counts.subscripts, expected.subscripts, counts.operands, expected.operands, counts.groups[0],
                     expected.groups[0]);
            check_test_expect(test, memcmp(&counts, &expected, sizeof(counts)) == 0, description);
            // Each token's syntax bits, as the test reads its kind, its successor's and its name.
            for (uint32_t i = 0; i < count; i += 1)
            {
                uint8_t bits = 0;
                for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(syntax_names); n += 1)
                {
                    bits |= names[i] == syntax_names[n] ? syntax_bits[n] : 0;
                }
                switch (kinds[i])
                {
                    case PARSE_KIND_ATTRIBUTE: case PARSE_KIND_ALIGNAS: case PARSE_KIND_ASM: case PARSE_KIND_CALLING_CONVENTION:
                        bits |= BUSTER_CHECK_SYNTAX_ATTRIBUTE;
                        break;
                    case PARSE_KIND_L_SQUARE:
                        bits |= i + 1 < count && kinds[i + 1] == PARSE_KIND_L_SQUARE ? BUSTER_CHECK_SYNTAX_ATTRIBUTE : 0;
                        break;
                    case PARSE_KIND_STRING:
                        bits |= BUSTER_CHECK_SYNTAX_STRING;
                        break;
                    default:
                        break;
                }
                syntax_differences += syntax[i] != bits;
            }
            syntax_differences += syntax[count] != 0;
        }
        mismatches += syntax_differences;
    }
    char description[120];
    snprintf(description, sizeof(description), "the reader's token scans: %u masks differ from the test's", mismatches);
    check_test_expect(test, ready && mismatches == 0, description);
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], 2 * page);
        }
    }
}
