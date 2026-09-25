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
    char leak_text[32], binary_sha[65], artifact_sha[65], code_sha[65], runtime_sha[65];
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
    char* environment[] = {"LC_ALL=C", "TP_RETIREMENT_TEST=explicit", NULL};
    char* arguments[2][4][7] = {0};
    char command_sha[2][4][65];
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
                commands[stage][index] = (TpRetirementMeasuredCommand){.row = 6,
                    .kind = kind, .variant = variant, .arguments = argv, .argument_count = 6,
                    .environment = environment, .environment_count = 2, .directory = directory,
                    .artifact = kind ? NULL : argv[3], .timeout_seconds = 2,
                    .command_sha256 = command_sha[stage][index],
                    .output_sha256 = kind ? runtime_sha : artifact_sha,
                    .code_section_sha256 = kind ? NULL : code_sha,
                    .code_section_bytes = kind ? 0 : 13};
                CHECK(tp_retirement_command_hash(&commands[stage][index], command_sha[stage][index]));
            }
    TpSampleTest aa, ab;
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    unsigned census_id[] = {6};
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    int cpu = tp_first_allowed_cpu();
    CHECK(cpu >= 0);
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    TpRetirementPlan plan = {.version = TP_RETIREMENT_STATISTICS_VERSION, .seed = 7,
        .pairs_per_round = 60, .resamples = TP_RETIREMENT_MIN_RESAMPLES,
        .bootstrap_members_per_scope = 1, .cell_members_per_scope = 1,
        .frozen_before_samples = 1};
    char const* identity = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    TpRetirementCampaign campaign = {0};
    BqRetirementCampaignBinding binding = {0};
    BqRetirementHeldBinaries held = {.descriptors = {-1, -1}};
    TpRetirementCampaignCommand snapshots[8];
    unsigned identities[3];
    BqRetirementTrustedRow trusted[7] = {0};
    BqRetirementRowFact facts[7] = {0};
    BqRetirementCorrectness gate = {0};
    gate.prepared.rows = gate.rows_done = 7;
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
    test_sample_close(&aa);
    test_sample_close(&ab);
    char candidate_identity[SHA256_HEX_CAPACITY];
    memcpy(candidate_identity, held.verified.binary_identity_sha256[1], sizeof(candidate_identity));
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
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
            identities, 3, identity, identity) &&
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
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
        &held, 1, 2, commands[0], commands[1], snapshots, 8,
        identities, 3, identity, identity));
    CHECK(binding.campaign == &campaign && binding.gate == &gate &&
          !strcmp(binding.sealed_sha256, gate.sealed_sha256));
    CHECK(campaign.phase == TP_RETIREMENT_CAMPAIGN_AA && campaign.row_ids[0] == 6 &&
          campaign.capacity.invocations_per_stage == 488 &&
          campaign.capacity.samples_per_stage == 120 &&
          campaign.capacity.total_invocations == 976 && campaign.capacity.total_samples == 240 &&
          campaign.capacity.spool_bytes_per_stage == 120 * TP_RETIREMENT_SAMPLE_RECORD_BYTES &&
          campaign.capacity.total_spool_bytes == 240 * TP_RETIREMENT_SAMPLE_RECORD_BYTES &&
          campaign.capacity.transcript_shards_per_stage == 1 &&
          campaign.capacity.total_transcript_shards == 2);
    TpRetirementCampaignCapacity large;
    CHECK(tp_retirement_campaign_capacity(72672, 0, 60, &large) &&
          large.samples_per_stage == UINT64_C(8720640) &&
          large.total_samples == UINT64_C(17441280) &&
          large.total_sample_partitions == 2);
    CHECK(!tp_retirement_campaign_capacity(72672, 72672, 254, &large));
    CHECK(!tp_retirement_campaign_capacity(0, 0, 60, &large));
    CHECK(!tp_retirement_campaign_capacity(1, 2, 60, &large));
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
        uint64_t values[9];
        CHECK(tp_retirement_sample_read(&ab.samples, i, values));
        ratios[i] = (double)values[1] / (double)values[0];
        CHECK(values[2] && values[3] && values[4] == 13 && values[5] == 13 &&
              values[6] && values[7]);
    }
    TpRetirementSeries series = {.ratios = ratios, .ratio_count = 120, .cell_count = 1,
        .observed_pairs = {60, 60}, .member_kind = TP_RETIREMENT_EXACT_CELL_MEMBER,
        .metric_index = TP_RETIREMENT_WALL_TIME, .limit = 1.05};
    TpRetirementResult statistic = tp_retirement_assess(&plan, &series, NULL, 0);
    CHECK(statistic.valid && statistic.outcome != TP_RETIREMENT_INVALID);
    test_sample_close(&aa);
    test_sample_close(&ab);

    /* The held-record join is checked again at launch, after freeze. */
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan,
        &aa.samples, &ab.samples, &held, 1, 2, commands[0], commands[1],
        snapshots, 8, identities, 3, identity, identity));
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
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, commands[0], commands[1], snapshots, 8, identities, 3, identity, identity));
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
        CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
        CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
            aa.runtime, 1, 60, aa.workspace, 5));
        CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
            ab.runtime, 1, 60, ab.workspace, 5));
        aa.transcript.cpu = ab.transcript.cpu = cpu;
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        arguments[0][index][4] = "fail";
        CHECK(tp_retirement_command_hash(&commands[0][index], command_sha[0][index]));
        CHECK(bq_retirement_correctness_ready(&gate));
        CHECK(!bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
            &frozen, &frozen, commands[0], commands[1], snapshots, 8,
            identities, 3, identity, identity) &&
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
        CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
        CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
            aa.runtime, 1, 60, aa.workspace, 5));
        CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
            ab.runtime, 1, 60, ab.workspace, 5));
        aa.transcript.cpu = ab.transcript.cpu = cpu;
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        CHECK(bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
            &held, 1, 2, commands[0], commands[1], snapshots, 8,
            identities, 3, identity, identity));
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
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    campaign = (TpRetirementCampaign){0};
    CHECK(tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 3, 7, identity, identity));
    int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    TpProcessInputs inputs = {binary, cwd, log, environment};
    TpRetirementMeasurementResult measured;
    commands[0][0].output_sha256 = identity;
    CHECK(!tp_retirement_campaign_run(&campaign, &commands[0][0], &inputs, cwd, &measured) &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !aa.execution.sequence &&
          !ab.execution.sequence && aa.samples.failed && ab.samples.failed);
    commands[0][0].output_sha256 = artifact_sha;
    if (log >= 3) CHECK(close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_sample_close(&aa); test_sample_close(&ab);

    /* A changed #1020 output oracle cannot be imported into a fresh timed
     * attempt, even if the command still has its original plan digest. */
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    campaign = (TpRetirementCampaign){0};
    facts[6].side[1].artifact_sha256[0] = 'e';
    bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&gate));
    binding = (BqRetirementCampaignBinding){0};
    CHECK(!bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 3, identity, identity) &&
        campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
        aa.samples.failed && ab.samples.failed && !aa.execution.sequence);
    facts[6].side[1].artifact_sha256[0] = 'd';
    test_sample_close(&aa); test_sample_close(&ab);

    /* No guessed A/A eligibility or partial campaign can reach A/B. */
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    campaign = (TpRetirementCampaign){0};
    CHECK(tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 3, 7, identity, identity));
    TpRetirementShard final;
    CHECK(!tp_retirement_campaign_finish_stage(&campaign, tp_process_monotonic_ns(), &final) &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && ab.samples.failed);
    test_sample_close(&aa); test_sample_close(&ab);

    /* Every rejected frozen input invalidates this attempt before timing. */
    for (unsigned scenario = 0; scenario < 7; ++scenario)
    {
        CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
        CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
            aa.runtime, 1, 60, aa.workspace, 5));
        CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
            ab.runtime, 1, 60, ab.workspace, 5));
        aa.transcript.cpu = ab.transcript.cpu = cpu;
        campaign = (TpRetirementCampaign){0};
        if (scenario == 0) ab.samples.rows[0].metrics ^= TP_RETIREMENT_SAMPLE_RUNTIME;
        if (scenario == 1) aa.execution.row_ids[0] = 7;
        if (scenario == 2) ab.execution.runtime_rows[0] = 1;
        if (scenario == 3) plan.pairs_per_round = 62;
        if (scenario == 4) frozen.valid = 0;
        if (scenario == 6) ab.samples.rows = aa.samples.rows;
        int ok = tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
            &frozen, &frozen, &frozen, commands[0], commands[1], scenario == 5 ? NULL : snapshots, 8,
            identities, 3, 7, identity, identity);
        CHECK(!ok && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
              aa.samples.failed && ab.samples.failed && !aa.execution.sequence);
        plan.pairs_per_round = 60;
        frozen.valid = 1;
        test_sample_close(&aa); test_sample_close(&ab);
    }

    /* A changed host observation cannot be retroactively attached to the
     * frozen service plan, even with a valid command and output oracle. */
    CHECK(test_sample_open(&aa, 1) && test_sample_open(&ab, 1));
    CHECK(tp_retirement_execution_init_rows(&aa.execution, 7, 1, census_id, 7,
        aa.runtime, 1, 60, aa.workspace, 5));
    CHECK(tp_retirement_execution_init_rows(&ab.execution, 7, 1, census_id, 7,
        ab.runtime, 1, 60, ab.workspace, 5));
    aa.transcript.cpu = ab.transcript.cpu = cpu;
    campaign = (TpRetirementCampaign){0};
    CHECK(tp_retirement_campaign_freeze(&campaign, &plan, &aa.samples, &ab.samples,
        &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 8,
        identities, 3, 7, identity, identity));
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
