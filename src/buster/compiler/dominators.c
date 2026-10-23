// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Entry-rooted dominators, frontiers and natural loops for a borrowed CSR CFG. Include after lane.c.
// dominators_analyze validates both CSR views, computes reachable reverse postorder, solves by AVX-512 set
// intersection or scalar Cooper-Harvey-Kennedy, then builds the tree and the requested frontier/loop sets.
// All input-dependent walks use explicit stacks. No source or implementation from another compiler is copied.
//
// The caller owns the input arrays throughout the call; each offsets array has block_count + 1 u32 elements,
// each edge array has edge_count u32 elements, and no padding or vector alignment is required. Duplicate edges
// are accepted when their multiplicities match in both views. Unreachable blocks have no dominance relation.
// Output columns belong to arena; scratch must be a distinct, nonoverlapping arena. Neither input nor result
// may overlap either arena's unused space. Failure preserves *result and arena.length; scratch is always restored.
// Matrices have 64-byte-aligned rows of row_words u64s, padded to 512 bits, with zero inactive bits. Every load
// stays inside its allocated row. Recompute after any CFG mutation. See docs/dominators.md for the stage contract.
//
// Layout: dominators_validate; dominators_order; dominators_scalar/vector; dominators_tree/frontiers;
// dominators_components; dominators_loops; dominators_analyze; the consumer query helpers.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_DOMINATOR_NONE UINT32_MAX
#define BUSTER_DOMINATOR_VECTOR_WORDS 8
#define BUSTER_DOMINATOR_VECTOR_BITS 512
#define BUSTER_DOMINATOR_VECTOR_BYTES 64
#define BUSTER_DOMINATOR_DEFAULT_MATRIX_BYTES (64ull << 20)
#define BUSTER_DOMINATOR_FRONTIERS 1u
#define BUSTER_DOMINATOR_LOOPS 2u

typedef enum DominatorStatus
{
    DOMINATOR_OK,
    DOMINATOR_INVALID,
    DOMINATOR_NO_MEMORY,
    DOMINATOR_MATRIX_LIMIT,
} DominatorStatus;

typedef enum DominatorPath
{
    DOMINATOR_AUTO,
    DOMINATOR_VECTOR,
    DOMINATOR_SCALAR,
} DominatorPath;

typedef struct DominatorCfg DominatorCfg;
struct DominatorCfg
{
    uint32_t block_count;
    uint32_t edge_count;
    uint32_t entry;
    const uint32_t* successor_offsets;
    const uint32_t* successors;
    const uint32_t* predecessor_offsets;
    const uint32_t* predecessors;
};

typedef struct DominatorOptions DominatorOptions;
struct DominatorOptions
{
    DominatorPath path;
    uint32_t outputs;
    // Peak bytes of simultaneously live bitset matrices, including temporary predecessor rows. Zero permits none.
    uint64_t max_matrix_bytes;
};

typedef struct Dominators Dominators;
struct Dominators
{
    uint32_t block_count;
    uint32_t entry;
    uint32_t reachable_count;
    uint32_t loop_count;
    uint32_t outputs;
    uint64_t row_words;
    uint64_t iterations;
    uint64_t matrix_peak_bytes;
    uint64_t output_bytes;
    uint64_t scratch_peak_bytes;
    DominatorPath path;
    DominatorPath loop_path;
    uint32_t* idom;
    uint32_t* rpo;
    uint32_t* rpo_index;
    uint32_t* tree_offsets;
    uint32_t* tree_children;
    uint32_t* preorder;
    uint32_t* subtree_end;
    uint64_t* frontiers;
    // Loop ids are assigned by ascending header block id. Several latches at one header share one body.
    uint32_t* loop_header;
    uint32_t* loop_parent;
    uint32_t* loop_size;
    uint64_t* loop_bodies;
    uint32_t* loop_depth;
    uint32_t* block_loop;
    // Conservative SCC flag: hoisting must skip any loop whose loop_irreducible flag is set.
    uint8_t* irreducible;
    uint8_t* loop_irreducible;
};

typedef struct DominatorFrame DominatorFrame;
struct DominatorFrame
{
    uint32_t block;
    uint32_t next;
};

static __attribute__((unused)) DominatorOptions dominators_options(void);
static __attribute__((unused)) DominatorStatus dominators_analyze(Arena* arena, Arena* scratch, const DominatorCfg* cfg, DominatorOptions options, Dominators* result);
static __attribute__((unused)) bool dominators_dominates(const Dominators* dominators, uint32_t dominator, uint32_t block);
static __attribute__((unused)) bool dominators_frontier_contains(const Dominators* dominators, uint32_t block, uint32_t member);
static __attribute__((unused)) bool dominators_loop_contains(const Dominators* dominators, uint32_t loop, uint32_t block);

static DominatorOptions dominators_options(void)
{
    const DominatorOptions result = {
        .path = DOMINATOR_AUTO,
        .outputs = BUSTER_DOMINATOR_FRONTIERS | BUSTER_DOMINATOR_LOOPS,
        .max_matrix_bytes = BUSTER_DOMINATOR_DEFAULT_MATRIX_BYTES,
    };
    return result;
}

static void dominators_record_scratch(Dominators* result, const Arena* scratch, uint64_t start)
{
    const uint64_t bytes = scratch->length - start;
    if (bytes > result->scratch_peak_bytes)
    {
        result->scratch_peak_bytes = bytes;
    }
}

// Compare the two CSR multisets in O(B + E), with one temporary transpose and a per-source counter column.
static DominatorStatus dominators_validate(const DominatorCfg* cfg, Arena* scratch, Dominators* result)
{
    const uint64_t saved = scratch->length;
    const uint32_t count = cfg->block_count;
    bool valid = count == 0 ? cfg->edge_count == 0 && cfg->entry == BUSTER_DOMINATOR_NONE : cfg->entry < count;
    if (valid && count != 0)
    {
        valid = cfg->successor_offsets != 0 && cfg->predecessor_offsets != 0;
        valid = valid && (cfg->edge_count == 0 || (cfg->successors != 0 && cfg->predecessors != 0));
        if (valid)
        {
            valid = cfg->successor_offsets[0] == 0 && cfg->predecessor_offsets[0] == 0;
            valid = valid && cfg->successor_offsets[count] == cfg->edge_count && cfg->predecessor_offsets[count] == cfg->edge_count;
        }
        for (uint32_t block = 0; valid && block < count; block += 1)
        {
            valid = cfg->successor_offsets[block] <= cfg->successor_offsets[(uint64_t)block + 1];
            valid = valid && cfg->successor_offsets[(uint64_t)block + 1] <= cfg->edge_count;
            valid = valid && cfg->predecessor_offsets[block] <= cfg->predecessor_offsets[(uint64_t)block + 1];
            valid = valid && cfg->predecessor_offsets[(uint64_t)block + 1] <= cfg->edge_count;
        }
        for (uint32_t edge = 0; valid && edge < cfg->edge_count; edge += 1)
        {
            valid = cfg->successors[edge] < count && cfg->predecessors[edge] < count;
        }
    }
    DominatorStatus status = valid ? DOMINATOR_OK : DOMINATOR_INVALID;
    uint32_t* counters = 0;
    uint32_t* transpose = 0;
    if (status == DOMINATOR_OK && count != 0)
    {
        counters = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        if (cfg->edge_count != 0)
        {
            transpose = arena_push_array(scratch, cfg->edge_count, sizeof(uint32_t), _Alignof(uint32_t));
        }
        if (counters == 0 || (cfg->edge_count != 0 && transpose == 0))
        {
            status = DOMINATOR_NO_MEMORY;
        }
    }
    if (status == DOMINATOR_OK && count != 0)
    {
        memset(counters, 0, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t edge = 0; edge < cfg->edge_count; edge += 1)
        {
            counters[cfg->successors[edge]] += 1;
        }
        for (uint32_t block = 0; valid && block < count; block += 1)
        {
            valid = counters[block] == cfg->predecessor_offsets[(uint64_t)block + 1] - cfg->predecessor_offsets[block];
            counters[block] = cfg->predecessor_offsets[block];
        }
        if (valid)
        {
            for (uint32_t block = 0; block < count; block += 1)
            {
                for (uint32_t edge = cfg->successor_offsets[block]; edge < cfg->successor_offsets[(uint64_t)block + 1]; edge += 1)
                {
                    const uint32_t target = cfg->successors[edge];
                    transpose[counters[target]] = block;
                    counters[target] += 1;
                }
            }
            memset(counters, 0, (uint64_t)count * sizeof(uint32_t));
            for (uint32_t block = 0; valid && block < count; block += 1)
            {
                const uint32_t first = cfg->predecessor_offsets[block];
                const uint32_t end = cfg->predecessor_offsets[(uint64_t)block + 1];
                for (uint32_t edge = first; edge < end; edge += 1)
                {
                    counters[transpose[edge]] += 1;
                }
                for (uint32_t edge = first; valid && edge < end; edge += 1)
                {
                    const uint32_t source = cfg->predecessors[edge];
                    valid = counters[source] != 0;
                    if (valid)
                    {
                        counters[source] -= 1;
                    }
                }
            }
        }
        if (!valid)
        {
            status = DOMINATOR_INVALID;
        }
    }
    dominators_record_scratch(result, scratch, saved);
    arena_pop_to(scratch, saved);
    return status;
}

static void dominators_order(const DominatorCfg* cfg, Dominators* result, DominatorFrame* stack, uint32_t* dfs_begin, uint32_t* dfs_end)
{
    const uint32_t count = cfg->block_count;
    memset(result->rpo_index, 0xff, (uint64_t)count * sizeof(uint32_t));
    memset(dfs_begin, 0xff, (uint64_t)count * sizeof(uint32_t));
    memset(dfs_end, 0xff, (uint64_t)count * sizeof(uint32_t));
    uint32_t entered = 1;
    uint32_t finished = 0;
    uint64_t depth = 1;
    dfs_begin[cfg->entry] = 0;
    stack[0] = (DominatorFrame){
        .block = cfg->entry,
        .next = cfg->successor_offsets[cfg->entry],
    };
    while (depth != 0)
    {
        DominatorFrame* frame = &stack[depth - 1];
        if (frame->next < cfg->successor_offsets[(uint64_t)frame->block + 1])
        {
            const uint32_t target = cfg->successors[frame->next];
            frame->next += 1;
            if (dfs_begin[target] == BUSTER_DOMINATOR_NONE)
            {
                dfs_begin[target] = entered;
                entered += 1;
                stack[depth] = (DominatorFrame){
                    .block = target,
                    .next = cfg->successor_offsets[target],
                };
                depth += 1;
            }
        }
        else
        {
            result->rpo[finished] = frame->block;
            finished += 1;
            dfs_end[frame->block] = entered;
            depth -= 1;
        }
    }
    result->reachable_count = finished;
    for (uint32_t i = 0; i < finished / 2; i += 1)
    {
        const uint32_t other = finished - i - 1;
        const uint32_t block = result->rpo[i];
        result->rpo[i] = result->rpo[other];
        result->rpo[other] = block;
    }
    for (uint32_t i = 0; i < finished; i += 1)
    {
        result->rpo_index[result->rpo[i]] = i;
    }
    for (uint32_t i = finished; i < count; i += 1)
    {
        result->rpo[i] = BUSTER_DOMINATOR_NONE;
    }
}

static uint32_t dominators_intersect(const Dominators* result, uint32_t a, uint32_t b)
{
    while (a != b)
    {
        if (result->rpo_index[a] > result->rpo_index[b])
        {
            a = result->idom[a];
        }
        else
        {
            b = result->idom[b];
        }
    }
    return a;
}

static void dominators_scalar(const DominatorCfg* cfg, Dominators* result)
{
    memset(result->idom, 0xff, (uint64_t)cfg->block_count * sizeof(uint32_t));
    result->idom[cfg->entry] = cfg->entry;
    bool changed;
    do
    {
        changed = false;
        result->iterations += 1;
        for (uint32_t order = 1; order < result->reachable_count; order += 1)
        {
            const uint32_t block = result->rpo[order];
            uint32_t immediate = BUSTER_DOMINATOR_NONE;
            for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                const uint32_t source = cfg->predecessors[edge];
                if (result->idom[source] != BUSTER_DOMINATOR_NONE)
                {
                    immediate = immediate == BUSTER_DOMINATOR_NONE ? source : dominators_intersect(result, source, immediate);
                }
            }
            changed = changed || result->idom[block] != immediate;
            result->idom[block] = immediate;
        }
    } while (changed);
}

static bool dominators_bit(const uint64_t* row, uint32_t bit)
{
    return ((row[bit / 64] >> (bit % 64)) & 1) != 0;
}

static void dominators_set(uint64_t* row, uint32_t bit)
{
    row[bit / 64] |= UINT64_C(1) << (bit % 64);
}

static void dominators_vector(const DominatorCfg* cfg, Dominators* result, uint64_t* sets, uint64_t* reachable, uint32_t* sizes)
{
    const uint64_t words = result->row_words;
    const uint64_t bytes = words * sizeof(uint64_t);
    memset(sets, 0, (uint64_t)cfg->block_count * bytes);
    memset(reachable, 0, bytes);
    for (uint32_t order = 0; order < result->reachable_count; order += 1)
    {
        dominators_set(reachable, result->rpo[order]);
    }
    for (uint32_t order = 1; order < result->reachable_count; order += 1)
    {
        memcpy(sets + (uint64_t)result->rpo[order] * words, reachable, bytes);
    }
    dominators_set(sets + (uint64_t)cfg->entry * words, cfg->entry);
    bool changed;
    do
    {
        changed = false;
        result->iterations += 1;
        for (uint32_t order = 1; order < result->reachable_count; order += 1)
        {
            const uint32_t block = result->rpo[order];
            uint64_t* row = sets + (uint64_t)block * words;
            for (uint64_t word = 0; word < words; word += BUSTER_DOMINATOR_VECTOR_WORDS)
            {
                __m512i value = _mm512_load_si512(reachable + word);
                for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
                {
                    const uint32_t source = cfg->predecessors[edge];
                    if (result->rpo_index[source] != BUSTER_DOMINATOR_NONE)
                    {
                        value = _mm512_and_si512(value, _mm512_load_si512(sets + (uint64_t)source * words + word));
                    }
                }
                const __m512i lane = _mm512_setr_epi64(word, word + 1, word + 2, word + 3, word + 4, word + 5, word + 6, word + 7);
                const __mmask8 own_word = _mm512_cmpeq_epi64_mask(lane, _mm512_set1_epi64(block / 64));
                const __m512i own_bit = _mm512_maskz_set1_epi64(own_word, UINT64_C(1) << (block % 64));
                value = _mm512_or_si512(value, own_bit);
                const __m512i difference = _mm512_xor_si512(value, _mm512_load_si512(row + word));
                changed = (_mm512_test_epi64_mask(difference, difference) != 0) || changed;
                _mm512_store_si512(row + word, value);
            }
        }
    } while (changed);
    memset(result->idom, 0xff, (uint64_t)cfg->block_count * sizeof(uint32_t));
    memset(sizes, 0, (uint64_t)cfg->block_count * sizeof(uint32_t));
    for (uint32_t order = 0; order < result->reachable_count; order += 1)
    {
        const uint32_t block = result->rpo[order];
        __m512i sums = _mm512_setzero_si512();
        for (uint64_t word = 0; word < words; word += BUSTER_DOMINATOR_VECTOR_WORDS)
        {
            sums = _mm512_add_epi64(sums, _mm512_popcnt_epi64(_mm512_load_si512(sets + (uint64_t)block * words + word)));
        }
        sizes[block] = (uint32_t)_mm512_reduce_add_epi64(sums);
    }
    result->idom[cfg->entry] = cfg->entry;
    for (uint32_t order = 1; order < result->reachable_count; order += 1)
    {
        const uint32_t block = result->rpo[order];
        const uint64_t* row = sets + (uint64_t)block * words;
        uint32_t largest = 0;
        for (uint64_t word = 0; word < words; word += 1)
        {
            uint64_t bits = row[word];
            while (bits != 0)
            {
                const uint32_t candidate = (uint32_t)(word * 64 + __builtin_ctzll(bits));
                bits &= bits - 1;
                if (candidate != block && sizes[candidate] > largest)
                {
                    largest = sizes[candidate];
                    result->idom[block] = candidate;
                }
            }
        }
    }
}

static void dominators_tree(Dominators* result, DominatorFrame* stack, uint32_t* cursors)
{
    const uint32_t count = result->block_count;
    memset(result->tree_offsets, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    // Empty intervals for unreachable blocks are directly consumable by IrDominance's enter/leave view.
    memset(result->preorder, 0, (uint64_t)count * sizeof(uint32_t));
    memset(result->subtree_end, 0, (uint64_t)count * sizeof(uint32_t));
    for (uint32_t order = 1; order < result->reachable_count; order += 1)
    {
        result->tree_offsets[(uint64_t)result->idom[result->rpo[order]] + 1] += 1;
    }
    for (uint32_t block = 0; block < count; block += 1)
    {
        result->tree_offsets[(uint64_t)block + 1] += result->tree_offsets[block];
        cursors[block] = result->tree_offsets[block];
    }
    for (uint32_t block = 0; block < count; block += 1)
    {
        if (block != result->entry && result->idom[block] != BUSTER_DOMINATOR_NONE)
        {
            const uint32_t parent = result->idom[block];
            result->tree_children[cursors[parent]] = block;
            cursors[parent] += 1;
        }
    }
    uint32_t entered = 1;
    uint64_t depth = 1;
    result->preorder[result->entry] = 0;
    stack[0] = (DominatorFrame){
        .block = result->entry,
        .next = result->tree_offsets[result->entry],
    };
    while (depth != 0)
    {
        DominatorFrame* frame = &stack[depth - 1];
        if (frame->next < result->tree_offsets[(uint64_t)frame->block + 1])
        {
            const uint32_t child = result->tree_children[frame->next];
            frame->next += 1;
            result->preorder[child] = entered;
            entered += 1;
            stack[depth] = (DominatorFrame){
                .block = child,
                .next = result->tree_offsets[child],
            };
            depth += 1;
        }
        else
        {
            result->subtree_end[frame->block] = entered;
            depth -= 1;
        }
    }
}

static bool dominators_dominates(const Dominators* dominators, uint32_t dominator, uint32_t block)
{
    bool result = dominator < dominators->block_count && block < dominators->block_count;
    if (result)
    {
        result = dominators->idom[dominator] != BUSTER_DOMINATOR_NONE && dominators->idom[block] != BUSTER_DOMINATOR_NONE;
        result = result && dominators->preorder[dominator] <= dominators->preorder[block];
        result = result && dominators->preorder[block] < dominators->subtree_end[dominator];
    }
    return result;
}

static void dominators_frontiers(const DominatorCfg* cfg, Dominators* result)
{
    memset(result->frontiers, 0, (uint64_t)cfg->block_count * result->row_words * sizeof(uint64_t));
    for (uint32_t order = 0; order < result->reachable_count; order += 1)
    {
        const uint32_t block = result->rpo[order];
        // Entry can have incoming reachable edges: its own frontier contains entry in that case.
        const uint32_t stop = block == cfg->entry ? BUSTER_DOMINATOR_NONE : result->idom[block];
        for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            uint32_t runner = cfg->predecessors[edge];
            if (result->rpo_index[runner] != BUSTER_DOMINATOR_NONE)
            {
                while (runner != stop)
                {
                    dominators_set(result->frontiers + (uint64_t)runner * result->row_words, block);
                    runner = runner == cfg->entry ? BUSTER_DOMINATOR_NONE : result->idom[runner];
                }
            }
        }
    }
}

// Kosaraju's reverse walk, followed by DFS-ancestor edge tests. Mark whole SCCs, a conservative LICM boundary.
static void dominators_components(const DominatorCfg* cfg, Dominators* result, uint32_t* component, uint32_t* stack, uint8_t* flags, const uint32_t* dfs_begin, const uint32_t* dfs_end)
{
    memset(component, 0xff, (uint64_t)cfg->block_count * sizeof(uint32_t));
    memset(flags, 0, cfg->block_count);
    uint32_t components = 0;
    for (uint32_t order = 0; order < result->reachable_count; order += 1)
    {
        const uint32_t root = result->rpo[order];
        if (component[root] == BUSTER_DOMINATOR_NONE)
        {
            uint64_t depth = 1;
            stack[0] = root;
            component[root] = components;
            while (depth != 0)
            {
                depth -= 1;
                const uint32_t block = stack[depth];
                for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
                {
                    const uint32_t source = cfg->predecessors[edge];
                    if (result->rpo_index[source] != BUSTER_DOMINATOR_NONE && component[source] == BUSTER_DOMINATOR_NONE)
                    {
                        component[source] = components;
                        stack[depth] = source;
                        depth += 1;
                    }
                }
            }
            components += 1;
        }
    }
    for (uint32_t order = 0; order < result->reachable_count; order += 1)
    {
        const uint32_t block = result->rpo[order];
        for (uint32_t edge = cfg->successor_offsets[block]; edge < cfg->successor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t target = cfg->successors[edge];
            const bool ancestor = dfs_begin[target] <= dfs_begin[block] && dfs_begin[block] < dfs_end[target];
            if (ancestor && !dominators_dominates(result, target, block))
            {
                flags[component[block]] = 1;
            }
        }
    }
    for (uint32_t block = 0; block < cfg->block_count; block += 1)
    {
        result->irreducible[block] = component[block] == BUSTER_DOMINATOR_NONE ? 0 : flags[component[block]];
    }
}

static void dominators_loop_scalar(const DominatorCfg* cfg, const Dominators* result, uint32_t header, uint64_t* body, uint32_t* stack)
{
    dominators_set(body, header);
    uint64_t depth = 0;
    for (uint32_t edge = cfg->predecessor_offsets[header]; edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
    {
        const uint32_t latch = cfg->predecessors[edge];
        if (dominators_dominates(result, header, latch) && !dominators_bit(body, latch))
        {
            dominators_set(body, latch);
            stack[depth] = latch;
            depth += 1;
        }
    }
    while (depth != 0)
    {
        depth -= 1;
        const uint32_t block = stack[depth];
        for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t source = cfg->predecessors[edge];
            if (result->rpo_index[source] != BUSTER_DOMINATOR_NONE && !dominators_bit(body, source))
            {
                dominators_set(body, source);
                stack[depth] = source;
                depth += 1;
            }
        }
    }
}

static void dominators_loop_vector(const DominatorCfg* cfg, const Dominators* result, uint32_t header, uint64_t* body, const uint64_t* predecessors, uint64_t* frontier, uint64_t* next)
{
    const uint64_t words = result->row_words;
    const uint64_t bytes = words * sizeof(uint64_t);
    memset(frontier, 0, bytes);
    dominators_set(body, header);
    for (uint32_t edge = cfg->predecessor_offsets[header]; edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
    {
        const uint32_t latch = cfg->predecessors[edge];
        if (latch != header && dominators_dominates(result, header, latch))
        {
            dominators_set(body, latch);
            dominators_set(frontier, latch);
        }
    }
    bool changed;
    do
    {
        memset(next, 0, bytes);
        for (uint64_t word = 0; word < words; word += 1)
        {
            uint64_t bits = frontier[word];
            while (bits != 0)
            {
                const uint32_t block = (uint32_t)(word * 64 + __builtin_ctzll(bits));
                bits &= bits - 1;
                const uint64_t* row = predecessors + (uint64_t)block * words;
                for (uint64_t w = 0; w < words; w += BUSTER_DOMINATOR_VECTOR_WORDS)
                {
                    _mm512_store_si512(next + w, _mm512_or_si512(_mm512_load_si512(next + w), _mm512_load_si512(row + w)));
                }
            }
        }
        changed = false;
        for (uint64_t word = 0; word < words; word += BUSTER_DOMINATOR_VECTOR_WORDS)
        {
            const __m512i old = _mm512_load_si512(body + word);
            const __m512i added = _mm512_andnot_si512(old, _mm512_load_si512(next + word));
            changed = (_mm512_test_epi64_mask(added, added) != 0) || changed;
            _mm512_store_si512(frontier + word, added);
            _mm512_store_si512(body + word, _mm512_or_si512(old, added));
        }
    } while (changed);
}

static DominatorStatus dominators_loops(Arena* arena, Arena* scratch, const DominatorCfg* cfg, DominatorOptions options, Dominators* result, uint32_t* workspace, const uint32_t* dfs_begin, const uint32_t* dfs_end, uint64_t scratch_start)
{
    const uint32_t count = cfg->block_count;
    const uint64_t row_bytes = result->row_words * sizeof(uint64_t);
    const uint64_t matrix_bytes = (uint64_t)count * row_bytes;
    const uint64_t frontier_bytes = result->frontiers != 0 ? matrix_bytes : 0;
    const uint64_t saved = scratch->length;
    result->loop_depth = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
    result->block_loop = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
    result->irreducible = arena_push_array(arena, count, sizeof(uint8_t), _Alignof(uint8_t));
    uint32_t* stack = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
    uint8_t* flags = arena_push_array(scratch, count, sizeof(uint8_t), _Alignof(uint8_t));
    DominatorStatus status = result->loop_depth != 0 && result->block_loop != 0 && result->irreducible != 0 && stack != 0 && flags != 0 ? DOMINATOR_OK : DOMINATOR_NO_MEMORY;
    if (status == DOMINATOR_OK)
    {
        dominators_components(cfg, result, workspace, stack, flags, dfs_begin, dfs_end);
        memset(flags, 0, count);
        for (uint32_t order = 0; order < result->reachable_count; order += 1)
        {
            const uint32_t block = result->rpo[order];
            for (uint32_t edge = cfg->successor_offsets[block]; edge < cfg->successor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                const uint32_t target = cfg->successors[edge];
                flags[target] |= dominators_dominates(result, target, block);
            }
        }
        for (uint32_t block = 0; block < count; block += 1)
        {
            result->loop_count += flags[block];
        }
        memset(result->loop_depth, 0, (uint64_t)count * sizeof(uint32_t));
        memset(result->block_loop, 0xff, (uint64_t)count * sizeof(uint32_t));
    }
    const uint64_t body_bytes = (uint64_t)result->loop_count * row_bytes;
    const uint64_t kept_bytes = frontier_bytes + body_bytes;
    if (status == DOMINATOR_OK && kept_bytes > options.max_matrix_bytes)
    {
        status = DOMINATOR_MATRIX_LIMIT;
    }
    if (status == DOMINATOR_OK && kept_bytes > result->matrix_peak_bytes)
    {
        result->matrix_peak_bytes = kept_bytes;
    }
    uint64_t* predecessor_rows = 0;
    uint64_t* frontier = 0;
    uint64_t* next = 0;
    if (status == DOMINATOR_OK && result->loop_count != 0)
    {
        const uint64_t vector_bytes = kept_bytes + matrix_bytes + 2 * row_bytes;
        result->loop_path = options.path == DOMINATOR_SCALAR || vector_bytes > options.max_matrix_bytes ? DOMINATOR_SCALAR : DOMINATOR_VECTOR;
        if (options.path == DOMINATOR_VECTOR && result->loop_path != DOMINATOR_VECTOR)
        {
            status = DOMINATOR_MATRIX_LIMIT;
        }
        if (status == DOMINATOR_OK)
        {
            result->loop_header = arena_push_array(arena, result->loop_count, sizeof(uint32_t), _Alignof(uint32_t));
            result->loop_parent = arena_push_array(arena, result->loop_count, sizeof(uint32_t), _Alignof(uint32_t));
            result->loop_size = arena_push_array(arena, result->loop_count, sizeof(uint32_t), _Alignof(uint32_t));
            result->loop_irreducible = arena_push_array(arena, result->loop_count, sizeof(uint8_t), _Alignof(uint8_t));
            result->loop_bodies = arena_push(arena, body_bytes, BUSTER_DOMINATOR_VECTOR_BYTES);
            if (result->loop_header == 0 || result->loop_parent == 0 || result->loop_size == 0 || result->loop_irreducible == 0 || result->loop_bodies == 0)
            {
                status = DOMINATOR_NO_MEMORY;
            }
        }
        if (status == DOMINATOR_OK && result->loop_path == DOMINATOR_VECTOR)
        {
            predecessor_rows = arena_push(scratch, matrix_bytes, BUSTER_DOMINATOR_VECTOR_BYTES);
            frontier = arena_push(scratch, row_bytes, BUSTER_DOMINATOR_VECTOR_BYTES);
            next = arena_push(scratch, row_bytes, BUSTER_DOMINATOR_VECTOR_BYTES);
            if (predecessor_rows == 0 || frontier == 0 || next == 0)
            {
                status = DOMINATOR_NO_MEMORY;
            }
            else
            {
                if (vector_bytes > result->matrix_peak_bytes)
                {
                    result->matrix_peak_bytes = vector_bytes;
                }
                memset(predecessor_rows, 0, matrix_bytes);
                for (uint32_t order = 0; order < result->reachable_count; order += 1)
                {
                    const uint32_t block = result->rpo[order];
                    uint64_t* row = predecessor_rows + (uint64_t)block * result->row_words;
                    for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
                    {
                        const uint32_t source = cfg->predecessors[edge];
                        if (result->rpo_index[source] != BUSTER_DOMINATOR_NONE)
                        {
                            dominators_set(row, source);
                        }
                    }
                }
            }
        }
        if (status == DOMINATOR_OK)
        {
            memset(result->loop_bodies, 0, body_bytes);
            uint32_t loop = 0;
            for (uint32_t header = 0; header < count; header += 1)
            {
                if (flags[header] != 0)
                {
                    result->loop_header[loop] = header;
                    uint64_t* body = result->loop_bodies + (uint64_t)loop * result->row_words;
                    if (result->loop_path == DOMINATOR_VECTOR)
                    {
                        dominators_loop_vector(cfg, result, header, body, predecessor_rows, frontier, next);
                    }
                    else
                    {
                        dominators_loop_scalar(cfg, result, header, body, stack);
                    }
                    uint32_t size = 0;
                    uint8_t irreducible = 0;
                    for (uint64_t word = 0; word < result->row_words; word += 1)
                    {
                        uint64_t bits = body[word];
                        while (bits != 0)
                        {
                            const uint32_t block = (uint32_t)(word * 64 + __builtin_ctzll(bits));
                            bits &= bits - 1;
                            size += 1;
                            result->loop_depth[block] += 1;
                            irreducible |= result->irreducible[block];
                        }
                    }
                    result->loop_size[loop] = size;
                    result->loop_irreducible[loop] = irreducible;
                    loop += 1;
                }
            }
            for (uint32_t inner = 0; inner < result->loop_count; inner += 1)
            {
                uint32_t parent = BUSTER_DOMINATOR_NONE;
                uint32_t smallest = UINT32_MAX;
                for (uint32_t outer = 0; outer < result->loop_count; outer += 1)
                {
                    const uint64_t* body = result->loop_bodies + (uint64_t)outer * result->row_words;
                    if (inner != outer && result->loop_size[outer] > result->loop_size[inner] && (parent == BUSTER_DOMINATOR_NONE || result->loop_size[outer] < smallest) && dominators_bit(body, result->loop_header[inner]))
                    {
                        parent = outer;
                        smallest = result->loop_size[outer];
                    }
                }
                result->loop_parent[inner] = parent;
                const uint64_t* body = result->loop_bodies + (uint64_t)inner * result->row_words;
                for (uint64_t word = 0; word < result->row_words; word += 1)
                {
                    uint64_t bits = body[word];
                    while (bits != 0)
                    {
                        const uint32_t block = (uint32_t)(word * 64 + __builtin_ctzll(bits));
                        bits &= bits - 1;
                        const uint32_t previous = result->block_loop[block];
                        if (previous == BUSTER_DOMINATOR_NONE || result->loop_size[inner] < result->loop_size[previous])
                        {
                            result->block_loop[block] = inner;
                        }
                    }
                }
            }
        }
    }
    dominators_record_scratch(result, scratch, scratch_start);
    arena_pop_to(scratch, saved);
    return status;
}

static DominatorStatus dominators_analyze(Arena* arena, Arena* scratch, const DominatorCfg* cfg, DominatorOptions options, Dominators* result)
{
    DominatorStatus status = DOMINATOR_INVALID;
    const bool arguments = arena != 0 && scratch != 0 && arena != scratch && cfg != 0 && result != 0;
    if (arguments)
    {
        const uint64_t saved = arena->length;
        const uint64_t scratch_start = scratch->length;
        Dominators candidate = {
            .block_count = cfg->block_count,
            .entry = cfg->entry,
            .outputs = options.outputs,
            .row_words = ((uint64_t)cfg->block_count + BUSTER_DOMINATOR_VECTOR_BITS - 1) / BUSTER_DOMINATOR_VECTOR_BITS * BUSTER_DOMINATOR_VECTOR_WORDS,
            .path = DOMINATOR_SCALAR,
            .loop_path = DOMINATOR_SCALAR,
        };
        const uint32_t count = cfg->block_count;
        const uint64_t row_bytes = candidate.row_words * sizeof(uint64_t);
        const uint64_t matrix_bytes = (uint64_t)count * row_bytes;
        const bool option_valid = options.path >= DOMINATOR_AUTO && options.path <= DOMINATOR_SCALAR && (options.outputs & ~(BUSTER_DOMINATOR_FRONTIERS | BUSTER_DOMINATOR_LOOPS)) == 0;
        if (option_valid)
        {
            status = dominators_validate(cfg, scratch, &candidate);
        }
        if (status == DOMINATOR_OK && (options.outputs & BUSTER_DOMINATOR_FRONTIERS) != 0 && matrix_bytes > options.max_matrix_bytes)
        {
            status = DOMINATOR_MATRIX_LIMIT;
        }
        DominatorFrame* frames = 0;
        uint32_t* dfs_begin = 0;
        uint32_t* dfs_end = 0;
        uint32_t* workspace = 0;
        if (status == DOMINATOR_OK && count != 0)
        {
            candidate.idom = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.rpo = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.rpo_index = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.tree_offsets = arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.tree_children = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.preorder = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.subtree_end = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            frames = arena_push_array(scratch, count, sizeof(DominatorFrame), _Alignof(DominatorFrame));
            dfs_begin = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
            dfs_end = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
            workspace = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
            if (candidate.idom == 0 || candidate.rpo == 0 || candidate.rpo_index == 0 || candidate.tree_offsets == 0 || candidate.tree_children == 0 || candidate.preorder == 0 || candidate.subtree_end == 0 || frames == 0 || dfs_begin == 0 || dfs_end == 0 || workspace == 0)
            {
                status = DOMINATOR_NO_MEMORY;
            }
        }
        if (status == DOMINATOR_OK && count != 0)
        {
            dominators_order(cfg, &candidate, frames, dfs_begin, dfs_end);
            const uint64_t vector_bytes = matrix_bytes + row_bytes;
            candidate.path = options.path == DOMINATOR_SCALAR || vector_bytes > options.max_matrix_bytes ? DOMINATOR_SCALAR : DOMINATOR_VECTOR;
            if (options.path == DOMINATOR_VECTOR && candidate.path != DOMINATOR_VECTOR)
            {
                status = DOMINATOR_MATRIX_LIMIT;
            }
            if (status == DOMINATOR_OK && candidate.path == DOMINATOR_VECTOR)
            {
                const uint64_t phase_start = scratch->length;
                uint64_t* sets = arena_push(scratch, matrix_bytes, BUSTER_DOMINATOR_VECTOR_BYTES);
                uint64_t* reachable = arena_push(scratch, row_bytes, BUSTER_DOMINATOR_VECTOR_BYTES);
                if (sets == 0 || reachable == 0)
                {
                    status = DOMINATOR_NO_MEMORY;
                }
                else
                {
                    candidate.matrix_peak_bytes = vector_bytes;
                    dominators_vector(cfg, &candidate, sets, reachable, workspace);
                }
                dominators_record_scratch(&candidate, scratch, scratch_start);
                arena_pop_to(scratch, phase_start);
            }
            else if (status == DOMINATOR_OK)
            {
                dominators_scalar(cfg, &candidate);
            }
        }
        if (status == DOMINATOR_OK && count != 0)
        {
            dominators_tree(&candidate, frames, workspace);
            if ((options.outputs & BUSTER_DOMINATOR_FRONTIERS) != 0)
            {
                candidate.frontiers = arena_push(arena, matrix_bytes, BUSTER_DOMINATOR_VECTOR_BYTES);
                if (candidate.frontiers == 0)
                {
                    status = DOMINATOR_NO_MEMORY;
                }
                else
                {
                    if (matrix_bytes > candidate.matrix_peak_bytes)
                    {
                        candidate.matrix_peak_bytes = matrix_bytes;
                    }
                    dominators_frontiers(cfg, &candidate);
                }
            }
            if (status == DOMINATOR_OK && (options.outputs & BUSTER_DOMINATOR_LOOPS) != 0)
            {
                status = dominators_loops(arena, scratch, cfg, options, &candidate, workspace, dfs_begin, dfs_end, scratch_start);
            }
        }
        dominators_record_scratch(&candidate, scratch, scratch_start);
        arena_pop_to(scratch, scratch_start);
        if (status == DOMINATOR_OK)
        {
            candidate.output_bytes = arena->length - saved;
            *result = candidate;
        }
        else
        {
            arena_pop_to(arena, saved);
        }
    }
    return status;
}

static bool dominators_frontier_contains(const Dominators* dominators, uint32_t block, uint32_t member)
{
    bool result = dominators->frontiers != 0 && block < dominators->block_count && member < dominators->block_count;
    if (result)
    {
        result = dominators_bit(dominators->frontiers + (uint64_t)block * dominators->row_words, member);
    }
    return result;
}

static bool dominators_loop_contains(const Dominators* dominators, uint32_t loop, uint32_t block)
{
    bool result = loop < dominators->loop_count && block < dominators->block_count;
    if (result)
    {
        result = dominators_bit(dominators->loop_bodies + (uint64_t)loop * dominators->row_words, block);
    }
    return result;
}
