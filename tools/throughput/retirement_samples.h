/* Native paired-sample producer for #881. No admission or verdict lives here.
 * init/append couples the transcript cursor to a bounded, private binary spool;
 * begin_export/write_shard emits #615's existing row-round-pair JSONL after
 * complete collection. Per-row in-memory hashes detect spool mutation during
 * export without retaining the experiment in RAM. manifest writes the canonical
 * full-cap partitions. The service owns exclusive streams, durable publication,
 * immutable applicability, correctness, quiet phases and receipt authority.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_H
#define BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_H
#include "retirement_execution.h"

#define TP_RETIREMENT_SAMPLE_CODE 1u
#define TP_RETIREMENT_SAMPLE_RUNTIME 2u
#define TP_RETIREMENT_SAMPLE_RECORD_BYTES 72u
#define TP_RETIREMENT_SAMPLE_LINE_CAP 1024u
#define TP_RETIREMENT_SAMPLE_PARTITION_RECORDS UINT64_C(16777216)
#define TP_RETIREMENT_SAMPLE_TOTAL_RECORDS UINT64_C(39518208)
#define TP_RETIREMENT_SAMPLE_PARTITION_BYTES UINT64_C(17179869184)

/* Values are serialized explicitly as little-endian u64s in a temporary spool,
 * never a second published evidence schema. Zero denotes an unwritten metric.
 * slots: wall[2], rss[2], code[2], runtime[2], first-variant bits by kind.
 */
typedef struct TpRetirementSampleRow
{
    Sha256 observations[2];
    unsigned metrics;
} TpRetirementSampleRow;

typedef struct TpRetirementSamples
{
    TpRetirementTranscript* transcript;
    TpRetirementSampleRow* rows;
    FILE* spool;
    Sha256 raw, descriptors, exported_observations[2];
    uint64_t expected, spool_bytes, collected, exported;
    unsigned shards, verified_row;
    int failed, exporting, finished;
    char raw_sha256[65], descriptors_sha256[65];
} TpRetirementSamples;

static int tp_retirement_sample_seek(FILE* file, uint64_t offset, int origin)
{
    int ok = file && offset <= INT64_MAX;
#ifdef _WIN32
    if (ok) ok = _fseeki64(file, (__int64)offset, origin) == 0;
#else
    if (ok) ok = (uint64_t)(off_t)offset == offset && fseeko(file, (off_t)offset, origin) == 0;
#endif
    return ok;
}

static int tp_retirement_sample_size(FILE* file, uint64_t expected)
{
    int ok = tp_retirement_sample_seek(file, 0, SEEK_END);
#ifdef _WIN32
    __int64 size = ok ? _ftelli64(file) : -1;
#else
    off_t size = ok ? ftello(file) : (off_t)-1;
#endif
    ok = ok && size >= 0 && (uint64_t)size == expected;
    return ok;
}

static void tp_retirement_sample_pack(unsigned char* bytes, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
}

static uint64_t tp_retirement_sample_unpack(unsigned char const* bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)bytes[i] << (i * 8);
    return value;
}

static void tp_retirement_samples_poison(TpRetirementSamples* samples)
{
    if (samples)
    {
        samples->failed = 1;
        samples->finished = 0;
        samples->raw_sha256[0] = samples->descriptors_sha256[0] = 0;
        if (samples->transcript)
        {
            samples->transcript->failed = 1;
            samples->transcript->finished = 0;
            if (samples->transcript->execution) samples->transcript->execution->failed = 1;
        }
    }
}

static uint64_t tp_retirement_samples_count(unsigned rows, unsigned pairs)
{
    uint64_t count = rows && rows <= TP_RETIREMENT_MAX_CELLS &&
        pairs >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND && pairs <= TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
        !(pairs & 1) ? (uint64_t)rows * TP_RETIREMENT_ROUNDS * pairs : 0;
    if (count > TP_RETIREMENT_SAMPLE_TOTAL_RECORDS) count = 0;
    return count;
}

static int tp_retirement_samples_init(TpRetirementSamples* samples, TpRetirementTranscript* transcript,
    FILE* spool, TpRetirementSampleRow* row_workspace, unsigned const* metrics, unsigned rows)
{
    TpRetirementExecution* execution = transcript ? transcript->execution : NULL;
    uint64_t count = execution ? tp_retirement_samples_count(rows, execution->pairs) : 0;
    int ok = samples && transcript && !transcript->failed && !transcript->finished &&
        execution && !execution->failed && !execution->sequence && !execution->pending &&
        !transcript->total_records && rows == execution->rows && count && spool &&
        spool != transcript->stream && row_workspace && metrics;
    unsigned runtime = 0;
    for (unsigned row = 0; ok && row < rows; ++row)
    {
        int applicable = runtime < execution->runtime_count && execution->runtime_rows[runtime] == row;
        ok = !(metrics[row] & ~(TP_RETIREMENT_SAMPLE_CODE | TP_RETIREMENT_SAMPLE_RUNTIME)) &&
             !!(metrics[row] & TP_RETIREMENT_SAMPLE_RUNTIME) == applicable;
        if (applicable) ++runtime;
    }
    uint64_t bytes = count * TP_RETIREMENT_SAMPLE_RECORD_BYTES;
    if (ok) ok = tp_retirement_sample_size(spool, 0) &&
        tp_retirement_sample_seek(spool, bytes - 1, SEEK_SET) && fputc(0, spool) != EOF &&
        fflush(spool) == 0 && !ferror(spool) && tp_retirement_sample_size(spool, bytes);
    if (samples)
    {
        *samples = (TpRetirementSamples){.transcript = transcript, .failed = !ok};
        if (ok)
        {
            samples->spool = spool;
            samples->rows = row_workspace;
            samples->expected = count;
            samples->spool_bytes = bytes;
            samples->verified_row = rows;
            sha256_init(&samples->raw);
            sha256_init(&samples->descriptors);
            for (unsigned row = 0; row < rows; ++row)
            {
                row_workspace[row].metrics = metrics[row];
                sha256_init(&row_workspace[row].observations[0]);
                sha256_init(&row_workspace[row].observations[1]);
            }
        }
        else tp_retirement_samples_poison(samples);
    }
    return ok;
}

static int tp_retirement_sample_read(TpRetirementSamples* samples, uint64_t ordinal, uint64_t values[9])
{
    unsigned char bytes[TP_RETIREMENT_SAMPLE_RECORD_BYTES];
    int ok = samples && ordinal < samples->expected &&
        tp_retirement_sample_seek(samples->spool, ordinal * sizeof(bytes), SEEK_SET) &&
        fread(bytes, 1, sizeof(bytes), samples->spool) == sizeof(bytes) && !ferror(samples->spool);
    if (ok)
        for (unsigned i = 0; i < 9; ++i) values[i] = tp_retirement_sample_unpack(bytes + i * 8);
    return ok;
}

static void tp_retirement_sample_hash(Sha256* hash, unsigned row, unsigned kind, unsigned round,
    unsigned pair, unsigned variant, uint64_t wall, uint64_t rss, uint64_t code)
{
    unsigned char bytes[64];
    uint64_t values[] = {row, kind, round, pair, variant, wall, rss, code};
    for (unsigned i = 0; i < 8; ++i) tp_retirement_sample_pack(bytes + i * 8, values[i]);
    sha256_add(hash, bytes, sizeof(bytes));
}

/* The transcript alone must not advance when this collector is attached.
 * A spool or transcript write failure invalidates the same attempt. No API
 * imports partial samples, skips warmups, retries a cell or resumes collection.
 */
static int tp_retirement_samples_append(TpRetirementSamples* samples,
    TpProcessObservation const* observed, TpProcess const* process, TpRetirementOutput const* output)
{
    TpRetirementInvocation invocation;
    TpRetirementTranscript* transcript = samples ? samples->transcript : NULL;
    TpRetirementExecution* execution = transcript ? transcript->execution : NULL;
    int ok = samples && !samples->failed && !samples->exporting && !samples->finished &&
        transcript && execution && samples->collected == execution->sequence &&
        tp_retirement_execution_peek(execution, &invocation) == TP_RETIREMENT_NEXT_READY;
    char checked[TP_RETIREMENT_EXECUTION_LINE_CAP];
    if (ok) ok = tp_retirement_execution_record(checked, sizeof(checked), &invocation, observed,
        process, output, transcript->job, transcript->attempt, transcript->boot, transcript->cpu) > 0;
    if (ok && !invocation.kind)
        ok = !!output->code_section_bytes == !!(samples->rows[invocation.row].metrics & TP_RETIREMENT_SAMPLE_CODE);
    uint64_t wall = ok ? observed->finished_ns - observed->started_ns : 0;
    uint64_t rss = ok && !invocation.kind ? (uint64_t)process->peak_rss_bytes : 0;
    uint64_t code = ok && !invocation.kind ? output->code_section_bytes : 0;
    if (ok && invocation.phase)
    {
        uint64_t values[9];
        uint64_t ordinal = ((uint64_t)invocation.row * TP_RETIREMENT_ROUNDS +
                            (unsigned)invocation.round) * execution->pairs + (unsigned)invocation.pair;
        unsigned slot = invocation.kind ? 6 : 0;
        ok = tp_retirement_sample_read(samples, ordinal, values) && !values[slot + invocation.variant];
        if (ok)
        {
            values[slot + invocation.variant] = wall;
            if (!invocation.kind)
            {
                values[2 + invocation.variant] = rss;
                values[4 + invocation.variant] = code;
            }
            if (!invocation.position)
                values[8] |= (uint64_t)invocation.variant << invocation.kind;
            unsigned char bytes[TP_RETIREMENT_SAMPLE_RECORD_BYTES];
            for (unsigned i = 0; i < 9; ++i) tp_retirement_sample_pack(bytes + i * 8, values[i]);
            ok = tp_retirement_sample_seek(samples->spool, ordinal * sizeof(bytes), SEEK_SET) &&
                fwrite(bytes, 1, sizeof(bytes), samples->spool) == sizeof(bytes) && !ferror(samples->spool);
        }
    }
    if (ok) ok = tp_retirement_transcript_append(transcript, observed, process, output);
    if (ok)
    {
        if (invocation.phase)
            tp_retirement_sample_hash(&samples->rows[invocation.row].observations[invocation.kind], invocation.row,
                invocation.kind, (unsigned)invocation.round, (unsigned)invocation.pair,
                invocation.variant, wall, rss, code);
        ++samples->collected;
    }
    else tp_retirement_samples_poison(samples);
    return ok;
}

static int tp_retirement_samples_begin_export(TpRetirementSamples* samples)
{
    TpRetirementTranscript* transcript = samples ? samples->transcript : NULL;
    int ok = samples && !samples->failed && !samples->exporting && !samples->finished && transcript &&
        !transcript->failed && transcript->finished && tp_retirement_execution_complete(transcript->execution) &&
        samples->collected == transcript->execution->expected && fflush(samples->spool) == 0 &&
        !ferror(samples->spool) && tp_retirement_sample_size(samples->spool, samples->spool_bytes);
    if (ok) samples->exporting = 1;
    else tp_retirement_samples_poison(samples);
    return ok;
}

static int tp_retirement_sample_values(uint64_t const values[9], unsigned metrics)
{
    int ok = values[8] <= 3 && ((metrics & TP_RETIREMENT_SAMPLE_RUNTIME) || !(values[8] & 2));
    for (unsigned variant = 0; ok && variant < 2; ++variant)
    {
        ok = values[variant] && values[variant] <= UINT64_C(86400000000000) &&
            values[2 + variant] && values[2 + variant] <= UINT64_C(9007199254740991);
        ok = ok && ((metrics & TP_RETIREMENT_SAMPLE_CODE) ?
            values[4 + variant] && values[4 + variant] <= INT64_MAX : !values[4 + variant]);
        ok = ok && ((metrics & TP_RETIREMENT_SAMPLE_RUNTIME) ?
            values[6 + variant] && values[6 + variant] <= UINT64_C(86400000000000) : !values[6 + variant]);
    }
    return ok;
}

/* The approved schedule orders each kind's observations for a single row by
 * round/pair/position. Hash exactly the values being serialized, including their
 * variant order, and compare at row completion. Separate kind hashes avoid a
 * second spool pass and catch mutation even when a row crosses a shard boundary.
 * Earlier shards stay partial integrity artifacts until finish/manifest succeed.
 */
static int tp_retirement_samples_verify_row(TpRetirementSamples* samples, unsigned row)
{
    int ok = 1;
    for (unsigned kind = 0; ok && kind < 2; ++kind)
    {
        Sha256 expected = samples->rows[row].observations[kind];
        char actual_digest[65], expected_digest[65];
        sha256_finish_hex(&samples->exported_observations[kind], actual_digest);
        sha256_finish_hex(&expected, expected_digest);
        ok = !strcmp(actual_digest, expected_digest);
    }
    return ok;
}

static size_t tp_retirement_sample_record(char* bytes, size_t capacity, unsigned row,
    unsigned round, unsigned pair, unsigned metrics, uint64_t const values[9])
{
    size_t result = 0;
    char wall[2][32], runtime[2][32], code_text[128] = "", runtime_text[128] = "";
    int ok = bytes && capacity && capacity <= TP_RETIREMENT_SAMPLE_LINE_CAP &&
        !(metrics & ~(TP_RETIREMENT_SAMPLE_CODE | TP_RETIREMENT_SAMPLE_RUNTIME)) &&
        tp_retirement_sample_values(values, metrics);
    for (unsigned variant = 0; ok && variant < 2; ++variant)
    {
        ok = tp_retirement_seconds(wall[variant], values[variant]);
        if (ok && (metrics & TP_RETIREMENT_SAMPLE_RUNTIME))
            ok = tp_retirement_seconds(runtime[variant], values[6 + variant]);
    }
    if (ok && (metrics & TP_RETIREMENT_SAMPLE_CODE))
    {
        int length = snprintf(code_text, sizeof(code_text),
            ",\"generated_code_bytes\":{\"baseline\":%" PRIu64 ",\"candidate\":%" PRIu64 "}", values[4], values[5]);
        ok = length > 0 && (size_t)length < sizeof(code_text);
    }
    if (ok && (metrics & TP_RETIREMENT_SAMPLE_RUNTIME))
    {
        int length = snprintf(runtime_text, sizeof(runtime_text),
            ",\"generated_runtime\":{\"baseline\":%s,\"candidate\":%s}", runtime[0], runtime[1]);
        ok = length > 0 && (size_t)length < sizeof(runtime_text);
    }
    if (ok)
    {
        int length = snprintf(bytes, capacity,
            "{\"measurements\":{\"compiler_peak_rss\":{\"baseline\":%" PRIu64 ",\"candidate\":%" PRIu64 "},"
            "\"compiler_wall_time\":{\"baseline\":%s,\"candidate\":%s}%s%s},\"pair\":%u,"
            "\"record_id\":\"row-%u/round-%u/pair-%u\",\"round\":%u,\"row\":%u}\n",
            values[2], values[3], wall[0], wall[1], code_text, runtime_text, pair, row, round, pair, round, row);
        if (length > 0 && (size_t)length < capacity) result = (size_t)length;
    }
    if (!result && bytes && capacity) bytes[0] = 0;
    return result;
}

static void tp_retirement_samples_descriptor_hash(Sha256* hash, TpRetirementShard const* shard)
{
    unsigned char bytes[16];
    tp_retirement_sample_pack(bytes, shard->bytes);
    tp_retirement_sample_pack(bytes + 8, shard->records);
    sha256_add(hash, bytes, sizeof(bytes));
    sha256_add(hash, shard->sha256, 64);
}

static int tp_retirement_samples_write_shard(TpRetirementSamples* samples, FILE* stream, TpRetirementShard* shard)
{
    int ok = samples && !samples->failed && samples->exporting && !samples->finished && shard &&
        samples->transcript && !samples->transcript->failed && samples->transcript->finished &&
        tp_retirement_execution_complete(samples->transcript->execution) &&
        stream && stream != samples->spool && samples->exported < samples->expected &&
        tp_retirement_sample_size(stream, 0) && tp_retirement_sample_seek(stream, 0, SEEK_SET);
    TpRetirementShard next = {0};
    Sha256 hash;
    sha256_init(&hash);
    if (shard) *shard = (TpRetirementShard){0};
    while (ok && samples->exported < samples->expected && next.records < TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS)
    {
        unsigned pairs = samples->transcript->execution->pairs;
        unsigned row = (unsigned)(samples->exported / (TP_RETIREMENT_ROUNDS * pairs));
        unsigned round = (unsigned)(samples->exported / pairs % TP_RETIREMENT_ROUNDS);
        unsigned pair = (unsigned)(samples->exported % pairs);
        if (samples->verified_row != row)
        {
            sha256_init(&samples->exported_observations[0]);
            sha256_init(&samples->exported_observations[1]);
            samples->verified_row = row;
        }
        uint64_t values[9];
        char bytes[TP_RETIREMENT_SAMPLE_LINE_CAP];
        ok = tp_retirement_sample_read(samples, samples->exported, values) &&
            tp_retirement_sample_values(values, samples->rows[row].metrics);
        for (unsigned kind = 0; ok && kind < 2; ++kind)
            for (unsigned position = 0; position < 2; ++position)
            {
                if (!kind || (samples->rows[row].metrics & TP_RETIREMENT_SAMPLE_RUNTIME))
                {
                    unsigned variant = (unsigned)((values[8] >> kind) & 1) ^ position;
                    tp_retirement_sample_hash(&samples->exported_observations[kind], row, kind,
                        round, pair, variant, values[(kind ? 6 : 0) + variant],
                        kind ? 0 : values[2 + variant], kind ? 0 : values[4 + variant]);
                }
            }
        if (ok && round + 1 == TP_RETIREMENT_ROUNDS && pair + 1 == pairs)
            ok = tp_retirement_samples_verify_row(samples, row);
        size_t count = ok ? tp_retirement_sample_record(bytes, sizeof(bytes), row, round, pair,
            samples->rows[row].metrics, values) : 0;
        ok = ok && count && next.bytes <= TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES &&
            count <= TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES - next.bytes;
        if (ok) ok = fwrite(bytes, 1, count, stream) == count && !ferror(stream);
        if (ok)
        {
            sha256_add(&hash, bytes, (u64)count);
            sha256_add(&samples->raw, bytes, (u64)count);
            next.bytes += count;
            ++next.records;
            ++samples->exported;
        }
    }
    if (ok) ok = fflush(stream) == 0 && !ferror(stream);
    if (ok)
    {
        sha256_finish_hex(&hash, next.sha256);
        tp_retirement_samples_descriptor_hash(&samples->descriptors, &next);
        *shard = next;
        ++samples->shards;
    }
    else tp_retirement_samples_poison(samples);
    return ok;
}

static int tp_retirement_samples_finish(TpRetirementSamples* samples)
{
    int ok = samples && !samples->failed && samples->exporting && !samples->finished &&
        samples->shards && samples->exported == samples->expected &&
        !samples->transcript->failed && samples->transcript->finished &&
        tp_retirement_execution_complete(samples->transcript->execution) &&
        tp_retirement_sample_size(samples->spool, samples->spool_bytes);
    if (ok)
    {
        sha256_finish_hex(&samples->raw, samples->raw_sha256);
        sha256_finish_hex(&samples->descriptors, samples->descriptors_sha256);
        samples->finished = 1;
    }
    else tp_retirement_samples_poison(samples);
    return ok;
}

static int tp_retirement_samples_manifest(TpRetirementSamples* samples, TpRetirementShard const* shards,
    unsigned shard_count, unsigned partition, FILE* stream, TpRetirementShard* descriptor)
{
    unsigned per_partition = (unsigned)(TP_RETIREMENT_SAMPLE_PARTITION_RECORDS / TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS);
    unsigned partitions = samples ? (unsigned)((samples->expected + TP_RETIREMENT_SAMPLE_PARTITION_RECORDS - 1) /
                                               TP_RETIREMENT_SAMPLE_PARTITION_RECORDS) : 0;
    int ok = samples && !samples->failed && samples->finished && samples->transcript &&
        !samples->transcript->failed && samples->transcript->finished &&
        tp_retirement_execution_complete(samples->transcript->execution) &&
        shards && shard_count == samples->shards &&
        shard_count == (samples->expected + TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS - 1) /
                       TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS && partition < partitions && stream && descriptor &&
        stream != samples->spool && tp_retirement_sample_size(stream, 0) && tp_retirement_sample_seek(stream, 0, SEEK_SET);
    Sha256 descriptors_hash, hash;
    sha256_init(&descriptors_hash);
    sha256_init(&hash);
    uint64_t remaining = samples ? samples->expected : 0, partition_bytes = 0;
    for (unsigned index = 0; ok && index < shard_count; ++index)
    {
        uint64_t records = remaining < TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS ? remaining : TP_RETIREMENT_TRANSCRIPT_SHARD_RECORDS;
        ok = shards[index].records == records && shards[index].bytes &&
            shards[index].bytes <= TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES && tp_retirement_digest(shards[index].sha256);
        if (ok)
        {
            tp_retirement_samples_descriptor_hash(&descriptors_hash, &shards[index]);
            remaining -= records;
            if (index / per_partition == partition) partition_bytes += shards[index].bytes;
        }
    }
    char digest[65];
    sha256_finish_hex(&descriptors_hash, digest);
    if (ok) ok = !remaining && !strcmp(digest, samples->descriptors_sha256) &&
        partition_bytes <= TP_RETIREMENT_SAMPLE_PARTITION_BYTES;
    TpRetirementShard result = {0};
    if (descriptor) *descriptor = result;
    unsigned begin = partition * per_partition;
    unsigned end = begin + per_partition < shard_count ? begin + per_partition : shard_count;
    /* Emit a bounded fragment at a time. Paths are fixed, not request fields. */
    for (unsigned index = begin; ok && index < end + 2; ++index)
    {
        char bytes[384];
        int length;
        if (index == begin)
            length = snprintf(bytes, sizeof(bytes), "{\"identity_field\":\"record_id\",\"schema\":\"buster-streaming-evidence-shards-v1\",\"shards\":[");
        else if (index == end + 1)
            length = snprintf(bytes, sizeof(bytes), "],\"version\":1}\n");
        else
        {
            unsigned number = index - 1;
            length = snprintf(bytes, sizeof(bytes),
                "%s{\"bytes\":%" PRIu64 ",\"identity\":\"samples-%04u\",\"path\":\"retirement-samples-%04u.jsonl\",\"sha256\":\"%s\"}",
                number == begin ? "" : ",", shards[number].bytes, number, number, shards[number].sha256);
        }
        ok = length > 0 && (size_t)length < sizeof(bytes) &&
            fwrite(bytes, 1, (size_t)length, stream) == (size_t)length && !ferror(stream);
        if (ok)
        {
            sha256_add(&hash, bytes, (u64)length);
            result.bytes += (unsigned)length;
        }
    }
    if (ok) ok = fflush(stream) == 0 && !ferror(stream);
    if (ok)
    {
        sha256_finish_hex(&hash, result.sha256);
        result.records = 1;
        *descriptor = result;
    }
    else tp_retirement_samples_poison(samples);
    return ok;
}
#endif
