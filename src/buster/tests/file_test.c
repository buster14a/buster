#include <buster/tests/file_test.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/os_internal.h>
#if BUSTER_INCLUDE_TESTS

// Use distinct native-error values to prove cleanup does not replace the
// original transfer failure. The seam closes real handles even on failure.
BUSTER_GLOBAL_LOCAL UnitTestResult file_test_write_failures(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 path = buster_test_temporary_path(arguments->arena, S8("file-write-fault"), S8(".bin"));
    String8 other = buster_test_temporary_path(arguments->arena, S8("file-write-unaffected"), S8(".bin"));
    u64 length = BUSTER_KB(128) + 1;
    u8* data = arena_allocate(arguments->arena, u8, length);
    for (u64 index = 0; index < length; index += 1) data[index] = (u8)(index * 37 + index / 251);
    ByteSlice content = {data, length};
    OsFileTestStep scripts[][4] = {
        {{OS_FILE_TEST_OPEN, OS_FILE_TEST_ERROR, 12345}},
        {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}},
        {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ZERO, 0}},
        {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}},
        {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ZERO, 0}},
        {{OS_FILE_TEST_WRITE, OS_FILE_TEST_INTERRUPT, 0}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100},
         {OS_FILE_TEST_WRITE, OS_FILE_TEST_INTERRUPT, 0}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 3}},
        {{OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
        {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345},
         {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
    };
    u32 counts[] = {1, 1, 1, 2, 2, 4, 1, 3};
    u64 transferred[] = {0, 0, 0, 100, 100, length, length, 100};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(scripts); index += 1)
    {
        os_file_delete(path);
        os_file_test_begin(path, scripts[index], counts[index]);
        BUSTER_TEST(arguments, file_write(other, content));
        OsFileTransferResult written = file_write_checked(path, content, (OpenPermissions){.read = 1, .write = 1});
        BUSTER_TEST(arguments, os_file_test_end() == counts[index]);
        BUSTER_TEST(arguments, written.transferred == transferred[index]);
        BUSTER_TEST(arguments, (written.error.v == 0) == (index == 5));
        if (index == 0 || index == 1 || index == 3 || index == 7) BUSTER_TEST(arguments, written.error.v == 12345);
        if (index == 6) BUSTER_TEST(arguments, written.error.v == 23456);
        ByteSlice actual = file_read(arguments->arena, path, (FileReadOptions){0});
        BUSTER_TEST(arguments, actual.length == transferred[index]);
        if (actual.length) BUSTER_TEST(arguments, memory_compare(actual.pointer, data, actual.length));
        // The existing boolean entry must propagate the same outcome.
        os_file_test_begin(path, scripts[index], counts[index]);
        bool success = file_write(path, content);
        BUSTER_TEST(arguments, os_file_test_end() == counts[index]);
        BUSTER_TEST(arguments, success == (index == 5));
    }
    BUSTER_TEST(arguments, file_write(path, (ByteSlice){0}));
    OsFileTestStep close_step = {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456};
    os_file_test_begin(path, &close_step, 1);
    OsFileTransferResult empty = file_write_checked(path, (ByteSlice){0}, (OpenPermissions){.write = 1});
    BUSTER_TEST(arguments, os_file_test_end() == 1);
    BUSTER_TEST(arguments, empty.transferred == 0 && empty.error.v == 23456);

    OsFileTestStep flush_step = {OS_FILE_TEST_FLUSH, OS_FILE_TEST_ERROR, 12345};
    os_file_test_begin(path, &flush_step, 1);
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.write = 1}, (OpenPermissions){.read = 1, .write = 1});
    BUSTER_TEST(arguments, file != 0);
    if (file)
    {
        BUSTER_TEST(arguments, os_file_flush(file).v == 12345);
        BUSTER_TEST(arguments, !os_file_flush(file).v);
        BUSTER_TEST(arguments, os_file_close(file));
    }
    BUSTER_TEST(arguments, os_file_test_end() == 1);
    BUSTER_TEST(arguments, os_file_flush(0).v != 0 && os_file_close_checked(0).v != 0);
    BUSTER_TEST(arguments, os_file_write_checked(0, (ByteSlice){0}).error.v == 0);
    BUSTER_TEST(arguments, os_file_write_checked(0, content).error.v != 0);
    BUSTER_TEST(arguments, os_file_open_checked((String8){0}, (OpenFlags){.read = 1}, (OpenPermissions){0}).error.v != 0);

    // file_copy must include completion of the destination in its boolean.
    BUSTER_TEST(arguments, file_write(other, content));
    os_file_test_begin(path, &close_step, 1);
    BUSTER_TEST(arguments, !file_copy((CopyFileArguments){.original_path = other, .new_path = path}));
    BUSTER_TEST(arguments, os_file_test_end() == 1);
    BUSTER_TEST(arguments, os_file_delete(path));
    BUSTER_TEST(arguments, os_file_delete(other));
    return result;
}

UnitTestResult file_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, file_test_write_failures);
#if !BUSTER_ANDROID && !BUSTER_IOS
    String8 source_path = buster_test_temporary_path(arguments->arena, S8("file-test-source"), S8(".bin"));
    String8 destination_path = buster_test_temporary_path(arguments->arena, S8("file-test-destination"), S8(".bin"));
    String8 content = S8("buster file copy test");

    BUSTER_TEST(arguments, file_write(source_path, (ByteSlice){(u8*)content.pointer, content.length}));
    String8 stale_content = S8("stale destination");
    BUSTER_TEST(arguments, file_write(destination_path, (ByteSlice){(u8*)stale_content.pointer, stale_content.length}));
    BUSTER_TEST(arguments, file_copy((CopyFileArguments){
                               .original_path = source_path,
                               .new_path = destination_path,
                           }));

    u64 arena_position = arguments->arena->position;
    ByteSlice copied_bytes = file_read(arguments->arena, destination_path, (FileReadOptions){0});
    String8 copied = {(char8*)copied_bytes.pointer, copied_bytes.length};
    BUSTER_STRING_TEST(arguments, copied, content);
    arena_set_position(arguments->arena, arena_position);

    FileMapRead required_map = file_map_read(arguments->arena, destination_path, (FileReadOptions){.map_required = 1});
#if BUSTER_WINDOWS || BUSTER_LINUX || BUSTER_MACOS
    BUSTER_TEST(arguments, required_map.mapped_pointer != 0);
    BUSTER_TEST(arguments, required_map.bytes.pointer != 0);
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, arguments->arena->position == arena_position);
#elif BUSTER_LINUX || BUSTER_MACOS
    BUSTER_TEST(arguments, arguments->arena->position > arena_position);
#else
    BUSTER_TEST(arguments, arguments->arena->position == arena_position);
#endif
#else
    BUSTER_TEST(arguments, required_map.mapped_pointer == 0);
    BUSTER_TEST(arguments, required_map.bytes.pointer == 0);
    BUSTER_TEST(arguments, arguments->arena->position == arena_position);
#endif
    file_map_unmap(required_map);

    FileMapRead fallback_map = file_map_read(arguments->arena, destination_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, fallback_map.bytes.pointer != 0);
#if BUSTER_WINDOWS || BUSTER_LINUX || BUSTER_MACOS
    BUSTER_TEST(arguments, fallback_map.mapped_pointer != 0);
#else
    BUSTER_TEST(arguments, fallback_map.mapped_pointer == 0);
#endif
    file_map_unmap(fallback_map);
    arena_set_position(arguments->arena, arena_position);

    // Exercise relative paths without changing the process-wide working directory:
    // like the compiler fixtures, these paths are relative to the checkout root.
    String8 relative_paths[] = {S8("tests/file_map_read.txt"), S8("./tests/file_map_read.txt")};
    String8 mapped_content = S8("buster file mapping test");
    for (u64 path_index = 0; path_index < BUSTER_ARRAY_LENGTH(relative_paths); path_index += 1)
    {
        for (u32 map_required = 0; map_required < 2; map_required += 1)
        {
            FileMapRead relative_map = file_map_read(arguments->arena, relative_paths[path_index], (FileReadOptions){.map_required = map_required});
            bool mapped = relative_map.mapped_pointer != 0 && relative_map.bytes.pointer != 0;
            BUSTER_TEST(arguments, mapped);
            if (mapped)
            {
                BUSTER_STRING_TEST(arguments, ((String8){(char8*)relative_map.bytes.pointer, relative_map.bytes.length}), mapped_content);
            }
            file_map_unmap(relative_map);
            arena_set_position(arguments->arena, arena_position);
        }
    }

    FileMapRead padded_required = file_map_read(arguments->arena, relative_paths[0], (FileReadOptions){.end_padding = 4, .map_required = 1});
    BUSTER_TEST(arguments, padded_required.bytes.pointer == 0 && padded_required.mapped_pointer == 0);
    file_map_unmap(padded_required);
    FileMapRead padded_fallback = file_map_read(arguments->arena, relative_paths[0], (FileReadOptions){.end_padding = 4});
    BUSTER_TEST(arguments, padded_fallback.bytes.pointer != 0 && padded_fallback.mapped_pointer == 0);
    if (padded_fallback.bytes.pointer)
    {
        BUSTER_STRING_TEST(arguments, ((String8){(char8*)padded_fallback.bytes.pointer, padded_fallback.bytes.length}), mapped_content);
        bool padding_zero = true;
        for (u64 padding_index = 0; padding_index < 4; padding_index += 1)
        {
            padding_zero &= padded_fallback.bytes.pointer[padded_fallback.bytes.length + padding_index] == 0;
        }
        BUSTER_TEST(arguments, padding_zero);
    }
    file_map_unmap(padded_fallback);
    arena_set_position(arguments->arena, arena_position);

    BUSTER_TEST(arguments, file_write(source_path, (ByteSlice){0}));
    ByteSlice empty = file_read(arguments->arena, source_path,
                                (FileReadOptions){
                                    .start_alignment = 4,
                                    .end_padding = 4,
                                });
    BUSTER_TEST(arguments, empty.pointer != 0);
    BUSTER_TEST(arguments, empty.length == 0);
    BUSTER_TEST(arguments, ((u64)empty.pointer & 3) == 0);
    bool padding_is_zero = false;
    if (empty.pointer)
    {
        padding_is_zero = empty.pointer[0] == 0 && empty.pointer[1] == 0 && empty.pointer[2] == 0 && empty.pointer[3] == 0;
    }
    BUSTER_TEST(arguments, padding_is_zero);
    arena_set_position(arguments->arena, arena_position);

#if BUSTER_LINUX
    // procfs exposes a pipe descriptor with st_size == 0. The read must consume
    // the descriptor instead of treating the reported size as EOF.
    {
        int descriptors[2] = {-1, -1};
        BUSTER_TEST(arguments, pipe(descriptors) == 0);
        if (descriptors[0] >= 0 && descriptors[1] >= 0)
        {
            String8 pipe_content = S8("int streamed_symbol(void){return 73;}\n");
            ssize_t written = write(descriptors[1], pipe_content.pointer, (size_t)pipe_content.length);
            BUSTER_TEST(arguments, written == (ssize_t)pipe_content.length);
            BUSTER_TEST(arguments, close(descriptors[1]) == 0);
            descriptors[1] = -1;
            String8 descriptor_path = string_format_z(arguments->arena, S8("/proc/self/fd/{s32}"), descriptors[0]);
            ByteSlice streamed = file_read(arguments->arena, descriptor_path, (FileReadOptions){.end_padding = 4});
            BUSTER_STRING_TEST(arguments, ((String8){.pointer = (char8*)streamed.pointer, .length = streamed.length}), pipe_content);
            BUSTER_TEST(arguments, streamed.pointer && streamed.pointer[streamed.length] == 0 && streamed.pointer[streamed.length + 1] == 0 &&
                                       streamed.pointer[streamed.length + 2] == 0 && streamed.pointer[streamed.length + 3] == 0);
            BUSTER_TEST(arguments, close(descriptors[0]) == 0);
            arena_set_position(arguments->arena, arena_position);
        }
    }
#endif

#if BUSTER_LINUX || BUSTER_MACOS
    // A FIFO exercises geometric growth beyond the first 64 KiB and the final
    // shrink back to the exact padded allocation.
    {
        String8 fifo_path = buster_test_temporary_path(arguments->arena, S8("file-stream"), S8(".fifo"));
        os_file_delete(fifo_path);
        BUSTER_TEST(arguments, mkfifo((char*)fifo_path.pointer, 0600) == 0);
        String8 writer_arguments[] = {
            S8("/bin/sh"),
            S8("-c"),
            S8("head -c 131073 /dev/zero | tr '\\0' x > \"$1\""),
            S8("file-stream-writer"),
            fifo_path,
        };
        ProcessSpawnResult writer = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(writer_arguments), (SliceString8){0}, (SliceString8){0},
                                                       (ProcessSpawnOptions){.use_process_environment = 1});
        BUSTER_TEST(arguments, writer.handle != 0);
        if (writer.handle)
        {
            ByteSlice streamed = file_read(arguments->arena, fifo_path,
                                           (FileReadOptions){
                                               .start_padding = 3,
                                               .start_alignment = 16,
                                               .end_padding = 5,
                                               .end_alignment = 16,
                                           });
            ProcessWaitResult writer_wait = os_process_wait_deadline(arguments->arena, writer, 30000000);
            BUSTER_TEST(arguments, !writer_wait.timed_out && writer_wait.result == PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, streamed.pointer != 0 && streamed.length == 131073);
            if (streamed.pointer && streamed.length == 131073)
            {
                BUSTER_TEST(arguments, (((u64)streamed.pointer - 3) & 15) == 0);
                BUSTER_TEST(arguments, streamed.pointer[0] == 'x' && streamed.pointer[65536] == 'x' && streamed.pointer[streamed.length - 1] == 'x');
                bool end_zero = true;
                for (u32 padding_index = 0; padding_index < 5; padding_index += 1)
                {
                    end_zero &= streamed.pointer[streamed.length + padding_index] == 0;
                }
                BUSTER_TEST(arguments, end_zero);
            }
        }
        BUSTER_TEST(arguments, os_file_delete(fifo_path));
        arena_set_position(arguments->arena, arena_position);
    }

    // The user-facing regression: Bash process substitution names a pipe under
    // /dev/fd. Compile and run a main function so an empty translation unit
    // cannot pass by merely producing a valid but symbol-free object.
    {
        String8 executable_path = buster_test_temporary_path(arguments->arena, S8("file-process-substitution"), S8(""));
        String8 compile_arguments[] = {
            S8("/bin/bash"),
            S8("-c"),
            S8("\"$1\" cc -x c -o \"$2\" <(printf 'int main(void){return 0;}\\n')"),
            S8("file-process-substitution"),
            program_state->input.arguments.pointer[0],
            executable_path,
        };
        ProcessSpawnResult compile = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(compile_arguments), (SliceString8){0}, (SliceString8){0},
                                                        (ProcessSpawnOptions){
                                                            .capture = (u64)1 << STANDARD_STREAM_ERROR,
                                                            .use_process_environment = 1,
                                                        });
        BUSTER_TEST(arguments, compile.handle != 0);
        if (compile.handle)
        {
            ProcessWaitResult compile_wait = os_process_wait_deadline(arguments->arena, compile, 30000000);
            BUSTER_TEST(arguments, !compile_wait.timed_out && compile_wait.result == PROCESS_RESULT_SUCCESS);
            if (!compile_wait.timed_out && compile_wait.result == PROCESS_RESULT_SUCCESS)
            {
                String8 run_arguments[] = {executable_path};
                ProcessSpawnResult run = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                           (ProcessSpawnOptions){.use_process_environment = 1});
                BUSTER_TEST(arguments, run.handle != 0);
                if (run.handle)
                {
                    ProcessWaitResult run_wait = os_process_wait_deadline(arguments->arena, run, 30000000);
                    BUSTER_TEST(arguments, !run_wait.timed_out && run_wait.result == PROCESS_RESULT_SUCCESS);
                }
            }
        }
        BUSTER_TEST(arguments, os_file_delete(executable_path));
        arena_set_position(arguments->arena, arena_position);
    }
#endif
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
#endif
