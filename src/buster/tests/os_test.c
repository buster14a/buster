// OS tests cover process, filesystem, and synchronization contracts. os_tests
// registers their focused fixtures; os_test_windows_handle_observation and
// os_test_process_shutdown_exit_windows distinguish owned kernel objects from
// the global handle census. os_test_image_retention exercises the
// test-only generated-image seam and its deletion/whole-tree behavior.
#include <buster/tests/os_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/file.h>
#include <buster/lib/os_internal.h>
#include <buster/lib/time.h>

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
#include <stdio.h>
#include <sys/socket.h>
#endif

// Compile-only GCC/MSVC matrix rows must also enforce the host byte contract.
BUSTER_CT_CHECK((char8)0xff == 0xff);
BUSTER_CT_CHECK(sizeof(u32) == 4 && sizeof(u64) == 8);

enum
{
    OS_TEST_LANE_MAX_COUNT = 8,
    OS_TEST_LANE_ITEM_COUNT = 1003,
    OS_TEST_LANE_INVOCATION_COUNT = 3,
    OS_TEST_NESTED_INVOCATION_COUNT = 2,
};

#if !BUSTER_SINGLE_THREADED
typedef struct OsTestThreadPoolState OsTestThreadPoolState;
struct OsTestThreadPoolState
{
    Arena* pooled_arena;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_thread_pool_entry(void* argument)
{
    OsTestThreadPoolState* state = (OsTestThreadPoolState*)argument;
    Arena* pooled = arena_create((ArenaCreation){0});
    BUSTER_RT_CHECK(pooled != 0);
    state->pooled_arena = pooled;
    BUSTER_RT_CHECK(arena_destroy(pooled, 1));
}

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_resource_noop(void* argument)
{
    BUSTER_UNUSED(argument);
}
#endif
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
typedef struct OsTestEnvironment OsTestEnvironment;
struct OsTestEnvironment
{
    SliceString8 keys;
    SliceString8 values;
};

BUSTER_GLOBAL_LOCAL bool os_test_environment_key_matches(String8 key, String8* overrides, u64 override_count)
{
    bool result = false;
    for (u64 index = 0; index < override_count && !result; index += 1)
    {
        result = string_equal(key, overrides[index]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL OsTestEnvironment os_test_environment(Arena* arena, String8* override_keys, String8* override_values, u64 override_count)
{
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    String8* keys = arena_allocate(arena, String8, inherited_keys.length + override_count);
    String8* values = arena_allocate(arena, String8, inherited_keys.length + override_count);
    u64 count = 0;
    for (u64 index = 0; index < override_count; index += 1)
    {
        keys[count] = override_keys[index];
        values[count] = override_values[index];
        count += 1;
    }
    for (u64 index = 0; index < inherited_keys.length; index += 1)
    {
        if (!os_test_environment_key_matches(inherited_keys.pointer[index], override_keys, override_count))
        {
            keys[count] = inherited_keys.pointer[index];
            values[count] = inherited_values.pointer[index];
            count += 1;
        }
    }
    return (OsTestEnvironment){{keys, count}, {values, count}};
}

BUSTER_GLOBAL_LOCAL void os_test_sleep_milliseconds(u32 milliseconds)
{
#if BUSTER_WINDOWS
    Sleep(milliseconds);
#else
    poll(0, 0, (int)milliseconds);
#endif
}

BUSTER_GLOBAL_LOCAL bool os_test_create_empty_file(String8 path)
{
    OsFileDescriptor* file = os_file_open(
        path,
        (OpenFlags){ .create = 1, .truncate = 1 },
        (OsFileAccess){ .write = 1 },
        (OsFileCreateMode){0},
        (OsFileShareFlags){ .read = 1, .write = 1, .delete = 1 });
    bool result = file != 0;
    if (file)
    {
        result = os_file_close(file) && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_test_regular_file_exists(String8 path)
{
    FileStats stats = os_file_replacement_target_stats(path);
    return stats.valid && stats.kind == OS_FILE_KIND_REGULAR;
}

typedef struct OsTestProcessTreeWait OsTestProcessTreeWait;
struct OsTestProcessTreeWait
{
    ProcessWaitResult waited;
    u32 polls;
    bool ready;
};

BUSTER_GLOBAL_LOCAL OsTestProcessTreeWait os_test_process_tree_wait(Arena* arena, ProcessSpawnResult spawn, String8 ready,
                                                                    u32 poll_limit, u32 poll_milliseconds,
                                                                    u64 timeout_microseconds)
{
    OsTestProcessTreeWait result = {0};
    while (result.polls < poll_limit && !os_test_regular_file_exists(ready))
    {
        os_test_sleep_milliseconds(poll_milliseconds);
        result.polls += 1;
    }
    result.ready = os_test_regular_file_exists(ready);
    result.waited = os_process_wait_deadline(arena, spawn, result.ready ? timeout_microseconds : 1);
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_test_process_tree_show_diagnostic(UnitTestArguments* arguments, String8 phase,
                                                              ProcessSpawnResult spawn, OsError spawn_error,
                                                              OsTestProcessTreeWait tree, String8 ready,
                                                              String8 release, String8 escaped)
{
    String8 standard_output = {
        .pointer = (char8*)tree.waited.streams[STANDARD_STREAM_OUTPUT].pointer,
        .length = tree.waited.streams[STANDARD_STREAM_OUTPUT].length,
    };
    String8 standard_error = {
        .pointer = (char8*)tree.waited.streams[STANDARD_STREAM_ERROR].pointer,
        .length = tree.waited.streams[STANDARD_STREAM_ERROR].length,
    };
    arguments->show(arguments,
        S8("PROCESS_TREE_READINESS_V1 phase={S8} ready={u32} polls={u32} spawn_handle={u32} spawn_group={u32} "
           "spawn_error={u32} result={u32} platform_status={u32} timed_out={u32} termination_requested={u32} "
           "forcibly_terminated={u32} cleanup_failed={u32} ready_path={S8} release_path={S8} escaped_path={S8} "
           "stdout={S8} stderr={S8}\n"),
        phase, (u32)tree.ready, tree.polls, (u32)(spawn.handle != 0), (u32)spawn.process_group, spawn_error.v,
        (u32)tree.waited.result, tree.waited.platform_status, (u32)tree.waited.timed_out,
        (u32)tree.waited.termination_requested, (u32)tree.waited.forcibly_terminated,
        (u32)tree.waited.process_tree_cleanup_failed, ready, release, escaped, standard_output, standard_error);
    return true;
}
#endif

typedef struct OsTestLaneState OsTestLaneState;
struct OsTestLaneState
{
    u64 items[OS_TEST_LANE_ITEM_COUNT];
    u8 taken[OS_TEST_LANE_ITEM_COUNT];
    u64 observed_counts[OS_TEST_LANE_MAX_COUNT];
    u64 sums_after_sync[OS_TEST_LANE_MAX_COUNT];
    u64 broadcast_received[OS_TEST_LANE_MAX_COUNT];
    u64 scratch_positions[OS_TEST_LANE_INVOCATION_COUNT][OS_TEST_LANE_MAX_COUNT];
    u64 scratch_committed[OS_TEST_LANE_INVOCATION_COUNT][OS_TEST_LANE_MAX_COUNT];
    ThreadContext* observed_contexts[OS_TEST_LANE_INVOCATION_COUNT][OS_TEST_LANE_MAX_COUNT];
    AtomicU64 sum;
    AtomicU64 take_index;
    u64 invocation;
};

// Runs once on every lane. Test macros are not thread-safe, so lanes only
// record what they observe; the caller checks after the gang has finished.
BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_lane_gang(void* argument)
{
    OsTestLaneState* state = (OsTestLaneState*)argument;
    u64 index = lane_index();
    state->observed_counts[index] = lane_count();
    ThreadContext* thread_context = thread_context_selected();
    state->observed_contexts[state->invocation][index] = thread_context;
    state->scratch_positions[state->invocation][index] = thread_context->arenas[0]->position;
    state->scratch_committed[state->invocation][index] = thread_context->arenas[0]->os_position;
    if (index)
    {
        arena_allocate(thread_context->arenas[0], u8, 64 + index);
    }
    if (state->invocation == 0 && index == 1)
    {
        // The resident context remains, but an outlier scratch commitment must
        // not remain resident across later dispatches.
        arena_allocate(thread_context->arenas[0], u8, BUSTER_MB(17));
    }

    LaneRange range = lane_range(OS_TEST_LANE_ITEM_COUNT);
    u64 local_sum = 0;
    for (u64 i = range.start; i < range.end; i += 1)
    {
        local_sum += state->items[i];
    }
    atomic_u64_add(&state->sum, local_sum);
    lane_sync();
    state->sums_after_sync[index] = state->sum;

    u64 broadcast_value = 0;
    if (index == 0)
    {
        broadcast_value = 0x1234567890abcdefull;
    }
    lane_broadcast(&broadcast_value, sizeof(broadcast_value), 0);
    state->broadcast_received[index] = broadcast_value;

    for (;;)
    {
        u64 item = atomic_u64_increment(&state->take_index);
        if (item >= OS_TEST_LANE_ITEM_COUNT)
        {
            break;
        }
        state->taken[item] += 1;
    }
}

#if !BUSTER_SINGLE_THREADED
typedef struct OsTestThreadLivenessState OsTestThreadLivenessState;
struct OsTestThreadLivenessState
{
    AtomicU64 started;
    AtomicU64 release;
    u64 worker_saw_only_live_thread;
};

// Parks until released so the caller can observe the process while this thread
// is provably still running.
// A created thread compiles like the main thread (driver test gangs, the
// -fcompile-jobs lanes), so it must hold a frame far beyond Apple's 512 KiB
// secondary-thread default. Touch one byte per page of a 2 MiB frame.
#define OS_TEST_THREAD_STACK_FRAME_BYTES BUSTER_MB(2)
#define OS_TEST_THREAD_STACK_PAGE_BYTES 4096

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_thread_large_frame(void* argument)
{
    u64* checksum = (u64*)argument;
    volatile u8 frame[OS_TEST_THREAD_STACK_FRAME_BYTES];
    for (u64 offset = 0; offset < OS_TEST_THREAD_STACK_FRAME_BYTES; offset += OS_TEST_THREAD_STACK_PAGE_BYTES)
    {
        frame[offset] = (u8)(offset / OS_TEST_THREAD_STACK_PAGE_BYTES);
    }
    u64 sum = 0;
    for (u64 offset = 0; offset < OS_TEST_THREAD_STACK_FRAME_BYTES; offset += OS_TEST_THREAD_STACK_PAGE_BYTES)
    {
        sum += frame[offset];
    }
    *checksum = sum;
}

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_thread_liveness(void* argument)
{
    OsTestThreadLivenessState* state = (OsTestThreadLivenessState*)argument;
    state->worker_saw_only_live_thread = os_is_only_live_thread() ? 1 : 0;
    atomic_u64_increment(&state->started);
    while (!state->release)
    {
    }
}

#define OS_TEST_DEBUGGER_THREAD_COUNT 4
#define OS_TEST_DEBUGGER_ROUNDS 16
#define OS_TEST_DEBUGGER_QUERIES 64

typedef struct OsTestDebuggerState OsTestDebuggerState;
struct OsTestDebuggerState
{
    AtomicU64 ready;
    AtomicU64 start;
    AtomicU64 disagreements;
    u64 expected;
};

// Every thread released together asks the cold failure-path question against an
// unprobed cache. Whichever thread loses the race to publish must still return
// the same answer as the one that won, never a half-written one.
BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_thread_debugger(void* argument)
{
    OsTestDebuggerState* state = (OsTestDebuggerState*)argument;
    atomic_u64_increment(&state->ready);
    while (!atomic_u64_load(&state->start))
    {
    }
    for (u32 query = 0; query < OS_TEST_DEBUGGER_QUERIES; query += 1)
    {
        if ((u64)is_debugger_present() != state->expected)
        {
            atomic_u64_increment(&state->disagreements);
        }
    }
}
#endif

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS && !BUSTER_SINGLE_THREADED
typedef struct OsTestDirectoryDeleteRaceState OsTestDirectoryDeleteRaceState;
struct OsTestDirectoryDeleteRaceState
{
    String8 child;
    String8 parked;
    String8 outside;
    AtomicU64 start;
    AtomicU64 stop;
    AtomicU64 swaps;
    u64 limit;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_directory_delete_race(void* argument)
{
    OsTestDirectoryDeleteRaceState* state = (OsTestDirectoryDeleteRaceState*)argument;
    while (!state->start)
    {
    }
    while (!state->stop && state->swaps < state->limit)
    {
        if (rename((const char*)state->child.pointer, (const char*)state->parked.pointer) == 0)
        {
            if (symlink((const char*)state->outside.pointer, (const char*)state->child.pointer) == 0)
            {
                atomic_u64_increment(&state->swaps);
                poll(0, 0, 1);
                (void)unlink((const char*)state->child.pointer);
            }
            (void)rename((const char*)state->parked.pointer, (const char*)state->child.pointer);
            poll(0, 0, 1);
        }
        else
        {
            (void)unlink((const char*)state->child.pointer);
            (void)rename((const char*)state->parked.pointer, (const char*)state->child.pointer);
        }
    }
    (void)unlink((const char*)state->child.pointer);
    (void)rename((const char*)state->parked.pointer, (const char*)state->child.pointer);
}
#endif

typedef struct OsTestNestedLaneState OsTestNestedLaneState;
struct OsTestNestedLaneState
{
    u64 outer_counts[OS_TEST_LANE_MAX_COUNT];
    u64 inner_counts[OS_TEST_LANE_MAX_COUNT];
    Arena* inner_pooled_arenas[OS_TEST_NESTED_INVOCATION_COUNT][OS_TEST_LANE_MAX_COUNT];
    u64 inner_lane_count;
    u64 invocation;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_inner_lane_gang(void* argument)
{
    OsTestNestedLaneState* state = (OsTestNestedLaneState*)argument;
    u64 index = lane_index();
    state->inner_counts[index] = lane_count();
    if (index)
    {
        // The fresh nested worker will park this arena beside its context
        // arenas. Generic OS-thread teardown must drain all of them before
        // the TLS pool root disappears.
        Arena* pooled = arena_create((ArenaCreation){0});
        BUSTER_RT_CHECK(pooled != 0);
        state->inner_pooled_arenas[state->invocation][index] = pooled;
        BUSTER_RT_CHECK(arena_destroy(pooled, 1));
    }
}

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_outer_lane_gang(void* argument)
{
    OsTestNestedLaneState* state = (OsTestNestedLaneState*)argument;
    u64 outer_index = lane_index();
    lane_sync();
    if (outer_index == 0)
    {
        lane_run(state->inner_lane_count, &os_test_inner_lane_gang, state);
    }
    lane_sync();
    state->outer_counts[outer_index] = lane_count();
}

#if (BUSTER_LINUX || BUSTER_MACOS || (BUSTER_WINDOWS && !defined(__TINYC__))) && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool os_test_thread_name_get(Arena* arena, String8* name)
{
    bool result = false;
    *name = (String8){0};
#if BUSTER_LINUX || BUSTER_MACOS
    char8 buffer[128] = {0};
    int status = pthread_getname_np(pthread_self(), buffer, sizeof(buffer));
    if (status == 0)
    {
        u64 length = 0;
        while (length < sizeof(buffer) && buffer[length])
        {
            length += 1;
        }
        result = length < sizeof(buffer);
        if (result)
        {
            *name = string_duplicate_arena(arena, (String8){.pointer = buffer, .length = length}, false);
        }
    }
#elif BUSTER_WINDOWS
    PWSTR description = 0;
    HRESULT status = GetThreadDescription(GetCurrentThread(), &description);
    result = SUCCEEDED(status);
    if (result)
    {
        u64 length = 0;
        while (description && description[length])
        {
            length += 1;
        }
        *name = string8_from_string16(arena, (String16){.pointer = (char16*)description, .length = length}, false);
    }
    if (description)
    {
        LocalFree(description);
    }
#endif
    return result;
}
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool os_process_spawn_test_released(ProcessSpawnResult spawn)
{
    bool result = spawn.handle == 0;
    for (u32 stream = 0; result && stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        for (u32 side = 0; result && side < 2; side += 1)
        {
            result = spawn.pipes[stream][side] == 0;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessWaitResult os_process_spawn_test_wait(Arena* arena, ProcessSpawnResult spawn)
{
    ProcessWaitResult result = {.result = PROCESS_RESULT_NOT_EXISTENT};
    if (spawn.handle)
    {
        result = os_process_wait_sync(arena, spawn);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_process_spawn_contract_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 self_arguments[] = {program_state->input.arguments.pointer[0], S8("help")};

    ProcessSpawnResult empty_arguments = os_process_spawn((SliceString8){0}, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){0});
    BUSTER_TEST(arguments, empty_arguments.failure == PROCESS_SPAWN_FAILURE_INVALID_ARGUMENTS && empty_arguments.error.v != 0 &&
                               os_process_spawn_test_released(empty_arguments));

    char8 embedded_nul_bytes[] = {'x', 0, 'y'};
    String8 embedded_nul_arguments[] = {{.pointer = embedded_nul_bytes, .length = BUSTER_ARRAY_LENGTH(embedded_nul_bytes)}};
    ProcessSpawnResult embedded_nul = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(embedded_nul_arguments), (SliceString8){0},
                                                       (SliceString8){0}, (ProcessSpawnOptions){0});
    BUSTER_TEST(arguments, embedded_nul.failure == PROCESS_SPAWN_FAILURE_INVALID_ARGUMENTS && embedded_nul.error.v != 0 &&
                               os_process_spawn_test_released(embedded_nul));

    String8 one_key[] = {S8("KEY")};
    ProcessSpawnResult mismatched_environment = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(one_key), (SliceString8){0},
                                                                 (ProcessSpawnOptions){0});
    BUSTER_TEST(arguments, mismatched_environment.failure == PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT &&
                               mismatched_environment.error.v != 0 && os_process_spawn_test_released(mismatched_environment));

    String8 invalid_keys[] = {S8("BAD=KEY")};
    String8 invalid_values[] = {S8("value")};
    ProcessSpawnResult invalid_environment = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                              (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_keys),
                                                              (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_values), (ProcessSpawnOptions){0});
    BUSTER_TEST(arguments, invalid_environment.failure == PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT && invalid_environment.error.v != 0 &&
                               os_process_spawn_test_released(invalid_environment));

    char8 embedded_nul_key_bytes[] = {'K', 0, 'Y'};
    String8 embedded_nul_keys[] = {{.pointer = embedded_nul_key_bytes, .length = BUSTER_ARRAY_LENGTH(embedded_nul_key_bytes)}};
    ProcessSpawnResult embedded_nul_key = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                          (SliceString8)BUSTER_ARRAY_TO_SLICE(embedded_nul_keys),
                                                          (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_values), (ProcessSpawnOptions){0});
    BUSTER_TEST(arguments, embedded_nul_key.failure == PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT && embedded_nul_key.error.v != 0 &&
                               os_process_spawn_test_released(embedded_nul_key));

    char8 embedded_nul_value_bytes[] = {'v', 0, 'x'};
    String8 embedded_nul_values[] = {{.pointer = embedded_nul_value_bytes, .length = BUSTER_ARRAY_LENGTH(embedded_nul_value_bytes)}};
    ProcessSpawnResult embedded_nul_value = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(one_key),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(embedded_nul_values), (ProcessSpawnOptions){0});
    BUSTER_TEST(arguments, embedded_nul_value.failure == PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT && embedded_nul_value.error.v != 0 &&
                               os_process_spawn_test_released(embedded_nul_value));

    String8 empty_key[] = {{0}};
    ProcessSpawnResult empty_environment_key = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                               (SliceString8)BUSTER_ARRAY_TO_SLICE(empty_key),
                                                               (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_values), (ProcessSpawnOptions){0});
    BUSTER_TEST(arguments, empty_environment_key.failure == PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT &&
                               empty_environment_key.error.v != 0 && os_process_spawn_test_released(empty_environment_key));

    ProcessSpawnResult conflicting_environment = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(one_key),
                                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_values),
                                                                 (ProcessSpawnOptions){.use_process_environment = true});
    BUSTER_TEST(arguments, conflicting_environment.failure == PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT &&
                               conflicting_environment.error.v != 0 && os_process_spawn_test_released(conflicting_environment));

    String8 environment_keys[] = {S8("BUSTER_OS_SPAWN_PROBE"), S8("BUSTER_OS_SPAWN_EXPECTED"), S8("PATH")};
    String8 environment_values[] = {S8("environment"), S8("present"), S8("hostile-path")};
    ProcessSpawnResult exact_environment = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_keys),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_values),
                                                            (ProcessSpawnOptions){
                                                                .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) |
                                                                           ((u64)1 << STANDARD_STREAM_ERROR),
                                                            });
    ProcessWaitResult exact_environment_wait = os_process_spawn_test_wait(arena, exact_environment);
    BUSTER_TEST(arguments, exact_environment.failure == PROCESS_SPAWN_FAILURE_NONE && exact_environment.error.v == 0 &&
                               exact_environment_wait.result == PROCESS_RESULT_SUCCESS);

#if BUSTER_WINDOWS
    String8 path_search_arguments[] = {S8("cmd.exe"), S8("/d"), S8("/c"), S8("exit 0")};
#else
    String8 path_search_arguments[] = {S8("sh"), S8("-c"), S8("exit 0")};
#endif
    ProcessSpawnResult no_search = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(path_search_arguments), (SliceString8){0},
                                                    (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
    BUSTER_TEST(arguments, !no_search.handle && no_search.failure == PROCESS_SPAWN_FAILURE_SPAWN && no_search.error.v != 0 &&
                               os_process_spawn_test_released(no_search));

    ProcessSpawnResult searched = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(path_search_arguments), (SliceString8){0}, (SliceString8){0},
                                                   (ProcessSpawnOptions){.use_process_environment = true, .search_path = true});
    ProcessWaitResult searched_wait = os_process_spawn_test_wait(arena, searched);
    BUSTER_TEST(arguments, searched.failure == PROCESS_SPAWN_FAILURE_NONE && searched_wait.result == PROCESS_RESULT_SUCCESS);

    String8 descriptor_value = {0};
#if BUSTER_WINDOWS
    // Handle values are per process, and the child's own runtime (notably the
    // sanitizer runtime) can hold an unrelated handle with the same value. Name
    // the event so the child can prove object identity instead of assuming a
    // valid handle value was inherited.
    String8 descriptor_name = string_format(arena, S8("Local\\buster-os-spawn-probe-{u32}"), (u32)GetCurrentProcessId());
    String16 descriptor_name16 = string16_from_string8(arena, descriptor_name, true);
    SECURITY_ATTRIBUTES security_attributes = {sizeof(security_attributes), 0, TRUE};
    HANDLE unrelated = CreateEventW(&security_attributes, TRUE, FALSE, (LPCWSTR)descriptor_name16.pointer);
    bool unrelated_created = unrelated != 0;
    if (unrelated_created)
    {
        descriptor_value = string_format(arena, S8("{u64}"), (u64)(uintptr_t)unrelated);
    }
#else
    int unrelated[2] = {-1, -1};
    bool unrelated_created = pipe(unrelated) == 0;
    if (unrelated_created)
    {
        descriptor_value = string_format(arena, S8("{u64}"), (u64)unrelated[0]);
    }
#endif
    BUSTER_TEST(arguments, unrelated_created);
    if (unrelated_created)
    {
#if BUSTER_WINDOWS
        String8 descriptor_keys[] = {S8("BUSTER_OS_SPAWN_PROBE"), S8("BUSTER_OS_SPAWN_PROBE_VALUE"), S8("BUSTER_OS_SPAWN_PROBE_NAME")};
        String8 descriptor_values[] = {S8("descriptor"), descriptor_value, descriptor_name};
#else
        String8 descriptor_keys[] = {S8("BUSTER_OS_SPAWN_PROBE"), S8("BUSTER_OS_SPAWN_PROBE_VALUE")};
        String8 descriptor_values[] = {S8("descriptor"), descriptor_value};
#endif
        ProcessSpawnResult descriptor_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                               (SliceString8)BUSTER_ARRAY_TO_SLICE(descriptor_keys),
                                                               (SliceString8)BUSTER_ARRAY_TO_SLICE(descriptor_values), (ProcessSpawnOptions){0});
        ProcessWaitResult descriptor_wait = os_process_spawn_test_wait(arena, descriptor_spawn);
        BUSTER_TEST(arguments, descriptor_spawn.failure == PROCESS_SPAWN_FAILURE_NONE && descriptor_wait.result == PROCESS_RESULT_SUCCESS);
#if BUSTER_WINDOWS
        CloseHandle(unrelated);
#else
        close(unrelated[0]);
        close(unrelated[1]);
#endif
    }

#if !BUSTER_WINDOWS
    // A descriptor from os_file_open_checked must be close-on-exec from creation. Spawn
    // sanitizes inherited descriptors by enumeration or a child-side close-from, and neither
    // is atomic with a concurrent open, so the flag is the contract that closes the window.
    String8 cloexec_path = buster_test_temporary_path(arena, S8("spawn-cloexec"), S8(".bin"));
    OsFileOpenResult cloexec_open = os_file_open_checked(cloexec_path, (OpenFlags){.create = 1, .truncate = 1}, (OsFileAccess){.read = 1, .write = 1},
                                                          (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
    BUSTER_TEST(arguments, cloexec_open.file != 0 && !cloexec_open.error.v);
    if (cloexec_open.file)
    {
        int cloexec_descriptor = generic_fd_to_posix(cloexec_open.file);
        int cloexec_flags = fcntl(cloexec_descriptor, F_GETFD);
        BUSTER_TEST(arguments, cloexec_flags >= 0 && (cloexec_flags & FD_CLOEXEC) != 0);

        String8 cloexec_value = string_format(arena, S8("{u64}"), (u64)cloexec_descriptor);
        String8 cloexec_keys[] = {S8("BUSTER_OS_SPAWN_PROBE"), S8("BUSTER_OS_SPAWN_PROBE_VALUE")};
        String8 cloexec_values[] = {S8("descriptor"), cloexec_value};
        ProcessSpawnResult cloexec_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(cloexec_keys),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(cloexec_values), (ProcessSpawnOptions){0});
        ProcessWaitResult cloexec_wait = os_process_spawn_test_wait(arena, cloexec_spawn);
        BUSTER_TEST(arguments, cloexec_spawn.failure == PROCESS_SPAWN_FAILURE_NONE && cloexec_wait.result == PROCESS_RESULT_SUCCESS);
        BUSTER_TEST(arguments, !os_file_close_checked(cloexec_open.file).v);
        BUSTER_TEST(arguments, !os_file_delete_checked(cloexec_path).v);
    }
#endif

    for (u32 mask = 0; mask < ((u32)1 << STANDARD_STREAM_COUNT); mask += 1)
    {
        String8 mask_value = string_format(arena, S8("{u32}"), mask);
        String8 closed_keys[] = {S8("BUSTER_OS_SPAWN_PROBE"), S8("BUSTER_OS_SPAWN_STDIO_MASK")};
        String8 closed_values[] = {S8("closed-stdio"), mask_value};
        ProcessSpawnResult closed_stdio = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                           (SliceString8)BUSTER_ARRAY_TO_SLICE(closed_keys),
                                                           (SliceString8)BUSTER_ARRAY_TO_SLICE(closed_values), (ProcessSpawnOptions){0});
        ProcessWaitResult closed_stdio_wait = os_process_spawn_test_wait(arena, closed_stdio);
        BUSTER_TEST(arguments, closed_stdio.failure == PROCESS_SPAWN_FAILURE_NONE &&
                                   closed_stdio_wait.result == PROCESS_RESULT_SUCCESS);
    }

#if BUSTER_WINDOWS
    (void)arena_allocate(arena, char16, 0);
    u64 environment_position = arena->position;
    char16 expected_empty[] = {0, 0};
    char16 expected_one[] = {'A', '=', '1', 0, 0};
    char16 expected_two[] = {'A', '=', '1', 0, 'B', '=', '2', 0, 0};
    String8 one_keys[] = {S8("A")};
    String8 one_values[] = {S8("1")};
    String8 two_keys[] = {S8("A"), S8("B")};
    String8 two_values[] = {S8("1"), S8("2")};
    String8 one_entry[] = {S8("A=1")};
    String8 two_entries[] = {S8("A=1"), S8("B=2")};

    char16* dirty = arena_allocate(arena, char16, BUSTER_ARRAY_LENGTH(expected_empty));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_empty); index += 1) dirty[index] = (char16)0xa5a5;
    arena_set_position(arena, environment_position);
    WindowsStringList empty_key_value = windows_environment_from_keys_and_values(arena, (SliceString8){0}, (SliceString8){0});
    BUSTER_TEST(arguments, arena->position - environment_position == sizeof(expected_empty) &&
                               !memcmp(empty_key_value, expected_empty, sizeof(expected_empty)));

    arena_set_position(arena, environment_position);
    WindowsStringList one_key_value = windows_environment_from_keys_and_values(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(one_keys), (SliceString8)BUSTER_ARRAY_TO_SLICE(one_values));
    BUSTER_TEST(arguments, arena->position - environment_position == sizeof(expected_one) &&
                               !memcmp(one_key_value, expected_one, sizeof(expected_one)));

    arena_set_position(arena, environment_position);
    WindowsStringList two_key_values = windows_environment_from_keys_and_values(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(two_keys), (SliceString8)BUSTER_ARRAY_TO_SLICE(two_values));
    BUSTER_TEST(arguments, arena->position - environment_position == sizeof(expected_two) &&
                               !memcmp(two_key_values, expected_two, sizeof(expected_two)));

    arena_set_position(arena, environment_position);
    dirty = arena_allocate(arena, char16, BUSTER_ARRAY_LENGTH(expected_empty));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_empty); index += 1) dirty[index] = (char16)0xa5a5;
    arena_set_position(arena, environment_position);
    WindowsStringList empty_block = windows_environment_block_from_slice_string(arena, (SliceString8){0});
    BUSTER_TEST(arguments, arena->position - environment_position == sizeof(expected_empty) &&
                               !memcmp(empty_block, expected_empty, sizeof(expected_empty)));

    arena_set_position(arena, environment_position);
    WindowsStringList one_block = windows_environment_block_from_slice_string(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(one_entry));
    BUSTER_TEST(arguments, arena->position - environment_position == sizeof(expected_one) &&
                               !memcmp(one_block, expected_one, sizeof(expected_one)));

    arena_set_position(arena, environment_position);
    WindowsStringList two_blocks = windows_environment_block_from_slice_string(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(two_entries));
    BUSTER_TEST(arguments, arena->position - environment_position == sizeof(expected_two) &&
                               !memcmp(two_blocks, expected_two, sizeof(expected_two)));
    arena_set_position(arena, environment_position);
#endif

#if BUSTER_WINDOWS
    OsProcessSpawnTestOperation operations[] = {
        OS_PROCESS_SPAWN_TEST_PIPE,
        OS_PROCESS_SPAWN_TEST_PIPE_CONFIGURATION,
        OS_PROCESS_SPAWN_TEST_HANDLE_DUPLICATION,
        OS_PROCESS_SPAWN_TEST_HANDLE_LIST,
        OS_PROCESS_SPAWN_TEST_SPAWN,
        OS_PROCESS_SPAWN_TEST_OWNER_ALLOCATION,
        OS_PROCESS_SPAWN_TEST_JOB_ASSIGNMENT,
        OS_PROCESS_SPAWN_TEST_RESUME,
    };
    ProcessSpawnFailure failures[] = {
        PROCESS_SPAWN_FAILURE_PIPE,
        PROCESS_SPAWN_FAILURE_PIPE_CONFIGURATION,
        PROCESS_SPAWN_FAILURE_HANDLE_DUPLICATION,
        PROCESS_SPAWN_FAILURE_HANDLE_LIST,
        PROCESS_SPAWN_FAILURE_SPAWN,
        PROCESS_SPAWN_FAILURE_OWNER_ALLOCATION,
        PROCESS_SPAWN_FAILURE_SPAWN,
        PROCESS_SPAWN_FAILURE_SPAWN,
    };
#else
    OsProcessSpawnTestOperation operations[] = {
        OS_PROCESS_SPAWN_TEST_FILE_ACTIONS_INIT,
        OS_PROCESS_SPAWN_TEST_ATTRIBUTES_INIT,
        OS_PROCESS_SPAWN_TEST_PIPE,
        OS_PROCESS_SPAWN_TEST_PIPE_CONFIGURATION,
        OS_PROCESS_SPAWN_TEST_FILE_ACTION,
        OS_PROCESS_SPAWN_TEST_ATTRIBUTE,
        OS_PROCESS_SPAWN_TEST_SPAWN,
    };
    ProcessSpawnFailure failures[] = {
        PROCESS_SPAWN_FAILURE_FILE_ACTIONS_INIT,
        PROCESS_SPAWN_FAILURE_ATTRIBUTES_INIT,
        PROCESS_SPAWN_FAILURE_PIPE,
        PROCESS_SPAWN_FAILURE_PIPE_CONFIGURATION,
        PROCESS_SPAWN_FAILURE_FILE_ACTION,
        PROCESS_SPAWN_FAILURE_ATTRIBUTE,
        PROCESS_SPAWN_FAILURE_SPAWN,
    };
#endif
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(operations) == BUSTER_ARRAY_LENGTH(failures));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(operations); index += 1)
    {
        u64 resources_before = os_process_spawn_test_resource_count();
        os_process_spawn_test_fail_on_call(operations[index], 0);
        ProcessSpawnResult injected = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                       (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_keys),
                                                       (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_values),
                                                       (ProcessSpawnOptions){
                                                           .capture = (u64)1 << STANDARD_STREAM_OUTPUT,
                                                           .new_process_group = true,
                                                       });
        bool injection_consumed = os_process_spawn_test_end();
        u64 resources_after = os_process_spawn_test_resource_count();
        BUSTER_TEST(arguments, injection_consumed);
        BUSTER_TEST(arguments, injected.failure == failures[index] && injected.error.v != 0 && os_process_spawn_test_released(injected));
        BUSTER_TEST(arguments, resources_before == resources_after);
    }

    ProcessSpawnResult after_faults = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(self_arguments),
                                                       (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_keys),
                                                       (SliceString8)BUSTER_ARRAY_TO_SLICE(environment_values), (ProcessSpawnOptions){0});
    ProcessWaitResult after_faults_wait = os_process_spawn_test_wait(arena, after_faults);
    BUSTER_TEST(arguments, after_faults.failure == PROCESS_SPAWN_FAILURE_NONE && after_faults_wait.result == PROCESS_RESULT_SUCCESS);

    return result;
}
#endif

// Refusal probes run inside a separately timed process. An accidentally
// admitted child inherits that outer group/job; it never creates containment
// that could escape the outer deadline while a full sink blocks its waiter.
#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
enum { OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES = 128 };

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_sink_refusals(UnitTestArguments* arguments,
    OsFileDescriptor* sink, OsFileDescriptor* regular, String8 marker, u32 expected_error)
{
    UnitTestResult result = {0};
    String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_OS_CAPTURE_SINK_MARKER"), S8("BUSTER_TEST_JOBS")};
    String8 values[] = {S8("capture-sink-marker"), marker, S8("1")};
    OsTestEnvironment environment = os_test_environment(arguments->arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
    String8 child[] = {program_state->input.arguments.pointer[0], S8("test")};
    u64 resources = os_process_spawn_test_resource_count();
    BUSTER_TEST(arguments, resources != UINT64_MAX);
    for (u32 shape = 0; shape < 3; shape += 1)
    {
        ProcessSpawnOptions options = {.capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE};
        if (shape == 0) options.capture = (u64)1 << STANDARD_STREAM_OUTPUT;
        else if (shape == 1) options.capture = (u64)1 << STANDARD_STREAM_ERROR;
        else options.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR);
        options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = 1;
        options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = 1;
        options.capture_limits.total = 2;
        options.capture_overflow_files[STANDARD_STREAM_OUTPUT] = shape == 2 ? regular : sink;
        options.capture_overflow_files[STANDARD_STREAM_ERROR] = sink;
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child), environment.keys, environment.values, options);
        BUSTER_TEST(arguments, spawn.failure == PROCESS_SPAWN_FAILURE_CAPTURE_SINK && spawn.error.v == expected_error);
        BUSTER_TEST(arguments, os_process_spawn_test_released(spawn) && !spawn.process_tree && !spawn.process_group);
        if (spawn.handle)
        {
            // A mutant with no admission guard may block here on the filled
            // pipe. The independently timed outer owner kills this whole group.
            os_process_wait_deadline(arguments->arena, spawn, 1000000);
        }
        BUSTER_TEST(arguments, !os_test_regular_file_exists(marker));
        BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_sink_admission_child(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 marker = os_get_environment_variable(S8("BUSTER_OS_CAPTURE_SINK_MARKER"));
    String8 regular_path = string_format_z(arena, S8("{S8}.regular"), marker);
    OsFileDescriptor* regular = marker.length ? os_file_open(regular_path, (OpenFlags){.create = 1, .truncate = 1}, (OsFileAccess){.write = 1},
                                                            (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1, .write = 1, .delete = 1}) : 0;
    if (BUSTER_REQUIRE(arguments, regular != 0))
    {
#if BUSTER_WINDOWS
        u32 invalid_argument = ERROR_INVALID_PARAMETER;
        HANDLE read_pipe = 0;
        HANDLE write_pipe = 0;
        if (BUSTER_REQUIRE(arguments, CreatePipe(&read_pipe, &write_pipe, 0, 0) != 0))
        {
            DWORD before = 0;
            BUSTER_TEST(arguments, GetHandleInformation(write_pipe, &before) != 0);
            UnitTestResult probe = os_test_capture_sink_refusals(arguments, (OsFileDescriptor*)write_pipe, regular, marker, invalid_argument);
            result.test_count += probe.test_count;
            result.succeeded_test_count += probe.succeeded_test_count;
            DWORD after = 0;
            BUSTER_TEST(arguments, GetHandleInformation(write_pipe, &after) != 0 && after == before && GetFileType(write_pipe) == FILE_TYPE_PIPE);
            u8 sent = 0xA5;
            u8 received = 0;
            DWORD transferred = 0;
            BUSTER_TEST(arguments, WriteFile(write_pipe, &sent, 1, &transferred, 0) != 0 && transferred == 1);
            BUSTER_TEST(arguments, ReadFile(read_pipe, &received, 1, &transferred, 0) != 0 && transferred == 1 && received == sent);
            BUSTER_TEST(arguments, CloseHandle(read_pipe) != 0);
            BUSTER_TEST(arguments, CloseHandle(write_pipe) != 0);
        }
        HANDLE character = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        if (BUSTER_REQUIRE(arguments, character != INVALID_HANDLE_VALUE))
        {
            UnitTestResult probe = os_test_capture_sink_refusals(arguments, (OsFileDescriptor*)character, regular, marker, invalid_argument);
            result.test_count += probe.test_count;
            result.succeeded_test_count += probe.succeeded_test_count;
            BUSTER_TEST(arguments, GetFileType(character) == FILE_TYPE_CHAR && CloseHandle(character) != 0);
        }
        UnitTestResult invalid = os_test_capture_sink_refusals(arguments, (OsFileDescriptor*)INVALID_HANDLE_VALUE, regular, marker, ERROR_INVALID_HANDLE);
        result.test_count += invalid.test_count;
        result.succeeded_test_count += invalid.succeeded_test_count;
        HANDLE directory = CreateFileW(L".", FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
        if (BUSTER_REQUIRE(arguments, directory != INVALID_HANDLE_VALUE))
        {
            UnitTestResult probe = os_test_capture_sink_refusals(arguments, (OsFileDescriptor*)directory, regular, marker, invalid_argument);
            result.test_count += probe.test_count;
            result.succeeded_test_count += probe.succeeded_test_count;
            BUSTER_TEST(arguments, CloseHandle(directory) != 0);
        }
#else
        u32 invalid_argument = EINVAL;
        int pipe_fds[2] = {-1, -1};
        if (BUSTER_REQUIRE(arguments, pipe(pipe_fds) == 0))
        {
            int write_flags = fcntl(pipe_fds[1], F_GETFL);
            int write_descriptor_flags = fcntl(pipe_fds[1], F_GETFD);
            int read_flags = fcntl(pipe_fds[0], F_GETFL);
            struct stat before = {0};
            bool configured = write_flags >= 0 && write_descriptor_flags >= 0 && read_flags >= 0 &&
                              fstat(pipe_fds[1], &before) == 0 && fcntl(pipe_fds[1], F_SETFL, write_flags | O_NONBLOCK) == 0;
            u8 fill[4096];
            for (u32 index = 0; index < sizeof(fill); index += 1) fill[index] = 0xA5;
            u64 filled = 0;
            bool stalled = false;
            for (u32 attempt = 0; configured && !stalled && attempt < 1024; attempt += 1)
            {
                ssize_t count = write(pipe_fds[1], fill, sizeof(fill));
                if (count > 0) filled += (u64)count;
                else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) stalled = true;
                else if (count >= 0 || errno != EINTR) configured = false;
            }
            // An atomic 4096-byte write can refuse while a smaller spill still
            // fits. Exhaust the remaining capacity one byte at a time too.
            bool full = false;
            for (u32 attempt = 0; configured && stalled && !full && attempt < sizeof(fill) + 1; attempt += 1)
            {
                ssize_t count = write(pipe_fds[1], fill, 1);
                if (count == 1) filled += 1;
                else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) full = true;
                else if (count >= 0 || errno != EINTR) configured = false;
            }
            bool restored = write_flags >= 0 && fcntl(pipe_fds[1], F_SETFL, write_flags) == 0;
            // Filling the pipe can set kernel bookkeeping flags. Snapshot the
            // restored blocking handle immediately before admission begins.
            int admission_write_flags = fcntl(pipe_fds[1], F_GETFL);
            int admission_write_descriptor_flags = fcntl(pipe_fds[1], F_GETFD);
            int restored_flags_mask = O_ACCMODE | O_APPEND | O_ASYNC | O_SYNC | O_DSYNC | O_NONBLOCK;
            if (BUSTER_REQUIRE(arguments, configured && full && filled && restored && admission_write_flags >= 0 &&
                                          !(admission_write_flags & O_NONBLOCK) && admission_write_descriptor_flags == write_descriptor_flags &&
                                          (admission_write_flags & restored_flags_mask) == (write_flags & restored_flags_mask)))
            {
                OsFileDescriptor* sink = (OsFileDescriptor*)(u64)(pipe_fds[1] + 1);
                UnitTestResult probe = os_test_capture_sink_refusals(arguments, sink, regular, marker, invalid_argument);
                result.test_count += probe.test_count;
                result.succeeded_test_count += probe.succeeded_test_count;
                struct stat after = {0};
                BUSTER_TEST(arguments, fstat(pipe_fds[1], &after) == 0 && before.st_dev == after.st_dev && before.st_ino == after.st_ino);
                BUSTER_TEST(arguments, fcntl(pipe_fds[1], F_GETFL) == admission_write_flags &&
                                       fcntl(pipe_fds[1], F_GETFD) == admission_write_descriptor_flags);
                if (BUSTER_REQUIRE(arguments, fcntl(pipe_fds[0], F_SETFL, read_flags | O_NONBLOCK) == 0))
                {
                    u64 drained = 0;
                    bool intact = true;
                    while (intact && drained < filled)
                    {
                        ssize_t count = read(pipe_fds[0], fill, sizeof(fill));
                        if (count > 0)
                        {
                            for (ssize_t index = 0; index < count; index += 1) intact = fill[index] == 0xA5 && intact;
                            drained += (u64)count;
                        }
                        else if (count >= 0 || errno != EINTR) intact = false;
                    }
                    BUSTER_TEST(arguments, intact && drained == filled);
                    BUSTER_TEST(arguments, fcntl(pipe_fds[0], F_SETFL, read_flags) == 0);
                }
            }
            BUSTER_TEST(arguments, close(pipe_fds[0]) == 0);
            BUSTER_TEST(arguments, close(pipe_fds[1]) == 0);
        }
        String8 fifo_path = string_format_z(arena, S8("{S8}.fifo"), marker);
        if (BUSTER_REQUIRE(arguments, mkfifo((char*)fifo_path.pointer, 0600) == 0))
        {
            int fifo = open((char*)fifo_path.pointer, O_RDWR | O_NONBLOCK);
            if (BUSTER_REQUIRE(arguments, fifo >= 0))
            {
                int flags = fcntl(fifo, F_GETFL);
                struct stat before = {0};
                BUSTER_TEST(arguments, flags >= 0 && fstat(fifo, &before) == 0 && S_ISFIFO(before.st_mode));
                UnitTestResult probe = os_test_capture_sink_refusals(arguments, (OsFileDescriptor*)(u64)(fifo + 1), regular, marker, invalid_argument);
                result.test_count += probe.test_count;
                result.succeeded_test_count += probe.succeeded_test_count;
                struct stat after = {0};
                BUSTER_TEST(arguments, fcntl(fifo, F_GETFL) == flags && fstat(fifo, &after) == 0 && before.st_dev == after.st_dev && before.st_ino == after.st_ino);
                BUSTER_TEST(arguments, close(fifo) == 0);
            }
            BUSTER_TEST(arguments, unlink((char*)fifo_path.pointer) == 0);
        }
        int sockets[2] = {-1, -1};
        if (BUSTER_REQUIRE(arguments, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0))
        {
            int flags = fcntl(sockets[0], F_GETFL);
            struct stat before = {0};
            BUSTER_TEST(arguments, flags >= 0 && fstat(sockets[0], &before) == 0 && S_ISSOCK(before.st_mode));
            UnitTestResult probe = os_test_capture_sink_refusals(arguments, (OsFileDescriptor*)(u64)(sockets[0] + 1), regular, marker, invalid_argument);
            result.test_count += probe.test_count;
            result.succeeded_test_count += probe.succeeded_test_count;
            struct stat after = {0};
            BUSTER_TEST(arguments, fcntl(sockets[0], F_GETFL) == flags && fstat(sockets[0], &after) == 0 && before.st_dev == after.st_dev && before.st_ino == after.st_ino);
            BUSTER_TEST(arguments, close(sockets[0]) == 0);
            BUSTER_TEST(arguments, close(sockets[1]) == 0);
        }
        OsFileDescriptor* closed = os_file_open(regular_path, (OpenFlags){0}, (OsFileAccess){.write = 1}, (OsFileCreateMode){0}, (OsFileShareFlags){0});
        if (BUSTER_REQUIRE(arguments, closed != 0 && os_file_close(closed)))
        {
            UnitTestResult probe = os_test_capture_sink_refusals(arguments, closed, regular, marker, EBADF);
            result.test_count += probe.test_count;
            result.succeeded_test_count += probe.succeeded_test_count;
        }
        int directory = open(".", O_RDONLY);
        if (BUSTER_REQUIRE(arguments, directory >= 0))
        {
            UnitTestResult probe = os_test_capture_sink_refusals(arguments, (OsFileDescriptor*)(u64)(directory + 1), regular, marker, invalid_argument);
            result.test_count += probe.test_count;
            result.succeeded_test_count += probe.succeeded_test_count;
            BUSTER_TEST(arguments, close(directory) == 0);
        }
#endif
        UnitTestResult missing = os_test_capture_sink_refusals(arguments, 0, regular, marker, invalid_argument);
        result.test_count += missing.test_count;
        result.succeeded_test_count += missing.succeeded_test_count;
        String8 child[] = {program_state->input.arguments.pointer[0], S8("help")};
        ProcessSpawnOptions options = {.capture = (u64)1 << STANDARD_STREAM_OUTPUT,
                                      .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE};
        options.capture_overflow_files[STANDARD_STREAM_OUTPUT] = regular;
        OsFileTestStep stats_failure = {OS_FILE_TEST_STATS, OS_FILE_TEST_ERROR, 12345};
        os_file_test_begin(regular_path, &stats_failure, 1);
        // Reopen after selecting the existing descriptor-scoped file seam.
        BUSTER_TEST(arguments, os_file_close(regular));
        regular = os_file_open(regular_path, (OpenFlags){0}, (OsFileAccess){.write = 1}, (OsFileCreateMode){0}, (OsFileShareFlags){0});
        if (BUSTER_REQUIRE(arguments, regular != 0))
        {
            options.capture_overflow_files[STANDARD_STREAM_OUTPUT] = regular;
            ProcessSpawnResult failed = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child), (SliceString8){0}, (SliceString8){0}, options);
            BUSTER_TEST(arguments, failed.failure == PROCESS_SPAWN_FAILURE_CAPTURE_SINK && failed.error.v == 12345 &&
                                   os_process_spawn_test_released(failed) && !failed.process_tree);
            if (failed.handle) os_process_wait_deadline(arena, failed, 1000000);
            BUSTER_TEST(arguments, os_file_test_end() == 1);
            BUSTER_TEST(arguments, os_file_close(regular));
        }
        else BUSTER_TEST(arguments, os_file_test_end() == 0);
        BUSTER_TEST(arguments, os_file_delete(regular_path));
    }
    return result;
}
#endif

enum { OS_TEST_PROCESS_DEADLINE_LARGE_OUTPUT_LENGTH = 32 * 1024 + 4096 };

// Private child payloads must run before compiler prewarming and unrelated test
// modules, so process deadlines measure the payload and not a nested suite.
#if BUSTER_WINDOWS && !BUSTER_SINGLE_THREADED
typedef struct OsTestExitWaiter OsTestExitWaiter;
struct OsTestExitWaiter
{
    ProcessSpawnResult spawn;
    OsProcessHandle* release;
    OsProcessHandle* ready;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_exit_waiter(void* argument)
{
    OsTestExitWaiter* state = (OsTestExitWaiter*)argument;
    Arena* arena = arena_create((ArenaCreation){0});
    if (arena)
    {
        os_process_cleanup_windows_test_begin(state->spawn, 0);
        os_process_shutdown_windows_test_claim_gate(state->release, state->ready);
        os_process_wait_deadline(arena, state->spawn, 1);
        os_process_cleanup_windows_test_end();
        arena_destroy(arena, 1);
    }
}
#endif

void os_test_process_child_run(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);

#if !BUSTER_SINGLE_THREADED && (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 resource_failure_mode = os_get_environment_variable(S8("BUSTER_OS_RESOURCE_FAILURE_MODE"));
    if (resource_failure_mode.length)
    {
        os_resource_test_clear();
        if (string_equal(resource_failure_mode, S8("barrier")) || string_equal(resource_failure_mode, S8("thread")))
        {
            ThreadContext* owner = thread_context_allocate();
            BUSTER_RT_CHECK(owner != 0);
            thread_context_select(owner);
            if (string_equal(resource_failure_mode, S8("barrier")))
            {
                os_resource_test_fail_on_call(OS_RESOURCE_TEST_BARRIER_CREATE, 0);
            }
            else
            {
                // Lane 1 starts successfully; lane 2 fails while lane 1 is
                // still held behind the constructor's startup gate.
                os_resource_test_fail_on_call(OS_RESOURCE_TEST_THREAD_CREATE, 1);
            }
            lane_run(3, &os_test_resource_noop, 0);
        }
        else if (string_equal(resource_failure_mode, S8("join")))
        {
            OsThreadHandle* handle = os_thread_create((ThreadCreateOptions){
                .callback = &os_test_resource_noop,
                .argument = 0,
            });
            BUSTER_RT_CHECK(handle != 0);
            os_resource_test_fail_on_call(OS_RESOURCE_TEST_THREAD_JOIN, 0);
            if (!os_thread_join(handle))
            {
                os_fail_message(S8("thread join failed"));
            }
        }
        os_fail_message(S8("resource failure injection was not observed"));
    }
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 process_test_mode = os_get_environment_variable(S8("BUSTER_OS_PROCESS_TEST_MODE"));
    if (string_equal(process_test_mode, S8("capture-sink-admission")))
    {
        UnitTestResult probe = os_test_capture_sink_admission_child(arguments);
        bool passed = unit_test_succeeded(probe);
        if (!passed)
        {
            arguments->show(arguments, S8("CAPTURE_SINK_CHILD_V1 passed={u64} failed={u64} assertions={u64}\n"),
                            probe.succeeded_test_count, probe.test_count - probe.succeeded_test_count, probe.test_count);
        }
        if (passed) os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(S8("CAPTURE_SINK_ADMISSION_V1\n")));
        os_exit(passed ? 0 : 94);
    }
    if (string_equal(process_test_mode, S8("capture-sink-marker")) || string_equal(process_test_mode, S8("capture-sink-park")) ||
        string_equal(process_test_mode, S8("capture-sink-payload")))
    {
        bool parked = string_equal(process_test_mode, S8("capture-sink-park"));
        bool written = parked || os_test_create_empty_file(os_get_environment_variable(S8("BUSTER_OS_CAPTURE_SINK_MARKER")));
        u8 output[OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES];
        u8 error[OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES];
        for (u32 index = 0; index < OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES; index += 1)
        {
            output[index] = (u8)(index * 17 + 3);
            error[index] = (u8)(index * 29 + 7);
        }
        written = written && os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){output, sizeof(output)});
        written = written && os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_ERROR), (ByteSlice){error, sizeof(error)});
        if (parked || string_equal(process_test_mode, S8("capture-sink-payload")))
        {
            written = written && os_test_create_empty_file(os_get_environment_variable(S8("BUSTER_OS_PROCESS_READY")));
            if (parked && written)
            {
                for (;;)
                {
                    os_test_sleep_milliseconds(10);
                }
            }
        }
        os_exit(written ? 0 : 95);
    }
#if BUSTER_WINDOWS
    if (string_equal(process_test_mode, S8("cleanup-park")))
    {
        bool ready = os_test_create_empty_file(os_get_environment_variable(S8("BUSTER_OS_PROCESS_READY")));
        if (!ready) os_exit(96);
        for (;;) os_test_sleep_milliseconds(10);
    }
    if (string_starts_with_sequence(process_test_mode, S8("cleanup-exit-")))
    {
        String8 argv[] = {program_state->input.arguments.pointer[0], S8("test")};
        String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE")};
        String8 values[] = {S8("cleanup-park")};
        OsTestEnvironment environment = os_test_environment(arguments->arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
            (ProcessSpawnOptions){.new_process_group = 1, .capture = (u64)1 << STANDARD_STREAM_OUTPUT});
        String16Z event_name = {0};
        bool converted = string16z_from_string8_arena(arguments->arena, os_get_environment_variable(S8("BUSTER_OS_CLEANUP_RELEASE_EVENT")), &event_name);
        HANDLE release = converted ? OpenEventW(SYNCHRONIZE, FALSE, event_name.pointer) : 0;
        if (!spawn.handle || !release) os_exit(98);
        String8 ready = os_get_environment_variable(S8("BUSTER_OS_PROCESS_READY"));
        u32 ready_polls = 0;
        while (ready_polls < 3000 && !os_test_regular_file_exists(ready))
        {
            os_test_sleep_milliseconds(1);
            ready_polls += 1;
        }
        if (!os_test_regular_file_exists(ready)) os_exit(97);
#if !BUSTER_SINGLE_THREADED
        if (string_equal(process_test_mode, S8("cleanup-exit-concurrent")))
        {
            HANDLE claimed = CreateEventW(0, TRUE, FALSE, 0);
            if (!claimed) os_exit(96);
            OsTestExitWaiter waiter = {.spawn = spawn, .release = (OsProcessHandle*)release, .ready = (OsProcessHandle*)claimed};
            OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){.callback = &os_test_exit_waiter, .argument = &waiter});
            if (!thread || WaitForSingleObject(claimed, 30000) != WAIT_OBJECT_0) os_exit(95);
            // The worker is parked AFTER the exclusive WAITING claim. This
            // main thread cannot steal it; the parent releases its owned gate.
            os_exit(0);
        }
#endif
        u32 flags = OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION | OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY;
        if (string_equal(process_test_mode, S8("cleanup-exit-waiting"))) flags |= OS_PROCESS_CLEANUP_TEST_EXIT_IN_CLAIM;
        if (string_equal(process_test_mode, S8("cleanup-exit-reserved"))) flags |= OS_PROCESS_CLEANUP_TEST_EXIT_AFTER_RESERVE;
        os_process_cleanup_windows_test_begin(spawn, flags);
        os_process_shutdown_windows_test_release_event((OsProcessHandle*)release);
        if (string_equal(process_test_mode, S8("cleanup-exit-reserved")))
        {
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values, (ProcessSpawnOptions){.new_process_group = 1});
        }
        else if (!string_equal(process_test_mode, S8("cleanup-exit-running")))
        {
            os_process_wait_deadline(arguments->arena, spawn, 1);
        }
        if (!string_equal(process_test_mode, S8("cleanup-exit-return"))) os_exit(0);
    }
    if (string_equal(process_test_mode, S8("cleanup-shutdown")))
    {
        String8 argv[] = {program_state->input.arguments.pointer[0], S8("test")};
        String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE")};
        String8 values[] = {S8("cleanup-park")};
        OsTestEnvironment environment = os_test_environment(arguments->arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
            (ProcessSpawnOptions){.new_process_group = 1});
        bool passed = spawn.handle != 0;
        if (spawn.handle)
        {
            ProcessCleanupResult active = os_process_cleanup_snapshot();
            passed = passed && active.active_owners == 1 && !active.pending_owners && active.error.v == ERROR_BUSY &&
                active.error_owner_id == spawn.windows_owner_id;
            os_process_cleanup_windows_test_begin(spawn, OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION |
                OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY);
            ProcessCleanupResult retained = os_process_shutdown(0);
            OsProcessCleanupWindowsTestStats stats = os_process_cleanup_windows_test_end();
            passed = passed && retained.active_owners == 1 && retained.pending_owners == 1 && retained.error.v != 0 &&
                !stats.ceiling_reached && !stats.infinite_waits;
            ProcessCleanupResult resumed = os_process_cleanup_pending(5000000);
            passed = passed && !resumed.active_owners && !os_process_shutdown(0).active_owners;
            ProcessSpawnResult refused = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
                (ProcessSpawnOptions){.new_process_group = 1});
            passed = passed && !refused.handle && refused.failure == PROCESS_SPAWN_FAILURE_PENDING_CLEANUP;
        }
        os_exit(passed ? 0 : 97);
    }
#endif
    if (string_equal(process_test_mode, S8("capture-small")))
    {
        bool written = os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(S8("owned")));
        os_exit(written ? 0 : 90);
    }
    if (string_equal(process_test_mode, S8("deadline-hot-writer")) ||
        string_equal(process_test_mode, S8("deadline-hot-descendant")))
    {
        u8 output[4096];
        u8 error[4096];
        memset(output, 'H', sizeof(output));
        memset(error, 'E', sizeof(error));
        bool hot_descendant = string_equal(process_test_mode, S8("deadline-hot-descendant"));
        bool written = os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){output, sizeof(output)});
        if (hot_descendant)
        {
            written = os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_ERROR), (ByteSlice){error, sizeof(error)}) && written;
        }
        String8 ready = os_get_environment_variable(S8("BUSTER_OS_PROCESS_READY"));
        written = written && os_test_create_empty_file(ready);
        if (written)
        {
            for (;;)
            {
                bool output_written = os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){output, sizeof(output)});
                bool error_written = !hot_descendant ||
                    os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_ERROR), (ByteSlice){error, sizeof(error)});
                if (!output_written || !error_written)
                {
                    os_test_sleep_milliseconds(1);
                }
            }
        }
        os_exit(96);
    }
    if (string_equal(process_test_mode, S8("deadline-large-output")))
    {
        u8 output[OS_TEST_PROCESS_DEADLINE_LARGE_OUTPUT_LENGTH];
        memset(output, 'L', sizeof(output));
        bool written = os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){output, sizeof(output)});
        os_exit(written ? 0 : 99);
    }
    if (string_equal(process_test_mode, S8("deadline-hot-writer-parent")))
    {
        String8 override_keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE")};
        String8 override_values[] = {S8("deadline-hot-descendant")};
        OsTestEnvironment child_environment =
            os_test_environment(arguments->arena, override_keys, override_values, BUSTER_ARRAY_LENGTH(override_keys));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        ProcessSpawnResult child =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), child_environment.keys, child_environment.values,
                             (ProcessSpawnOptions){0});
        if (child.handle)
        {
            String8 ready = os_get_environment_variable(S8("BUSTER_OS_PROCESS_READY"));
            u32 polls = 0;
            while (polls < 3000 && !os_test_regular_file_exists(ready))
            {
                os_test_sleep_milliseconds(1);
                polls += 1;
            }
            os_exit(os_test_regular_file_exists(ready) ? 0 : 97);
        }
        os_exit(98);
    }
    if (string_starts_with_sequence(process_test_mode, S8("flood")))
    {
        u8 output[4096];
        u8 error[4096];
        for (u64 index = 0; index < sizeof(output); index += 1)
        {
            output[index] = (u8)(index * 17 + 3);
            error[index] = (u8)(index * 29 + 7);
        }
        bool both = string_equal(process_test_mode, S8("flood-both"));
        bool written = true;
        for (u32 block = 0; block < 128 && written; block += 1)
        {
            written = os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){output, sizeof(output)});
            if (both)
            {
                written = os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_ERROR), (ByteSlice){error, sizeof(error)}) && written;
            }
        }
        os_exit(written ? 0 : 90);
    }
    if (string_equal(process_test_mode, S8("tree-grandchild")))
    {
        String8 marker = S8("PROCESS_TREE_DESCENDANT_ENTERED_V1\n");
        if (!os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_ERROR),
                (ByteSlice){.pointer = (u8*)marker.pointer, .length = marker.length}))
        {
            os_exit(93);
        }
        String8 ready = os_get_environment_variable(S8("BUSTER_OS_PROCESS_READY"));
        String8 release = os_get_environment_variable(S8("BUSTER_OS_PROCESS_RELEASE"));
        String8 escaped = os_get_environment_variable(S8("BUSTER_OS_PROCESS_ESCAPED"));
        bool ready_created = os_test_create_empty_file(ready);
        while (ready_created && !os_test_regular_file_exists(release))
        {
            os_test_sleep_milliseconds(1);
        }
        bool escaped_created = ready_created && os_test_create_empty_file(escaped);
        os_exit(escaped_created ? 0 : 91);
    }
    if (string_equal(process_test_mode, S8("tree-parent")))
    {
        String8 marker = S8("PROCESS_TREE_PARENT_ENTERED_V1\n");
        if (!os_file_write_attempt(os_get_standard_stream(STANDARD_STREAM_ERROR),
                (ByteSlice){.pointer = (u8*)marker.pointer, .length = marker.length}))
        {
            os_exit(94);
        }
        String8 override_keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE")};
        String8 override_values[] = {S8("tree-grandchild")};
        OsTestEnvironment child_environment = os_test_environment(arguments->arena, override_keys, override_values, BUSTER_ARRAY_LENGTH(override_keys));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        ProcessSpawnResult child =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), child_environment.keys, child_environment.values, (ProcessSpawnOptions){0});
        if (!child.handle)
        {
            os_exit(92);
        }
        for (;;)
        {
            os_test_sleep_milliseconds(100);
        }
    }
#endif
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_environment_lookup(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // os_tests runs serially, so these bounded captured-environment snapshots
    // cannot affect another module. Preserve values and restore both slices.
    SliceString8 saved_keys = program_state->input.environment_keys;
    SliceString8 saved_values = program_state->input.environment_values;
    String8 root_spellings[] = {S8("SystemRoot"), S8("SYSTEMROOT"), S8("systemroot"), S8("sYsTeMrOoT")};
    String8 path_spellings[] = {S8("PATH"), S8("Path"), S8("path"), S8("pAtH")};
#if BUSTER_WINDOWS
    if (os_get_environment_variable(S8("BUSTER_OS_ENVIRONMENT_EVIDENCE")).length)
    {
        for (u64 index = 0; index < saved_keys.length; index += 1)
        {
            for (u64 spelling = 0; spelling < BUSTER_ARRAY_LENGTH(root_spellings); spelling += 1)
            {
                if (string_equal(saved_keys.pointer[index], root_spellings[spelling]) ||
                    string_equal(saved_keys.pointer[index], path_spellings[spelling]))
                {
                    string_print(S8("OS_ENVIRONMENT_SPELLING name={S8}\n"), saved_keys.pointer[index]);
                }
            }
        }
        bool root_present = os_get_environment_variable(S8("SystemRoot")).length != 0;
        bool path_present = os_get_environment_variable(S8("PATH")).length != 0;
        string_print(S8("OS_ENVIRONMENT_LOOKUP SystemRoot_present={u32} PATH_present={u32}\n"), (u32)root_present, (u32)path_present);
        BUSTER_TEST(arguments, root_present && path_present);
    }
#endif
    String8 keys[] = {S8("SystemRoot"), S8("PATH"), S8("EMPTY"), S8("NULL_EMPTY")};
    String8 values[] = {S8("C:\\WiNdOwS"), S8("C:\\Tools;D:\\Mixed Case"), S8(""), {0}};
    program_state->input.environment_keys = (SliceString8)BUSTER_ARRAY_TO_SLICE(keys);
    program_state->input.environment_values = (SliceString8)BUSTER_ARRAY_TO_SLICE(values);
    for (u64 stored = 0; stored < BUSTER_ARRAY_LENGTH(root_spellings); stored += 1)
    {
        keys[0] = root_spellings[stored];
        keys[1] = path_spellings[stored];
        for (u64 queried = 0; queried < BUSTER_ARRAY_LENGTH(root_spellings); queried += 1)
        {
#if BUSTER_WINDOWS
            bool expected = true;
#else
            bool expected = stored == queried;
#endif
            String8 root = os_get_environment_variable(root_spellings[queried]);
            String8 path = os_get_environment_variable(path_spellings[queried]);
            BUSTER_TEST(arguments, expected ? root.pointer == values[0].pointer && root.length == values[0].length : root.pointer == 0 && root.length == 0);
            BUSTER_TEST(arguments, expected ? path.pointer == values[1].pointer && path.length == values[1].length : path.pointer == 0 && path.length == 0);
        }
    }
    String8 absent[] = {S8("MISSING"), S8("PATHX"), S8("SystemRootX"), S8(""), {0}, {.length = 4}};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(absent); index += 1)
    {
        String8 missing = os_get_environment_variable(absent[index]);
        BUSTER_TEST(arguments, missing.pointer == 0 && missing.length == 0);
    }
    String8 empty = os_get_environment_variable(S8("EMPTY"));
    BUSTER_TEST(arguments, empty.pointer == values[2].pointer && empty.length == 0);
    empty = os_get_environment_variable(S8("NULL_EMPTY"));
    BUSTER_TEST(arguments, empty.pointer == 0 && empty.length == 0);

    keys[0] = S8("SystemRoot");
    char8 bounded_name[] = {'S', 'y', 's', 't', 'e', 'm', 'R', 'o', 'o', 't', 'X'};
    String8 bounded = os_get_environment_variable((String8){.pointer = bounded_name, .length = 10});
    BUSTER_TEST(arguments, bounded.pointer == values[0].pointer && bounded.length == values[0].length);
    char8 exact_name[] = {'S', 'y', 's', 't', 'e', 'm', 'R', 'o', 'o', 't'};
    bounded = os_get_environment_variable((String8){.pointer = exact_name, .length = sizeof(exact_name)});
    BUSTER_TEST(arguments, bounded.pointer == values[0].pointer && bounded.length == values[0].length);

    keys[0] = S8("BUSTER_\xc3\x89");
    String8 unicode = os_get_environment_variable(S8("BUSTER_\xc3\x89"));
    BUSTER_TEST(arguments, unicode.pointer == values[0].pointer && unicode.length == values[0].length);
    unicode = os_get_environment_variable(S8("buster_\xc3\xa9"));
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, unicode.pointer == values[0].pointer && unicode.length == values[0].length);
    // The uncommon Unicode path remains usable without a ThreadContext and
    // does not change the caller's context or the captured value's lifetime.
    ThreadContext* saved_context = thread_context_selected();
    thread_context_select(0);
    unicode = os_get_environment_variable(S8("buster_\xc3\xa9"));
    thread_context_select(saved_context);
    BUSTER_TEST(arguments, unicode.pointer == values[0].pointer && unicode.length == values[0].length);
#else
    BUSTER_TEST(arguments, unicode.pointer == 0 && unicode.length == 0);
#endif
    keys[1] = S8("buster_\xc3\xa9");
    unicode = os_get_environment_variable(keys[1]);
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, unicode.pointer == values[0].pointer && unicode.length == values[0].length);
#else
    BUSTER_TEST(arguments, unicode.pointer == values[1].pointer && unicode.length == values[1].length);
#endif
    keys[0] = S8("BUSTER_\xef\xbf\xbd");
    unicode = os_get_environment_variable(S8("BUSTER_\xff"));
    BUSTER_TEST(arguments, unicode.pointer == 0 && unicode.length == 0);

    keys[0] = S8("PaTh");
    keys[1] = S8("PATH");
    String8 duplicate = os_get_environment_variable(S8("PATH"));
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, duplicate.pointer == values[0].pointer && duplicate.length == values[0].length);
#else
    BUSTER_TEST(arguments, duplicate.pointer == values[1].pointer && duplicate.length == values[1].length);
#endif
    keys[0] = S8("PATH");
    duplicate = os_get_environment_variable(S8("PATH"));
    BUSTER_TEST(arguments, duplicate.pointer == values[0].pointer && duplicate.length == values[0].length);
    values[0] = S8("");
    duplicate = os_get_environment_variable(S8("PATH"));
    BUSTER_TEST(arguments, duplicate.pointer == values[0].pointer && duplicate.length == 0);
    keys[0] = S8("pAtH");
    duplicate = os_get_environment_variable(S8("PATH"));
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, duplicate.pointer == values[0].pointer && duplicate.length == 0);
#else
    BUSTER_TEST(arguments, duplicate.pointer == values[1].pointer && duplicate.length == values[1].length);
#endif
    program_state->input.environment_keys = (SliceString8){0};
    program_state->input.environment_values = (SliceString8){0};
    String8 missing = os_get_environment_variable(S8("PATH"));
    BUSTER_TEST(arguments, missing.pointer == 0 && missing.length == 0);
    program_state->input.environment_keys = saved_keys;
    program_state->input.environment_values = saved_values;
    return result;
}

#include <buster/tests/os_capture_replay_test_internal.h>

// The allocation oracle counts requested arena bytes, not process RSS. Its
// independent 64-byte node budget includes the header and alignment padding.
// A one-byte-per-read collector would exceed it by about 32 MiB for 1 MiB.
BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_storage(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 start = arena->position;
    u8* data = arena_allocate(arena, u8, 1048576);
    for (u64 index = 0; index < 1048576; index += 1) data[index] = (u8)(index * 37 + index / 251);
    u64 lengths[] = {0, 1, 16384, 16385, 1048576};
    u64 read_sizes[] = {0, 1, 97};
    u64 output_start = arena->position;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(lengths); index += 1)
    {
        u64 length = lengths[index];
        u64 chunks = (length + 16383) / 16384;
        u64 storage = 0;
        for (u32 fragment = 0; fragment < BUSTER_ARRAY_LENGTH(read_sizes); fragment += 1)
        {
            OsProcessCaptureTestInput input = {STANDARD_STREAM_OUTPUT, {data, length}, read_sizes[fragment]};
            OsProcessCaptureTestResult collected = os_process_capture_test_collect(arena, (ProcessSpawnResult){0}, &input, 1);
            BUSTER_TEST(arguments, collected.chunk_count[STANDARD_STREAM_OUTPUT] == chunks &&
                collected.chunk_count[STANDARD_STREAM_ERROR] == 0 && collected.chunk_count[STANDARD_STREAM_INPUT] == 0);
            BUSTER_TEST(arguments, collected.storage_bytes >= chunks * 16384 &&
                collected.storage_bytes <= chunks * (16384 + 64));
            if (fragment == 0) storage = collected.storage_bytes;
            BUSTER_TEST(arguments, collected.storage_bytes == storage);
            BUSTER_TEST(arguments, collected.wait.observed_total == length && collected.wait.captured_total == length &&
                collected.wait.observed_bytes[STANDARD_STREAM_OUTPUT] == length &&
                collected.wait.captured_bytes[STANDARD_STREAM_OUTPUT] == length);
            BUSTER_TEST(arguments, collected.wait.streamed_total == 0 && collected.wait.dropped_total == 0 &&
                !collected.wait.capture_limit_exceeded && !collected.wait.output_truncated && !collected.wait.capture_failed &&
                collected.wait.result == PROCESS_RESULT_SUCCESS);
            // Reuse and overwrite the collector's scratch storage before
            // checking that flattened output belongs to the caller's arena.
            TemporalArena reuse = scratch_begin(&arena, 1);
            if (storage) memset(arena_allocate(reuse.arena, u8, storage), 0xA5, storage);
            scratch_end(reuse);
            if (BUSTER_REQUIRE(arguments, collected.wait.streams[STANDARD_STREAM_OUTPUT].length == length))
            {
                if (length) BUSTER_TEST(arguments, memory_compare(collected.wait.streams[STANDARD_STREAM_OUTPUT].pointer, data, length));
            }
            BUSTER_TEST(arguments, collected.wait.streams[STANDARD_STREAM_ERROR].length == 0);
            arena_set_position(arena, output_start);
        }
    }
    arena_set_position(arena, start);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_limits(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 start = arena->position;
    u64 length = 16777217;
    u8* data = arena_allocate(arena, u8, length);
    for (u64 index = 0; index < length; index += 1) data[index] = (u8)(index * 37 + index / 251);
    u64 output_start = arena->position;
    // Zero selects the defaults. Explicit defaults agree; UINT64_MAX removes
    // the per-stream and/or total bound. The total-only row crosses 32 MiB.
    u64 per_stream[] = {0, 16777216, UINT64_MAX, UINT64_MAX};
    u64 total[] = {0, 33554432, 0, UINT64_MAX};
    u64 expected_output[] = {16777216, 16777216, 16777217, 16777217};
    u64 expected_error[] = {16777216, 16777216, 16777215, 16777217};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(total); index += 1)
    {
        ProcessSpawnResult spawn = {0};
        spawn.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = per_stream[index];
        spawn.capture_limits.per_stream[STANDARD_STREAM_ERROR] = per_stream[index];
        spawn.capture_limits.total = total[index];
        OsProcessCaptureTestInput inputs[] = {
            {STANDARD_STREAM_OUTPUT, {data, length}, 0},
            {STANDARD_STREAM_ERROR, {data, length}, 0},
        };
        OsProcessCaptureTestResult collected = os_process_capture_test_collect(arena, spawn, inputs, BUSTER_ARRAY_LENGTH(inputs));
        u64 captured = expected_output[index] + expected_error[index];
        BUSTER_TEST(arguments, collected.wait.observed_total == 33554434 && collected.wait.captured_total == captured &&
            collected.wait.dropped_total == 33554434 - captured && collected.wait.streamed_total == 0);
        BUSTER_TEST(arguments, (bool)collected.wait.capture_limit_exceeded == (index != 3) &&
            (bool)collected.wait.output_truncated == (index != 3) && !collected.wait.capture_failed &&
            collected.wait.result == PROCESS_RESULT_SUCCESS);
        u64 expected[] = {0, expected_output[index], expected_error[index]};
        u64 nodes = 0;
        for (u32 stream = STANDARD_STREAM_OUTPUT; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            u64 chunks = (expected[stream] + 16383) / 16384;
            nodes += chunks;
            BUSTER_TEST(arguments, collected.chunk_count[stream] == chunks);
            BUSTER_TEST(arguments, collected.wait.observed_bytes[stream] == length &&
                collected.wait.captured_bytes[stream] == expected[stream] &&
                collected.wait.dropped_bytes[stream] == length - expected[stream] && collected.wait.streamed_bytes[stream] == 0);
            if (BUSTER_REQUIRE(arguments, collected.wait.streams[stream].length == expected[stream]))
            {
                BUSTER_TEST(arguments, memory_compare(collected.wait.streams[stream].pointer, data, expected[stream]));
            }
        }
        BUSTER_TEST(arguments, collected.storage_bytes <= captured + 2 * 16383 + nodes * 64);
        arena_set_position(arena, output_start);
    }

    // Interleaving matters when streams share a quota. Within each admitted
    // descriptor, fragmentation must preserve both prefixes and accounting.
    u64 read_sizes[] = {0, 1, 97};
    for (u32 policy = PROCESS_CAPTURE_OVERFLOW_TRUNCATE; policy <= PROCESS_CAPTURE_OVERFLOW_FAIL; policy += 1)
    {
        for (u32 fragment = 0; fragment < BUSTER_ARRAY_LENGTH(read_sizes); fragment += 1)
        {
            ProcessSpawnResult spawn = {0};
            spawn.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = 16385;
            spawn.capture_limits.per_stream[STANDARD_STREAM_ERROR] = 16384;
            spawn.capture_limits.total = 24577;
            spawn.capture_overflow_policy = (ProcessCaptureOverflowPolicy)policy;
            OsProcessCaptureTestInput inputs[] = {
                {STANDARD_STREAM_OUTPUT, {data, 16384}, read_sizes[fragment]},
                {STANDARD_STREAM_ERROR, {data, 8192}, read_sizes[fragment]},
                {STANDARD_STREAM_OUTPUT, {0}, 0},
                {STANDARD_STREAM_OUTPUT, {data + 16384, 16385}, read_sizes[fragment]},
                {STANDARD_STREAM_ERROR, {data + 8192, 8193}, read_sizes[fragment]},
            };
            OsProcessCaptureTestResult collected = os_process_capture_test_collect(arena, spawn, inputs, BUSTER_ARRAY_LENGTH(inputs));
            BUSTER_TEST(arguments, collected.wait.observed_total == 49154 && collected.wait.captured_total == 24577 &&
                collected.wait.dropped_total == 24577 && collected.wait.streamed_total == 0);
            BUSTER_TEST(arguments, collected.wait.capture_limit_exceeded && collected.wait.output_truncated &&
                (bool)collected.wait.capture_failed == (policy == PROCESS_CAPTURE_OVERFLOW_FAIL) &&
                collected.wait.result == (policy == PROCESS_CAPTURE_OVERFLOW_FAIL ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS));
            u64 observed[] = {0, 32769, 16385};
            u64 expected[] = {0, 16385, 8192};
            for (u32 stream = STANDARD_STREAM_OUTPUT; stream < STANDARD_STREAM_COUNT; stream += 1)
            {
                BUSTER_TEST(arguments, collected.wait.observed_bytes[stream] == observed[stream] &&
                    collected.wait.captured_bytes[stream] == expected[stream] &&
                    collected.wait.dropped_bytes[stream] == observed[stream] - expected[stream] && collected.wait.streamed_bytes[stream] == 0);
                if (BUSTER_REQUIRE(arguments, collected.wait.streams[stream].length == expected[stream]))
                {
                    BUSTER_TEST(arguments, memory_compare(collected.wait.streams[stream].pointer, data, expected[stream]));
                }
            }
            BUSTER_TEST(arguments, collected.chunk_count[STANDARD_STREAM_OUTPUT] == 2 && collected.chunk_count[STANDARD_STREAM_ERROR] == 1 &&
                collected.storage_bytes <= 3 * (16384 + 64));
            arena_set_position(arena, output_start);
        }
    }
    arena_set_position(arena, start);
    return result;
}

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_overflow(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 start = arena->position;
    String8 path = buster_test_temporary_path(arena, S8("capture-storage-overflow"), S8(".bin"));
    u8* data = arena_allocate(arena, u8, 32769);
    for (u64 index = 0; index < 32769; index += 1) data[index] = (u8)(index * 37 + index / 251);
    u64 output_start = arena->position;
    u64 read_sizes[] = {0, 97, 0, 0};
    u64 streamed[] = {16384, 16384, 0, 7};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(read_sizes); index += 1)
    {
        BUSTER_TEST(arguments, os_file_delete(path));
        OsFileTestStep steps[] = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 7}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}};
        if (index == 3) os_file_test_begin(path, steps, BUSTER_ARRAY_LENGTH(steps));
        OsFileDescriptor* file = 0;
        if (index != 2)
        {
            file = os_file_open(path, (OpenFlags){.create = 1, .truncate = 1}, (OsFileAccess){.write = 1}, (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
            BUSTER_TEST(arguments, file != 0);
        }
        if (file || index == 2)
        {
            ProcessSpawnResult spawn = {.capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE};
            spawn.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = 16385;
            spawn.capture_limits.total = 16385;
            spawn.capture_overflow_files[STANDARD_STREAM_OUTPUT] = file;
            OsProcessCaptureTestInput input = {STANDARD_STREAM_OUTPUT, {data, 32769}, read_sizes[index]};
            OsProcessCaptureTestResult collected = os_process_capture_test_collect(arena, spawn, &input, 1);
            if (index == 3) BUSTER_TEST(arguments, os_file_test_end() == BUSTER_ARRAY_LENGTH(steps));
            BUSTER_TEST(arguments, collected.wait.observed_total == 32769 && collected.wait.captured_total == 16385 &&
                collected.wait.streamed_total == streamed[index] && collected.wait.dropped_total == 16384 - streamed[index]);
            BUSTER_TEST(arguments, collected.wait.observed_bytes[STANDARD_STREAM_OUTPUT] == 32769 &&
                collected.wait.captured_bytes[STANDARD_STREAM_OUTPUT] == 16385 &&
                collected.wait.streamed_bytes[STANDARD_STREAM_OUTPUT] == streamed[index] &&
                collected.wait.dropped_bytes[STANDARD_STREAM_OUTPUT] == 16384 - streamed[index]);
            BUSTER_TEST(arguments, collected.wait.capture_limit_exceeded && collected.wait.output_truncated &&
                (bool)collected.wait.capture_failed == (index >= 2) &&
                collected.wait.result == (index >= 2 ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS));
            BUSTER_TEST(arguments, collected.chunk_count[STANDARD_STREAM_OUTPUT] == 2 && collected.storage_bytes <= 2 * (16384 + 64));
            if (BUSTER_REQUIRE(arguments, collected.wait.streams[STANDARD_STREAM_OUTPUT].length == 16385))
            {
                BUSTER_TEST(arguments, memory_compare(collected.wait.streams[STANDARD_STREAM_OUTPUT].pointer, data, 16385));
            }
            if (file)
            {
                // The caller retains descriptor ownership after collection.
                FileStats stats = os_file_get_stats(file, (FileStatsOptions){.size = 1});
                BUSTER_TEST(arguments, stats.valid && stats.size == streamed[index]);
                u8 marker = 0xD3;
                OsFileTransferResult appended = os_file_write_checked(file, (ByteSlice){&marker, 1});
                BUSTER_TEST(arguments, appended.transferred == 1 && !appended.error.v);
                BUSTER_TEST(arguments, os_file_close(file));
                ByteSlice actual = file_read(arena, path, (FileReadOptions){0});
                if (BUSTER_REQUIRE(arguments, actual.length == streamed[index] + 1))
                {
                    BUSTER_TEST(arguments, memory_compare(actual.pointer, data + 16385, streamed[index]) && actual.pointer[streamed[index]] == marker);
                }
                BUSTER_TEST(arguments, os_file_delete(path));
            }
        }
        else if (index == 3)
        {
            BUSTER_TEST(arguments, os_file_test_end() == 0);
        }
        arena_set_position(arena, output_start);
    }
    arena_set_position(arena, start);
    return result;
}
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_sink_admission(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 marker = buster_test_temporary_path(arena, S8("capture-sink-launch"), S8(".txt"));
    BUSTER_TEST(arguments, os_file_delete(marker));
    String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_OS_CAPTURE_SINK_MARKER"), S8("BUSTER_TEST_JOBS")};
    String8 values[] = {S8("capture-sink-admission"), marker, S8("1")};
    OsTestEnvironment environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
    String8 child[] = {program_state->input.arguments.pointer[0], S8("test")};
    ProcessSpawnOptions outer = {
        .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR), .new_process_group = 1,
    };
    outer.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = BUSTER_KB(4);
    outer.capture_limits.per_stream[STANDARD_STREAM_ERROR] = BUSTER_KB(4);
    outer.capture_limits.total = BUSTER_KB(8);
#if BUSTER_WINDOWS
    // A cold CreateProcess initializes process-lifetime tracing/registry state.
    // Full-suite runs already reached it before this fixture; isolated OS runs
    // must calibrate before the exact census, without accepting owned leaks.
    u64 cold_resources = os_process_spawn_test_resource_count();
    String8 warm_values[] = {S8("capture-small"), marker, S8("1")};
    OsTestEnvironment warm_environment = os_test_environment(arena, keys, warm_values, BUSTER_ARRAY_LENGTH(keys));
    ProcessSpawnResult warm_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child),
        warm_environment.keys, warm_environment.values, outer);
    BUSTER_RT_CHECK(warm_spawn.handle != 0 && warm_spawn.process_group);
    ProcessWaitResult warm_wait = os_process_wait_deadline(arena, warm_spawn, 30000000);
    BUSTER_RT_CHECK(warm_wait.result == PROCESS_RESULT_SUCCESS && !warm_wait.timed_out && !warm_wait.capture_failed &&
        !warm_wait.output_truncated && !warm_wait.capture_limit_exceeded && !warm_wait.process_tree_cleanup_failed &&
        !warm_wait.process_group_reservation_retained && !warm_wait.process_group_ownership_lost &&
        warm_wait.streams[STANDARD_STREAM_OUTPUT].length == 5);
    // Direct ownership checks cover the cold launch too, before taking a
    // steady-state census that includes the native runtime's persistent state.
    HANDLE owned[] = {(HANDLE)warm_spawn.handle, (HANDLE)warm_spawn.process_tree,
        (HANDLE)warm_spawn.pipes[STANDARD_STREAM_OUTPUT][0], (HANDLE)warm_spawn.pipes[STANDARD_STREAM_ERROR][0]};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(owned); index += 1)
    {
        DWORD flags = 0;
        BUSTER_RT_CHECK(owned[index] && !GetHandleInformation(owned[index], &flags) && GetLastError() == ERROR_INVALID_HANDLE);
    }
    arguments->show(arguments, S8("OS_CAPTURE_CENSUS_WARMUP_V1 before={u64} after={u64} owned_handles=closed status=pass\n"),
        cold_resources, os_process_spawn_test_resource_count());
#endif
    u64 resources = os_process_spawn_test_resource_count();
    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child), environment.keys, environment.values, outer);
    if (BUSTER_REQUIRE(arguments, spawn.handle != 0 && spawn.process_group))
    {
        u64 wait_start = os_now_microseconds();
        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 30000000);
        u64 wait_elapsed = os_now_microseconds() - wait_start;
        BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && !wait.capture_failed);
        BUSTER_TEST(arguments, !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained && !wait.process_group_ownership_lost);
        String8 receipt = S8("CAPTURE_SINK_ADMISSION_V1\n");
        if (wait.result != PROCESS_RESULT_SUCCESS || wait.timed_out || wait.capture_failed ||
            wait.streams[STANDARD_STREAM_OUTPUT].length != receipt.length)
        {
            String8 output = {(char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, wait.streams[STANDARD_STREAM_OUTPUT].length};
            String8 error = {(char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, wait.streams[STANDARD_STREAM_ERROR].length};
            arguments->show(arguments,
                S8("CAPTURE_SINK_ADMISSION_FAILURE_V1 result={u32} platform_status={u32} elapsed_us={u64} deadline_us=30000000 "
                   "timed_out={u32} capture_failed={u32} truncated={u32} observed={u64} captured={u64} dropped={u64} "
                   "cleanup_failed={u32} reservation_retained={u32} ownership_lost={u32} stdout={S8} stderr={S8}\n"),
                (u32)wait.result, wait.platform_status, wait_elapsed, (u32)wait.timed_out, (u32)wait.capture_failed,
                (u32)wait.output_truncated, wait.observed_total, wait.captured_total, wait.dropped_total,
                (u32)wait.process_tree_cleanup_failed, (u32)wait.process_group_reservation_retained,
                (u32)wait.process_group_ownership_lost, output, error);
        }
        if (BUSTER_REQUIRE(arguments, wait.streams[STANDARD_STREAM_OUTPUT].length == receipt.length))
        {
            BUSTER_TEST(arguments, memory_compare(wait.streams[STANDARD_STREAM_OUTPUT].pointer, receipt.pointer, receipt.length));
        }
    }
    BUSTER_TEST(arguments, !os_test_regular_file_exists(marker));
    u64 resources_after = os_process_spawn_test_resource_count();
    if (resources_after != resources)
    {
        arguments->show(arguments, S8("CAPTURE_SINK_RESOURCES_V1 before={u64} after={u64}\n"), resources, resources_after);
    }
    BUSTER_TEST(arguments, resources_after == resources);
    BUSTER_TEST(arguments, os_file_delete(marker));

    // These fields are deliberately invalid but ignored by their policies.
    values[0] = S8("capture-small");
    environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
    String8 regular_path = buster_test_temporary_path(arena, S8("capture-sink-ignored"), S8(".bin"));
    OsFileDescriptor* regular = os_file_open(regular_path, (OpenFlags){.create = 1, .truncate = 1}, (OsFileAccess){.write = 1},
                                            (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
    if (BUSTER_REQUIRE(arguments, regular != 0))
    {
        OsFileDescriptor* invalid = (OsFileDescriptor*)UINT64_MAX;
        for (u32 policy = 0; policy < 4; policy += 1)
        {
            ProcessSpawnOptions options = {.capture = (u64)1 << STANDARD_STREAM_OUTPUT};
            options.capture_overflow_files[STANDARD_STREAM_INPUT] = invalid;
            options.capture_overflow_files[STANDARD_STREAM_OUTPUT] = invalid;
            options.capture_overflow_files[STANDARD_STREAM_ERROR] = invalid;
            if (policy == 1) options.capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL;
            if (policy >= 2) options.capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE;
            if (policy == 2) options.capture_overflow_files[STANDARD_STREAM_OUTPUT] = regular;
            if (policy == 3) options.capture = 0;
            ProcessSpawnResult ignored = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child), environment.keys, environment.values, options);
            BUSTER_TEST(arguments, ignored.failure == PROCESS_SPAWN_FAILURE_NONE && ignored.error.v == 0);
            if (BUSTER_REQUIRE(arguments, ignored.handle != 0))
            {
                ProcessWaitResult wait = os_process_wait_deadline(arena, ignored, 30000000);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS && !wait.capture_failed && !wait.timed_out);
                BUSTER_TEST(arguments, wait.streams[STANDARD_STREAM_OUTPUT].length == (policy == 3 ? 0 : 5));
            }
        }
        BUSTER_TEST(arguments, os_file_close(regular));
        BUSTER_TEST(arguments, os_file_delete(regular_path));
    }
    ProcessSpawnOptions invalid_sink = {.capture = (u64)1 << STANDARD_STREAM_OUTPUT,
                                       .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE};
    ProcessSpawnResult empty = os_process_spawn((SliceString8){0}, (SliceString8){0}, (SliceString8){0}, invalid_sink);
    BUSTER_TEST(arguments, empty.failure == PROCESS_SPAWN_FAILURE_INVALID_ARGUMENTS && empty.error.v && os_process_spawn_test_released(empty));
    ProcessSpawnOptions invalid_policy = invalid_sink;
    invalid_policy.capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_COUNT;
    ProcessSpawnResult policy_failure = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child), (SliceString8){0}, (SliceString8){0}, invalid_policy);
    BUSTER_TEST(arguments, policy_failure.failure == PROCESS_SPAWN_FAILURE_INVALID_ARGUMENTS && policy_failure.error.v && os_process_spawn_test_released(policy_failure));
    String8 key = S8("key");
    ProcessSpawnResult invalid_environment = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child), (SliceString8){&key, 1},
                                                             (SliceString8){0}, invalid_sink);
    BUSTER_TEST(arguments, invalid_environment.failure == PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT && invalid_environment.error.v &&
                           os_process_spawn_test_released(invalid_environment));
    String8 missing_executable[] = {S8("buster-capture-sink-does-not-exist")};
    invalid_sink.search_path = 1;
    ProcessSpawnResult before_lookup = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(missing_executable), (SliceString8){0},
                                                       (SliceString8){0}, invalid_sink);
    BUSTER_TEST(arguments, before_lookup.failure == PROCESS_SPAWN_FAILURE_CAPTURE_SINK && before_lookup.error.v &&
                           os_process_spawn_test_released(before_lookup));
    ProcessWaitResult refused_wait = os_process_wait_deadline(arena, before_lookup, 1);
    BUSTER_TEST(arguments, refused_wait.result == PROCESS_RESULT_UNKNOWN && !refused_wait.observed_total && !refused_wait.timed_out);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_sink_spill(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 paths[] = {
        buster_test_temporary_path(arena, S8("capture-sink-output"), S8(".bin")),
        buster_test_temporary_path(arena, S8("capture-sink-error"), S8(".bin")),
    };
    String8 marker = buster_test_temporary_path(arena, S8("capture-sink-spill-launch"), S8(".txt"));
    String8 ready = buster_test_temporary_path(arena, S8("capture-sink-spill-ready"), S8(".txt"));
    String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_OS_CAPTURE_SINK_MARKER"), S8("BUSTER_OS_PROCESS_READY"), S8("BUSTER_TEST_JOBS")};
    String8 child[] = {program_state->input.arguments.pointer[0], S8("test")};
#if BUSTER_LINUX || BUSTER_MACOS
    u32 case_count = 5;
#else
    u32 case_count = 4;
#endif
    for (u32 test_case = 0; test_case < case_count; test_case += 1)
    {
        BUSTER_TEST(arguments, os_file_delete(marker) && os_file_delete(ready));
        OsFileTestStep write_failure[] = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 7}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}};
        if (test_case == 1) os_file_test_begin(paths[0], write_failure, BUSTER_ARRAY_LENGTH(write_failure));
        OsFileDescriptor* files[2] = {0};
        FileStats identities[2] = {0};
        for (u32 index = 0; index < 2; index += 1)
        {
            BUSTER_TEST(arguments, os_file_delete(paths[index]));
            if (test_case == 2 && index == 0)
            {
                BUSTER_TEST(arguments, os_test_create_empty_file(paths[index]));
                files[index] = os_file_open(paths[index], (OpenFlags){0}, (OsFileAccess){.read = 1}, (OsFileCreateMode){0}, (OsFileShareFlags){0});
            }
            else files[index] = os_file_open(paths[index], (OpenFlags){.create = 1, .truncate = 1}, (OsFileAccess){.write = 1},
                                              (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
            if (files[index]) identities[index] = os_file_get_stats(files[index], (FileStatsOptions){.identity = 1});
        }
        if (BUSTER_REQUIRE(arguments, files[0] != 0 && files[1] != 0 && identities[0].valid && identities[1].valid))
        {
            String8 values[] = {test_case >= 3 ? S8("capture-sink-park") : S8("capture-sink-payload"), marker, ready, S8("1")};
            OsTestEnvironment environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
            ProcessSpawnOptions options = {
                .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR), .new_process_group = 1,
                .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE,
            };
            options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = 1;
            options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = 1;
            options.capture_limits.total = 2;
            options.capture_overflow_files[STANDARD_STREAM_OUTPUT] = files[0];
            options.capture_overflow_files[STANDARD_STREAM_ERROR] = files[1];
            u64 resources = os_process_spawn_test_resource_count();
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child), environment.keys, environment.values, options);
            BUSTER_TEST(arguments, spawn.failure == PROCESS_SPAWN_FAILURE_NONE && spawn.error.v == 0);
            if (BUSTER_REQUIRE(arguments, spawn.handle != 0 && spawn.process_group))
            {
                // The entire modest atomic payload is queued on both pipes
                // before the waiter starts, including the partial-write case.
                u32 polls = 0;
                while (polls < 3000 && !os_test_regular_file_exists(ready))
                {
                    os_test_sleep_milliseconds(10);
                    polls += 1;
                }
                bool payload_ready = os_test_regular_file_exists(ready);
                BUSTER_TEST(arguments, payload_ready);
#if BUSTER_LINUX || BUSTER_MACOS
                ProcessControlAtomic cancellation = test_case == 4 ? SIGTERM : 0;
                ProcessGroupControlState control = {.cancellation_signal = &cancellation};
                if (test_case == 4) spawn.process_group_control = &control;
#endif
                ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, !payload_ready ? 1 : test_case == 3 ? 100000 : 30000000);
                if (test_case == 1) BUSTER_TEST(arguments, os_file_test_end() == BUSTER_ARRAY_LENGTH(write_failure));
                BUSTER_TEST(arguments, !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained && !wait.process_group_ownership_lost);
                BUSTER_TEST(arguments, (bool)wait.capture_failed == (test_case == 1 || test_case == 2));
                BUSTER_TEST(arguments, (bool)wait.timed_out == (test_case == 3));
                if (test_case < 3) BUSTER_TEST(arguments, wait.platform_status == 0 && wait.result == (test_case ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS));
                if (test_case == 3) BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_FAILED && wait.termination_requested && wait.forcibly_terminated);
#if BUSTER_LINUX || BUSTER_MACOS
                if (test_case == 4)
                {
                    BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_CRASH && WIFSIGNALED(wait.platform_status) && WTERMSIG(wait.platform_status) == SIGTERM);
                    BUSTER_TEST(arguments, process_control_atomic_load(&cancellation) == SIGTERM);
                }
#endif
                if (payload_ready)
                {
                    u64 output_streamed = test_case == 1 ? 7 : test_case == 2 ? 0 : OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES - 1;
                    BUSTER_TEST(arguments, wait.observed_total == OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES * 2 && wait.captured_total == 2);
                    BUSTER_TEST(arguments, wait.streamed_total == output_streamed + OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES - 1 &&
                                           wait.dropped_total == OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES - 1 - output_streamed);
                    for (u32 index = 0; index < 2; index += 1)
                    {
                        StandardStream stream = index == 0 ? STANDARD_STREAM_OUTPUT : STANDARD_STREAM_ERROR;
                        u64 streamed = index == 0 ? output_streamed : OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES - 1;
                        BUSTER_TEST(arguments, wait.observed_bytes[stream] == OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES && wait.captured_bytes[stream] == 1 &&
                                               wait.streamed_bytes[stream] == streamed && wait.dropped_bytes[stream] == OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES - 1 - streamed);
                        if (BUSTER_REQUIRE(arguments, wait.streams[stream].length == 1)) BUSTER_TEST(arguments, wait.streams[stream].pointer[0] == (index == 0 ? 3 : 7));
                    }
                }
                BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources);
                if (test_case < 3) BUSTER_TEST(arguments, os_test_regular_file_exists(marker));
            }
            else if (test_case == 1) BUSTER_TEST(arguments, os_file_test_end() == 0);
        }
        else if (test_case == 1) BUSTER_TEST(arguments, os_file_test_end() == 0);
        for (u32 index = 0; index < 2; index += 1)
        {
            if (files[index])
            {
                FileStats after = os_file_get_stats(files[index], (FileStatsOptions){.size = 1, .identity = 1});
                u64 streamed = index == 0 && test_case == 1 ? 7 : index == 0 && test_case == 2 ? 0 : OS_TEST_CAPTURE_SINK_PAYLOAD_BYTES - 1;
                BUSTER_TEST(arguments, after.valid && after.kind == OS_FILE_KIND_REGULAR && after.device == identities[index].device &&
                                       after.index == identities[index].index && after.size == streamed);
                bool append = !(test_case == 2 && index == 0);
                if (append)
                {
                    u8 byte = 0xD3;
                    OsFileTransferResult appended = os_file_write_checked(files[index], (ByteSlice){&byte, 1});
                    BUSTER_TEST(arguments, appended.transferred == 1 && !appended.error.v);
                }
                BUSTER_TEST(arguments, os_file_close(files[index]));
                ByteSlice actual = file_read(arena, paths[index], (FileReadOptions){0});
                if (BUSTER_REQUIRE(arguments, actual.length == streamed + (u64)append))
                {
                    bool bytes_match = true;
                    for (u64 byte = 0; byte < streamed; byte += 1) bytes_match = actual.pointer[byte] == (u8)((byte + 1) * (index == 0 ? 17 : 29) + (index == 0 ? 3 : 7)) && bytes_match;
                    BUSTER_TEST(arguments, bytes_match && (!append || actual.pointer[streamed] == 0xD3));
                }
            }
            BUSTER_TEST(arguments, os_file_delete(paths[index]));
        }
    }
    BUSTER_TEST(arguments, os_file_delete(marker) && os_file_delete(ready));
    return result;
}
#endif

#if BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
#if !BUSTER_SINGLE_THREADED
typedef struct OsTestCleanupThread OsTestCleanupThread;
struct OsTestCleanupThread
{
    ProcessSpawnResult spawn;
    ProcessWaitResult waited;
    OsProcessCleanupWindowsTestStats stats;
    bool arena_destroyed;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_cleanup_thread(void* argument)
{
    OsTestCleanupThread* state = (OsTestCleanupThread*)argument;
    Arena* arena = arena_create((ArenaCreation){0});
    if (arena)
    {
        os_process_cleanup_windows_test_begin(state->spawn, OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION |
            OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY);
        state->waited = os_process_wait_deadline(arena, state->spawn, 1);
        state->stats = os_process_cleanup_windows_test_end();
        state->arena_destroyed = arena_destroy(arena, 1);
    }
}
#endif

#if !BUSTER_SINGLE_THREADED
typedef struct OsTestCleanupResume OsTestCleanupResume;
struct OsTestCleanupResume
{
    AtomicU64* start;
    AtomicU64* ready;
    ProcessCleanupResult result;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_cleanup_resume_thread(void* argument)
{
    OsTestCleanupResume* state = (OsTestCleanupResume*)argument;
    atomic_u64_increment(state->ready);
    while (!atomic_u64_load(state->start)) os_test_sleep_milliseconds(1);
    state->result = os_process_cleanup_pending(5000000);
}
#endif
BUSTER_GLOBAL_LOCAL UnitTestResult os_test_process_cleanup_windows(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 argv[] = {program_state->input.arguments.pointer[0], S8("test")};
    String8 ready = buster_test_temporary_unique_path(arena, S8("process-cleanup-ready"), S8(".txt"));
    String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_OS_PROCESS_READY"), S8("BUSTER_TEST_JOBS")};
    String8 values[] = {S8("cleanup-park"), ready, S8("1")};
    OsTestEnvironment environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
    u64 resources_before = os_process_spawn_test_resource_count();
    u32 cases[] = {
        OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION | OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY,
        OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION | OS_PROCESS_CLEANUP_TEST_WAIT_FAILED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY,
        OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY,
    };
    for (EACH_ARRAY_INDEX(index, cases))
    {
        BUSTER_TEST(arguments, os_file_delete(ready));
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
            (ProcessSpawnOptions){.new_process_group = 1});
        if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
        {
            u32 polls = 0;
            while (polls < 3000 && !os_test_regular_file_exists(ready))
            {
                os_test_sleep_milliseconds(1);
                polls += 1;
            }
            BUSTER_TEST(arguments, os_test_regular_file_exists(ready));
            os_process_cleanup_windows_test_begin(spawn, cases[index]);
            ProcessWaitResult waited = os_process_wait_deadline(arena, spawn, 1);
            OsProcessCleanupWindowsTestStats stats = os_process_cleanup_windows_test_end();
            BUSTER_TEST(arguments, waited.result == PROCESS_RESULT_FAILED && waited.process_tree_cleanup_failed);
            BUSTER_TEST(arguments, waited.pending_windows_owner_id == spawn.windows_owner_id && waited.cleanup_error.v != 0);
            BUSTER_TEST(arguments, waited.timed_out == (index != 1));
            BUSTER_TEST(arguments, stats.infinite_waits == 0 && !stats.ceiling_reached && stats.calls <= 64);
            BUSTER_TEST(arguments, stats.termination_calls > 0 && stats.job_queries > 0);
            if (index == 0) BUSTER_TEST(arguments, waited.cleanup_error.v == ERROR_ACCESS_DENIED);
            if (index == 1) BUSTER_TEST(arguments, waited.cleanup_error.v == ERROR_INVALID_HANDLE);
            ProcessCleanupResult snapshot = os_process_cleanup_snapshot();
            BUSTER_TEST(arguments, snapshot.active_owners == 1 && snapshot.pending_owners == 1);
            ProcessSpawnResult refused = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
                (ProcessSpawnOptions){.new_process_group = 1});
            BUSTER_TEST(arguments, !refused.handle && refused.failure == PROCESS_SPAWN_FAILURE_PENDING_CLEANUP && refused.error.v == ERROR_BUSY);
            // A copied consumed spawn cannot close pipes or signal a reused handle.
            ProcessWaitResult stale = os_process_wait_deadline(arena, spawn, 1);
            BUSTER_TEST(arguments, stale.result == PROCESS_RESULT_FAILED && stale.cleanup_error.v == ERROR_INVALID_HANDLE);
            ProcessCleanupResult resumed = os_process_cleanup_pending(5000000);
            BUSTER_TEST(arguments, resumed.resumed_owner_id == spawn.windows_owner_id && !resumed.active_owners && !resumed.pending_owners);
        }
        BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
        BUSTER_TEST(arguments, os_file_delete(ready));
    }
    OsProcessSpawnTestOperation unwind_operations[] = {OS_PROCESS_SPAWN_TEST_JOB_ASSIGNMENT, OS_PROCESS_SPAWN_TEST_RESUME};
    for (EACH_ARRAY_INDEX(operation_index, unwind_operations))
    {
        for (u32 refused = 0; refused < 2; refused += 1)
        {
            os_process_spawn_test_fail_on_call(unwind_operations[operation_index], 0);
            if (refused) os_process_cleanup_windows_test_begin((ProcessSpawnResult){0},
                OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION | OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY);
            ProcessSpawnResult failed = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
                (ProcessSpawnOptions){.new_process_group = 1});
            bool consumed = os_process_spawn_test_end();
            OsProcessCleanupWindowsTestStats stats = os_process_cleanup_windows_test_end();
            BUSTER_TEST(arguments, consumed && !failed.handle && failed.failure == PROCESS_SPAWN_FAILURE_SPAWN && failed.error.v != 0);
            BUSTER_TEST(arguments, (failed.pending_windows_owner_id != 0) == (refused != 0));
            BUSTER_TEST(arguments, !stats.ceiling_reached && !stats.infinite_waits);
            if (refused)
            {
                BUSTER_TEST(arguments, failed.cleanup_error.v == ERROR_ACCESS_DENIED);
                ProcessCleanupResult pending = os_process_cleanup_snapshot();
                BUSTER_TEST(arguments, pending.pending_owners == 1 && pending.error_owner_id == failed.pending_windows_owner_id);
                ProcessCleanupResult resumed = os_process_cleanup_pending(5000000);
                BUSTER_TEST(arguments, resumed.resumed_owner_id == failed.pending_windows_owner_id && !resumed.active_owners);
            }
            BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before && !os_process_cleanup_snapshot().active_owners);
        }
    }
#if !BUSTER_SINGLE_THREADED
    // Both launches are admitted before any pending failure. One consuming wait
    // loses its arena/thread, then the other fails; process-global owners survive.
    ProcessSpawnResult concurrent[2];
    for (EACH_ARRAY_INDEX(index, concurrent))
    {
        concurrent[index] = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
            (ProcessSpawnOptions){.new_process_group = 1});
    }
    if (BUSTER_REQUIRE(arguments, concurrent[0].handle && concurrent[1].handle))
    {
        OsTestCleanupThread worker = {.spawn = concurrent[0]};
        OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){.callback = &os_test_cleanup_thread, .argument = &worker});
        if (BUSTER_REQUIRE(arguments, thread != 0))
        {
            BUSTER_TEST(arguments, os_thread_join(thread));
            BUSTER_TEST(arguments, worker.arena_destroyed && worker.waited.pending_windows_owner_id == concurrent[0].windows_owner_id);
            BUSTER_TEST(arguments, !worker.stats.ceiling_reached && !worker.stats.infinite_waits);
            os_process_cleanup_windows_test_begin(concurrent[1], OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION |
                OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY);
            ProcessWaitResult second = os_process_wait_deadline(arena, concurrent[1], 1);
            OsProcessCleanupWindowsTestStats stats = os_process_cleanup_windows_test_end();
            BUSTER_TEST(arguments, second.pending_windows_owner_id == concurrent[1].windows_owner_id && !stats.ceiling_reached);
            ProcessCleanupResult snapshot = os_process_cleanup_snapshot();
            BUSTER_TEST(arguments, snapshot.active_owners == 2 && snapshot.pending_owners == 2);
            os_process_cleanup_windows_test_begin(concurrent[1], OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION |
                OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY);
            ProcessCleanupResult first_retry = os_process_cleanup_pending(0);
            ProcessCleanupResult next_retry = os_process_cleanup_pending(5000000);
            stats = os_process_cleanup_windows_test_end();
            BUSTER_TEST(arguments, first_retry.resumed_owner_id == concurrent[1].windows_owner_id && first_retry.pending_owners == 2);
            BUSTER_TEST(arguments, next_retry.resumed_owner_id == concurrent[0].windows_owner_id && next_retry.pending_owners == 1);
            BUSTER_TEST(arguments, first_retry.error.v == ERROR_ACCESS_DENIED && first_retry.error_owner_id == concurrent[1].windows_owner_id);
            BUSTER_TEST(arguments, !stats.ceiling_reached && !stats.infinite_waits);
            ProcessCleanupResult recovered = os_process_cleanup_pending(5000000);
            BUSTER_TEST(arguments, !recovered.active_owners && !recovered.pending_owners);
        }
        else
        {
            os_process_wait_deadline(arena, concurrent[0], 1);
            os_process_wait_deadline(arena, concurrent[1], 1);
        }
    }
    else
    {
        for (EACH_ARRAY_INDEX(index, concurrent))
        {
            if (concurrent[index].handle) os_process_wait_deadline(arena, concurrent[index], 1);
        }
    }
    BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
#endif
#if !BUSTER_SINGLE_THREADED
    ProcessSpawnResult resume_spawns[2];
    for (EACH_ARRAY_INDEX(index, resume_spawns))
    {
        resume_spawns[index] = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
            (ProcessSpawnOptions){.new_process_group = 1});
    }
    for (EACH_ARRAY_INDEX(index, resume_spawns))
    {
        if (resume_spawns[index].handle)
        {
            os_process_cleanup_windows_test_begin(resume_spawns[index], OS_PROCESS_CLEANUP_TEST_REFUSE_TERMINATION |
                OS_PROCESS_CLEANUP_TEST_UNSIGNALED | OS_PROCESS_CLEANUP_TEST_JOB_NONEMPTY);
            ProcessWaitResult waited = os_process_wait_deadline(arena, resume_spawns[index], 1);
            OsProcessCleanupWindowsTestStats stats = os_process_cleanup_windows_test_end();
            BUSTER_TEST(arguments, waited.pending_windows_owner_id == resume_spawns[index].windows_owner_id && !stats.ceiling_reached);
        }
    }
    if (BUSTER_REQUIRE(arguments, resume_spawns[0].handle && resume_spawns[1].handle))
    {
        AtomicU64 start = 0;
        AtomicU64 ready_count = 0;
        OsTestCleanupResume workers[2] = {{.start = &start, .ready = &ready_count}, {.start = &start, .ready = &ready_count}};
        OsThreadHandle* threads[2] = {0};
        u32 started = 0;
        for (EACH_ARRAY_INDEX(index, threads))
        {
            threads[index] = os_thread_create((ThreadCreateOptions){.callback = &os_test_cleanup_resume_thread, .argument = &workers[index]});
            BUSTER_TEST(arguments, threads[index] != 0);
            started += threads[index] != 0;
        }
        while (atomic_u64_load(&ready_count) < started) os_test_sleep_milliseconds(1);
        atomic_u64_increment(&start);
        for (EACH_ARRAY_INDEX(index, threads))
        {
            if (threads[index]) BUSTER_TEST(arguments, os_thread_join(threads[index]));
        }
        if (started == 2)
        {
            BUSTER_TEST(arguments, workers[0].result.resumed_owner_id != workers[1].result.resumed_owner_id);
            for (EACH_ARRAY_INDEX(index, workers))
            {
                BUSTER_TEST(arguments, workers[index].result.resumed_owner_id == resume_spawns[0].windows_owner_id ||
                    workers[index].result.resumed_owner_id == resume_spawns[1].windows_owner_id);
            }
        }
    }
    // A failed test thread creation still leaves an owned, recoverable record.
    for (u32 index = 0; index < 2; index += 1) os_process_cleanup_pending(5000000);
    BUSTER_TEST(arguments, !os_process_cleanup_snapshot().active_owners && os_process_spawn_test_resource_count() == resources_before);
#endif
    // Native successful timeout cleanup, with and without a job.
    for (u32 grouped = 0; grouped < 2; grouped += 1)
    {
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
            (ProcessSpawnOptions){.new_process_group = grouped != 0});
        if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
        {
            ProcessWaitResult waited = os_process_wait_deadline(arena, spawn, 1);
            BUSTER_TEST(arguments, waited.timed_out && waited.termination_requested && !waited.process_tree_cleanup_failed);
            BUSTER_TEST(arguments, !waited.pending_windows_owner_id && !waited.cleanup_error.v);
        }
        BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
    }
    BUSTER_TEST(arguments, os_file_delete(ready));
    values[0] = S8("capture-small");
    environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
    ProcessSpawnResult exited = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
        (ProcessSpawnOptions){.new_process_group = 1, .capture = (u64)1 << STANDARD_STREAM_OUTPUT});
    if (BUSTER_REQUIRE(arguments, exited.handle != 0))
    {
        ProcessWaitResult waited = os_process_wait_sync(arena, exited);
        BUSTER_TEST(arguments, waited.result == PROCESS_RESULT_SUCCESS && !waited.termination_requested && !waited.process_tree_cleanup_failed);
        BUSTER_TEST(arguments, !waited.pending_windows_owner_id && waited.streams[STANDARD_STREAM_OUTPUT].length == 5);
    }
    BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
    // Shutdown permanently closes admission, so its controls own another host.
    values[0] = S8("cleanup-shutdown");
    environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
    ProcessSpawnResult shutdown = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
        (ProcessSpawnOptions){.new_process_group = 1});
    if (BUSTER_REQUIRE(arguments, shutdown.handle != 0))
    {
        ProcessWaitResult waited = os_process_wait_deadline(arena, shutdown, 30000000);
        BUSTER_TEST(arguments, waited.result == PROCESS_RESULT_SUCCESS && !waited.process_tree_cleanup_failed);
    }
    BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
    return result;
}

BUSTER_GLOBAL_LOCAL u64 os_test_shutdown_output_step(HANDLE pipe, bool* valid, u8* prefix, u64* prefix_length, u64 prefix_capacity)
{
    u64 result = 0;
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, 0, 0, 0, &available, 0))
    {
        *valid = *valid && GetLastError() == ERROR_BROKEN_PIPE;
    }
    else if (available)
    {
        u8 buffer[16 * 1024];
        DWORD requested = available < sizeof(buffer) ? available : (DWORD)sizeof(buffer);
        DWORD read_count = 0;
        bool read_ok = ReadFile(pipe, buffer, requested, &read_count, 0) != 0;
        *valid = *valid && read_ok && read_count != 0;
        if (read_ok)
        {
            result = read_count;
            u64 copied = read_count < prefix_capacity - *prefix_length ? read_count : prefix_capacity - *prefix_length;
            if (copied) memcpy(prefix + *prefix_length, buffer, (size_t)copied);
            *prefix_length += copied;
        }
    }
    return result;
}

typedef struct OsTestWindowsHandleCensus OsTestWindowsHandleCensus;
struct OsTestWindowsHandleCensus
{
    u32 count;
    u32 error;
    bool valid;
};

BUSTER_GLOBAL_LOCAL OsTestWindowsHandleCensus os_test_windows_handle_census(HANDLE process)
{
    OsTestWindowsHandleCensus result = {0};
    DWORD count = 0;
    result.valid = GetProcessHandleCount(process, &count) != 0;
    if (result.valid) result.count = count;
    else result.error = GetLastError();
    return result;
}

typedef struct OsTestWindowsHandleObservation OsTestWindowsHandleObservation;
struct OsTestWindowsHandleObservation
{
    u32 flags;
    u32 query_error;
    u32 compare_error;
    bool valid;
    bool same_object;
};

BUSTER_GLOBAL_LOCAL OsTestWindowsHandleObservation os_test_windows_handle_observe(HANDLE original, HANDLE witness)
{
    OsTestWindowsHandleObservation result = {0};
    DWORD flags = 0;
    result.valid = GetHandleInformation(original, &flags) != 0;
    if (result.valid)
    {
        result.flags = flags;
        // This API lives in KernelBase, outside the import set shared by the
        // native compiler matrix. Borrow its already loaded module; diagnostics
        // must not create a new library handle inside the census interval.
        typedef BOOL(WINAPI* CompareObjectHandlesProc)(HANDLE, HANDLE);
        CompareObjectHandlesProc compare = (CompareObjectHandlesProc)(void (*)(void))
            GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles");
        if (compare)
        {
            SetLastError(0);
            result.same_object = compare(original, witness) != 0;
            if (!result.same_object) result.compare_error = GetLastError();
        }
        else result.compare_error = ERROR_PROC_NOT_FOUND;
    }
    else result.query_error = GetLastError();
    return result;
}

// Only an explicit different-object result or a closed original proves this
// handle value no longer names the witnessed object. Observation failure does not.
BUSTER_GLOBAL_LOCAL bool os_test_windows_handle_released(OsTestWindowsHandleObservation observation)
{
    bool result = !observation.same_object &&
        ((observation.valid && observation.compare_error == ERROR_NOT_SAME_OBJECT) ||
         (!observation.valid && observation.query_error == ERROR_INVALID_HANDLE));
    return result;
}

enum { OS_TEST_WINDOWS_OWNER_DIAGNOSTIC_CAPACITY = 16 };

BUSTER_GLOBAL_LOCAL u64 os_test_windows_owner_diagnostic(UnitTestArguments* arguments, String8 mode, String8 phase)
{
    OsProcessCleanupWindowsTestOwner owners[OS_TEST_WINDOWS_OWNER_DIAGNOSTIC_CAPACITY] = {0};
    u64 count = os_process_cleanup_windows_test_inventory(owners, BUSTER_ARRAY_LENGTH(owners));
    u64 retained = count < BUSTER_ARRAY_LENGTH(owners) ? count : BUSTER_ARRAY_LENGTH(owners);
    arguments->show(arguments, S8("WINDOWS_SHUTDOWN_OWNER_INVENTORY_V1 mode={S8} phase={S8} owners={u64} rows={u64} complete={u32}\n"),
        mode, phase, count, retained, (u32)(count <= BUSTER_ARRAY_LENGTH(owners)));
    for (u64 index = 0; index < retained; index += 1)
    {
        OsProcessCleanupWindowsTestOwner owner = owners[index];
        arguments->show(arguments, S8("WINDOWS_SHUTDOWN_OWNER_ROW_V1 mode={S8} phase={S8} id={u64} state={u32} pending={u32} "
            "process={u64} job={u64} stdin={u64} stdout={u64} stderr={u64}\n"),
            mode, phase, owner.id, owner.state, (u32)owner.pending, (u64)owner.process, (u64)owner.job,
            (u64)owner.pipes[STANDARD_STREAM_INPUT], (u64)owner.pipes[STANDARD_STREAM_OUTPUT], (u64)owner.pipes[STANDARD_STREAM_ERROR]);
    }
    return count;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_windows_handle_observation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    OsTestWindowsHandleCensus entry = os_test_windows_handle_census(GetCurrentProcess());
    HANDLE owned = CreateEventW(0, TRUE, FALSE, 0);
    HANDLE unrelated = CreateEventW(0, TRUE, FALSE, 0);
    HANDLE witness = 0;
    bool duplicated = owned && DuplicateHandle(GetCurrentProcess(), owned, GetCurrentProcess(), &witness, 0, FALSE, DUPLICATE_SAME_ACCESS);
    if (BUSTER_REQUIRE(arguments, owned && unrelated && duplicated))
    {
        OsTestWindowsHandleCensus before = os_test_windows_handle_census(GetCurrentProcess());
        // A balanced unrelated replacement leaves the scalar census equal.
        // The independent duplicate still proves the owned object is live.
        bool unrelated_closed = CloseHandle(unrelated) != 0;
        BUSTER_TEST(arguments, unrelated_closed);
        if (unrelated_closed) unrelated = CreateEventW(0, TRUE, FALSE, 0);
        OsTestWindowsHandleCensus after = os_test_windows_handle_census(GetCurrentProcess());
        OsTestWindowsHandleObservation live = os_test_windows_handle_observe(owned, witness);
        OsTestWindowsHandleObservation different = os_test_windows_handle_observe(unrelated, witness);
        OsTestWindowsHandleObservation invalid_witness = os_test_windows_handle_observe(owned, 0);
        BUSTER_TEST(arguments, unrelated && before.valid && after.valid && before.count == after.count);
        BUSTER_TEST(arguments, live.valid && live.same_object && !live.query_error && !live.compare_error);
        BUSTER_TEST(arguments, different.valid && !different.same_object && !different.query_error &&
            different.compare_error == ERROR_NOT_SAME_OBJECT && os_test_windows_handle_released(different));
        BUSTER_TEST(arguments, !os_test_windows_handle_released(live));
        // The former !same_object && valid predicate accepted a failed compare.
        // Keep a real live original and invalidate only its independent witness.
        BUSTER_TEST(arguments, invalid_witness.valid && !invalid_witness.same_object &&
            invalid_witness.compare_error == ERROR_INVALID_HANDLE && !os_test_windows_handle_released(invalid_witness));
        HANDLE closed = owned;
        bool owned_closed = CloseHandle(owned) != 0;
        BUSTER_TEST(arguments, owned_closed);
        if (owned_closed) owned = 0;
        OsTestWindowsHandleObservation released = os_test_windows_handle_observe(closed, witness);
        BUSTER_TEST(arguments, !released.valid && !released.same_object && released.query_error == ERROR_INVALID_HANDLE &&
            os_test_windows_handle_released(released));
        OsTestWindowsHandleCensus failed = os_test_windows_handle_census(0);
        BUSTER_TEST(arguments, !failed.valid && failed.error != 0 && failed.count == 0);
        arguments->show(arguments, S8("WINDOWS_HANDLE_OBSERVATION_CONTROL_V1 count_before={u32} count_after={u32} "
            "balanced_live_same={u32} different_same={u32} closed_error={u32} failed_census_error={u32} invalid_witness_error={u32}\n"),
            before.count, after.count, (u32)live.same_object, (u32)different.same_object, released.query_error, failed.error, invalid_witness.compare_error);
    }
    if (witness) BUSTER_TEST(arguments, CloseHandle(witness) != 0);
    if (owned) BUSTER_TEST(arguments, CloseHandle(owned) != 0);
    if (unrelated) BUSTER_TEST(arguments, CloseHandle(unrelated) != 0);
    OsTestWindowsHandleCensus end_census = os_test_windows_handle_census(GetCurrentProcess());
    BUSTER_TEST(arguments, entry.valid && end_census.valid && entry.count == end_census.count);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_process_shutdown_exit_windows(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 argv[] = {program_state->input.arguments.pointer[0], S8("test"), S8("--module=integer_tests"), S8("--verbose=0")};
    String8 modes[] = {S8("cleanup-exit-pending"), S8("cleanup-exit-running"), S8("cleanup-exit-waiting"), S8("cleanup-exit-reserved"), S8("cleanup-exit-return")
#if !BUSTER_SINGLE_THREADED
        , S8("cleanup-exit-concurrent")
#endif
    };
    for (EACH_ARRAY_INDEX(index, modes))
    {
        String8 event_name8 = string_format(arena, S8("Local\\BusterCleanup{u64}_{u64}_{u64}"),
            (u64)GetCurrentProcessId(), os_now_microseconds(), index);
        String16Z event_name = {0};
        bool converted = string16z_from_string8_arena(arena, event_name8, &event_name);
        HANDLE release = converted ? CreateEventW(0, TRUE, FALSE, event_name.pointer) : 0;
        String8 ready = buster_test_temporary_unique_path(arena, S8("process-cleanup-exit-ready"), S8(".txt"));
        BUSTER_TEST(arguments, os_file_delete(ready));
        if (BUSTER_REQUIRE(arguments, release != 0))
        {
            String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_OS_CLEANUP_RELEASE_EVENT"), S8("BUSTER_TEST_JOBS"), S8("BUSTER_OS_PROCESS_READY"), S8("BUSTER_TEST_MODULE_GROUP")};
            String8 values[] = {modes[index], event_name8, S8("1"), ready, S8("")};
            OsTestEnvironment environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
            OsTestWindowsHandleCensus resources_before = os_test_windows_handle_census(GetCurrentProcess());
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
                (ProcessSpawnOptions){.new_process_group = 1, .capture = ((u64)1 << STANDARD_STREAM_ERROR) | ((u64)1 << STANDARD_STREAM_OUTPUT)});
            HANDLE originals[] = {(HANDLE)spawn.handle, (HANDLE)spawn.process_tree,
                (HANDLE)spawn.pipes[STANDARD_STREAM_OUTPUT][0], (HANDLE)spawn.pipes[STANDARD_STREAM_ERROR][0]};
            HANDLE witnesses[BUSTER_ARRAY_LENGTH(originals)] = {0};
            String8 roles[] = {S8("process"), S8("job"), S8("stdout"), S8("stderr")};
            if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
            {
                for (u32 handle_index = 0; handle_index < BUSTER_ARRAY_LENGTH(originals); handle_index += 1)
                {
                    bool duplicated = originals[handle_index] && DuplicateHandle(GetCurrentProcess(), originals[handle_index],
                        GetCurrentProcess(), &witnesses[handle_index], 0, FALSE, DUPLICATE_SAME_ACCESS);
                    u32 error = duplicated ? 0 : GetLastError();
                    BUSTER_TEST(arguments, duplicated);
                    arguments->show(arguments, S8("WINDOWS_SHUTDOWN_HANDLE_WITNESS_V1 mode={S8} owner_id={u64} role={S8} "
                        "original={u64} witness={u64} error={u32}\n"), modes[index], spawn.windows_owner_id, roles[handle_index],
                        (u64)originals[handle_index], (u64)witnesses[handle_index], error);
                }
                (void)os_test_windows_owner_diagnostic(arguments, modes[index], S8("before-wait"));
                DWORD available = 0;
                bool output_valid = true;
                u64 early_stdout_bytes = 0;
                u8 stdout_prefix[512] = {0};
                u64 stdout_prefix_length = 0;
                u64 receipt_deadline = os_now_microseconds() + 30000000;
                while (!available && os_now_microseconds() < receipt_deadline && WaitForSingleObject((HANDLE)spawn.handle, 0) == WAIT_TIMEOUT)
                {
                    // Normal-return suite setup can produce more than an
                    // anonymous pipe holds. Keep stdout flowing while observing
                    // stderr, without invoking parent process/job termination.
                    early_stdout_bytes += os_test_shutdown_output_step((HANDLE)spawn.pipes[STANDARD_STREAM_OUTPUT][0], &output_valid,
                        stdout_prefix, &stdout_prefix_length, sizeof(stdout_prefix));
                    if (!PeekNamedPipe((HANDLE)spawn.pipes[STANDARD_STREAM_ERROR][0], 0, 0, 0, &available, 0)) break;
                    os_test_sleep_milliseconds(1);
                }
                bool host_alive = WaitForSingleObject((HANDLE)spawn.handle, 0) == WAIT_TIMEOUT;
                BUSTER_TEST(arguments, available != 0 && host_alive);
                u8 receipt[256] = {0};
                DWORD read_count = 0;
                if (available)
                {
                    DWORD requested = available < sizeof(receipt) ? available : (DWORD)sizeof(receipt);
                    BUSTER_TEST(arguments, ReadFile((HANDLE)spawn.pipes[STANDARD_STREAM_ERROR][0], receipt, requested, &read_count, 0) != 0);
                    String8 text = {(char8*)receipt, read_count};
                    BUSTER_TEST(arguments, string_starts_with_sequence(text, S8("WINDOWS_PROCESS_SHUTDOWN_REFUSED_V1 active=")));
                    BUSTER_TEST(arguments, string_first_sequence(text, S8(" owner_id=")) != (u64)-1 && string_first_sequence(text, S8(" native_error=")) != (u64)-1);
                }
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information = {0};
                bool queried = QueryInformationJobObject((HANDLE)spawn.process_tree, JobObjectBasicAccountingInformation,
                    &information, sizeof(information), 0) != 0;
                BUSTER_TEST(arguments, queried && information.ActiveProcesses >= 2);
                DWORD active_before = queried ? information.ActiveProcesses : 0;
                BUSTER_TEST(arguments, SetEvent(release) != 0);
                // Observe host exit and an empty outer job BEFORE the consuming
                // parent wait can terminate descendants and mask a broken hook.
                DWORD host_wait = WAIT_TIMEOUT;
                u64 host_deadline = os_now_microseconds() + 30000000;
                while (host_wait == WAIT_TIMEOUT && os_now_microseconds() < host_deadline)
                {
                    early_stdout_bytes += os_test_shutdown_output_step((HANDLE)spawn.pipes[STANDARD_STREAM_OUTPUT][0], &output_valid,
                        stdout_prefix, &stdout_prefix_length, sizeof(stdout_prefix));
                    host_wait = WaitForSingleObject((HANDLE)spawn.handle, 1);
                }
                BUSTER_TEST(arguments, host_wait == WAIT_OBJECT_0 && output_valid);
                bool empty = false;
                u64 cleanup_deadline = os_now_microseconds() + 5000000;
                while (queried && !empty && os_now_microseconds() < cleanup_deadline)
                {
                    queried = QueryInformationJobObject((HANDLE)spawn.process_tree, JobObjectBasicAccountingInformation,
                        &information, sizeof(information), 0) != 0;
                    empty = queried && information.ActiveProcesses == 0;
                    if (queried && !empty) os_test_sleep_milliseconds(1);
                }
                BUSTER_TEST(arguments, queried && empty);
                ProcessWaitResult waited = os_process_wait_deadline(arena, spawn, 30000000);
                BUSTER_TEST(arguments, !waited.timed_out && !waited.process_tree_cleanup_failed && !waited.pending_windows_owner_id);
                BUSTER_TEST(arguments, waited.result == PROCESS_RESULT_FAILED && waited.platform_status == PROCESS_RESULT_FAILED);
                if (string_equal(modes[index], S8("cleanup-exit-return")))
                {
                    ByteSlice tail = waited.streams[STANDARD_STREAM_OUTPUT];
                    u64 copied = tail.length < sizeof(stdout_prefix) - stdout_prefix_length ? tail.length : sizeof(stdout_prefix) - stdout_prefix_length;
                    if (copied) memcpy(stdout_prefix + stdout_prefix_length, tail.pointer, (size_t)copied);
                    stdout_prefix_length += copied;
                    String8 output = {(char8*)stdout_prefix, stdout_prefix_length};
                    BUSTER_TEST(arguments, string_first_sequence(output, S8("Unit tests (1 of ")) != BUSTER_STRING_NO_MATCH);
                    BUSTER_TEST(arguments, string_first_sequence(output, S8("modules selected)")) != BUSTER_STRING_NO_MATCH);
                }
                arguments->show(arguments, S8("WINDOWS_SHUTDOWN_CONTROL_V1 mode={S8} early_stdout_bytes={u64} receipt_bytes={u32} "
                    "host_alive={u32} active_before={u32} host_wait={u32} empty_before_parent_wait={u32} timed_out={u32} status={u32}\n"),
                    modes[index], early_stdout_bytes, read_count, (u32)host_alive, active_before,
                    (u32)host_wait, (u32)empty, (u32)waited.timed_out, waited.platform_status);
            }
            for (u32 handle_index = 0; handle_index < BUSTER_ARRAY_LENGTH(originals); handle_index += 1)
            {
                if (witnesses[handle_index])
                {
                    OsTestWindowsHandleObservation observation = os_test_windows_handle_observe(originals[handle_index], witnesses[handle_index]);
                    arguments->show(arguments, S8("WINDOWS_SHUTDOWN_HANDLE_OBSERVATION_V1 mode={S8} owner_id={u64} role={S8} "
                        "original={u64} witness={u64} valid={u32} same_object={u32} flags={u32} query_error={u32} compare_error={u32}\n"),
                        modes[index], spawn.windows_owner_id, roles[handle_index], (u64)originals[handle_index],
                        (u64)witnesses[handle_index], (u32)observation.valid, (u32)observation.same_object,
                        observation.flags, observation.query_error, observation.compare_error);
                    BUSTER_TEST(arguments, os_test_windows_handle_released(observation));
                    BUSTER_TEST(arguments, CloseHandle(witnesses[handle_index]) != 0);
                }
            }
            (void)os_test_windows_owner_diagnostic(arguments, modes[index], S8("after-wait"));
            OsTestWindowsHandleCensus resources_after = os_test_windows_handle_census(GetCurrentProcess());
            arguments->show(arguments, S8("WINDOWS_SHUTDOWN_HANDLE_CENSUS_V1 mode={S8} before={u32} before_valid={u32} before_error={u32} "
                "after={u32} after_valid={u32} after_error={u32}\n"), modes[index], resources_before.count, (u32)resources_before.valid,
                resources_before.error, resources_after.count, (u32)resources_after.valid, resources_after.error);
            BUSTER_TEST(arguments, resources_before.valid && resources_after.valid);
            BUSTER_TEST(arguments, resources_after.count == resources_before.count);
            BUSTER_TEST(arguments, CloseHandle(release) != 0);
        }
        BUSTER_TEST(arguments, os_file_delete(ready));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_process_deadline_windows(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 arena_position = arena->position;
    String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
    String8 ready = buster_test_temporary_path(arena, S8("process-deadline-hot-writer-ready"), S8(".txt"));
    String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_OS_PROCESS_READY"), S8("BUSTER_TEST_JOBS")};
    String8 values[] = {S8("deadline-hot-writer"), ready, S8("1")};
    OsTestEnvironment environment = os_test_environment(arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
    ProcessSpawnOptions options = {
        .capture = (u64)1 << STANDARD_STREAM_OUTPUT,
        .new_process_group = 1,
    };
    u64 resources_before = os_process_spawn_test_resource_count();
    BUSTER_TEST(arguments, os_file_delete(ready));
    ProcessSpawnResult hot_spawn =
        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), environment.keys, environment.values, options);
    BUSTER_TEST(arguments, hot_spawn.failure == PROCESS_SPAWN_FAILURE_NONE && hot_spawn.error.v == 0);
    if (hot_spawn.handle)
    {
        u32 polls = 0;
        while (polls < 3000 && !os_test_regular_file_exists(ready))
        {
            os_test_sleep_milliseconds(1);
            polls += 1;
        }
        bool ready_observed = os_test_regular_file_exists(ready);
        BUSTER_TEST(arguments, ready_observed);
        os_process_wait_windows_test_begin(1000000, 30000, 128);
        ProcessWaitResult waited = os_process_wait_deadline(arena, hot_spawn, 100000);
        OsProcessWaitWindowsTestStats stats = os_process_wait_windows_test_end();
        BUSTER_TEST(arguments, waited.timed_out && waited.termination_requested && waited.forcibly_terminated);
        BUSTER_TEST(arguments, !waited.process_tree_cleanup_failed);
        BUSTER_TEST(arguments, !stats.scan_limit_reached && stats.scans >= 3 &&
                               stats.progress_scans > 0 && stats.child_polls == stats.scans &&
                               stats.clock_calls == stats.scans + 1);
        BUSTER_TEST(arguments, waited.observed_bytes[STANDARD_STREAM_OUTPUT] >= 4096 &&
                               waited.streams[STANDARD_STREAM_OUTPUT].length >= 4096);
        if (waited.streams[STANDARD_STREAM_OUTPUT].length >= 4096 &&
            waited.streams[STANDARD_STREAM_OUTPUT].pointer)
        {
            BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_OUTPUT].pointer[0] == 'H');
        }
        BUSTER_TEST(arguments, hot_spawn.process_tree != 0);
    }
    BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
    BUSTER_TEST(arguments, os_file_delete(ready));

    String8 tree_ready = buster_test_temporary_path(arena, S8("process-deadline-final-snapshot-ready"), S8(".txt"));
    String8 tree_values[] = {S8("deadline-hot-writer-parent"), tree_ready, S8("1")};
    OsTestEnvironment tree_environment = os_test_environment(arena, keys, tree_values, BUSTER_ARRAY_LENGTH(keys));
    ProcessSpawnOptions tree_options = options;
    tree_options.capture |= (u64)1 << STANDARD_STREAM_ERROR;
    BUSTER_TEST(arguments, os_file_delete(tree_ready));
    ProcessSpawnResult tree_spawn =
        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), tree_environment.keys, tree_environment.values, tree_options);
    BUSTER_TEST(arguments, tree_spawn.failure == PROCESS_SPAWN_FAILURE_NONE && tree_spawn.error.v == 0);
    bool tree_ready_observed = false;
    bool leader_exited = false;
    if (tree_spawn.handle)
    {
        u32 polls = 0;
        while (polls < 3000 && !os_test_regular_file_exists(tree_ready))
        {
            os_test_sleep_milliseconds(1);
            polls += 1;
        }
        tree_ready_observed = os_test_regular_file_exists(tree_ready);
        polls = 0;
        DWORD process_wait = WAIT_TIMEOUT;
        while (tree_ready_observed && polls < 3000 && process_wait == WAIT_TIMEOUT)
        {
            process_wait = WaitForSingleObject((HANDLE)tree_spawn.handle, 0);
            if (process_wait == WAIT_TIMEOUT)
            {
                os_test_sleep_milliseconds(1);
                polls += 1;
            }
        }
        leader_exited = process_wait == WAIT_OBJECT_0;
        BUSTER_TEST(arguments, tree_ready_observed && leader_exited);
        os_process_wait_windows_test_begin(1000000, 30000, 128);
        ProcessWaitResult waited = os_process_wait_deadline(arena, tree_spawn, 1);
        OsProcessWaitWindowsTestStats stats = os_process_wait_windows_test_end();
        BUSTER_TEST(arguments, !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS &&
                               waited.platform_status == 0 && !waited.process_tree_cleanup_failed);
        BUSTER_TEST(arguments, waited.observed_bytes[STANDARD_STREAM_OUTPUT] > 0 &&
                               waited.observed_bytes[STANDARD_STREAM_ERROR] > 0 &&
                               waited.captured_total == waited.observed_total);
        ByteSlice tree_output = waited.streams[STANDARD_STREAM_OUTPUT];
        ByteSlice tree_error = waited.streams[STANDARD_STREAM_ERROR];
        bool tree_bytes_match = tree_output.pointer != 0 && tree_error.pointer != 0 &&
                                tree_output.length == waited.observed_bytes[STANDARD_STREAM_OUTPUT] &&
                                tree_error.length == waited.observed_bytes[STANDARD_STREAM_ERROR];
        for (u64 index = 0; tree_bytes_match && index < tree_output.length; index += 1)
        {
            tree_bytes_match = tree_output.pointer[index] == 'H';
        }
        for (u64 index = 0; tree_bytes_match && index < tree_error.length; index += 1)
        {
            tree_bytes_match = tree_error.pointer[index] == 'E';
        }
        BUSTER_TEST(arguments, tree_bytes_match);
        BUSTER_TEST(arguments, stats.scans > 0 && !stats.scan_limit_reached &&
                               stats.progress_scans == stats.scans && stats.child_polls == stats.scans &&
                               stats.leader_exit_observations == stats.scans && stats.clock_calls == 2);
        BUSTER_TEST(arguments, tree_spawn.process_tree != 0);
    }
    BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
    BUSTER_TEST(arguments, os_file_delete(tree_ready));

    String8 large_values[] = {S8("deadline-large-output"), ready, S8("1")};
    OsTestEnvironment large_environment = os_test_environment(arena, keys, large_values, BUSTER_ARRAY_LENGTH(keys));
    ProcessSpawnResult large_spawn =
        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), large_environment.keys, large_environment.values, options);
    BUSTER_TEST(arguments, large_spawn.failure == PROCESS_SPAWN_FAILURE_NONE && large_spawn.error.v == 0);
    if (large_spawn.handle)
    {
        u64 large_output_length = OS_TEST_PROCESS_DEADLINE_LARGE_OUTPUT_LENGTH;
        os_process_wait_windows_test_begin(1000000, 0, 128);
        ProcessWaitResult waited = os_process_wait_deadline(arena, large_spawn, 1000000);
        OsProcessWaitWindowsTestStats stats = os_process_wait_windows_test_end();
        ByteSlice output = waited.streams[STANDARD_STREAM_OUTPUT];
        bool bytes_match = output.pointer != 0 && output.length == large_output_length &&
                           waited.observed_bytes[STANDARD_STREAM_OUTPUT] == large_output_length &&
                           waited.captured_total == large_output_length && waited.observed_total == large_output_length;
        for (u64 index = 0; bytes_match && index < output.length; index += 1)
        {
            bytes_match = output.pointer[index] == 'L';
        }
        BUSTER_TEST(arguments, !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS && waited.platform_status == 0);
        BUSTER_TEST(arguments, bytes_match);
        BUSTER_TEST(arguments, stats.scans >= 3 && !stats.scan_limit_reached &&
                               stats.child_polls == stats.scans);
    }
    BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);

    String8 small_values[] = {S8("capture-small"), ready, S8("1")};
    OsTestEnvironment small_environment = os_test_environment(arena, keys, small_values, BUSTER_ARRAY_LENGTH(keys));
    ProcessSpawnResult small_spawn =
        os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), small_environment.keys, small_environment.values, options);
    BUSTER_TEST(arguments, small_spawn.failure == PROCESS_SPAWN_FAILURE_NONE && small_spawn.error.v == 0);
    if (small_spawn.handle)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arena, small_spawn, 0);
        BUSTER_TEST(arguments, !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS && waited.platform_status == 0);
        BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_OUTPUT].length == 5 &&
                               waited.streams[STANDARD_STREAM_OUTPUT].pointer != 0 &&
                               memory_compare(waited.streams[STANDARD_STREAM_OUTPUT].pointer, "owned", 5));
    }
    BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
    BUSTER_TEST(arguments, os_file_delete(ready));
    arena_set_position(arena, arena_position);
    return result;
}
#endif

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
// Observe a real child's exit without reaping before injecting drain errors.
// The five-byte payload fits the pipe: failure cannot turn a still-writing
// child into SIGPIPE and obscure the successful-child regression witness.
typedef struct OsTestCaptureNativeFault OsTestCaptureNativeFault;
struct OsTestCaptureNativeFault
{
    OsProcessCaptureEvent event;
    u32 call_index;
    u64 bytes;
};

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_native(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    OsTestCaptureNativeFault failures[] = {
        {OS_PROCESS_CAPTURE_EVENT_COUNT, 0, 5},
        {OS_PROCESS_CAPTURE_WAIT_FAILED, 0, 0},
        {OS_PROCESS_CAPTURE_READ_FAILED, 0, 0},
        {OS_PROCESS_CAPTURE_READ_FAILED, 1, 5},
        {OS_PROCESS_CAPTURE_CLOSE_FAILED, 0, 5},
        {OS_PROCESS_CAPTURE_EVENT_COUNT, 0, 5},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(failures); index += 1)
    {
        u64 arena_position = arguments->arena->position;
        String8 keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_TEST_JOBS")};
        String8 values[] = {S8("capture-small"), S8("1")};
        OsTestEnvironment environment = os_test_environment(arguments->arena, keys, values, BUSTER_ARRAY_LENGTH(keys));
        String8 argv[] = {program_state->input.arguments.pointer[0], S8("test")};
        u64 resources_before = os_process_spawn_test_resource_count();
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(argv), environment.keys, environment.values,
            (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_OUTPUT});
        if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
        {
            siginfo_t observed = {0};
            int observed_status = 0;
            u64 deadline = os_now_microseconds() + 30000000;
            do
            {
                observed_status = waitid(P_PID, (id_t)(u64)spawn.handle, &observed, WEXITED | WNOHANG | WNOWAIT);
                if (!observed.si_pid && (!observed_status || errno == EINTR)) { os_test_sleep_milliseconds(1); }
            } while (!observed.si_pid && (!observed_status || errno == EINTR) && os_now_microseconds() < deadline);
            bool child_succeeded = !observed_status && observed.si_pid && observed.si_code == CLD_EXITED && !observed.si_status;
            BUSTER_TEST(arguments, child_succeeded);
            bool inject = child_succeeded && failures[index].event != OS_PROCESS_CAPTURE_EVENT_COUNT;
            if (inject) { os_process_capture_test_fail_on_call(failures[index].event, failures[index].call_index); }
            ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, spawn, 30000000);
            if (inject) { BUSTER_TEST(arguments, os_process_capture_test_end()); }
            BUSTER_TEST(arguments, !waited.timed_out && waited.platform_status == 0);
            BUSTER_TEST(arguments, waited.result == (inject ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS));
            BUSTER_TEST(arguments, (bool)waited.capture_failed == inject);
            u64 expected_bytes = failures[index].bytes;
            BUSTER_TEST(arguments, waited.observed_total == expected_bytes && waited.captured_total == expected_bytes &&
                waited.streams[STANDARD_STREAM_OUTPUT].length == expected_bytes);
            if (expected_bytes)
            {
                BUSTER_TEST(arguments, memory_compare(waited.streams[STANDARD_STREAM_OUTPUT].pointer, "owned", 5));
            }
        }
        BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
        arena_set_position(arguments->arena, arena_position);
    }
    return result;
}
#endif

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS

// Back-to-back group waits of short captured children must neither stall nor
// report a timeout: each child exits at once, so every wait has to observe the
// leader's exit and prove the group quiescent well inside the deadline (#2716).
// The churn runs on the calling thread; lane_run would leave the persistent
// gang running into later modules' serial table initialization.
enum
{
    OS_TEST_GROUP_CHURN_SPAWNS = 200,
    OS_TEST_GROUP_CHURN_DEADLINE_US = 30000000,
};

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_process_group_churn(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 command[] = {S8("true")};
    u64 failures = 0;
    u64 slowest_us = 0;
    u32 first_status = 0;
    bool first_timed_out = false;
    for (u32 spawn_index = 0; spawn_index < OS_TEST_GROUP_CHURN_SPAWNS; spawn_index += 1)
    {
        u64 position = arguments->arena->position;
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
            (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true, .search_path = true,
                                  .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
        bool ok = spawn.handle != 0;
        if (ok)
        {
            u64 started = os_now_microseconds();
            ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, spawn, OS_TEST_GROUP_CHURN_DEADLINE_US);
            u64 elapsed = os_now_microseconds() - started;
            ok = !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS;
            slowest_us = elapsed > slowest_us ? elapsed : slowest_us;
            if (!ok && !failures)
            {
                first_status = waited.platform_status;
                first_timed_out = waited.timed_out;
            }
        }
        failures += !ok;
        arena_set_position(arguments->arena, position);
    }
    BUSTER_TEST(arguments, failures == 0);
    if (failures)
    {
        arguments->show(arguments, S8("group churn: {u64} failures, first timeout {u32} status {u32}, slowest {u64} us\n"), failures,
            (u32)first_timed_out, first_status, slowest_us);
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL bool os_test_image_retention_write_elf(String8 path, u16 image_type)
{
    u8 header[64] = {0};
    header[0] = 0x7f;
    header[1] = 'E';
    header[2] = 'L';
    header[3] = 'F';
    header[4] = 2;
    header[5] = 1;
    header[6] = 1;
    header[16] = (u8)image_type;
    header[17] = (u8)(image_type >> 8);
    return file_write(path, (ByteSlice){.pointer = header, .length = sizeof(header)});
}

BUSTER_GLOBAL_LOCAL bool os_test_image_retention_write_macho(String8 path, u32 file_type)
{
    u8 header[64] = {0};
    header[0] = 0xcf;
    header[1] = 0xfa;
    header[2] = 0xed;
    header[3] = 0xfe;
    header[12] = (u8)file_type;
    header[13] = (u8)(file_type >> 8);
    header[14] = (u8)(file_type >> 16);
    header[15] = (u8)(file_type >> 24);
    return file_write(path, (ByteSlice){.pointer = header, .length = sizeof(header)});
}

BUSTER_GLOBAL_LOCAL bool os_test_image_retention_write_pe(String8 path, u16 characteristics)
{
    u8 image[88] = {0};
    image[0] = 'M';
    image[1] = 'Z';
    image[0x3c] = 64;
    image[64] = 'P';
    image[65] = 'E';
    image[86] = (u8)characteristics;
    image[87] = (u8)(characteristics >> 8);
    return file_write(path, (ByteSlice){.pointer = image, .length = sizeof(image)});
}

BUSTER_GLOBAL_LOCAL bool os_test_image_retention_exists(String8 path)
{
    FileStats stats = os_path_followed_stats(path);
    return stats.valid && stats.kind != OS_FILE_KIND_MISSING;
}

BUSTER_GLOBAL_LOCAL String8 os_test_image_retention_relative_path(Arena* arena, String8 absolute_path)
{
    String8 result = {0};
    String8 current_directory = os_path_absolute_lexical(arena, S8("."), false);
#if defined(_WIN32)
    char8 separator = '\\';
    bool same_root = current_directory.length >= 3 && absolute_path.length >= 3 &&
                     (current_directory.pointer[0] == absolute_path.pointer[0] ||
                      (current_directory.pointer[0] >= 'A' && current_directory.pointer[0] <= 'Z' &&
                       current_directory.pointer[0] + ('a' - 'A') == absolute_path.pointer[0]) ||
                      (absolute_path.pointer[0] >= 'A' && absolute_path.pointer[0] <= 'Z' &&
                       absolute_path.pointer[0] + ('a' - 'A') == current_directory.pointer[0])) &&
                     current_directory.pointer[1] == ':' && absolute_path.pointer[1] == ':' &&
                     current_directory.pointer[2] == separator && absolute_path.pointer[2] == separator;
    u64 root_length = same_root ? 3 : 0;
#else
    char8 separator = '/';
    if (current_directory.length >= 2 && current_directory.pointer[current_directory.length - 2] == separator &&
        current_directory.pointer[current_directory.length - 1] == '.')
    {
        current_directory.length -= 2;
    }
    bool same_root = current_directory.length && absolute_path.length && current_directory.pointer[0] == separator &&
                     absolute_path.pointer[0] == separator;
    u64 root_length = same_root ? 1 : 0;
#endif
    while (current_directory.length > root_length && current_directory.pointer[current_directory.length - 1] == separator)
    {
        current_directory.length -= 1;
    }

    if (same_root)
    {
        u64 common_length = root_length;
        while (common_length < current_directory.length && common_length < absolute_path.length)
        {
            char8 current_byte = current_directory.pointer[common_length];
            char8 absolute_byte = absolute_path.pointer[common_length];
#if defined(_WIN32)
            if (current_byte >= 'A' && current_byte <= 'Z')
            {
                current_byte = (char8)(current_byte + ('a' - 'A'));
            }
            if (absolute_byte >= 'A' && absolute_byte <= 'Z')
            {
                absolute_byte = (char8)(absolute_byte + ('a' - 'A'));
            }
#endif
            if (current_byte != absolute_byte)
            {
                break;
            }
            common_length += 1;
        }

        bool current_is_parent = common_length == current_directory.length &&
                                 (common_length == absolute_path.length || absolute_path.pointer[common_length] == separator);
        if (!current_is_parent)
        {
            while (common_length > root_length && current_directory.pointer[common_length - 1] != separator)
            {
                common_length -= 1;
            }
        }

        u64 parent_count = 0;
        bool in_component = false;
        for (u64 index = common_length; index <= current_directory.length; index += 1)
        {
            bool at_end = index == current_directory.length;
            bool is_separator = !at_end && current_directory.pointer[index] == separator;
            if (at_end || is_separator)
            {
                parent_count += in_component;
                in_component = false;
            }
            else
            {
                in_component = true;
            }
        }

        u64 destination_index = common_length;
        while (destination_index < absolute_path.length && absolute_path.pointer[destination_index] == separator)
        {
            destination_index += 1;
        }
        u64 destination_length = absolute_path.length - destination_index;
        u64 relative_length = parent_count * 3 + destination_length;
        if (relative_length)
        {
            char8* pointer = (char8*)arena_allocate_bytes(arena, relative_length, 1);
            u64 cursor = 0;
            for (u64 parent_index = 0; parent_index < parent_count; parent_index += 1)
            {
                pointer[cursor++] = '.';
                pointer[cursor++] = '.';
                pointer[cursor++] = separator;
            }
            for (u64 index = destination_index; index < absolute_path.length; index += 1)
            {
                pointer[cursor++] = absolute_path.pointer[index];
            }
            result = (String8){.pointer = pointer, .length = relative_length};
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 os_test_image_retention_dot_path(Arena* arena, String8 absolute_path)
{
#if defined(_WIN32)
    char8 separator = '\\';
#else
    char8 separator = '/';
#endif
    u64 component_start = 0;
    for (u64 index = 0; index < absolute_path.length; index += 1)
    {
#if defined(_WIN32)
        if (absolute_path.pointer[index] == '/' || absolute_path.pointer[index] == '\\')
        {
            separator = absolute_path.pointer[index];
            component_start = index + 1;
        }
#else
        if (absolute_path.pointer[index] == separator)
        {
            component_start = index + 1;
        }
#endif
    }
    String8 result = absolute_path;
    if (component_start < absolute_path.length)
    {
        u64 result_length = absolute_path.length + 2;
        char8* pointer = (char8*)arena_allocate_bytes(arena, result_length, 1);
        u64 cursor = 0;
        for (u64 index = 0; index < component_start; index += 1)
        {
            pointer[cursor++] = absolute_path.pointer[index];
        }
        pointer[cursor++] = '.';
        pointer[cursor++] = separator;
        for (u64 index = component_start; index < absolute_path.length; index += 1)
        {
            pointer[cursor++] = absolute_path.pointer[index];
        }
        result = (String8){.pointer = pointer, .length = result_length};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_image_retention(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    bool previous_active = os_image_retention_set_active(false);
    String8 inactive_text = buster_test_temporary_path(arguments->arena, S8("image-retention-inactive-text"), S8(".txt"));
    String8 inactive_shared = buster_test_temporary_path(arguments->arena, S8("image-retention-inactive-elf"), S8(".bin"));
    String8 inactive_registered = buster_test_temporary_path(arguments->arena, S8("image-retention-inactive-registered"), S8(".txt"));
    String8 active_registered = buster_test_temporary_path(arguments->arena, S8("image-retention-registered"), S8(".txt"));
    String8 active_relative_temporary = buster_test_temporary_path(arguments->arena, S8("image-retention-relative"), S8(".bin"));
    String8 active_relative = os_test_image_retention_relative_path(arguments->arena, active_relative_temporary);
    if (!active_relative.length)
    {
        active_relative = os_test_image_retention_dot_path(arguments->arena, active_relative_temporary);
    }
    String8 active_relative_absolute = os_path_absolute_lexical(arguments->arena, active_relative, false);
    String8 active_elf = buster_test_temporary_path(arguments->arena, S8("image-retention-elf-dyn"), S8(".bin"));
    String8 active_macho = buster_test_temporary_path(arguments->arena, S8("image-retention-macho-dylib"), S8(".bin"));
    String8 active_pe = buster_test_temporary_path(arguments->arena, S8("image-retention-pe-dll"), S8(".bin"));
    String8 active_rel = buster_test_temporary_path(arguments->arena, S8("image-retention-elf-rel"), S8(".bin"));
    String8 active_exec = buster_test_temporary_path(arguments->arena, S8("image-retention-elf-exec"), S8(".bin"));
    String8 active_text = buster_test_temporary_path(arguments->arena, S8("image-retention-active-text"), S8(".txt"));
    String8 injected = buster_test_temporary_path(arguments->arena, S8("image-retention-injected-delete"), S8(".txt"));
    String8 retained_directory = buster_test_temporary_path(arguments->arena, S8("image-retention-directory"), S8(""));
    String8 directory_image = string_format(arguments->arena, S8("{S8}/image.bin"), retained_directory);
    String8 directory_text = string_format(arguments->arena, S8("{S8}/text.txt"), retained_directory);
    String8 empty_directory = buster_test_temporary_path(arguments->arena, S8("image-retention-empty-directory"), S8(""));
    String8 empty_directory_text = string_format(arguments->arena, S8("{S8}/text.txt"), empty_directory);
    String8 text = S8("ordinary text");
    OsImageRetentionCounters before = os_image_retention_counters();

    BUSTER_TEST(arguments, file_write(inactive_text, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    BUSTER_TEST(arguments, os_test_image_retention_write_elf(inactive_shared, 3));
    BUSTER_TEST(arguments, file_write(inactive_registered, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    os_image_retention_register(inactive_registered);
    BUSTER_TEST(arguments, os_file_delete(inactive_text) && !os_test_image_retention_exists(inactive_text));
    BUSTER_TEST(arguments, os_file_delete(inactive_shared) && !os_test_image_retention_exists(inactive_shared));
    BUSTER_TEST(arguments, os_file_delete(inactive_registered) && !os_test_image_retention_exists(inactive_registered));

    os_image_retention_set_active(true);
    BUSTER_TEST(arguments, file_write(active_registered, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    os_image_retention_register(active_registered);
    BUSTER_TEST(arguments, os_file_delete(active_registered) && os_test_image_retention_exists(active_registered));
    BUSTER_TEST(arguments, file_write(active_relative, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    os_image_retention_register(active_relative);
    BUSTER_TEST(arguments, active_relative_absolute.length &&
                           os_file_delete(active_relative_absolute) && os_test_image_retention_exists(active_relative_absolute));
    BUSTER_TEST(arguments, os_test_image_retention_write_elf(active_elf, 3));
    // The followed probe reports the size of the file it reached.
    BUSTER_TEST(arguments, os_path_followed_stats(active_elf).size == 64);
    BUSTER_TEST(arguments, os_file_delete(active_elf) && os_test_image_retention_exists(active_elf));
    BUSTER_TEST(arguments, os_test_image_retention_write_macho(active_macho, 6));
    BUSTER_TEST(arguments, os_file_delete(active_macho) && os_test_image_retention_exists(active_macho));
    BUSTER_TEST(arguments, os_test_image_retention_write_pe(active_pe, 0x2000));
    BUSTER_TEST(arguments, os_file_delete(active_pe) && os_test_image_retention_exists(active_pe));
    BUSTER_TEST(arguments, os_test_image_retention_write_elf(active_rel, 1));
    BUSTER_TEST(arguments, os_file_delete(active_rel) && !os_test_image_retention_exists(active_rel));
    BUSTER_TEST(arguments, os_test_image_retention_write_elf(active_exec, 2));
    BUSTER_TEST(arguments, os_file_delete(active_exec) && !os_test_image_retention_exists(active_exec));
    BUSTER_TEST(arguments, file_write(active_text, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    BUSTER_TEST(arguments, os_file_delete(active_text) && !os_test_image_retention_exists(active_text));

    BUSTER_TEST(arguments, file_write(injected, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    os_image_retention_register(injected);
    OsFileTestStep injected_delete[] = {{OS_FILE_TEST_DELETE, OS_FILE_TEST_ERROR, 12345}};
    os_file_test_begin(injected, injected_delete, BUSTER_ARRAY_LENGTH(injected_delete));
    OsError injected_result = os_file_delete_checked(injected);
    u32 consumed = os_file_test_end();
    BUSTER_TEST(arguments, injected_result.v == 12345 && consumed == 1 && os_test_image_retention_exists(injected));

    BUSTER_TEST(arguments, os_make_directory_attempt(retained_directory));
    BUSTER_TEST(arguments, os_test_image_retention_write_elf(directory_image, 3));
    BUSTER_TEST(arguments, file_write(directory_text, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    OsImageRetentionCounters before_directory_delete = os_image_retention_counters();
    BUSTER_TEST(arguments, os_directory_delete(retained_directory));
    OsImageRetentionCounters after_directory_delete = os_image_retention_counters();
    BUSTER_TEST(arguments, os_test_image_retention_exists(retained_directory) &&
                           os_test_image_retention_exists(directory_image) && os_test_image_retention_exists(directory_text));
    BUSTER_TEST(arguments, after_directory_delete.retained_directories - before_directory_delete.retained_directories == 1);
    BUSTER_TEST(arguments, os_make_directory_attempt(empty_directory));
    BUSTER_TEST(arguments, file_write(empty_directory_text, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    BUSTER_TEST(arguments, os_directory_delete(empty_directory) && !os_test_image_retention_exists(empty_directory));

    OsImageRetentionCounters after_retained_files = os_image_retention_counters();
    BUSTER_TEST(arguments, after_retained_files.registered - before.registered >= 3);
    BUSTER_TEST(arguments, after_retained_files.retained_files - before.retained_files >= 5);
    os_image_retention_set_active(false);
    BUSTER_TEST(arguments, os_file_delete(active_registered) && !os_test_image_retention_exists(active_registered));
    BUSTER_TEST(arguments, os_file_delete(active_relative_absolute) && !os_test_image_retention_exists(active_relative_absolute));
    BUSTER_TEST(arguments, os_file_delete(active_elf) && !os_test_image_retention_exists(active_elf));
    BUSTER_TEST(arguments, os_file_delete(active_macho) && !os_test_image_retention_exists(active_macho));
    BUSTER_TEST(arguments, os_file_delete(active_pe) && !os_test_image_retention_exists(active_pe));
    BUSTER_TEST(arguments, os_file_delete(injected) && !os_test_image_retention_exists(injected));
    BUSTER_TEST(arguments, os_directory_delete(retained_directory) && !os_test_image_retention_exists(retained_directory));
    BUSTER_TEST(arguments, os_file_delete(active_text) && !os_test_image_retention_exists(active_text));
    BUSTER_TEST(arguments, os_file_delete(active_rel) && os_file_delete(active_exec));
    os_image_retention_set_active(previous_active);
    return result;
}

// Once-claims (os.h): lanes race the first fill of every entry of a shared
// table in different orders.  Each entry's fill also claims a lower-level
// leaf, as an x86 form's facts claim its operand views, and a u16 table
// stands for the claims whose state doubles as the published value.  Lanes
// only record; the caller checks that every entry was filled exactly once,
// every reader saw the winner's value, and no claim stayed open.
enum
{
    OS_TEST_ONCE_ENTRY_COUNT = 4096,
    OS_TEST_ONCE_LEAF_COUNT = OS_TEST_ONCE_ENTRY_COUNT / 4,
    OS_TEST_ONCE_LEVEL_LEAF = 1,
    OS_TEST_ONCE_LEVEL_ENTRY = 2,
    OS_TEST_ONCE_PENDING = 0,
    OS_TEST_ONCE_READY = 1,
    OS_TEST_ONCE_BUSY = 2,
    OS_TEST_ONCE_WIDE_BUSY = UINT16_MAX,
};

typedef struct OsTestOnceState OsTestOnceState;
struct OsTestOnceState
{
    u8 entry_states[OS_TEST_ONCE_ENTRY_COUNT];
    u64 entry_values[OS_TEST_ONCE_ENTRY_COUNT];
    u32 entry_fills[OS_TEST_ONCE_ENTRY_COUNT];
    u8 leaf_states[OS_TEST_ONCE_LEAF_COUNT];
    u64 leaf_values[OS_TEST_ONCE_LEAF_COUNT];
    u32 leaf_fills[OS_TEST_ONCE_LEAF_COUNT];
    u16 wide_states[OS_TEST_ONCE_ENTRY_COUNT];
    u64 wide_values[OS_TEST_ONCE_ENTRY_COUNT];
    u32 wide_fills[OS_TEST_ONCE_ENTRY_COUNT];
    u64 mismatches[OS_TEST_LANE_MAX_COUNT];
    u64 open_claims[OS_TEST_LANE_MAX_COUNT];
};

BUSTER_GLOBAL_LOCAL u64 os_test_once_value(u64 index, u64 salt)
{
    u64 result = (index + 1) * UINT64_C(0x9e3779b97f4a7c15) ^ salt;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 os_test_once_leaf(OsTestOnceState* state, u64 leaf)
{
    if (atomic_u8_load_acquire(&state->leaf_states[leaf]) != OS_TEST_ONCE_READY &&
        once_claim_u8(&state->leaf_states[leaf], OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_LEAF))
    {
        state->leaf_values[leaf] = os_test_once_value(leaf, 0x1eaf);
        (void)atomic_u32_add(&state->leaf_fills[leaf], 1);
        once_publish_u8(&state->leaf_states[leaf], OS_TEST_ONCE_READY);
    }
    u64 result = atomic_u8_load_acquire(&state->leaf_states[leaf]) == OS_TEST_ONCE_READY ? state->leaf_values[leaf] : 0;
    return result;
}

BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_once_lane(void* argument)
{
    OsTestOnceState* state = (OsTestOnceState*)argument;
    u64 lane = lane_index();
    u64 lanes = lane_count();
    lane_sync();
    u64 mismatches = 0;
    for (u64 step = 0; step < OS_TEST_ONCE_ENTRY_COUNT; step += 1)
    {
        // Odd lanes walk backwards, so every entry is contended from both
        // ends as well as by lanes walking the same direction.
        u64 offset = (step + lane * (OS_TEST_ONCE_ENTRY_COUNT / lanes)) % OS_TEST_ONCE_ENTRY_COUNT;
        u64 index = lane & 1 ? OS_TEST_ONCE_ENTRY_COUNT - 1 - offset : offset;
        if (atomic_u8_load_acquire(&state->entry_states[index]) != OS_TEST_ONCE_READY &&
            once_claim_u8(&state->entry_states[index], OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY))
        {
            state->entry_values[index] = os_test_once_value(index, os_test_once_leaf(state, index % OS_TEST_ONCE_LEAF_COUNT));
            (void)atomic_u32_add(&state->entry_fills[index], 1);
            once_publish_u8(&state->entry_states[index], OS_TEST_ONCE_READY);
        }
        u64 expected = os_test_once_value(index, os_test_once_value(index % OS_TEST_ONCE_LEAF_COUNT, 0x1eaf));
        mismatches += atomic_u8_load_acquire(&state->entry_states[index]) != OS_TEST_ONCE_READY || state->entry_values[index] != expected;

        u16 wide_ready = (u16)(index + 1);
        if (atomic_u16_load_acquire(&state->wide_states[index]) != wide_ready &&
            once_claim_u16(&state->wide_states[index], 0, OS_TEST_ONCE_WIDE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY))
        {
            state->wide_values[index] = os_test_once_value(index, 0x31de);
            (void)atomic_u32_add(&state->wide_fills[index], 1);
            once_publish_u16(&state->wide_states[index], wide_ready);
        }
        mismatches += atomic_u16_load_acquire(&state->wide_states[index]) != wide_ready ||
                      state->wide_values[index] != os_test_once_value(index, 0x31de);
    }
    state->mismatches[lane] = mismatches;
    state->open_claims[lane] = once_claim_depth();
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_once_claims(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;

    // The serial contract: a claim is won once, an abandoned claim (published
    // back to pending) is won again, and a final state is never claimed.
    {
        u8 state = OS_TEST_ONCE_PENDING;
        BUSTER_TEST(arguments, once_claim_u8(&state, OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY));
        BUSTER_TEST(arguments, once_claim_depth() == 1);
        once_publish_u8(&state, OS_TEST_ONCE_PENDING);
        BUSTER_TEST(arguments, once_claim_depth() == 0);
        BUSTER_TEST(arguments, once_claim_u8(&state, OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY));
        u8 leaf = OS_TEST_ONCE_PENDING;
        BUSTER_TEST(arguments, once_claim_u8(&leaf, OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_LEAF));
        BUSTER_TEST(arguments, once_claim_depth() == 2);
        once_publish_u8(&leaf, OS_TEST_ONCE_READY);
        once_publish_u8(&state, OS_TEST_ONCE_READY);
        BUSTER_TEST(arguments, !once_claim_u8(&state, OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY));
        BUSTER_TEST(arguments, once_claim_depth() == 0 && state == OS_TEST_ONCE_READY && leaf == OS_TEST_ONCE_READY);
        u16 wide = 0;
        BUSTER_TEST(arguments, once_claim_u16(&wide, 0, OS_TEST_ONCE_WIDE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY));
        once_publish_u16(&wide, 7);
        BUSTER_TEST(arguments, !once_claim_u16(&wide, 0, OS_TEST_ONCE_WIDE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY) && wide == 7);
    }

    // The race, on a temporary gang owner so its parked workers are joined
    // before later modules' serially checked globals initialize.
    {
        ThreadContext* main_context = thread_context_selected();
        ThreadContext* lane_owner_context = thread_context_allocate();
        thread_context_select(lane_owner_context);
#if BUSTER_SINGLE_THREADED
        u64 lanes = 1;
#else
        u64 lanes = BUSTER_MIN((u64)OS_TEST_LANE_MAX_COUNT, (u64)os_get_logical_thread_count());
        String8 jobs_text = os_get_environment_variable(S8("BUSTER_TEST_JOBS"));
        if (jobs_text.length)
        {
            IntegerParsingU64 parsed = string8_parse_u64_decimal(jobs_text);
            if (parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == jobs_text.length && parsed.value)
            {
                lanes = BUSTER_MIN(lanes, parsed.value);
            }
        }
        lanes = BUSTER_MAX(lanes, (u64)1);
#endif
        for (u32 round = 0; round < 4; round += 1)
        {
            OsTestOnceState* state = arena_allocate(arena, OsTestOnceState, 1);
            memset(state, 0, sizeof(*state));
            lane_run(lanes, &os_test_once_lane, state);
            u64 bad_entries = 0;
            for (u64 index = 0; index < OS_TEST_ONCE_ENTRY_COUNT; index += 1)
            {
                bad_entries += state->entry_fills[index] != 1 || state->wide_fills[index] != 1;
            }
            for (u64 leaf = 0; leaf < OS_TEST_ONCE_LEAF_COUNT; leaf += 1)
            {
                bad_entries += state->leaf_fills[leaf] != 1;
            }
            u64 mismatches = 0;
            u64 open_claims = 0;
            for (u64 lane = 0; lane < lanes; lane += 1)
            {
                mismatches += state->mismatches[lane];
                open_claims += state->open_claims[lane];
            }
            BUSTER_TEST(arguments, bad_entries == 0);
            BUSTER_TEST(arguments, mismatches == 0);
            BUSTER_TEST(arguments, open_claims == 0);
        }
        thread_context_release(lane_owner_context);
        thread_context_select(main_context);
    }

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    // A claim nested at a level that is not lower, and a wait on a claim the
    // thread holds, would deadlock under contention; both fail at once.
    {
        String8 modes[] = {S8("once-level"), S8("once-self-wait")};
        String8 messages[] = {S8("once-claim taken at a level not below the claim it is nested in"),
                              S8("once-claim waited on by the thread that holds it")};
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
        {
            SliceString8 inherited_keys = program_state->input.environment_keys;
            SliceString8 inherited_values = program_state->input.environment_values;
            String8* keys = arena_allocate(arena, String8, inherited_keys.length + 2);
            String8* values = arena_allocate(arena, String8, inherited_keys.length + 2);
            keys[0] = S8("BUSTER_OS_FATAL_OUTPUT_MODE");
            values[0] = modes[index];
            keys[1] = S8("BUSTER_TEST_JOBS");
            values[1] = S8("1");
            u64 count = 2;
            for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
            {
                if (!string_equal(inherited_keys.pointer[inherited], keys[0]) && !string_equal(inherited_keys.pointer[inherited], keys[1]))
                {
                    keys[count] = inherited_keys.pointer[inherited];
                    values[count] = inherited_values.pointer[inherited];
                    count += 1;
                }
            }
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), (SliceString8){keys, count},
                                                        (SliceString8){values, count},
                                                        (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_ERROR});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 30000000);
                BUSTER_TEST(arguments, !wait.timed_out);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_FAILED);
                String8 error = {(char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, wait.streams[STANDARD_STREAM_ERROR].length};
                BUSTER_TEST(arguments, string_starts_with_sequence(error, messages[index]));
            }
        }
    }
#endif

    arena_set_position(arena, position);
    return result;
}

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
// The child side of os_test_once_claims' failure checks: neither returns.
BUSTER_GLOBAL_LOCAL void os_test_once_claim_fault(bool self_wait)
{
    u8 outer = OS_TEST_ONCE_PENDING;
    u8 inner = OS_TEST_ONCE_PENDING;
    (void)once_claim_u8(&outer, OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY);
    if (self_wait)
    {
        once_wait_u8(&outer, OS_TEST_ONCE_BUSY);
    }
    else
    {
        (void)once_claim_u8(&inner, OS_TEST_ONCE_PENDING, OS_TEST_ONCE_BUSY, OS_TEST_ONCE_LEVEL_ENTRY);
    }
    os_exit(126);
}
#endif

UnitTestResult os_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);

    UnitTestResult result = {0};


#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 fatal_mode = os_get_environment_variable(S8("BUSTER_OS_FATAL_OUTPUT_MODE"));
    if (string_equal(fatal_mode, S8("once-level")) || string_equal(fatal_mode, S8("once-self-wait")))
    {
        os_test_once_claim_fault(string_equal(fatal_mode, S8("once-self-wait")));
    }
    if (fatal_mode.length)
    {
        if (string_ends_with_sequence(fatal_mode, S8("closed")))
        {
            os_file_close(os_get_standard_stream(STANDARD_STREAM_ERROR));
        }
#if BUSTER_LINUX
        if (string_ends_with_sequence(fatal_mode, S8("full")))
        {
            int full = open("/dev/full", O_WRONLY);
            if (full < 0 || dup2(full, STDERR_FILENO) < 0) { os_exit(7); }
            close(full);
        }
#endif
        if (string_starts_with_sequence(fatal_mode, S8("raw")))
        {
            os_fail_raw(19, S8("child"), S8("os-fail-regression.c"), S8("fatal-output-37"));
        }
        else
        {
            os_fail_va(19, S8("child"), S8("os-fail-regression.c"), S8("fatal-output-{u32}"), (u32)37);
        }
    }
#endif

    BUSTER_TEST_FIXTURE(arguments, os_test_environment_lookup);
    BUSTER_TEST_FIXTURE(arguments, os_test_once_claims);
    BUSTER_TEST_FIXTURE(arguments, os_test_capture_replay);
    BUSTER_TEST_FIXTURE(arguments, os_test_capture_storage);
    BUSTER_TEST_FIXTURE(arguments, os_test_capture_limits);
    BUSTER_TEST_FIXTURE(arguments, os_test_image_retention);
#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, os_test_capture_overflow);
    BUSTER_TEST_FIXTURE(arguments, os_test_capture_sink_admission);
    BUSTER_TEST_FIXTURE(arguments, os_test_capture_sink_spill);
#endif

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, os_test_capture_native);
#endif

#if BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, os_test_process_deadline_windows);
    BUSTER_TEST_FIXTURE(arguments, os_test_process_cleanup_windows);
    BUSTER_TEST_FIXTURE(arguments, os_test_process_shutdown_exit_windows);
    BUSTER_TEST_FIXTURE(arguments, os_test_windows_handle_observation);
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
    // Test-owned subprocess modes above must dispatch before this fixture.
    // Otherwise each probe recursively runs the full spawn contract before
    // reaching its sentinel, which can consume the deadlock deadline under
    // sanitizers without exercising the resource-failure path.
    BUSTER_TEST_FIXTURE(arguments, os_process_spawn_contract_tests);
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    // Symbol lookup accepts bounded String8 names. A readable suffix
    // and an exact-sized buffer must not become part of the C string.
    {
#if BUSTER_WINDOWS
        OsModuleHandle* module = (OsModuleHandle*)LoadLibraryW(L"kernel32.dll");
        String8 symbol = S8("GetCurrentProcessId");
        char8 exact_storage[] = {'G', 'e', 't', 'C', 'u', 'r', 'r', 'e', 'n', 't',
                                 'P', 'r', 'o', 'c', 'e', 's', 's', 'I', 'd'};
        OsSymbol* expected = module ? (OsSymbol*)GetProcAddress((HMODULE)module, symbol.pointer) : 0;
#else
        OsModuleHandle* module = (OsModuleHandle*)dlopen(0, RTLD_NOW | RTLD_LOCAL);
        String8 symbol = S8("getpid");
        char8 exact_storage[] = {'g', 'e', 't', 'p', 'i', 'd'};
        OsSymbol* expected = module ? (OsSymbol*)dlsym((void*)module, symbol.pointer) : 0;
#endif
        if (BUSTER_REQUIRE(arguments, module != 0 && expected != 0))
        {
            char8 suffix_storage[64] = {0};
            BUSTER_RT_CHECK(symbol.length + sizeof("_suffix") <= sizeof(suffix_storage));
            memcpy(suffix_storage, symbol.pointer, symbol.length);
            memcpy(suffix_storage + symbol.length, "_suffix", sizeof("_suffix"));

            BUSTER_TEST(arguments, os_dynamic_library_function_load(module, symbol) == expected);
            BUSTER_TEST(arguments,
                        os_dynamic_library_function_load(module, (String8){.pointer = suffix_storage, .length = symbol.length}) == expected);
            BUSTER_TEST(arguments,
                        os_dynamic_library_function_load(module,
                                                         (String8){.pointer = exact_storage, .length = sizeof(exact_storage)}) == expected);
            BUSTER_TEST(arguments, os_dynamic_library_function_load(module, (String8){0}) == 0);
            BUSTER_TEST(arguments,
                        os_dynamic_library_function_load(module, S8("buster_os_test_missing_symbol_661")) == 0);
        }
        os_dynamic_library_unload(module);
    }
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || (BUSTER_WINDOWS && !defined(__TINYC__))) && !BUSTER_ANDROID && !BUSTER_IOS
    // Thread names use the same bounded contract, including empty and
    // exact-sized inputs. Restore the runner's original name afterward.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;
        String8 original_name = {0};
        if (BUSTER_REQUIRE(arguments, os_test_thread_name_get(arena, &original_name)))
        {
#if BUSTER_LINUX
            enum { OS_TEST_THREAD_NAME_BOUNDARY = 15 };
#else
            enum { OS_TEST_THREAD_NAME_BOUNDARY = 63 };
#endif
            char8 suffix_storage[] = "worker-suffix";
            char8 exact_storage[] = {'e', 'x', 'a', 'c', 't', '6', '6', '2'};
            char8 boundary_storage[OS_TEST_THREAD_NAME_BOUNDARY];
            for (u64 index = 0; index < sizeof(boundary_storage); index += 1)
            {
                boundary_storage[index] = (char8)('a' + index % 26);
            }
            String8 names[] = {
                S8("os-662"),
                {.pointer = suffix_storage, .length = 6},
                {0},
                {.pointer = exact_storage, .length = sizeof(exact_storage)},
                {.pointer = boundary_storage, .length = sizeof(boundary_storage)},
            };

            for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
            {
                os_thread_set_name(names[index]);
                String8 observed = {0};
                if (BUSTER_REQUIRE(arguments, os_test_thread_name_get(arena, &observed)))
                {
                    BUSTER_STRING_TEST(arguments, observed, names[index]);
                }
            }

            os_thread_set_name(original_name);
            String8 restored = {0};
            if (BUSTER_REQUIRE(arguments, os_test_thread_name_get(arena, &restored)))
            {
                BUSTER_STRING_TEST(arguments, restored, original_name);
            }
        }
        arena_set_position(arena, position);
    }
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    {
        String8 modes[] = {S8("raw-live"), S8("formatted-live"), S8("raw-closed"), S8("formatted-closed"),
#if BUSTER_LINUX
                           S8("raw-full"), S8("formatted-full"),
#endif
        };
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
        {
            SliceString8 inherited_keys = program_state->input.environment_keys;
            SliceString8 inherited_values = program_state->input.environment_values;
            String8* keys = arena_allocate(arguments->arena, String8, inherited_keys.length + 2);
            String8* values = arena_allocate(arguments->arena, String8, inherited_keys.length + 2);
            keys[0] = S8("BUSTER_OS_FATAL_OUTPUT_MODE"); values[0] = modes[index];
            keys[1] = S8("BUSTER_TEST_JOBS"); values[1] = S8("1");
            u64 count = 2;
            for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
            {
                if (!string_equal(inherited_keys.pointer[inherited], keys[0]) && !string_equal(inherited_keys.pointer[inherited], keys[1]))
                {
                    keys[count] = inherited_keys.pointer[inherited]; values[count] = inherited_values.pointer[inherited];
                    count += 1;
                }
            }
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                (SliceString8){keys, count}, (SliceString8){values, count},
                (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_ERROR});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(arguments->arena, spawn, 30000000);
                BUSTER_TEST(arguments, !wait.timed_out);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_FAILED);
#if BUSTER_WINDOWS
                BUSTER_TEST(arguments, wait.platform_status == 1);
#else
                // Darwin's wait macros take the address of their argument.
                int native_status = (int)wait.platform_status;
                BUSTER_TEST(arguments, WIFEXITED(native_status) && WEXITSTATUS(native_status) == 1);
#endif
                String8 error = {(char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, wait.streams[STANDARD_STREAM_ERROR].length};
                BUSTER_TEST(arguments, index < 2 ? string_equal(error, S8("fatal-output-37 at os-fail-regression.c:19 in child\n")) : !error.length);
            }
        }
    }
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
    // Recoverable file IO must distinguish errors from EOF, retain short
    // reads, and report write errors instead of aborting a result-producing tool.
    {
        Arena* arena = arguments->arena;
        String8 root = buster_test_temporary_path(arena, S8("shared-io space"), S8(""));
        BUSTER_TEST(arguments, os_make_directory_attempt(root));
        BUSTER_TEST(arguments, os_make_directory_attempt(root));
        String8 status_directory = string_format_z(arena, S8("{S8}/directory-status"), root);
        OsDirectoryCreateResult created_directory = os_make_directory(status_directory);
        BUSTER_TEST(arguments, !created_directory.error.v && created_directory.created && !created_directory.already_exists &&
                                   !created_directory.existing_directory);
        OsDirectoryCreateResult existing_directory = os_make_directory(status_directory);
        BUSTER_TEST(arguments, !existing_directory.error.v && !existing_directory.created && existing_directory.already_exists &&
                                   existing_directory.existing_directory);
        String8 path = string_format_z(arena, S8("{S8}/bytes"), root);
        String8 absent = string_format_z(arena, S8("{S8}/absent/child"), root);
        BUSTER_TEST(arguments, !os_make_directory_attempt(absent));
        BUSTER_TEST(arguments, !os_make_directory_attempt((String8){0}));
        BUSTER_TEST(arguments, !os_make_directory_attempt((String8){.pointer = root.pointer, .length = 0}));
        BUSTER_TEST(arguments, !os_make_directory_attempt((String8){.pointer = 0, .length = 1}));
        // #3735: rejected NULs must fail even when the prefix is a directory.
        char8 leading_nul[] = {0, 'x'};
        BUSTER_TEST(arguments, !os_make_directory_attempt((String8){leading_nul, sizeof(leading_nul)}));
        String8 missing_prefix = string_format_z(arena, S8("{S8}/nul-prefix"), root);
        String8 prefixes[] = {missing_prefix, root};
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(prefixes); index += 1)
        {
            String8 prefix = prefixes[index];
            char8* bytes = arena_allocate(arena, char8, prefix.length + 2);
            memcpy(bytes, prefix.pointer, prefix.length);
            bytes[prefix.length] = 0;
            bytes[prefix.length + 1] = 'x';
            BUSTER_TEST(arguments, !os_make_directory_attempt((String8){bytes, prefix.length + 1}));
            BUSTER_TEST(arguments, !os_make_directory_attempt((String8){bytes, prefix.length + 2}));
        }
        // Exclusive creation proves the malformed calls did not create the prefix.
        OsDirectoryCreateResult prefix_created = os_make_directory_exclusive(missing_prefix);
        BUSTER_TEST(arguments, prefix_created.created && !prefix_created.error.v && !prefix_created.already_exists);
        OsDirectoryCreateResult missing_parent = os_make_directory(absent);
        BUSTER_TEST(arguments, missing_parent.error.v != 0 && !missing_parent.created && !missing_parent.existing_directory);
        u8 original[257];
        u8 copy[300];
        for (u32 i = 0; i < sizeof(original); i += 1) original[i] = (u8)i;
        u64 count = 99;
        BUSTER_TEST(arguments, os_file_read_attempt(0, (ByteSlice){0}, &count) && count == 0);
        BUSTER_TEST(arguments, os_file_write_attempt(0, (ByteSlice){0}));
        BUSTER_TEST(arguments, !os_file_read_attempt(0, (ByteSlice){copy, 1}, &count) && count == 0);
        BUSTER_TEST(arguments, !os_file_write_attempt(0, (ByteSlice){original, 1}));
        OsFileDescriptor* file = os_file_open(
            path,
            (OpenFlags){ .create = 1, .truncate = 1 },
            (OsFileAccess){ .write = 1 },
            (OsFileCreateMode){0},
            (OsFileShareFlags){ .read = 1, .write = 1, .delete = 1 });
        BUSTER_TEST(arguments, file != 0);
        if (file)
        {
            BUSTER_TEST(arguments, os_file_write_attempt(file, (ByteSlice){original, sizeof(original)}));
            BUSTER_TEST(arguments, !os_file_read_attempt(file, (ByteSlice){copy, 1}, &count) && count == 0);
            BUSTER_TEST(arguments, os_file_close(file));
        }
        OsDirectoryCreateResult file_collision = os_make_directory(path);
        BUSTER_TEST(arguments, !os_make_directory_attempt(path));
        BUSTER_TEST(arguments, file_collision.error.v != 0 && file_collision.already_exists && !file_collision.existing_directory &&
                                   !file_collision.created);
        file = os_file_open(path, (OpenFlags){0}, (OsFileAccess){ .read = 1 }, (OsFileCreateMode){0}, (OsFileShareFlags){ .read = 1 });
        BUSTER_TEST(arguments, file != 0);
        if (file)
        {
            BUSTER_TEST(arguments, !os_file_write_attempt(file, (ByteSlice){original, 1}));
            BUSTER_TEST(arguments, os_file_read_attempt(file, (ByteSlice){copy, 13}, &count) && count == 13);
            BUSTER_TEST(arguments, memcmp(copy, original, 13) == 0);
            BUSTER_TEST(arguments, os_file_read_attempt(file, (ByteSlice){copy, sizeof(copy)}, &count) && count == sizeof(original) - 13);
            BUSTER_TEST(arguments, memcmp(copy, original + 13, sizeof(original) - 13) == 0);
            BUSTER_TEST(arguments, os_file_read_attempt(file, (ByteSlice){copy, sizeof(copy)}, &count) && count == 0);
            BUSTER_TEST(arguments, os_file_close(file));
        }
        {
            String8 short_path = string_format_z(arena, S8("{S8}/foo"), root);
            String8 long_path = string_format_z(arena, S8("{S8}/foobar"), root);
            BUSTER_TEST(arguments, file_write(short_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("short-path"))));
            BUSTER_TEST(arguments, file_write(long_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("long-path"))));
            FileReadResult sliced_read = file_read_checked(arena, (String8){.pointer = long_path.pointer, .length = short_path.length}, (FileReadOptions){0});
            String8 sliced_contents = {.pointer = (char8*)sliced_read.bytes.pointer, .length = sliced_read.bytes.length};
            BUSTER_TEST(arguments, sliced_read.status == OS_FILE_READ_OK && string_equal(sliced_contents, S8("short-path")));
            BUSTER_TEST(arguments, os_file_delete(long_path) && os_file_delete(short_path));
        }
#if BUSTER_LINUX
        file = os_file_open(
            S8("/dev/full"),
            (OpenFlags){0},
            (OsFileAccess){ .write = 1 },
            (OsFileCreateMode){0},
            (OsFileShareFlags){ .read = 1, .write = 1, .delete = 1 });
        BUSTER_TEST(arguments, file != 0);
        if (file)
        {
            BUSTER_TEST(arguments, !os_file_write_attempt(file, (ByteSlice){original, sizeof(original)}));
            BUSTER_TEST(arguments, os_file_close(file));
        }
#endif
        // No path terminator is readable at length; missing outputs still
        // resolve. Returned storage survives nested scratch use and aliasing.
        char8 bounded[] = {'n', 'e', 'w'};
        for (u32 index = 0; index < SCRATCH_ARENA_COUNT; index += 1)
        {
            Arena* output = thread_context_selected()->arenas[index];
            u64 position = output->position;
            String8 absolute = os_path_absolute_lexical(output, (String8){bounded, sizeof(bounded)}, true);
            String8 again = os_path_absolute_lexical(output, absolute, true);
            memset(arena_allocate(output, u8, 256), 0x55, 256);
            BUSTER_TEST(arguments, absolute.length > sizeof(bounded));
            BUSTER_TEST(arguments, absolute.pointer && absolute.pointer[absolute.length] == 0);
            BUSTER_TEST(arguments, string_ends_with_sequence(absolute, S8("new")));
            BUSTER_STRING_TEST(arguments, absolute, again);
            arena_set_position(output, position);
        }
        char8 invalid[] = {'a', 0, 'b'};
        BUSTER_TEST(arguments, !os_path_absolute_lexical(arena, (String8){invalid, sizeof(invalid)}, true).length);
        BUSTER_TEST(arguments, !os_path_absolute_lexical(arena, (String8){0, 1}, true).length);
        OsFileOpenResult invalid_open = os_file_open_checked((String8){invalid, sizeof(invalid)}, (OpenFlags){0}, (OsFileAccess){.read = 1}, (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
        BUSTER_TEST(arguments, !invalid_open.file && invalid_open.error.v != 0);
        BUSTER_TEST(arguments, os_file_delete_checked((String8){invalid, sizeof(invalid)}).v != 0);
        BUSTER_TEST(arguments, !os_dynamic_library_load((String8){invalid, sizeof(invalid)}));
        BUSTER_TEST(arguments, !os_dynamic_library_function_load(0, (String8){invalid, sizeof(invalid)}));
        FileMapRead invalid_map = file_map_read(arena, (String8){invalid, sizeof(invalid)}, (FileReadOptions){.map_required = 1});
        BUSTER_TEST(arguments, !invalid_map.bytes.pointer);
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
#endif

#if !BUSTER_WINDOWS
    // Creation mode is independent of requested handle access and Windows sharing.
    {
        Arena* arena = arguments->arena;
        String8 private_path = buster_test_temporary_path(arena, S8("os-private-create-mode"), S8(".bin"));
        String8 executable_path = buster_test_temporary_path(arena, S8("os-executable-create-mode"), S8(".bin"));
        String8 explicit_path = buster_test_temporary_path(arena, S8("os-explicit-create-mode"), S8(".bin"));
        os_file_delete(private_path);
        os_file_delete(executable_path);
        os_file_delete(explicit_path);

        OsFileDescriptor* private_file = os_file_open(private_path, (OpenFlags){.create = 1, .truncate = 1},
                                                      (OsFileAccess){.write = 1},
                                                      (OsFileCreateMode){.kind = OS_FILE_CREATE_MODE_PRIVATE},
                                                      (OsFileShareFlags){0});
        BUSTER_TEST(arguments, private_file != 0);
        if (private_file)
        {
            FileStats stats = os_file_get_stats(private_file, (FileStatsOptions){.identity = 1});
            BUSTER_TEST(arguments, stats.valid && (stats.permissions & 0777) == 0600);
            BUSTER_TEST(arguments, os_file_close(private_file));
        }

        OsFileDescriptor* executable_file = os_file_open(executable_path, (OpenFlags){.create = 1, .truncate = 1},
                                                         (OsFileAccess){.write = 1},
                                                         (OsFileCreateMode){.kind = OS_FILE_CREATE_MODE_EXECUTABLE},
                                                         (OsFileShareFlags){0});
        BUSTER_TEST(arguments, executable_file != 0);
        if (executable_file)
        {
            FileStats stats = os_file_get_stats(executable_file, (FileStatsOptions){.identity = 1});
            BUSTER_TEST(arguments, stats.valid && (stats.permissions & 0100) != 0);
            BUSTER_TEST(arguments, os_file_close(executable_file));
        }
        OsFileDescriptor* explicit_file = os_file_open(explicit_path, (OpenFlags){.create = 1, .truncate = 1},
                                                       (OsFileAccess){.write = 1},
                                                       (OsFileCreateMode){.kind = OS_FILE_CREATE_MODE_EXPLICIT_POSIX,
                                                                          .posix_permissions = 0},
                                                       (OsFileShareFlags){0});
        BUSTER_TEST(arguments, explicit_file != 0);
        if (explicit_file)
        {
            FileStats stats = os_file_get_stats(explicit_file, (FileStatsOptions){.identity = 1});
            BUSTER_TEST(arguments, stats.valid && stats.permissions == 0);
            BUSTER_TEST(arguments, os_file_close(explicit_file));
        }
        BUSTER_TEST(arguments, os_file_delete(private_path));
        BUSTER_TEST(arguments, os_file_delete(executable_path));
        BUSTER_TEST(arguments, os_file_delete(explicit_path));
    }
#endif

#if BUSTER_WINDOWS
    // Each Windows share bit independently controls other handles' access.
    {
        Arena* arena = arguments->arena;
        String8 path = buster_test_temporary_path(arena, S8("os-windows-share-policy"), S8(".bin"));
        BUSTER_TEST(arguments, file_write(path, (ByteSlice){0}));

        OsFileOpenResult read_holder = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.read = 1},
                                                            (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
        BUSTER_TEST(arguments, read_holder.file != 0);
        OsFileOpenResult second_reader = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.read = 1},
                                                              (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
        OsFileOpenResult denied_writer = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.write = 1},
                                                             (OsFileCreateMode){0},
                                                             (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
        BUSTER_TEST(arguments, second_reader.file != 0);
        BUSTER_TEST(arguments, denied_writer.file == 0 && denied_writer.error.v == (u32)ERROR_SHARING_VIOLATION);
        if (denied_writer.file) BUSTER_TEST(arguments, os_file_close(denied_writer.file));
        if (second_reader.file) BUSTER_TEST(arguments, os_file_close(second_reader.file));
        if (read_holder.file) BUSTER_TEST(arguments, os_file_close(read_holder.file));

        OsFileOpenResult write_holder = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.write = 1},
                                                             (OsFileCreateMode){0}, (OsFileShareFlags){.write = 1});
        OsFileOpenResult second_writer = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.write = 1},
                                                              (OsFileCreateMode){0}, (OsFileShareFlags){.write = 1});
        OsFileOpenResult denied_reader = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.read = 1},
                                                              (OsFileCreateMode){0},
                                                              (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
        BUSTER_TEST(arguments, write_holder.file != 0);
        BUSTER_TEST(arguments, second_writer.file != 0);
        BUSTER_TEST(arguments, denied_reader.file == 0 && denied_reader.error.v == (u32)ERROR_SHARING_VIOLATION);
        if (denied_reader.file) BUSTER_TEST(arguments, os_file_close(denied_reader.file));
        if (second_writer.file) BUSTER_TEST(arguments, os_file_close(second_writer.file));
        if (write_holder.file) BUSTER_TEST(arguments, os_file_close(write_holder.file));

        OsFileOpenResult delete_blocker = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.read = 1},
                                                               (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
        BUSTER_TEST(arguments, delete_blocker.file != 0);
        BUSTER_TEST(arguments, !os_file_delete(path));
        if (delete_blocker.file) BUSTER_TEST(arguments, os_file_close(delete_blocker.file));

        OsFileOpenResult delete_holder = os_file_open_checked(path, (OpenFlags){0}, (OsFileAccess){.read = 1},
                                                              (OsFileCreateMode){0},
                                                              (OsFileShareFlags){.read = 1, .delete = 1});
        BUSTER_TEST(arguments, delete_holder.file != 0);
        BUSTER_TEST(arguments, os_file_delete(path));
        if (delete_holder.file) BUSTER_TEST(arguments, os_file_close(delete_holder.file));
        BUSTER_TEST(arguments, os_file_delete(path));

        String8 private_path = buster_test_temporary_path(arena, S8("os-windows-private-mode"), S8(".bin"));
        OsFileOpenResult private_mode = os_file_open_checked(private_path, (OpenFlags){.create = 1, .truncate = 1},
                                                              (OsFileAccess){.write = 1},
                                                              (OsFileCreateMode){.kind = OS_FILE_CREATE_MODE_PRIVATE},
                                                              (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
        BUSTER_TEST(arguments, private_mode.file == 0 && private_mode.error.v == (u32)ERROR_NOT_SUPPORTED);
        FileStats private_stats = os_file_replacement_target_stats(private_path);
        BUSTER_TEST(arguments, private_stats.valid && private_stats.kind == OS_FILE_KIND_MISSING);

        String8 explicit_path = buster_test_temporary_path(arena, S8("os-windows-explicit-posix-mode"), S8(".bin"));
        OsFileOpenResult explicit_mode = os_file_open_checked(explicit_path, (OpenFlags){.create = 1, .truncate = 1},
                                                              (OsFileAccess){.write = 1},
                                                              (OsFileCreateMode){.kind = OS_FILE_CREATE_MODE_EXPLICIT_POSIX,
                                                                                 .posix_permissions = 0640},
                                                              (OsFileShareFlags){.read = 1, .write = 1, .delete = 1});
        BUSTER_TEST(arguments, explicit_mode.file == 0 && explicit_mode.error.v == (u32)ERROR_NOT_SUPPORTED);
        FileStats explicit_stats = os_file_replacement_target_stats(explicit_path);
        BUSTER_TEST(arguments, explicit_stats.valid && explicit_stats.kind == OS_FILE_KIND_MISSING);
    }
#endif

    // Reservations start inaccessible, a committed interior page is writable,
    // and protecting it must not discard its contents. Do not conflate the
    // Windows allocation granularity with the commit/protection page size.
    {
        u64 page_size = os_get_page_size();
        u64 size = 3 * page_size;
        u8* reservation = (u8*)os_reserve(0, size, (ProtectionFlags){0},
                                         (MapFlags){.priv = true, .anonymous = true, .no_reserve = true});
        BUSTER_TEST(arguments, reservation != 0);
        if (reservation)
        {
            BUSTER_TEST(arguments, (u64)reservation % page_size == 0);
            u8* page = reservation + page_size;
            bool committed = os_commit(page, page_size, (ProtectionFlags){.read = true, .write = true}, false);
            BUSTER_TEST(arguments, committed);
            if (committed)
            {
                bool zeroed = true;
                for (u64 i = 0; i < page_size; i += 1)
                {
                    zeroed = zeroed && page[i] == 0;
                }
                BUSTER_TEST(arguments, zeroed);
                page[0] = 0xa5;
                page[page_size - 1] = 0x5a;
                bool inaccessible = os_protect(page, page_size, (ProtectionFlags){0});
                BUSTER_TEST(arguments, inaccessible);
#if defined(_WIN32)
                MEMORY_BASIC_INFORMATION information;
                SIZE_T queried = VirtualQuery(page, &information, sizeof(information));
                BUSTER_TEST(arguments, queried == sizeof(information));
                if (queried == sizeof(information))
                {
                    BUSTER_TEST(arguments, information.State == MEM_COMMIT);
                    BUSTER_TEST(arguments, information.Protect == PAGE_NOACCESS);
                    BUSTER_TEST(arguments, information.AllocationBase == reservation);
                }
#endif
                bool accessible = os_protect(page, page_size, (ProtectionFlags){.read = true, .write = true});
                BUSTER_TEST(arguments, accessible);
                if (accessible)
                {
                    BUSTER_TEST(arguments, page[0] == 0xa5 && page[page_size - 1] == 0x5a);
                }
#if defined(_WIN32)
                ProtectionFlags requested[] = {
                    {0}, {.read = true}, {.write = true}, {.read = true, .write = true},
                    {.execute = true}, {.read = true, .execute = true}, {.write = true, .execute = true},
                    {.read = true, .write = true, .execute = true},
                };
                DWORD expected[] = {
                    PAGE_NOACCESS, PAGE_READONLY, PAGE_READWRITE, PAGE_READWRITE,
                    PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READWRITE,
                };
                BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(requested) == BUSTER_ARRAY_LENGTH(expected));
                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(requested); i += 1)
                {
                    bool protected = os_protect(page, page_size, requested[i]);
                    BUSTER_TEST(arguments, protected);
                    queried = VirtualQuery(page, &information, sizeof(information));
                    BUSTER_TEST(arguments, queried == sizeof(information));
                    if (queried == sizeof(information))
                    {
                        BUSTER_TEST(arguments, information.Protect == expected[i]);
                    }
                }
#endif
                bool decommitted = os_decommit(page, page_size);
                BUSTER_TEST(arguments, decommitted);
                if (decommitted)
                {
                    bool recommitted = os_commit(page, page_size, (ProtectionFlags){.read = true, .write = true}, false);
                    BUSTER_TEST(arguments, recommitted);
                    if (recommitted)
                    {
                        // Darwin MADV_DONTNEED is not a promise of zero-fill.
                        // Only fresh commitment was tested for zeroes above.
                        page[page_size - 1] = 0xc3;
                        BUSTER_TEST(arguments, page[page_size - 1] == 0xc3);
                    }
                }
            }
            BUSTER_TEST(arguments, os_unreserve(reservation, size));
        }
    }

    // Prefaulting is advisory, and os_commit reports commitment only. Not
    // asking must issue no request at all; a refused request must leave a
    // successful commit successful and its bytes usable; and a commit that
    // really fails must fail without having issued the advisory request whose
    // outcome could otherwise be mistaken for the reason.
    {
        u64 page_size = os_get_page_size();
        u64 size = 2 * page_size;
        u8* reservation = (u8*)os_reserve(0, size, (ProtectionFlags){0},
                                          (MapFlags){.priv = true, .anonymous = true, .no_reserve = true});
        BUSTER_TEST(arguments, reservation != 0);
        if (reservation)
        {
            OsPrefaultTestCounters before = os_prefault_test_counters();
            bool quiet = os_commit(reservation, page_size, (ProtectionFlags){.read = true, .write = true}, false);
            BUSTER_TEST(arguments, quiet);
            BUSTER_TEST(arguments, os_prefault_test_counters().requests == before.requests);

            os_prefault_test_force_next(OS_PREFAULT_REFUSED);
            bool refused = os_commit(reservation, size, (ProtectionFlags){.read = true, .write = true}, true);
            OsPrefaultTestCounters after_refused = os_prefault_test_counters();
            BUSTER_TEST(arguments, refused);
            BUSTER_TEST(arguments, after_refused.requests == before.requests + 1);
            BUSTER_TEST(arguments, after_refused.unpopulated == before.unpopulated + 1);
            BUSTER_TEST(arguments, after_refused.last == OS_PREFAULT_REFUSED);
            reservation[0] = 0x3c;
            reservation[size - 1] = 0xc3;
            BUSTER_TEST(arguments, reservation[0] == 0x3c && reservation[size - 1] == 0xc3);

            // The override is one shot: the next request reaches the platform.
            // Whichever of the three documented outcomes this host reports,
            // the committed range is unchanged by asking.
            OsPrefaultResult native = os_prefault(reservation, size);
            BUSTER_TEST(arguments, native == OS_PREFAULT_POPULATED || native == OS_PREFAULT_REFUSED ||
                                       native == OS_PREFAULT_UNAVAILABLE);
            BUSTER_TEST(arguments, os_prefault_test_counters().requests == after_refused.requests + 1);
            BUSTER_TEST(arguments, os_prefault_test_counters().last == native);
            BUSTER_TEST(arguments, reservation[0] == 0x3c && reservation[size - 1] == 0xc3);

            BUSTER_TEST(arguments, os_unreserve(reservation, size));

            // Committing the range just released fails for a real reason, and
            // must do so before any prefault request is issued.
            OsPrefaultTestCounters before_failure = os_prefault_test_counters();
            bool failed = os_commit(reservation, page_size, (ProtectionFlags){.read = true, .write = true}, true);
            BUSTER_TEST(arguments, !failed);
            BUSTER_TEST(arguments, os_prefault_test_counters().requests == before_failure.requests);
        }
    }

    // os_discard releases only the complete pages inside an abandoned range
    // and leaves the whole mapping writable: the partial pages at either end
    // keep their bytes, and the released page accepts new writes (#3854).
    {
        u64 page_size = os_get_page_size();
        u64 size = 4 * page_size;
        u8* reservation = (u8*)os_reserve(0, size, (ProtectionFlags){0},
                                          (MapFlags){.priv = true, .anonymous = true, .no_reserve = true});
        BUSTER_TEST(arguments, reservation != 0);
        if (reservation)
        {
            bool committed = os_commit(reservation, size, (ProtectionFlags){.read = true, .write = true}, false);
            BUSTER_TEST(arguments, committed);
            if (committed)
            {
                memset(reservation, 0x5a, size);
                BUSTER_TEST(arguments, os_discard(0, size) == 0);
                BUSTER_TEST(arguments, os_discard(reservation + 1, page_size - 2) == 0);
                // From mid first page to mid third page: only page two is whole.
                u64 released = os_discard(reservation + page_size / 2, 2 * page_size);
                BUSTER_TEST(arguments, released == 0 || released == page_size);
                BUSTER_TEST(arguments, reservation[page_size - 1] == 0x5a && reservation[2 * page_size] == 0x5a);
                BUSTER_TEST(arguments, reservation[0] == 0x5a && reservation[size - 1] == 0x5a);
                reservation[page_size] = 0xc3;
                reservation[2 * page_size - 1] = 0x3c;
                BUSTER_TEST(arguments, reservation[page_size] == 0xc3 && reservation[2 * page_size - 1] == 0x3c);
#if defined(__linux__)
                BUSTER_TEST(arguments, released == page_size);
                BUSTER_TEST(arguments, reservation[page_size + 1] == 0);
#endif
            }
            BUSTER_TEST(arguments, os_unreserve(reservation, size));
        }
    }

    // Releasing the selected context must clear TLS before its arenas go
    // away. Direct scratch users still require a selected context; POSIX file
    // path operations retain their prior teardown behavior with private arenas.
    ThreadContext* main_context = thread_context_selected();
    BUSTER_TEST(arguments, main_context != 0);
    String8 contextless_path = buster_test_temporary_path(arguments->arena, S8("contextless-file"), S8(".bin"));
    OsFileOpenResult contextless_setup = os_file_open_checked(contextless_path, (OpenFlags){.create = 1, .truncate = 1}, (OsFileAccess){.write = 1},
                                                               (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1, .write = 1});
    bool contextless_setup_ok = contextless_setup.file != 0;
    if (contextless_setup.file)
    {
        OsFileTransferResult written = os_file_write_checked(contextless_setup.file, BUSTER_SLICE_TO_BYTE_SLICE(S8("contextless")));
        OsError closed = os_file_close_checked(contextless_setup.file);
        contextless_setup_ok &= !written.error.v && written.transferred == S8("contextless").length && !closed.v;
    }
    BUSTER_TEST(arguments, contextless_setup_ok);
    ThreadContext* temporary_context = thread_context_allocate();
    thread_context_select(temporary_context);
    thread_context_release(temporary_context);
    bool released_context_was_cleared = thread_context_selected() == 0;
#if !BUSTER_WINDOWS
    bool contextless_open_ok = false;
    bool contextless_stats_ok = false;
    bool contextless_delete_ok = false;
    if (released_context_was_cleared && contextless_setup_ok)
    {
        OsFileOpenResult opened = os_file_open_checked(contextless_path, (OpenFlags){0}, (OsFileAccess){.read = 1}, (OsFileCreateMode){0}, (OsFileShareFlags){.read = 1});
        contextless_open_ok = opened.file != 0 && !opened.error.v;
        if (opened.file) contextless_open_ok &= !os_file_close_checked(opened.file).v;
        FileStats stats = os_file_replacement_target_stats(contextless_path);
        contextless_stats_ok = stats.valid && stats.kind == OS_FILE_KIND_REGULAR;
        contextless_delete_ok = !os_file_delete_checked(contextless_path).v;
    }
#endif
    thread_context_select(main_context);
    BUSTER_TEST(arguments, released_context_was_cleared);
    BUSTER_TEST(arguments, thread_context_selected() == main_context);
#if !BUSTER_WINDOWS
    BUSTER_TEST(arguments, contextless_open_ok);
    BUSTER_TEST(arguments, contextless_stats_ok);
    BUSTER_TEST(arguments, contextless_delete_ok);
#endif

    // Generic OS-thread teardown must unmap parked arenas before its TLS pool
    // root disappears. Run twice so this remains a reclamation regression even
    // when lane tests are intentionally clamped to one worker. A compile-time
    // serial build has process-global context state and must not spawn workers.
#if !BUSTER_SINGLE_THREADED
    for (u64 invocation = 0; invocation < 2; invocation += 1)
    {
        OsTestThreadPoolState state = {0};
        OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){
            .callback = &os_test_thread_pool_entry,
            .argument = &state,
        });
        BUSTER_TEST(arguments, thread != 0);
        if (thread)
        {
            bool joined = os_thread_join(thread);
            BUSTER_TEST(arguments, joined);
            if (joined)
            {
                bool still_reserved = state.pooled_arena &&
                                      os_commit(state.pooled_arena, os_get_page_size(), (ProtectionFlags){.read = 1, .write = 1}, false);
                BUSTER_TEST(arguments, state.pooled_arena != 0 && !still_reserved);
            }
        }
    }
#endif

    // flag_set_ex/flag_get_ex pack one flag per bit across u64 words.
    {
        u64 flags[FLAG_ARRAY_LENGTH(u64, 100)] = {0};
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(flags) == 2);

        u64 set_indices[] = {0, 1, 63, 64, 99};
        for (EACH_ARRAY_INDEX(i, set_indices))
        {
            flag_set_ex(flags, 100, set_indices[i], true);
        }

        u64 set_i = 0;
        bool all_match = true;
        for (u64 flag_index = 0; flag_index < 100; flag_index += 1)
        {
            bool expected = set_i < BUSTER_ARRAY_LENGTH(set_indices) && set_indices[set_i] == flag_index;
            set_i += expected;
            all_match = all_match && flag_get_ex(flags, 100, flag_index) == expected;
        }
        BUSTER_TEST(arguments, all_match);

        flag_set_ex(flags, 100, 64, false);
        BUSTER_TEST(arguments, !flag_get_ex(flags, 100, 64));
        BUSTER_TEST(arguments, flag_get_ex(flags, 100, 63));
    }

    // Every standard stream must be representable; fd 0 (stdin) used to
    // collide with the null "no descriptor" encoding.
    {
        bool all_streams_valid = true;
        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            all_streams_valid = all_streams_valid && os_get_standard_stream((StandardStream)stream) != 0;
        }
        BUSTER_TEST(arguments, all_streams_valid);
#if defined(__linux__) || defined(__APPLE__)
        BUSTER_TEST(arguments, generic_fd_to_posix(os_get_standard_stream(STANDARD_STREAM_INPUT)) == 0);
        BUSTER_TEST(arguments, generic_fd_to_posix(os_get_standard_stream(STANDARD_STREAM_OUTPUT)) == 1);
        BUSTER_TEST(arguments, generic_fd_to_posix(os_get_standard_stream(STANDARD_STREAM_ERROR)) == 2);
#endif
    }

    // Physical-memory size feeds the build driver's MACHINE_INFO line.
    BUSTER_TEST(arguments, os_get_physical_memory_size() > 0);

    // Resident size budgets the fuzz session's RSS limit against what the
    // process has already used, so a platform silently reporting 0 would put
    // that limit straight back to where it was firing on inherited memory.
    // A process running its own test suite holds at least a page and less than
    // the machine.
    u64 resident_memory_size = os_get_resident_memory_size();
    BUSTER_TEST(arguments, resident_memory_size >= os_get_page_size());
    BUSTER_TEST(arguments, resident_memory_size <= os_get_physical_memory_size());
#if defined(__linux__)
    // Bounds that loose would accept the wrong /proc/self/statm column -- the
    // first cut of this read `shared` instead of `resident` and reported 20 MB
    // for a process holding 1.3 GB, which passed every test above and silently
    // put the fuzz limit back where it started. Cross-check the parse against
    // VmRSS, which /proc/self/status states in kilobytes and in words.
    int status_descriptor = open("/proc/self/status", O_RDONLY);
    BUSTER_TEST(arguments, status_descriptor >= 0);
    if (status_descriptor >= 0)
    {
        char status_buffer[8192];
        ssize_t status_bytes = read(status_descriptor, status_buffer, sizeof(status_buffer) - 1);
        close(status_descriptor);
        BUSTER_TEST(arguments, status_bytes > 0);
        if (status_bytes > 0)
        {
            status_buffer[status_bytes] = 0;
            String8 status = string_from_pointer((char8*)status_buffer);
            String8 label_text = S8("VmRSS:");
            u64 label = status.length;
            for (u64 offset = 0; offset + label_text.length <= status.length; offset += 1)
            {
                String8 tail = {.pointer = status.pointer + offset, .length = status.length - offset};
                if (string_starts_with_sequence(tail, label_text))
                {
                    label = offset;
                    break;
                }
            }
            BUSTER_TEST(arguments, label < status.length);
            if (label < status.length)
            {
                // status separates the label from the value with a tab and
                // then pads with spaces.
                u64 cursor = label + label_text.length;
                while (cursor < status.length && (status.pointer[cursor] == ' ' || status.pointer[cursor] == '\t'))
                {
                    cursor += 1;
                }
                u64 kilobytes = 0;
                while (cursor < status.length && status.pointer[cursor] >= '0' && status.pointer[cursor] <= '9')
                {
                    kilobytes = kilobytes * 10 + (u64)(status.pointer[cursor] - '0');
                    cursor += 1;
                }
                // The two are sampled a few instructions apart, so they agree
                // to a wide tolerance rather than exactly; picking a different
                // column would miss by far more than half.
                u64 reported = kilobytes * 1024;
                BUSTER_TEST(arguments, reported > 0);
                BUSTER_TEST(arguments, resident_memory_size * 2 >= reported && resident_memory_size <= reported * 2);
            }
        }
    }
#endif

#if defined(__APPLE__)
    BUSTER_TEST(arguments, !os_apple_process_is_traced(0));
    BUSTER_TEST(arguments, os_apple_process_is_traced(P_TRACED));
#endif

#if defined(_WIN32)
    // A parent pipe end must be made non-inheritable before process creation;
    // failure is observable so spawn can close every created handle and stop.
    {
        SECURITY_ATTRIBUTES attributes = {sizeof(attributes), 0, TRUE};
        HANDLE read_pipe = 0;
        HANDLE write_pipe = 0;
        BUSTER_TEST(arguments, CreatePipe(&read_pipe, &write_pipe, &attributes, 0) != 0);
        if (read_pipe && write_pipe)
        {
            DWORD flags = HANDLE_FLAG_INHERIT;
            BUSTER_TEST(arguments, os_windows_pipe_disable_inheritance((OsFileDescriptor*)read_pipe));
            BUSTER_TEST(arguments, GetHandleInformation(read_pipe, &flags) != 0 && !(flags & HANDLE_FLAG_INHERIT));
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
        }
        BUSTER_TEST(arguments, !os_windows_pipe_disable_inheritance((OsFileDescriptor*)INVALID_HANDLE_VALUE));
    }
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    // Limits retain deterministic prefixes while every stream continues to be
    // drained. Counters account for every byte and the fail policy changes only
    // the portable result, not the native child status.
    {
        u64 arena_position = arguments->arena->position;
        String8 override_keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_TEST_JOBS")};
        String8 both_values[] = {S8("flood-both"), S8("1")};
        OsTestEnvironment both_environment = os_test_environment(arguments->arena, override_keys, both_values, BUSTER_ARRAY_LENGTH(override_keys));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        ProcessSpawnOptions truncate_options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .new_process_group = 1,
        };
        truncate_options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = BUSTER_KB(64);
        truncate_options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = BUSTER_KB(64);
        truncate_options.capture_limits.total = BUSTER_KB(128);
        ProcessSpawnResult truncate_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), both_environment.keys, both_environment.values, truncate_options);
        BUSTER_TEST(arguments, truncate_spawn.handle != 0 && truncate_spawn.process_group);
        if (truncate_spawn.handle)
        {
            ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, truncate_spawn, 30000000);
            BUSTER_TEST(arguments, waited.result == PROCESS_RESULT_SUCCESS && !waited.timed_out);
            BUSTER_TEST(arguments, waited.capture_limit_exceeded && waited.output_truncated && !waited.capture_failed);
            BUSTER_TEST(arguments, waited.observed_bytes[STANDARD_STREAM_OUTPUT] == BUSTER_KB(512));
            BUSTER_TEST(arguments, waited.observed_bytes[STANDARD_STREAM_ERROR] == BUSTER_KB(512));
            BUSTER_TEST(arguments, waited.captured_bytes[STANDARD_STREAM_OUTPUT] == BUSTER_KB(64));
            BUSTER_TEST(arguments, waited.captured_bytes[STANDARD_STREAM_ERROR] == BUSTER_KB(64));
            BUSTER_TEST(arguments, waited.dropped_total == BUSTER_KB(896));
            BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_OUTPUT].length == BUSTER_KB(64));
            BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_ERROR].length == BUSTER_KB(64));
            if (waited.streams[STANDARD_STREAM_OUTPUT].length)
            {
                BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_OUTPUT].pointer[0] == 3);
            }
            if (waited.streams[STANDARD_STREAM_ERROR].length)
            {
                BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_ERROR].pointer[0] == 7);
            }
        }
        arena_set_position(arguments->arena, arena_position);
    }
    {
        u64 arena_position = arguments->arena->position;
        String8 override_keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_TEST_JOBS")};
        String8 output_values[] = {S8("flood-output"), S8("1")};
        OsTestEnvironment environment = os_test_environment(arguments->arena, override_keys, output_values, BUSTER_ARRAY_LENGTH(override_keys));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        ProcessSpawnOptions fail_options = {
            .capture = (u64)1 << STANDARD_STREAM_OUTPUT,
            .new_process_group = 1,
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
        };
        fail_options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = BUSTER_KB(64);
        fail_options.capture_limits.total = BUSTER_KB(32);
        ProcessSpawnResult fail_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), environment.keys, environment.values, fail_options);
        BUSTER_TEST(arguments, fail_spawn.handle != 0);
        if (fail_spawn.handle)
        {
            ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, fail_spawn, 30000000);
            BUSTER_TEST(arguments, waited.result == PROCESS_RESULT_FAILED && !waited.timed_out);
            BUSTER_TEST(arguments, waited.platform_status == 0 && waited.capture_failed);
            BUSTER_TEST(arguments, waited.captured_total == BUSTER_KB(32));
            BUSTER_TEST(arguments, waited.dropped_total == BUSTER_KB(480));
        }
        arena_set_position(arguments->arena, arena_position);
    }
    {
        u64 arena_position = arguments->arena->position;
        String8 overflow_path = buster_test_temporary_path(arguments->arena, S8("process-capture-overflow"), S8(".bin"));
        BUSTER_TEST(arguments, os_file_delete(overflow_path));
        OsFileDescriptor* overflow_file =
            os_file_open(
                overflow_path,
                (OpenFlags){ .create = 1, .truncate = 1 },
                (OsFileAccess){ .write = 1 },
                (OsFileCreateMode){0},
                (OsFileShareFlags){ .read = 1, .write = 1, .delete = 1 });
        BUSTER_TEST(arguments, overflow_file != 0);
        String8 override_keys[] = {S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_TEST_JOBS")};
        String8 output_values[] = {S8("flood-output"), S8("1")};
        OsTestEnvironment environment = os_test_environment(arguments->arena, override_keys, output_values, BUSTER_ARRAY_LENGTH(override_keys));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        ProcessSpawnOptions stream_options = {
            .capture = (u64)1 << STANDARD_STREAM_OUTPUT,
            .new_process_group = 1,
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE,
        };
        stream_options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = BUSTER_KB(32);
        stream_options.capture_limits.total = BUSTER_KB(32);
        stream_options.capture_overflow_files[STANDARD_STREAM_OUTPUT] = overflow_file;
        if (overflow_file)
        {
            ProcessSpawnResult stream_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), environment.keys, environment.values, stream_options);
            BUSTER_TEST(arguments, stream_spawn.handle != 0);
            if (stream_spawn.handle)
            {
                ProcessWaitResult waited = os_process_wait_deadline(arguments->arena, stream_spawn, 30000000);
                BUSTER_TEST(arguments, waited.result == PROCESS_RESULT_SUCCESS && !waited.capture_failed);
                BUSTER_TEST(arguments, waited.captured_total == BUSTER_KB(32));
                BUSTER_TEST(arguments, waited.streamed_total == BUSTER_KB(480));
                BUSTER_TEST(arguments, waited.dropped_total == 0);
            }
            FileStats stats = os_file_get_stats(overflow_file, (FileStatsOptions){.size = 1, .identity = 1});
            BUSTER_TEST(arguments, stats.valid && stats.kind == OS_FILE_KIND_REGULAR && stats.size == BUSTER_KB(480));
            BUSTER_TEST(arguments, os_file_close(overflow_file));
            BUSTER_TEST(arguments, os_file_delete(overflow_path));
        }
        arena_set_position(arguments->arena, arena_position);
    }

    // The child creates a grandchild that inherits the captured stdio. A
    // deadline must return only after the whole owned tree is gone; otherwise
    // releasing the sentinel lets the grandchild prove its escape. On POSIX,
    // use a lightweight shell helper so readiness does not depend on running
    // unrelated sanitizer-heavy test modules in two nested processes.
    {
        u64 arena_position = arguments->arena->position;
        String8 ready = buster_test_temporary_path(arguments->arena, S8("process-tree-ready"), S8(".txt"));
        String8 release = buster_test_temporary_path(arguments->arena, S8("process-tree-release"), S8(".txt"));
        String8 escaped = buster_test_temporary_path(arguments->arena, S8("process-tree-escaped"), S8(".txt"));
        BUSTER_TEST(arguments, os_file_delete(ready));
        BUSTER_TEST(arguments, os_file_delete(release));
        BUSTER_TEST(arguments, os_file_delete(escaped));
#if BUSTER_LINUX || BUSTER_MACOS
        String8 child_arguments[] = {
            S8("/bin/sh"),
            S8("-c"),
            S8("printf 'PROCESS_TREE_PARENT_ENTERED_V1\\n' >&2; (printf 'PROCESS_TREE_DESCENDANT_ENTERED_V1\\n' >&2; "
               "printf ready > \"$1\" || exit 90; while [ ! -f \"$2\" ]; do sleep 0.01; done; "
               "printf escaped > \"$3\") & wait"),
            S8("process-tree-helper"),
            ready,
            release,
            escaped,
        };
        ProcessSpawnOptions options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = 1,
            .new_process_group = 1,
            .observe_resources = 1,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                     (SliceString8){0}, (SliceString8){0}, options);
#else
        String8 override_keys[] = {
            S8("BUSTER_OS_PROCESS_TEST_MODE"), S8("BUSTER_OS_PROCESS_READY"), S8("BUSTER_OS_PROCESS_RELEASE"),
            S8("BUSTER_OS_PROCESS_ESCAPED"),   S8("BUSTER_TEST_JOBS"),
        };
        String8 override_values[] = {S8("tree-parent"), ready, release, escaped, S8("1")};
        OsTestEnvironment environment =
            os_test_environment(arguments->arena, override_keys, override_values, BUSTER_ARRAY_LENGTH(override_keys));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        ProcessSpawnOptions options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .new_process_group = 1,
        };
        ProcessSpawnResult spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments), environment.keys, environment.values, options);
#endif
        OsError spawn_error = {0};
        if (!spawn.handle)
        {
            spawn_error = os_get_last_error();
        }
        BUSTER_TEST(arguments, spawn.handle != 0 && spawn.process_group);
        OsTestProcessTreeWait tree = {0};
        bool readiness_proven = false;
        if (spawn.handle)
        {
            // Start the behavioral deadline only after the descendant has
            // entered its parked state. If setup never becomes ready, still
            // clean the owned tree up fail-closed but report it as a handshake
            // failure rather than applying the behavioral assertions.
            tree = os_test_process_tree_wait(arguments->arena, spawn, ready, 3000, 10, 2000000);
            readiness_proven = tree.ready;
            if (tree.ready)
            {
                BUSTER_TEST(arguments, tree.waited.result == PROCESS_RESULT_FAILED && tree.waited.timed_out);
                BUSTER_TEST(arguments, tree.waited.termination_requested && tree.waited.forcibly_terminated);
                BUSTER_TEST(arguments, !tree.waited.process_tree_cleanup_failed);
#if BUSTER_LINUX
                BUSTER_TEST(arguments, tree.waited.resources.cpu_status == PROCESS_RESOURCE_OBSERVED);
                BUSTER_TEST(arguments, tree.waited.resources.memory_status == PROCESS_RESOURCE_OBSERVED);
#endif
            }
        }
        if (!readiness_proven)
        {
            BUSTER_TEST(arguments, os_test_process_tree_show_diagnostic(arguments, S8("setup-failure"), spawn,
                spawn_error, tree, ready, release, escaped));
        }
        BUSTER_TEST(arguments, readiness_proven);
        BUSTER_TEST(arguments, os_test_create_empty_file(release));
        os_test_sleep_milliseconds(300);
        BUSTER_TEST(arguments, !os_test_regular_file_exists(escaped));
        BUSTER_TEST(arguments, os_file_delete(ready));
        BUSTER_TEST(arguments, os_file_delete(release));
        BUSTER_TEST(arguments, os_file_delete(escaped));
        arena_set_position(arguments->arena, arena_position);
    }

#if BUSTER_LINUX || BUSTER_MACOS
    // A deliberately missing readiness sentinel exercises the setup-failure
    // classification. The owned process group must still be terminated and
    // reaped without applying the post-readiness behavioral assertions.
    {
        u64 arena_position = arguments->arena->position;
        String8 missing_ready =
            buster_test_temporary_path(arguments->arena, S8("process-tree-missing-ready"), S8(".txt"));
        BUSTER_TEST(arguments, os_file_delete(missing_ready));
        String8 child_arguments[] = {S8("/bin/sh"), S8("-c"), S8("while :; do :; done")};
        ProcessSpawnOptions options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = 1,
            .new_process_group = 1,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                     (SliceString8){0}, (SliceString8){0}, options);
        OsError spawn_error = {0};
        if (!spawn.handle)
        {
            spawn_error = os_get_last_error();
        }
        BUSTER_TEST(arguments, spawn.handle != 0 && spawn.process_group);
        OsTestProcessTreeWait tree = {0};
        if (spawn.handle)
        {
            tree = os_test_process_tree_wait(arguments->arena, spawn, missing_ready, 2, 1, 2000000);
        }
        BUSTER_TEST(arguments, os_test_process_tree_show_diagnostic(arguments, S8("negative-control"), spawn,
            spawn_error, tree, missing_ready, (String8){0}, (String8){0}));
        if (spawn.handle)
        {
            BUSTER_TEST(arguments, !tree.ready);
            BUSTER_TEST(arguments, tree.waited.result == PROCESS_RESULT_FAILED && tree.waited.timed_out);
            BUSTER_TEST(arguments, tree.waited.termination_requested && tree.waited.forcibly_terminated);
            BUSTER_TEST(arguments, !tree.waited.process_tree_cleanup_failed);
        }
        BUSTER_TEST(arguments, os_file_delete(missing_ready));
        arena_set_position(arguments->arena, arena_position);
    }
#endif

#endif

#if BUSTER_LINUX || BUSTER_MACOS
    // A private group with only its exited leader is a normal successful wait.
    // Darwin reports EPERM when group signalling filters out that zombie; the
    // waiter must verify that exact state before reaping without losing helpers.
    {
        String8 spawn_arguments[] = {S8("/bin/sh"), S8("-c"), S8("exit 0")};
        ProcessSpawnOptions options = {.use_process_environment = 1, .search_path = 1, .new_process_group = 1};
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments),
            (SliceString8){0}, (SliceString8){0}, options);
        BUSTER_TEST(arguments, spawn.handle != 0 && spawn.process_group);
        if (spawn.handle)
        {
            ProcessWaitResult wait_result = os_process_wait_deadline(arguments->arena, spawn, 3000000);
            BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, wait_result.platform_status == 0);
            BUSTER_TEST(arguments, !wait_result.timed_out && !wait_result.process_tree_cleanup_failed);
            BUSTER_TEST(arguments, !wait_result.process_group_reservation_retained);
            BUSTER_TEST(arguments, !wait_result.process_group_ownership_lost);
        }
    }

    // Every process-group operation uses the same ownership gate. Releasing a
    // leader must deterministically refuse both signal and query operations,
    // without testing a reusable PID/PGID in the kernel.
    BUSTER_TEST(arguments, os_process_group_reservation_release_self_test());
    // Exhausting all owner-lane recovery campaigns publishes exactly one
    // cleanup-failure event and ends in an explicit retained reservation.
    BUSTER_TEST(arguments, os_process_group_recovery_self_test());
    // Losing the exact child publishes the admission stop exactly once and
    // prevents every later signal, group query, and numeric-ID operation.
    BUSTER_TEST(arguments, os_process_group_ownership_loss_self_test());
    // An inherited writer outside the owned group cannot extend a natural
    // successful exit or turn it into a false deadline failure. Bytes already
    // buffered by the owned group are retained before the foreign FD closes.
    BUSTER_TEST(arguments, os_process_group_escaped_capture_self_test(arguments->arena));
#if BUSTER_LINUX
    // Exercise the production raw-status context selection, including repeated
    // numeric IDs. Only the final hierarchy coordinate belongs to this caller.
    {
        typedef struct OsTestProcContextCase OsTestProcContextCase;
        struct OsTestProcContextCase
        {
            String8 status;
            s32 process_id;
            bool identity_valid;
            bool valid;
            u32 namespace_depth;
            u32 namespace_index;
        };
        OsTestProcContextCase cases[] = {
            {S8("Pid:\t41\nNSpid:\t41\n"), 41, true, true, 1, 0},
            {S8("Pid:\t10041\nNSpid:\t10041\t41\n"), 41, true, true, 2, 1},
            {S8("Pid:\t10041\nNSpid:\t10041\t42\t41\n"), 41, true, true, 3, 2},
            {S8("Pid:\t41\nNSpid:\t41\t41\n"), 41, true, true, 2, 1},
            {S8("Pid:\t10041\nNSpid:\t10041\t41\t41\n"), 41, true, true, 3, 2},
            {S8("Pid:\t41\nNSpid:\t41\t42\t41\n"), 41, true, true, 3, 2},
            {S8("Pid:\t41\nNSpid:\t41\t41\t41\n"), 41, true, true, 3, 2},
            {S8("Pid:\t41\nNSpid:\t41\t42\n"), 41, true, false, 0, 0},
            {S8("Pid:\t10041\nNSpid:\t10041\t41\t42\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\t41\n"), 42, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\t41\n"), 41, false, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\n"), 41, false, false, 0, 0},
            {S8("Pid:\t0\nNSpid:\t0\n"), 0, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\n"), -1, true, false, 0, 0},
            {S8("Pid:\t41\n"), 41, true, false, 0, 0},
            {S8("NSpid:\t41\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t42\t41\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nPid:\t41\nNSpid:\t41\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\t41\nNSpid:\t41\t41\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\tbroken\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\t2147483648\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41\t-41\n"), 41, true, false, 0, 0},
            {S8("Pid:\t41\nNSpid:\t41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41"
                " 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41\n"), 41, true, true, 32, 31},
            {S8("Pid:\t41\nNSpid:\t41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41"
                " 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41\n"), 41, true, false, 0, 0},
            {S8(""), 41, true, false, 0, 0},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
        {
            OsTestProcContextCase test = cases[index];
            u32 namespace_index = UINT32_MAX;
            u32 namespace_depth = UINT32_MAX;
            bool valid = os_linux_proc_context_select_self_test(test.status, test.process_id, test.identity_valid,
                &namespace_index, &namespace_depth);
            BUSTER_TEST(arguments, valid == test.valid);
            BUSTER_TEST(arguments, namespace_index == test.namespace_index);
            BUSTER_TEST(arguments, namespace_depth == test.namespace_depth);
        }
        u64 resources_before = os_process_spawn_test_resource_count();
        BUSTER_TEST(arguments, resources_before != UINT64_MAX);
        BUSTER_TEST(arguments, os_linux_proc_context_live_self_test());
        BUSTER_TEST(arguments, os_process_spawn_test_resource_count() == resources_before);
    }
    // /proc stat parsing must use the final command-name parenthesis and fail
    // closed on mismatched identities or malformed group fields.
    BUSTER_TEST(arguments, os_linux_process_stat_parse_self_test());
    // Skip stat reads only for a known unrelated group in the same namespace;
    // matching groups, ancestor coordinates and failed queries still read it.
    BUSTER_TEST(arguments, os_linux_process_group_filter_self_test());
    // A held unread proc descriptor must classify a reaped task as vanished;
    // empty, oversized and invalid-descriptor reads remain ordinary failures.
    BUSTER_TEST(arguments, os_linux_proc_read_self_test());
    // Unrelated PIDs disappearing between readdir and stat, once or in every
    // snapshot, are ordinary host churn and cannot invalidate a stable
    // target-group proof or consume a retry.
    BUSTER_TEST(arguments, os_linux_process_group_churn_self_test(arguments->arena));
#endif

    // A background helper proves it started before the leader exits, then
    // writes a second sentinel only if it survives process-group cleanup.
    // This exercises Darwin's kernel snapshot and Linux's stable /proc census.
    // Inspect files rather than probing a released PID/PGID.
    {
        String8 ready_sentinel = buster_test_temporary_path(arguments->arena, S8("buster-process-group-ready"), S8(".txt"));
        String8 release_sentinel = buster_test_temporary_path(arguments->arena, S8("buster-process-group-release"), S8(".txt"));
        String8 escaped_sentinel = buster_test_temporary_path(arguments->arena, S8("buster-process-group-escaped"), S8(".txt"));
        BUSTER_TEST(arguments, os_file_delete(ready_sentinel));
        BUSTER_TEST(arguments, os_file_delete(release_sentinel));
        BUSTER_TEST(arguments, os_file_delete(escaped_sentinel));
        String8 spawn_arguments[] = {
            S8("/bin/sh"),
            S8("-c"),
            S8("(printf ready > \"$1\"; while [ ! -f \"$2\" ]; do sleep 0.001; done; printf escaped > \"$3\") & "
               "i=0; while [ ! -f \"$1\" ] && [ \"$i\" -lt 1000 ]; do sleep 0.001; i=$((i + 1)); done; [ -f \"$1\" ]"),
            S8("process-group-helper"),
            ready_sentinel,
            release_sentinel,
            escaped_sentinel,
        };
        ProcessSpawnOptions options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = 1, .search_path = 1,
            .new_process_group = 1,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments),
            (SliceString8){0}, (SliceString8){0}, options);
        BUSTER_TEST(arguments, spawn.handle != 0 && spawn.process_group);
        if (spawn.handle)
        {
            ProcessWaitResult wait_result = os_process_wait_deadline(arguments->arena, spawn, 3000000);
            BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, wait_result.platform_status == 0);
            BUSTER_TEST(arguments, !wait_result.timed_out);
            BUSTER_TEST(arguments, !wait_result.process_group_reservation_retained);
            BUSTER_TEST(arguments, !wait_result.process_group_ownership_lost);
        }

        OsFileOpenResult ready_open = os_file_open_checked(
            ready_sentinel,
            (OpenFlags){0},
            (OsFileAccess){ .read = 1 },
            (OsFileCreateMode){0},
            (OsFileShareFlags){ .read = 1 });
        bool helper_ready = ready_open.file != 0;
        if (ready_open.file) { BUSTER_TEST(arguments, os_file_close(ready_open.file)); }
        BUSTER_TEST(arguments, helper_ready);

        OsFileDescriptor* release_file = os_file_open(
            release_sentinel,
            (OpenFlags){ .create = 1, .truncate = 1 },
            (OsFileAccess){ .write = 1 },
            (OsFileCreateMode){0},
            (OsFileShareFlags){ .read = 1, .write = 1, .delete = 1 });
        BUSTER_TEST(arguments, release_file != 0);
        if (release_file) { BUSTER_TEST(arguments, os_file_close(release_file)); }
        poll(0, 0, 300);
        OsFileOpenResult escaped_open = os_file_open_checked(
            escaped_sentinel,
            (OpenFlags){0},
            (OsFileAccess){ .read = 1 },
            (OsFileCreateMode){0},
            (OsFileShareFlags){ .read = 1 });
        bool helper_escaped = escaped_open.file != 0;
        if (escaped_open.file) { BUSTER_TEST(arguments, os_file_close(escaped_open.file)); }
        BUSTER_TEST(arguments, !helper_escaped);
        if (helper_ready) { BUSTER_TEST(arguments, os_file_delete(ready_sentinel)); }
        if (release_file) { BUSTER_TEST(arguments, os_file_delete(release_sentinel)); }
        if (helper_escaped) { BUSTER_TEST(arguments, os_file_delete(escaped_sentinel)); }
    }

    // A hot inherited writer keeps poll continuously readable. Wait until its
    // first output is written before starting the deadline, so a slow shell
    // startup cannot make the output assertion depend on scheduler timing.
    // The owner kills the group, proves every member quiescent, and drains
    // the captured pipe to EOF.
    {
        u64 position = arguments->arena->position;
        String8 ready = buster_test_temporary_path(arguments->arena, S8("buster-hot-writer-ready"), S8(".txt"));
        BUSTER_TEST(arguments, os_file_delete(ready));
        String8 spawn_arguments[] = {
            S8("/bin/sh"),
            S8("-c"),
            S8("printf 0123456789abcdef0123456789abcdef || exit 90; printf ready > \"$1\" || exit 90; "
               "while :; do printf 0123456789abcdef0123456789abcdef; done"),
            S8("hot-writer-helper"),
            ready,
        };
        ProcessSpawnOptions options = {
            .capture = (u64)1 << STANDARD_STREAM_OUTPUT,
            .use_process_environment = 1, .search_path = 1,
            .new_process_group = 1,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments),
            (SliceString8){0}, (SliceString8){0}, options);
        BUSTER_TEST(arguments, spawn.handle != 0 && spawn.process_group);
        if (spawn.handle)
        {
            OsTestProcessTreeWait tree = os_test_process_tree_wait(arguments->arena, spawn, ready, 3000, 10, 100000);
            BUSTER_TEST(arguments, tree.ready);
            if (tree.ready)
            {
                BUSTER_TEST(arguments, tree.waited.result == PROCESS_RESULT_FAILED);
                BUSTER_TEST(arguments, tree.waited.timed_out);
                BUSTER_TEST(arguments, tree.waited.streams[STANDARD_STREAM_OUTPUT].length > 0);
                BUSTER_TEST(arguments, !tree.waited.process_group_reservation_retained);
                BUSTER_TEST(arguments, !tree.waited.process_group_ownership_lost);
            }
        }
        BUSTER_TEST(arguments, os_file_delete(ready));
        arena_set_position(arguments->arena, position);
    }

    // realpath may write a resolved prefix even on failure. Discarding its
    // oversized output allocation must not make those bytes look fresh to
    // arena_allocate_zeroed. Use a fresh mapping so no prior dirty watermark
    // can accidentally hide the broken rewind.
    for (u32 terminate = 0; terminate < 2; terminate += 1)
    {
        Arena* path_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = 1}});
        BUSTER_TEST(arguments, path_arena != 0);
        if (path_arena)
        {
            String8 missing = buster_test_temporary_path(arguments->arena, S8("buster-missing-realpath"), S8(""));
            String8 absolute = os_path_absolute(path_arena, missing, terminate != 0);
            BUSTER_TEST(arguments, absolute.length == 0);
            BUSTER_TEST(arguments, arena_dirty_position(path_arena) > path_arena->position);
            u8* bytes = arena_allocate_zeroed(path_arena, u8, 4096);
            bool zeroed = true;
            for (u32 index = 0; index < 4096; index += 1)
            {
                zeroed = zeroed && bytes[index] == 0;
            }
            BUSTER_TEST(arguments, zeroed);
            BUSTER_TEST(arguments, arena_destroy(path_arena, 1));
        }
    }

    // Regression for #653: POSIX path helpers must accept bounded slices
    // without reading past their length and must reject embedded NULs.
    {
        Arena* arena = arguments->arena;
        char8 bounded[] = {'b', 'u', 's', 't', 'e', 'r', '-', '6', '5', '3', '-', 'm', 'i', 's', 's', 'i', 'n', 'g'};
        BUSTER_TEST(arguments, !os_path_absolute(arena, (String8){bounded, sizeof(bounded)}, true).length);
        char8 invalid[] = {'a', 0, 'b'};
        BUSTER_TEST(arguments, !os_path_absolute(arena, (String8){invalid, sizeof(invalid)}, true).length);
        BUSTER_TEST(arguments, !os_path_absolute(arena, (String8){0}, true).length);
        OsDirectoryCreateResult invalid_create = os_make_directory((String8){invalid, sizeof(invalid)});
        OsDirectoryCreateResult empty_create = os_make_directory((String8){0});
        BUSTER_TEST(arguments, invalid_create.error.v != 0 && !invalid_create.created && !invalid_create.already_exists);
        BUSTER_TEST(arguments, empty_create.error.v != 0 && !empty_create.created && !empty_create.already_exists);
    }

    // Regression: draining captured stdout/stderr sequentially deadlocked when
    // the child filled one pipe while the parent blocked on the other. The
    // child writes far more than a pipe buffer to stderr before touching
    // stdout.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        String8 spawn_arguments[] = {
            S8("/bin/sh"),
            S8("-c"),
            S8("head -c 262144 /dev/zero | tr '\\0' e >&2; head -c 262144 /dev/zero | tr '\\0' o"),
        };
        ProcessSpawnOptions options = {
            .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
            .use_process_environment = 1, .search_path = 1,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments), (SliceString8){0}, (SliceString8){0}, options);
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait_result = os_process_wait_sync(arena, spawn);
            BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, wait_result.streams[STANDARD_STREAM_OUTPUT].length == 262144);
            BUSTER_TEST(arguments, wait_result.streams[STANDARD_STREAM_ERROR].length == 262144);
        }

        arena_set_position(arena, position);
    }

    // Regression: a captured stdin used to leave the parent's write end open
    // (and, on Windows, made the child's read end non-inheritable), so a
    // child reading stdin to EOF deadlocked the wait.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        String8 spawn_arguments[] = {
            S8("/bin/sh"),
            S8("-c"),
            S8("cat; printf ok"),
        };
        ProcessSpawnOptions options = {
            .capture = ((u64)1 << STANDARD_STREAM_INPUT) | ((u64)1 << STANDARD_STREAM_OUTPUT),
            .use_process_environment = 1, .search_path = 1,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments), (SliceString8){0}, (SliceString8){0}, options);
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait_result = os_process_wait_sync(arena, spawn);
            BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, wait_result.streams[STANDARD_STREAM_OUTPUT].length == 2);
        }

        arena_set_position(arena, position);
    }

    // Exit codes past the ProcessResult range must not alias enum values, and
    // a child killed by a signal reports a crash rather than an exit code.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        struct
        {
            String8 script;
            ProcessResult expected;
        } exit_cases[] = {
            {S8("exit 200"), PROCESS_RESULT_FAILED},
            {S8("kill -SEGV $$"), PROCESS_RESULT_CRASH},
        };

        for (EACH_ARRAY_INDEX(i, exit_cases))
        {
            String8 spawn_arguments[] = {
                S8("/bin/sh"),
                S8("-c"),
                exit_cases[i].script,
            };
            // Spawn with an empty environment: under sanitized builds the
            // suite runs with LD_PRELOAD pointing at the ASan runtime, and an
            // inherited preload would catch the child's SIGSEGV and turn it
            // into a plain exit(1) instead of a death by signal.
            ProcessSpawnOptions options = {
                .capture = ((u64)1 << STANDARD_STREAM_ERROR),
            };
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments), (SliceString8){0}, (SliceString8){0}, options);
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait_result = os_process_wait_sync(arena, spawn);
                BUSTER_TEST(arguments, wait_result.result == exit_cases[i].expected);
            }
        }

        arena_set_position(arena, position);
    }

    // A run that stops making progress is killed at its deadline rather than
    // waited on forever, and whatever it had already written still comes back.
    // Both shapes are covered because they take different paths: a child with
    // no captured stream never enters the pipe drain loop at all, which is
    // exactly the shape the self-host stages have.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        struct
        {
            String8 script;
            bool capture_output;
            u64 timeout_microseconds;
            bool expected_timeout;
            u64 expected_output_length;
            bool expire_after_ready;
        } deadline_cases[] = {
            {S8("sleep 30"), false, 100000, true, 0, false},
            // The deadline includes shell startup, so this short timeout must not require child output.
            {S8("sleep 30"), true, 100000, true, 0, false},
            {S8("printf ok"), true, 30000000, false, 2, false},
            {S8("printf ok"), false, 0, false, 0, false},
            {S8("printf ok; sleep 30"), true, 30000000, true, 2, true},
        };

        for (EACH_ARRAY_INDEX(i, deadline_cases))
        {
            String8 spawn_arguments[] = {
                S8("/bin/sh"),
                S8("-c"),
                deadline_cases[i].script,
            };
            // Set from a constant rather than a stored mask: `capture` is a
            // three-bit field, and GCC rejects a runtime u64 narrowed into it.
            ProcessSpawnOptions options = {
                .use_process_environment = 1, .search_path = 1,
            };
            if (deadline_cases[i].capture_output)
            {
                options.capture = (u64)1 << STANDARD_STREAM_OUTPUT;
            }
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments), (SliceString8){0}, (SliceString8){0}, options);
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                if (deadline_cases[i].expire_after_ready) { os_process_wait_test_expire_deadline_after_ready_once(); }
                ProcessWaitResult wait_result = os_process_wait_deadline(arena, spawn, deadline_cases[i].timeout_microseconds);
                BUSTER_TEST(arguments, (wait_result.timed_out != 0) == deadline_cases[i].expected_timeout);
                BUSTER_TEST(arguments, wait_result.result == (deadline_cases[i].expected_timeout ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS));
                BUSTER_TEST(arguments, !wait_result.capture_failed);
                BUSTER_TEST(arguments, wait_result.streams[STANDARD_STREAM_OUTPUT].length == deadline_cases[i].expected_output_length);
            }
        }

        arena_set_position(arena, position);
    }

    // Regression: executable_resolve_in_path must treat empty PATH components
    // as the current directory and must skip directories ("cmake" is a
    // directory in the repository root, which is the test working directory).
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        SliceString8 keys = program_state->input.environment_keys;
        u64 path_index = BUSTER_STRING_NO_MATCH;
        for (u64 i = 0; i < keys.length; i += 1)
        {
            if (string_equal(keys.pointer[i], S8("PATH")))
            {
                path_index = i;
                break;
            }
        }

        BUSTER_TEST(arguments, path_index != BUSTER_STRING_NO_MATCH);
        if (path_index != BUSTER_STRING_NO_MATCH)
        {
            String8* path_value = &program_state->input.environment_values.pointer[path_index];
            String8 saved_path = *path_value;
            *path_value = S8(":");

            String8 resolved = executable_resolve_in_path(arena, S8("build.sh"));
            BUSTER_TEST(arguments, string_ends_with_sequence(resolved, S8("/build.sh")));

            String8 directory_resolved = executable_resolve_in_path(arena, S8("cmake"));
            BUSTER_TEST(arguments, directory_resolved.length == 0);

            *path_value = S8("");
            resolved = executable_resolve_in_path(arena, S8("build.sh"));
            BUSTER_TEST(arguments, string_ends_with_sequence(resolved, S8("/build.sh")));
            *path_value = (String8){0};
            resolved = executable_resolve_in_path(arena, S8("build.sh"));
            BUSTER_TEST(arguments, string_ends_with_sequence(resolved, S8("/build.sh")));
            BUSTER_TEST(arguments, executable_resolve_in_path(arena, S8("")).length == 0);

            *path_value = saved_path;
        }

        arena_set_position(arena, position);
    }
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
    // Returned paths must outlive the helper's scratch scope even when the
    // caller deliberately chooses either scratch arena as its output arena.
    // The OS module is serialized, so replacing the process PATH snapshot here
    // cannot race other tests. The fixture is ours, not a host-installed tool.
    {
        Arena* arena = arguments->arena;
        String8 root = buster_test_temporary_path(arena, S8("buster-path-lifetime space \xc3\xa9"), S8(""));
        os_make_directory(root);
        String8 path = string_format_z(arena, S8("{S8}/probe.exe"), root);
        OsFileDescriptor* file = os_file_open(
            path,
            (OpenFlags){ .create = true, .truncate = true },
            (OsFileAccess){ .write = true },
            (OsFileCreateMode){.kind = OS_FILE_CREATE_MODE_EXECUTABLE},
            (OsFileShareFlags){ .read = true, .write = true, .delete = true });
        BUSTER_TEST(arguments, file != 0);
        if (file)
        {
            os_file_close(file);
            String8 alias = string_format_z(arena, S8("{S8}/./probe.exe"), root);
            String8 absolute = os_path_absolute(arena, path, true);
            SliceString8 saved_keys = program_state->input.environment_keys;
            SliceString8 saved_values = program_state->input.environment_values;
#if defined(_WIN32)
            String8 keys[] = {S8("Path")};
#else
            String8 keys[] = {S8("PATH")};
#endif
            String8 values[] = {root};
            program_state->input.environment_keys = (SliceString8)BUSTER_ARRAY_TO_SLICE(keys);
            program_state->input.environment_values = (SliceString8)BUSTER_ARRAY_TO_SLICE(values);
            ThreadContext* context = thread_context_selected();
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(context->arenas); index += 1)
            {
                Arena* output = context->arenas[index];
                u64 start = output->position;
                String8 resolved = executable_resolve_in_path(output, S8("probe.exe"));
                bool retained = output->position > start;
                memset(arena_allocate(output, u8, 1024), 0xa5, 1024);
                bool intact = string_equal(resolved, path) && resolved.pointer[resolved.length] == 0;
                arena_set_position(output, start);
                BUSTER_TEST(arguments, retained);
                BUSTER_TEST(arguments, intact);

                String8 canonical = os_path_absolute(output, alias, true);
                retained = output->position > start;
                memset(arena_allocate(output, u8, 1024), 0x5a, 1024);
                intact = absolute.length && string_equal(canonical, absolute) && canonical.pointer[canonical.length] == 0;
                arena_set_position(output, start);
                BUSTER_TEST(arguments, retained);
                BUSTER_TEST(arguments, intact);
            }
            String8 key_spellings[] = {S8("Path"), S8("PATH"), S8("path"), S8("pAtH"), S8("PATHX")};
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(key_spellings); i += 1)
            {
                keys[0] = key_spellings[i];
#if defined(_WIN32)
                bool expected = i < 4;
#else
                bool expected = string_equal(keys[0], S8("PATH"));
#endif
                String8 resolved = executable_resolve_in_path(arena, S8("probe.exe"));
                BUSTER_TEST(arguments, expected ? string_equal(resolved, path) : resolved.length == 0);
            }
            keys[0] = S8("PATH");
#if defined(_WIN32)
            String8 spellings[] = {S8("upper.EXE"), S8("mixed.ExE")};
#else
            // A backslash is a filename byte on POSIX, not a separator.
            String8 spellings[] = {S8("back\\slash")};
#endif
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
            {
                String8 spelled_path = string_format_z(arena, S8("{S8}/{S8}"), root, spellings[i]);
                OsFileDescriptor* spelled = os_file_open(
                    spelled_path,
                    (OpenFlags){ .create = true, .truncate = true },
                    (OsFileAccess){ .write = true },
                    (OsFileCreateMode){.kind = OS_FILE_CREATE_MODE_EXECUTABLE},
                    (OsFileShareFlags){ .read = true, .write = true, .delete = true });
                BUSTER_TEST(arguments, spelled != 0);
                if (spelled)
                {
                    os_file_close(spelled);
                    String8 resolved = executable_resolve_in_path(arena, spellings[i]);
                    BUSTER_TEST(arguments, string_equal(resolved, spelled_path));
                }
            }
#if defined(_WIN32)
            String8 native_root = os_path_absolute(arena, root, true);
            BUSTER_TEST(arguments, native_root.length != 0);
            if (native_root.length)
            {
                for (u64 i = 0; i < native_root.length; i += 1)
                {
                    if (native_root.pointer[i] == '/')
                    {
                        native_root.pointer[i] = '\\';
                    }
                }
                values[0] = native_root;
                String8 resolved = executable_resolve_in_path(arena, S8("probe.exe"));
                BUSTER_TEST(arguments, resolved.length != 0);
                String8 extended_root = string_format_z(arena, S8("\\\\?\\{S8}"), native_root);
                values[0] = extended_root;
                resolved = executable_resolve_in_path(arena, S8("probe.exe"));
                String8 expected = string_format_z(arena, S8("{S8}\\probe.exe"), extended_root);
                BUSTER_TEST(arguments, string_equal(resolved, expected));
            }
#endif
            program_state->input.environment_keys = (SliceString8){0};
            program_state->input.environment_values = (SliceString8){0};
            BUSTER_TEST(arguments, executable_resolve_in_path(arena, S8("probe.exe")).length == 0);
            program_state->input.environment_keys = saved_keys;
            program_state->input.environment_values = saved_values;
        }
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
#endif

    // os_directory_delete removes a populated tree, and reports success for a
    // path that is already absent so callers can clean up unconditionally.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        String8 root = buster_test_temporary_path(arena, S8("buster-delete-tree"), S8(""));
        os_directory_delete(root);
        os_make_directory(root);
        String8 nested = string_format_z(arena, S8("{S8}/nested"), root);
        os_make_directory(nested);
        BUSTER_TEST(arguments, file_write(string_format_z(arena, S8("{S8}/top.txt"), root), BUSTER_SLICE_TO_BYTE_SLICE(S8("top"))));
        BUSTER_TEST(arguments, file_write(string_format_z(arena, S8("{S8}/deep.txt"), nested), BUSTER_SLICE_TO_BYTE_SLICE(S8("deep"))));

        // os_directory_entry_names lists direct children only, without "."
        // and "..", and nothing for a missing directory.
        u32 entry_count = 0;
        String8* entries = os_directory_entry_names(arena, root, &entry_count);
        BUSTER_TEST(arguments, entry_count == 2);
        bool saw_nested = false;
        bool saw_top = false;
        for (u32 index = 0; index < entry_count; index += 1)
        {
            saw_nested = saw_nested || string_equal(entries[index], S8("nested"));
            saw_top = saw_top || string_equal(entries[index], S8("top.txt"));
        }
        BUSTER_TEST(arguments, saw_nested && saw_top);

        OpenFlags read_flags = {0};
        OsFileAccess read_access = {.read = 1};
        OsFileCreateMode read_create_mode = {0};
        OsFileShareFlags read_share_flags = {.read = 1};
        BUSTER_TEST(arguments, os_directory_delete(root));
        entries = os_directory_entry_names(arena, root, &entry_count);
        BUSTER_TEST(arguments, entry_count == 0 && entries == 0);
        OsFileDescriptor* deleted = os_file_open(root, read_flags, read_access, read_create_mode, read_share_flags);
        BUSTER_TEST(arguments, deleted == 0);
        // Idempotent: a second delete of the same path still reports success.
        BUSTER_TEST(arguments, os_directory_delete(root));
        // An empty path is rejected rather than treated as the current directory.
        String8 empty_path = {0};
        BUSTER_TEST(arguments, !os_directory_delete(empty_path));

        String8 deep = buster_test_temporary_path(arena, S8("buster-delete-deep"), S8(""));
#if BUSTER_WINDOWS
        // The worklist test must really create all 256 levels. An ordinary
        // relative Win32 path stops at MAX_PATH and used to turn this test into
        // a shallow no-op, so use the extended-length absolute path form.
        String8 deep_absolute = os_path_absolute(arena, deep, true);
        BUSTER_TEST(arguments, deep_absolute.length != 0);
        deep_absolute = string_duplicate_arena(arena, deep_absolute, true);
        for (u64 i = 0; i < deep_absolute.length; i += 1)
        {
            if (deep_absolute.pointer[i] == '/')
            {
                deep_absolute.pointer[i] = '\\';
            }
        }
        deep = string_format_z(arena, S8("\\\\?\\{S8}"), deep_absolute);
#endif
        String8 deep_root = deep;
        os_directory_delete(deep);
        os_make_directory(deep);
        for (u32 depth = 0; depth < 256; depth += 1)
        {
#if BUSTER_WINDOWS
            deep = string_format_z(arena, S8("{S8}\\d"), deep);
#else
            deep = string_format_z(arena, S8("{S8}/d"), deep);
#endif
            os_make_directory(deep);
        }
#if BUSTER_WINDOWS
        String8 deep_leaf = string_format_z(arena, S8("{S8}\\leaf.txt"), deep);
#else
        String8 deep_leaf = string_format_z(arena, S8("{S8}/leaf.txt"), deep);
#endif
        BUSTER_TEST(arguments, file_write(deep_leaf, BUSTER_SLICE_TO_BYTE_SLICE(S8("leaf"))));
        BUSTER_TEST(arguments, os_directory_delete(deep_root));

        arena_set_position(arena, position);
    }

#if defined(__linux__) || defined(__APPLE__)
    // A symbolic link inside the tree is unlinked, never followed: deleting the
    // tree must not reach through it and delete the target outside.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;

        String8 outside = buster_test_temporary_path(arena, S8("buster-delete-outside"), S8(".txt"));
        String8 root = buster_test_temporary_path(arena, S8("buster-delete-link"), S8(""));
        os_directory_delete(root);
        os_file_delete(outside);
        BUSTER_TEST(arguments, file_write(outside, BUSTER_SLICE_TO_BYTE_SLICE(S8("keep me"))));
        os_make_directory(root);
        String8 link = string_format_z(arena, S8("{S8}/link.txt"), root);
        BUSTER_TEST(arguments, symlink((const char*)outside.pointer, (const char*)link.pointer) == 0);

        OpenFlags link_read_flags = {0};
        OsFileAccess link_read_access = {.read = 1};
        OsFileCreateMode link_read_create_mode = {0};
        OsFileShareFlags link_read_share_flags = {.read = 1};
        BUSTER_TEST(arguments, os_directory_delete(root));
        OsFileDescriptor* survivor = os_file_open(outside, link_read_flags, link_read_access, link_read_create_mode, link_read_share_flags);
        BUSTER_TEST(arguments, survivor != 0);
        if (survivor)
        {
            os_file_close(survivor);
        }
        os_file_delete(outside);

        String8 outside_directory = buster_test_temporary_path(arena, S8("buster-delete-outside-directory"), S8(""));
        String8 directory_root = buster_test_temporary_path(arena, S8("buster-delete-directory-link"), S8(""));
        os_directory_delete(outside_directory);
        os_directory_delete(directory_root);
        os_make_directory(outside_directory);
        os_make_directory(directory_root);
        String8 outside_file = string_format_z(arena, S8("{S8}/survivor.txt"), outside_directory);
        BUSTER_TEST(arguments, file_write(outside_file, BUSTER_SLICE_TO_BYTE_SLICE(S8("keep me too"))));
        String8 directory_link = string_format_z(arena, S8("{S8}/linked-directory"), directory_root);
        BUSTER_TEST(arguments, symlink((const char*)outside_directory.pointer, (const char*)directory_link.pointer) == 0);
        BUSTER_TEST(arguments, os_directory_delete(directory_root));
        OsFileDescriptor* directory_survivor = os_file_open(outside_file, link_read_flags, link_read_access, link_read_create_mode, link_read_share_flags);
        BUSTER_TEST(arguments, directory_survivor != 0);
        if (directory_survivor)
        {
            os_file_close(directory_survivor);
        }
        os_directory_delete(outside_directory);

        arena_set_position(arena, position);
    }
#endif

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS && !BUSTER_SINGLE_THREADED
    // A hostile peer repeatedly replaces a child directory with a link to an
    // outside directory while deletion runs. The operation may report a race,
    // but it must never resolve through the link or touch the outside file.
    {
        enum
        {
            OS_TEST_DIRECTORY_DELETE_RACE_ROUNDS = 8,
            OS_TEST_DIRECTORY_DELETE_RACE_FILLERS = 64,
            OS_TEST_DIRECTORY_DELETE_RACE_SWAPS = 32,
        };
        Arena* arena = arguments->arena;
        u64 position = arena->position;
        String8 outside = buster_test_temporary_path(arena, S8("buster-delete-race-outside"), S8(""));
        String8 root = buster_test_temporary_path(arena, S8("buster-delete-race-root"), S8(""));
        BUSTER_TEST(arguments, os_directory_delete(outside));
        BUSTER_TEST(arguments, os_directory_delete(root));
        BUSTER_TEST(arguments, os_make_directory_attempt(outside));
        String8 outside_file = string_format_z(arena, S8("{S8}/survivor.txt"), outside);
        BUSTER_TEST(arguments, file_write(outside_file, BUSTER_SLICE_TO_BYTE_SLICE(S8("outside must survive"))));
        String8 outside_absolute = os_path_absolute(arena, outside, true);
        BUSTER_TEST(arguments, outside_absolute.length != 0);

        bool survivor_present = outside_absolute.length != 0;
        u64 swap_count = 0;
        for (u32 round = 0; round < OS_TEST_DIRECTORY_DELETE_RACE_ROUNDS && survivor_present; round += 1)
        {
            BUSTER_TEST(arguments, os_directory_delete(root));
            BUSTER_TEST(arguments, os_make_directory_attempt(root));
            String8 child = string_format_z(arena, S8("{S8}/child"), root);
            String8 parked = string_format_z(arena, S8("{S8}/parked"), root);
            BUSTER_TEST(arguments, os_make_directory_attempt(child));
            BUSTER_TEST(arguments,
                        file_write(string_format_z(arena, S8("{S8}/inside.txt"), child),
                                   BUSTER_SLICE_TO_BYTE_SLICE(S8("inside"))));
            for (u32 index = 0; index < OS_TEST_DIRECTORY_DELETE_RACE_FILLERS; index += 1)
            {
                String8 filler = string_format_z(arena, S8("{S8}/filler-{u32}.txt"), root, index);
                BUSTER_TEST(arguments, file_write(filler, BUSTER_SLICE_TO_BYTE_SLICE(S8("filler"))));
            }

            OsTestDirectoryDeleteRaceState state = {
                .child = child,
                .parked = parked,
                .outside = outside_absolute,
                .limit = OS_TEST_DIRECTORY_DELETE_RACE_SWAPS,
            };
            OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){
                .callback = &os_test_directory_delete_race,
                .argument = &state,
            });
            BUSTER_TEST(arguments, thread != 0);
            if (thread)
            {
                atomic_u64_increment(&state.start);
                u32 wait_count = 0;
                while (!state.swaps && wait_count < 1000)
                {
                    poll(0, 0, 1);
                    wait_count += 1;
                }
                BUSTER_TEST(arguments, state.swaps != 0);
                (void)os_directory_delete(root);
                atomic_u64_increment(&state.stop);
                BUSTER_TEST(arguments, os_thread_join(thread));
                swap_count += state.swaps;
            }

            OsFileDescriptor* survivor = os_file_open(
                outside_file,
                (OpenFlags){0},
                (OsFileAccess){ .read = 1 },
                (OsFileCreateMode){0},
                (OsFileShareFlags){ .read = 1 });
            survivor_present = survivor != 0;
            BUSTER_TEST(arguments, survivor_present);
            if (survivor)
            {
                BUSTER_TEST(arguments, os_file_close(survivor));
            }
            BUSTER_TEST(arguments, os_directory_delete(root));
        }
        BUSTER_TEST(arguments, swap_count >= OS_TEST_DIRECTORY_DELETE_RACE_ROUNDS);
        BUSTER_TEST(arguments, os_directory_delete(outside));
        arena_set_position(arena, position);
    }
#endif

#if defined(_WIN32)
    // Regression: the tick-to-nanosecond conversion overflowed u64 for
    // intervals over ~30 minutes at a 10 MHz QueryPerformanceCounter rate.
    {
        TimeDataType start = 0;
        TimeDataType end = (TimeDataType)(os_state.frequency * 7200);
        BUSTER_TEST(arguments, timestamp_ns_between(start, end) == (u64)7200 * 1000 * 1000 * 1000);
    }
#endif

    // Outside a gang the selected thread context answers as the only lane, so
    // lane-style code must run serially without a separate path.
    {
        BUSTER_TEST(arguments, lane_index() == 0);
        BUSTER_TEST(arguments, lane_count() == 1);
        lane_sync();
        u64 kept = 41;
        lane_broadcast(&kept, sizeof(kept), 0);
        BUSTER_TEST(arguments, kept == 41);
        LaneRange whole = lane_range(17);
        BUSTER_TEST(arguments, whole.start == 0 && whole.end == 17);

        AtomicU64 counter = 7;
        BUSTER_TEST(arguments, atomic_u64_increment(&counter) == 7);
        BUSTER_TEST(arguments, atomic_u64_add(&counter, 5) == 8);
        BUSTER_TEST(arguments, counter == 13);
        BUSTER_TEST(arguments, atomic_u64_decrement(&counter) == 13);
        BUSTER_TEST(arguments, counter == 12);
        BUSTER_TEST(arguments, atomic_u64_load(&counter) == 12);
        counter = UINT64_MAX;
        BUSTER_TEST(arguments, atomic_u64_load(&counter) == UINT64_MAX);
        counter = UINT64_C(0x123456789abcdef0);
        BUSTER_TEST(arguments, atomic_u64_load(&counter) == UINT64_C(0x123456789abcdef0));
        counter = 12;

        ProcessControlAtomic control = 0;
        BUSTER_TEST(arguments, process_control_atomic_load(&control) == 0);
        process_control_atomic_store(&control, 0x7fffffffull);
        BUSTER_TEST(arguments, process_control_atomic_load(&control) == 0x7fffffffull);
        BUSTER_TEST(arguments, !process_control_atomic_set_if_zero(&control, 1));
        BUSTER_TEST(arguments, process_control_atomic_load(&control) == 0x7fffffffull);
        process_control_atomic_store(&control, 0);
        BUSTER_TEST(arguments, process_control_atomic_set_if_zero(&control, 1));
        BUSTER_TEST(arguments, process_control_atomic_load(&control) == 1);
#if BUSTER_SINGLE_THREADED
        control = -1;
        BUSTER_TEST(arguments, process_control_atomic_load(&control) == UINT64_MAX);
        control = INT32_MIN;
        BUSTER_TEST(arguments, process_control_atomic_load(&control) == UINT64_MAX - 0x7fffffffull);
#endif
    }

    // os_is_only_live_thread() is what BUSTER_CHECK_SERIAL_INITIALIZATION
    // rests on, so it has to be exact at both edges: false for as long as a
    // created thread could still touch a global, true again once it is joined.
    // The worker reports its own view too, because a thread that thought it
    // was alone would defeat the guard from the inside.
    {
        BUSTER_TEST(arguments, os_is_only_live_thread());
#if !BUSTER_SINGLE_THREADED
        OsTestThreadLivenessState liveness = {0};
        OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){
            .callback = &os_test_thread_liveness,
            .argument = &liveness,
        });
        BUSTER_TEST(arguments, thread != 0);
        if (thread)
        {
            while (!liveness.started)
            {
            }
            BUSTER_TEST(arguments, !os_is_only_live_thread());
            atomic_u64_increment(&liveness.release);
            BUSTER_TEST(arguments, os_thread_join(thread));
            BUSTER_TEST(arguments, os_is_only_live_thread());
            BUSTER_TEST(arguments, liveness.worker_saw_only_live_thread == 0);
        }

        // The debugger cache publishes one tri-state after its probe. Reset it
        // before each round so every thread races the first probe.
        {
            os_debugger_state_test_reset();
            BUSTER_TEST(arguments, os_debugger_state_test_state() == 0);
            bool expected = is_debugger_present();
            BUSTER_TEST(arguments, os_debugger_state_test_state() == (expected ? 2u : 1u));
            BUSTER_TEST(arguments, is_debugger_present() == expected);
            for (u32 round = 0; round < OS_TEST_DEBUGGER_ROUNDS; round += 1)
            {
                OsTestDebuggerState debugger = {.expected = expected ? 1 : 0};
                OsThreadHandle* threads[OS_TEST_DEBUGGER_THREAD_COUNT] = {0};
                u32 started = 0;
                os_debugger_state_test_reset();
                for (u32 index = 0; index < OS_TEST_DEBUGGER_THREAD_COUNT; index += 1)
                {
                    threads[index] = os_thread_create((ThreadCreateOptions){
                        .callback = &os_test_thread_debugger,
                        .argument = &debugger,
                    });
                    BUSTER_TEST(arguments, threads[index] != 0);
                    started += threads[index] != 0;
                }
                while (atomic_u64_load(&debugger.ready) != started)
                {
                }
                atomic_u64_increment(&debugger.start);
                for (u32 index = 0; index < OS_TEST_DEBUGGER_THREAD_COUNT; index += 1)
                {
                    if (threads[index])
                    {
                        BUSTER_TEST(arguments, os_thread_join(threads[index]));
                    }
                }
                BUSTER_TEST(arguments, atomic_u64_load(&debugger.disagreements) == 0);
                BUSTER_TEST(arguments, os_debugger_state_test_state() == (expected ? 2u : 1u));
            }
        }

        u64 frame_checksum = 0;
        u64 expected_checksum = 0;
        for (u64 page = 0; page < OS_TEST_THREAD_STACK_FRAME_BYTES / OS_TEST_THREAD_STACK_PAGE_BYTES; page += 1)
        {
            expected_checksum += (u8)page;
        }
        OsThreadHandle* large_frame = os_thread_create((ThreadCreateOptions){
            .callback = &os_test_thread_large_frame,
            .argument = &frame_checksum,
        });
        BUSTER_TEST(arguments, large_frame != 0);
        if (large_frame)
        {
            BUSTER_TEST(arguments, os_thread_join(large_frame));
            BUSTER_TEST(arguments, frame_checksum == expected_checksum);
        }
#endif
    }

#if !BUSTER_SINGLE_THREADED
    // Directly exercise partial persistent and nested-gang construction plus a
    // retryable injected join. No worker, OS entity, or owner state may remain.
    BUSTER_TEST(arguments, os_resource_failure_self_test());

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    // The same failures must take an always-defined fatal path in optimized
    // builds. Deadlock is a failure too, so every child has a deadline.
    {
        String8 modes[] = {S8("barrier"), S8("thread"), S8("join")};
        String8 diagnostics[] = {S8("lane gang creation failed"), S8("lane gang creation failed"), S8("thread join failed")};
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(modes) == BUSTER_ARRAY_LENGTH(diagnostics));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        for (u64 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(modes); mode_index += 1)
        {
            SliceString8 inherited_keys = program_state->input.environment_keys;
            SliceString8 inherited_values = program_state->input.environment_values;
            String8* keys = arena_allocate(arguments->arena, String8, inherited_keys.length + 2);
            String8* values = arena_allocate(arguments->arena, String8, inherited_keys.length + 2);
            keys[0] = S8("BUSTER_OS_RESOURCE_FAILURE_MODE");
            values[0] = modes[mode_index];
            keys[1] = S8("BUSTER_TEST_JOBS");
            values[1] = S8("1");
            u64 count = 2;
            for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
            {
                if (!string_equal(inherited_keys.pointer[inherited], keys[0]) &&
                    !string_equal(inherited_keys.pointer[inherited], keys[1]))
                {
                    keys[count] = inherited_keys.pointer[inherited];
                    values[count] = inherited_values.pointer[inherited];
                    count += 1;
                }
            }

            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                                                        (SliceString8){keys, count}, (SliceString8){values, count},
                                                        (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_ERROR});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(arguments->arena, spawn, 30000000);
                String8 error = {
                    .pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer,
                    .length = wait.streams[STANDARD_STREAM_ERROR].length,
                };
                BUSTER_TEST(arguments, !wait.timed_out);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_FAILED);
                BUSTER_TEST(arguments, string_first_sequence(error, diagnostics[mode_index]) != BUSTER_STRING_NO_MATCH);
            }
        }
    }
#endif
#endif

    // lane_range must hand out contiguous shares that cover the input exactly,
    // with sizes differing by at most one item. The context is faked per lane
    // so the partition can be checked without spawning threads.
    {
        ThreadContext* thread_context = thread_context_selected();
        LaneContext saved = thread_context->lane_context;
        u64 item_counts[] = {0, 1, 7, 64, 100};
        u64 lane_counts[] = {1, 3, 8};
        bool partitions_valid = true;
        for (EACH_ARRAY_INDEX(i, item_counts))
        {
            for (EACH_ARRAY_INDEX(j, lane_counts))
            {
                u64 previous_end = 0;
                u64 minimum_length = UINT64_MAX;
                u64 maximum_length = 0;
                for (u64 lane = 0; lane < lane_counts[j]; lane += 1)
                {
                    thread_context->lane_context = (LaneContext){
                        .lane_index = lane,
                        .lane_count = lane_counts[j],
                    };
                    LaneRange range = lane_range(item_counts[i]);
                    partitions_valid = partitions_valid && range.start == previous_end && range.end >= range.start;
                    previous_end = range.end;
                    u64 length = range.end - range.start;
                    minimum_length = BUSTER_MIN(minimum_length, length);
                    maximum_length = BUSTER_MAX(maximum_length, length);
                }
                partitions_valid = partitions_valid && previous_end == item_counts[i] && maximum_length - minimum_length <= 1;
            }
        }
        thread_context->lane_context = saved;
        BUSTER_TEST(arguments, partitions_valid);
    }

    // A gang: partitioned sum, barrier visibility, broadcast from lane 0, and
    // dynamic work distribution through an atomic take-index. Single-threaded
    // builds run the same code as a one-lane gang.
    {
        Arena* arena = arguments->arena;
        u64 position = arena->position;
        // A resident gang belongs to its calling ThreadContext. Exercise its
        // complete lifetime on a temporary owner so releasing the context
        // joins the parked workers before later test modules initialize their
        // guarded read-only globals.
        ThreadContext* lane_owner_context = thread_context_allocate();
        thread_context_select(lane_owner_context);

#if BUSTER_SINGLE_THREADED
        u64 lanes = 1;
#else
        u64 lanes = BUSTER_MIN((u64)4, (u64)os_get_logical_thread_count());
        String8 jobs_text = os_get_environment_variable(S8("BUSTER_TEST_JOBS"));
        if (jobs_text.length)
        {
            IntegerParsingU64 parsed = string8_parse_u64_decimal(jobs_text);
            if (parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == jobs_text.length && parsed.value)
            {
                lanes = BUSTER_MIN(lanes, parsed.value);
            }
        }
        lanes = BUSTER_MAX(lanes, (u64)1);
#endif

        OsTestLaneState* state = arena_allocate(arena, OsTestLaneState, 1);
        memset(state, 0, sizeof(*state));
        u64 expected_sum = 0;
        for (u64 i = 0; i < OS_TEST_LANE_ITEM_COUNT; i += 1)
        {
            state->items[i] = i + 1;
            expected_sum += i + 1;
        }

        // The resident gang must not borrow storage from an enclosing scratch
        // scope: that scope is deliberately ended before the final reuse.
        TemporalArena enclosing_scratch = scratch_begin(0, 0);
        lane_run(lanes, &os_test_lane_gang, state);
#if !BUSTER_SINGLE_THREADED
        if (lanes > 1)
        {
            BUSTER_TEST(arguments, thread_context_selected()->lane_arena != 0 &&
                                       thread_context_selected()->lane_arena != enclosing_scratch.arena);
        }
#endif
        scratch_end(enclosing_scratch);

        BUSTER_TEST(arguments, state->sum == expected_sum);
        BUSTER_TEST(arguments, state->take_index >= OS_TEST_LANE_ITEM_COUNT);
        bool lanes_observed_gang = true;
        for (u64 lane = 0; lane < lanes; lane += 1)
        {
            lanes_observed_gang = lanes_observed_gang && state->observed_counts[lane] == lanes;
            lanes_observed_gang = lanes_observed_gang && state->sums_after_sync[lane] == expected_sum;
            lanes_observed_gang = lanes_observed_gang && state->broadcast_received[lane] == 0x1234567890abcdefull;
        }
        BUSTER_TEST(arguments, lanes_observed_gang);
        bool each_item_taken_once = true;
        for (u64 i = 0; i < OS_TEST_LANE_ITEM_COUNT; i += 1)
        {
            each_item_taken_once = each_item_taken_once && state->taken[i] == 1;
        }
        BUSTER_TEST(arguments, each_item_taken_once);

        // Vary the active width and return to the original width. Resident
        // worker contexts must survive both dispatches, while the active
        // barrier and every per-invocation field are refreshed.
        u64 narrower_lanes = lanes > 1 ? lanes - 1 : 1;
        memset(state->taken, 0, sizeof(state->taken));
        memset(state->observed_counts, 0, sizeof(state->observed_counts));
        memset(state->sums_after_sync, 0, sizeof(state->sums_after_sync));
        memset(state->broadcast_received, 0, sizeof(state->broadcast_received));
        state->sum = 0;
        state->take_index = 0;
        state->invocation = 1;
        lane_run(narrower_lanes, &os_test_lane_gang, state);
        bool narrower_count_valid = true;
        for (u64 lane = 0; lane < narrower_lanes; lane += 1)
        {
            narrower_count_valid = narrower_count_valid && state->observed_counts[lane] == narrower_lanes;
        }
        BUSTER_TEST(arguments, narrower_count_valid);

        memset(state->taken, 0, sizeof(state->taken));
        memset(state->observed_counts, 0, sizeof(state->observed_counts));
        memset(state->sums_after_sync, 0, sizeof(state->sums_after_sync));
        memset(state->broadcast_received, 0, sizeof(state->broadcast_received));
        state->sum = 0;
        state->take_index = 0;
        state->invocation = 2;
        lane_run(lanes, &os_test_lane_gang, state);
        bool worker_contexts_reused = true;
        bool worker_scratch_trimmed = lanes == 1 || state->scratch_committed[2][1] <= BUSTER_MB(1);
        bool repeated_results_valid = state->sum == expected_sum;
        for (u64 lane = 0; lane < lanes; lane += 1)
        {
            worker_contexts_reused = worker_contexts_reused && state->observed_contexts[0][lane] == state->observed_contexts[2][lane];
            worker_contexts_reused = worker_contexts_reused && (!lane || state->scratch_positions[0][lane] == state->scratch_positions[2][lane]);
            repeated_results_valid = repeated_results_valid && state->observed_counts[lane] == lanes &&
                                     state->sums_after_sync[lane] == expected_sum &&
                                     state->broadcast_received[lane] == 0x1234567890abcdefull;
        }
        BUSTER_TEST(arguments, worker_contexts_reused);
        BUSTER_TEST(arguments, worker_scratch_trimmed);
        BUSTER_TEST(arguments, repeated_results_valid);

        // The gang left the caller's lane context untouched.
        BUSTER_TEST(arguments, lane_index() == 0);
        BUSTER_TEST(arguments, lane_count() == 1);

        // An inner region invoked by lane 0 uses an independent gang while
        // the other outer lanes wait. Re-entering the resident gang here
        // would deadlock on its dispatch barriers.
        OsTestNestedLaneState nested = {.inner_lane_count = lanes > 1 ? 2 : 1};
        for (u64 invocation = 0; invocation < OS_TEST_NESTED_INVOCATION_COUNT; invocation += 1)
        {
            memset(nested.outer_counts, 0, sizeof(nested.outer_counts));
            memset(nested.inner_counts, 0, sizeof(nested.inner_counts));
            nested.invocation = invocation;
            lane_run(lanes, &os_test_outer_lane_gang, &nested);
            bool nested_counts_valid = true;
            for (u64 lane = 0; lane < lanes; lane += 1)
            {
                nested_counts_valid = nested_counts_valid && nested.outer_counts[lane] == lanes;
            }
            for (u64 lane = 0; lane < nested.inner_lane_count; lane += 1)
            {
                nested_counts_valid = nested_counts_valid && nested.inner_counts[lane] == nested.inner_lane_count;
            }
            BUSTER_TEST(arguments, nested_counts_valid);
#if !BUSTER_SINGLE_THREADED
            if (nested.inner_lane_count > 1)
            {
                Arena* released = nested.inner_pooled_arenas[invocation][1];
                bool still_reserved = released && os_commit(released, os_get_page_size(), (ProtectionFlags){.read = 1, .write = 1}, false);
                BUSTER_TEST(arguments, released != 0 && !still_reserved);
            }
#endif
        }

        thread_context_release(lane_owner_context);
        thread_context_select(main_context);
        arena_set_position(arena, position);
    }

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, os_test_process_group_churn);
#endif

    return result;
}
#endif
