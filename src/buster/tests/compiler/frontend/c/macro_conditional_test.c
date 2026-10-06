// GCC/Clang-compatible macro source boundaries: ordinary newline lookahead,
// source conditionals inside arguments, skipped-group diagnostic release, and
// push/pop effects at the rescan cursor. Pin tokens, expansion ownership,
// diagnostic source attribution and dialect-owned phase-one trigraph
// translation in c_trigraph_preprocess_tests.
// c_punctuator_separator_tests pins lexical joins and direct/-E admission parity.
#include <buster/tests/compiler/frontend/c/macro_conditional_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/file.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL UnitTestResult c_macro_conditional_expect_preprocessed(UnitTestArguments* arguments, Arena* arena,
                                                                           CPreprocessResult actual, String8 expected_text)
{
    UnitTestResult result = {0};
    CLexResult expected = c_lex(arena, expected_text);
    u64 actual_index = 0;
    u64 expected_index = 0;
    while (actual_index < actual.token_count && expected_index < expected.token_count)
    {
        while (actual_index < actual.token_count && actual.tokens[actual_index].kind == C_TOKEN_NEWLINE)
        {
            actual_index += 1;
        }
        while (expected_index < expected.token_count && expected.tokens[expected_index].kind == C_TOKEN_NEWLINE)
        {
            expected_index += 1;
        }
        if (actual_index < actual.token_count && expected_index < expected.token_count)
        {
            BUSTER_TEST(arguments, actual.tokens[actual_index].kind == expected.tokens[expected_index].kind);
            BUSTER_STRING_TEST(arguments, c_token_spelling(actual.spelling_base, actual.tokens[actual_index]),
                               c_token_spelling(expected.spelling_base, expected.tokens[expected_index]));
            actual_index += 1;
            expected_index += 1;
        }
    }
    while (actual_index < actual.token_count && actual.tokens[actual_index].kind == C_TOKEN_NEWLINE)
    {
        actual_index += 1;
    }
    while (expected_index < expected.token_count && expected.tokens[expected_index].kind == C_TOKEN_NEWLINE)
    {
        expected_index += 1;
    }
    BUSTER_TEST(arguments, actual_index == actual.token_count);
    BUSTER_TEST(arguments, expected_index == expected.token_count);
    return result;
}

#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL UnitTestResult c_macro_conditional_compare_semantic_tokens(UnitTestArguments* arguments, CLexResult actual, CLexResult expected, String8 diagnostic)
{
    UnitTestResult result = {0};
    u64 actual_count = 0;
    u64 expected_count = 0;
    for (u64 index = 0; index < actual.token_count; index += 1)
    {
        actual_count += actual.tokens[index].kind != C_TOKEN_NEWLINE;
    }
    for (u64 index = 0; index < expected.token_count; index += 1)
    {
        expected_count += expected.tokens[index].kind != C_TOKEN_NEWLINE;
    }
    BUSTER_TEST_RAW(arguments, actual_count == expected_count, diagnostic);
    u64 actual_index = 0;
    u64 expected_index = 0;
    while (actual_index < actual.token_count && expected_index < expected.token_count)
    {
        while (actual_index < actual.token_count && actual.tokens[actual_index].kind == C_TOKEN_NEWLINE)
        {
            actual_index += 1;
        }
        while (expected_index < expected.token_count && expected.tokens[expected_index].kind == C_TOKEN_NEWLINE)
        {
            expected_index += 1;
        }
        if (actual_index < actual.token_count && expected_index < expected.token_count)
        {
            BUSTER_TEST_RAW(arguments, actual.tokens[actual_index].kind == expected.tokens[expected_index].kind, diagnostic);
            BUSTER_TEST_RAW(arguments, string_equal(c_token_spelling(actual.spelling_base, actual.tokens[actual_index]),
                                                   c_token_spelling(expected.spelling_base, expected.tokens[expected_index])), diagnostic);
            actual_index += 1;
            expected_index += 1;
        }
    }
    return result;
}
#endif

// Definition-owned argument demand is reused, never a previous expansion's
// tokens or stamps. Count checks pin both omitted work and once-only prescan.
BUSTER_GLOBAL_LOCAL UnitTestResult c_macro_argument_demand_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 source;
        String8 expected;
        u64 expansions;
        u32 limit;
    } cases[] = {
        // raw-only
        {S8("#define BAD(x,y) x+y\n#define RAW(x) #x\nRAW(BAD(1))\n"), S8("\"BAD(1)\""), 1, 0},
        // unused
        {S8("#define BAD(x,y) x+y\n#define UNUSED(x) 7\nUNUSED(BAD(1))\n"), S8("7"), 1, 0},
        // paste-only
        {S8("#define BAD(x,y) x+y\n#define PREFIX(x) prefix##x\nPREFIX(BAD(1))\n"), S8("prefixBAD(1)"), 1, 0},
        // mixed-stringize
        {S8("#define N 7\n#define MIX(x) #x x x\nMIX(N)\n"), S8("\"N\" 7 7"), 2, 0},
        // mixed-paste
        {S8("#define N 7\n#define MIX(x) x prefix##x\nMIX(N)\n"), S8("7 prefixN"), 2, 0},
        // nested-ordinary
        {S8("#define N 7\n#define ID(x) x\nID(ID(N))\n"), S8("7"), 3, 0},
        // disabled
        {S8("#define SELF a.SELF\n#define ID(x) x\nID(ID(SELF))\n"), S8("a.SELF"), 3, 0},
        // raw-line-file
        {S8("#define RAW(x) #x\n#define UNUSED(x) 7\nRAW(__LINE__) UNUSED(__FILE__)\n"), S8("\"__LINE__\" 7"), 2, 0},
        // empty
        {S8("#define RAW(x) #x\n#define CAT(a,b) a##b\nRAW() CAT(,) CAT(,x) CAT(x,)\n"), S8("\"\" x x"), 4, 0},
        // raw-variadic
        {S8("#define BAD(x,y) x+y\n#define VS(...) #__VA_ARGS__\nVS(BAD(1), BAD(2))\n"), S8("\"BAD(1), BAD(2)\""), 1, 0},
        // ordinary-variadic
        {S8("#define N 7\n#define V(...) __VA_ARGS__\nV(N,N)\n"), S8("7,7"), 3, 0},
        // paste-rescan
        {S8("#define CAT(a,b) a##b\nCAT(LA,TE)\n#define LATE 8\nCAT(LA,TE)\n#undef LATE\nCAT(LA,TE)\n"), S8("LATE 8 LATE"), 4, 0},
        // redefinition
        {S8("#define F(x) #x\n#define N 7\nF(N)\n#undef F\n#define F(x) x\nF(N)\n#undef F\n#define F(x) #x\nF(N)\n"), S8("\"N\" 7 \"N\""), 4, 0},
        // snapshot
        {S8("#define F(x) #x\n#define N 7\n#pragma push_macro(\"F\")\n#undef F\n#define F(x) x\nF(N)\n#pragma pop_macro(\"F\")\nF(N)\n"), S8("7 \"N\""), 3, 0},
        // pragma
        {S8("#define P \"push_macro(\\\"N\\\")\"\n#define Q \"pop_macro(\\\"N\\\")\"\n#define N 7\n_Pragma(P)\n#undef N\n#define N 9\nN\n_Pragma(Q)\nN\n"), S8("9 7"), 6, 0},
        // raw-expansion-budget
        {S8("#define A B\n#define B 9\n#define RAW(x) #x\nRAW(A)\n"), S8("\"A\""), 1, 1},
        // unused-expansion-budget
        {S8("#define A B\n#define B 9\n#define UNUSED(x) 7\nUNUSED(A)\n"), S8("7"), 1, 1},
    };
    CPreprocessDialect dialects[] = {C_PREPROCESS_DIALECT_GNU17, C_PREPROCESS_DIALECT_C17};
    for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialects); dialect_index += 1)
    {
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessResult actual = c_preprocess(temporary.arena, cases[case_index].source,
                                                     (CPreprocessOptions){.source_path = S8("argument-demand.c"),
                                                                          .dialect = dialects[dialect_index],
                                                                          .expansion_limit = cases[case_index].limit});
            CLexResult expected = c_lex(temporary.arena, cases[case_index].expected);
            BUSTER_TEST(arguments, actual.diagnostic_count == 0);
            BUSTER_TEST(arguments, actual.token_count == expected.token_count);
            BUSTER_TEST(arguments, actual.detail->preprocessed.expansions == cases[case_index].expansions);
            for (u64 token_index = 0; token_index < actual.token_count && token_index < expected.token_count; token_index += 1)
            {
                BUSTER_TEST(arguments, actual.tokens[token_index].kind == expected.tokens[token_index].kind);
                BUSTER_STRING_TEST(arguments, c_token_spelling(actual.spelling_base, actual.tokens[token_index]),
                                   c_token_spelling(expected.spelling_base, expected.tokens[token_index]));
            }
            scratch_end(temporary);
        }
    }

    String8 located = S8("#define MIX(x) #x x\n#define N 7\n"
                         "#line 40 \"demand-a.c\"\nMIX(N)\n"
                         "#line 90 \"demand-b.c\"\nMIX(N)\n");
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult actual = c_preprocess(temporary.arena, located, (CPreprocessOptions){.source_path = S8("demand-input.c")});
    BUSTER_TEST(arguments, actual.diagnostic_count == 0);
    if (BUSTER_REQUIRE(arguments, actual.token_count == 5))
    {
        for (u32 index = 0; index < 4; index += 1)
        {
            CSourceLocation location = c_preprocess_token_location(&actual, actual.tokens[index]);
            BUSTER_TEST(arguments, location.line == (index < 2 ? 40u : 90u));
            BUSTER_TEST(arguments, location.column == 1);
            if (BUSTER_REQUIRE(arguments, location.file < actual.file_count))
            {
                BUSTER_STRING_TEST(arguments, actual.files[location.file], index < 2 ? S8("demand-a.c") : S8("demand-b.c"));
            }
        }
    }
    scratch_end(temporary);

    // Any ordinary use still requires prescan, even beside # or ##. The
    // error belongs to the nested invocation, not to an earlier expansion.
    String8 invalid[] = {
        S8("#define BAD(x,y) x+y\n#define MIX(x) #x x\n#line 80 \"needed.c\"\nMIX(BAD(1))\n"),
        S8("#define BAD(x,y) x+y\n#define MIX(x) prefix##x x\n#line 80 \"needed.c\"\nMIX(BAD(1))\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        temporary = scratch_begin(&arguments->arena, 1);
        actual = c_preprocess(temporary.arena, invalid[index], (CPreprocessOptions){.source_path = S8("demand-input.c")});
        if (BUSTER_REQUIRE(arguments, actual.diagnostic_count == 1))
        {
            CDiagnostic diagnostic = actual.diagnostics[0];
            BUSTER_TEST(arguments, diagnostic.kind == C_DIAGNOSTIC_INVALID_MACRO_INVOCATION);
            BUSTER_TEST(arguments, diagnostic.location.line == 80);
            BUSTER_TEST(arguments, diagnostic.location.column == 5);
            if (BUSTER_REQUIRE(arguments, diagnostic.location.file < actual.file_count))
            {
                BUSTER_STRING_TEST(arguments, actual.files[diagnostic.location.file], S8("needed.c"));
            }
        }
        scratch_end(temporary);
    }
    return result;
}

// Ordinary whitespace lookahead and macro-state effects share the same rescan
// cursor. Pin exact tokens independently of parsing, then use both external
// preprocessors on hosted Linux and the assertion-bearing runtime fixture below.
BUSTER_GLOBAL_LOCAL UnitTestResult c_macro_rescan_boundary_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 source;
        String8 expected;
        String8 gcc_expected;
        bool skip_gcc;
    } cases[] = {
        {.source = S8("#define F(x) x\nF\n(11)\n"), .expected = S8("11")},
        {.source = S8("#define F(x) x\nF\n(__LINE__)\n"), .expected = S8("3")},
        {.source = S8("#define F(x) x\n#define A F\nA\n(__LINE__)\n"), .expected = S8("4"), .gcc_expected = S8("3")},
        {.source = S8("#define F(x) x\n#define ID(x) x\nID(F\n(__LINE__))\n"), .expected = S8("4")},
        {.source = S8("#define F(x) x\n#line 70 \"rescan-lines.c\"\nF\n(__LINE__)\n"), .expected = S8("71")},
        {.source = S8("#define F(x) x\n#define A F\n#define B A\nB\n\n(12)\n"), .expected = S8("12")},
        {.source = S8("#define F(x) x\nF /* comment\ncontinued */\n/* between */ (13)\n"), .expected = S8("13")},
        {.source = S8("#define F(x) x\r\nF\r\n\r\n(14)\r\n"), .expected = S8("14")},
        {.source = S8("#define F(x) x\n#define ID(x) x\n#define A F\nID(A)\n(ID(F\n(15)))\n"), .expected = S8("15")},
        {.source = S8("#define F(x) x\n#define TAIL(x) x F\nTAIL(16)\n(17)\n"), .expected = S8("16 17")},
        {.source = S8("#define F(x) x\n#define A F\nF\nname A\n+ 18\n"), .expected = S8("F name F + 18")},
        {.source = S8("#define F(x) x\n#define A F\nA\n#undef A\n#define A 19\nA\n"), .expected = S8("F 19")},
        {.source = S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "_Pragma(\"pop_macro(\\\"X\\\")\") X\n"), .expected = S8("1")},
        {.source = S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "_Pragma(\"pop_macro(\\\"X\\\")\")\nX\n"), .expected = S8("1")},
        {.source = S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "#define RESTORE _Pragma(\"pop_macro(\\\"X\\\")\")\n#define ID(x) x\n"
            "ID(RESTORE X) X\n"), .expected = S8("2 1")},
        {.source = S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n#pragma push_macro(\"X\")\n"
            "#undef X\n#define X 3\n#define DUP(x) x x\n"
            "DUP(_Pragma(\"pop_macro(\\\"X\\\")\") X) X\n"), .expected = S8("3 3 1")},
        {.source = S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "_Pragma(\"push_macro(\\\"X\\\")\") _Pragma(\"pop_macro(\\\"X\\\")\") X "
            "_Pragma(\"pop_macro(\\\"X\\\")\") X\n"), .expected = S8("2 1")},
        {.source = S8("#pragma push_macro(\"MISSING\")\n#define MISSING 9\n"
            "_Pragma(\"pop_macro(\\\"MISSING\\\")\") MISSING\n"), .expected = S8("MISSING")},
        // The two-argument replacement is materialized before its substituted
        // pragma restores the one-argument definition for later invocations.
        {.source = S8("#define F(x) x\n#pragma push_macro(\"F\")\n#undef F\n#define F(x,y) x + y\n"
            "F(_Pragma(\"pop_macro(\\\"F\\\")\") 1,2) F(3)\n"), .expected = S8("1 + 2 3")},
        {.source = S8("#define F(x) x\n#pragma push_macro(\"F\")\n#undef F\n#define F(x,y) x #y\n"
            "F(_Pragma(\"pop_macro(\\\"F\\\")\") 4,5) F(6)\n"), .expected = S8("4 \"5\" 6")},
        {.source = S8("#pragma push_macro(\"X\")\n#define X _Pragma(\"pop_macro(\\\"X\\\")\")\n"
            "X\n#define X 1\nX\n"), .expected = S8("1")},
        // Omission belongs to the suspended variadic definition even when
        // pop_macro restores an object-like name with no parameters. An
        // explicitly empty final argument must retain the separating comma.
        {.source = S8("#define M 7\n#pragma push_macro(\"M\")\n#undef M\n#define M(x,...) x , ##__VA_ARGS__\n"
            "M(_Pragma(\"pop_macro(\\\"M\\\")\") 11) M\n"), .expected = S8("11 7")},
        {.source = S8("#define M 7\n#pragma push_macro(\"M\")\n#undef M\n#define M(x,...) x , ##__VA_ARGS__\n"
            "M(_Pragma(\"pop_macro(\\\"M\\\")\") 11,) M\n"), .expected = S8("11 , 7")},
        // The dynamic builtin kind is part of the saved definition, including
        // restoring it while an ordinary replacement remains on the task stack.
        {.source = S8("#line 100 \"builtin-restored.c\"\n#pragma push_macro(\"__LINE__\")\n#undef __LINE__\n"
            "#define __LINE__ 7\n#pragma pop_macro(\"__LINE__\")\n__LINE__\n"), .expected = S8("104")},
        {.source = S8("#line 100 \"builtin-restored.c\"\n#pragma push_macro(\"__FILE__\")\n#undef __FILE__\n"
            "#define __FILE__ \"replacement.c\"\n#pragma pop_macro(\"__FILE__\")\n__FILE__\n"), .expected = S8("\"builtin-restored.c\"")},
        {.source = S8("#pragma push_macro(\"__LINE__\")\n#undef __LINE__\n#define __LINE__(x,y) x + y\n"
            "__LINE__(_Pragma(\"pop_macro(\\\"__LINE__\\\")\") 1,2) __LINE__\n"), .expected = S8("1 + 2 4")},
        {.source = S8("#pragma push_macro(\"__LINE__\")\n#undef __LINE__\n"
            "#define __LINE__ _Pragma(\"pop_macro(\\\"__LINE__\\\")\") __LINE__\n__LINE__\n"), .expected = S8("4")},
        // One argument prescan is shared by ordinary uses in the replacement list.
        {.source = S8("#define TWICE(x) x x\n#define STR(x) #x x\nTWICE(__COUNTER__) STR(__COUNTER__) __COUNTER__\n"),
         .expected = S8("0 0 \"__COUNTER__\" 1 2")},
        // Pasted identifiers observe a fresh counter value on each invocation.
        {.source = S8("#define CAT_(a,b) a##b\n#define CAT(a,b) CAT_(a,b)\n#define UNIQ(p) CAT(p,__COUNTER__)\nUNIQ(v) UNIQ(v)\n"),
         .expected = S8("v0 v1")},
        // Conditional evaluation advances the counter and exposes builtin definitions.
        {.source = S8("#if __COUNTER__ == 0 && defined(__COUNTER__) && defined(__INCLUDE_LEVEL__) && defined(__BASE_FILE__) && defined __TIMESTAMP__\nyes __COUNTER__ __INCLUDE_LEVEL__\n#endif\n"),
         .expected = S8("yes 1 0")},
        // GCC 15 loops emitting newlines for SAME; the isolated hosted oracle
        // recorded nontermination. Keep Buster and Clang checks for that case.
        // Alias __LINE__ above has a separate observed GCC expectation (3).
        // Evidence: actions/runs/36893167831/job/110473524861.
        // Restoring the same active generation keeps its replacement disabled.
        {.source = S8("#define SAME _Pragma(\"push_macro(\\\"SAME\\\")\") "
            "_Pragma(\"pop_macro(\\\"SAME\\\")\") SAME\nSAME\n"), .expected = S8("SAME"), .skip_gcc = true},
        // A restored different generation is enabled inside the old replacement.
        {.source = S8("#define SELF 7\n#pragma push_macro(\"SELF\")\n#undef SELF\n"
            "#define SELF _Pragma(\"pop_macro(\\\"SELF\\\")\") SELF\nSELF SELF\n"), .expected = S8("7 7")},
    };
    CPreprocessDialect dialects[] = {C_PREPROCESS_DIALECT_GNU17, C_PREPROCESS_DIALECT_C17};
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
    {
        for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialects); dialect_index += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessResult actual = c_preprocess(temporary.arena, cases[case_index].source,
                                                      (CPreprocessOptions){.source_path = S8("macro-rescan-boundary.c"),
                                                                           .dialect = dialects[dialect_index]});
            CLexResult expected = c_lex(temporary.arena, cases[case_index].expected);
            BUSTER_TEST_RAW(arguments, actual.error_count == 0, cases[case_index].source);
            BUSTER_TEST(arguments, actual.token_count == expected.token_count);
            for (u64 index = 0; index < actual.token_count && index < expected.token_count; index += 1)
            {
                BUSTER_TEST(arguments, actual.tokens[index].kind == expected.tokens[index].kind);
                BUSTER_STRING_TEST(arguments, c_token_spelling(actual.spelling_base, actual.tokens[index]),
                                   c_token_spelling(expected.spelling_base, expected.tokens[index]));
            }
            scratch_end(temporary);
        }
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        String8 reference_names[] = {S8("clang"), S8("gcc")};
        for (u32 reference_index = 0; reference_index < BUSTER_ARRAY_LENGTH(reference_names); reference_index += 1)
        {
            if (reference_index == 1 && cases[case_index].skip_gcc)
            {
                continue;
            }
            String8 reference_expected = reference_index == 1 && cases[case_index].gcc_expected.length
                                             ? cases[case_index].gcc_expected : cases[case_index].expected;
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 compiler = executable_resolve_in_path(temporary.arena, reference_names[reference_index]);
            String8 source_path = buster_test_temporary_path(temporary.arena, S8("macro-rescan-reference"), S8(".c"));
            bool written = file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(cases[case_index].source));
            BUSTER_TEST(arguments, compiler.length != 0);
            BUSTER_TEST(arguments, written);
            if (BUSTER_REQUIRE(arguments, compiler.length && written))
            {
                String8 command[] = {compiler, S8("-E"), S8("-P"), S8("-std=c17"), source_path};
                ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                                .use_process_environment = true, .search_path = true,
                                                            });
                BUSTER_TEST(arguments, spawn.handle != 0);
                if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
                {
                    ProcessWaitResult wait = os_process_wait_deadline(temporary.arena, spawn, 30000000);
                    String8 output = BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]);
                    String8 error = BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]);
                    String8 diagnostic_parts[] = {
                        compiler, S8("\nsource:\n"), cases[case_index].source,
                        S8("\nexpected:\n"), reference_expected,
                        S8("\nstdout (up to 4096 bytes):\n"), string_slice(output, 0, BUSTER_MIN(output.length, 4096)),
                        S8("\nstderr (up to 4096 bytes):\n"), string_slice(error, 0, BUSTER_MIN(error.length, 4096)),
                    };
                    String8 diagnostic = string_join_arena(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(diagnostic_parts), false);
                    BUSTER_TEST_RAW(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS, diagnostic);
                    if (BUSTER_REQUIRE(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS))
                    {
                        CLexResult reference = c_lex(temporary.arena, BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]));
                        CLexResult expected = c_lex(temporary.arena, reference_expected);
                        UnitTestResult reference_result = c_macro_conditional_compare_semantic_tokens(arguments, reference, expected, diagnostic);
                        result.test_count += reference_result.test_count;
                        result.succeeded_test_count += reference_result.succeeded_test_count;
                    }
                }
            }
            scratch_end(temporary);
        }
#endif
    }
    return result;
}

// Raw translation witnesses stay outside the frozen external corpora. Every
// dialect is checked against fixed tokens; Linux x64 also requires both host
// preprocessors rather than treating a missing tool as passing evidence.
BUSTER_GLOBAL_LOCAL UnitTestResult c_trigraph_preprocess_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 source;
        String8 expected;
        String8 gnu_expected;
    } cases[] = {
        {S8("?" "?=define VALUE 7\n?" "?=if VALUE != 7\n?" "?=error wrong_branch\n?" "?=endif\nVALUE\n"), S8("7"), {0}},
        {S8("?" "?=define JOINED 19\nJO?" "?/\nINED\n"), S8("19"), {0}},
        {S8("\"?" "?(" "?" "?)" "?" "?<" "?" "?>" "?" "?=" "?" "?/n" "?" "?'" "?" "?!" "?" "?-\" '?" "?/n'\n"),
         S8("\"[]{}#\\n^|~\" '\\n'"), {0}},
        {S8("\"???"
            "=\" \"?" "?x\" \"?" "?\" \"?\"\n"), S8("\"?#\" \"?" "?x\" \"?" "?\" \"?\""), {0}},
        {S8("\"?\\\n?=\"\n"), S8("\"?" "?=\""), S8("\"?" "?=\"")},
        {S8("#define S(x) #x\nS(?\\\n?=)\n"), S8("\"?" "?=\""), S8("\"?" "?=\"")},
        {S8("/?" "?/\n* hidden */ kept\n// hidden ?" "?/\nmore\nlast\n"), S8("kept last"), {0}},
        {S8("#include <trigraph-included.h>\nHEADER\n"), S8("23"), S8("?" "?=define HEADER 23 HEADER")},
    };
    struct
    {
        CPreprocessDialect dialect;
        String8 flag;
        bool gnu;
    } modes[] = {
        {C_PREPROCESS_DIALECT_C99, S8("-std=c99"), false},
        {C_PREPROCESS_DIALECT_C11, S8("-std=c11"), false},
        {C_PREPROCESS_DIALECT_C17, S8("-std=c17"), false},
        {C_PREPROCESS_DIALECT_GNU17, S8("-std=gnu17"), true},
    };
    TemporalArena files = scratch_begin(&arguments->arena, 1);
    String8 root = buster_test_temporary_path(files.arena, S8("trigraph-preprocess"), S8(".dir"));
    String8 header = string_format_z(files.arena, S8("{S8}/trigraph-included.h"), root);
    bool files_ready = root.pointer && os_make_directory_attempt(root) &&
        file_write(header, BUSTER_SLICE_TO_BYTE_SLICE(S8("?" "?=define HEADER 23\n")));
    if (BUSTER_REQUIRE(arguments, files_ready))
    {
        for (u32 test = 0; test < BUSTER_ARRAY_LENGTH(cases); test += 1)
        {
            for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
            {
                TemporalArena temporary = scratch_begin(&files.arena, 1);
                String8 expected_source = modes[mode].gnu
                    ? (cases[test].gnu_expected.length ? cases[test].gnu_expected : cases[test].source) : cases[test].expected;
                CLexResult expected = c_lex(temporary.arena, expected_source);
                CPreprocessResult actual = c_preprocess(temporary.arena, cases[test].source,
                    (CPreprocessOptions){.source_path = S8("trigraph-preprocess.c"), .dialect = modes[mode].dialect,
                                         .include_paths = &root, .include_path_count = 1});
                String8 diagnostic = string_format(temporary.arena, S8("trigraph preprocessing case={u32} mode={S8}"), test, modes[mode].flag);
                u64 expected_index = 0;
                BUSTER_TEST_RAW(arguments, actual.error_count == 0 && expected.diagnostic_count == 0, diagnostic);
                if (BUSTER_REQUIRE(arguments, actual.tokens && actual.spelling_base && expected.tokens && expected.spelling_base &&
                                   actual.error_count == 0 && expected.diagnostic_count == 0))
                {
                    for (u64 token = 0; token < actual.token_count; token += 1)
                    {
                        while (expected_index < expected.token_count && expected.tokens[expected_index].kind == C_TOKEN_NEWLINE)
                        {
                            expected_index += 1;
                        }
                        if (BUSTER_REQUIRE(arguments, expected_index < expected.token_count))
                        {
                            BUSTER_TEST_RAW(arguments, actual.tokens[token].kind == expected.tokens[expected_index].kind, diagnostic);
                            BUSTER_STRING_TEST(arguments, c_token_spelling(actual.spelling_base, actual.tokens[token]),
                                c_token_spelling(expected.spelling_base, expected.tokens[expected_index]));
                            expected_index += 1;
                        }
                    }
                    BUSTER_TEST_RAW(arguments, expected_index == expected.token_count, diagnostic);
                }
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
                String8 source_path = buster_test_temporary_path(temporary.arena, S8("trigraph-reference"), S8(".c"));
                bool written = file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(cases[test].source));
                BUSTER_TEST(arguments, written);
                String8 names[] = {S8("clang"), S8("gcc")};
                for (u32 reference_index = 0; reference_index < BUSTER_ARRAY_LENGTH(names); reference_index += 1)
                {
                    String8 compiler = executable_resolve_in_path(temporary.arena, names[reference_index]);
                    BUSTER_TEST(arguments, compiler.length != 0);
                    if (BUSTER_REQUIRE(arguments, compiler.length && written))
                    {
                        String8 command[] = {compiler, S8("-E"), S8("-P"), S8("-nostdinc"), S8("-Wno-trigraphs"),
                            modes[mode].flag, S8("-I"), root, source_path};
                        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                            (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                  .use_process_environment = true, .search_path = true});
                        BUSTER_TEST(arguments, spawn.handle != 0);
                        if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
                        {
                            ProcessWaitResult wait = os_process_wait_deadline(temporary.arena, spawn, 30000000);
                            String8 output = BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]);
                            String8 error = BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]);
                            String8 parts[] = {diagnostic, S8("\ncompiler: "), compiler, S8("\nsource:\n"), cases[test].source,
                                S8("\nstdout:\n"), string_slice(output, 0, BUSTER_MIN(output.length, 4096)),
                                S8("\nstderr:\n"), string_slice(error, 0, BUSTER_MIN(error.length, 4096))};
                            String8 context = string_join_arena(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), false);
                            BUSTER_TEST_RAW(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS, context);
                            if (BUSTER_REQUIRE(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS))
                            {
                                CLexResult reference = c_lex(temporary.arena, output);
                                BUSTER_TEST_RAW(arguments, reference.diagnostic_count == 0, context);
                                if (BUSTER_REQUIRE(arguments, reference.tokens && reference.spelling_base && expected.tokens && expected.spelling_base &&
                                                   reference.diagnostic_count == 0 && expected.diagnostic_count == 0))
                                {
                                    UnitTestResult compared = c_macro_conditional_compare_semantic_tokens(arguments, reference, expected, context);
                                    result.test_count += compared.test_count;
                                    result.succeeded_test_count += compared.succeeded_test_count;
                                }
                            }
                        }
                    }
                }
#endif
                if (actual.recovery)
                {
                    arena_destroy(actual.recovery->spelling_arena, 1);
                    arena_destroy(actual.recovery->token_arena, 1);
                    arena_destroy(actual.recovery->token_shape_arena, 1);
                }
                scratch_end(temporary);
            }
        }
    }
    scratch_end(files);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_dynamic_builtin_macro_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena files = scratch_begin(&arguments->arena, 1);
    String8 root = buster_test_temporary_path(files.arena, S8("dynamic-builtins"), S8(".dir"));
    String8 header = string_format_z(files.arena, S8("{S8}/counter-included.h"), root);
    bool files_ready = root.pointer && os_make_directory_attempt(root) &&
        file_write(header, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define HEADER_NAME __FILE_NAME__\n"
                                                          "header __COUNTER__ __INCLUDE_LEVEL__ __FILE_NAME__ __BASE_FILE__\n")));
    if (BUSTER_REQUIRE(arguments, files_ready))
    {
        String8 source = S8("__COUNTER__ __INCLUDE_LEVEL__\n"
                            "#include <counter-included.h>\n"
                            "__COUNTER__ __INCLUDE_LEVEL__ HEADER_NAME __FILE_NAME__ __BASE_FILE__ __TIMESTAMP__\n"
                            "#line 9 \"dir/renamed.c\"\n"
                            "__FILE__ __FILE_NAME__ __BASE_FILE__\n");
        String8 expected_source = S8("0 0 header 1 1 \"counter-included.h\" \"dir/dynamic-builtins.c\" "
                                     "2 0 \"dynamic-builtins.c\" \"dynamic-builtins.c\" \"dir/dynamic-builtins.c\" "
                                     "\"Thu Jan  1 00:00:00 1970\" \"dir/renamed.c\" \"renamed.c\" \"dir/dynamic-builtins.c\"");
        for (u32 run = 0; run < 2; run += 1)
        {
            TemporalArena temporary = scratch_begin(&files.arena, 1);
            CPreprocessResult actual = c_preprocess(temporary.arena, source,
                (CPreprocessOptions){.source_path = S8("dir/dynamic-builtins.c"), .include_paths = &root, .include_path_count = 1});
            CLexResult expected = c_lex(temporary.arena, expected_source);
            BUSTER_TEST_RAW(arguments, actual.error_count == 0, S8("dynamic builtin preprocessing"));
            BUSTER_TEST(arguments, actual.token_count == expected.token_count);
            if (BUSTER_REQUIRE(arguments, actual.tokens && actual.spelling_base && expected.tokens && expected.spelling_base &&
                               actual.error_count == 0 && expected.diagnostic_count == 0))
            {
                for (u64 index = 0; index < actual.token_count && index < expected.token_count; index += 1)
                {
                    BUSTER_TEST(arguments, actual.tokens[index].kind == expected.tokens[index].kind);
                    BUSTER_STRING_TEST(arguments, c_token_spelling(actual.spelling_base, actual.tokens[index]),
                                       c_token_spelling(expected.spelling_base, expected.tokens[index]));
                }
            }
            if (actual.recovery)
            {
                arena_destroy(actual.recovery->spelling_arena, 1);
                arena_destroy(actual.recovery->token_arena, 1);
                arena_destroy(actual.recovery->token_shape_arena, 1);
            }
            scratch_end(temporary);
        }
    }
    scratch_end(files);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_skipped_group_text_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 body;
    } bodies[] = {
        {S8("This block isn't compiled.\n")},
        {S8("He said \"stop\n")},
        {S8("Use `make test` to run this.\n")},
        {S8("control byte \x01\n")},
        {S8("control byte \x7f\n")},
        {S8("#error can't happen\n")},
        {S8("#bogus `x`\n")},
        {S8("# 'x\n")},
        {S8("/* comment with #endif inside\n#endif */ still skipped, isn't it\n")},
    };
    struct
    {
        String8 prefix;
        String8 suffix;
        String8 expected;
    } contexts[] = {
        {S8("#if 0\n"), S8("\n#endif\nint x;\n"), S8("int x;")},
        {S8("#ifdef UNDEFINED_NAME\n"), S8("\n#endif\nint x;\n"), S8("int x;")},
        {S8("#define DEFINED_NAME 1\n#ifndef DEFINED_NAME\n"), S8("\n#endif\nint x;\n"), S8("int x;")},
        {S8("#if 1\nint x;\n#elif 1\n"), S8("\n#endif\n"), S8("int x;")},
        {S8("#if 1\nint x;\n#else\n"), S8("\n#endif\n"), S8("int x;")},
        {S8("#if 1\n#if 0\n#if 1\n"), S8("\n#endif\n#endif\n#endif\nint x;\n"), S8("int x;")},
        {S8("#define ID(x) x\nID(\n#if 0\n"), S8("\n#endif\nkept\n)\n"), S8("kept")},
    };
    for (u32 context_index = 0; context_index < BUSTER_ARRAY_LENGTH(contexts); context_index += 1)
    {
        for (u32 body_index = 0; body_index < BUSTER_ARRAY_LENGTH(bodies); body_index += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 source_parts[] = {contexts[context_index].prefix, bodies[body_index].body, contexts[context_index].suffix};
            String8 source = string_join_arena(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(source_parts), false);
            CPreprocessResult actual = c_preprocess(temporary.arena, source, (CPreprocessOptions){.source_path = S8("skipped-group.c")});
            CLexResult expected = c_lex(temporary.arena, contexts[context_index].expected);
            String8 diagnostic = string_format(temporary.arena, S8("skipped-group context={u32} body={u32}"), context_index, body_index);
            BUSTER_TEST_RAW(arguments, actual.diagnostic_count == 0, diagnostic);
            BUSTER_TEST_RAW(arguments, expected.diagnostic_count == 0, diagnostic);
            BUSTER_TEST_RAW(arguments, actual.token_count == expected.token_count, diagnostic);
            for (u64 token_index = 0; token_index < actual.token_count && token_index < expected.token_count; token_index += 1)
            {
                BUSTER_TEST_RAW(arguments, actual.tokens[token_index].kind == expected.tokens[token_index].kind, diagnostic);
                BUSTER_TEST_RAW(arguments,
                                string_equal(c_token_spelling(actual.spelling_base, actual.tokens[token_index]),
                                             c_token_spelling(expected.spelling_base, expected.tokens[token_index])),
                                diagnostic);
            }
            scratch_end(temporary);
        }
    }

    TemporalArena stringized_temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult stringized = c_preprocess(stringized_temporary.arena,
                                                S8("#define STR(x) #x\n#define UNUSED `never expanded`\nSTR(`)\n"),
                                                (CPreprocessOptions){.source_path = S8("skipped-group-stringize.c")});
    BUSTER_TEST(arguments, stringized.diagnostic_count == 0);
    BUSTER_TEST(arguments, stringized.token_count == 2);
    if (BUSTER_REQUIRE(arguments, stringized.token_count == 2))
    {
        BUSTER_TEST(arguments, stringized.tokens[0].kind == C_TOKEN_STRING_LITERAL);
        BUSTER_STRING_TEST(arguments, c_token_spelling(stringized.spelling_base, stringized.tokens[0]), S8("\"`\""));
        BUSTER_TEST(arguments, stringized.tokens[1].kind == C_TOKEN_END_OF_FILE);
    }
    scratch_end(stringized_temporary);

    TemporalArena unused_argument_temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult unused_argument = c_preprocess(unused_argument_temporary.arena,
                                                     S8("#define IGNORE(x)\nIGNORE(`)\n"),
                                                     (CPreprocessOptions){.source_path = S8("skipped-group-unused-argument.c")});
    BUSTER_TEST(arguments, unused_argument.diagnostic_count == 0);
    BUSTER_TEST(arguments, unused_argument.token_count == 1);
    if (BUSTER_REQUIRE(arguments, unused_argument.token_count == 1))
    {
        BUSTER_TEST(arguments, unused_argument.tokens[0].kind == C_TOKEN_END_OF_FILE);
    }
    scratch_end(unused_argument_temporary);

    struct
    {
        String8 source;
        CDiagnosticKind kind;
    } rejected[] = {
        {S8("int y = 3 ` 4;\n"), C_DIAGNOSTIC_INVALID_CHARACTER},
        {S8("int x = \x01;\n"), C_DIAGNOSTIC_INVALID_CHARACTER},
        {S8("#define B `\nB\n"), C_DIAGNOSTIC_INVALID_CHARACTER},
        {S8("'x"), C_DIAGNOSTIC_UNTERMINATED_CHARACTER_LITERAL},
        {S8("\"x"), C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL},
        {S8("#if 0\n#elif 'x\n#endif\n"), C_DIAGNOSTIC_UNTERMINATED_CHARACTER_LITERAL},
        {S8("#if 0\n/* open"), C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT},
        {S8("#if `\n"), C_DIAGNOSTIC_INVALID_CONDITIONAL},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(rejected); case_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult actual = c_preprocess(temporary.arena, rejected[case_index].source,
                                                (CPreprocessOptions){.source_path = S8("skipped-group-invalid.c")});
        String8 diagnostic_context = string_format(temporary.arena, S8("skipped-group rejected case={u32}"), case_index);
        BUSTER_TEST_RAW(arguments, actual.diagnostic_count >= 1, diagnostic_context);
        bool found_expected_kind = false;
        for (u64 diagnostic_index = 0; diagnostic_index < actual.diagnostic_count; diagnostic_index += 1)
        {
            CDiagnostic diagnostic = actual.diagnostics[diagnostic_index];
            if (diagnostic.kind == rejected[case_index].kind)
            {
                found_expected_kind = true;
                if (case_index == 0)
                {
                    BUSTER_TEST_RAW(arguments, diagnostic.location.line == 1, diagnostic_context);
                    BUSTER_TEST_RAW(arguments, diagnostic.location.column == 11, diagnostic_context);
                }
            }
        }
        BUSTER_TEST_RAW(arguments, found_expected_kind, diagnostic_context);
        scratch_end(temporary);
    }
    return result;
}

typedef struct CTestPunctuatorSeparatorPair CTestPunctuatorSeparatorPair;
struct CTestPunctuatorSeparatorPair
{
    CPunctuator previous;
    CPunctuator current;
    String8 previous_spelling;
    String8 current_spelling;
    bool separator;
};

typedef struct CTestPunctuatorSeparatorEmission CTestPunctuatorSeparatorEmission;
struct CTestPunctuatorSeparatorEmission
{
    String8 source;
    String8 output;
    CPunctuator first;
    CPunctuator second;
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_punctuator_separator_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CTestPunctuatorSeparatorPair pairs[] = {
        {C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_ASSIGN, S8("%"), S8("="), true},
        {C_PUNCTUATOR_ASSIGN, C_PUNCTUATOR_ASSIGN, S8("="), S8("="), true},
        {C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_EQUAL, S8("%"), S8("=="), true},
        {C_PUNCTUATOR_ASSIGN, C_PUNCTUATOR_EQUAL, S8("="), S8("=="), true},
        {C_PUNCTUATOR_PERCENT_ASSIGN, C_PUNCTUATOR_ASSIGN, S8("%="), S8("="), false},
        {C_PUNCTUATOR_EQUAL, C_PUNCTUATOR_ASSIGN, S8("=="), S8("="), false},
        {C_PUNCTUATOR_EQUAL, C_PUNCTUATOR_EQUAL, S8("=="), S8("=="), false},
        {C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_SEMICOLON, S8("%"), S8(";"), false},
        {C_PUNCTUATOR_ASSIGN, C_PUNCTUATOR_SEMICOLON, S8("="), S8(";"), false},
        {C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_COLON, S8("%"), S8(":"), true},
        {C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_GREATER, S8("%"), S8(">"), true},
        {C_PUNCTUATOR_HASH, C_PUNCTUATOR_HASH, S8("%:"), S8("%:"), true},
        {C_PUNCTUATOR_HASH, C_PUNCTUATOR_ASSIGN, S8("%:"), S8("="), false},
        {C_PUNCTUATOR_HASH, C_PUNCTUATOR_HASH, S8("#"), S8("#"), true},
        {C_PUNCTUATOR_LESS, C_PUNCTUATOR_COLON, S8("<"), S8(":"), true},
        {C_PUNCTUATOR_COLON, C_PUNCTUATOR_GREATER, S8(":"), S8(">"), true},
        {C_PUNCTUATOR_SLASH, C_PUNCTUATOR_STAR, S8("/"), S8("*"), true},
        {C_PUNCTUATOR_SLASH, C_PUNCTUATOR_SLASH, S8("/"), S8("/"), true},
        {C_PUNCTUATOR_PLUS, C_PUNCTUATOR_PLUS, S8("+"), S8("+"), true},
        {C_PUNCTUATOR_SHIFT_LEFT, C_PUNCTUATOR_ASSIGN, S8("<<"), S8("="), true},
        {C_PUNCTUATOR_SHIFT_RIGHT, C_PUNCTUATOR_ASSIGN, S8(">>"), S8("="), true},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(pairs); index += 1)
    {
        CToken previous = {.kind = C_TOKEN_PUNCTUATOR, .punctuator = (u8)pairs[index].previous};
        CToken current = {.kind = C_TOKEN_PUNCTUATOR, .punctuator = (u8)pairs[index].current};
        BUSTER_TEST(arguments, c_token_requires_separator(previous, pairs[index].previous_spelling, current,
                                                          pairs[index].current_spelling) == pairs[index].separator);
    }

    CTestPunctuatorSeparatorEmission emitted[] = {
        {S8("#define P %\nP=\n"), S8("% =\n"), C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_ASSIGN},
        {S8("#define A =\nA=\n"), S8("= =\n"), C_PUNCTUATOR_ASSIGN, C_PUNCTUATOR_ASSIGN},
        {S8("#define P %\nP==\n"), S8("% ==\n"), C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_EQUAL},
        {S8("#define A =\nA==\n"), S8("= ==\n"), C_PUNCTUATOR_ASSIGN, C_PUNCTUATOR_EQUAL},
        {S8("#define P() %\nP()=\n"), S8("% =\n"), C_PUNCTUATOR_PERCENT, C_PUNCTUATOR_ASSIGN},
        {S8("#define A() =\nA()=\n"), S8("= =\n"), C_PUNCTUATOR_ASSIGN, C_PUNCTUATOR_ASSIGN},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(emitted); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 source_path = buster_test_temporary_path(temporary.arena, S8("punctuator-separator"), S8(".c"));
        String8 output_path = buster_test_temporary_path(temporary.arena, S8("punctuator-separator"), S8(".i"));
        bool written = file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(emitted[index].source));
        BUSTER_TEST(arguments, written);
        if (written)
        {
            CPreprocessResult direct = c_preprocess(temporary.arena, emitted[index].source,
                                                    (CPreprocessOptions){.source_path = source_path});
            BUSTER_TEST(arguments, direct.error_count == 0);
            BUSTER_TEST(arguments, direct.token_count == 3);
            for (u32 file_output = 0; file_output < 2; file_output += 1)
            {
                String8 command[] = {S8("-E"), S8("-nostdinc"), S8("-std=c17"), source_path};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(
                    temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                if (file_output) invocation.output_path = output_path;
                CompilerDriverResult printed = compiler_driver_execute_invocation(temporary.arena, invocation);
                BUSTER_TEST_RAW(arguments, printed.error == COMPILER_DRIVER_ERROR_NONE, printed.diagnostic);
                if (printed.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 output = file_output ? BYTE_SLICE_TO_STRING(8, file_read(temporary.arena, output_path, (FileReadOptions){0})) : printed.output;
                    BUSTER_STRING_TEST(arguments, output, emitted[index].output);
                    CLexResult restored = c_lex(temporary.arena, output);
                    BUSTER_TEST(arguments, restored.diagnostic_count == 0);
                    u32 token_index = 0;
                    for (u64 scan = 0; scan < restored.token_count; scan += 1)
                    {
                        CToken token = restored.tokens[scan];
                        if (token.kind != C_TOKEN_NEWLINE && token.kind != C_TOKEN_END_OF_FILE)
                        {
                            CPunctuator expected = token_index == 0 ? emitted[index].first : emitted[index].second;
                            BUSTER_TEST(arguments, token_index < 2 && token.kind == C_TOKEN_PUNCTUATOR && token.punctuator == expected);
                            if (token_index < direct.token_count && direct.tokens[token_index].kind != C_TOKEN_END_OF_FILE)
                            {
                                BUSTER_TEST(arguments, token.kind == direct.tokens[token_index].kind);
                                BUSTER_TEST(arguments, token.punctuator == direct.tokens[token_index].punctuator);
                                BUSTER_STRING_TEST(arguments, c_token_spelling(restored.spelling_base, token),
                                                   c_token_spelling(direct.spelling_base, direct.tokens[token_index]));
                            }
                            token_index += 1;
                        }
                    }
                    BUSTER_TEST(arguments, token_index == 2);
                }
            }
        }
        scratch_end(temporary);
    }

    // Both sources are invalid with their two separate tokens. Losing their
    // boundary would turn them into valid %= or == expressions in the .i file.
    String8 invalid[] = {
        S8("#define P %\nint f(void) { int x = 9; x P= 4; return x; }\n"),
        S8("#define A =\nint f(void) { int x = 9; x A= 4; return x; }\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 source_path = buster_test_temporary_path(temporary.arena, S8("punctuator-admission"), S8(".c"));
        String8 output_path = buster_test_temporary_path(temporary.arena, S8("punctuator-admission"), S8(".i"));
        String8 object_path = buster_test_temporary_path(temporary.arena, S8("punctuator-admission"), S8(".o"));
        String8 sentinel = S8("preserve invalid-source output");
        bool written = file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(invalid[index]));
        BUSTER_TEST(arguments, written);
        if (written)
        {
            String8 preprocess[] = {S8("-E"), S8("-nostdinc"), S8("-std=c17"), S8("-o"), output_path, source_path};
            CompilerDriverResult printed = compiler_driver_execute_invocation(
                temporary.arena, compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(preprocess)));
            BUSTER_TEST_RAW(arguments, printed.error == COMPILER_DRIVER_ERROR_NONE, printed.diagnostic);
            if (printed.error == COMPILER_DRIVER_ERROR_NONE)
            {
                for (u32 staged = 0; staged < 2; staged += 1)
                {
                    BUSTER_TEST(arguments, file_write(object_path, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
                    String8 check[] = {S8("-c"), S8("-nostdinc"), S8("-std=c17"), S8("-o"), object_path, staged ? output_path : source_path};
                    CompilerDriverResult checked = compiler_driver_execute_invocation(
                        temporary.arena, compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(check)));
                    String8 diagnostic = string_format(temporary.arena, S8("punctuator refusal case={u32} staged={u32} error={u32}: {S8}"),
                                                       index, staged, (u32)checked.error, checked.diagnostic);
                    BUSTER_TEST_RAW(arguments, checked.error == COMPILER_DRIVER_ERROR_PARSE || checked.error == COMPILER_DRIVER_ERROR_ANALYSIS ||
                                               checked.error == COMPILER_DRIVER_ERROR_IR, diagnostic);
                    BUSTER_TEST(arguments, !checked.has_object);
                    BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, file_read(temporary.arena, object_path, (FileReadOptions){0})), sentinel);
                }
            }
        }
        scratch_end(temporary);
    }

#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    String8 valid = S8("#define REM %=\n#define SAME ==\n"
                       "int main(void) { int x = 9; x REM 4; return !(x SAME 1); }\n");
    String8 frontend_flags[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 frontend = 0; frontend < BUSTER_ARRAY_LENGTH(frontend_flags); frontend += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 source_path = buster_test_temporary_path(temporary.arena, S8("punctuator-runtime"), S8(".c"));
        String8 staged_path = buster_test_temporary_path(temporary.arena, S8("punctuator-runtime"), S8(".i"));
        String8 output_path = buster_test_temporary_path(temporary.arena, S8("punctuator-runtime"), S8(""));
        bool written = file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(valid));
        BUSTER_TEST(arguments, written);
        if (written)
        {
            String8 preprocess[] = {S8("-E"), S8("-nostdinc"), S8("-std=c17"), S8("-o"), staged_path, source_path};
            CompilerDriverResult printed = compiler_driver_execute_invocation(
                temporary.arena, compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(preprocess)));
            BUSTER_TEST_RAW(arguments, printed.error == COMPILER_DRIVER_ERROR_NONE, printed.diagnostic);
            if (printed.error == COMPILER_DRIVER_ERROR_NONE)
            {
                for (u32 staged = 0; staged < 2; staged += 1)
                {
                    String8 command[] = {S8("-nostdinc"), S8("-std=c17"), frontend_flags[frontend], S8("-fverify-codegen"),
                                         S8("-fno-machine-fallback"), S8("-o"), output_path, staged ? staged_path : source_path};
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(
                        temporary.arena, compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
                    {
                        String8 run[] = {output_path};
                        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                                                    (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
                        BUSTER_TEST(arguments, spawn.handle != 0);
                        if (spawn.handle)
                        {
                            ProcessWaitResult wait = os_process_wait_deadline(temporary.arena, spawn, 30000000);
                            BUSTER_TEST(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS);
                        }
                    }
                }
            }
        }
        scratch_end(temporary);
    }
#endif
    return result;
}

UnitTestResult c_macro_conditional_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, c_macro_rescan_boundary_tests);
    BUSTER_TEST_FIXTURE(arguments, c_skipped_group_text_tests);
    BUSTER_TEST_FIXTURE(arguments, c_punctuator_separator_tests);
    BUSTER_TEST_FIXTURE(arguments, c_trigraph_preprocess_tests);
    BUSTER_TEST_FIXTURE(arguments, c_dynamic_builtin_macro_tests);
    UnitTestResult demand = c_macro_argument_demand_tests(arguments);
    result.test_count += demand.test_count;
    result.succeeded_test_count += demand.succeeded_test_count;
    struct
    {
        String8 source;
        String8 expected;
        u32 warning_line;
    } trailing_conditionals[] = {
        {S8("#ifndef FOO_H\n#define FOO_H\nint a;\n#endif FOO_H\n"), S8("int a;"), 4},
        {S8("#ifdef FOO\nint no;\n#else FOO\nint yes;\n#endif\n"), S8("int yes;"), 3},
        {S8("#if 0\n#else !0\nint yes;\n#endif\n"), S8("int yes;"), 2},
        {S8("#if 1\nint yes;\n#endif ;\n"), S8("int yes;"), 3},
        {S8("#ifdef A B\nint no;\n#endif\n"), S8(""), 1},
        {S8("#define A\n#ifndef A B\nint no;\n#else\nint yes;\n#endif\n"), S8("int yes;"), 2},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(trailing_conditionals); case_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult actual = c_preprocess(temporary.arena, trailing_conditionals[case_index].source,
                                                (CPreprocessOptions){.source_path = S8("trailing-conditional.c")});
        BUSTER_TEST(arguments, actual.error_count == 0);
        BUSTER_TEST(arguments, actual.warning_count == 1);
        BUSTER_TEST(arguments, actual.diagnostic_count == 1);
        if (BUSTER_REQUIRE(arguments, actual.diagnostic_count == 1))
        {
            CDiagnostic diagnostic = actual.diagnostics[0];
            BUSTER_TEST(arguments, diagnostic.kind == C_DIAGNOSTIC_EXTRA_DIRECTIVE_TOKENS);
            BUSTER_TEST(arguments, diagnostic.severity == C_DIAGNOSTIC_WARNING);
            BUSTER_TEST(arguments, diagnostic.location.line == trailing_conditionals[case_index].warning_line);
            BUSTER_TEST(arguments, diagnostic.location.column == 2);
        }
        UnitTestResult compared = c_macro_conditional_expect_preprocessed(arguments, temporary.arena, actual,
                                                                           trailing_conditionals[case_index].expected);
        result.test_count += compared.test_count;
        result.succeeded_test_count += compared.succeeded_test_count;
        scratch_end(temporary);
    }
    String8 no_warning_conditionals[] = {
        S8("#if 1\n#endif // FOO\n"),
        S8("#if 1\n#endif /* FOO */\n"),
        S8("#if 0\n#ifdef A B\n#else C\n#endif D\n#endif\n"),
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(no_warning_conditionals); case_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult actual = c_preprocess(temporary.arena, no_warning_conditionals[case_index],
                                                (CPreprocessOptions){.source_path = S8("trailing-conditional-controls.c")});
        BUSTER_TEST(arguments, actual.error_count == 0);
        BUSTER_TEST(arguments, actual.warning_count == 0);
        BUSTER_TEST(arguments, actual.diagnostic_count == 0);
        scratch_end(temporary);
    }
    TemporalArena include_files = scratch_begin(&arguments->arena, 1);
    String8 include_root = buster_test_temporary_path(include_files.arena, S8("trailing-directive-include"), S8(".dir"));
    String8 include_header = string_format_z(include_files.arena, S8("{S8}/hdr"), include_root);
    bool include_files_ready = include_root.pointer && os_make_directory_attempt(include_root) &&
                               file_write(include_header, BUSTER_SLICE_TO_BYTE_SLICE(S8("int header_token;\n")));
    BUSTER_TEST(arguments, include_files_ready);
    if (include_files_ready)
    {
        String8 include_sources[] = {S8("#include \"hdr\" extra\n"), S8("#include <hdr> extra\n")};
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(include_sources); case_index += 1)
        {
            TemporalArena temporary = scratch_begin(&include_files.arena, 1);
            CPreprocessResult actual = c_preprocess(temporary.arena, include_sources[case_index],
                                                    (CPreprocessOptions){.source_path = S8("trailing-include.c"),
                                                                         .include_paths = &include_root,
                                                                         .include_path_count = 1});
            BUSTER_TEST(arguments, actual.error_count == 0);
            BUSTER_TEST(arguments, actual.warning_count == 1);
            BUSTER_TEST(arguments, actual.diagnostic_count == 1);
            if (BUSTER_REQUIRE(arguments, actual.diagnostic_count == 1))
            {
                CDiagnostic diagnostic = actual.diagnostics[0];
                BUSTER_TEST(arguments, diagnostic.kind == C_DIAGNOSTIC_EXTRA_DIRECTIVE_TOKENS);
                BUSTER_TEST(arguments, diagnostic.severity == C_DIAGNOSTIC_WARNING);
                BUSTER_TEST(arguments, diagnostic.location.line == 1);
                BUSTER_TEST(arguments, diagnostic.location.column == 2);
            }
            UnitTestResult compared = c_macro_conditional_expect_preprocessed(arguments, temporary.arena, actual,
                                                                               S8("int header_token;"));
            result.test_count += compared.test_count;
            result.succeeded_test_count += compared.succeeded_test_count;
            scratch_end(temporary);
        }
    }
    scratch_end(include_files);
    struct
    {
        String8 source;
        u32 line;
        u32 column;
    } invalid_macro_definitions[] = {
        {S8("#define g(x) # y\n#ifdef g\nint wrong;\n#else\nint not_defined;\n#endif\n"), 1, 14},
        {S8("#define g(x) x #\n"), 1, 16},
        {S8("#define defined 1\n"), 1, 9},
        {S8("#undef defined\n"), 1, 8},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_macro_definitions); case_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult actual = c_preprocess(temporary.arena, invalid_macro_definitions[case_index].source,
                                                (CPreprocessOptions){.source_path = S8("invalid-macro-definition.c")});
        BUSTER_TEST(arguments, actual.error_count == 1);
        BUSTER_TEST(arguments, actual.warning_count == 0);
        BUSTER_TEST(arguments, actual.diagnostic_count == 1);
        if (BUSTER_REQUIRE(arguments, actual.diagnostic_count == 1))
        {
            CDiagnostic diagnostic = actual.diagnostics[0];
            BUSTER_TEST(arguments, diagnostic.kind == C_DIAGNOSTIC_INVALID_MACRO_DEFINITION);
            BUSTER_TEST(arguments, diagnostic.severity == C_DIAGNOSTIC_ERROR);
            BUSTER_TEST(arguments, diagnostic.location.line == invalid_macro_definitions[case_index].line);
            BUSTER_TEST(arguments, diagnostic.location.column == invalid_macro_definitions[case_index].column);
        }
        if (case_index == 0)
        {
            UnitTestResult compared = c_macro_conditional_expect_preprocessed(arguments, temporary.arena, actual,
                                                                               S8("int not_defined;"));
            result.test_count += compared.test_count;
            result.succeeded_test_count += compared.succeeded_test_count;
        }
        scratch_end(temporary);
    }
    String8 valid_macro_definitions = S8("#define s(x) #x\n"
                                         "#define v(...) #__VA_ARGS__\n"
                                         "#define h(x) a ## x\n"
                                         "#define H # y\n");
    TemporalArena valid_macro_temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult valid_macros = c_preprocess(valid_macro_temporary.arena, valid_macro_definitions,
                                                  (CPreprocessOptions){.source_path = S8("valid-macro-definitions.c")});
    BUSTER_TEST(arguments, valid_macros.diagnostic_count == 0);
    scratch_end(valid_macro_temporary);
    TemporalArena assembly_macro_temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult assembly_macro = c_preprocess(assembly_macro_temporary.arena, S8("#define g(x) # y\n"),
                                                    (CPreprocessOptions){.source_path = S8("assembly-macro-definition.c"),
                                                                         .assembly_comment_lines = true});
    BUSTER_TEST(arguments, assembly_macro.diagnostic_count == 0);
    scratch_end(assembly_macro_temporary);
    String8 source = S8("#define ENABLED 1\n"
                        "#define MISSING_VALUE 0\n"
                        "#define ID(x) x\n"
                        "#define INNER(x,y) x y\n"
                        "#define STR(x) #x\n"
                        "#define CAT(a,b) a##b\n"
                        "#define VAR(first,...) first __VA_ARGS__\n"
                        "ID(\n#if ENABLED\nif_value\n#endif\n)\n"
                        "ID(\n#ifdef ENABLED\nifdef_value\n#endif\n)\n"
                        "ID(\n#ifndef ABSENT\nifndef_value\n#endif\n)\n"
                        "ID(\n#if MISSING_VALUE\ninactive_if, ), ((\n#elif ENABLED\nelif_value\n#else\ninactive_else, ), ((\n#endif\n)\n"
                        "ID(\n#if MISSING_VALUE\ninactive\n#else\nelse_value\n#endif\n)\n"
                        "ID(\n#if MISSING_VALUE\n#error inactive directive must be skipped\n#endif\nafter_inactive_directive\n)\n"
                        "ID(\n#if ENABLED\n#if defined(ENABLED)\nINNER(nested_a, (nested_b, nested_c))\n#else\ninactive_nested, )\n#endif\n#endif\n)\n"
                        "ID(\n#if ENABLED\n#else\nnot_selected\n#endif\n) after_empty\n"
                        "STR(\n#if ENABLED\nalpha beta\n#else\nwrong, )\n#endif\n)\n"
                        "CAT(\n#if ENABLED\npre\n#endif\n,\n#if ENABLED\nfix\n#endif\n)\n"
                        "VAR(head,\n#if ENABLED\n+ tail\n#else\n, wrong, )\n#endif\n)\n"
                        "ID(\nordinary\n)\n"
                        "#if !(u'\\0' - 1 > 0)\n"
                        "#error UTF-16 character type lost in conditional preprocessing\n"
                        "#endif\n"
                        "#if !(U'\\0' - 1 > 0)\n"
                        "#error UTF-32 character type lost in conditional preprocessing\n"
                        "#endif\n"
                        "#if !(~u'\\0' > 0)\n"
                        "#error conditional complement lost unsigned character type\n"
                        "#endif\n"
                        "#if (1 ? -1 : u'\\0') < 0\n"
                        "#error conditional arms did not determine preprocessing type\n"
                        "#endif\n");
    String8 expected_source = S8("if_value ifdef_value ifndef_value elif_value else_value after_inactive_directive "
                                 "nested_a (nested_b, nested_c) after_empty \"alpha beta\" prefix head + tail ordinary");
    CPreprocessDialect dialects[] = {C_PREPROCESS_DIALECT_GNU17, C_PREPROCESS_DIALECT_C17};
    for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialects); dialect_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_preprocess(temporary.arena, source,
                                                    (CPreprocessOptions){
                                                        .source_path = S8("macro-conditional-arguments.c"),
                                                        .dialect = dialects[dialect_index],
                                                    });
        CLexResult expected = c_lex(temporary.arena, expected_source);
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
        BUSTER_TEST(arguments, expected.diagnostic_count == 0);
        BUSTER_TEST(arguments, preprocess.token_count == expected.token_count);
        for (u64 index = 0; index + 1 < expected.token_count && index < preprocess.token_count; index += 1)
        {
            CToken actual = preprocess.tokens[index];
            CToken reference = expected.tokens[index];
            BUSTER_TEST(arguments, actual.kind == reference.kind);
            BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, actual),
                               c_token_spelling(expected.spelling_base, reference));
            CSourceLocation location = c_preprocess_token_location(&preprocess, actual);
            if (BUSTER_REQUIRE(arguments, location.file < preprocess.file_count))
            {
                BUSTER_STRING_TEST(arguments, preprocess.files[location.file], S8("macro-conditional-arguments.c"));
            }
        }
        scratch_end(temporary);
    }
    // Preprocessing arithmetic widens each literal to intmax_t or uintmax_t
    // using its target type. Keep wchar choices explicit so this catches a
    // regression in either the decoder or the evaluator's type handoff.
    struct
    {
        Target target;
        bool wide_unsigned;
    } targets[] = {
        {{.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX}, false},
        {{.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX}, true},
        {{.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS}, false},
        {{.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS}, false},
        {{.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS}, true},
        {{.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS}, true},
    };
    String8 wide_sources[] = {
        S8("#if L'\\0' - 1 > 0\n#error signed wchar became unsigned in conditional preprocessing\n#endif\n"),
        S8("#if !(L'\\0' - 1 > 0)\n#error unsigned wchar became signed in conditional preprocessing\n#endif\n"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialects); dialect_index += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessOptions options = {
                .source_path = S8("prefixed-character-conditional.c"),
                .target = targets[target_index].target,
                .data_layout = target_data_layout(targets[target_index].target),
                .dialect = dialects[dialect_index],
            };
            CPreprocessResult common = c_preprocess(temporary.arena, S8("#if !(u'\\0' - 1 > 0)\n#error unsigned UTF-16 type lost\n#endif\n"
                                                                        "#if !(U'\\0' - 1 > 0)\n#error unsigned UTF-32 type lost\n#endif\n"
                                                                        "#if !(~u'\\0' > 0)\n#error unsigned complement type lost\n#endif\n"
                                                                        "#if (1 ? -1 : u'\\0') < 0\n#error conditional common type lost\n#endif\n"),
                                                        options);
            CPreprocessResult wide = c_preprocess(temporary.arena, wide_sources[targets[target_index].wide_unsigned], options);
            BUSTER_TEST(arguments, common.diagnostic_count == 0);
            BUSTER_TEST(arguments, wide.diagnostic_count == 0);
            scratch_end(temporary);
        }
    }
    TemporalArena utf8_temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult utf8 = c_preprocess(utf8_temporary.arena,
                                          S8("#if !(u8'\\0' - 1 > 0)\n#error C23 UTF-8 character type lost\n#endif\n"),
                                          (CPreprocessOptions){.source_path = S8("utf8-character-conditional.c"),
                                                               .target = targets[0].target,
                                                               .data_layout = target_data_layout(targets[0].target),
                                                               .dialect = C_PREPROCESS_DIALECT_C23});
    BUSTER_TEST(arguments, utf8.diagnostic_count == 0);
    scratch_end(utf8_temporary);
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    String8 reference_names[] = {S8("clang"), S8("gcc")};
    for (u32 reference_index = 0; reference_index < BUSTER_ARRAY_LENGTH(reference_names); reference_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 compiler = executable_resolve_in_path(temporary.arena, reference_names[reference_index]);
        String8 source_path = buster_test_temporary_path(temporary.arena, S8("macro-conditional-reference"), S8(".c"));
        BUSTER_TEST(arguments, compiler.length != 0);
        BUSTER_TEST(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source)));
        if (compiler.length)
        {
            String8 command[] = {compiler, S8("-E"), S8("-P"), S8("-std=gnu17"), source_path};
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                                                        (ProcessSpawnOptions){
                                                            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                            .use_process_environment = true, .search_path = true,
                                                        });
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(temporary.arena, spawn, 30000000);
                BUSTER_TEST(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS);
                CLexResult reference = c_lex(temporary.arena, BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]));
                CLexResult expected = c_lex(temporary.arena, expected_source);
                BUSTER_TEST(arguments, reference.diagnostic_count == 0);
                UnitTestResult reference_result = c_macro_conditional_compare_semantic_tokens(arguments, reference, expected, source);
                result.test_count += reference_result.test_count;
                result.succeeded_test_count += reference_result.succeeded_test_count;
            }
        }
        scratch_end(temporary);
    }
#endif

    struct
    {
        String8 source;
        CDiagnosticKind first_kind;
        u32 first_line;
        u32 first_column;
        CDiagnosticKind second_kind;
        u32 second_line;
        u32 second_column;
    } invalid[] = {
        {S8("#define ID(x) x\n#line 40 \"macro-conditional-error.c\"\nID(\n#if 1\nvalue\n#endif\n"),
         C_DIAGNOSTIC_INVALID_MACRO_INVOCATION, 40, 1, C_DIAGNOSTIC_KIND_COUNT, 0, 0},
        {S8("#define ID(x) x\n#line 70 \"macro-conditional-error.c\"\nID(\n#if 1\nvalue\n"),
         C_DIAGNOSTIC_UNMATCHED_CONDITIONAL, 71, 2, C_DIAGNOSTIC_INVALID_MACRO_INVOCATION, 70, 1},
        {S8("#define ID(x) x\n#line 90 \"macro-conditional-error.c\"\nID(\n#endif\nvalue\n)\n"),
         C_DIAGNOSTIC_UNMATCHED_CONDITIONAL, 91, 2, C_DIAGNOSTIC_KIND_COUNT, 0, 0},
        {S8("#define ID(x) x\n#line 110 \"macro-conditional-error.c\"\nID(\n#if (\nvalue\n#endif\n)\n"),
         C_DIAGNOSTIC_INVALID_CONDITIONAL, 111, 2, C_DIAGNOSTIC_KIND_COUNT, 0, 0},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid); case_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_preprocess(temporary.arena, invalid[case_index].source,
                                                    (CPreprocessOptions){.source_path = S8("macro-conditional-input.c")});
        BUSTER_TEST(arguments, preprocess.diagnostic_count >= 1);
        if (preprocess.diagnostic_count >= 1)
        {
            CDiagnostic diagnostic = preprocess.diagnostics[0];
            BUSTER_TEST(arguments, diagnostic.kind == invalid[case_index].first_kind);
            BUSTER_TEST(arguments, diagnostic.location.line == invalid[case_index].first_line);
            BUSTER_TEST(arguments, diagnostic.location.column == invalid[case_index].first_column);
            if (BUSTER_REQUIRE(arguments, diagnostic.location.file < preprocess.file_count))
            {
                BUSTER_STRING_TEST(arguments, preprocess.files[diagnostic.location.file], S8("macro-conditional-error.c"));
            }
        }
        if (invalid[case_index].second_kind != C_DIAGNOSTIC_KIND_COUNT)
        {
            BUSTER_TEST(arguments, preprocess.diagnostic_count >= 2);
            if (preprocess.diagnostic_count >= 2)
            {
                CDiagnostic diagnostic = preprocess.diagnostics[1];
                BUSTER_TEST(arguments, diagnostic.kind == invalid[case_index].second_kind);
                BUSTER_TEST(arguments, diagnostic.location.line == invalid[case_index].second_line);
                BUSTER_TEST(arguments, diagnostic.location.column == invalid[case_index].second_column);
                if (BUSTER_REQUIRE(arguments, diagnostic.location.file < preprocess.file_count))
                {
                    BUSTER_STRING_TEST(arguments, preprocess.files[diagnostic.location.file], S8("macro-conditional-error.c"));
                }
            }
        }
        scratch_end(temporary);
    }
#if !BUSTER_ANDROID && !BUSTER_IOS
    String8 runtime_source = S8("#define F(x) x\n#define ALIAS F\n#define ID(x) x\n"
                                "_Static_assert(F\n(11) == 11, \"direct newline invocation\");\n"
                                "_Static_assert(ALIAS /* split\ncomment */\r\n(12) == 12, \"alias CRLF invocation\");\n"
                                "_Static_assert(ID(ALIAS)\n(ID(F\n(13))) == 13, \"nested newline rescan\");\n"
                                "#define RESTORED 1\n#pragma push_macro(\"RESTORED\")\n#undef RESTORED\n#define RESTORED 2\n"
                                "#define RESTORE _Pragma(\"pop_macro(\\\"RESTORED\\\")\")\n"
                                "RESTORE _Static_assert(RESTORED == 1, \"same-line restoration\");\n"
                                "_Pragma(\"pack(push, 1)\") struct PackedRescan { char byte; int value; }; "
                                "_Pragma(\"pack(pop)\") struct NaturalRescan { char byte; int value; };\n"
                                "_Static_assert(sizeof(struct PackedRescan) == 5, \"pack marker position\");\n"
                                "_Static_assert(sizeof(struct NaturalRescan) == 8, \"pack pop marker position\");\n"
                                "#define ENABLED 1\n"
                                "#define SELECT(x) x\n"
                                "#define VALUES(...) __VA_ARGS__\n"
                                "#if !(u'\\0' - 1 > 0)\n"
                                "#error UTF-16 character type lost in driver preprocessing\n"
                                "#endif\n"
                                "#if !(U'\\0' - 1 > 0)\n"
                                "#error UTF-32 character type lost in driver preprocessing\n"
                                "#endif\n"
                                "#if (1 ? -1 : u'\\0') < 0\n"
                                "#error conditional common type lost in driver preprocessing\n"
                                "#endif\n"
                                "#define WIDE_PROMOTES_UNSIGNED _Generic(+(L'\\0'), unsigned int: 1, default: 0)\n"
                                "_Static_assert(!(u'\\0' - 1 > 0), \"ordinary UTF-16 promotes to int\");\n"
                                "_Static_assert((1 ? -1 : u'\\0') < 0, \"ordinary UTF-16 conditional promotes to int\");\n"
                                "_Static_assert(~u'\\0' == -1, \"ordinary UTF-16 complement promotes to int\");\n"
                                "_Static_assert((L'\\0' - 1 > 0) == WIDE_PROMOTES_UNSIGNED, \"ordinary wchar follows C promotions\");\n"
                                "enum { ORDINARY_UTF16_NEGATIVE = u'\\0' - 1 < 0 };\n"
                                "static int ordinary_utf16_initializer = u'\\0' - 1 < 0;\n"
                                "static int ordinary_utf16_designator[2] = { [u'\\0' - 1 < 0] = 17 };\n"
                                "static int ordinary_utf16_bound[(u'\\0' - 1 < 0) ? 2 : 1];\n"
                                "static int ordinary_utf16_return(void) { return u'\\0' - 1 < 0; }\n"
                                "int main(void)\n"
                                "{\n"
                                "    int effects = 0;\n"
                                "    int values[] = {VALUES(\n"
                                "#if ENABLED\n"
                                "        3, (4 + 5),\n"
                                "#else\n"
                                "        90, ), ((\n"
                                "#endif\n"
                                "        6)};\n"
                                "    SELECT(\n"
                                "#if ENABLED\n"
                                "        effects += 1;\n"
                                "#else\n"
                                "        effects += 100;\n"
                                "#endif\n"
                                "    )\n"
                                "    return effects != 1 || sizeof(values) / sizeof(values[0]) != 3 ||\n"
                                "           values[0] != 3 || values[1] != 9 || values[2] != 6 ||\n"
                                "           ORDINARY_UTF16_NEGATIVE != 1 || ordinary_utf16_initializer != 1 ||\n"
                                "           ordinary_utf16_designator[0] != 0 || ordinary_utf16_designator[1] != 17 ||\n"
                                "           sizeof(ordinary_utf16_bound) / sizeof(ordinary_utf16_bound[0]) != 2 ||\n"
                                "           ordinary_utf16_return() != 1;\n"
                                "}\n");
    String8 frontend_flags[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 frontend_index = 0; frontend_index < BUSTER_ARRAY_LENGTH(frontend_flags); frontend_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 source_path = buster_test_temporary_path(temporary.arena, S8("buster-c-macro-conditional"), S8(".c"));
        String8 output_path = buster_test_temporary_path(temporary.arena, S8("buster-c-macro-conditional"), S8(""));
        BUSTER_TEST(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(runtime_source)));
        String8 command[] = {
            S8("-nostdinc"), S8("-std=c17"), frontend_flags[frontend_index], S8("-o"), output_path, source_path,
        };
        CompilerDriverResult compiled = compiler_driver_execute_invocation(
            temporary.arena, compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
        if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 run[] = {output_path};
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                                        (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(temporary.arena, spawn, 30000000);
                BUSTER_TEST(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(temporary);
    }
#endif
    return result;
}
#endif
