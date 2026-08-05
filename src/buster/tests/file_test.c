#include <buster/tests/file_test.h>
#if BUSTER_INCLUDE_TESTS

UnitTestResult file_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if !BUSTER_ANDROID && !BUSTER_IOS
    String8 source_path = S8("build/buster_file_test_source.bin");
    String8 destination_path = S8("build/buster_file_test_destination.bin");
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
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, required_map.mapped_pointer != 0);
    BUSTER_TEST(arguments, required_map.bytes.pointer != 0);
#else
    BUSTER_TEST(arguments, required_map.mapped_pointer == 0);
    BUSTER_TEST(arguments, required_map.bytes.pointer == 0);
    BUSTER_TEST(arguments, arguments->arena->position == arena_position);
#endif
    file_map_unmap(required_map);

    FileMapRead fallback_map = file_map_read(arguments->arena, destination_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, fallback_map.bytes.pointer != 0);
#if BUSTER_WINDOWS
    BUSTER_TEST(arguments, fallback_map.mapped_pointer != 0);
#else
    BUSTER_TEST(arguments, fallback_map.mapped_pointer == 0);
#endif
    file_map_unmap(fallback_map);
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
