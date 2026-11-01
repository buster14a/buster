// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The reader's scans of its token columns (#529), against the test's own reading of the columns: the masks the passes
// that act on few tokens take (check_input_scan_*), check_input_count_tokens' counts, syntax bits and the walk in source
// order's marks, and check_input_expression_classes' classes and first values, each by both of its paths. Every column
// ends at a PROT_NONE page, for every length to 200, so a load or a store past a column's end faults. Included by
// c_checker_test.c.

// A column of `count` entries of `size` bytes that ends at a PROT_NONE page: `page_end` minus the column.
static void* check_test_scan_column(uint8_t* page_end, uint32_t count, uint32_t size)
{
    return page_end - (uint64_t)count * size;
}

// Whether the walk in source order may act at token `i` of these columns by its syntax (`syntax`, its syntax bits), as
// the test reads its tests: the twin of check_input_visit_of, for the marks of both of check_input_count_tokens' paths.
static bool check_test_visit_mark(const uint8_t* kinds, const uint8_t* roles, uint32_t count, uint32_t i, uint8_t syntax)
{
    static const uint8_t resolve_roles[] = { PARSE_ROLE_SPECIFIER, PARSE_ROLE_OPEN_BLOCK, PARSE_ROLE_OPEN_BODY, PARSE_ROLE_OPEN_FOR, PARSE_ROLE_OPEN_CONDITION,
                                             PARSE_ROLE_RETURN, PARSE_ROLE_OPEN_CAST, PARSE_ROLE_OPEN_SIZEOF, PARSE_ROLE_OPEN_COMPOUND, PARSE_ROLE_LABEL,
                                             PARSE_ROLE_OPEN_CASE };
    static const uint8_t resolve_kinds[] = { PARSE_KIND_STRUCT, PARSE_KIND_ENUM, PARSE_KIND_TYPEOF, PARSE_KIND_ATOMIC, PARSE_KIND_ASM, PARSE_KIND_GOTO,
                                             PARSE_KIND_SWITCH };
    bool mark = syntax != 0;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(resolve_roles); r += 1)
    {
        mark |= roles[i] == resolve_roles[r];
    }
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(resolve_kinds); k += 1)
    {
        mark |= kinds[i] == resolve_kinds[k];
    }
    mark |= kinds[i] == PARSE_KIND_IDENTIFIER && roles[i] != PARSE_ROLE_MEMBER && roles[i] != PARSE_ROLE_TAG && roles[i] != PARSE_ROLE_LEAF_RIGHT;
    if (i + 1 < count)
    {
        mark |= roles[i] == PARSE_ROLE_OPEN_GROUP && roles[i + 1] == PARSE_ROLE_OPEN_BLOCK;
        mark |= kinds[i] == PARSE_KIND_EXTENSION && (roles[i + 1] == PARSE_ROLE_SPECIFIER || kinds[i + 1] == PARSE_KIND_STRUCT || kinds[i + 1] == PARSE_KIND_ENUM);
    }
    return mark;
}

// The expressions' walk's classes of token `i` of the reader's columns (BUSTER_CHECK_EXPRESSION_*), as the test reads its
// tests: the twin of check_input_expression_class, for both of check_input_expression_classes' paths.
static uint8_t check_test_expression_class(const CheckInputReader* reader, uint32_t i)
{
    // The roles and kinds the walk acts at, in the order its tests take them, and an ignored token's list roles.
    static const uint8_t roles[] = { PARSE_ROLE_OPERAND, PARSE_ROLE_RETURN, PARSE_ROLE_PREFIX, PARSE_ROLE_MULTIPLICATIVE, PARSE_ROLE_ADDITIVE, PARSE_ROLE_SHIFT,
                                     PARSE_ROLE_RELATIONAL, PARSE_ROLE_EQUALITY, PARSE_ROLE_BITWISE_AND, PARSE_ROLE_BITWISE_XOR, PARSE_ROLE_BITWISE_OR,
                                     PARSE_ROLE_LOGICAL_AND, PARSE_ROLE_LOGICAL_OR, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR,
                                     PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CAST, PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_OPEN_TERNARY, PARSE_ROLE_ASSOCIATION,
                                     PARSE_ROLE_CLOSE_TERNARY, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_POSTFIX,
                                     PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_CLOSE_KEYWORD, PARSE_ROLE_CLOSE_CONDITION,
                                     PARSE_ROLE_TERMINATOR, PARSE_ROLE_CLOSE_FOR, PARSE_ROLE_CLOSE_INITIALIZER, PARSE_ROLE_CLOSE_COMPOUND_BODY,
                                     PARSE_ROLE_RANGE, PARSE_ROLE_CLOSE_CASE, PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_CLOSE_ENUM, PARSE_ROLE_CLOSE_SPECIFIER,
                                     PARSE_ROLE_CLOSE_ASM, PARSE_ROLE_LABEL, PARSE_ROLE_TAG, PARSE_ROLE_BITFIELD };
    static const uint8_t kinds[] = { PARSE_KIND_STRING, PARSE_KIND_GOTO, PARSE_KIND_BREAK, PARSE_KIND_CONTINUE, PARSE_KIND_LABEL, PARSE_KIND_SWITCH,
                                     PARSE_KIND_CASE, PARSE_KIND_DEFAULT, PARSE_KIND_STRUCT, PARSE_KIND_ENUM, PARSE_KIND_ASM, PARSE_KIND_ATTRIBUTE,
                                     PARSE_KIND_ALIGNAS, PARSE_KIND_ATOMIC, PARSE_KIND_CALLING_CONVENTION, PARSE_KIND_TYPEOF, PARSE_KIND_GENERIC,
                                     PARSE_KIND_TYPE_ARGUMENT, PARSE_KIND_REAL_IMAG, PARSE_KIND_EXTENSION };
    static const uint8_t lists[] = { PARSE_ROLE_SPECIFIER, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR, PARSE_ROLE_CLOSE_ATTRIBUTE, PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER,
                                     PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT, PARSE_ROLE_CLOSE_SPECIFIER };
    const uint8_t role = reader->parser->tree.roles[i];
    const uint8_t kind = reader->parser->tokens.kinds[i];
    const bool sparse = reader->output->expression_regions[i] != 0 || reader->constant_present[i] != 0 || reader->selected_tokens[i] < reader->parser->tokens.count ||
                        reader->vararg_calls[i] != 0 || reader->designator_tokens[i] != 0 || reader->vla_ends[i] != 0 || check_input_asm_id(reader->output, i) != 0;
    bool taken = false;
    for (uint32_t r = 0; reader->ignored[i] == 0 && r < BUSTER_ARRAY_LENGTH(roles); r += 1)
    {
        taken |= role == roles[r];
    }
    for (uint32_t k = 0; reader->ignored[i] == 0 && k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
    {
        taken |= kind == kinds[k];
    }
    for (uint32_t r = 0; reader->ignored[i] != 0 && r < BUSTER_ARRAY_LENGTH(lists); r += 1)
    {
        taken |= role == lists[r];
    }
    const bool acts = reader->first_child[i] != BUSTER_CHECK_NONE || sparse || taken;
    return (uint8_t)((acts ? BUSTER_CHECK_EXPRESSION_ACTS : 0) | (sparse ? BUSTER_CHECK_EXPRESSION_SPARSE : 0));
}

static CheckInput check_test_output_none_columns(uint32_t* const* columns, uint32_t count)
{
    return (CheckInput)
    {
        .declarations = columns[0],
        .initializer_nodes = columns[1],
        .initializer_begins = columns[2],
        .initializer_ends = columns[3],
        .token_nodes = columns[4],
        .body_declarations = columns[5],
        .declaration_bodies = columns[6],
        .parameter_next = columns[7],
        .control_owners = columns[8],
        .label_targets = columns[9],
        .case_ranges = columns[10],
        .string_ids = columns[11],
        .parameters = columns[12],
        .evaluation_nodes = columns[13],
        .token_count = count,
    };
}

static void check_test_output_none(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[14];
    bool ready = true;
    for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(mappings); column += 1)
    {
        mappings[column] = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[column] != MAP_FAILED && mprotect(mappings[column] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "guarded retained NONE output columns allocate");
    for (uint32_t count = 0; ready && count <= 200; count += 1)
    {
        uint32_t* columns[BUSTER_ARRAY_LENGTH(mappings)];
        for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(columns); column += 1)
        {
            memset(mappings[column], 0xa5, page);
            columns[column] = (uint32_t*)(mappings[column] + page - ((uint64_t)count + 1) * sizeof(uint32_t));
        }
        CheckInput input = check_test_output_none_columns(columns, count);
        check_input_output_none(&input, count);
        bool exact = true;
        for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(columns); column += 1)
        {
            const uint64_t prefix = page - ((uint64_t)count + 1) * sizeof(uint32_t);
            for (uint64_t at = 0; at < prefix; at += 1)
            {
                exact &= mappings[column][at] == 0xa5;
            }
            for (uint32_t token = 0; token <= count; token += 1)
            {
                exact &= columns[column][token] == UINT32_MAX;
            }
        }
        char description[120];
        snprintf(description, sizeof(description), "retained NONE outputs: %u dirty tokens plus sentinel match scalar bits within guard bounds", count);
        check_test_expect(test, exact, description);
    }
    for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(mappings); column += 1)
    {
        if (mappings[column] != MAP_FAILED)
        {
            munmap(mappings[column], 2 * page);
        }
    }
    Arena owned = arena_reserve(UINT64_C(1) << 17);
    check_test_expect(test, owned.base != NULL, "borrowed dirty NONE output arena allocates");
    static const uint32_t counts[] = { 0, 1, 257 };
    for (uint32_t at = 0; owned.base != NULL && at < BUSTER_ARRAY_LENGTH(counts); at += 1)
    {
        assert(arena_commit(&owned, owned.capacity));
        memset(owned.base, 0x63, owned.capacity);
        owned.length = 37;
        Arena borrowed = owned;
        uint32_t* columns[14];
        bool exact = true;
        for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(columns); column += 1)
        {
            columns[column] = BUSTER_ARENA_PUSH_ARRAY(&borrowed, uint32_t, (uint64_t)counts[at] + 1);
            exact &= columns[column] != NULL;
        }
        uint32_t asm_id = UINT32_C(0x91919191);
        CheckInput input = check_test_output_none_columns(columns, counts[at]);
        input.asm_ids = &asm_id;
        if (exact)
        {
            check_input_output_none(&input, counts[at]);
            for (uint32_t column = 0; column < BUSTER_ARRAY_LENGTH(columns); column += 1)
            {
                for (uint32_t token = 0; token <= counts[at]; token += 1)
                {
                    exact &= columns[column][token] == BUSTER_CHECK_NONE;
                }
            }
        }
        for (uint32_t byte = 0; byte < owned.length; byte += 1)
        {
            exact &= owned.base[byte] == 0x63;
        }
        check_test_expect(test, exact && owned.length == 37 && asm_id == UINT32_C(0x91919191),
                          "dirty columns borrowed through an arena copy retain the owner's prefix, sentinel and excluded zero-default column");
    }
    arena_release(&owned);
}

static __attribute__((noinline)) void check_test_input_scans(CheckTest* test)
{
    check_test_output_none(test);
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    // The columns, each in two pages, the second PROT_NONE: kinds, roles, names (or 32-bit values), pointers, and the
    // syntax bits and the marks count_tokens writes; then the expressions' classes' inputs (ignored tokens, first children
    // and the sparse columns) and the first values they write, with the classes in the marks' column.
    enum
    {
        KINDS,
        ROLES,
        NAMES,
        POINTERS,
        SYNTAX,
        VISITS,
        IGNORED,
        FIRST_CHILD,
        REGIONS,
        CONSTANTS,
        SELECTIONS,
        VARARGS,
        DESIGNATORS,
        BOUNDS,
        STATEMENTS,
        FIRST_VALUE,
        COLUMNS,
    };
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
                                    PARSE_KIND_L_SQUARE, PARSE_KIND_STRUCT, PARSE_KIND_ENUM, PARSE_KIND_TYPEOF, PARSE_KIND_EXTENSION, PARSE_KIND_IDENTIFIER,
                                    PARSE_KIND_GOTO, PARSE_KIND_SWITCH };
    // The names whose tokens the syntax bits mark, each as its bit.
    const uint32_t syntax_names[] = { C_NAME_BUILTIN_VA_ARG, C_NAME_BUILTIN_CONVERTVECTOR, C_NAME_BUILTIN_BIT_CAST, C_NAME_GENERIC, C_NAME_BUILTIN_CHOOSE_EXPR,
                                      C_NAME_BUILTIN_TYPES_COMPATIBLE_P, C_NAME_BUILTIN_OFFSETOF, C_NAME_NORETURN, C_NAME_NONNULL, C_NAME_NULLABLE,
                                      C_NAME_NULLABLE_RESULT, C_NAME_NULL_UNSPECIFIED, C_NAME_FUNC, C_NAME_FUNCTION, C_NAME_PRETTY_FUNCTION };
    const uint8_t syntax_bits[] = { BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED,
                                    BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_NAMED, BUSTER_CHECK_SYNTAX_ATTRIBUTE,
                                    BUSTER_CHECK_SYNTAX_ATTRIBUTE, BUSTER_CHECK_SYNTAX_ATTRIBUTE, BUSTER_CHECK_SYNTAX_ATTRIBUTE,
                                    BUSTER_CHECK_SYNTAX_ATTRIBUTE, BUSTER_CHECK_SYNTAX_STRING, BUSTER_CHECK_SYNTAX_STRING, BUSTER_CHECK_SYNTAX_STRING };
    const uint8_t role_values[] = { PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_OPERAND, PARSE_ROLE_CLOSE_CALL, PARSE_ROLE_CLOSE_FUNCTION, PARSE_ROLE_SEPARATOR,
                                    PARSE_ROLE_OPEN_GROUP, PARSE_ROLE_OPEN_BLOCK, PARSE_ROLE_SPECIFIER, PARSE_ROLE_MEMBER, PARSE_ROLE_TAG,
                                    PARSE_ROLE_LEAF_RIGHT, PARSE_ROLE_OPEN_BODY, PARSE_ROLE_RETURN, PARSE_ROLE_LABEL, PARSE_ROLE_CLOSE_ATTRIBUTE,
                                    PARSE_ROLE_TERMINATOR, PARSE_ROLE_OPEN_ASM };
    uint32_t mismatches = 0;
    uint32_t mark_differences = 0;
    uint32_t class_differences = 0;
    for (uint32_t count = 0; ready && count <= 200; count += 1)
    {
        uint8_t* kinds = check_test_scan_column(mappings[KINDS] + page, count, 1);
        uint8_t* roles = check_test_scan_column(mappings[ROLES] + page, count, 1);
        uint32_t* names = check_test_scan_column(mappings[NAMES] + page, count, 4);
        void** pointers = check_test_scan_column(mappings[POINTERS] + page, count, 8);
        uint8_t* syntax = check_test_scan_column(mappings[SYNTAX] + page, count + 1, 1);
        uint8_t* visits = check_test_scan_column(mappings[VISITS] + page, count + 1, 1);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t draw = check_test_random(test);
            const uint32_t pick = check_test_random(test);
            // Kinds and roles below 128, as the parser's are.
            kinds[i] = (draw & 3) == 0 ? (uint8_t)((draw >> 8) % PARSE_KIND_COUNT) : kind_values[(draw >> 2) % BUSTER_ARRAY_LENGTH(kind_values)];
            roles[i] = (draw & 0x30) == 0 ? (uint8_t)((draw >> 16) % PARSE_ROLE_COUNT) : role_values[(draw >> 6) % BUSTER_ARRAY_LENGTH(role_values)];
            names[i] = (pick & 7) == 0 ? syntax_names[(pick >> 3) % BUSTER_ARRAY_LENGTH(syntax_names)] : (draw >> 24) % 34;
            pointers[i] = (draw & 0xc0000) == 0 ? (void*)&pointers : 0;
        }
        // An attribute's `[[` across two blocks of 64 tokens, and a `[` without a successor; a group of a block and an
        // __extension__ before a struct across them too.
        if (count > 64)
        {
            kinds[63] = PARSE_KIND_L_SQUARE;
            kinds[64] = PARSE_KIND_L_SQUARE;
        }
        if (count > 128)
        {
            roles[127] = PARSE_ROLE_OPEN_GROUP;
            roles[128] = PARSE_ROLE_OPEN_BLOCK;
        }
        if (count > 192)
        {
            kinds[191] = PARSE_KIND_EXTENSION;
            kinds[192] = PARSE_KIND_STRUCT;
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
        // The walk's next-mark scan (check_input_scan_nonzero_from, check_input_resolve_next): from the 64-token line that
        // holds each token, with the tokens below it and from the stop on not read, against the next nonzero role.
        for (uint32_t first = 0; first < count; first += 1)
        {
            const uint32_t line = first & ~63u;
            uint64_t nonzero = 0;
            for (uint32_t i = first; i < count && i < line + 64; i += 1)
            {
                nonzero |= (uint64_t)(roles[i] != 0) << (i - line);
            }
            mismatches += check_input_scan_nonzero_from(roles, line, first, count) != nonzero;
        }
        {
            Parser walked = { 0 };
            CheckInputReader reader = { .parser = &walked, .visits = roles };
            const uint32_t limits[] = { 0, count / 3, count / 2 + 1, count, UINT32_MAX };
            for (uint32_t l = 0; count != 0 && l < BUSTER_ARRAY_LENGTH(limits); l += 1)
            {
                reader.resolve_limit = limits[l];
                const uint32_t ends[] = { count - 1, (count - 1) / 2 };
                for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(ends); e += 1)
                {
                    for (uint32_t token = 0; token <= ends[e]; token += 1)
                    {
                        const uint32_t stop = reader.resolve_limit > token && reader.resolve_limit < ends[e] ? reader.resolve_limit : ends[e];
                        uint32_t expected_next = token + 1;
                        while (expected_next < stop && roles[expected_next] == 0)
                        {
                            expected_next += 1;
                        }
                        expected_next = expected_next < stop ? expected_next : token + 1 < stop ? stop : token + 1;
                        mismatches += check_input_resolve_next(&reader, token, ends[e]) != expected_next;
                    }
                }
            }
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
            expected.asm_groups += roles[i] == PARSE_ROLE_OPEN_ASM;
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
            memset(visits, 0xa5, (uint64_t)count + 1);
            const CheckInputTokenCounts counts = check_input_count_tokens(&parser, sought, groups, BUSTER_ARRAY_LENGTH(sought), syntax, visits, scalar != 0);
            char description[200];
            snprintf(description, sizeof(description), "token counts of %u tokens by the %s path: subscripts %u (%u), operands %u (%u), group 0 %u (%u)", count,
                     scalar != 0 ? "scalar" : "vector", counts.subscripts, expected.subscripts, counts.operands, expected.operands, counts.groups[0],
                     expected.groups[0]);
            check_test_expect(test, memcmp(&counts, &expected, sizeof(counts)) == 0, description);
            // Each token's syntax bits, as the test reads its kind, its successor's and its name, and its mark.
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
                mark_differences += visits[i] != check_test_visit_mark(kinds, roles, count, i, bits);
            }
            syntax_differences += syntax[count] != 0;
            mark_differences += visits[count] != 0;
        }
        mismatches += syntax_differences;
        // The expressions' classes of a reader of these columns and sparse ones, by both paths: each token's classes must
        // be the test's own reading, and its first value NONE where the walk doesn't act at it and as it was elsewhere.
        CheckInput input = { 0 };
        input.expression_regions = check_test_scan_column(mappings[REGIONS] + page, count, 4);
        input.asm_ids = check_test_scan_column(mappings[STATEMENTS] + page, count, 4);
        CheckInputReader reader = { 0 };
        reader.parser = &parser;
        reader.output = &input;
        reader.visits = visits;
        reader.ignored = check_test_scan_column(mappings[IGNORED] + page, count, 1);
        uint32_t* first_child = check_test_scan_column(mappings[FIRST_CHILD] + page, count, 4);
        reader.first_child = first_child;
        reader.constant_present = check_test_scan_column(mappings[CONSTANTS] + page, count, 4);
        reader.selected_tokens = check_test_scan_column(mappings[SELECTIONS] + page, count, 4);
        reader.vararg_calls = check_test_scan_column(mappings[VARARGS] + page, count, 4);
        reader.designator_tokens = check_test_scan_column(mappings[DESIGNATORS] + page, count, 4);
        reader.vla_ends = check_test_scan_column(mappings[BOUNDS] + page, count, 4);
        reader.first_value = check_test_scan_column(mappings[FIRST_VALUE] + page, count, 4);
        uint32_t* sparse_columns[] = { input.expression_regions, input.asm_ids, reader.constant_present, reader.vararg_calls, reader.designator_tokens, reader.vla_ends };
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t draw = check_test_random(test);
            reader.ignored[i] = (draw & 3) == 0;
            first_child[i] = (draw & 0x1c) == 0 ? draw >> 8 : BUSTER_CHECK_NONE;
            // A runtime selection (below the count), a proof's (the high bit) or none.
            const uint32_t selection = (draw >> 5) & 7;
            reader.selected_tokens[i] = selection == 0 ? draw % (count + 1) : selection == 1 ? CHECK_INPUT_SOURCE_SELECTION | i : BUSTER_CHECK_NONE;
            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(sparse_columns); c += 1)
            {
                sparse_columns[c][i] = ((draw >> (8 + 4 * c)) & 15) == 0 ? 1 + (draw >> 28) : 0;
            }
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            parser.options.scalar = scalar != 0;
            memset(visits, 0xa5, (uint64_t)count + 1);
            memset(reader.first_value, 0x5a, (uint64_t)count * sizeof(uint32_t));
            const uint32_t acting = check_input_expression_classes(&reader);
            uint32_t expected_acting = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint8_t classes = check_test_expression_class(&reader, i);
                const uint32_t first_value = (classes & BUSTER_CHECK_EXPRESSION_ACTS) == 0 ? BUSTER_CHECK_NONE : UINT32_C(0x5a5a5a5a);
                class_differences += visits[i] != classes || reader.first_value[i] != first_value;
                class_differences += check_input_expression_class(&reader, i) != classes;
                expected_acting += (classes & BUSTER_CHECK_EXPRESSION_ACTS) != 0;
            }
            class_differences += visits[count] != 0xa5 || acting != expected_acting;
        }
        // The empty representation must agree with an independently read dense zero column, including vector
        // tails at the guard page. No address may be formed from the null asm column, even under a zero mask.
        uint32_t* dense_asms = input.asm_ids;
        uint8_t expected_empty[201];
        memset(dense_asms, 0, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t i = 0; i < count; i += 1)
        {
            expected_empty[i] = check_test_expression_class(&reader, i);
        }
        input.asm_ids = NULL;
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            parser.options.scalar = scalar != 0;
            memset(visits, 0xa5, (uint64_t)count + 1);
            memset(reader.first_value, 0x5a, (uint64_t)count * sizeof(uint32_t));
            const uint32_t acting = check_input_expression_classes(&reader);
            uint32_t expected_acting = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint8_t classes = expected_empty[i];
                const uint32_t first_value = (classes & BUSTER_CHECK_EXPRESSION_ACTS) == 0 ? BUSTER_CHECK_NONE : UINT32_C(0x5a5a5a5a);
                class_differences += visits[i] != classes || reader.first_value[i] != first_value;
                class_differences += check_input_expression_class(&reader, i) != classes || check_input_asm_id(&input, i) != 0;
                expected_acting += (classes & BUSTER_CHECK_EXPRESSION_ACTS) != 0;
            }
            class_differences += visits[count] != 0xa5 || acting != expected_acting || check_input_asm_id(&input, count) != 0;
        }
        input.asm_ids = dense_asms;
        parser.options.scalar = false;
    }
    mismatches += mark_differences + class_differences;
    char description[200];
    snprintf(description, sizeof(description), "the reader's token scans: %u masks differ from the test's (%u marks, %u classes)", mismatches, mark_differences,
             class_differences);
    check_test_expect(test, ready && mismatches == 0, description);
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], 2 * page);
        }
    }
}
