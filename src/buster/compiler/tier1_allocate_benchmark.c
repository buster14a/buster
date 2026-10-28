// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Whole interval preparation + assignment, including validation, position counting sort, allocation, fixed-mask
// filtering, pressure construction, coloring/fallback, fragment publication and arena reset. Input generation is
// untimed. Synthetic intervals are not source-corpus or generated-program speed evidence.
// Run each process under the shared 9700X lock, pinned to CPU 2. Options: --only vector|scalar, --after-scalar MS.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

#include "buster/compiler/lane.c"
#include "buster/compiler/tier1_allocate.c"

#define BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES 11

static uint64_t tier1_allocate_benchmark_now(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &time);
    return (uint64_t)time.tv_sec * 1000000000u + (uint64_t)time.tv_nsec;
}

static int tier1_allocate_benchmark_counter(void)
{
    struct perf_event_attr attributes = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t tier1_allocate_benchmark_cycles(int descriptor)
{
    uint64_t cycles = 0;
    if (descriptor < 0 || read(descriptor, &cycles, sizeof(cycles)) != sizeof(cycles))
    {
        cycles = 0;
    }
    return cycles;
}

static double tier1_allocate_benchmark_median(const double* samples)
{
    double ordered[BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES];
    for (uint32_t i = 0; i < BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES; i += 1)
    {
        uint32_t destination = i;
        while (destination != 0 && ordered[destination - 1] > samples[i])
        {
            ordered[destination] = ordered[destination - 1];
            destination -= 1;
        }
        ordered[destination] = samples[i];
    }
    return ordered[BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES / 2];
}

static __attribute__((noinline, aligned(64))) bool tier1_allocate_benchmark_batch(const Tier1AllocationInput* input, Arena* arena, bool vector, uint32_t repetitions, uint64_t* checksum)
{
    bool valid = true;
    const uint64_t saved = arena->length;
    for (uint32_t repetition = 0; valid && repetition < repetitions; repetition += 1)
    {
        Tier1Allocation output = { 0 };
        const Tier1AllocationError error = vector ? tier1_allocate(input, arena, &output) : tier1_allocate_scalar(input, arena, &output);
        valid = error == TIER1_ALLOCATION_OK;
        if (valid)
        {
            *checksum += output.fragment_count + output.spill_count + output.registers[input->count - 1];
        }
        arena_pop_to(arena, saved);
    }
    return valid;
}

int run_tier1_allocate_benchmark(int argc, char** argv);
int run_tier1_allocate_benchmark(int argc, char** argv)
{
    uint32_t only = 2;
    uint32_t after_scalar = 0;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool wants_only = strcmp(argv[i], "--only") == 0;
        const bool wants_warm = strcmp(argv[i], "--after-scalar") == 0;
        valid = (wants_only || wants_warm) && i + 1 < argc;
        if (valid)
        {
            i += 1;
            if (wants_only)
            {
                valid = strcmp(argv[i], "vector") == 0 || strcmp(argv[i], "scalar") == 0;
                only = strcmp(argv[i], "vector") == 0 ? 0 : 1;
            }
            else
            {
                char* end = 0;
                const unsigned long parsed = strtoul(argv[i], &end, 10);
                valid = argv[i][0] >= '0' && argv[i][0] <= '9' && end != argv[i] && *end == 0 && parsed <= 1000;
                after_scalar = (uint32_t)parsed;
            }
        }
    }
    Arena input_arena = arena_reserve(16u << 20);
    Arena arena = arena_reserve(64u << 20);
    valid = valid && input_arena.base != 0 && arena.base != 0;
    const int cycles_descriptor = tier1_allocate_benchmark_counter();
    uint64_t checksum = 0;
    const uint32_t counts[] = { 16, 128, 2048, 16384 };
    const char* names[] = { "tiny-fit", "small-fit", "large-fit", "large-pressure" };
    if (valid)
    {
        printf("allocation preparation+assignment; cycles=0 means counter unavailable; synthetic interval workloads\n");
    }
    for (uint32_t workload = 0; valid && workload < 4; workload += 1)
    {
        const uint32_t count = counts[workload];
        uint32_t* starts = arena_push_array(&input_arena, count, 4, 4);
        uint32_t* ends = arena_push_array(&input_arena, count, 4, 4);
        uint32_t* values = arena_push_array(&input_arena, count, 4, 4);
        uint32_t* allowed = arena_push_array(&input_arena, count, 4, 4);
        uint32_t* uses = arena_push_array(&input_arena, count, 4, 4);
        uint32_t* offsets = arena_push_array(&input_arena, (uint64_t)count + 1, 4, 4);
        uint8_t* classes = arena_push_array(&input_arena, count, 1, 1);
        uint8_t* remat = arena_push_array(&input_arena, count, 1, 1);
        valid = starts != 0 && ends != 0 && values != 0 && allowed != 0 && uses != 0 && offsets != 0 && classes != 0 && remat != 0;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            starts[i] = i * 2;
            ends[i] = starts[i] + (workload == 3 ? 96 + i % 32 : 8 + i % 8);
            values[i] = i;
            allowed[i] = 0xffef;
            uses[i] = ends[i] - 1;
            offsets[i] = i;
            classes[i] = (uint8_t)(i & 1);
            remat[i] = i % 4 == 0 ? 1 : 0;
        }
        if (valid)
        {
            offsets[count] = count;
        }
        const Tier1AllocationInput input = {
            .starts = starts,
            .ends = ends,
            .values = values,
            .allowed_registers = allowed,
            .classes = classes,
            .rematerializable = remat,
            .use_offsets = offsets,
            .use_positions = uses,
            .use_count = count,
            .count = count,
            .value_count = count,
            .position_count = count * 2 + 128,
            .registers = { 0xffef, 0xffef },
        };
        Tier1Allocation output = { 0 };
        valid = valid && tier1_allocate(&input, &arena, &output) == TIER1_ALLOCATION_OK;
        if (valid)
        {
            valid = tier1_allocation_verify(&input, &output);
        }
        const uint32_t fallback = output.scalar_intervals;
        const uint32_t spills = output.spill_count;
        const uint64_t bytes = output.retained_bytes + output.scratch_bytes;
        arena_pop_to(&arena, 0);
        valid = valid && tier1_allocate_scalar(&input, &arena, &output) == TIER1_ALLOCATION_OK && tier1_allocation_verify(&input, &output);
        arena_pop_to(&arena, 0);
        uint32_t repetitions = 1;
        uint64_t elapsed = 0;
        while (valid && elapsed < 2000000 && repetitions < (1u << 20))
        {
            const uint64_t begin = tier1_allocate_benchmark_now();
            valid = tier1_allocate_benchmark_batch(&input, &arena, true, repetitions, &checksum);
            elapsed = tier1_allocate_benchmark_now() - begin;
            if (elapsed < 2000000)
            {
                repetitions *= 2;
            }
        }
        double nanoseconds[2][BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES] = { { 0 } };
        double cycles[2][BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES] = { { 0 } };
        for (uint32_t pass = 0; valid && pass < BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES; pass += 1)
        {
            for (uint32_t order = 0; valid && order < 2; order += 1)
            {
                const uint32_t variant = (pass + order) & 1;
                if (only == 2 || variant == only)
                {
                    if (variant == 0 && after_scalar != 0)
                    {
                        const uint64_t until = tier1_allocate_benchmark_now() + (uint64_t)after_scalar * 1000000;
                        while (valid && tier1_allocate_benchmark_now() < until)
                        {
                            valid = tier1_allocate_benchmark_batch(&input, &arena, false, 1, &checksum);
                        }
                    }
                    const uint64_t begin_cycles = tier1_allocate_benchmark_cycles(cycles_descriptor);
                    const uint64_t begin = tier1_allocate_benchmark_now();
                    valid = valid && tier1_allocate_benchmark_batch(&input, &arena, variant == 0, repetitions, &checksum);
                    const uint64_t end = tier1_allocate_benchmark_now();
                    const uint64_t end_cycles = tier1_allocate_benchmark_cycles(cycles_descriptor);
                    nanoseconds[variant][pass] = (double)(end - begin) / repetitions / count;
                    cycles[variant][pass] = (double)(end_cycles - begin_cycles) / repetitions / count;
                }
            }
        }
        for (uint32_t variant = 0; valid && variant < 2; variant += 1)
        {
            if (only == 2 || variant == only)
            {
                double minimum = nanoseconds[variant][0];
                double maximum = minimum;
                for (uint32_t pass = 1; pass < BUSTER_TIER1_ALLOCATE_BENCHMARK_SAMPLES; pass += 1)
                {
                    minimum = nanoseconds[variant][pass] < minimum ? nanoseconds[variant][pass] : minimum;
                    maximum = nanoseconds[variant][pass] > maximum ? nanoseconds[variant][pass] : maximum;
                }
                printf("%-15s %-6s %5u intervals: %.3f ns/interval [%.3f, %.3f], %.3f cycles/interval; vector fallback %u, spill slots %u, peak arena %" PRIu64 " B\n", names[workload], variant == 0 ? "vector" : "scalar", count, tier1_allocate_benchmark_median(nanoseconds[variant]), minimum, maximum, tier1_allocate_benchmark_median(cycles[variant]), fallback, spills, bytes);
            }
        }
        arena_pop_to(&input_arena, 0);
    }
    if (valid)
    {
        printf("checksum %" PRIu64 "\n", checksum);
    }
    else
    {
        fprintf(stderr, "benchmark-tier1-allocation failed or invalid options: [--only scalar|vector] [--after-scalar MS]\n");
    }
    if (cycles_descriptor >= 0)
    {
        close(cycles_descriptor);
    }
    arena_release(&arena);
    arena_release(&input_arena);
    return valid ? 0 : 1;
}

#else
int run_tier1_allocate_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
