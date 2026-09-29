/* Linux service-owned durable result publication for #1023.
 * The bench service owns the directory descriptor, workspace and authority
 * handoff. begin/publish create and seal one file without replacement;
 * validate rereads every sealed inode. receipt_authority returns a reference
 * for the private service channel, never an authority embedded in the bundle.
 * read/settle/bound/retain serve the #881-E composer (retirement_compose.h),
 * which also publishes the retained manifest the authority binds;
 * authority_handoff/authority_state are the coordinator's copy-and-journal
 * call site before its final ACK.
 * Definitions live in tools/bench_service/retirement_result.c.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_STORE_H
#define BUSTER_THROUGHPUT_RETIREMENT_STORE_H
#ifdef __linux__
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#define TP_RETIREMENT_STORE_FILES 4096u
#define TP_RETIREMENT_STORE_PATH_BYTES 192u
#define TP_RETIREMENT_STORE_FILE_BYTES UINT64_C(67108864)
#define TP_RETIREMENT_STORE_TOTAL_BYTES (UINT64_C(128) * 1024 * 1024 * 1024)
/* Every receipt shard but the last holds exactly this many records. It must
 * equal TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS (checked in retirement_campaign.h). */
#define TP_RETIREMENT_STORE_RECEIPT_SHARD_RECORDS 65536u
#define TP_RETIREMENT_EXECUTION_RECEIPT_PATH "retirement-execution-receipt.json"
/* The composer's inventory of every retained (unsealed) store file. */
#define TP_RETIREMENT_RETAINED_MANIFEST_PATH "retirement-retained-manifest.txt"
#define TP_RETIREMENT_RETAINED_MANIFEST_HEADER "BQ-RETIREMENT-RETAINED-V1\n"
/* A SHA-256 hex string plus NUL, and a job/boot token plus NUL. */
#define TP_RETIREMENT_STORE_SHA256_CAPACITY 65u
#define TP_RETIREMENT_STORE_TOKEN_CAPACITY 129u
/* The authority field for an attempt without a retained manifest. */
#define TP_RETIREMENT_STORE_NO_RETAINED "0000000000000000000000000000000000000000000000000000000000000000"

typedef struct TpRetirementStoredFile
{
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char sha256[65];
    uint64_t bytes;
    dev_t device;
    ino_t inode;
    dev_t parent_device;
    ino_t parent_inode;
    uid_t owner;
    struct timespec changed;
} TpRetirementStoredFile;

typedef struct TpRetirementStore
{
    int root;
    struct stat root_identity;
    TpRetirementStoredFile* files;
    unsigned count, capacity, planned_files, external_entries;
    /* The part of planned_files reserved for upper-bounded kinds (metrics
     * and untimed shards, failure logs); settle may release only this. */
    unsigned bounded_files;
    uint64_t total, planned_bytes, external_bytes;
    int active, failed, authority_issued, planned, retained_bound;
    /* The digest of the retained-file declaration fixed before timing. */
    char retained_sha256[TP_RETIREMENT_STORE_SHA256_CAPACITY];
} TpRetirementStore;

typedef struct TpRetirementPending
{
    FILE* stream;
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char temporary[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    dev_t parent_device;
    ino_t parent_inode;
    uint64_t limit;
} TpRetirementPending;

typedef struct TpRetirementReceiptAuthority
{
    /* The service must persist and send this on its authenticated private
     * channel. Copying these fields into a bundle does not authenticate it. */
    char job[TP_RETIREMENT_STORE_TOKEN_CAPACITY];
    char plan_sha256[TP_RETIREMENT_STORE_SHA256_CAPACITY], context_sha256[TP_RETIREMENT_STORE_SHA256_CAPACITY];
    char receipt_sha256[TP_RETIREMENT_STORE_SHA256_CAPACITY], identity_sha256[TP_RETIREMENT_STORE_SHA256_CAPACITY];
    /* The retained manifest's digest (TP_RETIREMENT_STORE_NO_RETAINED when
     * the attempt published none): a fresh consumer reopens every file it
     * lists, so the unsealed A/A evidence is bound by the authority too. */
    char retained_sha256[TP_RETIREMENT_STORE_SHA256_CAPACITY];
    char authority_sha256[TP_RETIREMENT_STORE_SHA256_CAPACITY];
    uint64_t attempt;
} TpRetirementReceiptAuthority;

int tp_retirement_store_open(TpRetirementStore* store, int root,
                             TpRetirementStoredFile* workspace, unsigned capacity);
/* Account for every service-owned payload and all directory/control entries
 * the bundle validator will inventory outside this store. owned_bytes is the
 * checked byte budget for the owned files; publish rejects a file that would
 * exceed it. The caller supplies a conservative byte reservation for the
 * external files; owned plus external entries and bytes must each fit the
 * store ceilings (see tp_retirement_campaign_store_preflight). */
int tp_retirement_store_plan(TpRetirementStore* store, unsigned owned_files, uint64_t owned_bytes,
                             unsigned external_entries, uint64_t external_bytes);
/* After plan and before any publication: mark how many planned files are
 * upper-bound slack (bounded kinds) and bind the retained-file declaration.
 * Each may be set once. */
int tp_retirement_store_bound(TpRetirementStore* store, unsigned bounded_files);
int tp_retirement_store_retain(TpRetirementStore* store, char const* declaration_sha256);
int tp_retirement_store_begin(TpRetirementStore* store, char const* path,
                              uint64_t limit, TpRetirementPending* pending);
int tp_retirement_store_publish(TpRetirementStore* store, TpRetirementPending* pending,
                                uint64_t bytes, char const* sha256);
void tp_retirement_store_abort(TpRetirementStore* store, TpRetirementPending* pending);
int tp_retirement_store_validate(TpRetirementStore* store);
int tp_retirement_store_receipt_authority(TpRetirementStore* store, int authority_root, char const* path,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority* authority);
int tp_retirement_store_authority_matches(TpRetirementStore* store, int authority_root, char const* path,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority const* trusted);
/* Fresh export/replay consumer: trusted must arrive on the authenticated
 * control channel; the result bundle cannot supply it. This reopens the
 * private reference, receipt and every receipt shard without producer memory.
 * The private reference binds the original receipt and shard inode identities,
 * so byte-identical replacement at the service root also fails readback.
 * The complete result bundle still needs the service's separate manifest and
 * full-tree validation before an experiment can be acknowledged. */
int tp_retirement_store_authority_reopen(int result_root, int authority_root,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority const* trusted);
/* The service calls this only after the producer has issued `trusted` for the
 * final post-sample context. Independently reopen its original receipt and
 * ordered shards, durably publish the same canonical private authority record
 * in a distinct queue-private directory without replacement, then reopen the
 * result using that copy. The caller keeps the trusted fields outside the
 * bundle and journals the queue reference before acknowledging the attempt.
 * A failed copy leaves a pending or sealed collision; it is never retried by
 * overwriting or deleting that evidence, and a copy whose name or temporary
 * already exists is refused before anything is created. */
int tp_retirement_store_authority_copy(int result_root, int authority_root,
    int queue_authority_root, char const* job, uint64_t attempt,
    char const* plan_sha256, char const* final_context_sha256,
    TpRetirementReceiptAuthority const* trusted);
/* Open one sealed store file read-only after rechecking its recorded parent,
 * inode, size, owner, mode and change time. Returns the descriptor (the
 * caller closes it) or -1; entry receives the sealed record. The caller must
 * hash what it reads and compare it with entry->sha256. */
int tp_retirement_store_read(TpRetirementStore* store, char const* path, TpRetirementStoredFile const** entry);
/* The plan reserves upper bounds before timing (metrics shard counts are only
 * bounded, never exact). Once every remaining output is known, settle lowers
 * the reservation to the exact final inventory; validate then requires
 * exactly that many files. It never raises the reservation, and it may release
 * at most the bounded slack: an exact kind (a retained or sealed file) can
 * never be settled away. */
int tp_retirement_store_settle(TpRetirementStore* store, unsigned exact_files);
/* The `job-<id>` label of the queue's numeric job identity. */
static inline int tp_retirement_store_job_label(char output[TP_RETIREMENT_STORE_TOKEN_CAPACITY], uint64_t job)
{
    int length = output && job ? snprintf(output, TP_RETIREMENT_STORE_TOKEN_CAPACITY, "job-%" PRIu64, job) : -1;
    int valid = length > 0 && length < (int)TP_RETIREMENT_STORE_TOKEN_CAPACITY;
    if (!valid && output) output[0] = 0;
    return valid;
}

/* The coordinator's authority call site (#1023 / #1021). Call it in the
 * worker only after the private phase handoff has been authenticated, with
 * the numeric job and attempt that handoff carried and the producer-issued
 * authority it delivered; the authority's job must be `job-<id>`. It copies
 * the authority into the queue-private root (tp_retirement_store_authority_copy),
 * then durably publishes one journal record beside it without replacement and
 * reopens both. Only after it returns 1 may the worker send its final ACK and
 * release the lease. Any failure leaves the published evidence in place; a
 * retry never overwrites or deletes it: when the copy, the journal or either
 * `.pending` temporary already exists it refuses before creating anything,
 * so a retry cannot degrade a complete handoff. The copy's authority binds
 * the retained manifest, so the handoff reopens every retained A/A file. */
typedef struct TpRetirementAuthorityJournal
{
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char sha256[65];
    uint64_t bytes;
} TpRetirementAuthorityJournal;

typedef enum TpRetirementAuthorityState
{
    TP_RETIREMENT_AUTHORITY_INVALID,
    TP_RETIREMENT_AUTHORITY_ABSENT,
    TP_RETIREMENT_AUTHORITY_INCOMPLETE,
    TP_RETIREMENT_AUTHORITY_DAMAGED,
    TP_RETIREMENT_AUTHORITY_COMPLETE
} TpRetirementAuthorityState;

int tp_retirement_store_authority_handoff(int result_root, int authority_root, int queue_authority_root,
    uint64_t authenticated_job, uint64_t authenticated_attempt,
    char const* plan_sha256, char const* final_context_sha256,
    TpRetirementReceiptAuthority const* trusted, TpRetirementAuthorityJournal* journal);
/* Restart classification of the queue-private root for one attempt:
 * COMPLETE when the copied authority and its journal both reopen against the
 * result; INCOMPLETE for a crash prefix (one name without the other, or a
 * `.pending` temporary left by the link/unlink window); DAMAGED when both
 * final names exist without a temporary but either fails to reopen (later
 * tampering or loss of the result); ABSENT when nothing exists. INCOMPLETE and
 * DAMAGED attempts must be poisoned with their evidence retained. It never
 * repairs or completes a handoff. */
TpRetirementAuthorityState tp_retirement_store_authority_state(int result_root, int queue_authority_root,
    uint64_t authenticated_job, uint64_t authenticated_attempt,
    char const* plan_sha256, char const* final_context_sha256,
    TpRetirementReceiptAuthority const* trusted);
void tp_retirement_store_close(TpRetirementStore* store);
#endif
#endif
