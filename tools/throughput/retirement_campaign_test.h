/* Private functional fixtures for #1022. The integration owner registers this
 * in tests.c after the collection handoff is integrated; no fixture measures
 * an acceptance candidate or asserts an approved #426 noise verdict.
 * (A1, M4) The campaign fixture has one timed object batch group (census rows
 * 4 and 5, bound through the correctness gate's frozen batch contract and the
 * `@file` response file) and one singleton link group (row 6) with native
 * runtime; every freeze carries the reviewed test budget and its pin. */
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

/* One reviewed metrics bound per object group and no untimed groups. */
static int test_retirement_campaign_capacity_shape(unsigned groups, unsigned objects, unsigned rows,
    unsigned runtime, unsigned pairs, uint64_t metrics_per_group, TpRetirementCampaignCapacity* capacity)
{
    TpRetirementCampaignShape shape = {groups, objects, rows, runtime, pairs, 0, 0,
        (uint64_t)objects * metrics_per_group, 0, objects ? metrics_per_group : 0};
    return tp_retirement_campaign_capacity(&shape, capacity);
}

/* A realistic A1 native-host campaign: 16 configurations, each with a
 * compiler-default group of up to 416 inputs (404 fixtures plus 12 frozen
 * rejection controls) and four small recipe groups (4, 1, 1 and 1 inputs), two
 * stage singletons with native runtime, and the same group shapes on eleven
 * cross-target untimed targets. per_input is the reviewed per-input metrics
 * bound with a 4 KiB header. */
static int test_retirement_campaign_a1_shape(unsigned pairs, uint64_t per_input, TpRetirementCampaignCapacity* capacity)
{
    static unsigned const sizes[] = {416, 4, 1, 1, 1};
    uint64_t per_configuration = 0, largest = 4096 + 416 * per_input;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(sizes); ++i) per_configuration += 4096 + sizes[i] * per_input;
    TpRetirementCampaignShape shape = {82, 80, 16 * 411 + 2, 2, pairs, 11 * 80, 11 * 80,
        16 * per_configuration, 11 * 16 * per_configuration, largest};
    return tp_retirement_campaign_capacity(&shape, capacity);
}

/* The reviewed budget record: canonical text, strict decoding, the pin digest,
 * and the preflight derivation over the frozen counts. */
static void test_retirement_campaign_budget(void)
{
    TpRetirementCampaignBudget budget = test_retirement_budget();
    char text[TP_RETIREMENT_BUDGET_BYTES], digest[65], other[65];
    size_t size = tp_retirement_budget_encode(&budget, text, sizeof(text));
    CHECK(size && tp_retirement_budget_valid(&budget) && tp_retirement_budget_digest(&budget, digest));
    CHECK(!strncmp(text, "schema=" TP_RETIREMENT_BUDGET_SCHEMA "\nderivation=", 7 + 32 + 12) &&
          strstr(text, "\nreviewed-ns=36000000000000\n") && strstr(text, "\nmetrics-input-bytes=16384\n") &&
          strstr(text, "\nbatch=1:40000000\nbatch=4:60000000\nbatch=1024:2000000000\n"));
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, text, (u64)size);
    sha256_finish_hex(&hash, other);
    CHECK(!strcmp(digest, other));
    TpRetirementCampaignBudget decoded;
    CHECK(tp_retirement_budget_decode(text, size, &decoded) && !memcmp(&decoded, &budget, sizeof(budget)));
    /* Any other spelling, order or extra line rejects the installed record. */
    char mutated[TP_RETIREMENT_BUDGET_BYTES + 64];
    char const* const edits[][2] = {
        {"reviewed-ns=36000000000000", "reviewed-ns=036000000000000"},
        {"cleanup-ns=20000000000\nruntime-process-ns", "runtime-process-ns=50000000\ncleanup-ns"},
        {"batch=1:40000000\nbatch=4:60000000\nbatch=1024:2000000000\n", ""},
        {"batch=1:40000000", "batch=4:40000000"},
        {"batch=1:40000000", "batch=1:70000000"},
        {"batch=1024:2000000000\n", "batch=1024:2000000000\nextra=1\n"},
        {"derivation=fixed", "derivation=other"},
        {"metrics-header-bytes=4096", "metrics-header-bytes=0"},
        {"batch=1024:2000000000", "batch=1025:2000000000"}};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(edits); ++i)
    {
        char const* at = strstr(text, edits[i][0]);
        size_t prefix = at ? (size_t)(at - text) : 0, old_length = strlen(edits[i][0]);
        size_t new_length = strlen(edits[i][1]);
        CHECK(at != NULL);
        if (!at) continue;
        memcpy(mutated, text, prefix);
        memcpy(mutated + prefix, edits[i][1], new_length);
        memcpy(mutated + prefix + new_length, at + old_length, size - prefix - old_length);
        size_t mutated_size = size - old_length + new_length;
        CHECK(!tp_retirement_budget_decode(mutated, mutated_size, &decoded) &&
              !decoded.reviewed_ns && !decoded.classes);
    }
    CHECK(!tp_retirement_budget_decode(text, size - 1, &decoded));
    /* Every bound is an input of the pin: changing one changes the digest. */
    TpRetirementCampaignBudget changed = budget;
    changed.cleanup_ns += 1;
    CHECK(tp_retirement_budget_digest(&changed, other) && strcmp(other, digest));
    changed = budget;
    changed.batch[2].batch_ns += 1;
    CHECK(tp_retirement_budget_digest(&changed, other) && strcmp(other, digest));
    /* Batch bounds by group size; metrics bound by input count. */
    uint64_t value = 0;
    CHECK(tp_retirement_budget_batch_ns(&budget, 1, &value) && value == 40000000);
    CHECK(tp_retirement_budget_batch_ns(&budget, 2, &value) && value == 60000000);
    CHECK(tp_retirement_budget_batch_ns(&budget, 416, &value) && value == 2000000000);
    CHECK(!tp_retirement_budget_batch_ns(&budget, 1025, &value) && !value);
    CHECK(!tp_retirement_budget_batch_ns(&budget, 0, &value));
    CHECK(tp_retirement_budget_metrics_bytes(&budget, 416, &value) && value == 4096 + 416 * 16384);
    changed = budget;
    changed.metrics_input_bytes = TP_RETIREMENT_METRICS_ARTIFACT_BYTES / 16;
    CHECK(!tp_retirement_budget_metrics_bytes(&changed, 17, &value) && !value);
    uint64_t* required[] = {&changed.reviewed_ns, &changed.reservation_ns, &changed.materialization_ns,
        &changed.baseline_build_ns, &changed.candidate_build_ns, &changed.correctness_ns,
        &changed.settling_per_stage_ns, &changed.aa_qualification_ns, &changed.aa_receipt_sealing_ns,
        &changed.sample_export_per_stage_ns, &changed.final_statistics_ns, &changed.final_sealing_ns,
        &changed.cleanup_ns, &changed.runtime_process_ns, &changed.metrics_header_bytes,
        &changed.metrics_input_bytes};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(required); ++i)
    {
        changed = budget;
        *required[i] = 0;
        CHECK(!tp_retirement_budget_valid(&changed) && !tp_retirement_budget_encode(&changed, text, sizeof(text)));
    }
    changed = budget;
    changed.batch[1].batch_ns = 30000000; /* A larger group may not be cheaper. */
    CHECK(!tp_retirement_budget_valid(&changed));
    changed = budget;
    changed.classes = 0;
    CHECK(!tp_retirement_budget_valid(&changed));
    changed = budget;
    changed.batch[3] = (TpRetirementBudgetClass){1, 1}; /* Beyond `classes`. */
    CHECK(!tp_retirement_budget_valid(&changed));

    /* The derivation over the realistic A1 counts at 254 pairs. */
    unsigned groups[82], untimed[880];
    static unsigned const sizes[] = {416, 4, 1, 1, 1};
    for (unsigned i = 0; i < 80; ++i) groups[i] = sizes[i % 5];
    groups[80] = groups[81] = 1;
    for (unsigned i = 0; i < 880; ++i) untimed[i] = sizes[i % 5];
    TpRetirementBudgetCounts counts = {groups, untimed, 82, 2, 254, 880};
    TpRetirementBudgetPreflight preflight;
    /* The test record's ten-hour ceiling cannot hold this campaign at these
     * bounds (about 20 hours); a reviewed 28-hour ceiling can. */
    CHECK(!tp_retirement_budget_preflight(&budget, &counts, &preflight) && !preflight.required_ns);
    TpRetirementCampaignBudget a1_budget = budget;
    a1_budget.reviewed_ns = UINT64_C(100800000000000);
    /* per group: 2 stages * 2 variants * (2 warmups + 2 * 254) = 2040 batches. */
    uint64_t fixed = UINT64_C(1000000000) + 2000000000 + 3000000000 + 3000000000 + 4000000000 +
        2 * UINT64_C(500000000) + 600000000 + 700000000 + 2 * UINT64_C(800000000) + 900000000 + 1000000000 +
        UINT64_C(20000000000);
    uint64_t compiler = UINT64_C(2040) * (16 * (UINT64_C(2000000000) + 60000000 + 3 * UINT64_C(40000000)) +
        2 * UINT64_C(40000000));
    uint64_t runtime = UINT64_C(2040) * 2 * 50000000;
    uint64_t untimed_ns = 4 * UINT64_C(176) * (UINT64_C(2000000000) + 60000000 + 3 * UINT64_C(40000000));
    CHECK(tp_retirement_budget_preflight(&a1_budget, &counts, &preflight) && preflight.fits &&
          preflight.fixed_ns == fixed && preflight.compiler_ns == compiler && preflight.runtime_ns == runtime &&
          preflight.untimed_ns == untimed_ns && preflight.required_ns == fixed + compiler + runtime + untimed_ns &&
          preflight.remaining_ns == a1_budget.reviewed_ns - preflight.required_ns &&
          preflight.compiler_batches == UINT64_C(82) * 2040 && preflight.runtime_processes == UINT64_C(4080) &&
          preflight.untimed_batches == UINT64_C(3520));
    /* A reviewed ceiling one nanosecond short, a group beyond every class, a
     * missing runtime bound or an odd pair count rejects before timing. */
    changed = a1_budget;
    changed.reviewed_ns = preflight.required_ns - 1;
    CHECK(!tp_retirement_budget_preflight(&changed, &counts, &preflight) && !preflight.fits &&
          !preflight.required_ns);
    changed.reviewed_ns = fixed + compiler + runtime + untimed_ns;
    CHECK(tp_retirement_budget_preflight(&changed, &counts, &preflight) && !preflight.remaining_ns);
    groups[0] = 1025;
    CHECK(!tp_retirement_budget_preflight(&a1_budget, &counts, &preflight));
    groups[0] = 416;
    untimed[0] = 0;
    CHECK(!tp_retirement_budget_preflight(&a1_budget, &counts, &preflight));
    untimed[0] = 416;
    counts.pairs = 253;
    CHECK(!tp_retirement_budget_preflight(&a1_budget, &counts, &preflight));
    counts.pairs = 256;
    CHECK(!tp_retirement_budget_preflight(&a1_budget, &counts, &preflight));
    counts.pairs = 254;
    counts.runtime_rows = 83;
    CHECK(!tp_retirement_budget_preflight(&a1_budget, &counts, &preflight));
    counts.runtime_rows = 2;
    CHECK(!tp_retirement_budget_preflight(&a1_budget, NULL, &preflight));
    CHECK(tp_retirement_budget_preflight(&a1_budget, &counts, &preflight));
    /* At 60 pairs the same campaign fits the ten-hour test ceiling. */
    counts.pairs = 60;
    CHECK(tp_retirement_budget_preflight(&budget, &counts, &preflight) && preflight.fits);
}

/* The metrics shard writer: greedy rotation onto the spare stream, per
 * artifact (shard, offset, length, SHA-256) descriptors, and the consumed
 * rotated descriptor before the next rotation. */
static void test_retirement_campaign_metrics_shards(void)
{
    size_t size = (size_t)(TP_RETIREMENT_METRICS_SHARD_BYTES / 2 - 1024);
    unsigned char* data = (unsigned char*)malloc(size);
    CHECK(data != NULL);
    if (!data) return;
    memset(data, 'm', size);
    FILE* streams[3] = {tmpfile(), tmpfile(), tmpfile()};
    TpRetirementMetricsShards shards;
    TpRetirementMetricsArtifact artifact;
    TpRetirementShardFile completed;
    CHECK(streams[0] && streams[1] && streams[2] && tp_retirement_metrics_shards_init(&shards, "aa", streams[0]));
    CHECK(tp_retirement_metrics_shards_append(&shards, data, size, &artifact) && !artifact.offset &&
          artifact.bytes == size && !strcmp(artifact.path, "retirement-metrics-aa-0000.txt"));
    CHECK(tp_retirement_metrics_shards_append(&shards, data, size, &artifact) && artifact.offset == size);
    /* A third artifact does not fit: without a spare it poisons the writer. */
    TpRetirementMetricsShards copy = shards;
    CHECK(!tp_retirement_metrics_shards_append(&copy, data, size, &artifact) && copy.failed && !artifact.bytes);
    CHECK(tp_retirement_metrics_shards_spare(&shards, streams[1]));
    CHECK(tp_retirement_metrics_shards_append(&shards, data, size, &artifact) && !artifact.offset &&
          !strcmp(artifact.path, "retirement-metrics-aa-0001.txt") && shards.completed_ready);
    /* The rotated shard must be taken before anything else is appended. */
    CHECK(!tp_retirement_metrics_shards_append(&copy, data, 1, &artifact));
    copy = shards;
    CHECK(!tp_retirement_metrics_shards_append(&copy, data, 1, &artifact) && copy.failed);
    CHECK(tp_retirement_metrics_shards_take(&shards, &completed) &&
          !strcmp(completed.path, "retirement-metrics-aa-0000.txt") && completed.contents.records == 2 &&
          completed.contents.bytes == 2 * (uint64_t)size);
    Sha256 hash;
    char expected[65];
    sha256_init(&hash);
    sha256_add(&hash, data, (u64)size);
    sha256_add(&hash, data, (u64)size);
    sha256_finish_hex(&hash, expected);
    CHECK(!strcmp(completed.contents.sha256, expected));
    CHECK(tp_retirement_metrics_shards_append(&shards, data, 16, &artifact) && artifact.offset == size &&
          !strcmp(artifact.path, "retirement-metrics-aa-0001.txt"));
    TpRetirementShardFile last;
    CHECK(tp_retirement_metrics_shards_finish(&shards, &last) && last.contents.records == 2 &&
          last.contents.bytes == size + 16 && shards.artifacts == 4 && shards.index == 1);
    CHECK(!tp_retirement_metrics_shards_append(&shards, data, 16, &artifact));
    /* Streams must start empty and the spare must be a different stream. */
    CHECK(!tp_retirement_metrics_shards_init(&copy, "aa", streams[0]) && copy.failed);
    CHECK(tp_retirement_metrics_shards_init(&copy, "ab", streams[2]) &&
          !tp_retirement_metrics_shards_spare(&copy, streams[2]));
    CHECK(!tp_retirement_metrics_shards_init(&copy, "a-b", streams[2]));
    CHECK(!tp_retirement_metrics_shards_append(&copy, data, 0, &artifact));
    /* The shard-count bound of the store preflight: any two consecutive
     * greedy shards exceed one shard's capacity. */
    CHECK(tp_retirement_campaign_metrics_shards(4, 3 * (uint64_t)size + 16) == 4);
    CHECK(tp_retirement_campaign_metrics_shards(244, UINT64_C(12992512)) == 2);
    CHECK(tp_retirement_campaign_metrics_shards(1000, 10 * TP_RETIREMENT_METRICS_SHARD_BYTES) == 20);
    CHECK(tp_retirement_campaign_metrics_shards(0, 0) == 0);
    for (unsigned i = 0; i < 3; ++i)
        if (streams[i]) CHECK(fclose(streams[i]) == 0);
    free(data);
}

static void test_retirement_campaign_capacity(void)
{
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
          large.total_transcript_bytes_upper_bound == UINT64_C(36102286848) &&
          large.total_sample_bytes_upper_bound == UINT64_C(5755622400) &&
          large.total_payload_bytes_upper_bound == UINT64_C(41857909248) &&
          large.total_transcript_shards == 542 && large.total_sample_shards == 134 &&
          large.total_shard_files == 676 && large.total_payload_files == 676 &&
          !large.total_metrics_shards_upper_bound && !large.untimed_record_files);
    CHECK(test_retirement_campaign_capacity_shape(72672, 0, 72672, 72672, 60, 0, &large) &&
          large.compiler_invocations_per_stage == UINT64_C(17731968) &&
          large.runtime_invocations_per_stage == UINT64_C(17731968) &&
          large.invocations_per_stage == UINT64_C(35463936) &&
          large.total_invocations == UINT64_C(70927872) &&
          large.total_shard_files == 1218 &&
          large.total_payload_bytes_upper_bound == UINT64_C(77960196096));
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
          large.total_payload_bytes_upper_bound == UINT64_C(136046174240));
    CHECK(!test_retirement_campaign_capacity_shape(77762, 0, 77762, 0, 186, 0, &large) &&
          test_retirement_campaign_capacity_is_zero(&large));
    CHECK(test_retirement_campaign_capacity_shape(77762, 0, 77762, 77762, 98, 0, &large) &&
          large.total_shard_files == 2114 &&
          large.total_payload_bytes_upper_bound == UINT64_C(135451450464));
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
    /* Metrics artifacts are byte ranges of metrics shards, not store entries:
     * one 1 MiB-bound object group writes 244 artifacts per stage into at
     * most 2 * ceil(bytes / 64 MiB) shards. */
    CHECK(test_retirement_campaign_capacity_shape(2, 1, 3, 1, 60, UINT64_C(1048576), &large) &&
          large.invocations_per_stage == 732 && large.row_samples_per_stage == 360 &&
          large.batch_samples_per_stage == 120 && large.samples_per_stage == 480 &&
          large.metrics_artifacts_per_stage == 244 && large.total_metrics_artifacts == 488 &&
          large.metrics_bytes_per_stage_upper_bound == UINT64_C(255852544) &&
          large.metrics_shards_per_stage_upper_bound == 8 && large.total_metrics_shards_upper_bound == 16 &&
          large.sample_bytes_per_stage_upper_bound ==
              360 * TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX + 120 * TP_RETIREMENT_BATCH_RECORD_BYTES_MAX &&
          large.sample_shards_per_stage == 2 && large.sample_partitions_per_stage == 2 &&
          large.total_shard_files == 22 && large.total_payload_files == 22 &&
          large.total_payload_bytes_upper_bound == UINT64_C(513496880));
    /* (M4) A realistic A1 campaign fits both store ceilings at 60 and at the
     * 254-pair maximum with a 4 KiB or an 8 KiB reviewed per-input metrics
     * bound, including the untimed cross-target batches. 16 KiB per input
     * does not fit at 254 pairs and fails closed before timing. */
    CHECK(test_retirement_campaign_a1_shape(60, 4096, &large) && large.total_payload_files == 465 &&
          large.total_payload_bytes_upper_bound == UINT64_C(15492729152) &&
          large.metrics_shards_per_stage_upper_bound == 204 && large.untimed_metrics_shards_upper_bound == 38 &&
          large.untimed_batches_upper_bound == 3520 && large.untimed_record_files == 1);
    CHECK(test_retirement_campaign_a1_shape(254, 4096, &large) && large.total_payload_files == 1805 &&
          large.total_payload_bytes_upper_bound == UINT64_C(60859132512) &&
          large.invocations_per_stage == UINT64_C(85680) && large.metrics_artifacts_per_stage == UINT64_C(81600) &&
          large.samples_per_stage == UINT64_C(3382264));
    CHECK(test_retirement_campaign_a1_shape(254, 8192, &large) && large.total_payload_files == 3525 &&
          large.total_payload_bytes_upper_bound == UINT64_C(118631213664));
    CHECK(test_retirement_campaign_a1_shape(60, 16384, &large) && large.total_payload_files == 1785);
    CHECK(!test_retirement_campaign_a1_shape(254, 16384, &large) && test_retirement_campaign_capacity_is_zero(&large));
    /* Inconsistent shapes are rejected. */
    CHECK(!test_retirement_campaign_capacity_shape(0, 0, 1, 0, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(1, 0, 1, 2, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(2, 1, 3, 2, 60, 1, &large)); /* Runtime in an object group. */
    CHECK(!test_retirement_campaign_capacity_shape(2, 3, 3, 0, 60, 1, &large));
    CHECK(!test_retirement_campaign_capacity_shape(3, 0, 2, 0, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(2, 1, 3, 1, 60, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(2, 1, 3, 1, 60, TP_RETIREMENT_METRICS_ARTIFACT_BYTES + 1, &large));
    CHECK(!test_retirement_campaign_capacity_shape(1, 0, 1, 0, 61, 0, &large));
    CHECK(!test_retirement_campaign_capacity_shape(77792, 0, 77792, 0, 254, 0, &large));
    {
        TpRetirementCampaignShape shape = {1, 0, 1, 0, 60, 0, 0, 1, 0, 1}; /* Metrics without an object group. */
        CHECK(!tp_retirement_campaign_capacity(&shape, &large));
        shape = (TpRetirementCampaignShape){2, 1, 3, 1, 60, 1, 1, 100, 200, 100}; /* Untimed above the max. */
        CHECK(!tp_retirement_campaign_capacity(&shape, &large));
        shape.untimed_metrics_bytes = 100;
        CHECK(tp_retirement_campaign_capacity(&shape, &large) && large.untimed_metrics_artifacts_upper_bound == 4 &&
              large.untimed_metrics_bytes_upper_bound == 400 && large.untimed_metrics_shards_upper_bound == 2 &&
              large.untimed_record_bytes_upper_bound == 4 * TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX);
        shape.untimed_object_groups = 2;
        CHECK(!tp_retirement_campaign_capacity(&shape, &large));
    }
    CHECK(!tp_retirement_campaign_capacity(NULL, &large) && test_retirement_campaign_capacity_is_zero(&large));
    uint64_t capacity_value = 1;
    CHECK(!tp_retirement_campaign_u64_mul(UINT64_MAX, 2, &capacity_value) && !capacity_value);
    capacity_value = 1;
    CHECK(!tp_retirement_campaign_u64_add(UINT64_MAX, 1, &capacity_value) && !capacity_value);
    /* The exact store preflight adds the caller's external reservation. */
    TpRetirementCampaignStorePlan store_plan;
    uint64_t receipt = TP_RETIREMENT_RECEIPT_BYTES;
    uint64_t spare_bytes = TP_RETIREMENT_STORE_TOTAL_BYTES - UINT64_C(136046174240) - receipt;
    CHECK(test_retirement_campaign_capacity_shape(77762, 0, 77762, 0, 184, 0, &large));
    /* The store owns the payload plus the receipt; both are in its plan. */
    CHECK(tp_retirement_campaign_store_preflight(&large, 1, receipt, 3, 1024, &store_plan) &&
          store_plan.owned_files == 2197 && store_plan.owned_bytes == UINT64_C(136046174240) + receipt &&
          store_plan.external_entries == 3 && store_plan.external_bytes == 1024 &&
          store_plan.entries == 2200 && store_plan.bytes == UINT64_C(136046174240) + receipt + 1024 &&
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
    /* The realistic A1 campaign's metrics shards, untimed shards and record
     * file are payload entries and bytes of the exact preflight. */
    CHECK(test_retirement_campaign_a1_shape(254, 4096, &large) &&
          tp_retirement_campaign_store_preflight(&large, 1, receipt, 3, 0, &store_plan) &&
          store_plan.owned_files == 1806 && store_plan.owned_bytes == UINT64_C(60859132512) + receipt);
    mismatched = large;
    ++mismatched.untimed_record_bytes_upper_bound;
    CHECK(!tp_retirement_campaign_store_preflight(&mismatched, 1, receipt, 3, 0, &store_plan));
    mismatched = large;
    ++mismatched.total_metrics_shards_upper_bound;
    CHECK(!tp_retirement_campaign_store_preflight(&mismatched, 1, receipt, 3, 0, &store_plan));
}

/* Both stages: census rows 4 and 5 form the timed object group (dense group
 * 0), row 6 is the singleton link group (dense group 1) with native runtime.
 * attach is zero to leave the stage without a metrics shard writer. */
static int test_retirement_campaign_open_stage(TpSampleTest* test, int cpu, TpRetirementMetricsShards* metrics,
    FILE** metrics_stream, char const* tag, int attach)
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
        tp_retirement_transcript_init(&test->transcript, &test->execution, "job-1", 2, "boot-123", cpu, 1000) &&
        tp_retirement_transcript_begin_shard(&test->transcript, test->stream) &&
        tp_retirement_samples_init(&test->samples, &test->transcript, test->spool, &test->layout, test->rows,
            test->groups, test->members) &&
        tp_retirement_metrics_shards_init(metrics, tag, *metrics_stream) &&
        (!attach || tp_retirement_samples_attach_metrics(&test->samples, metrics));
    return ok;
}

typedef struct TestCampaignStages
{
    TpSampleTest aa, ab;
    TpRetirementMetricsShards metrics[2];
    FILE* metrics_streams[2];
} TestCampaignStages;

static int test_retirement_campaign_open(TestCampaignStages* stages, int cpu, int attach)
{
    int ok = test_retirement_campaign_open_stage(&stages->aa, cpu, &stages->metrics[0], &stages->metrics_streams[0],
            "aa", attach) &&
        test_retirement_campaign_open_stage(&stages->ab, cpu, &stages->metrics[1], &stages->metrics_streams[1],
            "ab", attach);
    return ok;
}

static void test_retirement_campaign_close(TestCampaignStages* stages)
{
    test_sample_close(&stages->aa);
    test_sample_close(&stages->ab);
    for (unsigned i = 0; i < 2; ++i)
    {
        if (stages->metrics_streams[i]) CHECK(fclose(stages->metrics_streams[i]) == 0);
        stages->metrics_streams[i] = NULL;
    }
}

/* Retire one completed invocation's scratch outputs. */
static void test_retirement_campaign_retire(int cwd, TpRetirementMeasuredCommand const* command)
{
    CHECK(unlinkat(cwd, "child.log", 0) == 0);
    if (command->batch)
    {
        CHECK(unlinkat(cwd, command->batch->metrics, 0) == 0);
        for (unsigned i = 0; i < command->batch->input_count; ++i)
            if (command->batch->inputs[i].artifact) CHECK(unlinkat(cwd, command->batch->inputs[i].artifact, 0) == 0);
    }
    else if (!command->kind) CHECK(unlinkat(cwd, command->artifact, 0) == 0);
}

static void test_retirement_campaign(char const* executable_path, char const* root)
{
    test_retirement_campaign_budget();
    test_retirement_campaign_metrics_shards();
    test_retirement_campaign_capacity();
    char directory[TP_PATH_CAP], binary_path[TP_PATH_CAP], candidate_path[TP_PATH_CAP];
    CHECK(tp_path(directory, root, "campaign-fixture") && tp_mkdirs(directory) && chmod(directory, 0700) == 0);
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
    char object_output[65];
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
    /* The reviewed budget and its recipe pin. */
    TpRetirementCampaignBudget budget = test_retirement_budget();
    char budget_sha[65];
    CHECK(tp_retirement_budget_digest(&budget, budget_sha));
    TpRetirementCampaignReview review = {&budget, budget_sha, NULL, NULL, 0};
    /* The frozen object batch: rows 4 and 5, then a rejection control, with
     * the budget's metrics bound for three inputs and its response file. */
    char* words[] = {"+alpha", "+beta", "-control"};
    TestBatchFixture batch;
    CHECK(test_batch_contract(&batch, "batch.metrics", words, 3, artifact_sha));
    batch.inputs[0].row = 4;
    batch.inputs[1].row = 5;
    CHECK(tp_retirement_budget_metrics_bytes(&budget, 3, &batch.contract.metrics_bytes_max) &&
          tp_retirement_batch_contract_valid(&batch.contract) &&
          tp_retirement_batch_contract_output(&batch.contract, object_output));
    char list_leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP], list_argument[TP_RETIREMENT_INPUT_LIST_LEAF_CAP + 1];
    CHECK(tp_retirement_batch_input_list_write(cwd, &batch.contract, list_leaf));
    snprintf(list_argument, sizeof(list_argument), "@%s", list_leaf);
    char* environment[] = {"LC_ALL=C", "TP_RETIREMENT_TEST=explicit", NULL};
    char* arguments[2][6][11] = {0};
    char command_sha[2][6][65];
    /* Per stage: [object group baseline, candidate, link group baseline,
     * candidate, runtime row baseline, candidate]; G = 2, U = 1. The second
     * A/A label and the A/B candidate name another argv[0]; the child is the
     * held descriptor either way. */
    TpRetirementMeasuredCommand commands[2][6] = {0};
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned slot = 0; slot < 3; ++slot)
            for (unsigned variant = 0; variant < 2; ++variant)
            {
                unsigned index = slot * 2 + variant;
                char** argv = arguments[stage][index];
                argv[0] = variant ? (stage ? candidate_path : "fixture-child-label-2") : binary_path;
                argv[1] = "retirement-child";
                unsigned count = 6;
                if (!slot)
                {
                    char* batch_argv[] = {"batch", "batch.metrics", "ok", leak_text, list_argument,
                        "+alpha", "+beta", "-control"};
                    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(batch_argv); ++i) argv[2 + i] = batch_argv[i];
                    count = 10;
                }
                else
                {
                    argv[2] = slot == 2 ? "runtime" : "compiler";
                    argv[3] = variant ? "artifact-right.bin" : "artifact-left.bin";
                    argv[4] = "ok"; argv[5] = leak_text;
                }
                commands[stage][index] = (TpRetirementMeasuredCommand){.unit = slot == 2 ? 6 : slot,
                    .kind = slot == 2, .variant = variant, .arguments = argv, .argument_count = count,
                    .environment = environment, .environment_count = 2, .directory = directory,
                    .artifact = slot == 1 ? argv[3] : NULL, .batch = slot ? NULL : &batch.contract,
                    .timeout_seconds = 2, .command_sha256 = command_sha[stage][index],
                    .output_sha256 = slot == 2 ? runtime_sha : slot ? batch_output : object_output,
                    .exit_status = slot ? 0 : 1};
                CHECK(tp_retirement_command_hash(&commands[stage][index], command_sha[stage][index]));
            }
    int cpu = tp_first_allowed_cpu();
    CHECK(cpu >= 0);
    TestCampaignStages stages;
    memset(&stages, 0, sizeof(stages));
    TpSampleTest* aa = &stages.aa;
    TpSampleTest* ab = &stages.ab;
    TpRetirementPlan plan = {.version = TP_RETIREMENT_STATISTICS_VERSION, .seed = 1,
        .pairs_per_round = 60, .resamples = TP_RETIREMENT_MIN_RESAMPLES,
        .bootstrap_members_per_scope = 1, .cell_members_per_scope = 1,
        .frozen_before_samples = 1};
    char const* identity = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    TpRetirementCampaign campaign = {0};
    BqRetirementCampaignBinding binding = {0};
    BqRetirementHeldBinaries held = {.descriptors = {-1, -1}};
    TpRetirementCampaignCommand snapshots[12];
    unsigned identities[3];
    BqRetirementTrustedRow trusted[7] = {0};
    BqRetirementRowFact facts[7] = {0};
    BqRetirementCorrectness gate = {0};
    gate.prepared.rows = gate.rows_done = 7;
    gate.prepared.native_target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
    gate.eligible_rows = 3;
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
    for (unsigned row = 4; row < 6; ++row)
    {
        trusted[row].compiler_eligible = 1;
        trusted[row].stage = BQ_RETIREMENT_STAGE_OBJECT;
        trusted[row].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
        facts[row].compiler_eligible = 1;
        for (unsigned variant = 0; variant < 2; ++variant)
        {
            memcpy(trusted[row].compiler_command_sha256[variant], command_sha[1][variant], 65);
            memcpy(facts[row].side[variant].compiler_command_sha256, command_sha[1][variant], 65);
            memcpy(facts[row].side[variant].artifact_sha256, artifact_sha, 65);
        }
    }
    trusted[6].compiler_eligible = trusted[6].code_obligation = 1;
    trusted[6].stage = BQ_RETIREMENT_STAGE_LINK;
    trusted[6].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
    facts[6].runtime_eligible = facts[6].code_eligible = 1;
    memcpy(trusted[6].independent_oracle_sha256, runtime_sha, 65);
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        BqRetirementObservedSide* side = &facts[6].side[variant];
        memcpy(side->compiler_command_sha256, commands[1][2 + variant].command_sha256, 65);
        memcpy(side->artifact_sha256, artifact_sha, 65);
        memcpy(side->code_sha256, code_sha, 65);
        memcpy(side->runtime_command_sha256, commands[1][4 + variant].command_sha256, 65);
        side->code_bytes = 13;
    }
    /* (A1) Freeze the plan-v3 object group contract in the gate: rows 4 and
     * 5 bind through it; the partition, commands and artifacts must agree. */
    BqRetirementBatchGroup frozen_group = {{batch.contract, batch.contract}, {{0}}};
    memcpy(frozen_group.command_sha256[0], command_sha[1][0], 65);
    memcpy(frozen_group.command_sha256[1], command_sha[1][1], 65);
    uint8_t assigned[7];
    {
        BqRetirementCorrectness probe = gate;
        BqRetirementBatchGroup wrong = frozen_group;
        CHECK(!bq_retirement_correctness_batches(&probe, &wrong, 1, assigned, 6) && probe.failed);
        probe = gate;
        wrong.command_sha256[1][0] ^= 1;
        CHECK(!bq_retirement_correctness_batches(&probe, &wrong, 1, assigned, 7) && probe.failed);
        probe = gate;
        CHECK(!bq_retirement_correctness_batches(&probe, NULL, 0, assigned, 7)); /* Rows 4, 5 stay unbound. */
        TpRetirementBatchInput shifted[TEST_BATCH_INPUTS];
        memcpy(shifted, batch.inputs, sizeof(shifted));
        wrong = frozen_group;
        wrong.contract[1].inputs = shifted;
        shifted[1].diagnostic_sha256 = test_batch_control_digest; /* The sides disagree. */
        probe = gate;
        CHECK(!bq_retirement_correctness_batches(&probe, &wrong, 1, assigned, 7));
        memcpy(shifted, batch.inputs, sizeof(shifted));
        shifted[2].row = 5; /* A control that names a timed row. */
        wrong.contract[0].inputs = shifted;
        probe = gate;
        CHECK(!bq_retirement_correctness_batches(&probe, &wrong, 1, assigned, 7));
        shifted[2].row = 3; /* An untimed row is a valid control row. */
        wrong.contract[1].inputs = shifted;
        probe = gate;
        CHECK(bq_retirement_correctness_batches(&probe, &wrong, 1, assigned, 7) && probe.batches_frozen);
        probe = gate;
        facts[5].side[1].artifact_sha256[0] ^= 1;
        CHECK(!bq_retirement_correctness_batches(&probe, &frozen_group, 1, assigned, 7));
        facts[5].side[1].artifact_sha256[0] ^= 1;
        probe = gate;
        probe.finished = 1;
        CHECK(!bq_retirement_correctness_batches(&probe, &frozen_group, 1, assigned, 7));
    }
    CHECK(bq_retirement_correctness_batches(&gate, &frozen_group, 1, assigned, 7));
    CHECK(!bq_retirement_correctness_batches(&gate, &frozen_group, 1, assigned, 7) && gate.failed);
    gate.failed = 0;
    gate.finished = 1;
    /* The fixture precommits the second baseline labels, whose distinct
     * argv[0] or path give them different command hashes from the first A/A
     * label: one v3 entry per timed group, keyed by its smallest row. */
    sha256_init(&hash);
    static char const second_domain[] = BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN;
    sha256_add(&hash, second_domain, sizeof(second_domain) - 1);
    uint8_t object_ordinal[4] = {4, 0, 0, 0}, link_ordinal[4] = {6, 0, 0, 0}, no_runtime = 0, applicable_runtime = 1;
    sha256_add(&hash, object_ordinal, sizeof(object_ordinal));
    sha256_add(&hash, command_sha[0][1], 64);
    sha256_add(&hash, &no_runtime, sizeof(no_runtime));
    sha256_add(&hash, link_ordinal, sizeof(link_ordinal));
    sha256_add(&hash, command_sha[0][3], 64);
    sha256_add(&hash, &applicable_runtime, sizeof(applicable_runtime));
    sha256_add(&hash, command_sha[0][5], 64);
    sha256_finish_hex(&hash, gate.prepared.aa_second_commands_sha256);
    bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&gate) && bq_retirement_campaign_timed_rows(&gate) == 3 &&
          bq_retirement_campaign_timed_groups(&gate) == 2);
#define TEST_CAMPAIGN_BIND_HELD() bq_retirement_campaign_bind_held(&binding, &gate, &campaign, &plan, \
    &aa->samples, &ab->samples, &held, 1, 2, commands[0], commands[1], snapshots, 12, identities, 3, &review, \
    identity, identity)
#define TEST_CAMPAIGN_FREEZE() tp_retirement_campaign_freeze(&campaign, &plan, &aa->samples, &ab->samples, \
    &frozen, &frozen, &frozen, commands[0], commands[1], snapshots, 12, identities, 3, 7, &review, identity, identity)
    char candidate_identity[SHA256_HEX_CAPACITY];
    memcpy(candidate_identity, held.verified.binary_identity_sha256[1], sizeof(candidate_identity));
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        CHECK(test_retirement_campaign_open(&stages, cpu, 1));
        if (scenario == 0)
            aa->transcript.attempt = ab->transcript.attempt = 3;
        else if (scenario == 1)
            strcpy(ab->transcript.job, "job-2");
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
        CHECK(!TEST_CAMPAIGN_BIND_HELD() &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa->samples.failed && ab->samples.failed &&
            !aa->execution.sequence && !ab->execution.sequence && !binding.campaign);
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
        test_retirement_campaign_close(&stages);
    }
    /* (A1) A timed link row cannot pose as an object row outside its frozen
     * group, a cross-target row is never timed, and the gate's native target
     * must be the pinned x86_64-unknown-linux-gnu index, even when the rows
     * agree with it; each fails closed before timing. */
    for (unsigned scenario = 0; scenario < 2 + 12; ++scenario)
    {
        unsigned other = scenario - 2 + 1;
        if (scenario >= 2 && other == BQ_RETIREMENT_NATIVE_TIMED_TARGET) continue;
        if (scenario >= 2)
            gate.prepared.native_target = trusted[4].target = trusted[5].target = trusted[6].target = other;
        else if (scenario) trusted[6].target = 2;
        else trusted[6].stage = BQ_RETIREMENT_STAGE_OBJECT;
        bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        CHECK(test_retirement_campaign_open(&stages, cpu, 1));
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        CHECK(!TEST_CAMPAIGN_BIND_HELD() &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa->samples.failed && !binding.campaign);
        trusted[6].stage = BQ_RETIREMENT_STAGE_LINK;
        trusted[4].target = trusted[5].target = trusted[6].target = gate.prepared.native_target =
            BQ_RETIREMENT_NATIVE_TIMED_TARGET;
        bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        test_retirement_campaign_close(&stages);
    }
    /* (M4) Object rows bind only through the gate's frozen contracts and the
     * reviewed budget: a gate without them, a command contract or batch
     * command other than the frozen one, a budget other than its pin, one that
     * cannot hold the counts, a contract without the reviewed metrics bound, a
     * stage without a metrics shard writer, and an A/A second label outside
     * the sealed aggregate each fail closed before timing. */
    for (unsigned scenario = 0; scenario < 8; ++scenario)
    {
        TpRetirementCampaignBudget scenario_budget = budget;
        char scenario_sha[65];
        TestBatchFixture changed = batch;
        changed.contract.inputs = changed.inputs;
        char* saved_argv0 = arguments[0][1][0];
        if (scenario == 0)
        {
            gate.batch_groups = NULL;
            gate.batch_group_count = gate.batches_frozen = 0;
        }
        if (scenario == 1)
        {
            changed.inputs[2].diagnostic_sha256 = test_batch_empty_digest;
            commands[1][1].batch = &changed.contract;
        }
        if (scenario == 2) frozen_group.command_sha256[0][0] ^= 1;
        if (scenario == 3) review.budget_sha256 = identity;
        if (scenario == 4) scenario_budget.reviewed_ns = 1000;
        if (scenario == 5) scenario_budget.metrics_input_bytes = 8192;
        if (scenario == 4 || scenario == 5)
        {
            CHECK(tp_retirement_budget_digest(&scenario_budget, scenario_sha));
            review.budget = &scenario_budget;
            review.budget_sha256 = scenario_sha;
        }
        if (scenario == 7)
        {
            arguments[0][1][0] = "fixture-child-unsealed-label";
            CHECK(tp_retirement_command_hash(&commands[0][1], command_sha[0][1]));
        }
        bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        CHECK(bq_retirement_correctness_ready(&gate));
        CHECK(test_retirement_campaign_open(&stages, cpu, scenario != 6));
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        CHECK(!TEST_CAMPAIGN_BIND_HELD() && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
              aa->samples.failed && ab->samples.failed && !binding.campaign && !aa->execution.sequence);
        gate.batch_groups = &frozen_group;
        gate.batch_group_count = gate.batches_frozen = 1;
        commands[1][1].batch = &batch.contract;
        if (scenario == 2) frozen_group.command_sha256[0][0] ^= 1;
        review = (TpRetirementCampaignReview){&budget, budget_sha, NULL, NULL, 0};
        arguments[0][1][0] = saved_argv0;
        CHECK(tp_retirement_command_hash(&commands[0][1], command_sha[0][1]));
        bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        test_retirement_campaign_close(&stages);
    }
    CHECK(bq_retirement_correctness_ready(&gate));
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(TEST_CAMPAIGN_BIND_HELD());
    CHECK(binding.campaign == &campaign && binding.gate == &gate && !strcmp(binding.sealed_sha256, gate.sealed_sha256));
    CHECK(campaign.phase == TP_RETIREMENT_CAMPAIGN_AA && campaign.runtime_rows[0] == 6 && campaign.groups == 2 &&
          campaign.group_shapes[0] == ((2u << 1) | TP_RETIREMENT_GROUP_OBJECT) &&
          campaign.group_shapes[1] == ((1u << 1) | TP_RETIREMENT_GROUP_SINGLETON) &&
          campaign.capacity.invocations_per_stage == 732 &&
          campaign.capacity.samples_per_stage == 480 &&
          campaign.capacity.total_invocations == 1464 && campaign.capacity.total_samples == 960 &&
          campaign.capacity.spool_bytes_per_stage == 480 * TP_RETIREMENT_SAMPLE_RECORD_BYTES &&
          campaign.capacity.transcript_shards_per_stage == 1 && campaign.capacity.total_transcript_shards == 2 &&
          campaign.capacity.metrics_artifacts_per_stage == 244 &&
          campaign.capacity.metrics_bytes_per_stage_upper_bound == UINT64_C(244) * (4096 + 3 * 16384) &&
          campaign.capacity.metrics_shards_per_stage_upper_bound == 2 && campaign.capacity.total_payload_files == 10 &&
          !strcmp(campaign.budget_sha256, budget_sha) && campaign.budget.fits &&
          campaign.budget.compiler_batches == 976 && campaign.budget.runtime_processes == 488 &&
          campaign.budget.compiler_ns == UINT64_C(488) * (60000000 + 40000000) &&
          campaign.budget.runtime_ns == UINT64_C(488) * 50000000);
    for (unsigned stage = 0; stage < 2; ++stage)
    {
        TpSampleTest* active = stage ? ab : aa;
        TpRetirementInvocation invocation;
        int ok = 1;
        while (ok && tp_retirement_execution_peek(&active->execution, &invocation) == TP_RETIREMENT_NEXT_READY)
        {
            unsigned index = (invocation.kind ? 2 : invocation.group) * 2 + invocation.variant;
            int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            TpProcessInputs inputs = {held.descriptors[stage && invocation.variant], cwd, log, environment};
            TpRetirementMeasurementResult measured;
            ok = log >= 3 && bq_retirement_campaign_run(&binding, &commands[stage][index],
                &inputs, cwd, &measured);
            CHECK(ok && measured.status == TP_RETIREMENT_MEASUREMENT_COMPLETE &&
                  (!invocation.kind && !invocation.group ? measured.metrics.bytes > 0 : !measured.metrics.bytes));
            if (log >= 3) CHECK(close(log) == 0);
            if (ok) test_retirement_campaign_retire(cwd, &commands[stage][index]);
        }
        CHECK(ok && tp_retirement_execution_complete(&active->execution));
        TpRetirementShard final;
        /* The stage's metrics shard writer is finished before the stage. */
        TpRetirementShardFile metrics_shard;
        CHECK(tp_retirement_metrics_shards_finish(&stages.metrics[stage], &metrics_shard) &&
              metrics_shard.contents.records == 244 &&
              !strcmp(metrics_shard.path, stage ? "retirement-metrics-ab-0000.txt" : "retirement-metrics-aa-0000.txt"));
        CHECK(tp_retirement_campaign_finish_stage(&campaign, tp_process_monotonic_ns(), &final));
        CHECK(final.records == 732 && active->samples.collected == 732);
        CHECK(!tp_retirement_campaign_stage_ready(&campaign, stage));
        if (stage)
            CHECK(tp_retirement_campaign_outcome(&campaign).execution ==
                  TP_RETIREMENT_CAMPAIGN_STATE_RUNNING);
        for (unsigned population = 0; population < 2; ++population)
        {
            FILE* numeric = tmpfile();
            TpRetirementShard sample_shard;
            CHECK(numeric && tp_retirement_samples_write_shard(&active->samples, numeric, &sample_shard) &&
                  sample_shard.records == (population ? 120u : 360u));
            if (numeric) CHECK(fclose(numeric) == 0);
        }
        CHECK(tp_retirement_samples_finish(&active->samples));
        CHECK(tp_retirement_campaign_stage_ready(&campaign, stage));
        if (!stage)
        {
            CHECK(campaign.phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA && !ab->samples.collected);
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
    /* Row 6 (dense row 2) holds wall, memory and runtime pairs; rows 4 and 5
     * hold each member's per-input interval and arena bytes. */
    double ratios[120];
    for (unsigned i = 0; i < 120; ++i)
    {
        uint64_t values[TP_RETIREMENT_SAMPLE_VALUES], member[TP_RETIREMENT_SAMPLE_VALUES];
        CHECK(tp_retirement_sample_read(&ab->samples, 240 + i, values) &&
              tp_retirement_sample_read(&ab->samples, i, member));
        ratios[i] = (double)values[1] / (double)values[0];
        CHECK(values[2] && values[3] && values[4] && values[5] && member[0] && member[1] && member[2] &&
              member[3] && !member[4] && !member[5]);
    }
    TpRetirementSeries series = {.ratios = ratios, .ratio_count = 120, .cell_count = 1,
        .observed_pairs = {60, 60}, .member_kind = TP_RETIREMENT_EXACT_CELL_MEMBER,
        .metric_index = TP_RETIREMENT_WALL_TIME, .limit = 1.05};
    TpRetirementResult statistic = tp_retirement_assess(&plan, &series, NULL, 0);
    CHECK(statistic.valid && statistic.outcome != TP_RETIREMENT_INVALID);
    test_retirement_campaign_close(&stages);

    /* The held-record join is checked again at launch, after freeze. */
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(TEST_CAMPAIGN_BIND_HELD());
    char held_binary_digit = held.verified.binary_sha256[1][0];
    held.verified.binary_sha256[1][0] = held_binary_digit == 'e' ? 'f' : 'e';
    int held_guard_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    TpProcessInputs held_guard_inputs = {binary, cwd, held_guard_log, environment};
    TpRetirementMeasurementResult held_guard_result;
    struct stat held_guard_stat;
    CHECK(held_guard_log >= 3 && !bq_retirement_campaign_run(&binding, &commands[0][0],
        &held_guard_inputs, cwd, &held_guard_result) &&
        held_guard_result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
        campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa->samples.failed && ab->samples.failed &&
        !aa->execution.sequence && !ab->execution.sequence && fstat(held_guard_log, &held_guard_stat) == 0 &&
        held_guard_stat.st_size == 0 &&
        fstatat(cwd, "alpha.o", &held_guard_stat, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    held.verified.binary_sha256[1][0] = held_binary_digit;
    if (held_guard_log >= 3)
        CHECK(close(held_guard_log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_retirement_campaign_close(&stages);

    /* Every launch recounts the timed groups from the sealed gate rows: a
     * row that becomes timed after the first child stops the next one, even
     * without a reseal. */
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(TEST_CAMPAIGN_BIND_HELD());
    {
        TpRetirementInvocation first;
        CHECK(tp_retirement_execution_peek(&aa->execution, &first) == TP_RETIREMENT_NEXT_READY);
        unsigned index = (first.kind ? 2 : first.group) * 2 + first.variant;
        int recount_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        TpProcessInputs recount_inputs = {binary, cwd, recount_log, environment};
        TpRetirementMeasurementResult recount_result;
        CHECK(recount_log >= 3 && bq_retirement_campaign_run(&binding, &commands[0][index],
            &recount_inputs, cwd, &recount_result) && aa->execution.sequence == 1);
        if (recount_log >= 3) CHECK(close(recount_log) == 0);
        test_retirement_campaign_retire(cwd, &commands[0][index]);
        trusted[3].compiler_eligible = 1;
        trusted[3].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
        CHECK(tp_retirement_execution_peek(&aa->execution, &first) == TP_RETIREMENT_NEXT_READY);
        index = (first.kind ? 2 : first.group) * 2 + first.variant;
        recount_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        recount_inputs.log = recount_log;
        CHECK(recount_log >= 3 && !bq_retirement_campaign_run(&binding, &commands[0][index],
            &recount_inputs, cwd, &recount_result) &&
            recount_result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa->execution.sequence == 1);
        if (recount_log >= 3) CHECK(close(recount_log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
        trusted[3].compiler_eligible = 0;
        trusted[3].target = 0;
    }
    test_retirement_campaign_close(&stages);

    /* The lower-level freeze helper cannot launch without held service files. */
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    binding = (BqRetirementCampaignBinding){0};
    CHECK(bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa->samples, &ab->samples,
        &frozen, &frozen, commands[0], commands[1], snapshots, 12, identities, 3, &review, identity, identity));
    int unheld_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    TpProcessInputs unheld_inputs = {binary, cwd, unheld_log, environment};
    TpRetirementMeasurementResult unheld_result;
    struct stat unheld_stat;
    CHECK(unheld_log >= 3 && !bq_retirement_campaign_run(&binding, &commands[0][0],
        &unheld_inputs, cwd, &unheld_result) &&
        unheld_result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
        campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && aa->samples.failed && ab->samples.failed &&
        !aa->execution.sequence && !ab->execution.sequence && fstat(unheld_log, &unheld_stat) == 0 &&
        unheld_stat.st_size == 0 &&
        fstatat(cwd, "alpha.o", &unheld_stat, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    if (unheld_log >= 3) CHECK(close(unheld_log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_retirement_campaign_close(&stages);

    /* Both baseline-label-2 command kinds require the sealed precommit;
     * a different, self-consistent plan fails before the first timed child. */
    for (unsigned kind = 0; kind < 2; ++kind)
    {
        unsigned index = (kind + 1) * 2 + 1;
        CHECK(test_retirement_campaign_open(&stages, cpu, 1));
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        arguments[0][index][4] = "fail";
        CHECK(tp_retirement_command_hash(&commands[0][index], command_sha[0][index]));
        CHECK(bq_retirement_correctness_ready(&gate));
        CHECK(!bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa->samples, &ab->samples,
            &frozen, &frozen, commands[0], commands[1], snapshots, 12, identities, 3, &review, identity, identity) &&
            campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
            aa->samples.failed && ab->samples.failed &&
            !aa->execution.sequence && !ab->execution.sequence && !binding.campaign);
        arguments[0][index][4] = "ok";
        CHECK(tp_retirement_command_hash(&commands[0][index], command_sha[0][index]));
        test_retirement_campaign_close(&stages);
    }

    /* Recheck the full #1020 seal at the launch boundary. A changed fact or
     * self-consistent reseal after freeze cannot start even the first child. */
    for (unsigned scenario = 0; scenario < 2; ++scenario)
    {
        CHECK(test_retirement_campaign_open(&stages, cpu, 1));
        campaign = (TpRetirementCampaign){0};
        binding = (BqRetirementCampaignBinding){0};
        CHECK(TEST_CAMPAIGN_BIND_HELD());
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
            aa->samples.failed && ab->samples.failed &&
            !aa->execution.sequence && !ab->execution.sequence &&
            fstat(log, &unchanged) == 0 && unchanged.st_size == 0 &&
            fstatat(cwd, "alpha.o", &unchanged, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
        facts[6].side[1].artifact_sha256[0] = original_artifact_digit;
        if (scenario) bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
        if (log >= 3) CHECK(close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
        test_retirement_campaign_close(&stages);
    }

    /* Mutation of a frozen oracle fails before launch and poisons both stages. */
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    CHECK(TEST_CAMPAIGN_FREEZE());
    int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    TpProcessInputs inputs = {binary, cwd, log, environment};
    TpRetirementMeasurementResult measured;
    commands[0][0].output_sha256 = identity;
    CHECK(!tp_retirement_campaign_run(&campaign, &commands[0][0], &inputs, cwd, &measured) &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !aa->execution.sequence &&
          !ab->execution.sequence && aa->samples.failed && ab->samples.failed);
    commands[0][0].output_sha256 = object_output;
    if (log >= 3) CHECK(close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_retirement_campaign_close(&stages);

    /* A changed #1020 output oracle cannot be imported into a fresh timed
     * attempt, even if the command still has its original plan digest. */
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    char saved_artifact_digit = facts[6].side[1].artifact_sha256[0];
    facts[6].side[1].artifact_sha256[0] = saved_artifact_digit == 'e' ? 'f' : 'e';
    bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&gate));
    binding = (BqRetirementCampaignBinding){0};
    CHECK(!bq_retirement_campaign_bind(&binding, &gate, &campaign, &plan, &aa->samples, &ab->samples,
        &frozen, &frozen, commands[0], commands[1], snapshots, 12, identities, 3, &review, identity, identity) &&
        campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
        aa->samples.failed && ab->samples.failed && !aa->execution.sequence);
    facts[6].side[1].artifact_sha256[0] = saved_artifact_digit;
    bq_retirement_correctness_seal(&gate, gate.sealed_sha256);
    test_retirement_campaign_close(&stages);

    /* No guessed A/A eligibility or partial campaign can reach A/B. */
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    CHECK(TEST_CAMPAIGN_FREEZE());
    TpRetirementShard final;
    CHECK(!tp_retirement_campaign_finish_stage(&campaign, tp_process_monotonic_ns(), &final) &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && ab->samples.failed);
    test_retirement_campaign_close(&stages);

    /* Every rejected frozen input invalidates this attempt before timing. */
    TestBatchFixture unbounded = batch;
    unbounded.contract.inputs = unbounded.inputs;
    unbounded.contract.metrics_bytes_max = TEST_BATCH_METRICS_BYTES;
    for (unsigned scenario = 0; scenario < 13; ++scenario)
    {
        CHECK(test_retirement_campaign_open(&stages, cpu, 1));
        campaign = (TpRetirementCampaign){0};
        TpRetirementCampaignReview const* frozen_review = &review;
        if (scenario == 0) ab->samples.rows[2].metrics ^= TP_RETIREMENT_SAMPLE_RUNTIME;
        if (scenario == 1) ab->samples.groups[1].kind = TP_RETIREMENT_GROUP_OBJECT;
        if (scenario == 2) ab->execution.runtime_rows[0] = 5;
        if (scenario == 3) plan.pairs_per_round = 62;
        if (scenario == 4) frozen.valid = 0;
        if (scenario == 6) ab->samples.rows = aa->samples.rows;
        if (scenario == 7) commands[0][2].exit_status = 1;
        if (scenario == 8) commands[1][4].unit = 5;
        if (scenario == 9) commands[0][3].artifact = NULL; /* A singleton group names its artifact. */
        if (scenario == 10) commands[1][1].batch = &unbounded.contract; /* Not the reviewed metrics bound. */
        if (scenario == 11) frozen_review = NULL;
        if (scenario == 12) ab->samples.metrics = aa->samples.metrics; /* One writer for both stages. */
        int ok = tp_retirement_campaign_freeze(&campaign, &plan, &aa->samples, &ab->samples,
            &frozen, &frozen, &frozen, commands[0], commands[1], scenario == 5 ? NULL : snapshots, 12,
            identities, 3, 7, frozen_review, identity, identity);
        CHECK(!ok && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
              aa->samples.failed && ab->samples.failed && !aa->execution.sequence);
        plan.pairs_per_round = 60;
        frozen.valid = 1;
        commands[0][2].exit_status = 0;
        commands[1][4].unit = 6;
        commands[0][3].artifact = arguments[0][3][3];
        commands[1][1].batch = &batch.contract;
        test_retirement_campaign_close(&stages);
    }

    /* A changed host observation cannot be retroactively attached to the
     * frozen service plan, even with a valid command and output oracle. */
    CHECK(test_retirement_campaign_open(&stages, cpu, 1));
    campaign = (TpRetirementCampaign){0};
    CHECK(TEST_CAMPAIGN_FREEZE());
    aa->transcript.cpu = -1;
    log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    inputs.log = log;
    CHECK(!tp_retirement_campaign_run(&campaign, &commands[0][0], &inputs, cwd, &measured) &&
          measured.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID &&
          campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !aa->execution.sequence);
    if (log >= 3) CHECK(close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    test_retirement_campaign_close(&stages);
#undef TEST_CAMPAIGN_BIND_HELD
#undef TEST_CAMPAIGN_FREEZE

    CHECK(unlinkat(cwd, list_leaf, 0) == 0);
    if (leak >= 3) CHECK(close(leak) == 0);
    if (cwd >= 3) CHECK(close(cwd) == 0);
    if (candidate_binary >= 3) CHECK(close(candidate_binary) == 0);
    if (binary >= 3) CHECK(close(binary) == 0);
}
#endif
#endif
