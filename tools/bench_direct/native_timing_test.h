// Synthetic controls for native_timing.c (#2761); no network, no real runs.
// nt_test_summary builds guard summaries, nt_test_evidence writes issue-708
// trial results (UTF-16LE like Windows PowerShell, or UTF-8), and fixture
// transports replay issue listings so nt_publish never reaches GitHub.
// Map: nt_test_summary, nt_test_macos, nt_test_windows, nt_test_publish, nt_self_test.
#ifndef BUSTER_NATIVE_TIMING_TEST_H
#define BUSTER_NATIVE_TIMING_TEST_H

#define NT_BASE "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define NT_HEAD "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define NT_RUN_URL "https://github.com/" CM_REPO "/actions/runs/7/attempts/1"
#define NT_CHECK(condition, label) nt_test_check(&failures, &checks, (condition), (label))

typedef struct NtTestCase NtTestCase;
struct NtTestCase
{
    const char *name, *wall0, *wall1, *decision;
    int regression0, regression1;
};

BUSTER_GLOBAL_LOCAL void nt_test_check(unsigned *failures, unsigned *checks, int condition, const char *label)
{
    ++*checks;
    if (!condition)
    {
        ++*failures;
        fprintf(stderr, "NATIVE_TIMING_SELF_TEST_FAIL %s\n", label);
    }
}

// A guard summary whose totals are computed from the cases unless overridden.
BUSTER_GLOBAL_LOCAL char *nt_test_summary(const NtTestCase *cases, unsigned count, const char *valid, int confirmed_override)
{
    FILE *f = tmpfile();
    unsigned confirmed = 0, inconclusive = 0;
    fprintf(f, "{\"schema\":1,\"guard_enabled\":true,\"comparisons\":[");
    for (unsigned i = 0; i < count; ++i)
    {
        const NtTestCase *c = &cases[i];
        confirmed += cm_equal(c->decision, "regression");
        inconclusive += cm_equal(c->decision, "inconclusive");
        fprintf(f, "%s{\"name\":\"%s\",\"tests\":[{\"metric\":\"wall_seconds\",\"round\":0,\"median_ratio\":%s,\"regression\":%s},"
            "{\"metric\":\"wall_seconds\",\"round\":1,\"median_ratio\":%s,\"regression\":%s},"
            "{\"metric\":\"peak_rss_bytes\",\"round\":0,\"median_ratio\":1.0,\"regression\":false},"
            "{\"metric\":\"peak_rss_bytes\",\"round\":1,\"median_ratio\":1.0,\"regression\":false}],\"decision\":\"%s\"}",
            i ? "," : "", c->name, c->wall0, c->regression0 ? "true" : "false", c->wall1, c->regression1 ? "true" : "false", c->decision);
    }
    fprintf(f, "],\"telemetry\":[],\"confirmed_regressions\":%d,\"inconclusive_cases\":%u,\"valid\":%s}",
        confirmed_override >= 0 ? confirmed_override : (int)confirmed, inconclusive, valid);
    char *result = cm_memory(f);
    fclose(f);
    return result;
}

BUSTER_GLOBAL_LOCAL int nt_test_macos(NtReport *r, const char *text, int driver_exit, int demonstrate)
{
    int result = nt_start(r, 0, NT_BASE, NT_HEAD, demonstrate) && nt_macos(r, text, text ? strlen(text) : 0, driver_exit);
    return result;
}

BUSTER_GLOBAL_LOCAL void nt_test_macos_cases(unsigned *failures_out, unsigned *checks_out)
{
    unsigned failures = 0, checks = 0;
    const NtTestCase ok = {"tiny_startup/fast", "1.01", "0.99", "no substantial regression detected", 0, 0};
    const NtTestCase regress = {"macros/fast", "1.30", "1.20", "regression", 1, 1};
    const NtTestCase unsure = {"macros/quality", "1.2", "1.0", "inconclusive", 1, 0};
    NtReport r;
    char *text = nt_test_summary(&ok, 1, "true", -1);
    NT_CHECK(nt_test_macos(&r, text, 0, 0) && cm_equal(r.verdict, "no regression"), "macOS clean run");
    free(text);
    NtTestCase pair[2] = {ok, regress};
    text = nt_test_summary(pair, 2, "true", -1);
    NT_CHECK(nt_test_macos(&r, text, 1, 0) && cm_equal(r.verdict, "regression") && r.cells[1].regression &&
        fabs(r.cells[1].ratio - sqrt(1.30 * 1.20)) < 1e-12 && cm_equal(r.cells[1].name, "macros/fast"),
        "macOS confirmed regression keeps its cell and geometric ratio");
    NT_CHECK(!nt_test_macos(&r, text, 2, 0), "macOS crash exit with a regression report fails");
    NT_CHECK(!nt_test_macos(&r, text, 0, 0), "macOS success exit with a regression fails");
    free(text);
    text = nt_test_summary(&unsure, 1, "true", -1);
    NT_CHECK(nt_test_macos(&r, text, 0, 0) && cm_equal(r.verdict, "inconclusive"), "macOS inconclusive warns without regression");
    free(text);
    text = nt_test_summary(&ok, 1, "true", -1);
    NT_CHECK(!nt_test_macos(&r, text, 1, 0), "macOS failure exit without a regression fails");
    free(text);
    text = nt_test_summary(&regress, 1, "false", -1);
    NT_CHECK(!nt_test_macos(&r, text, 1, 0), "macOS invalid bundle fails");
    free(text);
    text = nt_test_summary(&regress, 1, "true", 0);
    NT_CHECK(!nt_test_macos(&r, text, 1, 0), "macOS totals that disagree fail");
    free(text);
    NtTestCase contradicted = regress;
    contradicted.decision = "no substantial regression detected";
    text = nt_test_summary(&contradicted, 1, "true", -1);
    NT_CHECK(!nt_test_macos(&r, text, 0, 0), "macOS decision contradicting its rounds fails");
    free(text);
    text = nt_test_summary(&ok, 0, "true", -1);
    NT_CHECK(!nt_test_macos(&r, text, 0, 0), "macOS summary without comparisons fails");
    free(text);
    NT_CHECK(!nt_test_macos(&r, "{\"valid\":true", 0, 0), "macOS truncated JSON fails");
    NT_CHECK(!nt_test_macos(&r, "", 0, 0), "macOS empty summary fails");
    const char *missing_round = "{\"guard_enabled\":true,\"comparisons\":[{\"name\":\"a/fast\",\"tests\":["
        "{\"metric\":\"wall_seconds\",\"round\":0,\"median_ratio\":1.0,\"regression\":false}],"
        "\"decision\":\"no substantial regression detected\"}],\"confirmed_regressions\":0,\"inconclusive_cases\":0,\"valid\":true}";
    NT_CHECK(!nt_test_macos(&r, missing_round, 0, 0), "macOS missing round fails");
    char huge[512];
    memset(huge, '0', 401);
    huge[0] = '1';
    huge[401] = 0;
    NtTestCase extreme = {"macros/fast", huge, "1.0", "no substantial regression detected", 0, 0};
    text = nt_test_summary(&extreme, 1, "true", -1);
    NT_CHECK(!nt_test_macos(&r, text, 0, 0), "macOS 401-digit integer ratio is refused, not raised");
    free(text);
    extreme.wall0 = "1e400";
    text = nt_test_summary(&extreme, 1, "true", -1);
    NT_CHECK(!nt_test_macos(&r, text, 0, 0), "macOS overflowing exponent ratio is refused");
    free(text);
    extreme.wall0 = "\"1.0\"";
    text = nt_test_summary(&extreme, 1, "true", -1);
    NT_CHECK(!nt_test_macos(&r, text, 0, 0), "macOS string ratio is refused");
    free(text);
    NtTestCase large = {"macros/fast", "1e308", "1e308", "regression", 1, 1};
    text = nt_test_summary(&large, 1, "true", -1);
    NT_CHECK(nt_test_macos(&r, text, 1, 0) && r.cells[0].ratio == 1e308, "macOS large finite ratios do not overflow their mean");
    free(text);
    NtTestCase quiet[2] = {{"a/fast", "1.0", "1.0", "no substantial regression detected", 0, 0},
                           {"b/fast", "1.1", "1.0", "no substantial regression detected", 0, 0}};
    text = nt_test_summary(quiet, 2, "true", -1);
    NT_CHECK(nt_test_macos(&r, text, 0, 1) && cm_equal(r.verdict, "regression") && r.demonstration &&
        r.cells[1].regression && !r.cells[0].regression, "macOS demonstration forces the highest wall ratio");
    free(text);
    NT_CHECK(!nt_start(&r, 0, "main", NT_HEAD, 0), "a symbolic baseline is refused");
    *failures_out += failures;
    *checks_out += checks;
}

BUSTER_GLOBAL_LOCAL void nt_test_write(const char *path, const char *text, int utf16)
{
    FILE *f = fopen(path, "wb");
    if (f)
    {
        if (utf16)
        {
            fputc(0xff, f);
            fputc(0xfe, f);
            for (size_t i = 0; text[i]; ++i)
            {
                fputc(text[i], f);
                fputc(0, f);
            }
        }
        else fputs(text, f);
        fclose(f);
    }
}

BUSTER_GLOBAL_LOCAL void nt_test_mkdir(const char *path)
{
    char copy[4096];
    cm_copy(copy, sizeof(copy), path);
    for (char *p = copy + 1; *p; ++p)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(copy, 0700);
            *p = '/';
        }
    }
    mkdir(copy, 0700);
}

// Three trials under ROOT; OVERRIDE_TRIAL's OVERRIDE_ARM field gets OVERRIDE_JSON
// spliced in place of its exit code when nonzero.
BUSTER_GLOBAL_LOCAL void nt_test_evidence(const char *root, const char *const durations[3][2], int utf16,
    unsigned override_trial, unsigned override_arm, const char *override_json, unsigned skip_validation)
{
    const char *const arms[] = {"baseline", "candidate"};
    const char *const shas[] = {NT_BASE, NT_HEAD};
    for (unsigned trial = 1; trial <= 3; ++trial)
    {
        char path[4096], text[2048];
        for (unsigned arm = 0; arm < 2; ++arm)
        {
            snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1/%s/buster-ci", root, trial, arms[arm]);
            nt_test_mkdir(path);
            snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1/%s/buster-ci/benchmark-result.json", root, trial, arms[arm]);
            int overridden = trial == override_trial && arm == override_arm;
            snprintf(text, sizeof(text), "{\r\n    \"schema\":  \"" NT_RESULT_SCHEMA "\",\r\n    \"source_sha\":  \"%s\",\r\n"
                "    \"expected_sha\":  \"%s\",\r\n    \"arm\":  \"%s\",\r\n    \"trial\":  %u,\r\n    \"duration_seconds\":  %s,\r\n"
                "    %s,\r\n    \"summary_exit_code\":  0\r\n}\r\n", shas[arm], shas[arm], arms[arm], trial,
                durations[trial - 1][arm], overridden ? override_json : "\"exit_code\":  0");
            nt_test_write(path, text, utf16);
        }
        if (trial != skip_validation)
        {
            snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1/pair-validation.txt", root, trial);
            nt_test_write(path, "Both arms succeeded.\n", 0);
        }
    }
}

// Remove exactly the layout nt_test_evidence writes.
BUSTER_GLOBAL_LOCAL void nt_test_remove(const char *root)
{
    const char *const arms[] = {"baseline", "candidate"};
    for (unsigned trial = 1; trial <= 3; ++trial)
    {
        char path[4096];
        for (unsigned arm = 0; arm < 2; ++arm)
        {
            snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1/%s/buster-ci/benchmark-result.json", root, trial, arms[arm]);
            unlink(path);
            snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1/%s/buster-ci", root, trial, arms[arm]);
            rmdir(path);
            snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1/%s", root, trial, arms[arm]);
            rmdir(path);
        }
        snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1/pair-validation.txt", root, trial);
        unlink(path);
        snprintf(path, sizeof(path), "%s/issue-708-pair-%u-7-1", root, trial);
        rmdir(path);
    }
    if (rmdir(root) != 0) fprintf(stderr, "NATIVE_TIMING_SELF_TEST could not remove %s\n", root);
}

BUSTER_GLOBAL_LOCAL int nt_test_windows(NtReport *r, const char *const durations[3][2], int utf16, unsigned override_trial,
    unsigned override_arm, const char *override_json, unsigned skip_validation, int demonstrate)
{
    char root[] = "/tmp/buster-native-timing-XXXXXX";
    int result = mkdtemp(root) != NULL;
    if (result)
    {
        nt_test_evidence(root, durations, utf16, override_trial, override_arm, override_json, skip_validation);
        result = nt_start(r, 1, NT_BASE, NT_HEAD, demonstrate) && nt_windows(r, root, 7, 1, 3);
        nt_test_remove(root);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void nt_test_windows_cases(unsigned *failures_out, unsigned *checks_out)
{
    unsigned failures = 0, checks = 0;
    NtReport r;
    const char *const majority[3][2] = {{"600.0", "720.0"}, {"600.0", "700.0"}, {"600.0", "610.0"}};
    NT_CHECK(nt_test_windows(&r, majority, 1, 0, 0, NULL, 0, 0) && cm_equal(r.verdict, "regression") &&
        r.cells[0].regression && r.cells[1].regression && !r.cells[2].regression && fabs(r.cells[0].ratio - 1.2) < 1e-12,
        "Windows majority of crossing trials is a regression (UTF-16LE results)");
    const char *const minority[3][2] = {{"600.0", "720.0"}, {"600.0", "600.0"}, {"600.0", "690.0"}};
    NT_CHECK(nt_test_windows(&r, minority, 0, 0, 0, NULL, 0, 0) && cm_equal(r.verdict, "inconclusive"),
        "Windows minority is inconclusive and the margin is strict (UTF-8 results)");
    const char *const quiet[3][2] = {{"600.0", "600.0"}, {"600.0", "590.0"}, {"600.0", "605.0"}};
    NT_CHECK(nt_test_windows(&r, quiet, 1, 0, 0, NULL, 0, 0) && cm_equal(r.verdict, "no regression"), "Windows quiet run");
    NT_CHECK(nt_test_windows(&r, quiet, 1, 0, 0, NULL, 0, 1) && cm_equal(r.verdict, "regression") && r.demonstration,
        "Windows demonstration doubles the candidate");
    NT_CHECK(!nt_test_windows(&r, quiet, 1, 0, 0, NULL, 2, 0), "Windows trial without pair validation fails");
    NT_CHECK(!nt_test_windows(&r, quiet, 1, 2, 1, "\"exit_code\":  1", 0, 0), "Windows failed matrix fails");
    NT_CHECK(!nt_test_windows(&r, quiet, 1, 1, 0, "\"exit_code\":  0,\r\n    \"summary_exit_code\":  1", 0, 0),
        "Windows duplicate or failed summary exit fails");
    NT_CHECK(!nt_test_windows(&r, quiet, 1, 3, 1, "\"exit_code\":  0,\r\n    \"extra\":  \"\xc3\xa9\"", 0, 0),
        "Windows non-ASCII UTF-16 result fails");
    const char *const stale_null[3][2] = {{"600.0", "null"}, {"600.0", "600.0"}, {"600.0", "600.0"}};
    NT_CHECK(!nt_test_windows(&r, stale_null, 1, 0, 0, NULL, 0, 0), "Windows missing duration fails");
    const char *const zero[3][2] = {{"0", "600.0"}, {"600.0", "600.0"}, {"600.0", "600.0"}};
    NT_CHECK(!nt_test_windows(&r, zero, 1, 0, 0, NULL, 0, 0), "Windows zero duration fails");
    char huge[512];
    memset(huge, '0', 401);
    huge[0] = '1';
    huge[401] = 0;
    const char *const extreme_integer[3][2] = {{huge, "1.0"}, {"1.0", "1.0"}, {"1.0", "1.0"}};
    NT_CHECK(!nt_test_windows(&r, extreme_integer, 0, 0, 0, NULL, 0, 0), "Windows 401-digit duration is refused, not raised");
    const char *const overflow[3][2] = {{"1e-308", "1e308"}, {"1.0", "1.0"}, {"1.0", "1.0"}};
    NT_CHECK(!nt_test_windows(&r, overflow, 0, 0, 0, NULL, 0, 0), "Windows overflowing ratio is refused");
    const char *const overflow_demo[3][2] = {{"1.0", "1e308"}, {"1.0", "1.0"}, {"1.0", "1.0"}};
    NT_CHECK(!nt_test_windows(&r, overflow_demo, 0, 0, 0, NULL, 0, 1), "Windows overflowing demonstration is refused");
    const char *const large[3][2] = {{"1e300", "1e300"}, {"1e300", "1e300"}, {"1e300", "1e300"}};
    NT_CHECK(nt_test_windows(&r, large, 0, 0, 0, NULL, 0, 0) && cm_equal(r.verdict, "no regression"),
        "Windows large finite durations stay valid");
    *failures_out += failures;
    *checks_out += checks;
}

BUSTER_GLOBAL_LOCAL void nt_test_publish_cases(unsigned *failures_out, unsigned *checks_out)
{
    unsigned failures = 0, checks = 0;
    const NtTestCase regress = {"macros/fast", "1.30", "1.20", "regression", 1, 1};
    char *text = nt_test_summary(&regress, 1, "true", -1);
    NtReport r;
    int analyzed = nt_test_macos(&r, text, 1, 0);
    NT_CHECK(analyzed, "publisher input analyzes");
    char action[64];
    const char *listing = "issues?state=open&creator=github-actions%5Bbot%5D&per_page=100&page=1";
    const CmResponse opened[] = {{listing, "GET", "[]", 1, 0}, {"issues", "POST", "{\"number\":99}", 1, 0}};
    CmTransport t = {0};
    t.deadline = cm_clock() + 60;
    t.fixture = opened;
    t.fixture_count = 2;
    NT_CHECK(nt_publish(&t, &r, NT_RUN_URL, action, sizeof(action)) && cm_equal(action, "opened #99") && t.requests == 2,
        "the first regression opens one platform issue");
    const char *rows = "[{\"number\":5,\"title\":\"Native hosted timing regression: macOS (throughput-harness-macos.yml)\","
        "\"body\":\"<!-- native-hosted-timing platform=macos -->\",\"user\":{\"login\":\"someone\"}},"
        "{\"number\":6,\"title\":\"Native hosted timing regression: macOS (throughput-harness-macos.yml)\",\"body\":\"no marker\","
        "\"user\":{\"login\":\"github-actions[bot]\"}},"
        "{\"number\":7,\"title\":\"Native hosted timing regression: Windows (issue-708-benchmark.yml)\","
        "\"body\":\"<!-- native-hosted-timing platform=macos -->\",\"user\":{\"login\":\"github-actions[bot]\"}},"
        "{\"number\":8,\"title\":\"Native hosted timing regression: macOS (throughput-harness-macos.yml)\","
        "\"body\":\"<!-- native-hosted-timing platform=macos -->\",\"user\":{\"login\":\"github-actions[bot]\"},\"pull_request\":{}},"
        "{\"number\":12,\"title\":\"Native hosted timing regression: macOS (throughput-harness-macos.yml)\","
        "\"body\":\"<!-- native-hosted-timing platform=macos -->\",\"user\":{\"login\":\"github-actions[bot]\"}},"
        "{\"number\":11,\"title\":\"Native hosted timing regression: macOS (throughput-harness-macos.yml)\","
        "\"body\":\"<!-- native-hosted-timing platform=macos -->\",\"user\":{\"login\":\"github-actions[bot]\"}}]";
    const CmResponse updated[] = {{listing, "GET", rows, 1, 0}, {"issues/11/comments", "POST", "{\"id\":1}", 1, 0}};
    t = (CmTransport){0};
    t.deadline = cm_clock() + 60;
    t.fixture = updated;
    t.fixture_count = 2;
    NT_CHECK(nt_publish(&t, &r, NT_RUN_URL, action, sizeof(action)) && cm_equal(action, "updated #11"),
        "a later regression comments on the lowest matching issue only");
    const CmResponse broken[] = {{listing, "GET", "{\"message\":\"x\"}", 1, 0}};
    t = (CmTransport){0};
    t.deadline = cm_clock() + 60;
    t.fixture = broken;
    t.fixture_count = 1;
    NT_CHECK(!nt_publish(&t, &r, NT_RUN_URL, action, sizeof(action)), "a malformed listing fails");
    t = (CmTransport){0};
    t.deadline = cm_clock() + 60;
    t.fixture = opened;
    NtReport refused = r;
    refused.demonstration = 1;
    NT_CHECK(!nt_publish(&t, &refused, NT_RUN_URL, action, sizeof(action)) && t.requests == 0,
        "a demonstration never files an issue");
    refused = r;
    NT_CHECK(!nt_publish(&t, &refused, "https://github.com/" CM_REPO "/actions/runs/7", action, sizeof(action)) && t.requests == 0,
        "a run URL without its attempt is refused");
    NT_CHECK(!nt_publish(&t, &refused, "https://github.com/other/repo/actions/runs/7/attempts/1", action, sizeof(action)) &&
        t.requests == 0, "another repository's run is refused");
    free(text);
    const NtTestCase ok = {"a/fast", "1.0", "1.0", "no substantial regression detected", 0, 0};
    text = nt_test_summary(&ok, 1, "true", -1);
    NT_CHECK(nt_test_macos(&r, text, 0, 0) && nt_publish(&t, &r, NT_RUN_URL, action, sizeof(action)) &&
        cm_equal(action, "none") && t.requests == 0, "a quiet verdict publishes nothing");
    free(text);
    FILE *f = tmpfile();
    text = nt_test_summary(&regress, 1, "true", -1);
    nt_test_macos(&r, text, 1, 0);
    nt_render(f, &r, NT_RUN_URL);
    nt_annotate(f, &r);
    char *rendered = cm_memory(f);
    NT_CHECK(rendered && strstr(rendered, "not Zen 5 evidence") && strstr(rendered, "| macros/fast | wall_seconds | 1.2490 | yes |") &&
        strstr(rendered, "::warning title=Native macOS timing regression (informative)::macros/fast"),
        "the report and warning name the cell, ratio and disposition");
    free(rendered);
    free(text);
    fclose(f);
    *failures_out += failures;
    *checks_out += checks;
}

BUSTER_GLOBAL_LOCAL int nt_self_test(void)
{
    unsigned failures = 0, checks = 0;
    nt_test_macos_cases(&failures, &checks);
    nt_test_windows_cases(&failures, &checks);
    nt_test_publish_cases(&failures, &checks);
    printf("NATIVE_TIMING_SELF_TEST checks=%u failures=%u\n", checks, failures);
    int result = failures ? 1 : 0;
    return result;
}
#endif
