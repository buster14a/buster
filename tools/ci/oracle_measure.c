// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Temporary measurement for #542, removed before review: the external processes the 9700X job's tests run. Installed
// under each tool's name in a directory ahead of PATH, it runs the next tool of its name on PATH and appends a line to
// $BUSTER_MEASURE_LOG: the test (the outermost ./build/compiler among its ancestors, but the parallel runner), the
// tool, its CPU and wall microseconds (wait4's, with the descendants it waited for), its exit status and a hash of its
// arguments. A tool that a measured tool runs (gcc's as and ld) only runs.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int find_tool(const char* name, const char* self, char* path, size_t size)
{
    const char* search = getenv("PATH");
    int found = 0;
    while (!found && search != NULL && *search != 0)
    {
        const char* end = strchr(search, ':');
        size_t length = end != NULL ? (size_t)(end - search) : strlen(search);
        char candidate[PATH_MAX * 2];
        snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)length, search, name);
        char resolved[PATH_MAX];
        if (access(candidate, X_OK) == 0 && realpath(candidate, resolved) != NULL && strcmp(resolved, self) != 0)
        {
            snprintf(path, size, "%s", candidate);
            found = 1;
        }
        search = end != NULL ? end + 1 : NULL;
    }
    return found;
}

// Copies text into a buffer of size bytes, cut to fit.
static void copy_cut(char* buffer, size_t size, const char* text)
{
    size_t length = strlen(text);
    length = length < size - 1 ? length : size - 1;
    memcpy(buffer, text, length);
    buffer[length] = 0;
}

// The process's arguments joined by spaces, at most size - 1 bytes; returns the argument count.
static int read_arguments(int pid, char* text, size_t size, char* first, size_t first_size, char* second, size_t second_size)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    int fd = open(path, O_RDONLY);
    int count = 0;
    text[0] = 0;
    first[0] = 0;
    second[0] = 0;
    if (fd >= 0)
    {
        char buffer[8192];
        ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        if (length > 0)
        {
            buffer[length] = 0;
            size_t used = 0;
            for (ssize_t i = 0; i < length; i += (ssize_t)strlen(buffer + i) + 1)
            {
                const char* argument = buffer + i;
                if (count == 0)
                {
                    copy_cut(first, first_size, argument);
                }
                else
                {
                    if (count == 1)
                    {
                        copy_cut(second, second_size, argument);
                    }
                    int written = snprintf(text + used, size - used, "%s%s", used != 0 ? " " : "", argument);
                    used = written > 0 && (size_t)written < size - used ? used + (size_t)written : size - 1;
                }
                count += 1;
            }
        }
    }
    return count;
}

static int parent_of(int pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    int fd = open(path, O_RDONLY);
    int parent = 0;
    if (fd >= 0)
    {
        char buffer[1024];
        ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        if (length > 0)
        {
            buffer[length] = 0;
            const char* close_paren = strrchr(buffer, ')');
            if (close_paren != NULL)
            {
                char state = 0;
                if (sscanf(close_paren + 1, " %c %d", &state, &parent) != 2)
                {
                    parent = 0;
                }
            }
        }
    }
    return parent;
}

// The outermost ./build/compiler among the ancestors whose command isn't the parallel runner, by its arguments.
static void find_test(char* test, size_t size)
{
    snprintf(test, size, "?");
    int pid = getppid();
    for (int steps = 0; pid > 1 && steps < 64; steps += 1)
    {
        char arguments[512];
        char first[PATH_MAX];
        char second[256];
        int count = read_arguments(pid, arguments, sizeof(arguments), first, sizeof(first), second, sizeof(second));
        const char* base = strrchr(first, '/') != NULL ? strrchr(first, '/') + 1 : first;
        if (count >= 2 && strcmp(base, "compiler") == 0 && strcmp(second, "parallel") != 0)
        {
            copy_cut(test, size, arguments);
        }
        pid = parent_of(pid);
    }
}

int main(int argc, char** argv)
{
    char self[PATH_MAX];
    ssize_t self_length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    self[self_length > 0 ? self_length : 0] = 0;
    const char* depth_text = getenv("BUSTER_MEASURE_DEPTH");
    int depth = depth_text != NULL ? atoi(depth_text) : 0;
    int result = 127;
    if (depth > 16)
    {
        fprintf(stderr, "oracle_measure: ran itself\n");
        result = 126;
    }
    else
    {
        char depth_next[16];
        snprintf(depth_next, sizeof(depth_next), "%d", depth + 1);
        setenv("BUSTER_MEASURE_DEPTH", depth_next, 1);
        const char* name = strrchr(argv[0], '/') != NULL ? strrchr(argv[0], '/') + 1 : argv[0];
        char tool[PATH_MAX * 2];
        if (!find_tool(name, self, tool, sizeof(tool)))
        {
            fprintf(stderr, "oracle_measure: no %s on PATH\n", name);
        }
        else if (getenv("BUSTER_MEASURE_INSIDE") != NULL)
        {
            argv[0] = tool;
            execv(tool, argv);
        }
        else
        {
            uint64_t hash = 14695981039346656037u;
            for (int i = 1; i < argc; i += 1)
            {
                for (const char* c = argv[i]; *c != 0; c += 1)
                {
                    hash = (hash ^ (uint8_t)*c) * 1099511628211u;
                }
                hash = (hash ^ 0xff) * 1099511628211u;
            }
            char test[512];
            find_test(test, sizeof(test));
            struct timespec start;
            struct timespec end;
            clock_gettime(CLOCK_MONOTONIC, &start);
            pid_t child = fork();
            if (child == 0)
            {
                setenv("BUSTER_MEASURE_INSIDE", "1", 1);
                argv[0] = tool;
                execv(tool, argv);
                _exit(127);
            }
            int status = 0;
            struct rusage usage = { 0 };
            while (wait4(child, &status, 0, &usage) < 0 && errno == EINTR)
            {
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            const char* log = getenv("BUSTER_MEASURE_LOG");
            if (log != NULL)
            {
                long long cpu = (long long)usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec + (long long)usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
                long long wall = (long long)(end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
                char line[1024];
                int length = snprintf(line, sizeof(line), "%s\t%s\t%lld\t%lld\t%d\t%016llx\n", test, name, cpu, wall, result, (unsigned long long)hash);
                int fd = open(log, O_WRONLY | O_APPEND | O_CREAT, 0644);
                if (fd >= 0 && length > 0)
                {
                    ssize_t written = write(fd, line, (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1);
                    (void)written;
                    close(fd);
                }
            }
        }
    }
    return result;
}
