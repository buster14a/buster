// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original CFG fixtures and definition oracles. Include after dominators.c; run_dominator_tests is the command.
// Dominance is checked by deleting each possible dominator and testing reachability, never by either solver's
// transfer equation. Loop membership uses forward reachability to a latch with its header deleted; irreducibility
// uses the acyclicity of the graph after deleting every dominance backedge. Guard mappings bound all CSR columns.
// The small generator is also included by dominators_benchmark.c, outside its timed region.
// dominator_test_export checks dominators_export.c's format, which must be included before this file.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_DOMINATOR_TEST_BYTES (256ull << 20)
#define BUSTER_DOMINATOR_TEST_SHAPES 6

typedef struct DominatorTestEdge DominatorTestEdge;
struct DominatorTestEdge
{
    uint32_t source;
    uint32_t target;
};

typedef struct DominatorTestTally DominatorTestTally;
struct DominatorTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t seed;
    uint32_t blocks;
    uint32_t shape;
};

static void dominator_test_expect(DominatorTestTally* tally, bool condition, const char* message)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 12)
        {
            fprintf(stderr, "dominators: %s (blocks=%u, shape=%u, seed=%" PRIu64 ")\n", message, tally->blocks, tally->shape, tally->seed);
        }
        tally->failures += 1;
    }
}

static bool dominator_test_cfg(Arena* arena, uint32_t count, uint32_t entry, const DominatorTestEdge* edges, uint32_t edge_count, DominatorCfg* result)
{
    const uint64_t saved = arena->length;
    uint32_t* forward_offsets = arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* reverse_offsets = arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* forward = edge_count != 0 ? arena_push_array(arena, edge_count, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* reverse = edge_count != 0 ? arena_push_array(arena, edge_count, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    const uint64_t temporary = arena->length;
    uint32_t* forward_next = count != 0 ? arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* reverse_next = count != 0 ? arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    bool success = forward_offsets != 0 && reverse_offsets != 0;
    success = success && (edge_count == 0 || (forward != 0 && reverse != 0));
    success = success && (count == 0 || (forward_next != 0 && reverse_next != 0));
    if (success)
    {
        memset(forward_offsets, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(reverse_offsets, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t edge = 0; edge < edge_count; edge += 1)
        {
            forward_offsets[(uint64_t)edges[edge].source + 1] += 1;
            reverse_offsets[(uint64_t)edges[edge].target + 1] += 1;
        }
        for (uint32_t block = 0; block < count; block += 1)
        {
            forward_offsets[(uint64_t)block + 1] += forward_offsets[block];
            reverse_offsets[(uint64_t)block + 1] += reverse_offsets[block];
            forward_next[block] = forward_offsets[block];
            reverse_next[block] = reverse_offsets[block];
        }
        for (uint32_t edge = 0; edge < edge_count; edge += 1)
        {
            const uint32_t source = edges[edge].source;
            const uint32_t target = edges[edge].target;
            forward[forward_next[source]] = target;
            reverse[reverse_next[target]] = source;
            forward_next[source] += 1;
            reverse_next[target] += 1;
        }
        *result = (DominatorCfg){
            .block_count = count,
            .edge_count = edge_count,
            .entry = entry,
            .successor_offsets = forward_offsets,
            .successors = forward,
            .predecessor_offsets = reverse_offsets,
            .predecessors = reverse,
        };
        arena_pop_to(arena, temporary);
    }
    else
    {
        arena_pop_to(arena, saved);
    }
    return success;
}

static uint64_t dominator_test_random(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state >> 32;
}

static bool dominator_test_generate(Arena* arena, uint32_t count, uint32_t shape, uint64_t seed, DominatorCfg* result)
{
    const uint64_t capacity = (uint64_t)count * 5;
    DominatorTestEdge* edges = count != 0 ? arena_push_array(arena, capacity, sizeof(DominatorTestEdge), _Alignof(DominatorTestEdge)) : 0;
    bool success = count == 0 || edges != 0;
    uint32_t edge_count = 0;
    if (success)
    {
        uint64_t random = seed;
        for (uint32_t block = 0; block < count; block += 1)
        {
            // A component boundary in shape 5 deliberately leaves the last half unreachable.
            if (block + 1 < count && (shape != 5 || block + 1 != count / 2))
            {
                edges[edge_count] = (DominatorTestEdge){
                    .source = block,
                    .target = block + 1,
                };
                edge_count += 1;
            }
            if (shape == 1 && block + 2 < count && block % 3 == 0)
            {
                edges[edge_count] = (DominatorTestEdge){
                    .source = block,
                    .target = block + 2,
                };
                edge_count += 1;
            }
            if (shape == 2 && block + 1 < count)
            {
                // A chain with every suffix closed back to its own header: deep natural-loop nesting.
                edges[edge_count] = (DominatorTestEdge){
                    .source = count - 1,
                    .target = block,
                };
                edge_count += 1;
            }
            if (shape == 3 && block + 2 < count && block % 3 == 0)
            {
                edges[edge_count] = (DominatorTestEdge){
                    .source = block,
                    .target = block + 2,
                };
                edge_count += 1;
                edges[edge_count] = (DominatorTestEdge){
                    .source = block + 2,
                    .target = block + 1,
                };
                edge_count += 1;
                edges[edge_count] = (DominatorTestEdge){
                    .source = block + 1,
                    .target = block + 1,
                };
                edge_count += 1;
            }
            if (shape == 4)
            {
                for (uint32_t extra = 0; extra < 3; extra += 1)
                {
                    const uint32_t target = (uint32_t)(dominator_test_random(&random) % count);
                    edges[edge_count] = (DominatorTestEdge){
                        .source = block,
                        .target = target,
                    };
                    edge_count += 1;
                }
            }
            if (shape == 5 && block != 0 && block != count / 2)
            {
                edges[edge_count] = (DominatorTestEdge){
                    .source = block,
                    .target = block - 1,
                };
                edge_count += 1;
            }
        }
        const bool reverse = (seed & 1) != 0;
        if (reverse)
        {
            for (uint32_t edge = 0; edge < edge_count; edge += 1)
            {
                edges[edge].source = count - edges[edge].source - 1;
                edges[edge].target = count - edges[edge].target - 1;
            }
        }
        const uint32_t entry = count == 0 ? BUSTER_DOMINATOR_NONE : reverse ? count - 1 : 0;
        success = dominator_test_cfg(arena, count, entry, edges, edge_count, result);
    }
    return success;
}

// Breadth-first paths in the original graph with one vertex omitted; independent of dominator/tree calculations.
static void dominator_test_paths(const DominatorCfg* cfg, uint32_t start, uint32_t omitted, uint8_t* reached, uint32_t* queue)
{
    memset(reached, 0, cfg->block_count);
    uint64_t read_at = 0;
    uint64_t count = 0;
    if (start != omitted && start < cfg->block_count)
    {
        reached[start] = 1;
        queue[count] = start;
        count += 1;
    }
    while (read_at < count)
    {
        const uint32_t block = queue[read_at];
        read_at += 1;
        for (uint32_t edge = cfg->successor_offsets[block]; edge < cfg->successor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t target = cfg->successors[edge];
            if (target != omitted && reached[target] == 0)
            {
                reached[target] = 1;
                queue[count] = target;
                count += 1;
            }
        }
    }
}

static bool dominator_test_equal(const Dominators* a, const Dominators* b)
{
    const uint64_t column_bytes = (uint64_t)a->block_count * sizeof(uint32_t);
    bool equal = a->block_count == b->block_count && a->reachable_count == b->reachable_count && a->loop_count == b->loop_count;
    if (equal && a->block_count != 0)
    {
        equal = memcmp(a->idom, b->idom, column_bytes) == 0 && memcmp(a->rpo, b->rpo, column_bytes) == 0;
        equal = equal && memcmp(a->rpo_index, b->rpo_index, column_bytes) == 0;
        equal = equal && memcmp(a->preorder, b->preorder, column_bytes) == 0 && memcmp(a->subtree_end, b->subtree_end, column_bytes) == 0;
        equal = equal && memcmp(a->tree_offsets, b->tree_offsets, column_bytes + sizeof(uint32_t)) == 0;
        equal = equal && memcmp(a->tree_children, b->tree_children, (uint64_t)(a->reachable_count - 1) * sizeof(uint32_t)) == 0;
        if ((a->outputs & BUSTER_DOMINATOR_FRONTIERS) != 0)
        {
            equal = equal && memcmp(a->frontiers, b->frontiers, (uint64_t)a->block_count * a->row_words * sizeof(uint64_t)) == 0;
        }
        if ((a->outputs & BUSTER_DOMINATOR_LOOPS) != 0)
        {
            equal = equal && memcmp(a->loop_depth, b->loop_depth, column_bytes) == 0 && memcmp(a->block_loop, b->block_loop, column_bytes) == 0;
            equal = equal && memcmp(a->irreducible, b->irreducible, a->block_count) == 0;
        }
        if (a->loop_count != 0)
        {
            const uint64_t loop_bytes = (uint64_t)a->loop_count * sizeof(uint32_t);
            equal = equal && memcmp(a->loop_header, b->loop_header, loop_bytes) == 0 && memcmp(a->loop_parent, b->loop_parent, loop_bytes) == 0;
            equal = equal && memcmp(a->loop_size, b->loop_size, loop_bytes) == 0;
            equal = equal && memcmp(a->loop_irreducible, b->loop_irreducible, a->loop_count) == 0;
            equal = equal && memcmp(a->loop_bodies, b->loop_bodies, (uint64_t)a->loop_count * a->row_words * sizeof(uint64_t)) == 0;
        }
    }
    return equal;
}

static void dominator_test_definition(DominatorTestTally* tally, Arena* memory, const DominatorCfg* cfg, const Dominators* result)
{
    const uint64_t saved = memory->length;
    const uint32_t count = cfg->block_count;
    uint8_t* dominance = arena_push_array(memory, (uint64_t)count * count, sizeof(uint8_t), _Alignof(uint8_t));
    uint8_t* reachable = arena_push_array(memory, count, sizeof(uint8_t), _Alignof(uint8_t));
    uint8_t* paths = arena_push_array(memory, count, sizeof(uint8_t), _Alignof(uint8_t));
    uint32_t* queue = arena_push_array(memory, count, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* depths = arena_push_array(memory, count, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* incoming = arena_push_array(memory, count, sizeof(uint32_t), _Alignof(uint32_t));
    const bool allocated = dominance != 0 && reachable != 0 && paths != 0 && queue != 0 && depths != 0 && incoming != 0;
    dominator_test_expect(tally, allocated, "definition oracle allocation");
    if (allocated && count != 0)
    {
        dominator_test_paths(cfg, cfg->entry, BUSTER_DOMINATOR_NONE, reachable, queue);
        memset(depths, 0, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t d = 0; d < count; d += 1)
        {
            dominator_test_paths(cfg, cfg->entry, d, paths, queue);
            for (uint32_t block = 0; block < count; block += 1)
            {
                const bool dominates = reachable[d] != 0 && reachable[block] != 0 && paths[block] == 0;
                dominance[(uint64_t)d * count + block] = dominates;
                depths[block] += dominates;
                dominator_test_expect(tally, dominators_dominates(result, d, block) == dominates, "deleted-node dominance");
            }
        }
        for (uint32_t block = 0; block < count; block += 1)
        {
            uint32_t immediate = block == cfg->entry ? cfg->entry : BUSTER_DOMINATOR_NONE;
            uint32_t largest = 0;
            for (uint32_t d = 0; d < count; d += 1)
            {
                if (d != block && dominance[(uint64_t)d * count + block] != 0 && depths[d] > largest)
                {
                    immediate = d;
                    largest = depths[d];
                }
                bool frontier = false;
                if (reachable[block] != 0 && (d == block || dominance[(uint64_t)d * count + block] == 0))
                {
                    for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
                    {
                        frontier = frontier || dominance[(uint64_t)d * count + cfg->predecessors[edge]] != 0;
                    }
                }
                dominator_test_expect(tally, dominators_frontier_contains(result, d, block) == frontier, "frontier definition");
            }
            dominator_test_expect(tally, result->idom[block] == immediate, "immediate dominator definition");
            dominator_test_expect(tally, (result->rpo_index[block] != BUSTER_DOMINATOR_NONE) == (reachable[block] != 0), "entry reachability");
        }
        // Forward paths with the header removed find exactly the nodes which can reach any latch.
        uint32_t expected_loops = 0;
        for (uint32_t header = 0; header < count; header += 1)
        {
            bool has_backedge = false;
            for (uint32_t edge = cfg->predecessor_offsets[header]; edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
            {
                has_backedge = has_backedge || dominance[(uint64_t)header * count + cfg->predecessors[edge]] != 0;
            }
            if (has_backedge)
            {
                const uint32_t loop = expected_loops;
                expected_loops += 1;
                dominator_test_expect(tally, loop < result->loop_count && result->loop_header[loop] == header, "loop header and backedges");
                if (loop < result->loop_count)
                {
                    uint32_t size = 0;
                    for (uint32_t block = 0; block < count; block += 1)
                    {
                        bool member = block == header;
                        dominator_test_paths(cfg, block, header, paths, queue);
                        if (reachable[block] != 0)
                        {
                            for (uint32_t edge = cfg->predecessor_offsets[header]; edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
                            {
                                const uint32_t latch = cfg->predecessors[edge];
                                member = member || (dominance[(uint64_t)header * count + latch] != 0 && paths[latch] != 0);
                            }
                        }
                        size += member;
                        dominator_test_expect(tally, dominators_loop_contains(result, loop, block) == member, "forward-path natural loop body");
                    }
                    dominator_test_expect(tally, result->loop_size[loop] == size, "loop body count");
                }
            }
        }
        dominator_test_expect(tally, result->loop_count == expected_loops, "all backedge headers have loops");
        for (uint32_t loop = 0; loop < result->loop_count; loop += 1)
        {
            uint32_t parent = BUSTER_DOMINATOR_NONE;
            uint32_t size = UINT32_MAX;
            for (uint32_t other = 0; other < result->loop_count; other += 1)
            {
                bool contains = loop != other && result->loop_size[other] > result->loop_size[loop];
                for (uint32_t block = 0; contains && block < count; block += 1)
                {
                    contains = !dominators_loop_contains(result, loop, block) || dominators_loop_contains(result, other, block);
                }
                if (contains && (parent == BUSTER_DOMINATOR_NONE || result->loop_size[other] < size))
                {
                    parent = other;
                    size = result->loop_size[other];
                }
            }
            dominator_test_expect(tally, result->loop_parent[loop] == parent, "forest parent by strict set containment");
        }
        for (uint32_t block = 0; block < count; block += 1)
        {
            uint32_t depth = 0;
            uint32_t inner = BUSTER_DOMINATOR_NONE;
            uint32_t size = UINT32_MAX;
            for (uint32_t loop = 0; loop < result->loop_count; loop += 1)
            {
                if (dominators_loop_contains(result, loop, block))
                {
                    depth += 1;
                    if (inner == BUSTER_DOMINATOR_NONE || result->loop_size[loop] < size)
                    {
                        size = result->loop_size[loop];
                        inner = loop;
                    }
                }
            }
            dominator_test_expect(tally, result->loop_depth[block] == depth && result->block_loop[block] == inner, "loop depth and innermost body");
        }
        // Reducibility theorem: delete all edges whose targets dominate their sources; the remainder is acyclic.
        memset(incoming, 0, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t source = 0; source < count; source += 1)
        {
            if (reachable[source] != 0)
            {
                for (uint32_t edge = cfg->successor_offsets[source]; edge < cfg->successor_offsets[(uint64_t)source + 1]; edge += 1)
                {
                    const uint32_t target = cfg->successors[edge];
                    if (dominance[(uint64_t)target * count + source] == 0)
                    {
                        incoming[target] += 1;
                    }
                }
            }
        }
        uint32_t written = 0;
        uint32_t read_at = 0;
        bool flagged = false;
        for (uint32_t block = 0; block < count; block += 1)
        {
            flagged = flagged || result->irreducible[block] != 0;
            if (reachable[block] != 0 && incoming[block] == 0)
            {
                queue[written] = block;
                written += 1;
            }
        }
        while (read_at < written)
        {
            const uint32_t source = queue[read_at];
            read_at += 1;
            for (uint32_t edge = cfg->successor_offsets[source]; edge < cfg->successor_offsets[(uint64_t)source + 1]; edge += 1)
            {
                const uint32_t target = cfg->successors[edge];
                if (dominance[(uint64_t)target * count + source] == 0)
                {
                    incoming[target] -= 1;
                    if (incoming[target] == 0)
                    {
                        queue[written] = target;
                        written += 1;
                    }
                }
            }
        }
        dominator_test_expect(tally, flagged == (written < result->reachable_count), "irreducibility by deleting dominance backedges");
    }
    arena_pop_to(memory, saved);
}

static void dominator_test_case(DominatorTestTally* tally, Arena* memory, Arena* scratch, const DominatorCfg* cfg, bool oracle)
{
    const uint64_t saved = memory->length;
    const uint64_t scratch_saved = scratch->length;
    Dominators scalar = {0};
    Dominators vector = {0};
    DominatorOptions options = dominators_options();
    options.path = DOMINATOR_SCALAR;
    const DominatorStatus scalar_status = dominators_analyze(memory, scratch, cfg, options, &scalar);
    options.path = DOMINATOR_VECTOR;
    const DominatorStatus vector_status = dominators_analyze(memory, scratch, cfg, options, &vector);
    dominator_test_expect(tally, scalar_status == DOMINATOR_OK && vector_status == DOMINATOR_OK, "both paths succeed");
    dominator_test_expect(tally, scratch->length == scratch_saved, "scratch lifetime restored");
    if (scalar_status == DOMINATOR_OK && vector_status == DOMINATOR_OK)
    {
        dominator_test_expect(tally, dominator_test_equal(&scalar, &vector), "scalar/vector complete result equality");
        for (uint32_t block = 0; block < cfg->block_count; block += 1)
        {
            const uint64_t* row = vector.frontiers + (uint64_t)block * vector.row_words;
            dominator_test_expect(tally, ((uintptr_t)row & 63) == 0, "aligned frontier row");
            for (uint64_t bit = cfg->block_count; bit < vector.row_words * 64; bit += 1)
            {
                dominator_test_expect(tally, !dominators_bit(row, (uint32_t)bit), "zero frontier tail bits");
            }
        }
        for (uint32_t loop = 0; loop < vector.loop_count; loop += 1)
        {
            const uint64_t* row = vector.loop_bodies + (uint64_t)loop * vector.row_words;
            dominator_test_expect(tally, ((uintptr_t)row & 63) == 0, "aligned loop row");
            for (uint64_t bit = cfg->block_count; bit < vector.row_words * 64; bit += 1)
            {
                dominator_test_expect(tally, !dominators_bit(row, (uint32_t)bit), "zero loop tail bits");
            }
        }
        if (oracle)
        {
            dominator_test_definition(tally, memory, cfg, &vector);
        }
        Dominators repeated = {0};
        const uint64_t repeated_at = memory->length;
        const DominatorStatus repeated_status = dominators_analyze(memory, scratch, cfg, options, &repeated);
        dominator_test_expect(tally, repeated_status == DOMINATOR_OK && dominator_test_equal(&vector, &repeated), "repeatability");
        arena_pop_to(memory, repeated_at);
    }
    arena_pop_to(memory, saved);
}

static void dominator_test_limits(DominatorTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint64_t saved = memory->length;
    DominatorCfg cfg;
    const bool built = dominator_test_generate(memory, 17, 2, 0, &cfg);
    dominator_test_expect(tally, built, "limits fixture");
    if (built)
    {
        DominatorOptions options = dominators_options();
        Dominators sentinel;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        Dominators result = sentinel;
        const uint64_t start = memory->length;
        const uint64_t scratch_start = scratch->length;
        const uint64_t capacity = memory->capacity;
        memory->capacity = memory->length;
        DominatorStatus status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_NO_MEMORY && memcmp(&result, &sentinel, sizeof(result)) == 0 && memory->length == start && scratch->length == scratch_start, "output allocation rollback");
        memory->capacity = capacity;
        const uint64_t scratch_capacity = scratch->capacity;
        scratch->capacity = scratch->length;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_NO_MEMORY && memcmp(&result, &sentinel, sizeof(result)) == 0 && memory->length == start && scratch->length == scratch_start, "scratch allocation rollback");
        scratch->capacity = scratch_capacity;
        options.max_matrix_bytes = 0;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_MATRIX_LIMIT && memory->length == start && memcmp(&result, &sentinel, sizeof(result)) == 0, "frontier budget refusal");
        options.outputs = BUSTER_DOMINATOR_LOOPS;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_MATRIX_LIMIT && memory->length == start && memcmp(&result, &sentinel, sizeof(result)) == 0, "loop budget refusal");
        options.outputs = 0;
        options.path = DOMINATOR_VECTOR;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_MATRIX_LIMIT && memory->length == start, "explicit vector budget refusal");
        options.path = DOMINATOR_AUTO;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_OK && result.path == DOMINATOR_SCALAR && result.matrix_peak_bytes == 0 && result.frontiers == 0 && result.loop_bodies == 0, "automatic linear-memory fallback");
        arena_pop_to(memory, start);
        options = dominators_options();
        options.max_matrix_bytes = 17 * 64 + 16 * 64;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_OK && result.path == DOMINATOR_VECTOR && result.loop_path == DOMINATOR_SCALAR, "loop scratch independently falls back within budget");
        arena_pop_to(memory, start);
        uint32_t* reverse = (uint32_t*)cfg.predecessors;
        const uint32_t first = reverse[0];
        reverse[0] = (first + 1) % cfg.block_count;
        result = sentinel;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_INVALID && memory->length == start && memcmp(&result, &sentinel, sizeof(result)) == 0, "CSR edge multiset mismatch");
        reverse[0] = first;
        uint32_t* offsets = (uint32_t*)cfg.successor_offsets;
        const uint32_t offset = offsets[1];
        offsets[1] = cfg.edge_count + 1;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_INVALID, "malformed offsets");
        offsets[1] = offset;
        uint32_t* forward = (uint32_t*)cfg.successors;
        const uint32_t target = forward[0];
        forward[0] = cfg.block_count;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_INVALID, "out-of-range target");
        forward[0] = target;
        status = dominators_analyze(memory, memory, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_INVALID, "same arena rejected");
        options.outputs = 4;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_INVALID, "unknown output flag rejected");
        // Exercise partial allocation failures in every phase, not only the first output/scratch push.
        options = dominators_options();
        options.path = DOMINATOR_VECTOR;
        status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_OK, "allocation sweep baseline");
        if (status == DOMINATOR_OK)
        {
            const uint64_t output_bytes = result.output_bytes;
            const uint64_t scratch_bytes = result.scratch_peak_bytes;
            arena_pop_to(memory, start);
            for (uint64_t bytes = 0; bytes < output_bytes; bytes += 4)
            {
                memory->capacity = start + bytes;
                result = sentinel;
                status = dominators_analyze(memory, scratch, &cfg, options, &result);
                dominator_test_expect(tally, status == DOMINATOR_NO_MEMORY && memory->length == start && scratch->length == scratch_start && memcmp(&result, &sentinel, sizeof(result)) == 0, "every output allocation failure rolls back");
            }
            memory->capacity = capacity;
            for (uint64_t bytes = 0; bytes < scratch_bytes; bytes += 4)
            {
                scratch->capacity = scratch_start + bytes;
                result = sentinel;
                status = dominators_analyze(memory, scratch, &cfg, options, &result);
                dominator_test_expect(tally, status == DOMINATOR_NO_MEMORY && memory->length == start && scratch->length == scratch_start && memcmp(&result, &sentinel, sizeof(result)) == 0, "every scratch allocation failure rolls back");
            }
            scratch->capacity = scratch_capacity;
        }
        cfg.block_count = 0;
        cfg.entry = BUSTER_DOMINATOR_NONE;
        status = dominators_analyze(memory, scratch, &cfg, dominators_options(), &result);
        dominator_test_expect(tally, status == DOMINATOR_INVALID, "edges in empty graph rejected");
    }
    arena_pop_to(memory, saved);
}

typedef struct DominatorTestGuard DominatorTestGuard;
struct DominatorTestGuard
{
    uint8_t* mapping;
    uint64_t mapping_bytes;
    void* data;
};

static DominatorTestGuard dominator_test_guard(const void* data, uint64_t bytes)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t readable = (bytes + page - 1) / page * page;
    DominatorTestGuard result = {
        .mapping_bytes = readable + page,
    };
    void* mapped = mmap(0, result.mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped != MAP_FAILED)
    {
        result.mapping = mapped;
        result.data = result.mapping + readable - bytes;
        if (bytes != 0 && data != 0)
        {
            memcpy(result.data, data, bytes);
        }
        if (mprotect(result.mapping + readable, page, PROT_NONE) != 0)
        {
            munmap(result.mapping, result.mapping_bytes);
            result.mapping = 0;
        }
    }
    return result;
}

static void dominator_test_guards(DominatorTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint32_t sizes[] = {
        0,
        1,
        15,
        16,
        17,
        63,
        64,
        65,
        511,
        512,
        513,
    };
    for (uint64_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i += 1)
    {
        const uint64_t saved = memory->length;
        DominatorCfg cfg;
        const bool built = dominator_test_generate(memory, sizes[i], 1, 0, &cfg);
        tally->blocks = sizes[i];
        dominator_test_expect(tally, built, "guard fixture");
        if (built)
        {
            DominatorTestGuard guards[4] = {
                dominator_test_guard(cfg.successor_offsets, ((uint64_t)cfg.block_count + 1) * sizeof(uint32_t)),
                dominator_test_guard(cfg.successors, (uint64_t)cfg.edge_count * sizeof(uint32_t)),
                dominator_test_guard(cfg.predecessor_offsets, ((uint64_t)cfg.block_count + 1) * sizeof(uint32_t)),
                dominator_test_guard(cfg.predecessors, (uint64_t)cfg.edge_count * sizeof(uint32_t)),
            };
            const bool mapped = guards[0].mapping != 0 && guards[1].mapping != 0 && guards[2].mapping != 0 && guards[3].mapping != 0;
            dominator_test_expect(tally, mapped, "guard mappings");
            if (mapped)
            {
                cfg.successor_offsets = guards[0].data;
                cfg.successors = guards[1].data;
                cfg.predecessor_offsets = guards[2].data;
                cfg.predecessors = guards[3].data;
                dominator_test_case(tally, memory, scratch, &cfg, sizes[i] < 66);
            }
            for (uint32_t guard = 0; guard < 4; guard += 1)
            {
                if (guards[guard].mapping != 0)
                {
                    munmap(guards[guard].mapping, guards[guard].mapping_bytes);
                }
            }
        }
        arena_pop_to(memory, saved);
    }
}

// The vector solver's own matrices end at inaccessible pages, independently of the borrowed-CSR guard tests.
static void dominator_test_matrix_guards(DominatorTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint32_t sizes[] = {
        1,
        63,
        64,
        65,
        511,
        512,
        513,
        1025,
    };
    for (uint64_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i += 1)
    {
        const uint64_t saved = memory->length;
        DominatorCfg cfg;
        tally->blocks = sizes[i];
        const bool built = dominator_test_generate(memory, sizes[i], 4, tally->seed, &cfg);
        dominator_test_expect(tally, built, "matrix guard fixture");
        if (built)
        {
            DominatorOptions options = {
                .path = DOMINATOR_SCALAR,
                .outputs = 0,
                .max_matrix_bytes = 0,
            };
            Dominators scalar;
            const DominatorStatus status = dominators_analyze(memory, scratch, &cfg, options, &scalar);
            dominator_test_expect(tally, status == DOMINATOR_OK, "matrix guard scalar oracle");
            if (status == DOMINATOR_OK)
            {
                const uint64_t bytes = scalar.row_words * sizeof(uint64_t);
                DominatorTestGuard sets = dominator_test_guard(0, (uint64_t)cfg.block_count * bytes);
                DominatorTestGuard reachable = dominator_test_guard(0, bytes);
                Dominators vector = scalar;
                vector.idom = arena_push_array(memory, cfg.block_count, sizeof(uint32_t), _Alignof(uint32_t));
                uint32_t* counts = arena_push_array(memory, cfg.block_count, sizeof(uint32_t), _Alignof(uint32_t));
                const bool allocated = sets.mapping != 0 && reachable.mapping != 0 && vector.idom != 0 && counts != 0;
                dominator_test_expect(tally, allocated, "matrix guard allocations");
                if (allocated)
                {
                    memset(sets.data, 0xa5, (uint64_t)cfg.block_count * bytes);
                    memset(reachable.data, 0xa5, bytes);
                    vector.iterations = 0;
                    dominators_vector(&cfg, &vector, sets.data, reachable.data, counts);
                    dominator_test_expect(tally, memcmp(vector.idom, scalar.idom, (uint64_t)cfg.block_count * sizeof(uint32_t)) == 0, "guarded matrix solver result");
                    const uint64_t* rows = sets.data;
                    for (uint32_t block = 0; block < cfg.block_count; block += 1)
                    {
                        for (uint64_t bit = cfg.block_count; bit < vector.row_words * 64; bit += 1)
                        {
                            dominator_test_expect(tally, !dominators_bit(rows + (uint64_t)block * vector.row_words, (uint32_t)bit), "guarded dominator padding cleared");
                        }
                    }
                }
                if (sets.mapping != 0)
                {
                    munmap(sets.mapping, sets.mapping_bytes);
                }
                if (reachable.mapping != 0)
                {
                    munmap(reachable.mapping, reachable.mapping_bytes);
                }
            }
        }
        arena_pop_to(memory, saved);
    }
}

static void dominator_test_irreducible_regions(DominatorTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint64_t saved = memory->length;
    // A two-entry SCC surrounds natural self-loops at 1 and 2. Its exit, and an unreachable SCC, stay unflagged.
    const DominatorTestEdge edges[] = {
        {
            .source = 0,
            .target = 1,
        },
        {
            .source = 0,
            .target = 2,
        },
        {
            .source = 1,
            .target = 1,
        },
        {
            .source = 2,
            .target = 2,
        },
        {
            .source = 1,
            .target = 3,
        },
        {
            .source = 3,
            .target = 2,
        },
        {
            .source = 2,
            .target = 1,
        },
        {
            .source = 3,
            .target = 4,
        },
        {
            .source = 5,
            .target = 6,
        },
        {
            .source = 6,
            .target = 5,
        },
    };
    DominatorCfg cfg;
    const bool built = dominator_test_cfg(memory, 7, 0, edges, sizeof(edges) / sizeof(edges[0]), &cfg);
    dominator_test_expect(tally, built, "irreducible region fixture");
    if (built)
    {
        dominator_test_case(tally, memory, scratch, &cfg, true);
        Dominators analysis;
        const DominatorStatus status = dominators_analyze(memory, scratch, &cfg, dominators_options(), &analysis);
        dominator_test_expect(tally, status == DOMINATOR_OK, "irreducible region analysis");
        if (status == DOMINATOR_OK)
        {
            for (uint32_t block = 0; block < cfg.block_count; block += 1)
            {
                dominator_test_expect(tally, analysis.irreducible[block] == (block >= 1 && block <= 3), "irreducible SCC excludes exit and unreachable cycle");
            }
            dominator_test_expect(tally, analysis.loop_count == 2 && analysis.loop_irreducible[0] == 1 && analysis.loop_irreducible[1] == 1, "natural subloops cannot authorize LICM in irreducible SCC");
            dominator_test_expect(tally, analysis.preorder[5] == 0 && analysis.subtree_end[5] == 0 && analysis.preorder[6] == 0 && analysis.subtree_end[6] == 0, "unreachable intervals match IR verifier");
        }
    }
    arena_pop_to(memory, saved);
}

static void dominator_test_deep(DominatorTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint64_t saved = memory->length;
    const uint32_t count = 100000;
    tally->blocks = count;
    DominatorCfg cfg;
    const bool built = dominator_test_generate(memory, count, 0, 0, &cfg);
    dominator_test_expect(tally, built, "deep chain fixture");
    if (built)
    {
        DominatorOptions options = {
            .path = DOMINATOR_AUTO,
            .outputs = 0,
            .max_matrix_bytes = 0,
        };
        Dominators result;
        const DominatorStatus status = dominators_analyze(memory, scratch, &cfg, options, &result);
        dominator_test_expect(tally, status == DOMINATOR_OK, "100000-block explicit stacks");
        if (status == DOMINATOR_OK)
        {
            dominator_test_expect(tally, result.reachable_count == count && result.path == DOMINATOR_SCALAR && result.matrix_peak_bytes == 0, "deep sparse memory policy");
            for (uint32_t block = 0; block < count; block += 1)
            {
                dominator_test_expect(tally, result.idom[block] == (block == 0 ? 0 : block - 1) && result.preorder[block] == block && result.subtree_end[block] == count, "deep chain tree");
            }
        }
    }
    arena_pop_to(memory, saved);
}

#define BUSTER_DOMINATOR_TEST_EXPORTS 20

// One malformation of the small export: its first `from` replaced by `to`.
typedef struct DominatorTestChange DominatorTestChange;
struct DominatorTestChange
{
    const char* from;
    const char* to;
};

// Reads `bytes` as an export from a copy that ends at an inaccessible page, so that a read past its end faults.
static bool dominator_test_export_read(Arena* memory, const void* bytes, uint64_t length, DominatorExport* result)
{
    DominatorTestGuard guard = dominator_test_guard(bytes, length);
    const bool read = guard.mapping != 0 && dominators_export_read(memory, guard.data, length, result);
    if (guard.mapping != 0)
    {
        munmap(guard.mapping, guard.mapping_bytes);
    }
    return read;
}

// buster-dominators-v1 (dominators_export.c). Generated CFGs written and read back keep their counts, entries,
// successor views and names byte for byte; the predecessor view is the analyzer's (it validates both views) and
// sorted by source, and both paths' complete results are those of the original. Every truncation and each named
// malformation of a small export is refused whole, leaving the arena as it was.
static void dominator_test_export(DominatorTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint64_t saved = memory->length;
    const uint32_t sizes[] = {
        0,
        1,
        7,
        65,
        513,
    };
    const uint32_t shapes[] = {
        1,
        3,
        4,
        5,
    };
    DominatorCfg graphs[BUSTER_DOMINATOR_TEST_EXPORTS];
    uint8_t names[BUSTER_DOMINATOR_TEST_EXPORTS][4];
    const uint8_t file[] = {
        'a',
        ' ',
        '\n',
        0,
        0xff,
        'c',
    };
    uint32_t count = 0;
    bool built = true;
    for (uint32_t size = 0; size < sizeof(sizes) / sizeof(sizes[0]); size += 1)
    {
        for (uint32_t shape = 0; shape < sizeof(shapes) / sizeof(shapes[0]); shape += 1)
        {
            built = built && dominator_test_generate(memory, sizes[size], shapes[shape], count, &graphs[count]);
            names[count][0] = 'f';
            names[count][1] = (uint8_t)count;
            names[count][2] = '\n';
            names[count][3] = 0xa0;
            count += 1;
        }
    }
    dominator_test_expect(tally, built, "export fixtures");
    char* text = 0;
    size_t length = 0;
    FILE* stream = built ? open_memstream(&text, &length) : 0;
    bool written = stream != 0 && dominators_export_begin(stream);
    for (uint32_t i = 0; written && i < count; i += 1)
    {
        written = dominators_export_cfg(stream, &graphs[i], file, sizeof(file) - i % 2, names[i], 4 - i % 4, i % 3);
    }
    written = written && dominators_export_complete(stream, 2, 7, count);
    if (stream != 0)
    {
        written = fclose(stream) == 0 && written;
    }
    dominator_test_expect(tally, written, "export written");
    DominatorExport export = { 0 };
    const uint64_t read_at = memory->length;
    const bool read = written && dominator_test_export_read(memory, text, length, &export);
    dominator_test_expect(tally, read && export.count == count && export.files == 2 && export.functions == 7, "export read back");
    uint64_t blocks = 0;
    uint64_t edges = 0;
    for (uint32_t i = 0; read && i < count; i += 1)
    {
        const DominatorCfg* original = &graphs[i];
        const DominatorExportGraph* graph = &export.graphs[i];
        tally->blocks = original->block_count;
        blocks += original->block_count;
        edges += original->edge_count;
        bool same = graph->form == i % 3 && graph->cfg.block_count == original->block_count && graph->cfg.edge_count == original->edge_count && graph->cfg.entry == original->entry;
        same = same && graph->file_length == sizeof(file) - i % 2 && memcmp(graph->file, file, graph->file_length) == 0 && graph->name_length == 4 - i % 4 && memcmp(graph->name, names[i], graph->name_length) == 0;
        for (uint32_t block = 0; same && block <= original->block_count; block += 1)
        {
            same = graph->cfg.successor_offsets[block] == original->successor_offsets[block] && graph->cfg.predecessor_offsets[block] == original->predecessor_offsets[block];
        }
        for (uint32_t edge = 0; same && edge < original->edge_count; edge += 1)
        {
            same = graph->cfg.successors[edge] == original->successors[edge];
        }
        for (uint32_t block = 0; same && block < original->block_count; block += 1)
        {
            for (uint32_t edge = graph->cfg.predecessor_offsets[block] + 1; same && edge < graph->cfg.predecessor_offsets[block + 1]; edge += 1)
            {
                same = graph->cfg.predecessors[edge - 1] <= graph->cfg.predecessors[edge];
            }
        }
        dominator_test_expect(tally, same, "export round trip keeps counts, names and both CSR views");
        DominatorOptions options = dominators_options();
        for (uint32_t path = 0; same && path < 2; path += 1)
        {
            options.path = path == 0 ? DOMINATOR_SCALAR : DOMINATOR_VECTOR;
            const uint64_t analysis_at = memory->length;
            Dominators before = { 0 };
            Dominators after = { 0 };
            const bool analyzed = dominators_analyze(memory, scratch, original, options, &before) == DOMINATOR_OK && dominators_analyze(memory, scratch, &graph->cfg, options, &after) == DOMINATOR_OK;
            dominator_test_expect(tally, analyzed && dominator_test_equal(&before, &after), "an exported CFG's complete analysis is the original's");
            arena_pop_to(memory, analysis_at);
        }
    }
    dominator_test_expect(tally, !read || (export.blocks == blocks && export.edges == edges), "export totals");
    arena_pop_to(memory, read_at);
    free(text);
    static const char valid[] = "buster-dominators-v1\ncfg 0 3 3 0\nfile 612e63\nname 66\n0 1\n0 2\n1 2\nend\ncfg 1 0 0 4294967295\nfile \nname 67\nend\ncomplete 1 1 2\n";
    DominatorExport small = { 0 };
    const uint64_t small_at = memory->length;
    const bool small_read = dominator_test_export_read(memory, valid, sizeof(valid) - 1, &small);
    dominator_test_expect(tally, small_read && small.count == 2 && small.blocks == 3 && small.edges == 3 && small.graphs[1].cfg.entry == BUSTER_DOMINATOR_NONE && small.graphs[0].file_length == 3, "small export read");
    arena_pop_to(memory, small_at);
    for (uint64_t cut = 0; cut + 1 < sizeof(valid); cut += 1)
    {
        const bool refused = !dominator_test_export_read(memory, valid, cut, &small);
        dominator_test_expect(tally, refused && memory->length == small_at, "a truncated export is refused whole");
    }
    static const DominatorTestChange changes[] = {
        {
            .from = "dominators-v1",
            .to = "dominators-v2",
        },
        {
            .from = "complete 1 1 2\n",
            .to = "",
        },
        {
            .from = "complete 1 1 2",
            .to = "complete 1 1 3",
        },
        {
            .from = "complete 1 1 2\n",
            .to = "complete 1 1 2\nx",
        },
        {
            .from = "complete 1 1 2\n",
            .to = "complete 1 1 2\n\n",
        },
        {
            .from = "1 2\nend",
            .to = "3 2\nend",
        },
        {
            .from = "0 2\n",
            .to = "0 3\n",
        },
        {
            .from = "0 1\n0 2\n1 2\n",
            .to = "0 1\n1 2\n0 2\n",
        },
        {
            .from = "0 3 3 0",
            .to = "0 3 4 0",
        },
        {
            .from = "0 3 3 0",
            .to = "0 3 2 0",
        },
        {
            .from = "cfg 0 3",
            .to = "cfg 0 03",
        },
        {
            .from = "612e63",
            .to = "612E63",
        },
        {
            .from = "name 66",
            .to = "name 6",
        },
        {
            .from = "0 3 3 0",
            .to = "0 3 3 3",
        },
        {
            .from = "0 0 4294967295",
            .to = "0 0 0",
        },
        {
            .from = "cfg 0 3 3 0",
            .to = "cfg 0 4294967296 3 0",
        },
        {
            .from = "cfg 0 3 3 0",
            .to = "cfg 0 4294967295 3 0",
        },
        {
            .from = "1 2\nend",
            .to = "1 2\nfin",
        },
        {
            .from = "cfg 0 3 3 0\n",
            .to = "cfg 0 3 3 0\r\n",
        },
        {
            .from = "0 1\n",
            .to = "0  1\n",
        },
        {
            .from = "0 1\n",
            .to = "-0 1\n",
        },
    };
    char changed[512];
    for (uint32_t i = 0; i < sizeof(changes) / sizeof(changes[0]); i += 1)
    {
        const char* at = strstr(valid, changes[i].from);
        const uint64_t prefix = at != 0 ? (uint64_t)(at - valid) : 0;
        const uint64_t from = strlen(changes[i].from);
        const uint64_t to = strlen(changes[i].to);
        const uint64_t total = sizeof(valid) - 1 - from + to;
        const bool placed = at != 0 && total <= sizeof(changed);
        if (placed)
        {
            memcpy(changed, valid, prefix);
            memcpy(changed + prefix, changes[i].to, to);
            memcpy(changed + prefix + to, at + from, sizeof(valid) - 1 - prefix - from);
        }
        const bool refused = placed && !dominator_test_export_read(memory, changed, total, &small);
        dominator_test_expect(tally, refused && memory->length == small_at, "a malformed export is refused whole");
    }
    arena_pop_to(memory, saved);
}

static bool dominator_test_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool success = text[0] >= '0' && text[0] <= '9' && errno == 0 && end != text && *end == 0;
    if (success)
    {
        *value = parsed;
    }
    return success;
}

static __attribute__((unused)) int run_dominator_tests(int argc, char** argv)
{
    uint64_t seed = 174;
    uint64_t random_count = 40;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &seed);
        }
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &random_count) && random_count <= 100000;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-dominators [--seed N] [--count N]\n");
    }
    else
    {
        Arena memory = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
        Arena scratch = arena_reserve(BUSTER_DOMINATOR_TEST_BYTES);
        DominatorTestTally tally = {
            .seed = seed,
        };
        dominator_test_expect(&tally, memory.base != 0 && scratch.base != 0, "test arena reservation");
        if (memory.base != 0 && scratch.base != 0)
        {
            dominator_test_limits(&tally, &memory, &scratch);
            dominator_test_guards(&tally, &memory, &scratch);
            dominator_test_matrix_guards(&tally, &memory, &scratch);
            dominator_test_irreducible_regions(&tally, &memory, &scratch);
            const uint32_t sizes[] = {
                0,
                1,
                2,
                3,
                7,
                16,
                33,
                65,
                127,
                511,
                512,
                513,
                1025,
            };
            for (uint32_t shape = 0; shape < BUSTER_DOMINATOR_TEST_SHAPES; shape += 1)
            {
                for (uint64_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i += 1)
                {
                    const uint64_t saved = memory.length;
                    tally.blocks = sizes[i];
                    tally.shape = shape;
                    DominatorCfg cfg;
                    const bool built = dominator_test_generate(&memory, sizes[i], shape, seed, &cfg);
                    dominator_test_expect(&tally, built, "generated fixture");
                    if (built)
                    {
                        // The full forward-loop oracle costs O(loops * blocks * edges); retain it on small graphs.
                        dominator_test_case(&tally, &memory, &scratch, &cfg, sizes[i] <= 127 || (shape == 4 && sizes[i] == 513));
                    }
                    arena_pop_to(&memory, saved);
                }
            }
            uint64_t random = seed;
            for (uint64_t sample = 0; sample < random_count; sample += 1)
            {
                const uint64_t saved = memory.length;
                tally.blocks = 1 + (uint32_t)(dominator_test_random(&random) % 96);
                tally.shape = 4;
                DominatorCfg cfg;
                const bool built = dominator_test_generate(&memory, tally.blocks, 4, random, &cfg);
                dominator_test_expect(&tally, built, "random fixture");
                if (built)
                {
                    dominator_test_case(&tally, &memory, &scratch, &cfg, true);
                }
                arena_pop_to(&memory, saved);
            }
            const DominatorTestEdge edges[] = {
                {
                    .source = 0,
                    .target = 0,
                },
                {
                    .source = 0,
                    .target = 1,
                },
                {
                    .source = 0,
                    .target = 1,
                },
                {
                    .source = 1,
                    .target = 2,
                },
                {
                    .source = 2,
                    .target = 0,
                },
                {
                    .source = 2,
                    .target = 1,
                },
                {
                    .source = 2,
                    .target = 1,
                },
                {
                    .source = 3,
                    .target = 2,
                },
                {
                    .source = 3,
                    .target = 3,
                },
            };
            DominatorCfg special;
            const bool built = dominator_test_cfg(&memory, 4, 0, edges, sizeof(edges) / sizeof(edges[0]), &special);
            dominator_test_expect(&tally, built, "duplicate and entry-edge fixture");
            if (built)
            {
                tally.blocks = 4;
                dominator_test_case(&tally, &memory, &scratch, &special, true);
            }
            dominator_test_deep(&tally, &memory, &scratch);
            dominator_test_export(&tally, &memory, &scratch);
        }
        printf("dominators: %" PRIu64 " checks, %" PRIu64 " failures (seed=%" PRIu64 ", count=%" PRIu64 ")\n", tally.checks, tally.failures, seed, random_count);
        result = tally.failures == 0 ? 0 : 1;
        arena_release(&scratch);
        arena_release(&memory);
    }
    return result;
}
