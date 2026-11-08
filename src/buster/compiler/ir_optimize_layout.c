// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1218's block order for Tier 1, included after ir_optimize_single_successor. The optimizer's existing rewrite
// consumes order[new] = old in both its numbering and copying passes; it remains the sole owner of value, target,
// phi and label remapping. Only reachable blocks that survive jump threading enter this order, with entry first.
//
// A linear chain cover prefers a loop's unconditional backedge, then another unconditional edge, over a conditional
// fall-through. Existing physical adjacency breaks ties before the original source id. Conditional edges only join
// free ends/heads. Each node then has at most one predecessor and successor, so cycles can be found in one walk and
// cut at their weakest edge. An ordinary while loop keeps latch -> header and cuts header -> body: its initial jump
// remains, while the latch falls into the test on every iteration. This is a structural heuristic, not a profile.
//
// The staged 16-edge rank kernel has scalar/vector twins; target conflicts and chain walks share a scalar consumer.
// Each CFG edge and block is read a bounded number of times, with O(blocks + edges) work and 10 bytes/block of
// temporary columns, plus the caller's 4-byte order column. Input/threads are immutable, output order is disjoint,
// and all scratch allocations precede the first output store. Scratch is restored on success and failure.

#ifndef BUSTER_IR_OPTIMIZE_LAYOUT_INCLUDED
#define BUSTER_IR_OPTIMIZE_LAYOUT_INCLUDED

typedef struct IrOptimizeLayoutBatch IrOptimizeLayoutBatch;
struct IrOptimizeLayoutBatch
{
    uint32_t source_enter[BUSTER_IR_OPTIMIZE_BATCH];
    uint32_t target_enter[BUSTER_IR_OPTIMIZE_BATCH];
    uint32_t target_leave[BUSTER_IR_OPTIMIZE_BATCH];
    uint32_t target[BUSTER_IR_OPTIMIZE_BATCH];
    uint32_t following[BUSTER_IR_OPTIMIZE_BATCH];
    uint32_t active;
    uint32_t count;
};

// The complete staging arrays authorize these full-width loads, including a partial final batch. Inactive lanes
// are initialized and never access a CFG or dominator column. Rank 1/2 is reserved for conditional edges.
static void ir_optimize_layout_ranks(const IrOptimizeLayoutBatch* batch, bool scalar, uint32_t* ranks)
{
    const uint32_t count = batch->count;
    const uint32_t active = batch->active;
    (void)scalar;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            const bool back = batch->target_enter[lane] <= batch->source_enter[lane] && batch->source_enter[lane] < batch->target_leave[lane];
            const bool adjacent = batch->target[lane] == batch->following[lane];
            ranks[lane] = ((active >> lane) & 1) != 0 ? 3 + adjacent + 2 * back : 0;
        }
    }
#if BUSTER_HOST_VECTOR
    else
    {
        const __m512i source = _mm512_loadu_si512(batch->source_enter);
        const __m512i enter = _mm512_loadu_si512(batch->target_enter);
        const __m512i leave = _mm512_loadu_si512(batch->target_leave);
        const __mmask16 back = active & _mm512_cmple_epu32_mask(enter, source) & _mm512_cmplt_epu32_mask(source, leave);
        const __mmask16 adjacent = active & _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(batch->target), _mm512_loadu_si512(batch->following));
        const __m512i direct = _mm512_maskz_set1_epi32(active, 3);
        const __m512i ranked = _mm512_mask_add_epi32(direct, adjacent, direct, _mm512_set1_epi32(1));
        _mm512_storeu_si512(ranks, _mm512_mask_add_epi32(ranked, back, ranked, _mm512_set1_epi32(2)));
    }
#endif
}

static IrError ir_optimize_layout_order(const IrOptimizeWork* work, const uint32_t* threads, Arena* scratch, bool scalar, uint32_t* order, uint32_t* order_count)
{
    const IrFunction* function = work->function;
    const uint32_t blocks = function->block_count;
    const uint32_t* const offsets = function->block_offsets;
    const uint8_t* const operations = function->op;
    const uint32_t* const true_targets = function->b;
    const uint32_t* const false_targets = function->c;
    const uint32_t* const successor_offsets = work->cfg.successor_offsets;
    const uint32_t* const successors = work->cfg.successors;
    const uint8_t* const edges = work->edges;
    const uint8_t* const reachable = work->blocks;
    const uint32_t* const enter = work->dominators.preorder;
    const uint32_t* const leave = work->dominators.subtree_end;
    const uint64_t mark = scratch->length;
    bool ok = true;
    uint32_t* next = ir_allocate(scratch, blocks, sizeof(uint32_t), &ok);
    uint32_t* previous = ir_allocate(scratch, blocks, sizeof(uint32_t), &ok);
    uint8_t* priority = ir_allocate(scratch, blocks, sizeof(uint8_t), &ok);
    uint8_t* visited = ir_allocate(scratch, blocks, sizeof(uint8_t), &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    uint32_t kept = 0;
    if (ok && blocks != 0)
    {
        memset(next, 0xff, (uint64_t)blocks * sizeof(uint32_t));
        memset(previous, 0xff, (uint64_t)blocks * sizeof(uint32_t));
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            if (reachable[block] != 0 && threads[block] == block)
            {
                order[kept++] = block;
            }
        }
    }
    // Single-successor branches become unconditional in the rewrite. An indirect branch stays opaque even when its
    // target list has one entry. Candidate targets are canonical live blocks and never entry or the source itself.
    for (uint32_t first = 0; error == IR_OK && first < kept;)
    {
        const uint32_t count = kept - first < BUSTER_IR_OPTIMIZE_BATCH ? kept - first : BUSTER_IR_OPTIMIZE_BATCH;
        IrOptimizeLayoutBatch batch = { .count = count };
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            const uint32_t block = order[first + lane];
            const uint32_t last = offsets[block + 1] - 1;
            const uint32_t op = operations[last];
            uint32_t target = BUSTER_IR_NONE;
            uint32_t live = 0;
            if (op == IR_BR || op == IR_CBR || op == IR_SWITCH)
            {
                for (uint32_t edge = successor_offsets[block]; edge < successor_offsets[block + 1]; edge += 1)
                {
                    if (edges[edge] != 0)
                    {
                        target = threads[successors[edge]];
                        live += 1;
                    }
                }
            }
            const bool candidate = live == 1 && target != 0 && target != block && reachable[target] != 0 && threads[target] == target;
            if (candidate)
            {
                next[block] = target;
                batch.source_enter[lane] = enter[block];
                batch.target_enter[lane] = enter[target];
                batch.target_leave[lane] = leave[target];
                batch.target[lane] = target;
                batch.following[lane] = first + lane + 1 < kept ? order[first + lane + 1] : BUSTER_IR_NONE;
                batch.active |= 1u << lane;
            }
        }
        uint32_t ranks[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
        ir_optimize_layout_ranks(&batch, scalar, ranks);
        // Original ids increase in this scan, so retaining the first equal-rank proposal is the source-id tie break.
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            const uint32_t block = order[first + lane];
            const uint32_t target = next[block];
            priority[block] = (uint8_t)ranks[lane];
            if (target != BUSTER_IR_NONE && (previous[target] == BUSTER_IR_NONE || priority[previous[target]] < ranks[lane]))
            {
                previous[target] = block;
            }
        }
        first += count;
    }
    for (uint32_t position = 0; error == IR_OK && position < kept; position += 1)
    {
        const uint32_t block = order[position];
        const uint32_t target = next[block];
        if (target != BUSTER_IR_NONE && previous[target] != block)
        {
            next[block] = BUSTER_IR_NONE;
            priority[block] = 0;
        }
    }
    // Both conditional paths remain live here. Preserve the old fall-through where it is available; without profile
    // information, prefer true before false otherwise. Existing direct edges and earlier conditional edges win joins.
    for (uint32_t position = 0; error == IR_OK && position < kept; position += 1)
    {
        const uint32_t block = order[position];
        const uint32_t last = offsets[block + 1] - 1;
        const uint32_t first_edge = successor_offsets[block];
        const uint32_t degree = successor_offsets[block + 1] - first_edge;
        if (operations[last] == IR_CBR && next[block] == BUSTER_IR_NONE && degree == 2 && edges[first_edge] != 0 && edges[first_edge + 1] != 0)
        {
            const uint32_t following = position + 1 < kept ? order[position + 1] : BUSTER_IR_NONE;
            const uint32_t t = threads[true_targets[last]];
            const uint32_t f = threads[false_targets[last]];
            const bool false_first = f == following && t != following;
            const uint32_t targets[] = {
                false_first ? f : t,
                false_first ? t : f,
            };
            for (uint32_t choice = 0; next[block] == BUSTER_IR_NONE && choice < 2; choice += 1)
            {
                const uint32_t target = targets[choice];
                if (target != 0 && target != block && reachable[target] != 0 && threads[target] == target && previous[target] == BUSTER_IR_NONE)
                {
                    next[block] = target;
                    previous[target] = block;
                    priority[block] = (uint8_t)(1 + (target == following));
                }
            }
        }
    }
    // With at most one incoming edge there are only disjoint chains and cycles. A walk either reaches NONE/an earlier
    // walk, or returns to its own start. Cut the least valuable edge; equal ranks start at the lowest target id.
    for (uint32_t block = 0; error == IR_OK && block < blocks; block += 1)
    {
        if (reachable[block] != 0 && threads[block] == block && visited[block] == 0)
        {
            uint32_t current = block;
            uint32_t weakest = BUSTER_IR_NONE;
            while (current != BUSTER_IR_NONE && visited[current] == 0)
            {
                visited[current] = 1;
                const uint32_t target = next[current];
                if (target != BUSTER_IR_NONE && (weakest == BUSTER_IR_NONE || priority[current] < priority[weakest] ||
                    (priority[current] == priority[weakest] && target < next[weakest])))
                {
                    weakest = current;
                }
                current = target;
            }
            if (current == block && weakest != BUSTER_IR_NONE)
            {
                previous[next[weakest]] = BUSTER_IR_NONE;
                next[weakest] = BUSTER_IR_NONE;
            }
        }
    }
    uint32_t placed = 0;
    for (uint32_t block = 0; error == IR_OK && block < blocks; block += 1)
    {
        if (reachable[block] != 0 && threads[block] == block && previous[block] == BUSTER_IR_NONE)
        {
            for (uint32_t current = block; current != BUSTER_IR_NONE; current = next[current])
            {
                order[placed++] = current;
            }
        }
    }
    error = error == IR_OK && placed != kept ? IR_ERROR_STRUCTURE : error;
    if (error == IR_OK)
    {
        *order_count = placed;
    }
    arena_pop_to(scratch, mark);
    return error;
}

#endif
