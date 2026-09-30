#pragma once

// The platform boundary: virtual memory, threads and synchronization,
// files, process spawn/wait, dynamic libraries, timestamps, and failure
// reporting, one API over Linux, Windows, macOS, Android, and iOS.
//
// This header also owns the SPMD lane model every parallel phase uses
// (AGENTS.md): lane_run starts or reuses a persistent worker gang, every
// lane runs the same callback, lane_index/lane_count/lane_range split the
// work, lane_sync barriers, lane_broadcast shares one lane's value. Code
// written against it must degrade to serial — a one-lane gang and
// BUSTER_SINGLE_THREADED builds run the identical path. Lazily built
// globals state BUSTER_CHECK_SERIAL_INITIALIZATION() and are filled by
// their module's prewarm entry point before lanes run.

#include <buster/lib/base.h>

// Clearing every access flag makes the pages inaccessible.
typedef struct ProtectionFlags ProtectionFlags;
struct ProtectionFlags
{
    u64 read : 1;
    u64 write : 1;
    u64 execute : 1;
    u64 reserved : 61;
};

typedef struct MapFlags MapFlags;
struct MapFlags
{
    u64 priv : 1;
    u64 anonymous : 1;
    u64 no_reserve : 1;
    u64 populate : 1;
    u64 jit : 1;
    u64 fixed : 1;
    u64 reserved : 58;
};

typedef struct OpenFlags OpenFlags;
struct OpenFlags
{
    u64 truncate : 1;
    u64 execute : 1;
    u64 write : 1;
    u64 read : 1;
    u64 create : 1;
    u64 directory : 1;
    u64 reserved : 58;
};

typedef struct OpenPermissions OpenPermissions;
struct OpenPermissions
{
    u64 read : 1;
    u64 write : 1;
    u64 execute : 1;
    u64 reserved : 61;
};

typedef struct OsError OsError;
struct OsError
{
    u32 v;
};

typedef enum OsFileKind
{
    OS_FILE_KIND_MISSING,
    OS_FILE_KIND_REGULAR,
    OS_FILE_KIND_DIRECTORY,
    // A POSIX symbolic link or Windows reparse point that was not followed.
    OS_FILE_KIND_LINK,
    OS_FILE_KIND_OTHER,
} OsFileKind;

typedef struct FileStats FileStats;
struct FileStats
{
    u64 modified_time_s;
    u64 modified_time_ns;
    u64 size;
    // The identity option fills device, index, permissions and kind. POSIX
    // reports st_dev/st_ino; Windows reports the volume serial number and the
    // 64-bit file index (ReFS 128-bit ids are not read). Equal pairs taken from
    // handles that are open at the same time name one file.
    u64 device;
    u64 index;
    OsError error;
    // POSIX permission bits (0777). Windows maps the read-only attribute as the
    // C runtime's stat does: 0444 when set, otherwise 0666.
    u32 permissions;
    OsFileKind kind;
    bool valid;
};

typedef struct FileStatsOptions FileStatsOptions;
struct FileStatsOptions
{
    union
    {
        u64 raw;
        struct
        {
            u64 size : 1;
            u64 modified_time : 1;
            u64 identity : 1;
            u64 reserved : 61;
        };
    };
};

#if BUSTER_KERNEL == 0

typedef struct ThreadCreateOptions ThreadCreateOptions;
struct ThreadCreateOptions
{
    ThreadCallback* callback;
    void* argument;
};

typedef
#ifdef _WIN32
    u64
#else
    u128
#endif
        TimeDataType;
#endif

typedef struct ThreadInitialization ThreadInitialization;
struct ThreadInitialization
{
    u8 foo;
};

typedef enum StandardStream
{
    STANDARD_STREAM_INPUT,
    STANDARD_STREAM_OUTPUT,
    STANDARD_STREAM_ERROR,
    STANDARD_STREAM_COUNT,
} StandardStream;

// Captured output is finite by default. A zero configured limit selects the
// corresponding default; UINT64_MAX explicitly disables that one bound.
#define PROCESS_CAPTURE_DEFAULT_PER_STREAM_BYTES BUSTER_MB(16)
#define PROCESS_CAPTURE_DEFAULT_TOTAL_BYTES BUSTER_MB(32)

typedef enum ProcessCaptureOverflowPolicy
{
    // Retain the deterministic prefix and keep draining the rest.
    PROCESS_CAPTURE_OVERFLOW_TRUNCATE,
    // Retain the prefix, keep draining, and make the wait result fail.
    PROCESS_CAPTURE_OVERFLOW_FAIL,
    // Retain the prefix and write later bytes to the caller-owned descriptor
    // for that stream. The descriptor is neither flushed nor closed here.
    PROCESS_CAPTURE_OVERFLOW_STREAM_TO_FILE,
    PROCESS_CAPTURE_OVERFLOW_COUNT,
} ProcessCaptureOverflowPolicy;

typedef struct ProcessCaptureLimits ProcessCaptureLimits;
struct ProcessCaptureLimits
{
    u64 per_stream[(size_t)STANDARD_STREAM_COUNT];
    u64 total;
};

typedef struct ProcessGroupControlState ProcessGroupControlState;

typedef enum ProcessSpawnFailure
{
    PROCESS_SPAWN_FAILURE_NONE,
    PROCESS_SPAWN_FAILURE_INVALID_ARGUMENTS,
    PROCESS_SPAWN_FAILURE_INVALID_ENVIRONMENT,
    PROCESS_SPAWN_FAILURE_EXECUTABLE_LOOKUP,
    PROCESS_SPAWN_FAILURE_FILE_ACTIONS_INIT,
    PROCESS_SPAWN_FAILURE_ATTRIBUTES_INIT,
    PROCESS_SPAWN_FAILURE_PIPE,
    PROCESS_SPAWN_FAILURE_PIPE_CONFIGURATION,
    PROCESS_SPAWN_FAILURE_FILE_ACTION,
    PROCESS_SPAWN_FAILURE_ATTRIBUTE,
    PROCESS_SPAWN_FAILURE_HANDLE_DUPLICATION,
    PROCESS_SPAWN_FAILURE_HANDLE_LIST,
    PROCESS_SPAWN_FAILURE_SPAWN,
    PROCESS_SPAWN_FAILURE_UNSUPPORTED,
} ProcessSpawnFailure;

typedef struct ProcessSpawnResult ProcessSpawnResult;
struct ProcessSpawnResult
{
    OsProcessHandle* handle;
    // Windows Job Object used for kill-on-close tree containment. Null on
    // POSIX, where process_group reserves the exact group-leader identity.
    OsProcessHandle* process_tree;
    OsFileDescriptor* pipes[STANDARD_STREAM_COUNT][2];
    ProcessCaptureLimits capture_limits;
    OsFileDescriptor* capture_overflow_files[(size_t)STANDARD_STREAM_COUNT];
    // The first failed validation or platform setup stage. Native error zero is
    // reserved for success; callers never need to scrape a diagnostic string.
    OsError error;
    ProcessSpawnFailure failure;
    // Optional shared flag state. The wait lane remains the sole owner of the
    // process-group identity and performs every signal, query, and reap.
    ProcessGroupControlState* process_group_control;
    ProcessCaptureOverflowPolicy capture_overflow_policy;
    // On POSIX, the child is the leader of a fresh process group. On Windows,
    // it was assigned to a kill-on-close Job Object before its first instruction.
    u64 process_group : 1;
    u64 reserved : 63;
};

typedef struct ProcessSpawnOptions ProcessSpawnOptions;
struct ProcessSpawnOptions
{
    u64 capture : (size_t)STANDARD_STREAM_COUNT;
    // Inherit the captured process environment exactly. Otherwise the supplied
    // key/value slices form the complete child environment, including empty.
    u64 use_process_environment : 1;
    // POSIX creates a fresh process group. Windows creates a kill-on-close Job
    // Object and assigns the suspended child before allowing it to execute.
    u64 new_process_group : 1;
    // Resolve a bare argv[0] once against the captured parent PATH before any
    // pipe or platform spawn object is created. Direct execution never asks an
    // OS API to search PATH.
    u64 search_path : 1;
    u64 reserved : sizeof(u64) * 8 - (size_t)STANDARD_STREAM_COUNT - 3;
    ProcessCaptureLimits capture_limits;
    OsFileDescriptor* capture_overflow_files[(size_t)STANDARD_STREAM_COUNT];
    ProcessCaptureOverflowPolicy capture_overflow_policy;
};

typedef struct ProcessWaitResult ProcessWaitResult;
struct ProcessWaitResult
{
    ByteSlice streams[(size_t)STANDARD_STREAM_COUNT];
    // observed = every byte drained; captured = the returned prefix; streamed
    // = overflow written to the configured descriptor; dropped = the rest.
    u64 observed_bytes[(size_t)STANDARD_STREAM_COUNT];
    u64 captured_bytes[(size_t)STANDARD_STREAM_COUNT];
    u64 streamed_bytes[(size_t)STANDARD_STREAM_COUNT];
    u64 dropped_bytes[(size_t)STANDARD_STREAM_COUNT];
    u64 observed_total;
    u64 captured_total;
    u64 streamed_total;
    u64 dropped_total;
    ProcessResult result;
    // Native child status retained for diagnostics. On Windows this is the
    // DWORD returned by GetExitCodeProcess; on POSIX it is the status word
    // returned by waitpid. `result` remains the portable contract.
    u32 platform_status;
    // Set when the deadline passed and the process tree was terminated.
    u8 timed_out;
    u8 termination_requested;
    u8 forcibly_terminated;
    // The returned in-memory streams are prefixes because at least one bound
    // was reached. capture_failed additionally makes result a plain failure.
    u8 capture_limit_exceeded;
    u8 output_truncated;
    u8 capture_failed;
    u8 process_tree_cleanup_failed;
    // The exact group leader was not reaped. Callers must stop admission and
    // must not hand its retained numeric identity to another lane.
    u8 process_group_reservation_retained;
    // The WNOWAIT observation stopped proving ownership (for example ECHILD).
    // No later signal, group query, or reap was attempted with the numeric ID.
    u8 process_group_ownership_lost;
    u8 reserved[2];
};

typedef enum OsFileReadStatus
{
    OS_FILE_READ_OK,
    OS_FILE_READ_EOF,
    OS_FILE_READ_ERROR,
} OsFileReadStatus;

typedef struct OsFileReadResult OsFileReadResult;
struct OsFileReadResult
{
    u64 transferred;
    OsFileReadStatus status;
    OsError error;
};

// A zero error is success. Transfer counts remain valid on failure and never
// include bytes from a failed system call. Capture errors before cleanup.
typedef struct OsFileTransferResult OsFileTransferResult;
struct OsFileTransferResult
{
    u64 transferred;
    OsError error;
};

typedef struct OsFileOpenResult OsFileOpenResult;
struct OsFileOpenResult
{
    OsFileDescriptor* file;
    OsError error;
};

#define BUSTER_OS_ERROR_BUFFER_MAX_LENGTH (BUSTER_KB(64))

BUSTER_F_DECL OsError os_get_last_error(void);
BUSTER_F_DECL String8 string8_from_os_error(Arena* arena, OsError error, bool null_terminate);
BUSTER_F_DECL ProcessSpawnResult os_process_spawn(SliceString8 argv, SliceString8 environment_keys, SliceString8 environment_values,
                                                  ProcessSpawnOptions options);
BUSTER_F_DECL ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn);
// The same wait, given up on after `timeout_microseconds`: the child is killed,
// whatever it had already written is still returned, and `timed_out` says the
// deadline is why. Zero waits forever, which is what os_process_wait_sync does.
BUSTER_F_DECL ProcessWaitResult os_process_wait_deadline(Arena* arena, ProcessSpawnResult spawn, u64 timeout_microseconds);
BUSTER_F_DECL String8 os_get_environment_variable(String8 variable);

BUSTER_F_DECL void os_make_directory(String8 path);
// Creates one owner-only directory. An existing path counts as success, like
// mkdir/EEXIST; callers opening a result tree still validate its contents.
// Unlike os_make_directory, reports failure and accepts bounded path slices.
BUSTER_F_DECL bool os_make_directory_attempt(String8 path);

typedef struct OsDirectoryCreateResult OsDirectoryCreateResult;
struct OsDirectoryCreateResult
{
    OsError error;
    // True means an entry of any kind already occupied the requested name.
    bool already_exists;
    u8 reserved[3];
};
// Creates exactly one new directory and never accepts an existing file,
// directory or link as ownership. POSIX mode is 0700; Windows inherits the
// containing directory's access policy. Parent directories are not created.
BUSTER_F_DECL OsDirectoryCreateResult os_make_directory_exclusive(String8 path);

BUSTER_F_DECL bool os_file_delete(String8 path);
// The native error behind os_file_delete; a missing path is still success.
BUSTER_F_DECL OsError os_file_delete_checked(String8 path);
// Deletes `path` and everything under it. Symbolic links are removed as links
// rather than followed, so the walk cannot escape the tree it was given.
// Returns whether the tree is gone; a missing `path` counts as success.
BUSTER_F_DECL bool os_directory_delete(String8 path);

// Replacement publication: inspect the destination, stage beside it, rename
// the staging file over it.
//
// Inspects `path` as a replacement target without creating, truncating,
// writing, following a final link or waiting for a FIFO reader. POSIX has no
// metadata-only open, so it opens write-only and the kernel applies the
// caller's write access (for example EACCES, EROFS or Linux ETXTBSY) as an
// in-place writer would; ELOOP, EISDIR and ENXIO become the link, directory
// and other kinds. Windows opens attributes only. A missing target is valid
// with OS_FILE_KIND_MISSING; otherwise the result carries identity stats.
BUSTER_F_DECL FileStats os_file_replacement_target_stats(String8 path);

typedef struct OsFileStagingResult OsFileStagingResult;
struct OsFileStagingResult
{
    OsFileDescriptor* file;
    // Zero-terminated, in the caller's arena; empty on failure.
    String8 path;
    OsError error;
};
// Staging names are OS_FILE_STAGING_PREFIX, the process id, '-', a counter and
// OS_FILE_STAGING_SUFFIX, so files left by interrupted publications are
// recognizable.
#define OS_FILE_STAGING_PREFIX S8(".buster-staging-")
#define OS_FILE_STAGING_SUFFIX S8(".tmp")
// Exclusively creates a new, empty, write-only file in `destination`'s
// directory. Only name collisions are retried. Permissions map as for
// os_file_open.
BUSTER_F_DECL OsFileStagingResult os_file_staging_create(Arena* arena, String8 destination, OpenPermissions permissions);
// Renames `path` over `destination` on one filesystem: rename(2), or
// FileRenameInfoEx with replace/POSIX semantics, without a copy fallback. An existing
// destination entry, including a link, is replaced, never followed or deleted
// first. The namespace change is atomic where the filesystem provides it;
// nothing is flushed, so it is not a crash-durability promise.
BUSTER_F_DECL OsError os_file_replace(String8 path, String8 destination);
#if !defined(_WIN32)
// fchmod restricted to the 0777 permission bits.
BUSTER_F_DECL OsError os_file_set_permissions(OsFileDescriptor* file_descriptor, u32 permissions);
#endif
BUSTER_F_DECL OsFileDescriptor* os_file_open(String8 path, OpenFlags flags, OpenPermissions permissions);
BUSTER_F_DECL OsFileOpenResult os_file_open_checked(String8 path, OpenFlags flags, OpenPermissions permissions);
BUSTER_F_DECL OsFileTransferResult os_file_write_checked(OsFileDescriptor* file_descriptor, ByteSlice buffer);
// Flush is explicit: ordinary artifact writes promise completion, not crash
// durability. Close always consumes the descriptor, including on failure.
BUSTER_F_DECL OsError os_file_flush(OsFileDescriptor* file_descriptor);
BUSTER_F_DECL OsError os_file_close_checked(OsFileDescriptor* file_descriptor);
// Legacy size convenience: UINT64_MAX denotes failure, never an empty file.
BUSTER_F_DECL u64 os_file_get_size(OsFileDescriptor* file_descriptor);
BUSTER_F_DECL FileStats os_file_get_stats(OsFileDescriptor* file_descriptor, FileStatsOptions options);
BUSTER_F_DECL void os_file_write(OsFileDescriptor* file_descriptor, ByteSlice buffer);
// Some performs one successful transfer (retrying interruptions); exact fills
// the request or returns EOF/error with a preserved prefix count. Empty reads
// succeed without touching the descriptor. EOF is never an OS error.
BUSTER_F_DECL OsFileReadResult os_file_read_some(OsFileDescriptor* file_descriptor, ByteSlice buffer);
BUSTER_F_DECL OsFileReadResult os_file_read_exact(OsFileDescriptor* file_descriptor, ByteSlice buffer);
// Recoverable transfers: retry interrupted/partial IO without asserting or
// printing. Read fills the buffer or reaches EOF; true with *read_count == 0
// is EOF (or an empty request), false is an error, retaining any prefix count.
// Empty transfers succeed without touching the descriptor.
BUSTER_F_DECL bool os_file_read_attempt(OsFileDescriptor* file_descriptor, ByteSlice buffer, u64* read_count);
BUSTER_F_DECL bool os_file_write_attempt(OsFileDescriptor* file_descriptor, ByteSlice buffer);
BUSTER_F_DECL bool os_file_close(OsFileDescriptor* file_descriptor);

BUSTER_F_DECL String8 os_path_absolute(Arena* arena, String8 relative_file_path, bool null_terminate);
// Allows missing output paths. POSIX prefixes the current directory without
// resolving symlinks or dot components; Windows uses GetFullPathNameW.
// Accepts bounded slices and returns an empty slice on OS/invalid-input failure.
BUSTER_F_DECL String8 os_path_absolute_lexical(Arena* arena, String8 path, bool null_terminate);
BUSTER_F_DECL OsFileDescriptor* os_get_stdout(void);
BUSTER_F_DECL OsFileDescriptor* os_get_standard_stream(StandardStream stream);
BUSTER_F_DECL OsThreadHandle* os_thread_create(ThreadCreateOptions options);
BUSTER_F_DECL bool os_thread_join(OsThreadHandle* handle);
// True while every thread this process started through os_thread_create has
// been joined, so the caller is the only one that can be touching a global.
BUSTER_F_DECL bool os_is_only_live_thread(void);
BUSTER_F_DECL OsMutexHandle* os_mutex_create(void);
BUSTER_F_DECL void os_mutex_lock(OsMutexHandle* handle);
BUSTER_F_DECL void os_mutex_unlock(OsMutexHandle* handle);
BUSTER_F_DECL void os_mutex_destroy(OsMutexHandle* handle);

// Single-threaded builds compile the barrier out entirely: nothing in them
// can ever wait on one, and the tcc bootstrap's own Win32 headers and import
// list predate condition variables.
#if !BUSTER_SINGLE_THREADED
BUSTER_F_DECL OsBarrierHandle* os_barrier_create(u32 thread_count);
BUSTER_F_DECL void os_barrier_wait(OsBarrierHandle* handle);
BUSTER_F_DECL void os_barrier_destroy(OsBarrierHandle* handle);
#endif

// C11 _Atomic where the compiler provides it. Single-threaded builds use a
// plain u64 so tcc never sees the qualifier, and MSVC builds do the same
// because its C atomics are still experimental; the Interlocked implementation
// works on the plain type. _Atomic u64 and u64 share size and alignment on
// every supported target, so struct layout does not depend on the choice.
#if BUSTER_SINGLE_THREADED || BUSTER_COMPILER_MSVC
typedef u64 AtomicU64;
#else
typedef _Atomic u64 AtomicU64;
#endif

#if BUSTER_SINGLE_THREADED
typedef volatile s32 ProcessControlAtomic;
#else
typedef AtomicU64 ProcessControlAtomic;
#endif

// A process-group wait reads these flags directly; there is no callback or
// alternate dispatcher. The admission mutex makes a cleanup failure and the
// caller's spawn admission check one serialized policy decision.
struct ProcessGroupControlState
{
    ProcessControlAtomic* cancellation_signal;
    ProcessControlAtomic* cancellation_escalated;
    OsMutexHandle* admission_mutex;
    u64 test_cancel_before_reap : 1;
    u64 reserved : 63;
};

// Process-control accesses remain signal-safe in serial builds and lock-free
// in the threaded POSIX builds that share them with wait lanes.
BUSTER_F_DECL u64 process_control_atomic_load(ProcessControlAtomic* address);
BUSTER_F_DECL void process_control_atomic_store(ProcessControlAtomic* address, u64 value);
BUSTER_F_DECL bool process_control_atomic_set_if_zero(ProcessControlAtomic* address, u64 value);

// All three return the value the address held before the addition. In
// single-threaded builds they compile to plain arithmetic.
BUSTER_F_DECL u64 atomic_u64_increment(AtomicU64* address);
BUSTER_F_DECL u64 atomic_u64_decrement(AtomicU64* address);
BUSTER_F_DECL u64 atomic_u64_add(AtomicU64* address, u64 addend);

typedef enum ProgramFlag
{
    PROGRAM_FLAG_VERBOSE,
    PROGRAM_FLAG_CI,
    PROGRAM_FLAG_TEST_PERSIST,
    PROGRAM_FLAG_COUNT,
} ProgramFlag;

typedef struct ProgramInput ProgramInput;
struct ProgramInput
{
    SliceString8 arguments;
    SliceString8 environment_keys;
    SliceString8 environment_values;
    StringOsList raw_arguments;
    StringOsList raw_environment;
    FLAG_ARRAY_U64(flags, ProgramFlag, PROGRAM_FLAG_COUNT);
    u8 reserved[4];
};

typedef struct ProgramState ProgramState;
struct ProgramState
{
    ProgramInput input;
    Arena* arena;
    u64 is_debugger_present_called : 1;
    u64 _is_debugger_present : 1;
    u64 reserved : 62;
};

typedef struct LaneContext LaneContext;
typedef struct LaneGang LaneGang;
struct LaneContext
{
    u64 lane_index;
    u64 lane_count;
    OsBarrierHandle* barrier;
    u64* broadcast_memory;
};

typedef struct LaneRange LaneRange;
struct LaneRange
{
    u64 start;
    u64 end;
};

typedef struct ThreadContext ThreadContext;
struct ThreadContext
{
    Arena* arenas[(u64)SCRATCH_ARENA_COUNT];
    LaneContext lane_context;
    // Lazily-created resident workers owned by this caller context. Keeping
    // the gang here lets unrelated OS threads dispatch independently while
    // the normal compiler path reuses one process-lifetime gang.
    Arena* lane_arena;
    LaneGang* lane_gang;
};

BUSTER_V_DECL ProgramState* program_state;

BUSTER_NORETURN BUSTER_COLD BUSTER_F_DECL void os_fail_va(u32 line, String8 function, String8 file, String8 context, ...);
BUSTER_NORETURN BUSTER_COLD BUSTER_F_DECL void os_fail_raw(u32 line, String8 function, String8 file, String8 context);

#define os_fail_message(message)                                                                                                                               \
    (((void)(is_debugger_present() ? (BUSTER_BREAKPOINT(), 0) : 0)), os_fail_va((u32)__LINE__, BUSTER_FUNCTION, S8(__FILE__), message))
#define os_fail_message_format(message, ...)                                                                                                                   \
    (((void)(is_debugger_present() ? (BUSTER_BREAKPOINT(), 0) : 0)), os_fail_va((u32)__LINE__, BUSTER_FUNCTION, S8(__FILE__), message, __VA_ARGS__))
// Reports without allocating. Use for invariants that can fail while the thread
// context is unselected, where the formatted reporter would need the very
// scratch arena whose absence is being reported.
#define os_fail_message_raw(message)                                                                                                                           \
    (((void)(is_debugger_present() ? (BUSTER_BREAKPOINT(), 0) : 0)), os_fail_raw((u32)__LINE__, BUSTER_FUNCTION, S8(__FILE__), message))
#define os_fail() os_fail_message(S8("internal error"))
#define BUSTER_CHECK_RAW(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message_raw(S8("assertion failed")), 0) : 0))
// Invariants may become optimizer assumptions in Release. Validation may not:
// anything derived from source text, object bytes, file sizes, system calls or
// other fallible inputs must retain its branch and failure in every build.
// BUSTER_CHECK states a proven invariant and may become an optimizer
// assumption. BUSTER_ASSERT is diagnostic only and disappears in optimized
// builds. BUSTER_VALIDATE retains both its branch and defined failure in every
// build, so resource and input failures must use it (or return an error).
#define BUSTER_VALIDATE(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("validation failed")), 0) : 0))
#if BUSTER_OPTIMIZE
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (BUSTER_UNREACHABLE(), 0) : 0))
#define BUSTER_ASSERT(ok) ((void)sizeof(!!(ok)))
#else
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("assertion failed")), 0) : 0))
#define BUSTER_ASSERT(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("assertion failed")), 0) : 0))
#endif
// Stated by every global table that is still built on first use. Those builds
// are unsynchronized on purpose: they run once and every later read is a plain
// load, which is only sound while no other thread can be reading. A caller
// that reaches one with threads running skipped the module's prewarm entry
// point, so say that instead of racing. Always compiled in -- the whole point
// is the build that nobody profiled going parallel.
//
// A prewarm is sufficient and not merely likely to work: thread creation is a
// happens-before edge in both C11 and POSIX, so a table completed before
// os_thread_create is fully visible to the thread it starts, with no fence of
// the table's own.
#define BUSTER_CHECK_SERIAL_INITIALIZATION()                                                                                                                   \
    ((void)(BUSTER_UNLIKELY(!os_is_only_live_thread()) ? (os_fail_message(S8("global table built while other threads run; prewarm it first")), 0) : 0))
#define BUSTER_TODO_MESSAGE(message, ...)                                                                                                                      \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        os_fail_message_format(message, __VA_ARGS__);                                                                                                          \
    } while (1)
#define BUSTER_TODO()                                                                                                                                          \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        os_fail_message(S8("TODO"));                                                                                                                           \
    } while (1)

BUSTER_NORETURN BUSTER_F_DECL void os_exit(u32 code);

// Outcome of a best-effort prefault request. Prefaulting only populates the
// page table entries of a range that is already committed. It is advisory on
// every supported target: it is not residency, not protection from paging or
// swap, not a lock of any kind, and not a latency guarantee. The operating
// system may reclaim a populated page immediately afterwards, and nothing
// here pins anything for any lifetime.
typedef enum OsPrefaultResult
{
    // This build has no prefault facility for its target, so no request was
    // issued and no page was populated.
    OS_PREFAULT_UNAVAILABLE,
    // The platform accepted the request for the whole range.
    OS_PREFAULT_POPULATED,
    // The platform rejected it: an older kernel, a privilege or resource
    // limit, or a mapping it declines to populate. The range stays committed
    // and usable exactly as it was.
    OS_PREFAULT_REFUSED,
} OsPrefaultResult;

BUSTER_F_DECL void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map);
// Commits `size` bytes at `address`; the result reports that commitment and
// nothing else. `prefault` additionally requests best-effort prefaulting of
// the committed range. That request is issued only after the commit itself
// succeeded and its outcome is not folded into this result, so a refused or
// unavailable prefault can neither fail a good commit nor stand in for a
// failed one. Call os_prefault directly when the outcome matters.
BUSTER_F_DECL bool os_commit(void* address, u64 size, ProtectionFlags protection, bool prefault);
BUSTER_F_DECL OsPrefaultResult os_prefault(void* address, u64 size);
BUSTER_F_DECL bool os_protect(void* address, u64 size, ProtectionFlags protection);
BUSTER_F_DECL bool os_decommit(void* address, u64 size);
BUSTER_F_DECL bool os_unreserve(void* address, u64 size);
BUSTER_F_DECL bool os_flush_instruction_cache(void* address, u64 size);
// Selects write (false) or execute (true) access for MAP_JIT mappings on the
// calling Apple Silicon macOS thread. It is a no-op everywhere else.
BUSTER_F_DECL void os_jit_write_protect(bool enabled);

BUSTER_F_DECL bool os_is_tty(OsFileDescriptor* file);
BUSTER_F_DECL OsModuleHandle* os_dynamic_library_load(String8 library);
BUSTER_F_DECL void os_dynamic_library_unload(OsModuleHandle* module);
BUSTER_F_DECL OsSymbol* os_dynamic_library_function_load(OsModuleHandle* module, String8 symbol);
BUSTER_F_DECL u32 os_get_logical_thread_count(void);
BUSTER_F_DECL u64 os_get_page_size(void);
BUSTER_F_DECL u64 os_get_physical_memory_size(void);
BUSTER_F_DECL u64 os_get_resident_memory_size(void);
BUSTER_F_DECL u64 os_get_current_process_id(void);
BUSTER_F_DECL OsProcessHandle* os_get_current_process_handle(void);
BUSTER_F_DECL OsThreadHandle* os_get_current_thread_handle(void);
BUSTER_F_DECL void os_thread_set_name(String8 thread_name);

BUSTER_COLD BUSTER_F_DECL bool is_debugger_present(void);

#if defined(_WIN32)
// Safe before application entry-point initialization; does not mutate a cache.
BUSTER_F_DECL u64 os_performance_counter_frequency(void);
#endif
BUSTER_F_DECL u64 os_now_microseconds(void);

BUSTER_F_DECL ThreadContext* thread_context_allocate(void);
BUSTER_F_DECL ThreadContext* thread_context_selected(void);
BUSTER_F_DECL void thread_context_select(ThreadContext* context);
BUSTER_F_DECL void thread_context_release(ThreadContext* context);
BUSTER_F_DECL Arena* thread_context_get_scratch(Arena** conflicts, u64 count);

// Lanes: every thread of a gang runs the same code and asks for its share of
// the work, mirroring GPU threads. Outside a gang the selected thread context
// answers as the only lane, so lane-style code degrades to serial without a
// separate path.
BUSTER_F_DECL u64 lane_index(void);
BUSTER_F_DECL u64 lane_count(void);
// Barrier across the gang. A one-lane gang returns immediately.
BUSTER_F_DECL void lane_sync(void);
// Barrier that also copies up to 8 bytes from the source lane's value to every
// other lane's value.
BUSTER_F_DECL void lane_broadcast(void* value_pointer, u64 value_size, u64 source_lane_index);
// This lane's contiguous share of [0, item_count). Concatenating every lane's
// range in lane order reproduces [0, item_count) exactly; share sizes differ
// by at most one item.
BUSTER_F_DECL LaneRange lane_range(u64 item_count);
// Runs callback(argument) once on every lane and returns when all lanes have
// finished. A requested count of zero asks for one lane per logical processor.
// Resident workers are reused across calls made by the same caller context;
// single-threaded builds and one-lane requests run directly. The caller's lane
// context is saved and restored. Nested gangs retain the old independent-gang
// behavior so an outer lane can safely narrow and invoke another lane region.
BUSTER_F_DECL void lane_run(u64 lane_count_requested, ThreadCallback* callback, void* argument);

BUSTER_F_DECL void flag_set_ex(u64* flag_pointer, u64 flag_count, u64 flag_index, bool flag_value);
BUSTER_F_DECL bool flag_get_ex(u64* flag_pointer, u64 flag_count, u64 flag_index);

#define flag_get(arr, Count, e) flag_get_ex((arr), (Count), (u64)(e))
#define flag_set(arr, Count, e, v) flag_set_ex((arr), (Count), (e), (v))

typedef struct BooleanArgumentProcessResult BooleanArgumentProcessResult;
struct BooleanArgumentProcessResult
{
    u64 index;
    bool valid;
    u8 reserved[7];
};
BUSTER_F_DECL BooleanArgumentProcessResult boolean_argument_process(String8* flag_string_start_pointer, u64 flag_string_start_count, u64* flag_pointer,
                                                                    u64 flag_count, String8 argument);

BUSTER_F_DECL bool program_flag_get(ProgramFlag flag);
// Resolve using the captured PATH. An explicitly empty value searches the
// current directory; a missing PATH does not. Windows matches ASCII PATH keys
// and .exe suffixes without case, preserving the spelling of directory paths.
// A successful, zero-terminated result belongs to arena; an empty file fails.
BUSTER_F_DECL String8 executable_resolve_in_path(Arena* arena, String8 file);
