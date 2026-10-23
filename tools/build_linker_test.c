// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Exercise the real build script with a hermetic, recording Clang; no compiler sources are compiled.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fake_clang(int argc, char** argv)
{
    int compiling = 0;
    const char* output = NULL;
    const char* json = NULL;
    const char* selected = "";
    int result = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-S") == 0)
        {
            compiling = 1;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            output = argv[++i];
        }
        else if (strcmp(argv[i], "-MJ") == 0 && i + 1 < argc)
        {
            json = argv[++i];
        }
        else if (strncmp(argv[i], "--ld-path=", 10) == 0)
        {
            selected = argv[i] + 10;
        }
    }
    const char* expected = compiling ? "" : getenv("BUSTER_EXPECT_LINKER");
    if (expected == NULL || strcmp(selected, expected) != 0)
    {
        fprintf(stderr, "unexpected linker: '%s', expected '%s'\n", selected, expected == NULL ? "(unset)" : expected);
        result = 1;
    }
    int log = open("calls", O_WRONLY | O_CREAT | O_APPEND, 0600);
    char event = compiling ? 'C' : 'L';
    if (log < 0 || write(log, &event, 1) != 1)
    {
        result = 1;
    }
    if (log >= 0)
    {
        close(log);
    }
    if (!compiling && getenv("BUSTER_FAIL_LINK") != NULL)
    {
        result = 42;
    }
    if (result == 0 && output != NULL)
    {
        FILE* file = fopen(output, "w");
        if (file == NULL)
        {
            result = 1;
        }
        else
        {
            fputs("fixture", file);
            result = fclose(file) == 0 ? result : 1;
        }
    }
    if (result == 0 && json != NULL)
    {
        FILE* file = fopen(json, "w");
        if (file == NULL)
        {
            result = 1;
        }
        else
        {
            fputs("{}\n", file);
            result = fclose(file) == 0 ? result : 1;
        }
    }
    return result;
}

static int run_case(const char* script, const char* request, const char* expected, int fail_link, int status, const char* mode)
{
    unlink("calls");
    pid_t child = fork();
    if (child == 0)
    {
        setenv("BUSTER_FAKE_CLANG", "1", 1);
        setenv("BUSTER_EXPECT_LINKER", expected, 1);
        if (request == NULL)
        {
            unsetenv("BUSTER_LINKER");
        }
        else
        {
            setenv("BUSTER_LINKER", request, 1);
        }
        if (fail_link)
        {
            setenv("BUSTER_FAIL_LINK", "1", 1);
        }
        else
        {
            unsetenv("BUSTER_FAIL_LINK");
        }
        if (strcmp(mode, "lexer-variant") == 0)
        {
            execl("/bin/bash", "bash", script, mode, "copy=variant.c", (char*)NULL);
        }
        else if (strcmp(mode, "lexer-variant-assembly") == 0)
        {
            execl("/bin/bash", "bash", script, mode, "copy", "variant.c", "variant.s", (char*)NULL);
        }
        else
        {
            execl("/bin/bash", "bash", script, (char*)NULL);
        }
        _exit(127);
    }
    int result = 0;
    int waited = 0;
    if (child <= 0 || waitpid(child, &waited, 0) != child || !WIFEXITED(waited) || WEXITSTATUS(waited) != status)
    {
        fprintf(stderr, "case %s/%s returned unexpected status\n", request == NULL ? "(default)" : request, mode);
        result = 1;
    }
    FILE* file = fopen("calls", "r");
    unsigned links = 0;
    unsigned compiles = 0;
    if (file != NULL)
    {
        int c;
        while ((c = fgetc(file)) != EOF)
        {
            links += c == 'L';
            compiles += c == 'C';
        }
        fclose(file);
    }
    unsigned expected_links = status == 1 || strcmp(mode, "lexer-variant-assembly") == 0 ? 0 : 1;
    if (links != expected_links || (expected_links != 0 && compiles == 0))
    {
        fprintf(stderr, "case %s/%s: %u links, %u compiles\n", request == NULL ? "(default)" : request, mode, links, compiles);
        result = 1;
    }
    return result;
}

int main(int argc, char** argv)
{
    int result = 1;
    if (getenv("BUSTER_FAKE_CLANG") != NULL)
    {
        result = fake_clang(argc, argv);
    }
    else if (argc == 2)
    {
        char script[PATH_MAX];
        char self[PATH_MAX];
        char directory[] = "/tmp/buster-linker-test-XXXXXX";
        if (realpath(argv[1], script) != NULL && realpath(argv[0], self) != NULL && mkdtemp(directory) != NULL && chdir(directory) == 0 && mkdir("bin", 0700) == 0)
        {
            const char* commands[] = {"rm", "mkdir", "mktemp", "cat", "realpath", "dirname", "grep"};
            result = 0;
            for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
            {
                char target[PATH_MAX];
                char link[PATH_MAX];
                snprintf(target, sizeof(target), "/usr/bin/%s", commands[i]);
                snprintf(link, sizeof(link), "bin/%s", commands[i]);
                result |= symlink(target, link) != 0;
            }
            result |= symlink(self, "bin/clang") != 0;
            result |= setenv("PATH", "bin", 1) != 0;
            char custom[PATH_MAX];
            snprintf(custom, sizeof(custom), "%s/linker with spaces", directory);
            FILE* variant = fopen("variant.c", "w");
            if (variant != NULL)
            {
                fclose(variant);
            }
            else
            {
                result = 1;
            }
            result |= run_case(script, NULL, "", 0, 0, "");
            result |= run_case(script, "auto", "", 0, 0, "");
            result |= run_case(script, "mold", "", 0, 1, "");
            result |= run_case(script, "", "", 0, 1, "");
            result |= run_case(script, "unknown", "", 0, 1, "");
            result |= run_case(script, "/missing/linker", "", 0, 1, "");
            result |= symlink(self, "bin/mold") != 0;
            result |= run_case(script, "auto", "bin/mold", 0, 0, "");
            result |= symlink(self, "bin/wild") != 0;
            result |= symlink(self, "bin/ld.lld") != 0;
            result |= symlink(self, "bin/ld.bfd") != 0;
            result |= symlink(self, "linker with spaces") != 0;
            // command -v returns the relative PATH entry; the build passes it as one driver argument.
            result |= run_case(script, NULL, "bin/wild", 0, 0, "");
            result |= run_case(script, "auto", "bin/wild", 0, 0, "");
            result |= run_case(script, "system", "", 0, 0, "");
            result |= run_case(script, "mold", "bin/mold", 0, 0, "");
            result |= run_case(script, "wild", "bin/wild", 0, 0, "");
            result |= run_case(script, "lld", "bin/ld.lld", 0, 0, "");
            result |= run_case(script, "bfd", "bin/ld.bfd", 0, 0, "");
            result |= run_case(script, custom, custom, 0, 0, "");
            result |= run_case(script, "auto", "bin/wild", 1, 42, "");
            result |= run_case(script, "wild", "bin/wild", 1, 42, "");
            result |= run_case(script, "wild", "bin/wild", 0, 0, "lexer-variant");
            result |= run_case(script, "unknown", "", 0, 0, "lexer-variant-assembly");
            if (chdir("/") == 0)
            {
                pid_t child = fork();
                if (child == 0)
                {
                    execl("/bin/rm", "rm", "-rf", directory, (char*)NULL);
                    _exit(127);
                }
                int status = 0;
                result |= child <= 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0;
            }
            else
            {
                result = 1;
            }
        }
        printf("stage 1 linker selection regression: %s\n", result == 0 ? "passed" : "FAILED");
    }
    else
    {
        fprintf(stderr, "usage: %s /absolute/path/build.sh\n", argv[0]);
    }
    return result;
}
