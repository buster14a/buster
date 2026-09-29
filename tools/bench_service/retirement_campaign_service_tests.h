/* Focused #881-D queue-aware binding assertions. Included by the preparation
 * test runner after its real miniature source/binary fixture helpers. */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_TESTS_H
#include "retirement_campaign_service.h"

#ifdef __linux__
/* (A1) One timed native link row (census row 6 of 7) is its own singleton
 * batch group. With `object_group`, census rows 4 and 5 also form one timed
 * object batch group bound through the gate's frozen plan-v3 contract; each
 * stage then carries a metrics shard writer. */
typedef struct BqCampaignSampleFixture
{
    TpRetirementExecution execution;
    TpRetirementTranscript transcript;
    TpRetirementSamples samples;
    TpRetirementMetricsShards metrics;
    TpRetirementSampleRow rows[3];
    TpRetirementSampleGroup groups[2];
    unsigned workspace[7], members[3];
    FILE* stream;
    FILE* spool;
    FILE* metrics_stream;
    char* environment[2];
} BqCampaignSampleFixture;

typedef struct BqCampaignServiceFixture
{
    BqRetirementCampaignBinding binding;
    BqRetirementHeldBinaries held;
    BqRetirementCorrectness gate;
    BqRetirementTrustedRow trusted[7];
    BqRetirementRowFact facts[7];
    BqRetirementBatchGroup frozen;
    TpRetirementBatchInput inputs[3];
    TpRetirementBatchContract contract;
    TpRetirementCampaignBudget budget;
    TpRetirementCampaignReview review;
    unsigned group_stages[2];
    TpRetirementCampaign campaign;
    TpRetirementPlan plan;
    BqCampaignSampleFixture samples[2];
    TpRetirementMeasuredCommand commands[2][6];
    char* arguments[2][6][3];
    char command_sha256[2][6][65];
    TpRetirementCampaignCommand command_workspace[12];
    unsigned identity_workspace[3];
    char artifact_sha256[65], code_sha256[65], output_sha256[65], object_output_sha256[65];
    char budget_sha256[65];
    uint8_t assigned[7];
    bool object_group;
} BqCampaignServiceFixture;

static void bq_campaign_service_sample_close(BqCampaignSampleFixture* sample)
{
    if (sample->stream) fclose(sample->stream);
    if (sample->spool) fclose(sample->spool);
    if (sample->metrics_stream) fclose(sample->metrics_stream);
    sample->stream = sample->spool = sample->metrics_stream = NULL;
}

static bool bq_campaign_service_sample_open(BqCampaignSampleFixture* sample,
    uint64_t job_id, uint64_t attempt_token, bool object_group, char const* tag)
{
    char label[129];
    int length = snprintf(label, sizeof(label), "job-%" PRIu64, job_id);
    *sample = (BqCampaignSampleFixture){0};
    sample->stream = tmpfile();
    sample->spool = tmpfile();
    sample->metrics_stream = tmpfile();
    sample->environment[0] = "LC_ALL=C";
    static unsigned const row_ids[] = {6}, row_metrics[] = {0};
    static unsigned const kinds[] = {TP_RETIREMENT_GROUP_SINGLETON}, offsets[] = {0, 1}, members[] = {0};
    static unsigned const object_ids[] = {4, 5, 6}, object_metrics[] = {0, 0, 0};
    static unsigned const object_kinds[] = {TP_RETIREMENT_GROUP_OBJECT, TP_RETIREMENT_GROUP_SINGLETON};
    static unsigned const object_offsets[] = {0, 2, 3}, object_members[] = {0, 1, 2};
    TpRetirementLayout layout = object_group ?
        (TpRetirementLayout){3, 2, object_ids, object_metrics, object_kinds, object_offsets, object_members} :
        (TpRetirementLayout){1, 1, row_ids, row_metrics, kinds, offsets, members};
    unsigned groups = object_group ? 2 : 1;
    bool ok = length > 0 && (size_t)length < sizeof(label) && sample->stream && sample->spool &&
        sample->metrics_stream &&
        tp_retirement_execution_init(&sample->execution, 7, groups, NULL, 0, 7, 60,
            sample->workspace, (size_t)groups * 3) &&
        tp_retirement_transcript_init(&sample->transcript, &sample->execution, label,
            attempt_token, "boot-fixture", 0, 1000) &&
        tp_retirement_transcript_begin_shard(&sample->transcript, sample->stream) &&
        tp_retirement_samples_init(&sample->samples, &sample->transcript, sample->spool,
            &layout, sample->rows, sample->groups, sample->members) &&
        tp_retirement_metrics_shards_init(&sample->metrics, tag, sample->metrics_stream) &&
        (!object_group || tp_retirement_samples_attach_metrics(&sample->samples, &sample->metrics));
    return ok;
}

static void bq_campaign_service_fixture_close(BqCampaignServiceFixture* fixture)
{
    bq_campaign_service_sample_close(&fixture->samples[0]);
    bq_campaign_service_sample_close(&fixture->samples[1]);
    if (fixture->held.owned) bq_retirement_binaries_release(&fixture->held);
}

/* A reviewed fixture budget; the pinned profile names its digest. */
static TpRetirementCampaignBudget bq_campaign_service_budget(void)
{
    TpRetirementCampaignBudget budget = {.reviewed_ns = UINT64_C(36000000000000),
        .reservation_ns = 1000000000, .materialization_ns = 1000000000, .baseline_build_ns = 1000000000,
        .candidate_build_ns = 1000000000, .correctness_ns = 1000000000, .settling_per_stage_ns = 1000000000,
        .aa_qualification_ns = 1000000000, .aa_receipt_sealing_ns = 1000000000,
        .sample_export_per_stage_ns = 1000000000, .final_statistics_ns = 1000000000,
        .final_sealing_ns = 1000000000, .cleanup_ns = 1000000000, .runtime_process_ns = 50000000,
        .metrics_header_bytes = 4096, .metrics_input_bytes = 16384,
        .timed = {2, {{4, 100000000}, {TP_RETIREMENT_BATCH_INPUTS, 2000000000}},
                  {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 90000000, [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 900000000}},
        .untimed = {2, {{4, 150000000}, {TP_RETIREMENT_BATCH_INPUTS, 3000000000}},
                    {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 120000000,
                     [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 1200000000}}};
    return budget;
}

static bool bq_campaign_service_fixture_init(BqCampaignServiceFixture* fixture,
    BqRetirementPreparation const* preparation, BqRetirementBinaries const* binaries,
    char const preparation_sha256[SHA256_HEX_CAPACITY], uint64_t job_id, uint64_t attempt_token,
    bool object_group)
{
    *fixture = (BqCampaignServiceFixture){0};
    fixture->object_group = object_group;
    fixture->held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    static char const artifact[] = "fixture artifact bytes\n";
    static char const code[] = "fixture code bytes\n";
    static char const empty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    static char const control_digest[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    bq_digest(artifact, sizeof(artifact) - 1, (char8*)fixture->artifact_sha256);
    bq_digest(code, sizeof(code) - 1, (char8*)fixture->code_sha256);
    fixture->budget = bq_campaign_service_budget();
    fixture->inputs[0] = (TpRetirementBatchInput){"tests/alpha.c", "ok", "driver.none", empty,
        fixture->artifact_sha256, "alpha.o", 1, 4};
    fixture->inputs[1] = (TpRetirementBatchInput){"tests/beta.c", "ok", "driver.none", empty,
        fixture->artifact_sha256, "beta.o", 1, 5};
    fixture->inputs[2] = (TpRetirementBatchInput){"tests/control.c", "rejected", "driver.analysis",
        control_digest, NULL, NULL, 0, TP_RETIREMENT_BATCH_NO_ROW};
    fixture->contract = (TpRetirementBatchContract){"x86_64-linux", "none", "batch.metrics", fixture->inputs, 3, 1, 0};
    char const* objects[] = {fixture->artifact_sha256};
    bool ok = preparation && binaries && preparation_sha256 &&
        tp_retirement_budget_digest(&fixture->budget, fixture->budget_sha256) &&
        tp_retirement_budget_metrics_bytes(&fixture->budget, 3, &fixture->contract.metrics_bytes_max) &&
        tp_retirement_batch_contract_output(&fixture->contract, fixture->object_output_sha256) &&
        tp_retirement_batch_output_digest(objects, 1, fixture->output_sha256) &&
        bq_campaign_service_sample_open(&fixture->samples[0], job_id, attempt_token, object_group, "aa") &&
        bq_campaign_service_sample_open(&fixture->samples[1], job_id, attempt_token, object_group, "ab");
    /* Dense groups: [object group,] link singleton. */
    fixture->group_stages[0] = object_group ? TP_RETIREMENT_BUDGET_STAGE_OBJECT : TP_RETIREMENT_BUDGET_STAGE_LINK;
    fixture->group_stages[1] = TP_RETIREMENT_BUDGET_STAGE_LINK;
    fixture->review = (TpRetirementCampaignReview){&fixture->budget, fixture->group_stages, NULL, NULL, NULL,
        object_group ? 2u : 1u, 0};
    /* Slots: [object group,] link group, per variant. */
    unsigned link = object_group ? 1 : 0;
    for (u32 stage = 0; ok && stage < 2; stage += 1)
        for (u32 slot = 0; ok && slot <= link; slot += 1)
            for (u32 variant = 0; ok && variant < 2; variant += 1)
            {
                bool batch = slot < link;
                char* argv[] = {variant ? "/fixture/candidate-ide" : "/fixture/base-ide",
                                batch ? "@retirement-inputs.rsp" : variant ? "artifact-right.bin" : "artifact-left.bin",
                                NULL};
                if (!stage && variant) argv[0] = "/fixture/base-ide-second-label";
                u32 index = slot * 2 + variant;
                memcpy(fixture->arguments[stage][index], argv, sizeof(argv));
                TpRetirementMeasuredCommand* command = &fixture->commands[stage][index];
                *command = (TpRetirementMeasuredCommand){.unit = slot, .kind = 0, .variant = variant,
                    .argument_count = 2, .arguments = fixture->arguments[stage][index],
                    .environment = fixture->samples[stage].environment, .environment_count = 1,
                    .directory = "/tmp", .artifact = batch ? NULL : argv[1],
                    .batch = batch ? &fixture->contract : NULL, .timeout_seconds = 2,
                    .command_sha256 = fixture->command_sha256[stage][index],
                    .output_sha256 = batch ? fixture->object_output_sha256 : fixture->output_sha256,
                    .exit_status = batch ? 1 : 0};
                ok = tp_retirement_command_hash(command, fixture->command_sha256[stage][index]);
            }
    if (ok)
    {
        for (u32 row = 0; row < 7; row += 1)
            fixture->trusted[row].row = fixture->facts[row].row = row;
        BqRetirementTrustedRow* trusted = &fixture->trusted[6];
        BqRetirementRowFact* fact = &fixture->facts[6];
        trusted->compiler_eligible = trusted->code_obligation = 1;
        trusted->stage = BQ_RETIREMENT_STAGE_LINK;
        trusted->target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
        fact->compiler_eligible = fact->code_eligible = 1;
        for (u32 side = 0; side < 2; side += 1)
        {
            BqRetirementObservedSide* observed = &fact->side[side];
            memcpy(observed->compiler_command_sha256, fixture->command_sha256[1][link * 2 + side], 65);
            memcpy(observed->artifact_sha256, fixture->artifact_sha256, 65);
            memcpy(observed->code_sha256, fixture->code_sha256, 65);
            observed->code_bytes = sizeof(code) - 1;
        }
        for (u32 row = 4; object_group && row < 6; row += 1)
        {
            memset(fixture->trusted[row].batch_key_sha256, 'c', 64);
            fixture->trusted[row].compiler_eligible = 1;
            fixture->trusted[row].stage = BQ_RETIREMENT_STAGE_OBJECT;
            fixture->trusted[row].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
            fixture->facts[row].compiler_eligible = 1;
            for (u32 side = 0; side < 2; side += 1)
            {
                memcpy(fixture->trusted[row].compiler_command_sha256[side], fixture->command_sha256[1][side], 65);
                memcpy(fixture->facts[row].side[side].compiler_command_sha256, fixture->command_sha256[1][side], 65);
                memcpy(fixture->facts[row].side[side].artifact_sha256, fixture->artifact_sha256, 65);
            }
        }
        BqRetirementPrepared* prepared_gate = &fixture->gate.prepared;
        memcpy(prepared_gate->preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY);
        memset(prepared_gate->support_sha256, '1', 64);
        prepared_gate->support_sha256[64] = 0;
        memset(prepared_gate->census_sha256, '2', 64);
        prepared_gate->census_sha256[64] = 0;
        prepared_gate->rows = 7;
        prepared_gate->object_rows = 7;
        prepared_gate->native_target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
        for (u32 side = 0; side < 2; side += 1)
        {
            ok = ok && !memcmp(preparation->subjects[side].manifest_sha256,
                               binaries->source_sha256[side], SHA256_HEX_CAPACITY);
            memcpy(prepared_gate->source_sha256[side],
                   preparation->subjects[side].manifest_sha256, SHA256_HEX_CAPACITY);
            memcpy(prepared_gate->binary_sha256[side], binaries->binary_sha256[side],
                   SHA256_HEX_CAPACITY);
        }
        /* The v3 A/A second-command aggregate: one entry per timed group,
         * keyed by its smallest row. */
        Sha256 second_hash;
        sha256_init(&second_hash);
        static char const second_domain[] = BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN;
        sha256_add(&second_hash, second_domain, sizeof(second_domain) - 1);
        uint8_t no_runtime = 0;
        if (object_group)
        {
            uint8_t object_ordinal[4] = {4, 0, 0, 0};
            sha256_add(&second_hash, object_ordinal, sizeof(object_ordinal));
            sha256_add(&second_hash, fixture->command_sha256[0][1], 64);
            sha256_add(&second_hash, &no_runtime, sizeof(no_runtime));
        }
        uint8_t ordinal[4] = {6, 0, 0, 0};
        sha256_add(&second_hash, ordinal, sizeof(ordinal));
        sha256_add(&second_hash, fixture->command_sha256[0][link * 2 + 1], 64);
        sha256_add(&second_hash, &no_runtime, sizeof(no_runtime));
        sha256_finish_hex(&second_hash, prepared_gate->aa_second_commands_sha256);
        fixture->gate.rows_done = 7;
        fixture->gate.eligible_rows = object_group ? 3 : 1;
        fixture->gate.trusted_rows = fixture->trusted;
        fixture->gate.facts = fixture->facts;
        fixture->frozen = (BqRetirementBatchGroup){{fixture->contract, fixture->contract}, {{0}}};
        memcpy(fixture->frozen.command_sha256[0], fixture->command_sha256[1][0], 65);
        memcpy(fixture->frozen.command_sha256[1], fixture->command_sha256[1][1], 65);
        if (object_group)
            ok = ok && bq_retirement_correctness_batches(&fixture->gate, &fixture->frozen, 1, fixture->assigned, 7);
        fixture->gate.finished = 1;
        /* (M2) Stand-in for the future #509 importer, the only intended setter. */
        fixture->gate.batch_authority = object_group ? 1u : 0u;
        fixture->plan = (TpRetirementPlan){.version = TP_RETIREMENT_STATISTICS_VERSION,
            .seed = 7, .pairs_per_round = 60, .resamples = TP_RETIREMENT_MIN_RESAMPLES,
            .bootstrap_members_per_scope = 1, .cell_members_per_scope = 1,
            .frozen_before_samples = 1};
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
        ok = ok && bq_retirement_correctness_ready(&fixture->gate);
    }
    return ok;
}

enum
{
    BQ_CAMPAIGN_SERVICE_SINGLETON, BQ_CAMPAIGN_SERVICE_OBJECT_GROUP, BQ_CAMPAIGN_SERVICE_UNFROZEN_OBJECT,
    BQ_CAMPAIGN_SERVICE_UNPINNED_BUDGET, BQ_CAMPAIGN_SERVICE_OTHER_BUDGET, BQ_CAMPAIGN_SERVICE_NO_AUTHORITY,
    BQ_CAMPAIGN_SERVICE_WRONG_STAGE
};

static BqError bq_campaign_service_attempt(BqQueue* queue, uint64_t job_id,
    uint64_t attempt_token, int installed, int workspaces, String8 profile,
    BqRetirementPreparation const* preparation,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    BqRetirementBinaries const* binaries, unsigned mode, BqError expected)
{
    BqCampaignServiceFixture fixture = {0};
    bool ready = bq_campaign_service_fixture_init(&fixture, preparation, binaries,
        preparation_sha256, job_id, attempt_token,
        mode == BQ_CAMPAIGN_SERVICE_OBJECT_GROUP || mode == BQ_CAMPAIGN_SERVICE_NO_AUTHORITY);
    BQ_PREP_CHECK(ready);
    /* (M2) With a pinned profile, object groups still need the gate's #509
     * authority; (M1) a link singleton costed at the self-host stage. */
    if (ready && mode == BQ_CAMPAIGN_SERVICE_NO_AUTHORITY)
    {
        fixture.gate.batch_authority = 0;
        bq_retirement_correctness_seal(&fixture.gate, fixture.gate.sealed_sha256);
        ready = bq_retirement_correctness_ready(&fixture.gate);
        BQ_PREP_CHECK(ready);
    }
    if (mode == BQ_CAMPAIGN_SERVICE_WRONG_STAGE) fixture.group_stages[0] = TP_RETIREMENT_BUDGET_STAGE_SELF_HOST;
    /* (A1) A timed object row outside every frozen batch contract. */
    if (ready && mode == BQ_CAMPAIGN_SERVICE_UNFROZEN_OBJECT)
    {
        fixture.trusted[6].stage = BQ_RETIREMENT_STAGE_OBJECT;
        bq_retirement_correctness_seal(&fixture.gate, fixture.gate.sealed_sha256);
        ready = bq_retirement_correctness_ready(&fixture.gate);
        BQ_PREP_CHECK(ready);
    }
    /* (M4) The compiled profile pins the reviewed budget; the blocked profile
     * has no pin, and a different budget does not match it. */
    char pinned[4096];
    int length = snprintf(pinned, sizeof(pinned), "%.*scampaign-budget-sha256=%s\n", (int)profile.length,
                          (char const*)profile.pointer, fixture.budget_sha256);
    BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(pinned));
    String8 selected = mode == BQ_CAMPAIGN_SERVICE_UNPINNED_BUDGET ? profile :
        (String8){(char8*)pinned, (u64)length};
    TpRetirementCampaignBudget other = fixture.budget;
    char other_sha256[65];
    other.cleanup_ns += 1;
    BQ_PREP_CHECK(tp_retirement_budget_digest(&other, other_sha256));
    if (mode == BQ_CAMPAIGN_SERVICE_OTHER_BUDGET) fixture.review.budget = &other;
    char const* plan_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char const* context_sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    size_t units = fixture.object_group ? 2 : 1;
    BqError result = ready ? bq_retirement_campaign_service_bind_pinned(queue, job_id,
        attempt_token, installed, workspaces, selected, &fixture.gate, &fixture.campaign,
        &fixture.plan, &fixture.samples[0].samples, &fixture.samples[1].samples,
        fixture.commands[0], fixture.commands[1], fixture.command_workspace,
        units * 4, fixture.identity_workspace, units, &fixture.review, plan_sha256, context_sha256,
        &fixture.held, &fixture.binding) : BQ_IO;
    BQ_PREP_CHECK(result == expected);
    if (result == BQ_OK)
        BQ_PREP_CHECK(fixture.binding.campaign == &fixture.campaign &&
            fixture.binding.held_binaries == &fixture.held && fixture.held.owned == 1 &&
            fixture.held.descriptors[0] >= 3 && fixture.held.descriptors[1] >= 3 &&
            fixture.held.descriptors[0] != fixture.held.descriptors[1] &&
            fixture.binding.job_id == job_id && fixture.binding.attempt_token == attempt_token &&
            fixture.campaign.phase == TP_RETIREMENT_CAMPAIGN_AA &&
            fixture.campaign.attempt == attempt_token && fixture.campaign.groups == units &&
            fixture.campaign.capacity.metrics_artifacts_per_stage == (fixture.object_group ? 244u : 0u) &&
            !strcmp(fixture.campaign.budget_sha256, fixture.budget_sha256));
    else
        BQ_PREP_CHECK(!fixture.held.owned &&
            fixture.campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
            fixture.samples[0].samples.failed && fixture.samples[1].samples.failed);
    bq_campaign_service_fixture_close(&fixture);
    return result;
}

static bool bq_campaign_service_request(BqJob* job,
    BqRetirementPreparation const* preparation)
{
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("handoff"),
        S8("native-retirement-performance-v1"),
        string_from_pointer(preparation->subjects[0].commit),
        string_from_pointer(preparation->subjects[1].commit)};
    bool ok = job != NULL;
    for (u32 i = 0; ok && i < BQ_FIELD_COUNT; i += 1)
    {
        ok = fields[i].length <= BQ_REQUEST_CAP - job->request.size - 4;
        if (ok)
        {
            bq_put32(job->request.bytes + job->request.size, (u32)fields[i].length);
            job->request.size += 4;
            memcpy(job->request.bytes + job->request.size, fields[i].pointer,
                   (size_t)fields[i].length);
            job->request.size += (u32)fields[i].length;
        }
    }
    if (ok) bq_request_digest(&job->request, job->digest);
    return ok;
}

static bool bq_campaign_service_mutate_preparation(BqQueue* queue, uint64_t job_id)
{
    char name[48];
    int record = bq_record_name(name, "preparation", job_id) ?
        openat(queue->directory_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool ok = record >= 3 && fchmod(record, 0600) == 0;
    if (record >= 0 && close(record) != 0) ok = false;
    record = ok ? openat(queue->directory_fd, name, O_WRONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && record >= 3 && pwrite(record, "X", 1, 0) == 1 &&
        fsync(record) == 0 && fchmod(record, 0400) == 0;
    if (record >= 0 && close(record) != 0) ok = false;
    return ok;
}

static bool bq_retirement_campaign_service_test(int installed, int workspaces,
    BqRetirementPreparation const* source_preparation, char const* profile)
{
    BUSTER_UNUSED(workspaces);
    BUSTER_UNUSED(bq_retirement_campaign_run);
    BUSTER_UNUSED(tp_retirement_campaign_finish_stage);
    BUSTER_UNUSED(tp_retirement_samples_manifest);
    BUSTER_UNUSED(tp_retirement_samples_finish);
    BUSTER_UNUSED(tp_retirement_samples_write_shard);
    BUSTER_UNUSED(tp_retirement_transcript_receipt);
    BUSTER_UNUSED(tp_retirement_execution_init);
    BUSTER_UNUSED(tp_process);
    BUSTER_UNUSED(tp_first_allowed_cpu);
    BUSTER_UNUSED(tp_absolute);
    BUSTER_UNUSED(tp_mkdir);
    char queue_path[] = "/tmp/bq-retirement-campaign-queue-XXXXXX";
    char workspaces_path[] = "/tmp/bq-retirement-campaign-workspaces-XXXXXX";
    char attempt[64] = {0};
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqRetirementPreparation preparation = source_preparation ? *source_preparation :
        (BqRetirementPreparation){0};
    int root = -1, service_workspaces = -1;
    bool queue_created = mkdtemp(queue_path) != NULL;
    bool workspaces_created = mkdtemp(workspaces_path) != NULL &&
        chmod(workspaces_path, 02710) == 0;
    if (workspaces_created)
        service_workspaces = open(workspaces_path,
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = source_preparation && profile && queue_created && workspaces_created &&
        service_workspaces >= 3 && bq_open(&queue, queue_path) == BQ_OK;
    BQ_PREP_CHECK(ok);
    /* The shared binary readback helper deliberately mutates binaries-1 as
     * part of its negative cases. Use its canonical fixture id and isolate
     * the output workspace from the surrounding preparation tests. */
    const uint64_t job_id = 1, attempt_token = 2;
    BqJob job = {.id = job_id, .token = attempt_token, .phase = BQ_MEASURING};
    if (ok) ok = bq_campaign_service_request(&job, &preparation);
    if (ok)
    {
        queue.state.job_count = 1;
        queue.state.active_id = job.id;
        queue.state.jobs[0] = job;
        job = queue.state.jobs[0];
    }
    BqJob* active = ok ? &queue.state.jobs[0] : NULL;
    if (ok) ok = bq_workspace_name(attempt, active->id, active->token) &&
        mkdirat(service_workspaces, attempt, 0700) == 0;
    root = ok ? openat(service_workspaces, attempt,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && root >= 3;
    for (u32 side = 0; ok && side < 2; side += 1)
    {
        char const* name = side ? "candidate" : "base";
        int subject = mkdirat(root, name, 0700) == 0 ?
            openat(root, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        int source = subject >= 3 && mkdirat(subject, "source", 02750) == 0 ?
            openat(subject, "source", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = source >= 3 && bq_copy_manifest(installed, source,
            bq_field(&active->request, 3 + side), BQ_RETIREMENT_SOURCE_MANIFEST_CAP) &&
            bq_make_sources_read_only(source) &&
            bq_retirement_verify_subject(installed, subject, source,
                bq_field(&active->request, 3 + side), &preparation.subjects[side]);
        if (source >= 3) close(source);
        if (subject >= 3) close(subject);
    }
    BqRetirementStore store = bq_retirement_queue_store(&queue);
    if (ok) ok = bq_retirement_preparation_record(store, active, &preparation, BQ_OK, 2);
    char preparation_sha256[SHA256_HEX_CAPACITY] = {0};
    if (ok) ok = bq_retirement_preparation_ready_pinned(store, active, installed, service_workspaces,
        string_from_pointer(profile), preparation_sha256, NULL) == BQ_OK;
    char binary_sha256[SHA256_HEX_CAPACITY] = {0};
    if (ok) bq_prep_test_binary_handoff(&queue, active, installed, service_workspaces, root, profile,
        preparation_sha256, &preparation, binary_sha256);
    BqRetirementBinaries binaries = {0};
    if (ok) ok = bq_retirement_binaries_import_pinned(&queue, active, installed, service_workspaces,
        string_from_pointer(profile), preparation_sha256, binary_sha256, &binaries) == BQ_OK;
    /* A singleton-only campaign and (A1) timed object rows bound through the
     * gate's frozen batch contract (with the #509 authority stand-in) both
     * bind; an object row without a frozen contract, the unpinned blocked
     * profile, a budget other than the pin, object groups without the gate's
     * #509 authority under a pinned profile, and a singleton costed at
     * another stage fail closed. */
    static unsigned const modes[] = {BQ_CAMPAIGN_SERVICE_SINGLETON, BQ_CAMPAIGN_SERVICE_OBJECT_GROUP,
        BQ_CAMPAIGN_SERVICE_UNFROZEN_OBJECT, BQ_CAMPAIGN_SERVICE_UNPINNED_BUDGET, BQ_CAMPAIGN_SERVICE_OTHER_BUDGET,
        BQ_CAMPAIGN_SERVICE_NO_AUTHORITY, BQ_CAMPAIGN_SERVICE_WRONG_STAGE};
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(modes); index += 1)
    {
        BqError expected = modes[index] <= BQ_CAMPAIGN_SERVICE_OBJECT_GROUP ? BQ_OK : BQ_RECIPE_MISMATCH;
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token, installed,
            service_workspaces, string_from_pointer(profile), &preparation, preparation_sha256,
            &binaries, modes[index], expected) == expected);
    }
    if (ok)
    {
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id + 1, attempt_token,
            installed, service_workspaces, string_from_pointer(profile), &preparation,
            preparation_sha256, &binaries, BQ_CAMPAIGN_SERVICE_SINGLETON, BQ_INVALID_TRANSITION) == BQ_INVALID_TRANSITION);
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token + 1,
            installed, service_workspaces, string_from_pointer(profile), &preparation,
            preparation_sha256, &binaries, BQ_CAMPAIGN_SERVICE_SINGLETON, BQ_INVALID_TRANSITION) == BQ_INVALID_TRANSITION);
        active->phase = BQ_PREPARING;
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token, installed,
            service_workspaces, string_from_pointer(profile), &preparation, preparation_sha256,
            &binaries, BQ_CAMPAIGN_SERVICE_SINGLETON, BQ_INVALID_TRANSITION) == BQ_INVALID_TRANSITION);
        active->phase = BQ_MEASURING;
        BQ_PREP_CHECK(bq_campaign_service_mutate_preparation(&queue, job_id));
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token, installed,
            service_workspaces, string_from_pointer(profile), &preparation, preparation_sha256,
            &binaries, BQ_CAMPAIGN_SERVICE_SINGLETON, BQ_CORRUPT) == BQ_CORRUPT);
    }
    if (root >= 3)
    {
        BQ_PREP_CHECK(bq_remove_workspace_payload(root));
        close(root);
        root = -1;
    }
    if (attempt[0]) BQ_PREP_CHECK(unlinkat(service_workspaces, attempt, AT_REMOVEDIR) == 0);
    bq_close(&queue);
    if (queue_created) bq_prep_test_cleanup(queue_path);
    if (service_workspaces >= 0) close(service_workspaces);
    if (workspaces_created) bq_prep_test_cleanup(workspaces_path);
    return ok;
}
#endif
#endif
