// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Bounded offline CI latency summary. Input is job-id, event class, metric and
// integer seconds, separated by tabs. API collection belongs to timings.sh;
// unavailable timestamps produce no observation, never an invented zero.
// Administrative statistics are outside the compiler and measurement hot paths.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_TIMINGS_SAMPLES 256
#define BUSTER_TIMINGS_LINE 256
#define BUSTER_TIMINGS_SECONDS 604800

static const char* const timing_groups[] = { "pr", "main", "periodic" };
static const char* const timing_group_labels[] = { "PR", "main push", "scheduled/manual" };
static const char* const timing_metrics[] = { "queue", "execution", "runner-hook", "setup-build", "native-tests", "benchmark-steps" };

typedef struct TimingSample TimingSample;
struct TimingSample
{
    uint64_t id;
    uint32_t seconds;
};

typedef struct TimingSeries TimingSeries;
struct TimingSeries
{
    TimingSample samples[BUSTER_TIMINGS_SAMPLES];
    size_t count;
};

typedef struct Timings Timings;
struct Timings
{
    TimingSeries series[3][6];
};

static bool timing_number(const char* text, uint64_t limit, uint64_t* value)
{
    bool valid = *text != 0;
    uint64_t result = 0;
    for (size_t i = 0; text[i] != 0 && valid; i += 1)
    {
        unsigned int digit = (unsigned char)text[i] - (unsigned int)'0';
        valid = digit <= 9 && result <= limit / 10 && (result != limit / 10 || digit <= limit % 10);
        if (valid)
        {
            result = result * 10 + digit;
        }
    }
    *value = result;
    return valid;
}

static size_t timing_name(const char* name, const char* const* names, size_t count)
{
    size_t result = count;
    for (size_t i = 0; i < count; i += 1)
    {
        if (strcmp(name, names[i]) == 0)
        {
            result = i;
        }
    }
    return result;
}

static bool timing_add(Timings* timings, const char* input)
{
    size_t length = strlen(input);
    bool valid = length != 0 && length < BUSTER_TIMINGS_LINE && input[length - 1] == '\n';
    char line[BUSTER_TIMINGS_LINE];
    char* fields[4] = { NULL };
    size_t count = 0;
    if (valid)
    {
        memcpy(line, input, length);
        line[length - 1] = 0;
        fields[count++] = line;
        for (size_t i = 0; i + 1 < length && valid; i += 1)
        {
            if (line[i] == '\t')
            {
                valid = count < sizeof(fields) / sizeof(fields[0]);
                if (valid)
                {
                    line[i] = 0;
                    fields[count++] = line + i + 1;
                }
            }
        }
        valid = valid && count == sizeof(fields) / sizeof(fields[0]);
    }
    uint64_t id = 0;
    uint64_t seconds = 0;
    size_t group = 0;
    size_t metric = 0;
    if (valid)
    {
        group = timing_name(fields[1], timing_groups, sizeof(timing_groups) / sizeof(timing_groups[0]));
        metric = timing_name(fields[2], timing_metrics, sizeof(timing_metrics) / sizeof(timing_metrics[0]));
        valid = group < 3 && metric < 6 && timing_number(fields[0], UINT64_MAX, &id) && id != 0;
        valid = valid && timing_number(fields[3], BUSTER_TIMINGS_SECONDS, &seconds);
    }
    if (valid)
    {
        TimingSeries* series = &timings->series[group][metric];
        const size_t existing = series->count;
        valid = existing < BUSTER_TIMINGS_SAMPLES;
        // Do not weight a repeated page/job more heavily in the quantiles.
        for (size_t i = 0; i < existing && valid; i += 1)
        {
            valid = series->samples[i].id != id;
        }
        if (valid)
        {
            series->samples[existing] = (TimingSample){ .id = id, .seconds = (uint32_t)seconds };
            series->count = existing + 1;
        }
    }
    return valid;
}

static int timing_compare(const void* left, const void* right)
{
    const TimingSample* a = left;
    const TimingSample* b = right;
    return (a->seconds > b->seconds) - (a->seconds < b->seconds);
}

static bool timing_quantiles(TimingSeries* series, double* median, uint32_t* p95)
{
    const size_t count = series->count;
    bool valid = count != 0;
    if (valid)
    {
        qsort(series->samples, count, sizeof(series->samples[0]), timing_compare);
        const uint32_t lower = series->samples[(count - 1) / 2].seconds;
        const uint32_t upper = series->samples[count / 2].seconds;
        *median = (double)lower + ((double)upper - lower) / 2;
        *p95 = series->samples[(count * 95 + 99) / 100 - 1].seconds;
    }
    return valid;
}

static int timing_report(FILE* input, FILE* output)
{
    Timings timings = { 0 };
    char line[BUSTER_TIMINGS_LINE];
    bool valid = true;
    size_t rows = 0;
    while (valid && fgets(line, sizeof(line), input) != NULL)
    {
        rows += 1;
        valid = timing_add(&timings, line);
    }
    valid = valid && !ferror(input);
    if (!valid)
    {
        fprintf(stderr, "timings: invalid, duplicate or over-capacity observation at row %zu\n", rows);
    }
    else
    {
        fputs("## 9700X CI latency\n\n", output);
        fputs("Successful hardware jobs only; seconds of wall time. Event classes are not benchmark-coverage certificates.\n\n", output);
        fputs("| Event class | Metric | Samples | Median (s) | p95 (s) |\n| --- | --- | ---: | ---: | ---: |\n", output);
        for (size_t group = 0; group < 3; group += 1)
        {
            for (size_t metric = 0; metric < 6; metric += 1)
            {
                TimingSeries* series = &timings.series[group][metric];
                double median = 0;
                uint32_t p95 = 0;
                if (timing_quantiles(series, &median, &p95))
                {
                    fprintf(output, "| %s | %s | %zu | %.1f | %u |\n", timing_group_labels[group], timing_metrics[metric], series->count, median, p95);
                }
            }
        }
        if (rows == 0)
        {
            fputs("\nNo eligible observations were returned; this is not zero latency.\n", output);
        }
        fputs("\np95 uses the nearest-rank definition. Missing timestamps are omitted.\n", output);
        fputs("`runner-hook` includes hook overhead and possible lock waiting; it is **not** an exact lock-wait measurement. Exact lock acquisition/release requires runner-hook telemetry and is not inferred here.\n", output);
        fputs("`setup-build` spans host verification through the start of native tests. `benchmark-steps` is the sum of successful benchmark/measurement step durations, not a performance result.\n", output);
        valid = fflush(output) == 0;
    }
    return valid ? 0 : 1;
}

static int timing_test(void)
{
    Timings timings = { 0 };
    unsigned int checks = 0;
    unsigned int failures = 0;
    const char* invalid[] = {
        "", "1\tpr\tqueue\t1", "1\tpr\tqueue\t1\nextra", "0\tpr\tqueue\t1\n",
        "-1\tpr\tqueue\t1\n", "18446744073709551616\tpr\tqueue\t1\n", "1\tunknown\tqueue\t1\n",
        "1\tpr\tunknown\t1\n", "1\tpr\tqueue\t-1\n", "1\tpr\tqueue\t604801\n",
        "1\tpr\tqueue\t1.5\n", "1\tpr\tqueue\t1\textra\n", "1\tpr\tqueue\t\n", "1\tpr\tqueue\t 1\n",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        checks += 1;
        failures += timing_add(&timings, invalid[i]);
    }
    double median = 0;
    uint32_t p95 = 0;
    checks += 1;
    failures += timing_quantiles(&timings.series[0][0], &median, &p95);
    for (unsigned int i = 1; i <= BUSTER_TIMINGS_SAMPLES; i += 1)
    {
        char row[BUSTER_TIMINGS_LINE];
        snprintf(row, sizeof(row), "%u\tpr\tqueue\t%u\n", i, i);
        checks += 1;
        failures += !timing_add(&timings, row);
        if (i == 1 || i == 2 || i == 100)
        {
            checks += 1;
            bool valid = timing_quantiles(&timings.series[0][0], &median, &p95);
            failures += !valid || median != (i + 1) / 2.0 || p95 != (i * 95 + 99) / 100;
        }
    }
    checks += 1;
    failures += timing_add(&timings, "257\tpr\tqueue\t1\n");
    checks += 1;
    failures += !timing_add(&timings, "1\tmain\tqueue\t0\n");
    checks += 1;
    failures += timing_add(&timings, "1\tmain\tqueue\t0\n");
    checks += 1;
    failures += !timing_add(&timings, "18446744073709551615\tperiodic\texecution\t604800\n");
    checks += 1;
    failures += !timing_quantiles(&timings.series[1][0], &median, &p95) || median != 0 || p95 != 0;
    printf("timings --self-test: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}

int main(int argc, char** argv)
{
    int status = 2;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        status = timing_test();
    }
    else if (argc == 1)
    {
        status = timing_report(stdin, stdout);
    }
    else
    {
        fputs("usage: timings [--self-test] < observations.tsv\n", stderr);
    }
    return status;
}
