// GCC/Clang-compatible macro source boundaries: ordinary newline lookahead,
// source conditionals inside arguments, and push/pop effects at the rescan
// cursor. Pin tokens, expansion ownership, and diagnostic source attribution.
#include <buster/tests/compiler/frontend/c/macro_conditional_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/file.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL UnitTestResult c_macro_conditional_compare_semantic_tokens(UnitTestArguments* arguments, CLexResult actual, CLexResult expected)
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
    BUSTER_TEST(arguments, actual_count == expected_count);
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
    } cases[] = {
        {S8("#define F(x) x\nF\n(11)\n"), S8("11")},
        {S8("#define F(x) x\nF\n(__LINE__)\n"), S8("3")},
        {S8("#define F(x) x\n#define A F\nA\n(__LINE__)\n"), S8("4")},
        {S8("#define F(x) x\n#define ID(x) x\nID(F\n(__LINE__))\n"), S8("4")},
        {S8("#define F(x) x\n#line 70 \"rescan-lines.c\"\nF\n(__LINE__)\n"), S8("71")},
        {S8("#define F(x) x\n#define A F\n#define B A\nB\n\n(12)\n"), S8("12")},
        {S8("#define F(x) x\nF /* comment\ncontinued */\n/* between */ (13)\n"), S8("13")},
        {S8("#define F(x) x\r\nF\r\n\r\n(14)\r\n"), S8("14")},
        {S8("#define F(x) x\n#define ID(x) x\n#define A F\nID(A)\n(ID(F\n(15)))\n"), S8("15")},
        {S8("#define F(x) x\n#define TAIL(x) x F\nTAIL(16)\n(17)\n"), S8("16 17")},
        {S8("#define F(x) x\n#define A F\nF\nname A\n+ 18\n"), S8("F name F + 18")},
        {S8("#define F(x) x\n#define A F\nA\n#undef A\n#define A 19\nA\n"), S8("F 19")},
        {S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "_Pragma(\"pop_macro(\\\"X\\\")\") X\n"), S8("1")},
        {S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "_Pragma(\"pop_macro(\\\"X\\\")\")\nX\n"), S8("1")},
        {S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "#define RESTORE _Pragma(\"pop_macro(\\\"X\\\")\")\n#define ID(x) x\n"
            "ID(RESTORE X) X\n"), S8("1 1")},
        {S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n#pragma push_macro(\"X\")\n"
            "#undef X\n#define X 3\n#define DUP(x) x x\n"
            "DUP(_Pragma(\"pop_macro(\\\"X\\\")\") X) X\n"), S8("2 2 2")},
        {S8("#define X 1\n#pragma push_macro(\"X\")\n#undef X\n#define X 2\n"
            "_Pragma(\"push_macro(\\\"X\\\")\") _Pragma(\"pop_macro(\\\"X\\\")\") X "
            "_Pragma(\"pop_macro(\\\"X\\\")\") X\n"), S8("2 1")},
        {S8("#pragma push_macro(\"MISSING\")\n#define MISSING 9\n"
            "_Pragma(\"pop_macro(\\\"MISSING\\\")\") MISSING\n"), S8("MISSING")},
        // The two-argument invocation owns its original definition even when
        // argument prescan restores the one-argument definition for later use.
        {S8("#define F(x) x\n#pragma push_macro(\"F\")\n#undef F\n#define F(x,y) x + y\n"
            "F(_Pragma(\"pop_macro(\\\"F\\\")\") 1,2) F(3)\n"), S8("1 + 2 3")},
        {S8("#define F(x) x\n#pragma push_macro(\"F\")\n#undef F\n#define F(x,y) x #y\n"
            "F(_Pragma(\"pop_macro(\\\"F\\\")\") 4,5) F(6)\n"), S8("4 \"5\" 6")},
        {S8("#pragma push_macro(\"X\")\n#define X _Pragma(\"pop_macro(\\\"X\\\")\")\n"
            "X\n#define X 1\nX\n"), S8("1")},
        // Omission belongs to the suspended variadic definition even when
        // pop_macro restores an object-like name with no parameters. An
        // explicitly empty final argument must retain the separating comma.
        {S8("#define M 7\n#pragma push_macro(\"M\")\n#undef M\n#define M(x,...) x , ##__VA_ARGS__\n"
            "M(_Pragma(\"pop_macro(\\\"M\\\")\") 11) M\n"), S8("11 7")},
        {S8("#define M 7\n#pragma push_macro(\"M\")\n#undef M\n#define M(x,...) x , ##__VA_ARGS__\n"
            "M(_Pragma(\"pop_macro(\\\"M\\\")\") 11,) M\n"), S8("11 , 7")},
        // The dynamic builtin kind is part of the saved definition, including
        // a regular function invocation suspended while the builtin is restored.
        {S8("#line 100 \"builtin-restored.c\"\n#pragma push_macro(\"__LINE__\")\n#undef __LINE__\n"
            "#define __LINE__ 7\n#pragma pop_macro(\"__LINE__\")\n__LINE__\n"), S8("104")},
        {S8("#line 100 \"builtin-restored.c\"\n#pragma push_macro(\"__FILE__\")\n#undef __FILE__\n"
            "#define __FILE__ \"replacement.c\"\n#pragma pop_macro(\"__FILE__\")\n__FILE__\n"), S8("\"builtin-restored.c\"")},
        {S8("#pragma push_macro(\"__LINE__\")\n#undef __LINE__\n#define __LINE__(x,y) x + y\n"
            "__LINE__(_Pragma(\"pop_macro(\\\"__LINE__\\\")\") 1,2) __LINE__\n"), S8("1 + 2 4")},
        {S8("#pragma push_macro(\"__LINE__\")\n#undef __LINE__\n"
            "#define __LINE__ _Pragma(\"pop_macro(\\\"__LINE__\\\")\") __LINE__\n__LINE__\n"), S8("4")},
        // Restoring the same active generation keeps its replacement disabled.
        {S8("#define SAME _Pragma(\"push_macro(\\\"SAME\\\")\") "
            "_Pragma(\"pop_macro(\\\"SAME\\\")\") SAME\nSAME\n"), S8("SAME")},
        // A restored different generation is enabled inside the old replacement.
        {S8("#define SELF 7\n#pragma push_macro(\"SELF\")\n#undef SELF\n"
            "#define SELF _Pragma(\"pop_macro(\\\"SELF\\\")\") SELF\nSELF SELF\n"), S8("7 7")},
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
                    BUSTER_TEST_RAW(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS,
                                    BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]));
                    if (BUSTER_REQUIRE(arguments, !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS))
                    {
                        CLexResult reference = c_lex(temporary.arena, BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]));
                        CLexResult expected = c_lex(temporary.arena, cases[case_index].expected);
                        UnitTestResult reference_result = c_macro_conditional_compare_semantic_tokens(arguments, reference, expected);
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

UnitTestResult c_macro_conditional_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, c_macro_rescan_boundary_tests);
    UnitTestResult demand = c_macro_argument_demand_tests(arguments);
    result.test_count += demand.test_count;
    result.succeeded_test_count += demand.succeeded_test_count;
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
                UnitTestResult reference_result = c_macro_conditional_compare_semantic_tokens(arguments, reference, expected);
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
