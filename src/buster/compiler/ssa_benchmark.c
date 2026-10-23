// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic local-promotion measurements, included after ssa.c and ssa_program.c. This is not a C/SQLite corpus
// measurement. Both constructors pass the independent verifier before timing; their promotion and reachability
// columns must agree. Phi identities/counts may differ, so byte equality is not a correctness criterion.
//
// Each sample includes the complete constructor, its arena allocations, and returning output to its starting mark.
// Arena reservation, input generation and verification are outside timing. The 21 samples alternate path order,
// with at least 2 ms of work per retained sample after calibration. --only isolates a path; --after-scalar MS runs
// the scalar constructor immediately before each measured vector sample. Cycles are perf's actual CPU cycles,
// never TSC ticks or wall-time estimates; denied, failed or multiplexed counters are reported as unavailable.
// Measurement helpers are original to this module, with no imported benchmark implementation.

#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_SSA_BENCHMARK_PASSES 21
#define BUSTER_SSA_BENCHMARK_SAMPLE_NS UINT64_C(2000000)
#define BUSTER_SSA_BENCHMARK_MAX_REPEATS (1u << 20)
#define BUSTER_SSA_BENCHMARK_ARENA_BYTES (UINT64_C(256) << 20)
#define BUSTER_SSA_BENCHMARK_AFTER_SCALAR_MAX_MS 1000

typedef struct SsaBenchmarkCounter SsaBenchmarkCounter;
struct SsaBenchmarkCounter
{
    int descriptor;
    bool available;
};

typedef struct SsaBenchmarkSample SsaBenchmarkSample;
struct SsaBenchmarkSample
{
    double nanoseconds;
    double cycles;
    uint64_t elapsed;
    bool cycles_available;
};

typedef struct SsaBenchmarkStatistics SsaBenchmarkStatistics;
struct SsaBenchmarkStatistics
{
    double minimum;
    double median;
    double maximum;
};

typedef struct SsaBenchmarkShape SsaBenchmarkShape;
struct SsaBenchmarkShape
{
    uint32_t phis;
    uint32_t operands;
    uint64_t output_bytes;
};

typedef struct SsaBenchmarkWorkload SsaBenchmarkWorkload;
struct SsaBenchmarkWorkload
{
    const char* name;
    uint32_t blocks;
    uint32_t locals;
    uint32_t shape;
};

static const SsaBenchmarkWorkload ssa_benchmark_workloads[] =
{
    {
        .name = "small-diamond",
        .blocks = 32,
        .locals = 32,
        .shape = BUSTER_SSA_SHAPE_DIAMOND,
    },
    {
        .name = "medium-loop",
        .blocks = 256,
        .locals = 128,
        .shape = BUSTER_SSA_SHAPE_LOOP,
    },
    {
        .name = "irreducible",
        .blocks = 513,
        .locals = 65,
        .shape = BUSTER_SSA_SHAPE_IRREDUCIBLE,
    },
    {
        .name = "large-random",
        .blocks = 1025,
        .locals = 257,
        .shape = BUSTER_SSA_SHAPE_RANDOM,
    },
};

static bool ssa_benchmark_time(uint64_t* value)
{
    struct timespec clock = { 0 };
    const bool success = clock_gettime(CLOCK_MONOTONIC_RAW, &clock) == 0;
    if (success)
    {
        *value = (uint64_t)clock.tv_sec * UINT64_C(1000000000) + (uint64_t)clock.tv_nsec;
    }
    return success;
}

static SsaBenchmarkCounter ssa_benchmark_open_counter(void)
{
    struct perf_event_attr attributes =
    {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    const int descriptor = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    const SsaBenchmarkCounter result =
    {
        .descriptor = descriptor,
        .available = descriptor >= 0,
    };
    return result;
}

static SsaBenchmarkStatistics ssa_benchmark_statistics(const SsaBenchmarkSample* samples, bool cycles)
{
    double sorted[BUSTER_SSA_BENCHMARK_PASSES];
    for (uint32_t index = 0; index < BUSTER_SSA_BENCHMARK_PASSES; index += 1)
    {
        const double value = cycles ? samples[index].cycles : samples[index].nanoseconds;
        uint32_t place = index;
        while (place != 0 && sorted[place - 1] > value)
        {
            sorted[place] = sorted[place - 1];
            place -= 1;
        }
        sorted[place] = value;
    }
    const SsaBenchmarkStatistics result =
    {
        .minimum = sorted[0],
        .median = sorted[BUSTER_SSA_BENCHMARK_PASSES / 2],
        .maximum = sorted[BUSTER_SSA_BENCHMARK_PASSES - 1],
    };
    return result;
}

// Separate direct-call loops keep dispatch outside each timed construction. The compiler barrier keeps every
// result observable, including in a future build that inlines more of the constructors.
static __attribute__((noinline, aligned(64))) SsaError ssa_benchmark_scalar(const SsaInput* input, Arena* output, Arena* scratch, uint32_t repeats)
{
    const uint64_t mark = output->length;
    SsaError error = SSA_OK;
    for (uint32_t repeat = 0; error == SSA_OK && repeat < repeats; repeat += 1)
    {
        SsaResult result = { 0 };
        error = ssa_build_scalar(input, output, scratch, &result);
        __asm__ volatile("" : : "r"(result.replacements), "r"(result.phi_count) : "memory");
        arena_pop_to(output, mark);
    }
    return error;
}

static __attribute__((noinline, aligned(64))) SsaError ssa_benchmark_vector(const SsaInput* input, Arena* output, Arena* scratch, uint32_t repeats)
{
    const uint64_t mark = output->length;
    SsaError error = SSA_OK;
    for (uint32_t repeat = 0; error == SSA_OK && repeat < repeats; repeat += 1)
    {
        SsaResult result = { 0 };
        error = ssa_build(input, output, scratch, &result);
        __asm__ volatile("" : : "r"(result.replacements), "r"(result.phi_count) : "memory");
        arena_pop_to(output, mark);
    }
    return error;
}

static bool ssa_benchmark_condition(const SsaInput* input, Arena* output, Arena* scratch, uint32_t milliseconds)
{
    uint64_t begin = 0;
    uint64_t now = 0;
    bool success = true;
    if (milliseconds != 0)
    {
        success = ssa_benchmark_time(&begin);
        now = begin;
        while (success && now - begin < (uint64_t)milliseconds * UINT64_C(1000000))
        {
            success = ssa_benchmark_scalar(input, output, scratch, 1) == SSA_OK;
            success = success && ssa_benchmark_time(&now) && now >= begin;
        }
    }
    return success;
}

static bool ssa_benchmark_measure(const SsaInput* input, Arena* output, Arena* scratch, uint32_t path, uint32_t repeats, SsaBenchmarkCounter* counter, SsaBenchmarkSample* sample)
{
    // Enabled/running times are cumulative even across PERF_EVENT_IOC_RESET, so take differences of all fields.
    uint64_t before[3] = { 0 };
    uint64_t after[3] = { 0 };
    bool counted = counter->available;
    if (counted)
    {
        counted = read(counter->descriptor, before, sizeof(before)) == (ssize_t)sizeof(before);
        counted = counted && ioctl(counter->descriptor, PERF_EVENT_IOC_ENABLE, 0) == 0;
    }
    uint64_t begin = 0;
    uint64_t end = 0;
    bool success = ssa_benchmark_time(&begin);
    if (success)
    {
        const SsaError error = path == 0 ? ssa_benchmark_scalar(input, output, scratch, repeats) : ssa_benchmark_vector(input, output, scratch, repeats);
        success = error == SSA_OK;
    }
    success = ssa_benchmark_time(&end) && end >= begin && success;
    if (counter->available)
    {
        counted = ioctl(counter->descriptor, PERF_EVENT_IOC_DISABLE, 0) == 0 && counted;
        counted = read(counter->descriptor, after, sizeof(after)) == (ssize_t)sizeof(after) && counted;
        // Do not synthesize scaled cycles from a multiplexed interval, or let counter underflow look plausible.
        counted = counted && after[0] >= before[0] && after[1] >= before[1] && after[2] > before[2];
        counted = counted && after[1] - before[1] == after[2] - before[2];
        counter->available = counted;
    }
    if (success)
    {
        *sample = (SsaBenchmarkSample)
        {
            .nanoseconds = (double)(end - begin) / repeats,
            .cycles = counted ? (double)(after[0] - before[0]) / repeats : 0,
            .elapsed = end - begin,
            .cycles_available = counted,
        };
    }
    return success;
}

static bool ssa_benchmark_check(const SsaInput* input, Arena* scalar_output, Arena* vector_output, Arena* scratch, SsaBenchmarkShape* shapes)
{
    const uint64_t scalar_mark = scalar_output->length;
    const uint64_t vector_mark = vector_output->length;
    SsaResult scalar = { 0 };
    SsaResult vector = { 0 };
    SsaError error = ssa_build_scalar(input, scalar_output, scratch, &scalar);
    bool success = error == SSA_OK;
    if (success)
    {
        error = ssa_build(input, vector_output, scratch, &vector);
        success = error == SSA_OK;
    }
    if (!success)
    {
        fprintf(stderr, "ssa benchmark: construction failed (error %u)\n", (unsigned)error);
    }
    if (success)
    {
        const SsaCheck scalar_check = ssa_verify(input, &scalar, scratch);
        const SsaCheck vector_check = ssa_verify(input, &vector, scratch);
        success = scalar_check.error == SSA_OK && vector_check.error == SSA_OK;
        if (!success)
        {
            const SsaCheck check = scalar_check.error != SSA_OK ? scalar_check : vector_check;
            const char* path = scalar_check.error != SSA_OK ? "scalar" : "vector";
            fprintf(stderr, "ssa benchmark: %s verification failed at %u: %s\n", path, check.instruction, check.rule != 0 ? check.rule : "unspecified rule");
        }
    }
    if (success)
    {
        success = (input->local_count == 0 || memcmp(scalar.promoted, vector.promoted, input->local_count) == 0) &&
                  (input->block_count == 0 || memcmp(scalar.reachable, vector.reachable, input->block_count) == 0);
        if (!success)
        {
            fprintf(stderr, "ssa benchmark: promotion or reachability differs between paths\n");
        }
    }
    if (success)
    {
        shapes[0] = (SsaBenchmarkShape)
        {
            .phis = scalar.phi_count,
            .operands = scalar.operand_count,
            .output_bytes = scalar_output->length - scalar_mark,
        };
        shapes[1] = (SsaBenchmarkShape)
        {
            .phis = vector.phi_count,
            .operands = vector.operand_count,
            .output_bytes = vector_output->length - vector_mark,
        };
    }
    arena_pop_to(scalar_output, scalar_mark);
    arena_pop_to(vector_output, vector_mark);
    return success;
}

static bool ssa_benchmark_workload(const SsaBenchmarkWorkload* workload, uint64_t seed, uint32_t only, uint32_t after_scalar, SsaBenchmarkCounter* counter)
{
    Arena input_arena = arena_reserve(BUSTER_SSA_BENCHMARK_ARENA_BYTES);
    Arena scalar_output = arena_reserve(BUSTER_SSA_BENCHMARK_ARENA_BYTES);
    Arena vector_output = arena_reserve(BUSTER_SSA_BENCHMARK_ARENA_BYTES);
    Arena scratch = arena_reserve(BUSTER_SSA_BENCHMARK_ARENA_BYTES);
    SsaInput input = { 0 };
    bool success = input_arena.base != 0 && scalar_output.base != 0 && vector_output.base != 0 && scratch.base != 0;
    success = success && ssa_generate(&input_arena, workload->blocks, workload->locals, workload->shape, seed, &input);
    SsaBenchmarkShape shapes[2] = { 0 };
    success = success && ssa_benchmark_check(&input, &scalar_output, &vector_output, &scratch, shapes);
    SsaBenchmarkSample samples[2][BUSTER_SSA_BENCHMARK_PASSES] = { 0 };
    uint32_t repeats[2] = { 1, 1 };
    for (uint32_t path = 0; success && path < 2; path += 1)
    {
        if (only == 2 || only == path)
        {
            Arena* output = path == 0 ? &scalar_output : &vector_output;
            SsaBenchmarkSample sample = { 0 };
            // Calibrate to twice the retained minimum; first-touch allocation and warm-up were already performed
            // by verification. A sample below the minimum later grows its own repeat count and is retried.
            while (success && sample.elapsed < 2 * BUSTER_SSA_BENCHMARK_SAMPLE_NS)
            {
                success = ssa_benchmark_measure(&input, output, &scratch, path, repeats[path], counter, &sample);
                if (success && sample.elapsed < 2 * BUSTER_SSA_BENCHMARK_SAMPLE_NS)
                {
                    success = repeats[path] < BUSTER_SSA_BENCHMARK_MAX_REPEATS;
                    if (success)
                    {
                        repeats[path] *= 2;
                    }
                }
            }
        }
    }
    for (uint32_t pass = 0; success && pass < BUSTER_SSA_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t step = 0; success && step < 2; step += 1)
        {
            const uint32_t path = (pass + step) & 1;
            if (only == 2 || only == path)
            {
                Arena* output = path == 0 ? &scalar_output : &vector_output;
                SsaBenchmarkSample sample = { 0 };
                while (success && sample.elapsed < BUSTER_SSA_BENCHMARK_SAMPLE_NS)
                {
                    if (path == 1)
                    {
                        success = ssa_benchmark_condition(&input, &scalar_output, &scratch, after_scalar);
                    }
                    success = success && ssa_benchmark_measure(&input, output, &scratch, path, repeats[path], counter, &sample);
                    if (success && sample.elapsed < BUSTER_SSA_BENCHMARK_SAMPLE_NS)
                    {
                        success = repeats[path] < BUSTER_SSA_BENCHMARK_MAX_REPEATS;
                        if (success)
                        {
                            repeats[path] *= 2;
                        }
                    }
                }
                samples[path][pass] = sample;
            }
        }
    }
    if (success)
    {
        printf("%s: blocks=%u locals=%u instructions=%u edges=%u seed=%" PRIu64 "\n",
            workload->name, input.block_count, input.local_count, input.instruction_count, input.edge_count, seed);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            if (only == 2 || only == path)
            {
                const SsaBenchmarkStatistics nanoseconds = ssa_benchmark_statistics(samples[path], false);
                bool cycles_available = true;
                for (uint32_t pass = 0; pass < BUSTER_SSA_BENCHMARK_PASSES; pass += 1)
                {
                    cycles_available = cycles_available && samples[path][pass].cycles_available;
                }
                const double instructions = input.instruction_count;
                printf("  %-6s ns/instruction=%.3f [%.3f, %.3f] cycles/instruction=",
                    path == 0 ? "scalar" : "vector", nanoseconds.median / instructions,
                    nanoseconds.minimum / instructions, nanoseconds.maximum / instructions);
                if (cycles_available)
                {
                    const SsaBenchmarkStatistics cycles = ssa_benchmark_statistics(samples[path], true);
                    printf("%.3f", cycles.median / instructions);
                }
                else
                {
                    printf("unavailable");
                }
                printf(" phis=%u phi_operands=%u output_bytes=%" PRIu64 " repeats=%u\n",
                    shapes[path].phis, shapes[path].operands, shapes[path].output_bytes, repeats[path]);
            }
        }
    }
    else
    {
        fprintf(stderr, "ssa benchmark: %s failed; no timings reported for this workload\n", workload->name);
    }
    arena_release(&scratch);
    arena_release(&vector_output);
    arena_release(&scalar_output);
    arena_release(&input_arena);
    return success;
}

static bool ssa_benchmark_number(const char* text, uint64_t limit, uint64_t* result)
{
    char* end = 0;
    errno = 0;
    const unsigned long long value = strtoull(text, &end, 10);
    const bool success = text[0] >= '0' && text[0] <= '9' && errno == 0 && end != text && *end == 0 && value <= limit;
    if (success)
    {
        *result = (uint64_t)value;
    }
    return success;
}

static int run_ssa_benchmark(int argc, char** argv)
{
    uint64_t seed = 1;
    uint32_t only = 2;
    uint32_t after_scalar = 0;
    bool help = false;
    bool success = true;
    for (int index = 0; success && index < argc; index += 1)
    {
        if (strcmp(argv[index], "--help") == 0)
        {
            help = true;
        }
        else if (strcmp(argv[index], "--only") == 0 && index + 1 < argc)
        {
            index += 1;
            if (strcmp(argv[index], "scalar") == 0)
            {
                only = 0;
            }
            else if (strcmp(argv[index], "vector") == 0)
            {
                only = 1;
            }
            else
            {
                success = false;
            }
        }
        else if ((strcmp(argv[index], "--seed") == 0 || strcmp(argv[index], "--after-scalar") == 0) && index + 1 < argc)
        {
            const bool is_seed = strcmp(argv[index], "--seed") == 0;
            uint64_t value = 0;
            index += 1;
            success = ssa_benchmark_number(argv[index], is_seed ? UINT64_MAX : BUSTER_SSA_BENCHMARK_AFTER_SCALAR_MAX_MS, &value);
            if (is_seed)
            {
                seed = value;
            }
            else
            {
                after_scalar = (uint32_t)value;
            }
        }
        else
        {
            success = false;
        }
    }
    if (!success || help)
    {
        fprintf(success ? stdout : stderr, "usage: benchmark-ssa [--seed N] [--only scalar|vector] [--after-scalar MS]\n");
        fprintf(success ? stdout : stderr, "  Synthetic CFGs; MS is 0..%u. Run acceptance measurements on the locked 9700X, pinned to one CPU.\n", BUSTER_SSA_BENCHMARK_AFTER_SCALAR_MAX_MS);
    }
    SsaBenchmarkCounter counter =
    {
        .descriptor = -1,
        .available = false,
    };
    if (success && !help)
    {
        counter = ssa_benchmark_open_counter();
        printf("SSA synthetic local-promotion benchmark: %u samples, complete construction; input generation and verification excluded\n", BUSTER_SSA_BENCHMARK_PASSES);
        printf("Mode: %s; scalar conditioning before vector samples: %u ms; cycles: %s\n",
            only == 2 ? "interleaved" : only == 0 ? "scalar only" : "vector only", after_scalar,
            counter.available ? "perf CPU cycles (unscaled)" : "unavailable");
        for (uint32_t index = 0; success && index < sizeof(ssa_benchmark_workloads) / sizeof(ssa_benchmark_workloads[0]); index += 1)
        {
            success = ssa_benchmark_workload(&ssa_benchmark_workloads[index], seed, only, after_scalar, &counter);
        }
    }
    if (counter.descriptor >= 0)
    {
        close(counter.descriptor);
    }
    return success ? 0 : 1;
}
