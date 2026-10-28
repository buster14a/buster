// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// CLI regressions for the self-test workspace lifetime (#575). Run from the repository root with the compiler and
// Clang paths. Each child gets a private TMPDIR; check its status, actual files and reported artifact path separately.
// Expected failures keep their evidence inside this driver's workspace, which rm removes only after every case passes.
// The driver also acts as a failing compiler and as a failing or no-op rm through a private symlink, without shell fixtures.

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum LifecycleKind LifecycleKind;
enum LifecycleKind
{
    LIFECYCLE_DEFAULT,
    LIFECYCLE_KEEP,
    LIFECYCLE_BUILD_FAILURE,
    LIFECYCLE_SUITE_FAILURE,
    LIFECYCLE_CLEANUP_FAILURE,
    LIFECYCLE_RELATIVE_TMPDIR,
    LIFECYCLE_INVALID_OPTION,
    LIFECYCLE_CLEANUP_NO_OP,
};

typedef struct LifecycleCase LifecycleCase;
struct LifecycleCase
{
    const char* name;
    int status;
    bool retained;
    LifecycleKind kind;
};

static const LifecycleCase lifecycle_cases[] =
{
    {
        .name = "default",
        .status = 0,
        .retained = false,
        .kind = LIFECYCLE_DEFAULT,
    },
    {
        .name = "keep",
        .status = 0,
        .retained = true,
        .kind = LIFECYCLE_KEEP,
    },
    {
        .name = "build-failure",
        .status = 2,
        .retained = true,
        .kind = LIFECYCLE_BUILD_FAILURE,
    },
    {
        .name = "suite-failure",
        .status = 1,
        .retained = true,
        .kind = LIFECYCLE_SUITE_FAILURE,
    },
    {
        .name = "cleanup-failure",
        .status = 1,
        .retained = true,
        .kind = LIFECYCLE_CLEANUP_FAILURE,
    },
    {
        .name = "relative-tmpdir",
        .status = 0,
        .retained = false,
        .kind = LIFECYCLE_RELATIVE_TMPDIR,
    },
    {
        .name = "invalid-option",
        .status = 2,
        .retained = false,
        .kind = LIFECYCLE_INVALID_OPTION,
    },
    {
        .name = "cleanup-no-op",
        .status = 1,
        .retained = true,
        .kind = LIFECYCLE_CLEANUP_NO_OP,
    },
};

static bool lifecycle_path(char* output, size_t capacity, const char* directory, const char* name)
{
    const int length = snprintf(output, capacity, "%s/%s", directory, name);
    return length >= 0 && (size_t)length < capacity;
}

static int lifecycle_wait(pid_t child)
{
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    const int result = child > 0 && waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

static bool lifecycle_remove(const char* directory)
{
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(30);
        execlp("rm", "rm", "-rf", "--", directory, (char*)0);
        _exit(127);
    }
    const int status = lifecycle_wait(child);
    struct stat metadata;
    const bool removed = status == 0 && lstat(directory, &metadata) < 0 && errno == ENOENT;
    return removed;
}

static bool lifecycle_workspace(const char* temporary, bool retained, char* artifact, size_t capacity)
{
    DIR* directory = opendir(temporary);
    bool valid = directory != 0;
    unsigned count = 0;
    while (valid)
    {
        errno = 0;
        struct dirent* entry = readdir(directory);
        if (!entry)
        {
            valid = errno == 0;
            break;
        }
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            ++count;
            struct stat metadata;
            valid = lifecycle_path(artifact, capacity, temporary, entry->d_name) && lstat(artifact, &metadata) == 0 && S_ISDIR(metadata.st_mode);
        }
    }
    if (directory)
    {
        valid = closedir(directory) == 0 && valid;
    }
    if (valid && retained)
    {
        char evidence[PATH_MAX];
        struct stat metadata;
        valid = lifecycle_path(evidence, sizeof(evidence), artifact, "fixture-build.json") && lstat(evidence, &metadata) == 0 && S_ISREG(metadata.st_mode) && metadata.st_size > 0;
    }
    return valid && count == (unsigned)retained;
}

static bool lifecycle_report(const char* log, const char* artifact, const LifecycleCase* test)
{
    FILE* file = fopen(log, "r");
    bool valid = file != 0;
    bool summary = false;
    bool cleanup_failed = false;
    char line[PATH_MAX + 512];
    while (file && fgets(line, sizeof(line), file))
    {
        if (strncmp(line, "differential harness: ", sizeof("differential harness: ") - 1) == 0 && strstr(line, "compiler campaigns=0"))
        {
            summary = true;
            line[strcspn(line, "\n")] = 0;
            const char* named = strstr(line, "; artifacts=");
            valid = valid && (named != 0) == test->retained;
            if (named)
            {
                valid = valid && artifact[0] && strcmp(named + sizeof("; artifacts=") - 1, artifact) == 0;
            }
        }
        cleanup_failed = cleanup_failed || strstr(line, "FAIL remove passing artifacts") != 0;
    }
    if (file)
    {
        valid = !ferror(file) && valid;
        valid = fclose(file) == 0 && valid;
    }
    const bool expected_cleanup_failure = test->kind == LIFECYCLE_CLEANUP_FAILURE || test->kind == LIFECYCLE_CLEANUP_NO_OP;
    return valid && summary && cleanup_failed == expected_cleanup_failure;
}

// Report the captured child output on failure; it remains in the retained workspace too.
static void lifecycle_dump_log(const char* log)
{
    FILE* file = fopen(log, "r");
    if (file != 0)
    {
        char buffer[4096];
        size_t total = 0;
        size_t count;
        fprintf(stderr, "differential lifecycle: captured output from %s:\n", log);
        while (total < 65536 && (count = fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            fwrite(buffer, 1, count, stderr);
            total += count;
        }
        if (!feof(file))
        {
            fputs("\ndifferential lifecycle: captured output truncated\n", stderr);
        }
        fclose(file);
    }
    else
    {
        fprintf(stderr, "differential lifecycle: cannot open captured output %s: %s\n", log, strerror(errno));
    }
}

static bool lifecycle_case(const LifecycleCase* test, const char* root, const char* compiler, const char* clang, const char* fixture, const char* failing_fixture, const char* failing_clang, const char* no_op_path)
{
    char temporary[PATH_MAX];
    char log[PATH_MAX];
    char log_name[128];
    const int length = snprintf(log_name, sizeof(log_name), "%s.log", test->name);
    bool valid = length >= 0 && (size_t)length < sizeof(log_name) && lifecycle_path(temporary, sizeof(temporary), root, test->name) && mkdir(temporary, 0700) == 0 && lifecycle_path(log, sizeof(log), root, log_name);
    int status = -1;
    if (valid)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(60);
            const int output = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (output < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0 || chdir(root) != 0 || setenv("TMPDIR", test->kind == LIFECYCLE_RELATIVE_TMPDIR ? test->name : temporary, 1) != 0)
            {
                _exit(125);
            }
            if (output > STDERR_FILENO)
            {
                close(output);
            }
            if (test->kind == LIFECYCLE_CLEANUP_FAILURE || test->kind == LIFECYCLE_CLEANUP_NO_OP)
            {
                const char* original_path = getenv("PATH");
                char path[PATH_MAX * 2];
                const int path_length = snprintf(path, sizeof(path), "%s:%s", no_op_path, original_path ? original_path : "");
                if (path_length < 0 || (size_t)path_length >= sizeof(path) || setenv("PATH", path, 1) != 0 ||
                    setenv("BUSTER_LIFECYCLE_RM_FAIL", test->kind == LIFECYCLE_CLEANUP_FAILURE ? "1" : "0", 1) != 0)
                {
                    _exit(125);
                }
            }
            const char* option = test->kind == LIFECYCLE_KEEP ? "--keep" : test->kind == LIFECYCLE_INVALID_OPTION ? "--unknown-option" : 0;
            const char* arguments[] =
            {
                compiler,
                "test-differential-harness",
                "--clang",
                test->kind == LIFECYCLE_BUILD_FAILURE ? failing_clang : clang,
                "--fixture-source",
                test->kind == LIFECYCLE_SUITE_FAILURE ? failing_fixture : fixture,
                option,
                0,
            };
            execv(compiler, (char* const*)arguments);
            _exit(126);
        }
        status = lifecycle_wait(child);
        char artifact[PATH_MAX] = {0};
        const bool workspace = lifecycle_workspace(temporary, test->retained, artifact, sizeof(artifact));
        const bool report = lifecycle_report(log, artifact, test);
        valid = status == test->status && workspace && report;
        if (!valid)
        {
            fprintf(stderr, "differential lifecycle: %s workspace=%s report=%s\n", test->name, workspace ? "valid" : "invalid", report ? "valid" : "invalid");
            lifecycle_dump_log(log);
        }
    }
    printf("differential lifecycle: %s %s (status=%d, expected=%d)\n", test->name, valid ? "PASS" : "FAIL", status, test->status);
    return valid;
}

static int lifecycle_tests(int argc, char** argv)
{
    const bool keep = argc == 4 && strcmp(argv[3], "--keep") == 0;
    bool valid = argc == 3 || keep;
    char compiler[PATH_MAX];
    char clang[PATH_MAX];
    char fixture[PATH_MAX];
    char self[PATH_MAX];
    char temporary[PATH_MAX];
    const char* requested = getenv("TMPDIR");
    valid = valid && realpath(argv[1], compiler) && realpath(argv[2], clang) && realpath("tests/differential/tool.c", fixture) && realpath("/proc/self/exe", self) && realpath(requested && requested[0] ? requested : "/tmp", temporary);
    char root[PATH_MAX];
    valid = valid && lifecycle_path(root, sizeof(root), temporary, "buster-differential-lifecycle-XXXXXX") && mkdtemp(root);
    const bool made = valid;
    char failing_fixture[PATH_MAX];
    char no_op_path[PATH_MAX];
    char no_op_rm[PATH_MAX];
    valid = valid && lifecycle_path(failing_fixture, sizeof(failing_fixture), root, "failing-fixture.c") && lifecycle_path(no_op_path, sizeof(no_op_path), root, "no-op-path") && mkdir(no_op_path, 0700) == 0 && lifecycle_path(no_op_rm, sizeof(no_op_rm), no_op_path, "rm") && symlink(self, no_op_rm) == 0;
    FILE* file = valid ? fopen(failing_fixture, "w") : 0;
    valid = valid && file && fputs("int main(void) { return 1; }\n", file) >= 0;
    if (file)
    {
        valid = fclose(file) == 0 && valid;
    }
    unsigned count = 0;
    unsigned failures = 0;
    if (valid)
    {
        for (unsigned i = 0; i < sizeof(lifecycle_cases) / sizeof(lifecycle_cases[0]); ++i)
        {
            ++count;
            const bool passed = lifecycle_case(&lifecycle_cases[i], root, compiler, clang, fixture, failing_fixture, self, no_op_path);
            failures += !passed;
        }
    }
    bool retained = made;
    if (valid && failures == 0 && !keep)
    {
        retained = !lifecycle_remove(root);
        failures += retained;
    }
    if (!valid)
    {
        fputs("differential lifecycle: run from the repository root with COMPILER CLANG [--keep] and a writable TMPDIR\n", stderr);
    }
    printf("differential lifecycle: %u cases, %u failures", count, failures);
    if (retained)
    {
        printf("; artifacts=%s", root);
    }
    putchar('\n');
    return valid ? failures != 0 : 2;
}

int main(int argc, char** argv)
{
    const char* slash = argc > 0 ? strrchr(argv[0], '/') : 0;
    const char* name = argc > 0 ? slash ? slash + 1 : argv[0] : "";
    int result = 0;
    // differential_resolve canonicalizes tool paths, so identify the failing compiler by its compile invocation.
    if (argc > 1 && strcmp(argv[1], "-std=c11") == 0)
    {
        result = 1;
    }
    else if (strcmp(name, "rm") == 0)
    {
        const char* fail = getenv("BUSTER_LIFECYCLE_RM_FAIL");
        result = fail && strcmp(fail, "1") == 0 ? 1 : 0;
    }
    else
    {
        result = lifecycle_tests(argc, argv);
    }
    return result;
}
