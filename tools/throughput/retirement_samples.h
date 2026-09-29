/* Native paired-sample producer for #881 (A1). No admission or verdict lives here.
 * init/append couples the transcript cursor to a bounded, private binary spool;
 * begin_export/write_shard emits #615's two populations after complete
 * collection: `row-round-pair` records for the native-host timed rows, then
 * `group-round-pair` records for the object batch groups. One object batch
 * fans out to each member row (its per-input interval and arena high-water
 * bytes) and to its group's batch record (the process wall time and peak RSS).
 * A singleton link/self-host group's process is that row's sample. Code bytes
 * never enter the pairs: the code record set carries them once per row.
 * Per-unit in-memory hashes detect spool mutation during export without
 * retaining the experiment in RAM. manifest writes each population's canonical
 * full-cap partitions. The service owns exclusive streams, durable publication,
 * immutable applicability, correctness, quiet phases and receipt authority.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_H
#define BUSTER_THROUGHPUT_RETIREMENT_SAMPLES_H
#include "retirement_execution.h"

#define TP_RETIREMENT_SAMPLE_RUNTIME 2u
#define TP_RETIREMENT_GROUP_SINGLETON 0u
#define TP_RETIREMENT_GROUP_OBJECT 1u
#define TP_RETIREMENT_POPULATION_ROWS 0u
#define TP_RETIREMENT_POPULATION_BATCHES 1u
#define TP_RETIREMENT_SAMPLE_VALUES 7u
#define TP_RETIREMENT_SAMPLE_RECORD_BYTES 56u
#define TP_RETIREMENT_SAMPLE_LINE_CAP 1024u
#define TP_RETIREMENT_SAMPLE_PARTITION_RECORDS UINT64_C(16777216)
/* The immutable total-record ceiling and the three-partition bound apply to
 * the union of both populations. */
#define TP_RETIREMENT_SAMPLE_TOTAL_RECORDS UINT64_C(39518208)
#define TP_RETIREMENT_SAMPLE_PARTITIONS 3u
#define TP_RETIREMENT_SAMPLE_PARTITION_BYTES UINT64_C(17179869184)
/* Largest lines tp_retirement_sample_record and tp_retirement_batch_record can
 * emit, including LF; pinned by test_retirement_sample_max_record.
 * write_shard rejects a wider line. */
#define TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX 330u
#define TP_RETIREMENT_BATCH_RECORD_BYTES_MAX 266u
/* Numeric shards are independent of transcript shards. A whole number of
 * them fills each full manifest partition. */
#define TP_RETIREMENT_SAMPLE_SHARD_RECORDS UINT64_C(131072)
/* Largest code record line, including LF (validator line cap is 8192). */
#define TP_RETIREMENT_CODE_RECORD_BYTES_MAX 649u
BUSTER_CT_CHECK(TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX < TP_RETIREMENT_SAMPLE_LINE_CAP);
BUSTER_CT_CHECK(TP_RETIREMENT_BATCH_RECORD_BYTES_MAX < TP_RETIREMENT_SAMPLE_LINE_CAP);
BUSTER_CT_CHECK(TP_RETIREMENT_SAMPLE_PARTITION_RECORDS % TP_RETIREMENT_SAMPLE_SHARD_RECORDS == 0);
BUSTER_CT_CHECK(TP_RETIREMENT_SAMPLE_SHARD_RECORDS * TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX <=
                TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES);

/* The frozen A1 layout of the timed projection. Rows are dense timed-row
 * indexes in ascending census order; each belongs to exactly one group.
 * Group g's members are group_members[group_offsets[g] .. group_offsets[g+1]),
 * ascending; groups are ordered by their smallest member. A singleton group
 * has one member; every runtime row is a singleton. The service derives this
 * partition from the frozen rows; the collector only checks its shape. */
typedef struct TpRetirementLayout
{
    unsigned rows, groups;
    unsigned const* row_ids;
    unsigned const* row_metrics;
    unsigned const* group_kinds;
    unsigned const* group_offsets;
    unsigned const* group_members;
} TpRetirementLayout;

/* Values are serialized explicitly as little-endian u64s in a temporary spool,
 * never a second published evidence schema. Zero denotes an unwritten metric.
 * Row slots: compiler wall[2], compiler memory[2], runtime[2], first-variant
 * bits by kind. Batch slots: process wall[2], process RSS[2], zero[2], bits. */
typedef struct TpRetirementSampleRow
{
    Sha256 observations[2];
    unsigned id, metrics, group;
} TpRetirementSampleRow;

typedef struct TpRetirementSampleGroup
{
    Sha256 observations;
    unsigned first, count, kind, object;
} TpRetirementSampleGroup;

typedef struct TpRetirementSamples
{
    TpRetirementTranscript* transcript;
    TpRetirementSampleRow* rows;
    TpRetirementSampleGroup* groups;
    unsigned* members;
    FILE* spool;
    Sha256 raw, descriptors[2], exported_observations[2];
    uint64_t expected, row_records, spool_bytes, collected, exported;
    unsigned row_count, group_count, object_count, shards[2], verified_unit, export_group;
    int failed, exporting, finished;
    char raw_sha256[65], descriptors_sha256[2][65];
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
        samples->raw_sha256[0] = samples->descriptors_sha256[0][0] = samples->descriptors_sha256[1][0] = 0;
        if (samples->transcript)
        {
            samples->transcript->failed = 1;
            samples->transcript->finished = 0;
            if (samples->transcript->execution) samples->transcript->execution->failed = 1;
        }
    }
}

static uint64_t tp_retirement_samples_count(unsigned units, unsigned pairs)
{
    uint64_t count = units && units <= TP_RETIREMENT_MAX_CELLS &&
        pairs >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND && pairs <= TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
        !(pairs & 1) ? (uint64_t)units * TP_RETIREMENT_ROUNDS * pairs : 0;
    if (count > TP_RETIREMENT_SAMPLE_TOTAL_RECORDS) count = 0;
    return count;
}

static uint64_t tp_retirement_samples_partitions(uint64_t records)
{
    return records / TP_RETIREMENT_SAMPLE_PARTITION_RECORDS + !!(records % TP_RETIREMENT_SAMPLE_PARTITION_RECORDS);
}

/* Both populations' record counts for a layout, or zero when either the
 * union ceiling or the three-partition bound is exceeded. */
static uint64_t tp_retirement_samples_union(unsigned rows, unsigned objects, unsigned pairs, uint64_t* row_records)
{
    uint64_t rows_count = tp_retirement_samples_count(rows, pairs);
    uint64_t batches = objects ? tp_retirement_samples_count(objects, pairs) : 0;
    uint64_t total = rows_count && (!objects || batches) &&
        batches <= TP_RETIREMENT_SAMPLE_TOTAL_RECORDS - rows_count &&
        tp_retirement_samples_partitions(rows_count) + tp_retirement_samples_partitions(batches) <=
            TP_RETIREMENT_SAMPLE_PARTITIONS ? rows_count + batches : 0;
    if (row_records) *row_records = total ? rows_count : 0;
    return total;
}

/* The layout is copied: later mutation of producer input cannot change the
 * frozen fan-out. row_workspace and member_workspace hold `rows` entries and
 * group_workspace `groups`. The execution's runtime rows must be exactly the
 * runtime-flagged rows, in order. */
static int tp_retirement_samples_init(TpRetirementSamples* samples, TpRetirementTranscript* transcript,
    FILE* spool, TpRetirementLayout const* layout, TpRetirementSampleRow* row_workspace,
    TpRetirementSampleGroup* group_workspace, unsigned* member_workspace)
{
    TpRetirementExecution* execution = transcript ? transcript->execution : NULL;
    unsigned rows = layout ? layout->rows : 0, groups = layout ? layout->groups : 0;
    int ok = samples && transcript && !transcript->failed && !transcript->finished &&
        execution && !execution->failed && !execution->sequence && !execution->pending &&
        !transcript->total_records && layout && rows && rows <= TP_RETIREMENT_MAX_CELLS &&
        groups == execution->groups && groups <= rows && layout->row_ids && layout->row_metrics &&
        layout->group_kinds && layout->group_offsets && layout->group_members && spool &&
        spool != transcript->stream && row_workspace && group_workspace && member_workspace &&
        !layout->group_offsets[0] && layout->group_offsets[groups] == rows;
    unsigned runtime = 0, objects = 0;
    for (unsigned row = 0; ok && row < rows; ++row)
    {
        unsigned metrics = layout->row_metrics[row];
        ok = layout->row_ids[row] < execution->population_rows &&
            (!row || layout->row_ids[row] > layout->row_ids[row - 1]) &&
            !(metrics & ~TP_RETIREMENT_SAMPLE_RUNTIME);
        if (ok && (metrics & TP_RETIREMENT_SAMPLE_RUNTIME))
            ok = runtime < execution->runtime_count && execution->runtime_rows[runtime++] == layout->row_ids[row];
        if (ok) row_workspace[row] = (TpRetirementSampleRow){.id = layout->row_ids[row], .metrics = metrics,
                                                           .group = TP_RETIREMENT_NONE};
    }
    ok = ok && runtime == execution->runtime_count;
    for (unsigned group = 0; ok && group < groups; ++group)
    {
        unsigned first = layout->group_offsets[group], end = layout->group_offsets[group + 1];
        unsigned kind = layout->group_kinds[group];
        ok = first < end && end <= rows && (kind == TP_RETIREMENT_GROUP_OBJECT ||
            (kind == TP_RETIREMENT_GROUP_SINGLETON && end - first == 1)) &&
            (!group || layout->group_members[first] > member_workspace[group_workspace[group - 1].first]);
        for (unsigned member = first; ok && member < end; ++member)
        {
            unsigned dense = layout->group_members[member];
            ok = dense < rows && row_workspace[dense].group == TP_RETIREMENT_NONE &&
                (member == first || dense > layout->group_members[member - 1]) &&
                (kind == TP_RETIREMENT_GROUP_SINGLETON || !(row_workspace[dense].metrics & TP_RETIREMENT_SAMPLE_RUNTIME));
            if (ok)
            {
                row_workspace[dense].group = group;
                member_workspace[member] = dense;
            }
        }
        if (ok) group_workspace[group] = (TpRetirementSampleGroup){.first = first, .count = end - first,
            .kind = kind, .object = kind == TP_RETIREMENT_GROUP_OBJECT ? objects++ : TP_RETIREMENT_NONE};
    }
    uint64_t row_records = 0;
    uint64_t count = ok ? tp_retirement_samples_union(rows, objects, execution->pairs, &row_records) : 0;
    uint64_t bytes = count * TP_RETIREMENT_SAMPLE_RECORD_BYTES;
    ok = ok && count;
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
            samples->groups = group_workspace;
            samples->members = member_workspace;
            samples->expected = count;
            samples->row_records = row_records;
            samples->spool_bytes = bytes;
            samples->row_count = rows;
            samples->group_count = groups;
            samples->object_count = objects;
            samples->verified_unit = TP_RETIREMENT_NONE;
            samples->export_group = 0;
            sha256_init(&samples->raw);
            sha256_init(&samples->descriptors[0]);
            sha256_init(&samples->descriptors[1]);
            for (unsigned row = 0; row < rows; ++row)
            {
                sha256_init(&row_workspace[row].observations[0]);
                sha256_init(&row_workspace[row].observations[1]);
            }
            for (unsigned group = 0; group < groups; ++group) sha256_init(&group_workspace[group].observations);
        }
        else tp_retirement_samples_poison(samples);
    }
    return ok;
}

static int tp_retirement_sample_read(TpRetirementSamples* samples, uint64_t ordinal,
                                     uint64_t values[TP_RETIREMENT_SAMPLE_VALUES])
{
    unsigned char bytes[TP_RETIREMENT_SAMPLE_RECORD_BYTES];
    int ok = samples && ordinal < samples->expected &&
        tp_retirement_sample_seek(samples->spool, ordinal * sizeof(bytes), SEEK_SET) &&
        fread(bytes, 1, sizeof(bytes), samples->spool) == sizeof(bytes) && !ferror(samples->spool);
    if (ok)
        for (unsigned i = 0; i < TP_RETIREMENT_SAMPLE_VALUES; ++i) values[i] = tp_retirement_sample_unpack(bytes + i * 8);
    return ok;
}

/* Fill one unwritten variant of a spool record. kind 0 is compilation (row
 * wall/memory or batch wall/RSS), kind 1 native runtime. */
static int tp_retirement_sample_update(TpRetirementSamples* samples, uint64_t ordinal, unsigned kind,
    TpRetirementInvocation const* invocation, uint64_t wall, uint64_t memory)
{
    uint64_t values[TP_RETIREMENT_SAMPLE_VALUES];
    unsigned slot = kind ? 4 : 0;
    int ok = tp_retirement_sample_read(samples, ordinal, values) && !values[slot + invocation->variant];
    if (ok)
    {
        values[slot + invocation->variant] = wall;
        if (!kind) values[2 + invocation->variant] = memory;
        if (!invocation->position) values[6] |= (uint64_t)invocation->variant << kind;
        unsigned char bytes[TP_RETIREMENT_SAMPLE_RECORD_BYTES];
        for (unsigned i = 0; i < TP_RETIREMENT_SAMPLE_VALUES; ++i) tp_retirement_sample_pack(bytes + i * 8, values[i]);
        ok = tp_retirement_sample_seek(samples->spool, ordinal * sizeof(bytes), SEEK_SET) &&
            fwrite(bytes, 1, sizeof(bytes), samples->spool) == sizeof(bytes) && !ferror(samples->spool);
    }
    return ok;
}

static void tp_retirement_sample_hash(Sha256* hash, unsigned unit, unsigned kind, unsigned round,
    unsigned pair, unsigned variant, uint64_t wall, uint64_t memory)
{
    unsigned char bytes[56];
    uint64_t values[] = {unit, kind, round, pair, variant, wall, memory};
    for (unsigned i = 0; i < 7; ++i) tp_retirement_sample_pack(bytes + i * 8, values[i]);
    sha256_add(hash, bytes, sizeof(bytes));
}

/* Census ID -> dense timed row (ascending IDs). */
static unsigned tp_retirement_samples_row(TpRetirementSamples const* samples, unsigned id)
{
    unsigned low = 0, high = samples ? samples->row_count : 0, result = TP_RETIREMENT_NONE;
    while (low < high)
    {
        unsigned middle = low + (high - low) / 2;
        if (samples->rows[middle].id == id) { result = middle; break; }
        if (samples->rows[middle].id < id) low = middle + 1;
        else high = middle;
    }
    return result;
}

static uint64_t tp_retirement_samples_ordinal(TpRetirementSamples const* samples, uint64_t base, unsigned unit,
    TpRetirementInvocation const* invocation)
{
    unsigned pairs = samples->transcript->execution->pairs;
    return base + ((uint64_t)unit * TP_RETIREMENT_ROUNDS + (unsigned)invocation->round) * pairs +
        (unsigned)invocation->pair;
}

/* The transcript alone must not advance when this collector is attached.
 * An object batch supplies each member's (interval, memory, census row) from
 * its checked per-input metrics, in member order, and each row must be the
 * layout's row for that member; singleton and runtime invocations supply
 * none. A spool or transcript write failure invalidates the same attempt. No
 * API imports partial samples, skips warmups, retries a cell or resumes. */
static int tp_retirement_samples_append(TpRetirementSamples* samples,
    TpProcessObservation const* observed, TpProcess const* process, TpRetirementOutput const* output,
    TpRetirementMemberSample const* members, unsigned member_count)
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
    uint64_t wall = ok ? observed->finished_ns - observed->started_ns : 0;
    uint64_t rss = ok && !invocation.kind ? (uint64_t)process->peak_rss_bytes : 0;
    TpRetirementSampleGroup const* group = ok && !invocation.kind && invocation.group < samples->group_count ?
        &samples->groups[invocation.group] : NULL;
    unsigned row = ok && invocation.kind ? tp_retirement_samples_row(samples, invocation.row) : TP_RETIREMENT_NONE;
    if (ok && !invocation.kind)
    {
        ok = group != NULL;
        if (ok && group->kind == TP_RETIREMENT_GROUP_OBJECT)
        {
            uint64_t total = 0;
            ok = output->metrics_sha256 && members && member_count == group->count;
            for (unsigned i = 0; ok && i < member_count; ++i)
            {
                ok = members[i].interval_ns && members[i].interval_ns <= wall - total &&
                    members[i].peak_memory_bytes && members[i].peak_memory_bytes <= UINT64_C(9007199254740991) &&
                    members[i].row == samples->rows[samples->members[group->first + i]].id;
                if (ok) total += members[i].interval_ns;
            }
        }
        else if (ok) ok = !output->metrics_sha256 && !members && !member_count;
    }
    else if (ok)
        ok = row < samples->row_count && (samples->rows[row].metrics & TP_RETIREMENT_SAMPLE_RUNTIME) &&
            !members && !member_count;
    if (ok && invocation.phase)
    {
        if (invocation.kind)
            ok = tp_retirement_sample_update(samples, tp_retirement_samples_ordinal(samples, 0, row, &invocation),
                1, &invocation, wall, 0);
        else if (group->kind == TP_RETIREMENT_GROUP_SINGLETON)
            ok = tp_retirement_sample_update(samples, tp_retirement_samples_ordinal(samples, 0,
                samples->members[group->first], &invocation), 0, &invocation, wall, rss);
        else
        {
            ok = tp_retirement_sample_update(samples, tp_retirement_samples_ordinal(samples,
                samples->row_records, group->object, &invocation), 0, &invocation, wall, rss);
            for (unsigned i = 0; ok && i < group->count; ++i)
                ok = tp_retirement_sample_update(samples, tp_retirement_samples_ordinal(samples, 0,
                    samples->members[group->first + i], &invocation), 0, &invocation,
                    members[i].interval_ns, members[i].peak_memory_bytes);
        }
    }
    if (ok) ok = tp_retirement_transcript_append(transcript, observed, process, output);
    if (ok)
    {
        if (invocation.phase && invocation.kind)
            tp_retirement_sample_hash(&samples->rows[row].observations[1], samples->rows[row].id, 1,
                (unsigned)invocation.round, (unsigned)invocation.pair, invocation.variant, wall, 0);
        else if (invocation.phase && group->kind == TP_RETIREMENT_GROUP_SINGLETON)
        {
            unsigned dense = samples->members[group->first];
            tp_retirement_sample_hash(&samples->rows[dense].observations[0], samples->rows[dense].id, 0,
                (unsigned)invocation.round, (unsigned)invocation.pair, invocation.variant, wall, rss);
        }
        else if (invocation.phase)
        {
            tp_retirement_sample_hash(&samples->groups[invocation.group].observations, invocation.group, 0,
                (unsigned)invocation.round, (unsigned)invocation.pair, invocation.variant, wall, rss);
            for (unsigned i = 0; i < group->count; ++i)
            {
                unsigned dense = samples->members[group->first + i];
                tp_retirement_sample_hash(&samples->rows[dense].observations[0], samples->rows[dense].id, 0,
                    (unsigned)invocation.round, (unsigned)invocation.pair, invocation.variant,
                    members[i].interval_ns, members[i].peak_memory_bytes);
            }
        }
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

static int tp_retirement_sample_values(uint64_t const values[TP_RETIREMENT_SAMPLE_VALUES], unsigned population,
                                       unsigned metrics)
{
    int runtime = population == TP_RETIREMENT_POPULATION_ROWS && (metrics & TP_RETIREMENT_SAMPLE_RUNTIME);
    int ok = population <= TP_RETIREMENT_POPULATION_BATCHES && values[6] <= (runtime ? 3u : 1u);
    for (unsigned variant = 0; ok && variant < 2; ++variant)
    {
        ok = values[variant] && values[variant] <= UINT64_C(86400000000000) &&
            values[2 + variant] && values[2 + variant] <= UINT64_C(9007199254740991);
        ok = ok && (runtime ? values[4 + variant] && values[4 + variant] <= UINT64_C(86400000000000) :
            !values[4 + variant]);
    }
    return ok;
}

static size_t tp_retirement_sample_record(char* bytes, size_t capacity, unsigned row,
    unsigned round, unsigned pair, unsigned metrics, uint64_t const values[TP_RETIREMENT_SAMPLE_VALUES])
{
    size_t result = 0;
    char wall[2][32], runtime[2][32], runtime_text[128] = "";
    int ok = bytes && capacity && capacity <= TP_RETIREMENT_SAMPLE_LINE_CAP &&
        row < TP_RETIREMENT_MAX_CELLS && round < TP_RETIREMENT_ROUNDS && pair < TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
        !(metrics & ~TP_RETIREMENT_SAMPLE_RUNTIME) &&
        tp_retirement_sample_values(values, TP_RETIREMENT_POPULATION_ROWS, metrics);
    for (unsigned variant = 0; ok && variant < 2; ++variant)
    {
        ok = tp_retirement_seconds(wall[variant], values[variant]);
        if (ok && (metrics & TP_RETIREMENT_SAMPLE_RUNTIME))
            ok = tp_retirement_seconds(runtime[variant], values[4 + variant]);
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
            "{\"measurements\":{\"compiler_peak_memory\":{\"baseline\":%" PRIu64 ",\"candidate\":%" PRIu64 "},"
            "\"compiler_wall_time\":{\"baseline\":%s,\"candidate\":%s}%s},\"pair\":%u,"
            "\"record_id\":\"row-%u/round-%u/pair-%u\",\"round\":%u,\"row\":%u}\n",
            values[2], values[3], wall[0], wall[1], runtime_text, pair, row, round, pair, round, row);
        if (length > 0 && (size_t)length < capacity) result = (size_t)length;
    }
    if (!result && bytes && capacity) bytes[0] = 0;
    return result;
}

static size_t tp_retirement_batch_record(char* bytes, size_t capacity, unsigned group,
    unsigned round, unsigned pair, uint64_t const values[TP_RETIREMENT_SAMPLE_VALUES])
{
    size_t result = 0;
    char wall[2][32];
    int ok = bytes && capacity && capacity <= TP_RETIREMENT_SAMPLE_LINE_CAP &&
        group < TP_RETIREMENT_MAX_CELLS && round < TP_RETIREMENT_ROUNDS && pair < TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
        tp_retirement_sample_values(values, TP_RETIREMENT_POPULATION_BATCHES, 0) &&
        tp_retirement_seconds(wall[0], values[0]) && tp_retirement_seconds(wall[1], values[1]);
    if (ok)
    {
        int length = snprintf(bytes, capacity,
            "{\"group\":%u,\"measurements\":{\"compiler_batch_peak_rss\":{\"baseline\":%" PRIu64
            ",\"candidate\":%" PRIu64 "},\"compiler_batch_wall_time\":{\"baseline\":%s,\"candidate\":%s}},"
            "\"pair\":%u,\"record_id\":\"group-%u/round-%u/pair-%u\",\"round\":%u}\n",
            group, values[2], values[3], wall[0], wall[1], pair, group, round, pair, round);
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

static uint64_t tp_retirement_samples_population_records(TpRetirementSamples const* samples, unsigned population)
{
    return !samples ? 0 : population == TP_RETIREMENT_POPULATION_ROWS ? samples->row_records :
        population == TP_RETIREMENT_POPULATION_BATCHES ? samples->expected - samples->row_records : 0;
}

/* The population the next exported shard belongs to. */
static unsigned tp_retirement_samples_population(TpRetirementSamples const* samples)
{
    return samples && samples->exported >= samples->row_records ?
        TP_RETIREMENT_POPULATION_BATCHES : TP_RETIREMENT_POPULATION_ROWS;
}

/* Compare a completed unit's exported observations with its collected ones. */
static int tp_retirement_samples_verify(TpRetirementSamples* samples, Sha256 const* expected, unsigned kinds)
{
    int ok = 1;
    for (unsigned kind = 0; ok && kind < kinds; ++kind)
    {
        Sha256 copy = expected[kind];
        char actual_digest[65], expected_digest[65];
        sha256_finish_hex(&samples->exported_observations[kind], actual_digest);
        sha256_finish_hex(&copy, expected_digest);
        ok = !strcmp(actual_digest, expected_digest);
    }
    return ok;
}

/* The approved schedule orders each unit's observations by round/pair/
 * position. Hash exactly the values being serialized, including their variant
 * order, and compare at unit completion, which catches mutation even when a
 * unit crosses a shard boundary. A shard never mixes populations. Earlier
 * shards stay partial integrity artifacts until finish/manifest succeed. */
static int tp_retirement_samples_write_shard(TpRetirementSamples* samples, FILE* stream, TpRetirementShard* shard)
{
    int ok = samples && !samples->failed && samples->exporting && !samples->finished && shard &&
        samples->transcript && !samples->transcript->failed && samples->transcript->finished &&
        tp_retirement_execution_complete(samples->transcript->execution) &&
        stream && stream != samples->spool && samples->exported < samples->expected &&
        tp_retirement_sample_size(stream, 0) && tp_retirement_sample_seek(stream, 0, SEEK_SET);
    unsigned population = tp_retirement_samples_population(samples);
    uint64_t end = samples ? (population ? samples->expected : samples->row_records) : 0;
    TpRetirementShard next = {0};
    Sha256 hash;
    sha256_init(&hash);
    if (shard) *shard = (TpRetirementShard){0};
    while (ok && samples->exported < end && next.records < TP_RETIREMENT_SAMPLE_SHARD_RECORDS)
    {
        unsigned pairs = samples->transcript->execution->pairs;
        uint64_t local = samples->exported - (population ? samples->row_records : 0);
        unsigned unit = (unsigned)(local / (TP_RETIREMENT_ROUNDS * pairs));
        unsigned round = (unsigned)(local / pairs % TP_RETIREMENT_ROUNDS);
        unsigned pair = (unsigned)(local % pairs);
        unsigned global = population ? samples->row_count + unit : unit;
        if (samples->verified_unit != global)
        {
            sha256_init(&samples->exported_observations[0]);
            sha256_init(&samples->exported_observations[1]);
            samples->verified_unit = global;
            if (population)
            {
                while (samples->export_group < samples->group_count &&
                       samples->groups[samples->export_group].object != unit) ++samples->export_group;
                ok = samples->export_group < samples->group_count;
            }
        }
        uint64_t values[TP_RETIREMENT_SAMPLE_VALUES];
        char bytes[TP_RETIREMENT_SAMPLE_LINE_CAP];
        unsigned metrics = population ? 0 : samples->rows[unit].metrics;
        ok = ok && tp_retirement_sample_read(samples, samples->exported, values) &&
            tp_retirement_sample_values(values, population, metrics);
        for (unsigned kind = 0; ok && kind < 2; ++kind)
            for (unsigned position = 0; position < 2; ++position)
            {
                if (!kind || (metrics & TP_RETIREMENT_SAMPLE_RUNTIME))
                {
                    unsigned variant = (unsigned)((values[6] >> kind) & 1) ^ position;
                    tp_retirement_sample_hash(&samples->exported_observations[kind],
                        population ? samples->export_group : samples->rows[unit].id, kind,
                        round, pair, variant, values[(kind ? 4 : 0) + variant], kind ? 0 : values[2 + variant]);
                }
            }
        if (ok && round + 1 == TP_RETIREMENT_ROUNDS && pair + 1 == pairs)
            ok = population ? tp_retirement_samples_verify(samples, &samples->groups[samples->export_group].observations, 1) :
                tp_retirement_samples_verify(samples, samples->rows[unit].observations, 2);
        size_t count = !ok ? 0 : population ?
            tp_retirement_batch_record(bytes, sizeof(bytes), samples->export_group, round, pair, values) :
            tp_retirement_sample_record(bytes, sizeof(bytes), samples->rows[unit].id, round, pair, metrics, values);
        ok = ok && count && count <= (population ? TP_RETIREMENT_BATCH_RECORD_BYTES_MAX :
                                                   TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX) &&
            next.bytes <= TP_RETIREMENT_TRANSCRIPT_SHARD_BYTES &&
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
        tp_retirement_samples_descriptor_hash(&samples->descriptors[population], &next);
        *shard = next;
        ++samples->shards[population];
    }
    else tp_retirement_samples_poison(samples);
    return ok;
}

static int tp_retirement_samples_finish(TpRetirementSamples* samples)
{
    uint64_t batches = tp_retirement_samples_population_records(samples, TP_RETIREMENT_POPULATION_BATCHES);
    int ok = samples && !samples->failed && samples->exporting && !samples->finished &&
        samples->exported == samples->expected &&
        samples->shards[0] == (samples->row_records + TP_RETIREMENT_SAMPLE_SHARD_RECORDS - 1) /
                              TP_RETIREMENT_SAMPLE_SHARD_RECORDS &&
        samples->shards[1] == (batches + TP_RETIREMENT_SAMPLE_SHARD_RECORDS - 1) / TP_RETIREMENT_SAMPLE_SHARD_RECORDS &&
        !samples->transcript->failed && samples->transcript->finished &&
        tp_retirement_execution_complete(samples->transcript->execution) &&
        tp_retirement_sample_size(samples->spool, samples->spool_bytes);
    if (ok)
    {
        sha256_finish_hex(&samples->raw, samples->raw_sha256);
        sha256_finish_hex(&samples->descriptors[0], samples->descriptors_sha256[0]);
        sha256_finish_hex(&samples->descriptors[1], samples->descriptors_sha256[1]);
        samples->finished = 1;
    }
    else tp_retirement_samples_poison(samples);
    return ok;
}

/* One #615 manifest per full-cap partition of one population. Shard paths
 * and identities are fixed per population, not request fields. */
static int tp_retirement_samples_manifest(TpRetirementSamples* samples, unsigned population,
    TpRetirementShard const* shards, unsigned shard_count, unsigned partition, FILE* stream,
    TpRetirementShard* descriptor)
{
    unsigned per_partition = (unsigned)(TP_RETIREMENT_SAMPLE_PARTITION_RECORDS / TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
    uint64_t population_records = tp_retirement_samples_population_records(samples, population);
    unsigned partitions = (unsigned)tp_retirement_samples_partitions(population_records);
    int ok = samples && !samples->failed && samples->finished && samples->transcript &&
        population <= TP_RETIREMENT_POPULATION_BATCHES &&
        !samples->transcript->failed && samples->transcript->finished &&
        tp_retirement_execution_complete(samples->transcript->execution) &&
        shards && shard_count == samples->shards[population] &&
        shard_count == (population_records + TP_RETIREMENT_SAMPLE_SHARD_RECORDS - 1) /
                       TP_RETIREMENT_SAMPLE_SHARD_RECORDS && partition < partitions && stream && descriptor &&
        stream != samples->spool && tp_retirement_sample_size(stream, 0) && tp_retirement_sample_seek(stream, 0, SEEK_SET);
    char const* prefix = population == TP_RETIREMENT_POPULATION_BATCHES ? "batches" : "samples";
    Sha256 descriptors_hash, hash;
    sha256_init(&descriptors_hash);
    sha256_init(&hash);
    uint64_t remaining = population_records, partition_bytes = 0;
    for (unsigned index = 0; ok && index < shard_count; ++index)
    {
        uint64_t records = remaining < TP_RETIREMENT_SAMPLE_SHARD_RECORDS ? remaining : TP_RETIREMENT_SAMPLE_SHARD_RECORDS;
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
    if (ok) ok = !remaining && !strcmp(digest, samples->descriptors_sha256[population]) &&
        partition_bytes <= TP_RETIREMENT_SAMPLE_PARTITION_BYTES;
    TpRetirementShard result = {0};
    if (descriptor) *descriptor = result;
    unsigned begin = partition * per_partition;
    unsigned end = begin + per_partition < shard_count ? begin + per_partition : shard_count;
    /* Emit a bounded fragment at a time. */
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
                "%s{\"bytes\":%" PRIu64 ",\"identity\":\"%s-%04u\",\"path\":\"retirement-%s-%04u.jsonl\",\"sha256\":\"%s\"}",
                number == begin ? "" : ",", shards[number].bytes, prefix, number, prefix, number, shards[number].sha256);
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

/* (A1) The per-row code record set: one record per code-observed row on every
 * target, in ascending row order, holding each variant's frozen artifact, its
 * once-parsed code-section bytes and digest, and an independent reproduction
 * digest that must equal the artifact. The caller supplies facts read outside
 * timing (tp_retirement_code_observe on Linux); this encodes canonical bytes. */
typedef struct TpRetirementCodeSide
{
    char artifact_sha256[65], code_sha256[65], reproduction_sha256[65];
    uint64_t code_bytes;
} TpRetirementCodeSide;

typedef struct TpRetirementCodeRecords
{
    FILE* stream;
    Sha256 hash;
    uint64_t bytes, records;
    unsigned last_row;
    int failed, finished;
} TpRetirementCodeRecords;

static inline int tp_retirement_code_side(TpRetirementCodeSide const* side)
{
    int ok = side && tp_retirement_digest(side->artifact_sha256) && tp_retirement_digest(side->code_sha256) &&
        tp_retirement_digest(side->reproduction_sha256) &&
        !strcmp(side->artifact_sha256, side->reproduction_sha256) && side->code_bytes <= INT64_MAX &&
        (side->code_bytes || !strcmp(side->code_sha256,
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    return ok;
}

static inline size_t tp_retirement_code_record(char* bytes, size_t capacity, unsigned row,
    TpRetirementCodeSide const sides[2])
{
    size_t result = 0;
    int ok = bytes && capacity && capacity <= TP_RETIREMENT_SAMPLE_LINE_CAP && row < TP_RETIREMENT_MAX_CELLS &&
        sides && tp_retirement_code_side(&sides[0]) && tp_retirement_code_side(&sides[1]);
    if (ok)
    {
        int length = snprintf(bytes, capacity,
            "{\"baseline\":{\"artifact_sha256\":\"%s\",\"code_section_bytes\":%" PRIu64 ",\"code_section_sha256\":\"%s\","
            "\"reproduction_sha256\":\"%s\"},\"candidate\":{\"artifact_sha256\":\"%s\",\"code_section_bytes\":%" PRIu64
            ",\"code_section_sha256\":\"%s\",\"reproduction_sha256\":\"%s\"},\"row\":%u}\n",
            sides[0].artifact_sha256, sides[0].code_bytes, sides[0].code_sha256, sides[0].reproduction_sha256,
            sides[1].artifact_sha256, sides[1].code_bytes, sides[1].code_sha256, sides[1].reproduction_sha256, row);
        if (length > 0 && (size_t)length < capacity) result = (size_t)length;
    }
    if (!result && bytes && capacity) bytes[0] = 0;
    return result;
}

static inline int tp_retirement_code_records_init(TpRetirementCodeRecords* records, FILE* stream)
{
    int ok = records && stream && tp_retirement_sample_size(stream, 0) && tp_retirement_sample_seek(stream, 0, SEEK_SET);
    if (records)
    {
        *records = (TpRetirementCodeRecords){.failed = !ok, .last_row = TP_RETIREMENT_NONE};
        if (ok) records->stream = stream;
        sha256_init(&records->hash);
    }
    return ok;
}

static inline int tp_retirement_code_records_append(TpRetirementCodeRecords* records, unsigned row,
    TpRetirementCodeSide const sides[2])
{
    char bytes[TP_RETIREMENT_SAMPLE_LINE_CAP];
    int ok = records && !records->failed && !records->finished && records->stream &&
        (records->last_row == TP_RETIREMENT_NONE || row > records->last_row);
    size_t count = ok ? tp_retirement_code_record(bytes, sizeof(bytes), row, sides) : 0;
    ok = ok && count && count <= TP_RETIREMENT_CODE_RECORD_BYTES_MAX &&
        fwrite(bytes, 1, count, records->stream) == count && !ferror(records->stream);
    if (ok)
    {
        sha256_add(&records->hash, bytes, (u64)count);
        records->bytes += count;
        ++records->records;
        records->last_row = row;
    }
    else if (records) records->failed = 1;
    return ok;
}

static inline int tp_retirement_code_records_finish(TpRetirementCodeRecords* records, TpRetirementShard* descriptor)
{
    int ok = records && !records->failed && !records->finished && records->records && descriptor &&
        fflush(records->stream) == 0 && !ferror(records->stream);
    if (descriptor) *descriptor = (TpRetirementShard){0};
    if (ok)
    {
        descriptor->bytes = records->bytes;
        descriptor->records = records->records;
        sha256_finish_hex(&records->hash, descriptor->sha256);
        records->finished = 1;
    }
    else if (records) records->failed = 1;
    return ok;
}
#endif
