// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original command-line policy controls checked against Clang 23.1.4. Each pair states a visible result; the tests
// deliberately distinguish Everything's enabling fallback, ordered group mappings and global error promotion.

static void check_test_warning_policy(CheckTest* test)
{
    static const struct
    {
        uint32_t id;
        const char* first;
        const char* second;
        CheckWarningSeverity expected;
    } cases[] = {
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wno-literal-range", "-Weverything", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Weverything", "-Wno-literal-range", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wno-everything", "-Wliteral-range", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wliteral-range", "-Wno-everything", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wno-everything", "-Weverything", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Weverything", "-Wno-everything", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wno-everything", "-Werror=literal-range", CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Werror=literal-range", "-Wno-everything", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wno-error=literal-range", "-Werror", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Werror", "-Wno-error=literal-range", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Werror=literal-range", "-Wno-error", CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wno-error", "-Werror=literal-range", CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Werror=literal-range", "-Wno-literal-range", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-Wno-literal-range", "-Werror=literal-range", CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER, "-Wno-gnu-variable-sized-type-not-at-end", "-Wgnu", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_VARIABLE_SIZED_MEMBER, "-Wgnu", "-Wno-gnu-variable-sized-type-not-at-end", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_MISSING_DECLARATIONS, "-pedantic-errors", "-Wno-error=missing-declarations", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_MISSING_DECLARATIONS, "-Wno-error=missing-declarations", "-pedantic-errors", CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_MISSING_DECLARATIONS, "-Werror=pedantic", NULL, CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_ORDERED_POINTER_INTEGER, "-Werror=pedantic", NULL, CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_PLAIN_COMPLEX, "-Wno-pedantic", NULL, CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_PLAIN_COMPLEX, "-pedantic-errors", NULL, CHECK_WARNING_ERROR },
        { CHECK_WARNING_ID_ORDERED_POINTER_INTEGER, "-Wno-pointer-integer-compare", NULL, CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_C23_PARAMETER, "-Wno-c2x-extensions", NULL, CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_NON_LITERAL_NULL, "-Wno-conversion", NULL, CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_INITIALIZER_OVERRIDES, "-Wno-override-init", NULL, CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_INITIALIZER_OVERRIDES, "-Wno-extra", NULL, CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_STATIC_LOCAL_INLINE, "-Wno-static-in-inline", NULL, CHECK_WARNING_WARNING },
        { CHECK_WARNING_ID_LITERAL_RANGE, "-w", "-Werror=literal-range", CHECK_WARNING_IGNORE },
        { CHECK_WARNING_ID_PLAIN_COMPLEX, "-Wno-everything", "-Werror=pedantic", CHECK_WARNING_IGNORE },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        const char* arguments[] = { cases[i].first, cases[i].second };
        const PPOptions options = {
            .semantic_warning_options = arguments,
            .semantic_warning_option_count = cases[i].second != NULL ? 2 : 1,
        };
        const CheckWarningCommandLine actual = check_warning_command_line(&options, cases[i].id, false);
        check_test_expect(test, actual.severity == cases[i].expected, "sparse warning options follow independently observed Clang group and global policy");
    }
    const PPOptions empty = { 0 };
    for (uint32_t id = CHECK_WARNING_ID_NON_LITERAL_NULL; id < CHECK_WARNING_ID_COUNT; id += 1)
    {
        const CheckWarningCommandLine enabled = check_warning_command_line(&empty, id, false);
        const CheckWarningCommandLine ignored = check_warning_command_line(&empty, id, true);
        check_test_expect(test, enabled.severity == CHECK_WARNING_WARNING && ignored.severity == CHECK_WARNING_IGNORE,
                          "event defaults remain independent of every high policy ID");
    }
    const char* arguments[] = { "-Wsystem-headers", "-Wno-system-headers" };
    PPOptions options = { .semantic_warning_options = arguments, .semantic_warning_option_count = 1 };
    check_test_expect(test, check_warning_command_line(&options, CHECK_WARNING_ID_LITERAL_RANGE, false).system_headers,
                      "source warning visibility can explicitly include system headers");
    options.semantic_warning_option_count = 2;
    check_test_expect(test, !check_warning_command_line(&options, CHECK_WARNING_ID_LITERAL_RANGE, false).system_headers,
                      "the last system-header visibility option controls the sparse warnings");
    Arena arena = arena_reserve(UINT64_C(1) << 20);
    char* ordered[] = { "-Wno-everything", "-include", "-Werror=literal-range", "-Wliteral-range", "source.c", "-o", "-Werror", "-pedantic-errors" };
    const char* input = NULL;
    const char* output = NULL;
    bool markers = false;
    PPOptions parsed = { 0 };
    const bool captured = pp_parse_options(BUSTER_ARRAY_LENGTH(ordered), ordered, &parsed, &arena, &input, &output, &markers);
    check_test_expect(test, captured && parsed.semantic_warning_option_count == 3 &&
                           strcmp(parsed.semantic_warning_options[0], "-Wno-everything") == 0 &&
                           strcmp(parsed.semantic_warning_options[1], "-Wliteral-range") == 0 &&
                           strcmp(parsed.semantic_warning_options[2], "-pedantic-errors") == 0 &&
                           check_warning_command_line(&parsed, CHECK_WARNING_ID_LITERAL_RANGE, false).severity == CHECK_WARNING_WARNING,
                      "actual CLI capture keeps option order and never mistakes an include or output filename for warning policy");
    arena_release(&arena);
}
