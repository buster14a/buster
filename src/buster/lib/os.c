// Implementation of the platform boundary declared in os.h. Each facility
// keeps its POSIX and Windows paths side by side inside one function
// rather than in per-platform files, so the contract stays in one place:
// virtual memory (os_reserve/os_commit/os_decommit, the advisory os_prefault
// hint, and protection flags),
// threads, mutexes, and TLS, checked file IO (os_file_write_checked,
// os_file_close_checked, os_file_flush), replacement publication
// (os_file_replacement_target_stats, os_file_staging_create, os_file_replace),
// process spawn/wait with deadlines, executable lookup, dynamic libraries, and
// the crash/failure printers. Replacement publication follows os_file_close.
// The lane model's implementation lives at the bottom — lane_run dispatches through a
// persistent LaneGang of workers that survives across phases
// (lane_persistent_worker_entry_point); creating threads per phase is the
// shape it exists to avoid.

#include <buster/lib/os.h>
#include <buster/lib/os_internal.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/arena.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>

#if BUSTER_LINUX
#include <limits.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
// rename(2) is declared only by <stdio.h>.
#include <stdio.h>
#endif

#if !BUSTER_WINDOWS
#include <sys/ioctl.h>
#endif

#if BUSTER_WINDOWS
// FileRenameInfoEx has ABI value 22. Some MinGW headers hide its enum
// name behind NTDDI_VERSION even though the API and rename flags are declared.
#define BUSTER_WINDOWS_FILE_RENAME_INFO_EX ((FILE_INFO_BY_HANDLE_CLASS)22)
#endif

#if BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64 && defined(MAP_JIT)
extern void pthread_jit_write_protect_np(int enabled);
#endif

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

#if !BUSTER_SINGLE_THREADED
BUSTER_GLOBAL_LOCAL void lane_gang_release(ThreadContext* thread_context);
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

    if (options.identity)
    {
        DWORD attributes = file_information.dwFileAttributes;
        stats->device = file_information.dwVolumeSerialNumber;
        stats->index = ((u64)file_information.nFileIndexHigh << 32) | (u64)file_information.nFileIndexLow;
        stats->permissions = (attributes & FILE_ATTRIBUTE_READONLY) ? 0444u : 0666u;
        // Only a handle opened with FILE_FLAG_OPEN_REPARSE_POINT can report
        // the reparse point itself rather than its target.
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            stats->kind = OS_FILE_KIND_LINK;
        }
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            stats->kind = OS_FILE_KIND_DIRECTORY;
        }
        else
        {
            stats->kind = OS_FILE_KIND_REGULAR;
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

#if defined(__APPLE__)
bool os_apple_process_is_traced(u32 process_flags)
{
    return (process_flags & P_TRACED) != 0;
}
#endif

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
        int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
        struct kinfo_proc process_info = {0};
        size_t process_info_size = sizeof(process_info);
        bool traced = sysctl(query, 4, &process_info, &process_info_size, 0, 0) == 0 &&
                      process_info_size >= sizeof(process_info) && os_apple_process_is_traced((u32)process_info.kp_proc.p_flag);
        program_state->_is_debugger_present = traced;
#elif defined(_WIN32)
        BOOL os_result = IsDebuggerPresent();
        program_state->_is_debugger_present = os_result != 0;
#else
#error is_debugger_present requires a supported platform
#endif
    }

    return (bool)program_state->_is_debugger_present;
}

BUSTER_NORETURN BUSTER_COLD void os_fail_va(u32 line, String8 function, String8 file, String8 context, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, context);
    String8 message = string_format_va(scratch.arena, context, variable_arguments, STRING_FORMAT_VA_GP_SLOTS(7));
    va_end(variable_arguments);
    os_fail_raw(line, function, file, message);
}

BUSTER_COLD BUSTER_GLOBAL_LOCAL void os_fail_raw_write(OsFileDescriptor* stream, String8 string)
{
    if (string.length)
    {
        // The process already failed. A full, closed, or disconnected error
        // stream must not recursively enter the fatal reporter again.
        (void)os_file_write_attempt(stream, BUSTER_SLICE_TO_BYTE_SLICE(string));
    }
}

// The formatted reporter above needs a scratch arena, so it cannot describe a
// failure that is itself about the thread context being unavailable: the report
// would re-enter the failing check and recurse until the stack is gone. This
// variant writes only string literals and caller-owned storage.
BUSTER_NORETURN BUSTER_COLD void os_fail_raw(u32 line, String8 function, String8 file, String8 context)
{
    OsFileDescriptor* stream = os_get_standard_stream(STANDARD_STREAM_ERROR);
    char8 line_digits[10];
    u64 line_length = 0;
    do
    {
        line_digits[BUSTER_ARRAY_LENGTH(line_digits) - 1 - line_length] = (char8)('0' + line % 10);
        line_length += 1;
        line /= 10;
    } while (line && line_length < BUSTER_ARRAY_LENGTH(line_digits));

    os_fail_raw_write(stream, context);
    os_fail_raw_write(stream, S8(" at "));
    os_fail_raw_write(stream, file);
    os_fail_raw_write(stream, S8(":"));
    os_fail_raw_write(stream, string_from_pointer_length(line_digits + BUSTER_ARRAY_LENGTH(line_digits) - line_length, line_length));
    os_fail_raw_write(stream, S8(" in "));
    os_fail_raw_write(stream, function);
    os_fail_raw_write(stream, S8("\n"));
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
#if defined(__APPLE__) && defined(MAP_JIT)
        MAP_JIT * flags.jit |
#endif
        MAP_FIXED * flags.fixed | MAP_PRIVATE * flags.priv | MAP_ANON * flags.anonymous | MAP_NORESERVE * flags.no_reserve;

    return result;
}

// File descriptors are stored biased by +1 so that fd 0 (stdin) does not
// collide with the null pointer, which means "no descriptor" everywhere.
BUSTER_GLOBAL_LOCAL OsFileDescriptor* posix_fd_to_generic_fd(int fd)
{
    BUSTER_VALIDATE(fd >= 0);
    return (OsFileDescriptor*)((u64)fd + 1);
}

int generic_fd_to_posix(OsFileDescriptor* fd)
{
    BUSTER_CHECK(fd);
    return (int)((u64)fd - 1);
}
#elif defined(_WIN32)
BUSTER_GLOBAL_LOCAL DWORD os_windows_protection_flags(ProtectionFlags flags)
{
    DWORD result;

    // Windows has no write-only protection. A writable request necessarily
    // grants read access too; an empty request is a valid no-access page.
    if (flags.execute)
    {
        if (flags.write)
        {
            result = PAGE_EXECUTE_READWRITE;
        }
        else if (flags.read)
        {
            result = PAGE_EXECUTE_READ;
        }
        else
        {
            result = PAGE_EXECUTE;
        }
    }
    else if (flags.write)
    {
        result = PAGE_READWRITE;
    }
    else if (flags.read)
    {
        result = PAGE_READONLY;
    }
    else
    {
        result = PAGE_NOACCESS;
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

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool os_prefault_test_forced;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL OsPrefaultResult os_prefault_test_forced_result;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL OsPrefaultTestCounters os_prefault_test_state;

void os_prefault_test_force_next(OsPrefaultResult result)
{
    os_prefault_test_forced = true;
    os_prefault_test_forced_result = result;
}

OsPrefaultTestCounters os_prefault_test_counters(void)
{
    return os_prefault_test_state;
}

typedef struct OsResourceTestFailure OsResourceTestFailure;
struct OsResourceTestFailure
{
    u64 calls_before_failure;
    bool armed;
};

BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL OsResourceTestFailure
    os_resource_test_failures[(u64)OS_RESOURCE_TEST_OPERATION_COUNT];

void os_resource_test_fail_on_call(OsResourceTestOperation operation, u64 call_index)
{
    BUSTER_VALIDATE((u64)operation < (u64)OS_RESOURCE_TEST_OPERATION_COUNT);
    os_resource_test_failures[(u64)operation] = (OsResourceTestFailure){
        .calls_before_failure = call_index,
        .armed = true,
    };
}

void os_resource_test_clear(void)
{
    memset(os_resource_test_failures, 0, sizeof(os_resource_test_failures));
}

BUSTER_GLOBAL_LOCAL bool os_resource_test_should_fail(OsResourceTestOperation operation)
{
    BUSTER_VALIDATE((u64)operation < (u64)OS_RESOURCE_TEST_OPERATION_COUNT);
    OsResourceTestFailure* failure = &os_resource_test_failures[(u64)operation];
    bool result = false;
    if (failure->armed)
    {
        if (failure->calls_before_failure)
        {
            failure->calls_before_failure -= 1;
        }
        else
        {
            failure->armed = false;
            result = true;
        }
    }
    return result;
}
#endif

// Best-effort prefaulting of an already-committed range: it populates page
// table entries now so the first touch need not take a fault, and nothing
// more. No path below keeps a page resident, so no caller may read success
// as residency, as protection from paging, or as a latency guarantee. The
// three platforms reach it by different means and refuse it for different
// ordinary reasons, which is why the outcome is reported instead of hidden:
//
// Linux: madvise(MADV_POPULATE_WRITE) states exactly this intent and locks
// nothing. Kernels before 5.14 do not know the advice and reject it with
// EINVAL, which is a refusal of the request, never a commit failure.
//
// macOS: Darwin has no populate advice, so the range is locked and then
// immediately unlocked. The lock is only what forces the faults in; it is
// released before returning, so nothing stays pinned. RLIMIT_MEMLOCK bounds
// an unprivileged process, so refusing a large range here is ordinary.
//
// Windows: VirtualAlloc(MEM_COMMIT) already charges the backing store, and
// Windows exposes no supported populate call. An MSVC build can force the
// faults by registering the range as a Winsock Registered I/O buffer, which
// locks it for the registration and releases it on deregistration. That
// table is present only once the process obtained the RIO extension
// functions, and its length is 32 bits, so the ordinary Windows outcome is
// that no prefault is performed at all.
OsPrefaultResult os_prefault(void* address, u64 size)
{
    OsPrefaultResult result;
#if BUSTER_INCLUDE_TESTS
    if (os_prefault_test_forced)
    {
        os_prefault_test_forced = false;
        result = os_prefault_test_forced_result;
    }
    else
#endif
    {
#if defined(__linux__)
        result = madvise(address, size, MADV_POPULATE_WRITE) == 0 ? OS_PREFAULT_POPULATED : OS_PREFAULT_REFUSED;
#elif defined(__APPLE__)
        result = OS_PREFAULT_REFUSED;
        if (mlock(address, size) == 0)
        {
            // Releasing the lock is what makes this prefaulting rather than
            // pinning. A failed release would leave the process holding
            // locked memory it never promised to hold, so it is reported as
            // a refusal rather than as a populated range.
            result = munlock(address, size) == 0 ? OS_PREFAULT_POPULATED : OS_PREFAULT_REFUSED;
        }
#elif defined(_WIN32) && defined(_MSC_VER)
        result = OS_PREFAULT_UNAVAILABLE;
        // Both halves are required: a registration that cannot be released
        // would leave the range locked for the life of the process, which is
        // the opposite of what this function promises.
        if (w32_rio_functions.RIORegisterBuffer && w32_rio_functions.RIODeregisterBuffer)
        {
            // The registration length is a DWORD. A larger range would be
            // registered only in part, so refuse it instead of reporting a
            // range that was never fully populated.
            if (size > UINT32_MAX)
            {
                result = OS_PREFAULT_REFUSED;
            }
            else
            {
                RIO_BUFFERID buffer_id = w32_rio_functions.RIORegisterBuffer((PCHAR)address, (DWORD)size);
                result = buffer_id == RIO_INVALID_BUFFERID ? OS_PREFAULT_REFUSED : OS_PREFAULT_POPULATED;
                if (result == OS_PREFAULT_POPULATED)
                {
                    w32_rio_functions.RIODeregisterBuffer(buffer_id);
                }
            }
        }
#else
        BUSTER_UNUSED(address);
        BUSTER_UNUSED(size);
        result = OS_PREFAULT_UNAVAILABLE;
#endif
    }
#if BUSTER_INCLUDE_TESTS
    os_prefault_test_state.requests += 1;
    os_prefault_test_state.unpopulated += result != OS_PREFAULT_POPULATED;
    os_prefault_test_state.last = result;
#endif
    return result;
}

bool os_commit(void* address, u64 size, ProtectionFlags protection, bool prefault)
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

    // Strictly subordinate and strictly advisory: the request is issued only
    // once the commit itself succeeded, and its outcome is deliberately kept
    // out of `result`. A caller that needs the outcome asks os_prefault.
    if (result & prefault)
    {
        (void)os_prefault(address, size);
    }

#if BUSTER_BENCH_ALLOCATIONS
    arena_benchmark_event(ARENA_BENCHMARK_OS_COMMIT, S8(__FILE__), S8(__func__), __LINE__, size, 0, 0, 0, result);
#endif
    return result;
}

bool os_protect(void* address, u64 size, ProtectionFlags protection)
{
#if defined(__linux__) || defined(__APPLE__)
    return mprotect(address, size, os_posix_protection_flags(protection)) == 0;
#elif defined(_WIN32)
    DWORD previous_protection = 0;
    return VirtualProtect(address, (SIZE_T)size, os_windows_protection_flags(protection), &previous_protection) != 0;
#endif
}

bool os_decommit(void* address, u64 size)
{
    u64 page_size = os_get_page_size();
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(page_size));
    BUSTER_CHECK(size && is_aligned((u64)address, page_size) && is_aligned(size, page_size));
    bool result = true;
#if defined(__linux__) || defined(__APPLE__)
    // The reservation is already mapped for its lifetime. One syscall keeps
    // failure atomic with respect to arena accounting: a successful call
    // advises that pages are unused, while failure leaves both protection and
    // logical high-water mark unchanged. Retain the old fault guard as a
    // best-effort success-side step; os_commit restores access before reuse.
    // Unlike Linux, Darwin's MADV_DONTNEED does not promise zeroed contents.
    result = madvise(address, size, MADV_DONTNEED) == 0;
    if (result)
    {
        (void)mprotect(address, size, PROT_NONE);
    }
#elif defined(_WIN32)
    result = VirtualFree(address, size, MEM_DECOMMIT) != 0;
#endif
#if BUSTER_BENCH_ALLOCATIONS
    arena_benchmark_event(ARENA_BENCHMARK_OS_DECOMMIT, S8(__FILE__), S8(__func__), __LINE__, size, 0, 0, 0, result);
#endif
    return result;
}

void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map)
{
    void* address = 0;

#if defined(__linux__) || defined(__APPLE__)
    // An SDK without MAP_JIT cannot honour a JIT reservation, so the mapping is
    // skipped rather than made without the flag the caller asked for. Only the
    // `if` is conditional; the block below is not, so the braces balance in
    // every configuration.
#if defined(__APPLE__) && !defined(MAP_JIT)
    if (!map.jit)
#endif
    {
        int protection_flags = os_posix_protection_flags(protection);
        int map_flags = os_posix_map_flags(map);

        address = mmap(base, size, protection_flags, map_flags, -1, 0);
        if (address == MAP_FAILED)
        {
            address = 0;
        }
    }
#elif defined(_WIN32)
    DWORD allocation_flags = os_windows_allocation_flags(map);
    DWORD protection_flags = os_windows_protection_flags(protection);
    address = VirtualAlloc(base, size, allocation_flags, protection_flags);
#endif
#if BUSTER_BENCH_ALLOCATIONS
    arena_benchmark_event(ARENA_BENCHMARK_OS_RESERVE, S8(__FILE__), S8(__func__), __LINE__, size, 0, 0, 0, address != 0);
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

void os_jit_write_protect(bool enabled)
{
#if BUSTER_MACOS && BUSTER_CPU_ARCH_AARCH64 && defined(MAP_JIT)
    pthread_jit_write_protect_np(enabled);
#else
    BUSTER_UNUSED(enabled);
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

// Threads this process started through os_thread_create and has not yet seen
// return. A global built on first use and read afterwards through plain loads
// is only sound to build while this is zero, which is what
// os_is_only_live_thread() reports and BUSTER_CHECK_SERIAL_INITIALIZATION
// states. Counted rather than derived from the lane context because a raw
// os_thread_create thread is a lane of one and would look serial.
BUSTER_GLOBAL_LOCAL AtomicU64 os_live_thread_count;

bool os_is_only_live_thread(void)
{
    bool result = os_live_thread_count == 0;
    return result;
}

BUSTER_GLOBAL_LOCAL void thread_entry_point(ThreadCallback* user_entry_point, void* user_argument)
{
    ThreadContext* thread_context = thread_context_allocate();
    thread_context_select(thread_context);
    user_entry_point(user_argument);
    thread_context_release(thread_context);
    // thread_context_release parks its default scratch arenas for reuse. An
    // OS worker is about to lose this TLS pool, so unmap it instead of
    // stranding the reservations and touched pages until process exit.
    arena_pool_release_thread();
#if BUSTER_BENCH_ALLOCATIONS
    arena_benchmark_flush(false);
#endif
    // Last, so the count covers every instant this thread could still have
    // touched a shared global. os_thread_join returns after this store.
    atomic_u64_decrement(&os_live_thread_count);
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
    OsEntity* result = 0;
    bool create_thread = true;
#if BUSTER_INCLUDE_TESTS
    create_thread = !os_resource_test_should_fail(OS_RESOURCE_TEST_THREAD_CREATE);
#endif
    if (create_thread)
    {
        result = os_entity_allocate(OS_ENTITY_KIND_THREAD);
        result->thread.callback = options.callback;
        result->thread.argument = options.argument;
        // Counted before the thread exists rather than from inside it, so no
        // window has the new thread running while the process still looks serial.
        atomic_u64_increment(&os_live_thread_count);
#if defined(__linux__) || defined(__APPLE__)
        int create_result = pthread_create(&result->thread.handle, 0, &pthread_entry_point, result);
        bool os_result = create_result == 0;
        if (!os_result)
        {
            atomic_u64_decrement(&os_live_thread_count);
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
            atomic_u64_decrement(&os_live_thread_count);
            os_entity_release(result);
            result = 0;
        }
#endif
    }
    return (OsThreadHandle*)result;
}

bool os_thread_join(OsThreadHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;
    bool join_thread = true;
#if BUSTER_INCLUDE_TESTS
    join_thread = !os_resource_test_should_fail(OS_RESOURCE_TEST_THREAD_JOIN);
#endif

    bool result = false;
    if (join_thread)
    {
#if defined(__linux__) || defined(__APPLE__)
        void* void_return_value = 0;
        int join_result = pthread_join(entity->thread.handle, &void_return_value);
        result = join_result == 0;
#elif defined(_WIN32)
        DWORD wait_result = WaitForSingleObject(entity->thread.handle, INFINITE);
        result = wait_result == WAIT_OBJECT_0 && CloseHandle(entity->thread.handle);
#endif
    }
    // A failed wait leaves the handle owned by the caller so cleanup can retry.
    if (result)
    {
        os_entity_release(entity);
    }
    return result;
}

OsMutexHandle* os_mutex_create(void)
{
    OsEntity* result = os_entity_allocate(OS_ENTITY_KIND_MUTEX);
#if defined(__linux__) || defined(__APPLE__)
    if (pthread_mutex_init(&result->mutex, 0) != 0)
    {
        os_entity_release(result);
        result = 0;
    }
#elif defined(_WIN32)
    InitializeCriticalSection(&result->mutex);
#endif

    return (OsMutexHandle*)result;
}

void os_mutex_lock(OsMutexHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;
#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_lock(&entity->mutex);
#elif defined(_WIN32)
    EnterCriticalSection(&entity->mutex);
#endif
}

void os_mutex_unlock(OsMutexHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;
#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_unlock(&entity->mutex);
#elif defined(_WIN32)
    LeaveCriticalSection(&entity->mutex);
#endif
}

void os_mutex_destroy(OsMutexHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;
#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_destroy(&entity->mutex);
#elif defined(_WIN32)
    DeleteCriticalSection(&entity->mutex);
#endif
    os_entity_release(entity);
}

#if !BUSTER_SINGLE_THREADED
OsBarrierHandle* os_barrier_create(u32 thread_count)
{
    BUSTER_CHECK(thread_count >= 1);
    OsEntity* result = 0;
    bool create_barrier = true;
#if BUSTER_INCLUDE_TESTS
    create_barrier = !os_resource_test_should_fail(OS_RESOURCE_TEST_BARRIER_CREATE);
#endif
    if (create_barrier)
    {
        result = os_entity_allocate(OS_ENTITY_KIND_BARRIER);
        result->barrier.threshold = thread_count;
        result->barrier.arrived = 0;
        result->barrier.generation = 0;
#if defined(__linux__) || defined(__APPLE__)
        // The condition variable is only attempted once the mutex exists, and it
        // unwinds the mutex when it fails; both failures leave `result` null.
        if (pthread_mutex_init(&result->barrier.mutex, 0) != 0)
        {
            os_entity_release(result);
            result = 0;
        }
        else if (pthread_cond_init(&result->barrier.condition, 0) != 0)
        {
            pthread_mutex_destroy(&result->barrier.mutex);
            os_entity_release(result);
            result = 0;
        }
#elif defined(_WIN32)
        InitializeCriticalSection(&result->barrier.mutex);
        InitializeConditionVariable(&result->barrier.condition);
#endif
    }

    return (OsBarrierHandle*)result;
}

void os_barrier_wait(OsBarrierHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;
#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_lock(&entity->barrier.mutex);
    u64 generation = entity->barrier.generation;
    entity->barrier.arrived += 1;
    if (entity->barrier.arrived == entity->barrier.threshold)
    {
        entity->barrier.arrived = 0;
        entity->barrier.generation += 1;
        pthread_cond_broadcast(&entity->barrier.condition);
    }
    else
    {
        while (entity->barrier.generation == generation)
        {
            pthread_cond_wait(&entity->barrier.condition, &entity->barrier.mutex);
        }
    }
    pthread_mutex_unlock(&entity->barrier.mutex);
#elif defined(_WIN32)
    EnterCriticalSection(&entity->barrier.mutex);
    u64 generation = entity->barrier.generation;
    entity->barrier.arrived += 1;
    if (entity->barrier.arrived == entity->barrier.threshold)
    {
        entity->barrier.arrived = 0;
        entity->barrier.generation += 1;
        WakeAllConditionVariable(&entity->barrier.condition);
    }
    else
    {
        while (entity->barrier.generation == generation)
        {
            SleepConditionVariableCS(&entity->barrier.condition, &entity->barrier.mutex, INFINITE);
        }
    }
    LeaveCriticalSection(&entity->barrier.mutex);
#endif
}

void os_barrier_destroy(OsBarrierHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;
#if defined(__linux__) || defined(__APPLE__)
    pthread_cond_destroy(&entity->barrier.condition);
    pthread_mutex_destroy(&entity->barrier.mutex);
#elif defined(_WIN32)
    // Condition variables need no deletion on Windows.
    DeleteCriticalSection(&entity->barrier.mutex);
#endif
    os_entity_release(entity);
}
#endif

u64 process_control_atomic_load(ProcessControlAtomic* address)
{
    u64 result;
#if BUSTER_SINGLE_THREADED
    result = *address;
#elif BUSTER_COMPILER_MSVC
    result = (u64)_InterlockedCompareExchange64((volatile long long*)address, 0, 0);
#elif defined(__clang__)
    result = __c11_atomic_load(address, __ATOMIC_SEQ_CST);
#else
    result = __atomic_load_n(address, __ATOMIC_SEQ_CST);
#endif
    return result;
}

void process_control_atomic_store(ProcessControlAtomic* address, u64 value)
{
#if BUSTER_SINGLE_THREADED
    *address = (s32)value;
#elif BUSTER_COMPILER_MSVC
    _InterlockedExchange64((volatile long long*)address, (long long)value);
#elif defined(__clang__)
    __c11_atomic_store(address, value, __ATOMIC_SEQ_CST);
#else
    __atomic_store_n(address, value, __ATOMIC_SEQ_CST);
#endif
}

bool process_control_atomic_set_if_zero(ProcessControlAtomic* address, u64 value)
{
    bool result;
#if BUSTER_SINGLE_THREADED
    result = *address == 0;
    if (result) { *address = (s32)value; }
#elif BUSTER_COMPILER_MSVC
    result = _InterlockedCompareExchange64((volatile long long*)address, (long long)value, 0) == 0;
#elif defined(__clang__)
    u64 expected = 0;
    result = __c11_atomic_compare_exchange_strong(address, &expected, value, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#else
    u64 expected = 0;
    result = __atomic_compare_exchange_n(address, &expected, value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
    return result;
}

u64 atomic_u64_add(AtomicU64* address, u64 addend)
{
    // Builtins rather than <stdatomic.h>: that header lives in the host
    // compiler's resource directory, which self-hosted compiler generations
    // deliberately do not have.
    u64 result;
#if BUSTER_SINGLE_THREADED
    result = *address;
    *address += addend;
#elif BUSTER_COMPILER_MSVC
    result = (u64)_InterlockedExchangeAdd64((volatile long long*)address, (long long)addend);
#elif defined(__clang__)
    // clang, zig cc, and the self-hosted compiler, which defines __clang__.
    result = __c11_atomic_fetch_add(address, addend, __ATOMIC_SEQ_CST);
#else
    result = __atomic_fetch_add(address, addend, __ATOMIC_SEQ_CST);
#endif
    return result;
}

u64 atomic_u64_increment(AtomicU64* address)
{
    u64 result = atomic_u64_add(address, 1);
    return result;
}

u64 atomic_u64_decrement(AtomicU64* address)
{
    // Two's complement wrap, which is what every fetch_add lowers a subtract
    // to anyway; there is no separate fetch_sub to reach for on MSVC.
    u64 result = atomic_u64_add(address, ~(u64)0);
    return result;
}

String8 os_path_absolute(Arena* arena, String8 relative_file_path, bool null_terminate)
{
    String8 result = {0};
#if defined(__linux__) || defined(__APPLE__)
    bool valid = relative_file_path.pointer || !relative_file_path.length;
    for (u64 i = 0; i < relative_file_path.length && valid; i += 1)
    {
        valid = relative_file_path.pointer[i] != 0;
    }
    if (valid && relative_file_path.length)
    {
        TemporalArena temp = scratch_begin(&arena, 1);
        String8 terminated = string_duplicate_arena(temp.arena, relative_file_path, true);
        u64 position = arena->position;
        u64 length = PATH_MAX;
        char8* buffer = arena_allocate(arena, char8, length + null_terminate);
        char* syscall_result = realpath((char*)terminated.pointer, buffer);

        if (syscall_result)
        {
            result = string_from_pointer(syscall_result);
            BUSTER_VALIDATE(result.length <= length);
        }

        arena_set_position(arena, position + result.length + null_terminate);
        scratch_end(temp);
    }
#elif defined(_WIN32)
    TemporalArena temp = scratch_begin(&arena, 1);
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

String8 os_path_absolute_lexical(Arena* arena, String8 path, bool null_terminate)
{
    String8 result = {0};
    bool valid = path.pointer || !path.length;
    for (u64 i = 0; i < path.length && valid; i += 1)
    {
        valid = path.pointer[i] != 0;
    }
    if (valid)
    {
#if defined(_WIN32)
        result = os_path_absolute(arena, path, null_terminate);
#else
        if (path.length && path.pointer[0] == '/')
        {
            result = string_duplicate_arena(arena, path, null_terminate);
        }
        else
        {
            char current[PATH_MAX];
            if (getcwd(current, sizeof(current)))
            {
                String8 parts[] = {string_from_pointer(current), S8("/"), path};
                result = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), null_terminate);
            }
        }
#endif
    }
    return result;
}

#if defined(__APPLE__)
// libSystem's own query, declared here rather than through <mach-o/dyld.h> so
// that self-hosting on Apple targets parses no additional SDK header.
extern int _NSGetExecutablePath(char* buffer, uint32_t* buffer_size);
#endif

String8 os_executable_path(Arena* arena)
{
    String8 result = {0};
#if defined(__linux__)
    // readlink reports truncation only by filling the buffer, so a result that
    // fills it is retried with twice the room.
    bool done = false;
    for (u64 capacity = 1024; !done && capacity <= BUSTER_MB(1); capacity *= 2)
    {
        TemporalArena scratch = scratch_begin(&arena, 1);
        char8* buffer = arena_allocate(scratch.arena, char8, capacity);
        ssize_t length = readlink("/proc/self/exe", (char*)buffer, capacity);
        if (length <= 0)
        {
            done = true;
        }
        else if ((u64)length < capacity)
        {
            result = string_duplicate_arena(arena, (String8){.pointer = buffer, .length = (u64)length}, false);
            done = true;
        }
        scratch_end(scratch);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(0, &size);
    if (size)
    {
        TemporalArena scratch = scratch_begin(&arena, 1);
        char8* buffer = arena_allocate(scratch.arena, char8, (u64)size + 1);
        if (_NSGetExecutablePath((char*)buffer, &size) == 0)
        {
            u64 length = 0;
            while (length < size && buffer[length])
            {
                length += 1;
            }
            result = string_duplicate_arena(arena, (String8){.pointer = buffer, .length = length}, false);
        }
        scratch_end(scratch);
    }
#elif defined(_WIN32)
    bool done = false;
    for (DWORD capacity = 512; !done && capacity <= 32768; capacity *= 2)
    {
        TemporalArena scratch = scratch_begin(&arena, 1);
        char16* buffer = arena_allocate(scratch.arena, char16, capacity);
        DWORD length = GetModuleFileNameW(0, (LPWSTR)buffer, capacity);
        if (length == 0)
        {
            done = true;
        }
        else if (length < capacity)
        {
            result = string8_from_string16(arena, (String16){.pointer = buffer, .length = length}, false);
            done = true;
        }
        scratch_end(scratch);
    }
#else
    BUSTER_UNUSED(arena);
#endif
    return result;
}

bool os_make_directory_attempt(String8 path)
{
    bool result = path.pointer != 0 && path.length != 0;
    for (u64 i = 0; i < path.length && result; i += 1)
    {
        result = path.pointer[i] != 0;
    }
    if (result)
    {
        TemporalArena temp = scratch_begin(0, 0);
#if defined(_WIN32)
        String16 wide = string16_from_string8(temp.arena, path, true);
        result = CreateDirectoryW(wide.pointer, 0) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
        String8 terminated = string_duplicate_arena(temp.arena, path, true);
        result = mkdir(terminated.pointer, 0700) == 0 || errno == EEXIST;
#endif
        scratch_end(temp);
    }
    return result;
}

void os_make_directory(String8 path)
{
#if defined(__linux__) || defined(__APPLE__)
    bool valid = path.pointer != 0 && path.length != 0;
    for (u64 i = 0; i < path.length && valid; i += 1)
    {
        valid = path.pointer[i] != 0;
    }
    if (valid)
    {
        TemporalArena temp = scratch_begin(0, 0);
        String8 terminated = string_duplicate_arena(temp.arena, path, true);
        mkdir((const char*)terminated.pointer, 0755);
        scratch_end(temp);
    }
#elif defined(_WIN32)
    TemporalArena temp = scratch_begin(0, 0);
    String16 path_w = string16_from_string8(temp.arena, path, true);
    CreateDirectoryW(path_w.pointer, 0);
    scratch_end(temp);
#endif
}

OsDirectoryCreateResult os_make_directory_exclusive(String8 path)
{
    OsDirectoryCreateResult result = {0};
    bool valid = path.pointer != 0 && path.length != 0;
    for (u64 index = 0; index < path.length && valid; index += 1)
    {
        valid = path.pointer[index] != 0;
    }

    if (!valid)
    {
#if defined(_WIN32)
        result.error.v = (u32)ERROR_INVALID_PARAMETER;
#else
        result.error.v = (u32)EINVAL;
#endif
    }
    else
    {
        TemporalArena scratch = scratch_begin(0, 0);
#if defined(_WIN32)
        String16 wide = string16_from_string8(scratch.arena, path, true);
        if (!CreateDirectoryW(wide.pointer, 0))
        {
            result.error = os_get_last_error();
            result.already_exists = result.error.v == (u32)ERROR_ALREADY_EXISTS || result.error.v == (u32)ERROR_FILE_EXISTS;
            if (result.already_exists)
            {
                result.error = (OsError){0};
            }
        }
#elif defined(__linux__) || defined(__APPLE__)
        String8 terminated = string_duplicate_arena(scratch.arena, path, true);
        int status;
        do
        {
            status = mkdir((const char*)terminated.pointer, 0700);
        } while (status < 0 && errno == EINTR);
        if (status < 0)
        {
            result.error = os_get_last_error();
            result.already_exists = result.error.v == (u32)EEXIST;
            if (result.already_exists)
            {
                result.error = (OsError){0};
            }
        }
#else
        result.error.v = 1;
#endif
        scratch_end(scratch);
    }

    return result;
}

bool os_file_delete(String8 path)
{
    return !os_file_delete_checked(path).v;
}

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL String16 os_string16_from_wide(char16* pointer)
{
    u64 length = 0;
    while (pointer[length])
    {
        length += 1;
    }
    return (String16){.pointer = pointer, .length = length};
}

BUSTER_GLOBAL_LOCAL bool os_windows_entry_delete(Arena* arena, String8 path, DWORD attributes)
{
    // Keep conversion storage in the walker's arena. A nested scratch scope
    // can select that same arena and rewind away the pending worklist tasks.
    String16 path_w = string16_from_string8(arena, path, true);
    // Read-only files refuse DeleteFileW until the attribute is cleared.
    if (attributes & FILE_ATTRIBUTE_READONLY)
    {
        SetFileAttributesW(path_w.pointer, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
    // A directory reparse point is unlinked with RemoveDirectoryW, which
    // removes the link itself rather than its target.
    bool result = attributes & FILE_ATTRIBUTE_DIRECTORY ? RemoveDirectoryW(path_w.pointer) != 0 : DeleteFileW(path_w.pointer) != 0;
    if (!result)
    {
        DWORD error = GetLastError();
        result = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    return result;
}
#endif

#if defined(__linux__) || defined(__APPLE__)
typedef struct OsDirectoryDeleteFrame OsDirectoryDeleteFrame;
struct OsDirectoryDeleteFrame
{
    OsDirectoryDeleteFrame* parent;
    DIR* directory;
    String8 name;
    dev_t device;
    ino_t inode;
};

BUSTER_GLOBAL_LOCAL int os_directory_delete_open_path(String8 path)
{
    int result;
    do
    {
        result = open((const char*)path.pointer, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (result < 0 && errno == EINTR);
    return result;
}

BUSTER_GLOBAL_LOCAL int os_directory_delete_open_at(int parent, String8 name)
{
    int result;
    do
    {
        result = openat(parent, (const char*)name.pointer, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    } while (result < 0 && errno == EINTR);
    return result;
}

BUSTER_GLOBAL_LOCAL int os_directory_delete_stat(int descriptor, struct stat* stats)
{
    int result;
    do
    {
        result = fstat(descriptor, stats);
    } while (result != 0 && errno == EINTR);
    return result;
}

BUSTER_GLOBAL_LOCAL int os_directory_delete_stat_at(int parent, String8 name, struct stat* stats)
{
    int result;
    do
    {
        result = fstatat(parent, (const char*)name.pointer, stats, AT_SYMLINK_NOFOLLOW);
    } while (result != 0 && errno == EINTR);
    return result;
}

BUSTER_GLOBAL_LOCAL int os_directory_delete_unlink_at(int parent, String8 name, int flags)
{
    int result;
    do
    {
        result = unlinkat(parent, (const char*)name.pointer, flags);
    } while (result != 0 && errno == EINTR);
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_directory_delete_walk(Arena* arena, String8 root)
{
    // Retain the current directory and the root's parent. On ascent, reopen
    // ".." relative to the retained child and verify the saved parent identity.
    // Restarting the parent's enumeration is safe after removing the child;
    // any failed removal stops the walk. This bounds descriptors independently
    // of depth without following links or reopening a descendant by pathname.
    bool result = true;
    u64 root_end = root.length;
    while (root_end > 1 && root.pointer[root_end - 1] == '/')
    {
        root_end -= 1;
    }
    u64 separator = root_end;
    while (separator && root.pointer[separator - 1] != '/')
    {
        separator -= 1;
    }

    String8 parent_path;
    String8 root_name;
    if (separator)
    {
        u64 parent_length = separator == 1 ? 1 : separator - 1;
        parent_path = (String8){.pointer = root.pointer, .length = parent_length};
        root_name = (String8){.pointer = root.pointer + separator, .length = root_end - separator};
    }
    else
    {
        parent_path = S8(".");
        root_name = (String8){.pointer = root.pointer, .length = root_end};
    }
    if (!root_name.length)
    {
        root_name = S8(".");
    }
    parent_path = string_duplicate_arena(arena, parent_path, true);
    root_name = string_duplicate_arena(arena, root_name, true);

    int root_parent = os_directory_delete_open_path(parent_path);
    OsDirectoryDeleteFrame* frame = 0;
    if (root_parent < 0)
    {
        result = errno == ENOENT;
    }
    else
    {
        struct stat selected;
        if (os_directory_delete_stat_at(root_parent, root_name, &selected) != 0)
        {
            result = errno == ENOENT;
        }
        else if (S_ISLNK(selected.st_mode))
        {
            result = os_directory_delete_unlink_at(root_parent, root_name, 0) == 0 || errno == ENOENT;
        }
        else if (!S_ISDIR(selected.st_mode))
        {
            result = false;
        }
        else
        {
            int descriptor = os_directory_delete_open_at(root_parent, root_name);
            int open_error = descriptor < 0 ? errno : 0;
            struct stat opened;
            bool same = descriptor >= 0 && os_directory_delete_stat(descriptor, &opened) == 0 &&
                        opened.st_dev == selected.st_dev && opened.st_ino == selected.st_ino;
            DIR* directory = same ? fdopendir(descriptor) : 0;
            if (!same || !directory)
            {
                if (descriptor >= 0)
                {
                    close(descriptor);
                }
                result = descriptor < 0 && open_error == ENOENT;
            }
            else
            {
                frame = arena_allocate(arena, OsDirectoryDeleteFrame, 1);
                *frame = (OsDirectoryDeleteFrame){
                    .directory = directory,
                    .name = root_name,
                    .device = opened.st_dev,
                    .inode = opened.st_ino,
                };
            }
        }

        while (frame && result)
        {
            errno = 0;
            struct dirent* entry = readdir(frame->directory);
            if (entry)
            {
                String8 name = string_from_pointer((const char8*)entry->d_name);
                if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
                {
                    int directory = dirfd(frame->directory);
                    struct stat entry_stats;
                    if (os_directory_delete_stat_at(directory, name, &entry_stats) != 0)
                    {
                        if (errno != ENOENT)
                        {
                            result = false;
                        }
                    }
                    else if (S_ISDIR(entry_stats.st_mode))
                    {
                        int child = os_directory_delete_open_at(directory, name);
                        int open_error = child < 0 ? errno : 0;
                        struct stat opened;
                        bool same = child >= 0 && os_directory_delete_stat(child, &opened) == 0 &&
                                    opened.st_dev == entry_stats.st_dev && opened.st_ino == entry_stats.st_ino;
                        DIR* child_directory = same ? fdopendir(child) : 0;
                        if (!same || !child_directory)
                        {
                            if (child >= 0)
                            {
                                close(child);
                            }
                            if (child >= 0 || open_error != ENOENT)
                            {
                                result = false;
                            }
                        }
                        else
                        {
                            OsDirectoryDeleteFrame* child_frame = arena_allocate(arena, OsDirectoryDeleteFrame, 1);
                            *child_frame = (OsDirectoryDeleteFrame){
                                .parent = frame,
                                .directory = child_directory,
                                .name = string_duplicate_arena(arena, name, true),
                                .device = opened.st_dev,
                                .inode = opened.st_ino,
                            };
                            if (closedir(frame->directory) != 0)
                            {
                                result = false;
                            }
                            frame->directory = 0;
                            frame = child_frame;
                        }
                    }
                    else if (os_directory_delete_unlink_at(directory, name, 0) != 0 && errno != ENOENT)
                    {
                        result = false;
                    }
                }
            }
            else
            {
                int read_error = errno;
                OsDirectoryDeleteFrame* finished = frame;
                OsDirectoryDeleteFrame* parent = finished->parent;
                int parent_descriptor = root_parent;
                if (parent)
                {
                    parent_descriptor = os_directory_delete_open_at(dirfd(finished->directory), S8(".."));
                    struct stat reopened;
                    bool same_parent = parent_descriptor >= 0 && os_directory_delete_stat(parent_descriptor, &reopened) == 0 &&
                                       reopened.st_dev == parent->device && reopened.st_ino == parent->inode;
                    parent->directory = same_parent ? fdopendir(parent_descriptor) : 0;
                    if (!parent->directory)
                    {
                        if (parent_descriptor >= 0)
                        {
                            close(parent_descriptor);
                        }
                        parent_descriptor = -1;
                        result = false;
                    }
                }
                struct stat selected_again;
                if (read_error || parent_descriptor < 0)
                {
                    result = false;
                }
                else if (os_directory_delete_stat_at(parent_descriptor, finished->name, &selected_again) == 0)
                {
                    bool same = S_ISDIR(selected_again.st_mode) && selected_again.st_dev == finished->device &&
                                selected_again.st_ino == finished->inode;
                    if (!same)
                    {
                        result = false;
                    }
                    else if (os_directory_delete_unlink_at(parent_descriptor, finished->name, AT_REMOVEDIR) != 0 &&
                             errno != ENOENT)
                    {
                        result = false;
                    }
                }
                else if (errno != ENOENT)
                {
                    result = false;
                }
                if (closedir(finished->directory) != 0)
                {
                    result = false;
                }
                frame = parent;
            }
        }
        while (frame)
        {
            if (frame->directory && closedir(frame->directory) != 0)
            {
                result = false;
            }
            frame = frame->parent;
        }
        if (close(root_parent) != 0)
        {
            result = false;
        }
    }
    return result;
}
#elif defined(_WIN32)
typedef enum OsDirectoryDeleteTaskKind
{
    OS_DIRECTORY_DELETE_ENTER,
    OS_DIRECTORY_DELETE_POST,
    OS_DIRECTORY_DELETE_ENTRY,
} OsDirectoryDeleteTaskKind;

typedef struct OsDirectoryDeleteTask OsDirectoryDeleteTask;
struct OsDirectoryDeleteTask
{
    OsDirectoryDeleteTask* next;
    String8 path;
    OsDirectoryDeleteTaskKind kind;
    u8 reserved[3];
    u32 attributes;
};

BUSTER_GLOBAL_LOCAL void os_directory_delete_task_push(Arena* arena, OsDirectoryDeleteTask** tasks, String8 path,
                                                        OsDirectoryDeleteTaskKind kind, u32 attributes)
{
    OsDirectoryDeleteTask* task = arena_allocate(arena, OsDirectoryDeleteTask, 1);
    *task = (OsDirectoryDeleteTask){
        .next = *tasks,
        .path = path,
        .kind = kind,
        .attributes = attributes,
    };
    *tasks = task;
}

BUSTER_GLOBAL_LOCAL bool os_directory_delete_walk(Arena* arena, String8 root)
{
    bool result = true;
    OsDirectoryDeleteTask* tasks = 0;
    os_directory_delete_task_push(arena, &tasks, root, OS_DIRECTORY_DELETE_ENTER, 0);
    while (tasks)
    {
        OsDirectoryDeleteTask* task = tasks;
        tasks = task->next;
        if (task->kind == OS_DIRECTORY_DELETE_POST || task->kind == OS_DIRECTORY_DELETE_ENTRY)
        {
            result = os_windows_entry_delete(arena, task->path, task->attributes) && result;
            continue;
        }

        String16 task_path_w = string16_from_string8(arena, task->path, true);
        DWORD task_attributes = GetFileAttributesW(task_path_w.pointer);
        if (task_attributes == INVALID_FILE_ATTRIBUTES)
        {
            DWORD error = GetLastError();
            result = (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) && result;
            continue;
        }
        if (task_attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            result = os_windows_entry_delete(arena, task->path, task_attributes) && result;
            continue;
        }
        if (!(task_attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            result = false;
            continue;
        }
        os_directory_delete_task_push(arena, &tasks, task->path, OS_DIRECTORY_DELETE_POST, task_attributes);
        String16 pattern = string16_from_string8(arena, string_format_z(arena, S8("{S8}\\*"), task->path), true);
        WIN32_FIND_DATAW find_data;
        HANDLE find = FindFirstFileW(pattern.pointer, &find_data);
        if (find == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            result = error == ERROR_FILE_NOT_FOUND && result;
            continue;
        }
        bool more = true;
        while (more)
        {
            String8 name = string8_from_string16(arena, os_string16_from_wide(find_data.cFileName), false);
            if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
            {
                String8 entry_path = string_format_z(arena, S8("{S8}\\{S8}"), task->path, name);
                bool is_link = (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                bool descend = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && !is_link;
                os_directory_delete_task_push(arena, &tasks, entry_path, descend ? OS_DIRECTORY_DELETE_ENTER : OS_DIRECTORY_DELETE_ENTRY,
                                              find_data.dwFileAttributes);
            }
            more = FindNextFileW(find, &find_data) != 0;
        }
        result = GetLastError() == ERROR_NO_MORE_FILES && result;
        FindClose(find);
    }
    return result;
}
#endif

bool os_directory_delete(String8 path)
{
    bool result = false;
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    if (path.length)
    {
        BUSTER_VALIDATE(!path.pointer[path.length]);
        // The walk retains its descriptor frames or pending Windows tasks
        // for the whole traversal. Own their arena so nested scratch scopes can
        // never rewind live frame names or tasks.
        Arena* arena = arena_create((ArenaCreation){0});
        result = os_directory_delete_walk(arena, path);
        arena_destroy(arena, 1);
    }
#else
    BUSTER_UNUSED(path);
#endif

    return result;
}

BUSTER_GLOBAL_LOCAL OsError os_file_invalid_error(void)
{
#if defined(_WIN32)
    return (OsError){ERROR_INVALID_PARAMETER};
#else
    return (OsError){EINVAL};
#endif
}

BUSTER_GLOBAL_LOCAL OsError os_file_zero_write_error(void)
{
#if defined(_WIN32)
    return (OsError){ERROR_WRITE_FAULT};
#else
    return (OsError){EIO};
#endif
}

#if BUSTER_INCLUDE_TESTS
typedef struct OsFileTestState OsFileTestState;
struct OsFileTestState
{
    String8 path;
    // The staging file most recently created for `path` as its destination.
    String8 staging;
    OsFileDescriptor* file;
    const OsFileTestStep* steps;
    u32 count;
    u32 next;
};
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL OsFileTestState os_file_test_state;

void os_file_test_begin(String8 path, const OsFileTestStep* steps, u32 count)
{
    BUSTER_VALIDATE(!os_file_test_state.path.length && path.length && count <= 16);
    os_file_test_state = (OsFileTestState){.path = path, .steps = steps, .count = count};
}

u32 os_file_test_end(void)
{
    u32 result = os_file_test_state.next;
    os_file_test_state = (OsFileTestState){0};
    return result;
}

BUSTER_GLOBAL_LOCAL const OsFileTestStep* os_file_test_take(OsFileTestOperation operation)
{
    const OsFileTestStep* result = 0;
    if (os_file_test_state.next < os_file_test_state.count && os_file_test_state.steps[os_file_test_state.next].operation == operation)
    {
        result = &os_file_test_state.steps[os_file_test_state.next];
        os_file_test_state.next += 1;
    }
    return result;
}

// Replacement steps name both the staging file and its destination, so either
// spelling selects them.
BUSTER_GLOBAL_LOCAL bool os_file_test_selects(String8 path)
{
    bool staging = os_file_test_state.staging.length && string_equal(path, os_file_test_state.staging);
    return os_file_test_state.path.length && (string_equal(path, os_file_test_state.path) || staging);
}

bool os_file_test_map_unavailable(String8 path)
{
    bool selected = os_file_test_state.path.length && string_equal(path, os_file_test_state.path);
    return selected && os_file_test_take(OS_FILE_TEST_MAP) != 0;
}
#endif

OsFileOpenResult os_file_open_checked(String8 path, OpenFlags flags, OpenPermissions permissions)
{
    OsFileDescriptor* result = 0;
    OsError error = {0};
#if BUSTER_INCLUDE_TESTS
    bool selected = os_file_test_state.path.length && string_equal(path, os_file_test_state.path);
    const OsFileTestStep* step = selected ? os_file_test_take(OS_FILE_TEST_OPEN) : 0;
    if (step) error.v = (u32)step->value;
#endif
    if (path.pointer && !error.v)
    {
#if defined(__linux__) || defined(__APPLE__)
        BUSTER_VALIDATE(!path.pointer[path.length]);

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
        int fd;
        do
        {
            fd = open((char*)path.pointer, o, mode);
        } while (fd < 0 && errno == EINTR);

        if (fd >= 0)
        {
            result = posix_fd_to_generic_fd(fd);
        }
        else
        {
            error = os_get_last_error();
            // Missing paths are expected while probing include and library
            // candidates. Keep diagnostics for other failures visible.
            if (program_flag_get(PROGRAM_FLAG_VERBOSE) && error.v != (u32)ENOENT && error.v != (u32)ENOTDIR)
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
        else
        {
            error = os_get_last_error();
            // Missing paths are expected while probing include and library
            // candidates. Keep diagnostics for other failures visible.
            if (program_flag_get(PROGRAM_FLAG_VERBOSE) && error.v != (u32)ERROR_FILE_NOT_FOUND && error.v != (u32)ERROR_PATH_NOT_FOUND)
            {
                string_print(S8("Error opening {S8}: {EOs}\n"), path, error);
            }
        }
        scratch_end(scratch);
#endif
    }
    if (!result && !error.v) error = os_file_invalid_error();
#if BUSTER_INCLUDE_TESTS
    if (selected) os_file_test_state.file = result;
#endif
    return (OsFileOpenResult){result, error};
}

OsFileDescriptor* os_file_open(String8 path, OpenFlags flags, OpenPermissions permissions)
{
    return os_file_open_checked(path, flags, permissions).file;
}

// Neither platform's transfer primitive takes a u64 count: WriteFile/ReadFile
// take a DWORD, and write(2)/read(2) are only defined up to SSIZE_MAX. The
// recoverable helpers below clamp to that limit and let their transfer loops
// resume, because narrowing the count instead silently corrupted every request
// of 4 GiB or more — an exactly 4 GiB one became a zero-byte request, which
// os_file_write then retried forever without making progress.
#if defined(_WIN32)
#define OS_FILE_TRANSFER_MAX ((u64)UINT32_MAX)
#else
#define OS_FILE_TRANSFER_MAX ((u64)(SIZE_MAX >> 1))
#endif

OsFileTransferResult os_file_write_checked(OsFileDescriptor* file_descriptor, ByteSlice buffer)
{
    OsFileTransferResult result = {0};
    if (buffer.length && (!file_descriptor || !buffer.pointer)) result.error = os_file_invalid_error();
    while (result.transferred < buffer.length && !result.error.v)
    {
        u64 request = BUSTER_MIN(buffer.length - result.transferred, OS_FILE_TRANSFER_MAX);
        u64 transferred = 0;
        bool interrupted = false;
#if BUSTER_INCLUDE_TESTS
        const OsFileTestStep* step = file_descriptor == os_file_test_state.file ? os_file_test_take(OS_FILE_TEST_WRITE) : 0;
        if (step && step->action == OS_FILE_TEST_LIMIT) request = BUSTER_MIN(request, step->value);
        if (step && step->action == OS_FILE_TEST_ERROR) result.error.v = (u32)step->value;
        else if (step && step->action == OS_FILE_TEST_INTERRUPT) interrupted = true;
        else if (!step || step->action != OS_FILE_TEST_ZERO)
#endif
        {
#if defined(__linux__) || defined(__APPLE__)
            ssize_t count = write(generic_fd_to_posix(file_descriptor), buffer.pointer + result.transferred, (size_t)request);
            if (count < 0)
            {
                OsError error = os_get_last_error();
                interrupted = error.v == (u32)EINTR;
                if (!interrupted) result.error = error;
            }
            else transferred = (u64)count;
#elif defined(_WIN32)
            DWORD count = 0;
            if (!WriteFile(generic_fd_to_windows(file_descriptor), buffer.pointer + result.transferred, (DWORD)request, &count, 0))
            {
                result.error = os_get_last_error();
            }
            else transferred = count;
#endif
        }
        if (!interrupted && !result.error.v)
        {
            if (!transferred) result.error = os_file_zero_write_error();
            else result.transferred += transferred;
        }
    }
    return result;
}

bool os_file_write_attempt(OsFileDescriptor* file_descriptor, ByteSlice buffer)
{
    return !os_file_write_checked(file_descriptor, buffer).error.v;
}

void os_file_write(OsFileDescriptor* file_descriptor, ByteSlice buffer)
{
    bool success = os_file_write_attempt(file_descriptor, buffer);
    BUSTER_VALIDATE(success);
}

OsFileReadResult os_file_read_some(OsFileDescriptor* file_descriptor, ByteSlice buffer)
{
    OsFileReadResult result = {0};
    if (buffer.length && (!file_descriptor || !buffer.pointer))
    {
        result.status = OS_FILE_READ_ERROR;
        result.error = os_file_invalid_error();
    }
    bool retry = buffer.length && result.status == OS_FILE_READ_OK;
    while (retry)
    {
        retry = false;
        u64 request = BUSTER_MIN(buffer.length, OS_FILE_TRANSFER_MAX);
#if BUSTER_INCLUDE_TESTS
        const OsFileTestStep* step = file_descriptor == os_file_test_state.file ? os_file_test_take(OS_FILE_TEST_READ) : 0;
        if (step && step->action == OS_FILE_TEST_LIMIT) request = BUSTER_MIN(request, step->value);
        if (step && step->action == OS_FILE_TEST_ERROR) result.error.v = (u32)step->value;
        else if (step && step->action == OS_FILE_TEST_INTERRUPT) retry = true;
        else if (!step || step->action != OS_FILE_TEST_ZERO)
#endif
        {
#if defined(__linux__) || defined(__APPLE__)
            ssize_t count = read(generic_fd_to_posix(file_descriptor), buffer.pointer, (size_t)request);
            if (count < 0)
            {
                OsError error = os_get_last_error();
                retry = error.v == (u32)EINTR;
                if (!retry) result.error = error;
            }
            else result.transferred = (u64)count;
#elif defined(_WIN32)
            DWORD count = 0;
            if (!ReadFile(generic_fd_to_windows(file_descriptor), buffer.pointer, (DWORD)request, &count, 0))
            {
                OsError error = os_get_last_error();
                // Message pipes can return a prefix with ERROR_MORE_DATA;
                // closed pipes and file EOF terminate without an OS error.
                if (error.v == ERROR_MORE_DATA && count) result.transferred = count;
                else if (error.v != ERROR_BROKEN_PIPE && error.v != ERROR_HANDLE_EOF) result.error = error;
            }
            else result.transferred = count;
#endif
        }
        if (result.error.v) result.status = OS_FILE_READ_ERROR;
        else if (!retry && !result.transferred) result.status = OS_FILE_READ_EOF;
    }
    return result;
}

OsFileReadResult os_file_read_exact(OsFileDescriptor* file_descriptor, ByteSlice buffer)
{
    OsFileReadResult result = {0};
    // Validate before pointer arithmetic, including a null nonempty buffer.
    if (buffer.length && (!file_descriptor || !buffer.pointer))
    {
        result.status = OS_FILE_READ_ERROR;
        result.error = os_file_invalid_error();
    }
    while (result.transferred < buffer.length && result.status == OS_FILE_READ_OK)
    {
        OsFileReadResult part = os_file_read_some(file_descriptor, (ByteSlice){buffer.pointer + result.transferred, buffer.length - result.transferred});
        result.transferred += part.transferred;
        result.status = part.status;
        result.error = part.error;
    }
    return result;
}

bool os_file_read_attempt(OsFileDescriptor* file_descriptor, ByteSlice buffer, u64* read_count)
{
    OsFileReadResult result = os_file_read_exact(file_descriptor, buffer);
    *read_count = result.transferred;
    return result.status != OS_FILE_READ_ERROR;
}

FileStats os_file_get_stats(OsFileDescriptor* file_descriptor, FileStatsOptions options)
{
    FileStats result = {0};
    if (!file_descriptor) result.error = os_file_invalid_error();
#if BUSTER_INCLUDE_TESTS
    const OsFileTestStep* step = file_descriptor && file_descriptor == os_file_test_state.file ? os_file_test_take(OS_FILE_TEST_STATS) : 0;
    if (step && step->action == OS_FILE_TEST_ERROR) result.error.v = (u32)step->value;
#endif
    if (!result.error.v)
    {
#if defined(__linux__) || defined(__APPLE__)
        struct stat stats;
        int status;
        do
        {
            status = fstat(generic_fd_to_posix(file_descriptor), &stats);
        } while (status < 0 && errno == EINTR);
        if (status < 0) result.error = os_get_last_error();
        else if (stats.st_size < 0) result.error = os_file_invalid_error();
        else
        {
            if (options.size) result.size = (u64)stats.st_size;
            if (options.modified_time) result.modified_time_s = (u64)stats.st_mtime;
            if (options.identity)
            {
                result.device = (u64)stats.st_dev;
                result.index = (u64)stats.st_ino;
                result.permissions = (u32)(stats.st_mode & 0777);
                if (S_ISREG(stats.st_mode))
                {
                    result.kind = OS_FILE_KIND_REGULAR;
                }
                else if (S_ISDIR(stats.st_mode))
                {
                    result.kind = OS_FILE_KIND_DIRECTORY;
                }
                else if (S_ISLNK(stats.st_mode))
                {
                    result.kind = OS_FILE_KIND_LINK;
                }
                else
                {
                    result.kind = OS_FILE_KIND_OTHER;
                }
            }
            result.valid = true;
        }
#elif defined(_WIN32)
        BY_HANDLE_FILE_INFORMATION information = {0};
        if (!GetFileInformationByHandle(generic_fd_to_windows(file_descriptor), &information)) result.error = os_get_last_error();
        else
        {
            w32_file_stats_from_file_information(&result, options, information);
            result.valid = true;
        }
#endif
    }
#if BUSTER_INCLUDE_TESTS
    // A stale size deterministically models resize between stat and read,
    // without racing a second thread or changing process-global OS state.
    if (result.valid && step && step->action == OS_FILE_TEST_SIZE) result.size = step->value;
#endif
    return result;
}

OsError os_file_flush(OsFileDescriptor* file_descriptor)
{
    OsError result = {0};
    if (!file_descriptor) result = os_file_invalid_error();
#if BUSTER_INCLUDE_TESTS
    const OsFileTestStep* step = file_descriptor && file_descriptor == os_file_test_state.file ? os_file_test_take(OS_FILE_TEST_FLUSH) : 0;
    if (step) result.v = (u32)step->value;
#endif
    if (!result.v)
    {
#if defined(__linux__) || defined(__APPLE__)
        int flushed;
        do
        {
            flushed = fsync(generic_fd_to_posix(file_descriptor));
        } while (flushed < 0 && errno == EINTR);
        if (flushed < 0) result = os_get_last_error();
#elif defined(_WIN32)
        if (!FlushFileBuffers(generic_fd_to_windows(file_descriptor))) result = os_get_last_error();
#endif
    }
    return result;
}

OsError os_file_close_checked(OsFileDescriptor* file_descriptor)
{
    OsError result = {0};
    if (!file_descriptor) result = os_file_invalid_error();
    else
    {
        // Do not retry close: POSIX may already have released this descriptor,
        // and a retry could close a different thread's newly opened file.
#if defined(__linux__) || defined(__APPLE__)
        if (close(generic_fd_to_posix(file_descriptor)) != 0) result = os_get_last_error();
#elif defined(_WIN32)
        if (!CloseHandle(generic_fd_to_windows(file_descriptor))) result = os_get_last_error();
#endif
#if BUSTER_INCLUDE_TESTS
        if (file_descriptor == os_file_test_state.file)
        {
            const OsFileTestStep* step = os_file_test_take(OS_FILE_TEST_CLOSE);
            if (step && !result.v) result.v = (u32)step->value;
            os_file_test_state.file = 0;
        }
#endif
    }
    return result;
}

bool os_file_close(OsFileDescriptor* file_descriptor)
{
    return !os_file_close_checked(file_descriptor).v;
}

OsError os_file_delete_checked(String8 path)
{
    OsError result = {0};
#if BUSTER_INCLUDE_TESTS
    const OsFileTestStep* step = os_file_test_selects(path) ? os_file_test_take(OS_FILE_TEST_DELETE) : 0;
    if (step) result.v = (u32)step->value;
#endif
    if (!result.v)
    {
#if defined(__linux__) || defined(__APPLE__)
        BUSTER_VALIDATE(!path.pointer[path.length]);
        if (unlink((const char*)path.pointer) != 0 && errno != ENOENT)
        {
            result = os_get_last_error();
        }
#elif defined(_WIN32)
        TemporalArena scratch = scratch_begin(0, 0);
        String16 path_w = string16_from_string8(scratch.arena, path, true);
        if (!DeleteFileW(path_w.pointer))
        {
            OsError error = os_get_last_error();
            if (error.v != (u32)ERROR_FILE_NOT_FOUND && error.v != (u32)ERROR_PATH_NOT_FOUND)
            {
                result = error;
            }
        }
        scratch_end(scratch);
#else
        BUSTER_UNUSED(path);
        result = os_file_invalid_error();
#endif
    }
    return result;
}

FileStats os_file_replacement_target_stats(String8 path)
{
    FileStats result = {0};
#if BUSTER_INCLUDE_TESTS
    const OsFileTestStep* step = os_file_test_selects(path) ? os_file_test_take(OS_FILE_TEST_STATS) : 0;
    if (step && step->action == OS_FILE_TEST_ERROR) result.error.v = (u32)step->value;
#endif
    if (!path.pointer || !path.length)
    {
        result.error = os_file_invalid_error();
    }
    else if (!result.error.v)
    {
#if defined(__linux__) || defined(__APPLE__)
        BUSTER_VALIDATE(!path.pointer[path.length]);
        // O_NONBLOCK keeps a FIFO without a reader from blocking, and O_NOCTTY
        // keeps a terminal from becoming the controlling terminal.
        int fd;
        do
        {
            fd = open((char*)path.pointer, O_WRONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY);
        } while (fd < 0 && errno == EINTR);
        if (fd >= 0)
        {
            OsFileDescriptor* file = posix_fd_to_generic_fd(fd);
            result = os_file_get_stats(file, (FileStatsOptions){.identity = 1});
            OsError close_error = os_file_close_checked(file);
            if (result.valid && close_error.v)
            {
                result = (FileStats){.error = close_error};
            }
        }
        else
        {
            OsError error = os_get_last_error();
            result.valid = true;
            if (error.v == (u32)ENOENT)
            {
                result.kind = OS_FILE_KIND_MISSING;
            }
            else if (error.v == (u32)ELOOP)
            {
                result.kind = OS_FILE_KIND_LINK;
            }
            else if (error.v == (u32)EISDIR)
            {
                result.kind = OS_FILE_KIND_DIRECTORY;
            }
            else if (error.v == (u32)ENXIO)
            {
                result.kind = OS_FILE_KIND_OTHER;
            }
            else
            {
                result.valid = false;
                result.error = error;
            }
        }
#elif defined(_WIN32)
        TemporalArena scratch = scratch_begin(0, 0);
        String16 path_w = string16_from_string8(scratch.arena, path, true);
        // Attribute-only access never conflicts with other handles' share
        // modes; the reparse flag inspects a link rather than its target.
        HANDLE handle = CreateFileW(path_w.pointer, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, 0);
        if (handle != INVALID_HANDLE_VALUE)
        {
            OsFileDescriptor* file = (OsFileDescriptor*)handle;
            result = os_file_get_stats(file, (FileStatsOptions){.identity = 1});
            OsError close_error = os_file_close_checked(file);
            if (result.valid && close_error.v)
            {
                result = (FileStats){.error = close_error};
            }
        }
        else
        {
            OsError error = os_get_last_error();
            result.valid = error.v == (u32)ERROR_FILE_NOT_FOUND || error.v == (u32)ERROR_PATH_NOT_FOUND;
            if (!result.valid)
            {
                result.error = error;
            }
        }
        scratch_end(scratch);
#else
        result.error = os_file_invalid_error();
#endif
    }
    return result;
}

// Collisions come only from leftovers of an earlier process with the same id
// or from foreign files, so a short bounded search suffices.
#define OS_FILE_STAGING_ATTEMPTS 64
BUSTER_GLOBAL_LOCAL AtomicU64 os_file_staging_counter;

OsFileStagingResult os_file_staging_create(Arena* arena, String8 destination, OpenPermissions permissions)
{
    OsFileStagingResult result = {0};
    // The staging name replaces only the final component, keeping the rename
    // within one directory without lengthening the destination's name.
    u64 directory_length = destination.pointer ? destination.length : 0;
    bool separator = false;
    while (directory_length && !separator)
    {
        char8 character = destination.pointer[directory_length - 1];
#if defined(_WIN32)
        separator = character == '/' || character == '\\' || character == ':';
#else
        separator = character == '/';
#endif
        if (!separator)
        {
            directory_length -= 1;
        }
    }
#if BUSTER_INCLUDE_TESTS
    bool selected = os_file_test_state.path.length && string_equal(destination, os_file_test_state.path);
    const OsFileTestStep* step = selected ? os_file_test_take(OS_FILE_TEST_OPEN) : 0;
    if (step) result.error.v = (u32)step->value;
#endif
    if (!result.error.v && (!destination.pointer || destination.length == directory_length))
    {
        result.error = os_file_invalid_error();
    }

    String8 directory = {.pointer = destination.pointer, .length = directory_length};
    u64 mark = arena->position;
    for (u32 attempt = 0; attempt < OS_FILE_STAGING_ATTEMPTS && !result.file && !result.error.v; attempt += 1)
    {
        arena_set_position(arena, mark);
        u64 serial = atomic_u64_increment(&os_file_staging_counter);
        String8 path = string_format_z(arena, S8("{S8}{S8}{u64}-{u64}{S8}"), directory, OS_FILE_STAGING_PREFIX, os_get_current_process_id(), serial,
                                       OS_FILE_STAGING_SUFFIX);
        OsError error;
#if defined(__linux__) || defined(__APPLE__)
        mode_t mode = permissions.execute ? 0755 : 0644;
        int fd;
        do
        {
            fd = open((char*)path.pointer, O_WRONLY | O_CREAT | O_EXCL, mode);
        } while (fd < 0 && errno == EINTR);
        error = fd >= 0 ? (OsError){0} : os_get_last_error();
        if (fd >= 0)
        {
            result.file = posix_fd_to_generic_fd(fd);
        }
        bool collision = error.v == (u32)EEXIST;
#elif defined(_WIN32)
        String16 path_w = string16_from_string8(arena, path, true);
        DWORD shared_mode = 0;
        if (permissions.read)
        {
            shared_mode |= FILE_SHARE_READ;
        }
        if (permissions.write)
        {
            shared_mode |= FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        }
        SECURITY_ATTRIBUTES security_attributes = {sizeof(security_attributes), 0, 0};
        HANDLE handle = CreateFileW(path_w.pointer, GENERIC_WRITE, shared_mode, &security_attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
        error = handle != INVALID_HANDLE_VALUE ? (OsError){0} : os_get_last_error();
        if (handle != INVALID_HANDLE_VALUE)
        {
            result.file = (OsFileDescriptor*)handle;
        }
        bool collision = error.v == (u32)ERROR_FILE_EXISTS || error.v == (u32)ERROR_ALREADY_EXISTS;
#else
        BUSTER_UNUSED(permissions);
        error = os_file_invalid_error();
        bool collision = false;
#endif
        if (result.file)
        {
            result.path = path;
        }
        else if (!collision || attempt + 1 == OS_FILE_STAGING_ATTEMPTS)
        {
            result.error = error;
        }
    }
    if (!result.file)
    {
        arena_set_position(arena, mark);
    }
#if BUSTER_INCLUDE_TESTS
    if (selected)
    {
        os_file_test_state.file = result.file;
        os_file_test_state.staging = result.path;
    }
#endif
    return result;
}

OsError os_file_replace(String8 path, String8 destination)
{
    OsError result = {0};
    if (!path.pointer || !path.length || !destination.pointer || !destination.length)
    {
        result = os_file_invalid_error();
    }
#if BUSTER_INCLUDE_TESTS
    const OsFileTestStep* step = !result.v && (os_file_test_selects(path) || os_file_test_selects(destination)) ? os_file_test_take(OS_FILE_TEST_REPLACE) : 0;
    if (step) result.v = (u32)step->value;
#endif
    if (!result.v)
    {
#if defined(__linux__) || defined(__APPLE__)
        BUSTER_VALIDATE(!path.pointer[path.length] && !destination.pointer[destination.length]);
        if (rename((const char*)path.pointer, (const char*)destination.pointer) != 0)
        {
            result = os_get_last_error();
        }
#elif defined(_WIN32)
        TemporalArena scratch = scratch_begin(0, 0);
        String16 path_w = string16_from_string8(scratch.arena, path, true);
        String8 absolute_destination = os_path_absolute_lexical(scratch.arena, destination, true);
        String16 destination_w = string16_from_string8(scratch.arena, absolute_destination, true);
        u64 rename_bytes = sizeof(FILE_RENAME_INFO) + destination_w.length * sizeof(WindowsChar);
        if (!absolute_destination.length || rename_bytes > UINT32_MAX)
        {
            result = os_file_invalid_error();
        }
        else
        {
            HANDLE handle = CreateFileW(path_w.pointer, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
            if (handle == INVALID_HANDLE_VALUE)
            {
                result = os_get_last_error();
            }
            else
            {
                FILE_RENAME_INFO* rename_info = (FILE_RENAME_INFO*)arena_allocate_bytes(scratch.arena, rename_bytes, BUSTER_ALIGN_OF(FILE_RENAME_INFO));
                memset(rename_info, 0, (size_t)rename_bytes);
                rename_info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
                rename_info->FileNameLength = (DWORD)(destination_w.length * sizeof(WindowsChar));
                memcpy(rename_info->FileName, destination_w.pointer, rename_info->FileNameLength);
                // Existing readers retain the old object while new opens see
                // the replacement. MoveFileExW alone cannot provide this when
                // a destination handle is still open, even with delete sharing.
                if (!SetFileInformationByHandle(handle, BUSTER_WINDOWS_FILE_RENAME_INFO_EX, rename_info, (DWORD)rename_bytes))
                {
                    result = os_get_last_error();
                }
                // A completed rename cannot be reported as unpublished because
                // of closing this private, attribute-free handle.
                CloseHandle(handle);
            }
        }
        scratch_end(scratch);
#else
        result = os_file_invalid_error();
#endif
    }
    return result;
}

#if !defined(_WIN32)
OsError os_file_set_permissions(OsFileDescriptor* file_descriptor, u32 permissions)
{
    OsError result = {0};
    if (!file_descriptor || (permissions & ~(u32)0777))
    {
        result = os_file_invalid_error();
    }
#if BUSTER_INCLUDE_TESTS
    const OsFileTestStep* step = !result.v && file_descriptor == os_file_test_state.file ? os_file_test_take(OS_FILE_TEST_PERMISSIONS) : 0;
    if (step) result.v = (u32)step->value;
#endif
    if (!result.v)
    {
#if defined(__linux__) || defined(__APPLE__)
        int status;
        do
        {
            status = fchmod(generic_fd_to_posix(file_descriptor), (mode_t)permissions);
        } while (status < 0 && errno == EINTR);
        if (status < 0)
        {
            result = os_get_last_error();
        }
#else
        result = os_file_invalid_error();
#endif
    }
    return result;
}
#endif

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

#if defined(_WIN32)
bool os_windows_pipe_disable_inheritance(OsFileDescriptor* pipe)
{
    return SetHandleInformation((HANDLE)pipe, HANDLE_FLAG_INHERIT, 0) != 0;
}

BUSTER_GLOBAL_LOCAL bool os_windows_job_wait_empty(HANDLE job, u64 timeout_microseconds)
{
    bool result = false;
    u64 deadline = os_now_microseconds() + timeout_microseconds;
    bool query_valid = true;
    while (query_valid && !result && os_now_microseconds() < deadline)
    {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information = {0};
        query_valid = QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &information, sizeof(information), 0) != 0;
        result = query_valid && information.ActiveProcesses == 0;
        if (query_valid && !result)
        {
            Sleep(1);
        }
    }
    return result;
}
#endif

#if BUSTER_INCLUDE_TESTS
typedef struct OsProcessSpawnTestState OsProcessSpawnTestState;
struct OsProcessSpawnTestState
{
    u64 call_count;
    u64 fail_call;
    OsProcessSpawnTestOperation operation;
    bool armed;
    bool failed;
};

BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL OsProcessSpawnTestState os_process_spawn_test_state;

void os_process_spawn_test_fail_on_call(OsProcessSpawnTestOperation operation, u64 call_index)
{
    BUSTER_VALIDATE(operation < OS_PROCESS_SPAWN_TEST_OPERATION_COUNT);
    os_process_spawn_test_state = (OsProcessSpawnTestState){
        .fail_call = call_index,
        .operation = operation,
        .armed = true,
    };
}

bool os_process_spawn_test_end(void)
{
    bool result = os_process_spawn_test_state.armed && os_process_spawn_test_state.failed;
    os_process_spawn_test_state = (OsProcessSpawnTestState){0};
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool os_process_spawn_test_should_fail(OsProcessSpawnTestOperation operation)
{
    bool result = false;
    if (os_process_spawn_test_state.armed && os_process_spawn_test_state.operation == operation)
    {
        u64 call = os_process_spawn_test_state.call_count;
        os_process_spawn_test_state.call_count += 1;
        if (call == os_process_spawn_test_state.fail_call)
        {
            os_process_spawn_test_state.failed = true;
            result = true;
        }
    }
    return result;
}

u64 os_process_spawn_test_resource_count(void)
{
    u64 result = 0;
#if defined(_WIN32)
    DWORD count = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &count))
    {
        result = count;
    }
#else
#if defined(__linux__)
    char const* directory_path = "/proc/self/fd";
#else
    char const* directory_path = "/dev/fd";
#endif
    DIR* directory = opendir(directory_path);
    if (directory)
    {
        struct dirent* entry;
        while ((entry = readdir(directory)) != 0)
        {
            result += entry->d_name[0] != '.';
        }
        closedir(directory);
        // The directory used for the census is itself visible in fd directories.
        result -= result != 0;
    }
#endif
    return result;
}
#define OS_PROCESS_SPAWN_TEST_FAIL(operation) os_process_spawn_test_should_fail(operation)
#else
#define OS_PROCESS_SPAWN_TEST_FAIL(operation) false
#endif

BUSTER_GLOBAL_LOCAL void os_process_spawn_fail(ProcessSpawnResult* result, ProcessSpawnFailure failure, OsError error)
{
    if (result->failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        result->failure = failure;
        result->error = error;
    }
}

BUSTER_GLOBAL_LOCAL OsError os_process_spawn_invalid_error(void)
{
#if defined(_WIN32)
    return (OsError){ERROR_INVALID_PARAMETER};
#else
    return (OsError){EINVAL};
#endif
}

BUSTER_GLOBAL_LOCAL OsError os_process_spawn_not_found_error(void)
{
#if defined(_WIN32)
    return (OsError){ERROR_FILE_NOT_FOUND};
#else
    return (OsError){ENOENT};
#endif
}

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL OsError os_process_spawn_injected_error(void)
{
    return (OsError){ERROR_GEN_FAILURE};
}
#endif

#if BUSTER_ANDROID
BUSTER_GLOBAL_LOCAL OsError os_process_spawn_unsupported_error(void)
{
    return (OsError){ENOSYS};
}
#endif

BUSTER_GLOBAL_LOCAL bool os_process_spawn_string_valid(String8 string, bool allow_empty)
{
    bool result = (string.pointer != 0 || string.length == 0) && (allow_empty || string.length != 0);
    for (u64 index = 0; result && index < string.length; index += 1)
    {
        result = string.pointer[index] != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_process_spawn_inputs_valid(SliceString8 arguments, SliceString8 environment_keys, SliceString8 environment_values,
                                                        ProcessSpawnOptions options, ProcessSpawnFailure* failure)
{
    bool result = arguments.length && arguments.pointer && os_process_spawn_string_valid(arguments.pointer[0], false);
    for (u64 index = 1; result && index < arguments.length; index += 1)
    {
        result = os_process_spawn_string_valid(arguments.pointer[index], true);
    }
    if (!result)
    {
        *failure = PROCESS_SPAWN_FAILURE_INVALID_ARGUMENTS;
    }
    else
    {
        result = environment_keys.length == environment_values.length &&
                 (!environment_keys.length || (environment_keys.pointer && environment_values.pointer)) &&
                 (!options.use_process_environment || !environment_keys.length);
        for (u64 index = 0; result && index < environment_keys.length; index += 1)
        {
            String8 key = environment_keys.pointer[index];
            String8 value = environment_values.pointer[index];
            result = os_process_spawn_string_valid(key, false) && os_process_spawn_string_valid(value, true);
            for (u64 code_unit = 0; result && code_unit < key.length; code_unit += 1)
            {
                result = key.pointer[code_unit] != '=';
            }
        }
        if (!result)
        {
            *failure = PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_process_spawn_path_is_explicit(String8 path)
{
    bool result = false;
    for (u64 index = 0; !result && index < path.length; index += 1)
    {
#if defined(_WIN32)
        result = path.pointer[index] == '/' || path.pointer[index] == '\\' || path.pointer[index] == ':';
#else
        result = path.pointer[index] == '/';
#endif
    }
    return result;
}

#if !defined(_WIN32) && !BUSTER_ANDROID
#if !defined(__linux__) || !defined(F_DUPFD_CLOEXEC)
BUSTER_GLOBAL_LOCAL int os_process_spawn_set_cloexec(int descriptor)
{
    int result = 0;
    int flags;
    do
    {
        flags = fcntl(descriptor, F_GETFD);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0)
    {
        result = errno;
    }
    else
    {
        int status;
        do
        {
            status = fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
        } while (status < 0 && errno == EINTR);
        if (status < 0)
        {
            result = errno;
        }
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL int os_process_spawn_duplicate_above_standard(int descriptor)
{
    int result;
#ifdef F_DUPFD_CLOEXEC
    do
    {
        result = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    } while (result < 0 && errno == EINTR);
#else
    do
    {
        result = fcntl(descriptor, F_DUPFD, STDERR_FILENO + 1);
    } while (result < 0 && errno == EINTR);
    if (result >= 0)
    {
        int error = os_process_spawn_set_cloexec(result);
        if (error)
        {
            close(result);
            errno = error;
            result = -1;
        }
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL int os_process_spawn_pipe_create(int descriptors[2], ProcessSpawnFailure* failure)
{
    descriptors[0] = -1;
    descriptors[1] = -1;
    *failure = PROCESS_SPAWN_FAILURE_PIPE;
    int result = 0;
#if defined(__linux__)
    // Create the descriptors with close-on-exec atomically. A pipe() followed
    // by F_SETFD has a window in which another thread can spawn and inherit
    // either endpoint.
    if (pipe2(descriptors, O_CLOEXEC) != 0)
#else
    if (pipe(descriptors) != 0)
#endif
    {
        result = errno;
    }
    if (!result)
    {
        *failure = PROCESS_SPAWN_FAILURE_PIPE_CONFIGURATION;
        if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_PIPE_CONFIGURATION))
        {
            result = EIO;
        }
    }
    for (u32 index = 0; !result && index < 2; index += 1)
    {
        if (descriptors[index] <= STDERR_FILENO)
        {
            int replacement = os_process_spawn_duplicate_above_standard(descriptors[index]);
            if (replacement < 0)
            {
                result = errno;
            }
            else
            {
                close(descriptors[index]);
                descriptors[index] = replacement;
            }
        }
#if !defined(__linux__)
        else
        {
            result = os_process_spawn_set_cloexec(descriptors[index]);
        }
#endif
    }
    if (result)
    {
        for (u32 index = 0; index < 2; index += 1)
        {
            if (descriptors[index] >= 0)
            {
                close(descriptors[index]);
                descriptors[index] = -1;
            }
        }
    }
    return result;
}

#if !defined(__APPLE__) && !defined(__GLIBC__)
BUSTER_GLOBAL_LOCAL int os_process_spawn_add_open_descriptor_closes(posix_spawn_file_actions_t* file_actions)
{
    int result = 0;
#if defined(__linux__)
    char const* directory_path = "/proc/self/fd";
#else
    char const* directory_path = "/dev/fd";
#endif
    DIR* directory = opendir(directory_path);
    if (!directory)
    {
        result = errno;
    }
    else
    {
        int directory_descriptor = dirfd(directory);
        struct dirent* entry;
        while (!result && (entry = readdir(directory)) != 0)
        {
            u64 descriptor = 0;
            bool numeric = entry->d_name[0] != 0;
            for (u64 index = 0; numeric && entry->d_name[index]; index += 1)
            {
                numeric = entry->d_name[index] >= '0' && entry->d_name[index] <= '9';
                if (numeric)
                {
                    descriptor = descriptor * 10 + (u64)(entry->d_name[index] - '0');
                    numeric = descriptor <= INT_MAX;
                }
            }
            if (numeric && descriptor > STDERR_FILENO && (int)descriptor != directory_descriptor)
            {
                if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_FILE_ACTION))
                {
                    result = EIO;
                }
                else
                {
                    result = posix_spawn_file_actions_addclose(file_actions, (int)descriptor);
                }
            }
        }
        closedir(directory);
    }
    return result;
}
#endif
#endif

ProcessSpawnResult os_process_spawn(SliceString8 arguments, SliceString8 environment_keys, SliceString8 environment_values, ProcessSpawnOptions options)
{
    TemporalArena temp = scratch_begin(0, 0);
    ProcessSpawnResult result = {0};
    ProcessSpawnFailure validation_failure = PROCESS_SPAWN_FAILURE_NONE;
    bool inputs_valid = os_process_spawn_inputs_valid(arguments, environment_keys, environment_values, options, &validation_failure);
    String8 executable = {0};

    result.capture_limits = options.capture_limits;
    result.capture_overflow_policy = options.capture_overflow_policy;
    memcpy(result.capture_overflow_files, options.capture_overflow_files, sizeof(result.capture_overflow_files));
    if (options.capture_overflow_policy >= PROCESS_CAPTURE_OVERFLOW_COUNT)
    {
        os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_INVALID_ARGUMENTS, os_process_spawn_invalid_error());
    }
    else if (!inputs_valid)
    {
        os_process_spawn_fail(&result, validation_failure, os_process_spawn_invalid_error());
    }
    else
    {
        String8 requested = arguments.pointer[0];
        if (options.search_path && !os_process_spawn_path_is_explicit(requested))
        {
            requested = executable_resolve_in_path(temp.arena, requested);
            if (!requested.length)
            {
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_EXECUTABLE_LOOKUP, os_process_spawn_not_found_error());
            }
        }
        if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
        {
            executable = os_path_absolute_lexical(temp.arena, requested, true);
            if (!executable.length)
            {
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_EXECUTABLE_LOOKUP, os_process_spawn_not_found_error());
            }
        }
    }

#if defined(_WIN32)
    HANDLE duplicated_standard[STANDARD_STREAM_COUNT] = {0};
    HANDLE inherited_handles[STANDARD_STREAM_COUNT] = {0};
    u32 inherited_handle_count = 0;
    bool pipe_created[STANDARD_STREAM_COUNT] = {0};
    LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = 0;
    bool attribute_list_ready = false;
    PROCESS_INFORMATION process_information = {0};

    for (StandardStream stream = 0; result.failure == PROCESS_SPAWN_FAILURE_NONE && stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream))
        {
            SECURITY_ATTRIBUTES attributes = {sizeof(attributes), 0, TRUE};
            if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_PIPE))
            {
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_PIPE, os_process_spawn_injected_error());
            }
            else
            {
                BUSTER_CT_CHECK(sizeof(HANDLE) == sizeof(OsFileDescriptor*));
                BOOL created = CreatePipe((PHANDLE)&result.pipes[stream][0], (PHANDLE)&result.pipes[stream][1], &attributes, 0);
                if (!created)
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_PIPE, os_get_last_error());
                }
                else
                {
                    pipe_created[stream] = true;
                    u32 parent_side = stream == STANDARD_STREAM_INPUT ? 1 : 0;
                    if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_PIPE_CONFIGURATION))
                    {
                        os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_PIPE_CONFIGURATION, os_process_spawn_injected_error());
                    }
                    else if (!os_windows_pipe_disable_inheritance(result.pipes[stream][parent_side]))
                    {
                        os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_PIPE_CONFIGURATION, os_get_last_error());
                    }
                }
            }
        }
    }

    STARTUPINFOEXW startup_info = {0};
    startup_info.StartupInfo.cb = sizeof(startup_info);
    startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    DWORD standard_ids[STANDARD_STREAM_COUNT] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (StandardStream stream = 0; result.failure == PROCESS_SPAWN_FAILURE_NONE && stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        HANDLE child_handle = 0;
        if (options.capture & ((u64)1 << stream))
        {
            u32 child_side = stream == STANDARD_STREAM_INPUT ? 0 : 1;
            child_handle = (HANDLE)result.pipes[stream][child_side];
        }
        else
        {
            HANDLE source = GetStdHandle(standard_ids[stream]);
            if (source && source != INVALID_HANDLE_VALUE)
            {
                if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_HANDLE_DUPLICATION))
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_HANDLE_DUPLICATION, os_process_spawn_injected_error());
                }
                else if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicated_standard[stream], 0, TRUE, DUPLICATE_SAME_ACCESS))
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_HANDLE_DUPLICATION, os_get_last_error());
                }
                else
                {
                    child_handle = duplicated_standard[stream];
                }
            }
        }
        if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
        {
            if (stream == STANDARD_STREAM_INPUT) startup_info.StartupInfo.hStdInput = child_handle;
            if (stream == STANDARD_STREAM_OUTPUT) startup_info.StartupInfo.hStdOutput = child_handle;
            if (stream == STANDARD_STREAM_ERROR) startup_info.StartupInfo.hStdError = child_handle;
            if (child_handle)
            {
                inherited_handles[inherited_handle_count++] = child_handle;
            }
        }
    }

    if (result.failure == PROCESS_SPAWN_FAILURE_NONE && options.new_process_group)
    {
        HANDLE job = CreateJobObjectW(0, 0);
        if (!job)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_ATTRIBUTE, os_get_last_error());
        }
        else
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            {
                OsError error = os_get_last_error();
                CloseHandle(job);
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_ATTRIBUTE, error);
            }
            else
            {
                result.process_tree = (OsProcessHandle*)job;
            }
        }
    }

    if (result.failure == PROCESS_SPAWN_FAILURE_NONE && inherited_handle_count)
    {
        SIZE_T attribute_bytes = 0;
        (void)InitializeProcThreadAttributeList(0, 1, 0, &attribute_bytes);
        if (!attribute_bytes)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_HANDLE_LIST, os_get_last_error());
        }
        else
        {
            u64 word_count = (attribute_bytes + sizeof(u64) - 1) / sizeof(u64);
            attribute_list = (LPPROC_THREAD_ATTRIBUTE_LIST)arena_allocate(temp.arena, u64, word_count);
            if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_HANDLE_LIST))
            {
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_HANDLE_LIST, os_process_spawn_injected_error());
            }
            else if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_bytes))
            {
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_HANDLE_LIST, os_get_last_error());
            }
            else
            {
                attribute_list_ready = true;
                startup_info.lpAttributeList = attribute_list;
                if (!UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles,
                                               inherited_handle_count * sizeof(inherited_handles[0]), 0, 0))
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_HANDLE_LIST, os_get_last_error());
                }
            }
        }
    }

    if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        String16 application = string16_from_string8(temp.arena, executable, true);
        WindowsStringList command_line = windows_string_list_from_slice_string(temp.arena, arguments);
        WindowsStringList environment = options.use_process_environment
                                            ? program_state->input.raw_environment
                                            : windows_environment_from_keys_and_values(temp.arena, environment_keys, environment_values);
        DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT;
        if (attribute_list_ready)
        {
            creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
        }
        if (result.process_tree)
        {
            creation_flags |= CREATE_SUSPENDED;
        }
        BOOL created = FALSE;
        if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_SPAWN))
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_SPAWN, os_process_spawn_injected_error());
        }
        else
        {
            created = CreateProcessW(application.pointer, command_line, 0, 0, inherited_handle_count != 0, creation_flags, environment, 0,
                                     &startup_info.StartupInfo, &process_information);
            if (!created)
            {
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_SPAWN, os_get_last_error());
            }
        }
        if (created)
        {
            bool child_ready = true;
            if (result.process_tree)
            {
                if (!AssignProcessToJobObject((HANDLE)result.process_tree, process_information.hProcess))
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_SPAWN, os_get_last_error());
                    child_ready = false;
                }
                else if (ResumeThread(process_information.hThread) == (DWORD)-1)
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_SPAWN, os_get_last_error());
                    child_ready = false;
                }
            }
            if (child_ready)
            {
                result.handle = (OsProcessHandle*)process_information.hProcess;
            }
            else
            {
                if (result.process_tree)
                {
                    (void)TerminateJobObject((HANDLE)result.process_tree, 1);
                }
                (void)TerminateProcess(process_information.hProcess, 1);
                (void)WaitForSingleObject(process_information.hProcess, INFINITE);
                CloseHandle(process_information.hProcess);
            }
            CloseHandle(process_information.hThread);
        }
    }
    if (attribute_list_ready)
    {
        DeleteProcThreadAttributeList(attribute_list);
    }
    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (duplicated_standard[stream])
        {
            CloseHandle(duplicated_standard[stream]);
        }
        if (pipe_created[stream])
        {
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
    if (!result.handle && result.process_tree)
    {
        CloseHandle((HANDLE)result.process_tree);
        result.process_tree = 0;
    }
    result.process_group = result.handle != 0 && result.process_tree != 0;
#elif BUSTER_ANDROID
    if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_UNSUPPORTED, os_process_spawn_unsupported_error());
    }
#else
    pid_t pid = -1;
    int pipes[STANDARD_STREAM_COUNT][2];
    bool pipe_created[STANDARD_STREAM_COUNT] = {0};
    for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        pipes[stream][0] = -1;
        pipes[stream][1] = -1;
    }

    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attributes;
    bool file_actions_ready = false;
    bool attributes_ready = false;
    if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        int status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_FILE_ACTIONS_INIT) ? EIO : posix_spawn_file_actions_init(&file_actions);
        if (status)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_FILE_ACTIONS_INIT, (OsError){(u32)status});
        }
        else
        {
            file_actions_ready = true;
        }
    }
    if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        int status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_ATTRIBUTES_INIT) ? EIO : posix_spawnattr_init(&attributes);
        if (status)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_ATTRIBUTES_INIT, (OsError){(u32)status});
        }
        else
        {
            attributes_ready = true;
        }
    }

    for (StandardStream stream = 0; result.failure == PROCESS_SPAWN_FAILURE_NONE && stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (options.capture & ((u64)1 << stream))
        {
            int status;
            if (OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_PIPE))
            {
                status = EIO;
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_PIPE, (OsError){(u32)status});
            }
            else
            {
                ProcessSpawnFailure pipe_failure = PROCESS_SPAWN_FAILURE_PIPE;
                status = os_process_spawn_pipe_create(pipes[stream], &pipe_failure);
                if (status)
                {
                    os_process_spawn_fail(&result, pipe_failure, (OsError){(u32)status});
                }
                else
                {
                    pipe_created[stream] = true;
                }
            }
            if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
            {
                bool input = stream == STANDARD_STREAM_INPUT;
                int child_end = input ? pipes[stream][0] : pipes[stream][1];
                int action_status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_FILE_ACTION)
                                        ? EIO
                                        : posix_spawn_file_actions_adddup2(&file_actions, child_end, (int)stream);
                if (action_status)
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_FILE_ACTION, (OsError){(u32)action_status});
                }
            }
        }
    }

    short attribute_flags = 0;
#if defined(__APPLE__)
    attribute_flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
    for (StandardStream stream = 0; result.failure == PROCESS_SPAWN_FAILURE_NONE && stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (!(options.capture & ((u64)1 << stream)))
        {
            errno = 0;
            int descriptor_flags = fcntl((int)stream, F_GETFD);
            if (descriptor_flags >= 0)
            {
                int status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_FILE_ACTION)
                                 ? EIO
                                 : posix_spawn_file_actions_addinherit_np(&file_actions, (int)stream);
                if (status)
                {
                    os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_FILE_ACTION, (OsError){(u32)status});
                }
            }
            else if (errno != EBADF)
            {
                os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_PIPE_CONFIGURATION, (OsError){(u32)errno});
            }
        }
    }
#elif defined(__GLIBC__)
    if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        int status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_FILE_ACTION)
                         ? EIO
                         : posix_spawn_file_actions_addclosefrom_np(&file_actions, STDERR_FILENO + 1);
        if (status)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_FILE_ACTION, (OsError){(u32)status});
        }
    }
#else
    if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        int status = os_process_spawn_add_open_descriptor_closes(&file_actions);
        if (status)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_FILE_ACTION, (OsError){(u32)status});
        }
    }
#endif

    if (result.failure == PROCESS_SPAWN_FAILURE_NONE && options.new_process_group)
    {
        int status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_ATTRIBUTE) ? EIO : posix_spawnattr_setpgroup(&attributes, 0);
        if (status)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_ATTRIBUTE, (OsError){(u32)status});
        }
        else
        {
            attribute_flags |= POSIX_SPAWN_SETPGROUP;
        }
    }
    if (result.failure == PROCESS_SPAWN_FAILURE_NONE && attribute_flags)
    {
        int status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_ATTRIBUTE)
                         ? EIO
                         : posix_spawnattr_setflags(&attributes, attribute_flags);
        if (status)
        {
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_ATTRIBUTE, (OsError){(u32)status});
        }
    }

    if (result.failure == PROCESS_SPAWN_FAILURE_NONE)
    {
        PosixStringList argv = slice_string8_to_null_terminated_array_char(temp.arena, arguments);
        PosixStringList envp = options.use_process_environment
                                  ? program_state->input.raw_environment
                                  : posix_environment_from_keys_and_values(temp.arena, environment_keys, environment_values);
        int status = OS_PROCESS_SPAWN_TEST_FAIL(OS_PROCESS_SPAWN_TEST_SPAWN)
                         ? EIO
                         : posix_spawn(&pid, executable.pointer, &file_actions, &attributes, argv, envp);
        if (status)
        {
            pid = -1;
            os_process_spawn_fail(&result, PROCESS_SPAWN_FAILURE_SPAWN, (OsError){(u32)status});
        }
    }

    for (StandardStream stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
    {
        if (pipe_created[stream])
        {
            bool input = stream == STANDARD_STREAM_INPUT;
            u32 child_side = input ? 0 : 1;
            u32 parent_side = input ? 1 : 0;
            close(pipes[stream][child_side]);
            pipes[stream][child_side] = -1;
            if (pid == -1)
            {
                close(pipes[stream][parent_side]);
                pipes[stream][parent_side] = -1;
            }
        }
        for (u32 side = 0; side < 2; side += 1)
        {
            result.pipes[stream][side] = pipes[stream][side] >= 0 ? posix_fd_to_generic_fd(pipes[stream][side]) : 0;
        }
    }
    if (attributes_ready)
    {
        posix_spawnattr_destroy(&attributes);
    }
    if (file_actions_ready)
    {
        posix_spawn_file_actions_destroy(&file_actions);
    }
    if (pid != -1)
    {
        result.handle = (OsProcessHandle*)(u64)pid;
        result.process_group = options.new_process_group;
    }
#endif

    if (program_flag_get(PROGRAM_FLAG_VERBOSE))
    {
        if (result.handle)
        {
            string_print(S8("Launched [{u64}]: \"{[]S8}\"\n"), result.handle, arguments);
        }
        else
        {
            SliceString8 printable_arguments = arguments.pointer ? arguments : (SliceString8){0};
            string_print(S8("Failed to launch stage={u32} error=\"{EOs}\": \"{[]S8}\"\n"), (u32)result.failure, result.error,
                         printable_arguments);
        }
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

BUSTER_GLOBAL_LOCAL u64 pipe_capture_counter_add(u64 value, u64 addend)
{
    return value > UINT64_MAX - addend ? UINT64_MAX : value + addend;
}

BUSTER_GLOBAL_LOCAL void pipe_capture_add_count(u64* stream_count, u64* total_count, u64 addend)
{
    *stream_count = pipe_capture_counter_add(*stream_count, addend);
    *total_count = pipe_capture_counter_add(*total_count, addend);
}

BUSTER_GLOBAL_LOCAL u64 pipe_capture_limit(u64 configured, u64 fallback)
{
    return configured ? configured : fallback;
}

BUSTER_GLOBAL_LOCAL void pipe_capture_append(Arena* arena, PipeCapture* capture, ProcessSpawnResult spawn, ProcessWaitResult* result, StandardStream stream,
                                             u8* data, u64 length)
{
    u64 per_stream_limit = pipe_capture_limit(spawn.capture_limits.per_stream[stream], PROCESS_CAPTURE_DEFAULT_PER_STREAM_BYTES);
    u64 total_limit = pipe_capture_limit(spawn.capture_limits.total, PROCESS_CAPTURE_DEFAULT_TOTAL_BYTES);
    u64 per_stream_remaining = result->captured_bytes[stream] < per_stream_limit ? per_stream_limit - result->captured_bytes[stream] : 0;
    u64 total_remaining = result->captured_total < total_limit ? total_limit - result->captured_total : 0;
    u64 retained = length < per_stream_remaining ? length : per_stream_remaining;
    if (retained > total_remaining)
    {
        retained = total_remaining;
    }

    pipe_capture_add_count(result->observed_bytes + stream, &result->observed_total, length);
    if (retained)
    {
        PipeChunk* chunk = arena_allocate(arena, PipeChunk, 1);
        chunk->next = 0;
        chunk->length = retained;
        chunk->data = arena_allocate(arena, u8, retained);
        memcpy(chunk->data, data, retained);

        if (capture->last)
        {
            capture->last->next = chunk;
        }
        else
        {
            capture->first = chunk;
        }
        capture->last = chunk;
        capture->total_length += retained;
        pipe_capture_add_count(result->captured_bytes + stream, &result->captured_total, retained);
    }

    u64 overflow = length - retained;
    if (overflow)
    {
        result->capture_limit_exceeded = 1;
        result->output_truncated = 1;
        if (spawn.capture_overflow_policy == PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE)
        {
            OsFileDescriptor* file = spawn.capture_overflow_files[stream];
            if (file)
            {
                OsFileTransferResult transfer = os_file_write_checked(file, (ByteSlice){data + retained, overflow});
                pipe_capture_add_count(result->streamed_bytes + stream, &result->streamed_total, transfer.transferred);
                overflow -= transfer.transferred;
                if (transfer.error.v)
                {
                    result->capture_failed = 1;
                }
            }
            else
            {
                result->capture_failed = 1;
            }
        }
        else if (spawn.capture_overflow_policy == PROCESS_CAPTURE_OVERFLOW_FAIL)
        {
            result->capture_failed = 1;
        }
        if (overflow)
        {
            pipe_capture_add_count(result->dropped_bytes + stream, &result->dropped_total, overflow);
            if (spawn.capture_overflow_policy != PROCESS_CAPTURE_OVERFLOW_TRUNCATE)
            {
                result->capture_failed = 1;
            }
        }
    }
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

// Milliseconds left until `deadline`, clamped into a poll/wait argument. Zero
// when the deadline has passed, and `no_deadline` when there is none.
BUSTER_GLOBAL_LOCAL u64 os_process_deadline_milliseconds(u64 deadline_microseconds, u64 no_deadline)
{
    u64 result;
    if (!deadline_microseconds)
    {
        result = no_deadline;
    }
    else
    {
        u64 now = os_now_microseconds();
        if (now >= deadline_microseconds)
        {
            result = 0;
        }
        else
        {
            u64 remaining = (deadline_microseconds - now + 999) / 1000;
            // Wake up at least once a second regardless, so a deadline that
            // expires while nothing is readable is still noticed promptly.
            result = remaining < 1000 ? remaining : 1000;
        }
    }

    return result;
}

#if !BUSTER_WINDOWS
typedef enum OsProcessGroupOperation
{
    OS_PROCESS_GROUP_SIGNAL,
    OS_PROCESS_GROUP_QUERY,
    OS_PROCESS_GROUP_OPERATION_COUNT,
} OsProcessGroupOperation;

typedef struct OsProcessGroupReservation OsProcessGroupReservation;
struct OsProcessGroupReservation
{
    pid_t leader;
#if BUSTER_INCLUDE_TESTS
    bool count_test_syscalls;
    bool lose_test_ownership;
#endif
};

typedef struct OsProcessGroupSignalResult OsProcessGroupSignalResult;
struct OsProcessGroupSignalResult
{
    int status;
    int error;
};

typedef struct OsProcessGroupObservation OsProcessGroupObservation;
struct OsProcessGroupObservation
{
    bool valid;
    bool exited;
    int error;
};

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL u64 os_process_group_test_syscall_attempts;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool os_process_wait_test_expire_deadline_after_ready;
#if BUSTER_LINUX
BUSTER_GLOBAL_LOCAL pid_t os_linux_process_group_test_vanishing_process;
#endif

void os_process_wait_test_expire_deadline_after_ready_once(void)
{
    os_process_wait_test_expire_deadline_after_ready = true;
}
#endif

BUSTER_GLOBAL_LOCAL bool os_process_group_reservation_acquire(const OsProcessGroupReservation* reservation, OsProcessGroupOperation operation,
                                                              pid_t* leader)
{
    bool result = reservation->leader > 0 && operation < OS_PROCESS_GROUP_OPERATION_COUNT;
    if (result) { *leader = reservation->leader; }
    return result;
}

BUSTER_GLOBAL_LOCAL void os_process_group_reservation_release(OsProcessGroupReservation* reservation)
{
    reservation->leader = 0;
}

BUSTER_GLOBAL_LOCAL OsProcessGroupSignalResult os_process_group_signal(const OsProcessGroupReservation* reservation, int signal)
{
    OsProcessGroupSignalResult result = {.status = -1, .error = ESRCH};
    pid_t leader = 0;
    if (os_process_group_reservation_acquire(reservation, OS_PROCESS_GROUP_SIGNAL, &leader))
    {
#if BUSTER_INCLUDE_TESTS
        if (reservation->count_test_syscalls) { os_process_group_test_syscall_attempts += 1; }
#endif
        result.status = kill(-leader, signal);
        result.error = result.status ? errno : 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL OsProcessGroupObservation os_process_group_observe(const OsProcessGroupReservation* reservation, bool no_hang)
{
    OsProcessGroupObservation result = {.error = ESRCH};
    pid_t leader = 0;
    if (os_process_group_reservation_acquire(reservation, OS_PROCESS_GROUP_QUERY, &leader))
    {
#if BUSTER_INCLUDE_TESTS
        if (reservation->lose_test_ownership)
        {
            result.error = ECHILD;
        }
        else
#endif
        {
            siginfo_t information;
            int observed;
            do
            {
                information = (siginfo_t){0};
#if BUSTER_INCLUDE_TESTS
                if (reservation->count_test_syscalls) { os_process_group_test_syscall_attempts += 1; }
#endif
                observed = waitid(P_PID, (id_t)leader, &information, WEXITED | WNOWAIT | (no_hang ? WNOHANG : 0));
            } while (observed && errno == EINTR);
            result.valid = !observed && (information.si_pid == 0 || information.si_pid == leader);
            result.exited = result.valid && information.si_pid == leader;
            result.error = result.valid ? 0 : observed ? errno : EINVAL;
        }
    }
    return result;
}

#if BUSTER_MACOS
BUSTER_GLOBAL_LOCAL bool os_apple_process_group_is_quiescent(Arena* arena, const OsProcessGroupReservation* reservation);
#endif

#if BUSTER_LINUX
enum
{
    OS_LINUX_PROCESS_GROUP_MEMBER_LIMIT = 65536,
    OS_LINUX_PID_NAMESPACE_DEPTH_LIMIT = 32,
};

typedef struct OsLinuxProcessStatus OsLinuxProcessStatus;
struct OsLinuxProcessStatus
{
    pid_t process_id;
    pid_t namespace_process_ids[OS_LINUX_PID_NAMESPACE_DEPTH_LIMIT];
    u32 namespace_depth;
    bool valid;
};

typedef struct OsLinuxProcessGroupMember OsLinuxProcessGroupMember;
struct OsLinuxProcessGroupMember
{
    pid_t proc_process_id;
    pid_t local_process_id;
    pid_t namespace_process_ids[OS_LINUX_PID_NAMESPACE_DEPTH_LIMIT];
    u32 namespace_depth;
};

typedef struct OsLinuxProcessGroupCensus OsLinuxProcessGroupCensus;
struct OsLinuxProcessGroupCensus
{
    OsLinuxProcessGroupMember* members;
    u32 count;
    bool valid;
    bool retry;
};

typedef struct OsLinuxProcContext OsLinuxProcContext;
struct OsLinuxProcContext
{
    int descriptor;
    struct stat mount_identity;
    struct stat pid_namespace_identity;
    u32 current_namespace_index;
    u32 current_namespace_depth;
    bool valid;
};

BUSTER_GLOBAL_LOCAL bool os_linux_process_id_parse_length(const char* text, u64 length, pid_t* process_id)
{
    u64 value = 0;
    bool result = length != 0;
    for (u64 index = 0; result && index < length; index += 1)
    {
        u32 digit = (u32)(u8)text[index] - '0';
        result = digit < 10 && value <= ((u64)INT_MAX - digit) / 10;
        if (result) { value = value * 10 + digit; }
    }
    if (result) { *process_id = (pid_t)value; }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_id_parse(const char* text, pid_t* process_id)
{
    return os_linux_process_id_parse_length(text, strlen(text), process_id);
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_stat_parse(char* bytes, ssize_t length, pid_t expected_process_id,
                                                      pid_t* process_group, char* state)
{
    char* first_space = 0;
    char* close_parenthesis = 0;
    for (ssize_t index = 0; index < length; index += 1)
    {
        if (!first_space && bytes[index] == ' ') { first_space = bytes + index; }
        if (bytes[index] == ')') { close_parenthesis = bytes + index; }
    }

    bool result = first_space && first_space + 1 < bytes + length && first_space[1] == '(' &&
        close_parenthesis && close_parenthesis > first_space + 1;
    pid_t parsed_process_id = 0;
    if (result)
    {
        char saved = *first_space;
        *first_space = 0;
        result = os_linux_process_id_parse(bytes, &parsed_process_id);
        *first_space = saved;
    }
    long parent = 0;
    long group = 0;
    if (result)
    {
        result = parsed_process_id == expected_process_id &&
            sscanf(close_parenthesis + 1, " %c %ld %ld", state, &parent, &group) == 3 &&
            strchr("RSDZTWtXxKWPIN", *state) != 0 && group >= 0 && group <= INT_MAX;
    }
    if (result) { *process_group = (pid_t)group; }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_proc_read_at(int proc_descriptor, const char* path, char* bytes, u64 capacity, u64* length,
                                               bool* vanished)
{
    int descriptor = openat(proc_descriptor, path, O_RDONLY | O_CLOEXEC);
    *vanished = descriptor < 0 && (errno == ENOENT || errno == ESRCH);
    bool result = descriptor >= 0;
    u64 total = 0;
    if (result)
    {
        bool exhausted = false;
        while (result && !exhausted && total + 1 < capacity)
        {
            ssize_t read_result;
            do
            {
                read_result = read(descriptor, bytes + total, capacity - total - 1);
            } while (read_result < 0 && errno == EINTR);
            result = read_result >= 0;
            if (result)
            {
                exhausted = read_result == 0;
                total += (u64)read_result;
            }
        }
        if (result && !exhausted)
        {
            char overflow;
            ssize_t read_result;
            do
            {
                read_result = read(descriptor, &overflow, 1);
            } while (read_result < 0 && errno == EINTR);
            result = read_result == 0;
        }
        int close_result = close(descriptor);
        result = result && total != 0 && close_result == 0;
    }
    if (result)
    {
        bytes[total] = 0;
        *length = total;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_status_parse(char* bytes, u64 length, pid_t expected_process_id,
                                                        OsLinuxProcessStatus* status)
{
    OsLinuxProcessStatus parsed = {0};
    bool process_id_found = false;
    bool namespace_ids_found = false;
    bool result = true;
    for (u64 line_start = 0; result && line_start < length;)
    {
        u64 line_end = line_start;
        while (line_end < length && bytes[line_end] != '\n') { line_end += 1; }
        u64 line_length = line_end - line_start;
        char* line = bytes + line_start;
        if (line_length >= 4 && memcmp(line, "Pid:", 4) == 0)
        {
            u64 value_start = 4;
            while (value_start < line_length && (line[value_start] == ' ' || line[value_start] == '\t')) { value_start += 1; }
            u64 value_end = value_start;
            while (value_end < line_length && line[value_end] >= '0' && line[value_end] <= '9') { value_end += 1; }
            u64 digits_end = value_end;
            while (value_end < line_length && (line[value_end] == ' ' || line[value_end] == '\t')) { value_end += 1; }
            process_id_found = !process_id_found && value_end == line_length &&
                os_linux_process_id_parse_length(line + value_start, digits_end - value_start, &parsed.process_id);
            result = process_id_found;
        }
        else if (line_length >= 7 && memcmp(line, "NSpid:", 6) == 0)
        {
            result = !namespace_ids_found;
            namespace_ids_found = true;
            u64 at = 6;
            while (result && at < line_length)
            {
                while (at < line_length && (line[at] == ' ' || line[at] == '\t')) { at += 1; }
                if (at < line_length)
                {
                    u64 value_start = at;
                    while (at < line_length && line[at] >= '0' && line[at] <= '9') { at += 1; }
                    result = value_start < at && parsed.namespace_depth < OS_LINUX_PID_NAMESPACE_DEPTH_LIMIT &&
                        os_linux_process_id_parse_length(line + value_start, at - value_start,
                            &parsed.namespace_process_ids[parsed.namespace_depth]);
                    if (result) { parsed.namespace_depth += 1; }
                }
            }
        }
        line_start = line_end < length ? line_end + 1 : line_end;
    }
    result = result && process_id_found && namespace_ids_found && parsed.namespace_depth &&
        parsed.namespace_process_ids[0] == parsed.process_id &&
        (!expected_process_id || parsed.process_id == expected_process_id);
    if (result)
    {
        parsed.valid = true;
        *status = parsed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_stat_at(int proc_descriptor, pid_t process_id, pid_t* process_group,
                                                   char* state, bool* vanished)
{
    char path[64];
    int path_length = snprintf(path, sizeof(path), "%ld/stat", (long)process_id);
    char bytes[4096];
    u64 length = 0;
    bool result = path_length > 0 && (u64)path_length < sizeof(path) &&
        os_linux_proc_read_at(proc_descriptor, path, bytes, sizeof(bytes), &length, vanished);
    if (result)
    {
        result = os_linux_process_stat_parse(bytes, (ssize_t)length, process_id, process_group, state);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_status_at(int proc_descriptor, pid_t process_id, OsLinuxProcessStatus* status,
                                                     bool* vanished)
{
    char path[64];
    int path_length = snprintf(path, sizeof(path), "%ld/status", (long)process_id);
    char bytes[16384];
    u64 length = 0;
    bool result = path_length > 0 && (u64)path_length < sizeof(path) &&
        os_linux_proc_read_at(proc_descriptor, path, bytes, sizeof(bytes), &length, vanished);
    if (result) { result = os_linux_process_status_parse(bytes, length, process_id, status); }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_pid_namespace_at(int proc_descriptor, pid_t process_id, struct stat* identity,
                                                            bool* vanished)
{
    char path[64];
    int path_length = snprintf(path, sizeof(path), "%ld/ns/pid", (long)process_id);
    int status = path_length > 0 && (u64)path_length < sizeof(path) ? fstatat(proc_descriptor, path, identity, 0) : -1;
    *vanished = status != 0 && (errno == ENOENT || errno == ESRCH);
    return status == 0;
}

BUSTER_GLOBAL_LOCAL bool os_linux_same_file_identity(struct stat left, struct stat right)
{
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_status_namespace_index(OsLinuxProcessStatus status, pid_t process_id, u32* namespace_index)
{
    u32 matches = 0;
    for (u32 index = 0; index < status.namespace_depth; index += 1)
    {
        if (status.namespace_process_ids[index] == process_id)
        {
            *namespace_index = index;
            matches += 1;
        }
    }
    return matches == 1;
}

BUSTER_GLOBAL_LOCAL OsLinuxProcContext os_linux_proc_context_open(void)
{
    OsLinuxProcContext result = {.descriptor = -1};
    result.descriptor = open("/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    result.valid = result.descriptor >= 0 && fstat(result.descriptor, &result.mount_identity) == 0 &&
        fstatat(result.descriptor, "self/ns/pid", &result.pid_namespace_identity, 0) == 0;
    char bytes[16384];
    u64 length = 0;
    bool vanished = false;
    OsLinuxProcessStatus self = {0};
    if (result.valid)
    {
        result.valid = os_linux_proc_read_at(result.descriptor, "self/status", bytes, sizeof(bytes), &length, &vanished) &&
            os_linux_process_status_parse(bytes, length, 0, &self);
    }
    pid_t self_process_id = getpid();
    result.current_namespace_depth = self.namespace_depth;
    result.valid = result.valid && os_linux_process_status_namespace_index(self, self_process_id, &result.current_namespace_index);
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_proc_context_close(OsLinuxProcContext* context)
{
    struct stat final_identity = {0};
    bool result = context->valid && context->descriptor >= 0;
    if (context->descriptor >= 0)
    {
        bool identity_valid = fstat(context->descriptor, &final_identity) == 0 &&
            os_linux_same_file_identity(context->mount_identity, final_identity);
        int close_status = close(context->descriptor);
        result = identity_valid && close_status == 0 && result;
    }
    context->descriptor = -1;
    context->valid = false;
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_group_resolve_leader(OsLinuxProcContext* context, pid_t local_leader,
                                                                pid_t* proc_leader)
{
    bool result = false;
    u32 matches = 0;
    pid_t process_group = 0;
    char state = 0;
    bool vanished = false;
    OsLinuxProcessStatus status = {0};
    struct stat namespace_identity = {0};

    // Same-namespace procfs is the ordinary fast path.
    bool direct = os_linux_process_stat_at(context->descriptor, local_leader, &process_group, &state, &vanished) &&
        process_group == local_leader &&
        os_linux_process_status_at(context->descriptor, local_leader, &status, &vanished) &&
        os_linux_process_pid_namespace_at(context->descriptor, local_leader, &namespace_identity, &vanished) &&
        status.namespace_depth == context->current_namespace_depth &&
        status.namespace_process_ids[context->current_namespace_index] == local_leader &&
        os_linux_same_file_identity(namespace_identity, context->pid_namespace_identity);
    if (direct)
    {
        *proc_leader = local_leader;
        result = true;
    }
    else
    {
        int scan_descriptor = openat(context->descriptor, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        DIR* processes = scan_descriptor >= 0 ? fdopendir(scan_descriptor) : 0;
        bool valid = processes != 0;
        if (!processes && scan_descriptor >= 0) { close(scan_descriptor); }
        while (valid)
        {
            errno = 0;
            struct dirent* entry = readdir(processes);
            if (!entry)
            {
                valid = errno == 0;
                break;
            }
            bool numeric_name = entry->d_name[0] != 0;
            for (u64 index = 0; numeric_name && entry->d_name[index]; index += 1)
            {
                numeric_name = entry->d_name[index] >= '0' && entry->d_name[index] <= '9';
            }
            pid_t process_id = 0;
            bool parsed_id = numeric_name && os_linux_process_id_parse(entry->d_name, &process_id);
            if (numeric_name && !parsed_id)
            {
                valid = false;
            }
            else if (parsed_id)
            {
                OsLinuxProcessStatus candidate_status = {0};
                bool stat_valid = os_linux_process_stat_at(context->descriptor, process_id, &process_group, &state, &vanished);
                // readdir is only a point-in-time inventory. A PID that exits
                // before its files are opened is absent from this snapshot;
                // malformed or unreadable live entries still fail closed.
                valid = stat_valid || vanished;
                if (stat_valid && process_group == process_id)
                {
                    bool status_valid = os_linux_process_status_at(context->descriptor, process_id, &candidate_status, &vanished);
                    valid = status_valid || vanished;
                    if (status_valid && candidate_status.namespace_depth == context->current_namespace_depth &&
                        candidate_status.namespace_process_ids[context->current_namespace_index] == local_leader)
                    {
                        bool namespace_valid = os_linux_process_pid_namespace_at(
                            context->descriptor, process_id, &namespace_identity, &vanished);
                        valid = namespace_valid || vanished;
                        if (namespace_valid && os_linux_same_file_identity(namespace_identity, context->pid_namespace_identity))
                        {
                            *proc_leader = process_id;
                            matches += 1;
                        }
                    }
                }
            }
        }
        if (processes && closedir(processes) != 0) { valid = false; }
        result = valid && matches == 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL OsLinuxProcessGroupCensus os_linux_process_group_census(Arena* arena, OsLinuxProcContext* context,
                                                                            pid_t proc_leader, pid_t local_leader)
{
    OsLinuxProcessGroupCensus result = {
        .members = arena_allocate(arena, OsLinuxProcessGroupMember, OS_LINUX_PROCESS_GROUP_MEMBER_LIMIT),
        .valid = true,
    };
    int scan_descriptor = openat(context->descriptor, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR* processes = scan_descriptor >= 0 ? fdopendir(scan_descriptor) : 0;
    result.valid = processes != 0;
    if (!processes && scan_descriptor >= 0) { close(scan_descriptor); }
    while (result.valid)
    {
        errno = 0;
        struct dirent* entry = readdir(processes);
        if (!entry)
        {
            result.valid = errno == 0;
            break;
        }
        bool numeric_name = entry->d_name[0] != 0;
        for (u64 index = 0; numeric_name && entry->d_name[index]; index += 1)
        {
            numeric_name = entry->d_name[index] >= '0' && entry->d_name[index] <= '9';
        }
        pid_t process_id = 0;
        bool parsed_id = numeric_name && os_linux_process_id_parse(entry->d_name, &process_id);
        if (numeric_name && !parsed_id)
        {
            result.valid = false;
        }
        else if (parsed_id)
        {
            pid_t process_group = 0;
            char state = 0;
            bool vanished = false;
            OsLinuxProcessStatus status = {0};
            bool include_member = false;
            bool stat_valid = os_linux_process_stat_at(context->descriptor, process_id, &process_group, &state, &vanished);
#if BUSTER_INCLUDE_TESTS
            if (process_id == os_linux_process_group_test_vanishing_process)
            {
                stat_valid = false;
                vanished = true;
                os_linux_process_group_test_vanishing_process = 0;
            }
#endif
            // A directory entry may legitimately disappear before stat is
            // opened, but omitting it could make two incomplete inventories
            // appear equal. Retry the complete two-snapshot proof instead.
            result.retry = vanished;
            result.valid = stat_valid;
            if (stat_valid && process_group == proc_leader)
            {
                result.valid = state == 'Z';
                bool status_valid = false;
                if (result.valid)
                {
                    status_valid = os_linux_process_status_at(context->descriptor, process_id, &status, &vanished);
                    result.retry = vanished;
                    result.valid = status_valid;
                }
                if (status_valid)
                {
                    result.valid = status.namespace_depth >= context->current_namespace_depth &&
                        context->current_namespace_index < status.namespace_depth &&
                        result.count < OS_LINUX_PROCESS_GROUP_MEMBER_LIMIT;
                    include_member = result.valid;
                }
                if (include_member && status.namespace_depth == context->current_namespace_depth)
                {
                    struct stat namespace_identity = {0};
                    bool namespace_valid = os_linux_process_pid_namespace_at(
                        context->descriptor, process_id, &namespace_identity, &vanished);
                    result.retry = vanished;
                    result.valid = namespace_valid &&
                        os_linux_same_file_identity(namespace_identity, context->pid_namespace_identity);
                    include_member = namespace_valid && result.valid;
                }
                if (include_member)
                {
                    result.members[result.count++] = (OsLinuxProcessGroupMember){
                        .proc_process_id = process_id,
                        .local_process_id = status.namespace_process_ids[context->current_namespace_index],
                        .namespace_depth = status.namespace_depth,
                    };
                    memcpy(result.members[result.count - 1].namespace_process_ids, status.namespace_process_ids,
                        sizeof(pid_t) * status.namespace_depth);
                }
            }
        }
    }
    if (processes && closedir(processes) != 0) { result.valid = false; }
    for (u32 index = 1; result.valid && index < result.count; index += 1)
    {
        OsLinuxProcessGroupMember member = result.members[index];
        u32 insertion = index;
        while (insertion && (result.members[insertion - 1].local_process_id > member.local_process_id ||
                             (result.members[insertion - 1].local_process_id == member.local_process_id &&
                              result.members[insertion - 1].proc_process_id > member.proc_process_id)))
        {
            result.members[insertion] = result.members[insertion - 1];
            insertion -= 1;
        }
        result.members[insertion] = member;
    }
    u32 leader_count = 0;
    for (u32 index = 0; result.valid && index < result.count; index += 1)
    {
        OsLinuxProcessGroupMember member = result.members[index];
        leader_count += member.proc_process_id == proc_leader && member.local_process_id == local_leader;
        if (index && member.local_process_id == result.members[index - 1].local_process_id) { result.valid = false; }
    }
    result.valid = result.valid && leader_count == 1;
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_group_census_equal(OsLinuxProcessGroupCensus left, OsLinuxProcessGroupCensus right)
{
    bool result = left.valid && right.valid && left.count == right.count;
    for (u32 index = 0; result && index < left.count; index += 1)
    {
        result = left.members[index].proc_process_id == right.members[index].proc_process_id &&
            left.members[index].local_process_id == right.members[index].local_process_id &&
            left.members[index].namespace_depth == right.members[index].namespace_depth &&
            memcmp(left.members[index].namespace_process_ids, right.members[index].namespace_process_ids,
                sizeof(pid_t) * left.members[index].namespace_depth) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool os_linux_process_group_is_quiescent(Arena* arena, const OsProcessGroupReservation* reservation)
{
    enum {OS_LINUX_PROCESS_GROUP_CENSUS_ATTEMPTS = 8};
    pid_t leader = 0;
    bool result = os_process_group_reservation_acquire(reservation, OS_PROCESS_GROUP_QUERY, &leader);
    if (result)
    {
        OsLinuxProcContext context = os_linux_proc_context_open();
        pid_t proc_leader = 0;
        result = context.valid && os_linux_process_group_resolve_leader(&context, leader, &proc_leader);
        bool proven = false;
        bool fatal = false;
        u64 census_position = arena->position;
        if (result)
        {
            for (u32 attempt = 0; attempt < OS_LINUX_PROCESS_GROUP_CENSUS_ATTEMPTS && !proven && !fatal; attempt += 1)
            {
                OsLinuxProcessGroupCensus first = os_linux_process_group_census(arena, &context, proc_leader, leader);
                OsLinuxProcessGroupCensus second = {0};
                if (first.valid) { second = os_linux_process_group_census(arena, &context, proc_leader, leader); }
                fatal = (!first.valid && !first.retry) || (!second.valid && !second.retry && first.valid);
                proven = first.valid && second.valid && os_linux_process_group_census_equal(first, second);
                arena_set_position(arena, census_position);
            }
        }
        result = result && proven;
        result = os_linux_proc_context_close(&context) && result;
        // Revalidate the exact child reservation immediately after the two
        // snapshots. No numeric group operation is permitted if it was lost.
        OsProcessGroupObservation observation = os_process_group_observe(reservation, true);
        result = result && observation.valid && observation.exited;
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
bool os_linux_process_group_churn_self_test(Arena* arena)
{
    pid_t leader = fork();
    if (leader == 0)
    {
        int group_status = setpgid(0, 0);
        _exit(group_status == 0 ? 0 : 103);
    }
    bool group_ready = leader > 0 && setpgid(leader, leader) == 0;
    pid_t unrelated = group_ready ? fork() : -1;
    if (unrelated == 0)
    {
        poll(0, 0, 3000);
        _exit(0);
    }

    OsProcessGroupObservation observation = {0};
    OsProcessGroupReservation reservation = {.leader = leader};
    if (unrelated > 0)
    {
        observation = os_process_group_observe(&reservation, false);
        os_linux_process_group_test_vanishing_process = unrelated;
    }
    bool quiescent = observation.valid && observation.exited &&
        os_linux_process_group_is_quiescent(arena, &reservation);
    os_linux_process_group_test_vanishing_process = 0;

    bool leader_reaped = leader <= 0;
    if (leader > 0)
    {
        int leader_status = 0;
        pid_t leader_wait;
        do
        {
            leader_wait = waitpid(leader, &leader_status, 0);
        } while (leader_wait < 0 && errno == EINTR);
        leader_reaped = leader_wait == leader;
    }
    bool unrelated_reaped = unrelated <= 0;
    if (unrelated > 0)
    {
        int kill_status = kill(unrelated, SIGKILL);
        bool kill_valid = kill_status == 0 || errno == ESRCH;
        int unrelated_status = 0;
        pid_t unrelated_wait;
        do
        {
            unrelated_wait = waitpid(unrelated, &unrelated_status, 0);
        } while (unrelated_wait < 0 && errno == EINTR);
        unrelated_reaped = kill_valid && unrelated_wait == unrelated;
    }
    return group_ready && unrelated > 0 && quiescent && leader_reaped && unrelated_reaped;
}
#endif

#if BUSTER_INCLUDE_TESTS
bool os_linux_process_stat_parse_self_test(void)
{
    char zombie[] = "123 (comm with ) inside) Z 1 123 0";
    char running[] = "456 (run) R 1 456 0";
    char malformed[] = "789 (missing close Z 1 789 0";
    char invalid_group[] = "321 (bad group) Z 1 -1 0";
    char status_text[] = "Name:\ttest\nPid:\t999\nNSpid:\t999\t12\t7\n";
    char duplicate_status[] = "Pid:\t999\nNSpid:\t999\t12\nNSpid:\t999\t12\n";
    char short_status[] = "Pid:\t999\nNSpid:\t\n";
    pid_t group = 0;
    char state = 0;
    OsLinuxProcessStatus status = {0};
    bool result = os_linux_process_stat_parse(zombie, sizeof(zombie) - 1, 123, &group, &state) && group == 123 && state == 'Z';
    result = os_linux_process_stat_parse(running, sizeof(running) - 1, 456, &group, &state) && group == 456 && state == 'R' && result;
    result = !os_linux_process_stat_parse(zombie, sizeof(zombie) - 1, 124, &group, &state) && result;
    result = !os_linux_process_stat_parse(malformed, sizeof(malformed) - 1, 789, &group, &state) && result;
    result = !os_linux_process_stat_parse(invalid_group, sizeof(invalid_group) - 1, 321, &group, &state) && result;
    result = os_linux_process_status_parse(status_text, sizeof(status_text) - 1, 999, &status) &&
        status.namespace_depth == 3 && status.namespace_process_ids[0] == 999 && status.namespace_process_ids[1] == 12 &&
        status.namespace_process_ids[2] == 7 && result;
    result = !os_linux_process_status_parse(status_text, sizeof(status_text) - 1, 998, &status) && result;
    result = !os_linux_process_status_parse(duplicate_status, sizeof(duplicate_status) - 1, 999, &status) && result;
    result = !os_linux_process_status_parse(short_status, sizeof(short_status) - 1, 999, &status) && result;
    OsLinuxProcessStatus mapping = {
        .namespace_process_ids = {999, 12, 7},
        .namespace_depth = 3,
        .valid = true,
    };
    u32 namespace_index = 0;
    result = os_linux_process_status_namespace_index(mapping, 12, &namespace_index) && namespace_index == 1 && result;
    result = !os_linux_process_status_namespace_index(mapping, 8, &namespace_index) && result;
    mapping.namespace_process_ids[2] = 12;
    result = !os_linux_process_status_namespace_index(mapping, 12, &namespace_index) && result;
    OsLinuxProcessGroupMember members[] = {
        {.proc_process_id = 999, .local_process_id = 12, .namespace_process_ids = {999, 12}, .namespace_depth = 2},
    };
    OsLinuxProcessGroupMember changed_members[] = {
        {.proc_process_id = 999, .local_process_id = 12, .namespace_process_ids = {999, 12}, .namespace_depth = 2},
    };
    OsLinuxProcessGroupCensus census = {.members = members, .count = 1, .valid = true};
    OsLinuxProcessGroupCensus changed = {.members = changed_members, .count = 1, .valid = true};
    result = os_linux_process_group_census_equal(census, changed) && result;
    changed_members[0].namespace_process_ids[0] = 998;
    result = !os_linux_process_group_census_equal(census, changed) && result;
    return result;
}
#endif
#endif

typedef enum OsProcessGroupCleanupResult
{
    OS_PROCESS_GROUP_CLEANUP_RETRY,
    OS_PROCESS_GROUP_CLEANUP_PROVEN,
    OS_PROCESS_GROUP_CLEANUP_OWNERSHIP_LOST,
} OsProcessGroupCleanupResult;

BUSTER_GLOBAL_LOCAL OsProcessGroupCleanupResult os_process_group_terminate_and_prove(Arena* arena,
                                                                                     const OsProcessGroupReservation* reservation)
{
    // The first snapshot is the normal fast path. A bounded 100 ms tail lets
    // killed members reach SZOMB under loaded CI without weakening the proof.
    enum {OS_PROCESS_GROUP_CLEANUP_ATTEMPTS = 101};
    OsProcessGroupCleanupResult result = OS_PROCESS_GROUP_CLEANUP_RETRY;
#if BUSTER_LINUX || BUSTER_MACOS
    BUSTER_CHECK(arena != 0);
    u64 arena_position = arena->position;
#else
    BUSTER_UNUSED(arena);
#endif
    for (u32 attempt = 0; attempt < OS_PROCESS_GROUP_CLEANUP_ATTEMPTS && result == OS_PROCESS_GROUP_CLEANUP_RETRY; attempt += 1)
    {
#if BUSTER_LINUX || BUSTER_MACOS
        arena_set_position(arena, arena_position);
#endif
        OsProcessGroupObservation observation = os_process_group_observe(reservation, true);
        OsProcessGroupSignalResult signal_result = {0};
        if (!observation.valid)
        {
            result = OS_PROCESS_GROUP_CLEANUP_OWNERSHIP_LOST;
        }
        else
        {
            signal_result = os_process_group_signal(reservation, SIGKILL);
#if BUSTER_MACOS
        // Darwin killpg success means at least one member accepted the signal,
        // not that every member retired. Always prove the complete snapshot.
        BUSTER_UNUSED(signal_result);
            if (os_apple_process_group_is_quiescent(arena, reservation)) { result = OS_PROCESS_GROUP_CLEANUP_PROVEN; }
#elif BUSTER_LINUX
        BUSTER_UNUSED(signal_result);
            if (os_linux_process_group_is_quiescent(arena, reservation)) { result = OS_PROCESS_GROUP_CLEANUP_PROVEN; }
#else
            if (signal_result.status == 0 || signal_result.error == ESRCH) { result = OS_PROCESS_GROUP_CLEANUP_PROVEN; }
#endif
        }
        if (result == OS_PROCESS_GROUP_CLEANUP_RETRY && attempt + 1 < OS_PROCESS_GROUP_CLEANUP_ATTEMPTS) { poll(0, 0, 1); }
    }
#if BUSTER_LINUX || BUSTER_MACOS
    arena_set_position(arena, arena_position);
#endif
    return result;
}

#if BUSTER_MACOS
// Darwin reports EPERM when killpg finds only zombies. Query the still-reserved
// process group before reaping its leader. A growing or malformed snapshot
// fails closed, and a bound prevents a corrupt size from consuming the arena.
BUSTER_GLOBAL_LOCAL bool os_apple_process_group_is_quiescent(Arena* arena, const OsProcessGroupReservation* reservation)
{
    enum {OS_APPLE_PROCESS_GROUP_MEMBER_LIMIT = 4096};
    pid_t leader = 0;
    bool result = false;
    if (os_process_group_reservation_acquire(reservation, OS_PROCESS_GROUP_QUERY, &leader))
    {
        int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PGRP, leader};
        size_t required_size = 0;
#if BUSTER_INCLUDE_TESTS
        if (reservation->count_test_syscalls) { os_process_group_test_syscall_attempts += 1; }
#endif
        if (sysctl(query, 4, 0, &required_size, 0, 0) == 0 && required_size && required_size % sizeof(struct kinfo_proc) == 0 &&
            required_size / sizeof(struct kinfo_proc) <= OS_APPLE_PROCESS_GROUP_MEMBER_LIMIT)
        {
            u64 capacity = required_size / sizeof(struct kinfo_proc);
            struct kinfo_proc* process_infos = arena_allocate(arena, struct kinfo_proc, capacity);
            size_t returned_size = required_size;
#if BUSTER_INCLUDE_TESTS
            if (reservation->count_test_syscalls) { os_process_group_test_syscall_attempts += 1; }
#endif
            if (sysctl(query, 4, process_infos, &returned_size, 0, 0) == 0 && returned_size && returned_size <= required_size &&
                returned_size % sizeof(struct kinfo_proc) == 0)
            {
                u64 process_count = returned_size / sizeof(struct kinfo_proc);
                u64 leader_count = 0;
                bool all_zombies = true;
                for (u64 process_index = 0; process_index < process_count; process_index += 1)
                {
                    leader_count += process_infos[process_index].kp_proc.p_pid == leader;
                    all_zombies = all_zombies && process_infos[process_index].kp_proc.p_stat == SZOMB;
                }
                result = leader_count == 1 && all_zombies;
            }
        }
    }
    return result;
}
#endif

#if BUSTER_INCLUDE_TESTS && (BUSTER_LINUX || BUSTER_MACOS)
bool os_process_group_reservation_release_self_test(void)
{
    OsProcessGroupReservation reservation = {.leader = 0x7fffffff, .count_test_syscalls = true};
    bool result = true;
    for (u64 operation = 0; operation < OS_PROCESS_GROUP_OPERATION_COUNT; operation += 1)
    {
        pid_t leader = 0;
        result = os_process_group_reservation_acquire(&reservation, (OsProcessGroupOperation)operation, &leader) && leader == reservation.leader && result;
    }

    os_process_group_reservation_release(&reservation);
    u64 syscall_attempts = os_process_group_test_syscall_attempts;
    OsProcessGroupSignalResult signal_result = os_process_group_signal(&reservation, 0);
    result = signal_result.status == -1 && signal_result.error == ESRCH && os_process_group_test_syscall_attempts == syscall_attempts && result;
#if BUSTER_MACOS
    TemporalArena scratch = scratch_begin(0, 0);
    result = !os_apple_process_group_is_quiescent(scratch.arena, &reservation) &&
             os_process_group_test_syscall_attempts == syscall_attempts && result;
    scratch_end(scratch);
#else
    pid_t leader = 0x7fffffff;
    result = !os_process_group_reservation_acquire(&reservation, OS_PROCESS_GROUP_QUERY, &leader) && leader == 0x7fffffff && result;
#endif
    return result;
}
#endif
#endif

#if !BUSTER_WINDOWS
typedef enum OsProcessGroupWaitOutcome
{
    OS_PROCESS_GROUP_WAIT_ACTIVE,
    OS_PROCESS_GROUP_WAIT_QUIESCENT_RESERVED,
    OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST,
    OS_PROCESS_GROUP_WAIT_RETAINED_FAILURE,
} OsProcessGroupWaitOutcome;

typedef enum OsProcessGroupControl
{
    OS_PROCESS_GROUP_CONTROL_NONE,
    OS_PROCESS_GROUP_CONTROL_TERMINATE,
    OS_PROCESS_GROUP_CONTROL_KILL,
} OsProcessGroupControl;

typedef struct OsProcessGroupWaitState OsProcessGroupWaitState;
struct OsProcessGroupWaitState
{
    OsProcessGroupReservation reservation;
    OsProcessGroupControl delivered_control;
    OsProcessGroupWaitOutcome outcome;
    u32 recovery_failures;
    bool cleanup_failure_published;
};

BUSTER_GLOBAL_LOCAL OsProcessGroupControl os_process_group_control_requested(ProcessGroupControlState* control)
{
    OsProcessGroupControl result = OS_PROCESS_GROUP_CONTROL_NONE;
    if (control && control->cancellation_escalated && process_control_atomic_load(control->cancellation_escalated))
    {
        result = OS_PROCESS_GROUP_CONTROL_KILL;
    }
    else if (control && control->cancellation_signal && process_control_atomic_load(control->cancellation_signal))
    {
        result = OS_PROCESS_GROUP_CONTROL_TERMINATE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void os_process_group_wait_publish_cleanup_failure(ProcessSpawnResult spawn, OsProcessGroupWaitState* state)
{
    if (!state->cleanup_failure_published)
    {
        state->cleanup_failure_published = true;
        ProcessGroupControlState* control = spawn.process_group_control;
        if (control)
        {
            if (control->admission_mutex) { os_mutex_lock(control->admission_mutex); }
            if (control->cancellation_signal) { process_control_atomic_set_if_zero(control->cancellation_signal, SIGTERM); }
            if (control->cancellation_escalated) { process_control_atomic_store(control->cancellation_escalated, 1); }
            if (control->admission_mutex) { os_mutex_unlock(control->admission_mutex); }
        }
    }
}

BUSTER_GLOBAL_LOCAL void os_process_group_wait_recover(ProcessSpawnResult spawn, OsProcessGroupWaitState* state)
{
    // An initial bounded campaign plus three forced campaigns gives the owner
    // a deterministic recovery budget. Its first failure stops new admission;
    // shared flags cannot signal a group or grant cleanup success.
    enum {OS_PROCESS_GROUP_RECOVERY_CAMPAIGNS = 4};
    os_process_group_wait_publish_cleanup_failure(spawn, state);
    state->recovery_failures += 1;
    if (state->recovery_failures >= OS_PROCESS_GROUP_RECOVERY_CAMPAIGNS)
    {
        state->outcome = OS_PROCESS_GROUP_WAIT_RETAINED_FAILURE;
    }
}

#if BUSTER_INCLUDE_TESTS && (BUSTER_LINUX || BUSTER_MACOS)
bool os_process_group_recovery_self_test(void)
{
    ProcessControlAtomic cancellation_signal = 0;
    ProcessControlAtomic cancellation_escalated = 0;
    ProcessGroupControlState control = {
        .cancellation_signal = &cancellation_signal,
        .cancellation_escalated = &cancellation_escalated,
    };
    ProcessSpawnResult spawn = {.process_group_control = &control};
    OsProcessGroupWaitState state = {0};
    for (u32 campaign = 0; campaign < 4; campaign += 1)
    {
        os_process_group_wait_recover(spawn, &state);
    }
    return process_control_atomic_load(&cancellation_signal) == SIGTERM && process_control_atomic_load(&cancellation_escalated) == 1 &&
        state.cleanup_failure_published && state.recovery_failures == 4 &&
        state.outcome == OS_PROCESS_GROUP_WAIT_RETAINED_FAILURE;
}
#endif

BUSTER_GLOBAL_LOCAL void os_process_group_wait_step(Arena* arena, ProcessSpawnResult spawn, bool force_kill,
                                                    OsProcessGroupWaitState* state)
{
    if (state->outcome == OS_PROCESS_GROUP_WAIT_ACTIVE)
    {
        OsProcessGroupControl requested = os_process_group_control_requested(spawn.process_group_control);
        if (force_kill || state->recovery_failures) { requested = OS_PROCESS_GROUP_CONTROL_KILL; }

        // This successful WNOWAIT observation is the ownership proof for
        // every group operation performed by this step.
        OsProcessGroupObservation observation = os_process_group_observe(&state->reservation, true);
        if (!observation.valid)
        {
            os_process_group_wait_publish_cleanup_failure(spawn, state);
            state->outcome = OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST;
        }
        else if (observation.exited)
        {
            OsProcessGroupCleanupResult cleanup = os_process_group_terminate_and_prove(arena, &state->reservation);
            if (cleanup == OS_PROCESS_GROUP_CLEANUP_PROVEN)
            {
                state->outcome = OS_PROCESS_GROUP_WAIT_QUIESCENT_RESERVED;
            }
            else if (cleanup == OS_PROCESS_GROUP_CLEANUP_OWNERSHIP_LOST)
            {
                os_process_group_wait_publish_cleanup_failure(spawn, state);
                state->outcome = OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST;
            }
            else
            {
                os_process_group_wait_recover(spawn, state);
            }
        }
        else if (requested > state->delivered_control)
        {
            int signal = requested == OS_PROCESS_GROUP_CONTROL_KILL ? SIGKILL : SIGTERM;
            OsProcessGroupSignalResult signal_result = os_process_group_signal(&state->reservation, signal);
            if (signal_result.status == 0)
            {
                state->delivered_control = requested;
            }
            else
            {
                // The leader can exit after the ownership observation but
                // before killpg. Re-observe before interpreting that error or
                // making any further operation on its numeric identity.
                OsProcessGroupObservation after_signal = os_process_group_observe(&state->reservation, true);
                if (!after_signal.valid)
                {
                    os_process_group_wait_publish_cleanup_failure(spawn, state);
                    state->outcome = OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST;
                }
                else if (after_signal.exited)
                {
                    OsProcessGroupCleanupResult cleanup = os_process_group_terminate_and_prove(arena, &state->reservation);
                    if (cleanup == OS_PROCESS_GROUP_CLEANUP_PROVEN)
                    {
                        state->outcome = OS_PROCESS_GROUP_WAIT_QUIESCENT_RESERVED;
                    }
                    else if (cleanup == OS_PROCESS_GROUP_CLEANUP_OWNERSHIP_LOST)
                    {
                        os_process_group_wait_publish_cleanup_failure(spawn, state);
                        state->outcome = OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST;
                    }
                    else
                    {
                        os_process_group_wait_recover(spawn, state);
                    }
                }
                else
                {
                    os_process_group_wait_recover(spawn, state);
                }
            }
        }
    }
}

#if BUSTER_INCLUDE_TESTS && (BUSTER_LINUX || BUSTER_MACOS)
bool os_process_group_ownership_loss_self_test(void)
{
    ProcessControlAtomic cancellation_signal = 0;
    ProcessControlAtomic cancellation_escalated = 0;
    ProcessGroupControlState control = {
        .cancellation_signal = &cancellation_signal,
        .cancellation_escalated = &cancellation_escalated,
    };
    ProcessSpawnResult spawn = {.process_group_control = &control};
    OsProcessGroupWaitState state = {
        .reservation = {.leader = 0x7fffffff, .count_test_syscalls = true, .lose_test_ownership = true},
    };
    u64 syscall_attempts = os_process_group_test_syscall_attempts;
    os_process_group_wait_step(0, spawn, false, &state);
    os_process_group_wait_step(0, spawn, false, &state);
    return state.outcome == OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST && state.cleanup_failure_published &&
        process_control_atomic_load(&cancellation_signal) == SIGTERM && process_control_atomic_load(&cancellation_escalated) == 1 &&
        os_process_group_test_syscall_attempts == syscall_attempts;
}
#endif
#endif

ProcessWaitResult os_process_wait_deadline(Arena* arena, ProcessSpawnResult spawn, u64 timeout_microseconds)
{
    ProcessWaitResult result = {0};
    result.result = PROCESS_RESULT_UNKNOWN;
#if BUSTER_INCLUDE_TESTS && !BUSTER_WINDOWS
    bool test_expire_deadline_after_ready = os_process_wait_test_expire_deadline_after_ready;
    os_process_wait_test_expire_deadline_after_ready = false;
#endif

    if (spawn.handle)
    {
        u64 deadline = timeout_microseconds ? os_now_microseconds() + timeout_microseconds : 0;
        bool timed_out = false;
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

        bool child_exit_observed = false;
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
                        pipe_capture_append(scratch.arena, &captures[stream], spawn, &result, (StandardStream)stream, buffer, read_byte_count);
                        made_progress = true;
                    }
                }
            }

            if (!made_progress)
            {
                DWORD process_wait = WaitForSingleObject(spawn.handle, 0);
                if (process_wait == WAIT_OBJECT_0)
                {
                    if (child_exit_observed)
                    {
                        // The child was already known to have exited before
                        // this complete empty scan. A descendant may retain an
                        // inherited writer, but its lifetime must not turn the
                        // completed child's wait into a false timeout.
                        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
                        {
                            if (read_pipes[stream])
                            {
                                CloseHandle(read_pipes[stream]);
                                read_pipes[stream] = 0;
                                open_pipe_count -= 1;
                            }
                        }
                        break;
                    }
                    // The child may have written between the scan above and
                    // this observation. Perform one final complete scan before
                    // closing any writer kept alive outside the child.
                    child_exit_observed = true;
                    continue;
                }
                if (deadline && !os_process_deadline_milliseconds(deadline, 1))
                {
                    timed_out = true;
                    break;
                }
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
            if (read_pipes[stream])
            {
                CloseHandle(read_pipes[stream]);
                read_pipes[stream] = 0;
            }
        }

        // A child with no captured stream never entered the drain loop, so this
        // is where its deadline is enforced. The helper clamps its answer, so
        // one WAIT_TIMEOUT only means this slice elapsed; the deadline itself
        // decides whether to give up.
        while (!timed_out && WaitForSingleObject(spawn.handle, (DWORD)os_process_deadline_milliseconds(deadline, INFINITE)) == WAIT_TIMEOUT)
        {
            timed_out = !os_process_deadline_milliseconds(deadline, 1);
        }
        if (timed_out)
        {
            // The exit code below then describes this kill rather than the
            // child's own progress, which is why `timed_out` is reported
            // separately.
            result.termination_requested = 1;
            result.forcibly_terminated = 1;
            BOOL terminated = spawn.process_tree ? TerminateJobObject((HANDLE)spawn.process_tree, 1) : TerminateProcess((HANDLE)spawn.handle, 1);
            if (!terminated)
            {
                result.process_tree_cleanup_failed = 1;
            }
        }

        DWORD wait_result = WaitForSingleObject(spawn.handle, INFINITE);
        if (wait_result == WAIT_OBJECT_0)
        {
            DWORD exit_code;
            if (GetExitCodeProcess(spawn.handle, &exit_code))
            {
                result.platform_status = exit_code;
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
        if (spawn.process_tree)
        {
            if (!TerminateJobObject((HANDLE)spawn.process_tree, 1))
            {
                result.process_tree_cleanup_failed = 1;
            }
            if (!os_windows_job_wait_empty((HANDLE)spawn.process_tree, 5000000))
            {
                result.process_tree_cleanup_failed = 1;
            }
            CloseHandle((HANDLE)spawn.process_tree);
        }
        CloseHandle(spawn.handle);
        if (result.process_tree_cleanup_failed)
        {
            result.result = PROCESS_RESULT_FAILED;
        }
#else
        pid_t pid = (pid_t)(u64)spawn.handle;
        int status = 0;
        struct rusage usage = {0};
        pid_t wait_result = -1;
        bool wait_failed = false;
        OsProcessGroupWaitState group_state = {.reservation = {.leader = spawn.process_group ? pid : 0}};

        // The parent never writes to a captured stdin; close the write end up
        // front so a child reading stdin sees EOF instead of blocking forever.
        if (spawn.pipes[STANDARD_STREAM_INPUT][1])
        {
            close(generic_fd_to_posix(spawn.pipes[STANDARD_STREAM_INPUT][1]));
            spawn.pipes[STANDARD_STREAM_INPUT][1] = 0;
        }

        int read_pipes[(u64)STANDARD_STREAM_COUNT];
        u64 quiescent_capture_remaining[(u64)STANDARD_STREAM_COUNT] = {0};
        u64 open_pipe_count = 0;
        bool quiescent_capture_snapshot = false;
        bool capture_failed = false;
        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            OsFileDescriptor* generic_read_pipe = spawn.pipes[stream][0];
            read_pipes[stream] = generic_read_pipe ? generic_fd_to_posix(generic_read_pipe) : -1;
            captured[stream] = read_pipes[stream] >= 0;
            open_pipe_count += captured[stream];
        }

        while (open_pipe_count)
        {
            if (spawn.process_group)
            {
                bool deadline_expired = deadline && !os_process_deadline_milliseconds(deadline, 1);
                os_process_group_wait_step(scratch.arena, spawn, timed_out || deadline_expired, &group_state);
                if (deadline_expired && group_state.outcome == OS_PROCESS_GROUP_WAIT_ACTIVE) { timed_out = true; }
                if (group_state.outcome == OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST ||
                    group_state.outcome == OS_PROCESS_GROUP_WAIT_RETAINED_FAILURE) { break; }
                if (group_state.outcome == OS_PROCESS_GROUP_WAIT_QUIESCENT_RESERVED)
                {
                    // A descriptor can outlive the owned group when an
                    // unrelated process inherited its writer. Snapshot the
                    // finite bytes already buffered at the quiescence proof,
                    // drain exactly that snapshot, then close without waiting
                    // for foreign EOF or accepting later foreign writes.
                    if (!quiescent_capture_snapshot)
                    {
                        quiescent_capture_snapshot = true;
                        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
                        {
                            if (read_pipes[stream] >= 0)
                            {
                                int available = 0;
                                if (ioctl(read_pipes[stream], FIONREAD, &available) == 0 && available >= 0)
                                {
                                    quiescent_capture_remaining[stream] = (u64)available;
                                }
                                else
                                {
                                    capture_failed = true;
                                }
                            }
                        }
                    }
                    for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
                    {
                        if (read_pipes[stream] >= 0 && quiescent_capture_remaining[stream])
                        {
                            u8 buffer[16 * 1024];
                            u64 requested = quiescent_capture_remaining[stream] < sizeof(buffer)
                                ? quiescent_capture_remaining[stream] : sizeof(buffer);
                            ssize_t read_result = read(read_pipes[stream], buffer, (size_t)requested);
                            if (read_result > 0)
                            {
                                pipe_capture_append(scratch.arena, &captures[stream], spawn, &result, (StandardStream)stream, buffer, (u64)read_result);
                                quiescent_capture_remaining[stream] -= (u64)read_result;
                            }
                            else if (read_result == 0 || errno != EINTR)
                            {
                                capture_failed = true;
                                quiescent_capture_remaining[stream] = 0;
                            }
                        }
                        if (read_pipes[stream] >= 0 && !quiescent_capture_remaining[stream])
                        {
                            capture_failed = close(read_pipes[stream]) != 0 || capture_failed;
                            read_pipes[stream] = -1;
                            open_pipe_count -= 1;
                        }
                    }
                    continue;
                }
            }
            else if (deadline && !os_process_deadline_milliseconds(deadline, 1))
            {
                timed_out = true;
                break;
            }
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

            u64 poll_milliseconds = os_process_deadline_milliseconds(deadline, (u64)-1);
            if (spawn.process_group && (poll_milliseconds == (u64)-1 || poll_milliseconds > 10 || (timed_out && !poll_milliseconds)))
            {
                poll_milliseconds = 10;
            }
            int poll_result = poll(poll_fds, poll_count, (int)poll_milliseconds);
            int poll_error = errno;
#if BUSTER_INCLUDE_TESTS
            if (!spawn.process_group && poll_result > 0 && test_expire_deadline_after_ready)
            {
                test_expire_deadline_after_ready = false;
                deadline = 1;
            }
#endif
            // A continuously readable writer must not postpone the deadline.
            // Group waits force cleanup, snapshot buffered bytes at proven
            // quiescence, and drain that finite snapshot; the ordinary
            // non-group path consumes this ready batch before enforcing it.
            if (spawn.process_group)
            {
                bool deadline_expired = deadline && !os_process_deadline_milliseconds(deadline, 1);
                os_process_group_wait_step(scratch.arena, spawn, timed_out || deadline_expired, &group_state);
                if (deadline_expired && group_state.outcome == OS_PROCESS_GROUP_WAIT_ACTIVE) { timed_out = true; }
                if (group_state.outcome == OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST ||
                    group_state.outcome == OS_PROCESS_GROUP_WAIT_RETAINED_FAILURE) { break; }
                if (group_state.outcome == OS_PROCESS_GROUP_WAIT_QUIESCENT_RESERVED) { continue; }
            }
            if (poll_result < 0)
            {
                if (poll_error == EINTR)
                {
                    continue;
                }
                errno = poll_error;
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
                    pipe_capture_append(scratch.arena, &captures[stream], spawn, &result, (StandardStream)stream, buffer, (u64)read_result);
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
            if (!spawn.process_group && deadline && !os_process_deadline_milliseconds(deadline, 1))
            {
                timed_out = true;
                break;
            }
        }

        for (u64 stream = 0; stream < STANDARD_STREAM_COUNT; stream += 1)
        {
            if (captured[stream])
            {
                result.streams[stream] = pipe_capture_flatten(arena, &captures[stream]);
            }
            if (read_pipes[stream] >= 0)
            {
                close(read_pipes[stream]);
                read_pipes[stream] = -1;
            }
        }

        if (spawn.process_group)
        {
            // Keep the WNOWAIT leader reservation while this lane alone drives
            // cancellation, helper cleanup, and exit observation. Finite slices
            // let flag-only signal handlers wake progress without owning PGIDs.
            while (group_state.outcome == OS_PROCESS_GROUP_WAIT_ACTIVE)
            {
                bool deadline_expired = deadline && !os_process_deadline_milliseconds(deadline, 1);
                os_process_group_wait_step(scratch.arena, spawn, timed_out || deadline_expired, &group_state);
                if (deadline_expired && group_state.outcome == OS_PROCESS_GROUP_WAIT_ACTIVE) { timed_out = true; }
                if (group_state.outcome == OS_PROCESS_GROUP_WAIT_ACTIVE)
                {
                    poll(0, 0, 10);
                }
            }
            wait_failed = group_state.outcome != OS_PROCESS_GROUP_WAIT_QUIESCENT_RESERVED;
            if (!wait_failed)
            {
                ProcessGroupControlState* control = spawn.process_group_control;
                if (control && control->test_cancel_before_reap && control->cancellation_signal &&
                    !process_control_atomic_load(control->cancellation_signal))
                {
                    raise(SIGTERM);
                }
                // Cleanup was already proven while the leader reserved the ID.
                // A pre-reap cancellation only changes shared flags; signalling
                // again would be redundant and would enlarge the ownership surface.
                // Revalidate immediately before the nonblocking exact-child reap.
                OsProcessGroupObservation before_reap = os_process_group_observe(&group_state.reservation, true);
                if (!before_reap.valid)
                {
                    os_process_group_wait_publish_cleanup_failure(spawn, &group_state);
                    group_state.outcome = OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST;
                    wait_failed = true;
                }
                else if (!before_reap.exited)
                {
                    os_process_group_wait_publish_cleanup_failure(spawn, &group_state);
                    group_state.outcome = OS_PROCESS_GROUP_WAIT_RETAINED_FAILURE;
                    wait_failed = true;
                }
                else
                {
                    do
                    {
                        wait_result = wait4(pid, &status, WNOHANG, &usage);
                    } while (wait_result < 0 && errno == EINTR);
                    if (wait_result == pid)
                    {
                        os_process_group_reservation_release(&group_state.reservation);
                    }
                    else
                    {
                        if (wait_result < 0 && (errno == ECHILD || errno == ESRCH))
                        {
                            os_process_group_wait_publish_cleanup_failure(spawn, &group_state);
                            group_state.outcome = OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST;
                        }
                        else
                        {
                            os_process_group_wait_publish_cleanup_failure(spawn, &group_state);
                            group_state.outcome = OS_PROCESS_GROUP_WAIT_RETAINED_FAILURE;
                        }
                        wait_failed = true;
                    }
                }
            }
        }
        else
        {
            // A child with no captured stream never entered the drain loop, so
            // enforce its deadline with nonblocking waits before the final reap.
            while (!timed_out && deadline && wait_result != pid && !wait_failed)
            {
                wait_result = wait4(pid, &status, WNOHANG, &usage);
                if (wait_result < 0 && errno != EINTR) { wait_failed = true; }
                if (wait_result == pid || wait_failed) { break; }
                u64 remaining = os_process_deadline_milliseconds(deadline, 1);
                if (!remaining) { timed_out = true; }
                else { poll(0, 0, (int)(remaining < 10 ? remaining : 10)); }
            }
            if (timed_out)
            {
                // The status below then describes this kill rather than the
                // child's progress, so `timed_out` is reported separately.
                kill(pid, SIGKILL);
                wait_result = -1;
            }
            if (wait_result != pid && !wait_failed)
            {
                do
                {
                    wait_result = wait4(pid, &status, 0, &usage);
                } while (wait_result < 0 && errno == EINTR);
                if (wait_result != pid) { wait_failed = true; }
            }
        }

        if (program_flag_get(PROGRAM_FLAG_VERBOSE))
        {
            string_print(S8("Process [{s32}]: Time (user): {s64}:{s64} s,us, (system): {s64}:{s64} s,us. Max RSS: {s64} KB. PF (soft): {s64}, (hard): {s64}. "
                            "Block (input): {s64}, (output): {s64}. CTX SW (vol): {s64}, (invol): {s64}\n"),
                         pid, usage.ru_utime.tv_sec, usage.ru_utime.tv_usec, usage.ru_stime.tv_sec, usage.ru_stime.tv_usec, usage.ru_maxrss, usage.ru_minflt,
                         usage.ru_majflt, usage.ru_inblock, usage.ru_oublock, usage.ru_nvcsw, usage.ru_nivcsw);
        }

        if (wait_result == pid)
        {
            result.platform_status = (u32)status;
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
        if (capture_failed) { wait_failed = true; }
        if (wait_failed) { result.result = PROCESS_RESULT_FAILED; }
        result.process_group_reservation_retained = spawn.process_group && wait_result != pid;
        result.process_group_ownership_lost = result.process_group_reservation_retained && group_state.outcome == OS_PROCESS_GROUP_WAIT_OWNERSHIP_LOST;
        result.process_tree_cleanup_failed = result.process_group_reservation_retained || result.process_group_ownership_lost;
#endif
        if (result.capture_failed)
        {
            result.result = PROCESS_RESULT_FAILED;
        }
        if (timed_out)
        {
            result.timed_out = 1;
            result.termination_requested = 1;
            result.forcibly_terminated = 1;
            result.result = PROCESS_RESULT_FAILED;
        }
        scratch_end(scratch);
    }

    return result;
}

#if BUSTER_INCLUDE_TESTS && (BUSTER_LINUX || BUSTER_MACOS)
bool os_process_group_escaped_capture_self_test(Arena* arena)
{
    int capture_pipe[2] = {-1, -1};
    int ready_pipe[2] = {-1, -1};
    bool pipes_ready = pipe(capture_pipe) == 0 && pipe(ready_pipe) == 0;
    pid_t leader = -1;
    pid_t holder = -1;
    bool group_ready = false;
    bool leader_exited = false;
    bool holder_ready = false;
    ProcessWaitResult waited = {0};
    u64 elapsed = (u64)-1;

    if (pipes_ready)
    {
        leader = fork();
        if (leader == 0)
        {
            close(capture_pipe[0]);
            close(ready_pipe[0]);
            int group_status = setpgid(0, 0);
            char output[] = "owned";
            char ready = 'L';
            ssize_t output_write = -1;
            ssize_t ready_write = -1;
            if (group_status == 0)
            {
                do
                {
                    output_write = write(capture_pipe[1], output, sizeof(output) - 1);
                } while (output_write < 0 && errno == EINTR);
                do
                {
                    ready_write = write(ready_pipe[1], &ready, sizeof(ready));
                } while (ready_write < 0 && errno == EINTR);
            }
            close(capture_pipe[1]);
            close(ready_pipe[1]);
            _exit(group_status == 0 && output_write == (ssize_t)(sizeof(output) - 1) && ready_write == sizeof(ready) ? 0 : 101);
        }
        if (leader > 0)
        {
            struct pollfd leader_poll = {.fd = ready_pipe[0], .events = POLLIN};
            int leader_poll_status = poll(&leader_poll, 1, 1000);
            char leader_ready = 0;
            ssize_t leader_read = leader_poll_status > 0 ? read(ready_pipe[0], &leader_ready, sizeof(leader_ready)) : -1;
            group_ready = leader_read == sizeof(leader_ready) && leader_ready == 'L';
            OsProcessGroupReservation reservation = {.leader = leader};
            for (u32 attempt = 0; group_ready && !leader_exited && attempt < 1000; attempt += 1)
            {
                OsProcessGroupObservation observation = os_process_group_observe(&reservation, true);
                leader_exited = observation.valid && observation.exited;
                if (!leader_exited) { poll(0, 0, 1); }
            }
            if (leader_exited) { holder = fork(); }
            if (holder == 0)
            {
                close(capture_pipe[0]);
                close(ready_pipe[0]);
                char ready = 'H';
                ssize_t ready_write;
                do
                {
                    ready_write = write(ready_pipe[1], &ready, sizeof(ready));
                } while (ready_write < 0 && errno == EINTR);
                close(ready_pipe[1]);
                // Bound a broken wait without hanging the suite forever. A
                // correct wait returns while this unrelated writer remains.
                poll(0, 0, 3000);
                close(capture_pipe[1]);
                _exit(ready_write == sizeof(ready) ? 0 : 102);
            }
        }
    }

    if (capture_pipe[1] >= 0) { close(capture_pipe[1]); }
    if (ready_pipe[1] >= 0) { close(ready_pipe[1]); }
    if (group_ready && holder > 0)
    {
        struct pollfd ready_poll = {.fd = ready_pipe[0], .events = POLLIN};
        int ready_status = poll(&ready_poll, 1, 1000);
        char ready = 0;
        ssize_t ready_read = ready_status > 0 ? read(ready_pipe[0], &ready, sizeof(ready)) : -1;
        holder_ready = ready_read == sizeof(ready) && ready == 'H';
    }
    if (ready_pipe[0] >= 0) { close(ready_pipe[0]); }

    if (holder_ready)
    {
        ProcessSpawnResult spawn = {
            .handle = (OsProcessHandle*)(u64)leader,
            .process_group = true,
        };
        spawn.pipes[STANDARD_STREAM_OUTPUT][0] = posix_fd_to_generic_fd(capture_pipe[0]);
        capture_pipe[0] = -1;
        u64 start = os_now_microseconds();
        // The exact leader is already a reserved zombie. Even an immediately
        // expired deadline must not relabel its successful exit while the
        // finite buffered snapshot is drained.
        waited = os_process_wait_deadline(arena, spawn, 1);
        elapsed = os_now_microseconds() - start;
        if (!waited.process_group_reservation_retained) { leader = -1; }
    }

    if (capture_pipe[0] >= 0) { close(capture_pipe[0]); }
    bool holder_stopped = holder <= 0;
    if (holder > 0)
    {
        int kill_status = kill(holder, SIGKILL);
        bool kill_valid = kill_status == 0 || errno == ESRCH;
        int holder_status = 0;
        pid_t holder_wait;
        do
        {
            holder_wait = waitpid(holder, &holder_status, 0);
        } while (holder_wait < 0 && errno == EINTR);
        holder_stopped = kill_valid && holder_wait == holder;
    }
    if (leader > 0)
    {
        kill(leader, SIGKILL);
        int leader_status = 0;
        while (waitpid(leader, &leader_status, 0) < 0 && errno == EINTR) {}
    }

    bool output_matches = waited.streams[STANDARD_STREAM_OUTPUT].length == 5 &&
        !memcmp(waited.streams[STANDARD_STREAM_OUTPUT].pointer, "owned", 5);
    bool result = pipes_ready && group_ready && leader_exited && holder_ready && holder_stopped && waited.result == PROCESS_RESULT_SUCCESS &&
        !waited.timed_out && output_matches && elapsed < 1000000 &&
        !waited.process_group_reservation_retained && !waited.process_group_ownership_lost;
    return result;
}
#endif

ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn)
{
    return os_process_wait_deadline(arena, spawn, 0);
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
    FileStats stats = os_file_get_stats(file_descriptor, (FileStatsOptions){.size = 1});
    return stats.valid ? stats.size : UINT64_MAX;
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
#if BUSTER_BENCH_ALLOCATIONS
    arena_benchmark_event(ARENA_BENCHMARK_OS_UNRESERVE, S8(__FILE__), S8(__func__), __LINE__, size, 0, 0, 0, result);
#endif
    return result;
}

OsModuleHandle* os_dynamic_library_load(String8 library)
{
    OsModuleHandle* result = {0};
    BUSTER_VALIDATE(BUSTER_SLICE_IS_ZERO_TERMINATED(library));

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
    TemporalArena scratch = scratch_begin(0, 0);
    String8 terminated_symbol = string_duplicate_arena(scratch.arena, symbol, true);
    OsSymbol* result = {0};

#if defined(_WIN32)
    result = (OsSymbol*)GetProcAddress((HMODULE)module, terminated_symbol.pointer);
#else
    result = (OsSymbol*)dlsym((void*)module, terminated_symbol.pointer);
#endif

    scratch_end(scratch);
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

// Resident set size of this process, right now. Used to budget a memory limit
// against what a process has *already* used rather than from zero; returns 0
// where the platform does not report it, which every caller has to treat as
// "no information" rather than "no memory".
u64 os_get_resident_memory_size(void)
{
    u64 result = 0;
#if defined(__linux__)
    // /proc/self/statm is "size resident shared ..." in pages. The second
    // field is what /proc/self/status calls VmRSS, without the string parse.
    int statm_fd = open("/proc/self/statm", O_RDONLY);
    if (statm_fd >= 0)
    {
        char statm_buffer[128];
        ssize_t read_byte_count = read(statm_fd, statm_buffer, sizeof(statm_buffer) - 1);
        close(statm_fd);
        if (read_byte_count > 0)
        {
            statm_buffer[read_byte_count] = 0;
            // "size resident shared ...": skip one field to land on resident.
            u64 field_index = 0;
            u64 cursor = 0;
            while (field_index < 1 && statm_buffer[cursor])
            {
                while (statm_buffer[cursor] && statm_buffer[cursor] != ' ')
                {
                    cursor += 1;
                }
                while (statm_buffer[cursor] == ' ')
                {
                    cursor += 1;
                }
                field_index += 1;
            }
            u64 pages = 0;
            bool any = false;
            while (statm_buffer[cursor] >= '0' && statm_buffer[cursor] <= '9')
            {
                pages = pages * 10 + (u64)(statm_buffer[cursor] - '0');
                cursor += 1;
                any = true;
            }
            if (any)
            {
                result = pages * os_get_page_size();
            }
        }
    }
#elif defined(__APPLE__)
    // ru_maxrss is a peak rather than the current value, and is in bytes on
    // Apple where Linux reports kilobytes.
    struct rusage usage;
    memset(&usage, 0, sizeof(usage));
    if (getrusage(RUSAGE_SELF, &usage) == 0)
    {
        result = (u64)usage.ru_maxrss;
    }
#else
    // Resolved at runtime for the same reason GlobalMemoryStatusEx is below:
    // tcc's bundled import stubs do not carry it. K32GetProcessMemoryInfo is
    // the kernel32 export, so no psapi import library is needed either.
    //
    // The counters are declared here rather than taken from psapi.h, which
    // tcc's bundled headers do not ship: build.c includes this file and is
    // bootstrapped with tcc, so naming PROCESS_MEMORY_COUNTERS is an "invalid
    // type" there long before any Windows compiler sees it. The layout is
    // fixed by the ABI, and `cb` tells the callee which version it received.
    typedef struct
    {
        DWORD cb;
        DWORD page_fault_count;
        SIZE_T peak_working_set_size;
        SIZE_T working_set_size;
        SIZE_T quota_peak_paged_pool_usage;
        SIZE_T quota_paged_pool_usage;
        SIZE_T quota_peak_non_paged_pool_usage;
        SIZE_T quota_non_paged_pool_usage;
        SIZE_T pagefile_usage;
        SIZE_T peak_pagefile_usage;
    } OsProcessMemoryCounters;
    typedef BOOL(WINAPI * GetProcessMemoryInfoProc)(HANDLE, OsProcessMemoryCounters*, DWORD);
    GetProcessMemoryInfoProc get_process_memory_info =
        (GetProcessMemoryInfoProc)(void (*)(void))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
    OsProcessMemoryCounters counters = {0};
    counters.cb = sizeof(counters);
    if (get_process_memory_info && get_process_memory_info(GetCurrentProcess(), &counters, sizeof(counters)))
    {
        result = (u64)counters.working_set_size;
    }
#endif
    return result;
}

u64 os_get_physical_memory_size(void)
{
    u64 result = 0;
#if defined(__linux__)
    struct sysinfo info;
    memset(&info, 0, sizeof(info));
    if (sysinfo(&info) == 0)
    {
        result = (u64)info.totalram * (u64)info.mem_unit;
    }
#elif defined(__APPLE__)
    u64 memory_size = 0;
    size_t size = sizeof(memory_size);
    if (sysctlbyname("hw.memsize", &memory_size, &size, 0, 0) == 0)
    {
        result = memory_size;
    }
#else
    // The build driver compiles this file with tcc on Windows, and tcc's
    // bundled kernel32 import stubs lack GlobalMemoryStatusEx (the header
    // declares it; the link fails). Resolve it at runtime for every Windows
    // compiler instead of keeping a second import-based path.
    typedef BOOL(WINAPI* GlobalMemoryStatusExProc)(MEMORYSTATUSEX*);
    GlobalMemoryStatusExProc global_memory_status_ex =
        (GlobalMemoryStatusExProc)(void (*)(void))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GlobalMemoryStatusEx");
    MEMORYSTATUSEX status = {0};
    status.dwLength = sizeof(status);
    if (global_memory_status_ex && global_memory_status_ex(&status))
    {
        result = status.ullTotalPhys;
    }
#endif
    return result;
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
#if defined(__linux__) || defined(__APPLE__)
    TemporalArena scratch = scratch_begin(0, 0);
    String8 terminated_name = string_duplicate_arena(scratch.arena, thread_name, true);
#if defined(__linux__)
    pthread_setname_np(pthread_self(), terminated_name.pointer);
#else
    pthread_setname_np(terminated_name.pointer);
#endif
    scratch_end(scratch);
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
        BUSTER_CHECK_RAW(arenas[i] != 0);
    }

    ThreadContext* result = arena_allocate(arenas[0], ThreadContext, 1);
    memset(result, 0, sizeof(*result));
    memcpy(result->arenas, arenas, sizeof(arenas));
    result->lane_context.lane_count = 1;
    return result;
}

void thread_context_release(ThreadContext* thread_context)
{
    if (!thread_context)
    {
        return;
    }
#if !BUSTER_SINGLE_THREADED
    // Resident lane workers own OS entities and their own thread contexts.
    // Join them while the entity mutex and arenas are still alive.
    lane_gang_release(thread_context);
#endif
    ThreadContext* selected = thread_context_selected();
    if (selected == thread_context)
    {
        thread_context_select(0);
        // The context owns the scratch arenas that contain it. TLS must stop
        // referring to that storage before any arena is unmapped. Reported
        // without scratch: there is deliberately no selected context here.
        BUSTER_CHECK_RAW(thread_context_selected() == 0);
    }
    Arena* arenas[BUSTER_ARRAY_LENGTH(thread_context->arenas)];
    memcpy(arenas, thread_context->arenas, sizeof(arenas));
    for (u64 i = BUSTER_ARRAY_LENGTH(arenas); i > 0; i -= 1)
    {
        arena_destroy(arenas[i - 1], 1);
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
    // Releasing the selected context deliberately leaves TLS empty. Callers
    // must select another context before using any scratch-backed operation.
    // The raw reporter is required: the formatted one allocates from the
    // scratch arena this call is failing to produce.
    BUSTER_CHECK_RAW(thread_context != 0);
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

u64 lane_index(void)
{
    u64 result = thread_context_selected()->lane_context.lane_index;
    return result;
}

u64 lane_count(void)
{
    u64 result = thread_context_selected()->lane_context.lane_count;
    return result;
}

void lane_sync(void)
{
    // A single-threaded build only ever has one lane, so the barrier (and its
    // condition-variable dependency) is compiled out with nothing to replace.
#if !BUSTER_SINGLE_THREADED
    LaneContext* lane_context = &thread_context_selected()->lane_context;
    if (lane_context->lane_count > 1)
    {
        os_barrier_wait(lane_context->barrier);
    }
#endif
}

void lane_broadcast(void* value_pointer, u64 value_size, u64 source_lane_index)
{
    LaneContext* lane_context = &thread_context_selected()->lane_context;
    BUSTER_CHECK(value_size >= 1 && value_size <= sizeof(*lane_context->broadcast_memory));
    BUSTER_CHECK(source_lane_index < lane_context->lane_count);
#if !BUSTER_SINGLE_THREADED
    if (lane_context->lane_count > 1)
    {
        if (lane_context->lane_index == source_lane_index)
        {
            memcpy(lane_context->broadcast_memory, value_pointer, value_size);
        }
        os_barrier_wait(lane_context->barrier);
        if (lane_context->lane_index != source_lane_index)
        {
            memcpy(value_pointer, lane_context->broadcast_memory, value_size);
        }
        // The slot may not be reused by a later broadcast until every lane has
        // copied this one out.
        os_barrier_wait(lane_context->barrier);
    }
#else
    BUSTER_UNUSED(value_pointer);
#endif
}

LaneRange lane_range(u64 item_count)
{
    LaneContext* lane_context = &thread_context_selected()->lane_context;
    u64 base_length = item_count / lane_context->lane_count;
    u64 remainder = item_count % lane_context->lane_count;
    u64 start = lane_context->lane_index * base_length + BUSTER_MIN(lane_context->lane_index, remainder);
    u64 length = base_length + (lane_context->lane_index < remainder);
    LaneRange result = {
        .start = start,
        .end = start + length,
    };
    return result;
}

#if !BUSTER_SINGLE_THREADED
typedef struct LaneStartupGate LaneStartupGate;
struct LaneStartupGate
{
    OsMutexHandle* mutex;
    bool cancelled;
};

typedef struct LanePersistentWorkerStart LanePersistentWorkerStart;
struct LanePersistentWorkerStart
{
    LaneGang* gang;
    u64 lane_index;
};

struct LaneGang
{
    LaneStartupGate startup;
    OsBarrierHandle* dispatch_barrier;
    OsBarrierHandle* active_barrier;
    LanePersistentWorkerStart* starts;
    OsThreadHandle** handles;
    ThreadCallback* callback;
    void* argument;
    u64 broadcast_memory;
    u64 capacity;
    u64 active_count;
    bool shutdown;
    bool running;
    u8 reserved[6];
};

BUSTER_GLOBAL_LOCAL ThreadReturnType lane_persistent_worker_entry_point(void* argument)
{
    LanePersistentWorkerStart* start = (LanePersistentWorkerStart*)argument;
    LaneGang* gang = start->gang;
    os_mutex_lock(gang->startup.mutex);
    bool cancelled = gang->startup.cancelled;
    os_mutex_unlock(gang->startup.mutex);
    if (!cancelled)
    {
        ThreadContext* thread_context = thread_context_selected();
        os_thread_set_name(S8("lane_worker"));
        for (;;)
        {
            os_barrier_wait(gang->dispatch_barrier);
            if (gang->shutdown)
            {
                break;
            }
            if (start->lane_index < gang->active_count)
            {
                LaneContext saved_lane = thread_context->lane_context;
                u64 saved_arena_positions[SCRATCH_ARENA_COUNT];
                for (u64 arena_index = 0; arena_index < SCRATCH_ARENA_COUNT; arena_index += 1)
                {
                    saved_arena_positions[arena_index] = thread_context->arenas[arena_index]->position;
                }
                thread_context->lane_context = (LaneContext){
                    .lane_index = start->lane_index,
                    .lane_count = gang->active_count,
                    .barrier = gang->active_barrier,
                    .broadcast_memory = &gang->broadcast_memory,
                };
                gang->callback(gang->argument);
                thread_context->lane_context = saved_lane;
                // A fresh worker context used to disappear after every lane_run.
                // Reset resident worker scratch so callbacks retain that lifetime
                // contract and cannot accumulate allocation across dispatches. A
                // large high-water mark is unmapped as well; otherwise a long-lived
                // IDE would retain one peak scratch commitment per resident lane.
                for (u64 arena_index = 0; arena_index < SCRATCH_ARENA_COUNT; arena_index += 1)
                {
                    Arena* scratch = thread_context->arenas[arena_index];
                    if (scratch->os_position - saved_arena_positions[arena_index] > BUSTER_MB(16))
                    {
                        // arenas[0] owns ThreadContext itself, so preserve its
                        // stable prefix and decommit only the unused tail.
                        BUSTER_VALIDATE(arena_set_position_and_decommit(scratch, saved_arena_positions[arena_index]));
                    }
                    else
                    {
                        arena_set_position(scratch, saved_arena_positions[arena_index]);
                    }
                }
            }
            os_barrier_wait(gang->dispatch_barrier);
        }
    }
}

BUSTER_GLOBAL_LOCAL LaneGang* lane_gang_create(ThreadContext* owner, u64 count)
{
    BUSTER_CHECK(owner != 0);
    BUSTER_CHECK(count > 1 && count <= UINT32_MAX);
    BUSTER_CHECK(owner->lane_gang == 0 && owner->lane_arena == 0);

    LaneGang* result = 0;
    Arena* arena = arena_create((ArenaCreation){0});
    if (arena)
    {
        LaneGang* gang = arena_allocate(arena, LaneGang, 1);
        memset(gang, 0, sizeof(*gang));
        gang->capacity = count;
        gang->startup.mutex = os_mutex_create();
        if (gang->startup.mutex)
        {
            gang->dispatch_barrier = os_barrier_create((u32)count);
            u64 next_lane = 1;
            bool complete = gang->dispatch_barrier != 0;
            if (complete)
            {
                gang->starts = arena_allocate(arena, LanePersistentWorkerStart, count);
                gang->handles = arena_allocate(arena, OsThreadHandle*, count);
                memset(gang->handles, 0, sizeof(*gang->handles) * count);
            }
            // Allocation failure can enter the fatal reporter and perform
            // diagnostic I/O. Finish allocations before locking the startup gate.
            os_mutex_lock(gang->startup.mutex);
            if (complete)
            {
                while (complete && next_lane < count)
                {
                    gang->starts[next_lane] = (LanePersistentWorkerStart){
                        .gang = gang,
                        .lane_index = next_lane,
                    };
                    gang->handles[next_lane] = os_thread_create((ThreadCreateOptions){
                        .callback = &lane_persistent_worker_entry_point,
                        .argument = &gang->starts[next_lane],
                    });
                    complete = gang->handles[next_lane] != 0;
                    next_lane += complete;
                }
            }

            gang->startup.cancelled = !complete;
            if (complete)
            {
                owner->lane_arena = arena;
                owner->lane_gang = gang;
                result = gang;
            }
            os_mutex_unlock(gang->startup.mutex);

            if (!complete)
            {
                for (u64 lane = 1; lane < next_lane; lane += 1)
                {
                    BUSTER_VALIDATE(os_thread_join(gang->handles[lane]));
                }
                if (gang->dispatch_barrier)
                {
                    os_barrier_destroy(gang->dispatch_barrier);
                }
                os_mutex_destroy(gang->startup.mutex);
                BUSTER_VALIDATE(arena_destroy(arena, 1));
            }
        }
        else
        {
            BUSTER_VALIDATE(arena_destroy(arena, 1));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void lane_gang_destroy(LaneGang* gang)
{
    if (!gang)
    {
        return;
    }
    BUSTER_CHECK(!gang->running);
    gang->shutdown = true;
    os_barrier_wait(gang->dispatch_barrier);
    for (u64 lane = 1; lane < gang->capacity; lane += 1)
    {
        BUSTER_VALIDATE(os_thread_join(gang->handles[lane]));
    }
    if (gang->active_barrier)
    {
        os_barrier_destroy(gang->active_barrier);
    }
    os_barrier_destroy(gang->dispatch_barrier);
    os_mutex_destroy(gang->startup.mutex);
}

void lane_gang_release(ThreadContext* thread_context)
{
    if (thread_context && thread_context->lane_gang)
    {
        lane_gang_destroy(thread_context->lane_gang);
        thread_context->lane_gang = 0;
        Arena* lane_arena = thread_context->lane_arena;
        thread_context->lane_arena = 0;
        BUSTER_VALIDATE(arena_destroy(lane_arena, 1));
    }
}

BUSTER_GLOBAL_LOCAL void lane_gang_dispatch(ThreadContext* thread_context, LaneGang* gang, u64 count, ThreadCallback* callback, void* argument)
{
    BUSTER_CHECK(thread_context != 0 && gang != 0 && !gang->running);
    BUSTER_CHECK(count > 1 && count <= gang->capacity);
    if (!gang->active_barrier || gang->active_count != count)
    {
        OsBarrierHandle* replacement = os_barrier_create((u32)count);
        BUSTER_VALIDATE(replacement != 0);
        if (gang->active_barrier)
        {
            os_barrier_destroy(gang->active_barrier);
        }
        gang->active_barrier = replacement;
    }
    gang->active_count = count;
    gang->broadcast_memory = 0;
    gang->callback = callback;
    gang->argument = argument;
    gang->running = true;

    LaneContext saved = thread_context->lane_context;
    thread_context->lane_context = (LaneContext){
        .lane_index = 0,
        .lane_count = count,
        .barrier = gang->active_barrier,
        .broadcast_memory = &gang->broadcast_memory,
    };
    // The first generation publishes this dispatch; the second replaces the
    // old per-call joins as its completion and visibility edge.
    os_barrier_wait(gang->dispatch_barrier);
    callback(argument);
    os_barrier_wait(gang->dispatch_barrier);
    thread_context->lane_context = saved;

    gang->running = false;
    gang->callback = 0;
    gang->argument = 0;
}

typedef struct LaneFreshWorkerStart LaneFreshWorkerStart;
struct LaneFreshWorkerStart
{
    LaneStartupGate* startup;
    LaneContext lane_context;
    ThreadCallback* callback;
    void* argument;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType lane_fresh_worker_entry_point(void* argument)
{
    LaneFreshWorkerStart* start = (LaneFreshWorkerStart*)argument;
    os_mutex_lock(start->startup->mutex);
    bool cancelled = start->startup->cancelled;
    os_mutex_unlock(start->startup->mutex);
    if (!cancelled)
    {
        thread_context_selected()->lane_context = start->lane_context;
        start->callback(start->argument);
    }
}

BUSTER_GLOBAL_LOCAL bool lane_run_fresh(ThreadContext* thread_context, u64 count, ThreadCallback* callback, void* argument)
{
    TemporalArena temporary = scratch_begin(0, 0);
    u64 broadcast_memory = 0;
    LaneStartupGate startup = {.mutex = os_mutex_create()};
    OsBarrierHandle* barrier = 0;
    LaneFreshWorkerStart* starts = 0;
    OsThreadHandle** handles = 0;
    u64 next_lane = 1;
    bool result = false;

    if (startup.mutex)
    {
        barrier = os_barrier_create((u32)count);
        bool complete = barrier != 0;
        if (complete)
        {
            starts = arena_allocate(temporary.arena, LaneFreshWorkerStart, count);
            handles = arena_allocate(temporary.arena, OsThreadHandle*, count);
            memset(handles, 0, sizeof(*handles) * count);
        }
        // Allocate before taking the startup gate: allocation failure may
        // enter the fatal reporter, which performs blocking diagnostic I/O.
        os_mutex_lock(startup.mutex);
        if (complete)
        {
            while (complete && next_lane < count)
            {
                starts[next_lane] = (LaneFreshWorkerStart){
                    .startup = &startup,
                    .lane_context =
                        {
                            .lane_index = next_lane,
                            .lane_count = count,
                            .barrier = barrier,
                            .broadcast_memory = &broadcast_memory,
                        },
                    .callback = callback,
                    .argument = argument,
                };
                handles[next_lane] = os_thread_create((ThreadCreateOptions){
                    .callback = &lane_fresh_worker_entry_point,
                    .argument = &starts[next_lane],
                });
                complete = handles[next_lane] != 0;
                next_lane += complete;
            }
        }

        LaneContext saved = thread_context->lane_context;
        startup.cancelled = !complete;
        if (complete)
        {
            thread_context->lane_context = (LaneContext){
                .lane_index = 0,
                .lane_count = count,
                .barrier = barrier,
                .broadcast_memory = &broadcast_memory,
            };
        }
        os_mutex_unlock(startup.mutex);

        if (complete)
        {
            callback(argument);
            for (u64 lane = 1; lane < count; lane += 1)
            {
                BUSTER_VALIDATE(os_thread_join(handles[lane]));
            }
            thread_context->lane_context = saved;
            result = true;
        }
        else
        {
            for (u64 lane = 1; lane < next_lane; lane += 1)
            {
                BUSTER_VALIDATE(os_thread_join(handles[lane]));
            }
        }

        if (barrier)
        {
            os_barrier_destroy(barrier);
        }
        os_mutex_destroy(startup.mutex);
    }

    scratch_end(temporary);
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL ThreadReturnType os_resource_test_noop(void* argument)
{
    bool* called = (bool*)argument;
    if (called)
    {
        *called = true;
    }
}

bool os_resource_failure_self_test(void)
{
    ThreadContext* saved_context = thread_context_selected();
    ThreadContext* owner = thread_context_allocate();
    bool result = owner != 0;
    if (result)
    {
        thread_context_select(owner);

        os_resource_test_clear();
        os_resource_test_fail_on_call(OS_RESOURCE_TEST_BARRIER_CREATE, 0);
        LaneGang* gang = lane_gang_create(owner, 3);
        result = gang == 0 && owner->lane_gang == 0 && owner->lane_arena == 0 && os_is_only_live_thread();

        os_resource_test_clear();
        os_resource_test_fail_on_call(OS_RESOURCE_TEST_THREAD_CREATE, 1);
        gang = lane_gang_create(owner, 3);
        result = result && gang == 0 && owner->lane_gang == 0 && owner->lane_arena == 0 && os_is_only_live_thread();

        os_resource_test_clear();
        bool callback_called = false;
        os_resource_test_fail_on_call(OS_RESOURCE_TEST_THREAD_CREATE, 1);
        bool fresh = lane_run_fresh(owner, 3, &os_resource_test_noop, &callback_called);
        result = result && !fresh && !callback_called && os_is_only_live_thread();

        os_resource_test_clear();
        OsThreadHandle* handle = os_thread_create((ThreadCreateOptions){
            .callback = &os_resource_test_noop,
            .argument = 0,
        });
        result = result && handle != 0;
        if (handle)
        {
            os_resource_test_fail_on_call(OS_RESOURCE_TEST_THREAD_JOIN, 0);
            bool injected_join = os_thread_join(handle);
            os_resource_test_clear();
            bool retry_join = os_thread_join(handle);
            result = result && !injected_join && retry_join && os_is_only_live_thread();
        }

        thread_context_release(owner);
        thread_context_select(saved_context);
    }
    os_resource_test_clear();
    return result;
}
#endif
#endif

void lane_run(u64 lane_count_requested, ThreadCallback* callback, void* argument)
{
    ThreadContext* thread_context = thread_context_selected();
    BUSTER_CHECK(thread_context != 0);
    BUSTER_CHECK(callback != 0);

#if BUSTER_SINGLE_THREADED
    BUSTER_UNUSED(lane_count_requested);
    u64 count = 1;
#else
    u64 count = lane_count_requested ? lane_count_requested : os_get_logical_thread_count();
    BUSTER_CHECK(count <= UINT32_MAX);
#endif

    LaneContext saved = thread_context->lane_context;
    // One chain over the three ways to run: inline, a fresh short-lived gang, or
    // the resident one. Only the second and third exist when threading is on,
    // so the `else` arms live inside the same #if and the braces balance in
    // either configuration.
    if (count == 1)
    {
        thread_context->lane_context = (LaneContext){
            .lane_count = 1,
        };
        callback(argument);
        thread_context->lane_context = saved;
    }
#if !BUSTER_SINGLE_THREADED
    // A lane inside an active region gets an independent short-lived gang.
    // Re-entering the resident dispatch barriers would deadlock the outer
    // region and would break the documented nested-gang behavior.
    else if (saved.lane_count > 1 || (thread_context->lane_gang && thread_context->lane_gang->running))
    {
        BUSTER_VALIDATE(lane_run_fresh(thread_context, count, callback, argument));
    }
    else
    {
        if (!thread_context->lane_gang || thread_context->lane_gang->capacity < count)
        {
            lane_gang_release(thread_context);
            BUSTER_VALIDATE(lane_gang_create(thread_context, count) != 0);
        }
        lane_gang_dispatch(thread_context, thread_context->lane_gang, count, callback, argument);
    }
#endif
}

#if defined(_WIN32)
u64 os_performance_counter_frequency(void)
{
    // The application entry point prewarms this value. Foundation-only tools
    // may use clocks earlier; querying without publishing is safe on any lane.
    u64 frequency = os_state.frequency;
    if (!frequency)
    {
        LARGE_INTEGER native_frequency;
        BOOL success = QueryPerformanceFrequency(&native_frequency);
        BUSTER_VALIDATE(success && native_frequency.QuadPart > 0);
        frequency = (u64)native_frequency.QuadPart;
    }
    return frequency;
}
#endif

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
        u64 frequency = os_performance_counter_frequency();
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

#if defined(_WIN32)
// Only ASCII environment keys and executable suffixes use this comparison.
// Do not case-fold paths: directory case-sensitivity is a filesystem property.
BUSTER_GLOBAL_LOCAL bool os_windows_ascii_equal_ignore_case(String8 a, String8 b)
{
    bool result = a.length == b.length;
    for (u64 i = 0; result && i < a.length; i += 1)
    {
        u32 left = (u8)a.pointer[i];
        u32 right = (u8)b.pointer[i];
        if (left >= 'A' && left <= 'Z')
        {
            left += 'a' - 'A';
        }
        if (right >= 'A' && right <= 'Z')
        {
            right += 'a' - 'A';
        }
        result = left == right;
    }
    return result;
}
#endif

String8 executable_resolve_in_path(Arena* arena, String8 file)
{
    TemporalArena temp = scratch_begin(&arena, 1);

    String8 result = {0};
    String8 path_value = {0};
    bool path_present = false;
    String8* key_pointer = program_state->input.environment_keys.pointer;
    u64 key_length = program_state->input.environment_keys.length;
    String8 path_key = S8("PATH");

    for (u64 i = 0; i < key_length; i += 1)
    {
        String8 candidate_key = key_pointer[i];
#if defined(_WIN32)
        bool matches = os_windows_ascii_equal_ignore_case(path_key, candidate_key);
#else
        bool matches = string_equal(path_key, candidate_key);
#endif
        if (matches)
        {
            path_value = program_state->input.environment_values.pointer[i];
            path_present = true;
            break;
        }
    }

    if (path_present && file.length)
    {
        // An explicitly empty PATH is one empty component, not a missing
        // variable. Use a non-null slice for the existing slicing helpers.
        String8 path_it = path_value.length ? path_value : S8("");

#if defined(_WIN32)
        char8 path_separator = ';';
#else
        char8 path_separator = ':';
#endif

#if defined(_WIN32)
        bool has_exe_suffix = file.length >= 4 &&
            os_windows_ascii_equal_ignore_case(string_slice(file, file.length - 4, file.length), S8(".exe"));
        String8 exe_part = has_exe_suffix ? S8("") : S8(".exe");
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
            String8 directory_separator = S8("/");
#if defined(_WIN32)
            // Extended-length Win32 paths bypass slash normalization.
            if (string_starts_with_sequence(it, S8("\\\\?\\")))
            {
                directory_separator = S8("\\");
            }
#endif
            String8 parts[] = {
                it,
                directory_separator,
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
