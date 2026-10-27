// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by admission.c. Fixtures are original and deterministic; no API or runner is used by --self-test.

static unsigned ci_test_count;
static unsigned ci_test_failures;

static void ci_expect(bool condition, const char* name)
{
    ++ci_test_count;
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        ++ci_test_failures;
    }
}

static CiIdentity ci_test_identity(uint64_t number)
{
    CiIdentity current = {0};
    current.number = number;
    current.open = true;
    current.code = true;
    current.mergeable = 1;
    memset(current.head, 'a', 40);
    memset(current.base, 'b', 40);
    memset(current.merge, 'c', 40);
    return current;
}

static void ci_test_json(void)
{
    Json json = json_parse("{\"items\":[{\"body\":\"LGTM\\n\\u00e9\\ud83d\\ude00\",\"id\":6438358225}],\"mergeable\":null,\"boolean\":true}");
    size_t array = json_get(&json, 0, "items");
    ci_expect(strcmp(json_field(&json, array + 1, "body"), "LGTM\né😀") == 0, "JSON decodes approval bodies/Unicode without executing text");
    ci_expect(json_number(&json, array + 1, "id") == UINT64_C(6438358225), "64-bit GitHub IDs");
    ci_expect(strcmp(json_field(&json, 0, "mergeable"), "") == 0, "null is distinct from false");
    ci_expect(strcmp(json_field(&json, 0, "boolean"), "true") == 0, "JSON boolean");
    json_free(&json);
    Json nul = json_parse("{\"body\":\"LGTM\\u0000not approved\"}");
    ci_expect(ci_command(json_field(&nul, 0, "body")).kind == CI_COMMAND_NONE, "JSON NUL cannot hide trailing approval/command text or block other PRs");
    json_free(&nul);
    const char* invalid[] = {"{", "[1,]", "{\"a\":}", "[true false]", "{\"a\" 1}", "\"\\ud800\"", "\"\\q\"", "{}{}"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        pid_t child = fork();
        if (child == 0)
        {
            FILE* silent = freopen("/dev/null", "w", stderr);
            (void)silent;
            Json rejected = json_parse(invalid[i]);
            json_free(&rejected);
            _exit(0);
        }
        int status = 0;
        waitpid(child, &status, 0);
        ci_expect(WIFEXITED(status) && WEXITSTATUS(status) == 2, "malformed JSON fails closed");
    }
}

static void ci_test_commands(void)
{
    const char* approvals[] = {"LGTM", "APPROVED", "SHIP IT", "READY TO MERGE", "/ci approve", "\nLGTM\n", "   APPROVED  "};
    for (size_t i = 0; i < sizeof(approvals) / sizeof(approvals[0]); ++i)
    {
        ci_expect(ci_command(approvals[i]).kind == CI_COMMAND_APPROVE, "explicit standalone approval");
    }
    const char* rejected[] = {"not LGTM", "LGTM?", "LGTM-ish", "> LGTM", "`LGTM`", "```\nLGTM\n```", "~~~\nAPPROVED\n~~~", "    LGTM", "\tLGTM", "I think APPROVED is a keyword", "LGTM\nnot approved", "READY TO MERGE later", "/ci approve deadbeef", "/ci priority urgent", "/ci priority high run 0", "/ci priority high run -2", "/ci priority high run 18446744073709551616", "/ci hold run 1 trailing", "/ci preempt", "/ci retry; echo bad", "$(/ci priority critical)", "LGTM\rAPPROVED"};
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
    {
        ci_expect(ci_command(rejected[i]).kind == CI_COMMAND_NONE, "quotes/substrings/negations/code/invalid commands cannot approve");
    }
    CiCommand high = ci_command("/ci priority high run 40481067492");
    ci_expect(high.kind == CI_COMMAND_PRIORITY && high.priority == 1 && high.run == UINT64_C(40481067492), "run-specific high override grammar");
    ci_expect(ci_command("/ci priority critical").priority == 0, "critical override");
    ci_expect(ci_command("/ci priority normal").priority == -1, "normal clears the manual override");
    ci_expect(ci_command("/ci priority low").priority == 4, "explicit low");
    ci_expect(ci_command("/ci hold").kind == CI_COMMAND_HOLD, "hold grammar");
    ci_expect(ci_command("/ci resume run 42").kind == CI_COMMAND_RESUME, "run resume grammar");
    ci_expect(ci_command("/ci retry").kind == CI_COMMAND_RETRY, "retry grammar");
    ci_expect(ci_command("/ci status").kind == CI_COMMAND_STATUS, "status grammar");
    ci_expect(ci_trusted("admin") && ci_trusted("maintain") && ci_trusted("write"), "maintainer permissions");
    ci_expect(!ci_trusted("read") && !ci_trusted("triage") && !ci_trusted("none") && !ci_trusted("owner"), "untrusted readers/forgeable roles cannot elevate");
}

static void ci_test_policy(void)
{
    CiState state = {0};
    state.pr_count = 1;
    CiPr* pr = &state.prs[0];
    pr->number = 546;
    pr->manual = -1;
    CiIdentity current = ci_test_identity(546);
    ci_observe(pr, &current, 100);
    CiCandidate candidate = ci_pr_candidate(pr, &current, NULL);
    ci_expect(candidate.eligible && candidate.priority == 2, "ordinary conflict-free PR is P2 (including behind main)");
    current.approved = true;
    candidate = ci_pr_candidate(pr, &current, NULL);
    ci_expect(candidate.priority == 1, "current trusted approval gets P1");
    ci_apply(&state, pr, ci_command("/ci priority critical"), 101, "maintainer", 1);
    candidate = ci_pr_candidate(pr, &current, NULL);
    ci_expect(candidate.priority == 0, "P0 manual override wins automatic approval");
    ci_apply(&state, pr, ci_command("/ci priority normal"), 102, "maintainer", 2);
    candidate = ci_pr_candidate(pr, &current, NULL);
    ci_expect(candidate.priority == 1 && pr->manual == -1, "normal reset restores approval priority");
    ci_apply(&state, pr, ci_command("/ci hold"), 103, "maintainer", 3);
    ci_expect(!ci_pr_candidate(pr, &current, NULL).eligible, "hold prevents admission");
    ci_apply(&state, pr, ci_command("/ci resume"), 104, "maintainer", 4);
    ci_expect(ci_pr_candidate(pr, &current, NULL).eligible, "resume restores eligibility without a push");
    current.mergeable = 0;
    ci_expect(!ci_pr_candidate(pr, &current, NULL).eligible, "confirmed conflict never admitted");
    current.mergeable = -1;
    ci_expect(!ci_pr_candidate(pr, &current, NULL).eligible, "UNKNOWN never admitted or classified as confirmed conflict");
    current.mergeable = 1;
    current.fork = true;
    current.approved = false;
    pr->manual = 0;
    ci_expect(!ci_pr_candidate(pr, &current, NULL).eligible, "persistent override cannot authorize an unreviewed fork revision");
    current.approved = true;
    ci_expect(ci_pr_candidate(pr, &current, NULL).eligible, "trusted current-revision fork approval permits read-only execution");
    current.open = false;
    ci_expect(!ci_pr_candidate(pr, &current, NULL).eligible, "close revokes eligibility");
    current.open = true;
    current.code = false;
    ci_expect(!ci_pr_candidate(pr, &current, NULL).eligible, "docs-only PR never needs the hardware");
    current.code = true;
    ci_expect(!ci_comment_current(pr, 99) && !ci_comment_current(pr, 100) && ci_comment_current(pr, 101), "comments bind only after a head was observed, with conservative same-second handling");
    ci_expect(ci_command_new(pr, 105, 50), "new command version");
    pr->command_at = 105;
    pr->command_id = 50;
    ci_expect(!ci_command_new(pr, 105, 50) && !ci_command_new(pr, 104, 999), "replayed/out-of-order commands are idempotent");
    ci_expect(ci_command_new(pr, 106, 1), "editing an older comment creates a newer command version");
    pr->outcome = 1;
    pr->held = true;
    memset(current.head, 'd', 40);
    ci_observe(pr, &current, 110);
    ci_expect(pr->seen_at == 110 && pr->outcome == 0 && pr->manual == 0 && pr->held, "head update resets derived result/approval binding but preserves PR priority/hold");
    ci_expect(!ci_comment_current(pr, 109), "approval from the superseded SHA is stale");
    pr->outcome = 1;
    memset(current.base, 'e', 40);
    ci_observe(pr, &current, 120);
    ci_expect(pr->outcome == 0 && pr->seen_at == 110, "base advance requalifies without a rebase or invalidating current-head approvals");
    pr->held = false;
    pr->outcome = 2;
    ci_apply(&state, pr, ci_command("/ci retry"), 130, "maintainer", 5);
    ci_expect(pr->outcome == 0 && pr->queued_at == 130, "retry queues exactly the current revision");
    CiOverride override = {0};
    override.manual = 4;
    ci_expect(ci_pr_candidate(pr, &current, &override).priority == 4, "run override takes precedence over PR override");
    override.held = true;
    ci_expect(!ci_pr_candidate(pr, &current, &override).eligible, "run hold");
    ci_expect(ci_document("docs/design.md") && ci_document("README.md") && !ci_document("docs/performance/schema.json") && !ci_document("src/a.c"), "docs-only selector preserves the performance qualification exception");
}

static void ci_test_scheduler(void)
{
    CiCandidate queue[5] = {0};
    for (size_t i = 0; i < 4; ++i)
    {
        queue[i].pr = 546 + i;
        queue[i].queued_at = 100 + i;
        queue[i].priority = 2;
        queue[i].eligible = true;
    }
    ci_expect(ci_choose(queue, 4, 200, 0, 3) == 0, "equal-priority FIFO");
    queue[3].priority = 1;
    ci_expect(ci_choose(queue, 4, 200, 0, 3) == 3, "new approval is next after a non-preemptible current run");
    queue[2].priority = 0;
    ci_expect(ci_choose(queue, 4, 200, 0, 3) == 2, "P0 beats high and normal");
    queue[2].eligible = false;
    ci_expect(ci_choose(queue, 4, 200, 0, 3) == 3, "conflict/hold excludes even P0");
    queue[4].background = true;
    queue[4].eligible = true;
    queue[4].queued_at = 50;
    queue[4].priority = 3;
    ci_expect(ci_choose(queue, 5, 200, 0, 3) == 3, "main deferred with three eligible waiting PRs");
    queue[3].priority = 2;
    queue[1].eligible = false;
    ci_expect(ci_choose(queue, 5, 200, 0, 3) == 4, "main promptly competes by FIFO below the backlog threshold");
    queue[1].eligible = true;
    queue[0].priority = 0;
    ci_expect(ci_choose(queue, 5, 50 + BUSTER_CI_MAIN_BOUND, 0, 3) == 4, "main/daily deadline bounds starvation even under critical traffic");
    queue[4].eligible = false;
    queue[0].priority = 2;
    queue[3].priority = 0;
    ci_expect(ci_choose(queue, 5, 100 + BUSTER_CI_AGING_SECONDS, BUSTER_CI_FAIR_EVERY, 3) == 0, "periodic aged FIFO service bounds ordinary/low PR starvation");
    queue[0].eligible = false;
    queue[1].eligible = false;
    queue[3].eligible = false;
    ci_expect(ci_choose(queue, 5, 200, 0, 3) == SIZE_MAX, "empty eligible queue");
    // Out-of-order arrivals cannot change FIFO identities.
    CiCandidate reordered[] = {queue[1], queue[0]};
    reordered[0].eligible = true;
    reordered[1].eligible = true;
    ci_expect(ci_choose(reordered, 2, 200, 0, 3) == 1, "out-of-order webhook delivery still uses original FIFO age");
}

static void ci_test_lease_and_state(void)
{
    CiState state = {0};
    state.generation = 7;
    state.since_fair = 3;
    state.dispatch_event = 1234;
    state.pr_count = 1;
    state.prs[0].number = 546;
    state.prs[0].manual = -1;
    CiIdentity current = ci_test_identity(546);
    ci_observe(&state.prs[0], &current, 100);
    CiLease* lease = &state.lease;
    lease->pr = 546;
    lease->run = 999;
    lease->attempt = 1;
    lease->admitted_at = 150;
    lease->expires_at = 150 + BUSTER_CI_LEASE_SECONDS;
    ci_copy(lease->nonce, sizeof(lease->nonce), "1234-7");
    ci_copy(lease->head, sizeof(lease->head), current.head);
    ci_copy(lease->base, sizeof(lease->base), current.base);
    ci_copy(lease->merge, sizeof(lease->merge), current.merge);
    ci_copy(lease->workflow_sha, sizeof(lease->workflow_sha), current.base);
    ci_expect(ci_lease_current(lease, &current), "lease pins PR/head/base/merge");
    CiIdentity changed = current;
    changed.head[0] = 'd';
    ci_expect(!ci_lease_current(lease, &changed), "head change invalidates lease");
    changed = current;
    changed.base[0] = 'd';
    ci_expect(!ci_lease_current(lease, &changed), "base change invalidates lease");
    changed = current;
    changed.mergeable = 0;
    ci_expect(!ci_lease_current(lease, &changed), "conflict invalidates lease");
    changed.mergeable = -1;
    ci_expect(!ci_lease_current(lease, &changed), "unknown cannot pass final success guard");
    CiRun run = {0};
    run.id = 999;
    run.attempt = 1;
    run.workflow = 42;
    ci_copy(run.event, sizeof(run.event), "workflow_dispatch");
    ci_copy(run.branch, sizeof(run.branch), "main");
    ci_copy(run.head, sizeof(run.head), lease->workflow_sha);
    ci_copy(run.title, sizeof(run.title), "9700X lease 1234-7");
    ci_expect(ci_executor_identity(&run, lease, 42), "dispatch binds to the reviewed default workflow and exact lease");
    ++run.attempt;
    ci_expect(!ci_executor_identity(&run, lease, 42), "manual rerun cannot reuse a previous attempt's lease");
    --run.attempt;
    run.head[0] = 'f';
    ci_expect(!ci_executor_identity(&run, lease, 42), "default branch dispatch race fails closed");
    Buffer encoded = ci_state_json(&state);
    CiState decoded = ci_state_parse(encoded.data);
    ci_expect(decoded.generation == 7 && decoded.lease.run == 999 && decoded.pr_count == 1 && decoded.prs[0].number == 546 && decoded.prs[0].manual == -1 && strcmp(decoded.lease.head, current.head) == 0, "durable state v1 round trip");
    decoded.lease.revoking = true;
    Buffer revoked = ci_state_json(&decoded);
    CiState recovered = ci_state_parse(revoked.data);
    ci_expect(recovered.lease.revoking && recovered.lease.run == 999, "crash recovery retains the revoked live lease until terminal cancellation");
    ci_expect(recovered.lease.expires_at == state.lease.expires_at, "lease expiry survives controller restart");
    free(encoded.data);
    free(revoked.data);
}

static void ci_test_controller(void)
{
    ci_fixture_init();
    ci_fixture.current.mergeable = 0;
    ci_reconcile(false);
    ci_expect(ci_fixture.dispatches == 0 && strcmp(ci_fixture.required_conclusion, "action_required") == 0, "API: already-conflicting PR never dispatches and cannot get a green required check");

    ci_fixture_init();
    ci_fixture.current.code = false;
    ci_reconcile(false);
    ci_expect(ci_fixture.dispatches == 0 && strcmp(ci_fixture.required_conclusion, "success") == 0, "API: documentation-only PR keeps its successful required check without a hardware lease");
    ci_fixture.current.mergeable = 0;
    ci_reconcile(false);
    ci_expect(strcmp(ci_fixture.required_conclusion, "action_required") == 0, "API: conflicting docs-only PR cannot become a false green skip");

    ci_fixture_init();
    ci_reconcile(false);
    ci_expect(ci_fixture.dispatches == 1 && ci_fixture.state.lease.run == 5001, "API: one write-ahead lease then one dispatch");
    ci_expect(strcmp(ci_fixture.commit_status, "pending") == 0 && strcmp(ci_fixture.status_head, ci_fixture.current.head) == 0, "API: read-only PR jobs cannot replace the authoritative pending same-name commit status");
    ci_expect(strcmp(ci_fixture.required_head, ci_fixture.current.head) == 0 && strcmp(ci_fixture.required_head, ci_fixture.run.head) != 0, "API: required check is associated with the PR head, not the dispatch's main SHA");
    ci_expect(strcmp(ci_fixture.required_conclusion, "success") != 0, "API: admission alone is never required-check success");
    ci_reconcile(false);
    ci_expect(ci_fixture.dispatches == 1 && ci_fixture.cancellations == 0, "API: webhook replay cannot dispatch twice or preempt the active executor");
    ci_expect(ci_fixture.status_writes == 1, "API: queue/phase changes do not exhaust the commit-status append limit");
    unsigned writes = ci_fixture.state_writes;
    unsigned reads = ci_fixture.signal_reads;
    unsigned calls = ci_fixture.http_calls;
    ci_reconcile(false);
    ci_expect(ci_fixture.state_writes == writes && ci_fixture.signal_reads == reads, "API budget: unchanged wakeups reuse signal observations and do not rewrite the ledger");
    ci_expect(ci_fixture.http_calls - calls < 20, "API budget: unchanged reconciliation uses bounded metadata requests independent of PR signal history");
    uint64_t first = ci_fixture.state.lease.run;
    memset(ci_fixture.current.base, 'd', 40);
    ci_fixture.current.mergeable = 0;
    ci_reconcile(false);
    ci_expect(ci_fixture.cancellations != 0 && ci_fixture.cancelled[0] == first, "API: a base advance creating a conflict cancels only the PR's exact executor");
    ci_expect(ci_fixture.state.lease.run == first && ci_fixture.state.lease.revoking, "API: cancellation request/lease expiry does not free a still-running slot");
    ci_expect(strcmp(ci_fixture.required_conclusion, "action_required") == 0 && ci_fixture.dispatches == 1, "API: cancellation cannot report a successful required check or admit overlapping work");
    ci_copy(ci_fixture.run.status, sizeof(ci_fixture.run.status), "completed");
    ci_copy(ci_fixture.run.conclusion, sizeof(ci_fixture.run.conclusion), "cancelled");
    ci_reconcile(false);
    ci_expect(*ci_fixture.state.lease.nonce == 0 && ci_fixture.dispatches == 1, "API: confirmed terminal cancellation releases the lease");
    memset(ci_fixture.current.base, 'e', 40);
    memset(ci_fixture.current.merge, 'f', 40);
    ci_fixture.current.mergeable = 1;
    ci_reconcile(false);
    ci_expect(ci_fixture.dispatches == 2 && ci_fixture.state.lease.run != first && strcmp(ci_fixture.state.lease.head, ci_fixture.current.head) == 0, "API: conflict resolution without a head push schedules exactly one current qualification");
    ci_fixture.current.mergeable = -1;
    unsigned cancelled = ci_fixture.cancellations;
    ci_reconcile(false);
    ci_expect(ci_fixture.cancellations == cancelled && !ci_fixture.state.lease.revoking, "API: transient UNKNOWN with unchanged identities is not a confirmed conflict cancellation");
    ci_expect(strcmp(ci_fixture.required_conclusion, "success") != 0, "API: UNKNOWN never yields required success");
    ci_fixture.current.mergeable = 1;
    ci_copy(ci_fixture.run.status, sizeof(ci_fixture.run.status), "completed");
    ci_copy(ci_fixture.run.conclusion, sizeof(ci_fixture.run.conclusion), "success");
    ci_reconcile(false);
    ci_expect(ci_fixture.state.prs[0].outcome == 1 && strcmp(ci_fixture.required_conclusion, "success") == 0, "API: native executor success plus successful guard and current identities yields the PR check");
    ci_expect(strcmp(ci_fixture.commit_status, "success") == 0, "API: both the check and authoritative same-name status pass only after current qualification");
    ci_expect(ci_fixture.dispatches == 2, "API: completed current qualification is not automatically duplicated");

    ci_fixture_init();
    ci_fixture.dispatch_returns_id = false;
    ci_reconcile(false);
    ci_expect(ci_fixture.state.lease.run == 0 && ci_fixture.dispatches == 1, "API: legacy 204/ambiguous dispatch leaves a durable unbound nonce");
    ci_reconcile(false);
    ci_expect(ci_fixture.state.lease.run == ci_fixture.run.id && ci_fixture.dispatches == 1, "API: controller crash/204 recovery binds by exact nonce without redispatch");
    ci_fixture_clock = ci_fixture.state.lease.expires_at + 1;
    ci_reconcile(false);
    ci_expect(ci_fixture.state.lease.revoking && ci_fixture.state.lease.run != 0, "API: runner downtime/expired live lease retains ownership until cancellation is terminal");

    const char* bad[] = {"skipped", "cancelled", "failure", "timed_out"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        ci_fixture_init();
        ci_reconcile(false);
        ci_fixture.executor_conclusion = bad[i];
        ci_copy(ci_fixture.run.status, sizeof(ci_fixture.run.status), "completed");
        ci_copy(ci_fixture.run.conclusion, sizeof(ci_fixture.run.conclusion), "success");
        ci_reconcile(false);
        ci_expect(ci_fixture.state.prs[0].outcome == 2 && strcmp(ci_fixture.required_conclusion, "success") != 0, "API: skipped/cancelled/failed/timed-out executor cannot make a false green");
        ci_expect(strcmp(ci_fixture.commit_status, "success") != 0, "API: a misleading native check success cannot override the authoritative failure status");
    }

    ci_fixture_init();
    ci_fixture.current.fork = true;
    ci_reconcile(false);
    ci_expect(ci_fixture.dispatches == 0, "API: unreviewed public fork cannot reach the physical host");

    ci_fixture_init();
    ci_fixture.current.mergeable = 0;
    ci_fixture.legacy = true;
    ci_fixture.background_enabled = true;
    ci_copy(ci_fixture.native.status, sizeof(ci_fixture.native.status), "queued");
    ci_reconcile(false);
    ci_expect(ci_fixture.cancellations == 1 && ci_fixture.cancelled[0] == 4000, "API: queued conflicting native PR is cancelled; main/daily/manual background is untouched");

    ci_fixture_init();
    ci_reconcile(false);
    memset(ci_fixture.current.head, 'd', 40);
    ci_copy(ci_fixture.run.status, sizeof(ci_fixture.run.status), "completed");
    ci_copy(ci_fixture.run.conclusion, sizeof(ci_fixture.run.conclusion), "success");
    ci_reconcile(false);
    ci_expect(ci_fixture.state.prs[0].outcome == 0 && strcmp(ci_fixture.required_head, ci_fixture.current.head) == 0 && strcmp(ci_fixture.required_conclusion, "success") != 0, "API: an old revision's completion cannot qualify a new head");

    ci_fixture_init();
    ci_reconcile(false);
    ci_fixture.current.open = false;
    ci_reconcile(false);
    ci_expect(ci_fixture.state.pr_count == 0 && ci_fixture.state.lease.revoking && ci_fixture.cancellations == 1, "API: close expires PR-wide state and cancels only its live qualification");
    ci_fixture.current.open = true;
    ci_copy(ci_fixture.run.status, sizeof(ci_fixture.run.status), "completed");
    ci_copy(ci_fixture.run.conclusion, sizeof(ci_fixture.run.conclusion), "cancelled");
    ci_reconcile(false);
    ci_expect(ci_fixture.state.pr_count == 1 && ci_fixture.state.prs[0].manual == -1 && ci_fixture.dispatches == 2, "API: reopen starts a fresh current qualification and cannot restore a closed PR's override");
}

static Buffer ci_test_comment_body(const char* body, bool trusted, uint64_t id, uint64_t updated_at)
{
    Buffer fixture = {0};
    buffer_printf(&fixture, "[{\"id\":%" PRIu64 ",\"updated_at\":", id);
    ci_fixture_date(&fixture, updated_at);
    buffer_printf(&fixture, ",\"user\":{\"login\":\"%s\",\"id\":%u},\"body\":", trusted ? "maintainer" : "stranger", trusted ? 123 : 666);
    json_quote(&fixture, body);
    buffer_add(&fixture, "}]", 2);
    return fixture;
}

static Buffer ci_test_review_body(const char* decision, const char* sha, bool trusted)
{
    Buffer fixture = {0};
    buffer_printf(&fixture, "[{\"id\":1,\"state\":\"%s\",\"commit_id\":\"%s\",\"user\":{\"login\":\"%s\",\"id\":%u},\"submitted_at\":", decision, sha, trusted ? "maintainer" : "stranger", trusted ? 123 : 666);
    ci_fixture_date(&fixture, ci_now() - 50);
    buffer_add(&fixture, "}]", 2);
    return fixture;
}

static void ci_test_signals(void)
{
    ci_fixture_init();
    CiState state = {0};
    state.pr_count = 1;
    CiPr* pr = &state.prs[0];
    pr->number = 546;
    pr->manual = -1;
    ci_observe(pr, &ci_fixture.current, ci_now() - 100);
    pr->request_run = 4000;
    CiIdentity identity = ci_fixture.current;
    Buffer comment = ci_test_comment_body("LGTM", false, 101, ci_now() - 50);
    ci_fixture.comments = comment.data;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(!identity.approved, "API: an untrusted approval comment cannot elevate priority");
    free(comment.data);
    comment = ci_test_comment_body("LGTM", true, 102, ci_now() - 49);
    ci_fixture.comments = comment.data;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(identity.approved, "API: explicit authorized approval elevates the observed revision");
    free(comment.data);
    comment = ci_test_comment_body("not LGTM", true, 102, ci_now() - 48);
    ci_fixture.comments = comment.data;
    identity.approved = false;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(!identity.approved, "API: edited approval removal invalidates derived priority");
    free(comment.data);
    ci_fixture.comments = "[]";
    identity.approved = false;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(!identity.approved, "API: deleted approval removal invalidates derived priority");

    comment = ci_test_comment_body("/ci priority critical", false, 103, ci_now() - 47);
    ci_fixture.comments = comment.data;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(pr->manual == -1 && strstr(pr->audit, "Rejected") != NULL, "API: unauthorized manual command is rejected and observable");
    free(comment.data);
    comment = ci_test_comment_body("/ci priority high run 4000", true, 104, ci_now() - 46);
    ci_fixture.comments = comment.data;
    ci_comments(&state, pr, &identity, ci_now());
    CiOverride* override = ci_find_override(&state, 4000, pr->head, false);
    ci_expect(override != NULL && override->manual == 1 && override->pr == 546, "API: authorized run-specific override binds the current request ID and SHA");
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(state.override_count == 1, "API: replayed run command cannot create duplicate state");
    free(comment.data);
    comment = ci_test_comment_body("/ci priority normal run 4000", true, 104, ci_now() - 45);
    ci_fixture.comments = comment.data;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(override->manual == -1, "API: edited run-specific normal command clears the manual override");
    free(comment.data);
    comment = ci_test_comment_body("/ci priority low run 4000", true, 104, ci_now() - 45);
    ci_fixture.comments = comment.data;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(override->manual == 4, "API: same-second edit uses the current body hash and does not disappear as a replay");
    free(comment.data);
    comment = ci_test_comment_body("/ci hold run 9999", true, 105, ci_now() - 44);
    ci_fixture.comments = comment.data;
    ci_comments(&state, pr, &identity, ci_now());
    ci_expect(strstr(pr->audit, "Rejected") != NULL && state.override_count == 1, "API: a missing/unrelated run command is rejected without wedging future reconciliation");
    free(comment.data);
    ci_fixture.comments = "[]";

    const char* decisions[] = {"APPROVED", "DISMISSED", "CHANGES_REQUESTED"};
    for (size_t i = 0; i < 3; ++i)
    {
        Buffer review = ci_test_review_body(decisions[i], identity.head, true);
        ci_fixture.reviews = review.data;
        identity.approved = false;
        ci_reviews(&identity);
        ci_expect(identity.approved == (i == 0), "API: only current trusted APPROVED reviews elevate; dismissal/changes requested do not");
        free(review.data);
    }
    Buffer review = ci_test_review_body("APPROVED", identity.base, true);
    ci_fixture.reviews = review.data;
    identity.approved = false;
    ci_reviews(&identity);
    ci_expect(!identity.approved, "API: stale review commit cannot approve a new revision");
    free(review.data);
    review = ci_test_review_body("APPROVED", identity.head, false);
    ci_fixture.reviews = review.data;
    identity.approved = false;
    ci_reviews(&identity);
    ci_expect(!identity.approved, "API: untrusted formal review cannot elevate priority");
    free(review.data);
    ci_fixture.reviews = "[]";
}

static void ci_test_guard(void)
{
    ci_fixture_init();
    ci_reconcile(false);
    FILE* event = tmpfile();
    if (event == NULL)
    {
        ci_die("cannot create guard fixture event");
    }
    fputs(ci_fixture.worker_event, event);
    fflush(event);
    char path[80];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fileno(event));
    setenv("GITHUB_EVENT_PATH", path, 1);
    char run[32];
    snprintf(run, sizeof(run), "%" PRIu64, ci_fixture.run.id);
    setenv("GITHUB_RUN_ID", run, 1);
    ci_expect(ci_guard_once(false), "guard: exact run/attempt/nonce/head/base/merge/source revision is admitted");
    ci_fixture.current.head[0] = 'd';
    ci_expect(!ci_guard_once(false), "guard: event-to-admission head race cannot reach hardware");
    ci_fixture.current.head[0] = 'a';
    ci_fixture.current.base[0] = 'd';
    ci_expect(!ci_guard_once(false), "guard: base advance before hardware admission is refused");
    ci_fixture.current.base[0] = 'b';
    ci_fixture.current.mergeable = 0;
    ci_expect(!ci_guard_once(false), "guard: conflict appearing after selection is refused immediately before build");
    ci_fixture.current.mergeable = -1;
    ci_expect(!ci_guard_once(false), "guard: UNKNOWN cannot pass admission");
    ci_fixture.current.mergeable = 1;
    setenv("GITHUB_RUN_ATTEMPT", "2", 1);
    ci_expect(!ci_guard_once(false), "guard: rerun attempt needs a fresh lease");
    setenv("GITHUB_RUN_ATTEMPT", "1", 1);
    ci_fixture.state.lease.revoking = true;
    ci_expect(!ci_guard_once(false), "guard: revocation prevents a delayed/cancelled job from entering the host");
    ci_fixture.state.lease.revoking = false;
    uint64_t before_expiry = ci_now();
    ci_fixture_clock = ci_fixture.state.lease.expires_at;
    ci_expect(!ci_guard_once(false), "guard: expiry never authorizes a second delayed job");
    ci_fixture_clock = before_expiry;
    setenv("GITHUB_WORKFLOW_SHA", ci_fixture.current.head, 1);
    ci_expect(!ci_guard_once(false), "guard: unreviewed or raced workflow source cannot execute a trusted lease");
    setenv("GITHUB_WORKFLOW_SHA", ci_fixture.workflow_sha, 1);
    ci_fixture.state.lease.run = 0;
    ci_expect(!ci_guard_once(false), "guard: an ambiguous/unbound dispatch response cannot start work");
    ci_fixture.state.lease.run = ci_fixture.run.id;
    ci_fixture.state.lease.nonce[0] = '9';
    ci_expect(!ci_guard_once(false), "guard: a forged/mismatched dispatch input nonce is refused");
    fclose(event);
    ci_read_only = false;
}

static void ci_test_dispatch_controls(void)
{
    ci_fixture_init();
    ci_fixture.current.code = false;
    ci_fixture.commands = "[{\"id\":7002,\"head_branch\":\"main\",\"display_title\":\"9700X command pr 546 run 0 action clear priority normal\",\"actor\":{\"login\":\"maintainer\",\"id\":123}},{\"id\":7001,\"head_branch\":\"main\",\"display_title\":\"9700X command pr 546 run 0 action priority priority critical\",\"actor\":{\"login\":\"maintainer\",\"id\":123}}]";
    ci_reconcile(false);
    ci_expect(ci_fixture.state.dispatch_event == 7002 && ci_fixture.state.prs[0].manual == -1, "dispatch: coalesced/cancelled events recover in original order; reset wins");
    ci_reconcile(false);
    ci_expect(ci_fixture.state.prs[0].manual == -1 && ci_fixture.state.dispatch_event == 7002, "dispatch: recovery/replay is idempotent");
    ci_fixture.commands = "[{\"id\":7003,\"head_branch\":\"main\",\"display_title\":\"9700X command pr 546 run 4000 action priority priority high\",\"actor\":{\"login\":\"maintainer\",\"id\":123}}]";
    ci_reconcile(false);
    CiOverride* override = ci_find_override(&ci_fixture.state, 4000, ci_fixture.current.head, false);
    ci_expect(override != NULL && override->manual == 1, "dispatch: run-level override uses the same strict policy as comments");
    ci_fixture.commands = "[{\"id\":7004,\"head_branch\":\"main\",\"display_title\":\"9700X command pr 546 run 0 action priority priority critical\",\"actor\":{\"login\":\"stranger\",\"id\":666}}]";
    ci_reconcile(false);
    ci_expect(ci_fixture.state.prs[0].manual == -1 && ci_fixture.state.dispatch_event == 7004, "dispatch: actual actor permission is required; rejected commands are audited without replaying forever");
    ci_fixture.commands = "[{\"id\":7005,\"head_branch\":\"untrusted\",\"display_title\":\"9700X command pr 546 run 0 action priority priority critical\",\"actor\":{\"login\":\"maintainer\",\"id\":123}}]";
    ci_reconcile(false);
    ci_expect(ci_fixture.state.prs[0].manual == -1, "dispatch: non-default-branch command workflows are never authoritative");

    ci_fixture_init();
    ci_fixture.current.code = false;
    Buffer old = ci_test_comment_body("/ci priority critical", true, 200, ci_now() - 50);
    Buffer lifecycle = {0};
    buffer_add(&lifecycle, "[{\"event\":\"closed\",\"created_at\":", strlen("[{\"event\":\"closed\",\"created_at\":"));
    ci_fixture_date(&lifecycle, ci_now() - 10);
    buffer_add(&lifecycle, "}]", 2);
    ci_fixture.comments = old.data;
    ci_fixture.events = lifecycle.data;
    ci_reconcile(false);
    ci_expect(ci_fixture.state.prs[0].manual == -1, "lifecycle: pre-closure commands cannot resurrect a PR-wide override on reopen");
    free(old.data);
    old = ci_test_comment_body("/ci priority high", true, 201, ci_now() - 5);
    ci_fixture.comments = old.data;
    ci_fixture_clock += BUSTER_CI_SIGNAL_REFRESH_SECONDS;
    ci_reconcile(false);
    ci_expect(ci_fixture.state.prs[0].manual == 1, "lifecycle: new post-reopen commands work even when the initial webhook was missed");
    free(old.data);
    free(lifecycle.data);
    ci_fixture.comments = "[]";
    ci_fixture.events = "[]";
}

static int ci_self_test(void)
{
    ci_test_json();
    ci_test_commands();
    ci_test_policy();
    ci_test_scheduler();
    ci_test_lease_and_state();
    ci_test_controller();
    ci_test_signals();
    ci_test_guard();
    ci_test_dispatch_controls();
    ci_fixture_http = false;
    free(ci_fixture.worker_event);
    ci_fixture.worker_event = NULL;
    printf("9700X admission: %u checks, %u failures\n", ci_test_count, ci_test_failures);
    return ci_test_failures != 0;
}
