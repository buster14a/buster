/* Coordinator side of the retirement worker-unit (#881, PRs 3 and 4 of 4).
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
 *                                        packet carried and bound by its
 *                                        context chain to the coordinator's
 *                                        own facts, copied and journalled
 *                                        into the queue-private root
 *                                        (tp_retirement_store_authority_handoff)
 *   bq_retirement_coordinator_replay     at finalization: bq_retirement_unit_
 *                                        replay_pinned over the attempt
 *                                        workspace with the ready digest the
 *                                        RETIREMENT_READY packet carried
 *   bq_retirement_coordinator_authority_complete
 *                                        at finalization: the journalled copy
 *                                        of the authority MEASURED named,
 *                                        classified COMPLETE
 *   bq_retirement_coordinator_finalize   at finalization: the replay, the
 *                                        journalled authority and the
 *                                        derivation of its plan and context
 *                                        (#881 PR 3, review item M1 of #1961)
 *   bq_retirement_coordinator_handoff_class
 *                                        at recovery (L2): the MEASURED
 *                                        handoff classified complete,
 *                                        absent, incomplete or inconsistent
 *                                        from the worker-phase-4 record and
 *                                        tp_retirement_store_authority_state
 *                                        over the queue-private entries;
 *                                        read-only
 *
 * Recovery map: bq_retirement_coordinator_handoff_entries finds the copy,
 * the journal and their `.pending` temporaries by the store's names, and
 * bq_retirement_coordinator_copy_digest hashes a copy left without its
 * worker-phase-4 record so that copy can be classified too.
 *
 * Map: bq_retirement_coordinator_authority_read parses the producer's
 * canonical BQ-RETIREMENT-AUTHORITY-V3 record from
 * job-<id>-attempt-<token>/retirement-authority/ and requires its bytes to
 * hash to the channel's digest and to re-format byte for byte; the handoff
 * then reopens the receipt, its shards and the retained manifest itself.
 * bq_retirement_coordinator_chain_check requires the producer's
 * BQ-RETIREMENT-CONTEXT-CHAIN-V2 record (retirement_context_chain.h) to be
 * exactly the one the coordinator formats from its own A digest, the
 * channel's ready digest and the profile's row-plan pin, the authority's plan
 * and context, and the values the chain carries.
 * bq_retirement_coordinator_derive then refuses to trust the plan, the
 * context or the binding: it recomputes lane D's five documents from the
 * replayed gate and row plan, the pinned profile, budget, rows and
 * untimed-command contract and the result's admission receipt
 * (BqRetirementCoordinatorDocuments, bq_retirement_coordinator_documents),
 * recomputes the pre-sample context from its own A digest (in the replayed
 * gate), the ready digest, its own boot identity and the result's sealed
 * untimed record stream, recomputes the post-sample context from the carried
 * pre-sample context and stage facts, finds lane D's post-sample record
 * through the retained manifest the authority binds, renders the binding
 * itself from the pinned binding context and those documents
 * (bq_retirement_worker_binding_render) and requires the result's bytes, and
 * recomputes the final context from it and the A/B stage's numeric digest.
 * bq_retirement_coordinator_authority_root opens the producer's root and
 * bq_retirement_coordinator_queue_root opens (creating once, 0700, fsynced)
 * the queue directory's retirement-authority/, only after the chain check.
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
/* The coordinator's queue-private copy root, inside the queue directory,
 * which the unit cannot reach. */
#define BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY "retirement-authority"
#define BQ_RETIREMENT_COORDINATOR_AUTHORITY_MAGIC "BQ-RETIREMENT-AUTHORITY-V3"
/* The authority record: magic, job label, attempt and five digests. */
#define BQ_RETIREMENT_COORDINATOR_AUTHORITY_LINES 8u
#define BQ_RETIREMENT_COORDINATOR_AUTHORITY_CAP 640u
/* Bounds of the result-root files the derivation reads: the retained
 * manifest (TP_RETIREMENT_RETAINED_MANIFEST_HEADER and one line per store
 * file) and the binding (one bundle file). */
#define BQ_RETIREMENT_COORDINATOR_RETAINED_CAP (UINT32_C(2) << 20)
#define BQ_RETIREMENT_COORDINATOR_BINDING_CAP ((u32)BQ_WORKER_BUNDLE_FILE_CAP)
#define BQ_RETIREMENT_COORDINATOR_ARENA_BYTES (UINT64_C(1) << 32)

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

/* The producer's authority record, named by the channel's digest, and its
 * context chain, which must be byte for byte the chain the coordinator
 * formats from its own A digest, the channel's ready digest and the profile's
 * row-plan pin together with that authority's plan and context and the
 * values the stored chain carries (*carried, when given, receives them). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_coordinator_chain_check(int authority, u64 job_id, u64 attempt_token,
    String8 profile, char const* preparation_sha256, char const* ready_sha256,
    char const authority_sha256[SHA256_HEX_CAPACITY], TpRetirementReceiptAuthority* trusted,
    BqRetirementContextChainCarried* carried)
{
    char row_plan[SHA256_HEX_CAPACITY] = {0}, name[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char expected[BQ_RETIREMENT_CONTEXT_CHAIN_CAP], stored[BQ_RETIREMENT_CONTEXT_CHAIN_CAP];
    BqRetirementContextChainCarried values = {0};
    bool ok = bq_retirement_coordinator_authority_read(authority, job_id, attempt_token, authority_sha256, trusted) &&
              bq_retirement_profile_sha(profile, S8("row-plan-sha256="), row_plan) &&
              bq_retirement_context_chain_name(name, job_id, attempt_token);
    int file = ok ? openat(authority, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    u32 read_length = 0;
    ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
         info.st_uid == geteuid() && (info.st_mode & 0777) == 0400 && info.st_size > 0 &&
         (u64)info.st_size < BQ_RETIREMENT_CONTEXT_CHAIN_CAP &&
         bq_read_file(file, (u8*)stored, (u32)info.st_size, &read_length) && read_length == (u32)info.st_size &&
         bq_retirement_context_chain_parse(stored, read_length, &values);
    if (file >= 0 && close(file) != 0) ok = false;
    u32 length = ok ? bq_retirement_context_chain_format(expected, job_id, attempt_token, preparation_sha256,
                                                         ready_sha256, row_plan, trusted->plan_sha256,
                                                         trusted->context_sha256, &values) : 0;
    ok = ok && length == read_length && !memcmp(stored, expected, length);
    if (!ok && trusted) *trusted = (TpRetirementReceiptAuthority){0};
    if (carried) *carried = ok ? values : (BqRetirementContextChainCarried){0};
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
                                                                  &trusted, NULL))
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
 * the handoff COMPLETE against the result. *journalled and *carried, when
 * given, receive the copied authority and the chain's carried values. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_authority_complete(int result_directory, String8 workspace_root,
    int queue_directory, u64 job_id, u64 attempt_token, String8 profile, char const* preparation_sha256,
    char const* ready_sha256, char const authority_sha256[SHA256_HEX_CAPACITY], TpRetirementReceiptAuthority* journalled,
    BqRetirementContextChainCarried* carried)
{
    TpRetirementReceiptAuthority trusted = {0}, copied = {0};
    int authority = result_directory >= 0 ? bq_retirement_coordinator_authority_root(workspace_root, job_id,
                                                                                     attempt_token) : -1;
    int queue_root = queue_directory >= 0 ? openat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY,
                                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BqError result = authority >= 0 && queue_root >= 0 ? BQ_OK : BQ_WORKER_MISMATCH;
    if (result == BQ_OK &&
        !(bq_retirement_coordinator_chain_check(authority, job_id, attempt_token, profile, preparation_sha256,
                                                ready_sha256, authority_sha256, &trusted, carried) &&
          bq_retirement_coordinator_authority_read(queue_root, job_id, attempt_token, authority_sha256, &copied) &&
          tp_retirement_store_authority_state(result_directory, queue_root, job_id, attempt_token, copied.plan_sha256,
                                              copied.context_sha256, &copied) == TP_RETIREMENT_AUTHORITY_COMPLETE))
        result = BQ_WORKER_MISMATCH;
    if (queue_root >= 0 && close(queue_root) != 0 && result == BQ_OK) result = BQ_IO;
    if (authority >= 0 && close(authority) != 0 && result == BQ_OK) result = BQ_IO;
    if (journalled) *journalled = result == BQ_OK ? copied : (TpRetirementReceiptAuthority){0};
    if (carried && result != BQ_OK) *carried = (BqRetirementContextChainCarried){0};
    return result;
}

/* Called at finalization with the coordinator's own A digest and the ready
 * digest from the channel. seams names the profile and roots the replay
 * uses; production passes the installed seams, whose blocked profile fails
 * the replay's authority imports. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_replay(BqRetirementWorkerUnitSeams const* seams,
    String8 workspace_root, u64 job_id, u64 attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY], BqRetirementUnitReplayed* kept)
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
        result = bq_retirement_unit_replay_kept(store, workspaces, installed, job_id, attempt_token,
            string_from_pointer(seams->broker_workspaces), seams->profile, seams->census_profile, seams->driver,
            seams->toolchain_root, seams->broker, seams->broker_workspaces, preparation_sha256, ready_sha256, kept);
    if (store.directory >= 0 && close(store.directory) != 0 && result == BQ_OK) result = BQ_IO;
    if (installed >= 0 && close(installed) != 0 && result == BQ_OK) result = BQ_IO;
    if (workspaces >= 0 && close(workspaces) != 0 && result == BQ_OK) result = BQ_IO;
    return result;
}

/* --------------------------------------------------------- recovery (L2) */

/* How recovery classifies one attempt's MEASURED handoff from the
 * worker-phase-4 record and tp_retirement_store_authority_state. COMPLETE
 * needs both; ABSENT needs neither and nothing in the queue-private root;
 * INCOMPLETE is a copy, a journal or a `.pending` temporary without the record
 * (a handoff interrupted before it finished); INCONSISTENT is the record
 * without a COMPLETE state, or a COMPLETE state without the record. The record
 * is written only after the handoff completes, so recovery cannot tell a crash
 * between the two from forged records: both are never acknowledged, and
 * bq_worker_retirement_handoff_hold poisons and holds INCOMPLETE and
 * INCONSISTENT alike. */
typedef enum BqRetirementHandoffClass
{
    BQ_RETIREMENT_HANDOFF_ABSENT,
    BQ_RETIREMENT_HANDOFF_COMPLETE,
    BQ_RETIREMENT_HANDOFF_INCOMPLETE,
    BQ_RETIREMENT_HANDOFF_INCONSISTENT,
} BqRetirementHandoffClass;

/* The queue-private entries one attempt's handoff can leave, named as
 * tp_retirement_store_authority_handoff names them: the copy, the journal and
 * each one's `.pending` temporary. *present is any of them, or a lookup that
 * failed other than ENOENT (fail closed); *copied is the copy's final name. */
BUSTER_GLOBAL_LOCAL void bq_retirement_coordinator_handoff_entries(int queue_root, u64 job_id, u64 attempt_token,
                                                                    bool* present, bool* copied)
{
    char label[TP_RETIREMENT_STORE_TOKEN_CAPACITY], name[TP_RETIREMENT_STORE_PATH_BYTES + 16];
    char const* const suffixes[] = {".txt", ".journal", ".txt.pending", ".journal.pending"};
    bool labelled = tp_retirement_store_job_label(label, job_id) && attempt_token;
    *present = !labelled;
    *copied = false;
    for (u32 index = 0; labelled && index < BUSTER_ARRAY_LENGTH(suffixes); index += 1)
    {
        int named = snprintf(name, sizeof(name), "authority-%s-%" PRIu64 "%s", label, (uint64_t)attempt_token,
                             suffixes[index]);
        struct stat info = {0};
        errno = 0;
        bool found = named > 0 && (size_t)named < sizeof(name) &&
                     fstatat(queue_root, name, &info, AT_SYMLINK_NOFOLLOW) == 0;
        bool missing = !found && named > 0 && (size_t)named < sizeof(name) && errno == ENOENT;
        if (!missing) *present = true;
        if (found && index == 0) *copied = true;
    }
}

/* The digest of the queue-private copy's bytes, under the bounds
 * bq_retirement_coordinator_authority_read applies. Recovery uses it only
 * without a worker-phase-4 record, to classify the copy the handoff left. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_coordinator_copy_digest(int queue_root, u64 job_id, u64 attempt_token,
                                                               char digest[SHA256_HEX_CAPACITY])
{
    char label[TP_RETIREMENT_STORE_TOKEN_CAPACITY], name[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char body[BQ_RETIREMENT_COORDINATOR_AUTHORITY_CAP];
    bool ok = tp_retirement_store_job_label(label, job_id) && attempt_token;
    int named = ok ? snprintf(name, sizeof(name), "authority-%s-%" PRIu64 ".txt", label, (uint64_t)attempt_token) : -1;
    int file = named > 0 && (size_t)named < sizeof(name) ?
               openat(queue_root, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    u32 length = 0;
    ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
         (u64)info.st_size <= BQ_RETIREMENT_COORDINATOR_AUTHORITY_CAP &&
         bq_read_file(file, (u8*)body, (u32)info.st_size, &length) && length == (u32)info.st_size;
    if (file >= 0 && close(file) != 0) ok = false;
    if (ok) bq_digest(body, length, (char8*)digest);
    digest[ok ? 64 : 0] = 0;
    return ok;
}

/* Recovery's classification of job/attempt's handoff (the L2 input of
 * bq_worker_recover). measured says whether the queue's worker-phase-4 record
 * exists and authority_sha256 is its digest (empty when malformed). With the
 * record, the copy it names is classified; without it, the copy the handoff
 * left (bq_retirement_coordinator_copy_digest), if any. result_directory is the
 * attempt's result root (the store the copy must reopen against). *state
 * receives what tp_retirement_store_authority_state reported, or INVALID when
 * no copy could be read under a digest (ABSENT when nothing exists). Nothing is
 * created or changed. */
BUSTER_GLOBAL_LOCAL BqRetirementHandoffClass bq_retirement_coordinator_handoff_class(int result_directory,
    int queue_directory, u64 job_id, u64 attempt_token, bool measured,
    char const authority_sha256[SHA256_HEX_CAPACITY], TpRetirementAuthorityState* state)
{
    char digest[SHA256_HEX_CAPACITY] = {0};
    TpRetirementReceiptAuthority copied = {0};
    errno = 0;
    int queue_root = queue_directory >= 0 ? openat(queue_directory, BQ_RETIREMENT_COORDINATOR_QUEUE_AUTHORITY,
                                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    /* A root that cannot be opened for any reason but its absence proves
     * nothing absent. */
    bool present = queue_root < 0 && (queue_directory < 0 || errno != ENOENT);
    bool copy = false;
    if (queue_root >= 0) bq_retirement_coordinator_handoff_entries(queue_root, job_id, attempt_token, &present, &copy);
    if (measured && authority_sha256 && bq_retirement_hex(string_from_pointer(authority_sha256), 64))
        memcpy(digest, authority_sha256, SHA256_HEX_CAPACITY);
    else if (!measured && copy) bq_retirement_coordinator_copy_digest(queue_root, job_id, attempt_token, digest);
    bool parsed = queue_root >= 0 && digest[0] &&
                  bq_retirement_coordinator_authority_read(queue_root, job_id, attempt_token, digest, &copied);
    TpRetirementAuthorityState observed = parsed ?
        tp_retirement_store_authority_state(result_directory, queue_root, job_id, attempt_token, copied.plan_sha256,
                                            copied.context_sha256, &copied) :
        present ? TP_RETIREMENT_AUTHORITY_INVALID : TP_RETIREMENT_AUTHORITY_ABSENT;
    /* A root that fails to close proves nothing either. */
    if (queue_root >= 0 && close(queue_root) != 0)
    {
        observed = TP_RETIREMENT_AUTHORITY_INVALID;
        present = true;
    }
    bool complete = observed == TP_RETIREMENT_AUTHORITY_COMPLETE;
    BqRetirementHandoffClass handoff = measured ? (complete ? BQ_RETIREMENT_HANDOFF_COMPLETE :
                                                   BQ_RETIREMENT_HANDOFF_INCONSISTENT) :
                                       complete ? BQ_RETIREMENT_HANDOFF_INCONSISTENT :
                                       present ? BQ_RETIREMENT_HANDOFF_INCOMPLETE : BQ_RETIREMENT_HANDOFF_ABSENT;
    if (state) *state = observed;
    return handoff;
}

/* ------------------------------------------------------------ derivation */

/* A single-link, service-owned regular file of the result root, read whole
 * (at most `cap` bytes, NUL-terminated after them); *digest receives its
 * SHA-256. */
BUSTER_GLOBAL_LOCAL char* bq_retirement_coordinator_result_read(Arena* arena, int result_directory, char const* name,
    u32 cap, u32* length, char digest[SHA256_HEX_CAPACITY])
{
    int file = result_directory >= 0 ? openat(result_directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    bool ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              info.st_uid == geteuid() && info.st_size > 0 && (u64)info.st_size <= cap;
    char* bytes = ok ? bq_retirement_worker_allocate(arena, (u64)info.st_size + 1u, 1) : NULL;
    u32 used = 0;
    ok = ok && bytes && bq_read_file(file, (u8*)bytes, (u32)info.st_size, &used) && used == (u32)info.st_size;
    if (file >= 0 && close(file) != 0) ok = false;
    if (ok)
    {
        bytes[used] = 0;
        bq_digest(bytes, used, (char8*)digest);
    }
    *length = ok ? used : 0;
    return ok ? bytes : NULL;
}

/* The 64-hex value of the `\n<key>` line of a NUL-free record. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_coordinator_record_digest(char const* text, char const* key,
    char digest[SHA256_HEX_CAPACITY])
{
    char pattern[48];
    int length = snprintf(pattern, sizeof(pattern), "\n%s", key);
    char const* found = length > 0 && (size_t)length < sizeof(pattern) ? strstr(text, pattern) : NULL;
    char const* value = found ? found + length : NULL;
    bool ok = value && strlen(value) > 64 && value[64] == '\n' && !strstr(value, pattern);
    if (ok)
    {
        memcpy(digest, value, 64);
        digest[64] = 0;
        ok = tp_retirement_digest(digest);
    }
    return ok;
}

/* Exactly one `\n<key>` line in a NUL-free record, and it is
 * `\n<key><value>\n`. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_coordinator_record_line(char const* text, char const* key, char const* value)
{
    char line[256], pattern[64];
    int length = snprintf(line, sizeof(line), "\n%s%s\n", key, value);
    int key_length = snprintf(pattern, sizeof(pattern), "\n%s", key);
    char const* found = length > 0 && (size_t)length < sizeof(line) ? strstr(text, line) : NULL;
    bool ok = found && key_length > 0 && (size_t)key_length < sizeof(pattern) && strstr(text, pattern) == found &&
              !strstr(found + 1, pattern);
    return ok;
}

/* Lane D's five workflow documents, recomputed from what the coordinator
 * derives itself: the replayed gate and row plan, the pinned profile's
 * campaign values, budget record, performance rows and untimed-command
 * contract, and the admission receipt's digest. The untimed batches are
 * rebuilt as the unit builds them (bq_retirement_worker_untimed_build, which
 * writes no response file here), and each untimed compile row's reproduction
 * is the gate's sealed artifact, which the untimed step must have reproduced
 * (a code row whose reproduction differs refuses the campaign). As
 * bq_retirement_unit_campaign_documents_derive does before timing, the
 * pinned performance rows and the budget are the campaign's, the family's
 * counts the plan's, and the untimed batches four per untimed group; the
 * documents are only encoded and hashed. *plan and d_plan receive the #619
 * plan and lane D's campaign plan digest. */
typedef struct BqRetirementCoordinatorDocuments
{
    BqRetirementUnitCampaignDocumentSet set;
    BqRetirementDocumentDescriptor documents[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS];
} BqRetirementCoordinatorDocuments;

BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_documents(Arena* arena, int installed, String8 profile,
    BqRetirementUnitReplayed const* replayed, char const admission_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitCampaignPins* pins, TpRetirementPlan* plan, char d_plan[SHA256_HEX_CAPACITY],
    BqRetirementCoordinatorDocuments* derived)
{
    BqRetirementCorrectness const* gate = &replayed->gate.correctness;
    BqRetirementUnitCampaignDocumentSet* set = &derived->set;
    TpRetirementCampaignBudget budget = {0};
    BqRetirementDocumentPopulation population = {0};
    BqRetirementDocumentPartition timed = {0}, untimed = {0};
    BqRetirementWorkerUntimedContract contract = {0};
    BqRetirementWorkerUntimed build = {0};
    char performance[65] = {0}, budget_sha256[65] = {0};
    memset(derived, 0, sizeof(*derived));
    BqError result = replayed->kept && admission_sha256 && tp_retirement_digest(admission_sha256) &&
                     bq_retirement_unit_campaign_pins(profile, pins) &&
                     bq_retirement_unit_campaign_plan(gate, pins, plan) &&
                     bq_retirement_unit_campaign_plan_digest(gate, plan, d_plan) &&
                     bq_retirement_unit_campaign_document_pins(profile, set->support, set->manifest, set->rows,
                                                               performance) ?
                     BQ_OK : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_worker_budget_load(installed, profile, &budget);
    if (result == BQ_OK && !(tp_retirement_budget_digest(&budget, budget_sha256) &&
                             !strcmp(budget_sha256, pins->budget_sha256)))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
        result = bq_retirement_documents_population(installed, profile, gate->trusted_rows, gate->prepared.rows,
                                                    gate->prepared.native_target, &population);
    if (result == BQ_OK && !(!strcmp(performance, population.performance_rows_sha256) &&
                             bq_retirement_documents_partition(&population, 0, &timed) &&
                             bq_retirement_documents_partition(&population, 1, &untimed) &&
                             bq_retirement_documents_family(&population, &timed, &set->family) &&
                             set->family.bootstrap_members == plan->bootstrap_members_per_scope &&
                             set->family.cell_members == plan->cell_members_per_scope &&
                             bq_retirement_unit_campaign_document_timed_rows(&population, &timed, set->timed_rows)))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
        result = bq_retirement_worker_untimed_import(arena, installed, profile, &replayed->plan, &untimed, &contract);
    if (result == BQ_OK &&
        !(bq_retirement_worker_untimed_build(arena, BQ_RETIREMENT_WORKER_DERIVE_ONLY, &replayed->plan, gate, &untimed,
                                             &contract, &budget, &build) &&
          build.groups == untimed.count))
        result = BQ_RECIPE_MISMATCH;
    /* The untimed compile rows' code facts, each reproduction the gate's. */
    TpRetirementCodeRow* codes = result == BQ_OK ? bq_retirement_worker_allocate(arena, population.count + 1u,
                                                                                 sizeof(*codes)) : NULL;
    unsigned code_count = 0;
    if (result == BQ_OK && !codes) result = BQ_IO;
    for (u32 row = 0; result == BQ_OK && row < population.count; row += 1)
    {
        if (bq_retirement_document_timed(&population, row) || !population.rows[row].compile) continue;
        TpRetirementCodeRow* code = codes + code_count++;
        code->row = row;
        for (u32 side = 0; side < 2; side += 1)
        {
            memcpy(code->sides[side].artifact_sha256, gate->facts[row].side[side].artifact_sha256, 65);
            memcpy(code->sides[side].reproduction_sha256, gate->facts[row].side[side].artifact_sha256, 65);
        }
    }
    BqRetirementDocumentInputs inputs = {gate, &population, &timed, &untimed, &budget, plan, build.batches,
        4u * build.groups, build.rows, codes, code_count, (int)replayed->plan.cpu, set->support, set->manifest,
        set->rows};
    BqRetirementDocumentDescriptor* documents = derived->documents;
    BqRetirementDocumentPhase phase = {&set->family, documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256,
        bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN],
        &documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN], documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256,
        admission_sha256};
    if (result == BQ_OK &&
        !(bq_retirement_documents_oracle(NULL, &inputs, &documents[BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE]) &&
          bq_retirement_documents_execution_plan(NULL, &inputs, &documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN]) &&
          bq_retirement_documents_result_input_plan(NULL, &inputs, set->partitions, set->counts,
                                                    &documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN]) &&
          bq_retirement_documents_phase(NULL, &inputs, &phase, 0, &documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE]) &&
          bq_retirement_documents_phase(NULL, &inputs, &phase, 1, &documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA])))
        result = BQ_RECIPE_MISMATCH;
    bq_retirement_worker_untimed_release(&build);
    bq_retirement_documents_partition_release(&untimed);
    bq_retirement_documents_partition_release(&timed);
    bq_retirement_documents_population_release(&population);
    return result;
}

/* Review items M1 of #1961 and of #1964: the coordinator never trusts the
 * authority's plan or context, the chain's naming of them, nor the binding
 * the chain names. Over its own replay (`replayed`, from the A digest it
 * holds and the ready digest the channel carried) it requires, in order:
 *   1. lane D's five documents, recomputed (bq_retirement_coordinator_documents
 *      over the result's admission receipt), the authority's plan being the
 *      execution-plan document;
 *   2. the carried pre-sample context to be the one it recomputes from the
 *      replayed gate (which binds its A digest), the ready digest, lane D's
 *      plan digest, the pinned budget, the campaign commands rebuilt from the
 *      replayed row plan, the result's untimed record stream as the store
 *      sealed it, its own boot identity (so a reboot between MEASURED and
 *      finalization refuses, and a durable success is held), the job, the
 *      attempt, the row plan's CPU and the carried bind time;
 *   3. the carried post-sample context to descend from that pre-sample
 *      context (bq_retirement_unit_campaign_post_digest over the carried log
 *      chains and stage facts);
 *   4. lane D's post-sample record, found through the retained manifest the
 *      authority binds, to name the same job, attempt, plans, contexts, log
 *      chains, admission receipt, family and timed rows, and the recomputed
 *      pre-sample plan, result-input plan and post-A/A documents;
 *   5. the binding the chain names to be, byte for byte, the one it renders
 *      itself (bq_retirement_worker_binding_render) from the pinned binding
 *      context, checked against the replayed gate and plan
 *      (bq_retirement_worker_binding_check), the recomputed documents and the
 *      admission receipt, and the authority's context to be the validator's
 *      _execution_context of that binding over the A/B stage's numeric
 *      digest.
 * What it cannot re-observe it binds by digest: the bind time, the stage
 * facts and log chains (measurement outputs, which the post-sample record
 * and the receipt also bind), the untimed records' contents and the fixture
 * admission receipt. BQ_WORKER_MISMATCH on any refusal. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_derive(BqRetirementWorkerUnitSeams const* seams,
    BqRetirementUnitReplayed const* replayed, u64 job_id, u64 attempt_token, char const ready_sha256[SHA256_HEX_CAPACITY],
    TpRetirementReceiptAuthority const* trusted, BqRetirementContextChainCarried const* carried, int result_directory)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BQ_RETIREMENT_COORDINATOR_ARENA_BYTES,
                                                .flags = {.no_pool = 1}});
    int installed = seams && seams->installed_root ? bq_open_absolute_directory(string_from_pointer(seams->installed_root)) :
                    -1;
    BqRetirementCorrectness const* gate = replayed ? &replayed->gate.correctness : NULL;
    BqRetirementUnitCampaignPins pins = {0};
    TpRetirementPlan plan = {0};
    BqRetirementCoordinatorDocuments* derived = arena ? bq_retirement_worker_allocate(arena, 1, sizeof(*derived)) : NULL;
    char d_plan[SHA256_HEX_CAPACITY] = {0};
    BqError result = arena && derived && installed >= 0 && replayed && replayed->kept && trusted && carried &&
                     ready_sha256 && result_directory >= 0 ? BQ_OK : BQ_BAD_REQUEST;
    /* 1. The documents over the result's admission receipt, and the plan. */
    u32 admission_length = 0;
    char admission_sha256[SHA256_HEX_CAPACITY] = {0};
    char* admission = result == BQ_OK ? bq_retirement_coordinator_result_read(arena, result_directory,
        BQ_RETIREMENT_WORKER_ADMISSION_PATH, BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX, &admission_length,
        admission_sha256) : NULL;
    if (result == BQ_OK && !admission) result = BQ_WORKER_MISMATCH;
    if (result == BQ_OK)
        result = bq_retirement_coordinator_documents(arena, installed, seams->profile, replayed, admission_sha256, &pins,
                                                     &plan, d_plan, derived);
    BqRetirementDocumentDescriptor const* documents = derived ? derived->documents : NULL;
    if (result == BQ_OK && strcmp(documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN].sha256, trusted->plan_sha256))
        result = BQ_WORKER_MISMATCH;
    /* 2. The pre-sample context from the coordinator's own facts. */
    BqRetirementCampaignPlanCommands commands = {0};
    char measured[SHA256_HEX_CAPACITY] = {0}, label[TP_RETIREMENT_STORE_TOKEN_CAPACITY] = {0};
    char boot[TP_RETIREMENT_STORE_TOKEN_CAPACITY] = {0}, pre[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK &&
        !(bq_retirement_campaign_plan_commands(&replayed->plan, &replayed->gate, &commands) &&
          bq_retirement_unit_campaign_commands_measured(commands.commands, commands.commands + commands.count,
                                                        commands.count, measured) &&
          tp_retirement_store_job_label(label, job_id) && bq_retirement_worker_boot(boot)))
        result = BQ_RECIPE_MISMATCH;
    u32 length = 0;
    char untimed_sha256[SHA256_HEX_CAPACITY] = {0};
    char* untimed = result == BQ_OK ? bq_retirement_coordinator_result_read(arena, result_directory,
        BQ_RETIREMENT_WORKER_UNTIMED_PATH, (u32)TP_RETIREMENT_STORE_FILE_BYTES, &length, untimed_sha256) : NULL;
    if (result == BQ_OK && !untimed) result = BQ_WORKER_MISMATCH;
    TpRetirementShard shard = {.bytes = length};
    memcpy(shard.sha256, untimed_sha256, SHA256_HEX_CAPACITY);
    for (u32 index = 0; result == BQ_OK && index < length; index += 1) shard.records += untimed[index] == '\n';
    BqRetirementUnitCampaignFacts facts = {d_plan, ready_sha256, pins.budget_sha256, measured, &shard, label, boot,
                                           attempt_token, carried ? carried->bound_at_ns : 0,
                                           replayed ? (int)replayed->plan.cpu : -1};
    if (result == BQ_OK && !(bq_retirement_unit_campaign_pre_context(gate, &facts, pre) &&
                             !strcmp(pre, carried->pre_sample)))
        result = BQ_WORKER_MISMATCH;
    bq_retirement_campaign_plan_commands_release(&commands);
    /* 3. The post-sample context descends from the carried pre-sample one. */
    char post[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK &&
        !(bq_retirement_unit_campaign_post_digest(carried->pre_sample, d_plan, carried->logs, carried->stages, post) &&
          !strcmp(post, carried->post_sample)))
        result = BQ_WORKER_MISMATCH;
    /* 4. Lane D's post-sample record, bound by the authority's retained
     * manifest. */
    char retained_sha256[SHA256_HEX_CAPACITY] = {0}, record_sha256[SHA256_HEX_CAPACITY] = {0};
    char listed[128];
    u32 retained_length = 0, record_length = 0;
    char* retained = result == BQ_OK ? bq_retirement_coordinator_result_read(arena, result_directory,
        TP_RETIREMENT_RETAINED_MANIFEST_PATH, BQ_RETIREMENT_COORDINATOR_RETAINED_CAP, &retained_length,
        retained_sha256) : NULL;
    char* record = result == BQ_OK && retained && !strcmp(retained_sha256, trusted->retained_sha256) &&
                   !strncmp(retained, TP_RETIREMENT_RETAINED_MANIFEST_HEADER,
                            strlen(TP_RETIREMENT_RETAINED_MANIFEST_HEADER)) ?
                   bq_retirement_coordinator_result_read(arena, result_directory, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD,
                       BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX, &record_length, record_sha256) : NULL;
    int line = record ? snprintf(listed, sizeof(listed), "\n%s %s %u " BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD "\n",
                                 bq_retirement_unit_handoff_declared[0].kind, record_sha256, record_length) : -1;
    char attempt_text[24];
    snprintf(attempt_text, sizeof(attempt_text), "%" PRIu64, (uint64_t)attempt_token);
    bool linked = line > 0 && (size_t)line < sizeof(listed) && strstr(retained, listed) &&
                  strlen(record) == record_length && bq_retirement_coordinator_record_line(record, "job=", label) &&
                  bq_retirement_coordinator_record_line(record, "attempt=", attempt_text);
    static char const* const keys[] = {"pre-sample=", "post-sample=", "plan=", "execution-plan=", "log-untimed=",
                                       "log-aa=", "log-ab=", "pre-sample-plan=", "result-input-plan=",
                                       "post-aa-binding=", "aa-admission=", "family=", "timed-rows="};
    char const* expected[BUSTER_ARRAY_LENGTH(keys)] = {0};
    if (linked)
    {
        char const* values[] = {carried->pre_sample, carried->post_sample, d_plan, trusted->plan_sha256,
            carried->logs[0], carried->logs[1], carried->logs[2],
            documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256,
            documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256,
            documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].sha256, admission_sha256, derived->set.family.sha256,
            derived->set.timed_rows};
        memcpy(expected, values, sizeof(values));
    }
    for (u32 index = 0; linked && index < BUSTER_ARRAY_LENGTH(keys); index += 1)
    {
        char value[SHA256_HEX_CAPACITY] = {0};
        linked = bq_retirement_coordinator_record_digest(record, keys[index], value) && !strcmp(value, expected[index]);
    }
    if (result == BQ_OK && !linked) result = BQ_WORKER_MISMATCH;
    /* 5. The binding, rendered by the coordinator, and the final context. */
    char binding_sha256[SHA256_HEX_CAPACITY] = {0}, context_sha256[SHA256_HEX_CAPACITY] = {0};
    u32 binding_length = 0;
    char* binding = result == BQ_OK ? bq_retirement_coordinator_result_read(arena, result_directory,
        BQ_RETIREMENT_WORKER_BINDING_PATH, BQ_RETIREMENT_COORDINATOR_BINDING_CAP, &binding_length, binding_sha256) : NULL;
    BqRetirementWorkerBindingContext pinned = {0};
    if (result == BQ_OK && !(binding && !strcmp(binding_sha256, carried->binding))) result = BQ_WORKER_MISMATCH;
    if (result == BQ_OK &&
        bq_retirement_worker_binding_import(arena, installed, seams->profile, &pinned) != BQ_OK)
        result = BQ_RECIPE_MISMATCH;
    char* rendered = NULL;
    u64 rendered_length = 0;
    if (result == BQ_OK &&
        !(bq_retirement_worker_binding_check(&pinned, seams->profile, gate, derived->set.family.sha256, &pins, &plan) &&
          bq_retirement_worker_binding_render(&pinned, documents, admission_length, admission_sha256, arena, &rendered,
                                              &rendered_length) &&
          rendered_length == binding_length && !memcmp(rendered, binding, binding_length)))
        result = BQ_WORKER_MISMATCH;
    char* context = NULL;
    size_t context_length = 0;
    if (result == BQ_OK &&
        !tp_retirement_compose_execution_context((unsigned char const*)rendered, (size_t)rendered_length,
                                                 carried->stages[1].raw, arena, &context, &context_length))
        result = BQ_WORKER_MISMATCH;
    if (result == BQ_OK) bq_digest(context, (u32)context_length, (char8*)context_sha256);
    if (result == BQ_OK && strcmp(context_sha256, trusted->context_sha256)) result = BQ_WORKER_MISMATCH;
    bq_retirement_worker_binding_release(&pinned);
    if (installed >= 0 && close(installed) != 0 && result == BQ_OK) result = BQ_IO;
    if (arena) arena_destroy(arena, 1);
    return result;
}

/* At finalization, in order: the replay of the attempt from the A digest and
 * the ready digest, the journalled authority the MEASURED packet named, and
 * the derivation of that authority's plan and context
 * (bq_retirement_coordinator_derive). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_coordinator_finalize(BqRetirementWorkerUnitSeams const* seams,
    String8 workspace_root, int result_directory, int queue_directory, u64 job_id, u64 attempt_token,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY],
    char const authority_sha256[SHA256_HEX_CAPACITY])
{
    BqRetirementUnitReplayed replayed = {0};
    TpRetirementReceiptAuthority journalled = {0};
    BqRetirementContextChainCarried carried = {0};
    BqError result = bq_retirement_coordinator_replay(seams, workspace_root, job_id, attempt_token,
                                                           preparation_sha256, ready_sha256, &replayed);
    if (result == BQ_OK)
        result = bq_retirement_coordinator_authority_complete(result_directory, workspace_root, queue_directory, job_id,
            attempt_token, seams->profile, preparation_sha256, ready_sha256, authority_sha256, &journalled, &carried);
    if (result == BQ_OK)
        result = bq_retirement_coordinator_derive(seams, &replayed, job_id, attempt_token, ready_sha256, &journalled,
                                                  &carried, result_directory);
    if (!bq_retirement_unit_replayed_release(&replayed) && result == BQ_OK) result = BQ_IO;
    return result;
}
