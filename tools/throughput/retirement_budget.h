/* Reviewed A1 campaign budget for #881 (review item M4).
 * Amendment A1 replaces the fixed one-hour worker budget with a reviewed
 * budget bound into the admitted service recipe. This record holds every input
 * of that budget: the reviewed whole-job ceiling, the authenticated fixed-phase
 * bounds, measured upper bounds per compiler process keyed by group kind and
 * artifact stage (object batches by input count; link and self-host
 * singletons by stage, never as a one-input batch), separate untimed bounds
 * measured on the slowest untimed target, a measured upper bound per runtime
 * process, the reviewed metrics-artifact byte bound per input, and (v3, #426
 * plan step 6 / #1021) a measured upper bound on the coordinator's
 * AA_MEASURED re-read per MiB of A/A sample shard: that re-read (every A/A
 * sample shard read and SHA-256 hashed between A/A and A/B) costs the
 * worst-case shard bytes the frozen counts allow, times that rate. Its
 * canonical text encoding names the
 * derivation formula, so the recipe/profile pin (`campaign-budget-sha256=`)
 * binds inputs and derivation together; the blocked profile carries no pin.
 * tp_retirement_budget_preflight derives the required time from the frozen
 * counts and rejects a job the reviewed ceiling cannot hold, before timing.
 * The numeric bounds are integration-time pins; nothing here admits a recipe.
 *
 * Map: TpRetirementCampaignBudget, tp_retirement_budget_valid,
 * tp_retirement_budget_group_ns, tp_retirement_budget_metrics_bytes,
 * tp_retirement_budget_encode/_decode/_digest, TpRetirementBudgetCounts,
 * tp_retirement_budget_attestation, tp_retirement_budget_preflight.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_BUDGET_H
#define BUSTER_THROUGHPUT_RETIREMENT_BUDGET_H
#include "retirement_samples.h"

#define TP_RETIREMENT_BUDGET_SCHEMA "tp-retirement-campaign-budget-v3"
/* The derivation the pinned record commits to, per collection stage s (A/A,
 * A/B), per_unit = 2 * (warmups + rounds * pairs):
 * fixed + stages * (settling + export) + sum over timed groups of
 * stages * per_unit * timed(kind, stage, inputs) + runtime_rows * stages *
 * per_unit * runtime + sum over untimed groups of 2 variants * 2 purposes *
 * untimed(kind, stage, inputs). An object group costs the first batch class
 * whose max_inputs >= n; a link or self-host singleton costs its stage's own
 * bound, never a one-input batch. The untimed tables are separate bounds, each
 * measured as the maximum over every untimed target (the slowest target), and
 * never the native timed bounds. (v3) The A/A attestation adds
 * ceil(R * P * (330 * sum_g(n_g) + 266 * O) * attest / 2^20): every A/A
 * sample line is at most TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX (a row record;
 * the timed rows are at most the timed groups' inputs) or
 * TP_RETIREMENT_BATCH_RECORD_BYTES_MAX (a batch record, one per object group
 * O), R * P lines per unit, each byte read and hashed once at `attest`
 * nanoseconds per MiB. */
#define TP_RETIREMENT_BUDGET_DERIVATION \
    "fixed+stages*(settling+export)+sum_g(stages*2*(W+R*P)*timed(kind_g,stage_g,n_g))" \
    "+U*stages*2*(W+R*P)*runtime+sum_u(4*untimed(kind_u,stage_u,n_u))" \
    "+ceil(R*P*(330*sum_g(n_g)+266*O)*attest/2^20);" \
    "object:first batch class with max_inputs>=n;singleton:its stage bound,never a one-input batch;" \
    "untimed:separate tables measured on the slowest untimed target;" \
    "attest:measured AA_MEASURED re-read ns per MiB of A/A sample shard,O the timed object groups"
/* The A/A attestation term's per-line bounds and unit (v3). */
#define TP_RETIREMENT_BUDGET_ATTEST_ROW_BYTES 330u
#define TP_RETIREMENT_BUDGET_ATTEST_BATCH_BYTES 266u
#define TP_RETIREMENT_BUDGET_ATTEST_MIB (UINT64_C(1) << 20)
BUSTER_CT_CHECK(TP_RETIREMENT_BUDGET_ATTEST_ROW_BYTES == TP_RETIREMENT_SAMPLE_RECORD_BYTES_MAX);
BUSTER_CT_CHECK(TP_RETIREMENT_BUDGET_ATTEST_BATCH_BYTES == TP_RETIREMENT_BATCH_RECORD_BYTES_MAX);
#define TP_RETIREMENT_BUDGET_STAGES 2u
#define TP_RETIREMENT_BUDGET_CLASSES 16u
#define TP_RETIREMENT_BUDGET_BYTES 4096u
/* Budget keys by artifact stage (the validator's STAGES order). A singleton
 * group has a non-object stage; an object group has the object stage. The
 * singleton tables have room for later stages, whose slots stay zero until a
 * named stage (and so a new schema and pin) is added. */
#define TP_RETIREMENT_BUDGET_STAGE_OBJECT 0u
#define TP_RETIREMENT_BUDGET_STAGE_LINK 1u
#define TP_RETIREMENT_BUDGET_STAGE_SELF_HOST 2u
#define TP_RETIREMENT_BUDGET_STAGE_COUNT 3u
#define TP_RETIREMENT_BUDGET_STAGE_CAP 8u
BUSTER_CT_CHECK(TP_RETIREMENT_BUDGET_STAGE_COUNT <= TP_RETIREMENT_BUDGET_STAGE_CAP);
static char const* const tp_retirement_budget_stage_names[TP_RETIREMENT_BUDGET_STAGE_COUNT] = {
    "object", "link", "self-host-stage1"};
/* Untimed code-artifact groups cost two variants times production and
 * reproduction batches. */
#define TP_RETIREMENT_BUDGET_UNTIMED_BATCHES 4u

typedef struct TpRetirementBudgetClass
{
    unsigned max_inputs;
    uint64_t batch_ns;
} TpRetirementBudgetClass;

/* One table of compiler-process bounds: object batch classes ascending by
 * max_inputs with nondecreasing bounds, and one singleton bound per named
 * non-object stage (singleton_ns[TP_RETIREMENT_BUDGET_STAGE_OBJECT] and every
 * unnamed slot stay zero). */
typedef struct TpRetirementBudgetTable
{
    unsigned classes;
    TpRetirementBudgetClass batch[TP_RETIREMENT_BUDGET_CLASSES];
    uint64_t singleton_ns[TP_RETIREMENT_BUDGET_STAGE_CAP];
} TpRetirementBudgetTable;

/* Every nanosecond bound includes launcher and collector work (outputs hashed,
 * metrics read and sharded, records written). `timed` holds the native-host
 * bounds of the timed batch groups; `untimed` the bounds of the untimed
 * code-artifact batches, each the maximum measured over every untimed target. */
typedef struct TpRetirementCampaignBudget
{
    uint64_t reviewed_ns;
    uint64_t reservation_ns, materialization_ns, baseline_build_ns, candidate_build_ns, correctness_ns;
    uint64_t settling_per_stage_ns, aa_qualification_ns, aa_receipt_sealing_ns, sample_export_per_stage_ns;
    uint64_t final_statistics_ns, final_sealing_ns, cleanup_ns;
    uint64_t runtime_process_ns;
    uint64_t metrics_header_bytes, metrics_input_bytes;
    /* (v3) The measured upper bound of the AA_MEASURED re-read, in ns per MiB
     * of A/A sample shard read and hashed. */
    uint64_t aa_attestation_ns_per_mib;
    TpRetirementBudgetTable timed, untimed;
} TpRetirementCampaignBudget;

static inline int tp_retirement_budget_add(uint64_t left, uint64_t right, uint64_t* sum)
{
    int ok = sum && right <= UINT64_MAX - left;
    if (sum) *sum = ok ? left + right : 0;
    return ok;
}

static inline int tp_retirement_budget_mul(uint64_t left, uint64_t right, uint64_t* product)
{
    int ok = product && (!left || right <= UINT64_MAX / left);
    if (product) *product = ok ? left * right : 0;
    return ok;
}

static inline int tp_retirement_budget_table_valid(TpRetirementBudgetTable const* table)
{
    int ok = table->classes && table->classes <= TP_RETIREMENT_BUDGET_CLASSES;
    for (unsigned i = 0; ok && i < table->classes; ++i)
        ok = table->batch[i].max_inputs && table->batch[i].max_inputs <= TP_RETIREMENT_BATCH_INPUTS &&
            table->batch[i].batch_ns &&
            (!i || (table->batch[i].max_inputs > table->batch[i - 1].max_inputs &&
                    table->batch[i].batch_ns >= table->batch[i - 1].batch_ns));
    for (unsigned i = ok ? table->classes : TP_RETIREMENT_BUDGET_CLASSES; i < TP_RETIREMENT_BUDGET_CLASSES; ++i)
        ok = ok && !table->batch[i].max_inputs && !table->batch[i].batch_ns;
    for (unsigned stage = 0; ok && stage < TP_RETIREMENT_BUDGET_STAGE_CAP; ++stage)
    {
        int named = stage != TP_RETIREMENT_BUDGET_STAGE_OBJECT && stage < TP_RETIREMENT_BUDGET_STAGE_COUNT;
        ok = named ? table->singleton_ns[stage] != 0 : !table->singleton_ns[stage];
    }
    return ok;
}

/* Missing (zero) bounds, unordered classes, a bound that shrinks with the
 * group size, a missing stage bound or a bound in an unnamed stage slot are
 * rejected; so is a metrics bound that cannot hold one input. */
static inline int tp_retirement_budget_valid(TpRetirementCampaignBudget const* budget)
{
    int ok = budget && budget->reviewed_ns && budget->reservation_ns && budget->materialization_ns &&
        budget->baseline_build_ns && budget->candidate_build_ns && budget->correctness_ns &&
        budget->settling_per_stage_ns && budget->aa_qualification_ns && budget->aa_receipt_sealing_ns &&
        budget->sample_export_per_stage_ns && budget->final_statistics_ns && budget->final_sealing_ns &&
        budget->cleanup_ns && budget->runtime_process_ns && budget->metrics_header_bytes &&
        budget->metrics_input_bytes && budget->aa_attestation_ns_per_mib &&
        budget->metrics_input_bytes <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES - budget->metrics_header_bytes &&
        budget->metrics_header_bytes < TP_RETIREMENT_METRICS_ARTIFACT_BYTES &&
        tp_retirement_budget_table_valid(&budget->timed) && tp_retirement_budget_table_valid(&budget->untimed);
    return ok;
}

/* The measured upper bound for one compiler process of a group, keyed by its
 * kind and stage: an object group of `inputs` inputs costs the first batch
 * class that holds it (a group larger than every class rejects); a singleton
 * link or self-host group has one input and costs its stage's bound. Any
 * other kind/stage pairing rejects. `untimed` selects the untimed table. */
static inline int tp_retirement_budget_group_ns(TpRetirementCampaignBudget const* budget, int untimed,
    unsigned kind, unsigned stage, unsigned inputs, uint64_t* ns)
{
    TpRetirementBudgetTable const* table = budget ? (untimed ? &budget->untimed : &budget->timed) : NULL;
    uint64_t found = 0;
    int ok = ns && inputs && tp_retirement_budget_valid(budget);
    if (ok && kind == TP_RETIREMENT_GROUP_OBJECT && stage == TP_RETIREMENT_BUDGET_STAGE_OBJECT)
    {
        for (unsigned i = 0; !found && i < table->classes; ++i)
            if (table->batch[i].max_inputs >= inputs) found = table->batch[i].batch_ns;
    }
    else if (ok && kind == TP_RETIREMENT_GROUP_SINGLETON && inputs == 1 &&
             stage != TP_RETIREMENT_BUDGET_STAGE_OBJECT && stage < TP_RETIREMENT_BUDGET_STAGE_COUNT)
        found = table->singleton_ns[stage];
    ok = ok && found;
    if (ns) *ns = ok ? found : 0;
    return ok;
}

/* The reviewed per-artifact metrics bound of an object batch of `inputs`
 * inputs: header plus inputs times the per-input bound, at most 64 MiB. */
static inline int tp_retirement_budget_metrics_bytes(TpRetirementCampaignBudget const* budget, unsigned inputs,
    uint64_t* bytes)
{
    uint64_t total = 0;
    int ok = bytes && inputs && inputs <= TP_RETIREMENT_BATCH_INPUTS && tp_retirement_budget_valid(budget) &&
        tp_retirement_budget_mul(budget->metrics_input_bytes, inputs, &total) &&
        tp_retirement_budget_add(total, budget->metrics_header_bytes, &total) &&
        total <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES;
    if (bytes) *bytes = ok ? total : 0;
    return ok;
}

static char const* const tp_retirement_budget_keys[] = {
    "reviewed-ns", "reservation-ns", "materialization-ns", "baseline-build-ns", "candidate-build-ns",
    "correctness-ns", "settling-per-stage-ns", "aa-qualification-ns", "aa-receipt-sealing-ns",
    "sample-export-per-stage-ns", "final-statistics-ns", "final-sealing-ns", "cleanup-ns",
    "runtime-process-ns", "metrics-header-bytes", "metrics-input-bytes", "aa-attestation-ns-per-mib"};
#define TP_RETIREMENT_BUDGET_SCALARS 17u
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(tp_retirement_budget_keys) == TP_RETIREMENT_BUDGET_SCALARS);

static inline void tp_retirement_budget_scalars(TpRetirementCampaignBudget const* budget,
    uint64_t values[TP_RETIREMENT_BUDGET_SCALARS])
{
    uint64_t const source[TP_RETIREMENT_BUDGET_SCALARS] = {budget->reviewed_ns, budget->reservation_ns,
        budget->materialization_ns, budget->baseline_build_ns, budget->candidate_build_ns,
        budget->correctness_ns, budget->settling_per_stage_ns, budget->aa_qualification_ns,
        budget->aa_receipt_sealing_ns, budget->sample_export_per_stage_ns, budget->final_statistics_ns,
        budget->final_sealing_ns, budget->cleanup_ns, budget->runtime_process_ns,
        budget->metrics_header_bytes, budget->metrics_input_bytes, budget->aa_attestation_ns_per_mib};
    memcpy(values, source, sizeof(source));
}

static inline int tp_retirement_budget_number(char const* text, size_t length, uint64_t* value)
{
    int ok = value && length && length <= 20 && (text[0] != '0' || length == 1);
    uint64_t number = 0;
    for (size_t i = 0; ok && i < length; ++i)
    {
        unsigned digit = (unsigned)(text[i] - '0');
        ok = text[i] >= '0' && text[i] <= '9' && number <= (UINT64_MAX - digit) / 10;
        if (ok) number = number * 10 + digit;
    }
    if (value) *value = ok ? number : 0;
    return ok;
}

static inline int tp_retirement_budget_table_encode(TpRetirementBudgetTable const* table, char const* prefix,
    char* output, size_t capacity, size_t* size)
{
    int ok = 1, written = 0;
    for (unsigned i = 0; ok && i < table->classes; ++i)
    {
        written = snprintf(output + *size, capacity - *size, "%sbatch=%u:%" PRIu64 "\n", prefix,
                           table->batch[i].max_inputs, table->batch[i].batch_ns);
        ok = written > 0 && (size_t)written < capacity - *size;
        if (ok) *size += (size_t)written;
    }
    for (unsigned stage = TP_RETIREMENT_BUDGET_STAGE_OBJECT + 1; ok && stage < TP_RETIREMENT_BUDGET_STAGE_COUNT; ++stage)
    {
        written = snprintf(output + *size, capacity - *size, "%ssingleton=%s:%" PRIu64 "\n", prefix,
                           tp_retirement_budget_stage_names[stage], table->singleton_ns[stage]);
        ok = written > 0 && (size_t)written < capacity - *size;
        if (ok) *size += (size_t)written;
    }
    return ok;
}

/* Canonical ASCII: `schema=`, `derivation=`, the scalar keys above in order,
 * then the timed table (one `batch=<max_inputs>:<batch_ns>` line per class,
 * then `singleton=<stage>:<ns>` per named non-object stage) and the untimed
 * table with the same lines prefixed `untimed-`; LF-terminated decimal values
 * without leading zeros. Returns the size (0 on failure). */
static inline size_t tp_retirement_budget_encode(TpRetirementCampaignBudget const* budget, char* output, size_t capacity)
{
    size_t size = 0;
    uint64_t values[TP_RETIREMENT_BUDGET_SCALARS];
    int ok = output && capacity && capacity <= TP_RETIREMENT_BUDGET_BYTES && tp_retirement_budget_valid(budget);
    if (ok) tp_retirement_budget_scalars(budget, values);
    int written = ok ? snprintf(output, capacity, "schema=%s\nderivation=%s\n", TP_RETIREMENT_BUDGET_SCHEMA,
                                TP_RETIREMENT_BUDGET_DERIVATION) : -1;
    ok = ok && written > 0 && (size_t)written < capacity;
    if (ok) size = (size_t)written;
    for (unsigned i = 0; ok && i < TP_RETIREMENT_BUDGET_SCALARS; ++i)
    {
        written = snprintf(output + size, capacity - size, "%s=%" PRIu64 "\n", tp_retirement_budget_keys[i],
                           values[i]);
        ok = written > 0 && (size_t)written < capacity - size;
        if (ok) size += (size_t)written;
    }
    ok = ok && tp_retirement_budget_table_encode(&budget->timed, "", output, capacity, &size) &&
        tp_retirement_budget_table_encode(&budget->untimed, "untimed-", output, capacity, &size);
    if (!ok && output && capacity) output[0] = 0;
    return ok ? size : 0;
}

static inline int tp_retirement_budget_digest(TpRetirementCampaignBudget const* budget, char digest[65])
{
    char text[TP_RETIREMENT_BUDGET_BYTES];
    size_t size = tp_retirement_budget_encode(budget, text, sizeof(text));
    int ok = digest && size;
    if (digest) digest[0] = 0;
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, text, (u64)size);
        sha256_finish_hex(&hash, digest);
    }
    return ok;
}

/* One table line (after its `untimed-` prefix, if any): `batch=<n>:<ns>`
 * appends a class; `singleton=<stage>:<ns>` sets a named non-object stage. */
static inline int tp_retirement_budget_table_line(char const* text, size_t length, TpRetirementBudgetTable* table)
{
    size_t prefix = length > 6 && !memcmp(text, "batch=", 6) ? 6 :
        length > 10 && !memcmp(text, "singleton=", 10) ? 10 : 0;
    size_t colon = prefix;
    while (colon < length && text[colon] != ':') ++colon;
    uint64_t value = 0, max_inputs = 0;
    int ok = prefix && colon < length && tp_retirement_budget_number(text + colon + 1, length - colon - 1, &value);
    if (ok && prefix == 6)
    {
        ok = table->classes < TP_RETIREMENT_BUDGET_CLASSES &&
            tp_retirement_budget_number(text + 6, colon - 6, &max_inputs) && max_inputs <= TP_RETIREMENT_BATCH_INPUTS;
        if (ok) table->batch[table->classes++] = (TpRetirementBudgetClass){(unsigned)max_inputs, value};
    }
    else if (ok)
    {
        unsigned found = TP_RETIREMENT_BUDGET_STAGE_OBJECT;
        for (unsigned stage = TP_RETIREMENT_BUDGET_STAGE_OBJECT + 1; stage < TP_RETIREMENT_BUDGET_STAGE_COUNT; ++stage)
            if (strlen(tp_retirement_budget_stage_names[stage]) == colon - 10 &&
                !memcmp(text + 10, tp_retirement_budget_stage_names[stage], colon - 10))
                found = stage;
        ok = found != TP_RETIREMENT_BUDGET_STAGE_OBJECT;
        if (ok) table->singleton_ns[found] = value;
    }
    return ok;
}

/* Strict decoder for an installed record: every scalar key once, in order,
 * then the table lines; the decoded record must re-encode to exactly the
 * input bytes, so any other spelling, order, duplicate or extra key rejects. */
static inline int tp_retirement_budget_decode(char const* bytes, size_t size, TpRetirementCampaignBudget* budget)
{
    TpRetirementCampaignBudget decoded = {0};
    uint64_t values[TP_RETIREMENT_BUDGET_SCALARS] = {0};
    size_t offset = 0;
    unsigned line = 0;
    int ok = bytes && budget && size && size < TP_RETIREMENT_BUDGET_BYTES;
    while (ok && offset < size)
    {
        size_t end = offset;
        while (end < size && bytes[end] != '\n') ++end;
        ok = end < size;
        char const* text = bytes + offset;
        size_t length = end - offset;
        if (ok && line >= 2 && line < 2 + TP_RETIREMENT_BUDGET_SCALARS)
        {
            char const* key = tp_retirement_budget_keys[line - 2];
            size_t key_length = strlen(key);
            ok = length > key_length + 1 && !memcmp(text, key, key_length) && text[key_length] == '=' &&
                tp_retirement_budget_number(text + key_length + 1, length - key_length - 1, &values[line - 2]);
        }
        else if (ok && line >= 2 + TP_RETIREMENT_BUDGET_SCALARS)
        {
            int untimed = length > 8 && !memcmp(text, "untimed-", 8);
            ok = untimed ? tp_retirement_budget_table_line(text + 8, length - 8, &decoded.untimed) :
                tp_retirement_budget_table_line(text, length, &decoded.timed);
        }
        offset = end + 1;
        ++line;
    }
    if (ok)
    {
        decoded.reviewed_ns = values[0];
        decoded.reservation_ns = values[1];
        decoded.materialization_ns = values[2];
        decoded.baseline_build_ns = values[3];
        decoded.candidate_build_ns = values[4];
        decoded.correctness_ns = values[5];
        decoded.settling_per_stage_ns = values[6];
        decoded.aa_qualification_ns = values[7];
        decoded.aa_receipt_sealing_ns = values[8];
        decoded.sample_export_per_stage_ns = values[9];
        decoded.final_statistics_ns = values[10];
        decoded.final_sealing_ns = values[11];
        decoded.cleanup_ns = values[12];
        decoded.runtime_process_ns = values[13];
        decoded.metrics_header_bytes = values[14];
        decoded.metrics_input_bytes = values[15];
        decoded.aa_attestation_ns_per_mib = values[16];
    }
    char canonical[TP_RETIREMENT_BUDGET_BYTES];
    size_t canonical_size = ok ? tp_retirement_budget_encode(&decoded, canonical, sizeof(canonical)) : 0;
    ok = ok && canonical_size == size && !memcmp(canonical, bytes, size);
    if (budget) *budget = ok ? decoded : (TpRetirementCampaignBudget){0};
    return ok;
}

/* One group list of the frozen counts: each group's input count (object:
 * members plus controls; singleton: 1), kind (TP_RETIREMENT_GROUP_*) and
 * budget stage (TP_RETIREMENT_BUDGET_STAGE_*). */
typedef struct TpRetirementBudgetGroups
{
    unsigned const* inputs;
    unsigned const* kinds;
    unsigned const* stages;
    unsigned count;
} TpRetirementBudgetGroups;

/* The frozen counts: the timed groups, runtime-eligible timed rows, the frozen
 * pair count and the untimed code-artifact groups. */
typedef struct TpRetirementBudgetCounts
{
    TpRetirementBudgetGroups timed, untimed;
    unsigned runtime_rows, pairs;
} TpRetirementBudgetCounts;

/* compiler_ns splits into object batches and stage singletons; so does
 * untimed_ns. aa_attestation_ns is the (v3) re-read term over its worst-case
 * aa_attestation_bytes. */
typedef struct TpRetirementBudgetPreflight
{
    uint64_t fixed_ns, compiler_ns, runtime_ns, untimed_ns, required_ns, remaining_ns;
    uint64_t aa_attestation_bytes, aa_attestation_ns;
    uint64_t compiler_object_ns, compiler_singleton_ns, untimed_object_ns, untimed_singleton_ns;
    uint64_t compiler_batches, runtime_processes, untimed_batches;
    int fits;
} TpRetirementBudgetPreflight;

/* Sum one group list's bounds, each process `repeats` times. */
static inline int tp_retirement_budget_groups_ns(TpRetirementCampaignBudget const* budget, int untimed,
    TpRetirementBudgetGroups const* groups, uint64_t repeats, uint64_t* object_ns, uint64_t* singleton_ns,
    uint64_t* processes)
{
    int ok = groups->count <= TP_RETIREMENT_MAX_CELLS &&
        (!groups->count || (groups->inputs && groups->kinds && groups->stages));
    for (unsigned g = 0; ok && g < groups->count; ++g)
    {
        uint64_t bound = 0, cost = 0;
        uint64_t* sum = groups->kinds[g] == TP_RETIREMENT_GROUP_OBJECT ? object_ns : singleton_ns;
        ok = tp_retirement_budget_group_ns(budget, untimed, groups->kinds[g], groups->stages[g], groups->inputs[g],
                &bound) &&
            tp_retirement_budget_mul(bound, repeats, &cost) && tp_retirement_budget_add(*sum, cost, sum) &&
            tp_retirement_budget_add(*processes, repeats, processes);
    }
    return ok;
}

/* (v3) The A/A attestation term: the worst-case bytes of the A/A stage's
 * sample shards the frozen counts allow, R * P * (330 * sum of the timed
 * groups' inputs + 266 * object groups), and their re-read time at the
 * reviewed ns-per-MiB rate, rounded up. */
static inline int tp_retirement_budget_attestation(TpRetirementCampaignBudget const* budget,
    TpRetirementBudgetCounts const* counts, uint64_t* bytes, uint64_t* ns)
{
    uint64_t inputs = 0, objects = 0, lines = 0, row_bytes = 0, batch_bytes = 0, total = 0, scaled = 0;
    int ok = counts->timed.count <= TP_RETIREMENT_MAX_CELLS && (!counts->timed.count || counts->timed.inputs) &&
        (!counts->timed.count || counts->timed.kinds);
    for (unsigned g = 0; ok && g < counts->timed.count; ++g)
    {
        ok = tp_retirement_budget_add(inputs, counts->timed.inputs[g], &inputs);
        objects += counts->timed.kinds[g] == TP_RETIREMENT_GROUP_OBJECT;
    }
    ok = ok && tp_retirement_budget_mul(TP_RETIREMENT_ROUNDS, counts->pairs, &lines) &&
        tp_retirement_budget_mul(inputs, TP_RETIREMENT_BUDGET_ATTEST_ROW_BYTES, &row_bytes) &&
        tp_retirement_budget_mul(objects, TP_RETIREMENT_BUDGET_ATTEST_BATCH_BYTES, &batch_bytes) &&
        tp_retirement_budget_add(row_bytes, batch_bytes, &total) && tp_retirement_budget_mul(total, lines, &total) &&
        tp_retirement_budget_mul(total, budget->aa_attestation_ns_per_mib, &scaled) &&
        tp_retirement_budget_add(scaled, TP_RETIREMENT_BUDGET_ATTEST_MIB - 1u, &scaled);
    *bytes = ok ? total : 0;
    *ns = ok ? scaled / TP_RETIREMENT_BUDGET_ATTEST_MIB : 0;
    return ok;
}

/* Derive the required time exactly as TP_RETIREMENT_BUDGET_DERIVATION states
 * and reject, before any timing, a budget that cannot hold it: missing
 * bounds, a group larger than every class, overflow or a sum above
 * reviewed_ns. The result is zeroed on rejection. */
static inline int tp_retirement_budget_preflight(TpRetirementCampaignBudget const* budget,
    TpRetirementBudgetCounts const* counts, TpRetirementBudgetPreflight* preflight)
{
    TpRetirementBudgetPreflight result = {0};
    uint64_t per_unit = 0, per_group = 0, settling = 0, export_ns = 0;
    int ok = preflight && counts && tp_retirement_budget_valid(budget) && counts->timed.count &&
        counts->runtime_rows <= counts->timed.count &&
        counts->pairs >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND && counts->pairs <= TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
        !(counts->pairs & 1) &&
        tp_retirement_budget_mul(TP_RETIREMENT_ROUNDS, counts->pairs, &per_unit) &&
        tp_retirement_budget_add(per_unit, TP_RETIREMENT_WARMUPS, &per_unit) &&
        tp_retirement_budget_mul(per_unit, 2, &per_unit) &&
        tp_retirement_budget_mul(per_unit, TP_RETIREMENT_BUDGET_STAGES, &per_group) &&
        tp_retirement_budget_mul(budget->settling_per_stage_ns, TP_RETIREMENT_BUDGET_STAGES, &settling) &&
        tp_retirement_budget_mul(budget->sample_export_per_stage_ns, TP_RETIREMENT_BUDGET_STAGES, &export_ns);
    if (ok)
    {
        uint64_t const fixed[] = {budget->reservation_ns, budget->materialization_ns, budget->baseline_build_ns,
            budget->candidate_build_ns, budget->correctness_ns, settling, budget->aa_qualification_ns,
            budget->aa_receipt_sealing_ns, export_ns, budget->final_statistics_ns, budget->final_sealing_ns,
            budget->cleanup_ns};
        for (unsigned i = 0; ok && i < BUSTER_ARRAY_LENGTH(fixed); ++i)
            ok = tp_retirement_budget_add(result.fixed_ns, fixed[i], &result.fixed_ns);
    }
    ok = ok && tp_retirement_budget_groups_ns(budget, 0, &counts->timed, per_group, &result.compiler_object_ns,
            &result.compiler_singleton_ns, &result.compiler_batches) &&
        tp_retirement_budget_add(result.compiler_object_ns, result.compiler_singleton_ns, &result.compiler_ns) &&
        tp_retirement_budget_mul(counts->runtime_rows, per_group, &result.runtime_processes) &&
        tp_retirement_budget_mul(result.runtime_processes, budget->runtime_process_ns, &result.runtime_ns) &&
        tp_retirement_budget_groups_ns(budget, 1, &counts->untimed, TP_RETIREMENT_BUDGET_UNTIMED_BATCHES,
            &result.untimed_object_ns, &result.untimed_singleton_ns, &result.untimed_batches) &&
        tp_retirement_budget_add(result.untimed_object_ns, result.untimed_singleton_ns, &result.untimed_ns) &&
        tp_retirement_budget_attestation(budget, counts, &result.aa_attestation_bytes, &result.aa_attestation_ns);
    ok = ok && tp_retirement_budget_add(result.fixed_ns, result.compiler_ns, &result.required_ns) &&
        tp_retirement_budget_add(result.required_ns, result.runtime_ns, &result.required_ns) &&
        tp_retirement_budget_add(result.required_ns, result.untimed_ns, &result.required_ns) &&
        tp_retirement_budget_add(result.required_ns, result.aa_attestation_ns, &result.required_ns) &&
        result.required_ns <= budget->reviewed_ns;
    if (ok)
    {
        result.remaining_ns = budget->reviewed_ns - result.required_ns;
        result.fits = 1;
    }
    if (preflight) *preflight = ok ? result : (TpRetirementBudgetPreflight){0};
    return ok;
}
#endif
