#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#include <buster/lib/base.h>
#include <buster/lib/os.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>
#include <buster/lib/target.h>
#include <stdio.h>
#if BUSTER_LINUX || BUSTER_APPLE
#include <dirent.h>
#endif

#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/arena.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/integer.c>
#include <buster/lib/entry_point.c>
#include <buster/lib/target.c>

typedef enum BuildCommand
{
    BUILD_COMMAND_NONE,
    BUILD_COMMAND_GENERATE,
    BUILD_COMMAND_BUILD,
    BUILD_COMMAND_CLANG_ANALYZE,
    BUILD_COMMAND_CMAKE_PROFILE_SUMMARY,
    BUILD_COMMAND_NINJA_LOG_SUMMARY,
    BUILD_COMMAND_TIME_TRACE_SUMMARY,
    BUILD_COMMAND_TIME_TRACE_SUMMARY_SELF_TEST,
    BUILD_COMMAND_TEST_TIMING_SUMMARY,
    BUILD_COMMAND_TEST_TIMING_SUMMARY_SELF_TEST,
    BUILD_COMMAND_IMPORT_ASSEMBLY_METADATA,
    BUILD_COMMAND_TEST_SELF_HOST,
    BUILD_COMMAND_SELF_HOST_FROM_EXISTING,
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
    BuildActionCallback* cleanup_callback;
    void* cleanup_data;
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
    BUILD_ARGUMENT_PROVENANCE_RECORD,
    BUILD_ARGUMENT_AUDIT,
    BUILD_ARGUMENT_BASELINE,
    BUILD_ARGUMENT_UPDATE_BASELINE,
    BUILD_ARGUMENT_NO_UPDATE_BASELINE,
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
    [BUILD_ARGUMENT_PROVENANCE_RECORD] = S8_INITIALIZER("--provenance-record"),
    [BUILD_ARGUMENT_AUDIT] = S8_INITIALIZER("--audit"),
    [BUILD_ARGUMENT_BASELINE] = S8_INITIALIZER("--baseline"),
    [BUILD_ARGUMENT_UPDATE_BASELINE] = S8_INITIALIZER("--update-baseline"),
    [BUILD_ARGUMENT_NO_UPDATE_BASELINE] = S8_INITIALIZER("--no-update-baseline"),
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
    BUILD_COMPILER_ZIG,
    BUILD_COMPILER_COUNT,
} BuildCompiler;

BUSTER_GLOBAL_LOCAL String8 build_compilers[] = {
    [BUILD_COMPILER_CL] = S8_INITIALIZER("cl"), [BUILD_COMPILER_CLANG] = S8_INITIALIZER("clang"), [BUILD_COMPILER_GCC] = S8_INITIALIZER("gcc"),
    [BUILD_COMPILER_ZIG] = S8_INITIALIZER("zig"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(build_compilers) == BUILD_COMPILER_COUNT);

BUSTER_GLOBAL_LOCAL bool build_config_is_valid(String8 config)
{
    String8 configs[] = {
        S8("Debug"),
        S8("Release"),
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

BUSTER_GLOBAL_LOCAL bool build_compiler_value_is_tcc(String8 value)
{
    u64 semicolon = string_first_code_unit(value, ';');
    if (semicolon != BUSTER_STRING_NO_MATCH)
    {
        value = string_slice(value, 0, semicolon);
    }
    u64 basename_start = 0;
    for (u64 i = 0; i < value.length; i += 1)
    {
        if (value.pointer[i] == '/' || value.pointer[i] == '\\')
        {
            basename_start = i + 1;
        }
    }
    String8 basename = string_slice(value, basename_start, value.length);
    bool result = string_equal_ascii_case_insensitive(basename, S8("tcc")) || string_equal_ascii_case_insensitive(basename, S8("tcc.exe"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_cmake_definition_value(String8 argument, String8 key, String8* value)
{
    bool result = false;
    if (string_starts_with_sequence(argument, S8("-D")))
    {
        String8 definition = string_slice(argument, 2, argument.length);
        u64 equal_index = string_first_code_unit(definition, '=');
        if (equal_index != BUSTER_STRING_NO_MATCH)
        {
            String8 name = string_slice(definition, 0, equal_index);
            bool name_matches = string_equal(name, key) ||
                                (name.length > key.length && string_starts_with_sequence(name, key) && name.pointer[key.length] == ':');
            if (name_matches)
            {
                *value = string_slice(definition, equal_index + 1, definition.length);
                result = true;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_cmake_argument_is_tcc_application_compiler(Arena* arena, String8 argument, String8 split_definition)
{
    if (string_equal(argument, S8("-D")))
    {
        if (!split_definition.length)
        {
            return false;
        }
        argument = string_format(arena, S8("-D{S8}"), split_definition);
    }

    String8 value = {0};
    bool result = build_cmake_definition_value(argument, S8("CMAKE_C_COMPILER"), &value) && build_compiler_value_is_tcc(value);
    return result;
}

BUSTER_GLOBAL_LOCAL bool cmake_bool_parse(String8 value, bool* parsed)
{
    bool result = true;

    if (string_equal_ascii_case_insensitive(value, S8("ON")) || string_equal_ascii_case_insensitive(value, S8("TRUE")) ||
        string_equal_ascii_case_insensitive(value, S8("YES")) || string_equal(value, S8("1")))
    {
        *parsed = true;
    }
    else if (string_equal_ascii_case_insensitive(value, S8("OFF")) || string_equal_ascii_case_insensitive(value, S8("FALSE")) ||
             string_equal_ascii_case_insensitive(value, S8("NO")) || string_equal(value, S8("0")))
    {
        *parsed = false;
    }
    else
    {
        result = false;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool build_argument_read_required_value(SliceString8 arguments, u64* argument_i, bool has_inline_value, String8 inline_value,
                                                            String8* value)
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
    String8 configuration_types;
    String8 cc;
    String8 linker;
    String8 cmake_profile;
    SliceString8 cmake_arguments;
    u64 cmake_profile_summary_limit;
    BuildCompiler compiler;
    u32 fuzz_available : 1;
    u32 sanitize : 1;
    u32 ci : 1;
    u32 optimize : 1;
    u32 link_libc : 1;
    u32 time_trace : 1;
    u32 instrument : 1;
    u32 lto : 1;
    u32 include_tests : 1;
    u32 check_optional_warnings : 1;
    u32 developer_targets : 1;
    u32 profile_cmake : 1;
    u32 cc_set : 1;
    u32 linker_set : 1;
    u32 config_set : 1;
    u32 optimize_set : 1;
    u32 cmake_profile_set : 1;
    u32 cmake_profile_summary : 1;
    u32 cross_configs : 1;
};

BUSTER_GLOBAL_LOCAL String8 cmake_path = {0};

BUSTER_GLOBAL_LOCAL String8 cl_path = {0};
BUSTER_GLOBAL_LOCAL String8 clang_path = {0};
BUSTER_GLOBAL_LOCAL String8 gcc_path = {0};
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

    return (GenericRun){.builder = builder, .run = run};
}

BUSTER_GLOBAL_LOCAL void generic_tool_run_add_end(GenericRun r)
{
    SliceString8 arguments = os_argument_builder_flush(&r.builder);

    *r.run = (ProcessRun){
        .arguments = arguments,
        .spawn_options =
            (ProcessSpawnOptions){
                .use_process_environment = 1,
            },
    };
}

BUSTER_GLOBAL_LOCAL String8 cmake_cc(Arena* arena, BuildCompiler compiler)
{
    switch (compiler)
    {
        break;
    case BUILD_COMPILER_CL:
        return get_resolved_path(arena, &cl_path, S8("cl"));
        break;
    case BUILD_COMPILER_CLANG:
        return get_resolved_path(arena, &clang_path, S8("clang"));
        break;
    case BUILD_COMPILER_GCC:
        return get_resolved_path(arena, &gcc_path, S8("gcc"));
        break;
    case BUILD_COMPILER_ZIG:
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
    break;
    case BUILD_COMPILER_COUNT:
        return S8("");
        break;
    default:
        return S8("");
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
    OsFileDescriptor* fd = os_file_open(path_z, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
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
                    String8 name =
                        string8_from_string16(temp.arena, (String16){.pointer = find_data.cFileName, .length = string16_length(find_data.cFileName)}, true);
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
        if (BUSTER_LINUX && !generate_cc_contains(generate, cc, S8("zig")))
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
        String8 parts[] = {cc_command, S8(";cc")};
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
    String8 lto = cmake_flag(arena, S8("BUSTER_LTO"), generate.lto);
    String8 time_trace = cmake_flag(arena, S8("BUSTER_TIME_TRACE"), generate.time_trace);
    String8 instrument = cmake_flag(arena, S8("BUSTER_INSTRUMENT"), generate.instrument);
    String8 fuzz_available = cmake_flag(arena, S8("BUSTER_FUZZ_AVAILABLE"), generate.fuzz_available);
    String8 sanitize = cmake_flag(arena, S8("BUSTER_SANITIZE"), generate.sanitize);
    String8 include_tests = cmake_flag(arena, S8("BUSTER_INCLUDE_TESTS"), generate.include_tests);
    String8 link_libc = cmake_flag(arena, S8("BUSTER_LINK_LIBC"), generate.link_libc);
    String8 check_optional_warnings = cmake_flag(arena, S8("BUSTER_CHECK_OPTIONAL_WARNINGS"), generate.check_optional_warnings);
    String8 developer_targets = cmake_flag(arena, S8("BUSTER_DEVELOPER_TARGETS"), generate.developer_targets);
    String8 linker_argument = cmake_string(arena, S8("CMAKE_LINKER_TYPE"), linker);
    String8 default_build_type_argument = cmake_string(arena, S8("CMAKE_DEFAULT_BUILD_TYPE"), build_config);
    String8 configuration_types_argument = string_format(arena, S8("-DCMAKE_CONFIGURATION_TYPES={S8}"),
                                                         generate.configuration_types.pointer ? generate.configuration_types : S8("Debug;Release"));
    String8 profiling_output_argument = {0};
    if (generate.cmake_profile_set)
    {
        String8 profile_parts[] = {
            S8("--profiling-output="),
            generate.cmake_profile,
        };
        profiling_output_argument = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(profile_parts), true);
    }
    String8 timing_configuration = string_format(arena, S8(" (cc={S8}, fuzz_available={S8}, sanitize={S8})"), cc_command,
                                                 generate.fuzz_available ? S8("ON") : S8("OFF"), generate.sanitize ? S8("ON") : S8("OFF"));

    GenericRun r = generic_tool_run_add_start(arena, step, &cmake_path, S8("cmake"));
    OsArgumentBuilder* b = &r.builder;
    os_argument_builder_append(b, S8("--warn-uninitialized"));
    os_argument_builder_append(b, S8("-Werror=dev"));
    os_argument_builder_append(b, S8("-B"));
    os_argument_builder_append(b, generate.build_directory);
    os_argument_builder_append(b, ci);
    os_argument_builder_append(b, cc);
    os_argument_builder_append(b, fuzz_available);
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

    if (generate_cc_contains(generate, cc_command, S8("clang")) && !generate.sanitize)
    {
        os_argument_builder_append(b, S8("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"));
    }

    os_argument_builder_append(b, S8("-G"));
    os_argument_builder_append(b, S8("Ninja Multi-Config"));
    os_argument_builder_append(b, default_build_type_argument);
    os_argument_builder_append(b, configuration_types_argument);
    if (generate.cross_configs)
    {
        os_argument_builder_append(b, S8("-DCMAKE_CROSS_CONFIGS=all"));
    }

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
    *r.run = (ProcessRun){
        .arguments = arguments,
        .timing_description = S8("CMake generation"),
        .timing_configuration = timing_configuration,
        .spawn_options =
            (ProcessSpawnOptions){
                .use_process_environment = 1,
            },
    };
}

typedef struct CmakeBuildOptions CmakeBuildOptions;
struct CmakeBuildOptions
{
    String8 config;
    u32 parallel_jobs;
    u32 optimize : 1;
    u32 optimize_set : 1;
    u32 quiet : 1;
    u32 verbose : 1;
};

BUSTER_GLOBAL_LOCAL String8 cmake_build_config(CmakeBuildOptions options);

typedef struct BuildArtifactCompilerMetadata BuildArtifactCompilerMetadata;
struct BuildArtifactCompilerMetadata
{
    String8 compiler_path;
    String8 compiler_arg1;
    String8 compiler_id;
    String8 compiler_version;
    String8 compiler_wrapper;
    String8 compiler_frontend_variant;
    String8 compiler_apple_sysroot;
    String8 compiler_architecture_id;
    String8 compiler_ar;
    String8 compiler_ranlib;
    u64 compiler_hash;
    u64 compiler_size;
    u64 compiler_ar_hash;
    u64 compiler_ar_size;
    u64 compiler_ranlib_hash;
    u64 compiler_ranlib_size;
    u32 compiler_arg1_present : 1;
    u32 compiler_wrapper_present : 1;
};

typedef struct BuildArtifactFanoutToolMetadata BuildArtifactFanoutToolMetadata;
struct BuildArtifactFanoutToolMetadata
{
    String8 linker_path;
    String8 slangc_path;
    String8 spirv_opt_path;
    u64 linker_hash;
    u64 linker_size;
    u64 slangc_hash;
    u64 slangc_size;
    u64 spirv_opt_hash;
    u64 spirv_opt_size;
};

typedef struct BuildArtifactFanout BuildArtifactFanout;
struct BuildArtifactFanout
{
    String8 build_directory;
    String8 artifact_path;
    String8 private_bootstrap_path;
    String8 provenance_record_path;
    Generate generate;
    CmakeBuildOptions options;
    BuildArtifactCompilerMetadata expected_compiler;
    BuildArtifactFanoutToolMetadata expected_tools;
    u64 artifact_hash;
    u64 artifact_size;
    u64 cache_fingerprint;
    u64 graph_fingerprint;
    u64 environment_fingerprint;
    u32 release_build_scheduled : 1;
    u32 release_build_succeeded : 1;
    u32 provenance_captured : 1;
    u32 producer_clean_scheduled : 1;
    u32 producer_clean_succeeded : 1;
};

typedef struct BuildArtifactFanoutProvenanceRecord BuildArtifactFanoutProvenanceRecord;
struct BuildArtifactFanoutProvenanceRecord
{
    String8 build_directory;
    String8 artifact_path;
    String8 provenance_record_path;
    String8 config;
    BuildArtifactCompilerMetadata compiler;
    BuildArtifactFanoutToolMetadata tools;
    u64 cache_fingerprint;
    u64 graph_fingerprint;
    u64 environment_fingerprint;
    u32 ci : 1;
    u32 fuzz_available : 1;
    u32 producer_cleaned : 1;
};

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

BUSTER_GLOBAL_LOCAL void string8_list_push(Arena* arena, String8List* list, String8 string);
BUSTER_GLOBAL_LOCAL SliceString8 string8_list_to_slice(Arena* arena, String8List list);

BUSTER_GLOBAL_LOCAL String8 build_artifact_fanout_trim(String8 string)
{
    u64 start = 0;
    u64 end = string.length;
    while (start < end && (u8)string.pointer[start] <= ' ')
    {
        start += 1;
    }
    while (end > start && (u8)string.pointer[end - 1] <= ' ')
    {
        end -= 1;
    }
    return string_slice(string, start, end);
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_find_cache_line(String8 cache, String8 key, String8* line_out)
{
    bool result = false;
    for (u64 line_start = 0; line_start < cache.length && !result;)
    {
        u64 line_end = line_start;
        while (line_end < cache.length && cache.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        String8 line = build_artifact_fanout_trim(string_slice(cache, line_start, line_end));
        if (line.length > key.length && string_starts_with_sequence(line, key) && line.pointer[key.length] == ':')
        {
            *line_out = line;
            result = true;
        }
        line_start = line_end < cache.length ? line_end + 1 : cache.length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_find_value(String8 text, String8 key, bool cache, String8* value)
{
    bool result = false;
    for (u64 line_start = 0; line_start < text.length && !result;)
    {
        u64 line_end = line_start;
        while (line_end < text.length && text.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        String8 line = string_slice(text, line_start, line_end);
        if (cache)
        {
            String8 cache_line = {0};
            if (build_artifact_fanout_find_cache_line(text, key, &cache_line))
            {
                String8 after_key = string_slice(cache_line, key.length, cache_line.length);
                u64 equal_index = string_first_code_unit(after_key, '=');
                if (equal_index != BUSTER_STRING_NO_MATCH)
                {
                    *value = build_artifact_fanout_trim(string_slice(after_key, equal_index + 1, after_key.length));
                    result = true;
                }
            }
        }
        else if (string_starts_with_sequence(line, S8("set(")))
        {
            u64 cursor = 4;
            while (cursor < line.length && (u8)line.pointer[cursor] <= ' ')
            {
                cursor += 1;
            }
            if (cursor + key.length <= line.length && string_equal(string_slice(line, cursor, cursor + key.length), key) &&
                (cursor + key.length == line.length || (u8)line.pointer[cursor + key.length] <= ' ' || line.pointer[cursor + key.length] == ')'))
            {
                cursor += key.length;
                while (cursor < line.length && (u8)line.pointer[cursor] <= ' ')
                {
                    cursor += 1;
                }
                if (cursor < line.length && line.pointer[cursor] == '"')
                {
                    u64 value_start = cursor + 1;
                    cursor = value_start;
                    while (cursor < line.length && line.pointer[cursor] != '"')
                    {
                        cursor += 1;
                    }
                    if (cursor < line.length)
                    {
                        *value = string_slice(line, value_start, cursor);
                        result = true;
                    }
                }
                else
                {
                    u64 value_start = cursor;
                    while (cursor < line.length && (u8)line.pointer[cursor] > ' ' && line.pointer[cursor] != ')')
                    {
                        cursor += 1;
                    }
                    *value = string_slice(line, value_start, cursor);
                    result = true;
                }
            }
        }

        line_start = line_end < text.length ? line_end + 1 : text.length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_cache_bool(String8 cache, String8 name, bool* value)
{
    String8 raw = {0};
    bool result = build_artifact_fanout_find_value(cache, name, true, &raw) && cmake_bool_parse(raw, value);
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_cache_string(String8 cache, String8 name, String8* value)
{
    bool result = build_artifact_fanout_find_value(cache, name, true, value);
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_cache_value_matches(String8 cache, String8 name, String8 expected)
{
    String8 value = {0};
    bool result = build_artifact_fanout_cache_string(cache, name, &value) && string_equal(value, expected);
    return result;
}

BUSTER_GLOBAL_LOCAL u64 build_artifact_fanout_hash_mix_byte(u64 hash, u8 byte)
{
    hash ^= byte;
    hash *= 1099511628211ULL;
    return hash;
}

BUSTER_GLOBAL_LOCAL u64 build_artifact_fanout_hash_string(u64 hash, String8 string)
{
    for (u64 i = 0; i < string.length; i += 1)
    {
        hash = build_artifact_fanout_hash_mix_byte(hash, (u8)string.pointer[i]);
    }
    hash = build_artifact_fanout_hash_mix_byte(hash, 0);
    return hash;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_cache_fingerprint(String8 cache, u64* fingerprint)
{
    String8 names[] = {
        S8("CMAKE_C_COMPILER"),
        S8("CMAKE_C_COMPILER_AR"),
        S8("CMAKE_C_COMPILER_RANLIB"),
        S8("CMAKE_C_COMPILER_TARGET"),
        S8("CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN"),
        S8("CMAKE_C_COMPILER_LAUNCHER"),
        S8("CMAKE_CXX_COMPILER_LAUNCHER"),
        S8("CMAKE_C_LINKER_LAUNCHER"),
        S8("CMAKE_CXX_LINKER_LAUNCHER"),
        S8("CMAKE_LINKER_LAUNCHER"),
        S8("CMAKE_RULE_LAUNCH_COMPILE"),
        S8("CMAKE_RULE_LAUNCH_LINK"),
        S8("RULE_LAUNCH_COMPILE"),
        S8("RULE_LAUNCH_LINK"),
        S8("CMAKE_TOOLCHAIN_FILE"),
        S8("CMAKE_SYSROOT"),
        S8("CMAKE_SYSROOT_COMPILE"),
        S8("CMAKE_SYSROOT_LINK"),
        S8("CMAKE_OSX_SYSROOT"),
        S8("CMAKE_OSX_ARCHITECTURES"),
        S8("CMAKE_OSX_DEPLOYMENT_TARGET"),
        S8("CMAKE_C_STANDARD_LIBRARIES"),
        S8("CMAKE_C_STANDARD_LIBRARIES_DEBUG"),
        S8("CMAKE_C_STANDARD_LIBRARIES_MINSIZEREL"),
        S8("CMAKE_C_STANDARD_LIBRARIES_RELEASE"),
        S8("CMAKE_C_STANDARD_LIBRARIES_RELWITHDEBINFO"),
        S8("CMAKE_C_FLAGS"),
        S8("CMAKE_C_FLAGS_DEBUG"),
        S8("CMAKE_C_FLAGS_MINSIZEREL"),
        S8("CMAKE_C_FLAGS_RELEASE"),
        S8("CMAKE_C_FLAGS_RELWITHDEBINFO"),
        S8("CMAKE_EXE_LINKER_FLAGS"),
        S8("CMAKE_EXE_LINKER_FLAGS_DEBUG"),
        S8("CMAKE_EXE_LINKER_FLAGS_MINSIZEREL"),
        S8("CMAKE_EXE_LINKER_FLAGS_RELEASE"),
        S8("CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO"),
        S8("CMAKE_MODULE_LINKER_FLAGS"),
        S8("CMAKE_MODULE_LINKER_FLAGS_DEBUG"),
        S8("CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL"),
        S8("CMAKE_MODULE_LINKER_FLAGS_RELEASE"),
        S8("CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO"),
        S8("CMAKE_SHARED_LINKER_FLAGS"),
        S8("CMAKE_SHARED_LINKER_FLAGS_DEBUG"),
        S8("CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL"),
        S8("CMAKE_SHARED_LINKER_FLAGS_RELEASE"),
        S8("CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO"),
        S8("CMAKE_STATIC_LINKER_FLAGS"),
        S8("CMAKE_STATIC_LINKER_FLAGS_DEBUG"),
        S8("CMAKE_STATIC_LINKER_FLAGS_MINSIZEREL"),
        S8("CMAKE_STATIC_LINKER_FLAGS_RELEASE"),
        S8("CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO"),
        S8("CMAKE_C_LINK_FLAGS"),
        S8("CMAKE_LINKER"),
        S8("CMAKE_EXECUTABLE_CREATE_C_FLAGS"),
        S8("CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS"),
        S8("CMAKE_SHARED_MODULE_CREATE_C_FLAGS"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO"),
        S8("CMAKE_C_COMPILE_OPTIONS_IPO"),
        S8("CMAKE_C_LINK_OPTIONS_IPO"),
        S8("BUSTER_CI"),
        S8("BUSTER_COMPILE_SHADERS"),
        S8("BUSTER_FUZZ_AVAILABLE"),
        S8("BUSTER_INCLUDE_TESTS"),
        S8("BUSTER_INSTRUMENT"),
        S8("BUSTER_LINK_LIBC"),
        S8("BUSTER_LTO"),
        S8("BUSTER_SANITIZE"),
        S8("BUSTER_SINGLE_THREADED"),
        S8("BUSTER_TIME_TRACE"),
        S8("BUSTER_UNITY_BUILD"),
        S8("BUSTER_USE_VULKAN"),
        S8("BUSTER_USE_D3D12"),
        S8("BUSTER_USE_METAL"),
        S8("BUSTER_VULKAN_SDK"),
        S8("BUSTER_VULKAN_INCLUDE_DIR"),
        S8("BUSTER_SLANGC_EXECUTABLE"),
        S8("BUSTER_SPIRV_OPT_EXECUTABLE"),
        S8("CMAKE_LINKER_TYPE"),
    };
    u64 hash = 1469598103934665603ULL;
    bool result = cache.pointer != 0;
    for (u32 name_i = 0; result && name_i < BUSTER_ARRAY_LENGTH(names); name_i += 1)
    {
        String8 line = {0};
        bool present = build_artifact_fanout_find_cache_line(cache, names[name_i], &line);
        hash = build_artifact_fanout_hash_string(hash, names[name_i]);
        hash = build_artifact_fanout_hash_mix_byte(hash, present ? 1 : 0);
        if (present)
        {
            hash = build_artifact_fanout_hash_string(hash, line);
        }
    }
    if (result)
    {
        *fingerprint = hash;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_restricted_cache_inputs_match(String8 cache)
{
    String8 empty_names[] = {
        S8("CMAKE_C_COMPILER_LAUNCHER"),
        S8("CMAKE_CXX_COMPILER_LAUNCHER"),
        S8("CMAKE_C_LINKER_LAUNCHER"),
        S8("CMAKE_CXX_LINKER_LAUNCHER"),
        S8("CMAKE_LINKER_LAUNCHER"),
        S8("CMAKE_RULE_LAUNCH_COMPILE"),
        S8("CMAKE_RULE_LAUNCH_LINK"),
        S8("RULE_LAUNCH_COMPILE"),
        S8("RULE_LAUNCH_LINK"),
        S8("CMAKE_TOOLCHAIN_FILE"),
        S8("CMAKE_C_COMPILER_TARGET"),
        S8("CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN"),
        S8("CMAKE_OSX_ARCHITECTURES"),
    };
    bool result = true;
    for (u32 name_i = 0; name_i < BUSTER_ARRAY_LENGTH(empty_names); name_i += 1)
    {
        String8 value = {0};
        result &= !build_artifact_fanout_cache_string(cache, empty_names[name_i], &value) || value.length == 0;
    }

    String8 false_names[] = {
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO"),
    };
    for (u32 name_i = 0; name_i < BUSTER_ARRAY_LENGTH(false_names); name_i += 1)
    {
        bool value = false;
        String8 raw = {0};
        bool present = build_artifact_fanout_cache_string(cache, false_names[name_i], &raw);
        result &= !present || (cmake_bool_parse(raw, &value) && !value);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_environment_fingerprint(u64* fingerprint)
{
    String8 rejected_names[] = {
        S8("CC"),
        S8("CXX"),
        S8("CFLAGS"),
        S8("CPPFLAGS"),
        S8("LDFLAGS"),
        S8("CMAKE_TOOLCHAIN_FILE"),
        S8("CMAKE_C_COMPILER_LAUNCHER"),
        S8("CMAKE_C_LINKER_LAUNCHER"),
        S8("CMAKE_RULE_LAUNCH_COMPILE"),
        S8("CMAKE_RULE_LAUNCH_LINK"),
        S8("CMAKE_C_COMPILER_TARGET"),
        S8("CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION"),
        S8("CMAKE_SYSROOT"),
        S8("CMAKE_SYSROOT_COMPILE"),
        S8("CMAKE_SYSROOT_LINK"),
        S8("CMAKE_OSX_SYSROOT"),
        S8("CMAKE_OSX_ARCHITECTURES"),
    };
    for (u32 name_i = 0; name_i < BUSTER_ARRAY_LENGTH(rejected_names); name_i += 1)
    {
        if (os_get_environment_variable(rejected_names[name_i]).length)
        {
            return false;
        }
    }

    String8 observed_names[] = {
        S8("SDKROOT"),
        S8("MACOSX_DEPLOYMENT_TARGET"),
        S8("VULKAN_SDK"),
        S8("CPATH"),
        S8("C_INCLUDE_PATH"),
        S8("CPLUS_INCLUDE_PATH"),
        S8("OBJC_INCLUDE_PATH"),
        S8("LIBRARY_PATH"),
        S8("COMPILER_PATH"),
        S8("INCLUDE"),
        S8("LIB"),
        S8("LIBPATH"),
    };
    u64 hash = 1469598103934665603ULL;
    for (u32 name_i = 0; name_i < BUSTER_ARRAY_LENGTH(observed_names); name_i += 1)
    {
        String8 value = os_get_environment_variable(observed_names[name_i]);
        hash = build_artifact_fanout_hash_string(hash, observed_names[name_i]);
        hash = build_artifact_fanout_hash_mix_byte(hash, value.pointer != 0);
        hash = build_artifact_fanout_hash_string(hash, value);
    }
    *fingerprint = hash;
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_hash_file(Arena* arena, String8 path, u64* hash, u64* size)
{
    String8 path_z = string_duplicate_arena(arena, path, true);
    String8 absolute = os_path_absolute(arena, path_z, true);
    if (!absolute.length)
    {
        return false;
    }
    FileMapRead map = file_map_read(arena, absolute, (FileReadOptions){.map_required = 1});
    bool result = map.mapped_pointer && map.bytes.pointer && map.bytes.length;
    if (result)
    {
        *hash = buster_hash_64(map.bytes.pointer, map.bytes.length);
        *size = map.bytes.length;
    }
    file_map_unmap(map);
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_compiler_digests(Arena* arena, BuildArtifactCompilerMetadata* compiler)
{
    bool result = build_artifact_fanout_hash_file(arena, compiler->compiler_path, &compiler->compiler_hash, &compiler->compiler_size);
    if (result && compiler->compiler_ar.length && string_first_sequence(compiler->compiler_ar, S8("NOTFOUND")) == BUSTER_STRING_NO_MATCH)
    {
        result = build_artifact_fanout_hash_file(arena, compiler->compiler_ar, &compiler->compiler_ar_hash, &compiler->compiler_ar_size);
    }
    if (result && compiler->compiler_ranlib.length && string_first_sequence(compiler->compiler_ranlib, S8("NOTFOUND")) == BUSTER_STRING_NO_MATCH)
    {
        result = build_artifact_fanout_hash_file(arena, compiler->compiler_ranlib, &compiler->compiler_ranlib_hash, &compiler->compiler_ranlib_size);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_graph_fingerprint(Arena* arena, String8 build_directory, u64* fingerprint)
{
    String8 paths[] = {
        S8("compile_commands.json"),
        S8("build-Release.ninja"),
        S8("build.ninja"),
        S8("CMakeFiles/common.ninja"),
        S8("CMakeFiles/rules.ninja"),
        S8("CMakeFiles/impl-Release.ninja"),
    };
    u64 hash = 1469598103934665603ULL;
    for (u32 path_i = 0; path_i < BUSTER_ARRAY_LENGTH(paths); path_i += 1)
    {
        String8 path = path_join(arena, build_directory, paths[path_i]);
        u64 file_hash = 0;
        u64 file_size = 0;
        if (!build_artifact_fanout_hash_file(arena, path, &file_hash, &file_size))
        {
            return false;
        }
        hash = build_artifact_fanout_hash_string(hash, paths[path_i]);
        hash = build_artifact_fanout_hash_mix_byte(hash, (u8)file_size);
        for (u32 shift = 8; shift < 64; shift += 8)
        {
            hash = build_artifact_fanout_hash_mix_byte(hash, (u8)(file_size >> shift));
        }
        for (u32 shift = 0; shift < 64; shift += 8)
        {
            hash = build_artifact_fanout_hash_mix_byte(hash, (u8)(file_hash >> shift));
        }
    }
    *fingerprint = hash;
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_graph_definitions_match(Arena* arena, String8 build_directory)
{
    String8 path = path_join(arena, build_directory, S8("compile_commands.json"));
    String8 absolute = os_path_absolute(arena, path, true);
    FileMapRead map = file_map_read(arena, absolute, (FileReadOptions){.map_required = 1});
    bool result = map.mapped_pointer && map.bytes.pointer;
    if (result)
    {
        String8 graph = BYTE_SLICE_TO_STRING(8, map.bytes);
        String8 expected[] = {
            string_format(arena, S8("BUSTER_USE_VULKAN={u64}"), BUSTER_LINUX != 0),
            string_format(arena, S8("BUSTER_USE_D3D12={u64}"), BUSTER_WINDOWS != 0),
            string_format(arena, S8("BUSTER_USE_METAL={u64}"), BUSTER_MACOS != 0),
            S8("BUSTER_USE_SLANG_SHADERS=1"),
        };
        String8 opposite[] = {
            string_format(arena, S8("BUSTER_USE_VULKAN={u64}"), BUSTER_LINUX == 0),
            string_format(arena, S8("BUSTER_USE_D3D12={u64}"), BUSTER_WINDOWS == 0),
            string_format(arena, S8("BUSTER_USE_METAL={u64}"), BUSTER_MACOS == 0),
            S8("BUSTER_USE_SLANG_SHADERS=0"),
        };
        for (u32 define_i = 0; define_i < BUSTER_ARRAY_LENGTH(expected); define_i += 1)
        {
            result &= string_first_sequence(graph, expected[define_i]) != BUSTER_STRING_NO_MATCH;
            result &= string_first_sequence(graph, opposite[define_i]) == BUSTER_STRING_NO_MATCH;
        }
    }
    file_map_unmap(map);
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_platform_inputs_match(u64 expected_environment_fingerprint)
{
    u64 actual_environment_fingerprint = 0;
    bool result = build_artifact_fanout_environment_fingerprint(&actual_environment_fingerprint) &&
                  actual_environment_fingerprint == expected_environment_fingerprint;
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_inputs_match(String8 cache)
{
    u64 fingerprint = 0;
    bool compile_shaders = false;
    bool result = build_artifact_fanout_cache_fingerprint(cache, &fingerprint) && build_artifact_fanout_restricted_cache_inputs_match(cache) &&
                  build_artifact_fanout_cache_bool(cache, S8("BUSTER_COMPILE_SHADERS"), &compile_shaders) && compile_shaders;
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_path_equal(String8 left, String8 right)
{
    u64 left_length = left.length;
    u64 right_length = right.length;
    while (left_length > 1 && path_is_separator(left.pointer[left_length - 1]))
    {
        left_length -= 1;
    }
    while (right_length > 1 && path_is_separator(right.pointer[right_length - 1]))
    {
        right_length -= 1;
    }

    bool result = left_length == right_length;
    for (u64 i = 0; result && i < left_length; i += 1)
    {
        char8 left_character = left.pointer[i] == '\\' ? '/' : left.pointer[i];
        char8 right_character = right.pointer[i] == '\\' ? '/' : right.pointer[i];
#if BUSTER_WINDOWS
        left_character = ascii_to_lower(left_character);
        right_character = ascii_to_lower(right_character);
#endif
        result = left_character == right_character;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 build_artifact_fanout_absolute_path(Arena* arena, String8 path)
{
    String8 path_z = string_duplicate_arena(arena, path, true);
    String8 result = os_path_absolute(arena, path_z, true);
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_identity_matches(String8 expected_path, BuildArtifactCompilerMetadata actual)
{
    bool clang_id = string_equal(actual.compiler_id, S8("Clang")) || string_equal(actual.compiler_id, S8("AppleClang"));
    bool result = clang_id && actual.compiler_version.length && (!actual.compiler_arg1_present || actual.compiler_arg1.length == 0) &&
                  (!actual.compiler_wrapper_present || actual.compiler_wrapper.length == 0) && build_artifact_fanout_path_equal(expected_path, actual.compiler_path);
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_config_matches(Generate generate, bool ci, bool link_libc, bool unity_build, bool include_tests,
                                                              bool sanitize, bool time_trace, bool instrument, bool fuzz_available, bool lto,
                                                              bool single_threaded, bool use_vulkan, bool check_optional_warnings,
                                                              bool developer_targets, String8 linker)
{
    String8 expected_cc = generate.cc_set ? generate.cc : build_compilers[generate.compiler];
    String8 expected_linker = generate_linker(generate, expected_cc);
    bool result = ci == (bool)generate.ci && link_libc == (bool)generate.link_libc && unity_build && include_tests == (bool)generate.include_tests &&
                  sanitize == (bool)generate.sanitize && time_trace == (bool)generate.time_trace && instrument == (bool)generate.instrument &&
                  fuzz_available == (bool)generate.fuzz_available && lto == (bool)generate.lto && single_threaded &&
                  use_vulkan == (BUSTER_LINUX != 0) && check_optional_warnings == (bool)generate.check_optional_warnings &&
                  developer_targets == (bool)generate.developer_targets && string_equal(linker, expected_linker);
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_is_canonical(Generate generate, CmakeBuildOptions options)
{
    bool valid_arguments = !generate.cmake_arguments.length;
    if (generate.ci)
    {
        valid_arguments = generate.cmake_arguments.length == 1 &&
                          string_equal(generate.cmake_arguments.pointer[0], S8("-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"));
    }

    bool result = generate.compiler == BUILD_COMPILER_CLANG && !generate.cc_set && !generate.linker_set && !generate.config_set && !generate.sanitize &&
                  generate.link_libc && !generate.time_trace && !generate.instrument &&
                  !generate.lto && generate.include_tests && !generate.check_optional_warnings && !generate.developer_targets && valid_arguments &&
                  options.optimize_set && options.optimize && string_equal(cmake_build_config(options), S8("Release"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_read_compiler_metadata(Arena* arena, String8 build_directory,
                                                                       BuildArtifactCompilerMetadata* result)
{
    String8 cache_path = path_join(arena, build_directory, S8("CMakeCache.txt"));
    ByteSlice cache_bytes = file_read(arena, cache_path, (FileReadOptions){.map_required = 0});
    String8 cache = BYTE_SLICE_TO_STRING(8, cache_bytes);
    String8 major = {0};
    String8 minor = {0};
    String8 patch = {0};
    bool result_valid = cache.pointer && build_artifact_fanout_cache_string(cache, S8("CMAKE_CACHE_MAJOR_VERSION"), &major) &&
                        build_artifact_fanout_cache_string(cache, S8("CMAKE_CACHE_MINOR_VERSION"), &minor) &&
                        build_artifact_fanout_cache_string(cache, S8("CMAKE_CACHE_PATCH_VERSION"), &patch);
    if (result_valid)
    {
        String8 version_parts[] = {major, S8("."), minor, S8("."), patch};
        String8 version = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(version_parts), true);
        String8 metadata_path = path_join(arena, path_join(arena, path_join(arena, build_directory, S8("CMakeFiles")), version),
                                          S8("CMakeCCompiler.cmake"));
        ByteSlice metadata_bytes = file_read(arena, metadata_path, (FileReadOptions){.map_required = 0});
        String8 metadata = BYTE_SLICE_TO_STRING(8, metadata_bytes);
        result_valid = metadata.pointer && build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER"), false, &result->compiler_path) &&
                       build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_ID"), false, &result->compiler_id) &&
                       build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_VERSION"), false, &result->compiler_version);
        result->compiler_arg1_present = build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_ARG1"), false, &result->compiler_arg1);
        result->compiler_wrapper_present = build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_WRAPPER"), false, &result->compiler_wrapper);
        build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_FRONTEND_VARIANT"), false, &result->compiler_frontend_variant);
        build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_APPLE_SYSROOT"), false, &result->compiler_apple_sysroot);
        build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_ARCHITECTURE_ID"), false, &result->compiler_architecture_id);
        build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_AR"), false, &result->compiler_ar);
        build_artifact_fanout_find_value(metadata, S8("CMAKE_C_COMPILER_RANLIB"), false, &result->compiler_ranlib);
    }
    return result_valid;
}

typedef struct BuildArtifactFanoutLiveConfig BuildArtifactFanoutLiveConfig;
struct BuildArtifactFanoutLiveConfig
{
    String8 cache;
    String8 cached_compiler;
    String8 linker;
    String8 linker_path;
    String8 slangc_path;
    String8 spirv_opt_path;
    bool ci;
    bool compile_shaders;
    bool link_libc;
    bool unity_build;
    bool include_tests;
    bool sanitize;
    bool time_trace;
    bool instrument;
    bool fuzz_available;
    bool lto;
    bool single_threaded;
    bool use_vulkan;
    bool check_optional_warnings;
    bool developer_targets;
};

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_read_live_config(Arena* arena, String8 build_directory, BuildArtifactFanoutLiveConfig* result)
{
    String8 cache_path = path_join(arena, build_directory, S8("CMakeCache.txt"));
    ByteSlice cache_bytes = file_read(arena, cache_path, (FileReadOptions){.map_required = 0});
    result->cache = BYTE_SLICE_TO_STRING(8, cache_bytes);
    bool valid = result->cache.pointer && build_artifact_fanout_cache_string(result->cache, S8("CMAKE_C_COMPILER"), &result->cached_compiler) &&
                 build_artifact_fanout_cache_string(result->cache, S8("CMAKE_LINKER_TYPE"), &result->linker) &&
                 build_artifact_fanout_cache_string(result->cache, S8("CMAKE_LINKER"), &result->linker_path) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_CI"), &result->ci) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_COMPILE_SHADERS"), &result->compile_shaders) &&
                 result->compile_shaders &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_LINK_LIBC"), &result->link_libc) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_UNITY_BUILD"), &result->unity_build) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_INCLUDE_TESTS"), &result->include_tests) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_SANITIZE"), &result->sanitize) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_TIME_TRACE"), &result->time_trace) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_INSTRUMENT"), &result->instrument) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_FUZZ_AVAILABLE"), &result->fuzz_available) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_LTO"), &result->lto) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_SINGLE_THREADED"), &result->single_threaded) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_USE_VULKAN"), &result->use_vulkan) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_CHECK_OPTIONAL_WARNINGS"), &result->check_optional_warnings) &&
                 build_artifact_fanout_cache_bool(result->cache, S8("BUSTER_DEVELOPER_TARGETS"), &result->developer_targets) &&
                 build_artifact_fanout_provenance_inputs_match(result->cache);
    build_artifact_fanout_cache_string(result->cache, S8("BUSTER_SLANGC_EXECUTABLE"), &result->slangc_path);
    build_artifact_fanout_cache_string(result->cache, S8("BUSTER_SPIRV_OPT_EXECUTABLE"), &result->spirv_opt_path);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_tool_digests(Arena* arena, BuildArtifactFanoutLiveConfig live,
                                                            BuildArtifactFanoutToolMetadata* tools)
{
    *tools = (BuildArtifactFanoutToolMetadata){
        .linker_path = live.linker_path,
        .slangc_path = live.slangc_path,
        .spirv_opt_path = live.spirv_opt_path,
    };
    if (!tools->linker_path.length || string_first_sequence(tools->linker_path, S8("NOTFOUND")) != BUSTER_STRING_NO_MATCH ||
        !build_artifact_fanout_hash_file(arena, tools->linker_path, &tools->linker_hash, &tools->linker_size))
    {
        return false;
    }
    if (live.compile_shaders && (!tools->slangc_path.length || string_first_sequence(tools->slangc_path, S8("NOTFOUND")) != BUSTER_STRING_NO_MATCH))
    {
        return false;
    }
    if (live.compile_shaders && live.use_vulkan &&
        (!tools->spirv_opt_path.length || string_first_sequence(tools->spirv_opt_path, S8("NOTFOUND")) != BUSTER_STRING_NO_MATCH))
    {
        return false;
    }
    String8 optional_paths[] = {tools->slangc_path, tools->spirv_opt_path};
    u64* optional_hashes[] = {&tools->slangc_hash, &tools->spirv_opt_hash};
    u64* optional_sizes[] = {&tools->slangc_size, &tools->spirv_opt_size};
    for (u32 path_i = 0; path_i < BUSTER_ARRAY_LENGTH(optional_paths); path_i += 1)
    {
        if (optional_paths[path_i].length && string_first_sequence(optional_paths[path_i], S8("NOTFOUND")) == BUSTER_STRING_NO_MATCH &&
            !build_artifact_fanout_hash_file(arena, optional_paths[path_i], optional_hashes[path_i], optional_sizes[path_i]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_tool_metadata_match(Arena* arena, BuildArtifactFanoutToolMetadata expected,
                                                                    BuildArtifactFanoutToolMetadata actual)
{
    bool result = build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, expected.linker_path),
                                                   build_artifact_fanout_absolute_path(arena, actual.linker_path)) &&
                  build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, expected.slangc_path),
                                                   build_artifact_fanout_absolute_path(arena, actual.slangc_path)) &&
                  build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, expected.spirv_opt_path),
                                                   build_artifact_fanout_absolute_path(arena, actual.spirv_opt_path)) &&
                  expected.linker_hash == actual.linker_hash && expected.linker_size == actual.linker_size &&
                  expected.slangc_hash == actual.slangc_hash && expected.slangc_size == actual.slangc_size &&
                  expected.spirv_opt_hash == actual.spirv_opt_hash && expected.spirv_opt_size == actual.spirv_opt_size;
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_compiler_metadata_match(Arena* arena, BuildArtifactCompilerMetadata expected,
                                                                         BuildArtifactCompilerMetadata actual)
{
    String8 expected_path = string_duplicate_arena(arena, expected.compiler_path, true);
    String8 actual_path = string_duplicate_arena(arena, actual.compiler_path, true);
    String8 expected_absolute = os_path_absolute(arena, expected_path, true);
    String8 actual_absolute = os_path_absolute(arena, actual_path, true);
    bool result = expected_absolute.length && actual_absolute.length && build_artifact_fanout_path_equal(expected_absolute, actual_absolute) &&
                  string_equal(expected.compiler_id, actual.compiler_id) && string_equal(expected.compiler_version, actual.compiler_version) &&
                  string_equal(expected.compiler_arg1, actual.compiler_arg1) && string_equal(expected.compiler_wrapper, actual.compiler_wrapper) &&
                  string_equal(expected.compiler_frontend_variant, actual.compiler_frontend_variant) &&
                  string_equal(expected.compiler_apple_sysroot, actual.compiler_apple_sysroot) &&
                  string_equal(expected.compiler_architecture_id, actual.compiler_architecture_id) &&
                  build_artifact_fanout_path_equal(expected.compiler_ar, actual.compiler_ar) &&
                  build_artifact_fanout_path_equal(expected.compiler_ranlib, actual.compiler_ranlib) &&
                  expected.compiler_hash == actual.compiler_hash && expected.compiler_size == actual.compiler_size &&
                  expected.compiler_ar_hash == actual.compiler_ar_hash && expected.compiler_ar_size == actual.compiler_ar_size &&
                  expected.compiler_ranlib_hash == actual.compiler_ranlib_hash && expected.compiler_ranlib_size == actual.compiler_ranlib_size &&
                  expected.compiler_arg1_present == actual.compiler_arg1_present && expected.compiler_wrapper_present == actual.compiler_wrapper_present;
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_state_matches(Arena* arena, BuildArtifactFanout expected,
                                                                         BuildArtifactCompilerMetadata actual_compiler,
                                                                         BuildArtifactFanoutToolMetadata actual_tools,
                                                                         u64 actual_cache_fingerprint, u64 actual_graph_fingerprint,
                                                                         u64 actual_environment_fingerprint)
{
    bool result = build_artifact_fanout_compiler_metadata_match(arena, expected.expected_compiler, actual_compiler) &&
                  build_artifact_fanout_tool_metadata_match(arena, expected.expected_tools, actual_tools) &&
                  expected.cache_fingerprint == actual_cache_fingerprint && expected.graph_fingerprint == actual_graph_fingerprint &&
                  expected.environment_fingerprint == actual_environment_fingerprint;
    return result;
}

BUSTER_GLOBAL_LOCAL void build_artifact_fanout_provenance_record_append_u64(Arena* arena, String8List* parts, u64 value)
{
    string8_list_push(arena, parts, string_format(arena, S8("{u64}\n"), value));
}

BUSTER_GLOBAL_LOCAL void build_artifact_fanout_provenance_record_append_string(Arena* arena, String8List* parts, String8 value)
{
    build_artifact_fanout_provenance_record_append_u64(arena, parts, value.length);
    if (value.length)
    {
        string8_list_push(arena, parts, value);
    }
    string8_list_push(arena, parts, S8("\n"));
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_read_line(String8 text, u64* cursor, String8* line)
{
    if (!cursor || !line || *cursor > text.length)
    {
        return false;
    }
    u64 end = *cursor;
    while (end < text.length && text.pointer[end] != '\n')
    {
        end += 1;
    }
    if (end == text.length)
    {
        return false;
    }
    *line = string_slice(text, *cursor, end);
    *cursor = end + 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_read_u64(String8 text, u64* cursor, u64* value)
{
    String8 line = {0};
    if (!build_artifact_fanout_provenance_record_read_line(text, cursor, &line) || !line.length)
    {
        return false;
    }
    IntegerParsingU64 parsed = string8_parse_u64_decimal(line.pointer);
    if (parsed.length != line.length)
    {
        return false;
    }
    *value = parsed.value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_read_string(String8 text, u64* cursor, String8* value)
{
    u64 length = 0;
    if (!build_artifact_fanout_provenance_record_read_u64(text, cursor, &length) || length > BUSTER_MB(1) || *cursor > text.length ||
        length > text.length - *cursor)
    {
        return false;
    }
    *value = string_slice(text, *cursor, *cursor + length);
    *cursor += length;
    if (*cursor >= text.length || text.pointer[*cursor] != '\n')
    {
        return false;
    }
    *cursor += 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_payload(String8 text, String8* payload)
{
    if (!text.length || text.pointer[text.length - 1] != '\n')
    {
        return false;
    }
    u64 checksum_line_start = text.length - 1;
    while (checksum_line_start && text.pointer[checksum_line_start - 1] != '\n')
    {
        checksum_line_start -= 1;
    }
    if (!checksum_line_start)
    {
        return false;
    }
    String8 checksum_line = string_slice(text, checksum_line_start, text.length - 1);
    String8 checksum_prefix = S8("CHECKSUM ");
    if (!string_starts_with_sequence(checksum_line, checksum_prefix))
    {
        return false;
    }
    String8 checksum_digits = string_slice(checksum_line, checksum_prefix.length, checksum_line.length);
    if (!checksum_digits.length)
    {
        return false;
    }
    IntegerParsingU64 parsed = string8_parse_u64_hexadecimal(checksum_digits.pointer);
    if (parsed.length != checksum_digits.length)
    {
        return false;
    }
    *payload = string_slice(text, 0, checksum_line_start);
    return buster_hash_64((u8*)payload->pointer, payload->length) == parsed.value;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_write(Arena* arena, BuildArtifactFanout* fanout)
{
    if (!fanout->provenance_record_path.length)
    {
        return true;
    }

    String8List parts = {0};
    string8_list_push(arena, &parts, S8("BUSTER_ARTIFACT_FANOUT_PROVENANCE_V1\n"));
    build_artifact_fanout_provenance_record_append_string(arena, &parts, fanout->build_directory);
    build_artifact_fanout_provenance_record_append_string(arena, &parts, fanout->artifact_path);
    build_artifact_fanout_provenance_record_append_string(arena, &parts, fanout->provenance_record_path);
    build_artifact_fanout_provenance_record_append_string(arena, &parts, cmake_build_config(fanout->options));
    build_artifact_fanout_provenance_record_append_u64(arena, &parts, fanout->generate.ci);
    build_artifact_fanout_provenance_record_append_u64(arena, &parts, fanout->generate.fuzz_available);
    build_artifact_fanout_provenance_record_append_u64(arena, &parts, fanout->producer_clean_succeeded);

    BuildArtifactCompilerMetadata compiler = fanout->expected_compiler;
    String8 compiler_strings[] = {
        compiler.compiler_path,
        compiler.compiler_arg1,
        compiler.compiler_id,
        compiler.compiler_version,
        compiler.compiler_wrapper,
        compiler.compiler_frontend_variant,
        compiler.compiler_apple_sysroot,
        compiler.compiler_architecture_id,
        compiler.compiler_ar,
        compiler.compiler_ranlib,
    };
    for (u32 string_i = 0; string_i < BUSTER_ARRAY_LENGTH(compiler_strings); string_i += 1)
    {
        build_artifact_fanout_provenance_record_append_string(arena, &parts, compiler_strings[string_i]);
    }
    u64 compiler_values[] = {
        compiler.compiler_hash,
        compiler.compiler_size,
        compiler.compiler_ar_hash,
        compiler.compiler_ar_size,
        compiler.compiler_ranlib_hash,
        compiler.compiler_ranlib_size,
        compiler.compiler_arg1_present,
        compiler.compiler_wrapper_present,
    };
    for (u32 value_i = 0; value_i < BUSTER_ARRAY_LENGTH(compiler_values); value_i += 1)
    {
        build_artifact_fanout_provenance_record_append_u64(arena, &parts, compiler_values[value_i]);
    }

    BuildArtifactFanoutToolMetadata tools = fanout->expected_tools;
    String8 tool_strings[] = {tools.linker_path, tools.slangc_path, tools.spirv_opt_path};
    for (u32 string_i = 0; string_i < BUSTER_ARRAY_LENGTH(tool_strings); string_i += 1)
    {
        build_artifact_fanout_provenance_record_append_string(arena, &parts, tool_strings[string_i]);
    }
    u64 tool_values[] = {
        tools.linker_hash,
        tools.linker_size,
        tools.slangc_hash,
        tools.slangc_size,
        tools.spirv_opt_hash,
        tools.spirv_opt_size,
        fanout->cache_fingerprint,
        fanout->graph_fingerprint,
        fanout->environment_fingerprint,
    };
    for (u32 value_i = 0; value_i < BUSTER_ARRAY_LENGTH(tool_values); value_i += 1)
    {
        build_artifact_fanout_provenance_record_append_u64(arena, &parts, tool_values[value_i]);
    }
    string8_list_push(arena, &parts, S8("END\n"));

    String8 payload = string_join_arena(arena, string8_list_to_slice(arena, parts), false);
    u64 checksum = buster_hash_64((u8*)payload.pointer, payload.length);
    String8 record = string_format(arena, S8("{S8}CHECKSUM {u64:x,no_prefix}\n"), payload, checksum);
    return file_write(fanout->provenance_record_path, BUSTER_SLICE_TO_BYTE_SLICE(record));
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_read(Arena* arena, String8 path,
                                                                       BuildArtifactFanoutProvenanceRecord* result)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.map_required = 0});
    if (!bytes.pointer || !bytes.length || bytes.length > BUSTER_MB(1))
    {
        return false;
    }
    String8 text = BYTE_SLICE_TO_STRING(8, bytes);
    String8 payload = {0};
    if (!build_artifact_fanout_provenance_record_payload(text, &payload))
    {
        return false;
    }
    u64 cursor = 0;
    String8 line = {0};
    if (!build_artifact_fanout_provenance_record_read_line(payload, &cursor, &line) ||
        !string_equal(line, S8("BUSTER_ARTIFACT_FANOUT_PROVENANCE_V1")))
    {
        return false;
    }
    BuildArtifactFanoutProvenanceRecord record = {0};
    if (!build_artifact_fanout_provenance_record_read_string(payload, &cursor, &record.build_directory) ||
        !build_artifact_fanout_provenance_record_read_string(payload, &cursor, &record.artifact_path) ||
        !build_artifact_fanout_provenance_record_read_string(payload, &cursor, &record.provenance_record_path) ||
        !build_artifact_fanout_provenance_record_read_string(payload, &cursor, &record.config))
    {
        return false;
    }
    u64 value = 0;
    if (!build_artifact_fanout_provenance_record_read_u64(payload, &cursor, &value) || value > 1)
    {
        return false;
    }
    record.ci = (u32)value;
    if (!build_artifact_fanout_provenance_record_read_u64(payload, &cursor, &value) || value > 1)
    {
        return false;
    }
    record.fuzz_available = (u32)value;
    if (!build_artifact_fanout_provenance_record_read_u64(payload, &cursor, &value) || value > 1)
    {
        return false;
    }
    record.producer_cleaned = (u32)value;

    String8* compiler_strings[] = {
        &record.compiler.compiler_path,
        &record.compiler.compiler_arg1,
        &record.compiler.compiler_id,
        &record.compiler.compiler_version,
        &record.compiler.compiler_wrapper,
        &record.compiler.compiler_frontend_variant,
        &record.compiler.compiler_apple_sysroot,
        &record.compiler.compiler_architecture_id,
        &record.compiler.compiler_ar,
        &record.compiler.compiler_ranlib,
    };
    for (u32 string_i = 0; string_i < BUSTER_ARRAY_LENGTH(compiler_strings); string_i += 1)
    {
        if (!build_artifact_fanout_provenance_record_read_string(payload, &cursor, compiler_strings[string_i]))
        {
            return false;
        }
    }
    u64* compiler_values[] = {
        &record.compiler.compiler_hash,
        &record.compiler.compiler_size,
        &record.compiler.compiler_ar_hash,
        &record.compiler.compiler_ar_size,
        &record.compiler.compiler_ranlib_hash,
        &record.compiler.compiler_ranlib_size,
    };
    for (u32 value_i = 0; value_i < 6; value_i += 1)
    {
        if (!build_artifact_fanout_provenance_record_read_u64(payload, &cursor, compiler_values[value_i]))
        {
            return false;
        }
    }
    if (!build_artifact_fanout_provenance_record_read_u64(payload, &cursor, &value) || value > 1)
    {
        return false;
    }
    record.compiler.compiler_arg1_present = (u32)value;
    if (!build_artifact_fanout_provenance_record_read_u64(payload, &cursor, &value) || value > 1)
    {
        return false;
    }
    record.compiler.compiler_wrapper_present = (u32)value;

    String8* tool_strings[] = {&record.tools.linker_path, &record.tools.slangc_path, &record.tools.spirv_opt_path};
    for (u32 string_i = 0; string_i < BUSTER_ARRAY_LENGTH(tool_strings); string_i += 1)
    {
        if (!build_artifact_fanout_provenance_record_read_string(payload, &cursor, tool_strings[string_i]))
        {
            return false;
        }
    }
    u64* tool_values[] = {
        &record.tools.linker_hash,
        &record.tools.linker_size,
        &record.tools.slangc_hash,
        &record.tools.slangc_size,
        &record.tools.spirv_opt_hash,
        &record.tools.spirv_opt_size,
        &record.cache_fingerprint,
        &record.graph_fingerprint,
        &record.environment_fingerprint,
    };
    for (u32 value_i = 0; value_i < BUSTER_ARRAY_LENGTH(tool_values); value_i += 1)
    {
        if (!build_artifact_fanout_provenance_record_read_u64(payload, &cursor, tool_values[value_i]))
        {
            return false;
        }
    }
    if (!build_artifact_fanout_provenance_record_read_line(payload, &cursor, &line) || !string_equal(line, S8("END")) || cursor != payload.length)
    {
        return false;
    }
    *result = record;
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_load(Arena* arena, String8 path, BuildArtifactFanout* fanout)
{
    BuildArtifactFanoutProvenanceRecord record = {0};
    if (!path.length || !fanout->provenance_record_path.length || !build_artifact_fanout_is_canonical(fanout->generate, fanout->options) ||
        !build_artifact_fanout_provenance_record_read(arena, path, &record))
    {
        return false;
    }
    bool paths_match = build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, record.build_directory),
                                                        build_artifact_fanout_absolute_path(arena, fanout->build_directory)) &&
                       build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, record.artifact_path),
                                                        build_artifact_fanout_absolute_path(arena, fanout->artifact_path)) &&
                       build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, record.provenance_record_path),
                                                        build_artifact_fanout_absolute_path(arena, path)) &&
                       build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, record.provenance_record_path),
                                                        build_artifact_fanout_absolute_path(arena, fanout->provenance_record_path));
    if (!paths_match || !string_equal(record.config, cmake_build_config(fanout->options)) || record.ci != fanout->generate.ci ||
        record.fuzz_available != fanout->generate.fuzz_available)
    {
        return false;
    }
    fanout->expected_compiler = record.compiler;
    fanout->expected_tools = record.tools;
    fanout->cache_fingerprint = record.cache_fingerprint;
    fanout->graph_fingerprint = record.graph_fingerprint;
    fanout->environment_fingerprint = record.environment_fingerprint;
    fanout->producer_clean_succeeded = record.producer_cleaned;
    fanout->provenance_captured = 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_provenance_record_consume(Arena* arena, String8 path)
{
    remove_path_recursive(arena, path);
    return !path_exists(arena, path);
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_snapshot(Arena* arena, String8 source, String8 destination, u64 expected_hash, u64 expected_size)
{
#if BUSTER_LINUX || BUSTER_MACOS
    struct stat source_stats = {0};
    if (stat((const char*)source.pointer, &source_stats) != 0 || !S_ISREG(source_stats.st_mode))
    {
        return false;
    }
    mode_t source_mode = source_stats.st_mode & 07777;
    if (!(source_mode & 0111))
    {
        return false;
    }
#endif

    bool result = file_copy((CopyFileArguments){.original_path = source, .new_path = destination});
#if BUSTER_LINUX || BUSTER_MACOS
    OsFileDescriptor* destination_file = os_file_open(destination, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
    bool mode_result = false;
    if (destination_file)
    {
        int fchmod_result = fchmod(generic_fd_to_posix(destination_file), source_mode);
        bool close_result = os_file_close(destination_file);
        mode_result = fchmod_result == 0 && close_result;
    }
    struct stat destination_stats = {0};
    bool destination_mode_matches = stat((const char*)destination.pointer, &destination_stats) == 0 && S_ISREG(destination_stats.st_mode) &&
                                    (destination_stats.st_mode & 07777) == source_mode;
    result = result && mode_result && destination_mode_matches;
#endif
    u64 snapshot_hash = 0;
    u64 snapshot_size = 0;
    result = result && build_artifact_fanout_hash_file(arena, destination, &snapshot_hash, &snapshot_size) && snapshot_size == expected_size &&
             snapshot_hash == expected_hash;
    if (!result)
    {
        remove_path_recursive(arena, destination);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_ready(BuildArtifactFanout fanout, bool artifact_exists)
{
    bool result = fanout.release_build_scheduled && fanout.release_build_succeeded && fanout.producer_clean_succeeded && artifact_exists;
    return result;
}

typedef struct SelfHostCompare SelfHostCompare;
struct SelfHostCompare
{
    String8 stage1;
    String8 stage2;
#if BUSTER_WINDOWS
    String8 pdb1;
    String8 pdb2;
#endif
};

BUSTER_GLOBAL_LOCAL String8 cmake_build_config(CmakeBuildOptions options)
{
    String8 result = options.config.pointer ? options.config : (options.optimize ? S8("Release") : S8("Debug"));
    return result;
}

#if BUSTER_WINDOWS
typedef struct SelfHostRsdsPath SelfHostRsdsPath;
struct SelfHostRsdsPath
{
    u64 start;
    u64 end;
};

BUSTER_GLOBAL_LOCAL bool self_host_find_rsds_path(ByteSlice image, SelfHostRsdsPath* result)
{
    if (!image.pointer || !result || image.length < 24)
    {
        return false;
    }
    for (u64 offset = 0; offset + 24 <= image.length; offset += 1)
    {
        if (memcmp(image.pointer + offset, "RSDS", 4) != 0)
        {
            continue;
        }
        u64 start = offset + 24;
        u64 end = start;
        while (end < image.length && image.pointer[end])
        {
            end += 1;
        }
        if (end == image.length || end == start || end - start < 4 || image.pointer[end - 4] != '.' || image.pointer[end - 3] != 'p' ||
            image.pointer[end - 2] != 'd' || image.pointer[end - 1] != 'b')
        {
            continue;
        }
        *result = (SelfHostRsdsPath){
            .start = start,
            .end = end + 1,
        };
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool self_host_compare_pe(ByteSlice left, ByteSlice right)
{
    SelfHostRsdsPath left_path = {0};
    SelfHostRsdsPath right_path = {0};
    if (!self_host_find_rsds_path(left, &left_path) || !self_host_find_rsds_path(right, &right_path) || left_path.start != right_path.start ||
        left_path.end > left.length || right_path.end > right.length)
    {
        return false;
    }

    // The old comparison replaced each path with the same "self-host.pdb\0"
    // before comparing the full copies. Requiring the replacement point to be
    // at the same offset preserves every PE byte before the path and prevents
    // a shifted RSDS record from changing any other bytes under this exception.
    u64 left_suffix_length = left.length - left_path.end;
    u64 right_suffix_length = right.length - right_path.end;
    if (left_suffix_length != right_suffix_length || !memory_compare(left.pointer, right.pointer, left_path.start))
    {
        return false;
    }
    return memory_compare(left.pointer + left_path.end, right.pointer + right_path.end, left_suffix_length);
}
#endif

BUSTER_GLOBAL_LOCAL ProcessRun* build_run_add(Arena* arena, BuildStep* step, String8 build_directory, SliceString8 targets, SliceString8 native_arguments,
                                              CmakeBuildOptions options)
{
    String8 parallel_jobs = options.parallel_jobs ? string_format(arena, S8("{u32}"), options.parallel_jobs) : (String8){0};
    GenericRun r = generic_tool_run_add_start(arena, step, &cmake_path, S8("cmake"));
    OsArgumentBuilder* b = &r.builder;
    os_argument_builder_append(b, S8("--build"));
    os_argument_builder_append(b, build_directory);
    os_argument_builder_append(b, S8("--config"));
    os_argument_builder_append(b, cmake_build_config(options));

    if (options.parallel_jobs)
    {
        os_argument_builder_append(b, S8("--parallel"));
        os_argument_builder_append(b, parallel_jobs);
    }

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
    return r.run;
}

BUSTER_GLOBAL_LOCAL void build_add(Arena* arena, String8 build_directory, SliceString8 targets, SliceString8 native_arguments, CmakeBuildOptions options)
{
    BuildStep* step = step_add(arena);
    build_run_add(arena, step, build_directory, targets, native_arguments, options);
}

BUSTER_GLOBAL_LOCAL ProcessResult self_host_compare_action(Arena* arena, void* data)
{
    SelfHostCompare* compare = data;
    String8 stage1_path = compare->stage1;
    String8 stage2_path = compare->stage2;
#if BUSTER_LINUX || BUSTER_MACOS
    stage1_path = os_path_absolute(arena, stage1_path, true);
    stage2_path = os_path_absolute(arena, stage2_path, true);
#endif
    FileMapRead stage1_map = file_map_read(arena, stage1_path, (FileReadOptions){.map_required = 1});
    if (!stage1_map.mapped_pointer)
    {
        file_map_unmap(stage1_map);
        string_print(S8("error: could not map self-host outputs\n"));
        return PROCESS_RESULT_FAILED;
    }
    FileMapRead stage2_map = file_map_read(arena, stage2_path, (FileReadOptions){.map_required = 1});
    if (!stage2_map.mapped_pointer)
    {
        file_map_unmap(stage2_map);
        file_map_unmap(stage1_map);
        string_print(S8("error: could not map self-host outputs\n"));
        return PROCESS_RESULT_FAILED;
    }
    ByteSlice stage1 = stage1_map.bytes;
    ByteSlice stage2 = stage2_map.bytes;
    bool executable_equal = stage1.length == stage2.length && memory_compare(stage1.pointer, stage2.pointer, stage1.length);
#if BUSTER_WINDOWS
    FileMapRead pdb1_map = file_map_read(arena, compare->pdb1, (FileReadOptions){.map_required = 1});
    if (!pdb1_map.mapped_pointer)
    {
        file_map_unmap(pdb1_map);
        file_map_unmap(stage2_map);
        file_map_unmap(stage1_map);
        string_print(S8("error: could not map self-host outputs\n"));
        return PROCESS_RESULT_FAILED;
    }
    FileMapRead pdb2_map = file_map_read(arena, compare->pdb2, (FileReadOptions){.map_required = 1});
    if (!pdb2_map.mapped_pointer)
    {
        file_map_unmap(pdb2_map);
        file_map_unmap(pdb1_map);
        file_map_unmap(stage2_map);
        file_map_unmap(stage1_map);
        string_print(S8("error: could not map self-host outputs\n"));
        return PROCESS_RESULT_FAILED;
    }
    executable_equal = self_host_compare_pe(stage1, stage2);
    bool pdb_equal = pdb1_map.mapped_pointer && pdb2_map.mapped_pointer && pdb1_map.bytes.length == pdb2_map.bytes.length &&
                     memory_compare(pdb1_map.bytes.pointer, pdb2_map.bytes.pointer, pdb1_map.bytes.length);
    file_map_unmap(pdb2_map);
    file_map_unmap(pdb1_map);
    file_map_unmap(stage2_map);
    file_map_unmap(stage1_map);
    if (!executable_equal || !pdb_equal)
#else
    file_map_unmap(stage2_map);
    file_map_unmap(stage1_map);
    if (!executable_equal)
#endif
    {
        string_print(S8("error: self-host stages differ: {S8} ({u64} bytes) != {S8} ({u64} bytes)\n"), compare->stage1, stage1.length, compare->stage2,
                     stage2.length);
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("SELF_HOST deterministic bytes={u64} stage1={S8} stage2={S8}\n"), stage1.length, compare->stage1, compare->stage2);
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessRun* self_host_compile_add(Arena* arena, String8 compiler, String8 build_directory, String8 sysroot, String8 output,
                                                      String8 timing_description)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    String8 generated_include = string_format(arena, S8("-I{S8}/generated"), build_directory);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, compiler);
    os_argument_builder_append(&builder, S8("cc"));
    os_argument_builder_append(&builder, S8("-Isrc"));
    os_argument_builder_append(&builder, generated_include);
#if BUSTER_WINDOWS
    os_argument_builder_append(&builder, S8("-nostdinc"));
    String8 system_includes = os_get_environment_variable(S8("INCLUDE"));
    for (u64 start = 0; start < system_includes.length;)
    {
        u64 end = start;
        while (end < system_includes.length && system_includes.pointer[end] != ';')
        {
            end += 1;
        }
        if (end != start)
        {
            os_argument_builder_append(&builder, S8("-isystem"));
            os_argument_builder_append(&builder, string_slice(system_includes, start, end));
        }
        start = end + 1;
    }
#endif
    os_argument_builder_append(&builder, S8("-DBUSTER_UNITY_BUILD=1"));
    os_argument_builder_append(&builder, S8("-DBUSTER_INCLUDE_TESTS=0"));
    os_argument_builder_append(&builder, S8("-g"));
#if BUSTER_MACOS
    os_argument_builder_append(&builder, S8("-isysroot"));
    os_argument_builder_append(&builder, sysroot);
#else
    BUSTER_UNUSED(sysroot);
#endif
    os_argument_builder_append(&builder, S8("src/buster/apps/ide/ide.c"));
#if BUSTER_MACOS
    String8 frameworks[] = {
        S8("AppKit"),
        S8("Metal"),
        S8("QuartzCore"),
        S8("Foundation"),
    };
    for (u32 framework_index = 0; framework_index < BUSTER_ARRAY_LENGTH(frameworks); framework_index += 1)
    {
        os_argument_builder_append(&builder, S8("-framework"));
        os_argument_builder_append(&builder, frameworks[framework_index]);
    }
#endif
    os_argument_builder_append(&builder, S8("-o"));
    os_argument_builder_append(&builder, output);
    *run = (ProcessRun){
        .arguments = os_argument_builder_flush(&builder),
        .working_directory = S8("."),
        .timing_description = timing_description,
        .spawn_options =
            (ProcessSpawnOptions){
                .use_process_environment = 1,
            },
    };
    return run;
}

#if BUSTER_MACOS
BUSTER_GLOBAL_LOCAL String8 self_host_macos_sdk_path(Arena* arena)
{
    String8 arguments[] = {
        S8("xcrun"),
        S8("--sdk"),
        S8("macosx"),
        S8("--show-sdk-path"),
    };
    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), (SliceString8){0}, (SliceString8){0},
                                                 (ProcessSpawnOptions){
                                                     .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                     .use_process_environment = 1,
                                                 });
    if (!spawn.handle)
    {
        string_print(S8("error: could not run xcrun to locate the macOS SDK\n"));
        return (String8){0};
    }
    ProcessWaitResult wait = os_process_wait_sync(arena, spawn);
    if (wait.result != PROCESS_RESULT_SUCCESS)
    {
        if (wait.streams[STANDARD_STREAM_ERROR].length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), wait.streams[STANDARD_STREAM_ERROR]);
        }
        string_print(S8("error: xcrun could not locate the macOS SDK\n"));
        return (String8){0};
    }
    String8 result = {
        .pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer,
        .length = wait.streams[STANDARD_STREAM_OUTPUT].length,
    };
    while (result.length && (u8)result.pointer[result.length - 1] <= ' ')
    {
        result.length -= 1;
    }
    if (!result.length)
    {
        string_print(S8("error: xcrun returned an empty macOS SDK path\n"));
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL u32 build_artifact_fanout_producer_output_paths(Arena* arena, BuildArtifactFanout fanout, String8* paths)
{
    String8 config = cmake_build_config(fanout.options);
    String8 object_suffix =
#if BUSTER_WINDOWS
        S8(".obj");
#else
        S8(".o");
#endif
    String8 object_path = path_join(arena, fanout.build_directory,
                                    string_format(arena, S8("CMakeFiles/ide.dir/{S8}/src/buster/apps/ide/ide.c{S8}"), config, object_suffix));
    u32 producer_output_count = 0;
    paths[producer_output_count++] = fanout.artifact_path;
    paths[producer_output_count++] = object_path;
#if BUSTER_WINDOWS
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/rect.vert.hlsl"));
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/rect.frag.hlsl"));
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("generated/buster/lib/shaders/d3d12.h"));
#elif BUSTER_MACOS
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/rect.vert.metal"));
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/rect.frag.metal"));
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("generated/buster/lib/shaders/metal.h"));
#else
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/rect.vert.spv"));
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/rect.frag.spv"));
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/blur.vert.spv"));
    paths[producer_output_count++] = path_join(arena, fanout.build_directory, S8("shaders/blur.frag.spv"));
#endif
    return producer_output_count;
}

BUSTER_GLOBAL_LOCAL u32 build_artifact_fanout_graph_cache_paths(Arena* arena, BuildArtifactFanout fanout, String8* paths)
{
    paths[0] = path_join(arena, fanout.build_directory, S8("CMakeCache.txt"));
    paths[1] = path_join(arena, fanout.build_directory, S8("build.ninja"));
    paths[2] = path_join(arena, fanout.build_directory, S8("build-Release.ninja"));
    paths[3] = path_join(arena, path_join(arena, fanout.build_directory, S8("CMakeFiles")), S8("common.ninja"));
    paths[4] = path_join(arena, path_join(arena, fanout.build_directory, S8("CMakeFiles")), S8("rules.ninja"));
    paths[5] = path_join(arena, path_join(arena, fanout.build_directory, S8("CMakeFiles")), S8("impl-Release.ninja"));
    paths[6] = path_join(arena, fanout.build_directory, S8("compile_commands.json"));
    return 7;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_clean_postcondition(Arena* arena, BuildArtifactFanout fanout, bool* graph_preserved,
                                                                    bool* producer_outputs_absent)
{
    String8 producer_outputs[12] = {0};
    u32 producer_output_count = build_artifact_fanout_producer_output_paths(arena, fanout, producer_outputs);
    bool outputs_absent = true;
    for (u32 output_i = 0; output_i < producer_output_count; output_i += 1)
    {
        outputs_absent &= !path_exists(arena, producer_outputs[output_i]);
    }
    String8 graph_and_cache_paths[7] = {0};
    u32 graph_and_cache_path_count = build_artifact_fanout_graph_cache_paths(arena, fanout, graph_and_cache_paths);
    bool graph_files_present = true;
    for (u32 path_i = 0; path_i < graph_and_cache_path_count; path_i += 1)
    {
        graph_files_present &= path_exists(arena, graph_and_cache_paths[path_i]);
    }
    if (graph_preserved)
    {
        *graph_preserved = graph_files_present;
    }
    if (producer_outputs_absent)
    {
        *producer_outputs_absent = outputs_absent;
    }
    return graph_files_present && outputs_absent;
}

BUSTER_GLOBAL_LOCAL ProcessResult build_artifact_fanout_clean_action(Arena* arena, void* data)
{
    BuildArtifactFanout* fanout = (BuildArtifactFanout*)data;
    if (!fanout->producer_clean_scheduled || !fanout->provenance_captured)
    {
        string_print(S8("error: canonical Clang Release producer clean was not scheduled after provenance capture\n"));
        return PROCESS_RESULT_FAILED;
    }
    bool graph_preserved = false;
    bool producer_outputs_absent = false;
    if (!build_artifact_fanout_clean_postcondition(arena, *fanout, &graph_preserved, &producer_outputs_absent))
    {
        string_print(S8("error: canonical Clang Release producer clean did not preserve graph/cache or remove all producer outputs graph={u32} outputs={u32}\n"),
                     graph_preserved, producer_outputs_absent);
        return PROCESS_RESULT_FAILED;
    }

    fanout->producer_clean_succeeded = 1;
    if (fanout->provenance_record_path.length && !build_artifact_fanout_provenance_record_write(arena, fanout))
    {
        fanout->producer_clean_succeeded = 0;
        remove_path_recursive(arena, fanout->provenance_record_path);
        string_print(S8("error: could not persist canonical producer-clean provenance state\n"));
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("ARTIFACT_FANOUT producer_clean=1 graph_preserved=1 producer_outputs_absent=1\n"));
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult build_artifact_fanout_capture_action(Arena* arena, void* data)
{
    BuildArtifactFanout* fanout = (BuildArtifactFanout*)data;
    BuildArtifactFanoutLiveConfig live = {0};
    BuildArtifactCompilerMetadata compiler = {0};
    BuildArtifactFanoutToolMetadata tools = {0};
    u64 cache_fingerprint = 0;
    u64 graph_fingerprint = 0;
    u64 environment_fingerprint = 0;
    String8 expected_compiler = cmake_cc(arena, fanout->generate.compiler);
    String8 expected_compiler_absolute = build_artifact_fanout_absolute_path(arena, expected_compiler);
    bool live_valid = build_artifact_fanout_read_live_config(arena, fanout->build_directory, &live);
    bool tools_valid = live_valid && build_artifact_fanout_tool_digests(arena, live, &tools);
    bool metadata_valid = build_artifact_fanout_read_compiler_metadata(arena, fanout->build_directory, &compiler);
    bool compiler_digest_valid = metadata_valid && build_artifact_fanout_compiler_digests(arena, &compiler);
    bool compiler_identity_valid = compiler_digest_valid && expected_compiler_absolute.length &&
                                   build_artifact_fanout_identity_matches(expected_compiler_absolute,
                                                                          (BuildArtifactCompilerMetadata){
                                                                              .compiler_path = build_artifact_fanout_absolute_path(arena, compiler.compiler_path),
                                                                              .compiler_arg1 = compiler.compiler_arg1,
                                                                              .compiler_id = compiler.compiler_id,
                                                                              .compiler_version = compiler.compiler_version,
                                                                              .compiler_wrapper = compiler.compiler_wrapper,
                                                                              .compiler_frontend_variant = compiler.compiler_frontend_variant,
                                                                              .compiler_apple_sysroot = compiler.compiler_apple_sysroot,
                                                                              .compiler_architecture_id = compiler.compiler_architecture_id,
                                                                              .compiler_ar = compiler.compiler_ar,
                                                                              .compiler_ranlib = compiler.compiler_ranlib,
                                                                              .compiler_arg1_present = compiler.compiler_arg1_present,
                                                                              .compiler_wrapper_present = compiler.compiler_wrapper_present,
                                                                          });
    bool cached_compiler_valid = live_valid && compiler_identity_valid &&
                                 build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, compiler.compiler_path),
                                                                  build_artifact_fanout_absolute_path(arena, live.cached_compiler));
    bool config_valid = live_valid && build_artifact_fanout_config_matches(fanout->generate, live.ci, live.link_libc, live.unity_build, live.include_tests,
                                                                           live.sanitize, live.time_trace, live.instrument, live.fuzz_available,
                                                                           live.lto, live.single_threaded, live.use_vulkan, live.check_optional_warnings,
                                                                           live.developer_targets, live.linker);
    bool environment_valid = build_artifact_fanout_environment_fingerprint(&environment_fingerprint);
    bool cache_fingerprint_valid = live_valid && build_artifact_fanout_cache_fingerprint(live.cache, &cache_fingerprint);
    bool graph_fingerprint_valid = build_artifact_fanout_graph_fingerprint(arena, fanout->build_directory, &graph_fingerprint);
    bool graph_definitions_valid = build_artifact_fanout_graph_definitions_match(arena, fanout->build_directory);
    bool valid = live_valid && tools_valid && metadata_valid && compiler_digest_valid && compiler_identity_valid && cached_compiler_valid && config_valid &&
                 environment_valid && cache_fingerprint_valid && graph_fingerprint_valid && graph_definitions_valid;
    if (!valid)
    {
        string_print(S8("error: canonical Clang Release provenance capture rejected generated state\n"));
        return PROCESS_RESULT_FAILED;
    }
    fanout->expected_compiler = compiler;
    fanout->expected_tools = tools;
    fanout->cache_fingerprint = cache_fingerprint;
    fanout->graph_fingerprint = graph_fingerprint;
    fanout->environment_fingerprint = environment_fingerprint;
    fanout->provenance_captured = 1;
    if (fanout->provenance_record_path.length && !build_artifact_fanout_provenance_record_write(arena, fanout))
    {
        fanout->provenance_captured = 0;
        remove_path_recursive(arena, fanout->provenance_record_path);
        string_print(S8("error: could not persist canonical Clang Release provenance record: {S8}\n"), fanout->provenance_record_path);
        return PROCESS_RESULT_FAILED;
    }
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult build_artifact_fanout_release_success_action(Arena* arena, void* data)
{
    BuildArtifactFanout* fanout = (BuildArtifactFanout*)data;
    if (!fanout->release_build_scheduled || !fanout->provenance_captured || !fanout->producer_clean_succeeded)
    {
        string_print(S8("error: canonical Clang Release build/provenance/producer-clean state was not completed for artifact fan-out\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!path_exists(arena, fanout->artifact_path))
    {
        string_print(S8("error: canonical Clang Release artifact is missing after its successful build: {S8}\n"), fanout->artifact_path);
        return PROCESS_RESULT_FAILED;
    }
    fanout->release_build_succeeded = 1;
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult build_artifact_fanout_validate_action(Arena* arena, void* data)
{
    BuildArtifactFanout* fanout = (BuildArtifactFanout*)data;
    if (!build_artifact_fanout_ready(*fanout, path_exists(arena, fanout->artifact_path)))
    {
        string_print(S8("error: canonical Clang Release artifact fan-out is unavailable; the producer build did not succeed or the artifact is missing\n"));
        return PROCESS_RESULT_FAILED;
    }

    BuildArtifactCompilerMetadata compiler = {0};
    BuildArtifactFanoutToolMetadata tools = {0};
    BuildArtifactFanoutLiveConfig live = {0};
    u64 cache_fingerprint = 0;
    u64 graph_fingerprint = 0;
    bool live_valid = build_artifact_fanout_read_live_config(arena, fanout->build_directory, &live);
    bool tools_valid = live_valid && build_artifact_fanout_tool_digests(arena, live, &tools);
    bool metadata_valid = build_artifact_fanout_read_compiler_metadata(arena, fanout->build_directory, &compiler);
    bool compiler_digest_valid = metadata_valid && build_artifact_fanout_compiler_digests(arena, &compiler);
    bool compiler_path_valid = compiler_digest_valid &&
                               build_artifact_fanout_path_equal(build_artifact_fanout_absolute_path(arena, compiler.compiler_path),
                                                               build_artifact_fanout_absolute_path(arena, live.cached_compiler));
    bool config_valid = live_valid &&
                        build_artifact_fanout_config_matches(fanout->generate, live.ci, live.link_libc, live.unity_build, live.include_tests,
                                                             live.sanitize, live.time_trace, live.instrument, live.fuzz_available, live.lto,
                                                             live.single_threaded, live.use_vulkan, live.check_optional_warnings,
                                                             live.developer_targets, live.linker);
    bool environment_valid = build_artifact_fanout_platform_inputs_match(fanout->environment_fingerprint);
    bool cache_valid = live_valid && build_artifact_fanout_cache_fingerprint(live.cache, &cache_fingerprint);
    bool graph_valid = build_artifact_fanout_graph_fingerprint(arena, fanout->build_directory, &graph_fingerprint);
    bool graph_definitions_valid = build_artifact_fanout_graph_definitions_match(arena, fanout->build_directory);
    bool state_valid = metadata_valid && compiler_digest_valid && tools_valid && cache_valid && graph_valid &&
                       build_artifact_fanout_provenance_state_matches(arena, *fanout, compiler, tools, cache_fingerprint, graph_fingerprint,
                                                                      fanout->environment_fingerprint);
    bool valid = live_valid && tools_valid && metadata_valid && compiler_digest_valid && compiler_path_valid && config_valid && environment_valid &&
                 cache_valid && graph_valid && graph_definitions_valid && state_valid;
    if (!valid)
    {
        string_print(S8("error: canonical Clang Release artifact fan-out configuration/compiler identity mismatch live={u32} tools={u32} metadata={u32} digest={u32} compiler_path={u32} config={u32} environment={u32} cache={u32} graph={u32} graph_definitions={u32} state={u32}\n"),
                     live_valid, tools_valid, metadata_valid, compiler_digest_valid, compiler_path_valid, config_valid, environment_valid,
                     cache_valid, graph_valid, graph_definitions_valid, state_valid);
        return PROCESS_RESULT_FAILED;
    }

    String8 artifact_absolute = os_path_absolute(arena, fanout->artifact_path, true);
    if (!artifact_absolute.length)
    {
        string_print(S8("error: canonical Clang Release artifact path could not be resolved: {S8}\n"), fanout->artifact_path);
        return PROCESS_RESULT_FAILED;
    }
    if (!build_artifact_fanout_hash_file(arena, artifact_absolute, &fanout->artifact_hash, &fanout->artifact_size))
    {
        string_print(S8("error: canonical Clang Release artifact could not be read: {S8}\n"), artifact_absolute);
        return PROCESS_RESULT_FAILED;
    }

    make_directory_recursive(arena, path_parent(arena, fanout->private_bootstrap_path));
    remove_path_recursive(arena, fanout->private_bootstrap_path);
    if (!build_artifact_fanout_snapshot(arena, artifact_absolute, fanout->private_bootstrap_path, fanout->artifact_hash, fanout->artifact_size))
    {
        string_print(S8("error: could not snapshot canonical Clang Release artifact for self-hosting\n"));
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("ARTIFACT_FANOUT producer=Clang/{S8} path={S8} eliminated=trusted Clang Release bootstrap build bytes={u64}\n"),
                 cmake_build_config(fanout->options), fanout->artifact_path, fanout->artifact_size);
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult build_artifact_fanout_cleanup_action(Arena* arena, void* data)
{
    BuildArtifactFanout* fanout = (BuildArtifactFanout*)data;
    remove_path_recursive(arena, fanout->private_bootstrap_path);
    if (path_exists(arena, fanout->private_bootstrap_path))
    {
        string_print(S8("error: could not remove the private self-host bootstrap snapshot\n"));
        return PROCESS_RESULT_FAILED;
    }
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL void self_host_compare_and_bench_add(Arena* arena, String8 stage1, String8 stage2
#if BUSTER_WINDOWS
                                                          , String8 stage1_pdb, String8 stage2_pdb
#endif
)
{
    SelfHostCompare* compare = arena_allocate(arena, SelfHostCompare, 1);
    *compare = (SelfHostCompare){
        .stage1 = stage1,
        .stage2 = stage2,
#if BUSTER_WINDOWS
        .pdb1 = stage1_pdb,
        .pdb2 = stage2_pdb,
#endif
    };
    BuildStep* compare_step = step_add(arena);
    ProcessRun* compare_run = run_add(arena, compare_step);
    *compare_run = (ProcessRun){
        .callback = self_host_compare_action,
        .callback_data = compare,
    };
    BuildStep* bench_step = step_add(arena);
    ProcessRun* bench_run = run_add(arena, bench_step);
    String8* bench_arguments = arena_allocate(arena, String8, 2);
    bench_arguments[0] = stage2;
    bench_arguments[1] = S8("bench");
    *bench_run = (ProcessRun){
        .arguments =
            {
                .pointer = bench_arguments,
                .length = 2,
            },
        .timing_description = S8("Self-host stage 2 benchmark"),
        .spawn_options =
            (ProcessSpawnOptions){
                .use_process_environment = 1,
            },
    };
}

BUSTER_GLOBAL_LOCAL ProcessResult self_host_from_existing_add(Arena* arena, BuildArtifactFanout* fanout)
{
#if (!(BUSTER_LINUX || BUSTER_WINDOWS) || !BUSTER_CPU_ARCH_X86_64) && !BUSTER_MACOS
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(fanout);
    string_print(S8("error: artifact fan-out self-host consumer is unsupported on this target\n"));
    return PROCESS_RESULT_FAILED;
#else
    String8 config = cmake_build_config(fanout->options);
    String8 sysroot = {0};
#if BUSTER_MACOS
    sysroot = self_host_macos_sdk_path(arena);
    if (!sysroot.length)
    {
        string_print(S8("error: macOS self-host fan-out could not resolve an SDK sysroot\n"));
        return PROCESS_RESULT_FAILED;
    }
#endif
    String8 output_directory = path_join(arena, path_join(arena, fanout->build_directory, S8("self-host")), config);
    fanout->private_bootstrap_path = path_join(arena, output_directory,
#if BUSTER_WINDOWS
                                               S8("ide-bootstrap.exe"));
#else
                                               S8("ide-bootstrap"));
#endif
    String8 stage1 = path_join(arena, output_directory,
#if BUSTER_WINDOWS
                               S8("ide-stage1.exe"));
#else
                               S8("ide-stage1"));
#endif
    String8 stage2 = path_join(arena, output_directory,
#if BUSTER_WINDOWS
                               S8("ide-stage2.exe"));
#else
                               S8("ide-stage2"));
#endif
#if BUSTER_WINDOWS
    String8 stage1_pdb = string_format(arena, S8("{S8}.pdb"), string_slice(stage1, 0, stage1.length - 4));
    String8 stage2_pdb = string_format(arena, S8("{S8}.pdb"), string_slice(stage2, 0, stage2.length - 4));
#endif
    remove_path_recursive(arena, fanout->private_bootstrap_path);
    remove_path_recursive(arena, stage1);
    remove_path_recursive(arena, stage2);
#if BUSTER_WINDOWS
    remove_path_recursive(arena, stage1_pdb);
    remove_path_recursive(arena, stage2_pdb);
#endif

    BuildStep* validate_step = step_add(arena);
    ProcessRun* validate_run = run_add(arena, validate_step);
    *validate_run = (ProcessRun){
        .callback = build_artifact_fanout_validate_action,
        .callback_data = fanout,
    };
    ProcessRun* stage1_run = self_host_compile_add(arena, fanout->private_bootstrap_path, fanout->build_directory, sysroot, stage1,
                                                   S8("Self-host stage 1"));
    stage1_run->cleanup_callback = build_artifact_fanout_cleanup_action;
    stage1_run->cleanup_data = fanout;
    self_host_compile_add(arena, stage1, fanout->build_directory, sysroot, stage2, S8("Self-host stage 2"));
    self_host_compare_and_bench_add(arena, stage1, stage2
#if BUSTER_WINDOWS
                                    , stage1_pdb, stage2_pdb
#endif
    );
    return PROCESS_RESULT_SUCCESS;
#endif
}

BUSTER_GLOBAL_LOCAL ProcessResult self_host_from_existing_command_add(Arena* arena, String8 build_directory, String8 provenance_record_path,
                                                                       CmakeBuildOptions options, Generate request)
{
#if (!(BUSTER_LINUX || BUSTER_WINDOWS) || !BUSTER_CPU_ARCH_X86_64) && !BUSTER_MACOS
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(build_directory);
    BUSTER_UNUSED(provenance_record_path);
    BUSTER_UNUSED(options);
    BUSTER_UNUSED(request);
    string_print(S8("error: artifact fan-out self-host consumer is unsupported on this target\n"));
    return PROCESS_RESULT_FAILED;
#else
    if (!string_equal(cmake_build_config(options), S8("Release")) || !provenance_record_path.length)
    {
        string_print(S8("error: self_host_from_existing requires the canonical Release configuration and provenance record\n"));
        return PROCESS_RESULT_FAILED;
    }
    options.optimize = 1;
    options.optimize_set = 1;

    String8* cmake_arguments = 0;
    if (request.ci)
    {
        cmake_arguments = arena_allocate(arena, String8, 1);
        cmake_arguments[0] = S8("-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY");
    }

    Generate canonical = {
        .compiler = BUILD_COMPILER_CLANG,
        .fuzz_available = request.fuzz_available,
        .ci = request.ci,
        .link_libc = 1,
        .include_tests = 1,
        .check_optional_warnings = 0,
        .developer_targets = 0,
        .cmake_arguments =
            {
                .pointer = cmake_arguments,
                .length = request.ci ? 1 : 0,
            },
    };
    BuildArtifactFanout* fanout = arena_allocate(arena, BuildArtifactFanout, 1);
    String8 ide_name =
#if BUSTER_WINDOWS
        S8("ide.exe");
#else
        S8("ide");
#endif
    *fanout = (BuildArtifactFanout){
        .build_directory = build_directory,
        .artifact_path = path_join(arena, path_join(arena, build_directory, S8("Release")), ide_name),
        .provenance_record_path = provenance_record_path,
        .generate = canonical,
        .options = options,
        .release_build_scheduled = 1,
    };

    ProcessResult result = build_artifact_fanout_provenance_record_load(arena, provenance_record_path, fanout) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    if (result != PROCESS_RESULT_SUCCESS)
    {
        string_print(S8("error: canonical Clang Release provenance record is missing, malformed, or mismatched: {S8}\n"), provenance_record_path);
    }
    if (result == PROCESS_RESULT_SUCCESS && !build_artifact_fanout_provenance_record_consume(arena, provenance_record_path))
    {
        string_print(S8("error: canonical Clang Release provenance record could not be consumed before self-host stages: {S8}\n"),
                     provenance_record_path);
        result = PROCESS_RESULT_FAILED;
    }
    if (result == PROCESS_RESULT_SUCCESS)
    {
        result = build_artifact_fanout_release_success_action(arena, fanout);
    }
    if (result == PROCESS_RESULT_SUCCESS)
    {
        result = self_host_from_existing_add(arena, fanout);
    }
    return result;
#endif
}

BUSTER_GLOBAL_LOCAL ProcessResult self_host_add(Arena* arena, String8 build_directory, CmakeBuildOptions options, Generate generate)
{
#if (!(BUSTER_LINUX || BUSTER_WINDOWS) || !BUSTER_CPU_ARCH_X86_64) && !BUSTER_MACOS
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(build_directory);
    BUSTER_UNUSED(options);
    BUSTER_UNUSED(generate);
    string_print(S8("error: deterministic self-hosting is currently supported only on Linux and Windows x86-64, and macOS\n"));
    return PROCESS_RESULT_FAILED;
#else
    String8 config = cmake_build_config(options);
    String8 sysroot = {0};
#if BUSTER_MACOS
    sysroot = self_host_macos_sdk_path(arena);
    if (!sysroot.length)
    {
        return PROCESS_RESULT_FAILED;
    }
#endif
    String8 cmake_cache = path_join(arena, build_directory, S8("CMakeCache.txt"));
    if (!path_exists(arena, cmake_cache))
    {
        generate.build_directory = build_directory;
        generate.config = config;
        generate.config_set = true;
        BuildStep* generate_step = step_add(arena);
        generate_add(arena, generate_step, generate);
    }
    String8 ide_name =
#if BUSTER_WINDOWS
        S8("ide.exe");
#else
        S8("ide");
#endif
    String8 bootstrap = path_join(arena, path_join(arena, build_directory, config), ide_name);
    String8 output_directory = path_join(arena, path_join(arena, build_directory, S8("self-host")), config);
    make_directory_recursive(arena, output_directory);
    String8 stage1 = path_join(arena, output_directory,
#if BUSTER_WINDOWS
                               S8("ide-stage1.exe"));
#else
                               S8("ide-stage1"));
#endif
    String8 stage2 = path_join(arena, output_directory,
#if BUSTER_WINDOWS
                               S8("ide-stage2.exe"));
#else
                               S8("ide-stage2"));
#endif
#if BUSTER_WINDOWS
    bootstrap = os_path_absolute(arena, bootstrap, true);
    stage1 = os_path_absolute(arena, stage1, true);
    stage2 = os_path_absolute(arena, stage2, true);
    String8 stage1_pdb = string_format(arena, S8("{S8}.pdb"), string_slice(stage1, 0, stage1.length - 4));
    String8 stage2_pdb = string_format(arena, S8("{S8}.pdb"), string_slice(stage2, 0, stage2.length - 4));
#endif
    // Replacing an executable in place can leave macOS with a stale vnode code-signature cache.
    // Unlink both outputs before either compiler recreates them.
    remove_path_recursive(arena, stage1);
    remove_path_recursive(arena, stage2);
#if BUSTER_WINDOWS
    remove_path_recursive(arena, stage1_pdb);
    remove_path_recursive(arena, stage2_pdb);
#endif
    String8 targets[] = {S8("ide")};
    build_add(arena, build_directory, (SliceString8)BUSTER_ARRAY_TO_SLICE(targets), (SliceString8){0}, options);
    self_host_compile_add(arena, bootstrap, build_directory, sysroot, stage1, S8("Self-host stage 1"));
    self_host_compile_add(arena, stage1, build_directory, sysroot, stage2, S8("Self-host stage 2"));
    SelfHostCompare* compare = arena_allocate(arena, SelfHostCompare, 1);
    *compare = (SelfHostCompare){
        .stage1 = stage1,
        .stage2 = stage2,
#if BUSTER_WINDOWS
        .pdb1 = stage1_pdb,
        .pdb2 = stage2_pdb,
#endif
    };
    BuildStep* compare_step = step_add(arena);
    ProcessRun* compare_run = run_add(arena, compare_step);
    *compare_run = (ProcessRun){
        .callback = self_host_compare_action,
        .callback_data = compare,
    };
    BuildStep* bench_step = step_add(arena);
    ProcessRun* bench_run = run_add(arena, bench_step);
    String8* bench_arguments = arena_allocate(arena, String8, 2);
    bench_arguments[0] = stage2;
    bench_arguments[1] = S8("bench");
    *bench_run = (ProcessRun){
        .arguments =
            {
                .pointer = bench_arguments,
                .length = 2,
            },
        .timing_description = S8("Self-host stage 2 benchmark"),
        .spawn_options =
            (ProcessSpawnOptions){
                .use_process_environment = 1,
            },
    };
    return PROCESS_RESULT_SUCCESS;
#endif
}

typedef struct JsonParser JsonParser;
struct JsonParser
{
    String8 text;
    u64 index;
};

typedef struct AssemblyImportOptions AssemblyImportOptions;
struct AssemblyImportOptions
{
    String8 xed_datafiles;
    String8 aarch64_json;
    String8 output_directory;
    u32 xed_datafiles_set : 1;
    u32 aarch64_json_set : 1;
    u32 output_directory_set : 1;
    u32 audit : 1;
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
    u32 quiet : 1;
    u32 compile_commands_set : 1;
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
    u32 profile_set : 1;
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
    *node = (String8Node){.string = string};

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
    SliceString8 result = {.pointer = arena_allocate(arena, String8, list.count), .length = list.count};

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
            result = (String8){.pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start};
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
                break;
            case '"':
                arena_append_char8(arena, '"');
                break;
            case '\\':
                arena_append_char8(arena, '\\');
                break;
            case '/':
                arena_append_char8(arena, '/');
                break;
            case 'b':
                arena_append_char8(arena, '\b');
                break;
            case 'f':
                arena_append_char8(arena, '\f');
                break;
            case 'n':
                arena_append_char8(arena, '\n');
                break;
            case 'r':
                arena_append_char8(arena, '\r');
                break;
            case 't':
                arena_append_char8(arena, '\t');
                break;
            case 'u':
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
            break;
            default:
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

// The assembly metadata importer consumes llvm-tblgen's large JSON dump. Its
// nesting depends on upstream data, so keep this scanner iterative instead of
// extending json_skip_value's recursive implementation.
BUSTER_GLOBAL_LOCAL String8 json_raw_string(JsonParser* parser, bool* valid)
{
    String8 result = {0};
    json_skip_whitespace(parser);
    if (parser->index >= parser->text.length || parser->text.pointer[parser->index] != '"')
    {
        *valid = false;
        return result;
    }

    u64 start = parser->index++;
    bool escaped = false;
    while (parser->index < parser->text.length)
    {
        char8 c = parser->text.pointer[parser->index++];
        if (escaped)
        {
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
        }
        else if (c == '"')
        {
            return string_slice(parser->text, start, parser->index);
        }
    }

    *valid = false;
    return result;
}

// Summary-only borrowed scanner: this deliberately stays separate from
// json_raw_string because assembly metadata accepts that parser's legacy
// escape behavior.
BUSTER_GLOBAL_LOCAL bool time_trace_json_is_whitespace(char8 c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

BUSTER_GLOBAL_LOCAL void time_trace_json_skip_whitespace(JsonParser* parser)
{
    while (parser->index < parser->text.length && time_trace_json_is_whitespace(parser->text.pointer[parser->index]))
    {
        parser->index += 1;
    }
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_consume(JsonParser* parser, char8 c)
{
    time_trace_json_skip_whitespace(parser);
    bool result = parser->index < parser->text.length && parser->text.pointer[parser->index] == c;
    if (result)
    {
        parser->index += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_decode_unicode_escape(String8 text, u64* index, u32* codepoint)
{
    if (*index > text.length || text.length - *index < 4)
    {
        return false;
    }

    bool valid = true;
    u32 value = 0;
    for (u64 digit_i = 0; digit_i < 4; digit_i += 1)
    {
        value = (value << 4) | hexadecimal_digit_value(text.pointer[(*index)++], &valid);
    }
    if (!valid)
    {
        return false;
    }

    if (value >= 0xd800 && value <= 0xdbff)
    {
        if (*index > text.length || text.length - *index < 6 || text.pointer[*index] != '\\' || text.pointer[*index + 1] != 'u')
        {
            return false;
        }
        *index += 2;

        bool low_valid = true;
        u32 low = 0;
        for (u64 digit_i = 0; digit_i < 4; digit_i += 1)
        {
            low = (low << 4) | hexadecimal_digit_value(text.pointer[(*index)++], &low_valid);
        }
        if (!low_valid || low < 0xdc00 || low > 0xdfff)
        {
            return false;
        }
        *codepoint = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
        return true;
    }

    if (value >= 0xdc00 && value <= 0xdfff)
    {
        return false;
    }

    *codepoint = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_decode_escape(String8 text, u64* index, u32* codepoint)
{
    if (*index >= text.length)
    {
        return false;
    }

    char8 escape = text.pointer[(*index)++];
    switch (escape)
    {
        break;
    case '"':
        *codepoint = '"';
        break;
    case '\\':
        *codepoint = '\\';
        break;
    case '/':
        *codepoint = '/';
        break;
    case 'b':
        *codepoint = '\b';
        break;
    case 'f':
        *codepoint = '\f';
        break;
    case 'n':
        *codepoint = '\n';
        break;
    case 'r':
        *codepoint = '\r';
        break;
    case 't':
        *codepoint = '\t';
        break;
    case 'u':
        return time_trace_json_decode_unicode_escape(text, index, codepoint);
    default:
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL String8 time_trace_json_raw_string(JsonParser* parser, bool* valid, bool* invalid_string)
{
    String8 result = {0};
    time_trace_json_skip_whitespace(parser);
    if (parser->index >= parser->text.length || parser->text.pointer[parser->index] != '"')
    {
        *valid = false;
        return result;
    }

    u64 start = parser->index++;
    bool escaped = false;
    while (parser->index < parser->text.length)
    {
        char8 c = parser->text.pointer[parser->index++];
        if (escaped)
        {
            if (c == 'u')
            {
                u32 codepoint = 0;
                if (!time_trace_json_decode_unicode_escape(parser->text, &parser->index, &codepoint))
                {
                    if (invalid_string)
                    {
                        *invalid_string = true;
                    }
                    *valid = false;
                    return result;
                }
            }
            else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r' && c != 't')
            {
                if (invalid_string)
                {
                    *invalid_string = true;
                }
                *valid = false;
                return result;
            }
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
        }
        else if (c == '"')
        {
            return string_slice(parser->text, start, parser->index);
        }
        else if ((u8)c < 0x20)
        {
            *valid = false;
            return result;
        }
    }

    *valid = false;
    return result;
}

typedef enum JsonValueFrameState JsonValueFrameState;
enum JsonValueFrameState
{
    JSON_VALUE_FRAME_START,
    JSON_VALUE_FRAME_OBJECT_KEY,
    JSON_VALUE_FRAME_OBJECT_COLON,
    JSON_VALUE_FRAME_VALUE,
    JSON_VALUE_FRAME_AFTER_VALUE,
};

typedef struct JsonValueFrame JsonValueFrame;
struct JsonValueFrame
{
    char8 close;
    JsonValueFrameState state;
};

BUSTER_GLOBAL_LOCAL bool json_skip_json_scalar(JsonParser* parser, bool* valid)
{
    time_trace_json_skip_whitespace(parser);
    u64 start = parser->index;
    while (parser->index < parser->text.length)
    {
        char8 c = parser->text.pointer[parser->index];
        if (time_trace_json_is_whitespace(c) || c == ',' || c == ']' || c == '}')
        {
            break;
        }
        parser->index += 1;
    }

    if (parser->index == start)
    {
        *valid = false;
        return false;
    }

    String8 scalar = string_slice(parser->text, start, parser->index);
    if (string_equal(scalar, S8("true")) || string_equal(scalar, S8("false")) || string_equal(scalar, S8("null")))
    {
        return true;
    }

    u64 index = 0;
    if (scalar.pointer[index] == '-')
    {
        index += 1;
    }
    if (index >= scalar.length)
    {
        *valid = false;
        return false;
    }

    if (scalar.pointer[index] == '0')
    {
        index += 1;
    }
    else if (scalar.pointer[index] >= '1' && scalar.pointer[index] <= '9')
    {
        while (index < scalar.length && code_unit_is_decimal(scalar.pointer[index]))
        {
            index += 1;
        }
    }
    else
    {
        *valid = false;
        return false;
    }

    if (index < scalar.length && scalar.pointer[index] == '.')
    {
        index += 1;
        u64 fraction_start = index;
        while (index < scalar.length && code_unit_is_decimal(scalar.pointer[index]))
        {
            index += 1;
        }
        if (index == fraction_start)
        {
            *valid = false;
            return false;
        }
    }

    if (index < scalar.length && (scalar.pointer[index] == 'e' || scalar.pointer[index] == 'E'))
    {
        index += 1;
        if (index < scalar.length && (scalar.pointer[index] == '+' || scalar.pointer[index] == '-'))
        {
            index += 1;
        }
        u64 exponent_start = index;
        while (index < scalar.length && code_unit_is_decimal(scalar.pointer[index]))
        {
            index += 1;
        }
        if (index == exponent_start)
        {
            *valid = false;
            return false;
        }
    }

    if (index != scalar.length)
    {
        *valid = false;
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void json_skip_value_start(JsonParser* parser, JsonValueFrame* frames, u64* depth, bool* completed, bool* valid,
                                               bool* resource_exhausted)
{
    time_trace_json_skip_whitespace(parser);
    if (parser->index >= parser->text.length)
    {
        *valid = false;
        return;
    }

    char8 c = parser->text.pointer[parser->index];
    if (c == '"')
    {
        time_trace_json_raw_string(parser, valid, 0);
        *completed = *valid;
    }
    else if (c == '{' || c == '[')
    {
        if (*depth == 256)
        {
            *valid = false;
            *resource_exhausted = true;
            return;
        }

        parser->index += 1;
        frames[*depth] = (JsonValueFrame){
            .close = c == '{' ? '}' : ']',
            .state = JSON_VALUE_FRAME_START,
        };
        *depth += 1;
    }
    else
    {
        *completed = json_skip_json_scalar(parser, valid);
    }
}

BUSTER_GLOBAL_LOCAL bool json_skip_value_no_alloc(JsonParser* parser, bool* valid, bool* resource_exhausted)
{
    JsonValueFrame frames[256];
    u64 depth = 0;
    bool completed = false;

    json_skip_value_start(parser, frames, &depth, &completed, valid, resource_exhausted);

    while (*valid)
    {
        if (completed)
        {
            if (!depth)
            {
                return true;
            }

            JsonValueFrame* parent = &frames[depth - 1];
            if (parent->state != JSON_VALUE_FRAME_VALUE)
            {
                *valid = false;
                return false;
            }
            parent->state = JSON_VALUE_FRAME_AFTER_VALUE;
            completed = false;
        }

        if (!depth)
        {
            *valid = false;
            return false;
        }

        JsonValueFrame* frame = &frames[depth - 1];
        switch (frame->state)
        {
        case JSON_VALUE_FRAME_START:
            if (time_trace_json_consume(parser, frame->close))
            {
                depth -= 1;
                completed = true;
            }
            else if (frame->close == '}')
            {
                frame->state = JSON_VALUE_FRAME_OBJECT_KEY;
            }
            else
            {
                frame->state = JSON_VALUE_FRAME_VALUE;
            }
            break;
        case JSON_VALUE_FRAME_OBJECT_KEY:
            time_trace_json_raw_string(parser, valid, 0);
            frame->state = JSON_VALUE_FRAME_OBJECT_COLON;
            break;
        case JSON_VALUE_FRAME_OBJECT_COLON:
            if (time_trace_json_consume(parser, ':'))
            {
                frame->state = JSON_VALUE_FRAME_VALUE;
            }
            else
            {
                *valid = false;
            }
            break;
        case JSON_VALUE_FRAME_VALUE:
            json_skip_value_start(parser, frames, &depth, &completed, valid, resource_exhausted);
            break;
        case JSON_VALUE_FRAME_AFTER_VALUE:
            if (time_trace_json_consume(parser, ','))
            {
                frame->state = frame->close == '}' ? JSON_VALUE_FRAME_OBJECT_KEY : JSON_VALUE_FRAME_VALUE;
            }
            else if (time_trace_json_consume(parser, frame->close))
            {
                depth -= 1;
                completed = true;
            }
            else
            {
                *valid = false;
            }
            break;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL String8 json_raw_value_no_alloc(JsonParser* parser, bool* valid, bool* resource_exhausted)
{
    time_trace_json_skip_whitespace(parser);
    u64 start = parser->index;
    String8 result = {0};
    if (json_skip_value_no_alloc(parser, valid, resource_exhausted))
    {
        result = string_slice(parser->text, start, parser->index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 json_raw_value(JsonParser* parser, bool* valid)
{
    String8 result = {0};
    json_skip_whitespace(parser);
    if (parser->index >= parser->text.length)
    {
        *valid = false;
        return result;
    }

    u64 start = parser->index;
    char8 first = parser->text.pointer[parser->index];
    if (first == '"')
    {
        return json_raw_string(parser, valid);
    }
    if (first == '{' || first == '[')
    {
        char8 stack[256];
        u64 depth = 0;
        bool in_string = false;
        bool escaped = false;
        while (parser->index < parser->text.length)
        {
            char8 c = parser->text.pointer[parser->index++];
            if (in_string)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (c == '\\')
                {
                    escaped = true;
                }
                else if (c == '"')
                {
                    in_string = false;
                }
                continue;
            }
            if (c == '"')
            {
                in_string = true;
            }
            else if (c == '{' || c == '[')
            {
                if (depth == BUSTER_ARRAY_LENGTH(stack))
                {
                    *valid = false;
                    break;
                }
                stack[depth++] = c;
            }
            else if (c == '}' || c == ']')
            {
                char8 expected = c == '}' ? '{' : '[';
                if (!depth || stack[depth - 1] != expected)
                {
                    *valid = false;
                    break;
                }
                depth -= 1;
                if (!depth)
                {
                    result = string_slice(parser->text, start, parser->index);
                    break;
                }
            }
        }
        if (!result.pointer)
        {
            *valid = false;
        }
        return result;
    }

    while (parser->index < parser->text.length)
    {
        char8 c = parser->text.pointer[parser->index];
        if (c == ',' || c == ']' || c == '}' || character_is_space(c))
        {
            break;
        }
        parser->index += 1;
    }
    if (parser->index == start)
    {
        *valid = false;
    }
    else
    {
        result = string_slice(parser->text, start, parser->index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool json_raw_object_next(JsonParser* parser, String8* key, String8* value, bool* valid)
{
    json_skip_whitespace(parser);
    if (json_consume(parser, '}'))
    {
        return false;
    }
    *key = json_raw_string(parser, valid);
    if (!*valid || !json_consume(parser, ':'))
    {
        *valid = false;
        return false;
    }
    *value = json_raw_value(parser, valid);
    if (!*valid)
    {
        return false;
    }
    json_skip_whitespace(parser);
    if (json_consume(parser, ','))
    {
        return true;
    }
    if (parser->index < parser->text.length && parser->text.pointer[parser->index] == '}')
    {
        return true;
    }
    *valid = false;
    return false;
}

BUSTER_GLOBAL_LOCAL String8 json_raw_key_text(String8 raw)
{
    return raw.length >= 2 ? string_slice(raw, 1, raw.length - 1) : (String8){0};
}

BUSTER_GLOBAL_LOCAL bool json_raw_object_find(String8 object, String8 wanted, String8* value)
{
    JsonParser parser = {.text = object};
    bool valid = json_consume(&parser, '{');
    while (valid)
    {
        String8 key = {0};
        String8 candidate = {0};
        if (!json_raw_object_next(&parser, &key, &candidate, &valid))
        {
            break;
        }
        if (string_equal(json_raw_key_text(key), wanted))
        {
            *value = candidate;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void arena_append_string8(Arena* arena, String8 string)
{
    if (string.length)
    {
        memcpy(arena_allocate(arena, char8, string.length), string.pointer, string.length);
    }
}

BUSTER_GLOBAL_LOCAL void arena_append_json_string(Arena* arena, String8 string)
{
    arena_append_char8(arena, '"');
    for (u64 i = 0; i < string.length; i += 1)
    {
        char8 c = string.pointer[i];
        switch (c)
        {
            break;
        case '"':
            arena_append_string8(arena, S8("\\\""));
            break;
        case '\\':
            arena_append_string8(arena, S8("\\\\"));
            break;
        case '\n':
            arena_append_string8(arena, S8("\\n"));
            break;
        case '\r':
            arena_append_string8(arena, S8("\\r"));
            break;
        case '\t':
            arena_append_string8(arena, S8("\\t"));
            break;
        default:
            if ((u8)c < 0x20)
            {
                static char8 const hexadecimal[] = "0123456789abcdef";
                arena_append_string8(arena, S8("\\u00"));
                arena_append_char8(arena, hexadecimal[((u8)c >> 4) & 0xf]);
                arena_append_char8(arena, hexadecimal[(u8)c & 0xf]);
            }
            else
            {
                arena_append_char8(arena, c);
            }
        }
    }
    arena_append_char8(arena, '"');
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

        String8 argument = {.pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start};
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
            String8 argument = {.pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start};
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

BUSTER_GLOBAL_LOCAL bool clang_analyze_skip_option(SliceString8 arguments, u64 argument_index, u64* skip_count)
{
    BUSTER_CHECK(argument_index < arguments.length);
    String8 argument = arguments.pointer[argument_index];
    String8 next_argument = argument_index + 1 < arguments.length ? arguments.pointer[argument_index + 1] : (String8){0};
    String8 drop_options[] = {
        S8("-c"), S8("-fcolor-diagnostics"), S8("-MD"), S8("-MMD"), S8("-MP"),
    };
    String8 separate_options[] = {
        S8("-MF"), S8("-MJ"), S8("-MQ"), S8("-MT"), S8("-o"), S8("--output"), S8("-dependency-file"),
    };
    String8 joined_options[] = {
        S8("-MF"), S8("-MJ"), S8("-MQ"), S8("-MT"), S8("-o"), S8("--output="), S8("-dependency-file="),
    };
    String8 build_host_definitions[] = {
        S8("-DBUSTER_HOST_C_COMPILER="),
        S8("-DBUSTER_HOST_C_COMPILER_ARG1="),
        S8("-DBUSTER_HOST_C_RESOURCE_INCLUDE="),
        S8("-DBUSTER_HOST_C_COMPILER_MSVC="),
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

    for (u64 i = 0; !result && i < BUSTER_ARRAY_LENGTH(build_host_definitions); i += 1)
    {
        String8 prefix = build_host_definitions[i];
        if (argument.length >= prefix.length && string_starts_with_sequence(argument, prefix))
        {
            *skip_count = 1;
            // CMake can emit an escaped string definition containing spaces as
            // multiple command arguments on Windows. These build-host values
            // are adjacent to compiler options and are irrelevant to analysis.
            while (argument_index + *skip_count < arguments.length)
            {
                String8 continuation = arguments.pointer[argument_index + *skip_count];
                if (continuation.length && continuation.pointer[0] == '-')
                {
                    break;
                }
                *skip_count += 1;
            }
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
            u64 skip_count = 0;
            if (clang_analyze_skip_option(compile_arguments, i, &skip_count))
            {
                i += skip_count;
            }
            else
            {
                count += 1;
                i += 1;
            }
        }

        result = (SliceString8){.pointer = arena_allocate(arena, String8, count), .length = count};
        u64 out = 0;
        result.pointer[out++] = clang.pointer ? clang : compile_arguments.pointer[0];
        result.pointer[out++] = S8("--analyze");
        result.pointer[out++] = S8("-Xanalyzer");
        result.pointer[out++] = S8("-analyzer-output=text");
        result.pointer[out++] = S8("-fno-color-diagnostics");
        result.pointer[out++] = S8("-Wno-error=unused-command-line-argument");

        for (u64 i = 1; i < compile_arguments.length;)
        {
            u64 skip_count = 0;
            if (clang_analyze_skip_option(compile_arguments, i, &skip_count))
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
            result = string_has_path_part(entry.output, config) || string_contains(entry.output, cmake_intdir_plain) ||
                     string_contains(entry.output, cmake_intdir_quoted) || string_contains(entry.output, cmake_intdir_escaped);
        }

        for (u64 i = 0; !result && i < arguments.length; i += 1)
        {
            String8 candidate = arguments.pointer[i];
            result = string_has_path_part(candidate, config) || string_contains(candidate, cmake_intdir_plain) ||
                     string_contains(candidate, cmake_intdir_quoted) || string_contains(candidate, cmake_intdir_escaped);
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
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});

    if (!bytes.pointer)
    {
        string_print(S8("error: compile commands not found: {S8}\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    JsonParser parser = {.text = {.pointer = (char8*)bytes.pointer, .length = bytes.length}};
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
                    .spawn_options =
                        (ProcessSpawnOptions){
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

    *run = (ProcessRun){
        .arguments = os_argument_builder_flush(&builder),
        .spawn_options =
            (ProcessSpawnOptions){
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
    *run = (ProcessRun){
        .arguments = arguments,
        .spawn_options =
            (ProcessSpawnOptions){
                .use_process_environment = 1,
            },
    };
}

BUSTER_GLOBAL_LOCAL void cmake_profile_row_list_push(Arena* arena, CmakeProfileRowList* list, CmakeProfileRow row)
{
    CmakeProfileRowNode* node = arena_allocate(arena, CmakeProfileRowNode, 1);
    *node = (CmakeProfileRowNode){.row = row};

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

    ByteSlice bytes = file_read(arena, options.profile, (FileReadOptions){.end_padding = 1});
    if (!bytes.pointer)
    {
        string_print(S8("error: failed to read {S8}\n"), options.profile);
        return PROCESS_RESULT_FAILED;
    }

    JsonParser parser = {.text = {.pointer = (char8*)bytes.pointer, .length = bytes.length}};
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
            cmake_profile_row_list_push(arena, &row_list,
                                        (CmakeProfileRow){
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
        printf("%4llu.%03llu ms  %-10.*s %-30.*s %.*s %.*s\n", duration_ms_whole, duration_ms_fraction, string8_printf_length(row.category, 10),
               row.category.pointer ? row.category.pointer : "", string8_printf_length(row.name, 30), row.name.pointer ? row.name.pointer : "",
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
    u32 build_directory_set : 1;
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
    *node = (NinjaLogRowNode){.row = row};

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
    ByteSlice bytes = file_read(arena, log_path, (FileReadOptions){.end_padding = 1});
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

        if (!ninja_log_next_field(&line, &start_field) || !ninja_log_next_field(&line, &end_field) || !ninja_log_next_field(&line, &restat_field) ||
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

        ninja_log_row_list_upsert(arena, &row_list,
                                  (NinjaLogRow){
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

// --- summary output capture -----------------------------------------------
// Shared by the summary diagnostics below. A summary normally writes straight
// to a file descriptor; a self-test instead supplies a capture arena so it can
// compare the exact rendered report, which is the only way to cover column
// layout and rounding.

typedef struct SummaryOutput SummaryOutput;
struct SummaryOutput
{
    OsFileDescriptor* file;
    Arena* capture_arena;
    String8List captured;
};

BUSTER_GLOBAL_LOCAL void summary_output_print(SummaryOutput* output, String8 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    if (output->capture_arena)
    {
        String8 text = string_format_va(output->capture_arena, format, variable_arguments);
        string8_list_push(output->capture_arena, &output->captured, text);
        if (output->file && text.length)
        {
            os_file_write(output->file, BUSTER_SLICE_TO_BYTE_SLICE(text));
        }
    }
    else if (output->file)
    {
        TemporalArena scratch = scratch_begin(0, 0);
        String8 text = string_format_va(scratch.arena, format, variable_arguments);
        if (text.length)
        {
            os_file_write(output->file, BUSTER_SLICE_TO_BYTE_SLICE(text));
        }
        scratch_end(scratch);
    }
    va_end(variable_arguments);
}

BUSTER_GLOBAL_LOCAL String8 summary_output_text(Arena* arena, SummaryOutput output)
{
    u64 length = 0;
    for (String8Node* node = output.captured.first; node; node = node->next)
    {
        if (node->string.length > UINT64_MAX - length)
        {
            return (String8){0};
        }
        length += node->string.length;
    }

    if (!length)
    {
        return (String8){0};
    }

    char8* pointer = arena_allocate(arena, char8, length);
    u64 position = 0;
    for (String8Node* node = output.captured.first; node; node = node->next)
    {
        memcpy(pointer + position, node->string.pointer, node->string.length);
        position += node->string.length;
    }
    return (String8){.pointer = pointer, .length = length};
}

// Pads `string` on the right so a report can align a text column; the format
// language only knows how to pad integers.
BUSTER_GLOBAL_LOCAL String8 summary_pad_right(Arena* arena, String8 string, u64 width)
{
    if (string.length >= width)
    {
        return string;
    }

    char8* pointer = arena_allocate(arena, char8, width);
    memcpy(pointer, string.pointer, string.length);
    memset(pointer + string.length, ' ', width - string.length);
    return (String8){.pointer = pointer, .length = width};
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

typedef enum TimeTraceSummaryFailureReason TimeTraceSummaryFailureReason;
enum TimeTraceSummaryFailureReason
{
    TIME_TRACE_SUMMARY_FAILURE_NONE,
    TIME_TRACE_SUMMARY_FAILURE_RESOURCE,
    TIME_TRACE_SUMMARY_FAILURE_MALFORMED_JSON,
    TIME_TRACE_SUMMARY_FAILURE_TRUNCATED_JSON,
    TIME_TRACE_SUMMARY_FAILURE_TRAILING_JSON,
    TIME_TRACE_SUMMARY_FAILURE_NON_INTEGER_DURATION,
    TIME_TRACE_SUMMARY_FAILURE_INVALID_WHITESPACE,
    TIME_TRACE_SUMMARY_FAILURE_INVALID_STRING,
    TIME_TRACE_SUMMARY_FAILURE_DURATION_OVERFLOW,
    TIME_TRACE_SUMMARY_FAILURE_ROW_LIMIT,
    TIME_TRACE_SUMMARY_FAILURE_NAME_LIMIT,
    TIME_TRACE_SUMMARY_FAILURE_DEPTH_LIMIT,
};

typedef struct TimeTraceRow TimeTraceRow;
struct TimeTraceRow
{
    String8 name;
    s64 duration_us;
};

typedef struct TimeTraceRowList TimeTraceRowList;
struct TimeTraceRowList
{
    TimeTraceRow rows[4096];
    char8 name_storage[BUSTER_KB(512)];
    u64 name_storage_position;
    u64 count;
};

BUSTER_GLOBAL_LOCAL void time_trace_summary_failure_set(TimeTraceSummaryFailureReason* failure_reason, TimeTraceSummaryFailureReason reason)
{
    if (failure_reason)
    {
        *failure_reason = reason;
    }
}

BUSTER_GLOBAL_LOCAL bool time_trace_duration_add(s64* destination, s64 duration_us)
{
    const s64 s64_max = (s64)((u64)~(u64)0 >> 1);
    const s64 s64_min = -s64_max - 1;
    if ((duration_us > 0 && *destination > s64_max - duration_us) || (duration_us < 0 && *destination < s64_min - duration_us))
    {
        return false;
    }
    *destination += duration_us;
    return true;
}

typedef enum TimeTraceRowAddResult TimeTraceRowAddResult;
enum TimeTraceRowAddResult
{
    TIME_TRACE_ROW_ADD_SUCCESS,
    TIME_TRACE_ROW_ADD_NAME_LIMIT,
    TIME_TRACE_ROW_ADD_ROW_LIMIT,
    TIME_TRACE_ROW_ADD_DURATION_OVERFLOW,
    TIME_TRACE_ROW_ADD_INVALID_NAME,
};

BUSTER_GLOBAL_LOCAL TimeTraceRowAddResult time_trace_row_list_add(TimeTraceRowList* list, String8 name, s64 duration_us, bool copy_name,
                                                                   bool* name_retained)
{
    if (name_retained)
    {
        *name_retained = false;
    }

    for (u64 row_i = 0; row_i < list->count; row_i += 1)
    {
        TimeTraceRow* row = &list->rows[row_i];
        if (string_equal(row->name, name))
        {
            return time_trace_duration_add(&row->duration_us, duration_us) ? TIME_TRACE_ROW_ADD_SUCCESS : TIME_TRACE_ROW_ADD_DURATION_OVERFLOW;
        }
    }

    if (copy_name)
    {
        if (list->name_storage_position > BUSTER_KB(512) || name.length > BUSTER_KB(512) - list->name_storage_position)
        {
            return TIME_TRACE_ROW_ADD_NAME_LIMIT;
        }
        char8* name_pointer = list->name_storage + list->name_storage_position;
        memcpy(name_pointer, name.pointer, name.length);
        name = (String8){.pointer = name_pointer, .length = name.length};
        list->name_storage_position += name.length;
    }

    if (list->count == BUSTER_ARRAY_LENGTH(list->rows))
    {
        if (copy_name)
        {
            list->name_storage_position -= name.length;
        }
        return TIME_TRACE_ROW_ADD_ROW_LIMIT;
    }
    list->rows[list->count] = (TimeTraceRow){.name = name, .duration_us = duration_us};
    list->count += 1;
    if (name_retained)
    {
        *name_retained = true;
    }
    return TIME_TRACE_ROW_ADD_SUCCESS;
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
    String8 raw_name;
    s64 duration_us;
    u32 phase_is_x : 1;
    u32 has_duration : 1;
    u32 resource_exhausted : 1;
    u32 duration_non_integer : 1;
    u32 invalid_string : 1;
};

typedef enum TimeTraceDurationParseResult TimeTraceDurationParseResult;
enum TimeTraceDurationParseResult
{
    TIME_TRACE_DURATION_PARSE_SUCCESS,
    TIME_TRACE_DURATION_PARSE_INVALID,
    TIME_TRACE_DURATION_PARSE_NON_INTEGER,
};

BUSTER_GLOBAL_LOCAL TimeTraceDurationParseResult time_trace_parse_duration(String8 raw, s64* result)
{
    const u64 s64_max = (u64)~(u64)0 >> 1;
    const u64 s64_min_magnitude = s64_max + 1;
    u64 index = 0;
    bool negative = false;
    for (u64 i = 0; i < raw.length; i += 1)
    {
        if (raw.pointer[i] == '.' || raw.pointer[i] == 'e' || raw.pointer[i] == 'E')
        {
            return TIME_TRACE_DURATION_PARSE_NON_INTEGER;
        }
    }

    if (index < raw.length && raw.pointer[index] == '-')
    {
        negative = true;
        index += 1;
    }
    if (index >= raw.length)
    {
        return TIME_TRACE_DURATION_PARSE_INVALID;
    }

    u64 limit = negative ? s64_min_magnitude : s64_max;
    u64 magnitude = 0;
    u64 integer_start = index;
    if (raw.pointer[index] == '0')
    {
        index += 1;
    }
    else if (raw.pointer[index] >= '1' && raw.pointer[index] <= '9')
    {
        while (index < raw.length && code_unit_is_decimal(raw.pointer[index]))
        {
            u64 digit = (u64)(raw.pointer[index++] - '0');
            if (magnitude > (limit - digit) / 10)
            {
                return TIME_TRACE_DURATION_PARSE_INVALID;
            }
            magnitude = magnitude * 10 + digit;
        }
    }
    else
    {
        return TIME_TRACE_DURATION_PARSE_INVALID;
    }

    if (index == integer_start)
    {
        return TIME_TRACE_DURATION_PARSE_INVALID;
    }
    if (index != raw.length)
    {
        return TIME_TRACE_DURATION_PARSE_INVALID;
    }

    if (negative)
    {
        if (magnitude == s64_min_magnitude)
        {
            *result = -(s64_max) - 1;
        }
        else
        {
            *result = -(s64)magnitude;
        }
    }
    else
    {
        *result = (s64)magnitude;
    }
    return TIME_TRACE_DURATION_PARSE_SUCCESS;
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_string_decode_codepoint(String8 raw, u64* index, u32* codepoint, bool* direct_byte)
{
    if (raw.length < 2 || raw.pointer[0] != '"' || raw.pointer[raw.length - 1] != '"' || *index >= raw.length - 1)
    {
        return false;
    }

    char8 c = raw.pointer[(*index)++];
    if (c != '\\')
    {
        if (direct_byte)
        {
            *direct_byte = true;
        }
        *codepoint = (u8)c;
        return true;
    }

    if (direct_byte)
    {
        *direct_byte = false;
    }

    return time_trace_json_decode_escape(raw, index, codepoint);
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_string_has_prefix(String8 raw, String8 prefix)
{
    u64 index = 1;
    for (u64 prefix_i = 0; prefix_i < prefix.length; prefix_i += 1)
    {
        u32 codepoint = 0;
        if (!time_trace_json_string_decode_codepoint(raw, &index, &codepoint, 0) || codepoint != (u8)prefix.pointer[prefix_i])
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_string_is_plain(String8 raw)
{
    if (raw.length < 2 || raw.pointer[0] != '"' || raw.pointer[raw.length - 1] != '"')
    {
        return false;
    }

    for (u64 index = 1; index + 1 < raw.length; index += 1)
    {
        if (raw.pointer[index] == '\\')
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_string_equal(String8 raw, String8 wanted)
{
    if (raw.length < 2 || raw.pointer[0] != '"' || raw.pointer[raw.length - 1] != '"')
    {
        return false;
    }

    u64 index = 1;
    for (u64 wanted_i = 0; wanted_i < wanted.length; wanted_i += 1)
    {
        u32 codepoint = 0;
        if (!time_trace_json_string_decode_codepoint(raw, &index, &codepoint, 0) || codepoint != (u8)wanted.pointer[wanted_i])
        {
            return false;
        }
    }
    return index == raw.length - 1;
}

typedef enum TimeTraceJsonStringDecodeResult TimeTraceJsonStringDecodeResult;
enum TimeTraceJsonStringDecodeResult
{
    TIME_TRACE_JSON_STRING_DECODE_SUCCESS,
    TIME_TRACE_JSON_STRING_DECODE_RESOURCE,
    TIME_TRACE_JSON_STRING_DECODE_INVALID,
};

BUSTER_GLOBAL_LOCAL TimeTraceJsonStringDecodeResult time_trace_json_string_decode_into_list(TimeTraceRowList* row_list, String8 raw, String8* result)
{
    if (raw.length < 2 || raw.pointer[0] != '"' || raw.pointer[raw.length - 1] != '"')
    {
        return TIME_TRACE_JSON_STRING_DECODE_INVALID;
    }

    u64 start = row_list->name_storage_position;
    u64 index = 1;
    while (index < raw.length - 1)
    {
        u32 codepoint = 0;
        bool direct_byte = false;
        if (!time_trace_json_string_decode_codepoint(raw, &index, &codepoint, &direct_byte))
        {
            row_list->name_storage_position = start;
            return TIME_TRACE_JSON_STRING_DECODE_INVALID;
        }

        u64 encoded_length = 1;
        if (direct_byte)
        {
            encoded_length = 1;
        }
        else if (codepoint <= 0x7f)
        {
            encoded_length = 1;
        }
        else if (codepoint <= 0x7ff)
        {
            encoded_length = 2;
        }
        else if (codepoint <= 0xffff)
        {
            encoded_length = 3;
        }
        else
        {
            encoded_length = 4;
        }
        if (row_list->name_storage_position > BUSTER_KB(512) || encoded_length > BUSTER_KB(512) - row_list->name_storage_position)
        {
            row_list->name_storage_position = start;
            return TIME_TRACE_JSON_STRING_DECODE_RESOURCE;
        }

        if (direct_byte)
        {
            row_list->name_storage[row_list->name_storage_position++] = raw.pointer[index - 1];
        }
        else if (codepoint <= 0x7f)
        {
            row_list->name_storage[row_list->name_storage_position++] = (char8)codepoint;
        }
        else if (codepoint <= 0x7ff)
        {
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0xc0 | (codepoint >> 6));
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0x80 | (codepoint & 0x3f));
        }
        else if (codepoint <= 0xffff)
        {
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0xe0 | (codepoint >> 12));
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0x80 | ((codepoint >> 6) & 0x3f));
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0x80 | (codepoint & 0x3f));
        }
        else
        {
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0xf0 | (codepoint >> 18));
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0x80 | ((codepoint >> 12) & 0x3f));
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0x80 | ((codepoint >> 6) & 0x3f));
            row_list->name_storage[row_list->name_storage_position++] = (char8)(0x80 | (codepoint & 0x3f));
        }
    }

    *result = (String8){.pointer = row_list->name_storage + start, .length = row_list->name_storage_position - start};
    return TIME_TRACE_JSON_STRING_DECODE_SUCCESS;
}

BUSTER_GLOBAL_LOCAL TimeTraceRowAddResult time_trace_row_list_add_raw(TimeTraceRowList* row_list, String8 raw_name, s64 duration_us, bool* valid)
{
    if (!time_trace_json_string_has_prefix(raw_name, S8("Total ")))
    {
        return TIME_TRACE_ROW_ADD_SUCCESS;
    }

    if (time_trace_json_string_is_plain(raw_name))
    {
        String8 name = string_slice(raw_name, 1, raw_name.length - 1);
        return time_trace_row_list_add(row_list, name, duration_us, true, 0);
    }

    u64 name_position = row_list->name_storage_position;
    String8 name = {0};
    TimeTraceJsonStringDecodeResult decode_result = time_trace_json_string_decode_into_list(row_list, raw_name, &name);
    if (decode_result != TIME_TRACE_JSON_STRING_DECODE_SUCCESS)
    {
        if (decode_result == TIME_TRACE_JSON_STRING_DECODE_RESOURCE)
        {
            return TIME_TRACE_ROW_ADD_NAME_LIMIT;
        }
        *valid = false;
        return TIME_TRACE_ROW_ADD_INVALID_NAME;
    }

    bool name_retained = false;
    TimeTraceRowAddResult result = time_trace_row_list_add(row_list, name, duration_us, false, &name_retained);
    if (!name_retained)
    {
        row_list->name_storage_position = name_position;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool time_trace_json_string_is_phase_x(String8 raw)
{
    if (raw.length < 2 || raw.pointer[0] != '"' || raw.pointer[raw.length - 1] != '"')
    {
        return false;
    }

    u64 index = 1;
    u32 codepoint = 0;
    if (!time_trace_json_string_decode_codepoint(raw, &index, &codepoint, 0))
    {
        return false;
    }
    return codepoint == (u32)'X' && index == raw.length - 1;
}

BUSTER_GLOBAL_LOCAL TimeTraceEvent time_trace_parse_event(JsonParser* parser, bool* valid)
{
    TimeTraceEvent result = {0};
    bool resource_exhausted = false;
    bool invalid_string = false;

    if (!time_trace_json_consume(parser, '{'))
    {
        *valid = false;
        return result;
    }

    if (time_trace_json_consume(parser, '}'))
    {
        return result;
    }

    for (;;)
    {
        time_trace_json_skip_whitespace(parser);
        String8 key = time_trace_json_raw_string(parser, valid, &invalid_string);
        if (!*valid || !time_trace_json_consume(parser, ':'))
        {
            *valid = false;
            break;
        }

        if (time_trace_json_string_equal(key, S8("ph")))
        {
            String8 phase = time_trace_json_raw_string(parser, valid, &invalid_string);
            if (*valid)
            {
                result.phase_is_x = time_trace_json_string_is_phase_x(phase);
            }
        }
        else if (time_trace_json_string_equal(key, S8("name")))
        {
            result.raw_name = time_trace_json_raw_string(parser, valid, &invalid_string);
        }
        else if (time_trace_json_string_equal(key, S8("dur")))
        {
            String8 duration = json_raw_value_no_alloc(parser, valid, &resource_exhausted);
            if (*valid)
            {
                TimeTraceDurationParseResult duration_result = time_trace_parse_duration(duration, &result.duration_us);
                result.has_duration = duration_result == TIME_TRACE_DURATION_PARSE_SUCCESS;
                if (duration_result == TIME_TRACE_DURATION_PARSE_NON_INTEGER)
                {
                    result.duration_non_integer = 1;
                    *valid = false;
                }
                else if (duration_result != TIME_TRACE_DURATION_PARSE_SUCCESS)
                {
                    *valid = false;
                }
            }
        }
        else
        {
            json_raw_value_no_alloc(parser, valid, &resource_exhausted);
        }

        if (!*valid)
        {
            break;
        }
        if (time_trace_json_consume(parser, '}'))
        {
            break;
        }
        if (!time_trace_json_consume(parser, ','))
        {
            *valid = false;
            break;
        }
    }

    result.resource_exhausted = resource_exhausted;
    result.invalid_string = invalid_string;
    return result;
}

BUSTER_GLOBAL_LOCAL TimeTraceSummaryFailureReason time_trace_summary_parser_failure_reason(JsonParser* parser, bool invalid_string)
{
    if (invalid_string)
    {
        return TIME_TRACE_SUMMARY_FAILURE_INVALID_STRING;
    }
    if (parser->index < parser->text.length && (parser->text.pointer[parser->index] == '\f' || parser->text.pointer[parser->index] == '\v'))
    {
        return TIME_TRACE_SUMMARY_FAILURE_INVALID_WHITESPACE;
    }
    return parser->index >= parser->text.length ? TIME_TRACE_SUMMARY_FAILURE_TRUNCATED_JSON : TIME_TRACE_SUMMARY_FAILURE_MALFORMED_JSON;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_parse_file(String8 path, TimeTraceRowList* row_list, SummaryOutput* output,
                                                                TimeTraceSummaryFailureReason* failure_reason)
{
    u64 path_arena_size = BUSTER_MB(1);
    Arena* path_arena = arena_create((ArenaCreation){
        .reserved_size = path_arena_size,
        .granularity = BUSTER_KB(64),
        .initial_size = path_arena_size,
    });
    if (!path_arena)
    {
        summary_output_print(output, S8("error: time_trace_summary could not reserve path storage\n"));
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_RESOURCE);
        return PROCESS_RESULT_FAILED;
    }

    ProcessResult result = PROCESS_RESULT_FAILED;
    FileMapRead map = {0};
    String8 map_path = path;
    if (path.length && path.pointer[0] != '/')
    {
        String8 absolute_path = os_path_absolute(path_arena, path, true);
        if (absolute_path.length)
        {
            map_path = absolute_path;
        }
    }

    if (map_path.length >= path_arena_size)
    {
        summary_output_print(output, S8("error: time_trace_summary resource limit while opening {S8}\n"), path);
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_RESOURCE);
        goto cleanup;
    }

    map = file_map_read(path_arena, map_path, (FileReadOptions){.map_required = 1});
    if (!map.bytes.pointer)
    {
        summary_output_print(output, S8("error: failed to map {S8}\n"), path);
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_RESOURCE);
        goto cleanup;
    }

    JsonParser parser = {.text = {.pointer = (char8*)map.bytes.pointer, .length = map.bytes.length}};
    if (!time_trace_json_consume(&parser, '{'))
    {
        summary_output_print(output, S8("error: failed to parse {S8}: expected a JSON object\n"), path);
        time_trace_summary_failure_set(failure_reason, time_trace_summary_parser_failure_reason(&parser, false));
        goto cleanup;
    }

    bool valid = true;
    if (time_trace_json_consume(&parser, '}'))
    {
        result = PROCESS_RESULT_SUCCESS;
        goto trailing_check;
    }

    for (;;)
    {
        bool invalid_string = false;
        String8 key = time_trace_json_raw_string(&parser, &valid, &invalid_string);
        if (!valid || !time_trace_json_consume(&parser, ':'))
        {
            summary_output_print(output, S8("error: failed to parse {S8}: expected an object key\n"), path);
            time_trace_summary_failure_set(failure_reason, time_trace_summary_parser_failure_reason(&parser, invalid_string));
            goto cleanup;
        }

        if (time_trace_json_string_equal(key, S8("traceEvents")))
        {
            if (!time_trace_json_consume(&parser, '['))
            {
                summary_output_print(output, S8("error: failed to parse {S8}: expected traceEvents array\n"), path);
                time_trace_summary_failure_set(failure_reason, time_trace_summary_parser_failure_reason(&parser, false));
                goto cleanup;
            }

            if (!time_trace_json_consume(&parser, ']'))
            {
                for (;;)
                {
                    TimeTraceEvent event = time_trace_parse_event(&parser, &valid);
                    if (!valid)
                    {
                        if (event.resource_exhausted)
                        {
                            summary_output_print(output, S8("error: time_trace_summary resource limit while parsing {S8}\n"), path);
                            time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_DEPTH_LIMIT);
                        }
                        else if (event.invalid_string)
                        {
                            summary_output_print(output, S8("error: failed to parse {S8}: invalid Unicode string escape\n"), path);
                            time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_INVALID_STRING);
                        }
                        else if (event.duration_non_integer)
                        {
                            summary_output_print(output, S8("error: failed to parse {S8}: duration must be an integer number of microseconds\n"), path);
                            time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_NON_INTEGER_DURATION);
                        }
                        else
                        {
                            summary_output_print(output, S8("error: failed to parse {S8}: invalid trace event\n"), path);
                            time_trace_summary_failure_set(failure_reason, time_trace_summary_parser_failure_reason(&parser, false));
                        }
                        goto cleanup;
                    }

                    if (event.phase_is_x && event.has_duration)
                    {
                        TimeTraceRowAddResult add_result = time_trace_row_list_add_raw(row_list, event.raw_name, event.duration_us, &valid);
                        if (!valid)
                        {
                            summary_output_print(output, S8("error: failed to parse {S8}: invalid trace event name\n"), path);
                            time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_INVALID_STRING);
                            goto cleanup;
                        }
                        if (add_result == TIME_TRACE_ROW_ADD_NAME_LIMIT)
                        {
                            summary_output_print(output, S8("error: time_trace_summary resource limit while aggregating {S8}\n"), path);
                            time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_NAME_LIMIT);
                            goto cleanup;
                        }
                        if (add_result == TIME_TRACE_ROW_ADD_ROW_LIMIT)
                        {
                            summary_output_print(output, S8("error: time_trace_summary resource limit while aggregating {S8}\n"), path);
                            time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_ROW_LIMIT);
                            goto cleanup;
                        }
                        if (add_result == TIME_TRACE_ROW_ADD_DURATION_OVERFLOW)
                        {
                            summary_output_print(output, S8("error: time_trace_summary duration overflow while aggregating {S8}\n"), path);
                            time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_DURATION_OVERFLOW);
                            goto cleanup;
                        }
                    }

                    if (time_trace_json_consume(&parser, ']'))
                    {
                        break;
                    }
                    if (!time_trace_json_consume(&parser, ','))
                    {
                        summary_output_print(output, S8("error: failed to parse {S8}: expected ',' or ']'\n"), path);
                        time_trace_summary_failure_set(failure_reason, time_trace_summary_parser_failure_reason(&parser, false));
                        goto cleanup;
                    }
                }
            }
        }
        else
        {
            bool resource_exhausted = false;
            json_raw_value_no_alloc(&parser, &valid, &resource_exhausted);
            if (!valid)
            {
                if (resource_exhausted)
                {
                    summary_output_print(output, S8("error: time_trace_summary resource limit while parsing {S8}\n"), path);
                    time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_DEPTH_LIMIT);
                }
                else
                {
                    summary_output_print(output, S8("error: failed to parse {S8}: invalid value\n"), path);
                    time_trace_summary_failure_set(failure_reason, time_trace_summary_parser_failure_reason(&parser, false));
                }
                goto cleanup;
            }
        }

        if (time_trace_json_consume(&parser, '}'))
        {
            result = PROCESS_RESULT_SUCCESS;
            break;
        }
        if (!time_trace_json_consume(&parser, ','))
        {
            summary_output_print(output, S8("error: failed to parse {S8}: expected ',' or '}'\n"), path);
            time_trace_summary_failure_set(failure_reason, time_trace_summary_parser_failure_reason(&parser, false));
            goto cleanup;
        }
    }

trailing_check:
    time_trace_json_skip_whitespace(&parser);
    if (parser.index != parser.text.length)
    {
        summary_output_print(output, S8("error: failed to parse {S8}: trailing JSON data\n"), path);
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_TRAILING_JSON);
        result = PROCESS_RESULT_FAILED;
    }

cleanup:
    file_map_unmap(map);
    if (!arena_destroy(path_arena, 1))
    {
        result = PROCESS_RESULT_FAILED;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL Arena* time_trace_summary_create_accumulator(SummaryOutput* output, TimeTraceSummaryFailureReason* failure_reason)
{
    u64 accumulator_granularity = BUSTER_KB(64);
    u64 accumulator_size = arena_minimum_position;
    if (sizeof(TimeTraceRowList) > UINT64_MAX - accumulator_size)
    {
        summary_output_print(output, S8("error: time_trace_summary aggregation storage size overflow\n"));
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_RESOURCE);
        return 0;
    }
    accumulator_size += sizeof(TimeTraceRowList);
    if (accumulator_size > UINT64_MAX - (accumulator_granularity - 1))
    {
        summary_output_print(output, S8("error: time_trace_summary aggregation storage alignment overflow\n"));
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_RESOURCE);
        return 0;
    }
    u64 accumulator_reserved_size = align_forward(accumulator_size, accumulator_granularity);
    Arena* result = arena_create((ArenaCreation){
        .reserved_size = accumulator_reserved_size,
        .granularity = accumulator_granularity,
        .initial_size = accumulator_reserved_size,
    });
    if (!result)
    {
        summary_output_print(output, S8("error: time_trace_summary could not reserve aggregation storage\n"));
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_RESOURCE);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_collect(Arena* accumulator_arena, SliceString8 paths, TimeTraceRowList** row_list_out,
                                                            SummaryOutput* output, TimeTraceSummaryFailureReason* failure_reason)
{
    TimeTraceRowList* row_list = arena_allocate(accumulator_arena, TimeTraceRowList, 1);
    memset(row_list, 0, sizeof(*row_list));
    for (u64 i = 0; i < paths.length; i += 1)
    {
        ProcessResult file_result = time_trace_summary_parse_file(paths.pointer[i], row_list, output, failure_reason);
        if (file_result != PROCESS_RESULT_SUCCESS)
        {
            return file_result;
        }
    }
    *row_list_out = row_list;
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_run_with_output(TimeTraceSummaryOptions options, SummaryOutput* output,
                                                                     TimeTraceSummaryFailureReason* failure_reason)
{
    if (!options.limit)
    {
        options.limit = 25;
    }

    if (!options.paths.length)
    {
        summary_output_print(output, S8("error: time_trace_summary requires at least one -ftime-trace JSON path\n"));
        time_trace_summary_failure_set(failure_reason, TIME_TRACE_SUMMARY_FAILURE_RESOURCE);
        return PROCESS_RESULT_FAILED;
    }

    Arena* accumulator_arena = time_trace_summary_create_accumulator(output, failure_reason);
    if (!accumulator_arena)
    {
        return PROCESS_RESULT_FAILED;
    }

    TimeTraceRowList* row_list = 0;
    ProcessResult result = time_trace_summary_collect(accumulator_arena, options.paths, &row_list, output, failure_reason);
    if (result != PROCESS_RESULT_SUCCESS)
    {
        goto cleanup;
    }

    if (!row_list->count)
    {
        summary_output_print(output, S8("No \"Total *\" time-trace entries found.\n"));
        goto cleanup;
    }

    TimeTraceRow* rows = row_list->rows;
    qsort(rows, row_list->count, sizeof(rows[0]), time_trace_row_compare);

    summary_output_print(output, S8("Slowest compiler phases across {u64} time-trace file(s):\n"), options.paths.length);
    u64 limit = BUSTER_MIN(options.limit, row_list->count);
    for (u64 i = 0; i < limit; i += 1)
    {
        TimeTraceRow row = rows[i];
        s64 duration_ms = row.duration_us < 0 ? 0 : row.duration_us / 1000;
        summary_output_print(output, S8("{s64:width=[ ,6]} ms  {S8}\n"), duration_ms, row.name);
    }

cleanup:
    if (!arena_destroy(accumulator_arena, 1))
    {
        result = PROCESS_RESULT_FAILED;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_run(TimeTraceSummaryOptions options)
{
    SummaryOutput output = {.file = os_get_stdout()};
    TimeTraceSummaryFailureReason failure_reason = TIME_TRACE_SUMMARY_FAILURE_NONE;
    return time_trace_summary_run_with_output(options, &output, &failure_reason);
}

BUSTER_GLOBAL_LOCAL bool summary_self_test_write_text(String8 path, String8 text)
{
    return file_write(path, (ByteSlice){.pointer = (u8*)text.pointer, .length = text.length});
}

BUSTER_GLOBAL_LOCAL bool time_trace_summary_self_test_write_large(String8 path, u64* byte_count)
{
    String8 prefix = S8("{\"traceEvents\":[");
    String8 event = S8("{\"ph\":\"X\",\"name\":\"Total Synthetic\",\"dur\":1,\"args\":{\"padding\":\"012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345\"}}");
    String8 suffix = S8("]}");
    u64 event_count = 700000;
    u64 total_size = prefix.length + suffix.length + event.length * event_count + event_count - 1;
    if (total_size <= BUSTER_MB(65))
    {
        return false;
    }

    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.write = 1, .create = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
    if (!file)
    {
        return false;
    }

    os_file_write(file, (ByteSlice){.pointer = (u8*)prefix.pointer, .length = prefix.length});
    char8 chunk[BUSTER_KB(4)];
    u64 chunk_length = 0;
    for (u64 event_i = 0; event_i < event_count; event_i += 1)
    {
        if (event_i)
        {
            if (chunk_length == sizeof(chunk))
            {
                os_file_write(file, (ByteSlice){.pointer = (u8*)chunk, .length = chunk_length});
                chunk_length = 0;
            }
            chunk[chunk_length++] = ',';
        }
        if (chunk_length + event.length > sizeof(chunk))
        {
            os_file_write(file, (ByteSlice){.pointer = (u8*)chunk, .length = chunk_length});
            chunk_length = 0;
        }
        memcpy(chunk + chunk_length, event.pointer, event.length);
        chunk_length += event.length;
    }
    if (chunk_length)
    {
        os_file_write(file, (ByteSlice){.pointer = (u8*)chunk, .length = chunk_length});
    }
    os_file_write(file, (ByteSlice){.pointer = (u8*)suffix.pointer, .length = suffix.length});
    bool result = os_file_close(file);
    if (byte_count)
    {
        *byte_count = total_size;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool time_trace_summary_self_test_write_unique_rows(Arena* arena, String8 path, u64 row_count)
{
    String8 prefix = S8("{\"traceEvents\":[");
    String8 suffix = S8("]}");
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.write = 1, .create = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
    if (!file)
    {
        return false;
    }

    os_file_write(file, (ByteSlice){.pointer = (u8*)prefix.pointer, .length = prefix.length});
    bool result = true;
    for (u64 row_i = 0; row_i < row_count; row_i += 1)
    {
        if (row_i)
        {
            char8 comma = ',';
            os_file_write(file, (ByteSlice){.pointer = (u8*)&comma, .length = 1});
        }
        String8 event = string_format(arena, S8("{{\"ph\":\"X\",\"name\":\"Total Row{u64}\",\"dur\":1}}"), row_i);
        if (!event.pointer)
        {
            result = false;
            break;
        }
        os_file_write(file, (ByteSlice){.pointer = (u8*)event.pointer, .length = event.length});
    }
    os_file_write(file, (ByteSlice){.pointer = (u8*)suffix.pointer, .length = suffix.length});
    result = os_file_close(file) && result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool time_trace_summary_self_test_write_name_cap(String8 path)
{
    String8 prefix = S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total ");
    String8 suffix = S8("\",\"dur\":1}]}");
    u64 repeated_count = BUSTER_KB(512) - 6 + 1;
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.write = 1, .create = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
    if (!file)
    {
        return false;
    }

    os_file_write(file, (ByteSlice){.pointer = (u8*)prefix.pointer, .length = prefix.length});
    char8 chunk[BUSTER_KB(4)];
    memset(chunk, 'n', sizeof(chunk));
    while (repeated_count)
    {
        u64 write_count = BUSTER_MIN(repeated_count, (u64)sizeof(chunk));
        os_file_write(file, (ByteSlice){.pointer = (u8*)chunk, .length = write_count});
        repeated_count -= write_count;
    }
    os_file_write(file, (ByteSlice){.pointer = (u8*)suffix.pointer, .length = suffix.length});
    return os_file_close(file);
}

BUSTER_GLOBAL_LOCAL bool time_trace_summary_self_test_write_depth_cap(String8 path)
{
    String8 prefix = S8("{\"meta\":");
    String8 scalar = S8("0");
    String8 suffix = S8("}");
    u64 nested_count = 257;
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.write = 1, .create = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
    if (!file)
    {
        return false;
    }

    os_file_write(file, (ByteSlice){.pointer = (u8*)prefix.pointer, .length = prefix.length});
    char8 bracket = '[';
    for (u64 i = 0; i < nested_count; i += 1)
    {
        os_file_write(file, (ByteSlice){.pointer = (u8*)&bracket, .length = 1});
    }
    os_file_write(file, (ByteSlice){.pointer = (u8*)scalar.pointer, .length = scalar.length});
    bracket = ']';
    for (u64 i = 0; i < nested_count; i += 1)
    {
        os_file_write(file, (ByteSlice){.pointer = (u8*)&bracket, .length = 1});
    }
    os_file_write(file, (ByteSlice){.pointer = (u8*)suffix.pointer, .length = suffix.length});
    return os_file_close(file);
}

typedef struct TimeTraceSummarySelfTestFailure TimeTraceSummarySelfTestFailure;
struct TimeTraceSummarySelfTestFailure
{
    String8 path;
    TimeTraceSummaryFailureReason reason;
    String8 message;
};

BUSTER_GLOBAL_LOCAL bool time_trace_summary_self_test_expect_failure(Arena* arena, TimeTraceSummarySelfTestFailure failure)
{
    String8 paths[] = {failure.path};
    SummaryOutput output = {.capture_arena = arena};
    TimeTraceSummaryFailureReason actual_reason = TIME_TRACE_SUMMARY_FAILURE_NONE;
    ProcessResult result = time_trace_summary_run_with_output((TimeTraceSummaryOptions){
        .paths = {.pointer = paths, .length = BUSTER_ARRAY_LENGTH(paths)},
        .limit = 5,
    }, &output, &actual_reason);
    String8 actual_output = summary_output_text(arena, output);
    String8 expected_output = string_format(arena, failure.message, failure.path);
    return result != PROCESS_RESULT_SUCCESS && actual_reason == failure.reason && string_equal(actual_output, expected_output);
}

typedef enum SummaryDirectoryClaimResult SummaryDirectoryClaimResult;
enum SummaryDirectoryClaimResult
{
    SUMMARY_DIRECTORY_CLAIMED,
    SUMMARY_DIRECTORY_ALREADY_EXISTS,
    SUMMARY_DIRECTORY_ERROR,
};

BUSTER_GLOBAL_LOCAL SummaryDirectoryClaimResult summary_self_test_directory_claim(String8 path)
{
#if BUSTER_WINDOWS
    TemporalArena temp = scratch_begin(0, 0);
    String16 path_w = string16_from_string8(temp.arena, path, true);
    bool created = CreateDirectoryW(path_w.pointer, 0) != 0;
    DWORD error = created ? ERROR_SUCCESS : GetLastError();
    scratch_end(temp);
    if (created)
    {
        return SUMMARY_DIRECTORY_CLAIMED;
    }
    return error == ERROR_ALREADY_EXISTS ? SUMMARY_DIRECTORY_ALREADY_EXISTS : SUMMARY_DIRECTORY_ERROR;
#elif BUSTER_LINUX || BUSTER_MACOS
    if (mkdir((const char*)path.pointer, 0755) == 0)
    {
        return SUMMARY_DIRECTORY_CLAIMED;
    }
    return errno == EEXIST ? SUMMARY_DIRECTORY_ALREADY_EXISTS : SUMMARY_DIRECTORY_ERROR;
#else
    BUSTER_UNUSED(path);
    return SUMMARY_DIRECTORY_ERROR;
#endif
}

BUSTER_GLOBAL_LOCAL bool summary_self_test_claim_directory(Arena* arena, String8 name, String8* directory_out)
{
    enum
    {
        SUMMARY_SELF_TEST_DIRECTORY_ATTEMPTS = 32,
    };
    make_directory_recursive(arena, S8("build"));
    u64 process_id = os_get_current_process_id();
    u64 timestamp = os_now_microseconds();
    for (u64 attempt = 0; attempt < SUMMARY_SELF_TEST_DIRECTORY_ATTEMPTS; attempt += 1)
    {
        String8 directory_name = string_format(arena, S8("{S8}-self-test-{u64}-{u64}-{u64}"), name, process_id, timestamp, attempt);
        String8 directory = path_join(arena, S8("build"), directory_name);
        SummaryDirectoryClaimResult claim_result = summary_self_test_directory_claim(directory);
        if (claim_result == SUMMARY_DIRECTORY_CLAIMED)
        {
            *directory_out = directory;
            return true;
        }
        if (claim_result == SUMMARY_DIRECTORY_ERROR)
        {
            break;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_self_test(Arena* arena)
{
    String8 directory = {0};
    bool directory_owned = summary_self_test_claim_directory(arena, S8("time-trace-summary"), &directory);
    if (!directory_owned)
    {
        string_print(S8("error: time_trace_summary self-test could not claim an exclusive directory\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 ordinary_a = path_join(arena, directory, S8("ordinary-a.json"));
    String8 ordinary_b = path_join(arena, directory, S8("ordinary-b.json"));
    String8 phase = path_join(arena, directory, S8("phase.json"));
    String8 malformed = path_join(arena, directory, S8("malformed.json"));
    String8 truncated = path_join(arena, directory, S8("truncated.json"));
    String8 trailing = path_join(arena, directory, S8("trailing.json"));
    String8 decimal = path_join(arena, directory, S8("decimal.json"));
    String8 exponent = path_join(arena, directory, S8("exponent.json"));
    String8 whitespace_ff = path_join(arena, directory, S8("whitespace-ff.json"));
    String8 whitespace_vt = path_join(arena, directory, S8("whitespace-vt.json"));
    String8 duration_overflow = path_join(arena, directory, S8("duration-overflow.json"));
    String8 row_cap = path_join(arena, directory, S8("row-cap.json"));
    String8 name_cap = path_join(arena, directory, S8("name-cap.json"));
    String8 depth_cap = path_join(arena, directory, S8("depth-cap.json"));
    String8 invalid_surrogate_high = path_join(arena, directory, S8("invalid-surrogate-high.json"));
    String8 invalid_surrogate_low = path_join(arena, directory, S8("invalid-surrogate-low.json"));
    String8 large = path_join(arena, directory, S8("large.json"));
    bool passed = true;
    String8 existing_candidate = path_join(arena, directory, S8("already-exists"));
    String8 existing_sentinel = path_join(arena, existing_candidate, S8("sentinel.txt"));
    make_directory_recursive(arena, existing_candidate);
    bool existing_sentinel_written = summary_self_test_write_text(existing_sentinel, S8("preserve"));
    SummaryDirectoryClaimResult existing_claim = SUMMARY_DIRECTORY_ERROR;
    if (existing_sentinel_written)
    {
        existing_claim = summary_self_test_directory_claim(existing_candidate);
    }
    ByteSlice existing_contents = {0};
    if (existing_claim == SUMMARY_DIRECTORY_ALREADY_EXISTS)
    {
        existing_contents = file_read(arena, existing_sentinel, (FileReadOptions){.map_required = 0});
    }
    String8 preserved_text = S8("preserve");
    bool existing_candidate_preserved = existing_claim == SUMMARY_DIRECTORY_ALREADY_EXISTS && existing_contents.pointer &&
                                        existing_contents.length == preserved_text.length &&
                                        memcmp(existing_contents.pointer, preserved_text.pointer, preserved_text.length) == 0;
    passed = passed && existing_candidate_preserved;

    passed = passed && summary_self_test_write_text(
                            ordinary_a,
                            S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total Backend\",\"dur\":1500},{\"ph\":\"X\",\"name\":\"Total Backend\",\"dur\":500},{\"\\u0070h\":\"X\",\"\\u006eame\":\"Total \\u0046rontend\",\"\\u0064ur\":5,\"args\":{\"nested\":[true,{\"x\":\"y\"}]}},{\"ph\":\"X\",\"name\":\"Total \\ud83d\\ude00\",\"dur\":3}]}"));
    passed = passed && summary_self_test_write_text(
                            ordinary_b,
                            S8("{\"traceEvents\":[{\"\\u0070h\":\"X\",\"\\u006eame\":\"Total \\u0042ackend\",\"\\u0064ur\":7},{\"ph\":\"X\",\"name\":\"Total CodeGen\",\"dur\":9},{\"ph\":\"X\",\"name\":\"Total \xf0\x9f\x98\x80\",\"dur\":4}]}"));
    passed = passed && summary_self_test_write_text(
                            phase,
                            S8("{\"traceEvents\":[{\"ph\":\"\\u0058\",\"name\":\"Total Phase\",\"dur\":4},{\"ph\":\"X\",\"name\":\"Total Phase\",\"dur\":6},{\"ph\":\"\\u0158\",\"name\":\"Total Ignored\",\"dur\":8},{\"ph\":\"XX\",\"name\":\"Total Ignored2\",\"dur\":16}]}"));
    passed = passed && summary_self_test_write_text(
                            malformed,
                            S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total Bad\",\"dur\":1,\"args\":{\"bad\":1,}}]}"));
    passed = passed && summary_self_test_write_text(truncated, S8("{\"traceEvents\":[{\"ph\":\"X\"}"));
    passed = passed && summary_self_test_write_text(trailing, S8("{} trailing"));
    passed = passed && summary_self_test_write_text(decimal, S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total Decimal\",\"dur\":1500.9}]}"));
    passed = passed && summary_self_test_write_text(exponent, S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total Exponent\",\"dur\":2e6}]}"));
    passed = passed && summary_self_test_write_text(whitespace_ff, S8("{\f\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total FF\",\"dur\":1}]}"));
    passed = passed && summary_self_test_write_text(whitespace_vt, S8("{\v\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total VT\",\"dur\":1}]}"));
    passed = passed && summary_self_test_write_text(
                            duration_overflow,
                            S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total Overflow\",\"dur\":9223372036854775807},{\"ph\":\"X\",\"name\":\"Total Overflow\",\"dur\":1}]}"));
    passed = passed && time_trace_summary_self_test_write_unique_rows(arena, row_cap, 4097);
    passed = passed && time_trace_summary_self_test_write_name_cap(name_cap);
    passed = passed && time_trace_summary_self_test_write_depth_cap(depth_cap);
    passed = passed && summary_self_test_write_text(
                            invalid_surrogate_high,
                            S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total \\ud83d\",\"dur\":1}]}"));
    passed = passed && summary_self_test_write_text(
                            invalid_surrogate_low,
                            S8("{\"traceEvents\":[{\"ph\":\"X\",\"name\":\"Total \\ude00\",\"dur\":1}]}"));

    if (passed)
    {
        String8 ordinary_paths[] = {ordinary_a, ordinary_b};
        SummaryOutput aggregation_output = {.capture_arena = arena};
        TimeTraceSummaryFailureReason aggregation_failure = TIME_TRACE_SUMMARY_FAILURE_NONE;
        Arena* accumulator_arena = time_trace_summary_create_accumulator(&aggregation_output, &aggregation_failure);
        TimeTraceRowList* row_list = 0;
        if (!accumulator_arena || time_trace_summary_collect(accumulator_arena, (SliceString8){.pointer = ordinary_paths, .length = 2}, &row_list,
                                                              &aggregation_output, &aggregation_failure) != PROCESS_RESULT_SUCCESS)
        {
            passed = false;
        }
        else
        {
            qsort(row_list->rows, row_list->count, sizeof(row_list->rows[0]), time_trace_row_compare);
            TimeTraceRow expected[] = {
                {.name = S8("Total Backend"), .duration_us = 2007},
                {.name = S8("Total CodeGen"), .duration_us = 9},
                {.name = S8("Total \xf0\x9f\x98\x80"), .duration_us = 7},
                {.name = S8("Total Frontend"), .duration_us = 5},
            };
            if (row_list->count != BUSTER_ARRAY_LENGTH(expected))
            {
                passed = false;
            }
            else
            {
                for (u64 row_i = 0; row_i < row_list->count; row_i += 1)
                {
                    if (!string_equal(row_list->rows[row_i].name, expected[row_i].name) || row_list->rows[row_i].duration_us != expected[row_i].duration_us)
                    {
                        passed = false;
                        break;
                    }
                }
            }
        }
        if (accumulator_arena && !arena_destroy(accumulator_arena, 1))
        {
            passed = false;
        }

        String8 phase_paths[] = {phase};
        aggregation_output = (SummaryOutput){.capture_arena = arena};
        aggregation_failure = TIME_TRACE_SUMMARY_FAILURE_NONE;
        accumulator_arena = time_trace_summary_create_accumulator(&aggregation_output, &aggregation_failure);
        row_list = 0;
        if (!accumulator_arena || time_trace_summary_collect(accumulator_arena, (SliceString8){.pointer = phase_paths, .length = 1}, &row_list,
                                                              &aggregation_output, &aggregation_failure) != PROCESS_RESULT_SUCCESS ||
            row_list->count != 1 || !string_equal(row_list->rows[0].name, S8("Total Phase")) || row_list->rows[0].duration_us != 10)
        {
            passed = false;
        }
        if (accumulator_arena && !arena_destroy(accumulator_arena, 1))
        {
            passed = false;
        }

        SummaryOutput ordinary_output = {.capture_arena = arena};
        TimeTraceSummaryFailureReason ordinary_failure = TIME_TRACE_SUMMARY_FAILURE_NONE;
        ProcessResult ordinary_result = time_trace_summary_run_with_output((TimeTraceSummaryOptions){
                                                                                .paths = {.pointer = ordinary_paths, .length = 2},
                                                                                .limit = 3,
                                                                            },
                                                                            &ordinary_output, &ordinary_failure);
        String8 ordinary_output_text = summary_output_text(arena, ordinary_output);
        passed = passed && ordinary_result == PROCESS_RESULT_SUCCESS && ordinary_failure == TIME_TRACE_SUMMARY_FAILURE_NONE &&
                 string_equal(ordinary_output_text,
                              S8("Slowest compiler phases across 2 time-trace file(s):\n"
                                 "     2 ms  Total Backend\n"
                                 "     0 ms  Total CodeGen\n"
                                 "     0 ms  Total \xf0\x9f\x98\x80\n"));
    }

    u64 large_size = 0;
    passed = passed && time_trace_summary_self_test_write_large(large, &large_size) && large_size > BUSTER_MB(65);
    if (passed)
    {
        String8 large_paths[] = {large};
        SummaryOutput large_output = {.capture_arena = arena};
        TimeTraceSummaryFailureReason large_failure = TIME_TRACE_SUMMARY_FAILURE_NONE;
        ProcessResult large_result = time_trace_summary_run_with_output((TimeTraceSummaryOptions){
                                                                            .paths = {.pointer = large_paths, .length = 1},
                                                                            .limit = 1,
                                                                        },
                                                                        &large_output, &large_failure);
        String8 large_output_text = summary_output_text(arena, large_output);
        passed = large_result == PROCESS_RESULT_SUCCESS && large_failure == TIME_TRACE_SUMMARY_FAILURE_NONE &&
                 string_equal(large_output_text,
                              S8("Slowest compiler phases across 1 time-trace file(s):\n"
                                 "   700 ms  Total Synthetic\n"));
    }

    TimeTraceSummarySelfTestFailure failure_cases[] = {
        {.path = malformed,
         .reason = TIME_TRACE_SUMMARY_FAILURE_MALFORMED_JSON,
         .message = S8("error: failed to parse {S8}: invalid trace event\n")},
        {.path = truncated,
         .reason = TIME_TRACE_SUMMARY_FAILURE_TRUNCATED_JSON,
         .message = S8("error: failed to parse {S8}: expected ',' or ']'\n")},
        {.path = trailing,
         .reason = TIME_TRACE_SUMMARY_FAILURE_TRAILING_JSON,
         .message = S8("error: failed to parse {S8}: trailing JSON data\n")},
        {.path = decimal,
         .reason = TIME_TRACE_SUMMARY_FAILURE_NON_INTEGER_DURATION,
         .message = S8("error: failed to parse {S8}: duration must be an integer number of microseconds\n")},
        {.path = exponent,
         .reason = TIME_TRACE_SUMMARY_FAILURE_NON_INTEGER_DURATION,
         .message = S8("error: failed to parse {S8}: duration must be an integer number of microseconds\n")},
        {.path = whitespace_ff,
         .reason = TIME_TRACE_SUMMARY_FAILURE_INVALID_WHITESPACE,
         .message = S8("error: failed to parse {S8}: expected an object key\n")},
        {.path = whitespace_vt,
         .reason = TIME_TRACE_SUMMARY_FAILURE_INVALID_WHITESPACE,
         .message = S8("error: failed to parse {S8}: expected an object key\n")},
        {.path = duration_overflow,
         .reason = TIME_TRACE_SUMMARY_FAILURE_DURATION_OVERFLOW,
         .message = S8("error: time_trace_summary duration overflow while aggregating {S8}\n")},
        {.path = row_cap,
         .reason = TIME_TRACE_SUMMARY_FAILURE_ROW_LIMIT,
         .message = S8("error: time_trace_summary resource limit while aggregating {S8}\n")},
        {.path = name_cap,
         .reason = TIME_TRACE_SUMMARY_FAILURE_NAME_LIMIT,
         .message = S8("error: time_trace_summary resource limit while aggregating {S8}\n")},
        {.path = depth_cap,
         .reason = TIME_TRACE_SUMMARY_FAILURE_DEPTH_LIMIT,
         .message = S8("error: time_trace_summary resource limit while parsing {S8}\n")},
        {.path = invalid_surrogate_high,
         .reason = TIME_TRACE_SUMMARY_FAILURE_INVALID_STRING,
         .message = S8("error: failed to parse {S8}: invalid Unicode string escape\n")},
        {.path = invalid_surrogate_low,
         .reason = TIME_TRACE_SUMMARY_FAILURE_INVALID_STRING,
         .message = S8("error: failed to parse {S8}: invalid Unicode string escape\n")},
    };
    for (u64 failure_i = 0; failure_i < BUSTER_ARRAY_LENGTH(failure_cases); failure_i += 1)
    {
        bool failure_passed = time_trace_summary_self_test_expect_failure(arena, failure_cases[failure_i]);
        passed = passed && failure_passed;
    }

    if (directory_owned && !os_directory_delete(directory))
    {
        passed = false;
    }
    if (!passed)
    {
        string_print(S8("error: time_trace_summary self-test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("TIME_TRACE_SUMMARY_SELF_TEST passed small=1 multi_file=1 escaped=1 large_bytes={u64} negative_cases={u64} cleanup=1\n"), large_size,
                 BUSTER_ARRAY_LENGTH(failure_cases));
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult time_trace_summary_action(Arena* arena, void* data)
{
    BUSTER_UNUSED(arena);
    TimeTraceSummaryOptions* options = (TimeTraceSummaryOptions*)data;
    return time_trace_summary_run(*options);
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

// --- test_timing_summary ---------------------------------------------------
// Diagnostic only: parses the `TEST_MODULE_TIMING` lines `library_tests()`
// prints under `--verbose=1` (which every CI job already passes) out of a saved
// matrix run or CI log, aggregates them per module across the configurations in
// that run, and reports per-module deltas against a stored baseline.
//
// This exists because test cost accumulates unnoticed: `x86_64_metadata_tests`
// grew until it was 60% of all CI test CPU time, and every number needed to
// catch it in week one was already printed on every run and then thrown away
// with the log. Recording the history is the whole point.
//
// Two properties are deliberate:
//
//   * A module's cost is a property of one runner and one configuration, not of
//     the module. The same `x86_64_metadata_tests` measured 4.4 s in Linux
//     Release, 138 s in sanitized Debug, and 192 s on a sanitized Debug Windows
//     runner. Those are three series; a baseline records the runner it was
//     taken on and refuses to be compared against another, and per-module rows
//     are only ever compared within the same configuration.
//   * It never fails. Wall-clock test time is far too noisy to gate on --
//     identical code measured 290.1 s and 319.8 s for the same matrix on an
//     otherwise idle 16-thread desktop, a 10% spread -- so there is no
//     threshold here and no CI failure condition. Record history first; a gate
//     needs a noise model that does not exist yet.
//
// Configurations are recovered from the build commands the superbuild echoes
// ahead of each test block (`cmake --build <tree> --config <cfg> --target
// test_units`). Ninja buffers an edge's output and prints it on completion, so
// a block is contiguous, and each new command line replaces the pending
// configuration list rather than appending to it -- a test that printed no
// timing rows can therefore only lose its own block, never shift every block
// after it. Timing rows with no command line in front of them (a saved plain
// `ide test` run) are attributed to a numbered `unknown:<n>` configuration so
// that separate runs still never merge.

typedef struct TestTimingSummaryOptions TestTimingSummaryOptions;
struct TestTimingSummaryOptions
{
    SliceString8 paths;
    String8 baseline;
    String8 runner;
    u64 limit;
    u32 update_baseline : 1;
};

typedef enum TestTimingSummaryFailureReason TestTimingSummaryFailureReason;
enum TestTimingSummaryFailureReason
{
    TEST_TIMING_SUMMARY_FAILURE_NONE,
    TEST_TIMING_SUMMARY_FAILURE_RESOURCE,
    TEST_TIMING_SUMMARY_FAILURE_UNREADABLE_LOG,
    TEST_TIMING_SUMMARY_FAILURE_MALFORMED_BASELINE,
    TEST_TIMING_SUMMARY_FAILURE_BASELINE_RUNNER_MISMATCH,
    TEST_TIMING_SUMMARY_FAILURE_BASELINE_WRITE,
};

enum
{
    // A compiler tree runs at most a unit-test and an all-test command.
    TEST_TIMING_SUMMARY_PENDING_LIMIT = 8,
    TEST_TIMING_BASELINE_VERSION = 1,
};

typedef struct TestTimingRow TestTimingRow;
struct TestTimingRow
{
    String8 configuration;
    String8 module;
    u64 duration_ns;
    u64 samples;
    u64 failed;
    u64 baseline_ns;
    u32 has_baseline : 1;
};

typedef struct TestTimingRowNode TestTimingRowNode;
struct TestTimingRowNode
{
    TestTimingRow row;
    TestTimingRowNode* next;
};

typedef struct TestTimingRowList TestTimingRowList;
struct TestTimingRowList
{
    TestTimingRowNode* first;
    TestTimingRowNode* last;
    u64 count;
};

typedef struct TestTimingModuleTotal TestTimingModuleTotal;
struct TestTimingModuleTotal
{
    String8 module;
    u64 duration_ns;
    u64 configurations;
};

typedef struct TestTimingBaseline TestTimingBaseline;
struct TestTimingBaseline
{
    String8 runner;
    TestTimingRowList rows;
    u32 present : 1;
};

BUSTER_GLOBAL_LOCAL void test_timing_summary_failure_set(TestTimingSummaryFailureReason* failure_reason, TestTimingSummaryFailureReason reason)
{
    if (failure_reason)
    {
        *failure_reason = reason;
    }
}

// A baseline belongs to the machine that recorded it. Every runner keys its own
// series, and none is excluded from tracking -- including a shared or noisy one,
// whose noise is itself worth having on record.
BUSTER_GLOBAL_LOCAL String8 test_timing_summary_runner_name(Arena* arena)
{
    String8 configured = os_get_environment_variable(S8("BUSTER_TEST_TIMING_RUNNER"));
    if (configured.pointer && configured.length)
    {
        return configured;
    }

#if BUSTER_WINDOWS
    String8 platform = S8("windows");
#elif BUSTER_MACOS
    String8 platform = S8("macos");
#elif BUSTER_LINUX
    String8 platform = S8("linux");
#else
    String8 platform = S8("unknown");
#endif

#if BUSTER_CPU_ARCH_X86_64
    String8 architecture = S8("x86_64");
#elif BUSTER_CPU_ARCH_AARCH64
    String8 architecture = S8("aarch64");
#else
    String8 architecture = S8("unknown");
#endif

    return string_format(arena, S8("{S8}-{S8}"), platform, architecture);
}

BUSTER_GLOBAL_LOCAL String8 test_timing_summary_default_baseline(Arena* arena, String8 runner)
{
    return path_join(arena, S8("build"), string_format(arena, S8("test-timing-baseline-{S8}.txt"), runner));
}

// Splits one whitespace-delimited token off the front of `text`.
BUSTER_GLOBAL_LOCAL bool test_timing_next_token(String8* text, String8* token)
{
    u64 start = 0;
    while (start < text->length && (text->pointer[start] == ' ' || text->pointer[start] == '\t' || text->pointer[start] == '\r'))
    {
        start += 1;
    }
    if (start == text->length)
    {
        *text = (String8){0};
        return false;
    }

    u64 end = start;
    while (end < text->length && text->pointer[end] != ' ' && text->pointer[end] != '\t' && text->pointer[end] != '\r')
    {
        end += 1;
    }

    *token = string_slice(*text, start, end);
    *text = string_slice(*text, end, text->length);
    return true;
}

BUSTER_GLOBAL_LOCAL bool test_timing_next_line(String8* text, String8* line)
{
    if (!text->length)
    {
        return false;
    }

    u64 newline_index = string_first_code_unit(*text, '\n');
    bool is_last_line = newline_index == BUSTER_STRING_NO_MATCH;
    u64 line_end = is_last_line ? text->length : newline_index;
    *line = string_slice(*text, 0, line_end);
    *text = is_last_line ? (String8){0} : string_slice(*text, line_end + 1, text->length);
    return true;
}

BUSTER_GLOBAL_LOCAL bool test_timing_split_field(String8 token, String8* key, String8* value)
{
    u64 equals_index = string_first_code_unit(token, '=');
    if (equals_index == BUSTER_STRING_NO_MATCH)
    {
        return false;
    }

    *key = string_slice(token, 0, equals_index);
    *value = string_slice(token, equals_index + 1, token.length);
    return true;
}

BUSTER_GLOBAL_LOCAL bool test_timing_parse_u64(String8 value, u64* result)
{
    if (!value.length)
    {
        return false;
    }

    IntegerParsingU64 parsed = string8_parse_u64_decimal(value.pointer);
    if (parsed.length != value.length)
    {
        return false;
    }

    *result = parsed.value;
    return true;
}

// `build/build-ci_on-cc_gcc-...-configs_shared` plus `Release` becomes
// `ci_on-cc_gcc-...-configs_shared:Release`. The leading directory and the
// `build-` prefix carry no identity and would only differ between checkouts.
BUSTER_GLOBAL_LOCAL String8 test_timing_configuration_key(Arena* arena, String8 build_directory, String8 config)
{
    String8 name = build_directory;
    for (u64 i = name.length; i > 0; i -= 1)
    {
        char8 code_unit = name.pointer[i - 1];
        if (code_unit == '/' || code_unit == '\\')
        {
            name = string_slice(name, i, name.length);
            break;
        }
    }
    if (string_starts_with_sequence(name, S8("build-")))
    {
        name = string_slice(name, 6, name.length);
    }
    return string_format(arena, S8("{S8}:{S8}"), name, config);
}

BUSTER_GLOBAL_LOCAL bool test_timing_target_runs_tests(String8 target)
{
    return string_equal(target, S8("test_units")) || string_equal(target, S8("test_all"));
}

// Collects the (build directory, configuration) pairs of every test command on
// one echoed command line. A tree that runs both a unit-test and an all-test
// command emits them as one `&&`-joined line, so a line can describe more than
// one configuration and their order is the order the runs appear in.
BUSTER_GLOBAL_LOCAL u64 test_timing_scan_command_line(Arena* arena, String8 line, String8* pending, u64 pending_limit)
{
    String8 rest = line;
    String8 token = {0};
    String8 build_directory = {0};
    String8 config = {0};
    bool expect_build_directory = false;
    bool expect_config = false;
    bool in_targets = false;
    u64 count = 0;

    while (test_timing_next_token(&rest, &token))
    {
        if (string_equal(token, S8("--build")))
        {
            expect_build_directory = true;
            expect_config = false;
            in_targets = false;
        }
        else if (string_equal(token, S8("--config")))
        {
            expect_config = true;
            expect_build_directory = false;
            in_targets = false;
        }
        else if (string_equal(token, S8("--target")))
        {
            expect_build_directory = false;
            expect_config = false;
            in_targets = true;
        }
        else if (expect_build_directory)
        {
            build_directory = token;
            expect_build_directory = false;
        }
        else if (expect_config)
        {
            config = token;
            expect_config = false;
        }
        else if (in_targets)
        {
            if (string_starts_with_sequence(token, S8("-")))
            {
                in_targets = false;
            }
            else if (test_timing_target_runs_tests(token) && build_directory.length && config.length && count < pending_limit)
            {
                pending[count++] = test_timing_configuration_key(arena, build_directory, config);
            }
        }
    }

    return count;
}

BUSTER_GLOBAL_LOCAL TestTimingRow* test_timing_row_find(TestTimingRowList* list, String8 configuration, String8 module)
{
    for (TestTimingRowNode* node = list->first; node; node = node->next)
    {
        if (string_equal(node->row.configuration, configuration) && string_equal(node->row.module, module))
        {
            return &node->row;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void test_timing_row_accumulate(Arena* arena, TestTimingRowList* list, TestTimingRow row)
{
    TestTimingRow* existing = test_timing_row_find(list, row.configuration, row.module);
    if (existing)
    {
        existing->duration_ns += row.duration_ns;
        existing->samples += row.samples;
        existing->failed += row.failed;
        return;
    }

    TestTimingRowNode* node = arena_allocate(arena, TestTimingRowNode, 1);
    *node = (TestTimingRowNode){.row = row};
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

BUSTER_GLOBAL_LOCAL int test_timing_row_compare(const void* a, const void* b)
{
    const TestTimingRow* left = (const TestTimingRow*)a;
    const TestTimingRow* right = (const TestTimingRow*)b;
    return (left->duration_ns < right->duration_ns) - (left->duration_ns > right->duration_ns);
}

BUSTER_GLOBAL_LOCAL int test_timing_module_total_compare(const void* a, const void* b)
{
    const TestTimingModuleTotal* left = (const TestTimingModuleTotal*)a;
    const TestTimingModuleTotal* right = (const TestTimingModuleTotal*)b;
    return (left->duration_ns < right->duration_ns) - (left->duration_ns > right->duration_ns);
}

BUSTER_GLOBAL_LOCAL u64 test_timing_duration_ms(u64 duration_ns)
{
    return (duration_ns + 500000) / 1000000;
}

// Tenths of a percent of `total`, rounded. Durations are nanoseconds, so the
// numerator stays far below the 64-bit range for any plausible test run.
BUSTER_GLOBAL_LOCAL u64 test_timing_share_tenths(u64 part, u64 total)
{
    if (!total || part > (UINT64_MAX - total / 2) / 1000)
    {
        return 0;
    }
    return (part * 1000 + total / 2) / total;
}

BUSTER_GLOBAL_LOCAL ProcessResult test_timing_summary_parse_log(Arena* arena, String8 path, TestTimingRowList* rows, SummaryOutput* output,
                                                               TestTimingSummaryFailureReason* failure_reason)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});
    if (!bytes.pointer)
    {
        summary_output_print(output, S8("error: failed to read {S8}\n"), path);
        test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_UNREADABLE_LOG);
        return PROCESS_RESULT_FAILED;
    }

    String8 text = BYTE_SLICE_TO_STRING(8, bytes);
    String8 pending[TEST_TIMING_SUMMARY_PENDING_LIMIT] = {0};
    u64 pending_count = 0;
    u64 pending_index = 0;
    String8 configuration = {0};
    u64 unknown_count = 0;
    u64 previous_index = 0;
    String8 line = {0};

    while (test_timing_next_line(&text, &line))
    {
        if (!string_starts_with_sequence(line, S8("TEST_MODULE_TIMING ")))
        {
            u64 found = test_timing_scan_command_line(arena, line, pending, BUSTER_ARRAY_LENGTH(pending));
            if (found)
            {
                pending_count = found;
                pending_index = 0;
            }
            continue;
        }

        String8 rest = string_slice(line, 0, line.length);
        String8 token = {0};
        String8 module = {0};
        u64 index = 0;
        u64 duration_ns = 0;
        u64 failed = 0;
        bool has_index = false;
        bool has_duration = false;

        while (test_timing_next_token(&rest, &token))
        {
            String8 key = {0};
            String8 value = {0};
            if (!test_timing_split_field(token, &key, &value))
            {
                continue;
            }

            if (string_equal(key, S8("index")))
            {
                has_index = test_timing_parse_u64(value, &index);
            }
            else if (string_equal(key, S8("module")))
            {
                module = value;
            }
            else if (string_equal(key, S8("duration_ns")))
            {
                has_duration = test_timing_parse_u64(value, &duration_ns);
            }
            else if (string_equal(key, S8("failed")))
            {
                test_timing_parse_u64(value, &failed);
            }
        }

        if (!has_index || !has_duration || !module.length)
        {
            continue;
        }

        // Module indexes restart at zero for every `ide test` process, which is
        // the only in-band marker of where one configuration's run ends and the
        // next begins.
        bool run_started = !configuration.length || index <= previous_index;
        previous_index = index;
        if (run_started)
        {
            if (pending_index < pending_count)
            {
                configuration = pending[pending_index++];
            }
            else
            {
                configuration = string_format(arena, S8("unknown:{u64}"), unknown_count++);
            }
        }

        test_timing_row_accumulate(arena, rows,
                                   (TestTimingRow){
                                       .configuration = configuration,
                                       .module = module,
                                       .duration_ns = duration_ns,
                                       .samples = 1,
                                       .failed = failed,
                                   });
    }

    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult test_timing_baseline_read(Arena* arena, String8 path, String8 runner, TestTimingBaseline* baseline,
                                                            SummaryOutput* output, TestTimingSummaryFailureReason* failure_reason)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});
    if (!bytes.pointer)
    {
        // A missing baseline is the ordinary first run, not an error.
        return PROCESS_RESULT_SUCCESS;
    }

    String8 text = BYTE_SLICE_TO_STRING(8, bytes);
    String8 line = {0};
    bool version_seen = false;

    while (test_timing_next_line(&text, &line))
    {
        String8 rest = line;
        String8 token = {0};
        if (!test_timing_next_token(&rest, &token) || string_starts_with_sequence(token, S8("#")))
        {
            continue;
        }

        if (string_equal(token, S8("TEST_TIMING_BASELINE_VERSION")))
        {
            u64 version = 0;
            if (!test_timing_next_token(&rest, &token) || !test_timing_parse_u64(token, &version) || version != TEST_TIMING_BASELINE_VERSION)
            {
                summary_output_print(output, S8("error: {S8} is not a version {u64} test timing baseline\n"), path,
                                     (u64)TEST_TIMING_BASELINE_VERSION);
                test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_MALFORMED_BASELINE);
                return PROCESS_RESULT_FAILED;
            }
            version_seen = true;
            continue;
        }

        if (string_equal(token, S8("TEST_TIMING_BASELINE_RUNNER")))
        {
            if (!test_timing_next_token(&rest, &token))
            {
                summary_output_print(output, S8("error: {S8} has no baseline runner\n"), path);
                test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_MALFORMED_BASELINE);
                return PROCESS_RESULT_FAILED;
            }
            baseline->runner = token;
            continue;
        }

        if (!string_equal(token, S8("TEST_TIMING_BASELINE")))
        {
            summary_output_print(output, S8("error: {S8} has an unrecognized baseline record\n"), path);
            test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_MALFORMED_BASELINE);
            return PROCESS_RESULT_FAILED;
        }

        String8 configuration = {0};
        String8 module = {0};
        u64 duration_ns = 0;
        u64 samples = 0;
        bool has_duration = false;
        while (test_timing_next_token(&rest, &token))
        {
            String8 key = {0};
            String8 value = {0};
            if (!test_timing_split_field(token, &key, &value))
            {
                continue;
            }

            if (string_equal(key, S8("config")))
            {
                configuration = value;
            }
            else if (string_equal(key, S8("module")))
            {
                module = value;
            }
            else if (string_equal(key, S8("duration_ns")))
            {
                has_duration = test_timing_parse_u64(value, &duration_ns);
            }
            else if (string_equal(key, S8("samples")))
            {
                test_timing_parse_u64(value, &samples);
            }
        }

        if (!configuration.length || !module.length || !has_duration)
        {
            summary_output_print(output, S8("error: {S8} has an incomplete baseline record\n"), path);
            test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_MALFORMED_BASELINE);
            return PROCESS_RESULT_FAILED;
        }

        test_timing_row_accumulate(arena, &baseline->rows,
                                   (TestTimingRow){
                                       .configuration = configuration,
                                       .module = module,
                                       .duration_ns = duration_ns,
                                       .samples = samples,
                                   });
    }

    if (!version_seen || !baseline->runner.length)
    {
        summary_output_print(output, S8("error: {S8} is missing its baseline version or runner header\n"), path);
        test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_MALFORMED_BASELINE);
        return PROCESS_RESULT_FAILED;
    }

    // Merging runners would average a 4.4 s Linux Release module with a 192 s
    // Windows sanitized Debug one and describe neither.
    if (!string_equal(baseline->runner, runner))
    {
        summary_output_print(output, S8("error: {S8} was recorded on runner {S8}, not {S8}\n"), path, baseline->runner, runner);
        test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_BASELINE_RUNNER_MISMATCH);
        return PROCESS_RESULT_FAILED;
    }

    baseline->present = 1;
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult test_timing_baseline_write(Arena* arena, String8 path, String8 runner, TestTimingRow* rows, u64 row_count,
                                                             SummaryOutput* output, TestTimingSummaryFailureReason* failure_reason)
{
    String8List lines = {0};
    string8_list_push(arena, &lines, string_format(arena, S8("TEST_TIMING_BASELINE_VERSION {u64}\n"), (u64)TEST_TIMING_BASELINE_VERSION));
    string8_list_push(arena, &lines, string_format(arena, S8("TEST_TIMING_BASELINE_RUNNER {S8}\n"), runner));
    for (u64 row_i = 0; row_i < row_count; row_i += 1)
    {
        TestTimingRow row = rows[row_i];
        string8_list_push(arena, &lines,
                          string_format(arena, S8("TEST_TIMING_BASELINE config={S8} module={S8} duration_ns={u64} samples={u64}\n"), row.configuration,
                                        row.module, row.duration_ns, row.samples));
    }

    String8 contents = string_join_arena(arena, string8_list_to_slice(arena, lines), false);
    String8 parent = path_parent(arena, path);
    if (parent.length)
    {
        make_directory_recursive(arena, parent);
    }

    if (!file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(contents)))
    {
        summary_output_print(output, S8("error: failed to write {S8}\n"), path);
        test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_BASELINE_WRITE);
        return PROCESS_RESULT_FAILED;
    }

    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult test_timing_summary_run_with_output(Arena* arena, TestTimingSummaryOptions options, SummaryOutput* output,
                                                                      TestTimingSummaryFailureReason* failure_reason)
{
    if (!options.limit)
    {
        options.limit = 25;
    }

    if (!options.paths.length)
    {
        summary_output_print(output, S8("error: test_timing_summary requires at least one test log path\n"));
        test_timing_summary_failure_set(failure_reason, TEST_TIMING_SUMMARY_FAILURE_RESOURCE);
        return PROCESS_RESULT_FAILED;
    }

    String8 runner = options.runner.length ? options.runner : test_timing_summary_runner_name(arena);
    String8 baseline_path = options.baseline.length ? options.baseline : test_timing_summary_default_baseline(arena, runner);

    TestTimingRowList row_list = {0};
    for (u64 path_i = 0; path_i < options.paths.length; path_i += 1)
    {
        ProcessResult parse_result = test_timing_summary_parse_log(arena, options.paths.pointer[path_i], &row_list, output, failure_reason);
        if (parse_result != PROCESS_RESULT_SUCCESS)
        {
            return parse_result;
        }
    }

    if (!row_list.count)
    {
        summary_output_print(output, S8("No TEST_MODULE_TIMING rows found; test logs must come from a --verbose=1 run.\n"));
        return PROCESS_RESULT_SUCCESS;
    }

    TestTimingBaseline baseline = {0};
    ProcessResult baseline_result = test_timing_baseline_read(arena, baseline_path, runner, &baseline, output, failure_reason);
    if (baseline_result != PROCESS_RESULT_SUCCESS)
    {
        return baseline_result;
    }

    TestTimingRow* rows = arena_allocate(arena, TestTimingRow, row_list.count);
    TestTimingModuleTotal* module_totals = arena_allocate(arena, TestTimingModuleTotal, row_list.count);
    String8* configurations = arena_allocate(arena, String8, row_list.count);
    u64 row_count = 0;
    u64 module_count = 0;
    u64 configuration_count = 0;
    u64 total_ns = 0;
    u64 failed_modules = 0;
    u64 slower = 0;
    u64 faster = 0;
    u64 unchanged = 0;
    u64 added = 0;

    for (TestTimingRowNode* node = row_list.first; node; node = node->next)
    {
        TestTimingRow row = node->row;
        TestTimingRow* baseline_row = test_timing_row_find(&baseline.rows, row.configuration, row.module);
        if (baseline_row)
        {
            row.baseline_ns = baseline_row->duration_ns;
            row.has_baseline = 1;
        }

        if (baseline.present)
        {
            if (!row.has_baseline)
            {
                added += 1;
            }
            else if (row.duration_ns > row.baseline_ns)
            {
                slower += 1;
            }
            else if (row.duration_ns < row.baseline_ns)
            {
                faster += 1;
            }
            else
            {
                unchanged += 1;
            }
        }

        total_ns += row.duration_ns;
        failed_modules += row.failed != 0;

        u64 module_i = 0;
        for (; module_i < module_count; module_i += 1)
        {
            if (string_equal(module_totals[module_i].module, row.module))
            {
                break;
            }
        }
        if (module_i == module_count)
        {
            module_totals[module_count++] = (TestTimingModuleTotal){.module = row.module};
        }
        module_totals[module_i].duration_ns += row.duration_ns;
        module_totals[module_i].configurations += 1;

        u64 configuration_i = 0;
        for (; configuration_i < configuration_count; configuration_i += 1)
        {
            if (string_equal(configurations[configuration_i], row.configuration))
            {
                break;
            }
        }
        if (configuration_i == configuration_count)
        {
            configurations[configuration_count++] = row.configuration;
        }

        rows[row_count++] = row;
    }

    u64 missing = 0;
    for (TestTimingRowNode* node = baseline.rows.first; baseline.present && node; node = node->next)
    {
        missing += test_timing_row_find(&row_list, node->row.configuration, node->row.module) == 0;
    }

    qsort(module_totals, module_count, sizeof(module_totals[0]), test_timing_module_total_compare);
    qsort(rows, row_count, sizeof(rows[0]), test_timing_row_compare);

    // The headline number is the module's total across every configuration in
    // the run: that cross-configuration share is what turns "this module is a
    // bit slow" into "this module is most of the matrix".
    summary_output_print(output, S8("Slowest test modules across {u64} configuration(s) in {u64} log file(s):\n"), configuration_count,
                         options.paths.length);
    u64 module_limit = BUSTER_MIN(options.limit, module_count);
    for (u64 module_i = 0; module_i < module_limit; module_i += 1)
    {
        TestTimingModuleTotal total = module_totals[module_i];
        u64 share = test_timing_share_tenths(total.duration_ns, total_ns);
        summary_output_print(output, S8("{u64:width=[ ,7]} ms  {u64:width=[ ,3]}.{u64}%  {S8}{u64} configuration(s)\n"),
                             test_timing_duration_ms(total.duration_ns), share / 10, share % 10, summary_pad_right(arena, total.module, 34),
                             total.configurations);
    }

    summary_output_print(output, S8("Slowest per-configuration test modules:\n"));
    u64 row_limit = BUSTER_MIN(options.limit, row_count);
    for (u64 row_i = 0; row_i < row_limit; row_i += 1)
    {
        TestTimingRow row = rows[row_i];
        String8 annotation = S8("");
        if (baseline.present && !row.has_baseline)
        {
            annotation = S8("  (new)");
        }
        else if (baseline.present)
        {
            bool grew = row.duration_ns >= row.baseline_ns;
            u64 delta_ns = grew ? row.duration_ns - row.baseline_ns : row.baseline_ns - row.duration_ns;
            u64 delta_share = test_timing_share_tenths(delta_ns, row.baseline_ns);
            annotation = string_format(arena, S8("  base {u64} ms  {S8}{u64}.{u64}%"), test_timing_duration_ms(row.baseline_ns), grew ? S8("+") : S8("-"),
                                       delta_share / 10, delta_share % 10);
        }
        summary_output_print(output, S8("{u64:width=[ ,7]} ms  {S8}{S8}{S8}\n"), test_timing_duration_ms(row.duration_ns),
                             summary_pad_right(arena, row.module, 34), row.configuration, annotation);
    }

    summary_output_print(output, S8("TEST_TIMING_SUMMARY runner={S8} configurations={u64} modules={u64} rows={u64} total_ms={u64} failed_modules={u64}\n"),
                         runner, configuration_count, module_count, row_count, test_timing_duration_ms(total_ns), failed_modules);
    if (baseline.present)
    {
        summary_output_print(output, S8("TEST_TIMING_BASELINE_COMPARISON path={S8} slower={u64} faster={u64} unchanged={u64} new={u64} missing={u64}\n"),
                             baseline_path, slower, faster, unchanged, added, missing);
    }
    else
    {
        summary_output_print(output, S8("No stored baseline at {S8}; pass --update-baseline to record one.\n"), baseline_path);
    }

    if (options.update_baseline)
    {
        ProcessResult write_result = test_timing_baseline_write(arena, baseline_path, runner, rows, row_count, output, failure_reason);
        if (write_result != PROCESS_RESULT_SUCCESS)
        {
            return write_result;
        }
        summary_output_print(output, S8("Recorded {u64} baseline row(s) for runner {S8} at {S8}\n"), row_count, runner, baseline_path);
    }

    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult test_timing_summary_run(Arena* arena, TestTimingSummaryOptions options)
{
    SummaryOutput output = {.file = os_get_stdout()};
    TestTimingSummaryFailureReason failure_reason = TEST_TIMING_SUMMARY_FAILURE_NONE;
    return test_timing_summary_run_with_output(arena, options, &output, &failure_reason);
}

BUSTER_GLOBAL_LOCAL ProcessResult test_timing_summary_action(Arena* arena, void* data)
{
    TestTimingSummaryOptions* options = (TestTimingSummaryOptions*)data;
    return test_timing_summary_run(arena, *options);
}

typedef struct TestTimingSummarySelfTestFailure TestTimingSummarySelfTestFailure;
struct TestTimingSummarySelfTestFailure
{
    String8 log;
    String8 baseline;
    TestTimingSummaryFailureReason reason;
    String8 message;
};

// The self-test pins the rendered report, not just the parsed numbers: column
// layout, millisecond rounding and percentage rounding are exactly the parts a
// refactor breaks silently.
BUSTER_GLOBAL_LOCAL bool test_timing_summary_self_test_expect(Arena* arena, TestTimingSummaryOptions options, String8 expected)
{
    SummaryOutput output = {.capture_arena = arena};
    TestTimingSummaryFailureReason reason = TEST_TIMING_SUMMARY_FAILURE_NONE;
    ProcessResult result = test_timing_summary_run_with_output(arena, options, &output, &reason);
    String8 actual = summary_output_text(arena, output);
    return result == PROCESS_RESULT_SUCCESS && reason == TEST_TIMING_SUMMARY_FAILURE_NONE && string_equal(actual, expected);
}

BUSTER_GLOBAL_LOCAL bool test_timing_summary_self_test_expect_failure(Arena* arena, String8 runner, TestTimingSummarySelfTestFailure failure)
{
    String8 paths[] = {failure.log};
    SummaryOutput output = {.capture_arena = arena};
    TestTimingSummaryFailureReason reason = TEST_TIMING_SUMMARY_FAILURE_NONE;
    ProcessResult result = test_timing_summary_run_with_output(arena,
                                                               (TestTimingSummaryOptions){
                                                                   .paths = {.pointer = paths, .length = BUSTER_ARRAY_LENGTH(paths)},
                                                                   .baseline = failure.baseline,
                                                                   .runner = runner,
                                                                   .limit = 5,
                                                               },
                                                               &output, &reason);
    String8 actual = summary_output_text(arena, output);
    return result != PROCESS_RESULT_SUCCESS && reason == failure.reason && string_equal(actual, failure.message);
}

BUSTER_GLOBAL_LOCAL ProcessResult test_timing_summary_self_test(Arena* arena)
{
    String8 directory = {0};
    if (!summary_self_test_claim_directory(arena, S8("test-timing-summary"), &directory))
    {
        string_print(S8("error: test_timing_summary self-test could not claim an exclusive directory\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 runner = S8("self-test-runner");
    String8 matrix = path_join(arena, directory, S8("matrix.log"));
    String8 plain = path_join(arena, directory, S8("plain.log"));
    String8 baseline = path_join(arena, directory, S8("baseline.txt"));
    String8 malformed_baseline = path_join(arena, directory, S8("malformed-baseline.txt"));
    String8 foreign_baseline = path_join(arena, directory, S8("foreign-baseline.txt"));
    String8 missing_log = path_join(arena, directory, S8("missing.log"));
    bool passed = true;

    // One `&&`-joined command line covering two configurations of a shared
    // tree, then a second tree, exactly as the superbuild echoes them; the
    // interleaved non-test command lines must not disturb the attribution.
    passed = passed &&
             summary_self_test_write_text(
                 matrix,
                 S8("[1/4] cd /src && cmake --build build/build-cc_gcc-configs_shared --config Debug --target ide:Debug ide:Release --parallel 3\n"
                    "[2/4] cd /src && cmake -E env BUSTER_TEST_JOBS=3 cmake --build build/build-cc_gcc-configs_shared --config Debug --target test_units"
                    " && cmake -E env BUSTER_TEST_JOBS=3 cmake --build build/build-cc_gcc-configs_shared --config Release --target test_all\n"
                    "[1/1] Run unit tests\n"
                    "TEST_MODULE_TIMING index=0 module=arena_tests duration_ns=4000000 passed=4 failed=0 assertions=4 status=pass\n"
                    "TEST_MODULE_TIMING index=1 module=slow_tests duration_ns=60000000 passed=2 failed=0 assertions=2 status=pass\n"
                    "TEST_MODULE_TIMING index=0 module=arena_tests duration_ns=2000000 passed=4 failed=0 assertions=4 status=pass\n"
                    "TEST_MODULE_TIMING index=1 module=slow_tests duration_ns=30000000 passed=1 failed=1 assertions=2 status=fail\n"
                    "[3/4] cd /src && cmake --build build/build-cc_clang-sanitize_on --config Debug --target test_units\n"
                    "TEST_MODULE_TIMING index=0 module=arena_tests duration_ns=6000000 passed=4 failed=0 assertions=4 status=pass\n"
                    "TEST_MODULE_TIMING index=1 module=slow_tests duration_ns=98000000 passed=2 failed=0 assertions=2 status=pass\n"
                    "[4/4] cd /src && cmake --build build/build-cc_clang-sanitize_on --config Debug --target clang_analyze\n"));
    // A saved plain `ide test --verbose=1` run has no command line in front of
    // it; its two runs must stay two series rather than merging into one.
    passed = passed && summary_self_test_write_text(
                           plain, S8("TEST_MODULE_TIMING index=0 module=arena_tests duration_ns=1400000 passed=4 failed=0 assertions=4 status=pass\n"
                                     "TEST_MODULE_TIMING index=0 module=arena_tests duration_ns=1600000 passed=4 failed=0 assertions=4 status=pass\n"));
    passed = passed && summary_self_test_write_text(malformed_baseline, S8("TEST_TIMING_BASELINE_VERSION 1\n"
                                                                          "TEST_TIMING_BASELINE_RUNNER self-test-runner\n"
                                                                          "TEST_TIMING_BASELINE config=cc_gcc-configs_shared:Debug module=arena_tests\n"));
    passed = passed && summary_self_test_write_text(foreign_baseline, S8("TEST_TIMING_BASELINE_VERSION 1\n"
                                                                        "TEST_TIMING_BASELINE_RUNNER other-runner\n"
                                                                        "TEST_TIMING_BASELINE config=cc_gcc-configs_shared:Debug module=arena_tests"
                                                                        " duration_ns=4000000 samples=1\n"));

    String8 matrix_paths[] = {matrix};
    String8 plain_paths[] = {plain};

    TestTimingSummaryOptions matrix_options = {
        .paths = {.pointer = matrix_paths, .length = BUSTER_ARRAY_LENGTH(matrix_paths)},
        .baseline = baseline,
        .runner = runner,
        .limit = 2,
    };
    TestTimingSummaryOptions record_options = matrix_options;
    record_options.limit = 1;
    record_options.update_baseline = 1;
    TestTimingSummaryOptions replay_options = matrix_options;
    replay_options.limit = 1;
    TestTimingSummaryOptions plain_options = {
        .paths = {.pointer = plain_paths, .length = BUSTER_ARRAY_LENGTH(plain_paths)},
        .baseline = baseline,
        .runner = runner,
        .limit = 2,
    };

    // First run: no baseline yet, so no deltas and an explicit invitation to
    // record one. 200 ms total; slow_tests is 94% of it across 3 series.
    passed = passed &&
             test_timing_summary_self_test_expect(
                 arena, matrix_options,
                 string_format(arena,
                               S8("Slowest test modules across 3 configuration(s) in 1 log file(s):\n"
                                  "    188 ms   94.0%  slow_tests                        3 configuration(s)\n"
                                  "     12 ms    6.0%  arena_tests                       3 configuration(s)\n"
                                  "Slowest per-configuration test modules:\n"
                                  "     98 ms  slow_tests                        cc_clang-sanitize_on:Debug\n"
                                  "     60 ms  slow_tests                        cc_gcc-configs_shared:Debug\n"
                                  "TEST_TIMING_SUMMARY runner=self-test-runner configurations=3 modules=2 rows=6 total_ms=200 failed_modules=1\n"
                                  "No stored baseline at {S8}; pass --update-baseline to record one.\n"),
                               baseline));

    // Record the baseline, then replay the same log: every row must match
    // exactly, which is also the round-trip test for the stored format.
    passed = passed &&
             test_timing_summary_self_test_expect(
                 arena, record_options,
                 string_format(arena,
                               S8("Slowest test modules across 3 configuration(s) in 1 log file(s):\n"
                                  "    188 ms   94.0%  slow_tests                        3 configuration(s)\n"
                                  "Slowest per-configuration test modules:\n"
                                  "     98 ms  slow_tests                        cc_clang-sanitize_on:Debug\n"
                                  "TEST_TIMING_SUMMARY runner=self-test-runner configurations=3 modules=2 rows=6 total_ms=200 failed_modules=1\n"
                                  "No stored baseline at {S8}; pass --update-baseline to record one.\n"
                                  "Recorded 6 baseline row(s) for runner self-test-runner at {S8}\n"),
                               baseline, baseline));
    passed = passed &&
             test_timing_summary_self_test_expect(
                 arena, replay_options,
                 string_format(arena,
                               S8("Slowest test modules across 3 configuration(s) in 1 log file(s):\n"
                                  "    188 ms   94.0%  slow_tests                        3 configuration(s)\n"
                                  "Slowest per-configuration test modules:\n"
                                  "     98 ms  slow_tests                        cc_clang-sanitize_on:Debug  base 98 ms  +0.0%\n"
                                  "TEST_TIMING_SUMMARY runner=self-test-runner configurations=3 modules=2 rows=6 total_ms=200 failed_modules=1\n"
                                  "TEST_TIMING_BASELINE_COMPARISON path={S8} slower=0 faster=0 unchanged=6 new=0 missing=0\n"),
                               baseline));

    // A different run against that baseline: a module absent from it is new,
    // and its own series is the only thing it is compared against.
    passed = passed &&
             test_timing_summary_self_test_expect(
                 arena, plain_options,
                 string_format(arena,
                               S8("Slowest test modules across 2 configuration(s) in 1 log file(s):\n"
                                  "      3 ms  100.0%  arena_tests                       2 configuration(s)\n"
                                  "Slowest per-configuration test modules:\n"
                                  "      2 ms  arena_tests                       unknown:1  (new)\n"
                                  "      1 ms  arena_tests                       unknown:0  (new)\n"
                                  "TEST_TIMING_SUMMARY runner=self-test-runner configurations=2 modules=1 rows=2 total_ms=3 failed_modules=0\n"
                                  "TEST_TIMING_BASELINE_COMPARISON path={S8} slower=0 faster=0 unchanged=0 new=2 missing=6\n"),
                               baseline));

    TestTimingSummarySelfTestFailure failure_cases[] = {
        {.log = missing_log,
         .baseline = baseline,
         .reason = TEST_TIMING_SUMMARY_FAILURE_UNREADABLE_LOG,
         .message = string_format(arena, S8("error: failed to read {S8}\n"), missing_log)},
        {.log = matrix,
         .baseline = malformed_baseline,
         .reason = TEST_TIMING_SUMMARY_FAILURE_MALFORMED_BASELINE,
         .message = string_format(arena, S8("error: {S8} has an incomplete baseline record\n"), malformed_baseline)},
        {.log = matrix,
         .baseline = foreign_baseline,
         .reason = TEST_TIMING_SUMMARY_FAILURE_BASELINE_RUNNER_MISMATCH,
         .message = string_format(arena, S8("error: {S8} was recorded on runner other-runner, not self-test-runner\n"), foreign_baseline)},
    };
    for (u64 failure_i = 0; failure_i < BUSTER_ARRAY_LENGTH(failure_cases); failure_i += 1)
    {
        passed = passed && test_timing_summary_self_test_expect_failure(arena, runner, failure_cases[failure_i]);
    }

    SummaryOutput empty_output = {.capture_arena = arena};
    TestTimingSummaryFailureReason empty_reason = TEST_TIMING_SUMMARY_FAILURE_NONE;
    passed = passed && test_timing_summary_run_with_output(arena, (TestTimingSummaryOptions){.runner = runner}, &empty_output, &empty_reason) !=
                           PROCESS_RESULT_SUCCESS &&
             empty_reason == TEST_TIMING_SUMMARY_FAILURE_RESOURCE;

    if (!os_directory_delete(directory))
    {
        passed = false;
    }
    if (!passed)
    {
        string_print(S8("error: test_timing_summary self-test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("TEST_TIMING_SUMMARY_SELF_TEST passed attribution=1 aggregation=1 baseline_round_trip=1 negative_cases={u64} cleanup=1\n"),
                 BUSTER_ARRAY_LENGTH(failure_cases) + 1);
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL void test_timing_summary_action_add(Arena* arena, TestTimingSummaryOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    TestTimingSummaryOptions* options_copy = arena_allocate(arena, TestTimingSummaryOptions, 1);
    *options_copy = options;

    *run = (ProcessRun){
        .callback = test_timing_summary_action,
        .callback_data = options_copy,
    };
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_requested_for_platform(bool ci, bool forced, bool macos, bool windows, bool x86_64);
BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_consumer_supported_for_platform(bool linux_host, bool macos, bool windows, bool x86_64);
BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_selection_is_valid(bool requested, bool supported, bool producer_selected);
BUSTER_GLOBAL_LOCAL ProcessResult build_artifact_fanout_cleanup_action(Arena* arena, void* data);
BUSTER_GLOBAL_LOCAL ProcessSpawnResult process_run_spawn(Arena* arena, ProcessRun* run);
BUSTER_GLOBAL_LOCAL ProcessResult process_run_wait(Arena* arena, ProcessRun* run);

BUSTER_GLOBAL_LOCAL ProcessResult build_artifact_fanout_tests(Arena* arena, bool include_large_snapshot)
{
    String8 ci_arguments[] = {
        S8_INITIALIZER("-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"),
    };
    Generate canonical = {
        .compiler = BUILD_COMPILER_CLANG,
        .ci = 1,
        .link_libc = 1,
        .include_tests = 1,
        .check_optional_warnings = 0,
        .developer_targets = 0,
        .cmake_arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(ci_arguments),
    };
    CmakeBuildOptions release = {
        .config = S8("Release"),
        .optimize = 1,
        .optimize_set = 1,
    };
    if (!build_artifact_fanout_is_canonical(canonical, release))
    {
        string_print(S8("error: artifact fan-out canonical-combination test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (build_artifact_fanout_requested_for_platform(false, false, false, false, true) ||
        !build_artifact_fanout_requested_for_platform(true, false, true, false, false) ||
        !build_artifact_fanout_requested_for_platform(true, false, false, true, true) ||
        build_artifact_fanout_requested_for_platform(true, false, false, true, false) ||
        !build_artifact_fanout_requested_for_platform(false, true, false, false, true) ||
        !build_artifact_fanout_consumer_supported_for_platform(true, false, false, true) ||
        !build_artifact_fanout_consumer_supported_for_platform(false, true, false, false) ||
        !build_artifact_fanout_consumer_supported_for_platform(false, false, true, true) ||
        build_artifact_fanout_consumer_supported_for_platform(false, false, true, false) ||
        !build_artifact_fanout_requested_for_platform(false, true, false, false, true) ||
        build_artifact_fanout_selection_is_valid(true, true, false) || build_artifact_fanout_selection_is_valid(true, false, true) ||
        !build_artifact_fanout_selection_is_valid(false, false, false) ||
        !build_artifact_fanout_selection_is_valid(true, true, true))
    {
        string_print(S8("error: artifact fan-out platform-selection test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!build_compiler_value_is_tcc(S8("tcc")) || !build_compiler_value_is_tcc(S8("C:\\tools\\tcc.exe")) ||
        !build_compiler_value_is_tcc(S8("tcc;extra")) || !build_compiler_value_is_tcc(S8("C:\\tools\\tcc.exe;extra")) ||
        build_compiler_value_is_tcc(S8("clang")) || build_compiler_value_is_tcc(S8("clang;tcc")) ||
        !build_cmake_argument_is_tcc_application_compiler(arena, S8("-DCMAKE_C_COMPILER=tcc"), (String8){0}) ||
        !build_cmake_argument_is_tcc_application_compiler(arena, S8("-DCMAKE_C_COMPILER:FILEPATH=tcc;extra"), (String8){0}) ||
        !build_cmake_argument_is_tcc_application_compiler(arena, S8("-D"), S8("CMAKE_C_COMPILER:STRING=C:\\tools\\TCC.EXE;extra")) ||
        build_cmake_argument_is_tcc_application_compiler(arena, S8("-DCMAKE_C_COMPILER=clang"), (String8){0}) ||
        build_cmake_argument_is_tcc_application_compiler(arena, S8("-DCMAKE_C_COMPILER_LAUNCHER=ccache"), (String8){0}) ||
        build_cmake_argument_is_tcc_application_compiler(arena, S8("-DCMAKE_C_LINKER_LAUNCHER=wrapper"), (String8){0}) ||
        build_cmake_argument_is_tcc_application_compiler(arena, S8("-DCMAKE_C_FLAGS=-O2"), (String8){0}))
    {
        string_print(S8("error: artifact fan-out TCC application-compiler rejection test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (!build_artifact_fanout_path_equal(S8("C:\\LLVM\\bin\\clang.exe"), S8("C:/LLVM/bin/clang.exe")))
    {
        string_print(S8("error: artifact fan-out path separator normalization test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
#if BUSTER_WINDOWS
    if (!build_artifact_fanout_path_equal(S8("C:\\LLVM\\bin\\clang.exe"), S8("c:/llvm/bin/clang.exe")))
    {
        string_print(S8("error: artifact fan-out Windows path case normalization test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
#endif

    Generate non_clang = canonical;
    non_clang.compiler = BUILD_COMPILER_GCC;
    if (build_artifact_fanout_is_canonical(non_clang, release))
    {
        string_print(S8("error: artifact fan-out accepted a non-Clang producer\n"));
        return PROCESS_RESULT_FAILED;
    }
    CmakeBuildOptions debug = release;
    debug.config = S8("Debug");
    debug.optimize = 0;
    if (build_artifact_fanout_is_canonical(canonical, debug))
    {
        string_print(S8("error: artifact fan-out accepted a non-Release producer\n"));
        return PROCESS_RESULT_FAILED;
    }

    BuildArtifactCompilerMetadata compiler = {
        .compiler_path = S8("/usr/bin/clang"),
        .compiler_id = S8("Clang"),
        .compiler_version = S8("test"),
        .compiler_arg1_present = 1,
    };
    if (!build_artifact_fanout_identity_matches(S8("/usr/bin/clang"), compiler))
    {
        string_print(S8("error: artifact fan-out actual compiler identity acceptance test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    compiler.compiler_id = S8("GNU");
    if (build_artifact_fanout_identity_matches(S8("/usr/bin/clang"), compiler))
    {
        string_print(S8("error: artifact fan-out accepted non-Clang CMake metadata\n"));
        return PROCESS_RESULT_FAILED;
    }
    compiler.compiler_id = S8("Clang");
    compiler.compiler_path = S8("/tmp/clang-wrapper");
    if (build_artifact_fanout_identity_matches(S8("/usr/bin/clang"), compiler))
    {
        string_print(S8("error: artifact fan-out accepted a compiler wrapper path\n"));
        return PROCESS_RESULT_FAILED;
    }
    compiler.compiler_path = S8("/usr/bin/clang");
    compiler.compiler_wrapper = S8("/tmp/clang-wrapper");
    compiler.compiler_wrapper_present = 1;
    if (build_artifact_fanout_identity_matches(S8("/usr/bin/clang"), compiler))
    {
        string_print(S8("error: artifact fan-out accepted a configured compiler wrapper\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 expected_linker = generate_linker(canonical, S8("clang"));
    bool config_matches = build_artifact_fanout_config_matches(canonical, true, true, true, true, false, false, false, false, false, true,
                                                                BUSTER_LINUX != 0, false, false, expected_linker);
    if (!config_matches)
    {
        string_print(S8("error: artifact fan-out canonical configuration acceptance test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 canonical_flags_cache = S8(
        "BUSTER_COMPILE_SHADERS:BOOL=ON\n"
        "CMAKE_C_FLAGS:STRING=\n"
        "CMAKE_C_FLAGS_DEBUG:STRING=-g\n"
        "CMAKE_C_FLAGS_MINSIZEREL:STRING=-Os -DNDEBUG\n"
        "CMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG\n"
        "CMAKE_C_FLAGS_RELWITHDEBINFO:STRING=-O2 -g -DNDEBUG\n"
        "CMAKE_EXE_LINKER_FLAGS:STRING=\n"
        "CMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=\n"
        "CMAKE_EXE_LINKER_FLAGS_MINSIZEREL:STRING=\n"
        "CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=\n"
        "CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO:STRING=\n"
        "CMAKE_MODULE_LINKER_FLAGS:STRING=\n"
        "CMAKE_MODULE_LINKER_FLAGS_DEBUG:STRING=\n"
        "CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL:STRING=\n"
        "CMAKE_MODULE_LINKER_FLAGS_RELEASE:STRING=\n"
        "CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO:STRING=\n"
        "CMAKE_SHARED_LINKER_FLAGS:STRING=\n"
        "CMAKE_SHARED_LINKER_FLAGS_DEBUG:STRING=\n"
        "CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL:STRING=\n"
        "CMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING=\n"
        "CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO:STRING=\n"
        "CMAKE_STATIC_LINKER_FLAGS:STRING=\n"
        "CMAKE_STATIC_LINKER_FLAGS_DEBUG:STRING=\n"
        "CMAKE_STATIC_LINKER_FLAGS_MINSIZEREL:STRING=\n"
        "CMAKE_STATIC_LINKER_FLAGS_RELEASE:STRING=\n"
        "CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO:STRING=\n");
    if (!build_artifact_fanout_provenance_inputs_match(canonical_flags_cache))
    {
        string_print(S8("error: artifact fan-out canonical flag-state acceptance test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    u64 canonical_fingerprint = 0;
    if (!build_artifact_fanout_cache_fingerprint(canonical_flags_cache, &canonical_fingerprint))
    {
        string_print(S8("error: artifact fan-out canonical configuration fingerprint test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 shaders_off_cache = string_format(arena, S8("BUSTER_COMPILE_SHADERS:BOOL=OFF\n{S8}"), canonical_flags_cache);
    u64 shaders_off_fingerprint = 0;
    if (build_artifact_fanout_provenance_inputs_match(shaders_off_cache) ||
        !build_artifact_fanout_cache_fingerprint(shaders_off_cache, &shaders_off_fingerprint) || shaders_off_fingerprint == canonical_fingerprint)
    {
        string_print(S8("error: artifact fan-out accepted disabled shader compilation provenance\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 launcher_cache = string_format(arena, S8("CMAKE_C_COMPILER_LAUNCHER:FILEPATH=/tmp/ccache\n{S8}"), canonical_flags_cache);
    if (build_artifact_fanout_provenance_inputs_match(launcher_cache))
    {
        string_print(S8("error: artifact fan-out accepted a configured compiler launcher\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 cflags_cache = string_format(arena, S8("CMAKE_C_FLAGS_RELEASE:STRING=-march=native\n{S8}"), canonical_flags_cache);
    u64 cflags_fingerprint = 0;
    if (!build_artifact_fanout_cache_fingerprint(cflags_cache, &cflags_fingerprint) || cflags_fingerprint == canonical_fingerprint)
    {
        string_print(S8("error: artifact fan-out accepted a noncanonical C compiler flag state\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 ldflags_cache = string_format(arena, S8("CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=-fuse-ld=gold\n{S8}"), canonical_flags_cache);
    u64 ldflags_fingerprint = 0;
    if (!build_artifact_fanout_cache_fingerprint(ldflags_cache, &ldflags_fingerprint) || ldflags_fingerprint == canonical_fingerprint)
    {
        string_print(S8("error: artifact fan-out accepted a noncanonical linker flag state\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 windows_cache = S8("BUSTER_COMPILE_SHADERS:BOOL=ON\n"
                               "CMAKE_C_FLAGS_DEBUG:STRING=-O0 -gcodeview\n"
                               "CMAKE_C_FLAGS_RELEASE:STRING=-O2 -DNDEBUG\n"
                               "CMAKE_C_STANDARD_LIBRARIES_RELEASE:STRING=kernel32.lib;user32.lib;oldnames.lib\n"
                               "CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=/DEBUG\n"
                               "CMAKE_LINKER_TYPE:STRING=DEFAULT\n");
    u64 windows_fingerprint = 0;
    if (!build_artifact_fanout_provenance_inputs_match(windows_cache) || !build_artifact_fanout_cache_fingerprint(windows_cache, &windows_fingerprint))
    {
        string_print(S8("error: artifact fan-out Windows-shaped CMake flag-state acceptance test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 windows_standard_mutated_cache = string_format(arena, S8("CMAKE_C_STANDARD_LIBRARIES_RELEASE:STRING=kernel32.lib;advapi32.lib\n{S8}"),
                                                           windows_cache);
    u64 windows_standard_mutated_fingerprint = 0;
    if (!build_artifact_fanout_provenance_inputs_match(windows_standard_mutated_cache) ||
        !build_artifact_fanout_cache_fingerprint(windows_standard_mutated_cache, &windows_standard_mutated_fingerprint) ||
        windows_standard_mutated_fingerprint == windows_fingerprint)
    {
        string_print(S8("error: artifact fan-out Windows standard-library provenance test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 windows_mutated_cache = S8("CMAKE_C_FLAGS_DEBUG:STRING=-O0 -gcodeview -fno-omit-frame-pointer\n"
                                       "CMAKE_C_FLAGS_RELEASE:STRING=-O2 -DNDEBUG\n"
                                       "CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=/DEBUG\n"
                                       "CMAKE_LINKER_TYPE:STRING=DEFAULT\n");
    u64 windows_mutated_fingerprint = 0;
    if (!build_artifact_fanout_cache_fingerprint(windows_mutated_cache, &windows_mutated_fingerprint) ||
        windows_mutated_fingerprint == windows_fingerprint)
    {
        string_print(S8("error: artifact fan-out Windows-shaped flag mutation was not fingerprinted\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 restricted_cache_mutations[] = {
        S8("CMAKE_TOOLCHAIN_FILE:FILEPATH=C:/toolchain.cmake\n"),
        S8("CMAKE_C_COMPILER_TARGET:STRING=x86_64-custom\n"),
        S8("CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN:PATH=C:/external\n"),
        S8("CMAKE_C_COMPILER_LAUNCHER:FILEPATH=C:/ccache.exe\n"),
        S8("CMAKE_C_LINKER_LAUNCHER:FILEPATH=C:/link-wrapper.exe\n"),
        S8("CMAKE_RULE_LAUNCH_COMPILE:STRING=wrapper\n"),
        S8("CMAKE_RULE_LAUNCH_LINK:STRING=wrapper\n"),
        S8("CMAKE_OSX_ARCHITECTURES:STRING=arm64\n"),
        S8("CMAKE_INTERPROCEDURAL_OPTIMIZATION:BOOL=ON\n"),
    };
    for (u32 mutation_i = 0; mutation_i < BUSTER_ARRAY_LENGTH(restricted_cache_mutations); mutation_i += 1)
    {
        String8 mutation = string_format(arena, S8("{S8}{S8}"), restricted_cache_mutations[mutation_i], canonical_flags_cache);
        if (build_artifact_fanout_provenance_inputs_match(mutation))
        {
            string_print(S8("error: artifact fan-out accepted a restricted toolchain/launcher/IPO input\n"));
            return PROCESS_RESULT_FAILED;
        }
    }
    if (build_artifact_fanout_config_matches(canonical, false, true, true, true, false, false, false, false, false, true, BUSTER_LINUX != 0, false,
                                             false, expected_linker))
    {
        string_print(S8("error: artifact fan-out accepted a configuration mismatch\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (build_artifact_fanout_config_matches(canonical, true, true, true, true, true, false, false, false, false, true, BUSTER_LINUX != 0, false,
                                             false, expected_linker) ||
        build_artifact_fanout_config_matches(canonical, true, true, true, true, false, true, false, false, false, true, BUSTER_LINUX != 0, false,
                                             false, expected_linker) ||
        build_artifact_fanout_config_matches(canonical, true, true, true, true, false, false, true, false, false, true, BUSTER_LINUX != 0, false,
                                             false, expected_linker) ||
        build_artifact_fanout_config_matches(canonical, true, true, true, true, false, false, false, false, true, true, BUSTER_LINUX != 0, false,
                                             false, expected_linker))
    {
        string_print(S8("error: artifact fan-out accepted a sanitizer/instrument/time-trace/LTO mismatch\n"));
        return PROCESS_RESULT_FAILED;
    }

    BuildArtifactFanout state = {
        .release_build_scheduled = 1,
    };
    BuildArtifactFanout producer_ready = {
        .release_build_scheduled = 1,
        .release_build_succeeded = 1,
        .producer_clean_succeeded = 1,
    };
    if (build_artifact_fanout_ready(state, true) ||
        build_artifact_fanout_ready((BuildArtifactFanout){
                                         .release_build_scheduled = 1,
                                         .release_build_succeeded = 1,
                                     }, true) ||
        !build_artifact_fanout_ready(producer_ready, true) || build_artifact_fanout_ready(producer_ready, false))
    {
        string_print(S8("error: artifact fan-out producer-state test failed\n"));
        return PROCESS_RESULT_FAILED;
    }

    canonical.fuzz_available = 1;
    String8 provenance_record_path = string_format_z(arena, S8("build/artifact-fanout-provenance-{u64}.record"), os_get_current_process_id());
    remove_path_recursive(arena, provenance_record_path);
    BuildArtifactFanout provenance_state = {
        .build_directory = S8("build/provenance-tree"),
        .artifact_path = S8("build/provenance-tree/Release/ide"),
        .provenance_record_path = provenance_record_path,
        .generate = canonical,
        .options = release,
        .expected_compiler = {
            .compiler_path = S8("/usr/bin/clang"),
            .compiler_id = S8("Clang"),
            .compiler_version = S8("test"),
            .compiler_ar = S8("/usr/bin/llvm-ar"),
            .compiler_ranlib = S8("/usr/bin/llvm-ranlib"),
            .compiler_hash = 11,
            .compiler_size = 12,
            .compiler_ar_hash = 13,
            .compiler_ar_size = 14,
            .compiler_ranlib_hash = 15,
            .compiler_ranlib_size = 16,
        },
        .expected_tools = {
            .linker_path = S8("/usr/bin/ld"),
            .slangc_path = S8("/usr/bin/slangc"),
            .spirv_opt_path = S8("/usr/bin/spirv-opt"),
            .linker_hash = 21,
            .linker_size = 22,
            .slangc_hash = 23,
            .slangc_size = 24,
            .spirv_opt_hash = 25,
            .spirv_opt_size = 26,
        },
        .cache_fingerprint = 31,
        .graph_fingerprint = 32,
        .environment_fingerprint = 33,
        .provenance_captured = 1,
        .producer_clean_succeeded = 1,
    };
    bool provenance_written = build_artifact_fanout_provenance_record_write(arena, &provenance_state);
    BuildArtifactFanout loaded_provenance = {
        .build_directory = provenance_state.build_directory,
        .artifact_path = provenance_state.artifact_path,
        .provenance_record_path = provenance_state.provenance_record_path,
        .generate = provenance_state.generate,
        .options = provenance_state.options,
    };
    bool provenance_loaded = provenance_written && build_artifact_fanout_provenance_record_load(arena, provenance_record_path, &loaded_provenance);
    bool provenance_round_trip = provenance_loaded &&
                                 loaded_provenance.producer_clean_succeeded &&
                                 build_artifact_fanout_provenance_state_matches(arena, loaded_provenance, loaded_provenance.expected_compiler,
                                                                                loaded_provenance.expected_tools,
                                                                                loaded_provenance.cache_fingerprint,
                                                                                loaded_provenance.graph_fingerprint,
                                                                                loaded_provenance.environment_fingerprint);
    BuildArtifactCompilerMetadata changed_compiler = loaded_provenance.expected_compiler;
    changed_compiler.compiler_hash ^= 1;
    BuildArtifactFanoutToolMetadata changed_tools = loaded_provenance.expected_tools;
    changed_tools.linker_hash ^= 1;
    bool changed_pre_post_rejected = provenance_round_trip &&
                                      !build_artifact_fanout_provenance_state_matches(arena, loaded_provenance, changed_compiler,
                                                                                       loaded_provenance.expected_tools,
                                                                                       loaded_provenance.cache_fingerprint,
                                                                                       loaded_provenance.graph_fingerprint,
                                                                                       loaded_provenance.environment_fingerprint) &&
                                      !build_artifact_fanout_provenance_state_matches(arena, loaded_provenance,
                                                                                       loaded_provenance.expected_compiler, changed_tools,
                                                                                       loaded_provenance.cache_fingerprint,
                                                                                       loaded_provenance.graph_fingerprint,
                                                                                       loaded_provenance.environment_fingerprint) &&
                                      !build_artifact_fanout_provenance_state_matches(arena, loaded_provenance,
                                                                                       loaded_provenance.expected_compiler,
                                                                                       loaded_provenance.expected_tools,
                                                                                       loaded_provenance.cache_fingerprint ^ 1,
                                                                                       loaded_provenance.graph_fingerprint,
                                                                                       loaded_provenance.environment_fingerprint);
    ByteSlice original_provenance_bytes = file_read(arena, provenance_record_path, (FileReadOptions){.map_required = 0});
    BuildArtifactFanout unclean_provenance_state = provenance_state;
    unclean_provenance_state.producer_clean_succeeded = 0;
    bool unclean_record_written = build_artifact_fanout_provenance_record_write(arena, &unclean_provenance_state);
    BuildArtifactFanout unclean_candidate = {
        .build_directory = provenance_state.build_directory,
        .artifact_path = provenance_state.artifact_path,
        .provenance_record_path = provenance_state.provenance_record_path,
        .generate = provenance_state.generate,
        .options = provenance_state.options,
        .release_build_scheduled = 1,
        .release_build_succeeded = 1,
    };
    bool unclean_record_loaded = unclean_record_written &&
                                 build_artifact_fanout_provenance_record_load(arena, provenance_record_path, &unclean_candidate);
    bool unclean_record_rejected = unclean_record_loaded && !unclean_candidate.producer_clean_succeeded &&
                                   !build_artifact_fanout_ready(unclean_candidate, true);
    bool restored_after_unclean = original_provenance_bytes.pointer &&
                                  file_write(provenance_record_path, original_provenance_bytes);
    String8 corrupted_provenance = original_provenance_bytes.pointer
                                       ? string_duplicate_arena(arena, BYTE_SLICE_TO_STRING(8, original_provenance_bytes), false)
                                       : (String8){0};
    u64 header_end = string_first_code_unit(corrupted_provenance, '\n');
    bool corruption_written = original_provenance_bytes.pointer && header_end != BUSTER_STRING_NO_MATCH && header_end + 1 < corrupted_provenance.length;
    if (corruption_written)
    {
        corrupted_provenance.pointer[header_end + 1] = corrupted_provenance.pointer[header_end + 1] == '9' ? '8' : '9';
        corruption_written = file_write(provenance_record_path, BUSTER_SLICE_TO_BYTE_SLICE(corrupted_provenance));
    }
    BuildArtifactFanout corrupted_candidate = {
        .build_directory = provenance_state.build_directory,
        .artifact_path = provenance_state.artifact_path,
        .provenance_record_path = provenance_state.provenance_record_path,
        .generate = provenance_state.generate,
        .options = provenance_state.options,
    };
    bool corrupted_rejected = corruption_written && !build_artifact_fanout_provenance_record_load(arena, provenance_record_path, &corrupted_candidate);
    bool restored_provenance = original_provenance_bytes.pointer &&
                               file_write(provenance_record_path, original_provenance_bytes);
    bool malformed_written = file_write(provenance_record_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("malformed\n")));
    BuildArtifactFanout malformed_candidate = {
        .build_directory = provenance_state.build_directory,
        .artifact_path = provenance_state.artifact_path,
        .provenance_record_path = provenance_state.provenance_record_path,
        .generate = provenance_state.generate,
        .options = provenance_state.options,
    };
    bool malformed_rejected = malformed_written && !build_artifact_fanout_provenance_record_load(arena, provenance_record_path, &malformed_candidate);
    bool restored_after_malformed = restored_provenance &&
                                    file_write(provenance_record_path, original_provenance_bytes);
    remove_path_recursive(arena, provenance_record_path);
    if (!provenance_round_trip || !changed_pre_post_rejected || !unclean_record_rejected || !restored_after_unclean || !corrupted_rejected ||
        !restored_after_malformed || !malformed_rejected ||
        path_exists(arena, provenance_record_path))
    {
        string_print(S8("error: artifact fan-out durable provenance record/mismatch test failed\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 clean_tree = string_format_z(arena, S8("build/artifact-fanout-clean-tree-{u64}"), os_get_current_process_id());
    String8 clean_record_path = path_join(arena, clean_tree, S8("producer.provenance"));
    String8 clean_artifact_path = path_join(arena, path_join(arena, clean_tree, S8("Release")),
#if BUSTER_WINDOWS
                                            S8("ide.exe")
#else
                                            S8("ide")
#endif
    );
    String8 same_path_tool = path_join(arena, clean_tree, S8("producer-tool"));
    remove_path_recursive(arena, clean_tree);
    BuildArtifactFanout clean_state = {
        .build_directory = clean_tree,
        .artifact_path = clean_artifact_path,
        .provenance_record_path = clean_record_path,
        .generate = canonical,
        .options = release,
        .expected_compiler = provenance_state.expected_compiler,
        .expected_tools = provenance_state.expected_tools,
        .cache_fingerprint = provenance_state.cache_fingerprint,
        .graph_fingerprint = provenance_state.graph_fingerprint,
        .environment_fingerprint = provenance_state.environment_fingerprint,
        .release_build_scheduled = 1,
        .release_build_succeeded = 1,
        .provenance_captured = 1,
        .producer_clean_scheduled = 1,
    };
    String8 clean_outputs[12] = {0};
    u32 clean_output_count = build_artifact_fanout_producer_output_paths(arena, clean_state, clean_outputs);
    String8 clean_graph_paths[7] = {0};
    u32 clean_graph_path_count = build_artifact_fanout_graph_cache_paths(arena, clean_state, clean_graph_paths);
    make_directory_recursive(arena, clean_tree);
    bool clean_graph_written = true;
    for (u32 path_i = 0; path_i < clean_graph_path_count; path_i += 1)
    {
        make_directory_recursive(arena, path_parent(arena, clean_graph_paths[path_i]));
        clean_graph_written &= file_write(clean_graph_paths[path_i], BUSTER_SLICE_TO_BYTE_SLICE(S8("configured graph or cache\n")));
    }
    bool clean_outputs_written = true;
    for (u32 path_i = 0; path_i < clean_output_count; path_i += 1)
    {
        make_directory_recursive(arena, path_parent(arena, clean_outputs[path_i]));
        clean_outputs_written &= file_write(clean_outputs[path_i], BUSTER_SLICE_TO_BYTE_SLICE(S8("old producer output\n")));
    }
    bool old_tool_written = file_write(same_path_tool, BUSTER_SLICE_TO_BYTE_SLICE(S8("producer-old\n")));
    u64 old_tool_hash = 0;
    u64 old_tool_size = 0;
    bool old_tool_hashed = old_tool_written && build_artifact_fanout_hash_file(arena, same_path_tool, &old_tool_hash, &old_tool_size);
    bool changed_tool_written = file_write(same_path_tool, BUSTER_SLICE_TO_BYTE_SLICE(S8("producer-new-at-the-same-path\n")));
    u64 changed_tool_hash = 0;
    u64 changed_tool_size = 0;
    bool changed_tool_hashed = changed_tool_written && build_artifact_fanout_hash_file(arena, same_path_tool, &changed_tool_hash, &changed_tool_size);
    clean_state.expected_tools.linker_path = same_path_tool;
    clean_state.expected_tools.linker_hash = changed_tool_hash;
    clean_state.expected_tools.linker_size = changed_tool_size;
    bool preclean_graph_preserved = false;
    bool preclean_outputs_absent = true;
    bool preclean_postcondition_rejected = clean_graph_written && clean_outputs_written && old_tool_hashed && changed_tool_hashed &&
                                           old_tool_hash != changed_tool_hash &&
                                           !build_artifact_fanout_clean_postcondition(arena, clean_state, &preclean_graph_preserved,
                                                                                       &preclean_outputs_absent) &&
                                           preclean_graph_preserved && !preclean_outputs_absent && !clean_state.producer_clean_succeeded;
    for (u32 path_i = 0; path_i < clean_output_count; path_i += 1)
    {
        remove_path_recursive(arena, clean_outputs[path_i]);
    }
    bool modeled_clean_preserved = true;
    for (u32 path_i = 0; path_i < clean_graph_path_count; path_i += 1)
    {
        modeled_clean_preserved &= path_exists(arena, clean_graph_paths[path_i]);
    }
    bool clean_action_succeeded = build_artifact_fanout_clean_action(arena, &clean_state) == PROCESS_RESULT_SUCCESS;
    bool clean_record_written = clean_action_succeeded && clean_state.producer_clean_succeeded && path_exists(arena, clean_record_path);
    bool clean_artifact_absent = !path_exists(arena, clean_artifact_path);
    bool clean_ready_without_artifact = !build_artifact_fanout_ready(clean_state, path_exists(arena, clean_artifact_path));
    BuildArtifactFanout consumed_clean_record = {
        .build_directory = clean_state.build_directory,
        .artifact_path = clean_state.artifact_path,
        .provenance_record_path = clean_state.provenance_record_path,
        .generate = clean_state.generate,
        .options = clean_state.options,
    };
    bool clean_record_loaded = clean_record_written &&
                               build_artifact_fanout_provenance_record_load(arena, clean_record_path, &consumed_clean_record) &&
                               consumed_clean_record.producer_clean_succeeded;
    bool clean_record_consumed = clean_record_loaded && build_artifact_fanout_provenance_record_consume(arena, clean_record_path) &&
                                 !path_exists(arena, clean_record_path);
    BuildArtifactFanout non_reusable_clean_record = consumed_clean_record;
    bool clean_record_non_reusable = clean_record_consumed &&
                                     !build_artifact_fanout_provenance_record_load(arena, clean_record_path, &non_reusable_clean_record);
    make_directory_recursive(arena, path_parent(arena, clean_artifact_path));
    bool new_artifact_written = file_write(clean_artifact_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("artifact-from-new-producer\n")));
    bool clean_ready_after_new_artifact = new_artifact_written &&
                                          build_artifact_fanout_ready(clean_state, path_exists(arena, clean_artifact_path));
    remove_path_recursive(arena, clean_tree);
    if (!preclean_postcondition_rejected || !modeled_clean_preserved || !clean_action_succeeded ||
        !clean_record_written || !clean_artifact_absent || !clean_ready_without_artifact || !clean_record_non_reusable ||
        !clean_ready_after_new_artifact || path_exists(arena, clean_tree))
    {
        string_print(S8("error: artifact fan-out producer clean lifecycle test failed\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 snapshot_source = string_format_z(arena, S8("build/artifact-fanout-snapshot-{u64}.source"), os_get_current_process_id());
    String8 snapshot_copy = string_format_z(arena, S8("build/artifact-fanout-snapshot-{u64}.copy"), os_get_current_process_id());
    String8 snapshot_failure_copy = string_format_z(arena, S8("build/artifact-fanout-snapshot-{u64}.failure"), os_get_current_process_id());
    String8 lifecycle_copy = string_format_z(arena, S8("build/artifact-fanout-snapshot-{u64}.lifecycle"), os_get_current_process_id());
    String8 large_snapshot_source = string_format_z(arena, S8("build/artifact-fanout-snapshot-{u64}.large.source"), os_get_current_process_id());
    String8 large_snapshot_copy = string_format_z(arena, S8("build/artifact-fanout-snapshot-{u64}.large.copy"), os_get_current_process_id());
    remove_path_recursive(arena, snapshot_source);
    remove_path_recursive(arena, snapshot_copy);
    remove_path_recursive(arena, snapshot_failure_copy);
    remove_path_recursive(arena, lifecycle_copy);
    remove_path_recursive(arena, large_snapshot_source);
    remove_path_recursive(arena, large_snapshot_copy);
    String8 snapshot_content = S8("canonical trusted artifact snapshot\n");
    bool snapshot_written = file_write(snapshot_source, BUSTER_SLICE_TO_BYTE_SLICE(snapshot_content));
#if BUSTER_LINUX || BUSTER_MACOS
    snapshot_written = snapshot_written && chmod((const char*)snapshot_source.pointer, 0711) == 0;
#endif
    ByteSlice snapshot_source_bytes = file_read(arena, snapshot_source, (FileReadOptions){.map_required = 0});
    u64 snapshot_hash = snapshot_source_bytes.pointer ? buster_hash_64(snapshot_source_bytes.pointer, snapshot_source_bytes.length) : 0;
    bool snapshot_copied = snapshot_written && snapshot_source_bytes.pointer &&
                           build_artifact_fanout_snapshot(arena, snapshot_source, snapshot_copy, snapshot_hash, snapshot_source_bytes.length);
    bool snapshot_failure_cleanup = true;
    if (snapshot_written && snapshot_source_bytes.pointer)
    {
        bool failed_snapshot = build_artifact_fanout_snapshot(arena, snapshot_source, snapshot_failure_copy, snapshot_hash ^ 1, snapshot_source_bytes.length);
        snapshot_failure_cleanup = !failed_snapshot && !path_exists(arena, snapshot_failure_copy);
    }
    ByteSlice snapshot_copy_bytes = file_read(arena, snapshot_copy, (FileReadOptions){.map_required = 0});
    bool snapshot_bytes_match = snapshot_copied && snapshot_copy_bytes.pointer && snapshot_copy_bytes.length == snapshot_source_bytes.length &&
                                buster_hash_64(snapshot_copy_bytes.pointer, snapshot_copy_bytes.length) == snapshot_hash;
#if BUSTER_LINUX || BUSTER_MACOS
    struct stat snapshot_source_stats = {0};
    struct stat snapshot_copy_stats = {0};
    bool snapshot_mode_match = stat((const char*)snapshot_source.pointer, &snapshot_source_stats) == 0 &&
                               stat((const char*)snapshot_copy.pointer, &snapshot_copy_stats) == 0 &&
                               (snapshot_source_stats.st_mode & 07777) == (snapshot_copy_stats.st_mode & 07777) &&
                               (snapshot_copy_stats.st_mode & 0111);
#else
    bool snapshot_mode_match = true;
#endif
    remove_path_recursive(arena, snapshot_source);
    remove_path_recursive(arena, snapshot_copy);
    remove_path_recursive(arena, snapshot_failure_copy);
    bool snapshot_cleanup = !path_exists(arena, snapshot_source) && !path_exists(arena, snapshot_copy) && !path_exists(arena, snapshot_failure_copy);
    if (!snapshot_bytes_match || !snapshot_mode_match || !snapshot_failure_cleanup || !snapshot_cleanup)
    {
        string_print(S8("error: artifact fan-out snapshot bytes/hash/mode test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    bool lifecycle_present_cleanup = file_write(lifecycle_copy, BUSTER_SLICE_TO_BYTE_SLICE(snapshot_content));
    BuildArtifactFanout lifecycle_fanout = {.private_bootstrap_path = lifecycle_copy};
    lifecycle_present_cleanup = lifecycle_present_cleanup &&
                                build_artifact_fanout_cleanup_action(arena, &lifecycle_fanout) == PROCESS_RESULT_SUCCESS &&
                                !path_exists(arena, lifecycle_copy);
    bool lifecycle_absent_cleanup = build_artifact_fanout_cleanup_action(arena, &lifecycle_fanout) == PROCESS_RESULT_SUCCESS &&
                                    !path_exists(arena, lifecycle_copy);
    bool lifecycle_failure_cleanup = file_write(lifecycle_copy, BUSTER_SLICE_TO_BYTE_SLICE(snapshot_content));
#if BUSTER_WINDOWS
    String8 cleanup_failure_arguments[] = {S8("cmd.exe"), S8("/C"), S8("exit 200")};
#else
    String8 cleanup_failure_arguments[] = {S8("/bin/sh"), S8("-c"), S8("exit 200")};
#endif
    ProcessRun cleanup_failure_run = {
        .arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(cleanup_failure_arguments),
        .cleanup_callback = build_artifact_fanout_cleanup_action,
        .cleanup_data = &lifecycle_fanout,
    };
    if (lifecycle_failure_cleanup)
    {
        cleanup_failure_run.spawn = process_run_spawn(arena, &cleanup_failure_run);
        lifecycle_failure_cleanup = process_run_wait(arena, &cleanup_failure_run) != PROCESS_RESULT_SUCCESS &&
                                    !path_exists(arena, lifecycle_copy);
    }
    if (!lifecycle_present_cleanup || !lifecycle_absent_cleanup || !lifecycle_failure_cleanup)
    {
        string_print(S8("error: artifact fan-out private snapshot lifecycle cleanup test failed\n"));
        return PROCESS_RESULT_FAILED;
    }

    bool large_snapshot_result = true;
    if (include_large_snapshot)
    {
        OsFileDescriptor* large_snapshot_file =
            os_file_open(large_snapshot_source, (OpenFlags){.write = 1, .create = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
        bool large_snapshot_written = large_snapshot_file != 0;
        u8 large_snapshot_buffer[BUSTER_KB(64)] = {0};
        u64 large_snapshot_size = BUSTER_MB(65) + BUSTER_KB(1);
        if (large_snapshot_file)
        {
            for (u64 remaining = large_snapshot_size; remaining;)
            {
                u64 write_size = BUSTER_MIN(remaining, (u64)sizeof(large_snapshot_buffer));
                os_file_write(large_snapshot_file, (ByteSlice){large_snapshot_buffer, write_size});
                remaining -= write_size;
            }
            large_snapshot_written = os_file_close(large_snapshot_file);
        }
#if BUSTER_LINUX || BUSTER_MACOS
        large_snapshot_written = large_snapshot_written && chmod((const char*)large_snapshot_source.pointer, 0711) == 0;
#endif
        u64 large_snapshot_hash = 0;
        u64 large_snapshot_actual_size = 0;
        bool large_snapshot_digest = large_snapshot_written &&
                                     build_artifact_fanout_hash_file(arena, large_snapshot_source, &large_snapshot_hash, &large_snapshot_actual_size) &&
                                     large_snapshot_actual_size == large_snapshot_size;
        bool large_snapshot_copied = large_snapshot_digest &&
                                     build_artifact_fanout_snapshot(arena, large_snapshot_source, large_snapshot_copy, large_snapshot_hash,
                                                                    large_snapshot_actual_size);
        remove_path_recursive(arena, large_snapshot_source);
        remove_path_recursive(arena, large_snapshot_copy);
        bool large_snapshot_cleanup = !path_exists(arena, large_snapshot_source) && !path_exists(arena, large_snapshot_copy);
        if (!large_snapshot_copied || !large_snapshot_cleanup)
        {
            large_snapshot_result = false;
        }
    }
    if (!large_snapshot_result)
    {
        string_print(S8("error: artifact fan-out bounded-memory large snapshot test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("ARTIFACT_FANOUT_TESTS passed canonical=1 tcc=1 identity=1 config=1 flags=1 windows_flags=1 restricted=1 missing=1 provenance_record=1 mismatch=1 unclean_record=1 clean_lifecycle=1 record_consume=1 malformed=1 snapshot=1 large_snapshot={S8} cleanup=1\n"),
                 include_large_snapshot ? S8("1") : S8("skipped"));
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_requested_for_platform(bool ci, bool forced, bool macos, bool windows, bool x86_64)
{
    return forced || (ci && (macos || (windows && x86_64)));
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_consumer_supported_for_platform(bool linux_host, bool macos, bool windows, bool x86_64)
{
    return macos || (x86_64 && (linux_host || windows));
}

BUSTER_GLOBAL_LOCAL bool build_artifact_fanout_selection_is_valid(bool requested, bool supported, bool producer_selected)
{
    return !requested || (supported && producer_selected);
}

typedef struct ReleaseBuildParallelism ReleaseBuildParallelism;
struct ReleaseBuildParallelism
{
    u32 unity_jobs;
    u32 split_jobs;
};

BUSTER_GLOBAL_LOCAL ReleaseBuildParallelism release_build_parallelism(u32 thread_count, u32 unity_build_count, u32 split_build_count)
{
    thread_count = BUSTER_MAX(thread_count, 1);

    u32 unity_jobs = unity_build_count ? 1 : 0;
    u32 minimum_job_count = unity_build_count + split_build_count;
    if (unity_build_count && thread_count >= minimum_job_count + unity_build_count)
    {
        unity_jobs = 2;
    }

    u32 unity_job_count = unity_jobs * unity_build_count;
    u32 remaining_jobs = thread_count > unity_job_count ? thread_count - unity_job_count : 0;
    u32 split_jobs = split_build_count ? BUSTER_MAX(remaining_jobs / split_build_count, 1) : 0;
    return (ReleaseBuildParallelism){.unity_jobs = unity_jobs, .split_jobs = split_jobs};
}

BUSTER_GLOBAL_LOCAL ProcessResult release_build_parallelism_tests(void)
{
    typedef struct ReleaseBuildParallelismTest ReleaseBuildParallelismTest;
    struct ReleaseBuildParallelismTest
    {
        u32 thread_count;
        u32 unity_build_count;
        u32 split_build_count;
        u32 expected_unity_jobs;
        u32 expected_split_jobs;
    };

    ReleaseBuildParallelismTest tests[] = {
        {.thread_count = 32, .unity_build_count = 1, .split_build_count = 2, .expected_unity_jobs = 2, .expected_split_jobs = 15},
        {.thread_count = 32, .unity_build_count = 1, .split_build_count = 3, .expected_unity_jobs = 2, .expected_split_jobs = 10},
        {.thread_count = 8, .unity_build_count = 1, .split_build_count = 2, .expected_unity_jobs = 2, .expected_split_jobs = 3},
        {.thread_count = 4, .unity_build_count = 1, .split_build_count = 3, .expected_unity_jobs = 1, .expected_split_jobs = 1},
        {.thread_count = 2, .unity_build_count = 1, .split_build_count = 2, .expected_unity_jobs = 1, .expected_split_jobs = 1},
        {.thread_count = 1, .unity_build_count = 1, .split_build_count = 2, .expected_unity_jobs = 1, .expected_split_jobs = 1},
        {.thread_count = 8, .unity_build_count = 0, .split_build_count = 3, .expected_unity_jobs = 0, .expected_split_jobs = 2},
        {.thread_count = 8, .unity_build_count = 1, .split_build_count = 0, .expected_unity_jobs = 2, .expected_split_jobs = 0},
    };

    for (u32 test_i = 0; test_i < BUSTER_ARRAY_LENGTH(tests); test_i += 1)
    {
        ReleaseBuildParallelismTest test = tests[test_i];
        ReleaseBuildParallelism actual = release_build_parallelism(test.thread_count, test.unity_build_count, test.split_build_count);
        if (actual.unity_jobs != test.expected_unity_jobs || actual.split_jobs != test.expected_split_jobs)
        {
            string_print(S8("error: release build parallelism test {u32} failed: unity_jobs={u32}, split_jobs={u32}\n"), test_i, actual.unity_jobs,
                         actual.split_jobs);
            return PROCESS_RESULT_FAILED;
        }
    }
    return PROCESS_RESULT_SUCCESS;
}

typedef struct MatrixTestCombination MatrixTestCombination;
struct MatrixTestCombination
{
    String8 build_directory;
    CmakeBuildOptions options;
    Generate generate;
    BuildCompiler compiler;
    u32 sanitize : 1;
    u32 run_app_tests : 1;
};

typedef struct MatrixTestTree MatrixTestTree;
struct MatrixTestTree
{
    String8 build_directory;
    u64 combination_indices[2];
    u32 combination_count;
    u32 parallel_jobs;
    u32 unity_only : 1;
};

typedef struct MatrixSuperbuildSelfHostPlan MatrixSuperbuildSelfHostPlan;
struct MatrixSuperbuildSelfHostPlan
{
    String8 build_directory;
    String8 config;
    String8 build_driver;
    String8 provenance_record_path;
    u32 tree_index;
    u32 pool_jobs;
    u32 fuzz_available : 1;
    u32 enabled : 1;
    u32 ci : 1;
    u32 depends_on_compile : 1;
    u32 uses_inner_ninja : 1;
    u32 producer_clean_required : 1;
};

BUSTER_GLOBAL_LOCAL u32 matrix_superbuild_outer_jobs(u32 thread_count, u32 tree_count)
{
    if (!tree_count)
    {
        return 0;
    }
    thread_count = BUSTER_MAX(thread_count, 1);
    // On a low-core runner, admit one tree per logical CPU and give every
    // inner build one job. This keeps the complete matrix moving without
    // nesting several two-job Ninja processes on the same four CPUs.
    if (thread_count <= 4)
    {
        return BUSTER_MIN(thread_count, tree_count);
    }

    // Larger runners retain the weighted schedule: reserve at least two
    // logical CPUs per admitted split tree and let unity trees use one job.
    u32 split_tree_limit = BUSTER_MAX(thread_count / 2, 1);
    return BUSTER_MIN(split_tree_limit, tree_count);
}

// Ninja admits ready edges from a shared pool in declaration order, and a fresh
// CI checkout has no `.ninja_log` for its critical-path scheduler to learn from.
// Declaring the longest trees first therefore decides whether the slowest test
// command starts as soon as its compile finishes or waits behind shorter trees.
// The ranking follows the measured cost ordering: sanitized Debug dominates,
// then sanitized Release, then the unity Release tree that also runs
// clang_analyze, then trees that test two configurations, then the rest.
BUSTER_GLOBAL_LOCAL u32 matrix_superbuild_tree_schedule_rank(bool sanitize, bool optimize, bool unity_only, u32 combination_count)
{
    if (sanitize)
    {
        return optimize ? 3 : 4;
    }
    if (unity_only)
    {
        return 2;
    }
    return combination_count > 1 ? 1 : 0;
}

// Stable insertion sort: equal ranks keep the compiler/configuration order the
// matrix loop produced, so the manifest stays deterministic across hosts.
BUSTER_GLOBAL_LOCAL void matrix_superbuild_order_trees(MatrixTestTree* trees, u32* ranks, u32 tree_count)
{
    for (u32 tree_i = 1; tree_i < tree_count; tree_i += 1)
    {
        MatrixTestTree tree = trees[tree_i];
        u32 rank = ranks[tree_i];
        u32 position = tree_i;
        while (position > 0 && ranks[position - 1] < rank)
        {
            trees[position] = trees[position - 1];
            ranks[position] = ranks[position - 1];
            position -= 1;
        }
        trees[position] = tree;
        ranks[position] = rank;
    }
}

BUSTER_GLOBAL_LOCAL void matrix_superbuild_allocate_jobs(MatrixTestTree* trees, u32 tree_count, u32 thread_count)
{
    thread_count = BUSTER_MAX(thread_count, 1);
    u32 outer_jobs = matrix_superbuild_outer_jobs(thread_count, tree_count);
    if (outer_jobs < tree_count)
    {
        u32 split_jobs = BUSTER_MAX(thread_count / outer_jobs, 1);
        for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
        {
            trees[tree_i].parallel_jobs = trees[tree_i].unity_only ? 1 : split_jobs;
        }
        return;
    }

    for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
    {
        trees[tree_i].parallel_jobs = 1;
    }

    u32 remaining_jobs = thread_count > tree_count ? thread_count - tree_count : 0;
    for (u32 tree_i = 0; tree_i < tree_count && remaining_jobs; tree_i += 1)
    {
        if (trees[tree_i].unity_only)
        {
            trees[tree_i].parallel_jobs += 1;
            remaining_jobs -= 1;
        }
    }

    while (remaining_jobs)
    {
        bool assigned = false;
        for (u32 tree_i = 0; tree_i < tree_count && remaining_jobs; tree_i += 1)
        {
            if (!trees[tree_i].unity_only)
            {
                trees[tree_i].parallel_jobs += 1;
                remaining_jobs -= 1;
                assigned = true;
            }
        }
        if (!assigned)
        {
            break;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool matrix_superbuild_self_host_enabled(bool direct_matrix, bool fanout_requested)
{
    return !direct_matrix && fanout_requested;
}

BUSTER_GLOBAL_LOCAL bool matrix_superbuild_self_host_cpu_budget_valid(MatrixTestTree* trees, u32 tree_count, u32 thread_count)
{
    u32 outer_jobs = matrix_superbuild_outer_jobs(thread_count, tree_count);
    if (!outer_jobs || tree_count > BUILD_COMPILER_COUNT * 2)
    {
        return false;
    }

    bool selected[BUILD_COMPILER_COUNT * 2] = {0};
    u32 selected_count = 0;
    u32 cpu_budget = 1;
    while (selected_count + 1 < outer_jobs)
    {
        u32 best_tree = tree_count;
        for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
        {
            if (!selected[tree_i] && (best_tree == tree_count || trees[tree_i].parallel_jobs > trees[best_tree].parallel_jobs))
            {
                best_tree = tree_i;
            }
        }
        if (best_tree == tree_count || !trees[best_tree].parallel_jobs)
        {
            return false;
        }
        selected[best_tree] = true;
        cpu_budget += trees[best_tree].parallel_jobs;
        selected_count += 1;
    }
    return cpu_budget <= BUSTER_MAX(thread_count, 1);
}

BUSTER_GLOBAL_LOCAL bool matrix_superbuild_self_host_plan_valid(MatrixSuperbuildSelfHostPlan plan, MatrixTestTree* trees, u32 tree_count,
                                                                  u32 thread_count)
{
    bool result = !plan.enabled;
    if (plan.enabled)
    {
        result = plan.tree_index < tree_count && string_equal(plan.build_directory, trees[plan.tree_index].build_directory) &&
                 string_equal(plan.config, S8("Release")) && plan.build_driver.length && plan.provenance_record_path.length && plan.pool_jobs == 1 &&
                 plan.depends_on_compile && !plan.uses_inner_ninja && plan.producer_clean_required &&
                 matrix_superbuild_self_host_cpu_budget_valid(trees, tree_count, thread_count);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool matrix_superbuild_manifest_write(Arena* arena, String8 path, String8 source_directory, String8 build_driver,
                                                           MatrixTestTree* trees, u32 tree_count, u32 outer_jobs,
                                                           MatrixTestCombination* combinations, MatrixSuperbuildSelfHostPlan self_host,
                                                           bool verbose, bool quiet);

BUSTER_GLOBAL_LOCAL ProcessResult matrix_superbuild_ordering_tests(void)
{
    // Ranks follow the measured per-tree cost ordering on both Linux and Windows.
    if (matrix_superbuild_tree_schedule_rank(true, false, false, 1) != 4 ||
        matrix_superbuild_tree_schedule_rank(true, true, false, 1) != 3 ||
        matrix_superbuild_tree_schedule_rank(false, true, true, 1) != 2 ||
        matrix_superbuild_tree_schedule_rank(false, false, false, 2) != 1 ||
        matrix_superbuild_tree_schedule_rank(false, false, false, 1) != 0)
    {
        string_print(S8("error: superbuild tree schedule rank policy changed\n"));
        return PROCESS_RESULT_FAILED;
    }

    // The Linux matrix order the combination loop produces, longest-pole last.
    MatrixTestTree trees[6] = {
        {.build_directory = S8("clang-debug")},
        {.build_directory = S8("clang-release"), .unity_only = 1},
        {.build_directory = S8("clang-sanitize-debug")},
        {.build_directory = S8("clang-sanitize-release")},
        {.build_directory = S8("gcc-shared"), .combination_count = 2},
        {.build_directory = S8("zig-shared"), .combination_count = 2},
    };
    u32 ranks[BUSTER_ARRAY_LENGTH(trees)] = {0, 2, 4, 3, 1, 1};
    matrix_superbuild_order_trees(trees, ranks, BUSTER_ARRAY_LENGTH(trees));
    String8 expected[] = {
        S8_INITIALIZER("clang-sanitize-debug"), S8_INITIALIZER("clang-sanitize-release"), S8_INITIALIZER("clang-release"),
        S8_INITIALIZER("gcc-shared"),           S8_INITIALIZER("zig-shared"),             S8_INITIALIZER("clang-debug"),
    };
    for (u32 tree_i = 0; tree_i < BUSTER_ARRAY_LENGTH(trees); tree_i += 1)
    {
        if (!string_equal(trees[tree_i].build_directory, expected[tree_i]))
        {
            string_print(S8("error: superbuild tree ordering test failed at {u32}: {S8}\n"), tree_i, trees[tree_i].build_directory);
            return PROCESS_RESULT_FAILED;
        }
    }

    // Equal ranks must keep their original relative order so the manifest is
    // deterministic; gcc-shared and zig-shared both rank 1 above.
    if (!string_equal(trees[3].build_directory, S8("gcc-shared")) || !string_equal(trees[4].build_directory, S8("zig-shared")))
    {
        string_print(S8("error: superbuild tree ordering is not stable for equal ranks\n"));
        return PROCESS_RESULT_FAILED;
    }
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult matrix_superbuild_parallelism_tests(Arena* arena)
{
    ProcessResult ordering_result = matrix_superbuild_ordering_tests();
    if (ordering_result != PROCESS_RESULT_SUCCESS)
    {
        return ordering_result;
    }

    MatrixTestTree linux_trees[6] = {
        {.build_directory = S8("linux-debug")},
        {.build_directory = S8("linux-canonical"), .unity_only = 1},
        {0},
        {0},
        {0},
        {0},
    };
    matrix_superbuild_allocate_jobs(linux_trees, BUSTER_ARRAY_LENGTH(linux_trees), 16);
    u32 expected_linux_unity[] = {0, 1, 0, 0, 0, 0};
    u32 expected_linux_jobs[] = {3, 2, 3, 3, 3, 2};
    for (u32 tree_i = 0; tree_i < BUSTER_ARRAY_LENGTH(linux_trees); tree_i += 1)
    {
        if (linux_trees[tree_i].unity_only != expected_linux_unity[tree_i] ||
            linux_trees[tree_i].parallel_jobs != expected_linux_jobs[tree_i])
        {
            string_print(S8("error: superbuild Linux manifest allocation test failed at tree {u32}: unity={u32} jobs={u32}\n"), tree_i,
                         linux_trees[tree_i].unity_only, linux_trees[tree_i].parallel_jobs);
            return PROCESS_RESULT_FAILED;
        }
    }

    MatrixTestTree windows_trees[7] = {
        {0},
        {0},
        {.build_directory = S8("windows-canonical"), .unity_only = 1},
        {0},
        {0},
        {0},
        {0},
    };
    matrix_superbuild_allocate_jobs(windows_trees, BUSTER_ARRAY_LENGTH(windows_trees), 4);
    u32 expected_windows_unity[] = {0, 0, 1, 0, 0, 0, 0};
    u32 expected_windows_jobs[] = {1, 1, 1, 1, 1, 1, 1};
    for (u32 tree_i = 0; tree_i < BUSTER_ARRAY_LENGTH(windows_trees); tree_i += 1)
    {
        if (windows_trees[tree_i].unity_only != expected_windows_unity[tree_i] ||
            windows_trees[tree_i].parallel_jobs != expected_windows_jobs[tree_i])
        {
            string_print(S8("error: superbuild Windows manifest allocation test failed at tree {u32}: unity={u32} jobs={u32}\n"), tree_i,
                         windows_trees[tree_i].unity_only, windows_trees[tree_i].parallel_jobs);
            return PROCESS_RESULT_FAILED;
        }
    }
    MatrixSuperbuildSelfHostPlan windows_self_host = {
        .build_directory = S8("windows-canonical"),
        .config = S8("Release"),
        .build_driver = S8("build/build.exe"),
        .provenance_record_path = S8("superbuild/matrix.provenance"),
        .tree_index = 2,
        .pool_jobs = 1,
        .enabled = 1,
        .ci = 1,
        .depends_on_compile = 1,
        .uses_inner_ninja = 0,
        .producer_clean_required = 1,
    };
    MatrixSuperbuildSelfHostPlan linux_self_host = {
        .build_directory = S8("linux-canonical"),
        .config = S8("Release"),
        .build_driver = S8("build/build"),
        .provenance_record_path = S8("superbuild/matrix.provenance"),
        .tree_index = 1,
        .pool_jobs = 1,
        .enabled = 1,
        .ci = 1,
        .depends_on_compile = 1,
        .uses_inner_ninja = 0,
        .producer_clean_required = 1,
    };
    MatrixSuperbuildSelfHostPlan invalid_self_host = windows_self_host;
    invalid_self_host.tree_index = BUSTER_ARRAY_LENGTH(windows_trees);
    MatrixSuperbuildSelfHostPlan malformed_manifest = windows_self_host;
    malformed_manifest.build_driver = (String8){0};
    MatrixSuperbuildSelfHostPlan missing_provenance = windows_self_host;
    missing_provenance.provenance_record_path = (String8){0};
    MatrixSuperbuildSelfHostPlan missing_producer_clean = windows_self_host;
    missing_producer_clean.producer_clean_required = 0;
    String8 manifest_path = string_format_z(arena, S8("build/superbuild-manifest-{u64}.cmake"), os_get_current_process_id());
    remove_path_recursive(arena, manifest_path);
    MatrixTestCombination manifest_combination = {
        .build_directory = S8("build/canonical"),
        .options = {.config = S8("Release"), .optimize = 1, .optimize_set = 1},
        .compiler = BUILD_COMPILER_CLANG,
    };
    MatrixTestTree manifest_tree = {
        .build_directory = S8("build/canonical"),
        .combination_indices = {0},
        .combination_count = 1,
        .parallel_jobs = 1,
        .unity_only = 1,
    };
    MatrixSuperbuildSelfHostPlan manifest_plan = {
        .build_directory = manifest_tree.build_directory,
        .config = S8("Release"),
        .build_driver = S8("/absolute/build-driver"),
        .provenance_record_path = S8("/absolute/provenance.record"),
        .tree_index = 0,
        .pool_jobs = 1,
        .enabled = 1,
        .ci = 1,
        .depends_on_compile = 1,
        .producer_clean_required = 1,
    };
    bool manifest_written = matrix_superbuild_manifest_write(arena, manifest_path, S8("/absolute/source"), manifest_plan.build_driver,
                                                              &manifest_tree, 1, 1, &manifest_combination, manifest_plan, false, false);
    ByteSlice manifest_bytes = file_read(arena, manifest_path, (FileReadOptions){.map_required = 0});
    String8 manifest = BYTE_SLICE_TO_STRING(8, manifest_bytes);
    bool manifest_fields_valid = manifest_written && manifest.pointer &&
                                 string_first_sequence(manifest, S8("set(BUSTER_SUPERBUILD_BUILD_DRIVER [==[/absolute/build-driver]==])")) !=
                                     BUSTER_STRING_NO_MATCH &&
                                 string_first_sequence(manifest, S8("BUSTER_SUPERBUILD_SELF_HOST_PROVENANCE_RECORD")) != BUSTER_STRING_NO_MATCH &&
                                 string_first_sequence(manifest, S8("BUSTER_SUPERBUILD_SELF_HOST_PRODUCER_CLEAN_REQUIRED")) != BUSTER_STRING_NO_MATCH &&
                                 string_first_sequence(manifest, S8("BUSTER_SUPERBUILD_SELF_HOST_BUILD_DRIVER")) == BUSTER_STRING_NO_MATCH;
    remove_path_recursive(arena, manifest_path);
    if (matrix_superbuild_outer_jobs(4, BUSTER_ARRAY_LENGTH(windows_trees)) != 4 || matrix_superbuild_outer_jobs(16, 6) != 6 ||
        matrix_superbuild_outer_jobs(0, 0) != 0 || matrix_superbuild_self_host_enabled(true, true) ||
        !matrix_superbuild_self_host_enabled(false, true) || matrix_superbuild_self_host_enabled(false, false) ||
        !matrix_superbuild_self_host_plan_valid(windows_self_host, windows_trees, BUSTER_ARRAY_LENGTH(windows_trees), 4) ||
        !matrix_superbuild_self_host_plan_valid(linux_self_host, linux_trees, BUSTER_ARRAY_LENGTH(linux_trees), 16) ||
        matrix_superbuild_self_host_plan_valid(invalid_self_host, windows_trees, BUSTER_ARRAY_LENGTH(windows_trees), 4) ||
        matrix_superbuild_self_host_plan_valid(malformed_manifest, windows_trees, BUSTER_ARRAY_LENGTH(windows_trees), 4) ||
        matrix_superbuild_self_host_plan_valid(missing_provenance, windows_trees, BUSTER_ARRAY_LENGTH(windows_trees), 4) ||
        matrix_superbuild_self_host_plan_valid(missing_producer_clean, windows_trees, BUSTER_ARRAY_LENGTH(windows_trees), 4) || !manifest_fields_valid ||
        path_exists(arena, manifest_path))
    {
        string_print(S8("error: superbuild self-host resource/dependency allocation test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL bool matrix_superbuild_manifest_write(Arena* arena, String8 path, String8 source_directory, String8 build_driver,
                                                           MatrixTestTree* trees, u32 tree_count, u32 outer_jobs,
                                                           MatrixTestCombination* combinations, MatrixSuperbuildSelfHostPlan self_host,
                                                           bool verbose, bool quiet)
{
    String8List lines = {0};
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_SOURCE_DIR [==[{S8}]==])\n"), source_directory));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_BUILD_DRIVER [==[{S8}]==])\n"), build_driver));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_TREE_COUNT {u32})\n"), tree_count));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_OUTER_JOBS {u32})\n"), outer_jobs));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_VERBOSE {S8})\n"), verbose ? S8("ON") : S8("OFF")));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_QUIET {S8})\n"), quiet ? S8("ON") : S8("OFF")));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_ENABLED {u32})\n"), self_host.enabled));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_BUILD_DIRECTORY [==[{S8}]==])\n"),
                                                        self_host.build_directory));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_PROVENANCE_RECORD [==[{S8}]==])\n"),
                                    self_host.provenance_record_path));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_CONFIG {S8})\n"), self_host.config));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_TREE_INDEX {u32})\n"), self_host.tree_index));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_POOL_JOBS {u32})\n"), self_host.pool_jobs));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_FUZZ_AVAILABLE {u32})\n"), self_host.fuzz_available));
    string8_list_push(arena, &lines, string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_CI {S8})\n"), self_host.ci ? S8("ON") : S8("OFF")));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_DEPENDS_ON_COMPILE {u32})\n"), self_host.depends_on_compile));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_USES_INNER_NINJA {u32})\n"), self_host.uses_inner_ninja));
    string8_list_push(arena, &lines,
                      string_format(arena, S8("set(BUSTER_SUPERBUILD_SELF_HOST_PRODUCER_CLEAN_REQUIRED {u32})\n"), self_host.producer_clean_required));

    for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
    {
        MatrixTestTree tree = trees[tree_i];
        MatrixTestCombination first = combinations[tree.combination_indices[0]];
        String8 first_config = cmake_build_config(first.options);
        String8 first_target = first.run_app_tests ? S8("test_all") : S8("test_units");
        String8 second_config = {0};
        String8 second_target = {0};
        if (tree.combination_count > 1)
        {
            MatrixTestCombination second = combinations[tree.combination_indices[1]];
            second_config = cmake_build_config(second.options);
            second_target = second.run_app_tests ? S8("test_all") : S8("test_units");
        }

        String8 analyze_config = {0};
        for (u32 combination_i = 0; combination_i < tree.combination_count; combination_i += 1)
        {
            MatrixTestCombination combination = combinations[tree.combination_indices[combination_i]];
            if (combination.compiler == BUILD_COMPILER_CLANG && !combination.sanitize && combination.options.optimize)
            {
                analyze_config = cmake_build_config(combination.options);
            }
        }

        String8 prefix = string_format(arena, S8("BUSTER_SUPERBUILD_TREE_{u32}"), tree_i);
        string8_list_push(arena, &lines,
                          string_format(arena, S8("set({S8}_BUILD_DIRECTORY [==[{S8}]==])\n"), prefix, tree.build_directory));
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_NATIVE_CONFIG {S8})\n"), prefix, first_config));
        if (second_config.length)
        {
            string8_list_push(arena, &lines,
                              string_format(arena, S8("set({S8}_BUILD_TARGETS ide:{S8} ide:{S8})\n"), prefix, first_config, second_config));
        }
        else
        {
            string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_BUILD_TARGETS ide:{S8})\n"), prefix, first_config));
        }
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_PARALLEL_JOBS {u32})\n"), prefix, tree.parallel_jobs));
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_UNITY_ONLY {u32})\n"), prefix, tree.unity_only));
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_TEST_0_CONFIG {S8})\n"), prefix, first_config));
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_TEST_0_TARGET {S8})\n"), prefix, first_target));
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_TEST_1_CONFIG {S8})\n"), prefix, second_config));
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_TEST_1_TARGET {S8})\n"), prefix, second_target));
        string8_list_push(arena, &lines, string_format(arena, S8("set({S8}_ANALYZE_CONFIG {S8})\n"), prefix, analyze_config));
    }

    String8 manifest = string_join_arena(arena, string8_list_to_slice(arena, lines), true);
    return file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(manifest));
}

BUSTER_GLOBAL_LOCAL void matrix_superbuild_generate_add(Arena* arena, BuildStep* step, String8 build_directory, String8 manifest_path)
{
    String8 manifest_argument = cmake_string(arena, S8("BUSTER_SUPERBUILD_MATRIX_FILE"), manifest_path);
    GenericRun r = generic_tool_run_add_start(arena, step, &cmake_path, S8("cmake"));
    OsArgumentBuilder* b = &r.builder;
    os_argument_builder_append(b, S8("-S"));
    os_argument_builder_append(b, S8("cmake/superbuild"));
    os_argument_builder_append(b, S8("-B"));
    os_argument_builder_append(b, build_directory);
    os_argument_builder_append(b, S8("-G"));
    os_argument_builder_append(b, S8("Ninja"));
    os_argument_builder_append(b, manifest_argument);
    generic_tool_run_add_end(r);
}

BUSTER_GLOBAL_LOCAL ProcessResult test_all(Arena* arena, bool ci, CmakeBuildOptions base_options)
{
    ProcessResult time_trace_self_test_result = time_trace_summary_self_test(arena);
    if (time_trace_self_test_result != PROCESS_RESULT_SUCCESS)
    {
        return time_trace_self_test_result;
    }
    ProcessResult test_timing_self_test_result = test_timing_summary_self_test(arena);
    if (test_timing_self_test_result != PROCESS_RESULT_SUCCESS)
    {
        return test_timing_self_test_result;
    }

    bool fanout_forced = environment_flag_is_on(S8("BUSTER_TEST_FORCE_ARTIFACT_FANOUT"));
    bool fanout_requested = build_artifact_fanout_requested_for_platform(ci, fanout_forced, BUSTER_MACOS != 0, BUSTER_WINDOWS != 0,
                                                                          BUSTER_CPU_ARCH_X86_64 != 0);
    bool fanout_supported = build_artifact_fanout_consumer_supported_for_platform(BUSTER_LINUX != 0, BUSTER_MACOS != 0, BUSTER_WINDOWS != 0,
                                                                                     BUSTER_CPU_ARCH_X86_64 != 0);
    if (fanout_requested && !fanout_supported)
    {
        string_print(S8("error: artifact fan-out was requested on an unsupported self-host consumer platform\n"));
        return PROCESS_RESULT_FAILED;
    }
    ProcessResult focused_test_result = build_artifact_fanout_tests(arena, fanout_forced);
    if (focused_test_result != PROCESS_RESULT_SUCCESS)
    {
        return focused_test_result;
    }
    ProcessResult release_parallelism_test_result = release_build_parallelism_tests();
    if (release_parallelism_test_result != PROCESS_RESULT_SUCCESS)
    {
        return release_parallelism_test_result;
    }
    ProcessResult superbuild_parallelism_test_result = matrix_superbuild_parallelism_tests(arena);
    if (superbuild_parallelism_test_result != PROCESS_RESULT_SUCCESS)
    {
        return superbuild_parallelism_test_result;
    }

    bool direct_matrix = environment_flag_is_on(S8("BUSTER_MATRIX_DIRECT"));
    MatrixTestCombination combinations[BUILD_COMPILER_COUNT * 4] = {0};
    u64 combination_count = 0;
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
        bool fuzz_supported = compiler == BUILD_COMPILER_CLANG && !BUSTER_APPLE;
        bool support_sanitize = compiler == BUILD_COMPILER_CLANG;
        bool support_optimize = true;

        for (u32 sanitize = 0; sanitize < 1 + support_sanitize; sanitize += 1)
        {
            u32 tree_count = fuzz_supported ? 1 + support_optimize : 1;
            for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
            {
                u32 first_optimize = fuzz_supported ? tree_i : 0;
                u32 optimize_count = fuzz_supported ? 1 : 1 + support_optimize;
                bool fuzz_available = fuzz_supported && ((sanitize && !first_optimize) || (!sanitize && first_optimize));
                String8 build_directory_parts[] = {
                    build_prefix,
                    S8("ci_"),
                    ci ? S8("on") : S8("off"),
                    S8("-cc_"),
                    build_compilers[compiler],
                    S8("-sanitize_"),
                    sanitize ? S8("on") : S8("off"),
                    S8("-fuzz_available_"),
                    fuzz_available ? S8("on") : S8("off"),
                    S8("-configs_"),
                    fuzz_supported ? (first_optimize ? S8("Release") : S8("Debug")) : S8("shared"),
                };

                String8 build_directory = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(build_directory_parts), true);
                String8 configuration_types = optimize_count > 1 ? S8("Debug;Release") : (first_optimize ? S8("Release") : S8("Debug"));
                String8 cmake_profile_path = path_join(arena, build_directory, S8("cmake-profile.json"));

                Generate generate = {
                    .build_directory = build_directory,
                    .configuration_types = configuration_types,
                    .cmake_profile = cmake_profile_path,
                    .cmake_profile_summary_limit = cmake_profile_summary_limit,
                    .compiler = compiler,
                    .fuzz_available = fuzz_available,
                    .sanitize = sanitize,
                    .ci = ci,
                    .optimize = first_optimize,
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
                    .cross_configs = !direct_matrix,
                    .cmake_arguments = ci ? (SliceString8)BUSTER_ARRAY_TO_SLICE(ci_cmake_arguments) : (SliceString8){0},
                };

                generate_add(arena, generate_step, generate);
                if (cmake_profile)
                {
                    cmake_profile_summary_add(arena, profile_summary_step, cmake_profile_path, cmake_profile_summary_limit);
                }

                for (u32 optimize_i = 0; optimize_i < optimize_count; optimize_i += 1)
                {
                    u32 optimize = first_optimize + optimize_i;
                    combinations[combination_count++] = (MatrixTestCombination){
                        .build_directory = build_directory,
                        .options =
                            {
                                .optimize = optimize,
                                .optimize_set = true,
                                .quiet = base_options.quiet,
                            },
                        .compiler = compiler,
                        .generate = generate,
                        .sanitize = sanitize,
                        .run_app_tests = (sanitize && !optimize) || (!sanitize && optimize),
                    };
                }
            }
        }
    }

    MatrixTestTree trees[BUILD_COMPILER_COUNT * 2] = {0};
    u32 tree_count = 0;
    for (u64 combination_i = 0; combination_i < combination_count; combination_i += 1)
    {
        MatrixTestCombination combination = combinations[combination_i];
        u32 tree_i = 0;
        for (; tree_i < tree_count; tree_i += 1)
        {
            if (string_equal(trees[tree_i].build_directory, combination.build_directory))
            {
                break;
            }
        }
        if (tree_i == tree_count)
        {
            if (tree_count == BUSTER_ARRAY_LENGTH(trees))
            {
                string_print(S8("error: superbuild compiler tree capacity exceeded\n"));
                return PROCESS_RESULT_FAILED;
            }
            trees[tree_count++].build_directory = combination.build_directory;
        }
        MatrixTestTree* tree = &trees[tree_i];
        if (tree->combination_count == BUSTER_ARRAY_LENGTH(tree->combination_indices))
        {
            string_print(S8("error: superbuild compiler tree has more configurations than supported: {S8}\n"), tree->build_directory);
            return PROCESS_RESULT_FAILED;
        }
        tree->combination_indices[tree->combination_count++] = combination_i;
    }

    for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
    {
        MatrixTestTree* tree = &trees[tree_i];
        if (tree->combination_count == 1)
        {
            MatrixTestCombination combination = combinations[tree->combination_indices[0]];
            tree->unity_only = combination.compiler == BUILD_COMPILER_CLANG && !combination.sanitize && combination.options.optimize;
        }
    }

    // Declare the longest trees first so their test commands enter the shared
    // pool as soon as their compile finishes instead of queueing behind shorter
    // trees. Ordering happens before job allocation and before the fan-out tree
    // index is resolved by build directory, so both stay consistent.
    u32 tree_ranks[BUSTER_ARRAY_LENGTH(trees)] = {0};
    for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
    {
        MatrixTestCombination first = combinations[trees[tree_i].combination_indices[0]];
        tree_ranks[tree_i] = matrix_superbuild_tree_schedule_rank(first.sanitize, first.options.optimize, trees[tree_i].unity_only,
                                                                 trees[tree_i].combination_count);
    }
    if (!environment_flag_is_on(S8("BUSTER_MATRIX_NO_TREE_ORDER")))
    {
        matrix_superbuild_order_trees(trees, tree_ranks, tree_count);
    }

    BuildArtifactFanout* fanout = 0;
    u32 fanout_tree_index = tree_count;
    String8 ide_name =
#if BUSTER_WINDOWS
        S8("ide.exe");
#else
        S8("ide");
#endif
    for (u64 combination_i = 0; fanout_requested && combination_i < combination_count; combination_i += 1)
    {
        MatrixTestCombination combination = combinations[combination_i];
        if (build_artifact_fanout_is_canonical(combination.generate, combination.options))
        {
            if (fanout)
            {
                string_print(S8("error: more than one canonical Clang Release combination was selected for artifact fan-out\n"));
                return PROCESS_RESULT_FAILED;
            }
            fanout = arena_allocate(arena, BuildArtifactFanout, 1);
            *fanout = (BuildArtifactFanout){
                .build_directory = combination.build_directory,
                .artifact_path = path_join(arena, path_join(arena, combination.build_directory, S8("Release")), ide_name),
                .generate = combination.generate,
                .options = combination.options,
            };
            for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
            {
                if (string_equal(trees[tree_i].build_directory, combination.build_directory))
                {
                    fanout_tree_index = tree_i;
                    break;
                }
            }
        }
    }

    if (!build_artifact_fanout_selection_is_valid(fanout_requested, fanout_supported, fanout != 0))
    {
        if (!fanout_supported)
        {
            string_print(S8("error: artifact fan-out was requested on an unsupported self-host consumer platform\n"));
        }
        else
        {
            string_print(S8("error: artifact fan-out was requested but no canonical Clang Release producer was selected\n"));
        }
        return PROCESS_RESULT_FAILED;
    }

    // `get_nprocs()` ignores CPU affinity, so the only way to reproduce a small
    // runner's admission behaviour on a large host is to state the budget.
    u32 thread_count = (u32)environment_positive_u64_or(S8("BUSTER_MATRIX_THREADS"), os_get_logical_thread_count());
    String8 superbuild_directory = {0};
    if (!direct_matrix)
    {
        matrix_superbuild_allocate_jobs(trees, tree_count, thread_count);
        string_print(S8("BUSTER_SUPERBUILD_PARALLELISM: threads={u32} trees={u32} outer_jobs={u32}\n"), thread_count, tree_count,
                     matrix_superbuild_outer_jobs(thread_count, tree_count));
        for (u32 tree_i = 0; tree_i < tree_count; tree_i += 1)
        {
            string_print(S8("BUSTER_SUPERBUILD_TREE: index={u32} configs={u32} unity_only={u32} jobs={u32} directory={S8}\n"), tree_i,
                         trees[tree_i].combination_count, trees[tree_i].unity_only, trees[tree_i].parallel_jobs, trees[tree_i].build_directory);
        }

        superbuild_directory = string_format(arena, S8("{S8}superbuild-ci_{S8}"), build_prefix, ci ? S8("on") : S8("off"));
        remove_path_recursive(arena, superbuild_directory);
        make_directory_recursive(arena, superbuild_directory);
        String8 source_directory = os_path_absolute(arena, S8("."), true);
        String8 superbuild_directory_z = string_duplicate_arena(arena, superbuild_directory, true);
        String8 superbuild_absolute_directory = os_path_absolute(arena, superbuild_directory_z, true);
        String8 manifest_path = path_join(arena, superbuild_absolute_directory, S8("matrix.cmake"));
        String8 build_driver = path_join(arena, source_directory,
#if BUSTER_WINDOWS
                                         S8("build/build.exe")
#else
                                         S8("build/build")
#endif
        );
        String8 provenance_record_path = {0};
        if (fanout && fanout_requested)
        {
            provenance_record_path = path_join(arena, superbuild_absolute_directory, S8("canonical-artifact.provenance"));
            fanout->provenance_record_path = provenance_record_path;
        }
        MatrixSuperbuildSelfHostPlan self_host_plan = {
            .build_directory = fanout && fanout_requested ? fanout->build_directory : (String8){0},
            .config = fanout && fanout_requested ? cmake_build_config(fanout->options) : (String8){0},
            .build_driver = fanout && fanout_requested ? build_driver : (String8){0},
            .provenance_record_path = provenance_record_path,
            .tree_index = fanout && fanout_requested ? fanout_tree_index : 0,
            .pool_jobs = fanout && fanout_requested ? 1 : 0,
            .fuzz_available = fanout && fanout_requested ? fanout->generate.fuzz_available : 0,
            .enabled = matrix_superbuild_self_host_enabled(direct_matrix, fanout_requested && fanout),
            .ci = fanout && fanout_requested ? fanout->generate.ci : 0,
            .depends_on_compile = fanout && fanout_requested ? 1 : 0,
            .uses_inner_ninja = 0,
            .producer_clean_required = fanout && fanout_requested ? 1 : 0,
        };
        if (!matrix_superbuild_self_host_plan_valid(self_host_plan, trees, tree_count, thread_count))
        {
            string_print(S8("error: invalid superbuild self-host resource/dependency plan\n"));
            return PROCESS_RESULT_FAILED;
        }
        if (!source_directory.length || !superbuild_absolute_directory.length || !build_driver.length ||
            !matrix_superbuild_manifest_write(arena, manifest_path, source_directory, build_driver, trees, tree_count,
                                              matrix_superbuild_outer_jobs(thread_count, tree_count), combinations, self_host_plan,
                                              base_options.verbose, base_options.quiet))
        {
            string_print(S8("error: failed to write the CMake superbuild matrix manifest\n"));
            return PROCESS_RESULT_FAILED;
        }
        matrix_superbuild_generate_add(arena, generate_step, superbuild_directory, manifest_path);
    }

    if (fanout && fanout_requested)
    {
        BuildStep* capture_step = step_add(arena);
        ProcessRun* capture_run = run_add(arena, capture_step);
        *capture_run = (ProcessRun){
            .callback = build_artifact_fanout_capture_action,
            .callback_data = fanout,
        };

        // CMake does not model compiler/linker/shader executable bytes as
        // dependencies of every producer output. A reused canonical tree can
        // therefore be a successful Ninja no-op after a same-path tool change.
        // Run the generated Release clean target after capture and before any
        // matrix Ninja starts. This process is strictly serialized with the
        // outer superbuild, preserves CMake's cache/graph, and makes every
        // relevant producer output regenerate under the captured inputs.
        fanout->producer_clean_scheduled = 1;
        String8 clean_target[] = {S8("clean")};
        CmakeBuildOptions clean_options = fanout->options;
        clean_options.config = S8("Release");
        clean_options.optimize = 1;
        clean_options.optimize_set = 1;
        clean_options.parallel_jobs = 1;
        clean_options.quiet = base_options.quiet;
        clean_options.verbose = base_options.verbose;
        BuildStep* producer_clean_step = step_add(arena);
        ProcessRun* producer_clean_run = build_run_add(arena, producer_clean_step, fanout->build_directory,
                                                        (SliceString8)BUSTER_ARRAY_TO_SLICE(clean_target), (SliceString8){0}, clean_options);
        producer_clean_run->timing_description = S8("Canonical Clang Release producer clean");
        BuildStep* producer_clean_success_step = step_add(arena);
        ProcessRun* producer_clean_success_run = run_add(arena, producer_clean_success_step);
        *producer_clean_success_run = (ProcessRun){
            .callback = build_artifact_fanout_clean_action,
            .callback_data = fanout,
        };
    }

    if (!direct_matrix)
    {
        String8 superbuild_targets[] = {S8("buster_matrix")};
        CmakeBuildOptions superbuild_options = base_options;
        superbuild_options.config = S8("Debug");
        superbuild_options.parallel_jobs = matrix_superbuild_outer_jobs(thread_count, tree_count);
        BuildStep* superbuild_step = step_add(arena);
        build_run_add(arena, superbuild_step, superbuild_directory, (SliceString8)BUSTER_ARRAY_TO_SLICE(superbuild_targets), (SliceString8){0},
                      superbuild_options);
        return PROCESS_RESULT_SUCCESS;
    }

    u32 release_unity_build_count = 0;
    u32 release_split_build_count = 0;
    for (u64 combination_i = 0; combination_i < combination_count; combination_i += 1)
    {
        MatrixTestCombination combination = combinations[combination_i];
        if (!combination.sanitize && combination.options.optimize)
        {
            if (combination.compiler == BUILD_COMPILER_CLANG)
            {
                release_unity_build_count += 1;
            }
            else
            {
                release_split_build_count += 1;
            }
        }
    }

    ReleaseBuildParallelism release_parallelism =
        release_build_parallelism(thread_count, release_unity_build_count, release_split_build_count);
    string_print(S8("BUSTER_RELEASE_PARALLELISM: threads={u32} unity_builds={u32} unity_jobs={u32} split_builds={u32} split_jobs={u32}\n"), thread_count,
                 release_unity_build_count, release_parallelism.unity_jobs, release_split_build_count, release_parallelism.split_jobs);

    BuildStep* release_build_step = step_add(arena);
    for (u64 combination_i = 0; combination_i < combination_count; combination_i += 1)
    {
        MatrixTestCombination combination = combinations[combination_i];
        if (!combination.sanitize && combination.options.optimize)
        {
            combination.options.parallel_jobs = combination.compiler == BUILD_COMPILER_CLANG ? release_parallelism.unity_jobs : release_parallelism.split_jobs;
            build_run_add(arena, release_build_step, combination.build_directory, (SliceString8){0}, (SliceString8){0}, combination.options);
            if (fanout && fanout_requested && build_artifact_fanout_is_canonical(combination.generate, combination.options))
            {
                fanout->release_build_scheduled = 1;
            }
        }
    }

    if (fanout && fanout_requested)
    {
        BuildStep* release_success_step = step_add(arena);
        ProcessRun* release_success_run = run_add(arena, release_success_step);
        *release_success_run = (ProcessRun){
            .callback = build_artifact_fanout_release_success_action,
            .callback_data = fanout,
        };
    }

    for (u64 combination_i = 0; combination_i < combination_count; combination_i += 1)
    {
        MatrixTestCombination combination = combinations[combination_i];
        String8 test_targets[] = {
            combination.run_app_tests ? S8("test_all") : S8("test_units"),
        };
        build_add(arena, combination.build_directory, (SliceString8)BUSTER_ARRAY_TO_SLICE(test_targets), (SliceString8){0}, combination.options);

        if (combination.compiler == BUILD_COMPILER_CLANG && !combination.sanitize && combination.options.optimize)
        {
            clang_analyze_command_add(arena, combination.build_directory, combination.options);
        }
    }
    if (fanout && fanout_requested)
    {
        ProcessResult fanout_result = self_host_from_existing_add(arena, fanout);
        if (fanout_result != PROCESS_RESULT_SUCCESS)
        {
            return fanout_result;
        }
    }
    return PROCESS_RESULT_SUCCESS;
}

// --- import_assembly_metadata -------------------------------------------
// Explicit developer workflow. Normal builds consume only the checked-in
// generated C metadata and never need an XED checkout or llvm-tblgen.

typedef struct AssemblyImportPathNode AssemblyImportPathNode;
struct AssemblyImportPathNode
{
    String8 full;
    String8 relative;
    AssemblyImportPathNode* next;
};

typedef struct AssemblyImportPathList AssemblyImportPathList;
struct AssemblyImportPathList
{
    AssemblyImportPathNode* first;
    AssemblyImportPathNode* last;
    u64 count;
};

typedef struct XedImportRecord XedImportRecord;
struct XedImportRecord
{
    String8 source;
    String8 iclass;
    String8 iform;
    String8 isa_set;
    String8 category;
    String8 extension;
    String8 attributes;
    String8 cpl;
    String8 exceptions;
    String8 flags;
    String8 disasm;
    String8 disasm_intel;
    String8 disasm_attsv;
    String8 real_opcode;
    String8 uname;
    String8 comment;
    String8 version;
    String8 pattern;
    String8 operands;
    String8 operand_annotation;
    bool operands_present;
    XedImportRecord* next;
};

typedef struct XedImportRecordList XedImportRecordList;
struct XedImportRecordList
{
    XedImportRecord* first;
    XedImportRecord* last;
    u64 count;
};

BUSTER_GLOBAL_LOCAL String8 assembly_import_trim(String8 string)
{
    u64 start = 0;
    u64 end = string.length;
    while (start < end && character_is_space(string.pointer[start]))
    {
        start += 1;
    }
    while (end > start && character_is_space(string.pointer[end - 1]))
    {
        end -= 1;
    }
    return string_slice(string, start, end);
}

BUSTER_GLOBAL_LOCAL void assembly_import_path_list_push(Arena* arena, AssemblyImportPathList* list, String8 full, String8 relative)
{
    AssemblyImportPathNode* node = arena_allocate(arena, AssemblyImportPathNode, 1);
    *node = (AssemblyImportPathNode){
        .full = string_duplicate_arena(arena, full, true),
        .relative = string_duplicate_arena(arena, relative, true),
    };
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

BUSTER_GLOBAL_LOCAL bool assembly_import_collect_xed_config_paths(Arena* arena, String8 root, AssemblyImportPathList* files)
{
    bool root_valid = false;
#if BUSTER_WINDOWS
    String16 root_w = string16_from_string8(arena, root, true);
    DWORD root_attributes = GetFileAttributesW(root_w.pointer);
    root_valid = root_attributes != INVALID_FILE_ATTRIBUTES && (root_attributes & FILE_ATTRIBUTE_DIRECTORY) &&
                 !(root_attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat root_status;
    root_valid = lstat((const char*)root.pointer, &root_status) == 0 && S_ISDIR(root_status.st_mode) && !S_ISLNK(root_status.st_mode);
#endif
    if (!root_valid)
    {
        return false;
    }

    AssemblyImportPathNode* pending = arena_allocate(arena, AssemblyImportPathNode, 1);
    *pending = (AssemblyImportPathNode){.full = string_duplicate_arena(arena, root, true), .relative = S8("")};
    bool result = true;

    while (pending && result)
    {
        AssemblyImportPathNode* directory_node = pending;
        pending = pending->next;
        String8 directory = directory_node->full;
#if BUSTER_WINDOWS
        TemporalArena temp = scratch_begin(&arena, 1);
        String8 pattern = path_join(temp.arena, directory, S8("*"));
        String16 pattern_w = string16_from_string8(temp.arena, pattern, true);
        WIN32_FIND_DATAW find_data;
        HANDLE find = FindFirstFileW(pattern_w.pointer, &find_data);
        if (find == INVALID_HANDLE_VALUE)
        {
            result = false;
        }
        else
        {
            do
            {
                String8 name = string8_from_string16(temp.arena,
                                                     (String16){.pointer = find_data.cFileName, .length = string16_length(find_data.cFileName)}, true);
                if (string_equal(name, S8(".")) || string_equal(name, S8("..")))
                {
                    continue;
                }
                String8 full = path_join(arena, directory, name);
                String8 relative = directory_node->relative.length ? path_join(arena, directory_node->relative, name) : string_duplicate_arena(arena, name, true);
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                {
                    result = false;
                    break;
                }
                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    AssemblyImportPathNode* node = arena_allocate(arena, AssemblyImportPathNode, 1);
                    *node = (AssemblyImportPathNode){.full = full, .relative = relative, .next = pending};
                    pending = node;
                }
                else if (string_starts_with_sequence(name, S8("files")) && string_ends_with_sequence_insensitive(name, S8(".cfg")))
                {
                    assembly_import_path_list_push(arena, files, full, relative);
                }
            } while (FindNextFileW(find, &find_data));
            FindClose(find);
        }
        scratch_end(temp);
#else
        DIR* directory_handle = opendir((const char*)directory.pointer);
        if (!directory_handle)
        {
            result = false;
        }
        else
        {
            struct dirent* entry = 0;
            while ((entry = readdir(directory_handle)) != 0)
            {
                String8 name = string_from_pointer((char8*)entry->d_name);
                if (string_equal(name, S8(".")) || string_equal(name, S8("..")))
                {
                    continue;
                }
                String8 full = path_join(arena, directory, name);
                String8 relative = directory_node->relative.length ? path_join(arena, directory_node->relative, name) : string_duplicate_arena(arena, name, true);
                struct stat status;
                if (lstat((const char*)full.pointer, &status) != 0)
                {
                    result = false;
                    break;
                }
                if (S_ISLNK(status.st_mode))
                {
                    result = false;
                    break;
                }
                if (S_ISDIR(status.st_mode))
                {
                    AssemblyImportPathNode* node = arena_allocate(arena, AssemblyImportPathNode, 1);
                    *node = (AssemblyImportPathNode){.full = full, .relative = relative, .next = pending};
                    pending = node;
                }
                else if (S_ISREG(status.st_mode) && string_starts_with_sequence(name, S8("files")) &&
                         string_ends_with_sequence_insensitive(name, S8(".cfg")))
                {
                    assembly_import_path_list_push(arena, files, full, relative);
                }
            }
            closedir(directory_handle);
        }
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int assembly_import_path_compare(const void* left_pointer, const void* right_pointer)
{
    AssemblyImportPathNode const* left = *(AssemblyImportPathNode* const*)left_pointer;
    AssemblyImportPathNode const* right = *(AssemblyImportPathNode* const*)right_pointer;
    u64 count = BUSTER_MIN(left->relative.length, right->relative.length);
    int result = count ? memcmp(left->relative.pointer, right->relative.pointer, count) : 0;
    if (!result)
    {
        result = (left->relative.length > right->relative.length) - (left->relative.length < right->relative.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_path_list_contains(AssemblyImportPathList list, String8 relative)
{
    for (AssemblyImportPathNode* node = list.first; node; node = node->next)
    {
        if (string_equal(node->relative, relative))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_relative_path_is_safe(String8 path)
{
    if (!path.length || path_is_absolute(path) || path.pointer[0] == '/' || path.pointer[0] == '\\' ||
        (path.length >= 2 && path.pointer[1] == ':'))
    {
        return false;
    }
    u64 start = 0;
    for (u64 index = 0; index <= path.length; index += 1)
    {
        if (index == path.length || path_is_separator(path.pointer[index]) || path.pointer[index] == '/' || path.pointer[index] == '\\')
        {
            String8 component = string_slice(path, start, index);
            if (string_equal(component, S8("..")) || string_equal(component, S8(".")))
            {
                return false;
            }
            start = index + 1;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_regular_file(Arena* arena, String8 path)
{
#if BUSTER_WINDOWS
    String16 path_w = string16_from_string8(arena, path, true);
    DWORD attributes = GetFileAttributesW(path_w.pointer);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat status;
    return lstat((const char*)path.pointer, &status) == 0 && S_ISREG(status.st_mode) && !S_ISLNK(status.st_mode);
#endif
}

BUSTER_GLOBAL_LOCAL bool assembly_import_config_key_is_known(String8 key)
{
    static String8 const keys[] = {
        S8_INITIALIZER("add"),
        S8_INITIALIZER("add-source"),
        S8_INITIALIZER("add-tests"),
        S8_INITIALIZER("chip-models"),
        S8_INITIALIZER("clear"),
        S8_INITIALIZER("conversion-table"),
        S8_INITIALIZER("cpuid"),
        S8_INITIALIZER("dec-instructions"),
        S8_INITIALIZER("dec-patterns"),
        S8_INITIALIZER("dec-spine"),
        S8_INITIALIZER("define"),
        S8_INITIALIZER("element-type-base"),
        S8_INITIALIZER("element-types"),
        S8_INITIALIZER("enc-dec-patterns"),
        S8_INITIALIZER("enc-instructions"),
        S8_INITIALIZER("enc-patterns"),
        S8_INITIALIZER("enc2-instructions"),
        S8_INITIALIZER("errors"),
        S8_INITIALIZER("extra-widths"),
        S8_INITIALIZER("fields"),
        S8_INITIALIZER("map-descriptions"),
        S8_INITIALIZER("no-enc2-instructions"),
        S8_INITIALIZER("pointer-names"),
        S8_INITIALIZER("registers"),
        S8_INITIALIZER("remove"),
        S8_INITIALIZER("remove-source"),
        S8_INITIALIZER("replace-source"),
        S8_INITIALIZER("state"),
        S8_INITIALIZER("widths"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(keys); index += 1)
    {
        if (string_equal(key, keys[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_config_parse(Arena* arena, String8 root, AssemblyImportPathNode* config,
                                                       String8 text, AssemblyImportPathList* source_files, bool diagnostics)
{
    while (text.length)
    {
        u64 newline = string_first_code_unit(text, '\n');
        u64 line_end = newline == BUSTER_STRING_NO_MATCH ? text.length : newline;
        String8 line = assembly_import_trim(string_slice(text, 0, line_end));
        text = newline == BUSTER_STRING_NO_MATCH ? (String8){0} : string_slice(text, line_end + 1, text.length);
        if (!line.length || line.pointer[0] == '#')
        {
            continue;
        }
        u64 comment = string_first_code_unit(line, '#');
        if (comment != BUSTER_STRING_NO_MATCH)
        {
            line = assembly_import_trim(string_slice(line, 0, comment));
        }
        if (!line.length)
        {
            continue;
        }
        u64 colon = string_first_code_unit(line, ':');
        if (colon == BUSTER_STRING_NO_MATCH)
        {
            if (diagnostics)
            {
                string_print(S8("error: malformed XED config line in {S8}: {S8}\n"), config->relative, line);
            }
            return false;
        }
        String8 key = assembly_import_trim(string_slice(line, 0, colon));
        String8 value = assembly_import_trim(string_slice(line, colon + 1, line.length));
        if (!assembly_import_config_key_is_known(key) || !value.length)
        {
            if (diagnostics)
            {
                string_print(S8("error: unknown or empty XED config entry in {S8}: {S8}\n"), config->relative, line);
            }
            return false;
        }
        if (string_equal(key, S8("enc-instructions")))
        {
            if (string_first_code_unit(value, ':') != BUSTER_STRING_NO_MATCH || !assembly_import_relative_path_is_safe(value))
            {
                if (diagnostics)
                {
                    string_print(S8("error: unsafe XED enc-instructions path in {S8}: {S8}\n"), config->relative, value);
                }
                return false;
            }
            String8 parent = path_parent(arena, config->relative);
            String8 relative = parent.length ? path_join(arena, parent, value) : string_duplicate_arena(arena, value, true);
            String8 full = path_join(arena, root, relative);
            if (!assembly_import_regular_file(arena, full))
            {
                if (diagnostics)
                {
                    string_print(S8("error: XED enc-instructions file is missing, non-regular, or linked: {S8}\n"), full);
                }
                return false;
            }
            if (assembly_import_path_list_contains(*source_files, relative))
            {
                if (diagnostics)
                {
                    string_print(S8("error: duplicate XED enc-instructions source in {S8}: {S8}\n"), config->relative, relative);
                }
                return false;
            }
            else
            {
                assembly_import_path_list_push(arena, source_files, full, relative);
            }
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void xed_import_record_push(Arena* arena, XedImportRecordList* list, XedImportRecord record)
{
    XedImportRecord* node = arena_allocate(arena, XedImportRecord, 1);
    *node = record;
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

BUSTER_GLOBAL_LOCAL bool xed_import_record_assign(String8* field, String8 value)
{
    if (field->length && !string_equal(*field, value))
    {
        return false;
    }
    *field = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool xed_import_record_field(XedImportRecord* record, String8 key, String8 value)
{
    if (string_equal(key, S8("ICLASS")))
    {
        return xed_import_record_assign(&record->iclass, value);
    }
    else if (string_equal(key, S8("IFORM")))
    {
        return xed_import_record_assign(&record->iform, value);
    }
    else if (string_equal(key, S8("ISA_SET")))
    {
        return xed_import_record_assign(&record->isa_set, value);
    }
    else if (string_equal(key, S8("CATEGORY")))
    {
        return xed_import_record_assign(&record->category, value);
    }
    else if (string_equal(key, S8("EXTENSION")))
    {
        return xed_import_record_assign(&record->extension, value);
    }
    else if (string_equal(key, S8("ATTRIBUTES")))
    {
        return xed_import_record_assign(&record->attributes, value);
    }
    else if (string_equal(key, S8("CPL")))
    {
        return xed_import_record_assign(&record->cpl, value);
    }
    else if (string_equal(key, S8("EXCEPTIONS")))
    {
        return xed_import_record_assign(&record->exceptions, value);
    }
    else if (string_equal(key, S8("FLAGS")))
    {
        return xed_import_record_assign(&record->flags, value);
    }
    else if (string_equal(key, S8("DISASM")))
    {
        return xed_import_record_assign(&record->disasm, value);
    }
    else if (string_equal(key, S8("DISASM_INTEL")))
    {
        return xed_import_record_assign(&record->disasm_intel, value);
    }
    else if (string_equal(key, S8("DISASM_ATTSV")))
    {
        return xed_import_record_assign(&record->disasm_attsv, value);
    }
    else if (string_equal(key, S8("REAL_OPCODE")))
    {
        return xed_import_record_assign(&record->real_opcode, value);
    }
    else if (string_equal(key, S8("UNAME")))
    {
        return xed_import_record_assign(&record->uname, value);
    }
    else if (string_equal(key, S8("COMMENT")))
    {
        return xed_import_record_assign(&record->comment, value);
    }
    else if (string_equal(key, S8("VERSION")))
    {
        return xed_import_record_assign(&record->version, value);
    }
    else if (string_equal(key, S8("PATTERN")))
    {
        return xed_import_record_assign(&record->pattern, value);
    }
    else if (string_equal(key, S8("OPERANDS")))
    {
        if (record->operands_present && !string_equal(record->operands, value))
        {
            return false;
        }
        record->operands = value;
        record->operands_present = true;
    }
    else
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool xed_import_read_logical_line(Arena* arena, String8* text, String8* result)
{
    if (!text->length)
    {
        return false;
    }
    u64 newline = string_first_code_unit(*text, '\n');
    u64 line_end = newline == BUSTER_STRING_NO_MATCH ? text->length : newline;
    String8 line = assembly_import_trim(string_slice(*text, 0, line_end));
    *text = newline == BUSTER_STRING_NO_MATCH ? (String8){0} : string_slice(*text, line_end + 1, text->length);
    if (!line.length || line.pointer[line.length - 1] != '\\')
    {
        *result = line;
        return true;
    }

    u64 output_start = arena_buffer_size(arena);
    for (;;)
    {
        line = assembly_import_trim(string_slice(line, 0, line.length - 1));
        arena_append_string8(arena, line);
        if (!text->length)
        {
            return false;
        }
        arena_append_char8(arena, ' ');
        newline = string_first_code_unit(*text, '\n');
        line_end = newline == BUSTER_STRING_NO_MATCH ? text->length : newline;
        line = assembly_import_trim(string_slice(*text, 0, line_end));
        *text = newline == BUSTER_STRING_NO_MATCH ? (String8){0} : string_slice(*text, line_end + 1, text->length);
        if (!line.length || line.pointer[line.length - 1] != '\\')
        {
            arena_append_string8(arena, line);
            break;
        }
    }
    *result = (String8){.pointer = (char8*)arena_buffer_start(arena) + output_start,
                        .length = arena_buffer_size(arena) - output_start};
    return true;
}

BUSTER_GLOBAL_LOCAL bool xed_import_emit_pending(Arena* arena, XedImportRecordList* records, XedImportRecord record)
{
    if (!record.iclass.length || !record.pattern.length || !record.operands_present)
    {
        return false;
    }
    xed_import_record_push(arena, records, record);
    return true;
}

BUSTER_GLOBAL_LOCAL bool xed_import_source_directive_known(String8 line)
{
    static String8 const directives[] = {
        S8_INITIALIZER("INSTRUCTIONS()::"), S8_INITIALIZER("AVX_INSTRUCTIONS()::"),
        S8_INITIALIZER("EVEX_INSTRUCTIONS()::"), S8_INITIALIZER("XOP_INSTRUCTIONS()::"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(directives); index += 1)
    {
        if (string_equal(line, directives[index]))
        {
            return true;
        }
    }
    u64 colon = string_first_code_unit(line, ':');
    if (colon == BUSTER_STRING_NO_MATCH || !string_equal(assembly_import_trim(string_slice(line, 0, colon)), S8("UDELETE")))
    {
        return false;
    }
    String8 value = assembly_import_trim(string_slice(line, colon + 1, line.length));
    if (!value.length)
    {
        return false;
    }
    for (u64 index = 0; index < value.length; index += 1)
    {
        char8 c = value.pointer[index];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool xed_import_parse_file(Arena* arena, XedImportRecordList* records, String8 source, String8 text)
{
    XedImportRecord record = {.source = source};
    bool in_record = false;
    while (text.length)
    {
        String8 line = {0};
        if (!xed_import_read_logical_line(arena, &text, &line))
        {
            return false;
        }
        if (!line.length || line.pointer[0] == '#')
        {
            continue;
        }
        if (string_equal(line, S8("{")))
        {
            if (in_record)
            {
                return false;
            }
            record = (XedImportRecord){.source = source};
            in_record = true;
            continue;
        }
        if (string_equal(line, S8("}")))
        {
            if (!in_record || !record.iclass.length || !record.pattern.length || !record.operands_present)
            {
                return false;
            }
            if (!xed_import_emit_pending(arena, records, record))
            {
                return false;
            }
            record.pattern = (String8){0};
            record.operands = (String8){0};
            record.operand_annotation = (String8){0};
            record.operands_present = false;
            record.iform = (String8){0};
            in_record = false;
            continue;
        }
        if (!in_record)
        {
            if (xed_import_source_directive_known(line))
            {
                continue;
            }
            return false;
        }
        u64 colon = string_first_code_unit(line, ':');
        if (colon == BUSTER_STRING_NO_MATCH)
        {
            return false;
        }
        String8 key = assembly_import_trim(string_slice(line, 0, colon));
        String8 value = assembly_import_trim(string_slice(line, colon + 1, line.length));
        if (string_equal(key, S8("PATTERN")) || string_equal(key, S8("OPERANDS")))
        {
            u64 comment = string_first_code_unit(value, '#');
            if (comment != BUSTER_STRING_NO_MATCH)
            {
                String8 annotation = assembly_import_trim(string_slice(value, comment + 1, value.length));
                if (string_equal(key, S8("OPERANDS")))
                {
                    record.operand_annotation = annotation;
                }
                value = assembly_import_trim(string_slice(value, 0, comment));
            }
        }
        if (string_equal(key, S8("PATTERN")) && record.pattern.length)
        {
            if (!record.operands_present || !xed_import_emit_pending(arena, records, record))
            {
                return false;
            }
            record.pattern = (String8){0};
            record.operands = (String8){0};
            record.operand_annotation = (String8){0};
            record.operands_present = false;
            record.iform = (String8){0};
        }
        else if (string_equal(key, S8("IFORM")) && record.pattern.length && record.operands_present && record.iform.length)
        {
            if (!xed_import_emit_pending(arena, records, record))
            {
                return false;
            }
            record.pattern = (String8){0};
            record.operands = (String8){0};
            record.operand_annotation = (String8){0};
            record.operands_present = false;
            record.iform = (String8){0};
        }
        if (!xed_import_record_field(&record, key, value))
        {
            return false;
        }
    }
    return !in_record;
}

BUSTER_GLOBAL_LOCAL void xed_import_emit_field(Arena* output, String8 key, String8 value, bool* first)
{
    if (!value.length)
    {
        return;
    }
    if (!*first)
    {
        arena_append_char8(output, ',');
    }
    *first = false;
    arena_append_json_string(output, key);
    arena_append_char8(output, ':');
    arena_append_json_string(output, value);
}

BUSTER_GLOBAL_LOCAL void xed_import_emit_field_present(Arena* output, String8 key, String8 value, bool* first)
{
    if (!*first)
    {
        arena_append_char8(output, ',');
    }
    *first = false;
    arena_append_json_string(output, key);
    arena_append_char8(output, ':');
    arena_append_json_string(output, value);
}

BUSTER_GLOBAL_LOCAL void xed_import_emit(Arena* output, XedImportRecordList records)
{
    for (XedImportRecord* record = records.first; record; record = record->next)
    {
        bool first = true;
        arena_append_char8(output, '{');
        xed_import_emit_field(output, S8("source"), record->source, &first);
        xed_import_emit_field(output, S8("iclass"), record->iclass, &first);
        xed_import_emit_field(output, S8("iform"), record->iform, &first);
        xed_import_emit_field(output, S8("isa_set"), record->isa_set, &first);
        xed_import_emit_field(output, S8("category"), record->category, &first);
        xed_import_emit_field(output, S8("extension"), record->extension, &first);
        xed_import_emit_field(output, S8("attributes"), record->attributes, &first);
        xed_import_emit_field(output, S8("cpl"), record->cpl, &first);
        xed_import_emit_field(output, S8("exceptions"), record->exceptions, &first);
        xed_import_emit_field(output, S8("flags"), record->flags, &first);
        xed_import_emit_field(output, S8("disasm"), record->disasm, &first);
        xed_import_emit_field(output, S8("disasm_intel"), record->disasm_intel, &first);
        xed_import_emit_field(output, S8("disasm_attsv"), record->disasm_attsv, &first);
        xed_import_emit_field(output, S8("real_opcode"), record->real_opcode, &first);
        xed_import_emit_field(output, S8("uname"), record->uname, &first);
        xed_import_emit_field(output, S8("comment"), record->comment, &first);
        xed_import_emit_field(output, S8("version"), record->version, &first);
        xed_import_emit_field(output, S8("pattern"), record->pattern, &first);
        if (record->operands_present)
        {
            xed_import_emit_field_present(output, S8("operands"), record->operands, &first);
            xed_import_emit_field(output, S8("operand_annotation"), record->operand_annotation, &first);
        }
        arena_append_string8(output, S8("}\n"));
    }
}

typedef enum XedGeneratedCoverageClass
{
    XED_GENERATED_COVERAGE_DIRECT,
    XED_GENERATED_COVERAGE_NORMALIZED,
    XED_GENERATED_COVERAGE_NOT64,
    XED_GENERATED_COVERAGE_PRIVILEGED,
    XED_GENERATED_COVERAGE_RESERVED,
    XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN,
    XED_GENERATED_COVERAGE_UNCLASSIFIED,
    XED_GENERATED_COVERAGE_COUNT,
} XedGeneratedCoverageClass;

typedef enum XedGeneratedPrefixKind
{
    XED_GENERATED_PREFIX_LEGACY,
    XED_GENERATED_PREFIX_REX,
    XED_GENERATED_PREFIX_REX2,
    XED_GENERATED_PREFIX_VEX,
    XED_GENERATED_PREFIX_XOP,
    XED_GENERATED_PREFIX_EVEX,
    XED_GENERATED_PREFIX_COUNT,
} XedGeneratedPrefixKind;

typedef enum XedGeneratedMap
{
    XED_GENERATED_MAP_LEGACY,
    XED_GENERATED_MAP_0F,
    XED_GENERATED_MAP_0F38,
    XED_GENERATED_MAP_0F3A,
    XED_GENERATED_MAP_4,
    XED_GENERATED_MAP_5,
    XED_GENERATED_MAP_6,
    XED_GENERATED_MAP_7,
    XED_GENERATED_MAP_X8,
    XED_GENERATED_MAP_X9,
    XED_GENERATED_MAP_XA,
    XED_GENERATED_MAP_COUNT,
} XedGeneratedMap;

enum
{
    XED_GENERATED_FIELD_MODRM = 1u << 0,
    XED_GENERATED_FIELD_SIB = 1u << 1,
    XED_GENERATED_FIELD_VSIB = 1u << 2,
    XED_GENERATED_FIELD_MEMORY = 1u << 3,
    XED_GENERATED_FIELD_REGISTER = 1u << 4,
    XED_GENERATED_FIELD_DISPLACEMENT = 1u << 5,
    XED_GENERATED_FIELD_IMMEDIATE = 1u << 6,
    XED_GENERATED_FIELD_RELATIVE = 1u << 7,
    XED_GENERATED_FIELD_FIELD_END = 1u << 8,
};

enum
{
    XED_GENERATED_DECORATOR_MASK = 1u << 0,
    XED_GENERATED_DECORATOR_ZEROING = 1u << 1,
    XED_GENERATED_DECORATOR_BROADCAST = 1u << 2,
    XED_GENERATED_DECORATOR_ROUNDING = 1u << 3,
    XED_GENERATED_DECORATOR_SAE = 1u << 4,
};

enum
{
    XED_GENERATED_APX = 1u << 0,
    XED_GENERATED_APX_ND = 1u << 1,
    XED_GENERATED_APX_NF = 1u << 2,
    XED_GENERATED_APX_NDD = 1u << 3,
    XED_GENERATED_APX_SCC = 1u << 4,
    XED_GENERATED_APX_EGPR = 1u << 5,
};

enum
{
    XED_GENERATED_MODE_16 = 1u << 0,
    XED_GENERATED_MODE_32 = 1u << 1,
    XED_GENERATED_MODE_64 = 1u << 2,
    XED_GENERATED_MODE_NOT64 = 1u << 3,
    XED_GENERATED_MODE_EA16 = 1u << 4,
    XED_GENERATED_MODE_EA32 = 1u << 5,
    XED_GENERATED_MODE_EA64 = 1u << 6,
    XED_GENERATED_MODE_EANOT16 = 1u << 7,
};

enum
{
    XED_GENERATED_OPERAND_NONE,
    XED_GENERATED_OPERAND_REGISTER,
    XED_GENERATED_OPERAND_MEMORY,
    XED_GENERATED_OPERAND_IMMEDIATE,
    XED_GENERATED_OPERAND_RELATIVE,
    XED_GENERATED_OPERAND_ABSOLUTE,
    XED_GENERATED_OPERAND_BASE,
    XED_GENERATED_OPERAND_SEGMENT,
    XED_GENERATED_OPERAND_ADDRESS_GENERATOR,
    XED_GENERATED_OPERAND_PSEUDO,
};

enum
{
    XED_GENERATED_ACCESS_READ = 1u << 0,
    XED_GENERATED_ACCESS_WRITE = 1u << 1,
    XED_GENERATED_ACCESS_COND = 1u << 2,
    XED_GENERATED_ACCESS_SUPPRESSED = 1u << 3,
    XED_GENERATED_ACCESS_IMPLICIT = 1u << 4,
};

enum
{
    XED_GENERATED_FIELD_SOURCE_NONE,
    XED_GENERATED_FIELD_SOURCE_REG,
    XED_GENERATED_FIELD_SOURCE_RM,
    XED_GENERATED_FIELD_SOURCE_VVVV,
    XED_GENERATED_FIELD_SOURCE_MASK,
    XED_GENERATED_FIELD_SOURCE_FIXED,
    XED_GENERATED_FIELD_SOURCE_IMMEDIATE,
    XED_GENERATED_FIELD_SOURCE_RELATIVE,
};

enum
{
    XED_GENERATED_REASON_NONE,
    XED_GENERATED_REASON_MODE_NOT64,
    XED_GENERATED_REASON_CPL0,
    XED_GENERATED_REASON_UNKNOWN_PATTERN_TOKEN,
    XED_GENERATED_REASON_UNKNOWN_OPERAND_TOKEN,
    XED_GENERATED_REASON_COUNT,
};

enum
{
    XED_GENERATED_TUPLE_NONE,
    XED_GENERATED_TUPLE_FULL,
    XED_GENERATED_TUPLE_HALF,
    XED_GENERATED_TUPLE_QUARTER,
    XED_GENERATED_TUPLE_EIGHTH,
    XED_GENERATED_TUPLE_SCALAR,
    XED_GENERATED_TUPLE_TUPLE1,
    XED_GENERATED_TUPLE_TUPLE1_4X,
    XED_GENERATED_TUPLE_TUPLE1_BYTE,
    XED_GENERATED_TUPLE_TUPLE1_WORD,
    XED_GENERATED_TUPLE_TUPLE2,
    XED_GENERATED_TUPLE_TUPLE4,
    XED_GENERATED_TUPLE_TUPLE8,
};

enum
{
    XED_GENERATED_AMX_TILE_REGISTER = 1u << 0,
    XED_GENERATED_AMX_TILE_MEMORY = 1u << 1,
    XED_GENERATED_AMX_TILE_ROW = 1u << 2,
    XED_GENERATED_AMX_TILE_COLUMN = 1u << 3,
};

enum
{
    XED_GENERATED_TEST_SCHEMA,
    XED_GENERATED_TEST_PRIVILEGED_SCHEMA,
    XED_GENERATED_TEST_NOT64_SCHEMA,
};

#define XED_GENERATED_MAX_OPERANDS 16
#define XED_GENERATED_MAX_FIXED_BYTES 16

typedef struct XedGeneratedOperand XedGeneratedOperand;
struct XedGeneratedOperand
{
    String8 atom;
    String8 width;
    u8 slot;
    u8 visible;
    u8 kind;
    u8 access;
    u8 field_source;
    u8 reserved[3];
};

typedef struct XedGeneratedForm XedGeneratedForm;
struct XedGeneratedForm
{
    XedImportRecord* record;
    u64 stable_hash;
    u32 operand_count;
    XedGeneratedOperand operands[XED_GENERATED_MAX_OPERANDS];
    u8 coverage_class;
    u8 encoder_family;
    u8 test_class;
    u8 prefix_kind;
    u8 map;
    u8 fixed_byte_count;
    u8 fixed_bytes[XED_GENERATED_MAX_FIXED_BYTES];
    u8 mandatory_prefix;
    u8 reserved0;
    u16 field_flags;
    u16 decorator_flags;
    u16 apx_flags;
    u16 amx_flags;
    u16 mode_flags;
    u8 displacement_width;
    u8 displacement_scale;
    u8 immediate_width;
    u8 immediate_signed;
    u8 relocation_base;
    u8 reserved1[3];
    u8 tuple_kind;
    String8 tuple_rule;
    String8 element_size;
    String8 unsupported_token;
    u32 token_count;
    u16 reason_id;
    u16 reserved2;
};

typedef struct XedGeneratedFormList XedGeneratedFormList;
struct XedGeneratedFormList
{
    XedGeneratedForm* pointer;
    u64 length;
};

BUSTER_GLOBAL_LOCAL bool xed_import_string_in_array(String8 value, String8 const* values, u32 count)
{
    for (u32 index = 0; index < count; index += 1)
    {
        if (string_equal(value, values[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool xed_import_parse_integer(String8 string, u64* result)
{
    if (!string.length)
    {
        return false;
    }
    u64 base = 10;
    u64 index = 0;
    if (string.length >= 2 && string.pointer[0] == '0' && (string.pointer[1] == 'x' || string.pointer[1] == 'X'))
    {
        base = 16;
        index = 2;
    }
    else if (string.length >= 2 && string.pointer[0] == '0' && (string.pointer[1] == 'b' || string.pointer[1] == 'B'))
    {
        base = 2;
        index = 2;
    }
    if (index == string.length)
    {
        return false;
    }
    u64 value = 0;
    bool digit_seen = false;
    for (; index < string.length; index += 1)
    {
        char8 c = string.pointer[index];
        if (c == '_')
        {
            continue;
        }
        u64 digit = UINT64_MAX;
        if (c >= '0' && c <= '9')
        {
            digit = (u64)(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = (u64)(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = (u64)(c - 'A' + 10);
        }
        if (digit >= base)
        {
            return false;
        }
        if (value > (UINT64_MAX - digit) / base)
        {
            return false;
        }
        value = value * base + digit;
        digit_seen = true;
    }
    *result = value;
    return digit_seen;
}

BUSTER_GLOBAL_LOCAL bool xed_import_token_name_is_identifier(String8 token, String8 wanted)
{
    return string_equal(token, wanted);
}

BUSTER_GLOBAL_LOCAL bool xed_import_pattern_bracket_token_known(String8 token)
{
    u64 open = string_first_code_unit(token, '[');
    if (open == BUSTER_STRING_NO_MATCH || token.length < open + 3 || token.pointer[token.length - 1] != ']')
    {
        return false;
    }
    String8 base = string_slice(token, 0, open);
    String8 value = string_slice(token, open + 1, token.length - 1);
    static String8 const bases[] = {S8_INITIALIZER("MOD"), S8_INITIALIZER("REG"), S8_INITIALIZER("RM"), S8_INITIALIZER("SRM")};
    if (!xed_import_string_in_array(base, bases, BUSTER_ARRAY_LENGTH(bases)))
    {
        return false;
    }
    if (string_equal(value, S8("mm")) || string_equal(value, S8("rrr")) || string_equal(value, S8("nnn")) ||
        string_equal(value, S8("1-7")))
    {
        return true;
    }
    u64 numeric = 0;
    return xed_import_parse_integer(value, &numeric) && numeric < 8;
}

BUSTER_GLOBAL_LOCAL bool xed_import_pattern_function_known(String8 token)
{
    if (token.length < 3 || token.pointer[token.length - 1] != ')')
    {
        return false;
    }
    u64 open = string_first_code_unit(token, '(');
    if (open == BUSTER_STRING_NO_MATCH || open + 1 != token.length - 1)
    {
        return false;
    }
    String8 name = string_slice(token, 0, open);
    static String8 const exact[] = {
        S8_INITIALIZER("AVX512_ROUND"), S8_INITIALIZER("BRANCH_HINT"), S8_INITIALIZER("BRDISP32"),
        S8_INITIALIZER("BRDISP64"), S8_INITIALIZER("BRDISP8"), S8_INITIALIZER("BRDISPz"),
        S8_INITIALIZER("CET_NO_TRACK"), S8_INITIALIZER("CR_WIDTH"), S8_INITIALIZER("DF64"),
        S8_INITIALIZER("EVAPX"), S8_INITIALIZER("EVAPX_SCC"), S8_INITIALIZER("EVEXR4_ONE"),
        S8_INITIALIZER("FIX_ROUND_LEN128"), S8_INITIALIZER("FIX_ROUND_LEN512"), S8_INITIALIZER("FORCE64"),
        S8_INITIALIZER("IGNORE66"), S8_INITIALIZER("IMMUNE66"), S8_INITIALIZER("IMMUNE66_LOOP64"),
        S8_INITIALIZER("IMMUNE_REXW"), S8_INITIALIZER("MEMDISPv"), S8_INITIALIZER("MODRM"),
        S8_INITIALIZER("NELEM_EIGHTHMEM"), S8_INITIALIZER("NELEM_FULL"), S8_INITIALIZER("NELEM_FULLMEM"),
        S8_INITIALIZER("NELEM_HALF"), S8_INITIALIZER("NELEM_HALFMEM"), S8_INITIALIZER("NELEM_MEM128"),
        S8_INITIALIZER("NELEM_MOVDDUP"), S8_INITIALIZER("NELEM_ONE"), S8_INITIALIZER("NELEM_QUARTER"),
        S8_INITIALIZER("NELEM_QUARTERMEM"), S8_INITIALIZER("NELEM_TUPLE1_4X"), S8_INITIALIZER("NELEM_TUPLE2"),
        S8_INITIALIZER("NELEM_TUPLE4"), S8_INITIALIZER("NELEM_TUPLE8"), S8_INITIALIZER("ONE"),
        S8_INITIALIZER("OVERRIDE_SEG0"), S8_INITIALIZER("OVERRIDE_SEG1"), S8_INITIALIZER("REMOVE_SEGMENT"),
        S8_INITIALIZER("REFINING66"), S8_INITIALIZER("SAE"), S8_INITIALIZER("SE_IMM8"), S8_INITIALIZER("SIMM8"), S8_INITIALIZER("SIMMz"),
        S8_INITIALIZER("UIMM16"), S8_INITIALIZER("UIMM32"), S8_INITIALIZER("UIMM8"), S8_INITIALIZER("UIMM8_1"),
        S8_INITIALIZER("UIMMv"), S8_INITIALIZER("ZEROING"),
    };
    if (xed_import_string_in_array(name, exact, BUSTER_ARRAY_LENGTH(exact)))
    {
        return true;
    }
    if (string_starts_with_sequence(name, S8("ESIZE_")) || string_starts_with_sequence(name, S8("UISA_VMODRM_")) ||
        string_starts_with_sequence(name, S8("VMODRM_")))
    {
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool xed_import_pattern_assignment_known(String8 token)
{
    u64 equals = string_first_code_unit(token, '=');
    if (equals == BUSTER_STRING_NO_MATCH || equals == 0 || equals + 1 == token.length)
    {
        return false;
    }
    bool not_equal = equals && token.pointer[equals - 1] == '!';
    String8 key = string_slice(token, 0, not_equal ? equals - 1 : equals);
    static String8 const keys[] = {
        S8_INITIALIZER("BCRC"), S8_INITIALIZER("CET"), S8_INITIALIZER("CLDEMOTE"), S8_INITIALIZER("IBHF"), S8_INITIALIZER("LZCNT"),
        S8_INITIALIZER("MASK"), S8_INITIALIZER("MOD"), S8_INITIALIZER("MODEP5"), S8_INITIALIZER("MODE_SHORT_UD0"), S8_INITIALIZER("MPXMODE"),
        S8_INITIALIZER("ND"), S8_INITIALIZER("NF"), S8_INITIALIZER("P4"), S8_INITIALIZER("PREFETCHIT"), S8_INITIALIZER("PREFETCHRST"),
        S8_INITIALIZER("REP"), S8_INITIALIZER("RM"), S8_INITIALIZER("SRM"), S8_INITIALIZER("TZCNT"),
        S8_INITIALIZER("UBIT"), S8_INITIALIZER("WBNOINVD"), S8_INITIALIZER("ZEROING"),
    };
    if (!xed_import_string_in_array(key, keys, BUSTER_ARRAY_LENGTH(keys)))
    {
        return false;
    }
    u64 value = 0;
    return xed_import_parse_integer(string_slice(token, equals + 1, token.length), &value);
}

BUSTER_GLOBAL_LOCAL bool xed_import_pattern_word_known(String8 token)
{
    static String8 const words[] = {
        S8_INITIALIZER("66_prefix"), S8_INITIALIZER("ENCDELETE"), S8_INITIALIZER("EVV"), S8_INITIALIZER("VNP"),
        S8_INITIALIZER("VV1"), S8_INITIALIZER("XOPV"), S8_INITIALIZER("V0F"), S8_INITIALIZER("V0F38"),
        S8_INITIALIZER("V0F3A"), S8_INITIALIZER("MAP4"), S8_INITIALIZER("MAP5"), S8_INITIALIZER("MAP6"),
        S8_INITIALIZER("MAP7"), S8_INITIALIZER("XMAP8"), S8_INITIALIZER("XMAP9"), S8_INITIALIZER("XMAPA"),
        S8_INITIALIZER("V66"), S8_INITIALIZER("VF2"), S8_INITIALIZER("VF3"), S8_INITIALIZER("NOEVSR"),
        S8_INITIALIZER("NOVSR"), S8_INITIALIZER("NO_SCC_NF0"), S8_INITIALIZER("NO_SCC_NF1"),
        S8_INITIALIZER("SCC0"), S8_INITIALIZER("SCC1"), S8_INITIALIZER("SCC2"), S8_INITIALIZER("SCC3"),
        S8_INITIALIZER("SCC4"), S8_INITIALIZER("SCC5"), S8_INITIALIZER("SCC6"), S8_INITIALIZER("SCC7"),
        S8_INITIALIZER("SCC8"), S8_INITIALIZER("SCC9"), S8_INITIALIZER("SCC10"), S8_INITIALIZER("SCC11"),
        S8_INITIALIZER("SCC12"), S8_INITIALIZER("SCC13"), S8_INITIALIZER("SCC14"), S8_INITIALIZER("SCC15"),
        S8_INITIALIZER("SIB"), S8_INITIALIZER("SE_IMM8"), S8_INITIALIZER("VL128"), S8_INITIALIZER("VL256"),
        S8_INITIALIZER("VL512"), S8_INITIALIZER("W0"), S8_INITIALIZER("W1"), S8_INITIALIZER("eamode16"),
        S8_INITIALIZER("eamode32"), S8_INITIALIZER("eamode64"), S8_INITIALIZER("eanot16"), S8_INITIALIZER("mode16"),
        S8_INITIALIZER("mode32"), S8_INITIALIZER("mode64"), S8_INITIALIZER("lock_prefix"),
        S8_INITIALIZER("no66_prefix"), S8_INITIALIZER("no67_prefix"), S8_INITIALIZER("not16"),
        S8_INITIALIZER("no_refining_prefix"), S8_INITIALIZER("nolock_prefix"), S8_INITIALIZER("norep"),
        S8_INITIALIZER("norex2_prefix"), S8_INITIALIZER("norexb_prefix"), S8_INITIALIZER("norexb4_prefix"), S8_INITIALIZER("norexr_prefix"),
        S8_INITIALIZER("norexr_r4"), S8_INITIALIZER("norexw_prefix"), S8_INITIALIZER("not64"),
        S8_INITIALIZER("not_refining"), S8_INITIALIZER("not_refining_f3"), S8_INITIALIZER("osz_refining_prefix"),
        S8_INITIALIZER("repeating_prefix"), S8_INITIALIZER("refining_f3"), S8_INITIALIZER("repe"), S8_INITIALIZER("rexb_prefix"),
        S8_INITIALIZER("repne"), S8_INITIALIZER("rexb4_prefix"), S8_INITIALIZER("rex2_refining_prefix"),
        S8_INITIALIZER("rexw_prefix"), S8_INITIALIZER("f2_refining_prefix"), S8_INITIALIZER("f3_refining_prefix"),
        S8_INITIALIZER("not_refining_f3"),
    };
    if (xed_import_string_in_array(token, words, BUSTER_ARRAY_LENGTH(words)))
    {
        return true;
    }
    if ((string_starts_with_sequence(token, S8("SCC")) && token.length > 3) ||
        (string_starts_with_sequence(token, S8("VL")) && token.length > 2))
    {
        u64 value = 0;
        return xed_import_parse_integer(string_slice(token, string_starts_with_sequence(token, S8("SCC")) ? 3 : 2, token.length), &value);
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool xed_import_pattern_token_known(String8 token)
{
    u64 numeric = 0;
    if (xed_import_parse_integer(token, &numeric))
    {
        return true;
    }
    return xed_import_pattern_bracket_token_known(token) || xed_import_pattern_function_known(token) ||
           xed_import_pattern_assignment_known(token) || xed_import_pattern_word_known(token);
}

BUSTER_GLOBAL_LOCAL u8 xed_import_tuple_kind(String8 name)
{
    if (string_equal(name, S8("NELEM_FULL")) || string_equal(name, S8("NELEM_FULLMEM")) ||
        string_equal(name, S8("NELEM_MEM128")))
    {
        return XED_GENERATED_TUPLE_FULL;
    }
    if (string_equal(name, S8("NELEM_HALF")) || string_equal(name, S8("NELEM_HALFMEM")))
    {
        return XED_GENERATED_TUPLE_HALF;
    }
    if (string_equal(name, S8("NELEM_QUARTER")) || string_equal(name, S8("NELEM_QUARTERMEM")))
    {
        return XED_GENERATED_TUPLE_QUARTER;
    }
    if (string_equal(name, S8("NELEM_EIGHTHMEM")))
    {
        return XED_GENERATED_TUPLE_EIGHTH;
    }
    if (string_equal(name, S8("NELEM_ONE")) || string_equal(name, S8("NELEM_MOVDDUP")))
    {
        return XED_GENERATED_TUPLE_SCALAR;
    }
    if (string_equal(name, S8("NELEM_TUPLE1_4X")))
    {
        return XED_GENERATED_TUPLE_TUPLE1_4X;
    }
    if (string_equal(name, S8("NELEM_TUPLE1")))
    {
        return XED_GENERATED_TUPLE_TUPLE1;
    }
    if (string_equal(name, S8("NELEM_TUPLE1_BYTE")))
    {
        return XED_GENERATED_TUPLE_TUPLE1_BYTE;
    }
    if (string_equal(name, S8("NELEM_TUPLE1_WORD")))
    {
        return XED_GENERATED_TUPLE_TUPLE1_WORD;
    }
    if (string_equal(name, S8("NELEM_TUPLE2")))
    {
        return XED_GENERATED_TUPLE_TUPLE2;
    }
    if (string_equal(name, S8("NELEM_TUPLE4")))
    {
        return XED_GENERATED_TUPLE_TUPLE4;
    }
    if (string_equal(name, S8("NELEM_TUPLE8")))
    {
        return XED_GENERATED_TUPLE_TUPLE8;
    }
    return XED_GENERATED_TUPLE_NONE;
}

BUSTER_GLOBAL_LOCAL bool xed_import_attribute_contains(String8 attributes, String8 wanted);

BUSTER_GLOBAL_LOCAL u8 xed_import_attribute_tuple_kind(String8 attributes)
{
    static String8 const names[] = {
        S8_INITIALIZER("DISP8_FULL"), S8_INITIALIZER("DISP8_FULLMEM"), S8_INITIALIZER("DISP8_HALF"),
        S8_INITIALIZER("DISP8_HALFMEM"), S8_INITIALIZER("DISP8_QUARTER"), S8_INITIALIZER("DISP8_QUARTERMEM"),
        S8_INITIALIZER("DISP8_EIGHTHMEM"), S8_INITIALIZER("DISP8_SCALAR"), S8_INITIALIZER("DISP8_MOVDDUP"),
        S8_INITIALIZER("DISP8_TUPLE1"), S8_INITIALIZER("DISP8_TUPLE1_4X"), S8_INITIALIZER("DISP8_TUPLE1_BYTE"),
        S8_INITIALIZER("DISP8_TUPLE1_WORD"), S8_INITIALIZER("DISP8_TUPLE2"), S8_INITIALIZER("DISP8_TUPLE4"),
        S8_INITIALIZER("DISP8_TUPLE8"),
    };
    static u8 const kinds[] = {
        XED_GENERATED_TUPLE_FULL, XED_GENERATED_TUPLE_FULL, XED_GENERATED_TUPLE_HALF, XED_GENERATED_TUPLE_HALF,
        XED_GENERATED_TUPLE_QUARTER, XED_GENERATED_TUPLE_QUARTER, XED_GENERATED_TUPLE_EIGHTH,
        XED_GENERATED_TUPLE_SCALAR, XED_GENERATED_TUPLE_SCALAR, XED_GENERATED_TUPLE_TUPLE1,
        XED_GENERATED_TUPLE_TUPLE1_4X, XED_GENERATED_TUPLE_TUPLE1_BYTE, XED_GENERATED_TUPLE_TUPLE1_WORD,
        XED_GENERATED_TUPLE_TUPLE2, XED_GENERATED_TUPLE_TUPLE4, XED_GENERATED_TUPLE_TUPLE8,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        if (xed_import_attribute_contains(attributes, names[index]))
        {
            return kinds[index];
        }
    }
    return XED_GENERATED_TUPLE_NONE;
}

BUSTER_GLOBAL_LOCAL void xed_import_pattern_summary_token(XedGeneratedForm* form, String8 token)
{
    u64 numeric = 0;
    if (xed_import_parse_integer(token, &numeric))
    {
        if (token.length >= 2 && token.pointer[0] == '0' && (token.pointer[1] == 'x' || token.pointer[1] == 'X') &&
            form->fixed_byte_count < XED_GENERATED_MAX_FIXED_BYTES && numeric <= 0xff)
        {
            form->fixed_bytes[form->fixed_byte_count++] = (u8)numeric;
        }
        return;
    }

    if (xed_import_pattern_bracket_token_known(token))
    {
        form->field_flags |= XED_GENERATED_FIELD_MODRM;
        u64 open = string_first_code_unit(token, '[');
        String8 base = string_slice(token, 0, open);
        String8 value = string_slice(token, open + 1, token.length - 1);
        if (string_equal(base, S8("MOD")) && string_equal(value, S8("mm")))
        {
            form->field_flags |= XED_GENERATED_FIELD_MEMORY;
        }
        if (string_equal(base, S8("MOD")) && string_equal(value, S8("0b11")))
        {
            form->field_flags |= XED_GENERATED_FIELD_REGISTER;
        }
        return;
    }

    if (xed_import_pattern_assignment_known(token))
    {
        u64 equals = string_first_code_unit(token, '=');
        String8 key = string_slice(token, 0, equals);
        if (key.length && key.pointer[key.length - 1] == '!')
        {
            key = string_slice(key, 0, key.length - 1);
        }
        if (string_equal(key, S8("ND")))
        {
            form->apx_flags |= XED_GENERATED_APX_ND;
        }
        else if (string_equal(key, S8("NF")))
        {
            form->apx_flags |= XED_GENERATED_APX_NF;
        }
        if (string_equal(key, S8("MOD")) || string_equal(key, S8("RM")) || string_equal(key, S8("REG")) ||
            string_equal(key, S8("SRM")))
        {
            form->field_flags |= XED_GENERATED_FIELD_MODRM;
        }
        return;
    }

    if (xed_import_pattern_function_known(token))
    {
        u64 open = string_first_code_unit(token, '(');
        String8 name = string_slice(token, 0, open);
        form->field_flags |= string_starts_with_sequence(name, S8("MODRM")) ? XED_GENERATED_FIELD_MODRM : 0;
        form->field_flags |= string_starts_with_sequence(name, S8("BRDISP")) ? XED_GENERATED_FIELD_RELATIVE | XED_GENERATED_FIELD_FIELD_END : 0;
        form->field_flags |= string_starts_with_sequence(name, S8("SIMM")) || string_starts_with_sequence(name, S8("UIMM")) ||
                                     string_starts_with_sequence(name, S8("SE_IMM"))
                                 ? XED_GENERATED_FIELD_IMMEDIATE
                                 : 0;
        form->field_flags |= string_starts_with_sequence(name, S8("MEMDISP")) ? XED_GENERATED_FIELD_DISPLACEMENT : 0;
        if (string_starts_with_sequence(name, S8("NELEM_")))
        {
            form->tuple_rule = name;
            form->tuple_kind = xed_import_tuple_kind(name);
            form->field_flags |= XED_GENERATED_FIELD_DISPLACEMENT;
        }
        if (string_starts_with_sequence(name, S8("ESIZE_")))
        {
            form->element_size = name;
        }
        if (string_equal(name, S8("SAE")))
        {
            form->decorator_flags |= XED_GENERATED_DECORATOR_SAE;
        }
        if (string_equal(name, S8("AVX512_ROUND")))
        {
            form->decorator_flags |= XED_GENERATED_DECORATOR_ROUNDING;
        }
        if (string_starts_with_sequence(name, S8("EVAPX")))
        {
            form->prefix_kind = XED_GENERATED_PREFIX_EVEX;
            form->apx_flags |= XED_GENERATED_APX;
            if (string_equal(name, S8("EVAPX_SCC")))
            {
                form->apx_flags |= XED_GENERATED_APX_SCC;
            }
        }
        return;
    }

    if (string_equal(token, S8("EVV")))
    {
        form->prefix_kind = XED_GENERATED_PREFIX_EVEX;
    }
    else if (string_equal(token, S8("VV1")) || string_equal(token, S8("VNP")))
    {
        form->prefix_kind = XED_GENERATED_PREFIX_VEX;
    }
    else if (string_equal(token, S8("XOPV")))
    {
        form->prefix_kind = XED_GENERATED_PREFIX_XOP;
    }
    else if (string_equal(token, S8("rex2_refining_prefix")) || string_equal(token, S8("norex2_prefix")))
    {
        form->prefix_kind = XED_GENERATED_PREFIX_REX2;
    }
    else if (string_contains(token, S8("rex")))
    {
        form->prefix_kind = XED_GENERATED_PREFIX_REX;
    }

    if (string_equal(token, S8("V0F")))
    {
        form->map = XED_GENERATED_MAP_0F;
    }
    else if (string_equal(token, S8("V0F38")))
    {
        form->map = XED_GENERATED_MAP_0F38;
    }
    else if (string_equal(token, S8("V0F3A")))
    {
        form->map = XED_GENERATED_MAP_0F3A;
    }
    else if (string_equal(token, S8("MAP4")))
    {
        form->map = XED_GENERATED_MAP_4;
    }
    else if (string_equal(token, S8("MAP5")))
    {
        form->map = XED_GENERATED_MAP_5;
    }
    else if (string_equal(token, S8("MAP6")))
    {
        form->map = XED_GENERATED_MAP_6;
    }
    else if (string_equal(token, S8("MAP7")))
    {
        form->map = XED_GENERATED_MAP_7;
    }
    else if (string_equal(token, S8("XMAP8")))
    {
        form->map = XED_GENERATED_MAP_X8;
    }
    else if (string_equal(token, S8("XMAP9")))
    {
        form->map = XED_GENERATED_MAP_X9;
    }
    else if (string_equal(token, S8("XMAPA")))
    {
        form->map = XED_GENERATED_MAP_XA;
    }

    if (string_equal(token, S8("66_prefix")) || string_equal(token, S8("V66")))
    {
        form->mandatory_prefix = 0x66;
    }
    else if (string_equal(token, S8("f2_refining_prefix")) || string_equal(token, S8("VF2")))
    {
        form->mandatory_prefix = 0xf2;
    }
    else if (string_equal(token, S8("f3_refining_prefix")) || string_equal(token, S8("VF3")))
    {
        form->mandatory_prefix = 0xf3;
    }

    if (string_starts_with_sequence(token, S8("VL")))
    {
        form->field_flags |= XED_GENERATED_FIELD_MODRM;
    }
    if (string_equal(token, S8("MODRM()")))
    {
        form->field_flags |= XED_GENERATED_FIELD_MODRM;
    }
    if (string_equal(token, S8("SIB")))
    {
        form->field_flags |= XED_GENERATED_FIELD_SIB;
    }
    if (string_starts_with_sequence(token, S8("UISA_VMODRM_")) || string_starts_with_sequence(token, S8("VMODRM_")))
    {
        form->field_flags |= XED_GENERATED_FIELD_VSIB;
    }
    if (string_equal(token, S8("mode16")))
    {
        form->mode_flags |= XED_GENERATED_MODE_16;
    }
    else if (string_equal(token, S8("mode32")))
    {
        form->mode_flags |= XED_GENERATED_MODE_32;
    }
    else if (string_equal(token, S8("mode64")))
    {
        form->mode_flags |= XED_GENERATED_MODE_64;
    }
    else if (string_equal(token, S8("not64")))
    {
        form->mode_flags |= XED_GENERATED_MODE_NOT64;
    }
    else if (string_equal(token, S8("eamode16")))
    {
        form->mode_flags |= XED_GENERATED_MODE_EA16;
    }
    else if (string_equal(token, S8("eamode32")))
    {
        form->mode_flags |= XED_GENERATED_MODE_EA32;
    }
    else if (string_equal(token, S8("eamode64")))
    {
        form->mode_flags |= XED_GENERATED_MODE_EA64;
    }
    else if (string_equal(token, S8("eanot16")))
    {
        form->mode_flags |= XED_GENERATED_MODE_EANOT16;
    }
    if (string_equal(token, S8("MASK=0")))
    {
        form->decorator_flags |= XED_GENERATED_DECORATOR_MASK;
    }
    if (string_equal(token, S8("ZEROING=0")))
    {
        form->decorator_flags |= XED_GENERATED_DECORATOR_ZEROING;
    }
}

BUSTER_GLOBAL_LOCAL bool xed_import_normalize_pattern(XedGeneratedForm* form, String8 pattern, String8* bad_token)
{
    u64 start = 0;
    while (start < pattern.length)
    {
        while (start < pattern.length && character_is_space(pattern.pointer[start]))
        {
            start += 1;
        }
        if (start == pattern.length)
        {
            break;
        }
        u64 end = start;
        while (end < pattern.length && !character_is_space(pattern.pointer[end]))
        {
            end += 1;
        }
        String8 token = string_slice(pattern, start, end);
        if (!xed_import_pattern_token_known(token))
        {
            *bad_token = token;
            return false;
        }
        xed_import_pattern_summary_token(form, token);
        form->token_count += 1;
        start = end;
    }
    return form->token_count != 0;
}

BUSTER_GLOBAL_LOCAL bool xed_import_identifier_has_numeric_suffix(String8 token, String8 prefix)
{
    if (!string_starts_with_sequence(token, prefix) || token.length == prefix.length)
    {
        return false;
    }
    u64 value = 0;
    return xed_import_parse_integer(string_slice(token, prefix.length, token.length), &value);
}

BUSTER_GLOBAL_LOCAL bool xed_import_operand_identifier_known(String8 token)
{
    u64 numeric = 0;
    if (xed_import_parse_integer(token, &numeric))
    {
        return true;
    }
    static String8 const exact[] = {
        S8_INITIALIZER("2bf16"), S8_INITIALIZER("2f16"), S8_INITIALIZER("2i16"), S8_INITIALIZER("2i8"), S8_INITIALIZER("2u16"),
        S8_INITIALIZER("4bf8"), S8_INITIALIZER("4hf8"), S8_INITIALIZER("4i8"), S8_INITIALIZER("4u8"),
        S8_INITIALIZER("A_GPR_B"), S8_INITIALIZER("A_GPR_R"), S8_INITIALIZER("AGEN"), S8_INITIALIZER("ABSBR"),
        S8_INITIALIZER("ArAX"), S8_INITIALIZER("ArBP"), S8_INITIALIZER("ArBX"), S8_INITIALIZER("ArCX"),
        S8_INITIALIZER("ArDI"), S8_INITIALIZER("ArDX"), S8_INITIALIZER("ArSI"), S8_INITIALIZER("ArSP"), S8_INITIALIZER("BCASTSTR"),
        S8_INITIALIZER("BND_B"), S8_INITIALIZER("BND_R"), S8_INITIALIZER("OeAX"),
        S8_INITIALIZER("CR_R"), S8_INITIALIZER("DR_R"), S8_INITIALIZER("ECOND"), S8_INITIALIZER("FINAL_DSEG"),
        S8_INITIALIZER("FINAL_DSEG1"), S8_INITIALIZER("FINAL_ESEG"), S8_INITIALIZER("FINAL_ESEG1"),
        S8_INITIALIZER("FINAL_SSEG0"), S8_INITIALIZER("IMPL"), S8_INITIALIZER("INDEX"), S8_INITIALIZER("MASK1"),
        S8_INITIALIZER("MASKNOT0"), S8_INITIALIZER("MASK_B"), S8_INITIALIZER("MASK_N"), S8_INITIALIZER("MASK_R"),
        S8_INITIALIZER("MEM0"), S8_INITIALIZER("MEM1"), S8_INITIALIZER("MULTIDEST2"), S8_INITIALIZER("MULTISOURCE4"),
        S8_INITIALIZER("NDD"), S8_INITIALIZER("OrAX"), S8_INITIALIZER("OrBP"), S8_INITIALIZER("OrBX"),
        S8_INITIALIZER("OrCX"), S8_INITIALIZER("OrDX"), S8_INITIALIZER("OrSP"), S8_INITIALIZER("PTR"),
        S8_INITIALIZER("RELBR"), S8_INITIALIZER("ROUNDC"), S8_INITIALIZER("SAESTR"), S8_INITIALIZER("SCALE"),
        S8_INITIALIZER("SEG"), S8_INITIALIZER("SEG_MOV"), S8_INITIALIZER("SUPP"), S8_INITIALIZER("TMM_B"), S8_INITIALIZER("TMM_B3"),
        S8_INITIALIZER("TMM_N"), S8_INITIALIZER("TMM_N3"), S8_INITIALIZER("TMM_R"), S8_INITIALIZER("TMM_R3"),
        S8_INITIALIZER("TXT"), S8_INITIALIZER("VGPR32_B"), S8_INITIALIZER("VGPR32_N"), S8_INITIALIZER("VGPR32_R"),
        S8_INITIALIZER("VGPR64_B"), S8_INITIALIZER("VGPR64_N"), S8_INITIALIZER("VGPR64_R"),
        S8_INITIALIZER("VGPRy_B"), S8_INITIALIZER("VGPRy_N"), S8_INITIALIZER("VGPRy_R"), S8_INITIALIZER("X87"),
        S8_INITIALIZER("XMM_B"), S8_INITIALIZER("XMM_B3"), S8_INITIALIZER("XMM_N"), S8_INITIALIZER("XMM_N3"),
        S8_INITIALIZER("XMM_R"), S8_INITIALIZER("XMM_R3"), S8_INITIALIZER("XMM_SE"), S8_INITIALIZER("YMM_B"),
        S8_INITIALIZER("YMM_B3"), S8_INITIALIZER("YMM_N"), S8_INITIALIZER("YMM_N3"), S8_INITIALIZER("YMM_R"),
        S8_INITIALIZER("YMM_R3"), S8_INITIALIZER("YMM_SE"), S8_INITIALIZER("ZMM_B3"), S8_INITIALIZER("ZMM_N3"),
        S8_INITIALIZER("ZMM_R3"), S8_INITIALIZER("ZEROSTR"), S8_INITIALIZER("a16"), S8_INITIALIZER("a32"), S8_INITIALIZER("b"),
        S8_INITIALIZER("bf16"), S8_INITIALIZER("bf4"), S8_INITIALIZER("bf6"), S8_INITIALIZER("bf8"),
        S8_INITIALIZER("bnd32"), S8_INITIALIZER("bnd64"), S8_INITIALIZER("cr"), S8_INITIALIZER("crw"), S8_INITIALIZER("cw"),
        S8_INITIALIZER("d"), S8_INITIALIZER("dq"), S8_INITIALIZER("f16"), S8_INITIALIZER("f32"),
        S8_INITIALIZER("f64"), S8_INITIALIZER("f80"), S8_INITIALIZER("hf6"), S8_INITIALIZER("hf8"),
        S8_INITIALIZER("i1"), S8_INITIALIZER("i128"), S8_INITIALIZER("i16"), S8_INITIALIZER("i32"), S8_INITIALIZER("i64"),
        S8_INITIALIZER("i8"), S8_INITIALIZER("m384"), S8_INITIALIZER("m512"), S8_INITIALIZER("m64int"),
        S8_INITIALIZER("m64real"), S8_INITIALIZER("mem14"), S8_INITIALIZER("mem94"), S8_INITIALIZER("mem108"), S8_INITIALIZER("mem16"), S8_INITIALIZER("mem16int"),
        S8_INITIALIZER("mem28"), S8_INITIALIZER("mem32int"), S8_INITIALIZER("mem32real"), S8_INITIALIZER("mem80dec"),
        S8_INITIALIZER("mem80real"), S8_INITIALIZER("mfpxenv"), S8_INITIALIZER("mprefetch"), S8_INITIALIZER("mskw"),
        S8_INITIALIZER("mxsave"), S8_INITIALIZER("oo"), S8_INITIALIZER("p"), S8_INITIALIZER("p2"), S8_INITIALIZER("pmmsz16"),
        S8_INITIALIZER("pmmsz32"), S8_INITIALIZER("pmmsz64"), S8_INITIALIZER("ptr"),
        S8_INITIALIZER("pd"), S8_INITIALIZER("ps"), S8_INITIALIZER("q"), S8_INITIALIZER("qq"),
        S8_INITIALIZER("r"), S8_INITIALIZER("rIP"), S8_INITIALIZER("rcw"), S8_INITIALIZER("rw"),
        S8_INITIALIZER("s"), S8_INITIALIZER("s64"), S8_INITIALIZER("sd"), S8_INITIALIZER("spw"),
        S8_INITIALIZER("spw2"), S8_INITIALIZER("spw5"), S8_INITIALIZER("spw8"), S8_INITIALIZER("ss"),
        S8_INITIALIZER("tv"), S8_INITIALIZER("u128"), S8_INITIALIZER("u16"), S8_INITIALIZER("u256"),
        S8_INITIALIZER("u32"), S8_INITIALIZER("u64"), S8_INITIALIZER("u8"), S8_INITIALIZER("v"),
        S8_INITIALIZER("vv"), S8_INITIALIZER("w"), S8_INITIALIZER("wrd"), S8_INITIALIZER("xub"),
        S8_INITIALIZER("xud"), S8_INITIALIZER("xuq"), S8_INITIALIZER("y"), S8_INITIALIZER("yu"),
        S8_INITIALIZER("z"), S8_INITIALIZER("z2f16"), S8_INITIALIZER("z2i16"), S8_INITIALIZER("z2i8"),
        S8_INITIALIZER("z2u16"), S8_INITIALIZER("z4i8"), S8_INITIALIZER("z4u8"), S8_INITIALIZER("zbf16"),
        S8_INITIALIZER("zbf6"), S8_INITIALIZER("zbf8"), S8_INITIALIZER("zd"), S8_INITIALIZER("zf16"),
        S8_INITIALIZER("zf32"), S8_INITIALIZER("zf64"), S8_INITIALIZER("zhf6"), S8_INITIALIZER("zhf8"),
        S8_INITIALIZER("zi16"), S8_INITIALIZER("zi32"), S8_INITIALIZER("zi64"), S8_INITIALIZER("zi8"),
        S8_INITIALIZER("zu128"), S8_INITIALIZER("zu16"), S8_INITIALIZER("zu32"), S8_INITIALIZER("zu64"),
        S8_INITIALIZER("zu8"),
    };
    if (xed_import_string_in_array(token, exact, BUSTER_ARRAY_LENGTH(exact)))
    {
        return true;
    }
    if (xed_import_identifier_has_numeric_suffix(token, S8("Ar")))
    {
        u64 value = 0;
        return xed_import_parse_integer(string_slice(token, 2, token.length), &value) && value >= 8 && value <= 31;
    }
    if (xed_import_identifier_has_numeric_suffix(token, S8("REG")) || xed_import_identifier_has_numeric_suffix(token, S8("MEM")) ||
        xed_import_identifier_has_numeric_suffix(token, S8("IMM")) || xed_import_identifier_has_numeric_suffix(token, S8("BASE")) ||
        xed_import_identifier_has_numeric_suffix(token, S8("SEG")))
    {
        return true;
    }
    if (string_starts_with_sequence(token, S8("EMX_BROADCAST_")))
    {
        for (u64 index = 14; index < token.length; index += 1)
        {
            char8 c = token.pointer[index];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '_'))
            {
                return false;
            }
        }
        return token.length > 14;
    }
    if (string_starts_with_sequence(token, S8("GPR")) || string_starts_with_sequence(token, S8("VGPR")) ||
        string_starts_with_sequence(token, S8("XMM")) || string_starts_with_sequence(token, S8("YMM")) ||
        string_starts_with_sequence(token, S8("ZMM")) || string_starts_with_sequence(token, S8("MMX")))
    {
        static String8 const suffixes[] = {
            S8_INITIALIZER("_B"), S8_INITIALIZER("_B3"), S8_INITIALIZER("_N"), S8_INITIALIZER("_N3"),
            S8_INITIALIZER("_R"), S8_INITIALIZER("_R3"), S8_INITIALIZER("_SB"), S8_INITIALIZER("_SE"),
            S8_INITIALIZER("_B_NORSP"), S8_INITIALIZER("_N_NORSP"),
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(suffixes); index += 1)
        {
            if (string_ends_with_sequence_insensitive(token, suffixes[index]))
            {
                return true;
            }
        }
    }
    if (string_starts_with_sequence(token, S8("XED_REG_")))
    {
        static String8 const registers[] = {
            S8_INITIALIZER("XED_REG_AH"), S8_INITIALIZER("XED_REG_AL"), S8_INITIALIZER("XED_REG_AX"),
            S8_INITIALIZER("XED_REG_BP"), S8_INITIALIZER("XED_REG_BSR0"), S8_INITIALIZER("XED_REG_BX"),
            S8_INITIALIZER("XED_REG_CL"), S8_INITIALIZER("XED_REG_CR0"), S8_INITIALIZER("XED_REG_CS"),
            S8_INITIALIZER("XED_REG_CX"), S8_INITIALIZER("XED_REG_DI"), S8_INITIALIZER("XED_REG_DS"),
            S8_INITIALIZER("XED_REG_DX"), S8_INITIALIZER("XED_REG_EAX"), S8_INITIALIZER("XED_REG_EBP"),
            S8_INITIALIZER("XED_REG_EBX"), S8_INITIALIZER("XED_REG_ECX"), S8_INITIALIZER("XED_REG_EDI"),
            S8_INITIALIZER("XED_REG_EDX"), S8_INITIALIZER("XED_REG_EFLAGS"), S8_INITIALIZER("XED_REG_FLAGS"),
            S8_INITIALIZER("XED_REG_EIP"),
            S8_INITIALIZER("XED_REG_ES"), S8_INITIALIZER("XED_REG_ESI"), S8_INITIALIZER("XED_REG_ESP"),
            S8_INITIALIZER("XED_REG_FS"), S8_INITIALIZER("XED_REG_FSBASE"), S8_INITIALIZER("XED_REG_GDTR"),
            S8_INITIALIZER("XED_REG_GS"), S8_INITIALIZER("XED_REG_GSBASE"), S8_INITIALIZER("XED_REG_IA32_KERNEL_GS_BASE"),
            S8_INITIALIZER("XED_REG_IDTR"), S8_INITIALIZER("XED_REG_INVALID"), S8_INITIALIZER("XED_REG_IP"),
            S8_INITIALIZER("XED_REG_LDTR"), S8_INITIALIZER("XED_REG_MSRS"), S8_INITIALIZER("XED_REG_MXCSR"),
            S8_INITIALIZER("XED_REG_R11"), S8_INITIALIZER("XED_REG_RAX"), S8_INITIALIZER("XED_REG_RBX"),
            S8_INITIALIZER("XED_REG_RCX"), S8_INITIALIZER("XED_REG_RDI"), S8_INITIALIZER("XED_REG_RDX"),
            S8_INITIALIZER("XED_REG_RFLAGS"), S8_INITIALIZER("XED_REG_RIP"), S8_INITIALIZER("XED_REG_RSI"),
            S8_INITIALIZER("XED_REG_RSP"), S8_INITIALIZER("XED_REG_SI"), S8_INITIALIZER("XED_REG_SP"),
            S8_INITIALIZER("XED_REG_SS"), S8_INITIALIZER("XED_REG_SSP"), S8_INITIALIZER("XED_REG_ST0"),
            S8_INITIALIZER("XED_REG_ST1"), S8_INITIALIZER("XED_REG_STACKPOP"), S8_INITIALIZER("XED_REG_STACKPUSH"),
            S8_INITIALIZER("XED_REG_TR"), S8_INITIALIZER("XED_REG_TSC"), S8_INITIALIZER("XED_REG_TSCAUX"),
            S8_INITIALIZER("XED_REG_UIF"), S8_INITIALIZER("XED_REG_X87CONTROL"), S8_INITIALIZER("XED_REG_X87POP"),
            S8_INITIALIZER("XED_REG_X87POP2"), S8_INITIALIZER("XED_REG_X87PUSH"), S8_INITIALIZER("XED_REG_X87STATUS"),
            S8_INITIALIZER("XED_REG_X87TAG"), S8_INITIALIZER("XED_REG_XCR0"), S8_INITIALIZER("XED_REG_XMM0"),
            S8_INITIALIZER("XED_REG_XMM1"), S8_INITIALIZER("XED_REG_XMM2"), S8_INITIALIZER("XED_REG_XMM3"),
            S8_INITIALIZER("XED_REG_XMM4"), S8_INITIALIZER("XED_REG_XMM5"), S8_INITIALIZER("XED_REG_XMM6"),
            S8_INITIALIZER("XED_REG_XMM7"),
        };
        return xed_import_string_in_array(token, registers, BUSTER_ARRAY_LENGTH(registers));
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool xed_import_operand_token_known(String8 token)
{
    if (!token.length || string_equal(token, S8("\\")))
    {
        return false;
    }
    bool identifier_seen = false;
    u64 start = 0;
    for (u64 index = 0; index <= token.length; index += 1)
    {
        bool identifier_character = index < token.length &&
                                     ((token.pointer[index] >= 'a' && token.pointer[index] <= 'z') ||
                                      (token.pointer[index] >= 'A' && token.pointer[index] <= 'Z') ||
                                      (token.pointer[index] >= '0' && token.pointer[index] <= '9') || token.pointer[index] == '_');
        if (!identifier_character)
        {
            if (index > start)
            {
                String8 atom = string_slice(token, start, index);
                if (!xed_import_operand_identifier_known(atom))
                {
                    return false;
                }
                identifier_seen = true;
            }
            start = index + 1;
        }
    }
    return identifier_seen;
}

BUSTER_GLOBAL_LOCAL bool xed_import_operand_access_part(String8 part, u8* access)
{
    if (string_equal(part, S8("r")) || string_equal(part, S8("rw")) || string_equal(part, S8("rcw")) ||
        string_equal(part, S8("cr")))
    {
        *access |= XED_GENERATED_ACCESS_READ;
    }
    if (string_equal(part, S8("w")) || string_equal(part, S8("rw")) || string_equal(part, S8("rcw")) ||
        string_equal(part, S8("cw")))
    {
        *access |= XED_GENERATED_ACCESS_WRITE;
    }
    if (string_equal(part, S8("rcw")) || string_equal(part, S8("cr")) || string_equal(part, S8("cw")))
    {
        *access |= XED_GENERATED_ACCESS_COND;
    }
    if (string_equal(part, S8("SUPP")))
    {
        *access |= XED_GENERATED_ACCESS_SUPPRESSED;
    }
    if (string_equal(part, S8("IMPL")))
    {
        *access |= XED_GENERATED_ACCESS_IMPLICIT;
    }
    return string_equal(part, S8("r")) || string_equal(part, S8("w")) || string_equal(part, S8("rw")) ||
           string_equal(part, S8("rcw")) || string_equal(part, S8("cr")) || string_equal(part, S8("cw")) ||
           string_equal(part, S8("SUPP")) || string_equal(part, S8("IMPL"));
}

BUSTER_GLOBAL_LOCAL bool xed_import_operand_key_slot(String8 key, u8* slot)
{
    static String8 const prefixes[] = {
        S8_INITIALIZER("REG"), S8_INITIALIZER("MEM"), S8_INITIALIZER("IMM"), S8_INITIALIZER("BASE"),
        S8_INITIALIZER("SEG"), S8_INITIALIZER("MASK"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(prefixes); index += 1)
    {
        if (string_starts_with_sequence(key, prefixes[index]))
        {
            String8 suffix = string_slice(key, prefixes[index].length, key.length);
            u64 value = 0;
            if (xed_import_parse_integer(suffix, &value) && value < 255)
            {
                *slot = (u8)value;
                return true;
            }
        }
    }
    *slot = UINT8_MAX;
    return false;
}

BUSTER_GLOBAL_LOCAL bool xed_import_normalize_operands(XedGeneratedForm* form, String8 operands, String8* bad_token)
{
    u64 start = 0;
    while (start < operands.length)
    {
        while (start < operands.length && character_is_space(operands.pointer[start]))
        {
            start += 1;
        }
        if (start == operands.length)
        {
            break;
        }
        u64 end = start;
        while (end < operands.length && !character_is_space(operands.pointer[end]))
        {
            end += 1;
        }
        String8 token = string_slice(operands, start, end);
        if (!xed_import_operand_token_known(token))
        {
            *bad_token = token;
            return false;
        }
        if (form->operand_count >= XED_GENERATED_MAX_OPERANDS)
        {
            *bad_token = token;
            return false;
        }

        XedGeneratedOperand* operand = form->operands + form->operand_count++;
        *operand = (XedGeneratedOperand){0};
        u64 colon = string_first_code_unit(token, ':');
        u64 equals = string_first_code_unit(token, '=');
        u64 head_end = colon == BUSTER_STRING_NO_MATCH ? token.length : colon;
        String8 head = string_slice(token, 0, head_end);
        u64 key_end = equals != BUSTER_STRING_NO_MATCH && equals < head_end ? equals : head_end;
        String8 key = string_slice(token, 0, key_end);
        xed_import_operand_key_slot(key, &operand->slot);
        if (string_starts_with_sequence(key, S8("REG")) || string_starts_with_sequence(key, S8("MASK")))
        {
            operand->kind = XED_GENERATED_OPERAND_REGISTER;
        }
        else if (string_starts_with_sequence(key, S8("MEM")))
        {
            operand->kind = XED_GENERATED_OPERAND_MEMORY;
        }
        else if (string_starts_with_sequence(key, S8("IMM")))
        {
            operand->kind = XED_GENERATED_OPERAND_IMMEDIATE;
        }
        else if (string_starts_with_sequence(key, S8("RELBR")))
        {
            operand->kind = XED_GENERATED_OPERAND_RELATIVE;
            operand->field_source = XED_GENERATED_FIELD_SOURCE_RELATIVE;
            form->field_flags |= XED_GENERATED_FIELD_RELATIVE | XED_GENERATED_FIELD_FIELD_END;
            form->relocation_base = 1;
        }
        else if (string_starts_with_sequence(key, S8("ABSBR")))
        {
            operand->kind = XED_GENERATED_OPERAND_ABSOLUTE;
        }
        else if (string_starts_with_sequence(key, S8("BASE")))
        {
            operand->kind = XED_GENERATED_OPERAND_BASE;
        }
        else if (string_starts_with_sequence(key, S8("SEG")))
        {
            operand->kind = XED_GENERATED_OPERAND_SEGMENT;
        }
        else if (string_starts_with_sequence(key, S8("AGEN")))
        {
            operand->kind = XED_GENERATED_OPERAND_ADDRESS_GENERATOR;
        }
        else
        {
            operand->kind = XED_GENERATED_OPERAND_PSEUDO;
            operand->slot = UINT8_MAX;
        }

        if (operand->kind == XED_GENERATED_OPERAND_MEMORY)
        {
            form->field_flags |= XED_GENERATED_FIELD_MEMORY;
        }
        else if (operand->kind == XED_GENERATED_OPERAND_REGISTER)
        {
            form->field_flags |= XED_GENERATED_FIELD_REGISTER;
        }
        if (string_starts_with_sequence(key, S8("RELBR")))
        {
            form->field_flags |= XED_GENERATED_FIELD_RELATIVE | XED_GENERATED_FIELD_FIELD_END;
        }
        if (equals != BUSTER_STRING_NO_MATCH && equals + 1 < head_end)
        {
            operand->atom = string_slice(token, equals + 1, head_end);
        }
        else
        {
            operand->atom = key;
        }
        if (string_starts_with_sequence(operand->atom, S8("XED_REG_")))
        {
            operand->field_source = XED_GENERATED_FIELD_SOURCE_FIXED;
        }
        else if (string_ends_with_sequence_insensitive(operand->atom, S8("_R")) ||
                 string_ends_with_sequence_insensitive(operand->atom, S8("_R3")))
        {
            operand->field_source = XED_GENERATED_FIELD_SOURCE_REG;
        }
        else if (string_ends_with_sequence_insensitive(operand->atom, S8("_B")) ||
                 string_ends_with_sequence_insensitive(operand->atom, S8("_B3")))
        {
            operand->field_source = XED_GENERATED_FIELD_SOURCE_RM;
        }
        else if (string_ends_with_sequence_insensitive(operand->atom, S8("_N")) ||
                 string_ends_with_sequence_insensitive(operand->atom, S8("_N3")))
        {
            operand->field_source = XED_GENERATED_FIELD_SOURCE_VVVV;
        }
        else if (string_starts_with_sequence(key, S8("MASK")))
        {
            operand->field_source = XED_GENERATED_FIELD_SOURCE_MASK;
        }
        else if (operand->kind == XED_GENERATED_OPERAND_IMMEDIATE)
        {
            operand->field_source = XED_GENERATED_FIELD_SOURCE_IMMEDIATE;
        }
        else if (operand->field_source == XED_GENERATED_FIELD_SOURCE_NONE)
        {
            if (string_starts_with_sequence(key, S8("REG")))
            {
                operand->field_source = XED_GENERATED_FIELD_SOURCE_REG;
            }
            else if (string_starts_with_sequence(key, S8("MEM")))
            {
                operand->field_source = XED_GENERATED_FIELD_SOURCE_RM;
            }
        }
        if (string_starts_with_sequence(operand->atom, S8("TMM")))
        {
            form->amx_flags |= XED_GENERATED_AMX_TILE_REGISTER;
        }

        u64 part_start = head_end < token.length ? head_end + 1 : token.length;
        while (part_start < token.length)
        {
            u64 part_end = part_start;
            while (part_end < token.length && token.pointer[part_end] != ':')
            {
                part_end += 1;
            }
            String8 part = string_slice(token, part_start, part_end);
            if (xed_import_operand_access_part(part, &operand->access))
            {
                if (operand->access & XED_GENERATED_ACCESS_SUPPRESSED)
                {
                    operand->visible = 0;
                }
            }
            else if (part.length && xed_import_operand_identifier_known(part))
            {
                operand->width = part;
            }
            part_start = part_end < token.length ? part_end + 1 : token.length;
        }
        operand->visible = (operand->access & (XED_GENERATED_ACCESS_SUPPRESSED | XED_GENERATED_ACCESS_IMPLICIT)) == 0;
        if (string_contains(token, S8("BCASTSTR")) || string_contains(token, S8("EMX_BROADCAST_")))
        {
            form->decorator_flags |= XED_GENERATED_DECORATOR_BROADCAST;
        }
        if (string_contains(token, S8("ZEROSTR")))
        {
            form->decorator_flags |= XED_GENERATED_DECORATOR_ZEROING;
        }
        if (string_contains(token, S8("MASK")))
        {
            form->decorator_flags |= XED_GENERATED_DECORATOR_MASK;
        }
        if (string_contains(token, S8("SAESTR")) || string_contains(token, S8("ROUNDC")))
        {
            form->decorator_flags |= XED_GENERATED_DECORATOR_SAE | XED_GENERATED_DECORATOR_ROUNDING;
        }
        start = end;
    }
    return true;
}

enum
{
    XED_GENERATED_ENCODER_LEGACY,
    XED_GENERATED_ENCODER_REX,
    XED_GENERATED_ENCODER_REX2,
    XED_GENERATED_ENCODER_VEX,
    XED_GENERATED_ENCODER_XOP,
    XED_GENERATED_ENCODER_EVEX,
    XED_GENERATED_ENCODER_AMX,
    XED_GENERATED_ENCODER_SYSTEM,
};

BUSTER_GLOBAL_LOCAL bool xed_import_attribute_contains(String8 attributes, String8 wanted)
{
    u64 start = 0;
    while (start < attributes.length)
    {
        while (start < attributes.length && character_is_space(attributes.pointer[start]))
        {
            start += 1;
        }
        u64 end = start;
        while (end < attributes.length && !character_is_space(attributes.pointer[end]))
        {
            end += 1;
        }
        if (string_equal(string_slice(attributes, start, end), wanted))
        {
            return true;
        }
        start = end;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u64 xed_import_record_hash(XedImportRecord* record)
{
    String8 values[] = {
        record->source, record->iclass, record->iform, record->isa_set, record->category, record->extension,
        record->attributes, record->cpl, record->exceptions, record->flags, record->disasm, record->disasm_intel,
        record->disasm_attsv, record->real_opcode, record->uname, record->comment, record->version, record->pattern,
        record->operands, record->operand_annotation,
    };
    u64 result = UINT64_C(0x9e3779b97f4a7c15);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        u64 hash = buster_hash_64(values[index].pointer ? (u8*)values[index].pointer : (u8*)"", values[index].length);
        result ^= hash + UINT64_C(0x9e3779b97f4a7c15) + (result << 6) + (result >> 2);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool xed_import_normalize_record(XedGeneratedForm* form, XedImportRecord* record, u64 raw_index,
                                                      String8* bad_token, bool* bad_operand)
{
    *form = (XedGeneratedForm){
        .record = record,
        .stable_hash = xed_import_record_hash(record),
        .coverage_class = XED_GENERATED_COVERAGE_UNCLASSIFIED,
        .encoder_family = XED_GENERATED_ENCODER_LEGACY,
        .test_class = XED_GENERATED_TEST_SCHEMA,
        .prefix_kind = XED_GENERATED_PREFIX_LEGACY,
        .map = XED_GENERATED_MAP_LEGACY,
    };
    if (!xed_import_normalize_pattern(form, record->pattern, bad_token))
    {
        *bad_operand = false;
        form->coverage_class = XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN;
        form->reason_id = XED_GENERATED_REASON_UNKNOWN_PATTERN_TOKEN;
        form->unsupported_token = *bad_token;
        return false;
    }
    *bad_operand = false;
    if (!xed_import_normalize_operands(form, record->operands, bad_token))
    {
        *bad_operand = true;
        form->coverage_class = XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN;
        form->reason_id = XED_GENERATED_REASON_UNKNOWN_OPERAND_TOKEN;
        form->unsupported_token = *bad_token;
        return false;
    }
    if (string_contains(record->iclass, S8("TILEMOVROW")))
    {
        form->amx_flags |= XED_GENERATED_AMX_TILE_ROW;
    }
    if (string_contains(record->iclass, S8("TILEMOVCOL")))
    {
        form->amx_flags |= XED_GENERATED_AMX_TILE_COLUMN;
    }
    if (!form->tuple_kind)
    {
        form->tuple_kind = xed_import_attribute_tuple_kind(record->attributes);
    }
    if (string_contains(record->operand_annotation, S8("NDD")))
    {
        form->apx_flags |= XED_GENERATED_APX | XED_GENERATED_APX_NDD;
    }

    if (form->fixed_byte_count >= 2 && form->map == XED_GENERATED_MAP_LEGACY && form->fixed_bytes[0] == 0x0f)
    {
        form->map = form->fixed_bytes[1] == 0x38 ? XED_GENERATED_MAP_0F38
                    : form->fixed_bytes[1] == 0x3a ? XED_GENERATED_MAP_0F3A
                                                   : XED_GENERATED_MAP_0F;
    }
    if (form->prefix_kind == XED_GENERATED_PREFIX_LEGACY &&
        (form->mandatory_prefix || form->fixed_byte_count > 1 || form->field_flags & XED_GENERATED_FIELD_MODRM))
    {
        form->encoder_family = XED_GENERATED_ENCODER_LEGACY;
    }
    else
    {
        form->encoder_family = form->prefix_kind;
    }
    if (form->prefix_kind == XED_GENERATED_PREFIX_REX)
    {
        form->encoder_family = XED_GENERATED_ENCODER_REX;
    }
    else if (form->prefix_kind == XED_GENERATED_PREFIX_REX2)
    {
        form->encoder_family = XED_GENERATED_ENCODER_REX2;
    }
    else if (form->prefix_kind == XED_GENERATED_PREFIX_VEX)
    {
        form->encoder_family = XED_GENERATED_ENCODER_VEX;
    }
    else if (form->prefix_kind == XED_GENERATED_PREFIX_XOP)
    {
        form->encoder_family = XED_GENERATED_ENCODER_XOP;
    }
    else if (form->prefix_kind == XED_GENERATED_PREFIX_EVEX)
    {
        form->encoder_family = XED_GENERATED_ENCODER_EVEX;
    }

    if (xed_import_attribute_contains(record->attributes, S8("GATHER")) ||
        xed_import_attribute_contains(record->attributes, S8("SCATTER")))
    {
        form->field_flags |= XED_GENERATED_FIELD_VSIB;
    }
    if (xed_import_attribute_contains(record->attributes, S8("MASKOP_EVEX")) ||
        xed_import_attribute_contains(record->attributes, S8("KMASK")))
    {
        form->decorator_flags |= XED_GENERATED_DECORATOR_MASK;
    }
    if (xed_import_attribute_contains(record->attributes, S8("BROADCAST_ENABLED")))
    {
        form->decorator_flags |= XED_GENERATED_DECORATOR_BROADCAST;
    }
    if (xed_import_attribute_contains(record->attributes, S8("DISP8_NO_SCALE")) ||
        xed_import_attribute_contains(record->attributes, S8("DISP8_FULL")) ||
        xed_import_attribute_contains(record->attributes, S8("DISP8_FULLMEM")) ||
        xed_import_attribute_contains(record->attributes, S8("DISP8_SCALAR")) ||
        xed_import_attribute_contains(record->attributes, S8("DISP8_HALFMEM")))
    {
        form->field_flags |= XED_GENERATED_FIELD_DISPLACEMENT;
    }
    if (xed_import_attribute_contains(record->attributes, S8("APX_NDD")))
    {
        form->apx_flags |= XED_GENERATED_APX_NDD;
    }
    if (xed_import_attribute_contains(record->attributes, S8("APX_NF")))
    {
        form->apx_flags |= XED_GENERATED_APX_NF;
    }
    if (string_contains(record->extension, S8("APX")) || xed_import_attribute_contains(record->attributes, S8("APX_NDD")) ||
        xed_import_attribute_contains(record->attributes, S8("APX_NF")))
    {
        form->apx_flags |= XED_GENERATED_APX;
    }
    for (u32 index = 0; index < form->operand_count; index += 1)
    {
        if (string_starts_with_sequence(form->operands[index].atom, S8("TMM")))
        {
            form->encoder_family = XED_GENERATED_ENCODER_AMX;
        }
        if (string_starts_with_sequence(form->operands[index].atom, S8("GPR16")) ||
            (string_starts_with_sequence(form->operands[index].atom, S8("GPR64")) &&
             string_contains(form->operands[index].atom, S8("_N"))))
        {
            form->apx_flags |= XED_GENERATED_APX_EGPR;
        }
    }
    if (form->encoder_family == XED_GENERATED_ENCODER_AMX && (form->field_flags & XED_GENERATED_FIELD_MEMORY))
    {
        form->amx_flags |= XED_GENERATED_AMX_TILE_MEMORY;
    }
    if (string_starts_with_sequence(record->category, S8("SYSTEM")) || xed_import_attribute_contains(record->attributes, S8("RING0")))
    {
        form->encoder_family = XED_GENERATED_ENCODER_SYSTEM;
    }
    if (string_equal(record->cpl, S8("0")) || xed_import_attribute_contains(record->attributes, S8("RING0")))
    {
        form->coverage_class = XED_GENERATED_COVERAGE_PRIVILEGED;
        form->test_class = XED_GENERATED_TEST_PRIVILEGED_SCHEMA;
        form->reason_id = XED_GENERATED_REASON_CPL0;
    }
    else if (form->mode_flags & XED_GENERATED_MODE_NOT64)
    {
        form->coverage_class = XED_GENERATED_COVERAGE_NOT64;
        form->test_class = XED_GENERATED_TEST_NOT64_SCHEMA;
        form->reason_id = XED_GENERATED_REASON_MODE_NOT64;
    }
    else
    {
        form->coverage_class = XED_GENERATED_COVERAGE_NORMALIZED;
        form->reason_id = XED_GENERATED_REASON_NONE;
    }

    if (string_contains(record->pattern, S8("BRDISP8")))
    {
        form->displacement_width = 1;
    }
    else if (string_contains(record->pattern, S8("BRDISP64")))
    {
        form->displacement_width = 8;
    }
    else if (string_contains(record->pattern, S8("BRDISP32")) || string_contains(record->pattern, S8("BRDISPz")))
    {
        form->displacement_width = 4;
    }
    if (string_contains(record->pattern, S8("SIMM8")) || string_contains(record->pattern, S8("UIMM8")) ||
        string_contains(record->pattern, S8("SE_IMM8")))
    {
        form->immediate_width = 1;
    }
    else if (string_contains(record->pattern, S8("UIMM16")))
    {
        form->immediate_width = 2;
    }
    else if (string_contains(record->pattern, S8("UIMM32")) || string_contains(record->pattern, S8("SIMMz")))
    {
        form->immediate_width = 4;
    }
    form->immediate_signed = string_contains(record->pattern, S8("SIMM")) || string_contains(record->pattern, S8("SE_IMM"));
    form->displacement_scale = form->tuple_kind != XED_GENERATED_TUPLE_NONE;
    if (form->field_flags & XED_GENERATED_FIELD_RELATIVE)
    {
        form->relocation_base = 1;
    }
    BUSTER_UNUSED(raw_index);
    return true;
}

BUSTER_GLOBAL_LOCAL XedGeneratedFormList xed_import_normalize_records(Arena* arena, XedImportRecordList records, bool* valid,
                                                                        u64* failed_index, String8* bad_token, bool* bad_operand)
{
    XedGeneratedFormList result = {
        .pointer = arena_allocate(arena, XedGeneratedForm, records.count),
        .length = records.count,
    };
    u64 index = 0;
    for (XedImportRecord* record = records.first; record; record = record->next, index += 1)
    {
        if (!xed_import_normalize_record(result.pointer + index, record, index, bad_token, bad_operand))
        {
            XedGeneratedForm* form = result.pointer + index;
            if ((form->coverage_class == XED_GENERATED_COVERAGE_RESERVED && form->reason_id != XED_GENERATED_REASON_NONE) ||
                (form->coverage_class == XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN && form->reason_id != XED_GENERATED_REASON_NONE &&
                 form->unsupported_token.length))
            {
                continue;
            }
            *valid = false;
            *failed_index = index;
            return result;
        }
    }
    return result;
}

typedef struct XedGeneratedStringNode XedGeneratedStringNode;
struct XedGeneratedStringNode
{
    String8 string;
    u32 offset;
    XedGeneratedStringNode* next;
};

typedef struct XedGeneratedStringPool XedGeneratedStringPool;
struct XedGeneratedStringPool
{
    XedGeneratedStringNode* first;
    XedGeneratedStringNode* last;
    u32 count;
    u32 byte_count;
};

typedef struct XedGeneratedTableStats XedGeneratedTableStats;
struct XedGeneratedTableStats
{
    u64 coverage_counts[XED_GENERATED_COVERAGE_COUNT];
    u64 reason_counts[XED_GENERATED_REASON_COUNT];
    u64 encoder_counts[8];
    u64 test_counts[3];
    u64 operand_count;
    u64 token_count;
    u64 string_pool_bytes;
    u64 header_bytes;
    u64 coverage_bytes;
};

BUSTER_GLOBAL_LOCAL u32 xed_generated_string_intern(XedGeneratedStringPool* pool, Arena* arena, String8 string)
{
    if (!string.length)
    {
        return 0;
    }
    for (XedGeneratedStringNode* node = pool->first; node; node = node->next)
    {
        if (string_equal(node->string, string))
        {
            return node->offset;
        }
    }
    XedGeneratedStringNode* node = arena_allocate(arena, XedGeneratedStringNode, 1);
    *node = (XedGeneratedStringNode){.string = string_duplicate_arena(arena, string, false)};
    if (pool->last)
    {
        pool->last->next = node;
    }
    else
    {
        pool->first = node;
    }
    pool->last = node;
    pool->count += 1;
    return 0;
}

BUSTER_GLOBAL_LOCAL int xed_generated_string_node_compare(const void* left_pointer, const void* right_pointer)
{
    XedGeneratedStringNode* const* left = (XedGeneratedStringNode* const*)left_pointer;
    XedGeneratedStringNode* const* right = (XedGeneratedStringNode* const*)right_pointer;
    u64 count = BUSTER_MIN((*left)->string.length, (*right)->string.length);
    int result = count ? memcmp((*left)->string.pointer, (*right)->string.pointer, count) : 0;
    if (!result)
    {
        result = ((*left)->string.length > (*right)->string.length) - ((*left)->string.length < (*right)->string.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 xed_generated_string_pool_prepare(Arena* arena, XedGeneratedStringPool* pool,
                                                          XedGeneratedStringNode*** sorted_result)
{
    XedGeneratedStringNode** sorted = arena_allocate(arena, XedGeneratedStringNode*, pool->count);
    u32 count = 0;
    for (XedGeneratedStringNode* node = pool->first; node; node = node->next)
    {
        sorted[count++] = node;
    }
    qsort(sorted, count, sizeof(sorted[0]), xed_generated_string_node_compare);
    u32 offset = 1;
    for (u32 index = 0; index < count; index += 1)
    {
        sorted[index]->offset = offset;
        if (sorted[index]->string.length > UINT32_MAX - offset - 1)
        {
            return 0;
        }
        offset += (u32)sorted[index]->string.length + 1;
    }
    pool->byte_count = offset;
    *sorted_result = sorted;
    return offset;
}

BUSTER_GLOBAL_LOCAL u32 xed_generated_string_offset(XedGeneratedStringPool* pool, String8 string)
{
    if (!string.length)
    {
        return 0;
    }
    for (XedGeneratedStringNode* node = pool->first; node; node = node->next)
    {
        if (string_equal(node->string, string))
        {
            return node->offset;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void xed_generated_intern_record(XedGeneratedStringPool* pool, Arena* arena, XedImportRecord* record)
{
    String8 values[] = {
        record->source, record->iclass, record->iform, record->isa_set, record->category, record->extension,
        record->attributes, record->cpl, record->exceptions, record->flags, record->disasm, record->disasm_intel,
        record->disasm_attsv, record->real_opcode, record->uname, record->comment, record->version, record->pattern,
        record->operands, record->operand_annotation,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        xed_generated_string_intern(pool, arena, values[index]);
    }
}

BUSTER_GLOBAL_LOCAL void xed_generated_intern_forms(XedGeneratedStringPool* pool, Arena* arena, XedGeneratedFormList forms)
{
    for (u64 index = 0; index < forms.length; index += 1)
    {
        XedGeneratedForm* form = forms.pointer + index;
        xed_generated_intern_record(pool, arena, form->record);
        xed_generated_string_intern(pool, arena, form->tuple_rule);
        xed_generated_string_intern(pool, arena, form->element_size);
        xed_generated_string_intern(pool, arena, form->unsupported_token);
        for (u32 operand_index = 0; operand_index < form->operand_count; operand_index += 1)
        {
            xed_generated_string_intern(pool, arena, form->operands[operand_index].atom);
            xed_generated_string_intern(pool, arena, form->operands[operand_index].width);
        }
    }
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_bytes(Arena* output, const char8* pointer, u64 length)
{
    arena_append_char8(output, '"');
    for (u64 index = 0; index < length; index += 1)
    {
        u8 byte = (u8)pointer[index];
        if (byte == '"')
        {
            arena_append_string8(output, S8("\\\""));
        }
        else if (byte == '\\')
        {
            arena_append_string8(output, S8("\\\\"));
        }
        else if (byte >= 0x20 && byte < 0x7f)
        {
            arena_append_char8(output, (char8)byte);
        }
        else
        {
            arena_append_char8(output, '\\');
            arena_append_char8(output, (char8)('0' + ((byte >> 6) & 7)));
            arena_append_char8(output, (char8)('0' + ((byte >> 3) & 7)));
            arena_append_char8(output, (char8)('0' + (byte & 7)));
        }
    }
    arena_append_char8(output, '"');
    arena_append_char8(output, '\n');
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_decimal(Arena* output, u64 value)
{
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    if (length > 0)
    {
        arena_append_string8(output, (String8){.pointer = (char8*)buffer, .length = (u64)length});
    }
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_hex(Arena* output, u64 value, bool wide)
{
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), wide ? "0x%016llxULL" : "0x%llxU", (unsigned long long)value);
    if (length > 0)
    {
        arena_append_string8(output, (String8){.pointer = (char8*)buffer, .length = (u64)length});
    }
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_offset(Arena* output, XedGeneratedStringPool* pool, String8 string)
{
    xed_generated_append_decimal(output, xed_generated_string_offset(pool, string));
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_separator(Arena* output, bool* first)
{
    if (!*first)
    {
        arena_append_string8(output, S8(", "));
    }
    *first = false;
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_form(Arena* output, XedGeneratedStringPool* pool, XedGeneratedForm* form,
                                                    u32 operand_first)
{
    XedImportRecord* record = form->record;
    String8 record_values[] = {
        record->source, record->iclass, record->iform, record->isa_set, record->category, record->extension,
        record->attributes, record->cpl, record->exceptions, record->flags, record->disasm, record->disasm_intel,
        record->disasm_attsv, record->real_opcode, record->uname, record->comment, record->version, record->pattern,
        record->operands, record->operand_annotation,
    };
    bool first = true;
    arena_append_string8(output, S8("    {"));
    xed_generated_append_hex(output, form->stable_hash, true);
    first = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(record_values); index += 1)
    {
        xed_generated_append_separator(output, &first);
        xed_generated_append_offset(output, pool, record_values[index]);
    }
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, operand_first);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->operand_count);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->coverage_class);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->encoder_family);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->test_class);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->prefix_kind);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->map);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->fixed_byte_count);
    xed_generated_append_separator(output, &first);
    arena_append_string8(output, S8("{"));
    for (u32 index = 0; index < XED_GENERATED_MAX_FIXED_BYTES; index += 1)
    {
        if (index)
        {
            arena_append_string8(output, S8(", "));
        }
        xed_generated_append_decimal(output, form->fixed_bytes[index]);
    }
    arena_append_string8(output, S8("}"));
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->mandatory_prefix);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->field_flags);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->decorator_flags);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->apx_flags);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->amx_flags);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->mode_flags);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->displacement_width);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->displacement_scale);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->immediate_width);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->immediate_signed);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->relocation_base);
    xed_generated_append_separator(output, &first);
    arena_append_string8(output, S8("{0, 0, 0}"));
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->tuple_kind);
    xed_generated_append_separator(output, &first);
    xed_generated_append_offset(output, pool, form->tuple_rule);
    xed_generated_append_separator(output, &first);
    xed_generated_append_offset(output, pool, form->element_size);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->token_count);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, form->reason_id);
    xed_generated_append_separator(output, &first);
    xed_generated_append_offset(output, pool, form->unsupported_token);
    xed_generated_append_separator(output, &first);
    xed_generated_append_decimal(output, 0);
    arena_append_string8(output, S8("},\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_operand(Arena* output, XedGeneratedStringPool* pool, XedGeneratedOperand* operand)
{
    arena_append_string8(output, S8("    {"));
    xed_generated_append_offset(output, pool, operand->atom);
    arena_append_string8(output, S8(", "));
    xed_generated_append_offset(output, pool, operand->width);
    arena_append_string8(output, S8(", "));
    xed_generated_append_decimal(output, operand->slot);
    arena_append_string8(output, S8(", "));
    xed_generated_append_decimal(output, operand->visible);
    arena_append_string8(output, S8(", "));
    xed_generated_append_decimal(output, operand->kind);
    arena_append_string8(output, S8(", "));
    xed_generated_append_decimal(output, operand->access);
    arena_append_string8(output, S8(", "));
    xed_generated_append_decimal(output, operand->field_source);
    arena_append_string8(output, S8(", {0, 0, 0}},\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_string_pool(Arena* output, Arena* arena, XedGeneratedStringPool* pool,
                                                         XedGeneratedStringNode** sorted)
{
    u8* bytes = arena_allocate(arena, u8, pool->byte_count);
    memset(bytes, 0, pool->byte_count);
    u32 offset = 1;
    for (u32 index = 0; index < pool->count; index += 1)
    {
        XedGeneratedStringNode* node = sorted[index];
        memcpy(bytes + offset, node->string.pointer, node->string.length);
        offset += (u32)node->string.length + 1;
    }
    arena_append_string8(output, S8("static const char8 buster_x86_generated_string_pool[] =\n"));
    for (u32 start = 0; start < pool->byte_count; start += 64)
    {
        u32 length = BUSTER_MIN(64u, pool->byte_count - start);
        arena_append_string8(output, S8("    "));
        xed_generated_append_bytes(output, (char8*)bytes + start, length);
    }
    arena_append_string8(output, S8(";\n\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_preamble(Arena* output)
{
    arena_append_string8(output,
                         S8("/* Generated by build import_assembly_metadata; do not edit. */\n"
                            "#ifndef BUSTER_X86_64_ASSEMBLY_GENERATED_H\n"
                            "#define BUSTER_X86_64_ASSEMBLY_GENERATED_H\n"
                            "#include <buster/lib/base.h>\n\n"
                            "typedef enum BusterX86GeneratedCoverageClass {\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_DIRECT,\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_NORMALIZED,\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_NOT64,\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_PRIVILEGED,\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_RESERVED,\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_UNSUPPORTED_TOKEN,\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_UNCLASSIFIED,\n"
                            "    BUSTER_X86_GENERATED_COVERAGE_COUNT,\n"
                            "} BusterX86GeneratedCoverageClass;\n\n"
                            "typedef enum BusterX86GeneratedPrefixKind {\n"
                            "    BUSTER_X86_GENERATED_PREFIX_LEGACY,\n"
                            "    BUSTER_X86_GENERATED_PREFIX_REX,\n"
                            "    BUSTER_X86_GENERATED_PREFIX_REX2,\n"
                            "    BUSTER_X86_GENERATED_PREFIX_VEX,\n"
                            "    BUSTER_X86_GENERATED_PREFIX_XOP,\n"
                            "    BUSTER_X86_GENERATED_PREFIX_EVEX,\n"
                            "} BusterX86GeneratedPrefixKind;\n\n"
                            "typedef enum BusterX86GeneratedMap {\n"
                            "    BUSTER_X86_GENERATED_MAP_LEGACY,\n"
                            "    BUSTER_X86_GENERATED_MAP_0F,\n"
                            "    BUSTER_X86_GENERATED_MAP_0F38,\n"
                            "    BUSTER_X86_GENERATED_MAP_0F3A,\n"
                            "    BUSTER_X86_GENERATED_MAP_4,\n"
                            "    BUSTER_X86_GENERATED_MAP_5,\n"
                            "    BUSTER_X86_GENERATED_MAP_6,\n"
                            "    BUSTER_X86_GENERATED_MAP_7,\n"
                            "    BUSTER_X86_GENERATED_MAP_X8,\n"
                            "    BUSTER_X86_GENERATED_MAP_X9,\n"
                            "    BUSTER_X86_GENERATED_MAP_XA,\n"
                            "} BusterX86GeneratedMap;\n\n"
                            "enum {\n"
                            "    BUSTER_X86_GENERATED_FIELD_MODRM = 1u << 0,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SIB = 1u << 1,\n"
                            "    BUSTER_X86_GENERATED_FIELD_VSIB = 1u << 2,\n"
                            "    BUSTER_X86_GENERATED_FIELD_MEMORY = 1u << 3,\n"
                            "    BUSTER_X86_GENERATED_FIELD_REGISTER = 1u << 4,\n"
                            "    BUSTER_X86_GENERATED_FIELD_DISPLACEMENT = 1u << 5,\n"
                            "    BUSTER_X86_GENERATED_FIELD_IMMEDIATE = 1u << 6,\n"
                            "    BUSTER_X86_GENERATED_FIELD_RELATIVE = 1u << 7,\n"
                            "    BUSTER_X86_GENERATED_FIELD_FIELD_END = 1u << 8,\n"
                            "    BUSTER_X86_GENERATED_DECORATOR_MASK = 1u << 0,\n"
                            "    BUSTER_X86_GENERATED_DECORATOR_ZEROING = 1u << 1,\n"
                            "    BUSTER_X86_GENERATED_DECORATOR_BROADCAST = 1u << 2,\n"
                            "    BUSTER_X86_GENERATED_DECORATOR_ROUNDING = 1u << 3,\n"
                            "    BUSTER_X86_GENERATED_DECORATOR_SAE = 1u << 4,\n"
                            "    BUSTER_X86_GENERATED_APX = 1u << 0,\n"
                            "    BUSTER_X86_GENERATED_APX_ND = 1u << 1,\n"
                            "    BUSTER_X86_GENERATED_APX_NF = 1u << 2,\n"
                            "    BUSTER_X86_GENERATED_APX_NDD = 1u << 3,\n"
                            "    BUSTER_X86_GENERATED_APX_SCC = 1u << 4,\n"
                            "    BUSTER_X86_GENERATED_APX_EGPR = 1u << 5,\n"
                            "    BUSTER_X86_GENERATED_AMX_TILE_REGISTER = 1u << 0,\n"
                            "    BUSTER_X86_GENERATED_AMX_TILE_MEMORY = 1u << 1,\n"
                            "    BUSTER_X86_GENERATED_AMX_TILE_ROW = 1u << 2,\n"
                            "    BUSTER_X86_GENERATED_AMX_TILE_COLUMN = 1u << 3,\n"
                            "};\n\n"
                            "typedef enum BusterX86GeneratedTupleKind {\n"
                            "    BUSTER_X86_GENERATED_TUPLE_NONE,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_FULL,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_HALF,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_QUARTER,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_EIGHTH,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_SCALAR,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_TUPLE1,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_TUPLE1_4X,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_TUPLE1_BYTE,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_TUPLE1_WORD,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_TUPLE2,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_TUPLE4,\n"
                            "    BUSTER_X86_GENERATED_TUPLE_TUPLE8,\n"
                            "} BusterX86GeneratedTupleKind;\n\n"
                            "enum {\n"
                            "    BUSTER_X86_GENERATED_MODE_16 = 1u << 0,\n"
                            "    BUSTER_X86_GENERATED_MODE_32 = 1u << 1,\n"
                            "    BUSTER_X86_GENERATED_MODE_64 = 1u << 2,\n"
                            "    BUSTER_X86_GENERATED_MODE_NOT64 = 1u << 3,\n"
                            "    BUSTER_X86_GENERATED_MODE_EA16 = 1u << 4,\n"
                            "    BUSTER_X86_GENERATED_MODE_EA32 = 1u << 5,\n"
                            "    BUSTER_X86_GENERATED_MODE_EA64 = 1u << 6,\n"
                            "    BUSTER_X86_GENERATED_MODE_EANOT16 = 1u << 7,\n"
                            "};\n\n"
                            "enum {\n"
                            "    BUSTER_X86_GENERATED_ENCODER_LEGACY,\n"
                            "    BUSTER_X86_GENERATED_ENCODER_REX,\n"
                            "    BUSTER_X86_GENERATED_ENCODER_REX2,\n"
                            "    BUSTER_X86_GENERATED_ENCODER_VEX,\n"
                            "    BUSTER_X86_GENERATED_ENCODER_XOP,\n"
                            "    BUSTER_X86_GENERATED_ENCODER_EVEX,\n"
                            "    BUSTER_X86_GENERATED_ENCODER_AMX,\n"
                            "    BUSTER_X86_GENERATED_ENCODER_SYSTEM,\n"
                            "};\n\n"
                            "enum {\n"
                            "    BUSTER_X86_GENERATED_REASON_NONE,\n"
                            "    BUSTER_X86_GENERATED_REASON_MODE_NOT64,\n"
                            "    BUSTER_X86_GENERATED_REASON_CPL0,\n"
                            "    BUSTER_X86_GENERATED_REASON_UNKNOWN_PATTERN_TOKEN,\n"
                            "    BUSTER_X86_GENERATED_REASON_UNKNOWN_OPERAND_TOKEN,\n"
                            "};\n\n"
                            "enum {\n"
                            "    BUSTER_X86_GENERATED_OPERAND_NONE,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_REGISTER,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_MEMORY,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_IMMEDIATE,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_RELATIVE,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_ABSOLUTE,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_BASE,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_SEGMENT,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_ADDRESS_GENERATOR,\n"
                            "    BUSTER_X86_GENERATED_OPERAND_PSEUDO,\n"
                            "};\n"
                            "enum {\n"
                            "    BUSTER_X86_GENERATED_ACCESS_READ = 1u << 0,\n"
                            "    BUSTER_X86_GENERATED_ACCESS_WRITE = 1u << 1,\n"
                            "    BUSTER_X86_GENERATED_ACCESS_COND = 1u << 2,\n"
                            "    BUSTER_X86_GENERATED_ACCESS_SUPPRESSED = 1u << 3,\n"
                            "    BUSTER_X86_GENERATED_ACCESS_IMPLICIT = 1u << 4,\n"
                            "};\n"
                            "typedef enum BusterX86GeneratedFieldSource {\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_NONE,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_REG,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_RM,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_VVVV,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_MASK,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_FIXED,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_IMMEDIATE,\n"
                            "    BUSTER_X86_GENERATED_FIELD_SOURCE_RELATIVE,\n"
                            "} BusterX86GeneratedFieldSource;\n"
                            "enum {\n"
                            "    BUSTER_X86_GENERATED_TEST_SCHEMA,\n"
                            "    BUSTER_X86_GENERATED_TEST_PRIVILEGED_SCHEMA,\n"
                            "    BUSTER_X86_GENERATED_TEST_NOT64_SCHEMA,\n"
                            "};\n\n"
                            "typedef struct BusterX86GeneratedOperand BusterX86GeneratedOperand;\n"
                            "struct BusterX86GeneratedOperand {\n"
                            "    u32 atom_offset;\n"
                            "    u32 width_offset;\n"
                            "    u8 slot;\n"
                            "    u8 visible;\n"
                            "    u8 kind;\n"
                            "    u8 access;\n"
                            "    u8 field_source;\n"
                            "    u8 reserved[3];\n"
                            "};\n\n"
                            "typedef struct BusterX86GeneratedForm BusterX86GeneratedForm;\n"
                            "struct BusterX86GeneratedForm {\n"
                            "    u64 stable_hash;\n"
                            "    u32 source_offset;\n"
                            "    u32 iclass_offset;\n"
                            "    u32 iform_offset;\n"
                            "    u32 isa_set_offset;\n"
                            "    u32 category_offset;\n"
                            "    u32 extension_offset;\n"
                            "    u32 attributes_offset;\n"
                            "    u32 cpl_offset;\n"
                            "    u32 exceptions_offset;\n"
                            "    u32 flags_offset;\n"
                            "    u32 disasm_offset;\n"
                            "    u32 disasm_intel_offset;\n"
                            "    u32 disasm_attsv_offset;\n"
                            "    u32 real_opcode_offset;\n"
                            "    u32 uname_offset;\n"
                            "    u32 comment_offset;\n"
                            "    u32 version_offset;\n"
                            "    u32 pattern_offset;\n"
                            "    u32 operands_offset;\n"
                            "    u32 operand_annotation_offset;\n"
                            "    u32 operand_first;\n"
                            "    u16 operand_count;\n"
                            "    u8 coverage_class;\n"
                            "    u8 encoder_family;\n"
                            "    u8 test_class;\n"
                            "    u8 prefix_kind;\n"
                            "    u8 map;\n"
                            "    u8 fixed_byte_count;\n"
                            "    u8 fixed_bytes[16];\n"
                            "    u8 mandatory_prefix;\n"
                            "    u16 field_flags;\n"
                            "    u16 decorator_flags;\n"
                            "    u16 apx_flags;\n"
                            "    u16 amx_flags;\n"
                            "    u16 mode_flags;\n"
                            "    u8 displacement_width;\n"
                            "    u8 displacement_scale;\n"
                            "    u8 immediate_width;\n"
                            "    u8 immediate_signed;\n"
                            "    u8 relocation_base;\n"
                            "    u8 reserved[3];\n"
                            "    u8 tuple_kind;\n"
                            "    u32 tuple_offset;\n"
                            "    u32 element_size_offset;\n"
                            "    u32 token_count;\n"
                            "    u16 reason_id;\n"
                            "    u32 reason_offset;\n"
                            "    u16 reserved2;\n"
                            "};\n\n"
                            "typedef struct BusterX86GeneratedCoverage BusterX86GeneratedCoverage;\n"
                            "struct BusterX86GeneratedCoverage {\n"
                            "    u64 source_hash;\n"
                            "    u32 source_offset;\n"
                            "    u32 normalized_form_id;\n"
                            "    u8 coverage_class;\n"
                            "    u8 encoder_family;\n"
                            "    u8 test_class;\n"
                            "    u16 reason_id;\n"
                            "    u32 reason_offset;\n"
                            "};\n\n"
                            "#define BUSTER_X86_GENERATED_SCHEMA_VERSION 2\n\n"));
}

BUSTER_GLOBAL_LOCAL bool xed_import_emit_generated_tables_packed(Arena* output, Arena* coverage_output, Arena* arena,
                                                                  XedGeneratedFormList forms, XedGeneratedTableStats* stats);

BUSTER_GLOBAL_LOCAL bool json_raw_array_next(JsonParser* parser, String8* value, bool* valid)
{
    json_skip_whitespace(parser);
    if (json_consume(parser, ']'))
    {
        return false;
    }
    *value = json_raw_value(parser, valid);
    if (!*valid)
    {
        return false;
    }
    json_skip_whitespace(parser);
    if (json_consume(parser, ','))
    {
        return true;
    }
    if (parser->index < parser->text.length && parser->text.pointer[parser->index] == ']')
    {
        return true;
    }
    *valid = false;
    return false;
}

BUSTER_GLOBAL_LOCAL int assembly_import_string_compare(const void* left_pointer, const void* right_pointer)
{
    String8 const* left = (String8 const*)left_pointer;
    String8 const* right = (String8 const*)right_pointer;
    u64 count = BUSTER_MIN(left->length, right->length);
    int result = count ? memcmp(left->pointer, right->pointer, count) : 0;
    if (!result)
    {
        result = (left->length > right->length) - (left->length < right->length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_string_find(SliceString8 strings, String8 wanted)
{
    u64 low = 0;
    u64 high = strings.length;
    while (low < high)
    {
        u64 middle = low + (high - low) / 2;
        String8 candidate = strings.pointer[middle];
        int comparison = assembly_import_string_compare(&candidate, &wanted);
        if (comparison < 0)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < strings.length && string_equal(strings.pointer[low], wanted);
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_alias_source_present(String8 json)
{
    String8 instances = {0};
    if (!json_raw_object_find(json, S8("!instanceof"), &instances))
    {
        return false;
    }
    String8 aliases = {0};
    return json_raw_object_find(instances, S8("AArch64InstAlias"), &aliases) ||
           json_raw_object_find(instances, S8("AArch64InstAliases"), &aliases) ||
           json_raw_object_find(instances, S8("InstAlias"), &aliases);
}

BUSTER_GLOBAL_LOCAL String8 aarch64_import_alias_source_diagnostic(bool audit)
{
    return audit ? S8("audit: AArch64 alias records are present in the raw source but are not imported by this reduced schema path; generated artifacts remain blocked\n") :
                   S8("error: AArch64 alias records are present in the raw source but are not imported by this reduced schema path\n");
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_alias_source_is_fatal(bool audit)
{
    return !audit;
}

BUSTER_GLOBAL_LOCAL SliceString8 aarch64_import_instruction_names(Arena* arena, String8 json, bool* valid)
{
    SliceString8 result = {0};
    String8 instances = {0};
    String8 names_array = {0};
    if (!json_raw_object_find(json, S8("!instanceof"), &instances))
    {
        return result;
    }
    if (!json_raw_object_find(instances, S8("AArch64Inst"), &names_array))
    {
        *valid = false;
        return result;
    }

    JsonParser parser = {.text = names_array};
    *valid = json_consume(&parser, '[');
    String8List names = {0};
    while (*valid)
    {
        String8 raw = {0};
        if (!json_raw_array_next(&parser, &raw, valid))
        {
            break;
        }
        String8 name = json_raw_key_text(raw);
        if (string_first_code_unit(name, '\\') != BUSTER_STRING_NO_MATCH)
        {
            *valid = false;
            break;
        }
        string8_list_push(arena, &names, name);
    }
    if (*valid)
    {
        result = string8_list_to_slice(arena, names);
        qsort(result.pointer, result.length, sizeof(result.pointer[0]), assembly_import_string_compare);
    }
    return result;
}

typedef struct Aarch64ImportFields Aarch64ImportFields;
struct Aarch64ImportFields
{
    String8 pseudo;
    String8 inst;
    String8 assembly;
    String8 output_operands;
    String8 input_operands;
    String8 predicates;
};

typedef struct Aarch64ImportRootObject Aarch64ImportRootObject;
struct Aarch64ImportRootObject
{
    String8 name;
    String8 object;
};

typedef struct Aarch64ImportReducedObject Aarch64ImportReducedObject;
struct Aarch64ImportReducedObject
{
    String8 name;
    Aarch64ImportFields fields;
};

BUSTER_GLOBAL_LOCAL int aarch64_import_root_object_compare(const void* left_pointer, const void* right_pointer)
{
    Aarch64ImportRootObject const* left = (Aarch64ImportRootObject const*)left_pointer;
    Aarch64ImportRootObject const* right = (Aarch64ImportRootObject const*)right_pointer;
    return assembly_import_string_compare(&left->name, &right->name);
}

BUSTER_GLOBAL_LOCAL int aarch64_import_reduced_object_compare(const void* left_pointer, const void* right_pointer)
{
    Aarch64ImportReducedObject const* left = (Aarch64ImportReducedObject const*)left_pointer;
    Aarch64ImportReducedObject const* right = (Aarch64ImportReducedObject const*)right_pointer;
    return assembly_import_string_compare(&left->name, &right->name);
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_emit_predicates(Arena* output, String8 predicates);

BUSTER_GLOBAL_LOCAL bool aarch64_import_emit_record(Arena* output, String8 raw_name, Aarch64ImportFields fields,
                                                     bool has_pseudo)
{
    if (!fields.inst.length || !fields.assembly.length || !fields.output_operands.length || !fields.input_operands.length ||
        !fields.predicates.length || (has_pseudo && !fields.pseudo.length))
    {
        return false;
    }
    if (has_pseudo && !string_equal(fields.pseudo, S8("0")))
    {
        return true;
    }

    arena_append_string8(output, S8("{\"name\":"));
    arena_append_string8(output, raw_name);
    arena_append_string8(output, S8(",\"inst\":"));
    arena_append_string8(output, fields.inst);
    arena_append_string8(output, S8(",\"asm\":"));
    arena_append_string8(output, fields.assembly);
    arena_append_string8(output, S8(",\"out\":"));
    arena_append_string8(output, fields.output_operands);
    arena_append_string8(output, S8(",\"in\":"));
    arena_append_string8(output, fields.input_operands);
    arena_append_string8(output, S8(",\"predicates\":"));
    if (!aarch64_import_emit_predicates(output, fields.predicates))
    {
        return false;
    }
    arena_append_string8(output, S8("}\n"));
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_fields(String8 object, Aarch64ImportFields* fields)
{
    JsonParser parser = {.text = object};
    bool valid = json_consume(&parser, '{');
    while (valid)
    {
        String8 raw_key = {0};
        String8 value = {0};
        if (!json_raw_object_next(&parser, &raw_key, &value, &valid))
        {
            break;
        }
        String8 key = json_raw_key_text(raw_key);
        if (string_equal(key, S8("isPseudo")))
        {
            fields->pseudo = value;
        }
        else if (string_equal(key, S8("Inst")))
        {
            fields->inst = value;
        }
        else if (string_equal(key, S8("AsmString")))
        {
            fields->assembly = value;
        }
        else if (string_equal(key, S8("OutOperandList")))
        {
            json_raw_object_find(value, S8("printable"), &fields->output_operands);
        }
        else if (string_equal(key, S8("InOperandList")))
        {
            json_raw_object_find(value, S8("printable"), &fields->input_operands);
        }
        else if (string_equal(key, S8("Predicates")))
        {
            fields->predicates = value;
        }
    }
    return valid && fields->pseudo.length && fields->inst.length && fields->assembly.length && fields->output_operands.length && fields->input_operands.length &&
           fields->predicates.length;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_emit_predicates(Arena* output, String8 predicates)
{
    JsonParser parser = {.text = predicates};
    bool valid = json_consume(&parser, '[');
    bool first = true;
    arena_append_char8(output, '[');
    while (valid)
    {
        String8 predicate = {0};
        if (!json_raw_array_next(&parser, &predicate, &valid))
        {
            break;
        }
        String8 definition = {0};
        if (json_raw_object_find(predicate, S8("def"), &definition))
        {
            if (!first)
            {
                arena_append_char8(output, ',');
            }
            first = false;
            arena_append_string8(output, definition);
        }
        else if (predicate.length >= 2 && predicate.pointer[0] == '"' && predicate.pointer[predicate.length - 1] == '"')
        {
            if (!first)
            {
                arena_append_char8(output, ',');
            }
            first = false;
            arena_append_string8(output, predicate);
        }
        else
        {
            return false;
        }
    }
    arena_append_char8(output, ']');
    return valid;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_emit(Arena* arena, Arena* output, String8 json, u64* imported_count)
{
    bool valid = true;
    SliceString8 instruction_names = aarch64_import_instruction_names(arena, json, &valid);
    if (!valid)
    {
        return false;
    }

    // The checked-in audit file is also accepted as an importer input. This is
    // useful for deterministic regeneration tests and keeps the canonical
    // reduced JSONL independently consumable without llvm-tblgen.
    if (!instruction_names.length)
    {
        u64 reduced_line_count = 0;
        u64 start = 0;
        while (start < json.length)
        {
            u64 end = start;
            while (end < json.length && json.pointer[end] != '\n')
            {
                end += 1;
            }
            if (assembly_import_trim(string_slice(json, start, end)).length)
            {
                reduced_line_count += 1;
            }
            start = end + (end < json.length);
        }
        if (!reduced_line_count)
        {
            *imported_count = 0;
            return false;
        }
        Aarch64ImportReducedObject* selected = arena_allocate(arena, Aarch64ImportReducedObject, reduced_line_count);
        u64 selected_count = 0;
        start = 0;
        while (start < json.length)
        {
            u64 end = start;
            while (end < json.length && json.pointer[end] != '\n')
            {
                end += 1;
            }
            String8 line = assembly_import_trim(string_slice(json, start, end));
            if (line.length)
            {
                JsonParser line_parser = {.text = line};
                bool line_valid = json_consume(&line_parser, '{');
                String8 raw_name = {0};
                Aarch64ImportFields fields = {0};
                while (line_valid)
                {
                    String8 raw_key = {0};
                    String8 value = {0};
                    if (!json_raw_object_next(&line_parser, &raw_key, &value, &line_valid))
                    {
                        break;
                    }
                    String8 key = json_raw_key_text(raw_key);
                    if (string_equal(key, S8("name")))
                    {
                        raw_name = value;
                    }
                    else if (string_equal(key, S8("inst")))
                    {
                        fields.inst = value;
                    }
                    else if (string_equal(key, S8("asm")))
                    {
                        fields.assembly = value;
                    }
                    else if (string_equal(key, S8("out")))
                    {
                        fields.output_operands = value;
                    }
                    else if (string_equal(key, S8("in")))
                    {
                        fields.input_operands = value;
                    }
                    else if (string_equal(key, S8("predicates")))
                    {
                        fields.predicates = value;
                    }
                }
                json_skip_whitespace(&line_parser);
                line_valid = line_valid && line_parser.index == line_parser.text.length && raw_name.length;
                if (!line_valid || selected_count >= reduced_line_count)
                {
                    return false;
                }
                selected[selected_count++] = (Aarch64ImportReducedObject){.name = raw_name, .fields = fields};
            }
            start = end + (end < json.length);
        }
        if (selected_count != reduced_line_count)
        {
            return false;
        }
        if (selected_count > 1)
        {
            qsort(selected, selected_count, sizeof(selected[0]), aarch64_import_reduced_object_compare);
        }
        for (u64 index = 0; index < selected_count; index += 1)
        {
            if (!aarch64_import_emit_record(output, selected[index].name, selected[index].fields, false))
            {
                return false;
            }
        }
        *imported_count = selected_count;
        return true;
    }
    JsonParser parser = {.text = json};
    valid = json_consume(&parser, '{');
    Aarch64ImportRootObject* selected = arena_allocate(arena, Aarch64ImportRootObject, instruction_names.length);
    u64 selected_count = 0;
    while (valid)
    {
        String8 raw_name = {0};
        String8 object = {0};
        if (!json_raw_object_next(&parser, &raw_name, &object, &valid))
        {
            break;
        }
        String8 name = json_raw_key_text(raw_name);
        if (assembly_import_string_find(instruction_names, name))
        {
            if (selected_count >= instruction_names.length)
            {
                valid = false;
                break;
            }
            selected[selected_count++] = (Aarch64ImportRootObject){.name = raw_name, .object = object};
        }
    }
    json_skip_whitespace(&parser);
    valid = valid && parser.index == parser.text.length;
    if (valid && selected_count != instruction_names.length)
    {
        valid = false;
    }
    if (valid && selected_count > 1)
    {
        qsort(selected, selected_count, sizeof(selected[0]), aarch64_import_root_object_compare);
    }
    u64 count = 0;
    for (u64 index = 0; valid && index < selected_count; index += 1)
    {
        Aarch64ImportFields fields = {0};
        if (!aarch64_import_fields(selected[index].object, &fields))
        {
            valid = false;
            break;
        }
        u64 before = output->position;
        if (!aarch64_import_emit_record(output, selected[index].name, fields, true))
        {
            valid = false;
            break;
        }
        count += output->position != before;
    }
    *imported_count = count;
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 assembly_import_arena_contents(Arena* arena)
{
    return (String8){.pointer = (char8*)arena_buffer_start(arena), .length = arena_buffer_size(arena)};
}

BUSTER_GLOBAL_LOCAL String8 assembly_import_checksum(Arena* arena, String8 bytes)
{
    u64 checksum = buster_hash_64(bytes.pointer ? (u8*)bytes.pointer : (u8*)"", bytes.length);
    return string_format(arena, S8("{u64:x,width=[0,16],no_prefix}"), checksum);
}

BUSTER_GLOBAL_LOCAL u64 assembly_import_line_count(String8 text)
{
    if (!text.length)
    {
        return 0;
    }
    u64 count = 0;
    for (u64 index = 0; index < text.length; index += 1)
    {
        count += text.pointer[index] == '\n';
    }
    return text.pointer[text.length - 1] == '\n' ? count : count + 1;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_jsonl_unique_field(Arena* arena, String8 jsonl, String8 key, u64* unique_count, u64* missing_count)
{
    u64 line_count = assembly_import_line_count(jsonl);
    String8* values = line_count ? arena_allocate(arena, String8, line_count) : 0;
    u64 value_count = 0;
    u64 missing = 0;
    u64 start = 0;
    while (start < jsonl.length)
    {
        u64 end = start;
        while (end < jsonl.length && jsonl.pointer[end] != '\n')
        {
            end += 1;
        }
        String8 line = assembly_import_trim(string_slice(jsonl, start, end));
        if (line.length)
        {
            String8 raw = {0};
            if (json_raw_object_find(line, key, &raw))
            {
                if (value_count >= line_count)
                {
                    return false;
                }
                values[value_count++] = raw;
            }
            else
            {
                missing += 1;
            }
        }
        start = end + (end < jsonl.length);
    }
    if (value_count > 1)
    {
        qsort(values, value_count, sizeof(values[0]), assembly_import_string_compare);
    }
    u64 unique = 0;
    for (u64 index = 0; index < value_count; index += 1)
    {
        if (!index || !string_equal(values[index - 1], values[index]))
        {
            unique += 1;
        }
    }
    *unique_count = unique;
    if (missing_count)
    {
        *missing_count = missing;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_validate_artifact(Arena* arena, String8 bytes, u64 expected_bytes, String8 expected_checksum)
{
    return bytes.length == expected_bytes && string_equal(assembly_import_checksum(arena, bytes), expected_checksum);
}

// Keep these checked-in anchors independent of the generated manifest: the manifest is derived from the artifacts.
BUSTER_GLOBAL_LOCAL bool assembly_import_validate_checked_in_x86(Arena* arena, String8* artifacts)
{
    bool artifact_checks = assembly_import_validate_artifact(arena, artifacts[0], 4660881, S8("ba80aa3be0eb2e6f")) &&
                           assembly_import_validate_artifact(arena, artifacts[1], 6092260, S8("e6ef83ed59b0ddf3")) &&
                           assembly_import_validate_artifact(arena, artifacts[2], 392002, S8("78ecc6cfb575213a"));
    bool invariant_checks = assembly_import_line_count(artifacts[0]) == 11013 &&
                            string_contains(artifacts[1], S8("#define BUSTER_X86_GENERATED_FORM_COUNT 11013")) &&
                            string_contains(artifacts[1], S8("#define BUSTER_X86_GENERATED_COVERAGE_COUNT 11013"));
    bool valid = artifact_checks && invariant_checks;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_validate_checked_in_x86_mutation_self_test(Arena* arena, String8* artifacts)
{
    if (!artifacts[0].pointer || !artifacts[0].length)
    {
        return false;
    }
    char8* mutated_bytes = arena_allocate(arena, char8, artifacts[0].length);
    memcpy(mutated_bytes, artifacts[0].pointer, artifacts[0].length);
    mutated_bytes[artifacts[0].length / 2] ^= 1;
    String8 mutated_artifacts[] = {
        {.pointer = mutated_bytes, .length = artifacts[0].length},
        artifacts[1],
        artifacts[2],
    };
    return !assembly_import_validate_checked_in_x86(arena, mutated_artifacts);
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_reduced_provenance(Arena* arena, String8 input, u64 count)
{
    return count == 7491 && string_equal(assembly_import_checksum(arena, input), S8("f2e553abd71696e5"));
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_full_provenance(Arena* arena, String8 input, u64 count)
{
    return count == 7491 && string_equal(assembly_import_checksum(arena, input), S8("4c3cec3a88d0c821"));
}

// --- AArch64 metadata normalization -------------------------------------
//
// The llvm-tblgen JSON is deliberately kept out of normal builds.  The
// importer below turns each reduced JSONL record into a compact, pointer-free
// schema.  It records the complete encoding grammar even when a later emitter
// still needs to learn how to consume a particular operand family.

typedef enum Aarch64ImportCoverageClass
{
    AARCH64_IMPORT_COVERAGE_DIRECT,
    AARCH64_IMPORT_COVERAGE_NORMALIZED,
    AARCH64_IMPORT_COVERAGE_ALIAS,
    AARCH64_IMPORT_COVERAGE_PRIVILEGED_SYSTEM,
    AARCH64_IMPORT_COVERAGE_RESERVED_UNENCODABLE,
    AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN,
    AARCH64_IMPORT_COVERAGE_UNCLASSIFIED,
    AARCH64_IMPORT_COVERAGE_COUNT,
} Aarch64ImportCoverageClass;

typedef enum Aarch64ImportEncoderFamily
{
    AARCH64_IMPORT_ENCODER_SCALAR_INTEGER,
    AARCH64_IMPORT_ENCODER_BRANCH,
    AARCH64_IMPORT_ENCODER_LOAD_STORE,
    AARCH64_IMPORT_ENCODER_FP_SIMD_NEON,
    AARCH64_IMPORT_ENCODER_SVE_SVE2,
    AARCH64_IMPORT_ENCODER_SME_SME2,
    AARCH64_IMPORT_ENCODER_MTE,
    AARCH64_IMPORT_ENCODER_ATOMIC_LSE,
    AARCH64_IMPORT_ENCODER_CRYPTO,
    AARCH64_IMPORT_ENCODER_SYSTEM,
    AARCH64_IMPORT_ENCODER_COUNT,
} Aarch64ImportEncoderFamily;

typedef enum Aarch64ImportTestClass
{
    AARCH64_IMPORT_TEST_SCALAR,
    AARCH64_IMPORT_TEST_BRANCH,
    AARCH64_IMPORT_TEST_MEMORY,
    AARCH64_IMPORT_TEST_SIMD_LIST_LANE,
    AARCH64_IMPORT_TEST_SVE_PREDICATE,
    AARCH64_IMPORT_TEST_SME_SYSTEM,
    AARCH64_IMPORT_TEST_IMMEDIATE,
    AARCH64_IMPORT_TEST_SCHEMA,
    AARCH64_IMPORT_TEST_COUNT,
} Aarch64ImportTestClass;

typedef enum Aarch64ImportReason
{
    AARCH64_IMPORT_REASON_NONE,
    AARCH64_IMPORT_REASON_ALIAS_OF_CANONICAL,
    AARCH64_IMPORT_REASON_SYSTEM_OR_PRIVILEGED,
    AARCH64_IMPORT_REASON_UNMAPPED_VARIABLE,
    AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT,
    AARCH64_IMPORT_REASON_MALFORMED_DAG,
    AARCH64_IMPORT_REASON_MALFORMED_TEMPLATE,
    AARCH64_IMPORT_REASON_UNKNOWN_FIELD,
    AARCH64_IMPORT_REASON_UNKNOWN_PREDICATE,
    AARCH64_IMPORT_REASON_MISSING_OPERAND,
    AARCH64_IMPORT_REASON_INVALID_JSON,
    AARCH64_IMPORT_REASON_NULL_FIELD,
    AARCH64_IMPORT_REASON_UNPROVEN_FIELD_SEMANTICS,
    AARCH64_IMPORT_REASON_UNPROVEN_OPERAND_KIND,
    AARCH64_IMPORT_REASON_UNPROVEN_IMMEDIATE_RANGE,
    AARCH64_IMPORT_REASON_UNPROVEN_MEMORY_FORM,
    AARCH64_IMPORT_REASON_UNPROVEN_TIED_OPERAND,
    AARCH64_IMPORT_REASON_UNPROVEN_CORRESPONDENCE,
    AARCH64_IMPORT_REASON_UNSUPPORTED_ADDRESS_GRAMMAR,
    AARCH64_IMPORT_REASON_COUNT,
} Aarch64ImportReason;

typedef enum Aarch64ImportFieldTransform
{
    AARCH64_IMPORT_TRANSFORM_NONE,
    AARCH64_IMPORT_TRANSFORM_REGISTER,
    AARCH64_IMPORT_TRANSFORM_IMMEDIATE,
    AARCH64_IMPORT_TRANSFORM_CONDITION,
    AARCH64_IMPORT_TRANSFORM_SHIFT_EXTEND,
    AARCH64_IMPORT_TRANSFORM_LANE,
    AARCH64_IMPORT_TRANSFORM_PC_RELATIVE,
    AARCH64_IMPORT_TRANSFORM_SYSTEM,
} Aarch64ImportFieldTransform;

typedef enum Aarch64ImportRelocationKind
{
    AARCH64_IMPORT_RELOC_NONE,
    AARCH64_IMPORT_RELOC_BRANCH26,
    AARCH64_IMPORT_RELOC_COND_BRANCH19,
    AARCH64_IMPORT_RELOC_TEST_BRANCH14,
    AARCH64_IMPORT_RELOC_LITERAL19,
    AARCH64_IMPORT_RELOC_ADR21,
    AARCH64_IMPORT_RELOC_ADRP21,
} Aarch64ImportRelocationKind;

typedef enum Aarch64ImportOperandKind
{
    AARCH64_IMPORT_OPERAND_REGISTER,
    AARCH64_IMPORT_OPERAND_MEMORY,
    AARCH64_IMPORT_OPERAND_IMMEDIATE,
    AARCH64_IMPORT_OPERAND_RELATIVE,
    AARCH64_IMPORT_OPERAND_COMPOUND,
    AARCH64_IMPORT_OPERAND_SYSTEM,
    AARCH64_IMPORT_OPERAND_LIST,
    AARCH64_IMPORT_OPERAND_UNPROVEN,
} Aarch64ImportOperandKind;

typedef enum Aarch64ImportAddressKind
{
    AARCH64_IMPORT_ADDRESS_NONE,
    AARCH64_IMPORT_ADDRESS_BASE,
    AARCH64_IMPORT_ADDRESS_BASE_OFFSET,
    AARCH64_IMPORT_ADDRESS_BASE_INDEX,
    AARCH64_IMPORT_ADDRESS_LITERAL,
    AARCH64_IMPORT_ADDRESS_BRANCH,
    AARCH64_IMPORT_ADDRESS_SYSTEM,
    AARCH64_IMPORT_ADDRESS_UNPROVEN,
    AARCH64_IMPORT_ADDRESS_COUNT,
} Aarch64ImportAddressKind;

enum
{
    AARCH64_IMPORT_OPERAND_READ = 1u << 0,
    AARCH64_IMPORT_OPERAND_WRITE = 1u << 1,
    AARCH64_IMPORT_OPERAND_TIED = 1u << 2,
    AARCH64_IMPORT_OPERAND_SUPPRESSED = 1u << 3,
    AARCH64_IMPORT_OPERAND_IMPLICIT = 1u << 4,
    AARCH64_IMPORT_OPERAND_OPTIONAL = 1u << 5,
    AARCH64_IMPORT_OPERAND_FLAG_LIST = 1u << 6,
    AARCH64_IMPORT_OPERAND_LANE = 1u << 7,
};

enum
{
    AARCH64_IMPORT_OPERAND_IMMEDIATE_RANGE_EXACT = 1u << 0,
    AARCH64_IMPORT_OPERAND_IMMEDIATE_SIGNED = 1u << 1,
    AARCH64_IMPORT_OPERAND_SCALE_EXACT = 1u << 2,
    AARCH64_IMPORT_OPERAND_ADDRESS_EXACT = 1u << 3,
    AARCH64_IMPORT_OPERAND_REGISTER_WIDTH_EXACT = 1u << 4,
};

enum
{
    AARCH64_IMPORT_FIELD_DESTINATION = 1u << 0,
    AARCH64_IMPORT_FIELD_SOURCE = 1u << 1,
    AARCH64_IMPORT_FIELD_IMMEDIATE = 1u << 2,
    AARCH64_IMPORT_FIELD_PC_RELATIVE = 1u << 3,
    AARCH64_IMPORT_FIELD_UNMAPPED = 1u << 4,
};

typedef struct Aarch64ImportBit Aarch64ImportBit;
struct Aarch64ImportBit
{
    u8 instruction_bit;
    u8 value_bit;
    Aarch64ImportBit* next;
};

typedef struct Aarch64ImportVariable Aarch64ImportVariable;
struct Aarch64ImportVariable
{
    String8 name;
    Aarch64ImportBit* first_bit;
    Aarch64ImportBit* last_bit;
    u32 bit_count;
    u32 source_mask;
    u8 width;
    u8 unmapped;
    u8 transform;
    u8 relocation;
    u8 relocation_end;
    u8 shift;
    u8 flags;
    Aarch64ImportVariable* next;
};

typedef struct Aarch64ImportOperand Aarch64ImportOperand;
struct Aarch64ImportOperand
{
    String8 syntax;
    String8 type;
    String8 name;
    u32 field_index;
    s32 immediate_min;
    s32 immediate_max;
    u16 register_width;
    u8 direction;
    u8 kind;
    u8 flags;
    u8 scale;
    u8 seen_in_asm;
    u32 tied_to;
    u8 immediate_flags;
    u8 address_kind;
    u8 address_flags;
    u8 semantic_unproven;
    u16 address_base_index;
    u16 address_offset_index;
    Aarch64ImportOperand* next;
};

typedef struct Aarch64ImportPredicate Aarch64ImportPredicate;
struct Aarch64ImportPredicate
{
    String8 name;
    Aarch64ImportPredicate* next;
};

typedef struct Aarch64ImportRecord Aarch64ImportRecord;
struct Aarch64ImportRecord
{
    String8 name;
    String8 inst_json;
    String8 asm_json;
    String8 out_json;
    String8 in_json;
    String8 predicates_json;
    String8 assembly;
    String8 output_operands;
    String8 input_operands;
    Aarch64ImportVariable* first_variable;
    Aarch64ImportVariable* last_variable;
    Aarch64ImportOperand* first_operand;
    Aarch64ImportOperand* last_operand;
    Aarch64ImportPredicate* first_predicate;
    Aarch64ImportPredicate* last_predicate;
    u32 variable_count;
    u32 operand_count;
    u32 predicate_count;
    u32 fixed_mask;
    u32 fixed_value;
    u32 variable_instruction_mask;
    u64 source_hash;
    u64 signature_hash;
    u32 source_index;
    u32 normalized_form_id;
    u8 coverage_class;
    u8 encoder_family;
    u8 test_class;
    u8 reason_id;
    u8 parse_reason;
    u8 has_alternatives;
    u64 name_hash;
    String8 mnemonic;
    u8 address_kind;
    u8 address_flags;
    u16 address_base_index;
    u16 address_offset_index;
    Aarch64ImportRecord* next;
};

typedef struct Aarch64ImportRecordList Aarch64ImportRecordList;
struct Aarch64ImportRecordList
{
    Aarch64ImportRecord* first;
    Aarch64ImportRecord* last;
    u32 count;
};

typedef struct Aarch64GeneratedTableStats Aarch64GeneratedTableStats;
struct Aarch64GeneratedTableStats
{
    u64 coverage_counts[AARCH64_IMPORT_COVERAGE_COUNT];
    u64 reason_counts[AARCH64_IMPORT_REASON_COUNT];
    u64 encoder_counts[AARCH64_IMPORT_ENCODER_COUNT];
    u64 test_counts[AARCH64_IMPORT_TEST_COUNT];
    u64 canonical_form_count;
    u64 field_count;
    u64 segment_count;
    u64 operand_count;
    u64 predicate_count;
    u64 predicate_feature_count;
    u64 mnemonic_range_count;
    u64 signature_range_count;
    u64 mnemonic_candidate_count;
    u64 signature_candidate_count;
    u64 string_pool_bytes;
    u64 header_bytes;
    u64 coverage_bytes;
};

BUSTER_GLOBAL_LOCAL bool aarch64_import_acceptance_ready(Aarch64GeneratedTableStats stats)
{
    return stats.coverage_counts[AARCH64_IMPORT_COVERAGE_RESERVED_UNENCODABLE] == 0 &&
           stats.coverage_counts[AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN] == 0 &&
           stats.coverage_counts[AARCH64_IMPORT_COVERAGE_UNCLASSIFIED] == 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_ascii_identifier(String8 string)
{
    if (!string.length)
    {
        return false;
    }
    for (u64 index = 0; index < string.length; index += 1)
    {
        char8 c = string.pointer[index];
        bool okay = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '?';
        if (!okay)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_u32(String8 text, u32* value)
{
    if (!text.length)
    {
        return false;
    }
    u64 result = 0;
    for (u64 index = 0; index < text.length; index += 1)
    {
        char8 c = text.pointer[index];
        if (c < '0' || c > '9')
        {
            return false;
        }
        u64 digit = (u64)(c - '0');
        if (result > (UINT32_MAX - digit) / 10)
        {
            return false;
        }
        result = result * 10 + digit;
    }
    *value = (u32)result;
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_decode_json_string(Arena* arena, String8 raw, String8* result)
{
    JsonParser parser = {.text = raw};
    bool valid = true;
    *result = json_parse_string(arena, &parser, &valid);
    json_skip_whitespace(&parser);
    return valid && parser.index == parser.text.length;
}

BUSTER_GLOBAL_LOCAL Aarch64ImportVariable* aarch64_import_variable_find_or_add(Arena* arena, Aarch64ImportRecord* record,
                                                                                 String8 name)
{
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
    {
        if (string_equal(variable->name, name))
        {
            return variable;
        }
    }
    Aarch64ImportVariable* variable = arena_allocate(arena, Aarch64ImportVariable, 1);
    *variable = (Aarch64ImportVariable){.name = name};
    if (record->last_variable)
    {
        record->last_variable->next = variable;
    }
    else
    {
        record->first_variable = variable;
    }
    record->last_variable = variable;
    record->variable_count += 1;
    return variable;
}

BUSTER_GLOBAL_LOCAL void aarch64_import_variable_add_bit(Arena* arena, Aarch64ImportVariable* variable, u8 instruction_bit,
                                                          u8 value_bit)
{
    Aarch64ImportBit* bit = arena_allocate(arena, Aarch64ImportBit, 1);
    *bit = (Aarch64ImportBit){.instruction_bit = instruction_bit, .value_bit = value_bit};
    if (variable->last_bit)
    {
        variable->last_bit->next = bit;
    }
    else
    {
        variable->first_bit = bit;
    }
    variable->last_bit = bit;
    variable->bit_count += 1;
    variable->source_mask |= 1u << value_bit;
    variable->width = (u8)BUSTER_MAX(variable->width, (u32)value_bit + 1);
}

BUSTER_GLOBAL_LOCAL String8 aarch64_import_trim_local(String8 string)
{
    return assembly_import_trim(string);
}

BUSTER_GLOBAL_LOCAL void aarch64_import_reason_set(Aarch64ImportRecord* record, Aarch64ImportReason reason);

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_inst(Arena* arena, Aarch64ImportRecord* record)
{
    JsonParser parser = {.text = record->inst_json};
    bool valid = json_consume(&parser, '[');
    u32 bit = 0;
    while (valid)
    {
        json_skip_whitespace(&parser);
        if (json_consume(&parser, ']'))
        {
            break;
        }
        String8 raw = json_raw_value(&parser, &valid);
        if (!valid)
        {
            break;
        }
        if (bit >= 32)
        {
            record->parse_reason = AARCH64_IMPORT_REASON_UNKNOWN_FIELD;
        }
        else if (raw.length && raw.pointer[0] == '{')
        {
            String8 kind_raw = {0};
            String8 var_raw = {0};
            String8 printable_raw = {0};
            String8 index_raw = {0};
            json_raw_object_find(raw, S8("kind"), &kind_raw);
            json_raw_object_find(raw, S8("var"), &var_raw);
            json_raw_object_find(raw, S8("printable"), &printable_raw);
            json_raw_object_find(raw, S8("index"), &index_raw);
            String8 kind = {0};
            String8 variable_name = {0};
            String8 printable = {0};
            bool fields_valid = aarch64_import_decode_json_string(arena, kind_raw, &kind) &&
                                 aarch64_import_decode_json_string(arena, var_raw, &variable_name) &&
                                 aarch64_import_decode_json_string(arena, printable_raw, &printable);
            if (!fields_valid || !aarch64_import_ascii_identifier(variable_name))
            {
                record->parse_reason = AARCH64_IMPORT_REASON_UNKNOWN_FIELD;
            }
            else if (string_equal(kind, S8("varbit")))
            {
                u32 value_bit = 0;
                if (!aarch64_import_parse_u32(index_raw.length >= 2 ? string_slice(index_raw, 0, index_raw.length) : index_raw, &value_bit) ||
                    value_bit >= 32 || !string_starts_with_sequence(printable, variable_name))
                {
                    record->parse_reason = AARCH64_IMPORT_REASON_UNKNOWN_FIELD;
                }
                else
                {
                    Aarch64ImportVariable* variable = aarch64_import_variable_find_or_add(arena, record, variable_name);
                    if ((record->fixed_mask | record->variable_instruction_mask) & (1u << bit) ||
                        variable->source_mask & (1u << value_bit))
                    {
                        record->parse_reason = AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT;
                    }
                    else
                    {
                        aarch64_import_variable_add_bit(arena, variable, (u8)bit, (u8)value_bit);
                        record->variable_instruction_mask |= 1u << bit;
                    }
                }
            }
            else if (string_equal(kind, S8("var")))
            {
                Aarch64ImportVariable* variable = aarch64_import_variable_find_or_add(arena, record, variable_name);
                variable->unmapped = 1;
                if ((record->fixed_mask | record->variable_instruction_mask) & (1u << bit))
                {
                    record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT;
                }
                else
                {
                    record->variable_instruction_mask |= 1u << bit;
                    record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_UNMAPPED_VARIABLE;
                }
            }
            else
            {
                record->parse_reason = AARCH64_IMPORT_REASON_UNKNOWN_FIELD;
            }
        }
        else
        {
            u32 value = 0;
            if (string_equal(raw, S8("null")))
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_NULL_FIELD;
            }
            else if (!aarch64_import_parse_u32(raw, &value) || value > 1)
            {
                record->parse_reason = AARCH64_IMPORT_REASON_UNKNOWN_FIELD;
            }
            else if (record->variable_instruction_mask & (1u << bit))
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT;
            }
            else if (bit < 32)
            {
                record->fixed_mask |= 1u << bit;
                record->fixed_value |= value << bit;
            }
        }
        bit += 1;
        json_skip_whitespace(&parser);
        if (json_consume(&parser, ',') || (parser.index < parser.text.length && parser.text.pointer[parser.index] == ']'))
        {
            continue;
        }
        valid = false;
    }
    json_skip_whitespace(&parser);
    if (bit != 32 || parser.index != parser.text.length)
    {
        valid = false;
    }
    if (record->fixed_mask & record->variable_instruction_mask)
    {
        aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT);
    }
    u32 instruction_bits = 0;
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
    {
        u32 source_bits = 0;
        for (Aarch64ImportBit* field_bit = variable->first_bit; field_bit; field_bit = field_bit->next)
        {
            if (field_bit->instruction_bit >= 32 || field_bit->value_bit >= 32 ||
                instruction_bits & (1u << field_bit->instruction_bit) || source_bits & (1u << field_bit->value_bit))
            {
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT);
            }
            else
            {
                instruction_bits |= 1u << field_bit->instruction_bit;
                source_bits |= 1u << field_bit->value_bit;
            }
        }
        if (source_bits != variable->source_mask)
        {
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT);
        }
    }
    if (instruction_bits != record->variable_instruction_mask)
    {
        aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u32 aarch64_import_top_level_colon(String8 token)
{
    u32 parentheses = 0;
    u32 brackets = 0;
    u32 braces = 0;
    for (u32 index = 0; index < token.length; index += 1)
    {
        char8 c = token.pointer[index];
        if (c == '(')
        {
            parentheses += 1;
        }
        else if (c == ')' && parentheses)
        {
            parentheses -= 1;
        }
        else if (c == '[')
        {
            brackets += 1;
        }
        else if (c == ']' && brackets)
        {
            brackets -= 1;
        }
        else if (c == '{')
        {
            braces += 1;
        }
        else if (c == '}' && braces)
        {
            braces -= 1;
        }
        else if (c == ':' && !parentheses && !brackets && !braces)
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL u32 aarch64_import_operand_width(String8 type)
{
    for (u32 index = 0; index < type.length; index += 1)
    {
        if (type.pointer[index] >= '0' && type.pointer[index] <= '9')
        {
            u32 value = 0;
            u32 end = index;
            while (end < type.length && type.pointer[end] >= '0' && type.pointer[end] <= '9')
            {
                u32 digit = (u32)(type.pointer[end] - '0');
                if (value > (UINT32_MAX - digit) / 10)
                {
                    return 0;
                }
                value = value * 10 + digit;
                end += 1;
            }
            if (value == 3 || value == 8 || value == 16 || value == 32 || value == 64 || value == 128 || value == 256 || value == 512)
            {
                return value;
            }
            index = end;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_operand_has_name(Aarch64ImportRecord* record, String8 name, u32* index_result)
{
    for (u32 index = 0; index < record->operand_count; index += 1)
    {
        Aarch64ImportOperand* operand = record->first_operand;
        for (u32 skip = 0; operand && skip < index; skip += 1)
        {
            operand = operand->next;
        }
        if (operand && (string_equal(operand->name, name) ||
                        (operand->name.length && operand->name.pointer[0] == '_' &&
                         string_equal(string_slice(operand->name, 1, operand->name.length), name))))
        {
            if (index_result)
            {
                *index_result = index;
            }
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void aarch64_import_reason_set(Aarch64ImportRecord* record, Aarch64ImportReason reason)
{
    if (!record->parse_reason)
    {
        record->parse_reason = (u8)reason;
    }
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_type_starts_any(String8 type, String8* prefixes, u32 count)
{
    for (u32 index = 0; index < count; index += 1)
    {
        if (string_starts_with_sequence(type, prefixes[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_type_is_register(String8 type)
{
    String8 prefixes[] = {
        S8("GPR"), S8("FPR"), S8("V"), S8("ZPR"), S8("PPR"), S8("PNR"), S8("PP_"), S8("ZK"),
        S8("MatrixOp"), S8("MatrixIndex"), S8("TileOp"), S8("TileVectorOp"),
    };
    if (aarch64_import_type_starts_any(type, prefixes, BUSTER_ARRAY_LENGTH(prefixes)))
    {
        if (string_equal(type, S8("V")) || string_equal(type, S8("ZK")))
        {
            return false;
        }
        if (string_starts_with_sequence(type, S8("V")) && type.length > 1 && type.pointer[1] != '6' && type.pointer[1] != '1')
        {
            return false;
        }
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_type_is_list(String8 type)
{
    String8 prefixes[] = {S8("VecList"), S8("MatrixTileList"), S8("ZZ_"), S8("ZZZ_"), S8("ZZZZ_"), S8("VG")};
    return aarch64_import_type_starts_any(type, prefixes, BUSTER_ARRAY_LENGTH(prefixes));
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_type_is_lane(String8 type)
{
    String8 prefixes[] = {S8("VectorIndex"), S8("sme_elm_idx"), S8("sve_elm_idx")};
    return aarch64_import_type_starts_any(type, prefixes, BUSTER_ARRAY_LENGTH(prefixes));
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_type_is_relative(String8 type)
{
    String8 exact[] = {S8("am_b_target"), S8("am_bl_target"), S8("am_brcmpcond"), S8("am_brcond"),
                       S8("am_ldrlit"), S8("am_pauth_pcrel"), S8("am_tbrcond"), S8("adrlabel"), S8("adrplabel")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(exact); index += 1)
    {
        if (string_equal(type, exact[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_type_is_system(String8 type)
{
    String8 exact[] = {
        S8("mrs_sysreg_op"), S8("msr_sysreg_op"), S8("sys_cr_op"), S8("pstatefield1_op"), S8("pstatefield4_op"),
        S8("svcr_op"), S8("barrier_op"), S8("barrier_nxs_op"), S8("prfop"), S8("rprfop"), S8("sve_prfop"),
        S8("phint_op"), S8("CMHPriorityHint_op"), S8("TIndexhint_op"), S8("SyspXzrPairOperand"),
        S8("MrrsMssrPairClassOperand"), S8("WSeqPairClassOperand"), S8("XSeqPairClassOperand"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(exact); index += 1)
    {
        if (string_equal(type, exact[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_type_is_compound(String8 type)
{
    return (type.length && type.pointer[0] == '(' && string_contains(type, S8("arith_shifted_reg"))) ||
           (type.length && type.pointer[0] == '(' && string_contains(type, S8("arith_extended_reg"))) ||
           (type.length && type.pointer[0] == '(' && string_contains(type, S8("logical_shifted_reg"))) ||
           string_starts_with_sequence(type, S8("ro_Wextend")) || string_starts_with_sequence(type, S8("ro_Xextend")) ||
           string_starts_with_sequence(type, S8("arith_extendlsl"));
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_decimal_at(String8 text, u32 start, u32* end_result, u32* value_result)
{
    if (start >= text.length || text.pointer[start] < '0' || text.pointer[start] > '9')
    {
        return false;
    }
    u32 value = 0;
    u32 end = start;
    while (end < text.length && text.pointer[end] >= '0' && text.pointer[end] <= '9')
    {
        u32 digit = (u32)(text.pointer[end] - '0');
        if (value > (UINT32_MAX - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
        end += 1;
    }
    *end_result = end;
    *value_result = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_set_immediate_width_range(u32 width, bool signed_range, s32* minimum, s32* maximum)
{
    if (!width || width >= 32)
    {
        return false;
    }
    u64 extent = 1ULL << width;
    if (signed_range)
    {
        *minimum = -(s32)(extent / 2);
        *maximum = (s32)(extent / 2 - 1);
    }
    else
    {
        if (extent - 1 > INT32_MAX)
        {
            return false;
        }
        *minimum = 0;
        *maximum = (s32)(extent - 1);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_set_immediate_range(String8 type, Aarch64ImportOperand* operand)
{
    if (string_equal(type, S8("ccode")))
    {
        operand->immediate_min = 0;
        operand->immediate_max = 15;
        operand->immediate_flags = AARCH64_IMPORT_OPERAND_IMMEDIATE_RANGE_EXACT;
        return true;
    }

    String8 prefix = {0};
    bool signed_range = false;
    if (string_starts_with_sequence(type, S8("simm")))
    {
        prefix = S8("simm");
        signed_range = true;
    }
    else if (string_starts_with_sequence(type, S8("uimm")))
    {
        prefix = S8("uimm");
    }
    else if (string_starts_with_sequence(type, S8("imm")) || string_starts_with_sequence(type, S8("timm")))
    {
        prefix = string_starts_with_sequence(type, S8("timm")) ? S8("timm") : S8("imm");
    }
    else
    {
        return false;
    }

    u32 cursor = (u32)prefix.length;
    u32 first = 0;
    u32 end = 0;
    if (!aarch64_import_parse_decimal_at(type, cursor, &end, &first))
    {
        return false;
    }

    s32 minimum = 0;
    s32 maximum = 0;
    u32 scale = 0;
    u8 flags = 0;
    if (end == type.length)
    {
        if (!aarch64_import_set_immediate_width_range(first, signed_range, &minimum, &maximum))
        {
            return false;
        }
    }
    else if (type.pointer[end] == 's')
    {
        u32 scale_end = 0;
        if (!aarch64_import_parse_decimal_at(type, end + 1, &scale_end, &scale) || scale_end != type.length || !scale ||
            scale > UINT8_MAX || !aarch64_import_set_immediate_width_range(first, signed_range, &minimum, &maximum))
        {
            return false;
        }
        flags = AARCH64_IMPORT_OPERAND_SCALE_EXACT;
    }
    else if (type.pointer[end] == '_')
    {
        u32 second = 0;
        u32 second_end = 0;
        if (!aarch64_import_parse_decimal_at(type, end + 1, &second_end, &second))
        {
            return false;
        }
        if (second_end == type.length)
        {
            // The two-component imm0_127/timm0_1 grammar is an explicit
            // inclusive range. Other two-component spellings are ambiguous
            // with operand-width suffixes and stay unsupported.
            if (first != 0 || (!string_equal(prefix, S8("imm")) && !string_equal(prefix, S8("timm"))))
            {
                return false;
            }
            minimum = (s32)first;
            if (second > INT32_MAX || second < first)
            {
                return false;
            }
            maximum = (s32)second;
        }
        else if (type.pointer[second_end] == 'b' && second_end + 1 == type.length)
        {
            // A suffix such as _64b describes the register/vector width;
            // it is not the immediate range. The encoded width remains the
            // first component of simm/uimm/imm/timm.
            if (!aarch64_import_set_immediate_width_range(first, signed_range, &minimum, &maximum))
            {
                return false;
            }
        }
        else if (type.pointer[second_end] == '_')
        {
            u32 third = 0;
            u32 third_end = 0;
            if (!aarch64_import_parse_decimal_at(type, second_end + 1, &third_end, &third))
            {
                return false;
            }
            if (third_end + 1 == type.length && type.pointer[third_end] == 'b')
            {
                // The explicit range is followed by an operand-width
                // suffix, as in imm0_127_64b.
                if (first != 0 || second > INT32_MAX)
                {
                    return false;
                }
                minimum = (s32)first;
                maximum = (s32)second;
            }
            else if (third_end == type.length)
            {
                // The explicit width_range grammar is imm32_0_15 and its
                // timm variants. The first component is operand width,
                // followed by an inclusive low/high range.
                if (second > third || third > INT32_MAX)
                {
                    return false;
                }
                minimum = (s32)second;
                maximum = (s32)third;
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    if (signed_range)
    {
        flags |= AARCH64_IMPORT_OPERAND_IMMEDIATE_SIGNED;
    }
    flags |= AARCH64_IMPORT_OPERAND_IMMEDIATE_RANGE_EXACT;
    operand->immediate_min = minimum;
    operand->immediate_max = maximum;
    operand->scale = (u8)scale;
    operand->immediate_flags = flags;
    return true;
}

BUSTER_GLOBAL_LOCAL Aarch64ImportRelocationKind aarch64_import_relocation_for_type(String8 type, u8* shift, u8* end)
{
    *shift = 0;
    *end = 1;
    if (string_equal(type, S8("am_b_target")) || string_equal(type, S8("am_bl_target")))
    {
        *shift = 2;
        return AARCH64_IMPORT_RELOC_BRANCH26;
    }
    if (string_equal(type, S8("am_brcond")) || string_equal(type, S8("am_brcmpcond")))
    {
        *shift = 2;
        return AARCH64_IMPORT_RELOC_COND_BRANCH19;
    }
    if (string_equal(type, S8("am_tbrcond")))
    {
        *shift = 2;
        return AARCH64_IMPORT_RELOC_TEST_BRANCH14;
    }
    if (string_equal(type, S8("am_ldrlit")) || string_equal(type, S8("am_pauth_pcrel")))
    {
        *shift = 2;
        return AARCH64_IMPORT_RELOC_LITERAL19;
    }
    if (string_equal(type, S8("adrlabel")))
    {
        return AARCH64_IMPORT_RELOC_ADR21;
    }
    if (string_equal(type, S8("adrplabel")))
    {
        *shift = 12;
        return AARCH64_IMPORT_RELOC_ADRP21;
    }
    *end = 0;
    return AARCH64_IMPORT_RELOC_NONE;
}

BUSTER_GLOBAL_LOCAL Aarch64ImportOperand* aarch64_import_operand_add(Arena* arena, Aarch64ImportRecord* record, String8 syntax,
                                                                       String8 type, String8 name, u8 direction)
{
    Aarch64ImportOperand* operand = arena_allocate(arena, Aarch64ImportOperand, 1);
    *operand = (Aarch64ImportOperand){.syntax = syntax, .type = type, .name = name, .direction = direction, .tied_to = UINT32_MAX};
    u8 relocation_shift = 0;
    u8 relocation_end = 0;
    Aarch64ImportRelocationKind relocation = aarch64_import_relocation_for_type(type, &relocation_shift, &relocation_end);
    if (aarch64_import_type_is_list(type))
    {
        operand->kind = AARCH64_IMPORT_OPERAND_LIST;
        operand->flags |= AARCH64_IMPORT_OPERAND_FLAG_LIST;
    }
    else if (aarch64_import_type_is_lane(type))
    {
        operand->kind = AARCH64_IMPORT_OPERAND_IMMEDIATE;
        operand->flags |= AARCH64_IMPORT_OPERAND_LANE;
    }
    else if (aarch64_import_type_is_relative(type))
    {
        operand->kind = AARCH64_IMPORT_OPERAND_RELATIVE;
        operand->scale = relocation_shift;
        operand->immediate_flags = AARCH64_IMPORT_OPERAND_SCALE_EXACT;
        operand->address_kind = string_equal(type, S8("am_ldrlit")) || string_equal(type, S8("am_pauth_pcrel"))
                                    ? AARCH64_IMPORT_ADDRESS_LITERAL
                                    : AARCH64_IMPORT_ADDRESS_BRANCH;
    }
    else if (aarch64_import_type_is_register(type))
    {
        operand->kind = AARCH64_IMPORT_OPERAND_REGISTER;
        operand->register_width = (u16)aarch64_import_operand_width(type);
        operand->immediate_flags |= operand->register_width ? AARCH64_IMPORT_OPERAND_REGISTER_WIDTH_EXACT : 0;
        if (!operand->register_width)
        {
            operand->semantic_unproven = 1;
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_OPERAND_KIND);
        }
    }
    else if (aarch64_import_type_is_system(type))
    {
        operand->kind = AARCH64_IMPORT_OPERAND_SYSTEM;
    }
    else if (aarch64_import_type_is_compound(type))
    {
        operand->kind = AARCH64_IMPORT_OPERAND_COMPOUND;
        operand->semantic_unproven = 1;
    }
    else if (aarch64_import_set_immediate_range(type, operand))
    {
        operand->kind = AARCH64_IMPORT_OPERAND_IMMEDIATE;
    }
    else
    {
        operand->kind = AARCH64_IMPORT_OPERAND_UNPROVEN;
        operand->semantic_unproven = 1;
        aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_OPERAND_KIND);
    }
    (void)relocation;
    if (name.length && name.pointer[0] == '_')
    {
        operand->flags |= AARCH64_IMPORT_OPERAND_TIED | AARCH64_IMPORT_OPERAND_SUPPRESSED;
    }
    if (record->last_operand)
    {
        record->last_operand->next = operand;
    }
    else
    {
        record->first_operand = operand;
    }
    record->last_operand = operand;
    record->operand_count += 1;
    return operand;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_dag(Arena* arena, Aarch64ImportRecord* record, String8 dag, u8 direction)
{
    dag = aarch64_import_trim_local(dag);
    if (dag.length < 5 || dag.pointer[0] != '(' || dag.pointer[dag.length - 1] != ')')
    {
        record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
        return true;
    }
    u32 keyword_end = 1;
    while (keyword_end < dag.length && dag.pointer[keyword_end] != ' ' && dag.pointer[keyword_end] != '\t' && dag.pointer[keyword_end] != ')')
    {
        keyword_end += 1;
    }
    String8 keyword = string_slice(dag, 1, keyword_end);
    if (!string_equal(keyword, S8("outs")) && !string_equal(keyword, S8("ins")))
    {
        record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
        return true;
    }

    u32 body_start = keyword_end;
    while (body_start < dag.length - 1 && character_is_space(dag.pointer[body_start]))
    {
        body_start += 1;
    }
    u32 token_start = body_start;
    u32 parentheses = 0;
    u32 brackets = 0;
    u32 braces = 0;
    for (u32 index = body_start; index < dag.length - 1; index += 1)
    {
        char8 c = dag.pointer[index];
        if (c == '(')
        {
            parentheses += 1;
        }
        else if (c == ')')
        {
            if (!parentheses)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
                return true;
            }
            parentheses -= 1;
        }
        else if (c == '[')
        {
            brackets += 1;
        }
        else if (c == ']')
        {
            if (!brackets)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
                return true;
            }
            brackets -= 1;
        }
        else if (c == '{')
        {
            braces += 1;
        }
        else if (c == '}')
        {
            if (!braces)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
                return true;
            }
            braces -= 1;
        }
        bool at_separator = c == ',' && !parentheses && !brackets && !braces;
        bool at_end = index == dag.length - 2;
        if (at_separator || at_end)
        {
            u32 token_end = at_separator ? index : index + 1;
            String8 token = aarch64_import_trim_local(string_slice(dag, token_start, token_end));
            if (!token.length)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
                return true;
            }
            u32 colon = aarch64_import_top_level_colon(token);
            if (colon == UINT32_MAX || colon == 0 || colon + 1 >= token.length)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
                return true;
            }
            String8 type = aarch64_import_trim_local(string_slice(token, 0, colon));
            String8 operand_name = aarch64_import_trim_local(string_slice(token, colon + 1, token.length));
            if (operand_name.length && operand_name.pointer[0] == '$')
            {
                operand_name = string_slice(operand_name, 1, operand_name.length);
            }
            if (!type.length || !aarch64_import_ascii_identifier(operand_name))
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
                return true;
            }
            aarch64_import_operand_add(arena, record, token, type, operand_name, direction);
            token_start = index + 1;
            while (token_start < dag.length - 1 && character_is_space(dag.pointer[token_start]))
            {
                token_start += 1;
            }
        }
    }
    if (parentheses || brackets || braces)
    {
        record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_DAG;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_predicates(Arena* arena, Aarch64ImportRecord* record)
{
    JsonParser parser = {.text = record->predicates_json};
    bool valid = json_consume(&parser, '[');
    while (valid)
    {
        String8 raw = {0};
        if (!json_raw_array_next(&parser, &raw, &valid))
        {
            break;
        }
        String8 name_raw = raw;
        if (raw.length && raw.pointer[0] == '{')
        {
            if (!json_raw_object_find(raw, S8("def"), &name_raw))
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_UNKNOWN_PREDICATE;
                continue;
            }
        }
        String8 name = {0};
        if (!aarch64_import_decode_json_string(arena, name_raw, &name) || !aarch64_import_ascii_identifier(name))
        {
            record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_UNKNOWN_PREDICATE;
            continue;
        }
        Aarch64ImportPredicate* predicate = arena_allocate(arena, Aarch64ImportPredicate, 1);
        *predicate = (Aarch64ImportPredicate){.name = name};
        if (record->last_predicate)
        {
            record->last_predicate->next = predicate;
        }
        else
        {
            record->first_predicate = predicate;
        }
        record->last_predicate = predicate;
        record->predicate_count += 1;
    }
    json_skip_whitespace(&parser);
    return valid && parser.index == parser.text.length;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_asm(Arena* arena, Aarch64ImportRecord* record)
{
    String8 assembly = record->assembly;
    u32 braces = 0;
    u32 brackets = 0;
    bool in_comment = false;
    for (u32 index = 0; index < assembly.length; index += 1)
    {
        char8 c = assembly.pointer[index];
        if (c == '{')
        {
            braces += 1;
            record->has_alternatives = 1;
        }
        else if (c == '}')
        {
            if (!braces)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_TEMPLATE;
            }
            else
            {
                braces -= 1;
            }
        }
        else if (c == '[')
        {
            brackets += 1;
        }
        else if (c == ']')
        {
            if (!brackets)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_TEMPLATE;
            }
            else
            {
                brackets -= 1;
            }
        }
        else if (c == '$' && !in_comment)
        {
            u32 start = index + 1;
            u32 end = start;
            while (end < assembly.length)
            {
                char8 token = assembly.pointer[end];
                bool identifier = (token >= 'a' && token <= 'z') || (token >= 'A' && token <= 'Z') ||
                                  (token >= '0' && token <= '9') || token == '_';
                if (!identifier)
                {
                    break;
                }
                end += 1;
            }
            if (start == end)
            {
                record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_TEMPLATE;
            }
            else
            {
                String8 name = string_slice(assembly, start, end);
                u32 operand_index = 0;
                if (!aarch64_import_operand_has_name(record, name, &operand_index))
                {
                    record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MISSING_OPERAND;
                }
                else
                {
                    Aarch64ImportOperand* operand = record->first_operand;
                    for (u32 skip = 0; operand && skip < operand_index; skip += 1)
                    {
                        operand = operand->next;
                    }
                    if (operand)
                    {
                        operand->seen_in_asm = 1;
                        operand->flags &= (u8)~AARCH64_IMPORT_OPERAND_IMPLICIT;
                        if (braces)
                        {
                            operand->flags |= AARCH64_IMPORT_OPERAND_OPTIONAL;
                        }
                    }
                }
            }
            index = end ? end - 1 : index;
        }
        in_comment = c == ';' ? true : (c == '\n' ? false : in_comment);
    }
    if (braces || brackets)
    {
        record->parse_reason = record->parse_reason ? record->parse_reason : AARCH64_IMPORT_REASON_MALFORMED_TEMPLATE;
    }
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
    {
        if (operand->flags & AARCH64_IMPORT_OPERAND_TIED)
        {
            operand->flags |= AARCH64_IMPORT_OPERAND_SUPPRESSED;
        }
        else
        {
            if (!operand->seen_in_asm)
            {
                operand->flags |= AARCH64_IMPORT_OPERAND_IMPLICIT;
            }
        }
    }

    // Ties are structural DAG relationships. Preserve the target operand
    // index instead of leaving a lossy "tied" bit for the future emitter.
    u32 operand_index = 0;
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next, operand_index += 1)
    {
        String8 base_name = operand->name;
        if (base_name.length && base_name.pointer[0] == '_')
        {
            base_name = string_slice(base_name, 1, base_name.length);
        }
        u32 prior_index = 0;
        Aarch64ImportOperand* prior = record->first_operand;
        for (; prior && prior_index < operand_index; prior = prior->next, prior_index += 1)
        {
            String8 prior_name = prior->name;
            if (prior_name.length && prior_name.pointer[0] == '_')
            {
                prior_name = string_slice(prior_name, 1, prior_name.length);
            }
            if (string_equal(base_name, prior_name))
            {
                break;
            }
        }
        if (prior)
        {
            operand->tied_to = prior_index;
            operand->flags |= AARCH64_IMPORT_OPERAND_TIED;
            if (!string_equal(operand->type, prior->type))
            {
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_TIED_OPERAND);
            }
        }
        else if (operand->flags & AARCH64_IMPORT_OPERAND_TIED)
        {
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_TIED_OPERAND);
        }
    }

    // Normalize the simple AArch64 bracket-address grammar. More elaborate
    // nested/alternative templates remain explicit unsupported schema rows;
    // no address meaning is inferred from a mnemonic or a substring.
    record->address_kind = AARCH64_IMPORT_ADDRESS_NONE;
    record->address_base_index = UINT16_MAX;
    record->address_offset_index = UINT16_MAX;
    for (u32 index = 0; index < assembly.length; index += 1)
    {
        if (assembly.pointer[index] != '[' ||
            (index && ((assembly.pointer[index - 1] >= 'a' && assembly.pointer[index - 1] <= 'z') ||
                       (assembly.pointer[index - 1] >= 'A' && assembly.pointer[index - 1] <= 'Z') ||
                       (assembly.pointer[index - 1] >= '0' && assembly.pointer[index - 1] <= '9') ||
                       assembly.pointer[index - 1] == '_' || assembly.pointer[index - 1] == ']')))
        {
            continue;
        }
        if (record->address_kind != AARCH64_IMPORT_ADDRESS_NONE)
        {
            record->address_kind = AARCH64_IMPORT_ADDRESS_UNPROVEN;
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_MEMORY_FORM);
            break;
        }
        u32 close = index + 1;
        u32 nesting = 1;
        while (close < assembly.length && nesting)
        {
            if (assembly.pointer[close] == '[')
            {
                nesting += 1;
            }
            else if (assembly.pointer[close] == ']')
            {
                nesting -= 1;
            }
            close += 1;
        }
        if (nesting || close <= index + 1)
        {
            record->address_kind = AARCH64_IMPORT_ADDRESS_UNPROVEN;
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNSUPPORTED_ADDRESS_GRAMMAR);
            break;
        }
        u32 refs[3] = {0};
        u32 ref_count = 0;
        for (u32 cursor = index + 1; cursor + 1 < close; cursor += 1)
        {
            if (assembly.pointer[cursor] != '$')
            {
                continue;
            }
            u32 ref_start = cursor + 1;
            u32 ref_end = ref_start;
            while (ref_end < close - 1)
            {
                char8 c = assembly.pointer[ref_end];
                bool identifier = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                if (!identifier)
                {
                    break;
                }
                ref_end += 1;
            }
            if (ref_end == ref_start || ref_count >= BUSTER_ARRAY_LENGTH(refs))
            {
                record->address_kind = AARCH64_IMPORT_ADDRESS_UNPROVEN;
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNSUPPORTED_ADDRESS_GRAMMAR);
                break;
            }
            u32 found = 0;
            if (!aarch64_import_operand_has_name(record, string_slice(assembly, ref_start, ref_end), &found))
            {
                record->address_kind = AARCH64_IMPORT_ADDRESS_UNPROVEN;
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_MISSING_OPERAND);
                break;
            }
            refs[ref_count++] = found;
            cursor = ref_end;
        }
        if (record->address_kind == AARCH64_IMPORT_ADDRESS_UNPROVEN)
        {
            break;
        }
        Aarch64ImportOperand* base = 0;
        Aarch64ImportOperand* offset = 0;
        for (u32 ref_index = 0; ref_index < ref_count; ref_index += 1)
        {
            Aarch64ImportOperand* candidate = record->first_operand;
            for (u32 skip = 0; candidate && skip < refs[ref_index]; skip += 1)
            {
                candidate = candidate->next;
            }
            if (ref_index == 0)
            {
                base = candidate;
            }
            else if (ref_index == 1)
            {
                offset = candidate;
            }
        }
        if (!base || base->kind != AARCH64_IMPORT_OPERAND_REGISTER || ref_count > 2)
        {
            record->address_kind = AARCH64_IMPORT_ADDRESS_UNPROVEN;
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_MEMORY_FORM);
            break;
        }
        record->address_kind = ref_count == 1 ? AARCH64_IMPORT_ADDRESS_BASE :
                               (offset->kind == AARCH64_IMPORT_OPERAND_REGISTER ? AARCH64_IMPORT_ADDRESS_BASE_INDEX
                                                                                : AARCH64_IMPORT_ADDRESS_BASE_OFFSET);
        record->address_base_index = (u16)refs[0];
        record->address_offset_index = ref_count > 1 ? (u16)refs[1] : UINT16_MAX;
        record->address_flags = (close < assembly.length && assembly.pointer[close] == '!') ? 1u : 0u;
        if (close < assembly.length && assembly.pointer[close] == '!')
        {
            close += 1;
        }
        if (close < assembly.length && assembly.pointer[close] == ',')
        {
            record->address_flags |= 2u;
        }
        for (u32 ref_index = 0; ref_index < ref_count; ref_index += 1)
        {
            Aarch64ImportOperand* candidate = record->first_operand;
            for (u32 skip = 0; candidate && skip < refs[ref_index]; skip += 1)
            {
                candidate = candidate->next;
            }
            if (candidate)
            {
                candidate->address_kind = record->address_kind;
                candidate->address_flags = record->address_flags;
                candidate->address_base_index = (u16)refs[0];
                candidate->address_offset_index = record->address_offset_index;
                candidate->immediate_flags |= AARCH64_IMPORT_OPERAND_ADDRESS_EXACT;
            }
        }
        index = close;
    }
    if (record->address_kind != AARCH64_IMPORT_ADDRESS_NONE &&
        (record->address_base_index >= record->operand_count ||
         (record->address_offset_index != UINT16_MAX && record->address_offset_index >= record->operand_count)))
    {
        record->address_kind = AARCH64_IMPORT_ADDRESS_UNPROVEN;
        aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_MEMORY_FORM);
    }
    (void)arena;
    return true;
}

BUSTER_GLOBAL_LOCAL String8 aarch64_import_mnemonic(String8 assembly)
{
    u64 end = 0;
    while (end < assembly.length && assembly.pointer[end] != ' ' && assembly.pointer[end] != '\t' && assembly.pointer[end] != '{')
    {
        end += 1;
    }
    return string_slice(assembly, 0, end);
}

BUSTER_GLOBAL_LOCAL Aarch64ImportVariable* aarch64_import_variable_find(Aarch64ImportRecord* record, String8 name)
{
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
    {
        if (string_equal(variable->name, name))
        {
            return variable;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_operand_base_name_equal(String8 operand_name, String8 variable_name)
{
    if (operand_name.length && operand_name.pointer[0] == '_')
    {
        operand_name = string_slice(operand_name, 1, operand_name.length);
    }
    return string_equal(operand_name, variable_name);
}

BUSTER_GLOBAL_LOCAL void aarch64_import_set_field_semantics(Aarch64ImportRecord* record)
{
    // Only the operand type grammar is authoritative for a variable's
    // encoding semantics. In particular, a variable name or mnemonic never
    // supplies a register/immediate/relocation meaning by itself.
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
    {
        variable->transform = AARCH64_IMPORT_TRANSFORM_NONE;
        variable->relocation = AARCH64_IMPORT_RELOC_NONE;
        variable->relocation_end = 0;
        variable->shift = 0;
        bool mapped = false;
        u32 mapped_count = 0;
        u8 transform = AARCH64_IMPORT_TRANSFORM_NONE;
        for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
        {
            if (!aarch64_import_operand_base_name_equal(operand->name, variable->name))
            {
                continue;
            }
            mapped = true;
            mapped_count += 1;
            if (operand->semantic_unproven || operand->kind == AARCH64_IMPORT_OPERAND_UNPROVEN)
            {
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_FIELD_SEMANTICS);
                continue;
            }
            u8 candidate_transform = AARCH64_IMPORT_TRANSFORM_NONE;
            if (operand->kind == AARCH64_IMPORT_OPERAND_REGISTER)
            {
                candidate_transform = AARCH64_IMPORT_TRANSFORM_REGISTER;
            }
            else if (operand->kind == AARCH64_IMPORT_OPERAND_RELATIVE)
            {
                u8 shift = 0;
                u8 relocation_end = 0;
                Aarch64ImportRelocationKind relocation = aarch64_import_relocation_for_type(operand->type, &shift, &relocation_end);
                if (relocation == AARCH64_IMPORT_RELOC_NONE)
                {
                    aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_FIELD_SEMANTICS);
                    continue;
                }
                candidate_transform = AARCH64_IMPORT_TRANSFORM_PC_RELATIVE;
                variable->relocation = (u8)relocation;
                variable->relocation_end = relocation_end;
                variable->shift = shift;
                variable->flags |= AARCH64_IMPORT_FIELD_PC_RELATIVE;
            }
            else if (operand->kind == AARCH64_IMPORT_OPERAND_SYSTEM)
            {
                candidate_transform = AARCH64_IMPORT_TRANSFORM_SYSTEM;
            }
            else if (operand->flags & AARCH64_IMPORT_OPERAND_LANE)
            {
                candidate_transform = AARCH64_IMPORT_TRANSFORM_LANE;
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_IMMEDIATE_RANGE);
            }
            else if (operand->kind == AARCH64_IMPORT_OPERAND_IMMEDIATE)
            {
                if (!(operand->immediate_flags & AARCH64_IMPORT_OPERAND_IMMEDIATE_RANGE_EXACT))
                {
                    aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_IMMEDIATE_RANGE);
                    continue;
                }
                candidate_transform = string_equal(operand->type, S8("ccode")) ? AARCH64_IMPORT_TRANSFORM_CONDITION
                                                                                : AARCH64_IMPORT_TRANSFORM_IMMEDIATE;
            }
            else if (operand->kind == AARCH64_IMPORT_OPERAND_COMPOUND)
            {
                candidate_transform = AARCH64_IMPORT_TRANSFORM_SHIFT_EXTEND;
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_CORRESPONDENCE);
            }
            else
            {
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_FIELD_SEMANTICS);
                continue;
            }
            if (transform && transform != candidate_transform)
            {
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_FIELD_SEMANTICS);
            }
            transform = candidate_transform;
            if (operand->direction & AARCH64_IMPORT_OPERAND_WRITE)
            {
                variable->flags |= AARCH64_IMPORT_FIELD_DESTINATION;
            }
            if (operand->direction & AARCH64_IMPORT_OPERAND_READ)
            {
                variable->flags |= AARCH64_IMPORT_FIELD_SOURCE;
            }
        }
        if (variable->unmapped)
        {
            variable->flags |= AARCH64_IMPORT_FIELD_UNMAPPED;
            continue;
        }
        if (!mapped)
        {
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_CORRESPONDENCE);
        }
        else if (mapped_count > 1)
        {
            u32 untied_count = 0;
            for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
            {
                if (aarch64_import_operand_base_name_equal(operand->name, variable->name) && operand->tied_to == UINT32_MAX)
                {
                    untied_count += 1;
                }
            }
            if (untied_count > 1)
            {
                aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_CORRESPONDENCE);
            }
        }
        variable->transform = transform;
        if (transform == AARCH64_IMPORT_TRANSFORM_REGISTER)
        {
            variable->flags &= (u8)~AARCH64_IMPORT_FIELD_IMMEDIATE;
        }
        else if (transform == AARCH64_IMPORT_TRANSFORM_IMMEDIATE || transform == AARCH64_IMPORT_TRANSFORM_CONDITION ||
                 transform == AARCH64_IMPORT_TRANSFORM_SHIFT_EXTEND || transform == AARCH64_IMPORT_TRANSFORM_LANE ||
                 transform == AARCH64_IMPORT_TRANSFORM_SYSTEM)
        {
            variable->flags |= AARCH64_IMPORT_FIELD_IMMEDIATE;
        }
    }
    if (record->address_kind == AARCH64_IMPORT_ADDRESS_UNPROVEN)
    {
        aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_MEMORY_FORM);
    }
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_is_system(Aarch64ImportRecord* record)
{
    String8 name = record->name;
    String8 mnemonic = aarch64_import_mnemonic(record->assembly);
    String8 system_prefixes[] = {
        S8("MRS"), S8("MSR"), S8("SYS"), S8("SYSL"), S8("HINT"), S8("CLREX"), S8("DMB"), S8("DSB"), S8("ISB"),
        S8("DC"), S8("IC"), S8("AT"), S8("TLBI"), S8("ERET"), S8("DRPS"), S8("WFI"), S8("WFE"), S8("YIELD"),
        S8("SEV"), S8("SEVL"), S8("SMSTART"), S8("SMSTOP"), S8("SVCR"), S8("PSTATE"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(system_prefixes); index += 1)
    {
        if (string_starts_with_sequence(name, system_prefixes[index]) || string_starts_with_sequence(mnemonic, system_prefixes[index]))
        {
            return true;
        }
    }
    return string_contains(name, S8("SYSREG")) || string_contains(name, S8("PSTATE"));
}

BUSTER_GLOBAL_LOCAL Aarch64ImportEncoderFamily aarch64_import_encoder_family(Aarch64ImportRecord* record)
{
    String8 name = record->name;
    String8 mnemonic = aarch64_import_mnemonic(record->assembly);
    if (aarch64_import_is_system(record))
    {
        return AARCH64_IMPORT_ENCODER_SYSTEM;
    }
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
    {
        if (string_contains(operand->type, S8("Matrix")) || string_contains(operand->type, S8("Tile")) ||
            string_contains(operand->type, S8("ZA")))
        {
            return AARCH64_IMPORT_ENCODER_SME_SME2;
        }
    }
    if (string_contains(name, S8("SME")) || string_contains(name, S8("ZA")) || string_contains(name, S8("ZT")) ||
        string_contains(name, S8("MOVA")) || string_contains(name, S8("FMOPA")))
    {
        return AARCH64_IMPORT_ENCODER_SME_SME2;
    }
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
    {
        if (string_starts_with_sequence(operand->type, S8("ZPR")) || string_starts_with_sequence(operand->type, S8("PPR")))
        {
            return AARCH64_IMPORT_ENCODER_SVE_SVE2;
        }
    }
    if (string_contains(name, S8("SVE")) || string_contains(name, S8("SVE2")) || string_contains(name, S8("ZPR")) ||
        string_contains(name, S8("PPR")) || string_contains(mnemonic, S8("while")) || string_contains(mnemonic, S8("ptrue")))
    {
        return AARCH64_IMPORT_ENCODER_SVE_SVE2;
    }
    if (string_contains(name, S8("MTE")) || string_contains(name, S8("STG")) || string_contains(name, S8("LDG")) ||
        string_contains(name, S8("IRG")) || string_contains(name, S8("ADDG")))
    {
        return AARCH64_IMPORT_ENCODER_MTE;
    }
    if (string_contains(name, S8("ATOMIC")) || string_contains(name, S8("LDADD")) || string_contains(name, S8("CAS")) ||
        string_contains(name, S8("SWP")) || string_contains(name, S8("STXR")) || string_contains(name, S8("LDXR")))
    {
        return AARCH64_IMPORT_ENCODER_ATOMIC_LSE;
    }
    if (string_contains(name, S8("AES")) || string_contains(name, S8("SHA")) || string_contains(name, S8("SM3")) ||
        string_contains(name, S8("SM4")) || string_contains(name, S8("CRC")) || string_contains(name, S8("PMULL")))
    {
        return AARCH64_IMPORT_ENCODER_CRYPTO;
    }
    if (string_equal(mnemonic, S8("b")) || string_equal(mnemonic, S8("bl")) || string_equal(mnemonic, S8("br")) ||
        string_equal(mnemonic, S8("blr")) || string_starts_with_sequence(mnemonic, S8("b.")) ||
        string_starts_with_sequence(mnemonic, S8("cb")) || string_starts_with_sequence(mnemonic, S8("tb")) ||
        string_starts_with_sequence(mnemonic, S8("adr")))
    {
        return AARCH64_IMPORT_ENCODER_BRANCH;
    }
    if (string_starts_with_sequence(mnemonic, S8("ldr")) || string_starts_with_sequence(mnemonic, S8("str")) ||
        string_starts_with_sequence(mnemonic, S8("ld")) || string_starts_with_sequence(mnemonic, S8("st")))
    {
        return AARCH64_IMPORT_ENCODER_LOAD_STORE;
    }
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
    {
        if (string_starts_with_sequence(operand->type, S8("FPR")) || string_starts_with_sequence(operand->type, S8("V")) ||
            string_contains(operand->type, S8("VecList")))
        {
            return AARCH64_IMPORT_ENCODER_FP_SIMD_NEON;
        }
    }
    if (string_contains(name, S8("NEON")) || string_contains(name, S8("SIMD")) || string_contains(name, S8("V128")) ||
        string_contains(name, S8("FP")) || string_contains(name, S8("FPR")) || string_contains(name, S8("DOT")) ||
        string_contains(name, S8("Vd")))
    {
        return AARCH64_IMPORT_ENCODER_FP_SIMD_NEON;
    }
    return AARCH64_IMPORT_ENCODER_SCALAR_INTEGER;
}

BUSTER_GLOBAL_LOCAL Aarch64ImportTestClass aarch64_import_test_class(Aarch64ImportRecord* record)
{
    String8 mnemonic = aarch64_import_mnemonic(record->assembly);
    if (record->parse_reason)
    {
        return AARCH64_IMPORT_TEST_SCHEMA;
    }
    if (record->encoder_family == AARCH64_IMPORT_ENCODER_BRANCH || string_contains(mnemonic, S8("adr")))
    {
        return AARCH64_IMPORT_TEST_BRANCH;
    }
    if (record->encoder_family == AARCH64_IMPORT_ENCODER_SME_SME2 || aarch64_import_is_system(record))
    {
        return AARCH64_IMPORT_TEST_SME_SYSTEM;
    }
    if (record->address_kind == AARCH64_IMPORT_ADDRESS_BASE || record->address_kind == AARCH64_IMPORT_ADDRESS_BASE_OFFSET ||
        record->address_kind == AARCH64_IMPORT_ADDRESS_BASE_INDEX)
    {
        return AARCH64_IMPORT_TEST_MEMORY;
    }
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
    {
        if (operand->flags & AARCH64_IMPORT_OPERAND_LANE || operand->flags & AARCH64_IMPORT_OPERAND_FLAG_LIST)
        {
            return AARCH64_IMPORT_TEST_SIMD_LIST_LANE;
        }
        if (operand->type.length && (string_starts_with_sequence(operand->type, S8("PPR")) || string_contains(operand->type, S8("pred"))))
        {
            return AARCH64_IMPORT_TEST_SVE_PREDICATE;
        }
        if (operand->kind == AARCH64_IMPORT_OPERAND_MEMORY)
        {
            return AARCH64_IMPORT_TEST_MEMORY;
        }
    }
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
    {
        if (variable->transform == AARCH64_IMPORT_TRANSFORM_PC_RELATIVE)
        {
            return AARCH64_IMPORT_TEST_BRANCH;
        }
        if (variable->transform == AARCH64_IMPORT_TRANSFORM_IMMEDIATE || variable->transform == AARCH64_IMPORT_TRANSFORM_CONDITION ||
            variable->transform == AARCH64_IMPORT_TRANSFORM_SHIFT_EXTEND)
        {
            return AARCH64_IMPORT_TEST_IMMEDIATE;
        }
    }
    return AARCH64_IMPORT_TEST_SCALAR;
}

BUSTER_GLOBAL_LOCAL u64 aarch64_import_hash_mix(u64 hash, u64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    hash ^= hash >> 29;
    hash *= 0x9e3779b185ebca87ULL;
    hash ^= hash >> 32;
    return hash;
}

BUSTER_GLOBAL_LOCAL u64 aarch64_import_signature_hash(Aarch64ImportRecord* record)
{
    u64 hash = 0x6a09e667f3bcc909ULL;
    hash = aarch64_import_hash_mix(hash, record->fixed_mask);
    hash = aarch64_import_hash_mix(hash, record->fixed_value);
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
    {
        hash = aarch64_import_hash_mix(hash, buster_hash_64((u8*)variable->name.pointer, variable->name.length));
        hash = aarch64_import_hash_mix(hash, variable->source_mask | ((u64)variable->width << 32));
        hash = aarch64_import_hash_mix(hash, variable->transform | ((u64)variable->relocation << 8) | ((u64)variable->shift << 16));
        for (Aarch64ImportBit* bit = variable->first_bit; bit; bit = bit->next)
        {
            hash = aarch64_import_hash_mix(hash, bit->instruction_bit | ((u64)bit->value_bit << 8));
        }
    }
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
    {
        hash = aarch64_import_hash_mix(hash, buster_hash_64((u8*)operand->type.pointer, operand->type.length));
        hash = aarch64_import_hash_mix(hash, buster_hash_64((u8*)operand->name.pointer, operand->name.length));
        hash = aarch64_import_hash_mix(hash, operand->direction | ((u64)operand->kind << 8) | ((u64)operand->flags << 16) |
                                               ((u64)operand->tied_to << 24));
        hash = aarch64_import_hash_mix(hash, (u64)(u32)operand->immediate_min | ((u64)(u32)operand->immediate_max << 32));
        hash = aarch64_import_hash_mix(hash, operand->register_width | ((u64)operand->scale << 16) |
                                               ((u64)operand->immediate_flags << 24) | ((u64)operand->address_kind << 32) |
                                               ((u64)operand->address_flags << 40) | ((u64)operand->address_base_index << 48));
        hash = aarch64_import_hash_mix(hash, operand->address_offset_index);
    }
    hash = aarch64_import_hash_mix(hash, record->address_kind | ((u64)record->address_flags << 8) |
                                             ((u64)record->address_base_index << 16) | ((u64)record->address_offset_index << 32));
    for (Aarch64ImportPredicate* predicate = record->first_predicate; predicate; predicate = predicate->next)
    {
        hash = aarch64_import_hash_mix(hash, buster_hash_64((u8*)predicate->name.pointer, predicate->name.length));
    }
    return hash;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_variable_equal(Aarch64ImportVariable* left, Aarch64ImportVariable* right)
{
    if (!string_equal(left->name, right->name) || left->source_mask != right->source_mask || left->width != right->width ||
        left->unmapped != right->unmapped || left->transform != right->transform || left->relocation != right->relocation ||
        left->relocation_end != right->relocation_end || left->shift != right->shift || left->bit_count != right->bit_count)
    {
        return false;
    }
    Aarch64ImportBit* left_bit = left->first_bit;
    Aarch64ImportBit* right_bit = right->first_bit;
    while (left_bit && right_bit)
    {
        if (left_bit->instruction_bit != right_bit->instruction_bit || left_bit->value_bit != right_bit->value_bit)
        {
            return false;
        }
        left_bit = left_bit->next;
        right_bit = right_bit->next;
    }
    return !left_bit && !right_bit;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_signature_equal(Aarch64ImportRecord* left, Aarch64ImportRecord* right)
{
    if (left->fixed_mask != right->fixed_mask || left->fixed_value != right->fixed_value || left->variable_count != right->variable_count ||
        left->operand_count != right->operand_count)
    {
        return false;
    }
    Aarch64ImportVariable* left_variable = left->first_variable;
    Aarch64ImportVariable* right_variable = right->first_variable;
    while (left_variable && right_variable)
    {
        if (!aarch64_import_variable_equal(left_variable, right_variable))
        {
            return false;
        }
        left_variable = left_variable->next;
        right_variable = right_variable->next;
    }
    Aarch64ImportOperand* left_operand = left->first_operand;
    Aarch64ImportOperand* right_operand = right->first_operand;
    while (left_operand && right_operand)
    {
        if (!string_equal(left_operand->type, right_operand->type) || !string_equal(left_operand->name, right_operand->name) ||
            left_operand->direction != right_operand->direction || left_operand->kind != right_operand->kind ||
            left_operand->flags != right_operand->flags || left_operand->tied_to != right_operand->tied_to ||
            left_operand->immediate_min != right_operand->immediate_min || left_operand->immediate_max != right_operand->immediate_max ||
            left_operand->register_width != right_operand->register_width || left_operand->scale != right_operand->scale ||
            left_operand->immediate_flags != right_operand->immediate_flags || left_operand->address_kind != right_operand->address_kind ||
            left_operand->address_flags != right_operand->address_flags || left_operand->address_base_index != right_operand->address_base_index ||
            left_operand->address_offset_index != right_operand->address_offset_index)
        {
            return false;
        }
        left_operand = left_operand->next;
        right_operand = right_operand->next;
    }
    if (left_variable || right_variable || left_operand || right_operand || left->address_kind != right->address_kind ||
        left->address_flags != right->address_flags || left->address_base_index != right->address_base_index ||
        left->address_offset_index != right->address_offset_index || left->predicate_count != right->predicate_count)
    {
        return false;
    }
    Aarch64ImportPredicate* left_predicate = left->first_predicate;
    Aarch64ImportPredicate* right_predicate = right->first_predicate;
    while (left_predicate && right_predicate)
    {
        if (!string_equal(left_predicate->name, right_predicate->name))
        {
            return false;
        }
        left_predicate = left_predicate->next;
        right_predicate = right_predicate->next;
    }
    return !left_predicate && !right_predicate;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_record_object(Arena* arena, String8 object, Aarch64ImportRecord* record)
{
    JsonParser parser = {.text = object};
    bool valid = json_consume(&parser, '{');
    String8 raw_name = {0};
    while (valid)
    {
        String8 raw_key = {0};
        String8 value = {0};
        if (!json_raw_object_next(&parser, &raw_key, &value, &valid))
        {
            break;
        }
        String8 key = json_raw_key_text(raw_key);
        if (string_equal(key, S8("name")))
        {
            raw_name = value;
        }
        else if (string_equal(key, S8("inst")))
        {
            record->inst_json = value;
        }
        else if (string_equal(key, S8("asm")))
        {
            record->asm_json = value;
        }
        else if (string_equal(key, S8("out")))
        {
            record->out_json = value;
        }
        else if (string_equal(key, S8("in")))
        {
            record->in_json = value;
        }
        else if (string_equal(key, S8("predicates")))
        {
            record->predicates_json = value;
        }
    }
    json_skip_whitespace(&parser);
    valid = valid && parser.index == parser.text.length && raw_name.length && record->inst_json.length && record->asm_json.length &&
            record->out_json.length && record->in_json.length && record->predicates_json.length;
    if (!valid || !aarch64_import_decode_json_string(arena, raw_name, &record->name) ||
        !aarch64_import_decode_json_string(arena, record->asm_json, &record->assembly) ||
        !aarch64_import_decode_json_string(arena, record->out_json, &record->output_operands) ||
        !aarch64_import_decode_json_string(arena, record->in_json, &record->input_operands))
    {
        return false;
    }
    if (!aarch64_import_parse_inst(arena, record) || !aarch64_import_parse_dag(arena, record, record->output_operands, AARCH64_IMPORT_OPERAND_WRITE) ||
        !aarch64_import_parse_dag(arena, record, record->input_operands, AARCH64_IMPORT_OPERAND_READ) ||
        !aarch64_import_parse_predicates(arena, record))
    {
        return false;
    }
    aarch64_import_parse_asm(arena, record);
    aarch64_import_set_field_semantics(record);
    record->mnemonic = aarch64_import_mnemonic(record->assembly);
    record->name_hash = buster_hash_64((u8*)record->name.pointer, record->name.length);
    record->source_hash = 0x243f6a8885a308d3ULL;
    String8 raw_values[] = {record->name, record->inst_json, record->asm_json, record->out_json, record->in_json, record->predicates_json};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(raw_values); index += 1)
    {
        record->source_hash = aarch64_import_hash_mix(record->source_hash,
                                                       buster_hash_64((u8*)raw_values[index].pointer, raw_values[index].length));
        record->source_hash = aarch64_import_hash_mix(record->source_hash, raw_values[index].length);
    }
    record->encoder_family = (u8)aarch64_import_encoder_family(record);
    record->test_class = (u8)aarch64_import_test_class(record);
    return true;
}

BUSTER_GLOBAL_LOCAL void aarch64_import_record_push(Arena* arena, Aarch64ImportRecordList* list, Aarch64ImportRecord record)
{
    Aarch64ImportRecord* node = arena_allocate(arena, Aarch64ImportRecord, 1);
    *node = record;
    node->source_index = list->count;
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

BUSTER_GLOBAL_LOCAL int aarch64_import_record_compare(const void* left_pointer, const void* right_pointer)
{
    Aarch64ImportRecord const* left = *(Aarch64ImportRecord* const*)left_pointer;
    Aarch64ImportRecord const* right = *(Aarch64ImportRecord* const*)right_pointer;
    int result = assembly_import_string_compare(&left->name, &right->name);
    if (!result)
    {
        result = (left->source_hash > right->source_hash) - (left->source_hash < right->source_hash);
    }
    if (!result)
    {
        result = (left->source_index > right->source_index) - (left->source_index < right->source_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void aarch64_import_record_sort(Arena* arena, Aarch64ImportRecordList* records)
{
    if (records->count < 2)
    {
        return;
    }
    Aarch64ImportRecord** sorted = arena_allocate(arena, Aarch64ImportRecord*, records->count);
    u32 index = 0;
    for (Aarch64ImportRecord* record = records->first; record; record = record->next)
    {
        sorted[index++] = record;
    }
    qsort(sorted, records->count, sizeof(sorted[0]), aarch64_import_record_compare);
    records->first = sorted[0];
    for (index = 1; index < records->count; index += 1)
    {
        sorted[index - 1]->next = sorted[index];
    }
    records->last = sorted[records->count - 1];
    records->last->next = 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_parse_normalized(Arena* arena, String8 jsonl, Aarch64ImportRecordList* records)
{
    u64 start = 0;
    while (start < jsonl.length)
    {
        u64 end = start;
        while (end < jsonl.length && jsonl.pointer[end] != '\n')
        {
            end += 1;
        }
        String8 line = assembly_import_trim(string_slice(jsonl, start, end));
        if (line.length)
        {
            Aarch64ImportRecord record = {0};
            if (!aarch64_import_parse_record_object(arena, line, &record))
            {
                return false;
            }
            aarch64_import_record_push(arena, records, record);
        }
        start = end + (end < jsonl.length);
    }
    aarch64_import_record_sort(arena, records);
    return records->count != 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_record_is_valid_for_alias(Aarch64ImportRecord* record)
{
    return record->parse_reason == AARCH64_IMPORT_REASON_NONE;
}

BUSTER_GLOBAL_LOCAL void aarch64_import_normalize_records(Aarch64ImportRecordList* records)
{
    u32 canonical_count = 0;
    for (Aarch64ImportRecord* record = records->first; record; record = record->next)
    {
        if (record->parse_reason != AARCH64_IMPORT_REASON_NONE)
        {
            record->reason_id = record->parse_reason;
        }
        if (record->parse_reason == AARCH64_IMPORT_REASON_UNMAPPED_VARIABLE ||
            record->parse_reason == AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT)
        {
            record->coverage_class = AARCH64_IMPORT_COVERAGE_RESERVED_UNENCODABLE;
        }
        else if (record->parse_reason != AARCH64_IMPORT_REASON_NONE)
        {
            record->coverage_class = AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN;
        }
        else if (aarch64_import_is_system(record))
        {
            record->coverage_class = AARCH64_IMPORT_COVERAGE_PRIVILEGED_SYSTEM;
            record->reason_id = AARCH64_IMPORT_REASON_SYSTEM_OR_PRIVILEGED;
        }
        else if (!record->variable_count && !record->operand_count)
        {
            record->coverage_class = AARCH64_IMPORT_COVERAGE_DIRECT;
        }
        else
        {
            record->coverage_class = AARCH64_IMPORT_COVERAGE_NORMALIZED;
        }
        record->encoder_family = (u8)aarch64_import_encoder_family(record);
        record->test_class = (u8)aarch64_import_test_class(record);
        if (aarch64_import_record_is_valid_for_alias(record))
        {
            record->signature_hash = aarch64_import_signature_hash(record);
        }
        record->normalized_form_id = canonical_count++;
        if (aarch64_import_record_is_valid_for_alias(record))
        {
            for (Aarch64ImportRecord* prior = records->first; prior != record; prior = prior->next)
            {
                if (aarch64_import_record_is_valid_for_alias(prior) && prior->coverage_class == record->coverage_class &&
                    prior->signature_hash == record->signature_hash &&
                    aarch64_import_signature_equal(prior, record))
                {
                    record->coverage_class = AARCH64_IMPORT_COVERAGE_ALIAS;
                    record->reason_id = AARCH64_IMPORT_REASON_ALIAS_OF_CANONICAL;
                    record->normalized_form_id = prior->normalized_form_id;
                    canonical_count -= 1;
                    break;
                }
            }
        }
    }
}

typedef struct Aarch64GeneratedEmitIndex Aarch64GeneratedEmitIndex;
struct Aarch64GeneratedEmitIndex
{
    u32 field_first;
    u32 field_count;
    u32 operand_first;
    u32 operand_count;
    u32 predicate_first;
    u32 predicate_count;
};

typedef struct Aarch64GeneratedLookupRecord Aarch64GeneratedLookupRecord;
struct Aarch64GeneratedLookupRecord
{
    Aarch64ImportRecord* record;
    u32 coverage_index;
};

typedef struct Aarch64ImportIdentityKey Aarch64ImportIdentityKey;
struct Aarch64ImportIdentityKey
{
    u64 hash;
    String8 name;
};

BUSTER_GLOBAL_LOCAL int aarch64_import_identity_key_compare(const void* left_pointer, const void* right_pointer)
{
    Aarch64ImportIdentityKey const* left = (Aarch64ImportIdentityKey const*)left_pointer;
    Aarch64ImportIdentityKey const* right = (Aarch64ImportIdentityKey const*)right_pointer;
    int result = (left->hash > right->hash) - (left->hash < right->hash);
    if (!result)
    {
        result = assembly_import_string_compare(&left->name, &right->name);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int aarch64_import_lookup_mnemonic_compare(const void* left_pointer, const void* right_pointer)
{
    Aarch64GeneratedLookupRecord const* left = (Aarch64GeneratedLookupRecord const*)left_pointer;
    Aarch64GeneratedLookupRecord const* right = (Aarch64GeneratedLookupRecord const*)right_pointer;
    int result = assembly_import_string_compare(&left->record->mnemonic, &right->record->mnemonic);
    if (!result)
    {
        result = assembly_import_string_compare(&left->record->name, &right->record->name);
    }
    if (!result)
    {
        result = (left->coverage_index > right->coverage_index) - (left->coverage_index < right->coverage_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int aarch64_import_lookup_signature_compare(const void* left_pointer, const void* right_pointer)
{
    Aarch64GeneratedLookupRecord const* left = (Aarch64GeneratedLookupRecord const*)left_pointer;
    Aarch64GeneratedLookupRecord const* right = (Aarch64GeneratedLookupRecord const*)right_pointer;
    int result = (left->record->signature_hash > right->record->signature_hash) -
                 (left->record->signature_hash < right->record->signature_hash);
    if (!result)
    {
        result = assembly_import_string_compare(&left->record->name, &right->record->name);
    }
    if (!result)
    {
        result = (left->coverage_index > right->coverage_index) - (left->coverage_index < right->coverage_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_validate_identity(Arena* scratch, Aarch64ImportRecordList records)
{
    Aarch64ImportIdentityKey* names = arena_allocate(scratch, Aarch64ImportIdentityKey, records.count);
    Aarch64ImportIdentityKey* sources = arena_allocate(scratch, Aarch64ImportIdentityKey, records.count);
    u32 index = 0;
    String8 previous_name = {0};
    for (Aarch64ImportRecord* record = records.first; record; record = record->next)
    {
        if (previous_name.length && string_equal(previous_name, record->name))
        {
            return false;
        }
        previous_name = record->name;
        names[index] = (Aarch64ImportIdentityKey){.hash = record->name_hash, .name = record->name};
        sources[index] = (Aarch64ImportIdentityKey){.hash = record->source_hash, .name = record->name};
        index += 1;
    }
    qsort(names, records.count, sizeof(names[0]), aarch64_import_identity_key_compare);
    qsort(sources, records.count, sizeof(sources[0]), aarch64_import_identity_key_compare);
    for (index = 1; index < records.count; index += 1)
    {
        if ((names[index - 1].hash == names[index].hash && !string_equal(names[index - 1].name, names[index].name)) ||
            (sources[index - 1].hash == sources[index].hash && !string_equal(sources[index - 1].name, sources[index].name)))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u32 aarch64_import_variable_segment_count(Aarch64ImportVariable* variable)
{
    u32 result = 0;
    Aarch64ImportBit* previous = 0;
    for (Aarch64ImportBit* bit = variable->first_bit; bit; bit = bit->next)
    {
        if (!previous || bit->instruction_bit != previous->instruction_bit + 1 || bit->value_bit != previous->value_bit + 1)
        {
            result += 1;
        }
        previous = bit;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 aarch64_import_variable_index(Aarch64ImportRecord* record, String8 name)
{
    u32 index = 0;
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next, index += 1)
    {
        if (string_equal(variable->name, name))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL Aarch64ImportVariable* aarch64_import_operand_variable(Aarch64ImportRecord* record, String8 name)
{
    Aarch64ImportVariable* result = aarch64_import_variable_find(record, name);
    if (!result && name.length && name.pointer[0] == '_')
    {
        result = aarch64_import_variable_find(record, string_slice(name, 1, name.length));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void aarch64_import_apply_operand_metadata(Aarch64ImportRecord* record)
{
    for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
    {
        Aarch64ImportVariable* variable = aarch64_import_operand_variable(record, operand->name);
        if (variable)
        {
            operand->field_index = aarch64_import_variable_index(record, variable->name);
            if (operand->kind == AARCH64_IMPORT_OPERAND_RELATIVE || variable->relocation != AARCH64_IMPORT_RELOC_NONE)
            {
                operand->kind = AARCH64_IMPORT_OPERAND_RELATIVE;
                // Relocation width and signedness are emitter facts, not a
                // generic bit-width range. Keep the explicit relocation
                // field-end and scale only.
                operand->scale = variable->shift;
                operand->immediate_flags |= AARCH64_IMPORT_OPERAND_SCALE_EXACT;
            }
        }
        else
        {
            operand->field_index = UINT32_MAX;
        }
        if (operand->tied_to != UINT32_MAX && operand->tied_to >= record->operand_count)
        {
            aarch64_import_reason_set(record, AARCH64_IMPORT_REASON_UNPROVEN_TIED_OPERAND);
            operand->tied_to = UINT32_MAX;
        }
    }
    for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
    {
        for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
        {
            Aarch64ImportVariable* candidate = aarch64_import_operand_variable(record, operand->name);
            if (candidate == variable)
            {
                if (operand->direction & AARCH64_IMPORT_OPERAND_WRITE)
                {
                    variable->flags |= AARCH64_IMPORT_FIELD_DESTINATION;
                }
                if (operand->direction & AARCH64_IMPORT_OPERAND_READ)
                {
                    variable->flags |= AARCH64_IMPORT_FIELD_SOURCE;
                }
            }
        }
    }
}

#define AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE 4092u
#define AARCH64_GENERATED_BASE64_RAW_CHUNK_SIZE 3069u

BUSTER_GLOBAL_LOCAL void aarch64_generated_append_c_bytes(Arena* output, const u8* pointer, u32 length)
{
    arena_append_char8(output, '"');
    for (u32 index = 0; index < length; index += 1)
    {
        u8 byte = pointer[index];
        if (byte == '"')
        {
            arena_append_string8(output, S8("\\\""));
        }
        else if (byte == '\\')
        {
            arena_append_string8(output, S8("\\\\"));
        }
        else if (byte >= 0x20 && byte < 0x7f)
        {
            arena_append_char8(output, (char8)byte);
        }
        else
        {
            arena_append_char8(output, '\\');
            arena_append_char8(output, (char8)('0' + ((byte >> 6) & 7)));
            arena_append_char8(output, (char8)('0' + ((byte >> 3) & 7)));
            arena_append_char8(output, (char8)('0' + (byte & 7)));
        }
    }
    arena_append_char8(output, '"');
}

BUSTER_GLOBAL_LOCAL void aarch64_generated_emit_chunk_accessor(Arena* output, String8 name, u32 chunk_count)
{
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL char8 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(u64 logical)\n{\n    u64 chunk = logical / BUSTER_AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE;\n    u64 offset = logical % BUSTER_AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE;\n    switch (chunk)\n    {\n"));
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        arena_append_string8(output, S8("        case "));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8(": return offset < (u64)(sizeof("));
        arena_append_string8(output, name);
        arena_append_string8(output, S8("_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8(") - 1u) ? "));
        arena_append_string8(output, name);
        arena_append_string8(output, S8("_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8("[offset] : 0;\n"));
    }
    arena_append_string8(output, S8("        default: return 0;\n    }\n}\n\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u8 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted(u64 byte_count, u64 offset)\n{\n    u64 encoded_count = 0;\n    if (offset >= byte_count || !buster_aarch64_generated_base64_encoded_count(byte_count, &encoded_count)) return 0;\n    u64 encoded_offset = (offset / 3u) * 4u;\n    if (encoded_offset > encoded_count || encoded_count - encoded_offset < 4u) return 0;\n    u32 value = ((u32)buster_aarch64_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 0u)) << 18) | ((u32)buster_aarch64_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 1u)) << 12) | ((u32)buster_aarch64_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 2u)) << 6) | (u32)buster_aarch64_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 3u));\n    return (u8)(value >> ((2u - (offset % 3u)) * 8u));\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u8 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u16 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted(u64 byte_count, u64 offset)\n{\n    if (!buster_aarch64_generated_blob_range_valid(byte_count, offset, 2u)) return 0;\n    return (u16)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted(byte_count, offset) | ((u16)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted(byte_count, offset + 1u) << 8);\n}\nBUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u16 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n"));

    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u32 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted(u64 byte_count, u64 offset)\n{\n    if (!buster_aarch64_generated_blob_range_valid(byte_count, offset, 4u)) return 0;\n    return (u32)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted(byte_count, offset) | ((u32)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted(byte_count, offset + 2u) << 16);\n}\nBUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u32 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n"));

    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u64 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u64_counted(u64 byte_count, u64 offset)\n{\n    if (!buster_aarch64_generated_blob_range_valid(byte_count, offset, 8u)) return 0;\n    return (u64)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted(byte_count, offset) | ((u64)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted(byte_count, offset + 4u) << 32);\n}\nBUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u64 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u64(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u64_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n"));
    arena_append_string8(output, S8("\n"));
}

BUSTER_GLOBAL_LOCAL void aarch64_generated_emit_chunked_c_array(Arena* output, String8 name, const u8* bytes, u32 byte_count,
                                                                u32 logical_byte_count)
{
    u32 chunk_count = (byte_count + AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE - 1) / AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE;
    u32 data_chunk_count = chunk_count;
    if (!chunk_count)
    {
        chunk_count = 1;
    }
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        arena_append_string8(output, S8("static const char8 "));
        arena_append_string8(output, name);
        arena_append_string8(output, S8("_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8("[] = "));
        if (chunk < data_chunk_count)
        {
            u32 start = chunk * AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE;
            u32 length = BUSTER_MIN(AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE, byte_count - start);
            aarch64_generated_append_c_bytes(output, bytes + start, length);
        }
        else
        {
            arena_append_string8(output, S8("{0}"));
        }
        arena_append_string8(output, S8(";\n"));
    }
    arena_append_string8(output, S8("#define "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT "));
    xed_generated_append_decimal(output, logical_byte_count);
    arena_append_string8(output, S8("\n#define "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_CHUNK_COUNT "));
    xed_generated_append_decimal(output, chunk_count);
    arena_append_string8(output, S8("\n"));
    aarch64_generated_emit_chunk_accessor(output, name, chunk_count);
}

BUSTER_GLOBAL_LOCAL void aarch64_generated_emit_string_pool(Arena* output, Arena* arena, XedGeneratedStringPool* pool,
                                                             XedGeneratedStringNode** sorted)
{
    u8* bytes = arena_allocate(arena, u8, pool->byte_count);
    memset(bytes, 0, pool->byte_count);
    u32 offset = 1;
    for (u32 index = 0; index < pool->count; index += 1)
    {
        XedGeneratedStringNode* node = sorted[index];
        memcpy(bytes + offset, node->string.pointer, node->string.length);
        offset += (u32)node->string.length + 1;
    }
    aarch64_generated_emit_chunked_c_array(output, S8("buster_aarch64_generated_string_pool"), bytes, pool->byte_count, pool->byte_count);
    arena_append_string8(output, S8("#define buster_aarch64_generated_string_byte(offset) \\\n    ((char8)(((u64)(offset) < (u64)BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE) ? \\\n             buster_aarch64_generated_string_pool_char(offset) : 0))\n\n"));
    arena_append_string8(output, S8("#define BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE "));
    xed_generated_append_decimal(output, pool->byte_count);
    arena_append_string8(output, S8("\n\n"));
}

typedef struct Aarch64GeneratedBlob Aarch64GeneratedBlob;
struct Aarch64GeneratedBlob
{
    u8* bytes;
    u32 count;
    u32 capacity;
};

BUSTER_GLOBAL_LOCAL Aarch64GeneratedBlob aarch64_generated_blob_make(Arena* arena, u64 capacity)
{
    Aarch64GeneratedBlob result = {0};
    if (capacity <= UINT32_MAX)
    {
        result.bytes = capacity ? arena_allocate(arena, u8, capacity) : 0;
        result.capacity = (u32)capacity;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool aarch64_generated_blob_append_u8(Aarch64GeneratedBlob* blob, u8 value)
{
    if (!blob || blob->count >= blob->capacity)
    {
        return false;
    }
    blob->bytes[blob->count++] = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_generated_blob_append_u16(Aarch64GeneratedBlob* blob, u16 value)
{
    return aarch64_generated_blob_append_u8(blob, (u8)value) && aarch64_generated_blob_append_u8(blob, (u8)(value >> 8));
}

BUSTER_GLOBAL_LOCAL bool aarch64_generated_blob_append_u32(Aarch64GeneratedBlob* blob, u32 value)
{
    return aarch64_generated_blob_append_u8(blob, (u8)value) && aarch64_generated_blob_append_u8(blob, (u8)(value >> 8)) &&
           aarch64_generated_blob_append_u8(blob, (u8)(value >> 16)) && aarch64_generated_blob_append_u8(blob, (u8)(value >> 24));
}

BUSTER_GLOBAL_LOCAL bool aarch64_generated_blob_append_u64(Aarch64GeneratedBlob* blob, u64 value)
{
    return aarch64_generated_blob_append_u32(blob, (u32)value) && aarch64_generated_blob_append_u32(blob, (u32)(value >> 32));
}

BUSTER_GLOBAL_LOCAL void aarch64_generated_append_base64_blob(Arena* output, String8 name, Aarch64GeneratedBlob blob)
{
    static const char8 alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u64 encoded_byte_count = ((u64)blob.count + 2u) / 3u * 4u;
    u32 chunk_count = (u32)((encoded_byte_count + AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE - 1) / AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE);
    if (!chunk_count)
    {
        chunk_count = 1;
    }
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 start = chunk * AARCH64_GENERATED_BASE64_RAW_CHUNK_SIZE;
        u32 length = start < blob.count ? BUSTER_MIN(AARCH64_GENERATED_BASE64_RAW_CHUNK_SIZE, blob.count - start) : 0;
        char8 encoded[AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE + 1u];
        u32 encoded_count = 0;
        for (u32 index = 0; index < length; index += 3)
        {
            u32 remaining = length - index;
            u32 value = ((u32)blob.bytes[start + index] << 16) | (remaining > 1 ? (u32)blob.bytes[start + index + 1] << 8 : 0) |
                        (remaining > 2 ? blob.bytes[start + index + 2] : 0);
            encoded[encoded_count++] = alphabet[(value >> 18) & 63];
            encoded[encoded_count++] = alphabet[(value >> 12) & 63];
            encoded[encoded_count++] = remaining > 1 ? alphabet[(value >> 6) & 63] : '=';
            encoded[encoded_count++] = remaining > 2 ? alphabet[value & 63] : '=';
        }
        arena_append_string8(output, S8("static const char8 "));
        arena_append_string8(output, name);
        arena_append_string8(output, S8("_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8("[] = "));
        if (encoded_count)
        {
            aarch64_generated_append_c_bytes(output, (u8*)encoded, encoded_count);
        }
        else
        {
            arena_append_string8(output, S8("{0}"));
        }
        arena_append_string8(output, S8(";\n"));
    }
    arena_append_string8(output, S8("#define "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT "));
    xed_generated_append_decimal(output, blob.count);
    arena_append_string8(output, S8("\n#define "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_CHUNK_COUNT "));
    xed_generated_append_decimal(output, chunk_count);
    arena_append_string8(output, S8("\n"));
    aarch64_generated_emit_chunk_accessor(output, name, chunk_count);
}

BUSTER_GLOBAL_LOCAL void aarch64_generated_emit_preamble(Arena* output)
{
    arena_append_string8(output,
                         S8("/* Generated by build import_assembly_metadata; do not edit. */\n"
                            "#ifndef BUSTER_AARCH64_ASSEMBLY_GENERATED_H\n"
                            "#define BUSTER_AARCH64_ASSEMBLY_GENERATED_H\n"
                            "#include <buster/lib/base.h>\n\n"
                            "#define BUSTER_AARCH64_GENERATED_SCHEMA_VERSION 4\n"
                            "#define BUSTER_AARCH64_GENERATED_AUDIT_ONLY 1\n\n"
                            "typedef enum BusterAarch64GeneratedCoverageClass {\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_DIRECT,\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_NORMALIZED,\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_ALIAS,\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_PRIVILEGED_SYSTEM,\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_RESERVED_UNENCODABLE,\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_UNSUPPORTED_TOKEN,\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_UNCLASSIFIED,\n"
                            "    BUSTER_AARCH64_GENERATED_COVERAGE_CLASS_COUNT,\n"
                            "} BusterAarch64GeneratedCoverageClass;\n\n"
                            "typedef enum BusterAarch64GeneratedEncoderFamily {\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_SCALAR_INTEGER,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_BRANCH,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_LOAD_STORE,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_FP_SIMD_NEON,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_SVE_SVE2,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_SME_SME2,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_MTE,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_ATOMIC_LSE,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_CRYPTO,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_SYSTEM,\n"
                            "    BUSTER_AARCH64_GENERATED_ENCODER_COUNT,\n"
                            "} BusterAarch64GeneratedEncoderFamily;\n\n"
                            "typedef enum BusterAarch64GeneratedTestClass {\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_SCALAR,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_BRANCH,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_MEMORY,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_SIMD_LIST_LANE,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_SVE_PREDICATE,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_SME_SYSTEM,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_IMMEDIATE,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_SCHEMA,\n"
                            "    BUSTER_AARCH64_GENERATED_TEST_COUNT,\n"
                            "} BusterAarch64GeneratedTestClass;\n\n"
                            "typedef enum BusterAarch64GeneratedReason {\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_NONE,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_ALIAS_OF_CANONICAL,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_SYSTEM_OR_PRIVILEGED,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNMAPPED_VARIABLE,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_CONFLICTING_BIT_ASSIGNMENT,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_MALFORMED_DAG,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_MALFORMED_TEMPLATE,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNKNOWN_FIELD,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNKNOWN_PREDICATE,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_MISSING_OPERAND,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_INVALID_JSON,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_NULL_FIELD,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_FIELD_SEMANTICS,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_OPERAND_KIND,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_IMMEDIATE_RANGE,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_MEMORY_FORM,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_TIED_OPERAND,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNPROVEN_CORRESPONDENCE,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_UNSUPPORTED_ADDRESS_GRAMMAR,\n"
                            "    BUSTER_AARCH64_GENERATED_REASON_COUNT,\n"
                            "} BusterAarch64GeneratedReason;\n\n"
                            "typedef enum BusterAarch64GeneratedFieldTransform {\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_NONE,\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_REGISTER,\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_IMMEDIATE,\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_CONDITION,\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_SHIFT_EXTEND,\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_LANE,\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_PC_RELATIVE,\n"
                            "    BUSTER_AARCH64_GENERATED_TRANSFORM_SYSTEM,\n"
                            "} BusterAarch64GeneratedFieldTransform;\n\n"
                            "typedef enum BusterAarch64GeneratedRelocationKind {\n"
                            "    BUSTER_AARCH64_GENERATED_RELOC_NONE,\n"
                            "    BUSTER_AARCH64_GENERATED_RELOC_BRANCH26,\n"
                            "    BUSTER_AARCH64_GENERATED_RELOC_COND_BRANCH19,\n"
                            "    BUSTER_AARCH64_GENERATED_RELOC_TEST_BRANCH14,\n"
                            "    BUSTER_AARCH64_GENERATED_RELOC_LITERAL19,\n"
                            "    BUSTER_AARCH64_GENERATED_RELOC_ADR21,\n"
                            "    BUSTER_AARCH64_GENERATED_RELOC_ADRP21,\n"
                            "} BusterAarch64GeneratedRelocationKind;\n\n"
                            "typedef enum BusterAarch64GeneratedOperandKind {\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_REGISTER,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_MEMORY,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_IMMEDIATE,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_RELATIVE,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_COMPOUND,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_SYSTEM,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_LIST,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_UNPROVEN,\n"
                            "} BusterAarch64GeneratedOperandKind;\n\n"
                            "typedef enum BusterAarch64GeneratedAddressKind {\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_NONE,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_BASE,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_BASE_OFFSET,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_BASE_INDEX,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_LITERAL,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_BRANCH,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_SYSTEM,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_UNPROVEN,\n"
                            "    BUSTER_AARCH64_GENERATED_ADDRESS_COUNT,\n"
                            "} BusterAarch64GeneratedAddressKind;\n\n"
                            "enum {\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_READ = 1u << 0,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_WRITE = 1u << 1,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_TIED = 1u << 2,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_SUPPRESSED = 1u << 3,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_IMPLICIT = 1u << 4,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_OPTIONAL = 1u << 5,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_FLAG_LIST = 1u << 6,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_LANE = 1u << 7,\n"
                            "};\n"
                            "enum {\n"
                            "    BUSTER_AARCH64_GENERATED_FIELD_DESTINATION = 1u << 0,\n"
                            "    BUSTER_AARCH64_GENERATED_FIELD_SOURCE = 1u << 1,\n"
                            "    BUSTER_AARCH64_GENERATED_FIELD_IMMEDIATE = 1u << 2,\n"
                            "    BUSTER_AARCH64_GENERATED_FIELD_PC_RELATIVE = 1u << 3,\n"
                            "    BUSTER_AARCH64_GENERATED_FIELD_UNMAPPED = 1u << 4,\n"
                            "};\n\n"
                            "enum {\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_IMMEDIATE_RANGE_EXACT = 1u << 0,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_IMMEDIATE_SIGNED = 1u << 1,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_SCALE_EXACT = 1u << 2,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_ADDRESS_EXACT = 1u << 3,\n"
                            "    BUSTER_AARCH64_GENERATED_OPERAND_REGISTER_WIDTH_EXACT = 1u << 4,\n"
                            "};\n\n"
                            "typedef struct BusterAarch64GeneratedBitSegment BusterAarch64GeneratedBitSegment;\n"
                            "struct BusterAarch64GeneratedBitSegment { u8 instruction_lsb; u8 width; u8 value_lsb; u8 reserved; };\n\n"
                            "typedef struct BusterAarch64GeneratedField BusterAarch64GeneratedField;\n"
                            "struct BusterAarch64GeneratedField {\n"
                            "    u32 name_offset; u32 segment_first; u32 source_mask;\n"
                            "    u8 width; u8 segment_count; u8 transform; u8 relocation;\n"
                            "    u8 relocation_end; u8 shift; u8 flags; u8 reserved;\n"
                            "};\n\n"
                            "typedef struct BusterAarch64GeneratedOperand BusterAarch64GeneratedOperand;\n"
                            "struct BusterAarch64GeneratedOperand {\n"
                            "    u32 syntax_offset; u32 type_offset; u32 name_offset; u32 field_index;\n"
                            "    s32 immediate_min; s32 immediate_max; u16 register_width; u32 tied_to;\n"
                            "    u16 address_base_index; u16 address_offset_index;\n"
                            "    u8 direction; u8 kind; u8 flags; u8 scale; u8 immediate_flags; u8 address_kind; u8 address_flags; u8 reserved;\n"
                            "};\n\n"
                            "typedef struct BusterAarch64GeneratedForm BusterAarch64GeneratedForm;\n"
                            "struct BusterAarch64GeneratedForm {\n"
                            "    u64 source_hash; u64 name_hash; u64 signature_hash; u32 name_offset; u32 mnemonic_offset; u32 asm_offset; u32 out_offset; u32 in_offset;\n"
                            "    u32 field_first; u32 operand_first; u32 predicate_first; u32 normalized_form_id;\n"
                            "    u16 field_count; u16 operand_count; u16 predicate_count; u16 reserved0;\n"
                            "    u32 fixed_mask; u32 fixed_value; u8 coverage_class; u8 encoder_family; u8 test_class; u8 reason_id;\n"
                            "    u8 asm_flags; u8 address_kind; u8 address_flags; u8 reserved1;\n"
                            "    u16 address_base_index; u16 address_offset_index;\n"
                            "};\n\n"
                            "typedef struct BusterAarch64GeneratedCoverage BusterAarch64GeneratedCoverage;\n"
                            "struct BusterAarch64GeneratedCoverage {\n"
                            "    u64 source_hash; u64 name_hash; u32 name_offset; u32 normalized_form_id;\n"
                            "    u8 coverage_class; u8 encoder_family; u8 test_class; u8 reason_id;\n"
                            "};\n\n"
                            "typedef struct BusterAarch64GeneratedMnemonicRange BusterAarch64GeneratedMnemonicRange;\n"
                            "struct BusterAarch64GeneratedMnemonicRange { u32 key_offset; u32 candidate_first; u32 candidate_count; };\n"
                            "typedef struct BusterAarch64GeneratedSignatureRange BusterAarch64GeneratedSignatureRange;\n"
                            "struct BusterAarch64GeneratedSignatureRange { u64 signature_hash; u32 candidate_first; u32 candidate_count; };\n\n"
                            "#define BUSTER_AARCH64_GENERATED_PACKED_BASE64 1\n"
                            "#define BUSTER_AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE 4092u\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u8 buster_aarch64_generated_base64_value(char8 character)\n"
                            "{\n"
                            "    if (character >= 'A' && character <= 'Z') return (u8)(character - 'A');\n"
                            "    if (character >= 'a' && character <= 'z') return (u8)(character - 'a' + 26);\n"
                            "    if (character >= '0' && character <= '9') return (u8)(character - '0' + 52);\n"
                            "    if (character == '+') return 62;\n"
                            "    if (character == '/') return 63;\n"
                            "    return 0;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE bool buster_aarch64_generated_blob_range_valid(u64 byte_count, u64 offset, u64 length)\n"
                            "{\n"
                            "    return offset <= byte_count && length <= byte_count - offset;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE bool buster_aarch64_generated_base64_encoded_count(u64 byte_count, u64* encoded_count)\n"
                            "{\n"
                            "    if (!encoded_count || byte_count > UINT64_MAX - 2u) return false;\n"
                            "    u64 group_count = (byte_count + 2u) / 3u;\n"
                            "    if (group_count > UINT64_MAX / 4u) return false;\n"
                            "    *encoded_count = group_count * 4u;\n"
                            "    return true;\n"
                            "}\n"
                            "#define BUSTER_AARCH64_GENERATED_BLOB_COUNT_(blob) blob##_BYTE_COUNT\n"
                            "#define BUSTER_AARCH64_GENERATED_BLOB_COUNT(blob) BUSTER_AARCH64_GENERATED_BLOB_COUNT_(blob)\n"
                            "#define BUSTER_AARCH64_GENERATED_BLOB_CHUNK_COUNT_(blob) blob##_CHUNK_COUNT\n"
                            "#define BUSTER_AARCH64_GENERATED_BLOB_CHUNK_COUNT(blob) BUSTER_AARCH64_GENERATED_BLOB_CHUNK_COUNT_(blob)\n"
                            "#define buster_aarch64_generated_blob_char_in_bounds(blob, byte_count, offset) \\\n"
                            "    ((u64)(byte_count) <= (UINT64_MAX / 4u) * 3u && \\\n"
                            "     (u64)(offset) < ((((u64)(byte_count) + 2u) / 3u) * 4u) && \\\n"
                            "     (u64)(offset) < (u64)BUSTER_AARCH64_GENERATED_BLOB_CHUNK_COUNT(blob) * (u64)BUSTER_AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE)\n"
                            "#define buster_aarch64_generated_blob_char(blob, byte_count, offset) \\\n"
                            "    ((char8)(buster_aarch64_generated_blob_char_in_bounds(blob, byte_count, offset) ? blob##_char(offset) : 0))\n"
                            "#define buster_aarch64_generated_blob_u8_counted(blob, byte_count, offset) \\\n"
                            "    blob##_u8_counted(byte_count, offset)\n"
                            "#define buster_aarch64_generated_blob_u8(blob, offset) \\\n"
                            "    blob##_u8(offset)\n"
                            "#define buster_aarch64_generated_blob_u16_counted(blob, byte_count, offset) \\\n"
                            "    blob##_u16_counted(byte_count, offset)\n"
                            "#define buster_aarch64_generated_blob_u16(blob, offset) \\\n"
                            "    blob##_u16(offset)\n"
                            "#define buster_aarch64_generated_blob_u32_counted(blob, byte_count, offset) \\\n"
                            "    blob##_u32_counted(byte_count, offset)\n"
                            "#define buster_aarch64_generated_blob_u32(blob, offset) \\\n"
                            "    blob##_u32(offset)\n"
                            "#define buster_aarch64_generated_blob_u64_counted(blob, byte_count, offset) \\\n"
                            "    blob##_u64_counted(byte_count, offset)\n"
                            "#define buster_aarch64_generated_blob_u64(blob, offset) \\\n"
                            "    blob##_u64(offset)\n\n"));
}

BUSTER_GLOBAL_LOCAL void aarch64_generated_emit_blob_accessors(Arena* output)
{
    arena_append_string8(output,
                         S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterAarch64GeneratedBitSegment buster_aarch64_generated_segment_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_SEGMENT_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_segments_blob_BYTE_COUNT, (u64)index * 4u, 4u)) return (BusterAarch64GeneratedBitSegment){0};\n"
                            "    u64 offset = (u64)index * 4u;\n"
                            "    return (BusterAarch64GeneratedBitSegment){buster_aarch64_generated_blob_u8(buster_aarch64_generated_segments_blob, offset),\n"
                            "                                                     buster_aarch64_generated_blob_u8(buster_aarch64_generated_segments_blob, offset + 1),\n"
                            "                                                     buster_aarch64_generated_blob_u8(buster_aarch64_generated_segments_blob, offset + 2),\n"
                            "                                                     buster_aarch64_generated_blob_u8(buster_aarch64_generated_segments_blob, offset + 3)};\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterAarch64GeneratedField buster_aarch64_generated_field_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_FIELD_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_fields_blob_BYTE_COUNT, (u64)index * 20u, 20u)) return (BusterAarch64GeneratedField){0};\n"
                            "    u64 offset = (u64)index * 20u;\n"
                            "    BusterAarch64GeneratedField result = (BusterAarch64GeneratedField){buster_aarch64_generated_blob_u32(buster_aarch64_generated_fields_blob, offset),\n"
                            "                                             buster_aarch64_generated_blob_u32(buster_aarch64_generated_fields_blob, offset + 4),\n"
                            "                                             buster_aarch64_generated_blob_u32(buster_aarch64_generated_fields_blob, offset + 8),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 12),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 13),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 14),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 15),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 16),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 17),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 18),\n"
                            "                                             buster_aarch64_generated_blob_u8(buster_aarch64_generated_fields_blob, offset + 19)};\n"
                            "    if (result.name_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || !buster_aarch64_generated_blob_range_valid(BUSTER_AARCH64_GENERATED_SEGMENT_COUNT, result.segment_first, result.segment_count)) return (BusterAarch64GeneratedField){0};\n"
                            "    return result;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterAarch64GeneratedOperand buster_aarch64_generated_operand_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_OPERAND_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_operands_blob_BYTE_COUNT, (u64)index * 42u, 42u)) return (BusterAarch64GeneratedOperand){0};\n"
                            "    u64 offset = (u64)index * 42u;\n"
                            "    BusterAarch64GeneratedOperand result = (BusterAarch64GeneratedOperand){buster_aarch64_generated_blob_u32(buster_aarch64_generated_operands_blob, offset),\n"
                            "                                               buster_aarch64_generated_blob_u32(buster_aarch64_generated_operands_blob, offset + 4),\n"
                            "                                               buster_aarch64_generated_blob_u32(buster_aarch64_generated_operands_blob, offset + 8),\n"
                            "                                               buster_aarch64_generated_blob_u32(buster_aarch64_generated_operands_blob, offset + 12),\n"
                            "                                               (s32)buster_aarch64_generated_blob_u32(buster_aarch64_generated_operands_blob, offset + 16),\n"
                            "                                               (s32)buster_aarch64_generated_blob_u32(buster_aarch64_generated_operands_blob, offset + 20),\n"
                            "                                               buster_aarch64_generated_blob_u16(buster_aarch64_generated_operands_blob, offset + 24),\n"
                            "                                               buster_aarch64_generated_blob_u32(buster_aarch64_generated_operands_blob, offset + 26),\n"
                            "                                               buster_aarch64_generated_blob_u16(buster_aarch64_generated_operands_blob, offset + 30),\n"
                            "                                               buster_aarch64_generated_blob_u16(buster_aarch64_generated_operands_blob, offset + 32),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 34),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 35),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 36),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 37),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 38),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 39),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 40),\n"
                            "                                               buster_aarch64_generated_blob_u8(buster_aarch64_generated_operands_blob, offset + 41)};\n"
                            "    if (result.syntax_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || result.type_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || result.name_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||\n"
                            "        (result.field_index != UINT32_MAX && result.field_index >= BUSTER_AARCH64_GENERATED_FIELD_COUNT) ||\n"
                            "        (result.tied_to != UINT32_MAX && result.tied_to >= BUSTER_AARCH64_GENERATED_OPERAND_COUNT)) return (BusterAarch64GeneratedOperand){0};\n"
                            "    return result;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u32 buster_aarch64_generated_predicate_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_PREDICATE_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_predicates_blob_BYTE_COUNT, (u64)index * 4u, 4u)) return 0;\n"
                            "    u32 result = buster_aarch64_generated_blob_u32(buster_aarch64_generated_predicates_blob, (u64)index * 4u);\n"
                            "    return result < BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ? result : 0;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterAarch64GeneratedForm buster_aarch64_generated_form_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_FORM_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_forms_blob_BYTE_COUNT, (u64)index * 88u, 88u)) return (BusterAarch64GeneratedForm){0};\n"
                            "    u64 offset = (u64)index * 88u;\n"
                            "    BusterAarch64GeneratedForm result = (BusterAarch64GeneratedForm){buster_aarch64_generated_blob_u64(buster_aarch64_generated_forms_blob, offset),\n"
                            "                                           buster_aarch64_generated_blob_u64(buster_aarch64_generated_forms_blob, offset + 8),\n"
                            "                                           buster_aarch64_generated_blob_u64(buster_aarch64_generated_forms_blob, offset + 16),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 24),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 28),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 32),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 36),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 40),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 44),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 48),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 52),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 56),\n"
                            "                                           buster_aarch64_generated_blob_u16(buster_aarch64_generated_forms_blob, offset + 60),\n"
                            "                                           buster_aarch64_generated_blob_u16(buster_aarch64_generated_forms_blob, offset + 62),\n"
                            "                                           buster_aarch64_generated_blob_u16(buster_aarch64_generated_forms_blob, offset + 64),\n"
                            "                                           buster_aarch64_generated_blob_u16(buster_aarch64_generated_forms_blob, offset + 66),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 68),\n"
                            "                                           buster_aarch64_generated_blob_u32(buster_aarch64_generated_forms_blob, offset + 72),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 76),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 77),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 78),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 79),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 80),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 81),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 82),\n"
                            "                                           buster_aarch64_generated_blob_u8(buster_aarch64_generated_forms_blob, offset + 83),\n"
                            "                                           buster_aarch64_generated_blob_u16(buster_aarch64_generated_forms_blob, offset + 84),\n"
                            "                                           buster_aarch64_generated_blob_u16(buster_aarch64_generated_forms_blob, offset + 86)};\n"
                            "    if (result.name_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || result.mnemonic_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||\n"
                            "        result.asm_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || result.out_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE ||\n"
                            "        result.in_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || result.normalized_form_id >= BUSTER_AARCH64_GENERATED_FORM_COUNT ||\n"
                            "        !buster_aarch64_generated_blob_range_valid(BUSTER_AARCH64_GENERATED_FIELD_COUNT, result.field_first, result.field_count) ||\n"
                            "        !buster_aarch64_generated_blob_range_valid(BUSTER_AARCH64_GENERATED_OPERAND_COUNT, result.operand_first, result.operand_count) ||\n"
                            "        !buster_aarch64_generated_blob_range_valid(BUSTER_AARCH64_GENERATED_PREDICATE_COUNT, result.predicate_first, result.predicate_count)) return (BusterAarch64GeneratedForm){0};\n"
                            "    return result;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterAarch64GeneratedMnemonicRange buster_aarch64_generated_mnemonic_range_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_MNEMONIC_RANGE_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_mnemonic_ranges_blob_BYTE_COUNT, (u64)index * 12u, 12u)) return (BusterAarch64GeneratedMnemonicRange){0};\n"
                            "    u64 offset = (u64)index * 12u;\n"
                            "    BusterAarch64GeneratedMnemonicRange result = (BusterAarch64GeneratedMnemonicRange){buster_aarch64_generated_blob_u32(buster_aarch64_generated_mnemonic_ranges_blob, offset),\n"
                            "                                                     buster_aarch64_generated_blob_u32(buster_aarch64_generated_mnemonic_ranges_blob, offset + 4),\n"
                            "                                                     buster_aarch64_generated_blob_u32(buster_aarch64_generated_mnemonic_ranges_blob, offset + 8)};\n"
                            "    if (result.key_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || !buster_aarch64_generated_blob_range_valid(BUSTER_AARCH64_GENERATED_MNEMONIC_CANDIDATE_COUNT, result.candidate_first, result.candidate_count)) return (BusterAarch64GeneratedMnemonicRange){0};\n"
                            "    return result;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u32 buster_aarch64_generated_mnemonic_candidate_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_MNEMONIC_CANDIDATE_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_mnemonic_candidates_blob_BYTE_COUNT, (u64)index * 4u, 4u)) return UINT32_MAX;\n"
                            "    u32 result = buster_aarch64_generated_blob_u32(buster_aarch64_generated_mnemonic_candidates_blob, (u64)index * 4u);\n"
                            "    return result < BUSTER_AARCH64_GENERATED_COVERAGE_ROW_COUNT ? result : UINT32_MAX;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterAarch64GeneratedSignatureRange buster_aarch64_generated_signature_range_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_SIGNATURE_RANGE_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_signature_ranges_blob_BYTE_COUNT, (u64)index * 16u, 16u)) return (BusterAarch64GeneratedSignatureRange){0};\n"
                            "    u64 offset = (u64)index * 16u;\n"
                            "    BusterAarch64GeneratedSignatureRange result = (BusterAarch64GeneratedSignatureRange){buster_aarch64_generated_blob_u64(buster_aarch64_generated_signature_ranges_blob, offset),\n"
                            "                                                      buster_aarch64_generated_blob_u32(buster_aarch64_generated_signature_ranges_blob, offset + 8),\n"
                            "                                                      buster_aarch64_generated_blob_u32(buster_aarch64_generated_signature_ranges_blob, offset + 12)};\n"
                            "    if (!buster_aarch64_generated_blob_range_valid(BUSTER_AARCH64_GENERATED_SIGNATURE_CANDIDATE_COUNT, result.candidate_first, result.candidate_count)) return (BusterAarch64GeneratedSignatureRange){0};\n"
                            "    return result;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u32 buster_aarch64_generated_signature_candidate_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_SIGNATURE_CANDIDATE_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_signature_candidates_blob_BYTE_COUNT, (u64)index * 4u, 4u)) return UINT32_MAX;\n"
                            "    u32 result = buster_aarch64_generated_blob_u32(buster_aarch64_generated_signature_candidates_blob, (u64)index * 4u);\n"
                            "    return result < BUSTER_AARCH64_GENERATED_COVERAGE_ROW_COUNT ? result : UINT32_MAX;\n"
                            "}\n\n"));
}

BUSTER_GLOBAL_LOCAL bool aarch64_generated_emit_packed_tables(Arena* output, Arena* coverage_output, Arena* scratch,
                                                              Aarch64ImportRecordList records, Aarch64ImportRecord** canonical,
                                                              u32 canonical_count, XedGeneratedStringPool* pool,
                                                              XedGeneratedStringNode** sorted, Aarch64GeneratedEmitIndex* indexes,
                                                              u32 field_cursor, u32 segment_cursor, u32 operand_cursor,
                                                              u32 predicate_cursor, Aarch64GeneratedLookupRecord* mnemonic_order,
                                                              u32 mnemonic_count, Aarch64GeneratedLookupRecord* signature_order,
                                                              u32 signature_count, u32 mnemonic_range_count, u32 signature_range_count,
                                                              Aarch64GeneratedTableStats* stats)
{
    if (canonical_count > UINT32_MAX / 88 || field_cursor > UINT32_MAX / 20 || segment_cursor > UINT32_MAX / 4 ||
        operand_cursor > UINT32_MAX / 42 || predicate_cursor > UINT32_MAX / 4 || records.count > UINT32_MAX / 28 ||
        mnemonic_range_count > UINT32_MAX / 12 || mnemonic_count > UINT32_MAX / 4 || signature_range_count > UINT32_MAX / 16 ||
        signature_count > UINT32_MAX / 4)
    {
        return false;
    }
    Aarch64GeneratedBlob segments = aarch64_generated_blob_make(scratch, (u64)segment_cursor * 4);
    Aarch64GeneratedBlob fields = aarch64_generated_blob_make(scratch, (u64)field_cursor * 20);
    Aarch64GeneratedBlob operands = aarch64_generated_blob_make(scratch, (u64)operand_cursor * 42);
    Aarch64GeneratedBlob predicates = aarch64_generated_blob_make(scratch, (u64)predicate_cursor * 4);
    Aarch64GeneratedBlob forms = aarch64_generated_blob_make(scratch, (u64)canonical_count * 88);
    Aarch64GeneratedBlob mnemonic_ranges = aarch64_generated_blob_make(scratch, (u64)mnemonic_range_count * 12);
    Aarch64GeneratedBlob mnemonic_candidates = aarch64_generated_blob_make(scratch, (u64)mnemonic_count * 4);
    Aarch64GeneratedBlob signature_ranges = aarch64_generated_blob_make(scratch, (u64)signature_range_count * 16);
    Aarch64GeneratedBlob signature_candidates = aarch64_generated_blob_make(scratch, (u64)signature_count * 4);
    Aarch64GeneratedBlob coverage = aarch64_generated_blob_make(scratch, (u64)records.count * 28);
    bool valid = (segment_cursor == 0 || segments.bytes) && (field_cursor == 0 || fields.bytes) && (operand_cursor == 0 || operands.bytes) &&
                 (predicate_cursor == 0 || predicates.bytes) && (canonical_count == 0 || forms.bytes) &&
                 (mnemonic_range_count == 0 || mnemonic_ranges.bytes) && (mnemonic_count == 0 || mnemonic_candidates.bytes) &&
                 (signature_range_count == 0 || signature_ranges.bytes) && (signature_count == 0 || signature_candidates.bytes) &&
                 (records.count == 0 || coverage.bytes);
    for (u32 record_index = 0; valid && record_index < canonical_count; record_index += 1)
    {
        Aarch64ImportRecord* record = canonical[record_index];
        u32 field_first = indexes[record_index].field_first;
        u32 segment_first = segments.count / 4;
        for (Aarch64ImportVariable* variable = record->first_variable; valid && variable; variable = variable->next)
        {
            u32 variable_segment_first = segments.count / 4;
            Aarch64ImportBit* previous = 0;
            for (Aarch64ImportBit* bit = variable->first_bit; valid && bit; bit = bit->next)
            {
                if (!previous || bit->instruction_bit != previous->instruction_bit + 1 || bit->value_bit != previous->value_bit + 1)
                {
                    u32 width = 1;
                    Aarch64ImportBit* last = bit;
                    for (Aarch64ImportBit* end = bit->next; end && end->instruction_bit == last->instruction_bit + 1 &&
                                                         end->value_bit == last->value_bit + 1; end = end->next)
                    {
                        width += 1;
                        last = end;
                    }
                    valid = width <= UINT8_MAX && aarch64_generated_blob_append_u8(&segments, bit->instruction_bit) &&
                            aarch64_generated_blob_append_u8(&segments, (u8)width) &&
                            aarch64_generated_blob_append_u8(&segments, bit->value_bit) &&
                            aarch64_generated_blob_append_u8(&segments, 0);
                }
                previous = bit;
            }
            u32 variable_segment_count = segments.count / 4 - variable_segment_first;
            valid = valid && variable_segment_count <= UINT8_MAX && aarch64_generated_blob_append_u32(&fields, xed_generated_string_offset(pool, variable->name)) &&
                    aarch64_generated_blob_append_u32(&fields, variable_segment_first) &&
                    aarch64_generated_blob_append_u32(&fields, variable->source_mask) &&
                    aarch64_generated_blob_append_u8(&fields, variable->width) &&
                    aarch64_generated_blob_append_u8(&fields, (u8)variable_segment_count) &&
                    aarch64_generated_blob_append_u8(&fields, variable->transform) &&
                    aarch64_generated_blob_append_u8(&fields, variable->relocation) &&
                    aarch64_generated_blob_append_u8(&fields, variable->relocation_end) &&
                    aarch64_generated_blob_append_u8(&fields, variable->shift) &&
                    aarch64_generated_blob_append_u8(&fields, variable->flags) && aarch64_generated_blob_append_u8(&fields, 0);
        }
        valid = valid && field_first == fields.count / 20 - record->variable_count;
        for (Aarch64ImportOperand* operand = record->first_operand; valid && operand; operand = operand->next)
        {
            u32 field_index = operand->field_index == UINT32_MAX ? UINT32_MAX : field_first + operand->field_index;
            u32 operand_tied_to = operand->tied_to == UINT32_MAX ? UINT32_MAX : indexes[record_index].operand_first + operand->tied_to;
            valid = aarch64_generated_blob_append_u32(&operands, xed_generated_string_offset(pool, operand->syntax)) &&
                    aarch64_generated_blob_append_u32(&operands, xed_generated_string_offset(pool, operand->type)) &&
                    aarch64_generated_blob_append_u32(&operands, xed_generated_string_offset(pool, operand->name)) &&
                    aarch64_generated_blob_append_u32(&operands, field_index) &&
                    aarch64_generated_blob_append_u32(&operands, (u32)operand->immediate_min) &&
                    aarch64_generated_blob_append_u32(&operands, (u32)operand->immediate_max) &&
                    aarch64_generated_blob_append_u16(&operands, operand->register_width) &&
                    aarch64_generated_blob_append_u32(&operands, operand_tied_to) &&
                    aarch64_generated_blob_append_u16(&operands, operand->address_base_index) &&
                    aarch64_generated_blob_append_u16(&operands, operand->address_offset_index) &&
                    aarch64_generated_blob_append_u8(&operands, operand->direction) && aarch64_generated_blob_append_u8(&operands, operand->kind) &&
                    aarch64_generated_blob_append_u8(&operands, operand->flags) && aarch64_generated_blob_append_u8(&operands, operand->scale) &&
                    aarch64_generated_blob_append_u8(&operands, operand->immediate_flags) && aarch64_generated_blob_append_u8(&operands, operand->address_kind) &&
                    aarch64_generated_blob_append_u8(&operands, operand->address_flags) && aarch64_generated_blob_append_u8(&operands, 0);
        }
        for (Aarch64ImportPredicate* predicate = record->first_predicate; valid && predicate; predicate = predicate->next)
        {
            valid = aarch64_generated_blob_append_u32(&predicates, xed_generated_string_offset(pool, predicate->name));
        }
        valid = valid && aarch64_generated_blob_append_u64(&forms, record->source_hash) && aarch64_generated_blob_append_u64(&forms, record->name_hash) &&
                aarch64_generated_blob_append_u64(&forms, record->signature_hash) && aarch64_generated_blob_append_u32(&forms, xed_generated_string_offset(pool, record->name)) &&
                aarch64_generated_blob_append_u32(&forms, xed_generated_string_offset(pool, record->mnemonic)) &&
                aarch64_generated_blob_append_u32(&forms, xed_generated_string_offset(pool, record->assembly)) &&
                aarch64_generated_blob_append_u32(&forms, xed_generated_string_offset(pool, record->output_operands)) &&
                aarch64_generated_blob_append_u32(&forms, xed_generated_string_offset(pool, record->input_operands)) &&
                aarch64_generated_blob_append_u32(&forms, indexes[record_index].field_first) &&
                aarch64_generated_blob_append_u32(&forms, indexes[record_index].operand_first) &&
                aarch64_generated_blob_append_u32(&forms, indexes[record_index].predicate_first) &&
                aarch64_generated_blob_append_u32(&forms, record_index) &&
                aarch64_generated_blob_append_u16(&forms, (u16)indexes[record_index].field_count) &&
                aarch64_generated_blob_append_u16(&forms, (u16)indexes[record_index].operand_count) &&
                aarch64_generated_blob_append_u16(&forms, (u16)indexes[record_index].predicate_count) && aarch64_generated_blob_append_u16(&forms, 0) &&
                aarch64_generated_blob_append_u32(&forms, record->fixed_mask) && aarch64_generated_blob_append_u32(&forms, record->fixed_value) &&
                aarch64_generated_blob_append_u8(&forms, record->coverage_class) && aarch64_generated_blob_append_u8(&forms, record->encoder_family) &&
                aarch64_generated_blob_append_u8(&forms, record->test_class) && aarch64_generated_blob_append_u8(&forms, record->reason_id) &&
                aarch64_generated_blob_append_u8(&forms, record->has_alternatives) && aarch64_generated_blob_append_u8(&forms, record->address_kind) &&
                aarch64_generated_blob_append_u8(&forms, record->address_flags) && aarch64_generated_blob_append_u8(&forms, 0) &&
                aarch64_generated_blob_append_u16(&forms, record->address_base_index) && aarch64_generated_blob_append_u16(&forms, record->address_offset_index);
    }
    for (u32 index = 0; valid && index < mnemonic_count; index += 1)
    {
        valid = aarch64_generated_blob_append_u32(&mnemonic_candidates, mnemonic_order[index].coverage_index);
    }
    for (u32 index = 0; valid && index < signature_count; index += 1)
    {
        valid = aarch64_generated_blob_append_u32(&signature_candidates, signature_order[index].coverage_index);
    }
    for (u32 index = 0; valid && index < mnemonic_count;)
    {
        u32 first = index;
        String8 mnemonic = mnemonic_order[index].record->mnemonic;
        while (index < mnemonic_count && string_equal(mnemonic, mnemonic_order[index].record->mnemonic))
        {
            index += 1;
        }
        valid = aarch64_generated_blob_append_u32(&mnemonic_ranges, xed_generated_string_offset(pool, mnemonic)) &&
                aarch64_generated_blob_append_u32(&mnemonic_ranges, first) && aarch64_generated_blob_append_u32(&mnemonic_ranges, index - first);
    }
    for (u32 index = 0; valid && index < signature_count;)
    {
        u32 first = index;
        u64 signature_hash = signature_order[index].record->signature_hash;
        while (index < signature_count && signature_hash == signature_order[index].record->signature_hash)
        {
            index += 1;
        }
        valid = aarch64_generated_blob_append_u64(&signature_ranges, signature_hash) &&
                aarch64_generated_blob_append_u32(&signature_ranges, first) && aarch64_generated_blob_append_u32(&signature_ranges, index - first);
    }
    for (Aarch64ImportRecord* record = records.first; valid && record; record = record->next)
    {
        valid = aarch64_generated_blob_append_u64(&coverage, record->source_hash) && aarch64_generated_blob_append_u64(&coverage, record->name_hash) &&
                aarch64_generated_blob_append_u32(&coverage, xed_generated_string_offset(pool, record->name)) &&
                aarch64_generated_blob_append_u32(&coverage, record->normalized_form_id) &&
                aarch64_generated_blob_append_u8(&coverage, record->coverage_class) && aarch64_generated_blob_append_u8(&coverage, record->encoder_family) &&
                aarch64_generated_blob_append_u8(&coverage, record->test_class) && aarch64_generated_blob_append_u8(&coverage, record->reason_id);
        if (record->coverage_class >= AARCH64_IMPORT_COVERAGE_COUNT || record->reason_id >= AARCH64_IMPORT_REASON_COUNT)
        {
            stats->coverage_counts[AARCH64_IMPORT_COVERAGE_UNCLASSIFIED] += 1;
        }
        else
        {
            stats->coverage_counts[record->coverage_class] += 1;
            stats->reason_counts[record->reason_id] += 1;
            stats->encoder_counts[record->encoder_family] += 1;
            stats->test_counts[record->test_class] += 1;
        }
    }
    valid = valid && segments.count == segment_cursor * 4 && fields.count == field_cursor * 20 && operands.count == operand_cursor * 42 &&
            predicates.count == predicate_cursor * 4 && forms.count == canonical_count * 88 && mnemonic_ranges.count == mnemonic_range_count * 12 &&
            mnemonic_candidates.count == mnemonic_count * 4 && signature_ranges.count == signature_range_count * 16 &&
            signature_candidates.count == signature_count * 4 && coverage.count == records.count * 28;
    if (!valid)
    {
        return false;
    }

    aarch64_generated_emit_preamble(output);
    aarch64_generated_emit_string_pool(output, scratch, pool, sorted);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_segments_blob"), segments);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_fields_blob"), fields);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_operands_blob"), operands);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_predicates_blob"), predicates);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_forms_blob"), forms);
    arena_append_string8(output, S8("#define BUSTER_AARCH64_GENERATED_FORM_COUNT "));
    xed_generated_append_decimal(output, canonical_count);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_COVERAGE_ROW_COUNT "));
    xed_generated_append_decimal(output, records.count);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_FIELD_COUNT "));
    xed_generated_append_decimal(output, field_cursor);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_SEGMENT_COUNT "));
    xed_generated_append_decimal(output, segment_cursor);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_OPERAND_COUNT "));
    xed_generated_append_decimal(output, operand_cursor);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_PREDICATE_COUNT "));
    xed_generated_append_decimal(output, predicate_cursor);
    arena_append_string8(output, S8("\n"));
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_mnemonic_ranges_blob"), mnemonic_ranges);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_mnemonic_candidates_blob"), mnemonic_candidates);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_signature_ranges_blob"), signature_ranges);
    aarch64_generated_append_base64_blob(output, S8("buster_aarch64_generated_signature_candidates_blob"), signature_candidates);
    arena_append_string8(output, S8("#define BUSTER_AARCH64_GENERATED_MNEMONIC_RANGE_COUNT "));
    xed_generated_append_decimal(output, mnemonic_range_count);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_MNEMONIC_CANDIDATE_COUNT "));
    xed_generated_append_decimal(output, mnemonic_count);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_SIGNATURE_RANGE_COUNT "));
    xed_generated_append_decimal(output, signature_range_count);
    arena_append_string8(output, S8("\n#define BUSTER_AARCH64_GENERATED_SIGNATURE_CANDIDATE_COUNT "));
    xed_generated_append_decimal(output, signature_count);
    arena_append_string8(output, S8("\n\n"));
    aarch64_generated_emit_blob_accessors(output);
    arena_append_string8(output, S8("#include \"aarch64-coverage.generated.inc\"\n\n#endif\n"));

    arena_append_string8(coverage_output,
                         S8("/* Generated by build import_assembly_metadata; do not edit. */\n#ifndef BUSTER_AARCH64_GENERATED_COVERAGE_ROW_COUNT\n#define BUSTER_AARCH64_GENERATED_COVERAGE_ROW_COUNT "));
    xed_generated_append_decimal(coverage_output, records.count);
    arena_append_string8(coverage_output, S8("\n#endif\n"));
    aarch64_generated_append_base64_blob(coverage_output, S8("buster_aarch64_generated_coverage_blob"), coverage);
    arena_append_string8(coverage_output,
                         S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterAarch64GeneratedCoverage buster_aarch64_generated_coverage_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_AARCH64_GENERATED_COVERAGE_ROW_COUNT || !buster_aarch64_generated_blob_range_valid(buster_aarch64_generated_coverage_blob_BYTE_COUNT, (u64)index * 28u, 28u)) return (BusterAarch64GeneratedCoverage){0};\n"
                            "    u64 offset = (u64)index * 28u;\n"
                            "    BusterAarch64GeneratedCoverage result = (BusterAarch64GeneratedCoverage){buster_aarch64_generated_blob_u64(buster_aarch64_generated_coverage_blob, offset),\n"
                            "                                                    buster_aarch64_generated_blob_u64(buster_aarch64_generated_coverage_blob, offset + 8),\n"
                            "                                                    buster_aarch64_generated_blob_u32(buster_aarch64_generated_coverage_blob, offset + 16),\n"
                            "                                                    buster_aarch64_generated_blob_u32(buster_aarch64_generated_coverage_blob, offset + 20),\n"
                            "                                                    buster_aarch64_generated_blob_u8(buster_aarch64_generated_coverage_blob, offset + 24),\n"
                            "                                                    buster_aarch64_generated_blob_u8(buster_aarch64_generated_coverage_blob, offset + 25),\n"
                            "                                                    buster_aarch64_generated_blob_u8(buster_aarch64_generated_coverage_blob, offset + 26),\n"
                            "                                                    buster_aarch64_generated_blob_u8(buster_aarch64_generated_coverage_blob, offset + 27)};\n"
                            "    if (result.name_offset >= BUSTER_AARCH64_GENERATED_STRING_POOL_SIZE || result.normalized_form_id >= BUSTER_AARCH64_GENERATED_FORM_COUNT) return (BusterAarch64GeneratedCoverage){0};\n"
                            "    return result;\n"
                            "}\n"));
    stats->header_bytes = arena_buffer_size(output);
    stats->coverage_bytes = arena_buffer_size(coverage_output);
    return stats->coverage_counts[AARCH64_IMPORT_COVERAGE_UNCLASSIFIED] == 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_generated_emit_tables(Arena* output, Arena* coverage_output, Arena* scratch,
                                                       Aarch64ImportRecordList records, Aarch64GeneratedTableStats* stats)
{
    memset(stats, 0, sizeof(*stats));
    if (!records.count || !aarch64_import_validate_identity(scratch, records))
    {
        return false;
    }
    Aarch64ImportRecord** canonical = arena_allocate(scratch, Aarch64ImportRecord*, records.count);
    u32 canonical_count = 0;
    for (Aarch64ImportRecord* record = records.first; record; record = record->next)
    {
        if (record->coverage_class >= AARCH64_IMPORT_COVERAGE_COUNT || record->encoder_family >= AARCH64_IMPORT_ENCODER_COUNT ||
            record->test_class >= AARCH64_IMPORT_TEST_COUNT || record->reason_id >= AARCH64_IMPORT_REASON_COUNT)
        {
            return false;
        }
        aarch64_import_apply_operand_metadata(record);
        if (record->coverage_class != AARCH64_IMPORT_COVERAGE_ALIAS)
        {
            canonical[canonical_count++] = record;
        }
    }

    XedGeneratedStringPool pool = {0};
    for (Aarch64ImportRecord* record = records.first; record; record = record->next)
    {
        String8 values[] = {record->name, record->mnemonic, record->assembly, record->output_operands, record->input_operands};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
        {
            xed_generated_string_intern(&pool, scratch, values[index]);
        }
        for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
        {
            xed_generated_string_intern(&pool, scratch, variable->name);
        }
        for (Aarch64ImportOperand* operand = record->first_operand; operand; operand = operand->next)
        {
            xed_generated_string_intern(&pool, scratch, operand->syntax);
            xed_generated_string_intern(&pool, scratch, operand->type);
            xed_generated_string_intern(&pool, scratch, operand->name);
        }
        for (Aarch64ImportPredicate* predicate = record->first_predicate; predicate; predicate = predicate->next)
        {
            xed_generated_string_intern(&pool, scratch, predicate->name);
        }
    }
    XedGeneratedStringNode** sorted = 0;
    if (!xed_generated_string_pool_prepare(scratch, &pool, &sorted))
    {
        return false;
    }

    u64 feature_total = 0;
    for (Aarch64ImportRecord* record = records.first; record; record = record->next)
    {
        feature_total += record->predicate_count;
        if (feature_total > UINT32_MAX)
        {
            return false;
        }
    }
    String8* features = feature_total ? arena_allocate(scratch, String8, (u32)feature_total) : 0;
    u64 feature_index = 0;
    for (Aarch64ImportRecord* record = records.first; record; record = record->next)
    {
        for (Aarch64ImportPredicate* predicate = record->first_predicate; predicate; predicate = predicate->next)
        {
            features[feature_index++] = predicate->name;
        }
    }
    if (feature_index > 1)
    {
        qsort(features, feature_index, sizeof(features[0]), assembly_import_string_compare);
    }
    for (u64 index = 0; index < feature_index; index += 1)
    {
        if (!index || !string_equal(features[index - 1], features[index]))
        {
            stats->predicate_feature_count += 1;
        }
    }

    Aarch64GeneratedEmitIndex* indexes = arena_allocate(scratch, Aarch64GeneratedEmitIndex, canonical_count);
    u32 field_cursor = 0;
    u32 operand_cursor = 0;
    u32 predicate_cursor = 0;
    u32 segment_cursor = 0;
    for (u32 record_index = 0; record_index < canonical_count; record_index += 1)
    {
        Aarch64ImportRecord* record = canonical[record_index];
        if (record->variable_count > UINT16_MAX || record->operand_count > UINT16_MAX || record->predicate_count > UINT16_MAX)
        {
            return false;
        }
        indexes[record_index] = (Aarch64GeneratedEmitIndex){
            .field_first = field_cursor,
            .field_count = record->variable_count,
            .operand_first = operand_cursor,
            .operand_count = record->operand_count,
            .predicate_first = predicate_cursor,
            .predicate_count = record->predicate_count,
        };
        if (field_cursor > UINT32_MAX - record->variable_count || operand_cursor > UINT32_MAX - record->operand_count ||
            predicate_cursor > UINT32_MAX - record->predicate_count)
        {
            return false;
        }
        field_cursor += record->variable_count;
        operand_cursor += record->operand_count;
        predicate_cursor += record->predicate_count;
        for (Aarch64ImportVariable* variable = record->first_variable; variable; variable = variable->next)
        {
            u32 segment_count = aarch64_import_variable_segment_count(variable);
            if (segment_count > UINT8_MAX || segment_cursor > UINT32_MAX - segment_count)
            {
                return false;
            }
            segment_cursor += segment_count;
        }
    }
    stats->canonical_form_count = canonical_count;
    stats->field_count = field_cursor;
    stats->segment_count = segment_cursor;
    stats->operand_count = operand_cursor;
    stats->predicate_count = predicate_cursor;
    stats->string_pool_bytes = pool.byte_count;

    Aarch64GeneratedLookupRecord* mnemonic_order = arena_allocate(scratch, Aarch64GeneratedLookupRecord, records.count);
    Aarch64GeneratedLookupRecord* signature_order = arena_allocate(scratch, Aarch64GeneratedLookupRecord, records.count);
    u32 mnemonic_count = 0;
    u32 signature_count = 0;
    u32 coverage_index = 0;
    for (Aarch64ImportRecord* record = records.first; record; record = record->next, coverage_index += 1)
    {
        if (!record->mnemonic.length)
        {
            return false;
        }
        mnemonic_order[mnemonic_count++] = (Aarch64GeneratedLookupRecord){.record = record, .coverage_index = coverage_index};
        if (record->parse_reason == AARCH64_IMPORT_REASON_NONE && record->signature_hash)
        {
            signature_order[signature_count++] = (Aarch64GeneratedLookupRecord){.record = record, .coverage_index = coverage_index};
        }
    }
    qsort(mnemonic_order, mnemonic_count, sizeof(mnemonic_order[0]), aarch64_import_lookup_mnemonic_compare);
    qsort(signature_order, signature_count, sizeof(signature_order[0]), aarch64_import_lookup_signature_compare);
    for (u32 index = 0; index < mnemonic_count; index += 1)
    {
        if (mnemonic_order[index].coverage_index >= records.count ||
            (index && aarch64_import_lookup_mnemonic_compare(&mnemonic_order[index - 1], &mnemonic_order[index]) > 0))
        {
            return false;
        }
    }
    for (u32 index = 0; index < signature_count; index += 1)
    {
        if (signature_order[index].coverage_index >= records.count ||
            (index && aarch64_import_lookup_signature_compare(&signature_order[index - 1], &signature_order[index]) > 0))
        {
            return false;
        }
    }
    u32 mnemonic_range_count = 0;
    for (u32 index = 0; index < mnemonic_count; index += 1)
    {
        if (!index || !string_equal(mnemonic_order[index - 1].record->mnemonic, mnemonic_order[index].record->mnemonic))
        {
            mnemonic_range_count += 1;
        }
    }
    u32 signature_range_count = 0;
    for (u32 index = 0; index < signature_count; index += 1)
    {
        if (!index || signature_order[index - 1].record->signature_hash != signature_order[index].record->signature_hash)
        {
            signature_range_count += 1;
        }
    }
    stats->mnemonic_range_count = mnemonic_range_count;
    stats->signature_range_count = signature_range_count;
    stats->mnemonic_candidate_count = mnemonic_count;
    stats->signature_candidate_count = signature_count;

    return aarch64_generated_emit_packed_tables(output, coverage_output, scratch, records, canonical, canonical_count, &pool, sorted, indexes,
                                                field_cursor, segment_cursor, operand_cursor, predicate_cursor, mnemonic_order, mnemonic_count,
                                                signature_order, signature_count, mnemonic_range_count, signature_range_count, stats);

}

#define XED_GENERATED_C_ARRAY_CHUNK_SIZE 4092u
#define XED_GENERATED_BASE64_RAW_CHUNK_SIZE 3069u

typedef struct XedGeneratedNameLookup XedGeneratedNameLookup;
struct XedGeneratedNameLookup
{
    String8 key;
    u32 form_id;
};

typedef struct XedGeneratedHashLookup XedGeneratedHashLookup;
struct XedGeneratedHashLookup
{
    u64 key;
    u32 id;
};

BUSTER_GLOBAL_LOCAL String8 xed_generated_lower_first_token(Arena* arena, String8 value)
{
    u64 start = 0;
    while (start < value.length && character_is_space(value.pointer[start]))
    {
        start += 1;
    }
    u64 end = start;
    while (end < value.length && !character_is_space(value.pointer[end]))
    {
        end += 1;
    }
    if (end == start || end - start > UINT32_MAX)
    {
        return (String8){0};
    }
    char8* pointer = arena_allocate(arena, char8, end - start);
    for (u64 index = 0; index < end - start; index += 1)
    {
        char8 character = value.pointer[start + index];
        if (character >= 'A' && character <= 'Z')
        {
            character = (char8)(character - 'A' + 'a');
        }
        pointer[index] = character;
    }
    return (String8){.pointer = pointer, .length = end - start};
}

BUSTER_GLOBAL_LOCAL int xed_generated_name_lookup_compare(const void* left_pointer, const void* right_pointer)
{
    XedGeneratedNameLookup const* left = (XedGeneratedNameLookup const*)left_pointer;
    XedGeneratedNameLookup const* right = (XedGeneratedNameLookup const*)right_pointer;
    int result = assembly_import_string_compare(&left->key, &right->key);
    if (!result)
    {
        result = (left->form_id > right->form_id) - (left->form_id < right->form_id);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int xed_generated_hash_lookup_compare(const void* left_pointer, const void* right_pointer)
{
    XedGeneratedHashLookup const* left = (XedGeneratedHashLookup const*)left_pointer;
    XedGeneratedHashLookup const* right = (XedGeneratedHashLookup const*)right_pointer;
    if (left->key != right->key)
    {
        return left->key > right->key ? 1 : -1;
    }
    return (left->id > right->id) - (left->id < right->id);
}

BUSTER_GLOBAL_LOCAL u32 xed_generated_name_lookup_range_count(XedGeneratedNameLookup* entries, u32 count)
{
    u32 result = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        if (!index || !string_equal(entries[index - 1].key, entries[index].key))
        {
            result += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 xed_generated_hash_lookup_range_count(XedGeneratedHashLookup* entries, u32 count)
{
    u32 result = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        if (!index || entries[index - 1].key != entries[index].key)
        {
            result += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void xed_generated_append_x86_base64_value_preamble(Arena* output)
{
    arena_append_string8(output,
                         S8("#define BUSTER_X86_GENERATED_PACKED_BASE64 1\n"
                            "#define BUSTER_X86_GENERATED_C_ARRAY_CHUNK_SIZE 4092u\n"
                            "typedef struct BusterX86GeneratedTextRange BusterX86GeneratedTextRange;\n"
                            "struct BusterX86GeneratedTextRange { u32 key_offset; u32 candidate_first; u32 candidate_count; };\n"
                            "typedef struct BusterX86GeneratedHashRange BusterX86GeneratedHashRange;\n"
                            "struct BusterX86GeneratedHashRange { u64 key; u32 candidate_first; u32 candidate_count; };\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u8 buster_x86_generated_base64_value(char8 character)\n"
                            "{\n"
                            "    if (character >= 'A' && character <= 'Z') return (u8)(character - 'A');\n"
                            "    if (character >= 'a' && character <= 'z') return (u8)(character - 'a' + 26);\n"
                            "    if (character >= '0' && character <= '9') return (u8)(character - '0' + 52);\n"
                            "    if (character == '+') return 62;\n"
                            "    if (character == '/') return 63;\n"
                            "    return 0;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE bool buster_x86_generated_blob_range_valid(u64 byte_count, u64 offset, u64 length)\n"
                            "{\n"
                            "    return offset <= byte_count && length <= byte_count - offset;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE bool buster_x86_generated_base64_encoded_count(u64 byte_count, u64* encoded_count)\n"
                            "{\n"
                            "    if (!encoded_count || byte_count > UINT64_MAX - 2u) return false;\n"
                            "    u64 group_count = (byte_count + 2u) / 3u;\n"
                            "    if (group_count > UINT64_MAX / 4u) return false;\n"
                            "    *encoded_count = group_count * 4u;\n"
                            "    return true;\n"
                            "}\n\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_chunk_accessor(Arena* output, String8 name, u32 chunk_count)
{
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL char8 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(u64 logical)\n{\n    u64 chunk = logical / BUSTER_X86_GENERATED_C_ARRAY_CHUNK_SIZE;\n    u64 offset = logical % BUSTER_X86_GENERATED_C_ARRAY_CHUNK_SIZE;\n    switch (chunk)\n    {\n"));
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        arena_append_string8(output, S8("        case "));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8(": return offset < (u64)(sizeof("));
        arena_append_string8(output, name);
        arena_append_string8(output, S8("_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8(") - 1u) ? "));
        arena_append_string8(output, name);
        arena_append_string8(output, S8("_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8("[offset] : 0;\n"));
    }
    arena_append_string8(output, S8("        default: return 0;\n    }\n}\n\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u8 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted(u64 byte_count, u64 offset)\n{\n    u64 encoded_count = 0;\n    if (offset >= byte_count || !buster_x86_generated_base64_encoded_count(byte_count, &encoded_count)) return 0;\n    u64 encoded_offset = (offset / 3u) * 4u;\n    if (encoded_offset > encoded_count || encoded_count - encoded_offset < 4u) return 0;\n    u32 value = ((u32)buster_x86_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 0u)) << 18) | ((u32)buster_x86_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 1u)) << 12) | ((u32)buster_x86_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 2u)) << 6) | (u32)buster_x86_generated_base64_value("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_char(encoded_offset + 3u));\n    return (u8)(value >> ((2u - (offset % 3u)) * 8u));\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u8 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u16 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted(u64 byte_count, u64 offset)\n{\n    if (!buster_x86_generated_blob_range_valid(byte_count, offset, 2u)) return 0;\n    return (u16)("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted(byte_count, offset) | ((u16)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u8_counted(byte_count, offset + 1u) << 8));\n}\nBUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u16 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u32 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted(u64 byte_count, u64 offset)\n{\n    if (!buster_x86_generated_blob_range_valid(byte_count, offset, 4u)) return 0;\n    return (u32)("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted(byte_count, offset) | ((u32)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u16_counted(byte_count, offset + 2u) << 16));\n}\nBUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u32 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u64 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u64_counted(u64 byte_count, u64 offset)\n{\n    if (!buster_x86_generated_blob_range_valid(byte_count, offset, 8u)) return 0;\n    return (u64)("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted(byte_count, offset) | ((u64)"));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u32_counted(byte_count, offset + 4u) << 32));\n}\nBUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL u64 "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u64(u64 offset)\n{\n    return "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_u64_counted("));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT, offset);\n}\n\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_base64_blob(Arena* output, String8 name, Aarch64GeneratedBlob blob)
{
    static const char8 alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u64 encoded_byte_count = ((u64)blob.count + 2u) / 3u * 4u;
    u32 chunk_count = (u32)((encoded_byte_count + XED_GENERATED_C_ARRAY_CHUNK_SIZE - 1u) / XED_GENERATED_C_ARRAY_CHUNK_SIZE);
    if (!chunk_count)
    {
        chunk_count = 1;
    }
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 start = chunk * XED_GENERATED_BASE64_RAW_CHUNK_SIZE;
        u32 length = start < blob.count ? BUSTER_MIN(XED_GENERATED_BASE64_RAW_CHUNK_SIZE, blob.count - start) : 0;
        char8 encoded[XED_GENERATED_C_ARRAY_CHUNK_SIZE + 1u];
        u32 encoded_count = 0;
        for (u32 index = 0; index < length; index += 3)
        {
            u32 remaining = length - index;
            u32 value = ((u32)blob.bytes[start + index] << 16) | (remaining > 1 ? (u32)blob.bytes[start + index + 1] << 8 : 0) |
                        (remaining > 2 ? blob.bytes[start + index + 2] : 0);
            encoded[encoded_count++] = alphabet[(value >> 18) & 63];
            encoded[encoded_count++] = alphabet[(value >> 12) & 63];
            encoded[encoded_count++] = remaining > 1 ? alphabet[(value >> 6) & 63] : '=';
            encoded[encoded_count++] = remaining > 2 ? alphabet[value & 63] : '=';
        }
        arena_append_string8(output, S8("static const char8 "));
        arena_append_string8(output, name);
        arena_append_string8(output, S8("_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8("[] = "));
        if (encoded_count)
        {
            xed_generated_append_bytes(output, encoded, encoded_count);
        }
        else
        {
            arena_append_string8(output, S8("{0}"));
        }
        arena_append_string8(output, S8(";\n"));
    }
    arena_append_string8(output, S8("#define "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_BYTE_COUNT "));
    xed_generated_append_decimal(output, blob.count);
    arena_append_string8(output, S8("\n#define "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8("_CHUNK_COUNT "));
    xed_generated_append_decimal(output, chunk_count);
    arena_append_string8(output, S8("\n"));
    xed_generated_emit_x86_chunk_accessor(output, name, chunk_count);
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_chunked_string_pool(Arena* output, Arena* arena, XedGeneratedStringPool* pool,
                                                                     XedGeneratedStringNode** sorted)
{
    u8* bytes = arena_allocate(arena, u8, pool->byte_count);
    memset(bytes, 0, pool->byte_count);
    u32 offset = 1;
    for (u32 index = 0; index < pool->count; index += 1)
    {
        XedGeneratedStringNode* node = sorted[index];
        memcpy(bytes + offset, node->string.pointer, node->string.length);
        offset += (u32)node->string.length + 1;
    }
    u32 chunk_count = (pool->byte_count + XED_GENERATED_C_ARRAY_CHUNK_SIZE - 1u) / XED_GENERATED_C_ARRAY_CHUNK_SIZE;
    if (!chunk_count)
    {
        chunk_count = 1;
    }
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 start = chunk * XED_GENERATED_C_ARRAY_CHUNK_SIZE;
        u32 length = start < pool->byte_count ? BUSTER_MIN(XED_GENERATED_C_ARRAY_CHUNK_SIZE, pool->byte_count - start) : 0;
        arena_append_string8(output, S8("static const char8 buster_x86_generated_string_pool_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8("[] = "));
        if (length)
        {
            xed_generated_append_bytes(output, (char8*)bytes + start, length);
        }
        else
        {
            arena_append_string8(output, S8("{0}"));
        }
        arena_append_string8(output, S8(";\n"));
    }
    arena_append_string8(output, S8("#define BUSTER_X86_GENERATED_STRING_POOL_SIZE "));
    xed_generated_append_decimal(output, pool->byte_count);
    arena_append_string8(output, S8("\n#define BUSTER_X86_GENERATED_STRING_POOL_CHUNK_COUNT "));
    xed_generated_append_decimal(output, chunk_count);
    arena_append_string8(output, S8("\nBUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE char8 buster_x86_generated_string_byte(u64 logical)\n{\n    if (logical >= (u64)BUSTER_X86_GENERATED_STRING_POOL_SIZE) return 0;\n    u64 chunk = logical / BUSTER_X86_GENERATED_C_ARRAY_CHUNK_SIZE;\n    u64 offset = logical % BUSTER_X86_GENERATED_C_ARRAY_CHUNK_SIZE;\n    switch (chunk)\n    {\n"));
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        arena_append_string8(output, S8("        case "));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8(": return offset < (u64)(sizeof(buster_x86_generated_string_pool_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8(") - 1u) ? buster_x86_generated_string_pool_chunk_"));
        xed_generated_append_decimal(output, chunk);
        arena_append_string8(output, S8("[offset] : 0;\n"));
    }
    arena_append_string8(output, S8("        default: return 0;\n    }\n}\n\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_text_index_accessors(Arena* output, String8 macro_prefix, String8 blob_prefix)
{
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterX86GeneratedTextRange "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_range_at(u32 index)\n{\n    if (index >= "));
    arena_append_string8(output, macro_prefix);
    arena_append_string8(output, S8("_RANGE_COUNT || !buster_x86_generated_blob_range_valid("));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_BYTE_COUNT, (u64)index * 12u, 12u)) return (BusterX86GeneratedTextRange){0};\n    u64 offset = (u64)index * 12u;\n    return (BusterX86GeneratedTextRange){"));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_u32(offset), "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_u32(offset + 4u), "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_u32(offset + 8u)};\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u32 "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_candidate_at(u32 index)\n{\n    if (index >= "));
    arena_append_string8(output, macro_prefix);
    arena_append_string8(output, S8("_CANDIDATE_COUNT || !buster_x86_generated_blob_range_valid("));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_candidates_blob_BYTE_COUNT, (u64)index * 4u, 4u)) return UINT32_MAX;\n    return "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_candidates_blob_u32((u64)index * 4u);\n}\n\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_hash_index_accessors(Arena* output, String8 macro_prefix, String8 blob_prefix)
{
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterX86GeneratedHashRange "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_range_at(u32 index)\n{\n    if (index >= "));
    arena_append_string8(output, macro_prefix);
    arena_append_string8(output, S8("_RANGE_COUNT || !buster_x86_generated_blob_range_valid("));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_BYTE_COUNT, (u64)index * 16u, 16u)) return (BusterX86GeneratedHashRange){0};\n    u64 offset = (u64)index * 16u;\n    return (BusterX86GeneratedHashRange){"));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_u64(offset), "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_u32(offset + 8u), "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_ranges_blob_u32(offset + 12u)};\n}\n"));
    arena_append_string8(output, S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u32 "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_candidate_at(u32 index)\n{\n    if (index >= "));
    arena_append_string8(output, macro_prefix);
    arena_append_string8(output, S8("_CANDIDATE_COUNT || !buster_x86_generated_blob_range_valid("));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_candidates_blob_BYTE_COUNT, (u64)index * 4u, 4u)) return UINT32_MAX;\n    return "));
    arena_append_string8(output, blob_prefix);
    arena_append_string8(output, S8("_candidates_blob_u32((u64)index * 4u);\n}\n\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_record_accessors(Arena* output)
{
    arena_append_string8(output,
                         S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterX86GeneratedOperand buster_x86_generated_operand_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_X86_GENERATED_OPERAND_COUNT || !buster_x86_generated_blob_range_valid(buster_x86_generated_operands_blob_BYTE_COUNT, (u64)index * 16u, 16u)) return (BusterX86GeneratedOperand){0};\n"
                            "    u64 offset = (u64)index * 16u;\n"
                            "    BusterX86GeneratedOperand result = {0};\n"
                            "    result.atom_offset = buster_x86_generated_operands_blob_u32(offset);\n"
                            "    result.width_offset = buster_x86_generated_operands_blob_u32(offset + 4u);\n"
                            "    result.slot = buster_x86_generated_operands_blob_u8(offset + 8u);\n"
                            "    result.visible = buster_x86_generated_operands_blob_u8(offset + 9u);\n"
                            "    result.kind = buster_x86_generated_operands_blob_u8(offset + 10u);\n"
                            "    result.access = buster_x86_generated_operands_blob_u8(offset + 11u);\n"
                            "    result.field_source = buster_x86_generated_operands_blob_u8(offset + 12u);\n"
                            "    result.reserved[0] = buster_x86_generated_operands_blob_u8(offset + 13u);\n"
                            "    result.reserved[1] = buster_x86_generated_operands_blob_u8(offset + 14u);\n"
                            "    result.reserved[2] = buster_x86_generated_operands_blob_u8(offset + 15u);\n"
                            "    return result;\n"
                            "}\n"
                            "BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterX86GeneratedForm buster_x86_generated_form_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_X86_GENERATED_FORM_COUNT || !buster_x86_generated_blob_range_valid(buster_x86_generated_forms_blob_BYTE_COUNT, (u64)index * 156u, 156u)) return (BusterX86GeneratedForm){0};\n"
                            "    u64 offset = (u64)index * 156u;\n"
                            "    BusterX86GeneratedForm result = {0};\n"
                            "    result.stable_hash = buster_x86_generated_forms_blob_u64(offset);\n"
                            "    result.source_offset = buster_x86_generated_forms_blob_u32(offset + 8u);\n"
                            "    result.iclass_offset = buster_x86_generated_forms_blob_u32(offset + 12u);\n"
                            "    result.iform_offset = buster_x86_generated_forms_blob_u32(offset + 16u);\n"
                            "    result.isa_set_offset = buster_x86_generated_forms_blob_u32(offset + 20u);\n"
                            "    result.category_offset = buster_x86_generated_forms_blob_u32(offset + 24u);\n"
                            "    result.extension_offset = buster_x86_generated_forms_blob_u32(offset + 28u);\n"
                            "    result.attributes_offset = buster_x86_generated_forms_blob_u32(offset + 32u);\n"
                            "    result.cpl_offset = buster_x86_generated_forms_blob_u32(offset + 36u);\n"
                            "    result.exceptions_offset = buster_x86_generated_forms_blob_u32(offset + 40u);\n"
                            "    result.flags_offset = buster_x86_generated_forms_blob_u32(offset + 44u);\n"
                            "    result.disasm_offset = buster_x86_generated_forms_blob_u32(offset + 48u);\n"
                            "    result.disasm_intel_offset = buster_x86_generated_forms_blob_u32(offset + 52u);\n"
                            "    result.disasm_attsv_offset = buster_x86_generated_forms_blob_u32(offset + 56u);\n"
                            "    result.real_opcode_offset = buster_x86_generated_forms_blob_u32(offset + 60u);\n"
                            "    result.uname_offset = buster_x86_generated_forms_blob_u32(offset + 64u);\n"
                            "    result.comment_offset = buster_x86_generated_forms_blob_u32(offset + 68u);\n"
                            "    result.version_offset = buster_x86_generated_forms_blob_u32(offset + 72u);\n"
                            "    result.pattern_offset = buster_x86_generated_forms_blob_u32(offset + 76u);\n"
                            "    result.operands_offset = buster_x86_generated_forms_blob_u32(offset + 80u);\n"
                            "    result.operand_annotation_offset = buster_x86_generated_forms_blob_u32(offset + 84u);\n"
                            "    result.operand_first = buster_x86_generated_forms_blob_u32(offset + 88u);\n"
                            "    result.operand_count = buster_x86_generated_forms_blob_u16(offset + 92u);\n"
                            "    result.coverage_class = buster_x86_generated_forms_blob_u8(offset + 94u);\n"
                            "    result.encoder_family = buster_x86_generated_forms_blob_u8(offset + 95u);\n"
                            "    result.test_class = buster_x86_generated_forms_blob_u8(offset + 96u);\n"
                            "    result.prefix_kind = buster_x86_generated_forms_blob_u8(offset + 97u);\n"
                            "    result.map = buster_x86_generated_forms_blob_u8(offset + 98u);\n"
                            "    result.fixed_byte_count = buster_x86_generated_forms_blob_u8(offset + 99u);\n"
                            "    u32 byte_index = 0;\n"
                            "    for (; byte_index < 16; byte_index += 1)\n"
                            "    {\n"
                            "        result.fixed_bytes[byte_index] = buster_x86_generated_forms_blob_u8(offset + 100u + byte_index);\n"
                            "    }\n"
                            "    result.mandatory_prefix = buster_x86_generated_forms_blob_u8(offset + 116u);\n"
                            "    result.field_flags = buster_x86_generated_forms_blob_u16(offset + 117u);\n"
                            "    result.decorator_flags = buster_x86_generated_forms_blob_u16(offset + 119u);\n"
                            "    result.apx_flags = buster_x86_generated_forms_blob_u16(offset + 121u);\n"
                            "    result.amx_flags = buster_x86_generated_forms_blob_u16(offset + 123u);\n"
                            "    result.mode_flags = buster_x86_generated_forms_blob_u16(offset + 125u);\n"
                            "    result.displacement_width = buster_x86_generated_forms_blob_u8(offset + 127u);\n"
                            "    result.displacement_scale = buster_x86_generated_forms_blob_u8(offset + 128u);\n"
                            "    result.immediate_width = buster_x86_generated_forms_blob_u8(offset + 129u);\n"
                            "    result.immediate_signed = buster_x86_generated_forms_blob_u8(offset + 130u);\n"
                            "    result.relocation_base = buster_x86_generated_forms_blob_u8(offset + 131u);\n"
                            "    result.reserved[0] = buster_x86_generated_forms_blob_u8(offset + 132u);\n"
                            "    result.reserved[1] = buster_x86_generated_forms_blob_u8(offset + 133u);\n"
                            "    result.reserved[2] = buster_x86_generated_forms_blob_u8(offset + 134u);\n"
                            "    result.tuple_kind = buster_x86_generated_forms_blob_u8(offset + 135u);\n"
                            "    result.tuple_offset = buster_x86_generated_forms_blob_u32(offset + 136u);\n"
                            "    result.element_size_offset = buster_x86_generated_forms_blob_u32(offset + 140u);\n"
                            "    result.token_count = buster_x86_generated_forms_blob_u32(offset + 144u);\n"
                            "    result.reason_id = buster_x86_generated_forms_blob_u16(offset + 148u);\n"
                            "    result.reason_offset = buster_x86_generated_forms_blob_u32(offset + 150u);\n"
                            "    result.reserved2 = buster_x86_generated_forms_blob_u16(offset + 154u);\n"
                            "    return result;\n"
                            "}\n\n"));
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_coverage_accessor(Arena* output)
{
    arena_append_string8(output,
                         S8("BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE BusterX86GeneratedCoverage buster_x86_generated_coverage_at(u32 index)\n"
                            "{\n"
                            "    if (index >= BUSTER_X86_GENERATED_COVERAGE_COUNT || !buster_x86_generated_blob_range_valid(buster_x86_generated_coverage_blob_BYTE_COUNT, (u64)index * 25u, 25u)) return (BusterX86GeneratedCoverage){0};\n"
                            "    u64 offset = (u64)index * 25u;\n"
                            "    BusterX86GeneratedCoverage result = {0};\n"
                            "    result.source_hash = buster_x86_generated_coverage_blob_u64(offset);\n"
                            "    result.source_offset = buster_x86_generated_coverage_blob_u32(offset + 8u);\n"
                            "    result.normalized_form_id = buster_x86_generated_coverage_blob_u32(offset + 12u);\n"
                            "    result.coverage_class = buster_x86_generated_coverage_blob_u8(offset + 16u);\n"
                            "    result.encoder_family = buster_x86_generated_coverage_blob_u8(offset + 17u);\n"
                            "    result.test_class = buster_x86_generated_coverage_blob_u8(offset + 18u);\n"
                            "    result.reason_id = buster_x86_generated_coverage_blob_u16(offset + 19u);\n"
                            "    result.reason_offset = buster_x86_generated_coverage_blob_u32(offset + 21u);\n"
                            "    return result;\n"
                            "}\n"));
}

BUSTER_GLOBAL_LOCAL bool xed_generated_pack_name_lookup(Arena* scratch, XedGeneratedStringPool* pool,
                                                        XedGeneratedNameLookup* entries, u32 count,
                                                        Aarch64GeneratedBlob* ranges, Aarch64GeneratedBlob* candidates)
{
    for (u32 index = 0; index < count; index += 1)
    {
        if (!index || !string_equal(entries[index - 1].key, entries[index].key))
        {
            u32 end = index + 1;
            while (end < count && string_equal(entries[end].key, entries[index].key))
            {
                end += 1;
            }
            if (!aarch64_generated_blob_append_u32(ranges, xed_generated_string_offset(pool, entries[index].key)) ||
                !aarch64_generated_blob_append_u32(ranges, index) || !aarch64_generated_blob_append_u32(ranges, end - index))
            {
                return false;
            }
        }
        if (!aarch64_generated_blob_append_u32(candidates, entries[index].form_id))
        {
            return false;
        }
    }
    BUSTER_UNUSED(scratch);
    bool valid = ranges->count == xed_generated_name_lookup_range_count(entries, count) * 12u && candidates->count == count * 4u;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool xed_generated_pack_hash_lookup(XedGeneratedHashLookup* entries, u32 count,
                                                        Aarch64GeneratedBlob* ranges, Aarch64GeneratedBlob* candidates)
{
    for (u32 index = 0; index < count; index += 1)
    {
        if (!index || entries[index - 1].key != entries[index].key)
        {
            u32 end = index + 1;
            while (end < count && entries[end].key == entries[index].key)
            {
                end += 1;
            }
            if (!aarch64_generated_blob_append_u64(ranges, entries[index].key) ||
                !aarch64_generated_blob_append_u32(ranges, index) || !aarch64_generated_blob_append_u32(ranges, end - index))
            {
                return false;
            }
        }
        if (!aarch64_generated_blob_append_u32(candidates, entries[index].id))
        {
            return false;
        }
    }
    bool valid = ranges->count == xed_generated_hash_lookup_range_count(entries, count) * 16u && candidates->count == count * 4u;
    return valid;
}

BUSTER_GLOBAL_LOCAL void xed_generated_emit_x86_count_macro(Arena* output, String8 name, u32 value)
{
    arena_append_string8(output, S8("#define "));
    arena_append_string8(output, name);
    arena_append_string8(output, S8(" "));
    xed_generated_append_decimal(output, value);
    arena_append_string8(output, S8("\n"));
}

BUSTER_GLOBAL_LOCAL bool xed_import_emit_generated_tables_packed(Arena* output, Arena* coverage_output, Arena* arena,
                                                                  XedGeneratedFormList forms, XedGeneratedTableStats* stats)
{
    if (forms.length > UINT32_MAX || forms.length > UINT32_MAX / 4u)
    {
        return false;
    }
    XedGeneratedStringPool pool = {0};
    xed_generated_intern_forms(&pool, arena, forms);

    u32 name_capacity = (u32)forms.length * 4u;
    XedGeneratedNameLookup* mnemonic_entries = arena_allocate(arena, XedGeneratedNameLookup, name_capacity);
    XedGeneratedNameLookup* iclass_entries = arena_allocate(arena, XedGeneratedNameLookup, forms.length);
    XedGeneratedNameLookup* iform_entries = arena_allocate(arena, XedGeneratedNameLookup, forms.length);
    XedGeneratedHashLookup* form_hash_entries = arena_allocate(arena, XedGeneratedHashLookup, forms.length);
    XedGeneratedHashLookup* coverage_hash_entries = arena_allocate(arena, XedGeneratedHashLookup, forms.length);
    u32 mnemonic_count = 0;
    u32 iclass_count = 0;
    u32 iform_count = 0;
    u32 form_hash_count = 0;
    u32 coverage_hash_count = 0;
    for (u32 form_index = 0; form_index < forms.length; form_index += 1)
    {
        XedGeneratedForm* form = forms.pointer + form_index;
        XedImportRecord* record = form->record;
        String8 dialects[] = {record->disasm_intel, record->disasm_attsv, record->disasm};
        String8 added[3] = {0};
        u32 added_count = 0;
        for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialects); dialect_index += 1)
        {
            String8 key = xed_generated_lower_first_token(arena, dialects[dialect_index]);
            if (!key.length)
            {
                continue;
            }
            bool duplicate = false;
            for (u32 added_index = 0; added_index < added_count; added_index += 1)
            {
                duplicate |= string_equal(added[added_index], key);
            }
            if (!duplicate)
            {
                added[added_count++] = key;
                mnemonic_entries[mnemonic_count++] = (XedGeneratedNameLookup){.key = key, .form_id = form_index};
                xed_generated_string_intern(&pool, arena, key);
            }
        }
        if (!added_count)
        {
            String8 key = xed_generated_lower_first_token(arena, record->iclass);
            if (key.length)
            {
                mnemonic_entries[mnemonic_count++] = (XedGeneratedNameLookup){.key = key, .form_id = form_index};
                xed_generated_string_intern(&pool, arena, key);
            }
        }
        String8 iclass = xed_generated_lower_first_token(arena, record->iclass);
        if (iclass.length)
        {
            iclass_entries[iclass_count++] = (XedGeneratedNameLookup){.key = iclass, .form_id = form_index};
            xed_generated_string_intern(&pool, arena, iclass);
        }
        String8 iform = xed_generated_lower_first_token(arena, record->iform);
        if (iform.length)
        {
            iform_entries[iform_count++] = (XedGeneratedNameLookup){.key = iform, .form_id = form_index};
            xed_generated_string_intern(&pool, arena, iform);
        }
        form_hash_entries[form_hash_count++] = (XedGeneratedHashLookup){.key = form->stable_hash, .id = form_index};
        coverage_hash_entries[coverage_hash_count++] = (XedGeneratedHashLookup){.key = form->stable_hash, .id = form_index};
    }
    qsort(mnemonic_entries, mnemonic_count, sizeof(mnemonic_entries[0]), xed_generated_name_lookup_compare);
    qsort(iclass_entries, iclass_count, sizeof(iclass_entries[0]), xed_generated_name_lookup_compare);
    qsort(iform_entries, iform_count, sizeof(iform_entries[0]), xed_generated_name_lookup_compare);
    qsort(form_hash_entries, form_hash_count, sizeof(form_hash_entries[0]), xed_generated_hash_lookup_compare);
    qsort(coverage_hash_entries, coverage_hash_count, sizeof(coverage_hash_entries[0]), xed_generated_hash_lookup_compare);

    XedGeneratedStringNode** sorted = 0;
    if (!xed_generated_string_pool_prepare(arena, &pool, &sorted))
    {
        return false;
    }

    u32* operand_offsets = arena_allocate(arena, u32, forms.length);
    u32 operand_count = 0;
    for (u32 form_index = 0; form_index < forms.length; form_index += 1)
    {
        operand_offsets[form_index] = operand_count;
        if (forms.pointer[form_index].operand_count > UINT32_MAX - operand_count)
        {
            return false;
        }
        operand_count += forms.pointer[form_index].operand_count;
    }
    if (operand_count > UINT32_MAX / 16u || forms.length > UINT32_MAX / 156u || forms.length > UINT32_MAX / 25u)
    {
        return false;
    }
    u32 mnemonic_range_count = xed_generated_name_lookup_range_count(mnemonic_entries, mnemonic_count);
    u32 iclass_range_count = xed_generated_name_lookup_range_count(iclass_entries, iclass_count);
    u32 iform_range_count = xed_generated_name_lookup_range_count(iform_entries, iform_count);
    u32 form_hash_range_count = xed_generated_hash_lookup_range_count(form_hash_entries, form_hash_count);
    u32 coverage_hash_range_count = xed_generated_hash_lookup_range_count(coverage_hash_entries, coverage_hash_count);
    u32 index_capacity = mnemonic_count;
    if (iclass_count > index_capacity) index_capacity = iclass_count;
    if (iform_count > index_capacity) index_capacity = iform_count;
    if (form_hash_count > index_capacity) index_capacity = form_hash_count;
    if (coverage_hash_count > index_capacity) index_capacity = coverage_hash_count;
    if ((u64)index_capacity < forms.length) index_capacity = (u32)forms.length;
    Aarch64GeneratedBlob operands_blob = aarch64_generated_blob_make(arena, (u64)operand_count * 16u);
    Aarch64GeneratedBlob forms_blob = aarch64_generated_blob_make(arena, (u64)forms.length * 156u);
    Aarch64GeneratedBlob coverage_blob = aarch64_generated_blob_make(arena, (u64)forms.length * 25u);
    Aarch64GeneratedBlob mnemonic_ranges_blob = aarch64_generated_blob_make(arena, (u64)mnemonic_range_count * 12u);
    Aarch64GeneratedBlob mnemonic_candidates_blob = aarch64_generated_blob_make(arena, (u64)mnemonic_count * 4u);
    Aarch64GeneratedBlob iclass_ranges_blob = aarch64_generated_blob_make(arena, (u64)iclass_range_count * 12u);
    Aarch64GeneratedBlob iclass_candidates_blob = aarch64_generated_blob_make(arena, (u64)iclass_count * 4u);
    Aarch64GeneratedBlob iform_ranges_blob = aarch64_generated_blob_make(arena, (u64)iform_range_count * 12u);
    Aarch64GeneratedBlob iform_candidates_blob = aarch64_generated_blob_make(arena, (u64)iform_count * 4u);
    Aarch64GeneratedBlob form_hash_ranges_blob = aarch64_generated_blob_make(arena, (u64)form_hash_range_count * 16u);
    Aarch64GeneratedBlob form_hash_candidates_blob = aarch64_generated_blob_make(arena, (u64)form_hash_count * 4u);
    Aarch64GeneratedBlob coverage_hash_ranges_blob = aarch64_generated_blob_make(arena, (u64)coverage_hash_range_count * 16u);
    Aarch64GeneratedBlob coverage_hash_candidates_blob = aarch64_generated_blob_make(arena, (u64)coverage_hash_count * 4u);
    if ((operand_count && !operands_blob.bytes) || (forms.length && (!forms_blob.bytes || !coverage_blob.bytes)) ||
        (mnemonic_count && (!mnemonic_ranges_blob.bytes || !mnemonic_candidates_blob.bytes)) ||
        (iclass_count && (!iclass_ranges_blob.bytes || !iclass_candidates_blob.bytes)) ||
        (iform_count && (!iform_ranges_blob.bytes || !iform_candidates_blob.bytes)) ||
        (form_hash_count && (!form_hash_ranges_blob.bytes || !form_hash_candidates_blob.bytes)) ||
        (coverage_hash_count && (!coverage_hash_ranges_blob.bytes || !coverage_hash_candidates_blob.bytes)))
    {
        return false;
    }
    for (u32 form_index = 0; form_index < forms.length; form_index += 1)
    {
        XedGeneratedForm* form = forms.pointer + form_index;
        if (form->coverage_class == XED_GENERATED_COVERAGE_UNCLASSIFIED ||
            ((form->coverage_class == XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN || form->coverage_class == XED_GENERATED_COVERAGE_RESERVED) &&
             form->reason_id == XED_GENERATED_REASON_NONE))
        {
            return false;
        }
        for (u32 operand_index = 0; operand_index < form->operand_count; operand_index += 1)
        {
            XedGeneratedOperand* operand = form->operands + operand_index;
            if (!aarch64_generated_blob_append_u32(&operands_blob, xed_generated_string_offset(&pool, operand->atom)) ||
                !aarch64_generated_blob_append_u32(&operands_blob, xed_generated_string_offset(&pool, operand->width)) ||
                !aarch64_generated_blob_append_u8(&operands_blob, operand->slot) ||
                !aarch64_generated_blob_append_u8(&operands_blob, operand->visible) ||
                !aarch64_generated_blob_append_u8(&operands_blob, operand->kind) ||
                !aarch64_generated_blob_append_u8(&operands_blob, operand->access) ||
                !aarch64_generated_blob_append_u8(&operands_blob, operand->field_source) ||
                !aarch64_generated_blob_append_u8(&operands_blob, 0) || !aarch64_generated_blob_append_u8(&operands_blob, 0) ||
                !aarch64_generated_blob_append_u8(&operands_blob, 0))
            {
                return false;
            }
        }
        XedImportRecord* record = form->record;
        String8 record_values[] = {
            record->source, record->iclass, record->iform, record->isa_set, record->category, record->extension,
            record->attributes, record->cpl, record->exceptions, record->flags, record->disasm, record->disasm_intel,
            record->disasm_attsv, record->real_opcode, record->uname, record->comment, record->version, record->pattern,
            record->operands, record->operand_annotation,
        };
        bool packed = aarch64_generated_blob_append_u64(&forms_blob, form->stable_hash);
        for (u32 value_index = 0; packed && value_index < BUSTER_ARRAY_LENGTH(record_values); value_index += 1)
        {
            packed = aarch64_generated_blob_append_u32(&forms_blob, xed_generated_string_offset(&pool, record_values[value_index]));
        }
        packed = packed && aarch64_generated_blob_append_u32(&forms_blob, operand_offsets[form_index]) &&
                 aarch64_generated_blob_append_u16(&forms_blob, (u16)form->operand_count) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->coverage_class) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->encoder_family) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->test_class) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->prefix_kind) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->map) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->fixed_byte_count);
        for (u32 byte_index = 0; packed && byte_index < XED_GENERATED_MAX_FIXED_BYTES; byte_index += 1)
        {
            packed = aarch64_generated_blob_append_u8(&forms_blob, form->fixed_bytes[byte_index]);
        }
        packed = packed && aarch64_generated_blob_append_u8(&forms_blob, form->mandatory_prefix) &&
                 aarch64_generated_blob_append_u16(&forms_blob, form->field_flags) &&
                 aarch64_generated_blob_append_u16(&forms_blob, form->decorator_flags) &&
                 aarch64_generated_blob_append_u16(&forms_blob, form->apx_flags) &&
                 aarch64_generated_blob_append_u16(&forms_blob, form->amx_flags) &&
                 aarch64_generated_blob_append_u16(&forms_blob, form->mode_flags) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->displacement_width) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->displacement_scale) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->immediate_width) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->immediate_signed) &&
                 aarch64_generated_blob_append_u8(&forms_blob, form->relocation_base) &&
                 aarch64_generated_blob_append_u8(&forms_blob, 0) && aarch64_generated_blob_append_u8(&forms_blob, 0) &&
                 aarch64_generated_blob_append_u8(&forms_blob, 0) && aarch64_generated_blob_append_u8(&forms_blob, form->tuple_kind) &&
                 aarch64_generated_blob_append_u32(&forms_blob, xed_generated_string_offset(&pool, form->tuple_rule)) &&
                 aarch64_generated_blob_append_u32(&forms_blob, xed_generated_string_offset(&pool, form->element_size)) &&
                 aarch64_generated_blob_append_u32(&forms_blob, form->token_count) &&
                 aarch64_generated_blob_append_u16(&forms_blob, form->reason_id) &&
                 aarch64_generated_blob_append_u32(&forms_blob, xed_generated_string_offset(&pool, form->unsupported_token)) &&
                 aarch64_generated_blob_append_u16(&forms_blob, 0);
        if (!packed)
        {
            return false;
        }
        if (!aarch64_generated_blob_append_u64(&coverage_blob, form->stable_hash) ||
            !aarch64_generated_blob_append_u32(&coverage_blob, xed_generated_string_offset(&pool, record->source)) ||
            !aarch64_generated_blob_append_u32(&coverage_blob, form_index) ||
            !aarch64_generated_blob_append_u8(&coverage_blob, form->coverage_class) ||
            !aarch64_generated_blob_append_u8(&coverage_blob, form->encoder_family) ||
            !aarch64_generated_blob_append_u8(&coverage_blob, form->test_class) ||
            !aarch64_generated_blob_append_u16(&coverage_blob, form->reason_id) ||
            !aarch64_generated_blob_append_u32(&coverage_blob, xed_generated_string_offset(&pool, form->unsupported_token)))
        {
            return false;
        }
        stats->coverage_counts[form->coverage_class] += 1;
        stats->reason_counts[form->reason_id] += 1;
        stats->encoder_counts[form->encoder_family] += 1;
        stats->test_counts[form->test_class] += 1;
        stats->token_count += form->token_count;
    }
    bool packed_mnemonic = xed_generated_pack_name_lookup(arena, &pool, mnemonic_entries, mnemonic_count, &mnemonic_ranges_blob,
                                                          &mnemonic_candidates_blob);
    bool packed_iclass = xed_generated_pack_name_lookup(arena, &pool, iclass_entries, iclass_count, &iclass_ranges_blob,
                                                        &iclass_candidates_blob);
    bool packed_iform = xed_generated_pack_name_lookup(arena, &pool, iform_entries, iform_count, &iform_ranges_blob,
                                                       &iform_candidates_blob);
    bool packed_form_hash = xed_generated_pack_hash_lookup(form_hash_entries, form_hash_count, &form_hash_ranges_blob,
                                                            &form_hash_candidates_blob);
    bool packed_coverage_hash = xed_generated_pack_hash_lookup(coverage_hash_entries, coverage_hash_count, &coverage_hash_ranges_blob,
                                                               &coverage_hash_candidates_blob);
    if (!packed_mnemonic || !packed_iclass || !packed_iform || !packed_form_hash || !packed_coverage_hash)
    {
        return false;
    }

    xed_generated_emit_preamble(output);
    xed_generated_append_x86_base64_value_preamble(output);
    xed_generated_emit_x86_chunked_string_pool(output, arena, &pool, sorted);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_FORM_COUNT"), (u32)forms.length);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_OPERAND_COUNT"), operand_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_COVERAGE_COUNT"), (u32)forms.length);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_INDEX_CAPACITY"), index_capacity);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_MNEMONIC_RANGE_COUNT"), mnemonic_range_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_MNEMONIC_CANDIDATE_COUNT"), mnemonic_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_ICLASS_RANGE_COUNT"), iclass_range_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_ICLASS_CANDIDATE_COUNT"), iclass_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_IFORM_RANGE_COUNT"), iform_range_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_IFORM_CANDIDATE_COUNT"), iform_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_FORM_HASH_RANGE_COUNT"), form_hash_range_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_FORM_HASH_CANDIDATE_COUNT"), form_hash_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_COVERAGE_HASH_RANGE_COUNT"), coverage_hash_range_count);
    xed_generated_emit_x86_count_macro(output, S8("BUSTER_X86_GENERATED_COVERAGE_HASH_CANDIDATE_COUNT"), coverage_hash_count);
    arena_append_string8(output, S8("\n"));
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_operands_blob"), operands_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_forms_blob"), forms_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_mnemonic_ranges_blob"), mnemonic_ranges_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_mnemonic_candidates_blob"), mnemonic_candidates_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_iclass_ranges_blob"), iclass_ranges_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_iclass_candidates_blob"), iclass_candidates_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_iform_ranges_blob"), iform_ranges_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_iform_candidates_blob"), iform_candidates_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_form_hash_ranges_blob"), form_hash_ranges_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_form_hash_candidates_blob"), form_hash_candidates_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_coverage_hash_ranges_blob"), coverage_hash_ranges_blob);
    xed_generated_emit_x86_base64_blob(output, S8("buster_x86_generated_coverage_hash_candidates_blob"), coverage_hash_candidates_blob);
    xed_generated_emit_x86_record_accessors(output);
    xed_generated_emit_x86_text_index_accessors(output, S8("BUSTER_X86_GENERATED_MNEMONIC"), S8("buster_x86_generated_mnemonic"));
    xed_generated_emit_x86_text_index_accessors(output, S8("BUSTER_X86_GENERATED_ICLASS"), S8("buster_x86_generated_iclass"));
    xed_generated_emit_x86_text_index_accessors(output, S8("BUSTER_X86_GENERATED_IFORM"), S8("buster_x86_generated_iform"));
    xed_generated_emit_x86_hash_index_accessors(output, S8("BUSTER_X86_GENERATED_FORM_HASH"), S8("buster_x86_generated_form_hash"));
    xed_generated_emit_x86_hash_index_accessors(output, S8("BUSTER_X86_GENERATED_COVERAGE_HASH"), S8("buster_x86_generated_coverage_hash"));
    arena_append_string8(output, S8("\n#include \"x86_64-coverage.generated.inc\"\n\n#endif\n"));

    arena_append_string8(coverage_output,
                         S8("/* Generated by build import_assembly_metadata; do not edit. */\n"));
    xed_generated_emit_x86_base64_blob(coverage_output, S8("buster_x86_generated_coverage_blob"), coverage_blob);
    xed_generated_emit_x86_coverage_accessor(coverage_output);

    stats->operand_count = operand_count;
    stats->string_pool_bytes = pool.byte_count;
    stats->header_bytes = arena_buffer_size(output);
    stats->coverage_bytes = arena_buffer_size(coverage_output);
    return stats->coverage_counts[XED_GENERATED_COVERAGE_UNCLASSIFIED] == 0;
}

BUSTER_GLOBAL_LOCAL bool aarch64_generated_emit_missing_inventory(Arena* output, Aarch64ImportRecordList records, u64* row_count)
{
    char hash_buffer[32];
    u64 count = 0;
    for (Aarch64ImportRecord* record = records.first; record; record = record->next)
    {
        if (record->coverage_class != AARCH64_IMPORT_COVERAGE_RESERVED_UNENCODABLE &&
            record->coverage_class != AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN)
        {
            continue;
        }
        arena_append_string8(output, S8("{\"name\":"));
        arena_append_json_string(output, record->name);
        arena_append_string8(output, S8(",\"source_hash\":"));
        int hash_length = snprintf(hash_buffer, sizeof(hash_buffer), "\"%016llx\"", (unsigned long long)record->source_hash);
        if (hash_length > 0)
        {
            arena_append_string8(output, (String8){.pointer = (char8*)hash_buffer, .length = (u64)hash_length});
        }
        arena_append_string8(output, S8(",\"name_hash\":"));
        hash_length = snprintf(hash_buffer, sizeof(hash_buffer), "\"%016llx\"", (unsigned long long)record->name_hash);
        if (hash_length > 0)
        {
            arena_append_string8(output, (String8){.pointer = (char8*)hash_buffer, .length = (u64)hash_length});
        }
        arena_append_string8(output, S8(",\"normalized_form_id\":"));
        xed_generated_append_decimal(output, record->normalized_form_id);
        arena_append_string8(output, S8(",\"classification\":"));
        xed_generated_append_decimal(output, record->coverage_class);
        arena_append_string8(output, S8(",\"reason_id\":"));
        xed_generated_append_decimal(output, record->reason_id);
        arena_append_string8(output, S8(",\"encoder_family\":"));
        xed_generated_append_decimal(output, record->encoder_family);
        arena_append_string8(output, S8(",\"test_class\":"));
        xed_generated_append_decimal(output, record->test_class);
        arena_append_string8(output, S8("}\n"));
        count += 1;
    }
    *row_count = count;
    return true;
}

BUSTER_GLOBAL_LOCAL void aarch64_import_self_test_append_record(Arena* output, String8 name, u32 mode, String8 assembly,
                                                                 String8 out, String8 in, String8 predicates)
{
    arena_append_string8(output, S8("{\"name\":"));
    arena_append_json_string(output, name);
    arena_append_string8(output, S8(",\"inst\":["));
    for (u32 bit = 0; bit < 32; bit += 1)
    {
        if (bit)
        {
            arena_append_char8(output, ',');
        }
        bool variable = false;
        String8 variable_name = {0};
        if (mode == 1 && bit < 26)
        {
            variable = true;
            variable_name = S8("addr");
        }
        else if (mode == 2 && bit < 5)
        {
            variable = true;
            variable_name = S8("Vd");
        }
        else if (mode == 2 && bit == 5)
        {
            variable = true;
            variable_name = S8("idx");
        }
        else if (mode == 3 && bit < 5)
        {
            variable = true;
            variable_name = S8("Zd");
        }
        else if (mode == 3 && bit >= 5 && bit < 8)
        {
            variable = true;
            variable_name = S8("Pg");
        }
        else if (mode == 4 && bit == 0)
        {
            arena_append_string8(output, S8("{\"kind\":\"var\",\"printable\":\"ZAda\",\"var\":\"ZAda\"}"));
            continue;
        }
        else if (mode == 5 && bit == 0)
        {
            arena_append_string8(output, S8("{\"kind\":\"mystery\",\"printable\":\"X\",\"var\":\"X\"}"));
            continue;
        }
        else if (mode == 6 && bit < 2)
        {
            variable = true;
            variable_name = S8("conflict");
        }
        if (variable)
        {
            arena_append_string8(output, S8("{\"index\":"));
            xed_generated_append_decimal(output, (mode == 6 && bit == 1) ? 0 : (mode == 2 && bit == 5 ? 0 :
                                                                                         (mode == 3 && bit >= 5 ? bit - 5 : bit)));
            arena_append_string8(output, S8(",\"kind\":\"varbit\",\"printable\":"));
            arena_append_char8(output, '\"');
            arena_append_string8(output, variable_name);
            arena_append_string8(output, S8("{0}\",\"var\":"));
            arena_append_json_string(output, variable_name);
            arena_append_char8(output, '}');
        }
        else
        {
            xed_generated_append_decimal(output, (mode == 1 && bit >= 26) ? (bit == 26 || bit == 28) : (mode == 7 && bit == 0));
        }
    }
    arena_append_string8(output, S8("],\"asm\":"));
    arena_append_json_string(output, assembly);
    arena_append_string8(output, S8(",\"out\":"));
    arena_append_json_string(output, out);
    arena_append_string8(output, S8(",\"in\":"));
    arena_append_json_string(output, in);
    arena_append_string8(output, S8(",\"predicates\":"));
    arena_append_string8(output, predicates);
    arena_append_string8(output, S8("}\n"));
}

BUSTER_GLOBAL_LOCAL Aarch64ImportRecord* aarch64_import_self_test_find_record(Aarch64ImportRecordList records, String8 name)
{
    for (Aarch64ImportRecord* record = records.first; record; record = record->next)
    {
        if (string_equal(record->name, name))
        {
            return record;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL u64 xed_import_unique_record_value_count(Arena* arena, XedImportRecordList records, bool iform)
{
    String8* values = arena_allocate(arena, String8, records.count);
    u64 value_count = 0;
    for (XedImportRecord* record = records.first; record; record = record->next)
    {
        String8 value = iform ? record->iform : record->iclass;
        if (value.length)
        {
            values[value_count++] = value;
        }
    }
    qsort(values, value_count, sizeof(values[0]), assembly_import_string_compare);
    u64 unique_count = 0;
    for (u64 index = 0; index < value_count; index += 1)
    {
        if (!index || !string_equal(values[index - 1], values[index]))
        {
            unique_count += 1;
        }
    }
    return unique_count;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_write(Arena* arena, String8 directory, String8 name, String8 content)
{
    String8 path = path_join(arena, directory, name);
    if (!file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(content)))
    {
        string_print(S8("error: failed to write {S8}\n"), path);
        return false;
    }
    return true;
}

typedef struct AssemblyImportFullManifestData AssemblyImportFullManifestData;
struct AssemblyImportFullManifestData
{
    String8 generation_mode;
    String8 acceptance_status;
    u64 xed_config_file_count;
    u64 xed_source_file_count;
    String8 xed_config_checksum;
    String8 xed_input_kind;
    String8 xed_source_identity;
    String8 xed_raw_snapshot_provenance;
    String8 xed_input_checksum;
    String8 xed_output_checksum;
    u64 xed_form_count;
    u64 xed_iclass_count;
    u64 xed_iform_count;
    u64 xed_missing_iform_count;
    String8 generated_output_checksum;
    String8 generated_coverage_checksum;
    XedGeneratedTableStats xed_stats;
    String8 aarch64_generated_output_checksum;
    String8 aarch64_generated_coverage_checksum;
    String8 aarch64_alias_source_identity;
    String8 aarch64_inventory_checksum;
    u64 aarch64_inventory_bytes;
    String8 llvm_input_kind;
    String8 llvm_source_identity;
    String8 llvm_raw_snapshot_provenance;
    String8 llvm_input_checksum;
    String8 llvm_normalized_input_checksum;
    String8 llvm_output_checksum;
    u64 llvm_instruction_count;
    Aarch64GeneratedTableStats aarch64_stats;
};

BUSTER_GLOBAL_LOCAL String8 assembly_import_format_full_manifest(Arena* arena, AssemblyImportFullManifestData data)
{
    return string_format(
        arena,
        S8("{{\n"
           "  \"schema_version\": 4,\n"
           "  \"generation_mode\": \"{S8}\",\n"
           "  \"acceptance_status\": \"{S8}\",\n"
           "  \"checksum_algorithm\": \"xxh64\",\n"
           "  \"xed\": {{\n"
           "    \"source_url\": \"https://github.com/intelxed/xed\",\n"
           "    \"release\": \"v2026.07.15\",\n"
           "    \"commit\": \"519c843c86547e2003f5a404a53358a7dcfb82f3\",\n"
           "    \"license\": \"Apache-2.0\",\n"
           "    \"config_file_count\": {u64},\n"
           "    \"source_file_count\": {u64},\n"
           "    \"config_checksum\": \"{S8}\",\n"
           "    \"input_kind\": \"{S8}\",\n"
           "    \"source_identity\": \"{S8}\",\n"
           "    \"raw_snapshot_provenance\": {S8},\n"
           "    \"input_checksum\": \"{S8}\",\n"
           "    \"output_checksum\": \"{S8}\",\n"
           "    \"form_count\": {u64},\n"
           "    \"iclass_count\": {u64},\n"
           "    \"iform_count\": {u64},\n"
           "    \"missing_iform_count\": {u64}\n"
           "  }},\n"
           "  \"generated\": {{\n"
           "    \"header\": \"x86_64-assembly.generated.h\",\n"
           "    \"header_checksum\": \"{S8}\",\n"
           "    \"header_bytes\": {u64},\n"
           "    \"coverage\": \"x86_64-coverage.generated.inc\",\n"
           "    \"coverage_checksum\": \"{S8}\",\n"
           "    \"coverage_bytes\": {u64},\n"
           "    \"operand_count\": {u64},\n"
           "    \"token_count\": {u64},\n"
           "    \"string_pool_bytes\": {u64},\n"
           "    \"classification_counts\": {{\"DIRECT\": {u64}, \"NORMALIZED\": {u64}, \"NOT64\": {u64}, \"PRIVILEGED\": {u64}, \"RESERVED\": {u64}, \"UNSUPPORTED_TOKEN\": {u64}, \"UNCLASSIFIED\": {u64}}},\n"
           "    \"reason_counts\": {{\"NONE\": {u64}, \"MODE_NOT64\": {u64}, \"CPL0\": {u64}, \"UNKNOWN_PATTERN_TOKEN\": {u64}, \"UNKNOWN_OPERAND_TOKEN\": {u64}}}\n"
           "  }},\n"
           "  \"aarch64_generated\": {{\n"
           "    \"header\": \"aarch64-assembly.generated.h\",\n"
           "    \"header_checksum\": \"{S8}\",\n"
           "    \"header_bytes\": {u64},\n"
           "    \"coverage\": \"aarch64-coverage.generated.inc\",\n"
           "    \"coverage_checksum\": \"{S8}\",\n"
           "    \"coverage_bytes\": {u64},\n"
           "    \"canonical_form_count\": {u64},\n"
           "    \"field_count\": {u64},\n"
           "    \"segment_count\": {u64},\n"
           "    \"operand_count\": {u64},\n"
           "    \"predicate_count\": {u64},\n"
           "    \"string_pool_bytes\": {u64},\n"
           "    \"mnemonic_range_count\": {u64},\n"
           "    \"signature_range_count\": {u64},\n"
           "    \"mnemonic_candidate_count\": {u64},\n"
           "    \"signature_candidate_count\": {u64},\n"
           "    \"classification_counts\": {{\"DIRECT\": {u64}, \"NORMALIZED\": {u64}, \"ALIAS\": {u64}, \"PRIVILEGED/SYSTEM\": {u64}, \"RESERVED/UNENCODABLE\": {u64}, \"UNSUPPORTED_TOKEN\": {u64}, \"UNCLASSIFIED\": {u64}}},\n"
           "    \"reason_counts\": {{\"NONE\": {u64}, \"ALIAS_OF_CANONICAL\": {u64}, \"SYSTEM_OR_PRIVILEGED\": {u64}, \"UNMAPPED_VARIABLE\": {u64}, \"CONFLICTING_BIT_ASSIGNMENT\": {u64}, \"MALFORMED_DAG\": {u64}, \"MALFORMED_TEMPLATE\": {u64}, \"UNKNOWN_FIELD\": {u64}, \"UNKNOWN_PREDICATE\": {u64}, \"MISSING_OPERAND\": {u64}, \"INVALID_JSON\": {u64}, \"NULL_FIELD\": {u64}, \"UNPROVEN_FIELD_SEMANTICS\": {u64}, \"UNPROVEN_OPERAND_KIND\": {u64}, \"UNPROVEN_IMMEDIATE_RANGE\": {u64}, \"UNPROVEN_MEMORY_FORM\": {u64}, \"UNPROVEN_TIED_OPERAND\": {u64}, \"UNPROVEN_CORRESPONDENCE\": {u64}, \"UNSUPPORTED_ADDRESS_GRAMMAR\": {u64}}},\n"
           "    \"feature_count\": {u64},\n"
           "    \"encoder_family_counts\": {{\"SCALAR_INTEGER\": {u64}, \"BRANCH\": {u64}, \"LOAD_STORE\": {u64}, \"FP_SIMD_NEON\": {u64}, \"SVE_SVE2\": {u64}, \"SME_SME2\": {u64}, \"MTE\": {u64}, \"ATOMIC_LSE\": {u64}, \"CRYPTO\": {u64}, \"SYSTEM\": {u64}}},\n"
           "    \"test_class_counts\": {{\"SCALAR\": {u64}, \"BRANCH\": {u64}, \"MEMORY\": {u64}, \"SIMD_LIST_LANE\": {u64}, \"SVE_PREDICATE\": {u64}, \"SME_SYSTEM\": {u64}, \"IMMEDIATE\": {u64}, \"SCHEMA\": {u64}}},\n"
           "    \"alias_source\": \"{S8}\",\n"
           "    \"alias_count\": {u64},\n"
           "    \"missing_field_inventory\": {{\"artifact\": \"aarch64-missing-fields.generated.jsonl\", \"checksum\": \"{S8}\", \"bytes\": {u64}, \"classification_filter\": [\"RESERVED/UNENCODABLE\", \"UNSUPPORTED_TOKEN\"], \"reserved_unencodable_count\": {u64}, \"unsupported_token_count\": {u64}, \"unclassified_count\": {u64}}}\n"
           "  }},\n"
           "  \"llvm\": {{\n"
           "    \"source_url\": \"https://github.com/llvm/llvm-project\",\n"
           "    \"release\": \"llvmorg-22.1.8\",\n"
           "    \"commit\": \"ca7933e47d3a3451d81e72ac174dcb5aa28b59d1\",\n"
           "    \"license\": \"Apache-2.0 WITH LLVM-exception\",\n"
           "    \"input_kind\": \"{S8}\",\n"
           "    \"source_identity\": \"{S8}\",\n"
           "    \"raw_snapshot_provenance\": {S8},\n"
           "    \"input_checksum\": \"{S8}\",\n"
           "    \"normalized_input_checksum\": \"{S8}\",\n"
           "    \"output_checksum\": \"{S8}\",\n"
           "    \"instruction_count\": {u64}\n"
           "  }}\n"
           "}\n"),
        data.generation_mode, data.acceptance_status, data.xed_config_file_count, data.xed_source_file_count, data.xed_config_checksum,
        data.xed_input_kind, data.xed_source_identity, data.xed_raw_snapshot_provenance, data.xed_input_checksum, data.xed_output_checksum,
        data.xed_form_count, data.xed_iclass_count, data.xed_iform_count, data.xed_missing_iform_count, data.generated_output_checksum,
        data.xed_stats.header_bytes, data.generated_coverage_checksum, data.xed_stats.coverage_bytes, data.xed_stats.operand_count,
        data.xed_stats.token_count, data.xed_stats.string_pool_bytes, data.xed_stats.coverage_counts[0], data.xed_stats.coverage_counts[1],
        data.xed_stats.coverage_counts[2],
        data.xed_stats.coverage_counts[3], data.xed_stats.coverage_counts[4], data.xed_stats.coverage_counts[5], data.xed_stats.coverage_counts[6],
        data.xed_stats.reason_counts[0], data.xed_stats.reason_counts[1], data.xed_stats.reason_counts[2], data.xed_stats.reason_counts[3],
        data.xed_stats.reason_counts[4], data.aarch64_generated_output_checksum, data.aarch64_stats.header_bytes,
        data.aarch64_generated_coverage_checksum, data.aarch64_stats.coverage_bytes, data.aarch64_stats.canonical_form_count,
        data.aarch64_stats.field_count, data.aarch64_stats.segment_count, data.aarch64_stats.operand_count, data.aarch64_stats.predicate_count,
        data.aarch64_stats.string_pool_bytes, data.aarch64_stats.mnemonic_range_count, data.aarch64_stats.signature_range_count,
        data.aarch64_stats.mnemonic_candidate_count, data.aarch64_stats.signature_candidate_count, data.aarch64_stats.coverage_counts[0],
        data.aarch64_stats.coverage_counts[1], data.aarch64_stats.coverage_counts[2], data.aarch64_stats.coverage_counts[3],
        data.aarch64_stats.coverage_counts[4], data.aarch64_stats.coverage_counts[5], data.aarch64_stats.coverage_counts[6],
        data.aarch64_stats.reason_counts[0], data.aarch64_stats.reason_counts[1], data.aarch64_stats.reason_counts[2],
        data.aarch64_stats.reason_counts[3], data.aarch64_stats.reason_counts[4], data.aarch64_stats.reason_counts[5],
        data.aarch64_stats.reason_counts[6], data.aarch64_stats.reason_counts[7], data.aarch64_stats.reason_counts[8],
        data.aarch64_stats.reason_counts[9], data.aarch64_stats.reason_counts[10], data.aarch64_stats.reason_counts[11],
        data.aarch64_stats.reason_counts[12], data.aarch64_stats.reason_counts[13], data.aarch64_stats.reason_counts[14],
        data.aarch64_stats.reason_counts[15], data.aarch64_stats.reason_counts[16], data.aarch64_stats.reason_counts[17],
        data.aarch64_stats.reason_counts[18], data.aarch64_stats.predicate_feature_count, data.aarch64_stats.encoder_counts[0],
        data.aarch64_stats.encoder_counts[1], data.aarch64_stats.encoder_counts[2], data.aarch64_stats.encoder_counts[3],
        data.aarch64_stats.encoder_counts[4], data.aarch64_stats.encoder_counts[5], data.aarch64_stats.encoder_counts[6],
        data.aarch64_stats.encoder_counts[7], data.aarch64_stats.encoder_counts[8], data.aarch64_stats.encoder_counts[9],
        data.aarch64_stats.test_counts[0], data.aarch64_stats.test_counts[1], data.aarch64_stats.test_counts[2],
        data.aarch64_stats.test_counts[3], data.aarch64_stats.test_counts[4], data.aarch64_stats.test_counts[5],
        data.aarch64_stats.test_counts[6], data.aarch64_stats.test_counts[7], data.aarch64_alias_source_identity,
        data.aarch64_stats.coverage_counts[2], data.aarch64_inventory_checksum, data.aarch64_inventory_bytes,
        data.aarch64_stats.coverage_counts[4], data.aarch64_stats.coverage_counts[5], data.aarch64_stats.coverage_counts[6],
        data.llvm_input_kind, data.llvm_source_identity, data.llvm_raw_snapshot_provenance, data.llvm_input_checksum,
        data.llvm_normalized_input_checksum, data.llvm_output_checksum, data.llvm_instruction_count);
}

typedef struct AssemblyImportManifestFieldExpectation AssemblyImportManifestFieldExpectation;
struct AssemblyImportManifestFieldExpectation
{
    String8 key;
    String8 raw_value;
};

BUSTER_GLOBAL_LOCAL bool assembly_import_manifest_expect_fields(String8 object, AssemblyImportManifestFieldExpectation* expected, u64 expected_count)
{
    JsonParser parser = {.text = object};
    bool valid = json_consume(&parser, '{');
    u64 actual_count = 0;
    while (valid)
    {
        String8 key = {0};
        String8 value = {0};
        if (!json_raw_object_next(&parser, &key, &value, &valid))
        {
            break;
        }
        actual_count += 1;
    }
    json_skip_whitespace(&parser);
    valid = valid && parser.index == object.length && actual_count == expected_count;
    for (u64 index = 0; valid && index < expected_count; index += 1)
    {
        String8 value = {0};
        valid = json_raw_object_find(object, expected[index].key, &value) && string_equal(value, expected[index].raw_value);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 assembly_import_manifest_raw_string(Arena* arena, String8 value)
{
    return string_format(arena, S8("\"{S8}\""), value);
}

BUSTER_GLOBAL_LOCAL String8 assembly_import_manifest_raw_u64(Arena* arena, u64 value)
{
    return string_format(arena, S8("{u64}"), value);
}

BUSTER_GLOBAL_LOCAL bool assembly_import_manifest_format_self_test(Arena* arena)
{
    AssemblyImportFullManifestData data = {
        .generation_mode = S8("manifest-regression"),
        .acceptance_status = S8("blocked"),
        .xed_config_file_count = 11,
        .xed_source_file_count = 12,
        .xed_config_checksum = S8("xed-config-checksum"),
        .xed_input_kind = S8("xed_enc_instructions"),
        .xed_source_identity = S8("xed-source-identity"),
        .xed_raw_snapshot_provenance = S8("false"),
        .xed_input_checksum = S8("xed-input-checksum"),
        .xed_output_checksum = S8("xed-output-checksum"),
        .xed_form_count = 13,
        .xed_iclass_count = 14,
        .xed_iform_count = 15,
        .xed_missing_iform_count = 16,
        .generated_output_checksum = S8("x86-header-checksum"),
        .generated_coverage_checksum = S8("xed-coverage-checksum"),
        .aarch64_generated_output_checksum = S8("a64-header-checksum"),
        .aarch64_generated_coverage_checksum = S8("a64-coverage-checksum"),
        .aarch64_alias_source_identity = S8("alias-source-identity"),
        .aarch64_inventory_checksum = S8("inventory-checksum"),
        .aarch64_inventory_bytes = 17,
        .llvm_input_kind = S8("llvm_tblgen_json"),
        .llvm_source_identity = S8("llvm-source-identity"),
        .llvm_raw_snapshot_provenance = S8("true"),
        .llvm_input_checksum = S8("llvm-input-checksum"),
        .llvm_normalized_input_checksum = S8("llvm-normalized-checksum"),
        .llvm_output_checksum = S8("llvm-output-checksum"),
        .llvm_instruction_count = 18,
    };
    for (u32 index = 0; index < XED_GENERATED_COVERAGE_COUNT; index += 1)
    {
        data.xed_stats.coverage_counts[index] = 100 + index;
    }
    for (u32 index = 0; index < XED_GENERATED_REASON_COUNT; index += 1)
    {
        data.xed_stats.reason_counts[index] = 200 + index;
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(data.xed_stats.encoder_counts); index += 1)
    {
        data.xed_stats.encoder_counts[index] = 300 + index;
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(data.xed_stats.test_counts); index += 1)
    {
        data.xed_stats.test_counts[index] = 400 + index;
    }
    data.xed_stats.operand_count = 500;
    data.xed_stats.token_count = 501;
    data.xed_stats.string_pool_bytes = 502;
    data.xed_stats.header_bytes = 503;
    data.xed_stats.coverage_bytes = 504;
    for (u32 index = 0; index < AARCH64_IMPORT_COVERAGE_COUNT; index += 1)
    {
        data.aarch64_stats.coverage_counts[index] = 600 + index;
    }
    for (u32 index = 0; index < AARCH64_IMPORT_REASON_COUNT; index += 1)
    {
        data.aarch64_stats.reason_counts[index] = 700 + index;
    }
    for (u32 index = 0; index < AARCH64_IMPORT_ENCODER_COUNT; index += 1)
    {
        data.aarch64_stats.encoder_counts[index] = 800 + index;
    }
    for (u32 index = 0; index < AARCH64_IMPORT_TEST_COUNT; index += 1)
    {
        data.aarch64_stats.test_counts[index] = 900 + index;
    }
    data.aarch64_stats.canonical_form_count = 1000;
    data.aarch64_stats.field_count = 1001;
    data.aarch64_stats.segment_count = 1002;
    data.aarch64_stats.operand_count = 1003;
    data.aarch64_stats.predicate_count = 1004;
    data.aarch64_stats.predicate_feature_count = 1005;
    data.aarch64_stats.mnemonic_range_count = 1006;
    data.aarch64_stats.signature_range_count = 1007;
    data.aarch64_stats.mnemonic_candidate_count = 1008;
    data.aarch64_stats.signature_candidate_count = 1009;
    data.aarch64_stats.string_pool_bytes = 1010;
    data.aarch64_stats.header_bytes = 1011;
    data.aarch64_stats.coverage_bytes = 1012;

    String8 manifest = assembly_import_format_full_manifest(arena, data);
    String8 root = manifest;
    String8 xed = {0};
    String8 generated = {0};
    String8 aarch64_generated = {0};
    String8 llvm = {0};
    bool result = json_raw_object_find(root, S8("xed"), &xed) && json_raw_object_find(root, S8("generated"), &generated) &&
                  json_raw_object_find(root, S8("aarch64_generated"), &aarch64_generated) && json_raw_object_find(root, S8("llvm"), &llvm);
    String8 root_values[] = {
        S8("4"), assembly_import_manifest_raw_string(arena, data.generation_mode), assembly_import_manifest_raw_string(arena, data.acceptance_status),
        S8("\"xxh64\""), xed, generated, aarch64_generated, llvm,
    };
    String8 root_keys[] = {S8("schema_version"), S8("generation_mode"), S8("acceptance_status"), S8("checksum_algorithm"), S8("xed"),
                           S8("generated"), S8("aarch64_generated"), S8("llvm")};
    AssemblyImportManifestFieldExpectation root_expected[BUSTER_ARRAY_LENGTH(root_keys)] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(root_expected); index += 1)
    {
        root_expected[index] = (AssemblyImportManifestFieldExpectation){.key = root_keys[index], .raw_value = root_values[index]};
    }
    result = result && assembly_import_manifest_expect_fields(root, root_expected, BUSTER_ARRAY_LENGTH(root_expected));

    String8 xed_keys[] = {S8("source_url"), S8("release"), S8("commit"), S8("license"), S8("config_file_count"), S8("source_file_count"),
                          S8("config_checksum"), S8("input_kind"), S8("source_identity"), S8("raw_snapshot_provenance"), S8("input_checksum"),
                          S8("output_checksum"), S8("form_count"), S8("iclass_count"), S8("iform_count"), S8("missing_iform_count")};
    AssemblyImportManifestFieldExpectation xed_expected[BUSTER_ARRAY_LENGTH(xed_keys)] = {
        {.key = xed_keys[0], .raw_value = S8("\"https://github.com/intelxed/xed\"")},
        {.key = xed_keys[1], .raw_value = S8("\"v2026.07.15\"")},
        {.key = xed_keys[2], .raw_value = S8("\"519c843c86547e2003f5a404a53358a7dcfb82f3\"")},
        {.key = xed_keys[3], .raw_value = S8("\"Apache-2.0\"")},
        {.key = xed_keys[4], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_config_file_count)},
        {.key = xed_keys[5], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_source_file_count)},
        {.key = xed_keys[6], .raw_value = assembly_import_manifest_raw_string(arena, data.xed_config_checksum)},
        {.key = xed_keys[7], .raw_value = assembly_import_manifest_raw_string(arena, data.xed_input_kind)},
        {.key = xed_keys[8], .raw_value = assembly_import_manifest_raw_string(arena, data.xed_source_identity)},
        {.key = xed_keys[9], .raw_value = S8("false")},
        {.key = xed_keys[10], .raw_value = assembly_import_manifest_raw_string(arena, data.xed_input_checksum)},
        {.key = xed_keys[11], .raw_value = assembly_import_manifest_raw_string(arena, data.xed_output_checksum)},
        {.key = xed_keys[12], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_form_count)},
        {.key = xed_keys[13], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_iclass_count)},
        {.key = xed_keys[14], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_iform_count)},
        {.key = xed_keys[15], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_missing_iform_count)},
    };
    result = result && assembly_import_manifest_expect_fields(xed, xed_expected, BUSTER_ARRAY_LENGTH(xed_expected));

    String8 generated_classification = {0};
    String8 generated_reasons = {0};
    result = result && json_raw_object_find(generated, S8("classification_counts"), &generated_classification) &&
             json_raw_object_find(generated, S8("reason_counts"), &generated_reasons);
    String8 generated_keys[] = {S8("header"), S8("header_checksum"), S8("header_bytes"), S8("coverage"), S8("coverage_checksum"),
                               S8("coverage_bytes"), S8("operand_count"), S8("token_count"), S8("string_pool_bytes"), S8("classification_counts"),
                               S8("reason_counts")};
    AssemblyImportManifestFieldExpectation generated_expected[BUSTER_ARRAY_LENGTH(generated_keys)] = {
        {.key = generated_keys[0], .raw_value = S8("\"x86_64-assembly.generated.h\"")},
        {.key = generated_keys[1], .raw_value = assembly_import_manifest_raw_string(arena, data.generated_output_checksum)},
        {.key = generated_keys[2], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_stats.header_bytes)},
        {.key = generated_keys[3], .raw_value = S8("\"x86_64-coverage.generated.inc\"")},
        {.key = generated_keys[4], .raw_value = assembly_import_manifest_raw_string(arena, data.generated_coverage_checksum)},
        {.key = generated_keys[5], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_stats.coverage_bytes)},
        {.key = generated_keys[6], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_stats.operand_count)},
        {.key = generated_keys[7], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_stats.token_count)},
        {.key = generated_keys[8], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_stats.string_pool_bytes)},
        {.key = generated_keys[9], .raw_value = generated_classification},
        {.key = generated_keys[10], .raw_value = generated_reasons},
    };
    result = result && assembly_import_manifest_expect_fields(generated, generated_expected, BUSTER_ARRAY_LENGTH(generated_expected));

    String8 generated_classification_keys[] = {S8("DIRECT"), S8("NORMALIZED"), S8("NOT64"), S8("PRIVILEGED"), S8("RESERVED"),
                                               S8("UNSUPPORTED_TOKEN"), S8("UNCLASSIFIED")};
    AssemblyImportManifestFieldExpectation generated_classification_expected[BUSTER_ARRAY_LENGTH(generated_classification_keys)] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(generated_classification_expected); index += 1)
    {
        generated_classification_expected[index] = (AssemblyImportManifestFieldExpectation){
            .key = generated_classification_keys[index], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_stats.coverage_counts[index])};
    }
    String8 generated_reason_keys[] = {S8("NONE"), S8("MODE_NOT64"), S8("CPL0"), S8("UNKNOWN_PATTERN_TOKEN"), S8("UNKNOWN_OPERAND_TOKEN")};
    AssemblyImportManifestFieldExpectation generated_reason_expected[BUSTER_ARRAY_LENGTH(generated_reason_keys)] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(generated_reason_expected); index += 1)
    {
        generated_reason_expected[index] = (AssemblyImportManifestFieldExpectation){
            .key = generated_reason_keys[index], .raw_value = assembly_import_manifest_raw_u64(arena, data.xed_stats.reason_counts[index])};
    }
    result = result && assembly_import_manifest_expect_fields(generated_classification, generated_classification_expected,
                                                              BUSTER_ARRAY_LENGTH(generated_classification_expected)) &&
             assembly_import_manifest_expect_fields(generated_reasons, generated_reason_expected, BUSTER_ARRAY_LENGTH(generated_reason_expected));

    String8 aarch64_classification = {0};
    String8 aarch64_reasons = {0};
    String8 aarch64_encoder = {0};
    String8 aarch64_tests = {0};
    String8 inventory = {0};
    result = result && json_raw_object_find(aarch64_generated, S8("classification_counts"), &aarch64_classification) &&
             json_raw_object_find(aarch64_generated, S8("reason_counts"), &aarch64_reasons) &&
             json_raw_object_find(aarch64_generated, S8("encoder_family_counts"), &aarch64_encoder) &&
             json_raw_object_find(aarch64_generated, S8("test_class_counts"), &aarch64_tests) &&
             json_raw_object_find(aarch64_generated, S8("missing_field_inventory"), &inventory);
    String8 aarch64_keys[] = {S8("header"), S8("header_checksum"), S8("header_bytes"), S8("coverage"), S8("coverage_checksum"),
                              S8("coverage_bytes"), S8("canonical_form_count"), S8("field_count"), S8("segment_count"), S8("operand_count"),
                              S8("predicate_count"), S8("string_pool_bytes"), S8("mnemonic_range_count"), S8("signature_range_count"),
                              S8("mnemonic_candidate_count"), S8("signature_candidate_count"), S8("classification_counts"), S8("reason_counts"),
                              S8("feature_count"), S8("encoder_family_counts"), S8("test_class_counts"), S8("alias_source"), S8("alias_count"),
                              S8("missing_field_inventory")};
    AssemblyImportManifestFieldExpectation aarch64_expected[BUSTER_ARRAY_LENGTH(aarch64_keys)] = {
        {.key = aarch64_keys[0], .raw_value = S8("\"aarch64-assembly.generated.h\"")},
        {.key = aarch64_keys[1], .raw_value = assembly_import_manifest_raw_string(arena, data.aarch64_generated_output_checksum)},
        {.key = aarch64_keys[2], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.header_bytes)},
        {.key = aarch64_keys[3], .raw_value = S8("\"aarch64-coverage.generated.inc\"")},
        {.key = aarch64_keys[4], .raw_value = assembly_import_manifest_raw_string(arena, data.aarch64_generated_coverage_checksum)},
        {.key = aarch64_keys[5], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.coverage_bytes)},
        {.key = aarch64_keys[6], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.canonical_form_count)},
        {.key = aarch64_keys[7], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.field_count)},
        {.key = aarch64_keys[8], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.segment_count)},
        {.key = aarch64_keys[9], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.operand_count)},
        {.key = aarch64_keys[10], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.predicate_count)},
        {.key = aarch64_keys[11], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.string_pool_bytes)},
        {.key = aarch64_keys[12], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.mnemonic_range_count)},
        {.key = aarch64_keys[13], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.signature_range_count)},
        {.key = aarch64_keys[14], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.mnemonic_candidate_count)},
        {.key = aarch64_keys[15], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.signature_candidate_count)},
        {.key = aarch64_keys[16], .raw_value = aarch64_classification},
        {.key = aarch64_keys[17], .raw_value = aarch64_reasons},
        {.key = aarch64_keys[18], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.predicate_feature_count)},
        {.key = aarch64_keys[19], .raw_value = aarch64_encoder},
        {.key = aarch64_keys[20], .raw_value = aarch64_tests},
        {.key = aarch64_keys[21], .raw_value = assembly_import_manifest_raw_string(arena, data.aarch64_alias_source_identity)},
        {.key = aarch64_keys[22], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.coverage_counts[2])},
        {.key = aarch64_keys[23], .raw_value = inventory},
    };
    result = result && assembly_import_manifest_expect_fields(aarch64_generated, aarch64_expected, BUSTER_ARRAY_LENGTH(aarch64_expected));

    String8 aarch64_classification_keys[] = {S8("DIRECT"), S8("NORMALIZED"), S8("ALIAS"), S8("PRIVILEGED/SYSTEM"),
                                             S8("RESERVED/UNENCODABLE"), S8("UNSUPPORTED_TOKEN"), S8("UNCLASSIFIED")};
    AssemblyImportManifestFieldExpectation aarch64_classification_expected[BUSTER_ARRAY_LENGTH(aarch64_classification_keys)] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(aarch64_classification_expected); index += 1)
    {
        aarch64_classification_expected[index] = (AssemblyImportManifestFieldExpectation){
            .key = aarch64_classification_keys[index], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.coverage_counts[index])};
    }
    String8 aarch64_reason_keys[] = {S8("NONE"), S8("ALIAS_OF_CANONICAL"), S8("SYSTEM_OR_PRIVILEGED"), S8("UNMAPPED_VARIABLE"),
                                     S8("CONFLICTING_BIT_ASSIGNMENT"), S8("MALFORMED_DAG"), S8("MALFORMED_TEMPLATE"), S8("UNKNOWN_FIELD"),
                                     S8("UNKNOWN_PREDICATE"), S8("MISSING_OPERAND"), S8("INVALID_JSON"), S8("NULL_FIELD"),
                                     S8("UNPROVEN_FIELD_SEMANTICS"), S8("UNPROVEN_OPERAND_KIND"), S8("UNPROVEN_IMMEDIATE_RANGE"),
                                     S8("UNPROVEN_MEMORY_FORM"), S8("UNPROVEN_TIED_OPERAND"), S8("UNPROVEN_CORRESPONDENCE"),
                                     S8("UNSUPPORTED_ADDRESS_GRAMMAR")};
    AssemblyImportManifestFieldExpectation aarch64_reason_expected[BUSTER_ARRAY_LENGTH(aarch64_reason_keys)] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(aarch64_reason_expected); index += 1)
    {
        aarch64_reason_expected[index] = (AssemblyImportManifestFieldExpectation){
            .key = aarch64_reason_keys[index], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.reason_counts[index])};
    }
    String8 aarch64_encoder_keys[] = {S8("SCALAR_INTEGER"), S8("BRANCH"), S8("LOAD_STORE"), S8("FP_SIMD_NEON"), S8("SVE_SVE2"),
                                      S8("SME_SME2"), S8("MTE"), S8("ATOMIC_LSE"), S8("CRYPTO"), S8("SYSTEM")};
    AssemblyImportManifestFieldExpectation aarch64_encoder_expected[BUSTER_ARRAY_LENGTH(aarch64_encoder_keys)] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(aarch64_encoder_expected); index += 1)
    {
        aarch64_encoder_expected[index] = (AssemblyImportManifestFieldExpectation){
            .key = aarch64_encoder_keys[index], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.encoder_counts[index])};
    }
    String8 aarch64_test_keys[] = {S8("SCALAR"), S8("BRANCH"), S8("MEMORY"), S8("SIMD_LIST_LANE"), S8("SVE_PREDICATE"), S8("SME_SYSTEM"),
                                   S8("IMMEDIATE"), S8("SCHEMA")};
    AssemblyImportManifestFieldExpectation aarch64_test_expected[BUSTER_ARRAY_LENGTH(aarch64_test_keys)] = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(aarch64_test_expected); index += 1)
    {
        aarch64_test_expected[index] = (AssemblyImportManifestFieldExpectation){
            .key = aarch64_test_keys[index], .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.test_counts[index])};
    }
    AssemblyImportManifestFieldExpectation inventory_expected[] = {
        {.key = S8("artifact"), .raw_value = S8("\"aarch64-missing-fields.generated.jsonl\"")},
        {.key = S8("checksum"), .raw_value = assembly_import_manifest_raw_string(arena, data.aarch64_inventory_checksum)},
        {.key = S8("bytes"), .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_inventory_bytes)},
        {.key = S8("classification_filter"), .raw_value = S8("[\"RESERVED/UNENCODABLE\", \"UNSUPPORTED_TOKEN\"]")},
        {.key = S8("reserved_unencodable_count"), .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.coverage_counts[4])},
        {.key = S8("unsupported_token_count"), .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.coverage_counts[5])},
        {.key = S8("unclassified_count"), .raw_value = assembly_import_manifest_raw_u64(arena, data.aarch64_stats.coverage_counts[6])},
    };
    result = result && assembly_import_manifest_expect_fields(aarch64_classification, aarch64_classification_expected,
                                                              BUSTER_ARRAY_LENGTH(aarch64_classification_expected)) &&
             assembly_import_manifest_expect_fields(aarch64_reasons, aarch64_reason_expected, BUSTER_ARRAY_LENGTH(aarch64_reason_expected)) &&
             assembly_import_manifest_expect_fields(aarch64_encoder, aarch64_encoder_expected, BUSTER_ARRAY_LENGTH(aarch64_encoder_expected)) &&
             assembly_import_manifest_expect_fields(aarch64_tests, aarch64_test_expected, BUSTER_ARRAY_LENGTH(aarch64_test_expected)) &&
             assembly_import_manifest_expect_fields(inventory, inventory_expected, BUSTER_ARRAY_LENGTH(inventory_expected));

    String8 llvm_keys[] = {S8("source_url"), S8("release"), S8("commit"), S8("license"), S8("input_kind"), S8("source_identity"),
                           S8("raw_snapshot_provenance"), S8("input_checksum"), S8("normalized_input_checksum"), S8("output_checksum"),
                           S8("instruction_count")};
    AssemblyImportManifestFieldExpectation llvm_expected[BUSTER_ARRAY_LENGTH(llvm_keys)] = {
        {.key = llvm_keys[0], .raw_value = S8("\"https://github.com/llvm/llvm-project\"")},
        {.key = llvm_keys[1], .raw_value = S8("\"llvmorg-22.1.8\"")},
        {.key = llvm_keys[2], .raw_value = S8("\"ca7933e47d3a3451d81e72ac174dcb5aa28b59d1\"")},
        {.key = llvm_keys[3], .raw_value = S8("\"Apache-2.0 WITH LLVM-exception\"")},
        {.key = llvm_keys[4], .raw_value = assembly_import_manifest_raw_string(arena, data.llvm_input_kind)},
        {.key = llvm_keys[5], .raw_value = assembly_import_manifest_raw_string(arena, data.llvm_source_identity)},
        {.key = llvm_keys[6], .raw_value = S8("true")},
        {.key = llvm_keys[7], .raw_value = assembly_import_manifest_raw_string(arena, data.llvm_input_checksum)},
        {.key = llvm_keys[8], .raw_value = assembly_import_manifest_raw_string(arena, data.llvm_normalized_input_checksum)},
        {.key = llvm_keys[9], .raw_value = assembly_import_manifest_raw_string(arena, data.llvm_output_checksum)},
        {.key = llvm_keys[10], .raw_value = assembly_import_manifest_raw_u64(arena, data.llvm_instruction_count)},
    };
    result = result && assembly_import_manifest_expect_fields(llvm, llvm_expected, BUSTER_ARRAY_LENGTH(llvm_expected));

    AssemblyImportFullManifestData reduced_data = data;
    reduced_data.generation_mode = S8("audit");
    reduced_data.acceptance_status = S8("blocked");
    reduced_data.xed_input_kind = S8("checked-in-xed-jsonl");
    reduced_data.xed_source_identity = S8("checked-in-x86-artifacts");
    reduced_data.xed_raw_snapshot_provenance = S8("false");
    reduced_data.llvm_input_kind = S8("reduced_jsonl");
    reduced_data.llvm_source_identity = S8("unverified-reduced-jsonl");
    reduced_data.llvm_raw_snapshot_provenance = S8("false");
    reduced_data.llvm_input_checksum = S8("reduced-input-checksum");
    reduced_data.llvm_normalized_input_checksum = S8("normalized-input-checksum");
    reduced_data.llvm_output_checksum = S8("normalized-output-checksum");
    reduced_data.llvm_instruction_count = 7491;
    String8 reduced_manifest = assembly_import_format_full_manifest(arena, reduced_data);
    String8 reduced_llvm = {0};
    String8 reduced_generation_mode = {0};
    String8 reduced_input_kind = {0};
    String8 reduced_input_checksum = {0};
    String8 reduced_normalized_checksum = {0};
    result = result && json_raw_object_find(reduced_manifest, S8("generation_mode"), &reduced_generation_mode) &&
             string_equal(reduced_generation_mode, S8("\"audit\"")) &&
             json_raw_object_find(reduced_manifest, S8("llvm"), &reduced_llvm) &&
             json_raw_object_find(reduced_llvm, S8("input_kind"), &reduced_input_kind) &&
             string_equal(reduced_input_kind, S8("\"reduced_jsonl\"")) &&
             json_raw_object_find(reduced_llvm, S8("input_checksum"), &reduced_input_checksum) &&
             string_equal(reduced_input_checksum, S8("\"reduced-input-checksum\"")) &&
             json_raw_object_find(reduced_llvm, S8("normalized_input_checksum"), &reduced_normalized_checksum) &&
             string_equal(reduced_normalized_checksum, S8("\"normalized-input-checksum\""));
    return result;
}

BUSTER_GLOBAL_LOCAL bool assembly_import_self_test(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(8)});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(8)});
    Arena* schema_a = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(8)});
    Arena* schema_b = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(8)});
    Arena* coverage_a = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(2)});
    Arena* coverage_b = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(2)});
    if (!arena || !output || !schema_a || !schema_b || !coverage_a || !coverage_b)
    {
        if (arena)
        {
            arena_destroy(arena, 1);
        }
        if (output)
        {
            arena_destroy(output, 1);
        }
        if (schema_a)
        {
            arena_destroy(schema_a, 1);
        }
        if (schema_b)
        {
            arena_destroy(schema_b, 1);
        }
        if (coverage_a)
        {
            arena_destroy(coverage_a, 1);
        }
        if (coverage_b)
        {
            arena_destroy(coverage_b, 1);
        }
        return false;
    }

    XedImportRecordList xed = {0};
    bool result = xed_import_parse_file(arena, &xed, S8("fixture.xed.txt"),
                                        S8("INSTRUCTIONS()::\n{\nICLASS: ADD\nIFORM: ADD_GPRv_GPRv\nISA_SET: BASE\nPATTERN: 0x01 MODRM()\nOPERANDS: REG0=GPRv:r REG1=GPRv:r\n}\n"
                                           "{\nICLASS: INCOMPLETE\nPATTERN: 0x02\n}\n"));
    xed_import_emit(output, xed);
    String8 expected_xed =
        S8("{\"source\":\"fixture.xed.txt\",\"iclass\":\"ADD\",\"iform\":\"ADD_GPRv_GPRv\",\"isa_set\":\"BASE\","
           "\"pattern\":\"0x01 MODRM()\",\"operands\":\"REG0=GPRv:r REG1=GPRv:r\"}\n");
    result = !result && xed.count == 1 && string_equal(assembly_import_arena_contents(output), expected_xed);

    XedImportRecordList multiline = {0};
    result = result && xed_import_parse_file(
                            arena, &multiline, S8("multiline.xed.txt"),
                            S8("INSTRUCTIONS()::\n{\nICLASS: MULTI\nPATTERN: 0x10 MODRM() \\\n+  VL128\nOPERANDS: REG0=GPRv:r \\\n+  REG1=GPRv:w # NDD\nPATTERN: 0x11\nOPERANDS:\n}\n")) &&
             multiline.count == 2 && multiline.first->operands_present && string_equal(multiline.first->operand_annotation, S8("NDD")) &&
             multiline.last->operands_present && !multiline.last->operands.length;

    XedImportRecordList malformed = {0};
    result = result && !xed_import_parse_file(arena, &malformed, S8("bad.xed.txt"), S8("{\nUNKNOWN: value\n}"));
    XedImportRecordList unknown_directive = {0};
    result = result && !xed_import_parse_file(arena, &unknown_directive, S8("directive.xed.txt"),
                                              S8("BOGUS()::\n{\nICLASS: BAD\nPATTERN: 0x01\nOPERANDS:\n}\n"));
    result = result && assembly_import_relative_path_is_safe(S8("amd/isa.txt")) &&
             !assembly_import_relative_path_is_safe(S8("../isa.txt")) && !assembly_import_relative_path_is_safe(S8("a/../isa.txt")) &&
             !assembly_import_relative_path_is_safe(S8("/absolute.txt")) && !assembly_import_relative_path_is_safe(S8("a\\..\\isa.txt")) &&
             !assembly_import_relative_path_is_safe(S8("\\absolute.txt")) && assembly_import_config_key_is_known(S8("enc-instructions")) &&
             !assembly_import_config_key_is_known(S8("unknown-key"));
    AssemblyImportPathNode fixture_config = {.relative = S8("files.cfg")};
    AssemblyImportPathList fixture_sources = {0};
    AssemblyImportPathList duplicate_sources = {.first = &fixture_config, .last = &fixture_config, .count = 1};
    result = result && !assembly_import_config_parse(arena, S8("/tmp"), &fixture_config, S8("unknown-key: value\n"), &fixture_sources, false) &&
             !assembly_import_config_parse(arena, S8("/tmp"), &fixture_config, S8("enc-instructions: ../escape.xed.txt\n"), &fixture_sources, false) &&
             !assembly_import_config_parse(arena, S8("/tmp"), &fixture_config, S8("malformed\n"), &fixture_sources, false) &&
             assembly_import_path_list_contains(duplicate_sources, S8("files.cfg"));

    XedImportRecord normalized_record = {
        .source = S8("fixture.xed.txt"),
        .iclass = S8("VADD"),
        .iform = S8("VADD_XMM"),
        .isa_set = S8("AVX512"),
        .category = S8("AVX512"),
        .extension = S8("AVX512EVEX"),
        .attributes = S8("BROADCAST_ENABLED"),
        .pattern = S8("EVV 0x62 V0F38 MOD[mm] MOD!=3 REG[rrr] RM[nnn] MODRM() VL128 mode64 NELEM_FULL() SAE()"),
        .operands = S8("REG0=XMM_R():rw:f32 REG1=MASK1():r:mskw:TXT=ZEROSTR MEM0:r:vv:f32:TXT=BCASTSTR"),
        .operands_present = true,
    };
    XedGeneratedForm normalized_form = {0};
    String8 bad_token = {0};
    bool bad_operand = false;
    result = result && xed_import_normalize_record(&normalized_form, &normalized_record, 0, &bad_token, &bad_operand) &&
             normalized_form.coverage_class == XED_GENERATED_COVERAGE_NORMALIZED &&
             normalized_form.prefix_kind == XED_GENERATED_PREFIX_EVEX && normalized_form.map == XED_GENERATED_MAP_0F38 &&
             (normalized_form.field_flags & (XED_GENERATED_FIELD_MODRM | XED_GENERATED_FIELD_MEMORY)) ==
                 (XED_GENERATED_FIELD_MODRM | XED_GENERATED_FIELD_MEMORY) &&
             (normalized_form.decorator_flags & (XED_GENERATED_DECORATOR_MASK | XED_GENERATED_DECORATOR_ZEROING |
                                                  XED_GENERATED_DECORATOR_BROADCAST | XED_GENERATED_DECORATOR_SAE)) ==
                 (XED_GENERATED_DECORATOR_MASK | XED_GENERATED_DECORATOR_ZEROING | XED_GENERATED_DECORATOR_BROADCAST |
                  XED_GENERATED_DECORATOR_SAE) &&
             normalized_form.tuple_kind == XED_GENERATED_TUPLE_FULL && normalized_form.operands[0].field_source == XED_GENERATED_FIELD_SOURCE_REG &&
             normalized_form.operands[2].field_source == XED_GENERATED_FIELD_SOURCE_RM;

    XedImportRecord tdpbf16ps_record = {
        .source = S8("amx-spr/amx-spr-isa.xed.txt"),
        .iclass = S8("TDPBF16PS"),
        .iform = S8("TDPBF16PS_TMMf32_TMM2bf16_TMM2bf16"),
        .isa_set = S8("AMX_BF16"),
        .category = S8("AMX_TILE"),
        .extension = S8("AMX_TILE"),
        .pattern = S8("VV1 0x5C VF3 V0F38 MOD[0b11] MOD=3 REG[rrr] RM[nnn] W0 VL128 mode64"),
        .operands = S8("REG0=TMM_R():rw:tv:f32 REG1=TMM_B():r:tv:2bf16 REG2=TMM_N():r:tv:2bf16"),
        .operands_present = true,
    };
    XedGeneratedForm tdpbf16ps_form = {0};
    bad_token = (String8){0};
    bad_operand = false;
    result = result && xed_import_normalize_record(&tdpbf16ps_form, &tdpbf16ps_record, 0, &bad_token, &bad_operand) &&
             tdpbf16ps_form.encoder_family == XED_GENERATED_ENCODER_AMX &&
             (tdpbf16ps_form.amx_flags & XED_GENERATED_AMX_TILE_REGISTER) != 0 &&
             (tdpbf16ps_form.amx_flags & XED_GENERATED_AMX_TILE_MEMORY) == 0;

    XedImportRecord tileloadd_record = {
        .source = S8("amx-spr/amx-spr-isa.xed.txt"),
        .iclass = S8("TILELOADD"),
        .iform = S8("TILELOADD_TMMu32_MEMu32"),
        .isa_set = S8("AMX_TILE"),
        .category = S8("AMX_TILE"),
        .extension = S8("AMX_TILE"),
        .pattern = S8("VV1 0x4B VF2 V0F38 MOD[mm] MOD!=3 REG[rrr] RM[0b100] MODRM() SIB W0 VL128 mode64 NOVSR"),
        .operands = S8("REG0=TMM_R():w:tv:u32 MEM0:r:ptr:u32"),
        .operands_present = true,
    };
    XedGeneratedForm tileloadd_form = {0};
    bad_token = (String8){0};
    bad_operand = false;
    result = result && xed_import_normalize_record(&tileloadd_form, &tileloadd_record, 0, &bad_token, &bad_operand) &&
             tileloadd_form.encoder_family == XED_GENERATED_ENCODER_AMX &&
             (tileloadd_form.field_flags & XED_GENERATED_FIELD_MEMORY) != 0 &&
             (tileloadd_form.amx_flags & (XED_GENERATED_AMX_TILE_REGISTER | XED_GENERATED_AMX_TILE_MEMORY)) ==
                 (XED_GENERATED_AMX_TILE_REGISTER | XED_GENERATED_AMX_TILE_MEMORY);

    XedImportRecord tilestored_record = {
        .source = S8("amx-spr/amx-spr-isa.xed.txt"),
        .iclass = S8("TILESTORED"),
        .iform = S8("TILESTORED_MEMu32_TMMu32"),
        .isa_set = S8("AMX_TILE"),
        .category = S8("AMX_TILE"),
        .extension = S8("AMX_TILE"),
        .pattern = S8("VV1 0x4B VF3 V0F38 MOD[mm] MOD!=3 REG[rrr] RM[0b100] MODRM() SIB W0 VL128 mode64 NOVSR"),
        .operands = S8("MEM0:w:ptr:u32 REG0=TMM_R():r:tv:u32"),
        .operands_present = true,
    };
    XedGeneratedForm tilestored_form = {0};
    bad_token = (String8){0};
    bad_operand = false;
    result = result && xed_import_normalize_record(&tilestored_form, &tilestored_record, 0, &bad_token, &bad_operand) &&
             tilestored_form.encoder_family == XED_GENERATED_ENCODER_AMX &&
             (tilestored_form.field_flags & XED_GENERATED_FIELD_MEMORY) != 0 &&
             (tilestored_form.amx_flags & (XED_GENERATED_AMX_TILE_REGISTER | XED_GENERATED_AMX_TILE_MEMORY)) ==
                 (XED_GENERATED_AMX_TILE_REGISTER | XED_GENERATED_AMX_TILE_MEMORY);

    XedImportRecord apx_ndd_record = normalized_record;
    apx_ndd_record.iclass = S8("ADD");
    apx_ndd_record.iform = S8("ADD_APX_NDD");
    apx_ndd_record.isa_set = S8("APX_F");
    apx_ndd_record.category = S8("BINARY");
    apx_ndd_record.extension = S8("APX_F");
    apx_ndd_record.operand_annotation = S8("NDD");
    XedGeneratedForm apx_ndd_form = {0};
    bad_token = (String8){0};
    bad_operand = false;
    result = result && xed_import_normalize_record(&apx_ndd_form, &apx_ndd_record, 0, &bad_token, &bad_operand) &&
             (apx_ndd_form.apx_flags & (XED_GENERATED_APX | XED_GENERATED_APX_NDD)) ==
                 (XED_GENERATED_APX | XED_GENERATED_APX_NDD);

    XedImportRecord bad_pattern_record = normalized_record;
    bad_pattern_record.pattern = S8("BOGUS_PATTERN_TOKEN");
    XedGeneratedForm bad_pattern_form = {0};
    bad_token = (String8){0};
    bad_operand = false;
    result = result && !xed_import_normalize_record(&bad_pattern_form, &bad_pattern_record, 0, &bad_token, &bad_operand) &&
             bad_pattern_form.coverage_class == XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN &&
             bad_pattern_form.reason_id == XED_GENERATED_REASON_UNKNOWN_PATTERN_TOKEN && !bad_operand &&
             string_equal(bad_token, S8("BOGUS_PATTERN_TOKEN"));
    XedImportRecord bad_operand_record = normalized_record;
    bad_operand_record.operands = S8("REG0=BOGUS_OPERAND:r");
    XedGeneratedForm bad_operand_form = {0};
    bad_token = (String8){0};
    bad_operand = false;
    result = result && !xed_import_normalize_record(&bad_operand_form, &bad_operand_record, 0, &bad_token, &bad_operand) &&
             bad_operand_form.coverage_class == XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN &&
             bad_operand_form.reason_id == XED_GENERATED_REASON_UNKNOWN_OPERAND_TOKEN && bad_operand;
    Aarch64ImportRecord immediate_grammar_record = {0};
    Aarch64ImportOperand* scaled_immediate = aarch64_import_operand_add(arena, &immediate_grammar_record, S8("simm7s8:$offset"),
                                                                          S8("simm7s8"), S8("offset"), AARCH64_IMPORT_OPERAND_READ);
    Aarch64ImportOperand* width_suffixed_immediate =
        aarch64_import_operand_add(arena, &immediate_grammar_record, S8("imm0_127_64b:$imm"), S8("imm0_127_64b"), S8("imm"),
                                   AARCH64_IMPORT_OPERAND_READ);
    Aarch64ImportOperand* explicit_range_immediate =
        aarch64_import_operand_add(arena, &immediate_grammar_record, S8("imm32_0_15:$imm"), S8("imm32_0_15"), S8("imm2"),
                                   AARCH64_IMPORT_OPERAND_READ);
    result = result && scaled_immediate && scaled_immediate->kind == AARCH64_IMPORT_OPERAND_IMMEDIATE &&
             scaled_immediate->immediate_min == -64 && scaled_immediate->immediate_max == 63 && scaled_immediate->scale == 8 &&
             (scaled_immediate->immediate_flags & (AARCH64_IMPORT_OPERAND_IMMEDIATE_RANGE_EXACT | AARCH64_IMPORT_OPERAND_SCALE_EXACT)) ==
                 (AARCH64_IMPORT_OPERAND_IMMEDIATE_RANGE_EXACT | AARCH64_IMPORT_OPERAND_SCALE_EXACT) &&
             width_suffixed_immediate && width_suffixed_immediate->immediate_max == 127 && width_suffixed_immediate->register_width == 0 &&
             explicit_range_immediate && explicit_range_immediate->immediate_min == 0 && explicit_range_immediate->immediate_max == 15;
    String8 checksum_a = assembly_import_checksum(arena, S8("deterministic"));
    String8 checksum_b = assembly_import_checksum(arena, S8("deterministic"));
    result = result && string_equal(checksum_a, checksum_b);
    String8 provenance_fixture = S8("artifact-provenance");
    String8 provenance_checksum = assembly_import_checksum(arena, provenance_fixture);
    char8* mutated_bytes = arena_allocate(arena, char8, provenance_fixture.length);
    memcpy(mutated_bytes, provenance_fixture.pointer, provenance_fixture.length);
    mutated_bytes[provenance_fixture.length - 1] ^= 1;
    String8 mutated_fixture = {.pointer = mutated_bytes, .length = provenance_fixture.length};
    result = result && assembly_import_validate_artifact(arena, provenance_fixture, provenance_fixture.length, provenance_checksum) &&
             !assembly_import_validate_artifact(arena, mutated_fixture, provenance_fixture.length, provenance_checksum) &&
             !aarch64_import_reduced_provenance(arena, mutated_fixture, 7491);
    result = result &&
             string_equal(aarch64_import_alias_source_diagnostic(true),
                          S8("audit: AArch64 alias records are present in the raw source but are not imported by this reduced schema path; generated artifacts remain blocked\n")) &&
             string_equal(aarch64_import_alias_source_diagnostic(false),
                          S8("error: AArch64 alias records are present in the raw source but are not imported by this reduced schema path\n")) &&
             !aarch64_import_alias_source_is_fatal(true) && aarch64_import_alias_source_is_fatal(false);
    result = result && assembly_import_manifest_format_self_test(arena);
    Aarch64GeneratedTableStats blocked_stats = {0};
    blocked_stats.coverage_counts[AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN] = 1;
    result = result && !aarch64_import_acceptance_ready(blocked_stats);
    XedGeneratedFormList normalized_forms = {.pointer = &normalized_form, .length = 1};
    XedGeneratedTableStats schema_stats_a = {0};
    XedGeneratedTableStats schema_stats_b = {0};
    result = result && xed_import_emit_generated_tables_packed(schema_a, coverage_a, arena, normalized_forms, &schema_stats_a) &&
             xed_import_emit_generated_tables_packed(schema_b, coverage_b, arena, normalized_forms, &schema_stats_b) &&
             string_equal(assembly_import_arena_contents(schema_a), assembly_import_arena_contents(schema_b)) &&
             string_equal(assembly_import_arena_contents(coverage_a), assembly_import_arena_contents(coverage_b)) &&
             schema_stats_a.header_bytes == schema_stats_b.header_bytes && schema_stats_a.coverage_bytes == schema_stats_b.coverage_bytes &&
             schema_stats_a.coverage_counts[XED_GENERATED_COVERAGE_NORMALIZED] == 1 && schema_stats_a.reason_counts[XED_GENERATED_REASON_NONE] == 1;
    arena_reset_to_start(schema_a);
    arena_reset_to_start(coverage_a);
    XedGeneratedFormList unsupported_forms = {.pointer = &bad_pattern_form, .length = 1};
    XedGeneratedTableStats unsupported_stats = {0};
    result = result && xed_import_emit_generated_tables_packed(schema_a, coverage_a, arena, unsupported_forms, &unsupported_stats) &&
             unsupported_stats.coverage_counts[XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN] == 1 &&
             unsupported_stats.reason_counts[XED_GENERATED_REASON_UNKNOWN_PATTERN_TOKEN] == 1;

    arena_reset_to_start(output);
    String8 aarch64_json =
        S8("{\"!instanceof\":{\"AArch64Inst\":[\"Pseudo\",\"Real\"]},"
           "\"Pseudo\":{\"isPseudo\":1,\"Inst\":[0],\"AsmString\":\"pseudo\",\"OutOperandList\":{\"printable\":\"(outs)\"},"
           "\"InOperandList\":{\"printable\":\"(ins)\"},\"Predicates\":[]},"
           "\"Real\":{\"isPseudo\":0,\"Inst\":[1,0],\"AsmString\":\"real\\t$Rd\","
           "\"OutOperandList\":{\"printable\":\"(outs GPR:$Rd)\"},\"InOperandList\":{\"printable\":\"(ins)\"},"
           "\"Predicates\":[{\"def\":\"HasFeature\"}]}}");
    u64 aarch64_count = 0;
    String8 expected_aarch64 =
        S8("{\"name\":\"Real\",\"inst\":[1,0],\"asm\":\"real\\t$Rd\",\"out\":\"(outs GPR:$Rd)\",\"in\":\"(ins)\","
           "\"predicates\":[\"HasFeature\"]}\n");
    result = result && aarch64_import_emit(arena, output, aarch64_json, &aarch64_count) && aarch64_count == 1 &&
             string_equal(assembly_import_arena_contents(output), expected_aarch64);

    arena_reset_to_start(output);
    String8 unordered_aarch64_json =
        S8("{\"!instanceof\":{\"AArch64Inst\":[\"Second\",\"First\"]},"
           "\"Second\":{\"isPseudo\":0,\"Inst\":[1,0],\"AsmString\":\"second\",\"OutOperandList\":{\"printable\":\"(outs)\"},"
           "\"InOperandList\":{\"printable\":\"(ins)\"},\"Predicates\":[]},"
           "\"First\":{\"isPseudo\":0,\"Inst\":[0,1],\"AsmString\":\"first\",\"OutOperandList\":{\"printable\":\"(outs)\"},"
           "\"InOperandList\":{\"printable\":\"(ins)\"},\"Predicates\":[]}}");
    String8 expected_ordered_aarch64 =
        S8("{\"name\":\"First\",\"inst\":[0,1],\"asm\":\"first\",\"out\":\"(outs)\",\"in\":\"(ins)\",\"predicates\":[]}\n"
           "{\"name\":\"Second\",\"inst\":[1,0],\"asm\":\"second\",\"out\":\"(outs)\",\"in\":\"(ins)\",\"predicates\":[]}\n");
    String8 unordered_input_checksum = assembly_import_checksum(arena, unordered_aarch64_json);
    result = result && aarch64_import_emit(arena, output, unordered_aarch64_json, &aarch64_count) && aarch64_count == 2 &&
             string_equal(assembly_import_arena_contents(output), expected_ordered_aarch64);
    String8 unordered_normalized_checksum = assembly_import_checksum(arena, assembly_import_arena_contents(output));
    result = result && !string_equal(unordered_input_checksum, unordered_normalized_checksum);

    arena_reset_to_start(output);
    String8 reduced_noncanonical_jsonl =
        S8("  {\"name\":\"Second\",\"inst\":[1,0],\"asm\":\"second\",\"out\":\"(outs)\",\"in\":\"(ins)\",\"predicates\":[]}\n"
           "{\"name\":\"First\",\"inst\":[0,1],\"asm\":\"first\",\"out\":\"(outs)\",\"in\":\"(ins)\",\"predicates\":[]}\n");
    String8 reduced_input_checksum = assembly_import_checksum(arena, reduced_noncanonical_jsonl);
    result = result && aarch64_import_emit(arena, output, reduced_noncanonical_jsonl, &aarch64_count) && aarch64_count == 2 &&
             string_equal(assembly_import_arena_contents(output), expected_ordered_aarch64);
    String8 reduced_normalized_checksum = assembly_import_checksum(arena, assembly_import_arena_contents(output));
    result = result && !string_equal(reduced_input_checksum, reduced_normalized_checksum);

    arena_reset_to_start(output);
    aarch64_import_self_test_append_record(output, S8("Normal"), 0, S8("nop"), S8("(outs)"), S8("(ins)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("Alias"), 0, S8("nop"), S8("(outs)"), S8("(ins)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("Normal2"), 7, S8("nop"), S8("(outs)"), S8("(ins)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("PredicateGuardA"), 0, S8("nop"), S8("(outs)"), S8("(ins)"), S8("[\"FeatureGuardA\"]"));
    aarch64_import_self_test_append_record(output, S8("PredicateGuardB"), 0, S8("nop"), S8("(outs)"), S8("(ins)"), S8("[\"FeatureGuardB\"]"));
    aarch64_import_self_test_append_record(output, S8("Branch"), 1, S8("b\t$addr"), S8("(outs)"),
                                            S8("(ins am_b_target:$addr)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("Memory"), 0, S8("ldr\t$Rt, [$Rn, $offset]"), S8("(outs GPR32:$Rt)"),
                                            S8("(ins GPR64sp:$Rn, simm7s4:$offset)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("SimdLane"), 2, S8("simd\t$Vd$idx"), S8("(outs VecListFour16b:$Vd)"),
                                            S8("(ins VecListFour16b:$Vd, VectorIndexS:$idx)"), S8("[\"HasNEON\"]"));
    aarch64_import_self_test_append_record(output, S8("SvePredicate"), 3, S8("abs\t$Zd, $Pg/m, $Zn"), S8("(outs ZPR8:$Zd)"),
                                            S8("(ins ZPR8:$_Zd, PPR3bAny:$Pg, ZPR8:$Zn)"), S8("[\"HasSVE_or_SME\"]"));
    aarch64_import_self_test_append_record(output, S8("SME_SYSTEM"), 7, S8("SMSTART"), S8("(outs)"), S8("(ins)"),
                                            S8("[\"HasSME\"]"));
    aarch64_import_self_test_append_record(output, S8("ReservedVariable"), 4, S8("reserved"), S8("(outs)"), S8("(ins)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("UnknownField"), 5, S8("unknown"), S8("(outs)"), S8("(ins)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("Conflict"), 6, S8("conflict\t$conflict"), S8("(outs X:$conflict)"),
                                            S8("(ins)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("Malformed"), 0, S8("bad { $missing"), S8("(outs BAD)"), S8("(ins)"), S8("[]"));
    aarch64_import_self_test_append_record(output, S8("MalformedTemplate"), 0, S8("bad {"), S8("(outs)"), S8("(ins)"), S8("[]"));
    String8 synthetic_jsonl = assembly_import_arena_contents(output);
    Aarch64ImportRecordList synthetic_records = {0};
    result = result && aarch64_import_parse_normalized(arena, synthetic_jsonl, &synthetic_records) && synthetic_records.count == 15;
    aarch64_import_normalize_records(&synthetic_records);
    Aarch64ImportRecord* normal = aarch64_import_self_test_find_record(synthetic_records, S8("Normal"));
    Aarch64ImportRecord* alias = aarch64_import_self_test_find_record(synthetic_records, S8("Alias"));
    Aarch64ImportRecord* normal2 = aarch64_import_self_test_find_record(synthetic_records, S8("Normal2"));
    Aarch64ImportRecord* predicate_guard_a = aarch64_import_self_test_find_record(synthetic_records, S8("PredicateGuardA"));
    Aarch64ImportRecord* predicate_guard_b = aarch64_import_self_test_find_record(synthetic_records, S8("PredicateGuardB"));
    Aarch64ImportRecord* branch = aarch64_import_self_test_find_record(synthetic_records, S8("Branch"));
    Aarch64ImportRecord* memory = aarch64_import_self_test_find_record(synthetic_records, S8("Memory"));
    Aarch64ImportRecord* simd_lane = aarch64_import_self_test_find_record(synthetic_records, S8("SimdLane"));
    Aarch64ImportRecord* sve_predicate = aarch64_import_self_test_find_record(synthetic_records, S8("SvePredicate"));
    Aarch64ImportRecord* sme_system = aarch64_import_self_test_find_record(synthetic_records, S8("SME_SYSTEM"));
    Aarch64ImportRecord* reserved_variable = aarch64_import_self_test_find_record(synthetic_records, S8("ReservedVariable"));
    Aarch64ImportRecord* unknown_field = aarch64_import_self_test_find_record(synthetic_records, S8("UnknownField"));
    Aarch64ImportRecord* conflict = aarch64_import_self_test_find_record(synthetic_records, S8("Conflict"));
    Aarch64ImportRecord* malformed_record = aarch64_import_self_test_find_record(synthetic_records, S8("Malformed"));
    Aarch64ImportRecord* malformed_template = aarch64_import_self_test_find_record(synthetic_records, S8("MalformedTemplate"));
    Aarch64ImportVariable* branch_field = branch ? aarch64_import_variable_find(branch, S8("addr")) : 0;
    result = result && normal && alias && normal2 && predicate_guard_a && predicate_guard_b && branch && memory && simd_lane && sve_predicate && sme_system && reserved_variable && unknown_field && conflict && malformed_record &&
             malformed_template &&
             ((normal->coverage_class == AARCH64_IMPORT_COVERAGE_DIRECT && alias->coverage_class == AARCH64_IMPORT_COVERAGE_ALIAS) ||
              (normal->coverage_class == AARCH64_IMPORT_COVERAGE_ALIAS && alias->coverage_class == AARCH64_IMPORT_COVERAGE_DIRECT)) &&
             alias->normalized_form_id == normal->normalized_form_id && branch_field &&
             predicate_guard_a->coverage_class == AARCH64_IMPORT_COVERAGE_DIRECT && predicate_guard_b->coverage_class == AARCH64_IMPORT_COVERAGE_DIRECT &&
             predicate_guard_a->signature_hash != predicate_guard_b->signature_hash && predicate_guard_a->normalized_form_id != predicate_guard_b->normalized_form_id &&
             predicate_guard_a->first_predicate && predicate_guard_b->first_predicate &&
             string_equal(predicate_guard_a->first_predicate->name, S8("FeatureGuardA")) &&
             string_equal(predicate_guard_b->first_predicate->name, S8("FeatureGuardB")) &&
             branch_field->relocation == AARCH64_IMPORT_RELOC_BRANCH26 && branch_field->shift == 2 &&
             memory->address_kind == AARCH64_IMPORT_ADDRESS_BASE_OFFSET && memory->test_class == AARCH64_IMPORT_TEST_MEMORY &&
             (simd_lane->first_operand && (simd_lane->first_operand->flags & AARCH64_IMPORT_OPERAND_FLAG_LIST)) &&
             sve_predicate->test_class == AARCH64_IMPORT_TEST_SVE_PREDICATE &&
             sme_system->coverage_class == AARCH64_IMPORT_COVERAGE_PRIVILEGED_SYSTEM &&
             sme_system->encoder_family == AARCH64_IMPORT_ENCODER_SYSTEM && sme_system->test_class == AARCH64_IMPORT_TEST_SME_SYSTEM &&
             reserved_variable->coverage_class == AARCH64_IMPORT_COVERAGE_RESERVED_UNENCODABLE &&
             reserved_variable->reason_id == AARCH64_IMPORT_REASON_UNMAPPED_VARIABLE &&
             unknown_field->coverage_class == AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN &&
             unknown_field->reason_id == AARCH64_IMPORT_REASON_UNKNOWN_FIELD &&
             conflict->reason_id == AARCH64_IMPORT_REASON_CONFLICTING_BIT_ASSIGNMENT &&
             malformed_record->reason_id == AARCH64_IMPORT_REASON_MALFORMED_DAG &&
             malformed_template->reason_id == AARCH64_IMPORT_REASON_MALFORMED_TEMPLATE;

    arena_reset_to_start(schema_a);
    arena_reset_to_start(schema_b);
    arena_reset_to_start(coverage_a);
    arena_reset_to_start(coverage_b);
    Aarch64GeneratedTableStats aarch64_stats_a = {0};
    Aarch64GeneratedTableStats aarch64_stats_b = {0};
    bool generated_a = aarch64_generated_emit_tables(schema_a, coverage_a, arena, synthetic_records, &aarch64_stats_a);
    bool generated_b = aarch64_generated_emit_tables(schema_b, coverage_b, arena, synthetic_records, &aarch64_stats_b);
    result = result && generated_a && generated_b && string_equal(assembly_import_arena_contents(schema_a), assembly_import_arena_contents(schema_b)) &&
             string_equal(assembly_import_arena_contents(coverage_a), assembly_import_arena_contents(coverage_b)) &&
             aarch64_stats_a.coverage_counts[AARCH64_IMPORT_COVERAGE_UNCLASSIFIED] == 0 &&
             aarch64_stats_a.coverage_counts[AARCH64_IMPORT_COVERAGE_ALIAS] == 1 &&
             aarch64_stats_a.reason_counts[AARCH64_IMPORT_REASON_UNKNOWN_FIELD] == 1 &&
             aarch64_stats_a.mnemonic_candidate_count == synthetic_records.count && aarch64_stats_a.mnemonic_range_count < aarch64_stats_a.mnemonic_candidate_count &&
             aarch64_stats_a.signature_candidate_count >= 2 && aarch64_stats_a.signature_range_count < aarch64_stats_a.signature_candidate_count &&
             string_contains(assembly_import_arena_contents(schema_a), S8("FeatureGuardA")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("FeatureGuardB")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("buster_aarch64_generated_mnemonic_ranges")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("buster_aarch64_generated_signature_ranges")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("buster_aarch64_generated_blob_range_valid")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("BUSTER_AARCH64_GENERATED_COVERAGE_CLASS_COUNT")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("BUSTER_AARCH64_GENERATED_COVERAGE_ROW_COUNT")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("buster_aarch64_generated_string_pool_chunk_0[]")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("BUSTER_AARCH64_GENERATED_C_ARRAY_CHUNK_SIZE 4092u")) &&
             !string_contains(assembly_import_arena_contents(schema_a), S8("sizeof(blob)")) &&
             !string_contains(assembly_import_arena_contents(schema_a), S8("(blob)[")) &&
             string_contains(assembly_import_arena_contents(schema_a), S8("UINT32_MAX"));

    arena_destroy(coverage_b, 1);
    arena_destroy(coverage_a, 1);
    arena_destroy(schema_b, 1);
    arena_destroy(schema_a, 1);
    arena_destroy(output, 1);
    arena_destroy(arena, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult assembly_import_action_aarch64_only(Arena* arena, AssemblyImportOptions options)
{
    FileMapRead aarch64_map = file_map_read(arena, options.aarch64_json, (FileReadOptions){0});
    if (!aarch64_map.bytes.pointer)
    {
        string_print(S8("error: failed to read AArch64 metadata {S8}\n"), options.aarch64_json);
        return PROCESS_RESULT_FAILED;
    }
    Arena* aarch64_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
    Arena* generated_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
    Arena* coverage_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    Arena* inventory_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(8)});
    Arena* scratch = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
    if (!aarch64_output || !generated_output || !coverage_output || !inventory_output || !scratch)
    {
        if (scratch)
        {
            arena_destroy(scratch, 1);
        }
        if (coverage_output)
        {
            arena_destroy(coverage_output, 1);
        }
        if (inventory_output)
        {
            arena_destroy(inventory_output, 1);
        }
        if (generated_output)
        {
            arena_destroy(generated_output, 1);
        }
        if (aarch64_output)
        {
            arena_destroy(aarch64_output, 1);
        }
        file_map_unmap(aarch64_map);
        string_print(S8("error: failed to reserve AArch64 importer arenas\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 input = BYTE_SLICE_TO_STRING(8, aarch64_map.bytes);
    u64 count = 0;
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (!aarch64_import_emit(arena, aarch64_output, input, &count))
    {
        string_print(S8("error: malformed AArch64 metadata {S8}\n"), options.aarch64_json);
        result = PROCESS_RESULT_FAILED;
    }
    Aarch64ImportRecordList records = {0};
    Aarch64GeneratedTableStats stats = {0};
    bool reduced_provenance_valid = false;
    if (result == PROCESS_RESULT_SUCCESS)
    {
        String8 content = assembly_import_arena_contents(aarch64_output);
        if (!aarch64_import_parse_normalized(arena, content, &records))
        {
            string_print(S8("error: malformed normalized AArch64 metadata {S8}\n"), options.aarch64_json);
            result = PROCESS_RESULT_FAILED;
        }
        else
        {
            aarch64_import_normalize_records(&records);
            if (!aarch64_generated_emit_tables(generated_output, coverage_output, scratch, records, &stats))
            {
                string_print(S8("error: failed to generate AArch64 metadata tables or coverage contains UNCLASSIFIED rows\n"));
                result = PROCESS_RESULT_FAILED;
            }
            else if (!aarch64_import_acceptance_ready(stats))
            {
                string_print(options.audit ? S8("audit: AArch64 metadata acceptance blocked: RESERVED/UNENCODABLE={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}; inventory output was generated\n") :
                             S8("error: AArch64 metadata acceptance blocked: RESERVED/UNENCODABLE={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}; use --audit only for inventory output\n"),
                             stats.coverage_counts[AARCH64_IMPORT_COVERAGE_RESERVED_UNENCODABLE],
                             stats.coverage_counts[AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN],
                             stats.coverage_counts[AARCH64_IMPORT_COVERAGE_UNCLASSIFIED]);
                if (!options.audit)
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            if (result == PROCESS_RESULT_SUCCESS)
            {
                u64 inventory_count = 0;
                if (!aarch64_generated_emit_missing_inventory(inventory_output, records, &inventory_count))
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
    }
    }
    if (result == PROCESS_RESULT_SUCCESS)
    {
        reduced_provenance_valid = aarch64_import_reduced_provenance(arena, input, count);
        if (!reduced_provenance_valid)
        {
            string_print(options.audit ? S8("audit: AArch64 reduced JSONL provenance is not the checked-in reduced corpus; reduced-input provenance is recorded without a raw snapshot claim\n") :
                         S8("error: AArch64 reduced JSONL provenance is not the checked-in reduced corpus; use --audit only for inventory output\n"));
            if (!options.audit)
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
    }
    if (result == PROCESS_RESULT_SUCCESS)
    {
        ByteSlice xed_bytes[3] = {0};
        String8 xed_names[] = {S8("x86_64-xed.jsonl"), S8("x86_64-assembly.generated.h"), S8("x86_64-coverage.generated.inc")};
        String8 xed_content[3] = {0};
        for (u32 index = 0; index < 3; index += 1)
        {
            String8 path = path_join(arena, S8("src/buster/lib/compiler/assembly/generated"), xed_names[index]);
            xed_bytes[index] = file_read(arena, path, (FileReadOptions){0});
            xed_content[index] = BYTE_SLICE_TO_STRING(8, xed_bytes[index]);
            if (!xed_content[index].pointer)
            {
                string_print(S8("error: failed to read checked-in XED artifact {S8}\n"), path);
                result = PROCESS_RESULT_FAILED;
                break;
            }
        }
        if (result == PROCESS_RESULT_SUCCESS)
        {
            if (!assembly_import_validate_checked_in_x86(arena, xed_content))
            {
                string_print(S8("error: checked-in XED artifact provenance/checksum/count validation failed\n"));
                result = PROCESS_RESULT_FAILED;
            }
            else if (options.audit && !assembly_import_validate_checked_in_x86_mutation_self_test(arena, xed_content))
            {
                string_print(S8("error: checked-in XED artifact provenance mutation self-test failed\n"));
                result = PROCESS_RESULT_FAILED;
            }
            else if (options.audit)
            {
                string_print(S8("audit: checked-in XED provenance accepted; one-byte copied mutation rejected\n"));
            }
        }
        if (result == PROCESS_RESULT_SUCCESS)
        {
            make_directory_recursive(arena, options.output_directory);
            String8 aarch64_content = assembly_import_arena_contents(aarch64_output);
            String8 generated_content = assembly_import_arena_contents(generated_output);
            String8 coverage_content = assembly_import_arena_contents(coverage_output);
            String8 inventory_content = assembly_import_arena_contents(inventory_output);
            String8 reduced_input_checksum = assembly_import_checksum(arena, input);
            String8 normalized_input_checksum = assembly_import_checksum(arena, aarch64_content);
            String8 output_checksum = assembly_import_checksum(arena, aarch64_content);
            String8 generated_checksum = assembly_import_checksum(arena, generated_content);
            String8 coverage_checksum = assembly_import_checksum(arena, coverage_content);
            String8 xed_input_checksum = assembly_import_checksum(arena, xed_content[0]);
            String8 xed_header_checksum = assembly_import_checksum(arena, xed_content[1]);
            String8 xed_coverage_checksum = assembly_import_checksum(arena, xed_content[2]);
            u64 xed_form_count = assembly_import_line_count(xed_content[0]);
            u64 xed_iclass_count = 0;
            u64 xed_iform_count = 0;
            u64 xed_missing_iform_count = 0;
            bool xed_counts_valid = assembly_import_jsonl_unique_field(arena, xed_content[0], S8("iclass"), &xed_iclass_count, 0) &&
                                     assembly_import_jsonl_unique_field(arena, xed_content[0], S8("iform"), &xed_iform_count,
                                                                        &xed_missing_iform_count);
            String8 generation_mode = options.audit ? S8("audit") : S8("acceptance");
            String8 acceptance_status = aarch64_import_acceptance_ready(stats) && reduced_provenance_valid ? S8("ready") : S8("blocked");
            String8 reduced_source_identity = reduced_provenance_valid ? S8("checked-in-reduced-jsonl") : S8("unverified-reduced-jsonl");
            String8 raw_snapshot_provenance = S8("false");
            if (!xed_counts_valid)
            {
                result = PROCESS_RESULT_FAILED;
            }
            String8 inventory_checksum = assembly_import_checksum(arena, inventory_content);
            XedGeneratedTableStats xed_stats = {0};
            xed_stats.header_bytes = xed_content[1].length;
            xed_stats.coverage_bytes = xed_content[2].length;
            AssemblyImportFullManifestData manifest_data = {
                .generation_mode = generation_mode,
                .acceptance_status = acceptance_status,
                .xed_config_file_count = 0,
                .xed_source_file_count = 0,
                .xed_config_checksum = S8("not-applicable-aarch64-only"),
                .xed_input_kind = S8("checked-in-xed-jsonl"),
                .xed_source_identity = S8("checked-in-x86-artifacts"),
                .xed_raw_snapshot_provenance = S8("false"),
                .xed_input_checksum = xed_input_checksum,
                .xed_output_checksum = xed_input_checksum,
                .xed_form_count = xed_form_count,
                .xed_iclass_count = xed_iclass_count,
                .xed_iform_count = xed_iform_count,
                .xed_missing_iform_count = xed_missing_iform_count,
                .generated_output_checksum = xed_header_checksum,
                .generated_coverage_checksum = xed_coverage_checksum,
                .xed_stats = xed_stats,
                .aarch64_generated_output_checksum = generated_checksum,
                .aarch64_generated_coverage_checksum = coverage_checksum,
                .aarch64_alias_source_identity = S8("not-present-in-reduced-jsonl"),
                .aarch64_inventory_checksum = inventory_checksum,
                .aarch64_inventory_bytes = inventory_content.length,
                .llvm_input_kind = S8("reduced_jsonl"),
                .llvm_source_identity = reduced_source_identity,
                .llvm_raw_snapshot_provenance = raw_snapshot_provenance,
                .llvm_input_checksum = reduced_input_checksum,
                .llvm_normalized_input_checksum = normalized_input_checksum,
                .llvm_output_checksum = output_checksum,
                .llvm_instruction_count = count,
                .aarch64_stats = stats,
            };
            String8 manifest = assembly_import_format_full_manifest(arena, manifest_data);
            if (!assembly_import_write(arena, options.output_directory, xed_names[0], xed_content[0]) ||
                !assembly_import_write(arena, options.output_directory, xed_names[1], xed_content[1]) ||
                !assembly_import_write(arena, options.output_directory, xed_names[2], xed_content[2]) ||
                !assembly_import_write(arena, options.output_directory, S8("aarch64-llvm.jsonl"), aarch64_content) ||
                !assembly_import_write(arena, options.output_directory, S8("aarch64-assembly.generated.h"), generated_content) ||
                !assembly_import_write(arena, options.output_directory, S8("aarch64-coverage.generated.inc"), coverage_content) ||
                !assembly_import_write(arena, options.output_directory, S8("aarch64-missing-fields.generated.jsonl"), inventory_content) ||
                !assembly_import_write(arena, options.output_directory, S8("manifest.json"), manifest))
            {
                result = PROCESS_RESULT_FAILED;
            }
            if (result == PROCESS_RESULT_SUCCESS)
            {
                string_print(S8("AArch64 tables: forms={u64} fields={u64} segments={u64} operands={u64} predicates={u64} header={u64} bytes coverage={u64} bytes strings={u64} bytes.\n"),
                             stats.canonical_form_count, stats.field_count, stats.segment_count, stats.operand_count, stats.predicate_count,
                             stats.header_bytes, stats.coverage_bytes, stats.string_pool_bytes);
                string_print(S8("AArch64 coverage: DIRECT={u64} NORMALIZED={u64} ALIAS={u64} PRIVILEGED/SYSTEM={u64} RESERVED/UNENCODABLE={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}.\n"),
                             stats.coverage_counts[0], stats.coverage_counts[1], stats.coverage_counts[2], stats.coverage_counts[3],
                             stats.coverage_counts[4], stats.coverage_counts[5], stats.coverage_counts[6]);
            }
        }
    }
    file_map_unmap(aarch64_map);
    arena_destroy(scratch, 1);
    arena_destroy(inventory_output, 1);
    arena_destroy(coverage_output, 1);
    arena_destroy(generated_output, 1);
    arena_destroy(aarch64_output, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult assembly_import_action(Arena* arena, void* data)
{
    AssemblyImportOptions options = *(AssemblyImportOptions*)data;
    if (!assembly_import_self_test())
    {
        string_print(S8("error: assembly metadata importer self-test failed\n"));
        return PROCESS_RESULT_FAILED;
    }
    if (string_equal(options.xed_datafiles, S8("-")))
    {
        return assembly_import_action_aarch64_only(arena, options);
    }
    AssemblyImportPathList config_paths = {0};
    if (!assembly_import_collect_xed_config_paths(arena, options.xed_datafiles, &config_paths) || !config_paths.count)
    {
        string_print(S8("error: failed to enumerate XED files*.cfg configuration under {S8}\n"), options.xed_datafiles);
        return PROCESS_RESULT_FAILED;
    }

    AssemblyImportPathNode** sorted_config_paths = arena_allocate(arena, AssemblyImportPathNode*, config_paths.count);
    u64 path_index = 0;
    for (AssemblyImportPathNode* node = config_paths.first; node; node = node->next)
    {
        sorted_config_paths[path_index++] = node;
    }
    qsort(sorted_config_paths, config_paths.count, sizeof(sorted_config_paths[0]), assembly_import_path_compare);

    AssemblyImportPathList source_paths = {0};

    Arena* xed_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
    Arena* generated_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
    Arena* coverage_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
    Arena* aarch64_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
    Arena* aarch64_generated_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
    Arena* aarch64_generated_coverage_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    Arena* aarch64_inventory_output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(8)});
    Arena* aarch64_generated_scratch = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
    Arena* xed_config_checksum_input = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
    Arena* xed_checksum_input = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    if (!xed_output || !generated_output || !coverage_output || !aarch64_output || !aarch64_generated_output ||
        !aarch64_generated_coverage_output || !aarch64_inventory_output || !aarch64_generated_scratch || !xed_config_checksum_input ||
        !xed_checksum_input)
    {
        string_print(S8("error: failed to reserve assembly importer arenas\n"));
        if (xed_output)
        {
            arena_destroy(xed_output, 1);
        }
        if (generated_output)
        {
            arena_destroy(generated_output, 1);
        }
        if (coverage_output)
        {
            arena_destroy(coverage_output, 1);
        }
        if (aarch64_output)
        {
            arena_destroy(aarch64_output, 1);
        }
        if (aarch64_generated_output)
        {
            arena_destroy(aarch64_generated_output, 1);
        }
        if (aarch64_generated_coverage_output)
        {
            arena_destroy(aarch64_generated_coverage_output, 1);
        }
        if (aarch64_inventory_output)
        {
            arena_destroy(aarch64_inventory_output, 1);
        }
        if (aarch64_generated_scratch)
        {
            arena_destroy(aarch64_generated_scratch, 1);
        }
        if (xed_config_checksum_input)
        {
            arena_destroy(xed_config_checksum_input, 1);
        }
        if (xed_checksum_input)
        {
            arena_destroy(xed_checksum_input, 1);
        }
        return PROCESS_RESULT_FAILED;
    }

    ProcessResult result = PROCESS_RESULT_SUCCESS;
    for (u64 i = 0; i < config_paths.count; i += 1)
    {
        AssemblyImportPathNode* config = sorted_config_paths[i];
        ByteSlice bytes = file_read(arena, config->full, (FileReadOptions){0});
        if (!bytes.pointer && bytes.length == 0)
        {
            string_print(S8("error: failed to read XED configuration {S8}\n"), config->full);
            result = PROCESS_RESULT_FAILED;
            break;
        }
        String8 text = BYTE_SLICE_TO_STRING(8, bytes);
        arena_append_string8(xed_config_checksum_input, config->relative);
        arena_append_char8(xed_config_checksum_input, 0);
        arena_append_string8(xed_config_checksum_input, text);
        arena_append_char8(xed_config_checksum_input, 0);
        if (!assembly_import_config_parse(arena, options.xed_datafiles, config, text, &source_paths, true))
        {
            result = PROCESS_RESULT_FAILED;
            break;
        }
    }
    if (result == PROCESS_RESULT_SUCCESS && !source_paths.count)
    {
        string_print(S8("error: XED configuration selected no enc-instructions sources\n"));
        result = PROCESS_RESULT_FAILED;
    }

    AssemblyImportPathNode** sorted_source_paths = 0;
    if (result == PROCESS_RESULT_SUCCESS)
    {
        sorted_source_paths = arena_allocate(arena, AssemblyImportPathNode*, source_paths.count);
        path_index = 0;
        for (AssemblyImportPathNode* node = source_paths.first; node; node = node->next)
        {
            sorted_source_paths[path_index++] = node;
        }
        qsort(sorted_source_paths, source_paths.count, sizeof(sorted_source_paths[0]), assembly_import_path_compare);
    }

    XedImportRecordList xed_records = {0};
    for (u64 i = 0; result == PROCESS_RESULT_SUCCESS && i < source_paths.count; i += 1)
    {
        AssemblyImportPathNode* path = sorted_source_paths[i];
        ByteSlice bytes = file_read(arena, path->full, (FileReadOptions){0});
        if (!bytes.pointer && bytes.length == 0)
        {
            string_print(S8("error: failed to read XED data file {S8}\n"), path->full);
            result = PROCESS_RESULT_FAILED;
            break;
        }
        String8 text = BYTE_SLICE_TO_STRING(8, bytes);
        arena_append_string8(xed_checksum_input, path->relative);
        arena_append_char8(xed_checksum_input, 0);
        arena_append_string8(xed_checksum_input, text);
        arena_append_char8(xed_checksum_input, 0);
        if (!xed_import_parse_file(arena, &xed_records, path->relative, text))
        {
            string_print(S8("error: malformed XED enc-instructions source {S8}\n"), path->relative);
            result = PROCESS_RESULT_FAILED;
        }
    }
    if (result == PROCESS_RESULT_SUCCESS && !xed_records.count)
    {
        string_print(S8("error: no XED encoding forms were imported\n"));
        result = PROCESS_RESULT_FAILED;
    }
    if (result == PROCESS_RESULT_SUCCESS)
    {
        xed_import_emit(xed_output, xed_records);
    }

    XedGeneratedFormList generated_forms = {0};
    XedGeneratedTableStats generated_stats = {0};
    if (result == PROCESS_RESULT_SUCCESS)
    {
        bool valid = true;
        u64 failed_index = 0;
        String8 bad_token = {0};
        bool bad_operand = false;
        generated_forms = xed_import_normalize_records(arena, xed_records, &valid, &failed_index, &bad_token, &bad_operand);
        if (!valid)
        {
            XedImportRecord* record = xed_records.first;
            for (u64 index = 0; record && index < failed_index; index += 1)
            {
                record = record->next;
            }
            string_print(S8("error: unsupported XED {S8} token in {S8} iclass={S8}: {S8}\n"),
                         bad_operand ? S8("operand") : S8("pattern"), record ? record->source : S8("<unknown>"),
                         record ? record->iclass : S8("<unknown>"), bad_token);
            result = PROCESS_RESULT_FAILED;
        }
        else if (!xed_import_emit_generated_tables_packed(generated_output, coverage_output, arena, generated_forms, &generated_stats))
        {
            string_print(S8("error: generated XED coverage contains UNCLASSIFIED rows\n"));
            result = PROCESS_RESULT_FAILED;
        }
    }

    FileMapRead aarch64_map = {0};
    u64 aarch64_count = 0;
    String8 aarch64_json = {0};
    Aarch64ImportRecordList aarch64_records = {0};
    Aarch64GeneratedTableStats aarch64_generated_stats = {0};
    bool aarch64_raw_provenance_valid = false;
    bool aarch64_alias_source = false;
    if (result == PROCESS_RESULT_SUCCESS)
    {
        aarch64_map = file_map_read(arena, options.aarch64_json, (FileReadOptions){0});
        if (!aarch64_map.bytes.pointer)
        {
            string_print(S8("error: failed to read llvm-tblgen JSON {S8}\n"), options.aarch64_json);
            result = PROCESS_RESULT_FAILED;
        }
        else
        {
            aarch64_json = BYTE_SLICE_TO_STRING(8, aarch64_map.bytes);
            aarch64_alias_source = aarch64_import_alias_source_present(aarch64_json);
            if (aarch64_alias_source)
            {
                string_print(aarch64_import_alias_source_diagnostic(options.audit));
                if (aarch64_import_alias_source_is_fatal(options.audit))
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            if (!aarch64_import_emit(arena, aarch64_output, aarch64_json, &aarch64_count))
            {
                string_print(S8("error: malformed or incomplete AArch64 llvm-tblgen JSON in {S8}\n"), options.aarch64_json);
                result = PROCESS_RESULT_FAILED;
            }
            else
            {
                aarch64_raw_provenance_valid = aarch64_import_full_provenance(arena, aarch64_json, aarch64_count);
                if (!aarch64_raw_provenance_valid)
                {
                    string_print(options.audit ? S8("audit: AArch64 raw LLVM provenance is not the pinned llvmorg-22.1.8 snapshot or emitted count; raw snapshot provenance is not claimed\n") :
                                 S8("error: AArch64 raw LLVM provenance is not the pinned llvmorg-22.1.8 snapshot or emitted count; use --audit only for inventory output\n"));
                    if (!options.audit)
                    {
                        result = PROCESS_RESULT_FAILED;
                    }
                }
                String8 aarch64_content = assembly_import_arena_contents(aarch64_output);
                if (!aarch64_import_parse_normalized(arena, aarch64_content, &aarch64_records))
                {
                    string_print(S8("error: malformed normalized AArch64 metadata in {S8}\n"), options.aarch64_json);
                    result = PROCESS_RESULT_FAILED;
                }
                else
                {
                    aarch64_import_normalize_records(&aarch64_records);
                    if (!aarch64_generated_emit_tables(aarch64_generated_output, aarch64_generated_coverage_output,
                                                       aarch64_generated_scratch, aarch64_records, &aarch64_generated_stats))
                    {
                        string_print(S8("error: failed to generate AArch64 metadata tables or coverage contains UNCLASSIFIED rows\n"));
                        result = PROCESS_RESULT_FAILED;
                    }
                    else if (!aarch64_import_acceptance_ready(aarch64_generated_stats))
                    {
                        string_print(options.audit ? S8("audit: AArch64 metadata acceptance blocked: RESERVED/UNENCODABLE={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}; inventory output was generated\n") :
                                     S8("error: AArch64 metadata acceptance blocked: RESERVED/UNENCODABLE={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}; use --audit only for inventory output\n"),
                                     aarch64_generated_stats.coverage_counts[AARCH64_IMPORT_COVERAGE_RESERVED_UNENCODABLE],
                                     aarch64_generated_stats.coverage_counts[AARCH64_IMPORT_COVERAGE_UNSUPPORTED_TOKEN],
                                     aarch64_generated_stats.coverage_counts[AARCH64_IMPORT_COVERAGE_UNCLASSIFIED]);
                        if (!options.audit)
                        {
                            result = PROCESS_RESULT_FAILED;
                        }
                    }
                    if (result == PROCESS_RESULT_SUCCESS)
                    {
                        u64 inventory_count = 0;
                        if (!aarch64_generated_emit_missing_inventory(aarch64_inventory_output, aarch64_records, &inventory_count))
                        {
                            result = PROCESS_RESULT_FAILED;
                        }
                    }
                }
            }
        }
    }

    if (result == PROCESS_RESULT_SUCCESS)
    {
        make_directory_recursive(arena, options.output_directory);
        String8 xed_content = assembly_import_arena_contents(xed_output);
        String8 generated_content = assembly_import_arena_contents(generated_output);
        String8 coverage_content = assembly_import_arena_contents(coverage_output);
        String8 aarch64_content = assembly_import_arena_contents(aarch64_output);
        String8 aarch64_generated_content = assembly_import_arena_contents(aarch64_generated_output);
        String8 aarch64_generated_coverage_content = assembly_import_arena_contents(aarch64_generated_coverage_output);
        String8 aarch64_inventory_content = assembly_import_arena_contents(aarch64_inventory_output);
        String8 xed_config_checksum = assembly_import_checksum(arena, assembly_import_arena_contents(xed_config_checksum_input));
        String8 xed_input_checksum = assembly_import_checksum(arena, assembly_import_arena_contents(xed_checksum_input));
        String8 aarch64_input_checksum = assembly_import_checksum(arena, aarch64_json);
        String8 xed_output_checksum = assembly_import_checksum(arena, xed_content);
        String8 generated_output_checksum = assembly_import_checksum(arena, generated_content);
        String8 coverage_output_checksum = assembly_import_checksum(arena, coverage_content);
        String8 aarch64_output_checksum = assembly_import_checksum(arena, aarch64_content);
        String8 aarch64_normalized_input_checksum = assembly_import_checksum(arena, aarch64_content);
        String8 aarch64_generated_output_checksum = assembly_import_checksum(arena, aarch64_generated_content);
        String8 aarch64_generated_coverage_checksum = assembly_import_checksum(arena, aarch64_generated_coverage_content);
        String8 aarch64_inventory_checksum = assembly_import_checksum(arena, aarch64_inventory_content);
        String8 generation_mode = options.audit ? S8("audit") : S8("acceptance");
        String8 acceptance_status = aarch64_import_acceptance_ready(aarch64_generated_stats) && aarch64_raw_provenance_valid && !aarch64_alias_source
                                        ? S8("ready")
                                        : S8("blocked");
        String8 aarch64_source_identity = aarch64_raw_provenance_valid ? S8("llvmorg-22.1.8-pinned-raw-json") : S8("unverified-full-json");
        String8 raw_snapshot_provenance = aarch64_raw_provenance_valid ? S8("true") : S8("false");
        String8 alias_source_identity = aarch64_alias_source ? S8("raw-alias-records-not-imported") : S8("raw-alias-records-not-present-in-selected-source");
        u64 iclass_count = xed_import_unique_record_value_count(arena, xed_records, false);
        u64 iform_count = xed_import_unique_record_value_count(arena, xed_records, true);
        u64 missing_iform_count = 0;
        for (XedImportRecord* record = xed_records.first; record; record = record->next)
        {
            missing_iform_count += !record->iform.length;
        }
        String8 xed_input_kind = S8("xed_enc_instructions");
        String8 xed_source_identity = S8("unverified-xed-datafiles");
        String8 xed_raw_snapshot_provenance = S8("false");
        String8 llvm_input_kind = S8("llvm_tblgen_json");
        AssemblyImportFullManifestData manifest_data = {
            .generation_mode = generation_mode,
            .acceptance_status = acceptance_status,
            .xed_config_file_count = config_paths.count,
            .xed_source_file_count = source_paths.count,
            .xed_config_checksum = xed_config_checksum,
            .xed_input_kind = xed_input_kind,
            .xed_source_identity = xed_source_identity,
            .xed_raw_snapshot_provenance = xed_raw_snapshot_provenance,
            .xed_input_checksum = xed_input_checksum,
            .xed_output_checksum = xed_output_checksum,
            .xed_form_count = xed_records.count,
            .xed_iclass_count = iclass_count,
            .xed_iform_count = iform_count,
            .xed_missing_iform_count = missing_iform_count,
            .generated_output_checksum = generated_output_checksum,
            .generated_coverage_checksum = coverage_output_checksum,
            .xed_stats = generated_stats,
            .aarch64_generated_output_checksum = aarch64_generated_output_checksum,
            .aarch64_generated_coverage_checksum = aarch64_generated_coverage_checksum,
            .aarch64_alias_source_identity = alias_source_identity,
            .aarch64_inventory_checksum = aarch64_inventory_checksum,
            .aarch64_inventory_bytes = aarch64_inventory_content.length,
            .llvm_input_kind = llvm_input_kind,
            .llvm_source_identity = aarch64_source_identity,
            .llvm_raw_snapshot_provenance = raw_snapshot_provenance,
            .llvm_input_checksum = aarch64_input_checksum,
            .llvm_normalized_input_checksum = aarch64_normalized_input_checksum,
            .llvm_output_checksum = aarch64_output_checksum,
            .llvm_instruction_count = aarch64_count,
            .aarch64_stats = aarch64_generated_stats,
        };
        String8 manifest = assembly_import_format_full_manifest(arena, manifest_data);

        if (!assembly_import_write(arena, options.output_directory, S8("x86_64-xed.jsonl"), xed_content) ||
            !assembly_import_write(arena, options.output_directory, S8("x86_64-assembly.generated.h"), generated_content) ||
            !assembly_import_write(arena, options.output_directory, S8("x86_64-coverage.generated.inc"), coverage_content) ||
            !assembly_import_write(arena, options.output_directory, S8("aarch64-llvm.jsonl"), aarch64_content) ||
            !assembly_import_write(arena, options.output_directory, S8("aarch64-assembly.generated.h"), aarch64_generated_content) ||
            !assembly_import_write(arena, options.output_directory, S8("aarch64-coverage.generated.inc"), aarch64_generated_coverage_content) ||
            !assembly_import_write(arena, options.output_directory, S8("aarch64-missing-fields.generated.jsonl"), aarch64_inventory_content) ||
            !assembly_import_write(arena, options.output_directory, S8("manifest.json"), manifest))
        {
            result = PROCESS_RESULT_FAILED;
        }
        else
        {
            string_print(S8("Imported {u64} XED forms and {u64} AArch64 instructions into {S8}.\n"), xed_records.count, aarch64_count,
                         options.output_directory);
            string_print(S8("XED configuration: {u64} files, {u64} enc-instruction sources, {u64} iclasses, {u64} iforms ({u64} missing).\n"),
                         config_paths.count, source_paths.count, iclass_count, iform_count, missing_iform_count);
            string_print(S8("Generated tables: header={u64} bytes coverage={u64} bytes operands={u64} strings={u64} bytes.\n"),
                         generated_stats.header_bytes, generated_stats.coverage_bytes, generated_stats.operand_count,
                         generated_stats.string_pool_bytes);
            string_print(S8("AArch64 tables: forms={u64} fields={u64} segments={u64} operands={u64} predicates={u64} header={u64} bytes coverage={u64} bytes strings={u64} bytes.\n"),
                         aarch64_generated_stats.canonical_form_count, aarch64_generated_stats.field_count, aarch64_generated_stats.segment_count,
                         aarch64_generated_stats.operand_count, aarch64_generated_stats.predicate_count, aarch64_generated_stats.header_bytes,
                         aarch64_generated_stats.coverage_bytes, aarch64_generated_stats.string_pool_bytes);
            string_print(S8("XED coverage: DIRECT={u64} NORMALIZED={u64} NOT64={u64} PRIVILEGED={u64} RESERVED={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}.\n"),
                         generated_stats.coverage_counts[0], generated_stats.coverage_counts[1], generated_stats.coverage_counts[2],
                         generated_stats.coverage_counts[3], generated_stats.coverage_counts[4], generated_stats.coverage_counts[5],
                         generated_stats.coverage_counts[6]);
            string_print(S8("AArch64 coverage: DIRECT={u64} NORMALIZED={u64} ALIAS={u64} PRIVILEGED/SYSTEM={u64} RESERVED/UNENCODABLE={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}.\n"),
                         aarch64_generated_stats.coverage_counts[0], aarch64_generated_stats.coverage_counts[1], aarch64_generated_stats.coverage_counts[2],
                         aarch64_generated_stats.coverage_counts[3], aarch64_generated_stats.coverage_counts[4], aarch64_generated_stats.coverage_counts[5],
                         aarch64_generated_stats.coverage_counts[6]);
        }
    }

    if (aarch64_map.mapped_pointer)
    {
        file_map_unmap(aarch64_map);
    }
    arena_destroy(xed_checksum_input, 1);
    arena_destroy(xed_config_checksum_input, 1);
    arena_destroy(aarch64_output, 1);
    arena_destroy(aarch64_generated_scratch, 1);
    arena_destroy(aarch64_inventory_output, 1);
    arena_destroy(aarch64_generated_coverage_output, 1);
    arena_destroy(aarch64_generated_output, 1);
    arena_destroy(coverage_output, 1);
    arena_destroy(generated_output, 1);
    arena_destroy(xed_output, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL void assembly_import_action_add(Arena* arena, AssemblyImportOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    AssemblyImportOptions* options_copy = arena_allocate(arena, AssemblyImportOptions, 1);
    *options_copy = options;
    *run = (ProcessRun){.callback = assembly_import_action, .callback_data = options_copy};
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
        [BUILD_COMMAND_TIME_TRACE_SUMMARY_SELF_TEST] = S8_INITIALIZER("time_trace_summary_self_test"),
        [BUILD_COMMAND_TEST_TIMING_SUMMARY] = S8_INITIALIZER("test_timing_summary"),
        [BUILD_COMMAND_TEST_TIMING_SUMMARY_SELF_TEST] = S8_INITIALIZER("test_timing_summary_self_test"),
        [BUILD_COMMAND_IMPORT_ASSEMBLY_METADATA] = S8_INITIALIZER("import_assembly_metadata"),
        [BUILD_COMMAND_TEST_SELF_HOST] = S8_INITIALIZER("test_self_host"),
        [BUILD_COMMAND_SELF_HOST_FROM_EXISTING] = S8_INITIALIZER("self_host_from_existing"),
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
    String8 provenance_record_path = {0};
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
    ClangAnalyzeOptions clang_analyze_options = {.compile_commands = build_directory};
    CmakeProfileSummaryOptions cmake_profile_summary_options = {.limit = 25};
    NinjaLogSummaryOptions ninja_log_summary_options = {.limit = 25};
    TimeTraceSummaryOptions time_trace_summary_options = {.limit = 25};
    TestTimingSummaryOptions test_timing_summary_options = {.limit = 25};
    AssemblyImportOptions assembly_import_options = {.output_directory = S8("src/buster/lib/compiler/assembly/generated")};
    String8List time_trace_summary_paths = {0};
    String8List test_timing_summary_paths = {0};
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
                    String8 split_definition = {0};
                    if (string_equal(passthrough_argument, S8("-D")) && argument_i + 1 < arguments.length)
                    {
                        split_definition = arguments.pointer[argument_i + 1];
                    }
                    if (build_cmake_argument_is_tcc_application_compiler(arena, passthrough_argument, split_definition))
                    {
                        string_print(S8("error: CMake compiler override selects TCC; TCC is reserved for bootstrapping build.c and is not an application compiler\n"));
                        result = PROCESS_RESULT_FAILED;
                        break;
                    }
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
            break;
        case BUILD_ARGUMENT_COUNT:
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
            else if (command == BUILD_COMMAND_CMAKE_PROFILE_SUMMARY && !cmake_profile_summary_options.profile_set &&
                     !string_starts_with_sequence(argument, S8("--")))
            {
                cmake_profile_summary_options.profile = argument;
                cmake_profile_summary_options.profile_set = 1;
                argument_i += 1;
            }
            else if (command == BUILD_COMMAND_NINJA_LOG_SUMMARY && !ninja_log_summary_options.build_directory_set &&
                     !string_starts_with_sequence(argument, S8("--")))
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
            else if (command == BUILD_COMMAND_TEST_TIMING_SUMMARY && !string_starts_with_sequence(argument, S8("--")))
            {
                string8_list_push(arena, &test_timing_summary_paths, argument);
                argument_i += 1;
            }
            else if (command == BUILD_COMMAND_IMPORT_ASSEMBLY_METADATA && !string_starts_with_sequence(argument, S8("--")))
            {
                if (!assembly_import_options.xed_datafiles_set)
                {
                    assembly_import_options.xed_datafiles = argument;
                    assembly_import_options.xed_datafiles_set = true;
                }
                else if (!assembly_import_options.aarch64_json_set)
                {
                    assembly_import_options.aarch64_json = argument;
                    assembly_import_options.aarch64_json_set = true;
                }
                else if (!assembly_import_options.output_directory_set)
                {
                    assembly_import_options.output_directory = argument;
                    assembly_import_options.output_directory_set = true;
                }
                else
                {
                    result = PROCESS_RESULT_FAILED;
                    break;
                }
                argument_i += 1;
            }
            else if (command == BUILD_COMMAND_GENERATE)
            {
                String8 split_definition = {0};
                if (string_equal(argument, S8("-D")) && argument_i + 1 < arguments.length)
                {
                    split_definition = arguments.pointer[argument_i + 1];
                }
                if (build_cmake_argument_is_tcc_application_compiler(arena, argument, split_definition))
                {
                    string_print(S8("error: CMake compiler override selects TCC; TCC is reserved for bootstrapping build.c and is not an application compiler\n"));
                    result = PROCESS_RESULT_FAILED;
                }
                else
                {
                    string8_list_push(arena, &generate_cmake_arguments, argument);
                    argument_i += 1;
                }
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
        break;
        case BUILD_ARGUMENT_AUDIT:
        {
            if (command == BUILD_COMMAND_IMPORT_ASSEMBLY_METADATA && !argument_has_value)
            {
                assembly_import_options.audit = 1;
                argument_i += 1;
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        break;
        case BUILD_ARGUMENT_BASELINE:
        {
            String8 value = {0};
            if (command == BUILD_COMMAND_TEST_TIMING_SUMMARY &&
                build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                test_timing_summary_options.baseline = value;
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        break;
        case BUILD_ARGUMENT_UPDATE_BASELINE:
        case BUILD_ARGUMENT_NO_UPDATE_BASELINE:
        {
            bool value = build_argument == BUILD_ARGUMENT_UPDATE_BASELINE;
            if (command == BUILD_COMMAND_TEST_TIMING_SUMMARY &&
                build_argument_read_optional_bool(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                test_timing_summary_options.update_baseline = value;
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        break;
        case BUILD_ARGUMENT_BUILD_DIRECTORY:
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
        break;
        case BUILD_ARGUMENT_PROVENANCE_RECORD:
        {
            String8 value = {0};
            if (command == BUILD_COMMAND_SELF_HOST_FROM_EXISTING &&
                build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                provenance_record_path = value;
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        break;
        case BUILD_ARGUMENT_CC:
        {
            String8 value = {0};
            if (command == BUILD_COMMAND_GENERATE && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                if (build_compiler_value_is_tcc(value))
                {
                    string_print(S8("error: --cc tcc is reserved for bootstrapping build.c and is not an application compiler\n"));
                    result = PROCESS_RESULT_FAILED;
                }
                else
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
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        break;
        case BUILD_ARGUMENT_CLANG:
        {
            String8 value = {0};
            if (command == BUILD_COMMAND_CLANG_ANALYZE &&
                build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                clang_analyze_options.clang = value;
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        break;
        case BUILD_ARGUMENT_CONFIG:
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
                else if (command == BUILD_COMMAND_BUILD || command == BUILD_COMMAND_TEST_SELF_HOST || command == BUILD_COMMAND_SELF_HOST_FROM_EXISTING)
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
        break;
        case BUILD_ARGUMENT_LIMIT:
        {
            String8 candidate_limit = {0};
            bool is_summary_command = command == BUILD_COMMAND_CMAKE_PROFILE_SUMMARY || command == BUILD_COMMAND_NINJA_LOG_SUMMARY ||
                                      command == BUILD_COMMAND_TIME_TRACE_SUMMARY || command == BUILD_COMMAND_TEST_TIMING_SUMMARY;
            if (is_summary_command && build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &candidate_limit))
            {
                IntegerParsingU64 parsed_limit = string8_parse_u64_decimal(candidate_limit.pointer);
                if (parsed_limit.length == candidate_limit.length && parsed_limit.value > 0)
                {
                    switch (command)
                    {
                        break;
                    case BUILD_COMMAND_CMAKE_PROFILE_SUMMARY:
                        cmake_profile_summary_options.limit = parsed_limit.value;
                        break;
                    case BUILD_COMMAND_NINJA_LOG_SUMMARY:
                        ninja_log_summary_options.limit = parsed_limit.value;
                        break;
                    case BUILD_COMMAND_TIME_TRACE_SUMMARY:
                        time_trace_summary_options.limit = parsed_limit.value;
                        break;
                    case BUILD_COMMAND_TEST_TIMING_SUMMARY:
                        test_timing_summary_options.limit = parsed_limit.value;
                        break;
                    default:
                        BUSTER_UNREACHABLE();
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
        break;
        case BUILD_ARGUMENT_TARGET:
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
        break;
        case BUILD_ARGUMENT_LINKER:
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
        break;
        case BUILD_ARGUMENT_CMAKE_PROFILE:
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
        break;
        case BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY:
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
        break;
        case BUILD_ARGUMENT_CMAKE_PROFILE_SUMMARY_LIMIT:
        {
            String8 candidate_limit = {0};
            if (command == BUILD_COMMAND_GENERATE &&
                build_argument_read_required_value(arguments, &argument_i, argument_has_value, argument_value, &candidate_limit))
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
        break;
        case BUILD_ARGUMENT_CI:
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
            if ((command == BUILD_COMMAND_GENERATE || command == BUILD_COMMAND_SELF_HOST_FROM_EXISTING) &&
                (build_argument == BUILD_ARGUMENT_CI || build_argument == BUILD_ARGUMENT_FUZZ) &&
                build_argument_read_optional_bool(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                if (build_argument == BUILD_ARGUMENT_CI)
                {
                    generate.ci = value;
                }
                else
                {
                    generate.fuzz_available = value;
                }
            }
            else if (command == BUILD_COMMAND_GENERATE && build_argument_read_optional_bool(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                switch (build_argument)
                {
                    break;
                case BUILD_ARGUMENT_CI:
                    generate.ci = value;
                    break;
                case BUILD_ARGUMENT_FUZZ:
                    generate.fuzz_available = value;
                    break;
                case BUILD_ARGUMENT_OPTIMIZE:
                    generate.optimize = value;
                    generate.optimize_set = true;
                    break;
                case BUILD_ARGUMENT_SANITIZE:
                    generate.sanitize = value;
                    break;
                case BUILD_ARGUMENT_LTO:
                    generate.lto = value;
                    break;
                case BUILD_ARGUMENT_TIME_TRACE:
                    generate.time_trace = value;
                    break;
                case BUILD_ARGUMENT_INSTRUMENT:
                    generate.instrument = value;
                    break;
                case BUILD_ARGUMENT_INCLUDE_TESTS:
                    generate.include_tests = value;
                    break;
                case BUILD_ARGUMENT_LINK_LIBC:
                    generate.link_libc = value;
                    break;
                case BUILD_ARGUMENT_CHECK_OPTIONAL_WARNINGS:
                    generate.check_optional_warnings = value;
                    break;
                case BUILD_ARGUMENT_DEVELOPER_TARGETS:
                    generate.developer_targets = value;
                    break;
                default:
                    BUSTER_UNREACHABLE();
                }
            }
            else if (command == BUILD_COMMAND_BUILD && build_argument == BUILD_ARGUMENT_OPTIMIZE &&
                     build_argument_read_optional_bool(arguments, &argument_i, argument_has_value, argument_value, &value))
            {
                options.optimize = value;
                options.optimize_set = true;
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
        break;
        case BUILD_ARGUMENT_NO_CI:
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
            if ((command == BUILD_COMMAND_GENERATE || command == BUILD_COMMAND_SELF_HOST_FROM_EXISTING) &&
                (build_argument == BUILD_ARGUMENT_NO_CI || build_argument == BUILD_ARGUMENT_NO_FUZZ) && !argument_has_value)
            {
                if (build_argument == BUILD_ARGUMENT_NO_CI)
                {
                    generate.ci = false;
                }
                else
                {
                    generate.fuzz_available = false;
                }
                argument_i += 1;
            }
            else if (command == BUILD_COMMAND_GENERATE && !argument_has_value)
            {
                switch (build_argument)
                {
                    break;
                case BUILD_ARGUMENT_NO_CI:
                    generate.ci = false;
                    break;
                case BUILD_ARGUMENT_NO_FUZZ:
                    generate.fuzz_available = false;
                    break;
                case BUILD_ARGUMENT_NO_OPTIMIZE:
                    generate.optimize = false;
                    generate.optimize_set = true;
                    break;
                case BUILD_ARGUMENT_NO_SANITIZE:
                    generate.sanitize = false;
                    break;
                case BUILD_ARGUMENT_NO_LTO:
                    generate.lto = false;
                    break;
                case BUILD_ARGUMENT_NO_TIME_TRACE:
                    generate.time_trace = false;
                    break;
                case BUILD_ARGUMENT_NO_INSTRUMENT:
                    generate.instrument = false;
                    break;
                case BUILD_ARGUMENT_NO_INCLUDE_TESTS:
                    generate.include_tests = false;
                    break;
                case BUILD_ARGUMENT_NO_LINK_LIBC:
                    generate.link_libc = false;
                    break;
                case BUILD_ARGUMENT_NO_CHECK_OPTIONAL_WARNINGS:
                    generate.check_optional_warnings = false;
                    break;
                case BUILD_ARGUMENT_NO_DEVELOPER_TARGETS:
                    generate.developer_targets = false;
                    break;
                default:
                    BUSTER_UNREACHABLE();
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
        break;
        case BUILD_ARGUMENT_VERBOSE:
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
        break;
        case BUILD_ARGUMENT_QUIET:
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
            fprintf(stderr, "error: --optimize %s conflicts with --config %.*s\n", generate.optimize ? "ON" : "OFF",
                    string8_printf_length(build_config, UINT64_MAX), build_config.pointer ? build_config.pointer : "");
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
            fprintf(stderr, "error: --optimize %s conflicts with --config %.*s\n", options.optimize ? "ON" : "OFF",
                    string8_printf_length(options.config, UINT64_MAX), options.config.pointer ? options.config.pointer : "");
            result = PROCESS_RESULT_FAILED;
        }
    }

    if (result == PROCESS_RESULT_SUCCESS)
    {
        switch (command)
        {
            break;
        case BUILD_COMMAND_COUNT:
            BUSTER_UNREACHABLE();
            break;
        case BUILD_COMMAND_NONE:
        {
        }
        break;
        case BUILD_COMMAND_GENERATE:
        {
            generate.cmake_arguments = string8_list_to_slice(arena, generate_cmake_arguments);
            BuildStep* generate_step = step_add(arena);
            generate_add(arena, generate_step, generate);
        }
        break;
        case BUILD_COMMAND_BUILD:
        {
            build_add(arena, build_directory, string8_list_to_slice(arena, build_targets), string8_list_to_slice(arena, native_arguments), options);
        }
        break;
        case BUILD_COMMAND_CLANG_ANALYZE:
        {
            result = clang_analyze_add(arena, clang_analyze_options);
        }
        break;
        case BUILD_COMMAND_CMAKE_PROFILE_SUMMARY:
        {
            cmake_profile_summary_action_add(arena, cmake_profile_summary_options);
        }
        break;
        case BUILD_COMMAND_NINJA_LOG_SUMMARY:
        {
            ninja_log_summary_action_add(arena, ninja_log_summary_options);
        }
        break;
        case BUILD_COMMAND_TIME_TRACE_SUMMARY:
        {
            time_trace_summary_options.paths = string8_list_to_slice(arena, time_trace_summary_paths);
            time_trace_summary_action_add(arena, time_trace_summary_options);
        }
        break;
        case BUILD_COMMAND_TIME_TRACE_SUMMARY_SELF_TEST:
        {
            result = time_trace_summary_self_test(arena);
        }
        break;
        case BUILD_COMMAND_TEST_TIMING_SUMMARY:
        {
            test_timing_summary_options.paths = string8_list_to_slice(arena, test_timing_summary_paths);
            test_timing_summary_action_add(arena, test_timing_summary_options);
        }
        break;
        case BUILD_COMMAND_TEST_TIMING_SUMMARY_SELF_TEST:
        {
            result = test_timing_summary_self_test(arena);
        }
        break;
        case BUILD_COMMAND_IMPORT_ASSEMBLY_METADATA:
        {
            if (!assembly_import_options.xed_datafiles_set || !assembly_import_options.aarch64_json_set)
            {
                string_print(S8("error: import_assembly_metadata requires <xed-datafiles-directory> <aarch64-tblgen-json> [output-directory]\n"));
                result = PROCESS_RESULT_FAILED;
            }
            else
            {
                assembly_import_action_add(arena, assembly_import_options);
            }
        }
        break;
        case BUILD_COMMAND_TEST_SELF_HOST:
        {
            result = self_host_add(arena, build_directory, options, generate);
        }
        break;
        case BUILD_COMMAND_SELF_HOST_FROM_EXISTING:
        {
            result = self_host_from_existing_command_add(arena, build_directory, provenance_record_path, options, generate);
        }
        break;
        case BUILD_COMMAND_TEST_ALL_COMBINATIONS:
        case BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI:
        {
            bool ci = command == BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI;
            result = test_all(arena, ci, options);
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
        String8 error_output = {.pointer = (char8*)wait_result.streams[STANDARD_STREAM_ERROR].pointer,
                                .length = wait_result.streams[STANDARD_STREAM_ERROR].length};
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

    if (run->cleanup_callback)
    {
        ProcessResult cleanup_result = run->cleanup_callback(arena, run->cleanup_data);
        if (result == PROCESS_RESULT_SUCCESS && cleanup_result != PROCESS_RESULT_SUCCESS)
        {
            result = cleanup_result;
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
        u32 pending_count = 0;
        ProcessRun* first_pending = step->first_process;

        for (ProcessRun* run = step->first_process; run; run = run->next)
        {
            // A failed process or callback stops this server's build graph.
            // Runs already in the current batch are still waited for so they
            // can reap their children, but no later work is scheduled.
            if (result != PROCESS_RESULT_SUCCESS)
            {
                break;
            }

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
                        printf("%.*s%.*s took %llu.%06llu seconds (%llu nanoseconds)\n", string8_printf_length(wait->timing_description, UINT64_MAX),
                               wait->timing_description.pointer ? wait->timing_description.pointer : "",
                               string8_printf_length(wait->timing_configuration, UINT64_MAX),
                               wait->timing_configuration.pointer ? wait->timing_configuration.pointer : "", (unsigned long long)seconds_whole,
                               (unsigned long long)seconds_fraction, (unsigned long long)elapsed_ns);
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
                            printf("%.*s%.*s took %llu.%06llu seconds (%llu nanoseconds)\n", string8_printf_length(wait->timing_description, UINT64_MAX),
                                   wait->timing_description.pointer ? wait->timing_description.pointer : "",
                                   string8_printf_length(wait->timing_configuration, UINT64_MAX),
                                   wait->timing_configuration.pointer ? wait->timing_configuration.pointer : "", (unsigned long long)seconds_whole,
                                   (unsigned long long)seconds_fraction, (unsigned long long)elapsed_ns);
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

        if (result != PROCESS_RESULT_SUCCESS)
        {
            break;
        }
    }

    if (clang_analyze_summary.analyzed || clang_analyze_summary.failures || clang_analyze_summary.warnings)
    {
        string_print(S8("clang --analyze checked {u64} translation unit(s), {u64} with analyzer warning(s), {u64} failed.\n"), clang_analyze_summary.analyzed,
                     clang_analyze_summary.warnings, clang_analyze_summary.failures);
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
            string_print(S8("+ {S8} cmake_profile_summary {S8} --limit {u64}\n"), self, pending_cmake_profile_summary_options.profile,
                         pending_cmake_profile_summary_options.limit);
            result = cmake_profile_summary_run(arena, pending_cmake_profile_summary_options);
        }
    }

    return result;
}
