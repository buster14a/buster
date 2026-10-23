// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original measurement driver API for the recorded cloud comparisons.
#ifndef BUSTER_PP_PAIR_H
#define BUSTER_PP_PAIR_H

#include <stdbool.h>
#include <stdint.h>

#define BUSTER_PP_PAIR_PHASES 5
#define BUSTER_PP_PAIR_STATISTICS 18

typedef struct PpPairSample PpPairSample;
struct PpPairSample
{
    uint64_t preprocess_ns;
    uint64_t print_ns;
    uint64_t release_ns;
    uint64_t input_tokens;
    uint64_t output_tokens;
    uint64_t input_bytes;
    // read, lex, refine, directives, total: the existing benchmark's TSC ticks, not core-cycle counters.
    uint64_t ticks[BUSTER_PP_PAIR_PHASES];
    uint64_t digest;
    uint64_t token_digest;
    uint64_t history_bytes;
    uint64_t temporary_max_bytes;
    uint64_t expansions;
    uint64_t substitutions;
    uint64_t generated;
    uint64_t runs;
    // Exact PPStatistics declaration order, guarded by an assertion in each wrapper translation unit.
    uint64_t statistics[BUSTER_PP_PAIR_STATISTICS];
};

// argc/argv contain the normal preprocessing options and exactly one source path. They remain live until release.
// Configure parses options outside timing. Run owns a fresh preprocessor/intern table and releases both each call.
bool pp_a_configure(int argc, char** argv);
bool pp_b_configure(int argc, char** argv);
bool pp_a_run(PpPairSample* sample);
bool pp_b_run(PpPairSample* sample);
void pp_a_release(void);
void pp_b_release(void);

#endif
