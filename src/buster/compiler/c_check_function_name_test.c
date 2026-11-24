// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original predefined-name fixtures. Clang's constant string comparisons check the exact bytes independently;
// both production parser/graph paths check retained const-array objects, caching and source identities.

#define BUSTER_CHECK_FUNCTION_NAME_TEST_IMPLEMENTATION 1

typedef struct CheckFunctionNameFixture CheckFunctionNameFixture;
struct CheckFunctionNameFixture
{
    const char* label;
    const char* source;
    const char* name;
    const char* pretty;
};

static void check_test_function_name_storage(CheckTest* test, const Parser* parser, Preprocessor* pp, TypeTable* types, const CheckInput* input)
{
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    Arena columns = arena_reserve(UINT64_C(1) << 16);
    uint32_t* specifiers = arena_push_array(&columns, parser->tokens.count + 1, sizeof(uint32_t), 4);
    bool ready = arena.base != 0 && scratch.base != 0 && specifiers != 0;
    uint32_t body = BUSTER_CHECK_NONE;
    uint32_t token = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < parser->tokens.count; i += 1)
    {
        body = parser->tree.roles[i] == PARSE_ROLE_OPEN_BODY ? i : body;
        token = parser->tokens.names[i] == C_NAME_PRETTY_FUNCTION ? i : token;
    }
    ready &= body != BUSTER_CHECK_NONE && token != BUSTER_CHECK_NONE;
    check_test_expect(test, ready, "predefined-name exhaustion fixture setup");
    if (ready)
    {
        memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
        memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
        const uint64_t output_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t used = 0;
        bool scratch_succeeded = false;
        for (uint32_t pass = 0; pass < 3; pass += 1)
        {
            const uint64_t maximum = pass == 0 ? 1 : pass == 1 ? used - 31 : scratch_capacity;
            for (uint64_t limit = 0; limit < maximum && !(pass == 2 && scratch_succeeded); limit += pass == 2 ? 37 : 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : output_capacity;
                scratch.length = 29;
                scratch.capacity = pass == 2 ? 29 + limit : scratch_capacity;
                // A fresh column of string ids each time, from the same place in `columns`.
                const uint64_t mark = columns.length;
                CheckSparseColumn ids = { 0 };
                CheckSparsePool* pool = NULL;
                const bool reserved = check_sparse_reserve(&columns, &pool, &ids, 1, parser->tokens.count, false, false);
                check_test_expect(test, reserved, "predefined-name exhaustion fixture has room for its string ids");
                memset(specifiers, 0xff, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
                specifiers[body] = 0;
                CheckInitString strings[3] = { 0 };
                CheckInputReport reports[4];
                CheckInput output =
                {
                    .sparse_pool = pool,
                    .body_declarations = input->body_declarations,
                    .strings = strings,
                    .reports = reports,
                    .report_capacity = BUSTER_ARRAY_LENGTH(reports),
                };
                CheckInputReader reader =
                {
                    .output = &output,
                    .parser = parser,
                    .pp = pp,
                    .types = types,
                    .arena = &arena,
                    .scratch = &scratch,
                    .declared_types = input->declared_types,
                    .string_ids = ids,
                    .function_specifiers = specifiers,
                    .function_body = body,
                    .function_name_body = BUSTER_CHECK_NONE,
                    .function_name_ids = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE },
                    .file_function_name_ids = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE },
                };
                const CheckInitString* string = check_input_function_name(&reader, token);
                if (string != 0)
                {
                    check_test_expect(test, !output.resource && output.report_count == 0 && output.string_count == 1 &&
                                      string->byte_count == 12 && memcmp(string->bytes, "int f(void)", 12) == 0 && scratch.length == 29,
                                      "predefined printer ignores poisoned scratch and restores its mark");
                    used = arena.length;
                    reader.function_body = BUSTER_CHECK_NONE;
                    check_test_expect(test, check_input_function_name(&reader, token) == string && arena.length == used && output.string_count == 1,
                                      "cached predefined token survives later source-context changes");
                    scratch_succeeded |= pass == 2;
                }
                else
                {
                    check_test_expect(test, pass != 0 && output.resource && output.string_count == 0 && check_sparse_get(ids, token) == BUSTER_CHECK_NONE &&
                                      arena.length == 31 && scratch.length == 29,
                                      "predefined array exhaustion rolls back bytes without publishing an object");
                }
                arena_pop_to(&columns, mark);
            }
        }
        check_test_expect(test, scratch_succeeded, "predefined printer recovers after scratch exhaustion");
        arena.capacity = output_capacity;
        scratch.capacity = scratch_capacity;
    }
    arena_release(&columns);
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_function_name_fixture(CheckTest* test, const CheckFunctionNameFixture* fixture)
{
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 22);
        Arena output_arena = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 22);
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parse_options = { .scalar = variant == 0 };
        char source[8192];
        const int length = snprintf(source, sizeof(source), "#define NAMES (void)__func__; (void)__FUNCTION__; (void)__PRETTY_FUNCTION__; (void)__func__; (void)__FUNCTION__; (void)__PRETTY_FUNCTION__\n%s", fixture->source);
        bool ready = length > 0 && length < (int)sizeof(source) && input_arena.base != 0 && output_arena.base != 0 && scratch.base != 0 &&
                     intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parse_options, options.language) && check_test_types_open(&types);
        ready = ready && pp_run_source(&pp, "function-names.c", (const uint8_t*)source, (uint32_t)length) && parse_translation_unit(&parser, &pp);
        CheckInput input = { 0 };
        const bool adapted = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch);
        CheckStream stream = { 0 };
        const CheckOptions check_options = { 0 };
        const bool checked = adapted && check_graph_run(&types.table, &input.graph, &output_arena, &scratch, &check_options, &stream);
        char description[256];
        snprintf(description, sizeof(description), "%s: predefined arrays survive source and graph checking", fixture->label);
        check_test_expect(test, checked && input.report_count == 0 && stream.report_count == 0, description);
        if (!checked || input.report_count != 0 || stream.report_count != 0)
        {
            fprintf(stderr, "function names %s: ready %u adapted %u checked %u source %u graph %u\n", fixture->label, ready, adapted, checked, input.report_count, stream.report_count);
            for (uint32_t i = 0; i < input.report_count; i += 1)
            {
                const uint32_t message = input.reports[i].message;
                // The positional table's first generic entries are aligned; later report kinds need not have an entry.
                const char* text = message <= CHECK_INPUT_CONSTANT_INITIALIZER && message < BUSTER_ARRAY_LENGTH(check_input_messages) ?
                    check_input_messages[message] : "unlisted semantic diagnostic";
                fprintf(stderr, "  token %u: %s (kind %u)\n", input.reports[i].token, text, message);
            }
        }
        uint32_t found[3] = { 0 };
        for (uint32_t i = 0; i < input.string_count; i += 1)
        {
            const CheckInitString* string = &input.strings[i];
            if (check_input_function_name_token(&parser, string->token))
            {
                const uint32_t name = parser.tokens.names[string->token];
                const uint32_t kind = name == C_NAME_FUNC ? 0 : name == C_NAME_FUNCTION ? 1 : 2;
                const char* expected = kind == 2 ? fixture->pretty : fixture->name;
                const uint64_t count = strlen(expected) + 1;
                found[kind] += 1;
                snprintf(description, sizeof(description), "%s: exact %s bytes", fixture->label, kind == 2 ? "pretty" : "function");
                const bool equal = string->byte_count == count && memcmp(string->bytes, expected, count) == 0;
                check_test_expect(test, equal, description);
                if (!equal)
                {
                    fprintf(stderr, "function names %s: expected '%s', got '%.*s'\n", fixture->label, expected, (int)string->byte_count, string->bytes);
                }
                check_test_expect(test, type_kind(&types.table, string->type) == BUSTER_TYPE_KIND_ARRAY &&
                                  types.table.types.extent[type_index(string->type)] == count &&
                                  type_element(&types.table, string->type) == type_qualify(&types.table, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST),
                                  "predefined names retain const char array type and terminator extent");
                check_test_expect(test, string->object == string->token + 1, "predefined name object retains its first source token");
            }
        }
        check_test_expect(test, found[0] == 1 && found[1] == 1 && found[2] == 1, "repeated predefined names reuse exactly one array of each kind");
        uint32_t leaves[3] = { 0 };
        uint32_t objects[3] = { 0 };
        for (uint32_t i = 0; i < input.graph.count; i += 1)
        {
            const uint32_t token = input.graph.tokens[i];
            if (input.graph.ops[i] == CHECK_OP_VALUE && check_input_function_name_token(&parser, token))
            {
                const uint32_t name = parser.tokens.names[token];
                const uint32_t kind = name == C_NAME_FUNC ? 0 : name == C_NAME_FUNCTION ? 1 : 2;
                const CheckValue value = check_graph_leaf_value(&input.graph, i);
                check_test_expect(test, value.category == CHECK_LVALUE && value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                                  (value.flags & BUSTER_CHECK_STATIC_OBJECT) != 0 && (objects[kind] == 0 || objects[kind] == value.object),
                                  "predefined array leaves have stable static object identity");
                objects[kind] = value.object;
                leaves[kind] += 1;
            }
        }
        check_test_expect(test, leaves[0] == 2 && leaves[1] == 2 && leaves[2] == 2, "each predefined source operand retains an expression leaf");
        if (checked && strcmp(fixture->label, "plain prototype") == 0)
        {
            check_test_function_name_storage(test, &parser, &pp, &types.table, &input);
        }
        arena_release(&scratch);
        arena_release(&output_arena);
        arena_release(&input_arena);
        check_test_types_close(&types);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
}

static void check_test_function_name_semantics(CheckTest* test, const char* clang)
{
    CheckTestFixtures catalog;
    const uint32_t file_count = check_test_private_catalog(test, &catalog, "checker/function-name-semantics", NULL, 0, 0);
    const CheckSourceFixture* fixtures = catalog.cases;
    CheckTestOracleBatch batch;
    const bool allocated = check_test_oracle_batch(test, &batch, &catalog.storage, file_count);
    char directory[] = "/tmp/buster-function-name-semantics-XXXXXX";
    const bool created = allocated && mkdtemp(directory) != NULL;
    bool ready = created;
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        batch.pointers[i] = batch.paths[i];
        ready = parse_test_write(batch.paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17", "-fsyntax-only", "-w" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), batch.pointers, file_count, batch.outcomes);
    check_test_expect(test, ready, "predefined-name semantic Clang oracle runs");
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        check_test_expect(test, batch.outcomes[i].rejected == fixtures[i].rejected, "predefined-name semantic expectation agrees with Clang");
        check_test_source(test, &fixtures[i], batch.paths[i], &batch.outcomes[i]);
    }
    for (uint32_t i = 0; created && i < file_count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        unlink(batch.paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
}

// Octal escapes preserve the expected bytes when the Clang oracle embeds them in its string assertions.
static const char* check_test_function_name_literal(Arena* storage, const char* text)
{
    const size_t length = strlen(text);
    char* escaped = arena_push(storage, 4 * length + 1, 1);
    if (escaped != NULL)
    {
        char* out = escaped;
        for (size_t i = 0; i < length; i += 1)
        {
            const uint8_t byte = (uint8_t)text[i];
            if (byte >= 32 && byte <= 126 && byte != '"' && byte != '\\' && byte != '?')
            {
                *out++ = (char)byte;
            }
            else
            {
                *out++ = '\\';
                *out++ = (char)('0' + (byte >> 6));
                *out++ = (char)('0' + ((byte >> 3) & 7));
                *out++ = (char)('0' + (byte & 7));
            }
        }
        *out = 0;
    }
    return escaped;
}

static __attribute__((noinline)) void check_test_function_names(CheckTest* test, const char* clang)
{
    check_test_function_name_semantics(test, clang);
    char deep_source[512];
    char deep_pretty[512];
    const uint32_t depth = 128;
    memcpy(deep_source, "typedef int I; I ", 17);
    memset(deep_source + 17, '*', depth);
    memcpy(deep_source + 17 + depth, "f(void){NAMES;return 0;}\n", sizeof("f(void){NAMES;return 0;}\n"));
    memcpy(deep_pretty, "I ", 2);
    memset(deep_pretty + 2, '*', depth);
    memcpy(deep_pretty + 2 + depth, "f(void)", 8);
    CheckTestFixtures catalog;
    bool loaded = check_test_fixtures_load_bound(&catalog, "checker/function-name-objects", NULL, 0, true);
    const uint32_t count = loaded ? catalog.count + 1 : 0;
    CheckFunctionNameFixture* fixtures = arena_push(&catalog.storage, sizeof(*fixtures) * count, 8);
    loaded = loaded && fixtures != NULL;
    for (uint32_t i = 0; loaded && i < catalog.count; i += 1)
    {
        const CheckSourceFixture* row = catalog.cases + i;
        char* name = catalog.expected[i];
        char* newline = strchr(name, '\n');
        char* end = newline != NULL ? strchr(newline + 1, '\n') : NULL;
        loaded = name != newline && end != NULL && end > newline + 1 && end[1] == 0 &&
                 !row->rejected && !row->unsupported && !row->extension && row->standard == 0 && !row->char_unsigned && !row->undecided &&
                 row->capacity == NULL && row->overload_tables == 0;
        if (loaded)
        {
            *newline = 0;
            *end = 0;
            fixtures[i] = (CheckFunctionNameFixture){ row->name, row->source, name, newline + 1 };
        }
        else
        {
            snprintf(catalog.problem, sizeof(catalog.problem), "%s requires no flags and two lines of expected names", catalog.files[i]);
        }
    }
    check_test_expect(test, loaded, catalog.problem);
    if (loaded)
    {
        fixtures[catalog.count] = (CheckFunctionNameFixture){ "deep pointer return grows explicit printer frames", deep_source, "f", deep_pretty };
    }
    CheckTestOracleBatch batch;
    char directory[] = "/tmp/buster-function-names-XXXXXX";
    const bool created = loaded && check_test_oracle_batch(test, &batch, &catalog.storage, count) && mkdtemp(directory) != NULL;
    bool ready = created;
    for (uint32_t i = 0; ready && i < count; i += 1)
    {
        char source[8192];
        const CheckFunctionNameFixture* fixture = &fixtures[i];
        const char* name = check_test_function_name_literal(&catalog.storage, fixture->name);
        const char* pretty = check_test_function_name_literal(&catalog.storage, fixture->pretty);
        const int length = name == NULL || pretty == NULL ? -1 : snprintf(source, sizeof(source),
            "#define NAMES _Static_assert(__builtin_strcmp(__func__,\"%s\")==0,\"func\"); "
            "_Static_assert(__builtin_strcmp(__FUNCTION__,\"%s\")==0,\"function\"); "
            "_Static_assert(__builtin_strcmp(__PRETTY_FUNCTION__,\"%s\")==0,\"pretty\"); "
            "_Static_assert(__builtin_types_compatible_p(__typeof__(__PRETTY_FUNCTION__),const char[%zu]),\"array\")\n%s",
            name, name, pretty, strlen(fixture->pretty) + 1, fixture->source);
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        batch.pointers[i] = batch.paths[i];
        ready = length > 0 && length < (int)sizeof(source) && parse_test_write(batch.paths[i], source, (uint32_t)length);
    }
    const char* arguments[] = { "-std=gnu17", "-fsyntax-only", "-w" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), batch.pointers, count, batch.outcomes);
    check_test_expect(test, ready, "predefined-name Clang exact-byte oracle runs");
    for (uint32_t i = 0; ready && i < count; i += 1)
    {
        char description[256];
        snprintf(description, sizeof(description), "%s: exact bytes and array type match Clang", fixtures[i].label);
        check_test_expect(test, !batch.outcomes[i].rejected, description);
        check_test_function_name_fixture(test, &fixtures[i]);
    }
    for (uint32_t i = 0; created && i < count; i += 1)
    {
        snprintf(batch.paths[i], sizeof(batch.paths[i]), "%s/%u.c", directory, i);
        unlink(batch.paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
}
