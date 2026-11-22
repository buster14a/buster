// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The sweep in which dataflow.c's solver first sets each live bit, found without a fixpoint: the tests' oracle for
// its iteration counts and in rows. dataflow_test.c and ir_dataflow_test.c include it after dataflow.c.
//
// The solver starts from empty in rows and visits dataflow->postorder in every sweep, reading each successor's in row
// as the sweep has left it. An edge to a block that comes later in the postorder, or to the block itself, reads the
// previous sweep's row (a stale edge); an edge to an earlier block reads this sweep's. So value v is live into block b
// after sweep t exactly when a path from b reaches a use of v through blocks that neither use nor define v, crossing at
// most t - 1 stale edges. With D(b, v) the fewest stale edges on such a path, b's bit for v is first set in sweep
// D(b, v) + 1. A sweep reads only what the previous one left, so the first sweep that changes nothing is a fixpoint,
// and the solver's count, which includes that last sweep, is 2 plus the largest D over the live pairs, or 1 when
// nothing is live. A 0-1 breadth-first search per value, backwards from its uses, finds every D.

#ifndef BUSTER_DATAFLOW_SWEEPS_TEST_C
#define BUSTER_DATAFLOW_SWEEPS_TEST_C

// Whether graph's in rows hold exactly the live pairs. *sweeps is the solver's count for graph's postorder, or 0 when
// scratch can't hold the search; scratch returns to its length.
static __attribute__((unused)) bool dataflow_test_sweeps(const Dataflow* graph, Arena* scratch, uint64_t* sweeps)
{
    const uint64_t mark = scratch->length;
    const uint32_t blocks = graph->block_count;
    uint32_t* position = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, blocks);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)blocks + 1);
    uint32_t* predecessors = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->edge_count);
    uint8_t* stale = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->edge_count);
    uint32_t* distance = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, blocks);
    uint32_t* current = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, blocks);
    uint32_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, blocks);
    bool result = offsets != 0 && (graph->edge_count == 0 || (predecessors != 0 && stale != 0));
    result = result && (blocks == 0 || (position != 0 && distance != 0 && current != 0 && next != 0));
    const bool allocated = result;
    uint32_t deepest = 0;
    bool live = false;
    if (result)
    {
        // Predecessor columns, each edge marked stale where the solver reads its target's previous sweep.
        for (uint32_t order = 0; order < blocks; order += 1)
        {
            position[graph->postorder[order]] = order;
        }
        memset(offsets, 0, ((uint64_t)blocks + 1) * sizeof(uint32_t));
        for (uint32_t edge = 0; edge < graph->edge_count; edge += 1)
        {
            offsets[(uint64_t)graph->successors[edge] + 1] += 1;
        }
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            offsets[(uint64_t)block + 1] += offsets[block];
            next[block] = offsets[block];
        }
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            for (uint32_t edge = graph->successor_offsets[block]; edge < graph->successor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                const uint32_t successor = graph->successors[edge];
                const uint32_t place = next[successor];
                next[successor] += 1;
                predecessors[place] = block;
                stale[place] = position[successor] >= position[block];
            }
        }
    }
    for (uint32_t value = 0; result && value < graph->value_count; value += 1)
    {
        const uint64_t word = value / 64;
        const uint64_t bit = 1ull << (value % 64);
        uint32_t current_count = 0;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            distance[block] = UINT32_MAX;
            if ((graph->use[(uint64_t)block * graph->row_words + word] & bit) != 0)
            {
                distance[block] = 0;
                current[current_count] = block;
                current_count += 1;
            }
        }
        // `current` holds the blocks at `level`, each once; a stale edge's predecessor waits in `next` for the level
        // after, unless a current edge reaches it first. A block's distance only falls, so each enters each list once.
        uint32_t level = 0;
        while (current_count != 0)
        {
            uint32_t next_count = 0;
            while (current_count != 0)
            {
                current_count -= 1;
                const uint32_t block = current[current_count];
                for (uint32_t edge = offsets[block]; edge < offsets[(uint64_t)block + 1]; edge += 1)
                {
                    const uint32_t predecessor = predecessors[edge];
                    const uint32_t reached = level + stale[edge];
                    const bool defines = (graph->def[(uint64_t)predecessor * graph->row_words + word] & bit) != 0;
                    if (!defines && distance[predecessor] > reached)
                    {
                        distance[predecessor] = reached;
                        if (stale[edge] == 0)
                        {
                            current[current_count] = predecessor;
                            current_count += 1;
                        }
                        else
                        {
                            next[next_count] = predecessor;
                            next_count += 1;
                        }
                    }
                }
            }
            level += 1;
            for (uint32_t index = 0; index < next_count; index += 1)
            {
                if (distance[next[index]] == level)
                {
                    current[current_count] = next[index];
                    current_count += 1;
                }
            }
        }
        for (uint32_t block = 0; result && block < blocks; block += 1)
        {
            const bool reached = distance[block] != UINT32_MAX;
            result = reached == ((graph->in[(uint64_t)block * graph->row_words + word] & bit) != 0);
            if (reached)
            {
                live = true;
                deepest = distance[block] > deepest ? distance[block] : deepest;
            }
        }
    }
    *sweeps = !allocated ? 0 : live ? 2 + (uint64_t)deepest : 1;
    arena_pop_to(scratch, mark);
    return result;
}

#endif
