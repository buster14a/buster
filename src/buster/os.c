#pragma once
#include <buster/os.h>
#include <buster/system_headers.h>
#include <buster/arena.h>
#include <buster/string.h>

#if BUSTER_LINK_LIBC && !BUSTER_SINGLE_THREADED && defined(__TINYC__) && defined(__APPLE__) && BUSTER_CPU_ARCH_AARCH64
#define BUSTER_THREAD_CONTEXT_USE_PTHREAD_TLS 1
#else
#define BUSTER_THREAD_CONTEXT_USE_PTHREAD_TLS 0
#endif

#if BUSTER_THREAD_CONTEXT_USE_PTHREAD_TLS
BUSTER_GLOBAL_LOCAL pthread_key_t thread_context_tls_key;
BUSTER_GLOBAL_LOCAL pthread_once_t thread_context_tls_key_once = PTHREAD_ONCE_INIT;

BUSTER_GLOBAL_LOCAL void thread_context_tls_key_initialize(void)
{
    int result = pthread_key_create(&thread_context_tls_key, 0);
    if (result != 0)
    {
        BUSTER_TRAP();
    }
}

BUSTER_GLOBAL_LOCAL void thread_context_tls_key_ensure_initialized(void)
{
    int result = pthread_once(&thread_context_tls_key_once, thread_context_tls_key_initialize);
    if (result != 0)
    {
        BUSTER_TRAP();
    }
}
#else
BUSTER_THREAD_LOCAL_DECL ThreadContext* thread_context_thread_local;
#endif

#if defined(_MSC_VER)
RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions = {0};
#endif

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

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL u64 w32_file_size_from_file_information(BY_HANDLE_FILE_INFORMATION file_information)
{
    return ((u64)file_information.nFileSizeHigh << 32) | (u64)file_information.nFileSizeLow;
}

BUSTER_GLOBAL_LOCAL void w32_file_stats_from_file_information(FileStats* stats, FileStatsOptions options, BY_HANDLE_FILE_INFORMATION file_information)
{
    if (options.size)
    {
        stats->size = w32_file_size_from_file_information(file_information);
    }

    if (options.modified_time)
    {
        u64 file_time_100ns = ((u64)file_information.ftLastWriteTime.dwHighDateTime << 32) | (u64)file_information.ftLastWriteTime.dwLowDateTime;
        u64 unix_epoch_100ns = (u64)11644473600ULL * (u64)10000000ULL;
        if (file_time_100ns >= unix_epoch_100ns)
        {
            u64 unix_time_100ns = file_time_100ns - unix_epoch_100ns;
            stats->modified_time_s = unix_time_100ns / (u64)10000000;
            stats->modified_time_ns = (unix_time_100ns % (u64)10000000) * (u64)100;
        }
    }
}
#endif

BUSTER_GLOBAL_LOCAL void os_entity_lock(void)
{
#if defined(_WIN32)
    EnterCriticalSection(&os_state.entity_mutex);
#else
    pthread_mutex_lock(&os_state.entity_mutex);
#endif
}

BUSTER_GLOBAL_LOCAL void os_entity_unlock(void)
{
#if defined(_WIN32)
    LeaveCriticalSection(&os_state.entity_mutex);
#else
    pthread_mutex_unlock(&os_state.entity_mutex);
#endif
}

BUSTER_GLOBAL_LOCAL OsEntity* os_entity_allocate(OsEntityKind kind)
{

    os_entity_lock();
    OsEntity* result = os_state.entity_free_list;
    if (result)
    {
        SLLStackPop(os_state.entity_free_list);
    }
    else
    {
        result = arena_allocate(os_state.entity_arena, OsEntity, 1);
    }
    memset(result, 0, sizeof(*result));
    result->kind = kind;
    os_entity_unlock();
    return result;
}

BUSTER_GLOBAL_LOCAL void os_entity_release(OsEntity* entity)
{
    os_entity_lock();
    {
        SLLStackPush(os_state.entity_free_list, entity);
    }
    os_entity_unlock();
}

BUSTER_COLD bool is_debugger_present(void)
{
    if (BUSTER_UNLIKELY(!program_state->is_debugger_present_called))
    {
        program_state->is_debugger_present_called = true;
#if defined(__linux__)
        bool os_result = ptrace(PTRACE_TRACEME, 0, 0, 0) == -1;
        program_state->_is_debugger_present = os_result != 0;
#elif defined(__APPLE__)
#elif defined(_WIN32)
        BOOL os_result = IsDebuggerPresent();
        program_state->_is_debugger_present = os_result != 0;
#else
    BUSTER_TRAP();
#endif
    }

    return (bool)program_state->_is_debugger_present;
}

BUSTER_NORETURN BUSTER_COLD void os_fail(void)
{
    if (is_debugger_present())
    {
        BUSTER_TRAP();
    }

    os_exit(1);
}

BUSTER_NORETURN BUSTER_COLD void os_exit(u32 code)
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

BUSTER_GLOBAL_LOCAL void* generic_fd_to_windows(OsFileDescriptor* fd)
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
    int os_result = mlock(address, size);
    result = os_result == 0;
    if (result)
    {
        os_result = munlock(address, size);
    }
    result = os_result == 0;
#elif defined(_WIN32)
#if defined(_MSC_VER)
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
#else
    BUSTER_UNUSED(address);
    BUSTER_UNUSED(size);
#endif
#endif
    return result;
}

bool os_commit(void* address, u64 size, ProtectionFlags protection, bool lock)
{
    bool result = 1;

#if defined(__linux__) || defined(__APPLE__)
    int protection_flags = os_posix_protection_flags(protection);
    int os_result = mprotect(address, size, protection_flags);
    result = os_result == 0;
#elif defined(_WIN32)
    DWORD protection_flags = os_windows_protection_flags(protection);
    void* os_result = VirtualAlloc(address, size, MEM_COMMIT, protection_flags);
    result = os_result != 0;
#endif

    if (result & lock)
    {
        os_lock_and_unlock(address, size);
    }

    return result;
}

void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map)
{
    void* address = 0;

#if defined(__linux__) || defined(__APPLE__)
    int protection_flags = os_posix_protection_flags(protection);
    int map_flags = os_posix_map_flags(map);

    address = mmap(base, size, protection_flags, map_flags, -1, 0);
    if (address == MAP_FAILED)
    {
        address = 0;
    }
#elif defined(_WIN32)
    DWORD allocation_flags = os_windows_allocation_flags(map);
    DWORD protection_flags = os_windows_protection_flags(protection);
    address = VirtualAlloc(base, size, allocation_flags, protection_flags);
#endif
    return address;
}

OsFileDescriptor* os_get_standard_stream(StandardStream stream)
{
    OsFileDescriptor* result = {0};
#if defined(__linux__) || defined(__APPLE__)
    int fds[] = {
        [(u64)STANDARD_STREAM_INPUT] = STDIN_FILENO,
        [(u64)STANDARD_STREAM_OUTPUT] = STDOUT_FILENO,
        [(u64)STANDARD_STREAM_ERROR] = STDERR_FILENO,
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(fds) == (u64)STANDARD_STREAM_COUNT);
    result = posix_fd_to_generic_fd(fds[stream]);
#elif defined(_WIN32)
    DWORD descriptors[] = {
        [STANDARD_STREAM_INPUT] = STD_INPUT_HANDLE,
        [STANDARD_STREAM_OUTPUT] = STD_OUTPUT_HANDLE,
        [STANDARD_STREAM_ERROR] = STD_ERROR_HANDLE,
    };
    result = (OsFileDescriptor*)GetStdHandle(descriptors[stream]);
#endif
    return result;
}

OsFileDescriptor* os_get_stdout(void)
{
    OsFileDescriptor* result = {0};
#if defined(__linux__) || defined(__APPLE__)
    result = posix_fd_to_generic_fd(STDOUT_FILENO);
#elif defined(_WIN32)
    result = (OsFileDescriptor*)GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    return result;
}

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL OsThreadHandle* os_windows_thread_to_generic(HANDLE handle)
{
    BUSTER_CHECK(handle != 0);
    return (OsThreadHandle*)handle;
}

BUSTER_GLOBAL_LOCAL HANDLE os_windows_thread_from_generic(OsThreadHandle* handle)
{
    BUSTER_CHECK(handle != 0);
    return (HANDLE)handle;
}
#endif

BUSTER_GLOBAL_LOCAL void thread_entry_point(ThreadCallback* user_entry_point, void* user_argument)
{
    ThreadContext* thread_context = thread_context_allocate();
    thread_context_select(thread_context);
    user_entry_point(user_argument);
    thread_context_release(thread_context);
}

#if defined(__linux__) || defined(__APPLE__)
BUSTER_GLOBAL_LOCAL void* pthread_entry_point(void* argument)
{
    OsEntity* entity = (OsEntity*)argument;
    thread_entry_point(entity->thread.callback, entity->thread.argument);
    return (void*)0;
}
#endif

OsThreadHandle* os_thread_create(ThreadCreateOptions options)
{
    BUSTER_UNUSED(options);
    OsEntity* result = os_entity_allocate(OS_ENTITY_KIND_THREAD);
    result->thread.callback = options.callback;
    result->thread.argument = options.argument;
#if defined (__linux__) || defined(__APPLE__)
    int create_result = pthread_create(&result->thread.handle, 0, &pthread_entry_point, result);
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

bool os_thread_join(OsThreadHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;

    bool result;
#if defined(__linux__) || defined(__APPLE__)
    void* void_return_value = 0;
    int join_result = pthread_join(entity->thread.handle, &void_return_value);
    result = (join_result == 0);
#elif defined(_WIN32)
    WaitForSingleObject(handle, INFINITE);

    DWORD exit_code;
    BOOL exit_code_result = GetExitCodeThread(handle, &exit_code);
    if (exit_code_result)
    {
        result = (u32)exit_code;
    }
    else
    {
        result = ~(DWORD)0;
    }

    CloseHandle(handle);
#endif
    os_entity_release(entity);
    return result;
}

StringOs os_path_absolute(StringOs buffer, StringOs relative_file_path)
{
    StringOs result = {0};
#if defined(__linux__) || defined(__APPLE__)
    char* syscall_result = realpath((char*)relative_file_path.pointer, (char*)buffer.pointer);

    if (syscall_result)
    {
        result = string_from_pointer(syscall_result);
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

void os_make_directory(StringOs path)
{
#if defined(__linux__) || defined(__APPLE__)
    mkdir((const char*)path.pointer, 0755);
#elif defined(_WIN32)
    CreateDirectoryW(path.pointer, 0);
#endif
}

OsFileDescriptor* os_file_open(String8 path, OpenFlags flags, OpenPermissions permissions)
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
    TemporalArena scratch = scratch_begin(0, 0);

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

    String16 path_w = string16_from_string8(scratch.arena, path, true);
    HANDLE fd = CreateFileW(path_w.pointer, desired_access, shared_mode, &security_attributes, creation_disposition, flags_and_attributes, template_file);
    if (fd != INVALID_HANDLE_VALUE)
    {
        result = (OsFileDescriptor*)fd;
    }
    else
    {
        if (program_flag_get(PROGRAM_FLAG_VERBOSE))
        {
            string_print(S8("Error: {EOs}\n"), os_get_last_error());
        }
    }
    scratch_end(scratch);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL u64 os_file_write_partially(OsFileDescriptor* file_descriptor, void* pointer, u64 length)
{
#if defined(__linux__) || defined(__APPLE__)
    int fd = generic_fd_to_posix(file_descriptor);
    ssize_t result = write(fd, pointer, length);
    BUSTER_CHECK(result > 0);
    return (u64)result;
#elif defined(_WIN32)
    HANDLE fd = generic_fd_to_windows(file_descriptor);
    DWORD written_byte_count = 0;
    BOOL result = WriteFile(fd, pointer, (u32)length, &written_byte_count, 0);
    BUSTER_CHECK(result);
    return written_byte_count;
#endif
}

void os_file_write(OsFileDescriptor* file_descriptor, ByteSlice buffer)
{
    u64 total_written_byte_count = 0;

    while (total_written_byte_count < buffer.length)
    {
        u64 written_byte_count = os_file_write_partially(file_descriptor, buffer.pointer + total_written_byte_count, buffer.length - total_written_byte_count);
        total_written_byte_count += written_byte_count;
    }
}

BUSTER_GLOBAL_LOCAL u64 os_file_read_partially(OsFileDescriptor* file_descriptor, void* buffer, u64 byte_count)
{
    u64 result = 0;
    bool success = true;
#if defined(__linux__) || defined(__APPLE__)
    int fd = generic_fd_to_posix(file_descriptor);
    ssize_t read_byte_count = read(fd, buffer, byte_count);
    success = read_byte_count >= 0;
    if (success)
    {
        result = (u64)read_byte_count;
    }
#elif defined(_WIN32)
    HANDLE fd = generic_fd_to_windows(file_descriptor);
    DWORD read_byte_count = 0;
    success = ReadFile(fd, buffer, (u32)byte_count, &read_byte_count, 0) != 0;
    if (success)
    {
        result = read_byte_count;
    }
#endif
    if (!success)
    {
        string_print(S8("Error reading file: {EOs}\n"), os_get_last_error());
    }

    return result;
}

u64 os_file_read(OsFileDescriptor* file_descriptor, ByteSlice buffer, u64 byte_count)
{
    u64 read_byte_count = 0;
    u8* pointer = buffer.pointer;
    BUSTER_CHECK(buffer.length >= byte_count);
    while (byte_count - read_byte_count)
    {
        u64 iteration_read_byte_count = os_file_read_partially(file_descriptor, pointer + read_byte_count, byte_count - read_byte_count);
        if (iteration_read_byte_count == 0)
        {
            break;
        }
        read_byte_count += iteration_read_byte_count;
    }

    return read_byte_count;
}

FileStats os_file_get_stats(OsFileDescriptor* file_descriptor, FileStatsOptions options)
{
    FileStats result = {0};

    if (((u64)file_descriptor != 0) & (options.raw != 0))
    {
#if defined(__linux__) || defined(__APPLE__)
        int fd = generic_fd_to_posix(file_descriptor);
        struct stat sb;
        int fstat_result = fstat(fd, &sb);
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
        BY_HANDLE_FILE_INFORMATION file_information = {0};
        BOOL file_result = GetFileInformationByHandle(fd, &file_information);
        BUSTER_CHECK(file_result != 0);
        w32_file_stats_from_file_information(&result, options, file_information);
#endif
    }

    return result;
}

bool os_file_close(OsFileDescriptor* file_descriptor)
{
    bool result = false;
    if (file_descriptor)
    {
#if defined(__linux__) || defined(__APPLE__)
        int fd = generic_fd_to_posix(file_descriptor);
        int close_result = close(fd);
        result = close_result == 0;
#elif defined(_WIN32)
        HANDLE fd = generic_fd_to_windows(file_descriptor);
        BOOL close_result = CloseHandle(fd);
        result = close_result != 0;
#endif
    }

    return result;
}

u64 string8_code_point_count(String8 s, u8 code_point)
{
    u64 count = 0;
    char8* restrict p = s.pointer;
    for (u64 i = 0; i < s.length; i += 1)
    {
        count += p[i] == code_point;
    }

    return count;
}

ProcessSpawnResult os_process_spawn(StringOs first_argument, StringOsList argv, StringOsList envp, ProcessSpawnOptions options)
{
    ProcessSpawnResult result = {0};
    bool pipe_creation_results[(u64)STANDARD_STREAM_COUNT];
    bool pipe_result = true;
#if defined(_WIN32)
    bool any_capture = false;
    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream))
        {
            SECURITY_ATTRIBUTES security_attributes = { sizeof(security_attributes), 0, TRUE };
            BUSTER_CT_CHECK(sizeof(HANDLE) == sizeof(OsFileDescriptor*));
            BOOL pipe_creation_result = CreatePipe((PHANDLE)&result.pipes[stream][0], (PHANDLE)&result.pipes[stream][1], &security_attributes, 0) != 0;
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
        PROCESS_INFORMATION process_information = {0};
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
            result.handle = (OsProcessHandle*)process_information.hProcess;
        }
        else
        {
            string_print(S8("Error creating a process: {EOs}\n{SOsL}\n"), os_get_last_error(), argv);
        }
    }

    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream) && pipe_creation_results[stream])
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
    int file_actions_init = posix_spawn_file_actions_init(&file_actions);
    int attribute_init = posix_spawnattr_init(&attributes);

    int pipes[(u64)STANDARD_STREAM_COUNT][2];

    for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        pipes[stream][0] = -1;
        pipes[stream][1] = -1;
    }

    for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream))
        {
            int pipe_creation_result = pipe(pipes[stream]) == 0;
            pipe_creation_results[stream] = pipe_creation_result;
            if (pipe_creation_result)
            {
                if (posix_spawn_file_actions_addclose(&file_actions, pipes[stream][0]) != 0)
                {
                    pipe_result = false;
                }

                int fd = generic_fd_to_posix(os_get_standard_stream((StandardStream)stream));

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
        int spawn_result = posix_spawnp(&pid, first_argument.pointer, &file_actions, &attributes, (char**)argv, (char**)envp);

        if (spawn_result != 0)
        {
            pid = -1;
        }
    }

    for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
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

    if (program_flag_get(PROGRAM_FLAG_VERBOSE))
    {
        string_print(S8("{S8} [{u64}]: \"{SOsL}\" \n"), result.handle ? S8("Launched") : S8("Failed to launch"), result.handle, argv);
    }

    return result;
}

ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn)
{
    ProcessWaitResult result = {0};
    result.result = PROCESS_RESULT_UNKNOWN;

    if (spawn.handle)
    {
#if defined(_WIN32)
        for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            HANDLE read_pipe = (HANDLE)spawn.pipes[stream][0];
            if (read_pipe)
            {
                u64 start_position = arena->position;

                u8 buffer[16 * 1024];
                u64 total_read_byte_count = 0;

                DWORD iteration_read_byte_count;
                bool iteration_read_result;
                while ((iteration_read_result = ReadFile(read_pipe, buffer, sizeof(buffer), &iteration_read_byte_count, 0) != 0) && iteration_read_byte_count > 0)
                {
                    u8* iteration_buffer = arena_allocate(arena, u8, iteration_read_byte_count);
                    memcpy(iteration_buffer, buffer, iteration_read_byte_count);

                    total_read_byte_count += iteration_read_byte_count;
                }

                if (!iteration_read_result)
                {
                    OsError os_error = os_get_last_error();
                    if (os_error.v != ERROR_BROKEN_PIPE)
                    {
                        string_print(S8("Failed to read from process pipe: {EOs}\n"), os_error);
                    }
                }

                CloseHandle(read_pipe);

                u64 length = arena->position - start_position;
                BUSTER_CHECK(total_read_byte_count == length);

                result.streams[stream] = (ByteSlice) { (u8*)arena + start_position, length };
            }
        }

        DWORD wait_result = WaitForSingleObject(spawn.handle, INFINITE);
        if (wait_result == WAIT_OBJECT_0)
        {
            DWORD exit_code;
            if (GetExitCodeProcess(spawn.handle, &exit_code))
            {
                result.result = (ProcessResult)exit_code;
            }
        }
#else
        pid_t pid = (pid_t)(u64)spawn.handle;

        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            OsFileDescriptor* generic_read_pipe = spawn.pipes[stream][0];
            if (generic_read_pipe)
            {
                int read_pipe = generic_fd_to_posix(generic_read_pipe);
                u64 start_position = arena->position;

                u8 buffer[16 * 1024];
                u64 total_read_byte_count = 0;

                ssize_t iteration_read_result;
                while ((iteration_read_result = read(read_pipe, buffer, sizeof(buffer))) > 0)
                {
                    u64 iteration_read_byte_count = (u64)iteration_read_result;
                    u8* iteration_buffer = arena_allocate(arena, u8, iteration_read_byte_count);
                    memcpy(iteration_buffer, buffer, iteration_read_byte_count);

                    total_read_byte_count += iteration_read_byte_count;
                }
                
                if (iteration_read_result < 0)
                {
                    string_print(S8("Failed to read from process pipe: {OsE}\n"), os_get_last_error());
                }

                close(read_pipe);

                u64 length = arena->position - start_position;
                BUSTER_CHECK(total_read_byte_count == length);

                result.streams[stream] = (ByteSlice) { (u8*)arena + start_position, length };
            }
        }

        int status;
        int options = 0;
        struct rusage usage;
        pid_t wait_result = wait4(pid, &status, options, &usage);

        if (program_flag_get(PROGRAM_FLAG_VERBOSE))
        {
            string_print(S8("Process [{s32}]: Time (user): {s64}:{s64} s,us, (system): {s64}:{s64} s,us. Max RSS: {s64} KB. PF (soft): {s64}, (hard): {s64}. Block (input): {s64}, (output): {s64}. CTX SW (vol): {s64}, (invol): {s64}\n"), pid, usage.ru_utime.tv_sec, usage.ru_utime.tv_usec, usage.ru_stime.tv_sec, usage.ru_stime.tv_usec, usage.ru_maxrss, usage.ru_minflt, usage.ru_majflt, usage.ru_inblock, usage.ru_oublock, usage.ru_nvcsw, usage.ru_nivcsw);
        }

        // Normal exit
        if ((wait_result == pid) & WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            result.result = (ProcessResult)exit_code;
        }
        else
        {
            // TODO
            wait_result = (int)PROCESS_RESULT_FAILED;
        }
#endif
    }

    return result;
}

OsError os_get_last_error(void)
{
    OsError result = {0};
#if defined(_WIN32)
    result.v = GetLastError();
#else
    int error = errno;
    BUSTER_CT_CHECK(sizeof(result.v) == sizeof(error));
    result.v = (__typeof__(result.v))error;
#endif
    return result;
}

StringOs os_error_write_message(StringOs string, OsError error)
{
    BUSTER_CHECK(string.length == BUSTER_OS_ERROR_BUFFER_MAX_LENGTH);
    StringOs result = {0};
#if defined(_WIN32)
    DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, (DWORD)error.v, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), string.pointer, string.length > ~(DWORD)0 ? ~(DWORD)0 : (DWORD)string.length, 0);
    if (length != 0)
    {
        result = string_os_from_pointer_length(string.pointer, length - 1);
    }
#else
    const char* error_raw_string = strerror((int)error.v);
    String8 error_string = string_from_pointer(error_raw_string);
    result = string;
    memcpy(result.pointer, error_string.pointer, BUSTER_SLICE_SIZE(error_string));
    result.length = error_string.length;
#endif
    return result;
}

StringOs os_get_environment_variable(StringOs variable)
{
    StringOs result = {0};
#if defined(_WIN32)
    CharOs* envp = GetEnvironmentStringsW();
    CharOs* it = envp;
    while (*it)
    {
        u64 length = string16_length(it);
        StringOs full_env = string_os_from_pointer_length(it, length);
        it += length + 1;
        u64 key_index = string16_first_code_unit(full_env, '=');
        StringOs key = string_os_from_pointer_length(full_env.pointer, key_index);
        if (string16_equal(key, variable))
        {
            result = string_os_from_pointer_length(full_env.pointer + (key_index + 1), full_env.length - (key_index + 1));
            break;
        }
    }
#else
    const char8* pointer = getenv(variable.pointer);
    result = string_from_pointer(pointer);
#endif
    return result;
}

u64 os_file_get_size(OsFileDescriptor* file_descriptor)
{
#if defined(__linux__) || defined(__APPLE__)
    int fd = generic_fd_to_posix(file_descriptor);
    struct stat sb;
    int fstat_result = fstat(fd, &sb);
    BUSTER_CHECK(fstat_result == 0);

    return (u64)sb.st_size;
#elif defined(_WIN32)
    HANDLE fd = generic_fd_to_windows(file_descriptor);
    BY_HANDLE_FILE_INFORMATION file_information = {0};
    BOOL result = GetFileInformationByHandle(fd, &file_information);
    BUSTER_CHECK(result);
    return w32_file_size_from_file_information(file_information);
#endif
}

bool os_is_tty(OsFileDescriptor* file)
{
    bool result = false;
#if defined(_WIN32)
    DWORD mode;
    HANDLE handle = (HANDLE)file;
    result = GetConsoleMode(handle, &mode) != 0;
#else
    int fd = generic_fd_to_posix(file);
    result = isatty(fd);
#endif
    return result;
}

bool os_unreserve(void* address, u64 size)
{
    bool result = 1;
#if defined(__linux__) || defined(__APPLE__)
    int unmap_result = munmap(address, size);
    result = unmap_result == 0;
#elif defined(_WIN32)
    BOOL virtual_free_result = VirtualFree(address, size, MEM_DECOMMIT);
    result = virtual_free_result != 0;
    if (result)
    {
        virtual_free_result = VirtualFree(address, 0, MEM_RELEASE);
        result = virtual_free_result != 0;
    }
#endif
    return result;
}

OsModuleHandle* os_dynamic_library_load(StringOs library)
{
    OsModuleHandle* result = {0};
    BUSTER_CHECK(BUSTER_SLICE_IS_ZERO_TERMINATED(library));

#if defined(_WIN32)
    result = (OsModuleHandle*)LoadLibraryW(library.pointer);
#else
    result = (OsModuleHandle*) dlopen(library.pointer, RTLD_NOW | RTLD_LOCAL);
#endif

    return result;
}

void os_dynamic_library_unload(OsModuleHandle* module)
{
    if (module)
    {
#if defined(_WIN32)
        FreeLibrary((HMODULE)module);
#else
        dlclose(module);
#endif
    }
}

OsSymbol* os_dynamic_library_function_load(OsModuleHandle* module, String8 symbol)
{
    OsSymbol* result = {0};

#if defined(_WIN32)
    result = (OsSymbol*)GetProcAddress((HMODULE)module, symbol.pointer);
#else
    result = (OsSymbol*)dlsym((void*)module, symbol.pointer);
#endif

    return result;
}

u32 os_get_logical_thread_count(void)
{
    u32 result;

#if defined(__linux__)
    result = (u32)get_nprocs();
#elif defined(__APPLE__)
    int os_result = 1;
    size_t size = sizeof(result);

    if (sysctlbyname("hw.activecpu", &os_result, &size, 0, 0) != 0 || os_result < 1)
    {
        os_result = 1;
    }

    result = (u32)os_result;
#elif defined(_WIN32)
    SYSTEM_INFO sysinfo = {0};
    GetSystemInfo(&sysinfo);
    result = sysinfo.dwNumberOfProcessors;
#endif

    return result;
}

u64 os_get_page_size(void)
{
    u64 page_size;
#if defined(__linux__) || defined(__APPLE__)
    page_size = (u64)getpagesize();
#else
    SYSTEM_INFO sysinfo = {0};
    GetSystemInfo(&sysinfo);
    page_size = sysinfo.dwPageSize;
#endif
    return page_size;
}

OsProcessHandle* os_get_current_process_handle(void)
{
    OsProcessHandle* result;
#if defined(__linux__) || defined(__APPLE__)
     result = (OsProcessHandle*)(u64)getpid();
#else
     result = (OsProcessHandle*)GetCurrentProcess();
#endif
     return result;
}

OsThreadHandle* os_get_current_thread_handle(void)
{
    OsThreadHandle* result;
#if defined(__linux__) || defined(__APPLE__)
    result = (OsThreadHandle*)(u64)pthread_self();
#else
    result = (OsThreadHandle*)GetCurrentThread();
#endif
    return result;
}

void os_thread_set_name(String8 thread_name)
{
#if defined(__linux__)
    pthread_setname_np(pthread_self(), thread_name.pointer);
#elif defined(__APPLE__)
    pthread_setname_np(thread_name.pointer);
#elif defined(_WIN32)
#ifndef __TINYC__
    TemporalArena scratch = scratch_begin(0, 0);
    String16 string = string16_from_string8(scratch.arena, thread_name, true);
    SetThreadDescription(GetCurrentThread(), string.pointer);
    scratch_end(scratch);
#endif
#else
#pragma error
#endif
}

ThreadContext* thread_context_selected(void)
{
#if BUSTER_THREAD_CONTEXT_USE_PTHREAD_TLS
    thread_context_tls_key_ensure_initialized();
    return (ThreadContext*)pthread_getspecific(thread_context_tls_key);
#else
    return thread_context_thread_local;
#endif
}

ThreadContext* thread_context_allocate(void)
{
    Arena* arenas[(u64)SCRATCH_ARENA_COUNT];
    for (u64 i = 0; i < SCRATCH_ARENA_COUNT; i += 1)
    {
        arenas[i] = arena_create((ArenaCreation){0});
    }

    ThreadContext* result = arena_allocate(arenas[0], ThreadContext, 1);
    memset(result, 0, sizeof(*result));
    memcpy(result->arenas, arenas, sizeof(arenas));
    result->lane_context.lane_count = 1;
    return result;
}

void thread_context_release(ThreadContext* thread_context)
{
    u64 i = BUSTER_ARRAY_LENGTH(thread_context->arenas);
    while (i--)
    {
        arena_destroy(thread_context->arenas[i], 1);
    }
}

void thread_context_select(ThreadContext* context)
{
#if BUSTER_THREAD_CONTEXT_USE_PTHREAD_TLS
    thread_context_tls_key_ensure_initialized();
    int result = pthread_setspecific(thread_context_tls_key, context);
    if (result != 0)
    {
        BUSTER_TRAP();
    }
#else
    thread_context_thread_local = context;
#endif
}

Arena* thread_context_get_scratch(Arena** conflicts, u64 count)
{
    ThreadContext* thread_context = thread_context_selected();
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

u64 os_now_microseconds(void)
{
#if defined(_WIN32)
    u64 result = 0;
    LARGE_INTEGER os_counter;
    if(QueryPerformanceCounter(&os_counter))
    {
        result = (u64)(os_counter.QuadPart * 1000 * 1000) / os_state.frequency;
    }
    return result;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  u64 result = (u64)(ts.tv_sec * (1000 * 1000) + (ts.tv_nsec / 1000));
  return result;
#endif
}

void flag_set_ex(u64* flag_pointer, u64 flag_count, u64 flag_index, bool flag_value)
{
    BUSTER_CHECK(flag_index < flag_count);
    u64 divisor = sizeof(*flag_pointer);
    u64 element_index = flag_index / divisor;
    u64 bit_index = flag_index % divisor;
    flag_pointer[element_index] = (flag_pointer[element_index] & ~((u64)1 << bit_index)) | ((u64)flag_value << bit_index);
}

bool flag_get_ex(u64* flag_pointer, u64 flag_count, u64 flag_index)
{
    BUSTER_CHECK(flag_index < flag_count);
    u64 divisor = sizeof(*flag_pointer);
    u64 element_index = flag_index / divisor;
    u64 bit_index = flag_index % divisor;
    return (flag_pointer[element_index] & ((u64)1 << bit_index)) != 0;
}

BooleanArgumentProcessResult boolean_argument_process(String8* flag_string_start_pointer, u64 flag_string_start_count, u64* flag_pointer, u64 flag_count, String8 argument)
{
    BUSTER_CHECK(flag_string_start_count == flag_count);

    BooleanArgumentProcessResult result = {0};
    
    for (result.index = 0; result.index < flag_string_start_count; result.index += 1)
    {
        String8 flag_start = flag_string_start_pointer[result.index];
        if (string_starts_with_sequence(argument, flag_start))
        {
            if (argument.length == flag_start.length + 1)
            {
                CharOs v = argument.pointer[flag_start.length];
                switch (v)
                {
                    break; case '0': case '1':
                    {
                        bool flag_value = v == '1';
                        flag_set_ex(flag_pointer, flag_count, result.index, flag_value);
                        result.valid = true;
                    }
                    break; default: {}
                }
            }

            break;
        }
    }

    return result;
}

bool program_flag_get(ProgramFlag flag)
{
    return flag_get(program_state->input.flags, PROGRAM_FLAG_COUNT, flag);
}
