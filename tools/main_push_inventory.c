// Bounded Actions discovery for #3412/#4019. mp_runs owns complete active-state or
// exact-head inventories; mp_pending owns current-attempt jobs and rechecks.
// JSON parsing is shared with the native CI collector. API reads use a narrow
// stdin/stdout protocol; the legacy adapter owns HTTP only, never discovery.
// mp_discover owns retry policy, conservative unions and opposite-order sweeps.
// mp_reject/mp_diagnostic own safe predicate evidence; the adapter only transports.
// mp_self_test uses synthetic API responses and the production discovery path.
#include "ci_metrics_json.h"
#define MP_PAGES 10u
#define MP_ROWS 5000u
#define MP_REQUESTS 200u
#define MP_ATTEMPTS 4u
typedef struct MpRun MpRun;
struct MpRun
{
    uint64_t id, number, attempt;
    char head[41], status[24];
    int pending;
    unsigned seen;
};
typedef struct MpDiagnostic MpDiagnostic;
struct MpDiagnostic
{
    const char *reason, *field;
    char path[256], filter[128];
    uint64_t expected, total;
    unsigned page, rows, row, request, attempt;
    int has_expected, has_total, has_rows, retryable;
};
typedef struct MpState MpState;
struct MpState
{
    const char *workflow, *target, *head, *error, *phase, *active_status;
    uint64_t number;
    unsigned requests, pages, count;
    MpRun *runs;
    int successor, fixture, scenario;
    unsigned history, active, attempt, sweep, diagnostic_count;
    int retryable;
    MpDiagnostic query, diagnostics[MP_ATTEMPTS];
};
BUSTER_GLOBAL_LOCAL const char *mp_states[] = {"queued", "in_progress", "waiting", "pending", "requested"};
BUSTER_GLOBAL_LOCAL int mp_active(const char *status)
{
    int result = 0;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(mp_states); ++i) result |= cm_equal(status, mp_states[i]);
    return result;
}
BUSTER_GLOBAL_LOCAL int mp_reject(MpState *s, const char *reason, const char *field, int retryable)
{
    if (!s->error || (s->retryable && !retryable))
    {
        s->error = reason; s->retryable = retryable;
        s->query.reason = reason; s->query.field = field; s->query.retryable = retryable;
    }
    return 0;
}
BUSTER_GLOBAL_LOCAL int mp_fail(MpState *s, const char *message)
{
    return mp_reject(s, message, NULL, 0);
}
BUSTER_GLOBAL_LOCAL void mp_diagnostic(FILE *f, const MpDiagnostic *d)
{
    fputs("{\"reason\":", f); cm_quote(f, d->reason);
    fputs(",\"field\":", f); if (d->field) cm_quote(f, d->field); else fputs("null", f);
    fputs(",\"path\":", f); cm_quote(f, d->path);
    fputs(",\"filter\":", f); if (d->filter[0]) cm_quote(f, d->filter); else fputs("null", f);
    fputs(",\"page\":", f); if (d->page) fprintf(f, "%u", d->page); else fputs("null", f);
    fputs(",\"per_page\":", f); fputs(d->page ? "100" : "null", f);
    fprintf(f, ",\"request\":%u,\"attempt\":%u,\"row\":", d->request, d->attempt);
    if (d->row) fprintf(f, "%u", d->row); else fputs("null", f);
    fprintf(f, ",\"retryable\":%s", d->retryable ? "true" : "false");
    fputs(",\"expected_total\":", f); if (d->has_expected) fprintf(f, "%" PRIu64, d->expected); else fputs("null", f);
    fputs(",\"observed_total\":", f); if (d->has_total) fprintf(f, "%" PRIu64, d->total); else fputs("null", f);
    fputs(",\"rows\":", f); if (d->has_rows) fprintf(f, "%u", d->rows); else fputs("null", f);
    fputc('}', f);
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
BUSTER_GLOBAL_LOCAL int mp_field(MpState *s, const CmJson *j, unsigned node, const char *key, char kind)
{
    unsigned at = cm_member(j, node, key);
    int valid = at && j->tokens[at].kind == kind;
    if (!valid) mp_reject(s, at ? "wrong-field-type" : "missing-field", key, 0);
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_identity(MpState *s, const CmJson *j, unsigned node, const char *key, uint64_t *out)
{
    int valid = mp_field(s, j, node, key, 'v');
    if (valid && (!mp_uint(j, node, key, out) || !*out)) valid = mp_reject(s, "invalid-identity", key, 0);
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_run_checked(MpState *s, const CmJson *j, unsigned node, MpRun *out)
{
    int valid = j->valid && node && node <= j->count && j->tokens[node].kind == 'o';
    if (!valid) mp_reject(s, "invalid-row", "workflow_runs", 0);
    valid = valid && mp_identity(s, j, node, "id", &out->id) &&
        mp_identity(s, j, node, "run_number", &out->number) &&
        mp_identity(s, j, node, "run_attempt", &out->attempt) &&
        mp_field(s, j, node, "head_sha", 's') && mp_field(s, j, node, "event", 's') &&
        mp_field(s, j, node, "head_branch", 's') && mp_field(s, j, node, "status", 's');
    if (valid && !cm_sha(cm_get(j, node, "head_sha"))) valid = mp_reject(s, "invalid-identity", "head_sha", 0);
    if (valid && !mp_active(cm_get(j, node, "status")) && !cm_equal(cm_get(j, node, "status"), "completed"))
        valid = mp_reject(s, "invalid-status", "status", 0);
    if (valid)
    {
        cm_copy(out->head, sizeof(out->head), cm_get(j, node, "head_sha"));
        cm_copy(out->status, sizeof(out->status), cm_get(j, node, "status"));
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_run(const CmJson *j, unsigned node, MpRun *out)
{
    MpState s = {0};
    return mp_run_checked(&s, j, node, out);
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
            if (s->scenario == 20) total = !s->successor && (s->sweep == 1 || s->attempt > 1) && cm_equal(filter, "queued") ? 1 : 0;
            if (s->scenario == 21) total = cm_equal(filter, "queued") || cm_equal(filter, "in_progress") ? 1 : 0;
            if (s->scenario == 23 && s->attempt > 1) total = 0;
            if (s->scenario == 29) fputs("{malformed", f);
            unsigned reported = total;
            if ((s->scenario == 15 && s->attempt == 1 && (s->successor || cm_equal(filter, "in_progress"))) ||
                (s->scenario == 16 && (s->successor || cm_equal(filter, "queued")))) ++reported;
            if (s->scenario == 17 && s->attempt == 1 && page == 2) ++reported;
            if (s->scenario == 23 && s->attempt == 1 && cm_equal(filter, "queued")) ++reported;
            fprintf(f, "{\"total_count\":%u,\"workflow_runs\":[", reported);
            unsigned start = (page - 1) * 100, end = total < start + 100 ? total : start + 100;
            for (unsigned i = start; i < end; ++i)
            {
                if (i > start) fputc(',', f);
                unsigned shift = s->scenario == 18 && (s->sweep || s->attempt > 1);
                mp_emit_run(f, s->scenario == 14 || (s->scenario == 19 && page == 2) ? 100 : 100 + i + shift, s->successor ? (s->scenario == 10 ? 10 : 11) : 1 + i + shift, s->scenario == 21 && cm_equal(filter, "queued") ? 3 : 2,
                    (s->successor && s->scenario != 9) || (s->scenario == 31 && s->sweep) ? b : a,
                    s->scenario == 11 ? "unknown" : s->scenario == 32 ? "completed" : filter && mp_active(filter) ? filter : "completed",
                    s->scenario == 7 ? "workflow_dispatch" : "push", s->scenario == 8 ? "other" : "main");
            }
            fputs("]}", f);
        }
        else if (strstr(path, "/jobs"))
        {
            unsigned count = s->scenario == 2 ? 0 : s->scenario == 25 ? 2 : 1;
            unsigned reported = count + (s->scenario == 22 && s->attempt == 1);
            fprintf(f, "{\"total_count\":%u,\"jobs\":[", reported);
            for (unsigned i = 0; i < count; ++i)
            {
                if (i) fputc(',', f);
                fprintf(f, "{\"id\":200,\"name\":\"target\",\"status\":\"%s\",\"run_id\":%u,\"run_attempt\":%d}",
                    s->scenario == 3 || s->scenario == 4 ? "completed" : "in_progress", s->scenario == 26 ? 999 : 100,
                    s->scenario == 4 ? 1 : s->scenario == 21 ? 3 : 2);
            }
            fputs("]}", f);
        }
        else
        {
            mp_emit_run(f, 100, 1, s->scenario == 5 || s->scenario == 21 || s->scenario == 32 ? 3 : 2, s->scenario == 27 ? b : a,
                s->scenario == 6 ? "completed" : "in_progress", "push", "main");
        }
        result = mp_file(f); fclose(f);
    }
    return result;
}
BUSTER_GLOBAL_LOCAL CmJson mp_request(MpState *s, const char *path, const char *filter, unsigned page, int jobs)
{
    char *text = NULL;
    s->query = (MpDiagnostic){.page = page, .request = s->requests + 1, .attempt = s->attempt};
    cm_copy(s->query.path, sizeof(s->query.path), path);
    cm_copy(s->query.filter, sizeof(s->query.filter), filter);
    int allowed = s->requests < MP_REQUESTS;
    int valid = allowed;
    if (valid)
    {
        ++s->requests;
        s->pages += page > 0;
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
    if (!result.valid) mp_fail(s, !allowed ? "request-bound" : valid ? "malformed-json-or-parser-bound" : "response-bound-or-transport-eof");
    return result;
}
BUSTER_GLOBAL_LOCAL int mp_batch(MpState *s, const CmJson *j, const char *key, unsigned page,
    uint64_t *expected, unsigned *array, unsigned *count)
{
    uint64_t total = 0;
    *array = cm_member(j, 1, key); *count = 0;
    s->query.has_expected = page > 1; s->query.expected = *expected;
    int valid = j->valid;
    if (!valid) mp_fail(s, "malformed-json-or-parser-bound");
    if (valid) valid = mp_field(s, j, 1, "total_count", 'v');
    if (valid && !mp_uint(j, 1, "total_count", &total)) valid = mp_reject(s, "invalid-total", "total_count", 0);
    s->query.has_total = valid; s->query.total = total;
    if (valid) valid = mp_field(s, j, 1, key, 'a');
    s->query.has_rows = j->valid && *array && j->tokens[*array].kind == 'a';
    for (unsigned row = s->query.has_rows ? j->tokens[*array].child : 0; row; row = j->tokens[row].next)
    {
        ++*count;
        if (valid && j->tokens[row].kind != 'o')
        { s->query.row = *count; valid = mp_reject(s, "invalid-row", key, 0); }
    }
    s->query.rows = *count;
    if (valid && total > MP_PAGES * 100) valid = mp_reject(s, "inventory-bound", "total_count", 0);
    if (valid && *count > 100) valid = mp_reject(s, "page-row-bound", key, 0);
    if (page == 1 && s->query.has_total)
    {
        *expected = total; s->query.expected = total; s->query.has_expected = 1;
    }
    unsigned offset = (page - 1) * 100;
    uint64_t remaining = total >= offset ? total - offset : 0;
    if (valid && total != *expected) valid = mp_reject(s, "changed-total", "total_count", 1);
    if (valid && (total < offset || *count != (remaining < 100 ? remaining : 100)))
        valid = mp_reject(s, "count-list-mismatch", key, 1);
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_remember(MpState *s, const MpRun *run)
{
    unsigned at = s->count;
    for (unsigned i = 0; i < s->count; ++i) if (s->runs[i].id == run->id) at = i;
    int valid = 1;
    unsigned seen = at < s->count ? s->runs[at].seen : 0;
    for (unsigned i = 0; valid && i < s->count; ++i)
        if (i != at && s->runs[i].number == run->number) valid = mp_reject(s, "duplicate-run-number", "run_number", 0);
    if (valid && at < s->count)
    {
        valid = s->runs[at].number == run->number && cm_equal(s->runs[at].head, run->head);
        if (!valid) mp_reject(s, "changed-run-identity", "id/run_number/head_sha", 0);
        if (valid && run->attempt >= s->runs[at].attempt) s->runs[at] = *run;
    }
    else if (valid)
    {
        valid = s->count < MP_ROWS;
        if (valid) s->runs[s->count++] = *run;
        else mp_fail(s, "retained-run-bound");
    }
    if (valid) s->runs[at].seen = seen | (1u << s->sweep);
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_runs(MpState *s)
{
    char path[256]; snprintf(path, sizeof(path), "actions/workflows/%s/runs", s->workflow);
    int valid = 1;
    unsigned buckets = s->successor ? 1 : BUSTER_ARRAY_LENGTH(mp_states);
    for (unsigned step = 0; valid && step < buckets; ++step)
    {
        unsigned bucket = s->sweep & 1 ? buckets - step - 1 : step;
        uint64_t expected = 0, ids[MP_PAGES * 100];
        unsigned seen = 0;
        int complete = 0;
        for (unsigned page = 1; valid && !complete && page <= MP_PAGES; ++page)
        {
            CmJson j = mp_request(s, path, s->successor ? s->head : mp_states[bucket], page, 0);
            unsigned array = 0, count = 0;
            valid = mp_batch(s, &j, "workflow_runs", page, &expected, &array, &count);
            // A rejected count is never a complete page. Preserve its valid
            // identities for direct rechecking even if they disappear on retry.
            int rows_valid = valid || s->retryable;
            for (unsigned node = rows_valid ? j.tokens[array].child : 0; rows_valid && node; node = j.tokens[node].next)
            {
                ++s->query.row;
                MpRun run = {0};
                rows_valid = mp_run_checked(s, &j, node, &run);
                for (unsigned i = 0; rows_valid && i < seen; ++i)
                    if (ids[i] == run.id) rows_valid = mp_reject(s, "duplicate-run-id", "id", 0);
                if (rows_valid) ids[seen++] = run.id;
                int relevant = cm_equal(cm_get(&j, node, "event"), "push") &&
                    cm_equal(cm_get(&j, node, "head_branch"), "main") &&
                    (s->successor ? run.number > s->number && cm_equal(run.head, s->head) : run.number < s->number);
                unsigned wf = cm_member(&j, node, "path");
                if (wf)
                {
                    char wanted[256]; snprintf(wanted, sizeof(wanted), ".github/workflows/%s", s->workflow);
                    rows_valid = rows_valid && mp_field(s, &j, node, "path", 's');
                    relevant &= cm_equal(cm_value(&j, wf), wanted);
                }
                if (rows_valid && relevant) rows_valid = mp_remember(s, &run);
            }
            if (s->retryable) s->query.row = 0;
            valid = valid && rows_valid;
            complete = valid && seen == expected;
            cm_json_free(&j);
        }
        valid = valid && complete;
    }
    if (!valid && !s->error) mp_fail(s, "page-bound");
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_pending(MpState *s, MpRun *run, int *pending)
{
    int valid = 1;
    *pending = !cm_equal(run->status, "completed");
    char path[256];
    if (*pending)
    {
        snprintf(path, sizeof(path), "actions/runs/%" PRIu64 "/attempts/%" PRIu64 "/jobs", run->id, run->attempt);
        uint64_t expected = 0, seen = 0, ids[MP_PAGES * 100];
        int complete = 0, found = 0, unfinished = 0;
        for (unsigned page = 1; valid && !complete && page <= MP_PAGES; ++page)
        {
            CmJson j = mp_request(s, path, NULL, page, 1);
            unsigned array = 0, count = 0;
            valid = mp_batch(s, &j, "jobs", page, &expected, &array, &count);
            for (unsigned node = valid ? j.tokens[array].child : 0; valid && node; node = j.tokens[node].next)
            {
                ++s->query.row;
                uint64_t id = 0, owner = 0, attempt = 0;
                valid = mp_identity(s, &j, node, "id", &id) && mp_field(s, &j, node, "name", 's') &&
                    mp_field(s, &j, node, "status", 's') && mp_identity(s, &j, node, "run_id", &owner) &&
                    mp_identity(s, &j, node, "run_attempt", &attempt);
                if (valid && !mp_active(cm_get(&j, node, "status")) && !cm_equal(cm_get(&j, node, "status"), "completed"))
                    valid = mp_reject(s, "invalid-status", "status", 0);
                if (valid && (owner != run->id || attempt > run->attempt))
                    valid = mp_reject(s, "invalid-job-identity", "run_id/run_attempt", 0);
                for (unsigned i = 0; valid && i < seen; ++i)
                    if (ids[i] == id) valid = mp_reject(s, "duplicate-job-id", "id", 0);
                if (valid) ids[seen++] = id;
                if (valid && cm_equal(cm_get(&j, node, "name"), s->target))
                {
                    ++found;
                    unfinished |= attempt != run->attempt || !cm_equal(cm_get(&j, node, "status"), "completed");
                }
            }
            complete = valid && seen == expected;
            cm_json_free(&j);
        }
        valid = valid && complete;
        *pending = !found || unfinished;
    }
    if (valid)
    {
        snprintf(path, sizeof(path), "actions/runs/%" PRIu64, run->id);
        CmJson j = mp_request(s, path, NULL, 0, 0);
        MpRun live = {0};
        valid = mp_run_checked(s, &j, 1, &live);
        if (valid && (live.id != run->id || live.number != run->number || live.attempt < run->attempt ||
            !cm_equal(live.head, run->head) || !cm_equal(cm_get(&j, 1, "event"), "push") ||
            !cm_equal(cm_get(&j, 1, "head_branch"), "main")))
            valid = mp_reject(s, "changed-run-identity", "live-run", 0);
        if (valid)
        {
            int keep = !cm_equal(live.status, "completed") &&
                (*pending || live.attempt != run->attempt || cm_equal(run->status, "completed"));
            *run = live; *pending = keep;
        }
        cm_json_free(&j);
    }
    if (!valid && !s->error) mp_fail(s, "page-bound");
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_discover(MpState *s)
{
    s->phase = s->successor ? "successor-discovery" : "predecessor-discovery";
    int valid = 0, again = 1;
    for (s->attempt = 1; again && s->attempt <= MP_ATTEMPTS; ++s->attempt)
    {
        s->error = NULL; s->retryable = 0;
        for (unsigned i = 0; i < s->count; ++i) s->runs[i].seen = 0;
        valid = mp_runs(s);
        if (valid && !s->successor)
        {
            // Opposite-order coverage plus the retained union prevents observed
            // predecessors moving buckets/pages from being silently discarded.
            s->sweep = 1; valid = mp_runs(s); s->sweep = 0;
            for (unsigned i = 0; valid && i < s->count; ++i)
                if (s->runs[i].seen == 1 || s->runs[i].seen == 2)
                {
                    s->query = (MpDiagnostic){.request = s->requests, .attempt = s->attempt};
                    snprintf(s->query.path, sizeof(s->query.path), "actions/workflows/%s/runs", s->workflow);
                    valid = mp_reject(s, "changed-run-set", "run-identities", 1);
                }
            for (unsigned i = 0; valid && i < s->count; ++i)
                valid = mp_pending(s, &s->runs[i], &s->runs[i].pending);
        }
        again = !valid && s->retryable && s->attempt < MP_ATTEMPTS && s->requests < MP_REQUESTS;
        if (!valid)
        {
            s->diagnostics[s->diagnostic_count++] = s->query;
            if (!s->fixture)
            {
                fputs("{\"observation\":", stdout); mp_diagnostic(stdout, &s->query);
                fprintf(stdout, ",\"backoff_seconds\":%u}\n", again ? 1u << (s->attempt - 1) : 0);
                fflush(stdout);
            }
        }
    }
    --s->attempt;
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
BUSTER_GLOBAL_LOCAL int mp_seed_rows(MpState *s, const CmJson *j)
{
    int valid = j->valid && mp_field(s, j, 1, "retained", 'a');
    if (!valid) mp_fail(s, "invalid-retained-input");
    unsigned array = cm_member(j, 1, "retained");
    for (unsigned node = valid ? j->tokens[array].child : 0; valid && node; node = j->tokens[node].next)
    {
        MpRun run = {0};
        valid = mp_run_checked(s, j, node, &run);
        if (valid && (s->successor || run.number >= s->number ||
            !cm_equal(cm_get(j, node, "event"), "push") || !cm_equal(cm_get(j, node, "head_branch"), "main")))
            valid = mp_fail(s, "invalid-retained-identity");
        unsigned before = s->count;
        if (valid) valid = mp_remember(s, &run);
        if (valid && s->count == before) valid = mp_fail(s, "duplicate-retained-id");
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int mp_seed(MpState *s)
{
    char *text = malloc(CM_BYTES + 1);
    size_t n = 0;
    int c = 0, valid = text != NULL;
    while (valid && n < CM_BYTES && (c = getchar()) != EOF && c != '\n') text[n++] = (char)c;
    if (text) text[n] = 0;
    CmJson j = cm_json_parse(text ? text : "", n);
    valid = valid && c == '\n' && n < CM_BYTES && mp_seed_rows(s, &j);
    if (!valid) mp_fail(s, "invalid-retained-input");
    cm_json_free(&j); free(text);
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
            valid &= mp_discover(&s) && s.count == 0 && s.requests == 10;
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
        if (s.runs) { valid &= mp_discover(&s) && s.count == 0 && s.pages == 28; free(s.runs); }
    }
    const char *bad_pages[] = {
        "{}", "{\"total_count\":1,\"workflow_runs\":[]}",
        "{\"total_count\":\"0\",\"workflow_runs\":[]}",
        "{\"total_count\":0,\"workflow_runs\":{}}",
        "{\"total_count\":0,\"workflow_runs\":[{}]}",
        "{\"total_count\":1001,\"workflow_runs\":[]}", "{"
    };
    const char *bad_reasons[] = {"missing-field", "count-list-mismatch", "wrong-field-type",
        "wrong-field-type", "count-list-mismatch", "inventory-bound", "malformed-json-or-parser-bound"};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(bad_pages); ++i)
    {
        CmJson j = cm_json_parse(bad_pages[i], strlen(bad_pages[i]));
        MpState s = {0}; uint64_t expected = 0; unsigned array = 0, count = 0;
        valid &= !mp_batch(&s, &j, "workflow_runs", 1, &expected, &array, &count) && cm_equal(s.error, bad_reasons[i]);
        if (i == 1) valid &= s.query.has_total && s.query.has_rows && s.query.rows == 0 && s.query.total == 1;
        if (i == 2) valid &= !s.query.has_total && s.query.has_rows && s.query.rows == 0;
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
            unsigned expected = scenario >= 7 ? 0 : 1;
            valid &= ok && s.count == expected;
            if (ok && s.count) valid &= s.runs[0].pending == !(scenario == 3 || scenario == 6);
            free(s.runs);
        }
    }
    for (int scenario = 0; scenario <= 11; ++scenario)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .head = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            .number = 10, .successor = 1, .fixture = 1, .history = 1201, .scenario = scenario};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs)); valid &= s.runs != NULL;
        if (s.runs)
        {
            int ok = mp_discover(&s);
            valid &= scenario == 11 ? !ok : ok && s.count == (scenario >= 7 ? 0u : 1u);
            free(s.runs);
        }
    }
    const char *bad_runs[] = {
        "{}", "{\"id\":1,\"run_number\":2,\"run_attempt\":0}",
        "{\"id\":\"1\",\"run_number\":2,\"run_attempt\":1}",
        "{\"id\":1,\"id\":2}", "[]"
    };
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(bad_runs); ++i)
    {
        CmJson j = cm_json_parse(bad_runs[i], strlen(bad_runs[i])); MpRun run = {0};
        valid &= !mp_run(&j, 1, &run); cm_json_free(&j);
    }
    MpState duplicate = {.workflow = "test.yml", .number = 10, .target = "target",
        .fixture = 1, .active = 2, .scenario = 14};
    duplicate.runs = calloc(MP_ROWS, sizeof(*duplicate.runs)); valid &= duplicate.runs != NULL;
    if (duplicate.runs) { valid &= !mp_discover(&duplicate) && duplicate.error; free(duplicate.runs); }
    MpState exhausted = {.workflow = "test.yml", .number = 10, .target = "target",
        .fixture = 1, .requests = MP_REQUESTS};
    exhausted.runs = calloc(MP_ROWS, sizeof(*exhausted.runs)); valid &= exhausted.runs != NULL;
    if (exhausted.runs) { valid &= !mp_discover(&exhausted) && exhausted.error; free(exhausted.runs); }
    MpState incomplete = {.workflow = "test.yml", .number = 10, .target = "target", .fixture = 1, .active = 1001};
    incomplete.runs = calloc(MP_ROWS, sizeof(*incomplete.runs)); valid &= incomplete.runs != NULL;
    if (incomplete.runs) { valid &= !mp_discover(&incomplete) && incomplete.error; free(incomplete.runs); }
    MpState first_rejection = {.workflow = "test.yml", .target = "target", .number = 2, .fixture = 1,
        .scenario = 15, .active = 1, .attempt = 1};
    first_rejection.runs = calloc(MP_ROWS, sizeof(*first_rejection.runs));
    valid &= first_rejection.runs && !mp_runs(&first_rejection) && first_rejection.requests == 2 &&
        cm_equal(first_rejection.error, "count-list-mismatch") && first_rejection.query.page == 1;
    free(first_rejection.runs);
    // #4019 synthetic controls do not identify the original incident's
    // unresolved predicate; the original response was not retained.
    struct { int scenario; unsigned active; const char *error; } cases[] = {
        {15, 1, NULL}, {16, 0, "count-list-mismatch"}, {17, 101, NULL},
        {18, 101, NULL}, {19, 101, "duplicate-run-id"}, {20, 1, NULL}, {21, 1, NULL},
        {22, 1, NULL}, {23, 1, NULL}, {25, 1, "duplicate-job-id"},
        {26, 1, "invalid-job-identity"}, {27, 1, "changed-run-identity"},
        {29, 1, "malformed-json-or-parser-bound"}, {31, 1, "changed-run-identity"}, {32, 1, NULL}
    };
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(cases); ++i)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .number = 2, .fixture = 1,
            .scenario = cases[i].scenario, .active = cases[i].active};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs));
        int ok = s.runs && mp_discover(&s);
        int check = cases[i].error ? !ok && cm_equal(s.error, cases[i].error) : ok && s.count == 1 && s.runs[0].pending;
        if (s.scenario == 15 || s.scenario == 17 || s.scenario == 18 || s.scenario == 20 || s.scenario == 22 || s.scenario == 23)
            check &= s.attempt == 2 && s.diagnostic_count == 1 && s.diagnostics[0].retryable;
        if (s.scenario == 15) check &= s.diagnostics[0].page == 1 && s.diagnostics[0].request == 2 &&
            s.diagnostics[0].total == 1 && s.diagnostics[0].rows == 0;
        if (s.scenario == 16) check &= s.attempt == MP_ATTEMPTS && s.diagnostic_count == MP_ATTEMPTS;
        if (s.scenario == 21 || s.scenario == 32) check &= s.runs[0].attempt == 3;
        if (!check) fprintf(stderr, "#4019 scenario %d failed (%s)\n", s.scenario, s.error ? s.error : "success");
        valid &= check;
        free(s.runs);
    }
    const char *seed = "{\"retained\":[{\"id\":100,\"run_number\":1,\"run_attempt\":2,"
        "\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"in_progress\","
        "\"event\":\"push\",\"head_branch\":\"main\"}]}";
    for (unsigned used = 0; used <= MP_REQUESTS; used += MP_REQUESTS)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .number = 2, .fixture = 1, .requests = used};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs));
        CmJson j = cm_json_parse(seed, strlen(seed));
        int ok = s.runs && mp_seed_rows(&s, &j) && mp_discover(&s);
        valid &= used ? !ok && s.requests == MP_REQUESTS && cm_equal(s.error, "request-bound") :
            ok && s.count == 1 && s.runs[0].pending;
        cm_json_free(&j); free(s.runs);
    }
    MpState recovered_successor = {.workflow = "test.yml", .target = "target", .number = 10,
        .fixture = 1, .successor = 1, .head = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", .scenario = 15};
    recovered_successor.runs = calloc(MP_ROWS, sizeof(*recovered_successor.runs));
    valid &= recovered_successor.runs && mp_discover(&recovered_successor) && recovered_successor.count == 1 &&
        recovered_successor.attempt == 2 && recovered_successor.diagnostic_count == 1;
    free(recovered_successor.runs);
    for (int successor = 0; successor <= 1; ++successor)
    {
        MpState s = {.workflow = "test.yml", .target = "target", .number = 10, .fixture = 1,
            .successor = successor, .head = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", .requests = MP_REQUESTS - 1,
            .scenario = 16};
        s.runs = calloc(MP_ROWS, sizeof(*s.runs));
        int ok = s.runs && mp_discover(&s);
        valid &= !ok && s.requests == MP_REQUESTS && s.attempt == 1;
        free(s.runs);
    }
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
        int valid = (argc == 5 || argc == 6) && (cm_equal(argv[1], "predecessors") || cm_equal(argv[1], "successor"));
        if (valid)
        {
            s.successor = cm_equal(argv[1], "successor"); s.workflow = argv[2];
            valid = mp_name(s.workflow) && cm_unsigned(argv[3], &s.number) && s.number > 0;
            if (s.successor) { s.head = argv[4]; valid &= cm_sha(s.head); }
            else { s.target = argv[4]; valid &= s.target[0] && strlen(s.target) <= CM_FIELD; }
        }
        s.runs = valid ? calloc(MP_ROWS, sizeof(*s.runs)) : NULL;
        valid = valid && s.runs;
        if (valid && argc == 6)
        {
            uint64_t used = 0;
            valid = cm_unsigned(argv[5], &used) && used <= MP_REQUESTS;
            if (valid) { s.requests = (unsigned)used; valid = mp_seed(&s); }
        }
        if (valid) valid = mp_discover(&s);
        if (!valid && !s.error) mp_fail(&s, "invalid native discovery invocation");
        fputs("{\"result\":", stdout);
        if (!valid) fputs("null", stdout);
        else if (s.successor && !s.count) fputs("null", stdout);
        else
        {
            if (!s.successor) fputc('[', stdout);
            unsigned emitted = 0;
            unsigned n = s.successor ? 1 : s.count;
            for (unsigned i = 0; i < n; ++i)
            {
                MpRun *run = &s.runs[i];
                if (!s.successor && !run->pending) continue;
                if (emitted++) fputc(',', stdout);
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
        fprintf(stdout, ",\"attempts\":%u,\"request_limit\":%u,\"attempt_limit\":%u,\"rejections\":[",
            s.attempt, MP_REQUESTS, MP_ATTEMPTS);
        for (unsigned i = 0; i < s.diagnostic_count; ++i)
        { if (i) fputc(',', stdout); mp_diagnostic(stdout, &s.diagnostics[i]); }
        fputs("],\"recovery_stop\":", stdout);
        if (valid) fputs("null", stdout);
        else cm_quote(stdout, s.retryable ? (s.requests >= MP_REQUESTS ? "request-bound" : "attempt-bound") : "terminal-rejection");
        fputs(",\"retained\":[", stdout);
        if (!s.successor)
            for (unsigned i = 0; i < s.count; ++i)
            {
                if (i) fputc(',', stdout);
                MpRun *run = &s.runs[i];
                mp_emit_run(stdout, run->id, run->number, run->attempt, run->head, run->status, "push", "main");
            }
        fputc(']', stdout);
        if (!valid) { fputs(",\"error\":", stdout); cm_quote(stdout, s.error); }
        fputs("}\n", stdout);
        free(s.runs);
        result = valid ? 0 : 2;
    }
    return result;
}
