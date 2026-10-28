// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic CSR -> analysis -> consumer benchmark, not source-corpus acceptance or a measured AUTO threshold.
// This translation unit includes its original test generator. Analysis mode includes validation, allocation, RPO,
// solving, tree/frontier/loop construction, and consumer queries. Pipeline mode also rebuilds both CSR views.
// Clock, perf readings and sample selection below are original; no imported benchmark helper is adapted.

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
#include <fcntl.h>
#include <linux/perf_event.h>
#include <time.h>
#include <sys/syscall.h>

#include "buster/compiler/lane.c"
#include "buster/compiler/dominators.c"
#include "buster/compiler/dominators_test.c"

#define BUSTER_DOMINATOR_BENCHMARK_SAMPLES 11
#define BUSTER_DOMINATOR_BENCHMARK_MIN_NS UINT64_C(2000000)

typedef struct DominatorBenchmarkCase DominatorBenchmarkCase;
struct DominatorBenchmarkCase
{
    const char* name;
    uint32_t blocks;
    uint32_t shape;
};

typedef struct DominatorBenchmarkSample DominatorBenchmarkSample;
struct DominatorBenchmarkSample
{
    double ns;
    double cycles;
};

typedef struct DominatorBenchmarkCounter DominatorBenchmarkCounter;
struct DominatorBenchmarkCounter
{
    uint64_t cycles;
    uint64_t enabled;
    uint64_t running;
};

static uint64_t dominator_benchmark_clock(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static int dominator_benchmark_counter(void)
{
    struct perf_event_attr event = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    const int fd = (int)syscall(SYS_perf_event_open, &event, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    return fd;
}

static DominatorBenchmarkCounter dominator_benchmark_read(int fd)
{
    DominatorBenchmarkCounter result = {0};
    if (fd >= 0)
    {
        DominatorBenchmarkCounter value;
        const ssize_t bytes = read(fd, &value, sizeof(value));
        if (bytes == sizeof(value))
        {
            result = value;
        }
    }
    return result;
}

// A whole handoff, using direct calls. Output and temporary storage are reused between repetitions.
static __attribute__((noinline, aligned(64))) bool dominator_benchmark_run(Arena* inputs, Arena* output, Arena* scratch, const DominatorCfg* frozen, DominatorBenchmarkCase workload, uint64_t seed, DominatorOptions options, bool pipeline, uint64_t* checksum)
{
    const uint64_t input_start = inputs->length;
    const uint64_t output_start = output->length;
    DominatorCfg cfg = *frozen;
    bool success = !pipeline || dominator_test_generate(inputs, workload.blocks, workload.shape, seed, &cfg);
    Dominators analysis;
    if (success)
    {
        success = dominators_analyze(output, scratch, &cfg, options, &analysis) == DOMINATOR_OK;
    }
    if (success)
    {
        uint64_t sum = analysis.reachable_count + analysis.loop_count + analysis.iterations;
        // Ordinary consumers ask dominance and read the innermost loop of each block; no output is dead.
        for (uint32_t block = 0; block < analysis.block_count; block += 1)
        {
            const uint32_t parent = analysis.idom[block];
            sum += parent;
            sum += dominators_dominates(&analysis, parent, block);
            if ((options.outputs & BUSTER_DOMINATOR_LOOPS) != 0)
            {
                sum += analysis.loop_depth[block];
                sum += analysis.block_loop[block];
            }
            if ((options.outputs & BUSTER_DOMINATOR_FRONTIERS) != 0)
            {
                sum += dominators_frontier_contains(&analysis, block, block);
            }
        }
        *checksum += sum;
    }
    arena_pop_to(output, output_start);
    arena_pop_to(inputs, input_start);
    return success;
}

static bool dominator_benchmark_sample(Arena* inputs, Arena* output, Arena* scratch, const DominatorCfg* cfg, DominatorBenchmarkCase workload, uint64_t seed, DominatorOptions options, bool pipeline, uint32_t repetitions, int fd, uint64_t* checksum, DominatorBenchmarkSample* result)
{
    const DominatorBenchmarkCounter before = dominator_benchmark_read(fd);
    const uint64_t start = dominator_benchmark_clock();
    bool success = true;
    for (uint32_t i = 0; success && i < repetitions; i += 1)
    {
        success = dominator_benchmark_run(inputs, output, scratch, cfg, workload, seed, options, pipeline, checksum);
    }
    const uint64_t elapsed = dominator_benchmark_clock() - start;
    const DominatorBenchmarkCounter after = dominator_benchmark_read(fd);
    const uint64_t enabled = after.enabled - before.enabled;
    const uint64_t running = after.running - before.running;
    const bool cycles_valid = fd >= 0 && enabled != 0 && enabled == running && after.cycles >= before.cycles;
    result->ns = (double)elapsed / repetitions;
    result->cycles = cycles_valid ? (double)(after.cycles - before.cycles) / repetitions : -1;
    return success;
}

static double dominator_benchmark_middle(const DominatorBenchmarkSample* samples, bool cycles, double* low, double* high)
{
    double values[BUSTER_DOMINATOR_BENCHMARK_SAMPLES];
    for (uint32_t i = 0; i < BUSTER_DOMINATOR_BENCHMARK_SAMPLES; i += 1)
    {
        values[i] = cycles ? samples[i].cycles : samples[i].ns;
    }
    for (uint32_t i = 0; i < BUSTER_DOMINATOR_BENCHMARK_SAMPLES; i += 1)
    {
        uint32_t least = i;
        for (uint32_t j = i + 1; j < BUSTER_DOMINATOR_BENCHMARK_SAMPLES; j += 1)
        {
            if (values[j] < values[least])
            {
                least = j;
            }
        }
        const double selected = values[least];
        values[least] = values[i];
        values[i] = selected;
    }
    *low = values[0];
    *high = values[BUSTER_DOMINATOR_BENCHMARK_SAMPLES - 1];
    return values[BUSTER_DOMINATOR_BENCHMARK_SAMPLES / 2];
}

int run_dominator_benchmark(int argc, char** argv)
{
    uint64_t seed = 174;
    uint64_t after_scalar = 0;
    uint32_t only = 2;
    uint32_t phase = 2;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &seed);
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &after_scalar) && after_scalar <= 1000;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = strcmp(argv[i], "scalar") == 0 || strcmp(argv[i], "vector") == 0;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : 1;
        }
        else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = strcmp(argv[i], "analysis") == 0 || strcmp(argv[i], "pipeline") == 0;
            phase = strcmp(argv[i], "analysis") == 0 ? 0 : 1;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-dominators [--seed N] [--only scalar|vector] [--phase analysis|pipeline] [--after-scalar MS]\n");
    }
    else
    {
        const DominatorBenchmarkCase cases[] = {
            {
                .name = "diamonds",
                .blocks = 16,
                .shape = 1,
            },
            {
                .name = "nested",
                .blocks = 256,
                .shape = 2,
            },
            {
                .name = "irreducible",
                .blocks = 513,
                .shape = 3,
            },
            {
                .name = "dispatch",
                .blocks = 2049,
                .shape = 4,
            },
        };
        const uint32_t outputs[] = {
            0,
            BUSTER_DOMINATOR_LOOPS,
            BUSTER_DOMINATOR_FRONTIERS | BUSTER_DOMINATOR_LOOPS,
        };
        const char* output_names[] = {
            "tree",
            "loops",
            "all",
        };
        Arena inputs = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
        Arena output = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
        Arena scratch = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
        bool success = inputs.base != 0 && output.base != 0 && scratch.base != 0;
        const int fd = dominator_benchmark_counter();
        uint64_t checksum = 0;
        printf("dominators synthetic CSR benchmark; seed=%" PRIu64 ", %u samples, after-scalar=%" PRIu64 " ms; cycles=%s\n", seed, BUSTER_DOMINATOR_BENCHMARK_SAMPLES, after_scalar, fd >= 0 ? "perf (nonmultiplexed samples only)" : "unavailable");
        for (uint32_t case_index = 0; success && case_index < sizeof(cases) / sizeof(cases[0]); case_index += 1)
        {
            const DominatorBenchmarkCase workload = cases[case_index];
            DominatorCfg cfg;
            success = dominator_test_generate(&inputs, workload.blocks, workload.shape, seed, &cfg);
            for (uint32_t mode = 0; success && mode < sizeof(outputs) / sizeof(outputs[0]); mode += 1)
            {
                DominatorOptions options = dominators_options();
                options.outputs = outputs[mode];
                Dominators oracle[2];
                for (uint32_t path = 0; success && path < 2; path += 1)
                {
                    options.path = path == 0 ? DOMINATOR_SCALAR : DOMINATOR_VECTOR;
                    success = dominators_analyze(&output, &scratch, &cfg, options, &oracle[path]) == DOMINATOR_OK;
                }
                if (success)
                {
                    success = dominator_test_equal(&oracle[0], &oracle[1]);
                }
                for (uint32_t boundary = 0; success && boundary < 2; boundary += 1)
                {
                    if (phase == 2 || phase == boundary)
                    {
                        const bool pipeline = boundary != 0;
                        DominatorBenchmarkSample samples[2][BUSTER_DOMINATOR_BENCHMARK_SAMPLES];
                        uint32_t repetitions = 1;
                        bool calibrated = false;
                        while (success && !calibrated)
                        {
                            calibrated = true;
                            for (uint32_t path = 0; success && path < 2; path += 1)
                            {
                                if (only == 2 || only == path)
                                {
                                    DominatorBenchmarkSample probe;
                                    options.path = path == 0 ? DOMINATOR_SCALAR : DOMINATOR_VECTOR;
                                    success = dominator_benchmark_sample(&inputs, &output, &scratch, &cfg, workload, seed, options, pipeline, repetitions, fd, &checksum, &probe);
                                    calibrated = calibrated && probe.ns * repetitions >= BUSTER_DOMINATOR_BENCHMARK_MIN_NS;
                                }
                            }
                            if (!calibrated && repetitions < (1u << 20))
                            {
                                repetitions *= 2;
                            }
                            else
                            {
                                calibrated = true;
                            }
                        }
                        for (uint32_t sample = 0; success && sample < BUSTER_DOMINATOR_BENCHMARK_SAMPLES; sample += 1)
                        {
                            for (uint32_t turn = 0; success && turn < 2; turn += 1)
                            {
                                const uint32_t path = (turn + sample) % 2;
                                if (only == 2 || only == path)
                                {
                                    if (path == 1 && after_scalar != 0)
                                    {
                                        options.path = DOMINATOR_SCALAR;
                                        const uint64_t warm_until = dominator_benchmark_clock() + after_scalar * UINT64_C(1000000);
                                        do
                                        {
                                            success = dominator_benchmark_run(&inputs, &output, &scratch, &cfg, workload, seed, options, pipeline, &checksum);
                                        } while (success && dominator_benchmark_clock() < warm_until);
                                    }
                                    options.path = path == 0 ? DOMINATOR_SCALAR : DOMINATOR_VECTOR;
                                    if (success)
                                    {
                                        success = dominator_benchmark_sample(&inputs, &output, &scratch, &cfg, workload, seed, options, pipeline, repetitions, fd, &checksum, &samples[path][sample]);
                                    }
                                }
                            }
                        }
                        for (uint32_t path = 0; success && path < 2; path += 1)
                        {
                            if (only == 2 || only == path)
                            {
                                double low;
                                double high;
                                const double ns = dominator_benchmark_middle(samples[path], false, &low, &high);
                                double cycle_low;
                                double cycle_high;
                                const double cycles = dominator_benchmark_middle(samples[path], true, &cycle_low, &cycle_high);
                                printf("%s blocks=%u edges=%u %s %s %s: %.3f ns/block [%.3f, %.3f], %.3f Mblocks/s, ", workload.name, workload.blocks, cfg.edge_count, pipeline ? "pipeline" : "analysis", output_names[mode], path == 0 ? "scalar" : "vector", ns / workload.blocks, low / workload.blocks, high / workload.blocks, workload.blocks * 1000.0 / ns);
                                if (cycle_low >= 0)
                                {
                                    printf("%.3f cycles/block, ", cycles / workload.blocks);
                                }
                                else
                                {
                                    printf("cycles unavailable, ");
                                }
                                printf("output=%" PRIu64 " scratch-peak=%" PRIu64 " matrix-peak=%" PRIu64 " bytes, reachable=%u loops=%u sweeps=%" PRIu64 " repetitions=%u\n", oracle[path].output_bytes, oracle[path].scratch_peak_bytes, oracle[path].matrix_peak_bytes, oracle[path].reachable_count, oracle[path].loop_count, oracle[path].iterations, repetitions);
                            }
                        }
                    }
                }
                arena_pop_to(&output, 0);
            }
            arena_pop_to(&inputs, 0);
        }
        printf("dominators benchmark: %s; checksum=%" PRIu64 "; corpus/real-producer acceptance pending\n", success ? "passed" : "FAILED", checksum);
        if (fd >= 0)
        {
            close(fd);
        }
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&inputs);
        result = success ? 0 : 1;
    }
    return result;
}

#else
int run_dominator_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
