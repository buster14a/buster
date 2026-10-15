// Pending-main reporting (#3885). Native cancellation stays cancelled.
// lc_pending_superseded requires GitHub's own pending-replacement annotation,
// an empty complete exact-attempt job inventory and the pinned workflow policy.
// REST has no cancellation-cause field; unavailable/changed HTML fails closed.
// This header uses the existing bounded gh transport, never a runner credential.
#ifndef BUSTER_9700X_LIFECYCLE_SUPERSESSION_H
#define BUSTER_9700X_LIFECYCLE_SUPERSESSION_H
#define LC_PENDING_MESSAGE "Canceling since a higher priority waiting request for buster-9700x-direct-bench-main-compare exists"
#define LC_PENDING_POLICY "\nconcurrency:\n" \
    "  group: buster-9700x-direct-bench-${{ github.event.workflow_run.event == 'push' && 'main-compare' || github.event.workflow_run.head_branch }}\n" \
    "  cancel-in-progress: ${{ github.event.workflow_run.event != 'push' }}\n\njobs:\n"

BUSTER_GLOBAL_LOCAL char *lc_public_page(CmTransport *t, uint64_t run)
{
    char url[256];
    snprintf(url, sizeof(url), "https://github.com/" CM_REPO "/actions/runs/%" PRIu64 "/attempts/1", run);
    char *result = NULL;
    int valid = t->requests < (t->request_limit ? t->request_limit : CM_REQUESTS) && cm_clock() < t->deadline;
    if (valid)
    {
        ++t->requests;
        if (t->fixture)
        {
            valid = t->fixture_cursor < t->fixture_count;
            if (valid)
            {
                const CmResponse *response = &t->fixture[t->fixture_cursor++];
                valid = cm_equal(url, response->path) && cm_equal(response->method, "GET") && response->success &&
                    response->content && strlen(response->content) <= CM_BYTES;
                if (valid) result = strdup(response->content);
                valid &= result != NULL;
            }
        }
        else
        {
            // One public, canonical GitHub URL through the existing gh CLI.
            // No payload, shell, extra dependency or physical-runner capability.
            char *args[] = {"/usr/bin/gh", "api", "--hostname", "github.com", url, "-X", "GET",
                "-H", "Accept: text/html", NULL};
            valid = cm_process(args, &result, CM_BYTES, NULL);
        }
    }
    if (!valid) { ++t->failures; free(result); result = NULL; }
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_pending_annotation(const char *page, uint64_t run)
{
    char link[192];
    snprintf(link, sizeof(link), "href=\"/" CM_REPO "/actions/runs/%" PRIu64 "/workflow\"", run);
    const char *target = "data-target=\"annotation-message.annotationContainer\"";
    const char *message = "<div>" LC_PENDING_MESSAGE "</div>";
    while (page && (*page == ' ' || *page == '\n' || *page == '\r' || *page == '\t')) ++page;
    int valid = page && (strncmp(page, "<!DOCTYPE html>", 15) == 0 || strncmp(page, "<!doctype html>", 15) == 0) &&
        strstr(page, "</html>");
    unsigned matches = 0;
    const char *cursor = valid ? page : NULL;
    while (cursor && (cursor = strstr(cursor, link)) != NULL)
    {
        const char *annotation = strstr(cursor, target);
        if (annotation && annotation - cursor < 1024)
        {
            const char *body = strchr(annotation, '>');
            if (body)
            {
                ++body;
                while (*body == ' ' || *body == '\n' || *body == '\r' || *body == '\t') ++body;
                matches += strncmp(body, message, strlen(message)) == 0;
            }
        }
        cursor += strlen(link);
    }
    int result = valid && matches == 1;
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_empty_jobs(const CmJson *jobs)
{
    unsigned total = cm_member(jobs, 1, "total_count"), array = cm_member(jobs, 1, "jobs");
    // Missing/null/string counts cannot masquerade as zero; no pagination can
    // be incomplete when the server declares zero and the array is empty.
    int result = jobs->valid && total && jobs->tokens[total].kind == 'v' &&
        cm_equal(cm_value(jobs, total), "0") && array && jobs->tokens[array].kind == 'a' &&
        !jobs->tokens[array].child;
    return result;
}
BUSTER_GLOBAL_LOCAL int lc_current_main(CmTransport *t, const LcIdentity *id)
{
    char path[256];
    snprintf(path, sizeof(path), "actions/runs/%" PRIu64, id->executor);
    CmJson executor = cm_api_json(t, path, "GET", NULL, NULL);
    LcIdentity current = {0};
    int valid = lc_run(&executor, id->executor, 1) && cm_equal(cm_get(&executor, 1, "path"), LC_BENCH) &&
        cm_equal(cm_get(&executor, 1, "event"), "workflow_run") && cm_equal(cm_get(&executor, 1, "head_branch"), "main") &&
        cm_equal(cm_get(&executor, 1, "head_sha"), id->trusted) && cm_equal(cm_get(&executor, 1, "conclusion"), "cancelled") &&
        lc_title(cm_get(&executor, 1, "display_title"), &current) && current.request == id->request &&
        current.request_attempt == id->request_attempt && cm_equal(current.head, id->head);
    if (valid)
    {
        snprintf(path, sizeof(path), "actions/runs/%" PRIu64, id->request);
        CmJson request = cm_api_json(t, path, "GET", NULL, NULL);
        valid = lc_run(&request, id->request, 1) && cm_equal(cm_get(&request, 1, "path"), LC_MAIN) &&
            cm_equal(cm_get(&request, 1, "event"), "push") && cm_equal(cm_get(&request, 1, "head_branch"), "main") &&
            cm_equal(cm_get(&request, 1, "head_sha"), id->head) && cm_equal(cm_get(&request, 1, "conclusion"), "success");
        cm_json_free(&request);
    }
    cm_json_free(&executor);
    return valid;
}
BUSTER_GLOBAL_LOCAL int lc_pending_superseded(CmTransport *t, const LcIdentity *id, const char *summary)
{
    char request[384], path[256];
    snprintf(request, sizeof(request), LC_REQUEST "%" PRIu64 " attempt 1: https://github.com/" CM_REPO
        "/actions/runs/%" PRIu64 "/attempts/1", id->request, id->request);
    int valid = id->pending_probe && !id->pull && !id->research && !id->request_only &&
        id->attempt == 1 && id->request_attempt == 1 && cm_equal(id->outcome, "cancelled") &&
        lc_binding(summary, "Lifecycle protocol: ", LC_NATIVE) == 1 && lc_binding(summary, LC_REQUEST, request) == 1;
    if (valid)
    {
        snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/attempts/1/jobs?per_page=100", id->executor);
        CmJson jobs = cm_api_json(t, path, "GET", NULL, NULL);
        valid = lc_empty_jobs(&jobs);
        cm_json_free(&jobs);
    }
    if (valid)
    {
        snprintf(path, sizeof(path), "contents/" LC_BENCH "?ref=%s", id->trusted);
        char *policy = NULL;
        valid = cm_api(t, path, "GET", NULL, 1, &policy, NULL);
        const char *concurrency = valid && policy ? strstr(policy, "\nconcurrency:\n") : NULL;
        valid = concurrency && strncmp(concurrency, LC_PENDING_POLICY, strlen(LC_PENDING_POLICY)) == 0 &&
            !strstr(concurrency + 1, "\nconcurrency:\n");
        free(policy);
    }
    if (valid)
    {
        char *page = lc_public_page(t, id->executor);
        valid = lc_pending_annotation(page, id->executor);
        free(page);
    }
    // Current endpoints prevent a replayed old attempt from neutralizing a
    // changed executor/request. They are read after all other evidence.
    if (valid) valid = lc_current_main(t, id);
    return valid;
}
#endif
