#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#include <buster/base.h>
#include <buster/os.h>
#include <buster/entry_point.h>
#include <buster/integer.h>
#include <buster/string.h>
#include <buster/target.h>

#include <buster/assertion.c>
#include <buster/string.c>
#include <buster/os.c>
#include <buster/arena.c>
#include <buster/integer.c>
#include <buster/entry_point.c>
#include <buster/target.c>

typedef enum BuildCommand
{
    BUILD_COMMAND_NONE,
    BUILD_COMMAND_GENERATE,
    BUILD_COMMAND_BUILD,
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
    BUILD_ARGUMENT_COUNT,
} BuildArgument;

BUSTER_GLOBAL_LOCAL String8 build_arguments[] = {
    [BUILD_ARGUMENT_CC] = S8_INITIALIZER("--cc"),
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

    generic_tool_run_add_end(r);
}

// BUSTER_GLOBAL_LOCAL void build_add(Arena* arena, BuildStep* step)
// {

// CONFIGS = ("Debug", "Release", "RelWithDebInfo", "MinSizeRel")
//
// def parse_arguments(argv):
//     if "--" in argv:
//         separator = argv.index("--")
//         argv, native_arguments = argv[:separator], argv[separator + 1:]
//     else:
//         native_arguments = []
//
//     parser = argparse.ArgumentParser(
//         allow_abbrev=False,
//         description="Build buster with CMake.",
//     )
//     parser.add_argument(
//         "--build-directory",
//         "--build-dir",
//         default="build",
//         help="CMake build directory.",
//     )
//     parser.add_argument(
//         "--config",
//         "--configuration",
//         choices=CONFIGS,
//         default=None,
//         help="Build configuration.",
//     )
//     parser.add_argument(
//         "-v",
//         "--verbose",
//         action="store_true",
//         help="Show verbose build output.",
//     )
//     parser.add_argument(
//         "--quiet",
//         action="store_true",
//         help="Pass --quiet to the native build tool and suppress wrapper output.",
//     )
//     parser.add_argument(
//         "--clean-first",
//         action="store_true",
//         help="Build target clean first, then build.",
//     )
//     parser.add_argument(
//         "-j",
//         "--parallel",
//         nargs="?",
//         const="",
//         default=None,
//         metavar="JOBS",
//         help="Build in parallel, optionally with a job count.",
//     )
//     parser.add_argument(
//         "-t",
//         "--target",
//         dest="option_targets",
//         action="append",
//         default=[],
//         help="Build target.",
//     )
//     parser.add_argument("targets", nargs="*", help="Build targets.")
//     arguments = parser.parse_args(argv)
//     return arguments, native_arguments
//
//
// def command_string(command):
//     return " ".join(shlex.quote(str(argument)) for argument in command)
//
//
// def timed_subprocess_call(command, description, quiet=False):
//     start_ns = time.perf_counter_ns()
//     try:
//         return subprocess.call(command)
//     finally:
//         if not quiet:
//             elapsed_ns = time.perf_counter_ns() - start_ns
//             elapsed_seconds = elapsed_ns / 1_000_000_000
//             print(
//                 f"{description} took {elapsed_seconds:.3f} seconds ({elapsed_ns} nanoseconds)",
//                 flush=True,
//             )
//
//
// def main(argv):
//     arguments, native_arguments = parse_arguments(argv)
//
//     command = ["cmake", "--build", arguments.build_directory]
//
//     if arguments.config:
//         command.extend(["--config", arguments.config])
//
//     if arguments.parallel is not None:
//         command.append("--parallel")
//         if arguments.parallel:
//             command.append(arguments.parallel)
//
//     targets = [*arguments.option_targets, *arguments.targets]
//     if targets:
//         command.extend(["--target", *targets])
//
//     if arguments.clean_first:
//         command.append("--clean-first")
//
//     if arguments.verbose:
//         command.append("--verbose")
//
//     quiet = arguments.quiet or "--quiet" in native_arguments
//     if arguments.quiet and "--quiet" not in native_arguments:
//         native_arguments.append("--quiet")
//
//     if native_arguments:
//         command.extend(["--", *native_arguments])
//
//     if not quiet:
//         print(f"+ {command_string(command)}", flush=True)
//     return timed_subprocess_call(command, "ninja", quiet=quiet)
//
//
// if __name__ == "__main__":
//     sys.exit(main(sys.argv[1:]))

// }

BUSTER_GLOBAL_LOCAL void test_all(Arena* arena, bool ci)
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
                        String8 analyze_parts[] = {
                            S8("--target"),
                            S8("clang_analyze"),
                        };
                        build_add(arena, build_directory, (SliceString8)BUSTER_ARRAY_TO_SLICE(analyze_parts), options);
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
            break;
            case BUILD_COMMAND_TEST_ALL_COMBINATIONS:
            case BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI:
            {
                bool ci = command == BUILD_COMMAND_TEST_ALL_COMBINATIONS_CI;
                test_all(arena, ci);
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
