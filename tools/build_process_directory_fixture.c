// Build-driver process cwd regression. The real spawn/wait implementation runs
// this executable as a marker-writing child; cwd capture/entry have fault seams.
#define BUSTER_UNITY_BUILD 1
#include <buster/lib/base.h>
#include <buster/lib/system_headers.h>

static bool capture_failure;
static bool entry_failure;
#if defined(_WIN32)
static DWORD fixture_current_directory(DWORD size, WCHAR* buffer)
{
    DWORD result;
    if (capture_failure)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        result = 0;
    }
    else
    {
        result = GetCurrentDirectoryW(size, buffer);
    }
    return result;
}
static BOOL fixture_set_directory(WCHAR const* directory)
{
    BOOL result;
    if (entry_failure)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        result = FALSE;
    }
    else
    {
        result = SetCurrentDirectoryW(directory);
    }
    return result;
}
#define GetCurrentDirectoryW fixture_current_directory
#define SetCurrentDirectoryW fixture_set_directory
#else
static char* fixture_current_directory(char* buffer, size_t size)
{
    char* result;
    if (capture_failure)
    {
        errno = EACCES;
        result = 0;
    }
    else
    {
        result = getcwd(buffer, size);
    }
    return result;
}
static int fixture_set_directory(char const* directory)
{
    int result;
    if (entry_failure)
    {
        errno = EACCES;
        result = -1;
    }
    else
    {
        result = chdir(directory);
    }
    return result;
}
#define getcwd fixture_current_directory
#define chdir fixture_set_directory
#endif
#define main build_driver_main
#include "../build.c"
#undef main

int main(int argc, char** argv)
{
    int result = 1;
    if (argc == 2 && strcmp(argv[1], "child") == 0)
    {
        FILE* marker = fopen("child-marker", "wb");
        if (marker)
        {
            result = fclose(marker) == 0 ? 0 : 1;
        }
    }
    else if (argc == 3)
    {
        os_state.page_size = os_get_page_size();
        os_state.allocation_granularity = os_state.page_size;
        os_state.logical_thread_count = 1;
#if BUSTER_WINDOWS
        InitializeCriticalSection(&os_state.entity_mutex);
#else
        pthread_mutex_init(&os_state.entity_mutex, 0);
#endif
        os_state.entity_arena = arena_create((ArenaCreation){0});
        program_state->arena = arena_create((ArenaCreation){0});
        thread_context_select(thread_context_allocate());
        Arena* arena = program_state->arena;
        String8 arguments[] = {string_from_pointer(argv[0]), S8("child")};
        ProcessRun run = {.arguments = BUSTER_ARRAY_TO_SLICE(arguments),
                          .working_directory = string_from_pointer(argv[2])};
        capture_failure = strcmp(argv[1], "capture-failure") == 0;
        entry_failure = strcmp(argv[1], "entry-failure") == 0;
        ProcessSpawnResult spawn = process_run_spawn(arena, &run);
        capture_failure = false;
        entry_failure = false;
        bool rejected = strcmp(argv[1], "valid") != 0;
        bool valid = rejected ? !spawn.handle && spawn.failure == PROCESS_SPAWN_FAILURE_WORKING_DIRECTORY && spawn.error.v
                              : spawn.handle && spawn.failure == PROCESS_SPAWN_FAILURE_NONE;
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 10000000);
            valid = valid && wait.result == PROCESS_RESULT_SUCCESS;
        }
        // An unrequested second launch must run in the restored caller cwd.
        if (!rejected && valid)
        {
            run.working_directory = (String8){0};
            spawn = process_run_spawn(arena, &run);
            ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 10000000);
            valid = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS;
        }
        printf("cwd-fixture: rejected=%d failure=%d error=%u\n", rejected, (int)spawn.failure, spawn.error.v);
        result = valid ? 0 : 1;
    }
    return result;
}
