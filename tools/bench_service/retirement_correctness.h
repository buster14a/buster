/* Private pre-timing gate for #1020. The service imports #1018's verified
 * preparation and #508/#509's authenticated census/check/oracle declarations.
 * begin/check/row/finish preserve the complete population; ready is the only
 * handoff to a measurement plan. This is not a published evidence schema.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_H
#include <buster/lib/hash.h>
#include <stdint.h>

#define BQ_RETIREMENT_CORRECTNESS_ROWS_CAP 100000u
#define BQ_RETIREMENT_CORRECTNESS_CHECKS_CAP 256u

typedef enum BqRetirementCheckKind
{
    BQ_RETIREMENT_CHECK_CENSUS = 1,
    BQ_RETIREMENT_CHECK_SEMANTIC,
    BQ_RETIREMENT_CHECK_MATRIX,
    BQ_RETIREMENT_CHECK_NO_FALLBACK,
    BQ_RETIREMENT_CHECK_SELF_HOST,
    BQ_RETIREMENT_CHECK_FIXED_POINT,
    BQ_RETIREMENT_CHECK_COUNT
} BqRetirementCheckKind;

typedef enum BqRetirementStage
{
    BQ_RETIREMENT_STAGE_OBJECT = 1,
    BQ_RETIREMENT_STAGE_LINK,
    BQ_RETIREMENT_STAGE_SELF_HOST
} BqRetirementStage;

typedef struct BqRetirementPrepared
{
    char preparation_sha256[65], support_sha256[65], census_sha256[65];
    char source_sha256[2][65], binary_sha256[2][65];
    uint32_t rows, object_rows, native_target;
} BqRetirementPrepared;

/* Imported from the independently replayed #508/#929 projection, including
 * rows with no compiler invocation. A retained control may still be eligible.
 * The importer must authenticate the whole array and the oracle source before
 * calling begin; a candidate result cannot declare its own eligibility. */
typedef struct BqRetirementTrustedRow
{
    uint32_t row, census_row, target, stage, classification;
    uint32_t compiler_eligible, code_obligation, execution_obligation;
    char identity_sha256[65], source_sha256[65], configuration_sha256[65];
    char skip_proof_sha256[65], independent_oracle_sha256[65];
    /* Independently derived exact argv/cwd/environment, by trusted binary.
     * Untimed sides and inapplicable native runtime have empty commands. */
    char compiler_command_sha256[2][65], runtime_command_sha256[2][65];
} BqRetirementTrustedRow;

/* Every required check is an independently authenticated, exact-source and
 * exact-binary command. Multiple semantic/matrix checks cover host targets and
 * configurations; the trusted importer determines their complete set. */
typedef struct BqRetirementRequiredCheck
{
    uint32_t kind, target, rows;
    char command_sha256[65], configuration_sha256[65];
} BqRetirementRequiredCheck;

typedef struct BqRetirementCheckResult
{
    uint32_t kind, target, rows, failures, timed_out, out_of_memory;
    int exit_code;
    char command_sha256[65], configuration_sha256[65], receipt_sha256[65];
    char preparation_sha256[65], source_sha256[2][65], binary_sha256[2][65];
} BqRetirementCheckResult;

/* Observations are from the service-owned launcher and artifact reader.
 * Command digests include exact argv, cwd and environment; runtime output is
 * checked against an independent oracle, never against a candidate value. */
typedef struct BqRetirementObservedSide
{
    char compiler_command_sha256[65], artifact_sha256[65], code_sha256[65];
    char runtime_command_sha256[65], runtime_output_sha256[65];
    uint64_t code_bytes;
    uint32_t semantic_pass, fallback_count, timed_out, out_of_memory;
    int compiler_exit, runtime_exit;
} BqRetirementObservedSide;

typedef struct BqRetirementRowFact
{
    uint32_t row, census_row, compiler_eligible, runtime_eligible, code_eligible;
    BqRetirementObservedSide side[2];
} BqRetirementRowFact;

typedef struct BqRetirementCorrectness
{
    BqRetirementPrepared prepared;
    BqRetirementTrustedRow const* trusted_rows;
    BqRetirementRequiredCheck const* required_checks;
    BqRetirementCheckResult* check_facts;
    BqRetirementRowFact* facts;
    uint32_t check_count, checks_done, rows_done, eligible_rows;
    uint32_t required_kinds, seen_kinds, failed, finished;
    Sha256 checks_hash;
    char checks_sha256[65], sealed_sha256[65];
} BqRetirementCorrectness;

BUSTER_F_DECL bool bq_retirement_correctness_begin(BqRetirementCorrectness* gate,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow const* rows,
    BqRetirementRequiredCheck const* checks, uint32_t check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    uint32_t* identity_workspace, uint32_t identity_slots,
    uint8_t* census_workspace, uint32_t census_slots);
BUSTER_F_DECL bool bq_retirement_correctness_check(BqRetirementCorrectness* gate,
    BqRetirementCheckResult const* observed);
BUSTER_F_DECL bool bq_retirement_correctness_row(BqRetirementCorrectness* gate,
    BqRetirementRowFact const* observed);
BUSTER_F_DECL bool bq_retirement_correctness_finish(BqRetirementCorrectness* gate);
BUSTER_F_DECL bool bq_retirement_correctness_ready(BqRetirementCorrectness const* gate);
#endif
