// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Isolated #2610 uniform-successor frequency experiment. tier1_frequency_estimate consumes matching, immutable
// successor/predecessor CSR views and publishes Q32 expected visits only after all entry mass reaches a terminal.
// Rows are distinct-successor sets, as Tier1CfgInput requires: a switch's repeated cases do not multiply an edge.
// Both views must already describe the same graph. This helper checks their spans, endpoints and duplicate rows,
// but does not repeat the producer's transpose check. All inputs, workspace, report and output are disjoint.
//
// workspace is 8-byte aligned and contains 28*blocks writable bytes. Output has weight_capacity uint64 entries;
// it remains untouched on refusal. Neither input has padding. Masked vector tails stay inside logical bounds.
// No hidden allocation, profile, function-name gates or claim of profile-independent profitability is involved.

#include <immintrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BUSTER_TIER1_FREQUENCY_ONE (UINT64_C(1) << 32)
#define BUSTER_TIER1_FREQUENCY_ROUNDS 128u
#define BUSTER_TIER1_FREQUENCY_WORK (UINT64_C(8) << 20)

typedef enum Tier1FrequencyStatus
{
    TIER1_FREQUENCY_OK,
    TIER1_FREQUENCY_INVALID,
    TIER1_FREQUENCY_CAPACITY,
    TIER1_FREQUENCY_NONRETURNING,
    TIER1_FREQUENCY_UNCONVERGED,
    TIER1_FREQUENCY_WORK_LIMIT,
    TIER1_FREQUENCY_OVERFLOW,
} Tier1FrequencyStatus;

typedef struct Tier1FrequencyGraph Tier1FrequencyGraph;
struct Tier1FrequencyGraph
{
    const uint32_t* successor_offsets;
    const uint32_t* successors;
    const uint32_t* predecessor_offsets;
    const uint32_t* predecessors;
    uint32_t block_count;
    uint32_t edge_count;
    uint32_t entry;
};

typedef struct Tier1FrequencyLimits Tier1FrequencyLimits;
struct Tier1FrequencyLimits
{
    uint32_t rounds;
    uint64_t work;
};

typedef struct Tier1FrequencyStats Tier1FrequencyStats;
struct Tier1FrequencyStats
{
    uint32_t rounds;
    uint32_t reachable_blocks;
    uint64_t work;
    uint64_t absorbed_mass;
    uint64_t pending_mass;
};

// Work counts offset/flag rows and examined edges, including validation and reachability. Every full layout sweep
// also charges its block count, so an edgeless large graph cannot evade the bound. The limit is policy, not a profile.
static bool tier1_frequency_charge(uint64_t amount, uint64_t limit, Tier1FrequencyStats* stats)
{
    const bool ok = stats->work <= limit && amount <= limit - stats->work;
    if (ok)
    {
        stats->work += amount;
    }
    return ok;
}

static uint64_t tier1_frequency_workspace_bytes(uint32_t blocks)
{
    return UINT64_C(28) * blocks;
}

static Tier1FrequencyStatus tier1_frequency_view(const uint32_t* offsets, const uint32_t* targets, uint32_t blocks,
                                                uint32_t edges, uint32_t* stamps, uint64_t limit, Tier1FrequencyStats* stats)
{
    Tier1FrequencyStatus status = TIER1_FREQUENCY_OK;
    if (!tier1_frequency_charge((uint64_t)blocks + edges, limit, stats))
    {
        status = TIER1_FREQUENCY_WORK_LIMIT;
    }
    else if (offsets[0] != 0 || offsets[blocks] != edges)
    {
        status = TIER1_FREQUENCY_INVALID;
    }
    for (uint32_t b = 0; status == TIER1_FREQUENCY_OK && b < blocks;)
    {
        const uint32_t count = blocks - b < 16 ? blocks - b : 16;
        const __mmask16 lanes = (__mmask16)((1u << count) - 1);
        const __m512i first = _mm512_maskz_loadu_epi32(lanes, offsets + b);
        const __m512i last = _mm512_maskz_loadu_epi32(lanes, offsets + b + 1);
        const __mmask16 bad = _mm512_cmp_epu32_mask(first, last, _MM_CMPINT_GT) |
                              _mm512_cmp_epu32_mask(last, _mm512_set1_epi32(edges), _MM_CMPINT_GT);
        status = (bad & lanes) != 0 ? TIER1_FREQUENCY_INVALID : status;
        b += count;
    }
    if (status == TIER1_FREQUENCY_OK)
    {
        memset(stamps, 0, (uint64_t)blocks * sizeof(uint32_t));
    }
    for (uint32_t b = 0; status == TIER1_FREQUENCY_OK && b < blocks; b += 1)
    {
        const uint32_t end = offsets[b + 1];
        const __m512i stamp = _mm512_set1_epi32(b + 1);
        for (uint32_t e = offsets[b]; status == TIER1_FREQUENCY_OK && e < end;)
        {
            const uint32_t count = end - e < 16 ? end - e : 16;
            const __mmask16 lanes = (__mmask16)((1u << count) - 1);
            const __m512i ids = _mm512_maskz_loadu_epi32(lanes, targets + e);
            const __mmask16 outside = _mm512_cmp_epu32_mask(ids, _mm512_set1_epi32(blocks), _MM_CMPINT_NLT);
            const __mmask16 repeated = _mm512_test_epi32_mask(_mm512_conflict_epi32(ids), _mm512_set1_epi32(-1));
            if (((outside | repeated) & lanes) != 0)
            {
                status = TIER1_FREQUENCY_INVALID;
            }
            else
            {
                const __m512i seen = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, ids, stamps, 4);
                status = (_mm512_cmpeq_epi32_mask(seen, stamp) & lanes) != 0 ? TIER1_FREQUENCY_INVALID : status;
                _mm512_mask_i32scatter_epi32(stamps, lanes, ids, stamp, 4);
            }
            e += count;
        }
    }
    return status;
}

// The caller seeds the queue and sets its bit before this walk. Distinct CSR rows and marking before enqueue make
// every block appear at most once. The queue is exactly blocks entries; no inactive gather or store accesses it.
static Tier1FrequencyStatus tier1_frequency_walk(const uint32_t* offsets, const uint32_t* targets, uint32_t blocks,
                                                uint32_t* flags, uint32_t* queue, uint32_t queued, uint32_t bit,
                                                uint64_t limit, Tier1FrequencyStats* stats)
{
    Tier1FrequencyStatus status = TIER1_FREQUENCY_OK;
    for (uint32_t q = 0; status == TIER1_FREQUENCY_OK && q < queued; q += 1)
    {
        const uint32_t block = queue[q];
        const uint32_t first = offsets[block];
        const uint32_t end = offsets[block + 1];
        status = tier1_frequency_charge((uint64_t)(end - first) + 1, limit, stats) ? status : TIER1_FREQUENCY_WORK_LIMIT;
        for (uint32_t e = first; status == TIER1_FREQUENCY_OK && e < end;)
        {
            const uint32_t count = end - e < 16 ? end - e : 16;
            const __mmask16 lanes = (__mmask16)((1u << count) - 1);
            const __m512i ids = _mm512_maskz_loadu_epi32(lanes, targets + e);
            const __m512i seen = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, ids, flags, 4);
            const __m512i bits = _mm512_set1_epi32(bit);
            const __mmask16 fresh = lanes & ~_mm512_test_epi32_mask(seen, bits);
            const uint32_t added = (uint32_t)__builtin_popcount((uint32_t)fresh);
            if (added > blocks - queued)
            {
                status = TIER1_FREQUENCY_INVALID;
            }
            else
            {
                _mm512_mask_i32scatter_epi32(flags, fresh, ids, _mm512_or_si512(seen, bits), 4);
                _mm512_mask_compressstoreu_epi32(queue + queued, fresh, ids);
                queued += added;
            }
            e += count;
        }
    }
    return status;
}

static Tier1FrequencyStatus tier1_frequency_estimate(const Tier1FrequencyGraph* graph, const Tier1FrequencyLimits* limits,
                                                    void* workspace, uint64_t workspace_bytes, uint64_t* weights,
                                                    uint32_t weight_capacity, Tier1FrequencyStats* report)
{
    Tier1FrequencyStats stats = { 0 };
    Tier1FrequencyStatus status = graph != 0 && limits != 0 && weights != 0 && workspace != 0 ? TIER1_FREQUENCY_OK : TIER1_FREQUENCY_INVALID;
    const uint32_t blocks = graph != 0 ? graph->block_count : 0;
    const uint32_t edges = graph != 0 ? graph->edge_count : 0;
    const uint32_t entry = graph != 0 ? graph->entry : 0;
    const uint64_t work_limit = limits != 0 ? limits->work : 0;
    const uint32_t rounds = limits != 0 ? limits->rounds : 0;
    const uint32_t* const successor_offsets = graph != 0 ? graph->successor_offsets : 0;
    const uint32_t* const successors = graph != 0 ? graph->successors : 0;
    const uint32_t* const predecessor_offsets = graph != 0 ? graph->predecessor_offsets : 0;
    const uint32_t* const predecessors = graph != 0 ? graph->predecessors : 0;
    if (status == TIER1_FREQUENCY_OK && (blocks == 0 || blocks > INT32_MAX || entry >= blocks || rounds == 0 ||
        ((uintptr_t)workspace & 7) != 0 || successor_offsets == 0 || predecessor_offsets == 0 ||
        (edges != 0 && (successors == 0 || predecessors == 0))))
    {
        status = TIER1_FREQUENCY_INVALID;
    }
    if (status == TIER1_FREQUENCY_OK && (weight_capacity < blocks || workspace_bytes < tier1_frequency_workspace_bytes(blocks)))
    {
        status = TIER1_FREQUENCY_CAPACITY;
    }
    uint64_t* pending = workspace;
    uint64_t* visits = status == TIER1_FREQUENCY_OK ? pending + blocks : 0;
    uint32_t* rotors = status == TIER1_FREQUENCY_OK ? (uint32_t*)(visits + blocks) : 0;
    uint32_t* flags = status == TIER1_FREQUENCY_OK ? rotors + blocks : 0;
    uint32_t* queue = status == TIER1_FREQUENCY_OK ? flags + blocks : 0;
    if (status == TIER1_FREQUENCY_OK)
    {
        status = tier1_frequency_view(successor_offsets, successors, blocks, edges, flags, work_limit, &stats);
    }
    if (status == TIER1_FREQUENCY_OK)
    {
        status = tier1_frequency_view(predecessor_offsets, predecessors, blocks, edges, flags, work_limit, &stats);
    }
    if (status == TIER1_FREQUENCY_OK)
    {
        memset(flags, 0, (uint64_t)blocks * sizeof(uint32_t));
        flags[entry] = 1;
        queue[0] = entry;
        status = tier1_frequency_walk(successor_offsets, successors, blocks, flags, queue, 1, 1, work_limit, &stats);
    }
    uint32_t terminals = 0;
    if (status == TIER1_FREQUENCY_OK && !tier1_frequency_charge(blocks, work_limit, &stats))
    {
        status = TIER1_FREQUENCY_WORK_LIMIT;
    }
    for (uint32_t b = 0; status == TIER1_FREQUENCY_OK && b < blocks;)
    {
        const uint32_t count = blocks - b < 16 ? blocks - b : 16;
        const __mmask16 lanes = (__mmask16)((1u << count) - 1);
        const __m512i first = _mm512_maskz_loadu_epi32(lanes, successor_offsets + b);
        const __m512i last = _mm512_maskz_loadu_epi32(lanes, successor_offsets + b + 1);
        const __mmask16 sinks = lanes & _mm512_cmpeq_epi32_mask(first, last);
        const __m512i ids = _mm512_add_epi32(_mm512_set1_epi32(b), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
        const __m512i old = _mm512_maskz_loadu_epi32(lanes, flags + b);
        stats.reachable_blocks += (uint32_t)__builtin_popcount((uint32_t)(_mm512_test_epi32_mask(old, _mm512_set1_epi32(1)) & lanes));
        _mm512_mask_storeu_epi32(flags + b, sinks, _mm512_or_si512(old, _mm512_set1_epi32(2)));
        _mm512_mask_compressstoreu_epi32(queue + terminals, sinks, ids);
        terminals += (uint32_t)__builtin_popcount((uint32_t)sinks);
        b += count;
    }
    if (status == TIER1_FREQUENCY_OK)
    {
        status = tier1_frequency_walk(predecessor_offsets, predecessors, blocks, flags, queue, terminals, 2, work_limit, &stats);
    }
    if (status == TIER1_FREQUENCY_OK && !tier1_frequency_charge(blocks, work_limit, &stats))
    {
        status = TIER1_FREQUENCY_WORK_LIMIT;
    }
    for (uint32_t b = 0; status == TIER1_FREQUENCY_OK && b < blocks;)
    {
        const uint32_t count = blocks - b < 16 ? blocks - b : 16;
        const __mmask16 lanes = (__mmask16)((1u << count) - 1);
        const __m512i values = _mm512_maskz_loadu_epi32(lanes, flags + b);
        status = (_mm512_cmpeq_epi32_mask(values, _mm512_set1_epi32(1)) & lanes) != 0 ? TIER1_FREQUENCY_NONRETURNING : status;
        b += count;
    }
    if (status == TIER1_FREQUENCY_OK)
    {
        memset(pending, 0, (uint64_t)blocks * (2 * sizeof(uint64_t) + sizeof(uint32_t)));
        pending[entry] = BUSTER_TIER1_FREQUENCY_ONE;
        stats.pending_mass = BUSTER_TIER1_FREQUENCY_ONE;
    }
    for (uint32_t round = 0; status == TIER1_FREQUENCY_OK && stats.pending_mass != 0 && round < rounds; round += 1)
    {
        // Forward edges flow within the sweep; self/back edges wait for the next one. This is deliberately a bounded
        // layout heuristic: reordering blocks can change required work, and therefore refusal at the same budget.
        status = tier1_frequency_charge((uint64_t)blocks * 2, work_limit, &stats) ? status : TIER1_FREQUENCY_WORK_LIMIT;
        stats.rounds += status == TIER1_FREQUENCY_OK;
        for (uint32_t b = 0; status == TIER1_FREQUENCY_OK && b < blocks; b += 1)
        {
            const uint64_t mass = pending[b];
            pending[b] = 0; // Consume before distributing: self-loop deliveries belong to the next sweep.
            const uint32_t first = successor_offsets[b];
            const uint32_t degree = successor_offsets[b + 1] - first;
            if (mass > UINT64_MAX - visits[b])
            {
                status = TIER1_FREQUENCY_OVERFLOW;
            }
            else if (mass != 0)
            {
                visits[b] += mass;
                if (degree == 0)
                {
                    status = mass <= BUSTER_TIER1_FREQUENCY_ONE - stats.absorbed_mass ? status : TIER1_FREQUENCY_INVALID;
                    stats.absorbed_mass += status == TIER1_FREQUENCY_OK ? mass : 0;
                }
                else
                {
                    const uint64_t share = mass / degree;
                    const uint32_t remainder = (uint32_t)(mass % degree);
                    const uint32_t start = rotors[b];
                    const uint32_t room = degree - start;
                    rotors[b] = remainder < room ? start + remainder : remainder - room;
                    status = tier1_frequency_charge(degree, work_limit, &stats) ? status : TIER1_FREQUENCY_WORK_LIMIT;
                    for (uint32_t j = 0; status == TIER1_FREQUENCY_OK && j < degree;)
                    {
                        const uint32_t count = degree - j < 8 ? degree - j : 8;
                        const __mmask8 lanes = (__mmask8)((1u << count) - 1);
                        const __m256i ids = _mm256_maskz_loadu_epi32(lanes, successors + first + j);
                        const __m256i ordinal = _mm256_add_epi32(_mm256_set1_epi32(j), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
                        const __m256i starts = _mm256_set1_epi32(start);
                        const __mmask8 wrapped = _mm256_cmp_epu32_mask(ordinal, starts, _MM_CMPINT_LT);
                        const __m256i distance = _mm256_mask_add_epi32(_mm256_sub_epi32(ordinal, starts), wrapped,
                                                                      _mm256_sub_epi32(ordinal, starts), _mm256_set1_epi32(degree));
                        const __mmask8 bonus = lanes & _mm256_cmp_epu32_mask(distance, _mm256_set1_epi32(remainder), _MM_CMPINT_LT);
                        const __m512i added = _mm512_mask_add_epi64(_mm512_set1_epi64(share), bonus, _mm512_set1_epi64(share), _mm512_set1_epi64(1));
                        const __m512i before = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), lanes, ids, pending, 8);
                        const __m512i after = _mm512_add_epi64(before, added);
                        if ((_mm512_cmp_epu64_mask(after, before, _MM_CMPINT_LT) & lanes) != 0)
                        {
                            status = TIER1_FREQUENCY_OVERFLOW;
                        }
                        else
                        {
                            _mm512_mask_i32scatter_epi64(pending, lanes, ids, after, 8);
                        }
                        j += count;
                    }
                }
            }
        }
        uint64_t residual = 0;
        for (uint32_t b = 0; status == TIER1_FREQUENCY_OK && b < blocks;)
        {
            const uint32_t count = blocks - b < 8 ? blocks - b : 8;
            const __mmask8 lanes = (__mmask8)((1u << count) - 1);
            const __m512i values = _mm512_maskz_loadu_epi64(lanes, pending + b);
            const uint64_t sum = (uint64_t)_mm512_reduce_add_epi64(values);
            if ((_mm512_cmp_epu64_mask(values, _mm512_set1_epi64(BUSTER_TIER1_FREQUENCY_ONE), _MM_CMPINT_GT) & lanes) != 0 ||
                sum > BUSTER_TIER1_FREQUENCY_ONE - residual)
            {
                status = TIER1_FREQUENCY_INVALID;
            }
            else
            {
                residual += sum;
            }
            b += count;
        }
        if (status == TIER1_FREQUENCY_OK)
        {
            stats.pending_mass = residual;
            status = residual == BUSTER_TIER1_FREQUENCY_ONE - stats.absorbed_mass ? status : TIER1_FREQUENCY_INVALID;
        }
    }
    if (status == TIER1_FREQUENCY_OK && stats.pending_mass != 0)
    {
        status = TIER1_FREQUENCY_UNCONVERGED;
    }
    if (status == TIER1_FREQUENCY_OK)
    {
        memcpy(weights, visits, (uint64_t)blocks * sizeof(uint64_t));
    }
    if (report != 0)
    {
        *report = stats;
    }
    return status;
}

// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Include after frequency.c and the existing Arena/Tier1CfgInput definitions. The CFG has already passed the
// normal tier1_cfg_input_valid transpose check. Input storage, scratch and weights' pointer cell are disjoint.
// Success retains only the weight column; refusal preserves both the prior arena length and pointer cell.
static bool tier1_emit_exchange_frequency(const Tier1CfgInput* input, Arena* scratch, uint64_t** weights)
{
    const uint64_t mark = scratch->length;
    const uint32_t blocks = input->block_count;
    const Tier1FrequencyGraph graph = {
        .successor_offsets = input->successor_offsets,
        .successors = input->successors,
        .predecessor_offsets = input->predecessor_offsets,
        .predecessors = input->predecessors,
        .block_count = blocks,
        .edge_count = input->edge_count,
        .entry = 0,
    };
    const Tier1FrequencyLimits limits = {
        .rounds = BUSTER_TIER1_FREQUENCY_ROUNDS,
        .work = BUSTER_TIER1_FREQUENCY_WORK,
    };
    uint64_t* result = arena_push_array(scratch, blocks, sizeof(uint64_t), _Alignof(uint64_t));
    const uint64_t retained = scratch->length;
    const uint64_t bytes = tier1_frequency_workspace_bytes(blocks);
    void* workspace = result != 0 ? arena_push_array(scratch, bytes, 1, _Alignof(uint64_t)) : 0;
    const bool ok = workspace != 0 && tier1_frequency_estimate(&graph, &limits, workspace, bytes, result, blocks, 0) == TIER1_FREQUENCY_OK;
    if (ok)
    {
        arena_pop_to(scratch, retained);
        *weights = result;
    }
    else
    {
        arena_pop_to(scratch, mark);
    }
    return ok;
}
