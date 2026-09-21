/* Failure-first native paired-sample tests. Included only by tests.c. All
 * observations are synthetic; no service receipt or performance verdict. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_TEST_H
#define TP_SAMPLE_TEST_ROWS 274u

typedef struct TpSampleTest
{
    TpRetirementExecution execution;
    TpRetirementTranscript transcript;
    TpRetirementSamples samples;
    TpRetirementSampleRow rows[TP_SAMPLE_TEST_ROWS];
    unsigned workspace[TP_SAMPLE_TEST_ROWS * 4], metrics[TP_SAMPLE_TEST_ROWS], runtime[TP_SAMPLE_TEST_ROWS];
    FILE* stream;
    FILE* spool;
} TpSampleTest;

static int test_sample_open(TpSampleTest* test, unsigned rows)
{
    memset(test, 0, sizeof(*test));
    test->stream = tmpfile();
    test->spool = tmpfile();
    unsigned runtime_count = 0;
    int ok = test->stream && test->spool && rows && rows <= TP_SAMPLE_TEST_ROWS;
    for (unsigned row = 0; ok && row < rows; ++row)
    {
        test->metrics[row] = (row % 3 != 2 ? TP_RETIREMENT_SAMPLE_CODE : 0) |
                            (row % 3 != 1 ? TP_RETIREMENT_SAMPLE_RUNTIME : 0);
        if (test->metrics[row] & TP_RETIREMENT_SAMPLE_RUNTIME) test->runtime[runtime_count++] = row;
    }
    ok = ok && tp_retirement_execution_init(&test->execution, 1, rows, test->runtime,
        runtime_count, 60, test->workspace, (size_t)rows * 4) &&
        tp_retirement_transcript_init(&test->transcript, &test->execution, "job-1", 2, "boot-123", 2, 1000) &&
        tp_retirement_transcript_begin_shard(&test->transcript, test->stream) &&
        tp_retirement_samples_init(&test->samples, &test->transcript, test->spool, test->rows, test->metrics, rows);
    return ok;
}

static void test_sample_close(TpSampleTest* test)
{
    if (test->stream) CHECK(fclose(test->stream) == 0);
    if (test->spool) CHECK(fclose(test->spool) == 0);
    test->stream = test->spool = NULL;
}

static int test_sample_observe(TpSampleTest* test, int bypass, int wrong_code)
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
        TpProcess process = {.wall_seconds = (double)elapsed / 1000000000.0,
            .peak_rss_bytes = (double)(4096 + invocation.row + invocation.variant)};
        int code = !invocation.kind && !!(test->rows[invocation.row].metrics & TP_RETIREMENT_SAMPLE_CODE);
        if (wrong_code) code = !code;
        TpRetirementOutput output = {hash, hash, hash, code ? hash : NULL,
            code ? 32 + invocation.row + invocation.variant : 0};
        ok = bypass ? tp_retirement_transcript_append(&test->transcript, &observed, &process, &output) :
            tp_retirement_samples_append(&test->samples, &observed, &process, &output);
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
        if (ok) ok = test_sample_observe(test, 0, 0);
    }
    TpRetirementShard shard;
    ok = ok && tp_retirement_transcript_end_shard(&test->transcript, &shard) &&
        tp_retirement_transcript_finish(&test->transcript, test->transcript.last_end + 1);
    return ok;
}

static int test_sample_replace(TpSampleTest* test, uint64_t ordinal, unsigned slot, uint64_t value)
{
    unsigned char bytes[8];
    tp_retirement_sample_pack(bytes, value);
    int ok = slot < 9 && ordinal < test->samples.expected &&
        tp_retirement_sample_seek(test->spool, ordinal * TP_RETIREMENT_SAMPLE_RECORD_BYTES + slot * 8, SEEK_SET) &&
        fwrite(bytes, 1, sizeof(bytes), test->spool) == sizeof(bytes) && fflush(test->spool) == 0;
    return ok;
}

/* Manifest arithmetic only: synthetic descriptors, not a collected experiment.
 * Real collection crosses a full shard boundary in test_retirement_samples. */
static void test_sample_manifest_partitions(char const* root)
{
    TpRetirementExecution execution = {.rows = 1, .expected = 1, .sequence = 1};
    TpRetirementTranscript transcript = {.execution = &execution, .finished = 1};
    TpRetirementSamples samples = {.transcript = &transcript, .finished = 1,
        .expected = TP_RETIREMENT_SAMPLE_TOTAL_RECORDS};
    samples.shards = (unsigned)((samples.expected + TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS - 1) /
                              TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
    TpRetirementShard* shards = (TpRetirementShard*)calloc(samples.shards, sizeof(*shards));
    CHECK(shards != NULL);
    if (shards)
    {
        sha256_init(&samples.descriptors);
        uint64_t remaining = samples.expected;
        for (unsigned i = 0; i < samples.shards; ++i)
        {
            shards[i].records = remaining < TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS ?
                remaining : TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS;
            shards[i].bytes = shards[i].records * 500;
            memset(shards[i].sha256, 'a', 64);
            tp_retirement_samples_descriptor_hash(&samples.descriptors, &shards[i]);
            remaining -= shards[i].records;
        }
        sha256_finish_hex(&samples.descriptors, samples.descriptors_sha256);
        CHECK(!remaining && samples.shards == 1206);
        for (unsigned partition = 0; partition < 3; ++partition)
        {
            char name[64], path[TP_PATH_CAP];
            int length = snprintf(name, sizeof(name), "retirement-partition-%u.manifest.json", partition);
            CHECK(length > 0 && (size_t)length < sizeof(name) && tp_path(path, root, name));
            FILE* output = fopen(path, "wb");
            TpRetirementShard manifest;
            CHECK(output && tp_retirement_samples_manifest(&samples, shards, samples.shards, partition, output, &manifest));
            if (output) CHECK(fclose(output) == 0);
            char digest[65];
            uint64_t bytes, lines;
            CHECK(tp_hash_file(path, digest, &bytes, &lines) && lines == 1 && bytes == manifest.bytes &&
                  !strcmp(digest, manifest.sha256));
        }
        FILE* invalid = tmpfile();
        TpRetirementShard rejected;
        CHECK(invalid && !tp_retirement_samples_manifest(&samples, shards, samples.shards, 3, invalid, &rejected));
        CHECK(samples.failed && !rejected.records && !rejected.bytes && !rejected.sha256[0]);
        if (invalid) CHECK(fclose(invalid) == 0);
        free(shards);
    }
}

static void test_retirement_samples(char const* root)
{
    test_sample_manifest_partitions(root);
    CHECK(tp_retirement_samples_count(100000, 60) == UINT64_C(12000000));
    CHECK(tp_retirement_samples_count(77184, 254) == UINT64_C(39209472));
    CHECK(tp_retirement_samples_count(77791, 254) == UINT64_C(39517828));
    CHECK(!tp_retirement_samples_count(77792, 254));
    CHECK(!tp_retirement_samples_count(100000, 254));
    CHECK(!tp_retirement_samples_count(1, 256));
    CHECK(!tp_retirement_samples_count(1, 59));
    CHECK(!tp_retirement_samples_count(0, 60));
    CHECK(!tp_retirement_samples_count(UINT32_MAX, UINT32_MAX));
    TpSampleTest* test = (TpSampleTest*)malloc(sizeof(*test));
    CHECK(test != NULL);
    if (test)
    {
        for (unsigned failure = 0; failure < 19; ++failure)
        {
            CHECK(test_sample_open(test, 3));
            TpRetirementShard shard = {0}, manifest = {0};
            FILE* output = tmpfile();
            CHECK(output != NULL);
            if (failure == 0) CHECK(!tp_retirement_samples_begin_export(&test->samples));
            else if (failure == 1)
            {
                CHECK(test_sample_observe(test, 1, 0));
                CHECK(!test_sample_observe(test, 0, 0));
            }
            else if (failure == 2) CHECK(!test_sample_observe(test, 0, 1));
            else if (failure == 3)
            {
                CHECK(test_sample_observe(test, 0, 0));
                CHECK(!tp_retirement_samples_finish(&test->samples));
            }
            else
            {
                CHECK(test_sample_collect(test));
                if (failure == 4) CHECK(test_sample_replace(test, 0, 0, 1234));
                if (failure == 5) CHECK(test_sample_replace(test, 0, 2, 0));
                if (failure == 6) CHECK(test_sample_replace(test, 240, 4, 32));
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
                        CHECK(tp_retirement_samples_write_shard(&test->samples, output, &shard));
                        CHECK(tp_retirement_samples_finish(&test->samples));
                        if (failure == 10) CHECK(!tp_retirement_samples_finish(&test->samples));
                        else if (failure == 11) CHECK(!tp_retirement_samples_append(&test->samples, NULL, NULL, NULL));
                        else
                        {
                            FILE* manifest_file = tmpfile();
                            CHECK(manifest_file != NULL);
                            if (failure == 12) shard.sha256[0] = shard.sha256[0] == 'a' ? 'b' : 'a';
                            if (failure == 13) ++shard.records;
                            if (failure == 16) test->transcript.failed = 1;
                            if (failure == 17) test->execution.failed = 1;
                            if (failure == 18) CHECK(fputs("old manifest", manifest_file) >= 0);
                            CHECK(!tp_retirement_samples_manifest(&test->samples, &shard, failure == 14 ? 0 : 1,
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
                !test->samples.raw_sha256[0] && !test->samples.descriptors_sha256[0]);
            if (output) CHECK(fclose(output) == 0);
            test_sample_close(test);
        }
        for (unsigned failure = 0; failure < 4; ++failure)
        {
            CHECK(test_sample_open(test, 3));
            TpRetirementSamples bad;
            FILE* empty = tmpfile();
            CHECK(empty != NULL);
            if (failure == 0) test->metrics[0] = 4;
            if (failure == 1) test->metrics[1] |= TP_RETIREMENT_SAMPLE_RUNTIME;
            if (failure == 2) CHECK(fputs("old samples", empty) >= 0 && fflush(empty) == 0);
            CHECK(!tp_retirement_samples_init(&bad, &test->transcript, failure == 3 ? test->stream : empty,
                test->rows, test->metrics, 3));
            CHECK(bad.failed && test->transcript.failed);
            if (empty) CHECK(fclose(empty) == 0);
            test_sample_close(test);
        }
        /* A real shard boundary splits row 273: a subsequent mutation of that
         * row must be caught, rather than trusting a once-per-row precheck. */
        char first_digest[65] = {0}, second_digest[65] = {0};
        char boundary[TP_PATH_CAP], fixture_path[TP_PATH_CAP];
        CHECK(tp_path(boundary, root, "retirement-samples-boundary") && tp_mkdirs(boundary));
        for (unsigned pass = 0; pass < 3; ++pass)
        {
            CHECK(test_sample_open(test, TP_SAMPLE_TEST_ROWS));
            test->metrics[0] = 0; /* Init copied applicability, not borrowed it. */
            CHECK(test_sample_collect(test));
            CHECK(tp_retirement_samples_begin_export(&test->samples));
            TpRetirementShard shards[2], manifest;
            CHECK(tp_path(fixture_path, boundary, "retirement-samples-0000.jsonl"));
            FILE* output = !pass ? fopen(fixture_path, "wb") : tmpfile();
            CHECK(output && tp_retirement_samples_write_shard(&test->samples, output, &shards[0]));
            CHECK(shards[0].records == TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
            if (output) CHECK(fclose(output) == 0);
            if (pass == 2) CHECK(test_sample_replace(test, TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS, 0, 1234));
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
                CHECK(shards[1].records == TP_SAMPLE_TEST_ROWS * 120 - TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
                CHECK(tp_retirement_samples_finish(&test->samples));
                if (!pass)
                {
                    strcpy(first_digest, test->samples.raw_sha256);
                    strcpy(second_digest, test->samples.descriptors_sha256);
                }
                else CHECK(!strcmp(first_digest, test->samples.raw_sha256) &&
                           !strcmp(second_digest, test->samples.descriptors_sha256));
                CHECK(tp_path(fixture_path, boundary, "retirement-samples.manifest.json"));
                FILE* manifest_file = !pass ? fopen(fixture_path, "wb") : tmpfile();
                CHECK(manifest_file && tp_retirement_samples_manifest(&test->samples, shards, 2, 0, manifest_file, &manifest));
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
            else if (phase == 1) CHECK(!tp_retirement_samples_write_shard(&test->samples, output, &truncated_shard));
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
            FILE* output = tmpfile();
            CHECK(output != NULL);
            if (surface)
            {
                CHECK(tp_retirement_samples_write_shard(&test->samples, output, &shard));
                CHECK(tp_retirement_samples_finish(&test->samples));
            }
            FILE* full = fopen("/dev/full", "wb");
            char buffer[256 * 1024];
            CHECK(full && setvbuf(full, buffer, _IOFBF, sizeof(buffer)) == 0);
            if (full)
            {
                TpRetirementShard rejected;
                CHECK(surface ? !tp_retirement_samples_manifest(&test->samples, &shard, 1, 0, full, &rejected) :
                    !tp_retirement_samples_write_shard(&test->samples, full, &rejected));
                CHECK(!rejected.bytes && !rejected.records && !rejected.sha256[0]);
                CHECK(test->samples.failed && ferror(full));
                (void)fclose(full); /* The deliberately failed flush is already asserted. */
            }
            if (output) CHECK(fclose(output) == 0);
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
