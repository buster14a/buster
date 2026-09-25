/* Private #881-D collection coordinator. The service owns the authenticated
 * preparation/oracle/host inputs, exclusive streams and the #426 A/A verdict.
 * freeze copies the fixed command/output identities before any child starts;
 * run only accepts those identities in the existing #619 cursor order. The
 * state machine retains an interrupted attempt as invalid. It does not confer
 * receipt authority, evaluate #426, or admit a service recipe.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_CAMPAIGN_H
#define BUSTER_THROUGHPUT_RETIREMENT_CAMPAIGN_H
#include "retirement_measurement.h"

#ifdef __linux__
#define TP_RETIREMENT_CAMPAIGN_STAGES 2u
#define TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_ROW 4u
#define TP_RETIREMENT_CAMPAIGN_WHOLE_JOB_BUDGET_NS UINT64_C(3600000000000)

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

typedef struct TpRetirementCampaignCapacity
{
    uint64_t compiler_invocations_per_stage, runtime_invocations_per_stage;
    uint64_t invocations_per_stage, samples_per_stage, spool_bytes_per_stage;
    uint64_t transcript_bytes_per_stage_upper_bound;
    uint64_t transcript_shards_per_stage, sample_shards_per_stage, sample_partitions_per_stage;
    uint64_t total_compiler_invocations, total_runtime_invocations;
    uint64_t total_invocations, total_samples, total_spool_bytes;
    uint64_t total_transcript_bytes_upper_bound;
    uint64_t total_transcript_shards, total_sample_shards, total_sample_partitions;
} TpRetirementCampaignCapacity;

/* Supplied by the caller only after it independently authenticates each
 * phase ceiling for this exact job, population, host and held binaries. The
 * invocation ceilings include launcher/collector work for every applicable
 * compiler metric (wall, RSS and parsed code bytes) or runtime oracle. */
typedef struct TpRetirementCampaignDurationBounds
{
    uint64_t reservation_ns, materialization_ns;
    uint64_t baseline_build_ns, candidate_build_ns, correctness_ns;
    uint64_t settling_per_stage_ns, compiler_invocation_ns, runtime_invocation_ns;
    uint64_t aa_qualification_ns, aa_receipt_sealing_ns;
    uint64_t sample_export_per_stage_ns, final_statistics_ns, final_sealing_ns;
    uint64_t cleanup_ns;
} TpRetirementCampaignDurationBounds;

typedef struct TpRetirementCampaignPreflight
{
    uint64_t fixed_phase_ns, compiler_invocation_total_ns, runtime_invocation_total_ns;
    uint64_t required_ns, remaining_ns;
    int fits;
} TpRetirementCampaignPreflight;

/* The command hash covers argv/cwd/environment; all other oracle fields are
 * copied separately. One entry exists for each (canonical eligible row, kind,
 * variant), including empty entries for inapplicable runtime rows. */
typedef struct TpRetirementCampaignCommand
{
    char command_sha256[65], output_sha256[65], code_sha256[65], artifact[128];
    uint64_t code_bytes;
    unsigned timeout_seconds;
} TpRetirementCampaignCommand;

typedef struct TpRetirementCampaign
{
    TpRetirementPlan plan;
    TpRetirementSamples* samples[TP_RETIREMENT_CAMPAIGN_STAGES];
    TpRetirementExecutable const* binaries[TP_RETIREMENT_CAMPAIGN_STAGES][2];
    TpRetirementCampaignCommand* commands;
    unsigned* row_ids;
    unsigned* runtime_rows;
    unsigned* metrics;
    unsigned rows, runtime_count, population_rows;
    TpRetirementCampaignCapacity capacity;
    TpRetirementCampaignPhase phase;
    char plan_sha256[65], context_sha256[65];
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

/* Rows and runtime_rows must come from the authenticated correctness/oracle
 * gate. The report includes both fixed stages and a worst-case transcript
 * byte bound (every line at its current 8 KiB validator limit); it does not
 * predict host speed or establish that the whole-job deadline can be met. */
static int tp_retirement_campaign_capacity(unsigned rows, unsigned runtime_rows, unsigned pairs,
    TpRetirementCampaignCapacity* capacity)
{
    uint64_t per_variant = 0, invocations_per_row = 0;
    uint64_t compiler_invocations = 0, runtime_invocations = 0, invocations = 0;
    uint64_t samples = tp_retirement_samples_count(rows, pairs), spool_bytes = 0;
    uint64_t transcript_bytes = 0;
    uint64_t transcript_shards = 0, sample_shards = 0;
    uint64_t total_compiler_invocations = 0, total_runtime_invocations = 0;
    uint64_t total_invocations = 0, total_samples = 0, total_spool_bytes = 0;
    uint64_t total_transcript_bytes = 0;
    uint64_t total_transcript_shards = 0, total_sample_shards = 0, total_sample_partitions = 0;
    int ok = capacity && rows && runtime_rows <= rows && samples &&
        tp_retirement_campaign_u64_mul(TP_RETIREMENT_ROUNDS, pairs, &per_variant) &&
        tp_retirement_campaign_u64_add(per_variant, TP_RETIREMENT_WARMUPS, &per_variant) &&
        tp_retirement_campaign_u64_mul(per_variant, 2, &invocations_per_row) &&
        tp_retirement_campaign_u64_mul(rows, invocations_per_row, &compiler_invocations) &&
        tp_retirement_campaign_u64_mul(runtime_rows, invocations_per_row, &runtime_invocations) &&
        tp_retirement_campaign_u64_add(compiler_invocations, runtime_invocations, &invocations) &&
        tp_retirement_campaign_u64_mul(samples, TP_RETIREMENT_SAMPLE_RECORD_BYTES, &spool_bytes) &&
        tp_retirement_campaign_u64_mul(invocations, TP_RETIREMENT_EXECUTION_LINE_CAP, &transcript_bytes);
    if (ok)
    {
        transcript_shards = tp_retirement_campaign_ceil_div(invocations,
            TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
        sample_shards = tp_retirement_campaign_ceil_div(samples,
            TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
        ok = invocations <= (uint64_t)TP_RETIREMENT_TRANSCRIPT_SHARDS * TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS &&
            transcript_shards <= TP_RETIREMENT_TRANSCRIPT_SHARDS &&
            samples <= TP_RETIREMENT_SAMPLE_TOTAL_RECORDS &&
            sample_shards <= TP_RETIREMENT_TRANSCRIPT_SHARDS &&
            tp_retirement_campaign_u64_mul(compiler_invocations, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_compiler_invocations) &&
            tp_retirement_campaign_u64_mul(runtime_invocations, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_runtime_invocations) &&
            tp_retirement_campaign_u64_mul(invocations, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_invocations) &&
            tp_retirement_campaign_u64_mul(samples, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_samples) &&
            tp_retirement_campaign_u64_mul(spool_bytes, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_spool_bytes) &&
            tp_retirement_campaign_u64_mul(transcript_bytes, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_transcript_bytes) &&
            tp_retirement_campaign_u64_mul(transcript_shards, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_transcript_shards) &&
            tp_retirement_campaign_u64_mul(sample_shards, TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_sample_shards) &&
            tp_retirement_campaign_u64_mul(tp_retirement_campaign_ceil_div(samples,
                TP_RETIREMENT_SAMPLE_PARTITION_RECORDS), TP_RETIREMENT_CAMPAIGN_STAGES,
                &total_sample_partitions);
    }
    if (capacity)
    {
        *capacity = (TpRetirementCampaignCapacity){0};
        if (ok)
        {
            capacity->compiler_invocations_per_stage = compiler_invocations;
            capacity->runtime_invocations_per_stage = runtime_invocations;
            capacity->invocations_per_stage = invocations;
            capacity->samples_per_stage = samples;
            capacity->spool_bytes_per_stage = spool_bytes;
            capacity->transcript_bytes_per_stage_upper_bound = transcript_bytes;
            capacity->transcript_shards_per_stage = transcript_shards;
            capacity->sample_shards_per_stage = sample_shards;
            capacity->sample_partitions_per_stage =
                tp_retirement_campaign_ceil_div(samples, TP_RETIREMENT_SAMPLE_PARTITION_RECORDS);
            capacity->total_compiler_invocations = total_compiler_invocations;
            capacity->total_runtime_invocations = total_runtime_invocations;
            capacity->total_invocations = total_invocations;
            capacity->total_samples = total_samples;
            capacity->total_spool_bytes = total_spool_bytes;
            capacity->total_transcript_bytes_upper_bound = total_transcript_bytes;
            capacity->total_transcript_shards = total_transcript_shards;
            capacity->total_sample_shards = total_sample_shards;
            capacity->total_sample_partitions = total_sample_partitions;
        }
    }
    return ok;
}

/* Failure-first whole-job preflight. This arithmetic helper has no production
 * caller: the integrator must first authenticate nonzero worst-case bounds
 * from the trusted service for this job. Missing bounds, overflow, an
 * inconsistent capacity record or a sum above the fixed one-hour job budget
 * rejects before timing; fixture bounds are not authority. */
static inline int tp_retirement_campaign_preflight(TpRetirementCampaignCapacity const* capacity,
    TpRetirementCampaignDurationBounds const* bounds, TpRetirementCampaignPreflight* preflight)
{
    uint64_t expected_invocations = 0, settling_ns = 0, export_ns = 0;
    uint64_t compiler_ns = 0, runtime_ns = 0, fixed_ns = 0, required_ns = 0;
    int ok = capacity && bounds && preflight && capacity->total_invocations &&
        capacity->total_compiler_invocations && capacity->total_samples &&
        bounds->reservation_ns && bounds->materialization_ns && bounds->baseline_build_ns &&
        bounds->candidate_build_ns && bounds->correctness_ns && bounds->settling_per_stage_ns &&
        bounds->compiler_invocation_ns && bounds->aa_qualification_ns &&
        bounds->aa_receipt_sealing_ns && bounds->sample_export_per_stage_ns &&
        bounds->final_statistics_ns && bounds->final_sealing_ns && bounds->cleanup_ns &&
        (!capacity->total_runtime_invocations || bounds->runtime_invocation_ns) &&
        (capacity->total_runtime_invocations || !bounds->runtime_invocation_ns) &&
        tp_retirement_campaign_u64_add(capacity->total_compiler_invocations,
            capacity->total_runtime_invocations, &expected_invocations) &&
        expected_invocations == capacity->total_invocations &&
        tp_retirement_campaign_u64_mul(bounds->settling_per_stage_ns,
            TP_RETIREMENT_CAMPAIGN_STAGES, &settling_ns) &&
        tp_retirement_campaign_u64_mul(bounds->sample_export_per_stage_ns,
            TP_RETIREMENT_CAMPAIGN_STAGES, &export_ns) &&
        tp_retirement_campaign_u64_mul(capacity->total_compiler_invocations,
            bounds->compiler_invocation_ns, &compiler_ns) &&
        tp_retirement_campaign_u64_mul(capacity->total_runtime_invocations,
            bounds->runtime_invocation_ns, &runtime_ns);
    if (ok)
    {
        uint64_t fixed_parts[] = {
            bounds->reservation_ns, bounds->materialization_ns,
            bounds->baseline_build_ns, bounds->candidate_build_ns,
            bounds->correctness_ns, settling_ns, bounds->aa_qualification_ns,
            bounds->aa_receipt_sealing_ns, export_ns, bounds->final_statistics_ns,
            bounds->final_sealing_ns, bounds->cleanup_ns
        };
        for (unsigned i = 0; ok && i < BUSTER_ARRAY_LENGTH(fixed_parts); ++i)
            ok = tp_retirement_campaign_u64_add(fixed_ns, fixed_parts[i], &fixed_ns);
        ok = ok && tp_retirement_campaign_u64_add(fixed_ns, compiler_ns, &required_ns) &&
            tp_retirement_campaign_u64_add(required_ns, runtime_ns, &required_ns);
    }
    if (preflight) *preflight = (TpRetirementCampaignPreflight){0};
    if (ok && preflight)
    {
        preflight->fixed_phase_ns = fixed_ns;
        preflight->compiler_invocation_total_ns = compiler_ns;
        preflight->runtime_invocation_total_ns = runtime_ns;
        preflight->required_ns = required_ns;
        preflight->fits = required_ns <= TP_RETIREMENT_CAMPAIGN_WHOLE_JOB_BUDGET_NS;
        if (preflight->fits)
            preflight->remaining_ns = TP_RETIREMENT_CAMPAIGN_WHOLE_JOB_BUDGET_NS - required_ns;
        ok = preflight->fits;
    }
    else ok = 0;
    return ok;
}

static int tp_retirement_campaign_command_copy(TpRetirementCampaignCommand* target,
    TpRetirementMeasuredCommand const* command, unsigned row, unsigned kind, unsigned variant)
{
    char digest[65];
    size_t artifact = 0;
    if (command && command->artifact)
        while (artifact < sizeof(target->artifact) && command->artifact[artifact]) ++artifact;
    int ok = target && command && command->row == row && command->kind == kind &&
        command->variant == variant && command->timeout_seconds && command->timeout_seconds <= 86400 &&
        tp_retirement_digest(command->command_sha256) && tp_retirement_digest(command->output_sha256) &&
        tp_retirement_command_hash(command, digest) && !strcmp(digest, command->command_sha256) &&
        (kind ? !artifact && !command->code_section_bytes && !command->code_section_sha256 :
                artifact && artifact < sizeof(target->artifact) && tp_retirement_artifact_leaf(command->artifact)) &&
        (command->code_section_sha256 ? tp_retirement_digest(command->code_section_sha256) :
                                        !command->code_section_bytes);
    if (target)
    {
        *target = (TpRetirementCampaignCommand){0};
        if (ok)
        {
            memcpy(target->command_sha256, command->command_sha256, 65);
            memcpy(target->output_sha256, command->output_sha256, 65);
            if (command->code_section_sha256) memcpy(target->code_sha256, command->code_section_sha256, 65);
            if (artifact) memcpy(target->artifact, command->artifact, artifact + 1);
            target->code_bytes = command->code_section_bytes;
            target->timeout_seconds = command->timeout_seconds;
        }
    }
    return ok;
}

static size_t tp_retirement_campaign_index(unsigned rows, unsigned stage, unsigned dense,
    unsigned kind, unsigned variant)
{
    size_t index = (((size_t)stage * rows + dense) * 2 + kind) * 2 + variant;
    return index;
}

/* Called only after the service independently authenticates its immutable
 * inputs. The adapter checks consistency and snapshots all command and oracle
 * identities; a digest or this structure alone never authenticates them.
 * command_workspace has exactly 8*rows slots, each checked before timing.
 * identity_workspace has exactly 3*rows slots: row IDs, runtime IDs, metrics.
 */
static int tp_retirement_campaign_freeze(TpRetirementCampaign* campaign, TpRetirementPlan const* plan,
    TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementExecutable const* aa_binary, TpRetirementExecutable const* ab_baseline,
    TpRetirementExecutable const* ab_candidate, TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands, TpRetirementCampaignCommand* command_workspace,
    size_t command_count, unsigned* identity_workspace, size_t identity_count,
    unsigned population_rows, char const* plan_sha256, char const* context_sha256)
{
    TpRetirementExecution* a = aa && aa->transcript ? aa->transcript->execution : NULL;
    TpRetirementExecution* b = ab && ab->transcript ? ab->transcript->execution : NULL;
    unsigned rows = a ? a->rows : 0;
    TpRetirementCampaignCapacity capacity;
    int ok = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_UNAVAILABLE && plan && a && b && rows &&
        rows <= TP_RETIREMENT_MAX_CELLS && population_rows >= rows &&
        population_rows <= TP_RETIREMENT_MAX_CELLS &&
        command_count == (size_t)rows * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_ROW *
                         TP_RETIREMENT_CAMPAIGN_STAGES && identity_count == (size_t)rows * 3 &&
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
        a->seed == plan->seed && b->seed == plan->seed && a->runtime_count == b->runtime_count &&
        aa->rows != ab->rows && a->first_orders != b->first_orders &&
        a->runtime_rows != b->runtime_rows && (!a->row_ids || a->row_ids != b->row_ids) &&
        b->rows == rows && a->expected == b->expected &&
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
        tp_retirement_campaign_capacity(rows, a->runtime_count, a->pairs, &capacity) &&
        a->expected == capacity.invocations_per_stage &&
        aa->expected == capacity.samples_per_stage && ab->expected == capacity.samples_per_stage;
    for (unsigned i = 0; ok && i < rows; ++i)
    {
        unsigned id = a->row_ids ? a->row_ids[i] : i;
        ok = id < population_rows && (!i || id > identity_workspace[i - 1]) &&
             (b->row_ids ? b->row_ids[i] : i) == id &&
             aa->rows[i].metrics == ab->rows[i].metrics &&
             !(aa->rows[i].metrics & ~(TP_RETIREMENT_SAMPLE_CODE | TP_RETIREMENT_SAMPLE_RUNTIME |
                                      TP_RETIREMENT_SAMPLE_ZERO_BASELINE_CODE));
        if (ok)
        {
            identity_workspace[i] = id;
            identity_workspace[rows * 2 + i] = aa->rows[i].metrics;
        }
    }
    for (unsigned i = 0; ok && i < a->runtime_count; ++i)
    {
        ok = a->runtime_rows[i] == b->runtime_rows[i] && a->runtime_rows[i] < rows &&
             (!i || a->runtime_rows[i] > identity_workspace[rows + i - 1]);
        if (ok) identity_workspace[rows + i] = a->runtime_rows[i];
    }
    unsigned runtime = 0;
    for (unsigned i = 0; ok && i < rows; ++i)
    {
        int expected_runtime = runtime < a->runtime_count && identity_workspace[rows + runtime] == i;
        ok = !!(identity_workspace[rows * 2 + i] & TP_RETIREMENT_SAMPLE_RUNTIME) == expected_runtime;
        if (expected_runtime) ++runtime;
        for (unsigned stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
            for (unsigned kind = 0; ok && kind < 2; ++kind)
                for (unsigned variant = 0; ok && variant < 2; ++variant)
                {
                    size_t index = tp_retirement_campaign_index(rows, stage, i, kind, variant);
                    TpRetirementMeasuredCommand const* source =
                        (stage ? ab_commands : aa_commands) + i * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_ROW + kind * 2 + variant;
                    TpRetirementCampaignCommand* target = command_workspace + index;
                    if (kind && !expected_runtime)
                        ok = !source->arguments && !source->environment && !source->command_sha256 &&
                             !source->output_sha256 && !source->artifact && !source->code_section_sha256;
                    else
                    {
                        ok = tp_retirement_campaign_command_copy(target, source, identity_workspace[i], kind, variant);
                        if (ok && !kind)
                        {
                            unsigned metrics = identity_workspace[rows * 2 + i];
                            ok = (metrics & (TP_RETIREMENT_SAMPLE_CODE | TP_RETIREMENT_SAMPLE_ZERO_BASELINE_CODE)) ?
                                !!target->code_sha256[0] : !target->code_sha256[0] && !target->code_bytes;
                            if (ok && (metrics & TP_RETIREMENT_SAMPLE_ZERO_BASELINE_CODE) && !variant)
                                ok = !target->code_bytes;
                            if (ok && (metrics & TP_RETIREMENT_SAMPLE_CODE) && !variant)
                                ok = target->code_bytes > 0;
                        }
                    }
                }
    }
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
            campaign->row_ids = identity_workspace;
            campaign->runtime_rows = identity_workspace + rows;
            campaign->metrics = identity_workspace + rows * 2;
            campaign->rows = rows;
            campaign->runtime_count = a->runtime_count;
            campaign->population_rows = population_rows;
            campaign->capacity = capacity;
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
 * evidence. Export verifies every spool row against the observed transcript. */
static int tp_retirement_campaign_stage_ready(TpRetirementCampaign const* campaign, unsigned stage)
{
    TpRetirementSamples const* samples = campaign && stage < TP_RETIREMENT_CAMPAIGN_STAGES ?
        campaign->samples[stage] : NULL;
    TpRetirementTranscript const* transcript = samples ? samples->transcript : NULL;
    int ok = samples && transcript && !samples->failed && !transcript->failed &&
        transcript->finished && samples->finished && samples->exporting &&
        samples->collected == campaign->capacity.invocations_per_stage &&
        samples->exported == campaign->capacity.samples_per_stage &&
        samples->shards == campaign->capacity.sample_shards_per_stage &&
        tp_retirement_digest(samples->raw_sha256) &&
        tp_retirement_digest(samples->descriptors_sha256) &&
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
static int tp_retirement_campaign_run(TpRetirementCampaign* campaign,
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
        unsigned dense = invocation.dense;
        ok = dense < campaign->rows && campaign->row_ids[dense] == invocation.row &&
            campaign->metrics[dense] == samples->rows[dense].metrics;
        if (ok && invocation.kind)
        {
            unsigned cell = execution->phase ?
                (execution->pair_in_block ? execution->second_cells : execution->first_cells)[execution->cell] :
                execution->cell;
            ok = cell < campaign->runtime_count && campaign->runtime_rows[cell] == dense;
        }
        if (ok) ok = tp_retirement_campaign_command_copy(&checked, command,
            invocation.row, invocation.kind, invocation.variant);
        if (ok)
        {
            size_t index = tp_retirement_campaign_index(campaign->rows, stage, dense,
                invocation.kind, invocation.variant);
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
 * this frozen job, plan and context; that authority is supplied by #1021. */
static int tp_retirement_campaign_finish_stage(TpRetirementCampaign* campaign,
    uint64_t completed_at_ns, TpRetirementShard* final_shard)
{
    unsigned stage = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_AB ? 1 : 0;
    TpRetirementSamples* samples = campaign ? campaign->samples[stage] : NULL;
    TpRetirementTranscript* transcript = samples ? samples->transcript : NULL;
    int ok = campaign && (campaign->phase == TP_RETIREMENT_CAMPAIGN_AA ||
        campaign->phase == TP_RETIREMENT_CAMPAIGN_AB) && transcript &&
        tp_retirement_execution_complete(transcript->execution) &&
        samples->collected == campaign->capacity.invocations_per_stage &&
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
