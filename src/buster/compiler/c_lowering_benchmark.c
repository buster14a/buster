// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic definition-graph benchmark for #126/#129, included after c_lowering.c by main.c. This does not extract
// references from C, check function bodies or lower selected bodies: it cannot establish main.c lowering savings.
// run_c_lowering_benchmark measures vector/scalar selection on a reusable prepared graph, and "prepare" measures
// arena reservation, first touches, validation/copy/CSR handoff and vector selection together. Input generation and
// the independent byte-mark/linked-edge oracle are outside all timings. Prepared graphs are checked before their
// arenas are released; that verification and arena teardown are outside the timing. Workspace is reported explicitly.
//
// Each requested path has one discarded warm-up and 21 single-operation samples, with rotating order. Every result
// is checked against the independent oracle, including reached bits, ordered IDs, kinds and work counts. Preparation
// also checks the full copied graph, including unreachable edges. The default is unconditioned, even with --only.
// Intervening result checks do not establish sustained vector execution. --before-vector MS runs the selector repeatedly
// immediately before vector/prepare samples; mutually exclusive --after-scalar MS runs the scalar selector instead.
// Conditioning uses a separate prepared plan so its final output remains available for checking AFTER the measured
// call. No result verification separates the last conditioning iteration from measurement, only small local setup,
// counter control and clock reads. Compare both conditions across separate processes on the locked 9700X, CPU 2.
// Nanoseconds cover one operation; perf cycles/instructions include the bracketing clock reads, and are unavailable
// if opening, enabling, reading or uninterrupted scheduling of the group fails. There is no prepare+scalar path, so
// these measurements cannot establish a whole-planner vector speedup.
//
// The clock, median and perf setup adapt this repository's original c_bindings_benchmark.c at
// c62cd8405e75838bd387d0bd1b7c025de43586f6 (Apache-2.0 WITH LLVM-exception). The two-counter group, synthetic graphs,
// independent oracle and preparation measurement are original additions; no external code or corpus is incorporated.
// Layout: c_lowering_benchmark_generate, c_lowering_benchmark_expected, c_lowering_benchmark_matches,
// c_lowering_benchmark_case, run_c_lowering_benchmark.

#include <inttypes.h>
#include <linux/perf_event.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_C_LOWERING_BENCHMARK_PASSES 21
#define BUSTER_C_LOWERING_BENCHMARK_MAX_DEFINITIONS 1048576u
#define BUSTER_C_LOWERING_BENCHMARK_DUPLICATES 32u
#define BUSTER_C_LOWERING_BENCHMARK_SHAPES 6u
#define BUSTER_C_LOWERING_BENCHMARK_PATHS 3u

typedef enum CLoweringBenchmarkShape CLoweringBenchmarkShape;
enum CLoweringBenchmarkShape
{
    C_LOWERING_BENCHMARK_INTRINSIC,
    C_LOWERING_BENCHMARK_FANOUT,
    C_LOWERING_BENCHMARK_CHAIN,
    C_LOWERING_BENCHMARK_CYCLE,
    C_LOWERING_BENCHMARK_DUPLICATE,
    C_LOWERING_BENCHMARK_LARGE,
};

typedef enum CLoweringBenchmarkPath CLoweringBenchmarkPath;
enum CLoweringBenchmarkPath
{
    C_LOWERING_BENCHMARK_VECTOR,
    C_LOWERING_BENCHMARK_SCALAR,
    C_LOWERING_BENCHMARK_PREPARE,
};

static const char* const c_lowering_benchmark_shapes[BUSTER_C_LOWERING_BENCHMARK_SHAPES] = {
    "intrinsic",
    "fanout",
    "chain",
    "cycle",
    "duplicates",
    "large",
};

static const uint32_t c_lowering_benchmark_sizes[BUSTER_C_LOWERING_BENCHMARK_SHAPES] = {
    5200,
    5200,
    16384,
    16384,
    5200,
    131072,
};

static const char* const c_lowering_benchmark_paths[BUSTER_C_LOWERING_BENCHMARK_PATHS] = {
    "vector",
    "scalar",
    "prepare",
};

typedef struct CLoweringBenchmarkOptions CLoweringBenchmarkOptions;
struct CLoweringBenchmarkOptions
{
    uint32_t definitions;
    uint32_t shape;
    uint32_t only;
    uint32_t after_scalar;
    uint32_t before_vector;
};

typedef struct CLoweringBenchmarkExpected CLoweringBenchmarkExpected;
struct CLoweringBenchmarkExpected
{
    uint8_t* reached;
    uint64_t* bits;
    uint32_t* selected;
    uint32_t* heads;
    uint32_t* next;
    uint32_t selected_count;
    uint32_t root_count;
    uint32_t edge_count;
    uint64_t functions;
    uint64_t objects;
    uint64_t visited_edges;
    uint64_t rounds;
};

typedef struct CLoweringBenchmarkCounters CLoweringBenchmarkCounters;
struct CLoweringBenchmarkCounters
{
    int cycles;
    int instructions;
};

typedef struct CLoweringBenchmarkSample CLoweringBenchmarkSample;
struct CLoweringBenchmarkSample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    bool cycles_available;
    bool instructions_available;
};

static uint64_t c_lowering_benchmark_now(void)
{
    struct timespec value = { 0 };
    const bool valid = clock_gettime(CLOCK_MONOTONIC_RAW, &value) == 0;
    return valid ? (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec : 0;
}

static uint64_t c_lowering_benchmark_median(const CLoweringBenchmarkSample* samples, uint32_t metric)
{
    uint64_t ordered[BUSTER_C_LOWERING_BENCHMARK_PASSES];
    for (uint32_t i = 0; i < BUSTER_C_LOWERING_BENCHMARK_PASSES; i += 1)
    {
        const uint64_t value = metric == 0 ? samples[i].nanoseconds : metric == 1 ? samples[i].cycles : samples[i].instructions;
        uint32_t j = i;
        while (j != 0 && ordered[j - 1] > value)
        {
            ordered[j] = ordered[j - 1];
            j -= 1;
        }
        ordered[j] = value;
    }
    return ordered[BUSTER_C_LOWERING_BENCHMARK_PASSES / 2];
}

static CLoweringBenchmarkCounters c_lowering_benchmark_counters(void)
{
    CLoweringBenchmarkCounters result = {
        .cycles = -1,
        .instructions = -1,
    };
    struct perf_event_attr attribute = {
        .type = PERF_TYPE_HARDWARE,
        .size = sizeof(struct perf_event_attr),
        .config = PERF_COUNT_HW_CPU_CYCLES,
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
        .read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    result.cycles = (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (result.cycles >= 0)
    {
        attribute.config = PERF_COUNT_HW_INSTRUCTIONS;
        attribute.disabled = 0;
        result.instructions = (int)syscall(SYS_perf_event_open, &attribute, 0, -1, result.cycles, PERF_FLAG_FD_CLOEXEC);
    }
    return result;
}

static bool c_lowering_benchmark_counter_begin(CLoweringBenchmarkCounters counters)
{
    const bool result = counters.cycles >= 0 && ioctl(counters.cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0 &&
                        ioctl(counters.cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == 0;
    return result;
}

static void c_lowering_benchmark_counter_end(CLoweringBenchmarkCounters counters, bool started, CLoweringBenchmarkSample* sample)
{
    if (counters.cycles >= 0)
    {
        const bool stopped = ioctl(counters.cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
        uint64_t values[5] = { 0 };
        const uint64_t count = counters.instructions >= 0 ? 2 : 1;
        const ssize_t bytes = (ssize_t)((3 + count) * sizeof(uint64_t));
        const bool read_ok = read(counters.cycles, values, (size_t)bytes) == bytes;
        // Unscaled group counts are useful only if the group ran for the whole enabled interval.
        const bool available = started && stopped && read_ok && values[0] == count && values[1] == values[2] && values[2] != 0;
        sample->cycles = values[3];
        sample->instructions = values[4];
        sample->cycles_available = available;
        sample->instructions_available = available && counters.instructions >= 0;
    }
}

static bool c_lowering_benchmark_number(const char* text, uint32_t minimum, uint32_t maximum, uint32_t* result)
{
    uint32_t value = 0;
    bool valid = text[0] != 0;
    for (uint32_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint32_t digit = (uint32_t)(unsigned char)text[i] - '0';
        valid = digit <= 9 && digit <= maximum && value <= (maximum - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    valid = valid && value >= minimum;
    if (valid)
    {
        *result = value;
    }
    return valid;
}

static bool c_lowering_benchmark_options(int argc, char** argv, CLoweringBenchmarkOptions* options)
{
    *options = (CLoweringBenchmarkOptions){
        .shape = BUSTER_C_LOWERING_BENCHMARK_SHAPES,
        .only = BUSTER_C_LOWERING_BENCHMARK_PATHS,
    };
    bool valid = true;
    uint32_t seen = 0;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const char* const option = argv[i];
        valid = i + 1 < argc;
        if (valid)
        {
            const char* const value = argv[++i];
            uint32_t bit = 0;
            if (strcmp(option, "--definitions") == 0)
            {
                bit = 1;
                valid = c_lowering_benchmark_number(value, 1, BUSTER_C_LOWERING_BENCHMARK_MAX_DEFINITIONS, &options->definitions);
            }
            else if (strcmp(option, "--after-scalar") == 0)
            {
                bit = 2;
                valid = c_lowering_benchmark_number(value, 0, 1000, &options->after_scalar);
            }
            else if (strcmp(option, "--before-vector") == 0)
            {
                bit = 16;
                valid = c_lowering_benchmark_number(value, 0, 1000, &options->before_vector);
            }
            else if (strcmp(option, "--shape") == 0)
            {
                bit = 4;
                bool found = strcmp(value, "all") == 0;
                for (uint32_t shape = 0; shape < BUSTER_C_LOWERING_BENCHMARK_SHAPES; shape += 1)
                {
                    if (strcmp(value, c_lowering_benchmark_shapes[shape]) == 0)
                    {
                        options->shape = shape;
                        found = true;
                    }
                }
                valid = found;
            }
            else if (strcmp(option, "--only") == 0)
            {
                bit = 8;
                bool found = false;
                for (uint32_t path = 0; path < BUSTER_C_LOWERING_BENCHMARK_PATHS; path += 1)
                {
                    if (strcmp(value, c_lowering_benchmark_paths[path]) == 0)
                    {
                        options->only = path;
                        found = true;
                    }
                }
                valid = found;
            }
            else
            {
                valid = false;
            }
            valid = valid && (seen & bit) == 0;
            seen |= bit;
        }
    }
    valid = valid && (seen & (2 | 16)) != (2 | 16);
    return valid;
}

static uint32_t c_lowering_benchmark_reference_count(uint32_t shape, uint32_t count)
{
    const bool tree = shape == C_LOWERING_BENCHMARK_FANOUT || shape == C_LOWERING_BENCHMARK_CHAIN || shape == C_LOWERING_BENCHMARK_LARGE;
    const uint32_t evaluated = tree ? count - 1 : shape == C_LOWERING_BENCHMARK_DUPLICATE ? count * BUSTER_C_LOWERING_BENCHMARK_DUPLICATES : count;
    return evaluated + (count + 15) / 16 + (count + 31) / 32;
}

static bool c_lowering_benchmark_generate(Arena* arena, uint32_t shape, uint32_t count, CLoweringInput* input)
{
    const uint32_t reference_count = c_lowering_benchmark_reference_count(shape, count);
    uint32_t* flags = arena_push_array(arena, count, sizeof(uint32_t), 64);
    uint32_t* sources = arena_push_array(arena, reference_count, sizeof(uint32_t), 64);
    uint32_t* targets = arena_push_array(arena, reference_count, sizeof(uint32_t), 64);
    uint32_t* kinds = arena_push_array(arena, reference_count, sizeof(uint32_t), 64);
    bool valid = flags != 0 && sources != 0 && targets != 0 && kinds != 0;
    uint32_t reference = 0;
    const uint32_t small_live = count < 32 ? count : 32;
    const uint32_t duplicate_live = count < 8 ? 1 : count / 8;
    for (uint32_t row = 0; valid && row < count; row += 1)
    {
        // Reverse source order makes preparation exercise its unsorted-edge handoff, not an already-built CSR.
        const uint32_t source = count - 1 - row;
        flags[source] = source % 8 == 7 ? BUSTER_C_LOWERING_OBJECT : BUSTER_C_LOWERING_FUNCTION;
        flags[source] |= source == 0 ? BUSTER_C_LOWERING_EXTERNAL : 0;
        uint32_t degree = 0;
        uint32_t target = 0;
        uint32_t stride = 0;
        if (shape == C_LOWERING_BENCHMARK_INTRINSIC || shape == C_LOWERING_BENCHMARK_DUPLICATE)
        {
            const uint32_t live = shape == C_LOWERING_BENCHMARK_INTRINSIC ? small_live : duplicate_live;
            target = source < live ? (source + 1) % live : live + (source - live + 1) % (count - live);
            degree = shape == C_LOWERING_BENCHMARK_INTRINSIC ? 1 : BUSTER_C_LOWERING_BENCHMARK_DUPLICATES;
        }
        else if (shape == C_LOWERING_BENCHMARK_FANOUT)
        {
            degree = source == 0 ? count - 1 : 0;
            target = 1;
            stride = 1;
        }
        else if (shape == C_LOWERING_BENCHMARK_CHAIN)
        {
            degree = source + 1 < count ? 1 : 0;
            target = source + 1;
        }
        else if (shape == C_LOWERING_BENCHMARK_CYCLE)
        {
            degree = 1;
            target = (source + 1) % count;
        }
        else
        {
            target = source * 2 + 1;
            degree = target < count ? (count - target < 2 ? count - target : 2) : 0;
            stride = 1;
        }
        for (uint32_t edge = 0; edge < degree; edge += 1)
        {
            sources[reference] = source;
            targets[reference] = target + edge * stride;
            kinds[reference++] = C_LOWERING_REFERENCE_EVALUATED;
        }
        if (source % 16 == 0)
        {
            sources[reference] = source;
            targets[reference] = (source + count / 2 + 1) % count;
            kinds[reference++] = C_LOWERING_REFERENCE_UNEVALUATED;
        }
        if (source % 32 == 0)
        {
            sources[reference] = source;
            targets[reference] = BUSTER_C_LOWERING_NONE;
            kinds[reference++] = C_LOWERING_REFERENCE_EXTERNAL;
        }
    }
    valid = valid && reference == reference_count;
    if (valid)
    {
        *input = (CLoweringInput){
            .definition_count = count,
            .reference_count = reference_count,
            .definition_flags = flags,
            .reference_source = sources,
            .reference_target = targets,
            .reference_kind = kinds,
        };
    }
    return valid;
}

// This oracle uses the raw input and linked adjacency, not the planner's CSR, root mask or bitset helpers. A queue
// visits each reachable source once; even a million-definition chain does not rescan all definitions per round.
static bool c_lowering_benchmark_expected(Arena* arena, CLoweringInput input, CLoweringBenchmarkExpected* expected)
{
    const uint32_t count = input.definition_count;
    const uint64_t words = ((uint64_t)count + 63) / 64;
    expected->reached = arena_push_array(arena, count, sizeof(uint8_t), 64);
    expected->bits = arena_push_array(arena, words, sizeof(uint64_t), 64);
    expected->selected = arena_push_array(arena, count, sizeof(uint32_t), 64);
    expected->heads = arena_push_array(arena, count, sizeof(uint32_t), 64);
    expected->next = arena_push_array(arena, input.reference_count, sizeof(uint32_t), 64);
    uint32_t* queue = arena_push_array(arena, count, sizeof(uint32_t), 64);
    bool valid = expected->reached != 0 && expected->bits != 0 && expected->selected != 0 && expected->heads != 0 && expected->next != 0 && queue != 0;
    if (valid)
    {
        memset(expected->bits, 0, words * sizeof(uint64_t));
    }
    uint32_t queued = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        expected->reached[i] = 0;
        expected->heads[i] = BUSTER_C_LOWERING_NONE;
        const uint32_t flags = input.definition_flags[i];
        const bool root = (flags & (BUSTER_C_LOWERING_EXTERNAL | BUSTER_C_LOWERING_USED | BUSTER_C_LOWERING_CONSTRUCTOR |
                                  BUSTER_C_LOWERING_DESTRUCTOR | BUSTER_C_LOWERING_ALIAS_TARGET)) != 0;
        if (root && (flags & BUSTER_C_LOWERING_INLINE_ONLY) == 0)
        {
            expected->reached[i] = 1;
            queue[queued++] = i;
        }
    }
    expected->root_count = queued;
    for (uint32_t edge = 0; valid && edge < input.reference_count; edge += 1)
    {
        if (input.reference_kind[edge] == C_LOWERING_REFERENCE_EVALUATED)
        {
            const uint32_t source = input.reference_source[edge];
            const uint32_t target = input.reference_target[edge];
            valid = source < count && target < count;
            if (valid && ((input.definition_flags[source] | input.definition_flags[target]) & BUSTER_C_LOWERING_INLINE_ONLY) == 0)
            {
                expected->next[edge] = expected->heads[source];
                expected->heads[source] = edge;
                expected->edge_count += 1;
            }
        }
    }
    uint32_t position = 0;
    while (valid && position < queued)
    {
        const uint32_t frontier_end = queued;
        expected->rounds += 1;
        while (valid && position < frontier_end)
        {
            const uint32_t source = queue[position++];
            for (uint32_t edge = expected->heads[source]; valid && edge != BUSTER_C_LOWERING_NONE; edge = expected->next[edge])
            {
                const uint32_t target = input.reference_target[edge];
                expected->visited_edges += 1;
                if (expected->reached[target] == 0)
                {
                    valid = queued < count;
                    if (valid)
                    {
                        expected->reached[target] = 1;
                        queue[queued++] = target;
                    }
                }
            }
        }
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        if (expected->reached[i] != 0)
        {
            expected->bits[i / 64] |= UINT64_C(1) << (i % 64);
            expected->selected[expected->selected_count++] = i;
            expected->functions += (input.definition_flags[i] & BUSTER_C_LOWERING_FUNCTION) != 0;
            expected->objects += (input.definition_flags[i] & BUSTER_C_LOWERING_OBJECT) != 0;
        }
    }
    return valid;
}

static bool c_lowering_benchmark_matches(const CLowering* plan, CLoweringInput input, const CLoweringBenchmarkExpected* expected, bool check_graph)
{
    const uint64_t words = ((uint64_t)input.definition_count + 63) / 64;
    bool valid = plan->definition_count == input.definition_count && plan->reference_count == input.reference_count &&
                 plan->edge_count == expected->edge_count && plan->root_count == expected->root_count && plan->word_count == words &&
                 plan->selected_count == expected->selected_count && plan->statistics.selected_functions == expected->functions &&
                 plan->statistics.selected_objects == expected->objects && plan->statistics.visited_edges == expected->visited_edges &&
                 plan->statistics.rounds == expected->rounds;
    valid = valid && memcmp(plan->selected, expected->selected, (uint64_t)expected->selected_count * sizeof(uint32_t)) == 0 &&
            memcmp(plan->reached, expected->bits, words * sizeof(uint64_t)) == 0;
    if (valid && check_graph)
    {
        valid = memcmp(plan->definition_flags, input.definition_flags, (uint64_t)input.definition_count * sizeof(uint32_t)) == 0 &&
                plan->edge_offsets[0] == 0 && plan->edge_offsets[input.definition_count] == expected->edge_count;
        for (uint32_t source = 0; valid && source < input.definition_count; source += 1)
        {
            const uint32_t begin = plan->edge_offsets[source];
            uint32_t end = plan->edge_offsets[source + 1];
            valid = begin <= end && end <= plan->edge_count;
            uint32_t reference = expected->heads[source];
            while (valid && end != begin && reference != BUSTER_C_LOWERING_NONE)
            {
                // The linked list reverses raw order, so check the stable CSR range backwards.
                valid = plan->edge_targets[--end] == input.reference_target[reference];
                reference = expected->next[reference];
            }
            valid = valid && end == begin && reference == BUSTER_C_LOWERING_NONE;
        }
    }
    return valid;
}

static bool c_lowering_benchmark_condition(CLowering* plan, bool vector, uint32_t milliseconds)
{
    const uint64_t begin = c_lowering_benchmark_now();
    uint64_t now = begin;
    bool valid = begin != 0;
    while (valid && now - begin < (uint64_t)milliseconds * 1000000)
    {
        if (vector)
        {
            valid = c_lowering_vector(plan, C_LOWERING_REACHABLE) == C_LOWERING_OK;
        }
        else
        {
            valid = c_lowering_scalar(plan, C_LOWERING_REACHABLE) == C_LOWERING_OK;
        }
        __asm__ volatile("" : : "r"(plan->selected) : "memory");
        now = c_lowering_benchmark_now();
        valid = valid && now >= begin;
    }
    return valid;
}

static void c_lowering_benchmark_print(uint32_t path, const CLoweringBenchmarkSample* samples, const CLoweringStatistics* statistics, uint32_t count)
{
    bool cycles_available = true;
    bool instructions_available = true;
    for (uint32_t i = 0; i < BUSTER_C_LOWERING_BENCHMARK_PASSES; i += 1)
    {
        cycles_available &= samples[i].cycles_available;
        instructions_available &= samples[i].instructions_available;
    }
    const uint64_t nanoseconds = c_lowering_benchmark_median(samples, 0);
    printf("  %s: %" PRIu64 " ns/run (%.3f ns/definition)", c_lowering_benchmark_paths[path], nanoseconds, (double)nanoseconds / count);
    if (cycles_available)
    {
        printf(", %" PRIu64 " cycles/run", c_lowering_benchmark_median(samples, 1));
    }
    else
    {
        printf(", cycles unavailable");
    }
    if (instructions_available)
    {
        printf(", %" PRIu64 " instructions/run", c_lowering_benchmark_median(samples, 2));
    }
    else
    {
        printf(", instructions unavailable");
    }
    printf("; visited-edges %" PRIu64 ", rounds %" PRIu64 ", vector-batches %" PRIu64 "\n", statistics->visited_edges, statistics->rounds, statistics->vector_batches);
}

static bool c_lowering_benchmark_case(uint32_t shape, CLoweringBenchmarkOptions options, CLoweringBenchmarkCounters counters)
{
    const uint32_t count = options.definitions != 0 ? options.definitions : c_lowering_benchmark_sizes[shape];
    const uint32_t references = c_lowering_benchmark_reference_count(shape, count);
    const uint32_t conditioning_ms = options.before_vector != 0 ? options.before_vector : options.after_scalar;
    const uint64_t input_capacity = (uint64_t)count * 32 + (uint64_t)references * 16 + 4096;
    const uint64_t prepared_capacity = ((uint64_t)count + references + 16) * 64;
    Arena input_arena = arena_reserve(input_capacity);
    Arena prepared_arena = arena_reserve(prepared_capacity);
    Arena conditioning_arena = { 0 };
    CLoweringInput input = { 0 };
    CLoweringBenchmarkExpected expected = { 0 };
    CLowering plan = { 0 };
    CLowering conditioning = { 0 };
    bool valid = c_lowering_benchmark_generate(&input_arena, shape, count, &input) &&
                 c_lowering_benchmark_expected(&input_arena, input, &expected) &&
                 c_lowering_prepare(&prepared_arena, input, &plan) == C_LOWERING_OK &&
                 c_lowering_scalar(&plan, C_LOWERING_REACHABLE) == C_LOWERING_OK &&
                 c_lowering_benchmark_matches(&plan, input, &expected, true);
    if (valid && conditioning_ms != 0 && options.only != C_LOWERING_BENCHMARK_SCALAR)
    {
        conditioning_arena = arena_reserve(prepared_capacity);
        valid = c_lowering_prepare(&conditioning_arena, input, &conditioning) == C_LOWERING_OK &&
                c_lowering_scalar(&conditioning, C_LOWERING_REACHABLE) == C_LOWERING_OK &&
                c_lowering_benchmark_matches(&conditioning, input, &expected, true);
    }
    CLoweringBenchmarkSample samples[BUSTER_C_LOWERING_BENCHMARK_PATHS][BUSTER_C_LOWERING_BENCHMARK_PASSES] = { 0 };
    CLoweringStatistics statistics[BUSTER_C_LOWERING_BENCHMARK_PATHS] = { 0 };
    for (uint32_t pass = 0; valid && pass <= BUSTER_C_LOWERING_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t step = 0; valid && step < BUSTER_C_LOWERING_BENCHMARK_PATHS; step += 1)
        {
            const uint32_t path = (pass + step) % BUSTER_C_LOWERING_BENCHMARK_PATHS;
            if (options.only == BUSTER_C_LOWERING_BENCHMARK_PATHS || options.only == path)
            {
                const bool conditioned = conditioning_ms != 0 && path != C_LOWERING_BENCHMARK_SCALAR;
                if (conditioned)
                {
                    valid = c_lowering_benchmark_condition(&conditioning, options.before_vector != 0, conditioning_ms);
                }
                if (valid)
                {
                    Arena sample_arena = { 0 };
                    CLowering sample_plan = { 0 };
                    CLowering* measured = path == C_LOWERING_BENCHMARK_PREPARE ? &sample_plan : &plan;
                    CLoweringBenchmarkSample sample = { 0 };
                    const bool counters_started = c_lowering_benchmark_counter_begin(counters);
                    const uint64_t begin = c_lowering_benchmark_now();
                    if (path == C_LOWERING_BENCHMARK_PREPARE)
                    {
                        sample_arena = arena_reserve(prepared_capacity);
                        valid = c_lowering_prepare(&sample_arena, input, measured) == C_LOWERING_OK &&
                                c_lowering_vector(measured, C_LOWERING_REACHABLE) == C_LOWERING_OK;
                    }
                    else if (path == C_LOWERING_BENCHMARK_VECTOR)
                    {
                        valid = c_lowering_vector(measured, C_LOWERING_REACHABLE) == C_LOWERING_OK;
                    }
                    else
                    {
                        valid = c_lowering_scalar(measured, C_LOWERING_REACHABLE) == C_LOWERING_OK;
                    }
                    const uint64_t end = c_lowering_benchmark_now();
                    c_lowering_benchmark_counter_end(counters, counters_started, &sample);
                    sample.nanoseconds = end - begin;
                    valid = valid && begin != 0 && end >= begin &&
                            c_lowering_benchmark_matches(measured, input, &expected, path == C_LOWERING_BENCHMARK_PREPARE);
                    if (valid && conditioned)
                    {
                        // Separate ownership preserves the final conditioning result without copying or checking
                        // it between the last conditioning call and the timed operation.
                        valid = c_lowering_benchmark_matches(&conditioning, input, &expected, false);
                    }
                    if (valid && pass != 0)
                    {
                        samples[path][pass - 1] = sample;
                        statistics[path] = measured->statistics;
                    }
                    arena_release(&sample_arena);
                }
            }
        }
    }
    if (valid)
    {
        uint32_t functions = 0;
        uint32_t objects = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            functions += (input.definition_flags[i] & BUSTER_C_LOWERING_FUNCTION) != 0;
            objects += (input.definition_flags[i] & BUSTER_C_LOWERING_OBJECT) != 0;
        }
        printf("lowering synthetic %s: %u definitions, %u references, %u evaluated edges, %u roots\n", c_lowering_benchmark_shapes[shape], count,
               input.reference_count, expected.edge_count, expected.root_count);
        printf("  functions selected %" PRIu64 ", skipped %" PRIu64 "; objects selected %" PRIu64 ", skipped %" PRIu64 "\n",
               expected.functions, functions - expected.functions, expected.objects, objects - expected.objects);
        printf("  workspace %" PRIu64 " bytes, including %" PRIu64 " scratch/output bytes (alignment included)\n", plan.workspace_bytes, plan.scratch_bytes);
        if (conditioning_ms != 0 && options.only != C_LOWERING_BENCHMARK_SCALAR)
        {
            printf("  additional untimed conditioning workspace %" PRIu64 " bytes\n", conditioning.workspace_bytes);
        }
        for (uint32_t path = 0; path < BUSTER_C_LOWERING_BENCHMARK_PATHS; path += 1)
        {
            if (options.only == BUSTER_C_LOWERING_BENCHMARK_PATHS || options.only == path)
            {
                c_lowering_benchmark_print(path, samples[path], &statistics[path], count);
            }
        }
    }
    else
    {
        fprintf(stderr, "benchmark-lowering: %s failed (allocation, clock, preparation or independent-output mismatch)\n", c_lowering_benchmark_shapes[shape]);
    }
    arena_release(&conditioning_arena);
    arena_release(&prepared_arena);
    arena_release(&input_arena);
    return valid;
}

static int run_c_lowering_benchmark(int argc, char** argv)
{
    const bool help = argc == 1 && strcmp(argv[0], "--help") == 0;
    CLoweringBenchmarkOptions options = { 0 };
    bool valid = help || c_lowering_benchmark_options(argc, argv, &options);
    if (help || !valid)
    {
        FILE* const output = help ? stdout : stderr;
        fprintf(output, "usage: benchmark-lowering [--shape all|intrinsic|fanout|chain|cycle|duplicates|large] [--definitions N] [--only vector|scalar|prepare] [--before-vector MS | --after-scalar MS]\n");
        fprintf(output, "  N: 1..%u; MS: 0..1000; duplicate options and combining conditioning flags are rejected\n", BUSTER_C_LOWERING_BENCHMARK_MAX_DEFINITIONS);
        fprintf(output, "  default: unconditioned single-selection samples; conditioning applies to vector and prepare only\n");
    }
    else
    {
        const CLoweringBenchmarkCounters counters = c_lowering_benchmark_counters();
        printf("benchmark-lowering: synthetic graphs only; medians of %u rotating-order single-operation samples after one warm-up\n", BUSTER_C_LOWERING_BENCHMARK_PASSES);
        if ((options.before_vector != 0 || options.after_scalar != 0) && options.only != C_LOWERING_BENCHMARK_SCALAR)
        {
            printf("  vector/prepare conditioning: %s %u ms on a separate prepared plan immediately before measurement\n",
                   options.before_vector != 0 ? "before-vector" : "after-scalar", options.before_vector != 0 ? options.before_vector : options.after_scalar);
        }
        else
        {
            printf("  unconditioned single-selection samples; no sustained vector state is established\n");
        }
        printf("  vector/scalar: reusable graph selection; prepare: reserve + first-touch graph preparation/handoff + vector selection\n");
        printf("  oracle, result checks and arena teardown excluded; no body checking, body lowering or main.c savings measured\n");
        printf("  no prepare+scalar comparison: no whole-planner vector speedup can be inferred\n");
        printf("  measured and final conditioning outputs checked after timing; perf counts include clock reads and exclude kernel/hypervisor work\n");
        for (uint32_t shape = 0; valid && shape < BUSTER_C_LOWERING_BENCHMARK_SHAPES; shape += 1)
        {
            if (options.shape == BUSTER_C_LOWERING_BENCHMARK_SHAPES || options.shape == shape)
            {
                valid = c_lowering_benchmark_case(shape, options, counters);
            }
        }
        if (counters.instructions >= 0)
        {
            close(counters.instructions);
        }
        if (counters.cycles >= 0)
        {
            close(counters.cycles);
        }
    }
    return !valid;
}
