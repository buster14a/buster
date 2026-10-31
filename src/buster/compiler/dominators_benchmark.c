// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic CSR -> analysis -> consumer benchmark, and with --cfg the same analysis on real source CFGs that
// test-ir-source --dominators-cfg exported (dominators_export.c). This translation unit includes its original test
// generator. Analysis mode includes validation, allocation, RPO,
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
#include "buster/compiler/dominators_export.c"
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

// --cfg: real CFGs from a test-ir-source --dominators-cfg export, measured by block-count bucket, the largest ones
// alone and all together. A set's sample analyzes each of its CFGs once per repetition, in file order, with the
// same per-call work as the synthetic analysis phase; its time is divided by the set's blocks.
#define BUSTER_DOMINATOR_BENCHMARK_LARGEST 8
#define BUSTER_DOMINATOR_BENCHMARK_PATHS 3

typedef struct DominatorBenchmarkSet DominatorBenchmarkSet;
struct DominatorBenchmarkSet
{
    const uint32_t* members;
    uint32_t count;
    uint64_t blocks;
    uint64_t edges;
};

static const DominatorPath dominator_benchmark_paths[BUSTER_DOMINATOR_BENCHMARK_PATHS] = {
    DOMINATOR_SCALAR,
    DOMINATOR_VECTOR,
    DOMINATOR_AUTO,
};

static const char* const dominator_benchmark_path_names[BUSTER_DOMINATOR_BENCHMARK_PATHS] = {
    "scalar",
    "vector",
    "auto",
};

static bool dominator_benchmark_set_sample(Arena* inputs, Arena* output, Arena* scratch, const DominatorExport* export, const DominatorBenchmarkSet* set, DominatorOptions options, uint32_t repetitions, int fd, uint64_t* checksum, DominatorBenchmarkSample* result)
{
    const DominatorBenchmarkCase workload = {
        .name = "cfg",
    };
    const DominatorBenchmarkCounter before = dominator_benchmark_read(fd);
    const uint64_t start = dominator_benchmark_clock();
    bool success = true;
    for (uint32_t i = 0; success && i < repetitions; i += 1)
    {
        for (uint32_t member = 0; success && member < set->count; member += 1)
        {
            success = dominator_benchmark_run(inputs, output, scratch, &export->graphs[set->members[member]].cfg, workload, 0, options, false, checksum);
        }
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

static void dominator_benchmark_print_name(const uint8_t* name, uint32_t length)
{
    for (uint32_t i = 0; i < length; i += 1)
    {
        if (name[i] > ' ' && name[i] < 127 && name[i] != '\\')
        {
            putchar(name[i]);
        }
        else
        {
            printf("\\x%02x", name[i]);
        }
    }
}

// Measures one set in one output mode on the selected paths, 11 samples in rotating path order, and prints a line
// per path and the ratios of their median times.
static bool dominator_benchmark_set(Arena* inputs, Arena* output, Arena* scratch, const DominatorExport* export, const DominatorBenchmarkSet* set, const char* label, uint32_t outputs, const char* output_name, uint32_t only, uint64_t after_scalar, int fd, uint64_t* checksum)
{
    DominatorOptions options = dominators_options();
    options.outputs = outputs;
    bool success = true;
    // What AUTO chose for the set's members, from complete analyses outside the timing.
    uint32_t auto_vector = 0;
    uint32_t auto_loop_vector = 0;
    for (uint32_t member = 0; success && member < set->count; member += 1)
    {
        const uint64_t output_start = output->length;
        Dominators analysis;
        options.path = DOMINATOR_AUTO;
        success = dominators_analyze(output, scratch, &export->graphs[set->members[member]].cfg, options, &analysis) == DOMINATOR_OK;
        auto_vector += success && analysis.path == DOMINATOR_VECTOR;
        auto_loop_vector += success && analysis.loop_count != 0 && analysis.loop_path == DOMINATOR_VECTOR;
        arena_pop_to(output, output_start);
    }
    DominatorBenchmarkSample samples[BUSTER_DOMINATOR_BENCHMARK_PATHS][BUSTER_DOMINATOR_BENCHMARK_SAMPLES];
    uint32_t repetitions = 1;
    bool calibrated = false;
    while (success && !calibrated)
    {
        calibrated = true;
        for (uint32_t path = 0; success && path < BUSTER_DOMINATOR_BENCHMARK_PATHS; path += 1)
        {
            if (only == BUSTER_DOMINATOR_BENCHMARK_PATHS || only == path)
            {
                DominatorBenchmarkSample probe;
                options.path = dominator_benchmark_paths[path];
                success = dominator_benchmark_set_sample(inputs, output, scratch, export, set, options, repetitions, fd, checksum, &probe);
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
        for (uint32_t turn = 0; success && turn < BUSTER_DOMINATOR_BENCHMARK_PATHS; turn += 1)
        {
            const uint32_t path = (turn + sample) % BUSTER_DOMINATOR_BENCHMARK_PATHS;
            if (only == BUSTER_DOMINATOR_BENCHMARK_PATHS || only == path)
            {
                if (path != 0 && after_scalar != 0)
                {
                    options.path = DOMINATOR_SCALAR;
                    const uint64_t warm_until = dominator_benchmark_clock() + after_scalar * UINT64_C(1000000);
                    do
                    {
                        DominatorBenchmarkSample warm;
                        success = dominator_benchmark_set_sample(inputs, output, scratch, export, set, options, 1, -1, checksum, &warm);
                    } while (success && dominator_benchmark_clock() < warm_until);
                }
                options.path = dominator_benchmark_paths[path];
                if (success)
                {
                    success = dominator_benchmark_set_sample(inputs, output, scratch, export, set, options, repetitions, fd, checksum, &samples[path][sample]);
                }
            }
        }
    }
    double medians[BUSTER_DOMINATOR_BENCHMARK_PATHS] = { 0 };
    for (uint32_t path = 0; success && path < BUSTER_DOMINATOR_BENCHMARK_PATHS; path += 1)
    {
        if (only == BUSTER_DOMINATOR_BENCHMARK_PATHS || only == path)
        {
            double low;
            double high;
            medians[path] = dominator_benchmark_middle(samples[path], false, &low, &high);
            double cycle_low;
            double cycle_high;
            const double cycles = dominator_benchmark_middle(samples[path], true, &cycle_low, &cycle_high);
            printf("%s cfgs=%u blocks=%" PRIu64 " edges=%" PRIu64 " %s %s: %.3f ns/block [%.3f, %.3f], ", label, set->count, set->blocks, set->edges, output_name, dominator_benchmark_path_names[path], medians[path] / set->blocks, low / set->blocks, high / set->blocks);
            if (cycle_low >= 0)
            {
                printf("%.3f cycles/block [%.3f, %.3f], ", cycles / set->blocks, cycle_low / set->blocks, cycle_high / set->blocks);
            }
            else
            {
                printf("cycles unavailable, ");
            }
            printf("repetitions=%u\n", repetitions);
        }
    }
    if (success && only == BUSTER_DOMINATOR_BENCHMARK_PATHS)
    {
        printf("%s %s: vector/scalar=%.3f auto/scalar=%.3f; auto chose the vector solver for %u of %u, vector loops for %u\n", label, output_name, medians[1] / medians[0], medians[2] / medians[0], auto_vector, set->count, auto_loop_vector);
    }
    return success;
}

static int dominator_benchmark_cfg(const char* name, uint64_t form, uint32_t only, uint32_t outputs_only, uint64_t after_scalar)
{
    Arena inputs = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
    Arena output = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
    Arena scratch = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
    bool success = inputs.base != 0 && output.base != 0 && scratch.base != 0;
    FILE* file = success ? fopen(name, "rb") : 0;
    success = file != 0 && fseek(file, 0, SEEK_END) == 0;
    const long size = success ? ftell(file) : -1;
    success = success && size >= 0 && fseek(file, 0, SEEK_SET) == 0;
    uint8_t* text = success ? arena_push(&inputs, (uint64_t)size, 64) : 0;
    success = success && text != 0 && fread(text, 1, (size_t)size, file) == (size_t)size;
    if (file != 0)
    {
        fclose(file);
    }
    DominatorExport export = { 0 };
    success = success && dominators_export_read(&inputs, text, (uint64_t)size, &export);
    if (!success)
    {
        fprintf(stderr, "benchmark-dominators: %s is not a complete buster-dominators-v1 export\n", name);
    }
    // The selected CFGs' indices, grouped by bucket: floor(log2(blocks)), then the largest and all of them.
    uint32_t* members = success ? arena_push_array(&inputs, (uint64_t)export.count * 2 + BUSTER_DOMINATOR_BENCHMARK_LARGEST, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    success = success && members != 0;
    uint32_t bucket_counts[33] = { 0 };
    uint32_t selected = 0;
    for (uint32_t i = 0; success && i < export.count; i += 1)
    {
        const DominatorExportGraph* graph = &export.graphs[i];
        if ((form == UINT64_MAX || graph->form == form) && graph->cfg.block_count != 0)
        {
            bucket_counts[1 + (31 - __builtin_clz(graph->cfg.block_count))] += 1;
            members[export.count + selected] = i;
            selected += 1;
        }
    }
    success = success && selected != 0;
    for (uint32_t bucket = 1; bucket < 33; bucket += 1)
    {
        bucket_counts[bucket] += bucket_counts[bucket - 1];
    }
    uint32_t places[33];
    memcpy(places, bucket_counts, sizeof(places));
    for (uint32_t i = 0; success && i < selected; i += 1)
    {
        const uint32_t index = members[export.count + i];
        const uint32_t bucket = 31 - __builtin_clz(export.graphs[index].cfg.block_count);
        members[places[bucket]] = index;
        places[bucket] += 1;
    }
    uint32_t* largest = members + (uint64_t)export.count * 2;
    uint32_t largest_count = 0;
    for (uint32_t rank = 0; success && rank < BUSTER_DOMINATOR_BENCHMARK_LARGEST && rank < selected; rank += 1)
    {
        uint32_t best = UINT32_MAX;
        for (uint32_t i = 0; i < selected; i += 1)
        {
            const uint32_t index = members[export.count + i];
            bool taken = false;
            for (uint32_t earlier = 0; earlier < largest_count; earlier += 1)
            {
                taken = taken || largest[earlier] == index;
            }
            if (!taken && (best == UINT32_MAX || export.graphs[index].cfg.block_count > export.graphs[best].cfg.block_count))
            {
                best = index;
            }
        }
        largest[largest_count] = best;
        largest_count += 1;
    }
    const int fd = dominator_benchmark_counter();
    uint64_t checksum = 0;
    if (success)
    {
        printf("dominators CFG benchmark: %s, %u records (files=%" PRIu64 " functions=%" PRIu64 "), %u selected", name, export.count, export.files, export.functions, selected);
        if (form != UINT64_MAX)
        {
            printf(" of form %" PRIu64, form);
        }
        printf("; %u samples, after-scalar=%" PRIu64 " ms; cycles=%s\n", BUSTER_DOMINATOR_BENCHMARK_SAMPLES, after_scalar, fd >= 0 ? "perf (nonmultiplexed samples only)" : "unavailable");
    }
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
    // Every selected CFG's complete scalar, vector and AUTO analyses must agree before anything is timed.
    uint64_t compared = 0;
    for (uint32_t mode = 0; success && mode < 3; mode += 1)
    {
        DominatorOptions options = dominators_options();
        options.outputs = outputs[mode];
        for (uint32_t i = 0; success && i < selected; i += 1)
        {
            const DominatorCfg* cfg = &export.graphs[members[export.count + i]].cfg;
            Dominators analyses[BUSTER_DOMINATOR_BENCHMARK_PATHS];
            for (uint32_t path = 0; success && path < BUSTER_DOMINATOR_BENCHMARK_PATHS; path += 1)
            {
                options.path = dominator_benchmark_paths[path];
                success = dominators_analyze(&output, &scratch, cfg, options, &analyses[path]) == DOMINATOR_OK;
            }
            success = success && dominator_test_equal(&analyses[0], &analyses[1]) && dominator_test_equal(&analyses[0], &analyses[2]);
            compared += success;
            arena_pop_to(&output, 0);
        }
    }
    if (success)
    {
        printf("dominators CFG benchmark: scalar, vector and AUTO agree on all %" PRIu64 " analyses\n", compared);
    }
    char label[256];
    for (uint32_t mode = 0; success && mode < 3; mode += 1)
    {
        if (outputs_only == 3 || outputs_only == mode)
        {
            for (uint32_t bucket = 0; success && bucket < 32; bucket += 1)
            {
                DominatorBenchmarkSet set = {
                    .members = members + bucket_counts[bucket],
                    .count = bucket_counts[bucket + 1] - bucket_counts[bucket],
                };
                for (uint32_t i = 0; i < set.count; i += 1)
                {
                    set.blocks += export.graphs[set.members[i]].cfg.block_count;
                    set.edges += export.graphs[set.members[i]].cfg.edge_count;
                }
                if (set.count != 0)
                {
                    snprintf(label, sizeof(label), "set=blocks[%" PRIu64 ",%" PRIu64 ")", UINT64_C(1) << bucket, UINT64_C(1) << (bucket + 1));
                    success = dominator_benchmark_set(&inputs, &output, &scratch, &export, &set, label, outputs[mode], output_names[mode], only, after_scalar, fd, &checksum);
                }
            }
            for (uint32_t rank = 0; success && rank < largest_count; rank += 1)
            {
                const DominatorExportGraph* graph = &export.graphs[largest[rank]];
                const DominatorBenchmarkSet set = {
                    .members = &largest[rank],
                    .count = 1,
                    .blocks = graph->cfg.block_count,
                    .edges = graph->cfg.edge_count,
                };
                printf("set=largest-%u form=%u name=", rank + 1, graph->form);
                dominator_benchmark_print_name(graph->name, graph->name_length);
                printf("\n");
                snprintf(label, sizeof(label), "set=largest-%u", rank + 1);
                success = dominator_benchmark_set(&inputs, &output, &scratch, &export, &set, label, outputs[mode], output_names[mode], only, after_scalar, fd, &checksum);
            }
            DominatorBenchmarkSet all = {
                .members = members + export.count,
                .count = selected,
            };
            for (uint32_t i = 0; i < selected; i += 1)
            {
                all.blocks += export.graphs[all.members[i]].cfg.block_count;
                all.edges += export.graphs[all.members[i]].cfg.edge_count;
            }
            success = success && dominator_benchmark_set(&inputs, &output, &scratch, &export, &all, "set=all", outputs[mode], output_names[mode], only, after_scalar, fd, &checksum);
        }
    }
    printf("dominators CFG benchmark: %s; checksum=%" PRIu64 "\n", success ? "passed" : "FAILED", checksum);
    if (fd >= 0)
    {
        close(fd);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&inputs);
    return success ? 0 : 1;
}

int run_dominator_benchmark(int argc, char** argv)
{
    uint64_t seed = 174;
    uint64_t after_scalar = 0;
    uint32_t only = 2;
    uint32_t phase = 2;
    const char* cfg_name = 0;
    uint64_t form = UINT64_MAX;
    uint32_t cfg_only = BUSTER_DOMINATOR_BENCHMARK_PATHS;
    uint32_t outputs_only = 3;
    bool synthetic_option = false;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &seed);
            synthetic_option = true;
        }
        else if (strcmp(argv[i], "--cfg") == 0 && i + 1 < argc && cfg_name == 0)
        {
            i += 1;
            cfg_name = argv[i];
        }
        else if (strcmp(argv[i], "--form") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &form) && form < UINT32_MAX;
        }
        else if (strcmp(argv[i], "--outputs") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = strcmp(argv[i], "tree") == 0 || strcmp(argv[i], "loops") == 0 || strcmp(argv[i], "all") == 0;
            outputs_only = strcmp(argv[i], "tree") == 0 ? 0 : strcmp(argv[i], "loops") == 0 ? 1 : 2;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &after_scalar) && after_scalar <= 1000;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = strcmp(argv[i], "scalar") == 0 || strcmp(argv[i], "vector") == 0 || strcmp(argv[i], "auto") == 0;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : 1;
            cfg_only = strcmp(argv[i], "scalar") == 0 ? 0 : strcmp(argv[i], "vector") == 0 ? 1 : 2;
        }
        else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = strcmp(argv[i], "analysis") == 0 || strcmp(argv[i], "pipeline") == 0;
            phase = strcmp(argv[i], "analysis") == 0 ? 0 : 1;
            synthetic_option = true;
        }
        else
        {
            valid = false;
        }
    }
    // --form, --outputs and --only auto only measure an export; --seed and --phase only the synthetic shapes.
    valid = valid && (cfg_name != 0 ? !synthetic_option : form == UINT64_MAX && outputs_only == 3 && cfg_only != 2);
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-dominators [--seed N] [--only scalar|vector] [--phase analysis|pipeline] [--after-scalar MS]\n"
                        "       benchmark-dominators --cfg EXPORT [--form N] [--outputs tree|loops|all] [--only scalar|vector|auto] [--after-scalar MS]\n");
    }
    else if (cfg_name != 0)
    {
        result = dominator_benchmark_cfg(cfg_name, form, cfg_only, outputs_only, after_scalar);
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
