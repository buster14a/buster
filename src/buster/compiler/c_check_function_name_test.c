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
    uint32_t* ids = arena_push_array(&columns, parser->tokens.count + 1, sizeof(uint32_t), 4);
    uint32_t* specifiers = arena_push_array(&columns, parser->tokens.count + 1, sizeof(uint32_t), 4);
    bool ready = arena.base != 0 && scratch.base != 0 && ids != 0 && specifiers != 0;
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
                memset(ids, 0xff, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
                memset(specifiers, 0xff, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
                specifiers[body] = 0;
                CheckInitString strings[3] = { 0 };
                CheckInputReport reports[4];
                CheckInput output =
                {
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
                    check_test_expect(test, pass != 0 && output.resource && output.string_count == 0 && ids[token] == BUSTER_CHECK_NONE &&
                                      arena.length == 31 && scratch.length == 29,
                                      "predefined array exhaustion rolls back bytes without publishing an object");
                }
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
        const CheckOptions check_options = { .scalar = variant == 0 };
        const bool checked = adapted && check_graph_run(&types.table, &input.graph, &output_arena, &scratch, &check_options, &stream);
        char description[256];
        snprintf(description, sizeof(description), "%s: predefined arrays survive source and graph checking", fixture->label);
        check_test_expect(test, checked && input.report_count == 0 && stream.report_count == 0, description);
        if (!checked || input.report_count != 0 || stream.report_count != 0)
        {
            fprintf(stderr, "function names %s: ready %u adapted %u checked %u source %u graph %u\n", fixture->label, ready, adapted, checked, input.report_count, stream.report_count);
            for (uint32_t i = 0; i < input.report_count; i += 1)
            {
                fprintf(stderr, "  token %u: %s\n", input.reports[i].token, check_input_messages[input.reports[i].message]);
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
                const CheckValue value = *check_graph_leaf_of(&input.graph, i);
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
    const CheckSourceFixture fixtures[] =
    {
        {
            .name = "predefined arrays participate in sizeof and typeof queries",
            .source = "int f(void){_Static_assert(sizeof __func__==2,\"func\");_Static_assert(sizeof __FUNCTION__==2,\"function\");_Static_assert(sizeof __PRETTY_FUNCTION__==12,\"pretty\");_Static_assert(__builtin_types_compatible_p(__typeof__(__func__),const char[2]),\"type\");return 0;}\n",
        },
        {
            .name = "pretty alias size is evaluated at declaration position",
            .source = "typedef int I; I f(I x){_Static_assert(sizeof __PRETTY_FUNCTION__==7,\"pretty\");return x;}\n",
        },
        {
            .name = "file scope predefined array lengths",
            .source = "_Static_assert(sizeof __func__==1,\"func\");_Static_assert(sizeof __FUNCTION__==1,\"function\");_Static_assert(sizeof __PRETTY_FUNCTION__==10,\"pretty\");\n",
        },
        {
            .name = "file scope cache survives intervening function",
            .source = "const char *before=__PRETTY_FUNCTION__; int f(void){return sizeof __PRETTY_FUNCTION__;} const char *after=__PRETTY_FUNCTION__; _Static_assert(sizeof __func__==1,\"file\");\n",
        },
        {
            .name = "predefined static array addresses initialize static pointers",
            .source = "const char *f(void){static const char *p=__func__;return p;}\n",
        },
        {
            .name = "address of predefined array retains array extent",
            .source = "int f(void){const char (*p)[2]=&__func__;return (*p)[0];}\n",
        },
        {
            .name = "predefined array characters are const",
            .source = "int f(void){\n__func__[0]='x';\nreturn 0;}\n",
            .rejected = true,
        },
        {
            .name = "pretty array characters are const",
            .source = "int f(void){\n__PRETTY_FUNCTION__[0]='x';\nreturn 0;}\n",
            .rejected = true,
        },
        {
            .name = "predefined arrays are not assignable",
            .source = "int f(void){\n__func__=0;\nreturn 0;}\n",
            .rejected = true,
        },
        {
            .name = "predefined array declaration names cannot be replaced",
            .source = "int f(void){\nint __func__;\nreturn 0;}\n",
            .rejected = true,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-function-name-semantics-XXXXXX";
    char paths[FILE_COUNT][256];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17", "-fsyntax-only", "-w" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "predefined-name semantic Clang oracle runs");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, "predefined-name semantic expectation agrees with Clang");
        check_test_source(test, &fixtures[i], paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (ready)
    {
        rmdir(directory);
    }
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
    const CheckFunctionNameFixture fixtures[] =
    {
        {
            .label = "plain prototype",
            .source = "int f(void){NAMES;return 0;}\n",
            .name = "f",
            .pretty = "int f(void)",
        },
        {
            .label = "typedef return and parameter spelling",
            .source = "typedef int I; I f(I x){NAMES;return x;}\n",
            .name = "f",
            .pretty = "I f(I)",
        },
        {
            .label = "canonical scalar spelling and parameter qualifiers",
            .source = "static inline unsigned long int f(signed int x, volatile const int y){NAMES;return x+y;}\n",
            .name = "f",
            .pretty = "unsigned long f(int, const volatile int)",
        },
        {
            .label = "qualified pointer typedef and explicit pointers",
            .source = "typedef int I; typedef int *P; P f(const P p,I *q,const I *r,I *const s){NAMES;return p;}\n",
            .name = "f",
            .pretty = "P f(const P, I *, const I *, I *const)",
        },
        {
            .label = "array parameter adjustment drops outer alias",
            .source = "typedef int A[4]; int f(A a,int b[const static 4],const int c[3]){NAMES;return 0;}\n",
            .name = "f",
            .pretty = "int f(int *, int *const, const int *)",
        },
        {
            .label = "nested callbacks retain occurrence-specific aliases",
            .source = "typedef int I; typedef int F(int); int f(F a,F *b,I (*c)(I),I (*(*d)(I))(I)){NAMES;return 0;}\n",
            .name = "f",
            .pretty = "int f(F *, F *, I (*)(I), I (*(*)(I))(I))",
        },
        {
            .label = "callback unnamed qualifiers and pointer to array alias",
            .source = "typedef int I; typedef int A[4]; typedef int *P; void f(int (*cb)(const int,const I,I *const,const P),A *p,const A *q){NAMES;}\n",
            .name = "f",
            .pretty = "void f(int (*)(const int, const I, I *const, const P), A *, const A *)",
        },
        {
            .label = "function returning function pointer",
            .source = "int (*f(void))(int){NAMES;return 0;}\n",
            .name = "f",
            .pretty = "int (*f(void))(int)",
        },
        {
            .label = "function returning array pointer",
            .source = "int (*f(void))[4]{NAMES;return 0;}\n",
            .name = "f",
            .pretty = "int (*f(void))[4]",
        },
        {
            .label = "distinct return and nested parameter typedef occurrences",
            .source = "typedef int I; typedef int J; I (*f(J (*x)(I)))(J){NAMES;return 0;}\n",
            .name = "f",
            .pretty = "I (*f(J (*)(I)))(J)",
        },
        {
            .label = "variadic alias",
            .source = "typedef int I; int f(I x,...){NAMES;return x;}\n",
            .name = "f",
            .pretty = "int f(I, ...)",
        },
        {
            .label = "unprototyped definition",
            .source = "void f(){NAMES;}\n",
            .name = "f",
            .pretty = "void f()",
        },
        {
            .label = "record tag names",
            .source = "struct S{int x;}; void f(struct S s){NAMES;}\n",
            .name = "f",
            .pretty = "void f(struct S)",
        },
        {
            .label = "typedef already contains const",
            .source = "typedef const int CI; CI f(CI x,volatile CI y){NAMES;return x;}\n",
            .name = "f",
            .pretty = "CI f(CI, volatile CI)",
        },
        {
            .label = "typeof type and expression source spelling",
            .source = "typedef int I; typeof(I) f(typeof(I) x,typeof(1) y){NAMES;return x;}\n",
            .name = "f",
            .pretty = "typeof(I) f(typeof(I), typeof (1))",
        },
        {
            .label = "atomic types",
            .source = "_Atomic(int) f(_Atomic(int) x){NAMES;return 0;}\n",
            .name = "f",
            .pretty = "_Atomic(int) f(_Atomic(int))",
        },
        {
            .label = "typeof expression operator formatting",
            .source = "int x; typeof(sizeof x) f(typeof(1?2:3) y){NAMES;return y;}\n",
            .name = "f",
            .pretty = "typeof (sizeof x) f(typeof (1 ? 2 : 3))",
        },
        {
            .label = "attributes do not become signature suffixes",
            .source = "__attribute__((visibility(\"hidden\"))) int f(int x) __attribute__((nonnull)){NAMES;return x;}\n",
            .name = "f",
            .pretty = "int f(int)",
        },
        {
            .label = "macro-produced function identifier",
            .source = "#define JOIN(a,b) a##b\nint JOIN(macro_,function)(void){NAMES;return 0;}\n",
            .name = "macro_function",
            .pretty = "int macro_function(void)",
        },
        {
            .label = "body typedef shadow does not alter header spelling",
            .source = "typedef int I; I f(I x){typedef long I;NAMES;return x;}\n",
            .name = "f",
            .pretty = "I f(I)",
        },
        {
            .label = "deep pointer return grows explicit printer frames",
            .source = deep_source,
            .name = "f",
            .pretty = deep_pretty,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-function-names-XXXXXX";
    char paths[FILE_COUNT][256];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char source[8192];
        const CheckFunctionNameFixture* fixture = &fixtures[i];
        const int length = snprintf(source, sizeof(source),
            "#define NAMES _Static_assert(__builtin_strcmp(__func__,\"%s\")==0,\"func\"); "
            "_Static_assert(__builtin_strcmp(__FUNCTION__,\"%s\")==0,\"function\"); "
            "_Static_assert(__builtin_strcmp(__PRETTY_FUNCTION__,\"%s\")==0,\"pretty\"); "
            "_Static_assert(__builtin_types_compatible_p(__typeof__(__PRETTY_FUNCTION__),const char[%zu]),\"array\")\n%s",
            fixture->name, fixture->name, fixture->pretty, strlen(fixture->pretty) + 1, fixture->source);
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = length > 0 && length < (int)sizeof(source) && parse_test_write(paths[i], source, (uint32_t)length);
    }
    const char* arguments[] = { "-std=gnu17", "-fsyntax-only", "-w" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "predefined-name Clang exact-byte oracle runs");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        char description[256];
        snprintf(description, sizeof(description), "%s: exact bytes and array type match Clang", fixtures[i].label);
        check_test_expect(test, !outcomes[i].rejected, description);
        check_test_function_name_fixture(test, &fixtures[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (ready)
    {
        rmdir(directory);
    }
}
