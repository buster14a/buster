// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Diagnostic-only control scans retain real input/expression failures and never publish partial programs.
#ifndef BUSTER_C_CHECK_PROGRAM_DIAGNOSTIC_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_DIAGNOSTIC_TEST_INCLUDED

static uint32_t check_test_program_diagnostic(CheckTest* test, const char* source, uint8_t role, uint32_t occurrence, bool input_error)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                              pp_run_source(&pp, "program-order.c", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "control diagnostic source preprocesses");
    uint32_t line = 0;
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 22);
        Arena stream_arena = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 22);
        const ParseOptions parse_options = { .scalar = path == 0 };
        bool ready = check_test_types_open(&types) && parse_initialize(&parser, &parse_options, pp_options.language) &&
                     parse_translation_unit(&parser, &pp) && parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE;
        CheckInput input = { 0 };
        if (ready)
        {
            const bool accepted = check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch);
            ready = !input.resource && (accepted || input.diagnostic_graph);
        }
        CheckStream stream = { 0 };
        const CheckOptions options = { .scalar = path == 0 };
        ready = ready && check_graph_run(&types.table, &input.graph, &stream_arena, &scratch, &options, &stream);
        uint8_t* prefix = ready ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, 64) : 0;
        ready = ready && prefix != 0;
        check_test_expect(test, ready && (input.report_count != 0) == input_error && (input_error || stream.report_count != 0),
                          "actual control fragment retains its later phase failure");
        if (ready)
        {
            memset(prefix, 0xa5, 64);
            uint32_t expected = BUSTER_CHECK_NONE;
            uint32_t seen = 0;
            for (uint32_t token = 0; token < input.token_count; token += 1)
            {
                if (parser.tree.roles[token] == role)
                {
                    seen += 1;
                    expected = seen == occurrence ? token : expected;
                }
            }
            const uint32_t control = expected;
            for (uint32_t token = 0; token < control && token < input.token_count; token += 1)
            {
                if (parser.tree.parents[token] == control &&
                    (parser.tree.roles[token] == PARSE_ROLE_OPEN_CASE || parser.tokens.kinds[token] == PARSE_KIND_SWITCH))
                {
                    expected = parser.tokens.kinds[token] == PARSE_KIND_CASE ? token + 1 : token;
                }
            }
            const CheckStream before = stream;
            const CheckInput input_before = input;
            CheckProgramDiagnostic diagnostic = { 0 };
            const bool diagnosed = check_program_diagnose(&diagnostic, &input, &stream, &parser, &types.table, BUSTER_CHECK_NONE, &scratch);
            char description[240];
            snprintf(description, sizeof(description), "control ordering %.70s: expected %u, got %u/status %u on %s path", source,
                     expected, diagnostic.token, diagnostic.status, path == 0 ? "scalar" : "vector");
            check_test_expect(test, diagnosed && diagnostic.token == expected && diagnostic.status ==
                                   (expected == BUSTER_CHECK_NONE ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID), description);
            // The first diagnostic the driver publishes: the control diagnostic at its position, or a report at its
            // token, whichever comes first. A missing name in a switch's statements comes before the switch's end, where
            // its duplicate case is published (#1077); any other report comes after the control diagnostic.
            uint32_t reported = diagnostic.token;
            uint32_t published = diagnostic.token != BUSTER_CHECK_NONE ? diagnostic.position : BUSTER_CHECK_NONE;
            for (uint32_t i = 0; i < input.report_count; i += 1)
            {
                reported = input.reports[i].token < published ? input.reports[i].token : reported;
                published = input.reports[i].token < published ? input.reports[i].token : published;
            }
            for (uint32_t i = 0; i < stream.report_count; i += 1)
            {
                reported = stream.reports[i].token < published ? stream.reports[i].token : reported;
                published = stream.reports[i].token < published ? stream.reports[i].token : published;
            }
            const char* file = 0;
            uint32_t length = 0;
            const uint32_t actual_line = reported < parser.tokens.count ? pp_presumed(&pp, pp.output.locations[parser.tokens.sources[reported]], &file, &length) : 0;
            if (path == 0)
            {
                line = actual_line;
            }
            else
            {
                check_test_expect(test, line == actual_line, "scalar/vector diagnostic readiness and displayed source line agree");
            }
            bool untouched = scratch.length == 64 && memcmp(&before, &stream, sizeof(before)) == 0 && memcmp(&input_before, &input, sizeof(input)) == 0;
            for (uint32_t i = 0; i < 64; i += 1)
            {
                untouched = untouched && prefix[i] == 0xa5;
            }
            check_test_expect(test, untouched, "control diagnostic preserves actual reports, metadata and scratch prefix");
            CheckProgram rejected = { 0 };
            const uint64_t output_mark = stream_arena.length;
            check_test_expect(test, !check_program(&rejected, &parser, &input, &stream, &types.table, (CheckProgramOptions){ 0 }, &stream_arena, &scratch) &&
                                   rejected.status == CHECK_PROGRAM_INVALID && rejected.controls == 0 && rejected.body_count == 0 &&
                                   stream_arena.length == output_mark && scratch.length == 64,
                              "strict program publication rejects the actual erroneous unit");
            if (expected != BUSTER_CHECK_NONE)
            {
                CheckProgramDiagnostic limited = { 0 };
                check_test_expect(test, check_program_diagnose(&limited, &input, &stream, &parser, &types.table, expected, &scratch) &&
                                       limited.status == CHECK_PROGRAM_OK && limited.token == BUSTER_CHECK_NONE,
                                  "exclusive earlier cutoff excludes the conflicting source label");
                Arena tiny = scratch;
                tiny.capacity = tiny.length;
                CheckProgramDiagnostic exhausted = { 0 };
                check_test_expect(test, !check_program_diagnose(&exhausted, &input, &stream, &parser, &types.table, BUSTER_CHECK_NONE, &tiny) &&
                                       exhausted.status == CHECK_PROGRAM_RESOURCE && tiny.length == 64,
                                  "control diagnostic capacity failure restores all allocations");
                if (role == PARSE_ROLE_CLOSE_CASE)
                {
                    const uint64_t aligned = (64 + stream.count + _Alignof(CheckProgramCase) - 1) & ~(uint64_t)(_Alignof(CheckProgramCase) - 1);
                    const uint64_t spans = aligned + (uint64_t)seen * sizeof(CheckProgramCase);
                    for (uint32_t stage = 0; stage < 3; stage += 1)
                    {
                        tiny = scratch;
                        tiny.capacity = spans + (uint64_t)input.token_count * sizeof(uint32_t) * stage;
                        check_test_expect(test, !check_program_diagnose(&exhausted, &input, &stream, &parser, &types.table, BUSTER_CHECK_NONE, &tiny) &&
                                               exhausted.status == CHECK_PROGRAM_RESOURCE && tiny.length == 64,
                                          "every statement-readiness queue allocation stage restores scratch");
                    }
                }
                if (role == PARSE_ROLE_CLOSE_CASE && seen == 2 && input.token_nodes[control] != BUSTER_CHECK_NONE)
                {
                    const uint32_t saved = input.token_nodes[control];
                    input.token_nodes[control] = stream.count;
                    CheckProgramDiagnostic missing = { 0 };
                    check_test_expect(test, check_program_diagnose(&missing, &input, &stream, &parser, &types.table, BUSTER_CHECK_NONE, &scratch) &&
                                           missing.status == CHECK_PROGRAM_OK && missing.token == BUSTER_CHECK_NONE,
                                      "an incomplete case root cannot manufacture a duplicate or default diagnostic");
                    input.token_nodes[control] = saved;
                }
            }
        }
        arena_release(&scratch);
        arena_release(&stream_arena);
        arena_release(&input_arena);
        check_test_types_close(&types);
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    return line;
}

static void check_test_program_case_intervals(CheckTest* test)
{
    CheckTestTypes types = { 0 };
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const bool ready = check_test_types_open(&types) && scratch.base != 0;
    check_test_expect(test, ready, "case interval oracle storage is available");
    for (uint32_t fixture = 0; ready && fixture < 256; fixture += 1)
    {
        CheckProgramCase cases[64];
        const uint32_t count = fixture % BUSTER_ARRAY_LENGTH(cases);
        uint32_t expected = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t owner = check_test_random(test) % 4;
            const int32_t lower = (int32_t)(check_test_random(test) % 64) - 32;
            const bool range = check_test_random(test) % 2 != 0;
            const int32_t upper = lower + (range ? (int32_t)(check_test_random(test) % 16) - 2 : 0);
            cases[i] = (CheckProgramCase){
                .lower = (uint32_t)lower,
                .upper = (uint32_t)upper,
                .type = owner & 1 ? BUSTER_TYPE_INT : BUSTER_TYPE_UINT,
                .token = i * 7 + 1,
                .owner = owner,
                .fallback = check_test_random(test) % 8 == 0,
                .range = range,
            };
            const int64_t a = owner & 1 ? lower : (int64_t)(uint32_t)lower;
            const int64_t b = owner & 1 ? upper : (int64_t)(uint32_t)upper;
            for (uint32_t j = 0; j < i && expected == BUSTER_CHECK_NONE; j += 1)
            {
                const int64_t c = owner & 1 ? (int32_t)(uint32_t)cases[j].lower : (int64_t)(uint32_t)cases[j].lower;
                const int64_t d = owner & 1 ? (int32_t)(uint32_t)cases[j].upper : (int64_t)(uint32_t)cases[j].upper;
                const bool duplicate = cases[i].fallback && cases[j].fallback;
                const bool overlaps = !cases[i].fallback && !cases[j].fallback && a <= b && c <= d && a <= d && c <= b;
                if (cases[j].owner == owner && (duplicate || overlaps))
                {
                    expected = cases[i].token;
                }
            }
        }
        const uint32_t ends[] = { 1000, 1001, 1002, 1003 };
        CheckProgram program = { .types = &types.table, .cases = cases, .case_count = count, .token = BUSTER_CHECK_NONE, .diagnostic_position = BUSTER_CHECK_NONE };
        check_program_case_conflicts(&program, 0, ends, &scratch);
        check_test_expect(test, (program.token == BUSTER_CHECK_NONE) == (expected == BUSTER_CHECK_NONE) &&
                               program.status == (expected == BUSTER_CHECK_NONE ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID) &&
                               scratch.length == 0, "ordered case constraints agree with independent signed/unsigned all-pairs acceptance oracle");
        if (fixture == 63)
        {
            for (uint32_t stage = 0; stage < 2; stage += 1)
            {
                Arena tiny = scratch;
                tiny.capacity = (uint64_t)count * sizeof(uint32_t) * stage;
                CheckProgram failed = { .types = &types.table, .cases = cases, .case_count = count, .token = BUSTER_CHECK_NONE, .diagnostic_position = BUSTER_CHECK_NONE };
                check_program_case_conflicts(&failed, 0, ends, &tiny);
                check_test_expect(test, failed.status == CHECK_PROGRAM_RESOURCE && tiny.length == 0,
                                  "every interval-index allocation stage fails atomically");
            }
        }
    }
    arena_release(&scratch);
    check_test_types_close(&types);
}

static void check_test_program_diagnostics(CheckTest* test, const char* clang)
{
    check_test_program_case_intervals(test);
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 1:;case 1:;}return *1;}", PARSE_ROLE_CLOSE_CASE, 2, false);
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 1:;case 1:;}return missing;}", PARSE_ROLE_CLOSE_CASE, 2, true);
    check_test_program_diagnostic(test, "int f(unsigned n){switch(n){case -1:;case 4294967295U:;}return *1;}", PARSE_ROLE_CLOSE_CASE, 2, false);
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 1 ... 10:;case 9 ... 12:;case 5 ... 6:;}return *1;}", PARSE_ROLE_CLOSE_CASE, 3, false);
    check_test_program_diagnostic(test, "int f(int n){switch(n){default:;default:;}return *1;}", PARSE_ROLE_CLOSE_CASE, 1, false);
    check_test_program_diagnostic(test, "int f(double n){switch(n){}return *1;}", PARSE_ROLE_CLOSE_CONDITION, 1, false);
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 3 ... 1:;}return *1;}", PARSE_ROLE_CLOSE_CASE, 0, false);
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 1:;}switch(n){case 1:;}return *1;}", PARSE_ROLE_CLOSE_CASE, 0, false);
    check_test_program_diagnostic(test, "int f(int n){int x=*1;switch(n){case 1:;case 1:;}return x;}", PARSE_ROLE_CLOSE_CASE, 0, false);
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 1:;case *1:;}return 0;}", PARSE_ROLE_CLOSE_CASE, 0, false);
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 1:;case 1:;return *1;}return 0;}", PARSE_ROLE_CLOSE_CASE, 0, false);
    // A missing name in a switch's statements leaves its cases, whose duplicate clang reports at the switch's end (#1077).
    check_test_program_diagnostic(test, "int f(int n){switch(n){case 1:;case 1:;return missing;}return 0;}", PARSE_ROLE_CLOSE_CASE, 2, true);
    static const struct
    {
        const char* source;
        uint32_t occurrence;
        uint8_t role;
        bool input_error;
    } fixtures[] = {
        { "int f(int n){switch(n){case 1:;\ncase\n1\n:;\n}return *1;}\n", 2, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ndefault:;\ndefault:;\ndefault:;\n}return *1;}\n", 2, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 100:;\ncase 100:;\ncase 1:;\ncase 1:;\n}return *1;}\n", 4, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 1 ... 10:;\ncase 9 ... 12:;\ncase 5 ... 6:;\n}return *1;}\n", 3, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 5 ... 10:;\ncase 6:;\ncase 1:;\ncase 1:;\n}return *1;}\n", 4, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 1 ... 1:;\ncase 1:;\n}return *1;}\n", 1, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 1:;\ncase 1:;\ndefault:;\ndefault:;\n}return *1;}\n", 3, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 1:;\ncase 1:;\nswitch(n){case 2:;\ncase 2:;}\n}return *1;}\n", 4, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n)case 1:\ncase 1:\nswitch(n){case 2:;\ncase 2:;}\nreturn *1;}\n", 4, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 1:;\ncase 1:;\nreturn *1;\n}return 0;}\n", 0, PARSE_ROLE_CLOSE_CASE, false },
        { "int f(int n){switch(n){\ncase 1:;\ncase 1:;\nreturn missing;\n}return 0;}\n", 2, PARSE_ROLE_CLOSE_CASE, true },
        { "int f(double n){switch(\nn\n){}return *1;}\n", 1, PARSE_ROLE_CLOSE_CONDITION, false },
    };
    char directory[] = "/tmp/buster-control-order-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    uint32_t lines[BUSTER_ARRAY_LENGTH(fixtures)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
        lines[i] = check_test_program_diagnostic(test, fixtures[i].source, fixtures[i].role, fixtures[i].occurrence, fixtures[i].input_error);
    }
    const char* mode = "-std=gnu17";
    ready = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, ready, "independent Clang switch diagnostic oracle completes");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char description[160];
        snprintf(description, sizeof(description), "switch diagnostic fixture %u matches Clang first line %u (ours %u)", i, outcomes[i].line, lines[i]);
        check_test_expect(test, outcomes[i].rejected && lines[i] == outcomes[i].line, description);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

#endif
