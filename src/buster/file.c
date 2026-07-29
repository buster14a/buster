#include <buster/file.h>
#include <buster/system_headers.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>

#if BUSTER_ANDROID
#include <android/asset_manager.h>
AAssetManager* buster_android_asset_manager = 0;
String8 buster_android_internal_data_path = {0};
#endif

#if BUSTER_IOS
#include <objc/runtime.h>
#include <objc/message.h>
// The iOS app is sandboxed; test data and other assets are bundled under the
// app's Resources directory, so relative paths must be resolved against it.
BUSTER_GLOBAL_LOCAL const char* buster_ios_bundle_resource_path(void)
{
    id bundle = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSBundle"), sel_registerName("mainBundle"));
    if (!bundle)
    {
        return 0;
    }
    id path = ((id (*)(id, SEL))objc_msgSend)(bundle, sel_registerName("resourcePath"));
    if (!path)
    {
        return 0;
    }
    return ((const char* (*)(id, SEL))objc_msgSend)(path, sel_registerName("UTF8String"));
}
#endif

bool file_write(String8 path, ByteSlice content)
{
    OsFileDescriptor* fd = os_file_open(path, (OpenFlags) { .write = 1, .create = 1, .truncate = 1 }, (OpenPermissions){ .read = 1, .write = 1 });
    bool result = false;

    result = fd != 0;
    if (result)
    {
        os_file_write(fd, content);
        os_file_close(fd);
    }

    return result;
}

ByteSlice file_read(Arena* arena, String8 path, FileReadOptions options)
{
    ByteSlice result = {0};

    if (!options.start_alignment)
    {
        options.start_alignment = 1;
    }

    if (!options.end_alignment)
    {
        options.end_alignment = 1;
    }

#if BUSTER_ANDROID
    // The app has no test files on disk; relative paths resolve to APK assets.
    if (buster_android_asset_manager && path.length && path.pointer[0] != '/')
    {
        char* asset_path = (char*)arena_allocate_bytes(arena, path.length + 1, 1);
        memcpy(asset_path, path.pointer, path.length);
        asset_path[path.length] = 0;

        AAsset* asset = AAssetManager_open(buster_android_asset_manager, asset_path, AASSET_MODE_BUFFER);
        if (asset)
        {
            u64 file_size = (u64)AAsset_getLength64(asset);
            u64 allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
            allocation_size = BUSTER_MAX(allocation_size, 1);
            u64 allocation_bottom = allocation_size - (file_size + options.start_padding);
            u64 allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
            u8* file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
            if (file_size)
            {
                const void* asset_buffer = AAsset_getBuffer(asset);
                memcpy(file_buffer + options.start_padding, asset_buffer, file_size);
            }
            memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
            AAsset_close(asset);
            return (ByteSlice) { file_buffer + options.start_padding, file_size };
        }
    }
#endif

#if BUSTER_IOS
    // Resolve relative paths against the app bundle's Resources directory.
    if (path.length && path.pointer[0] != '/')
    {
        const char* resource_path = buster_ios_bundle_resource_path();
        if (resource_path)
        {
            String8 resource = string_from_pointer((char8*)resource_path);
            u64 total = resource.length + 1 + path.length;
            char* buffer = (char*)arena_allocate_bytes(arena, total + 1, 1);
            memcpy(buffer, resource.pointer, resource.length);
            buffer[resource.length] = '/';
            memcpy(buffer + resource.length + 1, path.pointer, path.length);
            buffer[total] = 0;
            path = (String8){ (char8*)buffer, total };
        }
    }
#endif

    OsFileDescriptor* fd = os_file_open(path, (OpenFlags) { .read = 1 }, (OpenPermissions){ .read = 1 });

    if (fd)
    {
        u64 file_size = os_file_get_size(fd);
        u64 allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
        allocation_size = BUSTER_MAX(allocation_size, 1);
        u64 allocation_bottom = allocation_size - (file_size + options.start_padding);
        u64 allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
        u8* file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
        if (file_size)
        {
            file_size = os_file_read(fd, (ByteSlice) { file_buffer + options.start_padding, file_size }, file_size);
        }
        memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
        os_file_close(fd);
        result = (ByteSlice) { file_buffer + options.start_padding, file_size };
    }

    return result;
}

bool file_copy(CopyFileArguments arguments)
{
    bool result = false;
    if (string_equal(arguments.original_path, arguments.new_path))
    {
        return result;
    }
    OsFileDescriptor* source = os_file_open(arguments.original_path, (OpenFlags){ .read = 1 }, (OpenPermissions){ .read = 1 });
    if (source)
    {
        OsFileDescriptor* destination = os_file_open(arguments.new_path, (OpenFlags){ .write = 1, .create = 1, .truncate = 1 }, (OpenPermissions){ .read = 1, .write = 1 });
        if (destination)
        {
            result = true;
            u64 remaining = os_file_get_size(source);
            u8 buffer[BUSTER_KB(64)];
            while (remaining)
            {
                u64 requested = BUSTER_MIN(remaining, sizeof(buffer));
                u64 read_count = os_file_read(source, (ByteSlice){ buffer, sizeof(buffer) }, requested);
                if (read_count != requested)
                {
                    result = false;
                    break;
                }
                os_file_write(destination, (ByteSlice){ buffer, read_count });
                remaining -= read_count;
            }
            result = os_file_close(destination) && result;
        }
        result = os_file_close(source) && result;
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
UnitTestResult file_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if !BUSTER_ANDROID && !BUSTER_IOS
    String8 source_path = S8("build/buster_file_test_source.bin");
    String8 destination_path = S8("build/buster_file_test_destination.bin");
    String8 content = S8("buster file copy test");

    BUSTER_TEST(arguments, file_write(source_path, (ByteSlice){ (u8*)content.pointer, content.length }));
    String8 stale_content = S8("stale destination");
    BUSTER_TEST(arguments, file_write(destination_path, (ByteSlice){ (u8*)stale_content.pointer, stale_content.length }));
    BUSTER_TEST(arguments, file_copy((CopyFileArguments){
        .original_path = source_path,
        .new_path = destination_path,
    }));

    u64 arena_position = arguments->arena->position;
    ByteSlice copied_bytes = file_read(arguments->arena, destination_path, (FileReadOptions){0});
    String8 copied = { (char8*)copied_bytes.pointer, copied_bytes.length };
    BUSTER_STRING_TEST(arguments, copied, content);
    arguments->arena->position = arena_position;

    BUSTER_TEST(arguments, file_write(source_path, (ByteSlice){0}));
    ByteSlice empty = file_read(arguments->arena, source_path, (FileReadOptions){
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
