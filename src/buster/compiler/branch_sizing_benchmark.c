// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The branch sizing benchmark, for the benchmark-branch-sizing command:
//
//   build/compiler benchmark-branch-sizing [--seed N] [--rounds N] [--only ROW] [--measure NAME [--samples N]]
//
// It sizes pools of generated functions (branch_sizing_generate.c), one row of branch_sizing_benchmark_rows at a time
// (--only ROW picks one): compiler-like functions of 40, 250, 2,000 and 10,000 records whose branches follow the
// statistics of clang's -O0 code (tier 0's) and -O2 code (docs/branch-sizing.md has them), a mix of the function sizes
// of sqlite3 at -O0, and chains of 1,000 links forward and backward (a round per link: the worklist's case). After
// checking that every function is valid and that the entry points and every path's phase-by-phase run give the same
// records and offsets, it reports medians of BUSTER_BRANCH_SIZING_BENCHMARK_PASSES samples, the measurements taking
// turns, of:
//   - x86_encode of each function with its offsets, as the caller encodes it after sizing: the reference;
//   - size_branches and size_branches_scalar, whole (with --rounds N, branch_sizing_run with that round limit);
//   - each path's phases: the difference between a run that stops after the phase and one that stops before it
//     (branch_sizing_benchmark_through, which runs branch_sizing_run's phases in its order), and the whole run. Lengths
//     includes the scratch memory's columns, and rounds the worklist. The paths are the vector one, the scalar one, and
//     the vector one with each alternative to its gathers measured against it (BranchSizingBenchmarkPath).
// in cycles per branch and per record, with instructions per cycle. --measure NAME times only one of them, --samples
// times (for perf): an entry point's name ("x86_encode", "size_branches" or "size_branches_scalar"), or a path's run
// through a phase, as "vector-setup" or "window-write".
//
// A sample sizes the whole pool once, a group of functions of at least BUSTER_BRANCH_SIZING_BENCHMARK_GROUP_RECORDS
// records at a time. Before each group, outside the time, it puts the branches' records back as they were generated and
// reads the group's records and branch columns, so that they are in cache as a function the code generator has just
// written is. The counters (this thread's user-space cycles and instructions, with rdpmc) are read around each group,
// less what reading them costs. The pool is BUSTER_BRANCH_SIZING_BENCHMARK_POOL_RECORDS records of different
// functions, more than the branch predictor learns: with a pool of 33,000 records, sized 13 times a sample, the scalar
// path's branches were learnt and its offsets phase ran three times faster than on a pool of 137,000. Run it under the
// 9700X's lock pinned to one CPU (taskset -c 2). The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the rows, the pools (generation, restoring and checking), the phase-by-phase run, the
// counters, timing, and run_branch_sizing_benchmark.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

#include "buster/compiler/lane.c"
// The encoder's batch widths and the variants its benchmark measures, which this file doesn't call.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#pragma clang diagnostic pop
#include "buster/compiler/branch_sizing.c"
#include "buster/compiler/branch_sizing_generate.c"

#define BUSTER_BRANCH_SIZING_BENCHMARK_PASSES 21
// The most samples --samples takes.
#define BUSTER_BRANCH_SIZING_BENCHMARK_MAX_SAMPLES 100000
// The records of a row's pool, and of a group timed at once.
#define BUSTER_BRANCH_SIZING_BENCHMARK_POOL_RECORDS 262144
#define BUSTER_BRANCH_SIZING_BENCHMARK_GROUP_RECORDS 2048
// The empty intervals whose median is what reading the counters costs.
#define BUSTER_BRANCH_SIZING_BENCHMARK_OVERHEAD_SAMPLES 1001
#define BUSTER_BRANCH_SIZING_BENCHMARK_ARENA_BYTES (1ull << 30)

// ---------------------------------------------------------------------------------------------------------------------
// The rows

// The shapes of clang's code (docs/branch-sizing.md): tier 0's like -O0's, a branch every 9 records, 45% of them jmp
// and 5.5% backward, most targets within 64 records and 15% anywhere in the function; and optimized code like -O2's, a
// branch every 7 records, 29% jmp and 23% backward, most targets within 128 records and 20% anywhere. The distances'
// logarithms are about uniform (branch_sizing_log_distance).
static const BranchSizingShape branch_sizing_benchmark_tier0 = {
    .branch_period = 9,
    .near = 64,
    .far_per_mille = 150,
    .jmp_per_mille = 450,
    .backward_per_mille = 55,
    .compiler_like = true,
};
static const BranchSizingShape branch_sizing_benchmark_optimized = {
    .branch_period = 7,
    .near = 128,
    .far_per_mille = 200,
    .jmp_per_mille = 290,
    .backward_per_mille = 230,
    .compiler_like = true,
};

// The function sizes of sqlite3 3.53 at clang -O0 (2,603 functions, 90.8 instructions each on average): the size at
// each per mille of them, between which branch_sizing_benchmark_mix_size interpolates.
typedef struct BranchSizingBenchmarkQuantile BranchSizingBenchmarkQuantile;
struct BranchSizingBenchmarkQuantile
{
    uint32_t per_mille;
    uint32_t records;
};

static const BranchSizingBenchmarkQuantile branch_sizing_benchmark_sizes[] = {
    { 0, 5 },
    { 50, 10 },
    { 100, 13 },
    { 200, 19 },
    { 300, 24 },
    { 400, 31 },
    { 500, 41 },
    { 600, 54 },
    { 700, 72 },
    { 800, 101 },
    { 850, 129 },
    { 900, 174 },
    { 950, 287 },
    { 975, 427 },
    { 990, 749 },
    { 995, 1120 },
    { 998, 1793 },
    { 999, 2605 },
    { 1000, 9257 },
};

// A function size of the mix: a uniform per mille, with fractions, and the size between its quantiles.
static uint64_t branch_sizing_benchmark_mix_size(uint64_t* state)
{
    const uint32_t point = branch_sizing_below(state, 1000 * 1024);
    uint32_t i = 1;
    while (branch_sizing_benchmark_sizes[i].per_mille * 1024 <= point)
    {
        i += 1;
    }
    const BranchSizingBenchmarkQuantile* low = &branch_sizing_benchmark_sizes[i - 1];
    const BranchSizingBenchmarkQuantile* high = &branch_sizing_benchmark_sizes[i];
    const uint64_t span = (uint64_t)(high->per_mille - low->per_mille) * 1024;
    return low->records + (uint64_t)(high->records - low->records) * (point - low->per_mille * 1024) / span;
}

typedef enum BranchSizingBenchmarkKind
{
    // Functions of `records` records of the profile.
    BRANCH_SIZING_BENCHMARK_FIXED,
    // Functions of the mix's sizes, of the profile.
    BRANCH_SIZING_BENCHMARK_MIX,
    // Chains of `records` links (branch_sizing_chain).
    BRANCH_SIZING_BENCHMARK_FORWARD_CHAIN,
    BRANCH_SIZING_BENCHMARK_BACKWARD_CHAIN,
} BranchSizingBenchmarkKind;

typedef struct BranchSizingBenchmarkRow BranchSizingBenchmarkRow;
struct BranchSizingBenchmarkRow
{
    const char* name;
    BranchSizingBenchmarkKind kind;
    uint64_t records;
    bool optimized;
};

// The largest function a row has: a chain of 1,000 links (the mix's largest is 9,257 records).
#define BUSTER_BRANCH_SIZING_BENCHMARK_LARGEST BUSTER_BRANCH_SIZING_CHAIN_RECORDS(1000)

static const BranchSizingBenchmarkRow branch_sizing_benchmark_rows[] = {
    { "tier0-40", BRANCH_SIZING_BENCHMARK_FIXED, 40, false },
    { "tier0-250", BRANCH_SIZING_BENCHMARK_FIXED, 250, false },
    { "tier0-2000", BRANCH_SIZING_BENCHMARK_FIXED, 2000, false },
    { "tier0-10000", BRANCH_SIZING_BENCHMARK_FIXED, 10000, false },
    { "tier0-mix", BRANCH_SIZING_BENCHMARK_MIX, 0, false },
    { "optimized-40", BRANCH_SIZING_BENCHMARK_FIXED, 40, true },
    { "optimized-250", BRANCH_SIZING_BENCHMARK_FIXED, 250, true },
    { "optimized-2000", BRANCH_SIZING_BENCHMARK_FIXED, 2000, true },
    { "optimized-10000", BRANCH_SIZING_BENCHMARK_FIXED, 10000, true },
    { "chain-forward", BRANCH_SIZING_BENCHMARK_FORWARD_CHAIN, 1000, false },
    { "chain-backward", BRANCH_SIZING_BENCHMARK_BACKWARD_CHAIN, 1000, false },
};

// ---------------------------------------------------------------------------------------------------------------------
// The pools

// A row's functions, one after another: each function's records from record_starts[f], and its branch columns (indices
// within the function) from branch_starts[f]; and each branch's record's form as generated, which the benchmark puts
// back after sizing changes it. The groups timed at once start at the functions group_starts gives.
typedef struct BranchSizingBenchmarkPool BranchSizingBenchmarkPool;
struct BranchSizingBenchmarkPool
{
    X86Instruction* records;
    uint32_t* branches;
    uint32_t* targets;
    uint16_t* forms;
    uint64_t* record_starts;
    uint64_t* branch_starts;
    uint64_t* group_starts;
    uint64_t functions;
    uint64_t groups;
    uint64_t records_total;
    uint64_t branches_total;
    // The largest function's records, for the offsets and the encoder's output, which every function reuses.
    uint64_t largest;
    uint32_t* offsets;
    uint8_t* encoded;
    bool valid;
};

// Generates the row's pool in the arena.
static BranchSizingBenchmarkPool branch_sizing_benchmark_pool(Arena* arena, const BranchSizingBenchmarkRow* row, uint64_t seed)
{
    const bool chain = row->kind == BRANCH_SIZING_BENCHMARK_FORWARD_CHAIN || row->kind == BRANCH_SIZING_BENCHMARK_BACKWARD_CHAIN;
    // Room for the pool's records and one more function of the largest size, and for as many functions of 5 records.
    const uint64_t room = BUSTER_BRANCH_SIZING_BENCHMARK_POOL_RECORDS + BUSTER_BRANCH_SIZING_BENCHMARK_LARGEST;
    const uint64_t most_functions = room / 5 + 1;
    BranchSizingBenchmarkPool result = {
        .records = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, room),
        .branches = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, room),
        .targets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, room),
        .forms = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, room),
        .record_starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, most_functions + 1),
        .branch_starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, most_functions + 1),
        .group_starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, most_functions + 1),
    };
    result.valid = result.records != 0 && result.branches != 0 && result.targets != 0 && result.forms != 0 && result.record_starts != 0 && result.branch_starts != 0 &&
                   result.group_starts != 0;
    uint64_t state = seed;
    uint64_t group_records = 0;
    bool done = !result.valid;
    while (!done)
    {
        X86Instruction* records = result.records + result.records_total;
        uint32_t* branches = result.branches + result.branches_total;
        uint32_t* targets = result.targets + result.branches_total;
        uint64_t count = 0;
        uint64_t branch_count = 0;
        if (chain)
        {
            count = branch_sizing_chain(&state, row->records, row->kind == BRANCH_SIZING_BENCHMARK_BACKWARD_CHAIN, records, branches, targets);
            branch_count = row->records;
        }
        else
        {
            BranchSizingShape shape = row->optimized ? branch_sizing_benchmark_optimized : branch_sizing_benchmark_tier0;
            shape.count = row->kind == BRANCH_SIZING_BENCHMARK_MIX ? branch_sizing_benchmark_mix_size(&state) : row->records;
            count = shape.count;
            branch_count = branch_sizing_generate(&state, &shape, records, branches, targets);
        }
        for (uint64_t k = 0; k < branch_count; k += 1)
        {
            result.forms[result.branches_total + k] = records[branches[k]].form;
        }
        if (group_records == 0)
        {
            result.group_starts[result.groups] = result.functions;
            result.groups += 1;
        }
        group_records = group_records + count >= BUSTER_BRANCH_SIZING_BENCHMARK_GROUP_RECORDS ? 0 : group_records + count;
        result.record_starts[result.functions] = result.records_total;
        result.branch_starts[result.functions] = result.branches_total;
        result.functions += 1;
        result.records_total += count;
        result.branches_total += branch_count;
        result.largest = count > result.largest ? count : result.largest;
        done = result.records_total >= BUSTER_BRANCH_SIZING_BENCHMARK_POOL_RECORDS || result.functions == most_functions;
    }
    if (result.valid)
    {
        result.record_starts[result.functions] = result.records_total;
        result.branch_starts[result.functions] = result.branches_total;
        result.group_starts[result.groups] = result.functions;
        result.offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, result.largest + 1);
        result.encoded = arena_push(arena, BUSTER_X86_ENCODER_CAPACITY(result.largest), 64);
        result.valid = result.offsets != 0 && result.encoded != 0;
    }
    return result;
}

// Puts the group's branches' records back as they were generated (their rel8 forms with displacement 0), and reads the
// group's records and branch columns, a dword per cache line. Returns what it read, which keeps the reads.
static uint32_t branch_sizing_benchmark_prepare(BranchSizingBenchmarkPool* pool, uint64_t group)
{
    const uint64_t first = pool->group_starts[group];
    const uint64_t end = pool->group_starts[group + 1];
    for (uint64_t f = first; f < end; f += 1)
    {
        X86Instruction* records = pool->records + pool->record_starts[f];
        for (uint64_t k = pool->branch_starts[f]; k < pool->branch_starts[f + 1]; k += 1)
        {
            records[pool->branches[k]].form = pool->forms[k];
            records[pool->branches[k]].displacement = 0;
        }
    }
    const uint32_t* record_words = (const uint32_t*)(pool->records + pool->record_starts[first]);
    const uint64_t record_bytes = (pool->record_starts[end] - pool->record_starts[first]) * sizeof(X86Instruction);
    uint32_t result = 0;
    for (uint64_t i = 0; i < record_bytes / sizeof(uint32_t); i += 16)
    {
        result += record_words[i];
    }
    for (uint64_t k = pool->branch_starts[first]; k < pool->branch_starts[end]; k += 16)
    {
        result += pool->branches[k] + pool->targets[k];
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The phase-by-phase run, and the variants of the vector path's phases measured against it

typedef enum BranchSizingBenchmarkStop
{
    // The scratch memory's columns and x86_encode's offsets.
    BRANCH_SIZING_BENCHMARK_LENGTHS,
    BRANCH_SIZING_BENCHMARK_SETUP,
    BRANCH_SIZING_BENCHMARK_RANKS,
    // The rounds, and the worklist past the round limit.
    BRANCH_SIZING_BENCHMARK_ROUNDS,
    BRANCH_SIZING_BENCHMARK_OFFSETS,
    // The records, and so everything branch_sizing_run does.
    BRANCH_SIZING_BENCHMARK_WRITE,
    BRANCH_SIZING_BENCHMARK_STOPS,
} BranchSizingBenchmarkStop;

static const char* const branch_sizing_benchmark_stop_names[BRANCH_SIZING_BENCHMARK_STOPS] = {
    "lengths",
    "setup",
    "ranks",
    "rounds",
    "offsets",
    "write",
};

// The paths of a phase-by-phase run: the vector one and the scalar one, and the vector one with each alternative to
// its gathers that #144's handoff proposed:
//   - compress: setup takes the branch records' offsets compressed out of the offsets column by the branch map
//     (vpcompressd, 16 records at a time) in place of their gather, building the map for it, which the ranks phase then
//     reuses;
//   - window: the rounds after the first take a short branch's P[rank] from the 144 prefix sums around its block of 16
//     (vpermi2d: its rank is within 63 branches of it) in place of a gather, and a last pass gathers the long branches'
//     displacements, whose ranks the windows don't reach.
typedef enum BranchSizingBenchmarkPath
{
    BRANCH_SIZING_BENCHMARK_VECTOR,
    BRANCH_SIZING_BENCHMARK_SCALAR,
    BRANCH_SIZING_BENCHMARK_COMPRESS,
    BRANCH_SIZING_BENCHMARK_WINDOW,
    BRANCH_SIZING_BENCHMARK_PATHS,
} BranchSizingBenchmarkPath;

static const char* const branch_sizing_benchmark_path_names[BRANCH_SIZING_BENCHMARK_PATHS] = {
    "vector",
    "scalar",
    "compress",
    "window",
};

// The compress variant's setup: branch_sizing_setup_vector with the branch records' offsets compressed out of the
// offsets column by the branch map, into the rank column (which the ranks phase fills later), in place of their gather.
static bool branch_sizing_benchmark_setup_compress(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                   uint64_t branch_count, const uint32_t* offsets, uint32_t length, BranchSizingColumns* columns)
{
    const uint64_t units = count / BUSTER_BRANCH_SIZING_MAP_RECORDS + 1;
    uint32_t* starts = columns->rank;
    memset(columns->map, 0, units * BUSTER_BRANCH_SIZING_MAP_RECORDS);
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        columns->map[branches[k]] = 1;
    }
    uint64_t taken = 0;
    for (uint64_t i = 0; i < count; i += BUSTER_BRANCH_SIZING_LANES)
    {
        const __m128i bytes = _mm_load_si128((const __m128i*)(columns->map + i));
        const __mmask16 marks = _mm_test_epi8_mask(bytes, bytes);
        const __m512i offset = _mm512_maskz_loadu_epi32(branch_sizing_lanes(i, count), offsets + i);
        _mm512_storeu_si512(starts + taken, _mm512_maskz_compress_epi32(marks, offset));
        taken += (uint64_t)__builtin_popcount(marks);
    }
    const __m512i zero = _mm512_setzero_si512();
    const __m512i count_vector = _mm512_set1_epi32((int32_t)count);
    const __m512i length_vector = _mm512_set1_epi32((int32_t)length);
    const __m512i short_bytes = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_SHORT_BYTES);
    const __m512i form_bits = _mm512_set1_epi32(0xffff);
    const __m512i jump_form = _mm512_set1_epi32(X86_FORM_JMP_REL8);
    const __m512i jump_growth = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_JMP_GROWTH);
    const __m512i condition_growth = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_JCC_GROWTH);
    const uint8_t* forms = (const uint8_t*)records + __builtin_offsetof(X86Instruction, form);
    uint32_t* prefix = columns->prefix[0];
    __m512i carry = zero;
    __mmask16 grown = 0;
    prefix[0] = 0;
    columns->prefix[1][0] = 0;
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __mmask16 lanes = branch_sizing_lanes(k, branch_count);
        const __m512i record = _mm512_maskz_loadu_epi32(lanes, branches + k);
        const __m512i target = _mm512_maskz_loadu_epi32(lanes, targets + k);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, target, count_vector);
        const __m512i start = _mm512_maskz_load_epi32(lanes, starts + k);
        const __m512i end = _mm512_mask_i32gather_epi32(length_vector, inside, target, offsets, 4);
        const __m512i form = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, lanes, _mm512_slli_epi32(record, 1), forms, 8), form_bits);
        const __mmask16 jump = _mm512_cmpeq_epi32_mask(form, jump_form);
        const __m512i grow = _mm512_maskz_mov_epi32(lanes, _mm512_mask_blend_epi32(jump, condition_growth, jump_growth));
        const __m512i displacement = _mm512_maskz_sub_epi32(lanes, end, _mm512_add_epi32(start, short_bytes));
        const __m512i growth = _mm512_maskz_mov_epi32(branch_sizing_out_of_range_vector(displacement), grow);
        const __m512i sums = _mm512_add_epi32(branch_sizing_scan(growth), carry);
        _mm512_store_si512(columns->grow + k, grow);
        _mm512_store_si512(columns->displacement0 + k, displacement);
        _mm512_store_si512(columns->displacement + k, displacement);
        _mm512_store_si512(columns->growth + k, growth);
        _mm512_storeu_si512(prefix + k + 1, sums);
        _mm512_storeu_si512(columns->prefix[1] + k + 1, zero);
        carry = branch_sizing_last(sums);
        grown |= _mm512_test_epi32_mask(growth, growth);
    }
    return grown != 0;
}

// The compress variant's ranks: branch_sizing_ranks_vector with the map that setup built.
static void branch_sizing_benchmark_ranks_reuse(uint64_t count, const uint32_t* targets, uint64_t branch_count, BranchSizingColumns* columns)
{
    const uint64_t units = count / BUSTER_BRANCH_SIZING_MAP_RECORDS + 1;
    uint64_t* ranks = columns->ranks;
    uint64_t before = 0;
    for (uint64_t unit = 0; unit < units; unit += 1)
    {
        const __m512i bytes = _mm512_load_si512(columns->map + unit * BUSTER_BRANCH_SIZING_MAP_RECORDS);
        const uint64_t bits = _mm512_test_epi8_mask(bytes, bytes);
        ranks[2 * unit] = (uint32_t)bits | before << 32;
        before += (uint64_t)__builtin_popcount((uint32_t)bits);
        ranks[2 * unit + 1] = (bits >> 32) | before << 32;
        before += (uint64_t)__builtin_popcount((uint32_t)(bits >> 32));
    }
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i low_bits = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_RANK_RECORDS - 1);
    const __m512i even = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    const __m512i odd = _mm512_setr_epi32(1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __mmask16 lanes = branch_sizing_lanes(k, branch_count);
        const __m512i target = _mm512_maskz_loadu_epi32(lanes, targets + k);
        const __m512i entry = _mm512_srli_epi32(target, 5);
        const __m512i first = _mm512_mask_i32gather_epi64(zero, (__mmask8)lanes, _mm512_castsi512_si256(entry), ranks, 8);
        const __m512i second = _mm512_mask_i32gather_epi64(zero, (__mmask8)(lanes >> 8), _mm512_extracti64x4_epi64(entry, 1), ranks, 8);
        const __m512i bits = _mm512_permutex2var_epi32(first, even, second);
        const __m512i counts = _mm512_permutex2var_epi32(first, odd, second);
        const __m512i below = _mm512_sub_epi32(_mm512_sllv_epi32(one, _mm512_and_si512(target, low_bits)), one);
        const __m512i rank = _mm512_add_epi32(counts, _mm512_popcnt_epi32(_mm512_and_si512(bits, below)));
        _mm512_store_si512(columns->rank + k, rank);
    }
}

// The prefix sums of a window: 144, from the block's 64 before it, or from the column's first, or its last 144, as many
// of them as the column has (branch_sizing_allocate's padded count and 16 more).
#define BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW 144
#define BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW_BEFORE 64

// The window variant's round: branch_sizing_round_vector with a short branch's P[rank] looked up in the block's window
// with four vpermi2d and a vpermd in place of a gather. A long branch stays long; its displacement, whose rank may be
// past the window, is left for branch_sizing_benchmark_long_branches.
static bool branch_sizing_benchmark_round_window(uint64_t branch_count, BranchSizingColumns* columns, uint32_t current)
{
    const uint32_t* before = columns->prefix[current];
    uint32_t* after = columns->prefix[current ^ 1];
    const uint64_t entries = ((branch_count + BUSTER_BRANCH_SIZING_LANES - 1) & ~(uint64_t)(BUSTER_BRANCH_SIZING_LANES - 1)) + 2 * BUSTER_BRANCH_SIZING_LANES;
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i two = _mm512_set1_epi32(2);
    const __m512i three = _mm512_set1_epi32(3);
    const __m512i four = _mm512_set1_epi32(4);
    __m512i carry = _mm512_setzero_si512();
    __mmask16 changed = 0;
    after[0] = 0;
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        uint64_t base = k >= BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW_BEFORE ? k - BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW_BEFORE : 0;
        base = entries >= BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW && base > entries - BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW ? entries - BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW : base;
        __m512i window[BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW / BUSTER_BRANCH_SIZING_LANES];
        for (uint32_t w = 0; w < BUSTER_BRANCH_SIZING_BENCHMARK_WINDOW / BUSTER_BRANCH_SIZING_LANES; w += 1)
        {
            const uint64_t at = base + (uint64_t)w * BUSTER_BRANCH_SIZING_LANES;
            window[w] = _mm512_maskz_loadu_epi32(at < entries ? branch_sizing_lanes(at, entries) : 0, before + at);
        }
        const __m512i index = _mm512_sub_epi32(_mm512_load_si512(columns->rank + k), _mm512_set1_epi32((int32_t)base));
        const __m512i pair = _mm512_srli_epi32(index, 5);
        __m512i at_target = _mm512_permutex2var_epi32(window[0], index, window[1]);
        at_target = _mm512_mask_mov_epi32(at_target, _mm512_cmpeq_epi32_mask(pair, one), _mm512_permutex2var_epi32(window[2], index, window[3]));
        at_target = _mm512_mask_mov_epi32(at_target, _mm512_cmpeq_epi32_mask(pair, two), _mm512_permutex2var_epi32(window[4], index, window[5]));
        at_target = _mm512_mask_mov_epi32(at_target, _mm512_cmpeq_epi32_mask(pair, three), _mm512_permutex2var_epi32(window[6], index, window[7]));
        at_target = _mm512_mask_mov_epi32(at_target, _mm512_cmpeq_epi32_mask(pair, four), _mm512_permutexvar_epi32(index, window[8]));
        const __m512i old = _mm512_load_si512(columns->growth + k);
        const __m512i at_end = _mm512_loadu_si512(before + k + 1);
        const __m512i displacement = _mm512_add_epi32(_mm512_load_si512(columns->displacement0 + k), _mm512_sub_epi32(at_target, at_end));
        const __mmask16 grows = _mm512_test_epi32_mask(old, old) | branch_sizing_out_of_range_vector(displacement);
        const __m512i growth = _mm512_maskz_mov_epi32(grows, _mm512_load_si512(columns->grow + k));
        const __m512i sums = _mm512_add_epi32(branch_sizing_scan(growth), carry);
        changed |= _mm512_cmpneq_epi32_mask(growth, old);
        _mm512_store_si512(columns->growth + k, growth);
        _mm512_store_si512(columns->displacement + k, displacement);
        _mm512_storeu_si512(after + k + 1, sums);
        carry = branch_sizing_last(sums);
    }
    return changed != 0;
}

// The window variant's last pass: each long branch's displacement from the final prefix sums, gathered at the long
// lanes' ranks alone.
static void branch_sizing_benchmark_long_branches(uint64_t branch_count, BranchSizingColumns* columns, uint32_t current)
{
    const uint32_t* prefix = columns->prefix[current];
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __m512i growth = _mm512_load_si512(columns->growth + k);
        const __mmask16 long_lanes = _mm512_test_epi32_mask(growth, growth);
        const __m512i at_target = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), long_lanes, _mm512_load_si512(columns->rank + k), prefix, 4);
        const __m512i displacement = _mm512_add_epi32(_mm512_load_si512(columns->displacement0 + k), _mm512_sub_epi32(at_target, _mm512_loadu_si512(prefix + k + 1)));
        _mm512_mask_store_epi32(columns->displacement + k, long_lanes, displacement);
    }
}

// branch_sizing_run's phases in its order, on the path, up to `stop`. With BRANCH_SIZING_BENCHMARK_WRITE it does all
// that branch_sizing_run does, which the checks compare.
static __attribute__((noinline)) BranchSizing branch_sizing_benchmark_through(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                                              uint64_t branch_count, uint32_t* offsets, BranchSizingBenchmarkPath path, uint32_t round_limit,
                                                                              uint32_t stop)
{
    const bool vector = path != BRANCH_SIZING_BENCHMARK_SCALAR;
    BranchSizing result = {
        .error = BRANCH_SIZING_OK,
    };
    Scratch scratch = scratch_begin(0);
    BranchSizingColumns columns = branch_sizing_allocate(scratch.arena, count, branch_count, vector);
    uint64_t length = 0;
    if (!columns.valid)
    {
        result.error = BRANCH_SIZING_ERROR_MEMORY;
    }
    else
    {
        length = branch_sizing_lengths(records, count, offsets, columns.encoded, vector);
        result.error = branch_sizing_too_large(length, branch_count) ? BRANCH_SIZING_ERROR_TOO_LARGE : BRANCH_SIZING_OK;
    }
    bool changed = false;
    uint32_t current = 0;
    if (result.error == BRANCH_SIZING_OK && stop >= BRANCH_SIZING_BENCHMARK_SETUP)
    {
        if (path == BRANCH_SIZING_BENCHMARK_COMPRESS)
        {
            changed = branch_sizing_benchmark_setup_compress(records, count, branches, targets, branch_count, offsets, (uint32_t)length, &columns);
        }
        else if (vector)
        {
            changed = branch_sizing_setup_vector(records, count, branches, targets, branch_count, offsets, (uint32_t)length, &columns);
        }
        else
        {
            changed = branch_sizing_setup_scalar(records, count, branches, targets, branch_count, offsets, (uint32_t)length, &columns);
        }
        result.rounds = 1;
    }
    if (result.error == BRANCH_SIZING_OK && stop >= BRANCH_SIZING_BENCHMARK_RANKS && changed)
    {
        if (path == BRANCH_SIZING_BENCHMARK_COMPRESS)
        {
            branch_sizing_benchmark_ranks_reuse(count, targets, branch_count, &columns);
        }
        else if (vector)
        {
            branch_sizing_ranks_vector(count, branches, targets, branch_count, &columns);
        }
        else
        {
            branch_sizing_ranks_scalar(count, branches, targets, branch_count, &columns);
        }
    }
    if (result.error == BRANCH_SIZING_OK && stop >= BRANCH_SIZING_BENCHMARK_ROUNDS)
    {
        const bool grew = changed;
        bool listed = false;
        while (changed)
        {
            if (result.rounds >= round_limit && !listed)
            {
                result.evaluations = branch_sizing_worklist(branch_count, &columns, current, vector);
                if (vector)
                {
                    branch_sizing_prefix_vector(branch_count, &columns, current ^ 1);
                }
                else
                {
                    branch_sizing_prefix_scalar(branch_count, &columns, current ^ 1);
                }
                current ^= 1;
                listed = true;
            }
            if (path == BRANCH_SIZING_BENCHMARK_WINDOW)
            {
                changed = branch_sizing_benchmark_round_window(branch_count, &columns, current);
            }
            else if (vector)
            {
                changed = branch_sizing_round_vector(branch_count, &columns, current);
            }
            else
            {
                changed = branch_sizing_round_scalar(branch_count, &columns, current);
            }
            current ^= 1;
            result.rounds += 1;
        }
        if (path == BRANCH_SIZING_BENCHMARK_WINDOW && grew)
        {
            branch_sizing_benchmark_long_branches(branch_count, &columns, current);
        }
    }
    if (result.error == BRANCH_SIZING_OK && stop >= BRANCH_SIZING_BENCHMARK_OFFSETS)
    {
        if (vector)
        {
            branch_sizing_offsets_vector(count, branches, branch_count, offsets, &columns, current);
        }
        else
        {
            branch_sizing_offsets_scalar(count, branches, branch_count, offsets, &columns, current);
        }
    }
    if (result.error == BRANCH_SIZING_OK && stop >= BRANCH_SIZING_BENCHMARK_WRITE)
    {
        result.length = length + columns.prefix[current][branch_count];
        result.long_branches = branch_sizing_write(records, branches, branch_count, &columns);
    }
    scratch_end(scratch);
    return result;
}

// What sizing made of a function, for the checks.
typedef struct BranchSizingBenchmarkOutcome BranchSizingBenchmarkOutcome;
struct BranchSizingBenchmarkOutcome
{
    BranchSizing result;
    X86Instruction* records;
    uint32_t* offsets;
};

// The ways the checks size a function: the two entry points (or branch_sizing_run with the round limit), and a
// phase-by-phase run on each path.
#define BUSTER_BRANCH_SIZING_BENCHMARK_WAYS (2 + BRANCH_SIZING_BENCHMARK_PATHS)

static BranchSizingBenchmarkOutcome branch_sizing_benchmark_size(Arena* arena, const BranchSizingBenchmarkPool* pool, uint64_t f, uint32_t way, uint32_t round_limit)
{
    const X86Instruction* records = pool->records + pool->record_starts[f];
    const uint64_t count = pool->record_starts[f + 1] - pool->record_starts[f];
    const uint32_t* branches = pool->branches + pool->branch_starts[f];
    const uint32_t* targets = pool->targets + pool->branch_starts[f];
    const uint64_t branch_count = pool->branch_starts[f + 1] - pool->branch_starts[f];
    BranchSizingBenchmarkOutcome result = {
        .records = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, count + 1),
        .offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + 1),
    };
    if (result.records != 0 && result.offsets != 0)
    {
        memcpy(result.records, records, count * sizeof(X86Instruction));
        if (way < 2 && round_limit == BUSTER_BRANCH_SIZING_ROUNDS)
        {
            result.result = way == 0 ? size_branches(result.records, count, branches, targets, branch_count, result.offsets)
                                     : size_branches_scalar(result.records, count, branches, targets, branch_count, result.offsets);
        }
        else if (way < 2)
        {
            result.result = branch_sizing_run(result.records, count, branches, targets, branch_count, result.offsets, way == 0, round_limit);
        }
        else
        {
            result.result = branch_sizing_benchmark_through(result.records, count, branches, targets, branch_count, result.offsets, (BranchSizingBenchmarkPath)(way - 2),
                                                            round_limit, BRANCH_SIZING_BENCHMARK_WRITE);
        }
    }
    else
    {
        result.result.error = BRANCH_SIZING_ERROR_MEMORY;
    }
    return result;
}

// The pool's statistics, from the checks.
typedef struct BranchSizingBenchmarkStatistics BranchSizingBenchmarkStatistics;
struct BranchSizingBenchmarkStatistics
{
    // The functions' bytes once sized.
    uint64_t bytes;
    uint64_t long_branches;
    uint64_t rounds;
    uint32_t most_rounds;
    uint64_t listed;
    uint64_t evaluations;
};

// Checks every function of the pool: branch_sizing_validate accepts it, and every way sizes it to the same result,
// records and offsets, with no error. Fills the statistics.
static bool branch_sizing_benchmark_check(Arena* arena, const BranchSizingBenchmarkPool* pool, uint32_t round_limit, BranchSizingBenchmarkStatistics* statistics)
{
    bool result = true;
    *statistics = (BranchSizingBenchmarkStatistics){ 0 };
    for (uint64_t f = 0; result && f < pool->functions; f += 1)
    {
        const uint64_t mark = arena->length;
        const uint64_t count = pool->record_starts[f + 1] - pool->record_starts[f];
        const uint64_t branch_count = pool->branch_starts[f + 1] - pool->branch_starts[f];
        const BranchSizingCheck check = branch_sizing_validate(pool->records + pool->record_starts[f], count, pool->branches + pool->branch_starts[f],
                                                               pool->targets + pool->branch_starts[f], branch_count);
        result = check.error == BRANCH_SIZING_OK;
        BranchSizingBenchmarkOutcome outcomes[BUSTER_BRANCH_SIZING_BENCHMARK_WAYS];
        for (uint32_t way = 0; result && way < BUSTER_BRANCH_SIZING_BENCHMARK_WAYS; way += 1)
        {
            outcomes[way] = branch_sizing_benchmark_size(arena, pool, f, way, round_limit);
            const BranchSizing* a = &outcomes[0].result;
            const BranchSizing* b = &outcomes[way].result;
            result = b->error == BRANCH_SIZING_OK && a->length == b->length && a->rounds == b->rounds && a->long_branches == b->long_branches &&
                     a->evaluations == b->evaluations && memcmp(outcomes[0].records, outcomes[way].records, count * sizeof(X86Instruction)) == 0 &&
                     memcmp(outcomes[0].offsets, outcomes[way].offsets, count * sizeof(uint32_t)) == 0;
            if (!result)
            {
                fprintf(stderr, "benchmark-branch-sizing: function %llu: way %u disagrees with size_branches\n", (unsigned long long)f, way);
            }
        }
        if (result)
        {
            const BranchSizing* sized = &outcomes[0].result;
            statistics->bytes += sized->length;
            statistics->long_branches += sized->long_branches;
            statistics->rounds += sized->rounds;
            statistics->most_rounds = sized->rounds > statistics->most_rounds ? sized->rounds : statistics->most_rounds;
            statistics->listed += sized->evaluations != 0;
            statistics->evaluations += sized->evaluations;
        }
        else if (check.error != BRANCH_SIZING_OK)
        {
            fprintf(stderr, "benchmark-branch-sizing: function %llu isn't valid (error %u at %llu)\n", (unsigned long long)f, check.error,
                    (unsigned long long)check.index);
        }
        arena_pop_to(arena, mark);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The counters: this thread's user-space cycles and instructions, read with rdpmc from each counter's page under its
// sequence lock (as linux/perf_event.h describes), or with read() where the kernel doesn't allow rdpmc.

#define BUSTER_BRANCH_SIZING_BENCHMARK_COUNTERS 2

typedef struct BranchSizingBenchmarkCounters BranchSizingBenchmarkCounters;
struct BranchSizingBenchmarkCounters
{
    // -1 where perf_event_open isn't allowed.
    int descriptors[BUSTER_BRANCH_SIZING_BENCHMARK_COUNTERS];
    volatile struct perf_event_mmap_page* pages[BUSTER_BRANCH_SIZING_BENCHMARK_COUNTERS];
};

typedef struct BranchSizingBenchmarkCount BranchSizingBenchmarkCount;
struct BranchSizingBenchmarkCount
{
    uint64_t cycles;
    uint64_t instructions;
};

static BranchSizingBenchmarkCounters branch_sizing_benchmark_open_counters(void)
{
    BranchSizingBenchmarkCounters result;
    for (uint32_t c = 0; c < BUSTER_BRANCH_SIZING_BENCHMARK_COUNTERS; c += 1)
    {
        struct perf_event_attr attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.size = sizeof(attributes);
        attributes.type = PERF_TYPE_HARDWARE;
        attributes.config = c == 0 ? PERF_COUNT_HW_CPU_CYCLES : PERF_COUNT_HW_INSTRUCTIONS;
        attributes.exclude_kernel = 1;
        attributes.exclude_hv = 1;
        result.descriptors[c] = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
        void* page = result.descriptors[c] >= 0 ? mmap(0, (size_t)sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, result.descriptors[c], 0) : MAP_FAILED;
        result.pages[c] = page != MAP_FAILED ? (volatile struct perf_event_mmap_page*)page : 0;
    }
    return result;
}

static void branch_sizing_benchmark_close_counters(BranchSizingBenchmarkCounters* counters)
{
    for (uint32_t c = 0; c < BUSTER_BRANCH_SIZING_BENCHMARK_COUNTERS; c += 1)
    {
        if (counters->pages[c] != 0)
        {
            munmap((void*)counters->pages[c], (size_t)sysconf(_SC_PAGESIZE));
        }
        if (counters->descriptors[c] >= 0)
        {
            close(counters->descriptors[c]);
        }
    }
}

static uint64_t branch_sizing_benchmark_read_counter(const BranchSizingBenchmarkCounters* counters, uint32_t c)
{
    uint64_t result = 0;
    volatile struct perf_event_mmap_page* page = counters->pages[c];
    if (page != 0 && page->cap_user_rdpmc)
    {
        bool stable = false;
        while (!stable)
        {
            const uint32_t sequence = page->lock;
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            const uint32_t index = page->index;
            int64_t count = page->offset;
            if (index != 0)
            {
                // The counter's bits, sign-extended from its width.
                const uint32_t unused = 64 - page->pmc_width;
                count += (int64_t)((uint64_t)__rdpmc((int)index - 1) << unused) >> unused;
            }
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            stable = page->lock == sequence;
            result = (uint64_t)count;
        }
    }
    else if (counters->descriptors[c] >= 0 && read(counters->descriptors[c], &result, sizeof(result)) != sizeof(result))
    {
        result = 0;
    }
    return result;
}

static BranchSizingBenchmarkCount branch_sizing_benchmark_count(const BranchSizingBenchmarkCounters* counters)
{
    BranchSizingBenchmarkCount result = {
        .cycles = branch_sizing_benchmark_read_counter(counters, 0),
        .instructions = branch_sizing_benchmark_read_counter(counters, 1),
    };
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Timing

// What a sample measures: x86_encode of every function (the reference), an entry point, or a phase-by-phase run on a
// path that stops after a phase.
typedef enum BranchSizingBenchmarkWork
{
    BRANCH_SIZING_BENCHMARK_ENCODE,
    BRANCH_SIZING_BENCHMARK_ENTRY,
    BRANCH_SIZING_BENCHMARK_THROUGH,
} BranchSizingBenchmarkWork;

typedef struct BranchSizingBenchmarkMeasurement BranchSizingBenchmarkMeasurement;
struct BranchSizingBenchmarkMeasurement
{
    BranchSizingBenchmarkWork work;
    BranchSizingBenchmarkPath path;
    uint32_t round_limit;
    uint32_t stop;
};

// The reference, the two entry points, and each path's runs through each phase.
#define BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES 3
#define BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS (BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES + BRANCH_SIZING_BENCHMARK_PATHS * BRANCH_SIZING_BENCHMARK_STOPS)

static const char* const branch_sizing_benchmark_entry_names[BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES] = {
    "x86_encode",
    "size_branches",
    "size_branches_scalar",
};

static BranchSizingBenchmarkMeasurement branch_sizing_benchmark_measurement(uint32_t index, uint32_t round_limit)
{
    const uint32_t through = index >= BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES ? index - BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES : 0;
    BranchSizingBenchmarkMeasurement result = {
        .work = index == 0 ? BRANCH_SIZING_BENCHMARK_ENCODE : index < BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES ? BRANCH_SIZING_BENCHMARK_ENTRY : BRANCH_SIZING_BENCHMARK_THROUGH,
        .path = index == 2 ? BRANCH_SIZING_BENCHMARK_SCALAR : index < BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES ? BRANCH_SIZING_BENCHMARK_VECTOR
                                                                                                          : (BranchSizingBenchmarkPath)(through / BRANCH_SIZING_BENCHMARK_STOPS),
        .round_limit = round_limit,
        .stop = through % BRANCH_SIZING_BENCHMARK_STOPS,
    };
    return result;
}

// The measurement --measure NAME names: an entry's name, or a path's and a stop's, as in "vector-setup". Returns
// BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS for none.
static uint32_t branch_sizing_benchmark_find_measurement(const char* name)
{
    uint32_t result = BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS;
    for (uint32_t m = 0; m < BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES; m += 1)
    {
        result = strcmp(name, branch_sizing_benchmark_entry_names[m]) == 0 ? m : result;
    }
    for (uint32_t path = 0; path < BRANCH_SIZING_BENCHMARK_PATHS; path += 1)
    {
        const uint64_t length = strlen(branch_sizing_benchmark_path_names[path]);
        if (strncmp(name, branch_sizing_benchmark_path_names[path], length) == 0 && name[length] == '-')
        {
            for (uint32_t stop = 0; stop < BRANCH_SIZING_BENCHMARK_STOPS; stop += 1)
            {
                const bool same = strcmp(name + length + 1, branch_sizing_benchmark_stop_names[stop]) == 0;
                result = same ? BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES + path * BRANCH_SIZING_BENCHMARK_STOPS + stop : result;
            }
        }
    }
    return result;
}

// Sizes (or encodes) the group's functions once. Returns the sum of the lengths, which keeps the work observable.
static __attribute__((noinline)) uint64_t branch_sizing_benchmark_group(BranchSizingBenchmarkPool* pool, uint64_t group, BranchSizingBenchmarkMeasurement measurement)
{
    uint64_t result = 0;
    const bool production = measurement.round_limit == BUSTER_BRANCH_SIZING_ROUNDS;
    const bool vector = measurement.path == BRANCH_SIZING_BENCHMARK_VECTOR;
    for (uint64_t f = pool->group_starts[group]; f < pool->group_starts[group + 1]; f += 1)
    {
        X86Instruction* records = pool->records + pool->record_starts[f];
        const uint64_t count = pool->record_starts[f + 1] - pool->record_starts[f];
        const uint32_t* branches = pool->branches + pool->branch_starts[f];
        const uint32_t* targets = pool->targets + pool->branch_starts[f];
        const uint64_t branch_count = pool->branch_starts[f + 1] - pool->branch_starts[f];
        if (measurement.work == BRANCH_SIZING_BENCHMARK_ENCODE)
        {
            result += (uint64_t)x86_encode(records, count, pool->encoded, BUSTER_X86_ENCODER_CAPACITY(count), pool->offsets, 0);
        }
        else if (measurement.work == BRANCH_SIZING_BENCHMARK_ENTRY && production && vector)
        {
            result += size_branches(records, count, branches, targets, branch_count, pool->offsets).length;
        }
        else if (measurement.work == BRANCH_SIZING_BENCHMARK_ENTRY && production)
        {
            result += size_branches_scalar(records, count, branches, targets, branch_count, pool->offsets).length;
        }
        else if (measurement.work == BRANCH_SIZING_BENCHMARK_ENTRY)
        {
            result += branch_sizing_run(records, count, branches, targets, branch_count, pool->offsets, vector, measurement.round_limit).length;
        }
        else
        {
            const BranchSizing sized = branch_sizing_benchmark_through(records, count, branches, targets, branch_count, pool->offsets, measurement.path,
                                                                       measurement.round_limit, measurement.stop);
            result += sized.length;
        }
    }
    return result;
}

// A sample: every group of the pool, each prepared outside the time, counted less the counters' own cost.
static BranchSizingBenchmarkCount branch_sizing_benchmark_sample(BranchSizingBenchmarkPool* pool, BranchSizingBenchmarkMeasurement measurement,
                                                                 const BranchSizingBenchmarkCounters* counters, BranchSizingBenchmarkCount overhead)
{
    BranchSizingBenchmarkCount result = { 0 };
    for (uint64_t group = 0; group < pool->groups; group += 1)
    {
        const uint32_t read = branch_sizing_benchmark_prepare(pool, group);
        __asm__ volatile("" : : "r"(read) : "memory");
        const BranchSizingBenchmarkCount begin = branch_sizing_benchmark_count(counters);
        const uint64_t length = branch_sizing_benchmark_group(pool, group, measurement);
        const BranchSizingBenchmarkCount end = branch_sizing_benchmark_count(counters);
        __asm__ volatile("" : : "r"(length) : "memory");
        result.cycles += end.cycles - begin.cycles - overhead.cycles;
        result.instructions += end.instructions - begin.instructions - overhead.instructions;
    }
    return result;
}

// The median of `count` values, sorted into `sorted`.
static uint64_t branch_sizing_benchmark_median(const uint64_t* values, uint64_t count, uint64_t* sorted)
{
    memcpy(sorted, values, count * sizeof(uint64_t));
    for (uint64_t i = 1; i < count; i += 1)
    {
        const uint64_t value = sorted[i];
        uint64_t j = i;
        while (j > 0 && sorted[j - 1] > value)
        {
            sorted[j] = sorted[j - 1];
            j -= 1;
        }
        sorted[j] = value;
    }
    return sorted[count / 2];
}

// What reading the counters costs: the medians of empty intervals.
static BranchSizingBenchmarkCount branch_sizing_benchmark_overhead(Arena* arena, const BranchSizingBenchmarkCounters* counters)
{
    const uint64_t mark = arena->length;
    uint64_t* cycles = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_BRANCH_SIZING_BENCHMARK_OVERHEAD_SAMPLES);
    uint64_t* instructions = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_BRANCH_SIZING_BENCHMARK_OVERHEAD_SAMPLES);
    uint64_t* sorted = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_BRANCH_SIZING_BENCHMARK_OVERHEAD_SAMPLES);
    BranchSizingBenchmarkCount result = { 0 };
    if (cycles != 0 && instructions != 0 && sorted != 0)
    {
        for (uint64_t i = 0; i < BUSTER_BRANCH_SIZING_BENCHMARK_OVERHEAD_SAMPLES; i += 1)
        {
            const BranchSizingBenchmarkCount begin = branch_sizing_benchmark_count(counters);
            const BranchSizingBenchmarkCount end = branch_sizing_benchmark_count(counters);
            cycles[i] = end.cycles - begin.cycles;
            instructions[i] = end.instructions - begin.instructions;
        }
        result.cycles = branch_sizing_benchmark_median(cycles, BUSTER_BRANCH_SIZING_BENCHMARK_OVERHEAD_SAMPLES, sorted);
        result.instructions = branch_sizing_benchmark_median(instructions, BUSTER_BRANCH_SIZING_BENCHMARK_OVERHEAD_SAMPLES, sorted);
    }
    arena_pop_to(arena, mark);
    return result;
}

// What a row runs: every measurement, or the one --measure names, `samples` times each.
typedef struct BranchSizingBenchmarkPlan BranchSizingBenchmarkPlan;
struct BranchSizingBenchmarkPlan
{
    uint32_t round_limit;
    uint32_t measure;
    uint64_t samples;
};

// One row: the pool, its checks and statistics, then the samples and the report.
static bool branch_sizing_benchmark_row(Arena* arena, const BranchSizingBenchmarkRow* row, uint64_t seed, BranchSizingBenchmarkPlan plan,
                                        const BranchSizingBenchmarkCounters* counters, BranchSizingBenchmarkCount overhead)
{
    const uint64_t mark = arena->length;
    BranchSizingBenchmarkPool pool = branch_sizing_benchmark_pool(arena, row, seed);
    BranchSizingBenchmarkStatistics statistics = { 0 };
    uint64_t* cycles = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS * plan.samples);
    uint64_t* instructions = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS * plan.samples);
    uint64_t* sorted = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, plan.samples);
    bool result = pool.valid && cycles != 0 && instructions != 0 && sorted != 0;
    if (!result)
    {
        fprintf(stderr, "benchmark-branch-sizing: no memory for %s's pool\n", row->name);
    }
    result = result && branch_sizing_benchmark_check(arena, &pool, plan.round_limit, &statistics);
    if (result)
    {
        const bool every = plan.measure == BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS;
        const uint32_t first = every ? 0 : plan.measure;
        const uint32_t measured = every ? BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS : 1;
        // A warm-up sample of each measurement, then the samples, each pass starting from another measurement.
        for (uint32_t m = 0; m < measured; m += 1)
        {
            branch_sizing_benchmark_sample(&pool, branch_sizing_benchmark_measurement(first + m, plan.round_limit), counters, overhead);
        }
        for (uint64_t pass = 0; pass < plan.samples; pass += 1)
        {
            for (uint32_t turn = 0; turn < measured; turn += 1)
            {
                const uint32_t m = first + (uint32_t)((pass + turn) % measured);
                const BranchSizingBenchmarkCount sample = branch_sizing_benchmark_sample(&pool, branch_sizing_benchmark_measurement(m, plan.round_limit), counters, overhead);
                cycles[m * plan.samples + pass] = sample.cycles;
                instructions[m * plan.samples + pass] = sample.instructions;
            }
        }
        const double branch_count = (double)(pool.branches_total != 0 ? pool.branches_total : 1);
        const double record_count = (double)pool.records_total;
        printf("%s: %llu functions in %llu groups, %llu records, %llu branches (a branch per %.2f records), %.2f bytes per record; %.1f%% of the branches "
               "long, %.2f rounds per function (at most %u), %llu with the worklist (%llu evaluations)\n",
               row->name, (unsigned long long)pool.functions, (unsigned long long)pool.groups, (unsigned long long)pool.records_total,
               (unsigned long long)pool.branches_total, record_count / branch_count, (double)statistics.bytes / record_count,
               100.0 * (double)statistics.long_branches / branch_count, (double)statistics.rounds / (double)pool.functions, statistics.most_rounds,
               (unsigned long long)statistics.listed, (unsigned long long)statistics.evaluations);
        for (uint32_t m = first; m < first + measured; m += 1)
        {
            const double c = (double)branch_sizing_benchmark_median(cycles + m * plan.samples, plan.samples, sorted);
            const double n = (double)branch_sizing_benchmark_median(instructions + m * plan.samples, plan.samples, sorted);
            if (m < BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES)
            {
                printf("  %-20s %8.2f cycles per branch %7.3f per record %5.2f IPC\n", branch_sizing_benchmark_entry_names[m], c / branch_count, c / record_count,
                       c != 0 ? n / c : 0);
            }
            else if (!every)
            {
                const BranchSizingBenchmarkMeasurement measurement = branch_sizing_benchmark_measurement(m, plan.round_limit);
                printf("  %s through %s: %8.2f cycles per branch %7.3f per record %5.2f IPC\n", branch_sizing_benchmark_path_names[measurement.path],
                       branch_sizing_benchmark_stop_names[measurement.stop], c / branch_count, c / record_count, c != 0 ? n / c : 0);
            }
        }
        for (uint32_t path = 0; every && path < BRANCH_SIZING_BENCHMARK_PATHS; path += 1)
        {
            printf("  %-20s", branch_sizing_benchmark_path_names[path]);
            double before = 0;
            for (uint32_t stop = 0; stop < BRANCH_SIZING_BENCHMARK_STOPS; stop += 1)
            {
                const uint32_t m = BUSTER_BRANCH_SIZING_BENCHMARK_ENTRIES + path * BRANCH_SIZING_BENCHMARK_STOPS + stop;
                const double c = (double)branch_sizing_benchmark_median(cycles + m * plan.samples, plan.samples, sorted);
                printf(" %s %.2f", branch_sizing_benchmark_stop_names[stop], (c - before) / branch_count);
                before = c;
            }
            printf(" (cycles per branch); whole %.3f per record\n", before / record_count);
        }
    }
    arena_pop_to(arena, mark);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// run_branch_sizing_benchmark

static bool branch_sizing_benchmark_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

int run_branch_sizing_benchmark(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t rounds = BUSTER_BRANCH_SIZING_ROUNDS;
    uint64_t samples = BUSTER_BRANCH_SIZING_BENCHMARK_PASSES;
    uint32_t measure = BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS;
    const char* only = 0;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && branch_sizing_benchmark_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc)
        {
            valid = valid && branch_sizing_benchmark_parse_u64(argv[i + 1], &rounds) && rounds >= 1 && rounds <= UINT32_MAX;
            i += 1;
        }
        else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
        {
            valid = valid && branch_sizing_benchmark_parse_u64(argv[i + 1], &samples) && samples >= 1 && samples <= BUSTER_BRANCH_SIZING_BENCHMARK_MAX_SAMPLES;
            i += 1;
        }
        else if (strcmp(argv[i], "--measure") == 0 && i + 1 < argc)
        {
            measure = branch_sizing_benchmark_find_measurement(argv[i + 1]);
            valid = valid && measure != BUSTER_BRANCH_SIZING_BENCHMARK_MEASUREMENTS;
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[i + 1];
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    bool known = only == 0;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(branch_sizing_benchmark_rows); r += 1)
    {
        known = known || strcmp(only, branch_sizing_benchmark_rows[r].name) == 0;
    }
    Arena arena = valid && known ? arena_reserve(BUSTER_BRANCH_SIZING_BENCHMARK_ARENA_BYTES) : (Arena){ 0 };
    bool passed = false;
    if (!valid || !known)
    {
        fprintf(stderr, "usage: benchmark-branch-sizing [--seed N] [--rounds N] [--only ROW] [--measure NAME [--samples N]]\n");
    }
    else if (arena.base == 0)
    {
        fprintf(stderr, "benchmark-branch-sizing: can't reserve the benchmark's arena\n");
    }
    else if (!x86_encoder_initialize())
    {
        fprintf(stderr, "benchmark-branch-sizing: the encoder's templates can't be built\n");
    }
    else
    {
        BranchSizingBenchmarkCounters counters = branch_sizing_benchmark_open_counters();
        if (counters.descriptors[0] < 0 || counters.descriptors[1] < 0)
        {
            fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
        }
        const BranchSizingBenchmarkCount overhead = branch_sizing_benchmark_overhead(&arena, &counters);
        printf("benchmark-branch-sizing: seed %llu, medians of %llu samples, round limit %llu, BUSTER_BRANCH_SIZING_CHUNK %u; reading the counters costs %llu "
               "cycles and %llu instructions\n",
               (unsigned long long)seed, (unsigned long long)samples, (unsigned long long)rounds, BUSTER_BRANCH_SIZING_CHUNK, (unsigned long long)overhead.cycles,
               (unsigned long long)overhead.instructions);
        const BranchSizingBenchmarkPlan plan = {
            .round_limit = (uint32_t)rounds,
            .measure = measure,
            .samples = samples,
        };
        passed = true;
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(branch_sizing_benchmark_rows); r += 1)
        {
            if (only == 0 || strcmp(only, branch_sizing_benchmark_rows[r].name) == 0)
            {
                passed = branch_sizing_benchmark_row(&arena, &branch_sizing_benchmark_rows[r], seed + r, plan, &counters, overhead) && passed;
            }
        }
        branch_sizing_benchmark_close_counters(&counters);
    }
    arena_release(&arena);
    return passed ? 0 : 1;
}
