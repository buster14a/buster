/* Focused #881-D queue-aware binding assertions. Included by the preparation
 * test runner after its real miniature source/binary fixture helpers. */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CAMPAIGN_SERVICE_TESTS_H
#include "retirement_campaign_service.h"

#ifdef __linux__
typedef struct BqCampaignSampleFixture
{
    TpRetirementExecution execution;
    TpRetirementTranscript transcript;
    TpRetirementSamples samples;
    TpRetirementSampleRow rows[1];
    unsigned workspace[5], metrics[1];
    FILE* stream;
    FILE* spool;
    char* environment[2];
} BqCampaignSampleFixture;

typedef struct BqCampaignServiceFixture
{
    BqRetirementCampaignBinding binding;
    BqRetirementHeldBinaries held;
    BqRetirementCorrectness gate;
    BqRetirementTrustedRow trusted[7];
    BqRetirementRowFact facts[7];
    TpRetirementCampaign campaign;
    TpRetirementPlan plan;
    BqCampaignSampleFixture samples[2];
    TpRetirementMeasuredCommand commands[2][4];
    char* arguments[2][2][3];
    char command_sha256[2][2][65];
    TpRetirementCampaignCommand command_workspace[8];
    unsigned identity_workspace[3];
    char artifact_sha256[65], code_sha256[65];
} BqCampaignServiceFixture;

static void bq_campaign_service_sample_close(BqCampaignSampleFixture* sample)
{
    if (sample->stream) fclose(sample->stream);
    if (sample->spool) fclose(sample->spool);
    sample->stream = sample->spool = NULL;
}

static bool bq_campaign_service_sample_open(BqCampaignSampleFixture* sample,
    uint64_t job_id, uint64_t attempt_token)
{
    char label[129];
    int length = snprintf(label, sizeof(label), "job-%" PRIu64, job_id);
    *sample = (BqCampaignSampleFixture){0};
    sample->stream = tmpfile();
    sample->spool = tmpfile();
    sample->environment[0] = "LC_ALL=C";
    unsigned row_id = 6;
    sample->metrics[0] = TP_RETIREMENT_SAMPLE_CODE;
    bool ok = length > 0 && (size_t)length < sizeof(label) && sample->stream && sample->spool &&
        tp_retirement_execution_init_rows(&sample->execution, 7, 1, &row_id, 7, NULL, 0, 60,
            sample->workspace, BUSTER_ARRAY_LENGTH(sample->workspace)) &&
        tp_retirement_transcript_init(&sample->transcript, &sample->execution, label,
            attempt_token, "boot-fixture", 0, 1000) &&
        tp_retirement_transcript_begin_shard(&sample->transcript, sample->stream) &&
        tp_retirement_samples_init(&sample->samples, &sample->transcript, sample->spool,
            sample->rows, sample->metrics, 1);
    return ok;
}

static void bq_campaign_service_fixture_close(BqCampaignServiceFixture* fixture)
{
    bq_campaign_service_sample_close(&fixture->samples[0]);
    bq_campaign_service_sample_close(&fixture->samples[1]);
    if (fixture->held.owned) bq_retirement_binaries_release(&fixture->held);
}

static bool bq_campaign_service_fixture_init(BqCampaignServiceFixture* fixture,
    BqRetirementPreparation const* preparation, BqRetirementBinaries const* binaries,
    char const preparation_sha256[SHA256_HEX_CAPACITY], uint64_t job_id, uint64_t attempt_token)
{
    *fixture = (BqCampaignServiceFixture){0};
    fixture->held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    static char const artifact[] = "fixture artifact bytes\n";
    static char const code[] = "fixture code bytes\n";
    bq_digest(artifact, sizeof(artifact) - 1, (char8*)fixture->artifact_sha256);
    bq_digest(code, sizeof(code) - 1, (char8*)fixture->code_sha256);
    bool ok = preparation && binaries && preparation_sha256 &&
        bq_campaign_service_sample_open(&fixture->samples[0], job_id, attempt_token) &&
        bq_campaign_service_sample_open(&fixture->samples[1], job_id, attempt_token);
    for (u32 stage = 0; ok && stage < 2; stage += 1)
        for (u32 variant = 0; ok && variant < 2; variant += 1)
        {
            char* argv[] = {variant ? "/fixture/candidate-ide" : "/fixture/base-ide",
                            variant ? "artifact-right.bin" : "artifact-left.bin", NULL};
            if (!stage && variant) argv[0] = "/fixture/base-ide-second-label";
            memcpy(fixture->arguments[stage][variant], argv, sizeof(argv));
            TpRetirementMeasuredCommand* command = &fixture->commands[stage][variant];
            *command = (TpRetirementMeasuredCommand){.row = 6, .kind = 0, .variant = variant,
                .argument_count = 2, .arguments = fixture->arguments[stage][variant],
                .environment = fixture->samples[stage].environment, .environment_count = 1,
                .directory = "/tmp", .artifact = argv[1], .timeout_seconds = 2,
                .command_sha256 = fixture->command_sha256[stage][variant],
                .output_sha256 = fixture->artifact_sha256,
                .code_section_sha256 = fixture->code_sha256, .code_section_bytes = sizeof(code) - 1};
            ok = tp_retirement_command_hash(command, fixture->command_sha256[stage][variant]);
        }
    if (ok)
    {
        for (u32 row = 0; row < 7; row += 1)
            fixture->trusted[row].row = fixture->facts[row].row = row;
        BqRetirementTrustedRow* trusted = &fixture->trusted[6];
        BqRetirementRowFact* fact = &fixture->facts[6];
        trusted->compiler_eligible = trusted->code_obligation = 1;
        fact->compiler_eligible = fact->code_eligible = 1;
        for (u32 side = 0; side < 2; side += 1)
        {
            BqRetirementObservedSide* observed = &fact->side[side];
            memcpy(observed->compiler_command_sha256, fixture->command_sha256[1][side], 65);
            memcpy(observed->artifact_sha256, fixture->artifact_sha256, 65);
            memcpy(observed->code_sha256, fixture->code_sha256, 65);
            observed->code_bytes = sizeof(code) - 1;
        }
        BqRetirementPrepared* prepared_gate = &fixture->gate.prepared;
        memcpy(prepared_gate->preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY);
        memset(prepared_gate->support_sha256, '1', 64);
        prepared_gate->support_sha256[64] = 0;
        memset(prepared_gate->census_sha256, '2', 64);
        prepared_gate->census_sha256[64] = 0;
        prepared_gate->rows = 7;
        prepared_gate->object_rows = 7;
        prepared_gate->native_target = 1;
        for (u32 side = 0; side < 2; side += 1)
        {
            ok = ok && !memcmp(preparation->subjects[side].manifest_sha256,
                               binaries->source_sha256[side], SHA256_HEX_CAPACITY);
            memcpy(prepared_gate->source_sha256[side],
                   preparation->subjects[side].manifest_sha256, SHA256_HEX_CAPACITY);
            memcpy(prepared_gate->binary_sha256[side], binaries->binary_sha256[side],
                   SHA256_HEX_CAPACITY);
        }
        Sha256 second_hash;
        sha256_init(&second_hash);
        static char const second_domain[] = "bq-retirement-aa-second-commands-v1";
        sha256_add(&second_hash, second_domain, sizeof(second_domain) - 1);
        uint8_t ordinal[4] = {6, 0, 0, 0}, applicable_runtime = 0;
        sha256_add(&second_hash, ordinal, sizeof(ordinal));
        sha256_add(&second_hash, fixture->command_sha256[0][1], 64);
        sha256_add(&second_hash, &applicable_runtime, sizeof(applicable_runtime));
        sha256_finish_hex(&second_hash, prepared_gate->aa_second_commands_sha256);
        fixture->gate.rows_done = 7;
        fixture->gate.eligible_rows = 1;
        fixture->gate.finished = 1;
        fixture->gate.trusted_rows = fixture->trusted;
        fixture->gate.facts = fixture->facts;
        fixture->plan = (TpRetirementPlan){.version = TP_RETIREMENT_STATISTICS_VERSION,
            .seed = 7, .pairs_per_round = 60, .resamples = TP_RETIREMENT_MIN_RESAMPLES,
            .bootstrap_members_per_scope = 1, .cell_members_per_scope = 1,
            .frozen_before_samples = 1};
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
        ok = ok && bq_retirement_correctness_ready(&fixture->gate);
    }
    return ok;
}

static BqError bq_campaign_service_attempt(BqQueue* queue, uint64_t job_id,
    uint64_t attempt_token, int installed, int workspaces, String8 profile,
    BqRetirementPreparation const* preparation,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    BqRetirementBinaries const* binaries, BqError expected)
{
    BqCampaignServiceFixture fixture = {0};
    bool ready = bq_campaign_service_fixture_init(&fixture, preparation, binaries,
        preparation_sha256, job_id, attempt_token);
    BQ_PREP_CHECK(ready);
    char const* plan_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char const* context_sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    BqError result = ready ? bq_retirement_campaign_service_bind_pinned(queue, job_id,
        attempt_token, installed, workspaces, profile, &fixture.gate, &fixture.campaign,
        &fixture.plan, &fixture.samples[0].samples, &fixture.samples[1].samples,
        fixture.commands[0], fixture.commands[1], fixture.command_workspace,
        BUSTER_ARRAY_LENGTH(fixture.command_workspace), fixture.identity_workspace,
        BUSTER_ARRAY_LENGTH(fixture.identity_workspace), plan_sha256, context_sha256,
        &fixture.held, &fixture.binding) : BQ_IO;
    BQ_PREP_CHECK(result == expected);
    if (result == BQ_OK)
        BQ_PREP_CHECK(fixture.binding.campaign == &fixture.campaign &&
            fixture.binding.held_binaries == &fixture.held && fixture.held.owned == 1 &&
            fixture.held.descriptors[0] >= 3 && fixture.held.descriptors[1] >= 3 &&
            fixture.held.descriptors[0] != fixture.held.descriptors[1] &&
            fixture.binding.job_id == job_id && fixture.binding.attempt_token == attempt_token &&
            fixture.campaign.phase == TP_RETIREMENT_CAMPAIGN_AA &&
            fixture.campaign.attempt == attempt_token);
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
    if (ok) ok = bq_retirement_preparation_record(&queue, active, &preparation, BQ_OK, 2);
    char preparation_sha256[SHA256_HEX_CAPACITY] = {0};
    if (ok) ok = bq_retirement_preparation_ready_pinned(&queue, active, installed, service_workspaces,
        string_from_pointer(profile), preparation_sha256, NULL) == BQ_OK;
    char binary_sha256[SHA256_HEX_CAPACITY] = {0};
    if (ok) bq_prep_test_binary_handoff(&queue, active, installed, service_workspaces, root, profile,
        preparation_sha256, &preparation, binary_sha256);
    BqRetirementBinaries binaries = {0};
    if (ok) ok = bq_retirement_binaries_import_pinned(&queue, active, installed, service_workspaces,
        string_from_pointer(profile), preparation_sha256, binary_sha256, &binaries) == BQ_OK;
    if (ok)
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token, installed,
            service_workspaces, string_from_pointer(profile), &preparation, preparation_sha256,
            &binaries, BQ_OK) == BQ_OK);
    if (ok)
    {
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id + 1, attempt_token,
            installed, service_workspaces, string_from_pointer(profile), &preparation,
            preparation_sha256, &binaries, BQ_INVALID_TRANSITION) == BQ_INVALID_TRANSITION);
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token + 1,
            installed, service_workspaces, string_from_pointer(profile), &preparation,
            preparation_sha256, &binaries, BQ_INVALID_TRANSITION) == BQ_INVALID_TRANSITION);
        active->phase = BQ_PREPARING;
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token, installed,
            service_workspaces, string_from_pointer(profile), &preparation, preparation_sha256,
            &binaries, BQ_INVALID_TRANSITION) == BQ_INVALID_TRANSITION);
        active->phase = BQ_MEASURING;
        BQ_PREP_CHECK(bq_campaign_service_mutate_preparation(&queue, job_id));
        BQ_PREP_CHECK(bq_campaign_service_attempt(&queue, job_id, attempt_token, installed,
            service_workspaces, string_from_pointer(profile), &preparation, preparation_sha256,
            &binaries, BQ_CORRUPT) == BQ_CORRUPT);
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
