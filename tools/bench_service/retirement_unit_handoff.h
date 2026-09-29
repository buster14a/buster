/* #881-D to #881-E handoff: a READY (or finished) in-unit campaign driver
 * and its timed-row layout mapped onto lane E's composer request
 * (retirement_compose.h).
 *
 * Ownership: lane D. Everything D derived and bound is copied or pointed to
 * here, never re-chosen: the timed rows (bq_retirement_campaign_service_
 * timed_rows, each joined to the campaign's frozen sample layout), the
 * campaign groups' kinds, the #619 plan, the campaign identity and window,
 * the A/A stage's metrics writer tag,
 * the five workflow documents' digests and paths, the result-input
 * partitions and the code facts of every code-observed row.
 *
 * Entry point: bq_retirement_unit_handoff fills the handoff and the request
 * fields D owns. The caller still supplies what D does not own: the store,
 * the scratch root and the reviewed adapter; the retained declaration; the
 * stream paths (the service creates and publishes every stream, so it names
 * them); the #511 binding document and its digest (binding_path:
 * the binding writer's, which names D's post-A/A document); the rest of the
 * prior closure; and the sealed-record path. D's five documents are the
 * handoff's prior entries, under the validator's _all_artifacts and
 * _sealed_closure_files names, for the caller to join with its own.
 *
 * Map: BqRetirementUnitHandoff, bq_retirement_unit_handoff_names,
 * bq_retirement_unit_handoff.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_HANDOFF_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_HANDOFF_H
#include "retirement_unit_documents.h"
#include "retirement_compose.h"

#ifdef __linux__
/* The validator's closure names of D's documents, in
 * bq_retirement_unit_campaign_document_paths order. */
static char const* const bq_retirement_unit_handoff_names[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS] = {
    "workflow.records.oracle", "workflow.execution_plan", "workflow.records.result_input_plan",
    "workflow.phases.pre_sample_plan", "workflow.phases.post_aa_binding"};

/* The request's D-owned storage: the driver result it points into, the
 * layout, the partitions in E's form and D's prior-closure entries. The
 * compose rows, group kinds and code facts are caller arrays (sized by the
 * timed row count, the campaign group count and the code capacity). */
typedef struct BqRetirementUnitHandoff
{
    BqRetirementUnitCampaignResult result;
    TpRetirementComposeLayout layout;
    TpRetirementComposePartition partitions[2][BQ_RETIREMENT_UNIT_CAMPAIGN_PARTITIONS];
    TpRetirementComposeClosure prior[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS];
} BqRetirementUnitHandoff;

/* Map the driver's result. Each timed row must be the campaign's frozen
 * sample row at its position (id, group and runtime flag), with six
 * dimension values; each campaign group's kind is its frozen sample group's.
 * The request's other fields are left as they were. */
static inline int bq_retirement_unit_handoff(BqRetirementUnitCampaign const* driver, TpRetirementTimedRow const* timed,
    unsigned timed_count, TpRetirementComposeRow* rows, unsigned* group_kinds, unsigned group_capacity,
    TpRetirementComposeCode* code, unsigned code_capacity, BqRetirementUnitHandoff* handoff,
    TpRetirementComposeRequest* request)
{
    TpRetirementCampaign const* campaign = driver && driver->binding ? driver->binding->campaign : NULL;
    TpRetirementSamples const* samples = campaign ? campaign->samples[1] : NULL;
    int ok = driver && timed && rows && group_kinds && code && handoff && request && samples && driver->gate &&
        driver->documented == BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS &&
        bq_retirement_unit_campaign_result(driver, &handoff->result) && timed_count == samples->row_count &&
        samples->group_count == campaign->groups && campaign->groups <= group_capacity &&
        handoff->result.code_count <= code_capacity;
    for (unsigned index = 0; ok && index < timed_count; ++index)
    {
        TpRetirementSampleRow const* frozen = &samples->rows[index];
        unsigned runtime = (frozen->metrics & TP_RETIREMENT_SAMPLE_RUNTIME) ? 1u : 0u;
        ok = timed[index].id == frozen->id && timed[index].group == frozen->group && timed[index].runtime == runtime;
        rows[index] = (TpRetirementComposeRow){timed[index].id, timed[index].group, timed[index].runtime, {0}};
        for (unsigned dimension = 0; ok && dimension < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++dimension)
        {
            ok = timed[index].dimensions[dimension][0] != 0;
            rows[index].dimensions[dimension] = timed[index].dimensions[dimension];
        }
    }
    for (unsigned group = 0; ok && group < campaign->groups; ++group) group_kinds[group] = samples->groups[group].kind;
    for (unsigned entry = 0; ok && entry < handoff->result.code_count; ++entry)
    {
        code[entry].row = handoff->result.codes[entry].row;
        memcpy(code[entry].sides, handoff->result.codes[entry].sides, sizeof(code[entry].sides));
    }
    for (unsigned population = 0; ok && population < 2; ++population)
    {
        ok = handoff->result.partition_counts[population] &&
            handoff->result.partition_counts[population] <= BQ_RETIREMENT_UNIT_CAMPAIGN_PARTITIONS;
        for (unsigned part = 0; ok && part < handoff->result.partition_counts[population]; ++part)
        {
            BqRetirementUnitCampaignPartition const* from = &handoff->result.partitions[population][part];
            TpRetirementComposePartition* to = &handoff->partitions[population][part];
            size_t identity = strlen(from->identity), path = strlen(from->path);
            ok = identity && identity <= TP_RETIREMENT_COMPOSE_NAME_BYTES && path && path <= TP_RETIREMENT_STORE_PATH_BYTES;
            if (ok)
            {
                memcpy(to->identity, from->identity, identity + 1);
                memcpy(to->path, from->path, path + 1);
                to->start = from->start;
                to->records = from->records;
            }
        }
    }
    for (unsigned index = 0; ok && index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; ++index)
        handoff->prior[index] = (TpRetirementComposeClosure){bq_retirement_unit_handoff_names[index],
            bq_retirement_unit_campaign_document_paths[index], handoff->result.documents[index].bytes,
            handoff->result.documents[index].sha256};
    if (ok)
    {
        BqRetirementUnitCampaignResult const* result = &handoff->result;
        handoff->layout = (TpRetirementComposeLayout){rows, group_kinds, timed_count, campaign->groups,
            driver->gate->prepared.rows, driver->untimed_batch_count / 4u};
        request->layout = &handoff->layout;
        request->statistics = &result->plan;
        request->job = result->job;
        request->boot = result->boot;
        request->attempt = result->attempt;
        request->bound_at_ns = result->bound_at_ns;
        request->completed_at_ns = result->completed_at_ns;
        request->execution_plan_sha256 = result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN].sha256;
        request->source_rows_sha256 = result->source_rows_sha256;
        request->result_input_plan_sha256 = result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256;
        request->family_sha256 = result->family_sha256;
        request->post_aa_binding_sha256 = result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].sha256;
        for (unsigned population = 0; population < 2; ++population)
        {
            request->partitions[population] = handoff->partitions[population];
            request->partition_counts[population] = result->partition_counts[population];
        }
        request->code = code;
        request->code_count = result->code_count;
        /* The A/A stage's metrics writer tag (NULL when it wrote none). */
        request->aa_metrics_tag = campaign->samples[0] && campaign->samples[0]->metrics ?
            campaign->samples[0]->metrics->tag : NULL;
    }
    return ok;
}
#endif
#endif
