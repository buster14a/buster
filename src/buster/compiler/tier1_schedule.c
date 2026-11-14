// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Postallocation block scheduling (#172), using the original measured operand cases of #181 where they exist.
// Include after x86_64_encoder.c, x86_semantics.c and x86_zen5.c, and initialize their tables before use.
// tier1_schedule_block produces an order[new_position] = old_position permutation, never changes instructions,
// and uses caller-owned reusable scratch. Its bounded physical-register graph preserves RAW, WAR and WAW hazards over
// registers, flags and memory, where any two memory accesses are dependent (no alias analysis) and two writers of flags
// no record reads before they are written again are not (tier1_schedule_live_flags). Possible faults, control transfers,
// MXCSR's readers and writers, fences, atomics, unknowns and caller pins are full ordering barriers; loads, stores and
// floating operations move among the other records. An adjacent cmp/test and conditional branch remains adjacent.
// The weighted heights are priorities, not cycle predictions: a register's or a flag's RAW edge carries the writer's
// measured latency, or its class's weight where the model has none (tier1_schedule_weight); a barrier's is zero.
// AVX-512 resolves effects, compares eight prior records at once and reduces contiguous height columns by level.
// The scalar twin computes heights backwards; both list schedulers break equal priorities by original position.
// tier1_schedule_verify independently reconstructs scalar effects and checks the resulting permutation, the register
// and memory hazards and each flag's reaching writers. The C driver's --tier1-schedule=post runs it on every block of
// Tier 1's code (c_check_tier1_schedule.c); docs/tier1-scheduling.md describes the boundary, the scratch/tail contract and
// the 9700X's measurements.

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
    // Records whose flag writes are all overwritten before any read (tier1_schedule_live_flags).
    uint32_t dead_flags;
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
    // writes less a dead flag writer's flags (tier1_schedule_live_flags).
    uint64_t live_writes[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
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

// The resources the scheduler orders a record by, from its operand-resolved effects (x86_semantics.c): every memory
// access both reads and writes memory, so that no two accesses change places (no alias analysis, and volatile accesses
// keep their order); and a record that may leave the block reads every flag, which the block's successors may read.
static void tier1_schedule_adjust(uint64_t* reads, uint64_t* writes)
{
    const uint64_t memory = (*reads | *writes) & BUSTER_X86_RESOURCE_MEMORY;
    *reads |= memory | ((*writes & BUSTER_X86_RESOURCE_CONTROL) ? BUSTER_X86_RESOURCE_FLAGS : 0);
    *writes |= memory;
}

// The records nothing moves across, and which move across nothing: a caller's pin; an uncovered form, or a decorated
// one (an opmask, a rounding override, a segment); a possible arithmetic fault or trap; anything that may leave the
// block (a branch, call, return or trap) or changes MXCSR; stmxcsr, which reads the floating-point status flags that
// every floating operation may set; a fence or an atomic access; endbr64, the first record of its target; and a compare
// or test right before a conditional branch, so that the pair stays adjacent and fusable. Loads, stores and floating
// operations move: their status flags are sticky, so their order among themselves leaves MXCSR the same, and the
// accesses keep their order (tier1_schedule_adjust).
static bool tier1_schedule_is_pinned(Tier1ScheduleInput input, uint32_t index, uint64_t reads, uint64_t writes)
{
    const X86Instruction* instruction = &input.instructions[index];
    const X86Effects* effects = x86_effects(instruction->form);
    const uint64_t special = BUSTER_X86_RESOURCE_CONTROL | BUSTER_X86_RESOURCE_CALL | BUSTER_X86_RESOURCE_MXCSR;
    const uint8_t ordering = BUSTER_X86_MEMORY_EFFECT_ATOMIC | BUSTER_X86_MEMORY_EFFECT_FENCE;
    // Register transfers can replace compiler-private m80 spills/reloads (#1387). They keep the original waiting
    // points, even though they no longer carry a memory resource: no access may move before a pending x87 trap.
    const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
    const bool x87_transfer = form == X86_FORM_FLD_ST || form == X86_FORM_FSTP_ST;
    const bool operation = effects->operation == X86_OPERATION_NONE || effects->operation == X86_OPERATION_STORE_MXCSR || effects->operation == X86_OPERATION_ENDBR ||
                           effects->operation == X86_OPERATION_SYSTEM;
    const bool pinned = (input.pinned && input.pinned[index]) || operation || x87_transfer || instruction->decorations != 0 || (effects->properties & BUSTER_X86_EFFECT_FAULT) != 0 ||
                        (writes & special) != 0 || (reads & BUSTER_X86_RESOURCE_CALL) != 0 || (effects->memory & ordering) != 0 ||
                        tier1_schedule_fusion_pair(input, index);
    return pinned;
}

// A record's priority weight, cycles * 256, where the measured model (x86_zen5.c, #181) has no latency for its operand
// case (a form without a row, or a case without the path: a constant's move, a store): the measured rows' classes for
// the integer forms (an ALU operation 1 cycle, a multiplication 3, a scaled lea 2, and an L1 load 4, the load taking the
// operation's place in a move and adding to it in another form); and for the floating ones weights of their usual
// order (an addition, multiplication or conversion 3, a fused multiply-add 4, a division 13, a square root 15). Weights order a block's ready records by the
// longest path after them; they are not cycle predictions, and a better model changes only which ready record is first.
static uint16_t tier1_schedule_weight(const X86Instruction* instruction, bool load)
{
    const X86Effects* effects = x86_effects(instruction->form);
    uint32_t weight = BUSTER_X86_COST_SCALE;
    bool move = false;
    switch (effects->operation)
    {
        case X86_OPERATION_MUL:
        case X86_OPERATION_IMUL:
        case X86_OPERATION_FADD:
        case X86_OPERATION_FSUB:
        case X86_OPERATION_FMUL:
        case X86_OPERATION_FMIN:
        case X86_OPERATION_FMAX:
        case X86_OPERATION_FROUND:
        case X86_OPERATION_FCOMPARE:
        case X86_OPERATION_FCOMPARE_SIGNALING:
        case X86_OPERATION_FCOMPARE_MASK:
        case X86_OPERATION_INT_TO_FLOAT:
        case X86_OPERATION_FLOAT_TO_INT:
        case X86_OPERATION_FLOAT_TO_FLOAT:
        case X86_OPERATION_X87_ARITHMETIC:
            weight = 3 * BUSTER_X86_COST_SCALE;
            break;
        case X86_OPERATION_FMA:
            weight = 4 * BUSTER_X86_COST_SCALE;
            break;
        case X86_OPERATION_FDIV:
            weight = 13 * BUSTER_X86_COST_SCALE;
            break;
        case X86_OPERATION_FSQRT:
            weight = 15 * BUSTER_X86_COST_SCALE;
            break;
        case X86_OPERATION_LEA:
            weight = (instruction->mode & BUSTER_X86_MODE_INDEX) && (instruction->mode & BUSTER_X86_MODE_SCALE) != 0 ? 2 * BUSTER_X86_COST_SCALE : BUSTER_X86_COST_SCALE;
            break;
        case X86_OPERATION_MOV:
        case X86_OPERATION_MOVZX:
        case X86_OPERATION_MOVSX:
        case X86_OPERATION_FMOVE:
        case X86_OPERATION_MOVD:
        case X86_OPERATION_VECTOR_MOVE:
        case X86_OPERATION_POP:
            move = true;
            break;
        default:
            break;
    }
    const uint32_t memory = 4 * BUSTER_X86_COST_SCALE;
    weight = load ? (move ? memory : weight + memory) : weight;
    return (uint16_t)weight;
}

// What a record's flag writes are worth keeping in order: the flags each record writes that a later record of the block
// reads before another writes them, or that are live at its end (all of them: the successors' reads aren't known
// here). A record whose written flags are all dead (overwritten before any read) needn't stay in order with another
// such record; its order with every live writer, reader and other resource stays. `live_writes` is `writes` less the
// flags of a dead writer. One backward pass over the block, in both paths.
static void tier1_schedule_live_flags(const uint64_t* reads, const uint64_t* writes, uint32_t count, uint64_t* live_writes)
{
    uint64_t live = BUSTER_X86_RESOURCE_FLAGS;
    for (uint32_t remaining = count; remaining != 0; remaining -= 1)
    {
        const uint32_t i = remaining - 1;
        const uint64_t written = writes[i] & BUSTER_X86_RESOURCE_FLAGS;
        live_writes[i] = written != 0 && (written & live) == 0 ? writes[i] & ~BUSTER_X86_RESOURCE_FLAGS : writes[i];
        live = (live & ~written) | (reads[i] & BUSTER_X86_RESOURCE_FLAGS);
    }
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

// The registers and flags whose RAW edges carry the writer's weight into its readers' path (data_successors).
#define BUSTER_TIER1_SCHEDULE_DATA (BUSTER_X86_RESOURCE_GPRS | BUSTER_X86_RESOURCE_VECTORS | BUSTER_X86_RESOURCE_FLAGS)

static void tier1_schedule_graph_scalar(Tier1ScheduleGraph* graph, uint32_t count, Tier1ScheduleStats* stats)
{
    #pragma clang loop vectorize(disable) interleave(disable)
    for (uint32_t current = 0; current < count; current += 1)
    {
        for (uint32_t previous = 0; previous < current; previous += 1)
        {
            // WAW between two dead flag writers is no hazard: either may be last, the later live writer overwrites both.
            const bool hazard = (graph->writes[previous] & graph->reads[current]) != 0 || (graph->reads[previous] & graph->writes[current]) != 0 ||
                                (graph->writes[previous] & graph->live_writes[current]) != 0 || (graph->live_writes[previous] & graph->writes[current]) != 0 ||
                                graph->barriers[previous] != 0 || graph->barriers[current] != 0;
            if (hazard)
            {
                graph->predecessors[current][previous / 64] |= UINT64_C(1) << (previous & 63);
                graph->successors[previous][current / 64] |= UINT64_C(1) << (current & 63);
                if (graph->writes[previous] & graph->reads[current] & BUSTER_TIER1_SCHEDULE_DATA)
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
        const __m512i read = _mm512_set1_epi64((int64_t)graph->reads[current]);
        const __m512i live = _mm512_set1_epi64((int64_t)graph->live_writes[current]);
        const __m512i data_reads = _mm512_set1_epi64((int64_t)(graph->reads[current] & BUSTER_TIER1_SCHEDULE_DATA));
        for (uint32_t first = 0; first < current; first += 8)
        {
            const uint32_t remaining = current - first < 8 ? current - first : 8;
            const __mmask8 active = (__mmask8)((1u << remaining) - 1);
            const __m512i reads = _mm512_maskz_loadu_epi64(active, graph->reads + first);
            const __m512i writes = _mm512_maskz_loadu_epi64(active, graph->writes + first);
            const __m512i lives = _mm512_maskz_loadu_epi64(active, graph->live_writes + first);
            const __m512i pins = _mm512_maskz_loadu_epi64(active, graph->barriers + first);
            // writes & (reads | live writes) of the current record, and reads | live writes of the earlier ones & its writes.
            __mmask8 hazards = _mm512_test_epi64_mask(writes, _mm512_or_si512(read, live)) | _mm512_test_epi64_mask(_mm512_or_si512(reads, lives), written) |
                               _mm512_test_epi64_mask(pins, pins);
            hazards |= graph->barriers[current] != 0 ? active : 0;
            const __mmask8 data = _mm512_test_epi64_mask(writes, data_reads) & active;
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
            // Whether each record loads (its own effects: an access reads and writes memory from here on), then its
            // ordering resources.
            uint64_t loads[BUSTER_TIER1_SCHEDULE_WORDS] = { 0 };
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                loads[i / 64] |= (uint64_t)((graph->reads[i] & BUSTER_X86_RESOURCE_MEMORY) != 0) << (i & 63);
                tier1_schedule_adjust(&graph->reads[i], &graph->writes[i]);
            }
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                const uint8_t id = x86_zen5_id(&input.instructions[i]);
                graph->cost_ids[i] = id;
                stats.unknown_cases += id == 0;
                for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
                {
                    stats.missing_columns[column] += x86_zen5_columns[column][id] == BUSTER_X86_COST_UNKNOWN;
                }
            }
            if (BUSTER_VECTOR_PATH(vector))
            {
                x86_zen5_lookup(graph->cost_ids, input.count, X86_COST_LATENCY, graph->latencies);
            }
            else
            {
                x86_zen5_lookup_scalar(graph->cost_ids, input.count, X86_COST_LATENCY, graph->latencies);
            }
            // The model's latency where it has one for the record's operand case, its class's weight where
            // it hasn't (tier1_schedule_weight).
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                const bool load = (loads[i / 64] >> (i & 63)) & 1;
                graph->latencies[i] = graph->latencies[i] != BUSTER_X86_COST_UNKNOWN ? graph->latencies[i] : tier1_schedule_weight(&input.instructions[i], load);
                const bool pinned = tier1_schedule_is_pinned(input, i, graph->reads[i], graph->writes[i]);
                graph->barriers[i] = pinned ? UINT64_MAX : 0;
                stats.pinned += pinned;
            }
            tier1_schedule_live_flags(graph->reads, graph->writes, input.count, graph->live_writes);
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                stats.dead_flags += graph->live_writes[i] != graph->writes[i];
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

// The record of `order` that writes flag `bit` latest before position `before`, or UINT32_MAX: the writer a reader at
// that position sees.
static uint32_t tier1_schedule_reaching(const uint64_t* writes, const uint16_t* order, uint32_t before, uint64_t bit)
{
    uint32_t writer = UINT32_MAX;
    for (uint32_t position = 0; position < before; position += 1)
    {
        writer = (writes[order[position]] & bit) != 0 ? order[position] : writer;
    }
    return writer;
}

// Independent scalar verifier: no graph, heights or scheduler-produced effects are trusted. Validates the complete
// permutation first, then from the inputs: every register and memory dependency in order (any two memory accesses
// count as dependent), barriers and compare/branch adjacency; and for each flag, that every reader of it (a record that
// may leave the block reads them all) sees the same writer as in the original order, and that the block's last writer
// of it is the same.
static __attribute__((unused)) bool tier1_schedule_verify(Tier1ScheduleInput input, const uint16_t* order)
{
    bool valid = input.count <= BUSTER_TIER1_SCHEDULE_MAX_RECORDS && (input.count == 0 || (input.instructions && order));
    uint16_t positions[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
    uint16_t identity[BUSTER_TIER1_SCHEDULE_MAX_RECORDS];
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
            pins[i] = tier1_schedule_is_pinned(input, i, reads[i], writes[i]);
            identity[i] = (uint16_t)i;
        }
        const uint64_t others = ~BUSTER_X86_RESOURCE_FLAGS;
        for (uint32_t current = 0; valid && current < input.count; current += 1)
        {
            if (tier1_schedule_fusion_pair(input, current))
            {
                valid = positions[current] + 1 == positions[current + 1];
            }
            const bool accesses = ((reads[current] | writes[current]) & BUSTER_X86_RESOURCE_MEMORY) != 0;
            for (uint32_t previous = 0; valid && previous < current; previous += 1)
            {
                const bool raw = (writes[previous] & reads[current] & others) != 0;
                const bool war = (reads[previous] & writes[current] & others) != 0;
                const bool waw = (writes[previous] & writes[current] & others) != 0;
                const bool memory = accesses && ((reads[previous] | writes[previous]) & BUSTER_X86_RESOURCE_MEMORY) != 0;
                valid = !(raw || war || waw || memory || pins[previous] || pins[current]) || positions[previous] < positions[current];
            }
            const uint64_t flags = (reads[current] | ((writes[current] & BUSTER_X86_RESOURCE_CONTROL) ? BUSTER_X86_RESOURCE_FLAGS : 0)) & BUSTER_X86_RESOURCE_FLAGS;
            for (uint64_t remaining = flags; valid && remaining != 0; remaining &= remaining - 1)
            {
                const uint64_t bit = remaining & (~remaining + 1);
                valid = tier1_schedule_reaching(writes, identity, current, bit) == tier1_schedule_reaching(writes, order, positions[current], bit);
            }
        }
        for (uint64_t remaining = BUSTER_X86_RESOURCE_FLAGS; valid && remaining != 0; remaining &= remaining - 1)
        {
            const uint64_t bit = remaining & (~remaining + 1);
            valid = tier1_schedule_reaching(writes, identity, input.count, bit) == tier1_schedule_reaching(writes, order, input.count, bit);
        }
    }
    return valid;
}

#endif
