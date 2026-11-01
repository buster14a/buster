// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original diagnostic policy oracle. Replay decoded directives into a dense severity table independently of
// the production predecessor chains, then compare source positions and default-ignored warning queries.
// Include after c_checker_test.c and c_check_diagnostic.c.

#ifndef BUSTER_CHECK_DIAGNOSTIC_TEST_IMPLEMENTATION
#define BUSTER_CHECK_DIAGNOSTIC_TEST_IMPLEMENTATION 1

static void check_test_diagnostic_replay(CheckTest* test)
{
    static const char* const options[] =
    {
        "-Wincompatible-pointer-types-discards-qualifiers",
        "-Wgnu-folding-constant",
        "-Wenum-too-large",
        "-Winteger-overflow",
        "-Wshift-count-negative",
        "-Wshift-count-overflow",
        "-Wshift-negative-value",
        "-Wshift-overflow",
        "-Wexcess-initializers",
        "-Wunknown-pragmas",
        "-Wignored-attributes",
        "-Wincompatible-pointer-types",
        "-Wgnu",
        "-Wpedantic",
        "-Weverything",
        "-Wpragmas",
        "-Wattributes",
    };
    // Independently stated membership: each exact option owns one bit, followed by the six group masks.
    static const uint16_t groups[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 1, 2, 2, 2047, 512, 1024 };
    CheckInputDiagnosticPragma records[384] = { 0 };
    CheckInputDiagnosticPragmas history =
    {
        .records = records,
        .final_state = BUSTER_CHECK_NONE,
    };
    CheckInput input = { .diagnostic_pragmas = &history };
    int8_t expected[129][11];
    int8_t current[11];
    int8_t stack[384][11];
    uint32_t saved_states[384];
    uint32_t saved_records[384];
    uint32_t random = 133;
    memset(current, -1, sizeof(current));
    memcpy(expected[0], current, sizeof(current));
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(records); i += 1)
    {
        random = random * 1664525u + 1013904223u;
        CheckInputDiagnosticPragma* record = records + i;
        *record = (CheckInputDiagnosticPragma)
        {
            .token = i / 3 + 1,
            .state_before = history.final_state,
            .matching_push = BUSTER_CHECK_NONE,
        };
        const uint32_t kind = (random >> 12) % 6;
        if (kind == 0)
        {
            record->operation = CHECK_DIAGNOSTIC_PUSH;
            const uint32_t depth = history.unmatched_pushes;
            memcpy(stack[depth], current, sizeof(current));
            saved_states[depth] = history.final_state;
            saved_records[depth] = i;
            history.unmatched_pushes += 1;
            history.maximum_depth = history.unmatched_pushes > history.maximum_depth ? history.unmatched_pushes : history.maximum_depth;
        }
        else if (kind == 1)
        {
            record->operation = CHECK_DIAGNOSTIC_POP;
            if (history.unmatched_pushes != 0)
            {
                history.unmatched_pushes -= 1;
                const uint32_t depth = history.unmatched_pushes;
                history.final_state = saved_states[depth];
                record->matching_push = saved_records[depth];
                memcpy(current, stack[depth], sizeof(current));
            }
            else
            {
                history.unmatched_pops += 1;
            }
        }
        else
        {
            const uint32_t group = (random >> 20) % BUSTER_ARRAY_LENGTH(options);
            const uint32_t severity = (random >> 4) % 4;
            record->operation = (uint8_t)(CHECK_DIAGNOSTIC_IGNORED + severity);
            record->option = (String8)
            {
                .pointer = (char*)options[group],
                .length = strlen(options[group]),
            };
            for (uint32_t bit = 0; bit < BUSTER_ARRAY_LENGTH(current); bit += 1)
            {
                if ((groups[group] & (1u << bit)) != 0)
                {
                    current[bit] = (int8_t)severity;
                }
            }
            history.final_state = i;
        }
        record->state_after = history.final_state;
        history.count += 1;
        memcpy(expected[record->token], current, sizeof(current));
    }
    for (uint32_t token = 0; token < BUSTER_ARRAY_LENGTH(expected); token += 1)
    {
        for (uint32_t bit = 0; bit < BUSTER_ARRAY_LENGTH(current); bit += 1)
        {
            for (uint32_t ignored = 0; ignored < 2; ignored += 1)
            {
                const CheckWarningSeverity fallback = ignored != 0 ? CHECK_WARNING_IGNORE : CHECK_WARNING_WARNING;
                const CheckWarningSeverity wanted = expected[token][bit] < 0 ? fallback : (CheckWarningSeverity)expected[token][bit];
                const CheckWarningSeverity actual = check_diagnostic_severity_default(&input, token, 1u << bit, ignored != 0);
                check_test_expect(test, actual == wanted, "diagnostic policy agrees with independent dense push/pop and warning-group replay");
            }
        }
    }
}

static void check_test_diagnostic_sources(CheckTest* test)
{
    CheckInput input = { 0 };
    uint32_t first = 0;
    check_test_expect(test, !check_diagnostic_first_error(&input, NULL, &first) && first == BUSTER_CHECK_NONE,
                      "empty diagnostic input has no promoted error");
    check_test_expect(test, check_diagnostic_severity_default(&input, 0, CHECK_WARNING_GNU_CONSTANT, true) == CHECK_WARNING_IGNORE &&
                           check_diagnostic_severity(&input, 0, CHECK_WARNING_INTEGER_OVERFLOW) == CHECK_WARNING_WARNING,
                      "absent policy preserves default-ignored and default-enabled warnings");
    uint8_t source_warnings[6] = { 0 };
    uint8_t source_ignored[6] = { 0 };
    uint32_t tokens[] = { 5, 2, 1 };
    uint32_t graph_warnings[] = { CHECK_WARNING_INTEGER_OVERFLOW, CHECK_WARNING_INTEGER_OVERFLOW, CHECK_WARNING_INTEGER_OVERFLOW };
    uint32_t graph_ignored[] = { CHECK_WARNING_INTEGER_OVERFLOW, CHECK_WARNING_INTEGER_OVERFLOW, 0 };
    source_warnings[3] = CHECK_WARNING_GNU_CONSTANT;
    source_ignored[3] = CHECK_WARNING_GNU_CONSTANT;
    source_warnings[4] = CHECK_WARNING_INTEGER_OVERFLOW;
    source_ignored[4] = CHECK_WARNING_INTEGER_OVERFLOW;
    input.constant_warnings = source_warnings;
    input.constant_warning_ignored = source_ignored;
    input.token_count = BUSTER_ARRAY_LENGTH(source_warnings);
    input.graph.tokens = tokens;
    input.graph.count = BUSTER_ARRAY_LENGTH(tokens);
    CheckStream stream =
    {
        .count = BUSTER_ARRAY_LENGTH(tokens),
        .warnings = graph_warnings,
        .warning_ignored = graph_ignored,
    };
    check_test_expect(test, !check_diagnostic_first_error(&input, &stream, &first) && first == BUSTER_CHECK_NONE,
                      "warnings in source and graph remain warnings without explicit promotion");
    CheckInputDiagnosticPragmas history = { 0 };
    input.diagnostic_pragmas = &history;
    check_test_expect(test, !check_diagnostic_first_error(&input, &stream, &first) && first == BUSTER_CHECK_NONE &&
                           check_diagnostic_severity_default(&input, 3, CHECK_WARNING_GNU_CONSTANT, true) == CHECK_WARNING_IGNORE,
                      "an empty decoded history has the same defaults as an absent history");
    CheckInputDiagnosticPragma records[] =
    {
        {
            .option =
            {
                .pointer = "-Winteger-overflow",
                .length = sizeof("-Winteger-overflow") - 1,
            },
            .token = 2,
            .state_before = BUSTER_CHECK_NONE,
            .state_after = 0,
            .operation = CHECK_DIAGNOSTIC_ERROR,
        },
        {
            .option =
            {
                .pointer = "-Weverything",
                .length = sizeof("-Weverything") - 1,
            },
            .token = 4,
            .state_before = 0,
            .state_after = 1,
            .operation = CHECK_DIAGNOSTIC_IGNORED,
        },
    };
    history.records = records;
    history.count = 1;
    history.final_state = 0;
    check_test_expect(test, check_diagnostic_first_error(&input, NULL, &first) && first == 4,
                      "explicit promotion overrides a source warning's default-ignored mask");
    check_test_expect(test, check_diagnostic_first_error(&input, &stream, &first) && first == 2,
                      "graph promotion selects source order independently of graph node order");
    graph_warnings[1] = 0;
    check_test_expect(test, check_diagnostic_first_error(&input, &stream, &first) && first == 4,
                      "an earlier source warning wins over a later graph warning");
    graph_warnings[1] = CHECK_WARNING_INTEGER_OVERFLOW;
    input.constant_warning_ignored = NULL;
    stream.warning_ignored = NULL;
    check_test_expect(test, check_diagnostic_first_error(&input, &stream, &first) && first == 2,
                      "optional default-ignored columns do not change explicit warning promotion");
    history.count = 2;
    history.final_state = 1;
    check_test_expect(test, check_diagnostic_first_error(&input, &stream, &first) && first == 2,
                      "later group suppression cannot erase an earlier promoted graph warning");
    graph_warnings[1] = 0;
    check_test_expect(test, !check_diagnostic_first_error(&input, &stream, &first) && first == BUSTER_CHECK_NONE,
                      "later group suppression applies to both source and graph warning producers");
    history.count = 1;
    history.final_state = 0;
    records[0].operation = CHECK_DIAGNOSTIC_FATAL;
    graph_warnings[1] = CHECK_WARNING_INTEGER_OVERFLOW;
    check_test_expect(test, check_diagnostic_first_error(&input, &stream, &first) && first == 2 &&
                           check_diagnostic_severity(&input, 2, CHECK_WARNING_INTEGER_OVERFLOW) == CHECK_WARNING_FATAL,
                      "fatal promotion participates in the same earliest-error selection");
}

static void check_test_warning_policy_prefix(CheckTest* test)
{
    static const struct
    {
        uint32_t id;
        const char* first;
        const char* second;
        CheckWarningSeverity expected;
    } cases[] = {
        { CHECK_WARNING_ID_KR_DEFINITION, "-Wno-deprecated-non-prototype", NULL, CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_NON_PROTOTYPE_CALL, "-Werror=deprecated-non-prototype", NULL, CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_KR_TOO_FEW, "-Wno-deprecated-non-prototype", NULL, CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_KR_TOO_MANY, "-Werror", NULL, CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_KR_DEFINITION, "-Werror", "-Wno-error=deprecated-non-prototype", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_KR_DEFINITION, "-Wno-error=deprecated-non-prototype", "-Werror", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_KR_DEFINITION, "-Werror=deprecated-non-prototype", "-Wno-error", CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_KR_DEFINITION, "-Werror=deprecated-non-prototype", "-Wno-deprecated-non-prototype", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_KR_DEFINITION, "-Wno-deprecated-non-prototype", "-Werror=deprecated-non-prototype", CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_KR_DEFINITION, "-pedantic-errors", NULL, CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_KR_DEFINITION, "-w", "-Werror=deprecated-non-prototype", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_KR_TOO_FEW, "-Wno-everything", NULL, CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_VISIBILITY_PRAGMA, "-Wno-pragmas", NULL, CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_VISIBILITY_PRAGMA, "-Wno-visibility", NULL, CHECK_WARNING_WARNING },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const char* options[] = { cases[i].first, cases[i].second };
        const PPOptions command = { .semantic_warning_options = options, .semantic_warning_option_count = cases[i].second != NULL ? 2 : 1 };
        check_test_expect(test, check_warning_command_line(&command, cases[i].id, false, false).severity == cases[i].expected,
                          "shared warning policy preserves the independently observed Clang group and promotion rules");
    }
    const char* hidden[] = { "-Wno-system-headers" };
    PPOptions programmatic = { .system_header_warnings = true };
    check_test_expect(test, check_warning_command_line(&programmatic, CHECK_WARNING_ID_KR_DEFINITION, false, false).system_headers,
                      "a programmatic system-header warning policy is retained without a CLI option slice");
    programmatic.semantic_warning_options = hidden;
    programmatic.semantic_warning_option_count = BUSTER_ARRAY_LENGTH(hidden);
    check_test_expect(test, !check_warning_command_line(&programmatic, CHECK_WARNING_ID_KR_DEFINITION, false, false).system_headers,
                      "an ordered visibility option overrides the programmatic baseline");
    Arena arena = arena_reserve(UINT64_C(1) << 20);
    char* ordered[] = { "-Wno-everything", "-include", "-Werror=deprecated-non-prototype", "-Wdeprecated-non-prototype", "source.c", "-o", "-Werror", "-pedantic-errors" };
    const char* input = NULL;
    const char* output = NULL;
    bool markers = false;
    PPOptions parsed = { 0 };
    const bool ready = pp_parse_options(BUSTER_ARRAY_LENGTH(ordered), ordered, &parsed, &arena, &input, &output, &markers);
    check_test_expect(test, ready && parsed.semantic_warning_option_count == 3 &&
                           strcmp(parsed.semantic_warning_options[0], "-Wno-everything") == 0 &&
                           strcmp(parsed.semantic_warning_options[1], "-Wdeprecated-non-prototype") == 0 &&
                           strcmp(parsed.semantic_warning_options[2], "-pedantic-errors") == 0 &&
                           check_warning_command_line(&parsed, CHECK_WARNING_ID_KR_DEFINITION, false, false).severity == CHECK_WARNING_WARNING,
                      "shared CLI capture preserves option order and excludes include/output filenames");
    arena_release(&arena);
}

// Clang's argument-outside-range is a DefaultError warning whose baseline the command line maps (#1080). The
// ordered mappings follow the same rules as the ordinary warnings above, starting from an error instead of a warning.
static void check_test_warning_policy_x86_range(CheckTest* test)
{
    static const struct
    {
        const char* options[3];
        uint32_t count;
        CheckWarningSeverity expected;
    } cases[] =
    {
        { { NULL }, 0, CHECK_WARNING_ERROR },
        { { "-Wno-error=argument-outside-range" }, 1, CHECK_WARNING_WARNING },
        { { "-Werror", "-Wno-error=argument-outside-range" }, 2, CHECK_WARNING_WARNING },
        { { "-Wno-error=argument-outside-range", "-Werror" }, 2, CHECK_WARNING_WARNING },
        { { "-Wno-argument-outside-range" }, 1, CHECK_WARNING_IGNORE },
        { { "-Werror=argument-outside-range" }, 1, CHECK_WARNING_ERROR },
        { { "-Wno-argument-outside-range", "-Wargument-outside-range" }, 2, CHECK_WARNING_WARNING },
        { { "-Wno-everything" }, 1, CHECK_WARNING_IGNORE },
        { { "-Weverything" }, 1, CHECK_WARNING_ERROR },
        { { "-Wno-everything", "-Wargument-outside-range" }, 2, CHECK_WARNING_WARNING },
        { { "-Werror", "-Wno-everything" }, 2, CHECK_WARNING_IGNORE },
        { { "-Weverything", "-Wno-error=argument-outside-range" }, 2, CHECK_WARNING_WARNING },
        { { "-Wno-argument-outside-range", "-Werror" }, 2, CHECK_WARNING_IGNORE },
        { { "-Wno-argument-outside-range", "-Werror=argument-outside-range" }, 2, CHECK_WARNING_ERROR },
        { { "-Werror=argument-outside-range", "-Wno-error" }, 2, CHECK_WARNING_ERROR },
        { { "-Werror", "-Wno-error=argument-outside-range", "-Werror" }, 3, CHECK_WARNING_WARNING },
        { { "-w", "-Werror=argument-outside-range" }, 2, CHECK_WARNING_IGNORE },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const PPOptions command = { .semantic_warning_options = cases[i].options, .semantic_warning_option_count = cases[i].count };
        check_test_expect(test, check_warning_command_line(&command, CHECK_WARNING_ID_ARGUMENT_OUTSIDE_RANGE, false, true).severity == cases[i].expected,
                          "x86 range warning command-line mapping starts from its DefaultError baseline");
    }
    const PPOptions empty = { 0 };
    check_test_expect(test, check_warning_command_line(&empty, CHECK_WARNING_ID_ARGUMENT_OUTSIDE_RANGE, false, false).severity == CHECK_WARNING_WARNING,
                      "an ordinary warning default for the same ID starts from a warning");
    CheckInput input = { .x86_range_policy = true, .x86_range_severities = { CHECK_WARNING_WARNING, CHECK_WARNING_WARNING } };
    check_test_expect(test, check_diagnostic_range_severity(&input, 0, true) == CHECK_WARNING_WARNING,
                      "the checked input's x86 range policy snapshot replaces the default error");
    CheckInputDiagnosticPragma record =
    {
        .option = { .pointer = "-Wargument-outside-range", .length = sizeof("-Wargument-outside-range") - 1 },
        .token = 0,
        .state_before = BUSTER_CHECK_NONE,
        .state_after = 0,
        .operation = CHECK_DIAGNOSTIC_ERROR,
    };
    CheckInputDiagnosticPragmas history = { .records = &record, .count = 1, .final_state = 0 };
    input.x86_range_severities[1] = CHECK_WARNING_IGNORE;
    input.diagnostic_pragmas = &history;
    check_test_expect(test, check_diagnostic_range_severity(&input, 0, true) == CHECK_WARNING_ERROR,
                      "a source pragma overrides the x86 range warning's command-line snapshot");
    input.x86_range_quiet = true;
    check_test_expect(test, check_diagnostic_range_severity(&input, 0, true) == CHECK_WARNING_IGNORE,
                      "-w suppresses the x86 range warning over a source error pragma");
}

static __attribute__((noinline)) void check_test_diagnostic_policy(CheckTest* test)
{
    check_test_warning_policy_prefix(test);
    check_test_warning_policy_x86_range(test);
    check_test_diagnostic_replay(test);
    check_test_diagnostic_sources(test);
}

#endif
