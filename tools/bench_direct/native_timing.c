// Informative native macOS/Windows hosted timing (#2761); never Zen 5 evidence.
// Entry: --self-test (synthetic, no network), or macos/windows with the run's
// downloaded evidence. The maintainer's disposition (2026-10-11): native timing
// runs on standard GitHub-hosted runners, never fails a check for a slow
// result, and stays visible as a ::warning::, a step-summary line and one
// tracking issue per platform. docs/performance-validation-v1.json records it
// as native-informative on route hosted-native.
// nt_macos classifies the existing bench_throughput_ci summary.json: a case is
// a regression only when the guard confirmed it in both rounds; its ratio is
// the geometric mean of the two rounds' median paired ratios, as in summary.md.
// nt_windows pairs each issue-708 trial's baseline/candidate matrix durations:
// a trial crosses the guard's wall margins (15% and 2 ms), a majority of
// crossing trials is a regression and a minority is inconclusive.
// Missing, malformed, partial or unrepresentable evidence exits 2; every
// complete verdict, a regression included, exits 0. nt_publish opens or
// comments on the platform's issue through the existing ci_metrics gh
// transport; workflows run it only in a trusted main job, never for pull
// request code or a --demonstrate run.
// Map: nt_number, nt_macos, nt_windows, nt_render, nt_publish, nt_main.
#include "../ci_metrics_io.h"

#define NT_CELLS 64u
#define NT_TRIALS 16u
#define NT_TEXT 65536u
#define NT_RESULT_BYTES 65536u
#define NT_ISSUE_PAGES 20u
#define NT_RELATIVE_MARGIN 0.15
#define NT_ABSOLUTE_SECONDS 0.002
#define NT_AUTHOR "github-actions[bot]"
#define NT_RESULT_SCHEMA "ISSUE_708_PAIRED_RESULT_V1"
#define NT_NOTICE "Informative only: GitHub-hosted runner timing, not Zen 5 evidence and never a " \
    "performance-validation claim or a failed check (#2761)."

typedef struct NtCell NtCell;
struct NtCell
{
    char name[128];
    const char *metric;
    double ratio;
    int regression, inconclusive, crossed;
};
typedef struct NtReport NtReport;
struct NtReport
{
    unsigned platform, count;
    int demonstration;
    const char *verdict;
    char baseline[41], candidate[41], error[256];
    NtCell cells[NT_CELLS];
};

BUSTER_GLOBAL_LOCAL const char *const nt_platform_names[] = {"macOS", "Windows"};
BUSTER_GLOBAL_LOCAL const char *const nt_platform_keys[] = {"macos", "windows"};
BUSTER_GLOBAL_LOCAL const char *const nt_workflows[] = {"throughput-harness-macos.yml", "issue-708-benchmark.yml"};

BUSTER_GLOBAL_LOCAL int nt_fail(NtReport *r, const char *message)
{
    if (!r->error[0]) cm_copy(r->error, sizeof(r->error), message);
    return 0;
}

// A JSON number token representable as a finite double; a huge or overflowing
// literal is refused rather than becoming infinity.
BUSTER_GLOBAL_LOCAL int nt_number(const CmJson *j, unsigned node, const char *key, double *out)
{
    unsigned member = cm_member(j, node, key);
    const char *text = cm_value(j, member);
    int valid = member && j->tokens[member].kind == 'v' && text[0] && !cm_equal(text, "null") &&
        !cm_equal(text, "true") && !cm_equal(text, "false");
    if (valid)
    {
        char *end = NULL;
        errno = 0;
        double value = strtod(text, &end);
        valid = end && !*end && errno != ERANGE && isfinite(value);
        if (valid) *out = value;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int nt_boolean(const CmJson *j, unsigned node, const char *key, int *out)
{
    unsigned member = cm_member(j, node, key);
    const char *text = cm_value(j, member);
    int valid = member && j->tokens[member].kind == 'v' && (cm_equal(text, "true") || cm_equal(text, "false"));
    if (valid) *out = cm_equal(text, "true");
    return valid;
}

BUSTER_GLOBAL_LOCAL int nt_count(const CmJson *j, unsigned node, const char *key, uint64_t *out)
{
    unsigned member = cm_member(j, node, key);
    int valid = member && j->tokens[member].kind == 'v' && cm_unsigned(cm_value(j, member), out);
    return valid;
}

BUSTER_GLOBAL_LOCAL int nt_string(const CmJson *j, unsigned node, const char *key, const char *expected)
{
    unsigned member = cm_member(j, node, key);
    int valid = member && j->tokens[member].kind == 's' && cm_equal(cm_value(j, member), expected);
    return valid;
}

BUSTER_GLOBAL_LOCAL int nt_cell_name(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    int valid = n >= 1 && n <= 127;
    for (size_t i = 0; valid && i < n; ++i)
    {
        char c = s[i];
        valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '.' || c == ':' || c == '+' || c == '/' || c == '-';
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int nt_start(NtReport *r, unsigned platform, const char *baseline, const char *candidate, int demonstrate)
{
    memset(r, 0, sizeof(*r));
    r->platform = platform;
    r->demonstration = demonstrate;
    int valid = cm_sha(baseline) && cm_sha(candidate);
    if (valid)
    {
        cm_copy(r->baseline, sizeof(r->baseline), baseline);
        cm_copy(r->candidate, sizeof(r->candidate), candidate);
    }
    else nt_fail(r, "baseline and candidate must be full lowercase commit SHAs");
    return valid;
}

BUSTER_GLOBAL_LOCAL void nt_verdict(NtReport *r)
{
    int regression = 0, inconclusive = 0;
    for (unsigned i = 0; i < r->count; ++i)
    {
        regression |= r->cells[i].regression;
        inconclusive |= r->cells[i].inconclusive;
    }
    r->verdict = regression ? "regression" : inconclusive ? "inconclusive" : "no regression";
}

// The guard exits 1 exactly for a complete report with confirmed regressions.
BUSTER_GLOBAL_LOCAL int nt_macos(NtReport *r, const char *text, size_t n, int driver_exit)
{
    CmJson j = cm_json_parse(text ? text : "", text ? n : 0);
    unsigned comparisons = cm_member(&j, 1, "comparisons");
    int valid_flag = 0, guard = 0;
    int valid = j.valid && j.tokens[1].kind == 'o' && nt_boolean(&j, 1, "valid", &valid_flag) && valid_flag &&
        nt_boolean(&j, 1, "guard_enabled", &guard) && guard && comparisons && j.tokens[comparisons].kind == 'a' &&
        j.tokens[comparisons].child;
    if (!valid) nt_fail(r, "summary.json is missing, invalid, unguarded or empty; no timing verdict");
    uint64_t confirmed_total = 0, inconclusive_total = 0, counted = 0, inconclusive_counted = 0;
    for (unsigned c = valid ? j.tokens[comparisons].child : 0; valid && c; c = j.tokens[c].next)
    {
        unsigned name = cm_member(&j, c, "name"), tests = cm_member(&j, c, "tests"), decision = cm_member(&j, c, "decision");
        const char *decided = cm_value(&j, decision);
        int is_regression = cm_equal(decided, "regression"), is_inconclusive = cm_equal(decided, "inconclusive");
        valid = j.tokens[c].kind == 'o' && name && j.tokens[name].kind == 's' && nt_cell_name(cm_value(&j, name)) &&
            decision && j.tokens[decision].kind == 's' &&
            (is_regression || is_inconclusive || cm_equal(decided, "no substantial regression detected")) &&
            tests && j.tokens[tests].kind == 'a' && r->count < NT_CELLS;
        if (!valid) nt_fail(r, "summary.json has a malformed comparison");
        double ratios[2][2] = {{0}};
        int regressions[2][2] = {{0}}, seen[2][2] = {{0}}, tests_seen = 0;
        for (unsigned t = valid ? j.tokens[tests].child : 0; valid && t; t = j.tokens[t].next)
        {
            unsigned metric = cm_member(&j, t, "metric");
            const char *metric_name = cm_value(&j, metric);
            int m = cm_equal(metric_name, "wall_seconds") ? 0 : cm_equal(metric_name, "peak_rss_bytes") ? 1 : -1;
            uint64_t round = 2;
            double ratio = 0;
            int regressed = 0;
            valid = j.tokens[t].kind == 'o' && metric && j.tokens[metric].kind == 's' && m >= 0 &&
                nt_count(&j, t, "round", &round) && round <= 1 && !seen[m][round] &&
                nt_number(&j, t, "median_ratio", &ratio) && ratio > 0 && nt_boolean(&j, t, "regression", &regressed);
            if (valid)
            {
                seen[m][round] = 1;
                ratios[m][round] = ratio;
                regressions[m][round] = regressed;
                ++tests_seen;
            }
            else nt_fail(r, "summary.json has a malformed guard test");
        }
        valid = valid && tests_seen == 4;
        if (!valid) nt_fail(r, "both rounds of both guarded metrics are required");
        int confirmed_metric = valid ? (regressions[0][0] && regressions[0][1] ? 0 : regressions[1][0] && regressions[1][1] ? 1 : -1) : -1;
        valid = valid && is_regression == (confirmed_metric >= 0);
        if (!valid) nt_fail(r, "a decision disagrees with its per-round results");
        if (valid)
        {
            int m = confirmed_metric >= 0 ? confirmed_metric : 0;
            // Split the geometric mean so two large finite ratios cannot overflow.
            double ratio = sqrt(ratios[m][0]) * sqrt(ratios[m][1]);
            valid = isfinite(ratio) && ratio > 0;
            if (!valid) nt_fail(r, "a ratio is not a representable positive number");
            NtCell *cell = &r->cells[r->count];
            cm_copy(cell->name, sizeof(cell->name), cm_value(&j, name));
            cell->metric = m ? "peak_rss_bytes" : "wall_seconds";
            cell->ratio = ratio;
            cell->regression = confirmed_metric >= 0;
            cell->inconclusive = is_inconclusive;
            counted += (uint64_t)cell->regression;
            inconclusive_counted += (uint64_t)is_inconclusive;
            r->count += (unsigned)valid;
        }
    }
    valid = valid && nt_count(&j, 1, "confirmed_regressions", &confirmed_total) &&
        nt_count(&j, 1, "inconclusive_cases", &inconclusive_total) &&
        confirmed_total == counted && inconclusive_total == inconclusive_counted;
    if (!valid) nt_fail(r, "summary.json totals disagree with its comparisons");
    valid = valid && driver_exit == (counted ? 1 : 0);
    if (!valid) nt_fail(r, "bench_throughput_ci exited with a status inconsistent with its counted regressions");
    if (valid && r->demonstration)
    {
        unsigned forced = 0;
        for (unsigned i = 1; i < r->count; ++i)
            if (r->cells[i].ratio > r->cells[forced].ratio) forced = i;
        r->cells[forced].regression = 1;
        r->cells[forced].inconclusive = 0;
        r->cells[forced].metric = "wall_seconds";
    }
    if (valid) nt_verdict(r);
    cm_json_free(&j);
    return valid;
}

// Windows PowerShell's Tee-Object writes UTF-16LE with a byte-order mark; the
// result is ASCII JSON, so any other code unit is malformed evidence.
BUSTER_GLOBAL_LOCAL int nt_ascii(char *text, size_t *n)
{
    int valid = 1;
    const unsigned char *bytes = (const unsigned char *)text;
    if (*n >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    {
        size_t out = 0;
        valid = *n % 2 == 0;
        for (size_t i = 2; valid && i < *n; i += 2)
        {
            valid = bytes[i] < 0x80 && bytes[i + 1] == 0;
            if (valid) text[out++] = (char)bytes[i];
        }
        *n = out;
        text[out] = 0;
    }
    else if (*n >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
    {
        memmove(text, text + 3, *n - 3);
        *n -= 3;
        text[*n] = 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int nt_windows(NtReport *r, const char *evidence, uint64_t run, uint64_t attempt, uint64_t trials)
{
    int valid = run && attempt && trials >= 1 && trials <= NT_TRIALS;
    if (!valid) nt_fail(r, "run id, attempt and a trial count of 1-16 are required");
    unsigned crossed = 0;
    for (uint64_t trial = 1; valid && trial <= trials; ++trial)
    {
        char path[4096];
        snprintf(path, sizeof(path), "%s/issue-708-pair-%" PRIu64 "-%" PRIu64 "-%" PRIu64 "/pair-validation.txt",
            evidence, trial, run, attempt);
        FILE *validation = fopen(path, "rb");
        valid = validation != NULL;
        if (validation) fclose(validation);
        if (!valid) nt_fail(r, "a trial has no pair validation; the pair did not complete");
        double durations[2] = {0};
        const char *const arms[] = {"baseline", "candidate"};
        const char *const shas[] = {r->baseline, r->candidate};
        for (unsigned arm = 0; valid && arm < 2; ++arm)
        {
            snprintf(path, sizeof(path), "%s/issue-708-pair-%" PRIu64 "-%" PRIu64 "-%" PRIu64 "/%s/buster-ci/benchmark-result.json",
                evidence, trial, run, attempt, arms[arm]);
            size_t n = 0;
            char *text = cm_read(path, NT_RESULT_BYTES, &n);
            valid = text && nt_ascii(text, &n);
            CmJson j = cm_json_parse(valid ? text : "", valid ? n : 0);
            uint64_t trial_seen = 0;
            double seconds = 0;
            valid = valid && j.valid && j.tokens[1].kind == 'o' && nt_string(&j, 1, "schema", NT_RESULT_SCHEMA) &&
                nt_string(&j, 1, "arm", arms[arm]) && nt_count(&j, 1, "trial", &trial_seen) && trial_seen == trial &&
                nt_string(&j, 1, "source_sha", shas[arm]) && nt_string(&j, 1, "expected_sha", shas[arm]);
            if (!valid) nt_fail(r, "a result does not identify its arm, trial and revision");
            uint64_t matrix_exit = 1, summary_exit = 1;
            valid = valid && nt_count(&j, 1, "exit_code", &matrix_exit) && matrix_exit == 0 &&
                nt_count(&j, 1, "summary_exit_code", &summary_exit) && summary_exit == 0;
            if (!valid) nt_fail(r, "the matrix or its coverage summary failed");
            valid = valid && nt_number(&j, 1, "duration_seconds", &seconds) && seconds > 0;
            if (!valid) nt_fail(r, "an arm has no measured duration");
            durations[arm] = seconds;
            cm_json_free(&j);
            free(text);
        }
        if (valid)
        {
            if (r->demonstration) durations[1] *= 2.0;
            double ratio = durations[1] / durations[0];
            valid = isfinite(ratio) && ratio > 0;
            if (!valid) nt_fail(r, "a candidate/baseline ratio is not a representable positive number");
            NtCell *cell = &r->cells[r->count];
            snprintf(cell->name, sizeof(cell->name), "test_all_combinations_ci trial %" PRIu64, trial);
            cell->metric = "wall_seconds";
            cell->ratio = ratio;
            cell->crossed = ratio > 1.0 + NT_RELATIVE_MARGIN && durations[1] - durations[0] > NT_ABSOLUTE_SECONDS;
            crossed += (unsigned)cell->crossed;
            r->count += (unsigned)valid;
        }
    }
    for (unsigned i = 0; valid && i < r->count; ++i)
    {
        r->cells[i].regression = r->cells[i].crossed && 2 * crossed > trials;
        r->cells[i].inconclusive = r->cells[i].crossed && 2 * crossed <= trials;
    }
    if (valid) nt_verdict(r);
    return valid;
}

BUSTER_GLOBAL_LOCAL void nt_render(FILE *f, const NtReport *r, const char *run_url)
{
    fprintf(f, "Native %s hosted timing (`%s`): **%s**%s.\n\n", nt_platform_names[r->platform], nt_workflows[r->platform],
        r->verdict, r->demonstration ? " (DEMONSTRATION: a forced regression, not a measurement)" : "");
    if (run_url && run_url[0]) fprintf(f, "Run: %s\n", run_url);
    fprintf(f, "Commit: `%s` against baseline `%s`.\n\n| Cell | Metric | Candidate/baseline | Regression |\n"
        "| --- | --- | ---: | --- |\n", r->candidate, r->baseline);
    for (unsigned i = 0; i < r->count; ++i)
    {
        const NtCell *c = &r->cells[i];
        fprintf(f, "| %s | %s | %.4f | %s |\n", c->name, c->metric, c->ratio,
            c->regression ? "yes" : c->inconclusive ? "inconclusive" : "no");
    }
    fprintf(f, "\n" NT_NOTICE "\n");
}

BUSTER_GLOBAL_LOCAL void nt_annotate(FILE *f, const NtReport *r)
{
    for (unsigned i = 0; i < r->count; ++i)
    {
        const NtCell *c = &r->cells[i];
        if (c->regression)
            fprintf(f, "::warning title=%sNative %s timing regression (informative)::%s %s candidate/baseline %.4f "
                "on a GitHub-hosted runner; not Zen 5 evidence and never a gate.\n",
                r->demonstration ? "DEMONSTRATION " : "", nt_platform_names[r->platform], c->name, c->metric, c->ratio);
    }
    if (cm_equal(r->verdict, "inconclusive"))
        fprintf(f, "::warning title=Native %s timing inconclusive (informative)::Some cells crossed the margin without "
            "confirmation; no equivalence claim.\n", nt_platform_names[r->platform]);
}

// The exact attempt URL of a run in this repository.
BUSTER_GLOBAL_LOCAL int nt_run_url(const char *url)
{
    const char *prefix = "https://github.com/" CM_REPO "/actions/runs/";
    size_t n = strlen(prefix);
    int valid = url && strncmp(url, prefix, n) == 0;
    const char *p = valid ? url + n : "";
    valid = valid && *p >= '1' && *p <= '9';
    while (valid && *p >= '0' && *p <= '9') ++p;
    valid = valid && strncmp(p, "/attempts/", 10) == 0;
    p += valid ? 10 : 0;
    valid = valid && *p >= '1' && *p <= '9';
    while (valid && *p >= '0' && *p <= '9') ++p;
    valid = valid && !*p;
    return valid;
}

BUSTER_GLOBAL_LOCAL void nt_title(char *out, size_t capacity, unsigned platform)
{
    snprintf(out, capacity, "Native hosted timing regression: %s (%s)", nt_platform_names[platform], nt_workflows[platform]);
}

BUSTER_GLOBAL_LOCAL void nt_marker(char *out, size_t capacity, unsigned platform)
{
    snprintf(out, capacity, "<!-- native-hosted-timing platform=%s -->", nt_platform_keys[platform]);
}

// Open the platform's tracking issue, or comment on the open one. The author is
// rechecked so an ignored filter cannot adopt another issue.
BUSTER_GLOBAL_LOCAL int nt_publish(CmTransport *t, NtReport *r, const char *run_url, char *action, size_t capacity)
{
    int valid = !r->demonstration;
    if (!valid) nt_fail(r, "a demonstration never files or updates an issue");
    valid = valid && nt_run_url(run_url);
    if (!valid) nt_fail(r, "publishing needs this repository's exact run attempt URL");
    cm_copy(action, capacity, "none");
    if (valid && cm_equal(r->verdict, "regression"))
    {
        char title[256], marker[128], path[256];
        nt_title(title, sizeof(title), r->platform);
        nt_marker(marker, sizeof(marker), r->platform);
        uint64_t existing = 0;
        int ended = 0;
        for (unsigned page = 1; valid && !ended; ++page)
        {
            valid = page <= NT_ISSUE_PAGES;
            if (!valid) nt_fail(r, "too many open issues to find the tracking issue");
            snprintf(path, sizeof(path), "issues?state=open&creator=github-actions%%5Bbot%%5D&per_page=100&page=%u", page);
            CmJson rows = valid ? cm_api_json(t, path, "GET", NULL, NULL) : (CmJson){0};
            valid = valid && rows.valid && rows.tokens[1].kind == 'a';
            if (!valid) nt_fail(r, "malformed or unavailable issue listing");
            unsigned listed = 0;
            for (unsigned row = valid ? rows.tokens[1].child : 0; row; row = rows.tokens[row].next)
            {
                unsigned user = cm_member(&rows, row, "user");
                uint64_t number = 0;
                ++listed;
                if (rows.tokens[row].kind == 'o' && !cm_member(&rows, row, "pull_request") &&
                    nt_string(&rows, row, "title", title) && nt_string(&rows, user, "login", NT_AUTHOR) &&
                    strstr(cm_get(&rows, row, "body"), marker) && nt_count(&rows, row, "number", &number) && number &&
                    (!existing || number < existing))
                    existing = number;
            }
            ended = listed < 100;
            cm_json_free(&rows);
        }
        FILE *report = valid ? tmpfile() : NULL, *payload = valid ? tmpfile() : NULL;
        valid = valid && report && payload;
        if (valid)
        {
            if (!existing)
                fprintf(report, "%s\nOne tracking issue per platform: each later regression on a trusted `main` run adds a "
                    "comment. Inspect the linked run's artifacts; close this issue once the regression is explained or "
                    "fixed. See docs/agents/benchmarking.md#native-hosted-timing.\n\n", marker);
            nt_render(report, r, run_url);
        }
        char *text = valid ? cm_memory(report) : NULL;
        valid = valid && text;
        if (valid)
        {
            fputc('{', payload);
            if (!existing)
            {
                fputs("\"title\":", payload);
                cm_quote(payload, title);
                fputc(',', payload);
            }
            fputs("\"body\":", payload);
            cm_quote(payload, text);
            fputc('}', payload);
        }
        char *body = valid ? cm_memory(payload) : NULL;
        valid = valid && body;
        if (valid && existing)
        {
            snprintf(path, sizeof(path), "issues/%" PRIu64 "/comments", existing);
            CmJson written = cm_api_json(t, path, "POST", body, NULL);
            valid = written.valid;
            if (valid) snprintf(action, capacity, "updated #%" PRIu64, existing);
            cm_json_free(&written);
        }
        else if (valid)
        {
            CmJson created = cm_api_json(t, "issues", "POST", body, NULL);
            uint64_t number = 0;
            valid = created.valid && nt_count(&created, 1, "number", &number) && number;
            if (valid) snprintf(action, capacity, "opened #%" PRIu64, number);
            cm_json_free(&created);
        }
        if (!valid) nt_fail(r, "the tracking issue could not be opened or updated");
        free(body);
        free(text);
        if (report) fclose(report);
        if (payload) fclose(payload);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL void nt_append(const char *variable, const char *text)
{
    const char *path = getenv(variable);
    FILE *f = path && path[0] ? fopen(path, "ab") : NULL;
    if (f)
    {
        fputs(text, f);
        fclose(f);
    }
}

#include "native_timing_test.h"

BUSTER_GLOBAL_LOCAL int nt_main(int argc, char **argv)
{
    int result = 2;
    unsigned platform = argc >= 2 && cm_equal(argv[1], "windows") ? 1u : 0u;
    int valid = argc >= 2 && (cm_equal(argv[1], "macos") || cm_equal(argv[1], "windows"));
    const char *baseline = "", *candidate = "", *summary = "", *driver_exit = "", *evidence = "", *run_url = "";
    uint64_t run = 0, attempt = 0, trials = 0;
    int demonstrate = 0, annotate = 0, publish = 0;
    for (int i = 2; valid && i < argc; ++i)
    {
        const char *key = argv[i], *value = i + 1 < argc ? argv[i + 1] : NULL;
        int flag = cm_equal(key, "--demonstrate") || cm_equal(key, "--annotate") || cm_equal(key, "--publish-issue");
        demonstrate |= cm_equal(key, "--demonstrate");
        annotate |= cm_equal(key, "--annotate");
        publish |= cm_equal(key, "--publish-issue");
        valid = flag || value;
        if (valid && !flag)
        {
            ++i;
            if (cm_equal(key, "--baseline")) baseline = value;
            else if (cm_equal(key, "--candidate")) candidate = value;
            else if (cm_equal(key, "--run-url")) run_url = value;
            else if (cm_equal(key, "--summary") && !platform) summary = value;
            else if (cm_equal(key, "--driver-exit") && !platform) driver_exit = value;
            else if (cm_equal(key, "--evidence") && platform) evidence = value;
            else if (cm_equal(key, "--run-id") && platform) valid = cm_unsigned(value, &run);
            else if (cm_equal(key, "--attempt") && platform) valid = cm_unsigned(value, &attempt);
            else if (cm_equal(key, "--trials") && platform) valid = cm_unsigned(value, &trials);
            else valid = 0;
        }
    }
    if (!valid)
        fputs("usage: native-timing --self-test | macos --summary FILE --driver-exit FILE | windows --evidence DIR "
            "--run-id N --attempt N --trials N; both take --baseline SHA --candidate SHA [--run-url URL] "
            "[--demonstrate] [--annotate | --publish-issue]\n", stderr);
    else
    {
        NtReport report;
        int ok = nt_start(&report, platform, baseline, candidate, demonstrate);
        if (ok && !platform)
        {
            size_t n = 0, exit_length = 0;
            char *text = cm_read(summary, CM_BYTES, &n), *status = cm_read(driver_exit, 64, &exit_length);
            uint64_t code = 0;
            size_t digits = 0;
            while (status && status[digits] >= '0' && status[digits] <= '9') ++digits;
            if (status) status[digits] = 0;
            ok = text && status && cm_unsigned(status, &code) && code <= 255;
            if (!ok) nt_fail(&report, "no complete comparison: the summary or driver exit status is unreadable");
            ok = ok && nt_macos(&report, text, n, (int)code);
            free(text);
            free(status);
        }
        else if (ok) ok = nt_windows(&report, evidence, run, attempt, trials);
        char action[64] = "", *text = NULL;
        FILE *markdown = ok ? tmpfile() : NULL;
        if (ok && publish)
        {
            CmTransport transport = {0};
            transport.collection_started = cm_clock();
            transport.deadline = cm_clock() + 120;
            transport.request_limit = NT_ISSUE_PAGES + 2;
            const char *token = getenv("GH_TOKEN");
            ok = token && token[0] && cm_equal(getenv("GITHUB_REPOSITORY"), CM_REPO);
            if (!ok) nt_fail(&report, "publishing the tracking issue requires GH_TOKEN in " CM_REPO);
            ok = ok && nt_publish(&transport, &report, run_url, action, sizeof(action));
        }
        else if (ok && annotate) nt_annotate(stdout, &report);
        if (ok && markdown)
        {
            nt_render(markdown, &report, nt_run_url(run_url) ? run_url : "");
            if (publish) fprintf(markdown, "\nTracking issue: %s\n", action);
            text = cm_memory(markdown);
            ok = text != NULL;
        }
        if (ok)
        {
            char line[64];
            fputs(text, stdout);
            nt_append("GITHUB_STEP_SUMMARY", text);
            snprintf(line, sizeof(line), "verdict=%s\n", report.verdict);
            nt_append("GITHUB_OUTPUT", line);
            result = 0;
        }
        else printf("::error title=Native %s timing incomplete::%s\n", nt_platform_names[platform],
            report.error[0] ? report.error : "the report could not be written");
        free(text);
        if (markdown) fclose(markdown);
    }
    return result;
}

int main(int argc, char **argv)
{
    int result = argc == 2 && cm_equal(argv[1], "--self-test") ? nt_self_test() : nt_main(argc, argv);
    return result;
}
