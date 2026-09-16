#pragma once
// Private deterministic file-I/O and virtual-memory seams. Only the calling
// thread and the exact opened path are affected; scripts are bounded data,
// never callbacks.
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
#endif
