#include <buster/tests/compiler/frontend/c/once_test.h>

#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS

// These fixtures intentionally keep the root source smaller than the include
// closure.  The old once-path array used the root byte count as its capacity,
// so the distinct-import cases cross that boundary while remaining small
// enough for every supported test runner.
#define C_ONCE_TEST_IMPORT_COUNT 96u
#define C_ONCE_TEST_PRAGMA_COUNT 96u
#define C_ONCE_TEST_LOOKUP_COUNT 96u
#define C_ONCE_TEST_DEPTH_COUNT 16u
#define C_ONCE_TEST_DEPTH_LIMIT 8u

BUSTER_GLOBAL_LOCAL String8 c_once_test_directory(String8 path)
{
    String8 result = {0};
    u64 index = path.length;
    while (index && !result.length)
    {
        index -= 1;
        if (path.pointer[index] == '/' || path.pointer[index] == '\\')
        {
            result = string_slice(path, 0, index);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 c_once_test_child_path(Arena* arena, String8 directory, String8 name)
{
    String8 result = string_format_z(arena, S8("{S8}/{S8}"), directory, name);
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_once_test_write(String8 path, String8 source)
{
    ByteSlice bytes = {
        .pointer = (u8*)source.pointer,
        .length = source.length,
    };
    bool result = file_write(path, bytes);
    return result;
}

BUSTER_GLOBAL_LOCAL void c_once_test_append(char8* destination, u64 capacity, u64* length, String8 source)
{
    BUSTER_CHECK(*length <= capacity && source.length <= capacity - *length);
    memcpy(destination + *length, source.pointer, source.length);
    *length += source.length;
}

BUSTER_GLOBAL_LOCAL String8 c_once_test_include_lines(Arena* arena, String8 prefix, String8 directive, u32 count, u32 repetitions)
{
    u64 capacity = (u64)count * repetitions * 96 + 1;
    char8* bytes = arena_allocate(arena, char8, capacity);
    u64 length = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        for (u32 repetition = 0; repetition < repetitions; repetition += 1)
        {
            String8 line = string_format(arena, S8("{S8} \"{S8}-{u32}.h\"\n"), directive, prefix, index);
            c_once_test_append(bytes, capacity, &length, line);
        }
    }
    return (String8){.pointer = bytes, .length = length};
}

BUSTER_GLOBAL_LOCAL bool c_once_test_write_number_leaves(Arena* arena, String8 directory, String8 prefix, u32 count)
{
    bool result = true;
    for (u32 index = 0; index < count; index += 1)
    {
        String8 name = string_format(arena, S8("{S8}-{u32}.h"), prefix, index);
        String8 path = c_once_test_child_path(arena, directory, name);
        String8 source = string_format(arena, S8("{u32}\n"), index);
        result = c_once_test_write(path, source) && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_once_test_write_pragma_leaves(Arena* arena, String8 directory, String8 prefix, u32 count)
{
    bool result = true;
    for (u32 index = 0; index < count; index += 1)
    {
        String8 name = string_format(arena, S8("{S8}-{u32}.h"), prefix, index);
        String8 path = c_once_test_child_path(arena, directory, name);
        String8 source = string_format(arena, S8("#pragma once\n{u32}\n"), index);
        result = c_once_test_write(path, source) && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void c_once_test_numbers(UnitTestArguments* arguments, UnitTestResult* outer_result, CPreprocessResult preprocess,
                                             u32 count)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, preprocess.token_count == (u64)count + 1);
    if (BUSTER_REQUIRE(arguments, preprocess.tokens != 0 && preprocess.spelling_base != 0 && preprocess.token_count >= (u64)count + 1))
    {
        for (u32 index = 0; index < count; index += 1)
        {
            BUSTER_TEST(arguments, preprocess.tokens[index].kind == C_TOKEN_PREPROCESSING_NUMBER);
            String8 expected = string_format(arguments->arena, S8("{u32}"), index);
            BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), expected);
        }
        BUSTER_TEST(arguments, preprocess.tokens[count].kind == C_TOKEN_END_OF_FILE);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL void c_once_test_single_lexed_rows(UnitTestArguments* arguments, UnitTestResult* outer_result,
                                                       CPreprocessResult preprocess, u32 expected_file_count)
{
    UnitTestResult result = {0};
    CPreprocessDetail const* detail = c_preprocess_detail(preprocess);
    BUSTER_TEST(arguments, detail->source_lexed.files == expected_file_count);
    BUSTER_TEST(arguments, detail->source_unique.files == expected_file_count);
    BUSTER_TEST(arguments, detail->lexed_file_count == expected_file_count);
    if (BUSTER_REQUIRE(arguments, detail->lexed_files != 0 && detail->lexed_file_count == expected_file_count))
    {
        for (u32 index = 0; index < expected_file_count; index += 1)
        {
            BUSTER_TEST(arguments, detail->lexed_files[index].lex_count == 1);
        }
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_import_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-import-growth"), S8(".c"));
    String8 directory = c_once_test_directory(root_path);
    String8 prefix = S8("buster-issue81-import-leaf");
    String8 fanout_name = S8("buster-issue81-import-fanout.h");
    String8 fanout_path = c_once_test_child_path(temporary.arena, directory, fanout_name);
    String8 fanout_source = c_once_test_include_lines(temporary.arena, prefix, S8("#import"), C_ONCE_TEST_IMPORT_COUNT, 1);
    String8 root_source = string_format(temporary.arena, S8("#include \"{S8}\"\n"), fanout_name);
    bool files_written = c_once_test_write(root_path, root_source);
    files_written = c_once_test_write(fanout_path, fanout_source) && files_written;
    files_written = c_once_test_write_number_leaves(temporary.arena, directory, prefix, C_ONCE_TEST_IMPORT_COUNT) && files_written;
    BUSTER_TEST(arguments, root_path.pointer != 0 && directory.pointer != 0);
    BUSTER_TEST(arguments, files_written);
    if (BUSTER_REQUIRE(arguments, root_path.pointer != 0 && directory.pointer != 0 && files_written))
    {
        CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
        c_once_test_numbers(arguments, &result, preprocess, C_ONCE_TEST_IMPORT_COUNT);
        c_once_test_single_lexed_rows(arguments, &result, preprocess, C_ONCE_TEST_IMPORT_COUNT + 2);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_pragma_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-pragma-growth"), S8(".c"));
    String8 directory = c_once_test_directory(root_path);
    String8 prefix = S8("buster-issue81-pragma-leaf");
    String8 fanout_name = S8("buster-issue81-pragma-fanout.h");
    String8 fanout_path = c_once_test_child_path(temporary.arena, directory, fanout_name);
    String8 fanout_source = c_once_test_include_lines(temporary.arena, prefix, S8("#include"), C_ONCE_TEST_PRAGMA_COUNT, 2);
    String8 root_source = string_format(temporary.arena, S8("#include \"{S8}\"\n"), fanout_name);
    bool files_written = c_once_test_write(root_path, root_source);
    files_written = c_once_test_write(fanout_path, fanout_source) && files_written;
    files_written = c_once_test_write_pragma_leaves(temporary.arena, directory, prefix, C_ONCE_TEST_PRAGMA_COUNT) && files_written;
    BUSTER_TEST(arguments, root_path.pointer != 0 && directory.pointer != 0);
    BUSTER_TEST(arguments, files_written);
    if (BUSTER_REQUIRE(arguments, root_path.pointer != 0 && directory.pointer != 0 && files_written))
    {
        CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
        c_once_test_numbers(arguments, &result, preprocess, C_ONCE_TEST_PRAGMA_COUNT);
        c_once_test_single_lexed_rows(arguments, &result, preprocess, C_ONCE_TEST_PRAGMA_COUNT + 2);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_lookup_work_bound(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-lookup-work"), S8(".c"));
    String8 directory = c_once_test_directory(root_path);
    String8 prefix = S8("buster-issue81-lookup-leaf");
    String8 fanout_name = S8("buster-issue81-lookup-fanout.h");
    String8 fanout_path = c_once_test_child_path(temporary.arena, directory, fanout_name);
    // There are exactly two identity queries for every leaf. The output and
    // lexed-file rows are a deterministic work bound: all duplicate imports
    // must be suppressed, leaving one lex of each generated leaf. A probe
    // counter would tighten this oracle when the private table exposes one.
    String8 fanout_source = c_once_test_include_lines(temporary.arena, prefix, S8("#import"), C_ONCE_TEST_LOOKUP_COUNT, 2);
    String8 root_source = string_format(temporary.arena, S8("#include \"{S8}\"\n"), fanout_name);
    bool files_written = c_once_test_write(root_path, root_source);
    files_written = c_once_test_write(fanout_path, fanout_source) && files_written;
    files_written = c_once_test_write_number_leaves(temporary.arena, directory, prefix, C_ONCE_TEST_LOOKUP_COUNT) && files_written;
    BUSTER_TEST(arguments, root_path.pointer != 0 && directory.pointer != 0);
    BUSTER_TEST(arguments, files_written);
    if (BUSTER_REQUIRE(arguments, root_path.pointer != 0 && directory.pointer != 0 && files_written))
    {
        CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
        c_once_test_numbers(arguments, &result, preprocess, C_ONCE_TEST_LOOKUP_COUNT);
        c_once_test_single_lexed_rows(arguments, &result, preprocess, C_ONCE_TEST_LOOKUP_COUNT + 2);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_failures(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 missing_root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-missing-include"), S8(".c"));
    String8 malformed_root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-malformed-include"), S8(".c"));
    String8 depth_root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-depth-limit"), S8(".c"));
    String8 directory = c_once_test_directory(missing_root_path);
    String8 valid_name = S8("buster-issue81-valid.h");
    String8 valid_path = c_once_test_child_path(temporary.arena, directory, valid_name);
    bool files_written = c_once_test_write(valid_path, S8("71\n"));
    for (u32 index = 0; index < C_ONCE_TEST_DEPTH_COUNT; index += 1)
    {
        String8 name = string_format(temporary.arena, S8("buster-issue81-depth-{u32}.h"), index);
        String8 path = c_once_test_child_path(temporary.arena, directory, name);
        String8 source = index + 1 < C_ONCE_TEST_DEPTH_COUNT
                             ? string_format(temporary.arena, S8("#include \"buster-issue81-depth-{u32}.h\"\n"), index + 1)
                             : S8("91\n");
        files_written = c_once_test_write(path, source) && files_written;
    }
    BUSTER_TEST(arguments, missing_root_path.pointer != 0 && malformed_root_path.pointer != 0 && depth_root_path.pointer != 0);
    BUSTER_TEST(arguments, directory.pointer != 0 && files_written);
    if (BUSTER_REQUIRE(arguments, directory.pointer != 0 && files_written))
    {
        CPreprocessResult missing = c_preprocess(temporary.arena,
                                                 string_format(temporary.arena,
                                                               S8("#include \"buster-issue81-absent.h\"\n#include \"{S8}\"\n"), valid_name),
                                                 (CPreprocessOptions){.source_path = missing_root_path});
        BUSTER_TEST(arguments, missing.diagnostic_count == 1 && missing.error_count == 1);
        if (BUSTER_REQUIRE(arguments, missing.diagnostic_count == 1 && missing.diagnostics != 0))
        {
            BUSTER_TEST(arguments, missing.diagnostics[0].kind == C_DIAGNOSTIC_INCLUDE_NOT_FOUND);
        }
        BUSTER_TEST(arguments, missing.token_count == 2);
        if (BUSTER_REQUIRE(arguments, missing.tokens != 0 && missing.spelling_base != 0 && missing.token_count >= 1))
        {
            BUSTER_TEST(arguments, missing.tokens[0].kind == C_TOKEN_PREPROCESSING_NUMBER);
            BUSTER_STRING_TEST(arguments, c_token_spelling(missing.spelling_base, missing.tokens[0]), S8("71"));
        }

        CPreprocessResult malformed = c_preprocess(temporary.arena, S8("#include malformed_header.h\n"),
                                                   (CPreprocessOptions){.source_path = malformed_root_path});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.error_count == 1);
        if (BUSTER_REQUIRE(arguments, malformed.diagnostic_count == 1 && malformed.diagnostics != 0))
        {
            BUSTER_TEST(arguments, malformed.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_INCLUDE);
        }
        BUSTER_TEST(arguments, malformed.token_count == 1);

        CPreprocessResult depth = c_preprocess(temporary.arena,
                                               S8("#include \"buster-issue81-depth-0.h\"\n"),
                                               (CPreprocessOptions){
                                                   .source_path = depth_root_path,
                                                   .include_depth_limit = C_ONCE_TEST_DEPTH_LIMIT,
                                               });
        BUSTER_TEST(arguments, depth.diagnostic_count == 1 && depth.error_count == 1);
        if (BUSTER_REQUIRE(arguments, depth.diagnostic_count == 1 && depth.diagnostics != 0))
        {
            BUSTER_TEST(arguments, depth.diagnostics[0].kind == C_DIAGNOSTIC_INCLUDE_DEPTH);
        }
        BUSTER_TEST(arguments, depth.token_count == 1);
    }
    scratch_end(temporary);
    return result;
}

UnitTestResult c_once_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, c_once_test_import_growth);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_pragma_growth);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_lookup_work_bound);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_failures);
    return result;
}

#endif
