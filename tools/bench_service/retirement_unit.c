/* Lane-B caller inputs inside the service worker-unit (#1020, #881).
 *
 * Ownership: the unit side of the A handoff. worker-unit runs in the outer
 * transient unit as buster-bench with the installed tree read-only and the
 * queue and lease inaccessible, so it cannot read the queue's
 * preparation-<job> record. The coordinator's
 * bq_retirement_preparation_export (retirement_prepare.c) copies that record
 * and the canonical request into job-<id>-attempt-<token>/retirement/; this
 * file consumes that BqRetirementStore.
 *
 * Entry points:
 *   bq_retirement_unit_store_open   open the attempt's export directory
 *   bq_retirement_unit_prepare      re-import A, verify the toolchain and
 *                                   import the pinned reference policy
 *   bq_retirement_unit_release      close the policy's held descriptors
 *   bq_retirement_unit_build        PREPARING ack, then both matched builds
 *                                   through the broker seam, evidence sealed
 *                                   in retirement-build/, re-imported and
 *                                   both binaries held
 *   bq_retirement_unit_built_release
 *
 * Map: bq_retirement_unit_store_closed checks the sealed export's exact
 * contents; bq_retirement_unit_prepare_pinned is the profile seam that the
 * public wrapper calls with the compiled profile and fixed toolchain root.
 * bq_retirement_unit_evidence_closed checks the evidence directory's exact
 * closure; bq_retirement_unit_stage runs one stage under the cancellation
 * descriptor and deadline; bq_retirement_unit_build_pinned and
 * bq_retirement_unit_build_import_pinned are the profile/driver/broker seams.
 *
 * The retirement recipe stays unadmitted and bq_worker_unit calls neither
 * prepare nor build yet. Their results are input for the later B producer,
 * not a correctness verdict.
 */
#include "retirement_unit.h"

BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_file(int directory, char const* name)
{
    struct stat info = {0};
    bool ok = fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
              info.st_nlink == 1 && info.st_uid == geteuid() && (info.st_mode & 07777) == 0400;
    return ok;
}

/* The store must be this attempt's own sealed export directory and hold only
 * the record and the request. A replaced, planted or writable entry fails. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_store_closed(BqRetirementStore store, int workspaces, BqJob const* job)
{
    char attempt[64], record_name[48], request_name[48];
    struct stat held = {0}, named = {0};
    bool ok = bq_workspace_name(attempt, job->id, job->token) &&
              bq_record_name(record_name, "preparation", job->id) &&
              bq_record_name(request_name, "request", job->id) &&
              fstat(store.directory, &held) == 0 && S_ISDIR(held.st_mode) && held.st_uid == geteuid() &&
              (held.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE;
    int workspace = ok ? openat(workspaces, attempt, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && workspace >= 0 && bq_workspace_seal(workspace, job, false) &&
         fstatat(workspace, BQ_RETIREMENT_EXPORT_DIRECTORY, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISDIR(named.st_mode) && named.st_dev == held.st_dev && named.st_ino == held.st_ino;
    if (workspace >= 0 && close(workspace) != 0) ok = false;
    /* A fresh description, so the listing never moves the caller's cursor. */
    int listing = ok ? openat(store.directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* stream = listing >= 0 ? fdopendir(listing) : NULL;
    if (!stream && listing >= 0) close(listing);
    ok = ok && stream != NULL;
    u32 found = 0;
    bool more = ok;
    while (ok && more)
    {
        errno = 0;
        struct dirent* entry = readdir(stream);
        more = entry != NULL;
        if (!more) ok = errno == 0;
        else if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            u32 bit = !strcmp(entry->d_name, record_name) ? 1u : !strcmp(entry->d_name, request_name) ? 2u : 0u;
            ok = bit && !(found & bit) && bq_retirement_unit_file(store.directory, entry->d_name);
            found |= bit;
        }
    }
    if (stream && closedir(stream) != 0) ok = false;
    return ok && found == 3u;
}

BqError bq_retirement_unit_store_open(int workspaces, u64 job_id, u64 attempt_token, BqRetirementStore* store)
{
    char attempt[64];
    if (store) store->directory = -1;
    int workspace = store && workspaces >= 0 && job_id && attempt_token &&
                    bq_workspace_name(attempt, job_id, attempt_token) ?
                    openat(workspaces, attempt, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int directory = workspace >= 0 ? openat(workspace, BQ_RETIREMENT_EXPORT_DIRECTORY,
                                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool ok = directory >= 0;
    if (workspace >= 0 && close(workspace) != 0) ok = false;
    if (ok) store->directory = directory;
    else if (directory >= 0) close(directory);
    BqError result = ok ? BQ_OK : BQ_WORKSPACE_MISMATCH;
    return result;
}

bool bq_retirement_unit_release(BqRetirementUnitPrepared* prepared)
{
    bool ok = prepared != NULL;
    if (prepared && prepared->owned) ok = bq_retirement_reference_policy_release(&prepared->policy);
    if (prepared) *prepared = (BqRetirementUnitPrepared){.policy = {.clang = -1, .inventory = -1}};
    return ok;
}

/* The request bytes are bound by the record's request= digest, which the
 * import compares with the canonical record for this job and token. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_prepare_pinned(BqRetirementStore store, int workspaces,
    int installed, u64 job_id, u64 attempt_token, String8 profile, char const* toolchain_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY], BqRetirementUnitPrepared* prepared)
{
    bool fresh = prepared && !prepared->owned;
    if (fresh) *prepared = (BqRetirementUnitPrepared){.policy = {.clang = -1, .inventory = -1}};
    BqJob job = {.id = job_id, .token = attempt_token};
    char request_name[48];
    BqError result = fresh && store.directory >= 0 && workspaces >= 0 && installed >= 0 && job_id &&
                     attempt_token && toolchain_root && preparation_sha256 &&
                     strnlen(preparation_sha256, SHA256_HEX_CAPACITY) == 64 &&
                     bq_record_name(request_name, "request", job_id) ? BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK)
        result = bq_record_read_at(store.directory, request_name, job.request.bytes, BQ_REQUEST_CAP,
                                   &job.request.size);
    if (result == BQ_OK)
    {
        u64 bytes = 0;
        bool nonempty = true;
        for (u32 index = 0; index < BQ_FIELD_COUNT; index += 1)
        {
            String8 field = bq_field(&job.request, index);
            nonempty = nonempty && field.length > 0;
            bytes += field.length;
        }
        result = nonempty && bytes + 4u * BQ_FIELD_COUNT == job.request.size ? BQ_OK : BQ_CORRUPT;
    }
    if (result == BQ_OK)
    {
        bq_request_digest(&job.request, job.digest);
        result = bq_retirement_unit_store_closed(store, workspaces, &job) ? BQ_OK : BQ_WORKSPACE_MISMATCH;
    }
    if (result == BQ_OK)
        result = bq_retirement_preparation_import_pinned(store, &job, installed, workspaces, profile,
                                                         preparation_sha256, &prepared->preparation);
    if (result == BQ_OK)
        result = bq_retirement_toolchain_verify(installed, profile, toolchain_root, &prepared->toolchain);
    if (result == BQ_OK)
        result = bq_retirement_reference_policy_import_profile(installed, profile, &prepared->preparation,
                                                               &prepared->toolchain, &prepared->policy);
    if (result == BQ_OK)
    {
        prepared->job = job;
        memcpy(prepared->preparation_sha256, preparation_sha256, 64);
        prepared->preparation_sha256[64] = 0;
        prepared->owned = 1;
    }
    else if (fresh)
    {
        /* A failed policy import has already released its own descriptors. */
        *prepared = (BqRetirementUnitPrepared){.policy = {.clang = -1, .inventory = -1}};
    }
    return result;
}

BqError bq_retirement_unit_prepare(BqRetirementStore store, int workspaces, int installed, u64 job_id,
                                   u64 attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY],
                                   BqRetirementUnitPrepared* prepared)
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BqError result = bq_retirement_unit_prepare_pinned(store, workspaces, installed, job_id, attempt_token,
                                                       profile, BQ_RETIREMENT_TOOLCHAIN_ROOT,
                                                       preparation_sha256, prepared);
    return result;
}

/* Matched builds in the unit (#1020 PR 2). The coordinator's sealed export
 * stays exactly two files; the unit writes its build evidence into the
 * sibling retirement-build/ directory, which it creates, fills with O_EXCL
 * read-only fsynced records and seals 0500. */
#define BQ_RETIREMENT_UNIT_EVIDENCE_FILES (2u * BQ_RETIREMENT_BUILD_STAGES + 2u)
#define BQ_RETIREMENT_UNIT_POLL_MILLISECONDS 20
/* Matches the worker's BQ_WORKER_STOP_MILLISECONDS budget for cleanup proof. */
#define BQ_RETIREMENT_UNIT_CLEANUP_NS (10ull * 1000000000ull)

BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_evidence_names(u64 job_id,
    char names[BQ_RETIREMENT_UNIT_EVIDENCE_FILES][48])
{
    bool ok = true;
    for (u32 stage = 0; ok && stage < BQ_RETIREMENT_BUILD_STAGES; stage += 1)
    {
        char log_prefix[32], receipt_prefix[32];
        int log_length = snprintf(log_prefix, sizeof(log_prefix), "matched-log-%u", stage);
        int receipt_length = snprintf(receipt_prefix, sizeof(receipt_prefix), "matched-stage-%u", stage);
        ok = log_length > 0 && (u32)log_length < sizeof(log_prefix) && receipt_length > 0 &&
             (u32)receipt_length < sizeof(receipt_prefix) &&
             bq_record_name(names[2u * stage], log_prefix, job_id) &&
             bq_record_name(names[2u * stage + 1u], receipt_prefix, job_id);
    }
    ok = ok && bq_record_name(names[2u * BQ_RETIREMENT_BUILD_STAGES], "binaries", job_id) &&
         bq_record_name(names[2u * BQ_RETIREMENT_BUILD_STAGES + 1u], "matched-builds", job_id);
    return ok;
}

/* The evidence directory must be this attempt's own retirement-build, by
 * device and inode after the attempt seal, owned by the service and holding
 * exactly the ten read-only single-link records; sealed also requires 0500. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_evidence_closed(int evidence, int workspaces, BqJob const* job,
    bool sealed)
{
    char attempt[64], names[BQ_RETIREMENT_UNIT_EVIDENCE_FILES][48];
    struct stat held = {0}, named = {0};
    bool ok = evidence >= 0 && bq_workspace_name(attempt, job->id, job->token) &&
              bq_retirement_unit_evidence_names(job->id, names) && fstat(evidence, &held) == 0 &&
              S_ISDIR(held.st_mode) && held.st_uid == geteuid() && (held.st_mode & 077) == 0 &&
              (!sealed || (held.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE);
    int workspace = ok ? openat(workspaces, attempt, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && workspace >= 0 && bq_workspace_seal(workspace, job, false) &&
         fstatat(workspace, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISDIR(named.st_mode) && named.st_dev == held.st_dev && named.st_ino == held.st_ino;
    if (workspace >= 0 && close(workspace) != 0) ok = false;
    int listing = ok ? openat(evidence, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* stream = listing >= 0 ? fdopendir(listing) : NULL;
    if (!stream && listing >= 0) close(listing);
    ok = ok && stream != NULL;
    u32 found = 0;
    bool more = ok;
    while (ok && more)
    {
        errno = 0;
        struct dirent* entry = readdir(stream);
        more = entry != NULL;
        if (!more) ok = errno == 0;
        else if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            u32 bit = 0;
            for (u32 index = 0; !bit && index < BQ_RETIREMENT_UNIT_EVIDENCE_FILES; index += 1)
                if (!strcmp(entry->d_name, names[index])) bit = 1u << index;
            ok = bit && !(found & bit) && bq_retirement_unit_file(evidence, entry->d_name);
            found |= bit;
        }
    }
    if (stream && closedir(stream) != 0) ok = false;
    return ok && found == (1u << BQ_RETIREMENT_UNIT_EVIDENCE_FILES) - 1u;
}

bool bq_retirement_unit_built_release(BqRetirementUnitBuilt* built)
{
    bool ok = built != NULL;
    if (built && built->owned)
    {
        bq_retirement_binaries_release(&built->binaries);
        ok = bq_retirement_matched_build_release(&built->verified);
    }
    if (built) *built = (BqRetirementUnitBuilt){.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
    return ok;
}

/* Re-import every stage from the sealed evidence and the export: commands
 * (including the broker binding) are rederived, logs and receipts reread,
 * the binaries and final record digests compared, A and the toolchain
 * rechecked. The coordinator's later replay reads the same files. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_build_import_pinned(BqRetirementStore store,
    BqRetirementUnitPrepared const* prepared, int workspaces, int installed, String8 workspace_root,
    String8 profile, char const* driver, char const* toolchain_root, char const* broker,
    char const binary_record_sha256[SHA256_HEX_CAPACITY], char const build_record_sha256[SHA256_HEX_CAPACITY],
    BqRetirementMatchedBuild* verified)
{
    if (verified) *verified = (BqRetirementMatchedBuild){.generated_root = -1};
    char attempt[64];
    bool ok = verified && prepared && prepared->owned && store.directory >= 0 && workspaces >= 0 &&
              bq_workspace_name(attempt, prepared->job.id, prepared->job.token);
    int workspace = ok ? openat(workspaces, attempt, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int evidence = workspace >= 0 ? openat(workspace, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY,
                                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (workspace >= 0 && close(workspace) != 0) ok = false;
    BqError result = ok && bq_retirement_unit_evidence_closed(evidence, workspaces, &prepared->job, true) ?
                     BQ_OK : BQ_WORKSPACE_MISMATCH;
    BqRetirementBuildStores stores = {store, {evidence}};
    if (result == BQ_OK)
        result = bq_retirement_matched_build_import_stores(stores, &prepared->job, installed, workspaces,
            workspace_root, profile, driver, toolchain_root, broker, prepared->preparation_sha256,
            binary_record_sha256, build_record_sha256, verified);
    if (evidence >= 0 && close(evidence) != 0 && result == BQ_OK) result = BQ_IO;
    if (result != BQ_OK && verified) *verified = (BqRetirementMatchedBuild){.generated_root = -1};
    return result;
}

/* Polls without consuming: the caller's self-pipe keeps its byte. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_cancelled(int cancellation_fd)
{
    struct pollfd wait = {.fd = cancellation_fd, .events = POLLIN};
    bool cancelled = cancellation_fd >= 0 && poll(&wait, 1, 0) > 0 && (wait.revents & (POLLIN | POLLHUP | POLLERR));
    return cancelled;
}

/* Launch the next stage and wait for it under the cancellation descriptor
 * and the absolute deadline. A cancelled or expired stage is killed, reaped
 * and proven absent before this returns; a settled one is completed. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_stage(BqRetirementBuildStores stores, BqJob const* job,
    int installed, int workspaces, String8 profile, uid_t candidate_uid, int cancellation_fd, u64 deadline_ns,
    BqRetirementMatchedBuild* build)
{
    BqError result = bq_retirement_unit_cancelled(cancellation_fd) ? BQ_WORKER_CANCEL_SIGNAL :
                     bq_retirement_build_clock_ns() >= deadline_ns ? BQ_WORKER_TIMEOUT : BQ_OK;
    BqRetirementBuildProcess process = {0};
    if (result == BQ_OK) result = bq_retirement_matched_build_launch(build, &process) ? BQ_OK : BQ_WORKER_FAILED;
    int settled = 0;
    while (result == BQ_OK && !settled)
    {
        u64 now = bq_retirement_build_clock_ns();
        u64 remaining = now < deadline_ns ? (deadline_ns - now) / 1000000u + 1u : 0;
        struct pollfd wait[2] = {{.fd = process.reader, .events = POLLIN}, {.fd = cancellation_fd, .events = POLLIN}};
        int timeout = remaining < BQ_RETIREMENT_UNIT_POLL_MILLISECONDS ? (int)remaining :
                      BQ_RETIREMENT_UNIT_POLL_MILLISECONDS;
        if (remaining && poll(wait, cancellation_fd >= 0 ? 2 : 1, timeout) < 0 && errno != EINTR)
            result = BQ_IO;
        settled = result == BQ_OK ? bq_retirement_matched_build_poll(&process) : 0;
        if (!settled && result == BQ_OK)
            result = bq_retirement_unit_cancelled(cancellation_fd) ? BQ_WORKER_CANCEL_SIGNAL :
                     bq_retirement_build_clock_ns() >= deadline_ns ? BQ_WORKER_TIMEOUT : BQ_OK;
    }
    if (process.state && !settled)
    {
        u64 now = bq_retirement_build_clock_ns();
        u64 cleanup = now && now <= UINT64_MAX - BQ_RETIREMENT_UNIT_CLEANUP_NS ?
                      now + BQ_RETIREMENT_UNIT_CLEANUP_NS : 0;
        if (!bq_retirement_matched_build_cancel(build, &process, cleanup)) result = BQ_CLEANUP_FAILED;
        bq_retirement_matched_build_abort(&process);
        if (!bq_retirement_matched_build_release(build) && result == BQ_OK) result = BQ_IO;
        build->failed = true;
    }
    else if (settled)
    {
        result = bq_retirement_matched_build_complete_stores(stores, job, installed, workspaces, profile,
                                                             &process, candidate_uid, build);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_build_pinned(BqRetirementStore store,
    BqRetirementUnitPrepared const* prepared, int workspaces, int installed, String8 workspace_root,
    String8 profile, char const* driver, char const* toolchain_root, char const* broker, uid_t candidate_uid,
    BqPhaseChannel* phases, int cancellation_fd, u64 deadline_ns, BqRetirementUnitBuilt* built)
{
    bool fresh = built && !built->owned;
    if (fresh) *built = (BqRetirementUnitBuilt){.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
    char attempt_name[64], pin[SHA256_HEX_CAPACITY];
    BqError result = fresh && prepared && prepared->owned && store.directory >= 0 && workspaces >= 0 &&
                     installed >= 0 && driver && toolchain_root && broker && phases &&
                     bq_workspace_name(attempt_name, prepared->job.id, prepared->job.token) ? BQ_OK : BQ_BAD_REQUEST;
    /* A profile without the driver pin cannot build; fail before the channel. */
    if (result == BQ_OK && !bq_retirement_profile_sha(profile, S8("build-driver-sha256="), pin))
        result = BQ_RECIPE_MISMATCH;
    /* No child exists before the supervisor acknowledges PREPARING. */
    if (result == BQ_OK && phases->sequence == 0)
        result = bq_phase_exchange_until(phases, BQ_PHASE_PREPARING, deadline_ns) ? BQ_OK : BQ_WORKER_MISMATCH;
    if (result == BQ_OK && (phases->failed || phases->sequence != BQ_PHASE_PREPARING)) result = BQ_WORKER_MISMATCH;
    int attempt = result == BQ_OK ? openat(workspaces, attempt_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool created = false;
    int evidence = attempt >= 0 && bq_workspace_seal(attempt, &prepared->job, false) ?
                   bq_create_inherited_group_directory(attempt, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY, 02700,
                                                       &created) : -1;
    if (result == BQ_OK && (evidence < 0 || !created)) result = BQ_WORKSPACE_MISMATCH;
    BqRetirementBuildStores stores = {store, {evidence}};
    BqRetirementMatchedBuild build = {.generated_root = -1};
    if (result == BQ_OK)
        result = bq_retirement_matched_build_begin_stores(stores, &prepared->job, installed, workspaces,
            workspace_root, profile, driver, toolchain_root, broker, prepared->preparation_sha256, true, &build);
    if (result == BQ_OK && strcmp(build.toolchain.manifest_sha256, prepared->toolchain.manifest_sha256))
        result = BQ_CONFIGURATION_MISMATCH;
    /* Each generate launch creates its subject's own configured root
     * (base/build/matched-build, candidate/matched-build), so the candidate
     * never starts from, or can write, the baseline configuration. */
    for (u32 stage = 0; result == BQ_OK && stage < BQ_RETIREMENT_BUILD_STAGES; stage += 1)
        result = bq_retirement_unit_stage(stores, &prepared->job, installed, workspaces, profile,
                                          candidate_uid, cancellation_fd, deadline_ns, &build);
    if (result == BQ_OK)
        result = bq_retirement_unit_evidence_closed(evidence, workspaces, &prepared->job, false) &&
                 fchmod(evidence, BQ_RETIREMENT_EXPORT_MODE) == 0 && fsync(evidence) == 0 && fsync(attempt) == 0 ?
                 BQ_OK : BQ_IO;
    if (!bq_retirement_matched_build_release(&build) && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
        result = bq_retirement_unit_build_import_pinned(store, prepared, workspaces, installed, workspace_root,
            profile, driver, toolchain_root, broker, build.binary_record_sha256, build.build_record_sha256,
            &built->verified);
    if (result == BQ_OK)
        result = bq_retirement_binaries_acquire_stores(stores, &prepared->job, installed, workspaces, profile,
            prepared->preparation_sha256, build.binary_record_sha256, &built->binaries);
    if (evidence >= 0 && close(evidence) != 0 && result == BQ_OK) result = BQ_IO;
    if (attempt >= 0 && close(attempt) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
    {
        memcpy(built->binary_record_sha256, build.binary_record_sha256, SHA256_HEX_CAPACITY);
        memcpy(built->build_record_sha256, build.build_record_sha256, SHA256_HEX_CAPACITY);
        built->owned = 1;
    }
    else if (fresh)
    {
        bq_retirement_binaries_release(&built->binaries);
        *built = (BqRetirementUnitBuilt){.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
    }
    return result;
}

BqError bq_retirement_unit_build(BqRetirementStore store, BqRetirementUnitPrepared const* prepared,
    int workspaces, int installed, String8 workspace_root, BqPhaseChannel* phases, int cancellation_fd,
    u64 deadline_ns, BqRetirementUnitBuilt* built)
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    struct passwd* candidate = getpwnam("buster-bench-candidate");
    BqError result = candidate && candidate->pw_uid != geteuid() ?
        bq_retirement_unit_build_pinned(store, prepared, workspaces, installed, workspace_root, profile,
            BQ_RETIREMENT_BUILD_DRIVER, BQ_RETIREMENT_TOOLCHAIN_ROOT, BQ_RETIREMENT_UNIT_BROKER, candidate->pw_uid,
            phases, cancellation_fd, deadline_ns, built) : BQ_CONFIGURATION_MISMATCH;
    return result;
}
