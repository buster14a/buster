// Bounded browsable Markdown reports and deterministic JSON/CSV exports.
// cm_reports renders raw observations, exclusions, trailing and fixed anchors.
// Hashes are filenames only: full series equality detects collisions.
// Data branch reports are GitHub-readable; no browser framework or service.
#ifndef BUSTER_CI_METRICS_REPORT_H
#define BUSTER_CI_METRICS_REPORT_H
#include "ci_metrics_collect.h"
#define CM_SERIES 128u
#define CM_FILES 256u
typedef struct CmOutputs CmOutputs;
struct CmOutputs { CmFile files[CM_FILES]; unsigned count; };
typedef struct CmSeries CmSeries;
struct CmSeries { const CmRow *representative; uint64_t hash; unsigned samples, raw, missing; };
BUSTER_GLOBAL_LOCAL const unsigned cm_series_fields[] =
{
    CM_WORKFLOW, CM_JOB_KEY, CM_MATRIX, CM_INVOCATION, CM_NAME, CM_LABELS, CM_EVENT, CM_HEAD_BRANCH,
    CM_WORKFLOW_BLOB, CM_CPU, CM_OS, CM_OS_VERSION, CM_KERNEL, CM_ARCH, CM_PROCESS_ARCH,
    CM_EFFECTIVE_CPU, CM_QUOTA, CM_MEMORY, CM_IMAGE, CM_IMAGE_VERSION, CM_EXECUTION_CONTEXT,
    CM_TOOLCHAIN, CM_CACHE, CM_WORKLOAD, CM_WORKERS, CM_KIND
};
BUSTER_GLOBAL_LOCAL uint64_t cm_series_hash(const CmRow *r)
{
    uint64_t result = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < sizeof(cm_series_fields) / sizeof(cm_series_fields[0]); ++i)
    {
        const char *s = r->s[cm_series_fields[i]];
        for (size_t k = 0; s[k]; ++k) { result ^= (unsigned char)s[k]; result *= UINT64_C(1099511628211); }
        result ^= 0; result *= UINT64_C(1099511628211);
    }
    result ^= r->origin_attempt; result *= UINT64_C(1099511628211);
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_output(CmOutputs *out, const char *path, FILE *file)
{
    int result = file && out->count < CM_FILES && cm_path(path);
    char *content = result ? cm_memory(file) : NULL;
    if (file) fclose(file);
    char *name = content ? strdup(path) : NULL;
    result = result && content && name;
    if (result) out->files[out->count++] = (CmFile){name, content, {0}};
    else { free(content); free(name); }
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_outputs_free(CmOutputs *out)
{
    for (unsigned i = 0; i < out->count; ++i)
    {
        free((void *)out->files[i].path); free((void *)out->files[i].content);
    }
    memset(out, 0, sizeof(*out));
}
BUSTER_GLOBAL_LOCAL int cm_before(const CmRow *a, const CmRow *b)
{
    int64_t at = cm_time(a->s[CM_STARTED]), bt = cm_time(b->s[CM_STARTED]);
    int result = at < bt || (at == bt && (a->run < b->run || (a->run == b->run && a->job < b->job)));
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_order(const CmRow **rows, const CmRow **scratch, unsigned n)
{
    for (unsigned width = 1; width < n; width *= 2)
    {
        for (unsigned start = 0; start < n; start += 2 * width)
        {
            unsigned mid = start + width < n ? start + width : n;
            unsigned end = start + 2 * width < n ? start + 2 * width : n;
            unsigned left = start, right = mid;
            for (unsigned p = start; p < end; ++p)
                scratch[p] = right == end || (left < mid && cm_before(rows[left], rows[right])) ? rows[left++] : rows[right++];
        }
        memcpy(rows, scratch, n * sizeof(*rows));
    }
}
BUSTER_GLOBAL_LOCAL void cm_point(FILE *f, const CmRow *r)
{
    fprintf(f, "[job %" PRIu64 "](https://github.com/" CM_REPO "/actions/runs/%" PRIu64 "/job/%" PRIu64
        ") / [attempt %" PRIu64 "](https://github.com/" CM_REPO "/actions/runs/%" PRIu64 "/attempts/%" PRIu64 ")",
        r->job, r->run, r->job, r->attempt, r->run, r->attempt);
    if (cm_sha(r->s[CM_TESTED_SHA]))
        fprintf(f, " / [tested %.12s](https://github.com/" CM_REPO "/commit/%s)", r->s[CM_TESTED_SHA], r->s[CM_TESTED_SHA]);
    else fputs(" / tested source unavailable", f);
}
BUSTER_GLOBAL_LOCAL void cm_stats_text(FILE *f, const CmStats *s)
{
    fprintf(f, "n=%u; median %.1f s; MAD %.1f s; %u UTC date buckets", s->n, s->median, s->mad, s->days);
    if (s->p95_available) fprintf(f, "; p95 %.1f s", s->p95);
    else fputs("; p95 unavailable (requires 20)", f);
}
BUSTER_GLOBAL_LOCAL void cm_comparison_text(FILE *f, const char *label, const CmComparison *c)
{
    fprintf(f, "### %s\n\n**%s**. Baseline: ", label, c->state); cm_stats_text(f, &c->baseline);
    fputs(". Candidate: ", f); cm_stats_text(f, &c->candidate);
    fprintf(f, ". Change: %+.1f s", c->seconds);
    if (c->percent_available) fprintf(f, " (%+.1f%%)", c->percent);
    else fputs(" (percent unavailable below two-second baseline)", f);
    fprintf(f, ". Observed practical floor %.1f s; candidate variability allowance %.1f s.\n\n",
        c->practical_seconds, c->uncertainty_seconds);
}
BUSTER_GLOBAL_LOCAL void cm_window(FILE *f, const char *name, const CmRow *const *rows, unsigned n)
{
    fprintf(f, "**%s observations:** ", name);
    for (unsigned i = 0; i < n; ++i)
    {
        if (i) fputs("; ", f);
        cm_point(f, rows[i]); fputs(" at ", f); cm_escape(f, rows[i]->s[CM_STARTED], 0);
    }
    if (!n) fputs("unavailable", f);
    fputs(".\n\n", f);
}
BUSTER_GLOBAL_LOCAL int cm_reports(CmTransport *t, CmStore *store, CmOutputs *out, const char *observed,
    const char *event_filter, const char *revision_filter, int64_t since, int64_t until)
{
    CmSeries series[CM_SERIES]; memset(series, 0, sizeof(series));
    unsigned count = 0, excluded = 0, missing_hardware = 0, costs_missing = 0, aliases = 0, failed = 0;
    uint64_t runner_seconds = 0, waste_seconds = 0;
    const CmRow **ordered = calloc(store->count + 1, sizeof(*ordered));
    const CmRow **scratch = calloc(store->count + 1, sizeof(*scratch));
    unsigned n = 0;
    int valid = ordered && scratch;
    for (unsigned i = 0; valid && i < store->count; ++i)
    {
        const CmRow *r = &store->rows[i];
        int64_t start = cm_time(r->s[CM_STARTED]);
        if (r->active && (!event_filter[0] || cm_equal(event_filter, r->s[CM_EVENT])) &&
            (!revision_filter[0] || cm_equal(revision_filter, r->s[CM_TESTED_SHA])) &&
            (since < 0 || start >= since) && (until < 0 || start < until))
        {
            ordered[n++] = r;
            if (!cm_equal(r->s[CM_HARDWARE], "verified-startup")) ++missing_hardware;
            if (r->origin_job != r->job) ++aliases;
            else if (cm_hosted(r) && r->elapsed_available)
            {
                runner_seconds += (uint64_t)r->seconds;
                if (!cm_equal(r->s[CM_CONCLUSION], "success")) { waste_seconds += (uint64_t)r->seconds; ++failed; }
            }
            else if (cm_equal(r->s[CM_KIND], "executed")) ++costs_missing;
            uint64_t hash = cm_series_hash(r);
            unsigned found = count;
            for (unsigned k = 0; k < count; ++k)
            {
                if (series[k].hash == hash)
                {
                    if (cm_same_series(series[k].representative, r)) found = k;
                    else { valid = 0; ++store->gaps; }
                }
            }
            if (found == count)
            {
                if (count < CM_SERIES) series[count++] = (CmSeries){r, hash, 0, 0, 0};
                else { ++excluded; found = CM_SERIES; }
            }
            if (found < CM_SERIES)
            {
                ++series[found].raw;
                series[found].samples += cm_sample(r);
                series[found].missing += !cm_context(r);
            }
        }
    }
    if (valid) cm_order(ordered, scratch, n);
    FILE *index = valid ? tmpfile() : NULL, *csv = valid ? tmpfile() : NULL, *json = valid ? tmpfile() : NULL;
    valid = valid && index && csv && json;
    if (valid)
    {
        fputs("# Hosted CI timing history\n\n", index);
        fputs("Operational Actions observations; advisory. Compiler performance acceptance and the qualified 9700X dashboard are separate.\n\n", index);
        fputs("Collected at ", index); cm_escape(index, observed, 0);
        fprintf(index, ". Policy %s. %u active observations, %u unknown hardware, %u API/ingestion gaps, "
            "%u incomplete runs, %u aliases, %u unresolved cost intervals.\n\n",
            CM_POLICY, n, missing_hardware, store->gaps, store->incomplete_runs, aliases, costs_missing);
        fprintf(index, "Observed hosted runner-seconds: %" PRIu64 "; observed unsuccessful-job runner-seconds: %" PRIu64
            " (%u jobs). These exclude unknown-hosting and missing intervals; they are neither billed minutes nor process CPU time. "
            "Dependency/deployment waits and required-CI critical-path attribution remain unavailable.\n\n", runner_seconds, waste_seconds, failed);
        fprintf(index, "Reporting bounds: 30 observation-date shards / %u rows / %u rendered series; %u rows outside the rendered series bound remain in exports. "
            "Raw history is retained separately from these bounded derived reports.\n\n", CM_ROWS, CM_SERIES, excluded);
        fputs("[JSON export](recent.jsonl) · [CSV export](recent.csv) · [Collector policy](https://github.com/" CM_REPO
            "/blob/main/docs/ci-timing-history.md)\n\n", index);
        fputs("Default trusted-main view: select rows with event push and branch main below. PR/queue/manual rows retain their own events and origins. "
            "Use the native report command for explicit revision/date/event filters; no candidate chooses the published policy.\n\n", index);
        fputs("| Job / matrix | OS / actual CPU | Event / branch | Successful samples | Missing context | History |\n"
              "|---|---|---|---:|---:|---|\n", index);
        fputs("run_id,job_id,association_attempt,origin_job_id,original_attempt,api_elapsed_seconds", csv);
        for (unsigned k = 0; k < CM_FIELD_COUNT; ++k) { fputc(',', csv); cm_escape(csv, cm_fields[k], 1); }
        fputc('\n', csv);
        for (unsigned i = 0; i < n; ++i)
        {
            const CmRow *r = ordered[i]; cm_emit_row(json, r);
            fprintf(csv, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",",
                r->run, r->job, r->attempt, r->origin_job, r->origin_attempt);
            if (r->elapsed_available) fprintf(csv, "%" PRId64, r->seconds);
            for (unsigned k = 0; k < CM_FIELD_COUNT; ++k) { fputc(',', csv); cm_escape(csv, r->s[k], 1); }
            fputc('\n', csv);
        }
    }
    for (unsigned k = 0; valid && k < count; ++k)
    {
        const CmRow *r = series[k].representative;
        char path[128], anchor_path[128];
        snprintf(path, sizeof(path), "reports/series-%016" PRIx64 ".md", series[k].hash);
        snprintf(anchor_path, sizeof(anchor_path), "history/anchors/%016" PRIx64 ".jsonl", series[k].hash);
        const CmRow *recent[CM_WINDOW * 2], *anchor[CM_WINDOW];
        unsigned recent_count = 0, anchor_count = 0;
        CmStore anchor_store; int anchor_initialized = cm_store_init(&anchor_store);
        valid = anchor_initialized;
        int available = 0; char *retained = valid ? cm_data_read(t, anchor_path, &available) : NULL;
        if (available < 0) { valid = 0; ++store->gaps; }
        if (retained && valid) valid = cm_import(&anchor_store, retained, strlen(retained));
        for (unsigned i = 0; valid && i < anchor_store.count; ++i)
        {
            const CmRow *a = &anchor_store.rows[i];
            valid = anchor_count < CM_WINDOW && cm_sample(a) && cm_same_series(r, a);
            if (valid) anchor[anchor_count++] = a;
        }
        for (unsigned i = 0; valid && i < n; ++i)
        {
            const CmRow *sample = ordered[i];
            if (cm_sample(sample) && cm_same_series(r, sample))
            {
                if (recent_count == CM_WINDOW * 2)
                {
                    memmove(recent, recent + 1, (CM_WINDOW * 2 - 1) * sizeof(*recent));
                    --recent_count;
                }
                recent[recent_count++] = sample;
                if (anchor_count < CM_WINDOW)
                {
                    int seen = 0;
                    for (unsigned a = 0; a < anchor_count; ++a)
                        seen |= anchor[a]->run == sample->run && anchor[a]->job == sample->job;
                    if (!seen) anchor[anchor_count++] = sample;
                }
            }
        }
        unsigned candidate_count = recent_count < CM_MINIMUM ? recent_count : CM_MINIMUM;
        unsigned baseline_count = recent_count - candidate_count;
        if (baseline_count > CM_WINDOW) baseline_count = CM_WINDOW;
        const CmRow *const *candidate = recent + recent_count - candidate_count;
        const CmRow *const *baseline = recent + recent_count - candidate_count - baseline_count;
        const CmRow *fixed[CM_WINDOW]; unsigned fixed_count = 0;
        for (unsigned a = 0; a < anchor_count; ++a)
        {
            int overlap = 0;
            for (unsigned i = 0; i < candidate_count; ++i)
                overlap |= anchor[a]->run == candidate[i]->run && anchor[a]->job == candidate[i]->job;
            if (!overlap) fixed[fixed_count++] = anchor[a];
        }
        CmComparison trailing = cm_compare(baseline, baseline_count, candidate, candidate_count);
        CmComparison anchored = cm_compare(fixed, fixed_count, candidate, candidate_count);
        FILE *report = valid ? tmpfile() : NULL;
        valid = valid && report;
        if (valid)
        {
            fputs("| ", index); cm_escape(index, r->s[CM_NAME], 0); fputs(" / ", index); cm_escape(index, r->s[CM_MATRIX], 0);
            fputs(" | ", index); cm_escape(index, r->s[CM_OS][0] ? r->s[CM_OS] : "unknown OS", 0);
            fputs(" / ", index); cm_escape(index, r->s[CM_CPU][0] ? r->s[CM_CPU] : "unknown CPU", 0);
            fputs(" | ", index); cm_escape(index, r->s[CM_EVENT], 0); fputs(" / ", index); cm_escape(index, r->s[CM_HEAD_BRANCH], 0);
            fprintf(index, " | %u | %u | [series](series-%016" PRIx64 ".md) |\n", series[k].samples, series[k].missing, series[k].hash);
            fputs("# ", report); cm_escape(report, r->s[CM_NAME], 0); fputs("\n\n", report);
            fputs("[All series](index.md) · [JSON](recent.jsonl) · [CSV](recent.csv)\n\n", report);
            fprintf(report, "Policy %s; refresh ", CM_POLICY); cm_escape(report, observed, 0);
            fputs(". Cohort fields and missing values:\n\n| Field | Value |\n|---|---|\n", report);
            for (unsigned a = 0; a < sizeof(cm_series_fields) / sizeof(cm_series_fields[0]); ++a)
            {
                unsigned field = cm_series_fields[a];
                fputs("| ", report); cm_escape(report, cm_fields[field], 0); fputs(" | ", report);
                cm_escape(report, r->s[field][0] ? r->s[field] : "unavailable", 0); fputs(" |\n", report);
            }
            fputs("\nContext qualification: ", report); cm_escape(report, r->s[CM_CONTEXT_STATUS], 0);
            fputs(". Unknown critical context prevents change signals; raw points remain selectable. "
                "Workflow/blob, CPU/resources, image, cache, toolchain and workload changes create distinct boundaries. "
                "The tested SHA labels a point, rather than defining a separate cohort.\n\n", report);
            cm_comparison_text(report, "Trailing comparison", &trailing);
            cm_window(report, "Trailing baseline", baseline, baseline_count); cm_window(report, "Candidate", candidate, candidate_count);
            cm_comparison_text(report, "Cumulative change from the fixed initial anchor", &anchored);
            fputs("Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. "
                "Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.\n\n", report);
            cm_window(report, "Fixed anchor", fixed, fixed_count);
            fputs("These are descriptive CI signals, not independent significance tests or paired experiments. "
                "At least eight successful physical executions and three UTC date buckets per side are required. "
                "No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.\n\n", report);
            fputs("| Started | Outcome / population | Elapsed s | Source evidence |\n|---|---|---:|---|\n", report);
            unsigned raw_count = 0;
            for (unsigned i = 0; i < n; ++i)
            {
                const CmRow *point = ordered[i];
                if (cm_same_series(r, point) && ++raw_count <= 512)
                {
                    fputs("| ", report); cm_escape(report, point->s[CM_STARTED], 0); fputs(" | ", report);
                    cm_escape(report, point->s[CM_CONCLUSION], 0); fputs(" / ", report); cm_escape(report, point->s[CM_KIND], 0);
                    fputs(" / ", report); cm_escape(report, point->s[CM_ALIAS], 0); fputs(" | ", report);
                    if (point->elapsed_available) fprintf(report, "%" PRId64, point->seconds); else fputs("unavailable", report);
                    fputs(" | ", report); cm_point(report, point); fputs(" |\n", report);
                }
            }
            if (raw_count > 512) fputs("\nThis bounded view shows 512 raw points; exports retain all selected observations.\n", report);
            valid = cm_output(out, path, report);
            FILE *anchor_output = valid ? tmpfile() : NULL;
            if (anchor_output)
            {
                for (unsigned a = 0; a < anchor_count; ++a) cm_emit_row(anchor_output, anchor[a]);
                char *text = cm_memory(anchor_output); fclose(anchor_output);
                if (text && (!retained || !cm_equal(text, retained)))
                {
                    FILE *saved = tmpfile();
                    if (saved) { fputs(text, saved); valid = cm_output(out, anchor_path, saved); }
                    else valid = 0;
                }
                free(text);
            }
            else valid = 0;
        }
        cm_store_free(&anchor_store); free(retained);
    }
    if (valid)
    {
        valid = cm_output(out, "reports/index.md", index); index = NULL;
        valid &= cm_output(out, "reports/recent.csv", csv); csv = NULL;
        valid &= cm_output(out, "reports/recent.jsonl", json); json = NULL;
    }
    if (index) fclose(index);
    if (csv) fclose(csv);
    if (json) fclose(json);
    free(ordered); free(scratch);
    return valid;
}
#endif
