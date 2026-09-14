/* Linux single-job supervisor for the installed validate-buster recipe.
 * The request selects only the named recipe; every executable and option used
 * below is service-owned.  See README.md for the deployment trust boundary.
 */
#ifndef BUSTER_BENCH_SERVICE_WORKER_LINUX_H
#define BUSTER_BENCH_SERVICE_WORKER_LINUX_H

#include "queue.h"

#define BQ_WORKER_UNIT_CAP 96u
#define BQ_WORKER_BOOT_CAP 40u
#define BQ_WORKER_CGROUP_CAP 192u
#define BQ_WORKER_INVOCATION_CAP 40u
#define BQ_WORKER_OUTPUT_CAP 4096u
#define BQ_WORKER_ARG_CAP 32u

typedef struct BqWorkerLimits
{
    u32 cpu;
    u64 memory_max;
    u64 memory_swap_max;
    u64 tasks_max;
    u64 runtime_max_usec;
} BqWorkerLimits;

typedef enum BqWorkerResult
{
    BQ_WORKER_RUNNING,
    BQ_WORKER_SUCCEEDED,
    BQ_WORKER_EXECUTION_FAILED,
    BQ_WORKER_OOM,
    BQ_WORKER_TIMED_OUT,
    BQ_WORKER_CANCELLED_RESULT
} BqWorkerResult;

typedef struct BqWorkerObserved
{
    char boot_id[BQ_WORKER_BOOT_CAP];
    char unit[BQ_WORKER_UNIT_CAP];
    char cgroup[BQ_WORKER_CGROUP_CAP];
    char invocation_id[BQ_WORKER_INVOCATION_CAP];
    char allowed_cpus[64];
    u64 memory_max;
    u64 memory_swap_max;
    u64 tasks_max;
    u64 runtime_max_usec;
    u64 timeout_stop_usec;
    BqWorkerResult result;
    bool unit_found;
    bool active;
    bool populated;
    bool send_sigkill;
    u64 cgroup_device;
    u64 cgroup_inode;
    u64 cgroup_root_device;
    u64 cgroup_root_inode;
    u64 cgroup_slice_device;
    u64 cgroup_slice_inode;
    char kill_mode[24];
} BqWorkerObserved;

typedef struct BqWorkerBackend BqWorkerBackend;
struct BqWorkerBackend
{
    void* context;
    BqError (*start)(BqWorkerBackend*, char const* const*, u32);
    BqError (*observe)(BqWorkerBackend*, char const*, BqWorkerObserved*, u64);
    BqError (*signal)(BqWorkerBackend*, char const*, char const*, u64);
    BqError (*join)(BqWorkerBackend*, int*, u64);
    BqError (*cleanup_launcher)(BqWorkerBackend*, u64);
    BqError (*delay)(BqWorkerBackend*, u32);
    u64 (*clock)(BqWorkerBackend*);
};

typedef struct BqWorkerQuarantine
{
    int descriptor;
    char lease_path[BQ_PATH_CAP + 1];
} BqWorkerQuarantine;

typedef struct BqWorkerConfig
{
    String8 installed_root;
    String8 workspace_root;
    String8 lease_file;
    String8 boot_id_file;
    String8 cgroup_root;
    BqWorkerLimits limits;
    BqWorkerBackend* backend;
    BqWorkerQuarantine* quarantine;
} BqWorkerConfig;

BUSTER_F_DECL BqError bq_worker_run(BqQueue* queue, BqWorkerConfig const* config, u64* id);
BUSTER_F_DECL BqError bq_worker_unit(String8 lease_file, int lease_fd);
BUSTER_F_DECL void bq_worker_backend_systemd(BqWorkerBackend* backend);

#endif
