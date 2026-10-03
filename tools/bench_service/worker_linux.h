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
#define BQ_WORKER_RELATION_CAP (BQ_WORKER_UNIT_CAP * 12u)
#define BQ_WORKER_INVOCATION_CAP 40u
#define BQ_WORKER_IDENTITY_CAP 64u
#define BQ_WORKER_OUTPUT_CAP 16384u
#define BQ_WORKER_ARG_CAP 32u
#define BQ_WORKER_BUNDLE_CAP (8u * 1024u * 1024u)
#define BQ_WORKER_BUNDLE_ENTRY_CAP 4096u
/* The result root's control files (the recipe's manifest, bundle and
 * outcome, bq_worker_bundle_reserved) count against the entry cap beside the
 * bundle's objects; the retirement producer reserves them in its store plan. */
#define BQ_WORKER_BUNDLE_CONTROL_ENTRIES 3u
/* The worker-phase-N receipts a retirement job's BQPHASE2 channel leaves in
 * the result root (bq_worker_phase_accept publishes one per acknowledged
 * phase: PREPARING, RETIREMENT_READY, SETTLING, MEASURING, AA_MEASURED and
 * MEASURED). They are bundle objects, so the retirement store plan reserves
 * them too. */
#define BQ_WORKER_RETIREMENT_PHASE_RECEIPTS 6u
#define BQ_WORKER_BUNDLE_FILE_CAP (64ull * 1024 * 1024)
/* Retirement evidence (#1880). A retirement result-root file named
 * BQ_WORKER_RETIREMENT_EVIDENCE_PREFIX plus a non-empty rest, with no "/", is
 * one of the evidence files the binding context names (#1998's
 * BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX, lane F's EVIDENCE_PREFIX). They
 * include the frozen toolchain's files, so only they have this per-file cap,
 * which covers the toolchain's own (BQ_RETIREMENT_TOOLCHAIN_FILE_CAP). Every
 * other file, and every file of another recipe, keeps
 * BQ_WORKER_BUNDLE_FILE_CAP (bq_worker_bundle_file_cap). Each reader hashes
 * and copies such a file in fixed chunks, never whole. */
#define BQ_WORKER_RETIREMENT_EVIDENCE_PREFIX "retirement-evidence-"
#define BQ_WORKER_RETIREMENT_EVIDENCE_FILE_CAP (512ull * 1024 * 1024)
#define BQ_WORKER_BUNDLE_TOTAL_CAP (512ull * 1024 * 1024)
/* The fixed retirement population needs millions of paired records. This is
 * a separate bounded ceiling; the installed smoke recipe keeps its limit. */
#define BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP (128ull * 1024 * 1024 * 1024)
#define BQ_WORKER_BUNDLE_DEPTH_CAP 256u
#define BQ_WORKER_BUNDLE_PATH_CAP BQ_PATH_CAP
#define BQ_WORKER_BUNDLE_LINE_CAP 320u
#define BQ_WORKER_EVIDENCE_CAP 4096u

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
    char part_of[BQ_WORKER_RELATION_CAP];
    char binds_to[BQ_WORKER_RELATION_CAP];
    char after[BQ_WORKER_RELATION_CAP];
    char collect_mode[32];
    char user[BQ_WORKER_IDENTITY_CAP];
    char group[BQ_WORKER_IDENTITY_CAP];
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
    bool no_new_privileges;
    bool private_tmp;
    bool private_devices;
    bool private_network;
    bool protect_home;
    bool protect_system;
    bool protect_proc;
    bool restrict_suidsgid;
    bool protect_control_groups;
    bool protect_kernel_tunables;
    bool protect_kernel_modules;
    bool protect_kernel_logs;
    bool protect_clock;
    bool protect_hostname;
    bool lock_personality;
    bool memory_deny_write_execute;
    bool remove_ipc;
    bool keyring_private;
    bool restrict_namespaces;
    bool restrict_realtime;
    bool address_families_unix;
    bool syscall_architectures_native;
    bool syscall_filter_system_service;
    bool syscall_error_number_eperm;
    bool capability_sets_empty;
    bool security_properties_valid;
    bool paths_valid;
    char inaccessible_paths[BQ_PATH_CAP * 2 + 2];
    char read_only_paths[BQ_PATH_CAP + 1];
    char read_write_paths[BQ_PATH_CAP + 1];
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
    /* The final flag makes a pending worker cancellation interrupt the wait.
     * Cleanup joins pass false: they run after the outer unit is proven empty. */
    BqError (*join)(BqWorkerBackend*, int*, u64, bool);
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
    /* Absolute service-owned queue directory. Production services make it
     * inaccessible to the recipe; private test backends may leave it empty. */
    String8 queue_root;
    /* Set for an installed admitted service recipe even when a deterministic
     * injected backend supplies the manager seam. The backend must still
     * satisfy every observed security property; it only replaces process
     * control in tests. */
    bool production_path;
    /* Retirement only (#881 A1): the installed canonical campaign-budget
     * record (tools/throughput/retirement_budget.h). The worker authenticates
     * it against the job recipe profile's campaign-budget-sha256= pin and
     * derives the job's unit limit and execution deadline from its reviewed
     * ceiling (bq_worker_retirement_runtime). Empty, like the blocked
     * profile's missing pin, refuses a retirement job before launch; the
     * smoke recipe ignores it and keeps limits.runtime_max_usec. */
    String8 retirement_budget;
} BqWorkerConfig;

/* The per-file cap of the result-root file at relative `path`: the evidence
 * cap for a retirement evidence entry when `retirement`, else
 * BQ_WORKER_BUNDLE_FILE_CAP. */
BUSTER_F_DECL u64 bq_worker_bundle_file_cap(bool retirement, char const* path);
BUSTER_F_DECL BqError bq_worker_run(BqQueue* queue, BqWorkerConfig const* config, u64* id);
BUSTER_F_DECL BqError bq_worker_result_binding_validate(BqJob const* job);
BUSTER_F_DECL BqError bq_worker_unit(String8 lease_file, String8 job_id, String8 attempt_token,
                                     String8 recipe, String8 workspace_root, String8 base_revision,
                                     String8 candidate_revision, String8 result_root);
BUSTER_F_DECL void bq_worker_backend_systemd(BqWorkerBackend* backend);

#endif
