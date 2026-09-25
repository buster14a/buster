/* Private #1020 -> #1022 held-binary handoff. The upstream service must first
 * authenticate the preparation, correctness gate and held binary record; this
 * adapter joins their facts to the fixed #619 campaign before any timed child
 * starts. The gate's local seal is an integrity check, not service attestation.
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
} BqRetirementCampaignBinding;

/* The transcript label is the private `job-<queue id>` convention used by
 * existing execution receipt readers. This seam derives it from its numeric
 * job-id argument; its caller must supply the active, authenticated queue ID.
 * No caller-supplied label participates in this join. */
static int bq_retirement_campaign_job_label(char output[129], uint64_t job_id)
{
    int length = output && job_id ? snprintf(output, 129, "job-%" PRIu64, job_id) : -1;
    int ok = length > 0 && length < 129;
    if (!ok && output) output[0] = 0;
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
            plan_sha256, context_sha256);
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
    char const* plan_sha256, char const* context_sha256)
{
    TpRetirementExecution const* execution = aa && aa->transcript ? aa->transcript->execution : NULL;
    unsigned dense = 0, runtime = 0;
    Sha256 second_hash;
    sha256_init(&second_hash);
    static char const second_domain[] = "bq-retirement-aa-second-commands-v1";
    sha256_add(&second_hash, second_domain, sizeof(second_domain) - 1);
    int ok = binding && !binding->campaign && gate &&
        bq_retirement_correctness_ready(gate) && execution &&
        baseline && candidate && baseline->valid && candidate->valid &&
        !strcmp(baseline->sha256, gate->prepared.binary_sha256[0]) &&
        !strcmp(candidate->sha256, gate->prepared.binary_sha256[1]) &&
        execution->rows == gate->eligible_rows &&
        gate->prepared.rows <= TP_RETIREMENT_MAX_CELLS &&
        aa_commands && ab_commands && aa && ab && aa->rows && ab->rows;
    for (uint32_t i = 0; ok && i < gate->prepared.rows; i += 1)
    {
        BqRetirementTrustedRow const* trusted = &gate->trusted_rows[i];
        BqRetirementRowFact const* fact = &gate->facts[i];
        if (!trusted->compiler_eligible) continue;
        unsigned metrics = trusted->code_obligation ?
            (fact->side[0].code_bytes ? TP_RETIREMENT_SAMPLE_CODE : TP_RETIREMENT_SAMPLE_ZERO_BASELINE_CODE) : 0;
        if (fact->runtime_eligible) metrics |= TP_RETIREMENT_SAMPLE_RUNTIME;
        ok = dense < execution->rows && trusted->row == i && fact->row == i &&
            (execution->row_ids ? execution->row_ids[dense] : dense) == i &&
            aa->rows[dense].metrics == metrics && ab->rows[dense].metrics == metrics;
        if (ok && fact->runtime_eligible)
        {
            ok = runtime < execution->runtime_count && execution->runtime_rows[runtime] == dense;
            runtime += 1;
        }
        for (unsigned stage = 0; ok && stage < 2; stage += 1)
            for (unsigned kind = 0; ok && kind < 2; kind += 1)
                for (unsigned variant = 0; ok && variant < 2; variant += 1)
                {
                    TpRetirementMeasuredCommand const* command =
                        (stage ? ab_commands : aa_commands) + dense * 4 + kind * 2 + variant;
                    BqRetirementObservedSide const* side = &fact->side[stage ? variant : 0];
                    if (kind && !fact->runtime_eligible) continue;
                    /* The second A/A label can use a different output path.
                     * Bind its exact hash through the separately authenticated
                     * aggregate of baseline-label-2 command identities. */
                    ok = command->row == i && command->kind == kind && command->variant == variant &&
                        command->command_sha256 && command->output_sha256 &&
                        (!stage && variant ? tp_retirement_digest(command->command_sha256) :
                         !strcmp(command->command_sha256,
                            kind ? side->runtime_command_sha256 : side->compiler_command_sha256)) &&
                        !strcmp(command->output_sha256, kind ? trusted->independent_oracle_sha256 :
                                                             side->artifact_sha256);
                    if (ok && !kind)
                        ok = command->code_section_bytes == side->code_bytes &&
                            (trusted->code_obligation ? command->code_section_sha256 &&
                                !strcmp(command->code_section_sha256, side->code_sha256) :
                                !command->code_section_sha256);
                }
        if (ok)
        {
            uint8_t ordinal[4] = {(uint8_t)i, (uint8_t)(i >> 8),
                (uint8_t)(i >> 16), (uint8_t)(i >> 24)};
            uint8_t applicable_runtime = fact->runtime_eligible ? 1 : 0;
            sha256_add(&second_hash, ordinal, sizeof(ordinal));
            sha256_add(&second_hash, aa_commands[dense * 4 + 1].command_sha256, 64);
            sha256_add(&second_hash, &applicable_runtime, sizeof(applicable_runtime));
            if (applicable_runtime)
                sha256_add(&second_hash, aa_commands[dense * 4 + 3].command_sha256, 64);
        }
        dense += 1;
    }
    char second_digest[65] = {0};
    if (ok)
    {
        sha256_finish_hex(&second_hash, second_digest);
        ok = !strcmp(second_digest, gate->prepared.aa_second_commands_sha256);
    }
    if (ok) ok = dense == execution->rows && runtime == execution->runtime_count &&
                  bq_retirement_correctness_ready(gate);
    if (ok)
        ok = tp_retirement_campaign_freeze(campaign, plan, aa, ab, baseline, baseline, candidate,
            aa_commands, ab_commands, command_workspace, command_count, identity_workspace,
            identity_count, gate->prepared.rows, plan_sha256, context_sha256);
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
        campaign->rows == gate->eligible_rows;
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
