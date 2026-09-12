// File content ownership and transfer policy: file_write_checked preserves
// transfer/close failures; file_read owns padded arena reads; file_map_read and
// file_map_unmap own optional mappings; file_copy streams between descriptors.
#include <buster/lib/file.h>
#include <buster/lib/os_internal.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/integer.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>

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
    const char* result = 0;
    id bundle = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSBundle"), sel_registerName("mainBundle"));
    id path = bundle ? ((id (*)(id, SEL))objc_msgSend)(bundle, sel_registerName("resourcePath")) : 0;
    if (path)
    {
        result = ((const char* (*)(id, SEL))objc_msgSend)(path, sel_registerName("UTF8String"));
    }

    return result;
}
#endif

OsFileTransferResult file_write_checked(String8 path, ByteSlice content, OpenPermissions permissions)
{
    OsFileOpenResult opened = os_file_open_checked(path, (OpenFlags){.write = 1, .create = 1, .truncate = 1}, permissions);
    OsFileTransferResult result = {.error = opened.error};
    if (opened.file)
    {
        result = os_file_write_checked(opened.file, content);
        OsError close_error = os_file_close_checked(opened.file);
        if (!result.error.v) result.error = close_error;
    }
    return result;
}

bool file_write(String8 path, ByteSlice content)
{
    return !file_write_checked(path, content, (OpenPermissions){.read = 1, .write = 1}).error.v;
}

FileMapRead file_map_read(Arena* arena, String8 path, FileReadOptions options)
{
    FileMapRead result = {0};

#if BUSTER_ANDROID || BUSTER_IOS
    // Neither platform offers a mapping backend here, so the read path is the
    // only way to satisfy a caller that did not demand a mapping.
    if (!options.map_required)
    {
        result.bytes = file_read(arena, path, options);
    }
#else
    // Padding and alignment requests cannot be served by a raw mapping.
    bool mapping_unavailable = false;
#if BUSTER_INCLUDE_TESTS
    mapping_unavailable = os_file_test_map_unavailable(path);
#endif
    if (mapping_unavailable || !path.length || options.start_padding || options.end_padding || options.start_alignment || options.end_alignment)
    {
        if (!options.map_required)
        {
            result.bytes = file_read(arena, path, options);
        }
    }
    else
    {
#if BUSTER_WINDOWS
    {
        OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        if (file)
        {
            u64 file_size = os_file_get_size(file);
            if (file_size && file_size != UINT64_MAX)
            {
                HANDLE mapping = CreateFileMappingW((HANDLE)file, 0, PAGE_READONLY, (DWORD)(file_size >> 32), (DWORD)file_size, 0);
                if (mapping)
                {
                    void* mapped = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
                    if (mapped)
                    {
                        result.bytes = (ByteSlice){(u8*)mapped, file_size};
                        result.mapped_pointer = mapped;
                        result.mapped_size = file_size;
                        result.mapped_handle = mapping;
                    }
                    else
                    {
                        CloseHandle(mapping);
                    }
                }
            }
            os_file_close(file);
        }
    }
#elif BUSTER_LINUX || BUSTER_MACOS
    {
        char* path_buffer = (char*)arena_allocate_bytes(arena, path.length + 1, 1);
        if (path_buffer)
        {
            memcpy(path_buffer, path.pointer, path.length);
            path_buffer[path.length] = 0;

            int file_descriptor = open(path_buffer, O_RDONLY, 0);
            if (file_descriptor >= 0)
            {
                struct stat file_stats = {0};
                if (fstat(file_descriptor, &file_stats) == 0 && file_stats.st_size > 0)
                {
                    void* mapped = mmap(0, (u64)file_stats.st_size, PROT_READ, MAP_PRIVATE, file_descriptor, 0);
                    if (mapped != MAP_FAILED)
                    {
                        result.bytes = (ByteSlice){(u8*)mapped, (u64)file_stats.st_size};
                        result.mapped_pointer = mapped;
                        result.mapped_size = (u64)file_stats.st_size;
                    }
                }
                close(file_descriptor);
            }
        }
    }
#endif

        if (!result.bytes.pointer && !options.map_required)
        {
            result.bytes = file_read(arena, path, options);
        }
    }
#endif

    return result;
}

void file_map_unmap(FileMapRead map)
{
#if BUSTER_WINDOWS
    if (map.mapped_pointer)
    {
        UnmapViewOfFile(map.mapped_pointer);
    }
    if (map.mapped_handle)
    {
        CloseHandle(map.mapped_handle);
    }
#elif BUSTER_ANDROID || BUSTER_IOS
    BUSTER_UNUSED(map);
#else
    if (map.mapped_pointer && map.mapped_size)
    {
        munmap(map.mapped_pointer, map.mapped_size);
    }
#endif
}

FileReadResult file_read_checked(Arena* arena, String8 path, FileReadOptions options)
{
    FileReadResult result = {0};
    u64 read_mark = arena->position;

    if (!options.start_alignment)
    {
        options.start_alignment = 1;
    }

    if (!options.end_alignment)
    {
        options.end_alignment = 1;
    }

#if BUSTER_ANDROID
    // An APK asset satisfies the read outright, so the descriptor path below is
    // skipped rather than returned around.
    bool asset_resolved = false;
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
                if (asset_buffer) memcpy(file_buffer + options.start_padding, asset_buffer, file_size);
                else result.status = OS_FILE_READ_ERROR;
            }
            if (result.status == OS_FILE_READ_OK)
            {
                memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
                result.bytes = (ByteSlice){file_buffer + options.start_padding, file_size};
            }
            AAsset_close(asset);
            asset_resolved = true;
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
            path = (String8){(char8*)buffer, total};
        }
    }
#endif

#if BUSTER_ANDROID
    if (!asset_resolved)
#endif
    {
        OsFileOpenResult opened = os_file_open_checked(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        result.error = opened.error;
        if (opened.file)
        {
            FileStats stats = os_file_get_stats(opened.file, (FileStatsOptions){.size = 1});
            result.error = stats.error;
            if (stats.valid)
            {
                u64 reported_size = stats.size;
                u64 allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
                u64 file_size;
                u64 allocation_size;
                u8* file_buffer;
                if (reported_size)
                {
                    allocation_size = align_forward(reported_size + options.start_padding + options.end_padding, options.end_alignment);
                    allocation_size = BUSTER_MAX(allocation_size, 1);
                    file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
                    OsFileReadResult read = os_file_read_exact(opened.file, (ByteSlice){file_buffer + options.start_padding, reported_size});
                    file_size = read.transferred;
                    result.status = read.status;
                    result.error = read.error;
                }
                else
                {
                    // Size-zero descriptors include procfs, pipes and empty
                    // files. Read to EOF with bounded geometric allocations.
                    u64 allocation_mark = arena->position;
                    u64 capacity = 1;
                    allocation_size = align_forward(capacity + options.start_padding + options.end_padding, options.end_alignment);
                    allocation_size = BUSTER_MAX(allocation_size, 1);
                    file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
                    file_size = 0;
                    bool ended = false;
                    while (!ended)
                    {
                        u64 available = capacity - file_size;
                        OsFileReadResult read = os_file_read_exact(opened.file, (ByteSlice){file_buffer + options.start_padding + file_size, available});
                        file_size += read.transferred;
                        result.error = read.error;
                        ended = read.status != OS_FILE_READ_OK;
                        if (read.status == OS_FILE_READ_ERROR) result.status = read.status;
                        if (!ended)
                        {
                            u64 next_capacity = capacity < BUSTER_KB(64) ? BUSTER_KB(64) : capacity * 2;
                            if (next_capacity <= capacity) arena_allocation_overflow();
                            capacity = next_capacity;
                            arena_set_position(arena, allocation_mark);
                            allocation_size = align_forward(capacity + options.start_padding + options.end_padding, options.end_alignment);
                            allocation_size = BUSTER_MAX(allocation_size, 1);
                            u8* grown_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
                            BUSTER_CHECK(grown_buffer == file_buffer);
                            file_buffer = grown_buffer;
                        }
                    }
                    allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
                    allocation_size = BUSTER_MAX(allocation_size, 1);
                    u64 allocation_offset = (u64)(file_buffer - (u8*)arena);
                    arena_set_position(arena, allocation_offset + allocation_size);
                }
                if (result.status == OS_FILE_READ_OK)
                {
                    u64 allocation_bottom = allocation_size - (file_size + options.start_padding);
                    memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
                    result.bytes = (ByteSlice){file_buffer + options.start_padding, file_size};
                }
            }
            OsError close_error = os_file_close_checked(opened.file);
            if (result.status == OS_FILE_READ_OK && !result.error.v) result.error = close_error;
        }
    }
    if (result.error.v) result.status = OS_FILE_READ_ERROR;
    if (result.status != OS_FILE_READ_OK)
    {
        result.bytes = (ByteSlice){0};
        arena_set_position(arena, read_mark);
    }
    return result;
}

ByteSlice file_read(Arena* arena, String8 path, FileReadOptions options)
{
    return file_read_checked(arena, path, options).bytes;
}

bool file_copy(CopyFileArguments arguments)
{
    bool result = false;
    // Copying a path onto itself would truncate the source before reading it.
    if (!string_equal(arguments.original_path, arguments.new_path))
    {
        OsFileDescriptor* source = os_file_open(arguments.original_path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        if (source)
        {
            OsFileDescriptor* destination =
                os_file_open(arguments.new_path, (OpenFlags){.write = 1, .create = 1, .truncate = 1}, (OpenPermissions){.read = 1, .write = 1});
            if (destination)
            {
                u8 buffer[BUSTER_KB(64)];
                u64 count = 0;
                while ((result = os_file_read_attempt(source, (ByteSlice){buffer, sizeof(buffer)}, &count)) && count)
                {
                    result = os_file_write_attempt(destination, (ByteSlice){buffer, count});
                    if (!result) break;
                }
                result = os_file_close(destination) && result;
            }
            result = os_file_close(source) && result;
        }
    }

    return result;
}
