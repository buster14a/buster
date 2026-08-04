#include <buster/lib/ide_document.h>

#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/hash.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>
#if defined(__linux__) || defined(__APPLE__)
#include <stdio.h>
#endif

typedef enum IdePathKind
{
    IDE_PATH_MISSING,
    IDE_PATH_REGULAR,
    IDE_PATH_DIRECTORY,
    IDE_PATH_SYMLINK,
    IDE_PATH_OTHER,
} IdePathKind;

typedef struct IdeLoadedFile IdeLoadedFile;
struct IdeLoadedFile
{
    String8 source;
    FileStats stats;
    u64 hash;
};

typedef struct IdeParsedDocument IdeParsedDocument;
struct IdeParsedDocument
{
    ParserResult parser;
    u32 tokenizer_error_count;
    bool analysis_eligible;
    u8 reserved[3];
};

typedef struct IdeScanResult IdeScanResult;
struct IdeScanResult
{
    String8* paths;
    u32 path_count;
};

typedef struct IdeImportTraversalFrame IdeImportTraversalFrame;
struct IdeImportTraversalFrame
{
    u32 document_index;
    u32 next_import_index;
};

BUSTER_GLOBAL_LOCAL u64 ide_save_temp_serial;
#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool ide_test_force_save_replace_failure;
#endif

BUSTER_GLOBAL_LOCAL String8 ide_string_copy(Arena* arena, String8 string)
{
    if (string.length == UINT64_MAX)
    {
        return (String8){0};
    }

    char8* pointer = (char8*)arena_allocate_bytes(arena, string.length + 1, BUSTER_ALIGN_OF(char8));
    if (string.length)
    {
        memcpy(pointer, string.pointer, string.length);
    }
    pointer[string.length] = 0;
    return (String8){.pointer = pointer, .length = string.length};
}

BUSTER_GLOBAL_LOCAL u8 ide_identity_byte(u8 byte)
{
#if defined(_WIN32)
    if (byte >= (u8)'A' && byte <= (u8)'Z')
    {
        byte = (u8)(byte + ((u8)'a' - (u8)'A'));
    }
#else
    BUSTER_UNUSED(byte);
#endif
    return byte;
}

BUSTER_GLOBAL_LOCAL s32 ide_string_compare(String8 left, String8 right, bool identity)
{
    u64 common_length = BUSTER_MIN(left.length, right.length);
    for (u64 index = 0; index < common_length; index += 1)
    {
        u8 left_byte = (u8)left.pointer[index];
        u8 right_byte = (u8)right.pointer[index];
        if (identity)
        {
            left_byte = ide_identity_byte(left_byte);
            right_byte = ide_identity_byte(right_byte);
        }
        if (left_byte != right_byte)
        {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left.length == right.length)
    {
        return 0;
    }
    return left.length < right.length ? -1 : 1;
}

BUSTER_GLOBAL_LOCAL bool ide_identity_equal(String8 left, String8 right)
{
    return ide_string_compare(left, right, true) == 0;
}

BUSTER_GLOBAL_LOCAL bool ide_path_has_suffix(String8 path, String8 suffix)
{
    return path.length >= suffix.length && string_equal(string_slice(path, path.length - suffix.length, path.length), suffix);
}

BUSTER_GLOBAL_LOCAL bool ide_path_is_absolute(String8 path)
{
    bool result = path.length && path.pointer[0] == '/';
#if defined(_WIN32)
    result |= path.length && path.pointer[0] == '\\';
    result |= path.length >= 2 && path.pointer[1] == ':';
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL IdePathKind ide_path_kind(String8 path)
{
    if (!path.length || !path.pointer)
    {
        return IDE_PATH_MISSING;
    }

#if defined(_WIN32)
    TemporalArena scratch = scratch_begin(0, 0);
    String16 path_w = string16_from_string8(scratch.arena, path, true);
    DWORD attributes = GetFileAttributesW(path_w.pointer);
    scratch_end(scratch);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return IDE_PATH_MISSING;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        return IDE_PATH_SYMLINK;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? IDE_PATH_DIRECTORY : IDE_PATH_REGULAR;
#else
    struct stat stats = {0};
    if (lstat((const char*)path.pointer, &stats) != 0)
    {
        return IDE_PATH_MISSING;
    }
    if (S_ISLNK(stats.st_mode))
    {
        return IDE_PATH_SYMLINK;
    }
    if (S_ISDIR(stats.st_mode))
    {
        return IDE_PATH_DIRECTORY;
    }
    if (S_ISREG(stats.st_mode))
    {
        return IDE_PATH_REGULAR;
    }
    return IDE_PATH_OTHER;
#endif
}

BUSTER_GLOBAL_LOCAL bool ide_path_separator(char8 byte);
BUSTER_GLOBAL_LOCAL String8 ide_path_parent(Arena* arena, String8 path);

String8 ide_document_path_canonical(Arena* arena, String8 path)
{
    if (!arena || !path.length || !path.pointer)
    {
        return (String8){0};
    }

    String8 path_z = ide_string_copy(arena, path);
    String8 result = os_path_absolute(arena, path_z, true);
#if defined(__linux__) || defined(__APPLE__)
    if (!result.length)
    {
        String8 parent = ide_path_parent(arena, path_z);
        u64 basename_start = 0;
        if (parent.length)
        {
            basename_start = parent.length;
            while (basename_start < path_z.length && ide_path_separator(path_z.pointer[basename_start]))
            {
                basename_start += 1;
            }
        }
        else
        {
            parent = ide_string_copy(arena, S8("."));
        }

        if (basename_start < path_z.length)
        {
            String8 basename = string_slice(path_z, basename_start, path_z.length);
            if (!string_equal(basename, S8(".")) && !string_equal(basename, S8("..")))
            {
                String8 canonical_parent = os_path_absolute(arena, parent, true);
                if (canonical_parent.length)
                {
                    bool parent_separator = ide_path_separator(canonical_parent.pointer[canonical_parent.length - 1]);
                    result = string_format_z(arena, parent_separator ? S8("{S8}{S8}") : S8("{S8}/{S8}"), canonical_parent, basename);
                }
            }
        }
    }
#elif defined(_WIN32)
    for (u64 index = 0; index < result.length; index += 1)
    {
        if (result.pointer[index] == '\\')
        {
            result.pointer[index] = '/';
        }
    }
#endif
    return result;
}

String8 ide_document_path_identity(Arena* arena, String8 canonical_path)
{
    String8 result = ide_string_copy(arena, canonical_path);
    for (u64 index = 0; index < result.length; index += 1)
    {
        result.pointer[index] = (char8)ide_identity_byte((u8)result.pointer[index]);
#if defined(_WIN32)
        if (result.pointer[index] == '\\')
        {
            result.pointer[index] = '/';
        }
#endif
    }
    return result;
}

bool ide_document_path_is_within(String8 root, String8 path)
{
    if (!root.length || !path.length || path.length < root.length)
    {
        return false;
    }

    for (u64 index = 0; index < root.length; index += 1)
    {
        u8 root_byte = ide_identity_byte((u8)root.pointer[index]);
        u8 path_byte = ide_identity_byte((u8)path.pointer[index]);
#if defined(_WIN32)
        if (root_byte == '\\')
        {
            root_byte = '/';
        }
        if (path_byte == '\\')
        {
            path_byte = '/';
        }
#endif
        if (root_byte != path_byte)
        {
            return false;
        }
    }

    return path.length == root.length || ide_path_separator(root.pointer[root.length - 1]) ||
           (path.length > root.length && ide_path_separator(path.pointer[root.length]));
}

BUSTER_GLOBAL_LOCAL bool ide_path_separator(char8 byte)
{
#if defined(_WIN32)
    return byte == '/' || byte == '\\';
#else
    return byte == '/';
#endif
}

BUSTER_GLOBAL_LOCAL bool ide_path_has_link_component(Arena* arena, String8 root, String8 path)
{
    if (!arena || !ide_document_path_is_within(root, path))
    {
        return true;
    }

    char8* prefix = (char8*)arena_allocate_bytes(arena, path.length + 1, BUSTER_ALIGN_OF(char8));
    memcpy(prefix, path.pointer, path.length);
    prefix[path.length] = 0;
    u64 cursor = root.length;
    while (cursor < path.length)
    {
        while (cursor < path.length && ide_path_separator(path.pointer[cursor]))
        {
            cursor += 1;
        }
        u64 component_start = cursor;
        while (cursor < path.length && !ide_path_separator(path.pointer[cursor]))
        {
            cursor += 1;
        }
        if (component_start == cursor)
        {
            continue;
        }
        String8 component = string_slice(path, component_start, cursor);
        if (string_equal(component, S8(".")))
        {
            continue;
        }
        if (string_equal(component, S8("..")))
        {
            return true;
        }
        char8 separator = prefix[cursor];
        prefix[cursor] = 0;
        IdePathKind kind = ide_path_kind((String8){.pointer = prefix, .length = cursor});
        prefix[cursor] = separator;
        if (kind == IDE_PATH_SYMLINK)
        {
            return true;
        }
        if (kind == IDE_PATH_MISSING)
        {
            // A missing final component is allowed so reload/poll can report a
            // normal deletion. Missing intermediate components cannot be safe.
            if (cursor < path.length)
            {
                return true;
            }
            continue;
        }
        if (cursor < path.length && kind != IDE_PATH_DIRECTORY)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 ide_path_parent(Arena* arena, String8 path)
{
    if (!path.length)
    {
        return (String8){0};
    }

    u64 slash = BUSTER_STRING_NO_MATCH;
    for (u64 index = path.length; index; index -= 1)
    {
        char8 byte = path.pointer[index - 1];
        if (ide_path_separator(byte))
        {
            slash = index - 1;
            break;
        }
    }
    if (slash == BUSTER_STRING_NO_MATCH)
    {
        return (String8){0};
    }
    if (slash == 0)
    {
        return ide_string_copy(arena, string_slice(path, 0, 1));
    }
#if defined(_WIN32)
    if (slash == 2 && path.length >= 3 && path.pointer[1] == ':')
    {
        return ide_string_copy(arena, string_slice(path, 0, 3));
    }
#endif
    return ide_string_copy(arena, string_slice(path, 0, slash));
}

BUSTER_GLOBAL_LOCAL String8 ide_path_join(Arena* arena, String8 root, String8 part, bool add_bbb)
{
    bool root_separator = root.length && ide_path_separator(root.pointer[root.length - 1]);
    bool part_has_extension = ide_path_has_suffix(part, S8(".bbb"));
    String8 result = string_format_z(arena, root_separator ? S8("{S8}{S8}{S8}") : S8("{S8}/{S8}{S8}"), root, part,
                                     add_bbb && !part_has_extension ? S8(".bbb") : S8(""));
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ide_path_resolve_for_root(Arena* arena, String8 root, String8 path)
{
    return ide_path_is_absolute(path) ? ide_string_copy(arena, path) : ide_path_join(arena, root, path, false);
}

BUSTER_GLOBAL_LOCAL bool ide_file_read(Arena* arena, String8 root, String8 path, IdeLoadedFile* result, IdeDocumentErrorKind* error_out)
{
    *result = (IdeLoadedFile){0};
    if (!ide_document_path_is_within(root, path) || ide_path_has_link_component(arena, root, path))
    {
        *error_out = IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        return false;
    }
    IdePathKind kind = ide_path_kind(path);
    if (kind == IDE_PATH_MISSING)
    {
        *error_out = IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
        return false;
    }
    if (kind != IDE_PATH_REGULAR)
    {
        *error_out = IDE_DOCUMENT_ERROR_NOT_REGULAR_FILE;
        return false;
    }

    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
    if (!file)
    {
        *error_out = IDE_DOCUMENT_ERROR_FILE_READ;
        return false;
    }

    u64 size = os_file_get_size(file);
    u8* bytes = (u8*)arena_allocate_bytes(arena, size + 1, BUSTER_ALIGN_OF(u8));
    u64 read_size = os_file_read(file, (ByteSlice){.pointer = bytes, .length = size}, size);
    FileStats stats = os_file_get_stats(file, (FileStatsOptions){.size = 1, .modified_time = 1});
    bool close_result = os_file_close(file);
    if (read_size != size || !close_result)
    {
        *error_out = IDE_DOCUMENT_ERROR_FILE_READ;
        return false;
    }

    bytes[size] = 0;
    result->source = (String8){.pointer = (char8*)bytes, .length = size};
    result->stats = stats;
    result->hash = buster_hash_64(bytes, size);
    *error_out = IDE_DOCUMENT_ERROR_NONE;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_loaded_file_matches(String8 source, u64 hash, String8 expected_source, u64 expected_hash)
{
    return hash == expected_hash && string_equal(source, expected_source);
}

BUSTER_GLOBAL_LOCAL void ide_document_clamp_view(IdeDocument* document)
{
    u64 source_length = document->source.length;
    document->view.cursor_offset = BUSTER_MIN(document->view.cursor_offset, source_length);
    document->view.selection_start = BUSTER_MIN(document->view.selection_start, source_length);
    document->view.selection_end = BUSTER_MIN(document->view.selection_end, source_length);
    if (document->view.selection_start > document->view.selection_end)
    {
        document->view.selection_start = document->view.selection_end;
    }
}

BUSTER_GLOBAL_LOCAL String8 ide_save_temp_path(Arena* arena, String8 path, u64 serial)
{
    String8 parent = ide_path_parent(arena, path);
    if (!parent.length)
    {
        return (String8){0};
    }
    String8 name = string_format_z(arena, S8(".buster-save-{u64}-{u64}.tmp"), os_get_current_process_id(), serial);
    return ide_path_join(arena, parent, name, false);
}

BUSTER_GLOBAL_LOCAL bool ide_atomic_write_file(Arena* arena, String8 path, String8 source)
{
#if defined(__linux__) || defined(__APPLE__)
    struct stat destination_stats = {0};
    if (lstat((const char*)path.pointer, &destination_stats) != 0 || !S_ISREG(destination_stats.st_mode))
    {
        return false;
    }
    mode_t destination_mode = destination_stats.st_mode & 07777;
    for (u32 attempt = 0; attempt < 64; attempt += 1)
    {
        String8 temporary_path = ide_save_temp_path(arena, path, ide_save_temp_serial++);
        if (!temporary_path.length)
        {
            return false;
        }
        int file_descriptor = open((const char*)temporary_path.pointer, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file_descriptor < 0)
        {
            if (errno == EEXIST)
            {
                continue;
            }
            return false;
        }

        bool write_success = true;
        u64 offset = 0;
        while (write_success && offset < source.length)
        {
            u64 chunk_length = BUSTER_MIN(source.length - offset, (u64)0x40000000);
            ssize_t write_count;
            do
            {
                write_count = write(file_descriptor, source.pointer + offset, (size_t)chunk_length);
            } while (write_count < 0 && errno == EINTR);
            if (write_count <= 0)
            {
                write_success = false;
            }
            else
            {
                offset += (u64)write_count;
            }
        }

        if (write_success)
        {
            write_success = fchmod(file_descriptor, destination_mode) == 0;
        }
        int sync_result = 0;
        if (write_success)
        {
            do
            {
                sync_result = fsync(file_descriptor);
            } while (sync_result < 0 && errno == EINTR);
            write_success = sync_result == 0;
        }
        bool close_success = close(file_descriptor) == 0;
        if (!write_success || !close_success)
        {
            os_file_delete(temporary_path);
            return false;
        }
#if BUSTER_INCLUDE_TESTS
        if (ide_test_force_save_replace_failure)
        {
            os_file_delete(temporary_path);
            return false;
        }
#endif
        int rename_result;
        do
        {
            rename_result = rename((const char*)temporary_path.pointer, (const char*)path.pointer);
        } while (rename_result != 0 && errno == EINTR);
        if (rename_result != 0)
        {
            os_file_delete(temporary_path);
            return false;
        }
        return true;
    }
#elif defined(_WIN32)
    String16 destination_w = string16_from_string8(arena, path, true);
    for (u32 attempt = 0; attempt < 64; attempt += 1)
    {
        String8 temporary_path = ide_save_temp_path(arena, path, ide_save_temp_serial++);
        if (!temporary_path.length)
        {
            return false;
        }
        String16 temporary_w = string16_from_string8(arena, temporary_path, true);
        HANDLE file = CreateFileW(temporary_w.pointer, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
            {
                continue;
            }
            return false;
        }

        bool write_success = true;
        u64 offset = 0;
        while (write_success && offset < source.length)
        {
            DWORD chunk_length = (DWORD)BUSTER_MIN(source.length - offset, (u64)0x40000000);
            DWORD write_count = 0;
            write_success = WriteFile(file, source.pointer + offset, chunk_length, &write_count, 0) != 0 && write_count == chunk_length;
            offset += write_count;
        }
        if (write_success)
        {
            write_success = FlushFileBuffers(file) != 0;
        }
        bool close_success = CloseHandle(file) != 0;
        if (!write_success || !close_success)
        {
            os_file_delete(temporary_path);
            return false;
        }
#if BUSTER_INCLUDE_TESTS
        if (ide_test_force_save_replace_failure)
        {
            os_file_delete(temporary_path);
            return false;
        }
#endif
        if (!ReplaceFileW(destination_w.pointer, temporary_w.pointer, 0, REPLACEFILE_WRITE_THROUGH, 0, 0))
        {
            os_file_delete(temporary_path);
            return false;
        }
        return true;
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(path);
    BUSTER_UNUSED(source);
#endif
    return false;
}

BUSTER_GLOBAL_LOCAL bool ide_scan_append(Arena* arena, String8 root, String8* directories, u32* directory_count, String8* paths, u32* path_count,
                                         u32 capacity, u32* entry_count, String8 entry)
{
    if (*entry_count >= capacity)
    {
        return false;
    }
    *entry_count += 1;

    if (ide_path_has_link_component(arena, root, entry))
    {
        return true;
    }
    IdePathKind kind = ide_path_kind(entry);
    if (kind == IDE_PATH_SYMLINK || kind == IDE_PATH_OTHER || kind == IDE_PATH_MISSING)
    {
        return true;
    }
    if (kind == IDE_PATH_DIRECTORY)
    {
        if (*directory_count >= capacity)
        {
            return false;
        }
        directories[*directory_count] = ide_string_copy(arena, entry);
        *directory_count += 1;
    }
    else if (kind == IDE_PATH_REGULAR && ide_path_has_suffix(entry, S8(".bbb")))
    {
        if (*path_count >= capacity)
        {
            return false;
        }
        String8 canonical = ide_document_path_canonical(arena, entry);
        if (!canonical.length || !ide_document_path_is_within(root, canonical) || ide_path_has_link_component(arena, root, canonical))
        {
            return false;
        }
        paths[*path_count] = canonical;
        *path_count += 1;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_scan_workspace(Arena* arena, String8 root, u32 capacity, IdeScanResult* result)
{
    if (!capacity || !root.length || !root.pointer)
    {
        result->paths = 0;
        result->path_count = 0;
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }
    result->paths = arena_allocate(arena, String8, capacity);
    result->path_count = 0;
    String8* directories = arena_allocate(arena, String8, capacity);
    if (!result->paths || !directories)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    u32 directory_count = 1;
    u32 directory_index = 0;
    u32 entry_count = 0;
    directories[0] = ide_string_copy(arena, root);
    if (!directories[0].length || !directories[0].pointer)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }

    while (directory_index < directory_count)
    {
        String8 directory_path = directories[directory_index];
        directory_index += 1;
#if defined(_WIN32)
        String8 pattern = string_format_z(arena, S8("{S8}\\*"), directory_path);
        String16 pattern_w = string16_from_string8(arena, pattern, true);
        WIN32_FIND_DATAW find_data = {0};
        HANDLE find = FindFirstFileW(pattern_w.pointer, &find_data);
        if (find == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND)
            {
                return IDE_DOCUMENT_ERROR_FILE_READ;
            }
            continue;
        }
        bool more = true;
        while (more)
        {
            u64 name_length = 0;
            while (find_data.cFileName[name_length])
            {
                name_length += 1;
            }
            String8 name = string8_from_string16(arena, (String16){.pointer = (char16*)find_data.cFileName, .length = name_length}, false);
            if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
            {
                String8 entry = string_format_z(arena, S8("{S8}\\{S8}"), directory_path, name);
                if (!ide_scan_append(arena, root, directories, &directory_count, result->paths, &result->path_count, capacity, &entry_count, entry))
                {
                    FindClose(find);
                    return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
                }
            }
            more = FindNextFileW(find, &find_data) != 0;
        }
        DWORD enumeration_error = GetLastError();
        FindClose(find);
        if (enumeration_error != ERROR_NO_MORE_FILES)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
#else
        if (!directory_path.length || !directory_path.pointer)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        DIR* directory = opendir((const char*)directory_path.pointer);
        if (!directory)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        for (;;)
        {
            errno = 0;
            struct dirent* entry = readdir(directory);
            if (!entry)
            {
                if (errno != 0)
                {
                    closedir(directory);
                    return IDE_DOCUMENT_ERROR_FILE_READ;
                }
                break;
            }
            String8 name = string_from_pointer((const char8*)entry->d_name);
            if (string_equal(name, S8(".")) || string_equal(name, S8("..")))
            {
                continue;
            }
            String8 entry_path = string_format_z(arena, S8("{S8}/{S8}"), directory_path, name);
            if (!ide_scan_append(arena, root, directories, &directory_count, result->paths, &result->path_count, capacity, &entry_count, entry_path))
            {
                closedir(directory);
                return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
            }
        }
        closedir(directory);
#endif
    }

    for (u32 index = 1; index < result->path_count; index += 1)
    {
        String8 value = result->paths[index];
        u32 insertion = index;
        while (insertion && ide_string_compare(result->paths[insertion - 1], value, true) > 0)
        {
            result->paths[insertion] = result->paths[insertion - 1];
            insertion -= 1;
        }
        result->paths[insertion] = value;
    }

    u32 unique_count = 0;
    for (u32 index = 0; index < result->path_count; index += 1)
    {
        if (!unique_count || !ide_identity_equal(result->paths[unique_count - 1], result->paths[index]))
        {
            result->paths[unique_count] = result->paths[index];
            unique_count += 1;
        }
    }
    result->path_count = unique_count;
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL u32 ide_workspace_find_identity(IdeDocumentWorkspace* workspace, String8 identity)
{
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        if (ide_identity_equal(workspace->documents[index].identity, identity))
        {
            return index;
        }
    }
    return IDE_DOCUMENT_INDEX_INVALID;
}

BUSTER_GLOBAL_LOCAL u32 ide_workspace_find_exact_path(IdeDocumentWorkspace* workspace, String8 path)
{
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        if (string_equal(workspace->documents[index].path, path) || string_equal(workspace->documents[index].identity, path))
        {
            return index;
        }
    }
    return IDE_DOCUMENT_INDEX_INVALID;
}

BUSTER_GLOBAL_LOCAL u32 ide_workspace_find_path(Arena* arena, IdeDocumentWorkspace* workspace, String8 path)
{
    String8 candidate = ide_path_resolve_for_root(arena, workspace->root_path, path);
    String8 canonical = ide_document_path_canonical(arena, candidate);
    if (canonical.length)
    {
        String8 identity = ide_document_path_identity(arena, canonical);
        u32 index = ide_workspace_find_identity(workspace, identity);
        if (index != IDE_DOCUMENT_INDEX_INVALID)
        {
            return index;
        }
    }
    return ide_workspace_find_exact_path(workspace, canonical.length ? canonical : candidate);
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_model_path_error(Arena* arena, String8 root, String8 path)
{
    String8 candidate = ide_path_resolve_for_root(arena, root, path);
    String8 canonical = ide_document_path_canonical(arena, candidate);
    if (canonical.length)
    {
        if (!ide_document_path_is_within(root, canonical) || ide_path_has_link_component(arena, root, canonical))
        {
            return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        }
    }
    else if (!ide_document_path_is_within(root, candidate))
    {
        return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
    }

    if (ide_document_path_is_within(root, candidate) && ide_path_has_link_component(arena, root, candidate))
    {
        return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
    }
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL String8 ide_module_name_from_path(Arena* arena, String8 root, String8 path)
{
    u64 start = root.length;
    if (start < path.length && (path.pointer[start] == '/' || path.pointer[start] == '\\'))
    {
        start += 1;
    }
    String8 relative = string_slice(path, start, path.length);
    if (ide_path_has_suffix(relative, S8(".bbb")))
    {
        relative = string_slice(relative, 0, relative.length - 4);
    }
    return ide_string_copy(arena, relative);
}

BUSTER_GLOBAL_LOCAL u32 ide_workspace_find_module(IdeDocumentWorkspace* workspace, String8 requested)
{
    String8 normalized = requested;
    if (ide_path_has_suffix(normalized, S8(".bbb")))
    {
        normalized = string_slice(normalized, 0, normalized.length - 4);
    }
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        if (string_equal(workspace->documents[index].module_name, normalized))
        {
            return index;
        }
    }
    return IDE_DOCUMENT_INDEX_INVALID;
}

BUSTER_GLOBAL_LOCAL bool ide_document_copy_to_arena(Arena* arena, IdeDocument* destination, const IdeDocument* source)
{
    *destination = *source;
    destination->path = ide_string_copy(arena, source->path);
    destination->identity = ide_string_copy(arena, source->identity);
    destination->module_name = ide_string_copy(arena, source->module_name);
    destination->source = ide_string_copy(arena, source->source);
    destination->saved_source = ide_string_copy(arena, source->saved_source);
    destination->search.query = ide_string_copy(arena, source->search.query);
    destination->search.replacement = ide_string_copy(arena, source->search.replacement);
    destination->compile.artifact_path = ide_string_copy(arena, source->compile.artifact_path);
    destination->compile.command_line = ide_string_copy(arena, source->compile.command_line);
    destination->compile.message = ide_string_copy(arena, source->compile.message);
    if (source->diagnostic_count)
    {
        destination->diagnostics = arena_allocate(arena, IdeDocumentDiagnostic, source->diagnostic_count);
        for (u32 index = 0; index < source->diagnostic_count; index += 1)
        {
            destination->diagnostics[index] = source->diagnostics[index];
            destination->diagnostics[index].file_path = ide_string_copy(arena, source->diagnostics[index].file_path);
            destination->diagnostics[index].message = ide_string_copy(arena, source->diagnostics[index].message);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_workspace_copy_to_arena(Arena* arena, IdeDocumentWorkspace* destination, const IdeDocumentWorkspace* source)
{
    *destination = *source;
    destination->root_path = ide_string_copy(arena, source->root_path);
    destination->filter.query = ide_string_copy(arena, source->filter.query);
    if (source->document_count)
    {
        destination->documents = arena_allocate(arena, IdeDocument, source->document_count);
        for (u32 index = 0; index < source->document_count; index += 1)
        {
            ide_document_copy_to_arena(arena, destination->documents + index, source->documents + index);
        }
    }
    if (source->import_count)
    {
        destination->imports = arena_allocate(arena, IdeDocumentImport, source->import_count);
        for (u32 index = 0; index < source->import_count; index += 1)
        {
            IdeDocumentImport* destination_import = destination->imports + index;
            IdeDocumentImport* source_import = source->imports + index;
            *destination_import = *source_import;
            destination_import->source_path = ide_string_copy(arena, source_import->source_path);
            destination_import->name_space = ide_string_copy(arena, source_import->name_space);
            destination_import->requested_path = ide_string_copy(arena, source_import->requested_path);
            destination_import->target_path = ide_string_copy(arena, source_import->target_path);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void ide_workspace_initialize_empty(IdeDocumentWorkspace* workspace)
{
    *workspace = (IdeDocumentWorkspace){0};
    workspace->active_document_index = IDE_DOCUMENT_INDEX_INVALID;
    workspace->next_open_order = 1;
}

BUSTER_GLOBAL_LOCAL u64 ide_diagnostic_identity(String8 path, ParserSourceRange range, String8 message, IdeDocumentDiagnosticSeverity severity,
                                                IdeDocumentDiagnosticSource source)
{
    u64 values[] = {
        buster_hash_64((u8*)path.pointer, path.length),
        buster_hash_64((u8*)message.pointer, message.length),
        range.offset,
        range.length,
        range.line,
        range.column,
        (u64)(u32)severity,
        (u64)(u32)source,
    };
    return buster_hash_64((u8*)values, sizeof(values));
}

BUSTER_GLOBAL_LOCAL s32 ide_diagnostic_compare(IdeDocumentDiagnostic left, IdeDocumentDiagnostic right)
{
    s32 result = ide_string_compare(left.file_path, right.file_path, true);
    if (result == 0 && left.range.offset != right.range.offset)
    {
        result = left.range.offset < right.range.offset ? -1 : 1;
    }
    if (result == 0 && left.range.line != right.range.line)
    {
        result = left.range.line < right.range.line ? -1 : 1;
    }
    if (result == 0 && left.range.column != right.range.column)
    {
        result = left.range.column < right.range.column ? -1 : 1;
    }
    if (result == 0 && left.range.length != right.range.length)
    {
        result = left.range.length < right.range.length ? -1 : 1;
    }
    if (result == 0 && left.severity != right.severity)
    {
        result = left.severity < right.severity ? -1 : 1;
    }
    if (result == 0 && left.source != right.source)
    {
        result = left.source < right.source ? -1 : 1;
    }
    if (result == 0 && left.identity != right.identity)
    {
        result = left.identity < right.identity ? -1 : 1;
    }
    if (result == 0)
    {
        result = ide_string_compare(left.message, right.message, false);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ide_diagnostics_sort(IdeDocument* document)
{
    for (u32 index = 1; index < document->diagnostic_count; index += 1)
    {
        IdeDocumentDiagnostic value = document->diagnostics[index];
        u32 insertion = index;
        while (insertion && ide_diagnostic_compare(document->diagnostics[insertion - 1], value) > 0)
        {
            document->diagnostics[insertion] = document->diagnostics[insertion - 1];
            insertion -= 1;
        }
        document->diagnostics[insertion] = value;
    }
}

BUSTER_GLOBAL_LOCAL IdeDocumentImportState ide_import_state_from_analysis(AnalysisImportResolutionState state)
{
    switch (state)
    {
        case ANALYSIS_IMPORT_CYCLE:
            return IDE_DOCUMENT_IMPORT_CYCLE;
        case ANALYSIS_IMPORT_AMBIGUOUS:
            return IDE_DOCUMENT_IMPORT_AMBIGUOUS;
        case ANALYSIS_IMPORT_MISSING:
            return IDE_DOCUMENT_IMPORT_MISSING;
        case ANALYSIS_IMPORT_RESOLVED:
            return IDE_DOCUMENT_IMPORT_RESOLVED;
        case ANALYSIS_IMPORT_UNRESOLVED:
        case ANALYSIS_IMPORT_COUNT:
            break;
    }
    return IDE_DOCUMENT_IMPORT_MISSING;
}

BUSTER_GLOBAL_LOCAL String8 ide_import_target_path(Arena* arena, String8 root, String8 requested)
{
    if (!requested.length)
    {
        return (String8){0};
    }
    String8 candidate = ide_path_is_absolute(requested) ? ide_string_copy(arena, requested) : ide_path_join(arena, root, requested, true);
    String8 canonical = ide_document_path_canonical(arena, candidate);
    if (canonical.length)
    {
        if (ide_document_path_is_within(root, canonical) && !ide_path_has_link_component(arena, root, candidate))
        {
            return canonical;
        }
        return (String8){0};
    }
    if (ide_document_path_is_within(root, candidate) && !ide_path_has_link_component(arena, root, candidate))
    {
        return candidate;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL void ide_documents_sort(IdeDocument* documents, u32 count)
{
    for (u32 index = 1; index < count; index += 1)
    {
        IdeDocument document = documents[index];
        u32 insertion = index;
        while (insertion && ide_string_compare(documents[insertion - 1].identity, document.identity, true) > 0)
        {
            documents[insertion] = documents[insertion - 1];
            insertion -= 1;
        }
        documents[insertion] = document;
    }
}

BUSTER_GLOBAL_LOCAL void ide_imports_detect_cycles(Arena* arena, IdeDocumentWorkspace* workspace)
{
    if (!workspace->document_count || !workspace->import_count)
    {
        return;
    }
    u8* colors = arena_allocate(arena, u8, workspace->document_count);
    IdeImportTraversalFrame* frames = arena_allocate(arena, IdeImportTraversalFrame, workspace->document_count);
    memset(colors, 0, workspace->document_count);
    for (u32 root = 0; root < workspace->document_count; root += 1)
    {
        if (colors[root])
        {
            continue;
        }
        u32 frame_count = 1;
        frames[0] = (IdeImportTraversalFrame){.document_index = root};
        colors[root] = 1;
        while (frame_count)
        {
            IdeImportTraversalFrame* frame = frames + frame_count - 1;
            u32 import_index = frame->next_import_index;
            while (import_index < workspace->import_count &&
                   !ide_identity_equal(workspace->imports[import_index].source_path, workspace->documents[frame->document_index].path))
            {
                import_index += 1;
            }
            if (import_index >= workspace->import_count)
            {
                colors[frame->document_index] = 2;
                frame_count -= 1;
                continue;
            }
            frame->next_import_index = import_index + 1;
            IdeDocumentImport* import = workspace->imports + import_index;
            if (import->state != IDE_DOCUMENT_IMPORT_RESOLVED)
            {
                continue;
            }
            u32 target = ide_workspace_find_exact_path(workspace, import->target_path);
            if (target == IDE_DOCUMENT_INDEX_INVALID)
            {
                continue;
            }
            if (colors[target] == 1)
            {
                import->state = IDE_DOCUMENT_IMPORT_CYCLE;
            }
            else if (!colors[target])
            {
                BUSTER_CHECK(frame_count < workspace->document_count);
                frames[frame_count] = (IdeImportTraversalFrame){.document_index = target};
                frame_count += 1;
                colors[target] = 1;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_rebuild_workspace_analysis(Arena* arena, Arena* expression_arena, IdeDocumentWorkspace* result,
                                                                         u32 max_diagnostics)
{
    u32 document_count = result->document_count;
    IdeParsedDocument* parsed = 0;
    if (document_count)
    {
        parsed = arena_allocate(arena, IdeParsedDocument, document_count);
        if (!parsed)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        memset(parsed, 0, sizeof(*parsed) * document_count);
    }

    u64 total_diagnostics = 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        IdeDocument* document = result->documents + index;
        TokenizerResult tokenizer = tokenize(arena, document->source.pointer, document->source.length);
        ParserResult parser = parser_parse(arena, expression_arena, document->source, tokenizer);
        parsed[index] = (IdeParsedDocument){
            .parser = parser,
            .tokenizer_error_count = tokenizer.error_count,
            .analysis_eligible = tokenizer.error_count == 0 && parser.diagnostic_count == 0,
        };
        total_diagnostics += parser.diagnostic_count + (tokenizer.error_count != 0);
    }

    AnalysisResult** analyses = 0;
    if (document_count)
    {
        analyses = arena_allocate(arena, AnalysisResult*, document_count);
        if (!analyses)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        memset(analyses, 0, sizeof(*analyses) * document_count);
    }
    for (u32 index = 0; index < document_count; index += 1)
    {
        IdeDocument* document = result->documents + index;
        AnalysisSourceInput input = {
            .path = document->path,
            .parser = parsed[index].analysis_eligible ? &parsed[index].parser : 0,
        };
        AnalysisResult* analysis = arena_allocate(arena, AnalysisResult, 1);
        *analysis = analysis_index_module(arena, (AnalysisModuleId){.value = index}, document->module_name, &input, 1);
        analyses[index] = analysis;
    }
    if (document_count)
    {
        analysis_resolve_program_interfaces(arena, analyses, document_count);
        for (u32 index = 0; index < document_count; index += 1)
        {
            if (parsed[index].analysis_eligible)
            {
                analysis_analyze_bodies(arena, analyses[index]);
            }
            total_diagnostics += analyses[index]->diagnostic_count;
        }
    }
    if (total_diagnostics > max_diagnostics)
    {
        return IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT;
    }

    u64 import_count = 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        import_count += parsed[index].parser.import_count;
    }
    if (import_count > UINT32_MAX)
    {
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }
    result->imports = 0;
    if (import_count)
    {
        result->imports = arena_allocate(arena, IdeDocumentImport, (u32)import_count);
        if (!result->imports)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
    }
    result->import_count = (u32)import_count;
    u32 import_index = 0;
    if (import_count)
    {
        for (u32 index = 0; index < document_count; index += 1)
        {
            IdeDocument* document = result->documents + index;
            u32 analysis_import_index = 0;
            for (AstImport* ast_import = parsed[index].parser.first_import; ast_import; ast_import = ast_import->next)
            {
                IdeDocumentImport* import = result->imports + import_index;
                *import = (IdeDocumentImport){
                    .source_path = document->path,
                    .name_space = ide_string_copy(arena, ast_import->name_space.text),
                    .requested_path = ide_string_copy(arena, ast_import->path),
                    .target_path = {0},
                    .range = ast_import->range,
                    .path_range = ast_import->path_range,
                    .state = IDE_DOCUMENT_IMPORT_MISSING,
                };
                u32 target_index = ide_workspace_find_module(result, ast_import->path);
                if (target_index == IDE_DOCUMENT_INDEX_INVALID)
                {
                    String8 target_path = ide_import_target_path(arena, result->root_path, ast_import->path);
                    import->target_path = target_path;
                    if (target_path.length)
                    {
                        target_index = ide_workspace_find_exact_path(result, target_path);
                    }
                }
                else
                {
                    import->target_path = result->documents[target_index].path;
                }
                import->state = target_index == IDE_DOCUMENT_INDEX_INVALID ? IDE_DOCUMENT_IMPORT_MISSING : IDE_DOCUMENT_IMPORT_RESOLVED;
                if (analyses[index] && parsed[index].analysis_eligible && analysis_import_index < analyses[index]->module.import_count)
                {
                    IdeDocumentImportState analysis_state =
                        ide_import_state_from_analysis(analyses[index]->module.imports[analysis_import_index].state);
                    if (analysis_state == IDE_DOCUMENT_IMPORT_AMBIGUOUS || analysis_state == IDE_DOCUMENT_IMPORT_CYCLE)
                    {
                        import->state = analysis_state;
                    }
                }
                analysis_import_index += 1;
                import_index += 1;
            }
        }
    }
    BUSTER_CHECK(import_index == result->import_count);
    ide_imports_detect_cycles(arena, result);

    for (u32 index = 0; index < document_count; index += 1)
    {
        IdeDocument* document = result->documents + index;
        IdeParsedDocument* parsed_document = parsed + index;
        u64 count64 = parsed_document->parser.diagnostic_count + (parsed_document->tokenizer_error_count != 0);
        count64 += analyses[index] ? analyses[index]->diagnostic_count : 0;
        if (count64 > UINT32_MAX)
        {
            return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
        }
        u32 count = (u32)count64;
        document->diagnostics = 0;
        document->diagnostic_count = count;
        if (count)
        {
            document->diagnostics = arena_allocate(arena, IdeDocumentDiagnostic, count);
            if (!document->diagnostics)
            {
                return IDE_DOCUMENT_ERROR_FILE_READ;
            }
            u32 diagnostic_index = 0;
            if (parsed_document->tokenizer_error_count)
            {
                document->diagnostics[diagnostic_index] = (IdeDocumentDiagnostic){
                    .file_path = document->path,
                    .message = ide_string_copy(arena, S8("tokenization failed")),
                    .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
                    .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_PARSER,
                };
                diagnostic_index += 1;
            }
            for (ParserDiagnostic* parser_diagnostic = parsed_document->parser.first_diagnostic; parser_diagnostic;
                 parser_diagnostic = parser_diagnostic->next)
            {
                document->diagnostics[diagnostic_index] = (IdeDocumentDiagnostic){
                    .file_path = document->path,
                    .range = parser_diagnostic->range,
                    .message = ide_string_copy(arena, parser_diagnostic->message),
                    .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
                    .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_PARSER,
                };
                diagnostic_index += 1;
            }
            for (AnalysisDiagnostic* analysis_diagnostic = analyses[index] ? analyses[index]->first_diagnostic : 0;
                 analysis_diagnostic; analysis_diagnostic = analysis_diagnostic->next)
            {
                document->diagnostics[diagnostic_index] = (IdeDocumentDiagnostic){
                    .file_path = document->path,
                    .range = analysis_diagnostic->range,
                    .message = ide_string_copy(arena, analysis_diagnostic->message),
                    .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
                    .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_ANALYSIS,
                };
                diagnostic_index += 1;
            }
            for (u32 index2 = 0; index2 < document->diagnostic_count; index2 += 1)
            {
                IdeDocumentDiagnostic* diagnostic = document->diagnostics + index2;
                diagnostic->identity = ide_diagnostic_identity(diagnostic->file_path, diagnostic->range, diagnostic->message,
                                                               diagnostic->severity, diagnostic->source);
            }
        }
        ide_diagnostics_sort(document);
    }
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_build_workspace(Arena* arena, Arena* expression_arena, String8 root_input, String8 open_input,
                                                              const IdeDocumentWorkspace* old_workspace, bool preserve_old,
                                                              u32 max_discovered_files, u32 max_traversal_entries, u32 max_diagnostics,
                                                              IdeDocumentWorkspace* result)
{
    ide_workspace_initialize_empty(result);

    String8 root_candidate = root_input.length ? root_input : S8(".");
    String8 canonical_root = ide_document_path_canonical(arena, root_candidate);
    String8 open_candidate = open_input.length ? ide_path_resolve_for_root(arena, canonical_root, open_input) : (String8){0};
    String8 canonical_open = open_input.length ? ide_document_path_canonical(arena, open_candidate) : (String8){0};
    if (open_input.length && !canonical_open.length)
    {
        return IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
    }
    if (!canonical_root.length && open_input.length && !root_input.length)
    {
        canonical_root = ide_path_parent(arena, canonical_open);
    }
    if (!canonical_root.length)
    {
        return IDE_DOCUMENT_ERROR_ROOT_NOT_FOUND;
    }

    IdePathKind root_kind = ide_path_kind(canonical_root);
    if (root_kind == IDE_PATH_REGULAR && ide_path_has_suffix(canonical_root, S8(".bbb")))
    {
        if (!canonical_open.length)
        {
            canonical_open = canonical_root;
        }
        canonical_root = ide_path_parent(arena, canonical_root);
        root_kind = ide_path_kind(canonical_root);
    }
    if (root_kind == IDE_PATH_MISSING)
    {
        return IDE_DOCUMENT_ERROR_ROOT_NOT_FOUND;
    }
    if (root_kind != IDE_PATH_DIRECTORY)
    {
        return IDE_DOCUMENT_ERROR_ROOT_NOT_DIRECTORY;
    }
    if (open_input.length)
    {
        String8 security_open = open_candidate;
        if (security_open.length && ide_document_path_is_within(canonical_root, security_open) &&
            ide_path_has_link_component(arena, canonical_root, security_open))
        {
            return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        }
    }
    if (canonical_open.length)
    {
        if (ide_path_kind(canonical_open) != IDE_PATH_REGULAR)
        {
            return IDE_DOCUMENT_ERROR_NOT_REGULAR_FILE;
        }
        if (!ide_document_path_is_within(canonical_root, canonical_open))
        {
            return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        }
    }

    IdeScanResult scan = {0};
    IdeDocumentErrorKind scan_error = ide_scan_workspace(arena, canonical_root, max_traversal_entries, &scan);
    if (scan_error != IDE_DOCUMENT_ERROR_NONE)
    {
        return scan_error;
    }

    u32 orphan_count = 0;
    if (preserve_old)
    {
        for (u32 old_index = 0; old_index < old_workspace->document_count; old_index += 1)
        {
            IdeDocument* old_document = old_workspace->documents + old_index;
            bool found = false;
            for (u32 path_index = 0; path_index < scan.path_count; path_index += 1)
            {
                String8 identity = ide_document_path_identity(arena, scan.paths[path_index]);
                found |= ide_identity_equal(old_document->identity, identity);
            }
            orphan_count += !found && (old_document->is_open || old_document->dirty);
        }
    }
    if ((u64)scan.path_count + orphan_count > max_discovered_files)
    {
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }

    u32 document_count = scan.path_count + orphan_count;
    result->root_path = ide_string_copy(arena, canonical_root);
    result->document_count = document_count;
    result->documents = document_count ? arena_allocate(arena, IdeDocument, document_count) : 0;
    if (document_count && !result->documents)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    if (document_count)
    {
        memset(result->documents, 0, sizeof(*result->documents) * document_count);
    }

    u32 document_index = 0;
    for (u32 path_index = 0; path_index < scan.path_count; path_index += 1)
    {
        String8 path = scan.paths[path_index];
        IdeDocument* document = result->documents + document_index;
        *document = (IdeDocument){0};
        document->path = ide_string_copy(arena, path);
        document->identity = ide_document_path_identity(arena, path);
        document->module_name = ide_module_name_from_path(arena, canonical_root, path);

        IdeLoadedFile loaded = {0};
        IdeDocumentErrorKind load_error = IDE_DOCUMENT_ERROR_NONE;
        if (!ide_file_read(arena, canonical_root, path, &loaded, &load_error))
        {
            return load_error;
        }
        document->source = loaded.source;
        document->saved_source = ide_string_copy(arena, loaded.source);
        document->external_stats = loaded.stats;
        document->external_hash = loaded.hash;
        document->saved_hash = loaded.hash;
        document->external_exists = true;
        document->revision = 1;
        document->saved_revision = 1;

        if (preserve_old)
        {
            u32 old_index = ide_workspace_find_identity((IdeDocumentWorkspace*)old_workspace, document->identity);
            if (old_index != IDE_DOCUMENT_INDEX_INVALID)
            {
                IdeDocument old_copy = old_workspace->documents[old_index];
                document->view = old_copy.view;
                document->search.query = ide_string_copy(arena, old_copy.search.query);
                document->search.replacement = ide_string_copy(arena, old_copy.search.replacement);
                document->search.match_count = old_copy.search.match_count;
                document->search.case_sensitive = old_copy.search.case_sensitive;
                document->search.whole_word = old_copy.search.whole_word;
                document->search.regular_expression = old_copy.search.regular_expression;
                document->compile.status = old_copy.compile.status;
                document->compile.compiled_revision = old_copy.compile.compiled_revision;
                document->compile.artifact_hash = old_copy.compile.artifact_hash;
                document->compile.artifact_path = ide_string_copy(arena, old_copy.compile.artifact_path);
                document->compile.command_line = ide_string_copy(arena, old_copy.compile.command_line);
                document->compile.message = ide_string_copy(arena, old_copy.compile.message);
                document->is_open = old_copy.is_open;
                document->open_order = old_copy.open_order;
                document->revision = old_copy.revision;
                document->saved_revision = old_copy.saved_revision;
                if (old_copy.dirty)
                {
                    document->source = ide_string_copy(arena, old_copy.source);
                    document->saved_source = ide_string_copy(arena, old_copy.saved_source);
                    document->saved_hash = old_copy.saved_hash;
                    document->external_stats = loaded.stats;
                    document->external_hash = loaded.hash;
                    document->external_exists = true;
                    document->external_modified = !string_equal(loaded.source, document->saved_source);
                    document->dirty = !string_equal(document->source, document->saved_source);
                }
                else
                {
                    bool source_changed = !string_equal(old_copy.source, loaded.source);
                    document->revision = old_copy.revision + source_changed;
                    document->saved_revision = document->revision;
                    document->saved_source = ide_string_copy(arena, loaded.source);
                    document->saved_hash = loaded.hash;
                    document->dirty = false;
                    document->external_modified = false;
                    if (source_changed)
                    {
                        document->compile.status = IDE_DOCUMENT_COMPILE_STALE;
                    }
                }
            }
        }
        document_index += 1;
    }

    if (preserve_old)
    {
        for (u32 old_index = 0; old_index < old_workspace->document_count; old_index += 1)
        {
            IdeDocument* old_document = old_workspace->documents + old_index;
            if (!old_document->is_open && !old_document->dirty)
            {
                continue;
            }
            if (ide_workspace_find_identity(result, old_document->identity) != IDE_DOCUMENT_INDEX_INVALID)
            {
                continue;
            }
            IdeDocument* orphan = result->documents + document_index;
            ide_document_copy_to_arena(arena, orphan, old_document);
            orphan->external_exists = false;
            orphan->external_modified = true;
            document_index += 1;
        }
    }
    for (u32 index = 0; index < document_count; index += 1)
    {
        ide_document_clamp_view(result->documents + index);
    }
    BUSTER_CHECK(document_index == document_count);
    ide_documents_sort(result->documents, document_count);

    IdeDocumentErrorKind analysis_error = ide_rebuild_workspace_analysis(arena, expression_arena, result, max_diagnostics);
    if (analysis_error != IDE_DOCUMENT_ERROR_NONE)
    {
        return analysis_error;
    }

    result->filter = preserve_old ? old_workspace->filter : (IdeDocumentWorkspaceFilterState){0};
    if (preserve_old)
    {
        result->filter.query = ide_string_copy(arena, old_workspace->filter.query);
        result->next_open_order = old_workspace->next_open_order;
    }
    if (!result->next_open_order)
    {
        result->next_open_order = 1;
    }
    result->open_document_count = 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        result->open_document_count += result->documents[index].is_open;
    }

    if (preserve_old)
    {
        String8 active_identity = {0};
        if (old_workspace->active_document_index != IDE_DOCUMENT_INDEX_INVALID && old_workspace->active_document_index < old_workspace->document_count)
        {
            active_identity = old_workspace->documents[old_workspace->active_document_index].identity;
        }
        result->active_document_index = ide_workspace_find_identity(result, active_identity);
        if (result->active_document_index != IDE_DOCUMENT_INDEX_INVALID && !result->documents[result->active_document_index].is_open)
        {
            result->active_document_index = IDE_DOCUMENT_INDEX_INVALID;
        }
        if (result->active_document_index == IDE_DOCUMENT_INDEX_INVALID)
        {
            u64 best_order = UINT64_MAX;
            for (u32 index = 0; index < document_count; index += 1)
            {
                if (result->documents[index].is_open && result->documents[index].open_order < best_order)
                {
                    best_order = result->documents[index].open_order;
                    result->active_document_index = index;
                }
            }
        }
    }
    else
    {
        u32 startup_index = IDE_DOCUMENT_INDEX_INVALID;
        if (canonical_open.length)
        {
            String8 startup_identity = ide_document_path_identity(arena, canonical_open);
            u32 startup_match_count = 0;
            for (u32 index = 0; index < document_count; index += 1)
            {
                if (ide_identity_equal(result->documents[index].identity, startup_identity))
                {
                    startup_index = index;
                    startup_match_count += 1;
                }
            }
            if (startup_match_count != 1)
            {
                return IDE_DOCUMENT_ERROR_OPEN_PATH_NOT_SUPPORTED;
            }
        }
        else if (document_count)
        {
            startup_index = 0;
        }
        if (startup_index != IDE_DOCUMENT_INDEX_INVALID)
        {
            result->documents[startup_index].is_open = true;
            result->documents[startup_index].open_order = result->next_open_order;
            result->next_open_order += 1;
            result->open_document_count = 1;
            result->active_document_index = startup_index;
        }
    }
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL void ide_model_reset_staging(IdeDocumentModel* model)
{
    arena_reset_to_start(model->staging_arena);
}

BUSTER_GLOBAL_LOCAL void ide_model_commit(IdeDocumentModel* model, IdeDocumentWorkspace workspace)
{
    Arena* old_active = model->active_arena;
    model->workspace = workspace;
    model->active_arena = model->staging_arena;
    model->staging_arena = old_active;
    arena_reset_to_start(model->staging_arena);
}

BUSTER_GLOBAL_LOCAL u32 ide_active_previous_or_first(IdeDocumentWorkspace* workspace, u64 closed_order)
{
    u32 result = IDE_DOCUMENT_INDEX_INVALID;
    u64 previous_order = 0;
    u64 first_order = UINT64_MAX;
    u32 first_index = IDE_DOCUMENT_INDEX_INVALID;
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        IdeDocument* document = workspace->documents + index;
        if (!document->is_open)
        {
            continue;
        }
        if (document->open_order < closed_order && document->open_order > previous_order)
        {
            previous_order = document->open_order;
            result = index;
        }
        if (document->open_order < first_order)
        {
            first_order = document->open_order;
            first_index = index;
        }
    }
    return result != IDE_DOCUMENT_INDEX_INVALID ? result : first_index;
}

BUSTER_GLOBAL_LOCAL void ide_document_compile_mark_stale(IdeDocument* document)
{
    document->compile.status = IDE_DOCUMENT_COMPILE_STALE;
}

String8 ide_document_error_kind_name(IdeDocumentErrorKind kind)
{
    String8 names[] = {
        S8("none"),
        S8("invalid argument"),
        S8("workspace root was not found"),
        S8("workspace root is not a directory"),
        S8("path was not found"),
        S8("path is outside the workspace root"),
        S8("path is not a regular file"),
        S8("file read failed"),
        S8("file write failed"),
        S8("external modification conflicts with the saved document"),
        S8("explicit open path is not a discovered .bbb document"),
        S8("document model is already initialized"),
        S8("workspace traversal limit exceeded"),
        S8("document was not found"),
        S8("document is not open"),
        S8("an active document is required"),
        S8("reload would discard dirty edits"),
        S8("selection is outside the source"),
        S8("diagnostic limit exceeded"),
    };
    return kind < IDE_DOCUMENT_ERROR_COUNT ? names[kind] : S8("unknown document error");
}

IdeDocumentErrorKind ide_document_model_initialize(IdeDocumentModel* model, Arena* arena, Arena* staging_arena, IdeDocumentModelOptions options)
{
    if (!model)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (model->initialized)
    {
        return IDE_DOCUMENT_ERROR_ALREADY_INITIALIZED;
    }
    if (!arena || !staging_arena || arena == staging_arena)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    memset(model, 0, sizeof(*model));
    model->active_arena = arena;
    model->staging_arena = staging_arena;
    if (options.expression_arena)
    {
        if (options.expression_arena == arena || options.expression_arena == staging_arena)
        {
            return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
        }
        model->expression_arena = options.expression_arena;
    }
    else
    {
        model->expression_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
        if (!model->expression_arena)
        {
            return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
        }
        model->owns_expression_arena = true;
    }
    model->max_discovered_files = options.max_discovered_files ? options.max_discovered_files : IDE_DOCUMENT_DEFAULT_MAX_DISCOVERED_FILES;
    model->max_traversal_entries = options.max_traversal_entries ? options.max_traversal_entries : IDE_DOCUMENT_DEFAULT_MAX_TRAVERSAL_ENTRIES;
    model->max_diagnostics = options.max_diagnostics ? options.max_diagnostics : IDE_DOCUMENT_DEFAULT_MAX_DIAGNOSTICS;
    ide_workspace_initialize_empty(&model->workspace);
    arena_reset_to_start(model->staging_arena);
    IdeDocumentWorkspace workspace = {0};
    IdeDocumentErrorKind error = ide_build_workspace(model->staging_arena, model->expression_arena, options.workspace_root, options.open_path, 0, false,
                                                      model->max_discovered_files, model->max_traversal_entries, model->max_diagnostics, &workspace);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        arena_reset_to_start(model->staging_arena);
        if (model->owns_expression_arena)
        {
            arena_destroy(model->expression_arena, 1);
            model->expression_arena = 0;
            model->owns_expression_arena = false;
        }
        return error;
    }
    model->initialized = true;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

void ide_document_model_deinitialize(IdeDocumentModel* model)
{
    if (!model)
    {
        return;
    }
    Arena* expression_arena = model->expression_arena;
    bool owns_expression_arena = model->owns_expression_arena;
    if (model->active_arena)
    {
        arena_reset_to_start(model->active_arena);
    }
    if (model->staging_arena)
    {
        arena_reset_to_start(model->staging_arena);
    }
    if (owns_expression_arena && expression_arena)
    {
        arena_destroy(expression_arena, 1);
    }
    memset(model, 0, sizeof(*model));
}

IdeDocumentErrorKind ide_document_model_refresh_workspace(IdeDocumentModel* model)
{
    if (!model || !model->initialized)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    IdeDocumentErrorKind error = ide_build_workspace(model->staging_arena, model->expression_arena, model->workspace.root_path, (String8){0},
                                                      &model->workspace, true,
                                                      model->max_discovered_files, model->max_traversal_entries, model->max_diagnostics, &workspace);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return error;
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_open(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = ide_model_path_error(model->staging_arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    bool needs_analysis_rebuild = !document->external_exists;
    if (!document->external_exists && !document->dirty)
    {
        IdeLoadedFile loaded = {0};
        IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
        if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
        {
            ide_model_reset_staging(model);
            return error;
        }
        document->source = loaded.source;
        document->saved_source = ide_string_copy(model->staging_arena, loaded.source);
        document->external_stats = loaded.stats;
        document->external_hash = loaded.hash;
        document->saved_hash = loaded.hash;
        document->external_exists = true;
        document->external_modified = false;
        document->revision += 1;
        document->saved_revision = document->revision;
        document->dirty = false;
        ide_document_clamp_view(document);
    }
    if (!document->is_open)
    {
        document->is_open = true;
        document->open_order = workspace.next_open_order;
        workspace.next_open_order += 1;
        workspace.open_document_count += 1;
    }
    workspace.active_document_index = index;
    if (needs_analysis_rebuild)
    {
        IdeDocumentErrorKind error = ide_rebuild_workspace_analysis(model->staging_arena, model->expression_arena, &workspace, model->max_diagnostics);
        if (error != IDE_DOCUMENT_ERROR_NONE)
        {
            ide_model_reset_staging(model);
            return error;
        }
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_close(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = ide_model_path_error(model->staging_arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (!document->is_open)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    u64 closed_order = document->open_order;
    bool was_active = workspace.active_document_index == index;
    document->is_open = false;
    workspace.open_document_count -= 1;
    if (was_active)
    {
        workspace.active_document_index = ide_active_previous_or_first(&workspace, closed_order);
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_active(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = ide_model_path_error(model->staging_arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    if (!workspace.documents[index].is_open)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    workspace.active_document_index = index;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_text(IdeDocumentModel* model, String8 path, String8 source)
{
    if (!model || !model->initialized || (!source.pointer && source.length))
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = ide_model_path_error(model->staging_arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (!document->is_open)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    if (!string_equal(document->source, source))
    {
        document->source = ide_string_copy(model->staging_arena, source);
        ide_document_clamp_view(document);
        document->revision += 1;
        document->dirty = !string_equal(document->source, document->saved_source);
        ide_document_compile_mark_stale(document);
        IdeDocumentErrorKind error = ide_rebuild_workspace_analysis(model->staging_arena, model->expression_arena, &workspace, model->max_diagnostics);
        if (error != IDE_DOCUMENT_ERROR_NONE)
        {
            ide_model_reset_staging(model);
            return error;
        }
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_reload(IdeDocumentModel* model, String8 path, IdeDocumentReloadMode mode)
{
    if (!model || !model->initialized || !path.length || mode >= IDE_DOCUMENT_RELOAD_MODE_COUNT)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = path.length ? ide_model_path_error(model->staging_arena, model->workspace.root_path, path) : IDE_DOCUMENT_ERROR_NONE;
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    IdeLoadedFile loaded = {0};
    IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
    if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
    {
        ide_model_reset_staging(model);
        return error;
    }
    if (document->dirty && mode == IDE_DOCUMENT_RELOAD_REJECT_DIRTY)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DIRTY_RELOAD_CONFLICT;
    }
    document->source = loaded.source;
    document->external_stats = loaded.stats;
    document->external_hash = loaded.hash;
    document->saved_source = ide_string_copy(model->staging_arena, loaded.source);
    document->saved_hash = loaded.hash;
    document->external_exists = true;
    document->external_modified = false;
    document->revision += 1;
    document->saved_revision = document->revision;
    document->dirty = false;
    ide_document_clamp_view(document);
    ide_document_compile_mark_stale(document);
    error = ide_rebuild_workspace_analysis(model->staging_arena, model->expression_arena, &workspace, model->max_diagnostics);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return error;
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_poll_external(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = path.length ? ide_model_path_error(model->staging_arena, model->workspace.root_path, path) : IDE_DOCUMENT_ERROR_NONE;
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 first = 0;
    u32 last = workspace.document_count;
    if (path.length)
    {
        first = ide_workspace_find_path(model->staging_arena, &workspace, path);
        if (first == IDE_DOCUMENT_INDEX_INVALID)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
        }
        last = first + 1;
    }
    for (u32 index = first; index < last; index += 1)
    {
        IdeDocument* document = workspace.documents + index;
        IdeLoadedFile loaded = {0};
        IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
        if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
        {
            if (error == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND)
            {
                document->external_exists = false;
                document->external_modified = true;
                continue;
            }
            ide_model_reset_staging(model);
            return error;
        }
        document->external_exists = true;
        document->external_stats = loaded.stats;
        document->external_hash = loaded.hash;
        document->external_modified = !string_equal(loaded.source, document->saved_source);
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_save(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    TemporalArena scratch = scratch_begin(0, 0);
    IdeDocumentErrorKind path_error = ide_model_path_error(scratch.arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        scratch_end(scratch);
        return path_error;
    }
    u32 index = ide_workspace_find_path(scratch.arena, &model->workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* current = model->workspace.documents + index;
    if (!current->is_open)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    if (!ide_document_path_is_within(model->workspace.root_path, current->path) ||
        ide_path_has_link_component(scratch.arena, model->workspace.root_path, current->path))
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
    }

    IdeLoadedFile current_disk = {0};
    IdeDocumentErrorKind disk_error = IDE_DOCUMENT_ERROR_NONE;
    if (!ide_file_read(scratch.arena, model->workspace.root_path, current->path, &current_disk, &disk_error))
    {
        scratch_end(scratch);
        return disk_error == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND ? IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT : disk_error;
    }
    if (!ide_loaded_file_matches(current_disk.source, current_disk.hash, current->saved_source, current->saved_hash))
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT;
    }

    bool write_result = ide_atomic_write_file(scratch.arena, current->path, current->source);
    scratch_end(scratch);
    if (!write_result)
    {
        return IDE_DOCUMENT_ERROR_FILE_WRITE;
    }

    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    IdeDocument* document = workspace.documents + index;
    IdeLoadedFile loaded = {0};
    IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
    if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
    {
        ide_model_reset_staging(model);
        return error;
    }
    u64 source_hash = buster_hash_64((u8*)document->source.pointer, document->source.length);
    if (!ide_loaded_file_matches(loaded.source, loaded.hash, document->source, source_hash))
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT;
    }
    document->external_stats = loaded.stats;
    document->external_hash = loaded.hash;
    document->external_exists = true;
    document->external_modified = false;
    document->saved_source = ide_string_copy(model->staging_arena, document->source);
    document->saved_hash = source_hash;
    document->saved_revision = document->revision;
    document->dirty = false;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_TEST_F_DECL void ide_document_model_test_set_save_replace_failure(bool enabled)
{
    ide_test_force_save_replace_failure = enabled;
}
#endif

IdeDocumentErrorKind ide_document_model_set_view(IdeDocumentModel* model, String8 path, IdeDocumentViewState view)
{
    if (!model || !model->initialized || !path.length || view.selection_start > view.selection_end)
    {
        return view.selection_start > view.selection_end ? IDE_DOCUMENT_ERROR_INVALID_SELECTION : IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (!document->is_open)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    if (view.cursor_offset > document->source.length || view.selection_end > document->source.length)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_INVALID_SELECTION;
    }
    document->view = view;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_compile_metadata(IdeDocumentModel* model, String8 path, IdeDocumentCompileMetadata metadata)
{
    if (!model || !model->initialized || !path.length || metadata.status >= IDE_DOCUMENT_COMPILE_STATUS_COUNT)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocumentCompileMetadata* destination = &workspace.documents[index].compile;
    destination->status = metadata.status;
    destination->compiled_revision = metadata.compiled_revision;
    destination->artifact_hash = metadata.artifact_hash;
    destination->artifact_path = ide_string_copy(model->staging_arena, metadata.artifact_path);
    destination->command_line = ide_string_copy(model->staging_arena, metadata.command_line);
    destination->message = ide_string_copy(model->staging_arena, metadata.message);
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_search_state(IdeDocumentModel* model, String8 path, IdeDocumentSearchState search)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocumentSearchState* destination = &workspace.documents[index].search;
    *destination = search;
    destination->query = ide_string_copy(model->staging_arena, search.query);
    destination->replacement = ide_string_copy(model->staging_arena, search.replacement);
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_filter_state(IdeDocumentModel* model, IdeDocumentWorkspaceFilterState filter)
{
    if (!model || !model->initialized)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    workspace.filter = filter;
    workspace.filter.query = ide_string_copy(model->staging_arena, filter.query);
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_replace_diagnostics(IdeDocumentModel* model, const IdeDocumentDiagnosticInput* inputs, u32 input_count)
{
    if (!model || !model->initialized || (input_count && !inputs))
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (input_count > model->max_diagnostics)
    {
        return IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32* counts = workspace.document_count ? arena_allocate(model->staging_arena, u32, workspace.document_count) : 0;
    if (workspace.document_count)
    {
        if (!counts)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        memset(counts, 0, sizeof(*counts) * workspace.document_count);
    }
    for (u32 input_index = 0; input_index < input_count; input_index += 1)
    {
        const IdeDocumentDiagnosticInput* input = inputs + input_index;
        if (input->severity >= IDE_DOCUMENT_DIAGNOSTIC_SEVERITY_COUNT || input->source >= IDE_DOCUMENT_DIAGNOSTIC_SOURCE_COUNT)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
        }
        u32 document_index = workspace.active_document_index;
        if (input->file_path.length)
        {
            document_index = ide_workspace_find_path(model->staging_arena, &workspace, input->file_path);
        }
        if (document_index == IDE_DOCUMENT_INDEX_INVALID)
        {
            ide_model_reset_staging(model);
            return input->file_path.length ? IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND : IDE_DOCUMENT_ERROR_ACTIVE_DOCUMENT_REQUIRED;
        }
        if (!counts)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        if (document_index >= workspace.document_count)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
        }
        counts[document_index] += 1;
    }
    for (u32 index = 0; index < workspace.document_count; index += 1)
    {
        workspace.documents[index].diagnostics = counts[index] ? arena_allocate(model->staging_arena, IdeDocumentDiagnostic, counts[index]) : 0;
        workspace.documents[index].diagnostic_count = counts[index];
        counts[index] = 0;
    }
    for (u32 input_index = 0; input_index < input_count; input_index += 1)
    {
        const IdeDocumentDiagnosticInput* input = inputs + input_index;
        u32 document_index = workspace.active_document_index;
        if (input->file_path.length)
        {
            document_index = ide_workspace_find_path(model->staging_arena, &workspace, input->file_path);
        }
        IdeDocument* document = workspace.documents + document_index;
        IdeDocumentDiagnostic* diagnostic = document->diagnostics + counts[document_index];
        *diagnostic = (IdeDocumentDiagnostic){
            .file_path = document->path,
            .range = input->range,
            .message = ide_string_copy(model->staging_arena, input->message),
            .identity = input->identity,
            .severity = input->severity,
            .source = input->source,
        };
        if (!diagnostic->identity)
        {
            diagnostic->identity = ide_diagnostic_identity(diagnostic->file_path, diagnostic->range, diagnostic->message, diagnostic->severity,
                                                           diagnostic->source);
        }
        counts[document_index] += 1;
    }
    for (u32 index = 0; index < workspace.document_count; index += 1)
    {
        ide_diagnostics_sort(workspace.documents + index);
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocument* ide_document_model_find(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return 0;
    }
    TemporalArena scratch = scratch_begin(0, 0);
    u32 index = ide_workspace_find_path(scratch.arena, &model->workspace, path);
    scratch_end(scratch);
    return index == IDE_DOCUMENT_INDEX_INVALID ? 0 : model->workspace.documents + index;
}

IdeDocument* ide_document_model_document_at(IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized || index >= model->workspace.document_count)
    {
        return 0;
    }
    return model->workspace.documents + index;
}

IdeDocument* ide_document_model_active_document(IdeDocumentModel* model)
{
    if (!model || !model->initialized || model->workspace.active_document_index == IDE_DOCUMENT_INDEX_INVALID)
    {
        return 0;
    }
    return ide_document_model_document_at(model, model->workspace.active_document_index);
}

IdeDocumentImport* ide_document_model_import_at(IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized || index >= model->workspace.import_count)
    {
        return 0;
    }
    return model->workspace.imports + index;
}

u32 ide_document_model_document_count(IdeDocumentModel* model)
{
    return model && model->initialized ? model->workspace.document_count : 0;
}

u32 ide_document_model_open_document_count(IdeDocumentModel* model)
{
    return model && model->initialized ? model->workspace.open_document_count : 0;
}
