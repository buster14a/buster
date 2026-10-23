// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original wrapper for the recorded cloud comparisons.
// Compile twice, with BUSTER_PAIR_PREFIX=pp_a_ / pp_b_ and -I selecting baseline/current src respectively.
// Existing benchmark code is included from its own tree, retaining its original provenance and notices.
#include "pp_pair.h"

#ifndef BUSTER_PAIR_PREFIX
#error "Define BUSTER_PAIR_PREFIX as pp_a_ or pp_b_"
#endif
#define BUSTER_PAIR_JOIN_INNER(a, b) a##b
#define BUSTER_PAIR_JOIN(a, b) BUSTER_PAIR_JOIN_INNER(a, b)
#define BUSTER_PAIR_NAME(name) BUSTER_PAIR_JOIN(BUSTER_PAIR_PREFIX, name)

// This is the included benchmark's only externally linked function; everything in its compiler modules is static.
#define run_preprocessor_benchmark BUSTER_PAIR_NAME(existing_command)
#include "buster/compiler/c_preprocessor_benchmark.c"
#undef run_preprocessor_benchmark

_Static_assert(PP_PHASE_COUNT + 1 == BUSTER_PP_PAIR_PHASES, "update the pair phase ABI");
_Static_assert(sizeof(PPStatistics) == sizeof(uint64_t) * BUSTER_PP_PAIR_STATISTICS, "update the pair statistics ABI");

static Arena pp_pair_option_arena;
static PPOptions pp_pair_options;
static const char* pp_pair_input;
static bool pp_pair_ready;

bool BUSTER_PAIR_NAME(configure)(int argc, char** argv)
{
    arena_release(&pp_pair_option_arena);
    pp_pair_option_arena = arena_reserve(1ull << 30);
    const char* output = 0;
    bool markers = false;
    pp_pair_ready = pp_pair_option_arena.base != 0 &&
                    pp_parse_options(argc, argv, &pp_pair_options, &pp_pair_option_arena, &pp_pair_input, &output, &markers) &&
                    pp_pair_input != 0 && !pp_pair_options.scalar;
    // benchmark-preprocessor uses Clang feature-query answers too; fixed --predefines supplies the version macros.
    pp_pair_options.clang_features = true;
    return pp_pair_ready;
}

bool BUSTER_PAIR_NAME(run)(PpPairSample* out)
{
    *out = (PpPairSample){ 0 };
    PPBenchmarkSample sample;
    char* file = (char*)pp_pair_input;
    const bool result = pp_pair_ready && pp_benchmark_pass(&pp_pair_options, BUSTER_FILE_CACHE_READ_LIMIT, &file, 1, -1, -1,
                                                          &sample, &out->input_tokens, &out->output_tokens);
    if (result)
    {
        out->preprocess_ns = sample.nanoseconds;
        out->print_ns = sample.print_nanoseconds;
        out->release_ns = sample.release_nanoseconds;
        out->input_bytes = sample.input_bytes;
        memcpy(out->ticks, sample.ticks, sizeof(out->ticks));
        out->digest = sample.digest;
        out->token_digest = sample.token_digest;
        out->history_bytes = sample.history_bytes;
        out->temporary_max_bytes = sample.temporary_max_bytes;
        out->expansions = sample.expansions;
        out->substitutions = sample.substitutions;
        out->generated = sample.generated;
        out->runs = sample.runs;
        memcpy(out->statistics, &sample.statistics, sizeof(out->statistics));
    }
    return result;
}

void BUSTER_PAIR_NAME(release)(void)
{
    arena_release(&pp_pair_option_arena);
    pp_pair_ready = false;
    pp_pair_input = 0;
}
