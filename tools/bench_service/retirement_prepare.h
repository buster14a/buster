/* Private #1018 preparation seam. The pinned inventory is installed policy,
 * never a request field. Queue/materializer integration lives in workspace.c.
 */
#ifndef BUSTER_BENCH_RETIREMENT_PREPARE_H
#define BUSTER_BENCH_RETIREMENT_PREPARE_H

typedef struct BqRetirementSource
{
    char commit[65];
    char tree[65];
    char manifest_sha256[SHA256_HEX_CAPACITY];
    /* Same-job inode closure, never a portable or published identity. */
    char installed_identity_sha256[SHA256_HEX_CAPACITY];
    char materialized_identity_sha256[SHA256_HEX_CAPACITY];
    u32 entries;
    u64 bytes;
    u32 directories;
    u32 max_path;
    u32 max_depth;
    u32 manifest_bytes;
} BqRetirementSource;

typedef struct BqRetirementPreparation
{
    BqRetirementSource subjects[2];
    char inventory_sha256[SHA256_HEX_CAPACITY];
    u64 source_reservation_bytes;
} BqRetirementPreparation;

/* Directory holding the immutable preparation-<job> record. The coordinator
 * uses the queue directory; the worker unit, which cannot reach the queue,
 * uses the read-only per-attempt export beneath the job workspace. Named to
 * stay distinct from the lane-F TpRetirementStore. The descriptor is borrowed. */
typedef struct BqRetirementStore
{
    int directory;
} BqRetirementStore;

#define BQ_RETIREMENT_EXPORT_DIRECTORY "retirement"
/* Sealed mode of the export directory once both files are durable. */
#define BQ_RETIREMENT_EXPORT_MODE 0500

BUSTER_F_DECL BqRetirementStore bq_retirement_queue_store(BqQueue const* queue);
BUSTER_F_DECL BqError bq_retirement_preflight(int installed, int workspaces, BqRequest const* request,
                                               BqRetirementPreparation* preparation);
BUSTER_F_DECL bool bq_retirement_verify_subject(int installed, int subject, int source, String8 revision,
                                                 BqRetirementSource* expected);
BUSTER_F_DECL bool bq_retirement_preparation_record(BqRetirementStore store, BqJob const* job,
                                                    BqRetirementPreparation const* preparation,
                                                    BqError outcome, u32 completed_subjects);
BUSTER_F_DECL BqError bq_retirement_preparation_ready(BqRetirementStore store, BqJob const* job,
                                                     int installed, int workspaces,
                                                     char record_sha256[SHA256_HEX_CAPACITY]);
/* The service-side correctness/build importer supplies the digest received by
 * the authenticated worker handoff. Facts are returned only after rereading
 * the durable record, the installed inventory and both materialized trees. */
BUSTER_F_DECL BqError bq_retirement_preparation_import(BqRetirementStore store, BqJob const* job,
                                                      int installed, int workspaces,
                                                      char const record_sha256[SHA256_HEX_CAPACITY],
                                                      BqRetirementPreparation* verified);

/* Coordinator-only: after bq_retirement_preparation_ready returned
 * preparation_sha256, copy that exact record and the canonical request bytes
 * into job-<id>-attempt-<token>/retirement/ for the worker unit. The directory
 * and both files are created without replacement, then sealed read-only. */
BUSTER_F_DECL BqError bq_retirement_preparation_export(BqRetirementStore queue_store, BqJob const* job,
    int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY]);

#endif
