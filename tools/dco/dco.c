// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// David's sign-off tooling beside the Git hooks (CONTRIBUTING.md, "Automatic mechanics and revocation" and
// "Auditing sign-offs"). It needs no compiler build: `cc -O2 -o build/dco tools/dco/dco.c`.
//
//   dco status                                  this worktree's opt-in and hooks, read-only
//   dco audit --base REV --head REV [--certification SHA=SOURCE]...
//   dco audit --api-pull FILE --api-commits FILE... [--head SHA] [--certification SHA=SOURCE]...
//   dco compose                                 chain David's commit-msg with the hooks Git already runs
//   dco uncompose                               undo compose, leaving anything changed since
//
// status prints what .githooks/sign-off-lib.sh, the one resolver the hooks share, reports (dco_report), and what Git
// will run. audit reports the sign-off trailers of the commits HEAD has and BASE doesn't, from Git's history or from
// GitHub's pull request API pages (`gh api repos/O/R/pulls/N` and its `/commits?per_page=100&page=K`). Its exit status:
// 0 when every commit has a well-formed Signed-off-by in its trailer block or a supplied current-head record covers it,
// 1 when some don't, 2 when the input is incomplete (shallow or missing history, a short or stale API listing) or the
// command is wrong. A trailer is a recorded claim, not proof of authority: the report says so and verifies nothing
// about who made it. Git's own parser finds the trailer block, run as the commit-msg hook runs it (--no-divider, the
// repository's comment prefix), so the audit counts what the hook writes.
//
// compose handles two layouts. With core.hooksPath unset and hooks in the common .git/hooks, it keeps an existing
// commit-msg as commit-msg.buster-dco-chained and installs an adapter there (dco_adapter_text). With core.hooksPath
// set, it records the value and points core.hooksPath, at the scope it came from (local for global or system), to a
// dispatcher directory in the Git directory whose links run each hook from the recorded directory (dco_dispatch_text).
// Both run the kept commit-msg, then .githooks/commit-msg, then the kept hook again on a copy of the signed message,
// which it must accept unchanged (dco_chain_text). Installing either is not consent to sign.
//
// Layout: running commands (dco_run); the resolver's report (dco_report); status; trailers (dco_classify); commits
// from Git (dco_git_commits) and from the API (dco_api_commits); audit; the generated hooks; compose; uncompose; main.
// It is CI and setup tooling outside the compile path and has no vector path: it reads a PR's few hundred commits.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#pragma GCC diagnostic ignored "-Wunused-function"
#include "../performance/core.c"
#include "../performance/json.c"

#define BUSTER_DCO_TRAILER "Signed-off-by: David Gonzalez Martin <davidgmbb@gmail.com>"
#define BUSTER_DCO_KEY "Signed-off-by: "
#define BUSTER_DCO_API_LIMIT 250u
#define BUSTER_DCO_REPORT_FIELDS 16u
#define BUSTER_DCO_PATH_BYTES 4096u

typedef struct DcoCommit DcoCommit;
struct DcoCommit
{
    char sha[41];
    char *parents;
    char *author;
    char *message;
    bool covered;
};

typedef struct DcoCommits DcoCommits;
struct DcoCommits
{
    DcoCommit *items;
    size_t count;
    size_t capacity;
};

typedef struct DcoCertification DcoCertification;
struct DcoCertification
{
    char sha[41];
    const char *source;
};

typedef struct DcoReport DcoReport;
struct DcoReport
{
    char *names[BUSTER_DCO_REPORT_FIELDS];
    char *values[BUSTER_DCO_REPORT_FIELDS];
    size_t count;
    PerfBuffer text;
};

// The buffer's bytes as a C string: a NUL past its size.
static char *dco_string(PerfBuffer *buffer)
{
    char *result = NULL;
    if (perf_buffer_byte(buffer, 0))
    {
        buffer->size -= 1;
        result = (char *)buffer->data;
    }
    return result;
}

static char *dco_copy(const char *data, size_t size)
{
    char *result = malloc(size + 1);
    if (result != NULL)
    {
        memcpy(result, data, size);
        result[size] = 0;
    }
    return result;
}

// Runs argv without a shell, its standard input from `input` (or /dev/null), its standard output into `output` when
// that is non-null, its standard error discarded when `quiet`. `isolated` runs it in / without the repository's or the
// user's Git configuration, as the commit-msg hook runs git interpret-trailers. Returns the exit status, or -1.
static int dco_run(const char *const argv[], const char *input, PerfBuffer *output, bool quiet, bool isolated)
{
    int result = -1;
    int pipe_ends[2] = {-1, -1};
    bool ready = output == NULL || pipe2(pipe_ends, O_CLOEXEC) == 0;
    pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        int in = open(input == NULL ? "/dev/null" : input, O_RDONLY);
        bool child_ready = in >= 0 && dup2(in, 0) == 0;
        if (child_ready && output != NULL)
        {
            child_ready = dup2(pipe_ends[1], 1) == 1;
        }
        if (child_ready && quiet)
        {
            int null = open("/dev/null", O_WRONLY);
            child_ready = null >= 0 && dup2(null, 2) == 2;
        }
        if (child_ready && isolated)
        {
            const char *names[] = {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT"};
            for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            {
                unsetenv(names[i]);
            }
            child_ready = setenv("GIT_CONFIG_NOSYSTEM", "1", 1) == 0 && setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1) == 0 && chdir("/") == 0;
        }
        if (child_ready)
        {
            execvp(argv[0], (char *const *)argv);
        }
        _exit(127);
    }
    if (child > 0)
    {
        bool reading = output != NULL;
        if (reading)
        {
            close(pipe_ends[1]);
            pipe_ends[1] = -1;
        }
        while (reading)
        {
            unsigned char block[65536];
            ssize_t size = read(pipe_ends[0], block, sizeof(block));
            if (size > 0)
            {
                reading = perf_buffer_append(output, (PerfBytes){.data = block, .size = (size_t)size});
            }
            else
            {
                reading = size < 0 && errno == EINTR;
            }
        }
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR)
        {
        }
        result = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    if (pipe_ends[0] >= 0)
    {
        close(pipe_ends[0]);
    }
    if (pipe_ends[1] >= 0)
    {
        close(pipe_ends[1]);
    }
    return result;
}

// One line of a command's output, its newline dropped, as a new string; NULL when the command fails.
static char *dco_line(const char *const argv[])
{
    char *result = NULL;
    PerfBuffer output = {0};
    if (dco_run(argv, NULL, &output, true, false) == 0 && dco_string(&output) != NULL)
    {
        size_t size = strcspn((const char *)output.data, "\n");
        result = dco_copy((const char *)output.data, size);
    }
    free(output.data);
    return result;
}

// Writes `text` to PATH.buster-dco-new, replacing a leftover one, and names it in `temporary`.
static bool dco_write_temporary(const char *path, const char *text, mode_t mode, char temporary[BUSTER_DCO_PATH_BYTES])
{
    bool result = (size_t)snprintf(temporary, BUSTER_DCO_PATH_BYTES, "%s.buster-dco-new", path) < BUSTER_DCO_PATH_BYTES && (unlink(temporary) == 0 || errno == ENOENT);
    FILE *file = result ? fopen(temporary, "wbx") : NULL;
    result = file != NULL && fputs(text, file) >= 0;
    if (file != NULL)
    {
        result = fclose(file) == 0 && result && chmod(temporary, mode) == 0;
        if (!result)
        {
            unlink(temporary);
        }
    }
    return result;
}

static bool dco_write_file(const char *path, const char *text, mode_t mode)
{
    char temporary[BUSTER_DCO_PATH_BYTES];
    bool result = dco_write_temporary(path, text, mode, temporary);
    if (result && rename(temporary, path) != 0)
    {
        unlink(temporary);
        result = false;
    }
    return result;
}

static bool dco_file_is(const char *path, const char *text)
{
    PerfBuffer content = {0};
    bool result = perf_read_file(path, &content) && perf_bytes_equal(perf_buffer_bytes(&content), perf_literal(text));
    free(content.data);
    return result;
}

static bool dco_exists(const char *path)
{
    struct stat information;
    bool result = lstat(path, &information) == 0;
    return result;
}

// The resolver's report: .githooks/sign-off-lib.sh's dco_report, run in this worktree.
static bool dco_report(DcoReport *report)
{
    const char *const toplevel_argv[] = {"git", "rev-parse", "--show-toplevel", NULL};
    char *toplevel = dco_line(toplevel_argv);
    char library[BUSTER_DCO_PATH_BYTES];
    bool result = toplevel != NULL && (size_t)snprintf(library, sizeof(library), "%s/.githooks/sign-off-lib.sh", toplevel) < sizeof(library) && access(library, R_OK) == 0;
    if (!result)
    {
        fprintf(stderr, "dco: run it inside a Buster working tree that has .githooks/sign-off-lib.sh\n");
    }
    if (result)
    {
        const char *const argv[] = {"sh", "-c", ". \"$0\" && dco_report", library, NULL};
        result = dco_run(argv, NULL, &report->text, false, false) == 0 && dco_string(&report->text) != NULL;
    }
    char *cursor = result ? (char *)report->text.data : NULL;
    while (result && cursor != NULL && *cursor != 0)
    {
        char *end = strchr(cursor, '\n');
        char *equals = strchr(cursor, '=');
        if (end != NULL)
        {
            *end = 0;
        }
        result = equals != NULL && report->count < BUSTER_DCO_REPORT_FIELDS;
        if (result)
        {
            *equals = 0;
            report->names[report->count] = cursor;
            report->values[report->count] = equals + 1;
            report->count += 1;
        }
        cursor = end == NULL ? NULL : end + 1;
    }
    free(toplevel);
    return result;
}

static const char *dco_field(const DcoReport *report, const char *name)
{
    const char *result = "";
    for (size_t i = 0; i < report->count; ++i)
    {
        if (strcmp(report->names[i], name) == 0)
        {
            result = report->values[i];
        }
    }
    return result;
}

static bool dco_is(const DcoReport *report, const char *name, const char *value)
{
    bool result = strcmp(dco_field(report, name), value) == 0;
    return result;
}

static bool dco_executable(const char *path)
{
    struct stat information;
    bool result = stat(path, &information) == 0 && S_ISREG(information.st_mode) && access(path, X_OK) == 0;
    return result;
}

static int dco_status(void)
{
    DcoReport report = {0};
    int result = dco_report(&report) ? 0 : 2;
    if (result == 0)
    {
        bool on = dco_is(&report, "on", "yes");
        const char *source = dco_field(&report, "source");
        printf("DCO setup status (read-only; an opt-in is configuration, not proof of who directs a session)\n");
        printf("worktree: %s\n", dco_field(&report, "worktree"));
        printf("opt-in: %s, from ", on ? "on" : "off");
        if (strcmp(source, "environment") == 0)
        {
            printf("BUSTER_DCO_SIGN_OFF, for the commands that inherit this environment; it overrides everything below\n");
        }
        else if (strcmp(source, "config-opt-out") == 0)
        {
            printf("buster.dcoSignOff in %s Git configuration, an opt-out for every worktree that reads it\n", dco_field(&report, "opt_out_scope"));
        }
        else if (strcmp(source, "worktree") == 0)
        {
            printf("this worktree's marker, which covers this worktree only\n");
        }
        else
        {
            printf("nothing: no opt-in is recorded for this worktree\n");
        }
        printf("  environment: %s\n", dco_is(&report, "environment", "absent") ? "BUSTER_DCO_SIGN_OFF absent" : dco_is(&report, "environment", "enabling") ? "BUSTER_DCO_SIGN_OFF enables" : "BUSTER_DCO_SIGN_OFF set, disabling");
        printf("  Git configuration: %s%s\n", *dco_field(&report, "opt_out_scope") == 0 ? "no opt-out" : "opt-out in ", dco_field(&report, "opt_out_scope"));
        printf("  worktree marker: %s (%s)\n", dco_field(&report, "marker"), dco_field(&report, "marker_state"));
        if (dco_is(&report, "legacy", "present"))
        {
            printf("  legacy: buster.dcoSignOff=davidgmbb in the clone's shared configuration is inert; --maintainer-session removes it\n");
        }
        const char *directory = dco_field(&report, "hooks_dir");
        const char *state = dco_field(&report, "commit_msg");
        char hook[BUSTER_DCO_PATH_BYTES];
        char own[BUSTER_DCO_PATH_BYTES];
        snprintf(hook, sizeof(hook), "%s/commit-msg", directory);
        snprintf(own, sizeof(own), "%s/.githooks/commit-msg", dco_field(&report, "worktree"));
        if (*dco_field(&report, "hooks_path") != 0)
        {
            printf("hooks: Git runs %s (core.hooksPath in %s configuration)\n", directory, dco_field(&report, "hooks_scope"));
        }
        else
        {
            printf("hooks: Git runs %s (core.hooksPath unset)\n", directory);
        }
        bool active = strcmp(state, "direct") == 0 || strcmp(state, "worktree-copy") == 0 || strcmp(state, "adapter") == 0 || strcmp(state, "dispatcher") == 0;
        const char *meaning = strcmp(state, "direct") == 0 ? "this worktree's .githooks/commit-msg"
            : strcmp(state, "worktree-copy") == 0 ? "another worktree's .githooks/commit-msg, which may be another version"
            : strcmp(state, "adapter") == 0 ? "dco compose's adapter, chaining the directory's own commit-msg"
            : strcmp(state, "dispatcher") == 0 ? "dco compose's dispatcher, chaining the recorded hooks directory"
            : strcmp(state, "foreign") == 0 ? "a hook of its own, which dco compose can chain"
            : strcmp(state, "absent") == 0 ? "missing, beside other hooks that dco compose can keep" : "missing; there are no hooks";
        printf("  commit-msg: %s%s\n", meaning, active && !dco_executable(hook) ? " (NOT executable, so Git skips it)" : "");
        if (!dco_executable(own))
        {
            printf("  .githooks/commit-msg: %s\n", access(own, F_OK) == 0 ? "not executable" : "missing");
        }
        if (dco_is(&report, "copying", "yes"))
        {
            printf("operation: a rebase or cherry-pick is in progress; the hook adds nothing until it ends\n");
        }
        if (dco_is(&report, "automatic", "yes"))
        {
            printf("comment prefix: core.commentChar or core.commentString is auto; the hook refuses commits while the opt-in is on\n");
        }
        if (on && active)
        {
            printf("effect: commits here get David's sign-off automatically\n");
        }
        else if (on)
        {
            printf("effect: the opt-in is on but the hook isn't active; run dco compose, or write the trailer yourself, last\n");
        }
        else
        {
            printf("effect: commits here get no automatic sign-off\n");
        }
    }
    free(report.text.data);
    return result;
}

// One commit's sign-off trailers. Git's parser gives the trailer block; a raw line counts as a sign-off when it is
// exactly `Signed-off-by: Name <address>` and in that block.
typedef struct DcoTrailers DcoTrailers;
struct DcoTrailers
{
    PerfBuffer signers;
    PerfBuffer findings;
    size_t valid;
    size_t malformed;
    size_t duplicates;
    size_t outside;
    bool co_author;
};

static bool dco_identity(const char *value, size_t size)
{
    const char *open = memchr(value, '<', size);
    bool result = open != NULL && open > value + 1 && open[-1] == ' ' && open[-2] != ' ' && value[0] != ' ' && size >= 3 && value[size - 1] == '>';
    for (const char *c = value; result && c < open - 1; ++c)
    {
        result = *c != '>';
    }
    bool at = false;
    for (const char *c = result ? open + 1 : value + size; c < value + size - 1; ++c)
    {
        result = result && *c != '<' && *c != '>' && *c != ' ' && *c != '\t';
        at = at || *c == '@';
    }
    result = result && at && open + 2 < value + size - 1;
    return result;
}

static bool dco_line_in(const char *lines, size_t lines_size, const char *line, size_t size, size_t *count)
{
    size_t found = 0;
    const char *cursor = lines;
    const char *end = lines + lines_size;
    while (cursor < end)
    {
        const char *next = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t length = next == NULL ? (size_t)(end - cursor) : (size_t)(next - cursor);
        if (length == size && memcmp(cursor, line, size) == 0)
        {
            found += 1;
        }
        cursor = next == NULL ? end : next + 1;
    }
    *count = found;
    return found != 0;
}

// The comment prefix as Git resolves it here, as the commit-msg hook derives it; `#` outside a repository.
static char *dco_comment_prefix(void)
{
    const char *const argv[] = {"sh", "-c", "echo x | git stripspace --comment-lines", NULL};
    char *result = dco_line(argv);
    size_t size = result == NULL ? 0 : strlen(result);
    if (size > 2 && strcmp(result + size - 2, " x") == 0)
    {
        result[size - 2] = 0;
    }
    else
    {
        free(result);
        result = dco_copy("#", 1);
    }
    return result;
}

// `prefix` is the comment prefix the hook saw (dco_comment_prefix), so that both parse a message alike.
static bool dco_classify(const char *message, const char *prefix, DcoTrailers *trailers)
{
    const char *variable = getenv("TMPDIR");
    char *directory = variable != NULL && *variable != 0 ? realpath(variable, NULL) : NULL;
    char template[BUSTER_DCO_PATH_BYTES];
    snprintf(template, sizeof(template), "%s/buster-dco-message-XXXXXX", directory != NULL ? directory : "/tmp");
    free(directory);
    int descriptor = mkstemp(template);
    size_t size = strlen(message);
    bool result = descriptor >= 0 && write(descriptor, message, size) == (ssize_t)size;
    if (descriptor >= 0)
    {
        result = close(descriptor) == 0 && result;
    }
    PerfBuffer parsed = {0};
    if (result)
    {
        char comment[64];
        snprintf(comment, sizeof(comment), "core.commentChar=%s", prefix);
        const char *const argv[] = {"git", "-c", comment, "interpret-trailers", "--parse", "--no-divider", template, NULL};
        result = dco_run(argv, NULL, &parsed, false, true) == 0 && dco_string(&parsed) != NULL;
    }
    if (descriptor >= 0)
    {
        unlink(template);
    }
    const char *parsed_text = result ? (const char *)parsed.data : "";
    for (const char *cursor = parsed_text; result && *cursor != 0;)
    {
        size_t length = strcspn(cursor, "\n");
        trailers->co_author = trailers->co_author || (length > 15 && strncasecmp(cursor, "co-authored-by:", 15) == 0);
        cursor += length + (cursor[length] == '\n');
    }
    for (const char *cursor = message; result && *cursor != 0;)
    {
        size_t length = strcspn(cursor, "\n");
        size_t indent = strspn(cursor, " \t>");
        if (indent < length && length - indent >= 13 && strncasecmp(cursor + indent, "signed-off-by", 13) == 0)
        {
            size_t key = strlen(BUSTER_DCO_KEY);
            bool canonical = indent == 0 && length > key && memcmp(cursor, BUSTER_DCO_KEY, key) == 0 && dco_identity(cursor + key, length - key);
            size_t count = 0;
            bool in_block = canonical && dco_line_in(parsed_text, parsed.size, cursor, length, &count);
            if (in_block && !dco_line_in((const char *)trailers->signers.data, trailers->signers.size, cursor + key, length - key, &count))
            {
                dco_line_in(parsed_text, parsed.size, cursor, length, &count);
                result = perf_buffer_append(&trailers->signers, (PerfBytes){.data = (const unsigned char *)cursor + key, .size = length - key}) && perf_buffer_byte(&trailers->signers, '\n');
                trailers->valid += 1;
                if (count > 1)
                {
                    trailers->duplicates += 1;
                    char note[64];
                    snprintf(note, sizeof(note), "  DUPLICATE: %zu copies of \"", count);
                    result = result && perf_buffer_append(&trailers->findings, perf_literal(note)) && perf_buffer_append(&trailers->findings, (PerfBytes){.data = (const unsigned char *)cursor, .size = length}) && perf_buffer_append(&trailers->findings, perf_literal("\"; keep one\n"));
                }
            }
            else if (!in_block && indent == 0 && !canonical)
            {
                trailers->malformed += 1;
                result = perf_buffer_append(&trailers->findings, perf_literal("  MALFORMED: \"")) && perf_buffer_append(&trailers->findings, (PerfBytes){.data = (const unsigned char *)cursor, .size = length}) && perf_buffer_append(&trailers->findings, perf_literal("\" isn't `Signed-off-by: Name <address>`\n"));
            }
            else if (!in_block)
            {
                trailers->outside += 1;
                result = perf_buffer_append(&trailers->findings, perf_literal("  OUTSIDE: \"")) && perf_buffer_append(&trailers->findings, (PerfBytes){.data = (const unsigned char *)cursor, .size = length}) && perf_buffer_append(&trailers->findings, perf_literal("\" is not in the trailer block, so it doesn't count\n"));
            }
        }
        cursor += length + (cursor[length] == '\n');
    }
    free(parsed.data);
    return result;
}

static bool dco_add_commit(DcoCommits *commits, const char *sha, size_t sha_size, char *parents, char *author, char *message)
{
    bool result = sha_size == 40 && parents != NULL && author != NULL && message != NULL;
    for (size_t i = 0; result && i < commits->count; ++i)
    {
        result = memcmp(commits->items[i].sha, sha, 40) != 0;
    }
    if (result && commits->count == commits->capacity)
    {
        size_t capacity = commits->capacity == 0 ? 64 : commits->capacity * 2;
        DcoCommit *grown = realloc(commits->items, capacity * sizeof(DcoCommit));
        result = grown != NULL;
        if (result)
        {
            commits->items = grown;
            commits->capacity = capacity;
        }
    }
    if (result)
    {
        DcoCommit *commit = &commits->items[commits->count];
        *commit = (DcoCommit){.parents = parents, .author = author, .message = message};
        memcpy(commit->sha, sha, 40);
        commits->count += 1;
    }
    else
    {
        free(parents);
        free(author);
        free(message);
    }
    return result;
}

static char *dco_resolve(const char *revision)
{
    char object[BUSTER_DCO_PATH_BYTES];
    snprintf(object, sizeof(object), "%s^{commit}", revision);
    const char *const argv[] = {"git", "rev-parse", "--verify", "--quiet", "--end-of-options", object, NULL};
    char *result = dco_line(argv);
    return result;
}

// Git's commits of base..head, oldest first: hash, parents, author and raw message, NUL-separated.
static int dco_git_commits(const char *base_revision, const char *head_revision, char base[41], char head[41], DcoCommits *commits)
{
    int result = 0;
    const char *const shallow_argv[] = {"git", "rev-parse", "--is-shallow-repository", NULL};
    char *shallow = dco_line(shallow_argv);
    char *base_sha = dco_resolve(base_revision);
    char *head_sha = dco_resolve(head_revision);
    if (shallow == NULL || strcmp(shallow, "false") != 0)
    {
        printf("INCOMPLETE: this repository's history is shallow or unreadable; fetch it in full (git fetch --unshallow)\n");
        result = 2;
    }
    else if (base_sha == NULL || head_sha == NULL)
    {
        printf("INCOMPLETE: cannot resolve %s to a commit here; fetch it first\n", base_sha == NULL ? base_revision : head_revision);
        result = 2;
    }
    if (result == 0)
    {
        snprintf(base, 41, "%s", base_sha);
        snprintf(head, 41, "%s", head_sha);
        const char *const merge_base_argv[] = {"git", "merge-base", base_sha, head_sha, NULL};
        char *merge_base = dco_line(merge_base_argv);
        if (merge_base == NULL)
        {
            printf("INCOMPLETE: %s and %s share no history here\n", base, head);
            result = 2;
        }
        free(merge_base);
    }
    PerfBuffer output = {0};
    if (result == 0)
    {
        char range[96];
        snprintf(range, sizeof(range), "%s..%s", base, head);
        const char *const argv[] = {"git", "-c", "log.showSignature=false", "log", "--reverse", "--topo-order", "--no-color", "--format=%H%x00%P%x00%an <%ae>%x00%B%x00", range, "--", NULL};
        if (dco_run(argv, NULL, &output, false, false) != 0 || dco_string(&output) == NULL)
        {
            printf("INCOMPLETE: git log failed on %s\n", range);
            result = 2;
        }
    }
    const char *cursor = result == 0 ? (const char *)output.data : NULL;
    const char *end = cursor == NULL ? NULL : cursor + output.size;
    while (result == 0 && cursor < end)
    {
        cursor += strspn(cursor, "\n");
        const char *fields[4];
        size_t sizes[4];
        size_t field_count = 0;
        while (field_count < 4 && cursor < end)
        {
            fields[field_count] = cursor;
            sizes[field_count] = strlen(cursor);
            cursor += sizes[field_count] + 1;
            field_count += 1;
        }
        if (field_count != 4 || cursor > end || !dco_add_commit(commits, fields[0], sizes[0], dco_copy(fields[1], sizes[1]), dco_copy(fields[2], sizes[2]), dco_copy(fields[3], sizes[3])))
        {
            printf("INCOMPLETE: git log's output for %s..%s doesn't parse\n", base, head);
            result = 2;
        }
        cursor += strspn(cursor, "\n");
    }
    free(output.data);
    free(shallow);
    free(base_sha);
    free(head_sha);
    return result;
}

static uint32_t dco_json_first(const PerfJson *json, uint32_t parent)
{
    uint32_t result = parent < json->node_count && (json->nodes[parent].kind == PERF_JSON_ARRAY || json->nodes[parent].kind == PERF_JSON_OBJECT) && json->nodes[parent].children != 0 ? parent + 1 : UINT32_MAX;
    return result;
}

static uint32_t dco_json_after(const PerfJson *json, uint32_t parent, uint32_t item)
{
    uint32_t result = json->nodes[parent].kind == PERF_JSON_OBJECT ? json->nodes[item + 1].next : json->nodes[item].next;
    result = result < json->nodes[parent].next ? result : UINT32_MAX;
    return result;
}

static char *dco_json_string(const PerfJson *json, uint32_t node)
{
    char *result = NULL;
    if (node < json->node_count && json->nodes[node].kind == PERF_JSON_STRING)
    {
        PerfBytes text = perf_json_text(json, node);
        result = memchr(text.data, 0, text.size) == NULL ? dco_copy((const char *)text.data, text.size) : NULL;
    }
    return result;
}

static bool dco_api_commit(const PerfJson *json, uint32_t object, DcoCommits *commits)
{
    uint32_t commit = perf_json_find(json, object, "commit");
    uint32_t author = perf_json_find(json, commit, "author");
    char *sha = dco_json_string(json, perf_json_find(json, object, "sha"));
    char *name = dco_json_string(json, perf_json_find(json, author, "name"));
    char *email = dco_json_string(json, perf_json_find(json, author, "email"));
    char *message = dco_json_string(json, perf_json_find(json, commit, "message"));
    PerfBuffer parents = {0};
    PerfBuffer identity = {0};
    uint32_t parent_list = perf_json_find(json, object, "parents");
    bool result = sha != NULL && name != NULL && email != NULL && message != NULL && parent_list < json->node_count && json->nodes[parent_list].kind == PERF_JSON_ARRAY;
    for (uint32_t parent = result ? dco_json_first(json, parent_list) : UINT32_MAX; result && parent != UINT32_MAX; parent = dco_json_after(json, parent_list, parent))
    {
        char *parent_sha = dco_json_string(json, perf_json_find(json, parent, "sha"));
        result = parent_sha != NULL && (parents.size == 0 || perf_buffer_byte(&parents, ' ')) && perf_buffer_append(&parents, perf_literal(parent_sha));
        free(parent_sha);
    }
    result = result && dco_string(&parents) != NULL && perf_buffer_append(&identity, perf_literal(name)) && perf_buffer_append(&identity, perf_literal(" <")) && perf_buffer_append(&identity, perf_literal(email)) && perf_buffer_byte(&identity, '>') && dco_string(&identity) != NULL;
    if (result)
    {
        result = dco_add_commit(commits, sha, strlen(sha), (char *)parents.data, (char *)identity.data, message);
        message = NULL;
    }
    else
    {
        free(parents.data);
        free(identity.data);
    }
    free(sha);
    free(name);
    free(email);
    free(message);
    return result;
}

// The API's commits from page files: each an array of commit objects, or an array of such arrays (gh --slurp).
static int dco_api_commits(const char *pull_path, const char *const pages[], size_t page_count, const char *expected_head, char base[41], char head[41], DcoCommits *commits)
{
    int result = 0;
    PerfBuffer pull_text = {0};
    PerfJson pull = {0};
    char *pull_head = NULL;
    char *pull_base = NULL;
    uint32_t total_node = UINT32_MAX;
    if (perf_read_file(pull_path, &pull_text) && perf_json_parse(&pull, perf_buffer_bytes(&pull_text)))
    {
        pull_head = dco_json_string(&pull, perf_json_find(&pull, perf_json_find(&pull, 0, "head"), "sha"));
        pull_base = dco_json_string(&pull, perf_json_find(&pull, perf_json_find(&pull, 0, "base"), "sha"));
        total_node = perf_json_find(&pull, 0, "commits");
    }
    long total = total_node < pull.node_count && pull.nodes[total_node].kind == PERF_JSON_NUMBER ? strtol((const char *)perf_json_text(&pull, total_node).data, NULL, 10) : -1;
    if (pull_head == NULL || pull_base == NULL || strlen(pull_head) != 40 || strlen(pull_base) != 40 || total < 0)
    {
        printf("INCOMPLETE: %s isn't a pull request object with head.sha, base.sha and commits\n", pull_path);
        result = 2;
    }
    else
    {
        snprintf(head, 41, "%s", pull_head);
        snprintf(base, 41, "%s", pull_base);
        if (expected_head != NULL && strcmp(expected_head, pull_head) != 0)
        {
            printf("INCOMPLETE: the pull request's head is %s, not the requested %s; it changed, so fetch both again\n", pull_head, expected_head);
            result = 2;
        }
    }
    for (size_t i = 0; result == 0 && i < page_count; ++i)
    {
        PerfBuffer text = {0};
        PerfJson page = {0};
        bool valid = perf_read_file(pages[i], &text) && perf_json_parse(&page, perf_buffer_bytes(&text)) && page.nodes[0].kind == PERF_JSON_ARRAY;
        for (uint32_t item = valid ? dco_json_first(&page, 0) : UINT32_MAX; valid && item != UINT32_MAX; item = dco_json_after(&page, 0, item))
        {
            if (page.nodes[item].kind == PERF_JSON_ARRAY)
            {
                for (uint32_t inner = dco_json_first(&page, item); valid && inner != UINT32_MAX; inner = dco_json_after(&page, item, inner))
                {
                    valid = dco_api_commit(&page, inner, commits);
                }
            }
            else
            {
                valid = dco_api_commit(&page, item, commits);
            }
        }
        if (!valid)
        {
            printf("INCOMPLETE: %s isn't a page of distinct commits (sha, parents, commit.author, commit.message)\n", pages[i]);
            result = 2;
        }
        perf_json_free(&page);
        free(text.data);
    }
    if (result == 0 && (size_t)total != commits->count)
    {
        printf("INCOMPLETE: the pull request has %ld commits and the pages hold %zu%s; fetch every page\n", total, commits->count, total > BUSTER_DCO_API_LIMIT ? ", and GitHub's commits API lists at most 250, so audit a full clone instead" : "");
        result = 2;
    }
    bool head_found = false;
    for (size_t i = 0; result == 0 && i < commits->count; ++i)
    {
        head_found = head_found || strcmp(commits->items[i].sha, head) == 0;
    }
    if (result == 0 && !head_found)
    {
        printf("INCOMPLETE: the pages don't include the head %s; they predate a push, so fetch them again\n", head);
        result = 2;
    }
    perf_json_free(&pull);
    free(pull_text.data);
    free(pull_head);
    free(pull_base);
    return result;
}

static size_t dco_find(const DcoCommits *commits, const char *sha, size_t size)
{
    size_t result = SIZE_MAX;
    for (size_t i = 0; i < commits->count && result == SIZE_MAX; ++i)
    {
        if (size == 40 && memcmp(commits->items[i].sha, sha, 40) == 0)
        {
            result = i;
        }
    }
    return result;
}

// Marks a record's head and its ancestors in the range as covered, following the commits' own parent lists.
static void dco_cover(DcoCommits *commits, size_t start)
{
    size_t *stack = malloc(commits->count * sizeof(size_t));
    size_t depth = 0;
    if (stack != NULL && !commits->items[start].covered)
    {
        commits->items[start].covered = true;
        stack[depth++] = start;
    }
    while (depth != 0)
    {
        DcoCommit *commit = &commits->items[stack[--depth]];
        for (const char *cursor = commit->parents; *cursor != 0;)
        {
            size_t length = strcspn(cursor, " ");
            size_t parent = dco_find(commits, cursor, length);
            if (parent != SIZE_MAX && !commits->items[parent].covered)
            {
                commits->items[parent].covered = true;
                stack[depth++] = parent;
            }
            cursor += length + (cursor[length] == ' ');
        }
    }
    free(stack);
}

static int dco_audit(int argc, char **argv)
{
    int result = 0;
    const char *base_revision = NULL;
    const char *head_revision = NULL;
    const char *pull = NULL;
    const char *pages[64];
    size_t page_count = 0;
    DcoCertification records[16];
    size_t record_count = 0;
    for (int i = 2; i < argc && result == 0; ++i)
    {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--base") == 0 && has_value)
        {
            base_revision = argv[++i];
        }
        else if (strcmp(argv[i], "--head") == 0 && has_value)
        {
            head_revision = argv[++i];
        }
        else if (strcmp(argv[i], "--api-pull") == 0 && has_value)
        {
            pull = argv[++i];
        }
        else if (strcmp(argv[i], "--api-commits") == 0 && has_value && page_count < 64)
        {
            pages[page_count++] = argv[++i];
        }
        else if (strcmp(argv[i], "--certification") == 0 && has_value && record_count < 16 && strlen(argv[i + 1]) > 41 && argv[i + 1][40] == '=')
        {
            ++i;
            memcpy(records[record_count].sha, argv[i], 40);
            records[record_count].sha[40] = 0;
            records[record_count].source = argv[i] + 41;
            record_count += 1;
        }
        else
        {
            result = 2;
        }
    }
    bool api = pull != NULL || page_count != 0;
    if (result != 0 || (api ? pull == NULL || page_count == 0 || base_revision != NULL : base_revision == NULL || head_revision == NULL))
    {
        fprintf(stderr, "usage: dco audit --base REV --head REV [--certification SHA=SOURCE]...\n       dco audit --api-pull FILE --api-commits FILE... [--head SHA] [--certification SHA=SOURCE]...\n");
        result = 2;
    }
    DcoCommits commits = {0};
    char base[41] = {0};
    char head[41] = {0};
    if (result == 0)
    {
        printf("DCO sign-off audit (structural report: a trailer is a recorded claim, not proof of authority, provenance or certification)\n");
        result = api ? dco_api_commits(pull, pages, page_count, head_revision, base, head, &commits) : dco_git_commits(base_revision, head_revision, base, head, &commits);
    }
    if (result == 0)
    {
        printf("base %s\nhead %s\n%zu commit%s from %s, those the base doesn't reach\n", base, head, commits.count, commits.count == 1 ? "" : "s", api ? "GitHub's pull request API" : "Git's history");
        for (size_t i = 0; i < record_count; ++i)
        {
            size_t index = dco_find(&commits, records[i].sha, 40);
            if (index == SIZE_MAX)
            {
                printf("record %s from %s: names no commit of this range; ignored\n", records[i].sha, records[i].source);
            }
            else
            {
                dco_cover(&commits, index);
                printf("record %s from %s: supplied by the reviewer, not verified here; covers that commit and its ancestors in the range%s\n", records[i].sha, records[i].source, strcmp(records[i].sha, head) == 0 ? "" : "; STALE: the head has moved past it, so later commits need their own");
            }
        }
    }
    size_t signed_count = 0;
    size_t missing = 0;
    size_t covered = 0;
    size_t findings = 0;
    char *prefix = result == 0 ? dco_comment_prefix() : NULL;
    for (size_t i = 0; result == 0 && i < commits.count; ++i)
    {
        DcoCommit *commit = &commits.items[i];
        DcoTrailers trailers = {0};
        if (!dco_classify(commit->message, prefix, &trailers))
        {
            printf("INCOMPLETE: git interpret-trailers failed on %s\n", commit->sha);
            result = 2;
        }
        size_t subject = strcspn(commit->message, "\n");
        printf("- %.12s%s %.*s\n", commit->sha, strchr(commit->parents, ' ') != NULL ? " (merge)" : "", (int)(subject < 72 ? subject : 72), commit->message);
        bool author_signed = false;
        for (const char *cursor = trailers.signers.size == 0 ? "" : dco_string(&trailers.signers); result == 0 && *cursor != 0;)
        {
            size_t length = strcspn(cursor, "\n");
            bool maintainer = length + strlen(BUSTER_DCO_KEY) == strlen(BUSTER_DCO_TRAILER) && memcmp(cursor, &BUSTER_DCO_TRAILER[sizeof(BUSTER_DCO_KEY) - 1], length) == 0;
            author_signed = author_signed || (length == strlen(commit->author) && memcmp(cursor, commit->author, length) == 0);
            printf("  signed: %.*s%s\n", (int)length, cursor, maintainer ? " (David's trailer)" : "");
            cursor += length + (cursor[length] == '\n');
        }
        if (trailers.valid != 0 && !author_signed)
        {
            printf("  note: the author, %s, is not a signer; a DCO signer need not be the author, but check who certifies\n", commit->author);
        }
        if (trailers.valid == 0)
        {
            printf("  MISSING: no well-formed Signed-off-by in the trailer block%s%s\n", trailers.co_author ? "; a Co-authored-by trailer certifies nothing" : "", commit->covered ? "; covered by a supplied record" : "");
            missing += 1;
            covered += commit->covered;
        }
        signed_count += trailers.valid != 0;
        findings += trailers.malformed + trailers.duplicates + trailers.outside;
        if (trailers.findings.size != 0)
        {
            printf("%s", dco_string(&trailers.findings));
        }
        free(trailers.signers.data);
        free(trailers.findings.data);
    }
    if (result == 0)
    {
        printf("summary: %zu signed, %zu missing (%zu covered by a supplied record), %zu trailer finding%s\n", signed_count, missing, covered, findings, findings == 1 ? "" : "s");
        if (missing > covered)
        {
            printf("next: each MISSING commit needs its author's own sign-off, or a PR-head certification as CONTRIBUTING.md's \"Effective point and in-flight work\" describes; an authorized session fixes only its own new commits, and nothing here changes history\n");
            result = 1;
        }
        else
        {
            printf("result: every commit has a sign-off record\n");
        }
    }
    else if (result == 2)
    {
        printf("result: incomplete, so nothing is established\n");
    }
    for (size_t i = 0; i < commits.count; ++i)
    {
        free(commits.items[i].parents);
        free(commits.items[i].author);
        free(commits.items[i].message);
    }
    free(commits.items);
    free(prefix);
    return result;
}

// The generated hooks. compose writes them, uncompose removes them only while they are byte for byte these texts.
#define BUSTER_DCO_CHAIN \
    "# The kept hook runs first and may reject or edit the message. David's\n" \
    "# sign-off (.githooks/commit-msg, which checks his opt-in) then goes last,\n" \
    "# and the kept hook checks a copy of the signed message: it must accept it\n" \
    "# unchanged. BUSTER_DCO_CHAINING stops a hook that calls this one again.\n" \
    "if [ -n \"${BUSTER_DCO_CHAINING-}\" ]; then\n" \
    "    exit 0\n" \
    "fi\n" \
    "BUSTER_DCO_CHAINING=1\n" \
    "export BUSTER_DCO_CHAINING\n" \
    "if [ -x \"$chained\" ]; then\n" \
    "    \"$chained\" \"$@\" || exit\n" \
    "fi\n" \
    "[ -f .githooks/commit-msg ] || exit 0\n" \
    "cp \"$1\" \"$1.buster-dco-before\" || exit\n" \
    "sh .githooks/commit-msg \"$@\"\n" \
    "status=$?\n" \
    "if [ \"$status\" = 0 ] && [ -x \"$chained\" ] && ! cmp -s \"$1\" \"$1.buster-dco-before\"; then\n" \
    "    cp \"$1\" \"$1.buster-dco-check\" || exit\n" \
    "    if ! \"$chained\" \"$1.buster-dco-check\"; then\n" \
    "        echo \"commit-msg: $chained rejects the message once David's sign-off is added.\" >&2\n" \
    "        status=1\n" \
    "    elif ! cmp -s \"$1\" \"$1.buster-dco-check\"; then\n" \
    "        echo \"commit-msg: $chained changes the message again after David's sign-off, so the hooks can't settle on one message; commit refused (CONTRIBUTING.md, \\\"Existing hooks\\\").\" >&2\n" \
    "        status=1\n" \
    "    fi\n" \
    "fi\n" \
    "rm -f \"$1.buster-dco-before\" \"$1.buster-dco-check\"\n" \
    "exit \"$status\"\n"

static const char dco_adapter_text[] =
    "#!/bin/sh\n"
    "# buster-dco-adapter 1\n"
    "# David's sign-off composed with this directory's own commit-msg, which\n"
    "# tools/dco/dco.c compose kept as commit-msg.buster-dco-chained; dco\n"
    "# uncompose puts it back (CONTRIBUTING.md, \"Existing hooks\").\n"
    "chained=\"$(dirname \"$0\")/commit-msg.buster-dco-chained\"\n"
    BUSTER_DCO_CHAIN;

static const char dco_dispatch_text[] =
    "#!/bin/sh\n"
    "# buster-dco-dispatch 1\n"
    "# Runs each hook from the hooks directory that tools/dco/dco.c compose\n"
    "# recorded in ./original, and composes David's sign-off with its\n"
    "# commit-msg; dco uncompose restores core.hooksPath (CONTRIBUTING.md,\n"
    "# \"Existing hooks\"). A relative directory is the worktree's, as Git's is.\n"
    "here=$(dirname \"$0\")\n"
    "original=$(cat \"$here/original\") || exit 1\n"
    "case $original in\n"
    "    /*) ;;\n"
    "    *) original=\"$PWD/$original\" ;;\n"
    "esac\n"
    "name=$(basename \"$0\")\n"
    "if [ \"$name\" != commit-msg ]; then\n"
    "    if [ -x \"$original/$name\" ]; then\n"
    "        exec \"$original/$name\" \"$@\"\n"
    "    fi\n"
    "    exit 0\n"
    "fi\n"
    "chained=\"$original/commit-msg\"\n"
    BUSTER_DCO_CHAIN;

static bool dco_path(char *path, const char *directory, const char *name)
{
    bool result = (size_t)snprintf(path, BUSTER_DCO_PATH_BYTES, "%s/%s", directory, name) < BUSTER_DCO_PATH_BYTES;
    return result;
}

// Links each executable hook of `original` (names without a dot) and commit-msg to the dispatcher.
static bool dco_link_hooks(const char *dispatcher, const char *original)
{
    char path[BUSTER_DCO_PATH_BYTES];
    bool result = dco_path(path, dispatcher, "commit-msg") && (dco_exists(path) || symlink("dispatch", path) == 0);
    PerfBuffer listing = {0};
    const char *const argv[] = {"find", "-H", original, "-mindepth", "1", "-maxdepth", "1", "!", "-name", "*.*", "-printf", "%f\\n", NULL};
    if (result && dco_run(argv, NULL, &listing, true, false) == 0 && dco_string(&listing) != NULL)
    {
        for (char *cursor = (char *)listing.data; result && *cursor != 0;)
        {
            size_t length = strcspn(cursor, "\n");
            cursor[length] = 0;
            char source[BUSTER_DCO_PATH_BYTES];
            if (dco_path(source, original, cursor) && dco_executable(source) && dco_path(path, dispatcher, cursor) && !dco_exists(path))
            {
                result = symlink("dispatch", path) == 0;
            }
            cursor += length + 1;
        }
    }
    free(listing.data);
    return result;
}

// Removes the dispatcher's links and the three files compose writes, then the directory if nothing else is in it.
static bool dco_clear_dispatcher(const char *dispatcher)
{
    char path[BUSTER_DCO_PATH_BYTES];
    const char *const argv[] = {"find", dispatcher, "-mindepth", "1", "-maxdepth", "1", "-type", "l", "-lname", "dispatch", "-delete", NULL};
    bool result = dco_run(argv, NULL, NULL, false, false) == 0;
    const char *names[] = {"dispatch", "original", "restore"};
    for (size_t i = 0; i < 3; ++i)
    {
        result = dco_path(path, dispatcher, names[i]) && (unlink(path) == 0 || errno == ENOENT) && result;
    }
    result = result && rmdir(dispatcher) == 0;
    return result;
}

static int dco_compose(void)
{
    DcoReport report = {0};
    int result = dco_report(&report) ? 0 : 2;
    const char *state = result == 0 ? dco_field(&report, "commit_msg") : "";
    const char *hooks_path = result == 0 ? dco_field(&report, "hooks_path") : "";
    const char *directory = result == 0 ? dco_field(&report, "hooks_dir") : "";
    char path[BUSTER_DCO_PATH_BYTES];
    char kept[BUSTER_DCO_PATH_BYTES];
    if (result != 0)
    {
    }
    else if (strcmp(state, "direct") == 0 || strcmp(state, "worktree-copy") == 0)
    {
        printf("dco compose: Git already runs a .githooks/commit-msg from %s; nothing to do\n", directory);
    }
    else if (strcmp(state, "adapter") == 0)
    {
        dco_path(path, directory, "commit-msg");
        printf("dco compose: %s\n", dco_file_is(path, dco_adapter_text) ? "already composed" : "the adapter changed since compose; left alone");
        result = dco_file_is(path, dco_adapter_text) ? 0 : 1;
    }
    else if (strcmp(state, "dispatcher") == 0)
    {
        char original[BUSTER_DCO_PATH_BYTES];
        PerfBuffer recorded = {0};
        bool valid = dco_path(path, directory, "original") && perf_read_file(path, &recorded) && dco_string(&recorded) != NULL;
        if (valid)
        {
            snprintf(original, sizeof(original), "%s%s%.*s", recorded.data[0] == '/' ? "" : dco_field(&report, "worktree"), recorded.data[0] == '/' ? "" : "/", (int)strcspn((const char *)recorded.data, "\n"), (const char *)recorded.data);
        }
        valid = valid && dco_link_hooks(directory, original);
        printf("dco compose: already composed%s\n", valid ? "; linked any hooks added since" : ", but linking new hooks failed");
        result = valid ? 0 : 1;
        free(recorded.data);
    }
    else if (*hooks_path == 0 && strcmp(state, "none") == 0)
    {
        printf("dco compose: Git has no hooks to keep here; sh .githooks/enable-sign-off.sh installs .githooks itself\n");
    }
    else if (*hooks_path == 0)
    {
        char hooks[BUSTER_DCO_PATH_BYTES];
        struct stat information;
        bool linked = dco_path(hooks, dco_field(&report, "common_dir"), "hooks") && lstat(hooks, &information) == 0 && S_ISLNK(information.st_mode);
        bool valid = !linked && dco_path(path, hooks, "commit-msg") && dco_path(kept, hooks, "commit-msg.buster-dco-chained") && !dco_exists(kept);
        if (!valid)
        {
            printf("dco compose: unsupported: %s\n", linked ? "the hooks directory is a symbolic link, possibly shared with other repositories" : "commit-msg.buster-dco-chained already exists");
            result = 1;
        }
        else
        {
            // The adapter is written first, so a failure leaves the existing commit-msg where it was.
            char temporary[BUSTER_DCO_PATH_BYTES];
            bool keeping = dco_exists(path);
            bool installed = dco_write_temporary(path, dco_adapter_text, 0755, temporary);
            installed = installed && (!keeping || rename(path, kept) == 0);
            if (installed && rename(temporary, path) != 0)
            {
                installed = false;
                if (keeping)
                {
                    rename(kept, path);
                }
            }
            if (!installed)
            {
                printf("dco compose: failed to install %s, so nothing changed: %s\n", path, strerror(errno));
                unlink(temporary);
                result = 1;
            }
        }
        if (result == 0 && valid)
        {
            printf("dco compose: installed %s%s; the other hooks there run as before\n", path, dco_exists(kept) ? ", keeping the existing commit-msg as commit-msg.buster-dco-chained" : "");
        }
    }
    else
    {
        const char *scope = dco_field(&report, "hooks_scope");
        bool worktree = strcmp(scope, "worktree") == 0;
        const char *target = worktree ? "--worktree" : "--local";
        char dispatcher[BUSTER_DCO_PATH_BYTES];
        const char *const previous_argv[] = {"git", "config", target, "--get-all", "core.hooksPath", NULL};
        PerfBuffer previous = {0};
        int previous_status = dco_run(previous_argv, NULL, &previous, true, false);
        bool valid = strcmp(scope, "command") != 0 && dco_path(dispatcher, dco_field(&report, worktree ? "git_dir" : "common_dir"), "buster-dco-hooks") && dco_string(&previous) != NULL && (previous_status == 1 || (previous_status == 0 && strchr((const char *)previous.data, '\n') == (const char *)previous.data + previous.size - 1)) && strchr(hooks_path, '\n') == NULL && !dco_exists(dispatcher);
        if (!valid)
        {
            printf("dco compose: unsupported: core.hooksPath comes from %s configuration%s; nothing changed\n", scope, dco_exists(dispatcher) ? " and the dispatcher directory already exists" : ", or has several values");
            result = 1;
        }
        else
        {
            PerfBuffer restore = {0};
            bool written = perf_buffer_append(&restore, perf_literal(target)) && perf_buffer_byte(&restore, '\n') && perf_buffer_append(&restore, perf_literal(previous_status == 0 ? "present\n" : "absent\n")) && perf_buffer_append(&restore, perf_buffer_bytes(&previous)) && dco_string(&restore) != NULL;
            PerfBuffer original = {0};
            written = written && perf_buffer_append(&original, perf_literal(hooks_path)) && perf_buffer_byte(&original, '\n') && dco_string(&original) != NULL;
            written = written && mkdir(dispatcher, 0755) == 0 && dco_path(path, dispatcher, "dispatch") && dco_write_file(path, dco_dispatch_text, 0755) && dco_path(path, dispatcher, "original") && dco_write_file(path, (const char *)original.data, 0644) && dco_path(path, dispatcher, "restore") && dco_write_file(path, (const char *)restore.data, 0644) && dco_link_hooks(dispatcher, directory);
            const char *const set_argv[] = {"git", "config", target, "core.hooksPath", dispatcher, NULL};
            written = written && dco_run(set_argv, NULL, NULL, false, false) == 0;
            if (!written && dco_exists(dispatcher))
            {
                dco_clear_dispatcher(dispatcher);
            }
            printf(written ? "dco compose: core.hooksPath (%s) now names %s, which runs the hooks of %s\n" : "dco compose: failed while composing %s, %s and %s, so nothing changed\n", target + 2, dispatcher, directory);
            result = written ? 0 : 1;
            free(restore.data);
            free(original.data);
        }
        free(previous.data);
    }
    free(report.text.data);
    return result;
}

// A record compose wrote: `original` is one non-empty line; `restore` is a scope (--local or --worktree), then
// `present` and the one previous value, or `absent`. Anything else was edited, and uncompose leaves it alone.
static bool dco_dispatcher_records(const char *dispatcher, char *restore, char **target, char **state, char **value)
{
    char path[BUSTER_DCO_PATH_BYTES];
    PerfBuffer original = {0};
    bool result = dco_path(path, dispatcher, "original") && perf_read_file(path, &original) && dco_string(&original) != NULL;
    const char *line_end = result ? strchr((const char *)original.data, '\n') : NULL;
    result = result && line_end != NULL && line_end != (const char *)original.data && line_end[1] == 0;
    char *first = strchr(restore, '\n');
    char *second = first == NULL ? NULL : strchr(first + 1, '\n');
    char *third = second == NULL ? NULL : strchr(second + 1, '\n');
    if (result && second != NULL)
    {
        *first = 0;
        *second = 0;
        *target = restore;
        *state = first + 1;
        *value = second + 1;
        bool scope = strcmp(*target, "--local") == 0 || strcmp(*target, "--worktree") == 0;
        bool absent = strcmp(*state, "absent") == 0 && **value == 0;
        bool present = strcmp(*state, "present") == 0 && third != NULL && third != *value && third[1] == 0;
        if (present)
        {
            *third = 0;
        }
        result = scope && (absent || present);
    }
    else
    {
        result = false;
    }
    free(original.data);
    return result;
}

static bool dco_remove_dispatcher(const char *dispatcher, bool *left)
{
    char path[BUSTER_DCO_PATH_BYTES];
    PerfBuffer restore = {0};
    char *target = NULL;
    char *state = NULL;
    char *value = NULL;
    bool found = dco_path(path, dispatcher, "dispatch") && dco_exists(path);
    bool ours = found && dco_file_is(path, dco_dispatch_text) && dco_path(path, dispatcher, "restore") && perf_read_file(path, &restore) && dco_string(&restore) != NULL && dco_dispatcher_records(dispatcher, (char *)restore.data, &target, &state, &value);
    if (found && !ours)
    {
        printf("dco uncompose: %s changed since compose; left alone\n", dispatcher);
        *left = true;
    }
    if (ours)
    {
        bool present = strcmp(state, "present") == 0;
        const char *const current_argv[] = {"git", "config", target, "--get", "core.hooksPath", NULL};
        char *current = dco_line(current_argv);
        if (current != NULL && strcmp(current, dispatcher) == 0)
        {
            const char *const set_argv[] = {"git", "config", target, "core.hooksPath", value, NULL};
            const char *const unset_argv[] = {"git", "config", target, "--unset", "core.hooksPath", NULL};
            bool restored = dco_run(present ? set_argv : unset_argv, NULL, NULL, false, false) == 0;
            printf("dco uncompose: %s core.hooksPath (%s)%s%s\n", restored ? "restored" : "failed to restore", target + 2, present ? " to " : ", unset as before", present ? value : "");
            *left = *left || !restored;
        }
        else
        {
            printf("dco uncompose: core.hooksPath no longer names %s; left as it is\n", dispatcher);
        }
        if (!dco_clear_dispatcher(dispatcher))
        {
            printf("dco uncompose: left %s, which holds files compose didn't write\n", dispatcher);
            *left = true;
        }
        free(current);
    }
    free(restore.data);
    return found;
}

static int dco_uncompose(void)
{
    DcoReport report = {0};
    int result = dco_report(&report) ? 0 : 2;
    bool left = false;
    bool found = false;
    if (result == 0)
    {
        char directory[BUSTER_DCO_PATH_BYTES];
        char path[BUSTER_DCO_PATH_BYTES];
        char kept[BUSTER_DCO_PATH_BYTES];
        dco_path(directory, dco_field(&report, "common_dir"), "hooks");
        dco_path(path, directory, "commit-msg");
        dco_path(kept, directory, "commit-msg.buster-dco-chained");
        PerfBuffer content = {0};
        if (perf_read_file(path, &content) && dco_string(&content) != NULL && strstr((const char *)content.data, "\n# buster-dco-adapter ") != NULL)
        {
            found = true;
            if (!dco_file_is(path, dco_adapter_text))
            {
                printf("dco uncompose: %s changed since compose; left alone\n", path);
                left = true;
            }
            else if (unlink(path) == 0 && (!dco_exists(kept) || rename(kept, path) == 0))
            {
                printf("dco uncompose: removed the adapter from %s%s\n", directory, dco_exists(path) ? " and restored its commit-msg" : "");
            }
            else
            {
                printf("dco uncompose: failed to restore %s: %s\n", path, strerror(errno));
                left = true;
            }
        }
        free(content.data);
        char dispatcher[BUSTER_DCO_PATH_BYTES];
        const char *places[] = {"git_dir", "common_dir"};
        for (size_t i = 0; i < 2; ++i)
        {
            if ((i == 0 || strcmp(dco_field(&report, "git_dir"), dco_field(&report, "common_dir")) != 0) && dco_path(dispatcher, dco_field(&report, places[i]), "buster-dco-hooks"))
            {
                found = dco_remove_dispatcher(dispatcher, &left) || found;
            }
        }
        if (!found)
        {
            printf("dco uncompose: nothing composed here\n");
        }
        result = left ? 1 : 0;
    }
    free(report.text.data);
    return result;
}

int main(int argc, char **argv)
{
    int result = 2;
    const char *command = argc > 1 ? argv[1] : "";
    if (strcmp(command, "status") == 0 && argc == 2)
    {
        result = dco_status();
    }
    else if (strcmp(command, "audit") == 0)
    {
        result = dco_audit(argc, argv);
    }
    else if (strcmp(command, "compose") == 0 && argc == 2)
    {
        result = dco_compose();
    }
    else if (strcmp(command, "uncompose") == 0 && argc == 2)
    {
        result = dco_uncompose();
    }
    else
    {
        fprintf(stderr, "usage: dco status | audit ... | compose | uncompose (tools/dco/dco.c's opening comment)\n");
    }
    fflush(stdout);
    return result;
}
