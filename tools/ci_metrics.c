// Native hosted-CI history owner (#2823/#2824); no compiler/runtime policy.
// JSON/model headers own validation and physical identities; collect owns REST
// and startup joins; report owns bounded descriptive cohorts; history separates
// read-only staging from the trusted data-branch writer. --self-test is synthetic.
#include "ci_metrics_history.h"
#include "ci_metrics_test.h"
BUSTER_GLOBAL_LOCAL void cm_summary(CmCollection *c, int success, const char *directory)
{
    FILE *summary = stdout;
    fprintf(summary, "CI_HISTORY_COLLECTION status=%s physical_executions=%u verified_machine_joins=%u verified_source_joins=%u "
        "skipped=%u gaps=%u api_requests=%u read_retries=%u api_failures=%u\n",
        success ? "complete" : "partial", c->executions, c->machine_verified, c->source_verified,
        c->skipped, c->store->gaps, c->transport->requests, c->transport->retries, c->transport->failures);
    const char *path = getenv("GITHUB_STEP_SUMMARY");
    FILE *file = path && path[0] ? fopen(path, "ab") : NULL;
    if (file)
    {
        fprintf(file, "## Hosted CI timing history (advisory)\n\nCollection: **%s**. "
            "%u physical executions; %u verified hardware joins; %u verified checkout joins; "
            "%u visible gaps; %u API reads/writes; %u retries.\n\n"
            "[Durable history and exports](https://github.com/" CM_REPO "/blob/" CM_BRANCH "/reports/index.md). "
            "Missing hardware/context remains unavailable; collection does not gate merges or qualify compiler performance.\n\n",
            success ? "complete" : "partial", c->executions, c->machine_verified, c->source_verified,
            c->store->gaps, c->transport->requests, c->transport->retries);
        fclose(file);
    }
    (void)directory;
}
BUSTER_GLOBAL_LOCAL int cm_main(int argc, char **argv)
{
    int result = 2, valid = argc >= 2;
    const char *mode = valid ? argv[1] : "", *directory = "", *input = "", *event = "", *revision = "";
    const char *job_filter = "", *os_filter = "", *cpu_filter = "", *branch_filter = "";
    uint64_t selected_run = 0, selected_job = 0, days = 2, max_runs = 100, expected_machine = 0;
    int64_t since = -1, until = -1;
    for (int i = 2; valid && i < argc; i += 2)
    {
        valid = i + 1 < argc;
        if (valid)
        {
            const char *key = argv[i], *value = argv[i + 1];
            if (cm_equal(key, "--out")) directory = value;
            else if (cm_equal(key, "--input")) input = value;
            else if (cm_equal(key, "--event")) event = value;
            else if (cm_equal(key, "--job-key")) job_filter = value;
            else if (cm_equal(key, "--os")) os_filter = value;
            else if (cm_equal(key, "--cpu")) cpu_filter = value;
            else if (cm_equal(key, "--branch")) branch_filter = value;
            else if (cm_equal(key, "--revision")) { revision = value; valid = cm_sha(value); }
            else if (cm_equal(key, "--since")) { since = cm_time(value); valid = since >= 0; }
            else if (cm_equal(key, "--until")) { until = cm_time(value); valid = until >= 0; }
            else if (cm_equal(key, "--run")) valid = cm_unsigned(value, &selected_run) && selected_run;
            else if (cm_equal(key, "--expect-machine")) valid = cm_unsigned(value, &expected_machine) && expected_machine <= 1000;
            else if (cm_equal(key, "--job")) valid = cm_unsigned(value, &selected_job) && selected_job;
            else if (cm_equal(key, "--days")) valid = cm_unsigned(value, &days) && days > 0 && days <= 7;
            else if (cm_equal(key, "--max-runs")) valid = cm_unsigned(value, &max_runs) && max_runs > 0 && max_runs <= 1000;
            else valid = 0;
        }
    }
    if (valid && cm_equal(mode, "--self-test") && argc == 2) result = cm_self_test();
    else if (valid && directory[0])
    {
        CmTransport transport = {0}; transport.collection_started = cm_clock(); transport.deadline = cm_clock() + 300;
        cm_copy(transport.revision, sizeof(transport.revision), getenv("GITHUB_SHA"));
        valid = cm_sha(transport.revision);
        time_t now = time(NULL); struct tm utc; gmtime_r(&now, &utc);
        char observed[32]; strftime(observed, sizeof(observed), "%Y-%m-%dT%H:%M:%SZ", &utc);
        if (valid && cm_equal(mode, "publish"))
        {
            valid = cm_publish_stage(&transport, directory);
            result = valid ? 0 : 2;
            printf("CI_HISTORY_PUBLICATION status=%s data_head=%s wall_seconds=%.3f api_requests=%u\n",
                valid ? "published-or-idempotent" : "failed", transport.head, cm_clock() - transport.collection_started, transport.requests);
        }
        else if (valid && (cm_equal(mode, "collect") || cm_equal(mode, "report")))
        {
            CmStore store; valid = cm_store_init(&store);
            CmCollection collection = {0};
            collection.transport = &transport; collection.store = &store; collection.observed = observed; collection.selected_job = selected_job;
            int complete = 1;
            if (valid && cm_equal(mode, "collect"))
            {
                valid = cm_history_load(&transport, &store, 30);
                unsigned checkpoint = store.count;
                transport.request_limit = CM_REQUESTS - 200;
                if (valid) complete = selected_run ? cm_collect_run(&collection, selected_run) :
                    cm_collect_recent(&collection, (unsigned)days, (unsigned)max_runs);
                if (selected_job && collection.executions != 1) { complete = 0; ++store.gaps; }
                if (expected_machine && collection.machine_verified < expected_machine) { complete = 0; ++store.gaps; }
                transport.request_limit = CM_REQUESTS; transport.deadline = cm_clock() + 300;
                CmOutputs outputs = {0};
                if (valid) valid = cm_history_append(&transport, &store, &outputs, checkpoint, observed);
                if (valid) valid = cm_progress_output(&transport, &outputs, observed);
                if (valid) valid = cm_reports(&transport, &store, &outputs, observed, event, revision, since, until, job_filter, os_filter, cpu_filter, branch_filter);
                if (valid) valid = cm_make_manifest(&transport, &store, &outputs, observed);
                if (valid)
                {
                    FILE *readme = tmpfile();
                    if (readme)
                    {
                        fputs("# Hosted CI operational history\n\n[Browse job/OS/CPU histories](reports/index.md).\n\n"
                            "This branch contains allowlisted advisory Actions observations. Raw records are append-only in bounded daily shards. "
                            "Missing hardware, context and API gaps are explicit. It contains no compiler code, required merge gate or qualified performance evidence.\n\n"
                            "Schema/policy and recovery: https://github.com/" CM_REPO "/blob/main/docs/ci-timing-history.md\n", readme);
                        valid = cm_output(&outputs, "README.md", readme);
                    }
                    else valid = 0;
                }
                if (valid) valid = cm_stage(&transport, &outputs, directory, observed);
                cm_outputs_free(&outputs);
                const char *output_path = getenv("GITHUB_OUTPUT");
                FILE *output = valid && output_path && output_path[0] ? fopen(output_path, "ab") : NULL;
                if (output) { fputs("bundle_ready=true\n", output); fclose(output); }
            }
            else if (valid)
            {
                size_t length = 0; char *records = cm_read(input, CM_BYTES, &length);
                valid = records && cm_import(&store, records, length); free(records);
                CmOutputs outputs = {0};
                if (valid) valid = cm_reports(&transport, &store, &outputs, observed, event, revision, since, until, job_filter, os_filter, cpu_filter, branch_filter);
                if (valid) valid = cm_stage(&transport, &outputs, directory, observed);
                cm_outputs_free(&outputs);
            }
            cm_summary(&collection, valid && complete && !store.gaps, directory);
            result = valid && complete && !store.gaps ? 0 : 2;
            cm_store_free(&store);
        }
        else fputs("CI history: invalid command or unavailable collector revision\n", stderr);
    }
    else fputs("usage: ci-metrics --self-test | collect --out NEW_DIRECTORY [--run ID --job ID | --days 1..7 --max-runs N] | "
        "report --input JSONL --out NEW_DIRECTORY [--event EVENT --revision SHA --since UTC --until UTC] | publish --out BUNDLE\n", stderr);
    return result;
}
int main(int argc, char **argv)
{
    int result = cm_main(argc, argv);
    return result;
}
