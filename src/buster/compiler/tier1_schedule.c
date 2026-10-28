// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Experimental postallocation block scheduling (#172), using the original measured operand cases of #181.
// Include after x86_64_encoder.c, x86_semantics.c and x86_zen5.c, and initialize their tables before use.
// tier1_schedule_block produces an order[new_position] = old_position permutation, never changes instructions,
// and uses caller-owned reusable scratch. Its bounded physical-register graph preserves RAW, WAR and WAW hazards.
// Memory accesses, possible faults, flags consumers, unknowns and caller pins are full ordering barriers. An adjacent
// cmp/test and conditional branch remains adjacent. There is no alias speculation, pipe model or invented latency.
// The weighted heights are priorities, not cycle predictions: a barrier's priority is zero and does not use its
// unknown latency. Only GPR RAW edges carry result latency; flags and other ordering edges never get that latency.
// AVX-512 resolves effects, compares eight prior records at once and reduces contiguous height columns by level.
// The scalar twin computes heights backwards; both list schedulers break equal priorities by original position.
// tier1_schedule_verify independently reconstructs scalar effects and checks the resulting permutation and hazards.
// This is opt-in infrastructure. Source lowering, preallocation scheduling and corpus runtime/compile-cost evidence
// remain acceptance work; docs/tier1-scheduling.md describes the boundary and the scratch/tail contract.

#ifndef BUSTER_TIER1_SCHEDULE_C
#define BUSTER_TIER1_SCHEDULE_C

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_TIER1_SCHEDULE_MAX_RECORDS 256
#define BUSTER_TIER1_SCHEDULE_WORDS (BUSTER_TIER1_SCHEDULE_MAX_RECORDS / 64)

typedef enum Tier1ScheduleStatus
{
    TIER1_SCHEDULE_OK,
    TIER1_SCHEDULE_INVALID_ARGUMENT,
    TIER1_SCHEDULE_CAPACITY,
} Tier1ScheduleStatus;

typedef struct Tier1ScheduleInput Tier1ScheduleInput;
struct Tier1ScheduleInput
{
    const X86Instruction* instructions;
    // Optional count-byte column for labels, source anchors or further caller ordering requirements.
    const uint8_t* pinned;
    uint32_t count;
};

typedef struct Tier1ScheduleStats Tier1ScheduleStats;
struct Tier1ScheduleStats
{
    uint32_t records;
    uint32_t pinned;
    uint32_t unknown_cases;
    uint32_t missing_columns[X86_COST_COLUMN_COUNT];
    uint32_t edges;
    uint32_t moved;
    uint32_t levels;
};

typedef struct Tier1ScheduleGraph Tier1ScheduleGraph;
struct Tier1ScheduleGraph
{
    // Each row has fixed stride WORDS; only ceil(count / 64) words and count bits are logically active.
    uint64_t predecessors[BUSTER_TIER1_SCHEDULE_MAX_RECORDS][BUSTER_TIER1_SCHEDULE_WORDS];
    uint64_t successors[BUSTER_TIER1_SCHEDULE_MAX_RECORDS][BUSTER_TIER1_SCHEDULE_WORDS];
    uint64_t data_successors[BUSTER_TIER1_SCHEDULE_MAX_RECORDS][BUSTER_TIER1_SCHEDULE_WORDS];
    uint64_t reads[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint64_t writes[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint64_t barriers[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint32_t heights[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint16_t levels[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint16_t latencies[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint8_t cost_ids[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
};

static bool tier1_schedule_fusion_pair(Tier1ScheduleInput input, uint32_t index)
{
    bool pair = false;
    if (index + 1 < input.count)
    {
        const X86Effects* left = x86_effects(input.instructions[index].form);
        const X86Effects* right = x86_effects(input.instructions[index + 1].form);
        pair = (left->operation == X86_OPERATION_CMP || left->operation == X86_OPERATION_TEST) && x86_effects_control(right) == BUSTER_X86_CONTROL_BRANCH;
    }
    return pair;
}

static bool tier1_schedule_is_pinned(Tier1ScheduleInput input, uint32_t index, uint64_t reads, uint64_t writes, uint16_t latency)
{
    const X86Instruction* instruction = &input.instructions[index];
    const X86Effects* effects = x86_effects(instruction->form);
    const uint64_t special = BUSTER_X86_RESOURCE_MEMORY | BUSTER_X86_RESOURCE_CONTROL | BUSTER_X86_RESOURCE_CALL | BUSTER_X86_RESOURCE_MXCSR;
    const uint16_t faults = BUSTER_X86_EFFECT_FAULT | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_MXCSR;
    const bool pair = tier1_schedule_fusion_pair(input, index);
    // Explicit memory operations are pinned, including read/read pairs: ordering faults or volatile accesses cannot
    // be justified by a register/memory dependency test alone. LEA does not touch the memory resource.
    const bool pinned = (input.pinned && input.pinned[index]) || latency == BUSTER_X86_COST_UNKNOWN || effects->operation == X86_OPERATION_NONE ||
                        instruction->decorations != 0 || (effects->properties & faults) != 0 || ((reads | writes) & special) != 0 ||
                        (reads & BUSTER_X86_RESOURCE_FLAGS) != 0 || pair;
    return pinned;
}

static void tier1_schedule_add_edges(Tier1ScheduleGraph* graph, uint32_t current, uint32_t first, uint32_t mask, uint32_t data_mask, Tier1ScheduleStats* stats)
{
    graph->predecessors[current][first / 64] |= (uint64_t)mask << (first & 63);
    stats->edges += (uint32_t)__builtin_popcount(mask);
    while (mask != 0)
    {
        const uint32_t previous = first + (uint32_t)__builtin_ctz(mask);
        graph->successors[previous][current / 64] |= UINT64_C(1) << (current & 63);
        if ((data_mask >> (previous - first)) & 1)
        {
            graph->data_successors[previous][current / 64] |= UINT64_C(1) << (current & 63);
        }
        mask &= mask - 1;
    }
}

static void tier1_schedule_graph_scalar(Tier1ScheduleGraph* graph, uint32_t count, Tier1ScheduleStats* stats)
{
    #pragma clang loop vectorize(disable) interleave(disable)
    for (uint32_t current = 0; current < count; current += 1)
    {
        for (uint32_t previous = 0; previous < current; previous += 1)
        {
            const bool hazard = (graph->writes[previous] & (graph->reads[current] | graph->writes[current])) != 0 ||
                                (graph->reads[previous] & graph->writes[current]) != 0 || graph->barriers[previous] != 0 || graph->barriers[current] != 0;
            if (hazard)
            {
                graph->predecessors[current][previous / 64] |= UINT64_C(1) << (previous & 63);
                graph->successors[previous][current / 64] |= UINT64_C(1) << (current & 63);
                if (graph->writes[previous] & graph->reads[current] & BUSTER_X86_RESOURCE_GPRS)
                {
                    graph->data_successors[previous][current / 64] |= UINT64_C(1) << (current & 63);
                }
                stats->edges += 1;
            }
        }
    }
}

static void tier1_schedule_graph_vector(Tier1ScheduleGraph* graph, uint32_t count, Tier1ScheduleStats* stats)
{
#if !BUSTER_HOST_VECTOR
    tier1_schedule_graph_scalar(graph, count, stats);
#else
    for (uint32_t current = 0; current < count; current += 1)
    {
        const __m512i written = _mm512_set1_epi64((int64_t)graph->writes[current]);
        const __m512i accessed = _mm512_set1_epi64((int64_t)(graph->reads[current] | graph->writes[current]));
        const __m512i gpr_reads = _mm512_set1_epi64((int64_t)(graph->reads[current] & BUSTER_X86_RESOURCE_GPRS));
        for (uint32_t first = 0; first < current; first += 8)
        {
            const uint32_t remaining = current - first < 8 ? current - first : 8;
            const __mmask8 active = (__mmask8)((1u << remaining) - 1);
            const __m512i reads = _mm512_maskz_loadu_epi64(active, graph->reads + first);
            const __m512i writes = _mm512_maskz_loadu_epi64(active, graph->writes + first);
            const __m512i pins = _mm512_maskz_loadu_epi64(active, graph->barriers + first);
            __mmask8 hazards = _mm512_test_epi64_mask(writes, accessed) | _mm512_test_epi64_mask(reads, written) | _mm512_test_epi64_mask(pins, pins);
            hazards |= graph->barriers[current] != 0 ? active : 0;
            const __mmask8 data = _mm512_test_epi64_mask(writes, gpr_reads) & active;
            tier1_schedule_add_edges(graph, current, first, hazards & active, data, stats);
        }
    }
#endif
}

static bool tier1_schedule_intersects(const uint64_t* left, const uint64_t* right, uint32_t words)
{
    uint64_t overlap = 0;
    for (uint32_t word = 0; word < words; word += 1)
    {
        overlap |= left[word] & right[word];
    }
    return overlap != 0;
}

static void tier1_schedule_pending(uint64_t* pending, uint32_t count)
{
    for (uint32_t word = 0; word < BUSTER_TIER1_SCHEDULE_WORDS; word += 1)
    {
        const uint32_t first = word * 64;
        const uint32_t remaining = count > first ? count - first : 0;
        pending[word] = remaining >= 64 ? UINT64_MAX : remaining != 0 ? (UINT64_C(1) << remaining) - 1 : 0;
    }
}

static void tier1_schedule_heights_scalar(Tier1ScheduleGraph* graph, uint32_t count, Tier1ScheduleStats* stats)
{
    for (uint32_t remaining = count; remaining != 0; remaining -= 1)
    {
        const uint32_t current = remaining - 1;
        uint32_t height = 0;
        uint16_t level = 1;
        for (uint32_t next = current + 1; next < count; next += 1)
        {
            if ((graph->successors[current][next / 64] >> (next & 63)) & 1)
            {
                level = level > graph->levels[next] ? level : graph->levels[next] + 1;
            }
            if ((graph->data_successors[current][next / 64] >> (next & 63)) & 1)
            {
                height = height > graph->heights[next] ? height : graph->heights[next];
            }
        }
        // A pinned node ends the weighted path. The value zero is a selection priority, not a modeled zero latency.
        graph->heights[current] = graph->barriers[current] ? 0 : graph->latencies[current] + height;
        graph->levels[current] = level;
        stats->levels = stats->levels > level ? stats->levels : level;
    }
}

static void tier1_schedule_heights_vector(Tier1ScheduleGraph* graph, uint32_t count, Tier1ScheduleStats* stats)
{
#if !BUSTER_HOST_VECTOR
    tier1_schedule_heights_scalar(graph, count, stats);
#else
    uint64_t pending[BUSTER_TIER1_SCHEDULE_WORDS];
    tier1_schedule_pending(pending, count);
    const uint32_t words = (count + 63) / 64;
    uint32_t left = count;
    uint16_t level = 0;
    while (left != 0)
    {
        uint64_t frontier[BUSTER_TIER1_SCHEDULE_WORDS] = { 0 };
        for (uint32_t current = 0; current < count; current += 1)
        {
            const uint64_t bit = UINT64_C(1) << (current & 63);
            if ((pending[current / 64] & bit) && !tier1_schedule_intersects(graph->successors[current], pending, words))
            {
                frontier[current / 64] |= bit;
            }
        }
        level += 1;
        // Every successor of this frontier belongs to an earlier, completed level. No gather or padded read is needed.
        for (uint32_t word = 0; word < words; word += 1)
        {
            uint64_t nodes = frontier[word];
            while (nodes != 0)
            {
                const uint32_t current = word * 64 + (uint32_t)__builtin_ctzll(nodes);
                uint32_t height = 0;
                if (!graph->barriers[current])
                {
                    __m512i maxima = _mm512_setzero_si512();
                    for (uint32_t first = 0; first < count; first += 16)
                    {
                        const __mmask16 successors = (__mmask16)(graph->data_successors[current][first / 64] >> (first & 63));
                        maxima = _mm512_max_epu32(maxima, _mm512_maskz_loadu_epi32(successors, graph->heights + first));
                    }
                    height = _mm512_reduce_max_epu32(maxima) + graph->latencies[current];
                }
                graph->heights[current] = height;
                graph->levels[current] = level;
                nodes &= nodes - 1;
                left -= 1;
            }
            pending[word] &= ~frontier[word];
        }
    }
    stats->levels = level;
#endif
}

static void tier1_schedule_order(const Tier1ScheduleGraph* graph, uint32_t count, uint16_t* order, Tier1ScheduleStats* stats)
{
    uint64_t pending[BUSTER_TIER1_SCHEDULE_WORDS];
    tier1_schedule_pending(pending, count);
    const uint32_t words = (count + 63) / 64;
    // Selection follows one dependency at a time. Its graph, costs and level reductions are batched above; the scalar
    // selection order is deliberate and measured separately from a claim that many-block SIMD scheduling would pay.
    for (uint32_t output = 0; output < count; output += 1)
    {
        uint32_t best = count;
        uint32_t priority = 0;
        for (uint32_t current = 0; current < count; current += 1)
        {
            const bool remains = (pending[current / 64] >> (current & 63)) & 1;
            if (remains && !tier1_schedule_intersects(graph->predecessors[current], pending, words) && (best == count || graph->heights[current] > priority))
            {
                best = current;
                priority = graph->heights[current];
            }
        }
        // Edges always point forward in original order, so a ready node exists in every nonempty remaining graph.
        order[output] = (uint16_t)best;
        pending[best / 64] &= ~(UINT64_C(1) << (best & 63));
        stats->moved += best != output;
    }
}

// Slices have precisely count readable records/optional pin bytes and capacity writable uint16_t elements, without
// required padding or alignment beyond their C types. All slices, graph and stats are disjoint. The caller owns them.
// Zero count permits null pointers. Rejection preserves order and stats and touches no input; callers retain their
// original block for counts above the bound. Encoding and all branch/relocation offsets are finalized afterwards.
static __attribute__((unused)) Tier1ScheduleStatus tier1_schedule_block(Tier1ScheduleInput input, Tier1ScheduleGraph* graph, uint16_t* order, uint32_t capacity, bool vector, Tier1ScheduleStats* statistics)
{
    (void)vector;

    Tier1ScheduleStatus status = TIER1_SCHEDULE_OK;
    if (input.count > BUSTER_TIER1_SCHEDULE_MAX_RECORDS || capacity < input.count)
    {
        status = TIER1_SCHEDULE_CAPACITY;
    }
    else if (input.count != 0 && (!input.instructions || !graph || !order))
    {
        status = TIER1_SCHEDULE_INVALID_ARGUMENT;
    }
    if (status == TIER1_SCHEDULE_OK)
    {
        Tier1ScheduleStats stats = { .records = input.count };
        if (input.count != 0)
        {
            memset(graph->predecessors, 0, input.count * sizeof(graph->predecessors[0]));
            memset(graph->successors, 0, input.count * sizeof(graph->successors[0]));
            memset(graph->data_successors, 0, input.count * sizeof(graph->data_successors[0]));
            if (BUSTER_VECTOR_PATH(vector))
            {
                x86_instructions_effects(input.instructions, input.count, graph->reads, graph->writes);
            }
            else
            {
                x86_instructions_effects_scalar(input.instructions, input.count, graph->reads, graph->writes);
            }
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                const uint8_t id = x86_zen5_id(&input.instructions[i]);
                graph->cost_ids[i] = id;
                stats.unknown_cases += id == X86_COST_NONE;
                for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
                {
                    stats.missing_columns[column] += x86_zen5_columns[column][id] == BUSTER_X86_COST_UNKNOWN;
                }
            }
            if (BUSTER_VECTOR_PATH(vector))
            {
                x86_zen5_lookup(graph->cost_ids, input.count, X86_COST_GPR_LATENCY, graph->latencies);
            }
            else
            {
                x86_zen5_lookup_scalar(graph->cost_ids, input.count, X86_COST_GPR_LATENCY, graph->latencies);
            }
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                const bool pinned = tier1_schedule_is_pinned(input, i, graph->reads[i], graph->writes[i], graph->latencies[i]);
                graph->barriers[i] = pinned ? UINT64_MAX : 0;
                stats.pinned += pinned;
            }
            if (BUSTER_VECTOR_PATH(vector))
            {
                tier1_schedule_graph_vector(graph, input.count, &stats);
                tier1_schedule_heights_vector(graph, input.count, &stats);
            }
            else
            {
                tier1_schedule_graph_scalar(graph, input.count, &stats);
                tier1_schedule_heights_scalar(graph, input.count, &stats);
            }
            tier1_schedule_order(graph, input.count, order, &stats);
        }
        if (statistics)
        {
            *statistics = stats;
        }
    }
    return status;
}

// Independent scalar verifier: no graph, heights or scheduler-produced effects are trusted. Validates the complete
// permutation first, then architectural resource ordering, barriers and compare/branch adjacency from the inputs.
static __attribute__((unused)) bool tier1_schedule_verify(Tier1ScheduleInput input, const uint16_t* order)
{
    bool valid = input.count <= BUSTER_TIER1_SCHEDULE_MAX_RECORDS && (input.count == 0 || (input.instructions && order));
    uint16_t positions[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint64_t reads[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint64_t writes[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    bool pins[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    if (valid)
    {
        memset(positions, 0xff, input.count * sizeof(positions[0]));
        for (uint32_t i = 0; valid && i < input.count; i += 1)
        {
            valid = order[i] < input.count;
            if (valid)
            {
                valid = positions[order[i]] == UINT16_MAX;
                positions[order[i]] = (uint16_t)i;
            }
        }
    }
    if (valid)
    {
        for (uint32_t i = 0; i < input.count; i += 1)
        {
            x86_instruction_effects_scalar(&input.instructions[i], &reads[i], &writes[i]);
            const uint8_t id = x86_zen5_id(&input.instructions[i]);
            pins[i] = tier1_schedule_is_pinned(input, i, reads[i], writes[i], x86_zen5_columns[X86_COST_GPR_LATENCY][id]);
        }
        for (uint32_t current = 0; valid && current < input.count; current += 1)
        {
            if (tier1_schedule_fusion_pair(input, current))
            {
                valid = positions[current] + 1 == positions[current + 1];
            }
            for (uint32_t previous = 0; valid && previous < current; previous += 1)
            {
                const bool raw = (writes[previous] & reads[current]) != 0;
                const bool war = (reads[previous] & writes[current]) != 0;
                const bool waw = (writes[previous] & writes[current]) != 0;
                valid = !(raw || war || waw || pins[previous] || pins[current]) || positions[previous] < positions[current];
            }
        }
    }
    return valid;
}

#endif
