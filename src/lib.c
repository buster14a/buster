#pragma once

#include <lib.h>

#if BUSTER_KERNEL == 0
#include <stdio.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <linux/limits.h>
#include <linux/fs.h>
#if BUSTER_USE_PTHREAD
#include <pthread.h>
#endif
#if BUSTER_USE_IO_RING
#include <liburing.h>
#endif

#elif defined(__APPLE__)
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#elif _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
LOCAL RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions = {};
#endif

STRUCT(IoRing)
{
#ifdef __linux__
#if BUSTER_USE_IO_RING
    struct io_uring linux_impl;
#endif
#else
#pragma error
#endif
    u32 submitted_entry_count;
    u32 completed_entry_count;
};

STRUCT(Thread)
{
    Arena* arena;
    ThreadEntryPoint* entry_point;
    IoRing ring;
    pthread_t handle;
    void* context;
};

BUSTER_IMPL __thread Thread* thread;

STRUCT(ProtectionFlags)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
};

STRUCT(MapFlags)
{
    u64 private:1;
    u64 anonymous:1;
    u64 no_reserve:1;
    u64 populate:1;
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
#elif _WIN32
LOCAL DWORD os_windows_protection_flags(ProtectionFlags flags)
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
        UNREACHABLE();
    }

    return result;
}

LOCAL DWORD os_windows_allocation_flags(MapFlags flags)
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
    RIO_BUFFERID buffer_id = w32_rio_functions.RIORegisterBuffer(address, size);
    result = buffer_id != RIO_INVALID_BUFFERID;
    if (result)
    {
        w32_rio_functions.RIODeregisterBuffer(buffer_id);
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
#elif _WIN32
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
#elif _WIN32
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
#elif _WIN32
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

BUSTER_LOCAL void str_reverse(str s)
{
    char* restrict pointer = s.pointer;
    for (u64 i = 0, reverse_i = s.length - 1; i < reverse_i; i += 1, reverse_i -= 1)
    {
        let ch = pointer[i];
        pointer[i] = pointer[reverse_i];
        pointer[reverse_i] = ch;
    }
}

BUSTER_LOCAL str format_integer_hexadecimal(str buffer, u64 value)
{
    str result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (str) { buffer.pointer, 1};
    }
    else
    {
        let v = value;
        u64 i = 0;

        while (v != 0)
        {
            let digit = v % 16;
            let ch = (u8)(digit > 9 ? (digit - 10 + 'a') : (digit + '0'));
            BUSTER_CHECK(i < buffer.length);
            buffer.pointer[i] = ch;
            i += 1;
            v = v / 16;
        }

        let length = i;

        result = (str) { buffer.pointer , length };
        str_reverse(result);
    }

    return result;
}

BUSTER_LOCAL str format_integer_decimal(str buffer, u64 value, bool treat_as_signed)
{
    str result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (str) { buffer.pointer, 1};
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

        result = (str) { buffer.pointer + treat_as_signed, length - treat_as_signed };
        str_reverse(result);
        result.pointer -= treat_as_signed;
        result.length += treat_as_signed;
    }

    return result;
}

BUSTER_LOCAL str format_integer_octal(str buffer, u64 value)
{
    str result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (str) { buffer.pointer, 1};
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

        result = (str) { buffer.pointer, length };
        str_reverse(result);
    }

    return result;
}

BUSTER_LOCAL str format_integer_binary(str buffer, u64 value)
{
    str result = {};

    if (value == 0)
    {
        buffer.pointer[0] = '0';
        result = (str) { buffer.pointer, 1};
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

        result = (str) { buffer.pointer, length };
        str_reverse(result);
    }

    return result;
}

BUSTER_IMPL str format_integer_stack(str buffer, FormatIntegerOptions options)
{
    if (options.treat_as_signed)
    {
        BUSTER_CHECK(!options.prefix);
        BUSTER_CHECK(options.format == INTEGER_FORMAT_DECIMAL);
    }

    u64 prefix_digit_count = 2;

    str result = {};
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
            result = format_integer_hexadecimal(buffer, options.value);
        }
        break; case INTEGER_FORMAT_DECIMAL:
        {
            result = format_integer_decimal(buffer, options.value, options.treat_as_signed);
        }
        break; case INTEGER_FORMAT_OCTAL:
        {
            result = format_integer_octal(buffer, options.value);
        }
        break; case INTEGER_FORMAT_BINARY:
        {
            result = format_integer_binary(buffer, options.value);
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

[[noreturn]] [[gnu::cold]] BUSTER_IMPL void _assert_failed(u32 line, str function_name, str file_path)
{
    print(S("Assert failed at "));
    print(function_name);
    print(S(" in "));
    print(file_path);
    print(S(":"));
    char buffer[128];
    let stack_string = format_integer_stack((str){ buffer, BUSTER_ARRAY_LENGTH(buffer) }, (FormatIntegerOptions){ .value = line });
    print(stack_string);
    print(S("\n"));
        
    fail();
}

#if BUSTER_KERNEL == 0
BUSTER_IMPL FileDescriptor* os_file_open(str path, OpenFlags flags, OpenPermissions permissions)
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
    int fd = open(path.pointer, o, mode);

    if (fd >= 0)
    {
        result = (void*)(u64)fd;
    }

    return result;
#elif _WIN32
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

    let fd = CreateFileA(path.pointer, desired_access, shared_mode, &security_attributes, creation_disposition, flags_and_attributes, template_file);
    if (fd != INVALID_HANDLE_VALUE)
    {
        result = (FileDescriptor*)fd;
    }
    else
    {
        DWORD error = GetLastError();
        LPVOID msgBuffer;

        FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&msgBuffer,
                0,
                NULL
                );

        printf("Error %lu: %s\n", error, (char*)msgBuffer);
        LocalFree(msgBuffer);
    }
#endif
    return result;
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

BUSTER_IMPL u64 os_file_get_size(FileDescriptor* file_descriptor)
{
#if defined(__linux__) || defined(__APPLE__)
    int fd = generic_fd_to_posix(file_descriptor);
    struct stat sb;
    let fstat_result = fstat(fd, &sb);
    BUSTER_CHECK(fstat_result == 0);

    return (u64)sb.st_size;
#elif _WIN32
    HANDLE fd = generic_fd_to_windows(file_descriptor);
    LARGE_INTEGER file_size = {};
    BOOL result = GetFileSizeEx(fd, &file_size);
    CHECK(result);
    return file_size.QuadPart;
#endif
}

BUSTER_LOCAL u64 os_file_read_partially(FileDescriptor* file_descriptor, void* buffer, u64 byte_count)
{
#if defined(__linux__) || defined(__APPLE__)
    let fd = generic_fd_to_posix(file_descriptor);
    let read_byte_count = read(fd, buffer, byte_count);
    BUSTER_CHECK(read_byte_count > 0);

    return (u64)read_byte_count;
#elif _WIN32
    let fd = generic_fd_to_windows(file_descriptor);
    DWORD read_byte_count = 0;
    BOOL result = ReadFile(fd, buffer, (u32)byte_count, &read_byte_count, 0);
    CHECK(result);
    return read_byte_count;
#endif
}

BUSTER_LOCAL void os_file_read(FileDescriptor* file_descriptor, str buffer, u64 byte_count)
{
    u64 read_byte_count = 0;
    char* pointer = buffer.pointer;
    BUSTER_CHECK(buffer.length >= byte_count);
    while (byte_count - read_byte_count)
    {
        read_byte_count += os_file_read_partially(file_descriptor, pointer + read_byte_count, byte_count - read_byte_count);
    }
}

BUSTER_LOCAL u64 os_file_write_partially(FileDescriptor* file_descriptor, void* pointer, u64 length)
{
#if defined(__linux__) || defined(__APPLE__)
    let fd = generic_fd_to_posix(file_descriptor);
    let result = write(fd, pointer, length);
    BUSTER_CHECK(result > 0);
    return result;
#elif defined(_WIN32)
    let fd = generic_fd_to_windows(file_descriptor);
    DWORD written_byte_count = 0;
    BOOL result = WriteFile(fd, pointer, (u32)length, &written_byte_count, 0);
    CHECK(result);
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

BUSTER_IMPL void os_file_write(FileDescriptor* file_descriptor, str buffer)
{
    u64 total_written_byte_count = 0;

    while (total_written_byte_count < buffer.length)
    {
        let written_byte_count = os_file_write_partially(file_descriptor, buffer.pointer + total_written_byte_count, buffer.length - total_written_byte_count);
        total_written_byte_count += written_byte_count;
    }
}

BUSTER_IMPL void os_file_close(FileDescriptor* file_descriptor)
{
#if defined(__linux__) || defined(__APPLE__)
    let fd = generic_fd_to_posix(file_descriptor);
    let close_result = close(fd);
    BUSTER_CHECK(close_result == 0);
#elif _WIN32
    let fd = generic_fd_to_windows(file_descriptor);
    let result = CloseHandle(fd);
    CHECK(result);
#endif
}

BUSTER_LOCAL u64 page_size = BUSTER_KB(4);
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

BUSTER_IMPL str arena_join_string(Arena* arena, StringSlice strings, bool zero_terminate)
{
    u64 size = 0;

    for (u64 i = 0; i < strings.length; i += 1)
    {
        str string = strings.pointer[i];
        size += string.length;
    }

    char* pointer = arena_allocate_bytes(arena, size + zero_terminate, 1);

    u64 i = 0;

    for (u64 index = 0; index < strings.length; index += 1)
    {
        str string = strings.pointer[index];
        memcpy(pointer + i, string.pointer, string.length);
        i += string.length;
    }

    BUSTER_CHECK(i == size);
    if (zero_terminate)
    {
        pointer[i] = 0;
    }

    return str_from_ptr_len(pointer, size);
}

BUSTER_IMPL str arena_duplicate_string(Arena* arena, str str, bool zero_terminate)
{
    char* pointer = arena_allocate_bytes(arena, str.length + zero_terminate, 1);
    memcpy(pointer, str.pointer, str.length);
    if (zero_terminate)
    {
        pointer[str.length] = 0;
    }

    return str_from_ptr_len(pointer, str.length);
}

BUSTER_IMPL TimeDataType take_timestamp()
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec ts;
    let result = clock_gettime(CLOCK_MONOTONIC, &ts);
    BUSTER_CHECK(result == 0);
    return *(u128*)&ts;
#elif _WIN32
    LARGE_INTEGER c;
    BOOL result = QueryPerformanceCounter(&c);
    CHECK(result);
    return c.QuadPart;
#endif
}

BUSTER_LOCAL TimeDataType frequency;

BUSTER_IMPL u64 ns_between(TimeDataType start, TimeDataType end)
{
#if defined(__linux__) || defined(__APPLE__)
    let start_ts = *(struct timespec*)&start;
    let end_ts = *(struct timespec*)&end;
    let second_diff = end_ts.tv_sec - start_ts.tv_sec;
    let ns_diff = end_ts.tv_nsec - start_ts.tv_nsec;

    let result = second_diff * 1000000000LL + ns_diff;
    return result;
#elif _WIN32
    let ns = (f64)((end - start) * 1000 * 1000 * 1000) / frequency;
    return ns;
#endif
}

BUSTER_IMPL str file_read(Arena* arena, str path, FileReadOptions options)
{
    let fd = os_file_open(path, (OpenFlags) { .read = 1 }, (OpenPermissions){ .read = 1 });
    str result = {};

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
        let allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
        let allocation_bottom = allocation_size - (file_size + options.start_padding);
        let allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
        let file_buffer = arena_allocate_bytes(arena, allocation_size, allocation_alignment);
        os_file_read(fd, (str) { (char*)file_buffer + options.start_padding, file_size }, file_size);
        memset((u8*)file_buffer + options.start_padding + file_size, 0, allocation_bottom);
        os_file_close(fd);
        result = (str) { (char*)file_buffer + options.start_padding, file_size };
    }

    return result;
}

BUSTER_IMPL bool file_write(str path, str content)
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

BUSTER_LOCAL str os_path_absolute_stack(str buffer, const char* restrict relative_file_path)
{
    str result = {};
#if defined(__linux__) || defined(__APPLE__)
    let syscall_result = realpath(relative_file_path, buffer.pointer);

    if (syscall_result)
    {
        result = str_from_ptr_len(syscall_result, strlen(syscall_result));
        BUSTER_CHECK(result.length < buffer.length);
    }

#elif defined(_WIN32)
    DWORD length = GetFullPathNameA(relative_file_path, buffer.length, buffer.pointer, 0);
    if (length <= buffer.length)
    {
        result = (str){buffer.pointer, length};
    }
#endif
    return result;
}

BUSTER_IMPL str path_absolute(Arena* arena, const char* restrict relative_file_path)
{
    char buffer[PATH_MAX];
    let stack_slice = os_path_absolute_stack((str){buffer, BUSTER_ARRAY_LENGTH(buffer)}, relative_file_path);
    let result = arena_duplicate_string(arena, stack_slice, true);
    return result;
}

BUSTER_IMPL GlobalProgram global_program;

BUSTER_LOCAL void global_initialize(BusterInitialization initialization)
{
    BUSTER_UNUSED(initialization);
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
    else if (global_program.verbose)
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

BUSTER_LOCAL void* thread_os_entry_point(void* context)
{
    let thread = (Thread*)context;
    thread->arena = arena_create((ArenaInitialization){});
#if BUSTER_USE_IO_RING
    io_ring_init(&thread->ring, 4096);
#endif
    let os_result = thread->entry_point(thread);
    return (void*)(u64)os_result;
}

BUSTER_IMPL ProcessResult buster_run(BusterInitialization initialization)
{
    ProcessResult result = {};
#ifdef _WIN32
    BOOL result = QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
    CHECK(result);

    WSADATA WinSockData;
    WSAStartup(MAKEWORD(2, 2), &WinSockData);
    GUID guid = WSAID_MULTIPLE_RIO;
    DWORD rio_byte = 0;
    SOCKET Sock = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
    WSAIoctl(Sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), (void**)&w32_rio_functions, sizeof(w32_rio_functions), &rio_byte, 0, 0);
    closesocket(Sock);
#else
#endif

    global_program.arena = arena_create((ArenaInitialization){});
    global_program.argc = initialization.argc;
    global_program.argv = initialization.argv;
    global_program.envp = initialization.envp;

    result = initialization.process_arguments(global_program.arena, initialization.context, initialization.argc, initialization.argv, initialization.envp);

    if (result == PROCESS_RESULT_SUCCESS)
    {
        if (initialization.thread_entry_point)
        {
#if BUSTER_USE_PTHREAD
            u64 thread_count = 0;

            switch (initialization.thread_spawn_policy)
            {
                break; case THREAD_SPAWN_POLICY_SINGLE_THREADED:
                {
                    thread_count = 0;
                }
                break; case THREAD_SPAWN_POLICY_SPAWN_SINGLE_THREAD:
                {
                    thread_count = 1;
                }
                break; case THREAD_SPAWN_POLICY_SATURATE_LOGICAL_CORES:
                {
                }
                break; case THREAD_SPAWN_POLICY_SATURATE_PHYSICAL_CORES:
                {
                }
            }

            if (thread_count != 0)
            {
                Thread* threads = arena_allocate(global_program.arena, Thread, thread_count);

                u64 failure_count = 0;
                for (u64 i = 0; i < thread_count; i += 1)
                {
                    let thread = &threads[i];
                    thread->entry_point = initialization.thread_entry_point;
                    let create_result = pthread_create(&thread->handle, 0, &thread_os_entry_point, thread);
                    failure_count += create_result == 0;
                }

                if (failure_count == 0)
                {
                    for (u64 i = 0; i < thread_count; i += 1)
                    {
                        let thread = &threads[i];
                        void* return_value;
                        let join_result = pthread_join(thread->handle, &return_value);
                        failure_count += join_result == 0;
                        let thread_result = (ProcessResult)(u64)return_value;
                        if (thread_result != PROCESS_RESULT_SUCCESS)
                        {
                            result = thread_result;
                        }
                    }
                }

                if (failure_count == 0)
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            else
#endif
            {
                Thread thread_buffer = {};
                thread = &thread_buffer;
                thread->entry_point = initialization.thread_entry_point;
                thread->context = initialization.context;
                BUSTER_CHECK(initialization.thread_spawn_policy == THREAD_SPAWN_POLICY_SINGLE_THREADED);
                result = thread->entry_point(thread);
            }
        }
        else
        {
            if (global_program.verbose) printf("No thread entry point specified\n");
            result = PROCESS_RESULT_FAILED;
        }
    }

    return result;
}

[[gnu::cold]] BUSTER_LOCAL bool is_debugger_present()
{
    if (BUSTER_UNLIKELY(!global_program.is_debugger_present_called))
    {
        global_program.is_debugger_present_called = true;
#if defined(__linux__)
        let os_result = ptrace(PTRACE_TRACEME, 0, 0, 0) == -1;
        global_program._is_debugger_present = os_result != 0;
#elif defined(__APPLE__)
#elif defined(_WIN32)
        let os_result = IsDebuggerPresent();
        _is_debugger_present = os_result != 0;
#else
    trap();
#endif
    }

    return global_program._is_debugger_present;
}

[[noreturn]] [[gnu::cold]] BUSTER_IMPL void fail()
{
    if (is_debugger_present())
    {
        BUSTER_TRAP();
    }

    exit(1);
}

BUSTER_IMPL str format_integer(Arena* arena, FormatIntegerOptions options, bool zero_terminate)
{
    char buffer[128];
    let stack_string = format_integer_stack((str){ buffer, BUSTER_ARRAY_LENGTH(buffer) }, options);
    return arena_duplicate_string(arena, stack_string, zero_terminate);
}

BUSTER_IMPL ExecutionResult os_execute(Arena* arena, char** arguments, char** environment, ExecutionOptions options)
{
    ExecutionResult result = {};
    BUSTER_UNUSED(arena);

#if defined (__linux__) || defined(__APPLE__)
    FileDescriptor* null_file_descriptor = 0;

    if (options.null_file_descriptor)
    {
        null_file_descriptor = options.null_file_descriptor;
    }
    else if ((options.policies[0] == STREAM_POLICY_IGNORE) | (options.policies[1] == STREAM_POLICY_IGNORE))
    {
        null_file_descriptor = os_file_open(S("/dev/null"), (OpenFlags) { .write = 1 }, (OpenPermissions){});
    }

    int pipes[STREAM_COUNT][2];

    for (int i = 0; i < STREAM_COUNT; i += 1)
    {
        if (options.policies[i] == STREAM_POLICY_PIPE)
        {
            if (pipe(pipes[i]) == -1)
            {
                fail();
            }
        }
    }

    let pid = fork();

    switch (pid)
    {
        break; case -1:
        {
            fail();
        }
        break; case 0:
        {
            for (int i = 0; i < STREAM_COUNT; i += 1)
            {
                let fd = (i + 1);

                switch (options.policies[i])
                {
                    break; case STREAM_POLICY_INHERIT: {}
                    break; case STREAM_POLICY_PIPE:
                    {
                        close(pipes[i][0]);
                        dup2(pipes[i][1], fd);
                        close(pipes[i][1]);
                    }
                    break; case STREAM_POLICY_IGNORE:
                    {
                        dup2(generic_fd_to_posix(null_file_descriptor), fd);
                        close(generic_fd_to_posix(null_file_descriptor));
                    }
                }
            }

            let result = execve(arguments[0], arguments, environment);

            if (result != -1)
            {
                BUSTER_UNREACHABLE();
            }

            fail();
        }
        break; default:
        {
            for (int i = 0; i < STREAM_COUNT; i += 1)
            {
                if (options.policies[i] == STREAM_POLICY_PIPE)
                {
                    close(pipes[i][1]);
                }
            }

            if (options.policies[0] == STREAM_POLICY_PIPE | options.policies[1] == STREAM_POLICY_PIPE)
            {
                fail();
            }

            int status = 0;
            let waitpid_result = waitpid(pid, &status, 0);

            if (waitpid_result == pid)
            {
                if (WIFEXITED(status))
                {
                    result.termination_code = WEXITSTATUS(status);
                    result.termination_kind = TERMINATION_KIND_EXIT;
                }
                else if (WIFSIGNALED(status))
                {
                    result.termination_code = WTERMSIG(status);
                    result.termination_kind = TERMINATION_KIND_SIGNAL;
                }
                else if (WIFSTOPPED(status))
                {
                    result.termination_code = WSTOPSIG(status);
                    result.termination_kind = TERMINATION_KIND_STOP;
                }
                else
                {
                    result.termination_kind = TERMINATION_KIND_UNKNOWN;
                }

                if (!options.null_file_descriptor & !!null_file_descriptor)
                {
                    os_file_close(null_file_descriptor);
                }
            }
            else if (waitpid_result == -1)
            {
                fail();
            }
            else
            {
                BUSTER_UNREACHABLE();
            }
        }
    }
#elif _WIN32
    {
        HANDLE hStdInRead  = NULL, hStdInWrite  = NULL;
        HANDLE hStdOutRead = NULL, hStdOutWrite = NULL;
        HANDLE hStdErrRead = NULL, hStdErrWrite = NULL;

        SECURITY_ATTRIBUTES sa = { sizeof(sa) };
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        if (options.policies[0] == STREAM_POLICY_PIPE)
        {
            if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0))
            {
                fail();
            }
            if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0))
            {
                fail();
            }
        }

        if (options.policies[1] == STREAM_POLICY_PIPE)
        {
            if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0))
            {
                fail();
            }
            if (!SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0))
            {
                fail();
            }
        }

        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;

        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = (options.policies[0] == STREAM_POLICY_PIPE) ? hStdOutWrite : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError  = (options.policies[1] == STREAM_POLICY_PIPE) ? hStdErrWrite : GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        size_t cmd_len = 0;
        for (int i = 0; arguments[i]; i += 1)
        {
            cmd_len += strlen(arguments[i]) + 1;
        }
        char* cmdline = (char*)malloc(cmd_len + 1);
        if (!cmdline)
        {
            fail();
        }
        cmdline[0] = '\0';
        for (int i = 0; arguments[i]; i += 1)
        {
            strcat(cmdline, arguments[i]);
            if (arguments[i+1]) strcat(cmdline, " ");
        }

        LPVOID env_block = NULL;
        if (environment)
        {
            size_t total_len = 1;
            for (char** e = environment; *e; ++e)
            {
                total_len += strlen(*e) + 1;
            }

            env_block = malloc(total_len);

            if (!env_block)
            {
                fail();
            }

            char* dst = (char*)env_block;
            for (char** e = environment; *e; ++e)
            {
                size_t len = strlen(*e);
                memcpy(dst, *e, len);
                dst[len] = '\0';
                dst += len + 1;
            }
            *dst = '\0';
        }

        BOOL success = CreateProcessA(
            NULL,
            cmdline,
            NULL,
            NULL,
            TRUE,
            0,
            env_block,
            NULL,
            &si,
            &pi
        );

        free(cmdline);

        if (!success)
        {
            fail();
        }

        if (hStdInRead)
        {
            CloseHandle(hStdInRead);
        }
        if (hStdOutWrite)
        {
            CloseHandle(hStdOutWrite);
        }
        if (hStdErrWrite)
        {
            CloseHandle(hStdErrWrite);
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exit_code = 0;
        if (!GetExitCodeProcess(pi.hProcess, &exit_code))
        {
            fail();
        }

        result.termination_code = (int)exit_code;
        result.termination_kind = TERMINATION_KIND_EXIT;

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        if (env_block)
        {
            free(env_block);
        }
    }
#endif

    return result;
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

    if (is_decimal(ch))
    {
        value = ch - '0';
    }
    else if (is_hexadecimal_alpha_upper(ch))
    {
        value = ch - 'A' + 10;
    }
    else if (is_hexadecimal_alpha_lower(ch))
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
    BUSTER_CHECK(is_decimal(ch));
    return (accumulator * 10) + (ch - '0');
}

BUSTER_LOCAL u64 accumulate_octal(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(is_octal(ch));

    return (accumulator * 8) + (ch - '0');
}

BUSTER_LOCAL u64 accumulate_binary(u64 accumulator, u8 ch)
{
    BUSTER_CHECK(is_binary(ch));

    return (accumulator * 2) + (ch - '0');
}

BUSTER_IMPL u64 parse_integer_decimal_assume_valid(str string)
{
    u64 value = 0;

    for (u64 i = 0; i < string.length; i += 1)
    {
        let ch = string.pointer[i];
        value = accumulate_decimal(value, ch);
    }

    return value;
}

BUSTER_IMPL IntegerParsing parse_hexadecimal_scalar(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!is_hexadecimal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_hexadecimal(value, ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing parse_decimal_scalar(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!is_decimal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_decimal(value, ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing parse_octal_scalar(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!is_octal(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_octal(value, ch);
    }

    return (IntegerParsing) { .value = value, .i = i };
}

BUSTER_IMPL IntegerParsing parse_binary_scalar(const char* restrict p)
{
    u64 value = 0;
    u64 i = 0;

    while (1)
    {
        let ch = p[i];

        if (!is_binary(ch))
        {
            break;
        }

        i += 1;
        value = accumulate_binary(value, ch);
    }

    return (IntegerParsing){ .value = value, .i = i };
}

#if defined(_WIN32)
LOCAL ThreadHandle* os_windows_thread_to_generic(HANDLE handle)
{
    CHECK(handle != 0);
    return (ThreadHandle*)handle;
}

LOCAL HANDLE os_windows_thread_from_generic(ThreadHandle* handle)
{
    CHECK(handle != 0);
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
    let pthread = os_posix_thread_from_generic(handle);
    void* void_return_value = 0;
#if BUSTER_USE_PTHREAD
    let join_result = pthread_join(pthread, &void_return_value);
    if (join_result == 0)
    {
        return_code = (u32)(u64)void_return_value;
    }
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

BUSTER_IMPL void test_error(str CHECK, u32 line, str function, str file_path)
{
    if (is_debugger_present())
    {
        BUSTER_UNUSED(CHECK);
        BUSTER_UNUSED(line);
        BUSTER_UNUSED(function);
        BUSTER_UNUSED(file_path);
        BUSTER_TRAP();
    }
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

BUSTER_IMPL bool str_is_zero_terminated(str s)
{
    return s.pointer[s.length] == 0;
}

BUSTER_IMPL str str_from_pointers(char* start, char* end)
{
    BUSTER_CHECK(end >= start);
    u64 len = end - start;
    return (str) { start, len };
}

BUSTER_IMPL str str_from_ptr_len(const char* ptr, u64 len)
{
    return (str) { (char*)ptr, len };
}

BUSTER_IMPL str str_from_ptr_start_end(char* ptr, u64 start, u64 end)
{
    return (str) { ptr + start, end - start };
}

BUSTER_IMPL str str_slice_start(str s, u64 start)
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

BUSTER_IMPL str str_slice(str s, u64 start, u64 end)
{
    s.pointer += start;
    s.length = end - start;
    return s;
}

BUSTER_IMPL bool str_equal(str s1, str s2)
{
    bool is_equal = s1.length == s2.length;
    if (is_equal & (s1.length != 0) & (s1.pointer != s2.pointer))
    {
        is_equal = memory_compare(s1.pointer, s2.pointer, s1.length);
    }

    return is_equal;
}

BUSTER_IMPL u64 str_first_ch(str s, u8 ch)
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

BUSTER_IMPL u64 str_last_ch(str s, u8 ch)
{
    let result = string_no_match;

    let pointer = s.pointer + s.length;

    do
    {
        pointer -= 1;
        if (*pointer == ch)
        {
            result = pointer - s.pointer;
            break;
        }
    } while (pointer - s.pointer);

    return result;
}

BUSTER_IMPL u64 align_forward(u64 n, u64 a)
{
    let mask = a - 1;
    let result = (n + mask) & ~mask;
    return result;
}

BUSTER_IMPL bool is_space(char ch)
{
    return ((ch == ' ') | (ch == '\t')) | ((ch == '\r') | (ch == '\n'));
}

BUSTER_IMPL bool is_decimal(char ch)
{
    return (ch >= '0') & (ch <= '9');
}

BUSTER_IMPL bool is_octal(char ch)
{
    return (ch >= '0') & (ch <= '7');
}

BUSTER_IMPL bool is_binary(char ch)
{
    return (ch == '0') | (ch == '1');
}

BUSTER_IMPL bool is_hexadecimal_alpha_lower(char ch)
{
    return (ch >= 'a') & (ch <= 'f');
}

BUSTER_IMPL bool is_hexadecimal_alpha_upper(char ch)
{
    return (ch >= 'A') & (ch <= 'F');
}

BUSTER_IMPL bool is_hexadecimal_alpha(char ch)
{
    return is_hexadecimal_alpha_upper(ch) | is_hexadecimal_alpha_lower(ch);
}

BUSTER_IMPL bool is_hexadecimal(char ch)
{
    return is_decimal(ch) | is_hexadecimal_alpha(ch);
}

BUSTER_IMPL bool is_identifier_start(char ch)
{
    return (((ch >= 'a') & (ch <= 'z')) | ((ch >= 'A') & (ch <= 'Z'))) | (ch == '_');
}

BUSTER_IMPL bool is_identifier(char ch)
{
    return is_identifier_start(ch) | is_decimal(ch);
}

BUSTER_IMPL void print(str str)
{
    os_file_write(os_get_stdout(), str);
}

BUSTER_IMPL char** argument_add(ArgumentBuilder* builder, char* arg)
{
    let ptr = arena_allocate(builder->arena, char*, 1);
    *ptr = arg;
    return ptr;
}

BUSTER_IMPL ArgumentBuilder* argument_builder_start(Arena* arena, char* s)
{
    let position = arena->position;
    let argument_builder = arena_allocate(arena, ArgumentBuilder, 1);
    *argument_builder = (ArgumentBuilder) {
        .argv = 0,
        .arena = arena,
        .arena_offset = position,
    };
    argument_builder->argv = argument_add(argument_builder, s);
    return argument_builder;
}

BUSTER_IMPL char** argument_builder_end(ArgumentBuilder* restrict builder)
{
    argument_add(builder, 0);
    return builder->argv;
}

BUSTER_IMPL ProcessResult argument_process(u64 argument_count, char** argument_pointer, char** environment_pointer, u64 argument_index)
{
    BUSTER_UNUSED(environment_pointer);
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    BUSTER_CHECK(argument_index < argument_count);

    let argument = argument_pointer[argument_index];
    if (strcmp(argument, "-verbose") == 0)
    {
        global_program.verbose = true;
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

#if BUSTER_INCLUDE_TESTS
BUSTER_IMPL bool lib_tests(TestArguments* restrict arguments)
{
    bool result = 1;
    let arena = arguments->arena;
    let position = arena->position;
    BUSTER_TEST(arguments, str_equal(S("123"), format_integer(arena, (FormatIntegerOptions) { .value = 123, .format = INTEGER_FORMAT_DECIMAL, }, true)));
    BUSTER_TEST(arguments, str_equal(S("1000"), format_integer(arena, (FormatIntegerOptions) { .value = 1000, .format = INTEGER_FORMAT_DECIMAL }, true)));
    BUSTER_TEST(arguments, str_equal(S("12839128391258192419"), format_integer(arena, (FormatIntegerOptions) { .value = 12839128391258192419ULL, .format = INTEGER_FORMAT_DECIMAL}, true)));
    BUSTER_TEST(arguments, str_equal(S("-1"), format_integer(arena, (FormatIntegerOptions) { .value = 1, .format = INTEGER_FORMAT_DECIMAL, .treat_as_signed = true}, true)));
    BUSTER_TEST(arguments, str_equal(S("-1123123123"), format_integer(arena, (FormatIntegerOptions) { .value = 1123123123, .format = INTEGER_FORMAT_DECIMAL, .treat_as_signed = true}, true)));
    BUSTER_TEST(arguments, str_equal(S("0d0"), format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_DECIMAL, .prefix = true }, true)));
    BUSTER_TEST(arguments, str_equal(S("0d123"), format_integer(arena, (FormatIntegerOptions) { .value = 123, .format = INTEGER_FORMAT_DECIMAL, .prefix = true, }, true)));
    BUSTER_TEST(arguments, str_equal(S("0"), format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_HEXADECIMAL, }, true)));
    BUSTER_TEST(arguments, str_equal(S("af"), format_integer(arena, (FormatIntegerOptions) { .value = 0xaf, .format = INTEGER_FORMAT_HEXADECIMAL, }, true)));
    BUSTER_TEST(arguments, str_equal(S("0x0"), format_integer(arena, (FormatIntegerOptions) { .value = 0, .format = INTEGER_FORMAT_HEXADECIMAL, .prefix = true }, true)));
    BUSTER_TEST(arguments, str_equal(S("0x8591baefcb"), format_integer(arena, (FormatIntegerOptions) { .value = 0x8591baefcb, .format = INTEGER_FORMAT_HEXADECIMAL, .prefix = true }, true)));
    BUSTER_TEST(arguments, str_equal(S("0o12557"), format_integer(arena, (FormatIntegerOptions) { .value = 012557, .format = INTEGER_FORMAT_OCTAL, .prefix = true }, true)));
    BUSTER_TEST(arguments, str_equal(S("12557"), format_integer(arena, (FormatIntegerOptions) { .value = 012557, .format = INTEGER_FORMAT_OCTAL, }, true)));
    BUSTER_TEST(arguments, str_equal(S("0b101101"), format_integer(arena, (FormatIntegerOptions) { .value = 0b101101, .format = INTEGER_FORMAT_BINARY, .prefix = true }, true)));
    BUSTER_TEST(arguments, str_equal(S("101101"), format_integer(arena, (FormatIntegerOptions) { .value = 0b101101, .format = INTEGER_FORMAT_BINARY, }, true)));
    arena->position = position;
    return result;
}
#endif
#else
EXPORT void* memset(void* restrict address, int ch, u64 byte_count)
{
    let c = (char)ch;

    let ptr = (u8* restrict)address;

    for (u64 i = 0; i < byte_count; i += 1)
    {
        ptr[i] = c;
    }

    return address;
}
#endif

BUSTER_IMPL char* get_last_error_message()
{
    return 0;
}

BUSTER_IMPL ProcessResult process_wait_sync(pid_t pid, void* siginfo_buffer)
{
    ProcessResult result;
    let siginfo = (siginfo_t*)siginfo_buffer;
    if (pid != -1)
    {
        int wait_result = waitid(P_PID, pid, siginfo_buffer, WEXITED);

        if (wait_result == 0)
        {
            if (siginfo->si_code == CLD_EXITED)
            {
                result = (ProcessResult)siginfo->si_status;
            }
            else
            {
                result = PROCESS_RESULT_UNKNOWN;
            }
        }
        else
        {
            result = PROCESS_RESULT_UNKNOWN;
        }
    }
    else
    {
        result = PROCESS_RESULT_NOT_EXISTENT;
    }

    return result;
}
