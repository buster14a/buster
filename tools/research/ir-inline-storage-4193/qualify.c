// Issue #4193 hosted object qualification only; this branch is not for integration.
// Ownership: this C process owns fixed compiler recipes, corpus/status admission,
// fresh outputs and streamed byte/hash inventories. The workflow only bootstraps
// it and supplies independently pinned checkouts. No timing or performance claim.
// Map: sq_run (owned process group/deadline), sq_equal (streamed equality),
// sq_build (existing native generator closure), sq_hash (bounded explicit argv),
// sq_pair (same cwd/source/output spelling), main (423 files x -g0/-g).
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_GLOBAL_LOCAL static
#define SQ_BASE "c14841ae8e286447cf126de59119a887c13c9e36"
#define SQ_HEAD "78ab5060819d7b76123ebc278e6e0e38cbde2029"
#define SQ_FILES 423u
#define SQ_MODES 2u
#define SQ_PATH 4096u
#define SQ_LIST (1024u * 1024u)
#define SQ_BUILD "build/4193-object-diff"
#define SQ_HASH_BATCH 64u

typedef struct SqState SqState;
struct SqState
{
    char roots[2][SQ_PATH];
    char compilers[2][SQ_PATH];
    char drivers[2][SQ_PATH];
    char output[SQ_PATH];
    char inventory[SQ_LIST];
    char* files[SQ_FILES];
    char object_paths[SQ_FILES * SQ_MODES * 2u][SQ_PATH];
    char* objects[SQ_FILES * SQ_MODES * 2u];
    unsigned object_count;
    unsigned compared;
    unsigned refused;
    unsigned differences;
    unsigned diagnostic_differences;
    unsigned hash_batch;
    FILE* rows;
};

BUSTER_GLOBAL_LOCAL bool sq_path(char* result, const char* root, const char* leaf)
{
    int length = snprintf(result, SQ_PATH, "%s/%s", root, leaf);
    return length > 0 && (unsigned)length < SQ_PATH;
}

BUSTER_GLOBAL_LOCAL double sq_clock(void)
{
    struct timespec now;
    bool valid = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
    return valid ? (double)now.tv_sec + (double)now.tv_nsec / 1e9 : 0.0;
}

// Observe with WNOWAIT, clean the exact group while its leader still reserves
// the numeric identity, then reap. A lost child is never used for signalling.
BUSTER_GLOBAL_LOCAL int sq_run(char* const* argv, const char* cwd, const char* output, const char* error, unsigned seconds)
{
    int result = 126;
    int out = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int err = strcmp(output, error) == 0 ? out : open(error, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out >= 0 && err >= 0)
    {
        pid_t child = fork();
        if (child == 0)
        {
            if (setpgid(0, 0) != 0 || dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0 ||
                (cwd && chdir(cwd) != 0)) _exit(126);
            close(out);
            if (err != out) close(err);
            execvp(argv[0], argv);
            _exit(127);
        }
        if (child > 0)
        {
            setpgid(child, child);
            double deadline = sq_clock() + seconds;
            bool owned = true;
            bool observed = false;
            bool timed_out = false;
            while (owned && !observed)
            {
                siginfo_t info = {0};
                int waited = waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT);
                if (waited < 0 && errno != EINTR) owned = false;
                else if (waited == 0 && info.si_pid == child) observed = true;
                if (owned && !observed && sq_clock() >= deadline)
                {
                    timed_out = true;
                    kill(-child, SIGKILL);
                    kill(child, SIGKILL);
                }
                if (owned && !observed)
                {
                    struct timespec pause = {0, 2000000};
                    nanosleep(&pause, 0);
                }
            }
            if (owned && observed)
            {
                kill(-child, SIGKILL);
                int status = 0;
                pid_t reaped;
                do { reaped = waitpid(child, &status, 0); } while (reaped < 0 && errno == EINTR);
                if (reaped == child)
                    result = timed_out ? 124 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
        }
    }
    bool closed = true;
    if (out >= 0) closed &= close(out) == 0;
    if (err >= 0 && err != out) closed &= close(err) == 0;
    if (!closed) result = 126;
    return result;
}

BUSTER_GLOBAL_LOCAL bool sq_absent(const char* path)
{
    struct stat info;
    bool result = lstat(path, &info) != 0 && errno == ENOENT;
    return result;
}

BUSTER_GLOBAL_LOCAL bool sq_read(const char* path, char* bytes, size_t capacity, size_t* length)
{
    FILE* file = fopen(path, "rb");
    bool valid = file != 0;
    size_t count = valid ? fread(bytes, 1, capacity, file) : 0;
    if (valid) valid = count < capacity && !ferror(file) && feof(file);
    if (file && fclose(file) != 0) valid = false;
    if (valid) bytes[count] = 0;
    *length = count;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool sq_equal(const char* left, const char* right, unsigned long long* bytes)
{
    FILE* a = fopen(left, "rb");
    FILE* b = fopen(right, "rb");
    bool valid = a && b;
    bool done = false;
    unsigned long long total = 0;
    while (valid && !done)
    {
        unsigned char first[8192], second[8192];
        size_t na = fread(first, 1, sizeof(first), a);
        size_t nb = fread(second, 1, sizeof(second), b);
        valid = !ferror(a) && !ferror(b) && na == nb && memcmp(first, second, na) == 0;
        total += na;
        done = na < sizeof(first);
    }
    if (a && fclose(a) != 0) valid = false;
    if (b && fclose(b) != 0) valid = false;
    *bytes = total;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool sq_step(SqState* state, const char* name, char* const* argv, const char* cwd, unsigned seconds)
{
    char path[SQ_PATH];
    bool valid = sq_path(path, state->output, name);
    if (valid)
    {
        printf("QUALIFICATION_COMMAND cwd=%s", cwd);
        for (unsigned i = 0; argv[i]; i += 1) printf(" argv[%u]=%s", i, argv[i]);
        printf("\n");
        fflush(stdout);
        valid = sq_run(argv, cwd, path, path, seconds) == 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool sq_revision(SqState* state, unsigned role)
{
    char leaf[64], path[SQ_PATH], text[128];
    snprintf(leaf, sizeof(leaf), "%s-revision.txt", role ? "candidate" : "baseline");
    char* argv[] = {"git", "rev-parse", "HEAD", 0};
    bool valid = sq_step(state, leaf, argv, state->roots[role], 30) && sq_path(path, state->output, leaf);
    size_t length = 0;
    if (valid) valid = sq_read(path, text, sizeof(text), &length);
    if (valid) valid = length == 41 && memcmp(text, role ? SQ_HEAD : SQ_BASE, 40) == 0 && text[40] == '\n';
    return valid;
}

BUSTER_GLOBAL_LOCAL bool sq_build(SqState* state, unsigned role)
{
    char leaf[64];
    const char* name = role ? "candidate" : "baseline";
    snprintf(leaf, sizeof(leaf), "%s-driver-build.log", name);
    char* bootstrap[] = {"clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
        "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-g", "build.c", "-o", state->drivers[role], 0};
    bool valid = sq_step(state, leaf, bootstrap, state->roots[role], 120);
    snprintf(leaf, sizeof(leaf), "%s-configure.log", name);
    char* generate[] = {state->drivers[role], "generate", "--build-directory", SQ_BUILD, "--cc", "clang", "--ci",
        "--linker", "DEFAULT", "--", "-DBUSTER_INCLUDE_TESTS=OFF", "-DBUSTER_UNITY_BUILD=ON",
        "-DBUSTER_BENCH_ALLOCATIONS=OFF", 0};
    if (valid) valid = sq_step(state, leaf, generate, state->roots[role], 300);
    snprintf(leaf, sizeof(leaf), "%s-compiler-build.log", name);
    char* build[] = {state->drivers[role], "build", "--build-directory", SQ_BUILD, "--config", "Release", "-t", "ide",
        "--", "-j2", 0};
    if (valid) valid = sq_step(state, leaf, build, state->roots[role], 900);
    snprintf(leaf, sizeof(leaf), "%s-generated-closure.sha256", name);
    char* closure[] = {"find", SQ_BUILD "/generated", "-type", "f", "-exec", "sha256sum", "--", "{}", "+", 0};
    if (valid) valid = sq_step(state, leaf, closure, state->roots[role], 60);
    snprintf(leaf, sizeof(leaf), "%s-cmake-cache.log", name);
    char* cache[] = {"cat", SQ_BUILD "/CMakeCache.txt", SQ_BUILD "/compile_commands.json", 0};
    if (valid) valid = sq_step(state, leaf, cache, state->roots[role], 60);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool sq_hash(SqState* state, char** paths, unsigned count, const char* cwd)
{
    bool valid = true;
    for (unsigned first = 0; valid && first < count; first += SQ_HASH_BATCH)
    {
        char* argv[SQ_HASH_BATCH + 3u] = {"sha256sum", "--"};
        unsigned batch = count - first < SQ_HASH_BATCH ? count - first : SQ_HASH_BATCH;
        for (unsigned i = 0; i < batch; i += 1) argv[i + 2u] = paths[first + i];
        char leaf[64];
        snprintf(leaf, sizeof(leaf), "sha256-%04u.txt", state->hash_batch++);
        valid = sq_step(state, leaf, argv, cwd, 60);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool sq_inventory(SqState* state)
{
    char path[SQ_PATH];
    char* argv[] = {"git", "ls-files", "-z", "--", "tests", 0};
    bool valid = sq_step(state, "tracked-files.nul", argv, state->roots[1], 30) &&
                 sq_path(path, state->output, "tracked-files.nul");
    size_t length = 0;
    if (valid) valid = sq_read(path, state->inventory, sizeof(state->inventory), &length);
    unsigned count = 0;
    for (size_t start = 0; valid && start < length;)
    {
        char* file = state->inventory + start;
        size_t remaining = length - start;
        char* end = memchr(file, 0, remaining);
        valid = end != 0;
        size_t size = valid ? (size_t)(end - file) : remaining;
        if (valid && size > 6 && memcmp(file, "tests/", 6) == 0 && strcmp(file + size - 2u, ".c") == 0)
        {
            valid = count < SQ_FILES && (count == 0 || strcmp(state->files[count - 1u], file) < 0);
            if (valid) state->files[count++] = file;
        }
        start += size + 1u;
    }
    if (valid) valid = count == SQ_FILES && sq_hash(state, state->files, count, state->roots[1]);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool sq_pair(SqState* state, unsigned file, unsigned mode)
{
    unsigned cell = file * SQ_MODES + mode;
    char output[SQ_PATH], objects[2][SQ_PATH], logs[2][2][SQ_PATH];
    bool valid = sq_path(output, state->output, "current.o");
    int status[2] = {126, 126};
    for (unsigned role = 0; valid && role < 2; role += 1)
    {
        char leaf[64];
        snprintf(leaf, sizeof(leaf), "%04u-%s.o", cell, role ? "candidate" : "baseline");
        valid = sq_path(objects[role], state->output, leaf);
        for (unsigned stream = 0; valid && stream < 2; stream += 1)
        {
            snprintf(leaf, sizeof(leaf), "%04u-%s.%s", cell, role ? "candidate" : "baseline", stream ? "stderr" : "stdout");
            valid = sq_path(logs[role][stream], state->output, leaf);
        }
        if (valid) valid = sq_absent(objects[role]) && (unlink(output) == 0 || errno == ENOENT);
        char* argv[] = {state->compilers[role], "cc", "-c", "-Isrc", "-I" SQ_BUILD "/generated",
                       mode ? "-g" : "-g0", state->files[file], "-o", output, 0};
        if (valid) status[role] = sq_run(argv, state->roots[1], logs[role][0], logs[role][1], 120);
        if (valid && status[role] == 0)
        {
            valid = rename(output, objects[role]) == 0 && state->object_count < SQ_FILES * SQ_MODES * 2u;
            if (valid)
            {
                memcpy(state->object_paths[state->object_count], objects[role], strlen(objects[role]) + 1u);
                state->objects[state->object_count] = state->object_paths[state->object_count];
                state->object_count += 1;
            }
        }
        else if (valid && status[role] == 1) valid = sq_absent(output) && sq_absent(objects[role]);
        else if (valid) valid = false;
    }
    unsigned long long bytes = 0, ignored = 0;
    bool stdout_equal = valid && sq_equal(logs[0][0], logs[1][0], &ignored);
    bool stderr_equal = valid && sq_equal(logs[0][1], logs[1][1], &ignored);
    const char* verdict = "invalid";
    if (valid)
    {
        if (status[0] == 0 && status[1] == 0)
        {
            bool equal = sq_equal(objects[0], objects[1], &bytes) && bytes != 0;
            state->compared += equal;
            state->differences += !equal;
            verdict = equal ? "objects-identical" : "objects-differ";
        }
        else if (status[0] == 1 && status[1] == 1)
        {
            state->refused += 1;
            verdict = "paired-refusal";
        }
        else
        {
            state->differences += 1;
            verdict = "status-differs";
        }
        state->diagnostic_differences += !stdout_equal || !stderr_equal;
    }
    if (fprintf(state->rows, "%u\t%s\t%s\t%d\t%d\t%s\t%llu\t%u\t%u\n", cell, state->files[file],
                mode ? "-g" : "-g0", status[0], status[1], verdict, bytes, (unsigned)stdout_equal, (unsigned)stderr_equal) < 0)
        valid = false;
    return valid;
}


BUSTER_GLOBAL_LOCAL bool sq_controls(SqState* state, char* self)
{
    char output[SQ_PATH], error[SQ_PATH];
    bool valid = sq_path(output, state->output, "control.stdout") && sq_path(error, state->output, "control.stderr");
    char* words[] = {"zero", "one", "signal", "timeout"};
    int expected[] = {0, 1, 128 + SIGTERM, 124};
    for (unsigned i = 0; valid && i < 4; i += 1)
    {
        char* argv[] = {self, "--fixture", words[i], 0};
        valid = sq_run(argv, state->roots[1], output, error, 1) == expected[i];
    }
    FILE* first = valid ? fopen(output, "wb") : 0;
    FILE* second = valid ? fopen(error, "wb") : 0;
    valid = valid && first && second;
    if (first)
    {
        valid &= fwrite("abc\0def", 1, 7, first) == 7;
        valid &= fclose(first) == 0;
    }
    if (second)
    {
        valid &= fwrite("abc\0def", 1, 7, second) == 7;
        valid &= fclose(second) == 0;
    }
    unsigned long long bytes = 0;
    if (valid) valid = sq_equal(output, error, &bytes) && bytes == 7;
    second = valid ? fopen(error, "wb") : 0;
    if (valid) valid = second != 0;
    if (second)
    {
        valid &= fwrite("abc\0deg", 1, 7, second) == 7;
        valid &= fclose(second) == 0;
    }
    if (valid) valid = !sq_equal(output, error, &bytes) && unlink(error) == 0 && !sq_equal(output, error, &bytes);
    printf("QUALIFICATION_CONTROLS pass=%u normal_exit_zero=0 normal_exit_one=1 signal_exit=%d deadline_exit=124 byte_equality=checked\n",
           (unsigned)valid, 128 + SIGTERM);
    return valid;
}

int main(int argc, char** argv)
{
    int result = 1;
    if (argc == 3 && strcmp(argv[1], "--fixture") == 0)
    {
        if (strcmp(argv[2], "zero") == 0) result = 0;
        else if (strcmp(argv[2], "one") == 0) result = 1;
        else if (strcmp(argv[2], "signal") == 0) raise(SIGTERM);
        else if (strcmp(argv[2], "timeout") == 0) { for (;;) pause(); }
    }
    else
    {
        static SqState state;
        char self[SQ_PATH];
        bool valid = argc == 4;
        if (valid) valid = realpath(argv[0], self) && realpath(argv[1], state.roots[0]) && realpath(argv[2], state.roots[1]) &&
                           realpath(argv[3], state.output) && strcmp(state.roots[0], state.roots[1]) != 0;
        if (valid)
        {
            for (unsigned role = 0; valid && role < 2; role += 1)
            {
                char leaf[64];
                snprintf(leaf, sizeof(leaf), "%s-driver", role ? "candidate" : "baseline");
                valid = sq_path(state.drivers[role], state.output, leaf) &&
                        sq_path(state.compilers[role], state.roots[role], SQ_BUILD "/Release/ide") && sq_revision(&state, role);
            }
            char* clang[] = {"clang", "--version", 0};
            char* cmake[] = {"cmake", "--version", 0};
            char* ninja[] = {"ninja", "--version", 0};
            char* git[] = {"git", "--version", 0};
            valid = valid && sq_controls(&state, self) && sq_step(&state, "clang-version.txt", clang, state.roots[1], 30) &&
                    sq_step(&state, "cmake-version.txt", cmake, state.roots[1], 30) &&
                    sq_step(&state, "ninja-version.txt", ninja, state.roots[1], 30) &&
                    sq_step(&state, "git-version.txt", git, state.roots[1], 30);
            for (unsigned role = 0; valid && role < 2; role += 1) valid = sq_build(&state, role);
            if (valid) valid = sq_inventory(&state);
            char rows[SQ_PATH];
            if (valid) valid = sq_path(rows, state.output, "cells.tsv");
            if (valid) state.rows = fopen(rows, "wb");
            valid = valid && state.rows != 0;
            if (valid) valid = fprintf(state.rows, "cell\tsource\tdebug\tbaseline_status\tcandidate_status\tverdict\tobject_bytes\tstdout_equal\tstderr_equal\n") > 0;
            for (unsigned file = 0; valid && file < SQ_FILES; file += 1)
                for (unsigned mode = 0; valid && mode < SQ_MODES; mode += 1) valid = sq_pair(&state, file, mode);
            if (state.rows && fclose(state.rows) != 0) valid = false;
            for (unsigned role = 0; valid && role < 2; role += 1)
            {
                char* clean[] = {"git", "diff", "--exit-code", "--", "src", "tests", 0};
                valid = sq_step(&state, role ? "candidate-clean.txt" : "baseline-clean.txt", clean, state.roots[role], 30) &&
                        sq_revision(&state, role);
            }
            if (valid)
            {
                char* binaries[] = {state.compilers[0], state.compilers[1], state.drivers[0], state.drivers[1], self};
                valid = sq_hash(&state, binaries, 5, state.roots[1]);
                if (valid) valid = sq_hash(&state, state.objects, state.object_count, state.roots[1]);
                if (valid) valid = sq_hash(&state, state.files, SQ_FILES, state.roots[1]);
            }
        }
        bool complete = valid && state.compared + state.refused + state.differences == SQ_FILES * SQ_MODES && state.compared != 0;
        bool pass = complete && state.differences == 0;
        printf("INLINE_STORAGE_4193_OBJECT_DIFF base=%s head=%s files=%u modes=%u cells=%u objects_identical=%u paired_refusals=%u differences=%u diagnostic_differences=%u complete=%u pass=%u\n",
               SQ_BASE, SQ_HEAD, SQ_FILES, SQ_MODES, state.compared + state.refused + state.differences,
               state.compared, state.refused, state.differences, state.diagnostic_differences, (unsigned)complete, (unsigned)pass);
        result = pass ? 0 : 1;
    }
    return result;
}
