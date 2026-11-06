// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #658 source/consumer regressions. Compare compact scalar/vector tables with ordinary expression graphs,
// then execute representative objects against Clang. Guarded syntax tails and failing output reservations check the
// producer contract separately. This file follows c_check_program_test.c's source/native helpers.

static bool check_test_literal_table_source(char* source, uint64_t capacity, const char* type, uint32_t count, uint32_t form, bool sized)
{
    int used = snprintf(source, capacity, "#define V 13\n%s table[", type);
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
        const uint32_t value = (i * 37 + 13) & 255;
        const char* separator = i + 1 < count || form % 2 == 0 ? "," : "";
        if (form == 1)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "%d%s", (int)(i % 128) - 64, separator);
        }
        else if (form == 2)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "-0x123456789L%s", separator);
        }
        else if (form == 3)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "0xfedcba9876543210ULL%s", separator);
        }
        else if (form == 4)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "%u.25%s", value, separator);
        }
        else if (form == 5 && i == 0)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "[2]=3,");
        }
        else if (form == 6 && i == 0)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "1000,");
        }
        else if (form == 7 && i == 0)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "1+2,");
        }
        else if (i % 7 == 0)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "V%s", separator);
        }
        else if (i % 5 == 0)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "0%oU%s", value, separator);
        }
        else if (i % 3 == 0)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "0X%02X%s", value, separator);
        }
        else if (i % 2 == 0)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "%u%s", value, separator);
        }
        else
        {
            used += snprintf(source + used, capacity - (uint64_t)used, "0x%02x%s", value, separator);
        }
    }
    if (used > 0 && (uint64_t)used < capacity)
    {
        used += snprintf(source + used, capacity - (uint64_t)used, "};\n");
    }
    return used > 0 && (uint64_t)used < capacity;
}

static void check_test_literal_table_case(CheckTest* test, const char* source, bool compact, bool replay)
{
    uint8_t expected[16384];
    uint64_t expected_size = 0;
    uint32_t dense_nodes = 0;
    for (uint32_t path = 0; path < (replay ? 5u : 3u); path += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        CheckTestTypes types = { 0 };
        Arena output = arena_reserve(UINT64_C(1) << 27);
        Arena scratch = arena_reserve(UINT64_C(1) << 27);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckDeferred deferred = { .fail_after_replay = path == 4 };
        const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parsing = { .scalar = path == 1 };
        const CheckOptions checking = { .scalar = path == 1 };
        bool ready = output.base != NULL && scratch.base != NULL && intern_initialize(&identifiers) &&
                     pp_initialize(&pp, &preprocessing, &identifiers) && parse_initialize(&parser, &parsing, preprocessing.language) &&
                     pp_run_source(&pp, "literal-table.c", (const uint8_t*)source, strlen(source)) && parse_translation_unit(&parser, &pp) &&
                     check_test_types_open(&types);
        ready = ready && (path == 0 ? check_input_tables_reference(&input, &parser, &pp, &types.table, &output, &scratch) :
                          path >= 3 ? check_input_deferred(&deferred, &input, &parser, &pp, &types.table, &output, &scratch, 2, CHECK_INPUT_BINDINGS_DEFAULT) :
                                      check_input(&input, &parser, &pp, &types.table, &output, &scratch)) &&
                check_graph_run(&types.table, &input.graph, &output, &scratch, &checking, &stream) && stream.report_count == 0 &&
                check_initializers_run(&plans, &input, &stream, &parser, &types.table, &output, &scratch) && plans.count == 1;
        check_test_expect(test, ready, "literal table source checks with compact paths and the full-graph reference");
        if (ready)
        {
            const CheckInitializer* plan = plans.plans;
            uint8_t bytes[sizeof(expected)];
            ready = plan->object_size <= sizeof(bytes) && check_init_object_bytes(&types.table, plan, bytes, plan->object_size);
            if (path == 0 && ready)
            {
                expected_size = plan->object_size;
                dense_nodes = input.graph.count;
                memcpy(expected, bytes, expected_size);
            }
            else if (path != 0)
            {
                ready = ready && plan->object_size == expected_size && memcmp(bytes, expected, expected_size) == 0;
            }
            check_test_expect(test, ready, "compact literal table bytes, omitted zero tail and completed type equal ordinary per-element planning");
            check_test_expect(test, input.literal_table_count == (path != 0 && compact ? 1u : 0u) &&
                                   (!compact || path == 0 || (input.graph.count < dense_nodes && dense_nodes - input.graph.count >= BUSTER_CHECK_LITERAL_TABLE_MINIMUM &&
                                    plan->count == 1 && plan->parts[0].kind == CHECK_INIT_BYTES)),
                              "eligible tables publish one bytes part and zero element nodes; unsupported shapes retain the full checker");
            if (compact && path != 0)
            {
                const CheckInputLiteralTable* table = input.literal_tables;
                check_test_expect(test, table->plan.type == input.declared_types[table->declaration] && table->part.type == table->plan.type,
                                  "retained array types follow deferred type replay and source-order fallback");
            }
            if (path >= 3)
            {
                check_test_expect(test, deferred.unsupported == (path == 4), "the type replay fixture exercises both a real merge and forced late fallback");
            }
        }
        check_test_expect(test, scratch.length == 0, "table production and all consumers restore scratch");
        check_test_types_close(&types);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        arena_release(&output);
        arena_release(&scratch);
        check_deferred_release(&deferred);
    }
}

static void check_test_literal_table_shapes(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ready = mapping != MAP_FAILED && page >= 257;
    ready = ready && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "literal table syntax has a protected logical tail");
    for (uint32_t count = 0; ready && count <= 257; count += 1)
    {
        uint8_t* kinds = mapping + page - count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            kinds[i] = i % 3 == 0 ? PARSE_KIND_MINUS : i % 3 == 1 ? PARSE_KIND_NUMBER : PARSE_KIND_COMMA;
        }
        const uint32_t expected = count % 3 == 1 ? 0 : (count + 1) / 3;
        check_test_expect(test, check_literal_table_shape(kinds, 0, count, true) == expected && check_literal_table_shape(kinds, 0, count, false) == expected,
                          "literal grammar carries minus/number/comma state across every guarded tail");
        for (uint32_t at = 0; at < count; at += 17)
        {
            const uint8_t saved = kinds[at];
            kinds[at] = PARSE_KIND_PLUS;
            check_test_expect(test, check_literal_table_shape(kinds, 0, count, true) == 0 && check_literal_table_shape(kinds, 0, count, false) == 0,
                              "nonliteral operators make the whole table retain general checking");
            kinds[at] = saved;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

// An independent recursive reading of check_literal_table_rows's grammar: `{ list }` rows of exactly `columns` numbers.
static uint32_t check_test_literal_rows_reference(const uint8_t* kinds, uint32_t count, uint32_t columns)
{
    uint32_t at = 0;
    uint32_t numbers = 0;
    bool valid = count != 0 && columns != 0;
    while (valid && at < count)
    {
        valid = kinds[at] == PARSE_KIND_L_BRACE;
        at += 1;
        uint32_t row = 0;
        while (valid && at < count && kinds[at] != PARSE_KIND_R_BRACE)
        {
            at += kinds[at] == PARSE_KIND_MINUS;
            valid = at < count && kinds[at] == PARSE_KIND_NUMBER;
            at += 1;
            row += 1;
            if (valid && at < count && kinds[at] == PARSE_KIND_COMMA)
            {
                at += 1;
            }
            else
            {
                valid = valid && at < count && kinds[at] == PARSE_KIND_R_BRACE;
            }
        }
        valid = valid && at < count && row == columns;
        at += 1;
        numbers += row;
        if (valid && at < count)
        {
            valid = kinds[at] == PARSE_KIND_COMMA;
            at += 1;
        }
    }
    return valid ? numbers : 0;
}

// Writes a random table of `rows` rows of `columns` items (an optional minus, a number) with random inner and outer
// trailing commas into `kinds`, and returns its length.
static uint32_t check_test_literal_rows_stream(CheckTest* test, uint8_t* kinds, uint32_t rows, uint32_t columns)
{
    uint32_t length = 0;
    for (uint32_t r = 0; r < rows; r += 1)
    {
        kinds[length] = PARSE_KIND_L_BRACE;
        length += 1;
        for (uint32_t c = 0; c < columns; c += 1)
        {
            if (check_test_random(test) % 4 == 0)
            {
                kinds[length] = PARSE_KIND_MINUS;
                length += 1;
            }
            kinds[length] = PARSE_KIND_NUMBER;
            kinds[length + 1] = PARSE_KIND_COMMA;
            length += c + 1 < columns || check_test_random(test) % 3 == 0 ? 2 : 1;
        }
        kinds[length] = PARSE_KIND_R_BRACE;
        kinds[length + 1] = PARSE_KIND_COMMA;
        length += r + 1 < rows || check_test_random(test) % 2 == 0 ? 2 : 1;
    }
    return length;
}

// check_literal_table_rows's SIMD grammar equals its scalar twin, and both equal the recursive reading, on random tables of
// rows (every brace, minus and trailing comma at every block edge) and on single-token mutations of them, each ending at a
// protected page.
static void check_test_literal_table_rows_shapes(CheckTest* test)
{
    static const uint8_t alphabet[] = { PARSE_KIND_NUMBER, PARSE_KIND_COMMA, PARSE_KIND_MINUS, PARSE_KIND_L_BRACE, PARSE_KIND_R_BRACE, PARSE_KIND_PLUS };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t stream[1024];
    bool ready = mapping != MAP_FAILED && page >= sizeof(stream);
    ready = ready && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "literal rows syntax has a protected logical tail");
    bool same = true;
    bool counted = true;
    bool mutated = false;
    for (uint32_t trial = 0; ready && trial < 3000; trial += 1)
    {
        const uint32_t columns = 1 + check_test_random(test) % (trial % 5 == 0 ? 17 : 5);
        const uint32_t most = (uint32_t)(sizeof(stream) - 2) / (3 * columns + 3);
        const uint32_t rows = 1 + check_test_random(test) % (most < 48 ? most : 48);
        uint32_t length = check_test_literal_rows_stream(test, stream, rows, columns);
        const uint32_t mutation = trial % 4;
        if (mutation != 0 && length > 0)
        {
            const uint32_t at = check_test_random(test) % length;
            const uint8_t kind = alphabet[check_test_random(test) % BUSTER_ARRAY_LENGTH(alphabet)];
            if (mutation == 1)
            {
                stream[at] = kind;
            }
            else if (mutation == 2)
            {
                memmove(stream + at, stream + at + 1, length - at - 1);
                length -= 1;
            }
            else
            {
                memmove(stream + at + 1, stream + at, length - at);
                stream[at] = kind;
                length += 1;
            }
        }
        uint8_t* kinds = mapping + page - length;
        memcpy(kinds, stream, length);
        const uint32_t expected = check_test_literal_rows_reference(kinds, length, columns);
        const uint32_t vector = check_literal_table_rows(kinds, 0, length, columns, false);
        const uint32_t scalar = check_literal_table_rows(kinds, 0, length, columns, true);
        same = same && vector == scalar && scalar == expected;
        counted = counted && (mutation != 0 || expected == rows * columns);
        mutated = mutated || (mutation != 0 && expected == 0);
        for (uint32_t prefix = length > 192 ? length - 192 : 0; same && trial % 16 == 0 && prefix < length; prefix += 1)
        {
            uint8_t* tail = mapping + page - prefix;
            memmove(tail, kinds, prefix);
            same = check_literal_table_rows(tail, 0, prefix, columns, false) == check_literal_table_rows(tail, 0, prefix, columns, true) &&
                   check_literal_table_rows(tail, 0, prefix, columns, true) == check_test_literal_rows_reference(tail, prefix, columns);
            memcpy(kinds, stream, length);
        }
    }
    check_test_expect(test, same, "rows grammar: SIMD blocks, scalar twin and the recursive reading agree on tables, mutations and every guarded tail");
    check_test_expect(test, counted && mutated, "rows grammar: unmutated tables count rows times columns, and mutations reach refusals");
    check_test_expect(test, check_literal_table_rows(stream, 0, 0, 4, false) == 0 && check_literal_table_rows(stream, 0, 0, 4, true) == 0,
                      "rows grammar: an empty table is ineligible");
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

// A file-scope table of `rows` rows of `columns` elements of `type` (#1383). Form 0 spells unsigned values as
// check_test_literal_table_source does, 1 signed ones, 2 floating ones; each fallback form changes one row: 3 shortens
// it, 4 nests every row once more (`table[][2][columns]`), 5 designates its element, 6 designates the row and 7 adds.
static bool check_test_literal_rows_source(char* source, uint64_t capacity, const char* type, uint32_t rows, uint32_t columns, uint32_t form, bool sized)
{
    int used = snprintf(source, capacity, "#define V 13\n%s table[", type);
    if (sized && used > 0 && (uint64_t)used < capacity)
    {
        used += snprintf(source + used, capacity - (uint64_t)used, "%u", rows + 3);
    }
    if (used > 0 && (uint64_t)used < capacity)
    {
        used += snprintf(source + used, capacity - (uint64_t)used, form == 4 ? "][2][%u]={" : "][%u]={", columns);
    }
    for (uint32_t r = 0; r < rows && used > 0 && (uint64_t)used < capacity; r += 1)
    {
        const bool changed = r == 7;
        used += snprintf(source + used, capacity - (uint64_t)used, form == 6 && changed ? "[7]={" : form == 4 ? "{{" : "{");
        const uint32_t length = form == 3 && changed ? columns - 1 : columns;
        for (uint32_t c = 0; c < length && used > 0 && (uint64_t)used < capacity; c += 1)
        {
            const uint32_t i = r * columns + c;
            const uint32_t value = (i * 37 + 13) & 255;
            const char* separator = c + 1 < length || r % 3 == 0 ? "," : "";
            if (form == 5 && changed && c == 0)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "[0]=");
            }
            if (form == 7 && changed && c == 0)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "1+2%s", separator);
            }
            else if (form == 1)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "%d%s", (int)(i % 128) - 64, separator);
            }
            else if (form == 2)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, i % 2 == 0 ? "%u.25%s" : "-%u.5e1%s", value, separator);
            }
            else if (i % 7 == 0)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "V%s", separator);
            }
            else if (i % 5 == 0)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "0%oU%s", value, separator);
            }
            else if (i % 3 == 0)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "0X%02X%s", value, separator);
            }
            else
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "%u%s", value, separator);
            }
            if (form == 4 && c + 1 == length && used > 0 && (uint64_t)used < capacity)
            {
                used += snprintf(source + used, capacity - (uint64_t)used, "},{0");
            }
        }
        if (used > 0 && (uint64_t)used < capacity)
        {
            used += snprintf(source + used, capacity - (uint64_t)used, form == 4 ? "}}%s" : "}%s", r + 1 < rows || r % 2 == 0 ? "," : "");
        }
    }
    if (used > 0 && (uint64_t)used < capacity)
    {
        used += snprintf(source + used, capacity - (uint64_t)used, "};\n");
    }
    return used > 0 && (uint64_t)used < capacity;
}

static void check_test_literal_table_contract(CheckTest* test, const char* element)
{
    char source[8192];
    InternTable identifiers = { 0 };
    Preprocessor pp = { 0 };
    Parser parser = { 0 };
    CheckTestTypes types = { 0 };
    CheckInput input = { 0 };
    Arena output = arena_reserve(UINT64_C(1) << 26);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    Arena rollback = arena_reserve(UINT64_C(1) << 20);
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parsing = { 0 };
    bool ready = output.base != NULL && scratch.base != NULL && rollback.base != NULL &&
                 check_test_literal_table_source(source, sizeof(source), element, 129, 0, true) &&
                 intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                 parse_initialize(&parser, &parsing, preprocessing.language) &&
                 pp_run_source(&pp, "table-contract.c", (const uint8_t*)source, strlen(source)) && parse_translation_unit(&parser, &pp) &&
                 check_test_types_open(&types) && check_input(&input, &parser, &pp, &types.table, &output, &scratch) && input.literal_table_count == 1;
    check_test_expect(test, ready, "literal table contract fixture is compact");
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ready = ready && mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "literal table stores end at a protected page");
    if (ready)
    {
        const uint32_t declaration = input.literal_tables->declaration;
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
        CheckInputReader reader = { .parser = &parser, .pp = &pp, .types = &types.table, .output = &input, .scratch = &scratch };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            parser.options.scalar = path != 0;
            for (uint32_t width = 8; width <= 64; width *= 2)
            {
                for (uint32_t count = 0; count <= found; count += 1)
                {
                    uint8_t* bytes = mapping + page - (uint64_t)count * (width / 8);
                    bool same = check_literal_table_batch(&reader, tokens, count, width, false, bytes);
                    for (uint32_t lane = 0; same && lane < count; lane += 1)
                    {
                        CheckLiteral literal;
                        check_input_literal(&reader, tokens[lane], &literal);
                        const uint64_t expected = literal.integer;
                        same = literal.valid && memcmp(bytes + (uint64_t)lane * (width / 8), &expected, width / 8) == 0;
                    }
                    check_test_expect(test, same && scratch.length == 0, "every scalar/vector batch tail and width matches the independent literal parser within guarded output");
                }
            }
        }
        parser.options.scalar = false;
        const uint64_t scratch_mark = scratch.length;
        uint32_t* declarations = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)parser.tokens.count + 1);
        uint8_t* ignored = declarations != NULL ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, (uint64_t)parser.tokens.count + 1) : NULL;
        bool allocated = ignored != NULL;
        if (allocated)
        {
            memset(declarations, 0xff, ((uint64_t)parser.tokens.count + 1) * sizeof(uint32_t));
            declarations[begin - 1] = declaration;
            memset(ignored, 0x9d, (uint64_t)parser.tokens.count + 1);
        }
        check_test_expect(test, allocated, "literal table rollback adapter has room");
        const uint64_t capacity = rollback.capacity;
        for (uint32_t failure = 0; allocated && failure < 2; failure += 1)
        {
            CheckInput copy = input;
            copy.literal_tables = NULL;
            copy.literal_tables_last = NULL;
            copy.literal_table_count = 0;
            copy.resource = false;
            rollback.length = 17;
            rollback.capacity = rollback.length + (failure != 0 ? input.literal_tables->part.byte_count : 0);
            reader.output = &copy;
            reader.arena = &rollback;
            reader.declared_types = input.declared_types;
            reader.initializer_declarations = declarations;
            reader.ignored = ignored;
            reader.function_body = BUSTER_CHECK_NONE;
            const uint64_t before = scratch.length;
            bool unchanged = !check_input_literal_table_prepare(&reader, begin - 1) && copy.resource && rollback.length == 17 &&
                             scratch.length == before && copy.literal_tables == NULL && copy.literal_table_count == 0;
            for (uint32_t token = 0; unchanged && token <= parser.tokens.count; token += 1)
            {
                unchanged = ignored[token] == 0x9d;
            }
            check_test_expect(test, unchanged, "payload and retained-record allocation failures restore arenas and publish no ignored range or table");
            rollback.capacity = capacity;
        }
        // A floating table's scratch, refused at every 4 bytes past its length (#1210's review): its converted bitset
        // (check_literal_table_floating), then what its conversions take, until the table is accepted. Each refusal is a
        // resource failure that restores both arenas and publishes no ignored range or table; the first room that holds
        // the table accepts it, and no step before it does.
        const bool floating = strcmp(element, "float") == 0 || strcmp(element, "double") == 0;
        // What an accepted table writes besides the table: its query's type and its name's linked type.
        TypeId* query_types = BUSTER_ARENA_PUSH_ARRAY(&scratch, TypeId, (uint64_t)parser.tokens.count + 1);
        TypeId* linked_types = query_types != NULL ? BUSTER_ARENA_PUSH_ARRAY(&scratch, TypeId, (uint64_t)identifiers.count + 1) : NULL;
        allocated = allocated && linked_types != NULL;
        check_test_expect(test, allocated, "floating scratch refusal adapter has room");
        const uint64_t scratch_capacity = scratch.capacity;
        uint32_t refusals = 0;
        bool accepted = false;
        for (uint64_t room = 0; allocated && floating && !accepted && room <= 4096; room += 4)
        {
            CheckInput copy = input;
            copy.literal_tables = NULL;
            copy.literal_tables_last = NULL;
            copy.literal_table_count = 0;
            copy.resource = false;
            rollback.length = 17;
            reader.output = &copy;
            reader.arena = &rollback;
            reader.declared_types = input.declared_types;
            reader.initializer_declarations = declarations;
            reader.ignored = ignored;
            reader.function_body = BUSTER_CHECK_NONE;
            reader.query_types = query_types;
            reader.linked_types = linked_types;
            reader.declaration_bits = input.declaration_bits;
            const uint64_t before = scratch.length;
            scratch.capacity = before + room;
            accepted = check_input_literal_table_prepare(&reader, begin - 1);
            scratch.capacity = scratch_capacity;
            bool unchanged = !accepted && copy.resource && rollback.length == 17 && scratch.length == before && copy.literal_tables == NULL &&
                             copy.literal_table_count == 0;
            for (uint32_t token = 0; unchanged && token <= parser.tokens.count; token += 1)
            {
                unchanged = ignored[token] == 0x9d;
            }
            refusals += !accepted;
            check_test_expect(test, accepted ? copy.literal_table_count == 1 && !copy.resource && scratch.length == before : unchanged,
                              "a floating table's scratch refusal restores arenas and publishes nothing, and enough room accepts it");
        }
        check_test_expect(test, !floating || (accepted && refusals != 0), "a floating table's scratch is refused, then accepted with room");
        arena_pop_to(&scratch, scratch_mark);
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    check_test_types_close(&types);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&rollback);
    arena_release(&scratch);
    arena_release(&output);
}

// Compact plans and ordinary string/scalar plans must share the sparse declaration index in source order.
static void check_test_literal_table_order(CheckTest* test)
{
    char table[8192];
    char source[32768];
    const bool made = check_test_literal_table_source(table, sizeof(table), "const unsigned char", 129, 0, true);
    const int used = made ? snprintf(source, sizeof(source),
                                    "int before=7;\n#define table first\n%s\n#undef table\n"
                                    "char middle[]=\"three\";\n#define table second\n%s\n#undef table\n"
                                    "const unsigned short after=19;\n", table, table) : -1;
    bool ready = used > 0 && (uint64_t)used < sizeof(source);
    check_test_expect(test, ready, "mixed compact/ordinary initializer source fits");
    uint8_t expected[5][256];
    uint64_t sizes[5] = { 0 };
    for (uint32_t path = 0; ready && path < 3; path += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        CheckTestTypes types = { 0 };
        Arena output = arena_reserve(UINT64_C(1) << 27);
        Arena scratch = arena_reserve(UINT64_C(1) << 27);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parsing = { .scalar = path == 1 };
        const CheckOptions checking = { .scalar = path == 1 };
        bool valid = output.base != NULL && scratch.base != NULL && intern_initialize(&identifiers) &&
                     pp_initialize(&pp, &preprocessing, &identifiers) && parse_initialize(&parser, &parsing, preprocessing.language) &&
                     pp_run_source(&pp, "literal-table-order.c", (const uint8_t*)source, strlen(source)) && parse_translation_unit(&parser, &pp) &&
                     check_test_types_open(&types);
        valid = valid && (path == 0 ? check_input_tables_reference(&input, &parser, &pp, &types.table, &output, &scratch) :
                                     check_input(&input, &parser, &pp, &types.table, &output, &scratch)) &&
                check_graph_run(&types.table, &input.graph, &output, &scratch, &checking, &stream) &&
                check_initializers_run(&plans, &input, &stream, &parser, &types.table, &output, &scratch) && plans.count == 5 &&
                input.literal_table_count == (path == 0 ? 0u : 2u);
        check_test_expect(test, valid, "mixed literal tables and ordinary initializers publish five plans");
        for (uint32_t at = 0; valid && at < plans.count; at += 1)
        {
            uint8_t bytes[sizeof(expected[0])];
            const CheckInitializer* plan = plans.plans + at;
            valid = plan->object_size <= sizeof(bytes) && check_init_object_bytes(&types.table, plan, bytes, plan->object_size) &&
                    check_initializers_plan_index(&plans, plans.declaration_tokens[at]) == at + 1 &&
                    (at == 0 || plans.declaration_tokens[at - 1] < plans.declaration_tokens[at]);
            if (valid && path == 0)
            {
                sizes[at] = plan->object_size;
                memcpy(expected[at], bytes, sizes[at]);
            }
            else if (valid)
            {
                valid = plan->object_size == sizes[at] && memcmp(expected[at], bytes, sizes[at]) == 0;
            }
        }
        for (uint32_t token = 0; valid && token <= parser.tokens.count; token += 1)
        {
            uint32_t expected_index = 0;
            for (uint32_t at = 0; at < plans.count; at += 1)
            {
                expected_index = plans.declaration_tokens[at] == token ? at + 1 : expected_index;
            }
            valid = check_initializers_plan_index(&plans, token) == expected_index;
        }
        check_test_expect(test, valid && check_initializers_plan_index(&plans, UINT32_MAX) == 0 && scratch.length == 0,
                          "source-ordered compact/ordinary plan bytes and sparse present/absent lookups equal the dense source oracle");
        check_test_types_close(&types);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        arena_release(&output);
        arena_release(&scratch);
    }
}

#include "c_check_literal_table_floating_test.c"

static void check_test_literal_tables(CheckTest* test, const char* clang)
{
    check_test_literal_table_shapes(test);
    check_test_literal_table_contract(test, "unsigned char");
    check_test_literal_table_contract(test, "float");
    check_test_literal_table_contract(test, "double");
    check_test_literal_table_order(test);
    char source[32768];
    static const uint32_t counts[] = { 127, 128, 129, 255, 256, 257 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(counts); i += 1)
    {
        const bool made = check_test_literal_table_source(source, sizeof(source), "const unsigned char", counts[i], 0, i % 2 == 0);
        check_test_expect(test, made, "literal table fixture fits its source buffer");
        if (made)
        {
            check_test_literal_table_case(test, source, counts[i] >= BUSTER_CHECK_LITERAL_TABLE_MINIMUM, false);
        }
    }
    static const char* const types[] = { "signed char", "short", "unsigned short", "int", "unsigned", "long", "unsigned long",
                                       "double", "volatile unsigned char", "_Bool", "__int128", "unsigned char", "unsigned char", "unsigned char" };
    static const uint32_t forms[] = { 1, 1, 0, 1, 0, 2, 3, 4, 0, 0, 0, 5, 6, 7 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(types); i += 1)
    {
        const bool made = check_test_literal_table_source(source, sizeof(source), types[i], 129, forms[i], true);
        check_test_expect(test, made, "typed literal table fixture fits its source buffer");
        if (made)
        {
            check_test_literal_table_case(test, source, i < 8, false);
        }
    }
    {
        char table_source[8192];
        const bool made = check_test_literal_table_source(table_source, sizeof(table_source), "const unsigned char", 129, 0, false);
        const int used = made ? snprintf(source, sizeof(source),
                                        "int before(int n){struct P{long x;int y;};return n+(int)sizeof(struct P);}\n%s", table_source) : -1;
        check_test_expect(test, used > 0 && (uint64_t)used < sizeof(source), "deferred type replay source fits");
        if (used > 0 && (uint64_t)used < sizeof(source))
        {
            check_test_literal_table_case(test, source, true, true);
        }
    }
    for (uint32_t form = 0; form < 3; form += 1)
    {
        const char* type = form == 0 ? "const unsigned char" : form == 1 ? "const long" : "unsigned short";
        const uint32_t spelling = form == 1 ? 2 : 0;
        bool made = check_test_literal_table_source(source, sizeof(source), type, 129, spelling, true);
        const uint64_t length = strlen(source);
        const int appended = made ? snprintf(source + length, sizeof(source) - length,
                                            "int tested(int n){return (int)table[(unsigned)n%%129]+(int)table[131]+(int)sizeof table;}") : -1;
        made = made && appended > 0 && (uint64_t)appended < sizeof(source) - length;
        check_test_expect(test, made, "literal table native fixture fits");
        if (made)
        {
            check_test_program_source(test, source, "literal-table-native", clang);
        }
    }
    check_test_literal_table_rows_shapes(test);
    {
        typedef struct
        {
            const char* type;
            uint32_t rows;
            uint32_t columns;
            uint32_t form;
            bool compact;
        } RowsCase;
        static const RowsCase cases[] = {
            { "const unsigned char", 33, 4, 0, true },  { "const unsigned char", 32, 4, 0, true },  { "const unsigned char", 31, 4, 0, false },
            { "int", 43, 3, 1, true },                  { "unsigned short", 9, 16, 0, true },       { "long", 129, 1, 1, true },
            { "signed char", 26, 5, 1, true },          { "unsigned long long", 17, 8, 0, true },  { "double", 33, 4, 2, true },
            { "const float", 22, 6, 2, true },          { "int", 33, 4, 3, false },                 { "int", 33, 4, 4, false },
            { "int", 33, 4, 5, false },                 { "int", 33, 4, 6, false },                 { "int", 33, 4, 7, false },
            { "volatile int", 33, 4, 0, false },        { "_Bool", 33, 4, 0, false },               { "double", 33, 4, 3, false },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            for (uint32_t sized = 0; sized < 2; sized += 1)
            {
                const RowsCase* row = cases + i;
                const bool made = check_test_literal_rows_source(source, sizeof(source), row->type, row->rows, row->columns, row->form, sized != 0);
                check_test_expect(test, made, "literal rows fixture fits its source buffer");
                if (made)
                {
                    check_test_literal_table_case(test, source, row->compact, false);
                }
            }
        }
        {
            char rows_source[8192];
            const bool made = check_test_literal_rows_source(rows_source, sizeof(rows_source), "const unsigned char", 33, 4, 0, false);
            const int used = made ? snprintf(source, sizeof(source),
                                            "int before(int n){struct P{long x;int y;};return n+(int)sizeof(struct P);}\n%s", rows_source) : -1;
            check_test_expect(test, used > 0 && (uint64_t)used < sizeof(source), "deferred rows type replay source fits");
            if (used > 0 && (uint64_t)used < sizeof(source))
            {
                check_test_literal_table_case(test, source, true, true);
            }
        }
        for (uint32_t form = 0; form < 3; form += 1)
        {
            const char* type = form == 0 ? "const unsigned char" : form == 1 ? "int" : "double";
            bool made = check_test_literal_rows_source(source, sizeof(source), type, 43, 3, form, true);
            const uint64_t length = strlen(source);
            const int appended = made ? snprintf(source + length, sizeof(source) - length,
                                                "int tested(int n){return (int)table[(unsigned)n%%43][(unsigned)n%%3]+(int)table[45][2]+(int)sizeof table+"
                                                "(int)sizeof table[0];}") : -1;
            made = made && appended > 0 && (uint64_t)appended < sizeof(source) - length;
            check_test_expect(test, made, "literal rows native fixture fits");
            if (made)
            {
                check_test_program_source(test, source, "literal-rows-native", clang);
            }
        }
    }
    check_test_literal_floating_tables(test, clang);
}
