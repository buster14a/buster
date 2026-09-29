/* Private functional fixtures for #1022. The integration owner registers this
 * in tests.c after the collection handoff is integrated; no fixture measures
 * an acceptance candidate or asserts an approved #426 noise verdict. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_CAMPAIGN_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_CAMPAIGN_TEST_H
#define TP_RETIREMENT_CAMPAIGN_FIXTURE_AA 1
#include "retirement_campaign.h"
#undef TP_RETIREMENT_CAMPAIGN_FIXTURE_AA
#include "../bench_service/retirement_correctness.c"
#include "../bench_service/retirement_campaign_binding.h"

#ifdef __linux__
static int test_retirement_campaign_capacity_is_zero(TpRetirementCampaignCapacity const* capacity)
{
    TpRetirementCampaignCapacity zero = {0};
    return capacity && !memcmp(capacity, &zero, sizeof(zero));
}

static int test_retirement_campaign_store_plan_is_zero(TpRetirementCampaignStorePlan const* plan)
{
    int zero = plan && !plan->owned_files && !plan->owned_bytes && !plan->external_entries &&
        !plan->external_bytes && !plan->entries && !plan->bytes && !plan->remaining_entries &&
        !plan->remaining_bytes;
    return zero;
}

static int test_retirement_campaign_capacity_shape(unsigned groups, unsigned objects, unsigned rows,
    unsigned runtime, unsigned pairs, uint64_t metrics_bytes, TpRetirementCampaignCapacity* capacity)
{
    TpRetirementCampaignShape shape = {groups, objects, rows, runtime, pairs, metrics_bytes};
    return tp_retirement_campaign_capacity(&shape, capacity);
}

/* Both stages of the one-row fixture: census row 6 of 7, a singleton link
 * group with native runtime. */
static int test_retirement_campaign_open(TpSampleTest* aa, TpSampleTest* ab, int cpu)
{
    int ok = test_sample_open_layout(aa, 1, 6) && test_sample_open_layout(ab, 1, 6);
    if (aa) aa->transcript.cpu = cpu;
    if (ab) ab->transcript.cpu = cpu;
    return ok;
}

static void test_retirement_campaign(char const* executable_path, char const* root)
{
    char directory[TP_PATH_CAP], binary_path[TP_PATH_CAP], candidate_path[TP_PATH_CAP];
    CHECK(tp_path(directory, root, "campaign-fixture") && tp_mkdirs(directory));
    CHECK(test_text(directory, "cwd-marker", "fixed cwd\n"));
    CHECK(tp_path(binary_path, directory, "fixture-child") && tp_copy_file(executable_path, binary_path));
    CHECK(tp_path(candidate_path, directory, "fixture-candidate-child") &&
          tp_copy_file(executable_path, candidate_path));
    CHECK(chmod(binary_path, 0500) == 0 && chmod(candidate_path, 0500) == 0);
    int binary = open(binary_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int candidate_binary = open(candidate_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int cwd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int leak = fcntl(binary, F_DUPFD, 512);
    char leak_text[32], binary_sha[65], artifact_sha[65], code_sha[65], runtime_sha[65], batch_output[65];
    uint64_t binary_bytes = 0;
    snprintf(leak_text, sizeof(leak_text), "%d", leak);
    CHECK(binary >= 3 && candidate_binary >= 3 && cwd >= 3 && leak >= 3 &&
          tp_retirement_file_hash(binary, binary_sha, &binary_bytes) && binary_bytes);
    TpRetirementExecutable frozen;
    CHECK(tp_retirement_executable_init(&frozen, binary, binary_sha));
    unsigned char artifact[1024];
    unsigned artifact_bytes = test_artifact_fixture(artifact, 1, 1);
    Sha256 hash;
    sha256_init(&hash); sha256_add(&hash, artifact, artifact_bytes); sha256_finish_hex(&hash, artifact_sha);
    sha256_init(&hash); sha256_add(&hash, "fixture-code\n", 13); sha256_finish_hex(&hash, code_sha);
    memcpy(runtime_sha, code_sha, sizeof(runtime_sha));
    char const* objects[] = {artifact_sha};
    CHECK(tp_retirement_batch_output_digest(objects, 1, batch_output));
    char* environment[] = {"LC_ALL=C", "TP_RETIREMENT_TEST=explicit", NULL};
    char* arguments[2][4][7] = {0};
    char command_sha[2][4][65];
    /* Per stage: [group 0 baseline, group 0 candidate, runtime row baseline,
     * runtime row candidate]; G = U = 1 in this fixture. */
    TpRetirementMeasuredCommand commands[2][4] = {0};
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned kind = 0; kind < 2; ++kind)
            for (unsigned variant = 0; variant < 2; ++variant)
            {
                unsigned index = kind * 2 + variant;
                char** argv = arguments[stage][index];
                argv[0] = binary_path; argv[1] = "retirement-child";
                argv[2] = kind ? "runtime" : "compiler";
                argv[3] = variant ? "artifact-right.bin" : "artifact-left.bin";
                argv[4] = "ok"; argv[5] = leak_text;
                commands[stage][index] = (TpRetirementMeasuredCommand){.unit = kind ? 6 : 0,
                    .kind = kind, .variant = variant, .arguments = argv, .argument_count = 6,
                    .environment = environment, .environment_count = 2, .directory = directory,
                    .artifact = kind ? NULL : argv[3], .timeout_seconds = 2,
                    .command_sha256 = command_sha[stage][index],
                    .output_sha256 = kind ? runtime_sha : batch_output};
                CHECK(tp_retirement_command_hash(&commands[stage][index], command_sha[stage][index]));
            }
    int cpu = tp_first_allowed_cpu();
    CHECK(cpu >= 0);
    TpSampleTest aa, ab;
    TpRetirementPlan plan = {.version = TP_RETIREMENT_STATISTICS_VERSION, .seed = 1,
        .pairs_per_round = 60, .resamples = TP_RETIREMENT_MIN_RESAMPLES,
        .bootstrap_members_per_scope = 1, .cell_members_per_scope = 1,
        .frozen_before_samples = 1};
    char const* identity = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    TpRetirementCampaign campaign = {0};
    BqRetirementCampaignBinding binding = {0};
    BqRetirementHeldBinaries held = {.descriptors = {-1, -1}};
    TpRetirementCampaignCommand snapshots[8];
    unsigned identities[2];
    BqRetirementTrustedRow trusted[7] = {0};
    BqRetirementRowFact facts[7] = {0};
    BqRetirementCorrectness gate = {0};
    gate.prepared.rows = gate.rows_done = 7;
    gate.prepared.native_target = 1;
    gate.eligible_rows = 1;
    gate.finished = 1;
    gate.trusted_rows = trusted;
    gate.facts = facts;
    memcpy(gate.prepared.preparation_sha256, identity, 65);
    memcpy(gate.prepared.source_sha256[0], identity, 65);
    memcpy(gate.prepared.source_sha256[1], identity, 65);
    memcpy(gate.prepared.binary_sha256[0], binary_sha, 65);
    memcpy(gate.prepared.binary_sha256[1], binary_sha, 65);
    held.owned = 1;
    held.descriptors[0] = binary;
    held.descriptors[1] = candidate_binary;
    memcpy(held.verified.preparation_sha256, gate.prepared.preparation_sha256, 65);
    memcpy(held.verified.directory_identity_sha256, identity, 65);
    memcpy(held.verified.source_sha256, gate.prepared.source_sha256, sizeof(held.verified.source_sha256));
    memcpy(held.verified.binary_sha256, gate.prepared.binary_sha256, sizeof(held.verified.binary_sha256));
    CHECK(bq_retirement_campaign_descriptor_identity(binary, held.verified.binary_identity_sha256[0]) &&
          bq_retirement_campaign_descriptor_identity(candidate_binary, held.verified.binary_identity_sha256[1]) &&
          strcmp(held.verified.binary_identity_sha256[0], held.verified.binary_identity_sha256[1]));
    for (unsigned row = 0; row < 7; ++row)
    {
        trusted[row].row = facts[row].row = row;
    }
    trusted[6].compiler_eligible = trusted[6].code_obligation = 1;
    trusted[6].stage = BQ_RETIREMENT_STAGE_LINK;
    trusted[6].target = 1;
    facts[6].runtime_eligible = facts[6].code_eligible = 1;
    memcpy(trusted[6].independent_oracle_sha256, runtime_sha, 65);
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        BqRetirementObservedSide* side = &facts[6].side[variant];
        memcpy(side->compiler_command_sha256, commands[1][variant].command_sha256, 65);
        memcpy(side->artifact_sha256, artifact_sha, 65);
        memcpy(side->code_sha256, code_sha, 65);
        memcpy(side->runtime_command_sha256, commands[1][2 + variant].command_sha256, 65);
        side->code_bytes = 13;
    }
    /* The fixture precommits the second baseline label, whose distinct path
     * gives it a different command hash from the first A/A label. */
    sha256_init(&hash);
    static char const second_domain[] = "bq-retirement-aa-second-commands-v1";
    sha256_add(&hash, second_domain, sizeof(second_domain) - 1);
    uint8_t ordinal[4] = {6, 0, 0, 0}, applicable_runtime = 1;
    sha256_add(&hash, ordinal, sizeof(ordinal));
    sha256_add(&hash, command_sha[0][1], 64);
    sha256_add(&hash, &applicable_runtime, sizeof(applicable_runtime));
    sha256_add(&hash, command_sha[0][3], 64);
    sha256_finish_hex(&hash, gate.prepared.aa_second_commands_sha256);
    bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&gate));
    char candidate_identity[SHA256_HEX_CAPACITY];
    memcpy(candidate_identity, held.verified.binary_identity_sha256[1], sizeof(candidate_identity));
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
        if (scenario == 0)
            aa.transcript.attempt = ab.transcript.attempt = 3;
        else if (scenario == 1)
            strcpy(ab.transcript.job, "job-2");
        else if (scenario == 2)
        {
            gate.prepared.source_sha256[1][0] = 'b';
            bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
            CHECK(bq_retirement_correctness_ready(&gate));
        }
        int alias = -1;
        if (scenario == 3)
        {
            alias = fcntl(binary, F_DUPFD_CLOEXEC, 512);
            held.descriptors[1] = alias;
            memcpy(held.verified.binary_identity_sha256[1],
                   held.verified.binary_identity_sha256[0], SHA256_HEX_CAPACITY);
        }
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        CHECK(!bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan,
            &aa.samples, &ab.samples, &held, 1, 2, commands[0], commands[1], snapshots, 8,
            identities, 2, identity, identity) &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa.samples.failed && ab.samples.failed &&
            !aa.execution.sequence && !ab.execution.sequence && !binding.campaign);
        if (scenario == 2)
        {
            gate.prepared.source_sha256[1][0] = 'a';
            bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
            CHECK(bq_retirement_correctness_ready(&gate));
        }
        if (scenario == 3)
        {
            held.descriptors[1] = candidate_binary;
            memcpy(held.verified.binary_identity_sha256[1], candidate_identity,
                   sizeof(candidate_identity));
            CHECK(alias >= 3 && close(alias) == 0);
        }
        test_sample_close(&aa);
        test_sample_close(&ab);
    }
    /* (A1) The gate cannot supply an object row's frozen batch contract, and
     * a cross-target row is never timed; either fails closed before timing. */
    for (unsigned scenario = 0; scenario < 2; ++scenario)
    {
        if (scenario) trusted[6].target = 2;
        else trusted[6].stage = BQ_RETIREMENT_STAGE_OBJECT;
        bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        CHECK(!bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
            &held, 1, 2, commands[0], commands[1], snapshots, 8, identities, 2, identity, identity) &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa.samples.failed && !binding.campaign);
        trusted[6].stage = BQ_RETIREMENT_STAGE_LINK;
        trusted[6].target = 1;
        bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        test_sample_close(&aa);
        test_sample_close(&ab);
    }
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
        &held, 1, 2, commands[0], commands[1], snapshots, 8,
        identities, 2, identity, identity));
    CHECK(binding.campaign == &campaign && binding.gate == &gate && binding.timed_rows == 1 &&
          !strcmp(binding.sealed_sha256, gate.sealed_sha256));
    CHECK(campaign.phase == TP_RETIREMENT_CAMPAIGN_AA && campaign.runtime_rows[0] == 6 &&
          campaign.group_shapes[0] == ((1u << 1) | TP_RETIREMENT_GROUP_SINGLETON) &&
          campaign.capacity.invocations_per_stage == 488 &&
          campaign.capacity.samples_per_stage == 120 &&
          campaign.capacity.total_invocations == 976 && campaign.capacity.total_samples == 240 &&
          campaign.capacity.spool_bytes_per_stage == 120 * TP_RETIREMENT_SAMPLE_RECORD_BYTES &&
          campaign.capacity.total_spool_bytes == 240 * TP_RETIREMENT_SAMPLE_RECORD_BYTES &&
          campaign.capacity.transcript_shards_per_stage == 1 &&
          campaign.capacity.total_transcript_shards == 2 && !campaign.capacity.total_metrics_artifacts);
    TpRetirementCampaignCapacity large;
    CHECK(test_retirement_campaign_capacity_shape(72672, 0, 72672, 0, 60, 0, &large) &&
          large.samples_per_stage == UINT64_C(8720640) &&
          large.total_samples == UINT64_C(17441280) &&
          large.total_sample_partitions == 2 &&
          large.compiler_invocations_per_stage == UINT64_C(17731968) &&
          large.runtime_invocations_per_stage == 0 &&
          large.total_invocations == UINT64_C(35463936) &&
          large.transcript_bytes_per_stage_upper_bound ==
              UINT64_C(17731968) * TP_RETIREMENT_TRANSCRIPT_RECORD_BYTES_MAX &&
          large.sample_bytes_per_stage_upper_bound ==
              UINT64_C(8720640) * TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX &&
          large.total_transcript_bytes_upper_bound == UINT64_C(36137750784) &&
          large.total_sample_bytes_upper_bound == UINT64_C(5755622400) &&
          large.total_payload_bytes_upper_bound == UINT64_C(41893373184) &&
          large.total_transcript_shards == 542 && large.total_sample_shards == 134 &&
          large.total_shard_files == 676 && large.total_payload_files == 676);
    CHECK(test_retirement_campaign_capacity_shape(72672, 0, 72672, 72672, 60, 0, &large) &&
          large.compiler_invocations_per_stage == UINT64_C(17731968) &&
          large.runtime_invocations_per_stage == UINT64_C(17731968) &&
          large.invocations_per_stage == UINT64_C(35463936) &&
          large.total_invocations == UINT64_C(70927872) &&
          large.total_shard_files == 1218 &&
          large.total_payload_bytes_upper_bound == UINT64_C(78031123968));
    CHECK(test_retirement_campaign_capacity_shape(77762, 0, 77762, 0, 60, 0, &large) &&
          large.total_transcript_shards == 580 && large.total_sample_shards == 144 &&
          large.total_shard_files == 724);
    CHECK(test_retirement_campaign_capacity_shape(77762, 0, 77762, 77762, 60, 0, &large) &&
          large.total_transcript_shards == 1160 && large.total_sample_shards == 144 &&
          large.total_shard_files == 1304);
    /* All singleton groups without runtime fit the 128 GiB store through 184
     * pairs at the proven maximal line widths; all runtime-eligible, 98. */
    CHECK(test_retirement_campaign_capacity_shape(77762, 0, 77762, 0, 184, 0, &large) &&
          large.total_shard_files == 2196 &&
          large.total_payload_bytes_upper_bound == UINT64_C(136161262000));
    CHECK(!test_retirement_campaign_capacity_shape(77762, 0, 77762, 0, 186, 0, &large) &&
          test_retirement_campaign_capacity_is_zero(&large));
    CHECK(test_retirement_campaign_capacity_shape(77762, 0, 77762, 77762, 98, 0, &large) &&
          large.total_shard_files == 2114 &&
          large.total_payload_bytes_upper_bound == UINT64_C(135574625472));
    CHECK(!test_retirement_campaign_capacity_shape(77762, 0, 77762, 77762, 100, 0, &large) &&
          test_retirement_campaign_capacity_is_zero(&large));
    CHECK(!test_retirement_campaign_capacity_shape(77762, 0, 77762, 0, 254, 0, &large) &&
          test_retirement_campaign_capacity_is_zero(&large));
    uint64_t entries = 0, bytes = 0;
    CHECK(tp_retirement_campaign_store_fits(3026, TP_RETIREMENT_STORE_TOTAL_BYTES,
              TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES, 0, &entries, &bytes) &&
          entries == 3029 && bytes == TP_RETIREMENT_STORE_TOTAL_BYTES);
    CHECK(!tp_retirement_campaign_store_fits(3026, TP_RETIREMENT_STORE_TOTAL_BYTES + 1,
              TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES, 0, &entries, &bytes));
    CHECK(!tp_retirement_campaign_store_fits(TP_RETIREMENT_STORE_FILES - 2, 1,
              TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES, 0, &entries, &bytes));
    /* One metrics artifact per object batch, warmups included, enters the
     * store plan: two groups, one of them an object batch with a 1 MiB
     * reviewed artifact bound. */
    CHECK(test_retirement_campaign_capacity_shape(2, 1, 3, 1, 60, UINT64_C(1048576), &large) &&
          large.invocations_per_stage == 732 && large.row_samples_per_stage == 360 &&
          large.batch_samples_per_stage == 120 && large.samples_per_stage == 480 &&
          large.metrics_artifacts_per_stage == 244 && large.total_metrics_artifacts == 488 &&
          large.metrics_bytes_per_stage_upper_bound == UINT64_C(255852544) &&
          large.sample_bytes_per_stage_upper_bound ==
              360 * TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX + 120 * TP_RETIREMENT_BATCH_RECORD_BYTES_MAX &&
          large.sample_shards_per_stage == 2 && large.sample_partitions_per_stage == 2 &&
          large.total_shard_files == 6 && large.total_payload_files == 494 &&
          large.total_payload_bytes_upper_bound == UINT64_C(513498344));
    /* Eight object groups fill the 4096-entry store at P=60; the 80 groups of
     * the full native-host population need 39040 metrics artifacts and fail
     * closed before any timing. */
    CHECK(test_retirement_campaign_capacity_shape(10, 8, 18, 2, 60, UINT64_C(1048576), &large) &&
          large.total_payload_files == 3910);
    CHECK(!test_retirement_campaign_capacity_shape(11, 9, 20, 2, 60, UINT64_C(1048576), &large) &&
          test_retirement_campaign_capacity_is_zero(&large));
    CHECK(!test_retirement_campaign_capacity_shape(82, 80, 6578, 2, 60, UINT64_C(1048576), &large));
    /* Inconsistent shapes are rejected. */
    CHECK(!test_retirement_campaign_capacity_shape(0, 0, 1, 0, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(1, 0, 1, 2, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(2, 1, 3, 2, 60, 1, &large)); /* Runtime in an object group. */
    CHECK(!test_retirement_campaign_capacity_shape(2, 3, 3, 0, 60, 1, &large));
    CHECK(!test_retirement_campaign_capacity_shape(3, 0, 2, 0, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(2, 1, 3, 1, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(2, 1, 3, 1, 60, TP_RETIREMENT_METRICS_ARTIFACT_BYTES + 1, &large));
    CHECK(!test_retirement_campaign_capacity_shape(1, 0, 1, 0, 60, 1, &large));
    CHECK(!test_retirement_campaign_capacity_shape(1, 0, 1, 0, 61, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(77792, 0, 77792, 0, 254, 0, &large));
    CHECK(!tp_retirement_campaign_capacity(NULL, &large) && test_retirement_campaign_capacity_is_zero(&large));
    uint64_t capacity_value = 1;
    CHECK(!tp_retirement_campaign_u64_mul(UINT64_MAX, 2, &capacity_value) && !capacity_value);
    capacity_value = 1;
    CHECK(!tp_retirement_campaign_u64_add(UINT64_MAX, 1, &capacity_value) && !capacity_value);
    /* The exact store preflight adds the caller's external reservation. */
    TpRetirementCampaignStorePlan store_plan;
    uint64_t receipt = TP_RETIREMENT_RECEIPT_BYTES;
    uint64_t spare_bytes = TP_RETIREMENT_STORE_TOTAL_BYTES - UINT64_C(136161262000) - receipt;
    CHECK(test_retirement_campaign_capacity_shape(77762, 0, 77762, 0, 184, 0, &large));
    /* The store owns the payload plus the receipt; both are in its plan. */
    CHECK(tp_retirement_campaign_store_preflight(&large, 1, receipt, 3, 1024, &store_plan) &&
          store_plan.owned_files == 2197 && store_plan.owned_bytes == UINT64_C(136161262000) + receipt &&
          store_plan.external_entries == 3 && store_plan.external_bytes == 1024 &&
          store_plan.entries == 2200 && store_plan.bytes == UINT64_C(136161262000) + receipt + 1024 &&
          store_plan.remaining_entries == TP_RETIREMENT_STORE_FILES - 2200 &&
          store_plan.remaining_bytes == spare_bytes - 1024);
    CHECK(tp_retirement_campaign_store_preflight(&large, 1, receipt, TP_RETIREMENT_STORE_FILES - 2197,
              spare_bytes, &store_plan) &&
          store_plan.entries == TP_RETIREMENT_STORE_FILES && !store_plan.remaining_entries &&
          store_plan.bytes == TP_RETIREMENT_STORE_TOTAL_BYTES && !store_plan.remaining_bytes);
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, receipt, TP_RETIREMENT_STORE_FILES - 2196, 0,
              &store_plan) && test_retirement_campaign_store_plan_is_zero(&store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, receipt, 3, spare_bytes + 1, &store_plan) &&
          test_retirement_campaign_store_plan_is_zero(&store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 0, receipt, 3, 0, &store_plan) &&
          test_retirement_campaign_store_plan_is_zero(&store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, receipt - 1, 3, 0, &store_plan) &&
          test_retirement_campaign_store_plan_is_zero(&store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, UINT64_MAX, receipt, 3, 0, &store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, UINT64_MAX, 3, 0, &store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, receipt,
              TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES - 1, 0, &store_plan) &&
          test_retirement_campaign_store_plan_is_zero(&store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, receipt, UINT64_MAX, 0, &store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, receipt, 3, UINT64_MAX, &store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(NULL, 1, receipt, 3, 0, &store_plan) &&
          test_retirement_campaign_store_plan_is_zero(&store_plan));
    CHECK(!tp_retirement_campaign_store_preflight(&large, 1, receipt, 3, 0, NULL));
    TpRetirementCampaignCapacity mismatched = large;
    ++mismatched.total_shard_files;
    CHECK(!tp_retirement_campaign_store_preflight(&mismatched, 1, receipt, 3, 0, &store_plan));
    mismatched = large;
    --mismatched.total_payload_bytes_upper_bound;
    CHECK(!tp_retirement_campaign_store_preflight(&mismatched, 1, receipt, 3, 0, &store_plan));
    mismatched = large;
    ++mismatched.total_payload_files;
    CHECK(!tp_retirement_campaign_store_preflight(&mismatched, 1, receipt, 3, 0, &store_plan));
    mismatched = (TpRetirementCampaignCapacity){0};
    CHECK(!tp_retirement_campaign_store_preflight(&mismatched, 1, receipt, 3, 0, &store_plan));
    /* Metrics artifacts count toward both store ceilings. */
    CHECK(test_retirement_campaign_capacity_shape(2, 1, 3, 1, 60, UINT64_C(1048576), &large) &&
          tp_retirement_campaign_store_preflight(&large, 1, receipt, 3, 0, &store_plan) &&
          store_plan.owned_files == 495 && store_plan.owned_bytes == UINT64_C(513498344) + receipt);
    CHECK(test_retirement_campaign_capacity_shape(1, 0, 1, 1, 60, 0, &large));
    TpRetirementCampaignDurationBounds bounds = {
        .reservation_ns = 1000000, .materialization_ns = 1000000,
        .baseline_build_ns = 1000000, .candidate_build_ns = 1000000,
        .correctness_ns = 1000000, .settling_per_stage_ns = 1000000,
        .compiler_invocation_ns = 2000000, .runtime_invocation_ns = 3000000,
        .aa_qualification_ns = 1000000, .aa_receipt_sealing_ns = 1000000,
        .sample_export_per_stage_ns = 1000000, .final_statistics_ns = 1000000,
        .final_sealing_ns = 1000000, .cleanup_ns = 1000000};
    TpRetirementCampaignPreflight preflight;
    CHECK(tp_retirement_campaign_preflight(&large, &bounds, &preflight) && preflight.fits &&
          preflight.fixed_phase_ns == UINT64_C(14000000) &&
          preflight.compiler_invocation_total_ns == UINT64_C(976000000) &&
          preflight.runtime_invocation_total_ns == UINT64_C(1464000000) &&
          preflight.required_ns == UINT64_C(2454000000) &&
          preflight.remaining_ns == TP_RETIREMENT_CAMPAIGN_WHOLE_JOB_BUDGET_NS - preflight.required_ns);
    uint64_t* required_bounds[] = {
        &bounds.reservation_ns, &bounds.materialization_ns,
        &bounds.baseline_build_ns, &bounds.candidate_build_ns, &bounds.correctness_ns,
        &bounds.settling_per_stage_ns, &bounds.compiler_invocation_ns,
        &bounds.runtime_invocation_ns, &bounds.aa_qualification_ns,
        &bounds.aa_receipt_sealing_ns, &bounds.sample_export_per_stage_ns,
        &bounds.final_statistics_ns, &bounds.final_sealing_ns, &bounds.cleanup_ns};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(required_bounds); ++i)
    {
        uint64_t saved = *required_bounds[i];
        *required_bounds[i] = 0;
        CHECK(!tp_retirement_campaign_preflight(&large, &bounds, &preflight) &&
              !preflight.required_ns);
        *required_bounds[i] = saved;
    }
    bounds.compiler_invocation_ns =
        TP_RETIREMENT_CAMPAIGN_WHOLE_JOB_BUDGET_NS / large.total_compiler_invocations + 1;
    CHECK(!tp_retirement_campaign_preflight(&large, &bounds, &preflight) && !preflight.fits &&
          preflight.required_ns > TP_RETIREMENT_CAMPAIGN_WHOLE_JOB_BUDGET_NS &&
          !preflight.remaining_ns);
    bounds.compiler_invocation_ns = 2000000;
    TpRetirementCampaignCapacity inconsistent = large;
    inconsistent.total_invocations += 1;
    CHECK(!tp_retirement_campaign_preflight(&inconsistent, &bounds, &preflight));
    CHECK(test_retirement_campaign_capacity_shape(1, 0, 1, 0, 60, 0, &large));
    bounds.runtime_invocation_ns = 0;
    CHECK(tp_retirement_campaign_preflight(&large, &bounds, &preflight) && preflight.fits &&
          !preflight.runtime_invocation_total_ns);
    for (unsigned stage = 0; stage < 2; ++stage)
    {
        TpSampleTest* active = stage ? &ab : &aa;
        TpRetirementInvocation invocation;
        int ok = 1;
        while (ok && tp_retirement_execution_peek(&active->execution, &invocation) == TP_RETIREMENT_NEXT_READY)
        {
            unsigned index = invocation.kind * 2 + invocation.variant;
            int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            TpProcessInputs inputs = {held.descriptors[stage && invocation.variant], cwd, log, environment};
            TpRetirementMeasurementResult measured;
            ok = log >= 3 && bq_retirement_campaign_run(&binding, &commands[stage][index],
                &inputs, cwd, &measured);
            CHECK(ok && measured.status == TP_RETIREMENT_MEASUREMENT_COMPLETE);
            if (log >= 3) CHECK(close(log) == 0);
            if (ok)
            {
                CHECK(unlinkat(cwd, "child.log", 0) == 0);
                if (!invocation.kind) CHECK(unlinkat(cwd, commands[stage][index].artifact, 0) == 0);
            }
        }
        CHECK(ok && tp_retirement_execution_complete(&active->execution));
        TpRetirementShard final;
        CHECK(tp_retirement_campaign_finish_stage(&campaign, tp_process_monotonic_ns(), &final));
        CHECK(final.records == 488 && active->samples.collected == 488);
        CHECK(!tp_retirement_campaign_stage_ready(&campaign, stage));
        if (stage)
            CHECK(tp_retirement_campaign_outcome(&campaign).execution ==
                  TP_RETIREMENT_CAMPAIGN_STATE_RUNNING);
        FILE* numeric = tmpfile();
        TpRetirementShard sample_shard;
        CHECK(numeric && tp_retirement_samples_write_shard(&active->samples, numeric, &sample_shard) &&
              sample_shard.records == 120 && tp_retirement_samples_finish(&active->samples));
        CHECK(tp_retirement_campaign_stage_ready(&campaign, stage));
        if (numeric) CHECK(fclose(numeric) == 0);
        if (!stage)
        {
            CHECK(campaign.phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA && !ab.samples.collected);
            /* The receipt here is explicitly a fixture. Production validation
             * has to obtain #426/#1021 authority from the service. */
            CHECK(tp_retirement_campaign_admit_aa_fixture(&campaign, 1, identity, identity, identity));
            CHECK(campaign.phase == TP_RETIREMENT_CAMPAIGN_AB);
        }
    }
    CHECK(campaign.phase == TP_RETIREMENT_CAMPAIGN_COLLECTED);
    TpRetirementCampaignOutcome outcome = tp_retirement_campaign_outcome(&campaign);
    CHECK(outcome.execution == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE &&
          outcome.aa_qualification == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE &&
          outcome.validity == TP_RETIREMENT_CAMPAIGN_STATE_UNAVAILABLE &&
          outcome.statistical_decision == TP_RETIREMENT_CAMPAIGN_STATE_UNAVAILABLE);
    double ratios[120];
    for (unsigned i = 0; i < 120; ++i)
    {
        uint64_t values[TP_RETIREMENT_SAMPLE_VALUES];
        CHECK(tp_retirement_sample_read(&ab.samples, i, values));
        ratios[i] = (double)values[1] / (double)values[0];
        CHECK(values[2] && values[3] && values[4] && values[5]);
    }
    TpRetirementSeries series = {.ratios = ratios, .ratio_count = 120, .cell_count = 1,
        .observed_pairs = {60, 60}, .member_kind = TP_RETIREMENT_EXACT_CELL_MEMBER,
        .metric_index = TP_RETIREMENT_WALL_TIME, .limit = 1.05};
    TpRetirementResult statistic = tp_retirement_assess(&plan, &series, NULL, 0);
    CHECK(statistic.valid && statistic.outcome != TP_RETIREMENT_INVALID);
    test_sample_close(&aa);
    test_sample_close(&ab);

    /* The held-record join is checked again at launch, after freeze. */
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan,
        &aa.samples, &ab.samples, &held, 1, 2, commands[0], commands[1],
        snapshots, 8, identities, 2, identity, identity));
    char held_binary_digit = held.verified.binary_sha256[1][0];
    held.verified.binary_sha256[1][0] = held_binary_digit == 'e' ? 'f' : 'e';
    int held_guard_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    TpProcessInputs held_guard_inputs = {binary, cwd, held_guard_log, environment};
    TpRetirementMeasurementResult held_guard_result;
    struct stat held_guard_stat;
    CHECK(held_guard_log >= 3 && !bq_retirement_campaign_run(&binding, &commands[0][0],
        &held_guard_inputs, cwd, &held_guard_result) &&
        held_guard_result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
        campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa.samples.failed && ab.samples.failed &&
        !aa.execution.sequence && !ab.execution.sequence && fstat(held_guard_log, &held_guard_stat) == 0 &&
        held_guard_stat.st_size == 0 &&
        fstatat(cwd, "artifact-left.bin", &held_guard_stat, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    held.verified.binary_sha256[1][0] = held_binary_digit;
    if (held_guard_log >= 3)
        CHECK(close(held_guard_log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_sample_close(&aa);
    test_sample_close(&ab);

    /* The lower-level freeze helper cannot launch without held service files. */
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, commands[0], commands[1], snapshots, 8, identities, 2, identity, identity));
    int unheld_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    TpProcessInputs unheld_inputs = {binary, cwd, unheld_log, environment};
    TpRetirementMeasurementResult unheld_result;
    struct stat unheld_stat;
    CHECK(unheld_log >= 3 && !bq_retirement_campaign_run(&binding, &commands[0][0],
        &unheld_inputs, cwd, &unheld_result) &&
        unheld_result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
        campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa.samples.failed && ab.samples.failed &&
        !aa.execution.sequence && !ab.execution.sequence && fstat(unheld_log, &unheld_stat) == 0 &&
        unheld_stat.st_size == 0 &&
        fstatat(cwd, "artifact-left.bin", &unheld_stat, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    if (unheld_log >= 3) CHECK(close(unheld_log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_sample_close(&aa);
    test_sample_close(&ab);

    /* Both baseline-label-2 command kinds require the sealed precommit;
     * a different, self-consistent plan fails before the first timed child. */
    for (unsigned kind = 0; kind < 2; ++kind)
    {
        unsigned index = kind * 2 + 1;
        CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        arguments[0][index][4] = "fail";
        CHECK(tp_retirement_command_hash(&commands[0][index], command_sha[0][index]));
        CHECK(bq_retirement_correctness_ready(&gate));
        CHECK(!bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
            &frozen, &frozen, commands[0], commands[1], snapshots, 8,
            identities, 2, identity, identity) &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
            aa.samples.failed && ab.samples.failed &&
            !aa.execution.sequence && !ab.execution.sequence && !binding.campaign);
        arguments[0][index][4] = "ok";
        CHECK(tp_retirement_command_hash(&commands[0][index], command_sha[0][index]));
        test_sample_close(&aa); test_sample_close(&ab);
    }

    /* Recheck the full #1020 seal at the launch boundary. A changed fact or
     * self-consistent reseal after freeze cannot start even the first child. */
    for (unsigned scenario = 0; scenario < 2; ++scenario)
    {
        CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        CHECK(bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
            &held, 1, 2, commands[0], commands[1], snapshots, 8,
            identities, 2, identity, identity));
        char original_artifact_digit = facts[6].side[1].artifact_sha256[0];
        facts[6].side[1].artifact_sha256[0] = original_artifact_digit == 'e' ? 'f' : 'e';
        if (scenario) bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        CHECK(scenario ? bq_retirement_correctness_ready(&gate) :
                         !bq_retirement_correctness_ready(&gate));
        int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        TpProcessInputs inputs = {binary, cwd, log, environment};
        TpRetirementMeasurementResult measured;
        struct stat unchanged;
        CHECK(log >= 3 && !bq_retirement_campaign_run(&binding, &commands[0][0],
            &inputs, cwd, &measured) &&
            measured.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
            aa.samples.failed && ab.samples.failed &&
            !aa.execution.sequence && !ab.execution.sequence &&
            fstat(log, &unchanged) == 0 && unchanged.st_size == 0 &&
            fstatat(cwd, "artifact-left.bin", &unchanged, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
        facts[6].side[1].artifact_sha256[0] = original_artifact_digit;
        if (scenario) bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        if (log >= 3) CHECK(close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
        test_sample_close(&aa); test_sample_close(&ab);
    }

    /* Mutation of a frozen oracle fails before launch and poisons both stages. */
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    CHECK(tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 2, 7, 0, identity, identity));
    int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    TpProcessInputs inputs = {binary, cwd, log, environment};
    TpRetirementMeasurementResult measured;
    commands[0][0].output_sha256 = identity;
    CHECK(!tp_retirement_campaign_run(&campaign, &commands[0][0], &inputs, cwd, &measured) &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !aa.execution.sequence &&
          !ab.execution.sequence && aa.samples.failed && ab.samples.failed);
    commands[0][0].output_sha256 = batch_output;
    if (log >= 3) CHECK(close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_sample_close(&aa); test_sample_close(&ab);

    /* A changed #1020 output oracle cannot be imported into a fresh timed
     * attempt, even if the command still has its original plan digest. */
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    char saved_artifact_digit = facts[6].side[1].artifact_sha256[0];
    facts[6].side[1].artifact_sha256[0] = saved_artifact_digit == 'e' ? 'f' : 'e';
    bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&gate));
    binding = (BqRetirementCampaignBinding){0};
    CHECK(!bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 2, identity, identity) &&
        campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
        aa.samples.failed && ab.samples.failed && !aa.execution.sequence);
    facts[6].side[1].artifact_sha256[0] = saved_artifact_digit;
    bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
    test_sample_close(&aa); test_sample_close(&ab);

    /* No guessed A/A eligibility or partial campaign can reach A/B. */
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    CHECK(tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 2, 7, 0, identity, identity));
    TpRetirementShard final;
    CHECK(!tp_retirement_campaign_finish_stage(&campaign, tp_process_monotonic_ns(), &final) &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && ab.samples.failed);
    test_sample_close(&aa); test_sample_close(&ab);

    /* Every rejected frozen input invalidates this attempt before timing. */
    for (unsigned scenario = 0; scenario < 10; ++scenario)
    {
        CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
        campaign = (TpRetirementCampaign){0};
        if (scenario == 0) ab.samples.rows[0].metrics ^= TP_RETIREMENT_SAMPLE_RUNTIME;
        if (scenario == 1) ab.samples.groups[0].kind = TP_RETIREMENT_GROUP_OBJECT;
        if (scenario == 2) ab.execution.runtime_rows[0] = 5;
        if (scenario == 3) plan.pairs_per_round = 62;
        if (scenario == 4) frozen.valid = 0;
        if (scenario == 6) ab.samples.rows = aa.samples.rows;
        if (scenario == 7) commands[0][0].exit_status = 1;
        if (scenario == 8) commands[1][2].unit = 5;
        if (scenario == 9) commands[0][1].artifact = NULL; /* A singleton group names its artifact. */
        int ok = tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
            &frozen, &frozen, &frozen, commands[0], commands[1], scenario == 5 ? NULL : snapshots, 8,
            identities, 2, 7, 0, identity, identity);
        CHECK(!ok && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
              aa.samples.failed && ab.samples.failed && !aa.execution.sequence);
        plan.pairs_per_round = 60;
        frozen.valid = 1;
        commands[0][0].exit_status = 0;
        commands[1][2].unit = 6;
        commands[0][1].artifact = arguments[0][1][3];
        test_sample_close(&aa); test_sample_close(&ab);
    }
    /* A metrics bound without an object group is an inconsistent shape. */
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    CHECK(!tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 2, 7, 1, identity, identity) && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID);
    test_sample_close(&aa); test_sample_close(&ab);

    /* A changed host observation cannot be retroactively attached to the
     * frozen service plan, even with a valid command and output oracle. */
    CHECK(test_retirement_campaign_open(&aa, &ab, cpu));
    campaign = (TpRetirementCampaign){0};
    CHECK(tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 2, 7, 0, identity, identity));
    aa.transcript.cpu = -1;
    log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    inputs.log = log;
    CHECK(!tp_retirement_campaign_run(&campaign, &commands[0][0], &inputs, cwd, &measured) &&
          measured.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !aa.execution.sequence);
    if (log >= 3) CHECK(close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_sample_close(&aa); test_sample_close(&ab);

    if (leak >= 3) CHECK(close(leak) == 0);
    if (cwd >= 3) CHECK(close(cwd) == 0);
    if (candidate_binary >= 3) CHECK(close(candidate_binary) == 0);
    if (binary >= 3) CHECK(close(binary) == 0);
}
#endif
#endif
