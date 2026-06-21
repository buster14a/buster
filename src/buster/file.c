#include <buster/file.h>
#include <buster/system_headers.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>

#if BUSTER_ANDROID
#include <android/asset_manager.h>
AAssetManager* buster_android_asset_manager = 0;
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
            u8* file_buffer = (u8*)&file_read;
            if (file_size)
            {
                const void* asset_buffer = AAsset_getBuffer(asset);
                u64 allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
                u64 allocation_bottom = allocation_size - (file_size + options.start_padding);
                u64 allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
                file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
                memcpy(file_buffer + options.start_padding, asset_buffer, file_size);
                memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
            }
            AAsset_close(asset);
            return (ByteSlice) { file_buffer + options.start_padding, file_size };
        }
    }
#endif

    OsFileDescriptor* fd = os_file_open(path, (OpenFlags) { .read = 1 }, (OpenPermissions){ .read = 1 });

    if (fd)
    {
        u64 file_size = os_file_get_size(fd);
        u8* file_buffer = (u8*)&file_read;
        if (file_size)
        {
            u64 allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
            u64 allocation_bottom = allocation_size - (file_size + options.start_padding);
            u64 allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
            file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
            file_size = os_file_read(fd, (ByteSlice) { file_buffer + options.start_padding, file_size }, file_size);
            memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
        }
        os_file_close(fd);
        result = (ByteSlice) { file_buffer + options.start_padding, file_size };
    }

    return result;
}

bool file_copy(CopyFileArguments arguments)
{
    bool result = true;
#if defined(_WIN32)
    TemporalArena temp = scratch_begin(0, 0);
    String16 original_path = string16_from_string8(temp.arena, arguments.original_path, true);
    String16 new_path = string16_from_string8(temp.arena, arguments.new_path, true);
    result = CopyFileW(original_path.pointer, new_path.pointer, false) != 0;
    if (!result)
    {
        // If the copy failed (e.g., file is locked by another process), check if the destination already exists.
        // This handles the case where a DLL is already loaded by a running process - we can just use it.
        DWORD attributes = GetFileAttributesW(new_path.pointer);
        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            // Destination exists, assume it's correct
            result = true;
        }
        else
        {
            string_print(S8("Error message: {EOs}. Original: {S8}. New: {S8}\n"), os_get_last_error(), arguments.original_path, arguments.new_path);
            os_fail();
        }
    }
    scratch_end(temp);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

