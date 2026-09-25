/* Queue/materializer/supervisor software for #437. No recipe execution,
 * transport or benchmark qualification lives here. queue.c owns persistence and all state changes;
 * protocol.c is the bounded control boundary. See README.md before extending.
 */
#ifndef BUSTER_BENCH_SERVICE_QUEUE_H
#define BUSTER_BENCH_SERVICE_QUEUE_H
#include <buster/lib/string.h>
#include <buster/lib/hash.h>
#include <buster/lib/system_headers.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define BQ_SCHEMA_LEGACY 1u
#define BQ_SCHEMA_MATERIALIZATION 2u
#define BQ_SCHEMA 3u
#define BQ_CONTROL_SCHEMA 2u
#define BQ_PENDING_CAP 8u
#define BQ_JOB_CAP 64u
#define BQ_EVENT_CAP (BQ_JOB_CAP * 16u)
#define BQ_REQUEST_CAP 320u
#define BQ_HEADER_SIZE 160u
#define BQ_JOURNAL_BODY_CAP 512u
#define BQ_RECORD_CAP (BQ_HEADER_SIZE + BQ_JOURNAL_BODY_CAP)
#define BQ_FIELD_COUNT 5u
#define BQ_PATH_CAP 192u
#define BQ_RECIPE_NAME_CAP 48u
#define BQ_RECIPE_FILE_CAP 80u
#define BQ_RECIPE_COMMAND_CAP 48u
#define BQ_RECIPE_PROFILE_CAP 1024u

typedef enum BqError
{
    BQ_OK, BQ_BAD_REQUEST, BQ_CONFLICT, BQ_FULL, BQ_BUSY, BQ_IO,
    BQ_CORRUPT, BQ_RECONCILIATION_REQUIRED, BQ_NOT_FOUND,
    BQ_UNSUPPORTED, BQ_INVALID_TRANSITION, BQ_RECIPE_MISMATCH,
    BQ_SOURCE_MISMATCH, BQ_WORKSPACE_MISMATCH, BQ_CLEANUP_FAILED,
    BQ_CONFIGURATION_MISMATCH, BQ_WORKER_MISMATCH, BQ_RESOURCE_MISMATCH,
    BQ_WORKER_FAILED, BQ_WORKER_OOM_FAILURE, BQ_WORKER_TIMEOUT,
    BQ_WORKER_INTERRUPTED, BQ_BOOT_INTERRUPTED, BQ_WORKER_CANCEL_SIGNAL,
    BQ_EXPORT_NOT_FINALIZED, BQ_EXPORT_INVALID, BQ_EXPORT_MISSING, BQ_EXPORT_UNAUTHORIZED,
    BQ_EXPORT_OVERSIZED, BQ_EXPORT_INTERRUPTED, BQ_EXPORT_CORRUPT, BQ_EXPORT_TIMEOUT
} BqError;

typedef enum BqPhase
{
    BQ_QUEUED, BQ_RESERVED, BQ_PREPARING, BQ_SETTLING, BQ_MEASURING,
    BQ_FINALIZING, BQ_CLEANING, BQ_FINISHED
} BqPhase;

typedef enum BqOutcome
{
    BQ_NO_OUTCOME, BQ_SUCCEEDED, BQ_FAILED, BQ_CANCELLED, BQ_INTERRUPTED
} BqOutcome;

typedef enum BqValidity
{
    BQ_NOT_EVALUATED, BQ_VALID, BQ_INVALID
} BqValidity;

typedef enum BqRecordKind
{
    BQ_SUBMIT = 1, BQ_RESERVE, BQ_ADVANCE, BQ_CANCEL, BQ_RECONCILE, BQ_RESULT_BIND
} BqRecordKind;

typedef enum BqRecipe
{
    BQ_RECIPE_UNKNOWN,
    BQ_RECIPE_FAKE_SUCCESS,
    BQ_RECIPE_FAKE_FAILURE,
    BQ_RECIPE_VALIDATE_BUSTER,
    BQ_RECIPE_COMPILER_THROUGHPUT,
    BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED
} BqRecipe;

typedef struct BqRecipeFiles
{
    char name[BQ_RECIPE_NAME_CAP + 1];
    char profile[BQ_RECIPE_FILE_CAP + 1];
    char manifest[BQ_RECIPE_FILE_CAP + 1];
    char bundle[BQ_RECIPE_FILE_CAP + 1];
    char outcome[BQ_RECIPE_FILE_CAP + 1];
    char command[BQ_RECIPE_COMMAND_CAP + 1];
} BqRecipeFiles;

typedef struct BqRequest
{
    u32 size;
    u8 bytes[BQ_REQUEST_CAP];
} BqRequest;

typedef struct BqJob
{
    u64 id;
    /* Reservation sequence is the immutable attempt/ownership token. It is
     * NOT a PID, boot identity, host lease or exactly-once proof. */
    u64 token;
    BqPhase phase;
    BqOutcome outcome;
    BqValidity validity;
    bool cancel_requested;
    BqRequest request;
    char8 digest[SHA256_HEX_CAPACITY];
    bool result_bound;
    char result_root[BQ_PATH_CAP + 1];
    char result_manifest_digest[SHA256_HEX_CAPACITY];
    char result_bundle_digest[SHA256_HEX_CAPACITY];
    char result_full_digest[SHA256_HEX_CAPACITY];
} BqJob;

typedef struct BqEvent
{
    u64 sequence;
    u64 job_id;
    BqRecordKind kind;
    BqPhase phase;
    BqOutcome outcome;
} BqEvent;

typedef struct BqState
{
    u64 sequence;
    u64 active_id;
    u32 journal_schema;
    u32 job_count;
    u32 event_count;
    BqJob jobs[BQ_JOB_CAP];
    BqEvent events[BQ_EVENT_CAP];
} BqState;

/* Data-driven syscall fault points: no callbacks or alternative queue model.
 * CLI callers cannot configure them. A zeroed structure disables injection.
 * fail_write_at is one plus the byte offset at which a write fails. */
typedef struct BqFault
{
    u32 write_chunk;
    u32 fail_write_at;
    bool before_sync;
    bool sync_error;
    bool after_sync;
} BqFault;

typedef struct BqQueue
{
    BqState state;
    int directory_fd;
    int lock_fd;
    int journal_fd;
    char directory_path[BQ_PATH_CAP + 1];
    u64 bytes;
    u64 recovered_tail_bytes;
    bool poisoned;
    bool needs_reconciliation;
    BqFault fault;
} BqQueue;

BUSTER_F_DECL u32 bq_u32(u8 const* bytes);
BUSTER_F_DECL u64 bq_u64(u8 const* bytes);
BUSTER_F_DECL void bq_put32(u8* bytes, u32 value);
BUSTER_F_DECL void bq_put64(u8* bytes, u64 value);
BUSTER_F_DECL String8 bq_field(BqRequest const* request, u32 index);
BUSTER_F_DECL bool bq_request_valid(BqRequest const* request);
BUSTER_F_DECL BqError bq_request_make(String8 const fields[BQ_FIELD_COUNT], BqRequest* request);
BUSTER_F_DECL BqJob* bq_job(BqState* state, u64 id);
BUSTER_F_DECL u32 bq_pending(BqState const* state);
BUSTER_F_DECL BqError bq_open(BqQueue* queue, char const* existing_private_directory);
BUSTER_F_DECL void bq_close(BqQueue* queue);
BUSTER_F_DECL BqError bq_submit(BqQueue* queue, BqRequest const* request, u64* id);
BUSTER_F_DECL BqError bq_reserve(BqQueue* queue, u64* id, u64* token);
BUSTER_F_DECL BqError bq_cancel(BqQueue* queue, u64 id);
BUSTER_F_DECL BqError bq_fake_step(BqQueue* queue, u64 id, u64 token);
BUSTER_F_DECL BqError bq_fake_run(BqQueue* queue, u64* id);
BUSTER_F_DECL BqError bq_fake_reconcile(BqQueue* queue, u64 id, u64 token);
BUSTER_F_DECL BqRecipe bq_recipe_from_name(String8 name);
BUSTER_F_DECL BqRecipe bq_request_recipe(BqRequest const* request);
BUSTER_F_DECL String8 bq_recipe_name(BqRecipe recipe);
BUSTER_F_DECL String8 bq_recipe_profile(BqRecipe recipe);
BUSTER_F_DECL bool bq_recipe_files(BqRecipe recipe, BqRecipeFiles* files);
BUSTER_F_DECL bool bq_recipe_admitted(BqRecipe recipe);
BUSTER_F_DECL bool bq_recipe_service(BqRecipe recipe);
BUSTER_F_DECL bool bq_recipe_blocked(BqRecipe recipe);
BUSTER_F_DECL bool bq_recipe_fake(BqRequest const* request);
BUSTER_F_DECL bool bq_recipe_real(BqRequest const* request);
BUSTER_F_DECL BqError bq_materialize(BqQueue* queue, String8 installed_root, String8 workspace_root, u64* id, u64* token);
BUSTER_F_DECL BqError bq_workspace_reconcile(BqQueue* queue, String8 workspace_root, u64 id, u64 token);
BUSTER_F_DECL bool bq_workspace_name(char result[64], u64 id, u64 token);
BUSTER_F_DECL BqError bq_failure_evidence(BqQueue* queue, BqJob const* job);
BUSTER_F_DECL BqError bq_result_bind(BqQueue* queue, BqJob const* job, String8 result_root,
                                     char const manifest_digest[SHA256_HEX_CAPACITY],
                                     char const bundle_digest[SHA256_HEX_CAPACITY],
                                     char const full_digest[SHA256_HEX_CAPACITY]);
BUSTER_F_DECL char const* bq_error_name(BqError error);
#endif
