// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// External-process measurements for host-linker evaluation.
// Every sample includes fork/exec/wait; files and compilation stay outside link timings.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec;
}

static int run(char** command)
{
    int result = -1;
    pid_t child = fork();
    if (child == 0)
    {
        alarm(120);
        if (freopen("/dev/null", "w", stdout) == NULL)
        {
            _exit(126);
        }
        if (getenv("BUSTER_MEASURE_QUIET") != NULL && freopen("/dev/null", "w", stderr) == NULL)
        {
            _exit(126);
        }
        execvp(command[0], command);
        perror(command[0]);
        _exit(127);
    }
    if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited == child && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
    }
    return result;
}

int main(int argc, char** argv)
{
    int result = 1;
    if (argc >= 7 && strcmp(argv[1], "time") == 0)
    {
        char* end = NULL;
        long rounds = strtol(argv[4], &end, 10);
        int valid = end != argv[4] && *end == 0 && rounds > 0 && rounds <= 1001;
        long expected = strtol(argv[5], &end, 10);
        valid = valid && end != argv[5] && *end == 0 && expected >= 0 && expected <= 255;
        FILE* file = valid ? fopen(argv[2], "a") : NULL;
        uint64_t samples[1001];
        if (file != NULL)
        {
            result = 0;
            for (long round = -1; round < rounds && result == 0; ++round)
            {
                uint64_t start = now_ns();
                int status = run(argv + 6);
                uint64_t elapsed = now_ns() - start;
                if (status != expected)
                {
                    fprintf(stderr, "%s: expected exit %ld, got %d\n", argv[3], expected, status);
                    result = 1;
                }
                if (round >= 0 && result == 0)
                {
                    samples[round] = elapsed;
                    if (fprintf(file, "%s,%ld,%llu,%d\n", argv[3], round, (unsigned long long)elapsed, status) < 0)
                    {
                        result = 1;
                    }
                }
            }
            if (fclose(file) != 0)
            {
                result = 1;
            }
            if (result == 0)
            {
                // Bounded insertion sort keeps the standalone harness independent of callback dispatch.
                for (long i = 1; i < rounds; ++i)
                {
                    uint64_t sample = samples[i];
                    long j = i;
                    while (j > 0 && samples[j - 1] > sample)
                    {
                        samples[j] = samples[j - 1];
                        --j;
                    }
                    samples[j] = sample;
                }
                printf("%s median=%.3f ms min=%.3f max=%.3f n=%ld\n", argv[3], samples[rounds / 2] / 1e6, samples[0] / 1e6, samples[rounds - 1] / 1e6, rounds);
            }
        }
    }
    else
    {
        fprintf(stderr, "usage: %s time CSV LABEL ROUNDS EXPECTED_EXIT COMMAND [ARG...]\n", argv[0]);
    }
    return result;
}
