#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#include <buster/base.h>
#include <buster/os.h>
#include <buster/entry_point.h>
#include <buster/file.h>
#include <buster/integer.h>
#include <buster/string.h>
#include <buster/target.h>
#include <stdio.h>

#include <buster/assertion.c>
#include <buster/string.c>
#include <buster/os.c>
#include <buster/arena.c>
#include <buster/file.c>
#include <buster/integer.c>
#include <buster/entry_point.c>
#include <buster/target.c>

typedef enum BuildCommand
{
    BUILD_COMMAND_NONE,
    BUILD_COMMAND_GENERATE,
    BUILD_COMMAND_BUILD,
    BUILD_COMMAND_CLANG_ANALYZE,
    BUILD_COMMAND_CMAKE_PROFILE_SUMMARY,
    BUILD_COMMAND_TEST_ALL_COMBINATIONS,
    BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI,
    BUILD_COMMAND_COUNT,
} BuildCommand;

typedef struct ProcessRun ProcessRun;
struct ProcessRun
{
    SliceString8 arguments;
    SliceString8 environment_keys;
    SliceString8 environment_values;
    ProcessSpawnOptions spawn_options;
    ProcessSpawnResult spawn;
    ProcessRun* next;
};

typedef struct BuildStep BuildStep;
struct BuildStep
{
    ProcessRun* first_process;
    ProcessRun* last_process;
    BuildStep* next;
};

typedef struct BuildGraph BuildGraph;
struct BuildGraph
{
    BuildStep* first_step;
    BuildStep* last_step;
};

typedef struct Program Program;
struct Program
{
    ProgramState state;
    BuildGraph build_graph;
};

BUSTER_GLOBAL_LOCAL Program program = {0};

BUSTER_V_IMPL ProgramState* program_state = &program.state;

typedef enum BuildArgument
{
    BUILD_ARGUMENT_CC,
    BUILD_ARGUMENT_CLANG,
    BUILD_ARGUMENT_CONFIG,
    BUILD_ARGUMENT_LIMIT,
    BUILD_ARGUMENT_QUIET,
    BUILD_ARGUMENT_COUNT,
} BuildArgument;

BUSTER_GLOBAL_LOCAL String8 build_arguments[] = {
    [BUILD_ARGUMENT_CC] = S8_INITIALIZER("--cc"),
    [BUILD_ARGUMENT_CLANG] = S8_INITIALIZER("--clang"),
    [BUILD_ARGUMENT_CONFIG] = S8_INITIALIZER("--config"),
    [BUILD_ARGUMENT_LIMIT] = S8_INITIALIZER("--limit"),
    [BUILD_ARGUMENT_QUIET] = S8_INITIALIZER("--quiet"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(build_arguments) == BUILD_ARGUMENT_COUNT);

typedef enum BuildCompiler
{
    BUILD_COMPILER_CL,
    BUILD_COMPILER_CLANG,
    BUILD_COMPILER_GCC,
    BUILD_COMPILER_TCC,
    BUILD_COMPILER_ZIG,
    BUILD_COMPILER_COUNT,
} BuildCompiler;

BUSTER_GLOBAL_LOCAL String8 build_compilers[] = {
    [BUILD_COMPILER_CL] = S8_INITIALIZER("cl"),
    [BUILD_COMPILER_CLANG] = S8_INITIALIZER("clang"),
    [BUILD_COMPILER_GCC] = S8_INITIALIZER("gcc"),
    [BUILD_COMPILER_TCC] = S8_INITIALIZER("tcc"),
    [BUILD_COMPILER_ZIG] = S8_INITIALIZER("zig"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(build_compilers) == BUILD_COMPILER_COUNT);

typedef struct Generate Generate;
struct Generate
{
    String8 build_directory;
    BuildCompiler compiler;
    u32 fuzz:1;
    u32 sanitize:1;
    u32 ci:1;
    u32 link_libc:1;
    u32 time_trace:1;
    u32 lto:1;
    u32 include_tests:1;
    u32 check_optional_warnings:1;
    u32 developer_targets:1;
    u32 profile_cmake:1;
};

BUSTER_GLOBAL_LOCAL String8 cmake_path = {0};

BUSTER_GLOBAL_LOCAL String8 cl_path = {0};
BUSTER_GLOBAL_LOCAL String8 clang_path = {0};
BUSTER_GLOBAL_LOCAL String8 gcc_path = {0};
BUSTER_GLOBAL_LOCAL String8 tcc_path = {0};
BUSTER_GLOBAL_LOCAL String8 zig_cc_path = {0};

BUSTER_GLOBAL_LOCAL BuildStep* step_create(Arena* arena)
{
    BuildStep* step = arena_allocate(arena, BuildStep, 1);
    *step = (BuildStep){0};
    return step;
}

BUSTER_GLOBAL_LOCAL void step_link(BuildStep* step, BuildStep* previous)
{
    if (previous)
    {
        previous->next = step;
    }
}

BUSTER_GLOBAL_LOCAL void build_graph_step_add(BuildStep* step)
{
    if (program.build_graph.last_step)
    {
        program.build_graph.last_step->next = step; 
    }
    else
    {
        program.build_graph.first_step = step;
    }

    program.build_graph.last_step = step;
}

BUSTER_GLOBAL_LOCAL BuildStep* step_add(Arena* arena)
{
    BuildStep* step = step_create(arena);
    build_graph_step_add(step);
    return step;
}

BUSTER_GLOBAL_LOCAL ProcessRun* run_add(Arena* arena, BuildStep* step)
{
    ProcessRun* run = arena_allocate(arena, ProcessRun, 1);

    if (step->last_process)
    {
        step->last_process->next = run;
    }
    else
    {
        step->first_process = run;
    }

    step->last_process = run;

    return run;
}

typedef struct GenericRun GenericRun;
struct GenericRun
{
    OsArgumentBuilder builder;
    ProcessRun* run;
};

BUSTER_GLOBAL_LOCAL String8 get_resolved_path(Arena* arena, String8* resolved, String8 unresolved)
{
    if (!resolved->pointer)
    {
        *resolved = executable_resolve_in_path(arena, unresolved);
    }

    return *resolved;
}

BUSTER_GLOBAL_LOCAL GenericRun generic_tool_run_add_start(Arena* arena, BuildStep* step, String8* resolved_path, String8 unresolved_path)
{
    ProcessRun* run = run_add(arena, step);
    String8 resolved = get_resolved_path(arena, resolved_path, unresolved_path);

    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, resolved);

    return (GenericRun) { .builder = builder, .run = run };
}

BUSTER_GLOBAL_LOCAL void generic_tool_run_add_end(GenericRun r)
{
    SliceString8 arguments = os_argument_builder_flush(&r.builder);

    *r.run = (ProcessRun) {
        .arguments = arguments,
        .spawn_options = (ProcessSpawnOptions){
            .use_process_environment = 1,
        },
    };
}

BUSTER_GLOBAL_LOCAL String8 cmake_cc(Arena* arena, BuildCompiler compiler)
{
    switch (compiler)
    {
        break; case BUILD_COMPILER_CL: return get_resolved_path(arena, &cl_path, S8("cl"));
        break; case BUILD_COMPILER_CLANG: return get_resolved_path(arena, &clang_path, S8("clang"));
        break; case BUILD_COMPILER_GCC: return get_resolved_path(arena, &gcc_path, S8("gcc"));
        break; case BUILD_COMPILER_TCC: return get_resolved_path(arena, &tcc_path, S8("tcc"));
        break; case BUILD_COMPILER_ZIG:
        {
            bool resolved = zig_cc_path.pointer != 0;

            String8 result = get_resolved_path(arena, &zig_cc_path, S8("zig"));
            if (!resolved)
            {
                String8 zig_path_parts[] = {
                    result,
                    S8(";cc"),
                };
                result = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(zig_path_parts), true);
                zig_cc_path = result;
            }

            return result;
        }
        break; case BUILD_COMPILER_COUNT: return S8("");
        break; default: return S8("");
    }
}

BUSTER_GLOBAL_LOCAL String8 cmake_flag(Arena* arena, String8 name, bool value)
{
    String8 parts[] = {
        S8("-D"),
        name,
        S8("="),
        value ? S8("ON") : S8("OFF"),
    };

    String8 result = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 cmake_string(Arena* arena, String8 name, String8 value)
{
    String8 parts[] = {
        S8("-D"),
        name,
        S8("="),
        value,
    };

    String8 result = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true);
    return result;
}

BUSTER_GLOBAL_LOCAL void generate_add(Arena* arena, BuildStep* step, Generate generate)
{
    os_make_directory(generate.build_directory);
    String8 cc = cmake_string(arena, S8("CMAKE_C_COMPILER"), cmake_cc(arena, generate.compiler));
    String8 ci = cmake_flag(arena, S8("BUSTER_CI"), generate.ci);
    String8 lto = cmake_flag(arena, S8("BUSTER_LTO"), generate.lto);
    String8 time_trace = cmake_flag(arena, S8("BUSTER_TIME_TRACE"), generate.time_trace);
    String8 fuzz = cmake_flag(arena, S8("BUSTER_FUZZ"), generate.fuzz);
    String8 sanitize = cmake_flag(arena, S8("BUSTER_SANITIZE"), generate.sanitize);
    String8 include_tests = cmake_flag(arena, S8("BUSTER_INCLUDE_TESTS"), generate.include_tests);
    String8 link_libc = cmake_flag(arena, S8("BUSTER_LINK_LIBC"), generate.link_libc);
    String8 check_optional_warnings = cmake_flag(arena, S8("BUSTER_CHECK_OPTIONAL_WARNINGS"), generate.check_optional_warnings);
    String8 developer_targets = cmake_flag(arena, S8("BUSTER_DEVELOPER_TARGETS"), generate.developer_targets);

    GenericRun r = generic_tool_run_add_start(arena, step, &cmake_path, S8("cmake"));
    OsArgumentBuilder* b = &r.builder;
    os_argument_builder_append(b, S8("--warn-uninitialized"));
    os_argument_builder_append(b, S8("-Werror=dev"));
    os_argument_builder_append(b, S8("-B"));
    os_argument_builder_append(b, generate.build_directory);
    os_argument_builder_append(b, ci);
    os_argument_builder_append(b, cc);
    os_argument_builder_append(b, fuzz);
    os_argument_builder_append(b, sanitize);

    if (BUSTER_LINUX && generate.compiler != BUILD_COMPILER_TCC)
    {
        os_argument_builder_append(b, S8("-DCMAKE_LINKER_TYPE=MOLD"));
    }
    else
    {
        os_argument_builder_append(b, S8("-DCMAKE_LINKER_TYPE=DEFAULT"));
    }

    os_argument_builder_append(b, lto);
    os_argument_builder_append(b, time_trace);
    os_argument_builder_append(b, include_tests);
    os_argument_builder_append(b, link_libc);
    os_argument_builder_append(b, check_optional_warnings);
    os_argument_builder_append(b, developer_targets);

    if (generate.compiler == BUILD_COMPILER_ZIG)
    {
        os_argument_builder_append(b, S8("-DCMAKE_C_LINKER_DEPFILE_SUPPORTED=FALSE"));
        os_argument_builder_append(b, S8("-DCMAKE_C_LINK_DEPENDS_USE_LINKER=FALSE"));
    }

    if (generate.compiler == BUILD_COMPILER_CLANG && !generate.fuzz && !generate.sanitize)
    {
        os_argument_builder_append(b, S8("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"));
    }

    os_argument_builder_append(b, S8("-G"));
    os_argument_builder_append(b, S8("Ninja Multi-Config"));
    os_argument_builder_append(b, S8("-DCMAKE_DEFAULT_BUILD_TYPE=Debug"));
    os_argument_builder_append(b, S8("-DCMAKE_CONFIGURATION_TYPES=Debug;Release"));

    generic_tool_run_add_end(r);
}

typedef struct CmakeBuildOptions CmakeBuildOptions;
struct CmakeBuildOptions
{
    u32 optimize:1;
    u32 quiet:1;
};

BUSTER_GLOBAL_LOCAL void build_add(Arena* arena, String8 build_directory, SliceString8 extra_arguments, CmakeBuildOptions options)
{
    BuildStep* step = step_add(arena);
    GenericRun r = generic_tool_run_add_start(arena, step, &cmake_path, S8("cmake"));
    OsArgumentBuilder* b = &r.builder;
    os_argument_builder_append(b, S8("--build"));
    os_argument_builder_append(b, build_directory);
    os_argument_builder_append(b, S8("--config"));
    os_argument_builder_append(b, options.optimize ? S8("Release") : S8("Debug"));

    for (u64 i = 0; i < extra_arguments.length; i += 1)
    {
        String8 extra_argument = extra_arguments.pointer[i];
        os_argument_builder_append(b, extra_argument);
    }

    if (options.quiet)
    {
        os_argument_builder_append(b, S8("--"));
        os_argument_builder_append(b, S8("--quiet"));
    }

    generic_tool_run_add_end(r);
}

typedef struct String8Node String8Node;
struct String8Node
{
    String8 string;
    String8Node* next;
};

typedef struct String8List String8List;
struct String8List
{
    String8Node* first;
    String8Node* last;
    u64 count;
};

typedef struct JsonParser JsonParser;
struct JsonParser
{
    String8 text;
    u64 index;
};

typedef struct CompileCommandEntry CompileCommandEntry;
struct CompileCommandEntry
{
    String8 directory;
    String8 command;
    String8 file;
    String8 output;
    SliceString8 arguments;
};

typedef struct ClangAnalyzeOptions ClangAnalyzeOptions;
struct ClangAnalyzeOptions
{
    String8 compile_commands;
    String8 config;
    String8 clang;
    u32 quiet:1;
    u32 compile_commands_set:1;
};

typedef struct CmakeProfileSummaryOptions CmakeProfileSummaryOptions;
struct CmakeProfileSummaryOptions
{
    String8 profile;
    u64 limit;
    u32 profile_set:1;
};

typedef struct CmakeProfileEvent CmakeProfileEvent;
struct CmakeProfileEvent
{
    CmakeProfileEvent* next;
    s64 pid;
    s64 tid;
    s64 ts;
    char8 phase;
    String8 category;
    String8 name;
    String8 location;
    String8 function_arguments;
};

typedef struct CmakeProfileStack CmakeProfileStack;
struct CmakeProfileStack
{
    CmakeProfileStack* next;
    CmakeProfileEvent* top;
    s64 pid;
    s64 tid;
};

typedef struct CmakeProfileRow CmakeProfileRow;
struct CmakeProfileRow
{
    s64 duration_us;
    String8 category;
    String8 name;
    String8 location;
    String8 function_arguments;
};

typedef struct CmakeProfileRowNode CmakeProfileRowNode;
struct CmakeProfileRowNode
{
    CmakeProfileRow row;
    CmakeProfileRowNode* next;
};

typedef struct CmakeProfileRowList CmakeProfileRowList;
struct CmakeProfileRowList
{
    CmakeProfileRowNode* first;
    CmakeProfileRowNode* last;
    u64 count;
};

BUSTER_GLOBAL_LOCAL bool character_is_space(char8 c)
{
    bool result = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
    return result;
}

BUSTER_GLOBAL_LOCAL char8 character_to_lower(char8 c)
{
    if (c >= 'A' && c <= 'Z')
    {
        c = (char8)(c - 'A' + 'a');
    }
    return c;
}

BUSTER_GLOBAL_LOCAL bool string_contains(String8 s, String8 sub)
{
    bool result = string_first_sequence(s, sub) != BUSTER_STRING_NO_MATCH;
    return result;
}

BUSTER_GLOBAL_LOCAL bool string_ends_with_sequence_insensitive(String8 s, String8 suffix)
{
    bool result = false;
    if (s.length >= suffix.length)
    {
        result = true;
        u64 offset = s.length - suffix.length;
        for (u64 i = 0; i < suffix.length; i += 1)
        {
            if (character_to_lower(s.pointer[offset + i]) != character_to_lower(suffix.pointer[i]))
            {
                result = false;
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void string8_list_push(Arena* arena, String8List* list, String8 string)
{
    String8Node* node = arena_allocate(arena, String8Node, 1);
    *node = (String8Node){ .string = string };

    if (list->last)
    {
        list->last->next = node;
    }
    else
    {
        list->first = node;
    }

    list->last = node;
    list->count += 1;
}

BUSTER_GLOBAL_LOCAL SliceString8 string8_list_to_slice(Arena* arena, String8List list)
{
    SliceString8 result = { .pointer = arena_allocate(arena, String8, list.count), .length = list.count };

    u64 i = 0;
    for (String8Node* node = list.first; node; node = node->next, i += 1)
    {
        result.pointer[i] = node->string;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void arena_append_char8(Arena* arena, char8 c)
{
    *arena_allocate(arena, char8, 1) = c;
}

BUSTER_GLOBAL_LOCAL void arena_append_utf8(Arena* arena, u32 codepoint)
{
    if (codepoint <= 0x7f)
    {
        arena_append_char8(arena, (char8)codepoint);
    }
    else if (codepoint <= 0x7ff)
    {
        arena_append_char8(arena, (char8)(0xc0 | (codepoint >> 6)));
        arena_append_char8(arena, (char8)(0x80 | (codepoint & 0x3f)));
    }
    else if (codepoint <= 0xffff)
    {
        arena_append_char8(arena, (char8)(0xe0 | (codepoint >> 12)));
        arena_append_char8(arena, (char8)(0x80 | ((codepoint >> 6) & 0x3f)));
        arena_append_char8(arena, (char8)(0x80 | (codepoint & 0x3f)));
    }
    else
    {
        arena_append_char8(arena, (char8)(0xf0 | (codepoint >> 18)));
        arena_append_char8(arena, (char8)(0x80 | ((codepoint >> 12) & 0x3f)));
        arena_append_char8(arena, (char8)(0x80 | ((codepoint >> 6) & 0x3f)));
        arena_append_char8(arena, (char8)(0x80 | (codepoint & 0x3f)));
    }
}

BUSTER_GLOBAL_LOCAL u32 hexadecimal_digit_value(char8 c, bool* valid)
{
    u32 result = 0;
    if (c >= '0' && c <= '9')
    {
        result = (u32)(c - '0');
    }
    else if (c >= 'a' && c <= 'f')
    {
        result = (u32)(10 + c - 'a');
    }
    else if (c >= 'A' && c <= 'F')
    {
        result = (u32)(10 + c - 'A');
    }
    else
    {
        *valid = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void json_skip_whitespace(JsonParser* parser)
{
    while (parser->index < parser->text.length && character_is_space(parser->text.pointer[parser->index]))
    {
        parser->index += 1;
    }
}

BUSTER_GLOBAL_LOCAL bool json_consume(JsonParser* parser, char8 c)
{
    json_skip_whitespace(parser);
    bool result = parser->index < parser->text.length && parser->text.pointer[parser->index] == c;
    if (result)
    {
        parser->index += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s64 json_parse_s64(JsonParser* parser, bool* valid)
{
    s64 result = 0;
    json_skip_whitespace(parser);

    bool negative = false;
    if (parser->index < parser->text.length && parser->text.pointer[parser->index] == '-')
    {
        negative = true;
        parser->index += 1;
    }

    bool found_digit = false;
    while (parser->index < parser->text.length)
    {
        char8 c = parser->text.pointer[parser->index];
        if (!code_unit_is_decimal(c))
        {
            break;
        }
        found_digit = true;
        result = result * 10 + (s64)(c - '0');
        parser->index += 1;
    }

    if (!found_digit)
    {
        *valid = false;
    }

    if (parser->index < parser->text.length && parser->text.pointer[parser->index] == '.')
    {
        parser->index += 1;
        while (parser->index < parser->text.length && code_unit_is_decimal(parser->text.pointer[parser->index]))
        {
            parser->index += 1;
        }
    }

    if (parser->index < parser->text.length && (parser->text.pointer[parser->index] == 'e' || parser->text.pointer[parser->index] == 'E'))
    {
        parser->index += 1;
        if (parser->index < parser->text.length && (parser->text.pointer[parser->index] == '+' || parser->text.pointer[parser->index] == '-'))
        {
            parser->index += 1;
        }
        while (parser->index < parser->text.length && code_unit_is_decimal(parser->text.pointer[parser->index]))
        {
            parser->index += 1;
        }
    }

    if (negative)
    {
        result = -result;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 json_parse_string(Arena* arena, JsonParser* parser, bool* valid)
{
    String8 result = {0};
    json_skip_whitespace(parser);

    if (parser->index >= parser->text.length || parser->text.pointer[parser->index] != '"')
    {
        *valid = false;
        return result;
    }

    parser->index += 1;
    u64 start = arena->position;

    while (*valid && parser->index < parser->text.length)
    {
        char8 c = parser->text.pointer[parser->index++];
        if (c == '"')
        {
            result = (String8){ .pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start };
            arena_append_char8(arena, 0);
            return result;
        }
        else if (c == '\\')
        {
            if (parser->index >= parser->text.length)
            {
                *valid = false;
                break;
            }

            char8 escape = parser->text.pointer[parser->index++];
            switch (escape)
            {
                break; case '"': arena_append_char8(arena, '"');
                break; case '\\': arena_append_char8(arena, '\\');
                break; case '/': arena_append_char8(arena, '/');
                break; case 'b': arena_append_char8(arena, '\b');
                break; case 'f': arena_append_char8(arena, '\f');
                break; case 'n': arena_append_char8(arena, '\n');
                break; case 'r': arena_append_char8(arena, '\r');
                break; case 't': arena_append_char8(arena, '\t');
                break; case 'u':
                {
                    if (parser->index + 4 > parser->text.length)
                    {
                        *valid = false;
                        break;
                    }

                    bool hex_valid = true;
                    u32 codepoint = 0;
                    for (u64 digit_i = 0; digit_i < 4; digit_i += 1)
                    {
                        codepoint = (codepoint << 4) | hexadecimal_digit_value(parser->text.pointer[parser->index++], &hex_valid);
                    }

                    if (hex_valid)
                    {
                        arena_append_utf8(arena, codepoint);
                    }
                    else
                    {
                        *valid = false;
                    }
                }
                break; default:
                {
                    *valid = false;
                }
            }
        }
        else
        {
            arena_append_char8(arena, c);
        }
    }

    *valid = false;
    return result;
}

BUSTER_GLOBAL_LOCAL void json_skip_value(Arena* arena, JsonParser* parser, bool* valid);

BUSTER_GLOBAL_LOCAL SliceString8 json_parse_string_array(Arena* arena, JsonParser* parser, bool* valid)
{
    SliceString8 result = {0};
    String8List list = {0};

    if (!json_consume(parser, '['))
    {
        *valid = false;
        return result;
    }

    for (;;)
    {
        json_skip_whitespace(parser);
        if (json_consume(parser, ']'))
        {
            break;
        }

        String8 string = json_parse_string(arena, parser, valid);
        if (!*valid)
        {
            break;
        }
        string8_list_push(arena, &list, string);

        json_skip_whitespace(parser);
        if (json_consume(parser, ','))
        {
            continue;
        }
        if (json_consume(parser, ']'))
        {
            break;
        }

        *valid = false;
        break;
    }

    if (*valid)
    {
        result = string8_list_to_slice(arena, list);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void json_skip_value(Arena* arena, JsonParser* parser, bool* valid)
{
    json_skip_whitespace(parser);
    if (parser->index >= parser->text.length)
    {
        *valid = false;
        return;
    }

    char8 c = parser->text.pointer[parser->index];
    if (c == '"')
    {
        json_parse_string(arena, parser, valid);
    }
    else if (c == '{')
    {
        parser->index += 1;
        for (;;)
        {
            json_skip_whitespace(parser);
            if (json_consume(parser, '}'))
            {
                break;
            }
            json_parse_string(arena, parser, valid);
            if (!*valid || !json_consume(parser, ':'))
            {
                *valid = false;
                break;
            }
            json_skip_value(arena, parser, valid);
            if (!*valid)
            {
                break;
            }
            if (json_consume(parser, ','))
            {
                continue;
            }
            if (json_consume(parser, '}'))
            {
                break;
            }
            *valid = false;
            break;
        }
    }
    else if (c == '[')
    {
        parser->index += 1;
        for (;;)
        {
            json_skip_whitespace(parser);
            if (json_consume(parser, ']'))
            {
                break;
            }
            json_skip_value(arena, parser, valid);
            if (!*valid)
            {
                break;
            }
            if (json_consume(parser, ','))
            {
                continue;
            }
            if (json_consume(parser, ']'))
            {
                break;
            }
            *valid = false;
            break;
        }
    }
    else
    {
        while (parser->index < parser->text.length)
        {
            c = parser->text.pointer[parser->index];
            if (c == ',' || c == ']' || c == '}' || character_is_space(c))
            {
                break;
            }
            parser->index += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL CompileCommandEntry json_parse_compile_command_entry(Arena* arena, JsonParser* parser, bool* valid)
{
    CompileCommandEntry result = {0};

    if (!json_consume(parser, '{'))
    {
        *valid = false;
        return result;
    }

    for (;;)
    {
        json_skip_whitespace(parser);
        if (json_consume(parser, '}'))
        {
            break;
        }

        String8 key = json_parse_string(arena, parser, valid);
        if (!*valid || !json_consume(parser, ':'))
        {
            *valid = false;
            break;
        }

        if (string_equal(key, S8("directory")))
        {
            result.directory = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("command")))
        {
            result.command = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("file")))
        {
            result.file = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("output")))
        {
            result.output = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("arguments")))
        {
            result.arguments = json_parse_string_array(arena, parser, valid);
        }
        else
        {
            json_skip_value(arena, parser, valid);
        }

        if (!*valid)
        {
            break;
        }

        if (json_consume(parser, ','))
        {
            continue;
        }
        if (json_consume(parser, '}'))
        {
            break;
        }

        *valid = false;
        break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL SliceString8 shell_split(Arena* arena, String8 command, bool* valid)
{
    String8List list = {0};
    u64 index = 0;

#if BUSTER_WINDOWS
    while (*valid)
    {
        while (index < command.length && character_is_space(command.pointer[index]))
        {
            index += 1;
        }

        if (index >= command.length)
        {
            break;
        }

        u64 start = arena->position;
        bool in_quotes = false;
        while (index < command.length)
        {
            char8 c = command.pointer[index++];
            if (c == '"')
            {
                in_quotes = !in_quotes;
            }
            else if (!in_quotes && character_is_space(c))
            {
                break;
            }
            else
            {
                arena_append_char8(arena, c);
            }
        }

        String8 argument = { .pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start };
        arena_append_char8(arena, 0);
        string8_list_push(arena, &list, argument);
    }
#else
    while (*valid)
    {
        while (index < command.length && character_is_space(command.pointer[index]))
        {
            index += 1;
        }

        if (index >= command.length)
        {
            break;
        }

        u64 start = arena->position;
        while (*valid && index < command.length && !character_is_space(command.pointer[index]))
        {
            char8 c = command.pointer[index++];
            if (c == '\'')
            {
                while (index < command.length && command.pointer[index] != '\'')
                {
                    arena_append_char8(arena, command.pointer[index++]);
                }

                if (index < command.length && command.pointer[index] == '\'')
                {
                    index += 1;
                }
                else
                {
                    *valid = false;
                }
            }
            else if (c == '"')
            {
                while (index < command.length && command.pointer[index] != '"')
                {
                    char8 quoted = command.pointer[index++];
                    if (quoted == '\\' && index < command.length)
                    {
                        quoted = command.pointer[index++];
                    }
                    arena_append_char8(arena, quoted);
                }

                if (index < command.length && command.pointer[index] == '"')
                {
                    index += 1;
                }
                else
                {
                    *valid = false;
                }
            }
            else if (c == '\\')
            {
                if (index < command.length)
                {
                    arena_append_char8(arena, command.pointer[index++]);
                }
                else
                {
                    *valid = false;
                }
            }
            else
            {
                arena_append_char8(arena, c);
            }
        }

        if (*valid)
        {
            String8 argument = { .pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start };
            arena_append_char8(arena, 0);
            string8_list_push(arena, &list, argument);
        }
    }
#endif

    SliceString8 result = {0};
    if (*valid)
    {
        result = string8_list_to_slice(arena, list);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_skip_option(String8 argument, String8 next_argument, u64* skip_count)
{
    String8 drop_options[] = {
        S8("-c"),
        S8("-fcolor-diagnostics"),
        S8("-MD"),
        S8("-MMD"),
        S8("-MP"),
    };
    String8 separate_options[] = {
        S8("-MF"),
        S8("-MJ"),
        S8("-MQ"),
        S8("-MT"),
        S8("-o"),
        S8("--output"),
        S8("-dependency-file"),
    };
    String8 joined_options[] = {
        S8("-MF"),
        S8("-MJ"),
        S8("-MQ"),
        S8("-MT"),
        S8("-o"),
        S8("--output="),
        S8("-dependency-file="),
    };

    bool result = false;
    *skip_count = 0;

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(drop_options); i += 1)
    {
        if (string_equal(argument, drop_options[i]))
        {
            *skip_count = 1;
            result = true;
            break;
        }
    }

    for (u64 i = 0; !result && i < BUSTER_ARRAY_LENGTH(separate_options); i += 1)
    {
        if (string_equal(argument, separate_options[i]))
        {
            *skip_count = next_argument.pointer ? 2 : 1;
            result = true;
            break;
        }
    }

    for (u64 i = 0; !result && i < BUSTER_ARRAY_LENGTH(joined_options); i += 1)
    {
        String8 prefix = joined_options[i];
        if (argument.length > prefix.length && string_starts_with_sequence(argument, prefix))
        {
            *skip_count = 1;
            result = true;
            break;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL SliceString8 clang_analyzer_command(Arena* arena, SliceString8 compile_arguments, String8 clang)
{
    SliceString8 result = {0};
    if (compile_arguments.length)
    {
        u64 count = 6;
        for (u64 i = 1; i < compile_arguments.length;)
        {
            String8 next = i + 1 < compile_arguments.length ? compile_arguments.pointer[i + 1] : (String8){0};
            u64 skip_count = 0;
            if (clang_analyze_skip_option(compile_arguments.pointer[i], next, &skip_count))
            {
                i += skip_count;
            }
            else
            {
                count += 1;
                i += 1;
            }
        }

        result = (SliceString8){ .pointer = arena_allocate(arena, String8, count), .length = count };
        u64 out = 0;
        result.pointer[out++] = clang.pointer ? clang : compile_arguments.pointer[0];
        result.pointer[out++] = S8("--analyze");
        result.pointer[out++] = S8("-Xanalyzer");
        result.pointer[out++] = S8("-analyzer-output=text");
        result.pointer[out++] = S8("-fno-color-diagnostics");
        result.pointer[out++] = S8("-Wno-error=unused-command-line-argument");

        for (u64 i = 1; i < compile_arguments.length;)
        {
            String8 next = i + 1 < compile_arguments.length ? compile_arguments.pointer[i + 1] : (String8){0};
            u64 skip_count = 0;
            if (clang_analyze_skip_option(compile_arguments.pointer[i], next, &skip_count))
            {
                i += skip_count;
            }
            else
            {
                result.pointer[out++] = compile_arguments.pointer[i++];
            }
        }
        BUSTER_CHECK(out == count);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_is_c_source(String8 file)
{
    bool result = string_ends_with_sequence_insensitive(file, S8(".c"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool string_has_path_part(String8 s, String8 part)
{
    bool result = false;
    u64 start = 0;

    for (u64 i = 0; i <= s.length; i += 1)
    {
        bool at_end = i == s.length;
        bool at_separator = !at_end && (s.pointer[i] == '/' || s.pointer[i] == '\\');
        if (at_end || at_separator)
        {
            String8 candidate = string_slice(s, start, i);
            if (string_equal(candidate, part))
            {
                result = true;
                break;
            }
            start = i + 1;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_entry_matches_config(Arena* arena, CompileCommandEntry entry, SliceString8 arguments, String8 config)
{
    bool result = true;
    if (config.pointer && config.length)
    {
        result = false;
        String8 cmake_intdir_plain = string_format(arena, S8("CMAKE_INTDIR={S8}"), config);
        String8 cmake_intdir_quoted = string_format(arena, S8("CMAKE_INTDIR=\"{S8}\""), config);
        String8 cmake_intdir_escaped = string_format(arena, S8("CMAKE_INTDIR=\\\"{S8}\\\""), config);

        if (entry.output.pointer)
        {
            result = string_has_path_part(entry.output, config) || string_contains(entry.output, cmake_intdir_plain) || string_contains(entry.output, cmake_intdir_quoted) || string_contains(entry.output, cmake_intdir_escaped);
        }

        for (u64 i = 0; !result && i < arguments.length; i += 1)
        {
            String8 candidate = arguments.pointer[i];
            result = string_has_path_part(candidate, config) || string_contains(candidate, cmake_intdir_plain) || string_contains(candidate, cmake_intdir_quoted) || string_contains(candidate, cmake_intdir_escaped);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_output_has_warning(String8 output)
{
    bool result = string_contains(output, S8(": warning:"));
    return result;
}

BUSTER_GLOBAL_LOCAL void command_print(SliceString8 command)
{
    string_print(S8("+ {[]S8}\n"), command);
}

BUSTER_GLOBAL_LOCAL ProcessWaitResult clang_analyze_run_command(Arena* arena, SliceString8 command, String8 directory, bool quiet, bool* has_warning)
{
    if (!quiet)
    {
        command_print(command);
    }

    bool restore_directory = false;
#if BUSTER_WINDOWS
    char16 old_directory_buffer[BUSTER_KB(32) / sizeof(char16)];
    DWORD old_directory_length = 0;
    if (directory.pointer && directory.length)
    {
        old_directory_length = GetCurrentDirectoryW(BUSTER_ARRAY_LENGTH(old_directory_buffer), old_directory_buffer);
        if (old_directory_length > 0 && old_directory_length < BUSTER_ARRAY_LENGTH(old_directory_buffer))
        {
            TemporalArena temp = scratch_begin(&arena, 1);
            String16 directory16 = string16_from_string8(temp.arena, directory, true);
            restore_directory = SetCurrentDirectoryW(directory16.pointer) != 0;
            scratch_end(temp);
        }
    }
#else
    char old_directory_buffer[BUSTER_KB(32)];
    if (directory.pointer && directory.length && getcwd(old_directory_buffer, sizeof(old_directory_buffer)))
    {
        restore_directory = chdir(directory.pointer) == 0;
    }
#endif

    ProcessSpawnResult spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){
        .capture = ((u64)1 << STANDARD_STREAM_ERROR),
        .use_process_environment = 1,
    });

    if (restore_directory)
    {
#if BUSTER_WINDOWS
        SetCurrentDirectoryW(old_directory_buffer);
#else
        chdir(old_directory_buffer);
#endif
    }

    ProcessWaitResult wait = os_process_wait_sync(arena, spawn);
    String8 error_output = { .pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length };
    *has_warning = clang_analyze_output_has_warning(error_output);

    if (quiet && (wait.result != PROCESS_RESULT_SUCCESS || *has_warning))
    {
        command_print(command);
    }

    if (error_output.length)
    {
        os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(error_output));
    }

    return wait;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_compile_commands_path(Arena* arena, String8 path)
{
    if (!path.pointer || !path.length)
    {
        path = S8("build");
    }

    String8 result = path;
    if (!string_ends_with_sequence_insensitive(path, S8(".json")))
    {
        String8 separator = S8("/");
        if (path.length && (path.pointer[path.length - 1] == '/' || path.pointer[path.length - 1] == '\\'))
        {
            separator = S8("");
        }

        String8 parts[] = {
            path,
            separator,
            S8("compile_commands.json"),
        };
        result = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult clang_analyze_run(Arena* arena, ClangAnalyzeOptions options)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    String8 path = clang_analyze_compile_commands_path(arena, options.compile_commands);
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){ .end_padding = 1 });

    if (!bytes.pointer)
    {
        string_print(S8("error: compile commands not found: {S8}\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    JsonParser parser = { .text = { .pointer = (char8*)bytes.pointer, .length = bytes.length } };
    bool valid = true;
    if (!json_consume(&parser, '['))
    {
        string_print(S8("error: failed to read {S8}: expected JSON array\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    u64 analyzed = 0;
    u64 warnings = 0;
    u64 failures = 0;

    for (;;)
    {
        json_skip_whitespace(&parser);
        if (json_consume(&parser, ']'))
        {
            break;
        }

        CompileCommandEntry entry = json_parse_compile_command_entry(arena, &parser, &valid);
        if (!valid)
        {
            string_print(S8("error: failed to read {S8}: invalid compile command entry\n"), path);
            return PROCESS_RESULT_FAILED;
        }

        if (entry.file.pointer && clang_analyze_is_c_source(entry.file))
        {
            SliceString8 compile_arguments = entry.arguments;
            bool split_valid = true;
            if (!compile_arguments.length && entry.command.pointer)
            {
                compile_arguments = shell_split(arena, entry.command, &split_valid);
            }

            if (!split_valid || !compile_arguments.length)
            {
                string_print(S8("error: {S8}: compile command entry has no usable command\n"), entry.file);
                failures += 1;
            }
            else if (clang_analyze_entry_matches_config(arena, entry, compile_arguments, options.config))
            {
                SliceString8 command = clang_analyzer_command(arena, compile_arguments, options.clang);
                bool has_warning = false;
                ProcessWaitResult wait = clang_analyze_run_command(arena, command, entry.directory, options.quiet, &has_warning);
                analyzed += 1;
                failures += wait.result != PROCESS_RESULT_SUCCESS;
                warnings += has_warning;
            }
        }

        if (json_consume(&parser, ','))
        {
            continue;
        }
        if (json_consume(&parser, ']'))
        {
            break;
        }

        string_print(S8("error: failed to read {S8}: expected ',' or ']'\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    if (analyzed == 0)
    {
        if (options.config.pointer && options.config.length)
        {
            string_print(S8("error: no C compile commands found for configuration {S8} in {S8}\n"), options.config, path);
        }
        else
        {
            string_print(S8("error: no C compile commands found in {S8}\n"), path);
        }
        result = PROCESS_RESULT_FAILED;
    }
    else
    {
        string_print(S8("clang --analyze checked {u64} translation unit(s), {u64} with analyzer warning(s), {u64} failed.\n"), analyzed, warnings, failures);

        if (warnings || failures)
        {
            result = PROCESS_RESULT_FAILED;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_add(Arena* arena, String8 build_directory, CmakeBuildOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    String8 self = program_state->input.arguments.length ? program_state->input.arguments.pointer[0] : S8("build/build");
    os_argument_builder_append(&builder, self);
    os_argument_builder_append(&builder, S8("clang_analyze"));
    os_argument_builder_append(&builder, build_directory);
    os_argument_builder_append(&builder, S8("--config"));
    os_argument_builder_append(&builder, options.optimize ? S8("Release") : S8("Debug"));
    if (options.quiet)
    {
        os_argument_builder_append(&builder, S8("--quiet"));
    }

    *run = (ProcessRun) {
        .arguments = os_argument_builder_flush(&builder),
        .spawn_options = (ProcessSpawnOptions){
            .use_process_environment = 1,
        },
    };
}

BUSTER_GLOBAL_LOCAL void cmake_profile_row_list_push(Arena* arena, CmakeProfileRowList* list, CmakeProfileRow row)
{
    CmakeProfileRowNode* node = arena_allocate(arena, CmakeProfileRowNode, 1);
    *node = (CmakeProfileRowNode){ .row = row };

    if (list->last)
    {
        list->last->next = node;
    }
    else
    {
        list->first = node;
    }

    list->last = node;
    list->count += 1;
}

BUSTER_GLOBAL_LOCAL CmakeProfileStack* cmake_profile_stack_get(Arena* arena, CmakeProfileStack** first, s64 pid, s64 tid)
{
    CmakeProfileStack* result = 0;
    for (CmakeProfileStack* stack = *first; stack; stack = stack->next)
    {
        if (stack->pid == pid && stack->tid == tid)
        {
            result = stack;
            break;
        }
    }

    if (!result)
    {
        result = arena_allocate(arena, CmakeProfileStack, 1);
        *result = (CmakeProfileStack){
            .next = *first,
            .pid = pid,
            .tid = tid,
        };
        *first = result;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void cmake_profile_parse_args(Arena* arena, JsonParser* parser, CmakeProfileEvent* event, bool* valid)
{
    if (!json_consume(parser, '{'))
    {
        json_skip_value(arena, parser, valid);
        return;
    }

    for (;;)
    {
        json_skip_whitespace(parser);
        if (json_consume(parser, '}'))
        {
            break;
        }

        String8 key = json_parse_string(arena, parser, valid);
        if (!*valid || !json_consume(parser, ':'))
        {
            *valid = false;
            break;
        }

        if (string_equal(key, S8("location")))
        {
            event->location = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("functionArgs")))
        {
            event->function_arguments = json_parse_string(arena, parser, valid);
        }
        else
        {
            json_skip_value(arena, parser, valid);
        }

        if (!*valid)
        {
            break;
        }
        if (json_consume(parser, ','))
        {
            continue;
        }
        if (json_consume(parser, '}'))
        {
            break;
        }

        *valid = false;
        break;
    }
}

BUSTER_GLOBAL_LOCAL CmakeProfileEvent cmake_profile_parse_event(Arena* arena, JsonParser* parser, bool* valid)
{
    CmakeProfileEvent result = {0};

    if (!json_consume(parser, '{'))
    {
        *valid = false;
        return result;
    }

    for (;;)
    {
        json_skip_whitespace(parser);
        if (json_consume(parser, '}'))
        {
            break;
        }

        String8 key = json_parse_string(arena, parser, valid);
        if (!*valid || !json_consume(parser, ':'))
        {
            *valid = false;
            break;
        }

        if (string_equal(key, S8("pid")))
        {
            result.pid = json_parse_s64(parser, valid);
        }
        else if (string_equal(key, S8("tid")))
        {
            result.tid = json_parse_s64(parser, valid);
        }
        else if (string_equal(key, S8("ts")))
        {
            result.ts = json_parse_s64(parser, valid);
        }
        else if (string_equal(key, S8("ph")))
        {
            String8 phase = json_parse_string(arena, parser, valid);
            result.phase = phase.length ? phase.pointer[0] : 0;
        }
        else if (string_equal(key, S8("cat")))
        {
            result.category = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("name")))
        {
            result.name = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("args")))
        {
            cmake_profile_parse_args(arena, parser, &result, valid);
        }
        else
        {
            json_skip_value(arena, parser, valid);
        }

        if (!*valid)
        {
            break;
        }
        if (json_consume(parser, ','))
        {
            continue;
        }
        if (json_consume(parser, '}'))
        {
            break;
        }

        *valid = false;
        break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL int cmake_profile_row_compare(const void* a, const void* b)
{
    const CmakeProfileRow* row_a = (const CmakeProfileRow*)a;
    const CmakeProfileRow* row_b = (const CmakeProfileRow*)b;

    int result = 0;
    if (row_a->duration_us < row_b->duration_us)
    {
        result = 1;
    }
    else if (row_a->duration_us > row_b->duration_us)
    {
        result = -1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int string8_printf_length(String8 string, u64 max_length)
{
    u64 length = BUSTER_MIN(string.length, max_length);
    int result = length > (u64)INT32_MAX ? INT32_MAX : (int)length;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult cmake_profile_summary_run(Arena* arena, CmakeProfileSummaryOptions options)
{
    if (!options.limit)
    {
        options.limit = 25;
    }

    if (!options.profile.pointer || !options.profile.length)
    {
        string_print(S8("error: cmake_profile_summary requires a profile path\n"));
        return PROCESS_RESULT_FAILED;
    }

    ByteSlice bytes = file_read(arena, options.profile, (FileReadOptions){ .end_padding = 1 });
    if (!bytes.pointer)
    {
        string_print(S8("error: failed to read {S8}\n"), options.profile);
        return PROCESS_RESULT_FAILED;
    }

    JsonParser parser = { .text = { .pointer = (char8*)bytes.pointer, .length = bytes.length } };
    bool valid = true;
    if (!json_consume(&parser, '['))
    {
        string_print(S8("error: failed to parse {S8}: expected JSON array\n"), options.profile);
        return PROCESS_RESULT_FAILED;
    }

    CmakeProfileStack* stacks = 0;
    CmakeProfileRowList row_list = {0};

    for (;;)
    {
        json_skip_whitespace(&parser);
        if (json_consume(&parser, ']'))
        {
            break;
        }

        CmakeProfileEvent event = cmake_profile_parse_event(arena, &parser, &valid);
        if (!valid)
        {
            string_print(S8("error: failed to parse {S8}: invalid profiling event\n"), options.profile);
            return PROCESS_RESULT_FAILED;
        }

        CmakeProfileStack* stack = cmake_profile_stack_get(arena, &stacks, event.pid, event.tid);
        if (event.phase == 'B')
        {
            CmakeProfileEvent* stored = arena_allocate(arena, CmakeProfileEvent, 1);
            *stored = event;
            stored->next = stack->top;
            stack->top = stored;
        }
        else if (event.phase == 'E' && stack->top)
        {
            CmakeProfileEvent* start = stack->top;
            stack->top = start->next;
            cmake_profile_row_list_push(arena, &row_list, (CmakeProfileRow){
                .duration_us = event.ts - start->ts,
                .category = start->category,
                .name = start->name,
                .location = start->location,
                .function_arguments = start->function_arguments,
            });
        }

        if (json_consume(&parser, ','))
        {
            continue;
        }
        if (json_consume(&parser, ']'))
        {
            break;
        }

        string_print(S8("error: failed to parse {S8}: expected ',' or ']'\n"), options.profile);
        return PROCESS_RESULT_FAILED;
    }

    if (!row_list.count)
    {
        string_print(S8("No complete CMake profiling events found.\n"));
        return PROCESS_RESULT_SUCCESS;
    }

    CmakeProfileRow* rows = arena_allocate(arena, CmakeProfileRow, row_list.count);
    u64 row_i = 0;
    for (CmakeProfileRowNode* node = row_list.first; node; node = node->next)
    {
        rows[row_i++] = node->row;
    }
    qsort(rows, row_list.count, sizeof(rows[0]), cmake_profile_row_compare);

    string_print(S8("Slowest CMake configure/generate entries:\n"));
    u64 limit = BUSTER_MIN(options.limit, row_list.count);
    for (u64 i = 0; i < limit; i += 1)
    {
        CmakeProfileRow row = rows[i];
        s64 duration_us = row.duration_us < 0 ? 0 : row.duration_us;
        unsigned long long duration_ms_whole = (unsigned long long)((u64)duration_us / 1000);
        unsigned long long duration_ms_fraction = (unsigned long long)((u64)duration_us % 1000);
        printf("%4llu.%03llu ms  %-10.*s %-30.*s %.*s %.*s\n",
               duration_ms_whole,
               duration_ms_fraction,
               string8_printf_length(row.category, 10), row.category.pointer ? row.category.pointer : "",
               string8_printf_length(row.name, 30), row.name.pointer ? row.name.pointer : "",
               string8_printf_length(row.location, UINT64_MAX), row.location.pointer ? row.location.pointer : "",
               string8_printf_length(row.function_arguments, 100), row.function_arguments.pointer ? row.function_arguments.pointer : "");
    }

    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL void test_all(Arena* arena, bool ci, CmakeBuildOptions base_options)
{
    BuildStep* generate_step = step_add(arena);

    for (BuildCompiler compiler = !BUSTER_WINDOWS; compiler < BUILD_COMPILER_COUNT; compiler += 1)
    {
        bool support_fuzz = (compiler == BUILD_COMPILER_CLANG || compiler == BUILD_COMPILER_CL) && !BUSTER_APPLE;
        bool support_sanitize = compiler != BUILD_COMPILER_TCC && (!BUSTER_WINDOWS || compiler != BUILD_COMPILER_GCC);
        bool support_optimize = compiler != BUILD_COMPILER_TCC;

        for (u32 fuzz = 0; fuzz < 1 + support_fuzz; fuzz += 1)
        {
            for (u32 sanitize = 0; sanitize < 1 + support_sanitize; sanitize += 1)
            {
                for (u32 optimize = 0; optimize < 1 + support_optimize; optimize += 1)
                {
                    String8 build_directory_parts[] = {
                        S8("build/build-"),
                        S8("ci_"),
                        ci ? S8("on") : S8("off"),
                        S8("-cc_"),
                        build_compilers[compiler],
                        S8("-optimize_"),
                        optimize ? S8("on") : S8("off"),
                        S8("-sanitize_"),
                        sanitize ? S8("on") : S8("off"),
                        S8("-fuzz_"),
                        fuzz ? S8("on") : S8("off"),
                    };

                    String8 build_directory = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(build_directory_parts), true);

                    Generate generate = {
                        .build_directory = build_directory,
                        .compiler = compiler,
                        .fuzz = fuzz,
                        .sanitize = sanitize,
                        .ci = ci,
                        .link_libc = true,
                        .time_trace = false,
                        .lto = false,
                        .include_tests = true,
                        .check_optional_warnings = false,
                        .developer_targets = false,
                        .profile_cmake = false,
                    };

                    CmakeBuildOptions options = {
                        .optimize = optimize,
                        .quiet = base_options.quiet,
                    };

                    generate_add(arena, generate_step, generate);

                    build_add(arena, build_directory, (SliceString8){0}, options);

                    String8 test_parts[] = {
                        S8("--target"),
                        S8("test_all"),
                    };
                    build_add(arena, build_directory, (SliceString8)BUSTER_ARRAY_TO_SLICE(test_parts), options);

                    if (compiler == BUILD_COMPILER_CLANG && !sanitize && !fuzz)
                    {
                        clang_analyze_add(arena, build_directory, options);
                    }
                }
            }
        }
    }
}

ProcessResult process_arguments(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    u64 argument_i = 0;
    SliceString8 arguments = program_state->input.arguments;

    BuildCommand command = BUILD_COMMAND_NONE;
    bool command_found = false;

    Arena* arena = program_state->arena;

    BUSTER_GLOBAL_LOCAL String8 build_command_names[] = {
        [BUILD_COMMAND_NONE] = S8_INITIALIZER("none"),
        [BUILD_COMMAND_GENERATE] = S8_INITIALIZER("generate"),
        [BUILD_COMMAND_BUILD] = S8_INITIALIZER("build"),
        [BUILD_COMMAND_CLANG_ANALYZE] = S8_INITIALIZER("clang_analyze"),
        [BUILD_COMMAND_CMAKE_PROFILE_SUMMARY] = S8_INITIALIZER("cmake_profile_summary"),
        [BUILD_COMMAND_TEST_ALL_COMBINATIONS] = S8_INITIALIZER("test_all_combinations"),
        [BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI] = S8_INITIALIZER("test_all_combinations_ci"),
    };

    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(build_command_names) == BUILD_COMMAND_COUNT);

    if (arguments.length)
    {
        argument_i += 1;

        if (arguments.length > 1)
        {
            String8 candidate_command = arguments.pointer[argument_i];

            bool is_candidate_command = !string_starts_with_sequence(candidate_command, S8("--"));
            if (is_candidate_command)
            {
                for (u64 i = 0; i < BUILD_COMMAND_COUNT; i += 1)
                {
                    if (string_equal(candidate_command, build_command_names[i]))
                    {
                        command = (BuildCommand)i;
                        command_found = true;
                        argument_i += 1;
                        break;
                    }
                }
            }
        }
    }
    else
    {
        result = PROCESS_RESULT_FAILED;
    }

    if (!command_found)
    {
        command = BUILD_COMMAND_BUILD;
    }

    String8 build_directory = S8("build");
    Generate generate = {
        .build_directory = build_directory,
        .compiler = BUILD_COMPILER_CLANG,
        .link_libc = true,
        .include_tests = true,
        .developer_targets = true,
    };
    CmakeBuildOptions options = {0};
    ClangAnalyzeOptions clang_analyze_options = { .compile_commands = build_directory };
    CmakeProfileSummaryOptions cmake_profile_summary_options = { .limit = 25 };

    while (result == PROCESS_RESULT_SUCCESS && argument_i < arguments.length)
    {
        String8 argument = arguments.pointer[argument_i];

        BuildArgument build_argument = BUILD_ARGUMENT_COUNT;

        for (u64 i = 0; i < BUILD_ARGUMENT_COUNT; i += 1)
        {
            String8 candidate_argument = build_arguments[i];

            if (string_equal(argument, candidate_argument))
            {
                build_argument = (BuildArgument)i;
                break;
            }
        }

        switch (build_argument)
        {
            break; case BUILD_ARGUMENT_COUNT:
            {
                if (command == BUILD_COMMAND_CLANG_ANALYZE && !clang_analyze_options.compile_commands_set && !string_starts_with_sequence(argument, S8("--")))
                {
                    clang_analyze_options.compile_commands = argument;
                    clang_analyze_options.compile_commands_set = 1;
                    argument_i += 1;
                }
                else if (command == BUILD_COMMAND_CMAKE_PROFILE_SUMMARY && !cmake_profile_summary_options.profile_set && !string_starts_with_sequence(argument, S8("--")))
                {
                    cmake_profile_summary_options.profile = argument;
                    cmake_profile_summary_options.profile_set = 1;
                    argument_i += 1;
                }
                else
                {
                    ProcessResult generic_argument_result = buster_argument_process(argument_i);
                    if (generic_argument_result == PROCESS_RESULT_SUCCESS)
                    {
                        argument_i += 1;
                    }
                    else
                    {
                        result = generic_argument_result;
                        string_print(S8("error: unknown argument => \"{S8}\"\n"), argument);
                    }
                }
            }
            break; case BUILD_ARGUMENT_CC:
            {
                if (command == BUILD_COMMAND_GENERATE && argument_i + 1 < arguments.length)
                {
                    argument_i += 1;

                    String8 candidate_compiler = arguments.pointer[argument_i];

                    BuildCompiler compiler = BUILD_COMPILER_COUNT;
                    for (u64 i = 0; i < BUILD_COMPILER_COUNT; i += 1)
                    {
                        if (string_equal(candidate_compiler, build_compilers[i]))
                        {
                            compiler = (BuildCompiler)i;
                            break;
                        }
                    }

                    if (compiler != BUILD_COMPILER_COUNT)
                    {
                        generate.compiler = compiler;
                    }
                    else
                    {
                        result = PROCESS_RESULT_FAILED;
                    }

                    argument_i += 1;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CLANG:
            {
                if (command == BUILD_COMMAND_CLANG_ANALYZE && argument_i + 1 < arguments.length)
                {
                    argument_i += 1;
                    clang_analyze_options.clang = arguments.pointer[argument_i];
                    argument_i += 1;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CONFIG:
            {
                if (command == BUILD_COMMAND_CLANG_ANALYZE && argument_i + 1 < arguments.length)
                {
                    argument_i += 1;
                    clang_analyze_options.config = arguments.pointer[argument_i];
                    argument_i += 1;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_LIMIT:
            {
                if (command == BUILD_COMMAND_CMAKE_PROFILE_SUMMARY && argument_i + 1 < arguments.length)
                {
                    argument_i += 1;
                    String8 candidate_limit = arguments.pointer[argument_i];
                    IntegerParsingU64 parsed_limit = string8_parse_u64_decimal(candidate_limit.pointer);
                    if (parsed_limit.length == candidate_limit.length && parsed_limit.value > 0)
                    {
                        cmake_profile_summary_options.limit = parsed_limit.value;
                        argument_i += 1;
                    }
                    else
                    {
                        result = PROCESS_RESULT_FAILED;
                    }
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_QUIET:
            {
                options.quiet = 1;
                clang_analyze_options.quiet = 1;
                argument_i += 1;
            }
        }
    }

    if (result == PROCESS_RESULT_SUCCESS)
    {
        switch (command)
        {
            break; case BUILD_COMMAND_COUNT: BUSTER_UNREACHABLE();
            break; case BUILD_COMMAND_NONE: {}
            break; case BUILD_COMMAND_GENERATE:
            {
                BuildStep* generate_step = step_add(arena);
                generate_add(arena, generate_step, generate);
            }
            break; case BUILD_COMMAND_BUILD:
            {
                build_add(arena, build_directory, (SliceString8){0}, options);
            }
            break; case BUILD_COMMAND_CLANG_ANALYZE:
            {
                result = clang_analyze_run(arena, clang_analyze_options);
            }
            break; case BUILD_COMMAND_CMAKE_PROFILE_SUMMARY:
            {
                result = cmake_profile_summary_run(arena, cmake_profile_summary_options);
            }
            break;
            case BUILD_COMMAND_TEST_ALL_COMBINATIONS:
            case BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI:
            {
                bool ci = command == BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI;
                test_all(arena, ci, options);
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void run_metaprogram(void)
{
}

ProcessResult entry_point(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    BuildGraph* build_graph = &program.build_graph;
    Arena* arena = program.state.arena;

    u32 thread_count = os_get_logical_thread_count();

    run_metaprogram();

    for (BuildStep* step = build_graph->first_step; step; step = step->next)
    {
        u32 i = 0;

        for (ProcessRun* run = step->first_process, *first_pending = step->first_process; run; run = run->next, i += 1)
        {
            run->spawn = os_process_spawn(run->arguments, run->environment_keys, run->environment_values, run->spawn_options);

            if (i == thread_count || !run->next)
            {
                for (ProcessRun* wait = first_pending; wait != run->next; wait = wait->next)
                {
                    ProcessWaitResult wait_result = os_process_wait_sync(arena, wait->spawn);

                    if (result == PROCESS_RESULT_SUCCESS && wait_result.result != PROCESS_RESULT_SUCCESS)
                    {
                        result = wait_result.result;
                    }
                }

                first_pending = run->next;
                i = 0;
            }
        }
    }

    return result;
}
