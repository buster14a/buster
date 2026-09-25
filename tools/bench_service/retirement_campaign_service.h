/* Private queue-owned #1022 -> #1022-D handoff. This entry derives both
 * durable record identities from the active job, reimports A and frozen B
 * outputs, then binds the held descriptors to the fixed campaign transcript.
 * It is an internal service seam; no worker or recipe calls it yet. */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_H
#include "retirement_prepare.h"
#include "retirement_binaries.h"
#include "retirement_campaign_binding.h"

#ifdef __linux__
static int bq_retirement_campaign_service_active(BqQueue* queue, uint64_t job_id,
    uint64_t attempt_token)
{
    BqJob const* job = queue ? bq_job(&queue->state, job_id) : NULL;
    int ok = queue && job && job_id && attempt_token && queue->directory_fd >= 3 &&
        queue->lock_fd >= 3 && queue->journal_fd >= 3 && !queue->poisoned &&
        !queue->needs_reconciliation && queue->state.active_id == job_id &&
        job->id == job_id && job->token == attempt_token && job->phase == BQ_MEASURING &&
        job->outcome == BQ_NO_OUTCOME && job->validity != BQ_INVALID &&
        !job->cancel_requested &&
        bq_request_recipe(&job->request) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED;
    return ok;
}

/* Read the immutable queue record's digest internally. The digest is only a
 * selector for acquire_pinned: that importer reconstructs the record from the
 * active job and rereads the files before and after opening the held fds. */
static BqError bq_retirement_campaign_service_binary_record_digest(BqQueue* queue,
    BqJob const* job, char digest[SHA256_HEX_CAPACITY])
{
    char name[48];
    u8 bytes[4096];
    u32 size = 0;
    if (digest) digest[0] = 0;
    BqError result = queue && job && digest && bq_record_name(name, "binaries", job->id) ?
        bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_BAD_REQUEST;
    if (result == BQ_OK && size) bq_digest(bytes, size, (char8*)digest);
    else if (result == BQ_OK) result = BQ_CORRUPT;
    return result;
}

static BqError bq_retirement_campaign_service_bind_pinned(BqQueue* queue,
    uint64_t job_id, uint64_t attempt_token, int installed, int workspaces, String8 profile,
    BqRetirementCorrectness const* gate, TpRetirementCampaign* campaign,
    TpRetirementPlan const* plan, TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands,
    TpRetirementCampaignCommand* command_workspace, size_t command_count,
    unsigned* identity_workspace, size_t identity_count,
    char const* plan_sha256, char const* context_sha256,
    BqRetirementHeldBinaries* held, BqRetirementCampaignBinding* binding)
{
    BqJob* job = queue ? bq_job(&queue->state, job_id) : NULL;
    bool binding_available = binding && !binding->campaign && !binding->held_binaries;
    bool held_started = false;
    BqRetirementPreparation prepared = {0};
    char preparation_sha256[SHA256_HEX_CAPACITY] = {0};
    char binary_record_sha256[SHA256_HEX_CAPACITY] = {0};
    BqError result = bq_retirement_campaign_service_active(queue, job_id, attempt_token) &&
        job && installed >= 3 && workspaces >= 3 && gate && campaign && plan && aa && ab &&
        held && !held->owned && binding && !binding->campaign && !binding->held_binaries ?
        BQ_OK : BQ_INVALID_TRANSITION;
    if (result == BQ_OK)
    {
        held_started = true;
        *held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
        result = bq_retirement_preparation_ready_pinned(queue, job, installed, workspaces,
            profile, preparation_sha256, &prepared);
    }
    if (result == BQ_OK)
    {
        BqRetirementPreparation imported = {0};
        result = bq_retirement_preparation_import_pinned(queue, job, installed, workspaces,
            profile, preparation_sha256, &imported);
        if (result == BQ_OK)
        {
            prepared = imported;
            int matches = !memcmp(gate->prepared.preparation_sha256, preparation_sha256,
                                  SHA256_HEX_CAPACITY);
            for (u32 side = 0; matches && side < 2; side += 1)
                matches = !memcmp(gate->prepared.source_sha256[side],
                                  prepared.subjects[side].manifest_sha256,
                                  SHA256_HEX_CAPACITY);
            if (!matches) result = BQ_SOURCE_MISMATCH;
        }
    }
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_binary_record_digest(queue, job,
            binary_record_sha256);
    if (result == BQ_OK)
        result = bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces,
            profile, preparation_sha256, binary_record_sha256, held);
    if (result == BQ_OK && !bq_retirement_campaign_service_active(queue, job_id, attempt_token))
        result = BQ_INVALID_TRANSITION;
    if (result == BQ_OK && !bq_retirement_campaign_bind_held(binding, gate, campaign, plan,
        aa, ab, held, job->id, job->token, aa_commands, ab_commands, command_workspace,
        command_count, identity_workspace, identity_count, plan_sha256, context_sha256))
        result = BQ_RECIPE_MISMATCH;
    if (result != BQ_OK)
    {
        if (binding_available) *binding = (BqRetirementCampaignBinding){0};
        if (held_started && held && held->owned) bq_retirement_binaries_release(held);
        if (campaign) tp_retirement_campaign_poison(campaign);
        if (aa) tp_retirement_samples_poison(aa);
        if (ab && ab != aa) tp_retirement_samples_poison(ab);
    }
    return result;
}

static inline BqError bq_retirement_campaign_service_bind(BqQueue* queue,
    uint64_t job_id, uint64_t attempt_token, int installed, int workspaces,
    BqRetirementCorrectness const* gate, TpRetirementCampaign* campaign,
    TpRetirementPlan const* plan, TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands,
    TpRetirementCampaignCommand* command_workspace, size_t command_count,
    unsigned* identity_workspace, size_t identity_count,
    char const* plan_sha256, char const* context_sha256,
    BqRetirementHeldBinaries* held, BqRetirementCampaignBinding* binding)
{
    BqJob const* job = queue ? bq_job(&queue->state, job_id) : NULL;
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_campaign_service_bind_pinned(queue, job_id, attempt_token,
        installed, workspaces, profile, gate, campaign, plan, aa, ab, aa_commands, ab_commands,
        command_workspace, command_count, identity_workspace, identity_count, plan_sha256,
        context_sha256, held, binding);
    return result;
}
#endif
#endif
