// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic CFGs and the liveness benchmark. dataflow_generate is also linked into main.c's tests; it makes only
// arena-owned columns, with zero padding in every bit row. Its seed, sizes and shape are its complete inputs.
// The generator and measurement helpers are original to this module, with no external fixtures or implementation.
//
// benchmark-dataflow [--seed N] checks scalar/vector equality, then measures complete solves including clearing in
// and out. Twenty-one passes alternate which implementation runs first, after warm-up and sample-size calibration.
// Hardware counters count this thread's user instructions; unavailable counters print zero, never a time estimate.
// Run on the 9700X through exclusive.sh with taskset -c 2.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>

#include "buster/compiler/lane.c"
#include "buster/compiler/dataflow.c"

#define BUSTER_DATAFLOW_GENERATOR_EDGES 4
#define BUSTER_DATAFLOW_GENERATOR_REGION 64
#define BUSTER_DATAFLOW_BENCHMARK_PASSES 21
#define BUSTER_DATAFLOW_BENCHMARK_SAMPLE_NS 2000000
#define BUSTER_DATAFLOW_BENCHMARK_MAX_REPEATS (1u << 20)
#define BUSTER_DATAFLOW_BENCHMARK_ARENA_BYTES (64ull << 20)

// A small, deterministic congruential stream for test topology and bit locations, not for cryptographic use.
static uint32_t dataflow_generator_random(uint64_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return (uint32_t)(*state >> 32);
}

static void dataflow_generator_edge(uint32_t block_count, uint32_t target, uint32_t* targets, uint32_t* count)
{
    bool present = false;
    for (uint32_t index = 0; index < *count; index += 1)
    {
        present |= targets[index] == target;
    }
    if (target < block_count && !present)
    {
        assert(*count < BUSTER_DATAFLOW_GENERATOR_EDGES);
        targets[*count] = target;
        *count += 1;
    }
}

static uint32_t dataflow_generator_successors(uint32_t blocks, uint32_t block, uint32_t shape, uint64_t* state, uint32_t* targets)
{
    uint32_t count = 0;
    switch (shape)
    {
        case BUSTER_DATAFLOW_SHAPE_CHAIN:
        {
            dataflow_generator_edge(blocks, block + 1, targets, &count);
        } break;

        case BUSTER_DATAFLOW_SHAPE_DIAMOND:
        {
            const uint32_t position = block % 4;
            const uint32_t base = block - position;
            if (position == 0)
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
                dataflow_generator_edge(blocks, block + 2, targets, &count);
            }
            else
            {
                const uint32_t target = position == 3 ? block + 1 : base + 3;
                dataflow_generator_edge(blocks, target, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_SELF_LOOP:
        {
            dataflow_generator_edge(blocks, block + 1, targets, &count);
            if (block % 3 == 0)
            {
                dataflow_generator_edge(blocks, block, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_NESTED_LOOP:
        {
            dataflow_generator_edge(blocks, block + 1, targets, &count);
            if (block % 8 == 6)
            {
                dataflow_generator_edge(blocks, block - 4, targets, &count);
            }
            if (block % 32 == 30)
            {
                dataflow_generator_edge(blocks, block - 29, targets, &count);
            }
            if (blocks < 8 && block + 1 == blocks)
            {
                dataflow_generator_edge(blocks, 0, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_IRREDUCIBLE:
        {
            // Both nodes of the cycle have an edge entering from outside it: neither dominates the other.
            const uint32_t position = block % 8;
            const uint32_t base = block - position;
            if (position == 0)
            {
                dataflow_generator_edge(blocks, base + 1, targets, &count);
                dataflow_generator_edge(blocks, base + 2, targets, &count);
            }
            else if (position == 1)
            {
                dataflow_generator_edge(blocks, base + 2, targets, &count);
                dataflow_generator_edge(blocks, base + 3, targets, &count);
            }
            else if (position == 2)
            {
                dataflow_generator_edge(blocks, base + 1, targets, &count);
                dataflow_generator_edge(blocks, base + 3, targets, &count);
            }
            else
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_MULTI_EXIT:
        {
            if (block % 7 != 4)
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
                if (block % 7 == 0)
                {
                    dataflow_generator_edge(blocks, block + 2, targets, &count);
                }
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_RANDOM:
        {
            // Local components bound live regions and also exercise postorder's disconnected roots. Making every
            // edge global would quickly make one large SCC with almost every value live almost everywhere.
            const uint32_t base = block - block % BUSTER_DATAFLOW_GENERATOR_REGION;
            const uint32_t left = blocks - base;
            const uint32_t size = left < BUSTER_DATAFLOW_GENERATOR_REGION ? left : BUSTER_DATAFLOW_GENERATOR_REGION;
            const uint32_t choice = dataflow_generator_random(state);
            if (block + 1 < base + size && block % 11 != 7)
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
                dataflow_generator_edge(blocks, base + choice % size, targets, &count);
                if ((choice & 3) == 0)
                {
                    const uint32_t target = base + dataflow_generator_random(state) % size;
                    dataflow_generator_edge(blocks, target, targets, &count);
                }
            }
        } break;
    }
    return count;
}

bool dataflow_generate(Arena* arena, uint32_t blocks, uint32_t values, uint32_t shape, uint64_t seed, Dataflow* result)
{
    const uint64_t saved_length = arena->length;
    bool valid = shape < BUSTER_DATAFLOW_SHAPE_COUNT && blocks <= UINT32_MAX / BUSTER_DATAFLOW_GENERATOR_EDGES;
    uint32_t edges = 0;
    uint64_t state = seed;
    for (uint32_t block = 0; valid && block < blocks; block += 1)
    {
        uint32_t targets[BUSTER_DATAFLOW_GENERATOR_EDGES];
        edges += dataflow_generator_successors(blocks, block, shape, &state, targets);
    }

    Dataflow generated = { 0 };
    if (valid)
    {
        valid = dataflow_create(arena, blocks, values, edges, &generated);
    }
    if (valid)
    {
        state = seed;
        uint32_t edge = 0;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            uint32_t targets[BUSTER_DATAFLOW_GENERATOR_EDGES];
            const uint32_t count = dataflow_generator_successors(blocks, block, shape, &state, targets);
            generated.successor_offsets[block] = edge;
            for (uint32_t index = 0; index < count; index += 1)
            {
                generated.successors[edge] = targets[index];
                edge += 1;
            }
        }
        generated.successor_offsets[blocks] = edge;

        // Every value has one definition and one to four uses nearby. The placement is independent of the graph,
        // so loops, bypassed definitions and disconnected uses all remain real inputs to the fixed-point solver.
        // Setting individual valid bits after zeroing the rows preserves all padding, including unused words.
        const uint64_t bytes = (uint64_t)blocks * generated.row_words * sizeof(uint64_t);
        if (bytes != 0)
        {
            memset(generated.use, 0, bytes);
            memset(generated.def, 0, bytes);
        }
        for (uint32_t value = 0; blocks != 0 && value < values; value += 1)
        {
            const uint32_t anchor = (uint32_t)((uint64_t)value * blocks / values);
            const uint32_t base = anchor - anchor % BUSTER_DATAFLOW_GENERATOR_REGION;
            const uint32_t left = blocks - base;
            const uint32_t size = left < BUSTER_DATAFLOW_GENERATOR_REGION ? left : BUSTER_DATAFLOW_GENERATOR_REGION;
            const uint32_t definition = base + dataflow_generator_random(&state) % size;
            const uint32_t uses = 1 + dataflow_generator_random(&state) % 4;
            const uint64_t bit = 1ull << (value % 64);
            const uint64_t word = value / 64;
            generated.def[(uint64_t)definition * generated.row_words + word] |= bit;
            const uint32_t following = base + size - definition;
            const uint32_t span = following < 17 ? following : 17;
            for (uint32_t use = 0; use < uses; use += 1)
            {
                const uint32_t user = definition + dataflow_generator_random(&state) % span;
                generated.use[(uint64_t)user * generated.row_words + word] |= bit;
            }
        }
        valid = dataflow_postorder(&generated, arena);
    }
    if (valid)
    {
        *result = generated;
    }
    else
    {
        arena_pop_to(arena, saved_length);
    }
    return valid;
}

typedef struct DataflowBenchmarkCounters DataflowBenchmarkCounters;
struct DataflowBenchmarkCounters
{
    int cycles;
    int instructions;
    bool available;
    uint64_t time_enabled;
    uint64_t time_running;
};

typedef struct DataflowBenchmarkSample DataflowBenchmarkSample;
struct DataflowBenchmarkSample
{
    double nanoseconds;
    double cycles;
    double instructions;
    uint64_t iterations;
};

typedef struct DataflowBenchmarkWorkload DataflowBenchmarkWorkload;
struct DataflowBenchmarkWorkload
{
    const char* name;
    uint32_t blocks;
    uint32_t values;
    uint32_t shape;
};

static const DataflowBenchmarkWorkload dataflow_benchmark_workloads[] =
{
    {
        .name = "small-diamond",
        .blocks = 16,
        .values = 127,
        .shape = BUSTER_DATAFLOW_SHAPE_DIAMOND,
    },
    {
        .name = "medium-nested",
        .blocks = 256,
        .values = 1025,
        .shape = BUSTER_DATAFLOW_SHAPE_NESTED_LOOP,
    },
    {
        .name = "large-random",
        .blocks = 4096,
        .values = 4097,
        .shape = BUSTER_DATAFLOW_SHAPE_RANDOM,
    },
};

static DataflowBenchmarkCounters dataflow_benchmark_open_counters(void)
{
    DataflowBenchmarkCounters result =
    {
        .cycles = -1,
        .instructions = -1,
        .available = false,
    };
    struct perf_event_attr attributes = { 0 };
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = PERF_COUNT_HW_CPU_CYCLES;
    attributes.disabled = 1;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    attributes.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    result.cycles = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (result.cycles >= 0)
    {
        attributes.config = PERF_COUNT_HW_INSTRUCTIONS;
        attributes.disabled = 0;
        attributes.read_format = 0;
        result.instructions = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, result.cycles, PERF_FLAG_FD_CLOEXEC);
        result.available = result.instructions >= 0;
    }
    return result;
}

static void dataflow_benchmark_close_counters(DataflowBenchmarkCounters* counters)
{
    if (counters->instructions >= 0)
    {
        close(counters->instructions);
    }
    if (counters->cycles >= 0)
    {
        close(counters->cycles);
    }
    *counters = (DataflowBenchmarkCounters)
    {
        .cycles = -1,
        .instructions = -1,
        .available = false,
    };
}

static void dataflow_benchmark_start_counters(DataflowBenchmarkCounters* counters)
{
    if (counters->available)
    {
        const bool reset = ioctl(counters->cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0;
        // RESET clears event values, but not the enabled/running clocks. Save those clocks while the group is
        // disabled, so a sample's scaling never uses time from earlier scalar or vector measurements.
        uint64_t readout[5] = { 0 };
        const bool read_ok = reset && read(counters->cycles, readout, sizeof(readout)) == (ssize_t)sizeof(readout) && readout[0] == 2;
        if (read_ok)
        {
            counters->time_enabled = readout[1];
            counters->time_running = readout[2];
        }
        const bool enabled = read_ok && ioctl(counters->cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == 0;
        if (!enabled)
        {
            dataflow_benchmark_close_counters(counters);
        }
    }
}

static void dataflow_benchmark_stop_counters(DataflowBenchmarkCounters* counters, DataflowBenchmarkSample* sample)
{
    sample->cycles = 0;
    sample->instructions = 0;
    if (counters->available)
    {
        // The group read keeps cycles and instructions on the same scheduling interval. Time scaling is needed if
        // the kernel multiplexes events; no conversion from TSC ticks or wall-clock nanoseconds is substituted.
        uint64_t readout[5] = { 0 };
        const bool disabled = ioctl(counters->cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
        const bool read_ok = disabled && read(counters->cycles, readout, sizeof(readout)) == (ssize_t)sizeof(readout);
        if (read_ok && readout[0] == 2 && readout[1] >= counters->time_enabled && readout[2] > counters->time_running)
        {
            const double scale = (double)(readout[1] - counters->time_enabled) / (double)(readout[2] - counters->time_running);
            sample->cycles = (double)readout[3] * scale;
            sample->instructions = (double)readout[4] * scale;
        }
        else
        {
            dataflow_benchmark_close_counters(counters);
        }
    }
}

static bool dataflow_benchmark_run(Dataflow* flow, bool vector, uint64_t repeats, DataflowBenchmarkCounters* counters, DataflowBenchmarkSample* sample)
{
    struct timespec begin;
    struct timespec end;
    *sample = (DataflowBenchmarkSample){ 0 };
    dataflow_benchmark_start_counters(counters);
    bool valid = clock_gettime(CLOCK_MONOTONIC_RAW, &begin) == 0;
    if (valid)
    {
        // Direct calls keep the timing free of a callback dispatch. The memory barrier makes every complete set of
        // output rows observable, even when a build can inline the solver and sees repeated identical inputs.
        if (vector)
        {
            for (uint64_t repeat = 0; repeat < repeats; repeat += 1)
            {
                sample->iterations += dataflow_liveness_vector(flow);
                __asm__ volatile("" : : "r"(flow->in), "r"(flow->out) : "memory");
            }
        }
        else
        {
            for (uint64_t repeat = 0; repeat < repeats; repeat += 1)
            {
                sample->iterations += dataflow_liveness_scalar(flow);
                __asm__ volatile("" : : "r"(flow->in), "r"(flow->out) : "memory");
            }
        }
        valid = clock_gettime(CLOCK_MONOTONIC_RAW, &end) == 0;
    }
    dataflow_benchmark_stop_counters(counters, sample);
    if (valid)
    {
        sample->nanoseconds = (double)(end.tv_sec - begin.tv_sec) * 1e9 + (double)(end.tv_nsec - begin.tv_nsec);
    }
    return valid;
}

static double dataflow_benchmark_median(const double* samples)
{
    // Selection by rank is tiny at 21 entries and uses neither a callback nor another module's sorting helper.
    double result = 0;
    for (uint32_t candidate = 0; candidate < BUSTER_DATAFLOW_BENCHMARK_PASSES; candidate += 1)
    {
        uint32_t less = 0;
        uint32_t equal = 0;
        for (uint32_t other = 0; other < BUSTER_DATAFLOW_BENCHMARK_PASSES; other += 1)
        {
            less += samples[other] < samples[candidate];
            equal += samples[other] == samples[candidate];
        }
        if (less <= BUSTER_DATAFLOW_BENCHMARK_PASSES / 2 && less + equal > BUSTER_DATAFLOW_BENCHMARK_PASSES / 2)
        {
            result = samples[candidate];
        }
    }
    return result;
}

static bool dataflow_benchmark_equal(const Dataflow* scalar, const Dataflow* vector)
{
    const uint64_t bytes = (uint64_t)scalar->block_count * scalar->row_words * sizeof(uint64_t);
    const bool result = bytes == 0 || (memcmp(scalar->in, vector->in, bytes) == 0 && memcmp(scalar->out, vector->out, bytes) == 0);
    return result;
}

static uint64_t dataflow_benchmark_digest(const Dataflow* flow)
{
    uint64_t result = 0;
    const uint64_t words = (uint64_t)flow->block_count * flow->row_words;
    for (uint64_t word = 0; word < words; word += 1)
    {
        result = (result << 7) | (result >> 57);
        result ^= flow->in[word] + flow->out[word] + word;
    }
    return result;
}

static bool dataflow_benchmark_workload(const DataflowBenchmarkWorkload* workload, uint64_t seed, DataflowBenchmarkCounters* counters)
{
    Arena arena = arena_reserve(BUSTER_DATAFLOW_BENCHMARK_ARENA_BYTES);
    Dataflow flows[2] = { 0 };
    bool valid = dataflow_generate(&arena, workload->blocks, workload->values, workload->shape, seed, &flows[0]);
    valid = valid && dataflow_generate(&arena, workload->blocks, workload->values, workload->shape, seed, &flows[1]);
    uint64_t iterations = 0;
    if (valid)
    {
        iterations = dataflow_liveness_scalar(&flows[0]);
        const uint64_t vector_iterations = dataflow_liveness_vector(&flows[1]);
        valid = iterations == vector_iterations && dataflow_benchmark_equal(&flows[0], &flows[1]);
    }

    uint64_t repeats = 1;
    if (valid)
    {
        // A complete warm solve on each path has already touched the pages. Calibrate against the faster path,
        // using the same repetition count for both; measurements include each solve's required row clearing.
        DataflowBenchmarkCounters no_counters =
        {
            .cycles = -1,
            .instructions = -1,
            .available = false,
        };
        DataflowBenchmarkSample calibration[2];
        valid = dataflow_benchmark_run(&flows[0], false, 1, &no_counters, &calibration[0]);
        valid = valid && dataflow_benchmark_run(&flows[1], true, 1, &no_counters, &calibration[1]);
        if (valid)
        {
            double fastest = calibration[0].nanoseconds < calibration[1].nanoseconds ? calibration[0].nanoseconds : calibration[1].nanoseconds;
            fastest = fastest > 0 ? fastest : 1;
            const double requested = BUSTER_DATAFLOW_BENCHMARK_SAMPLE_NS / fastest + 1;
            repeats = requested > BUSTER_DATAFLOW_BENCHMARK_MAX_REPEATS ? BUSTER_DATAFLOW_BENCHMARK_MAX_REPEATS : (uint64_t)requested;
        }
    }

    double nanoseconds[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    double cycles[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    double instructions[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    for (uint32_t pass = 0; valid && pass < BUSTER_DATAFLOW_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t turn = 0; valid && turn < 2; turn += 1)
        {
            const uint32_t variant = (pass + turn) % 2;
            DataflowBenchmarkSample sample;
            valid = dataflow_benchmark_run(&flows[variant], variant != 0, repeats, counters, &sample);
            valid = valid && sample.iterations == iterations * repeats;
            nanoseconds[variant][pass] = sample.nanoseconds / (double)repeats;
            cycles[variant][pass] = sample.cycles / (double)repeats;
            instructions[variant][pass] = sample.instructions / (double)repeats;
        }
    }
    valid = valid && dataflow_benchmark_equal(&flows[0], &flows[1]);
    if (valid)
    {
        printf("%s: blocks=%u values=%u edges=%u row_bytes=%" PRIu64 " iterations=%" PRIu64 " repeats=%" PRIu64 " digest=%016" PRIx64 "\n",
               workload->name, workload->blocks, workload->values, flows[0].edge_count, flows[0].row_words * sizeof(uint64_t),
               iterations, repeats, dataflow_benchmark_digest(&flows[0]));
        for (uint32_t variant = 0; variant < 2; variant += 1)
        {
            const double median_ns = dataflow_benchmark_median(nanoseconds[variant]);
            const double median_cycles = counters->available ? dataflow_benchmark_median(cycles[variant]) : 0;
            const double median_instructions = counters->available ? dataflow_benchmark_median(instructions[variant]) : 0;
            const double blocks_iterations = (double)workload->blocks * (double)iterations;
            printf("  %-6s %12.1f ns/function %12.1f cycles/function %8.3f cycles/block/iteration %12.1f instructions/function %5.2f IPC\n",
                   variant == 0 ? "scalar" : "avx512", median_ns, median_cycles, median_cycles / blocks_iterations,
                   median_instructions, median_cycles != 0 ? median_instructions / median_cycles : 0);
        }
    }
    else
    {
        fprintf(stderr, "dataflow benchmark failed for %s (allocation, clock, or scalar/vector disagreement)\n", workload->name);
    }
    arena_release(&arena);
    return valid;
}

static bool dataflow_benchmark_seed(const char* text, uint64_t* seed)
{
    char* end = 0;
    errno = 0;
    const unsigned long long value = strtoull(text, &end, 0);
    const bool valid = text[0] >= '0' && text[0] <= '9' && end != text && *end == '\0' && errno == 0;
    if (valid)
    {
        *seed = (uint64_t)value;
    }
    return valid;
}

int run_dataflow_benchmark(int argc, char** argv)
{
    bool valid = true;
    uint64_t seed = 1;
    for (int argument = 1; argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc)
        {
            valid = valid && dataflow_benchmark_seed(argv[argument + 1], &seed);
            argument += 1;
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--seed N]\n", argv[0]);
    }
    else
    {
        DataflowBenchmarkCounters counters = dataflow_benchmark_open_counters();
        printf("dataflow: seed=%" PRIu64 ", 21 interleaved-pass medians; complete solves including in/out clearing\n", seed);
        if (!counters.available)
        {
            printf("hardware counters unavailable; cycles, instructions and IPC are zero\n");
        }
        const uint32_t count = sizeof(dataflow_benchmark_workloads) / sizeof(dataflow_benchmark_workloads[0]);
        for (uint32_t workload = 0; valid && workload < count; workload += 1)
        {
            valid = dataflow_benchmark_workload(&dataflow_benchmark_workloads[workload], seed, &counters);
        }
        dataflow_benchmark_close_counters(&counters);
    }
    return valid ? 0 : 1;
}
