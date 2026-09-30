/* Linux single-job supervisor (coordinator) and transient worker-unit entry.
 * Ownership: #881-C lifecycle; README.md holds the deployment trust boundary.
 *
 * Coordinator: bq_worker_run takes the host lease before reservation, fixes
 * the job's limit and absolute deadline (bq_worker_execution_deadline; for
 * retirement bq_worker_retirement_runtime and bq_worker_outer_arguments),
 * launches the outer unit through the broker, hands it the lease
 * (bq_worker_lease_handoff_send), follows the phase channel
 * (bq_worker_phase_join), stops and proves absence (bq_worker_stop,
 * bq_worker_stop_children) and finishes the job (bq_worker_finish). Restart
 * recovery is bq_worker_recover, which reclaims a live unit's lease from its
 * keeper before any signal (bq_worker_lease_reclaim).
 *
 * Unit: bq_worker_unit adopts the transferred lease
 * (bq_worker_lease_handoff_receive), starts its lease keeper
 * (bq_worker_lease_keeper_start, bq_worker_lease_keeper_serve), pauses for the
 * coordinator's checks and executes the installed recipe. A retirement job
 * with a complete profile (bq_worker_unit_pinned) instead forks the producer
 * in retirement_worker_unit.c (bq_retirement_worker_unit_run); the compiled
 * blocked profile is refused before the lease handoff.
 *
 * Retirement coordinator (#881 PR 4; retirement_coordinator.c): bq_worker_run
 * is bq_worker_run_pinned with the installed seams. Its recipe gates
 * (bq_retirement_request_valid_pinned, bq_worker_recipe_service in
 * bq_worker_finalization_recipe, bq_worker_recipe_launchable) admit the
 * retirement recipe only when bq_retirement_profile_complete accepts the
 * seams' profile, which the compiled blocked one never is. They decide per
 * recipe (bq_worker_recipe_admitted, #881 N1), so the queue's own admission
 * predicate never substitutes for that completeness gate. Such a job loads
 * the installed budget (bq_retirement_coordinator_budget_load), follows a
 * BQPHASE2 channel whose RETIREMENT_READY digest bq_worker_phase_accept keeps
 * and whose MEASURED digest it hands off (bq_retirement_coordinator_handoff,
 * bound by the context chain to the coordinator's own digests) before
 * writing MEASURED's record and acknowledging, and succeeds only if
 * bq_worker_retirement_finalize replays the ready record and finds the
 * journalled authority in bq_worker_finish. Recovery reloads those digests
 * from the durable queue records (bq_worker_retirement_reload,
 * bq_worker_phase_record_digest) and never rewrites a durable success.
 * Before recovery finishes a retirement job, and before bq_worker_finish
 * records any failed, cancelled or interrupted outcome for one (a failed
 * MEASURED handoff in the live run among them),
 * bq_worker_retirement_handoff_hold classifies its MEASURED handoff
 * (bq_retirement_coordinator_handoff_class, recovery L2). An incomplete or
 * inconsistent one is poisoned (bq_retirement_poison_write in workspace.c) and
 * held for reconciliation with its evidence kept; a poisoned job never
 * finalizes, reconciles succeeded or exports (bq_retirement_poisoned).
 *
 * Manager seam: bq_worker_backend_systemd (bq_systemd_observe, bq_systemd_signal,
 * bq_systemd_join); readback checks bq_worker_observed and
 * bq_worker_verify_cgroup. Durable identity: bq_worker_record_write,
 * bq_worker_instance_write. Results: bq_worker_result_open,
 * bq_worker_bundle_validate_recipe, bq_worker_result_evidence.
 */
#include "worker_linux.h"
#include "phase_channel.h"

#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/vfs.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include "systemd_runtime.h"

#define BQ_WORKER_EXECUTABLE "/usr/local/libexec/buster-bench-service"
#define BQ_RECIPE_EXECUTABLE "/usr/local/libexec/buster-bench-build"
#define BQ_SYSTEMD_BROKER "/usr/local/libexec/buster-bench-systemd-broker"
#define BQ_BROKER_QUEUE "/var/lib/buster-bench/queue"
#define BQ_BROKER_LEASE "/var/lib/buster-bench/lease/host.lock"
#define BQ_BROKER_WORKSPACES "/var/lib/buster-bench/workspaces"
#define BQ_WORKER_CGROUP_SLICE "/buster.slice/buster-bench.slice"
#define BQ_WORKER_CGROUP_PREFIX BQ_WORKER_CGROUP_SLICE "/"
#define BQ_SYSTEMCTL "/usr/bin/systemctl"
#define BQ_WORKER_COMMAND_MILLISECONDS 5000u
#define BQ_WORKER_STOP_MILLISECONDS 10000u
#define BQ_WORKER_POLL_MILLISECONDS 100u
#define BQ_WORKER_LEASE_HANDOFF_NAME ".lease-handoff"
#define BQ_WORKER_LEASE_HANDOFF_MILLISECONDS 5000u
#define BQ_WORKER_LEASE_MAGIC "BQ-LEASE-HANDOFF-V2"
/* The kernel's readlink suffix for a /proc/<pid>/fd entry whose name was unlinked. */
#define BQ_WORKER_DELETED_SUFFIX " (deleted)"

typedef struct BqSystemdContext
{
    pid_t pid;
    bool starting;
} BqSystemdContext;

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_worker_cancel_signal;
BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_worker_shutdown_signal;
/* Set by the transport handler while ownership is being handed to this
 * worker.  Once bq_worker_run installs its own handlers, SIGTERM/SIGINT are
 * observed directly there; this flag closes the small pre-install window. */
BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_worker_transport_stop_signal;

BUSTER_GLOBAL_LOCAL u64 bq_worker_monotonic_milliseconds(void)
{
    struct timespec now = {0};
    bool ok = clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec >= 0;
    u64 seconds = ok ? (u64)now.tv_sec : UINT64_MAX / 1000;
    u64 result = seconds <= (UINT64_MAX - (u64)now.tv_nsec / 1000000) / 1000 ?
                 seconds * 1000 + (u64)now.tv_nsec / 1000000 : UINT64_MAX;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 bq_worker_deadline(u64 now, u64 milliseconds)
{
    u64 result = milliseconds <= UINT64_MAX - now ? now + milliseconds : UINT64_MAX;
    return result;
}

/* RuntimeMax is an inner unit limit. The coordinator also needs an absolute
 * budget starting under the host lease, before materialization can do work.
 * Reject an unrepresentable budget instead of silently disabling the cap. */
BUSTER_GLOBAL_LOCAL bool bq_worker_execution_deadline(u64 start, u64 runtime_usec, u64* deadline)
{
    u64 milliseconds = runtime_usec / 1000 + (runtime_usec % 1000 != 0);
    bool ok = deadline && milliseconds && milliseconds <= UINT64_MAX - start;
    if (ok) *deadline = start + milliseconds;
    return ok;
}

/* The campaign-budget record (tools/throughput/retirement_budget.h, whose
 * throughput dependencies stay out of the service unit): its schema line, a
 * derivation line, then these scalar keys first and in this order, each
 * `key=<decimal ns>` without leading zeros. Index 0 is the reviewed whole-job
 * ceiling; the rest are the record's fixed-phase bounds, two of them per
 * collection stage. */
#define BQ_WORKER_BUDGET_SCHEMA "schema=tp-retirement-campaign-budget-v2"
#define BQ_WORKER_BUDGET_BYTES 4096u
#define BQ_WORKER_BUDGET_STAGES 2u
BUSTER_GLOBAL_LOCAL char const* const bq_worker_budget_keys[] = {
    "reviewed-ns", "reservation-ns", "materialization-ns", "baseline-build-ns", "candidate-build-ns",
    "correctness-ns", "settling-per-stage-ns", "aa-qualification-ns", "aa-receipt-sealing-ns",
    "sample-export-per-stage-ns", "final-statistics-ns", "final-sealing-ns", "cleanup-ns"};
BUSTER_GLOBAL_LOCAL bool const bq_worker_budget_per_stage[] = {
    false, false, false, false, false, false, true, false, false, true, false, false, false};
_Static_assert(BUSTER_ARRAY_LENGTH(bq_worker_budget_keys) == BUSTER_ARRAY_LENGTH(bq_worker_budget_per_stage),
               "budget key table mismatch");

/* One `key=value` line at `*offset`, advancing past its LF. */
BUSTER_GLOBAL_LOCAL bool bq_worker_budget_line(String8 record, u64* offset, char const* key, u64* value)
{
    u64 key_length = strlen(key);
    u64 start = *offset + key_length + 1;
    u64 end = start;
    while (end < record.length && record.pointer[end] != '\n') end += 1;
    String8 digits = {record.pointer + start, end > start ? end - start : 0};
    IntegerParsingU64 parsed = string8_parse_u64_decimal(digits);
    bool ok = start < record.length && !memcmp(record.pointer + *offset, key, (size_t)key_length) &&
              record.pointer[*offset + key_length] == '=' && end < record.length && digits.length &&
              (digits.pointer[0] != '0' || digits.length == 1) &&
              parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == digits.length;
    *value = ok ? parsed.value : 0;
    if (ok) *offset = end + 1;
    return ok;
}

/* #881 A1: the retirement recipe's unit limit and absolute execution budget
 * are its reviewed whole-job ceiling, read from the installed campaign-budget
 * record only after its exact bytes hash to the recipe profile's
 * campaign-budget-sha256= pin (the pin is the digest of the canonical
 * encoding, so matching bytes are that encoding). The ceiling already
 * contains every fixed-phase bound the record names (the `fixed` term of its
 * derivation, as tp_retirement_budget_preflight sums it), so those bounds are
 * a floor the ceiling must cover, never an addition to it. The value is
 * rounded down to whole seconds, so the enforced limit never exceeds the
 * reviewed ceiling, and must lie in the broker's closed range
 * (systemd_runtime.h). A missing pin or record, a digest mismatch, another
 * schema or an out-of-range ceiling refuses the job before launch; nothing
 * observed during a run can change the value. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_retirement_runtime(String8 profile, String8 record, u64* runtime_usec)
{
    char pin[SHA256_HEX_CAPACITY] = {0};
    char digest[SHA256_HEX_CAPACITY] = {0};
    u64 schema_length = sizeof(BQ_WORKER_BUDGET_SCHEMA) - 1;
    bool ok = runtime_usec && record.pointer && record.length > schema_length &&
              record.length < BQ_WORKER_BUDGET_BYTES && record.pointer[record.length - 1] == '\n' &&
              bq_retirement_profile_sha(profile, S8("campaign-budget-sha256="), pin);
    if (ok) bq_digest(record.pointer, (u32)record.length, (char8*)digest);
    ok = ok && !memcmp(digest, pin, SHA256_HEX_CAPACITY) &&
         !memcmp(record.pointer, BQ_WORKER_BUDGET_SCHEMA "\nderivation=", schema_length + 12);
    u64 offset = ok ? schema_length + 12 : 0;
    while (ok && offset < record.length && record.pointer[offset] != '\n') offset += 1;
    ok = ok && offset < record.length;
    offset += 1;
    u64 ceiling = 0, fixed = 0;
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(bq_worker_budget_keys); index += 1)
    {
        u64 value = 0;
        ok = bq_worker_budget_line(record, &offset, bq_worker_budget_keys[index], &value);
        if (ok && index == 0) ceiling = value;
        else if (ok)
        {
            u64 stages = bq_worker_budget_per_stage[index] ? BQ_WORKER_BUDGET_STAGES : 1;
            ok = value && value <= (UINT64_MAX - fixed) / stages;
            if (ok) fixed += value * stages;
        }
    }
    u64 usec = ok ? ceiling / UINT64_C(1000000000) * BQ_SYSTEMD_USEC_PER_SECOND : 0;
    /* The floor applies to the enforced (rounded) limit, not the record. */
    ok = ok && ceiling && bq_systemd_retirement_runtime_valid(usec) && fixed <= usec * UINT64_C(1000);
    if (runtime_usec) *runtime_usec = ok ? usec : 0;
    return ok ? BQ_OK : BQ_RECIPE_MISMATCH;
}

/* The typed broker argv for the outer unit. Smoke keeps start-outer and its
 * six values; retirement adds its derived limit in decimal microseconds,
 * which the broker bounds again (systemd_broker.c, bq_broker_cli). */
BUSTER_GLOBAL_LOCAL bool bq_worker_outer_arguments(BqRecipe recipe, u64 runtime_usec, char const* job_id,
                                                   char const* attempt_token, char const* base,
                                                   char const* candidate, char runtime_text[32],
                                                   char const* arguments[8], u32* count)
{
    bool retirement = recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED;
    int length = retirement ? snprintf(runtime_text, 32, "%" PRIu64, (uint64_t)runtime_usec) : 0;
    bool ok = job_id && attempt_token && base && candidate && count &&
              (recipe == BQ_RECIPE_VALIDATE_BUSTER ||
               (retirement && bq_systemd_retirement_runtime_valid(runtime_usec) && length > 0 && length < 32));
    for (u32 index = 0; index < 8; index += 1) arguments[index] = NULL;
    if (ok)
    {
        arguments[0] = BQ_SYSTEMD_BROKER;
        arguments[1] = retirement ? BQ_SYSTEMD_RETIREMENT_OUTER_VERB : "start-outer";
        arguments[2] = job_id;
        arguments[3] = attempt_token;
        arguments[4] = base;
        arguments[5] = candidate;
        arguments[6] = retirement ? runtime_text : NULL;
    }
    if (count) *count = ok ? (retirement ? 7u : 6u) : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL u32 bq_worker_remaining(u64 deadline)
{
    u64 now = bq_worker_monotonic_milliseconds();
    u64 remaining = now < deadline ? deadline - now : 0;
    u32 result = remaining > UINT32_MAX ? UINT32_MAX : (u32)remaining;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_sleep_until(u64 deadline)
{
    BqError error = BQ_OK;
    u64 now = bq_worker_monotonic_milliseconds();
    while (error == BQ_OK && now < deadline)
    {
        struct timespec absolute = {(time_t)(deadline / 1000), (long)(deadline % 1000) * 1000 * 1000};
        int result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &absolute, NULL);
        if (result != 0 && result != EINTR) error = BQ_IO;
        now = bq_worker_monotonic_milliseconds();
    }
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_worker_cancel_handler(int signal_number)
{
    if (signal_number == SIGTERM || signal_number == SIGINT)
    {
        bq_worker_shutdown_signal = 1;
    }
    bq_worker_cancel_signal = 1;
}

typedef struct BqWorkerLease { int descriptor; } BqWorkerLease;
typedef struct BqWorkerLeaseHandoff BqWorkerLeaseHandoff;
typedef struct BqWorkerLeaseMessage BqWorkerLeaseMessage;
typedef struct BqWorkerFinalization BqWorkerFinalization;

struct BqWorkerLeaseHandoff
{
    int listener;
    int parent;
    dev_t device;
    ino_t inode;
    char path[BQ_PATH_CAP + 1];
};

struct BqWorkerLeaseMessage
{
    char magic[32];
    char lease_path[BQ_PATH_CAP + 1];
    char preparation_sha256[SHA256_HEX_CAPACITY];
    u64 job_id;
    u64 attempt_token;
    u64 device;
    u64 inode;
    u64 execution_deadline_ns;
    u32 phase;
};

/* Handoff phases carry the execution deadline; the reverse reclaim phases
 * (bq_worker_lease_keeper_serve, bq_worker_lease_reclaim) carry none. */
enum
{
    BQ_WORKER_LEASE_REQUEST = 1,
    BQ_WORKER_LEASE_RESPONSE,
    BQ_WORKER_LEASE_ACK,
    BQ_WORKER_LEASE_RECLAIM_REQUEST,
    BQ_WORKER_LEASE_RECLAIM_RESPONSE,
    BQ_WORKER_LEASE_RECLAIM_ACK,
};

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_phase_timed(u32 phase)
{
    bool timed = phase == BQ_WORKER_LEASE_RESPONSE || phase == BQ_WORKER_LEASE_ACK;
    return timed;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_directory_owner(int fd, bool final_private, bool allow_sticky)
{
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) &&
              (info.st_uid == 0 || info.st_uid == geteuid());
    if (ok && final_private)
        ok = info.st_uid == geteuid() && (info.st_mode & 077) == 0;
    else if (ok && (info.st_mode & 022) != 0)
        ok = allow_sticky && info.st_uid == 0 && (info.st_mode & S_ISVTX) != 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_temp_name(char const* prefix, char* output, u32 capacity)
{
    char const digits[] = "0123456789abcdef";
    u8 entropy[16] = {0};
    int source = open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    u32 used = 0;
    bool ok = source >= 0;
    while (ok && used < sizeof(entropy))
    {
        ssize_t count = read(source, entropy + used, sizeof(entropy) - used);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) used += (u32)count;
    }
    if (source >= 0 && close(source) != 0) ok = false;
    u32 prefix_length = prefix ? (u32)strlen(prefix) : 0;
    u32 required = prefix_length + 1 + (u32)sizeof(entropy) * 2 + 4;
    ok = ok && output && capacity > required;
    if (ok)
    {
        memcpy(output, prefix, prefix_length);
        output[prefix_length] = '.';
        for (u32 index = 0; index < sizeof(entropy); index += 1)
        {
            output[prefix_length + 1 + index * 2] = digits[entropy[index] >> 4];
            output[prefix_length + 2 + index * 2] = digits[entropy[index] & 15];
        }
        memcpy(output + prefix_length + 1 + sizeof(entropy) * 2, ".tmp", 5);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_worker_open_trusted_directory(String8 path, bool final_private,
                                                           bool allow_sticky_final)
{
    char text[BQ_PATH_CAP + 1];
    int current = bq_string_path(path, text) ? open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u64 offset = 1;
    while (current >= 0 && offset < path.length)
    {
        bool final = true;
        u64 end = offset;
        while (end < path.length && path.pointer[end] != '/') end += 1;
        if (end < path.length) final = false;
        if (!bq_worker_directory_owner(current, false, true))
        {
            close(current);
            current = -1;
        }
        else
        {
            char name[BQ_PATH_CAP + 1];
            u64 length = end - offset;
            memcpy(name, path.pointer + offset, (size_t)length);
            name[length] = 0;
            int next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            close(current);
            current = next;
            if (current >= 0 && final &&
                !bq_worker_directory_owner(current, final_private, allow_sticky_final))
            {
                close(current);
                current = -1;
            }
        }
        offset = end + 1;
    }
    if (current >= 0 && !bq_worker_directory_owner(current, final_private, allow_sticky_final))
    {
        close(current);
        current = -1;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL int bq_worker_open_lease_parent(char const* path, char leaf[BQ_PATH_CAP + 1])
{
    String8 whole = string_from_pointer(path);
    char validated[BQ_PATH_CAP + 1];
    int parent = -1;
    if (bq_string_path(whole, validated))
    {
        char* slash = strrchr(validated, '/');
        if (slash && slash[1])
        {
            snprintf(leaf, BQ_PATH_CAP + 1, "%s", slash + 1);
            u64 length = (u64)(slash - validated);
            String8 directory = {(char8*)validated, length ? length : 1};
            parent = bq_worker_open_trusted_directory(directory, false, false);
        }
    }
    return parent;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_permissions(char const* path, struct stat const* info)
{
    bool broker_lease = path && !strcmp(path, BQ_BROKER_LEASE);
    bool ok = info && info->st_uid == geteuid() && (broker_lease ?
              ((info->st_mode & 07777) == 0640 && info->st_gid == getegid()) :
              ((info->st_mode & 077) == 0));
    return ok;
}

/* Same stable-inode/open-description contract consumed by throughput's
 * --lease-fd.  Kept local so bench_service does not compile unrelated
 * throughput qualification/reporting functions into the service. */
BUSTER_GLOBAL_LOCAL int bq_worker_lease_acquire(char const* path, BqWorkerLease* lease)
{
    lease->descriptor = -1;
    errno = 0;
    char leaf[BQ_PATH_CAP + 1];
    int parent = path ? bq_worker_open_lease_parent(path, leaf) : -1;
    int fd = parent >= 0 ? openat(parent, leaf, O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600) : -1;
    int error = fd < 0 ? (!path || path[0] != '/' ? EINVAL : errno ? errno : EINVAL) : 0;
    if (parent >= 0) close(parent);
    if (!error && fd < 3)
    {
        int duplicate = fcntl(fd, F_DUPFD, 3);
        error = duplicate < 0 ? errno : 0;
        close(fd);
        fd = duplicate;
    }
    struct stat info;
    if (!error && fstat(fd, &info) != 0) error = errno;
    if (!error && (!S_ISREG(info.st_mode) || info.st_nlink != 1)) error = EINVAL;
    if (!error && !bq_worker_lease_permissions(path, &info)) error = EACCES;
    if (!error && flock(fd, LOCK_EX | LOCK_NB) != 0) error = errno;
    if (!error) lease->descriptor = fd;
    else if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL int bq_worker_lease_adopt(char const* path, int fd, BqWorkerLease* lease)
{
    lease->descriptor = -1;
    errno = 0;
    int inspection = -1;
    int flags = fd >= 3 ? fcntl(fd, F_GETFD) : -1;
    int error = !path || path[0] != '/' || fd < 3 ? EINVAL : flags < 0 ? errno : 0;
    struct stat inherited, selected;
    if (!error && fstat(fd, &inherited) != 0) error = errno;
    if (!error && (!S_ISREG(inherited.st_mode) || inherited.st_nlink != 1)) error = EINVAL;
    if (!error && !bq_worker_lease_permissions(path, &inherited)) error = EACCES;
    char leaf[BQ_PATH_CAP + 1];
    int parent = !error ? bq_worker_open_lease_parent(path, leaf) : -1;
    if (!error && parent < 0) error = errno ? errno : EINVAL;
    if (!error) inspection = openat(parent, leaf, O_RDWR | O_NOFOLLOW | O_NONBLOCK);
    if (!error && inspection < 0) error = errno;
    if (!error && fstat(inspection, &selected) != 0) error = errno;
    if (!error && (selected.st_dev != inherited.st_dev || selected.st_ino != inherited.st_ino)) error = EINVAL;
    if (!error)
    {
        int probe = flock(inspection, LOCK_SH | LOCK_NB);
        if (probe == 0) error = EINVAL;
        else if (errno != EWOULDBLOCK && errno != EAGAIN) error = errno;
    }
    if (!error && flock(fd, LOCK_EX | LOCK_NB) != 0) error = errno;
    if (!error && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) error = errno;
    if (inspection >= 0) close(inspection);
    if (parent >= 0) close(parent);
    if (!error) lease->descriptor = fd;
    else if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL int bq_worker_lease_adopt_transferred(char const* path, int fd, u64 device, u64 inode,
                                                           BqWorkerLease* lease)
{
    lease->descriptor = -1;
    int flags = fd >= 3 ? fcntl(fd, F_GETFD) : -1;
    struct stat info = {0};
    int error = fd < 3 ? EINVAL : flags < 0 ? errno : fstat(fd, &info) != 0 ? errno : 0;
    if (!error && (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_uid != geteuid() ||
                   !bq_worker_lease_permissions(path, &info) ||
                   (u64)info.st_dev != device || (u64)info.st_ino != inode))
        error = EACCES;
    if (!error && flock(fd, LOCK_EX | LOCK_NB) != 0) error = errno;
    if (!error && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) error = errno;
    if (!error) lease->descriptor = fd;
    else if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_worker_lease_release(BqWorkerLease* lease)
{
    if (lease->descriptor >= 0) close(lease->descriptor);
    lease->descriptor = -1;
}

/* The supervisor can pause the unit after handoff. Before exec, recheck the
 * held lease against its installed pathname and conflicting lock so a renamed
 * or replaced lease cannot carry the worker into a different attempt.
 *
 * The outer unit has the lease pathname in InaccessiblePaths (the broker's
 * fixed sandbox), so systemd mounts an inaccessible node over it and the
 * pathname cannot be reopened there. The recheck goes through the held
 * description instead: /proc/self/fd/<dup> names the file's current dentry in
 * its own mount tree, which must be exactly the lease pathname (a rename away
 * names the new path, a replacement over it reads "(deleted)"). Reopening
 * that magic link yields a fresh description of the same inode, which must
 * conflict with the lock while the held description still converts, so the
 * held reference is the holder. The duplicate shares the original lock and is
 * closed without releasing it. A configured path that itself ends in the
 * deleted-entry suffix is refused, so an unlinked name can never string-match. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_recheck_for_exec(char const* path,
                                                           BqWorkerLease const* lease)
{
    size_t const suffix_length = sizeof(BQ_WORKER_DELETED_SUFFIX) - 1;
    size_t path_length = path ? strlen(path) : 0;
    bool deleted_name = path_length >= suffix_length &&
                        !memcmp(path + path_length - suffix_length, BQ_WORKER_DELETED_SUFFIX, suffix_length);
    int duplicate = path && !deleted_name && lease && lease->descriptor >= 3 ?
        fcntl(lease->descriptor, F_DUPFD_CLOEXEC, 3) : -1;
    char self[64];
    char named[BQ_PATH_CAP + 2];
    ssize_t named_length = duplicate >= 3 && snprintf(self, sizeof(self), "/proc/self/fd/%d", duplicate) > 0 ?
                           readlink(self, named, sizeof(named)) : -1;
    bool ok = named_length > 0 && (size_t)named_length == path_length && !memcmp(named, path, path_length);
    int inspection = ok ? open(self, O_RDONLY | O_CLOEXEC | O_NONBLOCK) : -1;
    struct stat held = {0}, reopened = {0};
    /* The magic-link open always reaches the held file itself, so the
     * device/inode equality is a consistency check, not an identity proof. */
    ok = inspection >= 0 && fstat(duplicate, &held) == 0 && fstat(inspection, &reopened) == 0 &&
         S_ISREG(held.st_mode) && held.st_nlink == 1 && bq_worker_lease_permissions(path, &held) &&
         held.st_dev == reopened.st_dev && held.st_ino == reopened.st_ino;
    /* The shared probe and the upgrade are two steps, as in
     * bq_worker_lease_adopt: if another description held the lock at the probe
     * and released it before the upgrade, the upgrade takes a fresh lock. That
     * window cannot be closed without kernel lock-owner reporting. */
    if (ok)
    {
        errno = 0;
        int probe = flock(inspection, LOCK_SH | LOCK_NB);
        ok = probe != 0 && (errno == EWOULDBLOCK || errno == EAGAIN);
    }
    ok = ok && flock(duplicate, LOCK_EX | LOCK_NB) == 0;
    if (inspection >= 0) close(inspection);
    if (duplicate >= 0) close(duplicate);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_text(String8 input, char* output, u32 capacity)
{
    bool ok = input.length && input.length < capacity && !memchr(input.pointer, 0, (size_t)input.length);
    if (ok)
    {
        memcpy(output, input.pointer, (size_t)input.length);
        output[input.length] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_worker_copy_field(char* output, u32 capacity, char const* input)
{
    u32 length = 0;
    bool ok = output && capacity && input;
    while (ok && length < capacity && input[length]) length += 1;
    ok = ok && length < capacity;
    if (ok) memcpy(output, input, (size_t)length + 1);
    else if (output && capacity) output[0] = 0;
    int result = ok ? (int)length : -1;
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_socket_path(char const* result_root,
                                                       char output[BQ_PATH_CAP + 1])
{
    u64 root_length = result_root ? strlen(result_root) : 0;
    u64 length = root_length + 1 + sizeof(BQ_WORKER_LEASE_HANDOFF_NAME) - 1;
    bool ok = root_length > 0 && result_root[0] == '/' && length < BQ_PATH_CAP + 1 &&
              length < sizeof(((struct sockaddr_un*)0)->sun_path);
    if (ok)
    {
        memcpy(output, result_root, (size_t)root_length);
        output[root_length] = '/';
        memcpy(output + root_length + 1, BQ_WORKER_LEASE_HANDOFF_NAME, sizeof(BQ_WORKER_LEASE_HANDOFF_NAME));
    }
    return ok;
}

/* A successful #1018 record has one exact lowercase digest. The smoke
 * recipe has no preparation record and must use the empty identity. */
BUSTER_GLOBAL_LOCAL bool bq_worker_preparation_digest_valid(char const* digest)
{
    size_t length = digest ? strnlen(digest, SHA256_HEX_CAPACITY) : SHA256_HEX_CAPACITY;
    bool ok = length == 0 || length == SHA256_HEX_CAPACITY - 1;
    for (size_t index = 0; ok && index < length; index += 1)
        ok = (digest[index] >= '0' && digest[index] <= '9') ||
             (digest[index] >= 'a' && digest[index] <= 'f');
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_preparation_matches_recipe(BqRecipe recipe, char const* digest)
{
    bool ok = bq_worker_preparation_digest_valid(digest) &&
              ((recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED && digest[0] != 0) ||
               (recipe == BQ_RECIPE_VALIDATE_BUSTER && digest[0] == 0));
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_message_make(BqWorkerLeaseMessage* message, u32 phase,
                                                        char const* lease_path, u64 job_id,
                                                        u64 attempt_token, u64 device, u64 inode,
                                                        char const* preparation_sha256,
                                                        u64 execution_deadline_ns)
{
    *message = (BqWorkerLeaseMessage){0};
    int length = lease_path ? snprintf(message->lease_path, sizeof(message->lease_path), "%s", lease_path) : -1;
    bool ok = length > 0 && (u32)length < sizeof(message->lease_path) &&
              bq_worker_preparation_digest_valid(preparation_sha256) &&
              phase >= BQ_WORKER_LEASE_REQUEST && phase <= BQ_WORKER_LEASE_RECLAIM_ACK &&
              ((!bq_worker_lease_phase_timed(phase) && execution_deadline_ns == 0) ||
               (bq_worker_lease_phase_timed(phase) && execution_deadline_ns > bq_phase_clock()));
    if (ok)
    {
        memcpy(message->magic, BQ_WORKER_LEASE_MAGIC, sizeof(BQ_WORKER_LEASE_MAGIC));
        memcpy(message->preparation_sha256, preparation_sha256, strlen(preparation_sha256));
        message->job_id = job_id;
        message->attempt_token = attempt_token;
        message->device = device;
        message->inode = inode;
        message->execution_deadline_ns = execution_deadline_ns;
        message->phase = phase;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_message_matches(BqWorkerLeaseMessage const* message, u32 phase,
                                                           char const* lease_path, u64 job_id,
                                                           u64 attempt_token, u64 device, u64 inode,
                                                           char const* preparation_sha256,
                                                           u64 execution_deadline_ns)
{
    char expected[SHA256_HEX_CAPACITY] = {0};
    if (bq_worker_preparation_digest_valid(preparation_sha256))
        memcpy(expected, preparation_sha256, strlen(preparation_sha256));
    bool ok = message && !memcmp(message->magic, BQ_WORKER_LEASE_MAGIC, sizeof(BQ_WORKER_LEASE_MAGIC)) &&
              message->phase == phase && message->job_id == job_id && message->attempt_token == attempt_token &&
              message->device == device && message->inode == inode &&
              message->execution_deadline_ns == execution_deadline_ns &&
              (bq_worker_lease_phase_timed(phase) ? execution_deadline_ns > bq_phase_clock() :
                                                    execution_deadline_ns == 0) && lease_path &&
              preparation_sha256 && bq_worker_preparation_digest_valid(preparation_sha256) &&
              !strcmp(message->lease_path, lease_path) &&
              !memcmp(message->preparation_sha256, expected, sizeof(expected));
    return ok;
}

#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL bool bq_worker_test_handoff_unlink_failure;
BUSTER_GLOBAL_LOCAL bool bq_worker_test_handoff_fsync_failure;
BUSTER_GLOBAL_LOCAL bool bq_worker_test_handoff_listener_close_failure;
BUSTER_GLOBAL_LOCAL bool bq_worker_test_handoff_parent_close_failure;
BUSTER_GLOBAL_LOCAL u32 bq_worker_test_recipe_exec_count;
#endif

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_handoff_close(BqWorkerLeaseHandoff* handoff)
{
    bool ok = handoff != NULL;
    if (ok && handoff->listener >= 0 && close(handoff->listener) != 0) ok = false;
#ifdef BUSTER_BENCH_SERVICE_TEST
    if (ok && handoff->listener >= 0 && bq_worker_test_handoff_listener_close_failure) ok = false;
#endif
    if (handoff && handoff->parent >= 0)
    {
        struct stat info = {0};
        errno = 0;
        bool present = handoff->path[0] && fstatat(handoff->parent, BQ_WORKER_LEASE_HANDOFF_NAME, &info,
                                                 AT_SYMLINK_NOFOLLOW) == 0;
        bool absent = !present && errno == ENOENT;
        bool owned = present && S_ISSOCK(info.st_mode) && info.st_dev == handoff->device &&
                     info.st_ino == handoff->inode;
        if (!absent && !owned) ok = false;
        if (owned)
        {
#ifdef BUSTER_BENCH_SERVICE_TEST
            if (bq_worker_test_handoff_unlink_failure) ok = false;
            else
#endif
            if (unlinkat(handoff->parent, BQ_WORKER_LEASE_HANDOFF_NAME, 0) != 0) ok = false;
        }
        if (fsync(handoff->parent) != 0) ok = false;
#ifdef BUSTER_BENCH_SERVICE_TEST
        if (bq_worker_test_handoff_fsync_failure) ok = false;
#endif
        if (close(handoff->parent) != 0) ok = false;
#ifdef BUSTER_BENCH_SERVICE_TEST
        if (bq_worker_test_handoff_parent_close_failure) ok = false;
#endif
    }
    if (handoff) *handoff = (BqWorkerLeaseHandoff){.listener = -1, .parent = -1};
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_lease_handoff_purge_stale(int result_directory)
{
    struct stat info = {0};
    errno = 0;
    bool present = result_directory >= 0 &&
                   fstatat(result_directory, BQ_WORKER_LEASE_HANDOFF_NAME, &info, AT_SYMLINK_NOFOLLOW) == 0;
    BqError error = BQ_OK;
    if (present)
    {
        error = S_ISSOCK(info.st_mode) && info.st_uid == geteuid() && (info.st_mode & 077) == 0 &&
                unlinkat(result_directory, BQ_WORKER_LEASE_HANDOFF_NAME, 0) == 0 &&
                fsync(result_directory) == 0 ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    else if (result_directory < 0 || errno != ENOENT)
    {
        error = BQ_IO;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_handoff_open(char const* result_root, int result_directory,
                                                        BqWorkerLeaseHandoff* handoff)
{
    *handoff = (BqWorkerLeaseHandoff){.listener = -1, .parent = -1};
    char path[BQ_PATH_CAP + 1];
    bool ok = result_root && bq_worker_lease_socket_path(result_root, path) && result_directory >= 0;
    if (ok)
    {
        ok = bq_worker_directory_owner(result_directory, true, false) &&
             (handoff->parent = fcntl(result_directory, F_DUPFD_CLOEXEC, 3)) >= 0;
    }
    struct stat existing = {0};
    if (ok)
    {
        errno = 0;
        bool present = fstatat(handoff->parent, BQ_WORKER_LEASE_HANDOFF_NAME, &existing, AT_SYMLINK_NOFOLLOW) == 0;
        ok = !present && errno == ENOENT;
    }
    if (ok)
    {
        handoff->listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        ok = handoff->listener >= 0;
    }
    struct sockaddr_un address = {0};
    if (ok)
    {
        size_t length = strlen(path);
        address.sun_family = AF_UNIX;
        memcpy(address.sun_path, path, length + 1);
        socklen_t address_size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1);
        mode_t prior_umask = umask(0077);
        int bound = bind(handoff->listener, (struct sockaddr*)&address, address_size);
        int saved_errno = errno;
        umask(prior_umask);
        errno = saved_errno;
        ok = bound == 0;
    }
    if (ok)
    {
        struct stat socket_info = {0};
        ok = fstatat(handoff->parent, BQ_WORKER_LEASE_HANDOFF_NAME, &socket_info, AT_SYMLINK_NOFOLLOW) == 0 &&
             S_ISSOCK(socket_info.st_mode) && socket_info.st_uid == geteuid() && (socket_info.st_mode & 077) == 0;
        if (ok)
        {
            handoff->device = socket_info.st_dev;
            handoff->inode = socket_info.st_ino;
            memcpy(handoff->path, path, strlen(path) + 1);
        }
    }
    if (ok) ok = listen(handoff->listener, 1) == 0;
    if (!ok) bq_worker_lease_handoff_close(handoff);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_peer_matches(int descriptor)
{
    struct ucred peer = {0};
    socklen_t size = sizeof(peer);
    bool ok = getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && size == sizeof(peer) &&
              peer.uid == geteuid() && peer.gid == getegid();
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_handoff_poll(int descriptor, short events, u64 deadline)
{
    struct pollfd waiting = {descriptor, events, 0};
    u32 remaining = bq_worker_remaining(deadline);
    int result = remaining ? poll(&waiting, 1, remaining) : 0;
    bool ok = result > 0 && (waiting.revents & (events | POLLERR | POLLHUP)) != 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_lease_handoff_send(BqWorkerLeaseHandoff* handoff, int lease_fd,
                                                          char const* lease_path, u64 job_id,
                                                          u64 attempt_token, char const* preparation_sha256,
                                                          u64 execution_deadline_ms,
                                                          int* phase_descriptor)
{
    if (phase_descriptor) *phase_descriptor = -1;
    u64 execution_deadline_ns = 0;
    BqError error = bq_worker_preparation_digest_valid(preparation_sha256) &&
                    bq_phase_deadline_from_milliseconds(execution_deadline_ms, &execution_deadline_ns) &&
                    execution_deadline_ns > bq_phase_clock() ? BQ_IO : BQ_BAD_REQUEST;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_WORKER_LEASE_HANDOFF_MILLISECONDS);
    bool transferred = false;
    while (error == BQ_IO && !transferred && bq_worker_monotonic_milliseconds() < deadline)
    {
        int client = -1;
        if (bq_worker_lease_handoff_poll(handoff->listener, POLLIN, deadline))
            client = accept4(handoff->listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (client < 0)
        {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) break;
        }
        else if (!bq_worker_lease_peer_matches(client))
        {
            close(client);
        }
        else if (!bq_worker_lease_handoff_poll(client, POLLIN, deadline))
        {
            close(client);
        }
        else
        {
            BqWorkerLeaseMessage request = {0};
            ssize_t received = recv(client, &request, sizeof(request), MSG_DONTWAIT);
            struct stat lease_info = {0};
            bool request_ok = received == (ssize_t)sizeof(request) &&
                              bq_worker_lease_message_matches(&request, BQ_WORKER_LEASE_REQUEST, lease_path,
                                                               job_id, attempt_token, 0, 0, "", 0) &&
                              fstat(lease_fd, &lease_info) == 0 && S_ISREG(lease_info.st_mode) &&
                              lease_info.st_nlink == 1 && bq_worker_lease_permissions(lease_path, &lease_info);
            if (request_ok)
            {
                BqWorkerLeaseMessage response;
                request_ok = bq_worker_lease_message_make(&response, BQ_WORKER_LEASE_RESPONSE, lease_path, job_id,
                                                           attempt_token, (u64)lease_info.st_dev, (u64)lease_info.st_ino,
                                                           preparation_sha256, execution_deadline_ns);
                char control[CMSG_SPACE(sizeof(lease_fd))] = {0};
                struct iovec vector = {&response, sizeof(response)};
                struct msghdr message = {0};
                message.msg_iov = &vector;
                message.msg_iovlen = 1;
                message.msg_control = control;
                message.msg_controllen = sizeof(control);
                if (request_ok)
                {
                    struct cmsghdr* header = CMSG_FIRSTHDR(&message);
                    header->cmsg_level = SOL_SOCKET;
                    header->cmsg_type = SCM_RIGHTS;
                    header->cmsg_len = CMSG_LEN(sizeof(lease_fd));
                    memcpy(CMSG_DATA(header), &lease_fd, sizeof(lease_fd));
                    ssize_t sent = sendmsg(client, &message, MSG_NOSIGNAL);
                    request_ok = sent == (ssize_t)sizeof(response);
                }
            }
            if (request_ok) request_ok = bq_worker_lease_handoff_poll(client, POLLIN, deadline);
            if (request_ok)
            {
                BqWorkerLeaseMessage acknowledgement = {0};
                ssize_t received = recv(client, &acknowledgement, sizeof(acknowledgement), MSG_DONTWAIT);
                request_ok = received == (ssize_t)sizeof(acknowledgement) &&
                             bq_worker_lease_message_matches(&acknowledgement, BQ_WORKER_LEASE_ACK, lease_path,
                                                              job_id, attempt_token, (u64)lease_info.st_dev,
                                                              (u64)lease_info.st_ino, preparation_sha256,
                                                              execution_deadline_ns);
            }
            transferred = request_ok;
            if (transferred && phase_descriptor) *phase_descriptor = client;
            else close(client);
            if (!transferred) break;
        }
    }
    if (transferred) error = bq_worker_lease_handoff_close(handoff) ? BQ_OK : BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_lease_handoff_receive(String8 lease_file, String8 result_root,
                                                              String8 job_id_text, String8 attempt_token_text,
                                                              BqRecipe recipe, BqWorkerLease* lease,
                                                              int* phase_descriptor,
                                                              char preparation_sha256[SHA256_HEX_CAPACITY],
                                                              u64* execution_deadline_ns)
{
    if (phase_descriptor) *phase_descriptor = -1;
    if (preparation_sha256) preparation_sha256[0] = 0;
    if (execution_deadline_ns) *execution_deadline_ns = 0;
    char lease_path[BQ_PATH_CAP + 1], result_path[BQ_PATH_CAP + 1], socket_path[BQ_PATH_CAP + 1];
    IntegerParsingU64 job_value = string8_parse_u64_decimal(job_id_text);
    IntegerParsingU64 token_value = string8_parse_u64_decimal(attempt_token_text);
    bool valid = bq_worker_text(lease_file, lease_path, sizeof(lease_path)) && lease_path[0] == '/' &&
                 preparation_sha256 &&
                 bq_worker_text(result_root, result_path, sizeof(result_path)) && result_path[0] == '/' &&
                 bq_worker_lease_socket_path(result_path, socket_path) &&
                 job_value.status == INTEGER_PARSING_SUCCESS && job_value.length == job_id_text.length && job_value.value != 0 &&
                 token_value.status == INTEGER_PARSING_SUCCESS && token_value.length == attempt_token_text.length && token_value.value != 0;
    int result_directory = valid ? bq_worker_open_trusted_directory(string_from_pointer(result_path), true, false) : -1;
    struct stat socket_info = {0};
    if (valid)
    {
        valid = result_directory >= 0 && fstatat(result_directory, BQ_WORKER_LEASE_HANDOFF_NAME, &socket_info,
                                                 AT_SYMLINK_NOFOLLOW) == 0 && S_ISSOCK(socket_info.st_mode) &&
                socket_info.st_uid == geteuid() && (socket_info.st_mode & 077) == 0;
    }
    if (result_directory >= 0) close(result_directory);
    int client = -1;
    bool connected = false;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_WORKER_LEASE_HANDOFF_MILLISECONDS);
    while (valid && !connected && bq_worker_monotonic_milliseconds() < deadline)
    {
        client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (client >= 0)
        {
            struct sockaddr_un address = {0};
            size_t length = strlen(socket_path);
            address.sun_family = AF_UNIX;
            memcpy(address.sun_path, socket_path, length + 1);
            socklen_t address_size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1);
            connected = connect(client, (struct sockaddr*)&address, address_size) == 0;
            if (!connected)
            {
                int saved_errno = errno;
                close(client);
                client = -1;
                if (saved_errno != ENOENT && saved_errno != ECONNREFUSED && saved_errno != EINTR)
                    valid = false;
                else
                    bq_worker_sleep_until(bq_worker_deadline(bq_worker_monotonic_milliseconds(), 20));
            }
        }
        else
        {
            valid = false;
        }
    }
    BqError error = valid && connected ? BQ_IO : BQ_BAD_REQUEST;
    if (error == BQ_IO)
    {
        BqWorkerLeaseMessage request;
        bool made = bq_worker_lease_message_make(&request, BQ_WORKER_LEASE_REQUEST, lease_path,
                                                  job_value.value, token_value.value, 0, 0, "", 0);
        ssize_t sent = made ? send(client, &request, sizeof(request), MSG_NOSIGNAL) : -1;
        error = sent == (ssize_t)sizeof(request) && bq_worker_lease_handoff_poll(client, POLLIN, deadline) ? BQ_OK : BQ_IO;
    }
    int received_fd = -1;
    BqWorkerLease adopted = {.descriptor = -1};
    if (error == BQ_OK)
    {
        BqWorkerLeaseMessage response = {0};
        char control[CMSG_SPACE(sizeof(received_fd))] = {0};
        struct iovec vector = {&response, sizeof(response)};
        struct msghdr message = {0};
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        ssize_t received = recvmsg(client, &message, MSG_CMSG_CLOEXEC);
        bool response_ok = received == (ssize_t)sizeof(response) &&
                           !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) &&
                           bq_worker_preparation_matches_recipe(recipe, response.preparation_sha256) &&
                           bq_worker_lease_message_matches(&response, BQ_WORKER_LEASE_RESPONSE, lease_path,
                                                           job_value.value, token_value.value, response.device,
                                                           response.inode, response.preparation_sha256,
                                                           response.execution_deadline_ns);
        bool ancillary_ok = true;
        for (struct cmsghdr* header = received >= 0 ? CMSG_FIRSTHDR(&message) : NULL; header;
             header = CMSG_NXTHDR(&message, header))
        {
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                header->cmsg_len >= CMSG_LEN(0))
            {
                size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                if (header->cmsg_len != CMSG_LEN(count * sizeof(int)) || count != 1) ancillary_ok = false;
                for (size_t index = 0; index < count; ++index)
                {
                    int descriptor = -1;
                    memcpy(&descriptor, (u8*)CMSG_DATA(header) + index * sizeof(int), sizeof(descriptor));
                    if (descriptor >= 0 && received_fd < 0 && count == 1 && ancillary_ok)
                        received_fd = descriptor;
                    else
                    {
                        if (descriptor >= 0) close(descriptor);
                        ancillary_ok = false;
                    }
                }
            }
            else ancillary_ok = false;
        }
        response_ok = response_ok && ancillary_ok;
        bool adopted_ok = false;
        if (response_ok && received_fd >= 3)
        {
            adopted_ok = bq_worker_lease_adopt_transferred(lease_path, received_fd, response.device,
                                                            response.inode, &adopted) == 0;
            received_fd = -1;
        }
        response_ok = adopted_ok;
        if (response_ok)
        {
            BqWorkerLeaseMessage acknowledgement;
            response_ok = bq_worker_lease_message_make(&acknowledgement, BQ_WORKER_LEASE_ACK, lease_path,
                                                        job_value.value, token_value.value, response.device,
                                                        response.inode, response.preparation_sha256,
                                                        response.execution_deadline_ns) &&
                          send(client, &acknowledgement, sizeof(acknowledgement), MSG_NOSIGNAL) ==
                              (ssize_t)sizeof(acknowledgement);
        }
        if (response_ok)
        {
            *lease = adopted;
            adopted.descriptor = -1;
            memcpy(preparation_sha256, response.preparation_sha256, SHA256_HEX_CAPACITY);
            if (execution_deadline_ns) *execution_deadline_ns = response.execution_deadline_ns;
            error = BQ_OK;
        }
        else
        {
            error = BQ_CONFIGURATION_MISMATCH;
        }
    }
    if (adopted.descriptor >= 0) bq_worker_lease_release(&adopted);
    else if (received_fd >= 0) close(received_fd);
    if (error == BQ_OK && phase_descriptor) *phase_descriptor = client;
    else if (client >= 0) close(client);
    return error;
}

BUSTER_GLOBAL_LOCAL int bq_worker_join_text(char* output, u32 capacity, char const* const parts[], u32 count)
{
    u64 length = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        u64 part_length = parts[index] ? strlen(parts[index]) : 0;
        length = part_length <= UINT64_MAX - length ? length + part_length : UINT64_MAX;
    }
    bool ok = length < capacity && length <= INT_MAX;
    u32 offset = 0;
    if (ok)
    {
        for (u32 index = 0; index < count; index += 1)
        {
            char const* part = parts[index] ? parts[index] : "";
            u32 part_length = (u32)strlen(part);
            memcpy(output + offset, part, part_length);
            offset += part_length;
        }
        output[offset] = 0;
    }
    return ok ? (int)length : -1;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_read_regular(char const* path, char* output, u32 capacity)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    bool ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1;
    u32 used = 0;
    while (ok && used + 1 < capacity)
    {
        ssize_t count = read(fd, output + used, capacity - used - 1);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count < 0)
        {
            ok = false;
        }
        else if (!count)
        {
            break;
        }
        else
        {
            used += (u32)count;
        }
    }
    if (ok)
    {
        char extra;
        ssize_t count = read(fd, &extra, 1);
        ok = count == 0;
    }
    if (fd >= 0 && close(fd) != 0)
    {
        ok = false;
    }
    while (ok && used && (output[used - 1] == '\n' || output[used - 1] == '\r'))
    {
        used -= 1;
    }
    if (ok)
    {
        output[used] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_result_path(String8 workspace_root, u64 id, u64 token,
                                                char output[BQ_PATH_CAP + 1])
{
    char workspace[BQ_PATH_CAP + 1];
    bool ok = bq_worker_text(workspace_root, workspace, sizeof(workspace)) &&
              bq_string_path(workspace_root, workspace);
    char id_text[32], token_text[32];
    int id_length = snprintf(id_text, sizeof(id_text), "%" PRIu64, (uint64_t)id);
    int token_length = snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)token);
    char const* parts[] = {workspace, "/results/job-", id_text, "-attempt-", token_text};
    int length = ok && id_length > 0 && token_length > 0 ?
                 bq_worker_join_text(output, BQ_PATH_CAP + 1, parts, BUSTER_ARRAY_LENGTH(parts)) : -1;
    return length > 0;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_boot_valid(char const* boot)
{
    bool ok = strlen(boot) == 36;
    for (u32 i = 0; ok && i < 36; i += 1)
    {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        ok = dash ? boot[i] == '-' : (boot[i] >= '0' && boot[i] <= '9') || (boot[i] >= 'a' && boot[i] <= 'f');
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_invocation_valid(char const* invocation)
{
    bool ok = strlen(invocation) == 32;
    for (u32 i = 0; ok && i < 32; i += 1)
        ok = (invocation[i] >= '0' && invocation[i] <= '9') ||
             (invocation[i] >= 'a' && invocation[i] <= 'f');
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_unit_name(char output[BQ_WORKER_UNIT_CAP], u64 id, u64 token)
{
    int count = snprintf(output, BQ_WORKER_UNIT_CAP, "buster-bench-%" PRIu64 "-%" PRIu64 ".service",
                         (uint64_t)id, (uint64_t)token);
    return count > 0 && count < (int)BQ_WORKER_UNIT_CAP;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_record_name(char output[48], u64 id)
{
    return bq_record_name(output, "worker", id);
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_record_write(BqQueue* queue, BqJob const* job,
                                                    char const* boot, char const* unit)
{
    u8 bytes[232] = {0};
    char name[48];
    bool ok = job && bq_worker_boot_valid(boot) && strlen(unit) < BQ_WORKER_UNIT_CAP &&
              bq_worker_record_name(name, job->id);
    if (ok)
    {
        memcpy(bytes, "BQWORKER00000001", 16);
        bq_put64(bytes + 16, job->id);
        bq_put64(bytes + 24, job->token);
        memcpy(bytes + 32, job->digest, 64);
        memcpy(bytes + 96, boot, strlen(boot));
        memcpy(bytes + 136, unit, strlen(unit));
    }
    mode_t mode = queue && !strcmp(queue->directory_path, BQ_BROKER_QUEUE) ? 0440 : 0400;
    BqError error = ok ? bq_record_write_mode(queue, name, bytes, sizeof(bytes), false, mode) : BQ_WORKER_MISMATCH;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_record_read(BqQueue* queue, BqJob const* job,
                                                   char boot[BQ_WORKER_BOOT_CAP],
                                                   char unit[BQ_WORKER_UNIT_CAP])
{
    u8 bytes[232];
    u32 size = 0;
    char name[48];
    BqError error = bq_worker_record_name(name, job->id) ?
                    bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_CORRUPT;
    bool terminated = size == sizeof(bytes) && !bytes[132] && !bytes[231];
    if (error == BQ_OK)
    {
        memcpy(boot, bytes + 96, 37);
        memcpy(unit, bytes + 136, 96);
        bool ok = terminated && !memcmp(bytes, "BQWORKER00000001", 16) &&
                  bq_u64(bytes + 16) == job->id && bq_u64(bytes + 24) == job->token &&
                  !memcmp(bytes + 32, job->digest, 64) && bq_worker_boot_valid(boot) &&
                  strlen(unit) < BQ_WORKER_UNIT_CAP;
        char expected[BQ_WORKER_UNIT_CAP];
        ok = ok && bq_worker_unit_name(expected, job->id, job->token) && !strcmp(expected, unit);
        error = ok ? BQ_OK : BQ_WORKER_MISMATCH;
    }
    else if (error == BQ_NOT_FOUND)
    {
        error = BQ_WORKER_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_instance_name(char output[48], u64 id)
{
    return bq_record_name(output, "worker-instance", id);
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_instance_write(BqQueue* queue, BqJob const* job,
                                                      BqWorkerObserved const* observed)
{
    u8 bytes[544] = {0};
    char name[48];
    bool ok = job && observed && bq_worker_instance_name(name, job->id) &&
              bq_worker_boot_valid(observed->boot_id) &&
              bq_worker_invocation_valid(observed->invocation_id) &&
              strlen(observed->unit) < BQ_WORKER_UNIT_CAP &&
              strlen(observed->cgroup) < BQ_WORKER_CGROUP_CAP &&
              observed->cgroup_device && observed->cgroup_inode &&
              observed->cgroup_root_device && observed->cgroup_root_inode &&
              observed->cgroup_slice_device && observed->cgroup_slice_inode;
    if (ok)
    {
        memcpy(bytes, "BQINSTANCE000002", 16);
        bq_put64(bytes + 16, job->id);
        bq_put64(bytes + 24, job->token);
        memcpy(bytes + 32, job->digest, 64);
        memcpy(bytes + 96, observed->boot_id, strlen(observed->boot_id));
        memcpy(bytes + 136, observed->unit, strlen(observed->unit));
        memcpy(bytes + 232, observed->invocation_id, strlen(observed->invocation_id));
        bq_put64(bytes + 272, observed->cgroup_device);
        bq_put64(bytes + 280, observed->cgroup_inode);
        memcpy(bytes + 288, observed->cgroup, strlen(observed->cgroup));
        bq_put64(bytes + 480, observed->cgroup_root_device);
        bq_put64(bytes + 488, observed->cgroup_root_inode);
        bq_put64(bytes + 496, observed->cgroup_slice_device);
        bq_put64(bytes + 504, observed->cgroup_slice_inode);
    }
    mode_t mode = queue && !strcmp(queue->directory_path, BQ_BROKER_QUEUE) ? 0440 : 0400;
    BqError error = ok ? bq_record_write_mode(queue, name, bytes, sizeof(bytes), true, mode) : BQ_WORKER_MISMATCH;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_instance_read(BqQueue* queue, BqJob const* job,
                                                     BqWorkerObserved* identity)
{
    u8 bytes[544];
    u32 size = 0;
    char name[48];
    BqError error = bq_worker_instance_name(name, job->id) ?
                    bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_CORRUPT;
    if (error == BQ_OK)
    {
        bool terminated = size == sizeof(bytes) && !bytes[132] && !bytes[231] &&
                          !bytes[264] && !bytes[479] && !bytes[543];
        *identity = (BqWorkerObserved){0};
        memcpy(identity->boot_id, bytes + 96, 37);
        memcpy(identity->unit, bytes + 136, 96);
        memcpy(identity->invocation_id, bytes + 232, 33);
        identity->cgroup_device = bq_u64(bytes + 272);
        identity->cgroup_inode = bq_u64(bytes + 280);
        memcpy(identity->cgroup, bytes + 288, BQ_WORKER_CGROUP_CAP);
        identity->cgroup_root_device = bq_u64(bytes + 480);
        identity->cgroup_root_inode = bq_u64(bytes + 488);
        identity->cgroup_slice_device = bq_u64(bytes + 496);
        identity->cgroup_slice_inode = bq_u64(bytes + 504);
        bool ok = terminated && !memcmp(bytes, "BQINSTANCE000002", 16) &&
                  bq_u64(bytes + 16) == job->id && bq_u64(bytes + 24) == job->token &&
                  !memcmp(bytes + 32, job->digest, 64) &&
                  bq_worker_boot_valid(identity->boot_id) &&
                  bq_worker_invocation_valid(identity->invocation_id) &&
                  identity->cgroup_device && identity->cgroup_inode &&
                  identity->cgroup_root_device && identity->cgroup_root_inode &&
                  identity->cgroup_slice_device && identity->cgroup_slice_inode &&
                  strlen(identity->unit) < BQ_WORKER_UNIT_CAP &&
                  strlen(identity->cgroup) < BQ_WORKER_CGROUP_CAP;
        error = ok ? BQ_OK : BQ_WORKER_MISMATCH;
    }
    else if (error == BQ_NOT_FOUND)
    {
        error = BQ_WORKER_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_number(char const* text, u64* value)
{
    String8 input = string_from_pointer(text);
    IntegerParsingU64 parsed = string8_parse_u64_decimal(input);
    bool ok = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == input.length;
    *value = ok ? parsed.value : 0;
    return ok;
}

/* Plain microseconds or the manager's whole-second timespan text ("1h",
 * "10s", "2h 24min"), parsed exactly as the broker does (systemd_runtime.h). */
BUSTER_GLOBAL_LOCAL bool bq_worker_duration(char const* text, u64* value)
{
    uint64_t parsed = 0;
    bool ok = text && bq_systemd_timespan_parse(text, strlen(text), &parsed);
    *value = ok ? (u64)parsed : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_cpu_list(char const* text, u32 cpu, bool singleton)
{
    bool ok = true;
    bool found = false;
    u32 count = 0;
    char const* at = text;
    while (ok && *at)
    {
        char* end = NULL;
        errno = 0;
        unsigned long first = strtoul(at, &end, 10);
        ok = !errno && end != at && first <= UINT32_MAX;
        unsigned long last = first;
        if (ok && *end == '-')
        {
            at = end + 1;
            errno = 0;
            last = strtoul(at, &end, 10);
            ok = !errno && end != at && last >= first && last <= UINT32_MAX;
        }
        if (ok && (last - first + 1) <= UINT32_MAX - count)
        {
            found = found || (cpu >= first && cpu <= last);
            count += (u32)(last - first + 1);
            if (*end == ',') at = end + 1;
            else if (!*end) at = end;
            else ok = false;
        }
        else
        {
            ok = false;
        }
    }
    bool result = ok && found && (!singleton || count == 1);
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_read_at(int directory, char const* name, char* output, u32 capacity)
{
    int fd = openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    bool ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
    u32 used = 0;
    while (ok && used + 1 < capacity)
    {
        ssize_t count = read(fd, output + used, capacity - used - 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) ok = false;
        else if (!count) break;
        else used += (u32)count;
    }
    if (ok)
    {
        char extra;
        ok = read(fd, &extra, 1) == 0;
    }
    if (fd >= 0) close(fd);
    while (ok && used && (output[used - 1] == '\n' || output[used - 1] == '\r')) used -= 1;
    if (ok) output[used] = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_limit_ok(char const* text, u64 wanted, bool exact)
{
    u64 value = 0;
    bool unlimited = !strcmp(text, "max") || !strcmp(text, "infinity");
    return exact ? (!unlimited && bq_worker_number(text, &value) && value == wanted) :
           unlimited || (bq_worker_number(text, &value) && value >= wanted);
}

/* Validate manager text before it is used as a path.  Each component is then
 * opened relative to an already verified descriptor, without following links. */
BUSTER_GLOBAL_LOCAL bool bq_worker_cgroup_path_valid(char const* path)
{
    bool ok = path && path[0] == '/' && path[1] && strlen(path) < BQ_WORKER_CGROUP_CAP;
    char const* part = ok ? path + 1 : NULL;
    while (ok && *part)
    {
        char const* end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        ok = length && !(length == 1 && part[0] == '.') &&
             !(length == 2 && part[0] == '.' && part[1] == '.');
        for (size_t i = 0; ok && i < length; i += 1)
            ok = (part[i] >= 'a' && part[i] <= 'z') ||
                 (part[i] >= 'A' && part[i] <= 'Z') ||
                 (part[i] >= '0' && part[i] <= '9') ||
                 part[i] == '-' || part[i] == '_' || part[i] == '.';
        part = end ? end + 1 : part + length;
        if (end && !*part) ok = false;
    }
    return ok;
}

/* Verify the leaf and every ancestor below the service-owned cgroup root.
 * Tighter ancestor constraints are rejected: they would silently change the
 * installed recipe's effective budget.  Symlinked components/files fail shut.
 */
BUSTER_GLOBAL_LOCAL bool bq_worker_verify_cgroup(BqWorkerConfig const* config, BqWorkerObserved* observed,
                                                  bool resources)
{
    char root[BQ_PATH_CAP + 1];
    bool ok = bq_worker_text(config->cgroup_root, root, sizeof(root)) &&
              bq_worker_cgroup_path_valid(observed->cgroup) &&
              strncmp(observed->cgroup, BQ_WORKER_CGROUP_PREFIX,
                      sizeof(BQ_WORKER_CGROUP_PREFIX) - 1) == 0;
    int directory = ok ? bq_worker_open_trusted_directory(config->cgroup_root, false, false) : -1;
    ok = directory >= 0;
    struct stat root_identity;
    if (ok && fstat(directory, &root_identity) != 0) ok = false;
    if (ok && config->production_path && !config->backend)
    {
        struct statfs filesystem;
        ok = fstatfs(directory, &filesystem) == 0 && (u64)filesystem.f_type == 0x63677270ull;
    }
    char path[BQ_WORKER_CGROUP_CAP];
    if (ok) snprintf(path, sizeof(path), "%s", observed->cgroup + 1);
    char* part = path;
    bool leaf = false;
    while (ok && *part)
    {
        char* slash = strchr(part, '/');
        if (slash) *slash = 0;
        leaf = slash == NULL;
        int child = *part && strcmp(part, ".") && strcmp(part, "..") ?
                    openat(directory, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        if (child < 0)
        {
            ok = false;
        }
        else
        {
            close(directory);
            directory = child;
            struct stat identity;
            ok = bq_worker_directory_owner(directory, false, false) && fstat(directory, &identity) == 0;
            if (ok && config->production_path && !config->backend)
            {
                struct statfs filesystem;
                ok = fstatfs(directory, &filesystem) == 0 &&
                     (u64)filesystem.f_type == 0x63677270ull;
            }
            if (ok && !strcmp(part, "buster-bench.slice"))
            {
                observed->cgroup_root_device = (u64)root_identity.st_dev;
                observed->cgroup_root_inode = (u64)root_identity.st_ino;
                observed->cgroup_slice_device = (u64)identity.st_dev;
                observed->cgroup_slice_inode = (u64)identity.st_ino;
            }
            if (ok && resources)
            {
                char cpus[128], memory[64], swap[64], tasks[64];
                ok = bq_worker_read_at(directory, "cpuset.cpus.effective", cpus, sizeof(cpus)) &&
                     bq_worker_cpu_list(cpus, config->limits.cpu, leaf) &&
                     bq_worker_read_at(directory, "memory.max", memory, sizeof(memory)) &&
                     bq_worker_limit_ok(memory, config->limits.memory_max, leaf) &&
                     bq_worker_read_at(directory, "memory.swap.max", swap, sizeof(swap)) &&
                     bq_worker_limit_ok(swap, config->limits.memory_swap_max, leaf) &&
                     bq_worker_read_at(directory, "pids.max", tasks, sizeof(tasks)) &&
                     bq_worker_limit_ok(tasks, config->limits.tasks_max, leaf);
            }
            if (ok && leaf)
            {
                char events[128];
                ok = bq_worker_read_at(directory, "cgroup.events", events, sizeof(events)) &&
                     (strstr(events, "populated 1") != NULL || strstr(events, "populated 0") != NULL);
                if (ok)
                {
                    observed->populated = strstr(events, "populated 1") != NULL;
                    observed->cgroup_device = (u64)identity.st_dev;
                    observed->cgroup_inode = (u64)identity.st_ino;
                }
            }
        }
        part = slash ? slash + 1 : part + strlen(part);
    }
    if (directory >= 0) close(directory);
    return ok && leaf;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_observed(BqWorkerConfig const* config, char const* boot,
                                             char const* unit, BqWorkerObserved* observed,
                                             bool resources)
{
    char cpu[32], expected_cgroup[BQ_WORKER_CGROUP_CAP];
    char expected_inaccessible[BQ_PATH_CAP * 2 + 2];
    char expected_read_only[BQ_PATH_CAP + 1], expected_read_write[BQ_PATH_CAP + 1];
    snprintf(cpu, sizeof(cpu), "%u", config->limits.cpu);
    snprintf(expected_cgroup, sizeof(expected_cgroup), BQ_WORKER_CGROUP_PREFIX "%s", unit);
    String8 inaccessible_root = config->production_path ? config->queue_root : config->workspace_root;
    char inaccessible_root_text[BQ_PATH_CAP + 1], lease_text[BQ_PATH_CAP + 1];
    bool paths = bq_worker_text(inaccessible_root, inaccessible_root_text, sizeof(inaccessible_root_text)) &&
                 bq_worker_text(config->lease_file, lease_text, sizeof(lease_text));
    char const* inaccessible_parts[] = {inaccessible_root_text, " ", lease_text};
    int inaccessible_length = paths ? bq_worker_join_text(expected_inaccessible, sizeof(expected_inaccessible),
                                                            inaccessible_parts, BUSTER_ARRAY_LENGTH(inaccessible_parts)) : -1;
    paths = paths && bq_worker_text(config->installed_root, expected_read_only, sizeof(expected_read_only)) &&
            bq_worker_text(config->workspace_root, expected_read_write, sizeof(expected_read_write));
    bool ok = observed->unit_found && !strcmp(observed->boot_id, boot) && !strcmp(observed->unit, unit) &&
              !strcmp(observed->cgroup, expected_cgroup) &&
              bq_worker_invocation_valid(observed->invocation_id) &&
              bq_worker_verify_cgroup(config, observed, resources) && paths && observed->security_properties_valid &&
              observed->paths_valid && inaccessible_length > 0 && !strcmp(observed->inaccessible_paths, expected_inaccessible) &&
              !strcmp(observed->read_only_paths, expected_read_only) && !strcmp(observed->read_write_paths, expected_read_write) &&
              !strcmp(observed->user, "buster-bench") && !strcmp(observed->group, "buster-bench") &&
              !strcmp(observed->collect_mode, "inactive") &&
              observed->no_new_privileges && observed->private_tmp && observed->private_devices &&
              observed->private_network && observed->protect_home && observed->protect_system &&
              observed->protect_proc && observed->restrict_suidsgid && observed->protect_control_groups &&
              observed->protect_kernel_tunables && observed->protect_kernel_modules && observed->protect_kernel_logs &&
              observed->protect_clock && observed->protect_hostname && observed->lock_personality &&
              observed->memory_deny_write_execute && observed->remove_ipc && observed->keyring_private &&
              observed->restrict_namespaces && observed->restrict_realtime && observed->address_families_unix &&
              observed->syscall_architectures_native && observed->syscall_filter_system_service &&
              observed->syscall_error_number_eperm;
    if (ok && resources)
    {
        /* A fresh launch must satisfy the current sandbox. Identity checks
         * without resources still authorize cleanup of older exact units. */
        ok = observed->capability_sets_empty &&
             !strcmp(observed->allowed_cpus, cpu) && observed->memory_max == config->limits.memory_max &&
             observed->memory_swap_max == config->limits.memory_swap_max &&
             observed->tasks_max == config->limits.tasks_max &&
             observed->runtime_max_usec == config->limits.runtime_max_usec &&
             observed->timeout_stop_usec == 10ull * 1000000 && observed->send_sigkill &&
             observed->no_new_privileges && observed->private_tmp && observed->private_devices &&
             observed->private_network && observed->protect_home && observed->protect_system &&
             observed->protect_proc && observed->restrict_suidsgid &&
             !strcmp(observed->kill_mode, "control-group") && observed->active && observed->populated &&
             observed->result == BQ_WORKER_RUNNING;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_waitpid_until(pid_t pid, int* status, u64 deadline)
{
    BqError error = BQ_OK;
    bool finished = false;
    while (error == BQ_OK && !finished)
    {
        pid_t waited = waitpid(pid, status, WNOHANG);
        if (waited == pid) finished = true;
        else if (waited < 0 && errno != EINTR) error = BQ_IO;
        else
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_IO;
            else
            {
                u64 next = bq_worker_deadline(now, 10);
                if (next > deadline) next = deadline;
                error = bq_worker_sleep_until(next);
            }
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_process_group_absent(pid_t group, u64 deadline)
{
    BqError error = BQ_OK;
    bool absent = false;
    while (error == BQ_OK && !absent)
    {
        errno = 0;
        absent = kill(-group, 0) != 0 && errno == ESRCH;
        if (!absent)
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_CLEANUP_FAILED;
            else
            {
                u64 next = bq_worker_deadline(now, 10);
                if (next > deadline) next = deadline;
                error = bq_worker_sleep_until(next);
            }
        }
    }
    return error;
}

#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL bool bq_worker_test_setpgid_failure;
BUSTER_GLOBAL_LOCAL bool bq_worker_test_stop_before_exec;
BUSTER_GLOBAL_LOCAL pid_t bq_worker_test_exec_pid;
#endif

BUSTER_GLOBAL_LOCAL BqError bq_worker_exec_capture(char const* const* arguments, char* output, u32 capacity,
                                                    int* status, u32 timeout_milliseconds)
{
    if (capacity) output[0] = 0;
    int pipefd[2] = {-1, -1};
    int setup[2] = {-1, -1};
    BqError error = capacity && pipe(pipefd) == 0 && pipe(setup) == 0 ? BQ_OK : BQ_IO;
    pid_t pid = error == BQ_OK ? fork() : -1;
#ifdef BUSTER_BENCH_SERVICE_TEST
    bq_worker_test_exec_pid = pid;
#endif
    if (pid < 0) error = BQ_IO;
    if (!pid)
    {
        close(setup[0]);
        bool grouped = true;
#ifdef BUSTER_BENCH_SERVICE_TEST
        if (bq_worker_test_setpgid_failure) { errno = EPERM; grouped = false; }
        else
#endif
        if (setpgid(0, 0) != 0) grouped = false;
        u8 state = grouped ? 1 : 0;
        ssize_t sent;
        do { sent = write(setup[1], &state, 1); } while (sent < 0 && errno == EINTR);
        close(setup[1]);
        if (!grouped || sent != 1) _exit(125);
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) _exit(126);
        close(pipefd[1]);
#ifdef BUSTER_BENCH_SERVICE_TEST
        if (bq_worker_test_stop_before_exec) raise(SIGSTOP);
#endif
        execv(arguments[0], (char* const*)arguments);
        _exit(127);
    }
    if (error == BQ_OK)
    {
        close(setup[1]);
        setup[1] = -1;
        close(pipefd[1]);
        pipefd[1] = -1;
        int flags = fcntl(pipefd[0], F_GETFL), setup_flags = fcntl(setup[0], F_GETFL);
        if (flags < 0 || setup_flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) != 0 ||
            fcntl(setup[0], F_SETFL, setup_flags | O_NONBLOCK) != 0) error = BQ_IO;
        u32 used = 0;
        bool eof = false;
        bool overflow = false;
        u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), timeout_milliseconds);
        bool group_ready = false;
        while (error == BQ_OK && !group_ready)
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_IO;
            struct pollfd waiting = {setup[0], POLLIN | POLLHUP, 0};
            int milliseconds = error == BQ_OK && deadline - now < INT_MAX ? (int)(deadline - now) :
                               error == BQ_OK ? INT_MAX : 0;
            int ready = error == BQ_OK ? poll(&waiting, 1, milliseconds) : -1;
            if (ready < 0 && errno != EINTR) error = BQ_IO;
            if (ready == 0) error = BQ_IO;
            if (ready > 0)
            {
                u8 state = 0;
                ssize_t count = read(setup[0], &state, 1);
                if (count == 1 && state == 1) group_ready = true;
                else if (count == 1 || count == 0) error = BQ_CLEANUP_FAILED;
                else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) error = BQ_IO;
            }
        }
        close(setup[0]);
        setup[0] = -1;
        while (error == BQ_OK && !eof)
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_IO;
            struct pollfd waiting = {pipefd[0], POLLIN | POLLHUP, 0};
            int milliseconds = error == BQ_OK && deadline - now < INT_MAX ? (int)(deadline - now) :
                               error == BQ_OK ? INT_MAX : 0;
            int ready = error == BQ_OK ? poll(&waiting, 1, milliseconds) : -1;
            if (ready < 0 && errno != EINTR) error = BQ_IO;
            if (ready == 0) error = BQ_IO;
            if (ready > 0)
            {
                u8 discard[512];
                bool more = true;
                while (error == BQ_OK && more)
                {
                    char* destination = used + 1 < capacity ? output + used : (char*)discard;
                    u32 available = used + 1 < capacity ? capacity - used - 1 : (u32)sizeof(discard);
                    ssize_t count = read(pipefd[0], destination, available);
                    if (count > 0)
                    {
                        if (used + 1 < capacity) used += (u32)count;
                        else overflow = true;
                    }
                    else if (!count) { eof = true; more = false; }
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) more = false;
                    else if (errno != EINTR) error = BQ_IO;
                }
            }
        }
        output[used] = 0;
        close(pipefd[0]);
        pipefd[0] = -1;
        bool needs_kill = error != BQ_OK;
        bool child_reaped = false;
        if (error == BQ_OK)
        {
            error = bq_worker_waitpid_until(pid, status, deadline);
            needs_kill = error != BQ_OK;
            child_reaped = error == BQ_OK;
        }
        if (overflow) { error = BQ_IO; needs_kill = true; }
        if (needs_kill)
        {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            int ignored = 0;
            u64 reap = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 1000);
            BqError child = child_reaped ? BQ_OK : bq_worker_waitpid_until(pid, &ignored, reap);
            BqError group = bq_worker_process_group_absent(pid, reap);
            if (child != BQ_OK || group != BQ_OK) error = BQ_CLEANUP_FAILED;
        }
    }
    else
    {
        if (pipefd[0] >= 0) close(pipefd[0]);
        if (pipefd[1] >= 0) close(pipefd[1]);
        if (setup[0] >= 0) close(setup[0]);
        if (setup[1] >= 0) close(setup[1]);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_start(BqWorkerBackend* backend, char const* const* argv, u32 count)
{
    (void)count;
    BqSystemdContext* context = backend->context;
    context->pid = fork();
    if (!context->pid)
    {
        execv(argv[0], (char* const*)argv);
        _exit(127);
    }
    context->starting = context->pid > 0;
    return context->starting ? BQ_OK : BQ_IO;
}

BUSTER_GLOBAL_LOCAL char* bq_worker_property(char* text, char const* name)
{
    size_t length = strlen(name);
    char* at = text;
    char* result = NULL;
    while (at && *at && !result)
    {
        char* end = strchr(at, '\n');
        size_t available = end ? (size_t)(end - at) : strlen(at);
        if (available > length && !memcmp(at, name, length) && at[length] == '=') result = at + length + 1;
        else at = end ? end + 1 : NULL;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_systemd_capabilities_empty(char* output)
{
    char* bounding = bq_worker_property(output, "CapabilityBoundingSet");
    char* ambient = bq_worker_property(output, "AmbientCapabilities");
    /* systemctl show renders an empty set as an empty property value. */
    bool ok = bounding && ambient && (bounding[0] == '\n' || bounding[0] == 0) &&
              (ambient[0] == '\n' || ambient[0] == 0);
    return ok;
}

/* systemd exposes the expanded syscall set, not the @system-service token.
 * Compare it with the installed trusted service, which carries the same fixed
 * filter on the same manager and architecture. Cache only a successful read. */
BUSTER_GLOBAL_LOCAL bool bq_worker_systemd_filter_matches(char const* actual, u64 deadline)
{
    static char expected[BQ_WORKER_OUTPUT_CAP];
    static bool ready;
    bool ok = actual && actual[0];
    if (ok && !ready)
    {
        char output[BQ_WORKER_OUTPUT_CAP];
        char const* arguments[] = {BQ_SYSTEMCTL, "show", "--no-pager", "--property=SystemCallFilter",
                                   "buster-bench.service", NULL};
        int status = 0;
        ok = bq_worker_exec_capture(arguments, output, sizeof(output), &status,
                                    bq_worker_remaining(deadline)) == BQ_OK &&
             WIFEXITED(status) && WEXITSTATUS(status) == 0;
        char* value = ok ? bq_worker_property(output, "SystemCallFilter") : NULL;
        char* end = value ? strchr(value, '\n') : NULL;
        ok = ok && value && end && end > value && (size_t)(end - value) < sizeof(expected);
        if (ok)
        {
            memcpy(expected, value, (size_t)(end - value));
            expected[end - value] = 0;
            ready = true;
        }
    }
    return ok && ready && !strcmp(actual, expected);
}

BUSTER_GLOBAL_LOCAL BqWorkerResult bq_worker_systemd_result(char const* value, bool active)
{
    BqWorkerResult result = !strcmp(value, "oom-kill") ? BQ_WORKER_OOM :
                            !strcmp(value, "timeout") ? BQ_WORKER_TIMED_OUT :
                            !strcmp(value, "canceled") ? BQ_WORKER_CANCELLED_RESULT :
                            !strcmp(value, "success") && active ? BQ_WORKER_RUNNING :
                            !strcmp(value, "success") ? BQ_WORKER_SUCCEEDED :
                            BQ_WORKER_EXECUTION_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_systemd_collected(char* output)
{
    char* load_state = bq_worker_property(output, "LoadState");
    char* end = load_state ? strchr(load_state, '\n') : NULL;
    bool collected = end && (size_t)(end - load_state) == sizeof("not-found") - 1 &&
                     !memcmp(load_state, "not-found", sizeof("not-found") - 1);
    return collected;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_observe_once(BqWorkerBackend* backend, char const* unit,
                                                     BqWorkerObserved* observed, u64 deadline)
{
    (void)backend;
    char output[BQ_WORKER_OUTPUT_CAP];
    char const* arguments[] = {BQ_SYSTEMCTL, "show", "--no-pager", "--property=Id", "--property=LoadState",
        "--property=ActiveState", "--property=SubState", "--property=ControlGroup", "--property=AllowedCPUs",
        "--property=MemoryMax", "--property=MemorySwapMax", "--property=TasksMax", "--property=RuntimeMaxUSec",
        "--property=TimeoutStopUSec", "--property=KillMode", "--property=SendSIGKILL", "--property=InvocationID",
        "--property=Result", "--property=NoNewPrivileges", "--property=PrivateTmp", "--property=PrivateDevices",
        "--property=CapabilityBoundingSet", "--property=AmbientCapabilities",
        "--property=PrivateNetwork", "--property=ProtectHome", "--property=ProtectSystem", "--property=ProtectProc",
        "--property=RestrictSUIDSGID", "--property=ProtectControlGroups", "--property=ProtectKernelTunables",
        "--property=ProtectKernelModules", "--property=ProtectKernelLogs", "--property=ProtectClock",
        "--property=ProtectHostname", "--property=LockPersonality", "--property=MemoryDenyWriteExecute",
        "--property=RemoveIPC", "--property=KeyringMode", "--property=RestrictNamespaces", "--property=RestrictRealtime",
        "--property=RestrictAddressFamilies", "--property=SystemCallArchitectures", "--property=SystemCallFilter",
        "--property=SystemCallErrorNumber", "--property=InaccessiblePaths", "--property=ReadOnlyPaths",
        "--property=ReadWritePaths", "--property=User", "--property=Group", "--property=PartOf",
        "--property=BindsTo", "--property=After", "--property=CollectMode", unit, NULL};
    int status = 0;
    u32 remaining = bq_worker_remaining(deadline);
    BqError error = remaining ? bq_worker_exec_capture(arguments, output, sizeof(output), &status, remaining) : BQ_IO;
    *observed = (BqWorkerObserved){0};
    bool missing = error == BQ_OK && (!WIFEXITED(status) || WEXITSTATUS(status) != 0);
    if (error == BQ_OK && !missing && bq_worker_systemd_collected(output)) missing = true;
    if (error == BQ_OK && !missing) observed->capability_sets_empty = bq_worker_systemd_capabilities_empty(output);
    enum { BQ_SYSTEMD_FIELD_COUNT = 48 };
    char* fields[BQ_SYSTEMD_FIELD_COUNT] = {0};
    char const* names[] = {"Id", "LoadState", "ActiveState", "SubState", "ControlGroup", "AllowedCPUs",
                           "MemoryMax", "MemorySwapMax", "TasksMax", "RuntimeMaxUSec", "TimeoutStopUSec",
                           "KillMode", "SendSIGKILL", "InvocationID", "Result", "NoNewPrivileges", "PrivateTmp",
                           "PrivateDevices", "PrivateNetwork", "ProtectHome", "ProtectSystem", "ProtectProc",
                           "RestrictSUIDSGID", "ProtectControlGroups", "ProtectKernelTunables", "ProtectKernelModules",
                           "ProtectKernelLogs", "ProtectClock", "ProtectHostname", "LockPersonality",
                           "MemoryDenyWriteExecute", "RemoveIPC", "KeyringMode", "RestrictNamespaces", "RestrictRealtime",
                           "RestrictAddressFamilies", "SystemCallArchitectures", "SystemCallFilter", "SystemCallErrorNumber",
                           "InaccessiblePaths", "ReadOnlyPaths", "ReadWritePaths", "User", "Group",
                           "PartOf", "BindsTo", "After", "CollectMode"};
    _Static_assert(BUSTER_ARRAY_LENGTH(names) == BQ_SYSTEMD_FIELD_COUNT, "systemd property table mismatch");
    for (u32 i = 0; error == BQ_OK && !missing && i < BUSTER_ARRAY_LENGTH(fields); i += 1)
        fields[i] = bq_worker_property(output, names[i]);
    for (u32 i = 0; error == BQ_OK && !missing && i < BUSTER_ARRAY_LENGTH(fields); i += 1)
    {
        if (!fields[i]) error = BQ_WORKER_MISMATCH;
        else
        {
            char* end = strchr(fields[i], '\n');
            if (end) *end = 0;
        }
    }
    if (error == BQ_OK && !missing)
    {
        observed->unit_found = !strcmp(fields[1], "loaded");
        observed->active = !strcmp(fields[2], "active") || !strcmp(fields[2], "activating");
        int unit_length = bq_worker_copy_field(observed->unit, sizeof(observed->unit), fields[0]);
        int cgroup_length = bq_worker_copy_field(observed->cgroup, sizeof(observed->cgroup), fields[4]);
        int cpu_length = bq_worker_copy_field(observed->allowed_cpus, sizeof(observed->allowed_cpus), fields[5]);
        if (!bq_worker_number(fields[6], &observed->memory_max) || !bq_worker_number(fields[7], &observed->memory_swap_max) ||
            !bq_worker_number(fields[8], &observed->tasks_max) || !bq_worker_duration(fields[9], &observed->runtime_max_usec) ||
            !bq_worker_duration(fields[10], &observed->timeout_stop_usec) || unit_length < 0 ||
            (u32)unit_length >= sizeof(observed->unit) || cgroup_length < 0 || (u32)cgroup_length >= sizeof(observed->cgroup) ||
            cpu_length < 0 || (u32)cpu_length >= sizeof(observed->allowed_cpus))
        {
            error = BQ_WORKER_MISMATCH;
        }
        int kill_mode_length = bq_worker_copy_field(observed->kill_mode, sizeof(observed->kill_mode), fields[11]);
        observed->send_sigkill = !strcmp(fields[12], "yes");
        int invocation_length = bq_worker_copy_field(observed->invocation_id, sizeof(observed->invocation_id), fields[13]);
        observed->no_new_privileges = !strcmp(fields[15], "yes");
        observed->private_tmp = !strcmp(fields[16], "yes");
        observed->private_devices = !strcmp(fields[17], "yes");
        observed->private_network = !strcmp(fields[18], "yes");
        observed->protect_home = !strcmp(fields[19], "yes");
        observed->protect_system = !strcmp(fields[20], "strict");
        observed->protect_proc = !strcmp(fields[21], "invisible");
        observed->restrict_suidsgid = !strcmp(fields[22], "yes");
        observed->protect_control_groups = !strcmp(fields[23], "yes");
        observed->protect_kernel_tunables = !strcmp(fields[24], "yes");
        observed->protect_kernel_modules = !strcmp(fields[25], "yes");
        observed->protect_kernel_logs = !strcmp(fields[26], "yes");
        observed->protect_clock = !strcmp(fields[27], "yes");
        observed->protect_hostname = !strcmp(fields[28], "yes");
        observed->lock_personality = !strcmp(fields[29], "yes");
        observed->memory_deny_write_execute = !strcmp(fields[30], "yes");
        observed->remove_ipc = !strcmp(fields[31], "yes");
        observed->keyring_private = !strcmp(fields[32], "private");
        observed->restrict_namespaces = !strcmp(fields[33], "yes");
        observed->restrict_realtime = !strcmp(fields[34], "yes");
        observed->address_families_unix = !strcmp(fields[35], "AF_UNIX");
        observed->syscall_architectures_native = !strcmp(fields[36], "native");
        observed->syscall_filter_system_service = bq_worker_systemd_filter_matches(fields[37], deadline);
        observed->syscall_error_number_eperm = !strcmp(fields[38], "1") || !strcmp(fields[38], "EPERM");
        int inaccessible_length = bq_worker_copy_field(observed->inaccessible_paths, sizeof(observed->inaccessible_paths), fields[39]);
        int read_only_length = bq_worker_copy_field(observed->read_only_paths, sizeof(observed->read_only_paths), fields[40]);
        int read_write_length = bq_worker_copy_field(observed->read_write_paths, sizeof(observed->read_write_paths), fields[41]);
        int user_length = bq_worker_copy_field(observed->user, sizeof(observed->user), fields[42]);
        int group_length = bq_worker_copy_field(observed->group, sizeof(observed->group), fields[43]);
        int part_of_length = bq_worker_copy_field(observed->part_of, sizeof(observed->part_of), fields[44]);
        int binds_to_length = bq_worker_copy_field(observed->binds_to, sizeof(observed->binds_to), fields[45]);
        int after_length = bq_worker_copy_field(observed->after, sizeof(observed->after), fields[46]);
        int collect_mode_length = bq_worker_copy_field(observed->collect_mode, sizeof(observed->collect_mode), fields[47]);
        observed->paths_valid = inaccessible_length > 0 && (u32)inaccessible_length < sizeof(observed->inaccessible_paths) &&
                                read_only_length > 0 && (u32)read_only_length < sizeof(observed->read_only_paths) &&
                                read_write_length > 0 && (u32)read_write_length < sizeof(observed->read_write_paths);
        observed->security_properties_valid = kill_mode_length >= 0 && (u32)kill_mode_length < sizeof(observed->kill_mode) &&
                                                invocation_length >= 0 && (u32)invocation_length < sizeof(observed->invocation_id) &&
                                                part_of_length >= 0 && (u32)part_of_length < sizeof(observed->part_of) &&
                                                binds_to_length >= 0 && (u32)binds_to_length < sizeof(observed->binds_to) &&
                                                after_length >= 0 && (u32)after_length < sizeof(observed->after) &&
                                                collect_mode_length >= 0 &&
                                                (u32)collect_mode_length < sizeof(observed->collect_mode) &&
                                                user_length > 0 && (u32)user_length < sizeof(observed->user) &&
                                                group_length > 0 && (u32)group_length < sizeof(observed->group);
        observed->result = bq_worker_systemd_result(fields[14], observed->active);
        char boot[BQ_WORKER_BOOT_CAP];
        if (!bq_worker_read_regular("/proc/sys/kernel/random/boot_id", boot, sizeof(boot))) error = BQ_IO;
        else if (bq_worker_copy_field(observed->boot_id, sizeof(observed->boot_id), boot) < 0) error = BQ_IO;
        /* The manager path is deliberately not opened here.  Its syntax,
         * no-follow traversal, cgroup-v2 mount and files are validated later
         * through config->cgroup_root before the value authorizes any signal. */
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_observe(BqWorkerBackend* backend, char const* unit,
                                                BqWorkerObserved* observed, u64 deadline)
{
    BqSystemdContext* context = backend->context;
    /* The socket instance may be cold-started before the manager creates the
     * unit.  Keep retrying within the existing five-second command deadline. */
    u32 attempts = context->starting ? 500 : 1;
    BqError error = BQ_OK;
    for (u32 attempt = 0; error == BQ_OK && attempt < attempts; attempt += 1)
    {
        error = bq_systemd_observe_once(backend, unit, observed, deadline);
        if (error == BQ_OK && !observed->unit_found && attempt + 1 < attempts)
        {
            u64 next = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 10);
            if (next > deadline) next = deadline;
            error = bq_worker_sleep_until(next);
        }
        else
        {
            attempts = attempt + 1;
        }
    }
    context->starting = false;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_signal(BqWorkerBackend* backend, char const* unit, char const* signal_name,
                                               u64 deadline)
{
    (void)backend;
    char output[512];
    char const* arguments[] = {BQ_SYSTEMD_BROKER, "signal", unit, signal_name, NULL};
    int status = 0;
    u32 remaining = bq_worker_remaining(deadline);
    BqError error = strcmp(signal_name, "TERM") && strcmp(signal_name, "KILL") &&
                    strcmp(signal_name, "CONT") ? BQ_BAD_REQUEST : !remaining ? BQ_IO :
                    bq_worker_exec_capture(arguments, output, sizeof(output), &status, remaining);
    if (error == BQ_OK && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) error = BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_join(BqWorkerBackend* backend, int* status, u64 deadline, bool cancellable)
{
    BqSystemdContext* context = backend->context;
    BqError error = BQ_OK;
    if (context->pid > 0)
    {
        bool finished = false;
        while (error == BQ_OK && !finished && !(cancellable && bq_worker_cancel_signal))
        {
            pid_t waited = waitpid(context->pid, status, WNOHANG);
            if (waited == context->pid)
            {
                context->pid = -1;
                finished = true;
            }
            else if (waited < 0 && errno != EINTR) error = BQ_IO;
            else
            {
                u64 now = bq_worker_monotonic_milliseconds();
                if (now >= deadline) error = BQ_WORKER_TIMEOUT;
                else error = bq_worker_sleep_until(bq_worker_deadline(now, 100) < deadline ?
                                                   bq_worker_deadline(now, 100) : deadline);
            }
        }
        if (cancellable && bq_worker_cancel_signal && !finished) error = BQ_WORKER_CANCEL_SIGNAL;
    }
    else
    {
        *status = 0;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_cleanup_launcher(BqWorkerBackend* backend, u64 deadline)
{
    BqSystemdContext* context = backend->context;
    BqError error = BQ_OK;
    if (context->pid > 0)
    {
        if (kill(context->pid, SIGKILL) != 0 && errno != ESRCH) error = BQ_CLEANUP_FAILED;
        int status = 0;
        if (error == BQ_OK && bq_worker_waitpid_until(context->pid, &status, deadline) != BQ_OK)
            error = BQ_CLEANUP_FAILED;
        if (error == BQ_OK) context->pid = -1;
    }
    context->starting = false;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_delay(BqWorkerBackend* backend, u32 milliseconds)
{
    (void)backend;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), milliseconds);
    BqError error = bq_worker_sleep_until(deadline);
    return error;
}

BUSTER_GLOBAL_LOCAL u64 bq_systemd_clock(BqWorkerBackend* backend)
{
    (void)backend;
    u64 result = bq_worker_monotonic_milliseconds();
    return result;
}

void bq_worker_backend_systemd(BqWorkerBackend* backend)
{
    static BqSystemdContext context;
    context.pid = -1;
    context.starting = false;
    *backend = (BqWorkerBackend){&context, bq_systemd_start, bq_systemd_observe, bq_systemd_signal,
                                bq_systemd_join, bq_systemd_cleanup_launcher,
                                bq_systemd_delay, bq_systemd_clock};
}

struct BqWorkerFinalization
{
    sigset_t prior_mask;
    bool masked;
    BqWorkerConfig const* config;
    int result_directory;
    dev_t result_device;
    ino_t result_inode;
    char result_root[BQ_PATH_CAP + 1];
    bool result_bound;
    bool phases_required;
    /* Zero on recovery: an interrupted attempt never inherits a fresh budget. */
    u64 execution_deadline;
    char result_digest[SHA256_HEX_CAPACITY];
    char bundle_digest[SHA256_HEX_CAPACITY];
    char full_digest[SHA256_HEX_CAPACITY];
    BqRecipeFiles recipe;
    /* #881 PR 4. The channel's BQPHASE version (0 before the handoff); the
     * retirement seams whose profile opens the coordinator's recipe gates
     * (NULL admits no retirement job; bq_worker_run passes the installed
     * seams, whose profile is blocked); A's digest; and the ready and
     * authority digests the version-2 channel carried. */
    unsigned phase_version;
    BqRetirementWorkerUnitSeams const* retirement;
    char retirement_preparation_sha256[SHA256_HEX_CAPACITY];
    char retirement_ready_sha256[SHA256_HEX_CAPACITY];
    char retirement_authority_sha256[SHA256_HEX_CAPACITY];
    /* #881 recovery L2: bq_worker_retirement_handoff_hold held this job. */
    bool retirement_held;
};

BUSTER_GLOBAL_LOCAL bool bq_worker_finalization_expired(BqWorkerFinalization const* finalization)
{
    BqWorkerBackend* backend = finalization && finalization->config ? finalization->config->backend : NULL;
    u64 now = backend ? backend->clock(backend) : bq_worker_monotonic_milliseconds();
    bool expired = finalization && finalization->execution_deadline && now >= finalization->execution_deadline;
    return expired;
}

/* The coordinator's recipe gate, decided per recipe (#881 N1): the
 * retirement recipe only when the seams carry a profile that
 * bq_retirement_profile_complete accepts, even when the queue's admission
 * predicate (bq_recipe_service) already admits it; the smoke recipe through
 * bq_recipe_service. */
BUSTER_GLOBAL_LOCAL bool bq_worker_recipe_admitted(BqRecipe selected, BqRetirementWorkerUnitSeams const* seams)
{
    bool admitted = selected == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED ?
                    seams != NULL && bq_retirement_profile_complete(seams->profile) : bq_recipe_service(selected);
    return admitted;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_recipe_service(BqRecipe selected, BqWorkerFinalization const* finalization)
{
    bool admitted = bq_worker_recipe_admitted(selected, finalization ? finalization->retirement : NULL);
    return admitted;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_finalization_recipe(BqJob const* job, BqWorkerFinalization* finalization)
{
    BqRecipeFiles expected;
    BqRecipe selected = job ? bq_request_recipe(&job->request) : BQ_RECIPE_UNKNOWN;
    bool ok = finalization && bq_worker_recipe_service(selected, finalization) && bq_recipe_files(selected, &expected);
    if (ok && finalization->recipe.name[0]) ok = !strcmp(finalization->recipe.name, expected.name);
    if (ok && !finalization->recipe.name[0]) finalization->recipe = expected;
    return ok;
}

/* bq_worker_run's launch gate on the recipe the finalization bound: the
 * smoke recipe, or the retirement recipe under the same completeness gate. */
BUSTER_GLOBAL_LOCAL bool bq_worker_recipe_launchable(BqWorkerFinalization const* finalization)
{
    bool launchable = finalization && (!strcmp(finalization->recipe.name, "validate-buster-v1") ||
                      (!strcmp(finalization->recipe.name, "native-retirement-performance-v1") &&
                       bq_worker_recipe_service(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, finalization)));
    return launchable;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_open(BqWorkerConfig const* config, BqJob const* job,
                                                   BqWorkerFinalization* finalization, bool create)
{
    char result_root[BQ_PATH_CAP + 1], name[64];
    BqError error = config && job && finalization && bq_worker_finalization_recipe(job, finalization) &&
                    bq_worker_result_path(config->workspace_root, job->id, job->token, result_root) &&
                    bq_workspace_name(name, job->id, job->token) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    int workspace = error == BQ_OK ? bq_worker_open_trusted_directory(config->workspace_root, false, false) : -1;
    bool broker_results = config && string_equal(config->workspace_root, S8(BQ_BROKER_WORKSPACES));
    int results = workspace >= 0 ? openat(workspace, "results", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool made_results = false;
    bool made_result = false;
    if (results < 0 && create && workspace >= 0 && errno == ENOENT)
    {
        made_results = mkdirat(workspace, "results", 0700) == 0;
        if (made_results)
            results = openat(workspace, "results", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (made_results && broker_results &&
            (results < 0 || fchown(results, (uid_t)-1, getegid()) != 0 || fchmod(results, 0710) != 0))
            error = BQ_CONFIGURATION_MISMATCH;
    }
    struct stat results_info = {0};
    bool results_ok = results >= 0 &&
                      bq_worker_directory_owner(results, !broker_results, false) &&
                      (!broker_results || (fstat(results, &results_info) == 0 &&
                       results_info.st_uid == geteuid() && results_info.st_gid == getegid() &&
                       (results_info.st_mode & 07777) == 0710));
    if (error == BQ_OK && (workspace < 0 || !results_ok))
        error = BQ_CONFIGURATION_MISMATCH;
    int result = error == BQ_OK ? openat(results, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (result < 0 && create && error == BQ_OK && errno == ENOENT)
    {
        made_result = mkdirat(results, name, 0700) == 0;
        if (made_result)
            result = openat(results, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    struct stat info = {0};
    if (error == BQ_OK && (result < 0 || fstat(result, &info) != 0 || !bq_worker_directory_owner(result, true, false)))
        error = BQ_CONFIGURATION_MISMATCH;
    if (error == BQ_OK && made_results && fsync(workspace) != 0) error = BQ_IO;
    if (error == BQ_OK && made_result && fsync(results) != 0) error = BQ_IO;
    if (error == BQ_OK)
    {
        if (finalization->result_directory >= 0) close(finalization->result_directory);
        finalization->result_directory = result;
        finalization->result_device = info.st_dev;
        finalization->result_inode = info.st_ino;
        snprintf(finalization->result_root, sizeof(finalization->result_root), "%s", result_root);
        result = -1;
    }
    if (result >= 0) close(result);
    if (results >= 0) close(results);
    if (workspace >= 0) close(workspace);
    return error;
}

#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL u32 bq_worker_test_finish_checkpoints;
BUSTER_GLOBAL_LOCAL u32 bq_worker_test_cancel_during_finish;
#endif

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_validate(BqWorkerConfig const* config, BqJob const* job,
                                                       BqWorkerFinalization* finalization);
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_binding_validate_seamed(BqJob const* job,
                                                                     BqRetirementWorkerUnitSeams const* seams);
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_evidence(BqJob const* job, BqOutcome outcome, BqError reason,
                                                       BqWorkerFinalization* finalization);
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_failure_artifacts(BqJob const* job, BqOutcome outcome,
                                                                BqError reason, BqWorkerFinalization* finalization);

BUSTER_GLOBAL_LOCAL void bq_worker_finish_checkpoint(void)
{
#ifdef BUSTER_BENCH_SERVICE_TEST
    bq_worker_test_finish_checkpoints += 1;
    if (bq_worker_test_cancel_during_finish == bq_worker_test_finish_checkpoints)
        raise(SIGTERM);
#endif
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_finish_cancel(BqQueue* queue, BqJob** job)
{
    BqError error = BQ_OK;
    if (bq_worker_cancel_signal && *job)
    {
        if (!(*job)->cancel_requested)
        {
            error = bq_cancel(queue, (*job)->id);
            *job = bq_job(&queue->state, (*job)->id);
        }
        if (error == BQ_OK && *job && (*job)->cancel_requested) bq_worker_cancel_signal = 0;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_consume_pending_cancel(sigset_t const* pending)
{
    int signals[] = {SIGTERM, SIGINT};
    BqError error = BQ_OK;
    for (u32 index = 0; error == BQ_OK && index < BUSTER_ARRAY_LENGTH(signals); index += 1)
    {
        int member = sigismember(pending, signals[index]);
        if (member < 0) error = BQ_IO;
        else if (member == 1)
        {
            sigset_t selected;
            sigemptyset(&selected);
            if (sigaddset(&selected, signals[index]) != 0) error = BQ_IO;
            else
            {
                struct timespec no_wait = {0};
                siginfo_t received = {0};
                int signal_number = sigtimedwait(&selected, &received, &no_wait);
                /* EAGAIN, EINTR or a different result leaves cancellation
                 * unproven, so do not append a successful terminal record. */
                if (signal_number != signals[index]) error = BQ_IO;
                else bq_worker_cancel_handler(signal_number);
            }
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_before_terminal(BqQueue* queue, BqJob* job, void* context)
{
    BqWorkerFinalization* finalization = context;
    sigset_t blocked, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGINT);
    BqError error = sigprocmask(SIG_BLOCK, &blocked, &finalization->prior_mask) == 0 ? BQ_OK : BQ_IO;
    finalization->masked = error == BQ_OK;
    if (error == BQ_OK && finalization->config && finalization->config->production_path)
        error = bq_worker_result_validate(finalization->config, job, finalization);
    if (error == BQ_OK && finalization->config && finalization->config->production_path && job && finalization->result_bound)
        error = job->result_bound ? bq_worker_result_binding_validate_seamed(job, finalization->retirement) :
                bq_result_bind(queue, job, string_from_pointer(finalization->result_root), finalization->result_digest,
                               finalization->bundle_digest, finalization->full_digest);
    /* Result validation and binding may hash and sync the whole bundle. Keep
     * TERM/INT blocked across that work, then sample pending signals at the
     * terminal journal boundary immediately before the FINISHED transition. */
    if (error == BQ_OK) bq_worker_finish_checkpoint();
    if (error == BQ_OK && sigpending(&pending) != 0) error = BQ_IO;
    if (error == BQ_OK) error = bq_worker_consume_pending_cancel(&pending);
    bool cancelled = bq_worker_cancel_signal != 0;
    if (error == BQ_OK) error = bq_worker_finish_cancel(queue, &job);
    if (error == BQ_OK && cancelled && finalization->config && finalization->config->production_path)
    {
        error = bq_worker_result_evidence(job, BQ_CANCELLED, BQ_WORKER_CANCEL_SIGNAL, finalization);
        if (error == BQ_OK) error = bq_worker_result_failure_artifacts(job, BQ_CANCELLED,
                                                                         BQ_WORKER_CANCEL_SIGNAL, finalization);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_finalization_restore(BqWorkerFinalization* finalization)
{
    BqError error = BQ_OK;
    if (finalization->masked)
    {
        if (sigprocmask(SIG_SETMASK, &finalization->prior_mask, NULL) != 0) error = BQ_IO;
        finalization->masked = false;
    }
    return error;
}

#define BQ_WORKER_RESULT_CAP 32768u

BUSTER_GLOBAL_LOCAL bool bq_worker_result_line(char const* body, char const* expected)
{
    size_t length = strlen(expected);
    char const* cursor = body;
    bool found = false;
    while (!found && cursor && *cursor)
    {
        char const* end = strchr(cursor, '\n');
        size_t line_length = end ? (size_t)(end - cursor) : strlen(cursor);
        found = line_length == length && !memcmp(cursor, expected, length);
        cursor = end ? end + 1 : NULL;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_result_hex(char const* bytes, u32 length)
{
    bool ok = length == SHA256_HEX_CAPACITY - 1;
    for (u32 index = 0; ok && index < length; index += 1)
    {
        char value = bytes[index];
        ok = (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_result_digest_line(char const* body, char const* prefix,
                                                      char output[SHA256_HEX_CAPACITY])
{
    size_t prefix_length = strlen(prefix);
    char const* cursor = body;
    bool found = false;
    bool valid = true;
    while (valid && cursor && *cursor)
    {
        char const* end = strchr(cursor, '\n');
        size_t line_length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (line_length >= prefix_length && !memcmp(cursor, prefix, prefix_length))
        {
            valid = !found && line_length == prefix_length + SHA256_HEX_CAPACITY - 1 &&
                    bq_worker_result_hex(cursor + prefix_length, SHA256_HEX_CAPACITY - 1);
            if (valid)
            {
                memcpy(output, cursor + prefix_length, SHA256_HEX_CAPACITY - 1);
                output[SHA256_HEX_CAPACITY - 1] = 0;
                found = true;
            }
        }
        cursor = end ? end + 1 : NULL;
    }
    return valid && found;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_result_nonterminal_line(char const* body, char const* prefix)
{
    size_t prefix_length = strlen(prefix);
    char const* cursor = body;
    bool found = false;
    bool valid = true;
    while (valid && cursor && *cursor)
    {
        char const* end = strchr(cursor, '\n');
        size_t line_length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (line_length >= prefix_length && !memcmp(cursor, prefix, prefix_length))
        {
            char const* value = cursor + prefix_length;
            size_t value_length = line_length - prefix_length;
            bool terminal = value_length == strlen("success") && !memcmp(value, "success", value_length);
            terminal = terminal || (value_length == strlen("running") && !memcmp(value, "running", value_length));
            valid = !found && value_length > 0 && !terminal;
            found = valid;
        }
        cursor = end ? end + 1 : NULL;
    }
    return valid && found;
}

typedef struct BqWorkerBundleEntry BqWorkerBundleEntry;
struct BqWorkerBundleEntry
{
    char path[BQ_WORKER_BUNDLE_PATH_CAP + 1];
    u64 size;
    char digest[SHA256_HEX_CAPACITY];
    bool seen;
};

typedef struct BqWorkerBundleFrame BqWorkerBundleFrame;
struct BqWorkerBundleFrame
{
    DIR* stream;
    char path[BQ_WORKER_BUNDLE_PATH_CAP + 1];
    u32 depth;
};

BUSTER_GLOBAL_LOCAL bool bq_worker_bundle_path_valid(char const* path)
{
    u64 length = path ? strlen(path) : 0;
    bool ok = length > 0 && length <= BQ_WORKER_BUNDLE_PATH_CAP && path[0] != '/';
    u64 component = 0;
    for (u64 index = 0; ok && index <= length; index += 1)
    {
        if (index == length || path[index] == '/')
        {
            u64 component_length = index - component;
            ok = component_length && !(component_length == 1 && path[component] == '.') &&
                 !(component_length == 2 && path[component] == '.' && path[component + 1] == '.');
            component = index + 1;
        }
        else
        {
            u8 value = (u8)path[index];
            ok = value >= 0x21 && value <= 0x7e && value != '\\';
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_bundle_reserved(BqRecipeFiles const* recipe, char const* path)
{
    char const* names[] = {recipe ? recipe->manifest : NULL, recipe ? recipe->bundle : NULL,
                           recipe ? recipe->outcome : NULL};
    bool reserved = false;
    for (u32 index = 0; recipe && path && !reserved && index < BUSTER_ARRAY_LENGTH(names); index += 1)
        reserved = names[index][0] && !strcmp(path, names[index]);
    return reserved;
}

BUSTER_GLOBAL_LOCAL u64 bq_worker_bundle_total_cap(BqRecipeFiles const* recipe)
{
    u64 result = recipe && !strcmp(recipe->name, "native-retirement-performance-v1") ?
                 BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP : BQ_WORKER_BUNDLE_TOTAL_CAP;
    return result;
}

BUSTER_GLOBAL_LOCAL int bq_worker_bundle_relative(char output[BQ_WORKER_BUNDLE_PATH_CAP + 1],
                                                   char const* parent, char const* leaf)
{
    u32 parent_length = parent ? (u32)strlen(parent) : 0;
    u32 leaf_length = leaf ? (u32)strlen(leaf) : 0;
    u32 separator = parent_length != 0;
    u64 length = (u64)parent_length + separator + leaf_length;
    bool ok = length <= BQ_WORKER_BUNDLE_PATH_CAP && length <= INT_MAX;
    if (ok && parent_length) memcpy(output, parent, parent_length);
    if (ok && separator) output[parent_length] = '/';
    if (ok) memcpy(output + parent_length + separator, leaf, leaf_length);
    if (ok) output[length] = 0;
    return ok ? (int)length : -1;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_bundle_next_line(u8 const* bytes, u64 size, u64* cursor,
                                                     char output[BQ_WORKER_BUNDLE_LINE_CAP])
{
    u64 start = cursor ? *cursor : size;
    u8 const* end = start < size ? memchr(bytes + start, '\n', (size_t)(size - start)) : NULL;
    u64 length = end ? (u64)(end - (bytes + start)) : 0;
    bool ok = end && length > 0 && length + 1 < BQ_WORKER_BUNDLE_LINE_CAP &&
              !memchr(bytes + start, 0, (size_t)length);
    if (ok)
    {
        memcpy(output, bytes + start, (size_t)length);
        output[length] = 0;
        *cursor = start + length + 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_bundle_decimal(char const* text, u64 maximum, u64* output)
{
    String8 value = string_from_pointer(text);
    IntegerParsingU64 parsed = string8_parse_u64_decimal(value);
    bool ok = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == value.length && parsed.value <= maximum;
    if (output) *output = ok ? parsed.value : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_bundle_entry_parse(char const* line, BqWorkerBundleEntry* entry)
{
    char const* first_space = line ? strchr(line, ' ') : NULL;
    char const* second_space = first_space ? strchr(first_space + 1, ' ') : NULL;
    char digest[SHA256_HEX_CAPACITY] = {0};
    char size_text[32] = {0};
    u64 size = 0;
    bool ok = first_space && second_space && first_space - line == SHA256_HEX_CAPACITY - 1 &&
              (u64)(second_space - first_space - 1) < sizeof(size_text) &&
              strlen(second_space + 1) <= BQ_WORKER_BUNDLE_PATH_CAP &&
              !strchr(second_space + 1, ' ');
    if (ok)
    {
        memcpy(digest, line, SHA256_HEX_CAPACITY - 1);
        digest[SHA256_HEX_CAPACITY - 1] = 0;
        memcpy(size_text, first_space + 1, (size_t)(second_space - first_space - 1));
        size_text[second_space - first_space - 1] = 0;
        ok = bq_worker_result_hex(digest, SHA256_HEX_CAPACITY - 1) &&
             bq_worker_bundle_decimal(size_text, BQ_WORKER_BUNDLE_FILE_CAP, &size) &&
             bq_worker_bundle_path_valid(second_space + 1);
    }
    if (ok)
    {
        memcpy(entry->path, second_space + 1, strlen(second_space + 1) + 1);
        memcpy(entry->digest, digest, sizeof(entry->digest));
        entry->size = size;
        entry->seen = false;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_worker_bundle_entry_compare(char const* left, char const* right)
{
    int result = strcmp(left, right);
    return result;
}

BUSTER_GLOBAL_LOCAL int bq_worker_bundle_open_relative(int root, char const* path)
{
    int current = root >= 0 ? fcntl(root, F_DUPFD_CLOEXEC, 3) : -1;
    u64 length = path ? strlen(path) : 0;
    u64 offset = 0;
    while (current >= 0 && offset < length)
    {
        u64 end = offset;
        while (end < length && path[end] != '/') end += 1;
        char name[BQ_WORKER_BUNDLE_PATH_CAP + 1];
        u64 component_length = end - offset;
        bool valid = component_length > 0 && component_length <= BQ_WORKER_BUNDLE_PATH_CAP;
        if (valid)
        {
            memcpy(name, path + offset, (size_t)component_length);
            name[component_length] = 0;
            int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | (end < length ? O_DIRECTORY : O_NONBLOCK);
            int next = openat(current, name, flags);
            close(current);
            current = next;
        }
        else
        {
            close(current);
            current = -1;
        }
        offset = end + 1;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_bundle_file_digest(int root, char const* path, u64* size,
                                                       char output[SHA256_HEX_CAPACITY])
{
    int descriptor = bq_worker_bundle_open_relative(root, path);
    struct stat before = {0}, after = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
              before.st_nlink == 1 && before.st_size >= 0 &&
              (u64)before.st_size <= BQ_WORKER_BUNDLE_FILE_CAP &&
              (before.st_uid == 0 || before.st_uid == geteuid());
    Sha256 digest;
    sha256_init(&digest);
    u8 bytes[64 * 1024];
    u64 total = 0;
    while (ok)
    {
        ssize_t count = read(descriptor, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) ok = false;
        else if (!count) break;
        else if (total > BQ_WORKER_BUNDLE_FILE_CAP - (u64)count) ok = false;
        else
        {
            sha256_add(&digest, bytes, (u64)count);
            total += (u64)count;
        }
    }
    if (ok)
    {
        ok = fstat(descriptor, &after) == 0 && after.st_dev == before.st_dev && after.st_ino == before.st_ino &&
             after.st_size == before.st_size && total == (u64)before.st_size;
        if (ok) sha256_finish_hex(&digest, (char8*)output);
    }
    if (!ok) output[0] = 0;
    if (size) *size = ok ? total : 0;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_bundle_validate_recipe(int result_directory,
                                                              BqRecipeFiles const* recipe,
                                                              char const expected_digest[SHA256_HEX_CAPACITY],
                                                              char full_digest[SHA256_HEX_CAPACITY])
{
    if (full_digest) full_digest[0] = 0;
    int descriptor = recipe && result_directory >= 0 ? openat(result_directory, recipe->bundle,
                                                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0}, expected_info = {0}, after = {0};
    bool opened = recipe && expected_digest && descriptor >= 0 && fstatat(result_directory, recipe->bundle,
                                                                 &expected_info, AT_SYMLINK_NOFOLLOW) == 0 &&
                  fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
                  expected_info.st_dev == info.st_dev && expected_info.st_ino == info.st_ino && info.st_nlink == 1 &&
                  info.st_size > 0 && (u64)info.st_size <= BQ_WORKER_BUNDLE_CAP &&
                  (info.st_uid == 0 || info.st_uid == geteuid());
    u8* bytes = NULL;
    u64 size = opened ? (u64)info.st_size : 0;
    if (opened)
    {
        bytes = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        opened = bytes != MAP_FAILED;
    }
    u64 used = 0;
    while (opened && used < size)
    {
        ssize_t count = read(descriptor, bytes + used, (size_t)(size - used));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) opened = false;
        else used += (u64)count;
    }
    if (opened)
    {
        char extra = 0;
        opened = read(descriptor, &extra, 1) == 0 && fstat(descriptor, &after) == 0 &&
                 after.st_dev == info.st_dev && after.st_ino == info.st_ino && after.st_size == info.st_size;
    }
    if (descriptor >= 0 && close(descriptor) != 0) opened = false;
    BqWorkerBundleEntry* entries = NULL;
    if (opened)
    {
        entries = mmap(NULL, sizeof(*entries) * BQ_WORKER_BUNDLE_ENTRY_CAP,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        opened = entries != MAP_FAILED;
    }
    bool valid = opened;
    u64 cursor = 0, declared_bytes = 0;
    u64 total_cap = bq_worker_bundle_total_cap(recipe);
    u64 declared_entries = 0;
    char line[BQ_WORKER_BUNDLE_LINE_CAP];
    if (valid)
    {
        valid = bq_worker_bundle_next_line(bytes, size, &cursor, line) && !strcmp(line, "BQ-BUNDLE-V1");
        valid = valid && bq_worker_bundle_next_line(bytes, size, &cursor, line) &&
                !strncmp(line, "entries=", 8) && bq_worker_bundle_decimal(line + 8,
                                                                            BQ_WORKER_BUNDLE_ENTRY_CAP,
                                                                            &declared_entries);
        valid = valid && bq_worker_bundle_next_line(bytes, size, &cursor, line) &&
                !strncmp(line, "bytes=", 6) && bq_worker_bundle_decimal(line + 6,
                                                                          total_cap,
                                                                          &declared_bytes);
    }
    for (u64 index = 0; valid && index < declared_entries; index += 1)
    {
        valid = bq_worker_bundle_next_line(bytes, size, &cursor, line) &&
                bq_worker_bundle_entry_parse(line, entries + index) &&
                (index == 0 || bq_worker_bundle_entry_compare(entries[index - 1].path, entries[index].path) < 0);
    }
    if (valid) valid = cursor == size;
    if (valid)
    {
        Sha256 digest;
        char actual[SHA256_HEX_CAPACITY];
        sha256_init(&digest);
        sha256_add(&digest, bytes, size);
        sha256_finish_hex(&digest, (char8*)actual);
        valid = !memcmp(actual, expected_digest, SHA256_HEX_CAPACITY);
    }
    if (valid)
    {
        int root = fcntl(result_directory, F_DUPFD_CLOEXEC, 3);
        valid = root >= 0;
        u64 measured_bytes = 0;
        for (u64 index = 0; valid && index < declared_entries; index += 1)
        {
            u64 actual_size = 0;
            char actual_digest[SHA256_HEX_CAPACITY];
            valid = bq_worker_bundle_file_digest(root, entries[index].path, &actual_size, actual_digest) &&
                    actual_size == entries[index].size && !memcmp(actual_digest, entries[index].digest, SHA256_HEX_CAPACITY) &&
                    measured_bytes <= total_cap - actual_size;
            if (valid) measured_bytes += actual_size;
        }
        valid = valid && measured_bytes == declared_bytes;
        if (root >= 0) close(root);
    }
    if (valid)
    {
        int root = openat(result_directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int walk_root = openat(result_directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BqWorkerBundleFrame* frames = mmap(NULL, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP,
                                            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        DIR* stream = root >= 0 ? fdopendir(root) : NULL;
        u32 depth = stream ? 1 : 0;
        u32 entry_count = 0;
        valid = stream != NULL && frames != MAP_FAILED && walk_root >= 0;
        if (valid)
        {
            memset(frames, 0, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP);
            frames[0].stream = stream;
            frames[0].depth = 1;
        }
        else
        {
            if (stream) closedir(stream);
            else if (root >= 0) close(root);
            if (walk_root >= 0) close(walk_root);
            root = -1;
            walk_root = -1;
        }
        while (valid && depth)
        {
            BqWorkerBundleFrame* frame = frames + depth - 1;
            errno = 0;
            struct dirent* item = readdir(frame->stream);
            if (!item)
            {
                if (errno) valid = false;
                else
                {
                    closedir(frame->stream);
                    frame->stream = NULL;
                    depth -= 1;
                }
            }
            else if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
            {
            }
            else
            {
                entry_count += 1;
                char relative[BQ_WORKER_BUNDLE_PATH_CAP + 1];
                int length = bq_worker_bundle_relative(relative, frame->path, item->d_name);
                valid = entry_count <= BQ_WORKER_BUNDLE_ENTRY_CAP && length > 0 &&
                        (u32)length <= BQ_WORKER_BUNDLE_PATH_CAP && bq_worker_bundle_path_valid(relative);
                int parent = valid ? dirfd(frame->stream) : -1;
                struct stat child_info = {0};
                valid = valid && parent >= 0 && fstatat(parent, item->d_name, &child_info, AT_SYMLINK_NOFOLLOW) == 0;
                if (valid && S_ISDIR(child_info.st_mode))
                {
                    valid = depth < BQ_WORKER_BUNDLE_DEPTH_CAP &&
                            (child_info.st_uid == 0 || child_info.st_uid == geteuid()) &&
                            (child_info.st_mode & 022) == 0;
                    int child = valid ? openat(parent, item->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
                    DIR* child_stream = child >= 0 ? fdopendir(child) : NULL;
                    struct stat child_opened = {0};
                    valid = valid && child >= 0 && fstat(child, &child_opened) == 0 &&
                            child_opened.st_dev == child_info.st_dev && child_opened.st_ino == child_info.st_ino;
                    if (!child_stream && child >= 0) close(child);
                    valid = valid && child_stream != NULL;
                    if (valid)
                    {
                        BqWorkerBundleFrame* next = frames + depth;
                        memset(next, 0, sizeof(*next));
                        next->stream = child_stream;
                        next->depth = frame->depth + 1;
                        memcpy(next->path, relative, (size_t)length + 1);
                        depth += 1;
                    }
                    else if (child_stream)
                    {
                        closedir(child_stream);
                    }
                }
                else if (valid && S_ISREG(child_info.st_mode))
                {
                    if (!bq_worker_bundle_reserved(recipe, relative))
                    {
                        u64 actual_size = 0;
                        char actual_digest[SHA256_HEX_CAPACITY];
                        u64 index = 0;
                        bool found = false;
                        while (!found && index < declared_entries)
                        {
                            found = !strcmp(entries[index].path, relative);
                            if (!found) index += 1;
                        }
                        bool file_ok = found && !entries[index].seen &&
                                       bq_worker_bundle_file_digest(walk_root, relative, &actual_size, actual_digest) &&
                                       actual_size == entries[index].size &&
                                       !memcmp(actual_digest, entries[index].digest, SHA256_HEX_CAPACITY);
                        valid = file_ok;
                        if (valid) entries[index].seen = true;
                    }
                }
                else if (valid)
                {
                    valid = false;
                }
            }
        }
        while (depth)
        {
            if (frames[depth - 1].stream) closedir(frames[depth - 1].stream);
            depth -= 1;
        }
        if (walk_root >= 0) close(walk_root);
        if (valid)
        {
            for (u64 index = 0; valid && index < declared_entries; index += 1) valid = entries[index].seen;
        }
        if (frames != MAP_FAILED) munmap(frames, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP);
    }
    if (valid && full_digest)
    {
        Sha256 full;
        sha256_init(&full);
        sha256_add(&full, "BQ-FULL-BUNDLE-V1", 18);
        sha256_add(&full, bytes, size);
        for (u64 index = 0; index < declared_entries; index += 1)
        {
            u64 entry_size = entries[index].size;
            sha256_add(&full, entries[index].path, strlen(entries[index].path));
            sha256_add(&full, "\0", 1);
            sha256_add(&full, entries[index].digest, 64);
            sha256_add(&full, &entry_size, sizeof(entry_size));
        }
        sha256_finish_hex(&full, (char8*)full_digest);
    }
    if (entries && entries != MAP_FAILED) munmap(entries, sizeof(*entries) * BQ_WORKER_BUNDLE_ENTRY_CAP);
    if (bytes && bytes != MAP_FAILED) munmap(bytes, (size_t)size);
    return valid ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
}

#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL BqError bq_worker_bundle_validate_full(int result_directory,
                                                            char const expected_digest[SHA256_HEX_CAPACITY],
                                                            char full_digest[SHA256_HEX_CAPACITY])
{
    BqRecipeFiles recipe;
    BqError error = bq_recipe_files(BQ_RECIPE_VALIDATE_BUSTER, &recipe) ?
                    bq_worker_bundle_validate_recipe(result_directory, &recipe, expected_digest, full_digest) :
                    BQ_CONFIGURATION_MISMATCH;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_bundle_validate(int result_directory,
                                                       char const expected_digest[SHA256_HEX_CAPACITY])
{
    BqError error = bq_worker_bundle_validate_full(result_directory, expected_digest, NULL);
    return error;
}
#endif

/* A long-lived result descriptor can predate the entries a walk must reach.
 * btrfs (Linux 6.5+) fixes a directory's last readdir index when its open file
 * is created, and a consumed offset hides entries everywhere, so walks read a
 * fresh open file that must still be the pinned inode. */
BUSTER_GLOBAL_LOCAL int bq_worker_reopen_directory(int directory)
{
    int result = directory >= 0 ? openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat pinned = {0}, fresh = {0};
    bool same = result >= 0 && fstat(directory, &pinned) == 0 && fstat(result, &fresh) == 0 &&
                pinned.st_dev == fresh.st_dev && pinned.st_ino == fresh.st_ino;
    if (!same && result >= 0)
    {
        close(result);
        result = -1;
    }
    return result;
}

/* Fsyncs the whole result tree. A foreign entry type or an entry replaced
 * between listing and open is a configuration mismatch; a failed read or
 * fsync leaves durability unknown. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_sync_tree(int result_directory)
{
    BqWorkerBundleFrame* frames = mmap(NULL, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP,
                                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int root = bq_worker_reopen_directory(result_directory);
    DIR* stream = root >= 0 ? fdopendir(root) : NULL;
    if (stream) root = -1;
    BqError error = frames != MAP_FAILED && stream != NULL ? BQ_OK : BQ_IO;
    u32 depth = error == BQ_OK ? 1 : 0;
    if (error == BQ_OK)
    {
        memset(frames, 0, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP);
        frames[0].stream = stream;
    }
    else
    {
        if (stream) closedir(stream);
        else if (root >= 0) close(root);
        root = -1;
    }
    while (error == BQ_OK && depth)
    {
        BqWorkerBundleFrame* frame = frames + depth - 1;
        errno = 0;
        struct dirent* item = readdir(frame->stream);
        if (!item)
        {
            if (errno) error = BQ_IO;
            else
            {
                int directory = dirfd(frame->stream);
                if (fsync(directory) != 0) error = BQ_IO;
                closedir(frame->stream);
                frame->stream = NULL;
                depth -= 1;
            }
        }
        else if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
        {
        }
        else
        {
            int parent = dirfd(frame->stream);
            struct stat info = {0};
            if (parent < 0 || fstatat(parent, item->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) error = BQ_IO;
            else if (S_ISDIR(info.st_mode))
            {
                int child = depth < BQ_WORKER_BUNDLE_DEPTH_CAP ?
                            openat(parent, item->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
                DIR* child_stream = child >= 0 ? fdopendir(child) : NULL;
                if (!child_stream && child >= 0) close(child);
                struct stat opened = {0};
                if (!child_stream || fstat(child, &opened) != 0) error = BQ_IO;
                else if (opened.st_dev != info.st_dev || opened.st_ino != info.st_ino) error = BQ_CONFIGURATION_MISMATCH;
                if (error == BQ_OK)
                {
                    frames[depth].stream = child_stream;
                    depth += 1;
                }
                else if (child_stream)
                {
                    closedir(child_stream);
                }
            }
            else if (S_ISREG(info.st_mode))
            {
                int descriptor = openat(parent, item->d_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
                struct stat opened = {0};
                if (descriptor < 0 || fstat(descriptor, &opened) != 0) error = BQ_IO;
                else if (opened.st_dev != info.st_dev || opened.st_ino != info.st_ino || opened.st_size != info.st_size)
                    error = BQ_CONFIGURATION_MISMATCH;
                else if (fsync(descriptor) != 0) error = BQ_IO;
                if (descriptor >= 0 && close(descriptor) != 0 && error == BQ_OK) error = BQ_IO;
            }
            else
            {
                error = BQ_CONFIGURATION_MISMATCH;
            }
        }
    }
    while (depth)
    {
        if (frames[depth - 1].stream) closedir(frames[depth - 1].stream);
        depth -= 1;
    }
    if (root >= 0 && close(root) != 0 && error == BQ_OK) error = BQ_IO;
    if (frames != MAP_FAILED) munmap(frames, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP);
    return error;
}

/* A bound result's manifest, bundle and full digests are still the ones the
 * queue recorded. The service recipe is admitted; the retirement recipe only
 * when the seams' profile is complete (bq_worker_recipe_admitted, #881 PR 3
 * and N1), which the compiled blocked profile never is, so every production
 * caller (the non-pinned entries) still refuses it. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_binding_validate_pinned(BqJob const* job, int result_directory,
                                                                     BqRetirementWorkerUnitSeams const* seams)
{
    char path[BQ_PATH_CAP + 1] = {0};
    BqRecipeFiles recipe;
    BqRecipe selected = job ? bq_request_recipe(&job->request) : BQ_RECIPE_UNKNOWN;
    bool admitted = bq_worker_recipe_admitted(selected, seams);
    BqError error = job && job->result_bound && admitted && bq_recipe_files(selected, &recipe) &&
                    bq_worker_text(string_from_pointer(job->result_root), path, sizeof(path)) &&
                    path[0] == '/' ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    struct stat directory_info = {0};
    if (error == BQ_OK)
    {
        error = result_directory >= 0 && fstat(result_directory, &directory_info) == 0 && S_ISDIR(directory_info.st_mode) &&
                (directory_info.st_uid == 0 || directory_info.st_uid == geteuid()) &&
                (directory_info.st_mode & 077) == 0 ?
                BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    int manifest = error == BQ_OK ? openat(result_directory, recipe.manifest,
                                            O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    char bytes[BQ_WORKER_RESULT_CAP + 1];
    u32 used = 0;
    struct stat manifest_info = {0};
    if (error == BQ_OK)
    {
        struct stat expected_manifest = {0};
        error = manifest >= 0 && fstatat(result_directory, recipe.manifest, &expected_manifest,
                                         AT_SYMLINK_NOFOLLOW) == 0 && fstat(manifest, &manifest_info) == 0 &&
                expected_manifest.st_dev == manifest_info.st_dev && expected_manifest.st_ino == manifest_info.st_ino &&
                S_ISREG(manifest_info.st_mode) &&
                manifest_info.st_nlink == 1 && (manifest_info.st_uid == 0 || manifest_info.st_uid == geteuid()) &&
                (manifest_info.st_mode & 0222) == 0 && manifest_info.st_size > 0 &&
                (u64)manifest_info.st_size <= BQ_WORKER_RESULT_CAP ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    while (error == BQ_OK && used < BQ_WORKER_RESULT_CAP)
    {
        ssize_t count = read(manifest, bytes + used, BQ_WORKER_RESULT_CAP - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) error = BQ_IO;
        else if (!count) break;
        else used += (u32)count;
    }
    if (error == BQ_OK)
    {
        char extra = 0;
        error = read(manifest, &extra, 1) == 0 ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (manifest >= 0 && close(manifest) != 0 && error == BQ_OK) error = BQ_IO;
    char actual_manifest[SHA256_HEX_CAPACITY] = {0};
    char expected_bundle[SHA256_HEX_CAPACITY] = {0};
    char recursive_digest[SHA256_HEX_CAPACITY] = {0};
    char actual_full[SHA256_HEX_CAPACITY] = {0};
    char result_line[BQ_PATH_CAP + 16] = {0};
    char recipe_line[BQ_RECIPE_NAME_CAP + 8] = {0};
    u32 result_line_length = 12 + (u32)strlen(path);
    int recipe_line_length = snprintf(recipe_line, sizeof(recipe_line), "recipe=%s", recipe.name);
    if (result_line_length < sizeof(result_line))
    {
        memcpy(result_line, "result-root=", 12);
        memcpy(result_line + 12, path, strlen(path));
    }
    if (error == BQ_OK)
    {
        bytes[used] = 0;
        Sha256 digest;
        sha256_init(&digest);
        sha256_add(&digest, bytes, used);
        sha256_finish_hex(&digest, (char8*)actual_manifest);
        bool lines = result_line_length < sizeof(result_line) && recipe_line_length > 0 &&
                     (u32)recipe_line_length < sizeof(recipe_line) &&
                     bq_worker_result_line((char const*)bytes, "schema=1") &&
                     bq_worker_result_line((char const*)bytes, recipe_line) &&
                     bq_worker_result_line((char const*)bytes, result_line);
        if (lines)
        {
            lines = bq_worker_result_digest_line((char const*)bytes, "bundle-sha256=", expected_bundle);
        }
        error = lines && !memcmp(actual_manifest, job->result_manifest_digest, SHA256_HEX_CAPACITY) ?
                BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK)
        error = bq_worker_bundle_validate_recipe(result_directory, &recipe, expected_bundle, recursive_digest);
    if (error == BQ_OK)
    {
        Sha256 full;
        sha256_init(&full);
        sha256_add(&full, "BQ-RESULT-FULL-V1", 18);
        sha256_add(&full, bytes, used);
        sha256_add(&full, recursive_digest, 64);
        sha256_finish_hex(&full, (char8*)actual_full);
        error = !memcmp(expected_bundle, job->result_bundle_digest, SHA256_HEX_CAPACITY) &&
                !memcmp(actual_full, job->result_full_digest, SHA256_HEX_CAPACITY) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_binding_validate_at(BqJob const* job, int result_directory)
{
    BqError error = bq_worker_result_binding_validate_pinned(job, result_directory, NULL);
    return error;
}

/* The bound result under its trusted root, with the given seams. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_binding_validate_seamed(BqJob const* job,
                                                                     BqRetirementWorkerUnitSeams const* seams)
{
    int directory = job && job->result_bound ?
        bq_worker_open_trusted_directory(string_from_pointer(job->result_root), true, false) : -1;
    BqError error = bq_worker_result_binding_validate_pinned(job, directory, seams);
    if (directory >= 0 && close(directory) != 0 && error == BQ_OK) error = BQ_IO;
    return error;
}

BqError bq_worker_result_binding_validate(BqJob const* job)
{
    BqError error = bq_worker_result_binding_validate_seamed(job, NULL);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_validate_smoke(BqWorkerConfig const* config, BqJob const* job,
                                                             BqWorkerFinalization* finalization)
{
    char bytes[BQ_WORKER_RESULT_CAP + 1];
    char job_line[64], token_line[64], workspace_line[BQ_PATH_CAP + 32], result_line[BQ_PATH_CAP + 32];
    char base_line[100], candidate_line[100], base_binary_line[BQ_PATH_CAP + 64], candidate_binary_line[BQ_PATH_CAP + 64];
    char base_digest[SHA256_HEX_CAPACITY], candidate_digest[SHA256_HEX_CAPACITY], bundle_digest[SHA256_HEX_CAPACITY];
    char result_digest[SHA256_HEX_CAPACITY], recursive_digest[SHA256_HEX_CAPACITY];
    char full_digest[SHA256_HEX_CAPACITY];
    char workspace_text[BQ_PATH_CAP + 1], workspace_name[64], base_text[65], candidate_text[65];
    BqError error = job && config && finalization && finalization->result_directory >= 0 &&
                    bq_worker_finalization_recipe(job, finalization) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    int job_length = 0, token_length = 0, workspace_length = 0, result_length = 0, base_length = 0, candidate_length = 0;
    int base_binary_length = 0, candidate_binary_length = 0;
    if (error == BQ_OK) error = bq_worker_result_sync_tree(finalization->result_directory);
    if (error == BQ_OK)
    {
        String8 base = bq_field(&job->request, 3), candidate = bq_field(&job->request, 4);
        bool workspace_valid = bq_worker_text(config->workspace_root, workspace_text, sizeof(workspace_text)) &&
                               bq_workspace_name(workspace_name, job->id, job->token) &&
                               bq_worker_text(base, base_text, sizeof(base_text)) &&
                               bq_worker_text(candidate, candidate_text, sizeof(candidate_text));
        job_length = snprintf(job_line, sizeof(job_line), "job-id=%" PRIu64, (uint64_t)job->id);
        token_length = snprintf(token_line, sizeof(token_line), "attempt-token=%" PRIu64, (uint64_t)job->token);
        char const* workspace_parts[] = {"workspace-root=", workspace_text};
        workspace_length = workspace_valid ? bq_worker_join_text(workspace_line, sizeof(workspace_line),
                                                                  workspace_parts, BUSTER_ARRAY_LENGTH(workspace_parts)) : -1;
        result_length = snprintf(result_line, sizeof(result_line), "result-root=%s", finalization->result_root);
        char const* base_parts[] = {"base-revision=", base_text};
        char const* candidate_parts[] = {"candidate-revision=", candidate_text};
        base_length = bq_worker_join_text(base_line, sizeof(base_line), base_parts, BUSTER_ARRAY_LENGTH(base_parts));
        candidate_length = bq_worker_join_text(candidate_line, sizeof(candidate_line), candidate_parts,
                                               BUSTER_ARRAY_LENGTH(candidate_parts));
        char const* base_binary_parts[] = {"base-binary=", workspace_text, "/", workspace_name, "/base/build/Release/ide"};
        char const* candidate_binary_parts[] = {"candidate-binary=", workspace_text, "/", workspace_name,
                                                "/candidate/build/Release/ide"};
        base_binary_length = workspace_valid ? bq_worker_join_text(base_binary_line, sizeof(base_binary_line),
                                                                    base_binary_parts, BUSTER_ARRAY_LENGTH(base_binary_parts)) : -1;
        candidate_binary_length = workspace_valid ? bq_worker_join_text(candidate_binary_line, sizeof(candidate_binary_line),
                                                                         candidate_binary_parts,
                                                                         BUSTER_ARRAY_LENGTH(candidate_binary_parts)) : -1;
        if (!workspace_valid || job_length <= 0 || token_length <= 0 || workspace_length <= 0 || result_length <= 0 ||
            base_length <= 0 || candidate_length <= 0 || base_binary_length <= 0 ||
            (size_t)base_binary_length >= sizeof(base_binary_line) || candidate_binary_length <= 0 ||
            (size_t)candidate_binary_length >= sizeof(candidate_binary_line))
            error = BQ_CONFIGURATION_MISMATCH;
    }
    int descriptor = error == BQ_OK ? openat(finalization->result_directory, finalization->recipe.manifest,
                                              O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat directory_info = {0}, file_info = {0}, expected_file_info = {0};
    if (error == BQ_OK)
    {
        error = fstat(finalization->result_directory, &directory_info) == 0 && directory_info.st_dev == finalization->result_device &&
                directory_info.st_ino == finalization->result_inode && descriptor >= 0 &&
                fstatat(finalization->result_directory, finalization->recipe.manifest, &expected_file_info,
                        AT_SYMLINK_NOFOLLOW) == 0 && fstat(descriptor, &file_info) == 0 &&
                expected_file_info.st_dev == file_info.st_dev && expected_file_info.st_ino == file_info.st_ino &&
                S_ISREG(file_info.st_mode) && file_info.st_nlink == 1 && (file_info.st_uid == 0 || file_info.st_uid == geteuid()) &&
                (file_info.st_mode & 0222) == 0 &&
                (u64)file_info.st_size <= BQ_WORKER_RESULT_CAP ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    u32 used = 0;
    while (error == BQ_OK && used < BQ_WORKER_RESULT_CAP)
    {
        ssize_t count = read(descriptor, bytes + used, BQ_WORKER_RESULT_CAP - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) error = BQ_IO;
        else if (!count) break;
        else used += (u32)count;
    }
    if (error == BQ_OK)
    {
        char extra;
        error = read(descriptor, &extra, 1) == 0 ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (descriptor >= 0 && close(descriptor) != 0 && error == BQ_OK) error = BQ_IO;
    if (error == BQ_OK)
    {
        bytes[used] = 0;
        char prefix[BQ_RECIPE_NAME_CAP + 96];
        int prefix_length = snprintf(prefix, sizeof(prefix),
                                     "schema=1\nrecipe=%s\nstatus=succeeded\nstage=throughput\nprocess-result=success\n",
                                     finalization->recipe.name);
        bool lines = prefix_length > 0 && (u32)prefix_length < sizeof(prefix) && used >= (u32)prefix_length &&
                     !memcmp(bytes, prefix, (u32)prefix_length) &&
                     bq_worker_result_line((char const*)bytes, job_line) && bq_worker_result_line((char const*)bytes, token_line) &&
                     bq_worker_result_line((char const*)bytes, workspace_line) && bq_worker_result_line((char const*)bytes, result_line) &&
                     bq_worker_result_line((char const*)bytes, base_line) && bq_worker_result_line((char const*)bytes, candidate_line) &&
                     bq_worker_result_line((char const*)bytes, base_binary_line) &&
                     bq_worker_result_line((char const*)bytes, candidate_binary_line) &&
                     bq_worker_result_line((char const*)bytes, "driver=/usr/local/libexec/buster-bench-build") &&
                     bq_worker_result_line((char const*)bytes, "throughput=/usr/local/libexec/buster-bench-throughput") &&
                     bq_worker_result_line((char const*)bytes, "trusted-source-scope=operator-installed-read-only") &&
                     bq_worker_result_line((char const*)bytes, "namespace-policy=private-workspace-post-run-identity") &&
                     bq_worker_result_digest_line((char const*)bytes, "base-binary-sha256=", base_digest) &&
                     bq_worker_result_digest_line((char const*)bytes, "candidate-binary-sha256=", candidate_digest) &&
                     bq_worker_result_digest_line((char const*)bytes, "bundle-sha256=", bundle_digest);
        error = lines ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK && memchr(bytes, 0, used) != NULL) error = BQ_CONFIGURATION_MISMATCH;
    if (error == BQ_OK)
    {
        error = bq_worker_bundle_validate_recipe(finalization->result_directory, &finalization->recipe,
                                                  bundle_digest, recursive_digest);
        if (error == BQ_OK)
        {
            Sha256 full;
            sha256_init(&full);
            sha256_add(&full, "BQ-RESULT-FULL-V1", 18);
            sha256_add(&full, bytes, used);
            sha256_add(&full, recursive_digest, 64);
            sha256_finish_hex(&full, (char8*)full_digest);
            memcpy(finalization->bundle_digest, bundle_digest, sizeof(finalization->bundle_digest));
        }
    }
    if (error == BQ_OK)
    {
        Sha256 digest;
        sha256_init(&digest);
        sha256_add(&digest, bytes, used);
        sha256_finish_hex(&digest, (char8*)result_digest);
        if (finalization->result_bound)
        {
            error = !memcmp(finalization->result_digest, result_digest, SHA256_HEX_CAPACITY) &&
                    !memcmp(finalization->bundle_digest, bundle_digest, SHA256_HEX_CAPACITY) &&
                    !memcmp(finalization->full_digest, full_digest, SHA256_HEX_CAPACITY) ? BQ_OK :
                    BQ_CONFIGURATION_MISMATCH;
        }
        else
        {
            memcpy(finalization->result_digest, result_digest, sizeof(finalization->result_digest));
            memcpy(finalization->full_digest, full_digest, sizeof(finalization->full_digest));
            finalization->result_bound = true;
        }
    }
    return error;
}

/* The bound digests of a validated result: the manifest's, the bundle's and
 * the full result's (BQ-RESULT-FULL-V1), recorded on first validation and
 * required equal afterwards. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_bind_digests(BqWorkerFinalization* finalization, char const* bytes,
                                                           u32 used, char const bundle_digest[SHA256_HEX_CAPACITY],
                                                           char const recursive_digest[SHA256_HEX_CAPACITY])
{
    char result_digest[SHA256_HEX_CAPACITY], full_digest[SHA256_HEX_CAPACITY];
    Sha256 full;
    sha256_init(&full);
    sha256_add(&full, "BQ-RESULT-FULL-V1", 18);
    sha256_add(&full, bytes, used);
    sha256_add(&full, recursive_digest, 64);
    sha256_finish_hex(&full, (char8*)full_digest);
    bq_digest(bytes, used, (char8*)result_digest);
    BqError error = BQ_OK;
    if (finalization->result_bound)
        error = !memcmp(finalization->result_digest, result_digest, SHA256_HEX_CAPACITY) &&
                !memcmp(finalization->bundle_digest, bundle_digest, SHA256_HEX_CAPACITY) &&
                !memcmp(finalization->full_digest, full_digest, SHA256_HEX_CAPACITY) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    else
    {
        memcpy(finalization->result_digest, result_digest, sizeof(finalization->result_digest));
        memcpy(finalization->bundle_digest, bundle_digest, sizeof(finalization->bundle_digest));
        memcpy(finalization->full_digest, full_digest, sizeof(finalization->full_digest));
        finalization->result_bound = true;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_result_control_read(int parent, char const* name, char* body, u32 capacity,
                                                       u32* length);

/* #881 PR 3: the retirement producer's manifest
 * (bq_retirement_worker_manifest_format) must be exactly the one the
 * coordinator formats from its own job, attempt, roots, the ready and
 * authority digests the channel carried, the digests of the execution
 * receipt, the sealed result and the binding it rehashes in the result root,
 * and the manifest's bundle digest, whose index must then validate against
 * the whole tree (bq_worker_bundle_validate_recipe). */
BUSTER_GLOBAL_LOCAL BqError bq_worker_result_validate_retirement(BqWorkerConfig const* config, BqJob const* job,
                                                                  BqWorkerFinalization* finalization)
{
    char bytes[BQ_WORKER_RESULT_CAP + 1], expected[BQ_WORKER_RESULT_CAP];
    char workspace_text[BQ_PATH_CAP + 1], bundle_digest[SHA256_HEX_CAPACITY] = {0};
    char recursive_digest[SHA256_HEX_CAPACITY] = {0}, receipt[SHA256_HEX_CAPACITY] = {0};
    char sealed[SHA256_HEX_CAPACITY] = {0}, binding[SHA256_HEX_CAPACITY] = {0};
    BqError error = bq_worker_result_sync_tree(finalization->result_directory);
    u32 used = 0;
    if (error == BQ_OK &&
        !(bq_worker_text(config->workspace_root, workspace_text, sizeof(workspace_text)) &&
          bq_worker_result_control_read(finalization->result_directory, finalization->recipe.manifest, bytes,
                                        BQ_WORKER_RESULT_CAP, &used) &&
          !memchr(bytes, 0, used)))
        error = BQ_CONFIGURATION_MISMATCH;
    if (error == BQ_OK) bytes[used] = 0;
    u64 size = 0;
    int length = error == BQ_OK &&
                 bq_worker_result_digest_line(bytes, "bundle-sha256=", bundle_digest) &&
                 bq_worker_bundle_file_digest(finalization->result_directory, TP_RETIREMENT_EXECUTION_RECEIPT_PATH,
                                              &size, receipt) &&
                 bq_worker_bundle_file_digest(finalization->result_directory, BQ_RETIREMENT_WORKER_SEALED_PATH, &size,
                                              sealed) &&
                 bq_worker_bundle_file_digest(finalization->result_directory, BQ_RETIREMENT_WORKER_BINDING_PATH, &size,
                                              binding) ?
                 bq_retirement_worker_manifest_format(expected, sizeof(expected), job->id, job->token,
                     string_from_pointer(workspace_text), finalization->result_root,
                     finalization->retirement_ready_sha256, finalization->retirement_authority_sha256, receipt, sealed,
                     binding, bundle_digest) : -1;
    if (error == BQ_OK && !(length > 0 && (u32)length == used && !memcmp(expected, bytes, used)))
        error = BQ_CONFIGURATION_MISMATCH;
    if (error == BQ_OK)
        error = bq_worker_bundle_validate_recipe(finalization->result_directory, &finalization->recipe, bundle_digest,
                                                 recursive_digest);
    if (error == BQ_OK) error = bq_worker_result_bind_digests(finalization, bytes, used, bundle_digest, recursive_digest);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_validate(BqWorkerConfig const* config, BqJob const* job,
                                                       BqWorkerFinalization* finalization)
{
    bool admitted = job && config && finalization && finalization->result_directory >= 0 &&
                    bq_worker_finalization_recipe(job, finalization);
    bool retirement = admitted && bq_request_recipe(&job->request) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED;
    BqError error = !admitted ? BQ_CONFIGURATION_MISMATCH :
                    retirement ? bq_worker_result_validate_retirement(config, job, finalization) :
                    bq_worker_result_validate_smoke(config, job, finalization);
    return error;
}

BUSTER_GLOBAL_LOCAL char const* bq_worker_outcome_name(BqOutcome outcome)
{
    char const* names[] = {"none", "succeeded", "failed", "cancelled", "interrupted"};
    char const* result = (u32)outcome < BUSTER_ARRAY_LENGTH(names) ? names[outcome] : "unknown";
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_evidence_matches(int parent, char const* name, char const* body, u64 length)
{
    int descriptor = parent >= 0 ? openat(parent, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    char bytes[BQ_WORKER_RESULT_CAP];
    struct stat expected = {0}, info = {0}, after = {0};
    bool ok = descriptor >= 0 && fstatat(parent, name, &expected, AT_SYMLINK_NOFOLLOW) == 0 &&
              fstat(descriptor, &info) == 0 && expected.st_dev == info.st_dev && expected.st_ino == info.st_ino &&
              S_ISREG(info.st_mode) && info.st_nlink == 1 && info.st_size == (off_t)length &&
              (info.st_uid == 0 || info.st_uid == geteuid()) && (info.st_mode & 0222) == 0;
    u64 used = 0;
    while (ok && used < length)
    {
        u32 chunk = length - used < sizeof(bytes) ? (u32)(length - used) : (u32)sizeof(bytes);
        ssize_t count = read(descriptor, bytes, chunk);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) ok = false;
        else
        {
            ok = !memcmp(bytes, body + used, (size_t)count);
            used += (u64)count;
        }
    }
    if (ok)
    {
        char extra = 0;
        ok = read(descriptor, &extra, 1) == 0 && fstat(descriptor, &after) == 0 &&
             fstatat(parent, name, &expected, AT_SYMLINK_NOFOLLOW) == 0 && after.st_dev == info.st_dev &&
             after.st_ino == info.st_ino && after.st_size == info.st_size && expected.st_dev == info.st_dev &&
             expected.st_ino == info.st_ino;
    }
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_result_control_read(int parent, char const* name, char* body,
                                                       u32 capacity, u32* length)
{
    int descriptor = parent >= 0 ? openat(parent, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat expected = {0}, info = {0}, after = {0};
    bool ok = descriptor >= 0 && body && capacity > 0 && fstatat(parent, name, &expected, AT_SYMLINK_NOFOLLOW) == 0 &&
              fstat(descriptor, &info) == 0 && expected.st_dev == info.st_dev && expected.st_ino == info.st_ino &&
              S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_uid == 0 || info.st_uid == geteuid()) &&
              (info.st_mode & 0222) == 0 && info.st_size > 0 && (u64)info.st_size <= capacity;
    u64 used = 0;
    while (ok && used < (u64)info.st_size)
    {
        ssize_t count = read(descriptor, body + used, (size_t)((u64)info.st_size - used));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) ok = false;
        else used += (u64)count;
    }
    if (ok)
    {
        char extra = 0;
        ok = read(descriptor, &extra, 1) == 0 && fstat(descriptor, &after) == 0 &&
             after.st_dev == info.st_dev && after.st_ino == info.st_ino && after.st_size == info.st_size;
    }
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    if (length) *length = ok ? (u32)used : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_entry_matches(int parent, char const* name, struct stat const* expected)
{
    struct stat actual = {0};
    bool ok = parent >= 0 && name && expected && fstatat(parent, name, &actual, AT_SYMLINK_NOFOLLOW) == 0 &&
              actual.st_dev == expected->st_dev && actual.st_ino == expected->st_ino && S_ISREG(actual.st_mode);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_unlink_if_same(int parent, char const* name, struct stat const* expected)
{
    bool ok = !name || !name[0] || bq_worker_entry_matches(parent, name, expected);
    if (ok && name && name[0]) ok = unlinkat(parent, name, 0) == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_control_publish(BqWorkerFinalization* finalization,
                                                               char const* name, char const* body,
                                                               u64 length, mode_t mode)
{
    int parent = -1;
    int descriptor = -1;
    char temporary[96] = {0};
    struct stat temporary_info = {0};
    bool temporary_ready = false;
    bool published = false;
    u64 capacity = finalization && name && finalization->recipe.bundle[0] &&
                   !strcmp(name, finalization->recipe.bundle) ? BQ_WORKER_BUNDLE_CAP : BQ_WORKER_RESULT_CAP;
    bool ok = finalization && name && body && length < capacity && finalization->result_directory >= 0;
    if (ok)
    {
        struct stat info = {0};
        ok = fstat(finalization->result_directory, &info) == 0 && S_ISDIR(info.st_mode) &&
             (u64)info.st_dev == finalization->result_device && (u64)info.st_ino == finalization->result_inode &&
             (parent = fcntl(finalization->result_directory, F_DUPFD_CLOEXEC, 3)) >= 0 &&
             bq_worker_temp_name(name, temporary, sizeof(temporary));
    }
    if (ok)
    {
        descriptor = openat(parent, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
        ok = descriptor >= 0;
    }
    for (u64 offset = 0; ok && offset < length;)
    {
        ssize_t wrote = write(descriptor, body + offset, (size_t)(length - offset));
        if (wrote < 0 && errno == EINTR) continue;
        ok = wrote > 0;
        if (ok) offset += (u64)wrote;
    }
    if (descriptor >= 0)
    {
        if (ok && fchmod(descriptor, mode) != 0) ok = false;
        if (ok && fsync(descriptor) != 0) ok = false;
        if (ok && fstat(descriptor, &temporary_info) != 0) ok = false;
        if (ok) ok = S_ISREG(temporary_info.st_mode) && temporary_info.st_nlink == 1 &&
                       temporary_info.st_size == (off_t)length && (temporary_info.st_mode & 0222) == 0 &&
                       bq_worker_entry_matches(parent, temporary, &temporary_info);
        temporary_ready = ok;
    }
    if (ok)
    {
        if (linkat(parent, temporary, parent, name, 0) == 0)
        {
            struct stat published_info = {0};
            ok = fstatat(parent, name, &published_info, AT_SYMLINK_NOFOLLOW) == 0 &&
                 published_info.st_dev == temporary_info.st_dev && published_info.st_ino == temporary_info.st_ino &&
                 S_ISREG(published_info.st_mode) && published_info.st_nlink == 2 &&
                 (published_info.st_mode & 0222) == 0;
            published = ok;
            if (ok) ok = bq_worker_unlink_if_same(parent, temporary, &temporary_info);
            if (ok)
            {
                struct stat final_info = {0};
                ok = fstatat(parent, name, &final_info, AT_SYMLINK_NOFOLLOW) == 0 &&
                     final_info.st_dev == temporary_info.st_dev && final_info.st_ino == temporary_info.st_ino &&
                     S_ISREG(final_info.st_mode) && final_info.st_nlink == 1;
            }
        }
        else if (errno == EEXIST)
        {
            published = bq_worker_evidence_matches(parent, name, body, length);
            if (published) ok = bq_worker_unlink_if_same(parent, temporary, &temporary_info);
            else ok = false;
        }
        else
        {
            ok = false;
        }
    }
    if (ok && published && fsync(parent) != 0) ok = false;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    if (parent >= 0 && temporary_ready && !published) bq_worker_unlink_if_same(parent, temporary, &temporary_info);
    if (parent >= 0 && close(parent) != 0) ok = false;
    return ok ? BQ_OK : BQ_IO;
}

/* Each request is a one-way state transition. Persist both the queue authority
 * and exported evidence, then acknowledge. A crash between either write or the
 * acknowledgement leaves an interrupted attempt, never a resumable sample set.
 * On a version-2 (retirement) channel the receipt and record also carry the
 * digest. RETIREMENT_READY's is kept for the replay at finalization. A
 * MEASURED is first handed off: the receipt authority it names, bound by its
 * context chain to the coordinator's own A digest, ready digest and row-plan
 * pin, is copied and journalled (bq_retirement_coordinator_handoff), and only
 * then are its record and receipt written, so a durable worker-phase-4 means
 * the handoff completed. A cancellation or an execution deadline reached
 * before or after the handoff withholds the acknowledgement. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_phase_accept(BqQueue* queue, BqPhaseChannel* channel,
                                                   unsigned char* message, BqWorkerFinalization* finalization)
{
    BqJob* job = channel ? bq_job(&queue->state, channel->job) : NULL;
    BqError error = job && job->token == channel->attempt && job->id == queue->state.active_id &&
                    !job->cancel_requested && !bq_worker_cancel_signal && bq_phase_check(channel, message) ?
                    BQ_OK : BQ_WORKER_MISMATCH;
    unsigned phase = error == BQ_OK ? (unsigned)bq_phase_get(message + 24) : 0;
    bool retirement = error == BQ_OK && channel->version == BQ_PHASE_VERSION_2;
    unsigned size = error == BQ_OK ? bq_phase_bytes(channel->version) : 0;
    BqPhase expected = phase <= BQ_PHASE_SETTLING || phase == BQ_PHASE_RETIREMENT_READY ? BQ_PREPARING :
                       phase == BQ_PHASE_MEASURING ? BQ_SETTLING : BQ_MEASURING;
    if (error == BQ_OK && job->phase != expected) error = BQ_WORKER_MISMATCH;
    char digest[SHA256_HEX_CAPACITY] = {0}, digest_line[96] = {0};
    if (error == BQ_OK && bq_phase_digest_carried(channel->version, phase))
    {
        bq_phase_digest_format(message + BQ_PHASE_DIGEST_OFFSET, digest);
        snprintf(digest_line, sizeof(digest_line), "digest-sha256=%s\n", digest);
    }
    if (error == BQ_OK && retirement && phase == BQ_PHASE_MEASURED)
    {
        error = bq_worker_cancel_signal ? BQ_WORKER_CANCEL_SIGNAL :
                bq_worker_finalization_expired(finalization) ? BQ_WORKER_TIMEOUT :
                bq_retirement_coordinator_handoff(finalization->result_directory,
                    finalization->config ? finalization->config->workspace_root : (String8){0}, queue->directory_fd,
                    job->id, job->token, finalization->retirement ? finalization->retirement->profile : (String8){0},
                    finalization->retirement_preparation_sha256, finalization->retirement_ready_sha256, digest);
        if (error == BQ_OK && bq_worker_finalization_expired(finalization)) error = BQ_WORKER_TIMEOUT;
        if (error == BQ_OK) memcpy(finalization->retirement_authority_sha256, digest, SHA256_HEX_CAPACITY);
    }
    char name[48], body[512], record[48];
    int length = -1;
    if (error == BQ_OK)
    {
        snprintf(name, sizeof(name), "worker-phase-%u", phase);
        bool named = bq_record_name(record, name, job->id);
        length = snprintf(body, sizeof(body),
            "schema=1\nprotocol=%s\njob-id=%" PRIu64 "\nattempt-token=%" PRIu64
            "\nrequest-sha256=%s\nphase=%u\nrecipe-request-monotonic-ns=%" PRIu64
            "\nsupervisor-observed-monotonic-ns=%" PRIu64 "\n%s",
            retirement ? "BQPHASE2" : "BQPHASE1", (uint64_t)job->id, (uint64_t)job->token, job->digest, phase,
            bq_phase_get(message + 32), bq_phase_clock(), digest_line);
        error = named && length > 0 && (u32)length < sizeof(body) ?
                bq_record_write(queue, record, (u8 const*)body, (u32)length, false) : BQ_IO;
    }
    if (error == BQ_OK)
        error = bq_worker_result_control_publish(finalization, name, body, (u32)length, 0400);
    if (error == BQ_OK && (phase == BQ_PHASE_SETTLING || phase == BQ_PHASE_MEASURING))
        error = bq_real_advance(queue, job, phase == BQ_PHASE_SETTLING ? BQ_SETTLING : BQ_MEASURING, BQ_NO_OUTCOME);
    if (error == BQ_OK && retirement && phase == BQ_PHASE_RETIREMENT_READY)
        memcpy(finalization->retirement_ready_sha256, digest, SHA256_HEX_CAPACITY);
    if (error == BQ_OK && bq_worker_cancel_signal) error = BQ_WORKER_CANCEL_SIGNAL;
    if (error == BQ_OK)
    {
        channel->sequence = phase;
        channel->last_time = bq_phase_get(message + 32);
        bq_phase_put(message + 40, 1);
        if (send(channel->descriptor, message, size, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)size) error = BQ_IO;
    }
    if (error != BQ_OK && channel) channel->failed = 1;
    return error;
}

/* The digest a durable version-2 phase record carries (recovery reloads the
 * RETIREMENT_READY and MEASURED digests from the queue, never from the unit):
 * a BQPHASE2 record of this job, attempt and phase ending in exactly one
 * `digest-sha256=<64 lowercase hex>` line. */
BUSTER_GLOBAL_LOCAL bool bq_worker_phase_record_digest(BqQueue* queue, BqJob const* job, unsigned phase,
                                                       char digest[SHA256_HEX_CAPACITY])
{
    char name[48], record[48], identity[128], body[513] = {0};
    u32 size = 0;
    snprintf(name, sizeof(name), "worker-phase-%u", phase);
    int identity_length = job ? snprintf(identity, sizeof(identity), "\nprotocol=BQPHASE2\njob-id=%" PRIu64
                                         "\nattempt-token=%" PRIu64 "\n", (uint64_t)job->id, (uint64_t)job->token) : -1;
    char phase_line[32];
    snprintf(phase_line, sizeof(phase_line), "\nphase=%u\n", phase);
    bool ok = queue && identity_length > 0 && (size_t)identity_length < sizeof(identity) &&
              bq_record_name(record, name, job->id) &&
              bq_record_read(queue, record, (u8*)body, sizeof(body) - 1, &size) == BQ_OK && size > 0 &&
              !memchr(body, 0, size) && strstr(body, identity) && strstr(body, phase_line);
    char const* line = ok ? strstr(body, "\ndigest-sha256=") : NULL;
    ok = line && strstr(line + 1, "\ndigest-sha256=") == NULL && (u64)(line - body) + 15u + 64u + 1u == size &&
         body[size - 1] == '\n' && bq_retirement_hex((String8){(char8*)line + 15, 64}, 64);
    if (ok) memcpy(digest, line + 15, 64);
    digest[ok ? 64 : 0] = 0;
    return ok;
}

/* The admitted worker sleeps in poll throughout measurement. No timer polling,
 * manager query, journal write, control request or reconnect runs while the
 * recipe holds the measuring phase. pidfd gives prompt death/deadline handling
 * without replacing whole-cgroup cleanup or the final manager identity checks. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_phases_validate(BqQueue* queue, BqJob const* job,
                                                       BqWorkerFinalization* finalization)
{
    BqError error = job && job->phase >= BQ_MEASURING ? BQ_OK : BQ_WORKER_MISMATCH;
    /* The channel's own sequence: RETIREMENT_READY's receipt too on BQPHASE2. */
    unsigned version = finalization->phase_version == BQ_PHASE_VERSION_2 ? BQ_PHASE_VERSION_2 : BQ_PHASE_VERSION_1;
    for (unsigned phase = bq_phase_next(version, 0); error == BQ_OK && phase; phase = bq_phase_next(version, phase))
    {
        char name[48], record[48], exported[512];
        u8 authoritative[512];
        u32 source_bytes = 0, exported_bytes = 0;
        snprintf(name, sizeof(name), "worker-phase-%u", phase);
        error = bq_record_name(record, name, job->id) ?
                bq_record_read(queue, record, authoritative, sizeof(authoritative), &source_bytes) : BQ_IO;
        if (error == BQ_OK && (!bq_worker_result_control_read(finalization->result_directory, name,
                                exported, sizeof(exported), &exported_bytes) || source_bytes != exported_bytes ||
                              memcmp(authoritative, exported, source_bytes))) error = BQ_WORKER_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_phase_join(BqQueue* queue, BqSystemdContext* context,
                                                 BqPhaseChannel* channel, int* status, u64 deadline,
                                                 BqWorkerFinalization* finalization)
{
    int process = -1;
#ifdef SYS_pidfd_open
    if (context->pid > 0) process = (int)syscall(SYS_pidfd_open, context->pid, 0);
#endif
    BqError error = process >= 0 ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    bool finished = false;
    bool channel_closed = false;
    while (error == BQ_OK && !finished && !bq_worker_cancel_signal)
    {
        /* Continue watching the channel after MEASURED. A duplicate or an
         * unsolicited packet during sealing must not disappear before exit. */
        struct pollfd waiting[] = {{channel_closed ? -1 : channel->descriptor, POLLIN, 0},
                                   {process, POLLIN, 0}};
        u32 remaining = bq_worker_remaining(deadline);
        int timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
        int ready = timeout ? poll(waiting, 2, timeout) : 0;
        if (ready < 0) error = errno == EINTR && bq_worker_cancel_signal ? BQ_WORKER_CANCEL_SIGNAL : BQ_IO;
        else if (!ready) error = BQ_WORKER_TIMEOUT;
        else
        {
            if (waiting[0].revents & POLLIN)
            {
                unsigned char message[BQ_PHASE_MESSAGE_CAP] = {0};
                if (channel->sequence == BQ_PHASE_MEASURED)
                {
                    unsigned char pending = 0;
                    ssize_t count = recv(channel->descriptor, &pending, 1, MSG_DONTWAIT | MSG_PEEK);
                    if (count == 0 && (waiting[0].revents & POLLHUP)) channel_closed = true;
                    else error = BQ_WORKER_MISMATCH;
                }
                else error = bq_phase_receive_sized(channel->descriptor, message, bq_phase_bytes(channel->version)) ?
                             bq_worker_phase_accept(queue, channel, message, finalization) : BQ_WORKER_MISMATCH;
            }
            else if (waiting[0].revents & (POLLERR | POLLNVAL)) error = BQ_WORKER_MISMATCH;
            else if (waiting[0].revents & POLLHUP)
            {
                if (channel->sequence != BQ_PHASE_MEASURED) error = BQ_WORKER_MISMATCH;
                else channel_closed = true;
            }
            if (error == BQ_OK && (waiting[1].revents & POLLIN))
            {
                pid_t waited = waitpid(context->pid, status, WNOHANG);
                if (waited == context->pid)
                {
                    context->pid = -1;
                    finished = true;
                    if (channel->sequence != BQ_PHASE_MEASURED) error = BQ_WORKER_MISMATCH;
                }
                else if (waited < 0 && errno != EINTR) error = BQ_IO;
            }
            else if (waiting[1].revents & (POLLERR | POLLHUP | POLLNVAL)) error = BQ_IO;
        }
    }
    if (bq_worker_cancel_signal) error = BQ_WORKER_CANCEL_SIGNAL;
    if (error == BQ_OK && finished)
    {
        /* A pidfd wake can win the race against a final queued packet.
         * Require EOF after the launcher exits, with no leftover writer. */
        unsigned char pending = 0;
        ssize_t count = recv(channel->descriptor, &pending, 1, MSG_DONTWAIT | MSG_PEEK);
        if (channel->sequence != BQ_PHASE_MEASURED || count != 0) error = BQ_WORKER_MISMATCH;
    }
    if (process >= 0 && close(process) != 0 && error == BQ_OK) error = BQ_IO;
    if (error != BQ_OK) channel->failed = 1;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_failure_bundle_publish(BqWorkerFinalization* finalization,
    char digest[SHA256_HEX_CAPACITY], char recursive_digest[SHA256_HEX_CAPACITY])
{
    BqError error = finalization && finalization->result_directory >= 0 && finalization->recipe.name[0] ?
                    BQ_OK : BQ_IO;
    BqWorkerBundleEntry* entries = NULL;
    BqWorkerBundleFrame* frames = NULL;
    char* body = NULL;
    if (error == BQ_OK)
    {
        entries = mmap(NULL, sizeof(*entries) * BQ_WORKER_BUNDLE_ENTRY_CAP,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        frames = mmap(NULL, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP,
                      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        body = mmap(NULL, BQ_WORKER_BUNDLE_CAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    bool ok = entries && entries != MAP_FAILED && frames && frames != MAP_FAILED && body && body != MAP_FAILED;
    int root = ok ? openat(finalization->result_directory, ".",
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* stream = root >= 0 ? fdopendir(root) : NULL;
    ok = ok && stream != NULL;
    if (!stream && root >= 0) close(root);
    u32 depth = 0;
    u32 entry_count = 0;
    u32 object_count = 0;
    u64 total = 0;
    if (ok)
    {
        memset(frames, 0, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP);
        frames[0].stream = stream;
        frames[0].depth = 1;
        depth = 1;
    }
    while (ok && depth)
    {
        BqWorkerBundleFrame* frame = frames + depth - 1;
        errno = 0;
        struct dirent* item = readdir(frame->stream);
        if (!item)
        {
            if (errno) ok = false;
            else
            {
                ok = fsync(dirfd(frame->stream)) == 0;
                closedir(frame->stream);
                frame->stream = NULL;
                depth -= 1;
            }
        }
        else if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
        {
        }
        else
        {
            char relative[BQ_WORKER_BUNDLE_PATH_CAP + 1];
            int length = bq_worker_bundle_relative(relative, frame->path, item->d_name);
            int parent = dirfd(frame->stream);
            struct stat child_info = {0};
            ok = length > 0 && (u32)length <= BQ_WORKER_BUNDLE_PATH_CAP &&
                 bq_worker_bundle_path_valid(relative) && parent >= 0 &&
                 fstatat(parent, item->d_name, &child_info, AT_SYMLINK_NOFOLLOW) == 0;
            bool control = ok && bq_worker_bundle_reserved(&finalization->recipe, relative);
            if (ok && control)
            {
                ok = S_ISREG(child_info.st_mode) && child_info.st_nlink == 1 &&
                     (child_info.st_uid == 0 || child_info.st_uid == geteuid()) &&
                     (child_info.st_mode & 0222) == 0;
            }
            else if (ok)
            {
                object_count += 1;
                ok = object_count <= BQ_WORKER_BUNDLE_ENTRY_CAP - BQ_WORKER_BUNDLE_CONTROL_ENTRIES;
            }
            if (ok && !control && S_ISDIR(child_info.st_mode))
            {
                ok = depth < BQ_WORKER_BUNDLE_DEPTH_CAP &&
                     (child_info.st_uid == 0 || child_info.st_uid == geteuid()) &&
                     (child_info.st_mode & 022) == 0;
                int child = ok ? openat(parent, item->d_name,
                                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
                DIR* child_stream = child >= 0 ? fdopendir(child) : NULL;
                struct stat child_opened = {0};
                ok = ok && child_stream != NULL && fstat(child, &child_opened) == 0 &&
                     child_opened.st_dev == child_info.st_dev && child_opened.st_ino == child_info.st_ino;
                if (!child_stream && child >= 0) close(child);
                if (ok)
                {
                    BqWorkerBundleFrame* next = frames + depth;
                    memset(next, 0, sizeof(*next));
                    next->stream = child_stream;
                    next->depth = frame->depth + 1;
                    memcpy(next->path, relative, (size_t)length + 1);
                    depth += 1;
                }
                else if (child_stream)
                {
                    closedir(child_stream);
                }
            }
            else if (ok && !control && S_ISREG(child_info.st_mode))
            {
                ok = entry_count < BQ_WORKER_BUNDLE_ENTRY_CAP && child_info.st_nlink == 1 &&
                     (child_info.st_uid == 0 || child_info.st_uid == geteuid()) &&
                     (child_info.st_mode & 022) == 0 && child_info.st_size >= 0 &&
                     (u64)child_info.st_size <= BQ_WORKER_BUNDLE_FILE_CAP &&
                     total <= bq_worker_bundle_total_cap(&finalization->recipe) - (u64)child_info.st_size;
                if (ok)
                {
                    int evidence = bq_worker_bundle_open_relative(finalization->result_directory, relative);
                    struct stat opened = {0};
                    ok = evidence >= 0 && fstat(evidence, &opened) == 0 &&
                         opened.st_dev == child_info.st_dev && opened.st_ino == child_info.st_ino &&
                         fsync(evidence) == 0;
                    if (evidence >= 0 && close(evidence) != 0) ok = false;
                }
                if (ok)
                {
                    BqWorkerBundleEntry* record = entries + entry_count;
                    memcpy(record->path, relative, (size_t)length + 1);
                    record->size = (u64)child_info.st_size;
                    record->seen = false;
                    ok = bq_worker_bundle_file_digest(finalization->result_directory, relative,
                                                      NULL, record->digest);
                    if (ok)
                    {
                        total += record->size;
                        entry_count += 1;
                    }
                }
            }
            else if (ok && !control)
            {
                ok = false;
            }
        }
    }
    while (depth)
    {
        if (frames[depth - 1].stream) closedir(frames[depth - 1].stream);
        depth -= 1;
    }
    for (u32 index = 1; ok && index < entry_count; index += 1)
    {
        BqWorkerBundleEntry record = entries[index];
        u32 slot = index;
        while (slot && bq_worker_bundle_entry_compare(entries[slot - 1].path, record.path) > 0)
        {
            entries[slot] = entries[slot - 1];
            slot -= 1;
        }
        entries[slot] = record;
    }
    u64 used = 0;
    if (ok)
    {
        int length = snprintf(body, BQ_WORKER_BUNDLE_CAP, "BQ-BUNDLE-V1\nentries=%u\nbytes=%" PRIu64 "\n",
                              entry_count, total);
        ok = length > 0 && (u64)length < BQ_WORKER_BUNDLE_CAP;
        used = ok ? (u64)length : 0;
    }
    for (u32 index = 0; ok && index < entry_count; index += 1)
    {
        int length = snprintf(body + used, BQ_WORKER_BUNDLE_CAP - used, "%.64s %" PRIu64 " %s\n",
                              entries[index].digest, entries[index].size, entries[index].path);
        ok = length > 0 && (u64)length < BQ_WORKER_BUNDLE_CAP - used;
        if (ok) used += (u64)length;
    }
    if (ok)
        error = bq_worker_result_control_publish(finalization, finalization->recipe.bundle, body, used, 0400);
    if (ok && error == BQ_OK)
    {
        bq_digest(body, (u32)used, digest);
        error = bq_worker_bundle_validate_recipe(finalization->result_directory, &finalization->recipe,
                                                  digest, recursive_digest);
    }
    if (entries && entries != MAP_FAILED)
        munmap(entries, sizeof(*entries) * BQ_WORKER_BUNDLE_ENTRY_CAP);
    if (frames && frames != MAP_FAILED)
        munmap(frames, sizeof(*frames) * BQ_WORKER_BUNDLE_DEPTH_CAP);
    if (body && body != MAP_FAILED) munmap(body, BQ_WORKER_BUNDLE_CAP);
    if (!ok && error == BQ_OK) error = BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL int bq_worker_result_evidence_body(BqJob const* job, BqOutcome outcome, BqError reason,
                                                       BqWorkerFinalization* finalization, char* body, u32 capacity)
{
    bool recipe = bq_worker_finalization_recipe(job, finalization);
    int body_length = recipe ? snprintf(body, capacity,
                                     "schema=1\nrecipe=%s\nstatus=%s\nerror=%s\n"
                                     "job-id=%" PRIu64 "\nattempt-token=%" PRIu64 "\n",
                                     finalization->recipe.name, bq_worker_outcome_name(outcome), bq_error_name(reason),
                                     (uint64_t)job->id, (uint64_t)job->token) : -1;
    return body_length > 0 && (u32)body_length < capacity ? body_length : -1;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_evidence(BqJob const* job, BqOutcome outcome, BqError reason,
                                                       BqWorkerFinalization* finalization)
{
    char body[BQ_WORKER_EVIDENCE_CAP];
    int body_length = bq_worker_result_evidence_body(job, outcome, reason, finalization, body, sizeof(body));
    bool ok = body_length > 0 &&
              bq_worker_result_control_publish(finalization, finalization->recipe.outcome, body,
                                                (u64)body_length, 0400) == BQ_OK;
    return ok ? BQ_OK : BQ_IO;
}

/* A failure path that could not finish may already have published the
 * immutable outcome record. Recovery adopts exactly those bytes, limited to
 * `required` when the outcome is already durable, and otherwise to the
 * outcomes the queue accepts for this job (a durable cancellation only
 * finishes cancelled). Reasons are limited to those a terminal record can
 * carry; failed and interrupted reasons must stay readable as failure records.
 * A missing record keeps the caller's default; any other existing record
 * fails closed and is never replaced. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_recover_outcome(BqJob const* job, BqWorkerFinalization* finalization,
                                                      BqOutcome required, BqOutcome* outcome, BqError* reason)
{
    bool recipe = job && finalization && finalization->result_directory >= 0 &&
                  bq_worker_finalization_recipe(job, finalization);
    struct stat entry = {0};
    errno = 0;
    int lookup = recipe ? fstatat(finalization->result_directory, finalization->recipe.outcome, &entry,
                                  AT_SYMLINK_NOFOLLOW) : -1;
    BqError error = !recipe ? BQ_CONFIGURATION_MISMATCH :
                    lookup != 0 && errno == ENOENT ? BQ_OK :
                    lookup != 0 ? BQ_IO : BQ_CORRUPT;
    BqOutcome outcomes[] = {BQ_FAILED, BQ_CANCELLED, BQ_INTERRUPTED};
    for (u32 index = 0; error == BQ_CORRUPT && index < BUSTER_ARRAY_LENGTH(outcomes); index += 1)
    {
        if (required != BQ_NO_OUTCOME ? outcomes[index] != required :
            (outcomes[index] == BQ_CANCELLED) != job->cancel_requested) continue;
        u32 last = outcomes[index] == BQ_CANCELLED ? (u32)BQ_WORKER_CANCEL_SIGNAL : (u32)BQ_BOOT_INTERRUPTED;
        for (u32 candidate = BQ_RECIPE_MISMATCH; error == BQ_CORRUPT && candidate <= last; candidate += 1)
        {
            char body[BQ_WORKER_EVIDENCE_CAP];
            int length = bq_worker_result_evidence_body(job, outcomes[index], (BqError)candidate, finalization,
                                                        body, sizeof(body));
            if (length > 0 && bq_worker_evidence_matches(finalization->result_directory,
                                                         finalization->recipe.outcome, body, (u64)length))
            {
                *outcome = outcomes[index];
                *reason = (BqError)candidate;
                error = BQ_OK;
            }
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_result_failure_artifacts(BqJob const* job, BqOutcome outcome,
                                                                BqError reason, BqWorkerFinalization* finalization)
{
    char manifest_body[BQ_WORKER_RESULT_CAP + 1] = {0};
    char bundle_digest[SHA256_HEX_CAPACITY] = {0};
    char recursive_digest[SHA256_HEX_CAPACITY] = {0};
    char manifest_digest[SHA256_HEX_CAPACITY] = {0};
    char full_digest[SHA256_HEX_CAPACITY] = {0};
    bool ok = job && finalization && finalization->result_directory >= 0 &&
              bq_worker_finalization_recipe(job, finalization);
    bool existing = false;
    int manifest_lookup = -1, bundle_lookup = -1;
    int manifest_errno = 0, bundle_errno = 0;
    struct stat manifest_entry = {0}, bundle_entry = {0};
    u32 manifest_length = 0;
    bool existing_success = false;
    if (ok)
    {
        manifest_lookup = fstatat(finalization->result_directory, finalization->recipe.manifest, &manifest_entry,
                                  AT_SYMLINK_NOFOLLOW);
        manifest_errno = errno;
        bundle_lookup = fstatat(finalization->result_directory, finalization->recipe.bundle, &bundle_entry,
                                AT_SYMLINK_NOFOLLOW);
        bundle_errno = errno;
        bool manifest_missing = manifest_lookup != 0 && manifest_errno == ENOENT;
        bool bundle_missing = bundle_lookup != 0 && bundle_errno == ENOENT;
        existing = manifest_lookup == 0;
        ok = (manifest_lookup == 0 || manifest_missing) && (bundle_lookup == 0 || bundle_missing) &&
             (!existing || bundle_lookup == 0);
    }
    if (ok && existing)
    {
        char job_line[64], token_line[64], result_line[BQ_PATH_CAP + 16];
        char recipe_line[BQ_RECIPE_NAME_CAP + 16];
        int job_length = snprintf(job_line, sizeof(job_line), "job-id=%" PRIu64, (uint64_t)job->id);
        int token_length = snprintf(token_line, sizeof(token_line), "attempt-token=%" PRIu64, (uint64_t)job->token);
        int result_length = snprintf(result_line, sizeof(result_line), "result-root=%s", finalization->result_root);
        int recipe_length = snprintf(recipe_line, sizeof(recipe_line), "recipe=%s", finalization->recipe.name);
        ok = bq_worker_result_control_read(finalization->result_directory, finalization->recipe.manifest,
                                            manifest_body, BQ_WORKER_RESULT_CAP, &manifest_length) &&
             job_length > 0 && (u32)job_length < sizeof(job_line) && token_length > 0 &&
             (u32)token_length < sizeof(token_line) && result_length > 0 &&
             (u32)result_length < sizeof(result_line) && recipe_length > 0 &&
             (u32)recipe_length < sizeof(recipe_line);
        if (ok)
        {
            manifest_body[manifest_length] = 0;
            existing_success = bq_worker_result_line(manifest_body, "status=succeeded");
            if (existing_success)
            {
                ok = finalization->config &&
                     bq_worker_result_validate(finalization->config, job, finalization) == BQ_OK;
            }
            bool stage = bq_worker_result_line(manifest_body, "stage=prepare") ||
                         bq_worker_result_line(manifest_body, "stage=base-generate") ||
                         bq_worker_result_line(manifest_body, "stage=base-build") ||
                         bq_worker_result_line(manifest_body, "stage=candidate-generate") ||
                         bq_worker_result_line(manifest_body, "stage=candidate-build") ||
                         bq_worker_result_line(manifest_body, "stage=throughput") ||
                         bq_worker_result_line(manifest_body, "stage=worker");
            bool status = bq_worker_result_line(manifest_body, "status=failed") ||
                          bq_worker_result_line(manifest_body, "status=cancelled") ||
                          bq_worker_result_line(manifest_body, "status=interrupted");
            bool process = bq_worker_result_nonterminal_line(manifest_body, "process-result=");
            ok = ok && (existing_success || (memchr(manifest_body, 0, manifest_length) == NULL &&
                 bq_worker_result_line(manifest_body, "schema=1") &&
                 bq_worker_result_line(manifest_body, recipe_line) &&
                 status && stage && process &&
                 bq_worker_result_line(manifest_body, job_line) && bq_worker_result_line(manifest_body, token_line) &&
                 bq_worker_result_line(manifest_body, result_line) &&
                 bq_worker_result_digest_line(manifest_body, "bundle-sha256=", bundle_digest)));
        }
        if (ok && !existing_success)
            ok = bq_worker_bundle_validate_recipe(finalization->result_directory, &finalization->recipe,
                                                   bundle_digest, recursive_digest) == BQ_OK;
        if (ok && !existing_success)
        {
            bq_digest(manifest_body, manifest_length, manifest_digest);
            Sha256 full;
            sha256_init(&full);
            sha256_add(&full, "BQ-RESULT-FULL-V1", 18);
            sha256_add(&full, manifest_body, manifest_length);
            sha256_add(&full, recursive_digest, 64);
            sha256_finish_hex(&full, (char8*)full_digest);
        }
        if (ok && !existing_success && finalization->result_bound)
            ok = !memcmp(finalization->result_digest, manifest_digest, sizeof(finalization->result_digest)) &&
                 !memcmp(finalization->bundle_digest, bundle_digest, sizeof(finalization->bundle_digest)) &&
                 !memcmp(finalization->full_digest, full_digest, sizeof(finalization->full_digest));
        if (ok && !existing_success && !finalization->result_bound)
        {
            memcpy(finalization->result_digest, manifest_digest, sizeof(finalization->result_digest));
            memcpy(finalization->bundle_digest, bundle_digest, sizeof(finalization->bundle_digest));
            memcpy(finalization->full_digest, full_digest, sizeof(finalization->full_digest));
            finalization->result_bound = true;
        }
    }
    if (ok && !existing)
    {
        int generated_manifest_length = -1;
        ok = !finalization->result_bound &&
             bq_worker_failure_bundle_publish(finalization, bundle_digest, recursive_digest) == BQ_OK;
        if (ok)
        {
            generated_manifest_length = snprintf(
                manifest_body, sizeof(manifest_body),
                "schema=1\nrecipe=%s\nstatus=%s\nstage=worker\nprocess-result=%s\n"
                "error=%s\njob-id=%" PRIu64 "\nattempt-token=%" PRIu64 "\nresult-root=%s\n"
                "bundle-sha256=%s\n",
                finalization->recipe.name, bq_worker_outcome_name(outcome), bq_error_name(reason),
                bq_error_name(reason),
                (uint64_t)job->id, (uint64_t)job->token, finalization->result_root, bundle_digest);
            ok = generated_manifest_length > 0 && (u32)generated_manifest_length < sizeof(manifest_body);
            if (ok) manifest_length = (u32)generated_manifest_length;
        }
        if (ok) ok = bq_worker_result_control_publish(finalization, finalization->recipe.manifest, manifest_body,
                                                       manifest_length, 0400) == BQ_OK;
        if (ok)
        {
            bq_digest(manifest_body, manifest_length, manifest_digest);
            Sha256 full;
            sha256_init(&full);
            sha256_add(&full, "BQ-RESULT-FULL-V1", 18);
            sha256_add(&full, manifest_body, manifest_length);
            sha256_add(&full, recursive_digest, 64);
            sha256_finish_hex(&full, (char8*)full_digest);
            memcpy(finalization->result_digest, manifest_digest, sizeof(finalization->result_digest));
            memcpy(finalization->bundle_digest, bundle_digest, sizeof(finalization->bundle_digest));
            memcpy(finalization->full_digest, full_digest, sizeof(finalization->full_digest));
            finalization->result_bound = true;
        }
    }
    return ok ? BQ_OK : BQ_IO;
}

/* Recovery's finalization starts without the digests the run held in memory.
 * Reload each missing one from the coordinator's own durable records: A from
 * the queue's preparation-<id> record, whose digest the export and the lease
 * handoff carried, and the ready and authority digests from the queue's
 * worker-phase-5 and worker-phase-4 records (a worker-phase-4 exists only
 * after a completed handoff). A missing or malformed record leaves the digest
 * empty, which fails the finalization closed. */
BUSTER_GLOBAL_LOCAL void bq_worker_retirement_reload(BqQueue* queue, BqJob const* job,
                                                     BqWorkerFinalization* finalization)
{
    char record[48];
    u8 bytes[BQ_RETIREMENT_PREPARATION_RECORD_CAP];
    u32 size = 0;
    if (!finalization->retirement_preparation_sha256[0] && bq_record_name(record, "preparation", job->id) &&
        bq_record_read(queue, record, bytes, sizeof(bytes), &size) == BQ_OK && size > 0)
        bq_digest(bytes, size, (char8*)finalization->retirement_preparation_sha256);
    if (!finalization->retirement_ready_sha256[0])
        bq_worker_phase_record_digest(queue, job, BQ_PHASE_RETIREMENT_READY, finalization->retirement_ready_sha256);
    if (!finalization->retirement_authority_sha256[0])
        bq_worker_phase_record_digest(queue, job, BQ_PHASE_MEASURED, finalization->retirement_authority_sha256);
}

BUSTER_GLOBAL_LOCAL char const* bq_worker_retirement_state_name(TpRetirementAuthorityState state)
{
    char const* const names[] = {"invalid", "absent", "incomplete", "damaged", "complete"};
    char const* name = (u32)state < BUSTER_ARRAY_LENGTH(names) ? names[state] : "invalid";
    return name;
}

/* #881 recovery L2, called by bq_worker_recover before bq_worker_finish and by
 * bq_worker_finish before any failed, cancelled or interrupted outcome (the
 * live run's failures, a failed MEASURED handoff among them). A retirement job
 * whose MEASURED handoff bq_retirement_coordinator_handoff_class finds
 * INCOMPLETE or INCONSISTENT, or that already carries a poison record
 * (queue.h, bq_retirement_poison_read), is held: the poison record is written
 * once, a job with no durable outcome gets its failure record unless it was
 * cancelled (reason: the caller's for an incomplete handoff, worker-mismatch
 * for an inconsistent or unreadable one; an existing record's class decides
 * after a crash between the two writes), and nothing is advanced, published
 * or removed. The attempt workspace, the result root and the queue-private
 * copy and journal stay as evidence, finalization->retirement_held is set,
 * and the caller does not finish, so the queue keeps needing reconciliation
 * and admits no job. A COMPLETE or ABSENT handoff, and every other recipe,
 * proceed as before. Any error also holds. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_retirement_handoff_hold(BqQueue* queue, BqJob const* job,
    BqWorkerFinalization* finalization, BqError reason, bool* held)
{
    bool retirement = queue && job && bq_request_recipe(&job->request) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED;
    bool inconsistent = false;
    bool poisoned = retirement && bq_retirement_poison_read(queue, job, &inconsistent) != BQ_NOT_FOUND;
    BqError error = BQ_OK;
    if (retirement && !poisoned)
    {
        char name[48], digest[SHA256_HEX_CAPACITY] = {0};
        u8 bytes[513];
        u32 size = 0;
        BqError record = bq_record_name(name, "worker-phase-4", job->id) ?
                         bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_IO;
        bool measured = record != BQ_NOT_FOUND;
        if (measured) bq_worker_phase_record_digest(queue, job, BQ_PHASE_MEASURED, digest);
        TpRetirementAuthorityState state = TP_RETIREMENT_AUTHORITY_INVALID;
        BqRetirementHandoffClass handoff = bq_retirement_coordinator_handoff_class(
            finalization ? finalization->result_directory : -1, queue->directory_fd, job->id, job->token, measured,
            digest, &state);
        inconsistent = handoff == BQ_RETIREMENT_HANDOFF_INCONSISTENT;
        poisoned = inconsistent || handoff == BQ_RETIREMENT_HANDOFF_INCOMPLETE;
        if (poisoned)
            error = bq_retirement_poison_write(queue, job, inconsistent, measured,
                                               bq_worker_retirement_state_name(state));
    }
    if (error == BQ_OK && poisoned && !job->cancel_requested && job->phase < BQ_FINALIZING)
    {
        BqError prior = bq_failure_evidence(queue, job);
        error = prior == BQ_NOT_FOUND ? bq_failure_write(queue, job, inconsistent ? BQ_WORKER_MISMATCH : reason) :
                prior == BQ_CORRUPT || prior == BQ_IO ? prior : BQ_OK;
    }
    *held = poisoned || (retirement && error != BQ_OK);
    if (finalization && *held) finalization->retirement_held = true;
    return error;
}

/* #881 PR 4: a retirement job succeeds only if the coordinator's replay of
 * its attempt workspace reproduces the ready record whose digest the
 * RETIREMENT_READY packet carried, the authority the MEASURED packet named is
 * still copied, journalled and bound to the coordinator's facts, and (#881
 * PR 3) that authority's plan and final context are the ones the coordinator
 * derives itself from the replay (bq_retirement_coordinator_finalize).
 * Missing digests are reloaded from the durable records first; a missing
 * digest or seam fails, and so does a poisoned job
 * (bq_retirement_poisoned, recovery L2). A cancellation or the execution deadline (backend
 * clock) before or after the replay stops it. Other recipes pass unchanged. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_retirement_finalize(BqQueue* queue, BqJob const* job,
                                                           BqWorkerFinalization* finalization)
{
    bool retirement = job && bq_request_recipe(&job->request) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED;
    if (retirement && finalization && queue) bq_worker_retirement_reload(queue, job, finalization);
    bool bound = finalization && finalization->config && finalization->retirement && queue &&
                 finalization->retirement_ready_sha256[0] && finalization->retirement_preparation_sha256[0] &&
                 finalization->retirement_authority_sha256[0];
    BqError error = !retirement ? BQ_OK : !bound || bq_retirement_poisoned(queue, job) ? BQ_WORKER_MISMATCH :
                    bq_worker_cancel_signal ? BQ_WORKER_CANCEL_SIGNAL :
                    bq_worker_finalization_expired(finalization) ? BQ_WORKER_TIMEOUT :
                    bq_retirement_coordinator_finalize(finalization->retirement, finalization->config->workspace_root,
                        finalization->result_directory, queue->directory_fd, job->id, job->token,
                        finalization->retirement_preparation_sha256, finalization->retirement_ready_sha256,
                        finalization->retirement_authority_sha256);
    if (retirement && error == BQ_OK)
        error = bq_worker_cancel_signal ? BQ_WORKER_CANCEL_SIGNAL :
                bq_worker_finalization_expired(finalization) ? BQ_WORKER_TIMEOUT : BQ_OK;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_finish(BqQueue* queue, BqWorkerConfig const* config, BqJob* job,
                                              BqOutcome outcome, BqError reason,
                                              BqWorkerFinalization* finalization)
{
    BqError error = outcome == BQ_SUCCEEDED && !finalization ? BQ_BAD_REQUEST : BQ_OK;
    BqError evidence = BQ_OK;
    bool production = config && config->production_path;
    if (error == BQ_OK && production && outcome == BQ_SUCCEEDED &&
        (!finalization || finalization->result_directory < 0 || finalization->result_root[0] == 0))
        error = BQ_CONFIGURATION_MISMATCH;
    if (error == BQ_OK && finalization && finalization->phases_required && outcome == BQ_SUCCEEDED)
        error = bq_worker_phases_validate(queue, job, finalization);
    /* A retirement success that is already durable (recovery re-finishing a
     * FINALIZING job) is never rewritten: a failed finalization holds it for
     * reconciliation instead of turning it into a failure. */
    bool held = false;
    if (error == BQ_OK && outcome == BQ_SUCCEEDED)
    {
        error = bq_worker_retirement_finalize(queue, job, finalization);
        held = error != BQ_OK && job->phase >= BQ_FINALIZING && job->outcome == BQ_SUCCEEDED;
    }
    if (error == BQ_OK && production && outcome == BQ_SUCCEEDED && bq_worker_finalization_expired(finalization))
        error = BQ_WORKER_TIMEOUT;
    if (error == BQ_OK && production && outcome == BQ_SUCCEEDED) error = bq_worker_result_validate(config, job, finalization);
    /* Result validation may sync and hash a large tree. It cannot consume the
     * remaining job budget and still permit a successful journal transition. */
    if (error == BQ_OK && production && outcome == BQ_SUCCEEDED && bq_worker_finalization_expired(finalization))
        error = BQ_WORKER_TIMEOUT;
    if (production && outcome == BQ_SUCCEEDED && error != BQ_OK && !held)
    {
        reason = error;
        outcome = BQ_FAILED;
        error = BQ_OK;
    }
    /* #881 recovery L2: a retirement job about to end failed, cancelled or
     * interrupted (a failed MEASURED handoff in the live run among them) is
     * classified first; an incomplete or inconsistent handoff is poisoned and
     * held with its workspace, never advanced, published or cleaned. */
    bool poisoned = false;
    if (error == BQ_OK && outcome != BQ_SUCCEEDED)
    {
        error = bq_worker_retirement_handoff_hold(queue, job, finalization, reason, &poisoned);
        if (poisoned && error == BQ_OK) error = BQ_RECONCILIATION_REQUIRED;
    }
    if (outcome == BQ_SUCCEEDED)
    {
        for (BqPhase phase = (BqPhase)(job->phase + 1); error == BQ_OK && phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
        {
            BqOutcome next_outcome = job->cancel_requested && phase >= BQ_CLEANING ? BQ_CANCELLED :
                                     phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME;
            error = bq_real_advance(queue, job, phase, next_outcome);
            job = bq_job(&queue->state, job->id);
            bq_worker_finish_checkpoint();
            if (error == BQ_OK) error = bq_worker_finish_cancel(queue, &job);
            if (error == BQ_OK && job->cancel_requested && job->phase < BQ_CLEANING)
            {
                error = bq_real_advance(queue, job, BQ_CLEANING, BQ_CANCELLED);
                job = bq_job(&queue->state, job->id);
                phase = BQ_CLEANING;
            }
        }
    }
    else if (!poisoned && error == BQ_OK)
    {
        BqError prior = bq_failure_evidence(queue, job);
        if (outcome != BQ_CANCELLED && prior == BQ_NOT_FOUND) error = bq_failure_write(queue, job, reason);
        else if (outcome != BQ_CANCELLED && (prior == BQ_CORRUPT || prior == BQ_IO)) error = prior;
        if (error == BQ_OK && job->phase < BQ_CLEANING) error = bq_real_advance(queue, job, BQ_CLEANING, outcome);
        if (production && job && job->result_bound)
        {
            evidence = bq_worker_result_binding_validate_seamed(job, finalization ? finalization->retirement : NULL);
        }
        else if (production && finalization && finalization->result_directory >= 0)
        {
            evidence = bq_worker_result_evidence(job, outcome, reason, finalization);
            if (evidence == BQ_OK && !job->result_bound)
                evidence = bq_worker_result_failure_artifacts(job, outcome, reason, finalization);
        }
    }
    if (error == BQ_OK && evidence != BQ_OK) error = evidence;
    if (error == BQ_OK && evidence == BQ_OK && production && outcome != BQ_SUCCEEDED && finalization &&
        finalization->result_bound && job && !job->result_bound)
    {
        error = bq_result_bind(queue, job, string_from_pointer(finalization->result_root), finalization->result_digest,
                               finalization->bundle_digest, finalization->full_digest);
        job = bq_job(&queue->state, job->id);
    }
    if (error == BQ_OK)
    {
        queue->needs_reconciliation = true;
        BqOutcome terminal = outcome == BQ_INTERRUPTED &&
                             (reason == BQ_BOOT_INTERRUPTED || job->phase < BQ_CLEANING) ? BQ_INTERRUPTED : BQ_NO_OUTCOME;
        error = outcome == BQ_SUCCEEDED ?
                bq_workspace_reconcile_controlled(queue, config->workspace_root, job->id, job->token,
                                                  terminal, bq_worker_before_terminal, finalization) :
                bq_workspace_reconcile_outcome(queue, config->workspace_root, job->id, job->token, terminal);
    }
    if (error != BQ_OK) queue->needs_reconciliation = true;
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_cgroup_absent(BqWorkerConfig const* config,
                                                  BqWorkerObserved const* identity)
{
    int root = identity ? bq_worker_open_trusted_directory(config->cgroup_root, false, false) : -1;
    int parent = root >= 0 ? openat(root, "buster.slice", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int slice = parent >= 0 ? openat(parent, "buster-bench.slice", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat root_info, slice_info, info;
    struct statfs filesystem;
    bool filesystem_ok = root >= 0 && parent >= 0 && slice >= 0 &&
                         bq_worker_directory_owner(parent, false, false) &&
                         bq_worker_directory_owner(slice, false, false) &&
                         fstat(root, &root_info) == 0 && fstat(slice, &slice_info) == 0 &&
                         identity->cgroup_root_device == (u64)root_info.st_dev &&
                         identity->cgroup_root_inode == (u64)root_info.st_ino &&
                         identity->cgroup_slice_device == (u64)slice_info.st_dev &&
                         identity->cgroup_slice_inode == (u64)slice_info.st_ino &&
                         (config->backend || (fstatfs(root, &filesystem) == 0 &&
                          (u64)filesystem.f_type == 0x63677270ull));
    if (filesystem_ok && config->production_path && !config->backend)
        filesystem_ok = fstatfs(slice, &filesystem) == 0 &&
                        (u64)filesystem.f_type == 0x63677270ull;
    errno = 0;
    bool absent = filesystem_ok && fstatat(slice, identity->unit, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT;
    if (slice >= 0) close(slice);
    if (parent >= 0) close(parent);
    if (root >= 0) close(root);
    return absent;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_wait_cgroup_absent(BqWorkerConfig const* config,
                                                          BqWorkerBackend* backend,
                                                          BqWorkerObserved const* identity, u64 deadline)
{
    BqError error = BQ_OK;
    bool absent = bq_worker_cgroup_absent(config, identity);
    u32 attempts = 0;
    while (error == BQ_OK && !absent && backend->clock(backend) < deadline &&
           attempts < BQ_WORKER_STOP_MILLISECONDS / BQ_WORKER_POLL_MILLISECONDS)
    {
        attempts += 1;
        u64 now = backend->clock(backend);
        u64 available = deadline - now;
        u32 delay = available < BQ_WORKER_POLL_MILLISECONDS ? (u32)available : BQ_WORKER_POLL_MILLISECONDS;
        error = delay ? backend->delay(backend, delay) : BQ_OK;
        if (error == BQ_OK) absent = bq_worker_cgroup_absent(config, identity);
    }
    if (error == BQ_OK && !absent) error = BQ_CLEANUP_FAILED;
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_instance_matches(BqWorkerConfig const* config,
                                                     BqWorkerObserved const* identity,
                                                     BqWorkerObserved* observed)
{
    bool live = bq_worker_observed(config, identity->boot_id, identity->unit, observed, false) &&
                !strcmp(identity->cgroup, observed->cgroup) &&
                identity->cgroup_device == observed->cgroup_device &&
                identity->cgroup_inode == observed->cgroup_inode &&
                identity->cgroup_root_device == observed->cgroup_root_device &&
                identity->cgroup_root_inode == observed->cgroup_root_inode &&
                identity->cgroup_slice_device == observed->cgroup_slice_device &&
                identity->cgroup_slice_inode == observed->cgroup_slice_inode;
    /* systemd clears ControlGroup after exit. The recorded invocation and
     * original slice identity still have to prove that its leaf is gone. */
    bool drained = observed->unit_found && !strcmp(identity->boot_id, observed->boot_id) &&
                   !strcmp(identity->unit, observed->unit) && !observed->active && !observed->populated &&
                   observed->result != BQ_WORKER_RUNNING &&
                   !strcmp(observed->collect_mode, "inactive") &&
                   (!observed->cgroup[0] || !strcmp(identity->cgroup, observed->cgroup)) &&
                   bq_worker_cgroup_absent(config, identity);
    return (live || drained) && !strcmp(identity->invocation_id, observed->invocation_id);
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_poll_empty(BqWorkerConfig const* config, BqWorkerBackend* backend,
                                                 BqWorkerObserved const* identity,
                                                 BqWorkerObserved* observed, u64 deadline)
{
    BqError error = BQ_OK;
    u32 attempts = 0;
    while (error == BQ_OK && (observed->active || observed->populated) &&
           backend->clock(backend) < deadline &&
           attempts < BQ_WORKER_STOP_MILLISECONDS / BQ_WORKER_POLL_MILLISECONDS)
    {
        attempts += 1;
        u64 now = backend->clock(backend);
        u64 available = deadline - now;
        u32 delay = available < BQ_WORKER_POLL_MILLISECONDS ? (u32)available : BQ_WORKER_POLL_MILLISECONDS;
        error = delay ? backend->delay(backend, delay) : BQ_OK;
        if (error == BQ_OK) error = backend->observe(backend, identity->unit, observed, deadline);
        if (error == BQ_OK && observed->unit_found && !bq_worker_instance_matches(config, identity, observed))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK && !observed->unit_found)
            error = bq_worker_wait_cgroup_absent(config, backend, identity, deadline);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_relation_has(char const* relations, char const* unit)
{
    size_t wanted = strlen(unit);
    char const* at = relations;
    bool found = false;
    while (at && *at && !found)
    {
        while (*at == ' ') at += 1;
        char const* end = strchr(at, ' ');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        found = length == wanted && !memcmp(at, unit, wanted);
        at = end ? end + 1 : NULL;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_child_unit(char output[BQ_WORKER_UNIT_CAP], char const* parent,
                                               char const* stage)
{
    size_t length = strlen(parent);
    size_t stage_length = strlen(stage);
    bool suffix = length > 8 && !strcmp(parent + length - 8, ".service");
    size_t stem_length = suffix ? length - 8 : 0;
    bool fits = suffix && stem_length + 1 + stage_length + 8 < BQ_WORKER_UNIT_CAP;
    if (fits)
    {
        memcpy(output, parent, stem_length);
        output[stem_length] = '-';
        memcpy(output + stem_length + 1, stage, stage_length);
        memcpy(output + stem_length + 1 + stage_length, ".service", 9);
    }
    return fits;
}

BUSTER_GLOBAL_LOCAL void bq_worker_child_expected(BqWorkerObserved* child,
                                                   BqWorkerObserved const* parent,
                                                   char const* unit)
{
    *child = (BqWorkerObserved){0};
    snprintf(child->boot_id, sizeof(child->boot_id), "%s", parent->boot_id);
    snprintf(child->unit, sizeof(child->unit), "%s", unit);
    snprintf(child->cgroup, sizeof(child->cgroup), BQ_WORKER_CGROUP_PREFIX "%s", unit);
    child->cgroup_root_device = parent->cgroup_root_device;
    child->cgroup_root_inode = parent->cgroup_root_inode;
    child->cgroup_slice_device = parent->cgroup_slice_device;
    child->cgroup_slice_inode = parent->cgroup_slice_inode;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_child_matches(BqWorkerConfig const* config,
                                                  BqWorkerObserved const* parent,
                                                  BqWorkerObserved const* identity,
                                                  BqWorkerObserved* observed)
{
    BqWorkerObserved expected;
    bq_worker_child_expected(&expected, parent, identity->unit);
    bool relation = bq_worker_relation_has(observed->part_of, parent->unit) &&
                    bq_worker_relation_has(observed->binds_to, parent->unit) &&
                    bq_worker_relation_has(observed->after, parent->unit);
    bool common = observed->unit_found && !strcmp(observed->boot_id, expected.boot_id) &&
                  !strcmp(observed->unit, expected.unit) &&
                  bq_worker_invocation_valid(observed->invocation_id) && relation &&
                  !strcmp(observed->collect_mode, "inactive-or-failed") &&
                  (!identity->invocation_id[0] || !strcmp(observed->invocation_id, identity->invocation_id));
    bool live = common && !strcmp(observed->cgroup, expected.cgroup) &&
                bq_worker_verify_cgroup(config, observed, false) &&
                (!identity->cgroup_device ||
                 (observed->cgroup_device == identity->cgroup_device &&
                  observed->cgroup_inode == identity->cgroup_inode)) &&
                observed->cgroup_root_device == parent->cgroup_root_device &&
                observed->cgroup_root_inode == parent->cgroup_root_inode &&
                observed->cgroup_slice_device == parent->cgroup_slice_device &&
                observed->cgroup_slice_inode == parent->cgroup_slice_inode;
    bool drained = common && !observed->active && !observed->populated &&
                   (!observed->cgroup[0] || !strcmp(observed->cgroup, expected.cgroup)) &&
                   bq_worker_cgroup_absent(config, &expected);
    return live || drained;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_child_poll_absent(BqWorkerConfig const* config,
                                                         BqWorkerBackend* backend,
                                                         BqWorkerObserved const* parent,
                                                         BqWorkerObserved const* identity,
                                                         BqWorkerObserved* observed,
                                                         u64 deadline)
{
    BqError error = BQ_OK;
    u32 attempts = 0;
    while (error == BQ_OK && observed->unit_found && backend->clock(backend) < deadline &&
           attempts < BQ_WORKER_STOP_MILLISECONDS / BQ_WORKER_POLL_MILLISECONDS)
    {
        attempts += 1;
        u64 now = backend->clock(backend);
        u64 available = deadline - now;
        u32 delay = available < BQ_WORKER_POLL_MILLISECONDS ? (u32)available : BQ_WORKER_POLL_MILLISECONDS;
        error = delay ? backend->delay(backend, delay) : BQ_OK;
        if (error == BQ_OK) error = backend->observe(backend, identity->unit, observed, deadline);
        if (error == BQ_OK && observed->unit_found &&
            !bq_worker_child_matches(config, parent, identity, observed)) error = BQ_WORKER_MISMATCH;
    }
    if (error == BQ_OK && !observed->unit_found)
    {
        BqWorkerObserved expected;
        bq_worker_child_expected(&expected, parent, identity->unit);
        error = bq_worker_wait_cgroup_absent(config, backend, &expected, deadline);
    }
    return error;
}

/* #881-C reverse lease handoff. A recovering coordinator (a restarted
 * service) holds no descriptor of its own: the live unit's references are the
 * only ones keeping the host lease. Acquiring only after the unit is empty
 * would leave a gap in which another coordinator could take the lease while
 * cleanup is still unproven. The keeper closes that gap: right after the unit
 * adopts the transferred lease (bq_worker_unit) it forks this child, which
 * keeps a reference to the same open file description for the unit's whole
 * life and hands it back over a peer-checked socket, bound to the lease path,
 * job and attempt and adopted only if the transferred reference is the one
 * holding the lock (bq_worker_lease_adopt). The recovering coordinator
 * therefore holds the lease before it signals the unit (bq_worker_stop).
 *
 * The socket is `<workspace>/results/.lease-return/<job>-<attempt>`
 * (BQ_SYSTEMD_LEASE_RETURN_DIRECTORY), outside the result root, so no recipe
 * bundle lists it, and in a directory every stage unit has in
 * InaccessiblePaths. Stage units running as the service identity therefore
 * cannot reach it, and the keeper also refuses any peer whose control group
 * lies under buster-bench.slice (bq_worker_keeper_peer_allowed), so no stage
 * or unit can receive the lease description and unlock it. The keeper listens
 * itself, so the coordinator's peer credentials name the keeper. systemd stops
 * the keeper with the unit (KillMode=control-group), PR_SET_PDEATHSIG stops it
 * when the unit process exits outside systemd, and a graceful stop unlinks the
 * socket by identity; a leftover one is purged after the unit is proven empty
 * (bq_worker_lease_keeper_purge). The keeper never acquires a lock itself.
 *
 * A keeper that died while its unit lives leaves a socket nobody answers
 * (ECONNREFUSED). That is not treated as "no keeper": the recipe still holds
 * the lease through its inherited reference, so signalling the unit would
 * reopen the gap. The job stays quarantined until the unit ends (its RuntimeMax
 * bounds that) or an operator stops it; the next recovery then finds it empty
 * and acquires (README.md, "lease keeper"). */
#define BQ_WORKER_SUN_PATH_CAP sizeof(((struct sockaddr_un*)0)->sun_path)

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_worker_keeper_stop_signal;

BUSTER_GLOBAL_LOCAL void bq_worker_keeper_stop_handler(int signal_number)
{
    (void)signal_number;
    bq_worker_keeper_stop_signal = 1;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_lease_keeper_path(String8 workspace_root, u64 job, u64 token,
                                                     char output[BQ_WORKER_SUN_PATH_CAP])
{
    char root[BQ_PATH_CAP + 1];
    bool ok = bq_worker_text(workspace_root, root, sizeof(root)) && root[0] == '/' && job && token;
    int length = ok ? snprintf(output, BQ_WORKER_SUN_PATH_CAP,
                               "%s/" BQ_SYSTEMD_LEASE_RETURN_DIRECTORY "/%" PRIu64 "-%" PRIu64,
                               root, (uint64_t)job, (uint64_t)token) : -1;
    ok = ok && length > 0 && (size_t)length < BQ_WORKER_SUN_PATH_CAP;
    if (!ok && output) output[0] = 0;
    return ok;
}

/* Unlink the keeper socket at `path` only while it is still the one this
 * process bound (device and inode); absent is success. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_keeper_unlink(char const* path, dev_t device, ino_t inode)
{
    struct stat info = {0};
    errno = 0;
    bool present = lstat(path, &info) == 0;
    bool ok = present ? S_ISSOCK(info.st_mode) && info.st_dev == device && info.st_ino == inode &&
                        unlink(path) == 0 :
                        errno == ENOENT;
    return ok;
}

/* Send one lease message carrying exactly one descriptor. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_send_descriptor(int client, BqWorkerLeaseMessage const* message_body,
                                                         int descriptor)
{
    char control[CMSG_SPACE(sizeof(descriptor))] = {0};
    struct iovec vector = {(void*)message_body, sizeof(*message_body)};
    struct msghdr message = {0};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    struct cmsghdr* header = CMSG_FIRSTHDR(&message);
    bool ok = header != NULL && descriptor >= 0;
    if (ok)
    {
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(descriptor));
        memcpy(CMSG_DATA(header), &descriptor, sizeof(descriptor));
        ok = sendmsg(client, &message, MSG_NOSIGNAL) == (ssize_t)sizeof(*message_body);
    }
    return ok;
}

/* Receive one lease message that carries exactly one descriptor (>= 3,
 * close-on-exec); any other ancillary shape closes what arrived. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_receive_descriptor(int client, BqWorkerLeaseMessage* message_body,
                                                            int* descriptor)
{
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec vector = {message_body, sizeof(*message_body)};
    struct msghdr message = {0};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    *descriptor = -1;
    ssize_t received = recvmsg(client, &message, MSG_CMSG_CLOEXEC | MSG_DONTWAIT);
    bool ok = received == (ssize_t)sizeof(*message_body) && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
    for (struct cmsghdr* header = received >= 0 ? CMSG_FIRSTHDR(&message) : NULL; header;
         header = CMSG_NXTHDR(&message, header))
    {
        bool rights = header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                      header->cmsg_len >= CMSG_LEN(0);
        size_t count = rights ? (header->cmsg_len - CMSG_LEN(0)) / sizeof(int) : 0;
        if (!rights || header->cmsg_len != CMSG_LEN(count * sizeof(int)) || count != 1) ok = false;
        for (size_t index = 0; index < count; ++index)
        {
            int arrived = -1;
            memcpy(&arrived, (u8*)CMSG_DATA(header) + index * sizeof(int), sizeof(arrived));
            if (arrived >= 3 && *descriptor < 0 && count == 1 && rights) *descriptor = arrived;
            else if (arrived >= 0)
            {
                close(arrived);
                ok = false;
            }
        }
    }
    ok = ok && *descriptor >= 3;
    if (!ok && *descriptor >= 0)
    {
        close(*descriptor);
        *descriptor = -1;
    }
    return ok;
}

/* The cgroup-v2 path ("0::" line) of a connected peer. With SO_PEERPIDFD the
 * peer's pidfd must still name the same live pid after /proc is read, so a
 * reused pid cannot stand in for the peer; without it, SO_PEERCRED alone. */
BUSTER_GLOBAL_LOCAL bool bq_worker_peer_cgroup(int descriptor, char output[BQ_WORKER_CGROUP_CAP])
{
    struct ucred peer = {0};
    socklen_t size = sizeof(peer);
    char path[64], text[BQ_WORKER_CGROUP_CAP * 4];
    bool ok = getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && size == sizeof(peer) &&
              peer.pid > 1 && snprintf(path, sizeof(path), "/proc/%ld/cgroup", (long)peer.pid) > 0 &&
              bq_worker_read_regular(path, text, sizeof(text));
    char const* line = ok ? text : NULL;
    char const* found = NULL;
    while (line && *line && !found)
    {
        char const* end = strchr(line, '\n');
        if (!strncmp(line, "0::", 3)) found = line + 3;
        line = end ? end + 1 : NULL;
    }
    char const* end = found ? strchr(found, '\n') : NULL;
    size_t length = found ? (end ? (size_t)(end - found) : strlen(found)) : 0;
    ok = ok && found && length > 0 && length < BQ_WORKER_CGROUP_CAP && found[0] == '/';
    if (ok)
    {
        memcpy(output, found, length);
        output[length] = 0;
    }
#ifdef SO_PEERPIDFD
    int pidfd = -1;
    socklen_t pidfd_size = sizeof(pidfd);
    bool pidfd_known = ok && getsockopt(descriptor, SOL_SOCKET, SO_PEERPIDFD, &pidfd, &pidfd_size) == 0 &&
                       pidfd_size == sizeof(pidfd) && pidfd >= 0;
    if (pidfd_known)
    {
        char info_path[64], info[1024];
        char const* field = snprintf(info_path, sizeof(info_path), "/proc/self/fdinfo/%d", pidfd) > 0 &&
                            bq_worker_read_regular(info_path, info, sizeof(info)) ? strstr(info, "\nPid:\t") : NULL;
        long current = field ? strtol(field + 6, NULL, 10) : -1;
        ok = current == (long)peer.pid;
    }
    if (pidfd >= 0) close(pidfd);
#endif
    if (!ok && output) output[0] = 0;
    return ok;
}

/* A reclaim peer may be any process of the service identity except one in
 * the benchmark slice: stage units and the unit itself run there. */
BUSTER_GLOBAL_LOCAL bool bq_worker_keeper_peer_allowed(char const* cgroup)
{
    size_t prefix = sizeof(BQ_WORKER_CGROUP_SLICE) - 1;
    bool inside = cgroup && !strncmp(cgroup, BQ_WORKER_CGROUP_SLICE, prefix) &&
                  (cgroup[prefix] == 0 || cgroup[prefix] == '/');
    bool ok = cgroup && cgroup[0] == '/' && !inside;
    return ok;
}

/* One reclaim on an accepted connection: the exact request from a same
 * credential peer outside the benchmark slice, the lease descriptor with its
 * identity, then the coordinator's acknowledgement. The keeper keeps its own
 * reference. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_keeper_transfer(int client, int lease_fd, char const* lease_path,
                                                         u64 job, u64 token)
{
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_WORKER_LEASE_HANDOFF_MILLISECONDS);
    struct stat info = {0};
    char cgroup[BQ_WORKER_CGROUP_CAP];
    BqWorkerLeaseMessage request = {0}, response = {0}, acknowledgement = {0};
    bool ok = bq_worker_lease_peer_matches(client) && bq_worker_peer_cgroup(client, cgroup) &&
              bq_worker_keeper_peer_allowed(cgroup) && bq_worker_lease_handoff_poll(client, POLLIN, deadline) &&
              recv(client, &request, sizeof(request), MSG_DONTWAIT) == (ssize_t)sizeof(request) &&
              bq_worker_lease_message_matches(&request, BQ_WORKER_LEASE_RECLAIM_REQUEST, lease_path, job, token,
                                              0, 0, "", 0) &&
              fstat(lease_fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              bq_worker_lease_message_make(&response, BQ_WORKER_LEASE_RECLAIM_RESPONSE, lease_path, job, token,
                                           (u64)info.st_dev, (u64)info.st_ino, "", 0) &&
              bq_worker_lease_send_descriptor(client, &response, lease_fd) &&
              bq_worker_lease_handoff_poll(client, POLLIN, deadline) &&
              recv(client, &acknowledgement, sizeof(acknowledgement), MSG_DONTWAIT) ==
                  (ssize_t)sizeof(acknowledgement) &&
              bq_worker_lease_message_matches(&acknowledgement, BQ_WORKER_LEASE_RECLAIM_ACK, lease_path, job, token,
                                              (u64)info.st_dev, (u64)info.st_ino, "", 0);
    return ok;
}

/* The keeper process body: listen (so peer credentials name the keeper),
 * report readiness, then serve reclaims until the unit's stop (SIGTERM/SIGINT,
 * or the parent-death signal). The stop signals stay blocked except inside
 * ppoll, so one arriving between the check and the wait still ends it.
 * Returns its exit status; the socket is unlinked by identity on the way out. */
BUSTER_GLOBAL_LOCAL int bq_worker_lease_keeper_serve(int listener, int ready, pid_t parent, int lease_fd,
                                                     char const* lease_path, char const* socket_path,
                                                     dev_t device, ino_t inode, u64 job, u64 token)
{
    struct sigaction stop = {0};
    stop.sa_handler = bq_worker_keeper_stop_handler;
    sigemptyset(&stop.sa_mask);
    sigset_t stops, waiting_mask;
    bq_worker_keeper_stop_signal = 0;
    bool ok = sigemptyset(&stops) == 0 && sigaddset(&stops, SIGTERM) == 0 && sigaddset(&stops, SIGINT) == 0 &&
              sigprocmask(SIG_BLOCK, &stops, &waiting_mask) == 0 &&
              sigdelset(&waiting_mask, SIGTERM) == 0 && sigdelset(&waiting_mask, SIGINT) == 0 &&
              sigaction(SIGTERM, &stop, NULL) == 0 && sigaction(SIGINT, &stop, NULL) == 0 &&
              prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 && getppid() == parent && listen(listener, 1) == 0;
    u8 state = ok ? 1 : 0;
    ok = write(ready, &state, sizeof(state)) == (ssize_t)sizeof(state) && ok;
    close(ready);
    while (ok && !bq_worker_keeper_stop_signal)
    {
        struct pollfd waiting = {listener, POLLIN, 0};
        int polled = ppoll(&waiting, 1, NULL, &waiting_mask);
        if (polled < 0 && errno != EINTR) ok = false;
        else if (polled > 0 && (waiting.revents & POLLIN))
        {
            int client = accept4(listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (client >= 0)
            {
                bq_worker_lease_keeper_transfer(client, lease_fd, lease_path, job, token);
                close(client);
            }
            else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) ok = false;
        }
        else if (polled > 0) ok = false;
    }
    close(listener);
    bool unlinked = bq_worker_lease_keeper_unlink(socket_path, device, inode);
    int status = (ok || bq_worker_keeper_stop_signal) && unlinked ? 0 : 1;
    return status;
}

/* The keeper directory (the socket path's parent): created private when
 * absent, and otherwise required to be a private directory of this identity,
 * reached without following a symlink. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_keeper_directory(char const* socket_path)
{
    char directory[BQ_WORKER_SUN_PATH_CAP];
    size_t length = socket_path ? strlen(socket_path) : 0;
    bool ok = length > 0 && length < sizeof(directory);
    if (ok) memcpy(directory, socket_path, length + 1);
    char* slash = ok ? strrchr(directory, '/') : NULL;
    ok = slash && slash != directory;
    if (ok) *slash = 0;
    if (ok && mkdir(directory, 0700) != 0 && errno != EEXIST) ok = false;
    int descriptor = ok ? open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = descriptor >= 0 && bq_worker_directory_owner(descriptor, true, false);
    if (descriptor >= 0) close(descriptor);
    return ok;
}

/* Stop a keeper this process started and reap it within the handoff bound. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_keeper_stop(pid_t keeper)
{
    int status = 0;
    bool ok = true;
    if (keeper > 0)
    {
        ok = kill(keeper, SIGTERM) == 0 &&
             bq_worker_waitpid_until(keeper, &status,
                 bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_WORKER_LEASE_HANDOFF_MILLISECONDS)) == BQ_OK &&
             WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (!ok && waitpid(keeper, &status, WNOHANG) == 0)
        {
            kill(keeper, SIGKILL);
            while (waitpid(keeper, &status, 0) < 0 && errno == EINTR) {}
        }
    }
    return ok;
}

/* Bind the keeper socket, fork the keeper holding `lease_fd`, and wait until
 * it reports that it serves. The keeper does not keep the phase channel.
 * Returns its pid, or -1 with nothing left behind. */
BUSTER_GLOBAL_LOCAL pid_t bq_worker_lease_keeper_start(char const* workspace_root, u64 job, u64 token,
                                                       char const* lease_path, int lease_fd, int phase_fd)
{
    char socket_path[BQ_WORKER_SUN_PATH_CAP];
    int ready[2] = {-1, -1};
    struct stat info = {0};
    bool ok = workspace_root && lease_path && lease_fd >= 3 &&
              bq_worker_lease_keeper_path(string_from_pointer(workspace_root), job, token, socket_path) &&
              bq_worker_lease_keeper_directory(socket_path) && pipe2(ready, O_CLOEXEC) == 0;
    int listener = ok ? socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0) : -1;
    bool bound = false;
    if (listener >= 0)
    {
        struct sockaddr_un address = {0};
        size_t length = strlen(socket_path);
        address.sun_family = AF_UNIX;
        memcpy(address.sun_path, socket_path, length + 1);
        mode_t prior_umask = umask(0077);
        bound = bind(listener, (struct sockaddr*)&address,
                     (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1)) == 0;
        umask(prior_umask);
    }
    ok = bound && lstat(socket_path, &info) == 0 && S_ISSOCK(info.st_mode) && info.st_uid == geteuid() &&
         (info.st_mode & 077) == 0;
    pid_t parent = getpid();
    pid_t keeper = ok ? fork() : -1;
    if (keeper == 0)
    {
        close(ready[0]);
        if (phase_fd >= 0) close(phase_fd);
        _exit(bq_worker_lease_keeper_serve(listener, ready[1], parent, lease_fd, lease_path, socket_path,
                                           info.st_dev, info.st_ino, job, token));
    }
    if (ready[1] >= 0) close(ready[1]);
    if (listener >= 0) close(listener);
    u8 state = 0;
    ok = keeper > 0 &&
         bq_worker_lease_handoff_poll(ready[0], POLLIN,
             bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_WORKER_LEASE_HANDOFF_MILLISECONDS)) &&
         read(ready[0], &state, sizeof(state)) == (ssize_t)sizeof(state) && state == 1;
    if (ready[0] >= 0) close(ready[0]);
    if (!ok && keeper > 0)
    {
        kill(keeper, SIGKILL);
        while (waitpid(keeper, NULL, 0) < 0 && errno == EINTR) {}
    }
    if (!ok && bound) bq_worker_lease_keeper_unlink(socket_path, info.st_dev, info.st_ino);
    return ok ? keeper : -1;
}

/* A peer's cgroup-v2 membership, read from /proc: the reclaim accepts only a
 * keeper inside the exact outer unit's validated control group. */
BUSTER_GLOBAL_LOCAL bool bq_worker_peer_cgroup_matches(int descriptor, char const* cgroup)
{
    char actual[BQ_WORKER_CGROUP_CAP];
    bool ok = cgroup && cgroup[0] == '/' && bq_worker_peer_cgroup(descriptor, actual) && !strcmp(actual, cgroup);
    return ok;
}

/* Connect a non-blocking socket to `path` by `deadline`. A full backlog
 * (EAGAIN) is retried until then; nothing waits past it. */
BUSTER_GLOBAL_LOCAL bool bq_worker_lease_connect(int client, char const* path, u64 deadline)
{
    struct sockaddr_un address = {0};
    size_t length = strlen(path);
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, length + 1);
    socklen_t address_size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1);
    bool connected = false, failed = false;
    while (!connected && !failed)
    {
        int result = connect(client, (struct sockaddr*)&address, address_size);
        if (result == 0) connected = true;
        else if (errno == EINPROGRESS)
        {
            int pending = 0;
            socklen_t size = sizeof(pending);
            connected = bq_worker_lease_handoff_poll(client, POLLOUT, deadline) &&
                        getsockopt(client, SOL_SOCKET, SO_ERROR, &pending, &size) == 0 && pending == 0;
            failed = !connected;
        }
        else if ((errno == EAGAIN || errno == EINTR) && bq_worker_monotonic_milliseconds() < deadline)
        {
            u64 next = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 10);
            failed = bq_worker_sleep_until(next < deadline ? next : deadline) != BQ_OK;
        }
        else failed = true;
    }
    return connected;
}

/* Reclaim the lease reference a live unit's keeper holds. BQ_OK leaves this
 * coordinator holding the same locked open file description. BQ_NOT_FOUND
 * means the unit bound no keeper socket at all (it has not adopted a lease,
 * or predates the keeper), so there is nothing to take back. Anything else is
 * BQ_CLEANUP_FAILED: the caller must not signal the unit, and the job stays
 * quarantined rather than racing a fresh flock. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_lease_reclaim(BqWorkerConfig const* config, BqWorkerObserved const* identity,
                                                    BqJob const* job, char const* lease_path, BqWorkerLease* lease)
{
    char socket_path[BQ_WORKER_SUN_PATH_CAP];
    struct stat info = {0};
    bool named = config && identity && job && lease && lease->descriptor < 0 && lease_path &&
                 bq_worker_lease_keeper_path(config->workspace_root, job->id, job->token, socket_path);
    errno = 0;
    bool present = named && lstat(socket_path, &info) == 0;
    bool absent = named && !present && errno == ENOENT;
    bool ok = present && S_ISSOCK(info.st_mode) && info.st_uid == geteuid() && (info.st_mode & 077) == 0;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_WORKER_LEASE_HANDOFF_MILLISECONDS);
    int client = ok ? socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0) : -1;
    ok = client >= 0 && bq_worker_lease_connect(client, socket_path, deadline) &&
         bq_worker_lease_peer_matches(client);
    if (ok && config->production_path && !config->backend)
        ok = bq_worker_peer_cgroup_matches(client, identity->cgroup);
    BqWorkerLeaseMessage request = {0}, response = {0}, acknowledgement = {0};
    ok = ok && bq_worker_lease_message_make(&request, BQ_WORKER_LEASE_RECLAIM_REQUEST, lease_path, job->id,
                                            job->token, 0, 0, "", 0) &&
         send(client, &request, sizeof(request), MSG_NOSIGNAL) == (ssize_t)sizeof(request) &&
         bq_worker_lease_handoff_poll(client, POLLIN, deadline);
    int received = -1;
    ok = ok && bq_worker_lease_receive_descriptor(client, &response, &received);
    struct stat transferred = {0};
    ok = ok && fstat(received, &transferred) == 0 && (u64)transferred.st_dev == response.device &&
         (u64)transferred.st_ino == response.inode &&
         bq_worker_lease_message_matches(&response, BQ_WORKER_LEASE_RECLAIM_RESPONSE, lease_path, job->id,
                                         job->token, response.device, response.inode, "", 0);
    BqWorkerLease adopted = {.descriptor = -1};
    /* adopt proves the path still names this inode, that the lock is held,
     * and that this reference is the holder: it then takes LOCK_EX on it. */
    if (ok) ok = bq_worker_lease_adopt(lease_path, received, &adopted) == 0;
    else if (received >= 0) close(received);
    received = -1;
    ok = ok && bq_worker_lease_message_make(&acknowledgement, BQ_WORKER_LEASE_RECLAIM_ACK, lease_path, job->id,
                                            job->token, response.device, response.inode, "", 0) &&
         send(client, &acknowledgement, sizeof(acknowledgement), MSG_NOSIGNAL) == (ssize_t)sizeof(acknowledgement);
    if (ok)
    {
        *lease = adopted;
        adopted.descriptor = -1;
    }
    bq_worker_lease_release(&adopted);
    if (client >= 0) close(client);
    BqError error = ok ? BQ_OK : absent ? BQ_NOT_FOUND : BQ_CLEANUP_FAILED;
    return error;
}

/* Remove a keeper socket left by a unit that is proven empty (a keeper killed
 * before its graceful unlink). Only a private socket owned by this identity
 * is removed; anything else at that name is a cleanup failure. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_lease_keeper_purge(BqWorkerConfig const* config, BqJob const* job)
{
    char socket_path[BQ_WORKER_SUN_PATH_CAP];
    struct stat info = {0};
    bool named = config && job && bq_worker_lease_keeper_path(config->workspace_root, job->id, job->token,
                                                              socket_path);
    errno = 0;
    bool present = named && lstat(socket_path, &info) == 0;
    bool ok = named && (present ? S_ISSOCK(info.st_mode) && info.st_uid == geteuid() &&
                                  (info.st_mode & 077) == 0 && unlink(socket_path) == 0 :
                                  errno == ENOENT);
    return ok ? BQ_OK : BQ_CLEANUP_FAILED;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_stop_children(BqWorkerConfig const* config,
                                                     BqWorkerBackend* backend,
                                                     BqWorkerObserved const* parent)
{
    char const* stages[] = {"base-generate", "base-build", "candidate-generate",
                            "candidate-build", "throughput"};
    BqError error = BQ_OK;
    for (u32 index = 0; error == BQ_OK && index < BUSTER_ARRAY_LENGTH(stages); index += 1)
    {
        char unit[BQ_WORKER_UNIT_CAP];
        BqWorkerObserved expected, observed = {0}, identity = {0};
        if (!bq_worker_child_unit(unit, parent->unit, stages[index])) error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK) bq_worker_child_expected(&expected, parent, unit);
        if (error == BQ_OK)
            error = backend->observe(backend, unit, &observed,
                                     bq_worker_deadline(backend->clock(backend),
                                                        BQ_WORKER_COMMAND_MILLISECONDS));
        if (error == BQ_OK && observed.unit_found)
        {
            if (!bq_worker_child_matches(config, parent, &expected, &observed)) error = BQ_WORKER_MISMATCH;
            else identity = observed;
        }
        else if (error == BQ_OK)
        {
            error = bq_worker_wait_cgroup_absent(config, backend, &expected,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS));
        }
        if (error == BQ_OK && observed.unit_found && (observed.active || observed.populated))
        {
            error = backend->signal(backend, unit, "TERM",
                                    bq_worker_deadline(backend->clock(backend),
                                                       BQ_WORKER_COMMAND_MILLISECONDS));
            u64 deadline = bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS);
            if (error == BQ_OK)
                error = bq_worker_child_poll_absent(config, backend, parent, &identity, &observed, deadline);
        }
        if (error == BQ_OK && observed.unit_found && !observed.active && !observed.populated)
        {
            u64 deadline = bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS);
            error = bq_worker_child_poll_absent(config, backend, parent, &identity, &observed, deadline);
        }
        if (error == BQ_OK && observed.unit_found && (observed.active || observed.populated))
        {
            error = backend->signal(backend, unit, "KILL",
                                    bq_worker_deadline(backend->clock(backend),
                                                       BQ_WORKER_COMMAND_MILLISECONDS));
            u64 deadline = bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS);
            if (error == BQ_OK)
                error = bq_worker_child_poll_absent(config, backend, parent, &identity, &observed, deadline);
        }
        if (error == BQ_OK && observed.unit_found) error = BQ_CLEANUP_FAILED;
    }
    return error;
}

/* Stop the exact outer unit and prove it, its stage units and the launcher
 * gone. A coordinator without its own lease reference (recovery after a
 * restart) first reclaims the live unit's reference from its keeper, so the
 * lease is held before any signal can close the unit's references; a keeper
 * socket that exists but cannot hand the lease back leaves the unit
 * unsignalled and the job quarantined. Only a unit that bound no keeper, or
 * one already empty (its references closed before this coordinator ran),
 * falls back to acquiring once empty, where contention is BQ_BUSY. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_stop(BqWorkerConfig const* config, BqWorkerBackend* backend,
                                            BqWorkerObserved const* identity, BqWorkerObserved* observed,
                                            BqJob const* job, char const* lease_path, BqWorkerLease* lease)
{
    BqError error = !identity || !backend->delay ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK && observed->unit_found && !bq_worker_instance_matches(config, identity, observed))
        error = BQ_WORKER_MISMATCH;
    if (error == BQ_OK && !observed->unit_found)
        error = bq_worker_wait_cgroup_absent(config, backend, identity,
            bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS));
    if (error == BQ_OK && lease->descriptor < 0 && (observed->active || observed->populated))
    {
        BqError reclaimed = bq_worker_lease_reclaim(config, identity, job, lease_path, lease);
        if (reclaimed != BQ_OK && reclaimed != BQ_NOT_FOUND) error = BQ_CLEANUP_FAILED;
    }
    if (error == BQ_OK && (observed->active || observed->populated))
    {
        error = backend->signal(backend, identity->unit, "TERM",
                                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        u64 deadline = bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS);
        if (error == BQ_OK) error = bq_worker_poll_empty(config, backend, identity, observed, deadline);
    }
    if (error == BQ_OK && (observed->active || observed->populated))
    {
        /* The fixed service property grants ten seconds to TERM.  KILL then gets
         * the same bounded observation window; neither wait trusts unit names. */
        error = backend->signal(backend, identity->unit, "KILL",
                                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        u64 deadline = bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS);
        if (error == BQ_OK) error = bq_worker_poll_empty(config, backend, identity, observed, deadline);
    }
    bool empty = observed->unit_found ? !observed->active && !observed->populated :
                 bq_worker_cgroup_absent(config, identity);
    int status = 0;
    if (error == BQ_OK && empty && lease->descriptor < 0 && bq_worker_lease_acquire(lease_path, lease) != 0)
        error = BQ_BUSY;
    if (error == BQ_OK && empty) error = bq_worker_stop_children(config, backend, identity);
    /* The outer unit is proven empty, so the launcher exits once the broker
     * reports its outcome. A cancellation that is still pending (this stop may
     * be serving it) must not abandon that bounded reap as cleanup failure. */
    if (error == BQ_OK && empty)
        error = backend->join(backend, &status,
                              bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS), false);
    if (error == BQ_OK && empty)
    {
        BqWorkerObserved final = {0};
        error = backend->observe(backend, identity->unit, &final,
                                 bq_worker_deadline(backend->clock(backend),
                                                    BQ_WORKER_COMMAND_MILLISECONDS));
        if (error == BQ_OK && final.unit_found)
        {
            if (!bq_worker_instance_matches(config, identity, &final)) error = BQ_WORKER_MISMATCH;
            else if (final.active || final.populated) error = BQ_CLEANUP_FAILED;
        }
        else if (error == BQ_OK)
        {
            error = bq_worker_wait_cgroup_absent(config, backend, identity,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS));
        }
    }
    if (error == BQ_OK && empty && job) error = bq_worker_lease_keeper_purge(config, job);
    BqError result = error == BQ_WORKER_MISMATCH ? error :
                     error == BQ_OK && empty ? BQ_OK : BQ_CLEANUP_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_recover(BqQueue* queue, BqWorkerConfig const* config,
                                               BqWorkerBackend* backend, char const* lease_path,
                                               char const* current_boot, BqJob* job,
                                               BqWorkerLease* lease,
                                               BqWorkerFinalization* finalization)
{
    char saved_boot[BQ_WORKER_BOOT_CAP], unit[BQ_WORKER_UNIT_CAP];
    /* The same recipe gate as the run: the retirement recipe only under the
     * seams' completeness gate. */
    BqError error = !job || !bq_worker_recipe_service(bq_request_recipe(&job->request), finalization) ?
                    BQ_WORKER_MISMATCH : bq_worker_record_read(queue, job, saved_boot, unit);
    BqWorkerObserved observed = {0};
    bool durable_outcome = job && job->phase >= BQ_FINALIZING && job->outcome != BQ_NO_OUTCOME;
    if (error == BQ_OK && strcmp(saved_boot, current_boot))
    {
        if (lease->descriptor < 0 && bq_worker_lease_acquire(lease_path, lease) != 0) error = BQ_BUSY;
        /* A keeper socket from the previous boot is only an inode now. */
        if (error == BQ_OK) error = bq_worker_lease_keeper_purge(config, job);
    }
    else if (error == BQ_OK)
    {
        BqWorkerObserved identity = {0};
        error = bq_worker_instance_read(queue, job, &identity);
        if (error == BQ_OK && (strcmp(identity.boot_id, saved_boot) || strcmp(identity.unit, unit)))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK) error = backend->observe(backend, unit, &observed,
            bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        if (error == BQ_OK && observed.unit_found && !bq_worker_instance_matches(config, &identity, &observed))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK && !observed.unit_found)
            error = bq_worker_wait_cgroup_absent(config, backend, &identity,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS));
        if (error == BQ_OK) error = bq_worker_stop(config, backend, &identity, &observed, job, lease_path, lease);
    }
    if (error == BQ_OK && config && config->production_path)
    {
        error = bq_worker_result_open(config, job, finalization,
                                      job->phase < BQ_FINALIZING && !job->result_bound);
        if (error == BQ_OK) error = bq_worker_lease_handoff_purge_stale(finalization->result_directory);
    }
    BqOutcome outcome = BQ_INTERRUPTED;
    BqError reason = BQ_WORKER_INTERRUPTED;
    if (error == BQ_OK)
    {
        /* The queue accepts only a cancelled terminal outcome for a job with a
         * durable cancellation request; interrupted is for the rest. */
        outcome = durable_outcome ? job->outcome : job->cancel_requested ? BQ_CANCELLED : BQ_INTERRUPTED;
        reason = durable_outcome ? BQ_NOT_FOUND :
                 strcmp(saved_boot, current_boot) ? BQ_BOOT_INTERRUPTED : BQ_WORKER_INTERRUPTED;
    }
    /* A durable non-success outcome whose result is still unbound may also
     * have its record published; only that exact outcome may be adopted. */
    bool adoptable = !durable_outcome || (job->outcome != BQ_SUCCEEDED && !job->result_bound);
    if (error == BQ_OK && adoptable && config && config->production_path)
        error = bq_worker_recover_outcome(job, finalization, durable_outcome ? job->outcome : BQ_NO_OUTCOME,
                                          &outcome, &reason);
    /* #881 recovery L2: a retirement handoff that is not complete or absent
     * is poisoned and held, never finished. */
    bool held = false;
    if (error == BQ_OK) error = bq_worker_retirement_handoff_hold(queue, job, finalization, reason, &held);
    if (held)
    {
        queue->needs_reconciliation = true;
        if (error == BQ_OK) error = BQ_RECONCILIATION_REQUIRED;
    }
    if (error == BQ_OK) error = bq_worker_finish(queue, config, job, outcome, reason, finalization);
    return error;
}

/* seams selects the retirement profile and roots of the coordinator's gates,
 * budget pin, BQPHASE2 channel and replay (retirement_coordinator.c).
 * bq_worker_run passes the installed seams, whose blocked profile
 * bq_retirement_profile_complete refuses, so a retirement job still fails
 * every coordinator gate in production. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_run_pinned(BqQueue* queue, BqWorkerConfig const* config, u64* id,
                                                  BqRetirementWorkerUnitSeams const* seams)
{
    sigset_t handoff_signals = {0};
    sigset_t prior_signals = {0};
    bool handoff_blocked = sigemptyset(&handoff_signals) == 0 &&
                           sigaddset(&handoff_signals, SIGTERM) == 0 &&
                           sigaddset(&handoff_signals, SIGINT) == 0 &&
                           sigprocmask(SIG_BLOCK, &handoff_signals, &prior_signals) == 0;
    bool handoff_failed = !handoff_blocked;
    sig_atomic_t transport_stop = bq_worker_transport_stop_signal;
    bq_worker_transport_stop_signal = 0;
    *id = queue->state.active_id;
    char lease_path[BQ_PATH_CAP + 1], boot_path[BQ_PATH_CAP + 1], queue_root[BQ_PATH_CAP + 1];
    char current_boot[BQ_WORKER_BOOT_CAP];
    BqWorkerBackend systemd;
    BqWorkerBackend* backend = config ? config->backend : NULL;
    bool production = config && config->production_path;
    if (!backend)
    {
        bq_worker_backend_systemd(&systemd);
        backend = &systemd;
    }
    BqError error = !config || !bq_worker_text(config->lease_file, lease_path, sizeof(lease_path)) || lease_path[0] != '/' ||
                    !bq_worker_text(config->boot_id_file, boot_path, sizeof(boot_path)) || boot_path[0] != '/' ||
                    (production && (!bq_worker_text(config->queue_root, queue_root, sizeof(queue_root)) || queue_root[0] != '/')) ||
                    config->limits.cpu >= CPU_SETSIZE || !config->limits.memory_max ||
                    !config->limits.tasks_max || !config->limits.runtime_max_usec ||
                    !backend->start || !backend->observe || !backend->signal || !backend->join ||
                    !backend->cleanup_launcher ||
                    !backend->delay || !backend->clock || !config->quarantine || !seams ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK && production && handoff_failed) error = BQ_IO;
    if (error == BQ_OK && (!bq_worker_read_regular(boot_path, current_boot, sizeof(current_boot)) ||
                           !bq_worker_boot_valid(current_boot))) error = BQ_CONFIGURATION_MISMATCH;
    struct sigaction cancel_action = {0}, old_term = {0}, old_interrupt = {0};
    bool term_handler = false;
    bool interrupt_handler = false;
    if (error == BQ_OK && production)
    {
        cancel_action.sa_handler = bq_worker_cancel_handler;
        sigemptyset(&cancel_action.sa_mask);
        bq_worker_cancel_signal = transport_stop ? 1 : 0;
        bq_worker_shutdown_signal = transport_stop ? 1 : 0;
        term_handler = sigaction(SIGTERM, &cancel_action, &old_term) == 0;
        interrupt_handler = term_handler && sigaction(SIGINT, &cancel_action, &old_interrupt) == 0;
        if (!interrupt_handler) error = BQ_IO;
    }
    if (handoff_blocked && (!production || interrupt_handler || error != BQ_OK))
    {
        if (sigprocmask(SIG_SETMASK, &prior_signals, NULL) != 0) error = BQ_IO;
        handoff_blocked = false;
    }
    if (transport_stop)
    {
        bq_worker_cancel_signal = 1;
        bq_worker_shutdown_signal = 1;
    }
    BqWorkerLease lease = {.descriptor = -1};
    BqWorkerFinalization finalization = {.config = config, .result_directory = -1, .retirement = seams};
    BqWorkerLeaseHandoff handoff = {.listener = -1, .parent = -1};
    int phase_descriptor = -1;
    BqPhaseChannel phases = {.descriptor = -1};
    bool launched = false;
    bool instance_bound = false;
    BqJob* job = *id ? bq_job(&queue->state, *id) : NULL;
    bool recovering = queue->needs_reconciliation;
    if (error == BQ_OK && !recovering && bq_worker_cancel_signal) error = BQ_WORKER_CANCEL_SIGNAL;
    if (error == BQ_OK && config->quarantine->descriptor >= 0)
    {
        int descriptor = config->quarantine->descriptor;
        if (!recovering || strcmp(config->quarantine->lease_path, lease_path)) error = BQ_BUSY;
        else
        {
            int duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
            if (duplicate < 0 || bq_worker_lease_adopt(lease_path, duplicate, &lease) != 0)
                error = BQ_CONFIGURATION_MISMATCH;
            else
            {
                close(config->quarantine->descriptor);
                config->quarantine->descriptor = -1;
            }
        }
    }
    if (error == BQ_OK && recovering)
        error = bq_worker_recover(queue, config, backend, lease_path, current_boot, job, &lease,
                                  &finalization);
    /* #881 P1: the job the materializer would reserve (the first queued one)
     * must pass the coordinator's request gate before the lease or any
     * reservation, so an admitted but incomplete profile leaves it queued
     * instead of reserving it and holding the lease for reconciliation. */
    bool head_found = false;
    for (u32 index = 0; error == BQ_OK && !recovering && !head_found && index < queue->state.job_count; index += 1)
    {
        BqJob const* queued = queue->state.jobs + index;
        head_found = queued->phase == BQ_QUEUED;
        if (head_found && !bq_retirement_request_valid_pinned(&queued->request, seams->profile)) error = BQ_BAD_REQUEST;
    }
    if (error == BQ_OK && !recovering && bq_worker_lease_acquire(lease_path, &lease) != 0) error = BQ_BUSY;
    u64 execution_deadline = 0;
    u64 lease_start = error == BQ_OK && !recovering ? backend->clock(backend) : 0;
    if (error == BQ_OK && !recovering &&
        !bq_worker_execution_deadline(lease_start, config->limits.runtime_max_usec,
                                      &execution_deadline)) error = BQ_CONFIGURATION_MISMATCH;
    if (error == BQ_OK && !recovering) finalization.execution_deadline = execution_deadline;
    if (error == BQ_OK && !recovering && bq_worker_cancel_signal) error = BQ_WORKER_CANCEL_SIGNAL;
    u64 token = 0;
    if (error == BQ_OK && !recovering) error = bq_materialize(queue, config->installed_root, config->workspace_root, id, &token);
    if (!recovering) job = error == BQ_OK ? bq_job(&queue->state, *id) : NULL;
    /* The coordinator's request gate: the queue's own checks, and the
     * retirement recipe only under the seams' completeness gate. */
    if (error == BQ_OK && !recovering && job && !bq_retirement_request_valid_pinned(&job->request, seams->profile))
        error = BQ_BAD_REQUEST;
    if (error == BQ_OK && !recovering && job) error = bq_worker_result_open(config, job, &finalization, true);
    /* #881 A1: once the reserved job is known, a retirement job replaces the
     * smoke hour with its authenticated budget ceiling, before preparation or
     * any launch. The deadline still counts from lease acquisition, and the
     * effective configuration carries the same limit to the outer unit's
     * RuntimeMaxUSec readback (bq_worker_observed) and its broker request. */
    BqWorkerConfig retirement_config;
    u64 job_runtime_usec = config ? config->limits.runtime_max_usec : 0;
    char budget_bytes[BQ_WORKER_BUDGET_BYTES];
    if (error == BQ_OK && !recovering && job &&
        bq_request_recipe(&job->request) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED)
    {
        /* #881 PR 4: production supplies no record, so the installed one is
         * loaded; either way its bytes must hash to the seams' pin. */
        String8 budget = config->retirement_budget;
        if (!budget.length)
            error = bq_retirement_coordinator_budget_load(config->installed_root, seams->profile, budget_bytes, &budget);
        if (error == BQ_OK) error = bq_worker_retirement_runtime(seams->profile, budget, &job_runtime_usec);
        if (error == BQ_OK && !bq_worker_execution_deadline(lease_start, job_runtime_usec, &execution_deadline))
            error = BQ_CONFIGURATION_MISMATCH;
        if (error == BQ_OK)
        {
            retirement_config = *config;
            retirement_config.limits.runtime_max_usec = job_runtime_usec;
            config = &retirement_config;
            finalization.config = config;
            finalization.execution_deadline = execution_deadline;
        }
    }
    char preparation_sha256[SHA256_HEX_CAPACITY] = {0};
    if (error == BQ_OK && !recovering && job &&
        string_equal(bq_field(&job->request, 2), S8("native-retirement-performance-v1")))
    {
        int installed = bq_open_absolute_directory(config->installed_root);
        int workspaces = bq_open_absolute_directory(config->workspace_root);
        error = installed >= 0 && workspaces >= 0 ?
                bq_retirement_preparation_ready(bq_retirement_queue_store(queue), job, installed, workspaces,
                                                preparation_sha256) :
                BQ_CONFIGURATION_MISMATCH;
        /* The worker unit cannot reach the queue; give it the exact verified
         * record and request. The recipe gate below still rejects this job
         * before any launch, so nothing consumes the export yet. */
        if (error == BQ_OK)
            error = bq_retirement_preparation_export(bq_retirement_queue_store(queue), job, workspaces,
                                                     preparation_sha256);
        /* The coordinator's own A digest, for the replay at finalization. */
        if (error == BQ_OK)
            memcpy(finalization.retirement_preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY);
        if (installed >= 0 && close(installed) != 0 && error == BQ_OK) error = BQ_CONFIGURATION_MISMATCH;
        if (workspaces >= 0 && close(workspaces) != 0 && error == BQ_OK) error = BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK && !recovering && job &&
        !bq_worker_preparation_matches_recipe(bq_request_recipe(&job->request), preparation_sha256))
        error = BQ_CONFIGURATION_MISMATCH;
    char unit[BQ_WORKER_UNIT_CAP];
    if (error == BQ_OK && !recovering && (!job || !bq_worker_unit_name(unit, job->id, job->token))) error = BQ_WORKER_MISMATCH;
    if (error == BQ_OK && !recovering) error = bq_worker_record_write(queue, job, current_boot, unit);
    if (error == BQ_OK && !recovering && production && bq_worker_cancel_signal)
    {
        error = bq_cancel(queue, job->id);
        if (error == BQ_OK) error = bq_worker_finish(queue, config, job, BQ_CANCELLED,
                                                     BQ_WORKER_CANCEL_SIGNAL, &finalization);
        recovering = true;
    }
    char job_id[32], attempt_token[32];
    char base_revision_text[65], candidate_revision_text[65];
    String8 base_revision = {0}, candidate_revision = {0};
    if (error == BQ_OK && !recovering)
    {
        int job_length = snprintf(job_id, sizeof(job_id), "%" PRIu64, (uint64_t)job->id);
        int token_length = snprintf(attempt_token, sizeof(attempt_token), "%" PRIu64, (uint64_t)job->token);
        base_revision = bq_field(&job->request, 3);
        candidate_revision = bq_field(&job->request, 4);
        bool canonical = !production ||
                         (string_equal(config->queue_root, S8("/var/lib/buster-bench/queue")) &&
                          string_equal(config->installed_root, S8("/opt/buster-bench/installed")) &&
                          string_equal(config->workspace_root, S8("/var/lib/buster-bench/workspaces")) &&
                          !strcmp(lease_path, "/var/lib/buster-bench/lease/host.lock"));
        if (!canonical || job_length <= 0 || (size_t)job_length >= sizeof(job_id) ||
            token_length <= 0 || (size_t)token_length >= sizeof(attempt_token) ||
            !bq_worker_recipe_launchable(&finalization) ||
            !bq_worker_text(base_revision, base_revision_text, sizeof(base_revision_text)) ||
            !bq_worker_text(candidate_revision, candidate_revision_text, sizeof(candidate_revision_text)))
        {
            error = BQ_WORKSPACE_MISMATCH;
        }
    }
    if (error == BQ_OK && !recovering && production && !config->backend)
        error = bq_worker_lease_handoff_open(finalization.result_root, finalization.result_directory,
                                             &handoff) ? BQ_OK : BQ_IO;
    if (error == BQ_OK && !recovering && backend->clock(backend) >= execution_deadline)
        error = BQ_WORKER_TIMEOUT;
    if (error == BQ_OK && !recovering)
    {
        char const* arguments[8];
        char runtime_text[32];
        u32 argument_count = 0;
        error = bq_worker_outer_arguments(bq_request_recipe(&job->request), job_runtime_usec, job_id,
                                          attempt_token, base_revision_text, candidate_revision_text,
                                          runtime_text, arguments, &argument_count) ?
                backend->start(backend, arguments, argument_count) : BQ_CONFIGURATION_MISMATCH;
        launched = error == BQ_OK;
    }
    if (error == BQ_OK && !recovering && handoff.listener >= 0)
    {
        error = bq_worker_lease_handoff_send(&handoff, lease.descriptor, lease_path, job->id, job->token,
                                              preparation_sha256, execution_deadline, &phase_descriptor);
        finalization.phases_required = true;
        /* The recipe fixes the version: BQPHASE2 carries retirement digests. */
        finalization.phase_version = bq_request_recipe(&job->request) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED ?
                                     BQ_PHASE_VERSION_2 : BQ_PHASE_VERSION_1;
        if (error == BQ_OK &&
            !bq_phase_init_version(&phases, phase_descriptor, job->id, job->token, finalization.phase_version))
            error = BQ_IO;
    }
    if (!bq_worker_lease_handoff_close(&handoff) && error == BQ_OK) error = BQ_IO;
    BqWorkerObserved observed = {0};
    BqWorkerObserved identity = {0};
    if (error == BQ_OK && !recovering) error = backend->observe(backend, unit, &observed,
        bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
    if (error == BQ_OK && !recovering)
    {
        bool exact_unit = bq_worker_observed(config, current_boot, unit, &observed, false);
        if (!exact_unit) error = BQ_WORKER_MISMATCH;
        else
        {
            identity = observed;
            error = bq_worker_instance_write(queue, job, &identity);
            instance_bound = error == BQ_OK;
        }
    }
    if (error == BQ_OK && !recovering && !bq_worker_observed(config, current_boot, unit, &observed, true)) error = BQ_RESOURCE_MISMATCH;
    if (error == BQ_OK && !recovering)
    {
        error = backend->observe(backend, unit, &observed,
                                 bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        if (error == BQ_OK && !bq_worker_instance_matches(config, &identity, &observed)) error = BQ_WORKER_MISMATCH;
    }
    if (error == BQ_OK && !recovering && backend->clock(backend) >= execution_deadline)
        error = BQ_WORKER_TIMEOUT;
    if (error == BQ_OK && !recovering && observed.result == BQ_WORKER_RUNNING)
        error = backend->signal(backend, unit, "CONT",
                                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
    int status = 0;
    if (error == BQ_OK && !recovering)
    {
        error = phase_descriptor >= 0 ? bq_worker_phase_join(queue, backend->context, &phases, &status,
                                                              execution_deadline, &finalization) :
                                        backend->join(backend, &status, execution_deadline, true);
        if (error == BQ_OK && backend->clock(backend) >= execution_deadline) error = BQ_WORKER_TIMEOUT;
    }
    bool interrupted_join = error == BQ_WORKER_CANCEL_SIGNAL;
    bool signal_cancelled = interrupted_join || (production && bq_worker_cancel_signal);
    if (signal_cancelled && job)
    {
        BqError cancel = bq_cancel(queue, job->id);
        error = cancel == BQ_OK ? bq_worker_stop(config, backend, &identity, &observed, job, lease_path, &lease) : cancel;
        bq_worker_cancel_signal = 0;
        if (error == BQ_OK) error = bq_worker_finish(queue, config, job, BQ_CANCELLED,
                                                     BQ_WORKER_CANCEL_SIGNAL, &finalization);
    }
    else if (signal_cancelled)
    {
        error = BQ_WORKER_CANCEL_SIGNAL;
        bq_worker_cancel_signal = 0;
    }
    if (error == BQ_OK && !recovering && !signal_cancelled) error = backend->observe(backend, unit, &observed,
        bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
    if (error == BQ_OK && !recovering && !signal_cancelled)
    {
        if (observed.unit_found && !bq_worker_instance_matches(config, &identity, &observed))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK && !observed.unit_found)
        {
            error = bq_worker_wait_cgroup_absent(config, backend, &identity,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS));
            /* CollectMode=inactive permits successful outer units to disappear,
             * but retains failures and their exact Result. A missing failed
             * invocation cannot be classified from the launcher status. */
            if (error == BQ_OK && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))
                error = BQ_WORKER_MISMATCH;
            if (error == BQ_OK) observed.result = BQ_WORKER_SUCCEEDED;
        }
    }
    if (error == BQ_OK && !recovering && !signal_cancelled)
        error = bq_worker_stop(config, backend, &identity, &observed, job, lease_path, &lease);
    if (error == BQ_OK && !recovering && !signal_cancelled && bq_worker_cancel_signal)
    {
        job = bq_job(&queue->state, *id);
        BqError cancel = job ? bq_cancel(queue, job->id) : BQ_WORKER_MISMATCH;
        signal_cancelled = true;
        bq_worker_cancel_signal = 0;
        error = cancel == BQ_OK ? bq_worker_finish(queue, config, job, BQ_CANCELLED,
                                                    BQ_WORKER_CANCEL_SIGNAL, &finalization) : cancel;
    }
    if (error == BQ_OK && !recovering && !signal_cancelled)
    {
        job = bq_job(&queue->state, *id);
        bool expired = backend->clock(backend) >= execution_deadline;
        BqOutcome outcome = job->cancel_requested || observed.result == BQ_WORKER_CANCELLED_RESULT ? BQ_CANCELLED :
                            !expired && observed.result == BQ_WORKER_SUCCEEDED ? BQ_SUCCEEDED : BQ_FAILED;
        BqError reason = expired ? BQ_WORKER_TIMEOUT : observed.result == BQ_WORKER_OOM ? BQ_WORKER_OOM_FAILURE :
                         observed.result == BQ_WORKER_TIMED_OUT ? BQ_WORKER_TIMEOUT : BQ_WORKER_FAILED;
        error = bq_worker_finish(queue, config, job, outcome, reason, &finalization);
    }
    else if (error == BQ_WORKER_TIMEOUT && !recovering && !launched && job &&
             finalization.result_directory >= 0)
    {
        BqError finished = bq_worker_finish(queue, config, job, BQ_FAILED,
                                           BQ_WORKER_TIMEOUT, &finalization);
        if (finished != BQ_OK) error = finished;
    }
    else if (!recovering && job && queue->state.active_id == job->id)
    {
        BqError reason = error;
        if (launched && !instance_bound)
        {
            BqWorkerObserved candidate = {0};
            BqError inspected = backend->observe(backend, unit, &candidate,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
            if (inspected == BQ_OK && bq_worker_observed(config, current_boot, unit, &candidate, false) &&
                bq_worker_instance_write(queue, job, &candidate) == BQ_OK)
            {
                identity = candidate;
                observed = candidate;
                instance_bound = true;
            }
            else
            {
                error = inspected == BQ_OK ? BQ_WORKER_MISMATCH : BQ_CLEANUP_FAILED;
            }
            if (!instance_bound && backend->cleanup_launcher(
                    backend, bq_worker_deadline(backend->clock(backend),
                                                BQ_WORKER_COMMAND_MILLISECONDS)) != BQ_OK)
                error = BQ_CLEANUP_FAILED;
        }
        if (launched && instance_bound && reason != BQ_CLEANUP_FAILED)
        {
            BqWorkerObserved candidate = {0};
            BqError inspected = backend->observe(backend, unit, &candidate,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
            BqError stopped = inspected == BQ_OK && bq_worker_instance_matches(config, &identity, &candidate) ?
                              bq_worker_stop(config, backend, &identity, &candidate, job, lease_path, &lease) :
                              inspected == BQ_OK ? BQ_WORKER_MISMATCH : BQ_CLEANUP_FAILED;
            if (stopped == BQ_OK)
            {
                BqError finished = bq_worker_finish(queue, config, job, BQ_FAILED,
                    reason == BQ_RESOURCE_MISMATCH ? BQ_RESOURCE_MISMATCH :
                    reason == BQ_WORKER_TIMEOUT ? BQ_WORKER_TIMEOUT : BQ_WORKER_FAILED,
                    &finalization);
                if (finished != BQ_OK) error = finished;
            }
            else
            {
                error = stopped;
            }
        }
        /* A job bq_worker_retirement_handoff_hold held keeps its evidence as
         * it was: nothing more is published or bound for it. */
        if (queue->state.active_id == job->id && !finalization.retirement_held)
        {
            BqError evidence_reason = error == BQ_CLEANUP_FAILED ? BQ_CLEANUP_FAILED :
                                      error == BQ_RESOURCE_MISMATCH ? BQ_RESOURCE_MISMATCH : BQ_WORKER_MISMATCH;
            if (production && job->result_bound)
            {
                BqError retained = bq_worker_result_binding_validate(job);
                if (retained != BQ_OK && error == BQ_OK) error = retained;
            }
            else if (production && finalization.result_directory >= 0)
            {
                /* Follow the journal, not the signal: a CANCEL that did not
                 * become durable must not be published as cancelled. */
                BqOutcome retained_outcome = job->cancel_requested ? BQ_CANCELLED : BQ_FAILED;
                BqError retained_reason = job->cancel_requested && signal_cancelled ? BQ_WORKER_CANCEL_SIGNAL :
                                          evidence_reason;
                BqError retained = bq_worker_result_evidence(job, retained_outcome, retained_reason, &finalization);
                if (retained == BQ_OK && !job->result_bound)
                    retained = bq_worker_result_failure_artifacts(job, retained_outcome, retained_reason, &finalization);
                if (retained == BQ_OK && finalization.result_bound && !job->result_bound)
                    retained = bq_result_bind(queue, job, string_from_pointer(finalization.result_root),
                                              finalization.result_digest, finalization.bundle_digest,
                                              finalization.full_digest);
                if (retained != BQ_OK && error == BQ_OK) error = retained;
            }
            BqError prior = bq_failure_evidence(queue, job);
            BqError evidence = prior == BQ_NOT_FOUND ? bq_failure_write(queue, job, evidence_reason) :
                               prior == BQ_CORRUPT || prior == BQ_IO ? prior : BQ_OK;
            if (evidence != BQ_OK) error = evidence;
            queue->needs_reconciliation = true;
        }
    }
    if (interrupt_handler && sigaction(SIGINT, &old_interrupt, NULL) != 0) error = BQ_IO;
    if (term_handler)
    {
        if (sigaction(SIGTERM, &old_term, NULL) != 0) error = BQ_IO;
    }
    if (bq_worker_finalization_restore(&finalization) != BQ_OK) error = BQ_IO;
    if (config && config->quarantine && queue->state.active_id && lease.descriptor >= 0 &&
        config->quarantine->descriptor < 0)
    {
        config->quarantine->descriptor = lease.descriptor;
        snprintf(config->quarantine->lease_path, sizeof(config->quarantine->lease_path), "%s", lease_path);
        lease.descriptor = -1;
    }
    if (!bq_worker_lease_handoff_close(&handoff) && error == BQ_OK) error = BQ_IO;
    bq_worker_lease_release(&lease);
    if (phase_descriptor >= 0 && close(phase_descriptor) != 0 && error == BQ_OK) error = BQ_IO;
    if (finalization.result_directory >= 0) close(finalization.result_directory);
    if (handoff_blocked && sigprocmask(SIG_SETMASK, &prior_signals, NULL) != 0) error = BQ_IO;
    return error;
}

BqError bq_worker_run(BqQueue* queue, BqWorkerConfig const* config, u64* id)
{
    BqRetirementWorkerUnitSeams seams = bq_retirement_worker_unit_installed();
    BqError error = bq_worker_run_pinned(queue, config, id, &seams);
    return error;
}

/* The producer's wait bound (retirement_worker_unit.c) uses the worker's stop
 * budget. */
BUSTER_CT_CHECK(BQ_RETIREMENT_WORKER_UNIT_STOP_NS == (u64)BQ_WORKER_STOP_MILLISECONDS * 1000000ull);
/* The coordinator's budget loader (retirement_coordinator.c) reads into a
 * buffer of the runtime parser's size. */
BUSTER_CT_CHECK(BQ_RETIREMENT_COORDINATOR_BUDGET_CAP == BQ_WORKER_BUDGET_BYTES);

/* seams selects the retirement producer's profile and roots: the installed
 * ones in production (bq_retirement_worker_unit_installed), whose blocked
 * profile bq_retirement_profile_complete refuses before the lease handoff. */
BUSTER_GLOBAL_LOCAL BqError bq_worker_unit_pinned(String8 lease_file, String8 job_id, String8 attempt_token,
                                                   String8 recipe_name, String8 workspace_root,
                                                   String8 base_revision, String8 candidate_revision,
                                                   String8 result_root, BqRetirementWorkerUnitSeams const* seams)
{
    char path[BQ_PATH_CAP + 1];
    char job_id_text[32], attempt_token_text[32], recipe_text[BQ_RECIPE_NAME_CAP + 1];
    char workspace_text[BQ_PATH_CAP + 1], base_text[65], candidate_text[65], result_text[BQ_PATH_CAP + 1];
    BqRecipe recipe = bq_recipe_from_name(recipe_name);
    /* Retirement runs in a forked producer (retirement_worker_unit.c), and
     * only with a complete profile, whatever the queue's predicate admits;
     * smoke keeps the recipe exec. */
    bool retirement = recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED && bq_worker_recipe_admitted(recipe, seams);
    BqRecipeFiles files = {0};
    BqWorkerLease lease = {.descriptor = -1};
    int phase_descriptor = -1;
    u64 execution_deadline_ns = 0, job_number = 0, token_number = 0;
    char preparation_sha256[SHA256_HEX_CAPACITY] = {0};
    BqError error = !bq_worker_text(lease_file, path, sizeof(path)) || path[0] != '/' ||
                    !bq_worker_text(job_id, job_id_text, sizeof(job_id_text)) || !bq_worker_text(attempt_token, attempt_token_text, sizeof(attempt_token_text)) ||
                    !bq_worker_text(recipe_name, recipe_text, sizeof(recipe_text)) ||
                    (recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED ? !retirement :
                     !bq_recipe_admitted(recipe) || !bq_worker_recipe_admitted(recipe, seams)) ||
                    !bq_recipe_files(recipe, &files) || !files.command[0] || strcmp(recipe_text, files.name) ||
                    !bq_worker_text(workspace_root, workspace_text, sizeof(workspace_text)) || workspace_text[0] != '/' ||
                    !bq_worker_text(base_revision, base_text, sizeof(base_text)) || !bq_worker_text(candidate_revision, candidate_text, sizeof(candidate_text)) ||
                    !bq_worker_text(result_root, result_text, sizeof(result_text)) || result_text[0] != '/' ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK && bq_worker_lease_handoff_receive(lease_file, result_root, job_id, attempt_token,
                                                         recipe, &lease, &phase_descriptor,
                                                         preparation_sha256, &execution_deadline_ns) != BQ_OK)
        error = BQ_CONFIGURATION_MISMATCH;
    /* From here until the unit stops, a keeper holds the same lease reference
     * for a recovering coordinator to reclaim (bq_worker_lease_reclaim). No
     * keeper, no pause and no recipe. */
    pid_t keeper = -1;
    if (error == BQ_OK)
    {
        IntegerParsingU64 job_value = string8_parse_u64_decimal(job_id);
        IntegerParsingU64 token_value = string8_parse_u64_decimal(attempt_token);
        job_number = job_value.status == INTEGER_PARSING_SUCCESS ? job_value.value : 0;
        token_number = token_value.status == INTEGER_PARSING_SUCCESS ? token_value.value : 0;
        keeper = job_value.status == INTEGER_PARSING_SUCCESS && token_value.status == INTEGER_PARSING_SUCCESS ?
                 bq_worker_lease_keeper_start(workspace_text, job_value.value, token_value.value, path,
                                              lease.descriptor, phase_descriptor) : -1;
        if (keeper <= 0) error = BQ_IO;
    }
    /* Only the exec'd recipe inherits the lease and the phase channel; the
     * retirement producer is forked, so both stay close-on-exec. */
    if (error == BQ_OK && !retirement)
    {
        int flags = fcntl(lease.descriptor, F_GETFD);
        if (flags < 0 || fcntl(lease.descriptor, F_SETFD, flags & ~FD_CLOEXEC) != 0)
        {
            error = BQ_IO;
        }
    }
    if (error == BQ_OK && !retirement)
    {
        int flags = fcntl(phase_descriptor, F_GETFD);
        if (flags < 0 || fcntl(phase_descriptor, F_SETFD, flags & ~FD_CLOEXEC) != 0) error = BQ_IO;
    }
    if (error == BQ_OK)
    {
        raise(SIGSTOP);
        char phase_text[32];
        char deadline_text[32];
        snprintf(phase_text, sizeof(phase_text), "%d", phase_descriptor);
        snprintf(deadline_text, sizeof(deadline_text), "%" PRIu64, (uint64_t)execution_deadline_ns);
        char const* arguments[] = {BQ_RECIPE_EXECUTABLE, files.command, job_id_text, attempt_token_text,
                                   workspace_text, base_text, candidate_text, result_text, NULL, NULL, NULL, NULL};
        /* The private retirement build importer receives the authenticated A
         * record identity before the phase channel. Smoke keeps its six-value
         * build-driver interface. */
        arguments[8] = recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED ? preparation_sha256 : phase_text;
        if (recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) arguments[9] = phase_text;
        if (recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) arguments[10] = deadline_text;
        u64 now_ns = bq_phase_clock();
        bool live = now_ns && now_ns < execution_deadline_ns;
        bool lease_matches = live && bq_worker_lease_recheck_for_exec(path, &lease);
        bool produced = false;
        if (live && lease_matches && bq_phase_clock() < execution_deadline_ns)
        {
#ifdef BUSTER_BENCH_SERVICE_TEST
            bq_worker_test_recipe_exec_count += 1;
#endif
            if (retirement)
            {
                error = bq_retirement_worker_unit_run(seams, job_number, token_number, workspace_text, result_text,
                                                      lease.descriptor, &phase_descriptor, preparation_sha256,
                                                      execution_deadline_ns);
                produced = true;
            }
            else
            {
                execv(BQ_RECIPE_EXECUTABLE, (char* const*)arguments);
            }
        }
        if (!produced)
            error = !now_ns || bq_phase_clock() >= execution_deadline_ns ?
                    BQ_WORKER_TIMEOUT : BQ_CONFIGURATION_MISMATCH;
    }
    if (!bq_worker_lease_keeper_stop(keeper) && error == BQ_OK) error = BQ_IO;
    bq_worker_lease_release(&lease);
    if (phase_descriptor >= 0) close(phase_descriptor);
    return error;
}

BqError bq_worker_unit(String8 lease_file, String8 job_id, String8 attempt_token, String8 recipe_name,
                       String8 workspace_root, String8 base_revision, String8 candidate_revision,
                       String8 result_root)
{
    BqRetirementWorkerUnitSeams seams = bq_retirement_worker_unit_installed();
    BqError error = bq_worker_unit_pinned(lease_file, job_id, attempt_token, recipe_name, workspace_root,
                                          base_revision, candidate_revision, result_root, &seams);
    return error;
}

#else

void bq_worker_backend_systemd(BqWorkerBackend* backend)
{
    *backend = (BqWorkerBackend){0};
}

BqError bq_worker_result_binding_validate(BqJob const* job)
{
    (void)job;
    return BQ_UNSUPPORTED;
}

BqError bq_worker_run(BqQueue* queue, BqWorkerConfig const* config, u64* id)
{
    (void)queue;
    (void)config;
    *id = 0;
    return BQ_UNSUPPORTED;
}

BqError bq_worker_unit(String8 lease_file, String8 job_id, String8 attempt_token, String8 recipe,
                       String8 workspace_root, String8 base_revision, String8 candidate_revision,
                       String8 result_root)
{
    (void)lease_file;
    (void)job_id;
    (void)attempt_token;
    (void)recipe;
    (void)workspace_root;
    (void)base_revision;
    (void)candidate_revision;
    (void)result_root;
    return BQ_UNSUPPORTED;
}

#endif
