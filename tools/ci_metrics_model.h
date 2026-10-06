// Execution identity, immutable observation replay, and advisory distributions.
// CmStore owns a bounded contiguous string arena. cm_import never repairs a row.
// cm_alias preserves #2052's exact name/status/times/step equality contract.
// cm_compare separates descriptive signals from qualification and merge gates.
#ifndef BUSTER_CI_METRICS_MODEL_H
#define BUSTER_CI_METRICS_MODEL_H
#include "ci_metrics_json.h"
#define CM_ARENA_BYTES (128u * 1024u * 1024u)
#define CM_WINDOW 20u
#define CM_MINIMUM 8u
typedef enum CmField
{
    CM_WORKFLOW, CM_JOB_KEY, CM_MATRIX, CM_INVOCATION, CM_NAME, CM_EVENT, CM_HEAD_BRANCH,
    CM_EVENT_SHA, CM_TESTED_SHA, CM_TESTED_TREE, CM_WORKFLOW_SHA, CM_WORKFLOW_BLOB,
    CM_HARDWARE, CM_HOSTING, CM_LABELS, CM_CPU_RAW, CM_CPU, CM_OS, CM_OS_VERSION,
    CM_KERNEL, CM_ARCH, CM_PROCESS_ARCH, CM_EFFECTIVE_CPU, CM_QUOTA, CM_MEMORY,
    CM_IMAGE, CM_IMAGE_VERSION, CM_EXECUTION_CONTEXT, CM_TOOLCHAIN, CM_CACHE,
    CM_WORKLOAD, CM_WORKERS, CM_KIND, CM_CONCLUSION, CM_STARTED, CM_COMPLETED,
    CM_STEPS, CM_ALIAS, CM_OBSERVED, CM_COLLECTOR, CM_RUN_CONCLUSION, CM_CREATED,
    CM_CONTEXT_STATUS, CM_MACHINE_JSON, CM_SOURCE_JSON, CM_FIELD_COUNT
} CmField;
BUSTER_GLOBAL_LOCAL const char *const cm_fields[CM_FIELD_COUNT] =
{
    "workflow_path", "job_key", "matrix_identity", "invocation_path", "display_name", "event", "head_branch",
    "event_sha", "tested_sha", "tested_tree", "workflow_sha", "workflow_blob",
    "hardware_status", "runner_class", "requested_labels", "cpu_raw", "cpu_normalized", "os", "os_version",
    "kernel", "machine_arch", "process_arch", "effective_cpu", "cpu_quota", "memory_limit",
    "runner_image", "runner_image_version", "execution_context", "toolchain_identity", "cache_state",
    "workload_identity", "worker_budget", "execution_kind", "conclusion", "started_at", "completed_at",
    "steps_json", "alias_status", "observed_at", "collector_revision", "workflow_conclusion", "workflow_created_at",
    "comparison_context_status", "machine_report_json", "source_report_json"
};
typedef struct CmRow CmRow;
struct CmRow
{
    uint64_t run, job, attempt, origin_job, origin_attempt;
    const char *s[CM_FIELD_COUNT];
    int64_t seconds;
    int elapsed_available, active;
};
typedef struct CmStore CmStore;
struct CmStore
{
    CmRow *rows;
    char *arena;
    size_t used;
    unsigned count, duplicates, enrichments, conflicts, gaps, incomplete_runs;
};
typedef struct CmStats CmStats;
struct CmStats { unsigned n, days; double median, mad, p95; int p95_available; };
typedef struct CmComparison CmComparison;
struct CmComparison
{
    CmStats baseline, candidate;
    double seconds, percent, practical_seconds, uncertainty_seconds;
    int percent_available;
    const char *state;
};
BUSTER_GLOBAL_LOCAL int cm_store_init(CmStore *s)
{
    memset(s, 0, sizeof(*s));
    s->rows = calloc(CM_ROWS, sizeof(*s->rows)); s->arena = malloc(CM_ARENA_BYTES);
    int result = s->rows && s->arena;
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_store_free(CmStore *s)
{
    free(s->rows); free(s->arena); memset(s, 0, sizeof(*s));
}
BUSTER_GLOBAL_LOCAL const char *cm_keep(CmStore *s, const char *text)
{
    size_t length = strlen(text) + 1;
    const char *result = NULL;
    if (length <= CM_ARENA_BYTES - s->used)
    {
        result = s->arena + s->used; memcpy(s->arena + s->used, text, length); s->used += length;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_kind(const char *value)
{
    int result = cm_equal(value, "executed") || cm_equal(value, "skipped") ||
        cm_equal(value, "controller-metadata") || cm_equal(value, "draft-deferral") || cm_equal(value, "unresolved-alias");
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_row_valid(const CmRow *r)
{
    int valid = r->run && r->job && r->attempt && r->attempt <= 100 &&
        r->origin_attempt <= r->attempt && cm_kind(r->s[CM_KIND]) && cm_time(r->s[CM_OBSERVED]) >= 0 &&
        cm_sha(r->s[CM_COLLECTOR]);
    for (unsigned i = 0; valid && i < CM_FIELD_COUNT; ++i)
        valid = r->s[i] && strlen(r->s[i]) <= ((i == CM_STEPS || i == CM_MACHINE_JSON || i == CM_SOURCE_JSON) ? 65536u : CM_FIELD);
    valid = valid && (!r->s[CM_TESTED_SHA][0] || cm_sha(r->s[CM_TESTED_SHA])) &&
        (!r->s[CM_EVENT_SHA][0] || cm_sha(r->s[CM_EVENT_SHA])) &&
        (!r->s[CM_TESTED_TREE][0] || cm_sha(r->s[CM_TESTED_TREE])) &&
        (!r->s[CM_WORKFLOW_BLOB][0] || cm_sha(r->s[CM_WORKFLOW_BLOB]));
    if (valid && cm_equal(r->s[CM_HARDWARE], "verified-startup"))
    {
        valid = r->s[CM_JOB_KEY][0] && r->s[CM_WORKFLOW][0] && r->s[CM_CPU][0] &&
            (cm_equal(r->s[CM_HOSTING], "github-hosted") || cm_equal(r->s[CM_HOSTING], "self-hosted")) &&
            cm_sha(r->s[CM_WORKFLOW_SHA]) && cm_sha(r->s[CM_WORKFLOW_BLOB]);
    }
    int64_t seconds = 0;
    int elapsed = cm_span(r->s[CM_STARTED], r->s[CM_COMPLETED], &seconds);
    if (!cm_equal(r->s[CM_KIND], "executed") && !cm_equal(r->s[CM_KIND], "draft-deferral")) elapsed = 0;
    valid = valid && r->elapsed_available == elapsed && (!elapsed || r->seconds == seconds);
    if (valid && r->s[CM_STEPS][0])
    {
        CmJson steps = cm_json_parse(r->s[CM_STEPS], strlen(r->s[CM_STEPS]));
        valid = steps.valid && steps.tokens[1].kind == 'a';
        cm_json_free(&steps);
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_rows_equal(const CmRow *a, const CmRow *b, int ignore_observation)
{
    int equal = a->run == b->run && a->job == b->job && a->attempt == b->attempt &&
        a->origin_job == b->origin_job && a->origin_attempt == b->origin_attempt &&
        a->elapsed_available == b->elapsed_available && a->seconds == b->seconds;
    for (unsigned i = 0; equal && i < CM_FIELD_COUNT; ++i)
        if (!(ignore_observation && (i == CM_OBSERVED || i == CM_COLLECTOR))) equal = cm_equal(a->s[i], b->s[i]);
    return equal;
}
BUSTER_GLOBAL_LOCAL int cm_alias(const CmRow *later, const CmRow *earlier)
{
    int result = later->run == earlier->run && later->attempt > earlier->attempt &&
        earlier->elapsed_available && cm_equal(later->s[CM_NAME], earlier->s[CM_NAME]) &&
        cm_equal(later->s[CM_KIND], earlier->s[CM_KIND]) &&
        cm_equal(later->s[CM_CONCLUSION], earlier->s[CM_CONCLUSION]) &&
        cm_equal(later->s[CM_STARTED], earlier->s[CM_STARTED]) &&
        cm_equal(later->s[CM_COMPLETED], earlier->s[CM_COMPLETED]) &&
        cm_equal(later->s[CM_STEPS], earlier->s[CM_STEPS]);
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_immutable_equal(const CmRow *a, const CmRow *b)
{
    const unsigned immutable[] = {CM_NAME, CM_EVENT, CM_EVENT_SHA, CM_STARTED, CM_COMPLETED, CM_STEPS, CM_CONCLUSION, CM_KIND};
    int result = a->run == b->run && a->job == b->job && a->attempt == b->attempt;
    for (unsigned i = 0; result && i < sizeof(immutable) / sizeof(immutable[0]); ++i)
        result = cm_equal(a->s[immutable[i]], b->s[immutable[i]]);
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_add(CmStore *s, const CmRow *input)
{
    int result = cm_row_valid(input) && s->count < CM_ROWS;
    unsigned previous = CM_ROWS;
    for (unsigned i = 0; result && i < s->count; ++i)
    {
        CmRow *r = &s->rows[i];
        if (r->active && r->run == input->run && r->job == input->job && r->attempt == input->attempt)
        {
            previous = i;
            if (cm_rows_equal(r, input, 1)) { ++s->duplicates; result = 2; }
            else if (!cm_immutable_equal(r, input)) { ++s->conflicts; result = 0; }
            else if (cm_time(input->s[CM_OBSERVED]) <= cm_time(r->s[CM_OBSERVED])) result = 2;
        }
        if (result == 2) break;
    }
    if (result == 1)
    {
        CmRow row = *input;
        size_t checkpoint = s->used;
        for (unsigned i = 0; result && i < CM_FIELD_COUNT; ++i)
        {
            row.s[i] = cm_keep(s, input->s[i]); result = row.s[i] != NULL;
        }
        if (result)
        {
            row.active = 1;
            if (previous < s->count) { s->rows[previous].active = 0; ++s->enrichments; }
            s->rows[s->count++] = row;
        }
        else s->used = checkpoint;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_emit_row(FILE *f, const CmRow *r)
{
    fputs("{\"schema\":\"" CM_SCHEMA "\",\"repository\":\"" CM_REPO "\",", f);
    fprintf(f, "\"run_id\":%" PRIu64 ",\"job_id\":%" PRIu64 ",\"association_attempt\":%" PRIu64
        ",\"origin_job_id\":%" PRIu64 ",\"original_attempt\":%" PRIu64 ",\"api_elapsed_seconds\":",
        r->run, r->job, r->attempt, r->origin_job, r->origin_attempt);
    if (r->elapsed_available) fprintf(f, "%" PRId64, r->seconds); else fputs("null", f);
    fputs(",\"timestamp_resolution_seconds\":1", f);
    for (unsigned i = 0; i < CM_FIELD_COUNT; ++i)
    {
        fputc(',', f); cm_quote(f, cm_fields[i]); fputc(':', f); cm_quote(f, r->s[i]);
    }
    fputs("}\n", f);
}
BUSTER_GLOBAL_LOCAL int cm_import(CmStore *store, const char *text, size_t length)
{
    int valid = length <= CM_BYTES && !memchr(text, 0, length);
    size_t p = 0;
    while (valid && p < length)
    {
        size_t start = p;
        while (p < length && text[p] != '\n') ++p;
        size_t n = p - start;
        p += p < length;
        if (n)
        {
            CmJson j = cm_json_parse(text + start, n);
            CmRow r = {0};
            valid = j.valid && cm_equal(cm_get(&j, 1, "schema"), CM_SCHEMA) &&
                cm_equal(cm_get(&j, 1, "repository"), CM_REPO) && cm_number(&j, 1, "timestamp_resolution_seconds") == 1;
            r.run = cm_number(&j, 1, "run_id"); r.job = cm_number(&j, 1, "job_id");
            r.attempt = cm_number(&j, 1, "association_attempt"); r.origin_job = cm_number(&j, 1, "origin_job_id");
            r.origin_attempt = cm_number(&j, 1, "original_attempt");
            unsigned duration = cm_member(&j, 1, "api_elapsed_seconds");
            uint64_t seconds = 0;
            r.elapsed_available = duration && cm_unsigned(cm_value(&j, duration), &seconds) && seconds <= 7 * 86400;
            r.seconds = (int64_t)seconds;
            valid = valid && duration && (r.elapsed_available || cm_equal(cm_value(&j, duration), "null"));
            for (unsigned i = 0; valid && i < CM_FIELD_COUNT; ++i)
            {
                unsigned field = cm_member(&j, 1, cm_fields[i]);
                valid = field && j.tokens[field].kind == 's'; r.s[i] = cm_value(&j, field);
            }
            if (valid) valid = cm_add(store, &r) != 0;
            cm_json_free(&j);
        }
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_hosted(const CmRow *r)
{
    int result = cm_equal(r->s[CM_HOSTING], "github-hosted");
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_sample(const CmRow *r)
{
    int result = r->active && cm_hosted(r) && r->elapsed_available && cm_equal(r->s[CM_KIND], "executed") &&
        cm_equal(r->s[CM_CONCLUSION], "success") && r->origin_job == r->job &&
        r->origin_attempt == r->attempt && cm_equal(r->s[CM_ALIAS], "physical-execution");
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_context(const CmRow *r)
{
    const unsigned required[] = {CM_WORKFLOW, CM_JOB_KEY, CM_MATRIX, CM_WORKFLOW_BLOB, CM_CPU,
        CM_OS, CM_OS_VERSION, CM_KERNEL, CM_ARCH, CM_PROCESS_ARCH, CM_EFFECTIVE_CPU,
        CM_IMAGE, CM_IMAGE_VERSION, CM_TOOLCHAIN, CM_CACHE, CM_WORKLOAD, CM_WORKERS, CM_TESTED_SHA};
    int result = cm_equal(r->s[CM_HARDWARE], "verified-startup") &&
        cm_equal(r->s[CM_CONTEXT_STATUS], "complete");
    for (unsigned i = 0; result && i < sizeof(required) / sizeof(required[0]); ++i)
        result = r->s[required[i]][0] && !cm_equal(r->s[required[i]], "unknown") && !cm_equal(r->s[required[i]], "unavailable");
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_same_series(const CmRow *a, const CmRow *b)
{
    const unsigned fields[] = {CM_WORKFLOW, CM_JOB_KEY, CM_MATRIX, CM_INVOCATION, CM_NAME, CM_LABELS, CM_EVENT, CM_HEAD_BRANCH,
        CM_WORKFLOW_BLOB, CM_CPU, CM_OS, CM_OS_VERSION, CM_KERNEL, CM_ARCH, CM_PROCESS_ARCH,
        CM_EFFECTIVE_CPU, CM_QUOTA, CM_MEMORY, CM_IMAGE, CM_IMAGE_VERSION, CM_EXECUTION_CONTEXT,
        CM_TOOLCHAIN, CM_CACHE, CM_WORKLOAD, CM_WORKERS, CM_KIND};
    int result = a->origin_attempt == b->origin_attempt;
    for (unsigned i = 0; result && i < sizeof(fields) / sizeof(fields[0]); ++i)
        result = cm_equal(a->s[fields[i]], b->s[fields[i]]);
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_sort(double *values, unsigned n)
{
    // Windows are statically bounded to twenty observations, never all history.
    for (unsigned i = 1; i < n; ++i)
    {
        double v = values[i]; unsigned p = i;
        while (p && values[p - 1] > v) { values[p] = values[p - 1]; --p; }
        values[p] = v;
    }
}
BUSTER_GLOBAL_LOCAL double cm_median(const double *values, unsigned n)
{
    double result = n ? (n & 1 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) * .5) : 0;
    return result;
}
BUSTER_GLOBAL_LOCAL CmStats cm_statistics(const CmRow *const *rows, unsigned n)
{
    CmStats result = {0};
    double values[CM_WINDOW], deviations[CM_WINDOW];
    n = n < CM_WINDOW ? n : CM_WINDOW;
    int64_t days[CM_WINDOW];
    for (unsigned i = 0; i < n; ++i)
    {
        values[i] = (double)rows[i]->seconds;
        int64_t day = cm_time(rows[i]->s[CM_STARTED]) / 86400;
        int seen = 0;
        for (unsigned k = 0; k < result.days; ++k) seen |= days[k] == day;
        if (!seen) days[result.days++] = day;
    }
    result.n = n; cm_sort(values, n); result.median = cm_median(values, n);
    for (unsigned i = 0; i < n; ++i) deviations[i] = fabs(values[i] - result.median);
    cm_sort(deviations, n); result.mad = cm_median(deviations, n);
    result.p95_available = n >= 20;
    if (result.p95_available) result.p95 = values[(95 * n + 99) / 100 - 1];
    return result;
}
BUSTER_GLOBAL_LOCAL CmComparison cm_compare(const CmRow *const *baseline, unsigned bn,
    const CmRow *const *candidate, unsigned cn)
{
    CmComparison result = {0};
    result.baseline = cm_statistics(baseline, bn); result.candidate = cm_statistics(candidate, cn);
    result.seconds = result.candidate.median - result.baseline.median;
    result.percent_available = result.baseline.median >= 2;
    if (result.percent_available) result.percent = result.seconds * 100 / result.baseline.median;
    result.practical_seconds = fmax(2, 3 * result.baseline.mad);
    result.uncertainty_seconds = fmax(2, 3 * result.candidate.mad);
    result.state = "insufficient data";
    int comparable = bn && cn && cm_context(baseline[0]) && cm_context(candidate[0]) &&
        cm_same_series(baseline[0], candidate[0]);
    for (unsigned i = 0; comparable && i < bn; ++i)
    {
        comparable = cm_context(baseline[i]) && cm_same_series(baseline[0], baseline[i]);
        for (unsigned k = 0; comparable && k < cn; ++k)
            comparable = !(baseline[i]->run == candidate[k]->run && baseline[i]->job == candidate[k]->job);
    }
    for (unsigned i = 0; comparable && i < cn; ++i)
        comparable = cm_context(candidate[i]) && cm_same_series(candidate[0], candidate[i]);
    if (bn && cn && !comparable) result.state = "noncomparable/workload-or-environment changed";
    else if (bn >= CM_MINIMUM && cn >= CM_MINIMUM)
    {
        if (!result.percent_available || result.baseline.days < 3 || result.candidate.days < 3)
            result.state = "inconclusive";
        else if (fabs(result.seconds) > result.practical_seconds + result.uncertainty_seconds)
            result.state = result.seconds > 0 ? "regression signal" : "improvement signal";
        else if (fabs(result.seconds) <= result.practical_seconds && result.uncertainty_seconds <= result.practical_seconds)
            result.state = "no detectable change";
        else result.state = "inconclusive";
    }
    return result;
}
#endif
