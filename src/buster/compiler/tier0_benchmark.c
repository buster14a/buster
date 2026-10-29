// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The backend's benchmark (benchmark-backend, #136): Tier 0's vector path and its scalar twin on the functions of
// generated programs (tier0_generate.c), apart from branch sizing and encoding, which it measures on their records after.
// docs/tier0.md has the numbers.
//
// Usage: build/compiler benchmark-backend [--entries N] [--seed N]
//            [--only vector|scalar|sizing|encode|validate|validate-scalar|choose|choose-scalar] [--after-scalar MS]
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...). It generates programs until their functions
// hold --entries typed entries (262,144 by default, more than the branch predictor learns from pass to pass), checks
// that both paths give every function the same code, then reports the median of BUSTER_TIER0_BENCHMARK_PASSES passes
// that alternate which variant runs first, each a run over every function: nanoseconds, cycles and instructions per
// typed entry and per record (the counters read zero where perf_event_open refuses), IPC and the throttle (PMCx1A0's
// umask 0x08, #199); then the entries the vector path defers and the records' and bytes' totals, and the form choice's
// requests (#221): how many per entry, what they chose and the bytes they saved. --only times one variant alone, back
// to back; --after-scalar MS runs the scalar twin, untimed, for MS ms before each pass of the vector path, the
// validation and the choice (#199's state after lighter phases: 100 to 300). The exit status is 1 on invalid arguments or if the paths
// disagree.
//
// Layout: the corpus (tier0_benchmark_corpus); run_backend_benchmark. The clock, the counters, the variants and the
// report are tier0_benchmark_measure.c's, which benchmark-backend-source shares (tier0_source_benchmark.c).

#include "host_isa.h"
#if BUSTER_HOST_VECTOR
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>

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

#include "buster/compiler/tier0_benchmark_measure.c"

#define BUSTER_TIER0_BENCHMARK_ENTRIES 262144u

// ---------------------------------------------------------------------------------------------------------------------
// The corpus

// Programs of a shape like compiled C's (calls, loops, floating values and pointers), until the corpus holds `entries`
// entries, without the functions Tier 0 refuses (by both paths alike). Returns false if the paths disagree on a function
// or memory runs out.
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
    bool result = tier0_benchmark_reserve(corpus, arena, BUSTER_TIER0_BENCHMARK_MAX_FUNCTIONS);
    for (uint64_t p = 0; result && corpus->entries < entries; p += 1)
    {
        Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Program, 1);
        Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(arena, Tier0Functions, 1);
        result = program != 0 && functions != 0 && tier0_generate_program(program, arena, seed + p, options, "c_") &&
                 tier0_program_functions(program, tables, arena, functions);
        for (uint32_t f = 0; result && f < functions->count; f += 1)
        {
            const Tier0BenchmarkAdd added = tier0_benchmark_add(corpus, &functions->functions[f], arena, code_arena);
            result = added == TIER0_BENCHMARK_ADDED || added == TIER0_BENCHMARK_REFUSED;
            if (!result)
            {
                fprintf(stderr, "benchmark-backend: %s program %" PRIu64 "'s function %u\n",
                        added == TIER0_BENCHMARK_DISAGREE ? "the paths disagree on" : "memory ran out for", seed + p, f);
            }
        }
    }
    return result;
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
            // Each --only names its variant or is refused, whatever one before it named.
            uint32_t named = BUSTER_TIER0_BENCHMARK_VARIANTS;
            for (uint32_t v = 0; v < BUSTER_TIER0_BENCHMARK_VARIANTS; v += 1)
            {
                named = strcmp(argv[a + 1], tier0_benchmark_names[v]) == 0 ? v : named;
            }
            only = named;
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
        fprintf(stderr, "usage: build/compiler benchmark-backend [--entries N] [--seed N] [--only vector|scalar|sizing|encode|validate|validate-scalar|choose|choose-scalar] [--after-scalar MS]\n");
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
        if (tier0_benchmark_corpus(&corpus, &tables, &arena, &code_arena, seed, entries))
        {
            result = tier0_benchmark_measure(&corpus, &scratch, only, after_scalar, "benchmark-backend") ? 0 : 1;
        }
        arena_release(&arena);
        arena_release(&code_arena);
        arena_release(&scratch);
        tier0_tables_close(&tables);
    }
    return result;
}

#else
int run_backend_benchmark(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return 1;
}
#endif // BUSTER_HOST_VECTOR
