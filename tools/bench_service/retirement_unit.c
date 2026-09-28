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
 *
 * Map: bq_retirement_unit_store_closed checks the sealed export's exact
 * contents; bq_retirement_unit_prepare_pinned is the profile seam that the
 * public wrapper calls with the compiled profile and fixed toolchain root.
 *
 * The retirement recipe stays unadmitted and bq_worker_unit does not call
 * prepare yet. Its result is input for the later B producer, not a
 * correctness verdict or build provenance.
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
