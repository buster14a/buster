// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Bounded offline CI latency summary. Input is job-id, event class, metric and
// integer seconds, separated by tabs. API collection belongs to timings.sh;
// unavailable timestamps produce no observation, never an invented zero.
// Administrative statistics are outside the compiler and measurement hot paths.
//
// `timings --queue` reports the merge queue's candidates instead (#1430): one
// line per completed candidate run, its id, outcome (merged, passed, failed,
// cancelled), wall seconds and runner seconds. A candidate that didn't merge
// spent its runner time on nothing that landed, the cost of rebuilds and of
// failures the review of #1430 asks to be reported.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_TIMINGS_SAMPLES 256
#define BUSTER_TIMINGS_LINE 256
#define BUSTER_TIMINGS_SECONDS 604800

// Pull requests' jobs, main pushes', the nightly backlog's targeted runs, and full ones (the daily run, full dispatches
// and the nightly snapshot's comprehensive run).
static const char* const timing_groups[] = { "pr", "main", "targeted", "full" };
static const char* const timing_group_labels[] = { "PR", "main push", "nightly targeted", "full (daily, dispatched, snapshot)" };
static const char* const timing_metrics[] = { "queue", "execution", "runner-hook", "lock-wait", "lock-hold", "setup-build", "native-tests", "benchmark-steps", "host-occupation" };
#define BUSTER_TIMINGS_GROUPS (sizeof(timing_groups) / sizeof(timing_groups[0]))
#define BUSTER_TIMINGS_METRICS (sizeof(timing_metrics) / sizeof(timing_metrics[0]))

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
    TimingSeries series[BUSTER_TIMINGS_GROUPS][BUSTER_TIMINGS_METRICS];
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
        valid = group < BUSTER_TIMINGS_GROUPS && metric < BUSTER_TIMINGS_METRICS && timing_number(fields[0], UINT64_MAX, &id) && id != 0;
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
        for (size_t group = 0; group < BUSTER_TIMINGS_GROUPS; group += 1)
        {
            for (size_t metric = 0; metric < BUSTER_TIMINGS_METRICS; metric += 1)
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
        fputs("`runner-hook` is the job-started hook's step, from the API's whole-second step times. `lock-wait` and `lock-hold` come from the hooks' own log lines instead, rounded to whole seconds: the wait runs from the job-started hook's start to its `this job has the machine`, the hold from there to the job-completed hook's `gave the machine back`, the host's time under this job's lock. Those lines bracket the lock operations, so each includes the hook's own work around them, tens of milliseconds, below the rounding.\n", output);
        fputs("`setup-build` spans host verification through the start of native tests. `benchmark-steps` is the sum of successful benchmark/measurement step durations, not a performance result.\n", output);
        fputs("`host-occupation` spans the job's first step after the runner hook (the lock is held by then) through the job's end: the time the host was this job's.\n", output);
        valid = fflush(output) == 0;
    }
    return valid ? 0 : 1;
}

// --- The merge queue's candidates (#1430) ---

#define BUSTER_TIMINGS_CANDIDATES 1024
#define BUSTER_TIMINGS_RUNNER (UINT64_C(1000) * BUSTER_TIMINGS_SECONDS)

static const char* const queue_outcomes[] = { "merged", "passed", "failed", "cancelled" };
static const char* const queue_outcome_labels[] = { "merged", "passed, not merged (invalidated)", "failed", "cancelled (dropped or invalidated)" };
#define BUSTER_TIMINGS_OUTCOMES (sizeof(queue_outcomes) / sizeof(queue_outcomes[0]))

typedef struct QueueCandidates QueueCandidates;
struct QueueCandidates
{
    uint64_t ids[BUSTER_TIMINGS_CANDIDATES];
    size_t count;
    uint64_t candidates[BUSTER_TIMINGS_OUTCOMES];
    uint64_t runner[BUSTER_TIMINGS_OUTCOMES];
    // The merged candidates' wall times, for their quantiles.
    TimingSeries merged;
};

static bool queue_add(QueueCandidates* queue, const char* input)
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
    uint64_t wall = 0;
    uint64_t runner = 0;
    size_t outcome = BUSTER_TIMINGS_OUTCOMES;
    if (valid)
    {
        outcome = timing_name(fields[1], queue_outcomes, BUSTER_TIMINGS_OUTCOMES);
        valid = outcome < BUSTER_TIMINGS_OUTCOMES && timing_number(fields[0], UINT64_MAX, &id) && id != 0;
        valid = valid && timing_number(fields[2], BUSTER_TIMINGS_SECONDS, &wall) && timing_number(fields[3], BUSTER_TIMINGS_RUNNER, &runner);
        valid = valid && queue->count < BUSTER_TIMINGS_CANDIDATES;
        for (size_t i = 0; i < queue->count && valid; i += 1)
        {
            valid = queue->ids[i] != id;
        }
    }
    if (valid && outcome == 0)
    {
        valid = queue->merged.count < BUSTER_TIMINGS_SAMPLES;
        if (valid)
        {
            queue->merged.samples[queue->merged.count] = (TimingSample){ .id = id, .seconds = (uint32_t)wall };
            queue->merged.count += 1;
        }
    }
    if (valid)
    {
        queue->ids[queue->count] = id;
        queue->count += 1;
        queue->candidates[outcome] += 1;
        queue->runner[outcome] += runner;
    }
    return valid;
}

static int queue_report(FILE* input, FILE* output)
{
    QueueCandidates* queue = calloc(1, sizeof(QueueCandidates));
    char line[BUSTER_TIMINGS_LINE];
    bool valid = queue != NULL;
    size_t rows = 0;
    while (valid && fgets(line, sizeof(line), input) != NULL)
    {
        rows += 1;
        valid = queue_add(queue, line);
    }
    valid = valid && !ferror(input);
    if (!valid)
    {
        fprintf(stderr, "timings: invalid, duplicate or over-capacity candidate at row %zu\n", rows);
    }
    else
    {
        uint64_t all = 0;
        uint64_t runner = 0;
        for (size_t i = 0; i < BUSTER_TIMINGS_OUTCOMES; i += 1)
        {
            all += queue->candidates[i];
            runner += queue->runner[i];
        }
        const uint64_t wasted = runner - queue->runner[0];
        fputs("\n## Merge-queue candidates\n\n", output);
        fputs("Completed `merge_group` runs of `9700x.yml`, by what became of the candidate: runner time is the sum of its jobs' own times.\n\n", output);
        fputs("| Outcome | Candidates | Runner-minutes |\n| --- | ---: | ---: |\n", output);
        for (size_t i = 0; i < BUSTER_TIMINGS_OUTCOMES; i += 1)
        {
            fprintf(output, "| %s | %llu | %.0f |\n", queue_outcome_labels[i], (unsigned long long)queue->candidates[i], (double)queue->runner[i] / 60);
        }
        fprintf(output, "| all | %llu | %.0f |\n\n", (unsigned long long)all, (double)runner / 60);
        if (all == 0)
        {
            fputs("No completed candidate runs were returned; the queue may not have run.\n", output);
        }
        else
        {
            fprintf(output, "Candidates that didn't merge took %.0f runner-minutes, %.1f%% of the queue's: the failures themselves, and the entries behind them that GitHub rebuilt, whose first runs passed, failed or were cancelled for nothing.\n", (double)wasted / 60, runner != 0 ? 100.0 * (double)wasted / (double)runner : 0.0);
        }
        double median = 0;
        uint32_t p95 = 0;
        if (timing_quantiles(&queue->merged, &median, &p95))
        {
            fprintf(output, "\nMerged candidates' wall time, from the run's start to its last update: median %.1f min, p95 %.1f min (%zu candidates).\n", median / 60, (double)p95 / 60, queue->merged.count);
        }
        valid = fflush(output) == 0;
    }
    free(queue);
    return valid ? 0 : 1;
}

static unsigned int queue_test(unsigned int* checks)
{
    unsigned int failures = 0;
    QueueCandidates* queue = calloc(1, sizeof(QueueCandidates));
    const char* invalid[] = {
        "", "1\tmerged\t60\t600", "0\tmerged\t60\t600\n", "1\tqueued\t60\t600\n", "1\tmerged\t604801\t600\n",
        "1\tmerged\t60\n", "1\tmerged\t60\t600\textra\n", "1\tmerged\t-1\t600\n", "1\tmerged\t60\t604800001\n",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        *checks += 1;
        failures += queue_add(queue, invalid[i]);
    }
    const char* valid[] = { "1\tmerged\t2236\t54720\n", "2\tfailed\t240\t1800\n", "3\tcancelled\t600\t6000\n", "4\tpassed\t2200\t54000\n", "5\tmerged\t2400\t55000\n" };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i += 1)
    {
        *checks += 1;
        failures += !queue_add(queue, valid[i]);
    }
    *checks += 1;
    failures += queue_add(queue, "3\tmerged\t60\t60\n");
    *checks += 1;
    failures += queue->count != 5 || queue->candidates[0] != 2 || queue->runner[0] != 109720 || queue->candidates[1] != 1 || queue->runner[2] != 1800 || queue->runner[3] != 6000 || queue->merged.count != 2;
    double median = 0;
    uint32_t p95 = 0;
    *checks += 1;
    failures += !timing_quantiles(&queue->merged, &median, &p95) || median != 2318 || p95 != 2400;
    free(queue);
    return failures;
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
    failures += !timing_add(&timings, "18446744073709551615\tfull\texecution\t604800\n");
    checks += 1;
    failures += !timing_add(&timings, "5\ttargeted\thost-occupation\t60\n");
    checks += 1;
    failures += !timing_add(&timings, "5\ttargeted\tlock-wait\t0\n") || !timing_add(&timings, "5\ttargeted\tlock-hold\t61\n");
    checks += 1;
    failures += timing_add(&timings, "5\tperiodic\texecution\t60\n");
    checks += 1;
    failures += !timing_quantiles(&timings.series[1][0], &median, &p95) || median != 0 || p95 != 0;
    failures += queue_test(&checks);
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
    else if (argc == 2 && strcmp(argv[1], "--queue") == 0)
    {
        status = queue_report(stdin, stdout);
    }
    else
    {
        fputs("usage: timings [--self-test] < observations.tsv\n       timings --queue < candidates.tsv\n", stderr);
    }
    return status;
}
