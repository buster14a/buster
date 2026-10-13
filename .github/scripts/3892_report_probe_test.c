// Temporary hosted-only producer/reader composition probe; no API transport.
#define main lc_production_main
#include "../../tools/bench_direct/lifecycle.c"
#undef main

int main(void)
{
    const char *outcomes[] = {"cancelled", "cancelled", "failure", "timed_out", "skipped", "success"};
    LcIdentity id = {0};
    id.executor = 38297977458; id.attempt = 1;
    id.request = 38297937496; id.request_attempt = 1;
    cm_copy(id.head, sizeof(id.head), "a4ea18116d1dd5d03ecf7287234fe61f4fb40267");
    cm_copy(id.trusted, sizeof(id.trusted), id.head);
    lc_marker(&id);
    int result = 0;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(outcomes); ++i)
    {
        cm_copy(id.outcome, sizeof(id.outcome), outcomes[i]);
        id.superseded = i == 0;
        for (unsigned prior = 0; prior < 3; ++prior)
        {
            const char *history = prior == 0 ? NULL : prior == 1 ?
                "**9700X compiler benchmark: queued**\n\n" LC_NATIVE "\nBaseline: unavailable.\n" :
                "Baseline `0123456789012345678901234567890123456789` (first parent).\n"
                "[Workflow run, attempt 1](https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1)\n";
            char *body = lc_body(&id, history, 0);
            if (body) puts(body);
            else result = 1;
            free(body);
        }
    }
    return result;
}
