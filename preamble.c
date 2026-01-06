#define BUSTER_UNITY_BUILD 1
#include <buster/base.h>
#include <buster/entry_point.h>
#include <buster/build/build_common.h>
#include <buster/string_os.h>

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
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.c>
#endif
#include <buster/entry_point.c>

BUSTER_IMPL ProcessResult process_arguments()
{
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    let arena = thread_arena();
    StringOs home_path;
#if defined(_WIN32)
    home_path = os_get_environment_variable(SOs("USERPROFILE"));
#else
    home_path = os_get_environment_variable(SOs("HOME"));
#endif
    StringOs clang_path_parts[] = {
        home_path,
    };
    let clang_path = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, clang_path_parts), true);
    let compile_build_argument_builder = os_string_list_builder_create(arena, clang_path);
    os_string_list_builder_append(compile_build_argument_builder, SOs("-o"));
    os_string_list_builder_append(compile_build_argument_builder, SOs("src/build/build.c"));
    let compile_build_arguments = os_string_list_builder_end(compile_build_argument_builder);
    let compile_build_process = os_process_spawn(clang_path, compile_build_arguments, program_state->input.envp);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (compile_build_process)
    {
        result = os_process_wait_sync(compile_build_process, (ProcessResources){});
    }

    if (result == PROCESS_RESULT_SUCCESS)
    {
    }

    return result;
}

