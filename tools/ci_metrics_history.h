// Durable bounded shards and trusted publisher staging.
// Each raw record is immutable; only append is accepted for an existing shard.
// Daily indexes are bounded counters. Old shards remain on the data branch;
// refresh reads thirty observation-date days, not the indefinitely growing tree.
#ifndef BUSTER_CI_METRICS_HISTORY_H
#define BUSTER_CI_METRICS_HISTORY_H
#include "ci_metrics_report.h"
BUSTER_GLOBAL_LOCAL int cm_day(char out[11], time_t instant)
{
    struct tm utc;
    int valid = gmtime_r(&instant, &utc) != NULL && strftime(out, 11, "%Y-%m-%d", &utc) == 10;
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_progress_parse(CmTransport *t, const char *text)
{
    CmJson j = cm_json_parse(text, strlen(text));
    const char *cursor = cm_get(&j, 1, "sweep_before");
    uint64_t page = cm_number(&j, 1, "sweep_page"), replace = cm_number(&j, 1, "replace");
    int valid = j.valid && cm_equal(cm_get(&j, 1, "schema"), "buster-ci-history-progress-v1") &&
        cm_time(cursor) >= 0 && page > 0 && page <= 10 && replace < CM_RUN_CACHE;
    unsigned receipts = cm_member(&j, 1, "receipts"), pending = cm_member(&j, 1, "pending");
    valid = valid && receipts && pending && j.tokens[receipts].kind == 'a' && j.tokens[pending].kind == 'a';
    t->run_count = 0; t->pending_count = 0;
    for (unsigned entry = valid ? j.tokens[receipts].child : 0; valid && entry; entry = j.tokens[entry].next)
    {
        uint64_t id = cm_number(&j, entry, "run_id"), attempt = cm_number(&j, entry, "attempt");
        const char *complete = cm_get(&j, entry, "complete");
        valid = id && attempt && attempt <= CM_MAX_ATTEMPTS && t->run_count < CM_RUN_CACHE &&
            (cm_equal(complete, "true") || cm_equal(complete, "false"));
        for (unsigned i = 0; valid && i < t->run_count; ++i) valid = t->run_cache[i].id != id;
        if (valid) t->run_cache[t->run_count++] = (CmRunReceipt){id, attempt, cm_equal(complete, "true")};
    }
    for (unsigned entry = valid ? j.tokens[pending].child : 0; valid && entry; entry = j.tokens[entry].next)
    {
        uint64_t id = 0;
        valid = cm_unsigned(cm_value(&j, entry), &id) && id && t->pending_count < CM_PENDING;
        for (unsigned i = 0; valid && i < t->pending_count; ++i) valid = t->pending[i] != id;
        if (valid) t->pending[t->pending_count++] = id;
    }
    if (valid)
    {
        cm_copy(t->sweep_before, sizeof(t->sweep_before), cursor);
        t->sweep_page = (unsigned)page; t->run_replace = (unsigned)replace;
    }
    cm_json_free(&j);
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_progress_output(CmTransport *t, CmOutputs *out, const char *observed)
{
    FILE *file = tmpfile();
    int result = file != NULL;
    if (file)
    {
        fputs("{\"schema\":\"buster-ci-history-progress-v1\",\"sweep_before\":", file);
        cm_quote(file, t->sweep_before[0] ? t->sweep_before : observed);
        fprintf(file, ",\"sweep_page\":%u,\"replace\":%u,\"receipts\":[", t->sweep_page ? t->sweep_page : 1, t->run_replace);
        for (unsigned i = 0; i < t->run_count; ++i)
        {
            if (i) fputc(',', file);
            fprintf(file, "{\"run_id\":%" PRIu64 ",\"attempt\":%" PRIu64 ",\"complete\":%s}",
                t->run_cache[i].id, t->run_cache[i].attempt, t->run_cache[i].complete ? "true" : "false");
        }
        fputs("],\"pending\":[", file);
        for (unsigned i = 0; i < t->pending_count; ++i) { if (i) fputc(',', file); fprintf(file, "%" PRIu64, t->pending[i]); }
        fputs("]}\n", file); result = cm_output(out, "history/progress.json", file);
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_history_load(CmTransport *t, CmStore *s, unsigned days)
{
    int valid = cm_data_head(t);
    int available = 0;
    char *manifest = valid ? cm_data_read(t, "manifest.json", &available) : NULL;
    if (t->data_exists)
    {
        CmJson j = cm_json_parse(manifest ? manifest : "", manifest ? strlen(manifest) : 0);
        valid = valid && available == 1 && j.valid &&
            cm_equal(cm_get(&j, 1, "schema"), "buster-ci-history-manifest-v1") && cm_time(cm_get(&j, 1, "watermark")) >= 0;
        cm_json_free(&j);
    }
    free(manifest);
    char *progress = valid ? cm_data_read(t, "history/progress.json", &available) : NULL;
    if (progress) valid = cm_progress_parse(t, progress);
    else if (available < 0) valid = 0;
    free(progress);
    time_t now = time(NULL);
    for (unsigned day = days; valid && day > 0; --day)
    {
        char date[11], path[128];
        valid = cm_day(date, now - (time_t)(day - 1) * 86400);
        snprintf(path, sizeof(path), "history/%s/index.json", date);
        char *index = valid ? cm_data_read(t, path, &available) : NULL;
        if (available < 0) valid = 0;
        if (index)
        {
            CmJson j = cm_json_parse(index, strlen(index));
            uint64_t count = cm_number(&j, 1, "shards");
            valid = j.valid && cm_equal(cm_get(&j, 1, "schema"), "buster-ci-history-day-v1") &&
                cm_equal(cm_get(&j, 1, "date"), date) && count > 0 && count <= 64;
            cm_json_free(&j);
            for (uint64_t shard = 0; valid && shard < count; ++shard)
            {
                snprintf(path, sizeof(path), "history/%s/%" PRIu64 ".jsonl", date, shard);
                char *records = cm_data_read(t, path, &available);
                valid = records && available == 1 && cm_import(s, records, strlen(records));
                free(records);
            }
        }
        free(index);
    }
    if (!valid) { ++s->gaps; t->invalid_data = 1; }
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_history_append(CmTransport *t, CmStore *s, CmOutputs *out, unsigned begin, const char *observed)
{
    char date[11], path[128];
    cm_copy(date, sizeof(date), observed);
    snprintf(path, sizeof(path), "history/%s/index.json", date);
    int available = 0;
    char *index = cm_data_read(t, path, &available);
    int valid = available >= 0;
    uint64_t shards = 1;
    if (index)
    {
        CmJson j = cm_json_parse(index, strlen(index));
        shards = cm_number(&j, 1, "shards");
        valid = valid && j.valid && cm_equal(cm_get(&j, 1, "schema"), "buster-ci-history-day-v1") &&
            cm_equal(cm_get(&j, 1, "date"), date) && shards > 0 && shards <= 64;
        cm_json_free(&j);
    }
    free(index);
    snprintf(path, sizeof(path), "history/%s/%" PRIu64 ".jsonl", date, shards - 1);
    char *previous = valid ? cm_data_read(t, path, &available) : NULL;
    if (available < 0) valid = 0;
    FILE *batch = valid ? tmpfile() : NULL;
    valid = valid && batch;
    if (valid)
    {
        for (unsigned i = begin; i < s->count; ++i) cm_emit_row(batch, &s->rows[i]);
        char *append = cm_memory(batch); fclose(batch);
        valid = append != NULL;
        if (valid && append[0])
        {
            size_t old_length = previous ? strlen(previous) : 0;
            size_t new_length = strlen(append);
            if (old_length + new_length > CM_BYTES)
            {
                valid = shards < 64 && new_length <= CM_BYTES;
                if (valid)
                {
                    ++shards; snprintf(path, sizeof(path), "history/%s/%" PRIu64 ".jsonl", date, shards - 1);
                    old_length = 0;
                }
            }
            FILE *combined = valid ? tmpfile() : NULL;
            if (combined)
            {
                if (old_length) fputs(previous, combined);
                fputs(append, combined); valid = cm_output(out, path, combined);
            }
            else valid = 0;
            FILE *metadata = valid ? tmpfile() : NULL;
            if (metadata)
            {
                fprintf(metadata, "{\"schema\":\"buster-ci-history-day-v1\",\"date\":\"%s\",\"shards\":%" PRIu64 "}\n", date, shards);
                snprintf(path, sizeof(path), "history/%s/index.json", date); valid = cm_output(out, path, metadata);
            }
            else valid = 0;
        }
        free(append);
    }
    free(previous);
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_make_manifest(CmTransport *t, CmStore *s, CmOutputs *out, const char *observed)
{
    FILE *file = tmpfile();
    int result = file != NULL;
    if (file)
    {
        struct rusage resources = {0}; getrusage(RUSAGE_SELF, &resources);
        fputs("{\"schema\":\"buster-ci-history-manifest-v1\",\"watermark\":", file); cm_quote(file, observed);
        fputs(",\"collector_revision\":", file); cm_quote(file, t->revision);
        fputs(",\"producer_run_id\":", file); cm_quote(file, getenv("GITHUB_RUN_ID") ? getenv("GITHUB_RUN_ID") : "");
        fputs(",\"producer_run_attempt\":", file); cm_quote(file, getenv("GITHUB_RUN_ATTEMPT") ? getenv("GITHUB_RUN_ATTEMPT") : "");
        fputs(",\"sweep_before\":", file); cm_quote(file, t->sweep_before);
        fprintf(file, ",\"policy\":\"" CM_POLICY "\",\"gaps\":%u,\"incomplete_runs\":%u,"
            "\"requests\":%u,\"read_retries\":%u,\"api_failures\":%u,\"history_days_read\":30,"
            "\"pending_runs\":%u,\"run_cache_bound\":%u,\"sweep_page\":%u,"
            "\"collection_wall_seconds\":%.3f,\"native_peak_rss_kib\":%ld,"
            "\"row_bound\":%u,\"series_bound\":%u,\"retention\":\"raw shards retained indefinitely; bounded derived reports\"}\n",
            s->gaps, s->incomplete_runs, t->requests, t->retries, t->failures, t->pending_count, CM_RUN_CACHE,
            t->sweep_page, cm_clock() - t->collection_started, resources.ru_maxrss, CM_ROWS, CM_SERIES);
        result = cm_output(out, "manifest.json", file);
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_stage(CmTransport *t, CmOutputs *out, const char *directory, const char *observed)
{
    int valid = directory && strlen(directory) < 2048 && mkdir(directory, 0700) == 0;
    char path[4096];
    FILE *manifest = NULL;
    if (valid)
    {
        snprintf(path, sizeof(path), "%s/publish.json", directory);
        manifest = fopen(path, "wb"); valid = manifest != NULL;
    }
    if (valid)
    {
        fputs("{\"schema\":\"buster-ci-history-publication-v1\",\"observed_at\":", manifest); cm_quote(manifest, observed);
        fputs(",\"collector_revision\":", manifest); cm_quote(manifest, t->revision);
        fputs(",\"producer_run_id\":", manifest); cm_quote(manifest, getenv("GITHUB_RUN_ID") ? getenv("GITHUB_RUN_ID") : "");
        fputs(",\"producer_run_attempt\":", manifest); cm_quote(manifest, getenv("GITHUB_RUN_ATTEMPT") ? getenv("GITHUB_RUN_ATTEMPT") : "");
        fputs(",\"expected_head\":", manifest); cm_quote(manifest, t->head);
        fputs(",\"expected_tree\":", manifest); cm_quote(manifest, t->tree);
        fputs(",\"files\":[", manifest);
        for (unsigned i = 0; valid && i < out->count; ++i)
        {
            char name[32]; snprintf(name, sizeof(name), "%03u.txt", i);
            snprintf(path, sizeof(path), "%s/%s", directory, name);
            int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
            FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;
            if (file)
            {
                size_t n = strlen(out->files[i].content);
                valid = fwrite(out->files[i].content, 1, n, file) == n && !ferror(file);
                valid &= fclose(file) == 0;
            }
            else { if (fd >= 0) close(fd); valid = 0; }
            if (valid)
            {
                if (i) fputc(',', manifest);
                fputs("{\"path\":", manifest); cm_quote(manifest, out->files[i].path);
                fputs(",\"file\":", manifest); cm_quote(manifest, name); fputc('}', manifest);
            }
        }
        fputs("]}\n", manifest); valid &= !ferror(manifest); valid &= fclose(manifest) == 0;
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_regular_read(const char *directory, const char *name, char **content)
{
    int result = cm_path(name) && !strchr(name, '/');
    char path[4096];
    if (result)
    {
        int n = snprintf(path, sizeof(path), "%s/%s", directory, name);
        int fd = n > 0 && (size_t)n < sizeof(path) ? open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK) : -1;
        struct stat st = {0};
        result = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
            st.st_nlink == 1 && st.st_size >= 0 && (uint64_t)st.st_size <= CM_BYTES;
        FILE *file = result ? fdopen(fd, "rb") : NULL;
        if (file)
        {
            *content = cm_memory(file); fclose(file);
            result = *content != NULL && strlen(*content) == (size_t)st.st_size;
        }
        else { if (fd >= 0) close(fd); result = 0; }
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_publish_stage(CmTransport *t, const char *directory)
{
    int trusted = cm_equal(getenv("GITHUB_REF"), "refs/heads/main") &&
        (cm_equal(getenv("GITHUB_EVENT_NAME"), "schedule") || cm_equal(getenv("GITHUB_EVENT_NAME"), "workflow_dispatch"));
    char *text = NULL;
    int valid = trusted && cm_regular_read(directory, "publish.json", &text);
    CmJson plan = cm_json_parse(text ? text : "", text ? strlen(text) : 0);
    valid = valid && plan.valid && cm_equal(cm_get(&plan, 1, "schema"), "buster-ci-history-publication-v1") &&
        cm_equal(cm_get(&plan, 1, "collector_revision"), t->revision) &&
        cm_equal(cm_get(&plan, 1, "producer_run_id"), getenv("GITHUB_RUN_ID")) &&
        cm_equal(cm_get(&plan, 1, "producer_run_attempt"), getenv("GITHUB_RUN_ATTEMPT")) &&
        cm_time(cm_get(&plan, 1, "observed_at")) >= 0;
    CmOutputs out = {0};
    if (valid)
    {
        char expected_head[41], expected_tree[41];
        cm_copy(expected_head, sizeof(expected_head), cm_get(&plan, 1, "expected_head"));
        cm_copy(expected_tree, sizeof(expected_tree), cm_get(&plan, 1, "expected_tree"));
        valid = (!expected_head[0] || cm_sha(expected_head)) && (!expected_tree[0] || cm_sha(expected_tree)) && cm_data_head(t);
        int lease = valid && cm_equal(expected_head, t->head) && cm_equal(expected_tree, t->tree);
        int identical = valid && t->data_exists, newer = !t->data_exists;
        int available = 0;
        char *previous_manifest = valid ? cm_data_read(t, "manifest.json", &available) : NULL;
        if (previous_manifest)
        {
            CmJson m = cm_json_parse(previous_manifest, strlen(previous_manifest));
            valid = m.valid && cm_equal(cm_get(&m, 1, "schema"), "buster-ci-history-manifest-v1");
            newer = valid && cm_time(cm_get(&plan, 1, "observed_at")) > cm_time(cm_get(&m, 1, "watermark"));
            identical = identical && valid && cm_equal(cm_get(&plan, 1, "observed_at"), cm_get(&m, 1, "watermark")) &&
                cm_equal(cm_get(&plan, 1, "collector_revision"), cm_get(&m, 1, "collector_revision")) &&
                cm_equal(cm_get(&plan, 1, "producer_run_id"), cm_get(&m, 1, "producer_run_id")) &&
                cm_equal(cm_get(&plan, 1, "producer_run_attempt"), cm_get(&m, 1, "producer_run_attempt"));
            cm_json_free(&m);
        }
        else if (t->data_exists) valid = 0;
        free(previous_manifest);
        unsigned files = cm_member(&plan, 1, "files");
        valid = valid && files && plan.tokens[files].kind == 'a';
        for (unsigned entry = valid ? plan.tokens[files].child : 0; valid && entry; entry = plan.tokens[entry].next)
        {
            const char *path = cm_get(&plan, entry, "path"), *name = cm_get(&plan, entry, "file");
            char *content = NULL;
            valid = out.count < CM_FILES && cm_publication_path(path) && strlen(name) == 7 &&
                cm_regular_read(directory, name, &content);
            if (valid && strlen(path) > 6 && cm_equal(path + strlen(path) - 6, ".jsonl") && strncmp(path, "history/", 8) == 0)
            {
                CmStore validation;
                valid = cm_store_init(&validation);
                if (valid) valid = cm_import(&validation, content, strlen(content));
                cm_store_free(&validation);
                char *prior = valid ? cm_data_read(t, path, &available) : NULL;
                if (available < 0) valid = 0;
                if (prior) valid = valid && strlen(content) >= strlen(prior) && strncmp(content, prior, strlen(prior)) == 0;
                free(prior);
            }
            for (unsigned i = 0; valid && i < out.count; ++i) valid = !cm_equal(out.files[i].path, path);
            if (valid && cm_equal(path, "manifest.json"))
            {
                CmJson manifest = cm_json_parse(content, strlen(content));
                valid = manifest.valid && cm_equal(cm_get(&manifest, 1, "schema"), "buster-ci-history-manifest-v1") &&
                    cm_equal(cm_get(&manifest, 1, "watermark"), cm_get(&plan, 1, "observed_at")) &&
                    cm_equal(cm_get(&manifest, 1, "collector_revision"), t->revision) &&
                    cm_equal(cm_get(&manifest, 1, "policy"), CM_POLICY) &&
                    cm_equal(cm_get(&manifest, 1, "producer_run_id"), cm_get(&plan, 1, "producer_run_id")) &&
                    cm_equal(cm_get(&manifest, 1, "producer_run_attempt"), cm_get(&plan, 1, "producer_run_attempt"));
                cm_json_free(&manifest);
            }
            if (valid && cm_equal(path, "history/progress.json"))
            {
                CmTransport progress = {0}; valid = cm_progress_parse(&progress, content);
            }
            if (valid && identical)
            {
                char *prior = cm_data_read(t, path, &available);
                identical = available == 1 && prior && cm_equal(content, prior); free(prior);
            }
            if (valid)
            {
                char *safe_path = strdup(path);
                valid = safe_path != NULL;
                if (valid) out.files[out.count++] = (CmFile){safe_path, content, {0}};
            }
            if (!valid) free(content);
        }
        int has_manifest = 0;
        for (unsigned i = 0; i < out.count; ++i) has_manifest |= cm_equal(out.files[i].path, "manifest.json");
        valid = valid && has_manifest;
        if (valid) valid = identical ? out.count > 0 : lease && newer &&
            cm_publish(t, out.files, out.count, cm_get(&plan, 1, "observed_at"));
    }
    cm_outputs_free(&out); cm_json_free(&plan); free(text);
    return valid;
}
#endif
