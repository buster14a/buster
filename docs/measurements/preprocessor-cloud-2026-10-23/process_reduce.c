// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original validation/reduction of paired_measure CSV, reusing this task's original measurement sample/helpers.
#define main buster_original_measure_main
#include "paired_measure.c"
#undef main
#include <limits.h>

#define BUSTER_PROCESS_ROUNDS 31
#define BUSTER_PROCESS_WARMUPS 3
#define BUSTER_PROCESS_FIELDS 12

static bool process_number(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    *value = strtoull(text, &end, 10);
    return text[0] >= '0' && text[0] <= '9' && errno == 0 && *end == 0;
}

static double process_metric(const Sample* sample, uint32_t metric)
{
    return metric == 0 ? (double)sample->wall_ns / 1e6 : metric == 1 ? (double)sample->user_us / 1e3 :
           metric == 2 ? (double)sample->system_us / 1e3 : (double)sample->rss_kib;
}

static bool process_reduce(const char* path)
{
    Sample samples[(BUSTER_PROCESS_ROUNDS + BUSTER_PROCESS_WARMUPS) * 2] = { 0 };
    bool seen[(BUSTER_PROCESS_ROUNDS + BUSTER_PROCESS_WARMUPS) * 2] = { false };
    FILE* input = fopen(path, "r");
    bool valid = input != 0;
    char* line = 0;
    size_t capacity = 0;
    uint32_t count = 0;
    if (valid)
    {
        valid = getline(&line, &capacity, input) > 0;
        if (valid)
        {
            line[strcspn(line, "\r\n")] = 0;
            valid = strcmp(line, "phase,round,order,variant,wall_ns,user_us,system_us,maxrss_kib,minor_faults,major_faults,exit_code,signal") == 0;
        }
    }
    while (valid && getline(&line, &capacity, input) > 0)
    {
        line[strcspn(line, "\r\n")] = 0;
        char* tail = line;
        char* fields[BUSTER_PROCESS_FIELDS];
        for (uint32_t i = 0; i < BUSTER_PROCESS_FIELDS; i += 1)
        {
            fields[i] = strsep(&tail, ",");
            valid = valid && fields[i] != 0;
        }
        valid = valid && tail == 0;
        uint32_t round = 0;
        uint32_t order = 0;
        const bool warmup = valid && strcmp(fields[0], "warmup") == 0;
        valid = valid && (warmup || strcmp(fields[0], "sample") == 0) &&
                number(fields[1], warmup ? BUSTER_PROCESS_WARMUPS : BUSTER_PROCESS_ROUNDS, &round) && round != 0 &&
                number(fields[2], 2, &order) && order != 0 &&
                (strcmp(fields[3], "baseline") == 0 || strcmp(fields[3], "candidate") == 0);
        if (valid)
        {
            const uint32_t absolute = round - 1 + (warmup ? 0 : BUSTER_PROCESS_WARMUPS);
            const uint32_t variant = strcmp(fields[3], "baseline") == 0 ? 0 : 1;
            const uint32_t at = absolute * 2 + variant;
            valid = !seen[at] && variant == ((absolute + order - 1) & 1);
            uint64_t values[8];
            for (uint32_t i = 0; valid && i < 8; i += 1)
            {
                valid = process_number(fields[i + 4], &values[i]);
            }
            valid = valid && values[0] != 0 && values[3] <= LONG_MAX && values[4] <= LONG_MAX && values[5] <= LONG_MAX &&
                    values[6] == 0 && values[7] == 0;
            if (valid)
            {
                samples[at] = (Sample){
                    .wall_ns = values[0],
                    .user_us = values[1],
                    .system_us = values[2],
                    .rss_kib = (long)values[3],
                    .minor_faults = (long)values[4],
                    .major_faults = (long)values[5],
                    .exit_code = 0,
                    .signal = 0,
                };
                seen[at] = true;
                count += 1;
            }
        }
    }
    valid = valid && !ferror(input) && count == 2 * (BUSTER_PROCESS_ROUNDS + BUSTER_PROCESS_WARMUPS);
    if (input != 0)
    {
        valid = fclose(input) == 0 && valid;
    }
    free(line);
    for (uint32_t i = 0; valid && i < 2 * (BUSTER_PROCESS_ROUNDS + BUSTER_PROCESS_WARMUPS); i += 1)
    {
        valid = seen[i];
    }
    if (!valid)
    {
        fprintf(stderr, "%s: invalid/incomplete rows, pair order or child exit status\n", path);
    }
    else
    {
        fprintf(stderr, "%s: valid 31 measured pairs + 3 warmups; all 68 child exits successful\n", path);
        static const char* const names[4] = { "wall_ms", "user_ms", "system_ms", "rss_kib" };
        for (uint32_t metric = 0; metric < 4; metric += 1)
        {
            double values[2][BUSTER_PROCESS_ROUNDS];
            double ratios[BUSTER_PROCESS_ROUNDS];
            for (uint32_t i = 0; i < BUSTER_PROCESS_ROUNDS; i += 1)
            {
                values[0][i] = process_metric(&samples[(i + BUSTER_PROCESS_WARMUPS) * 2], metric);
                values[1][i] = process_metric(&samples[(i + BUSTER_PROCESS_WARMUPS) * 2 + 1], metric);
                ratios[i] = values[0][i] != 0 ? values[1][i] / values[0][i] : 0;
            }
            sort(values[0], BUSTER_PROCESS_ROUNDS);
            sort(values[1], BUSTER_PROCESS_ROUNDS);
            sort(ratios, BUSTER_PROCESS_ROUNDS);
            printf("%s\t%s\t31", path, names[metric]);
            for (uint32_t variant = 0; variant < 2; variant += 1)
            {
                printf("\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f", values[variant][15], values[variant][3], values[variant][27],
                       values[variant][0], values[variant][30]);
            }
            printf("\t%.6f\t%.6f\t%.6f\t%.6f\n", values[0][15] != 0 ? values[1][15] / values[0][15] : 0,
                   ratios[15], ratios[3], ratios[27]);
        }
    }
    return valid;
}

int main(int argc, char** argv)
{
    bool valid = argc > 1;
    puts("input\tmetric\tpairs\ta_median\ta_p10\ta_p90\ta_min\ta_max\tb_median\tb_p10\tb_p90\tb_min\tb_max\tmedian_ratio\tpaired_median_ratio\tpaired_p10_ratio\tpaired_p90_ratio");
    for (int i = 1; valid && i < argc; i += 1)
    {
        valid = process_reduce(argv[i]);
    }
    valid = fflush(stdout) == 0 && !ferror(stdout) && valid;
    return valid ? 0 : 1;
}
