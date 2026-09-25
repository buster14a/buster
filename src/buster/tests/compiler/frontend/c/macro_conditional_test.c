// GCC/Clang-compatible source conditional directives inside function-like
// macro arguments. These tests exercise the real preprocessor and pin active
// token selection, expansion interactions, and diagnostic source attribution.
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

UnitTestResult c_macro_conditional_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
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
    String8 runtime_source = S8("#define ENABLED 1\n"
                                "#define SELECT(x) x\n"
                                "#define VALUES(...) __VA_ARGS__\n"
                                "#define WIDE_PROMOTES_UNSIGNED _Generic(+(L'\\0'), unsigned int: 1, default: 0)\n"
                                "_Static_assert(sizeof(u'\\0') == 2, \"UTF-16 character width\");\n"
                                "_Static_assert(sizeof(U'\\0') == 4, \"UTF-32 character width\");\n"
                                "_Static_assert(!(u'\\0' - 1 > 0), \"ordinary UTF-16 promotes to int\");\n"
                                "_Static_assert(U'\\0' - 1 > 0, \"ordinary UTF-32 keeps unsigned int\");\n"
                                "_Static_assert((1 ? -1 : u'\\0') < 0, \"ordinary UTF-16 conditional promotes to int\");\n"
                                "_Static_assert(~u'\\0' == -1, \"ordinary UTF-16 complement promotes to int\");\n"
                                "_Static_assert((L'\\0' - 1 > 0) == WIDE_PROMOTES_UNSIGNED, \"ordinary wchar follows C promotions\");\n"
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
                                "           values[0] != 3 || values[1] != 9 || values[2] != 6;\n"
                                "}\n");
    String8 frontend_flags[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
    for (u32 frontend_index = 0; frontend_index < BUSTER_ARRAY_LENGTH(frontend_flags); frontend_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 source_path = buster_test_temporary_path(temporary.arena, S8("buster-c-macro-conditional"), S8(".c"));
        String8 output_path = buster_test_temporary_path(temporary.arena, S8("buster-c-macro-conditional"), S8(""));
        BUSTER_TEST(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(runtime_source)));
        String8 command[] = {
            S8("-nostdinc"), frontend_flags[frontend_index], S8("-o"), output_path, source_path,
        };
        CompilerDriverResult compiled = compiler_driver_execute_invocation(
            temporary.arena, compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
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
