/* Private #1020 -> #1022 handoff. The service has already authenticated and
 * completed the preparation/correctness gate; this adapter joins its exact
 * canonical facts to the fixed #619 campaign before any timed child starts.
 * The gate's local seal is an integrity check, not service attestation.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_BINDING_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_BINDING_H
#include "retirement_correctness.h"
#include "../throughput/retirement_campaign.h"

#ifdef __linux__
static int bq_retirement_campaign_bind(BqRetirementCorrectness const* gate,
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
    int ok = gate && bq_retirement_correctness_ready(gate) && execution &&
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
                    /* The second A/A label may use a different frozen path.
                     * #1020 supplies one correctness command for the baseline;
                     * the service must authenticate the full #619 plan too. */
                    ok = command->row == i && command->kind == kind && command->variant == variant &&
                        command->command_sha256 && command->output_sha256 &&
                        (!stage && variant ? 1 : !strcmp(command->command_sha256,
                            kind ? side->runtime_command_sha256 : side->compiler_command_sha256)) &&
                        !strcmp(command->output_sha256, kind ? trusted->independent_oracle_sha256 :
                                                             side->artifact_sha256);
                    if (ok && !kind)
                        ok = command->code_section_bytes == side->code_bytes &&
                            (trusted->code_obligation ? command->code_section_sha256 &&
                                !strcmp(command->code_section_sha256, side->code_sha256) :
                                !command->code_section_sha256);
                }
        dense += 1;
    }
    if (ok) ok = dense == execution->rows && runtime == execution->runtime_count &&
                  bq_retirement_correctness_ready(gate);
    if (ok)
        ok = tp_retirement_campaign_freeze(campaign, plan, aa, ab, baseline, baseline, candidate,
            aa_commands, ab_commands, command_workspace, command_count, identity_workspace,
            identity_count, gate->prepared.rows, plan_sha256, context_sha256);
    if (!ok)
    {
        if (campaign) tp_retirement_campaign_poison(campaign);
        if (aa) tp_retirement_samples_poison(aa);
        if (ab && ab != aa) tp_retirement_samples_poison(ab);
    }
    return ok;
}
#endif
#endif
