#pragma once
// Private deterministic file-I/O and virtual-memory seams. Only the calling
// thread and the exact opened path are affected; scripts are bounded data,
// never callbacks. A staging file created for the selected path as destination
// is selected too: OPEN fails its creation, descriptor steps apply to it, and
// REPLACE/DELETE apply to renaming or deleting it. Injected file steps skip
// the system call.
#include <buster/lib/os.h>
#if BUSTER_INCLUDE_TESTS

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
#if BUSTER_LINUX || BUSTER_MACOS
BUSTER_F_DECL void os_process_wait_test_expire_deadline_after_ready_once(void);
BUSTER_F_DECL bool os_process_group_reservation_release_self_test(void);
BUSTER_F_DECL bool os_process_group_recovery_self_test(void);
BUSTER_F_DECL bool os_process_group_ownership_loss_self_test(void);
BUSTER_F_DECL bool os_process_group_escaped_capture_self_test(Arena* arena);
#endif
#if BUSTER_LINUX
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
