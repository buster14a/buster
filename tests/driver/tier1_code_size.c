// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1116: exercise the public size-only command with original tiny sources under all corpus filenames. Every unit
// deliberately defines the same name: matching must stay within its object pair. The complete 34-source Lua corpus
// must qualify without runtime inputs; removing luac.c or refusing the oracle must fail instead of reducing coverage.
// Selecting Lua alone still reports both corpora, with the gate enforced only for Lua; unknown selectors are refused.
// Helper-level tests separately cover malformed manifests, ELF extents, threshold boundaries and fallback accounting.

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char* const size_lua_sources[] = {
    "lapi", "lcode", "lctype", "ldebug", "ldo", "ldump", "lfunc", "lgc", "llex", "lmem", "lobject", "lopcodes",
    "lparser", "lstate", "lstring", "ltable", "ltm", "lundump", "lvm", "lzio", "lauxlib", "lbaselib", "lcorolib",
    "ldblib", "liolib", "lmathlib", "loadlib", "loslib", "lstrlib", "ltablib", "lutf8lib", "linit", "lua", "luac",
};

static bool size_path(char* path, size_t capacity, const char* directory, const char* name)
{
    const int length = snprintf(path, capacity, "%s/%s", directory, name);
    return length > 0 && (size_t)length < capacity;
}

static bool size_source(const char* path)
{
    FILE* file = fopen(path, "w");
    bool ok = file != NULL && fputs("int repeated_name(void) { return 7; }\n", file) >= 0;
    if (file != NULL)
    {
        ok = fclose(file) == 0 && ok;
    }
    return ok;
}

static uint64_t size_now(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

// An exit code, or -1 for a signal, timeout or failure to start/wait. Expected rejection controls require exit 1,
// so a crashed command cannot satisfy a negative control. A timeout kills the whole command's process group.
static int size_run(char* const arguments[], const char* log)
{
    const pid_t child = fork();
    if (child == 0)
    {
        const int descriptor = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (setpgid(0, 0) != 0 || descriptor < 0 || dup2(descriptor, STDOUT_FILENO) < 0 || dup2(descriptor, STDERR_FILENO) < 0)
        {
            _exit(126);
        }
        close(descriptor);
        execvp(arguments[0], arguments);
        _exit(127);
    }
    int result = -1;
    if (child > 0)
    {
        const uint64_t deadline = size_now() + UINT64_C(180000000000);
        int status = 0;
        pid_t waited = 0;
        setpgid(child, child);
        while (waited == 0 && size_now() < deadline)
        {
            waited = waitpid(child, &status, WNOHANG);
            if (waited < 0 && errno == EINTR)
            {
                waited = 0;
            }
            if (waited == 0)
            {
                const struct timespec delay = { .tv_nsec = 10000000 };
                nanosleep(&delay, NULL);
            }
        }
        if (waited == 0)
        {
            kill(-child, SIGKILL);
            do
            {
                waited = waitpid(child, &status, 0);
            }
            while (waited < 0 && errno == EINTR);
        }
        if (waited == child && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
    }
    return result;
}

static bool size_field(const char* line, const char* field, uint64_t expected)
{
    const char* value = strstr(line, field);
    char* end = NULL;
    errno = 0;
    const unsigned long long parsed = value != NULL ? strtoull(value + strlen(field), &end, 10) : 0;
    return value != NULL && end != value + strlen(field) && errno != ERANGE &&
           (*end == '\t' || *end == '\n') && parsed == expected;
}

static bool size_summaries(const char* log, bool sqlite_enforced)
{
    FILE* file = fopen(log, "r");
    bool ok = file != NULL;
    unsigned sqlite = 0;
    unsigned lua = 0;
    char line[4096];
    while (ok && fgets(line, sizeof(line), file) != NULL)
    {
        const bool sqlite_line = strncmp(line, "tier1-code-size\tcorpus=sqlite/tier1\t", strlen("tier1-code-size\tcorpus=sqlite/tier1\t")) == 0;
        const bool lua_line = strncmp(line, "tier1-code-size\tcorpus=lua/tier1\t", strlen("tier1-code-size\tcorpus=lua/tier1\t")) == 0;
        if (sqlite_line || lua_line)
        {
            const uint64_t units = sqlite_line ? 1 : 34;
            const uint64_t enforced = sqlite_line ? sqlite_enforced : 1;
            ok = size_field(line, "\tunits=", units) && size_field(line, "\tcompiled=", units) &&
                 size_field(line, "\tmatched=", units) && size_field(line, "\tfallbacks=", 0) &&
                 size_field(line, "\tunmatched=", 0) && size_field(line, "\tenforced=", enforced) && strstr(line, "\tgate=pass\t") != NULL;
            sqlite += sqlite_line;
            lua += lua_line;
        }
        // The sizes-only path must not silently run the timed campaign or require speedtest1/scripts.
        ok = strncmp(line, "tier1-programs-run\t", strlen("tier1-programs-run\t")) != 0 && ok;
    }
    if (file != NULL)
    {
        ok = !ferror(file) && ok;
        ok = fclose(file) == 0 && ok;
    }
    return ok && sqlite == 1 && lua == 1;
}

static bool size_has(const char* log, const char* first, const char* second)
{
    FILE* file = fopen(log, "r");
    bool found = false;
    char line[4096];
    while (file != NULL && !found && fgets(line, sizeof(line), file) != NULL)
    {
        found = strstr(line, first) != NULL && (second == NULL || strstr(line, second) != NULL);
    }
    if (file != NULL)
    {
        found = !ferror(file) && found;
        found = fclose(file) == 0 && found;
    }
    return found;
}

int main(int argc, char** argv)
{
    bool ok = argc == 3;
    if (!ok)
    {
        fprintf(stderr, "usage: %s BUSTER_COMPILER CLANG\n", argv[0]);
    }
    const char* temporary = getenv("TMPDIR");
    temporary = temporary != NULL && temporary[0] != 0 ? temporary : "/tmp";
    char directory[PATH_MAX];
    ok = size_path(directory, sizeof(directory), temporary, "buster-tier1-code-size-XXXXXX") && ok;
    const bool made = ok && mkdtemp(directory) != NULL;
    ok = ok && made;
    char source[PATH_MAX];
    char log[PATH_MAX];
    ok = size_path(source, sizeof(source), directory, "sqlite3.c") && size_path(log, sizeof(log), directory, "command.txt") && ok;
    ok = ok && size_source(source);
    for (unsigned i = 0; ok && i < sizeof(size_lua_sources) / sizeof(size_lua_sources[0]); i += 1)
    {
        char name[32];
        snprintf(name, sizeof(name), "%s.c", size_lua_sources[i]);
        ok = size_path(source, sizeof(source), directory, name) && size_source(source);
    }
    if (ok)
    {
        char* arguments[] = { argv[1], "measure-tier1-programs", "--sizes-only", "--check-code-size", "--clang", argv[2],
                              "--sqlite", directory, "--lua", directory, "--directory", directory, NULL };
        ok = size_run(arguments, log) == 0 && size_summaries(log, true);
        printf("Tier 1 code-size complete corpora: %s\n", ok ? "pass" : "FAIL");
        if (ok)
        {
            arguments[3] = "--check-code-size=lua";
            ok = size_run(arguments, log) == 0 && size_summaries(log, false);
            printf("Tier 1 code-size Lua-only enforcement: %s\n", ok ? "pass" : "FAIL");
        }
        if (ok)
        {
            char* combined[14] = { 0 };
            memcpy(combined, arguments, 12 * sizeof(char*));
            combined[12] = "--check-code-size=sqlite";
            ok = size_run(combined, log) == 0 && size_summaries(log, true);
            printf("Tier 1 code-size combined corpus selectors: %s\n", ok ? "pass" : "FAIL");
        }
        if (ok)
        {
            arguments[3] = "--check-code-size=invalid";
            ok = size_run(arguments, log) == 1 && size_has(log, "usage:", "measure-tier1-programs");
            printf("Tier 1 code-size invalid corpus selector: %s\n", ok ? "pass" : "FAIL");
        }
        arguments[3] = "--check-code-size";
        if (ok)
        {
            ok = size_path(source, sizeof(source), directory, "luac.c") && unlink(source) == 0;
            ok = ok && size_run(arguments, log) == 1 && size_has(log, "code-size compile failed:", "luac.c");
            printf("Tier 1 code-size missing luac: %s\n", ok ? "pass" : "FAIL");
        }
        if (ok)
        {
            // Only the oracle changes. A missing Clang object must never become an empty passing denominator.
            arguments[5] = "/bin/false";
            ok = size_run(arguments, log) == 1 && size_has(log, "code-size compile failed:", "sqlite3.c");
            printf("Tier 1 code-size oracle failure: %s\n", ok ? "pass" : "FAIL");
        }
        if (ok)
        {
            ok = size_path(source, sizeof(source), directory, "sqlite3.c");
            char object[PATH_MAX];
            ok = size_path(object, sizeof(object), directory, "invalid.o") && ok;
            char* invalid[] = { argv[1], "--tier1-function-sizes", "-c", source, "-o", object, NULL };
            ok = ok && size_run(invalid, log) == 1 && size_has(log, "--tier1-function-sizes", "needs --tier1");
            printf("Tier 1 code-size option requires Tier 1: %s\n", ok ? "pass" : "FAIL");
        }
    }
    if (ok)
    {
        char* remove[] = { "rm", "-rf", "--", directory, NULL };
        ok = size_run(remove, "/dev/null") == 0;
    }
    if (!ok && made)
    {
        fprintf(stderr, "Tier 1 code-size regression artifacts retained in %s\n", directory);
    }
    return ok ? 0 : 1;
}
