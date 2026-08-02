#include <buster/lib/os.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>

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
        os_fail();
    }
}

BUSTER_GLOBAL_LOCAL void thread_context_tls_key_ensure_initialized(void)
{
    int result = pthread_once(&thread_context_tls_key_once, thread_context_tls_key_initialize);
    if (result != 0)
    {
        os_fail();
    }
}
#else
BUSTER_THREAD_LOCAL_DECL ThreadContext* thread_context_thread_local;
#endif

#if defined(_MSC_VER)
BUSTER_V_IMPL RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions = {0};
#endif

//- rjf: doubly-linked-lists
#define DLLInsert_NPZ(nil, f, l, p, n, next, prev)                                                                                                             \
    (CheckNil(nil, f)   ? ((f) = (l) = (n), SetNil(nil, (n)->next), SetNil(nil, (n)->prev))                                                                    \
     : CheckNil(nil, p) ? ((n)->next = (f), (f)->prev = (n), (f) = (n), SetNil(nil, (n)->prev))                                                                \
     : ((p) == (l))                                                                                                                                            \
         ? ((l)->next = (n), (n)->prev = (l), (l) = (n), SetNil(nil, (n)->next))                                                                               \
         : (((!CheckNil(nil, p) && CheckNil(nil, (p)->next)) ? (0) : ((p)->next->prev = (n))), ((n)->next = (p)->next), ((p)->next = (n)), ((n)->prev = (p))))
#define DLLPushBack_NPZ(nil, f, l, n, next, prev) DLLInsert_NPZ(nil, f, l, l, n, next, prev)
#define DLLPushFront_NPZ(nil, f, l, n, next, prev) DLLInsert_NPZ(nil, l, f, f, n, prev, next)
#define DLLRemove_NPZ(nil, f, l, n, next, prev)                                                                                                                \
    (((n) == (f) ? (f) = (n)->next : (0)), ((n) == (l) ? (l) = (l)->prev : (0)), (CheckNil(nil, (n)->prev) ? (0) : ((n)->prev->next = (n)->next)),             \
     (CheckNil(nil, (n)->next) ? (0) : ((n)->next->prev = (n)->prev)))

//- rjf: singly-linked, doubly-headed lists (queues)
#define SLLQueuePush_NZ(nil, f, l, n, next)                                                                                                                    \
    (CheckNil(nil, f) ? ((f) = (l) = (n), SetNil(nil, (n)->next)) : ((l)->next = (n), (l) = (n), SetNil(nil, (n)->next)))
#define SLLQueuePushFront_NZ(nil, f, l, n, next) (CheckNil(nil, f) ? ((f) = (l) = (n), SetNil(nil, (n)->next)) : ((n)->next = (f), (f) = (n)))
#define SLLQueuePop_NZ(nil, f, l, next) ((f) == (l) ? (SetNil(nil, f), SetNil(nil, l)) : ((f) = (f)->next))

//- rjf: singly-linked, singly-headed lists (stacks)
#define SLLStackPush_N(f, n, next) ((n)->next = (f), (f) = (n))
#define SLLStackPop_N(f, next) ((f) = (f)->next)

//- rjf: doubly-linked-list helpers
#define DLLInsert_NP(f, l, p, n, next, prev) DLLInsert_NPZ(0, f, l, p, n, next, prev)
#define DLLPushBack_NP(f, l, n, next, prev) DLLPushBack_NPZ(0, f, l, n, next, prev)
#define DLLPushFront_NP(f, l, n, next, prev) DLLPushFront_NPZ(0, f, l, n, next, prev)
#define DLLRemove_NP(f, l, n, next, prev) DLLRemove_NPZ(0, f, l, n, next, prev)
#define DLLInsert(f, l, p, n) DLLInsert_NPZ(0, f, l, p, n, next, prev)
#define DLLPushBack(f, l, n) DLLPushBack_NPZ(0, f, l, n, next, prev)
#define DLLPushFront(f, l, n) DLLPushFront_NPZ(0, f, l, n, next, prev)
#define DLLRemove(f, l, n) DLLRemove_NPZ(0, f, l, n, next, prev)

//- rjf: singly-linked, doubly-headed list helpers
#define SLLQueuePush_N(f, l, n, next) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePushFront_N(f, l, n, next) SLLQueuePushFront_NZ(0, f, l, n, next)
#define SLLQueuePop_N(f, l, next) SLLQueuePop_NZ(0, f, l, next)
#define SLLQueuePush(f, l, n) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePushFront(f, l, n) SLLQueuePushFront_NZ(0, f, l, n, next)
#define SLLQueuePop(f, l) SLLQueuePop_NZ(0, f, l, next)

//- rjf: singly-linked, singly-headed list helpers
#define SLLStackPush(f, n) SLLStackPush_N(f, n, next)
#define SLLStackPop(f) SLLStackPop_N(f, next)

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
        // Parse TracerPid out of /proc/self/status. The previous
        // PTRACE_TRACEME probe left the process permanently traced by its
        // parent and blocked a real debugger from attaching later.
        bool traced = false;
        int status_fd = open("/proc/self/status", O_RDONLY);
        if (status_fd >= 0)
        {
            char8 status_buffer[4096];
            ssize_t read_byte_count = read(status_fd, status_buffer, sizeof(status_buffer));
            close(status_fd);
            if (read_byte_count > 0)
            {
                String8 contents = {.pointer = status_buffer, .length = (u64)read_byte_count};
                String8 key = S8("TracerPid:");
                u64 key_index = string_first_sequence(contents, key);
                if (key_index != BUSTER_STRING_NO_MATCH)
                {
                    u64 value_index = key_index + key.length;
                    while (value_index < contents.length && (contents.pointer[value_index] == ' ' || contents.pointer[value_index] == '\t'))
                    {
                        value_index += 1;
                    }
                    traced = value_index < contents.length && contents.pointer[value_index] != '0';
                }
            }
        }
        program_state->_is_debugger_present = traced;
#elif defined(__APPLE__)
#elif defined(_WIN32)
        BOOL os_result = IsDebuggerPresent();
        program_state->_is_debugger_present = os_result != 0;
#else
        BUSTER_TODO();
#endif
    }

    return (bool)program_state->_is_debugger_present;
}

BUSTER_GLOBAL_LOCAL void os_error_print(String8 format, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, format);
    string_write_to_file_va(os_get_standard_stream(STANDARD_STREAM_ERROR), format, variable_arguments);
    va_end(variable_arguments);
}

BUSTER_NORETURN BUSTER_COLD void os_fail_va(u32 line, String8 function, String8 file, String8 context, ...)
{
    va_list variable_arguments;
    va_start(variable_arguments, context);
    string_write_to_file_va(os_get_standard_stream(STANDARD_STREAM_ERROR), context, variable_arguments);
    va_end(variable_arguments);
    os_error_print(S8(" at {S8}:{u32} in {S8}\n"), file, line, function);
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
#error os_exit requires libc on this platform
#endif
#endif
}

#if defined(__linux__) || defined(__APPLE__)
BUSTER_GLOBAL_LOCAL int os_posix_protection_flags(ProtectionFlags flags)
{
    int result = PROT_READ * flags.read | PROT_WRITE * flags.write | PROT_EXEC * flags.execute;

    return result;
}

BUSTER_GLOBAL_LOCAL int os_posix_map_flags(MapFlags flags)
{
    int result =
#ifdef __linux__
        MAP_POPULATE * flags.populate |
#endif
        MAP_PRIVATE * flags.priv | MAP_ANON * flags.anonymous | MAP_NORESERVE * flags.no_reserve;

    return result;
}

// File descriptors are stored biased by +1 so that fd 0 (stdin) does not
// collide with the null pointer, which means "no descriptor" everywhere.
BUSTER_GLOBAL_LOCAL OsFileDescriptor* posix_fd_to_generic_fd(int fd)
{
    BUSTER_CHECK(fd >= 0);
    return (OsFileDescriptor*)((u64)fd + 1);
}

BUSTER_TEST_F_DECL int generic_fd_to_posix(OsFileDescriptor* fd)
{
    BUSTER_CHECK(fd);
    return (int)((u64)fd - 1);
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

BUSTER_GLOBAL_LOCAL bool os_fault(void* address, u64 size)
{
    bool result = 1;

#if defined(__linux__)
    int os_result = madvise(address, size, MADV_POPULATE_WRITE);
    result = os_result == 0;
#elif defined(__APPLE__)
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
        os_fault(address, size);
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

bool os_flush_instruction_cache(void* address, u64 size)
{
#if defined(_WIN32)
    return FlushInstructionCache(GetCurrentProcess(), address, (SIZE_T)size) != 0;
#elif BUSTER_COMPILER_TCC
    BUSTER_UNUSED(address);
    BUSTER_UNUSED(size);
    return true;
#elif defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache((char*)address, (char*)address + size);
    return true;
#else
    BUSTER_UNUSED(address);
    BUSTER_UNUSED(size);
    return true;
#endif
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
#elif defined(_WIN32)
BUSTER_GLOBAL_LOCAL DWORD WINAPI windows_thread_entry_point(LPVOID argument)
{
    OsEntity* entity = (OsEntity*)argument;
    thread_entry_point(entity->thread.callback, entity->thread.argument);
    return 0;
}
#endif

OsThreadHandle* os_thread_create(ThreadCreateOptions options)
{
    BUSTER_UNUSED(options);
    OsEntity* result = os_entity_allocate(OS_ENTITY_KIND_THREAD);
    result->thread.callback = options.callback;
    result->thread.argument = options.argument;
#if defined(__linux__) || defined(__APPLE__)
    int create_result = pthread_create(&result->thread.handle, 0, &pthread_entry_point, result);
    bool os_result = create_result == 0;
    if (!os_result)
    {
        os_entity_release(result);
        result = 0;
    }
#elif defined(_WIN32)
    HANDLE handle = CreateThread(0, 0, &windows_thread_entry_point, result, 0, 0);
    if (handle)
    {
        result->thread.handle = handle;
    }
    else
    {
        os_entity_release(result);
        result = 0;
    }
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
    DWORD wait_result = WaitForSingleObject(entity->thread.handle, INFINITE);
    result = wait_result == WAIT_OBJECT_0;
    CloseHandle(entity->thread.handle);
#endif
    os_entity_release(entity);
    return result;
}

String8 os_path_absolute(Arena* arena, String8 relative_file_path, bool null_terminate)
{
    String8 result = {0};
#if defined(__linux__) || defined(__APPLE__)
    u64 position = arena->position;
    u64 length = PATH_MAX;
    char8* buffer = arena_allocate(arena, char8, length + null_terminate);
    char* syscall_result = realpath((char*)relative_file_path.pointer, buffer);

    if (syscall_result)
    {
        result = string_from_pointer(syscall_result);
        BUSTER_CHECK(result.length <= length);
    }

    arena->position = position + result.length + null_terminate;
#elif defined(_WIN32)
    TemporalArena temp = scratch_begin(0, 0);
    String16 relative_file_path_w = string16_from_string8(temp.arena, relative_file_path, true);
    DWORD length_plus_null_termination = GetFullPathNameW(relative_file_path_w.pointer, 0, 0, 0);

    if (length_plus_null_termination != 0)
    {
        DWORD buffer_length = length_plus_null_termination + 1;
        for (;;)
        {
            WindowsChar* os_result = arena_allocate(temp.arena, WindowsChar, buffer_length);
            DWORD new_length = GetFullPathNameW(relative_file_path_w.pointer, buffer_length, os_result, 0);
            if (new_length == 0)
            {
                break;
            }
            if (new_length < buffer_length)
            {
                String16 string16 = {.pointer = os_result, .length = new_length};
                result = string8_from_string16(arena, string16, null_terminate);
                break;
            }
            buffer_length = new_length + 1;
        }
    }

    scratch_end(temp);
#endif
    return result;
}

void os_make_directory(String8 path)
{
#if defined(__linux__) || defined(__APPLE__)
    mkdir((const char*)path.pointer, 0755);
#elif defined(_WIN32)
    TemporalArena temp = scratch_begin(0, 0);
    String16 path_w = string16_from_string8(temp.arena, path, true);
    CreateDirectoryW(path_w.pointer, 0);
    scratch_end(temp);
#endif
}

OsFileDescriptor* os_file_open(String8 path, OpenFlags flags, OpenPermissions permissions)
{
    OsFileDescriptor* result = 0;
    if (path.pointer)
    {
#if defined(__linux__) || defined(__APPLE__)
        BUSTER_CHECK(!path.pointer[path.length]);

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
            result = posix_fd_to_generic_fd(fd);
        }
        else if (program_flag_get(PROGRAM_FLAG_VERBOSE))
        {
            OsError error = os_get_last_error();
            // Missing paths are expected while probing include and library
            // candidates. Keep diagnostics for other failures visible.
            if (error.v != (u32)ENOENT && error.v != (u32)ENOTDIR)
            {
                string_print(S8("Error opening {S8}: {EOs}\n"), path, error);
            }
        }
#elif defined(_WIN32)
        TemporalArena scratch = scratch_begin(0, 0);

        DWORD desired_access = 0;
        DWORD shared_mode = 0;
        SECURITY_ATTRIBUTES security_attributes = {sizeof(security_attributes), 0, 0};
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

        // The creation disposition must come from the open flags, not the share
        // mode: mapping "writable" to CREATE_ALWAYS truncated existing files on
        // every open with write permission.
        if (flags.create && flags.truncate)
        {
            creation_disposition = CREATE_ALWAYS;
        }
        else if (flags.create)
        {
            creation_disposition = OPEN_ALWAYS;
        }
        else if (flags.truncate)
        {
            creation_disposition = TRUNCATE_EXISTING;
        }
        else
        {
            creation_disposition = OPEN_EXISTING;
        }

        String16 path_w = string16_from_string8(scratch.arena, path, true);
        HANDLE fd = CreateFileW(path_w.pointer, desired_access, shared_mode, &security_attributes, creation_disposition, flags_and_attributes, template_file);
        if (fd != INVALID_HANDLE_VALUE)
        {
            result = (OsFileDescriptor*)fd;
        }
        else if (program_flag_get(PROGRAM_FLAG_VERBOSE))
        {
            OsError error = os_get_last_error();
            // Missing paths are expected while probing include and library
            // candidates. Keep diagnostics for other failures visible.
            if (error.v != (u32)ERROR_FILE_NOT_FOUND && error.v != (u32)ERROR_PATH_NOT_FOUND)
            {
                string_print(S8("Error opening {S8}: {EOs}\n"), path, error);
            }
        }
        scratch_end(scratch);
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 os_file_write_partially(OsFileDescriptor* file_descriptor, void* pointer, u64 length)
{
#if defined(__linux__) || defined(__APPLE__)
    int fd = generic_fd_to_posix(file_descriptor);
    ssize_t result;
    do
    {
        result = write(fd, pointer, length);
    } while (result < 0 && errno == EINTR);
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

ProcessSpawnResult os_process_spawn(SliceString8 arguments, SliceString8 environment_keys, SliceString8 environment_values, ProcessSpawnOptions options)
{
    TemporalArena temp = scratch_begin(0, 0);
    ProcessSpawnResult result = {0};
    bool pipe_creation_results[(u64)STANDARD_STREAM_COUNT] = {0};
    bool pipe_result = true;
#if defined(_WIN32)
    bool any_capture = false;
    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream))
        {
            SECURITY_ATTRIBUTES security_attributes = {sizeof(security_attributes), 0, TRUE};
            BUSTER_CT_CHECK(sizeof(HANDLE) == sizeof(OsFileDescriptor*));
            BOOL pipe_creation_result = CreatePipe((PHANDLE)&result.pipes[stream][0], (PHANDLE)&result.pipes[stream][1], &security_attributes, 0) != 0;
            pipe_creation_results[stream] = pipe_creation_result;

            if (pipe_creation_result)
            {
                any_capture = true;
                // The child must inherit the read end for stdin and the write
                // end for stdout/stderr; only the parent's end may be made
                // non-inheritable.
                u32 parent_side = stream == STANDARD_STREAM_INPUT ? 1 : 0;
                // TODO: handle error for this
                SetHandleInformation(result.pipes[stream][parent_side], HANDLE_FLAG_INHERIT, 0);
            }
            else
            {
                pipe_result = false;
            }
        }
    }

    if (pipe_result)
    {
        PROCESS_INFORMATION process_information = {0};
        STARTUPINFOW startup_info = {sizeof(startup_info)};

        if (any_capture)
        {
            startup_info.dwFlags |= STARTF_USESTDHANDLES;
            startup_info.hStdInput = options.capture & (1 << STANDARD_STREAM_INPUT) ? result.pipes[STANDARD_STREAM_INPUT][0] : GetStdHandle(STD_INPUT_HANDLE);
            startup_info.hStdOutput =
                options.capture & (1 << STANDARD_STREAM_OUTPUT) ? result.pipes[STANDARD_STREAM_OUTPUT][1] : GetStdHandle(STD_OUTPUT_HANDLE);
            startup_info.hStdError = options.capture & (1 << STANDARD_STREAM_ERROR) ? result.pipes[STANDARD_STREAM_ERROR][1] : GetStdHandle(STD_ERROR_HANDLE);
        }

        String16 first_argument = {0};
        if (arguments.length > 0)
        {
            first_argument = string16_from_string8(temp.arena, arguments.pointer[0], true);
        }

        WindowsStringList argv = windows_string_list_from_slice_string(temp.arena, arguments);
        WindowsStringList envp = options.use_process_environment ? program_state->input.raw_environment
                                                                 : windows_environment_from_keys_and_values(temp.arena, environment_keys, environment_values);
        DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT;

        if (CreateProcessW(first_argument.pointer, argv, 0, 0, 1, creation_flags, envp, 0, &startup_info, &process_information))
        {
            result.handle = (OsProcessHandle*)process_information.hProcess;
            CloseHandle(process_information.hThread);
        }
        else
        {
            string_print(S8("Error creating a process: \"{EOs}\" => {[]S8}\n"), os_get_last_error(), arguments);
        }
    }

    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream) && pipe_creation_results[stream])
        {
            // The child inherits the write end for stdout/stderr capture, or the read end for
            // stdin capture; the parent only ever needs to keep the other end for itself.
            u32 child_side = stream == STANDARD_STREAM_INPUT ? 0 : 1;
            u32 parent_side = stream == STANDARD_STREAM_INPUT ? 1 : 0;

            CloseHandle(result.pipes[stream][child_side]);
            result.pipes[stream][child_side] = 0;

            if (!result.handle)
            {
                CloseHandle(result.pipes[stream][parent_side]);
                result.pipes[stream][parent_side] = 0;
            }
        }
    }
#elif BUSTER_ANDROID
    BUSTER_UNUSED(environment_keys);
    BUSTER_UNUSED(environment_values);
    BUSTER_UNUSED(options);
    BUSTER_UNUSED(pipe_creation_results);
    BUSTER_UNUSED(pipe_result);
    string_print(S8("Process spawning is not supported on Android: {[]S8}\n"), arguments);
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
                // For stdin, the child reads from the pipe, so it gets the read end dup2'd onto
                // its fd and the write end closed; for stdout/stderr it's the other way around.
                bool is_input = (StandardStream)stream == STANDARD_STREAM_INPUT;
                int child_end = is_input ? pipes[stream][0] : pipes[stream][1];
                int other_end = is_input ? pipes[stream][1] : pipes[stream][0];

                if (posix_spawn_file_actions_addclose(&file_actions, other_end) != 0)
                {
                    pipe_result = false;
                }

                int fd = generic_fd_to_posix(os_get_standard_stream((StandardStream)stream));

                if (posix_spawn_file_actions_adddup2(&file_actions, child_end, fd) != 0)
                {
                    pipe_result = false;
                }

                if (posix_spawn_file_actions_addclose(&file_actions, child_end) != 0)
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
        PosixStringList argv = slice_string8_to_null_terminated_array_char(temp.arena, arguments);
        PosixStringList envp = options.use_process_environment ? program_state->input.raw_environment
                                                               : posix_environment_from_keys_and_values(temp.arena, environment_keys, environment_values);
        int spawn_result = posix_spawnp(&pid, argv[0], &file_actions, &attributes, argv, envp);

        if (spawn_result != 0)
        {
            pid = -1;
            // Report unconditionally, like the Windows branch: a spawn failure
            // that only surfaces under --verbose is undebuggable.
            errno = spawn_result;
            string_print(S8("Error creating a process: \"{EOs}\" => {[]S8}\n"), os_get_last_error(), arguments);
        }
    }

    for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream) && pipe_creation_results[stream])
        {
            bool is_input = (StandardStream)stream == STANDARD_STREAM_INPUT;
            int child_side = is_input ? 0 : 1;
            int parent_side = is_input ? 1 : 0;

            close(pipes[stream][child_side]);
            pipes[stream][child_side] = -1;

            if (pid == -1)
            {
                close(pipes[stream][parent_side]);
                pipes[stream][parent_side] = -1;
            }
        }

        for (int i = 0; i < 2; i += 1)
        {
            result.pipes[stream][i] = pipes[stream][i] >= 0 ? posix_fd_to_generic_fd(pipes[stream][i]) : 0;
        }
    }

    // Destroying an object whose _init failed is undefined behavior.
    if (file_actions_init == 0)
    {
        posix_spawn_file_actions_destroy(&file_actions);
    }
    if (attribute_init == 0)
    {
        posix_spawnattr_destroy(&attributes);
    }

    result.handle = (OsProcessHandle*)(pid == -1 ? 0 : (u64)pid);
#endif

    if (program_flag_get(PROGRAM_FLAG_VERBOSE))
    {
        string_print(S8("{S8} [{u64}]: \"{[]S8}\" \n"), result.handle ? S8("Launched") : S8("Failed to launch"), result.handle, arguments);
    }

    scratch_end(temp);

    return result;
}

// Captured pipe output is accumulated as a chunk list in scratch memory while
// draining, because the streams are read interleaved (see below) but each
// stream's bytes must end up contiguous in the caller's arena.
typedef struct PipeChunk PipeChunk;
struct PipeChunk
{
    PipeChunk* next;
    u64 length;
    u8* data;
};

typedef struct PipeCapture PipeCapture;
struct PipeCapture
{
    PipeChunk* first;
    PipeChunk* last;
    u64 total_length;
};

BUSTER_GLOBAL_LOCAL void pipe_capture_append(Arena* arena, PipeCapture* capture, u8* data, u64 length)
{
    PipeChunk* chunk = arena_allocate(arena, PipeChunk, 1);
    chunk->next = 0;
    chunk->length = length;
    chunk->data = arena_allocate(arena, u8, length);
    memcpy(chunk->data, data, length);

    if (capture->last)
    {
        capture->last->next = chunk;
    }
    else
    {
        capture->first = chunk;
    }
    capture->last = chunk;
    capture->total_length += length;
}

BUSTER_GLOBAL_LOCAL ByteSlice pipe_capture_flatten(Arena* arena, PipeCapture* capture)
{
    u8* pointer = arena_allocate(arena, u8, capture->total_length);
    u64 offset = 0;
    for (PipeChunk* chunk = capture->first; chunk; chunk = chunk->next)
    {
        memcpy(pointer + offset, chunk->data, chunk->length);
        offset += chunk->length;
    }
    BUSTER_CHECK(offset == capture->total_length);
    return (ByteSlice){pointer, capture->total_length};
}

ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn)
{
    ProcessWaitResult result = {0};
    result.result = PROCESS_RESULT_UNKNOWN;

    if (spawn.handle)
    {
        // The captured streams must be drained together: reading one pipe to
        // EOF while the child blocks writing a full second pipe deadlocks both
        // processes.
        TemporalArena scratch = scratch_begin(&arena, 1);
        PipeCapture captures[(u64)STANDARD_STREAM_COUNT] = {0};
        bool captured[(u64)STANDARD_STREAM_COUNT] = {0};
#if defined(_WIN32)
        // The parent never writes to a captured stdin; close the write end up
        // front so a child reading stdin sees EOF instead of blocking forever.
        if (spawn.pipes[STANDARD_STREAM_INPUT][1])
        {
            CloseHandle((HANDLE)spawn.pipes[STANDARD_STREAM_INPUT][1]);
            spawn.pipes[STANDARD_STREAM_INPUT][1] = 0;
        }

        HANDLE read_pipes[(u64)STANDARD_STREAM_COUNT];
        u64 open_pipe_count = 0;
        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            read_pipes[stream] = (HANDLE)spawn.pipes[stream][0];
            captured[stream] = read_pipes[stream] != 0;
            open_pipe_count += captured[stream];
        }

        while (open_pipe_count)
        {
            bool made_progress = false;

            for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
            {
                HANDLE read_pipe = read_pipes[stream];
                if (!read_pipe)
                {
                    continue;
                }

                // ReadFile on an anonymous pipe blocks, so only read bytes
                // PeekNamedPipe reports as available.
                DWORD available_byte_count = 0;
                if (!PeekNamedPipe(read_pipe, 0, 0, 0, &available_byte_count, 0))
                {
                    OsError os_error = os_get_last_error();
                    if (os_error.v != ERROR_BROKEN_PIPE)
                    {
                        string_print(S8("Failed to read from process pipe: {EOs}\n"), os_error);
                    }
                    CloseHandle(read_pipe);
                    read_pipes[stream] = 0;
                    open_pipe_count -= 1;
                    made_progress = true;
                }
                else if (available_byte_count)
                {
                    u8 buffer[16 * 1024];
                    DWORD read_byte_count = 0;
                    DWORD requested_byte_count = available_byte_count < sizeof(buffer) ? available_byte_count : (DWORD)sizeof(buffer);
                    if (ReadFile(read_pipe, buffer, requested_byte_count, &read_byte_count, 0) && read_byte_count)
                    {
                        pipe_capture_append(scratch.arena, &captures[stream], buffer, read_byte_count);
                        made_progress = true;
                    }
                }
            }

            if (!made_progress)
            {
                // Nothing readable right now: sleep briefly instead of
                // spinning. The pipes report ERROR_BROKEN_PIPE once the child
                // exits and the buffered data has been drained.
                WaitForSingleObject(spawn.handle, 10);
            }
        }

        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            if (captured[stream])
            {
                result.streams[stream] = pipe_capture_flatten(arena, &captures[stream]);
            }
        }

        DWORD wait_result = WaitForSingleObject(spawn.handle, INFINITE);
        if (wait_result == WAIT_OBJECT_0)
        {
            DWORD exit_code;
            if (GetExitCodeProcess(spawn.handle, &exit_code))
            {
                if (exit_code >= 0xC0000000u)
                {
                    // NTSTATUS failure codes (e.g. 0xC0000005, access
                    // violation) mean the child died abnormally.
                    result.result = PROCESS_RESULT_CRASH;
                }
                else
                {
                    // Exit codes past the enum range carry no meaning as
                    // ProcessResult values; collapse them to plain failure.
                    result.result = exit_code < (DWORD)PROCESS_RESULT_COUNT ? (ProcessResult)exit_code : PROCESS_RESULT_FAILED;
                }
            }
        }
        CloseHandle(spawn.handle);
#else
        pid_t pid = (pid_t)(u64)spawn.handle;

        // The parent never writes to a captured stdin; close the write end up
        // front so a child reading stdin sees EOF instead of blocking forever.
        if (spawn.pipes[STANDARD_STREAM_INPUT][1])
        {
            close(generic_fd_to_posix(spawn.pipes[STANDARD_STREAM_INPUT][1]));
            spawn.pipes[STANDARD_STREAM_INPUT][1] = 0;
        }

        int read_pipes[(u64)STANDARD_STREAM_COUNT];
        u64 open_pipe_count = 0;
        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            OsFileDescriptor* generic_read_pipe = spawn.pipes[stream][0];
            read_pipes[stream] = generic_read_pipe ? generic_fd_to_posix(generic_read_pipe) : -1;
            captured[stream] = read_pipes[stream] >= 0;
            open_pipe_count += captured[stream];
        }

        while (open_pipe_count)
        {
            struct pollfd poll_fds[(u64)STANDARD_STREAM_COUNT];
            u64 poll_streams[(u64)STANDARD_STREAM_COUNT];
            nfds_t poll_count = 0;

            for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
            {
                if (read_pipes[stream] >= 0)
                {
                    poll_fds[poll_count] = (struct pollfd){.fd = read_pipes[stream], .events = POLLIN};
                    poll_streams[poll_count] = stream;
                    poll_count += 1;
                }
            }

            int poll_result = poll(poll_fds, poll_count, -1);
            if (poll_result < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                string_print(S8("Failed to poll process pipes: {EOs}\n"), os_get_last_error());
                break;
            }

            for (nfds_t poll_index = 0; poll_index < poll_count; poll_index += 1)
            {
                if (!(poll_fds[poll_index].revents & (POLLIN | POLLHUP | POLLERR)))
                {
                    continue;
                }

                u64 stream = poll_streams[poll_index];
                u8 buffer[16 * 1024];
                ssize_t read_result = read(read_pipes[stream], buffer, sizeof(buffer));

                if (read_result > 0)
                {
                    pipe_capture_append(scratch.arena, &captures[stream], buffer, (u64)read_result);
                }
                else
                {
                    if (read_result < 0 && errno != EINTR)
                    {
                        string_print(S8("Failed to read from process pipe: {EOs}\n"), os_get_last_error());
                    }

                    if (read_result == 0 || (read_result < 0 && errno != EINTR))
                    {
                        close(read_pipes[stream]);
                        read_pipes[stream] = -1;
                        open_pipe_count -= 1;
                    }
                }
            }
        }

        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            if (captured[stream])
            {
                result.streams[stream] = pipe_capture_flatten(arena, &captures[stream]);
            }
        }

        int status;
        int options = 0;
        struct rusage usage;
        pid_t wait_result = wait4(pid, &status, options, &usage);

        if (program_flag_get(PROGRAM_FLAG_VERBOSE))
        {
            string_print(S8("Process [{s32}]: Time (user): {s64}:{s64} s,us, (system): {s64}:{s64} s,us. Max RSS: {s64} KB. PF (soft): {s64}, (hard): {s64}. "
                            "Block (input): {s64}, (output): {s64}. CTX SW (vol): {s64}, (invol): {s64}\n"),
                         pid, usage.ru_utime.tv_sec, usage.ru_utime.tv_usec, usage.ru_stime.tv_sec, usage.ru_stime.tv_usec, usage.ru_maxrss, usage.ru_minflt,
                         usage.ru_majflt, usage.ru_inblock, usage.ru_oublock, usage.ru_nvcsw, usage.ru_nivcsw);
        }

        if (wait_result == pid && WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            // Exit codes past the enum range carry no meaning as ProcessResult
            // values; collapse them to plain failure.
            result.result = exit_code < (int)PROCESS_RESULT_COUNT ? (ProcessResult)exit_code : PROCESS_RESULT_FAILED;
        }
        else if (wait_result == pid && WIFSIGNALED(status))
        {
            result.result = PROCESS_RESULT_CRASH;
        }
        else
        {
            result.result = PROCESS_RESULT_FAILED;
        }
#endif
        scratch_end(scratch);
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

String8 string8_from_os_error(Arena* arena, OsError error, bool null_terminate)
{
    String8 result = {0};

#if defined(_WIN32)
    TemporalArena temp = scratch_begin(&arena, 1);
    DWORD buffer_size = 64 * 1024 / 2;
    char16* buffer = arena_allocate(temp.arena, char16, buffer_size);
    DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, (DWORD)error.v, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  buffer, buffer_size, 0);
    if (length != 0)
    {
        // Strip the trailing "\r\n" FormatMessage usually appends, but do not
        // assume it is present.
        while (length && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n'))
        {
            length -= 1;
        }

        String16 string16 = (String16){.pointer = buffer, .length = length};
        result = string8_from_string16(arena, string16, null_terminate);
    }

    scratch_end(temp);
#else
    const char* error_raw_string = strerror((int)error.v);
    String8 error_string = string_from_pointer(error_raw_string);
    result = string_duplicate_arena(arena, error_string, null_terminate);
#endif

    return result;
}

String8 os_get_environment_variable(String8 variable)
{
    String8 result = {0};
    if (variable.pointer && variable.length)
    {
        String8* env_pointer = program_state->input.environment_keys.pointer;
        u64 env_count = program_state->input.environment_keys.length;
        for (u64 i = 0; i < env_count; i += 1)
        {
            String8 env = env_pointer[i];
            if (string_equal(variable, env))
            {
                result = program_state->input.environment_values.pointer[i];
                break;
            }
        }
    }
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

OsModuleHandle* os_dynamic_library_load(String8 library)
{
    OsModuleHandle* result = {0};
    BUSTER_CHECK(BUSTER_SLICE_IS_ZERO_TERMINATED(library));

#if defined(_WIN32)
    TemporalArena temp = scratch_begin(0, 0);
    String16 library_w = string16_from_string8(temp.arena, library, true);
    result = (OsModuleHandle*)LoadLibraryW(library_w.pointer);
    scratch_end(temp);
#else
    result = (OsModuleHandle*)dlopen(library.pointer, RTLD_NOW | RTLD_LOCAL);
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

u64 os_get_current_process_id(void)
{
#if defined(__linux__) || defined(__APPLE__)
    return (u64)getpid();
#else
    return (u64)GetCurrentProcessId();
#endif
}

OsProcessHandle* os_get_current_process_handle(void)
{
    OsProcessHandle* result;
#if defined(__linux__) || defined(__APPLE__)
    result = (OsProcessHandle*)os_get_current_process_id();
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
#else
    BUSTER_UNUSED(thread_name);
#endif
#else
#error unsupported platform
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
        os_fail();
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
    if (QueryPerformanceCounter(&os_counter))
    {
        // Split the conversion so counter * 1e6 cannot overflow 64 bits
        // (a 10 MHz counter would overflow after ~10 days of uptime).
        u64 counter = (u64)os_counter.QuadPart;
        u64 frequency = os_state.frequency;
        u64 whole_seconds = counter / frequency;
        u64 remainder_ticks = counter % frequency;
        result = whole_seconds * (1000 * 1000) + (remainder_ticks * (1000 * 1000)) / frequency;
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
    u64 bits_per_element = sizeof(*flag_pointer) * 8;
    u64 element_index = flag_index / bits_per_element;
    u64 bit_index = flag_index % bits_per_element;
    flag_pointer[element_index] = (flag_pointer[element_index] & ~((u64)1 << bit_index)) | ((u64)flag_value << bit_index);
}

bool flag_get_ex(u64* flag_pointer, u64 flag_count, u64 flag_index)
{
    BUSTER_CHECK(flag_index < flag_count);
    u64 bits_per_element = sizeof(*flag_pointer) * 8;
    u64 element_index = flag_index / bits_per_element;
    u64 bit_index = flag_index % bits_per_element;
    return (flag_pointer[element_index] & ((u64)1 << bit_index)) != 0;
}

BooleanArgumentProcessResult boolean_argument_process(String8* flag_string_start_pointer, u64 flag_string_start_count, u64* flag_pointer, u64 flag_count,
                                                      String8 argument)
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
                    break;
                case '0':
                case '1':
                {
                    bool flag_value = v == '1';
                    flag_set_ex(flag_pointer, flag_count, result.index, flag_value);
                    result.valid = true;
                }
                break;
                default:
                {
                }
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

String8 executable_resolve_in_path(Arena* arena, String8 file)
{
    TemporalArena temp = scratch_begin(0, 0);

    String8 result = {0};
    String8 path_value = {0};
    String8* key_pointer = program_state->input.environment_keys.pointer;
    u64 key_length = program_state->input.environment_keys.length;
#if defined(_WIN32)
    String8 path_key = S8("Path");
#else
    String8 path_key = S8("PATH");
#endif

    for (u64 i = 0; i < key_length; i += 1)
    {
        String8 candidate_key = key_pointer[i];
        if (string_equal(path_key, candidate_key))
        {
            path_value = program_state->input.environment_values.pointer[i];
            break;
        }
    }

    if (path_value.pointer && path_value.length)
    {
        String8 path_it = path_value;

#if defined(_WIN32)
        char8 path_separator = ';';
#else
        char8 path_separator = ':';
#endif

#if defined(_WIN32)
        String8 exe_part = string_ends_with_sequence(file, S8(".exe")) ? S8("") : S8(".exe");
#endif

        while (true)
        {
            u64 colon_index = string_first_code_unit(path_it, path_separator);
            bool is_end = colon_index == BUSTER_STRING_NO_MATCH;
            u64 it_end = is_end ? path_it.length : colon_index;
            String8 it = string_slice(path_it, 0, it_end);
            if (it.length == 0)
            {
                // An empty PATH component conventionally means the current
                // directory, not the filesystem root.
                it = S8(".");
            }
            String8 parts[] = {
                it,
                S8("/"),
                file,
#if defined(_WIN32)
                exe_part,
#endif
            };

            String8 full_path = string_join_arena(temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true);

            bool found;
#if defined(_WIN32)
            DWORD file_attributes = GetFileAttributesW(string16_from_string8(temp.arena, full_path, true).pointer);
            found = file_attributes != INVALID_FILE_ATTRIBUTES && (file_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
            // access(X_OK) alone also matches directories (e.g. a "cmake/"
            // directory in a "." PATH component); require a regular file.
            struct stat file_stat;
            found = access(full_path.pointer, X_OK) == 0 && stat(full_path.pointer, &file_stat) == 0 && S_ISREG(file_stat.st_mode);
#endif
            if (found)
            {
                result = string_duplicate_arena(arena, full_path, true);
                break;
            }

            if (is_end)
            {
                break;
            }

            path_it = string_slice(path_it, it_end + 1, path_it.length);
        }
    }

    scratch_end(temp);

    return result;
}
