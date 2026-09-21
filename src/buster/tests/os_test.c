#include <buster/tests/os_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/file.h>
#include <buster/lib/os_internal.h>
#include <buster/lib/time.h>

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
#include <stdio.h>
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
    BUSTER_VALIDATE(pooled != 0);
    state->pooled_arena = pooled;
    BUSTER_VALIDATE(arena_destroy(pooled, 1));
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
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.create = 1, .write = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
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
BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_thread_liveness(void* argument)
{
    OsTestThreadLivenessState* state = (OsTestThreadLivenessState*)argument;
    state->worker_saw_only_live_thread = os_is_only_live_thread() ? 1 : 0;
    atomic_u64_increment(&state->started);
    while (!state->release)
    {
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
        BUSTER_VALIDATE(pooled != 0);
        state->inner_pooled_arenas[state->invocation][index] = pooled;
        BUSTER_VALIDATE(arena_destroy(pooled, 1));
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

// Private child payloads must run before compiler prewarming and unrelated test
// modules, so process deadlines measure the payload and not a nested suite.
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
            BUSTER_VALIDATE(owner != 0);
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
            BUSTER_VALIDATE(handle != 0);
            os_resource_test_fail_on_call(OS_RESOURCE_TEST_THREAD_JOIN, 0);
            BUSTER_VALIDATE(os_thread_join(handle));
        }
        os_fail_message(S8("resource failure injection was not observed"));
    }
#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 process_test_mode = os_get_environment_variable(S8("BUSTER_OS_PROCESS_TEST_MODE"));
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

UnitTestResult os_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);

    UnitTestResult result = {0};


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
            BUSTER_CHECK(symbol.length + sizeof("_suffix") <= sizeof(suffix_storage));
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
    String8 fatal_mode = os_get_environment_variable(S8("BUSTER_OS_FATAL_OUTPUT_MODE"));
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
        String8 path = string_format_z(arena, S8("{S8}/bytes"), root);
        String8 absent = string_format_z(arena, S8("{S8}/absent/child"), root);
        BUSTER_TEST(arguments, !os_make_directory_attempt(absent));
        BUSTER_TEST(arguments, !os_make_directory_attempt((String8){0}));
        u8 original[257];
        u8 copy[300];
        for (u32 i = 0; i < sizeof(original); i += 1) original[i] = (u8)i;
        u64 count = 99;
        BUSTER_TEST(arguments, os_file_read_attempt(0, (ByteSlice){0}, &count) && count == 0);
        BUSTER_TEST(arguments, os_file_write_attempt(0, (ByteSlice){0}));
        BUSTER_TEST(arguments, !os_file_read_attempt(0, (ByteSlice){copy, 1}, &count) && count == 0);
        BUSTER_TEST(arguments, !os_file_write_attempt(0, (ByteSlice){original, 1}));
        OsFileDescriptor* file = os_file_open(path, (OpenFlags){.create = 1, .write = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
        BUSTER_TEST(arguments, file != 0);
        if (file)
        {
            BUSTER_TEST(arguments, os_file_write_attempt(file, (ByteSlice){original, sizeof(original)}));
            BUSTER_TEST(arguments, !os_file_read_attempt(file, (ByteSlice){copy, 1}, &count) && count == 0);
            BUSTER_TEST(arguments, os_file_close(file));
        }
        file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
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
#if BUSTER_LINUX
        file = os_file_open(S8("/dev/full"), (OpenFlags){.write = 1}, (OpenPermissions){.read = 1, .write = 1});
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
        BUSTER_TEST(arguments, os_directory_delete(root));
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

    // Releasing the selected context must clear TLS before its arenas go
    // away. No scratch-backed operation is valid while TLS is empty, so
    // restore the process's main context immediately after observing it.
    ThreadContext* main_context = thread_context_selected();
    BUSTER_TEST(arguments, main_context != 0);
    ThreadContext* temporary_context = thread_context_allocate();
    thread_context_select(temporary_context);
    thread_context_release(temporary_context);
    bool released_context_was_cleared = thread_context_selected() == 0;
    thread_context_select(main_context);
    BUSTER_TEST(arguments, released_context_was_cleared);
    BUSTER_TEST(arguments, thread_context_selected() == main_context);

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
            os_file_open(overflow_path, (OpenFlags){.create = 1, .write = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
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
        ProcessSpawnOptions options = {.use_process_environment = 1, .new_process_group = 1};
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments),
            (SliceString8){0}, (SliceString8){0}, options);
        BUSTER_TEST(arguments, spawn.handle != 0 && spawn.process_group);
        if (spawn.handle)
        {
            ProcessWaitResult wait_result = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, wait_result.platform_status == 0);
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
    // /proc stat parsing must use the final command-name parenthesis and fail
    // closed on mismatched identities or malformed group fields.
    BUSTER_TEST(arguments, os_linux_process_stat_parse_self_test());
    // An unrelated PID disappearing between readdir and stat is ordinary
    // host churn and cannot invalidate a stable target-group proof.
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
            .use_process_environment = 1,
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

        OsFileOpenResult ready_open = os_file_open_checked(ready_sentinel, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        bool helper_ready = ready_open.file != 0;
        if (ready_open.file) { BUSTER_TEST(arguments, os_file_close(ready_open.file)); }
        BUSTER_TEST(arguments, helper_ready);

        OsFileDescriptor* release_file = os_file_open(release_sentinel, (OpenFlags){.create = 1, .write = 1, .truncate = 1},
            (OpenPermissions){.read = 1, .write = 1});
        BUSTER_TEST(arguments, release_file != 0);
        if (release_file) { BUSTER_TEST(arguments, os_file_close(release_file)); }
        poll(0, 0, 300);
        OsFileOpenResult escaped_open = os_file_open_checked(escaped_sentinel, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        bool helper_escaped = escaped_open.file != 0;
        if (escaped_open.file) { BUSTER_TEST(arguments, os_file_close(escaped_open.file)); }
        BUSTER_TEST(arguments, !helper_escaped);
        if (helper_ready) { BUSTER_TEST(arguments, os_file_delete(ready_sentinel)); }
        if (release_file) { BUSTER_TEST(arguments, os_file_delete(release_sentinel)); }
        if (helper_escaped) { BUSTER_TEST(arguments, os_file_delete(escaped_sentinel)); }
    }

    // A hot inherited writer keeps poll continuously readable. Deadline
    // enforcement is independent of readability: the owner kills the group,
    // proves every member quiescent, and drains the captured pipe to EOF.
    {
        u64 position = arguments->arena->position;
        String8 spawn_arguments[] = {
            S8("/bin/sh"),
            S8("-c"),
            S8("while :; do printf 0123456789abcdef0123456789abcdef; done"),
        };
        ProcessSpawnOptions options = {
            .capture = (u64)1 << STANDARD_STREAM_OUTPUT,
            .use_process_environment = 1,
            .new_process_group = 1,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(spawn_arguments),
            (SliceString8){0}, (SliceString8){0}, options);
        BUSTER_TEST(arguments, spawn.handle != 0 && spawn.process_group);
        if (spawn.handle)
        {
            ProcessWaitResult wait_result = os_process_wait_deadline(arguments->arena, spawn, 100000);
            BUSTER_TEST(arguments, wait_result.result == PROCESS_RESULT_FAILED);
            BUSTER_TEST(arguments, wait_result.timed_out);
            BUSTER_TEST(arguments, wait_result.streams[STANDARD_STREAM_OUTPUT].length > 0);
            BUSTER_TEST(arguments, !wait_result.process_group_reservation_retained);
            BUSTER_TEST(arguments, !wait_result.process_group_ownership_lost);
        }
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
        os_make_directory((String8){invalid, sizeof(invalid)});
        os_make_directory((String8){0});
        BUSTER_TEST(arguments, true);
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
            .use_process_environment = 1,
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
            .use_process_environment = 1,
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
            {S8("printf ok; sleep 30"), true, 100000, true, 2, false},
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
                .use_process_environment = 1,
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
        OsFileDescriptor* file = os_file_open(path, (OpenFlags){.create = true, .write = true, .truncate = true},
                                             (OpenPermissions){.read = true, .write = true, .execute = true});
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
                OsFileDescriptor* spelled = os_file_open(spelled_path, (OpenFlags){.create = true, .write = true, .truncate = true},
                                                       (OpenPermissions){.read = true, .write = true, .execute = true});
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

        OpenFlags read_flags = {.read = 1};
        OpenPermissions read_permissions = {.read = 1};
        BUSTER_TEST(arguments, os_directory_delete(root));
        OsFileDescriptor* deleted = os_file_open(root, read_flags, read_permissions);
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

        OpenFlags link_read_flags = {.read = 1};
        OpenPermissions link_read_permissions = {.read = 1};
        BUSTER_TEST(arguments, os_directory_delete(root));
        OsFileDescriptor* survivor = os_file_open(outside, link_read_flags, link_read_permissions);
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
        OsFileDescriptor* directory_survivor = os_file_open(outside_file, link_read_flags, link_read_permissions);
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

            OsFileDescriptor* survivor = os_file_open(outside_file, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
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
                BUSTER_TEST(arguments, string_first_sequence(error, S8("validation failed")) != BUSTER_STRING_NO_MATCH);
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

    return result;
}
#endif
