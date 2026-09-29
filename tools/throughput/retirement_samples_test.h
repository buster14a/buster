/* Failure-first native paired-sample tests. Included only by tests.c. All
 * observations are synthetic; no service receipt or performance verdict. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_TEST_H
/* 1093 rows * 120 samples crosses one full 131072-record numeric shard. */
#define TP_SAMPLE_TEST_ROWS 1093u
/* Rows with row % 3 == 1 form object groups of up to four members. */
#define TP_SAMPLE_TEST_OBJECT_MEMBERS 4u

typedef struct TpSampleTest
{
    TpRetirementExecution execution;
    TpRetirementTranscript transcript;
    TpRetirementSamples samples;
    TpRetirementSampleRow rows[TP_SAMPLE_TEST_ROWS];
    TpRetirementSampleGroup groups[TP_SAMPLE_TEST_ROWS];
    unsigned members[TP_SAMPLE_TEST_ROWS];
    unsigned workspace[TP_SAMPLE_TEST_ROWS * 4];
    unsigned row_ids[TP_SAMPLE_TEST_ROWS], row_metrics[TP_SAMPLE_TEST_ROWS], runtime[TP_SAMPLE_TEST_ROWS];
    unsigned kinds[TP_SAMPLE_TEST_ROWS], offsets[TP_SAMPLE_TEST_ROWS + 1], layout_members[TP_SAMPLE_TEST_ROWS];
    unsigned group_of[TP_SAMPLE_TEST_ROWS];
    TpRetirementLayout layout;
    FILE* stream;
    FILE* spool;
    unsigned group_count, runtime_count, population;
} TpSampleTest;

/* Derive the synthetic A1 layout: a row with row % 3 == 1 joins the open
 * object group (at most four members), every other row is a runtime-eligible
 * singleton link group. Groups are ordered by their smallest member. */
static void test_sample_layout(TpSampleTest* test, unsigned rows, unsigned first_id)
{
    unsigned open = TP_RETIREMENT_NONE, open_members = 0, counts[TP_SAMPLE_TEST_ROWS] = {0};
    test->group_count = test->runtime_count = 0;
    for (unsigned row = 0; row < rows; ++row)
    {
        test->row_ids[row] = first_id + row;
        int object = row % 3 == 1;
        test->row_metrics[row] = object ? 0 : TP_RETIREMENT_SAMPLE_RUNTIME;
        if (!object) test->runtime[test->runtime_count++] = first_id + row;
        if (object && open != TP_RETIREMENT_NONE && open_members < TP_SAMPLE_TEST_OBJECT_MEMBERS)
            ++open_members;
        else
        {
            if (object) { open = test->group_count; open_members = 1; }
            test->kinds[test->group_count++] = object ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON;
        }
        test->group_of[row] = object ? open : test->group_count - 1;
        ++counts[test->group_of[row]];
    }
    test->offsets[0] = 0;
    for (unsigned group = 0; group < test->group_count; ++group) test->offsets[group + 1] = test->offsets[group] + counts[group];
    unsigned filled[TP_SAMPLE_TEST_ROWS] = {0};
    for (unsigned row = 0; row < rows; ++row)
    {
        unsigned group = test->group_of[row];
        test->layout_members[test->offsets[group] + filled[group]++] = row;
    }
    test->layout = (TpRetirementLayout){rows, test->group_count, test->row_ids, test->row_metrics, test->kinds,
        test->offsets, test->layout_members};
}

static int test_sample_open_layout(TpSampleTest* test, unsigned rows, unsigned first_id)
{
    memset(test, 0, sizeof(*test));
    test->stream = tmpfile();
    test->spool = tmpfile();
    int ok = test->stream && test->spool && rows && rows <= TP_SAMPLE_TEST_ROWS;
    if (ok) test_sample_layout(test, rows, first_id);
    test->population = first_id + rows;
    ok = ok && tp_retirement_execution_init(&test->execution, 1, test->group_count, test->runtime,
        test->runtime_count, test->population, 60, test->workspace, (size_t)test->group_count * 3 + test->runtime_count) &&
        tp_retirement_transcript_init(&test->transcript, &test->execution, "job-1", 2, "boot-123", 2, 1000) &&
        tp_retirement_transcript_begin_shard(&test->transcript, test->stream) &&
        tp_retirement_samples_init(&test->samples, &test->transcript, test->spool, &test->layout, test->rows,
            test->groups, test->members);
    return ok;
}

static int test_sample_open(TpSampleTest* test, unsigned rows)
{
    return test_sample_open_layout(test, rows, 0);
}

static void test_sample_close(TpSampleTest* test)
{
    if (test->stream) CHECK(fclose(test->stream) == 0);
    if (test->spool) CHECK(fclose(test->spool) == 0);
    test->stream = test->spool = NULL;
}

enum
{
    TEST_SAMPLE_VALID, TEST_SAMPLE_WRONG_METRICS, TEST_SAMPLE_MEMBER_COUNT, TEST_SAMPLE_MEMBER_OVERRUN,
    TEST_SAMPLE_MEMBER_MEMORY, TEST_SAMPLE_MEMBER_ROW
};

/* One synthetic invocation. An object batch reports each member's interval
 * inside the process and a distinct arena high-water mark. */
static int test_sample_observe(TpSampleTest* test, int bypass, unsigned mode)
{
    TpRetirementInvocation invocation;
    int ok = tp_retirement_execution_peek(&test->execution, &invocation) == TP_RETIREMENT_NEXT_READY;
    if (ok)
    {
        char const* const hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        uint64_t elapsed = 1000000 + invocation.sequence;
        TpProcessObservation observed = {.pid = 4000 + invocation.sequence, .start_token = 2000,
            .started_ns = test->transcript.last_end + 1,
            .finished_ns = test->transcript.last_end + 1 + elapsed, .valid = 1};
        unsigned unit = invocation.kind ? invocation.row : invocation.group;
        TpProcess process = {.wall_seconds = (double)elapsed / 1000000000.0,
            .peak_rss_bytes = (double)(4096 + unit + invocation.variant)};
        TpRetirementSampleGroup const* group = invocation.kind ? NULL : &test->groups[invocation.group];
        int object = group && group->kind == TP_RETIREMENT_GROUP_OBJECT;
        TpRetirementMemberSample members[TP_SAMPLE_TEST_OBJECT_MEMBERS + 1];
        unsigned count = object ? group->count : 0;
        for (unsigned i = 0; i < count; ++i)
            members[i] = (TpRetirementMemberSample){elapsed / (count + 1) - i, 65536 * (i + 1) + invocation.variant,
                test->rows[test->members[group->first + i]].id};
        if (object && mode == TEST_SAMPLE_MEMBER_COUNT) --count;
        if (object && mode == TEST_SAMPLE_MEMBER_OVERRUN) members[0].interval_ns = elapsed + 1;
        if (object && mode == TEST_SAMPLE_MEMBER_MEMORY) members[0].peak_memory_bytes = 0;
        if (object && mode == TEST_SAMPLE_MEMBER_ROW) members[0].row = members[count - 1].row + 1;
        int metrics = object != (mode == TEST_SAMPLE_WRONG_METRICS && !invocation.kind);
        TpRetirementMetricsArtifact artifact = {"retirement-metrics-aa-0000.txt", 0, 1000, {0}};
        memcpy(artifact.sha256, hash, 65);
        TpRetirementOutput output = {hash, hash, hash, metrics ? &artifact : NULL, 0};
        ok = bypass ? tp_retirement_transcript_append(&test->transcript, &observed, &process, &output) :
            tp_retirement_samples_append(&test->samples, &observed, &process, &output,
                object ? members : NULL, count);
    }
    return ok;
}

static int test_sample_collect(TpSampleTest* test)
{
    int ok = 1;
    while (ok && !tp_retirement_execution_complete(&test->execution))
    {
        if (test->transcript.records == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS)
        {
            TpRetirementShard shard;
            ok = tp_retirement_transcript_end_shard(&test->transcript, &shard);
            if (fclose(test->stream) != 0) ok = 0;
            test->stream = tmpfile();
            ok = ok && test->stream && tp_retirement_transcript_begin_shard(&test->transcript, test->stream);
        }
        if (ok) ok = test_sample_observe(test, 0, TEST_SAMPLE_VALID);
    }
    TpRetirementShard shard;
    ok = ok && tp_retirement_transcript_end_shard(&test->transcript, &shard) &&
        tp_retirement_transcript_finish(&test->transcript, test->transcript.last_end + 1);
    return ok;
}

/* Export both populations of a small fixture (one shard each). */
static int test_sample_export(TpSampleTest* test, TpRetirementShard shards[2])
{
    int ok = 1;
    for (unsigned population = 0; ok && population < 2; ++population)
    {
        FILE* output = tmpfile();
        ok = output && tp_retirement_samples_population(&test->samples) == population &&
            tp_retirement_samples_write_shard(&test->samples, output, &shards[population]);
        if (output && fclose(output) != 0) ok = 0;
    }
    return ok;
}

static int test_sample_replace(TpSampleTest* test, uint64_t ordinal, unsigned slot, uint64_t value)
{
    unsigned char bytes[8];
    tp_retirement_sample_pack(bytes, value);
    int ok = slot < TP_RETIREMENT_SAMPLE_VALUES && ordinal < test->samples.expected &&
        tp_retirement_sample_seek(test->spool, ordinal * TP_RETIREMENT_SAMPLE_RECORD_BYTES + slot * 8, SEEK_SET) &&
        fwrite(bytes, 1, sizeof(bytes), test->spool) == sizeof(bytes) && fflush(test->spool) == 0;
    return ok;
}

/* Manifest arithmetic only: synthetic descriptors, not a collected experiment.
 * The union of both populations fills the immutable three-partition bound:
 * two full row partitions and one batch partition. Real collection crosses a
 * full shard boundary in test_retirement_samples. */
static void test_sample_manifest_partitions(char const* root)
{
    TpRetirementExecution execution = {.groups = 1, .expected = 1, .sequence = 1};
    TpRetirementTranscript transcript = {.execution = &execution, .finished = 1};
    TpRetirementSamples samples = {.transcript = &transcript, .finished = 1,
        .expected = TP_RETIREMENT_SAMPLE_TOTAL_RECORDS, .row_records = 2 * TP_RETIREMENT_SAMPLE_PARTITION_RECORDS};
    uint64_t records[2] = {samples.row_records, samples.expected - samples.row_records};
    TpRetirementShard* shards[2] = {NULL, NULL};
    for (unsigned population = 0; population < 2; ++population)
    {
        samples.shards[population] = (unsigned)((records[population] + TP_RETIREMENT_SAMPLE_SHARD_RECORDS - 1) /
                                                TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
        shards[population] = (TpRetirementShard*)calloc(samples.shards[population], sizeof(TpRetirementShard));
        CHECK(shards[population] != NULL);
        if (!shards[population]) continue;
        sha256_init(&samples.descriptors[population]);
        uint64_t remaining = records[population];
        for (unsigned i = 0; i < samples.shards[population]; ++i)
        {
            shards[population][i].records = remaining < TP_RETIREMENT_SAMPLE_SHARD_RECORDS ?
                remaining : TP_RETIREMENT_SAMPLE_SHARD_RECORDS;
            shards[population][i].bytes = shards[population][i].records * 500;
            memset(shards[population][i].sha256, 'a', 64);
            tp_retirement_samples_descriptor_hash(&samples.descriptors[population], &shards[population][i]);
            remaining -= shards[population][i].records;
        }
        sha256_finish_hex(&samples.descriptors[population], samples.descriptors_sha256[population]);
        CHECK(!remaining);
    }
    CHECK(samples.shards[0] == 256 && samples.shards[1] == 46 &&
          tp_retirement_samples_union(77184, 1, 254, NULL) == 0 &&
          tp_retirement_samples_union(65000, 1000, 254, NULL) == UINT64_C(33528000));
    unsigned const population_of[] = {0, 0, 1}, partition_of[] = {0, 1, 0};
    for (unsigned file = 0; shards[0] && shards[1] && file < 3; ++file)
    {
        char name[64], path[TP_PATH_CAP];
        unsigned population = population_of[file];
        int length = snprintf(name, sizeof(name), "retirement-partition-%u.manifest.json", file);
        CHECK(length > 0 && (size_t)length < sizeof(name) && tp_path(path, root, name));
        FILE* output = fopen(path, "wb");
        TpRetirementShard manifest;
        CHECK(output && tp_retirement_samples_manifest(&samples, population, shards[population],
            samples.shards[population], partition_of[file], output, &manifest));
        if (output) CHECK(fclose(output) == 0);
        char digest[65];
        uint64_t bytes, lines;
        CHECK(tp_hash_file(path, digest, &bytes, &lines) && lines == 1 && bytes == manifest.bytes &&
              !strcmp(digest, manifest.sha256));
    }
    FILE* invalid = tmpfile();
    TpRetirementShard rejected;
    CHECK(invalid && shards[1] && !tp_retirement_samples_manifest(&samples, 1, shards[1], samples.shards[1], 1,
                                                                   invalid, &rejected));
    CHECK(samples.failed && !rejected.records && !rejected.bytes && !rejected.sha256[0]);
    if (invalid) CHECK(fclose(invalid) == 0);
    free(shards[0]);
    free(shards[1]);
}

/* The sample producer supplies only schedule-valid coordinates: row and group
 * IDs are below the 100000-cell cap, rounds are 0..1, and pairs are 0..253.
 * Row records carry wall time, peak memory and optional runtime; batch records
 * the batch process's wall time and peak RSS. Their accepted numeric maxima are
 * 15-character day-bounded seconds and 16-character exact integers. Fixed JSON
 * keys and decimal-only record IDs need no variable string escaping. The
 * maximum-width row record is 330 bytes and the batch record 266, with LF. */
static void test_retirement_sample_max_record(char const* root)
{
    uint64_t elapsed = UINT64_C(86399999999999);
    uint64_t values[TP_RETIREMENT_SAMPLE_VALUES] = {elapsed, elapsed, UINT64_C(9007199254740991),
        UINT64_C(9007199254740991), elapsed, elapsed, 3};
    unsigned row = TP_RETIREMENT_MAX_CELLS - 1;
    unsigned round = TP_RETIREMENT_ROUNDS - 1;
    unsigned pair = TP_RETIREMENT_EXECUTION_MAX_PAIRS - 1;
    unsigned metrics = TP_RETIREMENT_SAMPLE_RUNTIME;
    char line[TP_RETIREMENT_SAMPLE_LINE_CAP];
    size_t count = tp_retirement_sample_record(line, sizeof(line), row, round, pair, metrics, values);
    CHECK(count == TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX && line[count - 1] == '\n');
    CHECK(strstr(line, "generated_code_bytes") == NULL && strstr(line, "compiler_peak_rss") == NULL);
    CHECK(tp_retirement_sample_record(line, count, row, round, pair, metrics, values) == 0 && !line[0]);
    count = tp_retirement_sample_record(line, TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX + 1, row, round, pair,
        metrics, values);
    CHECK(count == TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX && line[count - 1] == '\n');
    values[4] = UINT64_C(86400000000001);
    CHECK(tp_retirement_sample_record(line, sizeof(line), row, round, pair, metrics, values) == 0 && !line[0]);
    values[4] = elapsed;
    values[0] = UINT64_C(86400000000001);
    CHECK(tp_retirement_sample_record(line, sizeof(line), row, round, pair, metrics, values) == 0 && !line[0]);
    values[0] = elapsed;
    values[2] = UINT64_C(9007199254740992);
    CHECK(tp_retirement_sample_record(line, sizeof(line), row, round, pair, metrics, values) == 0 && !line[0]);
    values[2] = UINT64_C(9007199254740991);
    CHECK(tp_retirement_sample_record(line, sizeof(line), TP_RETIREMENT_MAX_CELLS, round, pair, metrics,
                                      values) == 0 && !line[0]);
    /* A runtime observation on a row without runtime is invalid. */
    CHECK(tp_retirement_sample_record(line, sizeof(line), row, round, pair, 0, values) == 0 && !line[0]);
    count = tp_retirement_sample_record(line, sizeof(line), row, round, pair, metrics, values);
    CHECK(count == TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX);
    char path[TP_PATH_CAP];
    CHECK(tp_path(path, root, "retirement-samples-max.jsonl"));
    FILE* file = fopen(path, "wb");
    CHECK(file && fwrite(line, 1, count, file) == count);
    if (file) CHECK(fclose(file) == 0);

    uint64_t batch[TP_RETIREMENT_SAMPLE_VALUES] = {elapsed, elapsed, UINT64_C(9007199254740991),
        UINT64_C(9007199254740991), 0, 0, 1};
    count = tp_retirement_batch_record(line, sizeof(line), row, round, pair, batch);
    CHECK(count == TP_RETIREMENT_BATCH_RECORD_BYTES_MAX && line[count - 1] == '\n');
    CHECK(tp_retirement_batch_record(line, count, row, round, pair, batch) == 0 && !line[0]);
    batch[6] = 3;
    CHECK(tp_retirement_batch_record(line, sizeof(line), row, round, pair, batch) == 0 && !line[0]);
    batch[6] = 1;
    batch[4] = 1;
    CHECK(tp_retirement_batch_record(line, sizeof(line), row, round, pair, batch) == 0 && !line[0]);
    batch[4] = 0;
    batch[3] = 0;
    CHECK(tp_retirement_batch_record(line, sizeof(line), row, round, pair, batch) == 0 && !line[0]);
    batch[3] = UINT64_C(9007199254740991);
    count = tp_retirement_batch_record(line, sizeof(line), row, round, pair, batch);
    CHECK(count == TP_RETIREMENT_BATCH_RECORD_BYTES_MAX && tp_path(path, root, "retirement-batches-max.jsonl"));
    file = fopen(path, "wb");
    CHECK(file && fwrite(line, 1, count, file) == count);
    if (file) CHECK(fclose(file) == 0);

    /* Code records hold the once-parsed frozen facts per row. */
    TpRetirementCodeSide sides[2];
    memset(sides, 0, sizeof(sides));
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        memset(sides[variant].artifact_sha256, 'e', 64);
        memset(sides[variant].code_sha256, 'e', 64);
        memset(sides[variant].reproduction_sha256, 'e', 64);
        sides[variant].code_bytes = INT64_MAX;
    }
    count = tp_retirement_code_record(line, sizeof(line), row, sides);
    CHECK(count == TP_RETIREMENT_CODE_RECORD_BYTES_MAX && line[count - 1] == '\n');
    sides[1].reproduction_sha256[0] = 'f'; /* Nondeterministic reproduction. */
    CHECK(tp_retirement_code_record(line, sizeof(line), row, sides) == 0 && !line[0]);
    sides[1].reproduction_sha256[0] = 'e';
    sides[0].code_bytes = 0; /* A zero section binds the empty payload. */
    CHECK(tp_retirement_code_record(line, sizeof(line), row, sides) == 0 && !line[0]);
    memcpy(sides[0].code_sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 64);
    CHECK(tp_retirement_code_record(line, sizeof(line), row, sides) > 0);
    sides[0].code_bytes = (uint64_t)INT64_MAX + 1;
    CHECK(tp_retirement_code_record(line, sizeof(line), row, sides) == 0 && !line[0]);
    sides[0].code_bytes = 0;
    FILE* code = tmpfile();
    TpRetirementCodeRecords records;
    TpRetirementShard descriptor;
    CHECK(code && tp_retirement_code_records_init(&records, code));
    CHECK(tp_retirement_code_records_append(&records, 3, sides) &&
          !tp_retirement_code_records_append(&records, 3, sides) && records.failed);
    CHECK(!tp_retirement_code_records_finish(&records, &descriptor) && !descriptor.records);
    if (code) CHECK(fclose(code) == 0);
    code = tmpfile();
    CHECK(code && tp_retirement_code_records_init(&records, code) &&
          tp_retirement_code_records_append(&records, 3, sides) &&
          tp_retirement_code_records_append(&records, 9, sides) &&
          tp_retirement_code_records_finish(&records, &descriptor) && descriptor.records == 2 &&
          descriptor.bytes == records.bytes && tp_retirement_digest(descriptor.sha256));
    if (code) CHECK(fclose(code) == 0);
}

static void test_retirement_samples(char const* root)
{
    test_sample_manifest_partitions(root);
    test_retirement_sample_max_record(root);
    CHECK(tp_retirement_samples_count(100000, 60) == UINT64_C(12000000));
    CHECK(tp_retirement_samples_count(77184, 254) == UINT64_C(39209472));
    CHECK(tp_retirement_samples_count(77791, 254) == UINT64_C(39517828));
    CHECK(!tp_retirement_samples_count(77792, 254));
    CHECK(!tp_retirement_samples_count(100000, 254));
    CHECK(!tp_retirement_samples_count(1, 256));
    CHECK(!tp_retirement_samples_count(1, 59));
    CHECK(!tp_retirement_samples_count(0, 60));
    CHECK(!tp_retirement_samples_count(UINT32_MAX, UINT32_MAX));
    /* The union ceiling and partition bound cover both populations. */
    uint64_t row_records = 0;
    CHECK(tp_retirement_samples_union(77790, 1, 254, &row_records) == 0 && !row_records);
    CHECK(tp_retirement_samples_union(66000, 12000, 254, &row_records) == 0);
    CHECK(tp_retirement_samples_union(66000, 11000, 254, &row_records) == UINT64_C(39116000));
    CHECK(tp_retirement_samples_union(66000, 10000, 254, &row_records) == UINT64_C(38608000) &&
          row_records == UINT64_C(33528000));
    CHECK(tp_retirement_samples_union(3, 0, 60, &row_records) == 360 && row_records == 360);
    TpSampleTest* test = (TpSampleTest*)malloc(sizeof(*test));
    CHECK(test != NULL);
    if (test)
    {
        /* Fan-out: an object batch gives each member its per-input interval
         * and arena bytes and its group the process wall time and RSS. */
        CHECK(test_sample_open(test, 3) && test->group_count == 3 && test->kinds[1] == TP_RETIREMENT_GROUP_OBJECT);
        CHECK(test_sample_collect(test));
        CHECK(tp_retirement_samples_begin_export(&test->samples));
        TpRetirementShard shards[2];
        FILE* rows_file = tmpfile();
        FILE* batch_file = tmpfile();
        CHECK(rows_file && batch_file && tp_retirement_samples_write_shard(&test->samples, rows_file, &shards[0]) &&
              tp_retirement_samples_write_shard(&test->samples, batch_file, &shards[1]) &&
              shards[0].records == 360 && shards[1].records == 120 && tp_retirement_samples_finish(&test->samples));
        char sample_line[TP_RETIREMENT_SAMPLE_LINE_CAP];
        uint64_t values[TP_RETIREMENT_SAMPLE_VALUES];
        CHECK(tp_retirement_sample_read(&test->samples, 120, values) && values[2] == 65536 && values[3] == 65537 &&
              !values[4] && !values[5]);
        CHECK(tp_retirement_sample_read(&test->samples, 360, values) && values[2] == 4097 &&
              values[3] == 4098 && !values[4] && !values[5] && values[0] > 0);
        CHECK(rows_file && fseek(rows_file, 0, SEEK_SET) == 0);
        for (unsigned line = 0; rows_file && line < 121; ++line)
            CHECK(fgets(sample_line, sizeof(sample_line), rows_file) != NULL);
        CHECK(strstr(sample_line, "\"row\":1}") && strstr(sample_line, "\"compiler_peak_memory\":{\"baseline\":65536,") &&
              !strstr(sample_line, "generated_runtime") && !strstr(sample_line, "generated_code_bytes"));
        CHECK(batch_file && fseek(batch_file, 0, SEEK_SET) == 0 &&
              fgets(sample_line, sizeof(sample_line), batch_file) != NULL &&
              !strncmp(sample_line, "{\"group\":1,\"measurements\":{\"compiler_batch_peak_rss\":{\"baseline\":4097,", 70));
        if (rows_file) CHECK(fclose(rows_file) == 0);
        if (batch_file) CHECK(fclose(batch_file) == 0);
        test_sample_close(test);
        for (unsigned failure = 0; failure < 24; ++failure)
        {
            CHECK(test_sample_open(test, 3));
            TpRetirementShard shard = {0}, manifest = {0};
            FILE* output = tmpfile();
            CHECK(output != NULL);
            if (failure == 0) CHECK(!tp_retirement_samples_begin_export(&test->samples));
            else if (failure == 1)
            {
                CHECK(test_sample_observe(test, 1, TEST_SAMPLE_VALID));
                CHECK(!test_sample_observe(test, 0, TEST_SAMPLE_VALID));
            }
            else if (failure == 2) CHECK(!test_sample_observe(test, 0, TEST_SAMPLE_WRONG_METRICS));
            else if (failure == 3)
            {
                CHECK(test_sample_observe(test, 0, TEST_SAMPLE_VALID));
                CHECK(!tp_retirement_samples_finish(&test->samples));
            }
            else if (failure >= 19)
            {
                /* The first object batch is group 1's first warmup. */
                while (test_sample_observe(test, 0, TEST_SAMPLE_VALID) && test->execution.sequence < 4) {}
                unsigned mode = failure == 19 ? TEST_SAMPLE_MEMBER_COUNT : failure == 20 ?
                    TEST_SAMPLE_MEMBER_OVERRUN : failure == 21 ? TEST_SAMPLE_MEMBER_MEMORY : failure == 23 ?
                    TEST_SAMPLE_MEMBER_ROW : TEST_SAMPLE_WRONG_METRICS;
                CHECK(test->execution.sequence == 4 && !test_sample_observe(test, 0, mode));
                CHECK(test->execution.sequence == 4);
            }
            else
            {
                CHECK(test_sample_collect(test));
                if (failure == 4) CHECK(test_sample_replace(test, 0, 0, 1234));
                if (failure == 5) CHECK(test_sample_replace(test, 0, 2, 0));
                if (failure == 6) CHECK(test_sample_replace(test, 120, 4, 32));
                if (failure == 7)
                {
                    CHECK(tp_retirement_sample_seek(test->spool, 0, SEEK_END));
                    CHECK(fputc(1, test->spool) != EOF && fflush(test->spool) == 0);
                    CHECK(!tp_retirement_samples_begin_export(&test->samples));
                }
                else
                {
                    CHECK(tp_retirement_samples_begin_export(&test->samples));
                    if (failure == 8) CHECK(!tp_retirement_samples_begin_export(&test->samples));
                    else if (failure == 9)
                    {
                        CHECK(fputs("sentinel", output) >= 0);
                        CHECK(!tp_retirement_samples_write_shard(&test->samples, output, &shard));
                        CHECK(tp_retirement_sample_size(output, 8));
                    }
                    else if (failure < 7) CHECK(!tp_retirement_samples_write_shard(&test->samples, output, &shard));
                    else
                    {
                        TpRetirementShard exported[2];
                        CHECK(test_sample_export(test, exported));
                        shard = exported[0];
                        CHECK(tp_retirement_samples_finish(&test->samples));
                        if (failure == 10) CHECK(!tp_retirement_samples_finish(&test->samples));
                        else if (failure == 11) CHECK(!tp_retirement_samples_append(&test->samples, NULL, NULL, NULL,
                                                                                     NULL, 0));
                        else
                        {
                            FILE* manifest_file = tmpfile();
                            CHECK(manifest_file != NULL);
                            if (failure == 12) shard.sha256[0] = shard.sha256[0] == 'a' ? 'b' : 'a';
                            if (failure == 13) ++shard.records;
                            if (failure == 16) test->transcript.failed = 1;
                            if (failure == 17) test->execution.failed = 1;
                            if (failure == 18) CHECK(fputs("old manifest", manifest_file) >= 0);
                            CHECK(!tp_retirement_samples_manifest(&test->samples, 0, &shard, failure == 14 ? 0 : 1,
                                failure == 15 ? 1 : 0, manifest_file, &manifest));
                            CHECK(!manifest.bytes && !manifest.records && !manifest.sha256[0]);
                            if (manifest_file) CHECK(fclose(manifest_file) == 0);
                        }
                    }
                }
            }
            /* No partial/invalid experiment can be promoted by finish. */
            CHECK(!tp_retirement_samples_finish(&test->samples));
            CHECK(test->samples.failed && test->transcript.failed && !test->transcript.finished &&
                !test->samples.raw_sha256[0] && !test->samples.descriptors_sha256[0][0]);
            if (output) CHECK(fclose(output) == 0);
            test_sample_close(test);
        }
        /* The frozen layout must be a real partition with runtime singletons. */
        for (unsigned failure = 0; failure < 11; ++failure)
        {
            CHECK(test_sample_open(test, 6));
            TpRetirementSamples bad;
            FILE* empty = tmpfile();
            CHECK(empty != NULL);
            if (failure == 0) test->row_metrics[0] = 8;
            if (failure == 1) test->row_metrics[1] |= TP_RETIREMENT_SAMPLE_RUNTIME;
            if (failure == 2) CHECK(fputs("old samples", empty) >= 0 && fflush(empty) == 0);
            if (failure == 4) test->kinds[0] = TP_RETIREMENT_GROUP_OBJECT, test->kinds[1] = TP_RETIREMENT_GROUP_SINGLETON;
            if (failure == 5) test->layout_members[1] = 0;
            if (failure == 6) test->offsets[test->group_count] -= 1;
            if (failure == 7) test->row_ids[1] = test->row_ids[0];
            if (failure == 8) test->row_ids[5] = test->population;
            if (failure == 9)
            {
                /* Groups ordered by smallest member: swap groups 0 and 1. */
                unsigned first = test->layout_members[0];
                test->layout_members[0] = test->layout_members[1];
                test->layout_members[1] = first;
                test->kinds[0] = TP_RETIREMENT_GROUP_OBJECT;
                test->kinds[1] = TP_RETIREMENT_GROUP_SINGLETON;
            }
            if (failure == 10) test->kinds[2] = 2;
            CHECK(!tp_retirement_samples_init(&bad, &test->transcript, failure == 3 ? test->stream : empty,
                &test->layout, test->rows, test->groups, test->members));
            CHECK(bad.failed && test->transcript.failed);
            if (empty) CHECK(fclose(empty) == 0);
            test_sample_close(test);
        }
        /* A real shard boundary splits row 1092: a subsequent mutation of that
         * row must be caught, rather than trusting a once-per-row precheck. */
        char first_digest[65] = {0}, second_digest[65] = {0}, batch_digest[65] = {0};
        char boundary[TP_PATH_CAP], fixture_path[TP_PATH_CAP];
        CHECK(tp_path(boundary, root, "retirement-samples-boundary") && tp_mkdirs(boundary));
        for (unsigned pass = 0; pass < 3; ++pass)
        {
            CHECK(test_sample_open(test, TP_SAMPLE_TEST_ROWS));
            test->row_metrics[0] = 0; /* Init copied the layout, not borrowed it. */
            test->kinds[1] = TP_RETIREMENT_GROUP_SINGLETON;
            CHECK(test_sample_collect(test));
            CHECK(tp_retirement_samples_begin_export(&test->samples));
            TpRetirementShard shards[3], manifest;
            CHECK(tp_path(fixture_path, boundary, "retirement-samples-0000.jsonl"));
            FILE* output = !pass ? fopen(fixture_path, "wb") : tmpfile();
            CHECK(output && tp_retirement_samples_write_shard(&test->samples, output, &shards[0]));
            CHECK(shards[0].records == TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
            if (output) CHECK(fclose(output) == 0);
            if (pass == 2) CHECK(test_sample_replace(test, TP_RETIREMENT_SAMPLE_SHARD_RECORDS, 0, 1234));
            CHECK(tp_path(fixture_path, boundary, "retirement-samples-0001.jsonl"));
            output = !pass ? fopen(fixture_path, "wb") : tmpfile();
            CHECK(output != NULL);
            if (pass == 2)
            {
                CHECK(!tp_retirement_samples_write_shard(&test->samples, output, &shards[1]));
                CHECK(!shards[1].records && !shards[1].bytes && !shards[1].sha256[0]);
                CHECK(!tp_retirement_samples_finish(&test->samples));
            }
            else
            {
                CHECK(tp_retirement_samples_write_shard(&test->samples, output, &shards[1]));
                CHECK(shards[1].records == TP_SAMPLE_TEST_ROWS * 120 - TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
                CHECK(tp_path(fixture_path, boundary, "retirement-batches-0000.jsonl"));
                FILE* batches = !pass ? fopen(fixture_path, "wb") : tmpfile();
                CHECK(batches && tp_retirement_samples_population(&test->samples) == TP_RETIREMENT_POPULATION_BATCHES &&
                      tp_retirement_samples_write_shard(&test->samples, batches, &shards[2]) &&
                      shards[2].records == (uint64_t)test->samples.object_count * 120 && test->samples.object_count == 91);
                if (batches) CHECK(fclose(batches) == 0);
                CHECK(tp_retirement_samples_finish(&test->samples));
                if (!pass)
                {
                    strcpy(first_digest, test->samples.raw_sha256);
                    strcpy(second_digest, test->samples.descriptors_sha256[0]);
                    strcpy(batch_digest, test->samples.descriptors_sha256[1]);
                }
                else CHECK(!strcmp(first_digest, test->samples.raw_sha256) &&
                           !strcmp(second_digest, test->samples.descriptors_sha256[0]) &&
                           !strcmp(batch_digest, test->samples.descriptors_sha256[1]));
                CHECK(tp_path(fixture_path, boundary, "retirement-samples.manifest.json"));
                FILE* manifest_file = !pass ? fopen(fixture_path, "wb") : tmpfile();
                CHECK(manifest_file && tp_retirement_samples_manifest(&test->samples, 0, shards, 2, 0, manifest_file,
                                                                      &manifest));
                CHECK(manifest.records == 1 && manifest.bytes && manifest.sha256[0]);
                if (manifest_file) CHECK(fclose(manifest_file) == 0);
                CHECK(tp_path(fixture_path, boundary, "retirement-batches.manifest.json"));
                manifest_file = !pass ? fopen(fixture_path, "wb") : tmpfile();
                CHECK(manifest_file && tp_retirement_samples_manifest(&test->samples, 1, shards + 2, 1, 0,
                                                                      manifest_file, &manifest));
                CHECK(manifest.records == 1 && manifest.bytes && manifest.sha256[0]);
                if (manifest_file) CHECK(fclose(manifest_file) == 0);
            }
            if (output) CHECK(fclose(output) == 0);
            test_sample_close(test);
        }
#ifndef _WIN32
        for (unsigned phase = 0; phase < 3; ++phase)
        {
            CHECK(test_sample_open(test, 3) && test_sample_collect(test));
            if (phase) CHECK(tp_retirement_samples_begin_export(&test->samples));
            CHECK(fflush(test->spool) == 0);
            CHECK(ftruncate(fileno(test->spool), (off_t)(test->samples.spool_bytes - 1)) == 0);
            TpRetirementShard truncated_shard;
            FILE* output = tmpfile();
            CHECK(output != NULL);
            if (!phase) CHECK(!tp_retirement_samples_begin_export(&test->samples));
            else if (phase == 1)
            {
                /* The truncated byte belongs to the last batch record. */
                CHECK(tp_retirement_samples_write_shard(&test->samples, output, &truncated_shard));
                CHECK(fclose(output) == 0);
                output = tmpfile();
                CHECK(output && !tp_retirement_samples_write_shard(&test->samples, output, &truncated_shard));
            }
            else CHECK(!tp_retirement_samples_finish(&test->samples));
            CHECK(test->samples.failed && !tp_retirement_samples_finish(&test->samples));
            if (output) CHECK(fclose(output) == 0);
            test_sample_close(test);
        }
        CHECK(test_sample_open(test, 3) && test_sample_collect(test));
        CHECK(tp_retirement_samples_begin_export(&test->samples));
        char path[TP_PATH_CAP];
        CHECK(test_text(root, "retirement-sample-readonly", "") && tp_path(path, root, "retirement-sample-readonly"));
        FILE* readonly = fopen(path, "rb");
        TpRetirementShard shard;
        CHECK(readonly && !tp_retirement_samples_write_shard(&test->samples, readonly, &shard));
        CHECK(!shard.bytes && !shard.records && !tp_retirement_samples_finish(&test->samples));
        if (readonly) CHECK(fclose(readonly) == 0);
        test_sample_close(test);
#ifdef __linux__
        for (unsigned surface = 0; surface < 2; ++surface)
        {
            CHECK(test_sample_open(test, 3) && test_sample_collect(test));
            CHECK(tp_retirement_samples_begin_export(&test->samples));
            TpRetirementShard exported[2];
            if (surface)
            {
                CHECK(test_sample_export(test, exported));
                CHECK(tp_retirement_samples_finish(&test->samples));
            }
            FILE* full = fopen("/dev/full", "wb");
            char buffer[256 * 1024];
            CHECK(full && setvbuf(full, buffer, _IOFBF, sizeof(buffer)) == 0);
            if (full)
            {
                TpRetirementShard rejected;
                CHECK(surface ? !tp_retirement_samples_manifest(&test->samples, 0, &exported[0], 1, 0, full, &rejected) :
                    !tp_retirement_samples_write_shard(&test->samples, full, &rejected));
                CHECK(!rejected.bytes && !rejected.records && !rejected.sha256[0]);
                CHECK(test->samples.failed && ferror(full));
                (void)fclose(full); /* The deliberately failed flush is already asserted. */
            }
            test_sample_close(test);
        }
#endif
#else
        (void)root;
#endif
        free(test);
    }
}
#endif
