/* Coordinator side of the retirement worker-unit (#881, PR 4 of 4).
 *
 * Ownership: the steps the coordinator (bq_worker_run in worker_linux.c)
 * performs for a retirement job around the phase channel. The unit side is
 * retirement_worker_unit.c. Linked only into the service translation unit,
 * after retirement_worker_unit.c and before worker_linux.c.
 *
 * Entry points (all reached only from worker_linux.c):
 *   bq_retirement_request_valid_pinned   the request gate: the portable
 *                                        queue's field checks, and the
 *                                        retirement recipe only when
 *                                        bq_retirement_profile_complete
 *                                        accepts the profile
 *   bq_retirement_coordinator_budget_load
 *                                        the installed campaign-budget
 *                                        record, read-only, whose bytes must
 *                                        hash to the profile's
 *                                        campaign-budget-sha256= pin
 *   bq_retirement_coordinator_handoff    before the MEASURED acknowledgement:
 *                                        the producer's receipt authority,
 *                                        named by the digest the MEASURED
 *                                        packet carried, copied and journalled
 *                                        into the queue-private root
 *                                        (tp_retirement_store_authority_handoff)
 *   bq_retirement_coordinator_replay     at finalization: bq_retirement_unit_
 *                                        replay_pinned over the attempt
 *                                        workspace with the ready digest the
 *                                        RETIREMENT_READY packet carried
 *
 * Map: bq_retirement_coordinator_authority_read parses the producer's
 * canonical BQ-RETIREMENT-AUTHORITY-V3 record from
 * job-<id>-attempt-<token>/retirement-authority/ and requires its bytes to
 * hash to the channel's digest and to re-format byte for byte; the handoff
 * then reopens the receipt, its shards and the retained manifest itself.
 * bq_retirement_coordinator_queue_root opens (creating once, 0700, fsynced)
 * the queue directory's retirement-authority/.
 *
 * Production passes the compiled profile (bq_retirement_worker_unit_installed),
 * which bq_retirement_profile_complete refuses and which pins no campaign
 * budget, so every entry here refuses a production retirement job. Only the
 * worker's _pinned seams pass a complete fixture profile.
 */
#include "../throughput/retirement_store.h"

/* The operator-installed record, read-only under <installed>/recipes/. */
#define BQ_RETIREMENT_COORDINATOR_BUDGET_NAME "native-retirement-performance-v1.campaign-budget"
/* Equal to BQ_WORKER_BUDGET_BYTES (checked in worker_linux.c). */
#define BQ_RETIREMENT_COORDINATOR_BUDGET_CAP 4096u
/* The producer's private authority root in its attempt workspace; PR 3's
 * tp_retirement_store_receipt_authority call publishes there. */
#define BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY "retirement-authority"
/* The coordinator's queue-private copy root, inside the queue directory,
 * which the unit cannot reach. */
#define BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY "retirement-authority"
#define BQ_RETIREMENT_COORDINATOR_AUTHORITY_MAGIC "BQ-RETIREMENT-AUTHORITY-V3"
/* The authority record: magic, job label, attempt and five digests. */
#define BQ_RETIREMENT_COORDINATOR_AUTHORITY_LINES 8u
#define BQ_RETIREMENT_COORDINATOR_AUTHORITY_CAP 640u
/* The context chain (bq_retirement_context_chain_format). */
#define BQ_RETIREMENT_COORDINATOR_CHAIN_MAGIC "BQ-RETIREMENT-CONTEXT-CHAIN-V1"
#define BQ_RETIREMENT_COORDINATOR_CHAIN_CAP 512u

BUSTER_GLOBAL_LOCAL bool bq_retirement_request_valid_pinned(BqRequest const* request, String8 profile)
{
    bool valid = request && bq_request_valid_admitting(request, bq_retirement_profile_complete(profile));
    return valid;
}

/* Returns BQ_OK with *loaded naming `record`'s bytes, or BQ_RECIPE_MISMATCH
 * when the profile pins no budget or the installed bytes are another
 * record's, or BQ_CONFIGURATION_MISMATCH when the installed file is missing,
 * writable, linked, oversized or not the service's. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_budget_load(String8 installed_root, String8 profile,
    char record[BQ_RETIREMENT_COORDINATOR_BUDGET_CAP], String8* loaded)
{
    char pin[SHA256_HEX_CAPACITY] = {0}, digest[SHA256_HEX_CAPACITY] = {0};
    if (loaded) *loaded = (String8){0};
    BqError result = record && loaded && installed_root.length && installed_root.pointer[0] == '/' ? BQ_OK :
                     BQ_BAD_REQUEST;
    if (result == BQ_OK && !bq_retirement_profile_sha(profile, S8("campaign-budget-sha256="), pin))
        result = BQ_RECIPE_MISMATCH;
    int installed = result == BQ_OK ? bq_open_absolute_directory(installed_root) : -1;
    int recipes = installed >= 0 ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    u8* bytes = NULL;
    u32 length = 0;
    if (result == BQ_OK)
        result = recipes >= 0 && bq_owned_directory(recipes, false, true) &&
                 bq_retirement_reference_read_installed(recipes, BQ_RETIREMENT_COORDINATOR_BUDGET_NAME,
                     BQ_RETIREMENT_COORDINATOR_BUDGET_CAP - 1u, &bytes, &length, digest, NULL) ?
                 BQ_OK : BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK && memcmp(digest, pin, SHA256_HEX_CAPACITY)) result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
    {
        memcpy(record, bytes, length);
        record[length] = 0;
        *loaded = (String8){(char8*)record, length};
    }
    free(bytes);
    if (recipes >= 0 && close(recipes) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    if (installed >= 0 && close(installed) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    return result;
}

/* The producer's authority record for job/attempt from its private root: a
 * single-link, owner-read-only regular file whose bytes hash to digest and
 * are the canonical record of the fields they spell. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_coordinator_authority_read(int authority, u64 job_id, u64 attempt_token,
    char const digest[SHA256_HEX_CAPACITY], TpRetirementReceiptAuthority* trusted)
{
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], label[TP_RETIREMENT_STORE_TOKEN_CAPACITY];
    char body[BQ_RETIREMENT_COORDINATOR_AUTHORITY_CAP + 1], canonical[BQ_RETIREMENT_COORDINATOR_AUTHORITY_CAP];
    char actual[SHA256_HEX_CAPACITY] = {0};
    if (trusted) *trusted = (TpRetirementReceiptAuthority){0};
    bool ok = authority >= 0 && trusted && digest && bq_retirement_hex(string_from_pointer(digest), 64) &&
              tp_retirement_store_job_label(label, job_id) && attempt_token;
    int named = ok ? snprintf(name, sizeof(name), "authority-%s-%" PRIu64 ".txt", label, (uint64_t)attempt_token) : -1;
    int file = named > 0 && (size_t)named < sizeof(name) ?
               openat(authority, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    u32 length = 0;
    ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
         info.st_uid == geteuid() && (info.st_mode & 0777) == 0400 && info.st_size > 0 &&
         (u64)info.st_size <= BQ_RETIREMENT_COORDINATOR_AUTHORITY_CAP &&
         bq_read_file(file, (u8*)body, (u32)info.st_size, &length) && length == (u32)info.st_size;
    if (file >= 0 && close(file) != 0) ok = false;
    if (ok)
    {
        bq_digest(body, length, (char8*)actual);
        ok = !memcmp(actual, digest, SHA256_HEX_CAPACITY);
    }
    /* Split the eight LF-terminated lines in place. */
    char* lines[BQ_RETIREMENT_COORDINATOR_AUTHORITY_LINES] = {0};
    u32 count = 0, start = 0;
    for (u32 index = 0; ok && index < length; index += 1)
    {
        if (body[index] == '\n')
        {
            ok = count < BQ_RETIREMENT_COORDINATOR_AUTHORITY_LINES;
            if (ok)
            {
                body[index] = 0;
                lines[count] = body + start;
                count += 1;
                start = index + 1;
            }
        }
    }
    ok = ok && count == BQ_RETIREMENT_COORDINATOR_AUTHORITY_LINES && start == length &&
         !strcmp(lines[0], BQ_RETIREMENT_COORDINATOR_AUTHORITY_MAGIC) && !strcmp(lines[1], label);
    u64 attempt = 0;
    ok = ok && bq_retirement_number(string_from_pointer(lines[2]), &attempt) && attempt == attempt_token;
    for (u32 index = 3; ok && index < BQ_RETIREMENT_COORDINATOR_AUTHORITY_LINES; index += 1)
        ok = bq_retirement_hex(string_from_pointer(lines[index]), 64);
    int formatted = ok ? snprintf(canonical, sizeof(canonical), BQ_RETIREMENT_COORDINATOR_AUTHORITY_MAGIC
                                  "\n%s\n%" PRIu64 "\n%s\n%s\n%s\n%s\n%s\n", label, (uint64_t)attempt_token,
                                  lines[3], lines[4], lines[5], lines[6], lines[7]) : -1;
    /* Re-joined, the lines must be the record: a leading zero or any other
     * non-canonical spelling of the attempt is refused. */
    for (u32 index = 0; ok && index < count; index += 1) lines[index][strlen(lines[index])] = '\n';
    ok = ok && formatted > 0 && (u32)formatted == length && !memcmp(canonical, body, length);
    if (ok)
    {
        snprintf(trusted->job, sizeof(trusted->job), "%s", label);
        trusted->attempt = attempt_token;
        memcpy(trusted->plan_sha256, lines[3], 64);
        memcpy(trusted->context_sha256, lines[4], 64);
        memcpy(trusted->receipt_sha256, lines[5], 64);
        memcpy(trusted->identity_sha256, lines[6], 64);
        memcpy(trusted->retained_sha256, lines[7], 64);
        memcpy(trusted->authority_sha256, digest, 64);
    }
    else if (trusted) *trusted = (TpRetirementReceiptAuthority){0};
    return ok;
}

/* The queue directory's retirement-authority/, created 0700 on first use and
 * made durable before any copy is published into it. */
BUSTER_GLOBAL_LOCAL int bq_retirement_coordinator_queue_root(int queue_directory)
{
    int root = queue_directory >= 0 ? openat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY,
                                             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (root < 0 && queue_directory >= 0 && errno == ENOENT &&
        mkdirat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY, 0700) == 0 && fsync(queue_directory) == 0)
        root = openat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    return root;
}

/* The canonical BQ-RETIREMENT-CONTEXT-CHAIN-V1 record binding the
 * authority's plan and final context to facts the coordinator holds itself:
 * the job and attempt, its own A digest (preparation), the ready digest the
 * RETIREMENT_READY packet carried, and the pinned row-plan authority (which
 * the replayed ready record's row-plan= also names). PR 3's producer writes
 * exactly these bytes as `context-chain-job-<id>-<token>.txt` (0400) in its
 * private authority root beside the authority record. Returns the length, or
 * 0 when a field is not a lowercase digest. */
BUSTER_GLOBAL_LOCAL u32 bq_retirement_context_chain_format(char chain[BQ_RETIREMENT_COORDINATOR_CHAIN_CAP],
    u64 job_id, u64 attempt_token, char const* preparation_sha256, char const* ready_sha256,
    char const* row_plan_sha256, char const* plan_sha256, char const* context_sha256)
{
    char const* digests[] = {preparation_sha256, ready_sha256, row_plan_sha256, plan_sha256, context_sha256};
    bool ok = chain && job_id && attempt_token;
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(digests); index += 1)
        ok = digests[index] && bq_retirement_hex(string_from_pointer(digests[index]), 64);
    int length = ok ? snprintf(chain, BQ_RETIREMENT_COORDINATOR_CHAIN_CAP, BQ_RETIREMENT_COORDINATOR_CHAIN_MAGIC
                               "\njob=job-%" PRIu64 "\nattempt=%" PRIu64 "\npreparation=%s\nready=%s\nrow-plan=%s"
                               "\nplan=%s\ncontext=%s\n", (uint64_t)job_id, (uint64_t)attempt_token,
                               preparation_sha256, ready_sha256, row_plan_sha256, plan_sha256, context_sha256) : -1;
    u32 result = length > 0 && length < (int)BQ_RETIREMENT_COORDINATOR_CHAIN_CAP ? (u32)length : 0;
    return result;
}

/* The producer's authority record, named by the channel's digest, and its
 * context chain, which must be byte for byte the chain the coordinator
 * formats from its own A digest, the channel's ready digest and the profile's
 * row-plan pin together with that authority's plan and context. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_coordinator_chain_check(int authority, u64 job_id, u64 attempt_token,
    String8 profile, char const* preparation_sha256, char const* ready_sha256,
    char const authority_sha256[SHA256_HEX_CAPACITY], TpRetirementReceiptAuthority* trusted)
{
    char row_plan[SHA256_HEX_CAPACITY] = {0}, name[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char expected[BQ_RETIREMENT_COORDINATOR_CHAIN_CAP], stored[BQ_RETIREMENT_COORDINATOR_CHAIN_CAP];
    bool ok = bq_retirement_coordinator_authority_read(authority, job_id, attempt_token, authority_sha256, trusted) &&
              bq_retirement_profile_sha(profile, S8("row-plan-sha256="), row_plan);
    u32 length = ok ? bq_retirement_context_chain_format(expected, job_id, attempt_token, preparation_sha256,
                                                         ready_sha256, row_plan, trusted->plan_sha256,
                                                         trusted->context_sha256) : 0;
    int named = length ? snprintf(name, sizeof(name), "context-chain-job-%" PRIu64 "-%" PRIu64 ".txt",
                                  (uint64_t)job_id, (uint64_t)attempt_token) : -1;
    int file = named > 0 && (size_t)named < sizeof(name) ?
               openat(authority, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    u32 read_length = 0;
    ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
         info.st_uid == geteuid() && (info.st_mode & 0777) == 0400 && (u64)info.st_size == length &&
         bq_read_file(file, (u8*)stored, length, &read_length) && read_length == length &&
         !memcmp(stored, expected, length);
    if (file >= 0 && close(file) != 0) ok = false;
    if (!ok && trusted) *trusted = (TpRetirementReceiptAuthority){0};
    return ok;
}

/* The attempt's private authority root, opened read-only. */
BUSTER_GLOBAL_LOCAL int bq_retirement_coordinator_authority_root(String8 workspace_root, u64 job_id, u64 attempt_token)
{
    char attempt_name[64];
    bool named = workspace_root.length && workspace_root.pointer[0] == '/' &&
                 bq_workspace_name(attempt_name, job_id, attempt_token);
    int workspaces = named ? bq_open_absolute_directory(workspace_root) : -1;
    int attempt = workspaces >= 0 ? openat(workspaces, attempt_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) :
                  -1;
    int authority = attempt >= 0 ? openat(attempt, BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY,
                                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (attempt >= 0) close(attempt);
    if (workspaces >= 0) close(workspaces);
    return authority;
}

/* Called by bq_worker_phase_accept for a version-2 MEASURED, before its
 * acknowledgement and before its receipt. result_directory is the attempt's
 * result root (the composer's store root); authority_sha256 is the digest the
 * packet carried; profile, preparation_sha256 and ready_sha256 are the
 * coordinator's own (bq_retirement_coordinator_chain_check). A missing or
 * foreign chain refuses before the queue-private root exists and before any
 * copy or journal. BQ_WORKER_MISMATCH: no authority or chain matches, or the
 * store's handoff refuses (another identity, a changed receipt or retained
 * file, an earlier copy or journal). BQ_IO: a root cannot be opened. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_handoff(int result_directory, String8 workspace_root,
    int queue_directory, u64 job_id, u64 attempt_token, String8 profile, char const* preparation_sha256,
    char const* ready_sha256, char const authority_sha256[SHA256_HEX_CAPACITY])
{
    TpRetirementReceiptAuthority trusted = {0};
    TpRetirementAuthorityJournal journal = {0};
    int authority = result_directory >= 0 ? bq_retirement_coordinator_authority_root(workspace_root, job_id,
                                                                                     attempt_token) : -1;
    BqError result = authority >= 0 ? BQ_OK : BQ_IO;
    if (result == BQ_OK && !bq_retirement_coordinator_chain_check(authority, job_id, attempt_token, profile,
                                                                  preparation_sha256, ready_sha256, authority_sha256,
                                                                  &trusted))
        result = BQ_WORKER_MISMATCH;
    int queue_root = result == BQ_OK ? bq_retirement_coordinator_queue_root(queue_directory) : -1;
    if (result == BQ_OK && queue_root < 0) result = BQ_IO;
    if (result == BQ_OK &&
        !tp_retirement_store_authority_handoff(result_directory, authority, queue_root, job_id, attempt_token,
                                               trusted.plan_sha256, trusted.context_sha256, &trusted, &journal))
        result = BQ_WORKER_MISMATCH;
    if (queue_root >= 0 && close(queue_root) != 0 && result == BQ_OK) result = BQ_IO;
    if (authority >= 0 && close(authority) != 0 && result == BQ_OK) result = BQ_IO;
    return result;
}

/* At finalization: the authority the MEASURED packet named is still the one
 * the coordinator copied and journalled. Its queue-private copy must hash to
 * authority_sha256, the producer's chain must still bind it to the
 * coordinator's facts, and tp_retirement_store_authority_state must classify
 * the handoff COMPLETE against the result. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_authority_complete(int result_directory,
    String8 workspace_root, int queue_directory, u64 job_id, u64 attempt_token, String8 profile,
    char const* preparation_sha256, char const* ready_sha256, char const authority_sha256[SHA256_HEX_CAPACITY])
{
    TpRetirementReceiptAuthority trusted = {0}, copied = {0};
    int authority = result_directory >= 0 ? bq_retirement_coordinator_authority_root(workspace_root, job_id,
                                                                                     attempt_token) : -1;
    int queue_root = queue_directory >= 0 ? openat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY,
                                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BqError result = authority >= 0 && queue_root >= 0 ? BQ_OK : BQ_WORKER_MISMATCH;
    if (result == BQ_OK &&
        !(bq_retirement_coordinator_chain_check(authority, job_id, attempt_token, profile, preparation_sha256,
                                                ready_sha256, authority_sha256, &trusted) &&
          bq_retirement_coordinator_authority_read(queue_root, job_id, attempt_token, authority_sha256, &copied) &&
          tp_retirement_store_authority_state(result_directory, queue_root, job_id, attempt_token, copied.plan_sha256,
                                              copied.context_sha256, &copied) == TP_RETIREMENT_AUTHORITY_COMPLETE))
        result = BQ_WORKER_MISMATCH;
    if (queue_root >= 0 && close(queue_root) != 0 && result == BQ_OK) result = BQ_IO;
    if (authority >= 0 && close(authority) != 0 && result == BQ_OK) result = BQ_IO;
    return result;
}

/* Called at finalization with the coordinator's own A digest and the ready
 * digest from the channel. seams names the profile and roots the replay
 * uses; production passes the installed seams, whose blocked profile fails
 * the replay's authority imports. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_replay(BqRetirementWorkerUnitSeams const* seams,
    String8 workspace_root, u64 job_id, u64 attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY])
{
    BqRetirementStore store = {-1};
    bool usable = seams && seams->installed_root && seams->broker_workspaces && workspace_root.length &&
                  workspace_root.pointer[0] == '/' && preparation_sha256 && ready_sha256 &&
                  bq_retirement_hex(string_from_pointer(ready_sha256), 64);
    int workspaces = usable ? bq_open_absolute_directory(workspace_root) : -1;
    int installed = workspaces >= 0 ? bq_open_absolute_directory(string_from_pointer(seams->installed_root)) : -1;
    BqError result = !usable ? BQ_BAD_REQUEST : installed >= 0 ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_unit_store_open(workspaces, job_id, attempt_token, &store);
    if (result == BQ_OK)
        result = bq_retirement_unit_replay_pinned(store, workspaces, installed, job_id, attempt_token,
            string_from_pointer(seams->broker_workspaces), seams->profile, seams->census_profile, seams->driver,
            seams->toolchain_root, seams->broker, seams->broker_workspaces, preparation_sha256, ready_sha256);
    if (store.directory >= 0 && close(store.directory) != 0 && result == BQ_OK) result = BQ_IO;
    if (installed >= 0 && close(installed) != 0 && result == BQ_OK) result = BQ_IO;
    if (workspaces >= 0 && close(workspaces) != 0 && result == BQ_OK) result = BQ_IO;
    return result;
}
