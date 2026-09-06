#include <buster/tests/file_test.h>
#if BUSTER_INCLUDE_TESTS

UnitTestResult file_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
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
    arguments->arena->position = arena_position;

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
    arguments->arena->position = arena_position;

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
            arguments->arena->position = arena_position;
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
    arguments->arena->position = arena_position;

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
    arguments->arena->position = arena_position;
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
#endif
