/* Private functional fixtures for the in-unit #881-D campaign driver
 * (bench_service/retirement_unit_campaign.h). Real fixture children run the
 * whole sequence in order: SETTLING, the untimed production and reproduction
 * batches, MEASURING, the held bind with the plan and pre-sample context
 * derived from test pins, A/A, the fixture A/A admission, the A/B freeze,
 * A/B, the post-sample context and MEASURED, with lane E's store plan made
 * from the frozen capacity before attach. A forked supervisor stand-in
 * acknowledges the phases. The campaign is the one retirement_campaign_test.h
 * binds (object group rows 4 and 5, link singleton row 6 with runtime), plus
 * one untimed singleton group. The admission, ready digest and pins are test
 * data: no #426 verdict, acceptance measurement or real ready record. Include
 * after retirement_campaign_test.h, which compiles the fixture admission. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_UNIT_CAMPAIGN_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_UNIT_CAMPAIGN_TEST_H
#define BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA 1
#include "../bench_service/retirement_unit_campaign.h"

#ifdef __linux__
#include <sys/socket.h>
#include <sys/wait.h>

static char const test_unit_campaign_ready[] = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
static char const test_unit_campaign_receipt[] = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

/* Supervisor stand-in for job 1, attempt 2: acknowledges `answers` phase
 * messages in order; the 1-based message `corrupt` gets a wrong reply. */
static pid_t test_unit_campaign_peer(BqPhaseChannel* channel, unsigned answers, unsigned corrupt)
{
    int pair[2] = {-1, -1};
    int paired = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0;
    pid_t child = paired ? fork() : -1;
    if (child == 0)
    {
        close(pair[0]);
        int ok = 1;
        for (unsigned i = 0; ok && i < answers; ++i)
        {
            unsigned char message[BQ_PHASE_MESSAGE_BYTES] = {0};
            ok = recv(pair[1], message, sizeof(message), 0) == BQ_PHASE_MESSAGE_BYTES;
            bq_phase_put(message + 40, i + 1 == corrupt ? 2u : 1u);
            ok = ok && send(pair[1], message, sizeof(message), MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES;
        }
        close(pair[1]);
        _exit(ok ? 0 : 1);
    }
    if (pair[1] >= 0) close(pair[1]);
    int ready = child > 0 && bq_phase_init(channel, pair[0], 1, 2);
    if (!ready && pair[0] >= 0) close(pair[0]);
    if (!ready) *channel = (BqPhaseChannel){.descriptor = -1, .failed = 1};
    return child;
}

static int test_unit_campaign_peer_join(BqPhaseChannel* channel, pid_t child)
{
    int ok = channel->descriptor < 0 || close(channel->descriptor) == 0;
    channel->descriptor = -1;
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    return ok && waited == child;
}

typedef struct TestUnitCampaign
{
    char directory[TP_PATH_CAP], binary_path[TP_PATH_CAP], candidate_path[TP_PATH_CAP];
    char list_leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP], list_argument[TP_RETIREMENT_INPUT_LIST_LEAF_CAP + 1];
    char leak_text[32], binary_sha[65], artifact_sha[65], code_sha[65], runtime_sha[65];
    char batch_output[65], object_output[65], budget_sha[65], profile[1024];
    char* environment[3];
    char* arguments[2][6][11];
    char command_sha[2][6][65];
    char* untimed_arguments[2][7];
    char untimed_sha[2][65];
    TpRetirementMeasuredCommand commands[2][6];
    TpRetirementUntimedBatch untimed[4];
    TestBatchFixture batch;
    TpRetirementCampaignBudget budget;
    TpRetirementCampaignReview review;
    unsigned group_stages[2], untimed_inputs[1], untimed_kinds[1], untimed_stages[1];
    BqRetirementTrustedRow trusted[7];
    BqRetirementRowFact facts[7];
    BqRetirementBatchGroup frozen;
    BqRetirementCorrectness gate;
    BqRetirementHeldBinaries held;
    BqRetirementUnitCampaignPins pins;
    uint8_t assigned[7];
    int binary, candidate, cwd, leak, cpu;
} TestUnitCampaign;

/* The campaign test's two stages with a real pre-sample binding time, so the
 * untimed batches (already finished) precede the timed window. */
static int test_unit_campaign_open_stage(TpSampleTest* test, int cpu, uint64_t bound, TpRetirementMetricsShards* metrics,
    FILE** metrics_stream, char const* tag)
{
    static unsigned const ids[] = {4, 5, 6}, row_metrics[] = {0, 0, TP_RETIREMENT_SAMPLE_RUNTIME};
    static unsigned const kinds[] = {TP_RETIREMENT_GROUP_OBJECT, TP_RETIREMENT_GROUP_SINGLETON};
    static unsigned const offsets[] = {0, 2, 3}, members[] = {0, 1, 2};
    memset(test, 0, sizeof(*test));
    test->stream = tmpfile();
    test->spool = tmpfile();
    *metrics_stream = tmpfile();
    memcpy(test->row_ids, ids, sizeof(ids));
    memcpy(test->row_metrics, row_metrics, sizeof(row_metrics));
    memcpy(test->kinds, kinds, sizeof(kinds));
    memcpy(test->offsets, offsets, sizeof(offsets));
    memcpy(test->layout_members, members, sizeof(members));
    test->runtime[0] = 6;
    test->runtime_count = 1;
    test->group_count = 2;
    test->population = 7;
    test->layout = (TpRetirementLayout){3, 2, test->row_ids, test->row_metrics, test->kinds, test->offsets,
        test->layout_members};
    int ok = test->stream && test->spool && *metrics_stream &&
        tp_retirement_execution_init(&test->execution, 1, 2, test->runtime, 1, 7, 60, test->workspace, 7) &&
        tp_retirement_transcript_init(&test->transcript, &test->execution, "job-1", 2, "boot-123", cpu, bound) &&
        tp_retirement_transcript_begin_shard(&test->transcript, test->stream) &&
        tp_retirement_samples_init(&test->samples, &test->transcript, test->spool, &test->layout, test->rows,
            test->groups, test->members) &&
        tp_retirement_metrics_shards_init(metrics, tag, *metrics_stream) &&
        tp_retirement_samples_attach_metrics(&test->samples, metrics);
    return ok;
}

static int test_unit_campaign_open(TestCampaignStages* stages, int cpu, uint64_t bound)
{
    int ok = test_unit_campaign_open_stage(&stages->aa, cpu, bound, &stages->metrics[0], &stages->metrics_streams[0],
            "aa") &&
        test_unit_campaign_open_stage(&stages->ab, cpu, bound, &stages->metrics[1], &stages->metrics_streams[1], "ab");
    return ok;
}

/* Remove what a failed scenario kept as evidence, so the next one starts on
 * an empty scratch directory (besides its markers and response file). */
static void test_unit_campaign_scrub(int cwd)
{
    static char const* const leaves[] = {"unit-campaign-untimed.log", "unit-campaign-aa.log", "unit-campaign-ab.log",
        "untimed-left.bin", "untimed-right.bin", "artifact-left.bin", "artifact-right.bin", "alpha.o", "beta.o",
        "batch.metrics"};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(leaves); ++i) unlinkat(cwd, leaves[i], 0);
}

/* The campaign test's commands, gate and held pair, the untimed singleton
 * group's four batches, and the test profile's pins. */
static int test_unit_campaign_setup(TestUnitCampaign* fixture, char const* executable_path, char const* root)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->binary = fixture->candidate = fixture->cwd = fixture->leak = -1;
    int ok = tp_path(fixture->directory, root, "unit-campaign-fixture") && tp_mkdirs(fixture->directory) &&
        chmod(fixture->directory, 0700) == 0 && test_text(fixture->directory, "cwd-marker", "fixed cwd\n") &&
        tp_path(fixture->binary_path, fixture->directory, "fixture-child") &&
        tp_copy_file(executable_path, fixture->binary_path) &&
        tp_path(fixture->candidate_path, fixture->directory, "fixture-candidate-child") &&
        tp_copy_file(executable_path, fixture->candidate_path) && chmod(fixture->binary_path, 0500) == 0 &&
        chmod(fixture->candidate_path, 0500) == 0;
    if (ok)
    {
        fixture->binary = open(fixture->binary_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        fixture->candidate = open(fixture->candidate_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        fixture->cwd = open(fixture->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        fixture->leak = fixture->binary >= 3 ? fcntl(fixture->binary, F_DUPFD, 512) : -1;
    }
    uint64_t binary_bytes = 0;
    ok = ok && fixture->binary >= 3 && fixture->candidate >= 3 && fixture->cwd >= 3 && fixture->leak >= 3 &&
        tp_retirement_file_hash(fixture->binary, fixture->binary_sha, &binary_bytes) && binary_bytes;
    snprintf(fixture->leak_text, sizeof(fixture->leak_text), "%d", fixture->leak);
    unsigned char artifact[1024];
    unsigned artifact_bytes = test_artifact_fixture(artifact, 1, 1);
    Sha256 hash;
    sha256_init(&hash); sha256_add(&hash, artifact, artifact_bytes); sha256_finish_hex(&hash, fixture->artifact_sha);
    sha256_init(&hash); sha256_add(&hash, "fixture-code\n", 13); sha256_finish_hex(&hash, fixture->code_sha);
    memcpy(fixture->runtime_sha, fixture->code_sha, 65);
    char const* objects[] = {fixture->artifact_sha};
    fixture->budget = test_retirement_budget();
    ok = ok && tp_retirement_batch_output_digest(objects, 1, fixture->batch_output) &&
        tp_retirement_budget_digest(&fixture->budget, fixture->budget_sha);
    fixture->group_stages[0] = TP_RETIREMENT_BUDGET_STAGE_OBJECT;
    fixture->group_stages[1] = TP_RETIREMENT_BUDGET_STAGE_LINK;
    fixture->untimed_inputs[0] = 1;
    fixture->untimed_kinds[0] = TP_RETIREMENT_GROUP_SINGLETON;
    fixture->untimed_stages[0] = TP_RETIREMENT_BUDGET_STAGE_LINK;
    fixture->review = (TpRetirementCampaignReview){&fixture->budget, fixture->group_stages, fixture->untimed_inputs,
        fixture->untimed_kinds, fixture->untimed_stages, 2, 1};
    char* words[] = {"+alpha", "+beta", "-control"};
    ok = ok && test_batch_contract(&fixture->batch, "batch.metrics", words, 3, fixture->artifact_sha);
    fixture->batch.inputs[0].row = 4;
    fixture->batch.inputs[1].row = 5;
    ok = ok && tp_retirement_budget_metrics_bytes(&fixture->budget, 3, &fixture->batch.contract.metrics_bytes_max) &&
        tp_retirement_batch_contract_valid(&fixture->batch.contract) &&
        tp_retirement_batch_contract_output(&fixture->batch.contract, fixture->object_output) &&
        tp_retirement_batch_input_list_write(fixture->cwd, &fixture->batch.contract, fixture->list_leaf);
    snprintf(fixture->list_argument, sizeof(fixture->list_argument), "@%s", fixture->list_leaf);
    fixture->environment[0] = "LC_ALL=C";
    fixture->environment[1] = "TP_RETIREMENT_TEST=explicit";
    /* Per stage: [object group baseline, candidate, link group baseline,
     * candidate, runtime row baseline, candidate]; G = 2, U = 1. */
    for (unsigned stage = 0; ok && stage < 2; ++stage)
        for (unsigned slot = 0; ok && slot < 3; ++slot)
            for (unsigned variant = 0; ok && variant < 2; ++variant)
            {
                unsigned index = slot * 2 + variant;
                char** argv = fixture->arguments[stage][index];
                argv[0] = variant ? (stage ? fixture->candidate_path : "fixture-child-label-2") : fixture->binary_path;
                argv[1] = "retirement-child";
                unsigned count = 6;
                if (!slot)
                {
                    char* batch_argv[] = {"batch", "batch.metrics", "ok", fixture->leak_text, fixture->list_argument,
                        "+alpha", "+beta", "-control"};
                    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(batch_argv); ++i) argv[2 + i] = batch_argv[i];
                    count = 10;
                }
                else
                {
                    argv[2] = slot == 2 ? "runtime" : "compiler";
                    argv[3] = variant ? "artifact-right.bin" : "artifact-left.bin";
                    argv[4] = "ok";
                    argv[5] = fixture->leak_text;
                }
                fixture->commands[stage][index] = (TpRetirementMeasuredCommand){.unit = slot == 2 ? 6 : slot,
                    .kind = slot == 2, .variant = variant, .arguments = argv, .argument_count = count,
                    .environment = fixture->environment, .environment_count = 2, .directory = fixture->directory,
                    .artifact = slot == 1 ? argv[3] : NULL, .batch = slot ? NULL : &fixture->batch.contract,
                    .timeout_seconds = 2, .command_sha256 = fixture->command_sha[stage][index],
                    .output_sha256 = slot == 2 ? fixture->runtime_sha : slot ? fixture->batch_output :
                                                                              fixture->object_output,
                    .exit_status = slot ? 0 : 1};
                ok = tp_retirement_command_hash(&fixture->commands[stage][index], fixture->command_sha[stage][index]);
            }
    /* The untimed singleton group 0: a production and a reproduction batch
     * per variant, each writing its own artifact leaf. */
    for (unsigned variant = 0; ok && variant < 2; ++variant)
    {
        char** argv = fixture->untimed_arguments[variant];
        argv[0] = variant ? fixture->candidate_path : fixture->binary_path;
        argv[1] = "retirement-child";
        argv[2] = "compiler";
        argv[3] = variant ? "untimed-right.bin" : "untimed-left.bin";
        argv[4] = "ok";
        argv[5] = fixture->leak_text;
        TpRetirementMeasuredCommand command = {.unit = 0, .kind = 0, .variant = variant, .arguments = argv,
            .argument_count = 6, .environment = fixture->environment, .environment_count = 2,
            .directory = fixture->directory, .artifact = argv[3], .timeout_seconds = 2,
            .command_sha256 = fixture->untimed_sha[variant], .output_sha256 = fixture->batch_output};
        ok = tp_retirement_command_hash(&command, fixture->untimed_sha[variant]);
        for (unsigned purpose = 0; ok && purpose < 2; ++purpose)
            fixture->untimed[variant * 2 + purpose] = (TpRetirementUntimedBatch){command, 0, variant, purpose,
                TP_RETIREMENT_GROUP_SINGLETON};
    }
    /* The sealed gate: rows 4 and 5 in the frozen object group, row 6 the
     * native link singleton with runtime; the #509 batch authority is the
     * test stand-in, as in retirement_campaign_test.h. */
    static char const identity[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    static char const batch_key[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    BqRetirementCorrectness* gate = &fixture->gate;
    gate->prepared.rows = gate->rows_done = 7;
    gate->prepared.native_target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
    gate->eligible_rows = 3;
    gate->trusted_rows = fixture->trusted;
    gate->facts = fixture->facts;
    memcpy(gate->prepared.preparation_sha256, identity, 65);
    memcpy(gate->prepared.source_sha256[0], identity, 65);
    memcpy(gate->prepared.source_sha256[1], identity, 65);
    memcpy(gate->prepared.binary_sha256[0], fixture->binary_sha, 65);
    memcpy(gate->prepared.binary_sha256[1], fixture->binary_sha, 65);
    for (unsigned row = 0; row < 7; ++row) fixture->trusted[row].row = fixture->facts[row].row = row;
    for (unsigned row = 4; ok && row < 6; ++row)
    {
        fixture->trusted[row].compiler_eligible = 1;
        fixture->trusted[row].stage = BQ_RETIREMENT_STAGE_OBJECT;
        fixture->trusted[row].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
        memcpy(fixture->trusted[row].batch_key_sha256, batch_key, sizeof(batch_key));
        fixture->facts[row].compiler_eligible = 1;
        for (unsigned variant = 0; variant < 2; ++variant)
        {
            memcpy(fixture->trusted[row].compiler_command_sha256[variant], fixture->command_sha[1][variant], 65);
            memcpy(fixture->facts[row].side[variant].compiler_command_sha256, fixture->command_sha[1][variant], 65);
            memcpy(fixture->facts[row].side[variant].artifact_sha256, fixture->artifact_sha, 65);
        }
    }
    fixture->trusted[6].compiler_eligible = fixture->trusted[6].code_obligation = 1;
    fixture->trusted[6].stage = BQ_RETIREMENT_STAGE_LINK;
    fixture->trusted[6].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
    fixture->facts[6].runtime_eligible = fixture->facts[6].code_eligible = 1;
    memcpy(fixture->trusted[6].independent_oracle_sha256, fixture->runtime_sha, 65);
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        BqRetirementObservedSide* side = &fixture->facts[6].side[variant];
        memcpy(side->compiler_command_sha256, fixture->command_sha[1][2 + variant], 65);
        memcpy(side->artifact_sha256, fixture->artifact_sha, 65);
        memcpy(side->code_sha256, fixture->code_sha, 65);
        memcpy(side->runtime_command_sha256, fixture->command_sha[1][4 + variant], 65);
        side->code_bytes = 13;
    }
    fixture->frozen = (BqRetirementBatchGroup){{fixture->batch.contract, fixture->batch.contract}, {{0}}};
    memcpy(fixture->frozen.command_sha256[0], fixture->command_sha[1][0], 65);
    memcpy(fixture->frozen.command_sha256[1], fixture->command_sha[1][1], 65);
    ok = ok && bq_retirement_correctness_batches(gate, &fixture->frozen, 1, fixture->assigned, 7);
    gate->finished = 1;
    gate->batch_authority = 1;
    sha256_init(&hash);
    static char const second_domain[] = BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN;
    sha256_add(&hash, second_domain, sizeof(second_domain) - 1);
    uint8_t object_ordinal[4] = {4, 0, 0, 0}, link_ordinal[4] = {6, 0, 0, 0}, no_runtime = 0, applicable_runtime = 1;
    sha256_add(&hash, object_ordinal, sizeof(object_ordinal));
    sha256_add(&hash, fixture->command_sha[0][1], 64);
    sha256_add(&hash, &no_runtime, sizeof(no_runtime));
    sha256_add(&hash, link_ordinal, sizeof(link_ordinal));
    sha256_add(&hash, fixture->command_sha[0][3], 64);
    sha256_add(&hash, &applicable_runtime, sizeof(applicable_runtime));
    sha256_add(&hash, fixture->command_sha[0][5], 64);
    sha256_finish_hex(&hash, gate->prepared.aa_second_commands_sha256);
    bq_retirement_correctness_seal(gate, gate->sealed_sha256);
    ok = ok && bq_retirement_correctness_ready(gate);
    fixture->held = (BqRetirementHeldBinaries){.descriptors = {fixture->binary, fixture->candidate}, .owned = 1};
    memcpy(fixture->held.verified.preparation_sha256, identity, 65);
    memcpy(fixture->held.verified.directory_identity_sha256, identity, 65);
    memcpy(fixture->held.verified.source_sha256, gate->prepared.source_sha256, sizeof(fixture->held.verified.source_sha256));
    memcpy(fixture->held.verified.binary_sha256, gate->prepared.binary_sha256, sizeof(fixture->held.verified.binary_sha256));
    ok = ok && bq_retirement_campaign_descriptor_identity(fixture->binary, fixture->held.verified.binary_identity_sha256[0]) &&
        bq_retirement_campaign_descriptor_identity(fixture->candidate, fixture->held.verified.binary_identity_sha256[1]);
    int length = snprintf(fixture->profile, sizeof(fixture->profile),
        "schema=1\nrecipe=native-retirement-performance-v1\ncampaign-seed=1\ncampaign-pairs=60\n"
        "campaign-resamples=100000\ncampaign-bootstrap-members=5\ncampaign-budget-sha256=%s\n", fixture->budget_sha);
    ok = ok && length > 0 && (size_t)length < sizeof(fixture->profile) &&
        bq_retirement_unit_campaign_pins(string_from_pointer(fixture->profile), &fixture->pins);
    fixture->cpu = tp_first_allowed_cpu();
    return ok && fixture->cpu >= 0;
}

static void test_unit_campaign_teardown(TestUnitCampaign* fixture)
{
    if (fixture->cwd >= 3)
    {
        test_unit_campaign_scrub(fixture->cwd);
        if (fixture->list_leaf[0]) CHECK(unlinkat(fixture->cwd, fixture->list_leaf, 0) == 0);
        CHECK(close(fixture->cwd) == 0);
    }
    if (fixture->leak >= 3) CHECK(close(fixture->leak) == 0);
    if (fixture->candidate >= 3) CHECK(close(fixture->candidate) == 0);
    if (fixture->binary >= 3) CHECK(close(fixture->binary) == 0);
}

/* Plan and context: pins from the profile only (the blocked profile has
 * none), the cell count derived from the gate, a candidate-independent plan
 * digest, and a pre-sample context bound to the subjects. */
static void test_unit_campaign_plan(TestUnitCampaign* fixture)
{
    BqRetirementUnitCampaignPins pins;
    CHECK(fixture->pins.seed == 1 && fixture->pins.pairs == 60 && fixture->pins.resamples == 100000 &&
          fixture->pins.bootstrap_members == 5 && !strcmp(fixture->pins.budget_sha256, fixture->budget_sha));
    /* Missing, duplicate, non-canonical or out-of-range pins fail closed. */
    char const* const edits[][2] = {
        {"campaign-seed=1\n", ""}, {"campaign-pairs=60\n", ""}, {"campaign-resamples=100000\n", ""},
        {"campaign-bootstrap-members=5\n", ""}, {"campaign-budget-sha256=", "campaign-budget=",},
        {"campaign-seed=1\n", "campaign-seed=1\ncampaign-seed=1\n"}, {"campaign-seed=1\n", "campaign-seed=0\n"},
        {"campaign-seed=1\n", "campaign-seed=01\n"}, {"campaign-seed=1\n", "campaign-seed=18446744073709551616\n"},
        {"campaign-pairs=60\n", "campaign-pairs=61\n"}, {"campaign-pairs=60\n", "campaign-pairs=58\n"},
        {"campaign-pairs=60\n", "campaign-pairs=256\n"}, {"campaign-pairs=60\n", "campaign-pairs=+60\n"},
        {"campaign-resamples=100000\n", "campaign-resamples=99999\n"},
        {"campaign-resamples=100000\n", "campaign-resamples=1000001\n"},
        {"campaign-bootstrap-members=5\n", "campaign-bootstrap-members=0\n"},
        {"campaign-bootstrap-members=5\n", "campaign-bootstrap-members=81\n"}};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(edits); ++i)
    {
        char mutated[1100];
        char const* at = strstr(fixture->profile, edits[i][0]);
        CHECK(at != NULL);
        if (!at) continue;
        size_t prefix = (size_t)(at - fixture->profile), old_length = strlen(edits[i][0]);
        int length = snprintf(mutated, sizeof(mutated), "%.*s%s%s", (int)prefix, fixture->profile, edits[i][1],
            at + old_length);
        CHECK(length > 0 && !bq_retirement_unit_campaign_pins(string_from_pointer(mutated), &pins) && !pins.seed &&
              !pins.pairs);
    }
    CHECK(!bq_retirement_unit_campaign_pins(S8("schema=1\nrecipe=native-retirement-performance-v1\nstatus=blocked\n"),
                                            &pins));
    CHECK(bq_retirement_unit_campaign_pins(string_from_pointer(fixture->profile), &pins) &&
          !memcmp(&pins, &fixture->pins, sizeof(pins)));
    pins.pairs = 254;
    TpRetirementPlan plan, other;
    char digest[65], changed[65];
    /* Exact cells: 2 * 3 timed rows + 1 runtime row + 2 * 1 object group. */
    CHECK(bq_retirement_unit_campaign_plan(&fixture->gate, &fixture->pins, &plan) && plan.seed == 1 &&
          plan.pairs_per_round == 60 && plan.resamples == 100000 && plan.bootstrap_members_per_scope == 5 &&
          plan.cell_members_per_scope == 9 && plan.frozen_before_samples == 1 &&
          plan.version == TP_RETIREMENT_STATISTICS_VERSION);
    CHECK(bq_retirement_unit_campaign_plan(&fixture->gate, &pins, &other) && other.pairs_per_round == 254);
    CHECK(bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, digest) &&
          bq_retirement_unit_campaign_plan_digest(&fixture->gate, &other, changed) && strcmp(digest, changed));
    /* A changed candidate binary reseals the gate but leaves the plan digest;
     * a changed #508 row identity changes it; an unready gate derives none. */
    char saved = fixture->gate.prepared.binary_sha256[1][0];
    fixture->gate.prepared.binary_sha256[1][0] = saved == 'e' ? 'f' : 'e';
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, changed) && !strcmp(digest, changed));
    fixture->gate.prepared.binary_sha256[1][0] = saved;
    fixture->trusted[5].identity_sha256[0] = '1';
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, changed) && strcmp(digest, changed));
    fixture->trusted[5].identity_sha256[0] = 0;
    CHECK(!bq_retirement_unit_campaign_plan(&fixture->gate, &fixture->pins, &other) && !other.seed &&
          !bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, changed) && !changed[0]);
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&fixture->gate));
    /* The pre-sample context binds the subjects the plan does not. */
    TpRetirementShard untimed = {100, 4, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    char commands[65], context[65], context_changed[65];
    CHECK(bq_retirement_unit_campaign_commands_measured(fixture->commands[0], fixture->commands[1], 6, commands));
    BqRetirementUnitCampaignFacts facts = {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed,
        "job-1", "boot-123", 2, 1000, 0};
    CHECK(bq_retirement_unit_campaign_pre_context(&fixture->gate, &facts, context));
    fixture->gate.prepared.binary_sha256[1][0] = saved == 'e' ? 'f' : 'e';
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_unit_campaign_pre_context(&fixture->gate, &facts, context_changed) &&
          strcmp(context, context_changed));
    fixture->gate.prepared.binary_sha256[1][0] = saved;
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    BqRetirementUnitCampaignFacts const variants[] = {
        {test_unit_campaign_receipt, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-1",
         "boot-123", 2, 1000, 0},
        {digest, test_unit_campaign_receipt, fixture->budget_sha, commands, &untimed, "job-1", "boot-123", 2, 1000, 0},
        {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-2", "boot-123", 2, 1000, 0},
        {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-1", "boot-123", 3, 1000, 0},
        {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-1", "boot-123", 2, 1001, 0}};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(variants); ++i)
        CHECK(bq_retirement_unit_campaign_pre_context(&fixture->gate, &variants[i], context_changed) &&
              strcmp(context, context_changed));
    TpRetirementShard no_untimed = {0, 0, ""};
    facts.untimed = &no_untimed;
    CHECK(!bq_retirement_unit_campaign_pre_context(&fixture->gate, &facts, context_changed) && !context_changed[0]);
    /* A changed command changes the commands digest. */
    fixture->arguments[1][1][0] = "fixture-candidate-child-other";
    CHECK(tp_retirement_command_hash(&fixture->commands[1][1], fixture->command_sha[1][1]) &&
          bq_retirement_unit_campaign_commands_measured(fixture->commands[0], fixture->commands[1], 6, changed) &&
          strcmp(changed, commands));
    fixture->arguments[1][1][0] = fixture->candidate_path;
    CHECK(tp_retirement_command_hash(&fixture->commands[1][1], fixture->command_sha[1][1]));
}

enum
{
    TEST_UNIT_CAMPAIGN_COMPLETE, TEST_UNIT_CAMPAIGN_UNTIMED_SHAPE, TEST_UNIT_CAMPAIGN_SWAPPED_HELD,
    TEST_UNIT_CAMPAIGN_MEASURING_ACK, TEST_UNIT_CAMPAIGN_LATE_BINDING, TEST_UNIT_CAMPAIGN_PLAN_OVERRIDE,
    TEST_UNIT_CAMPAIGN_CONTEXT_OVERRIDE, TEST_UNIT_CAMPAIGN_EARLY_FREEZE, TEST_UNIT_CAMPAIGN_CANCELLED,
    TEST_UNIT_CAMPAIGN_AB_WITHOUT_ADMISSION, TEST_UNIT_CAMPAIGN_DENIED, TEST_UNIT_CAMPAIGN_STALE_ADMISSION,
    TEST_UNIT_CAMPAIGN_AB_WITHOUT_FREEZE, TEST_UNIT_CAMPAIGN_UNPLANNED_STORE, TEST_UNIT_CAMPAIGN_SHORT_STORE,
    TEST_UNIT_CAMPAIGN_SCENARIOS
};

/* One attempt through the driver. Each fault stops the sequence at its step
 * and must leave no further launch; the complete run must reach MEASURED. */
static void test_unit_campaign_attempt(TestUnitCampaign* fixture, unsigned scenario)
{
    BqPhaseChannel phases;
    unsigned corrupt = scenario == TEST_UNIT_CAMPAIGN_MEASURING_ACK ? 3u : 0u;
    pid_t peer = test_unit_campaign_peer(&phases, 4, corrupt);
    int cancel[2] = {-1, -1};
    CHECK(peer > 0 && pipe2(cancel, O_CLOEXEC | O_NONBLOCK) == 0 && bq_phase_exchange(&phases, BQ_PHASE_PREPARING));
    uint64_t start = tp_process_monotonic_ns();
    BqRetirementUnitCampaign driver = {0};
    CHECK(bq_retirement_unit_campaign_begin(&driver, &phases, cancel[0], bq_phase_clock() + UINT64_C(600000000000),
                                            fixture->cwd, fixture->cwd) &&
          phases.sequence == BQ_PHASE_SETTLING);
    FILE* records = tmpfile();
    unsigned char reproduced[2];
    TpRetirementUntimed untimed;
    CHECK(records && tp_retirement_untimed_init(&untimed, records, NULL, &fixture->budget, 1, reproduced, "job-1", 2,
                                                "boot-123", fixture->cpu, start, UINT64_MAX - 1, 0));
    BqRetirementUnitCampaignStreams none = {0};
    BqRetirementHeldBinaries held = fixture->held;
    if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_SHAPE) fixture->untimed_inputs[0] = 2;
    if (scenario == TEST_UNIT_CAMPAIGN_SWAPPED_HELD)
    {
        held.descriptors[0] = fixture->candidate;
        held.descriptors[1] = fixture->binary;
    }
    int ok = bq_retirement_unit_campaign_untimed(&driver, &untimed, fixture->untimed, 4, &held, &fixture->review, &none);
    fixture->untimed_inputs[0] = 1;
    if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_SHAPE || scenario == TEST_UNIT_CAMPAIGN_SWAPPED_HELD)
        CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && !driver.launches[0] && untimed.failed &&
              !bq_retirement_unit_campaign_measuring(&driver) && phases.sequence == BQ_PHASE_SETTLING);
    else
        CHECK(ok && driver.launches[0] == 4 && driver.untimed_records.records == 4 &&
              driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_UNTIMED);
    ok = ok && bq_retirement_unit_campaign_measuring(&driver);
    if (scenario == TEST_UNIT_CAMPAIGN_MEASURING_ACK)
        CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && phases.failed &&
              phases.sequence == BQ_PHASE_SETTLING);
    TestCampaignStages stages;
    memset(&stages, 0, sizeof(stages));
    uint64_t bound = scenario == TEST_UNIT_CAMPAIGN_LATE_BINDING ? start + 1 : tp_process_monotonic_ns();
    TpRetirementCampaign campaign = {0};
    BqRetirementCampaignBinding binding = {0};
    TpRetirementCampaignCommand snapshots[12];
    unsigned identities[3];
    TpRetirementPlan plan = {0};
    char plan_sha[65] = {0}, context_sha[65] = {0};
    if (ok)
    {
        CHECK(phases.sequence == BQ_PHASE_MEASURING && test_unit_campaign_open(&stages, fixture->cpu, bound));
        CHECK(bq_retirement_unit_campaign_derive(&fixture->gate, &fixture->pins, test_unit_campaign_ready,
            &driver.untimed_records, &stages.aa.samples, &stages.ab.samples, fixture->commands[0], fixture->commands[1],
            6, &plan, plan_sha, context_sha));
        TpRetirementPlan bound_plan = plan;
        if (scenario == TEST_UNIT_CAMPAIGN_PLAN_OVERRIDE) bound_plan.cell_members_per_scope = 1;
        char const* bound_context = scenario == TEST_UNIT_CAMPAIGN_CONTEXT_OVERRIDE ? test_unit_campaign_receipt :
                                                                                      context_sha;
        CHECK(bq_retirement_campaign_bind_held(&binding, &fixture->gate, &campaign, &bound_plan, &stages.aa.samples,
            &stages.ab.samples, &fixture->held, 1, 2, fixture->commands[0], fixture->commands[1], snapshots, 12,
            identities, 3, &fixture->review, fixture->budget_sha, plan_sha, bound_context));
        /* Lane E's pre-timing store plan over the frozen capacity: one
         * owned receipt file at its bound and the minimum external entries. */
        TpRetirementCampaignStorePlan store_plan = {0};
        CHECK(tp_retirement_campaign_store_preflight(&campaign.capacity, 1, TP_RETIREMENT_RECEIPT_BYTES, 3, 0,
                                                     &store_plan));
        if (scenario == TEST_UNIT_CAMPAIGN_SHORT_STORE)
        {
            store_plan.owned_bytes -= TP_RETIREMENT_RECEIPT_BYTES;
            store_plan.bytes -= TP_RETIREMENT_RECEIPT_BYTES;
            store_plan.remaining_bytes += TP_RETIREMENT_RECEIPT_BYTES;
        }
        ok = bq_retirement_unit_campaign_attach(&driver, &binding, &fixture->pins, test_unit_campaign_ready,
                                                scenario == TEST_UNIT_CAMPAIGN_UNPLANNED_STORE ? NULL : &store_plan);
        if (scenario == TEST_UNIT_CAMPAIGN_LATE_BINDING || scenario == TEST_UNIT_CAMPAIGN_PLAN_OVERRIDE ||
            scenario == TEST_UNIT_CAMPAIGN_CONTEXT_OVERRIDE || scenario == TEST_UNIT_CAMPAIGN_UNPLANNED_STORE ||
            scenario == TEST_UNIT_CAMPAIGN_SHORT_STORE)
            CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED &&
                  campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.aa.execution.sequence);
        else CHECK(ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND && !strcmp(driver.plan_sha256, plan_sha) &&
                   !strcmp(driver.context_sha256, context_sha));
    }
    FILE* sample_streams[2][2] = {{tmpfile(), tmpfile()}, {tmpfile(), tmpfile()}};
    BqRetirementUnitCampaignStreams streams[2] = {{NULL, NULL, sample_streams[0], 0, 0, 2},
                                                  {NULL, NULL, sample_streams[1], 0, 0, 2}};
    if (ok && scenario == TEST_UNIT_CAMPAIGN_EARLY_FREEZE)
    {
        CHECK(!bq_retirement_unit_campaign_freeze(&driver) && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
              !stages.aa.execution.sequence);
        ok = 0;
    }
    if (ok && scenario == TEST_UNIT_CAMPAIGN_CANCELLED)
    {
        char byte = 1;
        CHECK(write(cancel[1], &byte, 1) == 1 &&
              !bq_retirement_unit_campaign_stage(&driver, fixture->commands[0], 6, &streams[0]) &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.aa.execution.sequence && !driver.launches[1]);
        ok = 0;
    }
    ok = ok && bq_retirement_unit_campaign_stage(&driver, fixture->commands[0], 6, &streams[0]);
    if (ok)
        CHECK(driver.launches[1] == 732 && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_AA &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA && tp_retirement_campaign_stage_ready(&campaign, 0) &&
              driver.transcript_shards[0] == 1 && driver.metrics_shards[1] == 1 && !stages.ab.execution.sequence);
    if (ok && scenario == TEST_UNIT_CAMPAIGN_AB_WITHOUT_ADMISSION)
    {
        CHECK(!bq_retirement_unit_campaign_stage(&driver, fixture->commands[1], 6, &streams[1]) &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.ab.execution.sequence);
        ok = 0;
    }
    BqRetirementUnitCampaignAdmission admission = {plan_sha, context_sha, test_unit_campaign_receipt,
        scenario == TEST_UNIT_CAMPAIGN_DENIED ? 0 : 1};
    if (scenario == TEST_UNIT_CAMPAIGN_STALE_ADMISSION) admission.context_sha256 = test_unit_campaign_ready;
    if (ok)
    {
        ok = bq_retirement_unit_campaign_admit(&driver, &admission);
        if (scenario == TEST_UNIT_CAMPAIGN_DENIED || scenario == TEST_UNIT_CAMPAIGN_STALE_ADMISSION)
            CHECK(!ok && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.ab.execution.sequence &&
                  driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED);
        else CHECK(ok && campaign.phase == TP_RETIREMENT_CAMPAIGN_AB);
    }
    if (ok && scenario == TEST_UNIT_CAMPAIGN_AB_WITHOUT_FREEZE)
    {
        CHECK(!bq_retirement_unit_campaign_stage(&driver, fixture->commands[1], 6, &streams[1]) &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.ab.execution.sequence);
        ok = 0;
    }
    ok = ok && bq_retirement_unit_campaign_freeze(&driver) &&
        bq_retirement_unit_campaign_stage(&driver, fixture->commands[1], 6, &streams[1]);
    if (ok)
        CHECK(driver.launches[2] == 732 && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_AB &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_COLLECTED && phases.sequence == BQ_PHASE_MEASURING);
    ok = ok && bq_retirement_unit_campaign_finish(&driver);
    if (scenario == TEST_UNIT_CAMPAIGN_COMPLETE)
    {
        TpRetirementCampaignOutcome outcome = tp_retirement_campaign_outcome(&campaign);
        char post[65];
        char const other_chains[2][65] = {"0000000000000000000000000000000000000000000000000000000000000000",
                                          "0000000000000000000000000000000000000000000000000000000000000000"};
        CHECK(ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED && phases.sequence == BQ_PHASE_MEASURED &&
              outcome.execution == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE &&
              outcome.aa_qualification == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE &&
              outcome.validity == TP_RETIREMENT_CAMPAIGN_STATE_UNAVAILABLE &&
              outcome.statistical_decision == TP_RETIREMENT_CAMPAIGN_STATE_UNAVAILABLE &&
              tp_retirement_digest(driver.post_context_sha256) && strcmp(driver.post_context_sha256, context_sha));
        /* The post-sample digest chains exactly the frozen pre-sample one. */
        CHECK(bq_retirement_unit_campaign_post_context(&campaign, context_sha,
                  (char const (*)[65])driver.shard_chain_sha256, post) && !strcmp(post, driver.post_context_sha256));
        CHECK(!bq_retirement_unit_campaign_post_context(&campaign, plan_sha,
                  (char const (*)[65])driver.shard_chain_sha256, post) && !post[0]);
        CHECK(bq_retirement_unit_campaign_post_context(&campaign, context_sha, other_chains, post) &&
              strcmp(post, driver.post_context_sha256));
        /* What lane E's composer takes: the campaign identity and times, the
         * frozen plan and D's digests. */
        BqRetirementUnitCampaignResult result;
        CHECK(bq_retirement_unit_campaign_result(&driver, &result) && !strcmp(result.job, "job-1") &&
              !strcmp(result.boot, "boot-123") && result.attempt == 2 && result.bound_at_ns == bound &&
              result.completed_at_ns > bound && !memcmp(&result.plan, &plan, sizeof(plan)) &&
              !strcmp(result.plan_sha256, plan_sha) && !strcmp(result.context_sha256, context_sha) &&
              !strcmp(result.post_context_sha256, driver.post_context_sha256));
        /* A finished driver runs no further step, and then has no result. */
        CHECK(!bq_retirement_unit_campaign_finish(&driver) && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED &&
              !bq_retirement_unit_campaign_result(&driver, &result) && !result.job);
    }
    else CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && phases.sequence != BQ_PHASE_MEASURED);
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned shard = 0; shard < 2; ++shard)
            if (sample_streams[stage][shard]) CHECK(fclose(sample_streams[stage][shard]) == 0);
    if (stages.aa.stream || stages.ab.stream) test_retirement_campaign_close(&stages);
    if (records) CHECK(fclose(records) == 0);
    for (unsigned side = 0; side < 2; ++side)
        if (cancel[side] >= 0) CHECK(close(cancel[side]) == 0);
    CHECK(test_unit_campaign_peer_join(&phases, peer));
    test_unit_campaign_scrub(fixture->cwd);
}

static void test_retirement_unit_campaign(char const* executable_path, char const* root)
{
    TestUnitCampaign* fixture = (TestUnitCampaign*)calloc(1, sizeof(*fixture));
    int ready = fixture && test_unit_campaign_setup(fixture, executable_path, root);
    CHECK(ready);
    if (ready)
    {
        test_unit_campaign_plan(fixture);
        /* A driver that has not begun runs no step and touches nothing. */
        BqRetirementUnitCampaign idle = {0};
        BqRetirementUnitCampaignStreams none = {0};
        BqRetirementUnitCampaignAdmission admission = {0};
        CHECK(!bq_retirement_unit_campaign_measuring(&idle) && idle.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED);
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_untimed(&idle, NULL, fixture->untimed, 4, &fixture->held, &fixture->review,
                                                   &none));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_attach(&idle, NULL, &fixture->pins, test_unit_campaign_ready, NULL));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_stage(&idle, fixture->commands[0], 6, &none));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_admit(&idle, &admission));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_freeze(&idle) && !bq_retirement_unit_campaign_finish(&idle));
        /* begin needs the PREPARING acknowledgement already held. */
        BqPhaseChannel fresh;
        pid_t peer = test_unit_campaign_peer(&fresh, 1, 0);
        idle = (BqRetirementUnitCampaign){0};
        CHECK(peer > 0 && !bq_retirement_unit_campaign_begin(&idle, &fresh, -1, bq_phase_clock() + 1000000000,
                                                             fixture->cwd, fixture->cwd) &&
              fresh.sequence == 0 && test_unit_campaign_peer_join(&fresh, peer));
        for (unsigned scenario = 0; scenario < TEST_UNIT_CAMPAIGN_SCENARIOS; ++scenario)
            test_unit_campaign_attempt(fixture, scenario);
    }
    if (fixture) test_unit_campaign_teardown(fixture);
    free(fixture);
}
#endif
#endif
