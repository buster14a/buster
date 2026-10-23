// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Selective code/data emission (#129), after semantic checking. Include after lane.c. This module does not parse,
// check, inline or lower C: the producer resolves redeclarations, linkage, inline dialect and evaluation context.
// Definition IDs are dense and already in source order. One ID names one function body or object definition;
// prototypes and alias declarations are not additional definitions. ALIAS_TARGET roots the definition named by an
// alias; publishing the alias symbol itself remains the checker's/linker's job.
//
// c_lowering_prepare validates the input, copies flags and stably counting-sorts retained references into CSR.
// EXTERNAL means an actual external definition that must emit, not merely external linkage. INLINE_ONLY means a
// function body without an emitted definition (for example a C99 inline definition): neither a reference to it nor
// its own references cause local emission. USED/CONSTRUCTOR/DESTRUCTOR/ALIAS_TARGET do not override INLINE_ONLY.
// Calls/addresses that bind only to an external symbol and unevaluated references never create local graph edges.
// This classification does not suppress checking a body or initializer; all diagnostics precede this module.
//
// c_lowering_vector visits compact breadth-first source frontiers, filters eight targets together against a reached
// bitset, and emits selected IDs with vector masks in source order. Sparse word reads/writes stay scalar: hardware
// gathers/scatters are costly on Zen 5, and duplicate targets require merging rather than conflicting stores.
// c_lowering_scalar is an independent byte-visited FIFO traversal and source-order collector. Neither recurses or
// rescans all edges/bitset words in each round. Both visit each reached source's retained outgoing edges once.
//
// Input columns have exactly their count's readable elements, each a uint32_t; no padding/alignment beyond their
// element type is needed. Zero counts permit null columns. Inputs and the result struct cannot overlap newly
// allocated arena storage; earlier allocations in the same arena are allowed. Successful preparation owns all
// copied input, CSR and reusable workspace in that arena. Input columns may then be changed or released. Failure
// preserves the arena length and *result. A prepared plan is private to one worker and must not be changed except
// by these solvers. Tests may substitute disjoint graph/workspace buffers of the same logical capacities, preserving
// all immutable graph contents and prepared-plan invariants; no internal pointer aliases another column.
//
// reached has word_count uint64_t words, with unused high bits zero. selected has definition_count uint32_t slots,
// of which selected_count are valid and strictly increasing; its remaining slots are unspecified. Empty plans have
// null buffers. Solvers allocate nothing, overwrite the previous result, and leave it unchanged on an invalid mode
// or unprepared plan. rounds counts nonempty BFS frontiers (roots are round 1); visited_edges counts retained edges
// out of reached sources, including duplicates. ALL collects every emission-eligible definition without graph work:
// rounds/visited_edges/vector_batches are zero. It is a diagnostic selection mode, not a measurement of lowering.

#ifndef BUSTER_C_LOWERING_INCLUDED
#define BUSTER_C_LOWERING_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_C_LOWERING_NONE UINT32_MAX
#define BUSTER_C_LOWERING_FUNCTION UINT32_C(1)
#define BUSTER_C_LOWERING_OBJECT UINT32_C(2)
#define BUSTER_C_LOWERING_EXTERNAL UINT32_C(4)
#define BUSTER_C_LOWERING_INLINE_ONLY UINT32_C(8)
#define BUSTER_C_LOWERING_USED UINT32_C(16)
#define BUSTER_C_LOWERING_CONSTRUCTOR UINT32_C(32)
#define BUSTER_C_LOWERING_DESTRUCTOR UINT32_C(64)
#define BUSTER_C_LOWERING_ALIAS_TARGET UINT32_C(128)
#define BUSTER_C_LOWERING_ROOT_FLAGS (BUSTER_C_LOWERING_EXTERNAL | BUSTER_C_LOWERING_USED | BUSTER_C_LOWERING_CONSTRUCTOR | BUSTER_C_LOWERING_DESTRUCTOR | BUSTER_C_LOWERING_ALIAS_TARGET)
#define BUSTER_C_LOWERING_ALL_FLAGS (BUSTER_C_LOWERING_FUNCTION | BUSTER_C_LOWERING_OBJECT | BUSTER_C_LOWERING_INLINE_ONLY | BUSTER_C_LOWERING_ROOT_FLAGS)
#define BUSTER_C_LOWERING_ID_LANES 16u
#define BUSTER_C_LOWERING_EDGE_LANES 8u
#define BUSTER_C_LOWERING_WORD_BITS 64u

typedef enum CLoweringReferenceKind
{
    C_LOWERING_REFERENCE_EVALUATED,
    C_LOWERING_REFERENCE_UNEVALUATED,
    C_LOWERING_REFERENCE_EXTERNAL,
} CLoweringReferenceKind;

typedef enum CLoweringStatus
{
    C_LOWERING_OK,
    C_LOWERING_INVALID,
    C_LOWERING_OUT_OF_MEMORY,
} CLoweringStatus;

typedef enum CLoweringMode
{
    C_LOWERING_REACHABLE,
    C_LOWERING_ALL,
} CLoweringMode;

typedef struct CLoweringInput CLoweringInput;
struct CLoweringInput
{
    uint32_t definition_count;
    uint32_t reference_count;
    const uint32_t* definition_flags;
    const uint32_t* reference_source;
    const uint32_t* reference_target;
    const uint32_t* reference_kind;
};

typedef struct CLoweringStatistics CLoweringStatistics;
struct CLoweringStatistics
{
    uint64_t rounds;
    uint64_t visited_edges;
    uint64_t vector_batches;
    uint64_t selected_functions;
    uint64_t selected_objects;
};

typedef struct CLowering CLowering;
struct CLowering
{
    uint32_t definition_count;
    uint32_t reference_count;
    uint32_t edge_count;
    uint32_t root_count;
    uint32_t selected_count;
    uint64_t word_count;
    uint64_t workspace_bytes;
    // Mutable traversal/output allocations and their alignment padding; included in workspace_bytes.
    uint64_t scratch_bytes;
    uint64_t* reached;
    uint32_t* selected;
    CLoweringStatistics statistics;
    // Immutable prepared graph: offsets has definition_count + 1 elements for a nonempty plan; empty plans use null.
    uint32_t* definition_flags;
    uint32_t* edge_offsets;
    uint32_t* edge_targets;
    // Private reusable traversal storage. queue has definition_count slots, visited has definition_count bytes.
    uint32_t* queue;
    uint8_t* visited;
    bool prepared;
};

static __attribute__((unused)) CLoweringStatus c_lowering_prepare(Arena* arena, CLoweringInput input, CLowering* result);
static __attribute__((unused)) CLoweringStatus c_lowering_vector(CLowering* plan, CLoweringMode mode);
static __attribute__((unused)) CLoweringStatus c_lowering_scalar(CLowering* plan, CLoweringMode mode);

static CLoweringStatus c_lowering_prepare(Arena* arena, CLoweringInput input, CLowering* result)
{
    CLoweringStatus status = C_LOWERING_INVALID;
    bool valid = arena != 0 && result != 0 && input.definition_count != UINT32_MAX && input.reference_count != UINT32_MAX;
    valid = valid && (input.definition_count == 0 || input.definition_flags != 0);
    valid = valid && (input.reference_count == 0 || (input.reference_source != 0 && input.reference_target != 0 && input.reference_kind != 0));
    valid = valid && (input.definition_count != 0 || input.reference_count == 0);
    uint32_t roots = 0;
    for (uint32_t definition = 0; valid && definition < input.definition_count; definition += 1)
    {
        const uint32_t flags = input.definition_flags[definition];
        const uint32_t kind = flags & (BUSTER_C_LOWERING_FUNCTION | BUSTER_C_LOWERING_OBJECT);
        const bool inline_only = (flags & BUSTER_C_LOWERING_INLINE_ONLY) != 0;
        valid = (flags & ~BUSTER_C_LOWERING_ALL_FLAGS) == 0 && (kind == BUSTER_C_LOWERING_FUNCTION || kind == BUSTER_C_LOWERING_OBJECT);
        valid = valid && (!inline_only || (kind == BUSTER_C_LOWERING_FUNCTION && (flags & BUSTER_C_LOWERING_EXTERNAL) == 0));
        valid = valid && ((flags & (BUSTER_C_LOWERING_CONSTRUCTOR | BUSTER_C_LOWERING_DESTRUCTOR)) == 0 || kind == BUSTER_C_LOWERING_FUNCTION);
        roots += !inline_only && (flags & BUSTER_C_LOWERING_ROOT_FLAGS) != 0;
    }
    uint32_t edges = 0;
    for (uint32_t reference = 0; valid && reference < input.reference_count; reference += 1)
    {
        const uint32_t source = input.reference_source[reference];
        const uint32_t target = input.reference_target[reference];
        const uint32_t kind = input.reference_kind[reference];
        valid = source < input.definition_count && kind <= C_LOWERING_REFERENCE_EXTERNAL;
        valid = valid && (target < input.definition_count || (target == BUSTER_C_LOWERING_NONE && kind != C_LOWERING_REFERENCE_EVALUATED));
        if (valid && kind == C_LOWERING_REFERENCE_EVALUATED)
        {
            edges += ((input.definition_flags[source] | input.definition_flags[target]) & BUSTER_C_LOWERING_INLINE_ONLY) == 0;
        }
    }
    if (valid)
    {
        const uint64_t mark = arena->length;
        CLowering candidate = {
            .definition_count = input.definition_count,
            .reference_count = input.reference_count,
            .edge_count = edges,
            .root_count = roots,
            .word_count = ((uint64_t)input.definition_count + BUSTER_C_LOWERING_WORD_BITS - 1) / BUSTER_C_LOWERING_WORD_BITS,
            .prepared = true,
        };
        if (input.definition_count != 0)
        {
            // Every dimension is bounded below UINT32_MAX; array byte counts and their sum fit uint64_t.
            candidate.definition_flags = arena_push_array(arena, input.definition_count, sizeof(uint32_t), 64);
            candidate.edge_offsets = candidate.definition_flags != 0 ? arena_push_array(arena, (uint64_t)input.definition_count + 1, sizeof(uint32_t), 64) : 0;
            const uint64_t scratch_mark = arena->length;
            candidate.reached = candidate.edge_offsets != 0 ? arena_push_array(arena, candidate.word_count, sizeof(uint64_t), 64) : 0;
            candidate.selected = candidate.reached != 0 ? arena_push_array(arena, input.definition_count, sizeof(uint32_t), 64) : 0;
            candidate.queue = candidate.selected != 0 ? arena_push_array(arena, input.definition_count, sizeof(uint32_t), 64) : 0;
            candidate.visited = candidate.queue != 0 ? arena_push_array(arena, input.definition_count, sizeof(uint8_t), 64) : 0;
            valid = candidate.visited != 0;
            candidate.scratch_bytes = arena->length - scratch_mark;
        }
        if (valid && edges != 0)
        {
            candidate.edge_targets = arena_push_array(arena, edges, sizeof(uint32_t), 64);
            valid = candidate.edge_targets != 0;
        }
        if (valid)
        {
            if (input.definition_count != 0)
            {
                memcpy(candidate.definition_flags, input.definition_flags, (uint64_t)input.definition_count * sizeof(uint32_t));
                memset(candidate.edge_offsets, 0, ((uint64_t)input.definition_count + 1) * sizeof(uint32_t));
                memset(candidate.reached, 0, candidate.word_count * sizeof(uint64_t));
                for (uint32_t reference = 0; reference < input.reference_count; reference += 1)
                {
                    if (input.reference_kind[reference] == C_LOWERING_REFERENCE_EVALUATED)
                    {
                        const uint32_t source = input.reference_source[reference];
                        const uint32_t target = input.reference_target[reference];
                        if (((candidate.definition_flags[source] | candidate.definition_flags[target]) & BUSTER_C_LOWERING_INLINE_ONLY) == 0)
                        {
                            candidate.edge_offsets[(uint64_t)source + 1] += 1;
                        }
                    }
                }
                for (uint32_t definition = 0; definition < input.definition_count; definition += 1)
                {
                    candidate.edge_offsets[(uint64_t)definition + 1] += candidate.edge_offsets[definition];
                    // selected is spare until the first solve: reuse it for stable counting-sort write cursors.
                    candidate.selected[definition] = candidate.edge_offsets[definition];
                }
                for (uint32_t reference = 0; reference < input.reference_count; reference += 1)
                {
                    if (input.reference_kind[reference] == C_LOWERING_REFERENCE_EVALUATED)
                    {
                        const uint32_t source = input.reference_source[reference];
                        const uint32_t target = input.reference_target[reference];
                        if (((candidate.definition_flags[source] | candidate.definition_flags[target]) & BUSTER_C_LOWERING_INLINE_ONLY) == 0)
                        {
                            candidate.edge_targets[candidate.selected[source]++] = target;
                        }
                    }
                }
            }
            candidate.workspace_bytes = arena->length - mark;
            *result = candidate;
            status = C_LOWERING_OK;
        }
        else
        {
            arena_pop_to(arena, mark);
            status = C_LOWERING_OUT_OF_MEMORY;
        }
    }
    return status;
}

// Register compression plus an ordinary masked store avoids the more expensive memory-form compression. The
// prefix mask never writes outside the logical count, including when queue/selected ends against a guard page.
static uint32_t c_lowering_store_ids(uint32_t* destination, __m512i ids, __mmask16 mask)
{
    const uint32_t count = (uint32_t)__builtin_popcount((uint32_t)mask);
    const __mmask16 prefix = (__mmask16)((UINT32_C(1) << count) - 1);
    _mm512_mask_storeu_epi32(destination, prefix, _mm512_maskz_compress_epi32(mask, ids));
    return count;
}

static uint32_t c_lowering_seed_vector(CLowering* plan, CLoweringMode mode)
{
    uint32_t count = 0;
    __m512i ids = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t base = 0; base < plan->definition_count; base += BUSTER_C_LOWERING_ID_LANES)
    {
        const uint32_t remaining = plan->definition_count - (uint32_t)base;
        const __mmask16 valid = remaining >= BUSTER_C_LOWERING_ID_LANES ? UINT16_MAX : (__mmask16)((UINT32_C(1) << remaining) - 1);
        const __m512i flags = _mm512_maskz_loadu_epi32(valid, plan->definition_flags + base);
        const __mmask16 eligible = valid & ~_mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_C_LOWERING_INLINE_ONLY));
        const __mmask16 roots = mode == C_LOWERING_ALL ? eligible : eligible & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_C_LOWERING_ROOT_FLAGS));
        plan->reached[base / BUSTER_C_LOWERING_WORD_BITS] |= (uint64_t)roots << (base % BUSTER_C_LOWERING_WORD_BITS);
        count += c_lowering_store_ids(plan->queue + count, ids, roots);
        ids = _mm512_add_epi32(ids, _mm512_set1_epi32(BUSTER_C_LOWERING_ID_LANES));
    }
    return count;
}

static void c_lowering_collect_vector(CLowering* plan)
{
    uint32_t count = 0;
    uint64_t functions = 0;
    __m512i ids = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t base = 0; base < plan->definition_count; base += BUSTER_C_LOWERING_ID_LANES)
    {
        const __mmask16 selected = (__mmask16)(plan->reached[base / BUSTER_C_LOWERING_WORD_BITS] >> (base % BUSTER_C_LOWERING_WORD_BITS));
        // reached's high padding bits are zero, so this mask also proves the flags load is within the final tail.
        const __m512i flags = _mm512_maskz_loadu_epi32(selected, plan->definition_flags + base);
        functions += (uint32_t)__builtin_popcount((uint32_t)(selected & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_C_LOWERING_FUNCTION))));
        count += c_lowering_store_ids(plan->selected + count, ids, selected);
        ids = _mm512_add_epi32(ids, _mm512_set1_epi32(BUSTER_C_LOWERING_ID_LANES));
    }
    plan->selected_count = count;
    plan->statistics.selected_functions = functions;
    plan->statistics.selected_objects = count - functions;
}

static CLoweringStatus c_lowering_vector(CLowering* plan, CLoweringMode mode)
{
    CLoweringStatus status = C_LOWERING_INVALID;
    if (plan != 0 && plan->prepared && (mode == C_LOWERING_REACHABLE || mode == C_LOWERING_ALL))
    {
        plan->statistics = (CLoweringStatistics){ 0 };
        if (plan->word_count != 0)
        {
            memset(plan->reached, 0, plan->word_count * sizeof(uint64_t));
        }
        uint32_t tail = c_lowering_seed_vector(plan, mode);
        uint32_t begin = 0;
        uint32_t end = mode == C_LOWERING_ALL ? 0 : tail;
        while (begin < end)
        {
            plan->statistics.rounds += 1;
            for (uint32_t position = begin; position < end; position += 1)
            {
                const uint32_t source = plan->queue[position];
                uint64_t edge = plan->edge_offsets[source];
                const uint64_t edge_end = plan->edge_offsets[(uint64_t)source + 1];
                plan->statistics.visited_edges += edge_end - edge;
                for (; edge_end - edge >= BUSTER_C_LOWERING_EDGE_LANES; edge += BUSTER_C_LOWERING_EDGE_LANES)
                {
                    const uint32_t* targets = plan->edge_targets + edge;
                    const __m512i ids = _mm512_cvtepu32_epi64(_mm256_loadu_si256((const __m256i*)targets));
                    const __m512i shifts = _mm512_and_si512(ids, _mm512_set1_epi64(BUSTER_C_LOWERING_WORD_BITS - 1));
                    const __m512i bits = _mm512_sllv_epi64(_mm512_set1_epi64(1), shifts);
                    const __m512i seen = _mm512_setr_epi64(
                        plan->reached[targets[0] / BUSTER_C_LOWERING_WORD_BITS],
                        plan->reached[targets[1] / BUSTER_C_LOWERING_WORD_BITS],
                        plan->reached[targets[2] / BUSTER_C_LOWERING_WORD_BITS],
                        plan->reached[targets[3] / BUSTER_C_LOWERING_WORD_BITS],
                        plan->reached[targets[4] / BUSTER_C_LOWERING_WORD_BITS],
                        plan->reached[targets[5] / BUSTER_C_LOWERING_WORD_BITS],
                        plan->reached[targets[6] / BUSTER_C_LOWERING_WORD_BITS],
                        plan->reached[targets[7] / BUSTER_C_LOWERING_WORD_BITS]
                    );
                    uint32_t fresh = (uint8_t)~_mm512_test_epi64_mask(seen, bits);
                    plan->statistics.vector_batches += 1;
                    while (fresh != 0)
                    {
                        const uint32_t lane = (uint32_t)__builtin_ctz(fresh);
                        const uint32_t target = targets[lane];
                        const uint64_t bit = UINT64_C(1) << (target % BUSTER_C_LOWERING_WORD_BITS);
                        uint64_t* word = plan->reached + target / BUSTER_C_LOWERING_WORD_BITS;
                        // The snapshot can contain duplicate unseen targets. Recheck after earlier lanes' updates,
                        // so each ID enters the bounded queue once, and distinct bits sharing a word are preserved.
                        if ((*word & bit) == 0)
                        {
                            *word |= bit;
                            plan->queue[tail++] = target;
                        }
                        fresh &= fresh - 1;
                    }
                }
                for (; edge < edge_end; edge += 1)
                {
                    const uint32_t target = plan->edge_targets[edge];
                    const uint64_t bit = UINT64_C(1) << (target % BUSTER_C_LOWERING_WORD_BITS);
                    uint64_t* word = plan->reached + target / BUSTER_C_LOWERING_WORD_BITS;
                    if ((*word & bit) == 0)
                    {
                        *word |= bit;
                        plan->queue[tail++] = target;
                    }
                }
            }
            begin = end;
            end = tail;
        }
        c_lowering_collect_vector(plan);
        status = C_LOWERING_OK;
    }
    return status;
}

static CLoweringStatus c_lowering_scalar(CLowering* plan, CLoweringMode mode)
{
    CLoweringStatus status = C_LOWERING_INVALID;
    if (plan != 0 && plan->prepared && (mode == C_LOWERING_REACHABLE || mode == C_LOWERING_ALL))
    {
        plan->statistics = (CLoweringStatistics){ 0 };
        plan->selected_count = 0;
        if (plan->word_count != 0)
        {
            memset(plan->reached, 0, plan->word_count * sizeof(uint64_t));
        }
        uint32_t tail = 0;
        for (uint32_t definition = 0; definition < plan->definition_count; definition += 1)
        {
            const uint32_t flags = plan->definition_flags[definition];
            const bool eligible = (flags & BUSTER_C_LOWERING_INLINE_ONLY) == 0;
            const bool root = eligible && (mode == C_LOWERING_ALL || (flags & BUSTER_C_LOWERING_ROOT_FLAGS) != 0);
            plan->visited[definition] = root;
            if (root)
            {
                plan->queue[tail++] = definition;
            }
        }
        uint32_t begin = 0;
        uint32_t end = mode == C_LOWERING_ALL ? 0 : tail;
        while (begin < end)
        {
            plan->statistics.rounds += 1;
            for (uint32_t position = begin; position < end; position += 1)
            {
                const uint32_t source = plan->queue[position];
                const uint32_t edge_begin = plan->edge_offsets[source];
                const uint32_t edge_end = plan->edge_offsets[(uint64_t)source + 1];
                plan->statistics.visited_edges += edge_end - edge_begin;
                for (uint32_t edge = edge_begin; edge < edge_end; edge += 1)
                {
                    const uint32_t target = plan->edge_targets[edge];
                    if (plan->visited[target] == 0)
                    {
                        plan->visited[target] = 1;
                        plan->queue[tail++] = target;
                    }
                }
            }
            begin = end;
            end = tail;
        }
        for (uint32_t definition = 0; definition < plan->definition_count; definition += 1)
        {
            if (plan->visited[definition] != 0)
            {
                plan->reached[definition / BUSTER_C_LOWERING_WORD_BITS] |= UINT64_C(1) << (definition % BUSTER_C_LOWERING_WORD_BITS);
                plan->selected[plan->selected_count++] = definition;
                plan->statistics.selected_functions += (plan->definition_flags[definition] & BUSTER_C_LOWERING_FUNCTION) != 0;
                plan->statistics.selected_objects += (plan->definition_flags[definition] & BUSTER_C_LOWERING_OBJECT) != 0;
            }
        }
        status = C_LOWERING_OK;
    }
    return status;
}

#endif
