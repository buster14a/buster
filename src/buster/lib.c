#pragma once

#include <buster/lib.h>

#include <buster/system_headers.h>

BUSTER_IMPL THREAD_LOCAL_DECL Thread* thread;

STRUCT(ProtectionFlags)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
    u64 reserved:61;
};

STRUCT(MapFlags)
{
    u64 private:1;
    u64 anonymous:1;
    u64 no_reserve:1;
    u64 populate:1;
    u64 reserved:60;
};

#if defined (__linux__) || defined(__APPLE__)
BUSTER_LOCAL int os_posix_protection_flags(ProtectionFlags flags)
{
    int result = 
        PROT_READ * flags.read |
        PROT_WRITE * flags.write |
        PROT_EXEC * flags.execute
    ;

    return result;
}

BUSTER_LOCAL int os_posix_map_flags(MapFlags flags)
{
    int result = 
#ifdef __linux__
        MAP_POPULATE * flags.populate |
#endif
        MAP_PRIVATE * flags.private |
        MAP_ANON * flags.anonymous |
        MAP_NORESERVE * flags.no_reserve;

    return result;
}
#elif defined(_WIN32)
BUSTER_LOCAL DWORD os_windows_protection_flags(ProtectionFlags flags)
{
    DWORD result = 0;

    if (flags.read & flags.write & flags.execute)
    {
        result = PAGE_EXECUTE_READWRITE;
    }
    else if (flags.read & flags.write)
    {
        result = PAGE_READWRITE;
    }
    else if (flags.read & flags.execute)
    {
        result = PAGE_EXECUTE_READ;
    }
    else if (flags.read)
    {
        result = PAGE_READONLY;
    }
    else if (flags.execute)
    {
        result = PAGE_EXECUTE;
    }
    else
    {
        BUSTER_UNREACHABLE();
    }

    return result;
}

BUSTER_LOCAL DWORD os_windows_allocation_flags(MapFlags flags)
{
    DWORD result = 0;
    result |= MEM_RESERVE;

    if (!flags.no_reserve)
    {
        result |= MEM_COMMIT;
    }

    return result;
}
#endif

BUSTER_LOCAL bool os_lock_and_unlock(void* address, u64 size)
{
    bool result = 1;

#if defined (__linux__) || defined(__APPLE__)
    let os_result = mlock(address, size);
    result = os_result == 0;
    if (result)
    {
        os_result = munlock(address, size);
    }
    result = os_result == 0;
#elif defined(_WIN32)
    if (w32_rio_functions.RIORegisterBuffer)
    {
        RIO_BUFFERID buffer_id = w32_rio_functions.RIORegisterBuffer(address, size);
        result = buffer_id != RIO_INVALID_BUFFERID;
        if (result)
        {
            if (w32_rio_functions.RIODeregisterBuffer)
            {
                w32_rio_functions.RIODeregisterBuffer(buffer_id);
            }
        }
    }
#endif
    return result;
}

BUSTER_LOCAL void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map)
{
    void* address = 0;

#if defined(__linux__) || defined(__APPLE__)
    let protection_flags = os_posix_protection_flags(protection);
    let map_flags = os_posix_map_flags(map);

    address = mmap(base, size, protection_flags, map_flags, -1, 0);
    if (address == MAP_FAILED)
    {
        address = 0;
    }
#elif defined(_WIN32)
    let allocation_flags = os_windows_allocation_flags(map);
    let protection_flags = os_windows_protection_flags(protection);
    address = VirtualAlloc(base, size, allocation_flags, protection_flags);
#endif
    return address;
}

BUSTER_LOCAL bool os_unreserve(void* address, u64 size)
{
    bool result = 1;
#if defined(__linux__) || defined(__APPLE__)
    let unmap_result = munmap(address, size);
    result = unmap_result == 0;
#elif defined(_WIN32)
    let virtual_free_result = VirtualFree(address, size, MEM_DECOMMIT);
    result = virtual_free_result != 0;
    if (result)
    {
        virtual_free_result = VirtualFree(address, 0, MEM_RELEASE);
        result = virtual_free_result != 0;
    }
#endif
    return result;
}

BUSTER_LOCAL bool os_commit(void* address, u64 size, ProtectionFlags protection, bool lock)
{
    bool result = 1;

#if defined(__linux__) || defined(__APPLE__)
    let protection_flags = os_posix_protection_flags(protection);
    let os_result = mprotect(address, size, protection_flags);
    result = os_result == 0;
#elif defined(_WIN32)
    let protection_flags = os_windows_protection_flags(protection);
    let os_result = VirtualAlloc(address, size, MEM_COMMIT, protection_flags);
    result = os_result != 0;
#endif

    if (result & lock)
    {
        result = os_lock_and_unlock(address, size);
    }

    return result;
}

BUSTER_LOCAL void string8_reverse(String8 s)
{
    let restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        let ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

BUSTER_IMPL u64 string8_occurrence_count(String8 s, u8 ch)
{
    u64 count = 0;
    let restrict p = s.pointer;
    for (u64 i = 0; i < s.length; i += 1)
    {
        count += p[i] == ch;
    }

    return count;
}

BUSTER_LOCAL void string16_reverse(String16 s)
{
    let restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        let ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

BUSTER_LOCAL String8 string8_format_integer_hexadecimal(String8 buffer, u64 value, bool upper)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        let v = value;
        u64 i = 0;
        char8 alpha_start = upper ? 'A' : 'a';

        while (v != 0)
        {
            let digit = v % 16;
            let ch = (char8)(digit > 9 ? (digit - 10 + alpha_start) : (digit + '0'));
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 16;
        }

        let length = i;

        result = (String8) { buffer.pointer , length };
        string8_reverse(result);
    }

    return result;
}

BUSTER_LOCAL String8 string8_format_integer_decimal(String8 buffer, u64 value, bool treat_as_signed)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        u64 i = treat_as_signed;

        buffer.pointer[0] = '-';
        let v = value;

        while (v != 0)
        {
            let digit = v % 10;
            let ch = (u8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 10;
        }

        let length = i;

        result = (String8) { buffer.pointer + treat_as_signed, length - treat_as_signed };
        string8_reverse(result);
        result.pointer -= treat_as_signed;
        result.length += treat_as_signed;
    }

    return result;
}

BUSTER_LOCAL String8 string8_format_integer_octal(String8 buffer, u64 value)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        u64 i = 0;
        let v = value;

        while (v != 0)
        {
            let digit = v % 8;
            let ch = (u8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 8;
        }

        let length = i;

        result = (String8) { buffer.pointer, length };
        string8_reverse(result);
    }

    return result;
}

BUSTER_LOCAL String8 string8_format_integer_binary(String8 buffer, u64 value)
{
    String8 result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (String8) { buffer.pointer, 1};
    }
    else
    {
        u64 i = 0;
        let v = value;

        while (v != 0)
        {
            let digit = v % 2;
            let ch = (u8)(digit + '0');
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 2;
        }

        let length = i;

        result = (String8) { buffer.pointer, length };
        string8_reverse(result);
    }

    return result;
}

BUSTER_IMPL String8 string8_format_integer_stack(String8 buffer, FormatIntegerOptions options)
{
    if (options.treat_as_signed)
    {
        BUSTER_CHECK(!options.prefix);
        BUSTER_CHECK(options.format == INTEGER_FORMAT_DECIMAL);
    }

    u64 prefix_digit_count = 2;

    String8 result = {};
    if (options.prefix)
    {
        u8 prefix_ch;
        switch (options.format)
        {
            break; case INTEGER_FORMAT_HEXADECIMAL: prefix_ch = 'x';
            break; case INTEGER_FORMAT_DECIMAL: prefix_ch = 'd';
            break; case INTEGER_FORMAT_OCTAL: prefix_ch = 'o';
            break; case INTEGER_FORMAT_BINARY: prefix_ch = 'b';
            break; default: BUSTER_UNREACHABLE();
        }
        buffer.pointer[0] = '0';
        buffer.pointer[1] = prefix_ch;
        buffer.pointer += prefix_digit_count;
        buffer.length += prefix_digit_count;
    }

    switch (options.format)
    {
        break; case INTEGER_FORMAT_HEXADECIMAL:
        {
            bool upper = false;
            result = string8_format_integer_hexadecimal(buffer, options.value, upper);
        }
        break; case INTEGER_FORMAT_DECIMAL:
        {
            result = string8_format_integer_decimal(buffer, options.value, options.treat_as_signed);
        }
        break; case INTEGER_FORMAT_OCTAL:
        {
            result = string8_format_integer_octal(buffer, options.value);
        }
        break; case INTEGER_FORMAT_BINARY:
        {
            result = string8_format_integer_binary(buffer, options.value);
        }
        break; default: BUSTER_UNREACHABLE();
    }

    if (options.prefix)
    {
        result.pointer -= prefix_digit_count;
        result.length += prefix_digit_count;
    }

    return result;
}

[[noreturn]] [[gnu::cold]] BUSTER_IMPL void buster_failed_assertion(u32 line, String8 function_name, String8 file_path)
{
    print(S8("Assert failed at "));
    print(function_name);
    print(S8(" in "));
    print(file_path);
    print(S8(":"));
    char8 buffer[128];
    let stack_string = string8_format_integer_stack((String8){ buffer, BUSTER_ARRAY_LENGTH(buffer) }, (FormatIntegerOptions){ .value = line });
    print(stack_string);
    print(S8("\n"));
        
    fail();
}

BUSTER_IMPL FileDescriptor* os_file_open(OsString path, OpenFlags flags, OpenPermissions permissions)
{
    BUSTER_CHECK(!path.pointer[path.length]);
    FileDescriptor* result = 0;
#if defined (__linux__) || defined(__APPLE__)
    int o = 0;
    if (flags.read & flags.write)
    {
        o = O_RDWR;
    }
    else if (flags.read)
    {
        o = O_RDONLY;
    }
    else if (flags.write)
    {
        o = O_WRONLY;
    }
    else
    {
        BUSTER_UNREACHABLE();
    }

    o |= (flags.truncate) * O_TRUNC;
    o |= (flags.create) * O_CREAT;
    o |= (flags.directory) * O_DIRECTORY;

    mode_t mode = permissions.execute ? 0755 : 0644;
    int fd = open((char*)path.pointer, o, mode);

    if (fd >= 0)
    {
        result = (FileDescriptor*)(u64)fd;
    }

    return result;
#elif defined(_WIN32)
    DWORD desired_access = 0;
    DWORD shared_mode = 0;
    SECURITY_ATTRIBUTES security_attributes = { sizeof(security_attributes), 0, 0 };
    DWORD creation_disposition = 0;
    DWORD flags_and_attributes = 0;
    HANDLE template_file = 0;

    if (flags.read)
    {
        desired_access |= GENERIC_READ;
    }

    if (flags.write)
    {
        desired_access |= GENERIC_WRITE;
    }

    if (flags.execute)
    {
        desired_access |= GENERIC_EXECUTE;
    }

    if (permissions.read)
    {
        shared_mode |= FILE_SHARE_READ;
    }
    
    if (permissions.write)
    {
        shared_mode |= FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    }

    if (permissions.write)
    {
        creation_disposition |= CREATE_ALWAYS;
    }
    else
    {
        creation_disposition |= OPEN_EXISTING;
    }

    let fd = CreateFileW(path.pointer, desired_access, shared_mode, &security_attributes, creation_disposition, flags_and_attributes, template_file);
    if (fd != INVALID_HANDLE_VALUE)
    {
        result = (FileDescriptor*)fd;
    }
    else
    {
#if 0
        DWORD error = GetLastError();
        OsChar msgBuffer[1024];

        FormatMessageW(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                msgBuffer,
                0,
                NULL
                );

        print(S8("Error {u32}: {S16}\n"), error, string16_from_pointer(msgBuffer));
        LocalFree(msgBuffer);
#endif
    }
#endif
    return result;
}

BUSTER_IMPL void os_make_directory(OsString path)
{
#if defined(__linux__) || defined(__APPLE__)
    mkdir((const char*)path.pointer, 0755);
#elif defined(_WIN32)
    CreateDirectoryW(path.pointer, 0);
#endif
}

BUSTER_LOCAL FileDescriptor* posix_fd_to_generic_fd(int fd)
{
    BUSTER_CHECK(fd >= 0);
    return (FileDescriptor*)(u64)(fd);
}

BUSTER_LOCAL int generic_fd_to_posix(FileDescriptor* fd)
{
    BUSTER_CHECK(fd);
    return (int)(u64)fd;
}

BUSTER_LOCAL void* generic_fd_to_windows(FileDescriptor* fd)
{
    BUSTER_CHECK(fd);
    return (void*)fd;
}

BUSTER_IMPL FileStats os_file_get_stats(FileDescriptor* file_descriptor, FileStatsOptions options)
{
    FileStats result = {};

    if (((u64)file_descriptor != 0) & (options.raw != 0))
    {
#if defined(__linux__) || defined(__APPLE__)
        int fd = generic_fd_to_posix(file_descriptor);
        struct stat sb;
        let fstat_result = fstat(fd, &sb);
        if (fstat_result == 0)
        {
            if (options.size)
            {
                result.size = (u64)sb.st_size;
            }

            if (options.modified_time)
            {
                result.modified_time_s = (u64)sb.st_mtime;
            }
        }
#elif defined(_WIN32)
        HANDLE fd = generic_fd_to_windows(file_descriptor);
        LARGE_INTEGER file_size = {};
        BOOL file_result = GetFileSizeEx(fd, &file_size);
        BUSTER_CHECK(file_result != 0);
        result.size = (u64)file_size.QuadPart;
#endif
    }

    return result;
}

BUSTER_IMPL u64 os_file_get_size(FileDescriptor* file_descriptor)
{
#if defined(__linux__) || defined(__APPLE__)
    int fd = generic_fd_to_posix(file_descriptor);
    struct stat sb;
    let fstat_result = fstat(fd, &sb);
    BUSTER_CHECK(fstat_result == 0);

    return (u64)sb.st_size;
#elif defined(_WIN32)
    HANDLE fd = generic_fd_to_windows(file_descriptor);
    LARGE_INTEGER file_size = {};
    BOOL result = GetFileSizeEx(fd, &file_size);
    BUSTER_CHECK(result);
    return file_size.QuadPart;
#endif
}

BUSTER_LOCAL u64 os_file_read_partially(FileDescriptor* file_descriptor, void* buffer, u64 byte_count)
{
    u64 result = 0;
#if defined(__linux__) || defined(__APPLE__)
    let fd = generic_fd_to_posix(file_descriptor);
    let read_byte_count = read(fd, buffer, byte_count);
    if (read_byte_count > 0)
    {
        result = (u64)read_byte_count;
    }
    else if (read_byte_count < 0)
    {
        print(string8_from_pointer(strerror(errno)));
    }
#elif defined(_WIN32)
    let fd = generic_fd_to_windows(file_descriptor);
    DWORD read_byte_count = 0;
    BOOL read_result = ReadFile(fd, buffer, (u32)byte_count, &read_byte_count, 0);
    if (read_result)
    {
        result = read_byte_count;
    }
#endif
    return result;
}

BUSTER_IMPL u64 os_file_read(FileDescriptor* file_descriptor, ByteSlice buffer, u64 byte_count)
{
    u64 read_byte_count = 0;
    let pointer = buffer.pointer;
    BUSTER_CHECK(buffer.length >= byte_count);
    while (byte_count - read_byte_count)
    {
        let iteration_read_byte_count = os_file_read_partially(file_descriptor, pointer + read_byte_count, byte_count - read_byte_count);
        if (iteration_read_byte_count == 0)
        {
            break;
        }
        read_byte_count += iteration_read_byte_count;
    }

    return read_byte_count;
}

BUSTER_LOCAL u64 os_file_write_partially(FileDescriptor* file_descriptor, void* pointer, u64 length)
{
#if defined(__linux__) || defined(__APPLE__)
    let fd = generic_fd_to_posix(file_descriptor);
    let result = write(fd, pointer, length);
    BUSTER_CHECK(result > 0);
    return (u64)result;
#elif defined(_WIN32)
    let fd = generic_fd_to_windows(file_descriptor);
    DWORD written_byte_count = 0;
    BOOL result = WriteFile(fd, pointer, (u32)length, &written_byte_count, 0);
    BUSTER_CHECK(result);
    return written_byte_count;
#endif
}

BUSTER_IMPL FileDescriptor* os_get_stdout()
{
    FileDescriptor* result = {};
#if defined(__linux__) || defined(__APPLE__)
    result = posix_fd_to_generic_fd(1);
#elif defined(_WIN32)
    result = (FileDescriptor*)GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    return result;
}

BUSTER_IMPL void os_file_write(FileDescriptor* file_descriptor, ByteSlice buffer)
{
    u64 total_written_byte_count = 0;

    while (total_written_byte_count < buffer.length)
    {
        let written_byte_count = os_file_write_partially(file_descriptor, buffer.pointer + total_written_byte_count, buffer.length - total_written_byte_count);
        total_written_byte_count += written_byte_count;
    }
}

BUSTER_IMPL bool os_file_close(FileDescriptor* file_descriptor)
{
    bool result = false;
    if (file_descriptor)
    {
#if defined(__linux__) || defined(__APPLE__)
        let fd = generic_fd_to_posix(file_descriptor);
        let close_result = close(fd);
        result = close_result == 0;
#elif defined(_WIN32)
        let fd = generic_fd_to_windows(file_descriptor);
        let close_result = CloseHandle(fd);
        result = close_result != 0;
#endif
    }

    return result;
}

__attribute__((used)) BUSTER_LOCAL u64 page_size = BUSTER_KB(4);
BUSTER_LOCAL u64 default_granularity = BUSTER_MB(2);

BUSTER_LOCAL u64 minimum_position = sizeof(Arena);

BUSTER_LOCAL bool arena_lock_pages = true;

BUSTER_LOCAL u64 default_reserve_size = BUSTER_GB(4);
BUSTER_LOCAL u64 initial_size_granularity_factor = 4;

BUSTER_IMPL Arena* arena_create(ArenaInitialization initialization)
{
    if (!initialization.reserved_size)
    {
        initialization.reserved_size = default_reserve_size;
    }

    if (!initialization.count)
    {
        initialization.count = 1;
    }

    let count = initialization.count;
    let individual_reserved_size = initialization.reserved_size;
    let total_reserved_size = individual_reserved_size * count;

    ProtectionFlags protection_flags = { .read = 1, .write = 1 };
    MapFlags map_flags = { .private = 1, .anonymous = 1, .no_reserve = 1, .populate = 0 };
    let raw_pointer = os_reserve(0, total_reserved_size, protection_flags, map_flags);

    if (!initialization.granularity)
    {
        initialization.granularity = default_granularity;
    }

    if (!initialization.initial_size)
    {
        initialization.initial_size = default_granularity * initial_size_granularity_factor;
    }

    for (u64 i = 0; i < count; i += 1)
    {
        let arena = (Arena*)((u8*)raw_pointer + (individual_reserved_size * i));
        os_commit(arena, initialization.initial_size, protection_flags, arena_lock_pages);
        *arena = (Arena){ 
            .reserved_size = individual_reserved_size,
            .position = minimum_position,
            .os_position = initialization.initial_size,
            .granularity = initialization.granularity,
        };
    }

    return (Arena*)raw_pointer;
}

BUSTER_IMPL bool arena_destroy(Arena* arena, u64 count)
{
    count = count == 0 ? 1 : count;
    let reserved_size = arena->reserved_size;
    let size = reserved_size * count;
    return os_unreserve(arena, size);
}

BUSTER_IMPL void arena_set_position(Arena* arena, u64 position)
{
    arena->position = position;
}

BUSTER_IMPL void arena_reset_to_start(Arena* arena)
{
    arena_set_position(arena, minimum_position);
}

BUSTER_IMPL void* arena_current_pointer(Arena* arena, u64 alignment)
{
    return (u8*)arena + align_forward(arena->position, alignment);
}

BUSTER_IMPL void* arena_allocate_bytes(Arena* arena, u64 size, u64 alignment)
{
    let aligned_offset = align_forward(arena->position, alignment);
    let aligned_size_after = aligned_offset + size;
    let arena_byte_pointer = (u8*)arena;
    let os_position = arena->os_position;

    if (BUSTER_UNLIKELY(aligned_size_after > os_position))
    {
        let target_committed_size = align_forward(aligned_size_after, arena->granularity);
        let size_to_commit = target_committed_size - os_position;
        let commit_pointer = arena_byte_pointer + os_position;
        os_commit(commit_pointer, size_to_commit, (ProtectionFlags) { .read = 1, .write = 1 }, arena_lock_pages);
        arena->os_position = target_committed_size;
    }

    let result = arena_byte_pointer + aligned_offset;
    arena->position = aligned_size_after;
    BUSTER_CHECK(arena->position <= arena->os_position);

    return result;
}

BUSTER_IMPL String8 arena_join_string8(Arena* arena, String8Slice strings, bool zero_terminate)
{
    u64 length = 0;

    for (u64 i = 0; i < strings.length; i += 1)
    {
        let string = strings.pointer[i];
        length += string.length;
    }

    let char_size = sizeof(*strings.pointer[0].pointer);

     let pointer = (typeof(strings.pointer[0].pointer))arena_allocate_bytes(arena, (length + zero_terminate) * char_size, alignof(typeof(*strings.pointer[0].pointer)));

    u64 i = 0;

    for (u64 index = 0; index < strings.length; index += 1)
    {
        let string = strings.pointer[index];
        memcpy(pointer + i, string.pointer, string_size(string));
        i += string.length;
    }

    BUSTER_CHECK(i == length);
    if (zero_terminate)
    {
        pointer[i] = 0;
    }

    return (String8){pointer, length};
}

BUSTER_IMPL String16 arena_join_string16(Arena* arena, String16Slice strings, bool zero_terminate)
{
    u64 length = 0;

    for (u64 i = 0; i < strings.length; i += 1)
    {
        let string = strings.pointer[i];
        length += string.length;
    }

    let char_size = sizeof(*strings.pointer[0].pointer);

    let pointer = (typeof(strings.pointer[0].pointer))arena_allocate_bytes(arena, (length + zero_terminate) * char_size, alignof(typeof(*strings.pointer[0].pointer)));

    u64 i = 0;

    for (u64 index = 0; index < strings.length; index += 1)
    {
        let string = strings.pointer[index];
        memcpy(pointer + i, string.pointer, string_size(string));
        i += string.length;
    }

    BUSTER_CHECK(i == length);
    if (zero_terminate)
    {
        pointer[i] = 0;
    }

    return (String16){pointer, length};
}

BUSTER_IMPL String8 arena_duplicate_string8(Arena* arena, String8 str, bool zero_terminate)
{
    let pointer = arena_allocate(arena, char8, str.length + zero_terminate);
    let result = (String8){pointer, str.length};
    memcpy(pointer, str.pointer, string_size(str));
    if (zero_terminate)
    {
        pointer[str.length] = 0;
    }
    return result;
}

BUSTER_IMPL String16 arena_duplicate_string16(Arena* arena, String16 str, bool zero_terminate)
{
    let pointer = arena_allocate(arena, u16, str.length + zero_terminate);
    let result = (String16){pointer, str.length};
    memcpy(pointer, str.pointer, string_size(str));
    if (zero_terminate)
    {
        pointer[str.length] = 0;
    }
    return result;
}

BUSTER_IMPL TimeDataType take_timestamp()
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec ts;
    let result = clock_gettime(CLOCK_MONOTONIC, &ts);
    BUSTER_CHECK(result == 0);
    return *(u128*)&ts;
#elif defined(_WIN32)
    LARGE_INTEGER c;
    BOOL result = QueryPerformanceCounter(&c);
    BUSTER_CHECK(result);
    return c.QuadPart;
#endif
}

__attribute__((used)) BUSTER_LOCAL TimeDataType frequency;

BUSTER_IMPL u64 ns_between(TimeDataType start, TimeDataType end)
{
#if defined(__linux__) || defined(__APPLE__)
    let start_ts = *(struct timespec*)&start;
    let end_ts = *(struct timespec*)&end;
    let second_diff = end_ts.tv_sec - start_ts.tv_sec;
    let ns_diff = end_ts.tv_nsec - start_ts.tv_nsec;

    let result = (u64)second_diff * 1000000000ULL + (u64)ns_diff;
    return result;
#elif defined(_WIN32)
    let ns = (f64)((end - start) * 1000 * 1000 * 1000) / frequency;
    return ns;
#endif
}

BUSTER_IMPL bool os_initialize_time()
{
    bool result = {};
#if defined(_WIN32)
    LARGE_INTEGER freq;
    result = QueryPerformanceFrequency(&freq) != 0;
    if (result)
    {
        frequency = (u64)freq.HighPart;
    }
#else
    struct timespec ts;
    result = clock_getres(CLOCK_MONOTONIC, &ts) == 0;
    if (result)
    {
        frequency = *(u128*)&ts;
    }
#endif
    return result;
}

BUSTER_IMPL ByteSlice file_read(Arena* arena, OsString path, FileReadOptions options)
{
    let fd = os_file_open(path, (OpenFlags) { .read = 1 }, (OpenPermissions){ .read = 1 });
    ByteSlice result = {};

    if (fd)
    {
        if (!options.start_alignment)
        {
            options.start_alignment = 1;
        }

        if (!options.end_alignment)
        {
            options.end_alignment = 1;
        }

        let file_size = os_file_get_size(fd);
        let file_buffer = (u8*)&file_read;
        if (file_size)
        {
            let allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
            let allocation_bottom = allocation_size - (file_size + options.start_padding);
            let allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
            file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
            file_size = os_file_read(fd, (ByteSlice) { file_buffer + options.start_padding, file_size }, file_size);
            memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
        }
        os_file_close(fd);
        result = (ByteSlice) { file_buffer + options.start_padding, file_size };
    }

    return result;
}

BUSTER_IMPL bool file_write(OsString path, ByteSlice content)
{
    let fd = os_file_open(path, (OpenFlags) { .write = 1, .create = 1, .truncate = 1 }, (OpenPermissions){ .read = 1, .write = 1 });
    bool result = {};

    result = fd != 0;
    if (result)
    {
        os_file_write(fd, content);
        os_file_close(fd);
    }

    return result;
}

BUSTER_IMPL OsString os_path_absolute_stack(OsString buffer, OsString relative_file_path)
{
    OsString result = {};
#if defined(__linux__) || defined(__APPLE__)
    let syscall_result = realpath((char*)relative_file_path.pointer, (char*)buffer.pointer);

    if (syscall_result)
    {
        result = string8_from_pointer_length(syscall_result, strlen(syscall_result));
        BUSTER_CHECK(result.length < buffer.length);
    }

#elif defined(_WIN32)
    DWORD length = GetFullPathNameW(relative_file_path.pointer, buffer.length, buffer.pointer, 0);
    if (length <= buffer.length)
    {
        result.pointer = buffer.pointer;
        result.length = length;
    }
#endif
    return result;
}

BUSTER_IMPL OsString path_absolute(Arena* arena, OsString relative_file_path)
{
    OsChar buffer[max_path_length];
    let stack_slice = os_path_absolute_stack(os_string_from_pointer_length(buffer, BUSTER_ARRAY_LENGTH(buffer)), relative_file_path);
    let result = arena_duplicate_os_string(arena, stack_slice, true);
    return result;
}

BUSTER_LOCAL void thread_initialize()
{
}

#if BUSTER_USE_IO_RING
BUSTER_LOCAL bool io_ring_init(IoRing* ring, u32 entry_count)
{
    bool result = true;
#ifdef __linux__
    int io_uring_queue_creation_result = io_uring_queue_init(entry_count, &ring->linux_impl, 0);
    result &= io_uring_queue_creation_result == 0;
#else
    BUSTER_UNUSED(ring);
    BUSTER_UNUSED(entry_count);
#endif
    return result;
}

BUSTER_LOCAL IoRingSubmission io_ring_get_submission(IoRing* ring)
{
    return (IoRingSubmission) {
        .sqe = io_uring_get_sqe(&ring->linux_impl),
    };
}

BUSTER_IMPL IoRingSubmission io_ring_prepare_open(char* path, u64 user_data)
{
    let ring = &thread->ring;
    let submission = io_ring_get_submission(ring);
    submission.sqe->user_data = user_data;
    io_uring_prep_openat(submission.sqe, AT_FDCWD, path, O_RDONLY, 0);
    ring->submitted_entry_count += 1;
    return submission;
}

STRUCT(StatOptions)
{
    u32 modified_time:1;
    u32 size:1;
};

BUSTER_IMPL IoRingSubmission io_ring_prepare_stat(int fd, struct statx* statx_buffer, u64 user_data, StatOptions options)
{
    let ring = &thread->ring;
    let submission = io_ring_get_submission(ring);
    submission.sqe->user_data = user_data;
    unsigned mask = 0;
    if (options.modified_time) mask |= STATX_MTIME;
    if (options.size) mask |= STATX_SIZE;
    io_uring_prep_statx(submission.sqe, fd, "", AT_EMPTY_PATH, mask, statx_buffer);
    ring->submitted_entry_count += 1;
    return submission;
}

BUSTER_LOCAL constexpr u64 max_rw_count = 0x7ffff000;

BUSTER_IMPL IoRingSubmission io_ring_prepare_read_and_close(int fd, u8* buffer, u64 size, u64 user_data, u64 read_mask, u64 close_mask)
{
    let ring = &thread->ring;
    for (u64 i = 0; i != size;)
    {
        let offset = i;
        let read_byte_count = BUSTER_MIN(size, max_rw_count);
        let submission = io_ring_get_submission(ring);
        io_uring_prep_read(submission.sqe, fd, buffer + offset, read_byte_count, offset);
        submission.sqe->user_data = read_mask | user_data;
        submission.sqe->flags |= IOSQE_IO_LINK;
        i += read_byte_count;
        ring->submitted_entry_count += 1;
    }

    let close = io_ring_get_submission(ring);
    close.sqe->user_data = user_data | close_mask;
    io_uring_prep_close(close.sqe, fd);
    ring->submitted_entry_count += 1;

    return close;
}

BUSTER_IMPL IoRingSubmission io_ring_prepare_waitid(pid_t pid, siginfo_t* siginfo, u64 user_data)
{
    let ring = &thread->ring;
    let submission = io_ring_get_submission(ring);
    submission.sqe->user_data = user_data;
    io_uring_prep_waitid(submission.sqe, P_PID, pid, siginfo, WEXITED, 0);
    ring->submitted_entry_count += 1;
    return submission;
}

BUSTER_IMPL u32 io_ring_submit_and_wait_all()
{
    let ring = &thread->ring;
    u32 result = 0;
    let submitted_entry_count = ring->submitted_entry_count;
    BUSTER_CHECK(submitted_entry_count);
    let submit_wait_result = io_uring_submit_and_wait(&ring->linux_impl, submitted_entry_count);
    if ((submit_wait_result >= 0) & (submitted_entry_count == (u32)submit_wait_result))
    {
        result = submitted_entry_count;
        ring->submitted_entry_count = 0;
    }
    else if (program_state->input.verbose)
    {
        printf("Waiting for io_ring tasks failed\n");
    }
    return result;
}

BUSTER_IMPL u32 io_ring_submit()
{
    u32 result = 0;
    let ring = &thread->ring;
    let submitted_entry_count = ring->submitted_entry_count;
    BUSTER_CHECK(submitted_entry_count);
    let submit_result = io_uring_submit(&ring->linux_impl);
    if ((submit_result >= 0) & ((u32)submit_result == submitted_entry_count))
    {
        result = (u32)submit_result;
        ring->submitted_entry_count = 0;
    }
    return result;
}

BUSTER_IMPL IoRingCompletion io_ring_wait_completion()
{
    IoRingCompletion result = {};
    let ring = &thread->ring.linux_impl;
    struct io_uring_cqe* cqe;
    let wait_result = io_uring_wait_cqe(ring, &cqe);
    if (wait_result == 0)
    {
        result = (IoRingCompletion){
            .user_data = cqe->user_data,
            .result = cqe->res,
        };
        io_uring_cqe_seen(ring, cqe);
    }

    return result;
}

BUSTER_IMPL IoRingCompletion io_ring_peek_completion()
{
    IoRingCompletion completion = {};
    let ring = &thread->ring.linux_impl;
    struct io_uring_cqe* cqe;
    int peek_result = io_uring_peek_cqe(ring, &cqe);
    if (peek_result == 0)
    {
        completion = (IoRingCompletion){
            .user_data = cqe->user_data,
            .result = cqe->res,
        };
        io_uring_cqe_seen(ring, cqe);
    }
    else
    {
        printf("Peek failed\n");
    }

    return completion;
}
#endif

BUSTER_IMPL Arena* thread_arena()
{
    return thread->arena;
}

[[gnu::cold]] BUSTER_LOCAL ThreadReturnType thread_os_entry_point(void* context)
{
    thread = (Thread*)context;
    thread->arena = arena_create((ArenaInitialization){});
#if BUSTER_USE_IO_RING
    io_ring_init(&thread->ring, 4096);
#endif
    let os_result = thread->entry_point();
    return (ThreadReturnType)(u64)os_result;
}

[[gnu::cold]] BUSTER_LOCAL bool is_debugger_present()
{
    if (BUSTER_UNLIKELY(!program_state->is_debugger_present_called))
    {
        program_state->is_debugger_present_called = true;
#if defined(__linux__)
        let os_result = ptrace(PTRACE_TRACEME, 0, 0, 0) == -1;
        program_state->_is_debugger_present = os_result != 0;
#elif defined(__APPLE__)
#elif defined(_WIN32)
        let os_result = IsDebuggerPresent();
        program_state->_is_debugger_present = os_result != 0;
#else
    BUSTER_TRAP();
#endif
    }

    return program_state->_is_debugger_present;
}

[[noreturn]] [[gnu::cold]] BUSTER_IMPL void fail()
{
    if (is_debugger_present())
    {
        BUSTER_TRAP();
    }

    os_exit(1);
}

BUSTER_IMPL String8 string8_format_integer(Arena* arena, FormatIntegerOptions options, bool zero_terminate)
{
    char8 buffer[128];
    let stack_string = string8_format_integer_stack((String8){ buffer, BUSTER_ARRAY_LENGTH(buffer) }, options);
    return arena_duplicate_string8(arena, stack_string, zero_terminate);
}

#if 0
BUSTER_IMPL IntegerParsing parse_hexadecimal_vectorized(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;
    BUSTER_UNUSED(p);

    while (1)
    {
        // let ch = p[i];
        //
        // if (!is_hexadecimal(ch))
        // {
        //     break;
        // }
        //
        // i += 1;
        // value = accumulate_hexadecimal(value, ch);
        BUSTER_TRAP();
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing parse_decimal_vectorized(const char* restrict p)
{
    let zero = _mm512_set1_epi8('0');
    let nine = _mm512_set1_epi8('9');
    let chunk = _mm512_loadu_epi8(&p[0]);
    let lower_limit = _mm512_cmpge_epu8_mask(chunk, zero);
    let upper_limit = _mm512_cmple_epu8_mask(chunk, nine);
    let is = _kand_mask64(lower_limit, upper_limit);

    let digit_count = _tzcnt_u64(~_cvtmask64_u64(is));

    let digit_mask = _cvtu64_mask64((1ULL << digit_count) - 1);
    let digit2bin = _mm512_maskz_sub_epi8(digit_mask, chunk, zero);
    //let lo0 = _mm512_castsi512_si128(digit2bin);
    //let a = _mm512_cvtepu8_epi64(lo0);
    let digit_count_splat = _mm512_set1_epi8((u8)digit_count);

    let to_sub = _mm512_set_epi8(
            64, 63, 62, 61, 60, 59, 58, 57,
            56, 55, 54, 53, 52, 51, 50, 49,
            48, 47, 46, 45, 44, 43, 42, 41,
            40, 39, 38, 37, 36, 35, 34, 33,
            32, 31, 30, 29, 28, 27, 26, 25,
            24, 23, 22, 21, 20, 19, 18, 17,
            16, 15, 14, 13, 12, 11, 10, 9,
            8, 7, 6, 5, 4, 3, 2, 1);
    let ib = _mm512_maskz_sub_epi8(digit_mask, digit_count_splat, to_sub);
    let asds = _mm512_maskz_permutexvar_epi8(digit_mask, ib, digit2bin);

    let a128_0_0 = _mm512_extracti64x2_epi64(asds, 0);
    let a128_1_0 = _mm512_extracti64x2_epi64(asds, 1);

    let a128_0_1 = _mm_srli_si128(a128_0_0, 8);
    //let a128_1_1 = _mm_srli_si128(a128_1_0, 8);

    let a8_0_0 = _mm512_cvtepu8_epi64(a128_0_0);
    let a8_0_1 = _mm512_cvtepu8_epi64(a128_0_1);
    let a8_1_0 = _mm512_cvtepu8_epi64(a128_1_0);

    let powers_of_ten_0_0 = _mm512_set_epi64(
            10000000,
            1000000,
            100000,
            10000,
            1000,
            100,
            10,
            1);
    let powers_of_ten_0_1 = _mm512_set_epi64(
            1000000000000000,
            100000000000000,
            10000000000000,
            1000000000000,
            100000000000,
            10000000000,
            1000000000,
            100000000
            );
    let powers_of_ten_1_0 = _mm512_set_epi64(
            0,
            0,
            0,
            0,
            10000000000000000000ULL,
            1000000000000000000,
            100000000000000000,
            10000000000000000
            );

    let a0_0 = _mm512_mullo_epi64(a8_0_0, powers_of_ten_0_0);
    let a0_1 = _mm512_mullo_epi64(a8_0_1, powers_of_ten_0_1);
    let a1_0 = _mm512_mullo_epi64(a8_1_0, powers_of_ten_1_0);

    let add = _mm512_add_epi64(_mm512_add_epi64(a0_0, a0_1), a1_0);
    let reduce_add = _mm512_reduce_add_epi64(add);
    let value = reduce_add;

    return (IntegerParsing){ .value = value, .i = digit_count };
}

BUSTER_IMPL IntegerParsing parse_octal_vectorized(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;
    BUSTER_UNUSED(p);

    while (1)
    {
        // let chunk = _mm512_loadu_epi8(&p[i]);
        // let lower_limit = _mm512_cmpge_epu8_mask(chunk, _mm512_set1_epi8('0'));
        // let upper_limit = _mm512_cmple_epu8_mask(chunk, _mm512_set1_epi8('7'));
        // let is_octal = _kand_mask64(lower_limit, upper_limit);
        // let octal_mask = _cvtu64_mask64(_tzcnt_u64(~_cvtmask64_u64(is_octal)));

        BUSTER_TRAP();

        // if (!is_octal(ch))
        // {
        //     break;
        // }
        //
        // i += 1;
        // value = accumulate_octal(value, ch);
    }

    return (IntegerParsing) { .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing parse_binary_vectorized(const char* restrict f)
{
#if 0
    u64 value = 0;

    let chunk = _mm512_loadu_epi8(f);
    let zero = _mm512_set1_epi8('0');
    let is0 = _mm512_cmpeq_epu8_mask(chunk, zero);
    let is1 = _mm512_cmpeq_epu8_mask(chunk, _mm512_set1_epi8('1'));
    let is_binary_chunk = _kor_mask64(is0, is1);
    u64 i = _tzcnt_u64(~_cvtmask64_u64(is_binary_chunk));
    let digit2bin = _mm512_maskz_sub_epi8(is_binary_chunk, chunk, zero);
    let rotated = _mm512_permutexvar_epi8(digit2bin,
            _mm512_set_epi8(
                0, 1, 2, 3, 4, 5, 6, 7,
                8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23,
                24, 25, 26, 27, 28, 29, 30, 31,
                32, 33, 34, 35, 36, 37, 38, 39,
                40, 41, 42, 43, 44, 45, 46, 47,
                48, 49, 50, 51, 52, 53, 54, 55,
                56, 57, 58, 59, 60, 61, 62, 63
                ));
    let mask = _mm512_test_epi8_mask(rotated, rotated);
    let mask_int = _cvtmask64_u64(mask);

    return (IntegerParsing) { .value = value, .i = i };
#else
    BUSTER_UNUSED(f);
    BUSTER_TRAP();
#endif
}
#endif

BUSTER_LOCAL u64 accumulate_hexadecimal(u64 accumulator, u8 ch)
{
    u8 value;

    if (character_is_decimal(ch))
    {
        value = ch - '0';
    }
    else if (character_is_hexadecimal_alpha_upper(ch))
    {
        value = ch - 'A' + 10;
    }
    else if (character_is_hexadecimal_alpha_lower(ch))
    {
        value = ch - 'a' + 10;
    }
    else
    {
        BUSTER_UNREACHABLE();
    }

    return (accumulator * 16) + value;
}

BUSTER_LOCAL u64 accumulate_decimal(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(character_is_decimal(ch));
    return (accumulator * 10) + (ch - '0');
}

BUSTER_LOCAL u64 accumulate_octal(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(character_is_octal(ch));

    return (accumulator * 8) + (ch - '0');
}

BUSTER_LOCAL u64 accumulate_binary(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(character_is_binary(ch));

    return (accumulator * 2) + (ch - '0');
}

BUSTER_IMPL u64 parse_integer_decimal_assume_valid(String8 string)
{
    u64 value = 0;

    for (u64 i = 0; i < string.length; i += 1)
    {
        let ch = string.pointer[i];
        value = accumulate_decimal(value, ch);
    }

    return value;
}

BUSTER_IMPL IntegerParsing string8_parse_hexadecimal(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_hexadecimal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_hexadecimal(value, ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string16_parse_hexadecimal(const char16* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_hexadecimal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_hexadecimal(value, (u8)ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string8_parse_decimal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_decimal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_decimal(value, ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string16_parse_decimal(const char16* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_decimal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_decimal(value, (u8)ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string8_parse_octal(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_octal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_octal(value, ch);
    }

    return (IntegerParsing) { .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string16_parse_octal(const char16* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_octal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_octal(value, (u8)ch);
    }

    return (IntegerParsing) { .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string8_parse_binary(const char8* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_binary(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_binary(value, ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing string16_parse_binary(const char16* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!character_is_binary(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_binary(value, (u8)ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

#if defined(_WIN32)
BUSTER_LOCAL ThreadHandle* os_windows_thread_to_generic(HANDLE handle)
{
    BUSTER_CHECK(handle != 0);
    return (ThreadHandle*)handle;
}

BUSTER_LOCAL HANDLE os_windows_thread_from_generic(ThreadHandle* handle)
{
    BUSTER_CHECK(handle != 0);
    return (HANDLE)handle;
}
#endif

#if defined(__linux__) || defined(__APPLE__)
BUSTER_LOCAL ThreadHandle* os_posix_thread_to_generic(pthread_t handle)
{
    BUSTER_CHECK(handle != 0);
    return (ThreadHandle*)handle;
}

BUSTER_LOCAL pthread_t os_posix_thread_from_generic(ThreadHandle* handle)
{
    BUSTER_CHECK(handle != 0);
    return (pthread_t)handle;
}
#endif

BUSTER_IMPL ThreadHandle* os_thread_create(ThreadCallback* callback, ThreadCreateOptions options)
{
    BUSTER_UNUSED(options);
    ThreadHandle* result = 0;
#if defined (__linux__) || defined(__APPLE__)
#if BUSTER_USE_PTHREAD
    pthread_t handle;
    let create_result = pthread_create(&handle, 0, callback, 0);
    bool os_result = create_result == 0;
    handle = os_result ? handle : 0;
    result = os_posix_thread_to_generic(handle);
#else
    BUSTER_UNUSED(callback);
#endif
#elif defined (_WIN32)
    HANDLE handle = CreateThread(0, 0, callback, 0, 0, 0);
    result = os_windows_thread_to_generic(handle);
#endif
    return result;
}

BUSTER_IMPL u32 os_thread_join(ThreadHandle* handle)
{
    u32 return_code = 1;

#if defined(__linux__) || defined(__APPLE__)
#if BUSTER_USE_PTHREAD
    let pthread = os_posix_thread_from_generic(handle);
    void* void_return_value = 0;
    let join_result = pthread_join(pthread, &void_return_value);
    if (join_result == 0)
    {
        return_code = (u32)(u64)void_return_value;
    }
#else
    BUSTER_UNUSED(handle);
#endif
#elif defined(_WIN32)
    let thread_handle = os_windows_thread_from_generic(handle);
    WaitForSingleObject(thread_handle, INFINITE);

    DWORD exit_code;
    let result = GetExitCodeThread(thread_handle, &exit_code) != 0;
    if (result)
    {
        return_code = (u32)exit_code;
    }

    CloseHandle(thread_handle);
#endif

    return return_code;
}

BUSTER_IMPL u64 next_power_of_two(u64 n)
{
    n -= 1;

    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;

    n += 1;

    return n;
}

BUSTER_IMPL String8 string8_from_pointer(const char8* pointer)
{
    return (String8){(char8*)pointer, string8_length(pointer)};
}

BUSTER_IMPL String16 string16_from_pointer(const char16* pointer)
{
    return (String16){(char16*)pointer, string16_length(pointer)};
}

BUSTER_IMPL String16 string16_from_pointer_length(const char16* pointer, u64 length)
{
    return (String16){(char16*)pointer, length};
}

BUSTER_IMPL String8 string8_from_pointers(const char8* start, const char8* end)
{
    BUSTER_CHECK(end >= start);
    let len = (u64)(end - start);
    return (String8) { (char*)start, len };
}

BUSTER_IMPL String8 string8_from_pointer_length(const char8* pointer, u64 len)
{
    return (String8) { (char8*)pointer, len };
}

BUSTER_IMPL String8 string8_from_pointer_unsigned_length(const char8* pointer, u64 len)
{
    return (String8) { (char8*)pointer, len };
}

BUSTER_IMPL String8 string8_from_pointer_start_end(const char8* pointer, u64 start, u64 end)
{
    return (String8) { (char8*)pointer + start, end - start };
}

BUSTER_IMPL String8 string8_slice_start(String8 s, u64 start)
{
    s.pointer += start;
    s.length -= start;
    return s;
}

BUSTER_IMPL String16 string16_slice_start(String16 s, u64 start)
{
    s.pointer += start;
    s.length -= start;
    return s;
}

BUSTER_IMPL bool memory_compare(void* a, void* b, u64 i)
{
    BUSTER_CHECK(a != b);
    bool result = 1;

    let p1 = (u8*)a;
    let p2 = (u8*)b;

    while (i--)
    {
        bool is_equal = *p1 == *p2;
        if (!is_equal)
        {
            result = 0;
            break;
        }

        p1 += 1;
        p2 += 1;
    }

    return result;
}

BUSTER_IMPL String8 string8_slice(String8 s, u64 start, u64 end)
{
    s.pointer += start;
    s.length = end - start;
    return s;
}

BUSTER_IMPL String16 string16_slice(String16 s, u64 start, u64 end)
{
    s.pointer += start;
    s.length = end - start;
    return s;
}

BUSTER_LOCAL bool string_generic_equal(void* p1, void* p2, u64 l1, u64 l2, u64 element_size)
{
    bool is_equal = l1 == l2;
    if (is_equal & (l1 != 0) & (p1 != p2))
    {
        is_equal = memory_compare(p1, p2, l1 * element_size);
    }

    return is_equal;
}

BUSTER_IMPL bool string8_equal(String8 s1, String8 s2)
{
    return string_generic_equal(s1.pointer, s2.pointer, s1.length, s2.length, sizeof(*s1.pointer));
}

BUSTER_IMPL bool string16_equal(String16 s1, String16 s2)
{
    return string_generic_equal(s1.pointer, s2.pointer, s1.length, s2.length, sizeof(*s1.pointer));
}

BUSTER_IMPL u64 string8_first_character(String8 s, char8 ch)
{
    let result = string_no_match;

    for (u64 i = 0; i < s.length; i += 1)
    {
        if (ch == s.pointer[i])
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_IMPL u64 string8_first_ocurrence(String8 s, String8 sub)
{
    u64 result = string_no_match;

    if (sub.length && s.length >= sub.length)
    {
        u64 i = 0;

        while (true)
        {
            let s_sub = string8_slice_start(s, i);
            let index = string8_first_character(s_sub, sub.pointer[0]);
            if (index == string_no_match)
            {
                break;
            }

            let candidate_result = i + index;
            let s_sub_sub = string8_slice_start(s, candidate_result);
            if (s_sub_sub.length < sub.length)
            {
                break;
            }
            s_sub_sub = string8_slice(s_sub_sub, 0, sub.length);
            
            if (string8_equal(s_sub_sub, sub))
            {
                result = candidate_result;
                break;
            }

            i = candidate_result + 1;
        }
    }

    return result;
}

BUSTER_IMPL u64 string16_first_ocurrence(String16 s, String16 sub)
{
    u64 result = string_no_match;

    if (sub.length && s.length >= sub.length)
    {
        u64 i = 0;

        while (true)
        {
            let s_sub = string16_slice_start(s, i);
            let index = string16_first_character(s_sub, sub.pointer[0]);
            if (index == string_no_match)
            {
                break;
            }

            let candidate_result = i + index;
            let s_sub_sub = string16_slice_start(s, candidate_result);
            if (s_sub_sub.length < sub.length)
            {
                break;
            }
            s_sub_sub = string16_slice(s_sub_sub, 0, sub.length);
            
            if (string16_equal(s_sub_sub, sub))
            {
                result = candidate_result;
                break;
            }

            i = candidate_result + 1;
        }
    }

    return result;
}

BUSTER_IMPL u64 string16_first_character(String16 s, char16 ch)
{
    let result = string_no_match;

    for (u64 i = 0; i < s.length; i += 1)
    {
        if (ch == s.pointer[i])
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_IMPL u64 raw_string16_first_character(const char16* s, char16 ch)
{
    let result = string_no_match;

    for (u64 i = 0; s[i]; i += 1)
    {
        if (ch == s[i])
        {
            result = i;
            break;
        }
    }

    return result;
}

BUSTER_IMPL u64 string8_last_character(String8 s, char8 ch)
{
    let result = string_no_match;

    let pointer = s.pointer + s.length;

    do
    {
        pointer -= 1;
        if (*pointer == ch)
        {
            result = (u64)(pointer - s.pointer);
            break;
        }
    } while (pointer - s.pointer);

    return result;
}

BUSTER_IMPL bool string8_ends_with(String8 s, String8 ending)
{
    bool result = (ending.length <= s.length);
    if (result)
    {
        String8 last_chunk = { s.pointer + (s.length - ending.length), ending.length };
        result = string8_equal(last_chunk, ending);
    }

    return result;
}

BUSTER_IMPL bool string8_starts_with(String8 s, String8 beginning)
{
    bool result = (beginning.length <= s.length);

    if (result)
    {
        String8 first_chunk = { s.pointer , beginning.length };
        result = string8_equal(first_chunk, beginning);
    }

    return result;
}

BUSTER_IMPL bool string16_starts_with(String16 s, String16 beginning)
{
    bool result = (beginning.length <= s.length);

    if (result)
    {
        String16 first_chunk = { s.pointer , beginning.length };
        result = string16_equal(first_chunk, beginning);
    }

    return result;
}

BUSTER_IMPL u64 align_forward(u64 n, u64 a)
{
    let mask = a - 1;
    let result = (n + mask) & ~mask;
    return result;
}

BUSTER_IMPL void print_raw(String8 str)
{
    os_file_write(os_get_stdout(), string_to_byte_slice(str));
}

STRUCT(StringFormatResult)
{
    u64 real_buffer_index;
    u64 needed_code_unit_count;
    u64 real_format_index;
};

BUSTER_IMPL StringFormatResult string8_format_to_memory(String8 buffer_slice, String8 format, va_list variable_arguments)
{
    StringFormatResult result = {};
    bool is_illformed_string = buffer_slice.pointer == 0 && buffer_slice.length != 0;
    u64 format_index = 0;

    if (is_illformed_string)
    {
        result.real_buffer_index = buffer_slice.length;
    }

    while (format_index < format.length)
    {
        while (format_index != format.length && format.pointer[format_index] != '{')
        {
            if (result.real_buffer_index < buffer_slice.length)
            {
                buffer_slice.pointer[result.real_buffer_index] = format.pointer[result.real_format_index];
                result.real_buffer_index += 1;
                result.real_format_index += 1;
            }

            result.needed_code_unit_count += 1;
            format_index += 1;
        }

        if (format.pointer[format_index] == '{')
        {
            char8 format_buffer[128];
            u64 format_buffer_i;

            for (format_buffer_i = 0; format.pointer[format_index] != '}'; format_buffer_i += 1, format_index += 1)
            {
                format_buffer[format_buffer_i] = format.pointer[format_index];
            }

            format_buffer[format_buffer_i] = '}';
            format_buffer_i += 1;
            format_index += 1;

            let whole_format_string = string8_from_pointer_length(format_buffer, format_buffer_i);
            ENUM(Format, 
                    FORMAT_OS_STRING,
                    FORMAT_OS_CHAR,
                    FORMAT_STRING8,
                    FORMAT_STRING16,
                    FORMAT_STRING32,
                    FORMAT_UNSIGNED_INTEGER_8,
                    FORMAT_UNSIGNED_INTEGER_16,
                    FORMAT_UNSIGNED_INTEGER_32,
                    FORMAT_UNSIGNED_INTEGER_64,
                    FORMAT_UNSIGNED_INTEGER_128,
                    FORMAT_SIGNED_INTEGER_8,
                    FORMAT_SIGNED_INTEGER_16,
                    FORMAT_SIGNED_INTEGER_32,
                    FORMAT_SIGNED_INTEGER_64,
                    FORMAT_SIGNED_INTEGER_128,
                    FORMAT_INTEGER_COUNT,
                );
            String8 possible_format_strings[FORMAT_INTEGER_COUNT] = {
                [FORMAT_OS_STRING] = S8("OsS"),
                [FORMAT_OS_CHAR] = S8("OsC"),
                [FORMAT_STRING8] = S8("S8"),
                [FORMAT_STRING16] = S8("S16"),
                [FORMAT_STRING32] = S8("S32"),
                [FORMAT_UNSIGNED_INTEGER_8] = S8("u8"),
                [FORMAT_UNSIGNED_INTEGER_16] = S8("u16"),
                [FORMAT_UNSIGNED_INTEGER_32] = S8("u32"),
                [FORMAT_UNSIGNED_INTEGER_64] = S8("u64"),
                [FORMAT_UNSIGNED_INTEGER_128] = S8("u128"),
                [FORMAT_SIGNED_INTEGER_8] = S8("s8"),
                [FORMAT_SIGNED_INTEGER_16] = S8("s16"),
                [FORMAT_SIGNED_INTEGER_32] = S8("s32"),
                [FORMAT_SIGNED_INTEGER_64] = S8("s64"),
                [FORMAT_SIGNED_INTEGER_128] = S8("s128"),
            };

            let first_format = string8_first_character(whole_format_string, ':');
            bool there_is_format_modifiers = first_format != string_no_match;
            let this_format_string_length = there_is_format_modifiers ? first_format : whole_format_string.length - 1; // Avoid final right brace
            let this_format_string = string8_slice(whole_format_string,
                    1, // Avoid starting left brace
                    this_format_string_length);

            u64 i;
            for (i = 0; i < BUSTER_ARRAY_LENGTH(possible_format_strings); i += 1)
            {
                if (string8_equal(this_format_string, possible_format_strings[i]))
                {
                    break;
                }
            }

            ENUM(IntegerFormatKind,
                FORMAT_KIND_DECIMAL,
                FORMAT_KIND_BINARY,
                FORMAT_KIND_OCTAL,
                FORMAT_KIND_HEXADECIMAL_LOWER,
                FORMAT_KIND_HEXADECIMAL_UPPER,
                FORMAT_KIND_COUNT,
            );

            ENUM(FormatSpecifier,
                FORMAT_SPECIFIER_D,
                FORMAT_SPECIFIER_X_UPPER,
                FORMAT_SPECIFIER_X_LOWER,
                FORMAT_SPECIFIER_O,
                FORMAT_SPECIFIER_B,
                FORMAT_SPECIFIER_WIDTH,
                FORMAT_SPECIFIER_NO_PREFIX,
                FORMAT_SPECIFIER_DIGIT_GROUP,

                FORMAT_SPECIFIER_COUNT,
            );

            let this_format = (Format)i;
            bool prefix = false;
            bool prefix_set = false;
            bool digit_group = false;
            u64 width = 0;
            char8 width_character = '0';
            bool width_natural_extension = false;
            constexpr u64 max_width = 64;

            IntegerFormatKind format_kind = FORMAT_KIND_DECIMAL;
            bool integer_format_set = false;

            if (there_is_format_modifiers)
            {
                String8 possible_format_specifier_strings[] = {
                    [FORMAT_SPECIFIER_D] = S8("d"),
                    [FORMAT_SPECIFIER_X_UPPER] = S8("X"),
                    [FORMAT_SPECIFIER_X_LOWER] = S8("x"),
                    [FORMAT_SPECIFIER_O] = S8("o"),
                    [FORMAT_SPECIFIER_B] = S8("b"),
                    [FORMAT_SPECIFIER_WIDTH] = S8("width"),
                    [FORMAT_SPECIFIER_NO_PREFIX] = S8("no_prefix"),
                    [FORMAT_SPECIFIER_DIGIT_GROUP] = S8("digit_group"),
                };
                static_assert(BUSTER_ARRAY_LENGTH(possible_format_specifier_strings) == FORMAT_SPECIFIER_COUNT);

                let whole_format_specifiers_string = string8_slice(whole_format_string, first_format + 1, whole_format_string.length - 1);
                BUSTER_CHECK(whole_format_specifiers_string.length <= whole_format_string.length);
                u64 format_specifier_string_i = 0;

                while (format_specifier_string_i < whole_format_specifiers_string.length && whole_format_specifiers_string.pointer[format_specifier_string_i] != '}')
                {
                    let iteration_left_format_specifiers_string = string8_slice_start(whole_format_specifiers_string, format_specifier_string_i);
                    BUSTER_CHECK(iteration_left_format_specifiers_string.length <= whole_format_specifiers_string.length);
                    let equal_index = string8_first_character(iteration_left_format_specifiers_string, '=');
                    let comma_index = string8_first_character(iteration_left_format_specifiers_string, ',');
                    let format_specifier_name_end = BUSTER_MIN(equal_index, comma_index);
                    let string_left = format_specifier_name_end == string_no_match;
                    format_specifier_name_end = string_left ? iteration_left_format_specifiers_string.length : format_specifier_name_end;
                    let next_character = string_left ? 0 : (equal_index < comma_index ? '=' : ',');

                    let format_name = string8_slice(iteration_left_format_specifiers_string, 0, format_specifier_name_end);
                    format_specifier_string_i += format_name.length + !string_left;
                    let left_format_specifiers_string = string8_slice_start(iteration_left_format_specifiers_string, format_name.length + !string_left);
                    BUSTER_CHECK(left_format_specifiers_string.length <= iteration_left_format_specifiers_string.length);

                    u64 format_i;
                    for (format_i = 0; format_i < FORMAT_SPECIFIER_COUNT; format_i += 1)
                    {
                        let candidate_format_specifier = possible_format_specifier_strings[format_i];
                        if (string8_equal(format_name, candidate_format_specifier))
                        {
                            break;
                        }
                    }

                    let format_specifier = (FormatSpecifier)format_i;
                    switch (format_specifier)
                    {
                        break; case FORMAT_SPECIFIER_D:
                        {
                            format_kind = FORMAT_KIND_DECIMAL;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_X_UPPER:
                        {
                            format_kind = FORMAT_KIND_HEXADECIMAL_UPPER;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_X_LOWER:
                        {
                            format_kind = FORMAT_KIND_HEXADECIMAL_LOWER;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_O:
                        {
                            format_kind = FORMAT_KIND_OCTAL;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_B:
                        {
                            format_kind = FORMAT_KIND_BINARY;
                            integer_format_set = true;
                        }
                        break; case FORMAT_SPECIFIER_WIDTH:
                        {
                            if (next_character == '=')
                            {
                                if (left_format_specifiers_string.pointer[0] == '[')
                                {
                                    width_character = left_format_specifiers_string.pointer[1];

                                    if (left_format_specifiers_string.pointer[2] == ',')
                                    {
                                        let right_bracket_index = string8_first_character(left_format_specifiers_string, ']');

                                        if (right_bracket_index != string_no_match)
                                        {
                                            u64 width_start = 3;
                                            let width_count_string = string8_slice(left_format_specifiers_string, width_start, right_bracket_index);
                                            u64 character_to_advance_count = right_bracket_index + 1;

                                            bool success = false;

                                            if (width_count_string.length == 1 && width_count_string.pointer[0] == 'x')
                                            {
                                                width_natural_extension = true;
                                                success = true;
                                                width = max_width;
                                            }
                                            else
                                            {
                                                let width_count_parsing = string8_parse_decimal(width_count_string.pointer);

                                                if (width_count_parsing.i == width_count_string.length && width_count_parsing.value != 0)
                                                {
                                                    width = width_count_parsing.value;

                                                    bool more_characters = right_bracket_index + 1 < left_format_specifiers_string.length;
                                                    if (more_characters)
                                                    {
                                                        let next_ch = left_format_specifiers_string.pointer[character_to_advance_count];
                                                        if (next_ch == ',')
                                                        {
                                                            character_to_advance_count += 1;
                                                            success = true;
                                                        }
                                                        else
                                                        {
                                                            fail();
                                                        }
                                                    }
                                                    else
                                                    {
                                                        success = true;
                                                    }

                                                    if (!success)
                                                    {
                                                        fail();
                                                    }
                                                }
                                            }

                                            if (success)
                                            {
                                                format_specifier_string_i += character_to_advance_count;
                                            }
                                            else
                                            {
                                                fail();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        break; case FORMAT_SPECIFIER_NO_PREFIX:
                        {
                            prefix = false;
                            prefix_set = true;
                        }
                        break; case FORMAT_SPECIFIER_DIGIT_GROUP:
                        {
                            digit_group = true;
                        }
                        break; case FORMAT_SPECIFIER_COUNT:
                        {
                        }
                    }
                }

                if (!prefix_set && integer_format_set)
                {
                    prefix = true;
                }

                if (!prefix_set && width && width_character == ' ')
                {
                    prefix = false;
                }
            }

            bool written = false;
            if (width > max_width)
            {
                width = max_width;
            }

            switch (this_format)
            {
                break; case FORMAT_OS_STRING:
                {
                    // TODO:
                    let string = va_arg(variable_arguments, OsString);
                    // TODO: compute proper size
                    let size = string_size(string);
                    if (result.real_buffer_index + size <= buffer_slice.length)
                    {
                        for (u64 string_i = 0; string_i < string.length; string_i += 1, result.needed_code_unit_count += 1)
                        {
                            if (result.real_buffer_index < buffer_slice.length)
                            {
                                buffer_slice.pointer[result.real_buffer_index] = (char8)string.pointer[string_i];
                                result.real_buffer_index += 1;
                            }
                        }
                    }
                }
                break; case FORMAT_OS_CHAR:
                {
                    // TODO:
                    let os_char = (OsChar)va_arg(variable_arguments, u32);
                    if (result.real_buffer_index < buffer_slice.length)
                    {
                        buffer_slice.pointer[result.real_buffer_index] = (char8)os_char;
                        result.real_buffer_index += 1;
                    }

                    result.needed_code_unit_count += 1;
                }
                break; case FORMAT_STRING8:
                {
                    let string = va_arg(variable_arguments, String8);
                    written = result.real_buffer_index + string.length <= buffer_slice.length;
                    if (written)
                    {
                        memcpy(&buffer_slice.pointer[result.real_buffer_index], string.pointer, string.length);
                        result.real_buffer_index += string.length;
                    }

                    result.needed_code_unit_count += string.length;
                }
                break; case FORMAT_STRING16:
                {
                    // TODO:
                    let string = va_arg(variable_arguments, String16);
                    for (u64 string_i = 0; string_i < string.length; string_i += 1, result.needed_code_unit_count += 1)
                    {
                        if (result.real_buffer_index < buffer_slice.length)
                        {
                            buffer_slice.pointer[result.real_buffer_index] = (char8)string.pointer[string_i];
                            result.real_buffer_index += 1;
                        }
                    }
                }
                break; case FORMAT_STRING32:
                {
                    // TODO:
                    let string = va_arg(variable_arguments, String32);
                    for (u64 string_i = 0; string_i < string.length; string_i += 1, result.needed_code_unit_count += 1)
                    {
                        if (result.real_buffer_index < buffer_slice.length)
                        {
                            buffer_slice.pointer[result.real_buffer_index] = (char8)string.pointer[string_i];
                            result.real_buffer_index += 1;
                        }
                    }
                }
                break; case FORMAT_UNSIGNED_INTEGER_8: case FORMAT_UNSIGNED_INTEGER_16: case FORMAT_UNSIGNED_INTEGER_32: case FORMAT_UNSIGNED_INTEGER_64:
                {
                    u8 prefix_buffer[2] = {};
                    prefix = prefix && format_kind != FORMAT_KIND_COUNT;

                    char8 prefix_second_character;
                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL: prefix_second_character = 'd';
                        break; case FORMAT_KIND_BINARY: prefix_second_character = 'b';
                        break; case FORMAT_KIND_OCTAL: prefix_second_character = 'o';
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER: prefix_second_character = 'x';
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    if (prefix)
                    {
                        prefix_buffer[0] = '0';


                        prefix_buffer[1] = prefix_second_character;
                    }

                    if (format_kind == FORMAT_KIND_COUNT)
                    {
                        format_kind = FORMAT_KIND_DECIMAL;
                    }

                    u64 value;
                    u64 value_size;
                    switch (this_format)
                    {
                        break; case FORMAT_UNSIGNED_INTEGER_8:
                        {
                            value = (u8)(va_arg(variable_arguments, u32) & UINT8_MAX);
                            value_size = sizeof(u8);
                        }
                        break; case FORMAT_UNSIGNED_INTEGER_16:
                        {
                            value = (u16)(va_arg(variable_arguments, u32) & UINT16_MAX);
                            value_size = sizeof(u16);
                        }
                        break; case FORMAT_UNSIGNED_INTEGER_32:
                        {
                            value = va_arg(variable_arguments, u32);
                            value_size = sizeof(u32);
                        }
                        break; case FORMAT_UNSIGNED_INTEGER_64:
                        {
                            value = va_arg(variable_arguments, u64);
                            value_size = sizeof(u64);
                        }
                        break; default: BUSTER_UNREACHABLE();
                    }

                    let prefix_character_count = (u64)prefix << 1;
                    char8 integer_format_buffer[(sizeof(u64) * 8) + max_width + 2];
                    let number_string_buffer = BUSTER_ARRAY_TO_SLICE(String8, integer_format_buffer);

                    String8 format_result;

                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL: format_result = string8_format_integer_decimal(number_string_buffer, value, false);
                        break; case FORMAT_KIND_BINARY: format_result = string8_format_integer_binary(number_string_buffer, value);
                        break; case FORMAT_KIND_OCTAL: format_result = string8_format_integer_octal(number_string_buffer, value);
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string8_format_integer_hexadecimal(number_string_buffer, value, format_kind == FORMAT_KIND_HEXADECIMAL_UPPER);
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    number_string_buffer.length = format_result.length;

                    u64 integer_max_width = 0;

                    u64 digit_group_character_count;

                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL:
                        {
                            prefix_second_character = 'd';
                            switch (this_format)
                            {
                                break; case FORMAT_UNSIGNED_INTEGER_8: integer_max_width = 3;
                                break; case FORMAT_UNSIGNED_INTEGER_16: integer_max_width = 5;
                                break; case FORMAT_UNSIGNED_INTEGER_32: integer_max_width = 10;
                                break; case FORMAT_UNSIGNED_INTEGER_64: integer_max_width = 39;
                                break; default: BUSTER_UNREACHABLE();
                            }
                            digit_group_character_count = 3;
                        }
                        break; case FORMAT_KIND_BINARY:
                        {
                            prefix_second_character = 'b';
                            integer_max_width = value_size * 8;
                            digit_group_character_count = 8;
                        }
                        break; case FORMAT_KIND_OCTAL:
                        {
                            prefix_second_character = 'o';
                            integer_max_width = value_size * 4;
                            digit_group_character_count = 4;
                        }
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER:
                        {
                            prefix_second_character = 'x';
                            integer_max_width = value_size * 2;
                            digit_group_character_count = 2;
                        }
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    width = width ? (width_natural_extension ? integer_max_width : width) : 0;

                    u64 width_character_count = width ? (width > number_string_buffer.length ? (width - number_string_buffer.length) : (number_string_buffer.length - width)) : 0;

                    u64 character_to_write_count = prefix_character_count + width_character_count + number_string_buffer.length;

                    written = result.real_buffer_index + character_to_write_count <= buffer_slice.length;

                    if (written)
                    {
                        if (prefix)
                        {
                            buffer_slice.pointer[result.real_buffer_index + 0] = prefix_buffer[0];
                            buffer_slice.pointer[result.real_buffer_index + 1] = prefix_buffer[1];

                            result.real_buffer_index += 2;
                        }

                        if (width_character_count)
                        {
                            memset(buffer_slice.pointer + result.real_buffer_index, width_character, width_character_count);
                            result.real_buffer_index += width_character_count;
                        }


                        if (digit_group && number_string_buffer.length > digit_group_character_count)
                        {
                            fail();
                        }
                        else
                        {
                            memcpy(buffer_slice.pointer + result.real_buffer_index, number_string_buffer.pointer, string_size(number_string_buffer));
                            result.real_buffer_index += number_string_buffer.length;
                        }
                    }

                    result.needed_code_unit_count += character_to_write_count;
                }
                break; case FORMAT_SIGNED_INTEGER_8: case FORMAT_SIGNED_INTEGER_16: case FORMAT_SIGNED_INTEGER_32: case FORMAT_SIGNED_INTEGER_64:
                {
                    if (format_kind == FORMAT_KIND_COUNT)
                    {
                        format_kind = FORMAT_KIND_DECIMAL;
                    }

                    s64 value;
                    switch (this_format)
                    {
                        break; case FORMAT_SIGNED_INTEGER_8: value = (s8)(va_arg(variable_arguments, s32) & INT8_MAX);
                        break; case FORMAT_SIGNED_INTEGER_16: value = (s16)(va_arg(variable_arguments, s32) & INT16_MAX);
                        break; case FORMAT_SIGNED_INTEGER_32: value = va_arg(variable_arguments, s32);
                        break; case FORMAT_SIGNED_INTEGER_64: value = va_arg(variable_arguments, s64);
                        break; default: BUSTER_UNREACHABLE();
                    }

                    char8 integer_format_buffer[sizeof(u64) * 8 + 1]; // 1 for the sign (needed?)
                    let string_buffer = BUSTER_ARRAY_TO_SLICE(String8, integer_format_buffer);
                    String8 format_result;

                    switch (format_kind)
                    {
                        break; case FORMAT_KIND_DECIMAL: format_result = string8_format_integer_decimal(string_buffer, (u64)value, true);
                        break; case FORMAT_KIND_BINARY: format_result = string8_format_integer_binary(string_buffer, (u64)value);
                        break; case FORMAT_KIND_OCTAL: format_result = string8_format_integer_octal(string_buffer, (u64)value);
                        break; case FORMAT_KIND_HEXADECIMAL_LOWER: case FORMAT_KIND_HEXADECIMAL_UPPER: format_result = string8_format_integer_hexadecimal(string_buffer, (u64)value, format_kind == FORMAT_KIND_HEXADECIMAL_UPPER);
                        break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
                    }

                    written = result.real_buffer_index + format_result.length <= buffer_slice.length;

                    if (written)
                    {
                        memcpy(buffer_slice.pointer + result.real_buffer_index, format_result.pointer, string_size(format_result));
                        result.real_buffer_index += format_result.length;
                    }

                    result.needed_code_unit_count += format_result.length;
                }
                break; case FORMAT_UNSIGNED_INTEGER_128:
                {
                    // TODO:
                }
                break; case FORMAT_SIGNED_INTEGER_128:
                {
                    // TODO:
                }
                break; case FORMAT_INTEGER_COUNT:
                {
                    if (result.real_buffer_index < buffer_slice.length)
                    {
                        buffer_slice.pointer[result.real_buffer_index] = '{';
                        result.real_buffer_index += 1;
                    }

                    result.needed_code_unit_count += 1;

                    let code_unit_to_write_count = whole_format_string.length + 1;

                    if (result.real_buffer_index + code_unit_to_write_count <= buffer_slice.length)
                    {
                        memcpy(buffer_slice.pointer + result.real_buffer_index, whole_format_string.pointer, string_size(whole_format_string));
                        buffer_slice.pointer[result.real_buffer_index + whole_format_string.length] = '}';
                        result.real_buffer_index += code_unit_to_write_count;
                    }

                    result.needed_code_unit_count += code_unit_to_write_count;
                }
            }

            if (written)
            {
                result.real_format_index += whole_format_string.length;
            }
        }
    }

    if (is_illformed_string)
    {
        result.real_buffer_index = buffer_slice.length;
    }

    return result;
}

BUSTER_IMPL String8 arena_string8_format(Arena* arena, bool null_terminate, String8 format, ...)
{
    String8 result = {};
    va_list variable_arguments;

    va_start(variable_arguments);
    StringFormatResult buffer_result = string8_format_to_memory((String8){}, format, variable_arguments);
    va_end(variable_arguments);

    let code_unit_count = buffer_result.needed_code_unit_count;
    let buffer = string8_from_pointer_length(arena_allocate(arena, char8, code_unit_count + null_terminate), code_unit_count);

    if (buffer.pointer)
    {
        va_start(variable_arguments);
        StringFormatResult final_result = string8_format_to_memory(buffer, format, variable_arguments);
        va_end(variable_arguments);

        if (final_result.needed_code_unit_count == code_unit_count)
        {
            if (null_terminate)
            {
                buffer.pointer[code_unit_count] = 0;
            }

            result = buffer;
        }
    }

    return result;
}

// BUSTER_LOCAL void string8_format_to_file(FileDescriptor* file_descriptor, String8 buffer_slice, String8 format, va_list variable_arguments, bool null_terminate)
// {
//     StringFormatResult result = {};
//     for (u64 format_index = 0, buffer_index = 0; format_index < format.length; format_index += result.real_format_index, buffer_index += result.real_buffer_index)
//     {
//         let iteration_buffer_slice = string8_slice_start(buffer_slice, buffer_index);
//         let iteration_format_slice = string8_slice_start(format, format_index);
//         result = string8_format_to_memory(iteration_buffer_slice, iteration_format_slice, variable_arguments, null_terminate);
//     }
// }

BUSTER_IMPL void print(String8 format, ...)
{
    BUSTER_UNUSED(format);
//     let it = format.pointer;
//     let top = it + format.length;
//     va_list va;
//     va_start(va);
//
//     char8 buffer[8192];
//     let buffer_slice = BUSTER_ARRAY_TO_SLICE(String8, buffer);
//     buffer_slice.length -= 1;
//     u64 buffer_i = 0;
//
//     while (it != top)
//     {
//         for (; it != top && *it != '{'; buffer_i += 1, it += 1)
//         {
//             if (buffer_slice.pointer)
//             {
//                 buffer_slice.pointer[buffer_i] = *it;
//             }
//         }
//
//         if (*it == '{')
//         {
//             it += 1;
//
//             char8 format_buffer[128];
//             u64 format_buffer_i;
//
//             for (format_buffer_i = 0; *it != '}'; format_buffer_i += 1, it += 1)
//             {
//                 format_buffer[format_buffer_i] = *it;
//             }
//
//             it += 1;
//
//             let whole_format_string = string8_from_pointer_length(format_buffer, format_buffer_i);
//             ENUM(Format, 
//                     FORMAT_OS_STRING,
//                     FORMAT_OS_CHAR,
//                     FORMAT_STRING8,
//                     FORMAT_STRING16,
//                     FORMAT_STRING32,
//                     FORMAT_UNSIGNED_INTEGER_8,
//                     FORMAT_UNSIGNED_INTEGER_16,
//                     FORMAT_UNSIGNED_INTEGER_32,
//                     FORMAT_UNSIGNED_INTEGER_64,
//                     FORMAT_UNSIGNED_INTEGER_128,
//                     FORMAT_SIGNED_INTEGER_8,
//                     FORMAT_SIGNED_INTEGER_16,
//                     FORMAT_SIGNED_INTEGER_32,
//                     FORMAT_SIGNED_INTEGER_64,
//                     FORMAT_SIGNED_INTEGER_128,
//                     FORMAT_INTEGER_COUNT,
//                 );
//             String8 possible_format_strings[FORMAT_INTEGER_COUNT] = {
//                 [FORMAT_OS_STRING] = S8("OsS"),
//                 [FORMAT_OS_CHAR] = S8("OsC"),
//                 [FORMAT_STRING8] = S8("S8"),
//                 [FORMAT_STRING16] = S8("S16"),
//                 [FORMAT_STRING32] = S8("S32"),
//                 [FORMAT_UNSIGNED_INTEGER_8] = S8("u8"),
//                 [FORMAT_UNSIGNED_INTEGER_16] = S8("u16"),
//                 [FORMAT_UNSIGNED_INTEGER_32] = S8("u32"),
//                 [FORMAT_UNSIGNED_INTEGER_64] = S8("u64"),
//                 [FORMAT_UNSIGNED_INTEGER_128] = S8("u128"),
//                 [FORMAT_SIGNED_INTEGER_8] = S8("s8"),
//                 [FORMAT_SIGNED_INTEGER_16] = S8("s16"),
//                 [FORMAT_SIGNED_INTEGER_32] = S8("s32"),
//                 [FORMAT_SIGNED_INTEGER_64] = S8("s64"),
//                 [FORMAT_SIGNED_INTEGER_128] = S8("s128"),
//             };
//
//             let first_format = string8_first_character(whole_format_string, ':');
//             bool is_format_kind = first_format != string_no_match;
//             let this_format_string_length = is_format_kind ? first_format : whole_format_string.length;
//             let this_format_string = string8_slice(whole_format_string, 0, this_format_string_length);
//
//             u64 i;
//             for (i = 0; i < BUSTER_ARRAY_LENGTH(possible_format_strings); i += 1)
//             {
//                 if (string8_equal(this_format_string, possible_format_strings[i]))
//                 {
//                     break;
//                 }
//             }
//
//             ENUM(FormatKind,
//                 FORMAT_KIND_DECIMAL,
//                 FORMAT_KIND_BINARY,
//                 FORMAT_KIND_OCTAL,
//                 FORMAT_KIND_HEXADECIMAL,
//                 FORMAT_KIND_COUNT,
//             );
//
//             let format = (Format)i;
//
//             FormatKind format_kind = FORMAT_KIND_DECIMAL;
//
//             if (is_format_kind)
//             {
//                 String8 possible_format_kind_strings[FORMAT_KIND_COUNT] = {
//                     [FORMAT_KIND_DECIMAL] = S8("d"),
//                     [FORMAT_KIND_BINARY] = S8("b"),
//                     [FORMAT_KIND_OCTAL] = S8("o"),
//                     [FORMAT_KIND_HEXADECIMAL] = S8("x"),
//                 };
//
//                 let format_kind_string = string8_slice_start(whole_format_string, first_format + 1);
//
//                 for (i = 0; i < BUSTER_ARRAY_LENGTH(possible_format_kind_strings); i += 1)
//                 {
//                     if (string8_equal(format_kind_string, possible_format_kind_strings[i]))
//                     {
//                         break;
//                     }
//                 }
//
//                 format_kind = (FormatKind)i;
//             }
//
//             switch (format)
//             {
//                 break; case FORMAT_OS_STRING:
//                 {
//                     // TODO:
//                     let string = va_arg(va, OsString);
//                     for (u64 i = 0; i < string.length; i += 1)
//                     {
//                         buffer[buffer_i++] = (char8)string.pointer[i];
//                     }
//                 }
//                 break; case FORMAT_OS_CHAR:
//                 {
//                     // TODO:
//                     let os_char = (OsChar)va_arg(va, u32);
//                     buffer[buffer_i++] = (char8)os_char;
//                 }
//                 break; case FORMAT_STRING8:
//                 {
//                     let string = va_arg(va, String8);
//                     for (u64 i = 0; i < string.length; i += 1)
//                     {
//                         buffer[buffer_i++] = string.pointer[i];
//                     }
//                 }
//                 break; case FORMAT_STRING16:
//                 {
//                     // TODO:
//                     let string = va_arg(va, String16);
//                     for (u64 i = 0; i < string.length; i += 1)
//                     {
//                         buffer[buffer_i++] = (char8)string.pointer[i];
//                     }
//                 }
//                 break; case FORMAT_STRING32:
//                 {
//                     // TODO:
//                     let string = va_arg(va, String32);
//                     for (u64 i = 0; i < string.length; i += 1)
//                     {
//                         buffer[buffer_i++] = (char8)string.pointer[i];
//                     }
//                 }
//                 break; case FORMAT_UNSIGNED_INTEGER_8: case FORMAT_UNSIGNED_INTEGER_16: case FORMAT_UNSIGNED_INTEGER_32: case FORMAT_UNSIGNED_INTEGER_64:
//                 {
//                     u64 value;
//                     switch (format)
//                     {
//                         break; case FORMAT_UNSIGNED_INTEGER_8: value = (u8)(va_arg(va, u32) & UINT8_MAX);
//                         break; case FORMAT_UNSIGNED_INTEGER_16: value = (u16)(va_arg(va, u32) & UINT16_MAX);
//                         break; case FORMAT_UNSIGNED_INTEGER_32: value = va_arg(va, u32);
//                         break; case FORMAT_UNSIGNED_INTEGER_64: value = va_arg(va, u64);
//                         break; default: BUSTER_UNREACHABLE();
//                     }
//
//                     let string_buffer = string8_slice(buffer_slice, buffer_i, buffer_slice.length);
//                     String8 format_result;
//
//                     switch (format_kind)
//                     {
//                         break; case FORMAT_KIND_DECIMAL: format_result = format_integer_decimal(string_buffer, value, false);
//                         break; case FORMAT_KIND_BINARY: format_result = format_integer_binary(string_buffer, value);
//                         break; case FORMAT_KIND_OCTAL: format_result = format_integer_octal(string_buffer, value);
//                         break; case FORMAT_KIND_HEXADECIMAL: format_result = format_integer_hexadecimal(string_buffer, value);
//                         break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
//                     }
//
//                     buffer_i += format_result.length;
//                 }
//                 break; case FORMAT_SIGNED_INTEGER_8: case FORMAT_SIGNED_INTEGER_16: case FORMAT_SIGNED_INTEGER_32: case FORMAT_SIGNED_INTEGER_64:
//                 {
//                     s64 value;
//                     switch (format)
//                     {
//                         break; case FORMAT_SIGNED_INTEGER_8: value = (s8)(va_arg(va, s32) & INT8_MAX);
//                         break; case FORMAT_SIGNED_INTEGER_16: value = (s16)(va_arg(va, s32) & INT16_MAX);
//                         break; case FORMAT_SIGNED_INTEGER_32: value = va_arg(va, s32);
//                         break; case FORMAT_SIGNED_INTEGER_64: value = va_arg(va, s64);
//                         break; default: BUSTER_UNREACHABLE();
//                     }
//
//                     let string_buffer = string8_slice(buffer_slice, buffer_i, buffer_slice.length);
//                     String8 format_result;
//
//                     switch (format_kind)
//                     {
//                         break; case FORMAT_KIND_DECIMAL: format_result = format_integer_decimal(string_buffer, value, true);
//                         break; case FORMAT_KIND_BINARY: format_result = format_integer_binary(string_buffer, value);
//                         break; case FORMAT_KIND_OCTAL: format_result = format_integer_octal(string_buffer, value);
//                         break; case FORMAT_KIND_HEXADECIMAL: format_result = format_integer_hexadecimal(string_buffer, value);
//                         break; case FORMAT_KIND_COUNT: BUSTER_UNREACHABLE();
//                     }
//
//                     buffer_i += format_result.length;
//                 }
//                 break; case FORMAT_UNSIGNED_INTEGER_128:
//                 {
//                 }
//                 break; case FORMAT_SIGNED_INTEGER_128:
//                 {
//                 }
//                 break; case FORMAT_INTEGER_COUNT:
//                 {
//                     buffer[buffer_i++] = '{';
//                     for (u64 i = 0; i < whole_format_string.length; i += 1)
//                     {
//                         buffer[buffer_i++] = whole_format_string.pointer[i];
//                     }
//                     buffer[buffer_i++] = '}';
//                 }
//             }
//         }
//     }
//
//     let string = string8_from_pointer_length(buffer, buffer_i);
//     os_file_write(os_get_stdout(), string_to_byte_slice(string));
}

BUSTER_IMPL OsStringList argument_add(ArgumentBuilder* builder, OsString arg)
{
#if defined(_WIN32)
    let result = arena_duplicate_os_string(builder->arena, arg, true);
    if (result.pointer)
    {
        result.pointer[arg.length] = ' ';
    }
    return result.pointer;
#else
    let result = arena_allocate(builder->arena, OsChar*, 1);
    if (result)
    {
        *result = (OsChar*)arg.pointer;
    }
    return result;
#endif
}

BUSTER_IMPL ArgumentBuilder* argument_builder_start(Arena* arena, OsString s)
{
    let position = arena->position;
    let argument_builder = arena_allocate(arena, ArgumentBuilder, 1);
    if (argument_builder)
    {
        *argument_builder = (ArgumentBuilder) {
            .argv = 0,
                .arena = arena,
                .arena_offset = position,
        };
        argument_builder->argv = argument_add(argument_builder, s);
    }
    return argument_builder;
}

BUSTER_IMPL OsStringList argument_builder_end(ArgumentBuilder* restrict builder)
{
#if defined(_WIN32)
    *(OsChar*)((u8*)builder->arena + builder->arena->position - sizeof(OsChar)) = 0;
#else
    argument_add(builder, (OsString){});
#endif
    return builder->argv;
}

BUSTER_IMPL ProcessResult buster_argument_process(OsStringList argument_pointer, OsStringList environment_pointer, u64 argument_index, OsString argument)
{
    BUSTER_UNUSED(argument_pointer);
    BUSTER_UNUSED(environment_pointer);
    BUSTER_UNUSED(argument_index);
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    if (os_string_equal(argument, OsS("--verbose")))
    {
        program_state->input.verbose = true;
    }
    else
    {
        result = PROCESS_RESULT_FAILED;
    }

    return result;
}

BUSTER_IMPL void argument_builder_destroy(ArgumentBuilder* restrict builder)
{
    let arena = builder->arena;
    let position = builder->arena_offset;
    arena->position = position;
}

BUSTER_IMPL OsString get_last_error_message(Arena* arena)
{
#if defined(_WIN32)
    OsChar buffer[4096];
    let error = GetLastError();

    DWORD length = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer,
            BUSTER_ARRAY_LENGTH(buffer),
            NULL
            );
    let error_string_stack = os_string_from_pointer_length(buffer, length);
    let error_string = arena_duplicate_os_string(arena, error_string_stack, true);
#else
    let error_string_pointer = strerror(errno);
    let error_string = string8_from_pointer(error_string_pointer);
    BUSTER_UNUSED(arena);
#endif
    return error_string;
}

BUSTER_IMPL u64 string16_length(const char16* s)
{
    u64 result = 0;

    if (s)
    {
        let it = s;

        while (*it)
        {
            it += 1;
        }

        result = (u64)(it - s);
    }

    return result;
}

BUSTER_IMPL u64 string32_length(const char32* s)
{
    u64 result = 0;

    if (s)
    {
        let it = s;

        while (*it)
        {
            it += 1;
        }

        result = (u64)(it - s);
    }

    return result;
}

BUSTER_IMPL ProcessHandle* os_process_spawn(OsChar* name, OsStringList argv, OsStringList envp)
{
    ProcessHandle* result = {};
#if defined(_WIN32)
    BUSTER_UNUSED(envp);
    PROCESS_INFORMATION process_information = {};
    STARTUPINFOW startup_info = {sizeof(startup_info)};
    if (CreateProcessW(name, argv, 0, 0, 1, 0, 0, 0, &startup_info, &process_information))
    {
        result = process_information.hProcess;
    }
    else
    {
        let error = GetLastError();

        OsChar buffer[4096];
        DWORD length = FormatMessageW(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                buffer,
                BUSTER_ARRAY_LENGTH(buffer),
                NULL
                );
        // TODO: delete?
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, 0, 0);
    }
#else
    pid_t pid = -1;
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attributes;
    let file_actions_init = posix_spawn_file_actions_init(&file_actions);
    let attribute_init = posix_spawnattr_init(&attributes);

    if (file_actions_init == 0 && attribute_init == 0)
    {
        let spawn_result = posix_spawn(&pid, name, &file_actions, &attributes, (char**)argv, (char**)envp);

        if (spawn_result != 0)
        {
            pid = -1;
        }
    }

    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attributes);

    result = pid == -1 ? (ProcessHandle*)0 : (ProcessHandle*)(u64)pid;
#endif

    if (program_state->input.verbose)
    {
        print(S8("Launched: "));

        let list = os_string_list_initialize(argv);
        for (let a = os_string_list_next(&list); a.pointer; a = os_string_list_next(&list))
        {
            print(S8("{OsS} "), a);
        }

        print(S8("\n"));
    }

    return result;
}

BUSTER_IMPL ProcessResult os_process_wait_sync(ProcessHandle* handle, ProcessResources resources)
{
    ProcessResult result = PROCESS_RESULT_UNKNOWN;

    if (handle)
    {
#if defined(_WIN32)
        BUSTER_UNUSED(resources);
        let wait_result = WaitForSingleObject(handle, INFINITE);
        if (wait_result == WAIT_OBJECT_0)
        {
            DWORD exit_code;
            if (GetExitCodeProcess(handle, &exit_code))
            {
                result = (ProcessResult)exit_code;
            }
        }
#else
        let pid = (pid_t)(u64)handle;
        int status;
        int options = 0;
        let wait_result = wait4(pid, &status, options, resources.linux_);
        // Normal exit
        if ((wait_result == pid) & WIFEXITED(status))
        {
            let exit_code = WEXITSTATUS(status);
            result = (ProcessResult)exit_code;
        }
        else
        {
            // TODO
            wait_result = PROCESS_RESULT_FAILED;
        }
#endif
    }

    return result;
}

BUSTER_IMPL String8 string16_to_string8(Arena* arena, String16 s)
{
    String8 error_result = {};
    let original_position = arena->position;
    let pointer = (char8*)arena + original_position;

    for (u64 i = 0; i < s.length; i += 1)
    {
        let ch8_pointer = arena_allocate(arena, char8, 1);
        let ch16 = s.pointer[i];
        if (ch16 <= 0x7f)
        {
            *ch8_pointer = (char8)ch16;
        }
        else
        {
            // TODO
            return error_result;
        }
    }

    String8 result = {pointer, arena->position - original_position};
    return result;
}

BUSTER_IMPL OsStringListIterator os_string_list_initialize(OsStringList list)
{
    return (OsStringListIterator) {
        .list = list,
    };
}

BUSTER_IMPL OsString os_string_list_next(OsStringListIterator* iterator)
{
    OsString result = {};
    let list = iterator->list;
    let original_position = iterator->position;
    let position = original_position;

    let current = list[position];
    if (current)
    {
#if defined(_WIN32)
        let original_pointer = &list[position];
        let pointer = original_pointer;
        if (*pointer == '"')
        {
            // TODO: handle escape
            let double_quote = raw_string16_first_character(pointer + 1, '"');
            if (double_quote == string_no_match)
            {
                return result;
            }

            position += double_quote + 1 + 1;
            pointer = &list[position];
        }

        let space = raw_string16_first_character(pointer, ' ');
        let is_space = space != string_no_match;
        space = is_space ? space : 0;
        position += space;
        position += is_space ? 0 : string16_length(pointer);
        let length = position - original_position;

        if (is_space)
        {
            while (list[position] == ' ')
            {
                position += 1;
            }
        }

        result = (OsString){ original_pointer, length };
#else
        position += 1;
        result = os_string_from_pointer(current);
#endif
        iterator->position = position;
    }

    return result;
}

BUSTER_IMPL OsString os_get_environment_variable(OsString variable)
{
    OsString result = {};
#if defined(_WIN32)
    let envp = GetEnvironmentStringsW();
    let it = envp;
    while (*it)
    {
        let length = string16_length(it);
        OsString full_env = {it, length};
        it += length + 1;
        let key_index = string16_first_character(full_env, '=');
        OsString key = {full_env.pointer, key_index};
        if (string16_equal(key, variable))
        {
            result = (OsString){full_env.pointer + (key_index + 1), full_env.length - (key_index + 1)};
            break;
        }
    }
#else
    let pointer = getenv(variable.pointer);
    result = (OsString){pointer, string8_length(pointer)};
#endif
    return result;
}

[[gnu::noreturn]] BUSTER_IMPL void os_exit(u32 code)
{
#if BUSTER_LINK_LIBC
    exit((int)code);
#else
#if defined(_WIN32)
    ExitProcess(code);
#else
#pragma error
#endif
#endif
}

BUSTER_IMPL bool copy_file(CopyFileArguments arguments)
{
    bool result = true;
#if defined(_WIN32)
    result = CopyFileW(arguments.original_path.pointer, arguments.new_path.pointer, false) != 0;
    if (!result)
    {
        print(S8("Error message: {OsS}\n"), get_last_error_message(arena_create((ArenaInitialization){})));
        print(S8("Original: {OsS}\n"), arguments.original_path);
        print(S8("New: {OsS}\n"), arguments.new_path);
        fail();
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_IMPL void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...)
{
    print(S8("{S8} failed at {S8}:{S8}:{u32}\n"), format, file_path, function, line);

    if (is_debugger_present())
    {
        fail();
    }
}

BUSTER_IMPL bool lib_tests(TestArguments* restrict arguments)
{
    print(S8("Running lib tests...\n"));
    bool result = 1;
    let arena = arguments->arena;
    let position = arena->position;

    // string8_format
    {
        enum UnsignedFormatTestCase
        {
            UNSIGNED_FORMAT_TEST_CASE_DEFAULT,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL,
            UNSIGNED_FORMAT_TEST_CASE_BINARY,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE,

            UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE,
            UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE,

            UNSIGNED_FORMAT_TEST_CASE_COUNT,
        };

        // u8
        {
            String8 u8_format_strings[] = {
                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("{u8}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("{u8:d}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("{u8:x}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("{u8:X}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("{u8:o}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("{u8:b}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("{u8:no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("{u8:d,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("{u8:x,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("{u8:X,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("{u8:o,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("{u8:b,no_prefix}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("{u8:width=[ ,2]}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("{u8:d,width=[ ,4]}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("{u8:x,width=[ ,8]}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("{u8:X,width=[ ,16]}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("{u8:o,width=[ ,32]}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("{u8:b,width=[ ,64]}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("{u8:width=[0,2]}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("{u8:d,width=[0,4]}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("{u8:x,width=[0,8]}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("{u8:X,width=[0,16]}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("{u8:o,width=[0,32]}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("{u8:b,width=[0,64]}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("{u8:width=[0,x]}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("{u8:d,width=[0,x]}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("{u8:x,width=[0,x]}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("{u8:X,width=[0,x]}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("{u8:o,width=[0,x]}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("{u8:b,width=[0,x]}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:width=[ ,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("{u8:d,width=[ ,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:x,width=[ ,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("{u8:X,width=[ ,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("{u8:o,width=[ ,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("{u8:b,width=[ ,x],no_prefix}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,2],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,4],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,8],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("{u8:X,width=[0,16],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("{u8:o,width=[0,32],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("{u8:b,width=[0,64],no_prefix}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:width=[0,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("{u8:d,width=[0,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:x,width=[0,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("{u8:X,width=[0,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("{u8:o,width=[0,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("{u8:b,width=[0,x],no_prefix}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("{u8:digit_group}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("{u8:digit_group,d}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("{u8:digit_group,x}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("{u8:digit_group,X}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("{u8:digit_group,o}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("{u8:digit_group,b}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("{u8:digit_group,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("{u8:digit_group,no_prefix,d}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("{u8:digit_group,no_prefix,x}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("{u8:digit_group,no_prefix,X}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("{u8:digit_group,no_prefix,o}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("{u8:digit_group,no_prefix,b}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("{u8:digit_group,width=[0,x]}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("{u8:digit_group,width=[0,x],d}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("{u8:digit_group,width=[0,x],x}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("{u8:digit_group,width=[0,x],X}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("{u8:digit_group,width=[0,x],o}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("{u8:digit_group,width=[0,x],b}"),

                [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("{u8:digit_group,width=[0,x],no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("{u8:digit_group,width=[0,x],d,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("{u8:digit_group,width=[0,x],x,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("{u8:digit_group,width=[0,x],X,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("{u8:digit_group,width=[0,x],o,no_prefix}"),
                [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("{u8:digit_group,width=[0,x],b,no_prefix}"),
            };

            static_assert(BUSTER_ARRAY_LENGTH(u8_format_strings) == UNSIGNED_FORMAT_TEST_CASE_COUNT);

            // 0, 1, 2, 4, 8, 16, UINT_MAX / 2, UINT_MAX

            STRUCT(TestCaseU8)
            {
                String8 expected_results[UNSIGNED_FORMAT_TEST_CASE_COUNT];
                u8 value;
                u8 reserved[7];
            };

            TestCaseU8 u8_cases[] =
            {
                {
                    .value = 0,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x0"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o0"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b0"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("0"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 0"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               0"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               0"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               0"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("00"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000000"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000000"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("000"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x00"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x00"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0000"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 0"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   0"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       0"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("00"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000000"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000000"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("00"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0000"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x0"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o0"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b0"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("0"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("0"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("000"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x00"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x00"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0000"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("000"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("000"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("00"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("00"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0000"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000000"),
                    },
                },
                {
                    .value = 1,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x1"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o1"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 1"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               1"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               1"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                               1"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("01"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000001"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000001"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000001"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("001"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x01"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x01"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0001"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000001"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 1"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   1"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("       1"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("01"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000001"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000001"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000001"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("01"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0001"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000001"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x1"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o1"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b1"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("1"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("1"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("001"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x01"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x01"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0001"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000001"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("001"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("001"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("01"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("01"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0001"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000001"),
                    },
                },
                {
                    .value = 2,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x2"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o2"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 2"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               2"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               2"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                              10"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("02"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000002"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000002"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000010"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("002"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x02"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x02"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0002"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000010"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 2"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   2"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("      10"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("02"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000002"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000002"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000010"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("02"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0002"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000010"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x2"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o2"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b10"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("2"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("10"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("002"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x02"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x02"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0002"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000010"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("002"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("002"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("02"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("02"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0002"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000010"),
                    },
                },
                {
                    .value = 4,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000100"),
                    },
                },
                {
                    .value = 8,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x8"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o10"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 8"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               8"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              10"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                            1000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("08"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000008"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000010"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000001000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("008"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x08"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x08"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0010"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00001000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 8"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  10"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("    1000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("08"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000008"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000010"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000001000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("08"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0010"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00001000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x8"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o10"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b1000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("8"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("1000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("008"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x08"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x08"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0010"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00001000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("008"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("008"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("08"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("08"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0010"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00001000"),
                    },
                },
                {
                    .value = 16,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d16"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o20"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b10000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("20"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("10000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("  16"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                              20"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                           10000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0016"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000010"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000010"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000020"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000010000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("016"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d016"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0020"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00010000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8(" 16"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("  20"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("   10000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0016"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000010"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000010"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000020"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000010000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("016"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0020"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00010000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d16"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o20"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b10000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("16"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("20"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("10000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("016"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d016"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0020"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00010000"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("016"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("016"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("10"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0020"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00010000"),
                    },
                },
                {
                    .value = 127,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b1111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("1111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8(" 127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("      7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("              7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                             177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                         1111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x0000007f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x000000000000007F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000001111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b01111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8("7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8(" 177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8(" 1111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("0000007f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("000000000000007F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000001111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("01111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b1111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("1111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b01111111"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("127"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("7f"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("7F"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0177"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("01111111"),
                    },
                },
                {
                    .value = 0xfa,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("250"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d250"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0xfa"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0xFA"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000100"),
                    },
                },
                {
                    .value = 0xfa,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000100"),
                    },
                },
                {
                    .value = 0xfa,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000100"),
                    },
                },
                {
                    .value = 0xfa,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000100"),
                    },
                },
                {
                    .value = 0xfa,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000100"),
                    },
                },
                {
                    .value = 0xfa,
                    .expected_results =
                    {
                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT] =                                           S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL] =                                           S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER] =                                 S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL] =                                             S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY] =                                            S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX] =                                 S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX] =                       S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX] =                                   S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX] =                                  S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_SPACE] =                             S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_SPACE] =                             S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_SPACE] =                   S8("       4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_SPACE] =                  S8("               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_SPACE] =                              S8("                               4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_SPACE] =                             S8("                                                             100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO] =                              S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO] =                              S8("0d0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO] =                    S8("0x00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO] =                   S8("0x0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO] =                               S8("0o00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO] =                              S8("0b0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO] =                              S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO] =                              S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO] =                    S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO] =                                S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO] =                               S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_SPACE_NO_PREFIX] =                   S8("  4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_SPACE_NO_PREFIX] =         S8(" 4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_SPACE_NO_PREFIX] =                     S8("   4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_SPACE_NO_PREFIX] =                    S8("     100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_2_ZERO_NO_PREFIX] =                    S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_4_ZERO_NO_PREFIX] =                    S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_8_ZERO_NO_PREFIX] =          S8("00000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_16_ZERO_NO_PREFIX] =         S8("0000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_32_ZERO_NO_PREFIX] =                     S8("00000000000000000000000000000004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_64_ZERO_NO_PREFIX] =                    S8("0000000000000000000000000000000000000000000000000000000000000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX] =                    S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX] =          S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX] =                      S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX] =                     S8("00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_SEPARATE] =                                  S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_SEPARATE] =                                  S8("0d4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_SEPARATE] =                        S8("0x4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_SEPARATE] =                                    S8("0o4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_SEPARATE] =                                   S8("0b100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_NO_PREFIX_SEPARATE] =                        S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_NO_PREFIX_SEPARATE] =              S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_NO_PREFIX_SEPARATE] =                          S8("4"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_NO_PREFIX_SEPARATE] =                         S8("100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_SEPARATE] =                     S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_SEPARATE] =                     S8("0d004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_SEPARATE] =           S8("0x04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_SEPARATE] =                       S8("0o0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_SEPARATE] =                      S8("0b00000100"),

                        [UNSIGNED_FORMAT_TEST_CASE_DEFAULT_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_DECIMAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =           S8("004"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_LOWER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_HEXADECIMAL_UPPER_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] = S8("04"),
                        [UNSIGNED_FORMAT_TEST_CASE_OCTAL_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =             S8("0004"),
                        [UNSIGNED_FORMAT_TEST_CASE_BINARY_WIDTH_X_ZERO_NO_PREFIX_SEPARATE] =            S8("00000100"),
                    },
                },
            };

            for (u64 case_value_i = 0; case_value_i < BUSTER_ARRAY_LENGTH(u8_cases); case_value_i += 1)
            {
                let u8_case = &u8_cases[case_value_i];
                let value = u8_case->value;

                for (u64 case_i = 0; case_i < UNSIGNED_FORMAT_TEST_CASE_COUNT; case_i += 1)
                {
                    let format_string = u8_format_strings[case_i];
                    let expected_string = u8_case->expected_results[case_i];
                    if (case_i == 0xfff)
                    {
                        BUSTER_BREAKPOINT();
                    }
                    let result_string = arena_string8_format(arena, 0, format_string, value);

                    if (!string8_equal(result_string, expected_string))
                    {
                        BUSTER_TEST_ERROR(S8(""), (u32)0);
                    }
                }
            }
        }
    }

    // {
    //     OsString strings[] = {
    //         OsS("clang"),
    //         OsS("-c"),
    //         OsS("-o"),
    //         OsS("--help"),
    //     };
    //
    //     let builder = argument_builder_start(arena, strings[0]);
    //     for (u64 i = 1; i < BUSTER_ARRAY_LENGTH(strings); i += 1)
    //     {
    //         argument_add(builder, strings[i]);
    //     }
    //
    //     let argv = argument_builder_end(builder);
    //     let it = os_string_list_initialize(argv);
    //
    //     for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(strings); i += 1)
    //     {
    //         BUSTER_OS_STRING_TEST(arguments, os_string_list_next(&it), strings[i]);
    //     }
    // }

    // string8_format_integer
    // {
    //     BUSTER_STRING8_TEST(arguments, S8("123"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 123, .format = INTEGER_FORMAT_DECIMAL, }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("1000"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 1000, .format = INTEGER_FORMAT_DECIMAL }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("12839128391258192419"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 12839128391258192419ULL, .format = INTEGER_FORMAT_DECIMAL}, true));
    //     BUSTER_STRING8_TEST(arguments, S8("-1"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 1, .format = INTEGER_FORMAT_DECIMAL, .treat_as_signed = true}, true));
    //     BUSTER_STRING8_TEST(arguments, S8("-1123123123"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 1123123123, .format = INTEGER_FORMAT_DECIMAL, .treat_as_signed = true}, true));
    //     BUSTER_STRING8_TEST(arguments, S8("0d0"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_DECIMAL, .prefix = true }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("0d123"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 123, .format = INTEGER_FORMAT_DECIMAL, .prefix = true, }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("0"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_HEXADECIMAL, }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("af"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0xaf, .format = INTEGER_FORMAT_HEXADECIMAL, }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("0x0"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_HEXADECIMAL, .prefix = true }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("0x8591baefcb"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0x8591baefcb, .format = INTEGER_FORMAT_HEXADECIMAL, .prefix = true }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("0o12557"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 012557, .format = INTEGER_FORMAT_OCTAL, .prefix = true }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("12557"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 012557, .format = INTEGER_FORMAT_OCTAL, }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("0b101101"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0b101101, .format = INTEGER_FORMAT_BINARY, .prefix = true }, true));
    //     BUSTER_STRING8_TEST(arguments, S8("101101"), string8_format_integer(arena, (FormatIntegerOptions) { .value = 0b101101, .format = INTEGER_FORMAT_BINARY, }, true));
    // }

    arena->position = position;
    print(S8("Lib tests {S8}!\n"), result ? S8("passed") : S8("failed"));
    return result;
}
#endif
