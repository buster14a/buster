// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original reduction of pp_pair_driver CSV. No external source was copied.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_PAIR_ROUNDS 31
#define BUSTER_PAIR_WARMUPS 3
#define BUSTER_PAIR_FIELDS 41
#define BUSTER_PAIR_VALUES (BUSTER_PAIR_FIELDS - 4)
#define BUSTER_PAIR_METRICS 8

typedef struct PairRow PairRow;
struct PairRow
{
    uint64_t values[BUSTER_PAIR_VALUES];
    bool seen;
};

static const char* const pair_header =
    "phase,round,order,variant,preprocess_ns,print_ns,release_ns,input_tokens,output_tokens,input_bytes,"
    "read_tsc,lex_tsc,refine_tsc,directives_tsc,total_tsc,digest,token_digest,history_bytes,temporary_peak_bytes,"
    "expansions,substitutions,generated,runs,file_run_tokens,file_runs,buffer_run_tokens,buffer_runs,"
    "direct_expansions,buffered_expansions,invocations,arguments,expanded_arguments,pastes,builtins,"
    "conditions,condition_tokens,long_conditions,longest_condition,directives,skipped_guards,skipped_once";

static bool pair_number(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    *value = strtoull(text, &end, 10);
    return text[0] >= '0' && text[0] <= '9' && errno == 0 && *end == 0;
}

static bool pair_work_equal(const PairRow* a, const PairRow* b)
{
    bool equal = true;
    for (uint32_t i = 3; i < BUSTER_PAIR_VALUES; i += 1)
    {
        if ((i < 6 || i == 11 || i == 12 || i >= 15) && a->values[i] != b->values[i])
        {
            equal = false;
        }
    }
    return equal;
}

static void pair_sort(double* values, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const double value = values[i];
        uint32_t j = i;
        while (j != 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

static double pair_metric(const PairRow* row, uint32_t metric)
{
    const uint64_t* v = row->values;
    double result = 0;
    if (metric == 0)
    {
        result = (double)v[0];
    }
    else if (metric == 1)
    {
        result = (double)v[0] + (double)v[1];
    }
    else if (metric == 2)
    {
        result = (double)v[2];
    }
    else
    {
        const uint64_t parts = v[6] + v[7] + v[8] + v[9];
        const uint64_t ticks = metric < 7 ? v[metric + 3] : v[10] > parts ? v[10] - parts : 0;
        result = v[10] != 0 ? (double)v[0] * (double)ticks / (double)v[10] : 0;
    }
    return result / 1e6;
}

static bool pair_reduce(const char* path)
{
    static PairRow rows[BUSTER_PAIR_WARMUPS + BUSTER_PAIR_ROUNDS][2];
    memset(rows, 0, sizeof(rows));
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
            valid = strcmp(line, pair_header) == 0;
        }
    }
    while (valid && getline(&line, &capacity, input) > 0)
    {
        line[strcspn(line, "\r\n")] = 0;
        char* tail = line;
        char* fields[BUSTER_PAIR_FIELDS];
        for (uint32_t i = 0; i < BUSTER_PAIR_FIELDS; i += 1)
        {
            fields[i] = strsep(&tail, ",");
            valid = valid && fields[i] != 0;
        }
        valid = valid && tail == 0;
        uint64_t round = 0;
        uint64_t order = 0;
        const bool warmup = valid && strcmp(fields[0], "warmup") == 0;
        valid = valid && (warmup || strcmp(fields[0], "sample") == 0) && pair_number(fields[1], &round) &&
                round >= 1 && round <= (warmup ? BUSTER_PAIR_WARMUPS : BUSTER_PAIR_ROUNDS) &&
                pair_number(fields[2], &order) && order >= 1 && order <= 2 &&
                (strcmp(fields[3], "a") == 0 || strcmp(fields[3], "b") == 0);
        if (valid)
        {
            const uint32_t absolute = (uint32_t)round - 1 + (warmup ? 0 : BUSTER_PAIR_WARMUPS);
            const uint32_t variant = fields[3][0] == 'a' ? 0 : 1;
            PairRow* row = &rows[absolute][variant];
            valid = !row->seen && variant == ((absolute + (uint32_t)order - 1) & 1);
            for (uint32_t i = 0; valid && i < BUSTER_PAIR_VALUES; i += 1)
            {
                valid = pair_number(fields[i + 4], &row->values[i]);
            }
            row->seen = valid;
            count += 1;
        }
    }
    valid = valid && !ferror(input) && count == 2 * (BUSTER_PAIR_ROUNDS + BUSTER_PAIR_WARMUPS);
    if (input != 0)
    {
        valid = fclose(input) == 0 && valid;
    }
    free(line);
    for (uint32_t i = 0; valid && i < BUSTER_PAIR_ROUNDS + BUSTER_PAIR_WARMUPS; i += 1)
    {
        valid = rows[i][0].seen && rows[i][1].seen && pair_work_equal(&rows[0][0], &rows[i][0]) &&
                pair_work_equal(&rows[0][0], &rows[i][1]);
    }
    if (!valid)
    {
        fprintf(stderr, "%s: invalid/incomplete rows, order, output identity, or work counts\n", path);
    }
    else
    {
        static const char* const names[BUSTER_PAIR_METRICS] = {
            "preprocess", "preprocess_print", "release", "read_estimate", "lex_estimate", "refine_estimate", "directives_estimate", "main_loop_estimate",
        };
        const uint64_t* v = rows[0][0].values;
        fprintf(stderr, "%s: valid 31 pairs + 3 warmups, input=%" PRIu64 " output=%" PRIu64 " bytes=%" PRIu64
                        " digest=%" PRIu64 " token_digest=%" PRIu64 " history_bytes=%" PRIu64 " temporary_peak=%" PRIu64
                        " expansions=%" PRIu64 " substitutions=%" PRIu64 " generated=%" PRIu64 " runs=%" PRIu64 "\n",
                path, v[3], v[4], v[5], v[11], v[12], v[13], v[14], v[15], v[16], v[17], v[18]);
        for (uint32_t metric = 0; metric < BUSTER_PAIR_METRICS; metric += 1)
        {
            double values[2][BUSTER_PAIR_ROUNDS];
            double ratios[BUSTER_PAIR_ROUNDS];
            for (uint32_t i = 0; i < BUSTER_PAIR_ROUNDS; i += 1)
            {
                values[0][i] = pair_metric(&rows[i + BUSTER_PAIR_WARMUPS][0], metric);
                values[1][i] = pair_metric(&rows[i + BUSTER_PAIR_WARMUPS][1], metric);
                ratios[i] = values[0][i] != 0 ? values[1][i] / values[0][i] : 0;
            }
            pair_sort(values[0], BUSTER_PAIR_ROUNDS);
            pair_sort(values[1], BUSTER_PAIR_ROUNDS);
            pair_sort(ratios, BUSTER_PAIR_ROUNDS);
            printf("%s\t%s\t31", path, names[metric]);
            for (uint32_t variant = 0; variant < 2; variant += 1)
            {
                printf("\t%.6f\t%.6f\t%.6f", values[variant][15], values[variant][3], values[variant][27]);
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
    puts("input\tmetric\tpairs\ta_median_ms\ta_p10_ms\ta_p90_ms\tb_median_ms\tb_p10_ms\tb_p90_ms\tmedian_ratio\tpaired_median_ratio\tpaired_p10_ratio\tpaired_p90_ratio");
    for (int i = 1; valid && i < argc; i += 1)
    {
        valid = pair_reduce(argv[i]);
    }
    valid = fflush(stdout) == 0 && !ferror(stdout) && valid;
    return valid ? 0 : 1;
}
