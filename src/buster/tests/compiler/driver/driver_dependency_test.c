// Included by driver_test.c: GCC-compatible dependency arguments, quoting,
// preprocessor identities and success-only depfile publication.
typedef struct CompilerDriverDependencyTestRule CompilerDriverDependencyTestRule;
struct CompilerDriverDependencyTestRule
{
    String8 target;
    String8 prerequisites[32];
    u32 prerequisite_count;
};

BUSTER_GLOBAL_LOCAL String8 compiler_driver_dependency_test_unescape(Arena* arena, String8 value)
{
    char8* output = arena_allocate(arena, char8, value.length);
    u64 count = 0;
    for (u64 index = 0; index < value.length;)
    {
        if (value.pointer[index] == '\\')
        {
            u64 end = index;
            while (end < value.length && value.pointer[end] == '\\')
            {
                end += 1;
            }
            if (end < value.length &&
                (value.pointer[end] == ' ' || value.pointer[end] == '\t' || value.pointer[end] == '#'))
            {
                for (u64 slash = 0; slash < (end - index) / 2; slash += 1)
                {
                    output[count++] = '\\';
                }
                output[count++] = value.pointer[end];
                index = end + 1;
            }
            else
            {
                while (index < end)
                {
                    output[count++] = '\\';
                    index += 1;
                }
            }
        }
        else if (value.pointer[index] == '$' && index + 1 < value.length && value.pointer[index + 1] == '$')
        {
            output[count++] = '$';
            index += 2;
        }
        else
        {
            output[count++] = value.pointer[index++];
        }
    }
    return (String8){.pointer = output, .length = count};
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_dependency_test_parse_rule(Arena* arena, String8 path,
                                                                     CompilerDriverDependencyTestRule* rule)
{
    bool valid = false;
    FileMapRead file = file_map_read(arena, path, (FileReadOptions){0});
    if (file.bytes.pointer)
    {
        char8* folded = arena_allocate(arena, char8, file.bytes.length + 1);
        u64 folded_count = 0;
        for (u64 index = 0; index < file.bytes.length;)
        {
            char8 byte = ((char8*)file.bytes.pointer)[index];
            if (byte == '\\' && index + 1 < file.bytes.length && ((char8*)file.bytes.pointer)[index + 1] == '\n')
            {
                folded[folded_count++] = ' ';
                index += 2;
                while (index < file.bytes.length &&
                       (((char8*)file.bytes.pointer)[index] == ' ' || ((char8*)file.bytes.pointer)[index] == '\t'))
                {
                    index += 1;
                }
            }
            else if (byte == '\n')
            {
                break;
            }
            else
            {
                folded[folded_count++] = byte;
                index += 1;
            }
        }
        u64 colon = 0;
        while (colon < folded_count && folded[colon] != ':')
        {
            colon += 1;
        }
        if (colon < folded_count)
        {
            *rule = (CompilerDriverDependencyTestRule){0};
            rule->target = compiler_driver_dependency_test_unescape(arena, (String8){.pointer = folded, .length = colon});
            u64 cursor = colon + 1;
            while (cursor < folded_count && rule->prerequisite_count < BUSTER_ARRAY_LENGTH(rule->prerequisites))
            {
                while (cursor < folded_count && (folded[cursor] == ' ' || folded[cursor] == '\t'))
                {
                    cursor += 1;
                }
                u64 start = cursor;
                while (cursor < folded_count)
                {
                    if (folded[cursor] == '\\')
                    {
                        u64 end = cursor;
                        while (end < folded_count && folded[end] == '\\')
                        {
                            end += 1;
                        }
                        if (end < folded_count && (folded[end] == ' ' || folded[end] == '\t' || folded[end] == '#'))
                        {
                            cursor = end + 1;
                        }
                        else
                        {
                            cursor = end;
                        }
                    }
                    else if (folded[cursor] == ' ' || folded[cursor] == '\t')
                    {
                        break;
                    }
                    else
                    {
                        cursor += 1;
                    }
                }
                if (cursor > start)
                {
                    rule->prerequisites[rule->prerequisite_count++] =
                        compiler_driver_dependency_test_unescape(arena, (String8){.pointer = folded + start, .length = cursor - start});
                }
            }
            valid = true;
        }
        file_map_unmap(file);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_dependency_test_compile(Arena* arena, String8 source, String8 object,
                                                                                  String8 include_directory, String8 system_directory,
                                                                                  String8 forced_include, String8 mode, bool phony,
                                                                                  String8* targets, u32 target_count)
{
    String8 command[24];
    u32 count = 0;
    command[count++] = S8("-c");
    command[count++] = mode;
    if (phony)
    {
        command[count++] = S8("-MP");
    }
    command[count++] = S8("-nostdinc");
    command[count++] = S8("-I");
    command[count++] = include_directory;
    command[count++] = S8("-isystem");
    command[count++] = system_directory;
    command[count++] = S8("-include");
    command[count++] = forced_include;
    for (u32 index = 0; index < target_count; index += 1)
    {
        command[count++] = targets[index];
    }
    command[count++] = S8("-o");
    command[count++] = object;
    command[count++] = source;
    CompilerDriverInvocation invocation =
        compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count});
    CompilerDriverResult result = compiler_driver_execute_invocation(arena, invocation);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_dependency_files(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 input = S8("tests/basic_c_multi_main.c");
    String8 accepted[][7] = {
        {S8("-c"), S8("-MD"), input},
        {S8("-c"), S8("-MMD"), input},
        {S8("-c"), S8("-MD"), S8("-MP"), input},
        {S8("-c"), S8("-MD"), S8("-MF"), S8("out.d"), input},
        {S8("-c"), S8("-MD"), S8("-MFout.d"), input},
        {S8("-c"), S8("-MD"), S8("-MT"), S8("raw"), S8("-MQquoted"), input},
        {S8("-c"), S8("-MD"), S8("-MTraw"), S8("-MQ"), S8("quoted"), input},
    };
    u32 accepted_lengths[] = {3, 3, 4, 5, 4, 6, 6};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(accepted); index += 1)
    {
        CompilerDriverInvocation parsed = compiler_driver_parse_arguments(
            arena, (SliceString8){.pointer = accepted[index], .length = accepted_lengths[index]});
        BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE && parsed.dependency_file);
    }
    String8 unsupported[] = {S8("-M"), S8("-MM"), S8("-MG")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(unsupported); index += 1)
    {
        String8 command[] = {S8("-c"), unsupported[index], input};
        CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_ARGUMENT);
        BUSTER_TEST(arguments, compiler_driver_test_string_contains(parsed.diagnostic, unsupported[index]));
    }
    String8 modifiers[][2] = {{S8("-MF"), S8("out.d")}, {S8("-MT"), S8("target")}, {S8("-MQ"), S8("target")}, {S8("-MP"), S8("")}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modifiers); index += 1)
    {
        u32 length = modifiers[index][1].length ? 3 : 2;
        String8 command[] = {modifiers[index][0], modifiers[index][1], input};
        CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = length});
        BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_ARGUMENT);
        BUSTER_TEST(arguments, compiler_driver_test_string_contains(parsed.diagnostic, modifiers[index][0]));
    }
    String8 invalid_modes[][4] = {
        {S8("-E"), S8("-MD"), input},
        {S8("-S"), S8("-MD"), input},
        {S8("-fsyntax-only"), S8("-MD"), input},
        {S8("-MD"), input},
        {S8("-c"), S8("-MD"), S8("tests/basic_asm_unit.s")},
    };
    u32 invalid_mode_lengths[] = {3, 3, 3, 2, 3};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_modes); index += 1)
    {
        CompilerDriverInvocation parsed = compiler_driver_parse_arguments(
            arena, (SliceString8){.pointer = invalid_modes[index], .length = invalid_mode_lengths[index]});
        BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_ARGUMENT);
        BUSTER_TEST(arguments, compiler_driver_test_string_contains(parsed.diagnostic, S8("-MD")));
    }
    String8 multiple_inputs[] = {S8("-c"), S8("-MD"), S8("-MF"), S8("out.d"), input, input};
    CompilerDriverInvocation multiple = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(multiple_inputs));
    BUSTER_TEST(arguments, multiple.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(multiple.diagnostic, S8("multiple input files")));
    String8 missing_dependency_path[] = {S8("-MF")};
    CompilerDriverInvocation missing_path =
        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing_dependency_path));
    BUSTER_TEST(arguments, missing_path.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(missing_path.diagnostic, S8("-MF")));
    String8 api_input[] = {input};
    CompilerDriverInvocation api_request = {
        .input_paths = api_input,
        .input_count = 1,
        .language = COMPILER_DRIVER_LANGUAGE_C,
        .action = COMPILER_DRIVER_ACTION_PREPROCESS,
        .dependency_file = true,
        .target = target_native,
    };
    CompilerDriverResult api_refused = compiler_driver_execute_invocation(arena, api_request);
    BUSTER_TEST(arguments, api_refused.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(api_refused.diagnostic, S8("-MD/-MMD")));

    String8 quote_inputs[] = {S8("a b"), S8("a$b"), S8("a#b"), S8("a\\ b"), S8("w\\x"), S8("line\nbreak")};
    String8 quote_expected[] = {S8("a\\ b"), S8("a$$b"), S8("a\\#b"), S8("a\\\\\\ b"), S8("w\\x"), {0}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(quote_inputs); index += 1)
    {
        CompilerDriverDependencyQuoteResult quoted = compiler_driver_dependency_quote(arena, quote_inputs[index]);
        if (index + 1 == BUSTER_ARRAY_LENGTH(quote_inputs))
        {
            BUSTER_TEST(arguments, !quoted.representable);
        }
        else
        {
            BUSTER_TEST(arguments, quoted.representable);
            BUSTER_STRING_TEST(arguments, quoted.text, quote_expected[index]);
        }
    }
    CompilerDriverDependencyQuoteResult newline_quote = compiler_driver_dependency_quote(arena, S8("line\nbreak"));
    CompilerDriverDependencyQuoteResult carriage_quote = compiler_driver_dependency_quote(arena, S8("line\rbreak"));
    BUSTER_TEST(arguments, !newline_quote.representable && !carriage_quote.representable);
    String8 object_paths[] = {S8("out/main.o"), S8("objs.dir/x"), S8("noext"), S8("a.b/c.o"), S8("a\\b.c.o")};
    String8 dep_paths[] = {S8("out/main.d"), S8("objs.dir/x.d"), S8("noext.d"), S8("a.b/c.d"), S8("a\\b.c.d")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(object_paths); index += 1)
    {
        BUSTER_STRING_TEST(arguments, compiler_driver_dependency_path(arena, object_paths[index]), dep_paths[index]);
    }

    String8 directory = buster_test_temporary_path(arena, S8("buster-dependency-files"), S8(""));
    String8 system_directory = string_format_z(arena, S8("{S8}/sys"), directory);
    String8 user_directory = string_format_z(arena, S8("{S8}/user"), directory);
    os_make_directory(directory);
    os_make_directory(system_directory);
    os_make_directory(user_directory);
    String8 source = string_format_z(arena, S8("{S8}/main.c"), directory);
    String8 forced = string_format_z(arena, S8("{S8}/forced.h"), directory);
    String8 middle = string_format_z(arena, S8("{S8}/middle.h"), directory);
    String8 leaf = string_format_z(arena, S8("{S8}/leaf.h"), directory);
    String8 once = string_format_z(arena, S8("{S8}/once.h"), directory);
    String8 special = string_format_z(arena, S8("{S8}/a b$#.h"), directory);
    String8 system_header = string_format_z(arena, S8("{S8}/sys/sys.h"), directory);
    String8 system_quoted = string_format_z(arena, S8("{S8}/sys/sysq.h"), directory);
    String8 system_nested = string_format_z(arena, S8("{S8}/sys/sys2.h"), directory);
    String8 system_user = string_format_z(arena, S8("{S8}/user/user_from_sys.h"), directory);
    String8 object = string_format_z(arena, S8("{S8}/main.o"), directory);
    String8 depfile = string_format_z(arena, S8("{S8}/main.d"), directory);
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
                                              "#include \"middle.h\"\n#include \"middle.h\"\n"
                                              "#include \"once.h\"\n#include \"once.h\"\n"
                                              "#include \"a b$#.h\"\n#include <stdbool.h>\n#include <sys.h>\n"
                                              "int dependency_answer(void) { bool value = true; "
                                              "return value + MIDDLE_VALUE + ONCE_VALUE + SPECIAL_VALUE + SYS_VALUE + FORCED_VALUE; }\n"))));
    BUSTER_TEST(arguments, file_write(forced, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define FORCED_VALUE 1\n"))));
    BUSTER_TEST(arguments, file_write(middle, BUSTER_SLICE_TO_BYTE_SLICE(S8("#ifndef MIDDLE_H\n#define MIDDLE_H\n#include \"leaf.h\"\n#define MIDDLE_VALUE LEAF_VALUE\n#endif\n"))));
    BUSTER_TEST(arguments, file_write(leaf, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define LEAF_VALUE 2\n"))));
    BUSTER_TEST(arguments, file_write(once, BUSTER_SLICE_TO_BYTE_SLICE(S8("#pragma once\n#define ONCE_VALUE 3\n"))));
    BUSTER_TEST(arguments, file_write(special, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define SPECIAL_VALUE 4\n"))));
    BUSTER_TEST(arguments, file_write(system_header, BUSTER_SLICE_TO_BYTE_SLICE(S8("#include \"sysq.h\"\n#include <sys2.h>\n#define SYS_VALUE (SYSQ_VALUE + SYS2_VALUE)\n"))));
    BUSTER_TEST(arguments, file_write(system_quoted, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define SYSQ_VALUE 5\n"))));
    BUSTER_TEST(arguments, file_write(system_nested, BUSTER_SLICE_TO_BYTE_SLICE(S8("#include \"user_from_sys.h\"\n#define SYS2_VALUE USER_FROM_SYS\n"))));
    BUSTER_TEST(arguments, file_write(system_user, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define USER_FROM_SYS 6\n"))));

    CompilerDriverResult compiled = compiler_driver_dependency_test_compile(arena, source, object, user_directory, system_directory,
                                                                             forced, S8("-MD"), false, 0, 0);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    CompilerDriverDependencyTestRule rule = {0};
    bool parsed = compiler_driver_dependency_test_parse_rule(arena, depfile, &rule);
    BUSTER_TEST(arguments, parsed);
    BUSTER_STRING_TEST(arguments, rule.target, object);
    String8 expected_all[] = {source, forced, middle, leaf, once, special, system_header, system_quoted, system_nested, system_user};
    BUSTER_TEST(arguments, rule.prerequisite_count == BUSTER_ARRAY_LENGTH(expected_all));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_all) && index < rule.prerequisite_count; index += 1)
    {
        BUSTER_STRING_TEST(arguments, rule.prerequisites[index], expected_all[index]);
    }
    for (u32 index = 0; index < rule.prerequisite_count; index += 1)
    {
        BUSTER_TEST(arguments, !string_equal(rule.prerequisites[index], S8("stdbool.h")));
        for (u32 other = index + 1; other < rule.prerequisite_count; other += 1)
        {
            BUSTER_TEST(arguments, !string_equal(rule.prerequisites[index], rule.prerequisites[other]));
        }
    }
    compiled = compiler_driver_dependency_test_compile(arena, source, object, user_directory, system_directory, forced,
                                                        S8("-MMD"), false, 0, 0);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    parsed = compiler_driver_dependency_test_parse_rule(arena, depfile, &rule);
    BUSTER_TEST(arguments, parsed && rule.prerequisite_count == 6);
    String8 expected_user[] = {source, forced, middle, leaf, once, special};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_user) && index < rule.prerequisite_count; index += 1)
    {
        BUSTER_STRING_TEST(arguments, rule.prerequisites[index], expected_user[index]);
    }
    compiled = compiler_driver_dependency_test_compile(arena, source, object, user_directory, system_directory, forced,
                                                        S8("-MD"), true, 0, 0);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    String8 headers[] = {forced, middle, leaf, once, special, system_header, system_quoted, system_nested, system_user};
    FileMapRead phony_file = file_map_read(arena, depfile, (FileReadOptions){0});
    String8 phony_text = BYTE_SLICE_TO_STRING(8, phony_file.bytes);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(headers); index += 1)
    {
        CompilerDriverDependencyQuoteResult quoted = compiler_driver_dependency_quote(arena, headers[index]);
        String8 rule_text = string_format(arena, S8("\n{S8}:\n"), quoted.text);
        BUSTER_TEST(arguments, quoted.representable && compiler_driver_test_string_contains(phony_text, rule_text));
    }
    CompilerDriverDependencyQuoteResult quoted_source = compiler_driver_dependency_quote(arena, source);
    String8 main_phony = string_format(arena, S8("\n{S8}:\n"), quoted_source.text);
    BUSTER_TEST(arguments, !compiler_driver_test_string_contains(phony_text, main_phony));
    file_map_unmap(phony_file);
    String8 target_options[] = {S8("-MT"), S8("a$b"), S8("-MQ"), S8("c$d e")};
    compiled = compiler_driver_dependency_test_compile(arena, source, object, user_directory, system_directory, forced,
                                                        S8("-MD"), false, target_options, BUSTER_ARRAY_LENGTH(target_options));
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    FileMapRead target_file = file_map_read(arena, depfile, (FileReadOptions){0});
    String8 target_text = BYTE_SLICE_TO_STRING(8, target_file.bytes);
    BUSTER_TEST(arguments, string_starts_with_sequence(target_text, S8("a$b c$$d\\ e:")));
    file_map_unmap(target_file);

    String8 invalid_source = string_format_z(arena, S8("{S8}/invalid.c"), directory);
    String8 failure_object = string_format_z(arena, S8("{S8}/failure.o"), directory);
    String8 failure_depfile = string_format_z(arena, S8("{S8}/failure.d"), directory);
    String8 absent_depfile = string_format_z(arena, S8("{S8}/absent.d"), directory);
    String8 sentinel_object = S8("object sentinel");
    String8 sentinel_depfile = S8("depfile sentinel");
    BUSTER_TEST(arguments, file_write(invalid_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int invalid( {\n"))));
    BUSTER_TEST(arguments, file_write(failure_object, BUSTER_SLICE_TO_BYTE_SLICE(sentinel_object)));
    BUSTER_TEST(arguments, file_write(failure_depfile, BUSTER_SLICE_TO_BYTE_SLICE(sentinel_depfile)));
    String8 failure_command[] = {S8("-c"), S8("-MD"), S8("-MF"), failure_depfile, S8("-o"), failure_object, invalid_source};
    CompilerDriverResult failed = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(failure_command)));
    BUSTER_TEST(arguments, failed.error != COMPILER_DRIVER_ERROR_NONE && failed.diagnostic.length);
    FileMapRead preserved_object = file_map_read(arena, failure_object, (FileReadOptions){0});
    FileMapRead preserved_depfile = file_map_read(arena, failure_depfile, (FileReadOptions){0});
    BUSTER_TEST(arguments, preserved_object.bytes.pointer &&
                               string_equal(BYTE_SLICE_TO_STRING(8, preserved_object.bytes), sentinel_object));
    BUSTER_TEST(arguments, preserved_depfile.bytes.pointer &&
                               string_equal(BYTE_SLICE_TO_STRING(8, preserved_depfile.bytes), sentinel_depfile));
    file_map_unmap(preserved_object);
    file_map_unmap(preserved_depfile);
    failure_command[3] = absent_depfile;
    failed = compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(failure_command)));
    BUSTER_TEST(arguments, failed.error != COMPILER_DRIVER_ERROR_NONE);
    FileMapRead absent = file_map_read(arena, absent_depfile, (FileReadOptions){0});
    BUSTER_TEST(arguments, !absent.bytes.pointer);
    file_map_unmap(absent);

    String8 line_break_source = string_format_z(arena, S8("{S8}/line\nbreak.c"), directory);
    String8 line_break_object = string_format_z(arena, S8("{S8}/line-break.o"), directory);
    String8 line_break_depfile = string_format_z(arena, S8("{S8}/line-break.d"), directory);
    BUSTER_TEST(arguments, file_write(line_break_source, BUSTER_SLICE_TO_BYTE_SLICE(S8("int line_break(void) { return 1; }\n"))));
    String8 line_break_command[] = {S8("-c"), S8("-MD"), S8("-o"), line_break_object, line_break_source};
    CompilerDriverResult line_break_result =
        compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(line_break_command)));
    String8 line_break_diagnostic =
        string_format(arena, S8("cannot represent {S8} in a dependency file"), line_break_source);
    BUSTER_TEST(arguments, line_break_result.error == COMPILER_DRIVER_ERROR_FILE_WRITE);
    BUSTER_STRING_TEST(arguments, line_break_result.diagnostic, line_break_diagnostic);
    FileMapRead line_break_dep = file_map_read(arena, line_break_depfile, (FileReadOptions){0});
    BUSTER_TEST(arguments, !line_break_dep.bytes.pointer);
    file_map_unmap(line_break_dep);

    scratch_end(temporary);
    return result;
}
