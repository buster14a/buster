#pragma once
// Private deterministic file-I/O seam. Only the calling thread and the exact
// opened path are affected; scripts are bounded data, never callbacks. A
// staging file created for the selected path as destination is selected too:
// OPEN fails its creation, descriptor steps apply to it, and REPLACE/DELETE
// apply to renaming or deleting it. Injected steps skip the system call.
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
#endif
