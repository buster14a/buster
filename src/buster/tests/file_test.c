#include <buster/tests/file_test.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/os_internal.h>
#if BUSTER_INCLUDE_TESTS

// file_copy regression helpers. Each copy fixture owns one directory under the
// test root, so listing it finds leftover staging files without racing other
// fixtures. Filesystem state is checked with native calls, not the OS layer's
// replacement primitives under test.

BUSTER_GLOBAL_LOCAL String8 file_test_child(Arena* arena, String8 directory, String8 name)
{
    return string_format_z(arena, S8("{S8}/{S8}"), directory, name);
}

BUSTER_GLOBAL_LOCAL bool file_test_directory_reset(String8 directory)
{
    return directory.length && os_directory_delete(directory) && os_make_directory_attempt(directory);
}

BUSTER_GLOBAL_LOCAL bool file_test_bytes_are(Arena* arena, String8 path, ByteSlice expected)
{
    u64 mark = arena->position;
    FileReadResult read = file_read_checked(arena, path, (FileReadOptions){0});
    bool result = read.status == OS_FILE_READ_OK && read.bytes.pointer && read.bytes.length == expected.length;
    // memcmp's pointers must be valid even for a zero count.
    if (result && expected.length)
    {
        result = memory_compare(read.bytes.pointer, expected.pointer, expected.length);
    }
    arena_set_position(arena, mark);
    return result;
}

BUSTER_GLOBAL_LOCAL bool file_test_path_missing(Arena* arena, String8 path)
{
    bool result;
#if BUSTER_WINDOWS
    String16 path_w = string16_from_string8(arena, path, true);
    DWORD attributes = GetFileAttributesW(path_w.pointer);
    DWORD error = GetLastError();
    result = attributes == INVALID_FILE_ATTRIBUTES && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
#else
    BUSTER_UNUSED(arena);
    struct stat stats;
    result = lstat((const char*)path.pointer, &stats) != 0 && errno == ENOENT;
#endif
    return result;
}

// Whether `path` is still a link entry; POSIX also checks its stored target.
BUSTER_GLOBAL_LOCAL bool file_test_is_link(Arena* arena, String8 path, String8 target)
{
    bool result;
#if BUSTER_WINDOWS
    BUSTER_UNUSED(target);
    String16 path_w = string16_from_string8(arena, path, true);
    DWORD attributes = GetFileAttributesW(path_w.pointer);
    result = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    BUSTER_UNUSED(arena);
    char buffer[4096];
    ssize_t length = readlink((const char*)path.pointer, buffer, sizeof(buffer));
    result = length > 0 && (u64)length == target.length && memory_compare(buffer, target.pointer, target.length);
#endif
    return result;
}

typedef enum FileTestLink
{
    FILE_TEST_LINK_CREATED,
    FILE_TEST_LINK_UNSUPPORTED,
    FILE_TEST_LINK_FAILED,
} FileTestLink;

// Creates `link_name` in `directory` naming `target_name` there; symbolic links
// store the relative name. Only a missing Windows symbolic-link privilege and
// Android storage refusing links count as unsupported, and both are reported.
BUSTER_GLOBAL_LOCAL FileTestLink file_test_link(UnitTestArguments* arguments, bool symbolic, String8 directory, String8 target_name, String8 link_name)
{
    Arena* arena = arguments->arena;
    String8 target = file_test_child(arena, directory, target_name);
    String8 path = file_test_child(arena, directory, link_name);
    bool created;
    u32 error;
    bool unsupported;
#if BUSTER_WINDOWS
    String16 path_w = string16_from_string8(arena, path, true);
    if (symbolic)
    {
        String16 target_name_w = string16_from_string8(arena, target_name, true);
        // 0x2 is SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE; releases that
        // predate it reject the flag, so retry without it.
        created = CreateSymbolicLinkW(path_w.pointer, target_name_w.pointer, 0x2) != 0;
        error = created ? 0 : (u32)GetLastError();
        if (error == (u32)ERROR_INVALID_PARAMETER)
        {
            created = CreateSymbolicLinkW(path_w.pointer, target_name_w.pointer, 0) != 0;
            error = created ? 0 : (u32)GetLastError();
        }
    }
    else
    {
        String16 target_w = string16_from_string8(arena, target, true);
        created = CreateHardLinkW(path_w.pointer, target_w.pointer, 0) != 0;
        error = created ? 0 : (u32)GetLastError();
    }
    unsupported = symbolic && error == (u32)ERROR_PRIVILEGE_NOT_HELD;
#else
    if (symbolic)
    {
        created = symlink((const char*)target_name.pointer, (const char*)path.pointer) == 0;
    }
    else
    {
        created = link((const char*)target.pointer, (const char*)path.pointer) == 0;
    }
    error = created ? 0 : (u32)errno;
    unsupported = BUSTER_ANDROID && (error == (u32)EACCES || error == (u32)EPERM);
#endif
    FileTestLink result = created ? FILE_TEST_LINK_CREATED : (unsupported ? FILE_TEST_LINK_UNSUPPORTED : FILE_TEST_LINK_FAILED);
    if (!created)
    {
        arguments->show(arguments, S8("FILE_COPY_LINK kind={S8} status={S8} error={u32}\n"), symbolic ? S8("symbolic") : S8("hard"),
                        unsupported ? S8("unsupported") : S8("failed"), error);
    }
    return result;
}

#define FILE_TEST_ENTRY_CAPACITY 16

typedef struct FileTestEntries FileTestEntries;
struct FileTestEntries
{
    String8 names[FILE_TEST_ENTRY_CAPACITY];
    u32 count;
    bool valid;
};

BUSTER_GLOBAL_LOCAL void file_test_entries_add(Arena* arena, FileTestEntries* entries, String8 name)
{
    if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
    {
        if (entries->count < FILE_TEST_ENTRY_CAPACITY)
        {
            entries->names[entries->count] = string_duplicate_arena(arena, name, false);
            entries->count += 1;
        }
        else
        {
            entries->valid = false;
        }
    }
}

BUSTER_GLOBAL_LOCAL FileTestEntries file_test_entries(Arena* arena, String8 directory)
{
    FileTestEntries result = {0};
#if BUSTER_WINDOWS
    String16 pattern = string16_from_string8(arena, string_format_z(arena, S8("{S8}\\*"), directory), true);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern.pointer, &data);
    result.valid = find != INVALID_HANDLE_VALUE;
    bool more = result.valid;
    while (more)
    {
        u64 length = 0;
        while (data.cFileName[length])
        {
            length += 1;
        }
        String16 name = {.pointer = (char16*)data.cFileName, .length = length};
        file_test_entries_add(arena, &result, string8_from_string16(arena, name, false));
        more = FindNextFileW(find, &data) != 0;
    }
    if (find != INVALID_HANDLE_VALUE)
    {
        result.valid = GetLastError() == ERROR_NO_MORE_FILES && result.valid;
        FindClose(find);
    }
#else
    DIR* handle = opendir((const char*)directory.pointer);
    result.valid = handle != 0;
    bool more = result.valid;
    while (more)
    {
        errno = 0;
        struct dirent* entry = readdir(handle);
        more = entry != 0;
        if (entry)
        {
            file_test_entries_add(arena, &result, string_from_pointer((const char8*)entry->d_name));
        }
        else
        {
            result.valid = errno == 0 && result.valid;
        }
    }
    if (handle)
    {
        closedir(handle);
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool file_test_is_staging_name(String8 name)
{
    return string_starts_with_sequence(name, OS_FILE_STAGING_PREFIX) && string_ends_with_sequence(name, OS_FILE_STAGING_SUFFIX);
}

// Whether the directory holds exactly `expected`, in any order, plus
// `staging_count` staging files.
BUSTER_GLOBAL_LOCAL bool file_test_entries_are(Arena* arena, String8 directory, String8* expected, u32 expected_count, u32 staging_count)
{
    u64 mark = arena->position;
    FileTestEntries entries = file_test_entries(arena, directory);
    u32 matched = 0;
    u32 staged = 0;
    for (u32 index = 0; index < entries.count; index += 1)
    {
        bool known = false;
        for (u32 expected_index = 0; expected_index < expected_count; expected_index += 1)
        {
            known |= string_equal(entries.names[index], expected[expected_index]);
        }
        if (known)
        {
            matched += 1;
        }
        else if (file_test_is_staging_name(entries.names[index]))
        {
            staged += 1;
        }
    }
    arena_set_position(arena, mark);
    return entries.valid && matched == expected_count && staged == staging_count && entries.count == expected_count + staging_count;
}

// Deletes staging files a fault deliberately left behind; returns how many.
BUSTER_GLOBAL_LOCAL u32 file_test_remove_staging(Arena* arena, String8 directory)
{
    u64 mark = arena->position;
    FileTestEntries entries = file_test_entries(arena, directory);
    u32 result = 0;
    for (u32 index = 0; index < entries.count; index += 1)
    {
        if (file_test_is_staging_name(entries.names[index]) && os_file_delete(file_test_child(arena, directory, entries.names[index])))
        {
            result += 1;
        }
    }
    arena_set_position(arena, mark);
    return result;
}

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
        if (BUSTER_REQUIRE(arguments, actual.length == transferred[index]))
        {
            if (actual.length)
                BUSTER_TEST(arguments, memory_compare(actual.pointer, data, actual.length));
        }
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

    // file_copy includes completion of its staging file: a close failure
    // leaves the existing empty destination unpublished and unchanged.
    BUSTER_TEST(arguments, file_write(other, content));
    os_file_test_begin(path, &close_step, 1);
    FileCopyResult close_failed = file_copy_checked((CopyFileArguments){.original_path = other, .new_path = path});
    BUSTER_TEST(arguments, os_file_test_end() == 1);
    BUSTER_TEST(arguments, close_failed.status == FILE_COPY_FAILED && close_failed.error.v == 23456 && !close_failed.cleanup_error.v);
    BUSTER_TEST(arguments, file_test_bytes_are(arguments->arena, path, (ByteSlice){0}));
    os_file_test_begin(path, &close_step, 1);
    BUSTER_TEST(arguments, !file_copy((CopyFileArguments){.original_path = other, .new_path = path}));
    BUSTER_TEST(arguments, os_file_test_end() == 1);
    BUSTER_TEST(arguments, os_file_delete(path));
    BUSTER_TEST(arguments, os_file_delete(other));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult file_test_read_failures(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 path = buster_test_temporary_path(arguments->arena, S8("file-read-fault"), S8(".bin"));
    String8 text = S8("0123456789abcdefghijklmnopqrstuvwxyz");
    BUSTER_TEST(arguments, file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text)));
    u8 buffer[64];
    memset(buffer, 0xA5, sizeof(buffer));
    OsFileReadResult empty = os_file_read_some(0, (ByteSlice){0});
    BUSTER_TEST(arguments, empty.status == OS_FILE_READ_OK && !empty.transferred && !empty.error.v);
    BUSTER_TEST(arguments, os_file_read_exact(0, (ByteSlice){0}).status == OS_FILE_READ_OK);
    BUSTER_TEST(arguments, os_file_read_some(0, (ByteSlice){buffer, 1}).status == OS_FILE_READ_ERROR);
    BUSTER_TEST(arguments, os_file_read_exact(0, (ByteSlice){buffer, 1}).status == OS_FILE_READ_ERROR);
    FileStats invalid = os_file_get_stats(0, (FileStatsOptions){.size = 1});
    BUSTER_TEST(arguments, !invalid.valid && invalid.error.v != 0);
    BUSTER_TEST(arguments, os_file_get_size(0) == UINT64_MAX);

    OsFileTestStep prefix[] = {{OS_FILE_TEST_READ, OS_FILE_TEST_LIMIT, 7}, {OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345}};
    for (u32 exact = 0; exact < 2; exact += 1)
    {
        os_file_test_begin(path, prefix, 2);
        OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        BUSTER_TEST(arguments, file != 0);
        if (file)
        {
            OsFileReadResult read;
            if (exact) read = os_file_read_exact(file, (ByteSlice){buffer, text.length});
            else
            {
                read = os_file_read_some(file, (ByteSlice){buffer, text.length});
                BUSTER_TEST(arguments, read.status == OS_FILE_READ_OK && read.transferred == 7 && !read.error.v);
                BUSTER_TEST(arguments, memory_compare(buffer, text.pointer, 7));
                read = os_file_read_some(file, (ByteSlice){buffer + 7, text.length - 7});
            }
            BUSTER_TEST(arguments, read.status == OS_FILE_READ_ERROR && read.error.v == 12345);
            BUSTER_TEST(arguments, read.transferred == (exact ? 7u : 0u));
            BUSTER_TEST(arguments, memory_compare(buffer, text.pointer, 7));
            BUSTER_TEST(arguments, os_file_close(file));
        }
        BUSTER_TEST(arguments, os_file_test_end() == 2);
    }
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
    BUSTER_TEST(arguments, file != 0);
    if (file)
    {
        OsFileReadResult read = os_file_read_exact(file, (ByteSlice){buffer, sizeof(buffer)});
        BUSTER_TEST(arguments, read.status == OS_FILE_READ_EOF && read.transferred == text.length && !read.error.v);
        BUSTER_TEST(arguments, memory_compare(buffer, text.pointer, text.length));
        read = os_file_read_some(file, (ByteSlice){buffer, sizeof(buffer)});
        BUSTER_TEST(arguments, read.status == OS_FILE_READ_EOF && !read.transferred && !read.error.v);
        BUSTER_TEST(arguments, os_file_close(file));
    }
    // Sizes smaller/larger than the actual descriptor model growth/truncation
    // immediately after the size snapshot, independent of thread scheduling.
    OsFileTestStep scripts[][4] = {
        {{OS_FILE_TEST_OPEN, OS_FILE_TEST_ERROR, 12345}},
        {{OS_FILE_TEST_STATS, OS_FILE_TEST_ERROR, 12345}},
        {{OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345}},
        {{OS_FILE_TEST_READ, OS_FILE_TEST_LIMIT, 7}, {OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345},
         {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
        {{OS_FILE_TEST_READ, OS_FILE_TEST_ZERO, 0}},
        {{OS_FILE_TEST_STATS, OS_FILE_TEST_SIZE, 100}},
        {{OS_FILE_TEST_STATS, OS_FILE_TEST_SIZE, 7}},
        {{OS_FILE_TEST_READ, OS_FILE_TEST_INTERRUPT, 0}, {OS_FILE_TEST_READ, OS_FILE_TEST_LIMIT, 7},
         {OS_FILE_TEST_READ, OS_FILE_TEST_INTERRUPT, 0}},
        {{OS_FILE_TEST_STATS, OS_FILE_TEST_SIZE, 0}, {OS_FILE_TEST_READ, OS_FILE_TEST_LIMIT, 1},
         {OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345}},
        {{OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
    };
    u32 counts[] = {1, 1, 1, 3, 1, 1, 1, 3, 3, 1};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(scripts); index += 1)
    {
        bool success = index == 6 || index == 7;
        u64 mark = arguments->arena->position;
        os_file_test_begin(path, scripts[index], counts[index]);
        FileReadResult read = file_read_checked(arguments->arena, path, (FileReadOptions){.start_padding = 3, .end_padding = 5});
        BUSTER_TEST(arguments, os_file_test_end() == counts[index]);
        bool read_pointer_matches = (read.bytes.pointer != 0) == success;
        BUSTER_TEST(arguments, (read.status == OS_FILE_READ_OK) == success);
        if (BUSTER_REQUIRE(arguments, read_pointer_matches))
        {
            if (success)
            {
                if (BUSTER_REQUIRE(arguments, read.bytes.length == (index == 6 ? 7 : text.length)))
                {
                    BUSTER_TEST(arguments, memory_compare(read.bytes.pointer, text.pointer, read.bytes.length));
                    BUSTER_TEST(arguments, read.bytes.pointer[read.bytes.length] == 0 && read.bytes.pointer[read.bytes.length + 4] == 0);
                }
            }
            else
            {
                BUSTER_TEST(arguments, arguments->arena->position == mark && read.bytes.length == 0);
                if (index == 4 || index == 5)
                    BUSTER_TEST(arguments, read.status == OS_FILE_READ_EOF && !read.error.v);
                else
                    BUSTER_TEST(arguments, read.error.v == (index == 9 ? 23456u : 12345u));
            }
        }
        arena_set_position(arguments->arena, mark);
        os_file_test_begin(path, scripts[index], counts[index]);
        ByteSlice legacy = file_read(arguments->arena, path, (FileReadOptions){0});
        BUSTER_TEST(arguments, os_file_test_end() == counts[index]);
        BUSTER_TEST(arguments, (legacy.pointer != 0) == success);
        arena_set_position(arguments->arena, mark);
    }
    // File copying streams through the same checked read loop; a read failure
    // publishes no destination.
    String8 copy = buster_test_temporary_path(arguments->arena, S8("file-read-failed-copy"), S8(".bin"));
    BUSTER_TEST(arguments, os_file_delete(copy));
    os_file_test_begin(path, prefix, 2);
    BUSTER_TEST(arguments, !file_copy((CopyFileArguments){.original_path = path, .new_path = copy}));
    BUSTER_TEST(arguments, os_file_test_end() == 2);
    BUSTER_TEST(arguments, file_test_path_missing(arguments->arena, copy));
    BUSTER_TEST(arguments, os_file_delete(copy));
    BUSTER_TEST(arguments, file_write(path, (ByteSlice){0}));
    FileReadResult empty_file = file_read_checked(arguments->arena, path, (FileReadOptions){0});
    BUSTER_TEST(arguments, empty_file.status == OS_FILE_READ_OK && empty_file.bytes.pointer && !empty_file.bytes.length);
    BUSTER_TEST(arguments, os_file_delete(path));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult file_test_read_alignment(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 path = buster_test_temporary_path(arguments->arena, S8("file-read-alignment"), S8(".bin"));
    String8 content = S8("padded alignment read");
    BUSTER_TEST(arguments, file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(content)));

    u32 start_alignments[] = {0, 16};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(start_alignments); index += 1)
    {
        u64 mark = arguments->arena->position;
        u64 effective_alignment = start_alignments[index] ? start_alignments[index] : 1;
        FileReadOptions options = {
            .start_padding = 3,
            .start_alignment = start_alignments[index],
            .end_padding = 5,
            .end_alignment = 8,
        };
        ByteSlice bytes = file_read(arguments->arena, path, options);
        if (BUSTER_REQUIRE(arguments, bytes.pointer != 0 && bytes.length == content.length))
        {
            u64 allocation_offset = (u64)(bytes.pointer - options.start_padding - (u8*)arguments->arena);
            BUSTER_TEST(arguments, allocation_offset == align_forward(mark, effective_alignment));
            BUSTER_TEST(arguments, memory_compare(bytes.pointer, content.pointer, content.length));
            bool end_padding_zero = true;
            for (u32 padding_index = 0; padding_index < options.end_padding; padding_index += 1)
            {
                end_padding_zero &= bytes.pointer[bytes.length + padding_index] == 0;
            }
            BUSTER_TEST(arguments, end_padding_zero);
        }
        arena_set_position(arguments->arena, mark);
    }

    BUSTER_TEST(arguments, os_file_delete(path));
    return result;
}

// Published copies over absent, shorter and longer destinations; repeated
// publication; refusals; and the metadata a replacement keeps or drops.
BUSTER_GLOBAL_LOCAL UnitTestResult file_test_copy_contents(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 directory = buster_test_temporary_path(arena, S8("file-copy-contents"), S8(""));
    if (BUSTER_REQUIRE(arguments, file_test_directory_reset(directory)))
    {
        String8 names[] = {S8("source.bin"), S8("destination.bin"), S8("linked.bin"), S8("directory")};
        String8 source = file_test_child(arena, directory, names[0]);
        String8 destination = file_test_child(arena, directory, names[1]);
        // Three whole 64 KiB copy buffers and a remainder cross chunk boundaries.
        u64 large_length = BUSTER_KB(64) * 3 + 17;
        u8* large = arena_allocate(arena, u8, large_length);
        for (u64 index = 0; index < large_length; index += 1)
        {
            large[index] = (u8)(index * 131 + (index >> 11));
        }
        u64 longer_length = large_length + 100;
        u8* longer = arena_allocate(arena, u8, longer_length);
        memset(longer, 0xEE, longer_length);
        u8 binary[] = {'b', 0, 'i', 0, 0, 'n', 0xFF, 0, 0x80, 'a', 0, 0};
        u8 shorter[] = {'s'};
        ByteSlice sources[] = {{0}, {binary, sizeof(binary)}, {large, large_length}};
        ByteSlice destinations[] = {{0}, {shorter, sizeof(shorter)}, {longer, longer_length}};
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(sources); source_index += 1)
        {
            // Destination state zero is absent.
            for (u32 state = 0; state < BUSTER_ARRAY_LENGTH(destinations); state += 1)
            {
                BUSTER_TEST(arguments, file_write(source, sources[source_index]) && os_file_delete(destination));
                if (state)
                {
                    BUSTER_TEST(arguments, file_write(destination, destinations[state]));
                }
                FileCopyResult copied = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = destination});
                BUSTER_TEST(arguments, copied.status == FILE_COPY_PUBLISHED && !copied.error.v && !copied.cleanup_error.v);
                BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, sources[source_index]));
                BUSTER_TEST(arguments, file_test_bytes_are(arena, source, sources[source_index]));
                BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 2, 0));
            }
        }

        // Repeated publication over one name leaves exactly one file.
        for (u32 repeat = 0; repeat < 4; repeat += 1)
        {
            BUSTER_TEST(arguments, file_copy((CopyFileArguments){.original_path = source, .new_path = destination}));
        }
        BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, sources[2]));
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 2, 0));

        // A missing source or destination directory fails without creating anything.
        String8 missing = file_test_child(arena, directory, S8("missing.bin"));
        FileCopyResult missing_source = file_copy_checked((CopyFileArguments){.original_path = missing, .new_path = destination});
        BUSTER_TEST(arguments, missing_source.status == FILE_COPY_FAILED && missing_source.error.v != 0);
        String8 unreachable = file_test_child(arena, directory, S8("absent-directory/destination.bin"));
        FileCopyResult no_directory = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = unreachable});
        BUSTER_TEST(arguments, no_directory.status == FILE_COPY_FAILED && no_directory.error.v != 0);
        BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, sources[2]));
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 2, 0));

        // A directory destination is refused, not replaced.
        String8 subdirectory = file_test_child(arena, directory, names[3]);
        BUSTER_TEST(arguments, os_make_directory_attempt(subdirectory));
        FileCopyResult into_directory = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = subdirectory});
        BUSTER_TEST(arguments, into_directory.status == FILE_COPY_UNSUPPORTED_DESTINATION && !into_directory.cleanup_error.v);
        String8 with_directory[] = {names[0], names[1], names[3]};
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, with_directory, 3, 0));
        BUSTER_TEST(arguments, os_directory_delete(subdirectory));

#if BUSTER_WINDOWS
        // The read-only attribute is refused, as a POSIX writer's open would be.
        String16 destination_w = string16_from_string8(arena, destination, true);
        BUSTER_TEST(arguments, file_write(destination, destinations[1]) && SetFileAttributesW(destination_w.pointer, FILE_ATTRIBUTE_READONLY));
        FileCopyResult read_only = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = destination});
        BUSTER_TEST(arguments, read_only.status == FILE_COPY_FAILED && read_only.error.v == (u32)ERROR_ACCESS_DENIED);
        BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, destinations[1]));
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 2, 0));
        BUSTER_TEST(arguments, SetFileAttributesW(destination_w.pointer, FILE_ATTRIBUTE_NORMAL));
#else
        // A replaced destination keeps its permission bits; a new destination
        // takes no execute bits from the source.
        struct stat stats;
        BUSTER_TEST(arguments, chmod((const char*)destination.pointer, 0751) == 0 && chmod((const char*)source.pointer, 0700) == 0);
        BUSTER_TEST(arguments, file_copy((CopyFileArguments){.original_path = source, .new_path = destination}));
        BUSTER_TEST(arguments, stat((const char*)destination.pointer, &stats) == 0 && (stats.st_mode & 07777) == 0751);
        BUSTER_TEST(arguments, stat((const char*)source.pointer, &stats) == 0 && (stats.st_mode & 07777) == 0700);
        BUSTER_TEST(arguments, os_file_delete(destination));
        BUSTER_TEST(arguments, file_copy((CopyFileArguments){.original_path = source, .new_path = destination}));
        BUSTER_TEST(arguments, stat((const char*)destination.pointer, &stats) == 0 && (stats.st_mode & 0111) == 0);
        // A destination the caller cannot write is refused, as before. Root
        // bypasses the permission check, so that configuration is reported.
        if (geteuid() != 0)
        {
            BUSTER_TEST(arguments, file_write(destination, destinations[1]) && chmod((const char*)destination.pointer, 0444) == 0);
            FileCopyResult read_only = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = destination});
            BUSTER_TEST(arguments, read_only.status == FILE_COPY_FAILED && read_only.error.v == (u32)EACCES);
            BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, destinations[1]));
            BUSTER_TEST(arguments, stat((const char*)destination.pointer, &stats) == 0 && (stats.st_mode & 07777) == 0444);
            BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 2, 0));
            BUSTER_TEST(arguments, chmod((const char*)destination.pointer, 0644) == 0);
        }
        else
        {
            arguments->show(arguments, S8("FILE_COPY_READ_ONLY status=unsupported reason=root\n"));
        }
#endif

        // Replacement publishes a new file: another name for the old
        // destination keeps the old bytes.
        BUSTER_TEST(arguments, file_write(destination, destinations[1]));
        FileTestLink hard = file_test_link(arguments, false, directory, names[1], names[2]);
        BUSTER_TEST(arguments, hard != FILE_TEST_LINK_FAILED);
        if (hard == FILE_TEST_LINK_CREATED)
        {
            String8 linked = file_test_child(arena, directory, names[2]);
            BUSTER_TEST(arguments, file_copy((CopyFileArguments){.original_path = source, .new_path = destination}));
            BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, sources[2]));
            BUSTER_TEST(arguments, file_test_bytes_are(arena, linked, destinations[1]));
            BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 3, 0));
            BUSTER_TEST(arguments, os_file_delete(linked));
        }
        BUSTER_TEST(arguments, os_directory_delete(directory));
    }
    return result;
}

// Aliases of one file are refused without modification; a destination link is
// never replaced or followed; a source link is followed.
BUSTER_GLOBAL_LOCAL UnitTestResult file_test_copy_aliases(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 directory = buster_test_temporary_path(arena, S8("file-copy-aliases"), S8(""));
    if (BUSTER_REQUIRE(arguments, file_test_directory_reset(directory)))
    {
        // Mixed case lets a case-insensitive filesystem expose a case alias.
        String8 names[] = {S8("Alias-Source.bin"), S8("other.bin"), S8("nested"), S8("hard-link.bin"), S8("symbolic-link.bin"), S8("through-link.bin")};
        String8 source = file_test_child(arena, directory, names[0]);
        String8 other = file_test_child(arena, directory, names[1]);
        u8 source_bytes[] = {'a', 'l', 'i', 'a', 's', 0, 's', 'o', 'u', 'r', 'c', 'e', 0, 0xFF};
        u8 other_bytes[] = {'o', 't', 'h', 'e', 'r'};
        ByteSlice content = {source_bytes, sizeof(source_bytes)};
        ByteSlice other_content = {other_bytes, sizeof(other_bytes)};
        BUSTER_TEST(arguments, file_write(source, content) && file_write(other, other_content));
        BUSTER_TEST(arguments, os_make_directory_attempt(file_test_child(arena, directory, names[2])));

        String8 spellings[6];
        spellings[0] = source;
        spellings[1] = string_format_z(arena, S8("{S8}/./{S8}"), directory, names[0]);
        spellings[2] = string_format_z(arena, S8("{S8}/{S8}/../{S8}"), directory, names[2], names[0]);
#if BUSTER_WINDOWS
        // Win32 normalization also equates separators, a trailing dot and the
        // extended-length form of the absolute path.
        String8 backslashed = string_duplicate_arena(arena, source, true);
        String8 absolute = string_duplicate_arena(arena, os_path_absolute(arena, source, true), true);
        for (u64 index = 0; index < backslashed.length; index += 1)
        {
            if (backslashed.pointer[index] == '/')
            {
                backslashed.pointer[index] = '\\';
            }
        }
        for (u64 index = 0; index < absolute.length; index += 1)
        {
            if (absolute.pointer[index] == '/')
            {
                absolute.pointer[index] = '\\';
            }
        }
        BUSTER_TEST(arguments, absolute.length != 0);
        spellings[3] = backslashed;
        spellings[4] = string_format_z(arena, S8("{S8}."), source);
        u32 spelling_count = 5;
        if (absolute.length && !string_starts_with_sequence(absolute, S8("\\\\")))
        {
            spellings[5] = string_format_z(arena, S8("\\\\?\\{S8}"), absolute);
            spelling_count = 6;
        }
#else
        u32 spelling_count = 3;
#endif
        for (u32 index = 0; index < spelling_count; index += 1)
        {
            FileCopyResult onto = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = spellings[index]});
            FileCopyResult from = file_copy_checked((CopyFileArguments){.original_path = spellings[index], .new_path = source});
            bool refused = onto.status == FILE_COPY_SAME_FILE && !onto.error.v && !onto.cleanup_error.v && from.status == FILE_COPY_SAME_FILE &&
                           !from.error.v && !from.cleanup_error.v;
            if (!refused)
            {
                arguments->show(arguments, S8("FILE_COPY_ALIAS spelling={S8} onto={u32}/{u32} from={u32}/{u32}\n"), spellings[index], (u32)onto.status,
                                onto.error.v, (u32)from.status, from.error.v);
            }
            BUSTER_TEST(arguments, refused);
            BUSTER_TEST(arguments, !file_copy((CopyFileArguments){.original_path = source, .new_path = spellings[index]}));
            BUSTER_TEST(arguments, file_test_bytes_are(arena, source, content));
            BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 3, 0));
        }

        // Lookup case sensitivity and name creation are different capabilities.
        // Darwin can force case-sensitive lookup on a case-insensitive volume:
        // the alternate spelling is ENOENT to lookup but EEXIST to creation or
        // rename. Probe that reserved-name case independently of file_copy.
        String8 case_alias = file_test_child(arena, directory, S8("alias-source.BIN"));
        OsFileOpenResult case_probe = os_file_open_checked(case_alias, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
        bool case_insensitive = case_probe.file != 0;
        u32 case_create_error = 0;
        if (case_probe.file)
        {
            BUSTER_TEST(arguments, !case_probe.error.v);
            BUSTER_TEST(arguments, os_file_close(case_probe.file));
        }
#if BUSTER_WINDOWS
        BUSTER_TEST(arguments, case_insensitive);
#else
        if (!case_insensitive)
        {
            // Never interpret an access, resource or I/O failure as absence.
            BUSTER_TEST(arguments, case_probe.error.v == (u32)ENOENT);
            if (case_probe.error.v == (u32)ENOENT)
            {
                int probe_fd;
                do
                {
                    // Exclusive creation cannot truncate the source even when
                    // the volume reserves this case-folded spelling.
                    probe_fd = open((const char*)case_alias.pointer, O_WRONLY | O_CREAT | O_EXCL, 0600);
                } while (probe_fd < 0 && errno == EINTR);
                if (probe_fd >= 0)
                {
                    BUSTER_TEST(arguments, close(probe_fd) == 0);
                    BUSTER_TEST(arguments, unlink((const char*)case_alias.pointer) == 0);
                }
                else
                {
                    case_create_error = (u32)errno;
                    BUSTER_TEST(arguments, case_create_error == (u32)EEXIST);
                }
            }
        }
#endif
        FileCopyResult case_copy = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = case_alias});
        arguments->show(arguments,
                        S8("FILE_COPY_ALIAS kind=case case_insensitive={u32} lookup_error={u32} create_error={u32} status={u32} error={u32} cleanup={u32}\n"),
                        (u32)case_insensitive, case_probe.error.v, case_create_error, (u32)case_copy.status, case_copy.error.v, case_copy.cleanup_error.v);
        if (case_insensitive)
        {
            BUSTER_TEST(arguments, case_copy.status == FILE_COPY_SAME_FILE && !case_copy.error.v);
        }
#if !BUSTER_WINDOWS
        else if (case_probe.error.v == (u32)ENOENT && case_create_error == (u32)EEXIST)
        {
            // This is a checked refusal, not a platform skip: only the native
            // reserved-name error is accepted, with no publication or debris.
            BUSTER_TEST(arguments, case_copy.status == FILE_COPY_FAILED && case_copy.error.v == (u32)EEXIST);
            BUSTER_TEST(arguments, file_test_path_missing(arena, case_alias));
        }
#endif
        else
        {
            BUSTER_TEST(arguments, case_copy.status == FILE_COPY_PUBLISHED && !case_copy.error.v && file_test_bytes_are(arena, case_alias, content));
            BUSTER_TEST(arguments, os_file_delete(case_alias));
        }
        BUSTER_TEST(arguments, !case_copy.cleanup_error.v);
        BUSTER_TEST(arguments, file_test_bytes_are(arena, source, content) && file_test_bytes_are(arena, other, other_content));
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 3, 0));

        FileTestLink hard = file_test_link(arguments, false, directory, names[0], names[3]);
        arguments->show(arguments, S8("FILE_COPY_ALIAS kind=hard_link link_status={u32}\n"), (u32)hard);
        BUSTER_TEST(arguments, hard != FILE_TEST_LINK_FAILED);
        if (hard == FILE_TEST_LINK_CREATED)
        {
            String8 hard_link = file_test_child(arena, directory, names[3]);
            FileCopyResult onto_hard = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = hard_link});
            FileCopyResult from_hard = file_copy_checked((CopyFileArguments){.original_path = hard_link, .new_path = source});
            BUSTER_TEST(arguments, onto_hard.status == FILE_COPY_SAME_FILE && from_hard.status == FILE_COPY_SAME_FILE);
            BUSTER_TEST(arguments, file_test_bytes_are(arena, source, content) && file_test_bytes_are(arena, hard_link, content));
            BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 4, 0));
            BUSTER_TEST(arguments, os_file_delete(hard_link));
        }

        FileTestLink symbolic = file_test_link(arguments, true, directory, names[0], names[4]);
        arguments->show(arguments, S8("FILE_COPY_ALIAS kind=symbolic_link link_status={u32}\n"), (u32)symbolic);
        BUSTER_TEST(arguments, symbolic != FILE_TEST_LINK_FAILED);
        if (symbolic == FILE_TEST_LINK_CREATED)
        {
            String8 link_path = file_test_child(arena, directory, names[4]);
            String8 through = file_test_child(arena, directory, names[5]);
            // A destination link is neither replaced nor followed, whichever
            // file the source is.
            FileCopyResult onto_link = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = link_path});
            FileCopyResult other_onto_link = file_copy_checked((CopyFileArguments){.original_path = other, .new_path = link_path});
            BUSTER_TEST(arguments, onto_link.status == FILE_COPY_UNSUPPORTED_DESTINATION && other_onto_link.status == FILE_COPY_UNSUPPORTED_DESTINATION);
            BUSTER_TEST(arguments, file_test_is_link(arena, link_path, names[0]) && file_test_bytes_are(arena, source, content));
            // A source link is followed to the file it names.
            FileCopyResult from_link = file_copy_checked((CopyFileArguments){.original_path = link_path, .new_path = source});
            FileCopyResult through_link = file_copy_checked((CopyFileArguments){.original_path = link_path, .new_path = through});
            BUSTER_TEST(arguments, from_link.status == FILE_COPY_SAME_FILE && through_link.status == FILE_COPY_PUBLISHED);
            BUSTER_TEST(arguments, file_test_bytes_are(arena, through, content) && !file_test_is_link(arena, through, names[0]));
            String8 linked_names[] = {names[0], names[1], names[2], names[4], names[5]};
            BUSTER_TEST(arguments, file_test_entries_are(arena, directory, linked_names, 5, 0));
            BUSTER_TEST(arguments, os_file_delete(through) && os_file_delete(link_path));

            // A dangling destination link is refused without creating its target.
            FileTestLink dangling = file_test_link(arguments, true, directory, S8("missing-target.bin"), S8("dangling-link.bin"));
            BUSTER_TEST(arguments, dangling == FILE_TEST_LINK_CREATED);
            if (dangling == FILE_TEST_LINK_CREATED)
            {
                String8 dangling_link = file_test_child(arena, directory, S8("dangling-link.bin"));
                FileCopyResult onto_dangling = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = dangling_link});
                BUSTER_TEST(arguments, onto_dangling.status == FILE_COPY_UNSUPPORTED_DESTINATION);
                BUSTER_TEST(arguments, file_test_is_link(arena, dangling_link, S8("missing-target.bin")));
                BUSTER_TEST(arguments, file_test_path_missing(arena, file_test_child(arena, directory, S8("missing-target.bin"))));
                BUSTER_TEST(arguments, os_file_delete(dangling_link));
            }
        }
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 3, 0));
        BUSTER_TEST(arguments, file_test_bytes_are(arena, source, content) && file_test_bytes_are(arena, other, other_content));
        BUSTER_TEST(arguments, os_directory_delete(directory));
    }
    return result;
}


BUSTER_GLOBAL_LOCAL UnitTestResult file_test_publish_contents(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 directory = buster_test_temporary_path(arena, S8("file-publish-contents"), S8(""));
    if (BUSTER_REQUIRE(arguments, file_test_directory_reset(directory)))
    {
        String8 names[] = {S8("artifact.bin"), S8("directory"), S8("linked.bin"), S8("executable.bin")};
        String8 artifact = file_test_child(arena, directory, names[0]);
        String8 subdirectory = file_test_child(arena, directory, names[1]);
        String8 linked = file_test_child(arena, directory, names[2]);
        String8 executable = file_test_child(arena, directory, names[3]);
        u8 old_bytes[] = {'o', 'l', 'd', 0, 'a', 'r', 't', 'i', 'f', 'a', 'c', 't'};
        u8 new_bytes[] = {'n', 'e', 'w', 0, 'a', 'r', 't', 'i', 'f', 'a', 'c', 't', 0xff};
        ByteSlice old_content = {old_bytes, sizeof(old_bytes)};
        ByteSlice new_content = {new_bytes, sizeof(new_bytes)};
        BUSTER_TEST(arguments, file_write(artifact, old_content));
        FilePublishResult published = file_publish_checked(artifact, new_content, (OpenPermissions){.read = 1, .write = 1});
        BUSTER_TEST(arguments, published.status == FILE_PUBLISH_PUBLISHED && !published.error.v && !published.cleanup_error.v);
        BUSTER_TEST(arguments, file_test_bytes_are(arena, artifact, new_content));
        String8 artifact_only[] = {names[0]};
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, artifact_only, 1, 0));

        // A reader opened before replacement keeps the old object; a new
        // reader sees the complete new object. This makes Windows open-handle
        // replacement deterministic instead of depending on race timing.
        OsFileOpenResult held = os_file_open_checked(artifact, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1, .write = 1});
        if (BUSTER_REQUIRE(arguments, held.file != 0))
        {
            BUSTER_TEST(arguments, file_publish(artifact, old_content));
            BUSTER_TEST(arguments, file_test_bytes_are(arena, artifact, old_content));
            u8 held_bytes[sizeof(new_bytes)];
            OsFileReadResult held_read = os_file_read_exact(held.file, (ByteSlice){held_bytes, sizeof(held_bytes)});
            BUSTER_TEST(arguments, held_read.status == OS_FILE_READ_OK && held_read.transferred == sizeof(held_bytes) &&
                                     memory_compare(held_bytes, new_bytes, sizeof(new_bytes)));
            BUSTER_TEST(arguments, os_file_close(held.file));
        }

        // Empty and repeated publications leave one complete destination.
        BUSTER_TEST(arguments, file_publish(artifact, (ByteSlice){0}));
        BUSTER_TEST(arguments, file_test_bytes_are(arena, artifact, (ByteSlice){0}));
        for (u32 repeat = 0; repeat < 4; repeat += 1)
        {
            BUSTER_TEST(arguments, file_publish(artifact, new_content));
        }
        BUSTER_TEST(arguments, file_test_bytes_are(arena, artifact, new_content));
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, artifact_only, 1, 0));

        // Missing parents fail without creating an alternate path.
        String8 unreachable = file_test_child(arena, directory, S8("missing/artifact.bin"));
        FilePublishResult no_parent = file_publish_checked(unreachable, new_content, (OpenPermissions){.read = 1, .write = 1});
        BUSTER_TEST(arguments, no_parent.status == FILE_PUBLISH_FAILED && no_parent.error.v != 0);
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, artifact_only, 1, 0));

        // Directories and destination links are never followed or replaced.
        BUSTER_TEST(arguments, os_make_directory_attempt(subdirectory));
        FilePublishResult into_directory = file_publish_checked(subdirectory, new_content, (OpenPermissions){.read = 1, .write = 1});
        BUSTER_TEST(arguments, into_directory.status == FILE_PUBLISH_UNSUPPORTED_DESTINATION && !into_directory.cleanup_error.v);
        FileTestLink link_status = file_test_link(arguments, true, directory, names[0], names[2]);
        BUSTER_TEST(arguments, link_status != FILE_TEST_LINK_FAILED);
        if (link_status == FILE_TEST_LINK_CREATED)
        {
            FilePublishResult into_link = file_publish_checked(linked, new_content, (OpenPermissions){.read = 1, .write = 1});
            BUSTER_TEST(arguments, into_link.status == FILE_PUBLISH_UNSUPPORTED_DESTINATION && !into_link.cleanup_error.v);
            BUSTER_TEST(arguments, file_test_is_link(arena, linked, names[0]) && file_test_bytes_are(arena, artifact, new_content));
            BUSTER_TEST(arguments, os_file_delete(linked));
        }
        BUSTER_TEST(arguments, os_directory_delete(subdirectory));

#if BUSTER_WINDOWS
        String16 artifact_w = string16_from_string8(arena, artifact, true);
        DWORD attributes = GetFileAttributesW(artifact_w.pointer);
        BUSTER_TEST(arguments, attributes != INVALID_FILE_ATTRIBUTES && SetFileAttributesW(artifact_w.pointer, attributes | FILE_ATTRIBUTE_READONLY));
        FilePublishResult read_only = file_publish_checked(artifact, old_content, (OpenPermissions){.read = 1, .write = 1});
        BUSTER_TEST(arguments, read_only.status == FILE_PUBLISH_FAILED && read_only.error.v == (u32)ERROR_ACCESS_DENIED);
        BUSTER_TEST(arguments, file_test_bytes_are(arena, artifact, new_content));
        BUSTER_TEST(arguments, SetFileAttributesW(artifact_w.pointer, attributes));
#else
        // Replacements preserve read/write bits while artifact kind controls
        // every execute bit, independent of the old inode.
        BUSTER_TEST(arguments, chmod((const char*)artifact.pointer, 0751) == 0);
        BUSTER_TEST(arguments, file_publish(artifact, old_content));
        struct stat stats;
        BUSTER_TEST(arguments, stat((const char*)artifact.pointer, &stats) == 0 && (stats.st_mode & 0777) == 0640);
        BUSTER_TEST(arguments, file_publish_executable(artifact, new_content));
        BUSTER_TEST(arguments, stat((const char*)artifact.pointer, &stats) == 0 && (stats.st_mode & 0777) == 0751);
#endif
        BUSTER_TEST(arguments, os_file_delete(executable));
        BUSTER_TEST(arguments, file_publish_executable(executable, new_content));
#if !BUSTER_WINDOWS
        struct stat executable_stats;
        // A new artifact honors the process umask (Android masks group
        // and other permissions). Existing-artifact tests above verify that
        // replacement explicitly restores all execute bits.
        BUSTER_TEST(arguments, stat((const char*)executable.pointer, &executable_stats) == 0 &&
                               (executable_stats.st_mode & 0100) == 0100 && (executable_stats.st_mode & 0022) == 0);
#endif
        String8 final_names[] = {names[0], names[3]};
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, final_names, 2, 0));
        BUSTER_TEST(arguments, os_directory_delete(directory));
    }
    return result;
}

typedef struct FileTestPublishFault FileTestPublishFault;
struct FileTestPublishFault
{
    OsFileTestStep steps[4];
    u32 step_count;
    u32 error;
    u32 cleanup_error;
    FilePublishStatus status;
    bool any_error;
    bool staging_left;
    bool posix_replacement;
};

// Every fallible staging boundary leaves an old destination byte-identical or
// a new destination absent. A successful interrupted write publishes only the
// complete bytes, and cleanup failures stay secondary.
BUSTER_GLOBAL_LOCAL UnitTestResult file_test_publish_faults(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_GLOBAL_LOCAL const FileTestPublishFault faults[] = {
        {.steps = {{OS_FILE_TEST_STATS, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_OPEN, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.posix_replacement = true, .steps = {{OS_FILE_TEST_PERMISSIONS, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ZERO, 0}}, .step_count = 1, .any_error = true},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}}, .step_count = 2, .error = 12345},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_INTERRUPT, 0},
                   {OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100},
                   {OS_FILE_TEST_WRITE, OS_FILE_TEST_INTERRUPT, 0},
                   {OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 3}},
         .step_count = 4,
         .status = FILE_PUBLISH_PUBLISHED},
        {.steps = {{OS_FILE_TEST_FLUSH, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}}, .step_count = 1, .error = 23456},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 2,
         .error = 12345,
         .cleanup_error = 23456},
        {.steps = {{OS_FILE_TEST_FLUSH, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 2,
         .error = 12345,
         .cleanup_error = 23456},
        {.steps = {{OS_FILE_TEST_REPLACE, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_REPLACE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_DELETE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 2,
         .error = 12345,
         .cleanup_error = 23456,
         .staging_left = true},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_DELETE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 2,
         .error = 12345,
         .cleanup_error = 23456,
         .staging_left = true},
    };
    Arena* arena = arguments->arena;
    String8 directory = buster_test_temporary_path(arena, S8("file-publish-faults"), S8(""));
    if (BUSTER_REQUIRE(arguments, file_test_directory_reset(directory)))
    {
        String8 names[] = {S8("old.bin"), S8("fresh.bin")};
        String8 old_path = file_test_child(arena, directory, names[0]);
        String8 fresh_path = file_test_child(arena, directory, names[1]);
        u64 length = BUSTER_KB(128) + 3;
        u8* data = arena_allocate(arena, u8, length);
        for (u64 index = 0; index < length; index += 1)
        {
            data[index] = (u8)(index * 37 + index / 251);
        }
        ByteSlice content = {data, length};
        u8 old_bytes[] = {'o', 'l', 'd', 0, 'd', 'e', 's', 't', 0xff};
        ByteSlice old_content = {old_bytes, sizeof(old_bytes)};
        for (u32 fault_index = 0; fault_index < BUSTER_ARRAY_LENGTH(faults); fault_index += 1)
        {
            const FileTestPublishFault* fault = &faults[fault_index];
            for (u32 existing = 0; existing < 2; existing += 1)
            {
#if BUSTER_WINDOWS
                bool runs = !fault->posix_replacement;
#else
                bool runs = existing || !fault->posix_replacement;
#endif
                if (runs)
                {
                    String8 destination = existing ? old_path : fresh_path;
                    BUSTER_TEST(arguments, file_write(old_path, old_content) && os_file_delete(fresh_path));
                    os_file_test_begin(destination, fault->steps, fault->step_count);
                    FilePublishResult published = file_publish_checked(destination, content, (OpenPermissions){.read = 1, .write = 1});
                    u32 consumed = os_file_test_end();
                    bool succeeded = fault->status == FILE_PUBLISH_PUBLISHED;
                    bool error_matches = fault->any_error ? published.error.v != 0 : published.error.v == fault->error;
                    bool matches = consumed == fault->step_count && published.status == fault->status && error_matches &&
                                   published.cleanup_error.v == fault->cleanup_error;
                    if (!matches)
                    {
                        arguments->show(arguments, S8("FILE_PUBLISH_FAULT index={u32} existing={u32} consumed={u32} status={u32} error={u32} cleanup={u32}\n"),
                                        fault_index, existing, consumed, (u32)published.status, published.error.v, published.cleanup_error.v);
                    }
                    BUSTER_TEST(arguments, matches);
                    if (succeeded)
                    {
                        BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, content));
                    }
                    else if (existing)
                    {
                        BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, old_content));
                    }
                    else
                    {
                        BUSTER_TEST(arguments, file_test_path_missing(arena, destination));
                    }
                    u32 expected_count = succeeded && !existing ? 2 : 1;
                    u32 staging_count = fault->staging_left ? 1 : 0;
                    BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, expected_count, staging_count));
                    if (fault->staging_left)
                    {
                        BUSTER_TEST(arguments, file_test_remove_staging(arena, directory) == 1);
                    }
                    if (succeeded && !existing)
                    {
                        BUSTER_TEST(arguments, os_file_delete(destination));
                    }
                    BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 1, 0));
                }
            }
        }
        BUSTER_TEST(arguments, os_directory_delete(directory));
    }
    return result;
}

#if !BUSTER_SINGLE_THREADED
typedef struct FileTestPublishReader FileTestPublishReader;
struct FileTestPublishReader
{
    String8 path;
    ByteSlice first;
    ByteSlice second;
    OsBarrierHandle* barrier;
    AtomicU64 failures;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType file_test_publish_reader(void* raw)
{
    FileTestPublishReader* reader = (FileTestPublishReader*)raw;
    os_barrier_wait(reader->barrier);
    u8 bytes[4096];
    for (u32 iteration = 0; iteration < 256; iteration += 1)
    {
        bool valid = false;
        // Windows readers must permit delete sharing for atomic replacement;
        // OpenPermissions controls sharing, while OpenFlags controls access.
        OsFileOpenResult opened = os_file_open_checked(reader->path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1, .write = 1});
        if (opened.file)
        {
            OsFileReadResult content = os_file_read_exact(opened.file, (ByteSlice){bytes, sizeof(bytes)});
            u8 extra = 0;
            OsFileReadResult tail = os_file_read_some(opened.file, (ByteSlice){&extra, 1});
            bool complete = content.status == OS_FILE_READ_OK && content.transferred == sizeof(bytes) &&
                            tail.status == OS_FILE_READ_EOF && tail.transferred == 0;
            bool first = complete && memory_compare(bytes, reader->first.pointer, reader->first.length);
            bool second = complete && memory_compare(bytes, reader->second.pointer, reader->second.length);
            bool closed = os_file_close(opened.file);
            valid = (first || second) && closed;
        }
        if (!valid)
        {
            atomic_u64_increment(&reader->failures);
        }
    }
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult file_test_publish_concurrent_reader(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if !BUSTER_SINGLE_THREADED
    Arena* arena = arguments->arena;
    String8 directory = buster_test_temporary_path(arena, S8("file-publish-concurrent"), S8(""));
    if (BUSTER_REQUIRE(arguments, file_test_directory_reset(directory)))
    {
        String8 path = file_test_child(arena, directory, S8("artifact.bin"));
        u8* first = arena_allocate(arena, u8, 4096);
        u8* second = arena_allocate(arena, u8, 4096);
        for (u32 index = 0; index < 4096; index += 1)
        {
            first[index] = (u8)(index * 17 + 3);
            second[index] = (u8)(index * 29 + 7);
        }
        FileTestPublishReader reader = {
            .path = path,
            .first = {first, 4096},
            .second = {second, 4096},
            .barrier = os_barrier_create(2),
        };
        BUSTER_TEST(arguments, file_publish(path, reader.first));
        if (BUSTER_REQUIRE(arguments, reader.barrier != 0))
        {
            OsThreadHandle* thread = os_thread_create((ThreadCreateOptions){
                .callback = &file_test_publish_reader,
                .argument = &reader,
            });
            if (BUSTER_REQUIRE(arguments, thread != 0))
            {
                os_barrier_wait(reader.barrier);
                for (u32 publication = 0; publication < 16; publication += 1)
                {
                    BUSTER_TEST(arguments, file_publish(path, publication & 1 ? reader.first : reader.second));
                }
                BUSTER_TEST(arguments, os_thread_join(thread));
                BUSTER_TEST(arguments, reader.failures == 0);
            }
            os_barrier_destroy(reader.barrier);
        }
        String8 expected[] = {S8("artifact.bin")};
        BUSTER_TEST(arguments, file_test_entries_are(arena, directory, expected, 1, 0));
        BUSTER_TEST(arguments, os_directory_delete(directory));
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

typedef struct FileTestCopyFault FileTestCopyFault;
struct FileTestCopyFault
{
    OsFileTestStep steps[4];
    u32 step_count;
    u32 error;
    u32 cleanup_error;
    FileCopyStatus status;
    // The script selects the source path instead of the destination.
    bool source;
    // Any nonzero primary error is expected rather than exactly `error`.
    bool any_error;
    // The injected deletion failure leaves the staging file behind.
    bool staging_left;
    // Only a replaced POSIX destination has permissions to apply.
    bool posix_replacement;
};

// Injected failures at every boundary. Before publication the source and an old
// destination stay byte-identical, a new destination stays absent, the staging
// file is gone unless its deletion failed, and cleanup failures never replace
// the primary error.
BUSTER_GLOBAL_LOCAL UnitTestResult file_test_copy_faults(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_GLOBAL_LOCAL const FileTestCopyFault faults[] = {
        {.source = true, .steps = {{OS_FILE_TEST_OPEN, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.source = true, .steps = {{OS_FILE_TEST_STATS, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.source = true, .steps = {{OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.source = true, .steps = {{OS_FILE_TEST_READ, OS_FILE_TEST_LIMIT, 7}, {OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345}}, .step_count = 2, .error = 12345},
        {.source = true,
         .steps = {{OS_FILE_TEST_READ, OS_FILE_TEST_INTERRUPT, 0}, {OS_FILE_TEST_READ, OS_FILE_TEST_LIMIT, 7}, {OS_FILE_TEST_READ, OS_FILE_TEST_INTERRUPT, 0}},
         .step_count = 3,
         .status = FILE_COPY_PUBLISHED},
        {.source = true, .steps = {{OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}}, .step_count = 1, .error = 23456},
        {.source = true,
         .steps = {{OS_FILE_TEST_READ, OS_FILE_TEST_LIMIT, 7}, {OS_FILE_TEST_READ, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 3,
         .error = 12345,
         .cleanup_error = 23456},
        {.steps = {{OS_FILE_TEST_OPEN, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.posix_replacement = true, .steps = {{OS_FILE_TEST_PERMISSIONS, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ZERO, 0}}, .step_count = 1, .any_error = true},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}}, .step_count = 2, .error = 12345},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_INTERRUPT, 0},
                   {OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100},
                   {OS_FILE_TEST_WRITE, OS_FILE_TEST_INTERRUPT, 0},
                   {OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 3}},
         .step_count = 4,
         .status = FILE_COPY_PUBLISHED},
        {.steps = {{OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}}, .step_count = 1, .error = 23456},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 100}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 3,
         .error = 12345,
         .cleanup_error = 23456},
        {.steps = {{OS_FILE_TEST_REPLACE, OS_FILE_TEST_ERROR, 12345}}, .step_count = 1, .error = 12345},
        {.steps = {{OS_FILE_TEST_REPLACE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_DELETE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 2,
         .error = 12345,
         .cleanup_error = 23456,
         .staging_left = true},
        {.steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_DELETE, OS_FILE_TEST_ERROR, 23456}},
         .step_count = 2,
         .error = 12345,
         .cleanup_error = 23456,
         .staging_left = true},
    };
    Arena* arena = arguments->arena;
    String8 directory = buster_test_temporary_path(arena, S8("file-copy-faults"), S8(""));
    if (BUSTER_REQUIRE(arguments, file_test_directory_reset(directory)))
    {
        String8 names[] = {S8("source.bin"), S8("old.bin"), S8("fresh.bin")};
        String8 source = file_test_child(arena, directory, names[0]);
        String8 old_path = file_test_child(arena, directory, names[1]);
        String8 fresh_path = file_test_child(arena, directory, names[2]);
        // Two whole copy buffers and a remainder, so faults land mid-stream.
        u64 length = BUSTER_KB(64) * 2 + 3;
        u8* data = arena_allocate(arena, u8, length);
        for (u64 index = 0; index < length; index += 1)
        {
            data[index] = (u8)(index * 37 + index / 251);
        }
        ByteSlice content = {data, length};
        u8 old_bytes[] = {'o', 'l', 'd', 0, 'd', 'e', 's', 't', 0xFF};
        ByteSlice old_content = {old_bytes, sizeof(old_bytes)};
        BUSTER_TEST(arguments, file_write(source, content));
        for (u32 fault_index = 0; fault_index < BUSTER_ARRAY_LENGTH(faults); fault_index += 1)
        {
            const FileTestCopyFault* fault = &faults[fault_index];
            for (u32 existing = 0; existing < 2; existing += 1)
            {
#if BUSTER_WINDOWS
                bool runs = !fault->posix_replacement;
#else
                bool runs = existing || !fault->posix_replacement;
#endif
                if (runs)
                {
                    String8 destination = existing ? old_path : fresh_path;
                    BUSTER_TEST(arguments, file_write(old_path, old_content));
                    os_file_test_begin(fault->source ? source : destination, fault->steps, fault->step_count);
                    FileCopyResult copied = file_copy_checked((CopyFileArguments){.original_path = source, .new_path = destination});
                    u32 consumed = os_file_test_end();
                    bool published = fault->status == FILE_COPY_PUBLISHED;
                    bool error_matches = fault->any_error ? copied.error.v != 0 : copied.error.v == fault->error;
                    bool matches = consumed == fault->step_count && copied.status == fault->status && error_matches &&
                                   copied.cleanup_error.v == fault->cleanup_error;
                    if (!matches)
                    {
                        arguments->show(arguments, S8("FILE_COPY_FAULT index={u32} existing={u32} consumed={u32} status={u32} error={u32} cleanup={u32}\n"),
                                        fault_index, existing, consumed, (u32)copied.status, copied.error.v, copied.cleanup_error.v);
                    }
                    BUSTER_TEST(arguments, matches);
                    BUSTER_TEST(arguments, file_test_bytes_are(arena, source, content));
                    if (published)
                    {
                        BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, content));
                    }
                    else if (existing)
                    {
                        BUSTER_TEST(arguments, file_test_bytes_are(arena, destination, old_content));
                    }
                    else
                    {
                        BUSTER_TEST(arguments, file_test_path_missing(arena, destination));
                    }
                    u32 file_count = published && !existing ? 3 : 2;
                    u32 staging_count = fault->staging_left ? 1 : 0;
                    BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, file_count, staging_count));
                    if (fault->staging_left)
                    {
                        BUSTER_TEST(arguments, file_test_remove_staging(arena, directory) == 1);
                    }
                    if (published && !existing)
                    {
                        BUSTER_TEST(arguments, os_file_delete(destination));
                    }
                    BUSTER_TEST(arguments, file_test_entries_are(arena, directory, names, 2, 0));
                }
            }
        }

        // The boolean entry reports the same unpublished outcome.
        OsFileTestStep write_error = {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345};
        os_file_test_begin(old_path, &write_error, 1);
        BUSTER_TEST(arguments, !file_copy((CopyFileArguments){.original_path = source, .new_path = old_path}));
        BUSTER_TEST(arguments, os_file_test_end() == 1);
        BUSTER_TEST(arguments, file_test_bytes_are(arena, old_path, old_content));
        BUSTER_TEST(arguments, os_directory_delete(directory));
    }
    return result;
}

UnitTestResult file_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, file_test_write_failures);
    BUSTER_TEST_FIXTURE(arguments, file_test_read_failures);
    BUSTER_TEST_FIXTURE(arguments, file_test_read_alignment);
    BUSTER_TEST_FIXTURE(arguments, file_test_publish_contents);
    BUSTER_TEST_FIXTURE(arguments, file_test_publish_faults);
    BUSTER_TEST_FIXTURE(arguments, file_test_publish_concurrent_reader);
    BUSTER_TEST_FIXTURE(arguments, file_test_copy_contents);
    BUSTER_TEST_FIXTURE(arguments, file_test_copy_aliases);
    BUSTER_TEST_FIXTURE(arguments, file_test_copy_faults);
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
            if (BUSTER_REQUIRE(arguments, mapped))
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
    if (BUSTER_REQUIRE(arguments, padded_fallback.bytes.pointer != 0 && padded_fallback.mapped_pointer == 0))
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
    if (BUSTER_REQUIRE(arguments, empty.pointer != 0))
    {
        BUSTER_TEST(arguments, empty.length == 0);
        BUSTER_TEST(arguments, ((u64)empty.pointer & 3) == 0);
        bool padding_is_zero = empty.pointer[0] == 0 && empty.pointer[1] == 0 && empty.pointer[2] == 0 && empty.pointer[3] == 0;
        BUSTER_TEST(arguments, padding_is_zero);
    }
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
                                                       (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
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
                                                            .use_process_environment = 1, .search_path = 1,
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
                                                           (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
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
