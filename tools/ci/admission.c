// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Hardware admission policy (#1272): pull requests and merge queues never reach the 9700X, directly or by a dispatch.
// Native correctness and the measurements run after the merge: on main's pushes, on the scheduled runs and on the
// workers those dispatch for first-parent states of main (nightly/SHA branches; docs/ci.md, "Hardware admission").
//
//   admission --self-test        the guard's verdict for every event and ref below, and the checks on fixtures
//   admission WORKFLOW.yml...    every job that could take the host, or dispatch work that does, carries the guard
//
// The guard is BUSTER_ADMISSION_GUARD, character for character: an allowlist of events (push, schedule,
// workflow_dispatch) and refs (main, and for a dispatch a nightly/ worker branch), so that a trigger added later
// (merge_group, pull_request_target, workflow_run, ...) is excluded without anyone remembering to exclude it. It is
// checked by evaluating it, not by trusting its text, on the contexts in admission_cases. A job needs it when:
//   - its runs-on is anything but one GitHub-hosted image label (ubuntu-*, windows-*, macos-*): a self-hosted label, a
//     runner group, a list or an expression could each select the 9700X. Its `if` must be the guard, or
//     `${{ GUARD && (REST) }}`, so that nothing in REST can widen it. It needs the guard itself, before GitHub allocates
//     the runner: skipping its steps after the allocation, or a guard in a job it needs, is not enough.
//   - it can dispatch workflow runs (`actions: write` or `write-all` for the job's token): its `if` carries the guard
//     as above, or it needs a job that does and its `if` calls no status function but success(), so that it is skipped
//     whenever that job is.
// Every workflow sets its top-level permissions, and none grants `actions: write` to all its jobs; a job's `uses:` is
// a workflow of this repository (whose own jobs are checked here) or nothing. A workflow these rules can't read (a
// multi-line `if`, a flow-style permissions map) fails, rather than pass unread. What this does not see: a dispatch
// by a token other than the job's (a secret); none may dispatch 9700x.yml (AGENTS.md, docs/ci.md).
// Administrative tooling outside the compiler: a few thousand short lines, scalar.

// strdup and open_memstream.
#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_ADMISSION_GUARD "((github.event_name == 'push' || github.event_name == 'schedule' || github.event_name == 'workflow_dispatch') && (github.ref == 'refs/heads/main' || (github.event_name == 'workflow_dispatch' && startsWith(github.ref, 'refs/heads/nightly/'))))"
#define BUSTER_ADMISSION_JOBS 128
#define BUSTER_ADMISSION_NAME 128
#define BUSTER_ADMISSION_NEEDS 16
#define BUSTER_ADMISSION_COUNT(array) (sizeof(array) / sizeof((array)[0]))

// --- The expression subset the guard uses: string literals, github.event_name and github.ref, ==, !=, !, &&, ||,
// parentheses and startsWith. Anything else is an error, so that the guard can't come to depend on what this doesn't
// evaluate.

typedef struct AdmissionContext AdmissionContext;
struct AdmissionContext
{
    const char* event;
    const char* ref;
};

typedef enum AdmissionKind AdmissionKind;
enum AdmissionKind
{
    ADMISSION_KIND_ERROR,
    ADMISSION_KIND_BOOL,
    ADMISSION_KIND_STRING,
};

typedef struct AdmissionValue AdmissionValue;
struct AdmissionValue
{
    AdmissionKind kind;
    bool truth;
    const char* text;
    size_t length;
};

typedef struct AdmissionParser AdmissionParser;
struct AdmissionParser
{
    const char* at;
    const AdmissionContext* context;
    bool failed;
};

static void admission_space(AdmissionParser* parser)
{
    while (*parser->at == ' ')
    {
        parser->at += 1;
    }
}

static bool admission_take(AdmissionParser* parser, const char* token)
{
    admission_space(parser);
    const size_t length = strlen(token);
    const bool taken = strncmp(parser->at, token, length) == 0;
    if (taken)
    {
        parser->at += length;
    }
    return taken;
}

static AdmissionValue admission_or(AdmissionParser* parser);

static AdmissionValue admission_string(const char* text, size_t length)
{
    AdmissionValue value = {
        .kind = ADMISSION_KIND_STRING,
        .text = text,
        .length = length,
    };
    return value;
}

static AdmissionValue admission_bool(bool truth)
{
    AdmissionValue value = {
        .kind = ADMISSION_KIND_BOOL,
        .truth = truth,
    };
    return value;
}

static AdmissionValue admission_primary(AdmissionParser* parser)
{
    AdmissionValue value = { .kind = ADMISSION_KIND_ERROR };
    admission_space(parser);
    if (admission_take(parser, "("))
    {
        value = admission_or(parser);
        parser->failed |= !admission_take(parser, ")");
    }
    else if (admission_take(parser, "!"))
    {
        const AdmissionValue operand = admission_primary(parser);
        parser->failed |= operand.kind != ADMISSION_KIND_BOOL;
        value = admission_bool(!operand.truth);
    }
    else if (*parser->at == '\'')
    {
        const char* start = parser->at + 1;
        const char* end = strchr(start, '\'');
        parser->failed |= end == NULL;
        if (end != NULL)
        {
            value = admission_string(start, (size_t)(end - start));
            parser->at = end + 1;
        }
    }
    else if (admission_take(parser, "github.event_name"))
    {
        value = admission_string(parser->context->event, strlen(parser->context->event));
    }
    else if (admission_take(parser, "github.ref"))
    {
        value = admission_string(parser->context->ref, strlen(parser->context->ref));
    }
    else if (admission_take(parser, "startsWith("))
    {
        const AdmissionValue text = admission_or(parser);
        parser->failed |= !admission_take(parser, ",");
        const AdmissionValue prefix = admission_or(parser);
        parser->failed |= !admission_take(parser, ")") || text.kind != ADMISSION_KIND_STRING || prefix.kind != ADMISSION_KIND_STRING;
        // GitHub compares case-insensitively; refs and the prefix here are lower case, so an exact prefix is the same.
        value = admission_bool(text.kind == ADMISSION_KIND_STRING && prefix.kind == ADMISSION_KIND_STRING && text.length >= prefix.length && memcmp(text.text, prefix.text, prefix.length) == 0);
    }
    else
    {
        parser->failed = true;
    }
    return value;
}

static AdmissionValue admission_equality(AdmissionParser* parser)
{
    AdmissionValue value = admission_primary(parser);
    const bool equal = admission_take(parser, "==");
    const bool unequal = !equal && admission_take(parser, "!=");
    if (equal || unequal)
    {
        const AdmissionValue other = admission_primary(parser);
        parser->failed |= value.kind != ADMISSION_KIND_STRING || other.kind != ADMISSION_KIND_STRING;
        const bool same = value.length == other.length && (value.length == 0 || memcmp(value.text, other.text, value.length) == 0);
        value = admission_bool(same == equal);
    }
    return value;
}

static AdmissionValue admission_and(AdmissionParser* parser)
{
    AdmissionValue value = admission_equality(parser);
    while (!parser->failed && admission_take(parser, "&&"))
    {
        const AdmissionValue other = admission_equality(parser);
        parser->failed |= value.kind != ADMISSION_KIND_BOOL || other.kind != ADMISSION_KIND_BOOL;
        value = admission_bool(value.truth && other.truth);
    }
    return value;
}

static AdmissionValue admission_or(AdmissionParser* parser)
{
    AdmissionValue value = admission_and(parser);
    while (!parser->failed && admission_take(parser, "||"))
    {
        const AdmissionValue other = admission_and(parser);
        parser->failed |= value.kind != ADMISSION_KIND_BOOL || other.kind != ADMISSION_KIND_BOOL;
        value = admission_bool(value.truth || other.truth);
    }
    return value;
}

// 1: admitted, 0: not, -1: the expression is outside the subset.
static int admission_evaluate(const char* expression, const AdmissionContext* context)
{
    AdmissionParser parser = {
        .at = expression,
        .context = context,
    };
    const AdmissionValue value = admission_or(&parser);
    admission_space(&parser);
    const bool valid = !parser.failed && *parser.at == 0 && value.kind == ADMISSION_KIND_BOOL;
    return valid ? (value.truth ? 1 : 0) : -1;
}

// --- `if` values ---

// The value without `${{ ... }}`, into `buffer`; false when it doesn't fit or is half wrapped.
static bool admission_unwrap(const char* value, char* buffer, size_t size)
{
    size_t length = strlen(value);
    const bool open = strncmp(value, "${{", 3) == 0;
    const bool close = length >= 2 && strcmp(value + length - 2, "}}") == 0;
    bool valid = open == close && length < size;
    if (valid && open)
    {
        value += 3;
        length -= 5;
    }
    while (valid && length != 0 && value[0] == ' ')
    {
        value += 1;
        length -= 1;
    }
    while (valid && length != 0 && value[length - 1] == ' ')
    {
        length -= 1;
    }
    if (valid)
    {
        memcpy(buffer, value, length);
        buffer[length] = 0;
    }
    return valid;
}

// Whether `text` is one parenthesized group: its first character opens what its last closes (quotes respected).
static bool admission_one_group(const char* text)
{
    const size_t length = strlen(text);
    bool valid = length >= 2 && text[0] == '(' && text[length - 1] == ')';
    int32_t depth = 0;
    bool quoted = false;
    for (size_t i = 0; valid && i < length; i += 1)
    {
        quoted = text[i] == '\'' ? !quoted : quoted;
        depth += !quoted && text[i] == '(' ? 1 : 0;
        depth -= !quoted && text[i] == ')' ? 1 : 0;
        valid = depth > 0 || i + 1 == length;
    }
    return valid && depth == 0 && !quoted;
}

// Whether an `if` value is the guard, or the guard && one parenthesized group.
static bool admission_guarded(const char* value)
{
    char buffer[4096];
    const size_t guard = strlen(BUSTER_ADMISSION_GUARD);
    bool guarded = value != NULL && admission_unwrap(value, buffer, sizeof(buffer)) && strncmp(buffer, BUSTER_ADMISSION_GUARD, guard) == 0;
    if (guarded && buffer[guard] != 0)
    {
        guarded = strncmp(buffer + guard, " && ", 4) == 0 && admission_one_group(buffer + guard + 4);
    }
    return guarded;
}

// Whether an `if` value calls a status function that runs a job whose needs were skipped.
static bool admission_status_function(const char* value)
{
    static const char* const functions[] = { "always(", "failure(", "cancelled(" };
    bool found = false;
    for (size_t i = 0; value != NULL && i < BUSTER_ADMISSION_COUNT(functions); i += 1)
    {
        found |= strstr(value, functions[i]) != NULL;
    }
    return found;
}

// --- Workflows, line by line: the shape this repository's workflows have (two-space indentation, a job's keys at
// four spaces). Anything else that matters here fails.

typedef struct AdmissionJob AdmissionJob;
struct AdmissionJob
{
    char name[BUSTER_ADMISSION_NAME];
    uint32_t line;
    bool hosted;
    bool has_runs_on;
    bool dispatches;
    bool unreadable;
    bool remote_uses;
    char* condition;
    char needs[BUSTER_ADMISSION_NEEDS][BUSTER_ADMISSION_NAME];
    uint32_t need_count;
};

typedef struct AdmissionReport AdmissionReport;
struct AdmissionReport
{
    const char* path;
    uint32_t violations;
    bool quiet;
};

static void admission_violation(AdmissionReport* report, uint32_t line, const char* job, const char* message)
{
    report->violations += 1;
    if (!report->quiet)
    {
        fprintf(stderr, "%s:%u: %s%s%s\n", report->path, line, job != NULL ? job : "", job != NULL ? ": " : "", message);
    }
}

static size_t admission_indent(const char* line)
{
    size_t indent = 0;
    while (line[indent] == ' ')
    {
        indent += 1;
    }
    return indent;
}

// The value after `key:` on a line of the given indentation, or NULL.
static const char* admission_key(const char* line, size_t indent, const char* key)
{
    const size_t length = strlen(key);
    const bool match = admission_indent(line) == indent && strncmp(line + indent, key, length) == 0 && line[indent + length] == ':';
    const char* value = match ? line + indent + length + 1 : NULL;
    while (value != NULL && *value == ' ')
    {
        value += 1;
    }
    return value;
}

static bool admission_hosted_label(const char* value)
{
    static const char* const images[] = { "ubuntu-", "windows-", "macos-" };
    bool hosted = false;
    for (size_t i = 0; i < BUSTER_ADMISSION_COUNT(images); i += 1)
    {
        hosted |= strncmp(value, images[i], strlen(images[i])) == 0;
    }
    for (const char* at = value; hosted && *at != 0; at += 1)
    {
        hosted = (*at >= 'a' && *at <= 'z') || (*at >= '0' && *at <= '9') || *at == '-' || *at == '.';
    }
    return hosted;
}

// Whether a permissions value or block line grants the token `actions: write`.
static bool admission_grants_actions(const char* value)
{
    return strcmp(value, "write-all") == 0 || strcmp(value, "actions: write") == 0;
}

static void admission_add_need(AdmissionJob* job, const char* name, size_t length, AdmissionReport* report)
{
    while (length != 0 && name[0] == ' ')
    {
        name += 1;
        length -= 1;
    }
    while (length != 0 && name[length - 1] == ' ')
    {
        length -= 1;
    }
    if (length == 0 || length >= BUSTER_ADMISSION_NAME || job->need_count == BUSTER_ADMISSION_NEEDS)
    {
        admission_violation(report, job->line, job->name, "a need this check can't read");
    }
    else
    {
        memcpy(job->needs[job->need_count], name, length);
        job->needs[job->need_count][length] = 0;
        job->need_count += 1;
    }
}

static void admission_needs(AdmissionJob* job, const char* value, AdmissionReport* report)
{
    const size_t length = strlen(value);
    if (length >= 2 && value[0] == '[' && value[length - 1] == ']')
    {
        const char* start = value + 1;
        const char* end = value + length - 1;
        while (start < end)
        {
            const char* comma = memchr(start, ',', (size_t)(end - start));
            const char* stop = comma != NULL ? comma : end;
            admission_add_need(job, start, (size_t)(stop - start), report);
            start = stop + 1;
        }
    }
    else if (length != 0)
    {
        admission_add_need(job, value, length, report);
    }
}

static const AdmissionJob* admission_find_job(const AdmissionJob* jobs, uint32_t count, const char* name)
{
    const AdmissionJob* found = NULL;
    for (uint32_t i = 0; i < count; i += 1)
    {
        found = strcmp(jobs[i].name, name) == 0 ? &jobs[i] : found;
    }
    return found;
}

// Checks one workflow's text (NUL-terminated, writable: lines are cut in place).
static void admission_check_text(char* text, AdmissionReport* report)
{
    AdmissionJob* jobs = calloc(BUSTER_ADMISSION_JOBS, sizeof(AdmissionJob));
    uint32_t count = 0;
    // 0 elsewhere, 1 in the top-level permissions block, 2 in `jobs`.
    uint32_t section = 0;
    // In a job: 0 elsewhere, 1 runs-on block, 2 permissions block, 3 needs block.
    uint32_t block = 0;
    bool permissions = false;
    uint32_t number = 0;
    char* line = text;
    while (jobs != NULL && line != NULL)
    {
        char* newline = strchr(line, '\n');
        if (newline != NULL)
        {
            *newline = 0;
        }
        number += 1;
        size_t length = strlen(line);
        while (length != 0 && (line[length - 1] == ' ' || line[length - 1] == '\r'))
        {
            length -= 1;
            line[length] = 0;
        }
        const size_t indent = admission_indent(line);
        const bool blank = line[indent] == 0 || line[indent] == '#';
        AdmissionJob* job = section == 2 && count != 0 ? &jobs[count - 1] : NULL;
        const char* value = NULL;
        if (blank)
        {
        }
        else if (indent == 0)
        {
            section = 0;
            block = 0;
            const char* top = admission_key(line, 0, "permissions");
            if (top != NULL)
            {
                permissions = true;
                section = top[0] == 0 ? 1 : 0;
                if (top[0] != 0 && (admission_grants_actions(top) || top[0] == '{'))
                {
                    admission_violation(report, number, NULL, "the top-level permissions grant every job actions: write, or can't be read");
                }
            }
            section = admission_key(line, 0, "jobs") != NULL ? 2 : section;
        }
        else if (section == 1)
        {
            if (admission_grants_actions(line + indent))
            {
                admission_violation(report, number, NULL, "the top-level permissions grant every job actions: write");
            }
        }
        else if (section == 2 && indent == 2)
        {
            block = 0;
            if (count == BUSTER_ADMISSION_JOBS || length < 4 || line[length - 1] != ':' || length - 3 >= BUSTER_ADMISSION_NAME)
            {
                admission_violation(report, number, NULL, "a job this check can't read");
            }
            else
            {
                AdmissionJob* added = &jobs[count];
                memcpy(added->name, line + 2, length - 3);
                added->name[length - 3] = 0;
                added->line = number;
                count += 1;
            }
        }
        else if (job != NULL && indent == 4)
        {
            block = 0;
            if ((value = admission_key(line, 4, "runs-on")) != NULL)
            {
                job->has_runs_on = true;
                job->hosted = admission_hosted_label(value);
                block = value[0] == 0 ? 1 : 0;
            }
            else if ((value = admission_key(line, 4, "if")) != NULL)
            {
                job->unreadable |= value[0] == '>' || value[0] == '|' || value[0] == 0;
                free(job->condition);
                job->condition = strdup(value);
            }
            else if ((value = admission_key(line, 4, "permissions")) != NULL)
            {
                job->dispatches |= admission_grants_actions(value);
                job->unreadable |= value[0] == '{';
                block = value[0] == 0 ? 2 : 0;
            }
            else if ((value = admission_key(line, 4, "needs")) != NULL)
            {
                admission_needs(job, value, report);
                block = value[0] == 0 ? 3 : 0;
            }
            else if ((value = admission_key(line, 4, "uses")) != NULL)
            {
                job->remote_uses = strncmp(value, "./.github/workflows/", 20) != 0;
            }
        }
        else if (job != NULL && indent > 4)
        {
            if (block == 1)
            {
                // A runs-on block (labels, a group): never one hosted image.
                job->hosted = false;
            }
            else if (block == 2)
            {
                job->dispatches |= indent == 6 && admission_grants_actions(line + indent);
            }
            else if (block == 3 && indent == 6 && line[6] == '-' && line[7] == ' ')
            {
                admission_add_need(job, line + 8, strlen(line + 8), report);
            }
        }
        line = newline != NULL ? newline + 1 : NULL;
    }
    if (jobs == NULL || !permissions)
    {
        admission_violation(report, 1, NULL, "the workflow sets no top-level permissions: its jobs' tokens could dispatch runs");
    }
    for (uint32_t i = 0; jobs != NULL && i < count; i += 1)
    {
        const AdmissionJob* job = &jobs[i];
        const bool guarded = admission_guarded(job->condition);
        bool inherited = false;
        for (uint32_t j = 0; j < job->need_count; j += 1)
        {
            const AdmissionJob* need = admission_find_job(jobs, count, job->needs[j]);
            inherited |= need != NULL && admission_guarded(need->condition);
        }
        inherited = inherited && !admission_status_function(job->condition);
        if (job->unreadable)
        {
            admission_violation(report, job->line, job->name, "an `if` or permissions this check can't read");
        }
        if (job->remote_uses)
        {
            admission_violation(report, job->line, job->name, "calls a workflow outside this repository, whose jobs this check can't see");
        }
        if (job->has_runs_on && !job->hosted && !guarded)
        {
            admission_violation(report, job->line, job->name, "can run on a self-hosted runner (the 9700X) without the admission guard first in its `if`");
        }
        if (job->dispatches && !guarded && !inherited)
        {
            admission_violation(report, job->line, job->name, "can dispatch runs without the admission guard, or a guarded need and no status function");
        }
    }
    for (uint32_t i = 0; jobs != NULL && i < count; i += 1)
    {
        free(jobs[i].condition);
    }
    free(jobs);
}

static uint32_t admission_check_file(const char* path, bool quiet)
{
    AdmissionReport report = {
        .path = path,
        .quiet = quiet,
    };
    FILE* file = fopen(path, "rb");
    char* text = NULL;
    size_t size = 0;
    if (file != NULL)
    {
        FILE* buffer = open_memstream(&text, &size);
        char chunk[65536];
        size_t read = 0;
        while (buffer != NULL && (read = fread(chunk, 1, sizeof(chunk), file)) != 0)
        {
            fwrite(chunk, 1, read, buffer);
        }
        if (buffer != NULL)
        {
            fclose(buffer);
        }
        fclose(file);
    }
    if (text == NULL)
    {
        admission_violation(&report, 0, NULL, "can't be read");
    }
    else
    {
        admission_check_text(text, &report);
    }
    free(text);
    return report.violations;
}

// --- Self-test ---

typedef struct AdmissionCase AdmissionCase;
struct AdmissionCase
{
    const char* event;
    const char* ref;
    bool admitted;
};

// Every trigger that can start a pre-merge or foreign run, and the main/scheduled ones that may reach the host.
static const AdmissionCase admission_cases[] = {
    { "pull_request", "refs/pull/1272/merge", false },
    { "pull_request", "refs/heads/main", false },
    { "pull_request_target", "refs/heads/main", false },
    { "merge_group", "refs/heads/gh-readonly-queue/main/pr-1272-0123456789abcdef0123456789abcdef01234567", false },
    { "merge_group", "refs/heads/main", false },
    { "push", "refs/heads/main", true },
    { "push", "refs/heads/feature", false },
    { "push", "refs/heads/nightly/0123456789abcdef0123456789abcdef01234567", false },
    { "push", "refs/tags/v1", false },
    { "schedule", "refs/heads/main", true },
    { "schedule", "refs/heads/nightly/0123456789abcdef0123456789abcdef01234567", false },
    { "workflow_dispatch", "refs/heads/main", true },
    { "workflow_dispatch", "refs/heads/nightly/0123456789abcdef0123456789abcdef01234567", true },
    { "workflow_dispatch", "refs/heads/feature", false },
    { "workflow_dispatch", "refs/heads/claude/nightly/x", false },
    { "workflow_dispatch", "refs/pull/1272/merge", false },
    { "workflow_dispatch", "refs/tags/v1", false },
    { "workflow_run", "refs/heads/main", false },
    { "workflow_call", "refs/heads/main", false },
    { "repository_dispatch", "refs/heads/main", false },
    { "issue_comment", "refs/heads/main", false },
    { "pull_request_review", "refs/pull/1272/merge", false },
    { "check_suite", "refs/heads/main", false },
    { "", "", false },
};

typedef struct AdmissionFixture AdmissionFixture;
struct AdmissionFixture
{
    const char* what;
    const char* text;
    uint32_t violations;
};

#define BUSTER_ADMISSION_HEAD "name: fixture\non:\n  pull_request:\n  merge_group:\npermissions:\n  contents: read\njobs:\n"
#define BUSTER_ADMISSION_SELF "    runs-on:\n      group: buster-9700x-service-dispatch\n      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]\n"

static const AdmissionFixture admission_fixtures[] = {
    { "hosted jobs need nothing", BUSTER_ADMISSION_HEAD "  test:\n    runs-on: ubuntu-26.04\n    steps:\n      - run: true\n", 0 },
    { "a guarded 9700X job passes", BUSTER_ADMISSION_HEAD "  bench:\n    if: ${{ " BUSTER_ADMISSION_GUARD " && (!cancelled() && needs.a.result == 'success') }}\n" BUSTER_ADMISSION_SELF, 0 },
    { "the guard alone passes", BUSTER_ADMISSION_HEAD "  bench:\n    if: " BUSTER_ADMISSION_GUARD "\n" BUSTER_ADMISSION_SELF, 0 },
    { "an unguarded 9700X job fails", BUSTER_ADMISSION_HEAD "  bench:\n    if: github.event_name != 'pull_request'\n" BUSTER_ADMISSION_SELF, 1 },
    { "a 9700X job without an if fails", BUSTER_ADMISSION_HEAD "  bench:\n" BUSTER_ADMISSION_SELF, 1 },
    { "a guard that a later || widens fails", BUSTER_ADMISSION_HEAD "  bench:\n    if: ${{ " BUSTER_ADMISSION_GUARD " && (true) || github.event_name == 'pull_request' }}\n" BUSTER_ADMISSION_SELF, 1 },
    { "a guard that an unbalanced rest widens fails", BUSTER_ADMISSION_HEAD "  bench:\n    if: ${{ " BUSTER_ADMISSION_GUARD " && (true) || (false) }}\n" BUSTER_ADMISSION_SELF, 1 },
    { "a guard after another condition fails", BUSTER_ADMISSION_HEAD "  bench:\n    if: ${{ always() || " BUSTER_ADMISSION_GUARD " }}\n" BUSTER_ADMISSION_SELF, 1 },
    { "a self-hosted label list fails", BUSTER_ADMISSION_HEAD "  bench:\n    runs-on: [self-hosted, ryzen-9700x]\n", 1 },
    { "a runner chosen by an expression fails", BUSTER_ADMISSION_HEAD "  bench:\n    runs-on: ${{ matrix.runner }}\n", 1 },
    { "a guarded need doesn't cover a 9700X job", BUSTER_ADMISSION_HEAD "  gate:\n    if: " BUSTER_ADMISSION_GUARD "\n    runs-on: ubuntu-26.04\n  bench:\n    needs: gate\n" BUSTER_ADMISSION_SELF, 1 },
    { "an unguarded dispatcher fails", BUSTER_ADMISSION_HEAD "  coordinate:\n    runs-on: ubuntu-26.04\n    permissions:\n      contents: write\n      actions: write\n", 1 },
    { "a write-all dispatcher fails", BUSTER_ADMISSION_HEAD "  coordinate:\n    runs-on: ubuntu-26.04\n    permissions: write-all\n", 1 },
    { "a guarded dispatcher passes", BUSTER_ADMISSION_HEAD "  coordinate:\n    if: ${{ " BUSTER_ADMISSION_GUARD " && (github.ref == 'refs/heads/main') }}\n    runs-on: ubuntu-26.04\n    permissions:\n      actions: write\n", 0 },
    { "a dispatcher that needs a guarded job passes", BUSTER_ADMISSION_HEAD "  prepare:\n    if: " BUSTER_ADMISSION_GUARD "\n    runs-on: ubuntu-26.04\n  window:\n    needs: prepare\n    if: needs.prepare.outputs.window == 'reachable'\n    runs-on: ubuntu-26.04\n    permissions:\n      actions: write\n", 0 },
    { "a dispatcher that needs a guarded job in a block list passes", BUSTER_ADMISSION_HEAD "  prepare:\n    if: " BUSTER_ADMISSION_GUARD "\n    runs-on: ubuntu-26.04\n  window:\n    needs:\n      - test\n      - prepare\n    runs-on: ubuntu-26.04\n    permissions:\n      actions: write\n", 0 },
    { "a dispatcher that runs whatever its guarded need did fails", BUSTER_ADMISSION_HEAD "  prepare:\n    if: " BUSTER_ADMISSION_GUARD "\n    runs-on: ubuntu-26.04\n  window:\n    needs: [prepare]\n    if: always()\n    runs-on: ubuntu-26.04\n    permissions:\n      actions: write\n", 1 },
    { "a dispatcher whose need is unguarded fails", BUSTER_ADMISSION_HEAD "  prepare:\n    runs-on: ubuntu-26.04\n  window:\n    needs: [prepare]\n    runs-on: ubuntu-26.04\n    permissions:\n      actions: write\n", 1 },
    { "top-level actions: write fails", "on: push\npermissions:\n  actions: write\njobs:\n  a:\n    runs-on: ubuntu-26.04\n", 1 },
    { "top-level write-all fails", "on: push\npermissions: write-all\njobs:\n  a:\n    runs-on: ubuntu-26.04\n", 1 },
    { "missing top-level permissions fails", "on: push\njobs:\n  a:\n    runs-on: ubuntu-26.04\n", 1 },
    { "a multi-line if fails", BUSTER_ADMISSION_HEAD "  bench:\n    if: >\n      " BUSTER_ADMISSION_GUARD "\n" BUSTER_ADMISSION_SELF, 2 },
    { "a remote reusable workflow fails", BUSTER_ADMISSION_HEAD "  call:\n    uses: someone/else/.github/workflows/bench.yml@main\n", 1 },
    { "a local reusable workflow passes", BUSTER_ADMISSION_HEAD "  call:\n    uses: ./.github/workflows/bench.yml\n", 0 },
    { "comments and blank lines are skipped", BUSTER_ADMISSION_HEAD "  # bench:\n\n  test:\n    # runs-on: [self-hosted]\n    runs-on: ubuntu-26.04\n", 0 },
};

static int admission_self_test(void)
{
    uint32_t checks = 0;
    uint32_t failures = 0;
    for (size_t i = 0; i < BUSTER_ADMISSION_COUNT(admission_cases); i += 1)
    {
        const AdmissionContext context = {
            .event = admission_cases[i].event,
            .ref = admission_cases[i].ref,
        };
        const int verdict = admission_evaluate(BUSTER_ADMISSION_GUARD, &context);
        checks += 1;
        if (verdict != (admission_cases[i].admitted ? 1 : 0))
        {
            failures += 1;
            fprintf(stderr, "admission: the guard %s %s on %s\n", verdict < 0 ? "fails to evaluate" : verdict ? "admits" : "refuses", admission_cases[i].event, admission_cases[i].ref);
        }
    }
    // The evaluator itself: an expression outside the subset is an error, never a verdict.
    static const char* const invalid[] = { "github.actor == 'x'", "'a' && 'b'", "(github.ref == 'x'", "contains(github.ref, 'x')", "github.ref == 'x' ||" };
    const AdmissionContext context = { .event = "push", .ref = "refs/heads/main" };
    for (size_t i = 0; i < BUSTER_ADMISSION_COUNT(invalid); i += 1)
    {
        checks += 1;
        if (admission_evaluate(invalid[i], &context) != -1)
        {
            failures += 1;
            fprintf(stderr, "admission: evaluated %s\n", invalid[i]);
        }
    }
    checks += 1;
    failures += admission_evaluate("!(github.event_name != 'push') && startsWith(github.ref, 'refs/')", &context) != 1;
    for (size_t i = 0; i < BUSTER_ADMISSION_COUNT(admission_fixtures); i += 1)
    {
        char* text = strdup(admission_fixtures[i].text);
        AdmissionReport report = {
            .path = admission_fixtures[i].what,
            .quiet = true,
        };
        if (text != NULL)
        {
            admission_check_text(text, &report);
        }
        checks += 1;
        if (text == NULL || report.violations != admission_fixtures[i].violations)
        {
            failures += 1;
            fprintf(stderr, "admission: %s: %u violations, expected %u\n", admission_fixtures[i].what, report.violations, admission_fixtures[i].violations);
        }
        free(text);
    }
    printf("admission --self-test: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}

int main(int argc, char** argv)
{
    int status = 2;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        status = admission_self_test();
    }
    else if (argc >= 2)
    {
        uint32_t violations = 0;
        for (int i = 1; i < argc; i += 1)
        {
            violations += admission_check_file(argv[i], false);
        }
        printf("admission: %d workflows, %u violations\n", argc - 1, violations);
        status = violations != 0 ? 1 : 0;
    }
    else
    {
        fputs("usage: admission --self-test | admission WORKFLOW.yml...\n", stderr);
    }
    return status;
}
