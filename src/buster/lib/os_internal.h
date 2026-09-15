#pragma once
// Private deterministic file-I/O seam. Only the calling thread and the exact
// opened path are affected; scripts are bounded data, never callbacks.
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
#endif
