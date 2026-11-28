// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Entry-rooted dominators, frontiers and natural loops for a borrowed CSR CFG. Include after lane.c.
// dominators_analyze validates both CSR views, computes reachable reverse postorder and DFS preorder, solves by
// SEMI-NCA, then builds the tree and the requested frontier/loop sets. Loops are found innermost first and absorb their
// inner loops' body rows by 512-bit ORs (dominators_discover, #2953). SEMI-NCA and the loops' union-find walk are scalar:
// on the 9700X SEMI-NCA beat the AVX-512 bit-matrix solver it replaced and the window sweep #2953 measured at the large
// sizes (#1352, #2924, #2953; docs/dominators.md, "Why it is scalar").
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
// Layout: dominators_validate; dominators_order; dominators_semi_nca; dominators_tree/frontiers;
// dominators_components; dominators_outermost/discover/loops; dominators_analyze; the consumer query helpers.

#include <immintrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
    uint32_t outputs;
    // Peak bytes of simultaneously live bitset matrices: the frontiers and the loop bodies. Zero permits none.
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
    uint64_t matrix_peak_bytes;
    uint64_t output_bytes;
    uint64_t scratch_peak_bytes;
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

// SEMI-NCA's virtual forest, indexed by DFS preorder number: a processed vertex's ancestor link, compressed toward
// its tree's unprocessed root, and the least semidominator number on the path up to, excluding, that root.
typedef struct DominatorLink DominatorLink;
struct DominatorLink
{
    uint32_t ancestor;
    uint32_t least;
};

static __attribute__((unused)) DominatorOptions dominators_options(void);
static __attribute__((unused)) DominatorStatus dominators_analyze(Arena* arena, Arena* scratch, const DominatorCfg* cfg, DominatorOptions options, Dominators* result);
static __attribute__((unused)) bool dominators_dominates(const Dominators* dominators, uint32_t dominator, uint32_t block);
static __attribute__((unused)) bool dominators_frontier_contains(const Dominators* dominators, uint32_t block, uint32_t member);
static __attribute__((unused)) bool dominators_loop_contains(const Dominators* dominators, uint32_t loop, uint32_t block);

static DominatorOptions dominators_options(void)
{
    const DominatorOptions result = {
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

// DFS numbers reachable blocks in preorder and reverse postorder. The DFS parent immediately
// dominates a block with just one incoming edge. Joins retain their preorder number for the semidominator pass. If
// loops are requested, record the sources of edges to ancestors still on the active DFS stack.
static uint32_t dominators_order(const DominatorCfg* cfg, Dominators* result, DominatorFrame* stack, uint32_t* dfs_begin, uint64_t* back_edge_sources, uint32_t* vertices, uint32_t* parents, uint32_t* joins)
{
    const uint32_t count = cfg->block_count;
    memset(result->rpo_index, 0xff, (uint64_t)count * sizeof(uint32_t));
    memset(dfs_begin, 0xff, (uint64_t)count * sizeof(uint32_t));
    if (back_edge_sources != NULL)
    {
        memset(back_edge_sources, 0, ((uint64_t)count + 63) / 64 * sizeof(uint64_t));
    }
    memset(result->idom, 0xff, (uint64_t)count * sizeof(uint32_t));
    result->idom[cfg->entry] = cfg->entry;
    uint32_t join_count = 0;
    bool has_alternate_entry = false;
    uint32_t entered = 1;
    uint32_t finished = 0;
    uint64_t depth = 1;
    dfs_begin[cfg->entry] = 0;
    vertices[0] = cfg->entry;
    parents[0] = 0;
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
                // Its DFS parent is the immediate dominator if this is its only incoming edge.
                result->idom[target] = frame->block;
                joins[join_count] = entered;
                join_count += cfg->predecessor_offsets[(uint64_t)target + 1] - cfg->predecessor_offsets[target] > 1;
                vertices[entered] = target;
                parents[entered] = dfs_begin[frame->block];
                dfs_begin[target] = entered;
                entered += 1;
                stack[depth] = (DominatorFrame){
                    .block = target,
                    .next = cfg->successor_offsets[target],
                };
                depth += 1;
            }
            else
            {
                // An edge to a block still on the DFS stack goes back to an ancestor. With only
                // tree and back edges, each DFS parent already dominates its entire child subtree.
                const bool finished_target = result->rpo_index[target] != BUSTER_DOMINATOR_NONE;
                has_alternate_entry |= finished_target;
                if (!finished_target && back_edge_sources != NULL)
                {
                    back_edge_sources[frame->block / 64] |= UINT64_C(1) << (frame->block % 64);
                }
            }
        }
        else
        {
            const uint32_t order = count - 1 - finished;
            result->rpo[order] = frame->block;
            result->rpo_index[frame->block] = order;
            finished += 1;
            depth -= 1;
        }
    }
    result->reachable_count = finished;
    if (finished < count)
    {
        memmove(result->rpo, result->rpo + (count - finished), (uint64_t)finished * sizeof(uint32_t));
        for (uint32_t i = 0; i < finished; i += 1)
        {
            result->rpo_index[result->rpo[i]] = i;
        }
        memset(result->rpo + finished, 0xff, (uint64_t)(count - finished) * sizeof(uint32_t));
    }
    return has_alternate_entry ? join_count : 0;
}

// The least semidominator number on number's processed path, up to and excluding the first ancestor numbered below
// processed. Compression points every vertex on that path at the excluded ancestor, carrying the path's minimum.
static uint32_t dominators_evaluate(DominatorLink* links, uint32_t* stack, uint32_t number, uint32_t processed)
{
    uint32_t result = links[number].least;
    if (links[number].ancestor >= processed)
    {
        uint64_t depth = 0;
        uint32_t top = number;
        do
        {
            stack[depth] = top;
            depth += 1;
            top = links[top].ancestor;
        } while (links[top].ancestor >= processed);
        const uint32_t root = links[top].ancestor;
        uint32_t least = links[top].least;
        do
        {
            depth -= 1;
            DominatorLink* link = &links[stack[depth]];
            link->ancestor = root;
            least = link->least < least ? link->least : least;
            link->least = least;
        } while (depth != 0);
        result = least;
    }
    return result;
}

// Populate a compressed ancestor path on first use. A larger jump combines two equal-length
// jumps below the parent. Once populated, each vertex can service weighted-ancestor queries
// without climbing each contributing dominator-tree level.
static __attribute__((noinline)) uint32_t dominators_cached_ancestor(uint32_t candidate, uint32_t limit, const uint32_t* parents, DominatorLink* links,
                                                                     uint32_t* stack, uint32_t* ready, uint32_t reachable, bool* initialized)
{
    if (!*initialized)
    {
        *initialized = true;
        memset(ready, 0, ((uint64_t)reachable + 31) / 32 * sizeof(uint32_t));
        ready[0] = 1;
        links[0] = (DominatorLink){0};
    }
    uint32_t length = 0;
    uint32_t top = candidate;
    while (!(ready[top / 32] & (1u << (top % 32))))
    {
        stack[length++] = top;
        top = parents[top];
    }
    while (length)
    {
        const uint32_t node = stack[--length];
        const uint32_t parent = parents[node];
        const uint32_t skip = links[parent].ancestor;
        const uint32_t twice = links[skip].ancestor;
        const uint32_t above = links[parent].least - links[skip].least;
        const uint32_t further = links[skip].least - links[twice].least;
        links[node].ancestor = above == further ? twice : parent;
        links[node].least = links[parent].least + 1;
        ready[node / 32] |= 1u << (node % 32);
    }
    while (candidate > limit)
    {
        const uint32_t skip = links[candidate].ancestor;
        candidate = skip > limit ? skip : parents[candidate];
    }
    return candidate;
}

// SEMI-NCA in DFS preorder numbers: semidominators in decreasing order through the compressed forest of processed
// vertices, then each idom is the nearest common dominator-tree ancestor of its DFS parent and semidominator, by a
// walk up the finished tree that is short unless a deep block has many DFS children dominated from far above it.
// One pass; parents is overwritten by the idom numbers.
static void dominators_semi_nca(const DominatorCfg* cfg, Dominators* result, const uint32_t* dfs_begin, const uint32_t* vertices, uint32_t* parents, uint32_t* semi, DominatorLink* links, uint32_t* stack, const uint32_t* joins, uint32_t join_count)
{
    const uint32_t reachable = result->reachable_count;
    // A single-predecessor vertex's least label is its parent. Path compression reads these
    // labels only on processed vertices. A join's own label is final before it can be consulted.
    for (uint64_t i = 0; i < reachable; i += 8)
    {
        const uint32_t n = reachable - i < 8 ? (uint32_t)(reachable - i) : 8;
        const __mmask8 valid = (__mmask8)((1u << n) - 1);
        const __m256i parent = _mm256_maskz_loadu_epi32(valid, parents + i);
        const __m512i wide = _mm512_cvtepu32_epi64(parent);
        _mm512_mask_storeu_epi64(links + i, valid, _mm512_or_si512(wide, _mm512_slli_epi64(wide, 32)));
    }
    for (uint32_t j = join_count; j != 0;)
    {
        const uint32_t number = joins[--j];
        const uint32_t block = vertices[number];
        uint32_t least = parents[number];
        for (uint32_t edge = cfg->predecessor_offsets[block]; edge < cfg->predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t source = dfs_begin[cfg->predecessors[edge]];
            if (source < least)
            {
                least = source;
            }
            else if (source > number && source != BUSTER_DOMINATOR_NONE && least != 0)
            {
                const uint32_t candidate = dominators_evaluate(links, stack, source, number + 1);
                least = candidate < least ? candidate : least;
            }
        }
        semi[number] = least;
        links[number].least = least;
    }
    // Auxiliary jump links are created only for ancestor paths actually queried. Each vertex
    // is initialized once. Equal-length jumps coalesce, yielding logarithmically many hops.
    bool initialized = false;
    uint32_t* ready = result->preorder;
    for (uint32_t j = 0; j < join_count; ++j)
    {
        const uint32_t number = joins[j];
        const uint32_t limit = semi[number];
        uint32_t candidate = parents[number];
        if (candidate == limit)
        {
            // DFS already stored this parent in both representations.
            continue;
        }
        if (limit == 0)
        {
            candidate = 0;
        }
        else
        {
            unsigned steps = 4;
            while (candidate > limit && steps != 0)
            {
                candidate = parents[candidate];
                --steps;
            }
            if (candidate > limit)
            {
                candidate = dominators_cached_ancestor(candidate, limit, parents, links, stack, ready, reachable, &initialized);
            }
        }
        parents[number] = candidate;
        result->idom[vertices[number]] = vertices[candidate];
    }
}

static bool dominators_bit(const uint64_t* row, uint32_t bit)
{
    return ((row[bit / 64] >> (bit % 64)) & 1) != 0;
}

static void dominators_set(uint64_t* row, uint32_t bit)
{
    row[bit / 64] |= UINT64_C(1) << (bit % 64);
}

static void dominators_tree(Dominators* result, DominatorFrame* stack)
{
    const uint32_t count = result->block_count;
    uint32_t* offsets = result->tree_offsets;
    memset(offsets, 0, (uint64_t)count * sizeof(uint32_t));
    if (result->reachable_count != count)
    {
        memset(result->preorder, 0, (uint64_t)count * sizeof(uint32_t));
        memset(result->subtree_end, 0, (uint64_t)count * sizeof(uint32_t));
    }
    for (uint32_t block = 0; block < count; block += 1)
    {
        const uint32_t parent = result->idom[block];
        if (parent != BUSTER_DOMINATOR_NONE && block != result->entry)
        {
            offsets[parent] += 1;
        }
    }
    uint32_t end = 0;
    for (uint32_t block = 0; block < count; block += 1)
    {
        end += offsets[block];
        offsets[block] = end;
    }
    offsets[count] = end;
    // Filling each interval backwards restores its start and leaves siblings in ascending block order.
    for (uint32_t block = count; block != 0;)
    {
        block -= 1;
        const uint32_t parent = result->idom[block];
        if (parent != BUSTER_DOMINATOR_NONE && block != result->entry)
        {
            result->tree_children[--offsets[parent]] = block;
        }
    }
    uint32_t entered = 1;
    uint32_t block = result->entry;
    uint32_t next = offsets[block];
    uint64_t depth = 0;
    result->preorder[block] = 0;
    for (;;)
    {
        if (next < offsets[(uint64_t)block + 1])
        {
            const uint32_t child = result->tree_children[next++];
            result->preorder[child] = entered++;
            if (offsets[child] == offsets[(uint64_t)child + 1])
            {
                result->subtree_end[child] = entered;
            }
            else
            {
                stack[depth++] = (DominatorFrame){ .block = block, .next = next };
                block = child;
                next = offsets[child];
            }
        }
        else
        {
            result->subtree_end[block] = entered;
            if (depth == 0)
            {
                break;
            }
            const DominatorFrame frame = stack[--depth];
            block = frame.block;
            next = frame.next;
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

// Each predecessor's walk climbs the tree to the block's idom. One that reaches a runner whose frontier already holds
// the block stops: an earlier predecessor's walk passed there and marked the same chain on up (#1352). That keeps the
// walks to the frontiers' total size plus the edges, where a join of many predecessors under a deep chain was quadratic.
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
                while (runner != stop && !dominators_bit(result->frontiers + (uint64_t)runner * result->row_words, block))
                {
                    dominators_set(result->frontiers + (uint64_t)runner * result->row_words, block);
                    runner = runner == cfg->entry ? BUSTER_DOMINATOR_NONE : result->idom[runner];
                }
            }
        }
    }
}

// Kosaraju's reverse walk, followed by DFS-ancestor edge tests. Mark whole SCCs, a conservative LICM boundary.
static void dominators_components(const DominatorCfg* cfg, Dominators* result, uint32_t* component, uint32_t* stack, uint8_t* headers, const uint32_t* dfs_begin, const uint64_t* back_edge_sources)
{
    // loop_depth is available until the natural-loop forest is built.
    uint32_t* witnesses = result->loop_depth;
    uint32_t witness_count = 0;
    memset(headers, 0, cfg->block_count);
    for (uint64_t first = 0; first < cfg->block_count; first += 64)
    {
        uint64_t bits = back_edge_sources[first / 64];
        while (bits != 0)
        {
            const uint32_t block = (uint32_t)first + (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1;
            bool irregular = false;
            for (uint32_t edge = cfg->successor_offsets[block]; edge < cfg->successor_offsets[(uint64_t)block + 1]; ++edge)
            {
                const uint32_t target = cfg->successors[edge];
                // A DFS ancestor precedes the source in both orders. An earlier sibling follows it in RPO.
                if (dfs_begin[target] <= dfs_begin[block] && result->rpo_index[target] <= result->rpo_index[block])
                {
                    const bool dominates = result->preorder[target] <= result->preorder[block] && result->preorder[block] < result->subtree_end[target];
                    if (dominates)
                    {
                        headers[target] = 1;
                    }
                    irregular |= !dominates;
                }
            }
            if (irregular)
            {
                witnesses[witness_count++] = block;
            }
        }
    }
    if (witness_count == 0)
    {
        memset(result->irreducible, 0, cfg->block_count);
    }
    else
    {
        // Only a non-dominating DFS back edge requires the conservative whole-component LICM boundary.
        memset(component, 0xff, (uint64_t)cfg->block_count * sizeof(uint32_t));
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
        // The reverse walk is finished. Reuse its stack to mark components with a witness.
        memset(stack, 0, (uint64_t)components * sizeof(uint32_t));
        for (uint32_t i = 0; i < witness_count; i += 1)
        {
            stack[component[witnesses[i]]] = 1;
        }
        for (uint32_t block = 0; block < cfg->block_count; block += 1)
        {
            result->irreducible[block] = component[block] == BUSTER_DOMINATOR_NONE ? 0 : (uint8_t)stack[component[block]];
        }
    }
}

// The outermost discovered loop containing loop, by union-find with path compression.
static uint32_t dominators_outermost(uint32_t* outer, uint32_t loop)
{
    uint32_t root = loop;
    while (outer[root] != root)
    {
        root = outer[root];
    }
    uint32_t step = loop;
    while (outer[step] != root)
    {
        const uint32_t next = outer[step];
        outer[step] = root;
        step = next;
    }
    return root;
}

// Natural loops innermost first: by decreasing dominator-tree preorder of their headers, since an outer loop's header
// dominates its inner loops'. Each loop walks back from its latches and stops at its header. A block no loop has
// claimed becomes this loop's own; a block in a loop found before is in that loop's outermost discovered loop, which
// this one absorbs whole: it becomes that loop's parent, ORs that loop's body row into its own with 512-bit ORs, and
// walks on from that loop's header. A walk leaves a natural loop only through its header, so each block is claimed
// once and each header's predecessors are read at most twice: O(E α) to walk and O(loops × row) to OR the bodies,
// where a walk per loop costs the bodies' total size. by_number holds each block's preorder number's block.
static void dominators_discover(const DominatorCfg* cfg, Dominators* result, const uint32_t* by_number, const uint32_t* loop_of, uint32_t* outer, uint32_t* stack)
{
    const uint32_t* offsets = cfg->predecessor_offsets;
    const uint32_t* predecessors = cfg->predecessors;
    const uint32_t* rpo_index = result->rpo_index;
    const uint64_t row_words = result->row_words;
    uint32_t* block_loop = result->block_loop;
    uint64_t* bodies = result->loop_bodies;
    for (uint32_t number = result->reachable_count; number != 0; number -= 1)
    {
        const uint32_t header = by_number[number - 1];
        const uint32_t loop = loop_of[header];
        if (loop != BUSTER_DOMINATOR_NONE)
        {
            uint64_t* body = bodies + (uint64_t)loop * row_words;
            uint32_t size = 1;
            uint8_t irreducible = result->irreducible[header];
            outer[loop] = loop;
            block_loop[header] = loop;
            dominators_set(body, header);
            uint64_t depth = 0;
            uint32_t walked = header;
            bool latches = true;
            while (latches || depth != 0)
            {
                if (!latches)
                {
                    depth -= 1;
                    walked = stack[depth];
                }
                for (uint32_t edge = offsets[walked]; edge < offsets[(uint64_t)walked + 1]; edge += 1)
                {
                    const uint32_t source = predecessors[edge];
                    const bool reachable = rpo_index[source] != BUSTER_DOMINATOR_NONE;
                    // From the header, only its latches: the predecessors it dominates.
                    // Absorbed inner loops have already added every member to this row.
                    if (reachable && !dominators_bit(body, source) && (!latches || dominators_dominates(result, header, source)))
                    {
                        const uint32_t claimed = block_loop[source];
                        if (claimed == BUSTER_DOMINATOR_NONE)
                        {
                            block_loop[source] = loop;
                            dominators_set(body, source);
                            size += 1;
                            irreducible |= result->irreducible[source];
                            stack[depth] = source;
                            depth += 1;
                        }
                        else
                        {
                            const uint32_t inner = dominators_outermost(outer, claimed);
                            if (inner != loop)
                            {
                                outer[inner] = loop;
                                result->loop_parent[inner] = loop;
                                const uint64_t* inner_body = bodies + (uint64_t)inner * row_words;
                                for (uint64_t word = 0; word < row_words; word += BUSTER_DOMINATOR_VECTOR_WORDS)
                                {
                                    const __m512i merged = _mm512_or_si512(_mm512_load_si512(body + word), _mm512_load_si512(inner_body + word));
                                    _mm512_store_si512(body + word, merged);
                                }
                                size += result->loop_size[inner];
                                irreducible |= result->loop_irreducible[inner];
                                stack[depth] = result->loop_header[inner];
                                depth += 1;
                            }
                        }
                    }
                }
                latches = false;
            }
            result->loop_size[loop] = size;
            result->loop_irreducible[loop] = irreducible;
        }
    }
}

static DominatorStatus dominators_loops(Arena* arena, Arena* scratch, const DominatorCfg* cfg, DominatorOptions options, Dominators* result, uint32_t* workspace, const uint32_t* dfs_begin, const uint64_t* back_edge_sources, uint64_t scratch_start)
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
        dominators_components(cfg, result, workspace, stack, flags, dfs_begin, back_edge_sources);
        for (uint32_t block = 0; block < count; block += 1)
        {
            result->loop_count += flags[block];
        }
        if (result->loop_count == 0)
        {
            memset(result->loop_depth, 0, (uint64_t)count * sizeof(uint32_t));
        }
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
    if (status == DOMINATOR_OK && result->loop_count != 0)
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
        uint32_t* by_number = arena_push_array(scratch, result->reachable_count, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* outer = arena_push_array(scratch, result->loop_count, sizeof(uint32_t), _Alignof(uint32_t));
        if (by_number == 0 || outer == 0)
        {
            status = DOMINATOR_NO_MEMORY;
        }
        if (status == DOMINATOR_OK)
        {
            memset(result->loop_bodies, 0, body_bytes);
            memset(result->loop_parent, 0xff, (uint64_t)result->loop_count * sizeof(uint32_t));
            // Loop IDs by ascending header block ID; the SCC pass is done with workspace.
            uint32_t* loop_of = workspace;
            uint32_t loop = 0;
            for (uint32_t block = 0; block < count; block += 1)
            {
                loop_of[block] = flags[block] != 0 ? loop : BUSTER_DOMINATOR_NONE;
                if (flags[block] != 0)
                {
                    result->loop_header[loop] = block;
                    loop += 1;
                }
                if (result->rpo_index[block] != BUSTER_DOMINATOR_NONE)
                {
                    by_number[result->preorder[block]] = block;
                }
            }
            dominators_discover(cfg, result, by_number, loop_of, outer, stack);
            // Depths top down: an outer loop's header has the lower preorder number. outer now holds each loop's depth.
            for (uint32_t number = 0; number < result->reachable_count; number += 1)
            {
                const uint32_t header_loop = loop_of[by_number[number]];
                if (header_loop != BUSTER_DOMINATOR_NONE)
                {
                    const uint32_t parent = result->loop_parent[header_loop];
                    outer[header_loop] = parent == BUSTER_DOMINATOR_NONE ? 1 : outer[parent] + 1;
                }
            }
            for (uint32_t block = 0; block < count; block += 1)
            {
                const uint32_t innermost = result->block_loop[block];
                result->loop_depth[block] = innermost == BUSTER_DOMINATOR_NONE ? 0 : outer[innermost];
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
        };
        const uint32_t count = cfg->block_count;
        const uint64_t row_bytes = candidate.row_words * sizeof(uint64_t);
        const uint64_t matrix_bytes = (uint64_t)count * row_bytes;
        const bool option_valid = (options.outputs & ~(BUSTER_DOMINATOR_FRONTIERS | BUSTER_DOMINATOR_LOOPS)) == 0;
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
        uint64_t* back_edge_sources = NULL;
        uint32_t* workspace = 0;
        uint32_t* dfs_vertex = 0;
        uint32_t* dfs_parent = 0;
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
            if ((options.outputs & BUSTER_DOMINATOR_LOOPS) != 0)
            {
                back_edge_sources = arena_push_array(scratch, ((uint64_t)count + 63) / 64, sizeof(uint64_t), _Alignof(uint64_t));
            }
            workspace = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
            dfs_vertex = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
            dfs_parent = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
            if (candidate.idom == 0 || candidate.rpo == 0 || candidate.rpo_index == 0 || candidate.tree_offsets == 0 || candidate.tree_children == 0 || candidate.preorder == 0 || candidate.subtree_end == 0 || frames == 0 || dfs_begin == 0 || ((options.outputs & BUSTER_DOMINATOR_LOOPS) != 0 && back_edge_sources == NULL) || workspace == 0 || dfs_vertex == 0 || dfs_parent == 0)
            {
                status = DOMINATOR_NO_MEMORY;
            }
        }
        if (status == DOMINATOR_OK && count != 0)
        {
            const uint32_t joins = dominators_order(cfg, &candidate, frames, dfs_begin, back_edge_sources, dfs_vertex, dfs_parent, candidate.tree_children);
            const uint64_t phase_start = scratch->length;
            if (joins != 0)
            {
                DominatorLink* links = arena_push_array(scratch, candidate.reachable_count, sizeof(DominatorLink), _Alignof(DominatorLink));
                uint32_t* stack = arena_push_array(scratch, candidate.reachable_count, sizeof(uint32_t), _Alignof(uint32_t));
                if (links == 0 || stack == 0)
                {
                    status = DOMINATOR_NO_MEMORY;
                }
                else
                {
                    dominators_semi_nca(cfg, &candidate, dfs_begin, dfs_vertex, dfs_parent, workspace, links, stack, candidate.tree_children, joins);
                }
            }
            dominators_record_scratch(&candidate, scratch, scratch_start);
            arena_pop_to(scratch, phase_start);
        }
        if (status == DOMINATOR_OK && count != 0)
        {
            dominators_tree(&candidate, frames);
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
                status = dominators_loops(arena, scratch, cfg, options, &candidate, workspace, dfs_begin, back_edge_sources, scratch_start);
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
