// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The backend's benchmark (benchmark-backend, #136): Tier 0's vector path and its scalar twin on the functions of
// generated programs (tier0_generate.c), apart from branch sizing and encoding, which it measures on their records after.
// docs/tier0.md has the numbers.
//
// Usage: build/compiler benchmark-backend [--entries N] [--seed N] [--only vector|scalar|sizing|encode]
//            [--after-scalar MS]
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...). It generates programs until their functions
// hold --entries typed entries (262,144 by default, more than the branch predictor learns from pass to pass), checks
// that both paths give every function the same code, then reports the median of BUSTER_TIER0_BENCHMARK_PASSES passes
// that alternate which variant runs first, each a run over every function: nanoseconds, cycles and instructions per
// typed entry and per record (the counters read zero where perf_event_open refuses), IPC and the throttle (PMCx1A0's
// umask 0x08, #199); then the entries the vector path defers and the records' and bytes' totals. --only times one
// variant alone, back to back; --after-scalar MS runs the scalar path, untimed, for MS ms before each pass of the
// vector path (#199's state after lighter phases: 100 to 300). The exit status is 1 on invalid arguments or if the paths
// disagree.
//
// Layout: the clock and the counters; the corpus (tier0_benchmark_corpus); the variants (tier0_benchmark_run);
// run_backend_benchmark.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>

// OS-dependent includes
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

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

// The encoder's validation helpers this file doesn't call.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#pragma clang diagnostic pop
#include "buster/compiler/lane.c"
#include "buster/compiler/link.c"
#include "buster/compiler/x86_semantics.c"
#include "buster/compiler/c_types.c"
#include "buster/compiler/calling_convention.c"
#include "buster/compiler/branch_sizing.c"
#include "buster/compiler/tier0.c"
#include "buster/compiler/tier0_generate.c"

#define BUSTER_TIER0_BENCHMARK_PASSES 21
#define BUSTER_TIER0_BENCHMARK_ENTRIES 262144u
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

// The functions of the generated programs, with each one's records as the vector path makes them (for sizing), its
// branch columns, and room for its sized records and its machine code.
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

// Programs of a shape like compiled C's (calls, loops, floating values and pointers), until the corpus holds `entries`
// entries. Returns false if the paths disagree on a function or memory runs out.
static bool tier0_benchmark_corpus(Tier0BenchmarkCorpus* corpus, Tier0Tables* tables, Arena* arena, Arena* code_arena, uint64_t seed, uint64_t entries)
{
    const Tier0ProgramOptions options = {
        .functions = 8,
        .statements = 24,
        .expression_nodes = 12,
        .calls = true,
        .loops = true,
        .floating = true,
        .pointers = true,
        .defined = false,
    };
    corpus->functions = BUSTER_ARENA_PUSH_ARRAY(arena, const Tier0Function*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->codes = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Code, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->sized = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    corpus->machine = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t*, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    bool result = corpus->functions != 0 && corpus->codes != 0 && corpus->sized != 0 && corpus->offsets != 0 && corpus->ends != 0 && corpus->machine != 0;
    for (uint64_t p = 0; result && corpus->entries < entries; p += 1)
    {
        Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Program, 1);
        Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Functions, 1);
        result = program != 0 && functions != 0 && tier0_generate_program(program, arena, seed + p, options, "c_") &&
                 tier0_program_functions(program, tables, arena, functions);
        for (uint32_t f = 0; result && f < functions->count && corpus->count < BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS; f += 1)
        {
            const Tier0Function* function = &functions->functions[f];
            const uint64_t mark = code_arena->length;
            const Tier0Code scalar = tier0_compile_scalar(function, code_arena);
            const Tier0Code vector = tier0_compile(function, code_arena);
            bool same = scalar.error == vector.error && scalar.count == vector.count && scalar.branch_count == vector.branch_count &&
                        memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0;
            if (!same)
            {
                fprintf(stderr, "benchmark-backend: the paths disagree on program %" PRIu64 "'s function %u\n", seed + p, f);
                result = false;
            }
            else if (vector.error == TIER0_OK)
            {
                const uint32_t index = corpus->count;
                corpus->functions[index] = function;
                corpus->codes[index] = vector;
                corpus->sized[index] = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, (uint64_t)vector.count + 1);
                corpus->offsets[index] = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)vector.count + 1);
                corpus->ends[index] = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(vector.reference_count));
                corpus->machine[index] = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_X86_ENCODER_CAPACITY(vector.count));
                result = corpus->sized[index] != 0 && corpus->offsets[index] != 0 && corpus->ends[index] != 0 && corpus->machine[index] != 0;
                corpus->count += 1;
                corpus->entries += function->count;
                corpus->records += vector.count;
                corpus->deferred += vector.deferred;
                (void)mark;
            }
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

int run_backend_benchmark(int argc, char** argv);
int run_backend_benchmark(int argc, char** argv)
{
    uint64_t entries = BUSTER_TIER0_BENCHMARK_ENTRIES;
    uint64_t seed = 1;
    uint32_t only = BUSTER_TIER0_BENCHMARK_VARIANTS;
    uint64_t after_scalar = 0;
    bool valid = true;
    for (int a = 1; a < argc && valid; a += 1)
    {
        if (strcmp(argv[a], "--entries") == 0 && a + 1 < argc)
        {
            entries = strtoull(argv[a + 1], 0, 0);
            a += 1;
        }
        else if (strcmp(argv[a], "--seed") == 0 && a + 1 < argc)
        {
            seed = strtoull(argv[a + 1], 0, 0);
            a += 1;
        }
        else if (strcmp(argv[a], "--after-scalar") == 0 && a + 1 < argc)
        {
            after_scalar = strtoull(argv[a + 1], 0, 0);
            a += 1;
        }
        else if (strcmp(argv[a], "--only") == 0 && a + 1 < argc)
        {
            for (uint32_t v = 0; v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
            {
                only = strcmp(argv[a + 1], tier0_benchmark_names[v]) == 0 ? v : only;
            }
            valid = only != BUSTER_TIER0_BENCHMARK_VARIANTS;
            a += 1;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    Tier0Tables tables;
    if (!valid || entries == 0 || after_scalar > 10000)
    {
        fprintf(stderr, "usage: build/compiler benchmark-backend [--entries N] [--seed N] [--only vector|scalar|sizing|encode] [--after-scalar MS]\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && tier0_tables_open(&tables)))
    {
        fprintf(stderr, "benchmark-backend: the tables can't be built\n");
    }
    else
    {
        Arena arena = arena_reserve(64ull << 30);
        Arena code_arena = arena_reserve(64ull << 30);
        Arena scratch = arena_reserve(1ull << 32);
        Tier0BenchmarkCorpus corpus;
        memset(&corpus, 0, sizeof(corpus));
        if (tier0_benchmark_corpus(&corpus, &tables, &arena, &code_arena, seed, entries))
        {
            const Tier0BenchmarkCounters counters = {
                .cycles = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES),
                .instructions = tier0_benchmark_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS),
                .throttle = tier0_benchmark_counter(PERF_TYPE_RAW, BUSTER_TIER0_BENCHMARK_THROTTLE_EVENT),
            };
            // A warm-up of each variant (sizing before encoding, which takes the sized records).
            for (uint32_t v = 0; v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
            {
                (void)tier0_benchmark_run(v, &corpus, &scratch, counters);
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
                            (void)tier0_benchmark_run(BUSTER_TIER0_BENCHMARK_SCALAR, &corpus, &scratch, counters);
                        }
                    }
                    const Tier0BenchmarkReading taken = tier0_benchmark_run(v, &corpus, &scratch, counters);
                    values[v][0][pass] = (double)taken.nanoseconds;
                    values[v][1][pass] = (double)taken.cycles;
                    values[v][2][pass] = (double)taken.instructions;
                    values[v][3][pass] = (double)taken.throttle;
                }
            }
            printf("benchmark-backend: %u functions, %" PRIu64 " typed entries, %" PRIu64 " records (%.3f per entry), %" PRIu64 " bytes; %" PRIu64
                   " entries deferred (%.2f%%)\n",
                   corpus.count, corpus.entries, corpus.records, (double)corpus.records / (double)corpus.entries, corpus.bytes, corpus.deferred,
                   100.0 * (double)corpus.deferred / (double)corpus.entries);
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
                const double e = (double)corpus.entries;
                const double r = (double)corpus.records;
                printf("%-8s %12.2f %12.2f %12.2f %12.2f %12.2f %8.2f %12.3f\n", tier0_benchmark_names[v], ns / e, cycles / e, instructions / e, ns / r, cycles / r,
                       cycles != 0 ? instructions / cycles : 0.0, throttle / r);
            }
            result = 0;
        }
        arena_release(&arena);
        arena_release(&code_arena);
        arena_release(&scratch);
        tier0_tables_close(&tables);
    }
    return result;
}
