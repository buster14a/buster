// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original scheduling experiment: fixed machine-record blocks -> effects/model/graph -> order -> copy -> encoder.
// Reports the complete handoff beside unscheduled encoding, then executes one original integer-loop fixture with
// and without the schedule. This is not speedtest1, a source compiler or corpus acceptance for #172/#181.
// Run every invocation under the 9700X per-run lock, pinned to CPU 2. --only and --after-scalar expose both vector
// states. Timing/perf helpers below are original, with no imported timing harness or third-party cost data.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include "host_isa.h"

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#include "buster/compiler/x86_semantics.c"
#include "buster/compiler/x86_zen5.c"
#pragma clang diagnostic pop
#include "buster/compiler/tier1_schedule.c"
#include "buster/compiler/tier1_schedule_fixture.c"

#define BUSTER_TIER1_SCHEDULE_BENCH_PASSES 11
#define BUSTER_TIER1_SCHEDULE_BENCH_SAMPLE_NS UINT64_C(2000000)
#define BUSTER_TIER1_SCHEDULE_BENCH_BLOCKS 64

typedef struct Tier1ScheduleBench Tier1ScheduleBench;
struct Tier1ScheduleBench
{
    X86Instruction* records;
    uint32_t blocks;
    uint32_t count;
    Tier1ScheduleGraph graph;
    uint16_t order[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    X86Instruction reordered[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint8_t code[BUSTER_X86_ENCODER_CAPACITY(BUSTER_TIER1_SCHEDULE_MAX_RECORDS)];
    uint32_t offsets[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    volatile uint64_t sink;
};

typedef struct Tier1ScheduleBenchSample Tier1ScheduleBenchSample;
struct Tier1ScheduleBenchSample
{
    double ns;
    double cycles;
};

static uint64_t tier1_schedule_bench_clock(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC_RAW, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static int tier1_schedule_bench_counter(void)
{
    struct perf_event_attr event = {
        .size = sizeof(event),
        .type = PERF_TYPE_HARDWARE,
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    return (int)syscall(SYS_perf_event_open, &event, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static double tier1_schedule_bench_median(double* samples)
{
    // Repeated minimum selection is deliberately a cold, small, original reduction rather than a copied harness.
    for (uint32_t i = 0; i <= BUSTER_TIER1_SCHEDULE_BENCH_PASSES / 2; i += 1)
    {
        uint32_t minimum = i;
        for (uint32_t j = i + 1; j < BUSTER_TIER1_SCHEDULE_BENCH_PASSES; j += 1)
        {
            if (samples[j] < samples[minimum])
            {
                minimum = j;
            }
        }
        const double saved = samples[i];
        samples[i] = samples[minimum];
        samples[minimum] = saved;
    }
    return samples[BUSTER_TIER1_SCHEDULE_BENCH_PASSES / 2];
}

static X86Instruction tier1_schedule_bench_record(uint16_t form, uint8_t reg, uint8_t rm)
{
    const X86Instruction record = {
        .form = form,
        .reg = reg,
        .rm = rm,
        .mode = BUSTER_X86_REGISTER_DIRECT,
    };
    return record;
}

static void tier1_schedule_bench_fill(Tier1ScheduleBench* bench)
{
    uint32_t state = 172;
    for (uint32_t block = 0; block < bench->blocks; block += 1)
    {
        for (uint32_t i = 0; i < bench->count; i += 1)
        {
            state = state * 1664525u + 1013904223u;
            const uint8_t rm = X86_REGISTER_R8 + ((state >> 8) & 7);
            const uint8_t reg = X86_REGISTER_R8 + ((state >> 12) & 7);
            X86Instruction record = tier1_schedule_bench_record(X86_FORM_NOT_RM64, 0, rm);
            if (i % 7 == 0)
            {
                record = tier1_schedule_bench_record(X86_FORM_IMUL_R64_RM64, reg, rm);
            }
            else if (i % 11 == 0)
            {
                record = tier1_schedule_bench_record(X86_FORM_ADD_RM64_R64, reg, rm);
            }
            else if (i % 29 == 0)
            {
                record = tier1_schedule_bench_record(X86_FORM_MOV_R64_IMM64, 0, rm);
                record.immediate = 4660;
            }
            bench->records[(uint64_t)block * bench->count + i] = record;
        }
    }
}

static bool tier1_schedule_bench_run(Tier1ScheduleBench* bench, uint32_t variant)
{
    bool valid = true;
    uint64_t sum = 0;
    for (uint32_t block = 0; valid && block < bench->blocks; block += 1)
    {
        const X86Instruction* records = bench->records + (uint64_t)block * bench->count;
        if (variant != 0)
        {
            const Tier1ScheduleInput input = { .instructions = records, .count = bench->count };
            valid = tier1_schedule_block(input, &bench->graph, bench->order, bench->count, variant == 2, 0) == TIER1_SCHEDULE_OK;
            for (uint32_t i = 0; valid && i < bench->count; i += 1)
            {
                bench->reordered[i] = records[bench->order[i]];
            }
            records = bench->reordered;
        }
        if (valid)
        {
            const int64_t bytes = x86_encode(records, bench->count, bench->code, sizeof(bench->code), bench->offsets, 0);
            valid = bytes >= 0;
            sum += (uint64_t)bytes + bench->code[0] + bench->offsets[bench->count - 1];
        }
    }
    bench->sink = sum;
    return valid;
}

static bool tier1_schedule_bench_check(Tier1ScheduleBench* bench, uint64_t* moved, uint64_t* missing)
{
    bool valid = true;
    uint16_t scalar_order[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint8_t scalar_code[BUSTER_X86_ENCODER_CAPACITY(BUSTER_TIER1_SCHEDULE_MAX_RECORDS)];
    *moved = 0;
    *missing = 0;
    for (uint32_t block = 0; valid && block < bench->blocks; block += 1)
    {
        const X86Instruction* records = bench->records + (uint64_t)block * bench->count;
        const Tier1ScheduleInput input = { .instructions = records, .count = bench->count };
        Tier1ScheduleStats stats = { 0 };
        valid = x86_validate(records, bench->count) == bench->count &&
                tier1_schedule_block(input, &bench->graph, scalar_order, bench->count, false, &stats) == TIER1_SCHEDULE_OK;
        *moved += stats.moved;
        *missing += stats.missing_columns[X86_COST_GPR_LATENCY];
        valid = valid && tier1_schedule_verify(input, scalar_order) &&
                tier1_schedule_block(input, &bench->graph, bench->order, bench->count, true, 0) == TIER1_SCHEDULE_OK &&
                memcmp(scalar_order, bench->order, bench->count * sizeof(scalar_order[0])) == 0 && tier1_schedule_verify(input, bench->order);
        if (valid)
        {
            for (uint32_t i = 0; i < bench->count; i += 1)
            {
                bench->reordered[i] = records[bench->order[i]];
            }
            const int64_t scalar_bytes = x86_encode_scalar(bench->reordered, bench->count, scalar_code, sizeof(scalar_code), 0, 0);
            const int64_t vector_bytes = x86_encode(bench->reordered, bench->count, bench->code, sizeof(bench->code), 0, 0);
            valid = scalar_bytes >= 0 && scalar_bytes == vector_bytes && memcmp(scalar_code, bench->code, (uint64_t)scalar_bytes) == 0;
        }
    }
    return valid;
}

static Tier1ScheduleBenchSample tier1_schedule_bench_sample(Tier1ScheduleBench* bench, uint32_t variant, uint32_t repeats, int counter, bool* valid)
{
    uint64_t before[3] = { 0 };
    uint64_t after[3] = { 0 };
    bool counted = counter >= 0 && read(counter, before, sizeof(before)) == sizeof(before) && ioctl(counter, PERF_EVENT_IOC_ENABLE, 0) == 0;
    const uint64_t start = tier1_schedule_bench_clock();
    for (uint32_t i = 0; i < repeats; i += 1)
    {
        *valid &= tier1_schedule_bench_run(bench, variant);
    }
    const uint64_t end = tier1_schedule_bench_clock();
    if (counted)
    {
        counted = ioctl(counter, PERF_EVENT_IOC_DISABLE, 0) == 0 && read(counter, after, sizeof(after)) == sizeof(after) &&
                  after[1] > before[1] && after[1] - before[1] == after[2] - before[2];
    }
    const double items = (double)repeats * bench->blocks * bench->count;
    const Tier1ScheduleBenchSample sample = {
        .ns = (end - start) / items,
        .cycles = counted ? (after[0] - before[0]) / items : -1.0,
    };
    return sample;
}

static bool tier1_schedule_bench_measure(Tier1ScheduleBench* bench, uint32_t only, uint32_t after_scalar)
{
    bool valid = true;
    const int counter = tier1_schedule_bench_counter();
    uint32_t repeats[3] = { 1, 1, 1 };
    double ns[3][BUSTER_TIER1_SCHEDULE_BENCH_PASSES];
    double cycles[3][BUSTER_TIER1_SCHEDULE_BENCH_PASSES];
    for (uint32_t variant = 0; variant < 3; variant += 1)
    {
        if (only == 3 || only == variant)
        {
            bool enough = false;
            while (!enough)
            {
                const uint64_t start = tier1_schedule_bench_clock();
                for (uint32_t repeat = 0; repeat < repeats[variant]; repeat += 1)
                {
                    valid &= tier1_schedule_bench_run(bench, variant);
                }
                enough = tier1_schedule_bench_clock() - start >= BUSTER_TIER1_SCHEDULE_BENCH_SAMPLE_NS || repeats[variant] >= (1u << 20);
                repeats[variant] *= enough ? 1 : 2;
            }
        }
    }
    for (uint32_t pass = 0; pass < BUSTER_TIER1_SCHEDULE_BENCH_PASSES; pass += 1)
    {
        for (uint32_t offset = 0; offset < 3; offset += 1)
        {
            const uint32_t variant = (pass + offset) % 3;
            if (only == 3 || only == variant)
            {
                if (variant == 2 && after_scalar != 0)
                {
                    const uint64_t start = tier1_schedule_bench_clock();
                    while (tier1_schedule_bench_clock() - start < (uint64_t)after_scalar * 1000000)
                    {
                        valid &= tier1_schedule_bench_run(bench, 1);
                    }
                }
                const Tier1ScheduleBenchSample sample = tier1_schedule_bench_sample(bench, variant, repeats[variant], counter, &valid);
                ns[variant][pass] = sample.ns;
                cycles[variant][pass] = sample.cycles;
            }
        }
    }
    const char* names[] = { "encode only", "scalar schedule + copy + encode", "vector schedule + copy + encode" };
    for (uint32_t variant = 0; variant < 3; variant += 1)
    {
        if (only == 3 || only == variant)
        {
            const double median_ns = tier1_schedule_bench_median(ns[variant]);
            const double median_cycles = tier1_schedule_bench_median(cycles[variant]);
            printf("%-34s %.4f ns/record", names[variant], median_ns);
            if (median_cycles >= 0)
            {
                printf("  %.4f core cycles/record", median_cycles);
            }
            else
            {
                printf("  core cycles unavailable");
            }
            printf("\n");
        }
    }
    if (counter >= 0)
    {
        close(counter);
    }
    return valid;
}

static bool tier1_schedule_bench_code(Tier1ScheduleBench* bench)
{
    Tier1ScheduleFixture fixture = { 0 };
    const bool valid = tier1_schedule_fixture_open(&fixture, &bench->graph, bench->order, true) && tier1_schedule_fixture_check(&fixture);
    if (valid)
    {
        double samples[2][BUSTER_TIER1_SCHEDULE_BENCH_PASSES];
        const uint64_t iterations = 32768;
        for (uint32_t pass = 0; pass < BUSTER_TIER1_SCHEDULE_BENCH_PASSES; pass += 1)
        {
            for (uint32_t offset = 0; offset < 2; offset += 1)
            {
                const uint32_t variant = (pass + offset) % 2;
                const uint64_t start = tier1_schedule_bench_clock();
                bench->sink = tier1_schedule_fixture_execute(fixture.code[variant], 11939, 23, iterations);
                const uint64_t end = tier1_schedule_bench_clock();
                samples[variant][pass] = (end - start) / (double)iterations;
            }
        }
        printf("Synthetic integer loop only: %u/%u records moved, bytes=%" PRId64 "/%" PRId64 ", baseline %.4f ns/iteration, scheduled %.4f ns/iteration\n",
               fixture.moved, BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS, fixture.sizes[0], fixture.sizes[1], tier1_schedule_bench_median(samples[0]), tier1_schedule_bench_median(samples[1]));
    }
    tier1_schedule_fixture_close(&fixture);
    return valid;
}

static bool tier1_schedule_bench_number(const char* text, uint32_t maximum, uint32_t* output)
{
    char* end = 0;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 10);
    const bool valid = errno == 0 && end != text && *end == 0 && value <= maximum;
    if (valid)
    {
        *output = (uint32_t)value;
    }
    return valid;
}

int run_tier1_schedule_benchmark(int argc, char** argv)
{
    Tier1ScheduleBench bench = { .blocks = BUSTER_TIER1_SCHEDULE_BENCH_BLOCKS, .count = 64 };
    uint32_t only = 3;
    uint32_t after_scalar = 0;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--blocks") == 0 && i + 1 < argc)
        {
            valid = tier1_schedule_bench_number(argv[i + 1], 4096, &bench.blocks) && bench.blocks != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--records") == 0 && i + 1 < argc)
        {
            valid = tier1_schedule_bench_number(argv[i + 1], BUSTER_TIER1_SCHEDULE_MAX_RECORDS, &bench.count) && bench.count != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            valid = tier1_schedule_bench_number(argv[i + 1], 10000, &after_scalar);
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = strcmp(argv[i + 1], "encode") == 0 ? 0 : strcmp(argv[i + 1], "scalar") == 0 ? 1 : strcmp(argv[i + 1], "vector") == 0 ? 2 : 4;
            valid = only <= 2;
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    valid = valid && x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
    if (valid)
    {
        bench.records = malloc((uint64_t)bench.blocks * bench.count * sizeof(*bench.records));
        valid = bench.records != 0;
    }
    if (valid)
    {
        tier1_schedule_bench_fill(&bench);
        uint64_t moved;
        uint64_t missing;
        valid = tier1_schedule_bench_check(&bench, &moved, &missing);
        printf("Tier 1 scheduling machine-record experiment: %u blocks x %u records, %zu scratch bytes, moved=%" PRIu64 ", missing GPR latency=%" PRIu64 ", after-scalar=%u ms\n",
               bench.blocks, bench.count, sizeof(bench.graph), moved, missing, after_scalar);
        if (valid)
        {
            valid = tier1_schedule_bench_measure(&bench, only, after_scalar) && tier1_schedule_bench_code(&bench);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "benchmark-tier1-schedule: invalid arguments, allocation, model/graph/encoding disagreement or generated-program mismatch\n");
    }
    free(bench.records);
    return valid ? 0 : 1;
}

#else
int run_tier1_schedule_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
