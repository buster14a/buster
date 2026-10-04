#pragma once
// Private captured-pipe replay, file-I/O and virtual-memory seams. Only the calling
// thread and the exact opened path are affected; scripts are bounded data,
// never callbacks. A staging file created for the selected path as destination
// is selected too: OPEN fails its creation, descriptor steps apply to it, and
// REPLACE/DELETE apply to renaming or deleting it. Injected file steps skip
// the system call.
#include <buster/lib/os.h>

// Ordinary POSIX captured-pipe draining. This reducer owns no native handles:
// the wait loop observes events and makes the one close attempt it requests.
// Bytes are successful read counts, never recorded output or paths.
typedef enum OsProcessCapturePhase
{
    OS_PROCESS_CAPTURE_WAITING,
    OS_PROCESS_CAPTURE_READY,
    OS_PROCESS_CAPTURE_CLOSING,
    OS_PROCESS_CAPTURE_CLOSED,
} OsProcessCapturePhase;

enum { OS_PROCESS_CAPTURE_READ_LIMIT = 16 * 1024 };

typedef enum OsProcessCaptureEvent
{
    OS_PROCESS_CAPTURE_WAIT_READY,
    OS_PROCESS_CAPTURE_WAIT_IDLE,
    OS_PROCESS_CAPTURE_WAIT_INTERRUPTED,
    OS_PROCESS_CAPTURE_WAIT_FAILED,
    OS_PROCESS_CAPTURE_READ_BYTES,
    OS_PROCESS_CAPTURE_READ_INTERRUPTED,
    OS_PROCESS_CAPTURE_READ_EOF,
    OS_PROCESS_CAPTURE_READ_FAILED,
    OS_PROCESS_CAPTURE_STOP,
    OS_PROCESS_CAPTURE_CLOSE_OK,
    OS_PROCESS_CAPTURE_CLOSE_FAILED,
    OS_PROCESS_CAPTURE_EVENT_COUNT,
} OsProcessCaptureEvent;

typedef struct OsProcessCaptureState OsProcessCaptureState;
struct OsProcessCaptureState
{
    OsProcessCapturePhase phase;
    u64 observed_bytes;
    u32 close_attempts;
    // Transport/cleanup failure, independent of abandonment without EOF.
    // A timeout alone must not suppress a consumer's permitted fresh-child retry.
    bool failed;
    bool eof;
    // A close error ends our authority to use/retry that descriptor. It does
    // not prove resource release under every POSIX implementation.
    bool close_outcome_unknown;
};

// Invalid events leave state untouched. Blocking pipes never produce a read
// EAGAIN event; idle readiness polls and zero-byte EINTR are recoverable.
BUSTER_F_DECL bool os_process_capture_step(OsProcessCaptureState* state, OsProcessCaptureEvent event, u64 bytes);
BUSTER_F_DECL ProcessResult os_process_capture_result(const OsProcessCaptureState* state, ProcessResult child_result);
#if BUSTER_INCLUDE_TESTS

#if BUSTER_LINUX || BUSTER_MACOS
// One failure on this thread in an ordinary (non-group) wait. WAIT/READ skip
// the syscall and report ENOMEM/EBADF; CLOSE releases the real descriptor,
// then reports EIO. These test the observation wiring, not kernel fault rates.
BUSTER_F_DECL void os_process_capture_test_fail_on_call(OsProcessCaptureEvent event, u32 call_index);
BUSTER_F_DECL bool os_process_capture_test_end(void);
#endif

typedef enum OsFileTestOperation
{
    OS_FILE_TEST_OPEN,
    OS_FILE_TEST_WRITE,
    OS_FILE_TEST_FLUSH,
    OS_FILE_TEST_CLOSE,
    OS_FILE_TEST_READ,
    OS_FILE_TEST_STATS,
    OS_FILE_TEST_MAP,
    OS_FILE_TEST_PERMISSIONS,
    OS_FILE_TEST_REPLACE,
    OS_FILE_TEST_DELETE,
} OsFileTestOperation;

typedef enum OsFileTestAction
{
    OS_FILE_TEST_ERROR,
    OS_FILE_TEST_LIMIT,
    OS_FILE_TEST_ZERO,
    OS_FILE_TEST_INTERRUPT,
    OS_FILE_TEST_SIZE,
} OsFileTestAction;

typedef struct OsFileTestStep OsFileTestStep;
struct OsFileTestStep
{
    OsFileTestOperation operation;
    OsFileTestAction action;
    u64 value;
};

BUSTER_F_DECL void os_file_test_begin(String8 path, const OsFileTestStep* steps, u32 count);
BUSTER_F_DECL u32 os_file_test_end(void);
BUSTER_F_DECL bool os_file_test_map_unavailable(String8 path);
// Calling-thread census of mapping views file_map_read created and
// file_map_unmap released. Nothing resets it; snapshot it before and after.
typedef struct FileMapTestCounters FileMapTestCounters;
struct FileMapTestCounters
{
    u64 mapped;
    u64 unmapped;
};
BUSTER_F_DECL FileMapTestCounters file_map_test_counters(void);
#if BUSTER_LINUX || BUSTER_MACOS
BUSTER_F_DECL void os_process_wait_test_expire_deadline_after_ready_once(void);
BUSTER_F_DECL bool os_process_group_reservation_release_self_test(void);
BUSTER_F_DECL bool os_process_group_recovery_self_test(void);
BUSTER_F_DECL bool os_process_group_ownership_loss_self_test(void);
BUSTER_F_DECL bool os_process_group_escaped_capture_self_test(Arena* arena);
#endif
#if BUSTER_LINUX
BUSTER_F_DECL bool os_linux_proc_context_select_self_test(String8 status, s32 process_id, bool identity_valid,
                                                           u32* namespace_index, u32* namespace_depth);
BUSTER_F_DECL bool os_linux_proc_context_live_self_test(void);
BUSTER_F_DECL bool os_linux_process_stat_parse_self_test(void);
BUSTER_F_DECL bool os_linux_process_group_churn_self_test(Arena* arena);
#endif

// Calling-thread census of the advisory prefault requests os_prefault
// observed, however they were produced. It exists so a test can prove that a
// request was issued, or that none was, without owning a privileged mapping
// or exhausting memory. Nothing resets it; snapshot it before and after.
typedef struct OsPrefaultTestCounters OsPrefaultTestCounters;
struct OsPrefaultTestCounters
{
    u64 requests;
    // Requests that reported anything other than OS_PREFAULT_POPULATED.
    u64 unpopulated;
    // The most recent outcome, or OS_PREFAULT_UNAVAILABLE before the first
    // request on this thread.
    OsPrefaultResult last;
};

// Makes the next os_prefault call on this thread report `result` without
// touching the mapping. One shot: the override is consumed by that call, so
// it cannot leak into an unrelated commit. It replaces only the reported
// outcome and never the commit that preceded it.
BUSTER_F_DECL void os_prefault_test_force_next(OsPrefaultResult result);
BUSTER_F_DECL OsPrefaultTestCounters os_prefault_test_counters(void);

#if BUSTER_INCLUDE_TESTS
typedef enum OsProcessSpawnTestOperation
{
    OS_PROCESS_SPAWN_TEST_FILE_ACTIONS_INIT,
    OS_PROCESS_SPAWN_TEST_ATTRIBUTES_INIT,
    OS_PROCESS_SPAWN_TEST_PIPE,
    OS_PROCESS_SPAWN_TEST_PIPE_CONFIGURATION,
    OS_PROCESS_SPAWN_TEST_FILE_ACTION,
    OS_PROCESS_SPAWN_TEST_ATTRIBUTE,
    OS_PROCESS_SPAWN_TEST_HANDLE_DUPLICATION,
    OS_PROCESS_SPAWN_TEST_HANDLE_LIST,
    OS_PROCESS_SPAWN_TEST_SPAWN,
    OS_PROCESS_SPAWN_TEST_OPERATION_COUNT,
} OsProcessSpawnTestOperation;

BUSTER_F_DECL void os_process_spawn_test_fail_on_call(OsProcessSpawnTestOperation operation, u64 call_index);
BUSTER_F_DECL bool os_process_spawn_test_end(void);
BUSTER_F_DECL u64 os_process_spawn_test_resource_count(void);
#endif

typedef enum OsResourceTestOperation
{
    OS_RESOURCE_TEST_BARRIER_CREATE,
    OS_RESOURCE_TEST_THREAD_CREATE,
    OS_RESOURCE_TEST_THREAD_JOIN,
    OS_RESOURCE_TEST_OPERATION_COUNT,
} OsResourceTestOperation;

// Fails exactly one zero-based call on the calling thread. The failure is
// consumed by that call and never reaches another test.
BUSTER_F_DECL void os_resource_test_fail_on_call(OsResourceTestOperation operation, u64 call_index);
BUSTER_F_DECL void os_resource_test_clear(void);
#if !BUSTER_SINGLE_THREADED
BUSTER_F_DECL bool os_resource_failure_self_test(void);
#endif
#endif
