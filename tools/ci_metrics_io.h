// Linux hosted transport and publication boundary; never executes payloads.
// cm_api invokes the preinstalled gh CLI without a shell and bounds time/bytes.
// cm_publish creates a Git commit whose only parent is the observed data head;
// a non-fast-forward ref update fails closed. No compiler-main ref is writable.
#ifndef BUSTER_CI_METRICS_IO_H
#define BUSTER_CI_METRICS_IO_H
#include "ci_metrics_json.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#define CM_REQUESTS 600u
typedef struct CmResponse CmResponse;
struct CmResponse { const char *path, *method, *content; int success, missing; };
#define CM_RUN_CACHE 4096u
#define CM_PENDING 64u
typedef struct CmRunReceipt CmRunReceipt;
struct CmRunReceipt { uint64_t id, attempt; int complete; };
typedef struct CmTransport CmTransport;
struct CmTransport
{
    unsigned requests, failures, retries;
    double deadline;
    char revision[41], head[41], tree[41];
    int data_exists, invalid_data;
    // Explicit synthetic read fixtures; production leaves this pointer null.
    const CmResponse *fixture;
    unsigned fixture_count, fixture_cursor;
    unsigned request_limit, run_count, run_replace, pending_count, sweep_page;
    double collection_started;
    CmRunReceipt run_cache[CM_RUN_CACHE];
    uint64_t pending[CM_PENDING];
    char sweep_before[32];
};
BUSTER_GLOBAL_LOCAL double cm_clock(void)
{
    struct timespec t = {0}; clock_gettime(CLOCK_MONOTONIC, &t);
    double result = (double)t.tv_sec + t.tv_nsec * 1e-9;
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_process(char *const *args, char **output, size_t limit, int *not_found)
{
    FILE *out = tmpfile(), *err = tmpfile();
    int result = 0;
    pid_t pid = -1;
    if (out && err) pid = fork();
    if (pid == 0)
    {
        setpgid(0, 0);
        struct rlimit bytes = {CM_BYTES, CM_BYTES};
        setrlimit(RLIMIT_FSIZE, &bytes);
        dup2(fileno(out), STDOUT_FILENO); dup2(fileno(err), STDERR_FILENO);
        unsetenv("GH_DEBUG"); setenv("GH_HOST", "github.com", 1);
        execv(args[0], args); _exit(127);
    }
    if (pid > 0)
    {
        setpgid(pid, pid);
        double deadline = cm_clock() + 30;
        int status = 0, done = 0;
        while (!done)
        {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            struct stat st = {0};
            int oversized = fstat(fileno(out), &st) != 0 || st.st_size > (off_t)limit;
            done = waited == pid || (waited < 0 && errno != EINTR);
            if (!done && (cm_clock() >= deadline || oversized))
            {
                kill(-pid, SIGKILL); kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
                done = 1; status = -1;
            }
            if (!done) { struct timespec pause = {0, 10000000}; nanosleep(&pause, NULL); }
        }
        // gh has no child workloads; terminate any unexpected surviving helper.
        kill(-pid, SIGKILL);
        result = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (fseek(out, 0, SEEK_END) == 0)
        {
            long n = ftell(out);
            if (n >= 0 && (uint64_t)n <= limit && fseek(out, 0, SEEK_SET) == 0)
            {
                *output = malloc((size_t)n + 1);
                if (*output)
                {
                    size_t received = fread(*output, 1, (size_t)n, out);
                    result &= received == (size_t)n && !ferror(out);
                    (*output)[received] = 0;
                }
                else result = 0;
            }
            else result = 0;
        }
        else result = 0;
        if (not_found || !result)
        {
            char message[2048];
            rewind(err); size_t n = fread(message, 1, sizeof(message) - 1, err); message[n] = 0;
            if (not_found) *not_found = !result && strstr(message, "(HTTP 404)") != NULL;
            if (!result)
            {
                const char *http = strstr(message, "(HTTP ");
                int code = http && strlen(http) >= 9 ? atoi(http + 6) : 0;
                const char *reason = strstr(message, "Resource not accessible") ? "resource-inaccessible" :
                    strstr(message, "admin rights") ? "admin-rights-required" :
                    strstr(message, "authentication") ? "authentication-failed" :
                    strstr(message, "rate limit") ? "rate-limited" :
                    strstr(message, "TLS") ? "tls-error" : strstr(message, "timeout") ? "timeout" :
                    strstr(message, "redirect") ? "redirect-error" :
                    strstr(message, "escape sequences") ? "terminal-escape-sequences" : "transport-or-response-error";
                fprintf(stderr, "CI history transport: exit=%d http=%d reason=%s\n",
                    status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1, code, reason);
            }
        }
    }
    if (out) fclose(out);
    if (err) fclose(err);
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_api(CmTransport *t, const char *path, const char *method,
    const char *body, int raw, char **output, int *missing)
{
    int result = 0, last_absent = 0;
    char endpoint[4096], temporary[] = "/tmp/buster-ci-metrics-input-XXXXXX";
    int fd = body ? mkstemp(temporary) : -1, input_ok = !body;
    if (body && fd >= 0)
    {
        FILE *input = fdopen(fd, "wb");
        if (input)
        {
            size_t n = strlen(body); input_ok = fwrite(body, 1, n, input) == n && !ferror(input);
            input_ok &= fclose(input) == 0;
        }
        else close(fd);
    }
    int sized = snprintf(endpoint, sizeof(endpoint), "/repos/" CM_REPO "/%s", path);
    int allowed = sized > 0 && (size_t)sized < sizeof(endpoint) && input_ok;
    unsigned attempts = cm_equal(method, "GET") ? 3 : 1;
    for (unsigned attempt = 0; allowed && attempt < attempts && !result; ++attempt)
    {
        if (t->requests >= (t->request_limit ? t->request_limit : CM_REQUESTS) || cm_clock() >= t->deadline) allowed = 0;
        else
        {
            free(*output); *output = NULL;
            char *args[18] = {"/usr/bin/gh", "api", "--hostname", "github.com", endpoint,
                "-X", (char *)method, "-H", raw ? "Accept: application/vnd.github.raw+json" : "Accept: application/vnd.github+json",
                "-H", "X-GitHub-Api-Version: 2022-11-28", NULL, NULL, NULL};
            if (body) { args[11] = "--input"; args[12] = temporary; }
            // Recent gh versions reject ANSI bytes even when stdout is a file.
            // Job logs are untrusted parser input, never sent to a terminal.
            size_t path_length = strlen(path);
            if (cm_equal(method, "GET") && path_length >= 5 && cm_equal(path + path_length - 5, "/logs"))
                args[11] = "--allow-escape-sequences";
            int absent = 0; ++t->requests;
            if (t->fixture)
            {
                if (t->fixture_cursor < t->fixture_count)
                {
                    const CmResponse *response = &t->fixture[t->fixture_cursor++];
                    result = cm_equal(path, response->path) && cm_equal(method, response->method) && response->success;
                    absent = response->missing;
                    *output = strdup(response->content ? response->content : "");
                    result &= *output != NULL;
                }
            }
            else result = cm_process(args, output, CM_BYTES, &absent);
            last_absent = absent;
            if (missing) *missing = absent;
            if (absent) allowed = 0;
            if (!result && allowed && attempt + 1 < attempts)
            {
                ++t->retries;
                if (!t->fixture) { struct timespec pause = {(time_t)(attempt + 1), 0}; nanosleep(&pause, NULL); }
            }
        }
    }
    if (body && fd >= 0) unlink(temporary);
    if (!result && !last_absent) ++t->failures;
    return result;
}
BUSTER_GLOBAL_LOCAL CmJson cm_api_json(CmTransport *t, const char *path, const char *method, const char *body, int *missing)
{
    char *text = NULL;
    int ok = cm_api(t, path, method, body, 0, &text, missing);
    CmJson result = cm_json_parse(text ? text : "", text ? strlen(text) : 0);
    result.valid &= ok;
    free(text);
    return result;
}
BUSTER_GLOBAL_LOCAL char *cm_memory(FILE *file)
{
    char *result = NULL;
    if (file && fflush(file) == 0 && fseek(file, 0, SEEK_END) == 0)
    {
        long n = ftell(file);
        if (n >= 0 && (uint64_t)n <= CM_BYTES && fseek(file, 0, SEEK_SET) == 0)
        {
            result = malloc((size_t)n + 1);
            if (result)
            {
                size_t got = fread(result, 1, (size_t)n, file);
                if (got != (size_t)n || ferror(file)) { free(result); result = NULL; }
                else result[got] = 0;
            }
        }
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_path(const char *path)
{
    int result = path && path[0] && strlen(path) < 512 && path[0] != '/' && !strstr(path, "..") && !strstr(path, "//");
    for (size_t i = 0; result && path[i]; ++i)
    {
        char c = path[i]; result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.';
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_data_head(CmTransport *t)
{
    int missing = 0;
    CmJson j = cm_api_json(t, "git/ref/heads/" CM_BRANCH, "GET", NULL, &missing);
    unsigned object = cm_member(&j, 1, "object");
    const char *sha = cm_get(&j, object, "sha");
    int result = j.valid && cm_sha(sha) && cm_equal(cm_get(&j, object, "type"), "commit");
    if (result)
    {
        cm_copy(t->head, sizeof(t->head), sha); t->data_exists = 1;
        char path[256]; snprintf(path, sizeof(path), "git/commits/%s", t->head);
        CmJson commit = cm_api_json(t, path, "GET", NULL, NULL);
        const char *tree = cm_get(&commit, cm_member(&commit, 1, "tree"), "sha");
        result = commit.valid && cm_sha(tree);
        if (result) cm_copy(t->tree, sizeof(t->tree), tree);
        cm_json_free(&commit);
    }
    else if (missing) { result = 1; t->data_exists = 0; t->head[0] = 0; t->tree[0] = 0; }
    cm_json_free(&j);
    return result;
}
BUSTER_GLOBAL_LOCAL char *cm_data_read(CmTransport *t, const char *path, int *available)
{
    char *result = NULL;
    *available = 0;
    if (t->data_exists && cm_path(path))
    {
        char endpoint[1024];
        snprintf(endpoint, sizeof(endpoint), "contents/%s?ref=%s", path, t->head);
        int missing = 0;
        int ok = cm_api(t, endpoint, "GET", NULL, 1, &result, &missing);
        *available = ok ? 1 : missing ? 0 : -1;
        if (!ok) { free(result); result = NULL; }
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_publication_path(const char *path)
{
    int valid = cm_path(path);
    int allowed = cm_equal(path, "manifest.json") || cm_equal(path, "README.md") ||
        cm_equal(path, "history/progress.json") || cm_equal(path, "reports/index.md") ||
        cm_equal(path, "reports/all.md") || cm_equal(path, "reports/recent.jsonl") || cm_equal(path, "reports/recent.csv");
    const char *hex = NULL, *suffix = NULL;
    if (valid && strncmp(path, "history/anchors/", 16) == 0) { hex = path + 16; suffix = ".jsonl"; }
    else if (valid && strncmp(path, "reports/series-", 15) == 0) { hex = path + 15; suffix = ".md"; }
    if (hex)
    {
        int shaped = strlen(hex) == 16 + strlen(suffix);
        for (unsigned i = 0; shaped && i < 16; ++i) shaped = cm_hex(hex[i]) >= 0 && !(hex[i] >= 'A' && hex[i] <= 'F');
        allowed |= shaped && cm_equal(hex + 16, suffix);
    }
    if (valid && strlen(path) >= 26 && strncmp(path, "history/", 8) == 0 && path[18] == '/')
    {
        char date[32]; memcpy(date, path + 8, 10); memcpy(date + 10, "T00:00:00Z", 11);
        int dated = cm_time(date) >= 0;
        const char *name = path + 19;
        if (dated && cm_equal(name, "index.json")) allowed = 1;
        else if (dated)
        {
            const char *dot = strchr(name, '.'); size_t digits = dot ? (size_t)(dot - name) : 0;
            char ordinal[4]; uint64_t shard = 0;
            if (digits && digits < sizeof(ordinal) && cm_equal(dot, ".jsonl"))
            {
                memcpy(ordinal, name, digits); ordinal[digits] = 0;
                allowed |= cm_unsigned(ordinal, &shard) && shard < 64 && (digits == 1 || ordinal[0] != '0');
            }
        }
    }
    int result = valid && allowed;
    return result;
}
typedef struct CmFile CmFile;
struct CmFile { const char *path; const char *content; char sha[41]; };
BUSTER_GLOBAL_LOCAL int cm_publish(CmTransport *t, CmFile *files, unsigned count, const char *observation)
{
    int valid = count && count <= 256 && cm_sha(t->revision) && cm_time(observation) >= 0 && !t->invalid_data;
    // This function is called only by trusted schedule/dispatch code, never a PR.
    for (unsigned i = 0; valid && i < count; ++i)
    {
        valid = cm_publication_path(files[i].path);
        for (unsigned k = 0; valid && k < i; ++k) valid = !cm_equal(files[i].path, files[k].path);
        FILE *body = valid ? tmpfile() : NULL;
        valid = valid && body;
        if (valid)
        {
            fputs("{\"encoding\":\"utf-8\",\"content\":", body); cm_quote(body, files[i].content); fputc('}', body);
            char *payload = cm_memory(body); fclose(body); valid = payload != NULL;
            CmJson blob = cm_api_json(t, "git/blobs", "POST", payload, NULL);
            const char *sha = cm_get(&blob, 1, "sha");
            valid = valid && blob.valid && cm_sha(sha);
            if (valid) cm_copy(files[i].sha, sizeof(files[i].sha), sha);
            cm_json_free(&blob); free(payload);
        }
    }
    FILE *body = valid ? tmpfile() : NULL;
    if (valid && body)
    {
        fputc('{', body);
        if (t->data_exists) { fputs("\"base_tree\":", body); cm_quote(body, t->tree); fputc(',', body); }
        fputs("\"tree\":[", body);
        for (unsigned i = 0; i < count; ++i)
        {
            if (i) fputc(',', body);
            fputs("{\"path\":", body); cm_quote(body, files[i].path);
            fputs(",\"mode\":\"100644\",\"type\":\"blob\",\"sha\":", body); cm_quote(body, files[i].sha); fputc('}', body);
        }
        fputs("]}", body);
        char *payload = cm_memory(body); fclose(body);
        CmJson tree = cm_api_json(t, "git/trees", "POST", payload, NULL);
        char new_tree[41]; cm_copy(new_tree, sizeof(new_tree), cm_get(&tree, 1, "sha"));
        valid = payload && tree.valid && cm_sha(new_tree); cm_json_free(&tree); free(payload);
        body = valid ? tmpfile() : NULL;
        if (body)
        {
            fputs("{\"message\":", body); cm_quote(body, "Hosted CI observations (advisory)");
            fputs(",\"tree\":", body); cm_quote(body, new_tree); fputs(",\"parents\":[", body);
            if (t->data_exists) cm_quote(body, t->head);
            fputs("]}", body); payload = cm_memory(body); fclose(body);
            CmJson commit = cm_api_json(t, "git/commits", "POST", payload, NULL);
            char sha[41]; cm_copy(sha, sizeof(sha), cm_get(&commit, 1, "sha"));
            valid = payload && commit.valid && cm_sha(sha); cm_json_free(&commit); free(payload);
            body = valid ? tmpfile() : NULL;
            if (body)
            {
                // Fresh read proves the parent/lease still matches before the non-force update.
                CmTransport current = *t;
                valid = cm_data_head(&current) && current.data_exists == t->data_exists && cm_equal(current.head, t->head);
                t->requests = current.requests; t->failures = current.failures; t->retries = current.retries;
                if (valid)
                {
                    fputc('{', body);
                    if (!t->data_exists) fputs("\"ref\":\"refs/heads/" CM_BRANCH "\",", body);
                    fputs("\"sha\":", body); cm_quote(body, sha);
                    if (t->data_exists) fputs(",\"force\":false", body);
                    fputc('}', body); payload = cm_memory(body);
                    CmJson ref = cm_api_json(t, t->data_exists ? "git/refs/heads/" CM_BRANCH : "git/refs",
                        t->data_exists ? "PATCH" : "POST", payload, NULL);
                    valid = payload && ref.valid && cm_equal(cm_get(&ref, cm_member(&ref, 1, "object"), "sha"), sha);
                    if (valid) { cm_copy(t->head, sizeof(t->head), sha); cm_copy(t->tree, sizeof(t->tree), new_tree); t->data_exists = 1; }
                    cm_json_free(&ref); free(payload);
                }
                fclose(body);
            }
            else valid = 0;
        }
        else valid = 0;
    }
    else valid = 0;
    return valid;
}
#endif
