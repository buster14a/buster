// Temporary hosted diagnostic, read-only. Excluded from the submitted PR.
#define main lifecycle_original_main
#include "tools/bench_direct/lifecycle.c"
#undef main
int main(void)
{
    CmTransport t = {0}; t.deadline = cm_clock() + 180; t.request_limit = 60;
    LcIdentity id = {0};
    int valid = lc_bind(&t, UINT64_C(38297977458), 1, &id);
    CmJson check = cm_api_json(&t, "check-runs/114973570744", "GET", NULL, NULL);
    valid = valid && lc_owned(&check, 1, &id) && lc_url_bound(&check, &id);
    if (valid)
    {
        const char *prior = cm_get(&check, cm_member(&check, 1, "output"), "summary");
        id.superseded = lc_pending_superseded(&t, &id, prior);
        valid = id.superseded;
        char *body = lc_body(&id, prior, 0);
        valid = valid && body;
        if (body) { puts(body); free(body); }
    }
    cm_json_free(&check);
    printf("LIVE_PENDING_SUPERSESSION=%s requests=%u failures=%u\n", valid ? "verified" : "unavailable", t.requests, t.failures);
    int result = valid ? 0 : 2;
    return result;
}
