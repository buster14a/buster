#include <buster/tests/os_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/file.h>
#include <buster/lib/time.h>

enum
{
    OS_TEST_LANE_MAX_COUNT = 8,
    OS_TEST_LANE_ITEM_COUNT = 1003,
};

typedef struct OsTestLaneState OsTestLaneState;
struct OsTestLaneState
{
    u64 items[OS_TEST_LANE_ITEM_COUNT];
    u8 taken[OS_TEST_LANE_ITEM_COUNT];
    u64 observed_counts[OS_TEST_LANE_MAX_COUNT];
    u64 sums_after_sync[OS_TEST_LANE_MAX_COUNT];
    u64 broadcast_received[OS_TEST_LANE_MAX_COUNT];
    AtomicU64 sum;
    AtomicU64 take_index;
};

// Runs once on every lane. Test macros are not thread-safe, so lanes only
// record what they observe; the caller checks after the gang has joined.
BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_lane_gang(void* argument)
{
    OsTestLaneState* state = (OsTestLaneState*)argument;
    u64 index = lane_index();
    state->observed_counts[index] = lane_count();

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

UnitTestResult os_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);

    UnitTestResult result = {0};

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

#if BUSTER_LINUX || BUSTER_MACOS
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

        arena->position = position;
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

        arena->position = position;
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

        arena->position = position;
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

            *path_value = saved_path;
        }

        arena->position = position;
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

        arena->position = position;
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

        arena->position = position;
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

#if BUSTER_SINGLE_THREADED
        u64 lanes = 1;
#else
        u64 lanes = BUSTER_MIN((u64)4, (u64)os_get_logical_thread_count());
        String8 jobs_text = os_get_environment_variable(S8("BUSTER_TEST_JOBS"));
        if (jobs_text.length)
        {
            IntegerParsingU64 parsed = string8_parse_u64_decimal(jobs_text.pointer);
            if (parsed.length == jobs_text.length && parsed.value)
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

        lane_run(lanes, &os_test_lane_gang, state);

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

        // The gang left the caller's lane context untouched.
        BUSTER_TEST(arguments, lane_index() == 0);
        BUSTER_TEST(arguments, lane_count() == 1);

        arena->position = position;
    }

    return result;
}
#endif
