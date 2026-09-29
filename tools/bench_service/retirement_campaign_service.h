/* Private #1022-D campaign bind seams: the queue-owned entry and the in-unit,
 * store-based entry share one verification tail.
 *
 * Queue entry (bq_retirement_campaign_service_bind_pinned): derives both
 * durable record identities from the active job, reimports A and frozen B
 * outputs from the queue, then binds the held descriptors to the fixed
 * campaign transcript. It requires the queue's active MEASURING attempt.
 *
 * Store entry (bq_retirement_campaign_service_bind_unit_pinned): the worker
 * unit never holds the queue or the lease. It authenticates from the
 * per-attempt record store instead: the coordinator's sealed A export, the
 * unit's sealed retirement-build/ records (matched builds and binaries) and
 * the sealed retirement-ready/ record that lane B's bq_retirement_unit_ready
 * wrote, plus the MEASURING acknowledgement on the private phase channel. The
 * ready record (the B -> D handoff) is re-derived from the store by the
 * coordinator's own replay (bq_retirement_unit_replay_pinned), then imported
 * (bq_retirement_campaign_ready_import); its gate seal, build and binary
 * record digests, template, inventory, reference descriptors and command
 * digests are those the replay rebuilt. The held binaries are re-imported
 * from those record digests (bq_retirement_campaign_ready_held) and the
 * correctness gate is joined to the record (bq_retirement_campaign_ready_gate).
 * The entry derives the plan, its digest and the pre-sample context itself
 * (retirement_unit_campaign.h) and refuses any caller value that differs.
 *
 * Shared tail: bq_retirement_campaign_service_gate_matches (A and both
 * sources against the gate), bq_retirement_campaign_service_bind_verified
 * (the recipe profile's `campaign-budget-sha256=` pin, then
 * bq_retirement_campaign_bind_held), bq_retirement_campaign_service_refuse
 * (release and poison on every failure).
 *
 * (M4) The blocked profile carries no budget or campaign pins, so both entries
 * fail closed with BQ_RECIPE_MISMATCH until integration pins them. (M2) A pin
 * alone never admits object batch groups: they also need the gate's #509
 * authority. The store entry uses retirement_unit.c's private helpers, so this
 * header must follow retirement_unit.c in its translation unit (main.c's
 * unity build). No worker or recipe calls either entry yet.
 *
 * Map: BqRetirementCampaignRequest, BqRetirementCampaignUnitStore,
 * BqRetirementCampaignReady, bq_retirement_campaign_service_active,
 * bq_retirement_campaign_service_measuring.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_H
#include "retirement_prepare.h"
#include "retirement_binaries.h"
#include "retirement_campaign_binding.h"
#include "retirement_unit.h"
#include "retirement_unit_campaign.h"

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
} BqRetirementCampaignRequest;

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

static int bq_retirement_campaign_service_request_ready(BqRetirementCampaignRequest const* request)
{
    int ok = request && request->gate && request->campaign && request->plan && request->aa && request->ab;
    return ok;
}

/* Shared: the authenticated A digest and both source manifests must be the
 * ones the ready correctness gate sealed. */
static BqError bq_retirement_campaign_service_gate_matches(BqRetirementCorrectness const* gate,
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
static BqError bq_retirement_campaign_service_bind_verified(BqRetirementCampaignRequest const* request,
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
static void bq_retirement_campaign_service_refuse(BqRetirementCampaignRequest const* request,
    BqRetirementHeldBinaries* held, bool held_started, BqRetirementCampaignBinding* binding, bool binding_available)
{
    if (binding_available) *binding = (BqRetirementCampaignBinding){0};
    if (held_started && held && held->owned) bq_retirement_binaries_release(held);
    if (request && request->campaign) tp_retirement_campaign_poison(request->campaign);
    if (request && request->aa) tp_retirement_samples_poison(request->aa);
    if (request && request->ab && request->ab != request->aa) tp_retirement_samples_poison(request->ab);
}

static BqError bq_retirement_campaign_service_bind_pinned(BqQueue* queue,
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
        command_workspace, command_count, identity_workspace, identity_count, review, plan_sha256, context_sha256};
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

/* The imported BQ-RETIREMENT-READY-V1 record: its verified bytes and the
 * fields D binds. Zero-initialize; release on every path. */
typedef struct BqRetirementCampaignReady
{
    char* text;
    u64 job_id, attempt_token;
    u32 length, rows, object_rows, native_target, observed_rows, owned;
    char ready_sha256[SHA256_HEX_CAPACITY], preparation_sha256[SHA256_HEX_CAPACITY];
    char binary_record_sha256[SHA256_HEX_CAPACITY], build_record_sha256[SHA256_HEX_CAPACITY];
    char binary_sha256[2][SHA256_HEX_CAPACITY];
    char support_sha256[SHA256_HEX_CAPACITY], census_sha256[SHA256_HEX_CAPACITY];
    char population_sha256[SHA256_HEX_CAPACITY], template_sha256[SHA256_HEX_CAPACITY];
    char inventory_sha256[SHA256_HEX_CAPACITY], oracle_attempt_sha256[SHA256_HEX_CAPACITY];
    char gate_sha256[SHA256_HEX_CAPACITY];
} BqRetirementCampaignReady;

static inline void bq_retirement_campaign_ready_release(BqRetirementCampaignReady* ready)
{
    if (ready)
    {
        free(ready->text);
        *ready = (BqRetirementCampaignReady){0};
    }
}

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
static BqError bq_retirement_campaign_ready_import(BqRetirementCampaignUnitStore const* unit,
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
            bq_retirement_unit_ready_value(text, "gate=admitted ", out->gate_sha256) &&
            !strcmp(out->preparation_sha256, prepared->preparation_sha256) &&
            !strcmp(out->template_sha256, prepared->policy.template_sha256) &&
            !strcmp(out->inventory_sha256, prepared->policy.inventory_sha256) ? BQ_OK : BQ_CORRUPT;
    }
    if (directory >= 0 && close(directory) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
    {
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

/* Join the correctness gate the campaign binds to the ready record: A, the
 * support and census digests, both binaries, the population shape and hash,
 * and every reference row's oracle output (strictly increasing rows, no gate
 * row with an oracle the record does not name). The gate's own seal is not
 * yet part of the record: see the interface note in the handoff docs. */
static BqError bq_retirement_campaign_ready_gate(BqRetirementCampaignReady const* ready,
    BqRetirementCorrectness const* gate)
{
    char population[SHA256_HEX_CAPACITY] = {0};
    int ok = ready && ready->owned && ready->text && gate && bq_retirement_correctness_ready(gate) &&
        !strcmp(gate->prepared.preparation_sha256, ready->preparation_sha256) &&
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
    BqError result = ok ? BQ_OK : BQ_SOURCE_MISMATCH;
    return result;
}

/* Hold both binaries again from the record digests the ready record names:
 * the unit's own build import re-verifies every matched-build stage and the
 * binary record, then opens the frozen files. Only a pair whose digests and
 * A are the record's is handed to held. */
static BqError bq_retirement_campaign_ready_held(BqRetirementCampaignUnitStore const* unit,
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

/* The in-unit stand-in for the queue's active MEASURING attempt: this job
 * and attempt's channel holds the MEASURING acknowledgement, the SIGTERM
 * self-pipe is quiet and the absolute deadline has not passed. */
static inline int bq_retirement_campaign_service_measuring(BqPhaseChannel const* phases, uint64_t job_id,
    uint64_t attempt_token, int cancellation_fd, uint64_t deadline_ns)
{
    int ok = phases && job_id && attempt_token && phases->job == job_id && phases->attempt == attempt_token &&
        phases->sequence == BQ_PHASE_MEASURING && phases->last_time &&
        bq_retirement_unit_campaign_live(phases, cancellation_fd, deadline_ns);
    return ok;
}

/* The store-based bind. untimed is the finished untimed record stream the
 * pre-sample context binds; ready receives the imported record (release it).
 * Every refusal releases held, clears binding and poisons both stages. */
static BqError bq_retirement_campaign_service_bind_unit_pinned(BqRetirementCampaignUnitStore const* unit,
    BqPhaseChannel const* phases, int cancellation_fd, uint64_t deadline_ns, uint64_t job_id, uint64_t attempt_token,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY],
    TpRetirementShard const* untimed, BqRetirementCampaignRequest const* request, BqRetirementHeldBinaries* held,
    BqRetirementCampaignBinding* binding, BqRetirementCampaignReady* ready)
{
    bool binding_available = binding && !binding->campaign && !binding->held_binaries;
    bool held_started = false;
    BqRetirementUnitPrepared prepared = {.policy = {.clang = -1, .inventory = -1}};
    BqError result = unit && bq_retirement_campaign_service_measuring(phases, job_id, attempt_token, cancellation_fd,
        deadline_ns) && bq_retirement_campaign_service_request_ready(request) && held && !held->owned &&
        binding_available && ready && !ready->owned ? BQ_OK : BQ_INVALID_TRANSITION;
    if (result == BQ_OK)
    {
        held_started = true;
        *held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
        result = bq_retirement_unit_prepare_pinned(unit->store, unit->workspaces, unit->installed, job_id,
            attempt_token, unit->profile, unit->toolchain_root, preparation_sha256, &prepared);
    }
    if (result == BQ_OK && bq_request_recipe(&prepared.job.request) != BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED)
        result = BQ_INVALID_TRANSITION;
    if (result == BQ_OK) result = bq_retirement_campaign_ready_import(unit, &prepared, ready_sha256, ready);
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_gate_matches(request->gate, preparation_sha256,
                                                             &prepared.preparation);
    if (result == BQ_OK) result = bq_retirement_campaign_ready_gate(ready, request->gate);
    if (result == BQ_OK) result = bq_retirement_campaign_ready_held(unit, &prepared, ready, held);
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
    if (result == BQ_OK &&
        !bq_retirement_campaign_service_measuring(phases, job_id, attempt_token, cancellation_fd, deadline_ns))
        result = BQ_INVALID_TRANSITION;
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_bind_verified(request, unit->profile, job_id, attempt_token, held,
                                                              binding);
    if (prepared.owned && !bq_retirement_unit_release(&prepared) && result == BQ_OK) result = BQ_IO;
    if (result != BQ_OK)
    {
        bq_retirement_campaign_service_refuse(request, held, held_started, binding, binding_available);
        if (ready && ready->owned) bq_retirement_campaign_ready_release(ready);
    }
    return result;
}

/* The compiled profile and installed paths: the blocked profile has no
 * reference, census or campaign pins, so this fails closed. */
static inline BqError bq_retirement_campaign_service_bind_unit(BqRetirementStore store, int workspaces,
    int installed, BqPhaseChannel const* phases, int cancellation_fd, uint64_t deadline_ns, uint64_t job_id,
    uint64_t attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY], TpRetirementShard const* untimed,
    BqRetirementCampaignRequest const* request, BqRetirementHeldBinaries* held, BqRetirementCampaignBinding* binding,
    BqRetirementCampaignReady* ready)
{
    BqRetirementCampaignUnitStore unit = {store, workspaces, installed, S8(BQ_RETIREMENT_STAGE_WORKSPACE_ROOT),
        bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED), S8("full-census"), BQ_RETIREMENT_BUILD_DRIVER,
        BQ_RETIREMENT_TOOLCHAIN_ROOT, BQ_RETIREMENT_UNIT_BROKER, BQ_RETIREMENT_STAGE_WORKSPACE_ROOT};
    BqError result = bq_retirement_campaign_service_bind_unit_pinned(&unit, phases, cancellation_fd, deadline_ns,
        job_id, attempt_token, preparation_sha256, ready_sha256, untimed, request, held, binding, ready);
    return result;
}
#endif
#endif
