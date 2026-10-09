// Trusted hosted 9700X terminal lifecycle recovery (#3209).
// Entry: --self-test (synthetic, no network) or recover RUN ATTEMPT.
// Existing ci_metrics transport/JSON own bounded gh calls, retries and parsing.
// lc_bind joins trusted executor title -> exact request attempt -> owned check.
// lc_research_identity joins an admitted research marker without starting work.
// lc_url_bound validates the persisted URL and exact native summary binding.
// lc_finish only closes missing terminal results; it never validates or reports
// measurement success, downloads artifacts, starts work or changes a runner.
// Native Actions job metadata, printed by lc_observe, owns live state and costs.
#include "../ci_metrics_io.h"

#define LC_BENCH ".github/workflows/9700x-direct-bench.yml"
#define LC_MAIN ".github/workflows/9700x-compiler-request.yml"
#define LC_PULL ".github/workflows/9700x-direct-request.yml"
#define LC_APP UINT64_C(15368)
#define LC_REPOSITORY UINT64_C(1071732997)
#define LC_PAGES 10u
#define LC_NATIVE "Lifecycle protocol: terminal-native-v1."
#define LC_EXECUTOR "Workflow run "
#define LC_REQUEST "Request run "
#define LC_SAMPLING_NAME "9700X compiler sampling research"
#define LC_SAMPLING_MARKER "buster-main-sampling-v1:"
#define LC_SAMPLING_NATIVE "Lifecycle protocol: sampling-terminal-native-v1."
#define LC_PREPARATION_NAME "9700X compiler preparation research"
#define LC_PREPARATION_MARKER "buster-compiler-preparation-v1:"
#define LC_PREPARATION_NATIVE "Lifecycle protocol: preparation-terminal-native-v1."
#define LC_RESEARCH_SAMPLING 1
#define LC_RESEARCH_PREPARATION 2

typedef struct LcIdentity LcIdentity;
struct LcIdentity
{
    uint64_t executor, attempt, request, request_attempt;
    int pull, request_only, research;
    char head[41], trusted[41], outcome[32], marker[192], details[256];
};
typedef struct LcResult LcResult;
struct LcResult { unsigned closed, terminal, foreign, unavailable; };

BUSTER_GLOBAL_LOCAL const char *lc_name(int pull)
{
    const char *result = pull ? "9700X compiler benchmark (pull request)" : "9700X compiler benchmark";
    return result;
}
BUSTER_GLOBAL_LOCAL const char *lc_check_name(const LcIdentity *id)
{
    const char *result = id->research == LC_RESEARCH_PREPARATION ? LC_PREPARATION_NAME :
        id->research == LC_RESEARCH_SAMPLING ? LC_SAMPLING_NAME : lc_name(id->pull);
    return result;
}
BUSTER_GLOBAL_LOCAL const char *lc_research_protocol(const LcIdentity *id)
{
    const char *result = id->research == LC_RESEARCH_PREPARATION ? LC_PREPARATION_NATIVE : LC_SAMPLING_NATIVE;
    return result;
}
BUSTER_GLOBAL_LOCAL const char *lc_research_title(const LcIdentity *id)
{
    const char *result = id->research == LC_RESEARCH_PREPARATION ? "Incomplete unqualified preparation research" : "Incomplete unqualified sampling packet";
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_repository(const CmJson *j, unsigned node)
{
    int result = cm_equal(cm_get(j, node, "full_name"), CM_REPO) && cm_number(j, node, "id") == LC_REPOSITORY;
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_run(const CmJson *j, uint64_t run, uint64_t attempt)
{
    int result = j->valid && cm_number(j, 1, "id") == run && cm_number(j, 1, "run_attempt") == attempt &&
        lc_repository(j, cm_member(j, 1, "repository")) && lc_repository(j, cm_member(j, 1, "head_repository")) &&
        cm_sha(cm_get(j, 1, "head_sha")) && cm_equal(cm_get(j, 1, "status"), "completed");
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_title(const char *title, LcIdentity *id)
{
    char request[32], attempt[32], head[64], canonical[192];
    int used = 0;
    int valid = title && sscanf(title, "9700X request %31[0-9].%31[0-9] head %63[0-9a-f]%n",
        request, attempt, head, &used) == 3 && title[used] == 0;
    valid = valid && cm_unsigned(request, &id->request) && id->request && cm_unsigned(attempt, &id->request_attempt) &&
        id->request_attempt && cm_sha(head);
    if (valid)
    {
        snprintf(canonical, sizeof(canonical), "9700X request %" PRIu64 ".%" PRIu64 " head %s",
            id->request, id->request_attempt, head);
        valid = cm_equal(title, canonical);
        if (valid) cm_copy(id->head, sizeof(id->head), head);
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL void lc_marker(LcIdentity *id)
{
    snprintf(id->marker, sizeof(id->marker), "%s:%s:%" PRIu64 ".%" PRIu64 ":%" PRIu64,
        id->pull ? "buster-9700x-compiler-pr-v1" : "buster-9700x-compiler-main-v1",
        id->head, id->request, id->request_attempt, id->attempt);
    snprintf(id->details, sizeof(id->details), "https://github.com/" CM_REPO "/actions/runs/%" PRIu64 "/attempts/%" PRIu64,
        id->request_only ? id->request : id->executor, id->request_only ? id->request_attempt : id->attempt);
}
BUSTER_GLOBAL_LOCAL int lc_bind(CmTransport *t, uint64_t run, uint64_t attempt, LcIdentity *id)
{
    char path[256]; snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/attempts/%" PRIu64, run, attempt);
    CmJson executor = cm_api_json(t, path, "GET", NULL, NULL);
    int valid = lc_run(&executor, run, attempt);
    const char *workflow = cm_get(&executor, 1, "path");
    if (valid && cm_equal(workflow, LC_BENCH))
    {
        id->executor = run; id->attempt = attempt;
        valid = cm_equal(cm_get(&executor, 1, "event"), "workflow_run") &&
            cm_equal(cm_get(&executor, 1, "head_branch"), "main") &&
            lc_title(cm_get(&executor, 1, "display_title"), id);
        cm_copy(id->trusted, sizeof(id->trusted), cm_get(&executor, 1, "head_sha"));
        cm_copy(id->outcome, sizeof(id->outcome), cm_get(&executor, 1, "conclusion"));
        if (valid)
        {
            snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/attempts/%" PRIu64, id->request, id->request_attempt);
            CmJson request = cm_api_json(t, path, "GET", NULL, NULL);
            valid = lc_run(&request, id->request, id->request_attempt) &&
                cm_equal(cm_get(&request, 1, "head_sha"), id->head);
            const char *requested = cm_get(&request, 1, "path");
            id->pull = cm_equal(requested, LC_PULL);
            valid = valid && (id->pull ? cm_equal(cm_get(&request, 1, "event"), "pull_request") :
                cm_equal(requested, LC_MAIN) && cm_equal(cm_get(&request, 1, "event"), "push") &&
                cm_equal(cm_get(&request, 1, "head_branch"), "main"));
            if (valid && id->pull)
            {
                unsigned actor = cm_member(&request, 1, "actor"), triggering = cm_member(&request, 1, "triggering_actor");
                valid = cm_number(&request, actor, "id") == UINT64_C(39247043) &&
                    cm_equal(cm_get(&request, actor, "login"), "davidgmbb") &&
                    cm_number(&request, triggering, "id") == UINT64_C(39247043) &&
                    cm_equal(cm_get(&request, triggering, "login"), "davidgmbb");
            }
            if (valid && !cm_equal(cm_get(&request, 1, "conclusion"), "success"))
                cm_copy(id->outcome, sizeof(id->outcome), cm_get(&request, 1, "conclusion"));
            cm_json_free(&request);
        }
    }
    else if (valid && cm_equal(workflow, LC_MAIN))
    {
        valid = cm_equal(cm_get(&executor, 1, "event"), "push") && cm_equal(cm_get(&executor, 1, "head_branch"), "main");
        id->request_only = 1; id->request = run; id->request_attempt = attempt; id->attempt = 1;
        // No benchmark executor exists for a failed request. Its future
        // attempt-1 marker is reserved by announce, not observed execution.
        cm_copy(id->head, sizeof(id->head), cm_get(&executor, 1, "head_sha"));
        cm_copy(id->outcome, sizeof(id->outcome), cm_get(&executor, 1, "conclusion"));
    }
    else valid = 0;
    if (valid) lc_marker(id);
    cm_json_free(&executor);
    return valid;
}
BUSTER_GLOBAL_LOCAL int lc_owned(const CmJson *j, unsigned row, const LcIdentity *id)
{
    const char *state = cm_get(j, row, "status");
    int result = j->valid && cm_number(j, row, "id") && cm_equal(cm_get(j, row, "head_sha"), id->head) &&
        cm_equal(cm_get(j, row, "name"), lc_check_name(id)) && cm_equal(cm_get(j, row, "external_id"), id->marker) &&
        cm_number(j, cm_member(j, row, "app"), "id") == LC_APP &&
        (cm_equal(state, "queued") || cm_equal(state, "in_progress") || cm_equal(state, "completed"));
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_research_identity(const CmJson *j, unsigned row, const LcIdentity *id, LcIdentity *research)
{
    // Two exact publisher contracts grant recovery authority for an existing
    // admitted row. No selector admission or missing-row creation occurs here.
    const char *name = cm_get(j, row, "name"), *marker = cm_get(j, row, "external_id");
    int kind = cm_equal(name, LC_SAMPLING_NAME) ? LC_RESEARCH_SAMPLING :
        cm_equal(name, LC_PREPARATION_NAME) ? LC_RESEARCH_PREPARATION : 0;
    const char *prefix = kind == LC_RESEARCH_PREPARATION ? LC_PREPARATION_MARKER : LC_SAMPLING_MARKER;
    size_t width = strlen(prefix);
    const char *tail = kind && strncmp(marker, prefix, width) == 0 ? marker + width : NULL;
    char campaign[65], phase[8], packet[32], request[32], executor[32], attempt[32], canonical[192];
    uint64_t number = 0, requested = 0, executed = 0, tried = 0;
    int used = 0;
    int valid = !id->request_only && id->pull && id->attempt == 1 && id->request_attempt == 1 && tail &&
        sscanf(tail, "%64[0-9a-f]:%7[a-z]:%31[0-9]:%31[0-9]:%31[0-9]:%31[0-9]%n",
            campaign, phase, packet, request, executor, attempt, &used) == 6 && tail[used] == 0;
    valid = valid && strlen(campaign) == 64 && cm_unsigned(packet, &number) &&
        cm_unsigned(request, &requested) && requested == id->request &&
        cm_unsigned(executor, &executed) && executed == id->executor && cm_unsigned(attempt, &tried) && tried == 1 &&
        (kind == LC_RESEARCH_PREPARATION ? cm_equal(phase, "qualify") && number == 0 :
         (cm_equal(phase, "acquire") && number == 0) || (cm_equal(phase, "pilot") && number <= 2) ||
         (cm_equal(phase, "confirm") && number <= 39));
    if (valid)
    {
        snprintf(canonical, sizeof(canonical), "%s%s:%s:%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":1",
            prefix, campaign, phase, number, requested, executed);
        valid = cm_equal(marker, canonical);
        if (valid)
        {
            *research = *id; research->research = kind;
            cm_copy(research->marker, sizeof(research->marker), canonical);
            valid = lc_owned(j, row, research);
        }
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int lc_line(const char *summary, const char *line)
{
    size_t length = strlen(line);
    int result = 0;
    while (summary && summary[0] && !result)
    {
        const char *next = strchr(summary, '\n');
        size_t width = next ? (size_t)(next - summary) : strlen(summary);
        result = width == length && memcmp(summary, line, length) == 0;
        summary = next ? next + 1 : NULL;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_binding(const char *summary, const char *prefix_text, const char *line)
{
    size_t length = strlen(line), prefix = strlen(prefix_text);
    int result = 0;
    while (summary && summary[0] && result >= 0)
    {
        const char *next = strchr(summary, '\n');
        size_t width = next ? (size_t)(next - summary) : strlen(summary);
        if (width >= prefix && memcmp(summary, prefix_text, prefix) == 0)
            result = !result && width == length && memcmp(summary, line, length) == 0 ? 1 : -1;
        summary = next ? next + 1 : NULL;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_executor_binding(const char *summary, const char *line)
{
    int result = lc_binding(summary, LC_EXECUTOR, line);
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_url_bound(const CmJson *j, const LcIdentity *id)
{
    const char *details = cm_get(j, 1, "details_url");
    int result = !details[0] || cm_equal(details, id->details) ||
        cm_equal(details, "https://github.com/" CM_REPO "/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run");
    if (id->research || !result)
    {
        // GitHub Actions canonicalizes a custom check's details_url to its
        // own /runs/CHECK_ID link. Retain the exact executor/request binding
        // from our trusted protocol summary rather than accepting any run URL.
        char canonical[256], executor[384], request[384];
        snprintf(canonical, sizeof(canonical), "https://github.com/" CM_REPO "/runs/%" PRIu64, cm_number(j, 1, "id"));
        snprintf(executor, sizeof(executor), LC_EXECUTOR "%" PRIu64 " attempt %" PRIu64 ": %s",
            id->executor, id->attempt, id->details);
        snprintf(request, sizeof(request), LC_REQUEST "%" PRIu64 " attempt %" PRIu64
            ": https://github.com/" CM_REPO "/actions/runs/%" PRIu64 "/attempts/%" PRIu64,
            id->request, id->request_attempt, id->request, id->request_attempt);
        const char *summary = cm_get(j, cm_member(j, 1, "output"), "summary");
        int binding = lc_executor_binding(summary, executor);
        if (id->research)
            result = (cm_equal(details, canonical) || cm_equal(details, id->details)) &&
                lc_binding(summary, "Lifecycle protocol: ", lc_research_protocol(id)) == 1 &&
                lc_binding(summary, LC_REQUEST, request) == 1 && binding == 1;
        else
            result = cm_equal(details, canonical) && lc_line(summary, LC_NATIVE) &&
                ((!id->request_only && binding == 1) || (binding == 0 && lc_line(summary, request)));
    }
    return result;
}
BUSTER_GLOBAL_LOCAL const char *lc_conclusion(const LcIdentity *id)
{
    const char *result = cm_equal(id->outcome, "cancelled") ? "cancelled" :
        (!id->research && cm_equal(id->outcome, "skipped")) ? "skipped" : "failure";
    return result;
}
BUSTER_GLOBAL_LOCAL char *lc_body(const LcIdentity *id, const char *prior, int create)
{
    FILE *file = tmpfile();
    char *result = NULL;
    if (file && (!id->research || !create))
    {
        fputs("{", file);
        if (create)
        {
            fputs("\"name\":", file); cm_quote(file, lc_check_name(id));
            fputs(",\"head_sha\":", file); cm_quote(file, id->head);
            fputs(",\"external_id\":", file); cm_quote(file, id->marker); fputc(',', file);
        }
        fputs("\"status\":\"completed\",\"conclusion\":", file); cm_quote(file, lc_conclusion(id));
        fputs(",\"details_url\":", file); cm_quote(file, id->details);
        fputs(",\"output\":{\"title\":", file);
        cm_quote(file, id->research ? lc_research_title(id) : "Not measured: terminal lifecycle reconciliation");
        fputs(",\"summary\":", file);
        FILE *text = tmpfile();
        if (text)
        {
            if (prior && prior[0]) fprintf(text, "%.*s\n\n", 40000, prior);
            char executor_identity[128];
            if (id->request_only) cm_copy(executor_identity, sizeof(executor_identity), "unavailable (request ended before benchmark assignment)");
            else snprintf(executor_identity, sizeof(executor_identity), "run %" PRIu64 " attempt %" PRIu64, id->executor, id->attempt);
            if (id->research)
                fprintf(text, "**%s.** Exact benchmark run %" PRIu64 " attempt %" PRIu64
                    " finished with conclusion `%s` without a validated terminal research packet. "
                    "qualification=unqualified; routine_profile_enabled=false. "
                    "This closes research bookkeeping only; no measurement evidence is validated or promoted to success. "
                    "Repository `" CM_REPO "`, source `%s`, request %" PRIu64 " attempt %" PRIu64
                    ", trusted harness `%s`, marker `%s`. Native Actions owns execution state: %s",
                    lc_research_title(id), id->executor, id->attempt, id->outcome, id->head, id->request, id->request_attempt,
                    id->trusted, id->marker, id->details);
            else fprintf(text, "**%s: not measured.** The exact %s run finished with conclusion `%s` without a validated terminal measurement check. "
                "This recovery closes bookkeeping only; it does not validate evidence or claim a successful measurement. "
                "Repository `" CM_REPO "`, candidate `%s`, request %" PRIu64 " attempt %" PRIu64
                ". Executor identity %s. Trusted harness `%s`. "
                "Baseline remains as recorded above; if setup never ran it is unavailable. Native Actions owns execution state: %s",
                lc_check_name(id), id->request_only ? "request" : "benchmark", id->outcome, id->head,
                id->request, id->request_attempt, executor_identity, id->trusted[0] ? id->trusted : "unavailable", id->details);
            char *summary = cm_memory(text); fclose(text);
            if (summary) { cm_quote(file, summary); fputs("}}", file); result = cm_memory(file); free(summary); }
        }
    }
    if (file) fclose(file);
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_finish(CmTransport *t, const LcIdentity *id, LcResult *result)
{
    int valid = 1, more = 1;
    unsigned matched = 0, listed = 0;
    // A successful request has only announced work; only executor completion
    // can reconcile it. No callback claims measurement from request success.
    if (id->request_only && cm_equal(id->outcome, "success")) more = 0;
    for (unsigned page = 1; valid && more && page <= LC_PAGES; ++page)
    {
        char path[512]; snprintf(path, sizeof(path), "commits/%s/check-runs?filter=all&app_id=15368&per_page=100&page=%u", id->head, page);
        CmJson rows = cm_api_json(t, path, "GET", NULL, NULL);
        unsigned array = cm_member(&rows, 1, "check_runs"), count = 0;
        valid = rows.valid && array && rows.tokens[array].kind == 'a';
        for (unsigned row = valid ? rows.tokens[array].child : 0; valid && row; row = rows.tokens[row].next)
        {
            ++count;
            LcIdentity owned = *id;
            int is_owned = lc_owned(&rows, row, &owned);
            if (!is_owned) is_owned = lc_research_identity(&rows, row, id, &owned);
            if (is_owned)
            {
                ++matched;
                uint64_t check = cm_number(&rows, row, "id");
                if (cm_equal(cm_get(&rows, row, "status"), "completed")) ++result->terminal;
                else
                {
                    // Fresh read prevents stale listing snapshots from reopening
                    // a terminal check. Completion callbacks run after all the
                    // executor's setup/publisher jobs have finished. Every hosted
                    // check writer (including cross-attempt orphan reconciliation)
                    // holds the same job-level queue:max lock during this read/write.
                    snprintf(path, sizeof(path), "check-runs/%" PRIu64, check);
                    CmJson fresh = cm_api_json(t, path, "GET", NULL, NULL);
                    valid = lc_owned(&fresh, 1, &owned) && cm_number(&fresh, 1, "id") == check;
                    int bound = lc_url_bound(&fresh, &owned);
                    if (valid && cm_equal(cm_get(&fresh, 1, "status"), "completed")) ++result->terminal;
                    else if (valid && !bound) ++result->foreign;
                    else if (valid)
                    {
                        char *body = lc_body(&owned, cm_get(&fresh, cm_member(&fresh, 1, "output"), "summary"), 0);
                        CmJson written = cm_api_json(t, path, "PATCH", body, NULL);
                        valid = body && lc_owned(&written, 1, &owned) && cm_number(&written, 1, "id") == check &&
                            cm_equal(cm_get(&written, 1, "status"), "completed");
                        if (!valid)
                        {
                            // A lost write response is observed once, never
                            // blindly retried; completed state remains final.
                            CmJson observed = cm_api_json(t, path, "GET", NULL, NULL);
                            valid = lc_owned(&observed, 1, &owned) && cm_number(&observed, 1, "id") == check &&
                                cm_equal(cm_get(&observed, 1, "status"), "completed");
                            cm_json_free(&observed);
                        }
                        if (valid) ++result->closed;
                        cm_json_free(&written); free(body);
                    }
                    cm_json_free(&fresh);
                }
            }
        }
        listed += count;
        more = count == 100;
        valid = valid && count <= 100 && listed <= cm_number(&rows, 1, "total_count") &&
            (more || listed == cm_number(&rows, 1, "total_count")) && !(page == LC_PAGES && more);
        cm_json_free(&rows);
    }
    // Pending cancellation before setup may have only the main request's
    // bridge check. If no check ever existed, retain the gap without creating
    // a check for an ordinary unrequested/skipped PR comparison.
    if (!matched && !id->request_only) ++result->unavailable;
    return valid;
}
BUSTER_GLOBAL_LOCAL void lc_seconds(FILE *file, const char *start, const char *finish)
{
    int64_t seconds = 0;
    if (cm_span(start, finish, &seconds)) fprintf(file, "%" PRId64, seconds);
    else fputs("null", file);
}
BUSTER_GLOBAL_LOCAL int lc_physical_job(const char *name)
{
    int result = cm_equal(name, "Compare the main commit compiler") ||
        cm_equal(name, "Compare the pull request compiler") || cm_equal(name, "bench") ||
        cm_equal(name, "Sampling qualification packet") || cm_equal(name, "Compiler preparation qualification");
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_observe(CmTransport *t, const LcIdentity *id)
{
    uint64_t observed_run = id->request_only ? id->request : id->executor;
    char path[256]; snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/attempts/%" PRIu64 "/jobs?per_page=100",
        observed_run, id->request_only ? id->request_attempt : id->attempt);
    CmJson jobs = cm_api_json(t, path, "GET", NULL, NULL);
    unsigned array = cm_member(&jobs, 1, "jobs"), count = 0;
    int valid = jobs.valid && array && jobs.tokens[array].kind == 'a' && cm_number(&jobs, 1, "total_count") <= 100;
    for (unsigned row = valid ? jobs.tokens[array].child : 0; row; row = jobs.tokens[row].next)
    {
        const char *name = cm_get(&jobs, row, "name");
        int physical = lc_physical_job(name);
        printf("{\"schema\":\"buster-9700x-lifecycle-cost-v1\",\"run_id\":%" PRIu64 ",\"attempt\":%" PRIu64 ",\"job_id\":%" PRIu64 ",\"job\":",
            observed_run, id->request_only ? id->request_attempt : id->attempt, cm_number(&jobs, row, "id"));
        cm_quote(stdout, name);
        printf(",\"request_run_id\":%" PRIu64 ",\"request_attempt\":%" PRIu64, id->request, id->request_attempt);
        fputs(",\"source_head\":", stdout); cm_quote(stdout, id->head);
        fputs(",\"trusted_revision\":", stdout); cm_quote(stdout, id->trusted[0] ? id->trusted : "unavailable");
        fputs(",\"role\":", stdout); cm_quote(stdout, physical ? "physical" : "hosted-control");
        fputs(",\"queue_delay_seconds\":", stdout); lc_seconds(stdout, cm_get(&jobs, row, "created_at"), cm_get(&jobs, row, "started_at"));
        fputs(",\"execution_seconds\":", stdout); lc_seconds(stdout, cm_get(&jobs, row, "started_at"), cm_get(&jobs, row, "completed_at"));
        fputs(",\"status\":", stdout); cm_quote(stdout, cm_get(&jobs, row, "status"));
        fputs(",\"conclusion\":", stdout); cm_quote(stdout, cm_get(&jobs, row, "conclusion")); fputs("}\n", stdout);
        ++count;
    }
    valid = valid && count == cm_number(&jobs, 1, "total_count");
    cm_json_free(&jobs);
    return valid;
}
BUSTER_GLOBAL_LOCAL int lc_recover(CmTransport *t, uint64_t run, uint64_t attempt, LcResult *result)
{
    LcIdentity id = {0};
    int valid = lc_bind(t, run, attempt, &id);
    if (valid) valid = lc_finish(t, &id, result);
    if (valid) valid = lc_observe(t, &id);
    return valid;
}

#include "lifecycle_test.h"
int main(int argc, char **argv)
{
    int result = 2;
    if (argc == 2 && cm_equal(argv[1], "--self-test")) result = lc_self_test();
    else if (argc == 4 && cm_equal(argv[1], "recover"))
    {
        uint64_t run = 0, attempt = 0;
        int valid = cm_unsigned(argv[2], &run) && run && cm_unsigned(argv[3], &attempt) && attempt;
        const char *repository = getenv("GITHUB_REPOSITORY"), *token = getenv("GH_TOKEN");
        valid = valid && cm_equal(repository, CM_REPO) && token && token[0];
        CmTransport transport = {0}; transport.collection_started = cm_clock(); transport.deadline = cm_clock() + 180; transport.request_limit = 60;
        LcResult recovery = {0};
        if (valid) valid = lc_recover(&transport, run, attempt, &recovery);
        printf("{\"schema\":\"buster-9700x-lifecycle-pass-v1\",\"status\":\"%s\",\"run_id\":%" PRIu64 ",\"attempt\":%" PRIu64
            ",\"closed\":%u,\"already_terminal\":%u,\"other_executor\":%u,\"unavailable\":%u,\"api_requests\":%u,\"api_retries\":%u,\"api_failures\":%u,\"control_execution_seconds\":%.6f}\n",
            valid ? "complete" : "incomplete", run, attempt, recovery.closed, recovery.terminal, recovery.foreign, recovery.unavailable,
            transport.requests, transport.retries, transport.failures, cm_clock() - transport.collection_started);
        result = valid ? 0 : 2;
    }
    else fputs("usage: lifecycle --self-test | recover RUN ATTEMPT\n", stderr);
    return result;
}
