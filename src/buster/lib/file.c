// File content ownership and transfer policy: file_write_checked preserves
// transfer/close failures; file_publish_checked atomically replaces complete
// in-memory artifacts, and file_publish_slices_checked does the same for an
// artifact held as ordered slices; file_read owns padded arena and normalized APK asset
// reads; file_map_read and file_map_unmap own optional mappings; file_copy_checked
// streams into a staging file beside its destination and publishes it with
// os_file_replace. The staging path and publication boundary are kept together
// so callers can validate the complete artifact before replacement.
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

// APK assets are a rooted namespace rather than a host filesystem. Normalize
// safe relative segments before lookup because AAssetManager does not resolve
// the `..` produced by a quoted include in a nested source file. Never permit a
// relative asset path to escape that root.
BUSTER_GLOBAL_LOCAL String8 file_android_asset_path(Arena* arena, String8 path)
{
    char8* bytes = arena_allocate(arena, char8, path.length + 1);
    u64 length = 0;
    bool valid = path.length && path.pointer[0] != '/' && path.pointer[0] != '\\';
    for (u64 offset = 0; valid && offset < path.length;)
    {
        while (offset < path.length && (path.pointer[offset] == '/' || path.pointer[offset] == '\\')) { offset += 1; }
        u64 end = offset;
        while (end < path.length && path.pointer[end] != '/' && path.pointer[end] != '\\' && path.pointer[end] != 0) { end += 1; }
        valid = end == path.length || path.pointer[end] != 0;
        String8 segment = string_slice(path, offset, end);
        if (string_equal(segment, S8("..")))
        {
            valid = length != 0;
            while (length && bytes[length - 1] != '/') { length -= 1; }
            if (length) { length -= 1; }
        }
        else if (segment.length && !string_equal(segment, S8(".")))
        {
            if (length) { bytes[length++] = '/'; }
            memcpy(bytes + length, segment.pointer, segment.length);
            length += segment.length;
        }
        offset = end;
    }
    valid &= length != 0;
    bytes[length] = 0;
    String8 result = valid ? (String8){.pointer = bytes, .length = length} : (String8){0};
    return result;
}
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

// Once publication has failed or been refused, later close/delete failures are
// cleanup and never replace the operation's primary outcome.
BUSTER_GLOBAL_LOCAL void file_publish_record(FilePublishResult* result, OsError error)
{
    if (error.v)
    {
        if (result->status == FILE_PUBLISH_FAILED && !result->error.v)
        {
            result->error = error;
        }
        else if (!result->cleanup_error.v)
        {
            result->cleanup_error = error;
        }
    }
}

// The artifact is the concatenation of `slices`, written in order into one
// staging file, so a writer can hand over bytes it already owns in place
// instead of first copying them into one contiguous image.
FilePublishResult file_publish_slices_checked(String8 path, ByteSlice const* slices, u64 slice_count, OpenPermissions permissions)
{
    FilePublishResult result = {0};
    FileStats target = os_file_replacement_target_stats(path);
    result.error = target.error;
    bool replaces = target.valid && target.kind == OS_FILE_KIND_REGULAR;
    bool stages = false;
    if (target.valid && !replaces && target.kind != OS_FILE_KIND_MISSING)
    {
        result.status = FILE_PUBLISH_UNSUPPORTED_DESTINATION;
    }
#if BUSTER_WINDOWS
    else if (replaces && !(target.permissions & 0222))
    {
        // Match the old in-place writer's refusal of a read-only destination
        // instead of relying on MoveFileExW attribute behavior.
        result.error.v = (u32)ERROR_ACCESS_DENIED;
    }
#endif
    else
    {
        stages = target.valid;
    }

    TemporalArena scratch = scratch_begin(0, 0);
    OsFileStagingResult staging = {0};
    if (stages && !result.error.v)
    {
        staging = os_file_staging_create(scratch.arena, path, permissions);
        result.error = staging.error;
    }
    if (staging.file)
    {
#if !BUSTER_WINDOWS
        if (replaces && !result.error.v)
        {
            u32 final_permissions = target.permissions;
            if (permissions.execute)
            {
                final_permissions |= 0111;
            }
            else
            {
                final_permissions &= ~0111u;
            }
            result.error = os_file_set_permissions(staging.file, final_permissions);
        }
#endif
        for (u64 index = 0; index < slice_count && !result.error.v && result.status != FILE_PUBLISH_INVALID_STAGING; index += 1)
        {
            OsFileTransferResult written = os_file_write_checked(staging.file, slices[index]);
            result.error = written.error;
            if (!result.error.v && written.transferred != slices[index].length)
            {
                result.status = FILE_PUBLISH_INVALID_STAGING;
            }
        }
        if (!result.error.v && result.status != FILE_PUBLISH_INVALID_STAGING)
        {
            result.error = os_file_flush(staging.file);
        }
        file_publish_record(&result, os_file_close_checked(staging.file));
        if (!result.error.v && result.status != FILE_PUBLISH_INVALID_STAGING)
        {
            result.error = os_file_replace(staging.path, path);
            if (!result.error.v)
            {
                result.status = FILE_PUBLISH_PUBLISHED;
            }
        }
        if (result.status != FILE_PUBLISH_PUBLISHED)
        {
            file_publish_record(&result, os_file_delete_checked(staging.path));
        }
    }
    scratch_end(scratch);
    return result;
}

FilePublishResult file_publish_checked(String8 path, ByteSlice content, OpenPermissions permissions)
{
    return file_publish_slices_checked(path, &content, 1, permissions);
}

bool file_publish_slices(String8 path, ByteSlice const* slices, u64 slice_count)
{
    FilePublishResult result = file_publish_slices_checked(path, slices, slice_count, (OpenPermissions){.read = 1, .write = 1});
    return result.status == FILE_PUBLISH_PUBLISHED;
}

bool file_publish(String8 path, ByteSlice content)
{
    FilePublishResult result = file_publish_checked(path, content, (OpenPermissions){.read = 1, .write = 1});
    return result.status == FILE_PUBLISH_PUBLISHED;
}

bool file_publish_executable(String8 path, ByteSlice content)
{
    FilePublishResult result = file_publish_checked(path, content, (OpenPermissions){.read = 1, .write = 1, .execute = 1});
    return result.status == FILE_PUBLISH_PUBLISHED;
}

BUSTER_GLOBAL_LOCAL FileIdentity file_identity_from_stats(FileStats stats)
{
    FileIdentity result = {
        .device = stats.device,
        .index = stats.index,
        .valid = stats.valid,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL void file_map_read_fallback(Arena* arena, String8 path, FileReadOptions options, FileMapRead* result)
{
    FileReadResult read = file_read_checked(arena, path, options);
    result->bytes = read.bytes;
    result->identity = read.identity;
}

FileMapRead file_map_read(Arena* arena, String8 path, FileReadOptions options)
{
    FileMapRead result = {0};

#if BUSTER_ANDROID || BUSTER_IOS
    // Neither platform offers a mapping backend here, so the read path is the
    // only way to satisfy a caller that did not demand a mapping.
    if (!options.map_required)
    {
        file_map_read_fallback(arena, path, options, &result);
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
            file_map_read_fallback(arena, path, options, &result);
        }
    }
    else
    {
#if BUSTER_WINDOWS
    {
        OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        if (file)
        {
            FileStats stats = os_file_get_stats(file, (FileStatsOptions){.size = 1, .identity = 1});
            u64 file_size = stats.size;
            if (stats.valid && file_size)
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
                        result.identity = file_identity_from_stats(stats);
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
        u64 path_buffer_size;
        BUSTER_VALIDATE(u64_add_checked(path.length, 1, &path_buffer_size));
        char* path_buffer = (char*)arena_allocate_bytes(arena, path_buffer_size, 1);
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
                    result.identity = (FileIdentity){
                        .device = (u64)file_stats.st_dev,
                        .index = (u64)file_stats.st_ino,
                        .valid = true,
                    };
                }
            }
            close(file_descriptor);
        }
    }
#endif

        if (!result.bytes.pointer && !options.map_required)
        {
            file_map_read_fallback(arena, path, options, &result);
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
        String8 asset_path = file_android_asset_path(arena, path);
        AAsset* asset = asset_path.length ? AAssetManager_open(buster_android_asset_manager, (char*)asset_path.pointer, AASSET_MODE_BUFFER) : 0;
        if (asset)
        {
            u64 file_size = (u64)AAsset_getLength64(asset);
            u64 allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
            allocation_size = BUSTER_MAX(allocation_size, 1);
            u64 allocation_bottom = allocation_size - (file_size + options.start_padding);
            u64 allocation_alignment = options.start_alignment;
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
            FileStats stats = os_file_get_stats(opened.file, (FileStatsOptions){.size = 1, .identity = 1});
            result.error = stats.error;
            if (stats.valid)
            {
                result.identity = file_identity_from_stats(stats);
                u64 reported_size = stats.size;
                u64 allocation_alignment = options.start_alignment;
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
        result.identity = (FileIdentity){0};
        arena_set_position(arena, read_mark);
    }
    return result;
}

ByteSlice file_read(Arena* arena, String8 path, FileReadOptions options)
{
    return file_read_checked(arena, path, options).bytes;
}

// A fixed stack buffer bounds copy memory independently of the source size.
#define FILE_COPY_BUFFER_SIZE BUSTER_KB(64)

// Until the outcome is decided the first failure is the result. After a
// failure or refusal, later failures are cleanup and never replace it.
BUSTER_GLOBAL_LOCAL void file_copy_record(FileCopyResult* result, OsError error)
{
    if (result->status == FILE_COPY_FAILED && !result->error.v)
    {
        result->error = error;
    }
    else if (!result->cleanup_error.v)
    {
        result->cleanup_error = error;
    }
}

FileCopyResult file_copy_checked(CopyFileArguments arguments)
{
    FileCopyResult result = {0};
    // Other aliases are judged by identity below. Nothing here opens the
    // destination's file destructively, so identity decides policy, not safety.
    if (string_equal(arguments.original_path, arguments.new_path))
    {
        result.status = FILE_COPY_SAME_FILE;
    }
    else
    {
        OsFileOpenResult source = os_file_open_checked(arguments.original_path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        result.error = source.error;
        if (source.file)
        {
            // The source stays open while the destination is inspected, so an
            // equal identity cannot come from a recycled inode or file index.
            FileStats source_stats = os_file_get_stats(source.file, (FileStatsOptions){.identity = 1});
            FileStats target = {.error = source_stats.error};
            if (source_stats.valid)
            {
                target = os_file_replacement_target_stats(arguments.new_path);
            }
            result.error = target.error;
            bool replaces = target.valid && target.kind == OS_FILE_KIND_REGULAR;
            bool stages = false;
            if (replaces && target.device == source_stats.device && target.index == source_stats.index)
            {
                result.status = FILE_COPY_SAME_FILE;
            }
            else if (target.valid && !replaces && target.kind != OS_FILE_KIND_MISSING)
            {
                result.status = FILE_COPY_UNSUPPORTED_DESTINATION;
            }
#if BUSTER_WINDOWS
            else if (replaces && !(target.permissions & 0222))
            {
                // A POSIX writer's open refuses a read-only file; refuse the
                // attribute here rather than depend on MoveFileExW's handling.
                result.error.v = (u32)ERROR_ACCESS_DENIED;
            }
#endif
            else
            {
                stages = target.valid;
            }

            TemporalArena scratch = scratch_begin(0, 0);
            OsFileStagingResult staging = {0};
            if (stages)
            {
                staging = os_file_staging_create(scratch.arena, arguments.new_path, (OpenPermissions){.read = 1, .write = 1});
                result.error = staging.error;
            }
            bool staged = staging.file != 0;
            if (staged)
            {
#if !BUSTER_WINDOWS
                if (replaces)
                {
                    result.error = os_file_set_permissions(staging.file, target.permissions);
                }
#endif
                u8 buffer[FILE_COPY_BUFFER_SIZE];
                bool copying = !result.error.v;
                while (copying)
                {
                    OsFileReadResult read = os_file_read_exact(source.file, (ByteSlice){buffer, sizeof(buffer)});
                    if (read.status == OS_FILE_READ_ERROR)
                    {
                        result.error = read.error;
                    }
                    else
                    {
                        result.error = os_file_write_checked(staging.file, (ByteSlice){buffer, read.transferred}).error;
                    }
                    copying = !result.error.v && read.status == OS_FILE_READ_OK;
                }
                file_copy_record(&result, os_file_close_checked(staging.file));
            }
            // Closing the source before publication leaves the rename as the
            // last fallible step.
            file_copy_record(&result, os_file_close_checked(source.file));
            if (staged && !result.error.v)
            {
                result.error = os_file_replace(staging.path, arguments.new_path);
                if (!result.error.v)
                {
                    result.status = FILE_COPY_PUBLISHED;
                }
            }
            if (staged && result.status != FILE_COPY_PUBLISHED)
            {
                file_copy_record(&result, os_file_delete_checked(staging.path));
            }
            scratch_end(scratch);
        }
    }

    return result;
}

bool file_copy(CopyFileArguments arguments)
{
    return file_copy_checked(arguments).status == FILE_COPY_PUBLISHED;
}
