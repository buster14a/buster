// Included by driver_test.c: GCC-compatible dependency arguments, quoting,
// preprocessor identities and success-only depfile publication.
#include <buster/lib/os.h>

typedef struct CompilerDriverDependencyTestRule CompilerDriverDependencyTestRule;
struct CompilerDriverDependencyTestRule
{
    String8 target;
    String8 prerequisites[32];
    u32 prerequisite_count;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_dependency_test_path_equal(String8 left, String8 right)
{
    u64 left_start = left.length >= 2 && left.pointer[0] == '.' && (left.pointer[1] == '/' || left.pointer[1] == '\\') ? 2 : 0;
    u64 right_start = right.length >= 2 && right.pointer[0] == '.' && (right.pointer[1] == '/' || right.pointer[1] == '\\') ? 2 : 0;
    u64 left_length = left.length - left_start;
    u64 right_length = right.length - right_start;
    bool equal = left_length == right_length;
    for (u64 index = 0; index < left_length && index < right_length; index += 1)
    {
        char8 left_byte = left.pointer[left_start + index] == '\\' ? '/' : left.pointer[left_start + index];
        char8 right_byte = right.pointer[right_start + index] == '\\' ? '/' : right.pointer[right_start + index];
        equal &= left_byte == right_byte;
    }
    return equal;
}

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool compiler_driver_dependency_test_change_directory(String8 path)
{
#if BUSTER_WINDOWS
    TemporalArena scratch = scratch_begin(0, 0);
    String16 wide_path = string16_from_string8(scratch.arena, path, true);
    bool result = SetCurrentDirectoryW(wide_path.pointer) != 0;
    scratch_end(scratch);
#else
    BUSTER_RT_CHECK(path.pointer != 0 && path.pointer[path.length] == 0);
    bool result = chdir((const char*)path.pointer) == 0;
#endif
    return result;
}
#endif

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
        while (colon < folded_count)
        {
            bool rule_separator = folded[colon] == ':' &&
                                  (colon + 1 == folded_count || folded[colon + 1] == ' ' ||
                                   folded[colon + 1] == '\t' || folded[colon + 1] == '\n');
            if (rule_separator)
            {
                break;
            }
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

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_dependency_target_scaling(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    // Measure the real parser's retained allocations, including response-file
    // expansion. A 16x population must not retain 256x target prefixes.
    for (u32 response = 0; response < 2; response += 1)
    {
        for (u32 mode = 0; mode < 3; mode += 1)
        {
            u64 small_bytes = 0;
            for (u32 scale = 0; scale < 2; scale += 1)
            {
                TemporalArena temporary = arena_begin_temporal(arena);
                u32 target_count = scale ? 2048 : 128;
                String8* command = arena_allocate(arena, String8, 3 + 2 * target_count);
                String8* expected_parts = arena_allocate(arena, String8, 2 * target_count - 1);
                String8* response_parts = arena_allocate(arena, String8, target_count + 1);
                command[0] = S8("-c");
                command[1] = S8("-MMD");
                command[2] = S8("tests/basic_c_multi_main.c");
                response_parts[0] = S8("-c -MMD tests/basic_c_multi_main.c\n");
                u32 expected_count = 0;
                for (u32 index = 0; index < target_count; index += 1)
                {
                    bool quoted = mode == 1 || (mode == 2 && (index & 1));
                    command[3 + index * 2] = quoted ? S8("-MQ") : S8("-MT");
                    command[4 + index * 2] = S8("target $name#.o");
                    response_parts[index + 1] = quoted ? S8("-MQ 'target $name#.o'\n") : S8("-MT 'target $name#.o'\n");
                    if (index)
                    {
                        expected_parts[expected_count++] = S8(" ");
                    }
                    expected_parts[expected_count++] = quoted ? S8("target\\ $$name\\#.o") : S8("target $name#.o");
                }
                String8 expected = string_join_arena(arena, (SliceString8){.pointer = expected_parts, .length = expected_count}, false);
                SliceString8 input = {.pointer = command, .length = 3 + 2 * target_count};
                String8 response_argument = {0};
                if (response)
                {
                    String8 path = buster_test_temporary_path(arena, S8("buster-dependency-targets"), S8(".rsp"));
                    String8 text = string_join_arena(arena, (SliceString8){.pointer = response_parts, .length = target_count + 1}, false);
                    BUSTER_TEST(arguments, file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text)));
                    response_argument = string_format(arena, S8("@{S8}"), path);
                    input = (SliceString8){.pointer = &response_argument, .length = 1};
                }
                u64 start = arena->position;
                CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, input);
                u64 retained = arena->position - start;
                BUSTER_TEST_RAW(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE, parsed.diagnostic);
                BUSTER_STRING_TEST(arguments, parsed.dependency_targets, expected);
                // Includes all argument-indexed arrays, quoting and expansion;
                // the old prefix join exceeds this bound at 2,048 targets.
                BUSTER_TEST_RAW(arguments, retained <= 4096 * (u64)target_count + 65536,
                                string_format(arena, S8("response={u32} mode={u32} targets={u32} retained={u64}"),
                                              response, mode, target_count, retained));
                if (!scale)
                {
                    small_bytes = retained;
                }
                else
                {
                    BUSTER_TEST_RAW(arguments, retained <= 16 * small_bytes + 65536,
                                    string_format(arena, S8("response={u32} mode={u32} small={u64} large={u64}"),
                                                  response, mode, small_bytes, retained));
                }
                scratch_end(temporary);
            }
        }
    }
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
    String8 target_cases[][11] = {
        {S8("-c"), S8("-MD"), input, S8("-MT"), S8("")},
        {S8("-c"), S8("-MD"), input, S8("-MQ"), S8("")},
        {S8("-c"), S8("-MD"), input, S8("-MT"), S8(""), S8("-MQ"), S8(""), S8("-MTraw")},
        {S8("-c"), S8("-MD"), input, S8("-MTraw$#"), S8("-MQ"), S8("q\\ \t$#"), S8("-MTraw$#"), S8("-MQ"), S8("")},
        {S8("-c"), S8("-MD"), input, S8("-MQ"), S8("line\nbreak")},
    };
    u32 target_case_lengths[] = {5, 5, 8, 9, 5};
    String8 target_case_expected[] = {S8(""), S8(""), S8("  raw"), S8("raw$# q\\\\\\ \\\t$$\\# raw$# "), S8("line\nbreak")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(target_cases); index += 1)
    {
        CompilerDriverInvocation parsed = compiler_driver_parse_arguments(
            arena, (SliceString8){.pointer = target_cases[index], .length = target_case_lengths[index]});
        BUSTER_TEST_RAW(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE, parsed.diagnostic);
        BUSTER_TEST(arguments, parsed.dependency_targets.pointer != 0);
        BUSTER_STRING_TEST(arguments, parsed.dependency_targets, target_case_expected[index]);
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


    String8 pass_modes[] = {S8("-Wp,-MD,out.d"), S8("-Wp,-MMD,out.d")};
    String8 pass_actions[] = {S8("-c"), S8("-S")};
    for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(pass_modes); mode += 1)
    {
        for (u32 action = 0; action < BUSTER_ARRAY_LENGTH(pass_actions); action += 1)
        {
            String8 command[] = {pass_actions[action], pass_modes[mode], S8("-o"), S8("out"), input};
            CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE && parsed.dependency_passthrough);
            BUSTER_TEST(arguments, parsed.dependency_exclude_system == (mode == 1));
            BUSTER_STRING_TEST(arguments, parsed.dependency_path, S8("out.d"));
        }
    }
    String8 malformed_pass[] = {
        S8("-Wp,-MD"), S8("-Wp,-MMD"), S8("-Wp,-MD,"), S8("-Wp,-MMD,"),
        S8("-Wp,-MD,out.d,-MP"), S8("-Wp,-MMD,out.d,"), S8("-Wp,-MMD,out.d,-DFOO"),
        S8("-Wp,-MD,-"), S8("-Wp,-MMD,line\nbreak.d"), S8("-Wp,-MMD,line\rbreak.d")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(malformed_pass); index += 1)
    {
        String8 command[] = {S8("-c"), malformed_pass[index], input};
        CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_ARGUMENT);
        BUSTER_TEST(arguments, compiler_driver_test_string_contains(parsed.diagnostic, malformed_pass[index]));
    }
    String8 direct_options[] = {S8("-MD"), S8("-MMD"), S8("-MFother.d"), S8("-MTtarget"), S8("-MQtarget"), S8("-MP")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(direct_options); index += 1)
    {
        for (u32 order = 0; order < 2; order += 1)
        {
            String8 command[] = {S8("-c"), order ? direct_options[index] : pass_modes[0],
                                 order ? pass_modes[0] : direct_options[index], input};
            BUSTER_TEST(arguments, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)).error == COMPILER_DRIVER_ERROR_ARGUMENT);
        }
    }
    String8 invalid_pass[][6] = {
        {S8("-E"), pass_modes[0], input},
        {S8("-fsyntax-only"), pass_modes[0], input},
        {pass_modes[0], input},
        {S8("-S"), pass_modes[0], input},
        {S8("-S"), pass_modes[0], S8("-o"), S8("-"), input},
        {S8("-c"), pass_modes[0], pass_modes[1], input},
        {S8("-c"), pass_modes[0], input, input},
        {S8("-c"), pass_modes[0], S8("tests/basic_asm_unit.s")},
        {S8("-c"), pass_modes[0], S8("-emit-llvm"), input},
        {S8("-c"), pass_modes[0], S8("--target=wasm32-unknown-unknown"), input}};
    u32 invalid_pass_lengths[] = {3, 3, 2, 3, 5, 4, 4, 3, 4, 4};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_pass); index += 1)
    {
        BUSTER_TEST(arguments, compiler_driver_parse_arguments(
                                  arena, (SliceString8){.pointer = invalid_pass[index], .length = invalid_pass_lengths[index]}).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }

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
    BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(rule.target, object));
    String8 expected_all[] = {source, forced, middle, leaf, once, special, system_header, system_quoted, system_nested, system_user};
    BUSTER_TEST(arguments, rule.prerequisite_count == BUSTER_ARRAY_LENGTH(expected_all));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_all) && index < rule.prerequisite_count; index += 1)
    {
        String8 actual = rule.prerequisites[index];
        String8 expected = expected_all[index];
        BUSTER_TEST_RAW(arguments, compiler_driver_dependency_test_path_equal(actual, expected),
                        string_format(arena, S8("actual ({u64})='{S8}', expected ({u64})='{S8}'"), actual.length, actual,
                                      expected.length, expected));
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
        String8 actual = rule.prerequisites[index];
        String8 expected = expected_user[index];
        BUSTER_TEST_RAW(arguments, compiler_driver_dependency_test_path_equal(actual, expected),
                        string_format(arena, S8("actual ({u64})='{S8}', expected ({u64})='{S8}'"), actual.length, actual,
                                      expected.length, expected));
    }
#if !BUSTER_ANDROID && !BUSTER_IOS
    String8 multi_directory = string_format_z(arena, S8("{S8}/multi"), directory);
    String8 multi_user_directory = string_format_z(arena, S8("{S8}/user"), multi_directory);
    String8 multi_system_directory = string_format_z(arena, S8("{S8}/system"), multi_directory);
    os_make_directory(multi_directory);
    os_make_directory(multi_user_directory);
    os_make_directory(multi_system_directory);
    String8 multi_source_user = string_format_z(arena, S8("{S8}/aaaa.c"), multi_directory);
    String8 multi_source_system = string_format_z(arena, S8("{S8}/bbbb.c"), multi_directory);
    String8 multi_user_header = string_format_z(arena, S8("{S8}/user.h"), multi_user_directory);
    String8 multi_system_header = string_format_z(arena, S8("{S8}/syst.h"), multi_system_directory);
    BUSTER_TEST(arguments, file_write(multi_source_user, BUSTER_SLICE_TO_BYTE_SLICE(S8("#include <user.h>\nint aaaa(void) { return USER_VALUE; }\n"))));
    BUSTER_TEST(arguments, file_write(multi_source_system, BUSTER_SLICE_TO_BYTE_SLICE(S8("#include <syst.h>\nint bbbb(void) { return SYSTEM_VALUE; }\n"))));
    BUSTER_TEST(arguments, file_write(multi_user_header, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define USER_VALUE 1\n"))));
    BUSTER_TEST(arguments, file_write(multi_system_header, BUSTER_SLICE_TO_BYTE_SLICE(S8("#define SYSTEM_VALUE 2\n"))));
    String8 original_directory = os_path_absolute(arena, S8("."), true);
    String8 multi_directory_absolute = os_path_absolute(arena, multi_directory, true);
    bool changed_directory = original_directory.length && multi_directory_absolute.length &&
                             compiler_driver_dependency_test_change_directory(multi_directory_absolute);
    BUSTER_TEST(arguments, changed_directory);
    if (changed_directory)
    {
        String8 multi_command[] = {
            S8("-c"), S8("-MMD"), S8("-I"), S8("user"), S8("-isystem"), S8("system"), S8("aaaa.c"), S8("bbbb.c")};
        CompilerDriverResult multi_compiled = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(multi_command)));
        BUSTER_TEST_RAW(arguments, multi_compiled.error == COMPILER_DRIVER_ERROR_NONE, multi_compiled.diagnostic);
        FileMapRead user_object = file_map_read(arena, S8("aaaa.o"), (FileReadOptions){0});
        FileMapRead system_object = file_map_read(arena, S8("bbbb.o"), (FileReadOptions){0});
        BUSTER_TEST(arguments, user_object.bytes.pointer && system_object.bytes.pointer);
        file_map_unmap(user_object);
        file_map_unmap(system_object);
        CompilerDriverDependencyTestRule user_rule = {0};
        CompilerDriverDependencyTestRule system_rule = {0};
        bool user_parsed = compiler_driver_dependency_test_parse_rule(arena, S8("aaaa.d"), &user_rule);
        bool system_parsed = compiler_driver_dependency_test_parse_rule(arena, S8("bbbb.d"), &system_rule);
        BUSTER_TEST(arguments, user_parsed && system_parsed);
        BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(user_rule.target, S8("aaaa.o")));
        BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(system_rule.target, S8("bbbb.o")));
        BUSTER_TEST(arguments, user_rule.prerequisite_count == 2);
        BUSTER_TEST(arguments, system_rule.prerequisite_count == 1);
        if (user_rule.prerequisite_count > 0)
        {
            BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(user_rule.prerequisites[0], S8("aaaa.c")));
        }
        if (user_rule.prerequisite_count > 1)
        {
            BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(user_rule.prerequisites[1], S8("user/user.h")));
        }
        if (system_rule.prerequisite_count > 0)
        {
            BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(system_rule.prerequisites[0], S8("bbbb.c")));
        }
        // Missing leaves must still have one destination identity. Exercise
        // relative, absolute, dot and parent-segment aliases before creation.
        for (u32 action = 0; action < 2; action += 1)
        {
            for (u32 alias = 0; alias < 4; alias += 1)
            {
                String8 output_name = string_format(arena, S8("fresh-{u32}-{u32}.o"), action, alias);
                String8 absolute_output = string_format(arena, S8("{S8}/{S8}"), multi_directory_absolute, output_name);
                String8 aliases[] = {
                    string_format(arena, S8("./{S8}"), output_name),
                    string_format(arena, S8("user/../{S8}"), output_name),
                    absolute_output, output_name};
                String8 output_path = alias == 3 ? absolute_output : output_name;
                String8 refusal[] = {action ? S8("-S") : S8("-c"),
                                     string_format(arena, S8("-Wp,-MMD,{S8}"), aliases[alias]),
                                     S8("-o"), output_path, S8("aaaa.c")};
                CompilerDriverResult refused = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(refusal)));
                BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_ARGUMENT);
                BUSTER_TEST(arguments, compiler_driver_test_string_contains(refused.diagnostic, S8("aliases")));
                ByteSlice absent_output = file_read(arena, output_path, (FileReadOptions){0});
                BUSTER_TEST(arguments, !absent_output.pointer);
            }
        }
        BUSTER_TEST(arguments, compiler_driver_dependency_test_change_directory(original_directory));
    }
#endif

    // The -Wp spellings share the resolved include identities, with named
    // native assembly published before its depfile just like an object.
    String8 pass_depfile = string_format_z(arena, S8("{S8}/pass through$#.d"), directory);
    String8 pass_output = string_format_z(arena, S8("{S8}/pass through$#.s"), directory);
    for (u32 mode = 0; mode < 2; mode += 1)
    {
        for (u32 action = 0; action < 2; action += 1)
        {
            String8 option = string_format_z(arena, mode ? S8("-Wp,-MMD,{S8}") : S8("-Wp,-MD,{S8}"), pass_depfile);
            String8 command[] = {action ? S8("-S") : S8("-c"), option, S8("-nostdinc"),
                                 S8("-I"), user_directory, S8("-isystem"), system_directory,
                                 S8("-include"), forced, S8("-o"), pass_output, source};
            CompilerDriverResult pass_compiled = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST_RAW(arguments, pass_compiled.error == COMPILER_DRIVER_ERROR_NONE, pass_compiled.diagnostic);
            ByteSlice output_bytes = file_read(arena, pass_output, (FileReadOptions){0});
            BUSTER_TEST(arguments, output_bytes.pointer && output_bytes.length);
            if (action)
            {
                BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, output_bytes), pass_compiled.output);
            }
            CompilerDriverDependencyTestRule pass_rule = {0};
            bool pass_parsed = compiler_driver_dependency_test_parse_rule(arena, pass_depfile, &pass_rule);
            u32 expected_count = mode ? BUSTER_ARRAY_LENGTH(expected_user) : BUSTER_ARRAY_LENGTH(expected_all);
            String8* expected_paths = mode ? expected_user : expected_all;
            BUSTER_TEST(arguments, pass_parsed && pass_rule.prerequisite_count == expected_count);
            BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(pass_rule.target, pass_output));
            for (u32 index = 0; index < expected_count && index < pass_rule.prerequisite_count; index += 1)
            {
                BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(pass_rule.prerequisites[index], expected_paths[index]));
            }
            String8 sentinel = S8("depfile must survive failed artifact publication");
            BUSTER_TEST(arguments, file_write(pass_depfile, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
            command[10] = directory;
            CompilerDriverResult failed_output = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST(arguments, failed_output.error == COMPILER_DRIVER_ERROR_FILE_WRITE);
            ByteSlice preserved = file_read(arena, pass_depfile, (FileReadOptions){0});
            BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, preserved), sentinel);
            command[10] = pass_output;
            command[1] = string_format_z(arena, mode ? S8("-Wp,-MMD,{S8}") : S8("-Wp,-MD,{S8}"), directory);
            CompilerDriverResult failed_depfile = compiler_driver_execute_invocation(
                arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            BUSTER_TEST(arguments, failed_depfile.error == COMPILER_DRIVER_ERROR_FILE_WRITE);
            BUSTER_TEST(arguments, compiler_driver_test_string_contains(failed_depfile.diagnostic, directory));
            // API-created requests use the same refusal authority.
            CompilerDriverInvocation api_pass = compiler_driver_parse_arguments(
                arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            api_pass.dependency_path = (String8){0};
            BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, api_pass).error == COMPILER_DRIVER_ERROR_ARGUMENT);
            api_pass.dependency_path = pass_output;
            BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, api_pass).error == COMPILER_DRIVER_ERROR_ARGUMENT);
            api_pass.dependency_path = source;
            BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, api_pass).error == COMPILER_DRIVER_ERROR_ARGUMENT);
        }
    }

    compiled = compiler_driver_dependency_test_compile(arena, source, object, user_directory, system_directory, forced,
                                                        S8("-MD"), true, 0, 0);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
    FileMapRead phony_file = file_map_read(arena, depfile, (FileReadOptions){0});
    String8 phony_text = BYTE_SLICE_TO_STRING(8, phony_file.bytes);
    CompilerDriverDependencyTestRule phony_rule = {0};
    bool phony_parsed = compiler_driver_dependency_test_parse_rule(arena, depfile, &phony_rule);
    BUSTER_TEST(arguments, phony_parsed && phony_rule.prerequisite_count > 0);
    for (u32 index = 1; index < phony_rule.prerequisite_count; index += 1)
    {
        CompilerDriverDependencyQuoteResult quoted = compiler_driver_dependency_quote(arena, phony_rule.prerequisites[index]);
        String8 rule_text = string_format(arena, S8("\n{S8}:\n"), quoted.text);
        BUSTER_TEST(arguments, quoted.representable && compiler_driver_test_string_contains(phony_text, rule_text));
    }
    if (phony_rule.prerequisite_count > 0)
    {
        CompilerDriverDependencyQuoteResult quoted_source = compiler_driver_dependency_quote(arena, phony_rule.prerequisites[0]);
        String8 main_phony = string_format(arena, S8("\n{S8}:\n"), quoted_source.text);
        BUSTER_TEST(arguments, !compiler_driver_test_string_contains(phony_text, main_phony));
    }
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

    for (u32 action = 0; action < 2; action += 1)
    {
        String8 pass_failure[] = {action ? S8("-S") : S8("-c"),
                                  string_format_z(arena, S8("-Wp,-MMD,{S8}"), failure_depfile),
                                  S8("-o"), failure_object, invalid_source};
        CompilerDriverResult pass_failed = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pass_failure)));
        BUSTER_TEST(arguments, pass_failed.error != COMPILER_DRIVER_ERROR_NONE);
        ByteSlice preserved_artifact = file_read(arena, failure_object, (FileReadOptions){0});
        ByteSlice preserved_dependency = file_read(arena, failure_depfile, (FileReadOptions){0});
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, preserved_artifact), sentinel_object);
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, preserved_dependency), sentinel_depfile);
    }

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

#if !BUSTER_WINDOWS
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
#endif

    scratch_end(temporary);
    return result;
}

/* #3676: late system directories must not shadow user or default headers.
   Literal header values observe actual lookup; MD/MMD observes classification. */
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_idirafter(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = temporary.arena;
    String8 missing[] = {S8("-E"), S8("source.c"), S8("-idirafter")};
    CompilerDriverInvocation refused = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing));
    BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(refused.diagnostic, S8("missing argument after -idirafter")));

    String8 gpu[] = {S8("-c"), S8("-x"), S8("cuda"), S8("--target=nvptx64-nvidia-cuda"),
                     S8("-idirafter"), S8("late"), S8("source.cu")};
    CompilerDriverInvocation gpu_refused = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(gpu));
    BUSTER_TEST(arguments, gpu_refused.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(gpu_refused.diagnostic, S8("-idirafter")));

    String8 root = buster_test_temporary_path(arena, S8("buster-idirafter"), S8(""));
    String8 user = string_format_z(arena, S8("{S8}/user"), root);
    String8 system = string_format_z(arena, S8("{S8}/system"), root);
    String8 sdk = string_format_z(arena, S8("{S8}/sdk"), root);
    String8 sdk_usr = string_format_z(arena, S8("{S8}/usr"), sdk);
    String8 defaults = string_format_z(arena, S8("{S8}/include"), sdk_usr);
    String8 after[] = {string_format_z(arena, S8("{S8}/after-first"), root),
                       string_format_z(arena, S8("{S8}/after-second"), root)};
    String8 directories[] = {root, user, system, sdk, sdk_usr, defaults, after[0], after[1]};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(directories); index += 1)
        os_make_directory(directories[index]);
    String8 header_directories[] = {user, system, defaults, after[0], after[1]};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(header_directories); index += 1)
    {
        String8 path = string_format_z(arena, S8("{S8}/buster_idirafter_order.h"), header_directories[index]);
        String8 text = string_format(arena, S8("#undef HEADER_VALUE\n#define HEADER_VALUE {u32}\n"), index + 1);
        BUSTER_TEST(arguments, file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    }
    String8 source = string_format_z(arena, S8("{S8}/main.c"), root);
    String8 source_text = S8("#include \"buster_idirafter_order.h\"\nint quoted = HEADER_VALUE;\n"
                            "#include <buster_idirafter_order.h>\nint angled = HEADER_VALUE;\n");
    BUSTER_TEST(arguments, file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(source_text)));
    // Put after options first: option placement must not alter class ordering.
    // Cases remove each earlier search class; the final case reverses late roots.
    u32 expected[] = {1, 2, 3, 4, 4, 5};
    for (u32 variant = 0; variant < BUSTER_ARRAY_LENGTH(expected); variant += 1)
    {
        TemporalArena attempt = arena_begin_temporal(arena);
        String8 command[18];
        u32 count = 0;
        command[count++] = S8("-E");
        command[count++] = S8("--target=x86_64-unknown-linux-gnu");
        command[count++] = S8("-idirafter");
        command[count++] = after[variant == 5];
        command[count++] = string_format(arena, S8("-idirafter{S8}"), after[variant != 5]);
        command[count++] = S8("--sysroot");
        command[count++] = sdk;
        if (variant >= 3)
            command[count++] = variant == 4 ? S8("-nostdlibinc") : S8("-nostdinc");
        if (variant <= 1)
        {
            command[count++] = S8("-isystem");
            command[count++] = system;
        }
        if (variant == 0)
        {
            command[count++] = S8("-I");
            command[count++] = user;
        }
        command[count++] = source;
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count});
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.system_include_path_count >= 2);
        if (invocation.system_include_path_count >= 2)
        {
            u32 end = invocation.system_include_path_count;
            BUSTER_STRING_TEST(arguments, invocation.system_include_paths[end - 2], after[variant == 5]);
            BUSTER_STRING_TEST(arguments, invocation.system_include_paths[end - 1], after[variant != 5]);
        }
        CompilerDriverResult preprocessed = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST_RAW(arguments, preprocessed.error == COMPILER_DRIVER_ERROR_NONE, preprocessed.diagnostic);
        BUSTER_TEST(arguments, compiler_driver_test_string_contains(
                                   preprocessed.output, string_format(arena, S8("int quoted = {u32};"), expected[variant])));
        BUSTER_TEST(arguments, compiler_driver_test_string_contains(
                                   preprocessed.output, string_format(arena, S8("int angled = {u32};"), expected[variant])));
        scratch_end(attempt);
    }

    // Quoted lookup still starts in the including file's own directory.
    String8 local_header = string_format_z(arena, S8("{S8}/buster_idirafter_order.h"), root);
    BUSTER_TEST(arguments, file_write(local_header, BUSTER_SLICE_TO_BYTE_SLICE(S8("#undef HEADER_VALUE\n#define HEADER_VALUE 7\n"))));
    String8 local_command[] = {S8("-E"), S8("-nostdinc"), S8("-idirafter"), after[0], source};
    CompilerDriverResult local = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(local_command)));
    BUSTER_TEST_RAW(arguments, local.error == COMPILER_DRIVER_ERROR_NONE, local.diagnostic);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(local.output, S8("int quoted = 7;")));
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(local.output, S8("int angled = 4;")));

    // include_next traverses explicit system -> default system -> after system.
    String8 chain_directories[] = {system, defaults, after[0]};
    String8 chain_text[] = {S8("#define CHAIN_SYSTEM 1\n#include_next <buster_idirafter_chain.h>\n"),
                           S8("#define CHAIN_DEFAULT 2\n#include_next <buster_idirafter_chain.h>\n"),
                           S8("#define CHAIN_AFTER 3\n")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(chain_directories); index += 1)
    {
        String8 path = string_format_z(arena, S8("{S8}/buster_idirafter_chain.h"), chain_directories[index]);
        BUSTER_TEST(arguments, file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(chain_text[index])));
    }
    String8 chain_source = string_format_z(arena, S8("{S8}/chain.c"), root);
    BUSTER_TEST(arguments, file_write(chain_source, BUSTER_SLICE_TO_BYTE_SLICE(
                                      S8("#include <buster_idirafter_chain.h>\nint chain = CHAIN_SYSTEM + CHAIN_DEFAULT + CHAIN_AFTER;\n"))));
    String8 chain_command[] = {S8("-E"), S8("--target=x86_64-unknown-linux-gnu"), S8("--sysroot"), sdk,
                               S8("-idirafter"), after[0], S8("-isystem"), system, chain_source};
    CompilerDriverResult chain = compiler_driver_execute_invocation(
        arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(chain_command)));
    BUSTER_TEST_RAW(arguments, chain.error == COMPILER_DRIVER_ERROR_NONE, chain.diagnostic);
    BUSTER_TEST(arguments, compiler_driver_test_string_contains(chain.output, S8("int chain = 1 + 2 + 3;")));

    // A header found through -idirafter is system-owned: MD retains it and
    // MMD omits it. Compile only the angle include, independent of local_header.
    String8 dependency_source = string_format_z(arena, S8("{S8}/dependency.c"), root);
    String8 object = string_format_z(arena, S8("{S8}/dependency.o"), root);
    String8 depfile = string_format_z(arena, S8("{S8}/dependency.d"), root);
    String8 late_header = string_format_z(arena, S8("{S8}/buster_idirafter_order.h"), after[0]);
    BUSTER_TEST(arguments, file_write(dependency_source, BUSTER_SLICE_TO_BYTE_SLICE(
                                      S8("#include <buster_idirafter_order.h>\nint dependency = HEADER_VALUE;\n"))));
    for (u32 mode = 0; mode < 2; mode += 1)
    {
        TemporalArena attempt = arena_begin_temporal(arena);
        String8 command[] = {S8("-c"), S8("-nostdinc"), string_format(arena, S8("-idirafter{S8}"), after[0]),
                             mode ? S8("-MMD") : S8("-MD"), S8("-MF"), depfile, S8("-o"), object, dependency_source};
        CompilerDriverResult compiled = compiler_driver_execute_invocation(
            arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
        BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object, compiled.diagnostic);
        CompilerDriverDependencyTestRule rule = {0};
        bool parsed = compiler_driver_dependency_test_parse_rule(arena, depfile, &rule);
        BUSTER_TEST(arguments, parsed && rule.prerequisite_count == (mode ? 1u : 2u));
        if (rule.prerequisite_count)
            BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(rule.prerequisites[0], dependency_source));
        if (!mode && rule.prerequisite_count == 2)
            BUSTER_TEST(arguments, compiler_driver_dependency_test_path_equal(rule.prerequisites[1], late_header));
        scratch_end(attempt);
    }
    scratch_end(temporary);
    return result;
}
