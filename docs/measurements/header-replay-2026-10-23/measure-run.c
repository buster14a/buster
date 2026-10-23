// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Process resource observation for the #111 experiment. No measured kernel changes.
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    int result = 1;
    struct timespec begin;
    struct timespec end;
    struct rusage usage = { 0 };
    const bool ready = argc > 1 && clock_gettime(CLOCK_MONOTONIC, &begin) == 0;
    const pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        execvp(argv[1], argv + 1);
        perror(argv[1]);
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = wait4(child, &status, 0, &usage);
        }
        while (waited < 0 && errno == EINTR);
    }
    if (child > 0 && waited == child && clock_gettime(CLOCK_MONOTONIC, &end) == 0)
    {
        result = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
        const uint64_t elapsed = (uint64_t)(end.tv_sec - begin.tv_sec) * 1000000000ull + end.tv_nsec - begin.tv_nsec;
        const uint64_t user = (uint64_t)usage.ru_utime.tv_sec * 1000000ull + usage.ru_utime.tv_usec;
        const uint64_t system = (uint64_t)usage.ru_stime.tv_sec * 1000000ull + usage.ru_stime.tv_usec;
        fprintf(stderr, "process elapsed_ns=%lu user_us=%lu system_us=%lu maxrss_kb=%ld minor_faults=%ld major_faults=%ld result=%d\n", elapsed, user, system, usage.ru_maxrss, usage.ru_minflt, usage.ru_majflt, result);
    }
    return result;
}
