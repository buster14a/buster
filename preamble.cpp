#define BUSTER_PREAMBLE
#define BUSTER_INCLUDE_TESTS 0
#define BUSTER_UNITY_BUILD 1
#include <buster/base.h>
#include <buster/entry_point.h>
#include <buster/build/build_common.h>
#include <buster/path.h>
#include <buster/system_headers.h>

#include <buster/memory.cpp>
#include <buster/integer.cpp>
#include <buster/string.cpp>
#include <buster/assertion.cpp>
#include <buster/os.cpp>
#include <buster/arena.cpp>
#include <buster/target.cpp>
#include <buster/path.cpp>
#include <buster/build/build_common.cpp>
#include <buster/file.cpp>
#if defined(__x86_64__)
#include <buster/x86_64.cpp>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.cpp>
#endif
#include <buster/entry_point.cpp>
#include <buster/arguments.cpp>

STRUCT(PreambleProgramState)
{
    ProgramState general_state;
};

BUSTER_F_IMPL void async_user_tick()
{
}

BUSTER_F_IMPL ProcessResult entry_point()
{
    ProcessResult result = ProcessResult::Failed;
    let arena = arena_create((ArenaCreation){});
    let toolchain_information = toolchain_get_information(arena, current_llvm_version);
    let clang_path = toolchain_information.clang_path;

#if BUSTER_CI
    bool success = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_SELF_HOSTED) || build_flag_get(BuildFlag::BUILD_OPTION_FLAG_AVOID_DOWNLOAD);

    if (!success)
    {
        let filename = SOs("toolchain.7z");

        StringOs executables[] = {
            SOs("curl"),
            SOs("7z"),
        };

#if defined(_WIN32)
        CharOs buffer[BUSTER_MAX_PATH_LENGTH + 1];

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(executables); i += 1)
        {
            let executable = executables[i];

            let search_path_result = SearchPathW(0, executable.pointer, SOs(".exe").pointer, BUSTER_MAX_PATH_LENGTH, buffer, 0);
            if (search_path_result > 0 && search_path_result <= BUSTER_MAX_PATH_LENGTH)
            {
                let buffer_slice = string_os_from_pointer_length(buffer, search_path_result);
                buffer_slice.pointer[buffer_slice.length] = 0;
                StringOs parts[] = {
                    // SOs("\""),
                    buffer_slice,
                    // SOs("\""),
                };
                executables[i] = string_os_join_arena(arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(parts), true);
            }

        }
#endif
        StringOs curl_arguments_parts[] = {
            executables[0],
            SOs("-L"),
            SOs("-o"),
            filename,
            toolchain_information.url,
        };
        let curl_arguments = string_os_list_create_from(arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(curl_arguments_parts));
        let curl_process = os_process_spawn(curl_arguments_parts[0], curl_arguments, program_state->input.envp, (ProcessSpawnOptions){});
        if (curl_process.handle)
        {
            if (os_process_wait_sync(arena, curl_process).result == ProcessResult::Success)
            {
                StringOs o_arg_parts[] = {
                    SOs("-o"),
                    toolchain_information.install_path,
                };
                let o_arg = string_os_join_arena(arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(o_arg_parts), true);
                StringOs arguments_7z_parts[] = {
                    executables[1],
                    SOs("x"),
                    filename,
                    o_arg,
                    SOs("-y"),
                };
                let argumentz_7z = string_os_list_create_from(arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(arguments_7z_parts));
                let process_7z = os_process_spawn(arguments_7z_parts[0], argumentz_7z, program_state->input.envp, (ProcessSpawnOptions){});

                if (process_7z.handle)
                {
                    success = os_process_wait_sync(arena, process_7z).result == ProcessResult::Success;
                }
            }
        }
    }

    if (success)
#endif
    {
        let cwd = path_absolute_arena(arena, SOs("."));
        let target_parts = target_to_split_string_os(target_native);

        StringOs build_executable_directory_parts[((u64)TargetStringComponents::Count * 2 - 1) + 2] = {
            cwd,
            SOs("/build/"),
        };

        for (EACH_ENUM_INT(TargetStringComponents, i))
        {
            build_executable_directory_parts[i * 2 + 2] = target_parts.s[i];
            if (i + 1 < (u64)TargetStringComponents::Count)
            {
                build_executable_directory_parts[i * 2 + 3] = SOs("-");
            }
        }

        let build_executable_directory_path = string_os_join_arena(arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(build_executable_directory_parts), true);
        os_make_directory(build_executable_directory_path);

        StringOs build_executable_parts[] = {
            build_executable_directory_path,
            SOs("/build"),
#if defined(_WIN32)
            SOs(".exe"),
#else
            SOs(""),
#endif
        };

        StringOs xc_sdk_path = {};
#if defined(__APPLE__)
        StringOs xc_first_argument = SOs("xcrun");
        CharOs* xc_sdk_path_process_argv[] = {
            xc_first_argument.pointer,
            "--show-sdk-path",
            0,
        };
        let xc_process = os_process_spawn(xc_first_argument, xc_sdk_path_process_argv, program_state->input.envp, (ProcessSpawnOptions){
            .cppapture = 1 << STANDARD_STREAM_OUTPUT,
        });

        if (xc_process.handle)
        {
            let xc_process_result = os_process_wait_sync(arena, xc_process);
            if (xc_process_result.result == PROCESS_RESULT_SUCCESS)
            {
                xc_sdk_path = BYTE_SLICE_TO_STRING(8, xc_process_result.streams[STANDARD_STREAM_OUTPUT]);
                // There's a line feed character before the null terminator
                xc_sdk_path.length -= 1;
                xc_sdk_path.pointer[xc_sdk_path.length] = 0;
            }
        }
#endif

        let build_executable = string_os_join_arena(arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(build_executable_parts), true);
        StringOs source_paths[] = {
            SOs("src/buster/build/build.cpp"),
        };
        CompileLinkOptions options = {
            .clang_path = clang_path,
#if defined(__APPLE__)
            .xc_sdk_path = xc_sdk_path,
#endif
            .destination_path = build_executable,
            .source_paths = source_paths,
            .source_count = BUSTER_ARRAY_LENGTH(source_paths),
            .target = &build_target_native,
            .optimize = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_OPTIMIZE),
            .fuzz = 0,
            .sanitize = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_SANITIZE),
            .has_debug_information = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_HAS_DEBUG_INFORMATION),
            .unity_build = 1,
            .use_io_ring = 0,
            .just_preprocessor = 0,
            .include_tests = 1,
            .compile = 1,
            .link = 1,
        };
        let compile_build_arguments = build_compile_link_arguments(arena, &options);
        let compile_build_process = os_process_spawn(clang_path, compile_build_arguments, program_state->input.envp, (ProcessSpawnOptions){});

        if (compile_build_process.handle)
        {
            let compile_build_process_result = os_process_wait_sync(arena, compile_build_process);
            result = compile_build_process_result.result;
        }

        if (result == ProcessResult::Success)
        {
            StringOs xc_sdk_path_argument_parts[] = {
                SOs("--xc-sdk-path="),
                xc_sdk_path,
            };
            BUSTER_UNUSED(xc_sdk_path_argument_parts);
            StringOs extra_arguments[] = {
#if defined(__APPLE__)
                string_os_join_arena(arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(xc_sdk_path_argument_parts), true),
#endif
            };
            let build_arguments = string_os_list_duplicate_and_substitute_first_argument(arena, program_state->input.argv, build_executable, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(extra_arguments));
            let build_process = os_process_spawn(build_executable, build_arguments, program_state->input.envp, (ProcessSpawnOptions){ .capture = 0 });
            if (build_process.handle)
            {
                let build_process_result = os_process_wait_sync(arena, build_process);
                result = build_process_result.result;
            }
            else
            {
                result = ProcessResult::Failed;
            }
        }
    }

    return result;
}
