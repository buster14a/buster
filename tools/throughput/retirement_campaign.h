/* Private #881-D collection coordinator (A1). The service owns the
 * authenticated preparation/oracle/host inputs, exclusive streams and the
 * #426 A/A verdict. freeze copies the fixed per-group batch contracts and
 * per-runtime-row command/output identities before any child starts; run only
 * accepts those identities in the existing #619 cursor order. The state
 * machine retains an interrupted attempt as invalid. capacity and
 * store_preflight size both stages (G batch groups, U runtime rows, both
 * result populations, and the per-batch metrics artifacts packed into metrics
 * shards) plus the untimed code-artifact batches against the #1023 store
 * entry and byte ceilings before any timing. freeze also requires the reviewed
 * campaign budget (retirement_budget.h) to match the recipe pin its caller
 * passes and to hold the derived counts, each timed group costed by its kind
 * and stage. It does not confer receipt authority, evaluate #426, or admit a
 * service recipe.
 *
 * Map: TpRetirementCampaignShape, tp_retirement_campaign_metrics_shards,
 * tp_retirement_campaign_capacity, tp_retirement_campaign_store_preflight,
 * TpRetirementCampaignReview, tp_retirement_campaign_freeze,
 * tp_retirement_campaign_run, tp_retirement_campaign_rotate,
 * tp_retirement_campaign_finish_stage, tp_retirement_campaign_outcome.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_CAMPAIGN_H
#define BUSTER_THROUGHPUT_RETIREMENT_CAMPAIGN_H
#include "retirement_untimed.h"

/* The installed service never compiles the A/A admission stand-in. */
#if defined(BQ_SERVICE_INSTALLED) && defined(TP_RETIREMENT_CAMPAIGN_FIXTURE_AA)
#error "the installed service must not define TP_RETIREMENT_CAMPAIGN_FIXTURE_AA"
#endif

#ifdef __linux__
#include "retirement_store.h"
#define TP_RETIREMENT_CAMPAIGN_STAGES 2u
/* Per stage: one command per (group, variant) and per (runtime row, variant). */
#define TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT 2u
/* tp_retirement_store_plan currently requires at least three external entries. */
#define TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES 3u
/* The store's receipt validator admits exactly the producer's shard size. */
BUSTER_CT_CHECK(TP_RETIREMENT_STORE_RECEIPT_SHARD_RECORDS == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
/* A metrics shard is one store file. */
BUSTER_CT_CHECK(TP_RETIREMENT_METRICS_SHARD_BYTES <= TP_RETIREMENT_STORE_FILE_BYTES);
BUSTER_CT_CHECK(TP_RETIREMENT_BUDGET_STAGES == TP_RETIREMENT_CAMPAIGN_STAGES);

typedef enum TpRetirementCampaignPhase
{
    TP_RETIREMENT_CAMPAIGN_UNAVAILABLE,
    TP_RETIREMENT_CAMPAIGN_AA,
    TP_RETIREMENT_CAMPAIGN_AWAIT_AA,
    TP_RETIREMENT_CAMPAIGN_AB,
    TP_RETIREMENT_CAMPAIGN_COLLECTED,
    TP_RETIREMENT_CAMPAIGN_INVALID
} TpRetirementCampaignPhase;

typedef enum TpRetirementCampaignState
{
    TP_RETIREMENT_CAMPAIGN_STATE_UNAVAILABLE,
    TP_RETIREMENT_CAMPAIGN_STATE_RUNNING,
    TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE,
    TP_RETIREMENT_CAMPAIGN_STATE_FAILED
} TpRetirementCampaignState;

typedef struct TpRetirementCampaignOutcome
{
    TpRetirementCampaignState execution, validity, aa_qualification, statistical_decision;
} TpRetirementCampaignOutcome;

/* The frozen campaign dimensions: G batch groups of which object_groups are
 * multi-input object batches, `rows` timed rows, U runtime-eligible rows, the
 * pair count, and the untimed code-artifact groups (untimed_object_groups of
 * them object batches). metrics_bytes is the sum of the object groups'
 * reviewed per-artifact metrics bounds (each group writes one artifact per
 * batch), untimed_metrics_bytes the same sum over untimed object groups, and
 * metrics_artifact_max the largest single bound (at most the 64 MiB cap). All
 * three are zero without object groups of their kind. */
typedef struct TpRetirementCampaignShape
{
    unsigned groups, object_groups, rows, runtime_rows, pairs;
    unsigned untimed_groups, untimed_object_groups;
    uint64_t metrics_bytes, untimed_metrics_bytes, metrics_artifact_max;
} TpRetirementCampaignShape;

/* Byte bounds use the source-proven maximal line widths
 * (TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX, TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX,
 * TP_RETIREMENT_BATCH_RECORD_BYTES_MAX and TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX),
 * which the writers enforce, and the reviewed metrics bounds, which the
 * measurement enforces per artifact. Metrics artifacts are not store entries:
 * they are byte ranges of metrics shards, whose count is bounded by the greedy
 * packing rule (tp_retirement_campaign_metrics_shards). Payload files are the
 * transcript, numeric and metrics shards of both stages plus the untimed
 * metrics shards and the untimed batch record file. */
typedef struct TpRetirementCampaignCapacity
{
    uint64_t compiler_invocations_per_stage, runtime_invocations_per_stage;
    uint64_t invocations_per_stage, samples_per_stage, row_samples_per_stage, batch_samples_per_stage;
    uint64_t spool_bytes_per_stage, metrics_artifacts_per_stage, metrics_bytes_per_stage_upper_bound;
    uint64_t metrics_shards_per_stage_upper_bound;
    uint64_t transcript_bytes_per_stage_upper_bound, sample_bytes_per_stage_upper_bound;
    uint64_t transcript_shards_per_stage, sample_shards_per_stage, sample_partitions_per_stage;
    uint64_t untimed_batches_upper_bound, untimed_record_bytes_upper_bound, untimed_record_files;
    uint64_t untimed_metrics_artifacts_upper_bound, untimed_metrics_bytes_upper_bound;
    uint64_t untimed_metrics_shards_upper_bound;
    uint64_t total_compiler_invocations, total_runtime_invocations;
    uint64_t total_invocations, total_samples, total_spool_bytes;
    uint64_t total_transcript_bytes_upper_bound, total_sample_bytes_upper_bound;
    uint64_t total_metrics_artifacts, total_metrics_bytes_upper_bound, total_metrics_shards_upper_bound;
    uint64_t total_transcript_shards, total_sample_shards, total_sample_partitions;
    uint64_t total_shard_files, total_payload_files, total_payload_bytes_upper_bound;
} TpRetirementCampaignCapacity;

/* Exact result-store reservation for one campaign: owned payload files and
 * worst-case payload bytes plus the caller's external entries and bytes. */
typedef struct TpRetirementCampaignStorePlan
{
    uint64_t owned_files, owned_bytes, external_entries, external_bytes;
    uint64_t entries, bytes, remaining_entries, remaining_bytes;
} TpRetirementCampaignStorePlan;

/* The command hash covers argv/cwd/environment; all other oracle fields are
 * copied separately. contract_sha256 identifies an object group's complete
 * frozen batch contract (inputs, statuses, diagnostics, objects, leaves, the
 * response-file digest and the reviewed metrics bound). */
typedef struct TpRetirementCampaignCommand
{
    char command_sha256[65], output_sha256[65], contract_sha256[65], artifact[128];
    unsigned timeout_seconds;
    int exit_status;
} TpRetirementCampaignCommand;

/* The reviewed-budget inputs freeze needs besides the frozen commands: the
 * budget record, each timed group's budget stage (TP_RETIREMENT_BUDGET_STAGE_*;
 * object for an object group, the row's link or self-host stage for a
 * singleton), and the untimed code-artifact groups (input count, group kind
 * and budget stage for each; a singleton has one input). The recipe pin is a
 * separate freeze argument taken from the admitted profile, never from here. */
typedef struct TpRetirementCampaignReview
{
    TpRetirementCampaignBudget const* budget;
    unsigned const* group_stages;
    unsigned const* untimed_inputs;
    unsigned const* untimed_kinds;
    unsigned const* untimed_stages;
    unsigned group_count, untimed_groups;
} TpRetirementCampaignReview;

/* Shared with lane E's composer (retirement_compose.h), which can adopt these
 * in place of its own copies. The statistical family's two #619 per-scope
 * counts, derived from the frozen layout before any timing
 * (TpRetirementComposeBounds carries the same two numbers). */
typedef struct TpRetirementFamilyCounts
{
    unsigned bootstrap_members, cell_members;
} TpRetirementFamilyCounts;

/* One code-observed row and its two variants' facts (the composer's
 * TpRetirementComposeCode layout). */
typedef struct TpRetirementCodeRow
{
    unsigned row;
    TpRetirementCodeSide sides[2];
} TpRetirementCodeRow;

/* The six #619 slice dimensions in the validator's STATISTICAL_DIMENSIONS
 * order (target, cpu, allocator, frontend_lowering, PIC, artifact_stage);
 * each value is a printable string of at most 64 bytes. */
#define TP_RETIREMENT_TIMED_DIMENSIONS 6u
#define TP_RETIREMENT_TIMED_DIMENSION_BYTES 64u

/* One native-host timed row for the composer's layout
 * (TpRetirementComposeRow): its population row id, its campaign batch-group
 * ordinal (groups in ascending smallest-member order), whether it is
 * runtime-eligible, and its identity's frozen dimension values. */
typedef struct TpRetirementTimedRow
{
    unsigned id, group, runtime;
    char dimensions[TP_RETIREMENT_TIMED_DIMENSIONS][TP_RETIREMENT_TIMED_DIMENSION_BYTES + 1];
} TpRetirementTimedRow;

typedef struct TpRetirementCampaign
{
    TpRetirementPlan plan;
    TpRetirementSamples* samples[TP_RETIREMENT_CAMPAIGN_STAGES];
    TpRetirementExecutable const* binaries[TP_RETIREMENT_CAMPAIGN_STAGES][2];
    TpRetirementCampaignCommand* commands;
    unsigned* group_shapes;
    unsigned* runtime_rows;
    unsigned groups, runtime_count, population_rows;
    TpRetirementCampaignCapacity capacity;
    TpRetirementBudgetPreflight budget;
    TpRetirementCampaignPhase phase;
    char plan_sha256[65], context_sha256[65], budget_sha256[65];
    char binary_sha256[TP_RETIREMENT_CAMPAIGN_STAGES][2][65];
    char job[129], boot[129];
    uint64_t attempt, bound_at_ns;
    int cpu;
} TpRetirementCampaign;

static int tp_retirement_campaign_u64_mul(uint64_t left, uint64_t right, uint64_t* product)
{
    int ok = product && (!left || right <= UINT64_MAX / left);
    if (product) *product = ok ? left * right : 0;
    return ok;
}

static int tp_retirement_campaign_u64_add(uint64_t left, uint64_t right, uint64_t* sum)
{
    int ok = sum && right <= UINT64_MAX - left;
    if (sum) *sum = ok ? left + right : 0;
    return ok;
}

static uint64_t tp_retirement_campaign_ceil_div(uint64_t value, uint64_t divisor)
{
    uint64_t quotient = divisor ? value / divisor : 0;
    uint64_t remainder = divisor ? value % divisor : 0;
    return quotient + !!remainder;
}

/* Upper bound on the shards one metrics writer produces for `artifacts`
 * artifacts of at most `bytes` bytes in total. Greedy rotation makes any two
 * consecutive shards together exceed one shard's capacity, so n shards hold
 * more than floor(n / 2) * cap bytes: n <= 2 * ceil(bytes / cap). A shard also
 * holds at least one artifact. */
static uint64_t tp_retirement_campaign_metrics_shards(uint64_t artifacts, uint64_t bytes)
{
    uint64_t greedy = 2 * tp_retirement_campaign_ceil_div(bytes, TP_RETIREMENT_METRICS_SHARD_BYTES);
    return artifacts < greedy ? artifacts : greedy;
}

/* Checked entry and byte fit against the #1023 store ceilings. */
static int tp_retirement_campaign_store_fits(uint64_t owned_files, uint64_t owned_bytes,
    uint64_t external_entries, uint64_t external_bytes, uint64_t* entries, uint64_t* bytes)
{
    int ok = entries && bytes && owned_files && owned_bytes &&
        external_entries >= TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES &&
        tp_retirement_campaign_u64_add(owned_files, external_entries, entries) &&
        *entries <= TP_RETIREMENT_STORE_FILES &&
        tp_retirement_campaign_u64_add(owned_bytes, external_bytes, bytes) &&
        *bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES;
    return ok;
}

/* The shape must come from the authenticated correctness/oracle gate and the
 * pinned budget. The report includes both fixed stages, worst-case
 * transcript, numeric-shard and metrics payload, and the untimed batches. It
 * rejects a campaign whose payload files leave fewer than the minimum external
 * entries or whose worst-case payload exceeds the store's byte ceiling. It
 * does not predict host speed; tp_retirement_budget_preflight does that from
 * the reviewed bounds, and tp_retirement_campaign_store_preflight adds the
 * caller's exact external reservation before timing. */
static int tp_retirement_campaign_capacity(TpRetirementCampaignShape const* shape,
    TpRetirementCampaignCapacity* capacity)
{
    uint64_t per_variant = 0, per_unit = 0, row_samples = 0, batch_samples = 0, samples = 0;
    uint64_t compiler = 0, runtime = 0, invocations = 0, spool = 0, transcript = 0, sample_bytes = 0;
    uint64_t row_bytes = 0, batch_bytes = 0, metrics = 0, metrics_bytes = 0, metrics_shards = 0;
    uint64_t untimed_batches = 0, untimed_records = 0, untimed_artifacts = 0, untimed_bytes = 0;
    uint64_t untimed_shards = 0, untimed_files = 0;
    uint64_t transcript_shards = 0, sample_shards = 0, partitions = 0;
    TpRetirementCampaignCapacity result = {0};
    unsigned singletons = shape && shape->groups >= shape->object_groups ? shape->groups - shape->object_groups : 0;
    int ok = capacity && shape && shape->groups && shape->groups <= TP_RETIREMENT_MAX_CELLS &&
        shape->object_groups <= shape->groups && shape->rows >= shape->groups &&
        shape->rows <= TP_RETIREMENT_MAX_CELLS && shape->runtime_rows <= singletons &&
        shape->untimed_groups <= TP_RETIREMENT_MAX_CELLS && shape->untimed_object_groups <= shape->untimed_groups &&
        (shape->object_groups || shape->untimed_object_groups ?
            shape->metrics_artifact_max && shape->metrics_artifact_max <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES :
            !shape->metrics_artifact_max) &&
        (shape->object_groups ? shape->metrics_bytes >= shape->object_groups &&
            shape->metrics_bytes / shape->object_groups <= shape->metrics_artifact_max : !shape->metrics_bytes) &&
        (shape->untimed_object_groups ? shape->untimed_metrics_bytes >= shape->untimed_object_groups &&
            shape->untimed_metrics_bytes / shape->untimed_object_groups <= shape->metrics_artifact_max :
            !shape->untimed_metrics_bytes);
    if (ok)
    {
        samples = tp_retirement_samples_union(shape->rows, shape->object_groups, shape->pairs, &row_samples);
        batch_samples = samples - row_samples;
        ok = samples &&
            tp_retirement_campaign_u64_mul(TP_RETIREMENT_ROUNDS, shape->pairs, &per_variant) &&
            tp_retirement_campaign_u64_add(per_variant, TP_RETIREMENT_WARMUPS, &per_variant) &&
            tp_retirement_campaign_u64_mul(per_variant, 2, &per_unit) &&
            tp_retirement_campaign_u64_mul(shape->groups, per_unit, &compiler) &&
            tp_retirement_campaign_u64_mul(shape->runtime_rows, per_unit, &runtime) &&
            tp_retirement_campaign_u64_add(compiler, runtime, &invocations) &&
            tp_retirement_campaign_u64_mul(samples, TP_RETIREMENT_SAMPLE_RECORD_BYTES, &spool) &&
            tp_retirement_campaign_u64_mul(invocations, TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX, &transcript) &&
            tp_retirement_campaign_u64_mul(row_samples, TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX, &row_bytes) &&
            tp_retirement_campaign_u64_mul(batch_samples, TP_RETIREMENT_BATCH_RECORD_BYTES_MAX, &batch_bytes) &&
            tp_retirement_campaign_u64_add(row_bytes, batch_bytes, &sample_bytes) &&
            tp_retirement_campaign_u64_mul(shape->object_groups, per_unit, &metrics) &&
            tp_retirement_campaign_u64_mul(shape->metrics_bytes, per_unit, &metrics_bytes) &&
            tp_retirement_campaign_u64_mul(shape->untimed_groups, TP_RETIREMENT_BUDGET_UNTIMED_BATCHES,
                &untimed_batches) &&
            tp_retirement_campaign_u64_mul(untimed_batches, TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX, &untimed_records) &&
            tp_retirement_campaign_u64_mul(shape->untimed_object_groups, TP_RETIREMENT_BUDGET_UNTIMED_BATCHES,
                &untimed_artifacts) &&
            tp_retirement_campaign_u64_mul(shape->untimed_metrics_bytes, TP_RETIREMENT_BUDGET_UNTIMED_BATCHES,
                &untimed_bytes);
    }
    if (ok)
    {
        transcript_shards = tp_retirement_campaign_ceil_div(invocations, TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
        sample_shards = tp_retirement_campaign_ceil_div(row_samples, TP_RETIREMENT_SAMPLE_SHARD_RECORDS) +
            tp_retirement_campaign_ceil_div(batch_samples, TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
        partitions = tp_retirement_samples_partitions(row_samples) + tp_retirement_samples_partitions(batch_samples);
        metrics_shards = tp_retirement_campaign_metrics_shards(metrics, metrics_bytes);
        untimed_shards = tp_retirement_campaign_metrics_shards(untimed_artifacts, untimed_bytes);
        untimed_files = shape->untimed_groups ? 1 : 0;
        result = (TpRetirementCampaignCapacity){
            .compiler_invocations_per_stage = compiler, .runtime_invocations_per_stage = runtime,
            .invocations_per_stage = invocations, .samples_per_stage = samples,
            .row_samples_per_stage = row_samples, .batch_samples_per_stage = batch_samples,
            .spool_bytes_per_stage = spool, .metrics_artifacts_per_stage = metrics,
            .metrics_bytes_per_stage_upper_bound = metrics_bytes,
            .metrics_shards_per_stage_upper_bound = metrics_shards,
            .transcript_bytes_per_stage_upper_bound = transcript,
            .sample_bytes_per_stage_upper_bound = sample_bytes,
            .transcript_shards_per_stage = transcript_shards, .sample_shards_per_stage = sample_shards,
            .sample_partitions_per_stage = partitions,
            .untimed_batches_upper_bound = untimed_batches, .untimed_record_bytes_upper_bound = untimed_records,
            .untimed_record_files = untimed_files, .untimed_metrics_artifacts_upper_bound = untimed_artifacts,
            .untimed_metrics_bytes_upper_bound = untimed_bytes,
            .untimed_metrics_shards_upper_bound = untimed_shards};
        ok = invocations <= (uint64_t)TP_RETIREMENT_TRANSCRIPT_SHARDS * TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
            transcript_shards <= TP_RETIREMENT_TRANSCRIPT_SHARDS && sample_shards <= TP_RETIREMENT_TRANSCRIPT_SHARDS &&
            metrics_shards <= TP_RETIREMENT_METRICS_SHARDS && untimed_shards <= TP_RETIREMENT_METRICS_SHARDS &&
            tp_retirement_campaign_u64_mul(compiler, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_compiler_invocations) &&
            tp_retirement_campaign_u64_mul(runtime, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_runtime_invocations) &&
            tp_retirement_campaign_u64_mul(invocations, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_invocations) &&
            tp_retirement_campaign_u64_mul(samples, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_samples) &&
            tp_retirement_campaign_u64_mul(spool, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_spool_bytes) &&
            tp_retirement_campaign_u64_mul(transcript, TP_RETIREMENT_CAMPAIGN_STAGES,
                &result.total_transcript_bytes_upper_bound) &&
            tp_retirement_campaign_u64_mul(sample_bytes, TP_RETIREMENT_CAMPAIGN_STAGES,
                &result.total_sample_bytes_upper_bound) &&
            tp_retirement_campaign_u64_mul(metrics, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_metrics_artifacts) &&
            tp_retirement_campaign_u64_add(result.total_metrics_artifacts, untimed_artifacts,
                &result.total_metrics_artifacts) &&
            tp_retirement_campaign_u64_mul(metrics_bytes, TP_RETIREMENT_CAMPAIGN_STAGES,
                &result.total_metrics_bytes_upper_bound) &&
            tp_retirement_campaign_u64_add(result.total_metrics_bytes_upper_bound, untimed_bytes,
                &result.total_metrics_bytes_upper_bound) &&
            tp_retirement_campaign_u64_mul(metrics_shards, TP_RETIREMENT_CAMPAIGN_STAGES,
                &result.total_metrics_shards_upper_bound) &&
            tp_retirement_campaign_u64_add(result.total_metrics_shards_upper_bound, untimed_shards,
                &result.total_metrics_shards_upper_bound) &&
            tp_retirement_campaign_u64_mul(transcript_shards, TP_RETIREMENT_CAMPAIGN_STAGES,
                &result.total_transcript_shards) &&
            tp_retirement_campaign_u64_mul(sample_shards, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_sample_shards) &&
            tp_retirement_campaign_u64_mul(partitions, TP_RETIREMENT_CAMPAIGN_STAGES, &result.total_sample_partitions) &&
            tp_retirement_campaign_u64_add(result.total_transcript_shards, result.total_sample_shards,
                &result.total_shard_files) &&
            tp_retirement_campaign_u64_add(result.total_shard_files, result.total_metrics_shards_upper_bound,
                &result.total_shard_files) &&
            tp_retirement_campaign_u64_add(result.total_shard_files, untimed_files, &result.total_payload_files) &&
            tp_retirement_campaign_u64_add(result.total_transcript_bytes_upper_bound,
                result.total_sample_bytes_upper_bound, &result.total_payload_bytes_upper_bound) &&
            tp_retirement_campaign_u64_add(result.total_payload_bytes_upper_bound,
                result.total_metrics_bytes_upper_bound, &result.total_payload_bytes_upper_bound) &&
            tp_retirement_campaign_u64_add(result.total_payload_bytes_upper_bound, untimed_records,
                &result.total_payload_bytes_upper_bound);
        uint64_t entries = 0, bytes = 0;
        ok = ok && tp_retirement_campaign_store_fits(result.total_payload_files, result.total_payload_bytes_upper_bound,
            TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES, 0, &entries, &bytes);
    }
    if (capacity) *capacity = ok ? result : (TpRetirementCampaignCapacity){0};
    return ok;
}

/* Exact store preflight, called before any timing. The store itself must
 * own the execution receipt and any manifest it publishes (receipt_authority
 * looks the receipt up among the store's files), so owned_control_files and
 * owned_control_bytes reserve those non-payload store files; they must cover at
 * least one receipt at its TP_RETIREMENT_RECEIPT_BYTES bound. external_entries
 * must cover every other inventoried entry (controls, binaries, logs and
 * directories), and external_bytes is the caller's conservative byte
 * reservation for them. An inconsistent capacity record, overflow or either
 * store ceiling rejects with a zeroed plan. owned_files/owned_bytes (payload
 * plus owned controls) are the reservation for tp_retirement_store_plan. */
static inline int tp_retirement_campaign_store_preflight(TpRetirementCampaignCapacity const* capacity,
    uint64_t owned_control_files, uint64_t owned_control_bytes,
    uint64_t external_entries, uint64_t external_bytes, TpRetirementCampaignStorePlan* plan)
{
    uint64_t shards = 0, payload_files = 0, payload = 0, files = 0, owned = 0, entries = 0, bytes = 0;
    int ok = capacity && plan && owned_control_files &&
        owned_control_bytes >= TP_RETIREMENT_RECEIPT_BYTES &&
        tp_retirement_campaign_u64_add(capacity->total_transcript_shards,
            capacity->total_sample_shards, &shards) &&
        tp_retirement_campaign_u64_add(shards, capacity->total_metrics_shards_upper_bound, &shards) &&
        shards && shards == capacity->total_shard_files &&
        capacity->untimed_record_files <= 1 &&
        tp_retirement_campaign_u64_add(shards, capacity->untimed_record_files, &payload_files) &&
        payload_files == capacity->total_payload_files &&
        tp_retirement_campaign_u64_add(capacity->total_transcript_bytes_upper_bound,
            capacity->total_sample_bytes_upper_bound, &payload) &&
        tp_retirement_campaign_u64_add(payload, capacity->total_metrics_bytes_upper_bound, &payload) &&
        tp_retirement_campaign_u64_add(payload, capacity->untimed_record_bytes_upper_bound, &payload) &&
        payload && payload == capacity->total_payload_bytes_upper_bound &&
        tp_retirement_campaign_u64_add(payload_files, owned_control_files, &files) &&
        tp_retirement_campaign_u64_add(payload, owned_control_bytes, &owned) &&
        tp_retirement_campaign_store_fits(files, owned, external_entries, external_bytes,
            &entries, &bytes);
    if (plan)
    {
        *plan = (TpRetirementCampaignStorePlan){0};
        if (ok)
        {
            plan->owned_files = files;
            plan->owned_bytes = owned;
            plan->external_entries = external_entries;
            plan->external_bytes = external_bytes;
            plan->entries = entries;
            plan->bytes = bytes;
            plan->remaining_entries = TP_RETIREMENT_STORE_FILES - entries;
            plan->remaining_bytes = TP_RETIREMENT_STORE_TOTAL_BYTES - bytes;
        }
    }
    return ok;
}

/* group_shape is (member count << 1) | kind for a compiler command's group. */
static int tp_retirement_campaign_command_copy(TpRetirementCampaignCommand* target,
    TpRetirementMeasuredCommand const* command, unsigned unit, unsigned kind, unsigned variant,
    unsigned group_shape)
{
    char digest[65], output[65];
    size_t artifact = 0;
    if (command && command->artifact)
        while (artifact < sizeof(target->artifact) && command->artifact[artifact]) ++artifact;
    TpRetirementBatchContract const* batch = command ? command->batch : NULL;
    unsigned members = 0;
    for (unsigned i = 0; batch && batch->inputs && i < batch->input_count && i < TP_RETIREMENT_BATCH_INPUTS; ++i)
        members += batch->inputs[i].member;
    int object = !kind && (group_shape & 1) == TP_RETIREMENT_GROUP_OBJECT;
    int ok = target && command && command->unit == unit && command->kind == kind &&
        command->variant == variant && command->timeout_seconds && command->timeout_seconds <= 86400 &&
        tp_retirement_digest(command->command_sha256) && tp_retirement_digest(command->output_sha256) &&
        tp_retirement_command_hash(command, digest) && !strcmp(digest, command->command_sha256) &&
        (kind ? !artifact && !batch && !command->exit_status :
         object ? !artifact && tp_retirement_batch_contract_valid(batch) && members == group_shape >> 1 &&
             batch->exit_status == (unsigned)command->exit_status &&
             tp_retirement_batch_contract_output(batch, output) && !strcmp(output, command->output_sha256) :
         group_shape >> 1 == 1 && artifact && artifact < sizeof(target->artifact) &&
             tp_retirement_artifact_leaf(command->artifact) && !batch && !command->exit_status);
    if (target)
    {
        *target = (TpRetirementCampaignCommand){0};
        if (ok && object) ok = tp_retirement_batch_contract_digest(batch, target->contract_sha256);
        if (ok)
        {
            memcpy(target->command_sha256, command->command_sha256, 65);
            memcpy(target->output_sha256, command->output_sha256, 65);
            if (artifact) memcpy(target->artifact, command->artifact, artifact + 1);
            target->timeout_seconds = command->timeout_seconds;
            target->exit_status = command->exit_status;
        }
        else *target = (TpRetirementCampaignCommand){0};
    }
    return ok;
}

/* Slot of one (unit, variant) command: groups first, then runtime rows. */
static size_t tp_retirement_campaign_index(unsigned groups, unsigned runtime, unsigned stage,
    unsigned slot, unsigned variant)
{
    size_t index = ((size_t)stage * (groups + runtime) + slot) * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + variant;
    return index;
}

static unsigned tp_retirement_campaign_group_shape(TpRetirementSampleGroup const* group)
{
    return (group->count << 1) | group->kind;
}

/* Derive the store shape and the budget counts from the frozen commands and
 * the reviewed record: each timed group's input count (an object group's
 * contract inputs, members plus controls; a singleton's one), kind and stage,
 * the reviewed metrics bound every object contract must carry, and the
 * untimed groups. Capacity and the budget preflight must both hold. */
static int tp_retirement_campaign_review(TpRetirementCampaignReview const* review,
    TpRetirementMeasuredCommand const* aa_commands, TpRetirementMeasuredCommand const* ab_commands,
    unsigned groups, unsigned const* group_shapes, unsigned rows, unsigned runtime, unsigned pairs,
    TpRetirementCampaignCapacity* capacity, TpRetirementBudgetPreflight* budget)
{
    unsigned* inputs = groups ? (unsigned*)malloc((size_t)groups * 2 * sizeof(*inputs)) : NULL;
    unsigned* kinds = inputs ? inputs + groups : NULL;
    unsigned objects = 0, untimed_objects = 0;
    uint64_t metrics = 0, untimed_metrics = 0, largest = 0;
    int ok = review && review->budget && inputs && aa_commands && ab_commands && group_shapes &&
        review->group_count == groups && review->group_stages &&
        review->untimed_groups <= TP_RETIREMENT_MAX_CELLS &&
        (!review->untimed_groups || (review->untimed_inputs && review->untimed_kinds && review->untimed_stages));
    for (unsigned group = 0; ok && group < groups; ++group)
    {
        unsigned object = (group_shapes[group] & 1) == TP_RETIREMENT_GROUP_OBJECT;
        kinds[group] = group_shapes[group] & 1;
        TpRetirementBatchContract const* first = aa_commands[(size_t)group * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT].batch;
        uint64_t bound = 0;
        inputs[group] = object ? (first ? first->input_count : 0) : 1;
        ok = inputs[group] && (!object || tp_retirement_budget_metrics_bytes(review->budget, inputs[group], &bound));
        for (unsigned stage = 0; ok && object && stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
            for (unsigned variant = 0; ok && variant < 2; ++variant)
            {
                TpRetirementBatchContract const* batch = ((stage ? ab_commands : aa_commands) +
                    (size_t)group * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + variant)->batch;
                ok = batch && batch->input_count == inputs[group] && batch->metrics_bytes_max == bound;
            }
        if (ok && object)
        {
            ++objects;
            ok = tp_retirement_campaign_u64_add(metrics, bound, &metrics);
            if (bound > largest) largest = bound;
        }
    }
    for (unsigned group = 0; ok && group < review->untimed_groups; ++group)
    {
        unsigned count = review->untimed_inputs[group], kind = review->untimed_kinds[group];
        uint64_t bound = 0;
        ok = count && (kind == TP_RETIREMENT_GROUP_OBJECT ?
            tp_retirement_budget_metrics_bytes(review->budget, count, &bound) :
            kind == TP_RETIREMENT_GROUP_SINGLETON && count == 1);
        if (ok && kind == TP_RETIREMENT_GROUP_OBJECT)
        {
            ++untimed_objects;
            ok = tp_retirement_campaign_u64_add(untimed_metrics, bound, &untimed_metrics);
            if (bound > largest) largest = bound;
        }
    }
    TpRetirementCampaignShape shape = {groups, objects, rows, runtime, pairs,
        review ? review->untimed_groups : 0, untimed_objects, metrics, untimed_metrics, largest};
    TpRetirementBudgetCounts counts = {{inputs, kinds, review ? review->group_stages : NULL, groups},
        {review ? review->untimed_inputs : NULL, review ? review->untimed_kinds : NULL,
         review ? review->untimed_stages : NULL, review ? review->untimed_groups : 0}, runtime, pairs};
    ok = ok && tp_retirement_campaign_capacity(&shape, capacity) &&
        tp_retirement_budget_preflight(review->budget, &counts, budget);
    if (!ok)
    {
        if (capacity) *capacity = (TpRetirementCampaignCapacity){0};
        if (budget) *budget = (TpRetirementBudgetPreflight){0};
    }
    free(inputs);
    return ok;
}

/* Called only after the service independently authenticates its immutable
 * inputs. The adapter checks consistency and snapshots all command, batch
 * contract and oracle identities; a digest or this structure alone never
 * authenticates them. budget_pin_sha256 is the admitted recipe profile's
 * `campaign-budget-sha256=` pin, which the review's budget must hash to. Source commands are laid out per stage as
 * [group * 2 + variant] for every group, then [(G + r) * 2 + variant] for every
 * runtime row r, so command_workspace has exactly 2 * 2 * (G + U) slots and
 * identity_workspace G + U: group shapes, then runtime row IDs. */
static inline int tp_retirement_campaign_freeze(TpRetirementCampaign* campaign, TpRetirementPlan const* plan,
    TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementExecutable const* aa_binary, TpRetirementExecutable const* ab_baseline,
    TpRetirementExecutable const* ab_candidate, TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands, TpRetirementCampaignCommand* command_workspace,
    size_t command_count, unsigned* identity_workspace, size_t identity_count,
    unsigned population_rows, TpRetirementCampaignReview const* review, char const* budget_pin_sha256,
    char const* plan_sha256, char const* context_sha256)
{
    TpRetirementExecution* a = aa && aa->transcript ? aa->transcript->execution : NULL;
    TpRetirementExecution* b = ab && ab->transcript ? ab->transcript->execution : NULL;
    unsigned groups = a ? a->groups : 0, runtime = a ? a->runtime_count : 0;
    TpRetirementCampaignCapacity capacity = {0};
    TpRetirementBudgetPreflight budget = {0};
    char budget_sha256[65] = {0};
    int ok = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_UNAVAILABLE && plan && a && b && groups &&
        groups <= TP_RETIREMENT_MAX_CELLS && a->population_rows == population_rows &&
        b->population_rows == population_rows &&
        command_count == (size_t)(groups + runtime) * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT *
                         TP_RETIREMENT_CAMPAIGN_STAGES && identity_count == (size_t)groups + runtime &&
        command_workspace && identity_workspace && aa_commands && ab_commands &&
        tp_retirement_digest(plan_sha256) && tp_retirement_digest(context_sha256) &&
        plan->version == TP_RETIREMENT_STATISTICS_VERSION && plan->frozen_before_samples == 1 &&
        plan->seed && plan->pairs_per_round == a->pairs && plan->pairs_per_round == b->pairs &&
        plan->resamples >= TP_RETIREMENT_MIN_RESAMPLES &&
        plan->resamples <= TP_RETIREMENT_MAX_RESAMPLES &&
        plan->bootstrap_members_per_scope &&
        plan->bootstrap_members_per_scope <= TP_RETIREMENT_MAX_BOOTSTRAP_MEMBERS_PER_SCOPE &&
        plan->cell_members_per_scope && plan->cell_members_per_scope <= TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE &&
        aa_binary && ab_baseline && ab_candidate && aa_binary->valid && ab_baseline->valid && ab_candidate->valid &&
        aa_binary->descriptor == ab_baseline->descriptor &&
        !strcmp(aa_binary->sha256, ab_baseline->sha256) &&
        a->seed == plan->seed && b->seed == plan->seed && b->groups == groups && b->runtime_count == runtime &&
        aa->rows != ab->rows && aa->groups != ab->groups && aa->members != ab->members &&
        a->first_orders != b->first_orders && a->runtime_rows != b->runtime_rows &&
        aa->row_count == ab->row_count && aa->group_count == groups && ab->group_count == groups &&
        aa->object_count == ab->object_count && a->expected == b->expected &&
        !a->sequence && !b->sequence && !a->failed && !b->failed &&
        !a->pending && !b->pending && aa->spool && ab->spool && aa->spool != ab->spool &&
        !aa->failed && !ab->failed && !aa->collected && !ab->collected &&
        !aa->exporting && !ab->exporting && !aa->finished && !ab->finished &&
        aa->transcript != ab->transcript &&
        aa->transcript->stream && ab->transcript->stream &&
        aa->transcript->stream != ab->transcript->stream &&
        !aa->transcript->failed && !ab->transcript->failed &&
        !aa->transcript->total_records && !ab->transcript->total_records &&
        aa->transcript->cpu == ab->transcript->cpu &&
        aa->transcript->attempt == ab->transcript->attempt &&
        aa->transcript->bound_at_ns == ab->transcript->bound_at_ns &&
        !strcmp(aa->transcript->job, ab->transcript->job) &&
        !strcmp(aa->transcript->boot, ab->transcript->boot) &&
        (!aa->object_count || (aa->metrics && ab->metrics && aa->metrics != ab->metrics &&
            strcmp(aa->metrics->tag, ab->metrics->tag) && strcmp(aa->metrics->tag, TP_RETIREMENT_UNTIMED_METRICS_TAG) &&
            strcmp(ab->metrics->tag, TP_RETIREMENT_UNTIMED_METRICS_TAG) && aa->metrics->stream != ab->metrics->stream &&
            !aa->metrics->artifacts && !ab->metrics->artifacts)) &&
        review && review->budget && tp_retirement_digest(budget_pin_sha256) &&
        tp_retirement_budget_digest(review->budget, budget_sha256) &&
        !strcmp(budget_sha256, budget_pin_sha256);
    /* Both stages must carry the same frozen layout. */
    for (unsigned row = 0; ok && row < aa->row_count; ++row)
        ok = aa->rows[row].id == ab->rows[row].id && aa->rows[row].metrics == ab->rows[row].metrics &&
            aa->rows[row].group == ab->rows[row].group && aa->members[row] == ab->members[row];
    for (unsigned group = 0; ok && group < groups; ++group)
    {
        TpRetirementSampleGroup const* left = &aa->groups[group];
        TpRetirementSampleGroup const* right = &ab->groups[group];
        ok = left->first == right->first && left->count == right->count && left->kind == right->kind &&
            left->object == right->object;
        if (ok) identity_workspace[group] = tp_retirement_campaign_group_shape(left);
    }
    for (unsigned r = 0; ok && r < runtime; ++r)
    {
        ok = a->runtime_rows[r] == b->runtime_rows[r];
        if (ok) identity_workspace[groups + r] = a->runtime_rows[r];
    }
    for (unsigned stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
        for (unsigned slot = 0; ok && slot < groups + runtime; ++slot)
            for (unsigned variant = 0; ok && variant < 2; ++variant)
            {
                size_t index = tp_retirement_campaign_index(groups, runtime, stage, slot, variant);
                TpRetirementMeasuredCommand const* source = (stage ? ab_commands : aa_commands) +
                    (size_t)slot * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + variant;
                unsigned kind = slot >= groups;
                ok = tp_retirement_campaign_command_copy(command_workspace + index, source,
                    kind ? identity_workspace[slot] : slot, kind, variant, kind ? 0 : identity_workspace[slot]);
            }
    /* (M4) Every object contract carries the budget's reviewed metrics bound
     * for its input count; the bounds size the metrics shards, and the batch
     * sizes and counts must fit the reviewed budget before any timing. */
    if (ok) ok = tp_retirement_campaign_review(review, aa_commands, ab_commands, groups, identity_workspace,
        aa->row_count, runtime, a->pairs, &capacity, &budget);
    ok = ok && a->expected == capacity.invocations_per_stage &&
        aa->expected == capacity.samples_per_stage && ab->expected == capacity.samples_per_stage;
    if (campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_UNAVAILABLE)
    {
        *campaign = (TpRetirementCampaign){.phase = TP_RETIREMENT_CAMPAIGN_INVALID};
        if (ok)
        {
            campaign->plan = *plan;
            campaign->samples[0] = aa; campaign->samples[1] = ab;
            campaign->binaries[0][0] = campaign->binaries[0][1] = aa_binary;
            campaign->binaries[1][0] = ab_baseline;
            campaign->binaries[1][1] = ab_candidate;
            campaign->commands = command_workspace;
            campaign->group_shapes = identity_workspace;
            campaign->runtime_rows = identity_workspace + groups;
            campaign->groups = groups;
            campaign->runtime_count = runtime;
            campaign->population_rows = population_rows;
            campaign->capacity = capacity;
            campaign->budget = budget;
            memcpy(campaign->budget_sha256, budget_sha256, 65);
            campaign->phase = TP_RETIREMENT_CAMPAIGN_AA;
            memcpy(campaign->plan_sha256, plan_sha256, 65);
            memcpy(campaign->context_sha256, context_sha256, 65);
            for (unsigned stage = 0; stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
                for (unsigned variant = 0; variant < 2; ++variant)
                    memcpy(campaign->binary_sha256[stage][variant],
                        campaign->binaries[stage][variant]->sha256, 65);
            memcpy(campaign->job, aa->transcript->job, sizeof(campaign->job));
            memcpy(campaign->boot, aa->transcript->boot, sizeof(campaign->boot));
            campaign->attempt = aa->transcript->attempt;
            campaign->bound_at_ns = aa->transcript->bound_at_ns;
            campaign->cpu = aa->transcript->cpu;
        }
    }
    if (!ok)
    {
        if (campaign) campaign->phase = TP_RETIREMENT_CAMPAIGN_INVALID;
        if (aa) tp_retirement_samples_poison(aa);
        if (ab && ab != aa) tp_retirement_samples_poison(ab);
    }
    return ok;
}

static void tp_retirement_campaign_poison(TpRetirementCampaign* campaign)
{
    if (campaign)
    {
        campaign->phase = TP_RETIREMENT_CAMPAIGN_INVALID;
        for (unsigned i = 0; i < TP_RETIREMENT_CAMPAIGN_STAGES; ++i)
            if (campaign->samples[i]) tp_retirement_samples_poison(campaign->samples[i]);
    }
}

/* Completion of the invocation cursor is not completion of its numeric
 * evidence. Export verifies every spool unit against the observed transcript. */
static int tp_retirement_campaign_stage_ready(TpRetirementCampaign const* campaign, unsigned stage)
{
    TpRetirementSamples const* samples = campaign && stage < TP_RETIREMENT_CAMPAIGN_STAGES ?
        campaign->samples[stage] : NULL;
    TpRetirementTranscript const* transcript = samples ? samples->transcript : NULL;
    int ok = samples && transcript && !samples->failed && !transcript->failed &&
        transcript->finished && samples->finished && samples->exporting &&
        samples->collected == campaign->capacity.invocations_per_stage &&
        samples->exported == campaign->capacity.samples_per_stage &&
        (uint64_t)samples->shards[0] + samples->shards[1] == campaign->capacity.sample_shards_per_stage &&
        tp_retirement_digest(samples->raw_sha256) &&
        tp_retirement_digest(samples->descriptors_sha256[0]) &&
        (!samples->object_count || tp_retirement_digest(samples->descriptors_sha256[1])) &&
        (!samples->object_count || (samples->metrics && samples->metrics->finished && !samples->metrics->failed &&
            samples->metrics->artifacts == campaign->capacity.metrics_artifacts_per_stage &&
            samples->metrics->total_bytes <= campaign->capacity.metrics_bytes_per_stage_upper_bound &&
            samples->metrics->index < campaign->capacity.metrics_shards_per_stage_upper_bound)) &&
        transcript->attempt == campaign->attempt &&
        !strcmp(transcript->job, campaign->job) &&
        !strcmp(transcript->boot, campaign->boot);
    return ok;
}

/* Collection completion is not a validity, A/B verdict, or admitted receipt.
 * The service and independent #511 replay own those separate decisions. */
static inline TpRetirementCampaignOutcome tp_retirement_campaign_outcome(TpRetirementCampaign const* campaign)
{
    TpRetirementCampaignOutcome outcome = {0};
    if (campaign)
    {
        if (campaign->phase == TP_RETIREMENT_CAMPAIGN_INVALID)
            outcome.execution = TP_RETIREMENT_CAMPAIGN_STATE_FAILED;
        else if (campaign->phase == TP_RETIREMENT_CAMPAIGN_COLLECTED &&
                 tp_retirement_campaign_stage_ready(campaign, 0) &&
                 tp_retirement_campaign_stage_ready(campaign, 1))
            outcome.execution = TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE;
        else if (campaign->phase != TP_RETIREMENT_CAMPAIGN_UNAVAILABLE)
            outcome.execution = TP_RETIREMENT_CAMPAIGN_STATE_RUNNING;
        if (campaign->phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA)
            outcome.aa_qualification = TP_RETIREMENT_CAMPAIGN_STATE_RUNNING;
        else if (campaign->phase == TP_RETIREMENT_CAMPAIGN_AB ||
                 campaign->phase == TP_RETIREMENT_CAMPAIGN_COLLECTED)
            outcome.aa_qualification = TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE;
        else if (campaign->phase == TP_RETIREMENT_CAMPAIGN_INVALID)
            outcome.aa_qualification = TP_RETIREMENT_CAMPAIGN_STATE_FAILED;
    }
    return outcome;
}

/* A child runs only after freeze and only for the next predeclared cursor item.
 * The caller supplies a fresh log/output directory and handles shard rotation
 * and durable publication through #1023. No failed launch is retried. */
static inline int tp_retirement_campaign_run(TpRetirementCampaign* campaign,
    TpRetirementMeasuredCommand const* command, TpProcessInputs const* inputs,
    int output_directory, TpRetirementMeasurementResult* result)
{
    unsigned stage = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_AB ? 1 : 0;
    TpRetirementSamples* samples = campaign ? campaign->samples[stage] : NULL;
    TpRetirementExecution* execution = samples && samples->transcript ? samples->transcript->execution : NULL;
    TpRetirementInvocation invocation;
    int ok = campaign && result && command && inputs &&
        (campaign->phase == TP_RETIREMENT_CAMPAIGN_AA || campaign->phase == TP_RETIREMENT_CAMPAIGN_AB) &&
        execution && !samples->failed && samples->transcript->stream &&
        samples->transcript->attempt == campaign->attempt &&
        samples->transcript->bound_at_ns == campaign->bound_at_ns &&
        samples->transcript->cpu == campaign->cpu &&
        !strcmp(samples->transcript->job, campaign->job) &&
        !strcmp(samples->transcript->boot, campaign->boot) &&
        samples->transcript->records < TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
        tp_retirement_execution_peek(execution, &invocation) == TP_RETIREMENT_NEXT_READY;
    TpRetirementCampaignCommand checked;
    if (ok)
    {
        unsigned slot = invocation.kind ? campaign->groups + invocation.dense : invocation.group;
        ok = invocation.kind ? invocation.dense < campaign->runtime_count &&
                campaign->runtime_rows[invocation.dense] == invocation.row :
            invocation.group < campaign->groups && invocation.group < samples->group_count &&
                campaign->group_shapes[invocation.group] ==
                    tp_retirement_campaign_group_shape(&samples->groups[invocation.group]);
        if (ok) ok = tp_retirement_campaign_command_copy(&checked, command,
            invocation.kind ? invocation.row : invocation.group, invocation.kind, invocation.variant,
            invocation.kind ? 0 : campaign->group_shapes[invocation.group]);
        if (ok)
        {
            size_t index = tp_retirement_campaign_index(campaign->groups, campaign->runtime_count, stage,
                slot, invocation.variant);
            TpRetirementCampaignCommand const* frozen = campaign->commands + index;
            ok = !memcmp(&checked, frozen, sizeof(checked)) &&
                 !strcmp(campaign->binaries[stage][invocation.variant]->sha256,
                         campaign->binary_sha256[stage][invocation.variant]) &&
                 execution->seed == campaign->plan.seed && execution->pairs == campaign->plan.pairs_per_round;
        }
    }
    if (ok) ok = tp_retirement_measurement_run(samples, command,
        campaign->binaries[stage][invocation.variant], inputs, output_directory, result);
    if (!ok)
    {
        if (result && (!samples || !samples->failed))
            *result = (TpRetirementMeasurementResult){.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
                .process = {.exit_code = -1}};
        tp_retirement_campaign_poison(campaign);
    }
    return ok;
}
#endif

/* Close the current fixed-size shard and start another service-owned stream.
 * A short intermediate shard is rejected by the underlying transcript. */
static inline int tp_retirement_campaign_rotate(TpRetirementCampaign* campaign, FILE* next,
    TpRetirementShard* completed)
{
    unsigned stage = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_AB ? 1 : 0;
    TpRetirementTranscript* transcript = campaign && campaign->samples[stage] ?
        campaign->samples[stage]->transcript : NULL;
    int ok = campaign && (campaign->phase == TP_RETIREMENT_CAMPAIGN_AA ||
        campaign->phase == TP_RETIREMENT_CAMPAIGN_AB) && transcript && next &&
        transcript->records == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
        tp_retirement_transcript_end_shard(transcript, completed) &&
        tp_retirement_transcript_begin_shard(transcript, next);
    if (!ok) tp_retirement_campaign_poison(campaign);
    return ok;
}

/* A/A remains waiting for the separate approved #426 decision. A/B is enabled
 * only after the service independently validates its admission receipt against
 * this frozen job, plan and context; that authority is supplied by #1021. A
 * stage with object groups must first finish its metrics shard writer. */
static inline int tp_retirement_campaign_finish_stage(TpRetirementCampaign* campaign,
    uint64_t completed_at_ns, TpRetirementShard* final_shard)
{
    unsigned stage = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_AB ? 1 : 0;
    TpRetirementSamples* samples = campaign ? campaign->samples[stage] : NULL;
    TpRetirementTranscript* transcript = samples ? samples->transcript : NULL;
    int ok = campaign && (campaign->phase == TP_RETIREMENT_CAMPAIGN_AA ||
        campaign->phase == TP_RETIREMENT_CAMPAIGN_AB) && transcript &&
        tp_retirement_execution_complete(transcript->execution) &&
        samples->collected == campaign->capacity.invocations_per_stage &&
        (!samples->object_count || (samples->metrics && samples->metrics->finished && !samples->metrics->failed)) &&
        tp_retirement_transcript_end_shard(transcript, final_shard) &&
        tp_retirement_transcript_finish(transcript, completed_at_ns) &&
        tp_retirement_samples_begin_export(samples);
    if (ok) campaign->phase = stage ? TP_RETIREMENT_CAMPAIGN_COLLECTED : TP_RETIREMENT_CAMPAIGN_AWAIT_AA;
    else tp_retirement_campaign_poison(campaign);
    return ok;
}

/* This transition is compiled for functional fixtures only. An authenticated
 * #426 A/A evaluator and #1021 supervisor handoff must implement production
 * admission; comparing a local digest cannot make a receipt authoritative.
 * Until that handoff lands, production cannot enter A/B. */
#ifdef TP_RETIREMENT_CAMPAIGN_FIXTURE_AA
/* Lets a later functional-fixture consumer (retirement_unit_campaign.h) check
 * that this stand-in was compiled into its translation unit. */
#define TP_RETIREMENT_CAMPAIGN_FIXTURE_AA_COMPILED 1
static int tp_retirement_campaign_admit_aa_fixture(TpRetirementCampaign* campaign, int admitted,
    char const* checked_plan_sha256, char const* checked_context_sha256,
    char const* aa_receipt_sha256)
{
    int ok = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA && admitted == 1 &&
        checked_plan_sha256 && checked_context_sha256 &&
        !strcmp(checked_plan_sha256, campaign->plan_sha256) &&
        !strcmp(checked_context_sha256, campaign->context_sha256) &&
        tp_retirement_digest(aa_receipt_sha256) &&
        tp_retirement_campaign_stage_ready(campaign, 0) &&
        !campaign->samples[1]->collected && !campaign->samples[1]->failed;
    if (ok) campaign->phase = TP_RETIREMENT_CAMPAIGN_AB;
    else tp_retirement_campaign_poison(campaign);
    return ok;
}
#endif
#endif
