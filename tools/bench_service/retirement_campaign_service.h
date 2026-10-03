/* Private #1022-D campaign bind seams: the queue-owned entry and the in-unit,
 * store-based entry share one verification tail.
 *
 * Queue entry (bq_retirement_campaign_service_bind_pinned): derives both
 * durable record identities from the active job, reimports A and frozen B
 * outputs from the queue, then binds the held descriptors to the fixed
 * campaign transcript. It requires the queue's active MEASURING attempt.
 *
 * Store entries: the worker unit never holds the queue or the lease, so it
 * authenticates from the per-attempt record store (the coordinator's sealed A
 * export, the unit's sealed retirement-build/ records and the sealed
 * retirement-ready/ record lane B's bq_retirement_unit_ready wrote) and the
 * private phase channel, in two calls:
 *   bq_retirement_campaign_service_import_unit(_pinned)  under SETTLING, the
 *       heavy part: A re-imported, the ready record imported after the
 *       coordinator's own replay (bq_retirement_unit_replay_pinned) re-derives
 *       every field from the store (bq_retirement_campaign_ready_import), the
 *       unit gate's A and sources matched, lane B's issued unit gate joined to
 *       the record (bq_retirement_campaign_ready_unit_gate: its seal is the
 *       record's `gate=`, its correctness seal the record's `correctness=`)
 *       both binaries held again from the record's digests
 *       (bq_retirement_campaign_ready_held) and A's two roots held for the
 *       launches' canonical layout (ready->sources);
 *   bq_retirement_campaign_service_bind_unit(_pinned)    under MEASURING, the
 *       cheap part: the record still stands with its imported bytes, the gate
 *       join and held pair recheck, the plan and pre-sample context are
 *       derived here (retirement_unit_campaign.h) and any differing caller
 *       value refused, then the shared tail binds and marks the binding for
 *       the driver's attach.
 * bq_retirement_campaign_service_timed_rows gives lane E's composer each
 * timed row's group, runtime flag and six dimension values from the pinned
 * performance rows, joined to the gate's sealed row identities, and
 * bq_retirement_documents_population parses those rows for it and for the
 * driver's workflow documents (retirement_unit_documents.h); the census
 * parser they reuse (retirement_correctness_service.c) must also precede
 * this header. retirement_unit_handoff.h maps the READY driver onto lane E's
 * composer request. bq_retirement_campaign_plan_commands resolves the
 * campaign's measured commands from lane B's row plan in the canonical child
 * layout with the plan's own (static) resolver, so this header must also
 * follow retirement_row_plan.c; every label-1 command must hash to the
 * digest the unit gate sealed for it.
 *
 * Shared tail: bq_retirement_campaign_service_gate_matches (A and both
 * source manifests against the gate), bq_retirement_campaign_service_bind_verified
 * (the recipe profile's `campaign-budget-sha256=` pin, then
 * bq_retirement_campaign_bind_held), bq_retirement_campaign_service_refuse.
 *
 * (M4) The blocked profile carries no budget or campaign pins, so every entry
 * fails closed with BQ_RECIPE_MISMATCH until integration pins them. (M2) A pin
 * alone never admits object batch groups: they also need the gate's #509
 * authority. The store entries call retirement_unit.c's private (static)
 * helpers, so this header must be included after that file in the same
 * translation unit: after main.c, as the preparation test runner does. Every
 * function here is static inline so an including unit that uses only some of
 * them builds warning-free. No worker or recipe calls any entry yet.
 *
 * Map: BqRetirementCampaignRequest, BqRetirementCampaignUnitStore,
 * bq_retirement_campaign_service_active, bq_retirement_campaign_service_phase,
 * bq_retirement_campaign_ready_standing, bq_retirement_campaign_ready_holds.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_H
#include "retirement_prepare.h"
#include "retirement_binaries.h"
#include "retirement_campaign_binding.h"
#include "retirement_unit.h"
#include "retirement_unit_campaign.h"
#include "retirement_unit_handoff.h"
#include "retirement_row_plan.h"

#ifdef __linux__
/* Everything the campaign freeze consumes besides the held binaries. */
typedef struct BqRetirementCampaignRequest
{
    BqRetirementCorrectness const* gate;
    TpRetirementCampaign* campaign;
    TpRetirementPlan const* plan;
    TpRetirementSamples* aa;
    TpRetirementSamples* ab;
    TpRetirementMeasuredCommand const* aa_commands;
    TpRetirementMeasuredCommand const* ab_commands;
    TpRetirementCampaignCommand* command_workspace;
    size_t command_count;
    unsigned* identity_workspace;
    size_t identity_count;
    TpRetirementCampaignReview const* review;
    char const* plan_sha256;
    char const* context_sha256;
    /* The store bind only: lane B's row plan the unit gate was issued on.
     * Every command must be the one it resolves (the queue entry leaves it
     * NULL). */
    BqRetirementRowPlan const* row_plan;
} BqRetirementCampaignRequest;

static inline int bq_retirement_campaign_service_active(BqQueue* queue, uint64_t job_id,
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
static inline BqError bq_retirement_campaign_service_binary_record_digest(BqQueue* queue,
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

static inline int bq_retirement_campaign_service_request_ready(BqRetirementCampaignRequest const* request)
{
    int ok = request && request->gate && request->campaign && request->plan && request->aa && request->ab;
    return ok;
}

/* Shared: the authenticated A digest and both source manifests must be the
 * ones the ready correctness gate sealed. */
static inline BqError bq_retirement_campaign_service_gate_matches(BqRetirementCorrectness const* gate,
    char const preparation_sha256[SHA256_HEX_CAPACITY], BqRetirementPreparation const* prepared)
{
    int matches = gate && prepared && preparation_sha256 &&
        !memcmp(gate->prepared.preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY);
    for (u32 side = 0; matches && side < 2; side += 1)
        matches = !memcmp(gate->prepared.source_sha256[side], prepared->subjects[side].manifest_sha256,
                          SHA256_HEX_CAPACITY);
    BqError result = matches ? BQ_OK : BQ_SOURCE_MISMATCH;
    return result;
}

/* Shared: the compiled profile's reviewed-budget pin, then the held bind. */
static inline BqError bq_retirement_campaign_service_bind_verified(BqRetirementCampaignRequest const* request,
    String8 profile, uint64_t job_id, uint64_t attempt_token, BqRetirementHeldBinaries const* held,
    BqRetirementCampaignBinding* binding)
{
    char budget_pin[SHA256_HEX_CAPACITY] = {0};
    BqError result = bq_retirement_campaign_service_request_ready(request) &&
        bq_retirement_profile_sha(profile, S8("campaign-budget-sha256="), budget_pin) ? BQ_OK : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_campaign_bind_held(binding, request->gate, request->campaign,
        request->plan, request->aa, request->ab, held, job_id, attempt_token, request->aa_commands,
        request->ab_commands, request->command_workspace, request->command_count, request->identity_workspace,
        request->identity_count, request->review, budget_pin, request->plan_sha256, request->context_sha256))
        result = BQ_RECIPE_MISMATCH;
    return result;
}

/* Shared: a refused bind leaves no binding, no held descriptor and both
 * stages poisoned. */
static inline void bq_retirement_campaign_service_refuse(BqRetirementCampaignRequest const* request,
    BqRetirementHeldBinaries* held, bool held_started, BqRetirementCampaignBinding* binding, bool binding_available)
{
    if (binding_available) *binding = (BqRetirementCampaignBinding){0};
    if (held_started && held && held->owned) bq_retirement_binaries_release(held);
    if (request && request->campaign) tp_retirement_campaign_poison(request->campaign);
    if (request && request->aa) tp_retirement_samples_poison(request->aa);
    if (request && request->ab && request->ab != request->aa) tp_retirement_samples_poison(request->ab);
}

static inline BqError bq_retirement_campaign_service_bind_pinned(BqQueue* queue,
    uint64_t job_id, uint64_t attempt_token, int installed, int workspaces, String8 profile,
    BqRetirementCorrectness const* gate, TpRetirementCampaign* campaign,
    TpRetirementPlan const* plan, TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands,
    TpRetirementCampaignCommand* command_workspace, size_t command_count,
    unsigned* identity_workspace, size_t identity_count, TpRetirementCampaignReview const* review,
    char const* plan_sha256, char const* context_sha256,
    BqRetirementHeldBinaries* held, BqRetirementCampaignBinding* binding)
{
    BqRetirementCampaignRequest request = {gate, campaign, plan, aa, ab, aa_commands, ab_commands,
        command_workspace, command_count, identity_workspace, identity_count, review, plan_sha256, context_sha256,
        NULL};
    BqJob* job = queue ? bq_job(&queue->state, job_id) : NULL;
    bool binding_available = binding && !binding->campaign && !binding->held_binaries;
    bool held_started = false;
    BqRetirementPreparation prepared = {0};
    char preparation_sha256[SHA256_HEX_CAPACITY] = {0};
    char binary_record_sha256[SHA256_HEX_CAPACITY] = {0};
    BqError result = bq_retirement_campaign_service_active(queue, job_id, attempt_token) &&
        job && installed >= 3 && workspaces >= 3 && bq_retirement_campaign_service_request_ready(&request) &&
        held && !held->owned && binding && !binding->campaign && !binding->held_binaries ?
        BQ_OK : BQ_INVALID_TRANSITION;
    if (result == BQ_OK)
    {
        held_started = true;
        *held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
        result = bq_retirement_preparation_ready_pinned(bq_retirement_queue_store(queue), job, installed,
            workspaces, profile, preparation_sha256, &prepared);
    }
    if (result == BQ_OK)
    {
        BqRetirementPreparation imported = {0};
        result = bq_retirement_preparation_import_pinned(bq_retirement_queue_store(queue), job, installed,
            workspaces, profile, preparation_sha256, &imported);
        if (result == BQ_OK)
        {
            prepared = imported;
            result = bq_retirement_campaign_service_gate_matches(gate, preparation_sha256, &prepared);
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
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_bind_verified(&request, profile, job->id, job->token, held, binding);
    if (result != BQ_OK)
        bq_retirement_campaign_service_refuse(&request, held, held_started, binding, binding_available);
    return result;
}

static inline BqError bq_retirement_campaign_service_bind(BqQueue* queue,
    uint64_t job_id, uint64_t attempt_token, int installed, int workspaces,
    BqRetirementCorrectness const* gate, TpRetirementCampaign* campaign,
    TpRetirementPlan const* plan, TpRetirementSamples* aa, TpRetirementSamples* ab,
    TpRetirementMeasuredCommand const* aa_commands,
    TpRetirementMeasuredCommand const* ab_commands,
    TpRetirementCampaignCommand* command_workspace, size_t command_count,
    unsigned* identity_workspace, size_t identity_count, TpRetirementCampaignReview const* review,
    char const* plan_sha256, char const* context_sha256,
    BqRetirementHeldBinaries* held, BqRetirementCampaignBinding* binding)
{
    BqJob const* job = queue ? bq_job(&queue->state, job_id) : NULL;
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_campaign_service_bind_pinned(queue, job_id, attempt_token,
        installed, workspaces, profile, gate, campaign, plan, aa, ab, aa_commands, ab_commands,
        command_workspace, command_count, identity_workspace, identity_count, review, plan_sha256,
        context_sha256, held, binding);
    return result;
}

/* The per-attempt record store and the fixed seams the unit's B steps use:
 * the production wrapper passes the compiled profile, the broker's fixed
 * workspace root, the full-census report and the installed driver, toolchain
 * and broker paths. Borrowed. */
typedef struct BqRetirementCampaignUnitStore
{
    BqRetirementStore store;
    int workspaces, installed;
    String8 workspace_root, profile, census_profile;
    char const* driver;
    char const* toolchain_root;
    char const* broker;
    char const* broker_workspaces;
} BqRetirementCampaignUnitStore;

/* The canonical decimal after "\n<key>", ending at its line feed. */
static inline int bq_retirement_campaign_ready_decimal(char const* text, char const* key, u64* value)
{
    char pattern[48];
    int length = snprintf(pattern, sizeof(pattern), "\n%s", key);
    char const* found = length > 0 && (size_t)length < sizeof(pattern) ? strstr(text, pattern) : NULL;
    char const* start = found ? found + length : NULL;
    char const* end = start ? strchr(start, '\n') : NULL;
    int ok = end && bq_retirement_unit_campaign_decimal((String8){(char8*)start, (u64)(end - start)}, value);
    if (!ok && value) *value = 0;
    return ok;
}

/* Import the ready record for this attempt. The coordinator's replay first
 * re-derives every field from the store (A, toolchain, reference policy,
 * matched builds, binaries, census projection, every reference-oracle/ file,
 * each runtime command from its recorded descriptor numbers, the oracle
 * attempt and the gate seal) and requires the stored bytes to be exactly the
 * record those facts format. The record is then read again by its content
 * address from the sealed directory and its fields parsed; job and attempt
 * must be this attempt's. prepared is this attempt's own store re-import. */
static inline BqError bq_retirement_campaign_ready_import(BqRetirementCampaignUnitStore const* unit,
    BqRetirementUnitPrepared const* prepared, char const ready_sha256[SHA256_HEX_CAPACITY],
    BqRetirementCampaignReady* ready)
{
    bool fresh = ready && !ready->owned;
    if (fresh) *ready = (BqRetirementCampaignReady){0};
    char name[80];
    int named = bq_retirement_unit_hex(ready_sha256) ? snprintf(name, sizeof(name), "ready-%s", ready_sha256) : -1;
    BqError result = fresh && unit && prepared && prepared->owned && named > 0 && (size_t)named < sizeof(name) ?
        BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK)
        result = bq_retirement_unit_replay_pinned(unit->store, unit->workspaces, unit->installed, prepared->job.id,
            prepared->job.token, unit->workspace_root, unit->profile, unit->census_profile, unit->driver,
            unit->toolchain_root, unit->broker, unit->broker_workspaces, prepared->preparation_sha256, ready_sha256);
    int directory = result == BQ_OK ? bq_retirement_unit_attempt_open(unit->workspaces, &prepared->job, NULL,
                                                                      BQ_RETIREMENT_UNIT_READY_DIRECTORY) : -1;
    u32 capacity = result == BQ_OK ? BQ_RETIREMENT_UNIT_READY_HEADER_CAP +
        BQ_RETIREMENT_UNIT_READY_ROW_CAP * prepared->policy.template.reference_count : 0;
    char* text = result == BQ_OK ? malloc((size_t)capacity + 1u) : NULL;
    if (result == BQ_OK && !text) result = BQ_IO;
    if (result == BQ_OK && !(directory >= 0 && bq_retirement_unit_closed(directory, NULL, name, true)))
        result = BQ_WORKSPACE_MISMATCH;
    u32 length = 0;
    if (result == BQ_OK) result = bq_record_read_at(directory, name, (u8*)text, capacity, &length);
    char digest[SHA256_HEX_CAPACITY] = {0};
    u64 numbers[6] = {0};
    if (result == BQ_OK)
    {
        text[length] = 0;
        bq_digest(text, length, (char8*)digest);
        BqRetirementCampaignReady* out = ready;
        result = !memcmp(digest, ready_sha256, SHA256_HEX_CAPACITY) && strlen(text) == length &&
            !strncmp(text, "BQ-RETIREMENT-READY-V1\n", 23) &&
            bq_retirement_campaign_ready_decimal(text, "job=", &numbers[0]) &&
            bq_retirement_campaign_ready_decimal(text, "attempt=", &numbers[1]) &&
            bq_retirement_campaign_ready_decimal(text, "rows=", &numbers[2]) &&
            bq_retirement_campaign_ready_decimal(text, "object-rows=", &numbers[3]) &&
            bq_retirement_campaign_ready_decimal(text, "native-target=", &numbers[4]) &&
            bq_retirement_campaign_ready_decimal(text, "observed-rows=", &numbers[5]) &&
            numbers[0] == prepared->job.id && numbers[1] == prepared->job.token && numbers[2] <= UINT32_MAX &&
            numbers[3] <= numbers[2] && numbers[4] <= UINT32_MAX && numbers[5] <= numbers[2] &&
            bq_retirement_unit_ready_value(text, "preparation=", out->preparation_sha256) &&
            bq_retirement_unit_ready_value(text, "binaries=", out->binary_record_sha256) &&
            bq_retirement_unit_ready_value(text, "matched-builds=", out->build_record_sha256) &&
            bq_retirement_unit_ready_value(text, "binary-base=", out->binary_sha256[0]) &&
            bq_retirement_unit_ready_value(text, "binary-candidate=", out->binary_sha256[1]) &&
            bq_retirement_unit_ready_value(text, "support=", out->support_sha256) &&
            bq_retirement_unit_ready_value(text, "census=", out->census_sha256) &&
            bq_retirement_unit_ready_value(text, "population=", out->population_sha256) &&
            bq_retirement_unit_ready_value(text, "template=", out->template_sha256) &&
            bq_retirement_unit_ready_value(text, "inventory=", out->inventory_sha256) &&
            bq_retirement_unit_ready_value(text, "oracle-attempt=", out->oracle_attempt_sha256) &&
            bq_retirement_unit_ready_value(text, "checks-authority=", out->checks_authority_sha256) &&
            bq_retirement_unit_ready_value(text, "check-evidence=", out->check_evidence_sha256) &&
            bq_retirement_unit_ready_value(text, "correctness=", out->correctness_sha256) &&
            bq_retirement_unit_ready_value(text, "gate=admitted ", out->gate_sha256) &&
            !strcmp(out->preparation_sha256, prepared->preparation_sha256) &&
            !strcmp(out->template_sha256, prepared->policy.template_sha256) &&
            !strcmp(out->inventory_sha256, prepared->policy.inventory_sha256) ? BQ_OK : BQ_CORRUPT;
    }
    if (directory >= 0 && close(directory) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
    {
        ready->job = prepared->job;
        ready->text = text;
        ready->length = length;
        ready->job_id = numbers[0];
        ready->attempt_token = numbers[1];
        ready->rows = (u32)numbers[2];
        ready->object_rows = (u32)numbers[3];
        ready->native_target = (u32)numbers[4];
        ready->observed_rows = (u32)numbers[5];
        memcpy(ready->ready_sha256, ready_sha256, SHA256_HEX_CAPACITY);
        ready->owned = 1;
    }
    else
    {
        free(text);
        if (fresh) *ready = (BqRetirementCampaignReady){0};
    }
    return result;
}

/* One "observed=" line: its index, row and reference-oracle output digest.
 * Returns the line's terminating line feed. */
static inline char const* bq_retirement_campaign_ready_observed(char const* line, u32 index, u64* row,
    char oracle[SHA256_HEX_CAPACITY])
{
    u64 fields[2] = {0};
    int ok = line != NULL;
    for (u32 field = 0; ok && field < 7; field += 1)
    {
        size_t length = strcspn(line, " \n");
        ok = length > 0 && line[length] == ' ';
        if (ok && field < 2) ok = bq_retirement_unit_campaign_decimal((String8){(char8*)line, length}, &fields[field]);
        if (ok && field == 6)
        {
            ok = length == 64 && bq_retirement_unit_campaign_hex((String8){(char8*)line, 64});
            if (ok) memcpy(oracle, line, 64);
            oracle[64] = 0;
        }
        if (ok) line += length + 1;
    }
    ok = ok && fields[0] == index;
    *row = ok ? fields[1] : 0;
    char const* end = ok ? strchr(line, '\n') : NULL;
    return end;
}

/* Join the correctness gate the campaign binds to the ready record: the
 * gate must be ready (its seal recomputes) with exactly the record's
 * `correctness=` seal, and carry the record's A, support and census digests,
 * both binaries, the population shape and hash, and every reference row's
 * oracle output (strictly increasing rows, no gate row with an oracle the
 * record does not name). */
static inline BqError bq_retirement_campaign_ready_gate(BqRetirementCampaignReady const* ready,
    BqRetirementCorrectness const* gate)
{
    char population[SHA256_HEX_CAPACITY] = {0};
    /* Another or an unready correctness gate than the record's seal is a
     * recipe (gate) mismatch; a field of the sealed gate that differs is a
     * source mismatch. */
    int sealed = ready && ready->owned && ready->text && gate && bq_retirement_correctness_ready(gate) &&
        !strcmp(gate->sealed_sha256, ready->correctness_sha256);
    int ok = sealed && !strcmp(gate->prepared.preparation_sha256, ready->preparation_sha256) &&
        !strcmp(gate->prepared.support_sha256, ready->support_sha256) &&
        !strcmp(gate->prepared.census_sha256, ready->census_sha256) &&
        !strcmp(gate->prepared.binary_sha256[0], ready->binary_sha256[0]) &&
        !strcmp(gate->prepared.binary_sha256[1], ready->binary_sha256[1]) &&
        gate->prepared.rows == ready->rows && gate->prepared.object_rows == ready->object_rows &&
        gate->prepared.native_target == ready->native_target &&
        bq_retirement_oracle_population_hash(gate->trusted_rows, gate->prepared.rows, population) &&
        !strcmp(population, ready->population_sha256);
    char const* cursor = ok ? strstr(ready->text, "\nobserved=") : NULL;
    u64 previous = 0;
    for (u32 index = 0; ok && index < ready->observed_rows; index += 1)
    {
        u64 row = 0;
        char oracle[SHA256_HEX_CAPACITY] = {0};
        cursor = cursor && !strncmp(cursor, "\nobserved=", 10) ?
            bq_retirement_campaign_ready_observed(cursor + 10, index, &row, oracle) : NULL;
        ok = cursor && row < gate->prepared.rows && (!index || row > previous) &&
            !strcmp(gate->trusted_rows[row].independent_oracle_sha256, oracle);
        previous = row;
    }
    u32 claimed = 0;
    for (u32 row = 0; ok && row < gate->prepared.rows; row += 1)
        claimed += gate->trusted_rows[row].independent_oracle_sha256[0] ? 1u : 0u;
    ok = ok && claimed == ready->observed_rows;
    BqError result = !sealed ? BQ_RECIPE_MISMATCH : ok ? BQ_OK : BQ_SOURCE_MISMATCH;
    return result;
}

/* The unit's issued step 9 gate (lane B's bq_retirement_unit_gate) is the
 * one the record admits: owned and issued, its seal the record's `gate=`
 * seal, and its correctness gate joined to the record. The campaign binds
 * only that correctness gate. */
static inline BqError bq_retirement_campaign_ready_unit_gate(BqRetirementCampaignReady const* ready,
    BqRetirementUnitGate const* unit_gate)
{
    int issued = ready && ready->owned && unit_gate && unit_gate->owned &&
        unit_gate->issuer == BQ_RETIREMENT_UNIT_GATE_ISSUED && !strcmp(unit_gate->seal_sha256, ready->gate_sha256) &&
        !strcmp(unit_gate->authority_sha256, ready->checks_authority_sha256) &&
        !strcmp(unit_gate->evidence_sha256, ready->check_evidence_sha256);
    BqError result = issued ? bq_retirement_campaign_ready_gate(ready, &unit_gate->correctness) : BQ_RECIPE_MISMATCH;
    return result;
}

/* The attempt's request, read from the record store before anything else:
 * it must name the blocked retirement recipe. */
static inline int bq_retirement_campaign_service_request_blocked(BqRetirementStore store, uint64_t job_id)
{
    char name[48];
    BqRequest request = {0};
    int ok = store.directory >= 0 && bq_record_name(name, "request", job_id) &&
        bq_record_read_at(store.directory, name, request.bytes, BQ_REQUEST_CAP, &request.size) == BQ_OK &&
        bq_request_recipe(&request) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED;
    return ok;
}

/* Hold both binaries again from the record digests the ready record names:
 * the unit's own build import re-verifies every matched-build stage and the
 * binary record, then opens the frozen files. Only a pair whose digests and
 * A are the record's is handed to held. */
static inline BqError bq_retirement_campaign_ready_held(BqRetirementCampaignUnitStore const* unit,
    BqRetirementUnitPrepared const* prepared, BqRetirementCampaignReady const* ready, BqRetirementHeldBinaries* held)
{
    BqRetirementUnitBuilt built = {.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
    BqError result = unit && prepared && ready && ready->owned && held && !held->owned ?
        bq_retirement_unit_built_import(unit->store, prepared, unit->workspaces, unit->installed, unit->workspace_root,
            unit->profile, unit->driver, unit->toolchain_root, unit->broker, unit->broker_workspaces,
            ready->binary_record_sha256, ready->build_record_sha256, &built) : BQ_BAD_REQUEST;
    if (result == BQ_OK)
    {
        int same = !strcmp(built.binary_record_sha256, ready->binary_record_sha256) &&
            !strcmp(built.build_record_sha256, ready->build_record_sha256) &&
            !strcmp(built.binaries.verified.preparation_sha256, ready->preparation_sha256);
        for (u32 side = 0; same && side < 2; side += 1)
            same = !strcmp(built.binaries.verified.binary_sha256[side], ready->binary_sha256[side]);
        result = same ? BQ_OK : BQ_SOURCE_MISMATCH;
    }
    bool filled = result == BQ_OK;
    if (filled)
    {
        *held = built.binaries;
        built.binaries = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    }
    if (built.owned && !bq_retirement_unit_built_release(&built) && result == BQ_OK) result = BQ_IO;
    /* A refused call never touches a holder it did not fill. */
    if (result != BQ_OK && filled) bq_retirement_binaries_release(held);
    return result;
}

/* This job and attempt's channel holds exactly the `sequence`
 * acknowledgement, the SIGTERM self-pipe is quiet and the absolute deadline
 * has not passed. With BQ_PHASE_MEASURING it is the in-unit stand-in for the
 * queue's active MEASURING attempt. */
static inline int bq_retirement_campaign_service_phase(BqPhaseChannel const* phases, unsigned sequence,
    uint64_t job_id, uint64_t attempt_token, int cancellation_fd, uint64_t deadline_ns)
{
    int ok = phases && job_id && attempt_token && phases->job == job_id && phases->attempt == attempt_token &&
        phases->sequence == sequence && phases->last_time &&
        bq_retirement_unit_campaign_live(phases, cancellation_fd, deadline_ns);
    return ok;
}

/* The heavy store-based import, run under SETTLING before the untimed
 * batches: A from the sealed export (the request must name the blocked
 * retirement recipe), the ready record through the coordinator replay, the
 * unit gate's A and sources, the unit gate joined to the record
 * (bq_retirement_campaign_ready_unit_gate), and both binaries held again
 * from the record's digests; the imported record remembers the unit gate it
 * joined (ready->unit_gate), and the bind accepts only that gate. held and
 * ready must be empty; on
 * success both are filled for this attempt (release both). A refusal
 * releases only what this call filled. The channel is checked before and
 * after the import. */
static inline BqError bq_retirement_campaign_service_import_unit_pinned(BqRetirementCampaignUnitStore const* unit,
    BqPhaseChannel const* phases, int cancellation_fd, uint64_t deadline_ns, uint64_t job_id, uint64_t attempt_token,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitGate const* unit_gate, BqRetirementHeldBinaries* held, BqRetirementCampaignReady* ready)
{
    bool fresh = unit && unit_gate && held && !held->owned && ready && !ready->owned;
    BqRetirementUnitPrepared prepared = {.policy = {.clang = -1, .inventory = -1}};
    BqError result = fresh && bq_retirement_campaign_service_phase(phases, BQ_PHASE_SETTLING, job_id, attempt_token,
        cancellation_fd, deadline_ns) ? BQ_OK : BQ_INVALID_TRANSITION;
    bool held_started = result == BQ_OK, ready_started = result == BQ_OK;
    if (held_started) *held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    if (result == BQ_OK && !bq_retirement_campaign_service_request_blocked(unit->store, job_id))
        result = BQ_INVALID_TRANSITION;
    if (result == BQ_OK)
        result = bq_retirement_unit_prepare_pinned(unit->store, unit->workspaces, unit->installed, job_id,
            attempt_token, unit->profile, unit->toolchain_root, preparation_sha256, &prepared);
    if (result == BQ_OK) result = bq_retirement_campaign_ready_import(unit, &prepared, ready_sha256, ready);
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_gate_matches(&unit_gate->correctness, preparation_sha256,
                                                             &prepared.preparation);
    if (result == BQ_OK) result = bq_retirement_campaign_ready_unit_gate(ready, unit_gate);
    if (result == BQ_OK) result = bq_retirement_campaign_ready_held(unit, &prepared, ready, held);
    /* A's two roots, each scanned against A's manifest, for the launches'
     * canonical layout (released with the record). */
    for (u32 side = 0; result == BQ_OK && side < 2; side += 1)
    {
        ready->sources[side] = bq_retirement_unit_source_root(unit->workspaces, &prepared, side);
        if (ready->sources[side] < 3) result = BQ_SOURCE_MISMATCH;
    }
    if (result == BQ_OK && !bq_retirement_campaign_service_phase(phases, BQ_PHASE_SETTLING, job_id, attempt_token,
        cancellation_fd, deadline_ns))
        result = BQ_INVALID_TRANSITION;
    if (prepared.owned && !bq_retirement_unit_release(&prepared) && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK) ready->unit_gate = unit_gate;
    if (result != BQ_OK)
    {
        if (held_started && held->owned) bq_retirement_binaries_release(held);
        if (ready_started && ready->owned) bq_retirement_campaign_ready_release(ready);
    }
    return result;
}

/* The compiled profile and installed paths: the blocked profile has no
 * reference, census or campaign pins, so this fails closed. */
static inline BqRetirementCampaignUnitStore bq_retirement_campaign_service_unit_store(BqRetirementStore store,
    int workspaces, int installed)
{
    BqRetirementCampaignUnitStore unit = {store, workspaces, installed, S8(BQ_RETIREMENT_STAGE_WORKSPACE_ROOT),
        bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED), S8("full-census"), BQ_RETIREMENT_BUILD_DRIVER,
        BQ_RETIREMENT_TOOLCHAIN_ROOT, BQ_RETIREMENT_UNIT_BROKER, BQ_RETIREMENT_STAGE_WORKSPACE_ROOT};
    return unit;
}

static inline BqError bq_retirement_campaign_service_import_unit(BqRetirementStore store, int workspaces,
    int installed, BqPhaseChannel const* phases, int cancellation_fd, uint64_t deadline_ns, uint64_t job_id,
    uint64_t attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY], BqRetirementUnitGate const* unit_gate,
    BqRetirementHeldBinaries* held, BqRetirementCampaignReady* ready)
{
    BqRetirementCampaignUnitStore unit = bq_retirement_campaign_service_unit_store(store, workspaces, installed);
    BqError result = bq_retirement_campaign_service_import_unit_pinned(&unit, phases, cancellation_fd, deadline_ns,
        job_id, attempt_token, preparation_sha256, ready_sha256, unit_gate, held, ready);
    return result;
}

/* Each timed row's campaign batch-group ordinal: groups in ascending order
 * of their smallest member, a timed object row in the frozen group that
 * names it as a member, every other timed row a singleton. */
static inline int bq_retirement_campaign_timed_groups_assign(BqRetirementCorrectness const* gate,
    TpRetirementTimedRow* rows, unsigned count)
{
    unsigned* ordinals = gate->batch_group_count ? calloc(gate->batch_group_count, sizeof(*ordinals)) : NULL;
    int ok = !gate->batch_group_count || ordinals;
    unsigned next = 0;
    for (unsigned index = 0; ok && index < count; index += 1)
    {
        u32 id = rows[index].id;
        u32 found = UINT32_MAX;
        for (u32 g = 0; gate->trusted_rows[id].stage == BQ_RETIREMENT_STAGE_OBJECT && g < gate->batch_group_count; g += 1)
            for (u32 k = 0; found == UINT32_MAX && k < gate->batch_groups[g].contract[0].input_count; k += 1)
                if (gate->batch_groups[g].contract[0].inputs[k].member &&
                    gate->batch_groups[g].contract[0].inputs[k].row == id)
                    found = g;
        ok = gate->trusted_rows[id].stage != BQ_RETIREMENT_STAGE_OBJECT || found != UINT32_MAX;
        if (ok && found != UINT32_MAX)
        {
            if (!ordinals[found]) ordinals[found] = ++next;
            rows[index].group = ordinals[found] - 1u;
        }
        else if (ok) rows[index].group = next++;
    }
    free(ordinals);
    ok = ok && next == bq_retirement_campaign_timed_groups(gate);
    return ok;
}

/* The pinned #508 performance-row artifact's canonical prefix, as
 * bq_retirement_performance_rows_derive accepts it. */
#define BQ_RETIREMENT_CAMPAIGN_ROWS_PREFIX "{\"row_identity_fields\":[\"fixture\",\"target\",\"target_abi\",\"cpu\"," \
    "\"cpu_features\",\"allocator\",\"frontend_lowering\",\"PIC\",\"fixture_recipe\",\"compile_obligation\"," \
    "\"link_obligation\",\"execution_obligation\",\"diagnostic_obligation\",\"argv_evidence\",\"artifact_stage\"]," \
    "\"rows\":["
/* The pinned rows through the installed census and the profile pin; each
 * declared row's identity must be the trusted row's sealed identity. */
static inline BqError bq_retirement_documents_population(int installed, String8 profile,
    BqRetirementTrustedRow const* trusted, uint32_t trusted_count, uint32_t native_target,
    BqRetirementDocumentPopulation* population)
{
    BqRetirementCensusFiles census;
    for (u32 index = 0; index < BQ_RETIREMENT_CENSUS_FILE_COUNT; index += 1) census.descriptors[index] = -1;
    u8* text = NULL;
    u64 length = 0;
    bool fresh = population && !population->rows && !population->pool;
    if (fresh) *population = (BqRetirementDocumentPopulation){.native_target = native_target};
    bool opened = fresh && trusted && trusted_count && bq_retirement_unit_census_open(installed, &census) == BQ_OK;
    bool ok = opened && bq_retirement_validator_read_pinned(census.descriptors[BQ_RETIREMENT_CENSUS_PERFORMANCE_ROWS],
        profile, S8("performance-rows-sha256="), BQ_RETIREMENT_POPULATION_BYTES_CAP, &text, &length,
        population->performance_rows_sha256);
    if (opened && !bq_retirement_unit_census_close(&census)) ok = false;
    char (*storage)[BQ_RETIREMENT_POPULATION_FIELD_CAP] = ok ? calloc(17, sizeof(*storage)) : NULL;
    if (ok)
    {
        population->rows = calloc(trusted_count, sizeof(*population->rows));
        population->pool_capacity = length + 1u;
        population->pool = malloc((size_t)population->pool_capacity);
    }
    ok = ok && storage && population->rows && population->pool;
    BqRetirementPopulationCursor cursor = {(String8){(char8*)text, length}, 0, ok};
    bq_retirement_population_literal(&cursor, S8(BQ_RETIREMENT_CAMPAIGN_ROWS_PREFIX));
    /* rows.tsv columns in ROW_IDENTITY_FIELDS order (16 argv_evidence; the
     * stage is the artifact_stage value). */
    static unsigned const columns[BQ_RETIREMENT_DOCUMENT_FIELDS - 1] = {2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16};
    uint32_t declared_rows = 0;
    bool more = cursor.ok;
    while (cursor.ok && more)
    {
        String8 fields[17] = {0};
        String8 stage = {0}, code_section = {0}, runtime_oracle = {0};
        bool metrics[4] = {0};
        u64 declared = 0;
        char identity[SHA256_HEX_CAPACITY] = {0};
        bq_retirement_population_row(&cursor, &declared, storage, fields, &stage, &code_section, &runtime_oracle,
                                     metrics);
        cursor.ok = cursor.ok && declared_rows < trusted_count && declared == declared_rows &&
            bq_retirement_census_stage_identity_sha256(fields, stage, identity) &&
            !memcmp(identity, trusted[declared_rows].identity_sha256, SHA256_HEX_CAPACITY);
        BqRetirementDocumentRow* row = cursor.ok ? &population->rows[declared_rows] : NULL;
        for (unsigned field = 0; row && cursor.ok && field < BQ_RETIREMENT_DOCUMENT_FIELDS; ++field)
        {
            String8 value = field + 1 < BQ_RETIREMENT_DOCUMENT_FIELDS ? fields[columns[field]] : stage;
            cursor.ok = value.length <= population->pool_capacity - population->pool_used;
            if (cursor.ok)
            {
                memcpy(population->pool + population->pool_used, value.pointer, (size_t)value.length);
                row->offset[field] = (uint32_t)population->pool_used;
                row->length[field] = (uint32_t)value.length;
                population->pool_used += value.length;
            }
        }
        if (row && cursor.ok)
        {
            row->compile = metrics[0];
            row->code = metrics[2];
            row->runtime = metrics[3];
            row->marker = string_equal(code_section, S8("deterministic-code-section")) ? 2u :
                string_equal(code_section, S8("deterministic-zero-baseline-code-section")) ? 1u : 0u;
            cursor.ok = row->compile == (trusted[declared_rows].compiler_eligible != 0) &&
                row->compile == (row->marker != 0);
        }
        declared_rows += cursor.ok;
        more = cursor.ok && cursor.offset < length && text[cursor.offset] == ',';
        if (more) cursor.offset += 1;
    }
    ok = cursor.ok && declared_rows == trusted_count;
    free(storage);
    free(text);
    if (ok) population->count = declared_rows;
    else if (fresh) bq_retirement_documents_population_release(population);
    BqError result = ok ? BQ_OK : BQ_SOURCE_MISMATCH;
    return result;
}

/* Lane E's per-timed-row layout: the native-host timed projection of the
 * sealed gate, in ascending row order, with each row's campaign group,
 * runtime flag and six dimension values (target, cpu, allocator,
 * frontend_lowering, PIC, artifact_stage). The values come from the pinned
 * performance-row artifact (bq_retirement_documents_population: read through
 * the installed census and its profile pin, each declared row's identity
 * recomputed from its fields and required to be the gate row's sealed
 * identity), so every value is one the gate authenticated. rows needs one
 * entry per timed row. */
static inline BqError bq_retirement_campaign_service_timed_rows(BqRetirementCampaignUnitStore const* unit,
    BqRetirementCorrectness const* gate, TpRetirementTimedRow* rows, unsigned capacity, unsigned* count)
{
    static unsigned const fields[TP_RETIREMENT_TIMED_DIMENSIONS] = {BQ_RETIREMENT_DOCUMENT_TARGET,
        BQ_RETIREMENT_DOCUMENT_CPU, BQ_RETIREMENT_DOCUMENT_ALLOCATOR, BQ_RETIREMENT_DOCUMENT_FRONTEND,
        BQ_RETIREMENT_DOCUMENT_PIC, BQ_RETIREMENT_DOCUMENT_STAGE};
    BqRetirementDocumentPopulation population = {0};
    int ok = unit && gate && rows && count && bq_retirement_correctness_ready(gate) &&
        bq_retirement_documents_population(unit->installed, unit->profile, gate->trusted_rows, gate->prepared.rows,
            gate->prepared.native_target, &population) == BQ_OK;
    unsigned timed = 0;
    for (u32 row = 0; ok && row < gate->prepared.rows; row += 1)
    {
        if (!(gate->trusted_rows[row].compiler_eligible && gate->trusted_rows[row].target == gate->prepared.native_target))
            continue;
        ok = timed < capacity;
        if (ok) rows[timed] = (TpRetirementTimedRow){.id = row, .runtime = gate->facts[row].runtime_eligible ? 1u : 0u};
        for (u32 index = 0; ok && index < TP_RETIREMENT_TIMED_DIMENSIONS; index += 1)
        {
            String8 value = bq_retirement_document_value(&population, row, fields[index]);
            ok = value.length && value.length <= TP_RETIREMENT_TIMED_DIMENSION_BYTES;
            if (ok) memcpy(rows[timed].dimensions[index], value.pointer, (size_t)value.length);
            if (ok) rows[timed].dimensions[index][value.length] = 0;
        }
        timed += ok;
    }
    ok = ok && timed == bq_retirement_campaign_timed_rows(gate) && bq_retirement_campaign_timed_groups_assign(gate, rows,
                                                                                                             timed);
    bq_retirement_documents_population_release(&population);
    if (count) *count = ok ? timed : 0;
    BqError result = ok ? BQ_OK : BQ_SOURCE_MISMATCH;
    return result;
}

/* The cheap recheck under MEASURING: the sealed record still stands at its
 * content address with exactly the imported bytes. */
static inline int bq_retirement_campaign_ready_standing(BqRetirementCampaignUnitStore const* unit,
    BqRetirementCampaignReady const* ready)
{
    char name[80], digest[SHA256_HEX_CAPACITY] = {0};
    int named = ready && ready->owned ? snprintf(name, sizeof(name), "ready-%s", ready->ready_sha256) : -1;
    int directory = unit && named > 0 && (size_t)named < sizeof(name) ?
        bq_retirement_unit_attempt_open(unit->workspaces, &ready->job, NULL, BQ_RETIREMENT_UNIT_READY_DIRECTORY) : -1;
    char* bytes = directory >= 0 ? (char*)malloc((size_t)ready->length + 1u) : NULL;
    u32 length = 0;
    int ok = bytes && bq_retirement_unit_closed(directory, NULL, name, true) &&
        bq_record_read_at(directory, name, (u8*)bytes, ready->length + 1u, &length) == BQ_OK &&
        length == ready->length && !memcmp(bytes, ready->text, length);
    if (ok)
    {
        bq_digest(bytes, length, (char8*)digest);
        ok = !memcmp(digest, ready->ready_sha256, SHA256_HEX_CAPACITY);
    }
    free(bytes);
    if (directory >= 0 && close(directory) != 0) ok = 0;
    return ok;
}

/* The held pair is still the record's: its A and binary digests, and each
 * descriptor's file identity is the verified one. */
static inline int bq_retirement_campaign_ready_holds(BqRetirementCampaignReady const* ready,
    BqRetirementHeldBinaries const* held)
{
    int ok = ready && ready->owned && held && held->owned == 1 &&
        !strcmp(held->verified.preparation_sha256, ready->preparation_sha256);
    for (u32 side = 0; ok && side < 2; side += 1)
    {
        char identity[65] = {0};
        ok = !strcmp(held->verified.binary_sha256[side], ready->binary_sha256[side]) &&
            bq_retirement_campaign_descriptor_identity(held->descriptors[side], identity) &&
            !strcmp(identity, held->verified.binary_identity_sha256[side]);
    }
    return ok;
}

static inline int bq_retirement_campaign_plan_commands_match(BqRetirementRowPlan const* plan,
    BqRetirementUnitGate const* unit_gate, BqRetirementCampaignRequest const* request);

/* The cheap store-based bind under the MEASURING acknowledgement, after the
 * import and the untimed batches. It rechecks the channel, that held and
 * ready are this attempt's import, that the record still stands with its
 * imported bytes, the unit gate join (request->gate must be that unit
 * gate's own correctness gate) and held pair, and that the pre-sample
 * binding follows the MEASURING acknowledgement, that every command is the
 * one lane B's row plan (request->row_plan) resolves for the unit gate, so
 * its address-space bound is the plan template's for that row and side
 * (bq_retirement_campaign_plan_commands_match); derives the plan and
 * pre-sample context (untimed is the finished untimed record stream) and
 * refuses any caller value that differs; then binds and marks the binding
 * with the record's digest (the driver's attach requires the mark). A
 * refusal clears binding and poisons both stages; held and ready stay the
 * caller's. */
static inline BqError bq_retirement_campaign_service_bind_unit_pinned(BqRetirementCampaignUnitStore const* unit,
    BqPhaseChannel const* phases, int cancellation_fd, uint64_t deadline_ns, uint64_t job_id, uint64_t attempt_token,
    TpRetirementShard const* untimed, BqRetirementUnitGate const* unit_gate, BqRetirementCampaignRequest const* request,
    BqRetirementHeldBinaries const* held, BqRetirementCampaignReady const* ready, BqRetirementCampaignBinding* binding)
{
    bool binding_available = binding && !binding->campaign && !binding->held_binaries;
    TpRetirementTranscript const* transcript = request && request->aa ? request->aa->transcript : NULL;
    BqError result = unit && bq_retirement_campaign_service_phase(phases, BQ_PHASE_MEASURING, job_id, attempt_token,
        cancellation_fd, deadline_ns) && bq_retirement_campaign_service_request_ready(request) && binding_available &&
        ready && ready->owned && ready->job_id == job_id && ready->attempt_token == attempt_token &&
        ready->job.id == job_id && ready->job.token == attempt_token && transcript &&
        transcript->bound_at_ns > phases->last_time && unit_gate ? BQ_OK : BQ_INVALID_TRANSITION;
    if (result == BQ_OK && !bq_retirement_campaign_ready_standing(unit, ready)) result = BQ_WORKSPACE_MISMATCH;
    if (result == BQ_OK && (unit_gate != ready->unit_gate || request->gate != &unit_gate->correctness))
        result = BQ_SOURCE_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_campaign_ready_unit_gate(ready, unit_gate);
    if (result == BQ_OK && !bq_retirement_campaign_ready_holds(ready, held)) result = BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_campaign_plan_commands_match(request->row_plan, unit_gate, request))
        result = BQ_RECIPE_MISMATCH;
    BqRetirementUnitCampaignPins pins = {0};
    if (result == BQ_OK && !bq_retirement_unit_campaign_pins(unit->profile, &pins)) result = BQ_RECIPE_MISMATCH;
    TpRetirementPlan plan = {0};
    char plan_sha256[65] = {0}, context_sha256[65] = {0};
    if (result == BQ_OK)
    {
        size_t per_stage = request->command_count / TP_RETIREMENT_CAMPAIGN_STAGES;
        int derived = request->command_count == per_stage * TP_RETIREMENT_CAMPAIGN_STAGES &&
            bq_retirement_unit_campaign_derive(request->gate, &pins, ready->ready_sha256, untimed, request->aa,
                request->ab, request->aa_commands, request->ab_commands, per_stage, &plan, plan_sha256,
                context_sha256) &&
            !memcmp(&plan, request->plan, sizeof(plan)) && request->plan_sha256 && request->context_sha256 &&
            !strcmp(plan_sha256, request->plan_sha256) && !strcmp(context_sha256, request->context_sha256);
        if (!derived) result = BQ_RECIPE_MISMATCH;
    }
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_bind_verified(request, unit->profile, job_id, attempt_token, held,
                                                              binding);
    if (result == BQ_OK) memcpy(binding->unit_ready_sha256, ready->ready_sha256, SHA256_HEX_CAPACITY);
    else bq_retirement_campaign_service_refuse(request, NULL, false, binding, binding_available);
    return result;
}

static inline BqError bq_retirement_campaign_service_bind_unit(BqRetirementStore store, int workspaces,
    int installed, BqPhaseChannel const* phases, int cancellation_fd, uint64_t deadline_ns, uint64_t job_id,
    uint64_t attempt_token, TpRetirementShard const* untimed, BqRetirementUnitGate const* unit_gate,
    BqRetirementCampaignRequest const* request, BqRetirementHeldBinaries const* held,
    BqRetirementCampaignReady const* ready, BqRetirementCampaignBinding* binding)
{
    BqRetirementCampaignUnitStore unit = bq_retirement_campaign_service_unit_store(store, workspaces, installed);
    BqError result = bq_retirement_campaign_service_bind_unit_pinned(&unit, phases, cancellation_fd, deadline_ns,
        job_id, attempt_token, untimed, unit_gate, request, held, ready, binding);
    return result;
}

/* The campaign's measured commands, resolved from lane B's pinned row plan
 * in the canonical child layout (retirement_sandbox.h), so each one's
 * tp_retirement_command_hash is exactly the digest the plan derived for the
 * same row, side and label. commands holds both stages ([stage * count +
 * slot * 2 + variant]); every command's argv, environment, artifact leaf and
 * digests live in its own allocation (blocks). Zero-initialize; release on
 * every path. */
typedef struct BqRetirementCampaignPlanCommands
{
    TpRetirementMeasuredCommand* commands;
    char** blocks;
    size_t count;
    unsigned groups, runtime_count;
} BqRetirementCampaignPlanCommands;

static inline void bq_retirement_campaign_plan_commands_release(BqRetirementCampaignPlanCommands* out)
{
    if (out)
    {
        for (size_t index = 0; out->blocks && index < out->count * TP_RETIREMENT_CAMPAIGN_STAGES; index += 1)
            free(out->blocks[index]);
        free(out->blocks);
        free(out->commands);
        *out = (BqRetirementCampaignPlanCommands){0};
    }
}

/* One resolved step copied into a compact allocation: argv and environment
 * (each NULL-terminated), the command and output digests and the artifact
 * leaf (empty for none); command points into it. */
static inline int bq_retirement_campaign_plan_command_copy(BqRetirementRowCommand const* resolved, char const* artifact,
    TpRetirementMeasuredCommand* command, char** block, char** digests)
{
    size_t strings = strlen(artifact) + 1u;
    for (u32 index = 0; index < resolved->argument_count; index += 1) strings += strlen(resolved->arguments[index]) + 1u;
    for (u32 index = 0; index < resolved->environment_count; index += 1)
        strings += strlen(resolved->environment[index]) + 1u;
    size_t pointers = (size_t)resolved->argument_count + resolved->environment_count + 2u;
    char* bytes = malloc(pointers * sizeof(char*) + 2u * SHA256_HEX_CAPACITY + strings);
    if (bytes)
    {
        char** arguments = (char**)(void*)bytes;
        char** environment = arguments + resolved->argument_count + 1u;
        char* digest = bytes + pointers * sizeof(char*);
        char* text = digest + 2u * SHA256_HEX_CAPACITY;
        memset(digest, 0, 2u * SHA256_HEX_CAPACITY);
        for (u32 index = 0; index < resolved->argument_count; index += 1)
        {
            size_t length = strlen(resolved->arguments[index]) + 1u;
            memcpy(text, resolved->arguments[index], length);
            arguments[index] = text;
            text += length;
        }
        arguments[resolved->argument_count] = NULL;
        for (u32 index = 0; index < resolved->environment_count; index += 1)
        {
            size_t length = strlen(resolved->environment[index]) + 1u;
            memcpy(text, resolved->environment[index], length);
            environment[index] = text;
            text += length;
        }
        environment[resolved->environment_count] = NULL;
        memcpy(text, artifact, strlen(artifact) + 1u);
        command->arguments = arguments;
        command->argument_count = resolved->argument_count;
        command->environment = environment;
        command->environment_count = resolved->environment_count;
        command->artifact = text[0] ? text : NULL;
        command->command_sha256 = digest;
        command->output_sha256 = digest + SHA256_HEX_CAPACITY;
        *digests = digest;
    }
    *block = bytes;
    int ok = bytes != NULL;
    return ok;
}

/* The first member row of a frozen batch group (TP_RETIREMENT_BATCH_NO_ROW
 * for none). */
static inline u32 bq_retirement_campaign_plan_first_member(BqRetirementBatchGroup const* group)
{
    u32 first = TP_RETIREMENT_BATCH_NO_ROW;
    for (u32 input = 0; group && input < group->contract[0].input_count && first == TP_RETIREMENT_BATCH_NO_ROW;
         input += 1)
        if (group->contract[0].inputs[input].member) first = group->contract[0].inputs[input].row;
    return first;
}

/* The campaign's commands from the row plan the unit gate was issued on
 * (its plan_sha256 is the plan's authority, and the gate's rows are the
 * plan's completed rows): the timed groups in the campaign's order (each
 * frozen object group at its first member, each timed singleton at its
 * row), then the runtime rows, per stage and variant. A command runs side
 * stage ? variant : 0 with label 2 only for the A/A second variant, in the
 * work slot (BQ_RETIREMENT_ROW_WORK_PATH), with its template's timeout and
 * address-space bound. An object group's command carries the frozen
 * contract of its side, whose exit status and output digest it expects; a
 * singleton's writes the output leaf and expects its side's observed
 * artifact; a runtime row's expects the independent oracle's output. Every
 * label-1 command must hash to the digest the gate sealed for it (its batch
 * group's, or its row's compiler or runtime command); the label-2 commands
 * are bound by the gate's A/A aggregate when the campaign binds. */
static inline int bq_retirement_campaign_plan_commands(BqRetirementRowPlan const* plan,
    BqRetirementUnitGate const* unit_gate, BqRetirementCampaignPlanCommands* out)
{
    BqRetirementCorrectness const* gate = unit_gate ? &unit_gate->correctness : NULL;
    bool fresh = out && !out->commands && !out->blocks;
    if (fresh) *out = (BqRetirementCampaignPlanCommands){0};
    char population[SHA256_HEX_CAPACITY] = {0};
    int ok = fresh && plan && plan->owned && unit_gate && unit_gate->owned && unit_gate->issuer == BQ_RETIREMENT_UNIT_GATE_ISSUED &&
        bq_retirement_correctness_ready(gate) && !strcmp(unit_gate->plan_sha256, plan->authority_sha256) &&
        plan->row_count == gate->prepared.rows && plan->native_target == gate->prepared.native_target &&
        bq_retirement_oracle_population_hash(gate->trusted_rows, gate->prepared.rows, population) &&
        !strcmp(population, plan->population_sha256);
    u32 native = ok ? gate->prepared.native_target : 0;
    unsigned groups = ok ? bq_retirement_campaign_timed_groups(gate) : 0, runtime_count = 0;
    for (u32 row = 0; ok && row < gate->prepared.rows; row += 1)
        runtime_count += gate->trusted_rows[row].compiler_eligible && gate->trusted_rows[row].target == native &&
            gate->trusted_rows[row].stage != BQ_RETIREMENT_STAGE_OBJECT && gate->facts[row].runtime_eligible;
    ok = ok && groups != UINT32_MAX && groups;
    size_t count = ok ? (size_t)(groups + runtime_count) * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT : 0;
    if (ok)
    {
        out->commands = calloc(count * TP_RETIREMENT_CAMPAIGN_STAGES, sizeof(*out->commands));
        out->blocks = calloc(count * TP_RETIREMENT_CAMPAIGN_STAGES, sizeof(*out->blocks));
        out->count = count;
    }
    char* storage = ok ? malloc(BQ_RETIREMENT_ROW_COMMAND_STORAGE) : NULL;
    ok = ok && out->commands && out->blocks && storage;
    unsigned group = 0, runtime = 0, next_batch = 0;
    for (u32 row = 0; ok && row < gate->prepared.rows; row += 1)
    {
        BqRetirementTrustedRow const* trusted = gate->trusted_rows + row;
        BqRetirementRowPlanRow const* planned = plan->rows + row;
        if (!(trusted->compiler_eligible && trusted->target == native)) continue;
        bool object = trusted->stage == BQ_RETIREMENT_STAGE_OBJECT;
        BqRetirementBatchGroup const* frozen = object && next_batch < gate->batch_group_count ?
            gate->batch_groups + next_batch : NULL;
        if (object && bq_retirement_campaign_plan_first_member(frozen) != row)
        {
            /* A later member, bound with its group. */
            ok = planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH;
            continue;
        }
        bool with_runtime = !object && gate->facts[row].runtime_eligible;
        BqRetirementRowPlanGroup const* batch = object && planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH &&
            planned->group < plan->group_count ? plan->groups + planned->group : NULL;
        ok = group < groups && (object ? frozen && batch :
             planned->compile < plan->template_count && planned->fixture &&
             (!with_runtime || planned->runtime < plan->template_count));
        char output[32], metrics[32];
        bq_retirement_row_leaves(row, output, metrics);
        for (u32 stage = 0; ok && stage < TP_RETIREMENT_CAMPAIGN_STAGES; stage += 1)
            for (u32 variant = 0; ok && variant < TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT; variant += 1)
                for (u32 kind = 0; ok && kind < (with_runtime ? 2u : 1u); kind += 1)
                {
                    u32 side = stage ? variant : 0, label = !stage && variant ? 2u : 1u;
                    size_t index = stage * count + (size_t)(kind ? groups + runtime : group) *
                        TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT + variant;
                    BqRetirementRowContext context = object ?
                        (BqRetirementRowContext){.metrics = batch->metrics, .inputs = batch->list_leaf, .side = side,
                                                 .label = label} :
                        (BqRetirementRowContext){.fixture = planned->fixture, .output = output, .metrics = metrics,
                                                 .side = side, .label = label};
                    BqRetirementRowTemplate const* template = plan->templates +
                        (kind ? planned->runtime : object ? batch->template_index : planned->compile);
                    TpRetirementMeasuredCommand* command = out->commands + index;
                    *command = (TpRetirementMeasuredCommand){.unit = kind ? row : group, .kind = kind,
                        .variant = variant, .directory = BQ_RETIREMENT_ROW_WORK_PATH,
                        .timeout_seconds = template->timeout_seconds, .memory_mib = template->memory_mib};
                    BqRetirementRowCommand resolved;
                    char* digests = NULL;
                    ok = bq_retirement_row_command(template, &context, storage, &resolved) &&
                        bq_retirement_campaign_plan_command_copy(&resolved, !object && !kind ? output : "", command,
                                                                 out->blocks + index, &digests);
                    char const* sealed = NULL;
                    if (ok && object)
                    {
                        command->batch = &frozen->contract[side];
                        command->exit_status = (int)frozen->contract[side].exit_status;
                        ok = tp_retirement_batch_contract_output(command->batch, digests + SHA256_HEX_CAPACITY);
                        sealed = frozen->command_sha256[side];
                    }
                    else if (ok && kind)
                    {
                        memcpy(digests + SHA256_HEX_CAPACITY, trusted->independent_oracle_sha256, SHA256_HEX_CAPACITY);
                        ok = tp_retirement_digest(digests + SHA256_HEX_CAPACITY);
                        sealed = trusted->runtime_command_sha256[side];
                    }
                    else if (ok)
                    {
                        char const* objects[1] = {gate->facts[row].side[side].artifact_sha256};
                        ok = tp_retirement_batch_output_digest(objects, 1, digests + SHA256_HEX_CAPACITY);
                        sealed = trusted->compiler_command_sha256[side];
                    }
                    ok = ok && tp_retirement_command_hash(command, digests) &&
                        (label == 2 || !strcmp(digests, sealed));
                }
        next_batch += object;
        runtime += with_runtime;
        group += 1;
    }
    free(storage);
    ok = ok && group == groups && runtime == runtime_count && next_batch == gate->batch_group_count;
    if (ok)
    {
        out->groups = groups;
        out->runtime_count = runtime_count;
    }
    else if (fresh) bq_retirement_campaign_plan_commands_release(out);
    return ok;
}

/* The request's commands are exactly the ones the row plan resolves for
 * this unit gate: slot by slot, the same kind, unit and variant, directory,
 * command digest (recomputed from the command's own fields), output digest,
 * exit status, timeout and address-space bound. The launch then requires its
 * bound to be its command's (tp_retirement_launch_layout), so every child's
 * RLIMIT_AS is the plan template's for its row and side. */
static inline int bq_retirement_campaign_plan_commands_match(BqRetirementRowPlan const* plan,
    BqRetirementUnitGate const* unit_gate, BqRetirementCampaignRequest const* request)
{
    BqRetirementCampaignPlanCommands derived = {0};
    int ok = plan && request && request->aa_commands && request->ab_commands &&
        bq_retirement_campaign_plan_commands(plan, unit_gate, &derived) &&
        request->command_count == derived.count * TP_RETIREMENT_CAMPAIGN_STAGES;
    for (size_t index = 0; ok && index < derived.count * TP_RETIREMENT_CAMPAIGN_STAGES; index += 1)
    {
        TpRetirementMeasuredCommand const* want = derived.commands + index;
        TpRetirementMeasuredCommand const* have = (index < derived.count ? request->aa_commands :
                                                   request->ab_commands) + index % derived.count;
        char digest[SHA256_HEX_CAPACITY] = {0};
        ok = have->unit == want->unit && have->kind == want->kind && have->variant == want->variant &&
            have->exit_status == want->exit_status && have->timeout_seconds == want->timeout_seconds &&
            have->memory_mib == want->memory_mib && have->directory && !strcmp(have->directory, want->directory) &&
            have->command_sha256 && have->output_sha256 && !strcmp(have->command_sha256, want->command_sha256) &&
            !strcmp(have->output_sha256, want->output_sha256) && tp_retirement_command_hash(have, digest) &&
            !strcmp(digest, want->command_sha256);
    }
    bq_retirement_campaign_plan_commands_release(&derived);
    return ok;
}
#endif
#endif
