/* #881-D pre-sample and post-A/A workflow documents of the in-unit campaign,
 * written canonically in C for the validator
 * (tools/native_retirement_performance_binding.py) and lane E's composer.
 *
 * Ownership: lane D. Every document is exactly
 * json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
 * of the validator's schema, streamed to a caller-owned file while hashed; its
 * descriptor (bytes, SHA-256) is what the workflow binds. Nothing here
 * chooses a measurement value: every field comes from the pinned performance
 * rows, the sealed correctness gate, the frozen campaign plan and pins, the
 * reviewed budget, the untimed batches and their observed reproductions.
 *
 * Entry points (the pinned rows are parsed by
 * bq_retirement_documents_population in retirement_campaign_service.h,
 * through the installed census):
 *   bq_retirement_unit_campaign_documents  the driver's step before A/A:
 *                                        derive, check and write the oracle,
 *                                        execution-plan, result-input and
 *                                        pre-sample documents
 *   bq_retirement_unit_campaign_post_aa_document  the driver's step after the
 *                                        A/A admission: the post-A/A binding
 *   bq_retirement_documents_partition    the validator's timed and untimed
 *                                        batch-group partitions (_batch_groups,
 *                                        _untimed_groups)
 *   bq_retirement_documents_family       the statistical family digest and its
 *                                        two per-scope counts
 *                                        (_derive_statistical_family)
 *   bq_retirement_documents_oracle       workflow.records.oracle
 *   bq_retirement_documents_execution_plan  workflow.execution_plan (plan v3)
 *   bq_retirement_documents_result_input_plan  workflow.records.result_input_plan
 *   bq_retirement_documents_phase        workflow.phases.pre_sample_plan and
 *                                        workflow.phases.post_aa_binding
 *
 * Map: BqRetirementDocumentWriter, BqRetirementDocumentPopulation,
 * BqRetirementDocumentPartition, BqRetirementDocumentFamily,
 * BqRetirementDocumentInputs, BqRetirementDocumentDescriptor,
 * BqRetirementDocumentPhase, bq_retirement_document_identity,
 * BqRetirementUnitCampaignDocumentSources.
 *
 * Nothing here reads the census: this header needs only the driver
 * (retirement_unit_campaign.h), so the throughput functional fixture runs the
 * driver's document steps over an in-memory population.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_DOCUMENTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_DOCUMENTS_H
#include "retirement_unit_campaign.h"

#ifdef __linux__
/* The validator's schema names and fixed values. */
#define BQ_RETIREMENT_DOCUMENT_EXECUTION_PLAN_SCHEMA "buster-native-retirement-execution-plan-v3"
#define BQ_RETIREMENT_DOCUMENT_ORACLE_SCHEMA "buster-native-retirement-oracle-v1"
#define BQ_RETIREMENT_DOCUMENT_RESULT_INPUT_SCHEMA "buster-native-retirement-result-input-plan-v3"
#define BQ_RETIREMENT_DOCUMENT_PRE_SAMPLE_SCHEMA "buster-native-retirement-pre-sample-plan-v1"
#define BQ_RETIREMENT_DOCUMENT_POST_AA_SCHEMA "buster-native-retirement-post-aa-binding-v1"
#define BQ_RETIREMENT_DOCUMENT_TIMED_TARGET "x86_64-unknown-linux-gnu"
#define BQ_RETIREMENT_DOCUMENT_SCHEDULE "tp-retirement-block-schedule-v2"
/* #615's immutable per-manifest record cap (native_retirement_result_input.py
 * HARD_CAPS["max_records"]) and three-partition bound. */
#define BQ_RETIREMENT_DOCUMENT_MANIFEST_RECORDS (UINT64_C(16) * 1024u * 1024u)
#define BQ_RETIREMENT_DOCUMENT_PARTITIONS BQ_RETIREMENT_UNIT_CAMPAIGN_PARTITIONS
/* The validator's ROW_IDENTITY_FIELDS order. */
#define BQ_RETIREMENT_DOCUMENT_FIELDS 15u
enum
{
    BQ_RETIREMENT_DOCUMENT_FIXTURE, BQ_RETIREMENT_DOCUMENT_TARGET, BQ_RETIREMENT_DOCUMENT_TARGET_ABI,
    BQ_RETIREMENT_DOCUMENT_CPU, BQ_RETIREMENT_DOCUMENT_CPU_FEATURES, BQ_RETIREMENT_DOCUMENT_ALLOCATOR,
    BQ_RETIREMENT_DOCUMENT_FRONTEND, BQ_RETIREMENT_DOCUMENT_PIC, BQ_RETIREMENT_DOCUMENT_RECIPE,
    BQ_RETIREMENT_DOCUMENT_COMPILE, BQ_RETIREMENT_DOCUMENT_LINK, BQ_RETIREMENT_DOCUMENT_EXECUTION,
    BQ_RETIREMENT_DOCUMENT_DIAGNOSTIC, BQ_RETIREMENT_DOCUMENT_ARGV, BQ_RETIREMENT_DOCUMENT_STAGE
};
/* BATCH_GROUP_KEY_FIELDS: allocator, frontend_lowering, PIC, fixture_recipe,
 * cpu, cpu_features. */
static unsigned const bq_retirement_document_key_fields[6] = {BQ_RETIREMENT_DOCUMENT_ALLOCATOR,
    BQ_RETIREMENT_DOCUMENT_FRONTEND, BQ_RETIREMENT_DOCUMENT_PIC, BQ_RETIREMENT_DOCUMENT_RECIPE,
    BQ_RETIREMENT_DOCUMENT_CPU, BQ_RETIREMENT_DOCUMENT_CPU_FEATURES};
/* STATISTICAL_DIMENSIONS: target, cpu, allocator, frontend_lowering, PIC,
 * artifact_stage. */
static unsigned const bq_retirement_document_dimensions[6] = {BQ_RETIREMENT_DOCUMENT_TARGET,
    BQ_RETIREMENT_DOCUMENT_CPU, BQ_RETIREMENT_DOCUMENT_ALLOCATOR, BQ_RETIREMENT_DOCUMENT_FRONTEND,
    BQ_RETIREMENT_DOCUMENT_PIC, BQ_RETIREMENT_DOCUMENT_STAGE};
static char const* const bq_retirement_document_dimension_names[6] = {"target", "cpu", "allocator",
    "frontend_lowering", "PIC", "artifact_stage"};

/* A canonical-JSON stream: bytes go to `stream` (may be NULL for a digest
 * only) and into the hash. Strings are escaped as json.dumps does. */
typedef struct BqRetirementDocumentWriter
{
    FILE* stream;
    Sha256 hash;
    uint64_t bytes;
    int ok;
} BqRetirementDocumentWriter;

static inline void bq_retirement_document_begin(BqRetirementDocumentWriter* writer, FILE* stream)
{
    *writer = (BqRetirementDocumentWriter){.stream = stream, .ok = 1};
    sha256_init(&writer->hash);
}

static inline void bq_retirement_document_raw(BqRetirementDocumentWriter* writer, char const* data, size_t length)
{
    writer->ok = writer->ok && (!writer->stream || fwrite(data, 1, length, writer->stream) == length);
    if (writer->ok)
    {
        sha256_add(&writer->hash, data, (u64)length);
        writer->bytes += length;
    }
}

static inline void bq_retirement_document_text(BqRetirementDocumentWriter* writer, char const* text)
{
    bq_retirement_document_raw(writer, text, strlen(text));
}

/* A JSON string of printable ASCII and control bytes (the only bytes these
 * documents carry); any other byte fails the writer. */
static inline void bq_retirement_document_string(BqRetirementDocumentWriter* writer, char const* text, size_t length)
{
    bq_retirement_document_raw(writer, "\"", 1);
    for (size_t index = 0; writer->ok && index < length; ++index)
    {
        unsigned char byte = (unsigned char)text[index];
        char escaped[8];
        int count = 0;
        if (byte == '"' || byte == '\\') count = snprintf(escaped, sizeof(escaped), "\\%c", byte);
        else if (byte == '\n') count = snprintf(escaped, sizeof(escaped), "\\n");
        else if (byte == '\t') count = snprintf(escaped, sizeof(escaped), "\\t");
        else if (byte == '\r') count = snprintf(escaped, sizeof(escaped), "\\r");
        else if (byte == '\b') count = snprintf(escaped, sizeof(escaped), "\\b");
        else if (byte == '\f') count = snprintf(escaped, sizeof(escaped), "\\f");
        else if (byte < 0x20) count = snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
        else if (byte < 0x7f)
        {
            escaped[0] = (char)byte;
            count = 1;
        }
        writer->ok = count > 0;
        bq_retirement_document_raw(writer, escaped, (size_t)(count > 0 ? count : 0));
    }
    bq_retirement_document_raw(writer, "\"", 1);
}

static inline void bq_retirement_document_cstring(BqRetirementDocumentWriter* writer, char const* text)
{
    writer->ok = writer->ok && text;
    if (writer->ok) bq_retirement_document_string(writer, text, strlen(text));
}

/* `"key":` after a comma unless first. */
static inline void bq_retirement_document_key(BqRetirementDocumentWriter* writer, char const* key, int first)
{
    if (!first) bq_retirement_document_raw(writer, ",", 1);
    bq_retirement_document_cstring(writer, key);
    bq_retirement_document_raw(writer, ":", 1);
}

static inline void bq_retirement_document_number(BqRetirementDocumentWriter* writer, uint64_t value)
{
    char text[24];
    int count = snprintf(text, sizeof(text), "%" PRIu64, value);
    writer->ok = writer->ok && count > 0;
    bq_retirement_document_raw(writer, text, (size_t)(count > 0 ? count : 0));
}

/* A 64-hex digest string, or null when absent (empty). */
static inline void bq_retirement_document_digest(BqRetirementDocumentWriter* writer, char const* digest, int present)
{
    if (present)
    {
        writer->ok = writer->ok && tp_retirement_digest(digest);
        bq_retirement_document_cstring(writer, digest);
    }
    else bq_retirement_document_text(writer, "null");
}

static inline int bq_retirement_document_end(BqRetirementDocumentWriter* writer, char digest[65])
{
    int ok = writer->ok && (!writer->stream || fflush(writer->stream) == 0);
    Sha256 copy = writer->hash;
    sha256_finish_hex(&copy, digest);
    if (!ok) digest[0] = 0;
    return ok;
}

/* One performance row: its 15 identity values (offsets into the pool, in
 * ROW_IDENTITY_FIELDS order), its compile, code and runtime eligibility and
 * its code-section marker (0 not-applicable, 1 zero baseline, 2 code). */
typedef struct BqRetirementDocumentRow
{
    uint32_t offset[BQ_RETIREMENT_DOCUMENT_FIELDS];
    uint32_t length[BQ_RETIREMENT_DOCUMENT_FIELDS];
    unsigned compile, code, runtime, marker;
} BqRetirementDocumentRow;

/* The pinned #508 performance rows, parsed once (retirement_campaign_service.h,
 * bq_retirement_documents_population, through the installed census and its
 * profile pin). performance_rows_sha256 is the pinned file's digest. */
typedef struct BqRetirementDocumentPopulation
{
    BqRetirementDocumentRow* rows;
    char* pool;
    uint64_t pool_used, pool_capacity;
    uint32_t count, native_target;
    char performance_rows_sha256[65];
} BqRetirementDocumentPopulation;

static inline void bq_retirement_documents_population_release(BqRetirementDocumentPopulation* population)
{
    if (population)
    {
        free(population->rows);
        free(population->pool);
        *population = (BqRetirementDocumentPopulation){0};
    }
}

static inline String8 bq_retirement_document_value(BqRetirementDocumentPopulation const* population, uint32_t row,
    unsigned field)
{
    BqRetirementDocumentRow const* entry = &population->rows[row];
    String8 value = {(char8*)population->pool + entry->offset[field], entry->length[field]};
    return value;
}

static inline int bq_retirement_document_equal(BqRetirementDocumentPopulation const* population, uint32_t left,
    uint32_t right, unsigned field)
{
    String8 a = bq_retirement_document_value(population, left, field);
    String8 b = bq_retirement_document_value(population, right, field);
    int same = a.length == b.length && !memcmp(a.pointer, b.pointer, (size_t)a.length);
    return same;
}

/* Whether population row `row` is in the native-host timed projection. */
static inline int bq_retirement_document_timed(BqRetirementDocumentPopulation const* population, uint32_t row)
{
    String8 target = bq_retirement_document_value(population, row, BQ_RETIREMENT_DOCUMENT_TARGET);
    int timed = population->rows[row].compile && string_equal(target, S8(BQ_RETIREMENT_DOCUMENT_TIMED_TARGET));
    return timed;
}

static inline int bq_retirement_document_object(BqRetirementDocumentPopulation const* population, uint32_t row)
{
    int object = string_equal(bq_retirement_document_value(population, row, BQ_RETIREMENT_DOCUMENT_STAGE), S8("object"));
    return object;
}

/* The validator's partitions: groups[i] is a group's kind (1 object batch,
 * 0 singleton stage) and its member rows (rows[first[i] .. first[i + 1])) in
 * ascending row order; groups are in ascending smallest-member order. The
 * timed partition keys object rows by BATCH_GROUP_KEY_FIELDS, the untimed
 * one (compile-eligible rows outside the timed projection) also by target. */
typedef struct BqRetirementDocumentPartition
{
    uint32_t* rows;
    uint32_t* first;
    unsigned* object;
    uint32_t count, row_count, object_groups;
} BqRetirementDocumentPartition;

static inline void bq_retirement_documents_partition_release(BqRetirementDocumentPartition* partition)
{
    if (partition)
    {
        free(partition->rows);
        free(partition->first);
        free(partition->object);
        *partition = (BqRetirementDocumentPartition){0};
    }
}

static inline int bq_retirement_documents_partition(BqRetirementDocumentPopulation const* population, int untimed,
    BqRetirementDocumentPartition* partition)
{
    uint32_t count = population ? population->count : 0;
    uint32_t* group_of = count ? calloc(count, sizeof(*group_of)) : NULL;
    unsigned* object = count ? calloc(count, sizeof(*object)) : NULL;
    uint32_t* leader = count ? calloc(count, sizeof(*leader)) : NULL;
    uint32_t* sizes = count ? calloc(count, sizeof(*sizes)) : NULL;
    int ok = partition && group_of && object && leader && sizes;
    if (partition) *partition = (BqRetirementDocumentPartition){0};
    uint32_t groups = 0, members = 0;
    for (uint32_t row = 0; ok && row < count; ++row)
    {
        int timed = bq_retirement_document_timed(population, row);
        group_of[row] = UINT32_MAX;
        if (untimed ? timed || !population->rows[row].compile : !timed) continue;
        int is_object = bq_retirement_document_object(population, row);
        uint32_t found = UINT32_MAX;
        for (uint32_t group = 0; is_object && group < groups && found == UINT32_MAX; ++group)
        {
            int same = object[group] && (!untimed ||
                bq_retirement_document_equal(population, leader[group], row, BQ_RETIREMENT_DOCUMENT_TARGET));
            for (unsigned key = 0; same && key < 6; ++key)
                same = bq_retirement_document_equal(population, leader[group], row, bq_retirement_document_key_fields[key]);
            if (same) found = group;
        }
        if (found == UINT32_MAX)
        {
            found = groups++;
            object[found] = (unsigned)is_object;
            leader[found] = row;
        }
        group_of[row] = found;
        sizes[found] += 1;
        members += 1;
    }
    if (ok)
    {
        partition->rows = calloc(members ? members : 1, sizeof(*partition->rows));
        partition->first = calloc((size_t)groups + 1u, sizeof(*partition->first));
        partition->object = calloc(groups ? groups : 1, sizeof(*partition->object));
        ok = partition->rows && partition->first && partition->object;
    }
    for (uint32_t group = 0; ok && group < groups; ++group)
    {
        partition->first[group + 1] = partition->first[group] + sizes[group];
        partition->object[group] = object[group];
        partition->object_groups += object[group];
        sizes[group] = 0;
    }
    for (uint32_t row = 0; ok && row < count; ++row)
        if (group_of[row] != UINT32_MAX)
        {
            uint32_t group = group_of[row];
            partition->rows[partition->first[group] + sizes[group]++] = row;
        }
    if (ok)
    {
        partition->count = groups;
        partition->row_count = members;
    }
    else bq_retirement_documents_partition_release(partition);
    free(group_of);
    free(object);
    free(leader);
    free(sizes);
    return ok;
}

/* The derived statistical family: its canonical digest, the bootstrap
 * (aggregate and slice) and exact-cell member counts per scope. */
typedef struct BqRetirementDocumentFamily
{
    char sha256[65];
    unsigned bootstrap_members, cell_members, members;
} BqRetirementDocumentFamily;

/* Ascending byte order of the `count` strings in `texts` (iterative merge
 * sort of an index array). */
static inline int bq_retirement_document_sort(char** texts, uint32_t count)
{
    uint32_t* order = calloc(count ? count : 1, sizeof(*order));
    uint32_t* scratch = calloc(count ? count : 1, sizeof(*scratch));
    int ok = order && scratch;
    for (uint32_t index = 0; ok && index < count; ++index) order[index] = index;
    for (uint32_t width = 1; ok && width < count; width *= 2)
    {
        for (uint32_t start = 0; start < count; start += 2 * width)
        {
            uint32_t middle = start + width < count ? start + width : count;
            uint32_t end = start + 2 * width < count ? start + 2 * width : count;
            uint32_t left = start, right = middle, out = start;
            while (left < middle || right < end)
            {
                int take_left = right >= end || (left < middle && strcmp(texts[order[left]], texts[order[right]]) <= 0);
                scratch[out++] = take_left ? order[left++] : order[right++];
            }
        }
        uint32_t* swap = order;
        order = scratch;
        scratch = swap;
    }
    char** sorted = ok ? calloc(count ? count : 1, sizeof(*sorted)) : NULL;
    ok = ok && sorted;
    for (uint32_t index = 0; ok && index < count; ++index) sorted[index] = texts[order[index]];
    if (ok) memcpy(texts, sorted, (size_t)count * sizeof(*texts));
    free(sorted);
    free(order);
    free(scratch);
    return ok;
}

/* One member name into the list (owned copies); duplicates are dropped
 * after sorting. */
static inline int bq_retirement_document_member(char*** members, uint32_t* count, uint32_t* capacity,
    char const* text)
{
    int ok = 1;
    if (*count == *capacity)
    {
        uint32_t grown = *capacity ? *capacity * 2 : 256;
        char** larger = realloc(*members, (size_t)grown * sizeof(**members));
        ok = larger != NULL;
        if (ok)
        {
            *members = larger;
            *capacity = grown;
        }
    }
    char* copy = ok ? strdup(text) : NULL;
    ok = ok && copy;
    if (ok) (*members)[(*count)++] = copy;
    return ok;
}

/* The family of the timed projection (_derive_statistical_family): per
 * variable metric its aggregate, one slice per dimension value present among
 * its cells and one member per cell, sorted and deduplicated, the cell
 * counts and the digest of the cells' canonical identities. Row metrics'
 * cells are timed rows (runtime only the runtime-eligible ones); the batch
 * pair's cells are the object batch groups. */
static inline int bq_retirement_documents_family(BqRetirementDocumentPopulation const* population,
    BqRetirementDocumentPartition const* timed, BqRetirementDocumentFamily* family)
{
    static char const* const metrics[5] = {"compiler_wall_time", "compiler_peak_memory", "generated_runtime",
        "compiler_batch_wall_time", "compiler_batch_peak_rss"};
    char** members = NULL;
    uint32_t member_count = 0, capacity = 0;
    uint64_t cell_counts[5] = {0};
    char cell_digests[5][65];
    int ok = population && timed && family;
    if (family) *family = (BqRetirementDocumentFamily){0};
    char name[512];
    for (unsigned metric = 0; ok && metric < 5; ++metric)
    {
        int batch = metric >= 3;
        uint32_t cells = batch ? timed->count : population->count;
        Sha256 digest;
        sha256_init(&digest);
        int named = snprintf(name, sizeof(name), "%s/aggregate", metrics[metric]);
        ok = named > 0 && (size_t)named < sizeof(name) && bq_retirement_document_member(&members, &member_count,
            &capacity, name);
        for (uint32_t cell = 0; ok && cell < cells; ++cell)
        {
            uint32_t row = batch ? timed->rows[timed->first[cell]] : cell;
            if (batch ? !timed->object[cell] : !bq_retirement_document_timed(population, row) ||
                (metric == 2 && !population->rows[row].runtime)) continue;
            cell_counts[metric] += 1;
            for (unsigned dimension = 0; ok && dimension < 6; ++dimension)
            {
                String8 value = bq_retirement_document_value(population, row, bq_retirement_document_dimensions[dimension]);
                named = snprintf(name, sizeof(name), "%s/slice/%s=%.*s", metrics[metric],
                    bq_retirement_document_dimension_names[dimension], (int)value.length, value.pointer);
                ok = named > 0 && (size_t)named < sizeof(name) &&
                    bq_retirement_document_member(&members, &member_count, &capacity, name);
            }
            named = snprintf(name, sizeof(name), "%s/cell/%s=%u", metrics[metric], batch ? "group" : "row",
                             batch ? cell : row);
            ok = ok && named > 0 && (size_t)named < sizeof(name) &&
                bq_retirement_document_member(&members, &member_count, &capacity, name);
            /* The cell's canonical identity line. */
            BqRetirementDocumentWriter line;
            bq_retirement_document_begin(&line, NULL);
            line.hash = digest;
            if (batch)
            {
                bq_retirement_document_text(&line, "{\"group\":");
                bq_retirement_document_number(&line, cell);
                bq_retirement_document_text(&line, ",\"key\":[");
                for (unsigned key = 0; key < 6; ++key)
                {
                    if (key) bq_retirement_document_raw(&line, ",", 1);
                    String8 value = bq_retirement_document_value(population, row, bq_retirement_document_key_fields[key]);
                    bq_retirement_document_string(&line, (char const*)value.pointer, (size_t)value.length);
                }
                bq_retirement_document_text(&line, "],\"rows\":[");
                for (uint32_t member = timed->first[cell]; member < timed->first[cell + 1]; ++member)
                {
                    if (member != timed->first[cell]) bq_retirement_document_raw(&line, ",", 1);
                    bq_retirement_document_number(&line, timed->rows[member]);
                }
                bq_retirement_document_text(&line, "]}\n");
            }
            else
            {
                bq_retirement_document_text(&line, "{\"identity\":[");
                for (unsigned field = 0; field < BQ_RETIREMENT_DOCUMENT_FIELDS; ++field)
                {
                    if (field) bq_retirement_document_raw(&line, ",", 1);
                    String8 value = bq_retirement_document_value(population, row, field);
                    bq_retirement_document_string(&line, (char const*)value.pointer, (size_t)value.length);
                }
                bq_retirement_document_text(&line, "],\"row\":");
                bq_retirement_document_number(&line, row);
                bq_retirement_document_text(&line, "}\n");
            }
            ok = ok && line.ok;
            digest = line.hash;
        }
        ok = ok && cell_counts[metric];
        sha256_finish_hex(&digest, cell_digests[metric]);
    }
    ok = ok && bq_retirement_document_sort(members, member_count);
    uint32_t unique = 0;
    for (uint32_t index = 0; ok && index < member_count; ++index)
    {
        if (unique && !strcmp(members[unique - 1], members[index])) free(members[index]);
        else members[unique++] = members[index];
    }
    BqRetirementDocumentWriter writer;
    bq_retirement_document_begin(&writer, NULL);
    /* Metric names in sorted key order for the two maps. */
    static unsigned const sorted[5] = {4, 3, 1, 0, 2};
    bq_retirement_document_text(&writer, "{\"cell_counts\":{");
    for (unsigned index = 0; index < 5; ++index)
    {
        bq_retirement_document_key(&writer, metrics[sorted[index]], !index);
        bq_retirement_document_number(&writer, cell_counts[sorted[index]]);
    }
    bq_retirement_document_text(&writer, "},\"cell_identity_sha256\":{");
    for (unsigned index = 0; index < 5; ++index)
    {
        bq_retirement_document_key(&writer, metrics[sorted[index]], !index);
        bq_retirement_document_cstring(&writer, cell_digests[sorted[index]]);
    }
    bq_retirement_document_text(&writer, "},\"dimensions\":[");
    for (unsigned index = 0; index < 6; ++index)
    {
        if (index) bq_retirement_document_raw(&writer, ",", 1);
        bq_retirement_document_cstring(&writer, bq_retirement_document_dimension_names[index]);
    }
    bq_retirement_document_text(&writer, "],\"members\":[");
    unsigned bootstrap = 0;
    for (uint32_t index = 0; ok && index < unique; ++index)
    {
        if (index) bq_retirement_document_raw(&writer, ",", 1);
        bq_retirement_document_cstring(&writer, members[index]);
        bootstrap += strstr(members[index], "/aggregate") || strstr(members[index], "/slice/") ? 1u : 0u;
        ok = strlen(members[index]) <= 127;
    }
    bq_retirement_document_text(&writer, "],\"metrics\":[");
    for (unsigned index = 0; index < 5; ++index)
    {
        if (index) bq_retirement_document_raw(&writer, ",", 1);
        bq_retirement_document_cstring(&writer, metrics[index]);
    }
    bq_retirement_document_text(&writer, "],\"scopes\":[\"round-1\",\"round-2\",\"pooled\"],\"timed_target\":\""
        BQ_RETIREMENT_DOCUMENT_TIMED_TARGET "\"}");
    uint64_t cells = 0;
    for (unsigned index = 0; index < 5; ++index) cells += cell_counts[index];
    ok = ok && bq_retirement_document_end(&writer, family->sha256) && bootstrap && bootstrap <= 80 && cells &&
        cells <= 300000;
    if (ok)
    {
        family->bootstrap_members = bootstrap;
        family->cell_members = (unsigned)cells;
        family->members = unique;
    }
    else if (family) *family = (BqRetirementDocumentFamily){0};
    for (uint32_t index = 0; index < unique; ++index) free(members[index]);
    free(members);
    return ok;
}

/* A written document: its size and SHA-256 (the workflow's descriptor, with
 * the path it is published under). */
typedef BqRetirementUnitCampaignDocument BqRetirementDocumentDescriptor;

/* Everything the pre-sample documents are derived from. untimed holds the
 * untimed batches in (group, variant, purpose) order with untimed_rows
 * mapping a singleton untimed group to its row, codes the observed untimed
 * code rows (reproductions) in ascending row order. The three source pins
 * are the recipe profile's support declaration, census manifest and census
 * rows digests. */
typedef struct BqRetirementDocumentInputs
{
    BqRetirementCorrectness const* gate;
    BqRetirementDocumentPopulation const* population;
    BqRetirementDocumentPartition const* timed;
    BqRetirementDocumentPartition const* untimed_partition;
    TpRetirementCampaignBudget const* budget;
    TpRetirementPlan const* plan;
    TpRetirementUntimedBatch const* untimed;
    unsigned untimed_count;
    unsigned const* untimed_rows;
    TpRetirementCodeRow const* codes;
    unsigned code_count;
    int cpu;
    char const* support_sha256;
    char const* manifest_sha256;
    char const* rows_sha256;
} BqRetirementDocumentInputs;

/* The baseline code facts of a compile-eligible row from the gate: its
 * code-section digest and size (a zero baseline binds the empty payload). */
static inline void bq_retirement_document_code(BqRetirementObservedSide const* side, char digest[65], uint64_t* bytes)
{
    Sha256 empty;
    sha256_init(&empty);
    if (side->code_sha256[0]) memcpy(digest, side->code_sha256, 65);
    else sha256_finish_hex(&empty, digest);
    *bytes = side->code_sha256[0] ? side->code_bytes : 0;
}

/* One oracle record (workflow.records.oracle), from the gate's baseline code
 * facts and the row's runtime obligation. */
static inline void bq_retirement_document_oracle_item(BqRetirementDocumentWriter* writer,
    BqRetirementDocumentInputs const* inputs, uint32_t row)
{
    BqRetirementDocumentRow const* entry = &inputs->population->rows[row];
    char code[65];
    uint64_t bytes = 0;
    bq_retirement_document_code(&inputs->gate->facts[row].side[0], code, &bytes);
    bq_retirement_document_text(writer, "{\"code_section_bytes\":");
    if (entry->compile) bq_retirement_document_number(writer, bytes);
    else bq_retirement_document_text(writer, "null");
    bq_retirement_document_key(writer, "code_section_sha256", 0);
    bq_retirement_document_digest(writer, code, entry->compile);
    bq_retirement_document_key(writer, "code_section_status", 0);
    bq_retirement_document_cstring(writer, entry->compile ? "parsed-deterministic" : "not-applicable");
    bq_retirement_document_key(writer, "native_runtime", 0);
    bq_retirement_document_text(writer, entry->runtime ? "true" : "false");
    bq_retirement_document_key(writer, "row", 0);
    bq_retirement_document_number(writer, row);
    bq_retirement_document_key(writer, "runtime_exit_code", 0);
    bq_retirement_document_text(writer, entry->runtime ? "0" : entry->compile ? "-1" : "null");
    bq_retirement_document_key(writer, "runtime_oracle_status", 0);
    bq_retirement_document_cstring(writer, entry->runtime ? "passed-native" : "not-applicable");
    bq_retirement_document_text(writer, "}");
}

/* workflow.records.oracle: every row's oracle record, bound to the census
 * manifest and rows. */
static inline int bq_retirement_documents_oracle(FILE* stream, BqRetirementDocumentInputs const* inputs,
    BqRetirementDocumentDescriptor* descriptor)
{
    BqRetirementDocumentWriter writer;
    bq_retirement_document_begin(&writer, stream);
    writer.ok = inputs && inputs->gate && inputs->population && descriptor;
    bq_retirement_document_text(&writer, "{\"records\":[");
    for (uint32_t row = 0; writer.ok && row < inputs->population->count; ++row)
    {
        if (row) bq_retirement_document_raw(&writer, ",", 1);
        bq_retirement_document_oracle_item(&writer, inputs, row);
    }
    bq_retirement_document_text(&writer, "],\"schema\":\"" BQ_RETIREMENT_DOCUMENT_ORACLE_SCHEMA "\"");
    bq_retirement_document_key(&writer, "source_manifest_sha256", 0);
    bq_retirement_document_digest(&writer, inputs ? inputs->manifest_sha256 : NULL, 1);
    bq_retirement_document_key(&writer, "source_rows_sha256", 0);
    bq_retirement_document_digest(&writer, inputs ? inputs->rows_sha256 : NULL, 1);
    bq_retirement_document_text(&writer, ",\"version\":1}");
    int ok = descriptor && bq_retirement_document_end(&writer, descriptor->sha256);
    if (descriptor) descriptor->bytes = ok ? writer.bytes : 0;
    return ok;
}

/* The response-file digest of a group's frozen inputs (_input_list_bytes):
 * one quoted, escaped line per fixture. */
static inline void bq_retirement_document_input_list(Sha256* hash, char const* fixture, size_t length)
{
    sha256_add(hash, "\"", 1);
    for (size_t index = 0; index < length; ++index)
    {
        if (fixture[index] == '"' || fixture[index] == '\\') sha256_add(hash, "\\", 1);
        sha256_add(hash, fixture + index, 1);
    }
    sha256_add(hash, "\"\n", 2);
}

/* One group contract of plan v3 (timed when `batch` is the frozen gate batch
 * group or the singleton's row commands; untimed from its production
 * batches). commands[side] and exits[side] are the variants' command digests
 * and exit statuses; contract the object group's frozen contract. */
static inline void bq_retirement_document_group(BqRetirementDocumentWriter* writer,
    BqRetirementDocumentInputs const* inputs, BqRetirementDocumentPartition const* partition, uint32_t group,
    TpRetirementBatchContract const* contract, char const* const commands[2], unsigned const exits[2])
{
    BqRetirementDocumentPopulation const* population = inputs->population;
    uint32_t leader = partition->rows[partition->first[group]];
    int object = partition->object[group] != 0;
    static char const* const sides[2] = {"baseline", "candidate"};
    bq_retirement_document_text(writer, "{");
    for (unsigned side = 0; side < 2; ++side)
    {
        bq_retirement_document_key(writer, sides[side], !side);
        bq_retirement_document_text(writer, "{\"command_sha256\":");
        bq_retirement_document_digest(writer, commands[side], 1);
        bq_retirement_document_key(writer, "exit_status", 0);
        bq_retirement_document_number(writer, exits[side]);
        bq_retirement_document_text(writer, "}");
    }
    bq_retirement_document_text(writer, ",\"configuration\":{");
    static unsigned const configuration[3] = {BQ_RETIREMENT_DOCUMENT_PIC, BQ_RETIREMENT_DOCUMENT_ALLOCATOR,
        BQ_RETIREMENT_DOCUMENT_FRONTEND};
    static char const* const configuration_names[3] = {"PIC", "allocator", "frontend_lowering"};
    for (unsigned index = 0; index < 3; ++index)
    {
        bq_retirement_document_key(writer, configuration_names[index], !index);
        String8 value = bq_retirement_document_value(population, leader, configuration[index]);
        bq_retirement_document_string(writer, (char const*)value.pointer, (size_t)value.length);
    }
    bq_retirement_document_text(writer, "},\"controls\":[");
    /* Object members are the contract's first inputs in row order (the plan
     * requires it); later inputs are status-checked controls. */
    uint32_t members = partition->first[group + 1] - partition->first[group];
    Sha256 listing;
    sha256_init(&listing);
    /* The listing is the members' identity fixtures (each the contract's
     * input fixture), then the controls' fixtures. */
    for (uint32_t member = 0; object && contract && member < members && member < contract->input_count; ++member)
    {
        String8 fixture = bq_retirement_document_value(population, partition->rows[partition->first[group] + member],
                                                       BQ_RETIREMENT_DOCUMENT_FIXTURE);
        TpRetirementBatchInput const* item = &contract->inputs[member];
        writer->ok = writer->ok && item->fixture && strlen(item->fixture) == fixture.length &&
            !memcmp(item->fixture, fixture.pointer, (size_t)fixture.length);
        bq_retirement_document_input_list(&listing, (char const*)fixture.pointer, (size_t)fixture.length);
    }
    unsigned controls = 0;
    for (unsigned input = 0; object && contract && input < contract->input_count; ++input)
    {
        TpRetirementBatchInput const* item = &contract->inputs[input];
        writer->ok = writer->ok && item->fixture && item->status && item->error;
        if (!writer->ok || item->member) continue;
        bq_retirement_document_input_list(&listing, item->fixture, strlen(item->fixture));
        if (controls++) bq_retirement_document_raw(writer, ",", 1);
        bq_retirement_document_text(writer, "{\"diagnostic_sha256\":");
        bq_retirement_document_digest(writer, item->diagnostic_sha256, 1);
        bq_retirement_document_key(writer, "error", 0);
        bq_retirement_document_cstring(writer, item->error);
        bq_retirement_document_key(writer, "fixture", 0);
        bq_retirement_document_cstring(writer, item->fixture);
        bq_retirement_document_key(writer, "object_sha256", 0);
        bq_retirement_document_digest(writer, item->object_sha256, !strcmp(item->status, "ok"));
        bq_retirement_document_key(writer, "row", 0);
        if (item->row == TP_RETIREMENT_BATCH_NO_ROW) bq_retirement_document_text(writer, "null");
        else bq_retirement_document_number(writer, item->row);
        bq_retirement_document_key(writer, "status", 0);
        bq_retirement_document_cstring(writer, item->status);
        bq_retirement_document_text(writer, "}");
    }
    writer->ok = writer->ok && (!object || (contract && contract->input_count == members + controls));
    bq_retirement_document_text(writer, "],\"group\":");
    bq_retirement_document_number(writer, group);
    char listed[65];
    sha256_finish_hex(&listing, listed);
    bq_retirement_document_key(writer, "input_list_sha256", 0);
    bq_retirement_document_digest(writer, listed, object);
    bq_retirement_document_key(writer, "kind", 0);
    bq_retirement_document_cstring(writer, object ? "object-batch" : "singleton-stage");
    bq_retirement_document_text(writer, ",\"members\":[");
    for (uint32_t member = 0; member < members; ++member)
    {
        uint32_t row = partition->rows[partition->first[group] + member];
        TpRetirementBatchInput const* item = object && contract && member < contract->input_count ?
            &contract->inputs[member] : NULL;
        writer->ok = writer->ok && (!object || (item && item->member && item->row == row));
        if (member) bq_retirement_document_raw(writer, ",", 1);
        bq_retirement_document_text(writer, "{\"diagnostic_sha256\":");
        bq_retirement_document_digest(writer, item ? item->diagnostic_sha256 : NULL, object);
        bq_retirement_document_key(writer, "row", 0);
        bq_retirement_document_number(writer, row);
        bq_retirement_document_text(writer, "}");
    }
    bq_retirement_document_text(writer, "],\"metrics_bytes_max\":");
    if (object && contract) bq_retirement_document_number(writer, contract->metrics_bytes_max);
    else bq_retirement_document_text(writer, "null");
    bq_retirement_document_text(writer, ",\"recipe\":{");
    static unsigned const recipe[3] = {BQ_RETIREMENT_DOCUMENT_CPU, BQ_RETIREMENT_DOCUMENT_CPU_FEATURES,
        BQ_RETIREMENT_DOCUMENT_RECIPE};
    static char const* const recipe_names[3] = {"cpu", "cpu_features", "fixture_recipe"};
    for (unsigned index = 0; index < 3; ++index)
    {
        bq_retirement_document_key(writer, recipe_names[index], !index);
        String8 value = bq_retirement_document_value(population, leader, recipe[index]);
        bq_retirement_document_string(writer, (char const*)value.pointer, (size_t)value.length);
    }
    bq_retirement_document_text(writer, "},\"target\":");
    String8 target = bq_retirement_document_value(population, leader, BQ_RETIREMENT_DOCUMENT_TARGET);
    bq_retirement_document_string(writer, (char const*)target.pointer, (size_t)target.length);
    bq_retirement_document_text(writer, "}");
}

/* The gate's frozen batch group of timed object group `group` (the n-th
 * object group), which must hold exactly the derived members. */
static inline BqRetirementBatchGroup const* bq_retirement_document_frozen(BqRetirementCorrectness const* gate,
    BqRetirementDocumentPartition const* timed, uint32_t group)
{
    uint32_t ordinal = 0;
    for (uint32_t index = 0; index < group; ++index) ordinal += timed->object[index];
    BqRetirementBatchGroup const* frozen = timed->object[group] && ordinal < gate->batch_group_count ?
        &gate->batch_groups[ordinal] : NULL;
    return frozen;
}

/* The untimed production batch of untimed group `group` and variant. */
static inline TpRetirementUntimedBatch const* bq_retirement_document_untimed_batch(
    BqRetirementDocumentInputs const* inputs, uint32_t group, unsigned variant)
{
    TpRetirementUntimedBatch const* found = NULL;
    for (unsigned index = 0; !found && inputs->untimed && index < inputs->untimed_count; ++index)
    {
        TpRetirementUntimedBatch const* batch = &inputs->untimed[index];
        if (batch->group == group && batch->variant == variant && batch->purpose == TP_RETIREMENT_UNTIMED_PRODUCTION)
            found = batch;
    }
    return found;
}

/* The observed untimed code row of `row`, or NULL. */
static inline TpRetirementCodeRow const* bq_retirement_document_code_row(BqRetirementDocumentInputs const* inputs,
    uint32_t row)
{
    TpRetirementCodeRow const* found = NULL;
    for (unsigned index = 0; !found && inputs->codes && index < inputs->code_count; ++index)
        if (inputs->codes[index].row == row) found = &inputs->codes[index];
    return found;
}

/* workflow.execution_plan, plan v3: the frozen schedule and pins, the
 * reviewed budget record, every timed and untimed group contract of the
 * derived partitions (joined to the gate's frozen batch groups, the rows'
 * commands and the untimed batches), and every row's contract joined to its
 * sealed identity and its oracle record. */
static inline int bq_retirement_documents_execution_plan(FILE* stream, BqRetirementDocumentInputs const* inputs,
    BqRetirementDocumentDescriptor* descriptor)
{
    BqRetirementDocumentWriter writer;
    bq_retirement_document_begin(&writer, stream);
    char budget[4096];
    size_t budget_bytes = inputs && inputs->budget ? tp_retirement_budget_encode(inputs->budget, budget, sizeof(budget)) : 0;
    char budget_sha256[65] = {0};
    writer.ok = inputs && inputs->gate && inputs->population && inputs->timed && inputs->untimed_partition &&
        inputs->plan && inputs->cpu >= 0 && budget_bytes && budget_bytes < sizeof(budget) &&
        tp_retirement_budget_digest(inputs->budget, budget_sha256) && descriptor &&
        inputs->population->count == inputs->gate->prepared.rows;
    BqRetirementCorrectness const* gate = writer.ok ? inputs->gate : NULL;
    bq_retirement_document_text(&writer, "{\"campaign_budget\":{\"record\":");
    if (writer.ok) bq_retirement_document_string(&writer, budget, budget_bytes);
    bq_retirement_document_key(&writer, "sha256", 0);
    bq_retirement_document_digest(&writer, budget_sha256, 1);
    bq_retirement_document_text(&writer, "},\"cpu\":");
    bq_retirement_document_number(&writer, writer.ok ? (uint64_t)inputs->cpu : 0);
    bq_retirement_document_text(&writer, ",\"groups\":[");
    /* Timed group contracts: object groups from the gate's frozen batch
     * groups, singletons from their row's frozen commands. */
    BqRetirementDocumentPartition const* timed = writer.ok ? inputs->timed : NULL;
    writer.ok = writer.ok && timed->object_groups == gate->batch_group_count;
    for (uint32_t group = 0; writer.ok && group < timed->count; ++group)
    {
        uint32_t leader = timed->rows[timed->first[group]];
        BqRetirementBatchGroup const* frozen = bq_retirement_document_frozen(gate, timed, group);
        char const* commands[2] = {gate->trusted_rows[leader].compiler_command_sha256[0],
                                   gate->trusted_rows[leader].compiler_command_sha256[1]};
        unsigned exits[2] = {0, 0};
        writer.ok = !timed->object[group] || frozen;
        for (unsigned side = 0; writer.ok && frozen && side < 2; ++side)
        {
            commands[side] = frozen->command_sha256[side];
            exits[side] = frozen->contract[side].exit_status;
        }
        if (group) bq_retirement_document_raw(&writer, ",", 1);
        bq_retirement_document_group(&writer, inputs, timed, group, frozen ? &frozen->contract[0] : NULL, commands,
                                     exits);
    }
    bq_retirement_document_text(&writer, "],\"native_target\":\"" BQ_RETIREMENT_DOCUMENT_TIMED_TARGET "\"");
    bq_retirement_document_key(&writer, "pairs_per_round", 0);
    bq_retirement_document_number(&writer, writer.ok ? inputs->plan->pairs_per_round : 0);
    bq_retirement_document_key(&writer, "performance_rows_sha256", 0);
    bq_retirement_document_digest(&writer, writer.ok ? inputs->population->performance_rows_sha256 : NULL, 1);
    bq_retirement_document_key(&writer, "rounds", 0);
    bq_retirement_document_number(&writer, TP_RETIREMENT_ROUNDS);
    bq_retirement_document_text(&writer, ",\"rows\":[");
    for (uint32_t row = 0; writer.ok && row < inputs->population->count; ++row)
    {
        BqRetirementDocumentRow const* entry = &inputs->population->rows[row];
        int is_timed = bq_retirement_document_timed(inputs->population, row);
        uint32_t group = UINT32_MAX;
        for (uint32_t index = 0; is_timed && index < timed->count && group == UINT32_MAX; ++index)
            for (uint32_t member = timed->first[index]; member < timed->first[index + 1]; ++member)
                if (timed->rows[member] == row) group = index;
        writer.ok = !is_timed || group != UINT32_MAX;
        BqRetirementBatchGroup const* frozen = is_timed ? bq_retirement_document_frozen(gate, timed, group) : NULL;
        TpRetirementCodeRow const* code = !is_timed && entry->compile ? bq_retirement_document_code_row(inputs, row) : NULL;
        writer.ok = writer.ok && (is_timed || !entry->compile || code);
        BqRetirementDocumentWriter oracle;
        bq_retirement_document_begin(&oracle, NULL);
        bq_retirement_document_oracle_item(&oracle, inputs, row);
        char oracle_sha256[65];
        writer.ok = writer.ok && bq_retirement_document_end(&oracle, oracle_sha256);
        if (row) bq_retirement_document_raw(&writer, ",", 1);
        bq_retirement_document_text(&writer, "{");
        static char const* const sides[2] = {"baseline", "candidate"};
        for (unsigned side = 0; writer.ok && side < 2; ++side)
        {
            BqRetirementObservedSide const* fact = &gate->facts[row].side[side];
            char code_sha256[65];
            uint64_t code_bytes = 0;
            bq_retirement_document_code(fact, code_sha256, &code_bytes);
            char const* command = frozen ? frozen->command_sha256[side] : gate->trusted_rows[row].compiler_command_sha256[side];
            bq_retirement_document_key(&writer, sides[side], !side);
            bq_retirement_document_text(&writer, "{\"artifact_sha256\":");
            bq_retirement_document_digest(&writer, fact->artifact_sha256, entry->compile);
            bq_retirement_document_key(&writer, "code_section_bytes", 0);
            if (entry->compile) bq_retirement_document_number(&writer, code_bytes);
            else bq_retirement_document_text(&writer, "null");
            bq_retirement_document_key(&writer, "code_section_sha256", 0);
            bq_retirement_document_digest(&writer, code_sha256, entry->compile);
            bq_retirement_document_key(&writer, "compiler_command_sha256", 0);
            bq_retirement_document_digest(&writer, command, is_timed);
            bq_retirement_document_key(&writer, "reproduction_sha256", 0);
            bq_retirement_document_digest(&writer, code ? code->sides[side].reproduction_sha256 : NULL, code != NULL);
            bq_retirement_document_key(&writer, "runtime_command_sha256", 0);
            bq_retirement_document_digest(&writer, fact->runtime_command_sha256, entry->runtime);
            bq_retirement_document_key(&writer, "runtime_output_sha256", 0);
            bq_retirement_document_digest(&writer, fact->runtime_output_sha256, entry->runtime);
            bq_retirement_document_text(&writer, "}");
            /* The observed untimed code is the gate's artifact. */
            writer.ok = writer.ok && (!code || !strcmp(code->sides[side].artifact_sha256, fact->artifact_sha256));
        }
        bq_retirement_document_key(&writer, "group", 0);
        if (is_timed) bq_retirement_document_number(&writer, group);
        else bq_retirement_document_text(&writer, "null");
        bq_retirement_document_key(&writer, "identity_sha256", 0);
        bq_retirement_document_digest(&writer, gate->trusted_rows[row].identity_sha256, 1);
        bq_retirement_document_key(&writer, "oracle_sha256", 0);
        bq_retirement_document_digest(&writer, oracle_sha256, 1);
        bq_retirement_document_key(&writer, "row", 0);
        bq_retirement_document_number(&writer, row);
        bq_retirement_document_text(&writer, "}");
    }
    bq_retirement_document_text(&writer, "],\"schedule\":\"" BQ_RETIREMENT_DOCUMENT_SCHEDULE "\",\"schema\":\""
        BQ_RETIREMENT_DOCUMENT_EXECUTION_PLAN_SCHEMA "\",\"seed\":");
    bq_retirement_document_number(&writer, writer.ok ? inputs->plan->seed : 0);
    bq_retirement_document_text(&writer, ",\"untimed_groups\":[");
    BqRetirementDocumentPartition const* untimed = writer.ok ? inputs->untimed_partition : NULL;
    writer.ok = writer.ok && inputs->untimed_count == 4u * untimed->count;
    for (uint32_t group = 0; writer.ok && group < untimed->count; ++group)
    {
        TpRetirementUntimedBatch const* batches[2] = {bq_retirement_document_untimed_batch(inputs, group, 0),
                                                     bq_retirement_document_untimed_batch(inputs, group, 1)};
        writer.ok = batches[0] && batches[1] &&
            (untimed->object[group] ? batches[0]->group_kind == TP_RETIREMENT_GROUP_OBJECT :
             batches[0]->group_kind == TP_RETIREMENT_GROUP_SINGLETON && inputs->untimed_rows &&
             inputs->untimed_rows[group] == untimed->rows[untimed->first[group]]);
        char const* commands[2] = {writer.ok ? batches[0]->command.command_sha256 : NULL,
                                   writer.ok ? batches[1]->command.command_sha256 : NULL};
        unsigned exits[2] = {writer.ok ? (unsigned)batches[0]->command.exit_status : 0,
                             writer.ok ? (unsigned)batches[1]->command.exit_status : 0};
        if (group) bq_retirement_document_raw(&writer, ",", 1);
        if (writer.ok)
            bq_retirement_document_group(&writer, inputs, untimed, group, batches[0]->command.batch, commands, exits);
    }
    bq_retirement_document_text(&writer, "],\"version\":1,\"warmups_per_variant\":");
    bq_retirement_document_number(&writer, TP_RETIREMENT_WARMUPS);
    bq_retirement_document_text(&writer, "}");
    int ok = descriptor && bq_retirement_document_end(&writer, descriptor->sha256);
    if (descriptor) descriptor->bytes = ok ? writer.bytes : 0;
    return ok;
}

/* workflow.records.result_input_plan (v3): the rows population (timed rows)
 * and the batches population (object batch groups), each split into the
 * canonical minimal contiguous cap-sized partitions, named
 * `<population>-NNNN` at `retirement-result-<population>-NNNN.json`. The
 * partitions are returned for lane E's composer (manifests[0] rows,
 * manifests[1] batches). */
static inline int bq_retirement_documents_result_input_plan(FILE* stream, BqRetirementDocumentInputs const* inputs,
    BqRetirementUnitCampaignPartition manifests[2][BQ_RETIREMENT_DOCUMENT_PARTITIONS], unsigned counts[2],
    BqRetirementDocumentDescriptor* descriptor)
{
    BqRetirementDocumentWriter writer;
    bq_retirement_document_begin(&writer, stream);
    writer.ok = inputs && inputs->population && inputs->timed && inputs->plan && inputs->gate && manifests && counts &&
        descriptor;
    uint64_t samples[2] = {0, 0};
    for (uint32_t row = 0; writer.ok && row < inputs->population->count; ++row)
        samples[0] += bq_retirement_document_timed(inputs->population, row) ? 1u : 0u;
    if (writer.ok) samples[1] = inputs->timed->object_groups;
    uint64_t per_unit = writer.ok ? (uint64_t)TP_RETIREMENT_ROUNDS * inputs->plan->pairs_per_round : 0;
    static char const* const names[2] = {"rows", "batches"};
    unsigned total = 0;
    for (unsigned population = 0; writer.ok && population < 2; ++population)
    {
        uint64_t required = samples[population] * per_unit;
        uint64_t parts = (required + BQ_RETIREMENT_DOCUMENT_MANIFEST_RECORDS - 1) / BQ_RETIREMENT_DOCUMENT_MANIFEST_RECORDS;
        writer.ok = samples[population] && parts && parts <= BQ_RETIREMENT_DOCUMENT_PARTITIONS;
        counts[population] = writer.ok ? (unsigned)parts : 0;
        total += counts[population];
        for (uint64_t part = 0; writer.ok && part < parts; ++part)
        {
            BqRetirementUnitCampaignPartition* manifest = &manifests[population][part];
            int named = snprintf(manifest->identity, sizeof(manifest->identity), "%s-%04u", names[population],
                                 (unsigned)part);
            int pathed = snprintf(manifest->path, sizeof(manifest->path), "retirement-result-%s-%04u.json",
                                  names[population], (unsigned)part);
            manifest->start = part * BQ_RETIREMENT_DOCUMENT_MANIFEST_RECORDS;
            manifest->records = part + 1 < parts ? BQ_RETIREMENT_DOCUMENT_MANIFEST_RECORDS :
                required - manifest->start;
            writer.ok = named > 0 && (size_t)named < sizeof(manifest->identity) && pathed > 0 &&
                (size_t)pathed < sizeof(manifest->path);
        }
    }
    writer.ok = writer.ok && total <= BQ_RETIREMENT_DOCUMENT_PARTITIONS;
    bq_retirement_document_text(&writer, "{\"identity_field\":\"record_id\",\"max_records_per_manifest\":");
    bq_retirement_document_number(&writer, BQ_RETIREMENT_DOCUMENT_MANIFEST_RECORDS);
    bq_retirement_document_key(&writer, "object_row_count", 0);
    bq_retirement_document_number(&writer, writer.ok ? inputs->gate->prepared.object_rows : 0);
    bq_retirement_document_key(&writer, "pairs_per_round", 0);
    bq_retirement_document_number(&writer, writer.ok ? inputs->plan->pairs_per_round : 0);
    static char const* const tokens[2][3] = {
        {"row-round-pair-v1", "authenticated-applicability-minus-nonexecuted-rows-on-native-host-target",
         "native-host-timed-rows-with-sampled-metrics"},
        {"group-round-pair-v1", "frozen-batch-groups-of-the-native-host-timed-projection",
         "native-host-object-batch-groups"}};
    bq_retirement_document_text(&writer, ",\"populations\":{");
    /* Populations in sorted key order: batches, then rows. */
    for (unsigned index = 0; writer.ok && index < 2; ++index)
    {
        unsigned population = 1 - index;
        bq_retirement_document_key(&writer, names[population], !index);
        bq_retirement_document_text(&writer, "{\"coordinate_schema\":");
        bq_retirement_document_cstring(&writer, tokens[population][0]);
        bq_retirement_document_key(&writer, "eligible_population", 0);
        bq_retirement_document_cstring(&writer, tokens[population][1]);
        bq_retirement_document_key(&writer, "manifest_count", 0);
        bq_retirement_document_number(&writer, counts[population]);
        bq_retirement_document_text(&writer, ",\"manifests\":[");
        for (unsigned part = 0; part < counts[population]; ++part)
        {
            BqRetirementUnitCampaignPartition const* manifest = &manifests[population][part];
            if (part) bq_retirement_document_raw(&writer, ",", 1);
            bq_retirement_document_text(&writer, "{\"identity\":");
            bq_retirement_document_cstring(&writer, manifest->identity);
            bq_retirement_document_key(&writer, "path", 0);
            bq_retirement_document_cstring(&writer, manifest->path);
            bq_retirement_document_key(&writer, "records", 0);
            bq_retirement_document_number(&writer, manifest->records);
            bq_retirement_document_key(&writer, "start_record", 0);
            bq_retirement_document_number(&writer, manifest->start);
            bq_retirement_document_text(&writer, "}");
        }
        bq_retirement_document_text(&writer, "],\"records_per_unit\":");
        bq_retirement_document_number(&writer, per_unit);
        bq_retirement_document_key(&writer, "required_records", 0);
        bq_retirement_document_number(&writer, samples[population] * per_unit);
        bq_retirement_document_key(&writer, "sample_count", 0);
        bq_retirement_document_number(&writer, samples[population]);
        bq_retirement_document_key(&writer, "sample_population", 0);
        bq_retirement_document_cstring(&writer, tokens[population][2]);
        bq_retirement_document_text(&writer, "}");
    }
    bq_retirement_document_text(&writer, "},\"predeclared\":true,\"rounds\":");
    bq_retirement_document_number(&writer, TP_RETIREMENT_ROUNDS);
    bq_retirement_document_text(&writer, ",\"schema\":\"" BQ_RETIREMENT_DOCUMENT_RESULT_INPUT_SCHEMA "\"");
    bq_retirement_document_key(&writer, "source_manifest_sha256", 0);
    bq_retirement_document_digest(&writer, inputs ? inputs->manifest_sha256 : NULL, 1);
    bq_retirement_document_key(&writer, "source_rows_sha256", 0);
    bq_retirement_document_digest(&writer, inputs ? inputs->rows_sha256 : NULL, 1);
    bq_retirement_document_text(&writer, ",\"timed_target\":\"" BQ_RETIREMENT_DOCUMENT_TIMED_TARGET "\",\"version\":1}");
    int ok = descriptor && bq_retirement_document_end(&writer, descriptor->sha256);
    if (descriptor) descriptor->bytes = ok ? writer.bytes : 0;
    return ok;
}

/* The two workflow phase documents: the pre-sample plan (post_aa NULL) and
 * the post-A/A binding, which adds the pre-sample document's digest and the
 * #437 A/A admission receipt's digest. Both bind the sources, the family,
 * the frozen sampling values, the result-input plan and the execution plan
 * descriptor (its path as published). */
typedef struct BqRetirementDocumentPhase
{
    BqRetirementDocumentFamily const* family;
    char const* result_input_plan_sha256;
    char const* execution_plan_path;
    BqRetirementDocumentDescriptor const* execution_plan;
    char const* pre_sample_plan_sha256;
    char const* aa_admission_sha256;
} BqRetirementDocumentPhase;

static inline int bq_retirement_documents_phase(FILE* stream, BqRetirementDocumentInputs const* inputs,
    BqRetirementDocumentPhase const* phase, int post_aa, BqRetirementDocumentDescriptor* descriptor)
{
    BqRetirementDocumentWriter writer;
    bq_retirement_document_begin(&writer, stream);
    writer.ok = inputs && inputs->plan && phase && phase->family && phase->execution_plan &&
        phase->execution_plan_path && descriptor && (!post_aa || (phase->pre_sample_plan_sha256 &&
        phase->aa_admission_sha256));
    bq_retirement_document_text(&writer, "{");
    if (writer.ok && post_aa)
    {
        bq_retirement_document_key(&writer, "aa_admission_sha256", 1);
        bq_retirement_document_digest(&writer, phase->aa_admission_sha256, 1);
        bq_retirement_document_raw(&writer, ",", 1);
    }
    bq_retirement_document_text(&writer, "\"bootstrap_members_per_scope\":");
    bq_retirement_document_number(&writer, writer.ok ? phase->family->bootstrap_members : 0);
    bq_retirement_document_key(&writer, "cell_members_per_scope", 0);
    bq_retirement_document_number(&writer, writer.ok ? phase->family->cell_members : 0);
    bq_retirement_document_text(&writer, ",\"execution_plan\":{\"bytes\":");
    bq_retirement_document_number(&writer, writer.ok ? phase->execution_plan->bytes : 0);
    bq_retirement_document_key(&writer, "path", 0);
    bq_retirement_document_cstring(&writer, writer.ok ? phase->execution_plan_path : NULL);
    bq_retirement_document_key(&writer, "sha256", 0);
    bq_retirement_document_digest(&writer, writer.ok ? phase->execution_plan->sha256 : NULL, 1);
    bq_retirement_document_text(&writer, "}");
    bq_retirement_document_key(&writer, "family_sha256", 0);
    bq_retirement_document_digest(&writer, writer.ok ? phase->family->sha256 : NULL, 1);
    bq_retirement_document_key(&writer, "manifest_sha256", 0);
    bq_retirement_document_digest(&writer, writer.ok ? inputs->manifest_sha256 : NULL, 1);
    bq_retirement_document_key(&writer, "pairs_per_round", 0);
    bq_retirement_document_number(&writer, writer.ok ? inputs->plan->pairs_per_round : 0);
    if (writer.ok && post_aa)
    {
        bq_retirement_document_key(&writer, "pre_sample_plan_sha256", 0);
        bq_retirement_document_digest(&writer, phase->pre_sample_plan_sha256, 1);
    }
    bq_retirement_document_key(&writer, "resamples", 0);
    bq_retirement_document_number(&writer, writer.ok ? inputs->plan->resamples : 0);
    bq_retirement_document_key(&writer, "result_input_plan_sha256", 0);
    bq_retirement_document_digest(&writer, writer.ok ? phase->result_input_plan_sha256 : NULL, 1);
    bq_retirement_document_key(&writer, "rounds", 0);
    bq_retirement_document_number(&writer, TP_RETIREMENT_ROUNDS);
    bq_retirement_document_key(&writer, "rows_sha256", 0);
    bq_retirement_document_digest(&writer, writer.ok ? inputs->rows_sha256 : NULL, 1);
    bq_retirement_document_key(&writer, "schema", 0);
    bq_retirement_document_cstring(&writer, post_aa ? BQ_RETIREMENT_DOCUMENT_POST_AA_SCHEMA :
                                                      BQ_RETIREMENT_DOCUMENT_PRE_SAMPLE_SCHEMA);
    bq_retirement_document_key(&writer, "seed", 0);
    bq_retirement_document_number(&writer, writer.ok ? inputs->plan->seed : 0);
    bq_retirement_document_key(&writer, "status", 0);
    bq_retirement_document_cstring(&writer, post_aa ? "bound-after-aa-before-samples" : "frozen-before-samples");
    bq_retirement_document_key(&writer, "support_declaration_sha256", 0);
    bq_retirement_document_digest(&writer, writer.ok ? inputs->support_sha256 : NULL, 1);
    bq_retirement_document_text(&writer, ",\"version\":1}");
    int ok = descriptor && bq_retirement_document_end(&writer, descriptor->sha256);
    if (descriptor) descriptor->bytes = ok ? writer.bytes : 0;
    return ok;
}

/* A population row's canonical identity digest (the validator's
 * _canonical_json_digest of the row's identity object, keys sorted), which
 * the gate sealed as the row's identity_sha256. */
static inline int bq_retirement_document_identity(BqRetirementDocumentPopulation const* population, uint32_t row,
    char digest[65])
{
    /* ROW_IDENTITY_FIELDS in sorted key order. */
    static unsigned const order[BQ_RETIREMENT_DOCUMENT_FIELDS] = {BQ_RETIREMENT_DOCUMENT_PIC,
        BQ_RETIREMENT_DOCUMENT_ALLOCATOR, BQ_RETIREMENT_DOCUMENT_ARGV, BQ_RETIREMENT_DOCUMENT_STAGE,
        BQ_RETIREMENT_DOCUMENT_COMPILE, BQ_RETIREMENT_DOCUMENT_CPU, BQ_RETIREMENT_DOCUMENT_CPU_FEATURES,
        BQ_RETIREMENT_DOCUMENT_DIAGNOSTIC, BQ_RETIREMENT_DOCUMENT_EXECUTION, BQ_RETIREMENT_DOCUMENT_FIXTURE,
        BQ_RETIREMENT_DOCUMENT_RECIPE, BQ_RETIREMENT_DOCUMENT_FRONTEND, BQ_RETIREMENT_DOCUMENT_LINK,
        BQ_RETIREMENT_DOCUMENT_TARGET, BQ_RETIREMENT_DOCUMENT_TARGET_ABI};
    static char const* const names[BQ_RETIREMENT_DOCUMENT_FIELDS] = {"PIC", "allocator", "argv_evidence",
        "artifact_stage", "compile_obligation", "cpu", "cpu_features", "diagnostic_obligation",
        "execution_obligation", "fixture", "fixture_recipe", "frontend_lowering", "link_obligation", "target",
        "target_abi"};
    BqRetirementDocumentWriter writer;
    bq_retirement_document_begin(&writer, NULL);
    bq_retirement_document_text(&writer, "{");
    for (unsigned index = 0; index < BQ_RETIREMENT_DOCUMENT_FIELDS; ++index)
    {
        bq_retirement_document_key(&writer, names[index], !index);
        String8 value = bq_retirement_document_value(population, row, order[index]);
        bq_retirement_document_string(&writer, (char const*)value.pointer, (size_t)value.length);
    }
    bq_retirement_document_text(&writer, "}");
    int ok = bq_retirement_document_end(&writer, digest);
    return ok;
}

/* What the driver's document steps take besides its own state: the
 * evidence root the documents are written into (their paths are
 * bq_retirement_unit_campaign_document_paths), the pinned performance rows
 * (bq_retirement_documents_population) and the recipe profile, whose
 * support declaration, census manifest, census rows and performance rows
 * pins the documents name. */
typedef struct BqRetirementUnitCampaignDocumentSources
{
    int directory;
    BqRetirementDocumentPopulation const* population;
    String8 profile;
} BqRetirementUnitCampaignDocumentSources;

/* The four source pins of the recipe profile, each a digest. */
static inline int bq_retirement_unit_campaign_document_pins(String8 profile, char support[65], char manifest[65],
    char rows[65], char performance[65])
{
    static char const* const keys[4] = {"support-declaration-sha256=", "census-manifest-sha256=",
        "census-rows-sha256=", "performance-rows-sha256="};
    char* outputs[4] = {support, manifest, rows, performance};
    int ok = 1;
    for (unsigned index = 0; ok && index < 4; ++index)
    {
        String8 value = {0};
        ok = bq_retirement_unit_campaign_profile_value(profile, keys[index], &value) && value.length == 64 &&
            bq_retirement_unit_campaign_hex(value);
        if (ok)
        {
            memcpy(outputs[index], value.pointer, 64);
            outputs[index][64] = 0;
        }
    }
    return ok;
}

/* A new document at `index`'s path in the evidence root, never replacing
 * one. */
static inline FILE* bq_retirement_unit_campaign_document_open(int directory, unsigned index)
{
    int file = openat(directory, bq_retirement_unit_campaign_document_paths[index],
                      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    FILE* stream = file >= 0 ? fdopen(file, "wb") : NULL;
    if (!stream && file >= 0) close(file);
    return stream;
}

/* Flushed and synced before it counts as written. */
static inline int bq_retirement_unit_campaign_document_close(FILE* stream, int written)
{
    int ok = stream && written && fflush(stream) == 0 && fsync(fileno(stream)) == 0;
    if (stream && fclose(stream) != 0) ok = 0;
    return ok;
}

/* The derived timed partition is the gate's: its rows are exactly the gate's
 * timed rows, every row's identity is the one the gate sealed and its
 * runtime flag the gate's, and its groups are the campaign's: each object
 * group's members are exactly the member inputs of both frozen contracts of
 * its gate batch group, in order, and each other group is one row. */
static inline int bq_retirement_unit_campaign_document_layout(BqRetirementCorrectness const* gate,
    TpRetirementCampaign const* campaign, BqRetirementDocumentPopulation const* population,
    BqRetirementDocumentPartition const* timed)
{
    int ok = population->count == gate->prepared.rows && timed->count == campaign->groups &&
        timed->object_groups == gate->batch_group_count;
    for (uint32_t row = 0; ok && row < population->count; ++row)
    {
        char identity[65];
        int is_timed = bq_retirement_document_timed(population, row);
        ok = is_timed == bq_retirement_unit_campaign_timed_row(gate, row) &&
            (population->rows[row].compile != 0) == (gate->trusted_rows[row].compiler_eligible != 0) &&
            (!is_timed || (population->rows[row].runtime != 0) == (gate->facts[row].runtime_eligible != 0)) &&
            bq_retirement_document_identity(population, row, identity) &&
            !strcmp(identity, gate->trusted_rows[row].identity_sha256);
    }
    for (uint32_t group = 0; ok && group < timed->count; ++group)
    {
        uint32_t first = timed->first[group], members = timed->first[group + 1] - first;
        BqRetirementBatchGroup const* frozen = bq_retirement_document_frozen(gate, timed, group);
        ok = timed->object[group] ? frozen != NULL : members == 1;
        for (unsigned side = 0; ok && frozen && side < 2; ++side)
        {
            TpRetirementBatchContract const* contract = &frozen->contract[side];
            uint32_t matched = 0;
            for (unsigned input = 0; ok && input < contract->input_count; ++input)
            {
                if (!contract->inputs[input].member) continue;
                ok = matched < members && contract->inputs[input].row == timed->rows[first + matched];
                matched += 1;
            }
            ok = ok && matched == members;
        }
    }
    return ok;
}

/* The timed-row layout digest from the pinned rows: each timed row, in
 * ascending id order, with its partition group, runtime flag and six
 * dimension values (bq_retirement_unit_campaign_timed_line). */
static inline int bq_retirement_unit_campaign_document_timed_rows(BqRetirementDocumentPopulation const* population,
    BqRetirementDocumentPartition const* timed, char digest[65])
{
    uint32_t* group_of = population->count ? calloc(population->count, sizeof(*group_of)) : NULL;
    int ok = group_of != NULL;
    for (uint32_t group = 0; ok && group < timed->count; ++group)
        for (uint32_t member = timed->first[group]; member < timed->first[group + 1]; ++member)
            group_of[timed->rows[member]] = group;
    Sha256 hash;
    sha256_init(&hash);
    static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_TIMED_DOMAIN;
    sha256_add(&hash, domain, sizeof(domain) - 1);
    uint32_t rows = 0;
    for (uint32_t row = 0; ok && row < population->count; ++row)
    {
        if (!bq_retirement_document_timed(population, row)) continue;
        char const* values[6];
        size_t lengths[6];
        for (unsigned dimension = 0; dimension < 6; ++dimension)
        {
            String8 value = bq_retirement_document_value(population, row, bq_retirement_document_dimensions[dimension]);
            values[dimension] = (char const*)value.pointer;
            lengths[dimension] = (size_t)value.length;
        }
        bq_retirement_unit_campaign_timed_line(&hash, row, group_of[row], population->rows[row].runtime ? 1u : 0u,
                                               values, lengths);
        rows += 1;
    }
    bq_retirement_unit_campaign_number(&hash, "rows", rows);
    if (ok) sha256_finish_hex(&hash, digest);
    else digest[0] = 0;
    free(group_of);
    return ok;
}

/* Before A/A: the oracle records, the execution plan, the result-input plan
 * and the pre-sample plan, written in that order into the evidence root and
 * recorded with the partitions, the family and source pin digests and the
 * timed-row layout digest (which the lane E handoff reproduces). The
 * partitions and family are derived here from the pinned rows (whose every
 * identity must be the gate's sealed one) and must be the bound campaign's:
 * its groups, the gate's frozen batch groups and the plan's two family
 * counts. The untimed groups, batches, rows and code facts are the ones the
 * untimed step ran and observed, and the budget the reviewed one the
 * campaign binds. A failure poisons the attempt; a document already written
 * stays as evidence, and none is ever replaced. */
static inline int bq_retirement_unit_campaign_documents(BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignDocumentSources const* sources)
{
    TpRetirementCampaign const* campaign = driver && driver->binding ? driver->binding->campaign : NULL;
    BqRetirementCorrectness const* gate = driver ? driver->gate : NULL;
    BqRetirementDocumentPopulation const* population = sources ? sources->population : NULL;
    BqRetirementDocumentPartition timed = {0}, untimed = {0};
    BqRetirementDocumentFamily family = {0};
    char support[65] = {0}, manifest[65] = {0}, rows[65] = {0}, performance[65] = {0}, budget[65] = {0};
    char timed_rows[65] = {0};
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND && !driver->documented && campaign && gate &&
        population && population->count && sources->directory >= 0 && driver->budget && driver->untimed_batches &&
        bq_retirement_unit_campaign_document_pins(sources->profile, support, manifest, rows, performance) &&
        !strcmp(performance, population->performance_rows_sha256) &&
        tp_retirement_budget_digest(driver->budget, budget) && !strcmp(budget, campaign->budget_sha256) &&
        bq_retirement_documents_partition(population, 0, &timed) &&
        bq_retirement_documents_partition(population, 1, &untimed) &&
        bq_retirement_documents_family(population, &timed, &family) &&
        family.bootstrap_members == driver->plan.bootstrap_members_per_scope &&
        family.cell_members == driver->plan.cell_members_per_scope &&
        bq_retirement_unit_campaign_document_layout(gate, campaign, population, &timed) &&
        bq_retirement_unit_campaign_document_timed_rows(population, &timed, timed_rows) &&
        driver->untimed_batch_count == 4u * untimed.count &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns);
    BqRetirementDocumentInputs inputs = {gate, population, &timed, &untimed, driver ? driver->budget : NULL,
        driver ? &driver->plan : NULL, driver ? driver->untimed_batches : NULL,
        driver ? driver->untimed_batch_count : 0, driver ? driver->untimed_rows : NULL, driver ? driver->codes : NULL,
        driver ? driver->code_count : 0, campaign ? campaign->cpu : -1, support, manifest, rows};
    BqRetirementDocumentDescriptor descriptors[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA];
    BqRetirementUnitCampaignPartition partitions[2][BQ_RETIREMENT_UNIT_CAMPAIGN_PARTITIONS];
    unsigned counts[2] = {0, 0};
    memset(descriptors, 0, sizeof(descriptors));
    memset(partitions, 0, sizeof(partitions));
    for (unsigned index = 0; ok && index < BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA; ++index)
    {
        FILE* stream = bq_retirement_unit_campaign_document_open(sources->directory, index);
        BqRetirementDocumentPhase phase = {&family, descriptors[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256,
            bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN],
            &descriptors[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN], NULL, NULL};
        int written = stream != NULL;
        if (written && index == BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE)
            written = bq_retirement_documents_oracle(stream, &inputs, &descriptors[index]);
        else if (written && index == BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN)
            written = bq_retirement_documents_execution_plan(stream, &inputs, &descriptors[index]);
        else if (written && index == BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN)
            written = bq_retirement_documents_result_input_plan(stream, &inputs, partitions, counts, &descriptors[index]);
        else if (written)
            written = bq_retirement_documents_phase(stream, &inputs, &phase, 0, &descriptors[index]);
        ok = bq_retirement_unit_campaign_document_close(stream, written);
    }
    if (ok)
    {
        memcpy(driver->documents, descriptors, sizeof(descriptors));
        memcpy(driver->partitions, partitions, sizeof(partitions));
        memcpy(driver->partition_counts, counts, sizeof(counts));
        memcpy(driver->family_sha256, family.sha256, 65);
        memcpy(driver->source_rows_sha256, rows, 65);
        memcpy(driver->support_sha256, support, 65);
        memcpy(driver->manifest_sha256, manifest, 65);
        memcpy(driver->timed_rows_sha256, timed_rows, 65);
        driver->documented = BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA;
    }
    else bq_retirement_unit_campaign_fail(driver);
    bq_retirement_documents_partition_release(&timed);
    bq_retirement_documents_partition_release(&untimed);
    return ok;
}

/* After the A/A admission, before the A/B freeze: the post-A/A binding over
 * the same sources, family, sampling values, result-input plan and
 * execution plan as the pre-sample plan, which it names, and over the
 * admission receipt the admission step recorded. The profile's support
 * declaration, census manifest and census rows pins must be the ones the
 * pre-sample plan named. */
static inline int bq_retirement_unit_campaign_post_aa_document(BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignDocumentSources const* sources)
{
    char support[65] = {0}, manifest[65] = {0}, rows[65] = {0}, performance[65] = {0};
    int ok = driver && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_ADMITTED &&
        driver->documented == BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA && sources && sources->directory >= 0 &&
        bq_retirement_unit_campaign_document_pins(sources->profile, support, manifest, rows, performance) &&
        !strcmp(rows, driver->source_rows_sha256) && !strcmp(support, driver->support_sha256) &&
        !strcmp(manifest, driver->manifest_sha256) && tp_retirement_digest(driver->aa_admission_sha256) &&
        bq_retirement_unit_campaign_live(driver->phases, driver->cancellation_fd, driver->deadline_ns);
    BqRetirementDocumentFamily family = {{0}, driver ? driver->plan.bootstrap_members_per_scope : 0,
        driver ? driver->plan.cell_members_per_scope : 0, 0};
    if (driver) memcpy(family.sha256, driver->family_sha256, 65);
    BqRetirementDocumentInputs inputs = {0};
    inputs.plan = driver ? &driver->plan : NULL;
    inputs.support_sha256 = support;
    inputs.manifest_sha256 = manifest;
    inputs.rows_sha256 = rows;
    BqRetirementDocumentPhase phase = {&family,
        driver ? driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256 : NULL,
        bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN],
        driver ? &driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN] : NULL,
        driver ? driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256 : NULL,
        driver ? driver->aa_admission_sha256 : NULL};
    BqRetirementDocumentDescriptor descriptor = {0};
    FILE* stream = ok ?
        bq_retirement_unit_campaign_document_open(sources->directory, BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA) : NULL;
    int written = stream && bq_retirement_documents_phase(stream, &inputs, &phase, 1, &descriptor);
    ok = ok && bq_retirement_unit_campaign_document_close(stream, written);
    if (ok)
    {
        driver->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA] = descriptor;
        driver->documented = BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS;
    }
    else bq_retirement_unit_campaign_fail(driver);
    return ok;
}
#endif
#endif
