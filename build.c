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
#if BUSTER_LINUX || BUSTER_APPLE
#include <dirent.h>
#endif

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
    BUILD_COMMAND_NINJA_LOG_SUMMARY,
    BUILD_COMMAND_TIME_TRACE_SUMMARY,
    BUILD_COMMAND_TEST_ALL_COMBINATIONS,
    BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI,
    BUILD_COMMAND_COUNT,
} BuildCommand;

typedef struct ProcessRun ProcessRun;
typedef ProcessResult BuildActionCallback(Arena* arena, void* data);

typedef enum ProcessRunFlag
{
    PROCESS_RUN_FLAG_PRINT_COMMAND = (1u << 0),
    PROCESS_RUN_FLAG_PRINT_COMMAND_ON_FAILURE_OR_WARNING = (1u << 1),
    PROCESS_RUN_FLAG_PRINT_CAPTURED_ERROR = (1u << 2),
    PROCESS_RUN_FLAG_STDERR_WARNING_IS_FAILURE = (1u << 3),
    PROCESS_RUN_FLAG_CLANG_ANALYZE = (1u << 4),
} ProcessRunFlag;

struct ProcessRun
{
    SliceString8 arguments;
    SliceString8 environment_keys;
    SliceString8 environment_values;
    ProcessSpawnOptions spawn_options;
    ProcessSpawnResult spawn;
    String8 working_directory;
    String8 timing_description;
    String8 timing_configuration;
    u64 start_us;
    u32 flags;
    BuildActionCallback* callback;
    void* callback_data;
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
    BUILD_ARGUMENT_BUILD_DIRECTORY,
    BUILD_ARGUMENT_BUILD_DIR,
    BUILD_ARGUMENT_CC,
    BUILD_ARGUMENT_CLANG,
    BUILD_ARGUMENT_CI,
    BUILD_ARGUMENT_CMAKE_PROFILE,
    BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY,
    BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY_LIMIT,
    BUILD_ARGUMENT_CONFIG,
    BUILD_ARGUMENT_CONFIGURATION,
    BUILD_ARGUMENT_CHECK_OPTIONAL_WARNINGS,
    BUILD_ARGUMENT_DEVELOPER_TARGETS,
    BUILD_ARGUMENT_FUZZ,
    BUILD_ARGUMENT_INCLUDE_TESTS,
    BUILD_ARGUMENT_INSTRUMENT,
    BUILD_ARGUMENT_LIMIT,
    BUILD_ARGUMENT_LINK_LIBC,
    BUILD_ARGUMENT_LINKER,
    BUILD_ARGUMENT_LTO,
    BUILD_ARGUMENT_NO_CHECK_OPTIONAL_WARNINGS,
    BUILD_ARGUMENT_NO_CI,
    BUILD_ARGUMENT_NO_DEVELOPER_TARGETS,
    BUILD_ARGUMENT_NO_FUZZ,
    BUILD_ARGUMENT_NO_INCLUDE_TESTS,
    BUILD_ARGUMENT_NO_INSTRUMENT,
    BUILD_ARGUMENT_NO_LINK_LIBC,
    BUILD_ARGUMENT_NO_LTO,
    BUILD_ARGUMENT_NO_OPTIMIZE,
    BUILD_ARGUMENT_NO_SANITIZE,
    BUILD_ARGUMENT_NO_TIME_TRACE,
    BUILD_ARGUMENT_OPTIMIZE,
    BUILD_ARGUMENT_QUIET,
    BUILD_ARGUMENT_SANITIZE,
    BUILD_ARGUMENT_TARGET,
    BUILD_ARGUMENT_TARGET_SHORT,
    BUILD_ARGUMENT_TIME_TRACE,
    BUILD_ARGUMENT_VERBOSE,
    BUILD_ARGUMENT_VERBOSE_SHORT,
    BUILD_ARGUMENT_COUNT,
} BuildArgument;

BUSTER_GLOBAL_LOCAL String8 build_arguments[] = {
    [BUILD_ARGUMENT_BUILD_DIRECTORY] = S8_INITIALIZER("--build-directory"),
    [BUILD_ARGUMENT_BUILD_DIR] = S8_INITIALIZER("--build-dir"),
    [BUILD_ARGUMENT_CC] = S8_INITIALIZER("--cc"),
    [BUILD_ARGUMENT_CLANG] = S8_INITIALIZER("--clang"),
    [BUILD_ARGUMENT_CI] = S8_INITIALIZER("--ci"),
    [BUILD_ARGUMENT_CMAKE_PROFILE] = S8_INITIALIZER("--cmake-profile"),
    [BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY] = S8_INITIALIZER("--cmake-profile-summary"),
    [BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY_LIMIT] = S8_INITIALIZER("--cmake-profile-summary-limit"),
    [BUILD_ARGUMENT_CONFIG] = S8_INITIALIZER("--config"),
    [BUILD_ARGUMENT_CONFIGURATION] = S8_INITIALIZER("--configuration"),
    [BUILD_ARGUMENT_CHECK_OPTIONAL_WARNINGS] = S8_INITIALIZER("--check-optional-warnings"),
    [BUILD_ARGUMENT_DEVELOPER_TARGETS] = S8_INITIALIZER("--developer-targets"),
    [BUILD_ARGUMENT_FUZZ] = S8_INITIALIZER("--fuzz"),
    [BUILD_ARGUMENT_INCLUDE_TESTS] = S8_INITIALIZER("--include-tests"),
    [BUILD_ARGUMENT_INSTRUMENT] = S8_INITIALIZER("--instrument"),
    [BUILD_ARGUMENT_LIMIT] = S8_INITIALIZER("--limit"),
    [BUILD_ARGUMENT_LINK_LIBC] = S8_INITIALIZER("--link-libc"),
    [BUILD_ARGUMENT_LINKER] = S8_INITIALIZER("--linker"),
    [BUILD_ARGUMENT_LTO] = S8_INITIALIZER("--lto"),
    [BUILD_ARGUMENT_NO_CHECK_OPTIONAL_WARNINGS] = S8_INITIALIZER("--no-check-optional-warnings"),
    [BUILD_ARGUMENT_NO_CI] = S8_INITIALIZER("--no-ci"),
    [BUILD_ARGUMENT_NO_DEVELOPER_TARGETS] = S8_INITIALIZER("--no-developer-targets"),
    [BUILD_ARGUMENT_NO_FUZZ] = S8_INITIALIZER("--no-fuzz"),
    [BUILD_ARGUMENT_NO_INCLUDE_TESTS] = S8_INITIALIZER("--no-include-tests"),
    [BUILD_ARGUMENT_NO_INSTRUMENT] = S8_INITIALIZER("--no-instrument"),
    [BUILD_ARGUMENT_NO_LINK_LIBC] = S8_INITIALIZER("--no-link-libc"),
    [BUILD_ARGUMENT_NO_LTO] = S8_INITIALIZER("--no-lto"),
    [BUILD_ARGUMENT_NO_OPTIMIZE] = S8_INITIALIZER("--no-optimize"),
    [BUILD_ARGUMENT_NO_SANITIZE] = S8_INITIALIZER("--no-sanitize"),
    [BUILD_ARGUMENT_NO_TIME_TRACE] = S8_INITIALIZER("--no-time-trace"),
    [BUILD_ARGUMENT_OPTIMIZE] = S8_INITIALIZER("--optimize"),
    [BUILD_ARGUMENT_QUIET] = S8_INITIALIZER("--quiet"),
    [BUILD_ARGUMENT_SANITIZE] = S8_INITIALIZER("--sanitize"),
    [BUILD_ARGUMENT_TARGET] = S8_INITIALIZER("--target"),
    [BUILD_ARGUMENT_TARGET_SHORT] = S8_INITIALIZER("-t"),
    [BUILD_ARGUMENT_TIME_TRACE] = S8_INITIALIZER("--time-trace"),
    [BUILD_ARGUMENT_VERBOSE] = S8_INITIALIZER("--verbose"),
    [BUILD_ARGUMENT_VERBOSE_SHORT] = S8_INITIALIZER("-v"),
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

BUSTER_GLOBAL_LOCAL bool build_config_is_valid(String8 config)
{
    String8 configs[] = {
        S8("Debug"),
        S8("Release"),
        S8("RelWithDebInfo"),
        S8("MinSizeRel"),
    };

    bool result = false;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(configs); i += 1)
    {
        if (string_equal(config, configs[i]))
        {
            result = true;
            break;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool build_config_is_optimized(String8 config)
{
    bool result = string_equal(config, S8("Release")) || string_equal(config, S8("RelWithDebInfo")) || string_equal(config, S8("MinSizeRel"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_argument_split_value(String8 argument, String8* option, String8* value)
{
    bool result = false;
    u64 equal_index = string_first_code_unit(argument, '=');
    if (equal_index != BUSTER_STRING_NO_MATCH)
    {
        *option = string_slice(argument, 0, equal_index);
        *value = string_slice(argument, equal_index + 1, argument.length);
        result = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL char8 ascii_to_lower(char8 c)
{
    char8 result = (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
    return result;
}

BUSTER_GLOBAL_LOCAL bool string_equal_ascii_case_insensitive(String8 a, String8 b)
{
    bool result = a.length == b.length;
    for (u64 i = 0; result && i < a.length; i += 1)
    {
        result = ascii_to_lower(a.pointer[i]) == ascii_to_lower(b.pointer[i]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool cmake_bool_parse(String8 value, bool* parsed)
{
    bool result = true;

    if (string_equal_ascii_case_insensitive(value, S8("ON")) || string_equal_ascii_case_insensitive(value, S8("TRUE")) || string_equal_ascii_case_insensitive(value, S8("YES")) || string_equal(value, S8("1")))
    {
        *parsed = true;
    }
    else if (string_equal_ascii_case_insensitive(value, S8("OFF")) || string_equal_ascii_case_insensitive(value, S8("FALSE")) || string_equal_ascii_case_insensitive(value, S8("NO")) || string_equal(value, S8("0")))
    {
        *parsed = false;
    }
    else
    {
        result = false;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool build_argument_read_required_value(SliceString8 arguments, u64* argument_i, bool has_inline_value, String8 inline_value, String8* value)
{
    bool result = false;
    if (has_inline_value)
    {
        *value = inline_value;
        *argument_i += 1;
        result = true;
    }
    else if (*argument_i + 1 < arguments.length)
    {
        *argument_i += 1;
        *value = arguments.pointer[*argument_i];
        *argument_i += 1;
        result = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_argument_read_optional_bool(SliceString8 arguments, u64* argument_i, bool has_inline_value, String8 inline_value, bool* value)
{
    bool result = true;
    if (has_inline_value)
    {
        result = cmake_bool_parse(inline_value, value);
        *argument_i += 1;
    }
    else if (*argument_i + 1 < arguments.length && cmake_bool_parse(arguments.pointer[*argument_i + 1], value))
    {
        *argument_i += 2;
    }
    else
    {
        *value = true;
        *argument_i += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool environment_flag_is_on(String8 name)
{
    String8 value = os_get_environment_variable(name);
    bool parsed = false;
    bool result = value.pointer && cmake_bool_parse(value, &parsed) && parsed;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 environment_positive_u64_or(String8 name, u64 fallback)
{
    u64 result = fallback;
    String8 value = os_get_environment_variable(name);
    if (value.pointer)
    {
        IntegerParsingU64 parsed = string8_parse_u64_decimal(value.pointer);
        if (parsed.length == value.length && parsed.value > 0)
        {
            result = parsed.value;
        }
    }
    return result;
}

typedef struct Generate Generate;
struct Generate
{
    String8 build_directory;
    String8 config;
    String8 cc;
    String8 linker;
    String8 cmake_profile;
    SliceString8 cmake_arguments;
    u64 cmake_profile_summary_limit;
    BuildCompiler compiler;
    u32 fuzz:1;
    u32 sanitize:1;
    u32 ci:1;
    u32 optimize:1;
    u32 link_libc:1;
    u32 time_trace:1;
    u32 instrument:1;
    u32 lto:1;
    u32 include_tests:1;
    u32 check_optional_warnings:1;
    u32 developer_targets:1;
    u32 profile_cmake:1;
    u32 cc_set:1;
    u32 linker_set:1;
    u32 config_set:1;
    u32 optimize_set:1;
    u32 cmake_profile_set:1;
    u32 cmake_profile_summary:1;
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

BUSTER_GLOBAL_LOCAL bool path_is_separator(char8 c)
{
    bool result = c == '/' || c == '\\';
    return result;
}

BUSTER_GLOBAL_LOCAL bool path_is_absolute(String8 path)
{
    bool result = false;
    if (path.length)
    {
        result = path_is_separator(path.pointer[0]);
#if BUSTER_WINDOWS
        result = result || (path.length >= 3 && path.pointer[1] == ':' && path_is_separator(path.pointer[2]));
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 path_join(Arena* arena, String8 left, String8 right)
{
    String8 separator = S8("/");
    String8 parts[] = {
        left,
        (left.length && !path_is_separator(left.pointer[left.length - 1])) ? separator : S8(""),
        right,
    };
    String8 result = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 path_parent(Arena* arena, String8 path)
{
    BUSTER_UNUSED(arena);
    String8 result = {0};
    for (u64 i = path.length; i > 0; i -= 1)
    {
        if (path_is_separator(path.pointer[i - 1]))
        {
            result = string_slice(path, 0, i - 1);
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 build_relative_path(Arena* arena, String8 build_directory, String8 path)
{
    String8 result = path_is_absolute(path) ? path : path_join(arena, build_directory, path);
    return result;
}

BUSTER_GLOBAL_LOCAL bool path_exists(Arena* arena, String8 path)
{
    String8 path_z = string_duplicate_arena(arena, path, true);
    OsFileDescriptor* fd = os_file_open(path_z, (OpenFlags){ .read = 1 }, (OpenPermissions){ .read = 1 });
    bool result = fd != 0;
    if (fd)
    {
        os_file_close(fd);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void make_directory_recursive(Arena* arena, String8 path)
{
    if (!path.length)
    {
        return;
    }

    u64 start = 0;
#if BUSTER_WINDOWS
    if (path.length >= 2 && path.pointer[1] == ':')
    {
        start = 2;
    }
#endif

    for (u64 i = start; i <= path.length; i += 1)
    {
        if (i == path.length || path_is_separator(path.pointer[i]))
        {
            if (i > start)
            {
                String8 part = string_duplicate_arena(arena, string_slice(path, 0, i), true);
                os_make_directory(part);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void remove_path_recursive(Arena* arena, String8 path)
{
    String8 path_z = string_duplicate_arena(arena, path, true);
#if BUSTER_WINDOWS
    TemporalArena temp = scratch_begin(&arena, 1);
    String16 path_w = string16_from_string8(temp.arena, path_z, true);
    DWORD attributes = GetFileAttributesW(path_w.pointer);
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            String8 pattern = path_join(temp.arena, path_z, S8("*"));
            String16 pattern_w = string16_from_string8(temp.arena, pattern, true);
            WIN32_FIND_DATAW find_data;
            HANDLE find = FindFirstFileW(pattern_w.pointer, &find_data);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    String8 name = string8_from_string16(temp.arena, (String16){ .pointer = find_data.cFileName, .length = string16_length(find_data.cFileName) }, true);
                    if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
                    {
                        remove_path_recursive(arena, path_join(temp.arena, path_z, name));
                    }
                } while (FindNextFileW(find, &find_data));
                FindClose(find);
            }
            RemoveDirectoryW(path_w.pointer);
        }
        else
        {
            DeleteFileW(path_w.pointer);
        }
    }
    scratch_end(temp);
#else
    struct stat st;
    if (lstat((const char*)path_z.pointer, &st) == 0)
    {
        if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
        {
            DIR* directory = opendir((const char*)path_z.pointer);
            if (directory)
            {
                struct dirent* entry;
                while ((entry = readdir(directory)) != 0)
                {
                    String8 name = string_from_pointer((char8*)entry->d_name);
                    if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
                    {
                        remove_path_recursive(arena, path_join(arena, path_z, name));
                    }
                }
                closedir(directory);
            }
            rmdir((const char*)path_z.pointer);
        }
        else
        {
            unlink((const char*)path_z.pointer);
        }
    }
#endif
}

BUSTER_GLOBAL_LOCAL String8 generate_cc(Arena* arena, Generate generate)
{
    BUSTER_UNUSED(arena);
    String8 result = generate.cc_set ? generate.cc : build_compilers[generate.compiler];
    if (string_equal(result, S8("zig cc")) || string_equal(result, S8("zig;cc")))
    {
        result = S8("zig");
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool generate_cc_contains(Generate generate, String8 cc, String8 needle)
{
    BUSTER_UNUSED(generate);
    bool result = string_first_sequence(cc, needle) != BUSTER_STRING_NO_MATCH;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 generate_linker(Generate generate, String8 cc)
{
    String8 result = generate.linker;
    if (!result.pointer)
    {
        if (BUSTER_LINUX && !generate_cc_contains(generate, cc, S8("zig")) && !generate_cc_contains(generate, cc, S8("tcc")))
        {
            result = S8("MOLD");
        }
        else
        {
            result = S8("DEFAULT");
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 generate_config(Generate generate)
{
    String8 result = generate.config.pointer ? generate.config : (generate.optimize_set && generate.optimize ? S8("Release") : S8("Debug"));
    return result;
}

BUSTER_GLOBAL_LOCAL void generate_add(Arena* arena, BuildStep* step, Generate generate)
{
    remove_path_recursive(arena, generate.build_directory);
    make_directory_recursive(arena, generate.build_directory);
    if (generate.cmake_profile_set)
    {
        make_directory_recursive(arena, path_parent(arena, generate.cmake_profile));
    }

    String8 cc_command = generate_cc(arena, generate);
    String8 build_config = generate_config(generate);
    String8 linker = generate_linker(generate, cc_command);
    String8 c_compiler = cc_command;
    if (generate_cc_contains(generate, cc_command, S8("zig")))
    {
        String8 parts[] = { cc_command, S8(";cc") };
        c_compiler = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true);
    }

    string_print(S8("BUSTER_BUILD_DIRECTORY: {S8}\n"), generate.build_directory);
    string_print(S8("BUSTER_BUILD_CONFIG: {S8}\n"), build_config);
    string_print(S8("BUSTER_CC: {S8}\n"), cc_command);
    string_print(S8("BUSTER_LINKER: {S8}\n"), linker);
    if (generate.cmake_profile_set)
    {
        string_print(S8("BUSTER_CMAKE_PROFILE: {S8}\n"), generate.cmake_profile);
    }

    String8 cc = cmake_string(arena, S8("CMAKE_C_COMPILER"), c_compiler);
    String8 ci = cmake_flag(arena, S8("BUSTER_CI"), generate.ci);
    String8 optimize = cmake_flag(arena, S8("BUSTER_OPTIMIZE"), generate.optimize);
    String8 lto = cmake_flag(arena, S8("BUSTER_LTO"), generate.lto);
    String8 time_trace = cmake_flag(arena, S8("BUSTER_TIME_TRACE"), generate.time_trace);
    String8 instrument = cmake_flag(arena, S8("BUSTER_INSTRUMENT"), generate.instrument);
    String8 fuzz = cmake_flag(arena, S8("BUSTER_FUZZ"), generate.fuzz);
    String8 sanitize = cmake_flag(arena, S8("BUSTER_SANITIZE"), generate.sanitize);
    String8 include_tests = cmake_flag(arena, S8("BUSTER_INCLUDE_TESTS"), generate.include_tests);
    String8 link_libc = cmake_flag(arena, S8("BUSTER_LINK_LIBC"), generate.link_libc);
    String8 check_optional_warnings = cmake_flag(arena, S8("BUSTER_CHECK_OPTIONAL_WARNINGS"), generate.check_optional_warnings);
    String8 developer_targets = cmake_flag(arena, S8("BUSTER_DEVELOPER_TARGETS"), generate.developer_targets);
    String8 linker_argument = cmake_string(arena, S8("CMAKE_LINKER_TYPE"), linker);
    String8 default_build_type_argument = cmake_string(arena, S8("CMAKE_DEFAULT_BUILD_TYPE"), build_config);
    String8 profiling_output_argument = {0};
    if (generate.cmake_profile_set)
    {
        String8 profile_parts[] = {
            S8("--profiling-output="),
            generate.cmake_profile,
        };
        profiling_output_argument = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(profile_parts), true);
    }
    String8 timing_configuration = string_format(arena, S8(" (cc={S8}, fuzz={S8}, sanitize={S8})"), cc_command, generate.fuzz ? S8("ON") : S8("OFF"), generate.sanitize ? S8("ON") : S8("OFF"));

    GenericRun r = generic_tool_run_add_start(arena, step, &cmake_path, S8("cmake"));
    OsArgumentBuilder* b = &r.builder;
    os_argument_builder_append(b, S8("--warn-uninitialized"));
    os_argument_builder_append(b, S8("-Werror=dev"));
    os_argument_builder_append(b, S8("-B"));
    os_argument_builder_append(b, generate.build_directory);
    os_argument_builder_append(b, ci);
    os_argument_builder_append(b, cc);
    os_argument_builder_append(b, fuzz);
    if (generate.optimize_set)
    {
        os_argument_builder_append(b, optimize);
    }
    os_argument_builder_append(b, sanitize);
    os_argument_builder_append(b, linker_argument);

    os_argument_builder_append(b, lto);
    os_argument_builder_append(b, time_trace);
    os_argument_builder_append(b, instrument);
    os_argument_builder_append(b, include_tests);
    os_argument_builder_append(b, link_libc);
    os_argument_builder_append(b, check_optional_warnings);
    os_argument_builder_append(b, developer_targets);

    if (generate_cc_contains(generate, cc_command, S8("zig")))
    {
        os_argument_builder_append(b, S8("-DCMAKE_C_LINKER_DEPFILE_SUPPORTED=FALSE"));
        os_argument_builder_append(b, S8("-DCMAKE_C_LINK_DEPENDS_USE_LINKER=FALSE"));
    }

    if (generate_cc_contains(generate, cc_command, S8("clang")) && !generate.fuzz && !generate.sanitize)
    {
        os_argument_builder_append(b, S8("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"));
    }

    os_argument_builder_append(b, S8("-G"));
    os_argument_builder_append(b, S8("Ninja Multi-Config"));
    os_argument_builder_append(b, default_build_type_argument);
    os_argument_builder_append(b, S8("-DCMAKE_CONFIGURATION_TYPES=Debug;Release;RelWithDebInfo;MinSizeRel"));

    if (generate.cmake_profile_set)
    {
        os_argument_builder_append(b, S8("--profiling-format=google-trace"));
        os_argument_builder_append(b, profiling_output_argument);
    }

    for (u64 i = 0; i < generate.cmake_arguments.length; i += 1)
    {
        os_argument_builder_append(b, generate.cmake_arguments.pointer[i]);
    }

    SliceString8 arguments = os_argument_builder_flush(&r.builder);
    string_print(S8("+ {[]S8}\n"), arguments);
    *r.run = (ProcessRun) {
        .arguments = arguments,
        .timing_description = S8("CMake generation"),
        .timing_configuration = timing_configuration,
        .spawn_options = (ProcessSpawnOptions){
            .use_process_environment = 1,
        },
    };
}

typedef struct CmakeBuildOptions CmakeBuildOptions;
struct CmakeBuildOptions
{
    String8 config;
    u32 optimize:1;
    u32 optimize_set:1;
    u32 quiet:1;
    u32 verbose:1;
};

BUSTER_GLOBAL_LOCAL String8 cmake_build_config(CmakeBuildOptions options)
{
    String8 result = options.config.pointer ? options.config : (options.optimize ? S8("Release") : S8("Debug"));
    return result;
}

BUSTER_GLOBAL_LOCAL void build_add(Arena* arena, String8 build_directory, SliceString8 targets, SliceString8 native_arguments, CmakeBuildOptions options)
{
    BuildStep* step = step_add(arena);
    GenericRun r = generic_tool_run_add_start(arena, step, &cmake_path, S8("cmake"));
    OsArgumentBuilder* b = &r.builder;
    os_argument_builder_append(b, S8("--build"));
    os_argument_builder_append(b, build_directory);
    os_argument_builder_append(b, S8("--config"));
    os_argument_builder_append(b, cmake_build_config(options));

    if (targets.length)
    {
        os_argument_builder_append(b, S8("--target"));
        for (u64 i = 0; i < targets.length; i += 1)
        {
            os_argument_builder_append(b, targets.pointer[i]);
        }
    }

    if (options.verbose)
    {
        os_argument_builder_append(b, S8("--verbose"));
    }

    bool native_quiet = false;
    for (u64 i = 0; i < native_arguments.length; i += 1)
    {
        native_quiet |= string_equal(native_arguments.pointer[i], S8("--quiet"));
    }

    if (native_arguments.length || options.quiet)
    {
        os_argument_builder_append(b, S8("--"));
        for (u64 i = 0; i < native_arguments.length; i += 1)
        {
            os_argument_builder_append(b, native_arguments.pointer[i]);
        }

        if (options.quiet && !native_quiet)
        {
            os_argument_builder_append(b, S8("--quiet"));
        }
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

typedef struct ClangAnalyzeSummary ClangAnalyzeSummary;
struct ClangAnalyzeSummary
{
    u64 analyzed;
    u64 warnings;
    u64 failures;
};

BUSTER_GLOBAL_LOCAL ClangAnalyzeSummary clang_analyze_summary = {0};

typedef struct CmakeProfileSummaryOptions CmakeProfileSummaryOptions;
struct CmakeProfileSummaryOptions
{
    String8 profile;
    u64 limit;
    u32 profile_set:1;
};

BUSTER_GLOBAL_LOCAL CmakeProfileSummaryOptions pending_cmake_profile_summary_options = {0};
BUSTER_GLOBAL_LOCAL bool pending_cmake_profile_summary = false;

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

BUSTER_GLOBAL_LOCAL ProcessResult clang_analyze_add(Arena* arena, ClangAnalyzeOptions options)
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

    BuildStep* step = 0;
    u64 scheduled = 0;
    u64 setup_failures = 0;
    clang_analyze_summary = (ClangAnalyzeSummary){0};

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
                setup_failures += 1;
            }
            else if (clang_analyze_entry_matches_config(arena, entry, compile_arguments, options.config))
            {
                SliceString8 command = clang_analyzer_command(arena, compile_arguments, options.clang);
                if (!step)
                {
                    step = step_add(arena);
                }

                ProcessRun* run = run_add(arena, step);
                u32 flags = PROCESS_RUN_FLAG_PRINT_CAPTURED_ERROR | PROCESS_RUN_FLAG_STDERR_WARNING_IS_FAILURE | PROCESS_RUN_FLAG_CLANG_ANALYZE;
                if (options.quiet)
                {
                    flags |= PROCESS_RUN_FLAG_PRINT_COMMAND_ON_FAILURE_OR_WARNING;
                }
                else
                {
                    flags |= PROCESS_RUN_FLAG_PRINT_COMMAND;
                }

                *run = (ProcessRun){
                    .arguments = command,
                    .working_directory = entry.directory,
                    .flags = flags,
                    .spawn_options = (ProcessSpawnOptions){
                        .capture = ((u64)1 << STANDARD_STREAM_ERROR),
                        .use_process_environment = 1,
                    },
                };
                scheduled += 1;
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

    clang_analyze_summary.failures += setup_failures;
    if (scheduled == 0)
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

    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_command_add(Arena* arena, String8 build_directory, CmakeBuildOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    String8 self = program_state->input.arguments.length ? program_state->input.arguments.pointer[0] : S8("build/build");
    os_argument_builder_append(&builder, self);
    os_argument_builder_append(&builder, S8("clang_analyze"));
    os_argument_builder_append(&builder, build_directory);
    os_argument_builder_append(&builder, S8("--config"));
    os_argument_builder_append(&builder, cmake_build_config(options));
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

BUSTER_GLOBAL_LOCAL void cmake_profile_summary_add(Arena* arena, BuildStep* step, String8 profile, u64 limit)
{
    String8 limit_string = string_format(arena, S8("{u64}"), limit);
    ProcessRun* run = run_add(arena, step);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    String8 self = program_state->input.arguments.length ? program_state->input.arguments.pointer[0] : S8("build/build");
    os_argument_builder_append(&builder, self);
    os_argument_builder_append(&builder, S8("cmake_profile_summary"));
    os_argument_builder_append(&builder, profile);
    os_argument_builder_append(&builder, S8("--limit"));
    os_argument_builder_append(&builder, limit_string);

    SliceString8 arguments = os_argument_builder_flush(&builder);
    string_print(S8("+ {[]S8}\n"), arguments);
    *run = (ProcessRun) {
        .arguments = arguments,
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

BUSTER_GLOBAL_LOCAL ProcessResult cmake_profile_summary_action(Arena* arena, void* data)
{
    CmakeProfileSummaryOptions* options = (CmakeProfileSummaryOptions*)data;
    ProcessResult result = cmake_profile_summary_run(arena, *options);
    return result;
}

BUSTER_GLOBAL_LOCAL void cmake_profile_summary_action_add(Arena* arena, CmakeProfileSummaryOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    CmakeProfileSummaryOptions* options_copy = arena_allocate(arena, CmakeProfileSummaryOptions, 1);
    *options_copy = options;

    *run = (ProcessRun){
        .callback = cmake_profile_summary_action,
        .callback_data = options_copy,
    };
}

// --- ninja_log_summary ---------------------------------------------------
// Diagnostic only: reports which build edges (translation units) in a
// `.ninja_log` took the longest. Only meaningful for multi-TU (Debug)
// builds -- optimized configs compile as a single unity TU, so there's
// nothing to break down there.

typedef struct NinjaLogSummaryOptions NinjaLogSummaryOptions;
struct NinjaLogSummaryOptions
{
    String8 build_directory;
    u64 limit;
    u32 build_directory_set:1;
};

typedef struct NinjaLogRow NinjaLogRow;
struct NinjaLogRow
{
    String8 output;
    s64 duration_ms;
};

typedef struct NinjaLogRowNode NinjaLogRowNode;
struct NinjaLogRowNode
{
    NinjaLogRow row;
    NinjaLogRowNode* next;
};

typedef struct NinjaLogRowList NinjaLogRowList;
struct NinjaLogRowList
{
    NinjaLogRowNode* first;
    NinjaLogRowNode* last;
    u64 count;
};

// Splits one tab-delimited field off the front of `line`. Returns false
// only when `line` is already empty (i.e. no more fields on this line).
BUSTER_GLOBAL_LOCAL bool ninja_log_next_field(String8* line, String8* field)
{
    if (!line->length)
    {
        return false;
    }

    u64 tab_index = string_first_code_unit(*line, '\t');
    bool is_last = tab_index == BUSTER_STRING_NO_MATCH;
    u64 field_end = is_last ? line->length : tab_index;

    *field = string_slice(*line, 0, field_end);
    *line = is_last ? (String8){0} : string_slice(*line, field_end + 1, line->length);
    return true;
}

// A target can appear more than once across a log's history (rebuilds);
// keep only the most recent entry per output path.
BUSTER_GLOBAL_LOCAL void ninja_log_row_list_upsert(Arena* arena, NinjaLogRowList* list, NinjaLogRow row)
{
    for (NinjaLogRowNode* node = list->first; node; node = node->next)
    {
        if (string_equal(node->row.output, row.output))
        {
            node->row = row;
            return;
        }
    }

    NinjaLogRowNode* node = arena_allocate(arena, NinjaLogRowNode, 1);
    *node = (NinjaLogRowNode){ .row = row };

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

BUSTER_GLOBAL_LOCAL int ninja_log_row_compare(const void* a, const void* b)
{
    const NinjaLogRow* left = (const NinjaLogRow*)a;
    const NinjaLogRow* right = (const NinjaLogRow*)b;
    return (left->duration_ms < right->duration_ms) - (left->duration_ms > right->duration_ms);
}

BUSTER_GLOBAL_LOCAL ProcessResult ninja_log_summary_run(Arena* arena, NinjaLogSummaryOptions options)
{
    if (!options.limit)
    {
        options.limit = 25;
    }

    if (!options.build_directory.pointer || !options.build_directory.length)
    {
        string_print(S8("error: ninja_log_summary requires a build directory\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 log_path = path_join(arena, options.build_directory, S8(".ninja_log"));
    ByteSlice bytes = file_read(arena, log_path, (FileReadOptions){ .end_padding = 1 });
    if (!bytes.pointer)
    {
        string_print(S8("error: failed to read {S8}\n"), log_path);
        return PROCESS_RESULT_FAILED;
    }

    String8 text = BYTE_SLICE_TO_STRING(8, bytes);
    NinjaLogRowList row_list = {0};

    while (text.length)
    {
        u64 newline_index = string_first_code_unit(text, '\n');
        bool is_last_line = newline_index == BUSTER_STRING_NO_MATCH;
        u64 line_end = is_last_line ? text.length : newline_index;
        String8 line = string_slice(text, 0, line_end);
        text = is_last_line ? (String8){0} : string_slice(text, line_end + 1, text.length);

        if (!line.length || line.pointer[0] == '#')
        {
            continue;
        }

        String8 start_field = {0};
        String8 end_field = {0};
        String8 restat_field = {0};
        String8 output_field = {0};

        if (!ninja_log_next_field(&line, &start_field) ||
            !ninja_log_next_field(&line, &end_field) ||
            !ninja_log_next_field(&line, &restat_field) ||
            !ninja_log_next_field(&line, &output_field))
        {
            continue;
        }

        IntegerParsingU64 start_ms = string8_parse_u64_decimal(start_field.pointer);
        IntegerParsingU64 end_ms = string8_parse_u64_decimal(end_field.pointer);
        if (!start_ms.length || !end_ms.length)
        {
            continue;
        }

        ninja_log_row_list_upsert(arena, &row_list, (NinjaLogRow){
            .output = output_field,
            .duration_ms = (s64)end_ms.value - (s64)start_ms.value,
        });
    }

    if (!row_list.count)
    {
        string_print(S8("No ninja log entries found in {S8}.\n"), log_path);
        return PROCESS_RESULT_SUCCESS;
    }

    NinjaLogRow* rows = arena_allocate(arena, NinjaLogRow, row_list.count);
    u64 row_i = 0;
    for (NinjaLogRowNode* node = row_list.first; node; node = node->next)
    {
        rows[row_i++] = node->row;
    }
    qsort(rows, row_list.count, sizeof(rows[0]), ninja_log_row_compare);

    string_print(S8("Slowest build edges in {S8}:\n"), log_path);
    u64 limit = BUSTER_MIN(options.limit, row_list.count);
    for (u64 i = 0; i < limit; i += 1)
    {
        NinjaLogRow row = rows[i];
        s64 duration_ms = row.duration_ms < 0 ? 0 : row.duration_ms;
        printf("%6lld ms  %.*s\n", (long long)duration_ms, string8_printf_length(row.output, UINT64_MAX), row.output.pointer ? row.output.pointer : "");
    }

    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult ninja_log_summary_action(Arena* arena, void* data)
{
    NinjaLogSummaryOptions* options = (NinjaLogSummaryOptions*)data;
    return ninja_log_summary_run(arena, *options);
}

BUSTER_GLOBAL_LOCAL void ninja_log_summary_action_add(Arena* arena, NinjaLogSummaryOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    NinjaLogSummaryOptions* options_copy = arena_allocate(arena, NinjaLogSummaryOptions, 1);
    *options_copy = options;

    *run = (ProcessRun){
        .callback = ninja_log_summary_action,
        .callback_data = options_copy,
    };
}

// --- time_trace_summary ---------------------------------------------------
// Diagnostic only: parses one or more clang -ftime-trace JSON files (see
// BUSTER_TIME_TRACE) and reports the slowest "Total *" rollups clang itself
// pre-aggregates (Total Frontend, Total Backend, Total InstantiateFunction,
// ...), summed across every file given. That directly answers "where did
// compile time go" without re-implementing clang's own per-include b/e
// event pairing (out of scope here).

typedef struct TimeTraceSummaryOptions TimeTraceSummaryOptions;
struct TimeTraceSummaryOptions
{
    SliceString8 paths;
    u64 limit;
};

typedef struct TimeTraceRow TimeTraceRow;
struct TimeTraceRow
{
    String8 name;
    s64 duration_us;
};

typedef struct TimeTraceRowNode TimeTraceRowNode;
struct TimeTraceRowNode
{
    TimeTraceRow row;
    TimeTraceRowNode* next;
};

typedef struct TimeTraceRowList TimeTraceRowList;
struct TimeTraceRowList
{
    TimeTraceRowNode* first;
    TimeTraceRowNode* last;
    u64 count;
};

BUSTER_GLOBAL_LOCAL void time_trace_row_list_add(Arena* arena, TimeTraceRowList* list, String8 name, s64 duration_us)
{
    for (TimeTraceRowNode* node = list->first; node; node = node->next)
    {
        if (string_equal(node->row.name, name))
        {
            node->row.duration_us += duration_us;
            return;
        }
    }

    TimeTraceRowNode* node = arena_allocate(arena, TimeTraceRowNode, 1);
    *node = (TimeTraceRowNode){ .row = { .name = name, .duration_us = duration_us } };

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

BUSTER_GLOBAL_LOCAL int time_trace_row_compare(const void* a, const void* b)
{
    const TimeTraceRow* left = (const TimeTraceRow*)a;
    const TimeTraceRow* right = (const TimeTraceRow*)b;
    return (left->duration_us < right->duration_us) - (left->duration_us > right->duration_us);
}

typedef struct TimeTraceEvent TimeTraceEvent;
struct TimeTraceEvent
{
    String8 name;
    s64 duration_us;
    char8 phase;
    u32 has_duration:1;
};

BUSTER_GLOBAL_LOCAL TimeTraceEvent time_trace_parse_event(Arena* arena, JsonParser* parser, bool* valid)
{
    TimeTraceEvent result = {0};

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

        if (string_equal(key, S8("ph")))
        {
            String8 phase = json_parse_string(arena, parser, valid);
            result.phase = phase.length ? phase.pointer[0] : 0;
        }
        else if (string_equal(key, S8("name")))
        {
            result.name = json_parse_string(arena, parser, valid);
        }
        else if (string_equal(key, S8("dur")))
        {
            result.duration_us = json_parse_s64(parser, valid);
            result.has_duration = 1;
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

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_parse_file(Arena* arena, String8 path, TimeTraceRowList* row_list)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){ .end_padding = 1 });
    if (!bytes.pointer)
    {
        string_print(S8("error: failed to read {S8}\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    JsonParser parser = { .text = { .pointer = (char8*)bytes.pointer, .length = bytes.length } };
    if (!json_consume(&parser, '{'))
    {
        string_print(S8("error: failed to parse {S8}: expected a JSON object\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    bool valid = true;
    for (;;)
    {
        json_skip_whitespace(&parser);
        if (json_consume(&parser, '}'))
        {
            break;
        }

        String8 key = json_parse_string(arena, &parser, &valid);
        if (!valid || !json_consume(&parser, ':'))
        {
            string_print(S8("error: failed to parse {S8}: expected an object key\n"), path);
            return PROCESS_RESULT_FAILED;
        }

        if (string_equal(key, S8("traceEvents")))
        {
            if (!json_consume(&parser, '['))
            {
                string_print(S8("error: failed to parse {S8}: expected traceEvents array\n"), path);
                return PROCESS_RESULT_FAILED;
            }

            for (;;)
            {
                json_skip_whitespace(&parser);
                if (json_consume(&parser, ']'))
                {
                    break;
                }

                TimeTraceEvent event = time_trace_parse_event(arena, &parser, &valid);
                if (!valid)
                {
                    string_print(S8("error: failed to parse {S8}: invalid trace event\n"), path);
                    return PROCESS_RESULT_FAILED;
                }

                if (event.phase == 'X' && event.has_duration && string_starts_with_sequence(event.name, S8("Total ")))
                {
                    time_trace_row_list_add(arena, row_list, event.name, event.duration_us);
                }

                if (json_consume(&parser, ','))
                {
                    continue;
                }
                if (json_consume(&parser, ']'))
                {
                    break;
                }

                string_print(S8("error: failed to parse {S8}: expected ',' or ']'\n"), path);
                return PROCESS_RESULT_FAILED;
            }
        }
        else
        {
            json_skip_value(arena, &parser, &valid);
            if (!valid)
            {
                string_print(S8("error: failed to parse {S8}: invalid value\n"), path);
                return PROCESS_RESULT_FAILED;
            }
        }

        if (json_consume(&parser, ','))
        {
            continue;
        }
        if (json_consume(&parser, '}'))
        {
            break;
        }

        string_print(S8("error: failed to parse {S8}: expected ',' or '}'\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_run(Arena* arena, TimeTraceSummaryOptions options)
{
    if (!options.limit)
    {
        options.limit = 25;
    }

    if (!options.paths.length)
    {
        string_print(S8("error: time_trace_summary requires at least one -ftime-trace JSON path\n"));
        return PROCESS_RESULT_FAILED;
    }

    TimeTraceRowList row_list = {0};
    for (u64 i = 0; i < options.paths.length; i += 1)
    {
        ProcessResult file_result = time_trace_summary_parse_file(arena, options.paths.pointer[i], &row_list);
        if (file_result != PROCESS_RESULT_SUCCESS)
        {
            return file_result;
        }
    }

    if (!row_list.count)
    {
        string_print(S8("No \"Total *\" time-trace entries found.\n"));
        return PROCESS_RESULT_SUCCESS;
    }

    TimeTraceRow* rows = arena_allocate(arena, TimeTraceRow, row_list.count);
    u64 row_i = 0;
    for (TimeTraceRowNode* node = row_list.first; node; node = node->next)
    {
        rows[row_i++] = node->row;
    }
    qsort(rows, row_list.count, sizeof(rows[0]), time_trace_row_compare);

    string_print(S8("Slowest compiler phases across {u64} time-trace file(s):\n"), options.paths.length);
    u64 limit = BUSTER_MIN(options.limit, row_list.count);
    for (u64 i = 0; i < limit; i += 1)
    {
        TimeTraceRow row = rows[i];
        s64 duration_ms = row.duration_us < 0 ? 0 : row.duration_us / 1000;
        printf("%6lld ms  %.*s\n", (long long)duration_ms, string8_printf_length(row.name, UINT64_MAX), row.name.pointer ? row.name.pointer : "");
    }

    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_action(Arena* arena, void* data)
{
    TimeTraceSummaryOptions* options = (TimeTraceSummaryOptions*)data;
    return time_trace_summary_run(arena, *options);
}

BUSTER_GLOBAL_LOCAL void time_trace_summary_action_add(Arena* arena, TimeTraceSummaryOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    TimeTraceSummaryOptions* options_copy = arena_allocate(arena, TimeTraceSummaryOptions, 1);
    *options_copy = options;

    *run = (ProcessRun){
        .callback = time_trace_summary_action,
        .callback_data = options_copy,
    };
}

BUSTER_GLOBAL_LOCAL void test_all(Arena* arena, bool ci, CmakeBuildOptions base_options)
{
    BuildStep* generate_step = step_add(arena);
    bool cmake_profile = environment_flag_is_on(S8("BUSTER_CMAKE_PROFILE"));
    u64 cmake_profile_summary_limit = environment_positive_u64_or(S8("BUSTER_CMAKE_PROFILE_SUMMARY_LIMIT"), 15);
    BuildStep* profile_summary_step = cmake_profile ? step_add(arena) : 0;
    String8 build_prefix = os_get_environment_variable(S8("BUSTER_BUILD_DIRECTORY_PREFIX"));
    if (!build_prefix.pointer || !build_prefix.length)
    {
        build_prefix = S8("build/build-");
    }

    BUSTER_GLOBAL_LOCAL String8 ci_cmake_arguments[] = {
        S8_INITIALIZER("-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"),
    };

    for (BuildCompiler compiler = !BUSTER_WINDOWS; compiler < BUILD_COMPILER_COUNT; compiler += 1)
    {
        if (ci && compiler == BUILD_COMPILER_TCC && (BUSTER_WINDOWS || BUSTER_APPLE))
        {
            continue;
        }

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
                        build_prefix,
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
                    String8 cmake_profile_path = path_join(arena, build_directory, S8("cmake-profile.json"));

                    Generate generate = {
                        .build_directory = build_directory,
                        .cmake_profile = cmake_profile_path,
                        .cmake_profile_summary_limit = cmake_profile_summary_limit,
                        .compiler = compiler,
                        .fuzz = fuzz,
                        .sanitize = sanitize,
                        .ci = ci,
                        .optimize = optimize,
                        .optimize_set = true,
                        .link_libc = true,
                        .time_trace = false,
                        .lto = false,
                        .include_tests = true,
                        .check_optional_warnings = false,
                        .developer_targets = false,
                        .profile_cmake = false,
                        .cmake_profile_set = cmake_profile,
                        .cmake_profile_summary = cmake_profile,
                        .cmake_arguments = ci ? (SliceString8)BUSTER_ARRAY_TO_SLICE(ci_cmake_arguments) : (SliceString8){0},
                    };

                    CmakeBuildOptions options = {
                        .optimize = optimize,
                        .quiet = base_options.quiet,
                    };

                    generate_add(arena, generate_step, generate);
                    if (cmake_profile)
                    {
                        cmake_profile_summary_add(arena, profile_summary_step, cmake_profile_path, cmake_profile_summary_limit);
                    }

                    build_add(arena, build_directory, (SliceString8){0}, (SliceString8){0}, options);

                    String8 test_targets[] = {
                        S8("test_all"),
                    };
                    build_add(arena, build_directory, (SliceString8)BUSTER_ARRAY_TO_SLICE(test_targets), (SliceString8){0}, options);

                    if (compiler == BUILD_COMPILER_CLANG && !sanitize && !fuzz)
                    {
                        clang_analyze_command_add(arena, build_directory, options);
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
        [BUILD_COMMAND_NINJA_LOG_SUMMARY] = S8_INITIALIZER("ninja_log_summary"),
        [BUILD_COMMAND_TIME_TRACE_SUMMARY] = S8_INITIALIZER("time_trace_summary"),
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
        .check_optional_warnings = true,
        .developer_targets = true,
        .cmake_profile_summary_limit = 25,
    };
    CmakeBuildOptions options = {0};
    ClangAnalyzeOptions clang_analyze_options = { .compile_commands = build_directory };
    CmakeProfileSummaryOptions cmake_profile_summary_options = { .limit = 25 };
    NinjaLogSummaryOptions ninja_log_summary_options = { .limit = 25 };
    TimeTraceSummaryOptions time_trace_summary_options = { .limit = 25 };
    String8List time_trace_summary_paths = {0};
    String8List generate_cmake_arguments = {0};
    String8List build_targets = {0};
    String8List native_arguments = {0};

    while (result == PROCESS_RESULT_SUCCESS && argument_i < arguments.length)
    {
        String8 argument = arguments.pointer[argument_i];
        String8 argument_option = argument;
        String8 argument_value = {0};
        bool argument_has_value = build_argument_split_value(argument, &argument_option, &argument_value);

        if ((command == BUILD_COMMAND_BUILD || command == BUILD_COMMAND_GENERATE) && string_equal(argument, S8("--")))
        {
            argument_i += 1;
            while (argument_i < arguments.length)
            {
                String8 passthrough_argument = arguments.pointer[argument_i];
                if (command == BUILD_COMMAND_BUILD)
                {
                    string8_list_push(arena, &native_arguments, passthrough_argument);
                    if (string_equal(passthrough_argument, S8("--quiet")))
                    {
                        options.quiet = 1;
                    }
                }
                else
                {
                    string8_list_push(arena, &generate_cmake_arguments, passthrough_argument);
                }
                argument_i += 1;
            }
            break;
        }

        BuildArgument build_argument = BUILD_ARGUMENT_COUNT;

        for (u64 i = 0; i < BUILD_ARGUMENT_COUNT; i += 1)
        {
            String8 candidate_argument = build_arguments[i];

            if (string_equal(argument_option, candidate_argument))
            {
                build_argument = (BuildArgument)i;
                break;
            }
        }

        switch (build_argument)
        {
            break; case BUILD_ARGUMENT_COUNT:
            {
                if (command == BUILD_COMMAND_BUILD && !string_starts_with_sequence(argument, S8("--")))
                {
                    string8_list_push(arena, &build_targets, argument);
                    argument_i += 1;
                }
                else if (command == BUILD_COMMAND_CLANG_ANALYZE && !clang_analyze_options.compile_commands_set && !string_starts_with_sequence(argument, S8("--")))
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
                else if (command == BUILD_COMMAND_NINJA_LOG_SUMMARY && !ninja_log_summary_options.build_directory_set && !string_starts_with_sequence(argument, S8("--")))
                {
                    ninja_log_summary_options.build_directory = argument;
                    ninja_log_summary_options.build_directory_set = 1;
                    argument_i += 1;
                }
                else if (command == BUILD_COMMAND_TIME_TRACE_SUMMARY && !string_starts_with_sequence(argument, S8("--")))
                {
                    string8_list_push(arena, &time_trace_summary_paths, argument);
                    argument_i += 1;
                }
                else if (command == BUILD_COMMAND_GENERATE)
                {
                    string8_list_push(arena, &generate_cmake_arguments, argument);
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
                        // The catch-all after the loop reports the offending
                        // argument for every failure path.
                        result = generic_argument_result;
                    }
                }
            }
            break; case BUILD_ARGUMENT_BUILD_DIRECTORY:
            case BUILD_ARGUMENT_BUILD_DIR:
            {
                String8 value = {0};
                if (build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
                {
                    build_directory = value;
                    generate.build_directory = build_directory;
                    if (!clang_analyze_options.compile_commands_set)
                    {
                        clang_analyze_options.compile_commands = build_directory;
                    }
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CC:
            {
                String8 value = {0};
                if (command == BUILD_COMMAND_GENERATE && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
                {
                    generate.cc = value;
                    generate.cc_set = true;

                    BuildCompiler compiler = BUILD_COMPILER_COUNT;
                    for (u64 i = 0; i < BUILD_COMPILER_COUNT; i += 1)
                    {
                        if (string_equal(value, build_compilers[i]))
                        {
                            compiler = (BuildCompiler)i;
                            break;
                        }
                    }

                    if (compiler != BUILD_COMPILER_COUNT)
                    {
                        generate.compiler = compiler;
                    }
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CLANG:
            {
                String8 value = {0};
                if (command == BUILD_COMMAND_CLANG_ANALYZE && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
                {
                    clang_analyze_options.clang = value;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CONFIG:
            case BUILD_ARGUMENT_CONFIGURATION:
            {
                String8 config = {0};
                if (build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &config))
                {
                    if (!build_config_is_valid(config))
                    {
                        string_print(S8("error: invalid configuration => \"{S8}\"\n"), config);
                        result = PROCESS_RESULT_FAILED;
                    }
                    else if (command == BUILD_COMMAND_BUILD)
                    {
                        options.config = config;
                    }
                    else if (command == BUILD_COMMAND_GENERATE)
                    {
                        generate.config = config;
                        generate.config_set = true;
                    }
                    else if (command == BUILD_COMMAND_CLANG_ANALYZE)
                    {
                        clang_analyze_options.config = config;
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
            break; case BUILD_ARGUMENT_LIMIT:
            {
                String8 candidate_limit = {0};
                bool is_summary_command = command == BUILD_COMMAND_CMAKE_PROFILE_SUMMARY
                    || command == BUILD_COMMAND_NINJA_LOG_SUMMARY
                    || command == BUILD_COMMAND_TIME_TRACE_SUMMARY;
                if (is_summary_command && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &candidate_limit))
                {
                    IntegerParsingU64 parsed_limit = string8_parse_u64_decimal(candidate_limit.pointer);
                    if (parsed_limit.length == candidate_limit.length && parsed_limit.value > 0)
                    {
                        switch (command)
                        {
                            break; case BUILD_COMMAND_CMAKE_PROFILE_SUMMARY: cmake_profile_summary_options.limit = parsed_limit.value;
                            break; case BUILD_COMMAND_NINJA_LOG_SUMMARY: ninja_log_summary_options.limit = parsed_limit.value;
                            break; case BUILD_COMMAND_TIME_TRACE_SUMMARY: time_trace_summary_options.limit = parsed_limit.value;
                            break; default: BUSTER_UNREACHABLE();
                        }
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
            break; case BUILD_ARGUMENT_TARGET:
            case BUILD_ARGUMENT_TARGET_SHORT:
            {
                String8 value = {0};
                if (command == BUILD_COMMAND_BUILD && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
                {
                    string8_list_push(arena, &build_targets, value);
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_LINKER:
            {
                String8 value = {0};
                if (command == BUILD_COMMAND_GENERATE && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
                {
                    generate.linker = value;
                    generate.linker_set = true;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CMAKE_PROFILE:
            {
                if (command == BUILD_COMMAND_GENERATE)
                {
                    if (argument_has_value)
                    {
                        generate.cmake_profile = argument_value;
                        argument_i += 1;
                    }
                    else if (argument_i + 1 < arguments.length && !string_starts_with_sequence(arguments.pointer[argument_i + 1], S8("--")))
                    {
                        argument_i += 1;
                        generate.cmake_profile = arguments.pointer[argument_i];
                        argument_i += 1;
                    }
                    else
                    {
                        generate.cmake_profile = S8("cmake-profile.json");
                        argument_i += 1;
                    }
                    generate.cmake_profile_set = true;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY:
            {
                if (command == BUILD_COMMAND_GENERATE && !argument_has_value)
                {
                    generate.cmake_profile_summary = true;
                    argument_i += 1;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY_LIMIT:
            {
                String8 candidate_limit = {0};
                if (command == BUILD_COMMAND_GENERATE && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &candidate_limit))
                {
                    IntegerParsingU64 parsed_limit = string8_parse_u64_decimal(candidate_limit.pointer);
                    if (parsed_limit.length == candidate_limit.length && parsed_limit.value > 0)
                    {
                        generate.cmake_profile_summary_limit = parsed_limit.value;
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
            break; case BUILD_ARGUMENT_CI:
            case BUILD_ARGUMENT_FUZZ:
            case BUILD_ARGUMENT_OPTIMIZE:
            case BUILD_ARGUMENT_SANITIZE:
            case BUILD_ARGUMENT_LTO:
            case BUILD_ARGUMENT_TIME_TRACE:
            case BUILD_ARGUMENT_INSTRUMENT:
            case BUILD_ARGUMENT_INCLUDE_TESTS:
            case BUILD_ARGUMENT_LINK_LIBC:
            case BUILD_ARGUMENT_CHECK_OPTIONAL_WARNINGS:
            case BUILD_ARGUMENT_DEVELOPER_TARGETS:
            {
                bool value = false;
                if (command == BUILD_COMMAND_GENERATE && build_argument_read_optional_bool(arguments, &argument_i, argument_has_value, argument_value, &value))
                {
                    switch (build_argument)
                    {
                        break; case BUILD_ARGUMENT_CI: generate.ci = value;
                        break; case BUILD_ARGUMENT_FUZZ: generate.fuzz = value;
                        break; case BUILD_ARGUMENT_OPTIMIZE: generate.optimize = value; generate.optimize_set = true;
                        break; case BUILD_ARGUMENT_SANITIZE: generate.sanitize = value;
                        break; case BUILD_ARGUMENT_LTO: generate.lto = value;
                        break; case BUILD_ARGUMENT_TIME_TRACE: generate.time_trace = value;
                        break; case BUILD_ARGUMENT_INSTRUMENT: generate.instrument = value;
                        break; case BUILD_ARGUMENT_INCLUDE_TESTS: generate.include_tests = value;
                        break; case BUILD_ARGUMENT_LINK_LIBC: generate.link_libc = value;
                        break; case BUILD_ARGUMENT_CHECK_OPTIONAL_WARNINGS: generate.check_optional_warnings = value;
                        break; case BUILD_ARGUMENT_DEVELOPER_TARGETS: generate.developer_targets = value;
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                else if (command == BUILD_COMMAND_BUILD && build_argument == BUILD_ARGUMENT_OPTIMIZE && build_argument_read_optional_bool(arguments, &argument_i, argument_has_value, argument_value, &value))
                {
                    options.optimize = value;
                    options.optimize_set = true;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_NO_CI:
            case BUILD_ARGUMENT_NO_FUZZ:
            case BUILD_ARGUMENT_NO_OPTIMIZE:
            case BUILD_ARGUMENT_NO_SANITIZE:
            case BUILD_ARGUMENT_NO_LTO:
            case BUILD_ARGUMENT_NO_TIME_TRACE:
            case BUILD_ARGUMENT_NO_INSTRUMENT:
            case BUILD_ARGUMENT_NO_INCLUDE_TESTS:
            case BUILD_ARGUMENT_NO_LINK_LIBC:
            case BUILD_ARGUMENT_NO_CHECK_OPTIONAL_WARNINGS:
            case BUILD_ARGUMENT_NO_DEVELOPER_TARGETS:
            {
                if (command == BUILD_COMMAND_GENERATE && !argument_has_value)
                {
                    switch (build_argument)
                    {
                        break; case BUILD_ARGUMENT_NO_CI: generate.ci = false;
                        break; case BUILD_ARGUMENT_NO_FUZZ: generate.fuzz = false;
                        break; case BUILD_ARGUMENT_NO_OPTIMIZE: generate.optimize = false; generate.optimize_set = true;
                        break; case BUILD_ARGUMENT_NO_SANITIZE: generate.sanitize = false;
                        break; case BUILD_ARGUMENT_NO_LTO: generate.lto = false;
                        break; case BUILD_ARGUMENT_NO_TIME_TRACE: generate.time_trace = false;
                        break; case BUILD_ARGUMENT_NO_INSTRUMENT: generate.instrument = false;
                        break; case BUILD_ARGUMENT_NO_INCLUDE_TESTS: generate.include_tests = false;
                        break; case BUILD_ARGUMENT_NO_LINK_LIBC: generate.link_libc = false;
                        break; case BUILD_ARGUMENT_NO_CHECK_OPTIONAL_WARNINGS: generate.check_optional_warnings = false;
                        break; case BUILD_ARGUMENT_NO_DEVELOPER_TARGETS: generate.developer_targets = false;
                        break; default: BUSTER_UNREACHABLE();
                    }
                    argument_i += 1;
                }
                else if (command == BUILD_COMMAND_BUILD && build_argument == BUILD_ARGUMENT_NO_OPTIMIZE && !argument_has_value)
                {
                    options.optimize = false;
                    options.optimize_set = true;
                    argument_i += 1;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            break; case BUILD_ARGUMENT_VERBOSE:
            case BUILD_ARGUMENT_VERBOSE_SHORT:
            {
                if (argument_has_value)
                {
                    ProcessResult generic_argument_result = buster_argument_process(argument_i);
                    if (generic_argument_result == PROCESS_RESULT_SUCCESS)
                    {
                        argument_i += 1;
                    }
                    else
                    {
                        result = generic_argument_result;
                    }
                }
                else if (command == BUILD_COMMAND_BUILD)
                {
                    options.verbose = 1;
                    argument_i += 1;
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

    if (result == PROCESS_RESULT_SUCCESS && command == BUILD_COMMAND_GENERATE)
    {
        String8 build_config = generate_config(generate);
        bool config_optimize = build_config_is_optimized(build_config);
        if (generate.optimize_set && generate.optimize != config_optimize)
        {
            fprintf(stderr,
                    "error: --optimize %s conflicts with --config %.*s\n",
                    generate.optimize ? "ON" : "OFF",
                    string8_printf_length(build_config, UINT64_MAX),
                    build_config.pointer ? build_config.pointer : "");
            result = PROCESS_RESULT_FAILED;
        }
        else
        {
            if (!generate.optimize_set)
            {
                generate.optimize = config_optimize;
            }

            if (generate.cmake_profile_summary && !generate.cmake_profile_set)
            {
                generate.cmake_profile = S8("cmake-profile.json");
                generate.cmake_profile_set = true;
            }

            if (generate.cmake_profile_set)
            {
                generate.cmake_profile = build_relative_path(arena, generate.build_directory, generate.cmake_profile);
                String8 profile_parent = path_parent(arena, generate.cmake_profile);
                make_directory_recursive(arena, profile_parent);
            }

            if (generate.cmake_profile_summary)
            {
                pending_cmake_profile_summary_options = (CmakeProfileSummaryOptions){
                    .profile = generate.cmake_profile,
                    .limit = generate.cmake_profile_summary_limit,
                    .profile_set = true,
                };
                pending_cmake_profile_summary = true;
            }
        }
    }

    // Every parse-failure path above leaves argument_i on the offending
    // argument; report it here so no failure exits silently with code 1.
    if (result != PROCESS_RESULT_SUCCESS)
    {
        if (argument_i < arguments.length)
        {
            string_print(S8("error: unknown or misplaced argument => \"{S8}\"\n"), arguments.pointer[argument_i]);
        }
        else
        {
            string_print(S8("error: invalid command-line arguments\n"));
        }
    }

    if (result == PROCESS_RESULT_SUCCESS && command == BUILD_COMMAND_BUILD && options.config.pointer && options.optimize_set)
    {
        bool config_optimize = build_config_is_optimized(options.config);
        if (options.optimize != config_optimize)
        {
            fprintf(stderr,
                    "error: --optimize %s conflicts with --config %.*s\n",
                    options.optimize ? "ON" : "OFF",
                    string8_printf_length(options.config, UINT64_MAX),
                    options.config.pointer ? options.config.pointer : "");
            result = PROCESS_RESULT_FAILED;
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
                generate.cmake_arguments = string8_list_to_slice(arena, generate_cmake_arguments);
                BuildStep* generate_step = step_add(arena);
                generate_add(arena, generate_step, generate);
            }
            break; case BUILD_COMMAND_BUILD:
            {
                build_add(arena, build_directory, string8_list_to_slice(arena, build_targets), string8_list_to_slice(arena, native_arguments), options);
            }
            break; case BUILD_COMMAND_CLANG_ANALYZE:
            {
                result = clang_analyze_add(arena, clang_analyze_options);
            }
            break; case BUILD_COMMAND_CMAKE_PROFILE_SUMMARY:
            {
                cmake_profile_summary_action_add(arena, cmake_profile_summary_options);
            }
            break; case BUILD_COMMAND_NINJA_LOG_SUMMARY:
            {
                ninja_log_summary_action_add(arena, ninja_log_summary_options);
            }
            break; case BUILD_COMMAND_TIME_TRACE_SUMMARY:
            {
                time_trace_summary_options.paths = string8_list_to_slice(arena, time_trace_summary_paths);
                time_trace_summary_action_add(arena, time_trace_summary_options);
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

BUSTER_GLOBAL_LOCAL ProcessSpawnResult process_run_spawn(Arena* arena, ProcessRun* run)
{
    if (run->flags & PROCESS_RUN_FLAG_PRINT_COMMAND)
    {
        command_print(run->arguments);
    }

    bool restore_directory = false;
#if BUSTER_WINDOWS
    char16 old_directory_buffer[BUSTER_KB(32) / sizeof(char16)];
    DWORD old_directory_length = 0;
    if (run->working_directory.pointer && run->working_directory.length)
    {
        old_directory_length = GetCurrentDirectoryW(BUSTER_ARRAY_LENGTH(old_directory_buffer), old_directory_buffer);
        if (old_directory_length > 0 && old_directory_length < BUSTER_ARRAY_LENGTH(old_directory_buffer))
        {
            TemporalArena temp = scratch_begin(&arena, 1);
            String16 directory16 = string16_from_string8(temp.arena, run->working_directory, true);
            restore_directory = SetCurrentDirectoryW(directory16.pointer) != 0;
            scratch_end(temp);
        }
    }
#else
    char old_directory_buffer[BUSTER_KB(32)];
    if (run->working_directory.pointer && run->working_directory.length && getcwd(old_directory_buffer, sizeof(old_directory_buffer)))
    {
        restore_directory = chdir(run->working_directory.pointer) == 0;
    }
#endif

    ProcessSpawnResult spawn = os_process_spawn(run->arguments, run->environment_keys, run->environment_values, run->spawn_options);

    if (restore_directory)
    {
#if BUSTER_WINDOWS
        SetCurrentDirectoryW(old_directory_buffer);
#else
        chdir(old_directory_buffer);
#endif
    }

    return spawn;
}

BUSTER_GLOBAL_LOCAL ProcessResult process_run_wait(Arena* arena, ProcessRun* run)
{
    ProcessWaitResult wait_result = os_process_wait_sync(arena, run->spawn);
    ProcessResult result = wait_result.result;
    bool has_warning = false;

    if (run->flags & (PROCESS_RUN_FLAG_PRINT_CAPTURED_ERROR | PROCESS_RUN_FLAG_STDERR_WARNING_IS_FAILURE | PROCESS_RUN_FLAG_CLANG_ANALYZE))
    {
        String8 error_output = { .pointer = (char8*)wait_result.streams[STANDARD_STREAM_ERROR].pointer, .length = wait_result.streams[STANDARD_STREAM_ERROR].length };
        has_warning = clang_analyze_output_has_warning(error_output);

        if (run->flags & PROCESS_RUN_FLAG_CLANG_ANALYZE)
        {
            clang_analyze_summary.analyzed += 1;
            clang_analyze_summary.failures += wait_result.result != PROCESS_RESULT_SUCCESS;
            clang_analyze_summary.warnings += has_warning;
        }

        if ((run->flags & PROCESS_RUN_FLAG_PRINT_COMMAND_ON_FAILURE_OR_WARNING) && (wait_result.result != PROCESS_RESULT_SUCCESS || has_warning))
        {
            command_print(run->arguments);
        }

        if ((run->flags & PROCESS_RUN_FLAG_PRINT_CAPTURED_ERROR) && error_output.length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(error_output));
        }
    }

    if ((run->flags & PROCESS_RUN_FLAG_STDERR_WARNING_IS_FAILURE) && has_warning && result == PROCESS_RESULT_SUCCESS)
    {
        result = PROCESS_RESULT_FAILED;
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
        u32 pending_count = 0;
        ProcessRun* first_pending = step->first_process;

        for (ProcessRun* run = step->first_process; run; run = run->next)
        {
            if (run->callback)
            {
                for (ProcessRun* wait = first_pending; wait != run; wait = wait->next)
                {
                    ProcessResult wait_result = process_run_wait(arena, wait);
                    if (wait->timing_description.pointer)
                    {
                        u64 elapsed_us = os_now_microseconds() - wait->start_us;
                        u64 elapsed_ns = elapsed_us * 1000;
                        u64 seconds_whole = elapsed_us / 1000000;
                        u64 seconds_fraction = elapsed_us % 1000000;
                        printf("%.*s%.*s took %llu.%06llu seconds (%llu nanoseconds)\n",
                               string8_printf_length(wait->timing_description, UINT64_MAX), wait->timing_description.pointer ? wait->timing_description.pointer : "",
                               string8_printf_length(wait->timing_configuration, UINT64_MAX), wait->timing_configuration.pointer ? wait->timing_configuration.pointer : "",
                               (unsigned long long)seconds_whole,
                               (unsigned long long)seconds_fraction,
                               (unsigned long long)elapsed_ns);
                    }

                    if (result == PROCESS_RESULT_SUCCESS && wait_result != PROCESS_RESULT_SUCCESS)
                    {
                        result = wait_result;
                    }
                }

                first_pending = run->next;
                pending_count = 0;

                if (result == PROCESS_RESULT_SUCCESS)
                {
                    ProcessResult callback_result = run->callback(arena, run->callback_data);
                    if (callback_result != PROCESS_RESULT_SUCCESS)
                    {
                        result = callback_result;
                    }
                }
            }
            else
            {
                run->start_us = os_now_microseconds();
                run->spawn = process_run_spawn(arena, run);
                pending_count += 1;

                if (pending_count == thread_count || !run->next)
                {
                    for (ProcessRun* wait = first_pending; wait != run->next; wait = wait->next)
                    {
                        ProcessResult wait_result = process_run_wait(arena, wait);
                        if (wait->timing_description.pointer)
                        {
                            u64 elapsed_us = os_now_microseconds() - wait->start_us;
                            u64 elapsed_ns = elapsed_us * 1000;
                            u64 seconds_whole = elapsed_us / 1000000;
                            u64 seconds_fraction = elapsed_us % 1000000;
                            printf("%.*s%.*s took %llu.%06llu seconds (%llu nanoseconds)\n",
                                   string8_printf_length(wait->timing_description, UINT64_MAX), wait->timing_description.pointer ? wait->timing_description.pointer : "",
                                   string8_printf_length(wait->timing_configuration, UINT64_MAX), wait->timing_configuration.pointer ? wait->timing_configuration.pointer : "",
                                   (unsigned long long)seconds_whole,
                                   (unsigned long long)seconds_fraction,
                                   (unsigned long long)elapsed_ns);
                        }

                        if (result == PROCESS_RESULT_SUCCESS && wait_result != PROCESS_RESULT_SUCCESS)
                        {
                            result = wait_result;
                        }
                    }

                    first_pending = run->next;
                    pending_count = 0;
                }
            }
        }

        if (result != PROCESS_RESULT_SUCCESS && step == build_graph->first_step)
        {
            break;
        }
    }

    if (clang_analyze_summary.analyzed || clang_analyze_summary.failures || clang_analyze_summary.warnings)
    {
        string_print(S8("clang --analyze checked {u64} translation unit(s), {u64} with analyzer warning(s), {u64} failed.\n"), clang_analyze_summary.analyzed, clang_analyze_summary.warnings, clang_analyze_summary.failures);
        if (result == PROCESS_RESULT_SUCCESS && (clang_analyze_summary.failures || clang_analyze_summary.warnings))
        {
            result = PROCESS_RESULT_FAILED;
        }
    }

    if (result == PROCESS_RESULT_SUCCESS && pending_cmake_profile_summary)
    {
        if (!path_exists(arena, pending_cmake_profile_summary_options.profile))
        {
            fprintf(stderr, "warning: CMake profiling output was not produced\n");
        }
        else
        {
            String8 self = program_state->input.arguments.length ? program_state->input.arguments.pointer[0] : S8("build/build");
            string_print(S8("+ {S8} cmake_profile_summary {S8} --limit {u64}\n"), self, pending_cmake_profile_summary_options.profile, pending_cmake_profile_summary_options.limit);
            result = cmake_profile_summary_run(arena, pending_cmake_profile_summary_options);
        }
    }

    return result;
}
