// Bounded Actions discovery for #3412. mp_runs owns complete active-state or
// exact-head inventories; mp_pending owns current-attempt jobs and rechecks.
// JSON parsing is shared with the native CI collector. API reads use a narrow
// stdin/stdout protocol; the legacy adapter owns HTTP only, never discovery.
// mp_self_test uses synthetic API responses and the production discovery path.
#include "ci_metrics_json.h"
#define MP_PAGES 10u
#define MP_ROWS 5000u
#define MP_REQUESTS 200u
typedef struct MpRun MpRun;
struct MpRun
{
    uint64_t id, number, attempt;
    char head[41], status[24];
};
typedef struct MpState MpState;
struct MpState
{
    const char *workflow, *target, *head, *error, *phase, *active_status;
    uint64_t number;
    unsigned requests, pages, count;
    MpRun *runs;
    int successor, fixture, scenario;
    unsigned history, active;
};
BUSTER_GLOBAL_LOCAL const char *mp_states[] = {"queued", "in_progress", "waiting", "pending", "requested"};
BUSTER_GLOBAL_LOCAL int mp_active(const char *status)
{
    int result = 0;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(mp_states); ++i) result |= cm_equal(status, mp_states[i]);
    return result;
}
BUSTER_GLOBAL_LOCAL int mp_fail(MpState *s, const char *message)
{
    if (!s->error) s->error = message;
    return 0;
}
BUSTER_GLOBAL_LOCAL int mp_name(const char *name)
{
    int result = name && name[0] && strlen(name) < 128;
    for (unsigned i = 0; result && name[i]; ++i)
    {
        char c = name[i];
        result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int mp_uint(const CmJson *j, unsigned node, const char *key, uint64_t *out)
{
    unsigned field = cm_member(j, node, key);
    int result = field && j->tokens[field].kind == 'v' && cm_unsigned(cm_value(j, field), out);
    return result;
}
BUSTER_GLOBAL_LOCAL int mp_string(const CmJson *j, unsigned node, const char *key)
{
    unsigned field = cm_member(j, node, key);
    int result = field && j->tokens[field].kind == 's';
    return result;
}
BUSTER_GLOBAL_LOCAL int mp_run(const CmJson *j, unsigned node, MpRun *out)
{
    int result = node && j->tokens[node].kind == 'o' &&
        mp_uint(j, node, "id", &out->id) && out->id &&
        mp_uint(j, node, "run_number", &out->number) && out->number &&
        mp_uint(j, node, "run_attempt", &out->attempt) && out->attempt &&
        mp_string(j, node, "head_sha") && cm_sha(cm_get(j, node, "head_sha")) &&
        mp_string(j, node, "event") && mp_string(j, node, "head_branch") &&
        mp_string(j, node, "status") &&
        (mp_active(cm_get(j, node, "status")) || cm_equal(cm_get(j, node, "status"), "completed"));
    if (result)
    {
        cm_copy(out->head, sizeof(out->head), cm_get(j, node, "head_sha"));
        cm_copy(out->status, sizeof(out->status), cm_get(j, node, "status"));
    }
    return result;
}
BUSTER_GLOBAL_LOCAL char *mp_file(FILE *f)
{
    char *result = NULL;
    if (f && fflush(f) == 0 && fseek(f, 0, SEEK_END) == 0)
    {
        long n = ftell(f);
        if (n >= 0 && n < CM_BYTES && fseek(f, 0, SEEK_SET) == 0)
        {
            result = malloc((size_t)n + 1);
            if (result)
            {
                size_t got = fread(result, 1, (size_t)n, f);
                if (got == (size_t)n && !ferror(f)) result[got] = 0;
                else { free(result); result = NULL; }
            }
        }
    }
    return result;
}
BUSTER_GLOBAL_LOCAL void mp_emit_run(FILE *f, uint64_t id, uint64_t number, uint64_t attempt,
    const char *head, const char *status, const char *event, const char *branch)
{
    fprintf(f, "{\"id\":%" PRIu64 ",\"run_number\":%" PRIu64 ",\"run_attempt\":%" PRIu64 ",\"head_sha\":", id, number, attempt);
    cm_quote(f, head); fputs(",\"status\":", f); cm_quote(f, status);
    fputs(",\"event\":", f); cm_quote(f, event); fputs(",\"head_branch\":", f); cm_quote(f, branch); fputc('}', f);
}
BUSTER_GLOBAL_LOCAL char *mp_fixture(MpState *s, const char *path, const char *filter, unsigned page)
{
    FILE *f = tmpfile();
    char *result = NULL;
    const char *a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", *b = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    if (f)
    {
        if (strstr(path, "/runs") && !strstr(path, "/jobs") && !strncmp(path, "actions/workflows/", 18))
        {
            // Completed histories of different sizes exist in the synthetic
            // server; a broad query would return them and trip its 1000-row cap.
            int scoped = s->successor ? filter && cm_sha(filter) : filter && mp_active(filter);
            unsigned total = scoped ? (s->successor ? 1 : cm_equal(filter, (s->active_status ? s->active_status : mp_states[s->scenario == 1 ? 1 : 0])) ? s->active : 0) : s->history;
            fprintf(f, "{\"total_count\":%u,\"workflow_runs\":[", total);
            unsigned start = (page - 1) * 100, end = total < start + 100 ? total : start + 100;
            for (unsigned i = start; i < end; ++i)
            {
                if (i > start) fputc(',', f);
                mp_emit_run(f, 100 + i, s->successor ? 11 : 1 + i, 2, s->successor ? b : a,
                    filter && mp_active(filter) ? filter : "completed",
                    s->scenario == 7 ? "workflow_dispatch" : "push", s->scenario == 8 ? "other" : "main");
            }
            fputs("]}", f);
        }
        else if (strstr(path, "/jobs"))
        {
            int missing = s->scenario == 2;
            fprintf(f, "{\"total_count\":%d,\"jobs\":[", !missing);
            if (!missing)
                fprintf(f, "{\"id\":200,\"name\":\"target\",\"status\":\"%s\",\"run_id\":100,\"run_attempt\":%d}",
                    s->scenario == 3 || s->scenario == 4 ? "completed" : "in_progress", s->scenario == 4 ? 1 : 2);
            fputs("]}", f);
        }
        else
        {
            mp_emit_run(f, 100, 1, s->scenario == 5 ? 3 : 2, a,
                s->scenario == 6 ? "completed" : "in_progress", "push", "main");
        }
        result = mp_file(f); fclose(f);
    }
    return result;
}
BUSTER_GLOBAL_LOCAL CmJson mp_request(MpState *s, const char *path, const char *filter, unsigned page, int jobs)
{
    char *text = NULL;
    int valid = s->requests < MP_REQUESTS;
    if (valid)
    {
        ++s->requests;
        if (s->fixture) text = mp_fixture(s, path, filter, page);
        else
        {
            fputs("{\"request\":", stdout); cm_quote(stdout, path); fputs(",\"query\":{", stdout);
            if (page)
            {
                fprintf(stdout, "\"per_page\":100,\"page\":%u", page);
                if (!jobs)
                {
                    fputs(",\"event\":\"push\",\"branch\":\"main\",", stdout);
                    cm_quote(stdout, s->successor ? "head_sha" : "status");
                    fputc(':', stdout); cm_quote(stdout, filter);
                }
            }
            fputs("}}\n", stdout); fflush(stdout);
            text = malloc(CM_BYTES + 1);
            if (text)
            {
                size_t n = 0;
                int c = 0;
                while (n < CM_BYTES && (c = getchar()) != EOF && c != '\n') text[n++] = (char)c;
                valid = c == '\n' && n < CM_BYTES;
                text[n] = 0;
            }
            else valid = 0;
        }
    }
    CmJson result = cm_json_parse(text ? text : "", text ? strlen(text) : 0);
    result.valid &= valid;
    free(text);
    if (!result.valid) mp_fail(s, valid ? "malformed API response" : "request or response bound reached");
    return result;
}
BUSTER_GLOBAL_LOCAL int mp_batch(MpState *s, const CmJson *j, const char *key, unsigned page,
    uint64_t *expected, unsigned *array, unsigned *count)
{
    uint64_t total = 0;
    *array = cm_member(j, 1, key); *count = 0;
    int valid = j->valid && mp_uint(j, 1, "total_count", &total) && total <= MP_PAGES * 100 &&
        *array && j->tokens[*array].kind == 'a';
    for (unsigned row = valid ? j->tokens[*array].child : 0; valid && row; row = j->tokens[row].next)
        valid = j->tokens[row].kind == 'o' && ++*count <= 100;
    if (page == 1) *expected = total;
    unsigned offset = (page - 1) * 100;
    uint64_t remaining = total >= offset ? total - offset : 0;
    valid = valid && total == *expected && total >= offset &&
        *count == (remaining < 100 ? remaining : 100);
    if (!valid) mp_fail(s, "incomplete or malformed inventory page");
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_runs(MpState *s)
{
    char path[256]; snprintf(path, sizeof(path), "actions/workflows/%s/runs", s->workflow);
    int valid = 1;
    unsigned buckets = s->successor ? 1 : BUSTER_ARRAY_LENGTH(mp_states);
    for (unsigned bucket = 0; valid && bucket < buckets; ++bucket)
    {
        uint64_t expected = 0, ids[MP_PAGES * 100];
        unsigned seen = 0;
        int complete = 0;
        for (unsigned page = 1; valid && !complete && page <= MP_PAGES; ++page)
        {
            CmJson j = mp_request(s, path, s->successor ? s->head : mp_states[bucket], page, 0);
            ++s->pages;
            unsigned array = 0, count = 0;
            valid = mp_batch(s, &j, "workflow_runs", page, &expected, &array, &count);
            for (unsigned node = valid ? j.tokens[array].child : 0; valid && node; node = j.tokens[node].next)
            {
                MpRun run = {0};
                valid = mp_run(&j, node, &run);
                for (unsigned i = 0; valid && i < seen; ++i) valid = ids[i] != run.id;
                if (valid) ids[seen++] = run.id;
                int relevant = cm_equal(cm_get(&j, node, "event"), "push") &&
                    cm_equal(cm_get(&j, node, "head_branch"), "main") &&
                    (s->successor ? run.number > s->number && cm_equal(run.head, s->head) : run.number < s->number);
                unsigned wf = cm_member(&j, node, "path");
                if (wf)
                {
                    char wanted[256]; snprintf(wanted, sizeof(wanted), ".github/workflows/%s", s->workflow);
                    relevant &= mp_string(&j, node, "path") && cm_equal(cm_value(&j, wf), wanted);
                }
                if (valid && relevant)
                {
                    unsigned at = s->count;
                    for (unsigned i = 0; i < s->count; ++i) if (s->runs[i].id == run.id) at = i;
                    if (at < s->count)
                    {
                        valid = s->runs[at].number == run.number && cm_equal(s->runs[at].head, run.head);
                        // Movement across status buckets is retained at the latest
                        // attempt. An older response cannot erase a new rerun.
                        if (valid && run.attempt >= s->runs[at].attempt) s->runs[at] = run;
                    }
                    else
                    {
                        valid = s->count < MP_ROWS;
                        if (valid) s->runs[s->count++] = run;
                    }
                }
            }
            complete = valid && seen == expected;
            cm_json_free(&j);
        }
        valid = valid && complete;
    }
    if (!valid) mp_fail(s, "invalid, duplicate or incomplete run inventory");
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_pending(MpState *s, const MpRun *run, int *pending)
{
    int valid = 1;
    *pending = !cm_equal(run->status, "completed");
    if (*pending)
    {
        char path[256];
        snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/attempts/%" PRIu64 "/jobs", run->id, run->attempt);
        uint64_t expected = 0, seen = 0, ids[MP_PAGES * 100];
        int complete = 0, found = 0, unfinished = 0;
        for (unsigned page = 1; valid && !complete && page <= MP_PAGES; ++page)
        {
            CmJson j = mp_request(s, path, NULL, page, 1);
            ++s->pages;
            unsigned array = 0, count = 0;
            valid = mp_batch(s, &j, "jobs", page, &expected, &array, &count);
            for (unsigned node = valid ? j.tokens[array].child : 0; valid && node; node = j.tokens[node].next)
            {
                uint64_t id = 0, owner = 0, attempt = 0;
                valid = mp_uint(&j, node, "id", &id) && id && mp_string(&j, node, "name") &&
                    mp_string(&j, node, "status") &&
                    (mp_active(cm_get(&j, node, "status")) || cm_equal(cm_get(&j, node, "status"), "completed")) &&
                    mp_uint(&j, node, "run_id", &owner) && owner == run->id &&
                    mp_uint(&j, node, "run_attempt", &attempt) && attempt <= run->attempt && attempt > 0;
                for (unsigned i = 0; valid && i < seen; ++i) valid = ids[i] != id;
                if (valid) ids[seen++] = id;
                if (valid && cm_equal(cm_get(&j, node, "name"), s->target))
                {
                    ++found;
                    // Inherited jobs from a previous attempt cannot prove that
                    // the current attempt's not-yet-materialized job is finished.
                    unfinished |= attempt != run->attempt || !cm_equal(cm_get(&j, node, "status"), "completed");
                }
            }
            complete = valid && seen == expected;
            cm_json_free(&j);
        }
        valid = valid && complete;
        *pending = !found || unfinished;
        if (valid)
        {
            snprintf(path, sizeof(path), "actions/runs/%" PRIu64, run->id);
            CmJson j = mp_request(s, path, NULL, 0, 0);
            MpRun live = {0};
            valid = mp_run(&j, 1, &live) && live.id == run->id && live.number == run->number &&
                cm_equal(live.head, run->head) && cm_equal(cm_get(&j, 1, "event"), "push") &&
                cm_equal(cm_get(&j, 1, "head_branch"), "main");
            if (valid) *pending = !cm_equal(live.status, "completed") && (*pending || live.attempt != run->attempt);
            cm_json_free(&j);
        }
    }
    if (!valid) mp_fail(s, "invalid or incomplete predecessor job/attempt inventory");
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_discover(MpState *s)
{
    s->phase = s->successor ? "successor-discovery" : "predecessor-discovery";
    int valid = mp_runs(s);
    if (valid && !s->successor)
    {
        unsigned kept = 0;
        for (unsigned i = 0; valid && i < s->count; ++i)
        {
            int pending = 0;
            valid = mp_pending(s, &s->runs[i], &pending);
            if (valid && pending) s->runs[kept++] = s->runs[i];
        }
        s->count = kept;
    }
    // Small bounded inventory, stable order without callback dispatch.
    for (unsigned i = 1; valid && i < s->count; ++i)
    {
        MpRun value = s->runs[i];
        unsigned at = i;
        while (at && (s->runs[at - 1].number > value.number ||
            (s->runs[at - 1].number == value.number && s->runs[at - 1].id > value.id)))
        {
            s->runs[at] = s->runs[at - 1]; --at;
        }
        s->runs[at] = value;
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_self_test(void)
{
    int valid = 1;
    const unsigned histories[] = {999, 1000, 1201};
    for (unsigned h = 0; h < BUSTER_ARRAY_LENGTH(histories); ++h)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .number = 10, .fixture = 1, .history = histories[h]};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs)); valid &= s.runs != NULL;
        if (s.runs)
        {
            valid &= mp_discover(&s) && s.count == 0 && s.requests == 5;
            free(s.runs);
        }
    }
    for (unsigned state = 0; state < BUSTER_ARRAY_LENGTH(mp_states); ++state)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .number = 10, .fixture = 1,
            .history = 1201, .active = 1, .active_status = mp_states[state]};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs)); valid &= s.runs != NULL;
        if (s.runs) { valid &= mp_discover(&s) && s.count == 1; free(s.runs); }
    }
    for (unsigned active = 999; active <= 1000; ++active)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .number = 1, .fixture = 1, .active = active};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs)); valid &= s.runs != NULL;
        if (s.runs) { valid &= mp_discover(&s) && s.count == 0 && s.pages == 14; free(s.runs); }
    }
    const char *bad_pages[] = {
        "{}", "{\"total_count\":1,\"workflow_runs\":[]}",
        "{\"total_count\":\"0\",\"workflow_runs\":[]}",
        "{\"total_count\":0,\"workflow_runs\":{}}",
        "{\"total_count\":0,\"workflow_runs\":[{}]}",
        "{\"total_count\":1001,\"workflow_runs\":[]}", "{"
    };
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(bad_pages); ++i)
    {
        CmJson j = cm_json_parse(bad_pages[i], strlen(bad_pages[i]));
        MpState s = {0}; uint64_t expected = 0; unsigned array = 0, count = 0;
        valid &= !mp_batch(&s, &j, "workflow_runs", 1, &expected, &array, &count);
        cm_json_free(&j);
    }
    for (int scenario = 0; scenario <= 8; ++scenario)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .number = 10, .fixture = 1,
            .history = 1201, .active = 1, .scenario = scenario};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs)); valid &= s.runs != NULL;
        if (s.runs)
        {
            int ok = mp_discover(&s);
            unsigned expected = scenario == 3 || scenario == 6 || scenario >= 7 ? 0 : 1;
            valid &= ok && s.count == expected;
            free(s.runs);
        }
    }
    for (int scenario = 0; scenario <= 8; ++scenario)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .head = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            .number = 10, .successor = 1, .fixture = 1, .history = 1201, .scenario = scenario};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs)); valid &= s.runs != NULL;
        if (s.runs) { valid &= mp_discover(&s) && s.count == (scenario >= 7 ? 0u : 1u); free(s.runs); }
    }
    MpState incomplete = {.workflow = "test.yml", .number = 10, .target = "target", .fixture = 1, .active = 1001};
    incomplete.runs = calloc(MP_ROWS, sizeof(*incomplete.runs)); valid &= incomplete.runs != NULL;
    if (incomplete.runs) { valid &= !mp_discover(&incomplete) && incomplete.error; free(incomplete.runs); }
    printf("MAIN_PUSH_INVENTORY_SELF_TEST %s\n", valid ? "PASS" : "FAIL");
    return valid ? 0 : 1;
}
int main(int argc, char **argv)
{
    int result = 2;
    if (argc == 2 && cm_equal(argv[1], "--self-test")) result = mp_self_test();
    else
    {
        MpState s = {0};
        int valid = argc == 5 && (cm_equal(argv[1], "predecessors") || cm_equal(argv[1], "successor"));
        if (valid)
        {
            s.successor = cm_equal(argv[1], "successor"); s.workflow = argv[2];
            valid = mp_name(s.workflow) && cm_unsigned(argv[3], &s.number) && s.number > 0;
            if (s.successor) { s.head = argv[4]; valid &= cm_sha(s.head); }
            else { s.target = argv[4]; valid &= s.target[0] && strlen(s.target) <= CM_FIELD; }
        }
        s.runs = valid ? calloc(MP_ROWS, sizeof(*s.runs)) : NULL;
        valid = valid && s.runs && mp_discover(&s);
        if (!valid && !s.error) mp_fail(&s, "invalid native discovery invocation");
        fputs("{\"result\":", stdout);
        if (!valid) fputs("null", stdout);
        else if (s.successor && !s.count) fputs("null", stdout);
        else
        {
            if (!s.successor) fputc('[', stdout);
            unsigned n = s.successor ? 1 : s.count;
            for (unsigned i = 0; i < n; ++i)
            {
                if (i) fputc(',', stdout);
                MpRun *run = &s.runs[i];
                fprintf(stdout, "{\"id\":%" PRIu64 ",\"run_number\":%" PRIu64 ",\"head_sha\":", run->id, run->number);
                cm_quote(stdout, run->head);
                if (s.successor) { fputs(",\"status\":", stdout); cm_quote(stdout, run->status); }
                fputc('}', stdout);
            }
            if (!s.successor) fputc(']', stdout);
        }
        fprintf(stdout, ",\"inventory_complete\":%s,\"requests\":%u,\"pages\":%u,\"phase\":",
            valid ? "true" : "false", s.requests, s.pages);
        cm_quote(stdout, s.phase ? s.phase : "configuration");
        if (!valid) { fputs(",\"error\":", stdout); cm_quote(stdout, s.error); }
        fputs("}\n", stdout);
        free(s.runs);
        result = valid ? 0 : 2;
    }
    return result;
}
