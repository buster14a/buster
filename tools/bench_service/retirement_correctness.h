/* Private pre-timing gate for #1020. The service imports #1018's verified
 * preparation and #508/#509's authenticated census/check/oracle declarations.
 * begin/check/row/batches/finish preserve the complete population; ready
 * verifies structural binding only. (A1) batches freezes the plan-v3 object
 * batch-group contracts of the native-host timed projection, after every row
 * fact and before finish, so lane D can bind timed object rows through them.
 * The service must independently authenticate each producer and gate the
 * actual measurement launch. This is not a published evidence schema.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_H
#include <buster/lib/hash.h>
#include <stdint.h>
#include "../throughput/retirement_metrics.h"

#define BQ_RETIREMENT_CORRECTNESS_ROWS_CAP 100000u
#define BQ_RETIREMENT_CORRECTNESS_CHECKS_CAP 256u
/* The A/A second-command commitment's domain, shared by the row plan that
 * derives BqRetirementPrepared.aa_second_commands_sha256 and the campaign
 * binding that checks it. v3: one entry per timed batch group in dense
 * order, keyed by its smallest member row (v2 had one entry per timed row,
 * each a singleton; v1 covered every compiler-eligible row). */
#define BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN "bq-retirement-aa-second-commands-v3"

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
    /* Predeclared baseline-label-2 compiler and applicable runtime commands
     * in canonical eligible-row order; the service authenticates the plan. */
    char aa_second_commands_sha256[65];
    char source_sha256[2][65], binary_sha256[2][65];
    uint32_t rows, object_rows, native_target;
} BqRetirementPrepared;

/* Imported from the independently replayed #508/#929 projection, including
 * rows with no compiler invocation. A retained control may still be eligible.
 * The importer must authenticate the whole array and the oracle source before
 * calling begin; a candidate result cannot declare its own eligibility.
 * (A1) batch_key_sha256 is the importer's digest of the row's batch-group key
 * (configuration and recipe: allocator, frontend lowering, PIC, fixture
 * recipe, CPU and CPU features); every member and row-naming control of one
 * object batch group shares it, and no two groups share it. batch_control
 * marks a native, compiler-ineligible object row that is a status-checked
 * control of a frozen object batch: it carries that batch's command digests
 * and its row facts record the control's observed status and diagnostic. */
typedef struct BqRetirementTrustedRow
{
    uint32_t row, census_row, target, stage, classification;
    uint32_t compiler_eligible, code_obligation, execution_obligation, batch_control;
    char identity_sha256[65], source_sha256[65], configuration_sha256[65];
    char skip_proof_sha256[65], independent_oracle_sha256[65], batch_key_sha256[65];
    /* Independently derived exact argv/cwd/environment, by trusted binary.
     * Untimed sides and inapplicable native runtime have empty commands; a
     * batch control row carries its batch's command. */
    char compiler_command_sha256[2][65], runtime_command_sha256[2][65];
} BqRetirementTrustedRow;

/* Every required check is an independently authenticated, exact-source and
 * exact-binary command. Multiple semantic/matrix checks cover host targets and
 * configurations; the trusted importer determines their complete set. */
typedef struct BqRetirementRequiredCheck
{
    uint32_t kind, target, rows;
    char command_sha256[65], configuration_sha256[65], receipt_sha256[65];
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
 * checked against an independent oracle, never against a candidate value.
 * diagnostic_sha256 is the compiler's diagnostic digest from its metrics
 * record (never a digest of stderr); a batch control row's facts carry it
 * with the control's exit status (and object when it compiles).
 * semantic_pass is compile acceptance: the compile exited 0, every metrics
 * input compiled (or, linking an executable, was prebuilt) and the artifact
 * passed the per-target reader. It is not a per-row #509 semantic proof;
 * the required checks' receipts carry that. */
typedef struct BqRetirementObservedSide
{
    char compiler_command_sha256[65], artifact_sha256[65], code_sha256[65], diagnostic_sha256[65];
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

/* (A1) One frozen plan-v3 object batch group of the native-host timed
 * projection, as the trusted importer derives it from the frozen rows (never
 * chosen by the producer). contract[side] is the complete batch contract of
 * the A/B baseline (0) and candidate (1) binaries: ordered inputs (timed
 * members in ascending row order, then status-checked controls), statuses,
 * diagnostics, each side's frozen object digests, output leaves, the
 * response-file input list and the reviewed metrics bound. command_sha256 is
 * that side's batch command digest, which every member row's compiler command
 * must equal. The second A/A label's command is bound through the sealed
 * aa_second_commands_sha256 aggregate instead. */
typedef struct BqRetirementBatchGroup
{
    TpRetirementBatchContract contract[2];
    char command_sha256[2][65];
} BqRetirementBatchGroup;

typedef struct BqRetirementCorrectness
{
    BqRetirementPrepared prepared;
    BqRetirementTrustedRow const* trusted_rows;
    BqRetirementRequiredCheck const* required_checks;
    BqRetirementCheckResult* check_facts;
    BqRetirementRowFact* facts;
    BqRetirementBatchGroup const* batch_groups;
    uint32_t check_count, checks_done, rows_done, eligible_rows;
    uint32_t required_kinds, seen_kinds, failed, finished;
    uint32_t batch_group_count, batches_frozen;
    /* (M2) The #509 correctness authority. Only
     * bq_retirement_correctness_authorize sets it, with the installed
     * required-check authority's digest in authority_sha256; the step 9
     * issuer (retirement_unit.c) is its one caller. Without it the campaign
     * binding refuses every object batch group. Both are sealed. */
    uint32_t batch_authority;
    Sha256 checks_hash;
    /* (L4) batches() snapshots every group's per-side contract digest and
     * batch command into batch_groups_sha256; finish seals the snapshot and
     * finish and ready require the live groups to still match it. */
    char checks_sha256[65], sealed_sha256[65], batch_groups_sha256[65];
    char authority_sha256[65];
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
/* Freeze the object batch groups once, after every row and before finish.
 * The groups must partition the timed object rows exactly (each member a
 * compiler-eligible native-target object row, groups ordered by their smallest
 * member), agree across both sides except for object digests, bind each
 * member's compiler command and observed artifact, share one batch key per
 * group (no two groups with the same key), and name as row controls exactly
 * the gate's batch control rows: each native and outside the timed
 * projection, in the group's key, run with the group's batch command, with its
 * status, diagnostic and object joined to the row facts. assigned_workspace
 * holds at least `rows` bytes. The contract digests are snapshotted here; the
 * caller keeps the groups and their contracts immutable. */
BUSTER_F_DECL bool bq_retirement_correctness_batches(BqRetirementCorrectness* gate,
    BqRetirementBatchGroup const* groups, uint32_t count, uint8_t* assigned_workspace, uint32_t workspace_slots);
/* (M2) Grants the #509 correctness authority once, after every required check
 * and row fact joined and the batch groups froze, and before finish; the
 * digest of the installed required-check authority that produced the
 * receipts is sealed with it. A refused call poisons the gate. */
BUSTER_F_DECL bool bq_retirement_correctness_authorize(BqRetirementCorrectness* gate,
    char const authority_sha256[65]);
BUSTER_F_DECL bool bq_retirement_correctness_finish(BqRetirementCorrectness* gate);
BUSTER_F_DECL bool bq_retirement_correctness_ready(BqRetirementCorrectness const* gate);
#endif
