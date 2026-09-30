/* Production writer and checker of the reviewed A1 campaign budget (#881,
 * review item M4): `retirement-records budget-encode|budget-preflight` in the
 * service binary (retirement_records.c forwards to tp_retirement_budget_cli).
 * It is not part of throughput.c: that file's include closure is the trusted
 * #619 adapter the binding validator compiles and pins.
 *
 * Ownership: the reviewed-input and frozen-count text formats and the rules
 * a budget must meet before it is pinned. The record itself, its canonical
 * encoding, strict decoder and derivation stay in retirement_budget.h; the
 * enforced unit limit is systemd_runtime.h's (the coordinator's
 * bq_worker_retirement_runtime applies the same floor and range). Nothing here
 * chooses a bound: every nanosecond value comes from the reviewed input, and
 * the production values still need 9700X measurements (#422).
 *
 * Entry points:
 *   tp_retirement_budget_input_parse   the reviewed input (explicit scalars
 *                                      and tables) into a record
 *   tp_retirement_budget_counts_encode / _parse  the frozen counts
 *                                      (`tp-retirement-budget-counts-v1`),
 *                                      also written by the service's
 *                                      `retirement-records budget-counts`
 *   tp_retirement_budget_review        every rule, with a diagnostic
 *   tp_retirement_budget_cli           encode and preflight; each writes its
 *                                      output only after every rule passed
 *
 * Map: TpRetirementBudgetText (whole-file reads), TpRetirementBudgetOwnedCounts,
 * tp_retirement_budget_fixed_ns, tp_retirement_budget_report.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_BUDGET_TOOL_H
#define BUSTER_THROUGHPUT_RETIREMENT_BUDGET_TOOL_H
#include "retirement_budget.h"
#include "../bench_service/systemd_runtime.h"
#include <stdarg.h>

#define TP_RETIREMENT_BUDGET_INPUT_SCHEMA "tp-retirement-campaign-budget-input-v1"
#define TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "tp-retirement-budget-counts-v1"
/* A reviewed input may carry `#` provenance comments; the counts hold at most
 * TP_RETIREMENT_MAX_CELLS groups per list. */
#define TP_RETIREMENT_BUDGET_INPUT_BYTES (64u * 1024u)
#define TP_RETIREMENT_BUDGET_COUNTS_BYTES (8u * 1024u * 1024u)
#define TP_RETIREMENT_BUDGET_DIAGNOSTIC 256u
#define TP_RETIREMENT_BUDGET_NS_PER_SECOND UINT64_C(1000000000)

static inline void tp_retirement_budget_say(char* diagnostic, char const* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(diagnostic, TP_RETIREMENT_BUDGET_DIAGNOSTIC, format, arguments);
    va_end(arguments);
}

/* A whole regular file read into a NUL-terminated heap copy. */
typedef struct TpRetirementBudgetText
{
    char* bytes;
    size_t size;
} TpRetirementBudgetText;

static inline int tp_retirement_budget_read(char const* path, size_t cap, TpRetirementBudgetText* text)
{
    FILE* file = path ? fopen(path, "rb") : NULL;
    *text = (TpRetirementBudgetText){0};
    text->bytes = file ? (char*)malloc(cap + 1) : NULL;
    int ok = text->bytes != NULL;
    size_t size = ok ? fread(text->bytes, 1, cap + 1, file) : 0;
    ok = ok && !ferror(file) && size && size <= cap;
    if (file && fclose(file) != 0) ok = 0;
    if (ok)
    {
        text->bytes[size] = 0;
        text->size = size;
    }
    else
    {
        free(text->bytes);
        *text = (TpRetirementBudgetText){0};
    }
    return ok;
}

/* ------------------------------------------------------------ frozen counts */

/* Parsed counts; the three per-group arrays of both lists share `storage`. */
typedef struct TpRetirementBudgetOwnedCounts
{
    TpRetirementBudgetCounts counts;
    unsigned* storage;
} TpRetirementBudgetOwnedCounts;

static inline void tp_retirement_budget_counts_release(TpRetirementBudgetOwnedCounts* owned)
{
    if (owned)
    {
        free(owned->storage);
        *owned = (TpRetirementBudgetOwnedCounts){0};
    }
}

/* Canonical ASCII: `schema=`, `pairs=`, `runtime-rows=`, `timed-groups=<n>`
 * and one `timed=<stage> <inputs>` per timed group in campaign order, then
 * `untimed-groups=<n>` and one `untimed=<stage> <inputs>` per untimed group in
 * the validator's untimed-partition order. An object group's inputs are its
 * members and controls; a link or self-host singleton has one. Writes at most
 * capacity bytes into output (which may be NULL) and returns the full size,
 * 0 for counts no group kind, stage and size pairing allows. */
typedef struct TpRetirementBudgetWriter
{
    char* output;
    size_t capacity, size;
    int ok;
} TpRetirementBudgetWriter;

static inline void tp_retirement_budget_line(TpRetirementBudgetWriter* writer, char const* format, ...)
{
    char line[128];
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    writer->ok = writer->ok && written > 0 && (size_t)written < sizeof(line);
    if (writer->ok && writer->output && writer->size + (size_t)written <= writer->capacity)
        memcpy(writer->output + writer->size, line, (size_t)written);
    if (writer->ok) writer->size += (size_t)written;
}

static inline size_t tp_retirement_budget_counts_encode(TpRetirementBudgetCounts const* counts, char* output,
    size_t capacity)
{
    TpRetirementBudgetWriter writer = {output, capacity, 0, counts != NULL};
    if (writer.ok)
        tp_retirement_budget_line(&writer, "schema=%s\npairs=%u\nruntime-rows=%u\n", TP_RETIREMENT_BUDGET_COUNTS_SCHEMA,
                                  counts->pairs, counts->runtime_rows);
    for (unsigned list = 0; writer.ok && list < 2; ++list)
    {
        TpRetirementBudgetGroups const* groups = list ? &counts->untimed : &counts->timed;
        char const* name = list ? "untimed" : "timed";
        writer.ok = groups->count <= TP_RETIREMENT_MAX_CELLS &&
            (!groups->count || (groups->inputs && groups->kinds && groups->stages));
        tp_retirement_budget_line(&writer, "%s-groups=%u\n", name, groups->count);
        for (unsigned g = 0; writer.ok && g < groups->count; ++g)
        {
            unsigned stage = groups->stages[g], object = groups->kinds[g] == TP_RETIREMENT_GROUP_OBJECT;
            writer.ok = stage < TP_RETIREMENT_BUDGET_STAGE_COUNT && groups->inputs[g] &&
                groups->inputs[g] <= TP_RETIREMENT_BATCH_INPUTS &&
                (object ? stage == TP_RETIREMENT_BUDGET_STAGE_OBJECT :
                 groups->kinds[g] == TP_RETIREMENT_GROUP_SINGLETON && stage != TP_RETIREMENT_BUDGET_STAGE_OBJECT &&
                 groups->inputs[g] == 1);
            if (writer.ok)
                tp_retirement_budget_line(&writer, "%s=%s %u\n", name, tp_retirement_budget_stage_names[stage],
                                          groups->inputs[g]);
        }
    }
    return writer.ok ? writer.size : 0;
}

/* One `<key><n>` line's number at *offset (canonical decimal, LF-ended). */
static inline int tp_retirement_budget_counts_number(char const* text, size_t size, size_t* offset, char const* key,
    uint64_t maximum, unsigned* value)
{
    size_t key_length = strlen(key), end = *offset;
    while (end < size && text[end] != '\n') ++end;
    uint64_t number = 0;
    int ok = end < size && end - *offset > key_length && !memcmp(text + *offset, key, key_length) &&
        tp_retirement_budget_number(text + *offset + key_length, end - *offset - key_length, &number) &&
        number <= maximum;
    *value = ok ? (unsigned)number : 0;
    if (ok) *offset = end + 1;
    return ok;
}

/* Strict decoder: the parsed counts must re-encode to exactly the input. */
static inline int tp_retirement_budget_counts_parse(char const* text, size_t size, TpRetirementBudgetOwnedCounts* owned)
{
    size_t offset = 0;
    unsigned timed = 0, untimed = 0, pairs = 0, runtime = 0;
    static char const schema[] = "schema=" TP_RETIREMENT_BUDGET_COUNTS_SCHEMA "\n";
    *owned = (TpRetirementBudgetOwnedCounts){0};
    int ok = text && size > sizeof(schema) - 1 && size <= TP_RETIREMENT_BUDGET_COUNTS_BYTES &&
        !memcmp(text, schema, sizeof(schema) - 1);
    offset = sizeof(schema) - 1;
    ok = ok && tp_retirement_budget_counts_number(text, size, &offset, "pairs=", UINT32_MAX, &pairs) &&
        tp_retirement_budget_counts_number(text, size, &offset, "runtime-rows=", TP_RETIREMENT_MAX_CELLS, &runtime) &&
        tp_retirement_budget_counts_number(text, size, &offset, "timed-groups=", TP_RETIREMENT_MAX_CELLS, &timed);
    /* Each group line is at least 10 bytes, which bounds the allocation by
     * the input before the untimed count is read. */
    uint64_t slots = (uint64_t)timed + (size - offset) / 10u + 1u;
    owned->storage = ok ? (unsigned*)calloc((size_t)slots * 3u, sizeof(unsigned)) : NULL;
    ok = ok && owned->storage;
    unsigned* inputs = owned->storage;
    unsigned* kinds = inputs ? inputs + slots : NULL;
    unsigned* stages = kinds ? kinds + slots : NULL;
    for (unsigned list = 0, used = 0; ok && list < 2; ++list)
    {
        unsigned count = timed;
        if (list) ok = tp_retirement_budget_counts_number(text, size, &offset, "untimed-groups=", TP_RETIREMENT_MAX_CELLS,
                                                          &untimed) && timed + untimed <= slots;
        if (list) count = untimed;
        char const* key = list ? "untimed=" : "timed=";
        size_t key_length = strlen(key);
        for (unsigned g = 0; ok && g < count; ++g)
        {
            size_t end = offset, space = offset + key_length;
            while (end < size && text[end] != '\n') ++end;
            while (space < end && text[space] != ' ') ++space;
            unsigned stage = TP_RETIREMENT_BUDGET_STAGE_COUNT;
            for (unsigned s = 0; s < TP_RETIREMENT_BUDGET_STAGE_COUNT; ++s)
                if (strlen(tp_retirement_budget_stage_names[s]) == space - offset - key_length &&
                    !memcmp(text + offset + key_length, tp_retirement_budget_stage_names[s], space - offset - key_length))
                    stage = s;
            uint64_t number = 0;
            ok = end < size && space < end && !memcmp(text + offset, key, key_length) &&
                stage < TP_RETIREMENT_BUDGET_STAGE_COUNT &&
                tp_retirement_budget_number(text + space + 1, end - space - 1, &number) &&
                number <= TP_RETIREMENT_BATCH_INPUTS;
            inputs[used] = (unsigned)number;
            kinds[used] = stage == TP_RETIREMENT_BUDGET_STAGE_OBJECT ? TP_RETIREMENT_GROUP_OBJECT :
                TP_RETIREMENT_GROUP_SINGLETON;
            stages[used] = stage;
            used += ok;
            offset = end + 1;
        }
    }
    if (ok)
    {
        owned->counts = (TpRetirementBudgetCounts){{inputs, kinds, stages, timed},
            {inputs + timed, kinds + timed, stages + timed, untimed}, runtime, pairs};
    }
    size_t canonical = ok && offset == size ? tp_retirement_budget_counts_encode(&owned->counts, NULL, 0) : 0;
    char* bytes = canonical == size ? (char*)malloc(canonical) : NULL;
    ok = bytes && tp_retirement_budget_counts_encode(&owned->counts, bytes, canonical) == size &&
        !memcmp(bytes, text, size);
    free(bytes);
    if (!ok) tp_retirement_budget_counts_release(owned);
    return ok;
}

/* ------------------------------------------------------------ reviewed input */

/* The reviewed input: `schema=tp-retirement-campaign-budget-input-v1` first,
 * then, in any order, each of the 16 scalar keys exactly once and the table
 * lines (`batch=<max_inputs>:<ns>` classes in ascending order,
 * `singleton=<stage>:<ns>` once per named non-object stage, and the same
 * lines prefixed `untimed-`), with blank lines and `#` comments (provenance:
 * which measurement or policy each value is) anywhere after the schema. The
 * record is exactly those values; the comments never reach it. */
static inline int tp_retirement_budget_input_parse(char const* text, size_t size, TpRetirementCampaignBudget* budget,
    char* diagnostic)
{
    TpRetirementCampaignBudget parsed = {0};
    uint64_t values[TP_RETIREMENT_BUDGET_SCALARS] = {0};
    unsigned seen[TP_RETIREMENT_BUDGET_SCALARS] = {0};
    static char const schema[] = "schema=" TP_RETIREMENT_BUDGET_INPUT_SCHEMA;
    size_t offset = 0;
    unsigned line = 0;
    int ok = text && size && size <= TP_RETIREMENT_BUDGET_INPUT_BYTES && memchr(text, 0, size) == NULL;
    if (!ok) tp_retirement_budget_say(diagnostic, "input is empty, too large or holds a NUL byte");
    while (ok && offset < size)
    {
        size_t end = offset;
        while (end < size && text[end] != '\n') ++end;
        char const* at = text + offset;
        size_t length = end - offset;
        ++line;
        ok = end < size;
        if (!ok) tp_retirement_budget_say(diagnostic, "line %u: not LF-terminated", line);
        else if (line == 1)
        {
            ok = length == sizeof(schema) - 1 && !memcmp(at, schema, length);
            if (!ok) tp_retirement_budget_say(diagnostic, "line 1: expected %s", schema);
        }
        else if (length && at[0] != '#')
        {
            int untimed = length > 8 && !memcmp(at, "untimed-", 8);
            size_t skip = untimed ? 8u : 0u;
            unsigned key = TP_RETIREMENT_BUDGET_SCALARS;
            for (unsigned i = 0; i < TP_RETIREMENT_BUDGET_SCALARS; ++i)
            {
                size_t key_length = strlen(tp_retirement_budget_keys[i]);
                if (!untimed && length > key_length && !memcmp(at, tp_retirement_budget_keys[i], key_length) &&
                    at[key_length] == '=')
                    key = i;
            }
            if (key < TP_RETIREMENT_BUDGET_SCALARS)
            {
                size_t key_length = strlen(tp_retirement_budget_keys[key]);
                ok = !seen[key] && tp_retirement_budget_number(at + key_length + 1, length - key_length - 1, &values[key]);
                seen[key] = 1;
                if (!ok) tp_retirement_budget_say(diagnostic, "line %u: %s repeated or not a canonical decimal", line,
                                                  tp_retirement_budget_keys[key]);
            }
            else
            {
                TpRetirementBudgetTable* table = untimed ? &parsed.untimed : &parsed.timed;
                TpRetirementBudgetTable one = {0};
                ok = tp_retirement_budget_table_line(at + skip, length - skip, &one);
                for (unsigned stage = 0; ok && stage < TP_RETIREMENT_BUDGET_STAGE_CAP; ++stage)
                {
                    ok = !(one.singleton_ns[stage] && table->singleton_ns[stage]);
                    if (one.singleton_ns[stage]) table->singleton_ns[stage] = one.singleton_ns[stage];
                }
                ok = ok && (!one.classes || table->classes < TP_RETIREMENT_BUDGET_CLASSES);
                if (ok && one.classes) table->batch[table->classes++] = one.batch[0];
                if (!ok)
                    tp_retirement_budget_say(diagnostic, "line %u: unknown key, malformed table line, repeated singleton "
                                             "or more than %u classes", line, TP_RETIREMENT_BUDGET_CLASSES);
            }
        }
        offset = end + 1;
    }
    for (unsigned i = 0; ok && i < TP_RETIREMENT_BUDGET_SCALARS; ++i)
    {
        ok = seen[i];
        if (!ok) tp_retirement_budget_say(diagnostic, "missing %s", tp_retirement_budget_keys[i]);
    }
    if (ok)
    {
        parsed.reviewed_ns = values[0];
        parsed.reservation_ns = values[1];
        parsed.materialization_ns = values[2];
        parsed.baseline_build_ns = values[3];
        parsed.candidate_build_ns = values[4];
        parsed.correctness_ns = values[5];
        parsed.settling_per_stage_ns = values[6];
        parsed.aa_qualification_ns = values[7];
        parsed.aa_receipt_sealing_ns = values[8];
        parsed.sample_export_per_stage_ns = values[9];
        parsed.final_statistics_ns = values[10];
        parsed.final_sealing_ns = values[11];
        parsed.cleanup_ns = values[12];
        parsed.runtime_process_ns = values[13];
        parsed.metrics_header_bytes = values[14];
        parsed.metrics_input_bytes = values[15];
    }
    *budget = ok ? parsed : (TpRetirementCampaignBudget){0};
    return ok;
}

/* ------------------------------------------------------------------ rules */

/* The fixed term of the derivation (both collection stages' settling and
 * export), as tp_retirement_budget_preflight and the coordinator sum it. */
static inline int tp_retirement_budget_fixed_ns(TpRetirementCampaignBudget const* budget, uint64_t* fixed)
{
    uint64_t sum = 0, settling = 0, export_ns = 0;
    int ok = tp_retirement_budget_mul(budget->settling_per_stage_ns, TP_RETIREMENT_BUDGET_STAGES, &settling) &&
        tp_retirement_budget_mul(budget->sample_export_per_stage_ns, TP_RETIREMENT_BUDGET_STAGES, &export_ns);
    uint64_t const parts[] = {budget->reservation_ns, budget->materialization_ns, budget->baseline_build_ns,
        budget->candidate_build_ns, budget->correctness_ns, settling, budget->aa_qualification_ns,
        budget->aa_receipt_sealing_ns, export_ns, budget->final_statistics_ns, budget->final_sealing_ns,
        budget->cleanup_ns};
    for (unsigned i = 0; ok && i < BUSTER_ARRAY_LENGTH(parts); ++i) ok = tp_retirement_budget_add(sum, parts[i], &sum);
    *fixed = ok ? sum : 0;
    return ok;
}

static inline int tp_retirement_budget_table_review(TpRetirementBudgetTable const* table, char const* name,
    char* diagnostic)
{
    int ok = table->classes >= 1 && table->classes <= TP_RETIREMENT_BUDGET_CLASSES;
    if (!ok) tp_retirement_budget_say(diagnostic, "%s table needs 1 to %u batch classes", name, TP_RETIREMENT_BUDGET_CLASSES);
    for (unsigned i = 0; ok && i < table->classes; ++i)
    {
        TpRetirementBudgetClass const* entry = &table->batch[i];
        TpRetirementBudgetClass const* previous = i ? &table->batch[i - 1] : NULL;
        if (!(entry->max_inputs >= 1 && entry->max_inputs <= TP_RETIREMENT_BATCH_INPUTS && entry->batch_ns))
            tp_retirement_budget_say(diagnostic, "%s class %u: max_inputs must be 1..%u and its bound nonzero",
                                     name, i, TP_RETIREMENT_BATCH_INPUTS);
        else if (previous && entry->max_inputs <= previous->max_inputs)
            tp_retirement_budget_say(diagnostic, "%s class %u: classes must ascend by max_inputs", name, i);
        else if (previous && entry->batch_ns < previous->batch_ns)
            tp_retirement_budget_say(diagnostic, "%s class %u: a bound may not decrease with the group size", name, i);
        ok = entry->max_inputs >= 1 && entry->max_inputs <= TP_RETIREMENT_BATCH_INPUTS && entry->batch_ns &&
            (!previous || (entry->max_inputs > previous->max_inputs && entry->batch_ns >= previous->batch_ns));
    }
    for (unsigned stage = TP_RETIREMENT_BUDGET_STAGE_OBJECT + 1; ok && stage < TP_RETIREMENT_BUDGET_STAGE_COUNT; ++stage)
    {
        ok = table->singleton_ns[stage] != 0;
        if (!ok) tp_retirement_budget_say(diagnostic, "%s table misses singleton=%s", name,
                                          tp_retirement_budget_stage_names[stage]);
    }
    return ok;
}

/* The object groups of one list against its table: each must have a class
 * (so the largest one is covered) and a metrics bound within the cap. */
static inline int tp_retirement_budget_groups_review(TpRetirementCampaignBudget const* budget,
    TpRetirementBudgetGroups const* groups, int untimed, char* diagnostic)
{
    int ok = 1;
    for (unsigned g = 0; ok && g < groups->count; ++g)
    {
        uint64_t ns = 0, bytes = 0;
        int object = groups->kinds[g] == TP_RETIREMENT_GROUP_OBJECT;
        ok = tp_retirement_budget_group_ns(budget, untimed, groups->kinds[g], groups->stages[g], groups->inputs[g], &ns);
        if (!ok) tp_retirement_budget_say(diagnostic, "%s group %u (%s, %u inputs) is not covered by the %s table",
                                          untimed ? "untimed" : "timed", g,
                                          tp_retirement_budget_stage_names[groups->stages[g] %
                                                                           TP_RETIREMENT_BUDGET_STAGE_COUNT],
                                          groups->inputs[g], untimed ? "untimed" : "timed");
        ok = ok && (!object || tp_retirement_budget_metrics_bytes(budget, groups->inputs[g], &bytes));
        if (!ok && object && ns)
            tp_retirement_budget_say(diagnostic, "%s group %u: %u inputs exceed the metrics artifact cap",
                                     untimed ? "untimed" : "timed", g, groups->inputs[g]);
    }
    return ok;
}

/* Every rule a pinned budget must meet, first failure described: the record's
 * own validity, the reviewed ceiling whose whole-second floor must lie in the
 * broker's retirement range and hold every fixed-phase bound, every counted
 * object group covered by a class and the metrics cap, and the derivation's
 * required time within the ceiling (*preflight, zeroed on failure). */
static inline int tp_retirement_budget_review(TpRetirementCampaignBudget const* budget,
    TpRetirementBudgetCounts const* counts, TpRetirementBudgetPreflight* preflight, char* diagnostic)
{
    uint64_t scalars[TP_RETIREMENT_BUDGET_SCALARS];
    tp_retirement_budget_scalars(budget, scalars);
    int ok = 1;
    for (unsigned i = 0; ok && i < TP_RETIREMENT_BUDGET_SCALARS; ++i)
    {
        ok = scalars[i] != 0;
        if (!ok) tp_retirement_budget_say(diagnostic, "%s must be nonzero", tp_retirement_budget_keys[i]);
    }
    ok = ok && tp_retirement_budget_table_review(&budget->timed, "timed", diagnostic) &&
        tp_retirement_budget_table_review(&budget->untimed, "untimed", diagnostic);
    if (ok && !tp_retirement_budget_valid(budget))
    {
        ok = 0;
        tp_retirement_budget_say(diagnostic, "metrics-header-bytes plus one metrics-input-bytes exceed %" PRIu64,
                                 (uint64_t)TP_RETIREMENT_METRICS_ARTIFACT_BYTES);
    }
    uint64_t usec = budget->reviewed_ns / TP_RETIREMENT_BUDGET_NS_PER_SECOND * BQ_SYSTEMD_USEC_PER_SECOND;
    if (ok && !bq_systemd_retirement_runtime_valid(usec))
    {
        ok = 0;
        tp_retirement_budget_say(diagnostic, "reviewed-ns floors to %" PRIu64 " s, outside [%" PRIu64 ", %" PRIu64 "] s",
                                 usec / BQ_SYSTEMD_USEC_PER_SECOND,
                                 BQ_SYSTEMD_RETIREMENT_RUNTIME_MIN_USEC / BQ_SYSTEMD_USEC_PER_SECOND,
                                 BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC / BQ_SYSTEMD_USEC_PER_SECOND);
    }
    uint64_t fixed = 0;
    if (ok && !(tp_retirement_budget_fixed_ns(budget, &fixed) && fixed <= usec * UINT64_C(1000)))
    {
        ok = 0;
        tp_retirement_budget_say(diagnostic, "the fixed phases (%" PRIu64 " ns) exceed the enforced whole-second "
                                 "ceiling (%" PRIu64 " ns)", fixed, usec * UINT64_C(1000));
    }
    ok = ok && tp_retirement_budget_groups_review(budget, &counts->timed, 0, diagnostic) &&
        tp_retirement_budget_groups_review(budget, &counts->untimed, 1, diagnostic);
    TpRetirementBudgetPreflight result = {0};
    if (ok && !tp_retirement_budget_preflight(budget, counts, &result))
    {
        /* The same derivation with an unbounded ceiling names the shortfall. */
        TpRetirementCampaignBudget unbounded = *budget;
        unbounded.reviewed_ns = UINT64_MAX;
        TpRetirementBudgetPreflight required = {0};
        ok = 0;
        if (tp_retirement_budget_preflight(&unbounded, counts, &required))
            tp_retirement_budget_say(diagnostic, "the derivation requires %" PRIu64 " ns, above reviewed-ns %" PRIu64,
                                     required.required_ns, budget->reviewed_ns);
        else tp_retirement_budget_say(diagnostic, "the counts are outside the derivation (no timed group, more "
                                      "runtime rows than timed groups, an odd or out-of-range pair count, or overflow)");
    }
    *preflight = ok ? result : (TpRetirementBudgetPreflight){0};
    return ok;
}

static inline int tp_retirement_budget_report(FILE* output, char const digest[65],
    TpRetirementBudgetPreflight const* result)
{
    int written = fprintf(output, "budget-sha256=%s\nfits=%d\nrequired-ns=%" PRIu64 "\nremaining-ns=%" PRIu64
                          "\nfixed-ns=%" PRIu64 "\ncompiler-ns=%" PRIu64 "\ncompiler-object-ns=%" PRIu64
                          "\ncompiler-singleton-ns=%" PRIu64 "\nruntime-ns=%" PRIu64 "\nuntimed-ns=%" PRIu64
                          "\nuntimed-object-ns=%" PRIu64 "\nuntimed-singleton-ns=%" PRIu64 "\ncompiler-batches=%" PRIu64
                          "\nruntime-processes=%" PRIu64 "\nuntimed-batches=%" PRIu64 "\n",
                          digest, result->fits, result->required_ns, result->remaining_ns, result->fixed_ns,
                          result->compiler_ns, result->compiler_object_ns, result->compiler_singleton_ns,
                          result->runtime_ns, result->untimed_ns, result->untimed_object_ns,
                          result->untimed_singleton_ns, result->compiler_batches, result->runtime_processes,
                          result->untimed_batches);
    return written > 0;
}

/* ------------------------------------------------------------------- CLI */

/* `encode INPUT COUNTS`: the canonical record on output, only when every
 * rule holds against the frozen counts. `preflight BUDGET COUNTS`: strict
 * decode of an existing record, the same rules and the derivation's terms.
 * 0 on success, 2 with one diagnostic line and nothing on output otherwise. */
static inline int tp_retirement_budget_cli(int argc, char** argv, FILE* output, FILE* diagnostics)
{
    char diagnostic[TP_RETIREMENT_BUDGET_DIAGNOSTIC] = "usage: encode REVIEWED_INPUT COUNTS | preflight BUDGET COUNTS";
    int encode = argc == 3 && !strcmp(argv[0], "encode");
    int ok = encode || (argc == 3 && !strcmp(argv[0], "preflight"));
    TpRetirementBudgetText record = {0}, counts_text = {0};
    TpRetirementBudgetOwnedCounts counts = {0};
    TpRetirementCampaignBudget budget = {0};
    TpRetirementBudgetPreflight result = {0};
    char canonical[TP_RETIREMENT_BUDGET_BYTES], digest[65] = {0};
    size_t size = 0;
    if (ok && !tp_retirement_budget_read(argv[1], encode ? TP_RETIREMENT_BUDGET_INPUT_BYTES :
                                         TP_RETIREMENT_BUDGET_BYTES - 1u, &record))
    {
        ok = 0;
        tp_retirement_budget_say(diagnostic, "cannot read %s (missing, empty or too large)", argv[1]);
    }
    if (ok && !(tp_retirement_budget_read(argv[2], TP_RETIREMENT_BUDGET_COUNTS_BYTES, &counts_text) &&
                tp_retirement_budget_counts_parse(counts_text.bytes, counts_text.size, &counts)))
    {
        ok = 0;
        tp_retirement_budget_say(diagnostic, "%s is not canonical %s", argv[2], TP_RETIREMENT_BUDGET_COUNTS_SCHEMA);
    }
    if (ok && encode) ok = tp_retirement_budget_input_parse(record.bytes, record.size, &budget, diagnostic);
    if (ok && !encode && !tp_retirement_budget_decode(record.bytes, record.size, &budget))
    {
        ok = 0;
        tp_retirement_budget_say(diagnostic, "%s is not a canonical %s record", argv[1], TP_RETIREMENT_BUDGET_SCHEMA);
    }
    ok = ok && tp_retirement_budget_review(&budget, &counts.counts, &result, diagnostic);
    if (ok)
    {
        size = tp_retirement_budget_encode(&budget, canonical, sizeof(canonical));
        TpRetirementCampaignBudget again = {0};
        ok = size && tp_retirement_budget_decode(canonical, size, &again) && tp_retirement_budget_digest(&budget, digest);
        if (!ok) tp_retirement_budget_say(diagnostic, "the record does not encode canonically");
    }
    int checked = ok;
    if (ok && encode) ok = fwrite(canonical, 1, size, output) == size;
    else if (ok) ok = tp_retirement_budget_report(output, digest, &result);
    ok = fflush(output) == 0 && ok;
    if (checked && !ok) tp_retirement_budget_say(diagnostic, "cannot write the output");
    if (!ok) fprintf(diagnostics, "retirement-budget: %s\n", diagnostic);
    free(record.bytes);
    free(counts_text.bytes);
    tp_retirement_budget_counts_release(&counts);
    return ok ? 0 : 2;
}
#endif
