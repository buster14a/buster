// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source/metadata tests. The installed Clang independently checks the handwritten source fixtures;
// helper-only tests deliberately retain malformed argument counts for the ordinary call checker and distinguish
// custom/ignored/unrepresented metadata from an ordinary prototype. No helper result claims feature eligibility.

#define BUSTER_CHECK_INTRINSIC_INPUT_TEST_IMPLEMENTATION 1

typedef struct CheckIntrinsicInputFixture CheckIntrinsicInputFixture;
struct CheckIntrinsicInputFixture
{
    const char* name;
    const char* source;
    uint32_t declarations;
    uint32_t calls;
    uint32_t failed;
    uint32_t arguments[32];
    bool rejected;
    bool ordinary_shadow;
};

static void check_test_intrinsic_input_storage(CheckTest* test, const Parser* parser, Preprocessor* pp, TypeTable* types, uint32_t keyword)
{
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    const bool ready = arena.base != 0 && scratch.base != 0;
    check_test_expect(test, ready, "intrinsic source storage fixture initializes");
    if (ready)
    {
        memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
        memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t output_required = 0;
        uint64_t scratch_required = 0;
        for (uint32_t pass = 0; pass < 3; pass += 1)
        {
            const uint64_t maximum = pass == 0 ? 1 : pass == 1 ? output_required + 1 : scratch_required + 1;
            for (uint64_t limit = 0; limit < maximum; limit += 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : arena_capacity;
                scratch.length = 29;
                scratch.capacity = pass == 2 ? 29 + limit : scratch_capacity;
                CheckInput output = { 0 };
                CheckInputReader reader =
                {
                    .output = &output,
                    .parser = parser,
                    .pp = pp,
                    .types = types,
                    .arena = &arena,
                    .scratch = &scratch,
                    .function_body = BUSTER_CHECK_NONE,
                };
                const bool initialized = check_input_intrinsic_initialize(&reader);
                if (initialized)
                {
                    output_required = arena.length - 31;
                    scratch_required = scratch.length - 29;
                    bool clean = reader.intrinsic_names != 0 && output.intrinsic_ids != 0 && output.intrinsic_declaration_count == 0 &&
                                 output.intrinsic_call_count == 0 && !output.resource;
                    for (uint32_t i = 0; clean && i <= parser->tokens.count; i += 1)
                    {
                        clean = output.intrinsic_ids[i] == 0;
                    }
                    for (uint32_t i = 0; clean && i < pp->table->count; i += 1)
                    {
                        clean = reader.intrinsic_names[i] == UINT32_MAX;
                    }
                    check_test_expect(test, clean, "intrinsic initialization overwrites poisoned active columns and sentinel");
                    if (pass == 0)
                    {
                        const uint64_t mark = arena.length;
                        arena.capacity = mark;
                        const CheckInputIntrinsicStatus failed = check_input_intrinsic_prepare(&reader, keyword);
                        check_test_expect(test, failed == CHECK_INPUT_INTRINSIC_FAILED && output.resource && output.intrinsic_call_count == 0 &&
                                          output.intrinsic_declaration_count == 0 && reader.intrinsic_names[parser->tokens.names[keyword]] == UINT32_MAX &&
                                          arena.length == mark,
                                          "argument storage exhaustion rolls back call, declaration and name-cache publication");
                        arena.capacity = arena_capacity;
                        output.resource = false;
                        const CheckInputIntrinsicStatus recovered = check_input_intrinsic_prepare(&reader, keyword);
                        check_test_expect(test, recovered == CHECK_INPUT_INTRINSIC_PREPARED && !output.resource && output.intrinsic_call_count == 1 &&
                                          output.intrinsic_declaration_count == 1,
                                          "an exact intrinsic retries successfully after bounded argument storage becomes available");
                    }
                }
                else
                {
                    check_test_expect(test, pass != 0 && output.resource && reader.intrinsic_names == 0 && output.intrinsic_ids == 0 &&
                                      output.intrinsic_declarations == 0 && output.intrinsic_declaration_capacity == 0 &&
                                      output.intrinsic_calls == 0 && output.intrinsic_call_capacity == 0 && arena.length == 31 && scratch.length == 29,
                                      "intrinsic initialization exhaustion rolls back every published field and both arenas");
                }
            }
        }
        uint8_t deferred[256];
        if (parser->tokens.count < BUSTER_ARRAY_LENGTH(deferred))
        {
            memset(deferred, 1, sizeof(deferred));
            arena.capacity = arena_capacity;
            scratch.capacity = scratch_capacity;
            arena.length = 31;
            scratch.length = 29;
            CheckInput output = { .deferred_tokens = deferred };
            CheckInputReader reader = { .output = &output, .parser = parser, .pp = pp, .types = types, .arena = &arena, .scratch = &scratch };
            const bool initialized = check_input_intrinsic_initialize(&reader);
            const CheckInputIntrinsicStatus status = check_input_intrinsic_prepare(&reader, keyword);
            check_test_expect(test, initialized && status == CHECK_INPUT_INTRINSIC_NOT_FOUND && !output.resource && reader.intrinsic_names == 0 &&
                              output.intrinsic_ids == 0 && arena.length == 31 && scratch.length == 29,
                              "validated deferred tokens produce no intrinsic candidates, storage or prepared call");
        }
        arena.capacity = arena_capacity;
        scratch.capacity = scratch_capacity;
    }
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_intrinsic_input_fixture(CheckTest* test, const CheckIntrinsicInputFixture* fixture, bool storage)
{
    uint64_t signatures[2] = { 0 };
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        CheckTestTypes types = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 20);
        Arena scratch = arena_reserve(UINT64_C(1) << 20);
        Arena bindings = arena_reserve(UINT64_C(1) << 20);
        const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parse_options = { .scalar = variant == 0 };
        bool ready = arena.base != 0 && scratch.base != 0 && bindings.base != 0 && intern_initialize(&identifiers) &&
                     pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parse_options, options.language) && check_test_types_open(&types);
        ready = ready && pp_run_source(&pp, "intrinsic-source.c", (const uint8_t*)fixture->source, (uint32_t)strlen(fixture->source)) &&
                parse_translation_unit(&parser, &pp);
        CheckInputReport reports[16];
        CheckInput output = { .reports = reports, .report_capacity = BUSTER_ARRAY_LENGTH(reports) };
        CheckInputReader reader =
        {
            .output = &output,
            .parser = &parser,
            .pp = &pp,
            .types = &types.table,
            .arena = &arena,
            .scratch = &scratch,
            .function_body = BUSTER_CHECK_NONE,
        };
        ready = ready && c_bindings_initialize(&reader.bindings, &bindings, identifiers.count, 16);
        char description[192];
        snprintf(description, sizeof(description), "%s: source and bindings initialize", fixture->name);
        check_test_expect(test, ready, description);
        if (ready)
        {
            if (fixture->ordinary_shadow)
            {
                const char spelling[] = "__builtin_ia32_crc32si";
                const uint32_t name = intern_find(&identifiers, (const uint8_t*)spelling, sizeof(spelling) - 1);
                const uint32_t binding = c_bindings_declare(&reader.bindings, C_BINDING_ORDINARY, name, C_BINDING_OBJECT, 0, 0, 1);
                check_test_expect(test, name != 0 && binding != 0, "shadow fixture supplies the original parameter's ordinary binding");
            }
            const uint32_t binding_events = reader.bindings.count;
            const bool initialized = check_input_intrinsic_initialize(&reader);
            check_test_expect(test, initialized && !output.resource, "exact intrinsic candidate preparation has bounded storage");
            uint32_t prepared = 0;
            uint32_t failed = 0;
            uint32_t first_argument_call = BUSTER_CHECK_NONE;
            for (uint32_t token = 0; initialized && token < parser.tokens.count; token += 1)
            {
                if (parser.tree.roles[token] == PARSE_ROLE_OPEN_BODY)
                {
                    reader.function_body = token;
                }
                else if (parser.tree.roles[token] == PARSE_ROLE_CLOSE_BODY)
                {
                    reader.function_body = BUSTER_CHECK_NONE;
                }
                const CheckInputIntrinsicStatus status = check_input_intrinsic_prepare(&reader, token);
                prepared += status == CHECK_INPUT_INTRINSIC_PREPARED;
                failed += status == CHECK_INPUT_INTRINSIC_FAILED;
            }
            check_test_expect(test, !output.resource && prepared == fixture->calls && failed == fixture->failed &&
                              output.intrinsic_call_count == fixture->calls && output.intrinsic_declaration_count == fixture->declarations &&
                              output.report_count == fixture->failed && reader.bindings.count == binding_events,
                              fixture->name);
            if (prepared != fixture->calls || failed != fixture->failed || output.intrinsic_declaration_count != fixture->declarations)
            {
                fprintf(stderr, "intrinsic source fixture %s: prepared=%u failed=%u declarations=%u reports=%u\n", fixture->name,
                        prepared, failed, output.intrinsic_declaration_count, output.report_count);
            }
            uint64_t signature = UINT64_C(1469598103934665603);
            for (uint32_t i = 0; i < output.intrinsic_declaration_count; i += 1)
            {
                const CheckInputIntrinsicDeclaration* declaration = &output.intrinsic_declarations[i];
                const BuiltinIntrinsicInfo* info = declaration->info;
                check_test_expect(test, info != 0 && builtin_intrinsic_lookup(info->name, info->name_length) == info,
                                  "source declaration retains the immutable exact catalogue row and all provenance");
                if (info != 0 && strcmp(info->name, "__builtin_ia32_crc32si") == 0)
                {
                    check_test_expect(test, declaration->types.result_type == BUSTER_TYPE_UINT && declaration->types.parameter_count == 2 &&
                                      declaration->types.parameters[0] == BUSTER_TYPE_UINT && declaration->types.parameters[1] == BUSTER_TYPE_UINT &&
                                      declaration->types.ice_parameters == 0 && !declaration->types.variadic && declaration->types.prototype &&
                                      strcmp(info->features, "crc32") == 0 && strcmp(info->attributes, "ncE") == 0,
                                      "CRC32 retains the independently specified unsigned prototype, features and opaque attributes");
                }
                else if (info != 0 && strcmp(info->name, "__builtin_ia32_pshufd") == 0)
                {
                    const TypeId vector = type_vector(&types.table, BUSTER_TYPE_INT, 4, false);
                    check_test_expect(test, vector != 0 && declaration->types.result_type == vector && declaration->types.parameter_count == 2 &&
                                      declaration->types.parameters[0] == vector && declaration->types.parameters[1] == BUSTER_TYPE_INT &&
                                      declaration->types.ice_parameters == 2 && strcmp(info->features, "sse2") == 0 &&
                                      strcmp(info->attributes, "ncEV:128:") == 0,
                                      "PSHUFD retains its exact vector signature and required immediate bit without inventing an ICE witness");
                }
                else if (info != 0 && strcmp(info->name, "__builtin_ia32_pause") == 0)
                {
                    check_test_expect(test, declaration->types.result_type == BUSTER_TYPE_VOID && declaration->types.parameter_count == 0 &&
                                      declaration->types.prototype && !declaration->types.variadic && declaration->types.ice_parameters == 0,
                                      "PAUSE retains a zero-argument void prototype");
                }
                signature = (signature ^ declaration->types.function_type ^ ((uint64_t)declaration->types.status << 32)) * UINT64_C(1099511628211);
                signature = (signature ^ declaration->types.ice_parameters) * UINT64_C(1099511628211);
            }
            for (uint32_t i = 0; i < output.intrinsic_call_count; i += 1)
            {
                const CheckInputIntrinsicCall* call = &output.intrinsic_calls[i];
                const CheckInputIntrinsicDeclaration* declaration = &output.intrinsic_declarations[call->declaration];
                const bool count = i < BUSTER_ARRAY_LENGTH(fixture->arguments) && call->argument_count == fixture->arguments[i];
                check_test_expect(test, count && call->keyword + 1 == call->opening && call->opening < call->closing &&
                                  call->closing < parser.tokens.count && output.intrinsic_ids[call->closing] == i + 1 &&
                                  parser.tree.roles[call->opening] == PARSE_ROLE_OPEN_CALL && parser.tree.roles[call->closing] == PARSE_ROLE_CLOSE_CALL &&
                                  declaration->types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && !declaration->types.signature_ignored &&
                                  type_kind(&types.table, declaration->types.function_type) == BUSTER_TYPE_KIND_FUNCTION,
                                  "intrinsic call retains its real count, original delimiters and exact ordinary function type");
                check_test_expect(test, call->function_body < call->keyword && parser.tree.roles[call->function_body] == PARSE_ROLE_OPEN_BODY &&
                                  parser.tree.parents[call->function_body] > call->closing,
                                  "intrinsic call retains the enclosing original function body");
                bool ranges = call->argument_count == 0 ? call->arguments == 0 : call->arguments != 0;
                for (uint32_t argument = 0; ranges && argument < call->argument_count; argument += 1)
                {
                    const CheckInputIntrinsicArgument span = call->arguments[argument];
                    ranges = span.first < span.end && span.first > call->opening && span.end <= call->closing &&
                             (argument == 0 ? span.first == call->opening + 1 : span.first == call->arguments[argument - 1].end + 1) &&
                             (argument + 1 == call->argument_count ? span.end == call->closing : parser.tokens.kinds[span.end] == PARSE_KIND_COMMA);
                    signature = (signature ^ span.first ^ ((uint64_t)span.end << 32)) * UINT64_C(1099511628211);
                }
                check_test_expect(test, ranges, "argument slices exclude delimiters and keep nested comma expressions intact");
                first_argument_call = first_argument_call == BUSTER_CHECK_NONE && call->argument_count != 0 ? call->keyword : first_argument_call;
                signature = (signature ^ call->keyword ^ ((uint64_t)call->closing << 32) ^ call->function_body) * UINT64_C(1099511628211);
                const uint64_t mark = arena.length;
                const uint32_t declarations = output.intrinsic_declaration_count;
                const CheckInputIntrinsicStatus repeated = check_input_intrinsic_prepare(&reader, call->keyword);
                check_test_expect(test, repeated == CHECK_INPUT_INTRINSIC_PREPARED && output.intrinsic_call_count == fixture->calls &&
                                  output.intrinsic_declaration_count == declarations && arena.length == mark,
                                  "revisiting a prepared call neither duplicates metadata nor allocates argument storage");
            }
            if (output.intrinsic_ids != 0)
            {
                bool identities = output.intrinsic_ids[parser.tokens.count] == 0;
                for (uint32_t token = 0; identities && token < parser.tokens.count; token += 1)
                {
                    const uint32_t id = output.intrinsic_ids[token];
                    identities = id == 0 || (id <= output.intrinsic_call_count && output.intrinsic_calls[id - 1].closing == token);
                }
                check_test_expect(test, identities, "only original call closers carry dense intrinsic identities; the sentinel stays zero");
            }
            else
            {
                check_test_expect(test, reader.intrinsic_names == 0 && output.intrinsic_calls == 0 && output.intrinsic_declarations == 0 &&
                                  arena.length == 0 && scratch.length == 0,
                                  "translation units without exact intrinsic operands allocate no intrinsic arrays");
            }
            if (output.intrinsic_call_count != 0)
            {
                const CheckInputIntrinsicCall* call = &output.intrinsic_calls[0];
                const uint32_t name = parser.tokens.names[call->keyword];
                const uint32_t handle = c_bindings_declare(&reader.bindings, C_BINDING_ORDINARY, name, C_BINDING_OBJECT, call->keyword, call->keyword, 1);
                const uint32_t events = reader.bindings.count;
                const CheckInputIntrinsicStatus shadowed = check_input_intrinsic_prepare(&reader, call->keyword);
                check_test_expect(test, handle != 0 && shadowed == CHECK_INPUT_INTRINSIC_NOT_FOUND && reader.bindings.count == events &&
                                  output.intrinsic_call_count == fixture->calls,
                                  "a source ordinary binding shadows even an already cached intrinsic declaration");
                const bool restored = c_bindings_restore(&reader.bindings, 0, call->keyword);
                const CheckInputIntrinsicStatus unshadowed = check_input_intrinsic_prepare(&reader, call->keyword);
                check_test_expect(test, restored && unshadowed == CHECK_INPUT_INTRINSIC_PREPARED,
                                  "leaving the ordinary binding restores exact intrinsic lookup without a synthetic declaration");
            }
            for (uint32_t i = 0; i < output.report_count; i += 1)
            {
                check_test_expect(test, output.reports[i].message == CHECK_INPUT_UNSUPPORTED && output.reports[i].token < parser.tokens.count &&
                                  parser.tokens.kinds[output.reports[i].token] == PARSE_KIND_IDENTIFIER,
                                  "unrepresented intrinsic uses report the original source identifier");
            }
            if (storage && variant == 0 && first_argument_call != BUSTER_CHECK_NONE)
            {
                check_test_intrinsic_input_storage(test, &parser, &pp, &types.table, first_argument_call);
            }
            signatures[variant] = signature;
        }
        arena_release(&bindings);
        arena_release(&scratch);
        arena_release(&arena);
        check_test_types_close(&types);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
    check_test_expect(test, signatures[0] != 0 && signatures[0] == signatures[1], "scalar/vector parser paths retain identical intrinsic metadata and operand intervals");
}

static void check_test_intrinsic_inputs(CheckTest* test, const char* clang)
{
    const CheckIntrinsicInputFixture fixtures[] =
    {
        {
            "scalar result and exact repeated-name cache",
            "unsigned f(unsigned x,unsigned y){__builtin_ia32_pause();return __builtin_ia32_crc32si(x,y)^__builtin_ia32_crc32si(y,x);}\n",
            2, 3, 0, { 0, 2, 2 }, false,
        },
        {
            "vector prototype and immediate marker",
            "typedef int V __attribute__((vector_size(16)));V f(V x){return __builtin_ia32_pshufd(x,3);}\n",
            1, 1, 0, { 2 }, false,
        },
        {
            "nested calls and parenthesized comma are exact source operands",
            "unsigned f(unsigned x){return __builtin_ia32_crc32si((x,1),__builtin_ia32_crc32si(2,3));}\n",
            1, 2, 0, { 2, 2 }, false,
        },
        {
            "too few arguments remain available to the typed call checker",
            "unsigned f(unsigned x){return __builtin_ia32_crc32si(x);}\n",
            1, 1, 0, { 1 }, true,
        },
        {
            "too many arguments remain available to the typed call checker",
            "void f(void){__builtin_ia32_pause(1,2,3);}\n",
            1, 1, 0, { 3 }, true,
        },
        {
            "non-call intrinsic reference does not receive an ordinary binding",
            "void f(void){(void)__builtin_ia32_pause;}\n",
            1, 0, 1, { 0 }, true,
        },
        {
            "custom signature stays explicit without an invented variadic prototype",
            "void *f(void *p){return __builtin_assume_aligned(p,16);}\n",
            1, 0, 1, { 0 }, false,
        },
        {
            "uppercase ignored-signature metadata cannot authorize an ordinary call",
            "void f(void){__builtin_get_device_side_mangled_name(0);}\n",
            1, 0, 1, { 0 }, true,
        },
        {
            "unrepresented reference descriptor stays unsupported",
            "void f(void){__addressof(0);}\n",
            1, 0, 1, { 0 }, true,
        },
        {
            "near spellings are exact misses",
            "int __builtin_ia32_crc32six(int);int f(int x){return __builtin_ia32_crc32six(x);}\n",
            0, 0, 0, { 0 }, false,
        },
        {
            "ordinary source needs no intrinsic storage",
            "int f(int x){return x+1;}\n",
            0, 0, 0, { 0 }, false,
        },
        {
            "variadic exact prototype retains every source argument",
            "void f(const void *p){__builtin_prefetch(p,0,3);}\n",
            1, 1, 0, { 3 }, false,
        },
        {
            "ordinary local name can shadow an intrinsic spelling",
            "unsigned f(unsigned (*__builtin_ia32_crc32si)(unsigned,unsigned)){return __builtin_ia32_crc32si(1,2);}\n",
            0, 0, 0, { 0 }, false, true,
        },
        {
            "candidate batches include two full vectors and one exact tail",
            "void f(void){__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();"
            "__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();"
            "__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();"
            "__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();__builtin_ia32_pause();}\n",
            1, 17, 0, { 0 }, false,
        },
        {
            "actual source count is not truncated to the descriptor's 64-parameter bound",
            "void f(void){__builtin_ia32_pause(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
            "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
            "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);}\n",
            1, 1, 0, { 65 }, true,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-check-intrinsic-source-XXXXXX";
    char paths[FILE_COUNT][256];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/fixture-%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, (uint32_t)strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17", "-fsyntax-only", "-nostdinc", "-march=znver5" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "original intrinsic source fixtures run through the installed Clang oracle");
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        if (ready)
        {
            check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, fixtures[i].name);
        }
        check_test_intrinsic_input_fixture(test, &fixtures[i], i == 0);
        if (ready)
        {
            unlink(paths[i]);
        }
    }
    if (ready)
    {
        rmdir(directory);
    }
}
