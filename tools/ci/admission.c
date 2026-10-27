// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Trusted default-branch control plane for #546-#548. Only this adapter writes GitHub metadata or dispatches work.
// The read-only executor validates a durable lease before checkout and again at completion. The required PR check
// is published on the PR head, never inferred from a dispatch's default-branch check. No PR code/cache/artifact is
// read as controller code or authoritative state. Layout: HTTP; ledger; live metadata; commands; reconciliation;
// read-only worker guard; deterministic tests (admission_test.c); main.

#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "admission_json.h"
#include "admission_policy.h"

#define BUSTER_CI_REPO "buster14a/buster"
#define BUSTER_CI_WORKFLOW ".github/workflows/9700x.yml"
#define BUSTER_CI_CONTROL "buster14a/buster/.github/workflows/9700x-control.yml@refs/heads/main"
#define BUSTER_CI_CANDIDATES (BUSTER_CI_ITEMS * 2)
#define BUSTER_CI_PR_FIELDS "number state headRefOid baseRefOid headRef { target { oid } } baseRef { target { oid } } mergeable potentialMergeCommit { oid } headRepository { databaseId }"

typedef struct Http Http;
struct Http
{
    int status;
    Json json;
};

static bool ci_read_only;
static bool ci_fixture_http;
static uint64_t ci_fixture_clock;
static uint64_t ci_workflow_id;
static Http ci_fixture_request(const char* method, const char* path, const char* body);
static uint64_t ci_workflow(void);

static const char* ci_env(const char* name)
{
    const char* value = getenv(name);
    return value != NULL ? value : "";
}

static uint64_t ci_now(void)
{
    time_t value = ci_fixture_http ? (time_t)ci_fixture_clock : time(NULL);
    if (value < 0)
    {
        ci_die("clock unavailable");
    }
    return (uint64_t)value;
}

static uint64_t ci_time(const char* string)
{
    struct tm parsed = {0};
    char* end = strptime(string, "%Y-%m-%dT%H:%M:%SZ", &parsed);
    time_t value = end != NULL && *end == 0 ? timegm(&parsed) : -1;
    if (value < 0)
    {
        ci_die("invalid GitHub timestamp");
    }
    return (uint64_t)value;
}

static Buffer ci_read(FILE* file)
{
    Buffer result = {0};
    char bytes[8192];
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), file)) != 0)
    {
        buffer_add(&result, bytes, count);
    }
    if (ferror(file))
    {
        ci_die("read failed");
    }
    buffer_reserve(&result, 0);
    result.data[result.length] = 0;
    return result;
}

#include "admission_git.c"

// gh handles authentication/TLS. No shell, PR-controlled executable, jq program or argument interpolation is used.
static Http ci_transport(const char* method, const char* path, const char* body)
{
    FILE* input = tmpfile();
    int pipes[2];
    if (input == NULL || pipe(pipes) != 0)
    {
        ci_die("cannot create HTTP transport");
    }
    if (body != NULL && fwrite(body, 1, strlen(body), input) != strlen(body))
    {
        ci_die("cannot write HTTP request");
    }
    rewind(input);
    pid_t child = fork();
    if (child < 0)
    {
        ci_die("cannot start gh");
    }
    if (child == 0)
    {
        if (dup2(fileno(input), STDIN_FILENO) < 0 || dup2(pipes[1], STDOUT_FILENO) < 0)
        {
            _exit(127);
        }
        close(pipes[0]);
        close(pipes[1]);
        char* arguments[16] = {"gh", "api", "--hostname", "github.com", "--include", "-H", "Accept: application/vnd.github+json", "-X", (char*)method, (char*)path, NULL};
        if (body != NULL)
        {
            arguments[10] = "--input";
            arguments[11] = "-";
            arguments[12] = NULL;
        }
        execvp("gh", arguments);
        _exit(127);
    }
    close(pipes[1]);
    FILE* output = fdopen(pipes[0], "r");
    if (output == NULL)
    {
        ci_die("cannot read gh output");
    }
    Buffer response = ci_read(output);
    fclose(output);
    fclose(input);
    int process_status;
    if (waitpid(child, &process_status, 0) != child)
    {
        ci_die("cannot wait for gh");
    }
    Http result = {0};
    char* content = strstr(response.data, "\r\n\r\n");
    size_t separator = 4;
    if (content == NULL)
    {
        content = strstr(response.data, "\n\n");
        separator = 2;
    }
    if (sscanf(response.data, "HTTP/%*s %d", &result.status) != 1 || content == NULL || !WIFEXITED(process_status))
    {
        ci_die("gh transport failed for %s", path);
    }
    content += separator;
    result.json = json_parse(*content != 0 ? content : "{}");
    free(response.data);
    return result;
}

static Http ci_http(const char* method, const char* path, const char* body)
{
    if (ci_read_only && strcmp(method, "GET") != 0 && !(strcmp(method, "POST") == 0 && strcmp(path, "graphql") == 0))
    {
        ci_die("a read-only inspection attempted a mutation");
    }
    Http result = ci_fixture_http ? ci_fixture_request(method, path, body) : ci_transport(method, path, body);
    return result;
}

static Http ci_api(const char* method, const char* suffix, const char* body)
{
    char path[1024];
    int count = snprintf(path, sizeof(path), "repos/%s/%s", BUSTER_CI_REPO, suffix);
    if (count < 0 || (size_t)count >= sizeof(path))
    {
        ci_die("API path too long");
    }
    Http result = ci_http(method, path, body);
    if (result.status < 200 || result.status >= 300)
    {
        ci_die("GitHub returned HTTP %d for %s %s; no new admission", result.status, method, suffix);
    }
    return result;
}

static void ci_mutate(const char* method, const char* path, const Buffer* body)
{
    Http result = ci_api(method, path, body != NULL ? body->data : NULL);
    json_free(&result.json);
}

static uint64_t ci_hash(const char* text)
{
    uint64_t value = UINT64_C(14695981039346656037);
    for (const unsigned char* at = (const unsigned char*)text; *at != 0; ++at)
    {
        value = (value ^ *at) * UINT64_C(1099511628211);
    }
    return value;
}

static void ci_json_string(Buffer* out, const char* key, const char* value)
{
    json_quote(out, key);
    buffer_add(out, ":", 1);
    json_quote(out, value);
}

static Buffer ci_state_json(const CiState* state)
{
    Buffer out = {0};
    buffer_printf(&out, "{\"version\":1,\"generation\":%" PRIu64 ",\"dispatch_event\":%" PRIu64 ",\"since_fair\":%u,\"lease\":{\"pr\":%" PRIu64 ",\"run\":%" PRIu64 ",\"attempt\":%" PRIu64 ",\"admitted_at\":%" PRIu64 ",\"expires_at\":%" PRIu64 ",\"revoking\":%s,", state->generation, state->dispatch_event, state->since_fair, state->lease.pr, state->lease.run, state->lease.attempt, state->lease.admitted_at, state->lease.expires_at, state->lease.revoking ? "true" : "false");
    const CiLease* lease = &state->lease;
    ci_json_string(&out, "nonce", lease->nonce);
    buffer_add(&out, ",", 1);
    ci_json_string(&out, "head", lease->head);
    buffer_add(&out, ",", 1);
    ci_json_string(&out, "base", lease->base);
    buffer_add(&out, ",", 1);
    ci_json_string(&out, "merge", lease->merge);
    buffer_add(&out, ",", 1);
    ci_json_string(&out, "workflow_sha", lease->workflow_sha);
    buffer_add(&out, ",", 1);
    ci_json_string(&out, "phase", lease->phase);
    buffer_add(&out, "},\"prs\":[", 9);
    for (size_t i = 0; i < state->pr_count; ++i)
    {
        const CiPr* pr = &state->prs[i];
        buffer_printf(&out, "%s{\"number\":%" PRIu64 ",\"seen_at\":%" PRIu64 ",\"queued_at\":%" PRIu64 ",\"manual\":%d,\"held\":%s,\"command_at\":%" PRIu64 ",\"command_id\":%" PRIu64 ",\"check\":%" PRIu64 ",\"check_hash\":%" PRIu64 ",\"outcome\":%u,\"request_run\":%" PRIu64 ",\"approval_at\":%" PRIu64 ",\"admitted_at\":%" PRIu64 ",\"completed_at\":%" PRIu64 ",", i != 0 ? "," : "", pr->number, pr->seen_at, pr->queued_at, pr->manual, pr->held ? "true" : "false", pr->command_at, pr->command_id, pr->check, pr->check_hash, pr->outcome, pr->request_run, pr->approval_at, pr->admitted_at, pr->completed_at);
        ci_json_string(&out, "head", pr->head);
        buffer_add(&out, ",", 1);
        ci_json_string(&out, "base", pr->base);
        buffer_add(&out, ",", 1);
        ci_json_string(&out, "check_base", pr->check_base);
        buffer_add(&out, ",", 1);
        ci_json_string(&out, "audit", pr->audit);
        buffer_printf(&out, ",\"active_seconds\":%" PRIu64, pr->active_seconds);
        buffer_printf(&out, ",\"head_repo\":%" PRIu64, pr->head_repo);
        buffer_printf(&out, ",\"command_hash\":%" PRIu64, pr->command_hash);
        buffer_printf(&out, ",\"executor_run\":%" PRIu64, pr->executor_run);
        buffer_printf(&out, ",\"signals_at\":%" PRIu64 ",\"cached_approved\":%s,\"signal_approval_at\":%" PRIu64 ",\"cached_code\":%s,", pr->signals_at, pr->cached_approved ? "true" : "false", pr->signal_approval_at, pr->cached_code ? "true" : "false");
        ci_json_string(&out, "code_merge", pr->code_merge);
        buffer_printf(&out, ",\"effective_priority\":%d,\"queue_position\":%" PRIu64 ",\"eligible\":%s,", pr->effective_priority, pr->queue_position, pr->eligible ? "true" : "false");
        ci_json_string(&out, "reason", pr->reason);
        buffer_add(&out, ",", 1);
        ci_json_string(&out, "status_state", pr->status_state);
        buffer_add(&out, "}", 1);
    }
    buffer_add(&out, "],\"overrides\":[", 15);
    for (size_t i = 0; i < state->override_count; ++i)
    {
        const CiOverride* override = &state->overrides[i];
        buffer_printf(&out, "%s{\"run\":%" PRIu64 ",\"pr\":%" PRIu64 ",\"manual\":%d,\"held\":%s,\"retry\":%s,\"queued_at\":%" PRIu64 ",", i != 0 ? "," : "", override->run, override->pr, override->manual, override->held ? "true" : "false", override->retry ? "true" : "false", override->queued_at);
        ci_json_string(&out, "head", override->head);
        buffer_add(&out, "}", 1);
    }
    buffer_add(&out, "]}", 2);
    if (out.length > 60000)
    {
        ci_die("durable state exceeds Checks API budget; no new admission");
    }
    return out;
}

static int ci_state_priority(const Json* json, size_t row)
{
    const char* value = json_field(json, row, "manual");
    uint64_t number = strcmp(value, "-1") == 0 ? 0 : ci_number(value);
    if (number != 0 && number != 1 && number != 4)
    {
        ci_die("invalid priority in durable state");
    }
    int result = strcmp(value, "-1") == 0 ? -1 : (int)number;
    return result;
}

static CiState ci_state_parse(const char* text)
{
    Json json = json_parse(text);
    if (json_number(&json, 0, "version") != 1)
    {
        ci_die("unsupported state schema; no new admission");
    }
    const char* required[] = {"generation", "dispatch_event", "since_fair", "lease", "prs", "overrides"};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
    {
        if (json_get(&json, 0, required[i]) == SIZE_MAX)
        {
            ci_die("incomplete durable state; no new admission");
        }
    }
    CiState state = {0};
    state.generation = json_number(&json, 0, "generation");
    state.dispatch_event = json_number(&json, 0, "dispatch_event");
    state.since_fair = (unsigned)json_number(&json, 0, "since_fair");
    size_t lease = json_get(&json, 0, "lease");
    const char* lease_fields[] = {"pr", "run", "attempt", "nonce", "head", "base", "merge", "workflow_sha", "phase", "admitted_at", "expires_at", "revoking"};
    for (size_t i = 0; i < sizeof(lease_fields) / sizeof(lease_fields[0]); ++i)
    {
        if (json_get(&json, lease, lease_fields[i]) == SIZE_MAX)
        {
            ci_die("incomplete durable lease; no new admission");
        }
    }
    state.lease.pr = json_number(&json, lease, "pr");
    state.lease.run = json_number(&json, lease, "run");
    state.lease.attempt = json_number(&json, lease, "attempt");
    state.lease.admitted_at = json_number(&json, lease, "admitted_at");
    state.lease.expires_at = json_number(&json, lease, "expires_at");
    state.lease.revoking = strcmp(json_field(&json, lease, "revoking"), "true") == 0;
    ci_copy(state.lease.nonce, sizeof(state.lease.nonce), json_field(&json, lease, "nonce"));
    ci_copy(state.lease.head, sizeof(state.lease.head), json_field(&json, lease, "head"));
    ci_copy(state.lease.base, sizeof(state.lease.base), json_field(&json, lease, "base"));
    ci_copy(state.lease.merge, sizeof(state.lease.merge), json_field(&json, lease, "merge"));
    ci_copy(state.lease.workflow_sha, sizeof(state.lease.workflow_sha), json_field(&json, lease, "workflow_sha"));
    ci_copy(state.lease.phase, sizeof(state.lease.phase), json_field(&json, lease, "phase"));
    size_t prs = json_get(&json, 0, "prs");
    if (prs >= json.count || json.rows[prs].type != '[')
    {
        ci_die("missing PR state array");
    }
    for (size_t row = prs + 1; row < json.rows[prs].next; row = json.rows[row].next)
    {
        if (state.pr_count == BUSTER_CI_ITEMS)
        {
            ci_die("too many PR state rows");
        }
        CiPr* pr = &state.prs[state.pr_count++];
        pr->number = json_number(&json, row, "number");
        pr->head_repo = json_number(&json, row, "head_repo");
        pr->seen_at = json_number(&json, row, "seen_at");
        pr->queued_at = json_number(&json, row, "queued_at");
        pr->manual = ci_state_priority(&json, row);
        pr->held = strcmp(json_field(&json, row, "held"), "true") == 0;
        pr->command_at = json_number(&json, row, "command_at");
        pr->command_id = json_number(&json, row, "command_id");
        pr->command_hash = json_number(&json, row, "command_hash");
        pr->executor_run = json_number(&json, row, "executor_run");
        pr->signals_at = json_number(&json, row, "signals_at");
        pr->cached_approved = strcmp(json_field(&json, row, "cached_approved"), "true") == 0;
        pr->signal_approval_at = json_number(&json, row, "signal_approval_at");
        pr->cached_code = strcmp(json_field(&json, row, "cached_code"), "true") == 0;
        ci_copy(pr->code_merge, sizeof(pr->code_merge), json_field(&json, row, "code_merge"));
        pr->check = json_number(&json, row, "check");
        pr->check_hash = json_number(&json, row, "check_hash");
        pr->outcome = (unsigned)json_number(&json, row, "outcome");
        pr->request_run = json_number(&json, row, "request_run");
        pr->approval_at = json_number(&json, row, "approval_at");
        pr->admitted_at = json_number(&json, row, "admitted_at");
        pr->completed_at = json_number(&json, row, "completed_at");
        pr->active_seconds = json_number(&json, row, "active_seconds");
        pr->effective_priority = (int)json_number(&json, row, "effective_priority");
        pr->queue_position = json_number(&json, row, "queue_position");
        pr->eligible = strcmp(json_field(&json, row, "eligible"), "true") == 0;
        ci_copy(pr->reason, sizeof(pr->reason), json_field(&json, row, "reason"));
        ci_copy(pr->status_state, sizeof(pr->status_state), json_field(&json, row, "status_state"));
        ci_copy(pr->head, sizeof(pr->head), json_field(&json, row, "head"));
        ci_copy(pr->base, sizeof(pr->base), json_field(&json, row, "base"));
        ci_copy(pr->check_base, sizeof(pr->check_base), json_field(&json, row, "check_base"));
        ci_copy(pr->audit, sizeof(pr->audit), json_field(&json, row, "audit"));
        if (pr->number == 0 || !ci_sha(pr->head) || !ci_sha(pr->base) || pr->outcome > 2)
        {
            ci_die("invalid PR state identity");
        }
        for (size_t earlier = 0; earlier + 1 < state.pr_count; ++earlier)
        {
            if (state.prs[earlier].number == pr->number)
            {
                ci_die("duplicate PR in durable state");
            }
        }
    }
    size_t overrides = json_get(&json, 0, "overrides");
    if (overrides >= json.count || json.rows[overrides].type != '[')
    {
        ci_die("missing run override array");
    }
    for (size_t row = overrides + 1; row < json.rows[overrides].next; row = json.rows[row].next)
    {
        if (state.override_count == BUSTER_CI_ITEMS)
        {
            ci_die("too many run overrides");
        }
        CiOverride* override = &state.overrides[state.override_count++];
        override->run = json_number(&json, row, "run");
        override->pr = json_number(&json, row, "pr");
        override->manual = ci_state_priority(&json, row);
        override->held = strcmp(json_field(&json, row, "held"), "true") == 0;
        override->retry = strcmp(json_field(&json, row, "retry"), "true") == 0;
        override->queued_at = json_number(&json, row, "queued_at");
        ci_copy(override->head, sizeof(override->head), json_field(&json, row, "head"));
        if (override->run == 0 || !ci_sha(override->head))
        {
            ci_die("invalid run override identity");
        }
    }
    if (*state.lease.nonce != 0 && (!ci_sha(state.lease.head) || !ci_sha(state.lease.merge) || !ci_sha(state.lease.workflow_sha) || (state.lease.pr != 0 && !ci_sha(state.lease.base))))
    {
        ci_die("invalid durable lease identity");
    }
    if ((*state.lease.nonce == 0 && (state.lease.run != 0 || state.lease.pr != 0)) || (*state.lease.nonce != 0 && (state.lease.expires_at <= state.lease.admitted_at || (state.lease.run != 0 && state.lease.attempt == 0))))
    {
        ci_die("inconsistent durable lease; no new admission");
    }
    json_free(&json);
    return state;
}

static CiState ci_load_state(void)
{
    char path[256];
    snprintf(path, sizeof(path), "commits/%s/check-runs?check_name=9700X%%20controller%%20state%%20v1&filter=all&per_page=100", BUSTER_CI_STATE_SHA);
    Http response = ci_api("GET", path, NULL);
    size_t array = json_get(&response.json, 0, "check_runs");
    CiState state = {0};
    if (json_number(&response.json, 0, "total_count") > 100 || array >= response.json.count)
    {
        ci_die("ambiguous/missing controller state; no new admission");
    }
    uint64_t id = 0;
    for (size_t row = array + 1; row < response.json.rows[array].next; row = response.json.rows[row].next)
    {
        size_t app = json_get(&response.json, row, "app");
        if (json_number(&response.json, app, "id") == 15368 && strcmp(json_field(&response.json, app, "slug"), "github-actions") == 0 && strcmp(json_field(&response.json, row, "external_id"), BUSTER_CI_STATE_ID) == 0)
        {
            uint64_t current = json_number(&response.json, row, "id");
            if (current > id)
            {
                size_t output = json_get(&response.json, row, "output");
                state = ci_state_parse(json_field(&response.json, output, "text"));
                id = current;
            }
        }
    }
    state.check = id;
    Buffer canonical = ci_state_json(&state);
    state.saved_json = canonical.data;
    json_free(&response.json);
    return state;
}

static void ci_save_state(CiState* state)
{
    Buffer payload = ci_state_json(state);
    if (state->check == 0 || state->saved_json == NULL || strcmp(state->saved_json, payload.data) != 0)
    {
        Buffer request = {0};
        buffer_add(&request, "{", 1);
        if (state->check == 0)
        {
            ci_json_string(&request, "name", BUSTER_CI_STATE_NAME);
            buffer_add(&request, ",", 1);
            ci_json_string(&request, "head_sha", BUSTER_CI_STATE_SHA);
            buffer_add(&request, ",", 1);
            ci_json_string(&request, "external_id", BUSTER_CI_STATE_ID);
            buffer_add(&request, ",", 1);
        }
        buffer_add(&request, "\"status\":\"in_progress\",\"output\":{\"title\":\"Serialized 9700X controller ledger (schema v1)\",\"summary\":", strlen("\"status\":\"in_progress\",\"output\":{\"title\":\"Serialized 9700X controller ledger (schema v1)\",\"summary\":"));
        Buffer summary = {0};
        buffer_printf(&summary, "Generation %" PRIu64 ". Pool capacity: one. Lease: %s; run %" PRIu64 "; PR %" PRIu64 ". The JSON in this check's text is the versioned controller/guard interface. Only the serialized default-branch controller writes it.", state->generation, *state->lease.nonce != 0 ? state->lease.nonce : "free", state->lease.run, state->lease.pr);
        json_quote(&request, summary.data);
        buffer_add(&request, ",\"text\":", 8);
        json_quote(&request, payload.data);
        buffer_add(&request, "}}", 2);
        char path[80];
        snprintf(path, sizeof(path), "check-runs");
        if (state->check != 0)
        {
            snprintf(path, sizeof(path), "check-runs/%" PRIu64, state->check);
        }
        Http response = ci_api(state->check == 0 ? "POST" : "PATCH", path, request.data);
        state->check = json_number(&response.json, 0, "id");
        if (state->check == 0)
        {
            ci_die("state write did not return a check ID");
        }
        free(state->saved_json);
        state->saved_json = strdup(payload.data);
        if (state->saved_json == NULL)
        {
            ci_die("cannot retain saved state");
        }
        json_free(&response.json);
        free(request.data);
        free(summary.data);
    }
    free(payload.data);
}

typedef struct CiRun CiRun;
struct CiRun
{
    uint64_t id;
    uint64_t head_repo;
    uint64_t attempt;
    uint64_t pr;
    uint64_t created_at;
    char head[BUSTER_CI_SHA];
    char event[32];
    char branch[256];
    char title[256];
    char status[32];
    char conclusion[32];
    uint64_t workflow;
    bool hardware;
    bool gate;
    char execution_status[32];
    uint64_t started_at;
    uint64_t completed_at;
};

static CiRun ci_run_parse(const Json* json, size_t row)
{
    CiRun run = {0};
    run.id = json_number(json, row, "id");
    run.attempt = json_number(json, row, "run_attempt");
    run.workflow = json_number(json, row, "workflow_id");
    size_t head_repo = json_get(json, row, "head_repository");
    run.head_repo = json_number(json, head_repo, "id");
    ci_copy(run.head, sizeof(run.head), json_field(json, row, "head_sha"));
    ci_copy(run.event, sizeof(run.event), json_field(json, row, "event"));
    ci_copy(run.branch, sizeof(run.branch), json_field(json, row, "head_branch"));
    // Titles are untrusted and can be large; only an exact, bounded scheduler title is relevant.
    const char* title = json_field(json, row, "display_title");
    if (strlen(title) < sizeof(run.title))
    {
        ci_copy(run.title, sizeof(run.title), title);
    }
    ci_copy(run.status, sizeof(run.status), json_field(json, row, "status"));
    ci_copy(run.conclusion, sizeof(run.conclusion), json_field(json, row, "conclusion"));
    run.created_at = ci_time(json_field(json, row, "created_at"));
    size_t prs = json_get(json, row, "pull_requests");
    if (prs < json->count && json->rows[prs].type == '[' && prs + 1 < json->rows[prs].next)
    {
        size_t first = prs + 1;
        if (json->rows[first].next == json->rows[prs].next)
        {
            run.pr = json_number(json, first, "number");
        }
    }
    return run;
}

static void ci_associate_run(CiRun* run, const CiState* state)
{
    if (run->pr == 0 && run->head_repo != 0 && strcmp(run->event, "pull_request") == 0)
    {
        uint64_t number = 0;
        unsigned matches = 0;
        for (size_t i = 0; i < state->pr_count; ++i)
        {
            const CiPr* pr = &state->prs[i];
            if (pr->head_repo == run->head_repo && strcmp(pr->head, run->head) == 0) { number = pr->number; ++matches; }
        }
        // Fork run payloads may omit pull_requests. A unique live repository-ID/SHA match is safe;
        // multiple PRs with the same head remain ambiguous and cannot be run-control targets.
        if (matches == 1) run->pr = number;
    }
}

static bool ci_run_path(const char* path)
{
    // Workflow runs may use the REST API's ref-qualified path. The suffix is not a source
    // attestation: the workflow ID and each caller's event/branch/head/lease checks remain required.
    size_t length = strlen(BUSTER_CI_WORKFLOW);
    return strncmp(path, BUSTER_CI_WORKFLOW, length) == 0 &&
           (path[length] == 0 || (path[length] == '@' && path[length + 1] != 0));
}

static bool ci_run_workflow(const Json* json)
{
    return ci_run_path(json_field(json, 0, "path")) && json_number(json, 0, "workflow_id") == ci_workflow();
}

static CiRun ci_get_run(uint64_t id)
{
    char path[100];
    snprintf(path, sizeof(path), "actions/runs/%" PRIu64, id);
    Http response = ci_api("GET", path, NULL);
    CiRun result = ci_run_parse(&response.json, 0);
    if (!ci_run_workflow(&response.json))
    {
        ci_die("run does not belong to the 9700X qualification workflow");
    }
    json_free(&response.json);
    return result;
}

static bool ci_command_run(uint64_t id, CiRun* run)
{
    char path[180];
    snprintf(path, sizeof(path), "repos/%s/actions/runs/%" PRIu64, BUSTER_CI_REPO, id);
    Http response = ci_http("GET", path, NULL);
    if (response.status != 200 && response.status != 404)
    {
        ci_die("cannot verify command's run identity (HTTP %d)", response.status);
    }
    bool valid = response.status == 200 && ci_run_workflow(&response.json);
    if (valid)
    {
        *run = ci_run_parse(&response.json, 0);
    }
    json_free(&response.json);
    return valid;
}

static void ci_jobs(CiRun* run, bool* executor_success, bool* guard_success)
{
    char path[160];
    snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/attempts/%" PRIu64 "/jobs?per_page=100", run->id, run->attempt);
    Http response = ci_api("GET", path, NULL);
    if (json_number(&response.json, 0, "total_count") > 100)
    {
        ci_die("qualification contains too many jobs");
    }
    size_t jobs = json_get(&response.json, 0, "jobs");
    if (jobs >= response.json.count)
    {
        ci_die("missing jobs response");
    }
    *executor_success = false;
    *guard_success = false;
    for (size_t row = jobs + 1; row < response.json.rows[jobs].next; row = response.json.rows[row].next)
    {
        const char* name = json_field(&response.json, row, "name");
        const char* status = json_field(&response.json, row, "status");
        const char* conclusion = json_field(&response.json, row, "conclusion");
        bool hardware = strcmp(name, BUSTER_CI_EXECUTOR) == 0 || strcmp(name, BUSTER_CI_REQUIRED) == 0;
        size_t labels = json_get(&response.json, row, "labels");
        if (labels < response.json.count && response.json.rows[labels].type == '[')
        {
            for (size_t label = labels + 1; label < response.json.rows[labels].next; label = response.json.rows[label].next)
            {
                hardware = hardware || strcmp(json_text(&response.json, label), "ryzen-9700x") == 0;
            }
        }
        if (hardware)
        {
            ci_copy(run->execution_status, sizeof(run->execution_status), status);
            const char* start = json_field(&response.json, row, "started_at");
            const char* end = json_field(&response.json, row, "completed_at");
            run->started_at = *start != 0 ? ci_time(start) : 0;
            run->completed_at = *end != 0 ? ci_time(end) : 0;
        }
        if (hardware && strcmp(status, "completed") != 0)
        {
            run->hardware = true;
        }
        if (hardware && strcmp(conclusion, "success") == 0 && *json_field(&response.json, row, "runner_name") != 0)
        {
            *executor_success = true;
        }
        if (strcmp(name, "admission") == 0)
        {
            run->gate = strcmp(status, "skipped") != 0 && strcmp(conclusion, "skipped") != 0;
            *guard_success = strcmp(conclusion, "success") == 0;
        }
    }
    json_free(&response.json);
}

static CiIdentity ci_graph_pr(const Json* json, size_t row)
{
    CiIdentity result = {0};
    result.number = json_number(json, row, "number");
    result.open = strcmp(json_field(json, row, "state"), "OPEN") == 0;
    size_t head_ref = json_get(json, row, "headRef");
    size_t base_ref = json_get(json, row, "baseRef");
    size_t head = json_get(json, head_ref, "target");
    size_t base = json_get(json, base_ref, "target");
    const char* head_sha = json_field(json, head, "oid");
    const char* base_sha = json_field(json, base, "oid");
    bool live = ci_sha(head_sha) && ci_sha(base_sha);
    ci_copy(result.head, sizeof(result.head), live ? head_sha : json_field(json, row, "headRefOid"));
    ci_copy(result.base, sizeof(result.base), live ? base_sha : json_field(json, row, "baseRefOid"));
    size_t repository = json_get(json, row, "headRepository");
    result.head_repo = json_number(json, repository, "databaseId");
    result.fork = result.head_repo != UINT64_C(1421805693);
    const char* mergeable = json_field(json, row, "mergeable");
    result.mergeable = !live ? -1 : strcmp(mergeable, "MERGEABLE") == 0 ? 1 : strcmp(mergeable, "CONFLICTING") == 0 ? 0 : -1;
    // Only the original API fixtures use the supplied test-merge SHA. Production derives a canonical merge
    // from the live refs: GitHub may defer updating baseRefOid/potentialMergeCommit for up to twelve hours.
    if (ci_fixture_http)
    {
        size_t merge = json_get(json, row, "potentialMergeCommit");
        ci_copy(result.merge, sizeof(result.merge), json_field(json, merge, "oid"));
    }
    if (result.number == 0 || !ci_sha(result.head) || !ci_sha(result.base)) ci_die("invalid live PR identity");
    return result;
}

static void ci_resolve_merge(CiIdentity* identity, const CiLease* proof)
{
    if (!ci_fixture_http && identity->open && identity->mergeable >= 0)
    {
        if (proof != NULL)
        {
            // A read-only worker trusts only an authenticated controller lease for this exact immutable pair.
            // It rechecks the live refs but performs no Git merge/fetch while holding the physical host lock.
            if (proof->pr == identity->number && strcmp(proof->head, identity->head) == 0 && strcmp(proof->base, identity->base) == 0)
            {
                identity->mergeable = 1;
                ci_copy(identity->merge, sizeof(identity->merge), proof->merge);
            }
        }
        else identity->mergeable = ci_git_merge(identity->base, identity->head, identity->merge);
    }
}

static CiIdentity ci_get_pr_mode(uint64_t number, bool retry_unknown, const CiLease* proof)
{
    CiIdentity result = {0};
    unsigned attempts = retry_unknown ? 3 : 1;
    for (unsigned i = 0; i < attempts; ++i)
    {
        Buffer query = {0}, request = {0};
        buffer_printf(&query, "query { repository(owner: \"buster14a\", name: \"buster\") { pullRequest(number: %" PRIu64 ") { " BUSTER_CI_PR_FIELDS " } } }", number);
        buffer_add(&request, "{\"query\":", 9); json_quote(&request, query.data); buffer_add(&request, "}", 1);
        Http response = ci_http("POST", "graphql", request.data);
        free(query.data); free(request.data);
        if (response.status != 200 || json_get(&response.json, 0, "errors") != SIZE_MAX) ci_die("current PR metadata query failed");
        size_t data = json_get(&response.json, 0, "data");
        size_t repository = json_get(&response.json, data, "repository");
        size_t pr = json_get(&response.json, repository, "pullRequest");
        result = ci_graph_pr(&response.json, pr);
        if (result.number != number) ci_die("current PR identity changed");
        json_free(&response.json);
        ci_resolve_merge(&result, proof);
        if (result.mergeable != -1 || !result.open)
        {
            break;
        }
        if (i + 1 < attempts && !ci_fixture_http)
        {
            sleep(1);
        }
    }
    return result;
}

static CiIdentity ci_get_pr(uint64_t number, bool retry_unknown)
{
    return ci_get_pr_mode(number, retry_unknown, NULL);
}

static bool ci_document(const char* path)
{
    size_t length = strlen(path);
    bool markdown = length >= 3 && strcmp(path + length - 3, ".md") == 0;
    return (strncmp(path, "docs/", 5) == 0 || markdown) && strncmp(path, "docs/performance/", 17) != 0;
}

static bool ci_pr_code(const CiIdentity* current)
{
    if (!ci_fixture_http && current->mergeable == 1) return ci_git_code(current);
    bool code = current->mergeable != 1;
    if (!code)
    {
        // Immutable endpoints prevent a force-push/force-push-back race mixing a docs-only diff with another head.
        // base is a parent of merge, so this is precisely the merge's changes against the selected base.
        char path[180];
        snprintf(path, sizeof(path), "compare/%s...%s?per_page=1&page=1", current->base, current->merge);
        Http response = ci_api("GET", path, NULL);
        size_t files = json_get(&response.json, 0, "files");
        size_t count = 0;
        if (files >= response.json.count || response.json.rows[files].type != '[')
        {
            ci_die("missing changed-file array");
        }
        for (size_t row = files + 1; row < response.json.rows[files].next; row = response.json.rows[row].next)
        {
            const char* previous = json_field(&response.json, row, "previous_filename");
            code = code || !ci_document(json_field(&response.json, row, "filename")) || (*previous != 0 && !ci_document(previous));
            ++count;
        }
        json_free(&response.json);
        // Comparison files appear on page one only, capped at 300. A truncated diff cannot prove docs-only.
        code = code || count >= 300;
    }
    return code;
}

static uint64_t ci_closed_after(uint64_t number)
{
    uint64_t closed_at = 0;
    for (unsigned page = 1; page <= 100; ++page)
    {
        char path[120];
        snprintf(path, sizeof(path), "issues/%" PRIu64 "/events?per_page=100&page=%u", number, page);
        Http response = ci_api("GET", path, NULL);
        if (response.json.rows[0].type != '[')
        {
            ci_die("missing PR lifecycle events");
        }
        size_t count = 0;
        for (size_t row = 1; row < response.json.rows[0].next; row = response.json.rows[row].next)
        {
            ++count;
            if (strcmp(json_field(&response.json, row, "event"), "closed") == 0)
            {
                uint64_t stamp = ci_time(json_field(&response.json, row, "created_at"));
                closed_at = closed_at > stamp ? closed_at : stamp;
            }
        }
        json_free(&response.json);
        if (count < 100)
        {
            break;
        }
        if (page == 100)
        {
            ci_die("lifecycle pagination limit reached; no new admission");
        }
    }
    return closed_at;
}

typedef struct CiPermission CiPermission;
struct CiPermission
{
    uint64_t id;
    char login[64];
    bool trusted;
};

static CiPermission ci_permissions[BUSTER_CI_ITEMS * 4];
static size_t ci_permission_count;

static bool ci_authorized(const Json* json, size_t user)
{
    const char* login = json_field(json, user, "login");
    uint64_t id = json_number(json, user, "id");
    bool safe = *login != 0 && strlen(login) < 64 && id != 0;
    for (const char* at = login; safe && *at != 0; ++at)
    {
        safe = isalnum((unsigned char)*at) || *at == '-' || *at == '_' || *at == '[' || *at == ']';
    }
    bool result = false;
    bool cached = false;
    for (size_t i = 0; safe && i < ci_permission_count; ++i)
    {
        if (ci_permissions[i].id == id && strcmp(ci_permissions[i].login, login) == 0)
        {
            result = ci_permissions[i].trusted;
            cached = true;
        }
    }
    if (safe && !cached)
    {
        char path[200];
        snprintf(path, sizeof(path), "repos/%s/collaborators/%s/permission", BUSTER_CI_REPO, login);
        Http response = ci_http("GET", path, NULL);
        if (response.status != 200 && response.status != 404)
        {
            ci_die("permission verification failed (HTTP %d); no new admission", response.status);
        }
        size_t actual_user = json_get(&response.json, 0, "user");
        result = response.status == 200 && json_number(&response.json, actual_user, "id") == id && ci_trusted(json_field(&response.json, 0, "permission"));
        json_free(&response.json);
        if (ci_permission_count < sizeof(ci_permissions) / sizeof(ci_permissions[0]))
        {
            CiPermission* entry = &ci_permissions[ci_permission_count++];
            entry->id = id;
            ci_copy(entry->login, sizeof(entry->login), login);
            entry->trusted = result;
        }
    }
    return result;
}

static CiPr* ci_find_pr(CiState* state, uint64_t number)
{
    CiPr* result = NULL;
    for (size_t i = 0; i < state->pr_count; ++i)
    {
        if (state->prs[i].number == number)
        {
            result = &state->prs[i];
        }
    }
    return result;
}

static CiOverride* ci_find_override(CiState* state, uint64_t run, const char* head, bool create)
{
    CiOverride* result = NULL;
    for (size_t i = 0; i < state->override_count; ++i)
    {
        if (state->overrides[i].run == run && strcmp(state->overrides[i].head, head) == 0)
        {
            result = &state->overrides[i];
        }
    }
    if (result == NULL && create)
    {
        if (state->override_count == BUSTER_CI_ITEMS)
        {
            ci_die("run override capacity exhausted; no new admission");
        }
        result = &state->overrides[state->override_count++];
        memset(result, 0, sizeof(*result));
        result->run = run;
        result->manual = -1;
        ci_copy(result->head, sizeof(result->head), head);
    }
    return result;
}

static void ci_apply(CiState* state, CiPr* pr, CiCommand command, uint64_t now, const char* actor, uint64_t source)
{
    CiOverride* override = NULL;
    bool valid = true;
    if (command.run != 0)
    {
        CiRun run = {0};
        valid = ci_command_run(command.run, &run);
        if (valid) ci_associate_run(&run, state);
        if (pr != NULL && (!valid || run.pr != pr->number || strcmp(run.head, pr->head) != 0 || run.id != pr->request_run))
        {
            snprintf(pr->audit, sizeof(pr->audit), "Rejected command by @%s (source %" PRIu64 "): run is not this revision's current request.", actor, source);
            valid = false;
        }
        if (pr == NULL && (run.pr != 0 || (strcmp(run.event, "push") != 0 && strcmp(run.event, "schedule") != 0 && strcmp(run.event, "workflow_dispatch") != 0) || strcmp(run.branch, "main") != 0 || strncmp(run.title, "9700X lease ", 12) == 0))
        {
            valid = false;
        }
        if (valid)
        {
            override = ci_find_override(state, run.id, run.head, true);
            override->pr = pr != NULL ? pr->number : 0;
        }
    }
    if (valid && command.kind == CI_COMMAND_PRIORITY)
    {
        if (override != NULL)
        {
            override->manual = command.priority;
        }
        else if (pr != NULL)
        {
            pr->manual = command.priority;
        }
    }
    else if (valid && (command.kind == CI_COMMAND_HOLD || command.kind == CI_COMMAND_RESUME))
    {
        if (override != NULL)
        {
            override->held = command.kind == CI_COMMAND_HOLD;
        }
        else if (pr != NULL)
        {
            pr->held = command.kind == CI_COMMAND_HOLD;
        }
    }
    else if (valid && command.kind == CI_COMMAND_RETRY)
    {
        if (pr != NULL && (state->lease.pr != pr->number || *state->lease.nonce == 0))
        {
            pr->outcome = 0;
            pr->queued_at = now;
            pr->admitted_at = 0;
            pr->completed_at = 0;
            pr->check = 0;
            pr->check_hash = 0;
        }
        if (override != NULL)
        {
            override->retry = true;
            override->queued_at = now;
        }
    }
    if (valid && pr != NULL)
    {
        snprintf(pr->audit, sizeof(pr->audit), "Applied /ci %s by @%s (source %" PRIu64 "), target %s; priority %s, %s. No running work was preempted.", ci_command_name(command.kind), actor, source, override != NULL ? "run" : "PR", ci_priority_name(override != NULL ? override->manual : pr->manual), (override != NULL ? override->held : pr->held) ? "held" : "released");
    }
    printf("command source=%" PRIu64 " actor=%s pr=%" PRIu64 " run=%" PRIu64 " kind=%s applied=%s\n", source, actor, pr != NULL ? pr->number : 0, command.run, ci_command_name(command.kind), valid ? "true" : "false");
}

typedef struct CiComment CiComment;
struct CiComment
{
    uint64_t id;
    uint64_t updated_at;
    uint64_t user_id;
    uint64_t hash;
    char login[64];
    CiCommand command;
    bool trusted;
};

static int ci_comment_order(const void* left, const void* right)
{
    const CiComment* a = left;
    const CiComment* b = right;
    int result = a->updated_at < b->updated_at ? -1 : a->updated_at > b->updated_at ? 1 : a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
    return result;
}

static void ci_comments(CiState* state, CiPr* pr, CiIdentity* current, uint64_t now)
{
    CiComment comments[1024];
    size_t count = 0;
    for (unsigned page = 1; page <= 100; ++page)
    {
        char path[120];
        snprintf(path, sizeof(path), "issues/%" PRIu64 "/comments?per_page=100&page=%u", pr->number, page);
        Http response = ci_api("GET", path, NULL);
        if (response.json.rows[0].type != '[')
        {
            ci_die("missing conversation comment array");
        }
        size_t page_count = 0;
        for (size_t row = 1; row < response.json.rows[0].next; row = response.json.rows[row].next)
        {
            ++page_count;
            const char* body = json_field(&response.json, row, "body");
            CiCommand command = ci_command(body);
            if (command.kind != CI_COMMAND_NONE || strncmp(body, "/ci ", 4) == 0)
            {
                if (count == sizeof(comments) / sizeof(comments[0]))
                {
                    ci_die("too many command comments; no new admission");
                }
                CiComment* comment = &comments[count++];
                comment->id = json_number(&response.json, row, "id");
                comment->updated_at = ci_time(json_field(&response.json, row, "updated_at"));
                size_t user = json_get(&response.json, row, "user");
                comment->user_id = json_number(&response.json, user, "id");
                ci_copy(comment->login, sizeof(comment->login), json_field(&response.json, user, "login"));
                comment->trusted = ci_authorized(&response.json, user);
                comment->command = command;
                comment->hash = ci_hash(json_field(&response.json, row, "body"));
            }
        }
        json_free(&response.json);
        if (page_count < 100)
        {
            break;
        }
        if (page == 100)
        {
            ci_die("conversation pagination limit reached; no new admission");
        }
    }
    qsort(comments, count, sizeof(comments[0]), ci_comment_order);
    for (size_t i = 0; i < count; ++i)
    {
        CiComment* comment = &comments[i];
        if (comment->command.kind == CI_COMMAND_APPROVE)
        {
            if (comment->trusted && ci_comment_current(pr, comment->updated_at))
            {
                current->approved = true;
                if (current->approval_at == 0 || comment->updated_at < current->approval_at)
                {
                    current->approval_at = comment->updated_at;
                }
            }
        }
        else if (ci_command_new(pr, comment->updated_at, comment->id) || (pr->command_at == comment->updated_at && pr->command_id == comment->id && pr->command_hash != comment->hash))
        {
            if (comment->trusted && comment->command.kind == CI_COMMAND_NONE)
            {
                snprintf(pr->audit, sizeof(pr->audit), "Rejected invalid CI command by @%s (comment %" PRIu64 "); use the documented standalone command syntax.", comment->login, comment->id);
            }
            else if (comment->trusted)
            {
                ci_apply(state, pr, comment->command, now, comment->login, comment->id);
            }
            else
            {
                snprintf(pr->audit, sizeof(pr->audit), "Rejected command by @%s (comment %" PRIu64 "): repository write/maintain/admin permission is required.", comment->login, comment->id);
            }
            pr->command_at = comment->updated_at;
            pr->command_id = comment->id;
            pr->command_hash = comment->hash;
        }
    }
}

typedef struct CiReview CiReview;
struct CiReview
{
    uint64_t user;
    uint64_t id;
    uint64_t submitted;
    char sha[BUSTER_CI_SHA];
    char state[32];
};

static void ci_reviews(CiIdentity* current)
{
    CiReview decisions[256] = {0};
    size_t count = 0;
    for (unsigned page = 1; page <= 100; ++page)
    {
        char path[120];
        snprintf(path, sizeof(path), "pulls/%" PRIu64 "/reviews?per_page=100&page=%u", current->number, page);
        Http response = ci_api("GET", path, NULL);
        if (response.json.rows[0].type != '[')
        {
            ci_die("missing review array");
        }
        size_t page_count = 0;
        for (size_t row = 1; row < response.json.rows[0].next; row = response.json.rows[row].next)
        {
            ++page_count;
            const char* decision = json_field(&response.json, row, "state");
            bool meaningful = strcmp(decision, "APPROVED") == 0 || strcmp(decision, "CHANGES_REQUESTED") == 0 || strcmp(decision, "DISMISSED") == 0;
            size_t user = json_get(&response.json, row, "user");
            if (meaningful && ci_authorized(&response.json, user))
            {
                uint64_t user_id = json_number(&response.json, user, "id");
                size_t at = 0;
                while (at < count && decisions[at].user != user_id)
                {
                    ++at;
                }
                if (at == count)
                {
                    if (count == sizeof(decisions) / sizeof(decisions[0]))
                    {
                        ci_die("too many trusted reviewers");
                    }
                    ++count;
                }
                uint64_t id = json_number(&response.json, row, "id");
                if (id >= decisions[at].id)
                {
                    decisions[at].user = user_id;
                    decisions[at].id = id;
                    ci_copy(decisions[at].sha, sizeof(decisions[at].sha), json_field(&response.json, row, "commit_id"));
                    ci_copy(decisions[at].state, sizeof(decisions[at].state), decision);
                    decisions[at].submitted = ci_time(json_field(&response.json, row, "submitted_at"));
                }
            }
        }
        json_free(&response.json);
        if (page_count < 100)
        {
            break;
        }
        if (page == 100)
        {
            ci_die("review pagination limit reached");
        }
    }
    bool changes = false;
    for (size_t i = 0; i < count; ++i)
    {
        CiReview* review = &decisions[i];
        if (strcmp(review->sha, current->head) == 0)
        {
            changes = changes || strcmp(review->state, "CHANGES_REQUESTED") == 0;
            if (strcmp(review->state, "APPROVED") == 0)
            {
                current->approved = true;
                if (current->approval_at == 0 || review->submitted < current->approval_at)
                {
                    current->approval_at = review->submitted;
                }
            }
        }
    }
    // A current changes request vetoes automatic approval, including an explicit approval comment.
    if (changes)
    {
        current->approved = false;
        current->approval_at = 0;
    }
}

static size_t ci_list_prs(CiIdentity* identities)
{
    // One cheap metadata-only GraphQL query, rather than N REST requests on every run event.
    const char* query = "query { repository(owner: \"buster14a\", name: \"buster\") { pullRequests(states: OPEN, first: 65, orderBy: {field: CREATED_AT, direction: ASC}) { pageInfo { hasNextPage } nodes { " BUSTER_CI_PR_FIELDS " } } } }";
    Buffer request = {0};
    buffer_add(&request, "{\"query\":", 9);
    json_quote(&request, query);
    buffer_add(&request, "}", 1);
    Http response = ci_http("POST", "graphql", request.data);
    free(request.data);
    if (response.status != 200 || json_get(&response.json, 0, "errors") != SIZE_MAX)
    {
        ci_die("metadata query failed; no admission");
    }
    size_t data = json_get(&response.json, 0, "data");
    size_t repository = json_get(&response.json, data, "repository");
    size_t prs = json_get(&response.json, repository, "pullRequests");
    size_t nodes = json_get(&response.json, prs, "nodes");
    size_t pagination = json_get(&response.json, prs, "pageInfo");
    if (nodes >= response.json.count || strcmp(json_field(&response.json, pagination, "hasNextPage"), "false") != 0)
    {
        ci_die("PR metadata pagination/bounds failure");
    }
    size_t count = 0;
    for (size_t row = nodes + 1; row < response.json.rows[nodes].next; row = response.json.rows[row].next)
    {
        if (count == BUSTER_CI_ITEMS)
        {
            ci_die("more than %u open PRs; raise the reviewed state bound before admission", BUSTER_CI_ITEMS);
        }
        CiIdentity* identity = &identities[count++];
        *identity = ci_graph_pr(&response.json, row);
    }
    json_free(&response.json);
    for (size_t i = 0; i < count; ++i)
    {
        if (identities[i].mergeable < 0)
        {
            identities[i] = ci_get_pr(identities[i].number, true);
        }
        else ci_resolve_merge(&identities[i], NULL);
    }
    return count;
}

static size_t ci_list_runs(CiRun* runs, uint64_t workflow, const char* status)
{
    size_t count = 0;
    for (unsigned page = 1; page <= 100; ++page)
    {
        char path[180];
        snprintf(path, sizeof(path), "actions/workflows/%" PRIu64 "/runs?per_page=100&page=%u%s%s", workflow, page, status != NULL ? "&status=" : "", status != NULL ? status : "");
        Http response = ci_api("GET", path, NULL);
        size_t array = json_get(&response.json, 0, "workflow_runs");
        if (array >= response.json.count)
        {
            ci_die("missing workflow run array");
        }
        size_t page_count = 0;
        for (size_t row = array + 1; row < response.json.rows[array].next; row = response.json.rows[row].next)
        {
            ++page_count;
            if (count == BUSTER_CI_CANDIDATES)
            {
                ci_die("too many matching workflow runs; no new admission");
            }
            runs[count++] = ci_run_parse(&response.json, row);
        }
        json_free(&response.json);
        if (page_count < 100)
        {
            break;
        }
    }
    return count;
}

static void ci_cancel(uint64_t run, uint64_t workflow)
{
    CiRun current = ci_get_run(run);
    if (current.workflow != workflow)
    {
        ci_die("cancellation identity changed");
    }
    if (strcmp(current.status, "completed") != 0)
    {
        char path[200];
        snprintf(path, sizeof(path), "repos/%s/actions/runs/%" PRIu64 "/cancel", BUSTER_CI_REPO, run);
        Http response = ci_http("POST", path, NULL);
        // Completion can race the cancellation request. Neither 409 nor expiry releases a live run's lease.
        if ((response.status < 200 || response.status >= 300) && response.status != 409)
        {
            ci_die("cancellation returned HTTP %d; retaining the lease", response.status);
        }
        json_free(&response.json);
    }
}

static CiIdentity* ci_identity(CiIdentity* identities, size_t count, uint64_t number)
{
    CiIdentity* result = NULL;
    for (size_t i = 0; i < count; ++i)
    {
        if (identities[i].number == number)
        {
            result = &identities[i];
        }
    }
    return result;
}

static uint64_t ci_workflow(void)
{
    if (ci_workflow_id != 0)
    {
        return ci_workflow_id;
    }
    Http response = ci_api("GET", "actions/workflows/9700x.yml", NULL);
    uint64_t result = json_number(&response.json, 0, "id");
    if (result == 0 || strcmp(json_field(&response.json, 0, "path"), BUSTER_CI_WORKFLOW) != 0 || strcmp(json_field(&response.json, 0, "state"), "active") != 0)
    {
        ci_die("qualification workflow unavailable; no new admission");
    }
    json_free(&response.json);
    ci_workflow_id = result;
    return result;
}

static void ci_main_sha(char* sha)
{
    Http response = ci_api("GET", "git/ref/heads/main", NULL);
    size_t object = json_get(&response.json, 0, "object");
    ci_copy(sha, BUSTER_CI_SHA, json_field(&response.json, object, "sha"));
    if (!ci_sha(sha))
    {
        ci_die("invalid default-branch identity");
    }
    json_free(&response.json);
}

static void ci_dispatch_event(CiState* state, const Json* event, uint64_t now, uint64_t event_id)
{
    if (event_id > state->dispatch_event)
    {
        size_t sender = json_get(event, 0, "sender");
        bool authorized = ci_authorized(event, sender);
        if (authorized)
        {
            size_t inputs = json_get(event, 0, "inputs");
            const char* pr_text = json_field(event, inputs, "pr");
            const char* run_text = json_field(event, inputs, "run");
            uint64_t number = *pr_text != 0 ? ci_number(pr_text) : 0;
            uint64_t run_id = *run_text != 0 ? ci_number(run_text) : 0;
            CiPr* pr = ci_find_pr(state, number);
            if (run_id != 0 && number == 0)
            {
                CiRun run = {0};
                if (ci_command_run(run_id, &run))
                {
                    ci_associate_run(&run, state);
                    pr = ci_find_pr(state, run.pr);
                }
            }
            const char* action = json_field(event, inputs, "action");
            const char* priority = json_field(event, inputs, "priority");
            Buffer line = {0};
            if (strcmp(action, "priority") == 0)
            {
                buffer_printf(&line, "/ci priority %s", priority);
            }
            else if (strcmp(action, "clear") == 0)
            {
                buffer_printf(&line, "/ci priority normal");
            }
            else
            {
                buffer_printf(&line, "/ci %s", action);
            }
            if (run_id != 0)
            {
                buffer_printf(&line, " run %" PRIu64, run_id);
            }
            CiCommand command = ci_command(line.data);
            if ((pr == NULL && run_id == 0) || (number != 0 && pr == NULL) || command.kind == CI_COMMAND_NONE || command.kind == CI_COMMAND_APPROVE)
            {
                printf("Rejected dispatch %" PRIu64 ": invalid command or closed/missing target PR.\n", event_id);
            }
            else
            {
                ci_apply(state, pr, command, now, json_field(event, sender, "login"), event_id);
            }
            free(line.data);
        }
        else
        {
            printf("Rejected dispatch %" PRIu64 ": sender lacks repository write/maintain/admin permission.\n", event_id);
        }
        state->dispatch_event = event_id;
    }
}

static void ci_recover_dispatches(CiState* state, uint64_t now)
{
    Http response = ci_api("GET", "actions/workflows/9700x-control.yml/runs?event=workflow_dispatch&per_page=100", NULL);
    size_t array = json_get(&response.json, 0, "workflow_runs");
    if (array >= response.json.count)
    {
        ci_die("missing manual-control history");
    }
    size_t rows[100];
    size_t count = 0;
    for (size_t row = array + 1; row < response.json.rows[array].next; row = response.json.rows[row].next)
    {
        if (count == 100)
        {
            ci_die("manual-control history overflow");
        }
        rows[count++] = row;
    }
    if (count == 100 && json_number(&response.json, rows[count - 1], "id") > state->dispatch_event)
    {
        ci_die("more than 100 unprocessed manual controls; operator recovery required");
    }
    // The Actions API returns reverse chronological runs. Replaying oldest first preserves reset/hold semantics.
    while (count != 0)
    {
        size_t row = rows[--count];
        uint64_t id = json_number(&response.json, row, "id");
        const char* title = json_field(&response.json, row, "display_title");
        if (id > state->dispatch_event && strcmp(json_field(&response.json, row, "head_branch"), "main") == 0)
        {
            char pr[21] = {0};
            char run[21] = {0};
            char action[16] = {0};
            char priority[16] = {0};
            int used = 0;
            int fields = sscanf(title, "9700X command pr %20[0-9] run %20[0-9] action %15s priority %15s%n", pr, run, action, priority, &used);
            if (fields == 4 && used > 0 && title[used] == 0)
            {
                size_t actor = json_get(&response.json, row, "actor");
                Buffer data = {0};
                buffer_printf(&data, "{\"sender\":{\"id\":%" PRIu64 ",\"login\":", json_number(&response.json, actor, "id"));
                json_quote(&data, json_field(&response.json, actor, "login"));
                buffer_printf(&data, "},\"inputs\":{\"pr\":\"%s\",\"run\":\"%s\",\"action\":\"%s\",\"priority\":\"%s\"}}", pr, run, action, priority);
                Json recovered = json_parse(data.data);
                ci_dispatch_event(state, &recovered, now, id);
                json_free(&recovered);
                free(data.data);
                ci_save_state(state);
            }
            else
            {
                // Non-main/old workflow formats are not commands. Never interpret arbitrary title substrings.
                printf("Ignored manual-control run %" PRIu64 ": unrecognized exact command envelope.\n", id);
                state->dispatch_event = id;
            }
        }
    }
    json_free(&response.json);
}

static Buffer ci_check_summary(const CiPr* pr, const CiIdentity* current, const CiCandidate* candidate, const CiLease* lease, size_t position, uint64_t now)
{
    Buffer summary = {0};
    buffer_printf(&summary, "**%s** — %s.\n\nHead: `%s`\n\nBase: `%s`\n\nMerge: `%s`\n\nQueue position: %s", ci_priority_name(candidate->priority), candidate->reason, current->head, current->base, *current->merge != 0 ? current->merge : "unavailable", candidate->eligible ? "" : "blocked / complete");
    if (candidate->eligible)
    {
        buffer_printf(&summary, "%zu", position);
    }
    buffer_printf(&summary, ". Waiting since Unix time %" PRIu64 ". PR override expires on reset/closure; run override expires with its request ID/SHA.\n\n", pr->queued_at);
    if (lease->pr == pr->number && *lease->nonce != 0)
    {
        buffer_printf(&summary, "State: **%s**. Admitted SHA: `%s`; base `%s`; merge `%s`. [Executor run](https://github.com/%s/actions/runs/%" PRIu64 "). Lease expires at Unix time %" PRIu64 "; expiry revokes admission and waits for terminal cancellation before reuse.\n\n", lease->revoking ? "cancelling / lease retained" : lease->run == 0 ? "dispatch pending" : lease->phase, lease->head, lease->base, lease->merge, BUSTER_CI_REPO, lease->run, lease->expires_at);
    }
    else
    {
        buffer_printf(&summary, "State: %s. Pool lease: %s. Current request run ID: %" PRIu64 ".\n\n", pr->outcome == 1 ? "completed successfully" : pr->outcome == 2 ? "failed" : "not admitted", *lease->nonce != 0 ? "occupied" : "free", pr->request_run);
    }
    if (pr->admitted_at != 0)
    {
        buffer_printf(&summary, "Queue wait: %" PRIu64 " s. ", pr->admitted_at >= pr->queued_at ? pr->admitted_at - pr->queued_at : 0);
    }
    if (pr->completed_at != 0 && pr->admitted_at != 0)
    {
        buffer_printf(&summary, "Admission-to-completion: %" PRIu64 " s. ", pr->completed_at >= pr->admitted_at ? pr->completed_at - pr->admitted_at : 0);
        buffer_printf(&summary, "Hardware job wall time: %" PRIu64 " s (includes any runner-hook wait). ", pr->active_seconds);
    }
    if (pr->completed_at != 0 && pr->approval_at != 0 && pr->completed_at >= pr->approval_at)
    {
        buffer_printf(&summary, "Approval-to-green: %" PRIu64 " s. ", pr->completed_at - pr->approval_at);
    }
    buffer_printf(&summary, "\n\nLast command: %s\n\nPolicy v1. No required success is published for a skipped/cancelled executor, a mismatched revision, a conflict or UNKNOWN. Status observed at Unix time %" PRIu64 ".", *pr->audit != 0 ? pr->audit : "none", now);
    return summary;
}

static void ci_commit_status(CiPr* pr, const char* state)
{
    if (strcmp(pr->status_state, state) != 0)
    {
        Buffer request = {0};
        buffer_printf(&request, "{\"state\":\"%s\",\"context\":\"%s\",\"description\":\"9700X current-revision qualification: %s; see Checks for priority and queue state\",\"target_url\":\"https://github.com/%s/pull/%" PRIu64 "/checks\"}", state, BUSTER_CI_REQUIRED, state, BUSTER_CI_REPO, pr->number);
        char path[100];
        snprintf(path, sizeof(path), "statuses/%s", pr->head);
        ci_mutate("POST", path, &request);
        ci_copy(pr->status_state, sizeof(pr->status_state), state);
        free(request.data);
    }
}

static void ci_refresh_status(CiPr* pr)
{
    char path[120];
    snprintf(path, sizeof(path), "commits/%s/statuses?per_page=100", pr->head);
    Http response = ci_api("GET", path, NULL);
    // The combined /status response omits creator. The individual status list retains the issuer identity.
    size_t statuses = 0;
    if (response.json.rows[statuses].type != '[')
    {
        ci_die("ambiguous required commit status; no publication");
    }
    pr->status_state[0] = 0;
    for (size_t row = statuses + 1; row < response.json.rows[statuses].next; row = response.json.rows[row].next)
    {
        size_t creator = json_get(&response.json, row, "creator");
        if (strcmp(json_field(&response.json, row, "context"), BUSTER_CI_REQUIRED) == 0)
        {
            if (strcmp(json_field(&response.json, creator, "login"), "github-actions[bot]") == 0 && json_number(&response.json, creator, "id") == UINT64_C(41898282))
            {
                ci_copy(pr->status_state, sizeof(pr->status_state), json_field(&response.json, row, "state"));
            }
            break;
        }
    }
    json_free(&response.json);
}

static bool ci_check_needs_restart(const CiPr* pr)
{
    char path[180];
    snprintf(path, sizeof(path), "repos/%s/check-runs/%" PRIu64, BUSTER_CI_REPO, pr->check);
    Http response = ci_http("GET", path, NULL);
    if (response.status != 200 && response.status != 404)
    {
        ci_die("cannot verify the current required check (HTTP %d)", response.status);
    }
    bool restart = response.status == 404;
    if (!restart)
    {
        size_t app = json_get(&response.json, 0, "app");
        if (json_number(&response.json, app, "id") != 15368 || strcmp(json_field(&response.json, 0, "head_sha"), pr->head) != 0 || strcmp(json_field(&response.json, 0, "name"), BUSTER_CI_REQUIRED) != 0)
        {
            ci_die("required check identity changed; refusing to overwrite it");
        }
        restart = strcmp(json_field(&response.json, 0, "status"), "completed") == 0;
    }
    json_free(&response.json);
    return restart;
}

static Buffer ci_receipt(const CiPr* pr, const CiIdentity* current)
{
    Buffer out = {0};
    buffer_printf(&out, "{\"version\":1,\"pr\":%" PRIu64 ",\"head\":\"%s\",\"base\":\"%s\",\"merge\":\"%s\",\"priority\":%d,\"queue_position\":%" PRIu64 ",\"eligible\":%s,\"outcome\":%u,\"request_run\":%" PRIu64 ",\"executor_run\":%" PRIu64 ",\"queued_at\":%" PRIu64 ",\"approval_at\":%" PRIu64 ",\"admitted_at\":%" PRIu64 ",\"completed_at\":%" PRIu64 ",\"hardware_job_seconds\":%" PRIu64 ",\"reason\":", pr->number, current->head, current->base, current->merge, pr->effective_priority, pr->queue_position, pr->eligible ? "true" : "false", pr->outcome, pr->request_run, pr->executor_run, pr->queued_at, pr->approval_at, pr->admitted_at, pr->completed_at, pr->active_seconds);
    json_quote(&out, pr->reason);
    buffer_printf(&out, ",\"merge_method\":\"git-merge-tree/v1\"}");
    return out;
}

static void ci_publish(CiPr* pr, const CiIdentity* current, const CiCandidate* candidate, const CiLease* lease, size_t position, uint64_t now)
{
    bool docs = !current->code && current->mergeable == 1;
    bool qualified = pr->outcome == 1 && current->mergeable == 1;
    bool failed = pr->outcome == 2;
    const char* conclusion = docs || qualified ? "success" : current->mergeable == 0 || failed || !current->open ? "action_required" : "";
    const char* status = docs || qualified ? "success" : *conclusion != 0 ? "failure" : "pending";
    pr->effective_priority = candidate->priority;
    pr->queue_position = candidate->eligible ? position : 0;
    pr->eligible = candidate->eligible;
    ci_copy(pr->reason, sizeof(pr->reason), candidate->reason);
    Buffer summary = ci_check_summary(pr, current, candidate, lease, position, now);
    // Time alone does not create a write storm. Hash only the stable part, before the observation timestamp.
    char* observed = strstr(summary.data, "Status observed at Unix time");
    if (observed != NULL)
    {
        *observed = 0;
        summary.length = strlen(summary.data);
    }
    uint64_t hash = ci_hash(summary.data) ^ ci_hash(conclusion);
    bool new_check = pr->check == 0 || strcmp(pr->check_base, current->base) != 0;
    bool writing = new_check || hash != pr->check_hash || strcmp(pr->status_state, status) != 0;
    if (writing)
    {
        // Revalidate immediately before a mutation; unchanged queue descriptions need neither a write nor another GET.
        CiIdentity latest = ci_get_pr(pr->number, false);
        if (!ci_same_pair(current, &latest) || current->mergeable != latest.mergeable || strcmp(current->merge, latest.merge) != 0)
        {
            ci_die("PR changed before check publication; reconciliation will retry");
        }
        // A controller can die after a successful outbound write but before persisting its local cache. Read the
        // actual status before repairing it; cached "pending" must never conceal an already-published success.
        ci_refresh_status(pr);
        if (!new_check && strcmp(status, "pending") == 0)
        {
            new_check = ci_check_needs_restart(pr);
        }
    }
    if (strcmp(status, "success") != 0)
    {
        ci_commit_status(pr, status);
    }
    if (new_check || hash != pr->check_hash)
    {
        Buffer request = {0};
        buffer_add(&request, "{", 1);
        if (new_check)
        {
            ci_json_string(&request, "name", BUSTER_CI_REQUIRED);
            buffer_add(&request, ",", 1);
            ci_json_string(&request, "head_sha", current->head);
            buffer_printf(&request, ",\"external_id\":\"buster-9700x/qualification/v1/%" PRIu64 "/%s/%s\",", pr->number, current->head, current->base);
        }
        ci_json_string(&request, "status", *conclusion != 0 ? "completed" : lease->pr == pr->number && *lease->nonce != 0 ? "in_progress" : "queued");
        if (*conclusion != 0)
        {
            buffer_add(&request, ",", 1);
            ci_json_string(&request, "conclusion", conclusion);
        }
        buffer_add(&request, ",\"output\":{\"title\":", 19);
        json_quote(&request, docs ? "Documentation only: hardware qualification is not needed" : qualified ? "Qualified current PR head and base on the 9700X" : candidate->reason);
        buffer_add(&request, ",\"summary\":", 11);
        json_quote(&request, summary.data);
        buffer_add(&request, ",\"text\":", 8);
        Buffer receipt = ci_receipt(pr, current);
        json_quote(&request, receipt.data);
        buffer_add(&request, "}}", 2);
        char path[80];
        snprintf(path, sizeof(path), "check-runs");
        if (!new_check)
        {
            snprintf(path, sizeof(path), "check-runs/%" PRIu64, pr->check);
        }
        Http response = ci_api(new_check ? "POST" : "PATCH", path, request.data);
        pr->check = json_number(&response.json, 0, "id");
        if (pr->check == 0)
        {
            ci_die("required-check write returned no ID");
        }
        pr->check_hash = hash;
        ci_copy(pr->check_base, sizeof(pr->check_base), current->base);
        json_free(&response.json);
        free(receipt.data);
        free(request.data);
    }
    if (strcmp(status, "success") == 0)
    {
        ci_commit_status(pr, status);
    }
    free(summary.data);
}

static bool ci_executor_identity(const CiRun* run, const CiLease* lease, uint64_t workflow)
{
    char expected[128];
    snprintf(expected, sizeof(expected), "9700X lease %s", lease->nonce);
    return run->workflow == workflow && run->id == lease->run && run->attempt == lease->attempt && strcmp(run->event, "workflow_dispatch") == 0 && strcmp(run->branch, "main") == 0 && strcmp(run->head, lease->workflow_sha) == 0 && strcmp(run->title, expected) == 0;
}

static void ci_dispatch(CiState* state)
{
    const CiLease* lease = &state->lease;
    Buffer request = {0};
    buffer_printf(&request, "{\"ref\":\"main\",\"inputs\":{\"target_pr\":\"%" PRIu64 "\",\"target_head\":\"%s\",\"target_base\":\"%s\",\"target_merge\":\"%s\",\"lease\":\"%s\"}}", lease->pr, lease->head, lease->base, lease->merge, lease->nonce);
    // State was committed before this request. An ambiguous response is recovered by title/nonce, never redispatched.
    Http response = ci_api("POST", "actions/workflows/9700x.yml/dispatches", request.data);
    uint64_t run = json_number(&response.json, 0, "workflow_run_id");
    if (run != 0)
    {
        state->lease.run = run;
        state->lease.attempt = 1;
        CiPr* pr = ci_find_pr(state, state->lease.pr);
        if (pr != NULL)
        {
            pr->executor_run = run;
        }
        ci_save_state(state);
    }
    json_free(&response.json);
    free(request.data);
}

static void ci_bind_dispatch(CiState* state, uint64_t workflow)
{
    if (*state->lease.nonce != 0 && state->lease.pr != 0 && state->lease.run == 0)
    {
        CiRun runs[BUSTER_CI_CANDIDATES];
        char path[200];
        snprintf(path, sizeof(path), "actions/workflows/%" PRIu64 "/runs?event=workflow_dispatch&per_page=100&created=%%3E%%3D%" PRIu64, workflow, state->lease.admitted_at);
        // Use an ISO timestamp, not a numeric created filter, to keep the query valid on every GitHub API version.
        time_t admitted = (time_t)state->lease.admitted_at;
        struct tm utc;
        gmtime_r(&admitted, &utc);
        char date[32];
        strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
        snprintf(path, sizeof(path), "actions/workflows/%" PRIu64 "/runs?event=workflow_dispatch&per_page=100&created=%%3E%%3D%s", workflow, date);
        Http response = ci_api("GET", path, NULL);
        size_t array = json_get(&response.json, 0, "workflow_runs");
        size_t count = 0;
        if (json_number(&response.json, 0, "total_count") > 100 || array >= response.json.count)
        {
            ci_die("ambiguous dispatch recovery; lease retained");
        }
        for (size_t row = array + 1; row < response.json.rows[array].next; row = response.json.rows[row].next)
        {
            runs[count++] = ci_run_parse(&response.json, row);
        }
        json_free(&response.json);
        char expected[128];
        snprintf(expected, sizeof(expected), "9700X lease %s", state->lease.nonce);
        unsigned matches = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (strcmp(runs[i].title, expected) == 0 && strcmp(runs[i].event, "workflow_dispatch") == 0 && strcmp(runs[i].branch, "main") == 0)
            {
                ++matches;
                state->lease.run = runs[i].id;
                state->lease.attempt = runs[i].attempt;
                CiPr* pr = ci_find_pr(state, state->lease.pr);
                if (pr != NULL)
                {
                    pr->executor_run = runs[i].id;
                }
            }
        }
        if (matches > 1)
        {
            state->lease.revoking = true;
            ci_save_state(state);
            ci_die("duplicate dispatch nonce; operator recovery required, no admission");
        }
        if (matches == 1)
        {
            ci_save_state(state);
        }
        else if (ci_now() - state->lease.admitted_at > BUSTER_CI_DISPATCH_SECONDS)
        {
            // A run without a bound ID cannot pass the guard. A late arrival will have a revoked/nonmatching nonce.
            state->lease.revoking = true;
            ci_save_state(state);
            memset(&state->lease, 0, sizeof(state->lease));
            ci_save_state(state);
        }
    }
}

static bool ci_reap_lease(CiState* state, CiIdentity* identities, size_t count, uint64_t workflow, uint64_t now)
{
    bool occupied = *state->lease.nonce != 0;
    ci_bind_dispatch(state, workflow);
    CiLease* lease = &state->lease;
    if (*lease->nonce != 0 && lease->run != 0)
    {
        CiRun run = ci_get_run(lease->run);
        if (run.workflow != workflow)
        {
            ci_die("lease run changed workflow; lease retained");
        }
        bool terminal = strcmp(run.status, "completed") == 0;
        bool test_success;
        bool guard_success;
        ci_jobs(&run, &test_success, &guard_success);
        ci_copy(lease->phase, sizeof(lease->phase), lease->revoking ? "cancelling" : *run.execution_status != 0 ? run.execution_status : "hosted admission / selector");
        CiIdentity* current = ci_identity(identities, count, lease->pr);
        bool stable = lease->pr == 0 || (current != NULL && ci_lease_current(lease, current));
        bool invalid = lease->pr != 0 && (current == NULL || !current->open || strcmp(current->head, lease->head) != 0 || strcmp(current->base, lease->base) != 0 || current->mergeable == 0 || (current->mergeable == 1 && strcmp(current->merge, lease->merge) != 0));
        bool executor = lease->pr == 0 || ci_executor_identity(&run, lease, workflow);
        if (terminal)
        {
            if (lease->pr != 0 && current != NULL)
            {
                CiPr* pr = ci_find_pr(state, lease->pr);
                CiIdentity final = ci_get_pr(lease->pr, false);
                bool success = stable && executor && !lease->revoking && strcmp(run.conclusion, "success") == 0 && test_success && guard_success && ci_lease_current(lease, &final);
                if (pr != NULL && strcmp(pr->head, lease->head) == 0 && strcmp(pr->base, lease->base) == 0)
                {
                    pr->outcome = success ? 1 : stable && !lease->revoking ? 2 : 0;
                    pr->completed_at = now;
                    pr->active_seconds = run.completed_at >= run.started_at && run.started_at != 0 ? run.completed_at - run.started_at : 0;
                }
            }
            memset(lease, 0, sizeof(*lease));
            ci_save_state(state);
        }
        else if (lease->pr != 0 && (invalid || !executor || now >= lease->expires_at || lease->revoking))
        {
            lease->revoking = true;
            ci_save_state(state);
            // Only this exact controller-owned PR dispatch can be cancelled here. Main/daily/manual work is separate.
            char expected[128];
            snprintf(expected, sizeof(expected), "9700X lease %s", lease->nonce);
            if (strcmp(run.event, "workflow_dispatch") != 0 || strcmp(run.branch, "main") != 0 || strcmp(run.title, expected) != 0)
            {
                ci_die("refusing to cancel an unrelated run; lease retained");
            }
            if (current != NULL)
            {
                CiIdentity final = ci_get_pr(current->number, false);
                // UNKNOWN never becomes a confirmed conflict. A known changed head/base still invalidates the lease.
                bool invalid_pair = strcmp(final.head, lease->head) != 0 || strcmp(final.base, lease->base) != 0 || !final.open;
                bool conflict = final.mergeable == 0;
                if (invalid_pair || conflict || !executor || now >= lease->expires_at || lease->revoking)
                {
                    ci_cancel(run.id, workflow);
                }
            }
            else
            {
                CiIdentity final = ci_get_pr(lease->pr, false);
                if (!final.open)
                {
                    ci_cancel(run.id, workflow);
                }
            }
        }
    }
    occupied = *state->lease.nonce != 0;
    return occupied;
}

static size_t ci_active_runs(CiRun* runs, uint64_t workflow)
{
    size_t count = 0;
    CiRun progress[BUSTER_CI_CANDIDATES];
    const char* statuses[] = {"queued", "in_progress", "pending", "waiting", "requested"};
    for (size_t status = 0; status < sizeof(statuses) / sizeof(statuses[0]); ++status)
    {
        size_t progressing = ci_list_runs(progress, workflow, statuses[status]);
        for (size_t i = 0; i < progressing; ++i)
        {
            bool duplicate = false;
            for (size_t earlier = 0; earlier < count; ++earlier)
            {
                duplicate = duplicate || runs[earlier].id == progress[i].id;
            }
            if (!duplicate)
            {
                if (count == BUSTER_CI_CANDIDATES)
                {
                    ci_die("active run capacity exhausted; no new admission");
                }
                runs[count++] = progress[i];
            }
        }
    }
    return count;
}

static bool ci_legacy_and_conflicts(CiState* state, CiIdentity* identities, size_t identity_count, CiRun* runs, size_t run_count, uint64_t workflow, bool inspect)
{
    bool occupied = false;
    for (size_t i = 0; i < run_count; ++i)
    {
        CiRun* run = &runs[i];
        ci_associate_run(run, state);
        bool executor_success;
        bool guard_success;
        ci_jobs(run, &executor_success, &guard_success);
        (void)executor_success;
        (void)guard_success;
        if (run->pr != 0 && strcmp(run->event, "pull_request") == 0)
        {
            CiIdentity* current = ci_identity(identities, identity_count, run->pr);
            if (current != NULL)
            {
                CiPr* pr = ci_find_pr(state, current->number);
                if (strcmp(run->head, current->head) == 0 && run->id > pr->request_run)
                {
                    pr->request_run = run->id;
                }
            }
            bool obsolete = current == NULL || strcmp(run->head, current->head) != 0;
            bool conflict = current != NULL && current->mergeable == 0;
            if (!inspect && (obsolete || conflict || run->hardware))
            {
                CiIdentity final = ci_get_pr(run->pr, false);
                bool same = current != NULL && ci_same_pair(current, &final);
                bool confirmed = (!final.open || strcmp(run->head, final.head) != 0) || (same && final.mergeable == 0);
                // At cutover, queued legacy PR jobs are replaced by the admission path. Running legacy jobs drain.
                bool queued_legacy = same && run->hardware && strcmp(run->status, "queued") == 0;
                if (confirmed || queued_legacy)
                {
                    CiRun exact = ci_get_run(run->id);
                    if (exact.pr == run->pr && strcmp(exact.event, "pull_request") == 0 && strcmp(exact.head, run->head) == 0)
                    {
                        ci_cancel(run->id, workflow);
                    }
                }
            }
        }
        if (run->hardware && run->id != state->lease.run)
        {
            // Repair/startup: don't dispatch alongside legacy, unrelated maintenance or a bypassed workflow gate.
            // The runner group's exact-default-workflow restriction prevents untrusted PR YAML bypassing admission.
            occupied = true;
        }
    }
    return occupied;
}

static void ci_refresh_requests(CiState* state, uint64_t workflow)
{
    // Only the latest native PR request per head is used for run-specific controls. It may have completed its fast
    // hosted preview already; it remains the request's stable Actions ID until qualification or supersession.
    char path[160];
    snprintf(path, sizeof(path), "actions/workflows/%" PRIu64 "/runs?event=pull_request&per_page=100", workflow);
    Http response = ci_api("GET", path, NULL);
    size_t runs = json_get(&response.json, 0, "workflow_runs");
    if (runs >= response.json.count)
    {
        ci_die("missing PR request list");
    }
    for (size_t row = runs + 1; row < response.json.rows[runs].next; row = response.json.rows[row].next)
    {
        CiRun run = ci_run_parse(&response.json, row);
        ci_associate_run(&run, state);
        CiPr* pr = ci_find_pr(state, run.pr);
        if (pr != NULL && strcmp(pr->head, run.head) == 0 && run.id > pr->request_run)
        {
            if (pr->request_run == 0 && pr->queued_at > run.created_at)
            {
                pr->queued_at = run.created_at;
            }
            pr->request_run = run.id;
        }
    }
    json_free(&response.json);
}

static void ci_prune(CiState* state, CiIdentity* identities, size_t count)
{
    size_t kept = 0;
    for (size_t i = 0; i < state->pr_count; ++i)
    {
        if (ci_identity(identities, count, state->prs[i].number) != NULL)
        {
            state->prs[kept++] = state->prs[i];
        }
    }
    state->pr_count = kept;
    kept = 0;
    for (size_t i = 0; i < state->override_count; ++i)
    {
        CiOverride* override = &state->overrides[i];
        CiPr* pr = ci_find_pr(state, override->pr);
        bool keep = override->pr == 0 || (pr != NULL && strcmp(pr->head, override->head) == 0 && pr->outcome == 0 && (pr->request_run == 0 || pr->request_run == override->run));
        if (keep && override->pr == 0)
        {
            CiRun run = ci_get_run(override->run);
            keep = strcmp(run.head, override->head) == 0 && (strcmp(run.status, "completed") != 0 || override->retry);
        }
        if (keep)
        {
            state->overrides[kept++] = *override;
        }
    }
    state->override_count = kept;
}

static void ci_background_retries(CiState* state)
{
    for (size_t i = 0; i < state->override_count; ++i)
    {
        CiOverride* override = &state->overrides[i];
        if (override->pr == 0 && override->retry)
        {
            CiRun run = ci_get_run(override->run);
            if (strcmp(run.status, "completed") == 0)
            {
                bool executor_success;
                bool guard_success;
                ci_jobs(&run, &executor_success, &guard_success);
                if (!run.gate)
                {
                    ci_die("cannot retry a legacy run without an admission gate; dispatch new main maintenance instead");
                }
                override->retry = false;
                ci_save_state(state);
                char path[100];
                snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/rerun", run.id);
                ci_mutate("POST", path, NULL);
            }
            else
            {
                // Retrying a queued/running request is an idempotent no-op.
                override->retry = false;
            }
        }
    }
}

static void ci_select(CiState* state, CiIdentity* identities, size_t identity_count, CiCandidate* candidates, size_t count, size_t selected, uint64_t workflow, uint64_t now)
{
    if (selected < count && *state->lease.nonce == 0)
    {
        CiCandidate* candidate = &candidates[selected];
        CiLease lease = {0};
        lease.pr = candidate->pr;
        lease.admitted_at = now;
        lease.expires_at = now + BUSTER_CI_LEASE_SECONDS;
        ci_copy(lease.phase, sizeof(lease.phase), "dispatch / admission pending");
        ++state->generation;
        snprintf(lease.nonce, sizeof(lease.nonce), "%" PRIu64 "-%" PRIu64, ci_number(ci_env("GITHUB_RUN_ID")), state->generation);
        ci_main_sha(lease.workflow_sha);
        if (candidate->pr != 0)
        {
            CiIdentity* current = ci_identity(identities, identity_count, candidate->pr);
            CiIdentity final = ci_get_pr(candidate->pr, false);
            if (current == NULL || !ci_same_pair(current, &final) || final.mergeable != 1 || strcmp(current->merge, final.merge) != 0)
            {
                ci_die("PR head/base changed before admission; no dispatch");
            }
            // Recompute approval/hold immediately before admission. A queued promotion never interrupts a lease.
            CiPr* pr = ci_find_pr(state, candidate->pr);
            final.code = ci_pr_code(&final);
            ci_comments(state, pr, &final, now);
            ci_reviews(&final);
            pr->cached_approved = final.approved;
            pr->signal_approval_at = final.approval_at;
            pr->signals_at = now;
            CiOverride* override = ci_find_override(state, pr->request_run, pr->head, false);
            CiCandidate checked = ci_pr_candidate(pr, &final, override);
            if (!checked.eligible || checked.priority != candidate->priority)
            {
                ci_save_state(state);
                ci_die("selected PR lost eligibility before dispatch");
            }
            ci_copy(lease.head, sizeof(lease.head), final.head);
            ci_copy(lease.base, sizeof(lease.base), final.base);
            ci_copy(lease.merge, sizeof(lease.merge), final.merge);
            pr->admitted_at = now;
            pr->completed_at = 0;
            pr->active_seconds = 0;
            pr->executor_run = 0;
            pr->approval_at = final.approval_at;
        }
        else
        {
            CiRun run = ci_get_run(candidate->run);
            if (run.workflow != workflow || run.attempt != candidate->attempt || strcmp(run.status, "completed") == 0 || strcmp(run.branch, "main") != 0 || run.pr != 0)
            {
                ci_die("background request changed before admission");
            }
            lease.run = run.id;
            lease.attempt = run.attempt;
            ci_copy(lease.head, sizeof(lease.head), run.head);
            ci_copy(lease.merge, sizeof(lease.merge), run.head);
            ci_copy(lease.workflow_sha, sizeof(lease.workflow_sha), run.head);
        }
        state->since_fair = ci_fair_after(state->since_fair, candidate, now);
        state->lease = lease;
        ci_save_state(state);
        if (candidate->pr != 0)
        {
            ci_dispatch(state);
        }
        printf("admitted pr=%" PRIu64 " request=%" PRIu64 " priority=%d queued_at=%" PRIu64 " admitted_at=%" PRIu64 " lease=%s\n", candidate->pr, candidate->run, candidate->priority, candidate->queued_at, now, lease.nonce);
    }
}

static Json ci_event(void)
{
    FILE* file = fopen(ci_env("GITHUB_EVENT_PATH"), "rb");
    if (file == NULL)
    {
        ci_die("cannot read GitHub event");
    }
    Buffer text = ci_read(file);
    fclose(file);
    Json result = json_parse(text.data);
    free(text.data);
    return result;
}

static uint64_t ci_event_target(void)
{
    uint64_t number = 0;
    const char* event_name = ci_env("GITHUB_EVENT_NAME");
    if (strcmp(event_name, "pull_request_target") == 0 || strcmp(event_name, "issue_comment") == 0 || strcmp(event_name, "workflow_run") == 0)
    {
        Json event = ci_event();
        if (strcmp(event_name, "pull_request_target") == 0)
        {
            number = json_number(&event, json_get(&event, 0, "pull_request"), "number");
        }
        else if (strcmp(event_name, "issue_comment") == 0)
        {
            number = json_number(&event, json_get(&event, 0, "issue"), "number");
        }
        else
        {
            size_t run = json_get(&event, 0, "workflow_run");
            if (strcmp(json_field(&event, run, "name"), "9700X review signal") == 0)
            {
                const char* title = json_field(&event, run, "display_title");
                const char* prefix = "9700X review PR ";
                if (strncmp(title, prefix, strlen(prefix)) == 0)
                {
                    Buffer command = {0};
                    buffer_printf(&command, "/ci status run %s", title + strlen(prefix));
                    CiCommand parsed = ci_command(command.data);
                    number = parsed.kind == CI_COMMAND_STATUS ? parsed.run : 0;
                    free(command.data);
                }
            }
        }
        json_free(&event);
    }
    return number;
}

static void ci_queue_positions(const CiCandidate* candidates, size_t count, uint64_t now, unsigned fair, unsigned threshold, size_t* ranks)
{
    CiCandidate remaining[BUSTER_CI_CANDIDATES];
    memcpy(remaining, candidates, count * sizeof(*remaining));
    memset(ranks, 0, count * sizeof(*ranks));
    for (size_t rank = 1; rank <= count; ++rank)
    {
        size_t index = ci_choose(remaining, count, now, fair, threshold);
        if (index == SIZE_MAX)
        {
            break;
        }
        ranks[index] = rank;
        remaining[index].eligible = false;
        fair = ci_fair_after(fair, &remaining[index], now);
    }
}

static void ci_reconcile(bool inspect)
{
    ci_read_only = inspect;
    if (!inspect && (strcmp(ci_env("GITHUB_ACTIONS"), "true") != 0 || strcmp(ci_env("GITHUB_REPOSITORY"), BUSTER_CI_REPO) != 0 || strcmp(ci_env("GITHUB_WORKFLOW_REF"), BUSTER_CI_CONTROL) != 0))
    {
        ci_die("writes are restricted to the reviewed default-branch control workflow");
    }
    if (!inspect && strcmp(ci_env("CI_9700X_POOL_CAPACITY"), "1") != 0)
    {
        ci_die("the restricted default-workflow runner pool must be verified with capacity one before activation");
    }
    CiState state = ci_load_state();
    uint64_t now = ci_now();
    uint64_t target_pr = inspect ? 0 : ci_event_target();
    uint64_t workflow = ci_workflow();
    CiIdentity identities[BUSTER_CI_ITEMS] = {0};
    size_t count = ci_list_prs(identities);
    ci_prune(&state, identities, count);
    for (size_t i = 0; i < count; ++i)
    {
        CiIdentity* current = &identities[i];
        CiPr* pr = ci_find_pr(&state, current->number);
        if (pr == NULL)
        {
            if (state.pr_count == BUSTER_CI_ITEMS)
            {
                ci_die("PR state capacity exhausted");
            }
            pr = &state.prs[state.pr_count++];
            memset(pr, 0, sizeof(*pr));
            pr->number = current->number;
            pr->manual = -1;
            pr->effective_priority = 2;
            pr->cached_code = true;
            // Closure expires PR-wide commands. Later comments work even if the initial/reopen webhook was missed.
            pr->command_at = ci_closed_after(current->number);
            pr->command_id = pr->command_at != 0 ? UINT64_MAX : 0;
        }
        ci_observe(pr, current, now);
    }
    ci_refresh_requests(&state, workflow);
    CiRun active[BUSTER_CI_CANDIDATES];
    size_t active_count = ci_active_runs(active, workflow);
    bool occupied = *state.lease.nonce != 0;
    if (!inspect) occupied = ci_reap_lease(&state, identities, count, workflow, now);
    occupied = ci_legacy_and_conflicts(&state, identities, count, active, active_count, workflow, inspect) || occupied;
    for (size_t i = 0; i < count; ++i)
    {
        CiIdentity* current = &identities[i];
        CiPr* pr = ci_find_pr(&state, current->number);
        if (current->mergeable == 1 && strcmp(current->merge, pr->code_merge) != 0)
        {
            pr->cached_code = ci_pr_code(current);
            ci_copy(pr->code_merge, sizeof(pr->code_merge), current->merge);
        }
        current->code = pr->cached_code;
        // A coalesced approval event may not be the event that wakes this run. At every free-pool decision,
        // refresh every candidate before ranking, not only whichever cached candidate would already win.
        bool refresh = inspect || !occupied || pr->signals_at == 0 || now < pr->signals_at || now - pr->signals_at >= BUSTER_CI_SIGNAL_REFRESH_SECONDS || target_pr == pr->number;
        if (refresh)
        {
            ci_comments(&state, pr, current, now);
            ci_reviews(current);
            pr->signals_at = now;
            pr->cached_approved = current->approved;
            pr->signal_approval_at = current->approval_at;
        }
        else
        {
            current->approved = pr->cached_approved;
            current->approval_at = pr->signal_approval_at;
        }
        if (pr->admitted_at == 0)
        {
            pr->approval_at = current->approved ? current->approval_at : 0;
        }
    }
    if (!inspect)
    {
        ci_recover_dispatches(&state, now);
        ci_save_state(&state);
        ci_background_retries(&state);
    }
    CiCandidate candidates[BUSTER_CI_CANDIDATES] = {0};
    size_t candidate_count = 0;
    for (size_t i = 0; i < count; ++i)
    {
        CiPr* pr = ci_find_pr(&state, identities[i].number);
        CiOverride* override = ci_find_override(&state, pr->request_run, pr->head, false);
        candidates[candidate_count++] = ci_pr_candidate(pr, &identities[i], override);
    }
    for (size_t i = 0; i < active_count; ++i)
    {
        CiRun* run = &active[i];
        if (run->pr == 0 && strcmp(run->branch, "main") == 0 && strncmp(run->title, "9700X lease ", 12) != 0 && run->id != state.lease.run && (strcmp(run->event, "push") == 0 || strcmp(run->event, "schedule") == 0 || strcmp(run->event, "workflow_dispatch") == 0))
        {
            if (candidate_count == BUSTER_CI_CANDIDATES)
            {
                ci_die("candidate capacity exhausted");
            }
            CiOverride* override = ci_find_override(&state, run->id, run->head, false);
            CiCandidate* candidate = &candidates[candidate_count++];
            candidate->run = run->id;
            candidate->attempt = run->attempt;
            candidate->background = true;
            candidate->priority = override != NULL && override->manual >= 0 ? override->manual : 3;
            candidate->queued_at = override != NULL && override->queued_at != 0 ? override->queued_at : run->created_at;
            candidate->eligible = run->gate && !run->hardware && (override == NULL || !override->held);
            candidate->reason = candidate->eligible ? "main/daily/manual background work" : !run->gate ? "waiting for hosted admission / legacy workflow" : "held / legacy hardware queue";
        }
    }
    unsigned threshold = BUSTER_CI_BACKLOG;
    const char* configured = ci_env("CI_9700X_BACKLOG");
    if (*configured != 0)
    {
        uint64_t value = ci_number(configured);
        if (value == 0 || value > BUSTER_CI_ITEMS)
        {
            ci_die("invalid backlog threshold");
        }
        threshold = (unsigned)value;
    }
    for (size_t i = 0; i < candidate_count; ++i)
    {
        ci_mark_admitted(&candidates[i], &state.lease);
    }
    size_t selected = ci_choose(candidates, candidate_count, now, state.since_fair, threshold);
    if (inspect)
    {
        printf("policy=v1 pool=%s next_pr=%" PRIu64 " next_run=%" PRIu64 " candidates=%zu\n", occupied ? "leased / legacy hardware occupied" : "free", selected < candidate_count ? candidates[selected].pr : 0, selected < candidate_count ? candidates[selected].run : 0, candidate_count);
        for (size_t i = 0; i < candidate_count; ++i)
        {
            printf("pr=%" PRIu64 " run=%" PRIu64 " priority=%d eligible=%s reason=%s\n", candidates[i].pr, candidates[i].run, candidates[i].priority, candidates[i].eligible ? "true" : "false", candidates[i].reason);
        }
    }
    else
    {
        size_t ranks[BUSTER_CI_CANDIDATES] = {0};
        ci_queue_positions(candidates, candidate_count, now, state.since_fair, threshold, ranks);
        // Invalidate stale successes and expose every blocked/queued revision before admitting any new hardware.
        for (size_t i = 0; i < count; ++i)
        {
            CiPr* pr = ci_find_pr(&state, identities[i].number);
            ci_publish(pr, &identities[i], &candidates[i], &state.lease, ranks[i], now);
        }
        ci_save_state(&state);
        if (!occupied)
        {
            ci_select(&state, identities, count, candidates, candidate_count, selected, workflow, now);
        }
        for (size_t i = 0; i < candidate_count; ++i)
        {
            ci_mark_admitted(&candidates[i], &state.lease);
        }
        ci_queue_positions(candidates, candidate_count, now, state.since_fair, threshold, ranks);
        for (size_t i = 0; i < count; ++i)
        {
            CiPr* pr = ci_find_pr(&state, identities[i].number);
            CiOverride* override = ci_find_override(&state, pr->request_run, pr->head, false);
            CiCandidate fresh = ci_pr_candidate(pr, &identities[i], override);
            ci_mark_admitted(&fresh, &state.lease);
            ci_publish(pr, &identities[i], &fresh, &state.lease, ranks[i], now);
        }
        printf("\nPool: %s; admitted PR: %" PRIu64 "; run: %" PRIu64 "; state: %s; SHA: `%s`.\n\n", *state.lease.nonce != 0 ? "leased" : "free / checking legacy occupancy", state.lease.pr, state.lease.run, state.lease.phase, state.lease.merge);
        puts("| PR | Request run | Priority | Queue position | Eligible | Reason |\n| --- | --- | --- | --- | --- | --- |");
        for (size_t i = 0; i < candidate_count; ++i)
        {
            printf("| %" PRIu64 " | %" PRIu64 " | %s | %zu | %s | %s |\n", candidates[i].pr, candidates[i].run, ci_priority_name(candidates[i].priority), ranks[i], candidates[i].eligible ? "yes" : "no", candidates[i].reason);
        }
        ci_save_state(&state);
    }
    free(state.saved_json);
}

static bool ci_guard_once(bool output)
{
    ci_read_only = true;
    Json event = ci_event();
    size_t inputs = json_get(&event, 0, "inputs");
    const char* target = json_field(&event, inputs, "target_pr");
    uint64_t pr = *target != 0 ? ci_number(target) : 0;
    if (*target != 0 && pr == 0)
    {
        ci_die("an explicit PR target must be a positive PR number");
    }
    uint64_t run_id = ci_number(ci_env("GITHUB_RUN_ID"));
    uint64_t attempt = ci_number(ci_env("GITHUB_RUN_ATTEMPT"));
    CiState state = ci_load_state();
    const CiLease* lease = &state.lease;
    bool admitted = *lease->nonce != 0 && !lease->revoking && lease->pr == pr && lease->run == run_id && lease->attempt == attempt && ci_now() < lease->expires_at;
    if (admitted)
    {
        CiRun run = ci_get_run(run_id);
        uint64_t workflow = ci_workflow();
        if (pr != 0)
        {
            bool inputs_match = strcmp(json_field(&event, inputs, "lease"), lease->nonce) == 0 && strcmp(json_field(&event, inputs, "target_head"), lease->head) == 0 && strcmp(json_field(&event, inputs, "target_base"), lease->base) == 0 && strcmp(json_field(&event, inputs, "target_merge"), lease->merge) == 0;
            CiIdentity current = ci_get_pr_mode(pr, true, lease);
            admitted = inputs_match && ci_executor_identity(&run, lease, workflow) && ci_lease_current(lease, &current);
        }
        else
        {
            admitted = run.workflow == workflow && run.pr == 0 && strcmp(run.branch, "main") == 0 && strcmp(run.head, lease->head) == 0 && strcmp(run.head, lease->workflow_sha) == 0;
        }
        admitted = admitted && strcmp(ci_env("GITHUB_WORKFLOW_SHA"), lease->workflow_sha) == 0;
    }
    if (admitted && output)
    {
        FILE* file = fopen(ci_env("GITHUB_OUTPUT"), "a");
        if (file == NULL)
        {
            ci_die("cannot publish immutable admission outputs");
        }
        fprintf(file, "commit=%s\nhead=%s\nbase=%s\nmerge=%s\ncontroller_sha=%s\nlease=%s\n", lease->merge, lease->head, lease->base, lease->merge, lease->workflow_sha, lease->nonce);
        if (fclose(file) != 0)
        {
            ci_die("cannot flush admission outputs");
        }
        printf("lease=%s pr=%" PRIu64 " run=%" PRIu64 " attempt=%" PRIu64 " head=%s base=%s merge=%s\n", lease->nonce, lease->pr, lease->run, lease->attempt, lease->head, lease->base, lease->merge);
    }
    json_free(&event);
    free(state.saved_json);
    return admitted;
}

static int ci_guard(bool wait)
{
    if (strcmp(ci_env("GITHUB_REPOSITORY"), BUSTER_CI_REPO) != 0 || strcmp(ci_env("GITHUB_REF"), "refs/heads/main") != 0 || strcmp(ci_env("CI_9700X_POOL_CAPACITY"), "1") != 0)
    {
        ci_die("only the reviewed main workflow can execute an admission");
    }
    bool admitted = false;
    Json event = ci_event();
    size_t inputs = json_get(&event, 0, "inputs");
    bool dispatched_pr = *json_field(&event, inputs, "target_pr") != 0;
    json_free(&event);
    unsigned seconds = dispatched_pr ? 5 : 60;
    unsigned attempts = wait ? dispatched_pr ? BUSTER_CI_DISPATCH_SECONDS / seconds : 330 : 1;
    for (unsigned i = 0; i < attempts; ++i)
    {
        admitted = ci_guard_once(wait);
        if (admitted)
        {
            break;
        }
        if (i + 1 < attempts)
        {
            sleep(seconds);
        }
    }
    if (!admitted)
    {
        fputs("No current, unexpired lease for this run/attempt/head/base/merge. No hardware qualification is authorized.\n", stderr);
    }
    return admitted ? 0 : 1;
}

static int ci_host_source_guard(void)
{
    bool allowed = ci_host_source_allowed(ci_env("GITHUB_REPOSITORY"), ci_env("GITHUB_WORKFLOW_REF"), ci_env("GITHUB_REF"));
    if (!allowed)
    {
        fputs("9700X admission: missing job identity or Buster workflow is not the reviewed main executor; refusing before the host lock and checkout.\n", stderr);
    }
    return allowed ? 0 : 1;
}

static int ci_guard_transfer(const char* path, bool checkout)
{
    if (ci_guard(false) != 0) return 1;
    CiState state = ci_load_state();
    if (checkout) ci_git_materialize(&state.lease, path);
    else ci_git_pack(&state.lease, path);
    free(state.saved_json);
    return 0;
}

#include "admission_fixture.c"
#include "admission_test.c"

int main(int argc, char** argv)
{
    int status = 0;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        status = ci_self_test();
    }
    else if (argc == 2 && strcmp(argv[1], "--snapshot") == 0)
    {
        ci_reconcile(true);
    }
    else if (argc == 2 && strcmp(argv[1], "--reconcile") == 0)
    {
        ci_reconcile(false);
    }
    else if (argc == 2 && strcmp(argv[1], "--wait") == 0)
    {
        status = ci_guard(true);
    }
    else if (argc == 2 && strcmp(argv[1], "--verify") == 0)
    {
        status = ci_guard(false);
    }
    else if (argc == 2 && strcmp(argv[1], "--host-source-guard") == 0)
    {
        status = ci_host_source_guard();
    }
    else if (argc == 3 && (strcmp(argv[1], "--merge-pack") == 0 || strcmp(argv[1], "--materialize") == 0))
    {
        status = ci_guard_transfer(argv[2], strcmp(argv[1], "--materialize") == 0);
    }
    else
    {
        fputs("usage: admission --self-test | --snapshot | --reconcile | --wait | --verify | --host-source-guard | --merge-pack FILE | --materialize FILE\n", stderr);
        status = 2;
    }
    return status;
}
