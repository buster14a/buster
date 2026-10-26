// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The backend's measurement (#136), which its two commands share: benchmark-backend's generated programs
// (tier0_benchmark.c, a translation unit of its own) and benchmark-backend-source's C files (tier0_source_benchmark.c,
// in main.c's checker unit). A corpus of Tier 0's functions, each compiled by both paths, which must give the same
// records, then timed: the vector path, its scalar twin, branch sizing and encoding, each a run over every function.
// Include after x86_64_encoder.c, branch_sizing.c and tier0.c.
//
// Layout: the clock and the counters; the corpus (tier0_benchmark_reserve, tier0_benchmark_add); the variants
// (tier0_benchmark_run); tier0_benchmark_measure.
#ifndef BUSTER_TIER0_BENCHMARK_MEASURE_INCLUDED
#define BUSTER_TIER0_BENCHMARK_MEASURE_INCLUDED

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

#define BUSTER_TIER0_BENCHMARK_PASSES 21
#define BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS (1u << 20)
#define BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT 0x1000008a0ull

#define BUSTER_TIER0_BENCHMARK_VECTOR 0
#define BUSTER_TIER0_BENCHMARK_SCALAR 1
#define BUSTER_TIER0_BENCHMARK_SIZING 2
#define BUSTER_TIER0_BENCHMARK_ENCODE 3
#define BUSTER_TIER0_BENCHMARK_VARIANTS 4

static const char* const tier0_benchmark_names[BUSTER_TIER0_BENCHMARK_VARIANTS] = {
    "vector",
    "scalar",
    "sizing",
    "encode",
};

// ---------------------------------------------------------------------------------------------------------------------
// The clock and the counters

static uint64_t tier0_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

typedef struct Tier0BenchmarkCounters Tier0BenchmarkCounters;
struct Tier0BenchmarkCounters
{
    int cycles;
    int instructions;
    int throttle;
};

static int tier0_benchmark_counter(uint32_t type, uint64_t config)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = type;
    attributes.config = config;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t tier0_benchmark_count(int counter)
{
    uint64_t result = 0;
    if (counter < 0 || read(counter, &result, sizeof(result)) != (ssize_t)sizeof(result))
    {
        result = 0;
    }
    return result;
}

typedef struct Tier0BenchmarkReading Tier0BenchmarkReading;
struct Tier0BenchmarkReading
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t throttle;
};

static Tier0BenchmarkReading tier0_benchmark_read(Tier0BenchmarkCounters counters)
{
    Tier0BenchmarkReading reading;
    reading.cycles = tier0_benchmark_count(counters.cycles);
    reading.instructions = tier0_benchmark_count(counters.instructions);
    reading.throttle = tier0_benchmark_count(counters.throttle);
    reading.nanoseconds = tier0_benchmark_now();
    return reading;
}

static double tier0_benchmark_median(const double* values)
{
    double result = 0;
    for (uint32_t i = 0; i < BUSTER_TIER0_BENCHMARK_PASSES; i += 1)
    {
        uint32_t less = 0;
        uint32_t equal = 0;
        for (uint32_t j = 0; j < BUSTER_TIER0_BENCHMARK_PASSES; j += 1)
        {
            less += values[j] < values[i] ? 1 : 0;
            equal += values[j] == values[i] ? 1 : 0;
        }
        if (less <= BUSTER_TIER0_BENCHMARK_PASSES / 2 && less + equal > BUSTER_TIER0_BENCHMARK_PASSES / 2)
        {
            result = values[i];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The corpus

// The functions measured, with each one's records as the vector path makes them (for sizing), its branch columns, and
// room for its sized records and its machine code.
typedef struct Tier0BenchmarkCorpus Tier0BenchmarkCorpus;
struct Tier0BenchmarkCorpus
{
    const Tier0Function** functions;
    Tier0Code* codes;
    X86Instruction** sized;
    uint32_t** offsets;
    uint32_t** ends;
    uint8_t** machine;
    uint32_t count;
    uint64_t entries;
    uint64_t records;
    uint64_t deferred;
    uint64_t bytes;
};

// An empty corpus with room for BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS functions, from `arena`: false if it has none.
static bool tier0_benchmark_reserve(Tier0BenchmarkCorpus* corpus, Arena* arena)
{
    memset(corpus, 0, sizeof(*corpus));
    corpus->functions = BUSTER_ARENA_PUSH_ARRAY(arena, const Tier0Function*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->codes = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Code, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->sized = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->machine = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    return corpus->functions != 0 && corpus->codes != 0 && corpus->sized != 0 && corpus->offsets != 0 && corpus->ends != 0 && corpus->machine != 0;
}

// `function`, compiled by both paths into `code_arena`, joins the corpus with room for its sized records and machine
// code from `arena`, unless Tier 0 refuses it (by both paths alike), which leaves it out. Returns false if the paths
// disagree on it (`*same` false), memory runs out or the corpus is full.
static bool tier0_benchmark_add(Tier0BenchmarkCorpus* corpus, const Tier0Function* function, Arena* arena, Arena* code_arena, bool* same)
{
    const Tier0Code scalar = tier0_compile_scalar(function, code_arena);
    const Tier0Code vector = tier0_compile(function, code_arena);
    *same = scalar.error == vector.error && scalar.count == vector.count && scalar.branch_count == vector.branch_count &&
            memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0;
    bool result = *same;
    if (result && vector.error == TIER0_OK)
    {
        const uint32_t index = corpus->count;
        result = index < BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS;
        corpus->sized[index] = result ? BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, (uint64_t)vector.count + 1) : 0;
        corpus->offsets[index] = result ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)vector.count + 1) : 0;
        corpus->ends[index] = result ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(vector.reference_count)) : 0;
        corpus->machine[index] = result ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_X86_ENCODER_CAPACITY(vector.count)) : 0;
        result = result && corpus->sized[index] != 0 && corpus->offsets[index] != 0 && corpus->ends[index] != 0 && corpus->machine[index] != 0;
        if (result)
        {
            corpus->functions[index] = function;
            corpus->codes[index] = vector;
            corpus->count += 1;
            corpus->entries += function->count;
            corpus->records += vector.count;
            corpus->deferred += vector.deferred;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The variants

// One run of a variant over every function: compiling (into `scratch`, put back after each), sizing (each function's
// records copied fresh before the timing) or encoding (the sized records).
static Tier0BenchmarkReading tier0_benchmark_run(uint32_t variant, Tier0BenchmarkCorpus* corpus, Arena* scratch, Tier0BenchmarkCounters counters)
{
    if (variant == BUSTER_TIER0_BENCHMARK_SIZING)
    {
        for (uint32_t f = 0; f < corpus->count; f += 1)
        {
            memcpy(corpus->sized[f], corpus->codes[f].records, (uint64_t)corpus->codes[f].count * sizeof(X86Instruction));
        }
    }
    const Tier0BenchmarkReading begin = tier0_benchmark_read(counters);
    uint64_t bytes = 0;
    const uint64_t mark = scratch->length;
    for (uint32_t f = 0; f < corpus->count; f += 1)
    {
        const Tier0Code* code = &corpus->codes[f];
        if (variant == BUSTER_TIER0_BENCHMARK_VECTOR)
        {
            (void)tier0_compile(corpus->functions[f], scratch);
            arena_pop_to(scratch, mark);
        }
        else if (variant == BUSTER_TIER0_BENCHMARK_SCALAR)
        {
            (void)tier0_compile_scalar(corpus->functions[f], scratch);
            arena_pop_to(scratch, mark);
        }
        else if (variant == BUSTER_TIER0_BENCHMARK_SIZING)
        {
            (void)size_branches(corpus->sized[f], code->count, code->branches, code->targets, code->branch_count, corpus->offsets[f]);
        }
        else
        {
            const int64_t length = x86_encode(corpus->sized[f], code->count, corpus->machine[f], BUSTER_X86_ENCODER_CAPACITY(code->count), 0, corpus->ends[f]);
            bytes += length > 0 ? (uint64_t)length : 0;
        }
    }
    const Tier0BenchmarkReading end = tier0_benchmark_read(counters);
    Tier0BenchmarkReading taken = {
        .nanoseconds = end.nanoseconds - begin.nanoseconds,
        .cycles = end.cycles - begin.cycles,
        .instructions = end.instructions - begin.instructions,
        .throttle = end.throttle - begin.throttle,
    };
    corpus->bytes = variant == BUSTER_TIER0_BENCHMARK_ENCODE ? bytes : corpus->bytes;
    return taken;
}

// Times the variants over the corpus, each after a warm-up, in BUSTER_TIER0_BENCHMARK_PASSES passes that alternate which
// compile path runs first (sizing before encoding, which takes the sized records), and prints the report under `name`:
// the corpus's totals, then each variant's medians per typed entry and per record. `only`, unless it is
// BUSTER_TIER0_BENCHMARK_VARIANTS, times one variant alone (encoding's sizing untimed), back to back; `after_scalar`
// runs the scalar path, untimed, for that many ms before each pass of the vector path.
static void tier0_benchmark_measure(Tier0BenchmarkCorpus* corpus, Arena* scratch, uint32_t only, uint64_t after_scalar, const char* name)
{
    const Tier0BenchmarkCounters counters = {
        .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
        .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
        .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
    };
    // A warm-up of each variant (sizing before encoding, which takes the sized records).
    for (uint32_t v = 0; v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
    {
        (void)tier0_benchmark_run(v, corpus, scratch, counters);
    }
    double values[BUSTER_TIER0_BENCHMARK_VARIANTS][4][BUSTER_TIER0_BENCHMARK_PASSES];
    memset(values, 0, sizeof(values));
    for (uint32_t pass = 0; pass < BUSTER_TIER0_BENCHMARK_PASSES; pass += 1)
    {
        for (uint32_t k = 0; k < BUSTER_TIER0_BENCHMARK_VARIANTS; k += 1)
        {
            // The compile paths alternate which runs first; encoding always follows sizing.
            const uint32_t v = k < 2 ? ((pass & 1) != 0 ? 1 - k : k) : k;
            if (only != BUSTER_TIER0_BENCHMARK_VARIANTS && v != only && !(only == BUSTER_TIER0_BENCHMARK_ENCODE && v == BUSTER_TIER0_BENCHMARK_SIZING))
            {
                continue;
            }
            if (v == BUSTER_TIER0_BENCHMARK_VECTOR && after_scalar != 0)
            {
                const uint64_t until = tier0_benchmark_now() + after_scalar * 1000000ull;
                while (tier0_benchmark_now() < until)
                {
                    (void)tier0_benchmark_run(BUSTER_TIER0_BENCHMARK_SCALAR, corpus, scratch, counters);
                }
            }
            const Tier0BenchmarkReading taken = tier0_benchmark_run(v, corpus, scratch, counters);
            values[v][0][pass] = (double)taken.nanoseconds;
            values[v][1][pass] = (double)taken.cycles;
            values[v][2][pass] = (double)taken.instructions;
            values[v][3][pass] = (double)taken.throttle;
        }
    }
    printf("%s: %u functions, %" PRIu64 " typed entries, %" PRIu64 " records (%.3f per entry), %" PRIu64 " bytes; %" PRIu64 " entries deferred (%.2f%%)\n", name,
           corpus->count, corpus->entries, corpus->records, (double)corpus->records / (double)corpus->entries, corpus->bytes, corpus->deferred,
           100.0 * (double)corpus->deferred / (double)corpus->entries);
    printf("%-8s %12s %12s %12s %12s %12s %8s %12s\n", "variant", "ns/entry", "cyc/entry", "ins/entry", "ns/record", "cyc/record", "IPC", "thr/record");
    for (uint32_t v = 0; v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
    {
        if (only != BUSTER_TIER0_BENCHMARK_VARIANTS && v != only)
        {
            continue;
        }
        const double ns = tier0_benchmark_median(values[v][0]);
        const double cycles = tier0_benchmark_median(values[v][1]);
        const double instructions = tier0_benchmark_median(values[v][2]);
        const double throttle = tier0_benchmark_median(values[v][3]);
        const double e = (double)corpus->entries;
        const double r = (double)corpus->records;
        printf("%-8s %12.2f %12.2f %12.2f %12.2f %12.2f %8.2f %12.3f\n", tier0_benchmark_names[v], ns / e, cycles / e, instructions / e, ns / r, cycles / r,
               cycles != 0 ? instructions / cycles : 0.0, throttle / r);
    }
    const int descriptors[] = { counters.cycles, counters.instructions, counters.throttle };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); i += 1)
    {
        if (descriptors[i] >= 0)
        {
            close(descriptors[i]);
        }
    }
}

#endif
