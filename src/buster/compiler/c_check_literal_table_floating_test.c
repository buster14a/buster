// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #658's floating-table continuation. The ordinary expression graph is the source/byte oracle; hand-written
// IEEE representations independently check zero signs and boundary values. Every masked store tail ends at a
// protected page. Integer precision loss and literal diagnostics must refuse the compact producer, including late in a
// list; a floating source rounds as the ordinary initializer rounds it.

static bool check_test_literal_floating_source(char* source, uint64_t capacity, const char* type, uint32_t count, bool sized, bool wide, const char* last)
{
    static const char* const single[] = {
        "0.1f",
        "-0.0f",
        "0.0f",
        "0",
        "-0",
        "-13",
        "0xffU",
        "1.25",
        "0x1p-149f",
        "0x1.fffffep127f",
        "1.5L",
        "075",
        "3.0e-4f",
        "0x1p24",
        "16777216U",
        "0.1",
        "1e300",
        "-0.1L",
    };
    static const char* const double_precision[] = {
        "0.1",
        "-0.0",
        "0.0",
        "0",
        "-0",
        "-13",
        "0xffU",
        "0.1f",
        "0x1p-1074",
        "0x1.fffffffffffffp1023",
        "1.5L",
        "075",
        "3.0e-200",
        "9007199254740992ULL",
        "0x1p53",
        "0.1L",
        "-1e4000L",
    };
    const char* const* spellings = wide ? double_precision : single;
    const uint32_t spelling_count = wide ? BUSTER_ARRAY_LENGTH(double_precision) : BUSTER_ARRAY_LENGTH(single);
    int used = snprintf(source, capacity, "%s table[", type);
    if (sized && used > 0 && (uint64_t)used < capacity)
    {
        used += snprintf(source + used, capacity - (uint64_t)used, "%u", count + 3);
    }
    if (used > 0 && (uint64_t)used < capacity)
    {
        used += snprintf(source + used, capacity - (uint64_t)used, "]={");
    }
    for (uint32_t i = 0; i < count && used > 0 && (uint64_t)used < capacity; i += 1)
    {
        const char* spelling = last != NULL && i + 1 == count ? last : spellings[i % spelling_count];
        const char* separator = i + 1 < count || sized ? "," : "";
        used += snprintf(source + used, capacity - (uint64_t)used, "%s%s", spelling, separator);
    }
    if (used > 0 && (uint64_t)used < capacity)
    {
        used += snprintf(source + used, capacity - (uint64_t)used, "};\n");
    }
    return used > 0 && (uint64_t)used < capacity;
}

static void check_test_literal_floating_values(CheckTest* test)
{
    typedef struct FloatingCase FloatingCase;
    struct FloatingCase
    {
        const char* spelling;
        TypeId element;
        uint64_t bits;
        bool negative;
        bool accepted;
    };
    static const FloatingCase cases[] = {
        { "0.0f", BUSTER_TYPE_FLOAT, UINT64_C(0x80000000), true, true },
        { "0.0", BUSTER_TYPE_DOUBLE, UINT64_C(0x8000000000000000), true, true },
        { "0", BUSTER_TYPE_FLOAT, 0, true, true },
        { "0", BUSTER_TYPE_DOUBLE, 0, true, true },
        { "0.1f", BUSTER_TYPE_FLOAT, UINT64_C(0x3dcccccd), false, true },
        { "0.1", BUSTER_TYPE_DOUBLE, UINT64_C(0x3fb999999999999a), false, true },
        { "0x1p-149f", BUSTER_TYPE_FLOAT, 1, false, true },
        { "0x1p-1074", BUSTER_TYPE_DOUBLE, 1, false, true },
        { "9007199254740992ULL", BUSTER_TYPE_DOUBLE, UINT64_C(0x4340000000000000), false, true },
        { "0.1", BUSTER_TYPE_FLOAT, UINT64_C(0x3dcccccd), false, true },
        { "0.1", BUSTER_TYPE_FLOAT, UINT64_C(0xbdcccccd), true, true },
        { "0.1L", BUSTER_TYPE_DOUBLE, UINT64_C(0x3fb999999999999a), false, true },
        { "1e300", BUSTER_TYPE_FLOAT, UINT64_C(0x7f800000), false, true },
        { "16777216", BUSTER_TYPE_FLOAT, UINT64_C(0x4b800000), false, true },
        { "16777217", BUSTER_TYPE_FLOAT, 0, false, false },
        { "9007199254740993ULL", BUSTER_TYPE_DOUBLE, 0, false, false },
        { "1e9999f", BUSTER_TYPE_FLOAT, 0, false, false },
        { "1e-9999f", BUSTER_TYPE_FLOAT, 0, false, false },
        { "1.0fi", BUSTER_TYPE_FLOAT, 0, false, false },
        { "1.0ff", BUSTER_TYPE_FLOAT, 0, false, false },
        { "1wb", BUSTER_TYPE_DOUBLE, 0, false, false },
    };
    CheckTestTypes types = { 0 };
    CheckInput input = { 0 };
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const bool ready = scratch.base != NULL && check_test_types_open(&types);
    check_test_expect(test, ready, "floating literal value fixtures have type and scratch storage");
    CheckInputReader reader = {
        .types = &types.table,
        .output = &input,
        .scratch = &scratch,
    };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const FloatingCase* fixture = cases + i;
        const uint64_t sentinel = UINT64_C(0x9d7a6b5c4d3e2f10);
        uint64_t bits = sentinel;
        const bool accepted = check_literal_table_floating_value(&reader, (const uint8_t*)fixture->spelling,
                                                               (uint32_t)strlen(fixture->spelling), fixture->negative, fixture->element, &bits);
        check_test_expect(test, accepted == fixture->accepted && bits == (accepted ? fixture->bits : sentinel) &&
                               !input.resource && scratch.length == 0,
                          "floating literal bytes and precision/range/syntax refusal match independent expectations");
    }
    if (ready)
    {
        const uint64_t capacity = scratch.capacity;
        scratch.capacity = scratch.length;
        uint64_t bits = UINT64_MAX;
        const bool accepted = check_literal_table_floating_value(&reader, (const uint8_t*)"1.25f", 5, false, BUSTER_TYPE_FLOAT, &bits);
        check_test_expect(test, !accepted && input.resource && bits == UINT64_MAX && scratch.length == 0,
                          "floating spelling allocation failure is a resource failure without publishing a value");
        scratch.capacity = capacity;
    }
    check_test_types_close(&types);
    arena_release(&scratch);
}

// Which spellings check_literal_table_floating_integers converts as integers: no '.', no exponent ('e' in decimal, 'p' in
// hexadecimal, where 'e' is a digit).
static void check_test_literal_floating_integer_spellings(CheckTest* test)
{
    typedef struct SpellingCase SpellingCase;
    struct SpellingCase
    {
        const char* spelling;
        bool integer;
    };
    static const SpellingCase cases[] = {
        { "16777217", true },
        { "0x1e", true },
        { "0X1E5", true },
        { "075", true },
        { "9007199254740993ULL", true },
        { "1e5", false },
        { "1E5", false },
        { "1.5", false },
        { ".5", false },
        { "1.", false },
        { "0x1p3", false },
        { "0x1P-3", false },
        { "0x1.8", false },
        { "0.1f", false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const char* spelling = cases[i].spelling;
        check_test_expect(test, check_literal_table_integer_spelling((const uint8_t*)spelling, (uint32_t)strlen(spelling)) == cases[i].integer,
                          "an integer literal's spelling is told from a floating literal's");
    }
    // Each base's longest spelling exact in the format, and one digit more, for float (32) and double (64).
    typedef struct RoundCase RoundCase;
    struct RoundCase
    {
        const char* spelling;
        uint32_t width;
        bool may_round;
    };
    static const RoundCase rounds[] = {
        { "9999999", 32, false },
        { "16777217", 32, true },
        { "1677721u", 32, false },
        { "0xffffff", 32, false },
        { "0x1000001", 32, true },
        { "077777777", 32, false },
        { "0100000001", 32, true },
        { "0b111111111111111111111111", 32, false },
        { "0b1000000000000000000000001", 32, true },
        { "999999999999999", 64, false },
        { "9007199254740993ULL", 64, true },
        { "0xfffffffffffff", 64, false },
        { "0x20000000000001", 64, true },
        { "077777777777777777", 64, false },
        { "0400000000000000001", 64, true },
        { "16'777'217", 32, true },
        { "1'000'000", 32, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rounds); i += 1)
    {
        const char* spelling = rounds[i].spelling;
        check_test_expect(test, check_literal_table_integer_may_round((const uint8_t*)spelling, (uint32_t)strlen(spelling), rounds[i].width) == rounds[i].may_round,
                          "an integer spelling is tested first exactly when its digits allow a value its format can't hold");
    }
}

static void check_test_literal_floating_tails(CheckTest* test, bool wide)
{
    char source[8192];
    uint8_t expected[2048];
    InternTable identifiers = { 0 };
    Preprocessor pp = { 0 };
    Parser parser = { 0 };
    CheckTestTypes types = { 0 };
    CheckInput input = { 0 };
    CheckStream stream = { 0 };
    CheckInitializers plans = { 0 };
    Arena output = arena_reserve(UINT64_C(1) << 27);
    Arena scratch = arena_reserve(UINT64_C(1) << 27);
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parsing = { 0 };
    const CheckOptions checking = { 0 };
    bool ready = output.base != NULL && scratch.base != NULL &&
                 check_test_literal_floating_source(source, sizeof(source), wide ? "double" : "float", 129, true, wide, NULL) &&
                 intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                 parse_initialize(&parser, &parsing, preprocessing.language) &&
                 pp_run_source(&pp, "floating-table-tail.c", (const uint8_t*)source, strlen(source)) && parse_translation_unit(&parser, &pp) &&
                 check_test_types_open(&types) && check_input_tables_reference(&input, &parser, &pp, &types.table, &output, &scratch) &&
                 check_graph_run(&types.table, &input.graph, &output, &scratch, &checking, &stream) && stream.report_count == 0 &&
                 check_initializers_run(&plans, &input, &stream, &parser, &types.table, &output, &scratch) && plans.count == 1;
    ready = ready && plans.plans[0].object_size <= sizeof(expected) &&
            check_init_object_bytes(&types.table, plans.plans, expected, plans.plans[0].object_size);
    check_test_expect(test, ready, "floating protected-tail fixture obtains bytes from ordinary per-element planning");
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ready = ready && mapping != MAP_FAILED && page >= BUSTER_CHECK_LITERAL_TABLE_BATCH * sizeof(double) &&
            mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "floating batch output has a protected logical tail");
    if (ready)
    {
        const uint32_t declaration = plans.declaration_tokens[0];
        const uint32_t begin = check_sparse_get(input.initializer_begins, declaration);
        const uint32_t end = check_sparse_get(input.initializer_ends, declaration);
        uint32_t tokens[BUSTER_CHECK_LITERAL_TABLE_BATCH];
        uint32_t found = 0;
        for (uint32_t token = begin + 1; token < end && found < BUSTER_CHECK_LITERAL_TABLE_BATCH; token += 1)
        {
            if (parser.tokens.kinds[token] == PARSE_KIND_NUMBER)
            {
                tokens[found] = token;
                found += 1;
            }
        }
        check_test_expect(test, found == BUSTER_CHECK_LITERAL_TABLE_BATCH, "floating tail fixture covers one full mixed-format batch");
        CheckInputReader reader = {
            .parser = &parser,
            .pp = &pp,
            .types = &types.table,
            .output = &input,
            .scratch = &scratch,
        };
        const uint32_t width = wide ? 64 : 32;
        const TypeId element = wide ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_FLOAT;
        // The batch's elements as check_literal_table_floating gathers them, none converted before the batch.
        const uint8_t* spellings[BUSTER_CHECK_LITERAL_TABLE_BATCH];
        uint32_t lengths[BUSTER_CHECK_LITERAL_TABLE_BATCH];
        uint64_t converted[1] = { 0 };
        bool spelled = true;
        for (uint32_t i = 0; spelled && i < found; i += 1)
        {
            spelled = check_input_spelling(&reader, tokens[i], &spellings[i], &lengths[i]);
        }
        check_test_expect(test, spelled, "floating tail fixture's spellings are made");
        const CheckLiteralTableElements elements = {
            .tokens = tokens,
            .spellings = spellings,
            .lengths = lengths,
            .converted = converted,
        };
        const uint64_t spelled_length = scratch.length;
        for (uint32_t path = 0; spelled && path < 2; path += 1)
        {
            parser.options.scalar = path != 0;
            for (uint32_t count = 0; count <= found; count += 1)
            {
                const uint64_t bytes = (uint64_t)count * (width / 8);
                uint8_t* destination = mapping + page - bytes;
                const bool accepted = check_literal_table_floating_batch(&reader, &elements, 0, count, element, width, destination);
                check_test_expect(test, accepted && (bytes == 0 || memcmp(destination, expected, bytes) == 0) &&
                                       !input.resource && scratch.length == spelled_length,
                                  "every scalar/vector floating store tail equals dense bytes without touching the protected page");
            }
        }
        arena_pop_to(&scratch, 0);
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    check_test_types_close(&types);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&output);
    arena_release(&scratch);
}

static void check_test_literal_floating_tables(CheckTest* test, const char* clang)
{
    check_test_literal_floating_values(test);
    check_test_literal_floating_integer_spellings(test);
    static const uint32_t counts[] = { 127, 128, 129, 255, 256, 257 };
    char source[32768];
    char table[8192];
    for (uint32_t format = 0; format < 2; format += 1)
    {
        const bool wide = format != 0;
        const char* type = wide ? "const double" : "const float";
        check_test_literal_floating_tails(test, wide);
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(counts); i += 1)
        {
            const bool made = check_test_literal_floating_source(source, sizeof(source), type, counts[i], i % 2 == 0, wide, NULL);
            check_test_expect(test, made, "mixed floating table source fits");
            if (made)
            {
                check_test_literal_table_case(test, source, counts[i] >= BUSTER_CHECK_LITERAL_TABLE_MINIMUM, false);
            }
        }
        const char* loss = wide ? "9007199254740993ULL" : "16777217";
        bool made = check_test_literal_floating_source(source, sizeof(source), type, 129, true, wide, loss);
        check_test_expect(test, made, "late integer precision-loss source fits");
        if (made)
        {
            check_test_literal_table_case(test, source, false, false);
        }
        made = check_test_literal_floating_source(source, sizeof(source), wide ? "volatile double" : "volatile float", 129, true, wide, NULL);
        check_test_expect(test, made, "volatile floating fallback source fits");
        if (made)
        {
            check_test_literal_table_case(test, source, false, false);
        }
        made = check_test_literal_floating_source(table, sizeof(table), type, 129, false, wide, NULL);
        const int replay = made ? snprintf(source, sizeof(source),
                                          "int before(int n){struct P{long x;int y;};return n+(int)sizeof(struct P);}\n%s", table) : -1;
        check_test_expect(test, replay > 0 && (uint64_t)replay < sizeof(source), "floating deferred replay source fits");
        if (replay > 0 && (uint64_t)replay < sizeof(source))
        {
            check_test_literal_table_case(test, source, true, true);
        }
        // Runtime casts stay in int range; the byte oracle above covers extremes, subnormals and zero signs.
        made = check_test_literal_table_source(source, sizeof(source), type, 129, 4, true);
        const uint64_t length = made ? strlen(source) : 0;
        const int appended = made ? snprintf(source + length, sizeof(source) - length,
                                            "int tested(int n){return (int)table[(unsigned)n%%129]+(int)table[131]+(int)sizeof table;}") : -1;
        made = made && appended > 0 && (uint64_t)appended < sizeof(source) - length;
        check_test_expect(test, made, "floating table native source fits");
        if (made)
        {
            check_test_program_source(test, source, "literal-floating-table-native", clang);
        }
    }
    // Floating sources round as the ordinary initializer rounds them, without a warning, so they stay compact.
    const bool made = check_test_literal_floating_source(source, sizeof(source), "float", 129, true, false, "0.3");
    check_test_expect(test, made, "late rounded floating source fits");
    if (made)
    {
        check_test_literal_table_case(test, source, true, false);
    }
    for (uint32_t form = 5; form <= 7; form += 2)
    {
        const bool fallback = check_test_literal_table_source(source, sizeof(source), "double", 129, form, true);
        check_test_expect(test, fallback, "designated/general-expression floating fallback source fits");
        if (fallback)
        {
            check_test_literal_table_case(test, source, false, false);
        }
    }
}
