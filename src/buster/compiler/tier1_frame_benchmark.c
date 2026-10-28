// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// benchmark-tier1-frame: frozen original synthetic lifetime columns through preparation, coloring and frame layout.
// The total includes both arena operations, validation, sorting, every output store and prologue planning. The other
// rows isolate those same phases and do not stand for total time. The generator itself and native emission are outside
// the measured path. These are planner measurements, not C compilation or generated-program performance evidence.
//
// Twenty-one samples alternate paths; --only separates them, --after-scalar MS conditions every vector sample with
// the corresponding scalar phase. Run separate processes on the locked 9700X, pinned to CPU 2. CPU cycles are read
// from perf_event_open where permitted, otherwise explicitly unavailable. All timing/counter helpers are original.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#include "buster/compiler/lane.c"
#include "buster/compiler/c_types.c"
#include "buster/compiler/calling_convention.c"
#include "buster/compiler/tier1_frame.c"

#define BUSTER_TIER1_FRAME_BENCHMARK_PASSES 21u
#define BUSTER_TIER1_FRAME_BENCHMARK_ARENA_BYTES (256u << 20)

typedef enum Tier1FrameBenchmarkPhase
{
    TIER1_FRAME_BENCHMARK_PREPARE,
    TIER1_FRAME_BENCHMARK_COLOR,
    TIER1_FRAME_BENCHMARK_LAYOUT,
    TIER1_FRAME_BENCHMARK_TOTAL,
    TIER1_FRAME_BENCHMARK_PHASES,
} Tier1FrameBenchmarkPhase;

static const char* const tier1_frame_benchmark_phases[TIER1_FRAME_BENCHMARK_PHASES] = {
    "prepare",
    "color",
    "layout",
    "total",
};

static uint64_t tier1_frame_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}

static uint32_t tier1_frame_benchmark_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return (uint32_t)(*state >> 16);
}

static int tier1_frame_benchmark_counter(void)
{
    struct perf_event_attr attr = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    return (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t tier1_frame_benchmark_cycles(int fd)
{
    uint64_t result = 0;
    if (fd >= 0 && read(fd, &result, sizeof(result)) != sizeof(result))
    {
        result = 0;
    }
    return result;
}

static void tier1_frame_benchmark_sort(double* samples)
{
    for (uint32_t i = 1; i < BUSTER_TIER1_FRAME_BENCHMARK_PASSES; i += 1)
    {
        const double value = samples[i];
        uint32_t j = i;
        while (j != 0 && samples[j - 1] > value)
        {
            samples[j] = samples[j - 1];
            j -= 1;
        }
        samples[j] = value;
    }
}

static __attribute__((noinline, aligned(64))) Tier1FrameError tier1_frame_benchmark_repeat(const Tier1FrameRequest* request, Tier1FrameWork* prepared,
    Arena* output, Arena* scratch, Tier1FrameBenchmarkPhase phase, bool scalar, uint32_t repeats)
{
    Tier1FrameError error = TIER1_FRAME_OK;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    for (uint32_t repeat = 0; error == TIER1_FRAME_OK && repeat < repeats; repeat += 1)
    {
        Tier1Frame frame;
        Tier1FrameWork temporary;
        switch (phase)
        {
            case TIER1_FRAME_BENCHMARK_PREPARE:
            {
                error = tier1_frame_prepare(request, scratch, scalar, &temporary);
                __asm__ volatile("" : : "r"(&temporary) : "memory");
            } break;
            case TIER1_FRAME_BENCHMARK_COLOR:
            {
                if (scalar)
                {
                    tier1_frame_color_scalar(request, prepared);
                }
                else
                {
                    tier1_frame_color_vector(prepared);
                }
                __asm__ volatile("" : : "r"(prepared->color) : "memory");
            } break;
            case TIER1_FRAME_BENCHMARK_LAYOUT:
            {
                error = tier1_frame_layout(request, prepared, output, scalar, &frame);
                __asm__ volatile("" : : "r"(&frame) : "memory");
            } break;
            case TIER1_FRAME_BENCHMARK_TOTAL:
            {
                error = scalar ? tier1_frame_plan_scalar(request, output, scratch, &frame) : tier1_frame_plan(request, output, scratch, &frame);
                __asm__ volatile("" : : "r"(&frame) : "memory");
            } break;
            default:
            {
                error = TIER1_FRAME_INVALID;
            } break;
        }
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
    }
    return error;
}

int run_tier1_frame_benchmark(int argc, char** argv)
{
    int only = -1;
    int selected_phase = -1;
    uint32_t after_ms = 0;
    uint32_t selected_count = 0;
    uint64_t seed = 177;
    bool passed = true;
    for (int i = 1; passed && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : (strcmp(argv[i], "vector") == 0 ? 1 : -1);
            passed = only >= 0;
        }
        else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc)
        {
            i += 1;
            selected_phase = -1;
            for (uint32_t phase = 0; phase < TIER1_FRAME_BENCHMARK_PHASES; phase += 1)
            {
                if (strcmp(argv[i], tier1_frame_benchmark_phases[phase]) == 0)
                {
                    selected_phase = (int)phase;
                }
            }
            passed = selected_phase >= 0;
        }
        else if ((strcmp(argv[i], "--after-scalar") == 0 || strcmp(argv[i], "--count") == 0 || strcmp(argv[i], "--seed") == 0) && i + 1 < argc)
        {
            const bool after = strcmp(argv[i], "--after-scalar") == 0;
            const bool count = strcmp(argv[i], "--count") == 0;
            i += 1;
            char* end;
            errno = 0;
            const uint64_t value = strtoull(argv[i], &end, 10);
            passed = errno == 0 && *argv[i] != 0 && *end == 0 && *argv[i] != '-';
            if (after)
            {
                passed &= value <= 1000;
                after_ms = (uint32_t)value;
            }
            else if (count)
            {
                passed &= value != 0 && value <= (1u << 20);
                selected_count = (uint32_t)value;
            }
            else
            {
                passed &= value != 0;
                seed = value;
            }
        }
        else
        {
            passed = false;
        }
    }
    if (!passed)
    {
        fprintf(stderr, "benchmark-tier1-frame: [--only scalar|vector] [--phase prepare|color|layout|total] [--count 1..1048576] [--seed N] [--after-scalar 0..1000]\n");
    }
    Arena prepared_arena = arena_reserve(BUSTER_TIER1_FRAME_BENCHMARK_ARENA_BYTES);
    Arena scratch = arena_reserve(BUSTER_TIER1_FRAME_BENCHMARK_ARENA_BYTES);
    Arena output = arena_reserve(BUSTER_TIER1_FRAME_BENCHMARK_ARENA_BYTES);
    passed &= prepared_arena.capacity != 0 && scratch.capacity != 0 && output.capacity != 0;
    const int counter = passed ? tier1_frame_benchmark_counter() : -1;
    const uint32_t counts[] = {16, 256, 4096, 32768};
    const char* const shapes[] = {"leaf", "calls", "variadic", "dynamic-realigned"};
    for (uint32_t shape = 0; passed && shape < 4; shape += 1)
    {
        const uint32_t count = selected_count != 0 ? selected_count : counts[shape];
        uint32_t* columns = malloc((size_t)count * 5 * sizeof(uint32_t));
        passed = columns != 0;
        if (passed)
        {
            Tier1FrameRequest request = {
                .slots = {
                    .start = columns,
                    .end = columns + count,
                    .size = columns + count * 2,
                    .alignment = columns + count * 3,
                    .indices = shape & 1 ? columns + count * 4 : 0,
                    .count = count,
                    .column_count = count,
                },
                .used_callee_saved = shape != 0 ? (1u << BUSTER_ABI_RBX) | (1u << BUSTER_ABI_R12) : 0,
                .flags = (shape >= 2 ? BUSTER_TIER1_FRAME_VARIADIC : 0) | (shape == 3 ? BUSTER_TIER1_FRAME_DYNAMIC_STACK : 0),
            };
            AbiCall calls[2] = {
                {.stack_bytes = 16, .stack_alignment_log2 = 4},
                {.stack_bytes = 40, .stack_alignment_log2 = shape == 3 ? 6 : 4},
            };
            request.calls = calls;
            request.call_count = shape == 0 ? 0 : 2;
            uint64_t state = seed;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t random = tier1_frame_benchmark_random(&state);
                columns[i] = random % (count * 4);
                columns[count + i] = columns[i] + 1 + (random >> 16) % 64;
                columns[count * 2 + i] = 8 * (1 + random % 4);
                columns[count * 3 + i] = shape == 3 ? 1u << (3 + (random >> 8) % 4) : 8;
                columns[count * 4 + i] = count - i - 1;
            }
            Tier1Frame frames[2];
            const Tier1FrameError scalar_error = tier1_frame_plan_scalar(&request, &output, &scratch, &frames[0]);
            const Tier1FrameError vector_error = tier1_frame_plan(&request, &output, &scratch, &frames[1]);
            passed = scalar_error == TIER1_FRAME_OK && vector_error == TIER1_FRAME_OK;
            if (passed)
            {
                passed = frames[0].slot_count == frames[1].slot_count && frames[0].object_bytes == frames[1].object_bytes &&
                    frames[0].stack_adjust_bytes == frames[1].stack_adjust_bytes;
            }
            Tier1FrameWork prepared;
            if (passed)
            {
                passed = tier1_frame_prepare(&request, &prepared_arena, false, &prepared) == TIER1_FRAME_OK;
            }
            if (passed)
            {
                tier1_frame_color_vector(&prepared);
                printf("tier1-frame shape=%s objects=%u indexed=%u seed=%" PRIu64 " slots=%u object_bytes=%u stack_adjust=%u input_bytes=%" PRIu64
                    " scratch_bytes=%" PRIu64 " output_bytes=%" PRIu64 " plan_bytes=%zu\n", shapes[shape], count, request.slots.indices != 0, seed,
                    frames[1].slot_count, frames[1].object_bytes, frames[1].stack_adjust_bytes, (uint64_t)count * (request.slots.indices != 0 ? 20 : 16),
                    prepared_arena.length, (uint64_t)count * 8, sizeof(Tier1Frame));
            }
            arena_pop_to(&output, 0);
            for (uint32_t phase = 0; passed && phase < TIER1_FRAME_BENCHMARK_PHASES; phase += 1)
            {
                if (selected_phase < 0 || selected_phase == (int)phase)
                {
                    uint32_t repeats[2] = {1, 1};
                    for (uint32_t path = 0; passed && path < 2; path += 1)
                    {
                        uint64_t elapsed = 0;
                        while (passed && elapsed < 2000000 && repeats[path] < (1u << 20))
                        {
                            const uint64_t before = tier1_frame_benchmark_now();
                            passed = tier1_frame_benchmark_repeat(&request, &prepared, &output, &scratch, (Tier1FrameBenchmarkPhase)phase, path == 0, repeats[path]) == TIER1_FRAME_OK;
                            elapsed = tier1_frame_benchmark_now() - before;
                            if (elapsed < 2000000)
                            {
                                repeats[path] *= 2;
                            }
                        }
                    }
                    double nanoseconds[2][BUSTER_TIER1_FRAME_BENCHMARK_PASSES];
                    double cycles[2][BUSTER_TIER1_FRAME_BENCHMARK_PASSES];
                    for (uint32_t pass = 0; passed && pass < BUSTER_TIER1_FRAME_BENCHMARK_PASSES; pass += 1)
                    {
                        for (uint32_t order = 0; passed && order < 2; order += 1)
                        {
                            const uint32_t path = order ^ (pass & 1);
                            if (only < 0 || only == (int)path)
                            {
                                if (path == 1 && after_ms != 0)
                                {
                                    const uint64_t until = tier1_frame_benchmark_now() + (uint64_t)after_ms * 1000000;
                                    while (passed && tier1_frame_benchmark_now() < until)
                                    {
                                        passed = tier1_frame_benchmark_repeat(&request, &prepared, &output, &scratch, (Tier1FrameBenchmarkPhase)phase, true, 1) == TIER1_FRAME_OK;
                                    }
                                }
                                const uint64_t before = tier1_frame_benchmark_now();
                                const uint64_t cycle_before = tier1_frame_benchmark_cycles(counter);
                                passed = passed && tier1_frame_benchmark_repeat(&request, &prepared, &output, &scratch, (Tier1FrameBenchmarkPhase)phase, path == 0, repeats[path]) == TIER1_FRAME_OK;
                                const uint64_t cycle_after = tier1_frame_benchmark_cycles(counter);
                                const uint64_t elapsed = tier1_frame_benchmark_now() - before;
                                const double objects = (double)count * repeats[path];
                                nanoseconds[path][pass] = elapsed / objects;
                                cycles[path][pass] = (cycle_after - cycle_before) / objects;
                            }
                        }
                    }
                    for (uint32_t path = 0; passed && path < 2; path += 1)
                    {
                        if (only < 0 || only == (int)path)
                        {
                            tier1_frame_benchmark_sort(nanoseconds[path]);
                            tier1_frame_benchmark_sort(cycles[path]);
                            const double median = nanoseconds[path][BUSTER_TIER1_FRAME_BENCHMARK_PASSES / 2];
                            printf("  phase=%s path=%s ns/object=%.3f ns/function=%.1f objects/s=%.0f", tier1_frame_benchmark_phases[phase], path == 0 ? "scalar" : "vector", median, median * count, 1e9 / median);
                            if (counter >= 0)
                            {
                                printf(" cycles/object=%.3f", cycles[path][BUSTER_TIER1_FRAME_BENCHMARK_PASSES / 2]);
                            }
                            else
                            {
                                printf(" cycles=unavailable");
                            }
                            printf(" samples=%u after_scalar_ms=%u\n", BUSTER_TIER1_FRAME_BENCHMARK_PASSES, path == 1 ? after_ms : 0);
                        }
                    }
                }
            }
        }
        free(columns);
        arena_pop_to(&prepared_arena, 0);
        arena_pop_to(&output, 0);
        arena_pop_to(&scratch, 0);
    }
    if (counter >= 0)
    {
        close(counter);
    }
    arena_release(&output);
    arena_release(&scratch);
    arena_release(&prepared_arena);
    if (!passed)
    {
        fprintf(stderr, "benchmark-tier1-frame: invalid options, allocation failure or frame mismatch; no incomplete timing accepted\n");
    }
    return passed ? 0 : 1;
}

#else
int run_tier1_frame_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
