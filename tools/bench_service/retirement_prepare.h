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

BUSTER_F_DECL BqError bq_retirement_preflight(int installed, int workspaces, BqRequest const* request,
                                               BqRetirementPreparation* preparation);
BUSTER_F_DECL bool bq_retirement_verify_subject(int installed, int subject, int source, String8 revision,
                                                 BqRetirementSource* expected);
BUSTER_F_DECL bool bq_retirement_preparation_record(BqQueue* queue, BqJob const* job,
                                                    BqRetirementPreparation const* preparation,
                                                    BqError outcome, u32 completed_subjects);
BUSTER_F_DECL BqError bq_retirement_preparation_ready(BqQueue* queue, BqJob const* job,
                                                     int installed, int workspaces,
                                                     char record_sha256[SHA256_HEX_CAPACITY]);

#endif
