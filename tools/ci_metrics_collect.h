// Finalized REST execution inventory and exact-job startup/source log joins.
// cm_jobs pages attempt-specific inventories and checks every API binding.
// cm_machine proves the pinned startup precedes checkout in the executed job;
// ambiguous schemas, workflow ownership or source records stay unavailable.
// Logs are a bounded fallback for reports already emitted by #2758, not probes.
#ifndef BUSTER_CI_METRICS_COLLECT_H
#define BUSTER_CI_METRICS_COLLECT_H
#include "ci_metrics_model.h"
#include "ci_metrics_io.h"
#define CM_MAX_ATTEMPTS 20u
#define CM_MAX_JOBS 1000u
typedef struct CmCollection CmCollection;
struct CmCollection
{
    CmTransport *transport;
    CmStore *store;
    const char *observed;
    uint64_t selected_job;
    unsigned executions, machine_verified, matrix_verified, source_verified, skipped, failed;
    size_t new_record_bytes;
    int source_action_verified, quiet;
};
BUSTER_GLOBAL_LOCAL const char *cm_machine_value(const CmJson *j, const char *key)
{
    unsigned field = cm_member(j, 1, key);
    const char *result = cm_equal(cm_get(j, field, "status"), "available") ? cm_get(j, field, "value") : "";
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_machine_schema(const CmJson *j, const char *schema)
{
    int result = j->valid && cm_equal(cm_machine_value(j, "schema"), schema);
    unsigned count = 0;
    for (unsigned key = j->valid ? j->tokens[1].child : 0; result && key; )
    {
        unsigned value = j->tokens[key].next;
        result = value && j->tokens[value].kind == 'o' && ++count <= 64 &&
            strlen(cm_get(j, value, "value")) <= CM_FIELD && strlen(cm_get(j, value, "reason")) <= CM_FIELD;
        const char *status = cm_get(j, value, "status");
        result = result && (cm_equal(status, "available") || cm_equal(status, "unknown") ||
            cm_equal(status, "unavailable") || cm_equal(status, "not_applicable") || cm_equal(status, "partial"));
        key = value ? j->tokens[value].next : 0;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_bindings(const CmJson *j, const CmRow *row, int source)
{
    uint64_t run = 0, attempt = 0;
    int result = cm_unsigned(cm_machine_value(j, "run_id"), &run) &&
        cm_unsigned(cm_machine_value(j, "run_attempt"), &attempt) && run == row->run &&
        attempt == row->origin_attempt && cm_sha(cm_machine_value(j, "workflow_sha")) &&
        cm_sha(cm_machine_value(j, "event_sha"));
    if (source) result &= cm_equal(cm_machine_value(j, "source_repository"), CM_REPO);
    return result;
}
BUSTER_GLOBAL_LOCAL char *cm_steps(const CmJson *j, unsigned job)
{
    FILE *file = tmpfile();
    char *result = NULL;
    if (file)
    {
        fputc('[', file); unsigned count = 0;
        unsigned array = cm_member(j, job, "steps");
        int valid = array && j->tokens[array].kind == 'a';
        const char *keys[] = {"name", "status", "conclusion", "started_at", "completed_at"};
        for (unsigned step = valid ? j->tokens[array].child : 0; valid && step; step = j->tokens[step].next)
        {
            valid = j->tokens[step].kind == 'o' && ++count <= 256;
            if (valid)
            {
                if (count > 1) fputc(',', file);
                fputc('{', file);
                for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k)
                {
                    if (k) fputc(',', file);
                    cm_quote(file, keys[k]); fputc(':', file);
                    const char *value = cm_get(j, step, keys[k]);
                    valid &= strlen(value) <= CM_FIELD;
                    cm_quote(file, value);
                }
                fputc('}', file);
            }
        }
        fputc(']', file);
        if (valid) result = cm_memory(file);
        fclose(file);
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_log_interval(const char *stamp, const char *start, const char *end)
{
    int64_t time = cm_time(stamp), a = cm_time(start), b = cm_time(end);
    // REST endpoints have one-second resolution. Keep that uncertainty explicit.
    int result = time >= 0 && a >= 0 && b >= a && time >= a && time <= b + 1;
    return result;
}
BUSTER_GLOBAL_LOCAL char *cm_log_record(char *log, const char *marker, const char *start, const char *end, unsigned *count)
{
    char *result = NULL;
    *count = 0;
    for (char *line = log; line && *line; )
    {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *space = strchr(line, ' ');
        if (space)
        {
            *space = 0;
            char *message = space + 1;
            size_t length = strlen(message);
            if (length && message[length - 1] == '\r') message[--length] = 0;
            if (strncmp(message, marker, strlen(marker)) == 0 && cm_log_interval(line, start, end))
            {
                ++*count;
                if (*count == 1 && length <= 32768) result = strdup(message + strlen(marker));
            }
            *space = ' ';
        }
        line = next;
    }
    if (*count != 1) { free(result); result = NULL; }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_job_key(const char *key)
{
    int valid = key && key[0] && strlen(key) <= 128;
    for (size_t i = 0; valid && key[i]; ++i)
        valid = (key[i] >= 'a' && key[i] <= 'z') || (key[i] >= 'A' && key[i] <= 'Z') ||
            (key[i] >= '0' && key[i] <= '9') || key[i] == '_' || key[i] == '-';
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_workflow_startup(const char *workflow, const char *key, int *matrix_static)
{
    // Restricted repository block-YAML contract: no aliases, containers, injected
    // loader/compiler environment, or action-selection expressions are admitted.
    int valid = workflow && cm_job_key(key), found = 0, in_job = 0, steps = 0, first = 0, pinned = 0;
    *matrix_static = 1;
    char *copy = valid ? strdup(workflow) : NULL;
    valid = valid && copy;
    char declaration[160];
    snprintf(declaration, sizeof(declaration), "  %s:", key);
    for (char *line = copy; valid && line && *line; )
    {
        char *next = strchr(line, '\n'); if (next) *next++ = 0;
        size_t indent = strspn(line, " ");
        const char *text = line + indent;
        if (strchr(line, '\t') || strstr(text, "LD_PRELOAD:") || strstr(text, "DYLD_") ||
            strncmp(text, "PATH:", 5) == 0 || strncmp(text, "container:", 10) == 0 ||
            strncmp(text, "defaults:", 9) == 0 || strncmp(text, "environment:", 12) == 0 ||
            strncmp(text, "BASH_ENV:", 9) == 0 || strncmp(text, "ENV:", 4) == 0)
            valid = 0;
        if (strncmp(line, declaration, strlen(declaration)) == 0 && line[strlen(declaration)] == 0)
        {
            ++found; in_job = 1; steps = 0; first = 0; pinned = 0;
        }
        else if (in_job && indent <= 2 && text[0] && text[0] != '#') in_job = 0;
        if (in_job)
        {
            if (strstr(text, "fromJSON(") || strstr(text, "fromJson(") || strstr(text, "needs.")) *matrix_static = 0;
            if (indent == 4 && cm_equal(text, "steps:")) steps = 1;
            else if (steps && indent == 6 && strncmp(text, "- ", 2) == 0)
            {
                if (first) steps = 0;
                else { first = 1; valid &= cm_equal(text, "- name: Machine specifications"); }
            }
            else if (steps && first && indent == 8)
            {
                if (cm_equal(text, "uses: buster14a/buster/.github/actions/machine-specifications@" CM_REPORTER)) pinned = 1;
                else if (strncmp(text, "uses:", 5) == 0 || strncmp(text, "run:", 4) == 0 ||
                         strncmp(text, "env:", 4) == 0 || strncmp(text, "if:", 3) == 0) valid = 0;
            }
            else if (steps && first && indent >= 10 && strncmp(text, "mode:", 5) == 0) valid = 0;
        }
        line = next;
    }
    valid = valid && found == 1 && first && pinned;
    free(copy);
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_workflow_path(char *out, size_t size, const char *ref)
{
    const char *prefix = CM_REPO "/";
    int valid = strncmp(ref, prefix, strlen(prefix)) == 0;
    const char *begin = ref + (valid ? strlen(prefix) : 0);
    const char *at = strchr(begin, '@');
    size_t length = at ? (size_t)(at - begin) : 0;
    valid = valid && length > 0 && length < size;
    if (valid)
    {
        memcpy(out, begin, length); out[length] = 0;
        valid = cm_path(out) && strncmp(out, ".github/workflows/", 18) == 0 &&
            strlen(out) >= 4 && cm_equal(out + strlen(out) - 4, ".yml");
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_workflow_source(const char *workflow, const char *key)
{
    char declaration[160]; snprintf(declaration, sizeof(declaration), "  %s:", key);
    char *copy = workflow ? strdup(workflow) : NULL;
    int in_job = 0, in_source = 0, pinned = 0, mode = 0, bad = 0, count = 0;
    for (char *line = copy; line && *line; )
    {
        char *next = strchr(line, '\n'); if (next) *next++ = 0;
        size_t indent = strspn(line, " "); const char *text = line + indent;
        if (cm_equal(line, declaration)) in_job = 1;
        else if (in_job && indent <= 2 && text[0] && text[0] != '#') in_job = 0;
        if (in_job && indent == 6 && strncmp(text, "- ", 2) == 0)
        {
            in_source = cm_equal(text, "- name: Record actual checkout identity");
            if (in_source) ++count;
        }
        else if (in_source)
        {
            if (indent == 8 && cm_equal(text, "uses: buster14a/buster/.github/actions/machine-specifications@" CM_REPORTER)) pinned = 1;
            else if (indent == 8 && (strncmp(text, "run:", 4) == 0 || strncmp(text, "env:", 4) == 0)) bad = 1;
            else if (indent == 10 && cm_equal(text, "mode: source")) mode = 1;
        }
        line = next;
    }
    int result = copy && count == 1 && pinned && mode && !bad;
    free(copy);
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_matrix_identity(char *out, size_t size, const char *workflow, const char *key, uint64_t ordinal)
{
    char declaration[160]; snprintf(declaration, sizeof(declaration), "  %s:", key);
    char *copy = workflow ? strdup(workflow) : NULL;
    FILE *file = copy ? tmpfile() : NULL;
    int valid = file != NULL, in_job = 0, in_matrix = 0, include = 0, matrix_count = 0;
    unsigned cell = 0, fields = 0; int selected = 0, index_bound = 0, in_startup = 0;
    if (file) fputc('{', file);
    for (char *line = copy; valid && line && *line; )
    {
        char *next = strchr(line, '\n'); if (next) *next++ = 0;
        size_t indent = strspn(line, " "); const char *text = line + indent;
        if (cm_equal(line, declaration)) in_job = 1;
        else if (in_job && indent <= 2 && text[0] && text[0] != '#') in_job = 0;
        if (in_job)
        {
            if (indent == 6 && cm_equal(text, "matrix:")) { in_matrix = 1; ++matrix_count; }
            else if (in_matrix && indent <= 6 && text[0] && text[0] != '#') in_matrix = 0;
            if (in_matrix && indent == 8 && cm_equal(text, "include:")) include = 1;
            else if (in_matrix && text[0] && text[0] != '#' && indent >= 8)
            {
                if (indent == 10 && strncmp(text, "- ", 2) == 0)
                {
                    selected = cell++ == ordinal; text += 2;
                    valid = include && cell <= 256;
                }
                else valid = include && indent == 12 && cell > 0;
                const char *colon = strchr(text, ':');
                valid = valid && colon && colon > text && (size_t)(colon - text) < 128 && !strstr(text, "${{");
                if (valid && selected)
                {
                    char name[128], value[CM_FIELD + 1];
                    size_t key_length = (size_t)(colon - text); memcpy(name, text, key_length); name[key_length] = 0;
                    valid = cm_job_key(name);
                    const char *begin = colon + 1; while (*begin == ' ') ++begin;
                    size_t n = strlen(begin); while (n && begin[n - 1] == ' ') --n;
                    // This restricted include-only contract excludes YAML aliases,
                    // block scalars and compound values rather than interpreting them.
                    valid = valid && n < sizeof(value) && n && begin[0] != '[' && begin[0] != '{' &&
                        begin[0] != '&' && begin[0] != '*' && begin[0] != '|' && begin[0] != '>';
                    if (valid && (begin[0] == 34 || begin[0] == 39))
                    {
                        valid = n >= 2 && begin[n - 1] == begin[0];
                        if (valid) { ++begin; n -= 2; }
                    }
                    if (valid)
                    {
                        memcpy(value, begin, n); value[n] = 0;
                        valid = !strchr(value, '\\') && !strchr(value, 39) && !strchr(value, 34) && ++fields <= 32;
                        if (valid)
                        {
                            if (fields > 1) fputc(',', file);
                            cm_quote(file, name); fputc(':', file); cm_quote(file, value);
                        }
                    }
                }
            }
            if (indent == 6 && strncmp(text, "- ", 2) == 0)
                in_startup = cm_equal(text, "- name: Machine specifications");
            if (in_startup && indent == 10 && cm_equal(text, "matrix-index: ${{ strategy.job-index }}")) index_bound = 1;
        }
        line = next;
    }
    if (file)
    {
        fputc('}', file); char *identity = cm_memory(file); fclose(file);
        valid = valid && matrix_count == 1 && include && ordinal < cell && fields && index_bound &&
            identity && strlen(identity) < size;
        CmJson canonical = cm_json_parse(identity ? identity : "", identity ? strlen(identity) : 0);
        valid = valid && canonical.valid && canonical.tokens[1].kind == 'o';
        if (valid) cm_copy(out, size, identity);
        cm_json_free(&canonical); free(identity);
    }
    free(copy);
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_workflow_proof(CmCollection *c, const CmJson *machine, CmRow *row, char fields[CM_FIELD_COUNT][CM_FIELD + 1])
{
    char path[512], endpoint[1024];
    const char *key = cm_machine_value(machine, "job"), *revision = cm_machine_value(machine, "workflow_sha");
    int valid = cm_job_key(key) && cm_workflow_path(path, sizeof(path), cm_machine_value(machine, "workflow_ref")) && cm_sha(revision);
    if (valid)
    {
        snprintf(endpoint, sizeof(endpoint), "contents/%s?ref=%s", path, revision);
        CmJson content = cm_api_json(c->transport, endpoint, "GET", NULL, NULL);
        const char *blob = cm_get(&content, 1, "sha");
        char blob_sha[41]; cm_copy(blob_sha, sizeof(blob_sha), blob);
        valid = content.valid && cm_sha(blob_sha); cm_json_free(&content);
        char *workflow = NULL;
        if (valid) valid = cm_api(c->transport, endpoint, "GET", NULL, 1, &workflow, NULL);
        int static_matrix = 0;
        valid = valid && cm_workflow_startup(workflow, key, &static_matrix);
        if (valid)
        {
            c->source_action_verified = cm_workflow_source(workflow, key);
            cm_copy(fields[CM_WORKFLOW], CM_FIELD + 1, path);
            cm_copy(fields[CM_JOB_KEY], CM_FIELD + 1, key);
            cm_copy(fields[CM_WORKFLOW_SHA], CM_FIELD + 1, revision);
            cm_copy(fields[CM_WORKFLOW_BLOB], CM_FIELD + 1, blob_sha);
            const char *index = cm_machine_value(machine, "matrix_index");
            unsigned field = cm_member(machine, 1, "matrix_index");
            uint64_t ordinal = 0;
            if (cm_unsigned(index, &ordinal) && ordinal < 256 &&
                cm_matrix_identity(fields[CM_MATRIX], CM_FIELD + 1, workflow, key, ordinal)) { }
            else if (cm_equal(cm_get(machine, field, "status"), "not_applicable"))
                cm_copy(fields[CM_MATRIX], CM_FIELD + 1, "non-matrix");
            else cm_copy(fields[CM_MATRIX], CM_FIELD + 1, "unavailable-dynamic-or-reusable-matrix");
            // Dynamic inputs and reusable invocation context cannot be guessed.
            cm_copy(fields[CM_INVOCATION], CM_FIELD + 1, "direct");
            row->s[CM_HARDWARE] = "verified-startup";
        }
        free(workflow);
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL void cm_reported_context(CmCollection *c, const char *log, CmRow *row,
    char fields[CM_FIELD_COUNT][CM_FIELD + 1])
{
    FILE *phases = tmpfile();
    unsigned count = 0, tools = 0; int valid = phases != NULL;
    uint64_t seen[64];
    if (phases) fputc('[', phases);
    for (const char *line = log; valid && line && *line; )
    {
        const char *end = strchr(line, '\n'); size_t length = end ? (size_t)(end - line) : strlen(line);
        const char *space = memchr(line, ' ', length);
        if (space && (size_t)(space - line) < 32)
        {
            char stamp[32]; size_t n = (size_t)(space - line);
            memcpy(stamp, line, n); stamp[n] = 0;
            const char *text = space + 1; size_t bytes = length - (size_t)(text - line);
            if (bytes && text[bytes - 1] == '\r') --bytes;
            if (cm_log_interval(stamp, row->s[CM_STARTED], row->s[CM_COMPLETED]))
            {
                const char *phase_marker = "NATIVE_PHASE_RECORD ", *tool_marker = "NATIVE_TOOLCHAIN ";
                size_t marker = strlen(phase_marker);
                if (bytes > marker && strncmp(text, phase_marker, marker) == 0)
                {
                    CmJson record = cm_json_parse(text + marker, bytes - marker);
                    uint64_t sequence = cm_number(&record, 1, "sequence");
                    uint64_t elapsed = cm_number(&record, 1, "elapsed_ns");
                    int accepted = record.valid && cm_equal(cm_get(&record, 1, "schema"), "buster.native-observation.phase.v1") &&
                        cm_equal(cm_get(&record, 1, "clock"), "time.perf_counter_ns") &&
                        sequence > 0 && sequence <= 1000 && elapsed <= UINT64_C(604800000000000) &&
                        strlen(cm_get(&record, 1, "identity_sha256")) == 64 && strlen(cm_get(&record, 1, "phase")) <= 128;
                    for (unsigned i = 0; accepted && i < count; ++i) accepted = seen[i] != sequence;
                    valid = valid && accepted && count < 64;
                    if (valid)
                    {
                        seen[count] = sequence; if (count++) fputc(',', phases);
                        fwrite(text + marker, 1, bytes - marker, phases);
                    }
                    cm_json_free(&record);
                }
                marker = strlen(tool_marker);
                if (bytes > marker && strncmp(text, tool_marker, marker) == 0)
                {
                    ++tools;
                    if (tools == 1 && bytes - marker < CM_FIELD)
                    { memcpy(fields[CM_TOOLCHAIN], text + marker, bytes - marker); fields[CM_TOOLCHAIN][bytes - marker] = 0; }
                    else fields[CM_TOOLCHAIN][0] = 0;
                }
            }
        }
        line = end ? end + 1 : NULL;
    }
    if (phases)
    {
        fputc(']', phases); char *text = valid && count ? cm_memory(phases) : NULL; fclose(phases);
        if (text && strlen(text) <= 65536) row->s[CM_PHASE_JSON] = cm_keep(c->store, text);
        free(text);
    }
    // Existing log receipts are retained as reported context. They do not
    // authenticate all tools, workers, caches or workloads for qualification.
}
BUSTER_GLOBAL_LOCAL int cm_machine(CmCollection *c, const CmJson *jobs, unsigned job, CmRow *row,
    char fields[CM_FIELD_COUNT][CM_FIELD + 1])
{
    c->source_action_verified = 0;
    unsigned steps = cm_member(jobs, job, "steps"), startup = 0, source_step = 0, startup_count = 0;
    unsigned step_index = 0;
    for (unsigned step = steps ? jobs->tokens[steps].child : 0; step; step = jobs->tokens[step].next)
    {
        ++step_index;
        if (cm_equal(cm_get(jobs, step, "name"), "Machine specifications"))
        {
            startup = step; ++startup_count;
            if (step_index != 2) startup_count += 2;
        }
        if (cm_equal(cm_get(jobs, step, "name"), "Record actual checkout identity")) source_step = step;
    }
    int valid = startup_count == 1 && cm_equal(cm_get(jobs, startup, "conclusion"), "success");
    char endpoint[256], *log = NULL;
    if (valid)
    {
        snprintf(endpoint, sizeof(endpoint), "actions/jobs/%" PRIu64 "/logs", row->job);
        valid = cm_api(c->transport, endpoint, "GET", NULL, 0, &log, NULL);
    }
    if (valid)
    {
        char *original_log = strdup(log);
        unsigned count = 0;
        char *record = cm_log_record(log, "MACHINE_SPECIFICATIONS_JSON ",
            cm_get(jobs, startup, "started_at"), cm_get(jobs, startup, "completed_at"), &count);
        CmJson machine = cm_json_parse(record ? record : "", record ? strlen(record) : 0);
        valid = cm_machine_schema(&machine, "buster-machine-specifications-v1") && cm_bindings(&machine, row, 0);
        if (valid) valid = cm_workflow_proof(c, &machine, row, fields);
        if (valid)
        {
            const struct { unsigned to; const char *from; } mapping[] =
            {
                {CM_HOSTING, "runner_class"}, {CM_CPU_RAW, "cpu_model"}, {CM_OS, "os_name"},
                {CM_OS_VERSION, "os_version"}, {CM_KERNEL, "kernel_release"}, {CM_ARCH, "machine_arch"},
                {CM_PROCESS_ARCH, "process_arch"}, {CM_EFFECTIVE_CPU, "cpu_process_available"},
                {CM_QUOTA, "cpu_quota"}, {CM_MEMORY, "memory_limit_bytes"}, {CM_IMAGE, "runner_image"},
                {CM_IMAGE_VERSION, "runner_image_version"}, {CM_EXECUTION_CONTEXT, "execution_context"}
            };
            for (unsigned i = 0; i < sizeof(mapping) / sizeof(mapping[0]); ++i)
                cm_copy(fields[mapping[i].to], CM_FIELD + 1, cm_machine_value(&machine, mapping[i].from));
            cm_normalize(fields[CM_CPU], CM_FIELD + 1, fields[CM_CPU_RAW]);
            if (strstr(fields[CM_LABELS], "self-hosted") && !cm_equal(fields[CM_HOSTING], "self-hosted")) fields[CM_CPU][0] = 0;
            valid = fields[CM_CPU][0] && (cm_equal(fields[CM_HOSTING], "github-hosted") ||
                cm_equal(fields[CM_HOSTING], "self-hosted"));
            if (valid)
            {
                row->s[CM_MACHINE_JSON] = cm_keep(c->store, record);
                valid = row->s[CM_MACHINE_JSON] != NULL;
                ++c->machine_verified;
                if (fields[CM_MATRIX][0] && strncmp(fields[CM_MATRIX], "unavailable", 11) != 0) ++c->matrix_verified;
                if (original_log) cm_reported_context(c, original_log, row, fields);
                if (original_log && source_step && c->source_action_verified)
                {
                    unsigned source_count = 0;
                    char *source = cm_log_record(original_log, "MACHINE_SOURCE_IDENTITY_JSON ",
                        cm_get(jobs, source_step, "started_at"), cm_get(jobs, source_step, "completed_at"), &source_count);
                    CmJson identity = cm_json_parse(source ? source : "", source ? strlen(source) : 0);
                    int source_valid = cm_machine_schema(&identity, "buster-machine-source-identity-v1") &&
                        cm_bindings(&identity, row, 1) &&
                        cm_equal(cm_machine_value(&identity, "job"), cm_machine_value(&machine, "job")) &&
                        cm_equal(cm_machine_value(&identity, "workflow_sha"), cm_machine_value(&machine, "workflow_sha")) &&
                        cm_sha(cm_machine_value(&identity, "tested_source_sha"));
                    if (source_valid)
                    {
                        row->s[CM_SOURCE_JSON] = cm_keep(c->store, source);
                        source_valid = row->s[CM_SOURCE_JSON] != NULL;
                        cm_copy(fields[CM_TESTED_SHA], CM_FIELD + 1, cm_machine_value(&identity, "tested_source_sha"));
                        snprintf(endpoint, sizeof(endpoint), "git/commits/%s", fields[CM_TESTED_SHA]);
                        CmJson commit = cm_api_json(c->transport, endpoint, "GET", NULL, NULL);
                        const char *tree = cm_get(&commit, cm_member(&commit, 1, "tree"), "sha");
                        if (commit.valid && cm_sha(tree))
                        {
                            cm_copy(fields[CM_TESTED_TREE], CM_FIELD + 1, tree); ++c->source_verified;
                        }
                        cm_json_free(&commit);
                    }
                    cm_json_free(&identity); free(source);
                }
            }
        }
        if (!valid)
        {
            row->s[CM_HARDWARE] = "unavailable-or-conflicting-startup";
            const unsigned clear[] = {CM_CPU_RAW, CM_CPU, CM_HOSTING, CM_OS, CM_OS_VERSION, CM_KERNEL,
                CM_ARCH, CM_PROCESS_ARCH, CM_EFFECTIVE_CPU, CM_QUOTA, CM_MEMORY, CM_IMAGE, CM_IMAGE_VERSION, CM_EXECUTION_CONTEXT};
            for (unsigned i = 0; i < sizeof(clear) / sizeof(clear[0]); ++i) fields[clear[i]][0] = 0;
        }
        cm_json_free(&machine); free(record); free(original_log);
    }
    free(log);
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_seen_job(const CmStore *s, uint64_t run, uint64_t job, uint64_t attempt, int complete)
{
    int result = 0;
    for (unsigned i = 0; i < s->count && !result; ++i)
    {
        const CmRow *r = &s->rows[i];
        if (r->active && r->run == run && r->job == job && r->attempt == attempt)
            result = !complete || !cm_equal(r->s[CM_KIND], "executed") || cm_equal(r->s[CM_HARDWARE], "verified-startup");
    }
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_collect_job(CmCollection *c, const CmJson *j, unsigned job, const CmJson *run, uint64_t attempt)
{
    CmRow row = {0};
    char fields[CM_FIELD_COUNT][CM_FIELD + 1];
    memset(fields, 0, sizeof(fields));
    for (unsigned i = 0; i < CM_FIELD_COUNT; ++i) row.s[i] = fields[i];
    row.run = cm_number(run, 1, "id"); row.job = cm_number(j, job, "id"); row.attempt = attempt;
    row.origin_job = row.job; row.origin_attempt = attempt;
    int valid = row.run && row.job && cm_number(j, job, "run_id") == row.run &&
        cm_number(j, job, "run_attempt") == attempt && cm_equal(cm_get(j, job, "head_sha"), cm_get(run, 1, "head_sha")) &&
        cm_equal(cm_get(j, job, "status"), "completed");
    if (valid && (!c->selected_job || c->selected_job == row.job) &&
        !cm_seen_job(c->store, row.run, row.job, attempt, 1))
    {
        const struct { unsigned to; const char *from; int in_run; } mapping[] =
        {
            {CM_NAME, "name", 0}, {CM_EVENT, "event", 1}, {CM_HEAD_BRANCH, "head_branch", 1},
            {CM_EVENT_SHA, "head_sha", 1}, {CM_WORKFLOW, "path", 1}, {CM_CONCLUSION, "conclusion", 0},
            {CM_STARTED, "started_at", 0}, {CM_COMPLETED, "completed_at", 0},
            {CM_RUN_CONCLUSION, "conclusion", 1}, {CM_CREATED, "created_at", 1}
        };
        for (unsigned i = 0; valid && i < sizeof(mapping) / sizeof(mapping[0]); ++i)
        {
            const char *value = cm_get(mapping[i].in_run ? run : j, mapping[i].in_run ? 1 : job, mapping[i].from);
            valid = strlen(value) <= CM_FIELD;
            if (valid) cm_copy(fields[mapping[i].to], CM_FIELD + 1, value);
        }
        cm_copy(fields[CM_OBSERVED], CM_FIELD + 1, c->observed);
        cm_copy(fields[CM_COLLECTOR], CM_FIELD + 1, c->transport->revision);
        cm_copy(fields[CM_CONTEXT_STATUS], CM_FIELD + 1, "incomplete-producer-context");
        cm_copy(fields[CM_KIND], CM_FIELD + 1, "executed");
        cm_copy(fields[CM_HARDWARE], CM_FIELD + 1, "missing-startup-report");
        cm_copy(fields[CM_ALIAS], CM_FIELD + 1, "physical-execution");
        char *steps = valid ? cm_steps(j, job) : NULL;
        valid = valid && steps && strlen(steps) <= 65536;
        if (valid)
        {
            row.s[CM_STEPS] = steps;
            unsigned labels = cm_member(j, job, "labels"); unsigned used = 0;
            for (unsigned label = labels ? j->tokens[labels].child : 0; label; label = j->tokens[label].next)
            {
                const char *text = cm_value(j, label);
                if (cm_equal(text, "self-hosted")) cm_copy(fields[CM_HOSTING], CM_FIELD + 1, "self-hosted");
                size_t n = strlen(text);
                if (n + used + 2 < CM_FIELD) { if (used) fields[CM_LABELS][used++] = ','; memcpy(fields[CM_LABELS] + used, text, n + 1); used += (unsigned)n; }
                else valid = 0;
            }
            if (cm_equal(fields[CM_CONCLUSION], "skipped") && cm_number(j, job, "runner_id") == 0)
            { cm_copy(fields[CM_KIND], CM_FIELD + 1, "skipped"); ++c->skipped; }
            else if (!cm_number(j, job, "runner_id") && cm_equal(steps, "[]"))
                cm_copy(fields[CM_KIND], CM_FIELD + 1, "controller-metadata");
            else
            {
                unsigned array = cm_member(j, job, "steps");
                for (unsigned step = array ? j->tokens[array].child : 0; step; step = j->tokens[step].next)
                    if (cm_equal(cm_get(j, step, "name"), "Defer macOS runner lane for draft pull request") &&
                        cm_equal(cm_get(j, step, "conclusion"), "success")) cm_copy(fields[CM_KIND], CM_FIELD + 1, "draft-deferral");
                row.elapsed_available = cm_span(fields[CM_STARTED], fields[CM_COMPLETED], &row.seconds);
                unsigned matches = 0; const CmRow *origin = NULL;
                for (unsigned i = 0; i < c->store->count; ++i)
                {
                    const CmRow *earlier = &c->store->rows[i];
                    if (earlier->active && earlier->job == earlier->origin_job && cm_alias(&row, earlier))
                    { ++matches; origin = earlier; }
                }
                if (matches == 1)
                {
                    row.origin_job = origin->origin_job; row.origin_attempt = origin->origin_attempt;
                    cm_copy(fields[CM_ALIAS], CM_FIELD + 1, "exact-carried-forward-fields");
                    // Reuse the independently verified original receipt, never rejoin by display name.
                    for (unsigned i = CM_JOB_KEY; i <= CM_WORKERS; ++i)
                        if (i != CM_NAME && i != CM_EVENT && i != CM_HEAD_BRANCH && i != CM_EVENT_SHA)
                            row.s[i] = origin->s[i];
                    row.s[CM_CONTEXT_STATUS] = origin->s[CM_CONTEXT_STATUS];
                    row.s[CM_MACHINE_JSON] = origin->s[CM_MACHINE_JSON]; row.s[CM_SOURCE_JSON] = origin->s[CM_SOURCE_JSON];
                    row.s[CM_PHASE_JSON] = origin->s[CM_PHASE_JSON];
                }
                else if (matches > 1)
                {
                    row.origin_job = 0; row.origin_attempt = 0; row.elapsed_available = 0;
                    cm_copy(fields[CM_KIND], CM_FIELD + 1, "unresolved-alias");
                    cm_copy(fields[CM_ALIAS], CM_FIELD + 1, "ambiguous-carried-forward-fields"); ++c->store->gaps;
                }
                else
                {
                    cm_machine(c, j, job, &row, fields);
                    ++c->executions;
                }
            }
            if (valid)
            {
                FILE *encoded = tmpfile(); char *text = NULL;
                if (encoded) { cm_emit_row(encoded, &row); text = cm_memory(encoded); fclose(encoded); }
                size_t bytes = text ? strlen(text) : 0;
                valid = text && bytes <= CM_BYTES / 2 - c->new_record_bytes;
                if (valid)
                {
                    int added = cm_add(c->store, &row); valid = added != 0;
                    if (added == 1) { c->new_record_bytes += bytes; if (!c->quiet) fputs(text, stdout); }
                }
                free(text);
            }
        }
        free(steps);
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_collect_run(CmCollection *c, uint64_t id)
{
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "actions/runs/%" PRIu64, id);
    unsigned initial_failures = c->transport->failures;
    CmJson run = cm_api_json(c->transport, endpoint, "GET", NULL, NULL);
    uint64_t attempts = cm_number(&run, 1, "run_attempt");
    int valid = run.valid && cm_number(&run, 1, "id") == id &&
        cm_equal(cm_get(&run, cm_member(&run, 1, "repository"), "full_name"), CM_REPO) &&
        cm_equal(cm_get(&run, 1, "status"), "completed") && attempts > 0 && attempts <= CM_MAX_ATTEMPTS;
    for (uint64_t attempt = 1; valid && attempt <= attempts; ++attempt)
    {
        CmJson previous = {0};
        const CmJson *execution = &run;
        if (attempt < attempts)
        {
            snprintf(endpoint, sizeof(endpoint), "actions/runs/%" PRIu64 "/attempts/%" PRIu64, id, attempt);
            previous = cm_api_json(c->transport, endpoint, "GET", NULL, NULL);
            execution = &previous;
        }
        valid = execution->valid && cm_number(execution, 1, "id") == id &&
            cm_number(execution, 1, "run_attempt") == attempt &&
            cm_equal(cm_get(execution, 1, "head_sha"), cm_get(&run, 1, "head_sha")) &&
            cm_equal(cm_get(execution, 1, "status"), "completed");
        uint64_t total = UINT64_MAX, seen[CM_MAX_JOBS];
        unsigned count = 0;
        for (unsigned page = 1; valid && count < total; ++page)
        {
            snprintf(endpoint, sizeof(endpoint), "actions/runs/%" PRIu64 "/attempts/%" PRIu64 "/jobs?per_page=100&page=%u", id, attempt, page);
            CmJson jobs = cm_api_json(c->transport, endpoint, "GET", NULL, NULL);
            uint64_t declared = cm_number(&jobs, 1, "total_count");
            unsigned array = cm_member(&jobs, 1, "jobs");
            valid = jobs.valid && array && jobs.tokens[array].kind == 'a' && declared <= CM_MAX_JOBS &&
                (total == UINT64_MAX || total == declared);
            total = declared;
            unsigned chunk = 0;
            for (unsigned job = valid ? jobs.tokens[array].child : 0; valid && job; job = jobs.tokens[job].next)
            {
                uint64_t job_id = cm_number(&jobs, job, "id");
                valid = job_id && count < total && count < CM_MAX_JOBS && ++chunk <= 100;
                for (unsigned i = 0; valid && i < count; ++i) valid = seen[i] != job_id;
                if (valid) { seen[count++] = job_id; valid = cm_collect_job(c, &jobs, job, execution, attempt); }
            }
            valid = valid && (count == total || chunk == 100);
            cm_json_free(&jobs);
        }
        cm_json_free(&previous);
    }
    valid = valid && c->transport->failures == initial_failures;
    if (!valid) { ++c->store->gaps; ++c->store->incomplete_runs; ++c->failed; }
    cm_json_free(&run);
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_receipt_known(CmTransport *t, uint64_t id, uint64_t attempt)
{
    int result = 0;
    for (unsigned i = 0; i < t->run_count && !result; ++i)
        result = t->run_cache[i].id == id && t->run_cache[i].attempt == attempt && t->run_cache[i].complete;
    return result;
}
BUSTER_GLOBAL_LOCAL void cm_receipt_record(CmTransport *t, uint64_t id, uint64_t attempt, int complete)
{
    unsigned found = t->run_count;
    for (unsigned i = 0; i < t->run_count; ++i) if (t->run_cache[i].id == id) found = i;
    if (found == t->run_count)
    {
        if (t->run_count < CM_RUN_CACHE) ++t->run_count;
        else { found = t->run_replace; t->run_replace = (t->run_replace + 1) % CM_RUN_CACHE; }
    }
    t->run_cache[found] = (CmRunReceipt){id, attempt, complete};
}
BUSTER_GLOBAL_LOCAL int cm_pending_add(CmTransport *t, uint64_t id)
{
    int found = 0;
    for (unsigned i = 0; i < t->pending_count; ++i) found |= t->pending[i] == id;
    int result = found || t->pending_count < CM_PENDING;
    if (result && !found) t->pending[t->pending_count++] = id;
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_batch_budget(CmTransport *t)
{
    int result = t->requests + 10 < (t->request_limit ? t->request_limit : CM_REQUESTS) &&
        cm_clock() + 35 < t->deadline;
    return result;
}
BUSTER_GLOBAL_LOCAL int cm_discovered_run(CmCollection *c, const CmJson *runs, unsigned run, unsigned *new_runs, unsigned max_runs)
{
    CmTransport *t = c->transport;
    uint64_t id = cm_number(runs, run, "id"), attempt = cm_number(runs, run, "run_attempt");
    int valid = id && attempt && attempt <= CM_MAX_ATTEMPTS;
    if (valid && !cm_receipt_known(t, id, attempt))
    {
        if (cm_equal(cm_get(runs, run, "status"), "completed"))
        {
            valid = *new_runs < max_runs && cm_batch_budget(t);
            if (valid)
            {
                ++*new_runs;
                int complete = cm_collect_run(c, id);
                cm_receipt_record(t, id, attempt, complete);
                if (!complete) valid = cm_pending_add(t, id);
            }
        }
        else valid = cm_pending_add(t, id);
    }
    return valid;
}
BUSTER_GLOBAL_LOCAL int cm_collect_recent(CmCollection *c, unsigned days, unsigned max_runs)
{
    CmTransport *t = c->transport;
    char endpoint[512];
    unsigned new_runs = 0;
    int valid = 1;
    // Pending IDs survive late finalization, failed API reads and old-run reruns.
    uint64_t pending[CM_PENDING]; unsigned pending_count = t->pending_count;
    memcpy(pending, t->pending, pending_count * sizeof(*pending)); t->pending_count = 0;
    for (unsigned i = 0; i < pending_count; ++i)
    {
        int ok = cm_batch_budget(t) && new_runs < max_runs;
        if (ok)
        {
            snprintf(endpoint, sizeof(endpoint), "actions/runs/%" PRIu64, pending[i]);
            CmJson run = cm_api_json(t, endpoint, "GET", NULL, NULL);
            ok = run.valid && cm_number(&run, 1, "id") == pending[i];
            if (ok) ok = cm_discovered_run(c, &run, 1, &new_runs, max_runs);
            else ++c->store->gaps;
            if (!cm_equal(cm_get(&run, 1, "status"), "completed")) ok = 0;
            cm_json_free(&run);
        }
        if (!ok) valid &= cm_pending_add(t, pending[i]);
    }
    time_t since = time(NULL) - (time_t)days * 86400;
    struct tm utc; gmtime_r(&since, &utc);
    char stamp[32]; strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    uint64_t seen[1000], total = UINT64_MAX;
    unsigned count = 0;
    int recent_complete = 1;
    for (unsigned page = 1; valid && recent_complete && count < total && page <= 10; ++page)
    {
        snprintf(endpoint, sizeof(endpoint), "actions/runs?created=%%3E%%3D%s&per_page=100&page=%u", stamp, page);
        CmJson runs = cm_api_json(t, endpoint, "GET", NULL, NULL);
        unsigned array = cm_member(&runs, 1, "workflow_runs");
        uint64_t declared = cm_number(&runs, 1, "total_count");
        valid = runs.valid && array && runs.tokens[array].kind == 'a';
        // GitHub caps filtered run search at 1000. The persisted reverse sweep
        // covers records outside this recent query, without abandoning a cursor.
        total = declared < 1000 ? declared : 1000;
        unsigned chunk = 0;
        for (unsigned run = valid ? runs.tokens[array].child : 0; valid && recent_complete && run; run = runs.tokens[run].next)
        {
            uint64_t id = cm_number(&runs, run, "id");
            valid = id && count < 1000 && ++chunk <= 100;
            for (unsigned i = 0; valid && i < count; ++i) valid = seen[i] != id;
            if (valid)
            {
                seen[count++] = id;
                recent_complete = cm_discovered_run(c, &runs, run, &new_runs, max_runs);
            }
        }
        if (recent_complete && count < total && chunk != 100) valid = 0;
        cm_json_free(&runs);
    }
    if (!recent_complete) ++c->store->gaps;
    // One bounded reverse page per batch discovers missed events and old reruns.
    // Pending work is persisted before advancing beyond its creation timestamp.
    if (valid && cm_batch_budget(t) && new_runs < max_runs)
    {
        if (!t->sweep_before[0]) cm_copy(t->sweep_before, sizeof(t->sweep_before), c->observed);
        snprintf(endpoint, sizeof(endpoint), "actions/runs?created=%%3C%%3D%s&per_page=100&page=%u", t->sweep_before, t->sweep_page ? t->sweep_page : 1);
        CmJson runs = cm_api_json(t, endpoint, "GET", NULL, NULL);
        unsigned array = cm_member(&runs, 1, "workflow_runs");
        int sweep_ok = runs.valid && array && runs.tokens[array].kind == 'a';
        unsigned chunk = 0;
        char oldest[32]; cm_copy(oldest, sizeof(oldest), t->sweep_before);
        for (unsigned run = sweep_ok ? runs.tokens[array].child : 0; sweep_ok && run; run = runs.tokens[run].next)
        {
            const char *created = cm_get(&runs, run, "created_at");
            sweep_ok = ++chunk <= 100 && cm_time(created) >= 0 &&
                cm_discovered_run(c, &runs, run, &new_runs, max_runs);
            if (sweep_ok && cm_time(created) < cm_time(oldest)) cm_copy(oldest, sizeof(oldest), created);
        }
        if (sweep_ok)
        {
            if (chunk < 100) { cm_copy(t->sweep_before, sizeof(t->sweep_before), c->observed); t->sweep_page = 1; }
            else if (cm_equal(oldest, t->sweep_before))
            {
                if (t->sweep_page < 10) ++t->sweep_page;
                else { ++c->store->gaps; sweep_ok = 0; }
            }
            else { cm_copy(t->sweep_before, sizeof(t->sweep_before), oldest); t->sweep_page = 1; }
        }
        else ++c->store->gaps;
        cm_json_free(&runs);
    }
    if (!valid) ++c->store->gaps;
    return valid && recent_complete;
}
#endif
