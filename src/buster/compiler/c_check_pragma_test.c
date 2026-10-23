// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original pragma fixtures: the installed Clang checks preprocessing acceptance independently, while both parser
// paths check retained warning-state identities, source spans, canonicalization and allocation rollback.

#define BUSTER_CHECK_PRAGMA_TEST_IMPLEMENTATION 1

typedef struct CheckPragmaFixture CheckPragmaFixture;
struct CheckPragmaFixture
{
    const char* name;
    const char* source;
    uint32_t records;
    uint32_t nonnull_records;
    uint32_t other;
    bool rejected;
    bool semantic_error;
};

static void check_test_nonnull_storage(CheckTest* test, const Parser* parser, Preprocessor* pp)
{
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    const bool ready = arena.base != 0 && scratch.base != 0 && parser->tokens.pragma_count == 4;
    check_test_expect(test, ready, "nonnull history exhaustion fixture setup");
    if (ready)
    {
        memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
        const uint64_t capacity = arena.capacity;
        uint64_t required = 0;
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            const uint64_t maximum = pass == 0 ? 1 : required - 31;
            for (uint64_t limit = 0; limit < maximum; limit += 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : capacity;
                scratch.length = 29;
                uint8_t handled[4] = { 0 };
                CheckInput output = { 0 };
                CheckInputReader reader =
                {
                    .output = &output,
                    .parser = parser,
                    .pp = pp,
                    .arena = &arena,
                    .scratch = &scratch,
                };
                const bool valid = check_input_nonnull_pragmas(&reader, handled);
                if (valid)
                {
                    check_test_expect(test, !output.resource && output.nonnull_pragmas != 0 && output.nonnull_pragmas->count == 4 &&
                                      output.nonnull_pragmas->active == BUSTER_CHECK_NONE && scratch.length == 29,
                                      "nonnull history initializes poisoned storage without using scratch");
                    required = arena.length;
                }
                else
                {
                    check_test_expect(test, pass != 0 && output.resource && output.nonnull_pragmas == 0 &&
                                      handled[0] == 0 && handled[1] == 0 && handled[2] == 0 && handled[3] == 0 &&
                                      arena.length == 31 && scratch.length == 29,
                                      "nonnull exhaustion rolls back history and handled mask");
                }
            }
        }
        arena.capacity = capacity;
        for (uint32_t mode = 0; mode < 3; mode += 1)
        {
            Parser view = *parser;
            uint32_t sources[2] = { parser->tokens.pragma_sources[0], parser->tokens.pragma_sources[2] };
            uint32_t positions[2] = { parser->tokens.pragma_positions[0], parser->tokens.pragma_positions[2] };
            view.tokens.pragma_count = mode == 0 ? 3 : mode == 1 ? 1 : 2;
            view.tokens.pragma_sources = mode == 0 ? parser->tokens.pragma_sources : mode == 1 ? parser->tokens.pragma_sources + 1 : sources;
            view.tokens.pragma_positions = mode == 0 ? parser->tokens.pragma_positions : mode == 1 ? parser->tokens.pragma_positions + 1 : positions;
            arena.length = 31;
            uint8_t handled[4] = { 0 };
            CheckInput output = { 0 };
            CheckInputReader reader =
            {
                .output = &output,
                .parser = &view,
                .pp = pp,
                .arena = &arena,
                .scratch = &scratch,
            };
            const bool valid = check_input_nonnull_pragmas(&reader, handled);
            const CheckInputNonnullPragmas* history = output.nonnull_pragmas;
            if (mode == 0)
            {
                check_test_expect(test, valid && history != 0 && history->count == 3 && history->active == 2 && history->error_count == 0 && handled[3] == 0,
                                  "diagnostic prefix preserves an open region without inventing an end-of-unit error");
            }
            else
            {
                check_test_expect(test, valid && history != 0 && history->error_count == 1 && handled[mode - 1] == 3,
                                  "unexpected retained nesting or unmatched end remains a hard directive error");
            }
        }
    }
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_pragma_storage(CheckTest* test, const Parser* parser, Preprocessor* pp)
{
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    const bool ready = arena.base != 0 && scratch.base != 0 && parser->tokens.pragma_count < 64;
    check_test_expect(test, ready, "diagnostic pragma exhaustion fixture setup");
    if (ready)
    {
        memset(arena_push(&arena, arena.capacity, 1), 0xa5, arena.capacity);
        memset(arena_push(&scratch, scratch.capacity, 1), 0x5a, scratch.capacity);
        const uint64_t output_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        uint64_t required = 0;
        bool scratch_succeeded = false;
        for (uint32_t pass = 0; pass < 3; pass += 1)
        {
            const uint64_t maximum = pass == 0 ? 1 : pass == 1 ? required - 31 : scratch_capacity;
            for (uint64_t limit = 0; limit < maximum && !(pass == 2 && scratch_succeeded); limit += pass == 2 ? 7 : 1)
            {
                arena.length = 31;
                arena.capacity = pass == 1 ? 31 + limit : output_capacity;
                scratch.length = 29;
                scratch.capacity = pass == 2 ? 29 + limit : scratch_capacity;
                uint8_t handled[64] = { 0 };
                CheckInput output = { 0 };
                CheckInputReader reader =
                {
                    .output = &output,
                    .parser = parser,
                    .pp = pp,
                    .arena = &arena,
                    .scratch = &scratch,
                };
                const bool valid = check_input_diagnostic_pragmas(&reader, handled);
                if (valid)
                {
                    check_test_expect(test, !output.resource && output.diagnostic_pragmas != 0 && output.diagnostic_pragmas->count == 8 &&
                                      output.diagnostic_pragmas->final_state == 7 && scratch.length == 29,
                                      "diagnostic history ignores poisoned scratch and restores its mark");
                    required = arena.length;
                    scratch_succeeded |= pass == 2;
                }
                else
                {
                    bool untouched = true;
                    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(handled); i += 1)
                    {
                        untouched &= handled[i] == 0;
                    }
                    check_test_expect(test, pass != 0 && output.resource && output.diagnostic_pragmas == 0 && untouched &&
                                      arena.length == 31 && scratch.length == 29,
                                      "diagnostic history exhaustion rolls back records, decoded strings and handled mask");
                }
            }
        }
        check_test_expect(test, scratch_succeeded, "diagnostic history recovers after scratch exhaustion");
        arena.capacity = output_capacity;
        scratch.capacity = scratch_capacity;
    }
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_pragma_history(CheckTest* test, const Parser* parser, Preprocessor* pp, const CheckPragmaFixture* fixture, bool first)
{
    Arena arena = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    CheckInput output = { 0 };
    CheckInputReader reader =
    {
        .output = &output,
        .parser = parser,
        .pp = pp,
        .arena = &arena,
        .scratch = &scratch,
    };
    uint8_t handled[64] = { 0 };
    bool valid = arena.base != 0 && scratch.base != 0 && parser->tokens.pragma_count < BUSTER_ARRAY_LENGTH(handled);
    valid = valid && check_input_diagnostic_pragmas(&reader, handled);
    valid = valid && check_input_nonnull_pragmas(&reader, handled);
    check_test_expect(test, valid && !output.resource && scratch.length == 0, "diagnostic pragma helper restores scratch without resource failure");
    const CheckInputDiagnosticPragmas* history = output.diagnostic_pragmas;
    check_test_expect(test, (history != 0 ? history->count : 0) == fixture->records, fixture->name);
    uint32_t handled_count = 0;
    for (uint32_t i = 0; i < parser->tokens.pragma_count; i += 1)
    {
        handled_count += handled[i] != 0;
    }
    check_test_expect(test, handled_count == fixture->records + fixture->nonnull_records && parser->tokens.pragma_count - handled_count == fixture->other,
                      "pragma metadata helpers leave unrelated directives unhandled");
    const CheckInputNonnullPragmas* nonnull = output.nonnull_pragmas;
    check_test_expect(test, (nonnull != 0 ? nonnull->count : 0) == fixture->nonnull_records, "assume_nonnull canonical record count is exact");
    for (uint32_t i = 0; nonnull != 0 && i < nonnull->count; i += 1)
    {
        const CheckInputNonnullPragma* record = &nonnull->records[i];
        check_test_expect(test, record->pragma < parser->tokens.pragma_count &&
                          record->source_begin == parser->tokens.pragma_sources[record->pragma] &&
                          record->source_end <= pp->output.count && pp->output.kinds[record->source_end - 1] == BUSTER_PP_KIND_PRAGMA_END &&
                          record->token == parser->tokens.pragma_positions[record->pragma] && !record->invalid,
                          "nonnull records preserve original PP and parser coordinates");
    }
    if (nonnull != 0)
    {
        check_test_expect(test, nonnull->active == BUSTER_CHECK_NONE && nonnull->error_count == 0,
                          "complete preprocessed units retain balanced nonnull regions");
    }
    if (nonnull != 0 && nonnull->count == 4)
    {
        const uint32_t before[] = { BUSTER_CHECK_NONE, 0, BUSTER_CHECK_NONE, 2 };
        const uint32_t after[] = { 0, BUSTER_CHECK_NONE, 2, BUSTER_CHECK_NONE };
        for (uint32_t i = 0; i < nonnull->count; i += 1)
        {
            const CheckInputNonnullPragma* record = &nonnull->records[i];
            check_test_expect(test, record->active_before == before[i] && record->active_after == after[i] &&
                              record->matching_begin == (i < 2 ? 0 : 2) && record->begin == ((i & 1) == 0),
                              "separate nonnull regions retain exact active interval identities");
        }
        check_test_nonnull_storage(test, parser, pp);
    }
    for (uint32_t i = 0; history != 0 && i < history->count; i += 1)
    {
        const CheckInputDiagnosticPragma* record = &history->records[i];
        check_test_expect(test, record->pragma < parser->tokens.pragma_count &&
                          record->source_begin == parser->tokens.pragma_sources[record->pragma] &&
                          record->token == parser->tokens.pragma_positions[record->pragma] && record->token <= parser->tokens.count &&
                          record->source_end <= pp->output.count && record->source_end > record->source_begin &&
                          pp->output.kinds[record->source_begin] == C_TOKEN_HASH && pp->output.kinds[record->source_end - 1] == BUSTER_PP_KIND_PRAGMA_END,
                          "diagnostic records retain exact PP source intervals and parser positions");
        check_test_expect(test, (record->state_before == BUSTER_CHECK_NONE || record->state_before < i) &&
                          (record->state_after == BUSTER_CHECK_NONE || record->state_after <= i) &&
                          (record->matching_push == BUSTER_CHECK_NONE || record->matching_push < i),
                          "diagnostic history references only existing immutable states");
    }
    if (first && history != 0 && history->count == 8)
    {
        const uint32_t before[] = { BUSTER_CHECK_NONE, 0, 0, 2, 2, 4, 2, 0 };
        const uint32_t after[] = { 0, 0, 2, 2, 4, 2, 0, 7 };
        const uint8_t operations[] = { CHECK_DIAGNOSTIC_WARNING, CHECK_DIAGNOSTIC_PUSH, CHECK_DIAGNOSTIC_IGNORED, CHECK_DIAGNOSTIC_PUSH,
                                       CHECK_DIAGNOSTIC_ERROR, CHECK_DIAGNOSTIC_POP, CHECK_DIAGNOSTIC_POP, CHECK_DIAGNOSTIC_FATAL };
        const uint8_t dialects[] = { CHECK_DIAGNOSTIC_CLANG, CHECK_DIAGNOSTIC_CLANG, CHECK_DIAGNOSTIC_GCC, CHECK_DIAGNOSTIC_GCC,
                                    CHECK_DIAGNOSTIC_CLANG, CHECK_DIAGNOSTIC_CLANG, CHECK_DIAGNOSTIC_GCC, CHECK_DIAGNOSTIC_GCC };
        const char* const options[] = { "-Wunused-variable", "", "-Wunused-variable", "", "-Wunused-function", "", "", "-Wdeprecated-declarations" };
        for (uint32_t i = 0; i < history->count; i += 1)
        {
            const CheckInputDiagnosticPragma* record = &history->records[i];
            check_test_expect(test, record->state_before == before[i] && record->state_after == after[i] &&
                              record->operation == operations[i] && record->dialect == dialects[i] &&
                              record->option.length == strlen(options[i]) &&
                              (record->option.length == 0 || memcmp(record->option.pointer, options[i], record->option.length) == 0),
                              "nested Clang/GCC directives restore the exact previous warning map");
        }
        check_test_expect(test, history->maximum_depth == 2 && history->unmatched_pushes == 0 && history->unmatched_pops == 0 &&
                          history->records[5].matching_push == 3 && history->records[6].matching_push == 1 && history->final_state == 7 &&
                          history->records[7].token == parser->tokens.count,
                          "mixed namespaces share stack and retain final directives at token-count sentinel");
        check_test_pragma_storage(test, parser, pp);
    }
    arena_release(&scratch);
    arena_release(&arena);
}

static void check_test_pragmas(CheckTest* test, const char* clang)
{
    const CheckPragmaFixture fixtures[] =
    {
        {
            .name = "nested shared diagnostic stack",
            .source = "#pragma clang diagnostic warning \"-Wunused-variable\"\nint a;\n#pragma clang diagnostic push\n#pragma GCC diagnostic ignored \"-Wunused-variable\"\n#pragma GCC diagnostic push\n#pragma clang diagnostic error \"-Wunused-function\"\nint f(void){return 0;}\n#pragma clang diagnostic pop\n#pragma GCC diagnostic pop\n#pragma GCC diagnostic fatal \"-Wdeprecated-declarations\"\n",
            .records = 8,
        },
        {
            .name = "preprocessor canonicalizes warning-only no-ops and strings",
            .source = "#pragma clang diagnostic pop\n#pragma clang diagnostic unknown \"-Wunused-variable\"\n#pragma GCC diagnostic ignored \"unused-variable\"\n#pragma clang diagnostic ignored \"-Wno-such-group-buster-test\"\n#pragma clang diagnostic warning \"-Wunused-variable\" extra\n#pragma clang diagnostic push extra\n#pragma GCC diagnostic ignored \"-Wunused-\" \"variable\"\n#pragma clang diagnostic pop extra\nint f(void){return 0;}\n",
            .records = 3,
        },
        {
            .name = "macro pragma expansion retains diagnostic positions",
            .source = "#define PUSH _Pragma(\"clang diagnostic push\")\n#define IGNORE _Pragma(\"GCC diagnostic ignored \\\"-Wunused-variable\\\"\")\nPUSH\nint a;\nIGNORE\nint b;\n_Pragma(\"clang diagnostic pop\")\n",
            .records = 3,
        },
        {
            .name = "warning state is not C block scope",
            .source = "int f(void){\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wunused-variable\"\nreturn 0;}\n#pragma GCC diagnostic pop\n",
            .records = 3,
        },
        {
            .name = "unmatched final push and remark groups are retained",
            .source = "#pragma GCC diagnostic push\n#pragma clang diagnostic ignored \"-Rpass\"\n#pragma GCC diagnostic warning \"-Weverything\"\nint a;\n",
            .records = 3,
        },
        {
            .name = "warning helper leaves other pragma ownership intact",
            .source = "#pragma clang diagnostic push\n#pragma pack(1)\n#pragma buster_unknown\n#pragma clang diagnostic pop\nint a;\n",
            .records = 2,
            .other = 2,
        },
        {
            .name = "no diagnostic pragma allocates no history",
            .source = "int f(void){return 0;}\n",
        },
        {
            .name = "missing diagnostic string is a preprocessing error",
            .source = "#pragma clang diagnostic ignored\nint a;\n",
            .rejected = true,
        },
        {
            .name = "wide diagnostic string is a preprocessing error",
            .source = "#pragma GCC diagnostic warning L\"-Wunused-variable\"\nint a;\n",
            .rejected = true,
        },
        {
            .name = "utf8 diagnostic string is a preprocessing error",
            .source = "#pragma clang diagnostic ignored u8\"-Wunused-variable\"\nint a;\n",
            .rejected = true,
        },
        {
            .name = "diagnostic string macro is not expanded",
            .source = "#define OPTION \"-Wunused-variable\"\n#pragma clang diagnostic ignored OPTION\nint a;\n",
            .rejected = true,
        },
        {
            .name = "warning policy cannot suppress an undeclared operand",
            .source = "#pragma clang diagnostic ignored \"-Weverything\"\nint f(void){return missing;}\n",
            .records = 1,
            .semantic_error = true,
        },
        {
            .name = "warning policy cannot suppress an invalid dereference",
            .source = "#pragma clang diagnostic ignored \"-Weverything\"\nint f(void){return *1;}\n",
            .records = 1,
            .semantic_error = true,
        },
        {
            .name = "separate assume_nonnull regions retain pointer declarations",
            .source = "#pragma clang assume_nonnull begin\nint *p; int *f(int *x){return x;}\n#pragma clang assume_nonnull end\nint *q;\n#pragma clang assume_nonnull begin\nint *r;\n#pragma clang assume_nonnull end\n",
            .nonnull_records = 4,
        },
        {
            .name = "diagnostic and nonnull histories coexist",
            .source = "#pragma GCC diagnostic push\n#pragma clang assume_nonnull begin\nint *f(int *p){return p;}\n#pragma clang assume_nonnull end\n#pragma clang diagnostic pop\n",
            .records = 2,
            .nonnull_records = 2,
        },
        {
            .name = "nonnull trailing tokens are already canonicalized",
            .source = "#pragma clang assume_nonnull begin extra\nint *f(void){return 0;}\n#pragma clang assume_nonnull end extra\n",
            .nonnull_records = 2,
        },
        {
            .name = "macro nonnull regions retain begin and end",
            .source = "#define BEGIN _Pragma(\"clang assume_nonnull begin\")\nBEGIN\nint *p;\n_Pragma(\"clang assume_nonnull end\")\n",
            .nonnull_records = 2,
        },
        {
            .name = "nested nonnull region is a preprocessing error",
            .source = "#pragma clang assume_nonnull begin\n#pragma clang assume_nonnull begin\nint *p;\n#pragma clang assume_nonnull end\n",
            .rejected = true,
        },
        {
            .name = "unmatched nonnull end is a preprocessing error",
            .source = "#pragma clang assume_nonnull end\nint *p;\n",
            .rejected = true,
        },
        {
            .name = "unterminated nonnull region is a preprocessing error",
            .source = "#pragma clang assume_nonnull begin\nint *p;\n",
            .rejected = true,
        },
        {
            .name = "unknown nonnull operation is a preprocessing error",
            .source = "#pragma clang assume_nonnull ignored\nint *p;\n",
            .rejected = true,
        },
    };
    enum { FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) };
    char directory[] = "/tmp/buster-diagnostic-pragmas-XXXXXX";
    char paths[FILE_COUNT][256];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    bool ready = mkdtemp(directory) != 0;
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], fixtures[i].source, (uint32_t)strlen(fixtures[i].source));
    }
    const char* arguments[] = { "-std=gnu17", "-fsyntax-only", "-w" };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "diagnostic pragma Clang oracle runs");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const CheckPragmaFixture* fixture = &fixtures[i];
        check_test_expect(test, outcomes[i].rejected == (fixture->rejected || fixture->semantic_error), fixture->name);
        for (uint32_t variant = 0; variant < 2; variant += 1)
        {
            InternTable identifiers = { 0 };
            Preprocessor pp = { 0 };
            Parser parser = { 0 };
            FILE* diagnostics = tmpfile();
            const PPOptions options =
            {
                .language = { .standard = 2017, .gnu = true },
                .no_standard_includes = true,
                .clang_features = true,
                .diagnostics = diagnostics,
            };
            const ParseOptions parse_options = { .scalar = variant == 0 };
            bool initialized = diagnostics != 0 && intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) &&
                               parse_initialize(&parser, &parse_options, options.language);
            check_test_expect(test, initialized, "diagnostic pragma fixture initializes");
            const bool preprocessed = initialized && pp_run_source(&pp, "diagnostic-pragmas.c", (const uint8_t*)fixture->source, (uint32_t)strlen(fixture->source));
            check_test_expect(test, preprocessed != fixture->rejected, fixture->name);
            if (preprocessed)
            {
                const bool parsed = parse_translation_unit(&parser, &pp);
                check_test_expect(test, parsed, "diagnostic pragmas leave declaration parse unchanged");
                if (parsed)
                {
                    check_test_pragma_history(test, &parser, &pp, fixture, i == 0);
                }
            }
            else if (initialized)
            {
                rewind(diagnostics);
                char line[1024];
                uint32_t first_line = 0;
                while (first_line == 0 && fgets(line, sizeof(line), diagnostics) != 0)
                {
                    if (strstr(line, "error:") != 0)
                    {
                        sscanf(line, "diagnostic-pragmas.c:%u:", &first_line);
                    }
                }
                check_test_expect(test, first_line == outcomes[i].line, "pragma syntax errors retain Clang's exact first source line");
            }
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
            if (diagnostics != 0)
            {
                fclose(diagnostics);
            }
        }
        if (!fixture->rejected && fixture->other == 0)
        {
            const CheckSourceFixture semantic =
            {
                .name = fixture->name,
                .source = fixture->source,
                .rejected = fixture->semantic_error,
            };
            check_test_source(test, &semantic, paths[i], &outcomes[i]);
        }
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}
