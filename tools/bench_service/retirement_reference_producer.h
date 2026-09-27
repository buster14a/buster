/* #1020 private, profile-pinned independent reference build producer.
 * The installed inventory supplies exact source paths, build arguments and
 * runtime command bytes; A supplies held materialized roots and toolchain.
 * begin authenticates those bytes to separate compiled-profile pins. next
 * observes a real bounded trusted-Clang child, freezes its binary/log/receipt
 * and issues one opaque build token for authority_next. The worker owns the
 * whole-job sandbox, lease, deadline and durable evidence after this call.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_REFERENCE_PRODUCER_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_REFERENCE_PRODUCER_H

#include "retirement_oracle_authority.h"

#define BQ_RETIREMENT_REFERENCE_FLAGS_CAP 16u
#define BQ_RETIREMENT_REFERENCE_ENV_CAP 8u
#define BQ_RETIREMENT_REFERENCE_ARGS_CAP 16u
#define BQ_RETIREMENT_REFERENCE_FIELD_CAP 256u
#define BQ_RETIREMENT_REFERENCE_INVENTORY_CAP (64u * 1024u * 1024u)

typedef struct BqRetirementReferenceSourceIdentity
{
    char commit[41], tree[41], manifest_sha256[65];
} BqRetirementReferenceSourceIdentity;

typedef struct BqRetirementReferencePlanRow
{
    uint32_t row, source_side;
    char source_path[BQ_RETIREMENT_REFERENCE_FIELD_CAP];
    char source_sha256[65];
    uint32_t flag_count, build_environment_count;
    char const* flags[BQ_RETIREMENT_REFERENCE_FLAGS_CAP];
    char const* build_environment[BQ_RETIREMENT_REFERENCE_ENV_CAP];
    uint32_t runtime_argument_count, runtime_environment_count;
    /* argv[0] is the typed logical executable supplied by the producer. */
    char const* runtime_arguments[BQ_RETIREMENT_REFERENCE_ARGS_CAP - 1u];
    char const* runtime_environment[BQ_RETIREMENT_REFERENCE_ENV_CAP];
} BqRetirementReferencePlanRow;

typedef struct BqRetirementReferencePlan
{
    BqRetirementOracleTemplate const* template;
    BqRetirementReferencePlanRow const* rows;
    uint32_t count;
    char clang_sha256[65];
} BqRetirementReferencePlan;

typedef struct BqRetirementReferenceSourceFile
{
    uint64_t device, inode;
} BqRetirementReferenceSourceFile;

typedef struct BqRetirementReferenceProducer BqRetirementReferenceProducer;

/* Defined only in this private issuer header, never in the public B header.
 * A caller cannot issue a token by supplying a digest or a boolean. The
 * authority checks its identity against the live producer before launching. */
struct BqRetirementOracleVerifiedBuild
{
    BqRetirementReferenceProducer* producer;
    uint64_t job_id, attempt_token;
    uint32_t row, census_row, target;
    int source, binary, receipt;
    char preparation_sha256[65], source_sha256[65];
    char configuration_sha256[65];
    char toolchain_identity_sha256[65], build_command_sha256[65];
    char binary_sha256[65], receipt_sha256[65];
};

struct BqRetirementReferenceProducer
{
    BqRetirementReferencePlan const* plan;
    BqRetirementOracleAuthority* authority;
    BqRetirementReferenceSourceIdentity source[2];
    int source_roots[2], clang, inventory, output_directory;
    uint64_t job_id, attempt_token;
    uint32_t next, pending, failed;
    int source_file, binary_file, receipt_file;
    char installed_inventory_sha256[65], installed_template_sha256[65];
    char toolchain_identity_sha256[65];
    BqRetirementReferenceSourceFile* source_files;
    uint32_t source_slots;
    BqRetirementOracleVerifiedBuild token;
};

/* Canonical binary inventory encoder. An installer can write these bytes and
 * pin their SHA-256 in the compiled profile before any request. This helper
 * never installs a pin or accepts a digest computed during a job as authority.
 * descriptor=-1 computes only the digest; a writable descriptor also writes
 * the exact bytes from offset zero, for fixture/installer preparation. */
BUSTER_F_DECL bool bq_retirement_reference_inventory_encode(
    BqRetirementReferencePlan const* plan,
    BqRetirementReferenceSourceIdentity const* source,
    char const toolchain_identity_sha256[65], int descriptor,
    char digest[65]);

BUSTER_F_DECL bool bq_retirement_reference_producer_begin(
    BqRetirementReferenceProducer* producer, BqRetirementReferencePlan const* plan,
    char const installed_inventory_sha256[65],
    char const installed_template_sha256[65], int inventory,
    BqRetirementReferenceSourceIdentity const* verified_source,
    int const* source_roots, char const verified_toolchain_identity_sha256[65],
    int held_clang, int output_directory,
    BqRetirementReferenceSourceFile* source_workspace, uint32_t source_slots,
    BqRetirementOracleAuthority* authority);

/* Source, binary and receipt descriptors remain held until consumed. The
 * caller immediately passes the returned token to authority_next and then
 * invokes release after the complete attempt's durable store has copied it. */
BUSTER_F_DECL bool bq_retirement_reference_producer_next(
    BqRetirementReferenceProducer* producer, int cancellation_fd,
    uint64_t absolute_deadline_ns, BqRetirementOracleVerifiedBuild const** issued);
BUSTER_F_DECL bool bq_retirement_reference_producer_ready(
    BqRetirementReferenceProducer const* producer);
BUSTER_F_DECL bool bq_retirement_reference_producer_release(
    BqRetirementReferenceProducer* producer);

/* Private authority hook: the token is checked while the issuer still holds
 * all three files. Successful authority_next consumes it exactly once. */
BUSTER_F_DECL bool bq_retirement_reference_producer_token_valid(
    BqRetirementOracleVerifiedBuild const* token,
    BqRetirementOracleAuthority const* authority);
BUSTER_F_DECL bool bq_retirement_reference_producer_consume(
    BqRetirementOracleVerifiedBuild const* token);

#endif
