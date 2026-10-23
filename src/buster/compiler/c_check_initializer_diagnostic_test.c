// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real source fragments prove that initializer diagnostics can precede later input/expression failures without
// publishing a partial unit or clearing its actual reports. Include after the shared initializer test helpers.
#ifndef BUSTER_C_CHECK_INITIALIZER_DIAGNOSTIC_TEST_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_DIAGNOSTIC_TEST_INCLUDED

static void check_init_test_diagnostic_source(CheckTest* test, const char* source, uint32_t error_offset, bool input_error, uint32_t warning_count)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                              pp_run_source(&pp, "initializer-order.c", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "initializer diagnostic source preprocesses");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 22);
        Arena stream_arena = arena_reserve(UINT64_C(1) << 22);
        Arena output = arena_reserve(UINT64_C(1) << 20);
        Arena scratch = arena_reserve(UINT64_C(1) << 22);
        const ParseOptions parse_options = { .scalar = path == 0 };
        bool ready = check_test_types_open(&types) && parse_initialize(&parser, &parse_options, pp_options.language) &&
                     parse_translation_unit(&parser, &pp) && parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE;
        CheckInput input = { 0 };
        bool input_valid = false;
        if (ready)
        {
            input_valid = check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch);
            ready = !input.resource && (input_valid || input.diagnostic_graph);
        }
        const CheckOptions options = { .scalar = path == 0 };
        CheckStream stream = { 0 };
        ready = ready && check_graph_run(&types.table, &input.graph, &stream_arena, &scratch, &options, &stream);
        uint8_t* prefix = ready ? BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, 64) : 0;
        uint8_t* scratch_prefix = ready ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, 64) : 0;
        ready = ready && prefix != 0 && scratch_prefix != 0;
        check_test_expect(test, ready && (input.report_count != 0) == input_error && (input_error || stream.report_count != 0),
                          "actual checked fragment retains its later phase failure");
        if (ready)
        {
            memset(prefix, 0xa5, 64);
            memset(scratch_prefix, 0x5a, 64);
            uint32_t declaration = BUSTER_CHECK_NONE;
            uint32_t cutoff = parser.tokens.count;
            for (uint32_t token = 0; token < input.token_count && declaration == BUSTER_CHECK_NONE; token += 1)
            {
                if (input.initializer_begins[token] != BUSTER_CHECK_NONE)
                {
                    declaration = token;
                }
            }
            for (uint32_t i = 0; i < input.report_count; i += 1)
            {
                cutoff = input.reports[i].token < cutoff ? input.reports[i].token : cutoff;
            }
            for (uint32_t i = 0; i < stream.report_count; i += 1)
            {
                cutoff = stream.reports[i].token < cutoff ? stream.reports[i].token : cutoff;
            }
            const CheckStream before = stream;
            const uint32_t input_reports = input.report_count;
            CheckInitDiagnostic diagnostic = { 0 };
            const bool diagnosed = check_initializers_diagnose(&diagnostic, &input, &stream, &parser, &types.table,
                                                                BUSTER_CHECK_NONE, &output, &scratch);
            const bool expected = error_offset != BUSTER_CHECK_NONE;
            const uint32_t token = expected && declaration != BUSTER_CHECK_NONE ? input.initializer_begins[declaration] + error_offset : BUSTER_CHECK_NONE;
            char description[224];
            snprintf(description, sizeof(description), "initializer ordering %.35s: expected token %u, got %u/status %u on %s path", source,
                     token, diagnostic.token, diagnostic.status, path == 0 ? "scalar" : "vector");
            check_test_expect(test, diagnosed && diagnostic.token == token && diagnostic.status == (expected ? CHECK_INIT_INVALID : CHECK_INIT_OK) &&
                                   (!expected || diagnostic.token < cutoff), description);
            check_test_expect(test, (diagnostic.warning_count != 0) == (warning_count != 0) &&
                                   (warning_count == 0 || (declaration != BUSTER_CHECK_NONE && diagnostic.warning_token >= input.initializer_begins[declaration] &&
                                                 diagnostic.warning_token < input.initializer_ends[declaration])),
                              "diagnostic initializer warnings remain distinct from the first hard error");
            bool untouched = output.length == 64 && scratch.length == 64 && input.report_count == input_reports && memcmp(&before, &stream, sizeof(before)) == 0;
            for (uint32_t i = 0; i < 64; i += 1)
            {
                untouched = untouched && prefix[i] == 0xa5 && scratch_prefix[i] == 0x5a;
            }
            check_test_expect(test, untouched, "diagnostic-only planning preserves actual reports and both arena prefixes");
            uint32_t warning_tokens[32] = { 0 };
            CheckInitWarningSink warnings = { .tokens = warning_tokens, .capacity = BUSTER_ARRAY_LENGTH(warning_tokens), .count = 1 };
            CheckInitDiagnostic complete_warnings = { 0 };
            const bool collected = check_initializers_diagnose_warnings(&complete_warnings, &input, &stream, &parser, &types.table,
                                                                        BUSTER_CHECK_NONE, &output, &scratch, &warnings);
            bool ordered = collected && warnings.count == warning_count + 1 && warning_tokens[0] == 0 &&
                           complete_warnings.warning_count == warning_count && complete_warnings.status == diagnostic.status &&
                           complete_warnings.token == diagnostic.token && output.length == 64 && scratch.length == 64;
            for (uint32_t i = 1; ordered && i < warnings.count; i += 1)
            {
                ordered = warning_tokens[i] < cutoff && warning_tokens[i] > warning_tokens[i - 1];
            }
            check_test_expect(test, ordered, "bounded diagnostic sink retains all original warning tokens in source order before later errors");
            if (warning_count != 0)
            {
                CheckInitWarningSink small = { .tokens = warning_tokens, .capacity = warning_count, .count = 1 };
                CheckInitDiagnostic overflow = { 0 };
                check_test_expect(test, !check_initializers_diagnose_warnings(&overflow, &input, &stream, &parser, &types.table,
                                                                             BUSTER_CHECK_NONE, &output, &scratch, &small) &&
                                       overflow.status == CHECK_INIT_RESOURCE && overflow.warning_count == 0 && small.count == 1 &&
                                       warning_tokens[0] == 0 && output.length == 64 && scratch.length == 64,
                                  "warning sink exhaustion restores its published count and both arena prefixes after partial collection");
            }
            CheckInitializers rejected = { 0 };
            check_test_expect(test, !check_initializers_run(&rejected, &input, &stream, &parser, &types.table, &output, &scratch) &&
                                   rejected.status == CHECK_INIT_CHECK_FAILED && rejected.plans == 0 && rejected.count == 0 && output.length == 64 && scratch.length == 64,
                              "the ordinary initializer publisher still rejects every erroneous unit");
            if (expected && declaration != BUSTER_CHECK_NONE)
            {
                CheckInitDiagnostic limited = { 0 };
                const uint32_t begin = input.initializer_begins[declaration];
                check_test_expect(test, check_initializers_diagnose(&limited, &input, &stream, &parser, &types.table, begin, &output, &scratch) &&
                                       limited.status == CHECK_INIT_OK && limited.token == BUSTER_CHECK_NONE,
                                  "an explicit earlier phase cutoff excludes the entire initializer span");
                Arena tiny = scratch;
                tiny.capacity = 64;
                CheckInitDiagnostic exhausted = { 0 };
                check_test_expect(test, !check_initializers_diagnose(&exhausted, &input, &stream, &parser, &types.table,
                                                                    BUSTER_CHECK_NONE, &output, &tiny) && exhausted.status == CHECK_INIT_RESOURCE &&
                                       tiny.length == 64 && output.length == 64,
                                  "diagnostic capacity exhaustion restores every temporary allocation");
                const uint32_t saved = input.token_nodes[begin + 1];
                input.token_nodes[begin + 1] = stream.count;
                CheckInitDiagnostic incomplete = { 0 };
                check_test_expect(test, check_initializers_diagnose(&incomplete, &input, &stream, &parser, &types.table,
                                                                    BUSTER_CHECK_NONE, &output, &scratch) && incomplete.status == CHECK_INIT_OK &&
                                       incomplete.token == BUSTER_CHECK_NONE,
                                  "a missing checked root cannot manufacture an earlier initializer diagnostic");
                input.token_nodes[begin + 1] = saved;
            }
        }
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&stream_arena);
        arena_release(&input_arena);
        check_test_types_close(&types);
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_init_test_diagnostics(CheckTest* test, const char* clang)
{
    check_init_test_diagnostic_source(test, "int a[1]={1,2};\nint f(void){return *1;}\n", BUSTER_CHECK_NONE, false, true);
    check_init_test_diagnostic_source(test, "char a[1]=\"long\";\nint f(void){return *1;}\n", BUSTER_CHECK_NONE, false, true);
    check_init_test_diagnostic_source(test, "struct S{int *p;} a={3};\nint f(void){return *1;}\n", 1, false, false);
    check_init_test_diagnostic_source(test, "int a[1]={1,2};\nint z;double z;\n", BUSTER_CHECK_NONE, true, true);
    check_init_test_diagnostic_source(test, "int a[1]={1,2};\nint f(void){return missing;}\n", BUSTER_CHECK_NONE, true, true);
    check_init_test_diagnostic_source(test, "int f(void){int a[1]={1,2};return *1;}\n", BUSTER_CHECK_NONE, false, true);
    check_init_test_diagnostic_source(test, "struct S{int *p;} a={3};\nint z;double z;\n", 1, true, false);
    check_init_test_diagnostic_source(test, "struct S{int *p;} a={3};\nint f(void){return missing;}\n", 1, true, false);
    check_init_test_diagnostic_source(test, "int f(void){return *1;}\nint a[1]={1,2};\n", BUSTER_CHECK_NONE, false, false);
    check_init_test_diagnostic_source(test, "int a[1]={1};\nint f(void){return *1;}\n", BUSTER_CHECK_NONE, false, false);
    check_init_test_diagnostic_source(test, "int a[1]={1,*1};\n", BUSTER_CHECK_NONE, false, false);
    check_init_test_diagnostic_source(test, "int a[1]={1,2};int b[1]={3,4};\nint f(void){return missing;}\n", BUSTER_CHECK_NONE, true, 2);
    check_init_test_diagnostic_source(test, "struct S{int a[1];int b[1];int *p;} first={{1,2},{3,4},5};\nint f(void){return missing;}\n", 13, true, 2);
    check_init_test_diagnostic_source(test, "int a[3][1]={[0 ... 2]={1,2}};\nint f(void){return missing;}\n", BUSTER_CHECK_NONE, true, 1);
    // Excess initializers are a Clang extension warning by default. Use a hard conversion error for the
    // independent first-error oracle, while retaining the requested excess-element ordering fixtures above.
    static const char* sources[] = {
        "struct S{int *p;} a={3};\nint f(void){return *1;}\n",
        "struct S{int *p;} a={3};\nint z;double z;\n",
        "int f(void){return *1;}\nstruct S{int *p;} a={3};\n",
    };
    char directory[] = "/tmp/buster-initializer-order-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(sources)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    const char* mode = "-std=gnu17";
    ready = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(sources), outcomes);
    check_test_expect(test, ready, "independent Clang first-error ordering oracle runs");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        check_test_expect(test, outcomes[i].rejected && outcomes[i].line == 1, "Clang reports the earlier phase's error first");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

#endif
