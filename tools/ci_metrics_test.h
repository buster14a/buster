// Synthetic histories only: parser/ingestion identity and detector controls.
// No fixture is ever written to the production history branch.
#ifndef BUSTER_CI_METRICS_TEST_H
#define BUSTER_CI_METRICS_TEST_H
#include "ci_metrics_history.h"
BUSTER_GLOBAL_LOCAL int cm_test_check(int condition, const char *name)
{
    int result = condition ? 0 : 1;
    if (result) fprintf(stderr, "FAIL: %s\n", name);
    return result;
}
BUSTER_GLOBAL_LOCAL CmRow cm_fixture(char text[CM_FIELD_COUNT][CM_FIELD + 1], unsigned serial, int seconds)
{
    CmRow r = {0}; memset(text, 0, CM_FIELD_COUNT * (CM_FIELD + 1));
    for (unsigned i = 0; i < CM_FIELD_COUNT; ++i) r.s[i] = text[i];
    const unsigned identifiers[] = {CM_EVENT_SHA, CM_TESTED_SHA, CM_TESTED_TREE, CM_WORKFLOW_SHA, CM_WORKFLOW_BLOB, CM_COLLECTOR};
    for (unsigned i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); ++i)
        cm_copy(text[identifiers[i]], CM_FIELD + 1, "1111111111111111111111111111111111111111");
    r.run = 1000 + serial; r.job = 10000 + serial; r.origin_job = r.job; r.attempt = 1; r.origin_attempt = 1;
    r.seconds = seconds; r.elapsed_available = 1; r.active = 1;
    cm_copy(text[CM_WORKFLOW], CM_FIELD + 1, ".github/workflows/ci.yml");
    cm_copy(text[CM_JOB_KEY], CM_FIELD + 1, "test"); cm_copy(text[CM_MATRIX], CM_FIELD + 1, "static-cell-1");
    cm_copy(text[CM_INVOCATION], CM_FIELD + 1, "direct"); cm_copy(text[CM_NAME], CM_FIELD + 1, "Linux test");
    cm_copy(text[CM_EVENT], CM_FIELD + 1, "push"); cm_copy(text[CM_HEAD_BRANCH], CM_FIELD + 1, "main");
    cm_copy(text[CM_HARDWARE], CM_FIELD + 1, "verified-startup"); cm_copy(text[CM_HOSTING], CM_FIELD + 1, "github-hosted");
    cm_copy(text[CM_LABELS], CM_FIELD + 1, "ubuntu-26.04"); cm_copy(text[CM_CPU_RAW], CM_FIELD + 1, "CPU A ");
    cm_copy(text[CM_CPU], CM_FIELD + 1, "CPU A"); cm_copy(text[CM_OS], CM_FIELD + 1, "Linux");
    cm_copy(text[CM_OS_VERSION], CM_FIELD + 1, "26.04"); cm_copy(text[CM_KERNEL], CM_FIELD + 1, "7.0");
    cm_copy(text[CM_ARCH], CM_FIELD + 1, "x86_64"); cm_copy(text[CM_PROCESS_ARCH], CM_FIELD + 1, "x86_64");
    cm_copy(text[CM_EFFECTIVE_CPU], CM_FIELD + 1, "4"); cm_copy(text[CM_QUOTA], CM_FIELD + 1, "unlimited");
    cm_copy(text[CM_MEMORY], CM_FIELD + 1, "16GB"); cm_copy(text[CM_IMAGE], CM_FIELD + 1, "ubuntu26");
    cm_copy(text[CM_IMAGE_VERSION], CM_FIELD + 1, "version1"); cm_copy(text[CM_EXECUTION_CONTEXT], CM_FIELD + 1, "native");
    cm_copy(text[CM_TOOLCHAIN], CM_FIELD + 1, "clang20"); cm_copy(text[CM_CACHE], CM_FIELD + 1, "miss/policy1");
    cm_copy(text[CM_WORKLOAD], CM_FIELD + 1, "definition1"); cm_copy(text[CM_WORKERS], CM_FIELD + 1, "4");
    cm_copy(text[CM_KIND], CM_FIELD + 1, "executed"); cm_copy(text[CM_CONCLUSION], CM_FIELD + 1, "success");
    cm_copy(text[CM_STEPS], CM_FIELD + 1, "[]"); cm_copy(text[CM_ALIAS], CM_FIELD + 1, "physical-execution");
    cm_copy(text[CM_OBSERVED], CM_FIELD + 1, "2026-10-06T12:00:00Z"); cm_copy(text[CM_RUN_CONCLUSION], CM_FIELD + 1, "failure");
    cm_copy(text[CM_CREATED], CM_FIELD + 1, "2026-10-01T00:00:00Z"); cm_copy(text[CM_CONTEXT_STATUS], CM_FIELD + 1, "complete");
    unsigned day = 1 + serial % 3;
    snprintf(text[CM_STARTED], CM_FIELD + 1, "2026-10-%02uT00:00:00Z", day);
    snprintf(text[CM_COMPLETED], CM_FIELD + 1, "2026-10-%02uT00:%02d:%02dZ", day, seconds / 60, seconds % 60);
    return r;
}
BUSTER_GLOBAL_LOCAL char *cm_inventory_fixture(unsigned begin, unsigned count, unsigned total, int terminal)
{
    FILE *file = tmpfile();
    char *result = NULL;
    if (file)
    {
        fprintf(file, "{\"total_count\":%u,\"jobs\":[", total);
        for (unsigned i = 0; i < count; ++i)
        {
            if (i) fputc(',', file);
            fprintf(file, "{\"id\":%u,\"run_id\":1,\"run_attempt\":1,"
                "\"head_sha\":\"1111111111111111111111111111111111111111\","
                "\"name\":\"cell-%u\",\"status\":\"%s\",\"conclusion\":\"skipped\","
                "\"started_at\":\"2026-10-01T00:00:00Z\",\"completed_at\":\"2026-10-01T00:00:01Z\","
                "\"runner_id\":0,\"labels\":[],\"steps\":[]}", begin + i, begin + i, terminal ? "completed" : "in_progress");
        }
        fputs("]}", file); result = cm_memory(file); fclose(file);
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_inventory_tests(void)
{
    int failures = 0;
    const char *run = "{\"id\":1,\"run_attempt\":1,\"repository\":{\"full_name\":\"" CM_REPO "\"},"
        "\"status\":\"completed\",\"conclusion\":\"failure\",\"event\":\"push\",\"head_branch\":\"main\","
        "\"head_sha\":\"1111111111111111111111111111111111111111\","
        "\"path\":\".github/workflows/ci.yml\",\"created_at\":\"2026-10-01T00:00:00Z\"}";
    char *page1 = cm_inventory_fixture(10, 100, 101, 1), *page2 = cm_inventory_fixture(110, 1, 101, 1);
    char *duplicate = cm_inventory_fixture(10, 1, 101, 1), *unfinished = cm_inventory_fixture(110, 1, 101, 0);
    CmStore store;
    int initialized = cm_store_init(&store);
    if (initialized && page1 && page2 && duplicate && unfinished)
    {
        CmResponse responses[] =
        {
            {"actions/runs/1", "GET", run, 1, 0},
            {"actions/runs/1/attempts/1/jobs?per_page=100&page=1", "GET", "", 0, 0},
            {"actions/runs/1/attempts/1/jobs?per_page=100&page=1", "GET", page1, 1, 0},
            {"actions/runs/1/attempts/1/jobs?per_page=100&page=2", "GET", duplicate, 1, 0}
        };
        CmTransport transport = {0}; transport.deadline = cm_clock() + 60;
        cm_copy(transport.revision, sizeof(transport.revision), "1111111111111111111111111111111111111111");
        transport.fixture = responses; transport.fixture_count = sizeof(responses) / sizeof(responses[0]);
        CmCollection collection = {0}; collection.transport = &transport; collection.store = &store;
        collection.observed = "2026-10-06T12:00:00Z"; collection.quiet = 1;
        failures += cm_test_check(!cm_collect_run(&collection, 1) && store.count == 100 &&
            store.gaps == 1 && transport.retries == 1, "pagination duplicate rejected after bounded transient recovery");
        responses[1].success = 1; responses[1].content = page1;
        responses[2].path = responses[3].path; responses[2].content = unfinished; responses[3].path = "";
        transport.fixture_cursor = 0; transport.fixture_count = 3;
        failures += cm_test_check(!cm_collect_run(&collection, 1) && store.count == 100,
            "completed workflow with unfinalized job remains pending");
        responses[2].content = page2; transport.fixture_cursor = 0;
        failures += cm_test_check(cm_collect_run(&collection, 1) && store.count == 101,
            "late finalized retry adds only the missing execution");
        transport.fixture_cursor = 0;
        failures += cm_test_check(cm_collect_run(&collection, 1) && store.count == 101,
            "paginated reconciliation does not duplicate durable executions");
        CmResponse absent[] = {{"git/ref/heads/" CM_BRANCH, "GET", "", 0, 1}};
        transport.fixture = absent; transport.fixture_cursor = 0; transport.fixture_count = 1;
        unsigned before = transport.failures;
        failures += cm_test_check(cm_data_head(&transport) && !transport.data_exists &&
            transport.fixture_cursor == 1 && transport.failures == before, "initial absent data branch is not an API outage");
    }
    else ++failures;
    cm_store_free(&store); free(page1); free(page2); free(duplicate); free(unfinished);
    const char *workflow = "name: Fixture\njobs:\n  test:\n    steps:\n"
        "      - name: Machine specifications\n"
        "        uses: buster14a/buster/.github/actions/machine-specifications@" CM_REPORTER "\n"
        "      - name: Record actual checkout identity\n"
        "        uses: buster14a/buster/.github/actions/machine-specifications@" CM_REPORTER "\n"
        "        with:\n          mode: source\n";
    int matrix = 0;
    failures += cm_test_check(cm_workflow_startup(workflow, "test", &matrix) &&
        cm_workflow_source(workflow, "test"), "reviewed startup and source action provenance");
    failures += cm_test_check(!cm_workflow_startup(workflow, "other", &matrix),
        "same display name cannot bind a different logical job");
    char log[] = "2026-10-01T00:00:00Z MACHINE_SPECIFICATIONS_JSON {}\n"
        "2026-10-01T00:00:01Z MACHINE_SPECIFICATIONS_JSON {}\n";
    unsigned duplicates = 0;
    char *record = cm_log_record(log, "MACHINE_SPECIFICATIONS_JSON ",
        "2026-10-01T00:00:00Z", "2026-10-01T00:00:01Z", &duplicates);
    failures += cm_test_check(!record && duplicates == 2, "duplicate startup records never attribute hardware");
    free(record);
    char directory[] = "/tmp/buster-ci-history-test-XXXXXX";
    char *created = mkdtemp(directory);
    if (created)
    {
        char path[4096]; snprintf(path, sizeof(path), "%s/link.txt", directory);
        int linked = symlink("/etc/passwd", path) == 0;
        char *content = NULL;
        failures += cm_test_check(linked && !cm_regular_read(directory, "link.txt", &content),
            "publisher rejects symlink payload without following it");
        free(content); unlink(path); rmdir(directory);
    }
    else ++failures;
    return failures;
}
BUSTER_GLOBAL_LOCAL int cm_self_test(void)
{
    int failures = cm_inventory_tests();
    const char *valid[] = {"{}", "[]", "{\"a\":[1,true,false,null,\"a\\n\\u20ac\\ud83d\\ude00\"]}", "0", "-1.25e+2"};
    const char *invalid[] = {"", "{\"a\":1,\"a\":2}", "[1,]", "{\"a\":}", "{\"a\":1,}", "[01]", "[NaN]",
        "[Infinity]", "[1e]", "\"\\u0000\"", "\"\\ud800\"", "\"\\udc00\"", "{}{}", "\"bad\nstring\""};
    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
    {
        CmJson j = cm_json_parse(valid[i], strlen(valid[i]));
        failures += cm_test_check(j.valid, "valid JSON"); cm_json_free(&j);
    }
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        CmJson j = cm_json_parse(invalid[i], strlen(invalid[i]));
        failures += cm_test_check(!j.valid, "malformed/duplicate/nonfinite JSON"); cm_json_free(&j);
    }
    char deep[256]; unsigned n = 0;
    for (unsigned i = 0; i <= CM_DEPTH; ++i) deep[n++] = '[';
    for (unsigned i = 0; i <= CM_DEPTH; ++i) deep[n++] = ']';
    CmJson excessive = cm_json_parse(deep, n);
    failures += cm_test_check(!excessive.valid, "bounded explicit grammar stack"); cm_json_free(&excessive);
    uint64_t number = 0;
    failures += cm_test_check(!cm_unsigned("18446744073709551616", &number), "integer overflow");
    failures += cm_test_check(!cm_unsigned("-1", &number), "negative identity");
    int64_t elapsed = 0;
    failures += cm_test_check(cm_span("2026-10-01T00:00:00Z", "2026-10-01T00:01:40Z", &elapsed) && elapsed == 100, "API elapsed");
    failures += cm_test_check(!cm_span("2026-10-01T00:01:40Z", "2026-10-01T00:00:00Z", &elapsed), "inverted timestamps");
    failures += cm_test_check(cm_time("2026-02-29T00:00:00Z") < 0 && cm_time("2026-10-01T00:00:00") < 0, "invalid calendar/timezone");
    char normalized[128]; cm_normalize(normalized, sizeof(normalized), "  AMD  CPU \t ");
    failures += cm_test_check(cm_equal(normalized, "AMD CPU"), "harmless whitespace normalization");
    FILE *escaped = tmpfile();
    if (escaped)
    {
        cm_escape(escaped, "=formula,\"<tag>|[x]\n", 1); rewind(escaped);
        char output[256]; size_t length = fread(output, 1, sizeof(output) - 1, escaped); output[length] = 0;
        failures += cm_test_check(strncmp(output, "\"'=formula,\"\"", 12) == 0, "CSV formula neutralization");
        fclose(escaped);
        escaped = tmpfile();
        if (escaped)
        {
            cm_escape(escaped, "<tag>|[x]\n", 0); rewind(escaped);
            length = fread(output, 1, sizeof(output) - 1, escaped); output[length] = 0;
            failures += cm_test_check(!strchr(output, '<') && !strchr(output, '|') && !strchr(output, '\n'), "Markdown injection");
            fclose(escaped);
        }
        else ++failures;
    }
    else ++failures;
    char (*fields)[CM_FIELD_COUNT][CM_FIELD + 1] = calloc(40, sizeof(*fields));
    CmRow rows[40];
    const CmRow *baseline[CM_WINDOW], *candidate[CM_WINDOW];
    if (fields)
    {
        for (unsigned i = 0; i < 40; ++i) rows[i] = cm_fixture(fields[i], i, 100);
        for (unsigned i = 0; i < 20; ++i) { baseline[i] = &rows[i]; candidate[i] = &rows[i + 20]; }
        CmComparison cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "no detectable change") && cmp.baseline.p95_available, "stable history/p95 policy");
        rows[39] = cm_fixture(fields[39], 39, 500);
        cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "no detectable change") && cmp.candidate.p95 == 100, "isolated outlier preserved");
        for (unsigned i = 20; i < 40; ++i) rows[i] = cm_fixture(fields[i], i, 120);
        cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "regression signal") && cmp.seconds == 20 && cmp.percent == 20, "sustained slowdown");
        for (unsigned i = 20; i < 40; ++i) rows[i] = cm_fixture(fields[i], i, 80);
        cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "improvement signal") && cmp.seconds == -20 && cmp.percent == -20, "sustained improvement");
        cmp = cm_compare(baseline, 7, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "insufficient data"), "minimum sample policy");
        const unsigned boundaries[] = {CM_CPU, CM_EFFECTIVE_CPU, CM_IMAGE_VERSION, CM_TOOLCHAIN, CM_CACHE, CM_WORKLOAD, CM_WORKERS, CM_WORKFLOW_BLOB};
        for (unsigned k = 0; k < sizeof(boundaries) / sizeof(boundaries[0]); ++k)
        {
            const char *saved = rows[20].s[boundaries[k]]; rows[20].s[boundaries[k]] = "changed";
            cmp = cm_compare(baseline, 20, candidate, 20);
            failures += cm_test_check(cm_equal(cmp.state, "noncomparable/workload-or-environment changed"), "explicit cohort boundary/equal-count workload change");
            rows[20].s[boundaries[k]] = saved;
        }
        rows[20].s[CM_TESTED_SHA] = "2222222222222222222222222222222222222222";
        failures += cm_test_check(cm_same_series(&rows[0], &rows[20]), "source changes retain semantic series");
        rows[20].s[CM_HARDWARE] = "missing-startup-report";
        cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "noncomparable/workload-or-environment changed"), "missing hardware is not comparable");
        rows[20] = cm_fixture(fields[20], 20, 100);
        rows[20].s[CM_CONCLUSION] = "failure";
        failures += cm_test_check(!cm_sample(&rows[20]), "failed execution cannot be speedup");
        rows[20].s[CM_CONCLUSION] = "cancelled";
        failures += cm_test_check(!cm_sample(&rows[20]), "cancelled execution cannot be speedup");
        rows[20] = cm_fixture(fields[20], 20, 100);
        failures += cm_test_check(cm_sample(&rows[20]), "successful sibling in failed workflow retained");
        rows[20].s[CM_HOSTING] = "self-hosted";
        failures += cm_test_check(!cm_sample(&rows[20]), "self-hosted excluded from default population");
        rows[20] = rows[0]; rows[20].job += 100; rows[20].attempt = 2;
        failures += cm_test_check(cm_alias(&rows[20], &rows[0]), "exact carried-forward equality");
        rows[20].origin_job = rows[0].job; rows[20].origin_attempt = 1; rows[20].s[CM_ALIAS] = "exact-carried-forward-fields";
        failures += cm_test_check(!cm_sample(&rows[20]), "carried-forward alias has no sample/cost");
        rows[20].s[CM_COMPLETED] = "2026-10-01T00:01:41Z";
        failures += cm_test_check(!cm_alias(&rows[20], &rows[0]), "genuine rerun not alias");
        CmStore store;
        if (cm_store_init(&store))
        {
            failures += cm_test_check(cm_add(&store, &rows[0]) == 1 && cm_add(&store, &rows[0]) == 2 && store.count == 1, "duplicate ingestion");
            FILE *exported = tmpfile();
            if (exported)
            {
                cm_emit_row(exported, &rows[0]); rewind(exported);
                char text[32768]; size_t got = fread(text, 1, sizeof(text), exported);
                failures += cm_test_check(cm_import(&store, text, got) && store.count == 1, "deterministic export/import");
                fclose(exported);
            }
            else ++failures;
            CmRow changed = rows[0]; changed.s[CM_COMPLETED] = "2026-10-01T00:01:41Z"; changed.seconds = 101;
            failures += cm_test_check(!cm_add(&store, &changed) && store.conflicts == 1, "conflicting immutable identity rejected");
            CmRow enriched = rows[0]; enriched.s[CM_OBSERVED] = "2026-10-06T13:00:00Z"; enriched.s[CM_TOOLCHAIN] = "verified";
            failures += cm_test_check(cm_add(&store, &enriched) == 1 && !store.rows[0].active && store.rows[1].active, "append-only enrichment");
            failures += cm_test_check(cm_add(&store, &rows[0]) == 2 && store.rows[1].active, "stale observation cannot overwrite");
            cm_store_free(&store);
        }
        else ++failures;
        for (unsigned i = 0; i < 40; ++i) rows[i] = cm_fixture(fields[i], i, i < 20 ? 0 : 1);
        cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(!cmp.percent_available && cm_equal(cmp.state, "inconclusive"), "short/zero durations");
        for (unsigned i = 0; i < 40; ++i) rows[i] = cm_fixture(fields[i], i, i < 20 ? 100 : 101);
        cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "no detectable change"), "small single-window drift");
        for (unsigned i = 20; i < 40; ++i) rows[i] = cm_fixture(fields[i], i, 108);
        cmp = cm_compare(baseline, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "regression signal"), "cumulative drift against fixed anchor");
        cmp = cm_compare(candidate, 20, candidate, 20);
        failures += cm_test_check(cm_equal(cmp.state, "noncomparable/workload-or-environment changed"), "candidate excluded from own baseline");
        free(fields);
    }
    else ++failures;
    printf("Hosted CI metrics synthetic self-test: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
#endif
