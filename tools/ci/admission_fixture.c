// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by admission.c: deterministic GitHub API transport for --self-test only. The production CLI has no
// environment/argument switch for this transport. Fixtures exercise the real controller/guard and JSON requests.

typedef struct CiFixture CiFixture;
struct CiFixture
{
    CiState state;
    bool exists;
    CiIdentity current;
    char workflow_sha[BUSTER_CI_SHA];
    CiRun run;
    CiRun native;
    CiRun background;
    bool native_enabled;
    bool background_enabled;
    bool background_ready;
    bool legacy;
    bool legacy_running;
    bool dispatch_returns_id;
    bool cancel_terminal;
    const char* comments;
    const char* reviews;
    const char* commands;
    const char* events;
    const char* executor_conclusion;
    const char* guard_conclusion;
    const char* run_path;
    const char* snapshot_base;
    char* worker_event;
    unsigned dispatches;
    unsigned cancellations;
    unsigned reruns;
    unsigned publications;
    uint64_t cancelled[32];
    uint64_t next_check;
    uint64_t next_run;
    char required_head[BUSTER_CI_SHA];
    char required_status[32];
    char required_conclusion[32];
    char commit_status[16];
    char status_head[BUSTER_CI_SHA];
    unsigned status_writes;
    unsigned http_calls;
    unsigned seconds_per_call;
    unsigned state_writes;
    unsigned signal_reads;
};

static CiFixture ci_fixture;

static void ci_fixture_date(Buffer* out, uint64_t value)
{
    time_t seconds = (time_t)value;
    struct tm utc;
    gmtime_r(&seconds, &utc);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    json_quote(out, text);
}

static Http ci_fixture_response(int status, const char* body)
{
    Http response = {0};
    response.status = status;
    response.json = json_parse(body);
    return response;
}

static void ci_fixture_run_json(Buffer* out, const CiRun* run)
{
    buffer_printf(out, "{\"id\":%" PRIu64 ",\"workflow_id\":%" PRIu64 ",\"run_attempt\":%" PRIu64 ",\"path\":", run->id, run->workflow, run->attempt);
    if (ci_fixture.run_path != NULL)
    {
        json_quote(out, ci_fixture.run_path);
    }
    else
    {
        Buffer qualified = {0};
        buffer_printf(&qualified, "%s@%s", BUSTER_CI_WORKFLOW, run->branch);
        json_quote(out, qualified.data);
        free(qualified.data);
    }
    buffer_printf(out, ",\"head_repository\":{\"id\":%u},\"created_at\":", ci_fixture.current.fork && strcmp(run->event, "pull_request") == 0 ? 123 : 1421805693);
    ci_fixture_date(out, run->created_at);
    buffer_add(out, ",", 1);
    ci_json_string(out, "head_sha", run->head);
    buffer_add(out, ",", 1);
    ci_json_string(out, "head_branch", run->branch);
    buffer_add(out, ",", 1);
    ci_json_string(out, "event", run->event);
    buffer_add(out, ",", 1);
    ci_json_string(out, "status", run->status);
    buffer_add(out, ",", 1);
    ci_json_string(out, "conclusion", run->conclusion);
    buffer_add(out, ",", 1);
    ci_json_string(out, "display_title", run->title);
    buffer_add(out, ",\"pull_requests\":[", 18);
    if (run->pr != 0)
    {
        buffer_printf(out, "{\"number\":%" PRIu64 "}", run->pr);
    }
    buffer_add(out, "]}", 2);
}

static CiRun* ci_fixture_run(uint64_t id)
{
    CiRun* result = NULL;
    if (ci_fixture.run.id == id)
    {
        result = &ci_fixture.run;
    }
    else if (ci_fixture.native.id == id)
    {
        result = &ci_fixture.native;
    }
    else if (ci_fixture.background.id == id)
    {
        result = &ci_fixture.background;
    }
    return result;
}

static void ci_fixture_pr_json(Buffer* out)
{
    const CiIdentity* current = &ci_fixture.current;
    const char* snapshot = ci_fixture.snapshot_base != NULL ? ci_fixture.snapshot_base : current->base;
    buffer_printf(out, "{\"number\":546,\"state\":\"%s\",\"headRefOid\":\"%s\",\"baseRefOid\":\"%s\",\"headRef\":{\"target\":{\"oid\":\"%s\"}},\"baseRef\":{\"target\":{\"oid\":\"%s\"}},\"headRepository\":{\"databaseId\":%u},\"potentialMergeCommit\":{\"oid\":\"%s\"},\"mergeable\":\"%s\"}", current->open ? "OPEN" : "CLOSED", current->head, snapshot, current->head, current->base, current->fork ? 123 : 1421805693, current->merge, current->mergeable == 1 ? "MERGEABLE" : current->mergeable == 0 ? "CONFLICTING" : "UNKNOWN");
}

static Http ci_fixture_request(const char* method, const char* full_path, const char* body)
{
    ++ci_fixture.http_calls;
    ci_fixture_clock += ci_fixture.seconds_per_call;
    const char* prefix = "repos/" BUSTER_CI_REPO "/";
    bool graphql = strcmp(full_path, "graphql") == 0;
    if (!graphql && strncmp(full_path, prefix, strlen(prefix)) != 0)
    {
        ci_die("fixture received an unexpected repository path: %s", full_path);
    }
    const char* path = graphql ? "graphql" : full_path + strlen(prefix);
    Buffer out = {0};
    int status = 200;
    if (graphql && strcmp(method, "POST") == 0)
    {
        Json request = json_parse(body);
        bool single = strstr(json_field(&request, 0, "query"), "pullRequest(number:") != NULL;
        json_free(&request);
        if (single)
        {
            buffer_printf(&out, "{\"data\":{\"repository\":{\"pullRequest\":");
            ci_fixture_pr_json(&out);
            buffer_printf(&out, "}}}");
        }
        else
        {
            buffer_printf(&out, "{\"data\":{\"repository\":{\"pullRequests\":{\"pageInfo\":{\"hasNextPage\":false},\"nodes\":[");
            if (ci_fixture.current.open) ci_fixture_pr_json(&out);
            buffer_printf(&out, "]}}}}");
        }
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "commits/", 8) == 0 && strstr(path, "/statuses?") != NULL)
    {
        bool present = *ci_fixture.commit_status != 0 && strncmp(path + 8, ci_fixture.status_head, 40) == 0;
        buffer_printf(&out, "[");
        if (present)
        {
            buffer_printf(&out, "{\"context\":\"%s\",\"state\":\"%s\",\"creator\":{\"login\":\"github-actions[bot]\",\"id\":41898282}}", BUSTER_CI_REQUIRED, ci_fixture.commit_status);
        }
        buffer_printf(&out, "]");
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "commits/", 8) == 0)
    {
        buffer_printf(&out, "{\"total_count\":%u,\"check_runs\":[", ci_fixture.exists ? 1 : 0);
        if (ci_fixture.exists)
        {
            Buffer ledger = ci_state_json(&ci_fixture.state);
            buffer_printf(&out, "{\"id\":9000,\"external_id\":\"%s\",\"app\":{\"id\":15368,\"slug\":\"github-actions\"},\"output\":{\"text\":", BUSTER_CI_STATE_ID);
            json_quote(&out, ledger.data);
            buffer_add(&out, "}}", 2);
            free(ledger.data);
        }
        buffer_add(&out, "]}", 2);
    }
    else if (strcmp(method, "GET") == 0 && strcmp(path, "actions/workflows/9700x.yml") == 0)
    {
        buffer_printf(&out, "{\"id\":42,\"path\":\"%s\",\"state\":\"active\"}", BUSTER_CI_WORKFLOW);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "check-runs/", 11) == 0)
    {
        buffer_printf(&out, "{\"name\":\"%s\",\"head_sha\":\"%s\",\"app\":{\"id\":15368},\"status\":\"%s\"}", BUSTER_CI_REQUIRED, ci_fixture.required_head, ci_fixture.required_status);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "actions/workflows/9700x-control.yml/runs?", strlen("actions/workflows/9700x-control.yml/runs?")) == 0)
    {
        buffer_printf(&out, "{\"workflow_runs\":%s}", ci_fixture.commands);
    }
    else if (strcmp(method, "GET") == 0 && strcmp(path, "git/ref/heads/main") == 0)
    {
        buffer_printf(&out, "{\"object\":{\"sha\":\"%s\"}}", ci_fixture.workflow_sha);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "pulls?", 6) == 0)
    {
        buffer_printf(&out, "[%s", ci_fixture.current.open ? "{\"number\":546}" : "");
        buffer_add(&out, "]", 1);
    }
    else if (strcmp(method, "GET") == 0 && strcmp(path, "pulls/546") == 0)
    {
        const CiIdentity* current = &ci_fixture.current;
        buffer_printf(&out, "{\"number\":546,\"state\":\"%s\",\"head\":{\"sha\":\"%s\",\"repo\":{\"id\":%u}},\"base\":{\"sha\":\"%s\",\"ref\":\"main\"},\"merge_commit_sha\":\"%s\",\"mergeable\":%s}", current->open ? "open" : "closed", current->head, current->fork ? 123 : 1421805693, current->base, current->merge, current->mergeable == 1 ? "true" : current->mergeable == 0 ? "false" : "null");
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "compare/", 8) == 0)
    {
        buffer_printf(&out, "{\"files\":[{\"filename\":\"%s\"}]}", ci_fixture.current.code ? "src/buster/compiler/c_check.c" : "docs/design.md");
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "pulls/546/reviews?", 18) == 0)
    {
        ++ci_fixture.signal_reads;
        buffer_printf(&out, "%s", ci_fixture.reviews);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "issues/546/comments?", 20) == 0)
    {
        ++ci_fixture.signal_reads;
        buffer_printf(&out, "%s", ci_fixture.comments);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "issues/546/events?", strlen("issues/546/events?")) == 0)
    {
        buffer_printf(&out, "%s", ci_fixture.events);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "collaborators/", 14) == 0)
    {
        bool trusted = strcmp(path, "collaborators/maintainer/permission") == 0;
        buffer_printf(&out, "{\"permission\":\"%s\",\"user\":{\"id\":%u}}", trusted ? "write" : "read", trusted ? 123 : 666);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "actions/workflows/42/runs?", 26) == 0)
    {
        const char* filter = strstr(path, "status=");
        const char* event = strstr(path, "event=");
        const CiRun* runs[] = {&ci_fixture.run, &ci_fixture.native, &ci_fixture.background};
        unsigned count = 0;
        Buffer rows = {0};
        for (size_t i = 0; i < 3; ++i)
        {
            const CiRun* run = runs[i];
            bool enabled = run->id != 0 && (i != 1 || ci_fixture.native_enabled) && (i != 2 || ci_fixture.background_enabled);
            bool match = filter == NULL || strncmp(filter + 7, run->status, strlen(run->status)) == 0;
            match = match && (event == NULL || strncmp(event + 6, run->event, strlen(run->event)) == 0);
            if (enabled && match)
            {
                if (count++ != 0)
                {
                    buffer_add(&rows, ",", 1);
                }
                ci_fixture_run_json(&rows, run);
            }
        }
        buffer_printf(&out, "{\"total_count\":%u,\"workflow_runs\":[%s]}", count, rows.data != NULL ? rows.data : "");
        free(rows.data);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "actions/runs/", 13) == 0)
    {
        uint64_t id = 0;
        int used = 0;
        if (sscanf(path, "actions/runs/%" SCNu64 "%n", &id, &used) != 1)
        {
            ci_die("invalid fixture run request");
        }
        CiRun* run = ci_fixture_run(id);
        if (run == NULL)
        {
            status = 404;
            buffer_printf(&out, "{\"message\":\"Not Found\"}");
        }
        else if (path[used] == 0)
        {
            ci_fixture_run_json(&out, run);
        }
        else
        {
            const char* execution = ci_fixture.executor_conclusion;
            const char* guard = ci_fixture.guard_conclusion;
            if (run == &ci_fixture.run)
            {
                buffer_printf(&out, "{\"total_count\":2,\"jobs\":[{\"name\":\"admission\",\"status\":\"completed\",\"conclusion\":\"%s\"},{\"name\":\"%s\",\"status\":\"%s\",\"conclusion\":\"%s\",\"runner_name\":\"9700X\"}]}", guard, BUSTER_CI_EXECUTOR, strcmp(run->status, "completed") == 0 ? "completed" : "in_progress", strcmp(run->status, "completed") == 0 ? execution : "");
            }
            else if (run == &ci_fixture.native && ci_fixture.legacy)
            {
                buffer_printf(&out, "{\"total_count\":1,\"jobs\":[{\"name\":\"%s\",\"status\":\"%s\",\"conclusion\":null}]}", BUSTER_CI_REQUIRED, ci_fixture.legacy_running ? "in_progress" : "queued");
            }
            else if (run == &ci_fixture.background)
            {
                buffer_printf(&out, "%s", ci_fixture.background_ready ? "{\"total_count\":1,\"jobs\":[{\"name\":\"admission\",\"status\":\"in_progress\",\"conclusion\":null}]}" : "{\"total_count\":0,\"jobs\":[]}");
            }
            else
            {
                buffer_printf(&out, "{\"total_count\":1,\"jobs\":[{\"name\":\"9700X queue request\",\"status\":\"completed\",\"conclusion\":\"skipped\"}]}");
            }
        }
    }
    else if ((strcmp(method, "POST") == 0 || strcmp(method, "PATCH") == 0) && strncmp(path, "check-runs", 10) == 0)
    {
        Json request = json_parse(body);
        size_t output = json_get(&request, 0, "output");
        bool ledger = strcmp(json_field(&request, 0, "external_id"), BUSTER_CI_STATE_ID) == 0 || strcmp(path, "check-runs/9000") == 0;
        uint64_t id = ledger ? 9000 : strcmp(method, "POST") == 0 ? ++ci_fixture.next_check : ci_number(path + strlen("check-runs/"));
        if (ledger)
        {
            ++ci_fixture.state_writes;
            ci_fixture.state = ci_state_parse(json_field(&request, output, "text"));
            ci_fixture.state.check = 9000;
            ci_fixture.exists = true;
        }
        else
        {
            ++ci_fixture.publications;
            const char* head = json_field(&request, 0, "head_sha");
            if (*head != 0)
            {
                ci_copy(ci_fixture.required_head, sizeof(ci_fixture.required_head), head);
            }
            ci_copy(ci_fixture.required_status, sizeof(ci_fixture.required_status), json_field(&request, 0, "status"));
            ci_copy(ci_fixture.required_conclusion, sizeof(ci_fixture.required_conclusion), json_field(&request, 0, "conclusion"));
        }
        buffer_printf(&out, "{\"id\":%" PRIu64 "}", id);
        json_free(&request);
    }
    else if (strcmp(method, "POST") == 0 && strncmp(path, "statuses/", 9) == 0)
    {
        Json request = json_parse(body);
        if (strcmp(json_field(&request, 0, "context"), BUSTER_CI_REQUIRED) != 0)
        {
            ci_die("fixture received the wrong required commit status context");
        }
        ci_copy(ci_fixture.commit_status, sizeof(ci_fixture.commit_status), json_field(&request, 0, "state"));
        ci_copy(ci_fixture.status_head, sizeof(ci_fixture.status_head), path + 9);
        ++ci_fixture.status_writes;
        json_free(&request);
        status = 201;
        buffer_printf(&out, "{}");
    }
    else if (strcmp(method, "POST") == 0 && strcmp(path, "actions/workflows/9700x.yml/dispatches") == 0)
    {
        if (!ci_fixture.exists || *ci_fixture.state.lease.nonce == 0 || ci_fixture.state.lease.run != 0)
        {
            ci_die("dispatch occurred before durable lease publication");
        }
        ++ci_fixture.dispatches;
        free(ci_fixture.worker_event);
        ci_fixture.worker_event = strdup(body);
        CiRun* run = &ci_fixture.run;
        memset(run, 0, sizeof(*run));
        run->id = ++ci_fixture.next_run;
        run->attempt = 1;
        run->workflow = 42;
        run->created_at = ci_now();
        ci_copy(run->head, sizeof(run->head), ci_fixture.workflow_sha);
        ci_copy(run->branch, sizeof(run->branch), "main");
        ci_copy(run->event, sizeof(run->event), "workflow_dispatch");
        ci_copy(run->status, sizeof(run->status), "in_progress");
        snprintf(run->title, sizeof(run->title), "9700X lease %s", ci_fixture.state.lease.nonce);
        if (ci_fixture.dispatch_returns_id)
        {
            buffer_printf(&out, "{\"workflow_run_id\":%" PRIu64 "}", run->id);
        }
        else
        {
            buffer_printf(&out, "{}");
            status = 204;
        }
    }
    else if (strcmp(method, "POST") == 0 && strncmp(path, "actions/runs/", 13) == 0)
    {
        uint64_t id = 0;
        int used = 0;
        sscanf(path, "actions/runs/%" SCNu64 "%n", &id, &used);
        CiRun* run = ci_fixture_run(id);
        if (run == NULL)
        {
            ci_die("fixture mutation targeted unknown run");
        }
        if (strcmp(path + used, "/cancel") == 0)
        {
            if (ci_fixture.cancellations == 32)
            {
                ci_die("fixture cancellation overflow");
            }
            ci_fixture.cancelled[ci_fixture.cancellations++] = id;
            if (ci_fixture.cancel_terminal)
            {
                ci_copy(run->status, sizeof(run->status), "completed");
                ci_copy(run->conclusion, sizeof(run->conclusion), "cancelled");
            }
            status = 202;
        }
        else if (strcmp(path + used, "/rerun") == 0)
        {
            ++ci_fixture.reruns;
            ++run->attempt;
            ci_copy(run->status, sizeof(run->status), "in_progress");
            status = 201;
        }
        else
        {
            ci_die("unexpected fixture run mutation");
        }
        buffer_printf(&out, "{}");
    }
    else
    {
        ci_die("unexpected fixture HTTP request: %s %s", method, path);
    }
    Http response = ci_fixture_response(status, out.data);
    free(out.data);
    return response;
}

static void ci_fixture_init(void)
{
    free(ci_fixture.worker_event);
    memset(&ci_fixture, 0, sizeof(ci_fixture));
    ci_workflow_id = 0;
    ci_fixture_http = true;
    ci_fixture_clock = UINT64_C(1793095583);
    ci_fixture.current.number = 546;
    ci_fixture.current.open = true;
    ci_fixture.current.code = true;
    ci_fixture.current.mergeable = 1;
    memset(ci_fixture.current.head, 'a', 40);
    memset(ci_fixture.current.base, 'b', 40);
    memset(ci_fixture.current.merge, 'c', 40);
    memset(ci_fixture.workflow_sha, 'b', 40);
    ci_fixture.comments = "[]";
    ci_fixture.reviews = "[]";
    ci_fixture.commands = "[]";
    ci_fixture.events = "[]";
    ci_fixture.executor_conclusion = "success";
    ci_fixture.guard_conclusion = "success";
    ci_fixture.dispatch_returns_id = true;
    ci_fixture.next_check = 10000;
    ci_fixture.next_run = 5000;
    ci_fixture.native.id = 4000;
    ci_fixture.native.pr = 546;
    ci_fixture.native.attempt = 1;
    ci_fixture.native.workflow = 42;
    ci_fixture.native.created_at = ci_now() - 100;
    ci_copy(ci_fixture.native.head, sizeof(ci_fixture.native.head), ci_fixture.current.head);
    ci_copy(ci_fixture.native.branch, sizeof(ci_fixture.native.branch), "feature");
    ci_copy(ci_fixture.native.event, sizeof(ci_fixture.native.event), "pull_request");
    ci_copy(ci_fixture.native.status, sizeof(ci_fixture.native.status), "completed");
    ci_fixture.native_enabled = true;
    ci_fixture.background.id = 6000;
    ci_fixture.background_ready = true;
    ci_fixture.background.workflow = 42;
    ci_fixture.background.attempt = 1;
    ci_fixture.background.created_at = ci_now() - 50;
    ci_copy(ci_fixture.background.head, sizeof(ci_fixture.background.head), ci_fixture.workflow_sha);
    ci_copy(ci_fixture.background.branch, sizeof(ci_fixture.background.branch), "main");
    ci_copy(ci_fixture.background.event, sizeof(ci_fixture.background.event), "schedule");
    ci_copy(ci_fixture.background.status, sizeof(ci_fixture.background.status), "in_progress");
    ci_permission_count = 0;
    ci_read_only = false;
    ci_fixture_http = true;
    setenv("GITHUB_ACTIONS", "true", 1);
    setenv("GITHUB_EVENT_NAME", "schedule", 1);
    setenv("GITHUB_REPOSITORY", BUSTER_CI_REPO, 1);
    setenv("GITHUB_WORKFLOW_REF", BUSTER_CI_CONTROL, 1);
    setenv("GITHUB_RUN_ID", "1234", 1);
    setenv("GITHUB_RUN_ATTEMPT", "1", 1);
    setenv("GITHUB_REF", "refs/heads/main", 1);
    setenv("GITHUB_WORKFLOW_SHA", ci_fixture.workflow_sha, 1);
    setenv("CI_9700X_POOL_CAPACITY", "1", 1);
    unsetenv("CI_9700X_BACKLOG");
}
