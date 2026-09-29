/* Reviewed A1 campaign budget for #881 (review item M4).
 * Amendment A1 replaces the fixed one-hour worker budget with a reviewed
 * budget bound into the admitted service recipe. This record holds every input
 * of that budget: the reviewed whole-job ceiling, the authenticated fixed-phase
 * bounds, a measured upper bound per batch process by group size (input
 * count), a measured upper bound per runtime process, and the reviewed
 * metrics-artifact byte bound per input. Its canonical text encoding names the
 * derivation formula, so the recipe/profile pin (`campaign-budget-sha256=`)
 * binds inputs and derivation together; the blocked profile carries no pin.
 * tp_retirement_budget_preflight derives the required time from the frozen
 * counts and rejects a job the reviewed ceiling cannot hold, before timing.
 * The numeric bounds are integration-time pins; nothing here admits a recipe.
 *
 * Map: TpRetirementCampaignBudget, tp_retirement_budget_valid,
 * tp_retirement_budget_batch_ns, tp_retirement_budget_metrics_bytes,
 * tp_retirement_budget_encode/_decode/_digest, TpRetirementBudgetCounts,
 * tp_retirement_budget_preflight.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_BUDGET_H
#define BUSTER_THROUGHPUT_RETIREMENT_BUDGET_H
#include "retirement_execution.h"

#define TP_RETIREMENT_BUDGET_SCHEMA "tp-retirement-campaign-budget-v1"
/* The derivation the pinned record commits to, per collection stage s (A/A,
 * A/B), per_unit = 2 * (warmups + rounds * pairs):
 * fixed + stages * (settling + export) + sum over timed groups of
 * stages * per_unit * batch(inputs) + runtime_rows * stages * per_unit *
 * runtime + sum over untimed groups of 2 variants * 2 purposes * batch(inputs),
 * where batch(n) is the first class whose max_inputs >= n. */
#define TP_RETIREMENT_BUDGET_DERIVATION \
    "fixed+stages*(settling+export)+sum_g(stages*2*(W+R*P)*batch(n_g))+U*stages*2*(W+R*P)*runtime" \
    "+sum_u(4*batch(n_u))"
#define TP_RETIREMENT_BUDGET_STAGES 2u
#define TP_RETIREMENT_BUDGET_CLASSES 16u
#define TP_RETIREMENT_BUDGET_BYTES 4096u
/* Untimed code-artifact groups cost two variants times production and
 * reproduction batches. */
#define TP_RETIREMENT_BUDGET_UNTIMED_BATCHES 4u

typedef struct TpRetirementBudgetClass
{
    unsigned max_inputs;
    uint64_t batch_ns;
} TpRetirementBudgetClass;

/* Every nanosecond bound includes launcher and collector work (outputs hashed,
 * metrics read and sharded, records written). batch[] is ascending by
 * max_inputs with nondecreasing bounds. */
typedef struct TpRetirementCampaignBudget
{
    uint64_t reviewed_ns;
    uint64_t reservation_ns, materialization_ns, baseline_build_ns, candidate_build_ns, correctness_ns;
    uint64_t settling_per_stage_ns, aa_qualification_ns, aa_receipt_sealing_ns, sample_export_per_stage_ns;
    uint64_t final_statistics_ns, final_sealing_ns, cleanup_ns;
    uint64_t runtime_process_ns;
    uint64_t metrics_header_bytes, metrics_input_bytes;
    unsigned classes;
    TpRetirementBudgetClass batch[TP_RETIREMENT_BUDGET_CLASSES];
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

/* Missing (zero) bounds, unordered classes or a bound that shrinks with the
 * group size are rejected; so is a metrics bound that cannot hold one input. */
static inline int tp_retirement_budget_valid(TpRetirementCampaignBudget const* budget)
{
    int ok = budget && budget->reviewed_ns && budget->reservation_ns && budget->materialization_ns &&
        budget->baseline_build_ns && budget->candidate_build_ns && budget->correctness_ns &&
        budget->settling_per_stage_ns && budget->aa_qualification_ns && budget->aa_receipt_sealing_ns &&
        budget->sample_export_per_stage_ns && budget->final_statistics_ns && budget->final_sealing_ns &&
        budget->cleanup_ns && budget->runtime_process_ns && budget->metrics_header_bytes &&
        budget->metrics_input_bytes &&
        budget->metrics_input_bytes <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES - budget->metrics_header_bytes &&
        budget->metrics_header_bytes < TP_RETIREMENT_METRICS_ARTIFACT_BYTES &&
        budget->classes && budget->classes <= TP_RETIREMENT_BUDGET_CLASSES;
    for (unsigned i = 0; ok && i < budget->classes; ++i)
        ok = budget->batch[i].max_inputs && budget->batch[i].max_inputs <= TP_RETIREMENT_BATCH_INPUTS &&
            budget->batch[i].batch_ns &&
            (!i || (budget->batch[i].max_inputs > budget->batch[i - 1].max_inputs &&
                    budget->batch[i].batch_ns >= budget->batch[i - 1].batch_ns));
    for (unsigned i = budget && budget->classes <= TP_RETIREMENT_BUDGET_CLASSES ? budget->classes : 0;
         ok && i < TP_RETIREMENT_BUDGET_CLASSES; ++i)
        ok = !budget->batch[i].max_inputs && !budget->batch[i].batch_ns;
    return ok;
}

/* The measured upper bound for one batch of `inputs` inputs (a singleton
 * link/self-host group has one); a group larger than every class rejects. */
static inline int tp_retirement_budget_batch_ns(TpRetirementCampaignBudget const* budget, unsigned inputs, uint64_t* ns)
{
    unsigned found = TP_RETIREMENT_BUDGET_CLASSES;
    int ok = ns && inputs && tp_retirement_budget_valid(budget);
    for (unsigned i = 0; ok && i < budget->classes && found == TP_RETIREMENT_BUDGET_CLASSES; ++i)
        if (budget->batch[i].max_inputs >= inputs) found = i;
    ok = ok && found < TP_RETIREMENT_BUDGET_CLASSES;
    if (ns) *ns = ok ? budget->batch[found].batch_ns : 0;
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
    "runtime-process-ns", "metrics-header-bytes", "metrics-input-bytes"};
#define TP_RETIREMENT_BUDGET_SCALARS 16u
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(tp_retirement_budget_keys) == TP_RETIREMENT_BUDGET_SCALARS);

static inline void tp_retirement_budget_scalars(TpRetirementCampaignBudget const* budget,
    uint64_t values[TP_RETIREMENT_BUDGET_SCALARS])
{
    uint64_t const source[TP_RETIREMENT_BUDGET_SCALARS] = {budget->reviewed_ns, budget->reservation_ns,
        budget->materialization_ns, budget->baseline_build_ns, budget->candidate_build_ns,
        budget->correctness_ns, budget->settling_per_stage_ns, budget->aa_qualification_ns,
        budget->aa_receipt_sealing_ns, budget->sample_export_per_stage_ns, budget->final_statistics_ns,
        budget->final_sealing_ns, budget->cleanup_ns, budget->runtime_process_ns,
        budget->metrics_header_bytes, budget->metrics_input_bytes};
    memcpy(values, source, sizeof(source));
}

/* Canonical ASCII: `schema=`, `derivation=`, the scalar keys above in order,
 * then one `batch=<max_inputs>:<batch_ns>` line per class; LF-terminated
 * decimal values without leading zeros. Returns the size (0 on failure). */
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
    for (unsigned i = 0; ok && i < budget->classes; ++i)
    {
        written = snprintf(output + size, capacity - size, "batch=%u:%" PRIu64 "\n", budget->batch[i].max_inputs,
                           budget->batch[i].batch_ns);
        ok = written > 0 && (size_t)written < capacity - size;
        if (ok) size += (size_t)written;
    }
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

/* Strict decoder for an installed record: every scalar key once, in order,
 * then at least one class line; the decoded record must re-encode to exactly
 * the input bytes, so any other spelling, order or extra key rejects. */
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
            size_t colon = 6;
            while (colon < length && text[colon] != ':') ++colon;
            uint64_t max_inputs = 0, batch_ns = 0;
            ok = decoded.classes < TP_RETIREMENT_BUDGET_CLASSES && length > 6 && !memcmp(text, "batch=", 6) &&
                colon < length && tp_retirement_budget_number(text + 6, colon - 6, &max_inputs) &&
                max_inputs <= TP_RETIREMENT_BATCH_INPUTS &&
                tp_retirement_budget_number(text + colon + 1, length - colon - 1, &batch_ns);
            if (ok) decoded.batch[decoded.classes++] = (TpRetirementBudgetClass){(unsigned)max_inputs, batch_ns};
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
    }
    char canonical[TP_RETIREMENT_BUDGET_BYTES];
    size_t canonical_size = ok ? tp_retirement_budget_encode(&decoded, canonical, sizeof(canonical)) : 0;
    ok = ok && canonical_size == size && !memcmp(canonical, bytes, size);
    if (budget) *budget = ok ? decoded : (TpRetirementCampaignBudget){0};
    return ok;
}

/* The frozen counts: timed groups (object: its input count, members plus
 * controls; singleton: 1), runtime-eligible timed rows, the frozen pair count
 * and the untimed code-artifact groups' input counts. */
typedef struct TpRetirementBudgetCounts
{
    unsigned const* group_inputs;
    unsigned const* untimed_inputs;
    unsigned groups, runtime_rows, pairs, untimed_groups;
} TpRetirementBudgetCounts;

typedef struct TpRetirementBudgetPreflight
{
    uint64_t fixed_ns, compiler_ns, runtime_ns, untimed_ns, required_ns, remaining_ns;
    uint64_t compiler_batches, runtime_processes, untimed_batches;
    int fits;
} TpRetirementBudgetPreflight;

/* Derive the required time exactly as TP_RETIREMENT_BUDGET_DERIVATION states
 * and reject, before any timing, a budget that cannot hold it: missing
 * bounds, a group larger than every class, overflow or a sum above
 * reviewed_ns. The result is zeroed on rejection. */
static inline int tp_retirement_budget_preflight(TpRetirementCampaignBudget const* budget,
    TpRetirementBudgetCounts const* counts, TpRetirementBudgetPreflight* preflight)
{
    TpRetirementBudgetPreflight result = {0};
    uint64_t per_unit = 0, per_group = 0, settling = 0, export_ns = 0;
    int ok = preflight && counts && tp_retirement_budget_valid(budget) && counts->groups &&
        counts->groups <= TP_RETIREMENT_MAX_CELLS && counts->group_inputs &&
        counts->runtime_rows <= counts->groups &&
        counts->pairs >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND && counts->pairs <= TP_RETIREMENT_EXECUTION_MAX_PAIRS &&
        !(counts->pairs & 1) && (!counts->untimed_groups || counts->untimed_inputs) &&
        counts->untimed_groups <= TP_RETIREMENT_MAX_CELLS &&
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
    for (unsigned g = 0; ok && g < counts->groups; ++g)
    {
        uint64_t batch = 0, cost = 0;
        ok = tp_retirement_budget_batch_ns(budget, counts->group_inputs[g], &batch) &&
            tp_retirement_budget_mul(batch, per_group, &cost) &&
            tp_retirement_budget_add(result.compiler_ns, cost, &result.compiler_ns) &&
            tp_retirement_budget_add(result.compiler_batches, per_group, &result.compiler_batches);
    }
    ok = ok && tp_retirement_budget_mul(counts->runtime_rows, per_group, &result.runtime_processes) &&
        tp_retirement_budget_mul(result.runtime_processes, budget->runtime_process_ns, &result.runtime_ns);
    for (unsigned u = 0; ok && u < counts->untimed_groups; ++u)
    {
        uint64_t batch = 0, cost = 0;
        ok = tp_retirement_budget_batch_ns(budget, counts->untimed_inputs[u], &batch) &&
            tp_retirement_budget_mul(batch, TP_RETIREMENT_BUDGET_UNTIMED_BATCHES, &cost) &&
            tp_retirement_budget_add(result.untimed_ns, cost, &result.untimed_ns) &&
            tp_retirement_budget_add(result.untimed_batches, TP_RETIREMENT_BUDGET_UNTIMED_BATCHES,
                &result.untimed_batches);
    }
    ok = ok && tp_retirement_budget_add(result.fixed_ns, result.compiler_ns, &result.required_ns) &&
        tp_retirement_budget_add(result.required_ns, result.runtime_ns, &result.required_ns) &&
        tp_retirement_budget_add(result.required_ns, result.untimed_ns, &result.required_ns) &&
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
