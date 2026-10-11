// Included by driver_test.c. #3628: literal macro filename remapping,
// independent oracle observations, physical includes/diagnostics/depfiles.
typedef struct CompilerDriverPrefixMapCase CompilerDriverPrefixMapCase;
struct CompilerDriverPrefixMapCase
{
    String8 option;
    String8 second;
    String8 logical_path;
    String8 expected_file;
    String8 clang_file;
};

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_macro_prefix_map(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CompilerDriverPrefixMapCase cases[] = {
        {S8("-fmacro-prefix-map=prefix/=mapped/"), {0}, S8("prefix/file.c"), S8("\"mapped/file.c\""), S8("\"mapped/file.c\"")},
        {S8("-fmacro-prefix-map=other/=mapped/"), {0}, S8("prefix/file.c"), S8("\"prefix/file.c\""), S8("\"prefix/file.c\"")},
        {S8("-fmacro-prefix-map=prefix/="), {0}, S8("prefix/file.c"), S8("\"file.c\""), S8("\"file.c\"")},
        {S8("-fmacro-prefix-map==added/"), {0}, S8("prefix/file.c"), S8("\"added/prefix/file.c\""), S8("\"added/prefix/file.c\"")},
        {S8("-fmacro-prefix-map=="), {0}, S8("prefix/file.c"), S8("\"prefix/file.c\""), S8("\"prefix/file.c\"")},
        {S8("-fmacro-prefix-map=prefix/=prefix/"), {0}, S8("prefix/file.c"), S8("\"prefix/file.c\""), S8("\"prefix/file.c\"")},
        {S8("-fmacro-prefix-map=pre=partial"), {0}, S8("prefix/file.c"), S8("\"partialfix/file.c\""), S8("\"partialfix/file.c\"")},
        {S8("-fmacro-prefix-map=PREFIX/=mapped/"), {0}, S8("prefix/file.c"), S8("\"prefix/file.c\""), S8("\"prefix/file.c\"")},
        {S8("-fmacro-prefix-map=prefix/=first/"), S8("-fmacro-prefix-map=prefix/=last/"), S8("prefix/file.c"),
         S8("\"last/file.c\""), S8("\"first/file.c\"")},
        {S8("-fmacro-prefix-map=prefix/=short/"), S8("-fmacro-prefix-map=prefix/file=long"), S8("prefix/file.c"),
         S8("\"long.c\""), S8("\"long.c\"")},
        {S8("-fmacro-prefix-map=prefix/file=long"), S8("-fmacro-prefix-map=prefix/=short/"), S8("prefix/file.c"),
         S8("\"short/file.c\""), S8("\"long.c\"")},
        {S8("-fmacro-prefix-map=prefix/=mapped/"), S8("-fmacro-prefix-map=mapped/=twice/"), S8("prefix/file.c"),
         S8("\"mapped/file.c\""), S8("\"mapped/file.c\"")},
        {S8("-fmacro-prefix-map=prefix=build/=mapped/"), {0}, S8("prefix=build/file.c"),
         S8("\"mapped/file.c\""), S8("\"build/=mapped/=build/file.c\"")},
        {S8("-fmacro-prefix-map=prefix/=quote\"\\\n7/"), {0}, S8("prefix/file.c"),
         S8("\"quote\\\"\\\\\\0127/file.c\""), {0}},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        CompilerDriverPrefixMapCase item = cases[index];
        String8 argv[] = {S8("-E"), item.option, item.second, S8("prefix/base.c")};
        SliceString8 command = {.pointer = argv, .length = BUSTER_ARRAY_LENGTH(argv)};
        if (!item.second.length)
        {
            argv[2] = argv[3];
            command.length -= 1;
        }
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, command);
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, invocation.macro_prefix_map_count == (item.second.length ? 2 : 1));
        String8 source = string_format(arena, S8("#line 70 \"{S8}\"\n#define GET(x) x\nGET(__FILE__) __FILE_NAME__ marker\n#warning physical\n"),
                                       item.logical_path);
        CPreprocessResult preprocess = c_preprocess(arena, source, (CPreprocessOptions){
            .source_path = S8("prefix/base.c"),
            .macro_prefix_maps = invocation.macro_prefix_maps,
            .macro_prefix_map_count = invocation.macro_prefix_map_count,
        });
        if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && preprocess.token_count == 4))
        {
            BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocess.tokens[0]), item.expected_file);
            CSourceLocation location = c_preprocess_token_location(&preprocess, preprocess.tokens[2]);
            BUSTER_TEST(arguments, location.line == 71);
            if (BUSTER_REQUIRE(arguments, location.file < preprocess.file_count))
            {
                BUSTER_STRING_TEST(arguments, preprocess.files[location.file], item.logical_path);
            }
            BUSTER_TEST(arguments, preprocess.warning_count == 1);
        }
        c_preprocess_release(&preprocess);
#if BUSTER_LINUX && !BUSTER_ANDROID
        if (item.clang_file.length && string_equal(os_get_environment_variable(S8("BUSTER_PREFIX_MAP_ORACLES")), S8("1")))
        {
            String8 oracle_source = buster_test_temporary_path(arena, S8("prefix-map-oracle"), S8(".c"));
            String8 text = string_format(arena, S8("#line 70 \"{S8}\"\nPREFIX_RESULT __FILE__\n"), item.logical_path);
            if (BUSTER_REQUIRE(arguments, file_write(oracle_source, BUSTER_SLICE_TO_BYTE_SLICE(text))))
            {
                String8 oracles[] = {S8("gcc"), S8("clang")};
                for (u32 oracle = 0; oracle < BUSTER_ARRAY_LENGTH(oracles); oracle += 1)
                {
                    String8 argv_oracle[] = {oracles[oracle], S8("-E"), item.option, item.second, oracle_source};
                    u32 count = BUSTER_ARRAY_LENGTH(argv_oracle);
                    if (!item.second.length)
                    {
                        argv_oracle[3] = oracle_source;
                        count -= 1;
                    }
                    ProcessSpawnResult spawned = os_process_spawn((SliceString8){.pointer = argv_oracle, .length = count},
                        (SliceString8){0}, (SliceString8){0},
                        (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                              .use_process_environment = true, .search_path = true});
                    if (BUSTER_REQUIRE(arguments, spawned.handle != 0))
                    {
                        ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
                        BUSTER_TEST(arguments, !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS);
                        String8 output_text = BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_OUTPUT]);
                        String8 expected = string_format(arena, S8("PREFIX_RESULT {S8}"), oracle ? item.clang_file : item.expected_file);
                        BUSTER_TEST(arguments, string_first_sequence(output_text, expected) != BUSTER_STRING_NO_MATCH);
                        if (string_first_sequence(output_text, expected) == BUSTER_STRING_NO_MATCH)
                        {
                            string_print(S8("PREFIX_MAP_ORACLE case={u32} compiler={S8} expected={S8} actual={S8}\n"),
                                         index, oracles[oracle], expected, output_text);
                        }
                    }
                }
            }
        }
#endif
        invocation.input_source = source;
        CompilerDriverResult output = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST(arguments, output.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, string_first_sequence(output.output, item.expected_file) != BUSTER_STRING_NO_MATCH);
        scratch_end(temporary);
    }
    // Basenames are taken before GCC's mapping, and #line macro operands
    // retain their physical spelling; the directive must not cascade a map.
    CMacroPrefixMap maps[] = {{S8("prefix/"), S8("mapped/")}, {S8("file"), S8("renamed")}};
    CPreprocessResult directive = c_preprocess(arguments->arena,
        S8("#line 12 __FILE__\n__FILE__ __BASE_FILE__ __FILE_NAME__\n"),
        (CPreprocessOptions){.source_path = S8("prefix/file.c"), .macro_prefix_maps = maps, .macro_prefix_map_count = 2});
    if (BUSTER_REQUIRE(arguments, directive.error_count == 0 && directive.token_count == 4))
    {
        BUSTER_STRING_TEST(arguments, c_token_spelling(directive.spelling_base, directive.tokens[0]), S8("\"mapped/file.c\""));
        BUSTER_STRING_TEST(arguments, c_token_spelling(directive.spelling_base, directive.tokens[1]), S8("\"mapped/file.c\""));
        BUSTER_STRING_TEST(arguments, c_token_spelling(directive.spelling_base, directive.tokens[2]), S8("\"renamed.c\""));
    }
    c_preprocess_release(&directive);
    String8 invalid[] = {S8("-fmacro-prefix-map"), S8("-fmacro-prefix-map="), S8("-fmacro-prefix-map=missing")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        String8 argv[] = {S8("-E"), invalid[index], S8("unused.c")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    String8 terminated[] = {S8("-E"), S8("--"), S8("-fmacro-prefix-map=old=new")};
    CompilerDriverInvocation ended = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(terminated));
    BUSTER_TEST(arguments, ended.macro_prefix_map_count == 0 && ended.input_count == 1);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_macro_prefix_map_files(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if !BUSTER_ANDROID && !BUSTER_IOS
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 directory = buster_test_temporary_path(arena, S8("prefix-map"), S8(""));
    BUSTER_TEST(arguments, os_make_directory_attempt(directory));
    String8 source_path = string_format_z(arena, S8("{S8}/source.c"), directory);
    String8 header_path = string_format_z(arena, S8("{S8}/header.h"), directory);
    String8 object_path = string_format_z(arena, S8("{S8}/source.o"), directory);
    String8 depfile_path = string_format_z(arena, S8("{S8}/source.d"), directory);
    String8 header = S8("#pragma once\nchar const header_file[] = __FILE__;\nchar const header_base[] = __BASE_FILE__;\n#warning header-physical\n");
    String8 source = S8("#include \"header.h\"\n#include \"header.h\"\nchar const source_file[] = __FILE__;\nchar const source_base[] = __BASE_FILE__;\n");
    String8 option = string_format_z(arena, S8("-fmacro-prefix-map={S8}/=mapped/"), directory);
    if (BUSTER_REQUIRE(arguments, file_write(header_path, BUSTER_SLICE_TO_BYTE_SLICE(header)) &&
                                  file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source))))
    {
        String8 argv[] = {S8("-E"), option, source_path};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv));
        CompilerDriverResult output = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST(arguments, output.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, compiler_driver_test_contains(output.output, S8("header_file[] = \"mapped/header.h\"")));
        BUSTER_TEST(arguments, compiler_driver_test_contains(output.output, S8("header_base[] = \"mapped/source.c\"")));
        BUSTER_TEST(arguments, compiler_driver_test_contains(output.output, S8("source_file[] = \"mapped/source.c\"")));
        BUSTER_TEST(arguments, compiler_driver_test_contains(output.warning, header_path));
        // Real include identity still suppresses the second declaration.
        u64 first = string_first_sequence(output.output, S8("header_file"));
        BUSTER_TEST(arguments, first != BUSTER_STRING_NO_MATCH);
        if (first != BUSTER_STRING_NO_MATCH)
        {
            BUSTER_TEST(arguments, string_first_sequence(string_slice(output.output, first + S8("header_file").length, output.output.length),
                                                        S8("header_file")) == BUSTER_STRING_NO_MATCH);
        }
        for (u32 mode = 0; mode < 2; mode += 1)
        {
            String8 compile[] = {S8("-c"), S8("-MMD"), S8("-MF"), depfile_path, S8("-o"), object_path, option, source_path};
            CompilerDriverInvocation compile_invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile));
            compile_invocation.disable_direct_ssa = mode != 0;
            CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, compile_invocation);
            BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
            CompilerDriverDependencyTestRule rule = {0};
            if (BUSTER_REQUIRE(arguments, compiler_driver_dependency_test_parse_rule(arena, depfile_path, &rule)))
            {
                BUSTER_TEST(arguments, rule.prerequisite_count == 2);
                if (BUSTER_REQUIRE(arguments, rule.prerequisite_count == 2))
                {
                    BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(rule.prerequisites[0], source_path));
                    BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(rule.prerequisites[1], header_path));
                }
            }
        }
        // Explicit hosted oracle runs use the same actual source and argv.
        // Prefix-map-only -E output retains no line markers in Buster, so
        // compare independently expected macro fragments, not printer bytes.
#if BUSTER_LINUX && !BUSTER_ANDROID
        if (string_equal(os_get_environment_variable(S8("BUSTER_PREFIX_MAP_ORACLES")), S8("1")))
        {
            String8 oracles[] = {S8("gcc"), S8("clang")};
            for (u32 oracle = 0; oracle < BUSTER_ARRAY_LENGTH(oracles); oracle += 1)
            {
                String8 command[] = {oracles[oracle], S8("-E"), option, source_path};
                ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                    (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                          .use_process_environment = true, .search_path = true});
                if (BUSTER_REQUIRE(arguments, spawned.handle != 0))
                {
                    ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
                    BUSTER_TEST(arguments, !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS);
                    String8 text = BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_OUTPUT]);
                    BUSTER_TEST(arguments, compiler_driver_test_contains(text, S8("header_file[] = \"mapped/header.h\"")));
                    BUSTER_TEST(arguments, compiler_driver_test_contains(text, S8("header_base[] = \"mapped/source.c\"")));
                    BUSTER_TEST(arguments, compiler_driver_test_contains(text, S8("source_file[] = \"mapped/source.c\"")));
                }
            }
        }
#endif
    }
    scratch_end(temporary);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
