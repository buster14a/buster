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
 *   bq_retirement_unit_campaign_begin      SETTLING acknowledgement; the heavy
 *                                          store-based import runs next
 *   bq_retirement_unit_campaign_untimed    untimed production and
 *                                          reproduction batches
 *                                          (retirement_untimed.h) on the
 *                                          imported held pair; each untimed
 *                                          row's code facts are observed
 *                                          before any output is retired
 *   bq_retirement_unit_campaign_measuring  MEASURING acknowledgement; the
 *                                          cheap store-based bind needs it
 *   bq_retirement_unit_campaign_attach     take the store-bound campaign for
 *                                          its ready record, re-derive and
 *                                          compare its plan and context, and
 *                                          require lane E's family counts and
 *                                          result-store plan
 *   bq_retirement_unit_campaign_documents  (retirement_unit_documents.h) the
 *                                          oracle, execution, result-input and
 *                                          pre-sample documents, before A/A
 *   bq_retirement_unit_campaign_stage      A/A (then, after freeze, A/B): the
 *                                          exact held descriptors in cursor
 *                                          order, shards, metrics, export;
 *                                          after A/A the post-A/A binding
 *   bq_retirement_unit_campaign_admit      A/A admission (fixture only)
 *   bq_retirement_unit_campaign_post_aa_document  (retirement_unit_documents.h)
 *                                          the post-A/A binding over the
 *                                          admission receipt
 *   bq_retirement_unit_campaign_freeze     A/B launch freeze
 *   bq_retirement_unit_campaign_ready      post-sample context and record; the
 *                                          result is ready for lane E
 *   bq_retirement_unit_campaign_measured   MEASURED, after the caller confirms
 *                                          composition and authority handoff
 *
 * Every launch runs in lane B's canonical child layout and sandbox
 * (bq_retirement_unit_campaign_inputs; retirement_sandbox.h): the side's held
 * binary at 3 or 4, A's roots (the imported record's sources) at 5 and 6 and
 * the work directory at 7 and as cwd, a new ruleset per launch built before
 * the fork, and the child enters the sandbox before the timer starts
 * (tp_process_observe_inputs). A runtime launch runs lane B's `./{{output}}`
 * command in a fresh step directory where its row's frozen compile command
 * first reproduced the program untimed, executing that observed file
 * (bq_retirement_unit_campaign_program, TpProcessInputs.program), as lane B
 * runs a row's runtime step after its compile. A launch the sandbox refuses is recorded as
 * REFUSED: a ruleset the kernel cannot build before any child starts, and a
 * child that could not be set up, placed or sandboxed with the errno it
 * reported (launched, launch_error).
 * Every launch is refused when it could outlive the absolute deadline, and
 * its child is killed when the cancellation descriptor becomes readable
 * (TpProcessInputs.cancellation). The first failure is retained with its
 * coordinates and process facts (bq_retirement_unit_campaign_failure); each
 * successful launch's log joins a per-stage chain before it is retired.
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
 * bq_retirement_unit_campaign_derive, bq_retirement_unit_campaign_post_aa,
 * BqRetirementCampaignReady, BqRetirementUnitCampaignFailure,
 * BqRetirementUnitCampaignStreams, BqRetirementUnitCampaignCode,
 * BqRetirementUnitCampaign, bq_retirement_unit_campaign_live,
 * bq_retirement_unit_campaign_launchable, bq_retirement_unit_campaign_code_step,
 * bq_retirement_unit_campaign_log_chain, bq_retirement_unit_campaign_retire,
 * bq_retirement_unit_campaign_program (a runtime launch's program step),
 * bq_retirement_unit_campaign_store_planned (lane E's pre-timing store plan),
 * bq_retirement_unit_campaign_result (what lanes E and F consume).
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
#define BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA_DOMAIN "bq-retirement-unit-campaign-post-aa-v1"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_LOGS_DOMAIN "bq-retirement-unit-campaign-logs-v1"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_MEASURED_DOMAIN "bq-retirement-unit-campaign-measured-v1"
/* Every launch writes one scratch log, `<prefix>0000<suffix>` in the log
 * directory: its digest joins its stage's chain and it is unlinked after a
 * successful launch. A failed launch keeps it as evidence and the driver
 * stops, so an attempt retains at most one log (lane E declares the group
 * with these values): its full digest and size are in the failure record and
 * it is truncated to the byte cap, a storage bound chosen here, not a
 * measurement value. */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_PREFIX "unit-campaign-log-"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_SUFFIX ".log"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_LOG BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_PREFIX "0000" BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_SUFFIX
#define BQ_RETIREMENT_UNIT_CAMPAIGN_LOGS_MAX 1u
#define BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX (UINT64_C(1) << 20)
/* The validator's workflow documents the unit writes
 * (retirement_unit_documents.h), in the order it writes them, and their
 * leaves in the evidence root: the oracle records, the plan-v3 execution
 * plan, the result-input plan and the pre-sample plan before any timed
 * child; the post-A/A binding after the A/A admission (it binds the
 * admission receipt). */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS 5u
enum
{
    BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN,
    BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN,
    BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA
};
static char const* const bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS] = {
    "retirement-oracle.json", "retirement-execution-plan.json", "retirement-result-input-plan.json",
    "retirement-pre-sample-plan.json", "retirement-post-aa-binding.json"};
/* #615's three-partition bound across both result populations. */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_PARTITIONS 3u
/* The timed-row layout digest (bq_retirement_unit_campaign_timed_line). */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_TIMED_DOMAIN "bq-retirement-unit-campaign-timed-rows-v1"
/* READY writes the post-sample record: the campaign's identity, its
 * pre-sample, post-A/A and post-sample digests, the documents it bound and
 * all three launch-log chains, as `key=value` lines under the header. The
 * service publishes it as a retained store file at this path (lane E's
 * retained manifest, which the producer authority binds, seals its digest),
 * and MEASURED's measured digest covers it. The byte cap is a storage bound. */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD "unit-campaign-post-sample.txt"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_HEADER "BQ-RETIREMENT-UNIT-POST-SAMPLE-V1\n"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX 4096u
/* The #437 A/A admission receipt (the validator's AA_SCHEMA) and its cap. */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA "buster-native-retirement-aa-admission-v1"
#define BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX 4096u

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

/* Lane E's composer reads an object group's member metrics in its batches'
 * input order: in both frozen contracts the members are the first inputs, in
 * ascending row (census) order. */
static inline int bq_retirement_unit_campaign_members_first(BqRetirementCorrectness const* gate)
{
    int ok = gate && (!gate->batch_group_count || gate->batch_groups);
    for (uint32_t g = 0; ok && g < gate->batch_group_count; g += 1)
        for (unsigned side = 0; ok && side < 2; ++side)
        {
            TpRetirementBatchContract const* contract = &gate->batch_groups[g].contract[side];
            unsigned members = 0;
            for (unsigned k = 0; ok && k < contract->input_count; ++k)
                if (contract->inputs[k].member)
                {
                    ok = members == k && (!k || contract->inputs[k].row > contract->inputs[k - 1].row);
                    members += 1;
                }
            ok = ok && members;
        }
    return ok;
}

/* The #619 plan: the pinned seed, pairs, resamples and bootstrap members, and
 * the exact-cell count derived from the gate (never supplied). As the
 * validator's _derive_statistical_family requires, every variable metric has
 * cells: runtime needs a runtime-eligible timed row (U > 0) and the batch
 * metric pair an object batch group (B > 0). Every object group keeps its
 * members first (bq_retirement_unit_campaign_members_first). */
static inline int bq_retirement_unit_campaign_plan(BqRetirementCorrectness const* gate,
    BqRetirementUnitCampaignPins const* pins, TpRetirementPlan* plan)
{
    uint32_t timed = 0, runtime = 0, groups = 0;
    int ok = plan && pins && pins->seed && bq_retirement_unit_campaign_shape(gate, &timed, &runtime, &groups);
    uint64_t cells = ok ? 2u * (uint64_t)timed + runtime + 2u * (uint64_t)gate->batch_group_count : 0;
    ok = ok && runtime && gate->batch_group_count && bq_retirement_unit_campaign_members_first(gate) &&
        cells <= TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE;
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

/* One timed row of the layout digest (BQ_RETIREMENT_UNIT_CAMPAIGN_TIMED_DOMAIN,
 * then each row in ascending id order, then the row count): its id, campaign
 * group, runtime flag and six dimension values in STATISTICAL_DIMENSIONS
 * order, each value length-prefixed. The documents step forms it from the
 * pinned rows; the lane E handoff must reproduce it from the caller's rows. */
static inline void bq_retirement_unit_campaign_timed_line(Sha256* hash, unsigned id, unsigned group, unsigned runtime,
    char const* const values[6], size_t const lengths[6])
{
    bq_retirement_unit_campaign_number(hash, "row", id);
    bq_retirement_unit_campaign_number(hash, "group", group);
    bq_retirement_unit_campaign_number(hash, "runtime", runtime);
    for (unsigned dimension = 0; dimension < 6; ++dimension)
    {
        bq_retirement_unit_campaign_number(hash, "dimension-bytes", lengths[dimension]);
        sha256_add(hash, values[dimension], (u64)lengths[dimension]);
        sha256_add(hash, "\n", 1);
    }
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
 * both stages' transcript shard chains and numeric digests, and to the three
 * launch-log chains (0 untimed, 1 A/A, 2 A/B). It exists only for a
 * completely collected campaign. */
static inline int bq_retirement_unit_campaign_post_context(TpRetirementCampaign const* campaign,
    char const* pre_context, char const shard_chains[2][65], char const log_chains[3][65], char digest[65])
{
    TpRetirementCampaignOutcome outcome = tp_retirement_campaign_outcome(campaign);
    int ok = campaign && shard_chains && log_chains && digest && pre_context && tp_retirement_digest(pre_context) &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_COLLECTED &&
        outcome.execution == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE && !strcmp(campaign->context_sha256, pre_context);
    for (unsigned stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; ++stage)
        ok = tp_retirement_digest(shard_chains[stage]);
    for (unsigned index = 0; ok && index < 3; ++index) ok = tp_retirement_digest(log_chains[index]);
    Sha256 hash;
    sha256_init(&hash);
    if (ok)
    {
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_POST_DOMAIN;
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_unit_campaign_text(&hash, "pre-sample", pre_context);
        bq_retirement_unit_campaign_text(&hash, "plan", campaign->plan_sha256);
        for (unsigned index = 0; index < 3; ++index) bq_retirement_unit_campaign_text(&hash, "logs", log_chains[index]);
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

/* The post-A/A evidence digest: the frozen pre-sample context and the
 * pre-sample plan document (which binds the execution plan, the result-input
 * plan and the family) chained to the finished A/A stage (its transcript
 * shard chain, numeric and shard descriptors and metrics totals) and to the
 * untimed and A/A launch-log chains. It exists only while the campaign awaits
 * A/A admission with a ready A/A stage; the admission must name it. The
 * validator's post-A/A binding document cannot be the admitted digest: it
 * binds the admission receipt, so it is written after admission
 * (retirement_unit_documents.h) and the A/B freeze requires it. */
static inline int bq_retirement_unit_campaign_post_aa(TpRetirementCampaign const* campaign, char const* pre_context,
    char const* pre_sample_plan, char const shard_chain[65], char const log_chains[2][65], char digest[65])
{
    TpRetirementSamples const* samples = campaign ? campaign->samples[0] : NULL;
    int ok = campaign && samples && digest && pre_context && shard_chain && log_chains &&
        tp_retirement_digest(pre_context) && tp_retirement_digest(pre_sample_plan) && tp_retirement_digest(shard_chain) &&
        tp_retirement_digest(log_chains[0]) && tp_retirement_digest(log_chains[1]) &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA &&
        !strcmp(campaign->context_sha256, pre_context) && tp_retirement_campaign_stage_ready(campaign, 0);
    Sha256 hash;
    sha256_init(&hash);
    if (ok)
    {
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA_DOMAIN;
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_unit_campaign_text(&hash, "pre-sample", pre_context);
        bq_retirement_unit_campaign_text(&hash, "pre-sample-plan", pre_sample_plan);
        bq_retirement_unit_campaign_text(&hash, "plan", campaign->plan_sha256);
        bq_retirement_unit_campaign_text(&hash, "transcript-shards", shard_chain);
        bq_retirement_unit_campaign_text(&hash, "untimed-logs", log_chains[0]);
        bq_retirement_unit_campaign_text(&hash, "aa-logs", log_chains[1]);
        bq_retirement_unit_campaign_number(&hash, "invocations", samples->transcript->total_records);
        bq_retirement_unit_campaign_number(&hash, "completed-at", samples->transcript->completed_at_ns);
        bq_retirement_unit_campaign_text(&hash, "raw", samples->raw_sha256);
        bq_retirement_unit_campaign_text(&hash, "row-shards", samples->descriptors_sha256[0]);
        bq_retirement_unit_campaign_text(&hash, "batch-shards", samples->descriptors_sha256[1]);
        bq_retirement_unit_campaign_number(&hash, "samples", samples->exported);
        bq_retirement_unit_campaign_number(&hash, "metrics-artifacts", samples->metrics ? samples->metrics->artifacts : 0);
        bq_retirement_unit_campaign_number(&hash, "metrics-bytes", samples->metrics ? samples->metrics->total_bytes : 0);
        sha256_finish_hex(&hash, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

/* The imported BQ-RETIREMENT-READY-V1 record (lane B's step 10): the
 * attempt it belongs to, its verified bytes and the fields D binds. The
 * store-based import in retirement_campaign_service.h fills it; the driver's
 * attach requires an owned one. correctness_sha256 is the sealed
 * correctness gate (BqRetirementCorrectness.sealed_sha256) the unit gate
 * seal (gate_sha256) covers; checks_authority_sha256 and
 * check_evidence_sha256 are the #509 required-check authority and the
 * digest of its run records and logs. unit_gate is the BqRetirementUnitGate
 * the import joined (by address; the bind accepts only that gate).
 * sources are A's base and candidate roots, held by the import (each opened
 * and scanned against A's manifest, as lane B's gate holds them) for every
 * launch's canonical layout: slots 5 and 6 and the sandbox's read-only rules
 * (bq_retirement_unit_campaign_inputs). The release closes them; a zeroed
 * record holds none (only descriptors from 3 up are ever held).
 * Zero-initialize; release on every path. */
typedef struct BqRetirementCampaignReady
{
    BqJob job;
    void const* unit_gate;
    char* text;
    int sources[2];
    u64 job_id, attempt_token;
    u32 length, rows, object_rows, native_target, observed_rows, owned;
    char ready_sha256[SHA256_HEX_CAPACITY], preparation_sha256[SHA256_HEX_CAPACITY];
    char binary_record_sha256[SHA256_HEX_CAPACITY], build_record_sha256[SHA256_HEX_CAPACITY];
    char binary_sha256[2][SHA256_HEX_CAPACITY];
    char support_sha256[SHA256_HEX_CAPACITY], census_sha256[SHA256_HEX_CAPACITY];
    char population_sha256[SHA256_HEX_CAPACITY], template_sha256[SHA256_HEX_CAPACITY];
    char inventory_sha256[SHA256_HEX_CAPACITY], oracle_attempt_sha256[SHA256_HEX_CAPACITY];
    char checks_authority_sha256[SHA256_HEX_CAPACITY], check_evidence_sha256[SHA256_HEX_CAPACITY];
    char correctness_sha256[SHA256_HEX_CAPACITY], gate_sha256[SHA256_HEX_CAPACITY];
} BqRetirementCampaignReady;

static inline void bq_retirement_campaign_ready_release(BqRetirementCampaignReady* ready)
{
    if (ready)
    {
        free(ready->text);
        for (u32 side = 0; side < 2; side += 1)
            if (ready->owned && ready->sources[side] >= 3) close(ready->sources[side]);
        *ready = (BqRetirementCampaignReady){0};
    }
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
    BQ_RETIREMENT_UNIT_CAMPAIGN_READY,
    BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED,
    BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED
} BqRetirementUnitCampaignStep;

/* Why the driver stopped. REFUSED: a step or its inputs were refused (order,
 * plan, binding, admission, store plan), or a launch's sandbox was: its
 * ruleset could not be built, or its child refused before exec (launched,
 * with the child's errno as launch_error). CANCELLED and DEADLINE stop before a
 * launch; CHANNEL is a failed phase exchange; LAUNCH is a launched child that
 * failed or whose output, metrics or record was refused (its process facts
 * say which). */
typedef enum BqRetirementUnitCampaignStop
{
    BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_NONE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED,
    BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CANCELLED,
    BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_DEADLINE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CHANNEL,
    BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH
} BqRetirementUnitCampaignStop;

/* The first failure, retained for lanes E and F: the step it stopped, the
 * stage (0 untimed, 1 A/A, 2 A/B, TP_RETIREMENT_NONE outside a stage),
 * whether a child was launched, the invocation's coordinates (NONE or -1 where
 * they do not apply; purpose is the untimed batch's, or
 * BQ_RETIREMENT_UNIT_CAMPAIGN_PURPOSE_PROGRAM for a runtime launch's program
 * step), and the launched
 * child's measurement status, exit code, signal, timeout and cancellation.
 * The unit cannot see a cgroup OOM kill directly: it shows as SIGKILL, and
 * the supervisor's memory events are the authority. A child that finished
 * but whose code observation, log chaining, log close, scratch retirement or
 * program observation was refused keeps its launch facts too, with `after`
 * naming the refused step (BqRetirementUnitCampaignAfter). The failed launch's log
 * (BQ_RETIREMENT_UNIT_CAMPAIGN_LOG) and scratch outputs are kept in place:
 * log_bytes and log_sha256 are the log's full size and digest (empty when
 * the log is too large to hash), retained_bytes and retained_sha256 those of
 * the kept file after the BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX cap. */
typedef enum BqRetirementUnitCampaignAfter
{
    BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_NONE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CODE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CHAIN,
    BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CLOSE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_RETIRE,
    BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_PROGRAM
} BqRetirementUnitCampaignAfter;

typedef struct BqRetirementUnitCampaignFailure
{
    unsigned reason, step, stage, launched, after;
    unsigned kind, group, row, variant, phase, purpose;
    int round, pair, warmup, status, exit_code, signal_number, timed_out, cancelled, launch_error;
    uint64_t sequence, at_ns, log_bytes, retained_bytes;
    char log_sha256[65], retained_sha256[65];
} BqRetirementUnitCampaignFailure;

/* Service-created, empty streams one stage consumes, in order: spare
 * transcript shards for rotation, spare metrics shards, and one stream per
 * numeric sample shard (capacity.sample_shards_per_stage). The caller owns
 * every stream and its durable no-replace publication (#1023); transcript
 * shard paths must sort in index order for lane E's composer. */
typedef struct BqRetirementUnitCampaignStreams
{
    FILE* const* transcripts;
    FILE* const* metrics;
    FILE* const* samples;
    unsigned transcript_count, metrics_count, sample_count;
} BqRetirementUnitCampaignStreams;

/* Where the code facts go: code_directory (service-owned, private, on the
 * work directory's file system) retains each untimed production object as
 * `code-<row>-<variant>.o`; rows maps each untimed singleton group to its
 * row (object groups name their rows in their contracts); codes (capacity
 * entries) receives one entry per code-observed row of the gate, both
 * variants: the untimed rows from their production object and
 * reproduction, the timed rows from their first A/B artifact (every timed
 * launch reproduces its frozen output digest). The set must be exactly the
 * gate's code-observed rows, and each side must equal the gate's artifact,
 * code-section digest and size where the gate has them. */
typedef struct BqRetirementUnitCampaignCode
{
    unsigned const* rows;
    TpRetirementCodeRow* codes;
    unsigned capacity;
    int code_directory;
} BqRetirementUnitCampaignCode;

/* One written workflow document: its size and SHA-256 (its path is
 * bq_retirement_unit_campaign_document_paths). */
typedef struct BqRetirementUnitCampaignDocument
{
    uint64_t bytes;
    char sha256[65];
} BqRetirementUnitCampaignDocument;

/* One predeclared #615 partition of the result-input plan: identity, store
 * path, first record and record count. */
typedef struct BqRetirementUnitCampaignPartition
{
    char identity[32], path[64];
    uint64_t start, records;
} BqRetirementUnitCampaignPartition;

/* Zero-initialize. The phase channel, the untimed runner, the held binaries,
 * the ready record, the code workspace and the binding are borrowed and must
 * outlive the driver. */
typedef struct BqRetirementUnitCampaign
{
    BqPhaseChannel* phases;
    BqRetirementCorrectness const* gate;
    BqRetirementCampaignBinding const* binding;
    TpRetirementUntimed* untimed;
    TpRetirementCodeRow* codes;
    /* The untimed batches, their singleton rows and the reviewed budget the
     * untimed step ran on (borrowed): the documents bind them. */
    TpRetirementUntimedBatch const* untimed_batches;
    unsigned const* untimed_rows;
    TpRetirementCampaignBudget const* budget;
    unsigned untimed_batch_count;
    /* The workflow documents written so far (documented of them, in order),
     * the result-input partitions (0 rows, 1 batches), the family and source
     * rows digests they bind and the A/A admission receipt digest. */
    BqRetirementUnitCampaignDocument documents[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS];
    BqRetirementUnitCampaignPartition partitions[2][BQ_RETIREMENT_UNIT_CAMPAIGN_PARTITIONS];
    unsigned partition_counts[2], documented;
    char family_sha256[65], source_rows_sha256[65], aa_admission_sha256[65];
    /* The support declaration and census manifest pins the documents named,
     * the digest of the timed-row layout they derived
     * (bq_retirement_unit_campaign_timed_line) and the post-sample record. */
    char support_sha256[65], manifest_sha256[65], timed_rows_sha256[65];
    BqRetirementUnitCampaignDocument record;
    TpRetirementExecutable untimed_executables[2];
    /* A's base and candidate roots (borrowed), for every launch's canonical
     * layout: slots 5 and 6 and the sandbox's read-only rules. */
    int sources[2];
    TpRetirementPlan plan;
    BqRetirementUnitCampaignFailure failure;
    Sha256 shard_hash[TP_RETIREMENT_CAMPAIGN_STAGES];
    Sha256 log_hash[3];
    TpRetirementShard untimed_records;
    TpRetirementShardFile untimed_metrics;
    char held_sha256[2][65], held_identity_sha256[2][65];
    char plan_sha256[65], context_sha256[65], post_aa_sha256[65], post_context_sha256[65];
    char shard_chain_sha256[TP_RETIREMENT_CAMPAIGN_STAGES][65];
    char log_chain_sha256[3][65];
    char sealed_result_sha256[65], authority_sha256[65], measured_sha256[65];
    uint64_t deadline_ns, launches[3];
    unsigned step, code_count, code_capacity, transcript_shards[TP_RETIREMENT_CAMPAIGN_STAGES], metrics_shards[3];
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

/* Record the first failure only; later refusals keep it. */
static inline void bq_retirement_unit_campaign_record(BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignFailure const* failure)
{
    if (driver && failure && !driver->failure.reason)
    {
        driver->failure = *failure;
        driver->failure.step = driver->step;
        driver->failure.at_ns = bq_phase_clock();
    }
}

static inline BqRetirementUnitCampaignFailure bq_retirement_unit_campaign_stop(unsigned reason, unsigned stage)
{
    BqRetirementUnitCampaignFailure failure = {.reason = reason, .stage = stage, .kind = TP_RETIREMENT_NONE,
        .group = TP_RETIREMENT_NONE, .row = TP_RETIREMENT_NONE, .variant = TP_RETIREMENT_NONE,
        .phase = TP_RETIREMENT_NONE, .purpose = TP_RETIREMENT_NONE, .round = -1, .pair = -1, .warmup = -1,
        .status = -1, .exit_code = -1};
    return failure;
}

static inline void bq_retirement_unit_campaign_fail(BqRetirementUnitCampaign* driver)
{
    if (driver)
    {
        BqRetirementUnitCampaignFailure refused = bq_retirement_unit_campaign_stop(BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED,
            TP_RETIREMENT_NONE);
        if (driver->phases && driver->phases->failed) refused.reason = BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CHANNEL;
        bq_retirement_unit_campaign_record(driver, &refused);
        if (driver->binding && driver->binding->campaign) tp_retirement_campaign_poison(driver->binding->campaign);
        if (driver->untimed && !driver->untimed->finished)
        {
            driver->untimed->failed = 1;
            if (driver->untimed->metrics) driver->untimed->metrics->failed = 1;
        }
        driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED;
    }
}

/* Before a launch of `timeout_seconds`: not cancelled, the channel intact and
 * the child able to finish before the absolute deadline (a launch is never
 * shortened, since its timeout is part of the frozen command). A refusal is
 * recorded with the coordinates the caller filled in. */
static inline int bq_retirement_unit_campaign_launchable(BqRetirementUnitCampaign* driver, unsigned timeout_seconds,
    BqRetirementUnitCampaignFailure* coordinates)
{
    struct pollfd wait = {.fd = driver->cancellation_fd, .events = POLLIN};
    int readable = driver->cancellation_fd >= 3 ? poll(&wait, 1, 0) : -1;
    uint64_t now = bq_phase_clock(), span = (uint64_t)timeout_seconds * UINT64_C(1000000000);
    int ok = readable == 0 && !driver->phases->failed && now && timeout_seconds &&
        now < driver->deadline_ns && span <= driver->deadline_ns - now;
    if (!ok)
    {
        coordinates->reason = readable != 0 ? BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CANCELLED :
            driver->phases->failed ? BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CHANNEL : BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_DEADLINE;
        bq_retirement_unit_campaign_record(driver, coordinates);
    }
    return ok;
}

/* A failed launch, or a finished child whose `after` step was refused: its
 * measurement status and process facts, and, while its log is open, the
 * log's full size and digest, the cap, and the kept file's size and digest.
 * PLAN_INVALID means the launch boundary refused before any child started. */
static inline void bq_retirement_unit_campaign_launch_failed(BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignFailure* coordinates, TpRetirementMeasurementResult const* result, int log, unsigned after)
{
    struct stat info;
    if (log >= 3 && fstat(log, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0)
    {
        uint64_t hashed = 0;
        coordinates->log_bytes = (uint64_t)info.st_size;
        if (!tp_retirement_file_hash(log, coordinates->log_sha256, &hashed) || hashed != coordinates->log_bytes)
            coordinates->log_sha256[0] = 0;
        int kept = coordinates->log_bytes <= BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX ||
            ftruncate(log, (off_t)BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX) == 0;
        if (!(kept && tp_retirement_file_hash(log, coordinates->retained_sha256, &coordinates->retained_bytes)))
        {
            coordinates->retained_sha256[0] = 0;
            coordinates->retained_bytes = 0;
        }
    }
    coordinates->after = after;
    coordinates->reason = result->process.refused ? BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED :
        BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH;
    coordinates->launched = result->status != TP_RETIREMENT_MEASUREMENT_PLAN_INVALID;
    coordinates->status = (int)result->status;
    coordinates->exit_code = result->process.exit_code;
    coordinates->signal_number = result->process.signal_number;
    coordinates->timed_out = result->process.timed_out;
    coordinates->cancelled = result->process.cancelled;
    coordinates->launch_error = result->process.launch_error;
    bq_retirement_unit_campaign_record(driver, coordinates);
}

/* A fresh, empty read-write log for one launch. */
static inline int bq_retirement_unit_campaign_launch_log(BqRetirementUnitCampaign const* driver)
{
    int log = driver ? openat(driver->log_directory, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG,
                              O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    return log;
}

/* A successful launch's log joins its stage's chain (digest and size) before
 * the scratch log is retired. */
static inline int bq_retirement_unit_campaign_log_chain(BqRetirementUnitCampaign* driver, int log, unsigned index)
{
    char digest[65] = {0};
    uint64_t bytes = 0;
    int ok = index < 3 && tp_retirement_file_hash(log, digest, &bytes);
    if (ok)
    {
        bq_retirement_unit_campaign_number(&driver->log_hash[index], "log", driver->launches[index]);
        bq_retirement_unit_campaign_number(&driver->log_hash[index], "bytes", bytes);
        bq_retirement_unit_campaign_text(&driver->log_hash[index], "sha256", digest);
    }
    return ok;
}

static inline void bq_retirement_unit_campaign_log_seal(BqRetirementUnitCampaign* driver, unsigned index)
{
    Sha256 copy = driver->log_hash[index];
    bq_retirement_unit_campaign_number(&copy, "logs", driver->launches[index]);
    sha256_finish_hex(&copy, driver->log_chain_sha256[index]);
}

/* Retire a successful launch's scratch outputs and its log; a failed launch
 * keeps both as evidence. */
static inline int bq_retirement_unit_campaign_retire(BqRetirementUnitCampaign const* driver,
    TpRetirementMeasuredCommand const* command)
{
    int ok = unlinkat(driver->log_directory, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, 0) == 0;
    if (command->batch)
    {
        ok = unlinkat(driver->work_directory, command->batch->metrics, 0) == 0 && ok;
        for (unsigned i = 0; i < command->batch->input_count; ++i)
            if (command->batch->inputs[i].artifact)
                ok = unlinkat(driver->work_directory, command->batch->inputs[i].artifact, 0) == 0 && ok;
    }
    else if (!command->kind && command->artifact) ok = unlinkat(driver->work_directory, command->artifact, 0) == 0 && ok;
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

/* SETTLING: the unit's build already holds the PREPARING acknowledgement.
 * The heavy store-based import (retirement_campaign_service.h) belongs in
 * this phase, before the untimed batches. */
static inline int bq_retirement_unit_campaign_begin(BqRetirementUnitCampaign* driver, BqPhaseChannel* phases,
    int cancellation_fd, uint64_t deadline_ns, int work_directory, int log_directory)
{
    /* SETTLING must be the channel's next phase: after PREPARING (BQPHASE1)
     * or after RETIREMENT_READY carried the ready digest (BQPHASE2). */
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_NEW && !driver->phases && phases &&
        bq_phase_next(phases->version, phases->sequence) == BQ_PHASE_SETTLING && work_directory >= 3 &&
        log_directory >= 3 &&
        bq_retirement_unit_campaign_live(phases, cancellation_fd, deadline_ns);
    if (ok)
    {
        driver->phases = phases;
        driver->cancellation_fd = cancellation_fd;
        driver->deadline_ns = deadline_ns;
        driver->work_directory = work_directory;
        driver->log_directory = log_directory;
        for (unsigned index = 0; index < 3; ++index)
        {
            static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_LOGS_DOMAIN;
            sha256_init(&driver->log_hash[index]);
            sha256_add(&driver->log_hash[index], domain, sizeof(domain) - 1);
            bq_retirement_unit_campaign_number(&driver->log_hash[index], "stage", index);
        }
        ok = bq_phase_exchange_until(phases, BQ_PHASE_SETTLING, deadline_ns);
    }
    if (ok) driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING;
    else bq_retirement_unit_campaign_fail(driver);
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

/* One untimed artifact: its output leaf and the row whose code it is. */
static inline int bq_retirement_unit_campaign_artifact(TpRetirementUntimedBatch const* batch,
    BqRetirementUnitCampaignCode const* code, unsigned index, char const** leaf, unsigned* row)
{
    TpRetirementBatchContract const* contract = batch->command.batch;
    int ok = contract ? index < contract->input_count && contract->inputs[index].member &&
        contract->inputs[index].artifact : !index && batch->command.artifact;
    *leaf = ok ? (contract ? contract->inputs[index].artifact : batch->command.artifact) : NULL;
    *row = ok ? (contract ? contract->inputs[index].row : code->rows[batch->group]) : TP_RETIREMENT_NONE;
    return ok;
}

static inline int bq_retirement_unit_campaign_code_name(char name[48], unsigned row, unsigned variant)
{
    int length = snprintf(name, 48, "code-%u-%u.o", row, variant);
    return length > 0 && length < 48;
}

/* Whether gate row `row` is in the native-host timed projection. */
static inline int bq_retirement_unit_campaign_timed_row(BqRetirementCorrectness const* gate, uint32_t row)
{
    int timed = gate->trusted_rows[row].compiler_eligible &&
        gate->trusted_rows[row].target == gate->prepared.native_target;
    return timed;
}

/* One observed side: artifact and reproduction equal, the gate's artifact
 * (a code-observed row always has one), and its code-section digest and size
 * where the gate has them. */
static inline int bq_retirement_unit_campaign_code_fact(BqRetirementCorrectness const* gate, unsigned row,
    unsigned variant, TpRetirementCodeSide const* side)
{
    BqRetirementObservedSide const* fact = &gate->facts[row].side[variant];
    int ok = side->artifact_sha256[0] && !strcmp(side->artifact_sha256, side->reproduction_sha256) &&
        fact->artifact_sha256[0] && !strcmp(fact->artifact_sha256, side->artifact_sha256) &&
        (!fact->code_sha256[0] || (!strcmp(fact->code_sha256, side->code_sha256) &&
                                   fact->code_bytes == side->code_bytes));
    return ok;
}

/* The entry of `row`, appended (empty) if it is new and capacity allows. */
static inline TpRetirementCodeRow* bq_retirement_unit_campaign_code_entry(BqRetirementUnitCampaign* driver,
    unsigned row)
{
    unsigned entry = 0;
    while (entry < driver->code_count && driver->codes[entry].row != row) ++entry;
    TpRetirementCodeRow* found = entry < driver->code_count ? &driver->codes[entry] : NULL;
    if (!found && entry < driver->code_capacity)
    {
        driver->codes[entry] = (TpRetirementCodeRow){.row = row};
        driver->code_count += 1;
        found = &driver->codes[entry];
    }
    return found;
}

/* After a production batch, keep each object as the row's frozen artifact
 * in the code directory (a no-replace link, then the scratch name goes);
 * after its reproduction, parse the frozen artifact and require the
 * reproduction to be byte-identical (tp_retirement_code_observe). */
static inline int bq_retirement_unit_campaign_code_step(BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignCode const* code, TpRetirementUntimedBatch const* batch)
{
    unsigned count = batch->command.batch ? batch->command.batch->input_count : 1;
    int ok = 1;
    for (unsigned index = 0; ok && index < count; ++index)
    {
        char const* leaf = NULL;
        unsigned row = TP_RETIREMENT_NONE;
        char name[48];
        if (!bq_retirement_unit_campaign_artifact(batch, code, index, &leaf, &row)) continue;
        ok = row != TP_RETIREMENT_NONE && bq_retirement_unit_campaign_code_name(name, row, batch->variant);
        if (ok && batch->purpose == TP_RETIREMENT_UNTIMED_PRODUCTION)
            ok = linkat(driver->work_directory, leaf, code->code_directory, name, 0) == 0 &&
                unlinkat(driver->work_directory, leaf, 0) == 0;
        else if (ok)
        {
            TpRetirementCodeRow* entry = bq_retirement_unit_campaign_code_entry(driver, row);
            ok = entry != NULL;
            int frozen = ok ? openat(code->code_directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
            int reproduction = ok ? openat(driver->work_directory, leaf, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
            TpRetirementCodeSide* side = ok ? &entry->sides[batch->variant] : NULL;
            ok = ok && frozen >= 3 && reproduction >= 3 && !side->artifact_sha256[0] &&
                tp_retirement_code_observe(frozen, reproduction, side);
            if (frozen >= 0 && close(frozen) != 0) ok = 0;
            if (reproduction >= 0 && close(reproduction) != 0) ok = 0;
        }
    }
    return ok;
}

/* A production batch's objects already moved to the code directory, so only
 * its log and metrics remain to retire; a reproduction retires everything. */
static inline int bq_retirement_unit_campaign_untimed_retire(BqRetirementUnitCampaign const* driver,
    TpRetirementUntimedBatch const* batch)
{
    TpRetirementMeasuredCommand retired = batch->command;
    TpRetirementBatchContract kept;
    TpRetirementBatchInput* left = NULL;
    int ok = 1;
    if (batch->purpose == TP_RETIREMENT_UNTIMED_PRODUCTION)
    {
        retired.artifact = NULL;
        if (retired.batch)
        {
            kept = *retired.batch;
            left = (TpRetirementBatchInput*)malloc((size_t)kept.input_count * sizeof(*left));
            ok = left != NULL;
            for (unsigned k = 0; ok && k < kept.input_count; ++k)
            {
                left[k] = kept.inputs[k];
                if (left[k].member) left[k].artifact = NULL;
            }
            kept.inputs = left;
            retired.batch = &kept;
        }
    }
    ok = ok && bq_retirement_unit_campaign_retire(driver, &retired);
    free(left);
    return ok;
}

/* Sorted by row, then exactly the gate's code-observed rows (untimed only,
 * or every row with `timed`), each side agreeing with the gate. A row's code
 * is observed when it is compile-eligible (the validator's _code_observed:
 * its oracle parsed the code section, a zero baseline included), which is
 * wider than the gate's code_eligible (a nonzero baseline section): the
 * validator's untimed groups and code records cover every code-observed
 * row. */
static inline int bq_retirement_unit_campaign_codes_sealed(BqRetirementUnitCampaign* driver, int timed)
{
    BqRetirementCorrectness const* gate = driver->gate;
    int ok = gate && gate->facts && gate->trusted_rows && driver->code_count != 0;
    for (unsigned entry = 1; ok && entry < driver->code_count; ++entry)
    {
        TpRetirementCodeRow moving = driver->codes[entry];
        unsigned at = entry;
        while (at && driver->codes[at - 1].row > moving.row)
        {
            driver->codes[at] = driver->codes[at - 1];
            --at;
        }
        driver->codes[at] = moving;
    }
    for (unsigned entry = 1; ok && entry < driver->code_count; ++entry)
        ok = driver->codes[entry - 1].row < driver->codes[entry].row;
    unsigned matched = 0;
    for (uint32_t row = 0; ok && row < gate->prepared.rows; ++row)
    {
        if (!gate->trusted_rows[row].compiler_eligible || (!timed && bq_retirement_unit_campaign_timed_row(gate, row)))
            continue;
        TpRetirementCodeRow const* entry = matched < driver->code_count ? &driver->codes[matched] : NULL;
        ok = entry && entry->row == row && bq_retirement_unit_campaign_code_fact(gate, row, 0, &entry->sides[0]) &&
            bq_retirement_unit_campaign_code_fact(gate, row, 1, &entry->sides[1]);
        matched += 1;
    }
    ok = ok && matched == driver->code_count;
    return ok;
}

/* After a successful A/B compiler launch: the first artifact of each
 * code-observed timed row and variant is observed before it is retired (the
 * launch already required the command's frozen output digest). */
static inline int bq_retirement_unit_campaign_timed_code(BqRetirementUnitCampaign* driver,
    TpRetirementSamples const* samples, TpRetirementInvocation const* invocation,
    TpRetirementMeasuredCommand const* command)
{
    BqRetirementCorrectness const* gate = driver->gate;
    unsigned count = command->batch ? command->batch->input_count : 1;
    int ok = 1;
    for (unsigned index = 0; ok && !invocation->kind && index < count; ++index)
    {
        TpRetirementBatchInput const* input = command->batch ? &command->batch->inputs[index] : NULL;
        char const* leaf = input ? (input->member ? input->artifact : NULL) : command->artifact;
        unsigned first = samples->groups[invocation->group].first;
        unsigned row = input ? input->row : samples->rows[samples->members[first]].id;
        if (!leaf || row >= gate->prepared.rows || !gate->trusted_rows[row].compiler_eligible) continue;
        TpRetirementCodeRow* entry = bq_retirement_unit_campaign_code_entry(driver, row);
        TpRetirementCodeSide* side = entry ? &entry->sides[invocation->variant] : NULL;
        ok = side != NULL;
        if (ok && !side->artifact_sha256[0])
        {
            int artifact = openat(driver->work_directory, leaf, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            ok = artifact >= 3 && tp_retirement_code_observe(artifact, artifact, side) &&
                bq_retirement_unit_campaign_code_fact(gate, row, invocation->variant, side);
            if (artifact >= 0 && close(artifact) != 0) ok = 0;
        }
    }
    return ok;
}

/* One launch's inputs in lane B's canonical child layout
 * (retirement_sandbox.h): the held executable of side at slot 3 + side, A's
 * roots at 5 and 6 and the work directory `work` at 7 (the cwd), a new
 * sandbox ruleset over exactly those (the caller closes it) and the command's
 * address-space limit. The child enters the sandbox before the timer
 * starts (tp_process_observe_inputs). */
static inline int bq_retirement_unit_campaign_inputs(BqRetirementUnitCampaign const* driver, int executable,
    unsigned side, int log, int work, TpRetirementMeasuredCommand const* command, TpProcessInputs* inputs)
{
    int ruleset = command && log >= 3 && work >= 3 ?
        bq_retirement_sandbox(&executable, 1, driver->sources, work, NULL) : -1;
    *inputs = (TpProcessInputs){executable, work, log, command ? command->environment : NULL,
        driver->cancellation_fd, (int)side, {driver->sources[0], driver->sources[1]}, ruleset,
        command ? (uint64_t)command->memory_mib << 20 : 0, NULL};
    return ruleset >= 3;
}

/* A timed runtime launch's program. Lane B runs a row's runtime step in the
 * step directory its compile step just wrote (retirement_row_producer.c,
 * bq_retirement_row_compile: same work directory, same held binary slot and
 * sandbox), and the campaign's compiler launches retire their artifacts, so
 * every runtime launch gets a fresh step directory `runtime-<stage>-<sequence>`
 * in the work directory: its row's frozen compile command for the same
 * variant (the only compiler command whose artifact is the runtime command's
 * `./<leaf>`) runs there untimed on the same held binary, must reproduce the
 * frozen artifact, and the file it wrote is observed (a single-link,
 * owner-executable regular file of this user) for the launch to recheck
 * (TpProcessInputs.program). A label-1 runtime command must also be the
 * gate's sealed runtime command for its side (the A/A second label's is not
 * sealed, like its compiler command). A refusal or failure is recorded with
 * purpose BQ_RETIREMENT_UNIT_CAMPAIGN_PURPOSE_PROGRAM and the step directory
 * kept as evidence. */
#define BQ_RETIREMENT_UNIT_CAMPAIGN_PURPOSE_PROGRAM 2u
typedef struct BqRetirementUnitCampaignProgram
{
    TpProcessProgram program;
    char name[48];
    int step;
} BqRetirementUnitCampaignProgram;

static inline int bq_retirement_unit_campaign_program(BqRetirementUnitCampaign* driver,
    TpRetirementMeasuredCommand const* commands, TpRetirementMeasuredCommand const* runtime, unsigned stage,
    unsigned variant, uint64_t sequence, BqRetirementUnitCampaignFailure const* at,
    BqRetirementUnitCampaignProgram* program)
{
    BqRetirementCampaignBinding const* binding = driver->binding;
    TpRetirementCampaign const* campaign = binding->campaign;
    BqRetirementCorrectness const* gate = binding->gate;
    unsigned side = stage && variant;
    char const* argument = runtime->arguments ? runtime->arguments[0] : NULL;
    char const* leaf = argument && argument[0] == '.' && argument[1] == '/' ? argument + 2 : NULL;
    TpRetirementMeasuredCommand const* compile = NULL;
    unsigned found = 0;
    for (unsigned group = 0; leaf && group < campaign->groups; ++group)
    {
        TpRetirementMeasuredCommand const* candidate = &commands[(size_t)group * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT +
                                                                 variant];
        if (!candidate->kind && !candidate->batch && candidate->artifact && !strcmp(candidate->artifact, leaf))
        {
            compile = candidate;
            found += 1;
        }
    }
    int sealed = gate && runtime->unit < gate->prepared.rows &&
        ((!stage && variant) || !strcmp(runtime->command_sha256, gate->trusted_rows[runtime->unit].runtime_command_sha256[side]));
    BqRetirementUnitCampaignFailure coordinates = *at;
    coordinates.purpose = BQ_RETIREMENT_UNIT_CAMPAIGN_PURPOSE_PROGRAM;
    int named = snprintf(program->name, sizeof(program->name), "runtime-%u-%llu", stage,
                         (unsigned long long)sequence);
    int ok = runtime->kind == 1 && leaf && found == 1 && sealed && named > 0 && (size_t)named < sizeof(program->name) &&
        gate->finished && !gate->failed &&
        !memcmp(binding->sealed_sha256, gate->sealed_sha256, sizeof(binding->sealed_sha256)) &&
        bq_retirement_campaign_held_matches(binding);
    if (!ok) bq_retirement_unit_campaign_record(driver, &coordinates);
    ok = ok && compile->timeout_seconds <= ~0u - runtime->timeout_seconds &&
        bq_retirement_unit_campaign_launchable(driver, compile->timeout_seconds + runtime->timeout_seconds, &coordinates);
    if (ok && mkdirat(driver->work_directory, program->name, 0700) == 0)
        program->step = openat(driver->work_directory, program->name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int log = ok && program->step >= 3 ? bq_retirement_unit_campaign_launch_log(driver) : -1;
    TpProcessInputs inputs = {.ruleset = -1};
    int sandboxed = log >= 3 && bq_retirement_unit_campaign_inputs(driver, binding->held_executables[side].descriptor,
                                                                   side, log, program->step, compile, &inputs);
    if (ok && !sandboxed) bq_retirement_unit_campaign_record(driver, &coordinates);
    TpRetirementMeasurementResult result = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
        .process = {.exit_code = -1}};
    TpRetirementLaunch launch = {compile, &binding->held_executables[side], &inputs, NULL, program->step,
        campaign->cpu, TP_RETIREMENT_GROUP_SINGLETON};
    char digest[65];
    int launched = ok && sandboxed;
    ok = launched && tp_retirement_launch(&launch, NULL, 0, &result, digest);
    if (inputs.ruleset >= 0) close(inputs.ruleset);
    struct stat info;
    int observed = ok && fstatat(program->step, leaf, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
        info.st_nlink == 1 && info.st_uid == geteuid() && !(info.st_mode & 0022) && (info.st_mode & S_IXUSR);
    if (launched && (!ok || !observed))
        bq_retirement_unit_campaign_launch_failed(driver, &coordinates, &result, log,
            ok ? BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_PROGRAM : BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_NONE);
    ok = ok && observed;
    if (ok) program->program = (TpProcessProgram){leaf, info.st_dev, info.st_ino, info.st_size, info.st_ctim};
    unsigned after = log >= 0 && close(log) != 0 ? BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CLOSE :
        ok && unlinkat(driver->log_directory, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, 0) != 0 ?
        BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_RETIRE : BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_NONE;
    if (ok && after) bq_retirement_unit_campaign_launch_failed(driver, &coordinates, &result, -1, after);
    ok = ok && !after;
    return ok;
}

/* A successful runtime launch's step directory goes: its program, what its
 * compile step wrote beside it (a compile's metrics leaf is not a command
 * field) and anything else its files; a directory left inside fails the
 * retirement. */
static inline int bq_retirement_unit_campaign_program_retire(BqRetirementUnitCampaign const* driver,
    BqRetirementUnitCampaignProgram const* program)
{
    int listed = program->step >= 3 ? fcntl(program->step, F_DUPFD_CLOEXEC, 3) : -1;
    DIR* listing = listed >= 0 ? fdopendir(listed) : NULL;
    if (!listing && listed >= 0) close(listed);
    int ok = listing != NULL, found = 0;
    for (struct dirent* entry = ok ? readdir(listing) : NULL; ok && entry; entry = readdir(listing))
    {
        int dots = !strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..");
        found = found || !strcmp(entry->d_name, program->program.leaf);
        ok = dots || unlinkat(program->step, entry->d_name, 0) == 0;
    }
    if (listing) closedir(listing);
    ok = ok && found && unlinkat(driver->work_directory, program->name, AT_REMOVEDIR) == 0;
    return ok;
}

/* Untimed code-artifact batches, outside timing and before MEASURING: each
 * group's production then reproduction batch per variant, in (group,
 * variant, purpose) order, on the held binary of its variant (the pair the
 * store-based import holds), its group shape the reviewed one. Each untimed
 * row's code side is observed from the retained production object and the
 * reproduction before any scratch output is retired, and the observed rows
 * must be exactly gate's code-observed untimed rows (the gate is the unit
 * gate's correctness gate the import joined; attach requires the binding to
 * use the same one). The runner rejects an order or window violation and
 * finish requires every reproduction. */
static inline int bq_retirement_unit_campaign_untimed(BqRetirementUnitCampaign* driver, TpRetirementUntimed* untimed,
    TpRetirementUntimedBatch const* batches, unsigned count, BqRetirementHeldBinaries const* held,
    int const sources[2], BqRetirementCorrectness const* gate, TpRetirementCampaignReview const* review,
    BqRetirementUnitCampaignStreams const* streams, BqRetirementUnitCampaignCode const* code)
{
    unsigned char* produced = untimed && untimed->groups && untimed->groups <= TP_RETIREMENT_MAX_CELLS ?
        (unsigned char*)calloc((size_t)untimed->groups * 2, 1) : NULL;
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_SETTLING && untimed && !untimed->failed &&
        !untimed->finished && !untimed->records && batches && gate && bq_retirement_correctness_ready(gate) &&
        review && streams && code && code->rows && code->codes && code->capacity && code->code_directory >= 3 &&
        produced &&
        review->untimed_groups == untimed->groups && count == 4u * untimed->groups &&
        review->untimed_inputs && review->untimed_kinds && sources && sources[0] >= 3 && sources[1] >= 3 &&
        bq_retirement_unit_campaign_executables(held, driver->untimed_executables);
    if (driver) driver->untimed = untimed;
    if (ok)
    {
        driver->gate = gate;
        driver->codes = code->codes;
        driver->code_capacity = code->capacity;
        driver->code_count = 0;
        driver->untimed_batches = batches;
        driver->untimed_batch_count = count;
        driver->untimed_rows = code->rows;
        driver->budget = review->budget;
        driver->sources[0] = sources[0];
        driver->sources[1] = sources[1];
    }
    for (unsigned side = 0; ok && side < 2; ++side)
    {
        memcpy(driver->held_sha256[side], held->verified.binary_sha256[side], 65);
        memcpy(driver->held_identity_sha256[side], held->verified.binary_identity_sha256[side], 65);
    }
    for (unsigned i = 0; ok && i < count; ++i)
    {
        TpRetirementUntimedBatch const* batch = &batches[i];
        unsigned inputs_count = batch->command.batch ? batch->command.batch->input_count : 1;
        BqRetirementUnitCampaignFailure coordinates = bq_retirement_unit_campaign_stop(
            BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED, 0);
        coordinates.group = batch->group;
        coordinates.variant = batch->variant;
        coordinates.purpose = batch->purpose;
        coordinates.sequence = i;
        ok = batch->group < untimed->groups && batch->variant < 2 && batch->purpose < 2 &&
            batch->group_kind == review->untimed_kinds[batch->group] &&
            inputs_count == review->untimed_inputs[batch->group] &&
            (batch->purpose == TP_RETIREMENT_UNTIMED_PRODUCTION || produced[batch->group * 2 + batch->variant]);
        if (!ok) bq_retirement_unit_campaign_record(driver, &coordinates);
        ok = ok && bq_retirement_unit_campaign_launchable(driver, batch->command.timeout_seconds, &coordinates) &&
            bq_retirement_unit_campaign_metrics_ready(driver, untimed->metrics, streams->metrics,
                streams->metrics_count, 0);
        int log = ok ? bq_retirement_unit_campaign_launch_log(driver) : -1;
        TpProcessInputs inputs;
        int sandboxed = bq_retirement_unit_campaign_inputs(driver,
            driver->untimed_executables[batch->variant].descriptor, batch->variant, log, driver->work_directory,
            &batch->command, &inputs);
        if (ok && !sandboxed) bq_retirement_unit_campaign_record(driver, &coordinates);
        TpRetirementMeasurementResult result = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
            .process = {.exit_code = -1}};
        int launched = ok && log >= 3 && sandboxed;
        ok = launched && tp_retirement_untimed_run(untimed, batch, &driver->untimed_executables[batch->variant],
            &inputs, driver->work_directory, &result);
        unsigned after = BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_NONE;
        if (inputs.ruleset >= 0) close(inputs.ruleset);
        if (ok && !bq_retirement_unit_campaign_code_step(driver, code, batch))
            after = BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CODE;
        else if (ok && !bq_retirement_unit_campaign_log_chain(driver, log, 0))
            after = BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CHAIN;
        if (launched && (!ok || after)) bq_retirement_unit_campaign_launch_failed(driver, &coordinates, &result, log, after);
        ok = ok && !after;
        after = log >= 0 && close(log) != 0 ? BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CLOSE :
            ok && !bq_retirement_unit_campaign_untimed_retire(driver, batch) ? BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_RETIRE :
            BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_NONE;
        if (ok && after) bq_retirement_unit_campaign_launch_failed(driver, &coordinates, &result, -1, after);
        ok = ok && !after;
        if (ok)
        {
            if (batch->purpose == TP_RETIREMENT_UNTIMED_PRODUCTION) produced[batch->group * 2 + batch->variant] = 1;
            driver->launches[0] += 1;
        }
    }
    free(produced);
    ok = ok && tp_retirement_untimed_finish(untimed, &driver->untimed_records);
    if (ok && untimed->metrics)
    {
        ok = bq_retirement_unit_campaign_metrics_ready(driver, untimed->metrics, NULL, 0, 0) &&
            tp_retirement_metrics_shards_finish(untimed->metrics, &driver->untimed_metrics);
        if (ok) driver->metrics_shards[0] += 1;
    }
    ok = ok && bq_retirement_unit_campaign_codes_sealed(driver, 0);
    if (ok)
    {
        bq_retirement_unit_campaign_log_seal(driver, 0);
        driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_UNTIMED;
    }
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
 * acknowledgement. The binding must carry the store bind's mark for exactly
 * this owned ready record of this job and attempt; its held binaries must be
 * the ones the untimed batches ran; its pre-sample binding must follow the
 * MEASURING acknowledgement and every untimed batch; its plan, plan digest
 * and context must equal the ones re-derived here from the pins, the ready
 * record and the frozen snapshot; lane E's family counts (derived from the
 * same frozen layout before timing) must equal the plan's; and the result
 * store must be planned for the frozen capacity. */
static inline int bq_retirement_unit_campaign_attach(BqRetirementUnitCampaign* driver,
    BqRetirementCampaignBinding const* binding, BqRetirementCampaignReady const* ready,
    BqRetirementUnitCampaignPins const* pins, TpRetirementCampaignStorePlan const* store_plan,
    TpRetirementFamilyCounts const* family)
{
    TpRetirementCampaign const* campaign = binding ? binding->campaign : NULL;
    BqRetirementHeldBinaries const* held = binding ? binding->held_binaries : NULL;
    TpRetirementPlan plan = {0};
    char plan_sha256[65] = {0}, commands[65] = {0}, context[65] = {0};
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_MEASURING && campaign && held && pins && ready &&
        family && ready->owned && driver->phases->sequence == BQ_PHASE_MEASURING &&
        ready->job_id == driver->phases->job && ready->attempt_token == driver->phases->attempt &&
        binding->job_id == driver->phases->job && binding->attempt_token == driver->phases->attempt &&
        tp_retirement_digest(ready->ready_sha256) && !strcmp(binding->unit_ready_sha256, ready->ready_sha256) &&
        binding->gate == driver->gate &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_AA && bq_retirement_campaign_held_matches(binding) &&
        !campaign->samples[0]->transcript->execution->sequence && driver->untimed->last_end < campaign->bound_at_ns &&
        campaign->bound_at_ns > driver->phases->last_time &&
        bq_retirement_unit_campaign_store_planned(campaign, store_plan) &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns);
    for (unsigned side = 0; ok && side < 2; ++side)
        ok = !strcmp(held->verified.binary_sha256[side], driver->held_sha256[side]) &&
            !strcmp(held->verified.binary_identity_sha256[side], driver->held_identity_sha256[side]);
    ok = ok && bq_retirement_unit_campaign_plan(binding->gate, pins, &plan) &&
        !memcmp(&plan, &campaign->plan, sizeof(plan)) && family->bootstrap_members == plan.bootstrap_members_per_scope &&
        family->cell_members == plan.cell_members_per_scope &&
        bq_retirement_unit_campaign_plan_digest(binding->gate, &plan, plan_sha256) &&
        !strcmp(plan_sha256, campaign->plan_sha256) && !strcmp(pins->budget_sha256, campaign->budget_sha256) &&
        bq_retirement_unit_campaign_commands_frozen(campaign, commands);
    BqRetirementUnitCampaignFacts facts = {plan_sha256, ready ? ready->ready_sha256 : NULL,
        pins ? pins->budget_sha256 : NULL, commands, driver ? &driver->untimed_records : NULL,
        campaign ? campaign->job : NULL, campaign ? campaign->boot : NULL, campaign ? campaign->attempt : 0,
        campaign ? campaign->bound_at_ns : 0, campaign ? campaign->cpu : -1};
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
 * rechecks the sealed gate and held join before each launch; the child is
 * killed if the cancellation descriptor becomes readable while it runs, and
 * no launch starts that could outlive the deadline. Then the metrics writer,
 * the transcript and the numeric export finish, and the stage must be ready.
 * After A/A the post-A/A binding digest is formed. There is no retry: any
 * failure is recorded and poisons the attempt. */
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
        driver->documented == (stage ? BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS : BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA) &&
        campaign->phase == (stage ? TP_RETIREMENT_CAMPAIGN_AB : TP_RETIREMENT_CAMPAIGN_AA) &&
        command_count == (size_t)(campaign->groups + campaign->runtime_count) * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT &&
        streams->sample_count >= campaign->capacity.sample_shards_per_stage;
    TpRetirementInvocation invocation;
    while (ok && tp_retirement_execution_peek(execution, &invocation) == TP_RETIREMENT_NEXT_READY)
    {
        BqRetirementUnitCampaignFailure coordinates = bq_retirement_unit_campaign_stop(
            BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED, 1 + stage);
        coordinates.sequence = invocation.sequence;
        coordinates.kind = invocation.kind;
        coordinates.group = invocation.group;
        coordinates.row = invocation.row;
        coordinates.variant = invocation.variant;
        coordinates.phase = invocation.phase;
        coordinates.round = invocation.round;
        coordinates.pair = invocation.pair;
        coordinates.warmup = invocation.warmup;
        size_t index = ((size_t)(invocation.kind ? campaign->groups + invocation.dense : invocation.group)) *
            TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + invocation.variant;
        TpRetirementMeasuredCommand const* command = index < command_count ? &commands[index] : NULL;
        ok = command && bq_retirement_unit_campaign_launchable(driver, command->timeout_seconds, &coordinates) &&
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
        /* A runtime launch runs in its program's fresh step directory
         * (bq_retirement_unit_campaign_program); a compiler launch in the
         * work directory. */
        BqRetirementUnitCampaignProgram program = {.step = -1};
        if (ok && invocation.kind)
            ok = bq_retirement_unit_campaign_program(driver, commands, command, stage, invocation.variant,
                                                     invocation.sequence, &coordinates, &program);
        int work = invocation.kind ? program.step : driver->work_directory;
        int log = ok ? bq_retirement_unit_campaign_launch_log(driver) : -1;
        unsigned side = stage && invocation.variant;
        TpProcessInputs inputs;
        int sandboxed = bq_retirement_unit_campaign_inputs(driver, binding->held_executables[side].descriptor, side, log,
            work, command, &inputs);
        if (invocation.kind) inputs.program = &program.program;
        if (ok && !sandboxed) bq_retirement_unit_campaign_record(driver, &coordinates);
        TpRetirementMeasurementResult result = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
            .process = {.exit_code = -1}};
        int launched = ok && log >= 3 && sandboxed;
        ok = launched && bq_retirement_campaign_run(binding, command, &inputs, work, &result);
        if (inputs.ruleset >= 0) close(inputs.ruleset);
        unsigned after = BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_NONE;
        if (ok && stage && !bq_retirement_unit_campaign_timed_code(driver, samples, &invocation, command))
            after = BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CODE;
        else if (ok && !bq_retirement_unit_campaign_log_chain(driver, log, 1 + stage))
            after = BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CHAIN;
        if (launched && (!ok || after)) bq_retirement_unit_campaign_launch_failed(driver, &coordinates, &result, log, after);
        ok = ok && !after;
        after = log >= 0 && close(log) != 0 ? BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CLOSE :
            ok && !(bq_retirement_unit_campaign_retire(driver, command) &&
                    (!invocation.kind || bq_retirement_unit_campaign_program_retire(driver, &program))) ?
            BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_RETIRE : BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_NONE;
        if (ok && after) bq_retirement_unit_campaign_launch_failed(driver, &coordinates, &result, -1, after);
        ok = ok && !after;
        if (program.step >= 0 && close(program.step) != 0 && ok)
        {
            bq_retirement_unit_campaign_launch_failed(driver, &coordinates, &result, -1,
                                                      BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CLOSE);
            ok = 0;
        }
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
        bq_retirement_unit_campaign_log_seal(driver, 1 + stage);
        ok = stage || bq_retirement_unit_campaign_post_aa(campaign, driver->context_sha256,
            driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256, driver->shard_chain_sha256[0],
            (char const (*)[65])driver->log_chain_sha256, driver->post_aa_sha256);
    }
    if (ok) driver->step = stage ? BQ_RETIREMENT_UNIT_CAMPAIGN_AB : BQ_RETIREMENT_UNIT_CAMPAIGN_AA;
    else bq_retirement_unit_campaign_fail(driver);
    return ok;
}

/* The A/A admission the service must present: the #426 decision bound to
 * this job's frozen plan, pre-sample context and post-A/A evidence digest,
 * delivered as #1021's one-use capability, with the #437 admission receipt's
 * bytes and digest. */
typedef struct BqRetirementUnitCampaignAdmission
{
    char const* plan_sha256;
    char const* context_sha256;
    char const* post_aa_sha256;
    char const* receipt_sha256;
    unsigned char const* receipt;
    size_t receipt_bytes;
    int admitted;
} BqRetirementUnitCampaignAdmission;

/* Whether the flat JSON object `text` holds `"key":value` with exactly that
 * value (a member is followed by `,` or the closing `}`). */
static inline int bq_retirement_unit_campaign_member(char const* text, size_t length, char const* key,
    char const* value)
{
    char member[160];
    int written = snprintf(member, sizeof(member), "\"%s\":%s", key, value);
    size_t size = written > 0 && (size_t)written < sizeof(member) ? (size_t)written : 0;
    int found = 0;
    for (size_t at = 1; size && !found && at + size < length; ++at)
        found = (text[at - 1] == ',' || text[at - 1] == '{') && !memcmp(text + at, member, size) &&
            (text[at + size] == ',' || text[at + size] == '}');
    return found;
}

/* The #437 receipt the admission carries: its bytes hash to the recorded
 * digest, and it is this campaign's approved schema, admitted and native
 * only, on the campaign's CPU and native target, over the family the
 * pre-sample plan named. Its schema has no field for the post-A/A evidence
 * digest (the validator refuses unknown receipt fields), so the admission
 * capability names that digest and the post-A/A binding document binds this
 * receipt. */
static inline int bq_retirement_unit_campaign_receipt(BqRetirementUnitCampaign const* driver,
    TpRetirementCampaign const* campaign, BqRetirementUnitCampaignAdmission const* admission)
{
    char digest[65] = {0}, family[80], cpu[24];
    size_t length = admission->receipt_bytes;
    char const* text = (char const*)admission->receipt;
    int ok = text && length > 2 && length <= BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX && text[0] == '{' &&
        text[length - 1] == '}' && campaign->cpu >= 0 && tp_retirement_digest(driver->family_sha256) &&
        tp_retirement_digest(admission->receipt_sha256);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, text, (u64)length);
        sha256_finish_hex(&hash, digest);
        snprintf(family, sizeof(family), "\"%s\"", driver->family_sha256);
        snprintf(cpu, sizeof(cpu), "%d", campaign->cpu);
    }
    ok = ok && !strcmp(digest, admission->receipt_sha256) &&
        bq_retirement_unit_campaign_member(text, length, "schema", "\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA "\"") &&
        bq_retirement_unit_campaign_member(text, length, "version", "1") &&
        bq_retirement_unit_campaign_member(text, length, "admitted", "true") &&
        bq_retirement_unit_campaign_member(text, length, "native_only", "true") &&
        bq_retirement_unit_campaign_member(text, length, "native_target", "\"x86_64-unknown-linux-gnu\"") &&
        bq_retirement_unit_campaign_member(text, length, "logical_cpu", cpu) &&
        bq_retirement_unit_campaign_member(text, length, "family_sha256", family);
    return ok;
}

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
    ok = ok && admission->plan_sha256 && admission->context_sha256 && admission->post_aa_sha256 &&
        !strcmp(admission->plan_sha256, driver->plan_sha256) &&
        !strcmp(admission->context_sha256, driver->context_sha256) &&
        !strcmp(admission->post_aa_sha256, driver->post_aa_sha256) &&
        bq_retirement_unit_campaign_receipt(driver, campaign, admission) &&
        tp_retirement_campaign_admit_aa_fixture(campaign, admission->admitted, admission->plan_sha256,
            admission->context_sha256, admission->receipt_sha256);
    if (ok)
    {
        memcpy(driver->aa_admission_sha256, admission->receipt_sha256, 65);
        driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_ADMITTED;
    }
    else bq_retirement_unit_campaign_fail(driver);
#else
    /* #426 has no approved empirical A/A decision and #1021 no launch
     * capability: nothing can authorize A/B. */
    ok = 0;
#endif
    return ok;
}

/* The A/B launch freeze, after admission and the post-A/A binding document,
 * before the first candidate child: the held join, the sealed gate, the
 * frozen plan and context, the finished A/A evidence and an untouched A/B
 * stage are all rechecked. */
static inline int bq_retirement_unit_campaign_freeze(BqRetirementUnitCampaign* driver)
{
    BqRetirementCampaignBinding const* binding = driver ? driver->binding : NULL;
    TpRetirementCampaign* campaign = binding ? binding->campaign : NULL;
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_ADMITTED && campaign &&
        driver->documented == BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS &&
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

/* The post-sample record's text (BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD). */
static inline size_t bq_retirement_unit_campaign_record_text(BqRetirementUnitCampaign const* driver,
    TpRetirementCampaign const* campaign, char text[BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX])
{
    int written = snprintf(text, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_HEADER
        "job=%s\nattempt=%" PRIu64 "\nboot=%s\nplan=%s\npre-sample=%s\npre-sample-plan=%s\nexecution-plan=%s\n"
        "result-input-plan=%s\npost-aa=%s\npost-aa-binding=%s\naa-admission=%s\nfamily=%s\ntimed-rows=%s\n"
        "log-untimed=%s\nlog-aa=%s\nlog-ab=%s\nlaunches=%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\ncompleted-at=%" PRIu64
        "\npost-sample=%s\n", campaign->job, campaign->attempt, campaign->boot, driver->plan_sha256,
        driver->context_sha256, driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256,
        driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN].sha256,
        driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256, driver->post_aa_sha256,
        driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].sha256, driver->aa_admission_sha256,
        driver->family_sha256, driver->timed_rows_sha256, driver->log_chain_sha256[0], driver->log_chain_sha256[1],
        driver->log_chain_sha256[2], driver->launches[0], driver->launches[1], driver->launches[2],
        campaign->samples[1]->transcript->completed_at_ns, driver->post_context_sha256);
    size_t length = written > 0 && written < (int)BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX ? (size_t)written : 0;
    return length;
}

/* READY: every code-observed row of the gate has its code facts (untimed
 * and timed), then the collected campaign's post-sample context, written with
 * the three launch-log chains and the bound documents to the post-sample
 * record on `record` (a new, empty, service-owned stream it publishes as the
 * retained BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD), flushed and synced. The
 * result is then available for lane E's composition and the producer
 * authority handoff; MEASURED is not sent yet. */
static inline int bq_retirement_unit_campaign_ready(BqRetirementUnitCampaign* driver, FILE* record)
{
    TpRetirementCampaign const* campaign = driver && driver->binding ? driver->binding->campaign : NULL;
    char text[BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX];
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_AB && campaign && record &&
        tp_retirement_metrics_stream_empty(record) && bq_retirement_unit_campaign_codes_sealed(driver, 1) &&
        bq_retirement_unit_campaign_post_context(campaign, driver->context_sha256,
            (char const (*)[65])driver->shard_chain_sha256, (char const (*)[65])driver->log_chain_sha256,
            driver->post_context_sha256) &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns);
    size_t length = ok ? bq_retirement_unit_campaign_record_text(driver, campaign, text) : 0;
    ok = ok && length && fwrite(text, 1, length, record) == length && fflush(record) == 0 && fsync(fileno(record)) == 0;
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, text, (u64)length);
        sha256_finish_hex(&hash, driver->record.sha256);
        driver->record.bytes = length;
        driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_READY;
    }
    else
    {
        if (driver) driver->post_context_sha256[0] = 0;
        bq_retirement_unit_campaign_fail(driver);
    }
    return ok;
}

/* The caller's confirmation that lane E composed the sealed result and the
 * producer authority was handed off, both for this campaign. */
typedef struct BqRetirementUnitCampaignHandoff
{
    char const* sealed_result_sha256;
    char const* authority_sha256;
} BqRetirementUnitCampaignHandoff;

/* The measured digest: the post-sample context and the post-sample record
 * chained to the sealed result and producer authority digests the caller
 * confirmed. */
static inline int bq_retirement_unit_campaign_measured_digest(char const post_context[65], char const* record_sha256,
    char const* sealed_result_sha256, char const* authority_sha256, char digest[65])
{
    int ok = post_context && tp_retirement_digest(post_context) && tp_retirement_digest(record_sha256) &&
        tp_retirement_digest(sealed_result_sha256) && tp_retirement_digest(authority_sha256) && digest;
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_MEASURED_DOMAIN;
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_unit_campaign_text(&hash, "post-sample", post_context);
        bq_retirement_unit_campaign_text(&hash, "record", record_sha256);
        bq_retirement_unit_campaign_text(&hash, "sealed-result", sealed_result_sha256);
        bq_retirement_unit_campaign_text(&hash, "authority", authority_sha256);
        sha256_finish_hex(&hash, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

/* MEASURED, only after READY and the caller's composition and authority
 * confirmation; the two confirmed digests are recorded and chained to the
 * post-sample context and record (measured_sha256). A BQPHASE1 message
 * carries no digest (phase_channel.h); on the worker-unit's BQPHASE2 channel
 * MEASURED must carry the authority digest (bq_phase_exchange_digest_until,
 * #881 PR 3), and this digest-less send is refused there. The post-sample
 * record, sealed by lane E's retained manifest under the producer authority,
 * keeps the A/B log chain durable. A failed or incomplete campaign never
 * sends it,
 * which the supervisor requires for success. */
static inline int bq_retirement_unit_campaign_measured(BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignHandoff const* handoff)
{
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_READY && handoff &&
        bq_retirement_unit_campaign_measured_digest(driver->post_context_sha256, driver->record.sha256,
            handoff->sealed_result_sha256, handoff->authority_sha256, driver->measured_sha256) &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns) &&
        bq_phase_exchange_until(driver->phases, BQ_PHASE_MEASURED, driver->deadline_ns);
    if (ok)
    {
        memcpy(driver->sealed_result_sha256, handoff->sealed_result_sha256, 65);
        memcpy(driver->authority_sha256, handoff->authority_sha256, 65);
        driver->step = BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED;
    }
    else
    {
        if (driver) driver->measured_sha256[0] = 0;
        bq_retirement_unit_campaign_fail(driver);
    }
    return ok;
}

/* What lane E's composer (and F's replay) takes from a READY or finished
 * driver: the campaign's job, attempt and boot, its pre-sample binding and
 * A/B completion times, the frozen #619 plan, D's plan, pre-sample, post-A/A
 * and post-sample digests, the untimed record stream, the code facts of
 * every code-observed row (ascending), the per-stage log chains
 * (0 untimed, 1 A/A, 2 A/B), which the post-A/A and post-sample digests
 * bind, the five workflow documents with the partitions and digests they
 * bind, the timed-row layout digest and the post-sample record
 * (retirement_unit_handoff.h maps them onto lane E's request). After
 * MEASURED it also carries the confirmed sealed-result and authority digests
 * and the measured digest over them. The driver writes no execution receipt:
 * the composer writes the post-sample one. */
typedef struct BqRetirementUnitCampaignResult
{
    TpRetirementPlan plan;
    TpRetirementShard untimed_records;
    TpRetirementCodeRow const* codes;
    char const* job;
    char const* boot;
    uint64_t attempt, bound_at_ns, completed_at_ns, launches[3];
    unsigned code_count;
    char plan_sha256[65], context_sha256[65], post_aa_sha256[65], post_context_sha256[65];
    char log_chain_sha256[3][65];
    char sealed_result_sha256[65], authority_sha256[65], measured_sha256[65];
    /* The five workflow documents (their paths are
     * bq_retirement_unit_campaign_document_paths, below the evidence root),
     * the result-input partitions, and the family, source rows and A/A
     * admission receipt digests they bind. */
    BqRetirementUnitCampaignDocument documents[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS];
    BqRetirementUnitCampaignPartition partitions[2][BQ_RETIREMENT_UNIT_CAMPAIGN_PARTITIONS];
    unsigned partition_counts[2];
    char family_sha256[65], source_rows_sha256[65], aa_admission_sha256[65], timed_rows_sha256[65];
    BqRetirementUnitCampaignDocument record;
} BqRetirementUnitCampaignResult;

static inline int bq_retirement_unit_campaign_result(BqRetirementUnitCampaign const* driver,
    BqRetirementUnitCampaignResult* result)
{
    TpRetirementCampaign const* campaign = driver && driver->binding ? driver->binding->campaign : NULL;
    int ok = driver && result && campaign && (driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_READY ||
        driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED) &&
        campaign->phase == TP_RETIREMENT_CAMPAIGN_COLLECTED && campaign->samples[1]->transcript->finished;
    if (result)
    {
        *result = (BqRetirementUnitCampaignResult){0};
        if (ok)
        {
            result->plan = campaign->plan;
            result->untimed_records = driver->untimed_records;
            result->codes = driver->codes;
            result->code_count = driver->code_count;
            result->job = campaign->job;
            result->boot = campaign->boot;
            result->attempt = campaign->attempt;
            result->bound_at_ns = campaign->bound_at_ns;
            result->completed_at_ns = campaign->samples[1]->transcript->completed_at_ns;
            memcpy(result->launches, driver->launches, sizeof(result->launches));
            memcpy(result->plan_sha256, driver->plan_sha256, 65);
            memcpy(result->context_sha256, driver->context_sha256, 65);
            memcpy(result->post_aa_sha256, driver->post_aa_sha256, 65);
            memcpy(result->post_context_sha256, driver->post_context_sha256, 65);
            memcpy(result->log_chain_sha256, driver->log_chain_sha256, sizeof(result->log_chain_sha256));
            memcpy(result->sealed_result_sha256, driver->sealed_result_sha256, 65);
            memcpy(result->authority_sha256, driver->authority_sha256, 65);
            memcpy(result->measured_sha256, driver->measured_sha256, 65);
            memcpy(result->documents, driver->documents, sizeof(result->documents));
            memcpy(result->partitions, driver->partitions, sizeof(result->partitions));
            memcpy(result->partition_counts, driver->partition_counts, sizeof(result->partition_counts));
            memcpy(result->family_sha256, driver->family_sha256, 65);
            memcpy(result->source_rows_sha256, driver->source_rows_sha256, 65);
            memcpy(result->aa_admission_sha256, driver->aa_admission_sha256, 65);
            memcpy(result->timed_rows_sha256, driver->timed_rows_sha256, 65);
            result->record = driver->record;
        }
    }
    return ok;
}

/* The retained failure of any driver: reason NONE while nothing failed. */
static inline BqRetirementUnitCampaignFailure bq_retirement_unit_campaign_failure(BqRetirementUnitCampaign const* driver)
{
    BqRetirementUnitCampaignFailure failure = driver ? driver->failure : (BqRetirementUnitCampaignFailure){0};
    return failure;
}
#endif
#endif
