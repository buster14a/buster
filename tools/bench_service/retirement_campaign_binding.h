/* Private #1020 -> #1022 held-binary handoff. The upstream service must first
 * authenticate the preparation, correctness gate and held binary record; this
 * adapter joins their facts to the fixed #619 campaign before any timed child
 * starts. The gate's local seal is an integrity check, not service attestation.
 * (A1) The campaign covers the native-host timed projection. Timed object rows
 * bind through the gate's frozen plan-v3 batch groups (contract digest, batch
 * command, output digest and exit status per side); a timed object row outside
 * every frozen group fails closed. Timed link and self-host rows bind as
 * singleton groups against their per-row commands. freeze then requires the
 * reviewed campaign budget to hash to the caller's recipe-profile pin and to
 * hold the counts, each group costed by the kind and stage the gate's rows
 * give it. (M2) Object batch groups bind only when the gate carries the #509
 * correctness authority (gate->batch_authority), which only
 * bq_retirement_correctness_authorize sets (lane B's step 9 issuer, over the
 * row plan's evidence and the required checks); without it bind, bind_held
 * and the pinned service entry refuse every campaign with an object group,
 * whatever the profile pins. The recipe stays blocked.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_BINDING_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_BINDING_H
#include "queue.h"
#include <inttypes.h>
#include <sys/stat.h>
#include "retirement_correctness.h"
#include "retirement_binaries.h"
#include "../throughput/retirement_campaign.h"

#ifdef __linux__
/* Retain the seal captured at freeze; rehash the complete gate before the
 * first A/A and A/B child, rather than once for each of millions of timed
 * invocations. Only bind_held establishes a launchable binding. */
typedef struct BqRetirementCampaignBinding
{
    BqRetirementCorrectness const* gate;
    TpRetirementCampaign* campaign;
    BqRetirementHeldBinaries const* held_binaries;
    TpRetirementExecutable held_executables[2];
    uint64_t job_id, attempt_token;
    char sealed_sha256[65];
    /* Set only by the store-based unit bind (retirement_campaign_service.h):
     * the ready record digest it authenticated. The in-unit driver attaches
     * only a binding that carries it. */
    char unit_ready_sha256[65];
} BqRetirementCampaignBinding;

/* (A1) The pinned native-host timed target, x86_64-unknown-linux-gnu: the
 * gate's one-based index in the validator's TARGETS order. */
#define BQ_RETIREMENT_NATIVE_TIMED_TARGET 11u
/* BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN, the A/A second-command
 * commitment's domain, lives in retirement_correctness.h, shared with the
 * row plan (lane B) that derives the commitment. */

/* The timed projection's size, recomputed from the sealed gate rows. */
static inline uint32_t bq_retirement_campaign_timed_rows(BqRetirementCorrectness const* gate)
{
    uint32_t count = 0;
    for (uint32_t i = 0; gate && gate->trusted_rows && i < gate->prepared.rows; i += 1)
        count += gate->trusted_rows[i].compiler_eligible &&
            gate->trusted_rows[i].target == gate->prepared.native_target;
    return count;
}

/* The timed batch-group count G, recomputed from the sealed gate: one
 * singleton per timed link/self-host row plus the frozen object groups, whose
 * members must be exactly the timed object rows (UINT32_MAX otherwise). */
static uint32_t bq_retirement_campaign_timed_groups(BqRetirementCorrectness const* gate)
{
    uint32_t singletons = 0, objects = 0, members = 0;
    for (uint32_t i = 0; gate && gate->trusted_rows && i < gate->prepared.rows; i += 1)
        if (gate->trusted_rows[i].compiler_eligible && gate->trusted_rows[i].target == gate->prepared.native_target)
        {
            if (gate->trusted_rows[i].stage == BQ_RETIREMENT_STAGE_OBJECT) objects += 1;
            else singletons += 1;
        }
    for (uint32_t g = 0; gate && gate->batch_groups && g < gate->batch_group_count; g += 1)
        for (uint32_t k = 0; k < gate->batch_groups[g].contract[0].input_count; k += 1)
            members += gate->batch_groups[g].contract[0].inputs[k].member;
    uint32_t count = gate && members == objects && (!gate->batch_group_count || gate->batches_frozen) ?
        singletons + gate->batch_group_count : UINT32_MAX;
    return count;
}

/* The transcript label is the private `job-<queue id>` convention used by
 * existing execution receipt readers. This seam derives it from its numeric
 * job-id argument; its caller must supply the active, authenticated queue ID.
 * No caller-supplied label participates in this join. */
static int bq_retirement_campaign_job_label(char output[129], uint64_t job_id)
{
    /* One spelling, shared with the store's authority handoff. */
    int ok = tp_retirement_store_job_label(output, job_id);
    return ok;
}

static int bq_retirement_campaign_descriptor_identity(int descriptor, char digest[65])
{
    struct stat info = {0};
    int ok = descriptor >= 3 && digest && fstat(descriptor, &info) == 0 &&
        S_ISREG(info.st_mode) && info.st_nlink == 1 && info.st_size > 0;
    if (ok)
    {
        Sha256 identity;
        sha256_init(&identity);
        sha256_add(&identity, &info.st_dev, sizeof(info.st_dev));
        sha256_add(&identity, &info.st_ino, sizeof(info.st_ino));
        sha256_add(&identity, &info.st_size, sizeof(info.st_size));
        sha256_finish_hex(&identity, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

static int bq_retirement_campaign_held_matches(BqRetirementCampaignBinding const* binding)
{
    BqRetirementHeldBinaries const* held = binding ? binding->held_binaries : NULL;
    BqRetirementCorrectness const* gate = binding ? binding->gate : NULL;
    TpRetirementCampaign const* campaign = binding ? binding->campaign : NULL;
    TpRetirementTranscript const* aa = campaign && campaign->samples[0] ?
        campaign->samples[0]->transcript : NULL;
    TpRetirementTranscript const* ab = campaign && campaign->samples[1] ?
        campaign->samples[1]->transcript : NULL;
    char job[129];
    int label = bq_retirement_campaign_job_label(job, binding ? binding->job_id : 0);
    int ok = binding && held && gate && campaign && aa && ab && label && binding->attempt_token &&
        held->owned == 1 && held->descriptors[0] >= 3 && held->descriptors[1] >= 3 &&
        held->descriptors[0] != held->descriptors[1] &&
        memcmp(held->verified.binary_identity_sha256[0],
               held->verified.binary_identity_sha256[1], SHA256_HEX_CAPACITY) &&
        !strcmp(aa->job, job) && !strcmp(ab->job, job) &&
        aa->attempt == binding->attempt_token && ab->attempt == binding->attempt_token &&
        !strcmp(campaign->job, job) && campaign->attempt == binding->attempt_token &&
        !memcmp(binding->sealed_sha256, gate->sealed_sha256, sizeof(binding->sealed_sha256)) &&
        !memcmp(held->verified.preparation_sha256, gate->prepared.preparation_sha256, 65) &&
        !memcmp(held->verified.source_sha256[0], gate->prepared.source_sha256[0], 65) &&
        !memcmp(held->verified.source_sha256[1], gate->prepared.source_sha256[1], 65) &&
        !memcmp(held->verified.binary_sha256[0], gate->prepared.binary_sha256[0], 65) &&
        !memcmp(held->verified.binary_sha256[1], gate->prepared.binary_sha256[1], 65) &&
        campaign->binaries[0][0] == &binding->held_executables[0] &&
        campaign->binaries[0][1] == &binding->held_executables[0] &&
        campaign->binaries[1][0] == &binding->held_executables[0] &&
        campaign->binaries[1][1] == &binding->held_executables[1] &&
        binding->held_executables[0].valid && binding->held_executables[1].valid &&
        binding->held_executables[0].descriptor == held->descriptors[0] &&
        binding->held_executables[1].descriptor == held->descriptors[1] &&
        !strcmp(binding->held_executables[0].sha256, held->verified.binary_sha256[0]) &&
        !strcmp(binding->held_executables[1].sha256, held->verified.binary_sha256[1]);
    return ok;
}

static int bq_retirement_campaign_bind(BqRetirementCampaignBinding* binding,
    BqRetirementCorrectness const* gate,
    TpRetirementCampaign* campaign, TpRetirementPlan const* plan,
    TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementExecutable const* baseline, TpRetirementExecutable const* candidate,
    TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands,
    TpRetirementCampaignCommand* command_workspace, size_t command_count,
    unsigned* identity_workspace, size_t identity_count,
    TpRetirementCampaignReview const* review, char const* budget_pin_sha256,
    char const* plan_sha256, char const* context_sha256);

/* Bind service-acquired held descriptors and require both transcript streams
 * to name the supplied queue job and reservation token. A production caller
 * must reimport same-attempt A/binary records and authenticate these numeric
 * identifiers before entering this seam. */
static int bq_retirement_campaign_bind_held(BqRetirementCampaignBinding* binding,
    BqRetirementCorrectness const* gate,
    TpRetirementCampaign* campaign, TpRetirementPlan const* plan,
    TpRetirementSamples* aa, TpRetirementSamples* ab,
    BqRetirementHeldBinaries const* held, uint64_t job_id, uint64_t attempt_token,
    TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands,
    TpRetirementCampaignCommand* command_workspace, size_t command_count,
    unsigned* identity_workspace, size_t identity_count,
    TpRetirementCampaignReview const* review, char const* budget_pin_sha256,
    char const* plan_sha256, char const* context_sha256)
{
    TpRetirementTranscript const* aa_transcript = aa ? aa->transcript : NULL;
    TpRetirementTranscript const* ab_transcript = ab ? ab->transcript : NULL;
    char job[129];
    int label = bq_retirement_campaign_job_label(job, job_id);
    int ok = binding && !binding->campaign && !binding->held_binaries && gate && held && held->owned == 1 &&
        held->descriptors[0] >= 3 && held->descriptors[1] >= 3 &&
        held->descriptors[0] != held->descriptors[1] && attempt_token && label &&
        memcmp(held->verified.binary_identity_sha256[0],
               held->verified.binary_identity_sha256[1], SHA256_HEX_CAPACITY) &&
        aa_transcript && ab_transcript && !strcmp(aa_transcript->job, job) &&
        !strcmp(ab_transcript->job, job) && aa_transcript->attempt == attempt_token &&
        ab_transcript->attempt == attempt_token && bq_retirement_correctness_ready(gate) &&
        tp_retirement_digest(held->verified.preparation_sha256) &&
        tp_retirement_digest(held->verified.directory_identity_sha256) &&
        !memcmp(held->verified.preparation_sha256, gate->prepared.preparation_sha256, 65);
    for (unsigned side = 0; ok && side < 2; side += 1)
        ok = tp_retirement_digest(held->verified.source_sha256[side]) &&
            tp_retirement_digest(held->verified.binary_sha256[side]) &&
            tp_retirement_digest(held->verified.binary_identity_sha256[side]) &&
            !memcmp(held->verified.source_sha256[side], gate->prepared.source_sha256[side], 65) &&
            !memcmp(held->verified.binary_sha256[side], gate->prepared.binary_sha256[side], 65);
    if (ok)
        ok = tp_retirement_executable_init(&binding->held_executables[0], held->descriptors[0],
                    held->verified.binary_sha256[0]) &&
             tp_retirement_executable_init(&binding->held_executables[1], held->descriptors[1],
                    held->verified.binary_sha256[1]);
    for (unsigned side = 0; ok && side < 2; side += 1)
    {
        char identity[65] = {0};
        ok = bq_retirement_campaign_descriptor_identity(held->descriptors[side], identity) &&
             !strcmp(identity, held->verified.binary_identity_sha256[side]);
    }
    if (ok)
        ok = bq_retirement_campaign_bind(binding, gate, campaign, plan, aa, ab,
            &binding->held_executables[0], &binding->held_executables[1], aa_commands, ab_commands,
            command_workspace, command_count, identity_workspace, identity_count,
            review, budget_pin_sha256, plan_sha256, context_sha256);
    if (ok)
    {
        binding->held_binaries = held;
        binding->job_id = job_id;
        binding->attempt_token = attempt_token;
        ok = bq_retirement_campaign_held_matches(binding);
    }
    if (!ok)
    {
        if (campaign) tp_retirement_campaign_poison(campaign);
        if (aa) tp_retirement_samples_poison(aa);
        if (ab && ab != aa) tp_retirement_samples_poison(ab);
        if (binding && !binding->campaign) binding->held_binaries = NULL;
    }
    return ok;
}

static int bq_retirement_campaign_bind(BqRetirementCampaignBinding* binding,
    BqRetirementCorrectness const* gate,
    TpRetirementCampaign* campaign, TpRetirementPlan const* plan,
    TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementExecutable const* baseline, TpRetirementExecutable const* candidate,
    TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands,
    TpRetirementCampaignCommand* command_workspace, size_t command_count,
    unsigned* identity_workspace, size_t identity_count,
    TpRetirementCampaignReview const* review, char const* budget_pin_sha256,
    char const* plan_sha256, char const* context_sha256)
{
    TpRetirementExecution const* execution = aa && aa->transcript ? aa->transcript->execution : NULL;
    unsigned groups = execution ? execution->groups : 0, group = 0, runtime = 0, rows = 0;
    uint32_t next_batch = 0, batched_rows = 0, members_bound = 0;
    Sha256 second_hash;
    sha256_init(&second_hash);
    static char const second_domain[] = BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN;
    sha256_add(&second_hash, second_domain, sizeof(second_domain) - 1);
    int ok = binding && !binding->campaign && gate &&
        bq_retirement_correctness_ready(gate) && execution &&
        gate->prepared.native_target == BQ_RETIREMENT_NATIVE_TIMED_TARGET &&
        baseline && candidate && baseline->valid && candidate->valid &&
        !strcmp(baseline->sha256, gate->prepared.binary_sha256[0]) &&
        !strcmp(candidate->sha256, gate->prepared.binary_sha256[1]) &&
        gate->prepared.rows <= TP_RETIREMENT_MAX_CELLS &&
        aa_commands && ab_commands && aa && ab && aa->rows && ab->rows && ab->row_count == aa->row_count &&
        aa->group_count == groups && ab->group_count == groups &&
        (!gate->batch_group_count || (gate->batches_frozen && gate->batch_authority == 1)) &&
        (!aa->object_count || gate->batch_authority == 1) &&
        review && review->group_stages && review->group_count == groups;
    for (uint32_t i = 0; ok && i < gate->prepared.rows; i += 1)
    {
        BqRetirementTrustedRow const* trusted = &gate->trusted_rows[i];
        BqRetirementRowFact const* fact = &gate->facts[i];
        /* (A1) Only the native-host timed projection is timed. */
        if (!trusted->compiler_eligible || trusted->target != gate->prepared.native_target) continue;
        ok = trusted->row == i && fact->row == i && rows < aa->row_count && aa->rows[rows].id == i &&
            ab->rows[rows].id == i;
        rows += ok;
        if (ok && trusted->stage == BQ_RETIREMENT_STAGE_OBJECT)
        {
            /* A timed object row binds through its frozen batch group: the
             * first member opens the group at its dense position; later
             * members were bound with it. A row outside every frozen group
             * has no batch contract and fails closed. */
            BqRetirementBatchGroup const* frozen = next_batch < gate->batch_group_count ?
                &gate->batch_groups[next_batch] : NULL;
            uint32_t first = TP_RETIREMENT_BATCH_NO_ROW;
            for (uint32_t k = 0; frozen && k < frozen->contract[0].input_count && first == TP_RETIREMENT_BATCH_NO_ROW;
                 k += 1)
                if (frozen->contract[0].inputs[k].member) first = frozen->contract[0].inputs[k].row;
            batched_rows += 1;
            ok = aa->rows[rows - 1].metrics == 0 && ab->rows[rows - 1].metrics == 0 && !fact->runtime_eligible;
            if (!ok || first != i)
            {
                ok = ok && batched_rows <= members_bound;
                continue;
            }
            char contract_sha256[2][65] = {{0}}, output_sha256[2][65] = {{0}};
            unsigned members = 0;
            for (uint32_t k = 0; k < frozen->contract[0].input_count; k += 1) members += frozen->contract[0].inputs[k].member;
            TpRetirementSampleGroup const* layout = group < groups ? &aa->groups[group] : NULL;
            ok = layout && layout->kind == TP_RETIREMENT_GROUP_OBJECT && ab->groups[group].kind == TP_RETIREMENT_GROUP_OBJECT &&
                layout->count == members && ab->groups[group].count == members &&
                review->group_stages[group] == TP_RETIREMENT_BUDGET_STAGE_OBJECT &&
                tp_retirement_batch_contract_digest(&frozen->contract[0], contract_sha256[0]) &&
                tp_retirement_batch_contract_digest(&frozen->contract[1], contract_sha256[1]) &&
                tp_retirement_batch_contract_output(&frozen->contract[0], output_sha256[0]) &&
                tp_retirement_batch_contract_output(&frozen->contract[1], output_sha256[1]);
            for (unsigned k = 0, member = 0; ok && k < frozen->contract[0].input_count; k += 1)
                if (frozen->contract[0].inputs[k].member)
                {
                    ok = aa->rows[aa->members[layout->first + member]].id == frozen->contract[0].inputs[k].row &&
                        ab->rows[ab->members[ab->groups[group].first + member]].id == frozen->contract[0].inputs[k].row;
                    member += 1;
                }
            for (unsigned stage = 0; ok && stage < 2; stage += 1)
                for (unsigned variant = 0; ok && variant < 2; variant += 1)
                {
                    TpRetirementMeasuredCommand const* command = (stage ? ab_commands : aa_commands) +
                        (size_t)group * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + variant;
                    unsigned side = stage ? variant : 0;
                    char digest[65] = {0};
                    /* The second A/A label runs the baseline with the same
                     * frozen contract (object leaves are cwd/basename.o); its
                     * distinct command is bound by the sealed aggregate. */
                    ok = command->unit == group && command->kind == 0 && command->variant == variant &&
                        !command->artifact && command->batch &&
                        tp_retirement_batch_contract_digest(command->batch, digest) &&
                        !strcmp(digest, contract_sha256[side]) &&
                        command->exit_status == (int)frozen->contract[side].exit_status &&
                        command->command_sha256 && command->output_sha256 &&
                        !strcmp(command->output_sha256, output_sha256[side]) &&
                        (!stage && variant ? tp_retirement_digest(command->command_sha256) :
                         !strcmp(command->command_sha256, frozen->command_sha256[side]));
                }
            if (ok)
            {
                uint8_t ordinal[4] = {(uint8_t)i, (uint8_t)(i >> 8), (uint8_t)(i >> 16), (uint8_t)(i >> 24)};
                uint8_t applicable_runtime = 0;
                sha256_add(&second_hash, ordinal, sizeof(ordinal));
                sha256_add(&second_hash,
                    aa_commands[(size_t)group * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + 1].command_sha256, 64);
                sha256_add(&second_hash, &applicable_runtime, sizeof(applicable_runtime));
                members_bound += members;
                next_batch += 1;
                group += 1;
            }
            continue;
        }
        unsigned metrics = fact->runtime_eligible ? TP_RETIREMENT_SAMPLE_RUNTIME : 0;
        unsigned runtime_slot = groups + runtime;
        ok = ok && group < groups && aa->rows[rows - 1].metrics == metrics && ab->rows[rows - 1].metrics == metrics &&
            aa->groups[group].kind == TP_RETIREMENT_GROUP_SINGLETON &&
            ab->groups[group].kind == TP_RETIREMENT_GROUP_SINGLETON &&
            aa->rows[aa->members[aa->groups[group].first]].id == i &&
            /* (M1) A singleton is costed by its row's own stage. */
            review->group_stages[group] == (trusted->stage == BQ_RETIREMENT_STAGE_LINK ?
                TP_RETIREMENT_BUDGET_STAGE_LINK : trusted->stage == BQ_RETIREMENT_STAGE_SELF_HOST ?
                TP_RETIREMENT_BUDGET_STAGE_SELF_HOST : TP_RETIREMENT_BUDGET_STAGE_COUNT);
        if (ok && fact->runtime_eligible)
            ok = runtime < execution->runtime_count && execution->runtime_rows[runtime] == i;
        for (unsigned stage = 0; ok && stage < 2; stage += 1)
            for (unsigned kind = 0; ok && kind < 2; kind += 1)
                for (unsigned variant = 0; ok && variant < 2; variant += 1)
                {
                    if (kind && !fact->runtime_eligible) continue;
                    TpRetirementMeasuredCommand const* command = (stage ? ab_commands : aa_commands) +
                        (size_t)(kind ? runtime_slot : group) * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + variant;
                    BqRetirementObservedSide const* side = &fact->side[stage ? variant : 0];
                    char artifact_output[65] = {0};
                    char const* objects[1] = {side->artifact_sha256};
                    ok = kind || tp_retirement_batch_output_digest(objects, 1, artifact_output);
                    /* The second A/A label can use a different output path.
                     * Bind its exact hash through the separately authenticated
                     * aggregate of baseline-label-2 command identities. A
                     * singleton group's output digest covers its one artifact. */
                    ok = ok && command->unit == (kind ? i : group) && command->kind == kind &&
                        command->variant == variant && !command->batch && !command->exit_status &&
                        command->command_sha256 && command->output_sha256 &&
                        (!stage && variant ? tp_retirement_digest(command->command_sha256) :
                         !strcmp(command->command_sha256,
                            kind ? side->runtime_command_sha256 : side->compiler_command_sha256)) &&
                        !strcmp(command->output_sha256, kind ? trusted->independent_oracle_sha256 :
                                                             artifact_output);
                }
        if (ok)
        {
            uint8_t ordinal[4] = {(uint8_t)i, (uint8_t)(i >> 8),
                (uint8_t)(i >> 16), (uint8_t)(i >> 24)};
            uint8_t applicable_runtime = fact->runtime_eligible ? 1 : 0;
            sha256_add(&second_hash, ordinal, sizeof(ordinal));
            sha256_add(&second_hash, aa_commands[(size_t)group * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + 1].command_sha256, 64);
            sha256_add(&second_hash, &applicable_runtime, sizeof(applicable_runtime));
            if (applicable_runtime)
                sha256_add(&second_hash,
                    aa_commands[(size_t)runtime_slot * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + 1].command_sha256, 64);
            runtime += applicable_runtime;
        }
        group += 1;
    }
    char second_digest[65] = {0};
    if (ok)
    {
        sha256_finish_hex(&second_hash, second_digest);
        ok = !strcmp(second_digest, gate->prepared.aa_second_commands_sha256);
    }
    if (ok) ok = group == groups && rows == aa->row_count && runtime == execution->runtime_count &&
                  next_batch == gate->batch_group_count && batched_rows == members_bound &&
                  (uint32_t)aa->object_count == gate->batch_group_count &&
                  bq_retirement_correctness_ready(gate);
    if (ok)
        ok = tp_retirement_campaign_freeze(campaign, plan, aa, ab, baseline, baseline, candidate,
            aa_commands, ab_commands, command_workspace, command_count, identity_workspace,
            identity_count, gate->prepared.rows, review, budget_pin_sha256, plan_sha256, context_sha256);
    if (ok)
    {
        binding->gate = gate;
        binding->campaign = campaign;
        memcpy(binding->sealed_sha256, gate->sealed_sha256, sizeof(binding->sealed_sha256));
    }
    if (!ok)
    {
        if (campaign) tp_retirement_campaign_poison(campaign);
        if (aa) tp_retirement_samples_poison(aa);
        if (ab && ab != aa) tp_retirement_samples_poison(ab);
    }
    return ok;
}

/* A changed preflight fact or freshly poisoned gate must stop the actual first
 * launch, even when the #619 plan was frozen earlier. The first invocation of
 * A/B is checked again after the independently admitted A/A interval. */
static int bq_retirement_campaign_run(BqRetirementCampaignBinding const* binding,
    TpRetirementMeasuredCommand const* command, TpProcessInputs const* inputs,
    int output_directory, TpRetirementMeasurementResult* result)
{
    TpRetirementCampaign* campaign = binding ? binding->campaign : NULL;
    BqRetirementCorrectness const* gate = binding ? binding->gate : NULL;
    unsigned stage = campaign && campaign->phase == TP_RETIREMENT_CAMPAIGN_AB ? 1 : 0;
    TpRetirementSamples* samples = campaign ? campaign->samples[stage] : NULL;
    TpRetirementExecution const* execution = samples && samples->transcript ?
        samples->transcript->execution : NULL;
    int ok = binding && campaign && gate && execution && binding->held_binaries &&
        gate->finished && !gate->failed &&
        !memcmp(binding->sealed_sha256, gate->sealed_sha256, sizeof(binding->sealed_sha256)) &&
        campaign->population_rows == gate->prepared.rows &&
        gate->prepared.native_target == BQ_RETIREMENT_NATIVE_TIMED_TARGET &&
        campaign->groups == bq_retirement_campaign_timed_groups(gate);
    if (ok) ok = bq_retirement_campaign_held_matches(binding);
    if (ok && !execution->sequence)
        ok = bq_retirement_correctness_ready(gate) &&
            !strcmp(campaign->binary_sha256[0][0], gate->prepared.binary_sha256[0]) &&
            !strcmp(campaign->binary_sha256[0][1], gate->prepared.binary_sha256[0]) &&
            !strcmp(campaign->binary_sha256[1][0], gate->prepared.binary_sha256[0]) &&
            !strcmp(campaign->binary_sha256[1][1], gate->prepared.binary_sha256[1]);
    if (ok) ok = tp_retirement_campaign_run(campaign, command, inputs, output_directory, result);
    else
    {
        if (result) *result = (TpRetirementMeasurementResult){
            .status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID, .process = {.exit_code = -1}};
        if (campaign) tp_retirement_campaign_poison(campaign);
    }
    return ok;
}
#endif
#endif
