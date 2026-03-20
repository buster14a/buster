#pragma once
#include <buster/os.h>
#include <buster/system_headers.h>
#include <buster/assertion.h>
#include <buster/arena.h>
#include <buster/arguments.h>

//- rjf: doubly-linked-lists
#define DLLInsert_NPZ(nil,f,l,p,n,next,prev) (CheckNil(nil,f) ? \
((f) = (l) = (n), SetNil(nil,(n)->next), SetNil(nil,(n)->prev)) :\
CheckNil(nil,p) ? \
((n)->next = (f), (f)->prev = (n), (f) = (n), SetNil(nil,(n)->prev)) :\
((p)==(l)) ? \
((l)->next = (n), (n)->prev = (l), (l) = (n), SetNil(nil, (n)->next)) :\
(((!CheckNil(nil,p) && CheckNil(nil,(p)->next)) ? (0) : ((p)->next->prev = (n))), ((n)->next = (p)->next), ((p)->next = (n)), ((n)->prev = (p))))
#define DLLPushBack_NPZ(nil,f,l,n,next,prev) DLLInsert_NPZ(nil,f,l,l,n,next,prev)
#define DLLPushFront_NPZ(nil,f,l,n,next,prev) DLLInsert_NPZ(nil,l,f,f,n,prev,next)
#define DLLRemove_NPZ(nil,f,l,n,next,prev) (((n) == (f) ? (f) = (n)->next : (0)),\
((n) == (l) ? (l) = (l)->prev : (0)),\
(CheckNil(nil,(n)->prev) ? (0) :\
((n)->prev->next = (n)->next)),\
(CheckNil(nil,(n)->next) ? (0) :\
((n)->next->prev = (n)->prev)))

//- rjf: singly-linked, doubly-headed lists (queues)
#define SLLQueuePush_NZ(nil,f,l,n,next) (CheckNil(nil,f)?\
((f)=(l)=(n),SetNil(nil,(n)->next)):\
((l)->next=(n),(l)=(n),SetNil(nil,(n)->next)))
#define SLLQueuePushFront_NZ(nil,f,l,n,next) (CheckNil(nil,f)?\
((f)=(l)=(n),SetNil(nil,(n)->next)):\
((n)->next=(f),(f)=(n)))
#define SLLQueuePop_NZ(nil,f,l,next) ((f)==(l)?\
(SetNil(nil,f),SetNil(nil,l)):\
((f)=(f)->next))

//- rjf: singly-linked, singly-headed lists (stacks)
#define SLLStackPush_N(f,n,next) ((n)->next=(f), (f)=(n))
#define SLLStackPop_N(f,next) ((f)=(f)->next)

//- rjf: doubly-linked-list helpers
#define DLLInsert_NP(f,l,p,n,next,prev) DLLInsert_NPZ(0,f,l,p,n,next,prev)
#define DLLPushBack_NP(f,l,n,next,prev) DLLPushBack_NPZ(0,f,l,n,next,prev)
#define DLLPushFront_NP(f,l,n,next,prev) DLLPushFront_NPZ(0,f,l,n,next,prev)
#define DLLRemove_NP(f,l,n,next,prev) DLLRemove_NPZ(0,f,l,n,next,prev)
#define DLLInsert(f,l,p,n) DLLInsert_NPZ(0,f,l,p,n,next,prev)
#define DLLPushBack(f,l,n) DLLPushBack_NPZ(0,f,l,n,next,prev)
#define DLLPushFront(f,l,n) DLLPushFront_NPZ(0,f,l,n,next,prev)
#define DLLRemove(f,l,n) DLLRemove_NPZ(0,f,l,n,next,prev)

//- rjf: singly-linked, doubly-headed list helpers
#define SLLQueuePush_N(f,l,n,next) SLLQueuePush_NZ(0,f,l,n,next)
#define SLLQueuePushFront_N(f,l,n,next) SLLQueuePushFront_NZ(0,f,l,n,next)
#define SLLQueuePop_N(f,l,next) SLLQueuePop_NZ(0,f,l,next)
#define SLLQueuePush(f,l,n) SLLQueuePush_NZ(0,f,l,n,next)
#define SLLQueuePushFront(f,l,n) SLLQueuePushFront_NZ(0,f,l,n,next)
#define SLLQueuePop(f,l) SLLQueuePop_NZ(0,f,l,next)

//- rjf: singly-linked, singly-headed list helpers
#define SLLStackPush(f,n) SLLStackPush_N(f,n,next)
#define SLLStackPop(f) SLLStackPop_N(f,next)


BUSTER_GLOBAL_LOCAL OsEntity* os_entity_allocate(OsEntityKind kind)
{
    OsEntity* result = 0;

    pthread_mutex_lock(&os_state.entity_mutex);
    {
        result = os_state.entity_free_list;
        if (result)
        {
            SLLStackPop(os_state.entity_free_list);
        }
        else
        {
            result = arena_allocate(os_state.entity_arena, OsEntity, 1);
        }
    }
    pthread_mutex_unlock(&os_state.entity_mutex);
    memset(result, 0, sizeof(*result));
    result->kind = kind;
    return result;
}

BUSTER_GLOBAL_LOCAL void os_entity_release(OsEntity* entity)
{
    pthread_mutex_lock(&os_state.entity_mutex);
    {
        SLLStackPush(os_state.entity_free_list, entity);
    }
    pthread_mutex_unlock(&os_state.entity_mutex);
}

[[gnu::cold]] BUSTER_F_IMPL bool is_debugger_present()
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

[[noreturn]] [[gnu::cold]] BUSTER_F_IMPL void os_fail()
{
    if (is_debugger_present())
    {
        BUSTER_TRAP();
    }

    os_exit(1);
}

[[gnu::noreturn]] BUSTER_F_IMPL void os_exit(u32 code)
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
        MAP_PRIVATE * flags.priv |
        MAP_ANON * flags.anonymous |
        MAP_NORESERVE * flags.no_reserve;

    return result;
}

BUSTER_GLOBAL_LOCAL OsFileDescriptor* posix_fd_to_generic_fd(int fd)
{
    BUSTER_CHECK(fd >= 0);
    return (OsFileDescriptor*)(u64)(fd);
}

BUSTER_GLOBAL_LOCAL int generic_fd_to_posix(OsFileDescriptor* fd)
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
BUSTER_GLOBAL_LOCAL OsThreadHandle* os_posix_thread_to_generic(pthread_t handle)
{
    BUSTER_CHECK(handle != 0);
    return (OsThreadHandle*)handle;
}

BUSTER_GLOBAL_LOCAL pthread_t os_posix_thread_from_generic(OsThreadHandle* handle)
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

BUSTER_F_IMPL bool os_commit(void* address, u64 size, ProtectionFlags protection, bool lock)
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

BUSTER_F_IMPL void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map)
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

BUSTER_F_IMPL OsFileDescriptor* os_get_standard_stream(StandardStream stream)
{
    OsFileDescriptor* result = {};
#if defined(__linux__) || defined(__APPLE__)
    int fds[] = {
        [(u64)StandardStream::Input] = STDIN_FILENO,
        [(u64)StandardStream::Output] = STDOUT_FILENO,
        [(u64)StandardStream::Error] = STDERR_FILENO,
    };
    static_assert(BUSTER_ARRAY_LENGTH(fds) == (u64)StandardStream::Count);
    result = posix_fd_to_generic_fd(fds[(u64)stream]);
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

BUSTER_F_IMPL OsFileDescriptor* os_get_stdout()
{
    OsFileDescriptor* result = {};
#if defined(__linux__) || defined(__APPLE__)
    result = posix_fd_to_generic_fd(STDOUT_FILENO);
#elif defined(_WIN32)
    result = (FileDescriptor*)GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    return result;
}

__attribute__((used)) BUSTER_GLOBAL_LOCAL TimeDataType frequency;

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

BUSTER_GLOBAL_LOCAL void thread_entry_point(ThreadCallback* user_entry_point, void* user_argument)
{
    let thread_context = thread_context_allocate();
    thread_context_select(thread_context);
    user_entry_point(user_argument);
    thread_context_release(thread_context);
}

#if defined(__linux__) || defined(__APPLE__)
BUSTER_GLOBAL_LOCAL void* pthread_entry_point(void* argument)
{
    let entity = (OsEntity*)argument;
    thread_entry_point(entity->thread.callback, entity->thread.argument);
    return (void*)0;
}
#endif

BUSTER_F_IMPL OsThreadHandle* os_thread_create(ThreadCreateOptions options)
{
    BUSTER_UNUSED(options);
    OsEntity* result = os_entity_allocate(OsEntityKind::OS_ENTITY_KIND_THREAD);
    result->thread.callback = options.callback;
    result->thread.argument = options.argument;
#if defined (__linux__) || defined(__APPLE__)
    let create_result = pthread_create(&result->thread.handle, 0, &pthread_entry_point, result);
    bool os_result = create_result == 0;
    if (!os_result)
    {
        os_entity_release(result);
        result = 0;
    }
#elif defined (_WIN32)
#endif
    return (OsThreadHandle*)result;
}

BUSTER_F_IMPL bool os_thread_join(OsThreadHandle* handle)
{
    let entity = (OsEntity*)handle;

    bool result;
#if defined(__linux__) || defined(__APPLE__)
    void* void_return_value = 0;
    let join_result = pthread_join(entity->thread.handle, &void_return_value);
    result = (join_result == 0);
#elif defined(_WIN32)
    WaitForSingleObject(thread_handle, INFINITE);

    DWORD exit_code;
    let result = GetExitCodeThread(thread_handle, &exit_code) != 0;
    if (result)
    {
        return_code = (u32)exit_code;
    }

    CloseHandle(thread_handle);
#endif
    os_entity_release(entity);
    return result;
}

BUSTER_F_IMPL StringOs os_path_absolute(StringOs buffer, StringOs relative_file_path)
{
    StringOs result = {};
#if defined(__linux__) || defined(__APPLE__)
    let syscall_result = realpath((char*)relative_file_path.pointer, (char*)buffer.pointer);

    if (syscall_result)
    {
        result = string8_from_pointer(syscall_result);
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

BUSTER_F_IMPL void os_make_directory(StringOs path)
{
#if defined(__linux__) || defined(__APPLE__)
    mkdir((const char*)path.pointer, 0755);
#elif defined(_WIN32)
    CreateDirectoryW(path.pointer, 0);
#endif
}

BUSTER_F_IMPL OsFileDescriptor* os_file_open(StringOs path, OpenFlags flags, OpenPermissions permissions)
{
    BUSTER_CHECK(!path.pointer[path.length]);
    OsFileDescriptor* result = 0;
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
        result = (OsFileDescriptor*)(u64)fd;
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

BUSTER_GLOBAL_LOCAL u64 os_file_write_partially(OsFileDescriptor* file_descriptor, void* pointer, u64 length)
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

BUSTER_F_IMPL void os_file_write(OsFileDescriptor* file_descriptor, ByteSlice buffer)
{
    u64 total_written_byte_count = 0;

    while (total_written_byte_count < buffer.length)
    {
        let written_byte_count = os_file_write_partially(file_descriptor, buffer.pointer + total_written_byte_count, buffer.length - total_written_byte_count);
        total_written_byte_count += written_byte_count;
    }
}

BUSTER_GLOBAL_LOCAL u64 os_file_read_partially(OsFileDescriptor* file_descriptor, void* buffer, u64 byte_count)
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

BUSTER_F_IMPL u64 os_file_read(OsFileDescriptor* file_descriptor, ByteSlice buffer, u64 byte_count)
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

BUSTER_F_IMPL FileStats os_file_get_stats(OsFileDescriptor* file_descriptor, FileStatsOptions options)
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

BUSTER_F_IMPL bool os_file_close(OsFileDescriptor* file_descriptor)
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

BUSTER_F_IMPL u64 string8_code_point_count(String8 s, u8 code_point)
{
    u64 count = 0;
    let restrict p = s.pointer;
    for (u64 i = 0; i < s.length; i += 1)
    {
        count += p[i] == code_point;
    }

    return count;
}

BUSTER_F_IMPL ProcessSpawnResult os_process_spawn(StringOs first_argument, StringOsList argv, StringOsList envp, ProcessSpawnOptions options)
{
    ProcessSpawnResult result = {};
    bool pipe_creation_results[(u64)StandardStream::Count];
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

    int pipes[(u64)StandardStream::Count][2];

    for (EACH_ENUM(StandardStream, stream))
    {
        pipes[(u64)stream][0] = -1;
        pipes[(u64)stream][1] = -1;
    }

    for (EACH_ENUM(StandardStream, stream))
    {
        if (options.capture & ((u64)1 << (u64)stream))
        {
            let pipe_creation_result = pipe(pipes[(u64)stream]) == 0;
            pipe_creation_results[(u64)stream] = pipe_creation_result;
            if (pipe_creation_result)
            {
                if (posix_spawn_file_actions_addclose(&file_actions, pipes[(u64)stream][0]) != 0)
                {
                    pipe_result = false;
                }

                let fd = generic_fd_to_posix(os_get_standard_stream(stream));

                if (posix_spawn_file_actions_adddup2(&file_actions, pipes[(u64)stream][1], fd) != 0)
                {
                    pipe_result = false;
                }

                if (posix_spawn_file_actions_addclose(&file_actions, pipes[(u64)stream][1]) != 0)
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

    for (EACH_ENUM_INT(StandardStream, stream))
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

    result.handle = (OsProcessHandle*)(pid == -1 ? 0 : (u64)pid);
#endif

    if (flag_get(program_state->input.flags, ProgramFlag::Verbose))
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

BUSTER_F_IMPL ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn)
{
    ProcessWaitResult result = {};
    result.result = ProcessResult::Unknown;

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
                        string8_print(S8("Failed to read from process pipe: {EOs}\n"), os_error);
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

        for (EACH_ENUM_INT(StandardStream, stream))
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
            wait_result = (int)ProcessResult::Failed;
        }
#endif
    }

    return result;
}

BUSTER_F_IMPL OsError os_get_last_error()
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

BUSTER_F_IMPL StringOs os_error_write_message(StringOs string, OsError error)
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

BUSTER_F_IMPL StringOs os_get_environment_variable(StringOs variable)
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
    result = string8_from_pointer(pointer);
#endif
    return result;
}

BUSTER_F_IMPL u64 os_file_get_size(OsFileDescriptor* file_descriptor)
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

BUSTER_F_IMPL bool os_is_tty(OsFileDescriptor* file)
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

BUSTER_F_IMPL bool os_unreserve(void* address, u64 size)
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

BUSTER_F_IMPL OsModuleHandle* os_dynamic_library_load(StringOs library)
{
    OsModuleHandle* result = {};
    BUSTER_CHECK(BUSTER_SLICE_IS_ZERO_TERMINATED(library));

#if defined(_WIN32)
    result = (OSModuleHandle*)LoadLibraryW(library.pointer);
#else
    result = (OsModuleHandle*) dlopen(library.pointer, RTLD_NOW | RTLD_LOCAL);
#endif

    return result;
}

BUSTER_F_IMPL void os_dynamic_library_unload(OsModuleHandle* module)
{
#if defined(_WIN32)
    result = (OSModule*)LoadLibraryW(library.pointer);
#else
    dlclose(module);
#endif
}

BUSTER_F_IMPL OsSymbol* os_dynamic_library_function_load(OsModuleHandle* module, String8 symbol)
{
    OsSymbol* result = {};

#if defined(_WIN32)
    result = GetProcAddress((HMODULE)module, symbol.pointer);
#else
    result = (OsSymbol*)dlsym((void*)module, symbol.pointer);
#endif

    return result;
}

BUSTER_F_IMPL u32 os_get_logical_thread_count()
{
    u32 result;

#if defined(__linux__)
    result = (u32)get_nprocs();
#else
#endif

    return result;
}

BUSTER_F_IMPL u64 os_get_page_size()
{
    u64 page_size;
#if defined(__linux__)
    page_size = (u64)getpagesize();
#else
#endif
    return page_size;
}

BUSTER_F_IMPL OsProcessHandle* os_get_current_process_handle()
{
    OsProcessHandle* result;
#if defined(__linux__) || defined(__APPLE__)
     result = (OsProcessHandle*)(u64)getpid();
#else
#endif
     return result;
}

BUSTER_F_IMPL OsThreadHandle* os_get_current_thread_handle()
{
    OsThreadHandle* result;
#if defined(__linux__) || defined(__APPLE__)
    result = (OsThreadHandle*)(u64)pthread_self();
#else
#endif
    return result;
}

BUSTER_F_IMPL void os_thread_set_name(StringOs thread_name)
{
#if defined(__linux__) || defined(__APPLE__)
    pthread_setname_np(pthread_self(), thread_name.pointer);
#else
    BUSTER_TRAP();
#endif
}

BUSTER_F_IMPL OsBarrierHandle* os_barrier_allocate(u32 count)
{
    let entity = os_entity_allocate(OsEntityKind::OS_ENTITY_KIND_BARRIER);
#if defined(__linux__) || defined(__APPLE__)
    if (pthread_barrier_init(&entity->barrier, 0, count) != 0)
    {
        os_entity_release(entity);
        entity = 0;
    }
#endif
    return (OsBarrierHandle*)entity;
}

BUSTER_F_IMPL void os_barrier_release(OsBarrierHandle* barrier)
{
    let entity = (OsEntity*)barrier;
    pthread_barrier_destroy(&entity->barrier);
    os_entity_release(entity);
}

BUSTER_F_IMPL ThreadContext* thread_context_selected()
{
    return thread_context_thread_local;
}

BUSTER_F_IMPL ThreadContext* thread_context_allocate()
{
    Arena* arenas[(u64)ScratchArenaId::Count];
    for (EACH_ENUM_INT(ScratchArenaId, i))
    {
        arenas[i] = arena_create((ArenaCreation){});
    }

    let result = arena_allocate(arenas[0], ThreadContext, 1);
    memset(result, 0, sizeof(*result));
    memcpy(result->arenas, arenas, sizeof(arenas));
    result->lane_context.lane_count = 1;
    return result;
}

BUSTER_F_IMPL void thread_context_release(ThreadContext* thread_context)
{
    u64 i = BUSTER_ARRAY_LENGTH(thread_context->arenas);
    while (i--)
    {
        arena_destroy(thread_context->arenas[i], 1);
    }
}

BUSTER_F_IMPL LaneContext thread_context_set_lane(LaneContext lane_context)
{
    let thread_context = thread_context_selected();
    let restore = thread_context->lane_context;
    thread_context->lane_context = lane_context;
    return restore;
}

BUSTER_F_IMPL void thread_context_select(ThreadContext* context)
{
    thread_context_thread_local = context;
}

BUSTER_F_IMPL void os_condition_variable_broadcast(OsConditionVariableHandle* condition_variable)
{
    let entity = (OsEntity*)condition_variable;
    pthread_cond_broadcast(&entity->condition_variable.handle);
}

BUSTER_F_IMPL OsConditionVariableHandle* os_condition_variable_allocate()
{
    let entity = os_entity_allocate(OsEntityKind::OS_ENTITY_KIND_CONDITION_VARIABLE);
    pthread_condattr_t attributes;
    let result_code_attr = pthread_condattr_init(&attributes);
    let result_code_clock = result_code_attr == 0 ? pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC) : -1;
    let result_code_cv = (result_code_attr == 0 && result_code_clock == 0) ? pthread_cond_init(&entity->condition_variable.handle, &attributes) : -1;
    let result_code_rw = result_code_cv == 0 ? pthread_mutex_init(&entity->condition_variable.rw_lock, 0) : 0;

    if (result_code_attr == 0)
    {
        pthread_condattr_destroy(&attributes);
    }

    if (!(result_code_cv == 0 && result_code_rw == 0))
    {
        if (result_code_cv == 0)
        {
            pthread_cond_destroy(&entity->condition_variable.handle);
        }

        os_entity_release(entity);
        entity = 0;
    }

    return (OsConditionVariableHandle*)entity;
}

BUSTER_F_IMPL void os_condition_variable_release(OsConditionVariableHandle* condition_variable)
{
    let entity = (OsEntity*)condition_variable;
    pthread_cond_destroy(&entity->condition_variable.handle);
    pthread_mutex_destroy(&entity->condition_variable.rw_lock);
    os_entity_release(entity);
}

BUSTER_F_IMPL OsMutexHandle* os_mutex_allocate()
{
    let entity = os_entity_allocate(OsEntityKind::OS_ENTITY_KIND_MUTEX);
    let result_code = pthread_mutex_init(&entity->mutex, 0);
    bool success = result_code == 0;
    if (!success)
    {
        os_entity_release(entity);
        entity = 0;
    }
    return (OsMutexHandle*)entity;
}

BUSTER_F_IMPL void os_mutex_release(OsMutexHandle* mutex)
{
    OsEntity* entity = (OsEntity*)mutex;
    pthread_mutex_destroy(&entity->mutex);
    os_entity_release(entity);
}

BUSTER_F_IMPL Arena* thread_context_get_scratch(Arena** conflicts, u64 count)
{
    let thread_context = thread_context_selected();
    Arena** arena_pointer = thread_context->arenas;

    Arena* result = 0;

    for (u64 arena_i = 0; arena_i < BUSTER_ARRAY_LENGTH(thread_context->arenas); arena_i += 1, arena_pointer += 1)
    {
        Arena** conflict_pointer = conflicts;
        bool has_conflict = false;

        for (u64 arena_j = 0; arena_j < count; arena_j += 1, conflict_pointer += 1)
        {
            if (*arena_pointer == *conflict_pointer)
            {
                has_conflict = true;
                break;
            }
        }

        if (!has_conflict)
        {
            result = *arena_pointer;
            break;
        }
    }

    return result;
}

BUSTER_F_IMPL u64 lane_index()
{
    return thread_context_selected()->lane_context.lane_index;
}

BUSTER_F_IMPL void os_barrier_wait(OsBarrierHandle* barrier)
{
    let entity = (OsEntity*)barrier;
    pthread_barrier_wait(&entity->barrier);
}

BUSTER_F_IMPL void thread_context_lane_barrier_wait(void* broadcast_pointer, u64 broadcast_size, u64 broadcast_source_lane_index)
{
    let thread_context = thread_context_selected();
    let broadcast_size_clamped = BUSTER_CLAMP_TOP(broadcast_size, sizeof(thread_context->lane_context.broadcast_memory[0]));

    if (broadcast_pointer != 0 && lane_index() == broadcast_source_lane_index)
    {
        memcpy(thread_context->lane_context.broadcast_memory, broadcast_pointer, broadcast_size_clamped);
    }

    os_barrier_wait(thread_context->lane_context.barrier);

    if (broadcast_pointer != 0 && lane_index() != broadcast_source_lane_index)
    {
        memcpy(broadcast_pointer, thread_context->lane_context.broadcast_memory, broadcast_size_clamped);
    }

    if (broadcast_pointer != 0)
    {
        os_barrier_wait(thread_context->lane_context.barrier);
    }
}

BUSTER_F_IMPL void lane_sync()
{
    thread_context_lane_barrier_wait(0, 0, 0);
}

BUSTER_F_IMPL void os_mutex_take(OsMutexHandle* mutex)
{
    let entity = (OsEntity*)mutex;
    pthread_mutex_lock(&entity->mutex);
}

BUSTER_F_IMPL void os_mutex_drop(OsMutexHandle* mutex)
{
    let entity = (OsEntity*)mutex;
    pthread_mutex_unlock(&entity->mutex);
}

BUSTER_F_IMPL bool os_condition_variable_wait(OsConditionVariableHandle* condition_variable, OsMutexHandle* mutex, u64 endt_us)
{
    let cv_entity = (OsEntity*)condition_variable;
    let mutex_entity = (OsEntity*)mutex;

    struct timespec endt_ts;
    endt_ts.tv_sec = endt_us / (1000 * 1000);
    endt_ts.tv_nsec = 1000 * ((s64)endt_us - ((s64)endt_us / (1000 * 1000)) * (1000 * 1000));
    let wait_result = pthread_cond_timedwait(&cv_entity->condition_variable.handle, &mutex_entity->mutex, &endt_ts);
    let result = wait_result != ETIMEDOUT;
    return result;
}

BUSTER_F_IMPL u64 os_now_microseconds()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  u64 result = (u64)(ts.tv_sec * (1000 * 1000) + (ts.tv_nsec / 1000));
  return result;
}
