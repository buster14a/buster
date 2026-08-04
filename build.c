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
    BUILD_COMMAND_IMPORT_ASSEMBLY_METADATA,
    BUILD_COMMAND_TEST_SELF_HOST,
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
    [BUILD_COMPILER_CL] = S8_INITIALIZER("cl"),   [BUILD_COMPILER_CLANG] = S8_INITIALIZER("clang"), [BUILD_COMPILER_GCC] = S8_INITIALIZER("gcc"),
    [BUILD_COMPILER_TCC] = S8_INITIALIZER("tcc"), [BUILD_COMPILER_ZIG] = S8_INITIALIZER("zig"),
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
    case BUILD_COMPILER_TCC:
        return get_resolved_path(arena, &tcc_path, S8("tcc"));
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
    u32 optimize : 1;
    u32 optimize_set : 1;
    u32 quiet : 1;
    u32 verbose : 1;
};

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

BUSTER_GLOBAL_LOCAL ByteSlice self_host_canonicalize_pe(Arena* arena, ByteSlice image)
{
    SelfHostRsdsPath path = {0};
    if (!arena || !self_host_find_rsds_path(image, &path))
    {
        return (ByteSlice){0};
    }
    String8 canonical_path = S8("self-host.pdb");
    u64 path_size = path.end - path.start;
    u64 canonical_size = canonical_path.length + 1;
    if (path_size > image.length || image.length - path_size > UINT64_MAX - canonical_size)
    {
        return (ByteSlice){0};
    }
    u64 size = image.length - path_size + canonical_size;
    u8* bytes = arena_allocate(arena, u8, size);
    memcpy(bytes, image.pointer, path.start);
    memcpy(bytes + path.start, canonical_path.pointer, canonical_path.length);
    bytes[path.start + canonical_path.length] = 0;
    memcpy(bytes + path.start + canonical_size, image.pointer + path.end, image.length - path.end);
    return (ByteSlice){
        .pointer = bytes,
        .length = size,
    };
}
#endif

BUSTER_GLOBAL_LOCAL void build_run_add(Arena* arena, BuildStep* step, String8 build_directory, SliceString8 targets, SliceString8 native_arguments,
                                       CmakeBuildOptions options)
{
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

BUSTER_GLOBAL_LOCAL void build_add(Arena* arena, String8 build_directory, SliceString8 targets, SliceString8 native_arguments, CmakeBuildOptions options)
{
    BuildStep* step = step_add(arena);
    build_run_add(arena, step, build_directory, targets, native_arguments, options);
}

BUSTER_GLOBAL_LOCAL ProcessResult self_host_compare_action(Arena* arena, void* data)
{
    SelfHostCompare* compare = data;
    ByteSlice stage1 = file_read(arena, compare->stage1, (FileReadOptions){0});
    ByteSlice stage2 = file_read(arena, compare->stage2, (FileReadOptions){0});
    if (!stage1.pointer || !stage2.pointer)
    {
        string_print(S8("error: could not read self-host stage outputs\n"));
        return PROCESS_RESULT_FAILED;
    }
#if BUSTER_WINDOWS
    ByteSlice canonical_stage1 = self_host_canonicalize_pe(arena, stage1);
    ByteSlice canonical_stage2 = self_host_canonicalize_pe(arena, stage2);
    ByteSlice pdb1 = file_read(arena, compare->pdb1, (FileReadOptions){0});
    ByteSlice pdb2 = file_read(arena, compare->pdb2, (FileReadOptions){0});
    bool executable_equal = canonical_stage1.pointer && canonical_stage2.pointer && canonical_stage1.length == canonical_stage2.length &&
                            memory_compare(canonical_stage1.pointer, canonical_stage2.pointer, canonical_stage1.length);
    bool pdb_equal = pdb1.pointer && pdb2.pointer && pdb1.length == pdb2.length && memory_compare(pdb1.pointer, pdb2.pointer, pdb1.length);
    if (!executable_equal || !pdb_equal)
#else
    if (stage1.length != stage2.length || !memory_compare(stage1.pointer, stage2.pointer, stage1.length))
#endif
    {
        string_print(S8("error: self-host stages differ: {S8} ({u64} bytes) != {S8} ({u64} bytes)\n"), compare->stage1, stage1.length, compare->stage2,
                     stage2.length);
        return PROCESS_RESULT_FAILED;
    }
    string_print(S8("SELF_HOST deterministic bytes={u64} stage1={S8} stage2={S8}\n"), stage1.length, compare->stage1, compare->stage2);
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL void self_host_compile_add(Arena* arena, String8 compiler, String8 build_directory, String8 sysroot, String8 output,
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

typedef struct AssemblyImportOptions AssemblyImportOptions;
struct AssemblyImportOptions
{
    String8 xed_datafiles;
    String8 aarch64_json;
    String8 output_directory;
    u32 xed_datafiles_set : 1;
    u32 aarch64_json_set : 1;
    u32 output_directory_set : 1;
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
    *node = (TimeTraceRowNode){.row = {.name = name, .duration_us = duration_us}};

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
    u32 has_duration : 1;
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
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});
    if (!bytes.pointer)
    {
        string_print(S8("error: failed to read {S8}\n"), path);
        return PROCESS_RESULT_FAILED;
    }

    JsonParser parser = {.text = {.pointer = (char8*)bytes.pointer, .length = bytes.length}};
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
    typedef struct TestCombination TestCombination;
    struct TestCombination
    {
        String8 build_directory;
        CmakeBuildOptions options;
        BuildCompiler compiler;
        u32 sanitize : 1;
        u32 run_app_tests : 1;
    };

    TestCombination combinations[BUILD_COMPILER_COUNT * 4] = {0};
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
        if (ci && compiler == BUILD_COMPILER_TCC && (BUSTER_WINDOWS || BUSTER_APPLE))
        {
            continue;
        }

        bool fuzz_supported = compiler == BUILD_COMPILER_CLANG && !BUSTER_APPLE;
        bool support_sanitize = compiler == BUILD_COMPILER_CLANG;
        bool support_optimize = compiler != BUILD_COMPILER_TCC;

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
                String8 cmake_profile_path = path_join(arena, build_directory, S8("cmake-profile.json"));

                Generate generate = {
                    .build_directory = build_directory,
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
                    combinations[combination_count++] = (TestCombination){
                        .build_directory = build_directory,
                        .options =
                            {
                                .optimize = optimize,
                                .optimize_set = true,
                                .quiet = base_options.quiet,
                            },
                        .compiler = compiler,
                        .sanitize = sanitize,
                        .run_app_tests = (sanitize && !optimize) || (!sanitize && optimize),
                    };
                }
            }
        }
    }

    BuildStep* release_build_step = step_add(arena);
    for (u64 combination_i = 0; combination_i < combination_count; combination_i += 1)
    {
        TestCombination combination = combinations[combination_i];
        if (!combination.sanitize && combination.options.optimize)
        {
            build_run_add(arena, release_build_step, combination.build_directory, (SliceString8){0}, (SliceString8){0}, combination.options);
        }
    }

    for (u64 combination_i = 0; combination_i < combination_count; combination_i += 1)
    {
        TestCombination combination = combinations[combination_i];
        if (combination.sanitize || !combination.options.optimize)
        {
            build_add(arena, combination.build_directory, (SliceString8){0}, (SliceString8){0}, combination.options);
        }

        String8 test_targets[] = {
            combination.run_app_tests ? S8("test_all") : S8("test_units"),
        };
        build_add(arena, combination.build_directory, (SliceString8)BUSTER_ARRAY_TO_SLICE(test_targets), (SliceString8){0}, combination.options);

        if (combination.compiler == BUILD_COMPILER_CLANG && !combination.sanitize && combination.options.optimize)
        {
            clang_analyze_command_add(arena, combination.build_directory, combination.options);
        }
    }
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
        if (string_starts_with_sequence(operand->atom, S8("TMM")))
        {
            form->amx_flags |= XED_GENERATED_AMX_TILE_REGISTER;
            if (operand->kind == XED_GENERATED_OPERAND_MEMORY)
            {
                form->amx_flags |= XED_GENERATED_AMX_TILE_MEMORY;
            }
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
        if (string_equal(token, S8("NDD")))
        {
            form->apx_flags |= XED_GENERATED_APX_NDD;
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
                            "#define BUSTER_X86_GENERATED_SCHEMA_VERSION 1\n\n"));
}

BUSTER_GLOBAL_LOCAL bool xed_import_emit_generated_tables(Arena* output, Arena* coverage_output, Arena* arena,
                                                           XedGeneratedFormList forms, XedGeneratedTableStats* stats)
{
    XedGeneratedStringPool pool = {0};
    xed_generated_intern_forms(&pool, arena, forms);
    XedGeneratedStringNode** sorted = 0;
    if (!xed_generated_string_pool_prepare(arena, &pool, &sorted))
    {
        return false;
    }

    u32* operand_offsets = arena_allocate(arena, u32, forms.length);
    u32 operand_count = 0;
    for (u64 index = 0; index < forms.length; index += 1)
    {
        operand_offsets[index] = operand_count;
        if (forms.pointer[index].operand_count > UINT32_MAX - operand_count)
        {
            return false;
        }
        operand_count += forms.pointer[index].operand_count;
    }
    stats->operand_count = operand_count;
    stats->string_pool_bytes = pool.byte_count;

    xed_generated_emit_preamble(output);
    xed_generated_emit_string_pool(output, arena, &pool, sorted);
    arena_append_string8(output, S8("static const BusterX86GeneratedOperand buster_x86_generated_operands[] = {\n"));
    for (u64 index = 0; index < forms.length; index += 1)
    {
        for (u32 operand_index = 0; operand_index < forms.pointer[index].operand_count; operand_index += 1)
        {
            xed_generated_append_operand(output, &pool, forms.pointer[index].operands + operand_index);
        }
    }
    arena_append_string8(output, S8("};\n\nstatic const BusterX86GeneratedForm buster_x86_generated_forms[] = {\n"));
    for (u64 index = 0; index < forms.length; index += 1)
    {
        XedGeneratedForm* form = forms.pointer + index;
        if (form->coverage_class == XED_GENERATED_COVERAGE_UNCLASSIFIED ||
            ((form->coverage_class == XED_GENERATED_COVERAGE_UNSUPPORTED_TOKEN ||
              form->coverage_class == XED_GENERATED_COVERAGE_RESERVED) &&
             form->reason_id == XED_GENERATED_REASON_NONE))
        {
            return false;
        }
        xed_generated_append_form(output, &pool, form, operand_offsets[index]);
        stats->coverage_counts[form->coverage_class] += 1;
        stats->reason_counts[form->reason_id] += 1;
        stats->encoder_counts[form->encoder_family] += 1;
        stats->test_counts[form->test_class] += 1;
        stats->token_count += form->token_count;
    }
    arena_append_string8(output, S8("};\n\n"));
    arena_append_string8(output, S8("#define BUSTER_X86_GENERATED_FORM_COUNT "));
    xed_generated_append_decimal(output, forms.length);
    arena_append_string8(output, S8("\n#define BUSTER_X86_GENERATED_OPERAND_COUNT "));
    xed_generated_append_decimal(output, operand_count);
    arena_append_string8(output, S8("\n#define BUSTER_X86_GENERATED_STRING_POOL_SIZE "));
    xed_generated_append_decimal(output, pool.byte_count);
    arena_append_string8(output, S8("\n\n#include \"x86_64-coverage.generated.inc\"\n\n#endif\n"));

    arena_append_string8(coverage_output,
                         S8("/* Generated by build import_assembly_metadata; do not edit. */\n"
                            "#define BUSTER_X86_GENERATED_COVERAGE_COUNT "));
    xed_generated_append_decimal(coverage_output, forms.length);
    arena_append_string8(coverage_output, S8("\nstatic const struct BusterX86GeneratedCoverage buster_x86_generated_coverage[] = {\n"));
    for (u64 index = 0; index < forms.length; index += 1)
    {
        XedGeneratedForm* form = forms.pointer + index;
        arena_append_string8(coverage_output, S8("    {"));
        xed_generated_append_hex(coverage_output, form->stable_hash, true);
        arena_append_string8(coverage_output, S8(", "));
        xed_generated_append_decimal(coverage_output, xed_generated_string_offset(&pool, form->record->source));
        arena_append_string8(coverage_output, S8(", "));
        xed_generated_append_decimal(coverage_output, index);
        arena_append_string8(coverage_output, S8(", "));
        xed_generated_append_decimal(coverage_output, form->coverage_class);
        arena_append_string8(coverage_output, S8(", "));
        xed_generated_append_decimal(coverage_output, form->encoder_family);
        arena_append_string8(coverage_output, S8(", "));
        xed_generated_append_decimal(coverage_output, form->test_class);
        arena_append_string8(coverage_output, S8(", "));
        xed_generated_append_decimal(coverage_output, form->reason_id);
        arena_append_string8(coverage_output, S8(", "));
        xed_generated_append_offset(coverage_output, &pool, form->unsupported_token);
        arena_append_string8(coverage_output, S8("},\n"));
    }
    arena_append_string8(coverage_output, S8("};\n"));
    stats->header_bytes = arena_buffer_size(output);
    stats->coverage_bytes = arena_buffer_size(coverage_output);
    return stats->coverage_counts[XED_GENERATED_COVERAGE_UNCLASSIFIED] == 0;
}

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

BUSTER_GLOBAL_LOCAL SliceString8 aarch64_import_instruction_names(Arena* arena, String8 json, bool* valid)
{
    SliceString8 result = {0};
    String8 instances = {0};
    String8 names_array = {0};
    if (!json_raw_object_find(json, S8("!instanceof"), &instances) || !json_raw_object_find(instances, S8("AArch64Inst"), &names_array))
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
    }
    arena_append_char8(output, ']');
    return valid;
}

BUSTER_GLOBAL_LOCAL bool aarch64_import_emit(Arena* arena, Arena* output, String8 json, u64* imported_count)
{
    bool valid = true;
    SliceString8 instruction_names = aarch64_import_instruction_names(arena, json, &valid);
    if (!valid || !instruction_names.length)
    {
        return false;
    }

    JsonParser parser = {.text = json};
    valid = json_consume(&parser, '{');
    u64 count = 0;
    while (valid)
    {
        String8 raw_name = {0};
        String8 object = {0};
        if (!json_raw_object_next(&parser, &raw_name, &object, &valid))
        {
            break;
        }
        String8 name = json_raw_key_text(raw_name);
        if (!assembly_import_string_find(instruction_names, name))
        {
            continue;
        }

        Aarch64ImportFields fields = {0};
        if (!aarch64_import_fields(object, &fields))
        {
            valid = false;
            break;
        }
        if (!string_equal(fields.pseudo, S8("0")))
        {
            continue;
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
            valid = false;
            break;
        }
        arena_append_string8(output, S8("}\n"));
        count += 1;
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
    String8 checksum_a = assembly_import_checksum(arena, S8("deterministic"));
    String8 checksum_b = assembly_import_checksum(arena, S8("deterministic"));
    result = result && string_equal(checksum_a, checksum_b);
    XedGeneratedFormList normalized_forms = {.pointer = &normalized_form, .length = 1};
    XedGeneratedTableStats schema_stats_a = {0};
    XedGeneratedTableStats schema_stats_b = {0};
    result = result && xed_import_emit_generated_tables(schema_a, coverage_a, arena, normalized_forms, &schema_stats_a) &&
             xed_import_emit_generated_tables(schema_b, coverage_b, arena, normalized_forms, &schema_stats_b) &&
             string_equal(assembly_import_arena_contents(schema_a), assembly_import_arena_contents(schema_b)) &&
             string_equal(assembly_import_arena_contents(coverage_a), assembly_import_arena_contents(coverage_b)) &&
             schema_stats_a.header_bytes == schema_stats_b.header_bytes && schema_stats_a.coverage_bytes == schema_stats_b.coverage_bytes &&
             schema_stats_a.coverage_counts[XED_GENERATED_COVERAGE_NORMALIZED] == 1 && schema_stats_a.reason_counts[XED_GENERATED_REASON_NONE] == 1;

    arena_reset_to_start(schema_a);
    arena_reset_to_start(coverage_a);
    XedGeneratedFormList unsupported_forms = {.pointer = &bad_pattern_form, .length = 1};
    XedGeneratedTableStats unsupported_stats = {0};
    result = result && xed_import_emit_generated_tables(schema_a, coverage_a, arena, unsupported_forms, &unsupported_stats) &&
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

    arena_destroy(coverage_b, 1);
    arena_destroy(coverage_a, 1);
    arena_destroy(schema_b, 1);
    arena_destroy(schema_a, 1);
    arena_destroy(output, 1);
    arena_destroy(arena, 1);
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
    Arena* xed_config_checksum_input = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
    Arena* xed_checksum_input = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    if (!xed_output || !generated_output || !coverage_output || !aarch64_output || !xed_config_checksum_input || !xed_checksum_input)
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
        else if (!xed_import_emit_generated_tables(generated_output, coverage_output, arena, generated_forms, &generated_stats))
        {
            string_print(S8("error: generated XED coverage contains UNCLASSIFIED rows\n"));
            result = PROCESS_RESULT_FAILED;
        }
    }

    FileMapRead aarch64_map = {0};
    u64 aarch64_count = 0;
    String8 aarch64_json = {0};
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
            if (!aarch64_import_emit(arena, aarch64_output, aarch64_json, &aarch64_count))
            {
                string_print(S8("error: malformed or incomplete AArch64 llvm-tblgen JSON in {S8}\n"), options.aarch64_json);
                result = PROCESS_RESULT_FAILED;
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
        String8 xed_config_checksum = assembly_import_checksum(arena, assembly_import_arena_contents(xed_config_checksum_input));
        String8 xed_input_checksum = assembly_import_checksum(arena, assembly_import_arena_contents(xed_checksum_input));
        String8 aarch64_input_checksum = assembly_import_checksum(arena, aarch64_json);
        String8 xed_output_checksum = assembly_import_checksum(arena, xed_content);
        String8 generated_output_checksum = assembly_import_checksum(arena, generated_content);
        String8 coverage_output_checksum = assembly_import_checksum(arena, coverage_content);
        String8 aarch64_output_checksum = assembly_import_checksum(arena, aarch64_content);
        u64 iclass_count = xed_import_unique_record_value_count(arena, xed_records, false);
        u64 iform_count = xed_import_unique_record_value_count(arena, xed_records, true);
        u64 missing_iform_count = 0;
        for (XedImportRecord* record = xed_records.first; record; record = record->next)
        {
            missing_iform_count += !record->iform.length;
        }
        String8 manifest = string_format(
            arena,
            S8("{{\n"
               "  \"schema_version\": 2,\n"
               "  \"checksum_algorithm\": \"xxh64\",\n"
               "  \"xed\": {{\n"
               "    \"source_url\": \"https://github.com/intelxed/xed\",\n"
               "    \"release\": \"v2026.07.15\",\n"
               "    \"commit\": \"519c843c86547e2003f5a404a53358a7dcfb82f3\",\n"
               "    \"license\": \"Apache-2.0\",\n"
               "    \"config_file_count\": {u64},\n"
               "    \"source_file_count\": {u64},\n"
               "    \"config_checksum\": \"{S8}\",\n"
               "    \"input_checksum\": \"{S8}\",\n"
               "    \"output_checksum\": \"{S8}\",\n"
               "    \"form_count\": {u64},\n"
               "    \"iclass_count\": {u64},\n"
               "    \"iform_count\": {u64},\n"
               "    \"missing_iform_count\": {u64}\n"
               "  },\n"
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
               "  },\n"
               "  \"llvm\": {{\n"
               "    \"source_url\": \"https://github.com/llvm/llvm-project\",\n"
               "    \"release\": \"llvmorg-22.1.8\",\n"
               "    \"commit\": \"ca7933e47d3a3451d81e72ac174dcb5aa28b59d1\",\n"
               "    \"license\": \"Apache-2.0 WITH LLVM-exception\",\n"
               "    \"input_checksum\": \"{S8}\",\n"
               "    \"output_checksum\": \"{S8}\",\n"
               "    \"instruction_count\": {u64}\n"
               "  }\n"
               "}\n"),
            config_paths.count, source_paths.count, xed_config_checksum, xed_input_checksum, xed_output_checksum, xed_records.count,
            iclass_count, iform_count, missing_iform_count, generated_output_checksum, generated_stats.header_bytes,
            coverage_output_checksum, generated_stats.coverage_bytes, generated_stats.operand_count, generated_stats.token_count,
            generated_stats.string_pool_bytes, generated_stats.coverage_counts[0], generated_stats.coverage_counts[1],
            generated_stats.coverage_counts[2], generated_stats.coverage_counts[3], generated_stats.coverage_counts[4],
            generated_stats.coverage_counts[5], generated_stats.coverage_counts[6], generated_stats.reason_counts[0],
            generated_stats.reason_counts[1], generated_stats.reason_counts[2], generated_stats.reason_counts[3], generated_stats.reason_counts[4],
            aarch64_input_checksum, aarch64_output_checksum, aarch64_count);

        if (!assembly_import_write(arena, options.output_directory, S8("x86_64-xed.jsonl"), xed_content) ||
            !assembly_import_write(arena, options.output_directory, S8("x86_64-assembly.generated.h"), generated_content) ||
            !assembly_import_write(arena, options.output_directory, S8("x86_64-coverage.generated.inc"), coverage_content) ||
            !assembly_import_write(arena, options.output_directory, S8("aarch64-llvm.jsonl"), aarch64_content) ||
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
            string_print(S8("Coverage: DIRECT={u64} NORMALIZED={u64} NOT64={u64} PRIVILEGED={u64} RESERVED={u64} UNSUPPORTED_TOKEN={u64} UNCLASSIFIED={u64}.\n"),
                         generated_stats.coverage_counts[0], generated_stats.coverage_counts[1], generated_stats.coverage_counts[2],
                         generated_stats.coverage_counts[3], generated_stats.coverage_counts[4], generated_stats.coverage_counts[5],
                         generated_stats.coverage_counts[6]);
        }
    }

    if (aarch64_map.mapped_pointer)
    {
        file_map_unmap(aarch64_map);
    }
    arena_destroy(xed_checksum_input, 1);
    arena_destroy(xed_config_checksum_input, 1);
    arena_destroy(aarch64_output, 1);
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
        [BUILD_COMMAND_IMPORT_ASSEMBLY_METADATA] = S8_INITIALIZER("import_assembly_metadata"),
        [BUILD_COMMAND_TEST_SELF_HOST] = S8_INITIALIZER("test_self_host"),
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
    ClangAnalyzeOptions clang_analyze_options = {.compile_commands = build_directory};
    CmakeProfileSummaryOptions cmake_profile_summary_options = {.limit = 25};
    NinjaLogSummaryOptions ninja_log_summary_options = {.limit = 25};
    TimeTraceSummaryOptions time_trace_summary_options = {.limit = 25};
    AssemblyImportOptions assembly_import_options = {.output_directory = S8("src/buster/lib/compiler/assembly/generated")};
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
        case BUILD_ARGUMENT_CC:
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
                else if (command == BUILD_COMMAND_BUILD || command == BUILD_COMMAND_TEST_SELF_HOST)
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
            bool is_summary_command =
                command == BUILD_COMMAND_CMAKE_PROFILE_SUMMARY || command == BUILD_COMMAND_NINJA_LOG_SUMMARY || command == BUILD_COMMAND_TIME_TRACE_SUMMARY;
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
            if (command == BUILD_COMMAND_GENERATE && build_argument_read_optional_bool(arguments, &argument_i, argument_has_value, argument_value, &value))
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
            if (command == BUILD_COMMAND_GENERATE && !argument_has_value)
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
