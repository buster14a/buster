/* Private #1020 immutable oracle policy and observed-attempt handoff.
 * A reviewed installed template authorizes source/tree, toolchain, build and
 * logical runtime commands. Fresh build bytes and /proc/self/fd commands are
 * observed inside one attempt. The independent builder must issue the opaque
 * VerifiedBuild token; this module does not claim that matching hashes prove
 * who built a program. No production issuer exists while that gate is absent.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_ORACLE_AUTHORITY_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_ORACLE_AUTHORITY_H

#include "retirement_correctness_oracle.h"

typedef struct BqRetirementOracleTemplateRow
{
    uint32_t row, census_row, target;
    char source_sha256[65], configuration_sha256[65];
    char build_command_sha256[65], logical_command_sha256[65];
    char output_name[BQ_RETIREMENT_OUTPUT_NAME_CAP];
} BqRetirementOracleTemplateRow;

typedef struct BqRetirementOracleTemplate
{
    char source_commit[2][41], source_tree[2][41], source_sha256[2][65];
    char support_sha256[65], census_sha256[65], population_sha256[65];
    char toolchain_identity_sha256[65];
    uint32_t population_rows, object_rows, native_target, reference_count;
    BqRetirementOracleTemplateRow const* references;
} BqRetirementOracleTemplate;

/* Issued only by an independently authenticated reference build producer.
 * This child has a test-only miniature issuer, not a production issuer. */
typedef struct BqRetirementOracleVerifiedBuild BqRetirementOracleVerifiedBuild;

typedef struct BqRetirementOracleAuthority
{
    BqRetirementOracleLedger ledger;
    BqRetirementOracleTemplate const* template;
    BqRetirementOracleReference* references;
    uint64_t job_id, attempt_token;
    char template_sha256[65], toolchain_identity_sha256[65], attempt_sha256[65];
    uint32_t finished;
} BqRetirementOracleAuthority;

BUSTER_F_DECL bool bq_retirement_oracle_population_hash(
    BqRetirementTrustedRow const* rows, uint32_t count, char digest[65]);
BUSTER_F_DECL bool bq_retirement_oracle_template_hash(
    BqRetirementOracleTemplate const* source, char digest[65]);
/* Hash exact argv[1..], explicit environment and a typed logical executable
 * and working-directory identity, independent of live descriptor numbers. */
BUSTER_F_DECL bool bq_retirement_oracle_logical_command_hash(
    BqRetirementProcessCommand const* command, char digest[65]);
BUSTER_F_DECL bool bq_retirement_oracle_authority_begin(BqRetirementOracleAuthority* authority,
    BqRetirementOracleTemplate const* template, char const installed_template_sha256[65],
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference* reference_workspace, uint32_t reference_slots,
    uint64_t job_id, uint64_t attempt_token);
BUSTER_F_DECL bool bq_retirement_oracle_authority_next(BqRetirementOracleAuthority* authority,
    BqRetirementOracleVerifiedBuild const* build,
    BqRetirementProcessCommand const* command, BqRetirementArtifactLocation output,
    int cancellation_fd, uint64_t absolute_deadline_ns);
BUSTER_F_DECL bool bq_retirement_oracle_authority_finish(BqRetirementOracleAuthority* authority);
BUSTER_F_DECL bool bq_retirement_oracle_authority_ready(BqRetirementOracleAuthority const* authority);

#endif
