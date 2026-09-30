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
 *   bq_retirement_unit_gate         design step 9: run the installed #509
 *                                   required checks in the unit and issue the
 *                                   correctness gate and its seal; refuses
 *                                   without the pinned required-check and
 *                                   row-plan authorities
 *   bq_retirement_unit_gate_release
 *   bq_retirement_unit_ready        design step 10: the durable ready record,
 *                                   only for a gate whose seal verifies
 *   bq_retirement_unit_replay       coordinator side: re-derive and compare
 *                                   every digest the record binds
 *   bq_retirement_unit_replay_kept  the same, keeping the replayed objects
 *                                   and a re-issued gate (BqRetirementUnitReplayed,
 *                                   bq_retirement_unit_replayed_release) for
 *                                   the coordinator's derivation of the
 *                                   authority's plan and contexts (#881 PR 3)
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
 * timeout. BqRetirementUnitReadyFacts and bq_retirement_unit_ready_format
 * define the record both sides format. Step 9: bq_retirement_unit_gate_admit
 * joins the required-check results and the row evidence in the correctness
 * gate and is the only caller of bq_retirement_correctness_authorize, the
 * one writer of its batch_authority;
 * bq_retirement_unit_gate_issue binds that to the attempt's joined objects
 * (bq_retirement_unit_joined) and computes the seal
 * (bq_retirement_unit_gate_seal), which bq_retirement_unit_gate_sealed
 * verifies for the ready record and the replay; bq_retirement_unit_gate_pinned
 * is the profile seam that imports both authorities, runs the checks
 * (retirement_check_runner.c) in a new retirement-checks/ and the row plan
 * (retirement_row_producer.c) and persists its canonical row evidence in a
 * new retirement-rows/ (bq_retirement_unit_rows_write, read back by
 * bq_retirement_unit_rows_read), from which the replay recomputes the row
 * plan and the correctness seal.
 * bq_retirement_unit_reference_observe rehashes reference-oracle/ and
 * rebuilds each runtime command; bq_retirement_unit_replay_authority rebuilds
 * the finished oracle authority; bq_retirement_unit_ready_publish is the
 * temporary-then-link write.
 *
 * bq_worker_unit reaches these only through the forked producer in
 * retirement_worker_unit.c (#881), which it admits only with a complete
 * profile. The blocked profile pins neither step 9 authority, so the gate
 * refuses and the ready record is never written in production; nothing here
 * is a correctness verdict or a timing fact.
 */
#include "retirement_unit.h"

/* The installed service (build.c passes BQ_SERVICE_INSTALLED for its main.c
 * and tests.c) must never compile the test-only seams. */
#if defined(BQ_SERVICE_INSTALLED) && defined(BQ_RETIREMENT_CORRECTNESS_TEST_ONLY)
#error "the installed service must not define BQ_RETIREMENT_CORRECTNESS_TEST_ONLY"
#endif

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
    if (oracle && oracle->owned)
    {
        free(oracle->references);
        free(oracle->descriptors);
    }
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
    int* descriptors = references ? calloc(2u * (size_t)references, sizeof(*descriptors)) : NULL;
    if (result == BQ_OK)
        result = reference_workspace && source_workspace && descriptors && policy->plan.count == references &&
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
        if (built)
        {
            descriptors[2u * index] = token->binary;
            descriptors[2u * index + 1u] = output;
        }
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
        oracle->descriptors = descriptors;
        memcpy(oracle->attempt_sha256, oracle->authority.attempt_sha256, SHA256_HEX_CAPACITY);
        oracle->owned = 1;
    }
    else
    {
        free(reference_workspace);
        free(descriptors);
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

/* Correctness gate, ready record and coordinator replay (#1020 PR 4 and the
 * #509 receipts, design steps 9 and 10). */

/* Header lines, then one observed line per reference (at most ~470 bytes). */
#define BQ_RETIREMENT_UNIT_READY_HEADER_CAP 4096u
#define BQ_RETIREMENT_UNIT_READY_ROW_CAP 512u
/* Set only by bq_retirement_unit_gate_issue. The marker admits nothing: the
 * ready record and the replay verify the seal. */
#define BQ_RETIREMENT_UNIT_GATE_ISSUED 1u
/* The producer's receipt is bounded by its own 1024-byte formatter. */
#define BQ_RETIREMENT_UNIT_RECEIPT_CAP 1024u

/* Everything one ready record binds. The writer fills it from the live
 * unit objects and the issued gate, the replay from facts it re-derived
 * itself: the row plan from the pinned authority and the correctness seal by
 * rerunning the gate over the persisted row evidence. row_evidence_sha256,
 * the persisted row evidence's digest, enters the seal but not the record. */
typedef struct BqRetirementUnitReadyFacts
{
    BqJob const* job;
    BqRetirementProjection const* projection;
    BqRetirementOracleAuthority const* authority;
    int const* descriptors;
    char const* binary_record_sha256;
    char const* build_record_sha256;
    char const* template_sha256;
    char const* inventory_sha256;
    char const* checks_authority_sha256;
    char const* receipts_sha256;
    char const* evidence_sha256;
    char const* plan_sha256;
    char const* correctness_sha256;
    char const* row_evidence_sha256;
    u32 check_count;
    char attempt_sha256[SHA256_HEX_CAPACITY];
    char gate_sha256[SHA256_HEX_CAPACITY];
} BqRetirementUnitReadyFacts;

BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_hex(char const* value)
{
    bool ok = value && strnlen(value, SHA256_HEX_CAPACITY) == 64 && bq_retirement_hex(string_from_pointer(value), 64);
    return ok;
}

/* The attempt digest is the SHA-256 of the attempt's canonical .identity
 * seal: job, token, request digest, recipe and both commits. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_attempt_sha(BqJob const* job, char digest[SHA256_HEX_CAPACITY])
{
    char bytes[512];
    u32 size = 0;
    bool ok = bq_workspace_seal_bytes(job, bytes, &size) && size > 0;
    if (ok) bq_digest(bytes, size, (char8*)digest);
    return ok;
}

/* The step 9 seal: the attempt, the request, A, the population, the oracle
 * attempt, the required-check authority and count, the ordered receipts,
 * the ordered run records and logs, the row plan, the persisted row evidence
 * (with its CPU provenance) and the correctness gate's own seal (which
 * covers every check result, row fact, frozen batch group, the batch
 * authority and the required-check authority's digest). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_gate_seal(BqRetirementUnitReadyFacts const* facts,
    char digest[SHA256_HEX_CAPACITY])
{
    bool ok = facts && facts->job && facts->projection && facts->authority && facts->check_count &&
              bq_retirement_unit_hex(facts->attempt_sha256) && bq_retirement_unit_hex((char const*)facts->job->digest) &&
              bq_retirement_unit_hex(facts->projection->prepared.preparation_sha256) &&
              bq_retirement_unit_hex(facts->projection->population_sha256) &&
              bq_retirement_unit_hex(facts->authority->attempt_sha256) &&
              bq_retirement_unit_hex(facts->checks_authority_sha256) && bq_retirement_unit_hex(facts->receipts_sha256) &&
              bq_retirement_unit_hex(facts->evidence_sha256) && bq_retirement_unit_hex(facts->plan_sha256) &&
              bq_retirement_unit_hex(facts->correctness_sha256) && bq_retirement_unit_hex(facts->row_evidence_sha256);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-unit-gate-v2";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_correctness_number(&hash, facts->job->id);
        bq_retirement_correctness_number(&hash, facts->job->token);
        sha256_add(&hash, facts->attempt_sha256, 64);
        sha256_add(&hash, facts->job->digest, 64);
        sha256_add(&hash, facts->projection->prepared.preparation_sha256, 64);
        sha256_add(&hash, facts->projection->population_sha256, 64);
        sha256_add(&hash, facts->authority->attempt_sha256, 64);
        bq_retirement_correctness_number(&hash, facts->check_count);
        sha256_add(&hash, facts->checks_authority_sha256, 64);
        sha256_add(&hash, facts->receipts_sha256, 64);
        sha256_add(&hash, facts->evidence_sha256, 64);
        sha256_add(&hash, facts->plan_sha256, 64);
        sha256_add(&hash, facts->row_evidence_sha256, 64);
        sha256_add(&hash, facts->correctness_sha256, 64);
        sha256_finish_hex(&hash, digest);
    }
    return ok;
}

/* Whether seal is the step 9 seal of exactly these facts. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_gate_sealed(char const seal[SHA256_HEX_CAPACITY],
    BqRetirementUnitReadyFacts const* facts)
{
    char expected[SHA256_HEX_CAPACITY] = {0};
    bool ok = seal && bq_retirement_unit_gate_seal(facts, expected) && !memcmp(expected, seal, SHA256_HEX_CAPACITY);
    return ok;
}

bool bq_retirement_unit_gate_release(BqRetirementUnitGate* gate)
{
    bool ok = gate != NULL;
    if (gate && gate->owned)
    {
        free(gate->rows);
        free(gate->facts);
        free(gate->check_facts);
        free(gate->checks);
        bq_retirement_row_joined_release(&gate->joined);
    }
    if (gate) *gate = (BqRetirementUnitGate){0};
    return ok;
}

/* The correctness half of step 9, over the sealed projection alone: begin
 * over the evidence rows (the projection's sealed rows completed by the row
 * plan), every required check's observed result in authority order, every
 * row fact, the frozen batch groups, then the #509 batch authority, finish
 * and ready. The authority must be this attempt's over this projection, and
 * the unit times only the A1 native-host target. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_gate_admit(BqRetirementProjection const* projection,
    BqRetirementRequiredChecks const* checks, BqRetirementCheckResult const* results,
    BqRetirementRowEvidence const* evidence, BqRetirementUnitGate* gate)
{
    bool fresh = gate && !gate->owned;
    if (fresh) *gate = (BqRetirementUnitGate){0};
    BqRetirementPrepared const* prepared = projection ? &projection->prepared : NULL;
    BqError result = fresh && projection && projection->owned && projection->rows && checks && checks->owned &&
                     checks->count && results && evidence && evidence->rows && evidence->facts &&
                     (evidence->groups || !evidence->group_count) ? BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK)
        result = checks->job_id == projection->job_id && checks->attempt_token == projection->attempt_token &&
                 !strcmp(checks->preparation_sha256, prepared->preparation_sha256) &&
                 !strcmp(checks->population_sha256, projection->population_sha256) &&
                 !memcmp(checks->source_sha256, prepared->source_sha256, sizeof(checks->source_sha256)) &&
                 !memcmp(checks->binary_sha256, prepared->binary_sha256, sizeof(checks->binary_sha256)) &&
                 prepared->native_target == BQ_RETIREMENT_UNIT_NATIVE_TARGET ? BQ_OK : BQ_RECIPE_MISMATCH;
    /* The evidence rows keep every sealed field and the observed oracle. */
    char population[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK)
    {
        bool same = evidence->row_count == prepared->rows && bq_retirement_unit_hex(evidence->plan_sha256) &&
                    bq_retirement_unit_hex(evidence->aa_second_commands_sha256) &&
                    bq_retirement_oracle_population_hash(evidence->rows, evidence->row_count, population) &&
                    !memcmp(population, projection->population_sha256, SHA256_HEX_CAPACITY);
        for (u32 row = 0; same && row < prepared->rows; row += 1)
            same = !memcmp(evidence->rows[row].independent_oracle_sha256, projection->rows[row].independent_oracle_sha256,
                           SHA256_HEX_CAPACITY);
        if (!same) result = BQ_SOURCE_MISMATCH;
    }
    u32 rows = result == BQ_OK ? prepared->rows : 0, count = result == BQ_OK ? checks->count : 0;
    u32 identity_slots = 2u * rows + 1u, census_slots = result == BQ_OK ? prepared->object_rows : 0;
    u32* identity = rows ? calloc(identity_slots, sizeof(*identity)) : NULL;
    u8* census = census_slots ? calloc(census_slots, 1) : NULL;
    u8* assigned = rows ? calloc(rows, 1) : NULL;
    if (result == BQ_OK)
    {
        gate->owned = 1;
        gate->rows = calloc(rows, sizeof(*gate->rows));
        gate->facts = calloc(rows, sizeof(*gate->facts));
        gate->check_facts = calloc(count, sizeof(*gate->check_facts));
        gate->checks = calloc(count, sizeof(*gate->checks));
        if (!(identity && census && assigned && gate->rows && gate->facts && gate->check_facts && gate->checks))
            result = BQ_IO;
    }
    bool admitted = result == BQ_OK;
    if (admitted)
    {
        memcpy(gate->rows, evidence->rows, (size_t)rows * sizeof(*gate->rows));
        memcpy(gate->checks, checks->checks, (size_t)count * sizeof(*gate->checks));
        BqRetirementPrepared joined = *prepared;
        memcpy(joined.aa_second_commands_sha256, evidence->aa_second_commands_sha256, SHA256_HEX_CAPACITY);
        admitted = bq_retirement_correctness_begin(&gate->correctness, &joined, gate->rows, gate->checks, count,
                                                   gate->check_facts, gate->facts, identity, identity_slots, census,
                                                   census_slots);
    }
    for (u32 index = 0; admitted && index < count; index += 1)
        admitted = bq_retirement_correctness_check(&gate->correctness, results + index);
    for (u32 row = 0; admitted && row < rows; row += 1)
        admitted = bq_retirement_correctness_row(&gate->correctness, evidence->facts + row);
    admitted = admitted && bq_retirement_correctness_batches(&gate->correctness, evidence->groups,
                                                             evidence->group_count, assigned, rows);
    /* (M2) The #509 correctness authority, granted only through
     * bq_retirement_correctness_authorize once every required check, row
     * fact and frozen batch group has joined; the authority digest enters
     * the seal finish takes. This issuer is its only caller. */
    admitted = admitted && bq_retirement_correctness_authorize(&gate->correctness, checks->authority_sha256) &&
               bq_retirement_correctness_finish(&gate->correctness) &&
               bq_retirement_correctness_ready(&gate->correctness) &&
               bq_retirement_check_receipts_hash(gate->checks, count, gate->receipts_sha256);
    if (result == BQ_OK && !admitted) result = BQ_RECIPE_MISMATCH;
    free(identity);
    free(census);
    free(assigned);
    if (result == BQ_OK)
    {
        memcpy(gate->authority_sha256, checks->authority_sha256, SHA256_HEX_CAPACITY);
        memcpy(gate->plan_sha256, evidence->plan_sha256, SHA256_HEX_CAPACITY);
        gate->check_count = count;
    }
    else if (fresh) bq_retirement_unit_gate_release(gate);
    return result;
}

/* The ready facts of a live attempt and an issued (or issuing) gate. */
BUSTER_GLOBAL_LOCAL BqRetirementUnitReadyFacts bq_retirement_unit_facts(BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, BqRetirementProjection const* projection,
    BqRetirementUnitOracle const* oracle, BqRetirementUnitGate const* gate)
{
    BqRetirementUnitReadyFacts facts = {.job = prepared ? &prepared->job : NULL, .projection = projection,
        .authority = oracle ? &oracle->authority : NULL, .descriptors = oracle ? oracle->descriptors : NULL,
        .binary_record_sha256 = built ? built->binary_record_sha256 : NULL,
        .build_record_sha256 = built ? built->build_record_sha256 : NULL,
        .template_sha256 = prepared ? prepared->policy.template_sha256 : NULL,
        .inventory_sha256 = prepared ? prepared->policy.inventory_sha256 : NULL,
        .checks_authority_sha256 = gate ? gate->authority_sha256 : NULL,
        .receipts_sha256 = gate ? gate->receipts_sha256 : NULL, .evidence_sha256 = gate ? gate->evidence_sha256 : NULL,
        .plan_sha256 = gate ? gate->plan_sha256 : NULL,
        .correctness_sha256 = gate ? gate->correctness.sealed_sha256 : NULL,
        .row_evidence_sha256 = gate ? gate->row_evidence_sha256 : NULL, .check_count = gate ? gate->check_count : 0};
    if (!(prepared && bq_retirement_unit_attempt_sha(&prepared->job, facts.attempt_sha256))) facts.attempt_sha256[0] = 0;
    return facts;
}

/* The live objects of one attempt belong together: the same job and token,
 * the authority over the projection's rows and the prepared template, the
 * projection still sealed and naming the held binaries, a ready authority. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_joined(BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, BqRetirementProjection const* projection, BqRetirementUnitOracle const* oracle)
{
    BqRetirementOracleAuthority const* authority = oracle ? &oracle->authority : NULL;
    char population[SHA256_HEX_CAPACITY] = {0};
    bool ok = prepared && prepared->owned && built && built->owned && projection && projection->owned &&
              projection->rows && oracle && oracle->owned && oracle->descriptors &&
              projection->job_id == prepared->job.id && projection->attempt_token == prepared->job.token &&
              authority->job_id == prepared->job.id && authority->attempt_token == prepared->job.token &&
              authority->template == &prepared->policy.template && authority->ledger.rows == projection->rows &&
              !strcmp(projection->prepared.preparation_sha256, prepared->preparation_sha256);
    for (u32 side = 0; ok && side < 2; side += 1)
        ok = !strcmp(projection->prepared.binary_sha256[side], built->binaries.verified.binary_sha256[side]);
    ok = ok && bq_retirement_oracle_population_hash(projection->rows, projection->prepared.rows, population) &&
         !memcmp(population, projection->population_sha256, SHA256_HEX_CAPACITY) &&
         bq_retirement_oracle_authority_ready(authority) && !strcmp(oracle->attempt_sha256, authority->attempt_sha256);
    return ok;
}

/* Step 9's issuer: the correctness half over this attempt's joined objects,
 * required-check authority and the row evidence joined from the pinned row
 * plan (which the issued gate then owns), then the seal over it, the ordered
 * run record and log aggregate (check_evidence_sha256) and the persisted row
 * evidence (row_evidence_sha256). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_gate_issue(BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, BqRetirementProjection const* projection,
    BqRetirementUnitOracle const* oracle, BqRetirementRequiredChecks const* checks,
    BqRetirementCheckResult const* results, BqRetirementRowJoined* rows,
    char const check_evidence_sha256[SHA256_HEX_CAPACITY], char const row_evidence_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitGate* gate)
{
    bool fresh = gate && !gate->owned;
    char attempt[SHA256_HEX_CAPACITY] = {0};
    bool joined = fresh && bq_retirement_unit_joined(prepared, built, projection, oracle) && checks &&
                  checks->owned && checks->job_id == prepared->job.id && checks->attempt_token == prepared->job.token &&
                  bq_retirement_unit_attempt_sha(&prepared->job, attempt) && !strcmp(attempt, checks->attempt_sha256) &&
                  !memcmp(checks->request_sha256, prepared->job.digest, SHA256_HEX_CAPACITY) &&
                  bq_retirement_unit_hex(check_evidence_sha256) && bq_retirement_unit_hex(row_evidence_sha256) &&
                  rows && rows->owned;
    BqError result = joined ? bq_retirement_unit_gate_admit(projection, checks, results, &rows->evidence, gate) :
                     BQ_BAD_REQUEST;
    if (result == BQ_OK)
    {
        memcpy(gate->evidence_sha256, check_evidence_sha256, SHA256_HEX_CAPACITY);
        memcpy(gate->row_evidence_sha256, row_evidence_sha256, SHA256_HEX_CAPACITY);
        /* The frozen batch groups the correctness gate points into. */
        gate->joined = *rows;
        *rows = (BqRetirementRowJoined){0};
    }
    BqRetirementUnitReadyFacts facts = bq_retirement_unit_facts(prepared, built, projection, oracle,
                                                                result == BQ_OK ? gate : NULL);
    if (result == BQ_OK) result = bq_retirement_unit_gate_seal(&facts, gate->seal_sha256) ? BQ_OK : BQ_CORRUPT;
    if (result == BQ_OK) gate->issuer = BQ_RETIREMENT_UNIT_GATE_ISSUED;
    else if (fresh) bq_retirement_unit_gate_release(gate);
    return result;
}

/* The canonical BQ-RETIREMENT-READY-V1 bytes for facts. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_ready_format(BqRetirementUnitReadyFacts const* facts, char* text,
    u32 capacity, u32* length)
{
    BqRetirementProjection const* projection = facts->projection;
    BqRetirementPrepared const* prepared = &projection->prepared;
    BqRetirementOracleAuthority const* authority = facts->authority;
    char const* digests[] = {facts->attempt_sha256, facts->job->digest, prepared->preparation_sha256,
        facts->binary_record_sha256, facts->build_record_sha256, prepared->binary_sha256[0],
        prepared->binary_sha256[1], prepared->support_sha256, prepared->census_sha256,
        projection->population_sha256, projection->evidence_sha256, facts->template_sha256,
        facts->inventory_sha256, authority->attempt_sha256, authority->observed_sha256, facts->checks_authority_sha256,
        facts->receipts_sha256, facts->evidence_sha256, facts->plan_sha256, facts->correctness_sha256,
        facts->gate_sha256};
    bool ok = true;
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(digests); index += 1) ok = bq_retirement_unit_hex(digests[index]);
    int used = ok ? snprintf(text, capacity, "BQ-RETIREMENT-READY-V1\njob=%" PRIu64 "\nattempt=%" PRIu64
        "\nattempt-identity=%s\nrequest=%s\npreparation=%s\nbinaries=%s\nmatched-builds=%s\nbinary-base=%s\n"
        "binary-candidate=%s\nsupport=%s\ncensus=%s\npopulation=%s\nrows=%u\nobject-rows=%u\nnative-target=%u\n"
        "evidence=%s\ntemplate=%s\ninventory=%s\noracle-attempt=%s\noracle-observed=%s\nobserved-rows=%u\n",
        (uint64_t)facts->job->id, (uint64_t)facts->job->token, digests[0], digests[1], digests[2], digests[3],
        digests[4], digests[5], digests[6], digests[7], digests[8], digests[9], prepared->rows,
        prepared->object_rows, prepared->native_target, digests[10], digests[11], digests[12], digests[13],
        digests[14], authority->ledger.count) : -1;
    ok = ok && used > 0 && (u32)used < capacity;
    for (u32 index = 0; ok && index < authority->ledger.count; index += 1)
    {
        BqRetirementOracleReference const* reference = authority->references + index;
        BqRetirementTrustedRow const* row = reference->row < prepared->rows ? projection->rows + reference->row : NULL;
        ok = row && bq_retirement_unit_hex(row->independent_oracle_sha256) &&
             bq_retirement_unit_hex(reference->binary_sha256) &&
             bq_retirement_unit_hex(reference->build_receipt_sha256) &&
             bq_retirement_unit_hex(reference->command_sha256) && bq_retirement_oracle_name(reference->output_name);
        int line = ok ? snprintf(text + used, capacity - (u32)used, "observed=%u %u %u %u %d %d %s %s %s %s %s\n",
            index, reference->row, reference->census_row, reference->target, facts->descriptors[2u * index],
            facts->descriptors[2u * index + 1u], row->independent_oracle_sha256, reference->binary_sha256,
            reference->build_receipt_sha256, reference->command_sha256, reference->output_name) : -1;
        ok = line > 0 && (u32)line < capacity - (u32)used;
        if (ok) used += line;
    }
    int tail = ok ? snprintf(text + used, capacity - (u32)used, "checks-authority=%s\nchecks=%u\ncheck-receipts=%s\n"
                             "check-evidence=%s\nrow-plan=%s\ncorrectness=%s\ngate=admitted %s\n", digests[15],
                             facts->check_count, digests[16], digests[17], digests[18], digests[19], digests[20]) : -1;
    ok = ok && tail > 0 && (u32)tail < capacity - (u32)used;
    *length = ok ? (u32)(used + tail) : 0;
    return ok;
}

/* The runtime command bq_retirement_reference_producer_runtime builds for
 * plan row with these held binary and output directory numbers: concrete is
 * its /proc/self/fd digest, logical the template's descriptor-free one. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_runtime_sha(BqRetirementReferencePlanRow const* row, int binary,
    int directory, char concrete[SHA256_HEX_CAPACITY], char logical[SHA256_HEX_CAPACITY])
{
    char executable[64], cwd[64];
    char* arguments[BQ_RETIREMENT_REFERENCE_ARGS_CAP + 1] = {0};
    char* environment[BQ_RETIREMENT_REFERENCE_ENV_CAP + 1] = {0};
    int exe_length = snprintf(executable, sizeof(executable), "/proc/self/fd/%d", binary);
    int cwd_length = snprintf(cwd, sizeof(cwd), "/proc/self/fd/%d", directory);
    bool ok = binary >= 3 && directory >= 3 && exe_length > 0 && (size_t)exe_length < sizeof(executable) &&
              cwd_length > 0 && (size_t)cwd_length < sizeof(cwd) && row->runtime_argument_count >= 1 &&
              row->runtime_argument_count <= BQ_RETIREMENT_REFERENCE_ARGS_CAP &&
              row->runtime_environment_count <= BQ_RETIREMENT_REFERENCE_ENV_CAP;
    if (ok)
    {
        arguments[0] = executable;
        for (u32 index = 1; index < row->runtime_argument_count; index += 1)
            arguments[index] = (char*)row->runtime_arguments[index - 1];
        for (u32 index = 0; index < row->runtime_environment_count; index += 1)
            environment[index] = (char*)row->runtime_environment[index];
        BqRetirementProcessCommand command = {arguments, environment, cwd, row->runtime_argument_count,
                                              row->runtime_environment_count};
        ok = tp_retirement_command_fields_hash(command.arguments, command.argument_count, command.directory,
                                               command.environment, command.environment_count, concrete) &&
             bq_retirement_oracle_logical_command_hash(&command, logical);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_hash_file(int directory, char const* name, u64 cap, bool executable,
    char digest[SHA256_HEX_CAPACITY])
{
    int file = bq_retirement_unit_promote(openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    bool ok = file >= 3 && bq_retirement_oracle_file_hash(file, cap, executable, digest);
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* One of the producer's names for a template reference: reference-<i>,
 * reference-log-<i>, reference-receipt-<i> or the row's output name. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_reference_name(BqRetirementOracleTemplate const* template, char const* name)
{
    static char const* const kinds[] = {"reference-log-", "reference-receipt-", "reference-"};
    bool known = false;
    for (u32 kind = 0; !known && kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
    {
        size_t prefix = strlen(kinds[kind]);
        unsigned long index = !strncmp(name, kinds[kind], prefix) && name[prefix] >= '0' && name[prefix] <= '9' ?
                              strtoul(name + prefix, NULL, 10) : ULONG_MAX;
        char expected[48];
        int length = index < template->reference_count ?
                     snprintf(expected, sizeof(expected), "%s%08lu", kinds[kind], index) : -1;
        known = length > 0 && (size_t)length < sizeof(expected) && !strcmp(expected, name);
    }
    for (u32 index = 0; !known && index < template->reference_count; index += 1)
        known = !strcmp(name, template->references[index].output_name);
    return known;
}

/* directory must be service-owned and private (sealed: exactly 0500) and
 * hold exactly the expected regular, single-link, service-owned, read-only
 * files: with a template, the producer's four per reference, else only name,
 * mode 0400. Names are unique, so every entry known and the expected count
 * is the exact closure (a colliding expected name falls short). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_closed(int directory, BqRetirementOracleTemplate const* template,
    char const* name, bool sealed)
{
    struct stat held = {0};
    bool ok = fstat(directory, &held) == 0 && S_ISDIR(held.st_mode) && held.st_uid == geteuid() &&
              (held.st_mode & 077) == 0 && (!sealed || (held.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE);
    int listing = ok ? openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* stream = listing >= 0 ? fdopendir(listing) : NULL;
    if (!stream && listing >= 0) close(listing);
    ok = ok && stream != NULL;
    u64 found = 0;
    bool more = ok;
    while (ok && more)
    {
        errno = 0;
        struct dirent* entry = readdir(stream);
        more = entry != NULL;
        if (!more) ok = errno == 0;
        else if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            struct stat info = {0};
            ok = (template ? bq_retirement_unit_reference_name(template, entry->d_name) : !strcmp(entry->d_name, name)) &&
                 fstatat(directory, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
                 info.st_nlink == 1 && info.st_uid == geteuid() && (info.st_mode & 0222) == 0 &&
                 (template || (info.st_mode & 07777) == 0400);
            found += 1;
        }
    }
    if (stream && closedir(stream) != 0) ok = false;
    return ok && found == (template ? 4ull * template->reference_count : 1u);
}

/* Rehash one reference's binary, log and output, and rebuild its receipt
 * from the policy: every field but the observed build command, whose
 * /proc/self/fd paths only the receipt records, must be what the producer
 * wrote. digests receives the output, binary and receipt SHA-256. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_reference_files(int directory, BqRetirementUnitPrepared const* prepared,
    u32 index, char digests[3][SHA256_HEX_CAPACITY])
{
    BqRetirementReferencePolicy const* policy = &prepared->policy;
    BqRetirementOracleTemplateRow const* approved = policy->template.references + index;
    char binary_name[32], log_name[32], receipt_name[32], log_sha256[SHA256_HEX_CAPACITY] = {0};
    char expected[BQ_RETIREMENT_UNIT_RECEIPT_CAP], receipt[BQ_RETIREMENT_UNIT_RECEIPT_CAP];
    u32 length = 0;
    bool ok = snprintf(binary_name, sizeof(binary_name), "reference-%08u", index) > 0 &&
              snprintf(log_name, sizeof(log_name), "reference-log-%08u", index) > 0 &&
              snprintf(receipt_name, sizeof(receipt_name), "reference-receipt-%08u", index) > 0 &&
              bq_retirement_unit_hash_file(directory, binary_name, BQ_RETIREMENT_ORACLE_BINARY_CAP, true, digests[1]) &&
              bq_retirement_unit_hash_file(directory, log_name, BQ_RETIREMENT_ORACLE_OUTPUT_CAP, false, log_sha256) &&
              bq_retirement_unit_hash_file(directory, approved->output_name, BQ_RETIREMENT_ORACLE_OUTPUT_CAP, false,
                                           digests[0]) &&
              bq_record_read_at(directory, receipt_name, (u8*)receipt, sizeof(receipt), &length) == BQ_OK;
    int prefix = ok ? snprintf(expected, sizeof(expected), "BQ-RETIREMENT-REFERENCE-BUILD-V1\n"
        "job=%llu\nattempt=%llu\nrow=%u\ncensus-row=%u\ntarget=%u\npreparation=%s\ntemplate=%s\ninventory=%s\n"
        "toolchain=%s\nclang=%s\nsource=%s\nbuild-command=%s\nobserved-command=",
        (unsigned long long)prepared->job.id, (unsigned long long)prepared->job.token, approved->row,
        approved->census_row, approved->target, prepared->preparation_sha256, policy->template_sha256,
        policy->inventory_sha256, policy->toolchain_manifest_sha256, policy->plan.clang_sha256,
        approved->source_sha256, approved->build_command_sha256) : -1;
    ok = ok && prefix > 0 && (u32)prefix + 64u < length && !memcmp(receipt, expected, (size_t)prefix) &&
         bq_retirement_hex((String8){(char8*)receipt + prefix, 64}, 64);
    int suffix = ok ? snprintf(expected, sizeof(expected), "\nbinary=%s\nlog=%s\n", digests[1], log_sha256) : -1;
    ok = ok && suffix > 0 && (u32)prefix + 64u + (u32)suffix == length &&
         !memcmp(receipt + prefix + 64, expected, (size_t)suffix);
    if (ok) bq_digest(receipt, length, (char8*)digests[2]);
    return ok;
}

/* The references an honest oracle run leaves, from the template and the
 * rehashed reference-oracle/ closure, each runtime command rebuilt from its
 * recorded descriptor numbers; outputs receives each observed output digest.
 * The producer runs every reference in one output directory, which is never
 * the reference binary's descriptor. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_reference_observe(int directory, BqRetirementUnitPrepared const* prepared,
    int const* descriptors, BqRetirementOracleReference* references, char (*outputs)[SHA256_HEX_CAPACITY])
{
    BqRetirementReferencePolicy const* policy = &prepared->policy;
    u32 count = policy->template.reference_count;
    bool ok = count && policy->plan.count == count && bq_retirement_unit_closed(directory, &policy->template, NULL, true);
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqRetirementOracleTemplateRow const* approved = policy->template.references + index;
        BqRetirementOracleReference* reference = references + index;
        char digests[3][SHA256_HEX_CAPACITY] = {{0}}, logical[SHA256_HEX_CAPACITY] = {0};
        *reference = (BqRetirementOracleReference){.row = approved->row, .census_row = approved->census_row,
                                                   .target = approved->target};
        memcpy(reference->preparation_sha256, prepared->preparation_sha256, SHA256_HEX_CAPACITY);
        memcpy(reference->source_sha256, approved->source_sha256, SHA256_HEX_CAPACITY);
        memcpy(reference->configuration_sha256, approved->configuration_sha256, SHA256_HEX_CAPACITY);
        memcpy(reference->build_command_sha256, approved->build_command_sha256, SHA256_HEX_CAPACITY);
        memcpy(reference->logical_command_sha256, approved->logical_command_sha256, SHA256_HEX_CAPACITY);
        memcpy(reference->output_name, approved->output_name, BQ_RETIREMENT_OUTPUT_NAME_CAP);
        ok = descriptors[2u * index + 1u] == descriptors[1] && descriptors[2u * index] != descriptors[1] &&
             bq_retirement_unit_reference_files(directory, prepared, index, digests) &&
             bq_retirement_unit_runtime_sha(policy->plan.rows + index, descriptors[2u * index],
                                            descriptors[2u * index + 1u], reference->command_sha256, logical) &&
             !strcmp(logical, approved->logical_command_sha256);
        if (ok)
        {
            memcpy(outputs[index], digests[0], SHA256_HEX_CAPACITY);
            memcpy(reference->binary_sha256, digests[1], SHA256_HEX_CAPACITY);
            memcpy(reference->build_receipt_sha256, digests[2], SHA256_HEX_CAPACITY);
        }
    }
    return ok;
}

/* An O_EXCL temporary, fsynced, then linked to its content address and
 * unlinked. A crash before the link leaves only ready-partial-<id> in an
 * unsealed directory, one after it two links; the replay rejects both. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_ready_publish(int ready, u64 job_id, char const* text, u32 length,
    char const digest[SHA256_HEX_CAPACITY])
{
    char partial[48], name[80];
    int named = snprintf(name, sizeof(name), "ready-%s", digest);
    bool ok = bq_record_name(partial, "ready-partial", job_id) && named > 0 && (size_t)named < sizeof(name);
    int writer = ok ? openat(ready, partial, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && writer >= 0 && bq_write_all(writer, (u8 const*)text, length) && fchmod(writer, 0400) == 0 &&
         fsync(writer) == 0;
    if (writer >= 0 && close(writer) != 0) ok = false;
    ok = ok && linkat(ready, partial, ready, name, 0) == 0 && unlinkat(ready, partial, 0) == 0 && fsync(ready) == 0 &&
         bq_retirement_unit_closed(ready, NULL, name, false) && fchmod(ready, BQ_RETIREMENT_EXPORT_MODE) == 0 &&
         fsync(ready) == 0;
    return ok;
}

/* Opens job-<id>-attempt-<token>/[<parent>/]<name> after the attempt seal,
 * without following links. */
BUSTER_GLOBAL_LOCAL int bq_retirement_unit_attempt_open(int workspaces, BqJob const* job, char const* parent,
    char const* name)
{
    char attempt_name[64];
    int attempt = bq_workspace_name(attempt_name, job->id, job->token) ?
                  openat(workspaces, attempt_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool sealed = attempt >= 0 && bq_workspace_seal(attempt, job, false);
    int middle = sealed && parent ? openat(attempt, parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int base = parent ? middle : sealed ? attempt : -1;
    int result = base >= 0 ?
                 bq_retirement_unit_promote(openat(base, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) : -1;
    if (middle >= 0) close(middle);
    if (attempt >= 0) close(attempt);
    return result;
}

/* Writes the canonical row evidence as the new rows/row-evidence (O_EXCL,
 * 0400, fsynced with its directory) and returns its digest. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_rows_write(int rows, char const* text, u32 length,
    char digest[SHA256_HEX_CAPACITY])
{
    int file = bq_retirement_unit_promote(openat(rows, BQ_RETIREMENT_ROW_EVIDENCE_NAME,
                                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    bool ok = file >= 3 && bq_write_all(file, (u8 const*)text, length) && fchmod(file, 0400) == 0 && fsync(file) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    ok = ok && fsync(rows) == 0;
    if (ok) bq_digest(text, length, (char8*)digest);
    return ok;
}

/* The persisted row evidence of a sealed retirement-rows/: exactly one
 * read-only row-evidence file, read and hashed. */
BUSTER_GLOBAL_LOCAL u8* bq_retirement_unit_rows_read(int rows, u32* length, char digest[SHA256_HEX_CAPACITY])
{
    u64 size = 0;
    u8* bytes = rows >= 0 && bq_retirement_unit_closed(rows, NULL, BQ_RETIREMENT_ROW_EVIDENCE_NAME, true) ?
                bq_retirement_row_read(rows, BQ_RETIREMENT_ROW_EVIDENCE_NAME, BQ_RETIREMENT_ROW_EVIDENCE_BYTES_CAP,
                                       &size) : NULL;
    if (bytes) bq_digest(bytes, (u32)size, (char8*)digest);
    *length = bytes ? (u32)size : 0;
    return bytes;
}

/* Design step 9 with a profile (the production entry has neither pin yet).
 * The required-check and row-plan imports refuse before retirement-checks/
 * or retirement-rows/ exists or any child starts; once they exist, a second
 * gate into the attempt is refused. The unit must have no child at all,
 * before the first check and again before the gate is issued. A's roots are
 * scanned when held (their manifests fill every receipt) and rescanned after
 * the last check and row. The row evidence is produced here by
 * bq_retirement_row_produce; supplied, a test seam, replaces that run with a
 * given observation of the same plan. Either way the gate admits exactly
 * what it persisted: the canonical bytes, parsed back and joined with the
 * plan. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_gate_pinned(BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, BqRetirementProjection const* projection,
    BqRetirementUnitOracle const* oracle, int workspaces, int installed, String8 profile,
    BqRetirementRowObserved const* supplied, int cancellation_fd, u64 deadline_ns, BqRetirementUnitGate* gate)
{
    bool fresh = gate && !gate->owned;
    if (fresh) *gate = (BqRetirementUnitGate){0};
    BqError result = fresh && workspaces >= 0 && installed >= 0 &&
                     bq_retirement_unit_joined(prepared, built, projection, oracle) ? BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK && !bq_retirement_unit_cancellation_valid(cancellation_fd)) result = BQ_CONFIGURATION_MISMATCH;
    BqRetirementRequiredChecks checks = {.hosted = -1};
    if (result == BQ_OK)
        result = bq_retirement_required_checks_import_profile(installed, profile, &prepared->job,
                                                              &prepared->preparation, projection, &checks);
    BqRetirementRowPlan plan = {0};
    if (result == BQ_OK)
        result = bq_retirement_row_plan_import_profile(installed, profile, &prepared->job, projection, &plan);
    if (result == BQ_OK && !bq_retirement_check_descendants_absent()) result = BQ_WORKER_MISMATCH;
    BqRetirementCheckResult* results = result == BQ_OK ? calloc(checks.count, sizeof(*results)) : NULL;
    if (result == BQ_OK && !results) result = BQ_IO;
    /* New, private evidence directories whose entries are durable before any
     * child; the attempt's private work directory; A's two held roots. */
    char attempt_name[64];
    int attempt = result == BQ_OK && bq_workspace_name(attempt_name, prepared->job.id, prepared->job.token) ?
                  openat(workspaces, attempt_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool created = false, rows_created = false;
    int directory = attempt >= 0 && bq_workspace_seal(attempt, &prepared->job, false) ?
                    bq_retirement_unit_promote(bq_create_inherited_group_directory(attempt,
                        BQ_RETIREMENT_UNIT_CHECKS_DIRECTORY, 02700, &created)) : -1;
    int rows = directory >= 3 && created ?
               bq_retirement_unit_promote(bq_create_inherited_group_directory(attempt, BQ_RETIREMENT_ROWS_DIRECTORY,
                                                                              02700, &rows_created)) : -1;
    if (result == BQ_OK && (directory < 3 || !created || rows < 3 || !rows_created)) result = BQ_WORKSPACE_MISMATCH;
    if (result == BQ_OK && fsync(attempt) != 0) result = BQ_IO;
    int work = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared->job, NULL,
                                                                 BQ_RETIREMENT_BUILD_WORK_DIRECTORY) : -1;
    if (result == BQ_OK && !(work >= 3 && bq_owned_directory(work, true, false))) result = BQ_WORKSPACE_MISMATCH;
    int sources[2] = {-1, -1};
    char scanned[2][SHA256_HEX_CAPACITY] = {{0}};
    for (u32 side = 0; result == BQ_OK && side < 2; side += 1)
    {
        /* The scan verified the root against A's manifest. */
        sources[side] = bq_retirement_unit_source_root(workspaces, prepared, side);
        if (sources[side] < 3) result = BQ_SOURCE_MISMATCH;
        else memcpy(scanned[side], prepared->preparation.subjects[side].manifest_sha256, SHA256_HEX_CAPACITY);
    }
    /* Every required check in authority order; the first failing one stops
     * the gate and keeps its receipt in the unsealed directory. */
    char const (*scanned_view)[SHA256_HEX_CAPACITY] = (char const (*)[SHA256_HEX_CAPACITY])scanned;
    BqRetirementCheckRun run = {&checks, &built->binaries, scanned_view, {sources[0], sources[1]}, work, directory,
                                cancellation_fd, deadline_ns};
    for (u32 index = 0; result == BQ_OK && index < checks.count; index += 1)
    {
        BqRetirementCheckResult const* observed = results + index;
        result = bq_retirement_check_run(&run, index, results + index);
        if (result == BQ_OK && !(observed->exit_code == 0 && !observed->failures && !observed->timed_out &&
                                 !observed->out_of_memory &&
                                 !strcmp(observed->receipt_sha256, checks.checks[index].receipt_sha256)))
            result = BQ_RECIPE_MISMATCH;
    }
    /* The check evidence is closed, digested and sealed before any row step
     * runs candidate code (which its sandbox also keeps out of it). */
    char evidence_sha256[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK && !(bq_retirement_check_evidence_closed(directory, checks.checks, checks.count, false,
                                                                 evidence_sha256) &&
                             fchmod(directory, BQ_RETIREMENT_EXPORT_MODE) == 0 && fsync(directory) == 0))
        result = BQ_SOURCE_MISMATCH;
    /* The per-row half: every row step of the pinned plan with the held
     * binaries, persisted canonically and admitted as persisted. */
    BqRetirementRowObserved produced = {0}, persisted = {0};
    BqRetirementRowRun rows_run = {&plan, &built->binaries, {sources[0], sources[1]}, work, cancellation_fd,
                                   deadline_ns};
    if (result == BQ_OK && !supplied) result = bq_retirement_row_produce(&rows_run, &produced);
    char* text = NULL;
    u32 text_length = 0;
    char row_evidence[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK && !(bq_retirement_row_observed_format(supplied ? supplied : &produced, &text, &text_length) &&
                             bq_retirement_unit_rows_write(rows, text, text_length, row_evidence)))
        result = BQ_IO;
    if (result == BQ_OK) result = bq_retirement_row_observed_parse((u8 const*)text, text_length, &plan, &persisted);
    BqRetirementRowJoined joined = {0};
    if (result == BQ_OK) result = bq_retirement_row_evidence_join(&plan, projection, &persisted, &joined);
    /* A's roots, rescanned after the last check and row, must still be the
     * same directories holding A's exact manifests. */
    for (u32 side = 0; result == BQ_OK && side < 2; side += 1)
    {
        int again = bq_retirement_unit_source_root(workspaces, prepared, side);
        struct stat held = {0}, current = {0};
        bool same = again >= 3 && fstat(again, &current) == 0 && fstat(sources[side], &held) == 0 &&
                    held.st_dev == current.st_dev && held.st_ino == current.st_ino;
        if (again >= 0) close(again);
        if (!same) result = BQ_SOURCE_MISMATCH;
    }
    /* The sealed check evidence is unchanged, and no process is left
     * behind. */
    char evidence_again[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK && !(bq_retirement_check_evidence_closed(directory, checks.checks, checks.count, true,
                                                                 evidence_again) &&
                             !strcmp(evidence_again, evidence_sha256)))
        result = BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_check_descendants_absent()) result = BQ_CLEANUP_FAILED;
    if (result == BQ_OK)
        result = bq_retirement_unit_gate_issue(prepared, built, projection, oracle, &checks, results, &joined,
                                               evidence_sha256, row_evidence, gate);
    /* Freeze both evidence directories with the issued gate: ready and the
     * replay require exactly these receipts and this row evidence, sealed. */
    if (result == BQ_OK &&
        !(bq_retirement_check_evidence_closed(directory, gate->checks, gate->check_count, false, NULL) &&
          fchmod(directory, BQ_RETIREMENT_EXPORT_MODE) == 0 && fsync(directory) == 0 &&
          fchmod(rows, BQ_RETIREMENT_EXPORT_MODE) == 0 && fsync(rows) == 0 &&
          bq_retirement_unit_closed(rows, NULL, BQ_RETIREMENT_ROW_EVIDENCE_NAME, true)))
    {
        result = BQ_IO;
        bq_retirement_unit_gate_release(gate);
    }
    int const opened[] = {sources[0], sources[1], work, directory, rows, attempt};
    for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(opened); slot += 1)
        if (opened[slot] >= 0) close(opened[slot]);
    free(results);
    free(text);
    bq_retirement_row_joined_release(&joined);
    bq_retirement_row_observed_release(&persisted);
    bq_retirement_row_observed_release(&produced);
    bq_retirement_row_plan_release(&plan);
    if (!bq_retirement_required_checks_release(&checks) && result == BQ_OK) result = BQ_IO;
    return result;
}

BqError bq_retirement_unit_gate(BqRetirementUnitPrepared const* prepared, BqRetirementUnitBuilt const* built,
    BqRetirementProjection const* projection, BqRetirementUnitOracle const* oracle, int workspaces, int installed,
    int cancellation_fd, u64 deadline_ns, BqRetirementUnitGate* gate)
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BqError result = bq_retirement_unit_gate_pinned(prepared, built, projection, oracle, workspaces, installed,
                                                    profile, NULL, cancellation_fd, deadline_ns, gate);
    return result;
}

/* Design step 10 with a profile: the gate's authority digest and row plan
 * must still be the profile's required-checks and row-plan pins. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_ready_pinned(BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, BqRetirementProjection const* projection, BqRetirementUnitOracle const* oracle,
    BqRetirementUnitGate const* gate, int workspaces, String8 profile, char ready_sha256[SHA256_HEX_CAPACITY])
{
    if (ready_sha256) ready_sha256[0] = 0;
    BqRetirementOracleAuthority const* authority = oracle ? &oracle->authority : NULL;
    BqRetirementUnitReadyFacts facts = bq_retirement_unit_facts(prepared, built, projection, oracle, gate);
    /* Design step 9 must have issued this exact attempt's gate, whose seal
     * verifies against the live facts, under both pinned authorities;
     * production pins neither, so it refuses here, before any object or the
     * attempt is examined. */
    char pin[SHA256_HEX_CAPACITY] = {0}, plan_pin[SHA256_HEX_CAPACITY] = {0};
    bool admitted = gate && gate->owned && gate->issuer == BQ_RETIREMENT_UNIT_GATE_ISSUED && prepared &&
                    bq_retirement_profile_sha(profile, S8("required-checks-sha256="), pin) &&
                    !strcmp(pin, gate->authority_sha256) && !strcmp(pin, gate->correctness.authority_sha256) &&
                    bq_retirement_profile_sha(profile, S8("row-plan-sha256="), plan_pin) &&
                    !strcmp(plan_pin, gate->plan_sha256) &&
                    bq_retirement_unit_gate_sealed(gate->seal_sha256, &facts);
    BqRetirementCorrectness const* correctness = admitted ? &gate->correctness : NULL;
    char receipts[SHA256_HEX_CAPACITY] = {0};
    bool ok = admitted && ready_sha256 && workspaces >= 0 &&
              bq_retirement_unit_joined(prepared, built, projection, oracle) &&
              bq_retirement_correctness_ready(correctness) && correctness->batch_authority == 1 &&
              correctness->trusted_rows == gate->rows && correctness->required_checks == gate->checks &&
              correctness->check_count == gate->check_count &&
              !memcmp(correctness->prepared.preparation_sha256, projection->prepared.preparation_sha256,
                      SHA256_HEX_CAPACITY) &&
              !memcmp(correctness->prepared.binary_sha256, projection->prepared.binary_sha256,
                      sizeof(correctness->prepared.binary_sha256)) &&
              bq_retirement_check_receipts_hash(gate->checks, gate->check_count, receipts) &&
              !strcmp(receipts, gate->receipts_sha256);
    BqError result = !admitted ? BQ_RECIPE_MISMATCH : ok ? BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK) memcpy(facts.gate_sha256, gate->seal_sha256, SHA256_HEX_CAPACITY);
    /* The sealed step 9 evidence must hold exactly the gate's receipts, run
     * records and logs. */
    int checks = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared->job, NULL,
                                                                   BQ_RETIREMENT_UNIT_CHECKS_DIRECTORY) : -1;
    char evidence_sha256[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK &&
        !(checks >= 0 && bq_retirement_check_evidence_closed(checks, gate->checks, gate->check_count, true,
                                                             evidence_sha256) &&
          !strcmp(evidence_sha256, gate->evidence_sha256)))
        result = BQ_SOURCE_MISMATCH;
    if (checks >= 0) close(checks);
    /* The sealed step 9 row evidence must be exactly the bytes the gate
     * admitted. */
    int rows = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared->job, NULL,
                                                                 BQ_RETIREMENT_ROWS_DIRECTORY) : -1;
    u32 rows_length = 0;
    char rows_sha256[SHA256_HEX_CAPACITY] = {0};
    u8* rows_bytes = result == BQ_OK ? bq_retirement_unit_rows_read(rows, &rows_length, rows_sha256) : NULL;
    if (result == BQ_OK && !(rows_bytes && !strcmp(rows_sha256, gate->row_evidence_sha256))) result = BQ_SOURCE_MISMATCH;
    free(rows_bytes);
    if (rows >= 0) close(rows);
    /* Freeze the producer's output directory, require its files to be
     * exactly the authority's and format the record, all before
     * retirement-ready/ exists: a failure here leaves the attempt able to
     * write its record later. */
    int reference = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared->job,
        BQ_RETIREMENT_BUILD_WORK_DIRECTORY, BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY) : -1;
    if (result == BQ_OK && !(reference >= 0 && bq_owned_directory(reference, true, false) &&
                             fchmod(reference, BQ_RETIREMENT_EXPORT_MODE) == 0 && fsync(reference) == 0))
        result = BQ_WORKSPACE_MISMATCH;
    u32 count = result == BQ_OK ? authority->ledger.count : 0;
    u32 capacity = BQ_RETIREMENT_UNIT_READY_HEADER_CAP + BQ_RETIREMENT_UNIT_READY_ROW_CAP * count;
    BqRetirementOracleReference* observed = count ? calloc(count, sizeof(*observed)) : NULL;
    char (*outputs)[SHA256_HEX_CAPACITY] = count ? calloc(count, sizeof(*outputs)) : NULL;
    char* text = count ? malloc(capacity) : NULL;
    if (result == BQ_OK && !(observed && outputs && text)) result = BQ_IO;
    if (result == BQ_OK)
        result = count == prepared->policy.template.reference_count &&
                 bq_retirement_unit_reference_observe(reference, prepared, oracle->descriptors, observed, outputs) ?
                 BQ_OK : BQ_SOURCE_MISMATCH;
    for (u32 index = 0; result == BQ_OK && index < count; index += 1)
    {
        BqRetirementOracleReference const* held = authority->references + index;
        bool same = held->row == observed[index].row &&
                    !strcmp(held->build_receipt_sha256, observed[index].build_receipt_sha256) &&
                    !strcmp(held->binary_sha256, observed[index].binary_sha256) &&
                    !strcmp(held->command_sha256, observed[index].command_sha256) &&
                    !strcmp(outputs[index], projection->rows[held->row].independent_oracle_sha256);
        if (!same) result = BQ_SOURCE_MISMATCH;
    }
    u32 length = 0;
    char digest[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK)
        result = bq_retirement_unit_ready_format(&facts, text, capacity, &length) ? BQ_OK : BQ_CORRUPT;
    if (result == BQ_OK) bq_digest(text, length, (char8*)digest);
    /* The new directory's entry is made durable in the attempt before the
     * record is written, so a successful publish needs no later fsync and
     * returns its digest. */
    char attempt_name[64];
    int attempt = result == BQ_OK && bq_workspace_name(attempt_name, prepared->job.id, prepared->job.token) ?
                  openat(workspaces, attempt_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool created = false;
    int ready = attempt >= 0 && bq_workspace_seal(attempt, &prepared->job, false) ?
                bq_create_inherited_group_directory(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, 02700, &created) : -1;
    if (result == BQ_OK && (ready < 0 || !created)) result = BQ_WORKSPACE_MISMATCH;
    if (result == BQ_OK && fsync(attempt) != 0) result = BQ_IO;
    bool published = result == BQ_OK && bq_retirement_unit_ready_publish(ready, prepared->job.id, text, length, digest);
    if (result == BQ_OK && !published) result = BQ_IO;
    free(text);
    free(outputs);
    free(observed);
    /* result is BQ_OK exactly when the record is published, and a failed
     * close of a read-only descriptor cannot undo that durable record. */
    if (reference >= 0) close(reference);
    if (ready >= 0) close(ready);
    if (attempt >= 0) close(attempt);
    if (published) memcpy(ready_sha256, digest, SHA256_HEX_CAPACITY);
    return result;
}

BqError bq_retirement_unit_ready(BqRetirementUnitPrepared const* prepared, BqRetirementUnitBuilt const* built,
    BqRetirementProjection const* projection, BqRetirementUnitOracle const* oracle, BqRetirementUnitGate const* gate,
    int workspaces, char ready_sha256[SHA256_HEX_CAPACITY])
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BqError result = bq_retirement_unit_ready_pinned(prepared, built, projection, oracle, gate, workspaces, profile,
                                                     ready_sha256);
    return result;
}

/* A record's 64-hex value after "\n<key>". Only the build digests, the gate
 * seal, the runtime descriptor numbers and the command digests they must
 * rebuild are read from the record; the replay recomputes everything else
 * and compares the whole byte stream. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_ready_value(char const* text, char const* key,
    char value[SHA256_HEX_CAPACITY])
{
    char pattern[48];
    int length = snprintf(pattern, sizeof(pattern), "\n%s", key);
    char const* found = length > 0 && (size_t)length < sizeof(pattern) ? strstr(text, pattern) : NULL;
    char const* start = found ? found + length : NULL;
    bool ok = start && strnlen(start, 65) == 65 && start[64] == '\n' &&
              bq_retirement_hex((String8){(char8*)start, 64}, 64);
    if (ok)
    {
        memcpy(value, start, 64);
        value[64] = 0;
    }
    return ok;
}

/* One observed line after "observed=": six canonical decimal fields, four
 * digests and a name, each bounded by its separator. Keeps the descriptor
 * numbers and the recorded command digest; returns the line's end. */
BUSTER_GLOBAL_LOCAL char const* bq_retirement_unit_ready_row(char const* line, u32 index, int descriptors[2],
    char command[SHA256_HEX_CAPACITY])
{
    u64 values[6] = {0};
    bool ok = true;
    for (u32 field = 0; ok && field < 10; field += 1)
    {
        size_t length = strcspn(line, " \n");
        ok = length > 0 && line[length] == ' ';
        if (ok && field < 6)
        {
            IntegerParsingU64 parsed = string8_parse_u64_decimal((String8){(char8*)line, length});
            ok = length <= 10 && parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == length;
            values[field] = parsed.value;
        }
        if (ok && field == 9)
        {
            ok = length == 64 && bq_retirement_hex((String8){(char8*)line, 64}, 64);
            if (ok) memcpy(command, line, 64);
            command[64] = 0;
        }
        if (ok) line += length + 1;
    }
    ok = ok && values[0] == index && values[4] <= INT_MAX && values[5] <= INT_MAX;
    if (ok)
    {
        descriptors[0] = (int)values[4];
        descriptors[1] = (int)values[5];
    }
    char const* end = ok ? strchr(line, '\n') : NULL;
    return end;
}

/* The observed lines must follow one another, in index order. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_ready_rows(char const* text, u32 count, int* descriptors,
    char (*commands)[SHA256_HEX_CAPACITY])
{
    char const* cursor = strstr(text, "\nobserved=");
    for (u32 index = 0; cursor && index < count; index += 1)
        cursor = !strncmp(cursor, "\nobserved=", 10) ?
                 bq_retirement_unit_ready_row(cursor + 10, index, descriptors + 2u * index, commands[index]) : NULL;
    return cursor != NULL;
}

/* Re-import the matched builds and hold both binaries, as the unit's own
 * build does after it seals retirement-build/. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_built_import(BqRetirementStore store,
    BqRetirementUnitPrepared const* prepared, int workspaces, int installed, String8 workspace_root, String8 profile,
    char const* driver, char const* toolchain_root, char const* broker, char const* broker_workspaces,
    char const binary_record_sha256[SHA256_HEX_CAPACITY], char const build_record_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitBuilt* built)
{
    *built = (BqRetirementUnitBuilt){.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}},
                                     .owned = 1};
    BqError result = bq_retirement_unit_build_import_pinned(store, prepared, workspaces, installed, workspace_root,
        profile, driver, toolchain_root, broker, broker_workspaces, binary_record_sha256, build_record_sha256,
        &built->verified);
    int evidence = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared->job, NULL,
                                                                     BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY) : -1;
    BqRetirementBuildStores stores = {store, {evidence}};
    if (result == BQ_OK)
        result = evidence >= 0 && bq_retirement_unit_evidence_closed(evidence, workspaces, &prepared->job, true) ?
                 bq_retirement_binaries_acquire_stores(stores, &prepared->job, installed, workspaces, profile,
                     prepared->preparation_sha256, binary_record_sha256, &built->binaries) : BQ_WORKSPACE_MISMATCH;
    if (evidence >= 0 && close(evidence) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
    {
        memcpy(built->binary_record_sha256, binary_record_sha256, SHA256_HEX_CAPACITY);
        memcpy(built->build_record_sha256, build_record_sha256, SHA256_HEX_CAPACITY);
    }
    else bq_retirement_unit_built_release(built);
    return result;
}

/* The finished authority an honest oracle run leaves, rebuilt from the
 * template, the replayed projection and the rehashed references. The
 * authority's own hashes recompute the ledger spec and seal, the observation
 * chain and the attempt digest, and authority_ready rechecks them together.
 * The projection's reference rows receive the observed output digests. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_replay_authority(BqRetirementUnitPrepared const* prepared,
    BqRetirementProjection* projection, BqRetirementOracleReference* references,
    char (*outputs)[SHA256_HEX_CAPACITY], BqRetirementOracleAuthority* authority)
{
    BqRetirementOracleTemplate const* template = &prepared->policy.template;
    u32 count = template->reference_count;
    bool ok = count && projection->prepared.rows == template->population_rows;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqRetirementTrustedRow* row = references[index].row < projection->prepared.rows ?
                                      projection->rows + references[index].row : NULL;
        /* authority_next's rule: a reference is neither matched binary. */
        ok = row && !row->independent_oracle_sha256[0] &&
             strcmp(references[index].binary_sha256, projection->prepared.binary_sha256[0]) &&
             strcmp(references[index].binary_sha256, projection->prepared.binary_sha256[1]);
        if (ok) memcpy(row->independent_oracle_sha256, outputs[index], SHA256_HEX_CAPACITY);
    }
    *authority = (BqRetirementOracleAuthority){.template = template, .references = references,
        .job_id = prepared->job.id, .attempt_token = prepared->job.token, .observed_rows = count};
    BqRetirementOracleLedger* ledger = &authority->ledger;
    *ledger = (BqRetirementOracleLedger){.prepared = projection->prepared, .rows = projection->rows,
        .references = references, .count = count, .done = count, .authority_bound = 1,
        .job_id = prepared->job.id, .attempt_token = prepared->job.token};
    memcpy(ledger->template_sha256, prepared->policy.template_sha256, SHA256_HEX_CAPACITY);
    memcpy(authority->template_sha256, prepared->policy.template_sha256, SHA256_HEX_CAPACITY);
    memcpy(authority->toolchain_identity_sha256, template->toolchain_identity_sha256, SHA256_HEX_CAPACITY);
    char progress[SHA256_HEX_CAPACITY] = {0}, next[SHA256_HEX_CAPACITY] = {0};
    ok = ok && bq_retirement_oracle_spec_hash(&ledger->prepared, references, count, ledger->pinned_sha256) &&
         bq_retirement_oracle_seal(ledger, ledger->sealed_sha256) &&
         bq_oracle_authority_observation_seed(authority, progress);
    for (u32 index = 0; ok && index < count; index += 1)
    {
        ok = bq_oracle_authority_observation_step(progress, index, references + index,
                                                  projection->rows + references[index].row, next);
        if (ok) memcpy(progress, next, SHA256_HEX_CAPACITY);
    }
    if (ok)
    {
        memcpy(authority->observed_sha256, progress, SHA256_HEX_CAPACITY);
        ledger->finished = 1;
        authority->finished = 1;
        ok = bq_oracle_authority_attempt_hash(authority, authority->attempt_sha256) &&
             bq_retirement_oracle_authority_ready(authority);
    }
    return ok;
}

/* What a successful replay re-derived, kept for the coordinator's
 * derivation of the authority's plan and pre-sample context (#881 PR 3,
 * retirement_coordinator.c): the prepared attempt, the imported builds, the
 * projection, the row plan re-imported from its pin and the gate re-admitted
 * over the persisted row evidence. The gate and projection borrow from the
 * others, so they are released together (bq_retirement_unit_replayed_release). */
typedef struct BqRetirementUnitReplayed
{
    BqRetirementUnitPrepared prepared;
    BqRetirementUnitBuilt built;
    BqRetirementProjection projection;
    BqRetirementRowPlan plan;
    BqRetirementUnitGate gate;
    bool kept;
} BqRetirementUnitReplayed;

BUSTER_GLOBAL_LOCAL bool bq_retirement_unit_replayed_release(BqRetirementUnitReplayed* replayed)
{
    bool ok = true;
    if (replayed && replayed->kept)
    {
        bq_retirement_unit_gate_release(&replayed->gate);
        if (replayed->plan.owned && !bq_retirement_row_plan_release(&replayed->plan)) ok = false;
        if (!bq_retirement_projection_release(&replayed->projection)) ok = false;
        if (!bq_retirement_unit_built_release(&replayed->built)) ok = false;
        if (!bq_retirement_unit_release(&replayed->prepared)) ok = false;
    }
    if (replayed) replayed->kept = false;
    return ok;
}

/* bq_retirement_unit_replay_pinned; with `kept`, a successful replay moves
 * its re-derived objects there instead of releasing them. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_replay_kept(BqRetirementStore store, int workspaces, int installed,
    u64 job_id, u64 attempt_token, String8 workspace_root, String8 profile, String8 census_profile,
    char const* driver, char const* toolchain_root, char const* broker, char const* broker_workspaces,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitReplayed* kept)
{
    if (kept) kept->kept = false;
    BqRetirementUnitPrepared prepared = {.policy = {.clang = -1, .inventory = -1}};
    BqRetirementUnitBuilt built = {.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
    BqRetirementProjection projection = {0};
    char name[80];
    int named = bq_retirement_unit_hex(ready_sha256) ? snprintf(name, sizeof(name), "ready-%s", ready_sha256) : -1;
    BqError result = named > 0 && (size_t)named < sizeof(name) ? BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK)
        result = bq_retirement_unit_prepare_pinned(store, workspaces, installed, job_id, attempt_token, profile,
                                                   toolchain_root, preparation_sha256, &prepared);
    /* The sealed record, by its content address. */
    int ready = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared.job, NULL,
                                                                  BQ_RETIREMENT_UNIT_READY_DIRECTORY) : -1;
    u32 count = result == BQ_OK ? prepared.policy.template.reference_count : 0;
    u32 capacity = BQ_RETIREMENT_UNIT_READY_HEADER_CAP + BQ_RETIREMENT_UNIT_READY_ROW_CAP * count;
    char* text = result == BQ_OK ? malloc(capacity + 1u) : NULL;
    char* expected = result == BQ_OK ? malloc(capacity) : NULL;
    int* descriptors = count ? calloc(2u * (size_t)count, sizeof(*descriptors)) : NULL;
    BqRetirementOracleReference* references = count ? calloc(count, sizeof(*references)) : NULL;
    char (*outputs)[SHA256_HEX_CAPACITY] = count ? calloc(count, sizeof(*outputs)) : NULL;
    char (*commands)[SHA256_HEX_CAPACITY] = count ? calloc(count, sizeof(*commands)) : NULL;
    if (result == BQ_OK && !(text && expected && descriptors && references && outputs && commands)) result = BQ_IO;
    if (result == BQ_OK && !(ready >= 0 && bq_retirement_unit_closed(ready, NULL, name, true)))
        result = BQ_WORKSPACE_MISMATCH;
    u32 length = 0;
    if (result == BQ_OK) result = bq_record_read_at(ready, name, (u8*)text, capacity, &length);
    char digest[SHA256_HEX_CAPACITY] = {0}, binary_record[SHA256_HEX_CAPACITY] = {0};
    char build_record[SHA256_HEX_CAPACITY] = {0}, gate_seal[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK)
    {
        text[length] = 0;
        bq_digest(text, length, (char8*)digest);
        result = !memcmp(digest, ready_sha256, SHA256_HEX_CAPACITY) && strlen(text) == length &&
                 bq_retirement_unit_ready_value(text, "binaries=", binary_record) &&
                 bq_retirement_unit_ready_value(text, "matched-builds=", build_record) &&
                 bq_retirement_unit_ready_value(text, "gate=admitted ", gate_seal) &&
                 bq_retirement_unit_ready_rows(text, count, descriptors, commands) ? BQ_OK : BQ_CORRUPT;
    }
    if (result == BQ_OK)
        result = bq_retirement_unit_built_import(store, &prepared, workspaces, installed, workspace_root, profile,
            driver, toolchain_root, broker, broker_workspaces, binary_record, build_record, &built);
    if (result == BQ_OK)
        result = bq_retirement_unit_project_pinned(store, &prepared, &built, workspaces, installed, workspace_root,
            profile, census_profile, driver, toolchain_root, broker, broker_workspaces, &projection);
    int reference = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared.job,
        BQ_RETIREMENT_BUILD_WORK_DIRECTORY, BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY) : -1;
    if (result == BQ_OK)
        result = reference >= 0 && bq_retirement_unit_reference_observe(reference, &prepared, descriptors,
                                                                        references, outputs) ?
                 BQ_OK : BQ_SOURCE_MISMATCH;
    /* Each recorded command digest must be the one its descriptor numbers
     * rebuild. */
    for (u32 index = 0; result == BQ_OK && index < count; index += 1)
        if (strcmp(references[index].command_sha256, commands[index])) result = BQ_SOURCE_MISMATCH;
    BqRetirementOracleAuthority authority = {0};
    if (result == BQ_OK)
        result = bq_retirement_unit_replay_authority(&prepared, &projection, references, outputs, &authority) ?
                 BQ_OK : BQ_SOURCE_MISMATCH;
    /* The required-check authority for this attempt, re-imported, and the
     * sealed receipts a passing run of it produces. */
    BqRetirementRequiredChecks checks = {.hosted = -1};
    if (result == BQ_OK)
        result = bq_retirement_required_checks_import_profile(installed, profile, &prepared.job, &prepared.preparation,
                                                              &projection, &checks);
    int evidence = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared.job, NULL,
                                                                     BQ_RETIREMENT_UNIT_CHECKS_DIRECTORY) : -1;
    char receipts[SHA256_HEX_CAPACITY] = {0}, check_evidence[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK)
        result = evidence >= 0 && bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, true,
                                                                      check_evidence) &&
                 bq_retirement_check_receipts_hash(checks.checks, checks.count, receipts) ? BQ_OK : BQ_SOURCE_MISMATCH;
    /* The row plan, re-imported from its pin, and the correctness seal,
     * recomputed by rerunning the gate over the persisted row evidence and
     * the passing results the sealed receipts prove: neither is read from the
     * record. */
    BqRetirementRowPlan plan = {0};
    if (result == BQ_OK) result = bq_retirement_row_plan_import_profile(installed, profile, &prepared.job, &projection, &plan);
    int rows = result == BQ_OK ? bq_retirement_unit_attempt_open(workspaces, &prepared.job, NULL,
                                                                 BQ_RETIREMENT_ROWS_DIRECTORY) : -1;
    u32 rows_length = 0;
    char row_evidence[SHA256_HEX_CAPACITY] = {0};
    u8* rows_bytes = result == BQ_OK ? bq_retirement_unit_rows_read(rows, &rows_length, row_evidence) : NULL;
    if (result == BQ_OK && !rows_bytes) result = BQ_SOURCE_MISMATCH;
    BqRetirementRowObserved observed = {0};
    BqRetirementRowJoined joined = {0};
    if (result == BQ_OK) result = bq_retirement_row_observed_parse(rows_bytes, rows_length, &plan, &observed);
    if (result == BQ_OK) result = bq_retirement_row_evidence_join(&plan, &projection, &observed, &joined);
    BqRetirementCheckResult* passing = result == BQ_OK ? calloc(checks.count, sizeof(*passing)) : NULL;
    if (result == BQ_OK && !passing) result = BQ_IO;
    for (u32 index = 0; result == BQ_OK && index < checks.count; index += 1)
    {
        BqRetirementRequiredCheck const* required = checks.checks + index;
        BqRetirementCheckResult* check = passing + index;
        *check = (BqRetirementCheckResult){.kind = required->kind, .target = required->target, .rows = required->rows};
        memcpy(check->command_sha256, required->command_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->configuration_sha256, required->configuration_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->receipt_sha256, required->receipt_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->preparation_sha256, checks.preparation_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->source_sha256, checks.source_sha256, sizeof(check->source_sha256));
        memcpy(check->binary_sha256, checks.binary_sha256, sizeof(check->binary_sha256));
    }
    BqRetirementUnitGate gate = {0};
    if (result == BQ_OK) result = bq_retirement_unit_gate_admit(&projection, &checks, passing, &joined.evidence, &gate);
    BqRetirementUnitReadyFacts facts = {.job = &prepared.job, .projection = &projection, .authority = &authority,
        .descriptors = descriptors, .binary_record_sha256 = built.binary_record_sha256,
        .build_record_sha256 = built.build_record_sha256, .template_sha256 = prepared.policy.template_sha256,
        .inventory_sha256 = prepared.policy.inventory_sha256, .checks_authority_sha256 = checks.authority_sha256,
        .receipts_sha256 = receipts, .evidence_sha256 = check_evidence, .plan_sha256 = plan.authority_sha256,
        .correctness_sha256 = gate.correctness.sealed_sha256, .row_evidence_sha256 = row_evidence,
        .check_count = checks.count};
    if (result == BQ_OK)
        result = bq_retirement_unit_attempt_sha(&prepared.job, facts.attempt_sha256) &&
                 bq_retirement_unit_gate_sealed(gate_seal, &facts) ? BQ_OK : BQ_RECIPE_MISMATCH;
    memcpy(facts.gate_sha256, gate_seal, SHA256_HEX_CAPACITY);
    u32 expected_length = 0;
    if (result == BQ_OK)
        result = bq_retirement_unit_ready_format(&facts, expected, capacity, &expected_length) &&
                 expected_length == length && !memcmp(expected, text, length) ? BQ_OK : BQ_CORRUPT;
    /* A kept replay hands over what it re-derived; the locals are left in
     * their released state. */
    if (result == BQ_OK && kept)
    {
        /* The frozen batch groups the correctness gate points into move with
         * it (as bq_retirement_unit_gate_pinned keeps them), and the seal the
         * replay just verified against the re-derived facts marks it issued
         * for the derivation's command builder, which requires that marker. */
        gate.joined = joined;
        joined = (BqRetirementRowJoined){0};
        memcpy(gate.evidence_sha256, check_evidence, SHA256_HEX_CAPACITY);
        memcpy(gate.row_evidence_sha256, row_evidence, SHA256_HEX_CAPACITY);
        memcpy(gate.seal_sha256, gate_seal, SHA256_HEX_CAPACITY);
        gate.issuer = BQ_RETIREMENT_UNIT_GATE_ISSUED;
        *kept = (BqRetirementUnitReplayed){prepared, built, projection, plan, gate, true};
        prepared = (BqRetirementUnitPrepared){.policy = {.clang = -1, .inventory = -1}};
        built = (BqRetirementUnitBuilt){.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
        projection = (BqRetirementProjection){0};
        plan = (BqRetirementRowPlan){0};
        gate = (BqRetirementUnitGate){0};
    }
    bq_retirement_unit_gate_release(&gate);
    bq_retirement_row_joined_release(&joined);
    bq_retirement_row_observed_release(&observed);
    bq_retirement_row_plan_release(&plan);
    free(passing);
    free(rows_bytes);
    if (rows >= 0 && close(rows) != 0 && result == BQ_OK) result = BQ_IO;
    if (evidence >= 0 && close(evidence) != 0 && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_required_checks_release(&checks) && result == BQ_OK) result = BQ_IO;
    if (reference >= 0 && close(reference) != 0 && result == BQ_OK) result = BQ_IO;
    if (ready >= 0 && close(ready) != 0 && result == BQ_OK) result = BQ_IO;
    free(commands);
    free(outputs);
    free(references);
    free(descriptors);
    free(expected);
    free(text);
    if (!bq_retirement_projection_release(&projection) && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_unit_built_release(&built) && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_unit_release(&prepared) && result == BQ_OK) result = BQ_IO;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_unit_replay_pinned(BqRetirementStore store, int workspaces, int installed,
    u64 job_id, u64 attempt_token, String8 workspace_root, String8 profile, String8 census_profile,
    char const* driver, char const* toolchain_root, char const* broker, char const* broker_workspaces,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY])
{
    BqError result = bq_retirement_unit_replay_kept(store, workspaces, installed, job_id, attempt_token, workspace_root,
        profile, census_profile, driver, toolchain_root, broker, broker_workspaces, preparation_sha256, ready_sha256,
        NULL);
    return result;
}

BqError bq_retirement_unit_replay(BqRetirementStore store, int workspaces, int installed, u64 job_id,
    u64 attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY])
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BqError result = bq_retirement_unit_replay_pinned(store, workspaces, installed, job_id, attempt_token,
        S8(BQ_RETIREMENT_STAGE_WORKSPACE_ROOT), profile, S8("full-census"), BQ_RETIREMENT_BUILD_DRIVER,
        BQ_RETIREMENT_TOOLCHAIN_ROOT, BQ_RETIREMENT_UNIT_BROKER, BQ_RETIREMENT_STAGE_WORKSPACE_ROOT,
        preparation_sha256, ready_sha256);
    return result;
}
