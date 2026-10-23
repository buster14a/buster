// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Fresh-process paired measurements. Both argument lists are passed directly to execvp; no shell is involved.
// Even rounds run baseline first, odd rounds candidate first. Every child is pinned to the same CPU.
// stdout contains every observation as CSV. stderr contains exact argv lists and the summary.
// Linux wait4 reports maximum resident set size in KiB. Wall time includes fork, exec and wait overhead.

typedef struct Sample Sample;
struct Sample
{
    uint64_t wall_ns;
    uint64_t user_us;
    uint64_t system_us;
    long rss_kib;
    long minor_faults;
    long major_faults;
    int exit_code;
    int signal;
};

static uint64_t nanoseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0)
    {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static bool number(const char* text, uint32_t maximum, uint32_t* result)
{
    char* end = 0;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 10);
    const bool valid = text[0] != 0 && text[0] != '-' && errno == 0 && *end == 0 && value <= maximum;
    if (valid)
    {
        *result = (uint32_t)value;
    }
    return valid;
}

static void show_command(const char* name, char** arguments)
{
    fprintf(stderr, "%s argv:", name);
    for (uint32_t i = 0; arguments[i] != 0; i += 1)
    {
        fputs(" [\"", stderr);
        for (const unsigned char* p = (const unsigned char*)arguments[i]; *p != 0; p += 1)
        {
            if (*p == '\\' || *p == '"')
            {
                fputc('\\', stderr);
            }
            if (*p < 32 || *p >= 127)
            {
                fprintf(stderr, "\\x%02x", *p);
            }
            else
            {
                fputc(*p, stderr);
            }
        }
        fputs("\"]", stderr);
    }
    fputc('\n', stderr);
}

static bool measure(char** arguments, int discard, Sample* sample)
{
    struct rusage usage = { 0 };
    int status = 0;
    fflush(0);
    const uint64_t start = nanoseconds();
    const pid_t child = fork();
    bool success = child >= 0;
    if (child == 0)
    {
        if (dup2(discard, STDOUT_FILENO) < 0)
        {
            perror("dup2");
            _exit(126);
        }
        close(discard);
        execvp(arguments[0], arguments);
        perror(arguments[0]);
        _exit(127);
    }
    else if (child < 0)
    {
        perror("fork");
    }
    else
    {
        pid_t waited;
        do
        {
            waited = wait4(child, &status, 0, &usage);
        }
        while (waited < 0 && errno == EINTR);
        const uint64_t finish = nanoseconds();
        if (waited != child)
        {
            perror("wait4");
            success = false;
        }
        else
        {
            *sample = (Sample){
                .wall_ns = finish - start,
                .user_us = (uint64_t)usage.ru_utime.tv_sec * UINT64_C(1000000) + (uint64_t)usage.ru_utime.tv_usec,
                .system_us = (uint64_t)usage.ru_stime.tv_sec * UINT64_C(1000000) + (uint64_t)usage.ru_stime.tv_usec,
                .rss_kib = usage.ru_maxrss,
                .minor_faults = usage.ru_minflt,
                .major_faults = usage.ru_majflt,
                .exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                .signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0,
            };
            success = sample->exit_code == 0 && sample->signal == 0;
        }
    }
    return success;
}

static void sort(double* values, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const double value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

static double median(const double* sorted, uint32_t count)
{
    return (sorted[(count - 1) / 2] + sorted[count / 2]) * 0.5;
}

static void summarize(const Sample* samples, uint32_t count, double* temporary)
{
    double medians[2] = { 0 };
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            temporary[i] = (double)samples[2 * i + variant].wall_ns / 1000000.0;
        }
        sort(temporary, count);
        medians[variant] = median(temporary, count);
        fprintf(stderr, "%s: wall_ms median=%.6f p10=%.6f p90=%.6f min=%.6f max=%.6f",
                variant == 0 ? "baseline" : "candidate", medians[variant], temporary[(count - 1) / 10],
                temporary[(count - 1) * 9 / 10], temporary[0], temporary[count - 1]);
        for (uint32_t i = 0; i < count; i += 1)
        {
            temporary[i] = (double)samples[2 * i + variant].rss_kib;
        }
        sort(temporary, count);
        fprintf(stderr, " rss_kib median=%.0f min=%.0f max=%.0f\n", median(temporary, count), temporary[0], temporary[count - 1]);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        temporary[i] = (double)samples[2 * i + 1].wall_ns / (double)samples[2 * i].wall_ns;
    }
    sort(temporary, count);
    fprintf(stderr, "candidate/baseline: median_wall_ratio=%.6f paired_ratio_median=%.6f paired_ratio_p10=%.6f paired_ratio_p90=%.6f\n",
            medians[1] / medians[0], median(temporary, count), temporary[(count - 1) / 10], temporary[(count - 1) * 9 / 10]);
}

int main(int argc, char** argv)
{
    uint32_t rounds = 31;
    uint32_t warmups = 3;
    uint32_t cpu = 2;
    uint32_t first = 0;
    uint32_t split = 0;
    bool valid = true;
    for (uint32_t i = 1; valid && i < (uint32_t)argc; i += 1)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            first = i + 1;
            break;
        }
        if (i + 1 >= (uint32_t)argc)
        {
            valid = false;
        }
        else if (strcmp(argv[i], "--rounds") == 0)
        {
            valid = number(argv[++i], 10000, &rounds) && rounds != 0;
        }
        else if (strcmp(argv[i], "--warmup") == 0)
        {
            valid = number(argv[++i], 100, &warmups);
        }
        else if (strcmp(argv[i], "--cpu") == 0)
        {
            valid = number(argv[++i], CPU_SETSIZE - 1, &cpu);
        }
        else
        {
            valid = false;
        }
    }
    for (uint32_t i = first; first != 0 && i < (uint32_t)argc && split == 0; i += 1)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            split = i;
        }
    }
    valid = valid && first != 0 && split > first && split + 1 < (uint32_t)argc;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--rounds N] [--warmup N] [--cpu N] -- BASELINE [ARG...] -- CANDIDATE [ARG...]\n", argv[0]);
    }
    else
    {
        argv[split] = 0;
        char** commands[2] = { argv + first, argv + split + 1 };
        Sample* samples = calloc((size_t)rounds * 2, sizeof(*samples));
        double* temporary = malloc((size_t)rounds * sizeof(*temporary));
        const int discard = open("/dev/null", O_WRONLY | O_CLOEXEC);
        cpu_set_t affinity;
        CPU_ZERO(&affinity);
        CPU_SET(cpu, &affinity);
        valid = samples != 0 && temporary != 0 && discard >= 0 && sched_setaffinity(0, sizeof(affinity), &affinity) == 0;
        if (!valid)
        {
            perror("measurement setup");
        }
        else
        {
            fprintf(stderr, "CLOCK_MONOTONIC_RAW; Linux wait4 RSS in KiB; CPU %u; warmup pairs %u; measured pairs %u\n", cpu, warmups, rounds);
            show_command("baseline", commands[0]);
            show_command("candidate", commands[1]);
            puts("phase,round,order,variant,wall_ns,user_us,system_us,maxrss_kib,minor_faults,major_faults,exit_code,signal");
            for (uint32_t round = 0; valid && round < warmups + rounds; round += 1)
            {
                for (uint32_t order = 0; valid && order < 2; order += 1)
                {
                    const uint32_t variant = (round + order) & 1;
                    Sample sample = { .exit_code = -1 };
                    valid = measure(commands[variant], discard, &sample);
                    printf("%s,%u,%u,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%ld,%ld,%ld,%d,%d\n",
                           round < warmups ? "warmup" : "sample", round < warmups ? round + 1 : round - warmups + 1,
                           order + 1, variant == 0 ? "baseline" : "candidate", sample.wall_ns, sample.user_us,
                           sample.system_us, sample.rss_kib, sample.minor_faults, sample.major_faults, sample.exit_code, sample.signal);
                    fflush(stdout);
                    if (round >= warmups)
                    {
                        samples[2 * (round - warmups) + variant] = sample;
                    }
                    if (!valid)
                    {
                        fprintf(stderr, "measurement failed: %s exit=%d signal=%d; no performance summary\n",
                                variant == 0 ? "baseline" : "candidate", sample.exit_code, sample.signal);
                    }
                }
            }
            if (valid)
            {
                summarize(samples, rounds, temporary);
                result = 0;
            }
        }
        if (discard >= 0)
        {
            close(discard);
        }
        free(temporary);
        free(samples);
    }
    return result;
}
