/* In-unit #881-D campaign driver and plan/context derivation (#1022, A1).
 *
 * Ownership: lane D. The worker unit holds the attempt's record store, the
 * held binaries and the private phase channel, never the queue or the lease.
 * This header sequences the fixed campaign inside the unit, in the one order
 * the contract allows, and derives the frozen plan and its pre-sample and
 * post-sample context digests from service-authenticated inputs only. It does
 * not admit the recipe: production A/A admission has no authority (#426
 * decision plus #1021 capability) and always refuses, so production can never
 * reach A/B; only the functional fixture build compiles the stand-in.
 *
 * Entry points, in the only order the driver accepts:
 *   bq_retirement_unit_campaign_begin      SETTLING acknowledgement
 *   bq_retirement_unit_campaign_untimed    untimed production and
 *                                          reproduction batches
 *                                          (retirement_untimed.h)
 *   bq_retirement_unit_campaign_measuring  MEASURING acknowledgement; the
 *                                          store-based bind needs it
 *   bq_retirement_unit_campaign_attach     take the bound campaign, re-derive
 *                                          and compare its plan and context,
 *                                          require the planned result store
 *   bq_retirement_unit_campaign_stage      A/A (then, after freeze, A/B): the
 *                                          exact held descriptors in cursor
 *                                          order, shards, metrics, export
 *   bq_retirement_unit_campaign_admit      A/A admission (fixture only)
 *   bq_retirement_unit_campaign_freeze     A/B launch freeze
 *   bq_retirement_unit_campaign_finish     post-sample context, MEASURED
 *
 * Plan and context: bq_retirement_unit_campaign_pins reads the frozen #426
 * values (seed, pairs, resamples), the family's bootstrap member count and
 * the budget digest from the recipe profile; nothing here invents them, and
 * the blocked profile carries none, so every derivation fails closed.
 * bq_retirement_unit_campaign_plan builds the #619 plan from those pins and
 * the sealed gate (exact cells are 2R + U + 2B: wall and peak memory per timed
 * row, runtime per runtime-eligible timed row, the batch pair per object
 * group). bq_retirement_unit_campaign_plan_digest is candidate independent: it
 * binds the schedule, the pins and the #508 row population and batch layout,
 * never a subject binary, command or output. bq_retirement_unit_campaign_
 * pre_context binds that plan digest to the subjects (gate seal, preparation,
 * both binaries, ready record, budget, frozen commands, untimed record stream,
 * host/transcript identity) before any timed child; post_context chains the
 * pre-sample digest to both stages' transcript shards and numeric digests.
 *
 * Map: BqRetirementUnitCampaignPins, BqRetirementUnitCampaignFacts,
 * bq_retirement_unit_campaign_derive, BqRetirementUnitCampaignStreams,
 * BqRetirementUnitCampaign, bq_retirement_unit_campaign_live,
 * bq_retirement_unit_campaign_launch_log, bq_retirement_unit_campaign_retire,
 * bq_retirement_unit_campaign_store_planned (lane E's pre-timing store plan),
 * bq_retirement_unit_campaign_result (what lane E's composer consumes).
 *
 * Nothing calls this from bq_worker_unit; the recipe stays blocked.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_CAMPAIGN_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_CAMPAIGN_H
#include "retirement_campaign_binding.h"
#include "phase_channel.h"

/* The installed service never compiles the A/A admission stand-in. */
#if defined(BQ_SERVICE_INSTALLED) && defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
#error "the installed service must not define BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA"
#endif
#if defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA) && !defined(TP_RETIREMENT_CAMPAIGN_FIXTURE_AA_COMPILED)
#error "BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA needs retirement_campaign.h built with its fixture admission"
#endif

#ifdef __linux__
#include <poll.h>

/* Recipe-profile keys of the frozen campaign values. P and the seed are
 * #426's frozen values, resamples the #619 draw count, bootstrap members the
 * validator-derived family's aggregate/slice count; the budget pin is the
 * reviewed record's digest (retirement_budget.h). */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_SEED_KEY "campaign-seed="
#define BQ_RETIREMENT_UNIT_CAMPAIGN_PAIRS_KEY "campaign-pairs="
#define BQ_RETIREMENT_UNIT_CAMPAIGN_RESAMPLES_KEY "campaign-resamples="
#define BQ_RETIREMENT_UNIT_CAMPAIGN_BOOTSTRAP_KEY "campaign-bootstrap-members="
#define BQ_RETIREMENT_UNIT_CAMPAIGN_BUDGET_KEY "campaign-budget-sha256="
/* The versioned #619 schedule the execution plan names. */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_SCHEDULE "tp-retirement-block-schedule-v2"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_PLAN_DOMAIN "bq-retirement-unit-campaign-plan-v1"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_DOMAIN "bq-retirement-unit-campaign-pre-sample-v1"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_POST_DOMAIN "bq-retirement-unit-campaign-post-sample-v1"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_SHARDS_DOMAIN "bq-retirement-unit-campaign-shards-v1"
/* One scratch log per launch, unlinked after a successful launch and kept as
 * failure evidence otherwise. Index 0 is untimed, 1 A/A, 2 A/B. */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_LOGS {"unit-campaign-untimed.log", "unit-campaign-aa.log", "unit-campaign-ab.log"}

typedef struct BqRetirementUnitCampaignPins
{
    uint64_t seed;
    unsigned pairs, resamples, bootstrap_members;
    char budget_sha256[65];
} BqRetirementUnitCampaignPins;

/* Exactly one `key` line; value is the rest of that line. */
static inline int bq_retirement_unit_campaign_profile_value(String8 profile, char const* key, String8* value)
{
    size_t key_length = key ? strlen(key) : 0;
    u64 start = 0;
    unsigned found = 0;
    String8 match = {0};
    while (key_length && start < profile.length)
    {
        u64 end = start;
        while (end < profile.length && profile.pointer[end] != '\n') end += 1;
        if (end - start > key_length && !memcmp(profile.pointer + start, key, key_length))
        {
            found += 1;
            match = (String8){profile.pointer + start + key_length, end - start - key_length};
        }
        start = end + 1;
    }
    int ok = value && found == 1;
    if (value) *value = ok ? match : (String8){0};
    return ok;
}

/* A canonical decimal: digits only, no leading zero, no overflow. */
static inline int bq_retirement_unit_campaign_decimal(String8 text, uint64_t* value)
{
    uint64_t result = 0;
    int ok = value && text.length && text.length <= 20 && (text.length == 1 || text.pointer[0] != '0');
    for (u64 i = 0; ok && i < text.length; i += 1)
    {
        unsigned digit = (unsigned)(text.pointer[i] - '0');
        ok = text.pointer[i] >= '0' && text.pointer[i] <= '9' && result <= (UINT64_MAX - digit) / 10;
        if (ok) result = result * 10 + digit;
    }
    if (value) *value = ok ? result : 0;
    return ok;
}

static inline int bq_retirement_unit_campaign_hex(String8 text)
{
    int ok = text.length == 64;
    for (u64 i = 0; ok && i < text.length; i += 1)
        ok = (text.pointer[i] >= '0' && text.pointer[i] <= '9') || (text.pointer[i] >= 'a' && text.pointer[i] <= 'f');
    return ok;
}

/* The frozen values, each pinned by the admitted recipe profile. A missing,
 * duplicate or non-canonical pin, an odd pair count or one outside 60..254,
 * resamples outside #619's range or a family larger than its cap fails. */
static inline int bq_retirement_unit_campaign_pins(String8 profile, BqRetirementUnitCampaignPins* pins)
{
    String8 seed = {0}, pairs = {0}, resamples = {0}, bootstrap = {0}, budget = {0};
    uint64_t values[4] = {0};
    int ok = pins && bq_retirement_unit_campaign_profile_value(profile, BQ_RETIREMENT_UNIT_CAMPAIGN_SEED_KEY, &seed) &&
        bq_retirement_unit_campaign_profile_value(profile, BQ_RETIREMENT_UNIT_CAMPAIGN_PAIRS_KEY, &pairs) &&
        bq_retirement_unit_campaign_profile_value(profile, BQ_RETIREMENT_UNIT_CAMPAIGN_RESAMPLES_KEY, &resamples) &&
        bq_retirement_unit_campaign_profile_value(profile, BQ_RETIREMENT_UNIT_CAMPAIGN_BOOTSTRAP_KEY, &bootstrap) &&
        bq_retirement_unit_campaign_profile_value(profile, BQ_RETIREMENT_UNIT_CAMPAIGN_BUDGET_KEY, &budget) &&
        bq_retirement_unit_campaign_decimal(seed, &values[0]) && bq_retirement_unit_campaign_decimal(pairs, &values[1]) &&
        bq_retirement_unit_campaign_decimal(resamples, &values[2]) &&
        bq_retirement_unit_campaign_decimal(bootstrap, &values[3]) && bq_retirement_unit_campaign_hex(budget) &&
        values[0] && values[1] >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND &&
        values[1] <= TP_RETIREMENT_EXECUTION_MAX_PAIRS && !(values[1] & 1) &&
        values[2] >= TP_RETIREMENT_MIN_RESAMPLES && values[2] <= TP_RETIREMENT_MAX_RESAMPLES &&
        values[3] && values[3] <= TP_RETIREMENT_MAX_BOOTSTRAP_MEMBERS_PER_SCOPE;
    if (pins)
    {
        *pins = (BqRetirementUnitCampaignPins){0};
        if (ok)
        {
            pins->seed = values[0];
            pins->pairs = (unsigned)values[1];
            pins->resamples = (unsigned)values[2];
            pins->bootstrap_members = (unsigned)values[3];
            memcpy(pins->budget_sha256, budget.pointer, 64);
        }
    }
    return ok;
}

/* R timed rows, U runtime-eligible timed rows and B object groups, from the
 * sealed gate alone. */
static inline int bq_retirement_unit_campaign_shape(BqRetirementCorrectness const* gate, uint32_t* timed,
    uint32_t* runtime, uint32_t* groups)
{
    uint32_t rows = 0, runtimes = 0;
    uint32_t count = gate ? bq_retirement_campaign_timed_groups(gate) : UINT32_MAX;
    for (uint32_t i = 0; gate && gate->trusted_rows && gate->facts && i < gate->prepared.rows; i += 1)
        if (gate->trusted_rows[i].compiler_eligible && gate->trusted_rows[i].target == gate->prepared.native_target)
        {
            rows += 1;
            runtimes += gate->facts[i].runtime_eligible ? 1u : 0u;
        }
    int ok = gate && bq_retirement_correctness_ready(gate) && rows && count != UINT32_MAX && count <= rows;
    *timed = ok ? rows : 0;
    *runtime = ok ? runtimes : 0;
    *groups = ok ? count : 0;
    return ok;
}

/* The #619 plan: the pinned seed, pairs, resamples and bootstrap members, and
 * the exact-cell count derived from the gate (never supplied). */
static inline int bq_retirement_unit_campaign_plan(BqRetirementCorrectness const* gate,
    BqRetirementUnitCampaignPins const* pins, TpRetirementPlan* plan)
{
    uint32_t timed = 0, runtime = 0, groups = 0;
    int ok = plan && pins && pins->seed && bq_retirement_unit_campaign_shape(gate, &timed, &runtime, &groups);
    uint64_t cells = ok ? 2u * (uint64_t)timed + runtime + 2u * (uint64_t)gate->batch_group_count : 0;
    ok = ok && cells && cells <= TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE;
    if (plan)
        *plan = ok ? (TpRetirementPlan){.seed = pins->seed, .version = TP_RETIREMENT_STATISTICS_VERSION,
            .bootstrap_members_per_scope = pins->bootstrap_members, .cell_members_per_scope = (unsigned)cells,
            .pairs_per_round = pins->pairs, .resamples = pins->resamples, .frozen_before_samples = 1} :
            (TpRetirementPlan){0};
    return ok;
}

static inline void bq_retirement_unit_campaign_text(Sha256* hash, char const* key, char const* value)
{
    size_t length = value ? strlen(value) : 0;
    sha256_add(hash, key, strlen(key));
    sha256_add(hash, "=", 1);
    if (length) sha256_add(hash, value, length);
    sha256_add(hash, "\n", 1);
}

static inline void bq_retirement_unit_campaign_number(Sha256* hash, char const* key, uint64_t value)
{
    char text[24];
    int length = snprintf(text, sizeof(text), "%" PRIu64, value);
    bq_retirement_unit_campaign_text(hash, key, length > 0 ? text : "");
}

/* Candidate independent: the schedule, the pins, every #508 row's identity,
 * eligibility and reference-oracle facts, and the frozen batch layout (input
 * rows, membership and fixture paths). No binary, command, object digest or
 * candidate output enters, so the same plan digest holds for every candidate
 * against this population. */
static inline int bq_retirement_unit_campaign_plan_digest(BqRetirementCorrectness const* gate,
    TpRetirementPlan const* plan, char digest[65])
{
    uint32_t timed = 0, runtime = 0, groups = 0;
    int ok = plan && digest && plan->version == TP_RETIREMENT_STATISTICS_VERSION && plan->frozen_before_samples == 1 &&
        bq_retirement_unit_campaign_shape(gate, &timed, &runtime, &groups);
    Sha256 hash;
    sha256_init(&hash);
    if (ok)
    {
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_PLAN_DOMAIN;
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_unit_campaign_text(&hash, "schedule", BQ_RETIREMENT_UNIT_CAMPAIGN_SCHEDULE);
        bq_retirement_unit_campaign_number(&hash, "statistics", plan->version);
        bq_retirement_unit_campaign_number(&hash, "seed", plan->seed);
        bq_retirement_unit_campaign_number(&hash, "rounds", TP_RETIREMENT_ROUNDS);
        bq_retirement_unit_campaign_number(&hash, "warmups", TP_RETIREMENT_WARMUPS);
        bq_retirement_unit_campaign_number(&hash, "pairs", plan->pairs_per_round);
        bq_retirement_unit_campaign_number(&hash, "resamples", plan->resamples);
        bq_retirement_unit_campaign_number(&hash, "bootstrap-members", plan->bootstrap_members_per_scope);
        bq_retirement_unit_campaign_number(&hash, "cell-members", plan->cell_members_per_scope);
        bq_retirement_unit_campaign_number(&hash, "native-target", gate->prepared.native_target);
        bq_retirement_unit_campaign_number(&hash, "rows", gate->prepared.rows);
        bq_retirement_unit_campaign_number(&hash, "timed-rows", timed);
        bq_retirement_unit_campaign_number(&hash, "runtime-rows", runtime);
        bq_retirement_unit_campaign_number(&hash, "groups", groups);
        bq_retirement_unit_campaign_number(&hash, "object-groups", gate->batch_group_count);
        for (uint32_t i = 0; i < gate->prepared.rows; i += 1)
        {
            BqRetirementTrustedRow const* row = &gate->trusted_rows[i];
            uint64_t words[] = {row->row, row->census_row, row->target, row->stage, row->classification,
                row->compiler_eligible, row->code_obligation, row->execution_obligation, row->batch_control,
                gate->facts[i].runtime_eligible, gate->facts[i].code_eligible};
            for (u32 word = 0; word < BUSTER_ARRAY_LENGTH(words); word += 1)
                bq_retirement_unit_campaign_number(&hash, "row", words[word]);
            bq_retirement_unit_campaign_text(&hash, "identity", row->identity_sha256);
            bq_retirement_unit_campaign_text(&hash, "source", row->source_sha256);
            bq_retirement_unit_campaign_text(&hash, "configuration", row->configuration_sha256);
            bq_retirement_unit_campaign_text(&hash, "skip", row->skip_proof_sha256);
            bq_retirement_unit_campaign_text(&hash, "oracle", row->independent_oracle_sha256);
            bq_retirement_unit_campaign_text(&hash, "batch-key", row->batch_key_sha256);
        }
        for (uint32_t g = 0; g < gate->batch_group_count; g += 1)
        {
            TpRetirementBatchContract const* contract = &gate->batch_groups[g].contract[0];
            bq_retirement_unit_campaign_number(&hash, "batch", g);
            bq_retirement_unit_campaign_number(&hash, "inputs", contract->input_count);
            for (unsigned k = 0; k < contract->input_count; k += 1)
            {
                bq_retirement_unit_campaign_number(&hash, "input-row", contract->inputs[k].row);
                bq_retirement_unit_campaign_number(&hash, "input-member", contract->inputs[k].member);
                bq_retirement_unit_campaign_text(&hash, "input-fixture", contract->inputs[k].fixture);
            }
        }
        sha256_finish_hex(&hash, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

/* The pre-sample context's inputs besides the gate. commands_sha256 is the
 * positional digest of the frozen A/A and A/B commands (bq_retirement_unit_
 * campaign_commands_*); untimed is the finished untimed record stream. */
typedef struct BqRetirementUnitCampaignFacts
{
    char const* plan_sha256;
    char const* ready_sha256;
    char const* budget_sha256;
    char const* commands_sha256;
    TpRetirementShard const* untimed;
    char const* job;
    char const* boot;
    uint64_t attempt, bound_at_ns;
    int cpu;
} BqRetirementUnitCampaignFacts;

static inline void bq_retirement_unit_campaign_command_entry(Sha256* hash, char const* command_sha256,
    char const* output_sha256, char const* contract_sha256, char const* artifact, unsigned timeout_seconds,
    int exit_status)
{
    bq_retirement_unit_campaign_text(hash, "command", command_sha256);
    bq_retirement_unit_campaign_text(hash, "output", output_sha256);
    bq_retirement_unit_campaign_text(hash, "contract", contract_sha256);
    bq_retirement_unit_campaign_text(hash, "artifact", artifact);
    bq_retirement_unit_campaign_number(hash, "timeout", timeout_seconds);
    bq_retirement_unit_campaign_number(hash, "exit", (uint64_t)(uint32_t)exit_status);
}

/* The digest of the caller's commands, laid out per stage as freeze takes
 * them ([slot * 2 + variant], groups then runtime rows). */
static inline int bq_retirement_unit_campaign_commands_measured(TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands, size_t per_stage, char digest[65])
{
    Sha256 hash;
    sha256_init(&hash);
    int ok = aa_commands && ab_commands && digest && per_stage && !(per_stage & 1);
    for (unsigned stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
        for (size_t index = 0; ok && index < per_stage; ++index)
        {
            TpRetirementMeasuredCommand const* command = (stage ? ab_commands : aa_commands) + index;
            char contract[65] = {0};
            ok = tp_retirement_digest(command->command_sha256) && tp_retirement_digest(command->output_sha256) &&
                (!command->batch || tp_retirement_batch_contract_digest(command->batch, contract));
            if (ok)
                bq_retirement_unit_campaign_command_entry(&hash, command->command_sha256, command->output_sha256,
                    contract, command->batch ? NULL : command->artifact, command->timeout_seconds,
                    command->exit_status);
        }
    if (ok) sha256_finish_hex(&hash, digest);
    else if (digest) digest[0] = 0;
    return ok;
}

/* The same digest over the campaign's frozen snapshot, positionally equal to
 * the measured form for the commands freeze accepted. */
static inline int bq_retirement_unit_campaign_commands_frozen(TpRetirementCampaign const* campaign, char digest[65])
{
    size_t count = campaign ? (size_t)(campaign->groups + campaign->runtime_count) *
        TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT * TP_RETIREMENT_CAMPAIGN_STAGES : 0;
    Sha256 hash;
    sha256_init(&hash);
    int ok = campaign && campaign->commands && count && digest;
    for (size_t index = 0; ok && index < count; ++index)
    {
        TpRetirementCampaignCommand const* command = &campaign->commands[index];
        bq_retirement_unit_campaign_command_entry(&hash, command->command_sha256, command->output_sha256,
            command->contract_sha256, command->artifact, command->timeout_seconds, command->exit_status);
    }
    if (ok) sha256_finish_hex(&hash, digest);
    else if (digest) digest[0] = 0;
    return ok;
}

/* The pre-sample context: the candidate-independent plan joined to the
 * authenticated subjects, the frozen commands, the finished untimed record
 * stream and the host/transcript identity, before any timed child. */
static inline int bq_retirement_unit_campaign_pre_context(BqRetirementCorrectness const* gate,
    BqRetirementUnitCampaignFacts const* facts, char digest[65])
{
    int ok = gate && facts && digest && bq_retirement_correctness_ready(gate) &&
        tp_retirement_digest(facts->plan_sha256) && tp_retirement_digest(facts->ready_sha256) &&
        tp_retirement_digest(facts->budget_sha256) && tp_retirement_digest(facts->commands_sha256) &&
        facts->untimed && facts->untimed->records && tp_retirement_digest(facts->untimed->sha256) &&
        tp_retirement_token(facts->job) && tp_retirement_token(facts->boot) && facts->attempt &&
        facts->bound_at_ns && facts->cpu >= 0;
    Sha256 hash;
    sha256_init(&hash);
    if (ok)
    {
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_DOMAIN;
        sha256_add(&hash, domain, sizeof(domain) - 1);
        BqRetirementPrepared const* prepared = &gate->prepared;
        bq_retirement_unit_campaign_text(&hash, "plan", facts->plan_sha256);
        bq_retirement_unit_campaign_text(&hash, "ready", facts->ready_sha256);
        bq_retirement_unit_campaign_text(&hash, "gate", gate->sealed_sha256);
        bq_retirement_unit_campaign_text(&hash, "preparation", prepared->preparation_sha256);
        bq_retirement_unit_campaign_text(&hash, "source-base", prepared->source_sha256[0]);
        bq_retirement_unit_campaign_text(&hash, "source-candidate", prepared->source_sha256[1]);
        bq_retirement_unit_campaign_text(&hash, "binary-base", prepared->binary_sha256[0]);
        bq_retirement_unit_campaign_text(&hash, "binary-candidate", prepared->binary_sha256[1]);
        bq_retirement_unit_campaign_text(&hash, "budget", facts->budget_sha256);
        bq_retirement_unit_campaign_text(&hash, "commands", facts->commands_sha256);
        bq_retirement_unit_campaign_text(&hash, "untimed", facts->untimed->sha256);
        bq_retirement_unit_campaign_number(&hash, "untimed-records", facts->untimed->records);
        bq_retirement_unit_campaign_number(&hash, "untimed-bytes", facts->untimed->bytes);
        bq_retirement_unit_campaign_text(&hash, "job", facts->job);
        bq_retirement_unit_campaign_number(&hash, "attempt", facts->attempt);
        bq_retirement_unit_campaign_text(&hash, "boot", facts->boot);
        bq_retirement_unit_campaign_number(&hash, "cpu", (uint64_t)facts->cpu);
        bq_retirement_unit_campaign_number(&hash, "bound-at", facts->bound_at_ns);
        sha256_finish_hex(&hash, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

/* The plan, its digest and the pre-sample context for stages that have not
 * started, before freeze: the store-based bind derives these itself and
 * refuses any caller value that differs. */
static inline int bq_retirement_unit_campaign_derive(BqRetirementCorrectness const* gate,
    BqRetirementUnitCampaignPins const* pins, char const* ready_sha256, TpRetirementShard const* untimed,
    TpRetirementSamples const* aa, TpRetirementSamples const* ab, TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands, size_t per_stage, TpRetirementPlan* plan,
    char plan_sha256[65], char context_sha256[65])
{
    TpRetirementTranscript const* a = aa ? aa->transcript : NULL;
    TpRetirementTranscript const* b = ab ? ab->transcript : NULL;
    char commands[65] = {0};
    int ok = pins && a && b && plan_sha256 && context_sha256 && !a->total_records && !b->total_records &&
        a->attempt == b->attempt && a->bound_at_ns == b->bound_at_ns && a->cpu == b->cpu &&
        !strcmp(a->job, b->job) && !strcmp(a->boot, b->boot) &&
        bq_retirement_unit_campaign_plan(gate, pins, plan) && bq_retirement_unit_campaign_plan_digest(gate, plan, plan_sha256) &&
        bq_retirement_unit_campaign_commands_measured(aa_commands, ab_commands, per_stage, commands);
    BqRetirementUnitCampaignFacts facts = {plan_sha256, ready_sha256, pins ? pins->budget_sha256 : NULL, commands,
        untimed, a ? a->job : NULL, a ? a->boot : NULL, a ? a->attempt : 0, a ? a->bound_at_ns : 0, a ? a->cpu : -1};
    ok = ok && bq_retirement_unit_campaign_pre_context(gate, &facts, context_sha256);
    if (!ok)
    {
        if (plan) *plan = (TpRetirementPlan){0};
        if (plan_sha256) plan_sha256[0] = 0;
        if (context_sha256) context_sha256[0] = 0;
    }
    return ok;
}

/* The post-sample context chains the pre-sample digest the campaign froze to
 * both stages' transcript shard chains and numeric digests. It exists only
 * for a completely collected campaign. */
static inline int bq_retirement_unit_campaign_post_context(TpRetirementCampaign const* campaign,
    char const* pre_context, char const shard_chains[2][65], char digest[65])
{
    TpRetirementCampaignOutcome outcome = tp_retirement_campaign_outcome(campaign);
    int ok = campaign && shard_chains && digest && pre_context && tp_retirement_digest(pre_context) &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_COLLECTED &&
        outcome.execution == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE && !strcmp(campaign->context_sha256, pre_context);
    for (unsigned stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
        ok = tp_retirement_digest(shard_chains[stage]);
    Sha256 hash;
    sha256_init(&hash);
    if (ok)
    {
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_POST_DOMAIN;
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_unit_campaign_text(&hash, "pre-sample", pre_context);
        bq_retirement_unit_campaign_text(&hash, "plan", campaign->plan_sha256);
        for (unsigned stage = 0; stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
        {
            TpRetirementSamples const* samples = campaign->samples[stage];
            TpRetirementTranscript const* transcript = samples->transcript;
            bq_retirement_unit_campaign_number(&hash, "stage", stage);
            bq_retirement_unit_campaign_text(&hash, "transcript-shards", shard_chains[stage]);
            bq_retirement_unit_campaign_number(&hash, "invocations", transcript->total_records);
            bq_retirement_unit_campaign_number(&hash, "completed-at", transcript->completed_at_ns);
            bq_retirement_unit_campaign_text(&hash, "raw", samples->raw_sha256);
            bq_retirement_unit_campaign_text(&hash, "row-shards", samples->descriptors_sha256[0]);
            bq_retirement_unit_campaign_text(&hash, "batch-shards", samples->descriptors_sha256[1]);
            bq_retirement_unit_campaign_number(&hash, "samples", samples->exported);
            bq_retirement_unit_campaign_number(&hash, "metrics-artifacts", samples->metrics ? samples->metrics->artifacts : 0);
            bq_retirement_unit_campaign_number(&hash, "metrics-bytes", samples->metrics ? samples->metrics->total_bytes : 0);
        }
        sha256_finish_hex(&hash, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

typedef enum BqRetirementUnitCampaignStep
{
    BQ_RETIREMENT_UNIT_CAMPAIGN_NEW,
    BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING,
    BQ_RETIREMENT_UNIT_CAMPAIGN_UNTIMED,
    BQ_RETIREMENT_UNIT_CAMPAIGN_MEASURING,
    BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND,
    BQ_RETIREMENT_UNIT_CAMPAIGN_AA,
    BQ_RETIREMENT_UNIT_CAMPAIGN_ADMITTED,
    BQ_RETIREMENT_UNIT_CAMPAIGN_FROZEN,
    BQ_RETIREMENT_UNIT_CAMPAIGN_AB,
    BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED,
    BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED
} BqRetirementUnitCampaignStep;

/* Service-created, empty streams one stage consumes, in order: spare
 * transcript shards for rotation, spare metrics shards, and one stream per
 * numeric sample shard (capacity.sample_shards_per_stage). The caller owns
 * every stream and its durable no-replace publication (#1023). */
typedef struct BqRetirementUnitCampaignStreams
{
    FILE* const* transcripts;
    FILE* const* metrics;
    FILE* const* samples;
    unsigned transcript_count, metrics_count, sample_count;
} BqRetirementUnitCampaignStreams;

/* Zero-initialize. The phase channel, the untimed runner, the held binaries
 * and the binding are borrowed and must outlive the driver. */
typedef struct BqRetirementUnitCampaign
{
    BqPhaseChannel* phases;
    BqRetirementCampaignBinding const* binding;
    TpRetirementUntimed* untimed;
    TpRetirementExecutable untimed_executables[2];
    TpRetirementPlan plan;
    Sha256 shard_hash[TP_RETIREMENT_CAMPAIGN_STAGES];
    TpRetirementShard untimed_records;
    TpRetirementShardFile untimed_metrics;
    char held_sha256[2][65], held_identity_sha256[2][65];
    char plan_sha256[65], context_sha256[65], post_context_sha256[65];
    char shard_chain_sha256[TP_RETIREMENT_CAMPAIGN_STAGES][65];
    uint64_t deadline_ns, launches[3];
    unsigned step, transcript_shards[TP_RETIREMENT_CAMPAIGN_STAGES], metrics_shards[3];
    int cancellation_fd, work_directory, log_directory;
} BqRetirementUnitCampaign;

/* Not cancelled (the SIGTERM self-pipe is not readable and not hung up), the
 * absolute deadline not reached, and the channel intact. */
static inline int bq_retirement_unit_campaign_live(BqPhaseChannel const* phases, int cancellation_fd,
    uint64_t deadline_ns)
{
    struct pollfd wait = {.fd = cancellation_fd, .events = POLLIN};
    int ready = cancellation_fd >= 3 ? poll(&wait, 1, 0) : -1;
    uint64_t now = bq_phase_clock();
    int ok = phases && !phases->failed && phases->descriptor >= 3 && ready == 0 && now && now < deadline_ns;
    return ok;
}

static inline void bq_retirement_unit_campaign_fail(BqRetirementUnitCampaign* driver)
{
    if (driver)
    {
        if (driver->binding && driver->binding->campaign) tp_retirement_campaign_poison(driver->binding->campaign);
        if (driver->untimed && !driver->untimed->finished)
        {
            driver->untimed->failed = 1;
            if (driver->untimed->metrics) driver->untimed->metrics->failed = 1;
        }
        driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED;
    }
}

/* A fresh, empty read-write log for one launch. */
static inline int bq_retirement_unit_campaign_launch_log(BqRetirementUnitCampaign const* driver, unsigned index)
{
    static char const* const names[] = BQ_RETIREMENT_UNIT_CAMPAIGN_LOGS;
    int log = driver && index < BUSTER_ARRAY_LENGTH(names) ?
        openat(driver->log_directory, names[index], O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    return log;
}

/* Retire a successful launch's scratch outputs and its log; a failed launch
 * keeps both as evidence. */
static inline int bq_retirement_unit_campaign_retire(BqRetirementUnitCampaign const* driver,
    TpRetirementMeasuredCommand const* command, unsigned index)
{
    static char const* const names[] = BQ_RETIREMENT_UNIT_CAMPAIGN_LOGS;
    int ok = index < BUSTER_ARRAY_LENGTH(names) && unlinkat(driver->log_directory, names[index], 0) == 0;
    if (command->batch)
    {
        ok = unlinkat(driver->work_directory, command->batch->metrics, 0) == 0 && ok;
        for (unsigned i = 0; i < command->batch->input_count; ++i)
            if (command->batch->inputs[i].artifact)
                ok = unlinkat(driver->work_directory, command->batch->inputs[i].artifact, 0) == 0 && ok;
    }
    else if (!command->kind) ok = unlinkat(driver->work_directory, command->artifact, 0) == 0 && ok;
    return ok;
}

/* Take a rotated metrics shard and hand the writer its next spare before a
 * launch that may rotate. */
static inline int bq_retirement_unit_campaign_metrics_ready(BqRetirementUnitCampaign* driver,
    TpRetirementMetricsShards* metrics, FILE* const* spares, unsigned spare_count, unsigned index)
{
    int ok = 1;
    if (metrics && metrics->completed_ready)
    {
        TpRetirementShardFile completed;
        ok = tp_retirement_metrics_shards_take(metrics, &completed);
        if (ok) driver->metrics_shards[index] += 1;
    }
    unsigned used = driver->metrics_shards[index];
    if (ok && metrics && !metrics->spare && used < spare_count)
        ok = tp_retirement_metrics_shards_spare(metrics, spares[used]);
    return ok;
}

/* SETTLING: the unit's build already holds the PREPARING acknowledgement. */
static inline int bq_retirement_unit_campaign_begin(BqRetirementUnitCampaign* driver, BqPhaseChannel* phases,
    int cancellation_fd, uint64_t deadline_ns, int work_directory, int log_directory)
{
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_NEW && !driver->phases && phases &&
        phases->sequence == BQ_PHASE_PREPARING && work_directory >= 3 && log_directory >= 3 &&
        bq_retirement_unit_campaign_live(phases, cancellation_fd, deadline_ns);
    if (ok)
    {
        driver->phases = phases;
        driver->cancellation_fd = cancellation_fd;
        driver->deadline_ns = deadline_ns;
        driver->work_directory = work_directory;
        driver->log_directory = log_directory;
        ok = bq_phase_exchange_until(phases, BQ_PHASE_SETTLING, deadline_ns);
    }
    if (driver) driver->step = ok ? BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING : BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED;
    return ok;
}

/* The held pair as launchable executables: distinct descriptors whose
 * identity and digest are the verified record's. */
static inline int bq_retirement_unit_campaign_executables(BqRetirementHeldBinaries const* held,
    TpRetirementExecutable executables[2])
{
    int ok = held && held->owned == 1 && held->descriptors[0] >= 3 && held->descriptors[1] >= 3 &&
        held->descriptors[0] != held->descriptors[1] &&
        memcmp(held->verified.binary_identity_sha256[0], held->verified.binary_identity_sha256[1], 65);
    for (unsigned side = 0; ok && side < 2; ++side)
    {
        char identity[65] = {0};
        ok = bq_retirement_campaign_descriptor_identity(held->descriptors[side], identity) &&
            !strcmp(identity, held->verified.binary_identity_sha256[side]) &&
            tp_retirement_executable_init(&executables[side], held->descriptors[side],
                held->verified.binary_sha256[side]);
    }
    return ok;
}

/* Untimed code-artifact batches, outside timing and before MEASURING: each
 * batch in (group, variant, purpose) order on the held binary of its
 * variant, its group shape the reviewed one. The runner rejects an order or
 * window violation and finish requires every reproduction. */
static inline int bq_retirement_unit_campaign_untimed(BqRetirementUnitCampaign* driver, TpRetirementUntimed* untimed,
    TpRetirementUntimedBatch const* batches, unsigned count, BqRetirementHeldBinaries const* held,
    TpRetirementCampaignReview const* review, BqRetirementUnitCampaignStreams const* streams)
{
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING && untimed && !untimed->failed &&
        !untimed->finished && !untimed->records && batches && review && streams &&
        review->untimed_groups == untimed->groups && count >= 2u * untimed->groups &&
        count <= 4u * untimed->groups && review->untimed_inputs && review->untimed_kinds &&
        bq_retirement_unit_campaign_executables(held, driver->untimed_executables);
    if (driver) driver->untimed = untimed;
    for (unsigned side = 0; ok && side < 2; ++side)
    {
        memcpy(driver->held_sha256[side], held->verified.binary_sha256[side], 65);
        memcpy(driver->held_identity_sha256[side], held->verified.binary_identity_sha256[side], 65);
    }
    for (unsigned i = 0; ok && i < count; ++i)
    {
        TpRetirementUntimedBatch const* batch = &batches[i];
        unsigned inputs_count = batch->command.batch ? batch->command.batch->input_count : 1;
        ok = batch->group < untimed->groups && batch->variant < 2 &&
            batch->group_kind == review->untimed_kinds[batch->group] &&
            inputs_count == review->untimed_inputs[batch->group] &&
            bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns) &&
            bq_retirement_unit_campaign_metrics_ready(driver, untimed->metrics, streams->metrics,
                streams->metrics_count, 0);
        int log = ok ? bq_retirement_unit_campaign_launch_log(driver, 0) : -1;
        TpProcessInputs inputs = {driver->untimed_executables[batch->variant].descriptor, driver->work_directory, log,
            batch->command.environment};
        TpRetirementMeasurementResult result;
        ok = ok && log >= 3 && tp_retirement_untimed_run(untimed, batch, &driver->untimed_executables[batch->variant],
            &inputs, driver->work_directory, &result);
        if (log >= 0 && close(log) != 0) ok = 0;
        if (ok) ok = bq_retirement_unit_campaign_retire(driver, &batch->command, 0);
        if (ok) driver->launches[0] += 1;
    }
    ok = ok && tp_retirement_untimed_finish(untimed, &driver->untimed_records);
    if (ok && untimed->metrics)
    {
        ok = bq_retirement_unit_campaign_metrics_ready(driver, untimed->metrics, NULL, 0, 0) &&
            tp_retirement_metrics_shards_finish(untimed->metrics, &driver->untimed_metrics);
        if (ok) driver->metrics_shards[0] += 1;
    }
    if (ok) driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_UNTIMED;
    else bq_retirement_unit_campaign_fail(driver);
    return ok;
}

/* MEASURING, only after every untimed batch sealed its record. */
static inline int bq_retirement_unit_campaign_measuring(BqRetirementUnitCampaign* driver)
{
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_UNTIMED && driver->untimed &&
        driver->untimed->finished &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns) &&
        bq_phase_exchange_until(driver->phases, BQ_PHASE_MEASURING, driver->deadline_ns);
    if (ok) driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_MEASURING;
    else bq_retirement_unit_campaign_fail(driver);
    return ok;
}

/* The result store must already be planned for this campaign before its
 * first timed child: lane E's tp_retirement_compose_plan reserves both
 * stages' payload (the frozen capacity) plus at least the execution receipt
 * through tp_retirement_campaign_store_preflight, within the store ceilings. */
static inline int bq_retirement_unit_campaign_store_planned(TpRetirementCampaign const* campaign,
    TpRetirementCampaignStorePlan const* plan)
{
    uint64_t payload = campaign ? campaign->capacity.total_payload_bytes_upper_bound : 0;
    int ok = campaign && plan && campaign->capacity.total_payload_files &&
        plan->owned_files > campaign->capacity.total_payload_files &&
        payload <= UINT64_MAX - TP_RETIREMENT_RECEIPT_BYTES && plan->owned_bytes >= payload + TP_RETIREMENT_RECEIPT_BYTES &&
        plan->external_entries >= TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES &&
        plan->owned_files <= TP_RETIREMENT_STORE_FILES - plan->external_entries &&
        plan->entries == plan->owned_files + plan->external_entries &&
        plan->owned_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES && plan->external_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES &&
        plan->bytes == plan->owned_bytes + plan->external_bytes && plan->bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES &&
        plan->remaining_entries == TP_RETIREMENT_STORE_FILES - plan->entries &&
        plan->remaining_bytes == TP_RETIREMENT_STORE_TOTAL_BYTES - plan->bytes;
    return ok;
}

/* Take the campaign the store-based bind froze under this MEASURING
 * acknowledgement. Its held binaries must be the ones the untimed batches
 * ran, every untimed batch must have finished before its pre-sample binding,
 * its plan, plan digest and context must equal the ones re-derived here from
 * the pins, the ready record and the frozen snapshot, and the result store
 * must be planned for its frozen capacity. */
static inline int bq_retirement_unit_campaign_attach(BqRetirementUnitCampaign* driver,
    BqRetirementCampaignBinding const* binding, BqRetirementUnitCampaignPins const* pins, char const* ready_sha256,
    TpRetirementCampaignStorePlan const* store_plan)
{
    TpRetirementCampaign const* campaign = binding ? binding->campaign : NULL;
    BqRetirementHeldBinaries const* held = binding ? binding->held_binaries : NULL;
    TpRetirementPlan plan = {0};
    char plan_sha256[65] = {0}, commands[65] = {0}, context[65] = {0};
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_MEASURING && campaign && held && pins &&
        driver->phases->sequence == BQ_PHASE_MEASURING && binding->job_id == driver->phases->job &&
        binding->attempt_token == driver->phases->attempt && campaign->phase == TP_RETIREMENT_CAMPAIGN_AA &&
        bq_retirement_campaign_held_matches(binding) && !campaign->samples[0]->transcript->execution->sequence &&
        driver->untimed->last_end < campaign->bound_at_ns &&
        bq_retirement_unit_campaign_store_planned(campaign, store_plan) &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns);
    for (unsigned side = 0; ok && side < 2; ++side)
        ok = !strcmp(held->verified.binary_sha256[side], driver->held_sha256[side]) &&
            !strcmp(held->verified.binary_identity_sha256[side], driver->held_identity_sha256[side]);
    ok = ok && bq_retirement_unit_campaign_plan(binding->gate, pins, &plan) &&
        !memcmp(&plan, &campaign->plan, sizeof(plan)) &&
        bq_retirement_unit_campaign_plan_digest(binding->gate, &plan, plan_sha256) &&
        !strcmp(plan_sha256, campaign->plan_sha256) && !strcmp(pins->budget_sha256, campaign->budget_sha256) &&
        bq_retirement_unit_campaign_commands_frozen(campaign, commands);
    BqRetirementUnitCampaignFacts facts = {plan_sha256, ready_sha256, pins ? pins->budget_sha256 : NULL, commands,
        driver ? &driver->untimed_records : NULL, campaign ? campaign->job : NULL, campaign ? campaign->boot : NULL,
        campaign ? campaign->attempt : 0, campaign ? campaign->bound_at_ns : 0, campaign ? campaign->cpu : -1};
    ok = ok && bq_retirement_unit_campaign_pre_context(binding->gate, &facts, context) &&
        !strcmp(context, campaign->context_sha256);
    if (ok)
    {
        driver->binding = binding;
        driver->plan = plan;
        memcpy(driver->plan_sha256, plan_sha256, 65);
        memcpy(driver->context_sha256, context, 65);
        for (unsigned stage = 0; stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
        {
            sha256_init(&driver->shard_hash[stage]);
            static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_SHARDS_DOMAIN;
            sha256_add(&driver->shard_hash[stage], domain, sizeof(domain) - 1);
        }
        driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND;
    }
    else
    {
        if (driver && binding && !driver->binding) tp_retirement_campaign_poison(binding->campaign);
        bq_retirement_unit_campaign_fail(driver);
    }
    return ok;
}

static inline void bq_retirement_unit_campaign_chain(BqRetirementUnitCampaign* driver, unsigned stage,
    TpRetirementShard const* shard)
{
    bq_retirement_unit_campaign_number(&driver->shard_hash[stage], "records", shard->records);
    bq_retirement_unit_campaign_number(&driver->shard_hash[stage], "bytes", shard->bytes);
    bq_retirement_unit_campaign_text(&driver->shard_hash[stage], "sha256", shard->sha256);
    driver->transcript_shards[stage] += 1;
}

/* One whole stage: A/A after attach, A/B after freeze. Every cursor item runs
 * its frozen command on the held descriptor of its stage and variant (the
 * baseline for both A/A labels) through bq_retirement_campaign_run, which
 * rechecks the sealed gate and held join before each launch. Then the metrics
 * writer, the transcript and the numeric export finish, and the stage must be
 * ready. There is no retry: any failure poisons the attempt. */
static inline int bq_retirement_unit_campaign_stage(BqRetirementUnitCampaign* driver,
    TpRetirementMeasuredCommand const* commands, size_t command_count, BqRetirementUnitCampaignStreams const* streams)
{
    unsigned stage = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_FROZEN ? 1 : 0;
    BqRetirementCampaignBinding const* binding = driver ? driver->binding : NULL;
    TpRetirementCampaign* campaign = binding ? binding->campaign : NULL;
    TpRetirementSamples* samples = campaign ? campaign->samples[stage] : NULL;
    TpRetirementExecution* execution = samples ? samples->transcript->execution : NULL;
    int ok = driver && campaign && commands && streams &&
        (driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND || driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_FROZEN) &&
        campaign->phase == (stage ? TP_RETIREMENT_CAMPAIGN_AB : TP_RETIREMENT_CAMPAIGN_AA) &&
        command_count == (size_t)(campaign->groups + campaign->runtime_count) * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT &&
        streams->sample_count >= campaign->capacity.sample_shards_per_stage;
    TpRetirementInvocation invocation;
    while (ok && tp_retirement_execution_peek(execution, &invocation) == TP_RETIREMENT_NEXT_READY)
    {
        ok = bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns) &&
            bq_retirement_unit_campaign_metrics_ready(driver, samples->metrics, streams->metrics, streams->metrics_count,
                1 + stage);
        if (ok && samples->transcript->records == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS)
        {
            unsigned used = driver->transcript_shards[stage];
            TpRetirementShard completed;
            ok = used < streams->transcript_count &&
                tp_retirement_campaign_rotate(campaign, streams->transcripts[used], &completed);
            if (ok) bq_retirement_unit_campaign_chain(driver, stage, &completed);
        }
        size_t index = ((size_t)(invocation.kind ? campaign->groups + invocation.dense : invocation.group)) *
            TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + invocation.variant;
        TpRetirementMeasuredCommand const* command = index < command_count ? &commands[index] : NULL;
        ok = ok && command;
        int log = ok ? bq_retirement_unit_campaign_launch_log(driver, 1 + stage) : -1;
        TpProcessInputs inputs = {binding->held_executables[stage && invocation.variant].descriptor,
            driver->work_directory, log, command ? command->environment : NULL};
        TpRetirementMeasurementResult result;
        ok = ok && log >= 3 && bq_retirement_campaign_run(binding, command, &inputs, driver->work_directory, &result);
        if (log >= 0 && close(log) != 0) ok = 0;
        if (ok) ok = bq_retirement_unit_campaign_retire(driver, command, 1 + stage);
        if (ok) driver->launches[1 + stage] += 1;
    }
    ok = ok && tp_retirement_execution_complete(execution);
    if (ok && samples->metrics)
    {
        TpRetirementShardFile last;
        ok = bq_retirement_unit_campaign_metrics_ready(driver, samples->metrics, NULL, 0, 1 + stage) &&
            tp_retirement_metrics_shards_finish(samples->metrics, &last);
        if (ok) driver->metrics_shards[1 + stage] += 1;
    }
    TpRetirementShard final = {0};
    uint64_t now = tp_process_monotonic_ns();
    ok = ok && tp_retirement_campaign_finish_stage(campaign, now, &final);
    if (ok) bq_retirement_unit_campaign_chain(driver, stage, &final);
    for (uint64_t shard = 0; ok && shard < campaign->capacity.sample_shards_per_stage; ++shard)
    {
        TpRetirementShard written;
        ok = tp_retirement_samples_write_shard(samples, streams->samples[shard], &written);
    }
    ok = ok && tp_retirement_samples_finish(samples) && tp_retirement_campaign_stage_ready(campaign, stage);
    if (ok)
    {
        Sha256 copy = driver->shard_hash[stage];
        sha256_finish_hex(&copy, driver->shard_chain_sha256[stage]);
        driver->step = stage ? BQ_RETIREMENT_UNIT_CAMPAIGN_AB : BQ_RETIREMENT_UNIT_CAMPAIGN_AA;
    }
    else bq_retirement_unit_campaign_fail(driver);
    return ok;
}

/* The A/A admission the service must present: the #426 decision bound to
 * this job's frozen plan and pre-sample context, delivered as #1021's one-use
 * capability. */
typedef struct BqRetirementUnitCampaignAdmission
{
    char const* plan_sha256;
    char const* context_sha256;
    char const* receipt_sha256;
    int admitted;
} BqRetirementUnitCampaignAdmission;

/* Production has no admission authority, so it refuses and leaves the A/A
 * attempt awaiting one (A/B stays unreachable). The functional fixture build
 * enters A/B through the campaign's fixture stand-in, which poisons the
 * attempt on a denied, stale or mismatched admission. */
static inline int bq_retirement_unit_campaign_admit(BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignAdmission const* admission)
{
    TpRetirementCampaign* campaign = driver && driver->binding ? driver->binding->campaign : NULL;
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_AA && campaign && admission &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns);
#if defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
    ok = ok && admission->plan_sha256 && admission->context_sha256 &&
        !strcmp(admission->plan_sha256, driver->plan_sha256) &&
        !strcmp(admission->context_sha256, driver->context_sha256) &&
        tp_retirement_campaign_admit_aa_fixture(campaign, admission->admitted, admission->plan_sha256,
            admission->context_sha256, admission->receipt_sha256);
    if (ok) driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_ADMITTED;
    else bq_retirement_unit_campaign_fail(driver);
#else
    /* #426 has no approved empirical A/A decision and #1021 no launch
     * capability: nothing can authorize A/B. */
    ok = 0;
#endif
    return ok;
}

/* The A/B launch freeze, after admission and before the first candidate
 * child: the held join, the sealed gate, the frozen plan and context, the
 * finished A/A evidence and an untouched A/B stage are all rechecked. */
static inline int bq_retirement_unit_campaign_freeze(BqRetirementUnitCampaign* driver)
{
    BqRetirementCampaignBinding const* binding = driver ? driver->binding : NULL;
    TpRetirementCampaign* campaign = binding ? binding->campaign : NULL;
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_ADMITTED && campaign &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_AB && bq_retirement_campaign_held_matches(binding) &&
        bq_retirement_correctness_ready(binding->gate) &&
        !memcmp(binding->sealed_sha256, binding->gate->sealed_sha256, sizeof(binding->sealed_sha256)) &&
        !memcmp(&driver->plan, &campaign->plan, sizeof(driver->plan)) &&
        !strcmp(driver->plan_sha256, campaign->plan_sha256) && !strcmp(driver->context_sha256, campaign->context_sha256) &&
        tp_retirement_campaign_stage_ready(campaign, 0) && !campaign->samples[1]->collected &&
        !campaign->samples[1]->transcript->execution->sequence &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns);
    if (ok) driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_FROZEN;
    else bq_retirement_unit_campaign_fail(driver);
    return ok;
}

/* The post-sample context, then MEASURED. A failed or incomplete campaign
 * never sends MEASURED, which the supervisor requires for success. */
static inline int bq_retirement_unit_campaign_finish(BqRetirementUnitCampaign* driver)
{
    TpRetirementCampaign const* campaign = driver && driver->binding ? driver->binding->campaign : NULL;
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_AB && campaign &&
        bq_retirement_unit_campaign_post_context(campaign, driver->context_sha256,
            (char const (*)[65])driver->shard_chain_sha256, driver->post_context_sha256) &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns) &&
        bq_phase_exchange_until(driver->phases, BQ_PHASE_MEASURED, driver->deadline_ns);
    if (ok) driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED;
    else
    {
        if (driver) driver->post_context_sha256[0] = 0;
        bq_retirement_unit_campaign_fail(driver);
    }
    return ok;
}

/* What lane E's composer takes from a finished driver: the campaign's job,
 * attempt and boot, its pre-sample binding and A/B completion times, the
 * frozen #619 plan, and D's plan and context digests. The composer writes the
 * post-sample execution receipt itself; the driver writes no receipt. */
typedef struct BqRetirementUnitCampaignResult
{
    TpRetirementPlan plan;
    char const* job;
    char const* boot;
    uint64_t attempt, bound_at_ns, completed_at_ns;
    char plan_sha256[65], context_sha256[65], post_context_sha256[65];
} BqRetirementUnitCampaignResult;

static inline int bq_retirement_unit_campaign_result(BqRetirementUnitCampaign const* driver,
    BqRetirementUnitCampaignResult* result)
{
    TpRetirementCampaign const* campaign = driver && driver->binding ? driver->binding->campaign : NULL;
    int ok = driver && result && campaign && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_COLLECTED && campaign->samples[1]->transcript->finished;
    if (result)
    {
        *result = (BqRetirementUnitCampaignResult){0};
        if (ok)
        {
            result->plan = campaign->plan;
            result->job = campaign->job;
            result->boot = campaign->boot;
            result->attempt = campaign->attempt;
            result->bound_at_ns = campaign->bound_at_ns;
            result->completed_at_ns = campaign->samples[1]->transcript->completed_at_ns;
            memcpy(result->plan_sha256, driver->plan_sha256, 65);
            memcpy(result->context_sha256, driver->context_sha256, 65);
            memcpy(result->post_context_sha256, driver->post_context_sha256, 65);
        }
    }
    return ok;
}
#endif
#endif
