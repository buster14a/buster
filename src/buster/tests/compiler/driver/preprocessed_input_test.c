// Included by driver_test.c. The phase distinction is exercised through both
// the public preprocessing options and the command-line driver contract.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_preprocessed_c_input(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;

    String8 cpp_output_command[] = {S8("-x"), S8("cpp-output"), S8("input")};
    CompilerDriverInvocation cpp_output = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(cpp_output_command));
    BUSTER_TEST(arguments, cpp_output.error == COMPILER_DRIVER_ERROR_NONE && cpp_output.language == COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT);
    String8 c_command[] = {S8("-x"), S8("c"), S8("input.i")};
    CompilerDriverInvocation c = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_command));
    BUSTER_TEST(arguments, c.error == COMPILER_DRIVER_ERROR_NONE && c.language == COMPILER_DRIVER_LANGUAGE_C);
    String8 automatic_command_line[] = {S8("-x"), S8("c"), S8("-x"), S8("none"), S8("input.i")};
    CompilerDriverInvocation automatic_language =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(automatic_command_line));
    BUSTER_TEST(arguments, automatic_language.error == COMPILER_DRIVER_ERROR_NONE &&
                               automatic_language.language == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);

    CPreprocessorDefinition definition = {
        .name = S8("marker"),
        .value = S8("7"),
    };
    CPreprocessResult preprocessed = c_preprocess(arena, S8("#define marker 9\nint marker;\nint __LINE__;\n"),
                                                  (CPreprocessOptions){
                                                      .definitions = &definition,
                                                      .source_path = S8("direct.i"),
                                                      .definition_count = 1,
                                                      .dialect = C_PREPROCESS_DIALECT_C17,
                                                      .already_preprocessed = true,
                                                  });
    BUSTER_TEST(arguments, preprocessed.error_count == 0);
    bool retained_marker = false;
    bool retained_line_builtin = false;
    for (u64 token_index = 0; token_index < preprocessed.token_count; token_index += 1)
    {
        CToken token = preprocessed.tokens[token_index];
        retained_marker |= token.kind == C_TOKEN_IDENTIFIER &&
                           string_equal(c_token_spelling(preprocessed.spelling_base, token), S8("marker"));
        retained_line_builtin |= token.kind == C_TOKEN_IDENTIFIER &&
                                 string_equal(c_token_spelling(preprocessed.spelling_base, token), S8("__LINE__"));
    }
    BUSTER_TEST(arguments, retained_marker && retained_line_builtin);

    CPreprocessResult mapped = c_preprocess(arena, S8("# 41 \"logical-input.c\"\nint 7(void);\n"),
                                            (CPreprocessOptions){
                                                .source_path = S8("physical-input.i"),
                                                .dialect = C_PREPROCESS_DIALECT_C17,
                                                .already_preprocessed = true,
                                            });
    CParseResult mapped_parse = c_parse(arena, mapped);
    BUSTER_TEST(arguments, mapped.error_count == 0 && mapped_parse.diagnostic_count != 0);
    if (mapped_parse.diagnostic_count)
    {
        CSourceLocation location = mapped_parse.diagnostics[0].location;
        BUSTER_TEST(arguments, location.line == 41 && location.file < mapped.file_count);
        if (location.file < mapped.file_count)
        {
            BUSTER_STRING_TEST(arguments, mapped.files[location.file], S8("logical-input.c"));
        }
    }

    String8 source_path = buster_test_temporary_path(arena, S8("buster-preprocessed-input"), S8(".c"));
    String8 saved_path = buster_test_temporary_path(arena, S8("buster-preprocessed-input"), S8(".i"));
    String8 roundtrip_path = buster_test_temporary_path(arena, S8("buster-preprocessed-roundtrip"), S8(".i"));
    String8 extensionless_path = buster_test_temporary_path(arena, S8("buster-preprocessed-input-extensionless"), S8(""));
    BUSTER_TEST(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("#undef marker\nint marker(void);\n"))));

    String8 emit_command[] = {S8("-nostdinc"), S8("-std=c17"), S8("-Dmarker=7"), S8("-E"), S8("-o"), saved_path, source_path};
    CompilerDriverResult emitted = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(emit_command)));
    BUSTER_TEST(arguments, emitted.error == COMPILER_DRIVER_ERROR_NONE);

    ByteSlice saved = file_read(arena, saved_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, saved.pointer != 0 && saved.length != 0);
    if (saved.pointer)
    {
        BUSTER_TEST(arguments, file_write(extensionless_path, saved));
    }

    String8 automatic_command[] = {S8("-nostdinc"), S8("-std=c17"), S8("-Dmarker=7"), S8("-fsyntax-only"), saved_path};
    CompilerDriverResult automatic = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(automatic_command)));
    BUSTER_TEST(arguments, automatic.error == COMPILER_DRIVER_ERROR_NONE);

    String8 roundtrip_command[] = {S8("-nostdinc"), S8("-std=c17"), S8("-Dmarker=7"), S8("-E"), S8("-o"), roundtrip_path, saved_path};
    CompilerDriverResult roundtrip = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(roundtrip_command)));
    BUSTER_TEST(arguments, roundtrip.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice roundtrip_bytes = file_read(arena, roundtrip_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, roundtrip_bytes.pointer != 0);
    if (roundtrip_bytes.pointer)
    {
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, roundtrip_bytes), BYTE_SLICE_TO_STRING(8, saved));
    }

    String8 explicit_command[] = {S8("-nostdinc"), S8("-std=c17"), S8("-Dmarker=7"), S8("-fsyntax-only"),
                                  S8("-x"), S8("cpp-output"), extensionless_path};
    CompilerDriverResult explicit_cpp_output = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(explicit_command)));
    BUSTER_TEST(arguments, explicit_cpp_output.error == COMPILER_DRIVER_ERROR_NONE);

    String8 raw_override_command[] = {S8("-nostdinc"), S8("-std=c17"), S8("-Dmarker=7"), S8("-fsyntax-only"),
                                      S8("-x"), S8("c"), saved_path};
    CompilerDriverResult raw_override = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(raw_override_command)));
    BUSTER_TEST(arguments, raw_override.error != COMPILER_DRIVER_ERROR_NONE);

    BUSTER_TEST(arguments, os_file_delete(source_path));
    BUSTER_TEST(arguments, os_file_delete(saved_path));
    BUSTER_TEST(arguments, os_file_delete(roundtrip_path));
    BUSTER_TEST(arguments, os_file_delete(extensionless_path));
    scratch_end(temporary);
    return result;
}
