/* Lane-B caller inside the service worker-unit (#1020, #881).
 *
 * Ownership: the unit side of the A handoff and the in-unit B steps.
 * worker-unit runs in the outer transient unit as buster-bench with the
 * installed tree read-only and the queue and lease inaccessible, so it cannot
 * read the queue's preparation-<job> record. The coordinator's
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
 *   bq_retirement_unit_census_open  hold the nine pinned census files
 *   bq_retirement_unit_project      design step 6: import and join the B rows
 *                                   from #508's pinned population, the census
 *                                   and the unit's stores
 *   bq_retirement_unit_oracle       design steps 7-8: oracle authority and
 *                                   reference producer over those rows
 *   bq_retirement_unit_oracle_release
 *
 * Map: bq_retirement_unit_store_closed checks the sealed export's exact
 * contents; bq_retirement_unit_prepare_pinned is the profile seam that the
 * public wrapper calls with the compiled profile and fixed toolchain root.
 * bq_retirement_unit_evidence_closed checks the evidence directory's exact
 * closure; bq_retirement_unit_stage runs one stage under the cancellation
 * descriptor and deadline; bq_retirement_unit_build_pinned and
 * bq_retirement_unit_build_import_pinned are the profile/driver/broker seams.
 * bq_retirement_unit_project_pinned and bq_retirement_unit_oracle_pinned are
 * the profile seams of steps 6 to 8; bq_retirement_unit_source_root holds one
 * of A's materialized copies for the producer and
 * bq_retirement_unit_stop_reason maps a failed step to cancellation or
 * timeout.
 *
 * The retirement recipe stays unadmitted and bq_worker_unit calls none of
 * these yet. The correctness gate after step 8 stays fail-closed; nothing
 * here is a correctness verdict or a timing fact.
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
    String8 profile, char const* driver, char const* toolchain_root, char const* broker, char const* broker_workspaces,
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
            workspace_root, profile, driver, toolchain_root, broker, broker_workspaces, prepared->preparation_sha256,
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
 * and the absolute deadline. A cancelled or expired stage, and a broker stage
 * that settled other than cleanly, is stopped and proven absent
 * (bq_retirement_matched_build_cancel) before anything is recorded; a
 * settled one is then completed. */
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
    /* A broker stage that settled any other way than exit 0 with complete
     * capture (a nonzero or unproven CLI status, a log overflow, a capture or
     * wait failure) may have left its unit running: it takes the same KILL
     * and proof as a cancelled one before any failure evidence is written. */
    bool unproven = settled < 0 && process.launcher == BQ_RETIREMENT_LAUNCH_BROKER;
    if (process.state && (!settled || unproven))
    {
        u64 now = bq_retirement_build_clock_ns();
        u64 cleanup = now && now <= UINT64_MAX - BQ_RETIREMENT_UNIT_CLEANUP_NS ?
                      now + BQ_RETIREMENT_UNIT_CLEANUP_NS : 0;
        if (!bq_retirement_matched_build_cancel(build, &process, cleanup)) result = BQ_CLEANUP_FAILED;
        else if (settled) unproven = false;
    }
    if (process.state && (!settled || unproven))
    {
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
    String8 profile, char const* driver, char const* toolchain_root, char const* broker, char const* broker_workspaces,
    uid_t candidate_uid, BqPhaseChannel* phases, int cancellation_fd, u64 deadline_ns, BqRetirementUnitBuilt* built)
{
    bool fresh = built && !built->owned;
    if (fresh) *built = (BqRetirementUnitBuilt){.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
    char attempt_name[64], pin[SHA256_HEX_CAPACITY];
    BqError result = fresh && prepared && prepared->owned && store.directory >= 0 && workspaces >= 0 &&
                     installed >= 0 && driver && toolchain_root && broker && phases &&
                     bq_workspace_name(attempt_name, prepared->job.id, prepared->job.token) ? BQ_OK : BQ_BAD_REQUEST;
    /* The broker builds cwd and --build-directory from its own fixed root;
     * a different caller root would bind other bytes than the broker runs. */
    if (result == BQ_OK && !(broker_workspaces && string_equal(workspace_root, string_from_pointer(broker_workspaces))))
        result = BQ_WORKSPACE_MISMATCH;
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
            workspace_root, profile, driver, toolchain_root, broker, broker_workspaces, prepared->preparation_sha256,
            true, &build);
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
            profile, driver, toolchain_root, broker, broker_workspaces, build.binary_record_sha256,
            build.build_record_sha256, &built->verified);
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
            BQ_RETIREMENT_BUILD_DRIVER, BQ_RETIREMENT_TOOLCHAIN_ROOT, BQ_RETIREMENT_UNIT_BROKER,
            BQ_RETIREMENT_STAGE_WORKSPACE_ROOT, candidate->pw_uid, phases, cancellation_fd, deadline_ns, built) :
        BQ_CONFIGURATION_MISMATCH;
    return result;
}

/* Census projection, oracle authority and reference producer in the unit
 * (#1020 PR 3, design steps 6 to 8). */
BUSTER_GLOBAL_LOCAL int bq_retirement_unit_promote(int descriptor)
{
    int result = descriptor;
    if (descriptor >= 0 && descriptor < 3)
    {
        result = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
        close(descriptor);
    }
    return result;
}

bool bq_retirement_unit_census_close(BqRetirementCensusFiles* census)
{
    bool ok = census != NULL;
    for (u32 index = 0; census && index < BQ_RETIREMENT_CENSUS_FILE_COUNT; index += 1)
    {
        if (census->descriptors[index] >= 0 && close(census->descriptors[index]) != 0) ok = false;
        census->descriptors[index] = -1;
    }
    return ok;
}

BqError bq_retirement_unit_census_open(int installed, BqRetirementCensusFiles* census)
{
    /* In BqRetirementCensusFile order. */
    static char const* const names[] = {BQ_RETIREMENT_UNIT_CENSUS_FILES};
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == BQ_RETIREMENT_CENSUS_FILE_COUNT);
    for (u32 index = 0; census && index < BQ_RETIREMENT_CENSUS_FILE_COUNT; index += 1)
        census->descriptors[index] = -1;
    int recipes = census && installed >= 0 ?
                  openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int directory = recipes >= 0 && bq_owned_directory(recipes, false, true) ?
                    openat(recipes, BQ_RETIREMENT_UNIT_CENSUS_DIRECTORY,
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool ok = directory >= 0 && bq_owned_directory(directory, false, true);
    for (u32 index = 0; ok && index < BQ_RETIREMENT_CENSUS_FILE_COUNT; index += 1)
    {
        census->descriptors[index] = bq_retirement_unit_promote(openat(directory, names[index],
            O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
        ok = census->descriptors[index] >= 3;
    }
    if (directory >= 0 && close(directory) != 0) ok = false;
    if (recipes >= 0 && close(recipes) != 0) ok = false;
    if (!ok && census) bq_retirement_unit_census_close(census);
    BqError result = ok ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    return result;
}

/* The projection reads the same sealed evidence the build re-imported and
 * must name the binaries the unit holds, for the template's own population. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_project_pinned(BqRetirementStore store,
    BqRetirementUnitPrepared const* prepared, BqRetirementUnitBuilt const* built, int workspaces, int installed,
    String8 workspace_root, String8 profile, String8 census_profile, char const* driver, char const* toolchain_root,
    char const* broker, char const* broker_workspaces, BqRetirementProjection* projection)
{
    bool fresh = projection && !projection->owned;
    if (fresh) *projection = (BqRetirementProjection){0};
    char attempt[64];
    BqError result = fresh && prepared && prepared->owned && built && built->owned && store.directory >= 0 &&
                     workspaces >= 0 && installed >= 0 &&
                     bq_workspace_name(attempt, prepared->job.id, prepared->job.token) ? BQ_OK : BQ_BAD_REQUEST;
    BqRetirementCensusFiles census;
    for (u32 index = 0; index < BQ_RETIREMENT_CENSUS_FILE_COUNT; index += 1) census.descriptors[index] = -1;
    if (result == BQ_OK) result = bq_retirement_unit_census_open(installed, &census);
    int workspace = result == BQ_OK ? openat(workspaces, attempt, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int evidence = workspace >= 0 ? openat(workspace, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY,
                                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (workspace >= 0 && close(workspace) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK && !bq_retirement_unit_evidence_closed(evidence, workspaces, &prepared->job, true))
        result = BQ_WORKSPACE_MISMATCH;
    BqRetirementBuildStores stores = {store, {evidence}};
    if (result == BQ_OK)
        result = bq_retirement_correctness_project_profile(stores, &prepared->job, installed, workspaces,
            workspace_root, profile, census_profile, driver, toolchain_root, broker, broker_workspaces, &census,
            prepared->policy.template.native_target, prepared->preparation_sha256, built->binary_record_sha256,
            built->build_record_sha256, projection);
    bool same = result == BQ_OK &&
        !memcmp(projection->prepared.support_sha256, prepared->policy.template.support_sha256, SHA256_HEX_CAPACITY) &&
        !memcmp(projection->prepared.census_sha256, prepared->policy.template.census_sha256, SHA256_HEX_CAPACITY);
    for (u32 side = 0; same && side < 2; side += 1)
        same = !memcmp(projection->prepared.binary_sha256[side], built->binaries.verified.binary_sha256[side],
                       SHA256_HEX_CAPACITY);
    if (result == BQ_OK && !same) result = BQ_SOURCE_MISMATCH;
    if (evidence >= 0 && close(evidence) != 0 && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_unit_census_close(&census) && result == BQ_OK) result = BQ_IO;
    if (result != BQ_OK && fresh) bq_retirement_projection_release(projection);
    return result;
}

BqError bq_retirement_unit_project(BqRetirementStore store, BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, int workspaces, int installed, BqRetirementProjection* projection)
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BqError result = bq_retirement_unit_project_pinned(store, prepared, built, workspaces, installed,
        S8(BQ_RETIREMENT_STAGE_WORKSPACE_ROOT), profile, S8("full-census"), BQ_RETIREMENT_BUILD_DRIVER,
        BQ_RETIREMENT_TOOLCHAIN_ROOT, BQ_RETIREMENT_UNIT_BROKER, BQ_RETIREMENT_STAGE_WORKSPACE_ROOT, projection);
    return result;
}

bool bq_retirement_unit_oracle_release(BqRetirementUnitOracle* oracle)
{
    bool ok = oracle != NULL;
    if (oracle && oracle->owned) free(oracle->references);
    if (oracle) *oracle = (BqRetirementUnitOracle){0};
    return ok;
}

/* The producer and authority refuse a child deadline more than an hour
 * away. Each step takes the earlier of the unit's absolute deadline and
 * 3500 s from now, leaving headroom below that one-hour cap. */
#define BQ_RETIREMENT_UNIT_STEP_NS (3500ull * 1000000000ull)

BUSTER_GLOBAL_LOCAL u64 bq_retirement_unit_step_deadline(u64 deadline_ns)
{
    u64 now = bq_retirement_build_clock_ns();
    u64 step = now && now <= UINT64_MAX - BQ_RETIREMENT_UNIT_STEP_NS ? now + BQ_RETIREMENT_UNIT_STEP_NS : 0;
    u64 result = step && step < deadline_ns ? step : deadline_ns;
    return result;
}

/* Why a producer or authority step failed: the cancellation descriptor and
 * the deadline take precedence over the step's own refusal. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_stop_reason(int cancellation_fd, u64 deadline_ns, BqError otherwise)
{
    BqError result = bq_retirement_unit_cancelled(cancellation_fd) ? BQ_WORKER_CANCEL_SIGNAL :
                     bq_retirement_build_clock_ns() >= deadline_ns ? BQ_WORKER_TIMEOUT : otherwise;
    return result;
}

/* The producer requires its cancellation descriptor to be a close-on-exec,
 * read-only FIFO or socket (the SIGTERM self-pipe). Check it before any work
 * so a non-conforming caller fails with a configuration error, not as a
 * worker failure after the first reference build. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_cancellation_valid(int cancellation_fd)
{
    int flags = cancellation_fd >= 3 ? fcntl(cancellation_fd, F_GETFD) : -1;
    int mode = flags >= 0 ? fcntl(cancellation_fd, F_GETFL) : -1;
    struct stat pipe_info = {0};
    bool ok = flags >= 0 && (flags & FD_CLOEXEC) && mode >= 0 && (mode & O_ACCMODE) == O_RDONLY &&
              fstat(cancellation_fd, &pipe_info) == 0 &&
              (S_ISFIFO(pipe_info.st_mode) || S_ISSOCK(pipe_info.st_mode));
    return ok;
}

/* A's materialized copy of one subject, held open only while it still
 * matches the imported manifest and the same-job materialized inode closure.
 * Its owner is the service UID that materialized it and runs this unit. */
BUSTER_GLOBAL_LOCAL int bq_retirement_unit_source_root(int workspaces, BqRetirementUnitPrepared const* prepared,
    u32 side)
{
    char path[128];
    int length = snprintf(path, sizeof(path), "job-%" PRIu64 "-attempt-%" PRIu64 "/%s/source",
                          (uint64_t)prepared->job.id, (uint64_t)prepared->job.token, side ? "candidate" : "base");
    int root = length > 0 && (size_t)length < sizeof(path) ?
               bq_retirement_unit_promote(bq_open_directory_path(workspaces, string_from_pointer(path))) : -1;
    BqRetirementSource observed = {0};
    BqRetirementSource const* expected = prepared->preparation.subjects + side;
    bool ok = root >= 3 && bq_owned_directory(root, false, true) &&
              bq_retirement_scan(root, ".source-manifest", bq_field(&prepared->job.request, 3 + side), &observed,
                                 true) &&
              bq_retirement_same_source(expected, &observed) &&
              !memcmp(expected->materialized_identity_sha256, observed.installed_identity_sha256, 64);
    if (!ok && root >= 0) close(root);
    int result = ok ? root : -1;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_oracle_pinned(BqRetirementUnitPrepared const* prepared,
    BqRetirementProjection* projection, int workspaces, String8 profile, int cancellation_fd, u64 deadline_ns,
    BqRetirementUnitOracle* oracle)
{
    bool fresh = oracle && !oracle->owned;
    if (fresh) *oracle = (BqRetirementUnitOracle){0};
    BqRetirementReferencePolicy const* policy = prepared ? &prepared->policy : NULL;
    char attempt_name[64], template_pin[SHA256_HEX_CAPACITY] = {0}, population[SHA256_HEX_CAPACITY] = {0};
    BqError result = fresh && prepared && prepared->owned && projection && projection->owned &&
                     projection->rows && workspaces >= 0 && projection->job_id == prepared->job.id &&
                     projection->attempt_token == prepared->job.token &&
                     bq_workspace_name(attempt_name, prepared->job.id, prepared->job.token) ? BQ_OK : BQ_BAD_REQUEST;
    /* The compiled profile must pin the template the policy decoded. */
    if (result == BQ_OK && !(bq_retirement_profile_sha(profile, S8("reference-template-sha256="), template_pin) &&
                             !memcmp(template_pin, policy->template_sha256, SHA256_HEX_CAPACITY)))
        result = BQ_RECIPE_MISMATCH;
    /* authority_begin must see exactly the rows rows_join accepted. */
    if (result == BQ_OK &&
        !(bq_retirement_oracle_population_hash(projection->rows, projection->prepared.rows, population) &&
          !memcmp(population, projection->population_sha256, SHA256_HEX_CAPACITY) &&
          !memcmp(projection->prepared.preparation_sha256, prepared->preparation_sha256, SHA256_HEX_CAPACITY)))
        result = BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_unit_cancellation_valid(cancellation_fd))
        result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_unit_stop_reason(cancellation_fd, deadline_ns, BQ_OK);
    u32 references = result == BQ_OK ? policy->template.reference_count : 0;
    BqRetirementOracleReference* reference_workspace = references ?
        calloc(references, sizeof(*reference_workspace)) : NULL;
    BqRetirementReferenceSourceFile* source_workspace = references ?
        calloc(references, sizeof(*source_workspace)) : NULL;
    if (result == BQ_OK)
        result = reference_workspace && source_workspace && policy->plan.count == references &&
                 bq_retirement_oracle_authority_begin(&oracle->authority, &policy->template, policy->template_sha256,
                     &projection->prepared, projection->rows, reference_workspace, references, prepared->job.id,
                     prepared->job.token) ? BQ_OK : BQ_SOURCE_MISMATCH;
    /* The output directory is new and private beneath retirement-work/, the
     * build helper's private directory, so a second oracle is refused. */
    int attempt = result == BQ_OK ?
                  openat(workspaces, attempt_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int work = attempt >= 0 && bq_workspace_seal(attempt, &prepared->job, false) ?
               openat(attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool created = work >= 0 && bq_owned_directory(work, true, false) &&
                   mkdirat(work, BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY, 0700) == 0;
    int output = created ? bq_retirement_unit_promote(openat(work, BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY,
                                                             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) : -1;
    if (result == BQ_OK && !(output >= 3 && bq_owned_directory(output, true, false))) result = BQ_WORKSPACE_MISMATCH;
    int roots[2] = {-1, -1};
    for (u32 side = 0; result == BQ_OK && side < 2; side += 1)
    {
        roots[side] = bq_retirement_unit_source_root(workspaces, prepared, side);
        if (roots[side] < 0) result = BQ_SOURCE_MISMATCH;
    }
    BqRetirementReferenceProducer producer = {.source_file = -1, .binary_file = -1, .receipt_file = -1};
    if (result == BQ_OK &&
        !bq_retirement_reference_producer_begin(&producer, &policy->plan, policy->inventory_sha256,
            policy->template_sha256, policy->inventory, policy->source, roots, policy->toolchain_manifest_sha256,
            policy->clang, output, source_workspace, references, &oracle->authority))
        result = BQ_CONFIGURATION_MISMATCH;
    for (u32 index = 0; result == BQ_OK && index < references; index += 1)
    {
        BqRetirementOracleVerifiedBuild const* token = NULL;
        BqRetirementReferenceRuntime runtime = {0};
        u64 step = bq_retirement_unit_step_deadline(deadline_ns);
        bool built = bq_retirement_reference_producer_next(&producer, cancellation_fd, step, &token) &&
                     bq_retirement_reference_producer_runtime(&producer, token, &runtime);
        bool observed = built && bq_retirement_oracle_authority_next(&oracle->authority, token, &runtime.command,
                                                                     runtime.output, cancellation_fd, step);
        /* step never exceeds deadline_ns, so an expired step is a timeout. */
        if (!observed) result = bq_retirement_unit_stop_reason(cancellation_fd, step, BQ_WORKER_FAILED);
    }
    if (result == BQ_OK)
        result = bq_retirement_oracle_authority_finish(&oracle->authority) &&
                 bq_retirement_oracle_authority_ready(&oracle->authority) &&
                 bq_retirement_reference_producer_ready(&producer) ? BQ_OK : BQ_CORRUPT;
    /* Release closes the producer's held source, binary and receipt files; a
     * never-begun producer holds none. The policy keeps Clang and inventory. */
    if (!bq_retirement_reference_producer_release(&producer) && result == BQ_OK) result = BQ_IO;
    for (u32 side = 0; side < 2; side += 1)
        if (roots[side] >= 0 && close(roots[side]) != 0 && result == BQ_OK) result = BQ_IO;
    if (output >= 0 && close(output) != 0 && result == BQ_OK) result = BQ_IO;
    if (work >= 0 && close(work) != 0 && result == BQ_OK) result = BQ_IO;
    if (attempt >= 0 && close(attempt) != 0 && result == BQ_OK) result = BQ_IO;
    free(source_workspace);
    if (result == BQ_OK)
    {
        oracle->references = reference_workspace;
        memcpy(oracle->attempt_sha256, oracle->authority.attempt_sha256, SHA256_HEX_CAPACITY);
        oracle->owned = 1;
    }
    else
    {
        free(reference_workspace);
        if (fresh) *oracle = (BqRetirementUnitOracle){0};
    }
    return result;
}

BqError bq_retirement_unit_oracle(BqRetirementUnitPrepared const* prepared, BqRetirementProjection* projection,
    int workspaces, int cancellation_fd, u64 deadline_ns, BqRetirementUnitOracle* oracle)
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BqError result = bq_retirement_unit_oracle_pinned(prepared, projection, workspaces, profile, cancellation_fd,
                                                      deadline_ns, oracle);
    return result;
}
