// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The liveness benchmark uses dataflow_program.c's arena-owned synthetic CFGs and input rows. Measurement helpers
// are original to this module; no external fixtures or benchmark implementation are incorporated.
//
// benchmark-dataflow [--seed N] [--after-scalar MS] [--only scalar|avx512] checks scalar/vector equality, then
// measures complete solves including the rows each solver clears. Twenty-one passes alternate which implementation runs
// first, after warm-up and sample-size calibration; --only times one alone, back to back. Each retained sample lasts at
// least 2 ms. Printed ranges are within one process; repeat this command in separately locked processes to measure
// variation between runs rather than holding the shared host lock around a series. Hardware counters count this
// thread's user cycles, instructions and the throttle, read as the dispatch slots Zen 5's power cap withholds
// (BUSTER_DATAFLOW_BENCHMARK_THROTTLE_EVENT); unavailable counters print zero, never a time estimate. --after-scalar MS
// runs the scalar solver for MS ms before every AVX-512 sample (dataflow_benchmark_after_scalar), the state a
// compiler's vector phases run in after lighter ones. Run on the 9700X through exclusive.sh with taskset -c 2.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
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
#define BUSTER_DATAFLOW_BENCHMARK_AFTER_SCALAR_MAX_MS 10000
// PMCx1A0 (dispatch slots not used) with its undocumented umask 0x08, as a raw perf config: the event's low byte in
// bits 0-7, the umask in bits 8-15 and the event's high nibble in bits 32-35. #199 reads it as the slots that Zen 5's
// per-core power cap withholds from a heavy vector loop at a boosted clock, an inference from its controls: it is the
// one dispatch counter that rises in that state, by about the cycles lost.
#define BUSTER_DATAFLOW_BENCHMARK_THROTTLE_EVENT 0x1000008a0ull
// The group's read: its member count, enabled and running times, then cycles, instructions and the throttle's slots.
#define BUSTER_DATAFLOW_BENCHMARK_READOUT_WORDS 6

typedef struct DataflowBenchmarkCounters DataflowBenchmarkCounters;
struct DataflowBenchmarkCounters
{
    int cycles;
    int instructions;
    // In the group when it can be counted, and -1 (the slots read as zero) when it can't.
    int throttle;
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
    double throttle;
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
        .throttle = -1,
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
    if (result.available)
    {
        attributes.type = PERF_TYPE_RAW;
        attributes.config = BUSTER_DATAFLOW_BENCHMARK_THROTTLE_EVENT;
        result.throttle = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, result.cycles, PERF_FLAG_FD_CLOEXEC);
    }
    return result;
}

static void dataflow_benchmark_close_counters(DataflowBenchmarkCounters* counters)
{
    if (counters->throttle >= 0)
    {
        close(counters->throttle);
    }
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
        .throttle = -1,
        .available = false,
    };
}

// Reads the group into `readout`, BUSTER_DATAFLOW_BENCHMARK_READOUT_WORDS zeroed words, whose last stays zero when the
// throttle isn't in the group. Returns false if the read fails or doesn't have every member.
static bool dataflow_benchmark_read_counters(const DataflowBenchmarkCounters* counters, uint64_t* readout)
{
    const uint64_t members = counters->throttle >= 0 ? 3 : 2;
    const ssize_t bytes = read(counters->cycles, readout, BUSTER_DATAFLOW_BENCHMARK_READOUT_WORDS * sizeof(uint64_t));
    const bool result = bytes == (ssize_t)((3 + members) * sizeof(uint64_t)) && readout[0] == members;
    return result;
}

static void dataflow_benchmark_start_counters(DataflowBenchmarkCounters* counters)
{
    if (counters->available)
    {
        const bool reset = ioctl(counters->cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0;
        // RESET clears event values, but not the enabled/running clocks. Save those clocks while the group is
        // disabled, so a sample's scaling never uses time from earlier scalar or vector measurements.
        uint64_t readout[BUSTER_DATAFLOW_BENCHMARK_READOUT_WORDS] = { 0 };
        const bool read_ok = reset && dataflow_benchmark_read_counters(counters, readout);
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
    sample->throttle = 0;
    if (counters->available)
    {
        // The group read keeps cycles, instructions and the throttle on the same scheduling interval. Time scaling is
        // needed if the kernel multiplexes events; no conversion from TSC ticks or wall-clock nanoseconds is
        // substituted.
        uint64_t readout[BUSTER_DATAFLOW_BENCHMARK_READOUT_WORDS] = { 0 };
        const bool disabled = ioctl(counters->cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
        const bool read_ok = disabled && dataflow_benchmark_read_counters(counters, readout);
        if (read_ok && readout[1] >= counters->time_enabled && readout[2] > counters->time_running)
        {
            const double scale = (double)(readout[1] - counters->time_enabled) / (double)(readout[2] - counters->time_running);
            sample->cycles = (double)readout[3] * scale;
            sample->instructions = (double)readout[4] * scale;
            sample->throttle = (double)readout[5] * scale;
        }
        else
        {
            dataflow_benchmark_close_counters(counters);
        }
    }
}

// Each wrapper starts a 64-byte line, so that every build lays its solver's loops out in lines the same way, however
// much driver code comes before them (docs/dataflow.md, "Where the scalar twin's code falls").
static __attribute__((noinline, aligned(64))) uint64_t dataflow_benchmark_vector(Dataflow* flow)
{
    const uint64_t iterations = dataflow_liveness_vector(flow);
    return iterations;
}

static __attribute__((noinline, aligned(64))) uint64_t dataflow_benchmark_scalar(Dataflow* flow)
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

// --after-scalar: complete scalar solves of `flow` for at least `milliseconds`, untimed, before an AVX-512 sample. A
// compiler runs its vector phases in short bursts after lighter ones, which let the clock boost (from about 5.31 to
// 5.52 GHz on the 9700X, over 30 to 300 ms), and at that clock a heavy vector loop runs under the core's power cap
// until the clock comes down, 5 to 20 ms later (#199; docs/dataflow.md). Back-to-back samples miss that state.
static bool dataflow_benchmark_after_scalar(Dataflow* flow, uint64_t milliseconds)
{
    struct timespec now;
    bool valid = clock_gettime(CLOCK_MONOTONIC_RAW, &now) == 0;
    const uint64_t end = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec + milliseconds * 1000000ull;
    bool running = valid;
    while (running)
    {
        dataflow_benchmark_scalar(flow);
        __asm__ volatile("" : : "r"(flow->in), "r"(flow->out) : "memory");
        valid = clock_gettime(CLOCK_MONOTONIC_RAW, &now) == 0;
        running = valid && (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec < end;
    }
    return valid;
}

// `timed` has a bit per implementation to time (--only): bit 0 for the scalar solver and bit 1 for the AVX-512 one.
static bool dataflow_benchmark_calibrate(Dataflow* flows, uint64_t iterations, uint32_t timed, uint64_t* repeats)
{
    DataflowBenchmarkCounters no_counters =
    {
        .cycles = -1,
        .instructions = -1,
        .throttle = -1,
        .available = false,
    };
    bool valid = true;
    bool ready = false;
    *repeats = 1;
    while (valid && !ready)
    {
        // Repeated warm batches account for clocks and caches settling after the first solve. The timed
        // implementations use one repetition count, and every order must reach twice the required retained-sample
        // duration.
        double shortest = BUSTER_DATAFLOW_BENCHMARK_CALIBRATION_NS;
        for (uint32_t round = 0; valid && round < 2; round += 1)
        {
            for (uint32_t turn = 0; valid && turn < 2; turn += 1)
            {
                const uint32_t variant = (round + turn) % 2;
                if (timed & (1u << variant))
                {
                    DataflowBenchmarkSample sample;
                    valid = dataflow_benchmark_run(&flows[variant], variant != 0, *repeats, &no_counters, &sample);
                    valid = valid && sample.iterations == iterations * (*repeats);
                    shortest = sample.nanoseconds < shortest ? sample.nanoseconds : shortest;
                }
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

static bool dataflow_benchmark_workload(const DataflowBenchmarkWorkload* workload, uint64_t seed, uint32_t timed, uint64_t after_scalar, DataflowBenchmarkCounters* counters)
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
        valid = dataflow_benchmark_calibrate(flows, iterations, timed, &repeats);
        if (!valid)
        {
            fprintf(stderr, "dataflow warm calibration failed for %s\n", workload->name);
        }
    }

    double nanoseconds[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    double cycles[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    double instructions[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
    double throttle[2][BUSTER_DATAFLOW_BENCHMARK_PASSES] = { 0 };
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
                if (timed & (1u << variant))
                {
                    DataflowBenchmarkSample sample = { 0 };
                    if (after_scalar != 0 && variant != 0)
                    {
                        valid = dataflow_benchmark_after_scalar(&flows[0], after_scalar);
                    }
                    valid = valid && dataflow_benchmark_run(&flows[variant], variant != 0, repeats, counters, &sample);
                    valid = valid && sample.iterations == iterations * repeats;
                    short_sample = sample.nanoseconds < BUSTER_DATAFLOW_BENCHMARK_SAMPLE_NS;
                    nanoseconds[variant][pass] = sample.nanoseconds / (double)repeats;
                    cycles[variant][pass] = sample.cycles / (double)repeats;
                    instructions[variant][pass] = sample.instructions / (double)repeats;
                    throttle[variant][pass] = sample.throttle / (double)repeats;
                }
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
            if (timed & (1u << variant))
            {
                const DataflowBenchmarkStatistics time = dataflow_benchmark_statistics(nanoseconds[variant]);
                const DataflowBenchmarkStatistics cycle = counters->available ? dataflow_benchmark_statistics(cycles[variant]) : (DataflowBenchmarkStatistics){ 0 };
                const DataflowBenchmarkStatistics instruction = counters->available ? dataflow_benchmark_statistics(instructions[variant]) : (DataflowBenchmarkStatistics){ 0 };
                const DataflowBenchmarkStatistics slots = counters->available ? dataflow_benchmark_statistics(throttle[variant]) : (DataflowBenchmarkStatistics){ 0 };
                const double blocks_iterations = (double)workload->blocks * (double)iterations;
                printf("  %-6s %.1f [%.1f-%.1f] ns/function %.1f [%.1f-%.1f] cycles/function %.3f [%.3f-%.3f] cycles/block/iteration %.1f instructions/function %.2f IPC %.3f throttle/block/iteration min_sample_ms=%.3f\n",
                       variant == 0 ? "scalar" : "avx512", time.median, time.minimum, time.maximum,
                       cycle.median, cycle.minimum, cycle.maximum,
                       cycle.median / blocks_iterations, cycle.minimum / blocks_iterations, cycle.maximum / blocks_iterations,
                       instruction.median, cycle.median != 0 ? instruction.median / cycle.median : 0,
                       slots.median / blocks_iterations, time.minimum * (double)repeats / 1e6);
            }
        }
    }
    else
    {
        fprintf(stderr, "dataflow benchmark failed for %s (allocation, clock, calibration, or scalar/vector disagreement)\n", workload->name);
    }
    arena_release(&arena);
    return valid;
}

// A number in `base` (0 takes C's prefixes), into `value` only if the whole text is one.
static bool dataflow_benchmark_number(const char* text, int base, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, base);
    const bool valid = text[0] >= '0' && text[0] <= '9' && end != text && *end == '\0' && errno == 0;
    if (valid)
    {
        *value = (uint64_t)parsed;
    }
    return valid;
}

int run_dataflow_benchmark(int argc, char** argv)
{
    bool valid = true;
    uint64_t seed = 1;
    uint64_t after_scalar = 0;
    // Bit 0 times the scalar solver and bit 1 the AVX-512 one; --only keeps one.
    uint32_t timed = 3;
    for (int argument = 1; argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc)
        {
            valid = valid && dataflow_benchmark_number(argv[argument + 1], 0, &seed);
            argument += 1;
        }
        else if (strcmp(argv[argument], "--after-scalar") == 0 && argument + 1 < argc)
        {
            valid = valid && dataflow_benchmark_number(argv[argument + 1], 10, &after_scalar) && after_scalar <= BUSTER_DATAFLOW_BENCHMARK_AFTER_SCALAR_MAX_MS;
            argument += 1;
        }
        else if (strcmp(argv[argument], "--only") == 0 && argument + 1 < argc)
        {
            const char* name = argv[argument + 1];
            valid = valid && (strcmp(name, "scalar") == 0 || strcmp(name, "avx512") == 0);
            timed = strcmp(name, "scalar") == 0 ? 1 : 2;
            argument += 1;
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: %s [--seed N] [--after-scalar MS] [--only scalar|avx512]\n", argv[0]);
    }
    else
    {
        DataflowBenchmarkCounters counters = dataflow_benchmark_open_counters();
        printf("dataflow: generator=%u seed=%" PRIu64 ", 21 interleaved-pass median [min-max]; complete solves including the rows each solver clears", BUSTER_DATAFLOW_GENERATOR_VERSION,
               seed);
        if (timed != 3)
        {
            printf("; only %s", timed == 1 ? "scalar" : "avx512");
        }
        if (after_scalar != 0)
        {
            printf("; each avx512 sample after %" PRIu64 " ms of scalar solves", after_scalar);
        }
        printf("\nthrottle: PMCx1A0 umask 0x08, read as the dispatch slots Zen 5's power cap takes (#199)\n");
        if (!counters.available)
        {
            printf("hardware counters unavailable; cycles, instructions, IPC and throttle are zero\n");
        }
        else if (counters.throttle < 0)
        {
            printf("PMCx1A0 umask 0x08 (raw event 0x%llx) unavailable; throttle is zero\n", BUSTER_DATAFLOW_BENCHMARK_THROTTLE_EVENT);
        }
        const uint32_t count = sizeof(dataflow_benchmark_workloads) / sizeof(dataflow_benchmark_workloads[0]);
        for (uint32_t workload = 0; valid && workload < count; workload += 1)
        {
            valid = dataflow_benchmark_workload(&dataflow_benchmark_workloads[workload], seed, timed, after_scalar, &counters);
        }
        dataflow_benchmark_close_counters(&counters);
    }
    return valid ? 0 : 1;
}

#else
int run_dataflow_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
