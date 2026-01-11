#define BUSTER_PREAMBLE
#define BUSTER_UNITY_BUILD 1
#include <buster/base.h>
#include <buster/entry_point.h>
#include <buster/build/build_common.h>
#include <buster/string_os.h>
#include <buster/path.h>

#include <buster/memory.c>
#include <buster/integer.c>
#include <buster/string.c>
#include <buster/string8.c>
#if defined(_WIN32)
#include <buster/string16.c>
#endif
#include <buster/string_os.c>
#include <buster/assertion.c>
#include <buster/os.c>
#include <buster/arena.c>
#include <buster/target.c>
#include <buster/path.c>
#include <buster/build/build_common.c>
#include <buster/file.c>
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.c>
#endif
#include <buster/entry_point.c>

STRUCT(PreambleProgramState)
{
    ProgramState general_state;
};

BUSTER_IMPL ProcessResult thread_entry_point()
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    let arena = thread_arena();
    let toolchain_information = toolchain_get_information(arena, current_llvm_version);
    let clang_path = toolchain_information.clang_path;

#if BUSTER_CI
    bool successfully_downloaded = build_flag_get(BUILD_FLAG_SELF_HOSTED);

    if (!successfully_downloaded)
    {
        let filename = SOs("toolchain.7z");
        StringOs curl_arguments_parts[] = {
            SOs("curl"),
            SOs("-L"),
            SOs("-o"),
            filename,
            toolchain_information.url,
        };
        let curl_arguments = string_os_list_create_from(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, curl_arguments_parts));
        let curl_process = os_process_spawn(curl_arguments_parts[0], curl_arguments, program_state->input.envp);
        if (curl_process)
        {
            if (os_process_wait_sync(curl_process, (ProcessResources){}) == PROCESS_RESULT_SUCCESS)
            {
                StringOs o_arg_parts[] = {
                    SOs("-o"),
                    toolchain_information.install_path,
                };
                let o_arg = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, o_arg_parts), true);
                StringOs arguments_7z_parts[] = {
                    SOs("7z"),
                    SOs("x"),
                    filename,
                    o_arg,
                    SOs("-y"),
                };
                let argumentz_7z = string_os_list_create_from(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, arguments_7z_parts));
                let process_7z = os_process_spawn(SOs("7z"), argumentz_7z, program_state->input.envp);

                if (process_7z)
                {
                    successfully_downloaded = os_process_wait_sync(process_7z, (ProcessResources){}) == PROCESS_RESULT_SUCCESS;
                }
            }
        }
    }

    if (successfully_downloaded)
#endif
    {
        let cwd = path_absolute_arena(arena, SOs("."));
        let target_parts = target_to_split_string_os(target_native);
        StringOs build_executable_directory_parts[(TARGET_STRING_COMPONENT_COUNT * 2 - 1) + 2] = {
            cwd,
            SOs("/build/"),
        };
        for (u64 i = 0; i < TARGET_STRING_COMPONENT_COUNT; i += 1)
        {
            build_executable_directory_parts[i * 2 + 2] = target_parts.s[i];
            if (i + 1 < TARGET_STRING_COMPONENT_COUNT)
            {
                build_executable_directory_parts[i * 2 + 3] = SOs("-");
            }
        }

        let build_executable_directory_path = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, build_executable_directory_parts), true);
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

        let build_executable = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, build_executable_parts), true);
        StringOs source_paths[] = {
            SOs("src/buster/build/build.c"),
        };
        CompileLinkOptions options = {
            .clang_path = clang_path,
#if defined(__APPLE__)
            .xc_sdk_path = ...
#endif
                .destination_path = build_executable,
            .source_paths = source_paths,
            .source_count = BUSTER_ARRAY_LENGTH(source_paths),
            .target = &build_target_native,
            .optimize = build_flag_get(BUILD_FLAG_OPTIMIZE),
            .fuzz = 0,
            .sanitize = build_flag_get(BUILD_FLAG_SANITIZE),
            .has_debug_information = build_flag_get(BUILD_FLAG_HAS_DEBUG_INFORMATION),
            .unity_build = 1,
            .use_io_ring = 0,
            .just_preprocessor = 0,
            .compile = 1,
            .link = 1,
        };
        let compile_build_arguments = build_compile_link_arguments(arena, &options);
        let compile_build_process = os_process_spawn(clang_path, compile_build_arguments, program_state->input.envp, (ProcessSpawnOptions){});

        if (compile_build_process.handle)
        {
            result = os_process_wait_sync(compile_build_process);
        }

        if (result == PROCESS_RESULT_SUCCESS)
        {
            StringOs extra_arguments[] = {};
            let build_arguments = string_os_list_duplicate_and_substitute_first_argument(arena, program_state->input.argv, build_executable, BUSTER_ARRAY_TO_SLICE(StringOsSlice, extra_arguments));
            let build_process = os_process_spawn(build_executable, build_arguments, program_state->input.envp, (ProcessSpawnOptions){});
            if (build_process.handle)
            {
                result = os_process_wait_sync(build_process);
            }
            else
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
    }

    return result;
}
