#pragma once
#include <buster/os.h>
#include <buster/system_headers.h>
#include <buster/assertion.h>
#include <buster/arena.h>

[[gnu::cold]] BUSTER_IMPL bool is_debugger_present()
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

[[noreturn]] [[gnu::cold]] BUSTER_IMPL void os_fail()
{
    if (is_debugger_present())
    {
        BUSTER_TRAP();
    }

    os_exit(1);
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

#if defined (__linux__) || defined(__APPLE__)
BUSTER_GLOBAL_LOCAL int os_posix_protection_flags(ProtectionFlags flags)
{
    int result = 
        PROT_READ * flags.read |
        PROT_WRITE * flags.write |
        PROT_EXEC * flags.execute
    ;

    return result;
}

BUSTER_GLOBAL_LOCAL int os_posix_map_flags(MapFlags flags)
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

BUSTER_GLOBAL_LOCAL FileDescriptor* posix_fd_to_generic_fd(int fd)
{
    BUSTER_CHECK(fd >= 0);
    return (FileDescriptor*)(u64)(fd);
}

BUSTER_GLOBAL_LOCAL int generic_fd_to_posix(FileDescriptor* fd)
{
    BUSTER_CHECK(fd);
    return (int)(u64)fd;
}
#elif defined(_WIN32)
BUSTER_GLOBAL_LOCAL DWORD os_windows_protection_flags(ProtectionFlags flags)
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

BUSTER_GLOBAL_LOCAL DWORD os_windows_allocation_flags(MapFlags flags)
{
    DWORD result = 0;
    result |= MEM_RESERVE;

    if (!flags.no_reserve)
    {
        result |= MEM_COMMIT;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void* generic_fd_to_windows(FileDescriptor* fd)
{
    BUSTER_CHECK(fd);
    return (void*)fd;
}
#endif

#if defined(__linux__) || defined(__APPLE__)
BUSTER_GLOBAL_LOCAL ThreadHandle* os_posix_thread_to_generic(pthread_t handle)
{
    BUSTER_CHECK(handle != 0);
    return (ThreadHandle*)handle;
}

BUSTER_GLOBAL_LOCAL pthread_t os_posix_thread_from_generic(ThreadHandle* handle)
{
    BUSTER_CHECK(handle != 0);
    return (pthread_t)handle;
}
#endif


BUSTER_GLOBAL_LOCAL bool os_lock_and_unlock(void* address, u64 size)
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
        RIO_BUFFERID buffer_id = w32_rio_functions.RIORegisterBuffer((PCHAR)address, (DWORD)size);
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

BUSTER_IMPL bool os_commit(void* address, u64 size, ProtectionFlags protection, bool lock)
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
        os_lock_and_unlock(address, size);
    }

    return result;
}

BUSTER_IMPL void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map)
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

BUSTER_IMPL FileDescriptor* os_get_standard_stream(StandardStream stream)
{
    FileDescriptor* result = {};
#if defined(__linux__) || defined(__APPLE__)
    int fds[] = {
        [STANDARD_STREAM_INPUT] = STDIN_FILENO,
        [STANDARD_STREAM_OUTPUT] = STDOUT_FILENO,
        [STANDARD_STREAM_ERROR] = STDERR_FILENO,
    };
    static_assert(BUSTER_ARRAY_LENGTH(fds) == STANDARD_STREAM_COUNT);
    result = posix_fd_to_generic_fd(fds[stream]);
#elif defined(_WIN32)
    DWORD descriptors[] = {
        [STANDARD_STREAM_INPUT] = STD_INPUT_HANDLE,
        [STANDARD_STREAM_OUTPUT] = STD_OUTPUT_HANDLE,
        [STANDARD_STREAM_ERROR] = STD_ERROR_HANDLE,
    };
    result = (FileDescriptor*)GetStdHandle(descriptors[stream]);
#endif
    return result;
}

BUSTER_IMPL FileDescriptor* os_get_stdout()
{
    FileDescriptor* result = {};
#if defined(__linux__) || defined(__APPLE__)
    result = posix_fd_to_generic_fd(STDOUT_FILENO);
#elif defined(_WIN32)
    result = (FileDescriptor*)GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    return result;
}

__attribute__((used)) BUSTER_GLOBAL_LOCAL TimeDataType frequency;

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

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL ThreadHandle* os_windows_thread_to_generic(HANDLE handle)
{
    BUSTER_CHECK(handle != 0);
    return (ThreadHandle*)handle;
}

BUSTER_GLOBAL_LOCAL HANDLE os_windows_thread_from_generic(ThreadHandle* handle)
{
    BUSTER_CHECK(handle != 0);
    return (HANDLE)handle;
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

BUSTER_IMPL StringOs os_path_absolute(StringOs buffer, StringOs relative_file_path)
{
    StringOs result = {};
#if defined(__linux__) || defined(__APPLE__)
    let syscall_result = realpath((char*)relative_file_path.pointer, (char*)buffer.pointer);

    if (syscall_result)
    {
        result = string8_from_pointer_length(syscall_result, strlen(syscall_result));
        BUSTER_CHECK(result.length < buffer.length);
    }

#elif defined(_WIN32)
    DWORD length = GetFullPathNameW(relative_file_path.pointer, (DWORD)buffer.length, buffer.pointer, 0);
    if (length <= buffer.length)
    {
        result.pointer = buffer.pointer;
        result.length = length;
    }
#endif
    return result;
}

BUSTER_IMPL void os_make_directory(StringOs path)
{
#if defined(__linux__) || defined(__APPLE__)
    mkdir((const char*)path.pointer, 0755);
#elif defined(_WIN32)
    CreateDirectoryW(path.pointer, 0);
#endif
}

BUSTER_IMPL FileDescriptor* os_file_open(StringOs path, OpenFlags flags, OpenPermissions permissions)
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
        if (program_state->input.verbose)
        {
            string8_print(S8("Error: {EOs}\n"), os_get_last_error());
        }
    }
#endif
    return result;
}

BUSTER_IMPL Arena* thread_arena()
{
    return thread->arena;
}

BUSTER_GLOBAL_LOCAL u64 os_file_write_partially(FileDescriptor* file_descriptor, void* pointer, u64 length)
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

BUSTER_IMPL void os_file_write(FileDescriptor* file_descriptor, ByteSlice buffer)
{
    u64 total_written_byte_count = 0;

    while (total_written_byte_count < buffer.length)
    {
        let written_byte_count = os_file_write_partially(file_descriptor, buffer.pointer + total_written_byte_count, buffer.length - total_written_byte_count);
        total_written_byte_count += written_byte_count;
    }
}

BUSTER_GLOBAL_LOCAL u64 os_file_read_partially(FileDescriptor* file_descriptor, void* buffer, u64 byte_count)
{
    u64 result = 0;
    bool success = true;
#if defined(__linux__) || defined(__APPLE__)
    let fd = generic_fd_to_posix(file_descriptor);
    let read_byte_count = read(fd, buffer, byte_count);
    success = read_byte_count >= 0;
    if (success)
    {
        result = (u64)read_byte_count;
    }
#elif defined(_WIN32)
    let fd = generic_fd_to_windows(file_descriptor);
    DWORD read_byte_count = 0;
    success = ReadFile(fd, buffer, (u32)byte_count, &read_byte_count, 0) != 0;
    if (success)
    {
        result = read_byte_count;
    }
#endif
    if (!success)
    {
        string8_print(S8("Error reading file: {EOs}\n"), os_get_last_error());
    }

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

BUSTER_IMPL u64 string8_code_point_count(String8 s, u8 code_point)
{
    u64 count = 0;
    let restrict p = s.pointer;
    for (u64 i = 0; i < s.length; i += 1)
    {
        count += p[i] == code_point;
    }

    return count;
}

BUSTER_IMPL ProcessSpawnResult os_process_spawn(StringOs first_argument, StringOsList argv, StringOsList envp, ProcessSpawnOptions options)
{
    ProcessSpawnResult result = {};
    bool pipe_creation_results[STANDARD_STREAM_COUNT];
    bool pipe_result = true;
#if defined(_WIN32)
    bool any_capture = false;
    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & (1 << stream))
        {
            SECURITY_ATTRIBUTES security_attributes = { sizeof(security_attributes), 0, TRUE };
            static_assert(sizeof(HANDLE) == sizeof(FileDescriptor*));
            let pipe_creation_result = CreatePipe((PHANDLE)&result.pipes[stream][0], (PHANDLE)&result.pipes[stream][1], &security_attributes, 0) != 0;
            pipe_creation_results[stream] = pipe_creation_result;

            if (pipe_creation_result)
            {
                any_capture = true;
                // TODO: handle error for this
                SetHandleInformation(result.pipes[stream][0], HANDLE_FLAG_INHERIT, 0);
            }
            else
            {
                pipe_result = false;
            }
        }
    }

    if (pipe_result)
    {
        BUSTER_UNUSED(envp);
        PROCESS_INFORMATION process_information = {};
        STARTUPINFOW startup_info = {sizeof(startup_info)};

        if (any_capture)
        {
            startup_info.dwFlags |= STARTF_USESTDHANDLES;
            startup_info.hStdInput = options.capture & (1 << STANDARD_STREAM_INPUT) ? result.pipes[STANDARD_STREAM_INPUT][1] : GetStdHandle(STD_INPUT_HANDLE);
            startup_info.hStdOutput = options.capture & (1 << STANDARD_STREAM_OUTPUT) ? result.pipes[STANDARD_STREAM_OUTPUT][1] : GetStdHandle(STD_OUTPUT_HANDLE);
            startup_info.hStdError = options.capture & (1 << STANDARD_STREAM_ERROR) ? result.pipes[STANDARD_STREAM_ERROR][1] : GetStdHandle(STD_ERROR_HANDLE);
        }

        if (CreateProcessW(first_argument.pointer, argv, 0, 0, 1, 0, 0, 0, &startup_info, &process_information))
        {
            result.handle = (ProcessHandle*)process_information.hProcess;
        }
        else
        {
            string8_print(S8("Error creating a process: {EOs}\n{SOsL}\n"), os_get_last_error(), argv);
        }
    }

    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & (1 << stream) && pipe_creation_results[stream])
        {
            CloseHandle(result.pipes[stream][1]);

            if (!result.handle)
            {
                CloseHandle(result.pipes[stream][0]);
            }
        }
    }
#else
    pid_t pid = -1;
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attributes;
    let file_actions_init = posix_spawn_file_actions_init(&file_actions);
    let attribute_init = posix_spawnattr_init(&attributes);

    int pipes[STANDARD_STREAM_COUNT][2];

    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        pipes[stream][0] = -1;
        pipes[stream][1] = -1;
    }

    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & (1 << stream))
        {
            let pipe_creation_result = pipe(pipes[stream]) == 0;
            pipe_creation_results[stream] = pipe_creation_result;
            if (pipe_creation_result)
            {
                if (posix_spawn_file_actions_addclose(&file_actions, pipes[stream][0]) != 0)
                {
                    pipe_result = false;
                }

                let fd = generic_fd_to_posix(os_get_standard_stream(stream));

                if (posix_spawn_file_actions_adddup2(&file_actions, pipes[stream][1], fd) != 0)
                {
                    pipe_result = false;
                }

                if (posix_spawn_file_actions_addclose(&file_actions, pipes[stream][1]) != 0)
                {
                    pipe_result = false;
                }
            }
            else
            {
                pipe_result = false;
            }
        }
    }

    if (file_actions_init == 0 && attribute_init == 0 && pipe_result)
    {
        let spawn_result = posix_spawnp(&pid, first_argument.pointer, &file_actions, &attributes, (char**)argv, (char**)envp);

        if (spawn_result != 0)
        {
            pid = -1;
        }
    }

    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & (1 << stream) && pipe_creation_results[stream])
        {
            close(pipes[stream][1]);

            if (pid == -1)
            {
                close(pipes[stream][0]);
            }
        }

        for (int i = 0; i < 2; i += 1)
        {
            result.pipes[stream][i] = pipes[stream][i] >= 0 ? posix_fd_to_generic_fd(pipes[stream][i]) : 0;
        }
    }

    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attributes);

    result.handle = pid == -1 ? (ProcessHandle*)0 : (ProcessHandle*)(u64)pid;
#endif

    if (program_state->input.verbose)
    {
        string8_print(result.handle ? S8("Launched: ") : S8("Failed to launch: "));

        let list = string_os_list_iterator_initialize(argv);
        for (let a = string_os_list_iterator_next(&list); a.pointer; a = string_os_list_iterator_next(&list))
        {
            string8_print(S8("{SOs} "), a);
        }

        string8_print(S8("\n"));
    }

    return result;
}

BUSTER_IMPL ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn)
{
    ProcessWaitResult result = {};
    result.result = PROCESS_RESULT_UNKNOWN;

    if (spawn.handle)
    {
#if defined(_WIN32)
        for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            let read_pipe = (HANDLE)spawn.pipes[stream][0];
            if (read_pipe)
            {
                let start_position = arena->position;

                u8 buffer[16 * 1024];
                u64 total_read_byte_count = 0;

                DWORD iteration_read_byte_count;
                bool iteration_read_result;
                while ((iteration_read_result = ReadFile(read_pipe, buffer, sizeof(buffer), &iteration_read_byte_count, 0) != 0) && iteration_read_byte_count > 0)
                {
                    let iteration_buffer = arena_allocate(arena, u8, iteration_read_byte_count);
                    memcpy(iteration_buffer, buffer, iteration_read_byte_count);

                    total_read_byte_count += iteration_read_byte_count;
                }

                if (!iteration_read_result)
                {
                    let os_error = os_get_last_error();
                    if (os_error.v != ERROR_BROKEN_PIPE)
                    {
                        string8_print(S8("Failed to read from process pipe: {EOs}\n"), os_get_last_error());
                    }
                }

                CloseHandle(read_pipe);

                let length = arena->position - start_position;
                BUSTER_CHECK(total_read_byte_count == length);

                result.streams[stream] = (ByteSlice) { (u8*)arena + start_position, length };
            }
        }

        let wait_result = WaitForSingleObject(spawn.handle, INFINITE);
        if (wait_result == WAIT_OBJECT_0)
        {
            DWORD exit_code;
            if (GetExitCodeProcess(spawn.handle, &exit_code))
            {
                result.result = (ProcessResult)exit_code;
            }
        }
#else
        let pid = (pid_t)(u64)spawn.handle;

        for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            let generic_read_pipe = spawn.pipes[stream][0];
            if (generic_read_pipe)
            {
                let read_pipe = generic_fd_to_posix(generic_read_pipe);
                let start_position = arena->position;

                u8 buffer[16 * 1024];
                u64 total_read_byte_count = 0;

                ssize_t iteration_read_result;
                while ((iteration_read_result = read(read_pipe, buffer, sizeof(buffer))) > 0)
                {
                    let iteration_read_byte_count = (u64)iteration_read_result;
                    let iteration_buffer = arena_allocate(arena, u8, iteration_read_byte_count);
                    memcpy(iteration_buffer, buffer, iteration_read_byte_count);

                    total_read_byte_count += iteration_read_byte_count;
                }
                
                if (iteration_read_result < 0)
                {
                    string8_print(S8("Failed to read from process pipe: {OsE}\n"), os_get_last_error());
                }

                close(read_pipe);

                let length = arena->position - start_position;
                BUSTER_CHECK(total_read_byte_count == length);

                result.streams[stream] = (ByteSlice) { (u8*)arena + start_position, length };
            }
        }

        int status;
        int options = 0;
        struct rusage* usage = {};
        let wait_result = wait4(pid, &status, options, usage);
        // Normal exit
        if ((wait_result == pid) & WIFEXITED(status))
        {
            let exit_code = WEXITSTATUS(status);
            result.result = (ProcessResult)exit_code;
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

BUSTER_IMPL OsError os_get_last_error()
{
    OsError result = {};
#if defined(_WIN32)
    result.v = GetLastError();
#else
    let error = errno;
    static_assert(sizeof(result.v) == sizeof(error));
    result.v = (typeof(result.v))error;
#endif
    return result;
}

BUSTER_IMPL StringOs os_error_write_message(StringOs string, OsError error)
{
    BUSTER_CHECK(string.length == BUSTER_OS_ERROR_BUFFER_MAX_LENGTH);
    StringOs result = {};
#if defined(_WIN32)
    let length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, (DWORD)error.v, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), string.pointer, string.length > ~(DWORD)0 ? ~(DWORD)0 : (DWORD)string.length, 0);
    if (length != 0)
    {
        result = string_os_from_pointer_length(string.pointer, length - 1);
    }
#else
    let error_raw_string = strerror((int)error.v);
    let error_string = string8_from_pointer(error_raw_string);
    result = string;
    memcpy(result.pointer, error_string.pointer, BUSTER_SLICE_SIZE(error_string));
    result.length = error_string.length;
#endif
    return result;
}

BUSTER_IMPL StringOs os_get_environment_variable(StringOs variable)
{
    StringOs result = {};
#if defined(_WIN32)
    let envp = GetEnvironmentStringsW();
    let it = envp;
    while (*it)
    {
        let length = string16_length(it);
        let full_env = string_os_from_pointer_length(it, length);
        it += length + 1;
        let key_index = string_first_code_point(full_env, '=');
        let key = string_os_from_pointer_length(full_env.pointer, key_index);
        if (string_equal(key, variable))
        {
            result = string_os_from_pointer_length(full_env.pointer + (key_index + 1), full_env.length - (key_index + 1));
            break;
        }
    }
#else
    let pointer = getenv(variable.pointer);
    result = (StringOs){pointer, string8_length(pointer)};
#endif
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
    return (u64)file_size.QuadPart;
#endif
}

BUSTER_IMPL bool os_is_tty(FileDescriptor* file)
{
    bool result = false;
#if defined(_WIN32)
    DWORD mode;
    let handle = (HANDLE)file;
    result = GetConsoleMode(handle, &mode) != 0;
#else
    let fd = generic_fd_to_posix(file);
    result = isatty(fd);
#endif
    return result;
}

BUSTER_IMPL bool os_unreserve(void* address, u64 size)
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

