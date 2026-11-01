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
    // The class (CheckInputExpressionCode): the walk's visit for a sparse entry or a type query's finish; an ignored
    // token's list or none; then by the roles each class takes, an operand's by its kind, and a role no test takes a DROP
    // but for the kinds above, which the walk refuses.
    static const uint8_t classed_roles[] = { PARSE_ROLE_PREFIX, PARSE_ROLE_MULTIPLICATIVE, PARSE_ROLE_ADDITIVE, PARSE_ROLE_SHIFT, PARSE_ROLE_RELATIONAL,
                                             PARSE_ROLE_EQUALITY, PARSE_ROLE_BITWISE_AND, PARSE_ROLE_BITWISE_XOR, PARSE_ROLE_BITWISE_OR, PARSE_ROLE_LOGICAL_AND,
                                             PARSE_ROLE_LOGICAL_OR, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA, PARSE_ROLE_SEPARATOR, PARSE_ROLE_CLOSE_GROUP,
                                             PARSE_ROLE_CLOSE_CAST, PARSE_ROLE_OPEN_TERNARY, PARSE_ROLE_ASSOCIATION, PARSE_ROLE_CLOSE_INITIALIZER,
                                             PARSE_ROLE_CLOSE_TERNARY, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_POSTFIX,
                                             PARSE_ROLE_CLOSE_CONDITION, PARSE_ROLE_TERMINATOR, PARSE_ROLE_CLOSE_FOR, PARSE_ROLE_CLOSE_CALL,
                                             PARSE_ROLE_CLOSE_FUNCTION };
    static const uint8_t classed_codes[] = { CHECK_INPUT_CODE_PREFIX, CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY,
                                             CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY,
                                             CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_BINARY,
                                             CHECK_INPUT_CODE_BINARY, CHECK_INPUT_CODE_SEPARATOR, CHECK_INPUT_CODE_GROUP, CHECK_INPUT_CODE_CAST,
                                             CHECK_INPUT_CODE_LIST, CHECK_INPUT_CODE_LIST, CHECK_INPUT_CODE_LIST, CHECK_INPUT_CODE_TERNARY,
                                             CHECK_INPUT_CODE_SUBSCRIPT, CHECK_INPUT_CODE_MEMBER, CHECK_INPUT_CODE_POSTFIX, CHECK_INPUT_CODE_CONDITION,
                                             CHECK_INPUT_CODE_TERMINATOR, CHECK_INPUT_CODE_TERMINATOR, CHECK_INPUT_CODE_CALL, CHECK_INPUT_CODE_CALL };
    // The walk's visit: the roles its other tests take, but none of the classes.
    static const uint8_t visited_roles[] = { PARSE_ROLE_RETURN, PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_CLOSE_KEYWORD, PARSE_ROLE_CLOSE_COMPOUND_BODY,
                                             PARSE_ROLE_RANGE, PARSE_ROLE_CLOSE_CASE, PARSE_ROLE_CLOSE_STRUCT, PARSE_ROLE_CLOSE_ENUM, PARSE_ROLE_CLOSE_SPECIFIER,
                                             PARSE_ROLE_CLOSE_ASM, PARSE_ROLE_LABEL, PARSE_ROLE_TAG, PARSE_ROLE_BITFIELD };
    bool listed = false;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(lists); r += 1)
    {
        listed |= role == lists[r];
    }
    bool refused = false;
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
    {
        refused |= kind == kinds[k];
    }
    uint8_t code = refused ? CHECK_INPUT_CODE_SCALAR : CHECK_INPUT_CODE_DROP;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(classed_roles); r += 1)
    {
        code = role == classed_roles[r] ? classed_codes[r] : code;
    }
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(visited_roles); r += 1)
    {
        code = role == visited_roles[r] ? CHECK_INPUT_CODE_SCALAR : code;
    }
    if (role == PARSE_ROLE_OPERAND)
    {
        code = kind == PARSE_KIND_IDENTIFIER ? CHECK_INPUT_CODE_IDENTIFIER :
               kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER ? CHECK_INPUT_CODE_LITERAL : CHECK_INPUT_CODE_SCALAR;
    }
    if (reader->ignored[i] != 0)
    {
        code = listed ? CHECK_INPUT_CODE_LIST : CHECK_INPUT_CODE_DROP;
    }
    if (sparse || reader->specifier_finishes[i] != NULL)
    {
        code = CHECK_INPUT_CODE_SCALAR;
    }
    return (uint8_t)(acts ? BUSTER_CHECK_EXPRESSION_ACTS | (sparse ? BUSTER_CHECK_EXPRESSION_SPARSE : 0) | code << BUSTER_CHECK_EXPRESSION_CODE_SHIFT : 0);
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
        FINISHES,
        POSTORDER,
        PADDED_VISITS,
        COMPACT_TOKENS,
        COMPACT_CLASSES,
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
                                    PARSE_KIND_GOTO, PARSE_KIND_SWITCH, PARSE_KIND_NUMBER, PARSE_KIND_CHARACTER, PARSE_KIND_PLUS, PARSE_KIND_ARROW };
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
                                    PARSE_ROLE_TERMINATOR, PARSE_ROLE_OPEN_ASM, PARSE_ROLE_PREFIX, PARSE_ROLE_ADDITIVE, PARSE_ROLE_ASSIGN, PARSE_ROLE_COMMA,
                                    PARSE_ROLE_CLOSE_GROUP, PARSE_ROLE_CLOSE_CAST, PARSE_ROLE_OPEN_TERNARY, PARSE_ROLE_CLOSE_INITIALIZER,
                                    PARSE_ROLE_CLOSE_TERNARY, PARSE_ROLE_CLOSE_SUBSCRIPT, PARSE_ROLE_POSTFIX, PARSE_ROLE_CLOSE_SIZEOF, PARSE_ROLE_BITFIELD };
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
        reader.specifier_finishes = check_test_scan_column(mappings[FINISHES] + page, count, sizeof(CheckInputSpecifierJob*));
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
            // A type query that finishes at one token in eight.
            reader.specifier_finishes[i] = (check_test_random(test) & 7) == 0 ? (CheckInputSpecifierJob*)&reader : NULL;
        }
        // The sparse marks the columns' writers keep (CheckInputReader.sparse_marks, #628), by the test's own reading, which
        // the vector path's classes read in place of the columns.
        uint64_t marks[(200 + 64) / 64] = { 0 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            marks[i >> 6] |= (uint64_t)((check_test_expression_class(&reader, i) & BUSTER_CHECK_EXPRESSION_SPARSE) != 0) << (i & 63);
        }
        reader.sparse_marks = marks;
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            parser.options.scalar = scalar != 0;
            memset(visits, 0xa5, (uint64_t)count + 1);
            memset(reader.first_value, 0x5a, (uint64_t)count * sizeof(uint32_t));
            input.visit_misses = 0;
            const uint32_t acting = check_input_expression_classes(&reader);
            uint32_t expected_acting = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint8_t classes = check_test_expression_class(&reader, i);
                // --scalar's walk, which reads no class code, gets none.
                const uint8_t uncoded = (uint8_t)(classes & ~BUSTER_CHECK_EXPRESSION_CODE_MASK);
                const uint32_t first_value = (classes & BUSTER_CHECK_EXPRESSION_ACTS) == 0 ? BUSTER_CHECK_NONE : UINT32_C(0x5a5a5a5a);
                class_differences += visits[i] != (scalar != 0 ? uncoded : classes) || reader.first_value[i] != first_value;
                class_differences += check_input_expression_class(&reader, i, true) != classes || check_input_expression_class(&reader, i, false) != uncoded;
                expected_acting += (classes & BUSTER_CHECK_EXPRESSION_ACTS) != 0;
            }
            class_differences += visits[count] != 0xa5 || acting != expected_acting || input.visit_misses != 0;
        }
        // A mark the columns contradict, either way, is a miss the scalar path counts.
        if (count != 0)
        {
            const uint32_t wrong = check_test_random(test) % count;
            marks[wrong >> 6] ^= UINT64_C(1) << (wrong & 63);
            parser.options.scalar = true;
            input.visit_misses = 0;
            check_input_expression_classes(&reader);
            class_differences += input.visit_misses != 1;
            marks[wrong >> 6] ^= UINT64_C(1) << (wrong & 63);
        }
        // The empty representation must agree with an independently read dense zero column, including vector
        // tails at the guard page. No address may be formed from the null asm column, even under a zero mask.
        uint32_t* dense_asms = input.asm_ids;
        uint8_t expected_empty[201];
        memset(dense_asms, 0, (uint64_t)count * sizeof(uint32_t));
        memset(marks, 0, sizeof(marks));
        for (uint32_t i = 0; i < count; i += 1)
        {
            expected_empty[i] = check_test_expression_class(&reader, i);
            marks[i >> 6] |= (uint64_t)((expected_empty[i] & BUSTER_CHECK_EXPRESSION_SPARSE) != 0) << (i & 63);
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
                const uint8_t uncoded = (uint8_t)(classes & ~BUSTER_CHECK_EXPRESSION_CODE_MASK);
                const uint32_t first_value = (classes & BUSTER_CHECK_EXPRESSION_ACTS) == 0 ? BUSTER_CHECK_NONE : UINT32_C(0x5a5a5a5a);
                class_differences += visits[i] != (scalar != 0 ? uncoded : classes) || reader.first_value[i] != first_value;
                class_differences += check_input_expression_class(&reader, i, true) != classes || check_input_asm_id(&input, i) != 0;
                expected_acting += (classes & BUSTER_CHECK_EXPRESSION_ACTS) != 0;
            }
            class_differences += visits[count] != 0xa5 || acting != expected_acting || check_input_asm_id(&input, count) != 0;
        }
        input.asm_ids = dense_asms;
        parser.options.scalar = false;
        // The coded walk's compaction of these classes (check_input_expression_compact), by both paths: the tokens the walk
        // acts at among each range of a postorder (a random permutation of the tokens), in its order, with their bytes;
        // from bytes that end as the reader's do (count + 1 and BUSTER_CHECK_VISITS_PADDING, three past the last token's,
        // the most the gathers reach), and into arrays of exactly the range's room, each ending at a PROT_NONE page.
        check_input_expression_classes(&reader);
        uint32_t* postorder = check_test_scan_column(mappings[POSTORDER] + page, count, 4);
        const uint32_t padded_count = count + 1 + BUSTER_CHECK_VISITS_PADDING;
        uint8_t* padded = check_test_scan_column(mappings[PADDED_VISITS] + page, padded_count, 1);
        for (uint32_t i = 0; i < count; i += 1)
        {
            postorder[i] = i;
        }
        for (uint32_t i = count; i > 1; i -= 1)
        {
            const uint32_t j = check_test_random(test) % i;
            const uint32_t swapped = postorder[i - 1];
            postorder[i - 1] = postorder[j];
            postorder[j] = swapped;
        }
        memcpy(padded, visits, count);
        for (uint32_t i = count; i < padded_count; i += 1)
        {
            padded[i] = (uint8_t)check_test_random(test);
        }
        reader.visits = padded;
        reader.postorder = postorder;
        const uint32_t ranges[][2] = { { 0, count }, { count / 3, count }, { count / 2, count / 2 + (count + 3) / 4 }, { count > 0 ? 1 : 0, count > 1 ? count - 1 : count } };
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(ranges); r += 1)
        {
            const uint32_t first = ranges[r][0];
            const uint32_t end = ranges[r][1];
            uint32_t* compact_tokens = check_test_scan_column(mappings[COMPACT_TOKENS] + page, end - first, 4);
            uint32_t* compact_classes = check_test_scan_column(mappings[COMPACT_CLASSES] + page, end - first, 4);
            // Every token the walk acts at, and all but its DROPs, as the coded walk keeps them.
            const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
            for (uint32_t run = 0; run < 4; run += 1)
            {
                const uint32_t skipped = run < 2 ? UINT32_MAX : dropped;
                const uint32_t acting = check_input_expression_compact(&reader, first, end, compact_tokens, compact_classes, skipped, (run & 1) != 0);
                uint32_t expected = 0;
                for (uint32_t position = first; position < end; position += 1)
                {
                    const uint32_t token = postorder[position];
                    if ((padded[token] & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && padded[token] != skipped)
                    {
                        class_differences += expected >= acting || compact_tokens[expected] != token || compact_classes[expected] != padded[token];
                        expected += 1;
                    }
                }
                class_differences += acting != expected;
            }
        }
        reader.visits = visits;
        reader.postorder = NULL;
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

// check_input_selection_positions (#877) by both paths, against the test's own reading of the tree: random forests of
// up to 1,100 tokens, whose parents come before or after their children, with names drawn among the two selections',
// another and none, and a random order of the tokens in which each comes after its children as their postorder. A
// close's bit is set where its least child has one of the two names. The names, the tree's columns and the bits each
// end at a PROT_NONE page, so a load past one faults.
static __attribute__((noinline)) void check_test_selection_positions(CheckTest* test)
{
    enum
    {
        NAMES,
        PARENTS,
        POSTORDER,
        POSITIONS,
        FIRST_CHILD,
        BITS,
        COLUMNS,
    };
    enum
    {
        CAPACITY = 1100,
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t bytes = (((uint64_t)CAPACITY + 1) * sizeof(uint32_t) + page - 1) / page * page;
    uint8_t* mappings[COLUMNS];
    bool ready = true;
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        mappings[i] = mmap(0, bytes + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[i] != MAP_FAILED && mprotect(mappings[i] + bytes, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "guarded selection position columns' allocation");
    static uint64_t expected[(CAPACITY + 63) / 64];
    const uint32_t spellings[] = { C_NAME_GENERIC, C_NAME_BUILTIN_CHOOSE_EXPR, C_NAME_BUILTIN_OFFSETOF, 0 };
    uint32_t differences = 0;
    uint32_t selections = 0;
    for (uint32_t round = 0; ready && round < 420; round += 1)
    {
        const uint32_t count = round <= 200 ? round : 201 + check_test_random(test) % (CAPACITY - 200);
        const uint32_t words = (count + 63) / 64;
        uint32_t* names = check_test_scan_column(mappings[NAMES] + bytes, count, 4);
        uint32_t* parents = check_test_scan_column(mappings[PARENTS] + bytes, count, 4);
        uint32_t* postorder = check_test_scan_column(mappings[POSTORDER] + bytes, count, 4);
        uint32_t* positions = check_test_scan_column(mappings[POSITIONS] + bytes, count + 1, 4);
        uint32_t* first_child = check_test_scan_column(mappings[FIRST_CHILD] + bytes, count + 1, 4);
        uint64_t* bits = check_test_scan_column(mappings[BITS] + bytes, words, 8);
        // The postorder, a random order of the tokens: each token's parent is a later one in it, or none.
        for (uint32_t i = 0; i < count; i += 1)
        {
            postorder[i] = i;
        }
        for (uint32_t i = count; i > 1; i -= 1)
        {
            const uint32_t j = check_test_random(test) % i;
            const uint32_t swap = postorder[i - 1];
            postorder[i - 1] = postorder[j];
            postorder[j] = swap;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            positions[postorder[i]] = i;
        }
        positions[count] = count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t later = count - 1 - positions[i];
            parents[i] = later == 0 || check_test_random(test) % 8 == 0 ? BUSTER_PARSE_NONE : postorder[positions[i] + 1 + check_test_random(test) % later];
            names[i] = spellings[check_test_random(test) % BUSTER_ARRAY_LENGTH(spellings)];
        }
        // Each token's least child, as parse_tree_links links them: the last write from the end.
        for (uint32_t i = 0; i <= count; i += 1)
        {
            first_child[i] = BUSTER_PARSE_NONE;
        }
        for (uint32_t i = count; i-- > 0;)
        {
            if (parents[i] < count)
            {
                first_child[parents[i]] = i;
            }
        }
        memset(expected, 0, sizeof(expected));
        uint32_t expected_count = 0;
        for (uint32_t keyword = 0; keyword < count; keyword += 1)
        {
            const uint32_t close = parents[keyword];
            bool least = close < count && (names[keyword] == C_NAME_GENERIC || names[keyword] == C_NAME_BUILTIN_CHOOSE_EXPR);
            for (uint32_t other = 0; least && other < keyword; other += 1)
            {
                least = parents[other] != close;
            }
            if (least)
            {
                expected[positions[close] / 64] |= UINT64_C(1) << (positions[close] % 64);
                expected_count += 1;
            }
        }
        selections += expected_count;
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            memset(bits, 0, (uint64_t)words * sizeof(uint64_t));
            const uint32_t found = check_input_selection_positions(names, parents, first_child, postorder, positions, count, bits, scalar != 0);
            differences += found != expected_count || memcmp(bits, expected, (uint64_t)words * sizeof(uint64_t)) != 0;
        }
    }
    char description[160];
    snprintf(description, sizeof(description), "the selections' closes: %u of 840 position sets differ from the tree's (%u closes)", differences, selections);
    check_test_expect(test, ready && differences == 0 && selections != 0, description);
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], bytes + page);
        }
    }
}

// The asm labels' kernels (#877) by both paths, against the test's own reading of the columns: the tokens with
// attributes (check_input_asm_attributed_tokens), the tokens whose object is labelled (check_input_asm_labelled_tokens)
// and the uses of labelled declarations (check_input_asm_labelled_uses), on random columns of every length to 200 and
// some to 1,100, whose objects and declarations fall past the tokens too. Each column ends at a PROT_NONE page, so a
// load or a store past one faults.
static __attribute__((noinline)) void check_test_asm_label_kernels(CheckTest* test)
{
    enum
    {
        ATTRIBUTES,
        OBJECTS,
        DECLARATIONS,
        LABELLED,
        DECLARED,
        TOKENS,
        COLUMNS,
    };
    enum
    {
        CAPACITY = 1100,
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t bytes = (((uint64_t)CAPACITY + 1) * sizeof(void*) + page - 1) / page * page;
    uint8_t* mappings[COLUMNS];
    bool ready = true;
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        mappings[i] = mmap(0, bytes + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[i] != MAP_FAILED && mprotect(mappings[i] + bytes, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "guarded asm label kernel columns' allocation");
    static uint32_t expected[CAPACITY];
    static uint32_t expected_declared[(CAPACITY + 31) / 32];
    static CheckInputAttributes record;
    uint32_t differences = 0;
    uint32_t uses = 0;
    for (uint32_t round = 0; ready && round < 260; round += 1)
    {
        const uint32_t count = round <= 200 ? round : 201 + check_test_random(test) % (CAPACITY - 200);
        const uint32_t slots = count + 1;
        const uint32_t object_words = (slots + 31) / 32;
        const uint32_t token_words = (count + 31) / 32;
        const uint32_t density = 1 + check_test_random(test) % 16;
        CheckInputAttributes** attributes = check_test_scan_column(mappings[ATTRIBUTES] + bytes, count, sizeof(void*));
        uint32_t* objects = check_test_scan_column(mappings[OBJECTS] + bytes, count, 4);
        uint32_t* declarations = check_test_scan_column(mappings[DECLARATIONS] + bytes, count, 4);
        uint32_t* labelled = check_test_scan_column(mappings[LABELLED] + bytes, object_words, 4);
        uint32_t* declared = check_test_scan_column(mappings[DECLARED] + bytes, token_words, 4);
        uint32_t* tokens = check_test_scan_column(mappings[TOKENS] + bytes, count, 4);
        for (uint32_t i = 0; i < count; i += 1)
        {
            attributes[i] = check_test_random(test) % density == 0 ? &record : NULL;
            const uint32_t object = check_test_random(test) % (slots + 2);
            objects[i] = check_test_random(test) % 3 == 0 ? 0 : object;
            declarations[i] = check_test_random(test) % 4 == 0 ? BUSTER_CHECK_NONE : check_test_random(test) % (count + 2);
        }
        for (uint32_t w = 0; w < object_words; w += 1)
        {
            labelled[w] = check_test_random(test) & check_test_random(test);
        }
        const CheckInput input = {
            .token_count = count,
            .attributes = attributes,
            .objects = objects,
            .declarations = declarations,
        };
        uint32_t expected_count = 0;
        memset(expected_declared, 0, sizeof(expected_declared));
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (attributes[i] != NULL)
            {
                expected[expected_count++] = i;
            }
            if (objects[i] != 0 && objects[i] < slots && ((labelled[objects[i] / 32] >> (objects[i] % 32)) & 1) != 0)
            {
                expected_declared[i / 32] |= UINT32_C(1) << (i % 32);
            }
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            // The scalar path's uses are every token, whose declaration the pass tests: a host without the vector set
            // takes it for both.
            const bool every = BUSTER_SCALAR_PATH(scalar != 0);
            const uint32_t found = check_input_asm_attributed_tokens(&input, count, tokens, scalar != 0);
            differences += found != expected_count || memcmp(tokens, expected, (uint64_t)found * sizeof(uint32_t)) != 0;
            memset(declared, 0xa5, (uint64_t)token_words * sizeof(uint32_t));
            check_input_asm_labelled_tokens(&input, labelled, declared, scalar != 0);
            differences += memcmp(declared, expected_declared, (uint64_t)token_words * sizeof(uint32_t)) != 0;
            for (uint32_t first = 0; first < count; first += 16)
            {
                uint32_t mask = 0;
                for (uint32_t lane = 0; lane < 16 && first + lane < count; lane += 1)
                {
                    const uint32_t declaration = declarations[first + lane];
                    const bool marked = declaration < count && ((expected_declared[declaration / 32] >> (declaration % 32)) & 1) != 0;
                    mask |= marked || every ? UINT32_C(1) << lane : 0;
                    uses += marked && scalar == 0;
                }
                differences += check_input_asm_labelled_uses(&input, declared, first, count, scalar != 0) != mask;
            }
        }
    }
    char description[160];
    snprintf(description, sizeof(description), "the asm labels' kernels: %u results differ from the columns' (%u uses of labelled declarations)",
             differences, uses);
    check_test_expect(test, ready && differences == 0 && uses != 0, description);
    for (uint32_t i = 0; i < COLUMNS; i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], bytes + page);
        }
    }
}
