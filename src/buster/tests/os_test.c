#include <buster/tests/os_test.h>
#include <buster/lib/time.h>


BUSTER_TEST_F_DECL UnitTestResult os_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);

    UnitTestResult result = {0};

    // Releasing the selected context must clear TLS before its arenas go
    // away. Restore the process's main context for the remainder of the suite.
    ThreadContext* main_context = thread_context_selected();
    ThreadContext* temporary_context = thread_context_allocate();
    thread_context_select(temporary_context);
    thread_context_release(temporary_context);
    BUSTER_TEST(arguments, thread_context_selected() == 0);
    thread_context_select(main_context);

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

        // BUSTER_TEST reuses the stringified expression as a format string, so
        // compound literals stay out of the assertion itself.
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
        os_directory_delete(deep);
        os_make_directory(deep);
        for (u32 depth = 0; depth < 256; depth += 1)
        {
            deep = string_format_z(arena, S8("{S8}/d"), deep);
            os_make_directory(deep);
        }
        BUSTER_TEST(arguments, file_write(string_format_z(arena, S8("{S8}/leaf.txt"), deep), BUSTER_SLICE_TO_BYTE_SLICE(S8("leaf"))));
        String8 deep_root = buster_test_temporary_path(arena, S8("buster-delete-deep"), S8(""));
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

    return result;
}
