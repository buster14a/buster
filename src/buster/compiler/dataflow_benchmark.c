// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The liveness benchmark uses dataflow_program.c's arena-owned synthetic CFGs and input rows. Measurement helpers
// are original to this module; no external fixtures or benchmark implementation are incorporated.
//
// benchmark-dataflow [--seed N] checks scalar/vector equality, then measures complete solves including clearing in
// and out. Twenty-one passes alternate which implementation runs first, after warm-up and sample-size calibration.
// Each retained sample lasts at least 2 ms. Printed ranges are within one process; repeat this command in separately
// locked processes to measure variation between runs rather than holding the shared host lock around a series.
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
#include "buster/compiler/dataflow_program.c"

#define BUSTER_DATAFLOW_BENCHMARK_PASSES 21
#define BUSTER_DATAFLOW_BENCHMARK_SAMPLE_NS 2000000
#define BUSTER_DATAFLOW_BENCHMARK_CALIBRATION_NS (2 * BUSTER_DATAFLOW_BENCHMARK_SAMPLE_NS)
#define BUSTER_DATAFLOW_BENCHMARK_MAX_RESTARTS 4
#define BUSTER_DATAFLOW_BENCHMARK_MAX_REPEATS (1u << 20)
#define BUSTER_DATAFLOW_BENCHMARK_ARENA_BYTES (64ull << 20)

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

typedef struct DataflowBenchmarkStatistics DataflowBenchmarkStatistics;
struct DataflowBenchmarkStatistics
{
    double minimum;
    double median;
    double maximum;
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
        .name = "large-local",
        .blocks = 4096,
        .values = 4097,
        .shape = BUSTER_DATAFLOW_SHAPE_RANDOM,
    },
    {
        .name = "large-dispatch",
        .blocks = 4096,
        .values = 4097,
        .shape = BUSTER_DATAFLOW_SHAPE_DISPATCH,
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

static __attribute__((noinline)) uint64_t dataflow_benchmark_vector(Dataflow* flow)
{
    const uint64_t iterations = dataflow_liveness_vector(flow);
    return iterations;
}

static __attribute__((noinline)) uint64_t dataflow_benchmark_scalar(Dataflow* flow)
{
    const uint64_t iterations = dataflow_liveness_scalar(flow);
    return iterations;
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
        // Both implementations cross an out-of-line wrapper with direct calls, so neither is inlined into the
        // repetition loop. The memory barrier makes every complete set of output rows observable between solves.
        if (vector)
        {
            for (uint64_t repeat = 0; repeat < repeats; repeat += 1)
            {
                sample->iterations += dataflow_benchmark_vector(flow);
                __asm__ volatile("" : : "r"(flow->in), "r"(flow->out) : "memory");
            }
        }
        else
        {
            for (uint64_t repeat = 0; repeat < repeats; repeat += 1)
            {
                sample->iterations += dataflow_benchmark_scalar(flow);
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

static bool dataflow_benchmark_calibrate(Dataflow* flows, uint64_t iterations, uint64_t* repeats)
{
    DataflowBenchmarkCounters no_counters =
    {
        .cycles = -1,
        .instructions = -1,
        .available = false,
    };
    bool valid = true;
    bool ready = false;
    *repeats = 1;
    while (valid && !ready)
    {
        // Repeated warm batches account for clocks and caches settling after the first solve. Both implementations
        // use one repetition count, and both orders must reach twice the required retained-sample duration.
        double shortest = BUSTER_DATAFLOW_BENCHMARK_CALIBRATION_NS;
        for (uint32_t round = 0; valid && round < 2; round += 1)
        {
            for (uint32_t turn = 0; valid && turn < 2; turn += 1)
            {
                const uint32_t variant = (round + turn) % 2;
                DataflowBenchmarkSample sample;
                valid = dataflow_benchmark_run(&flows[variant], variant != 0, *repeats, &no_counters, &sample);
                valid = valid && sample.iterations == iterations * (*repeats);
                shortest = sample.nanoseconds < shortest ? sample.nanoseconds : shortest;
            }
        }
        ready = shortest >= BUSTER_DATAFLOW_BENCHMARK_CALIBRATION_NS;
        if (valid && !ready)
        {
            if (*repeats < BUSTER_DATAFLOW_BENCHMARK_MAX_REPEATS)
            {
                *repeats *= 2;
            }
            else
            {
                valid = false;
            }
        }
    }
    return valid;
}

static DataflowBenchmarkStatistics dataflow_benchmark_statistics(const double* samples)
{
    // Selection by rank is tiny at 21 entries and uses neither a callback nor another module's sorting helper.
    DataflowBenchmarkStatistics result =
    {
        .minimum = samples[0],
        .median = 0,
        .maximum = samples[0],
    };
    for (uint32_t candidate = 0; candidate < BUSTER_DATAFLOW_BENCHMARK_PASSES; candidate += 1)
    {
        result.minimum = samples[candidate] < result.minimum ? samples[candidate] : result.minimum;
        result.maximum = samples[candidate] > result.maximum ? samples[candidate] : result.maximum;
        uint32_t less = 0;
        uint32_t equal = 0;
        for (uint32_t other = 0; other < BUSTER_DATAFLOW_BENCHMARK_PASSES; other += 1)
        {
            less += samples[other] < samples[candidate];
            equal += samples[other] == samples[candidate];
        }
        if (less <= BUSTER_DATAFLOW_BENCHMARK_PASSES / 2 && less + equal > BUSTER_DATAFLOW_BENCHMARK_PASSES / 2)
        {
            result.median = samples[candidate];
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
        iterations = dataflow_benchmark_scalar(&flows[0]);
        const uint64_t vector_iterations = dataflow_benchmark_vector(&flows[1]);
        valid = iterations == vector_iterations && dataflow_benchmark_equal(&flows[0], &flows[1]);
    }

    uint64_t repeats = 1;
    if (valid)
    {
        valid = dataflow_benchmark_calibrate(flows, iterations, &repeats);
        if (!valid)
        {
            fprintf(stderr, "dataflow warm calibration failed for %s\n", workload->name);
        }
    }

    double nanoseconds[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    double cycles[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    double instructions[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    bool complete = false;
    uint32_t restarts = 0;
    while (valid && !complete)
    {
        bool short_sample = false;
        for (uint32_t pass = 0; valid && !short_sample && pass < BUSTER_DATAFLOW_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t turn = 0; valid && !short_sample && turn < 2; turn += 1)
            {
                const uint32_t variant = (pass + turn) % 2;
                DataflowBenchmarkSample sample;
                valid = dataflow_benchmark_run(&flows[variant], variant != 0, repeats, counters, &sample);
                valid = valid && sample.iterations == iterations * repeats;
                short_sample = sample.nanoseconds < BUSTER_DATAFLOW_BENCHMARK_SAMPLE_NS;
                nanoseconds[variant][pass] = sample.nanoseconds / (double)repeats;
                cycles[variant][pass] = sample.cycles / (double)repeats;
                instructions[variant][pass] = sample.instructions / (double)repeats;
            }
        }
        complete = !short_sample;
        if (valid && !complete)
        {
            // Never mix repetition counts in a reported distribution. If either path becomes faster after warm-up,
            // discard the entire attempt and restart all passes with twice as many complete solves on both paths.
            if (restarts < BUSTER_DATAFLOW_BENCHMARK_MAX_RESTARTS && repeats < BUSTER_DATAFLOW_BENCHMARK_MAX_REPEATS)
            {
                repeats *= 2;
                restarts += 1;
            }
            else
            {
                fprintf(stderr, "dataflow samples remain below 2 ms for %s after bounded calibration\n", workload->name);
                valid = false;
            }
        }
    }
    valid = valid && dataflow_benchmark_equal(&flows[0], &flows[1]);
    if (valid)
    {
        printf("%s: blocks=%u values=%u edges=%u row_bytes=%" PRIu64 " iterations=%" PRIu64 " repeats=%" PRIu64 " restarts=%u digest=%016" PRIx64 "\n",
               workload->name, workload->blocks, workload->values, flows[0].edge_count, flows[0].row_words * sizeof(uint64_t),
               iterations, repeats, restarts, dataflow_benchmark_digest(&flows[0]));
        for (uint32_t variant = 0; variant < 2; variant += 1)
        {
            const DataflowBenchmarkStatistics time = dataflow_benchmark_statistics(nanoseconds[variant]);
            const DataflowBenchmarkStatistics cycle = counters->available ? dataflow_benchmark_statistics(cycles[variant]) : (DataflowBenchmarkStatistics){ 0 };
            const DataflowBenchmarkStatistics instruction = counters->available ? dataflow_benchmark_statistics(instructions[variant]) : (DataflowBenchmarkStatistics){ 0 };
            const double blocks_iterations = (double)workload->blocks * (double)iterations;
            printf("  %-6s %.1f [%.1f-%.1f] ns/function %.1f [%.1f-%.1f] cycles/function %.3f [%.3f-%.3f] cycles/block/iteration %.1f instructions/function %.2f IPC min_sample_ms=%.3f\n",
                   variant == 0 ? "scalar" : "avx512", time.median, time.minimum, time.maximum,
                   cycle.median, cycle.minimum, cycle.maximum,
                   cycle.median / blocks_iterations, cycle.minimum / blocks_iterations, cycle.maximum / blocks_iterations,
                   instruction.median, cycle.median != 0 ? instruction.median / cycle.median : 0,
                   time.minimum * (double)repeats / 1e6);
        }
    }
    else
    {
        fprintf(stderr, "dataflow benchmark failed for %s (allocation, clock, calibration, or scalar/vector disagreement)\n", workload->name);
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
        printf("dataflow: generator=%u seed=%" PRIu64 ", 21 interleaved-pass median [min-max]; complete solves including in/out clearing\n",
               BUSTER_DATAFLOW_GENERATOR_VERSION, seed);
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
