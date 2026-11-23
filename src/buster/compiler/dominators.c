// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Entry-rooted dominators, frontiers and natural loops for a borrowed CSR CFG. Include after lane.c.
// dominators_analyze validates both CSR views, computes reachable reverse postorder and DFS preorder, solves by
// SEMI-NCA, then builds the tree and the requested frontier/loop sets. SEMI-NCA and the loop-body walk are scalar:
// on the 9700X they beat the AVX-512 bit-matrix solver and loop-body sweep they replaced at every measured size
// (#1352, #2924; docs/dominators.md, "Why it is scalar").
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
// dominators_components; dominators_loop_body/loops; dominators_analyze; the consumer query helpers.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BUSTER_DOMINATOR_NONE UINT32_MAX
#define BUSTER_DOMINATOR_VECTOR_WORDS 8
#define BUSTER_DOMINATOR_VECTOR_BITS 512
#define BUSTER_DOMINATOR_VECTOR_BYTES 64
#define BUSTER_DOMINATOR_WINDOW_BITS 64
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

// A reachable retreating edge, by reverse-postorder number, kept for the check once the tree has its intervals.
typedef struct DominatorEdge DominatorEdge;
struct DominatorEdge
{
    uint32_t source;
    uint32_t target;
};

// Myers's skew-binary jump pointer of a solved reverse-postorder number, and its depth in the dominator tree.
typedef struct DominatorJump DominatorJump;
struct DominatorJump
{
    uint32_t depth;
    uint32_t jump;
};

// The tree solver's state, all by reverse-postorder number: each solved number's immediate dominator and dominator
// window (bit k: number - k dominates it), the jump pointers filled on first use, their fill stack, and the reachable
// retreating edges.
typedef struct DominatorSweep DominatorSweep;
struct DominatorSweep
{
    uint64_t* windows;
    uint32_t* idom;
    DominatorJump* jumps;
    uint32_t* stack;
    DominatorEdge* retreating;
    uint32_t retreating_count;
    // Jump pointers below this number are filled or marked unfilled; above it they are not yet initialized.
    uint32_t jumps_limit;
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

// One DFS from entry, in successor order: RPO and its inverse. A block is marked discovered in rpo_index until the
// numbering at the end. An edge retreats in RPO exactly when the DFS reached its target first and is still inside it:
// its target is its source's DFS ancestor.
static void dominators_order(const DominatorCfg* cfg, Dominators* result, DominatorFrame* stack)
{
    const uint32_t count = cfg->block_count;
    uint32_t* rpo_index = result->rpo_index;
    memset(rpo_index, 0xff, (uint64_t)count * sizeof(uint32_t));
    uint32_t finished = 0;
    uint64_t depth = 1;
    rpo_index[cfg->entry] = 0;
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
            if (rpo_index[target] == BUSTER_DOMINATOR_NONE)
            {
                rpo_index[target] = 0;
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

// Fill the jump pointers of number and of its unfilled tree ancestors, top down. A number's jump is its idom's jump's
// jump when the idom's two jump spans are equal, else its idom (Myers, "An applicative random-access stack", 1983), so
// a climb by jumps takes O(log depth) steps. Each number is filled once per sweep.
static void dominators_fill_jumps(DominatorSweep* sweep, uint32_t number)
{
    DominatorJump* jumps = sweep->jumps;
    const uint32_t* idom = sweep->idom;
    uint32_t* stack = sweep->stack;
    uint64_t depth = 0;
    uint32_t top = number;
    while (jumps[top].depth == BUSTER_DOMINATOR_NONE)
    {
        stack[depth] = top;
        depth += 1;
        top = idom[top];
    }
    while (depth != 0)
    {
        depth -= 1;
        const uint32_t vertex = stack[depth];
        const uint32_t parent = idom[vertex];
        const DominatorJump up = jumps[parent];
        const DominatorJump over = jumps[up.jump];
        const bool equal = up.depth - over.depth == over.depth - jumps[over.jump].depth;
        jumps[vertex] = (DominatorJump){
            .depth = up.depth + 1,
            .jump = equal ? over.jump : parent,
        };
    }
}

// The nearest common dominator of two solved reverse-postorder numbers. Within 64 numbers of each other, their windows'
// intersection usually holds it; otherwise depth and jump pointers find it in O(log depth), whatever the tree's depth.
// Out of line: the sweep calls it for 2.4% of blocks, and inlined it would crowd the sweep loop's registers.
static __attribute__((noinline)) uint32_t dominators_meet(DominatorSweep* sweep, uint32_t a, uint32_t b)
{
    const uint32_t high = a > b ? a : b;
    const uint32_t low = a > b ? b : a;
    const uint32_t distance = high - low;
    uint64_t common = 0;
    if (distance < BUSTER_DOMINATOR_WINDOW_BITS)
    {
        common = sweep->windows[high] & (sweep->windows[low] << distance);
    }
    uint32_t result = low;
    if (common != 0)
    {
        result = high - (uint32_t)__builtin_ctzll(common);
    }
    else if (low != 0)
    {
        DominatorJump* jumps = sweep->jumps;
        const uint32_t* idom = sweep->idom;
        const uint32_t limit = sweep->jumps_limit;
        if (high >= limit)
        {
            // Mark the numbers solved since the last climb unfilled; entry is the root.
            memset(jumps + limit, 0xff, ((uint64_t)high + 1 - limit) * sizeof(DominatorJump));
            jumps[0] = (DominatorJump){
                .depth = 0,
                .jump = 0,
            };
            sweep->jumps_limit = high + 1;
        }
        dominators_fill_jumps(sweep, high);
        dominators_fill_jumps(sweep, low);
        uint32_t deep = jumps[high].depth >= jumps[low].depth ? high : low;
        uint32_t other = deep == high ? low : high;
        const uint32_t level = jumps[other].depth;
        while (jumps[deep].depth > level)
        {
            const uint32_t jump = jumps[deep].jump;
            deep = jumps[jump].depth >= level ? jump : idom[deep];
        }
        // Equal depths have equal jump spans, so the two climbs stay level.
        while (deep != other)
        {
            const bool apart = jumps[deep].jump != jumps[other].jump;
            deep = apart ? jumps[deep].jump : idom[deep];
            other = apart ? jumps[other].jump : idom[other];
        }
        result = deep;
    }
    return result;
}

// The tree solver: one sweep in reverse postorder, where a block's forward predecessors are solved before it.
// Dominators precede what they dominate in RPO, so a number's dominators within the 64 numbers above it fit a u64
// window, bit k for number - k. Each forward predecessor's window, shifted by the distance, ANDs to the dominators
// common to all of them there; the lowest set bit is the nearest, the immediate dominator, and that set with the
// number's own bit is its window. Where no common dominator is that close (2.4% of sqlite3.c's blocks),
// dominators_meet climbs to it. Retreating predecessors are skipped and kept: the result is the dominator tree of the
// graph without them, which is the whole graph's when each one's target's idom dominates its source
// (dominators_settled).
static __attribute__((noinline)) void dominators_sweep(const DominatorCfg* cfg, const Dominators* result, DominatorSweep* sweep)
{
    const uint32_t reachable = result->reachable_count;
    const uint32_t* rpo = result->rpo;
    const uint32_t* rpo_index = result->rpo_index;
    const uint32_t* offsets = cfg->predecessor_offsets;
    const uint32_t* predecessors = cfg->predecessors;
    uint64_t* windows = sweep->windows;
    uint32_t* idom = sweep->idom;
    DominatorEdge* retreating = sweep->retreating;
    windows[0] = 1;
    idom[0] = 0;
    sweep->jumps_limit = 0;
    // Every reachable edge into entry retreats; the loop pass reads them as entry's backedges.
    for (uint32_t edge = offsets[cfg->entry]; edge < offsets[(uint64_t)cfg->entry + 1]; edge += 1)
    {
        const uint32_t source = rpo_index[predecessors[edge]];
        if (source != BUSTER_DOMINATOR_NONE)
        {
            *retreating = (DominatorEdge){
                .source = source,
                .target = 0,
            };
            retreating += 1;
        }
    }
    for (uint32_t number = 1; number < reachable; number += 1)
    {
        const uint32_t block = rpo[number];
        const uint32_t first = offsets[block];
        const uint32_t end = offsets[(uint64_t)block + 1];
        uint64_t common = UINT64_MAX;
        uint32_t parent = BUSTER_DOMINATOR_NONE;
        // Most blocks have one or two predecessors: unrolling only adds remainder code and register pressure.
#pragma clang loop unroll(disable)
        for (uint32_t edge = first; edge < end; edge += 1)
        {
            const uint32_t source = rpo_index[predecessors[edge]];
            if (source < number)
            {
                const uint32_t distance = number - source;
                common &= distance < BUSTER_DOMINATOR_WINDOW_BITS ? windows[source] << distance : 0;
                parent = source;
            }
            else if (source != BUSTER_DOMINATOR_NONE)
            {
                *retreating = (DominatorEdge){
                    .source = source,
                    .target = number,
                };
                retreating += 1;
            }
        }
        uint64_t window = common | 1;
        if (common != 0)
        {
            parent = number - (uint32_t)__builtin_ctzll(common);
        }
        else
        {
            for (uint32_t edge = first; edge < end; edge += 1)
            {
                const uint32_t source = rpo_index[predecessors[edge]];
                if (source < number)
                {
                    parent = dominators_meet(sweep, parent, source);
                }
            }
            const uint32_t distance = number - parent;
            window = distance < BUSTER_DOMINATOR_WINDOW_BITS ? windows[parent] << distance | 1 : 1;
        }
        windows[number] = window;
        idom[number] = parent;
    }
    sweep->retreating_count = (uint32_t)(retreating - sweep->retreating);
}

// Whether the sweep's tree is the whole graph's: each retreating edge's target's idom dominates its source, by the
// tree's intervals. Then each block's retreating predecessors' dominators hold its forward ones' common dominators,
// so they change no intersection. A reducible graph always passes: there each retreating edge's target dominates
// its source.
static bool dominators_settled(const Dominators* result, const DominatorSweep* sweep)
{
    bool settled = true;
    for (uint32_t edge = 0; settled && edge < sweep->retreating_count; edge += 1)
    {
        const DominatorEdge retreating = sweep->retreating[edge];
        settled = dominators_dominates(result, result->rpo[sweep->idom[retreating.target]], result->rpo[retreating.source]);
    }
    return settled;
}

// An irreducible region the sweep didn't settle: Cooper, Harvey and Kennedy's iteration ("A Simple, Fast Dominance
// Algorithm", 2001) from the sweep's tree, until a pass changes no idom. Each pass is the sweep again, but meets a
// block's forward dominator with each retreating predecessor by walking both up the current tree, as their
// algorithm does. The sweep's tree holds every true dominator, so the passes only remove them. The walks cost the
// depth between a retreating predecessor and the meeting point; sqlite3.c has no graph that takes this path.
static void dominators_settle(const DominatorCfg* cfg, const Dominators* result, DominatorSweep* sweep)
{
    const uint32_t reachable = result->reachable_count;
    const uint32_t* rpo = result->rpo;
    const uint32_t* rpo_index = result->rpo_index;
    const uint32_t* offsets = cfg->predecessor_offsets;
    const uint32_t* predecessors = cfg->predecessors;
    uint64_t* windows = sweep->windows;
    uint32_t* idom = sweep->idom;
    bool changed = true;
    while (changed)
    {
        changed = false;
        // The jump pointers describe the previous pass's tree.
        sweep->jumps_limit = 0;
        for (uint32_t number = 1; number < reachable; number += 1)
        {
            const uint32_t block = rpo[number];
            const uint32_t first = offsets[block];
            const uint32_t end = offsets[(uint64_t)block + 1];
            uint64_t common = UINT64_MAX;
            uint32_t parent = BUSTER_DOMINATOR_NONE;
            for (uint32_t edge = first; edge < end; edge += 1)
            {
                const uint32_t source = rpo_index[predecessors[edge]];
                if (source < number)
                {
                    const uint32_t distance = number - source;
                    common &= distance < BUSTER_DOMINATOR_WINDOW_BITS ? windows[source] << distance : 0;
                    parent = source;
                }
            }
            if (common != 0)
            {
                parent = number - (uint32_t)__builtin_ctzll(common);
            }
            for (uint32_t edge = first; edge < end; edge += 1)
            {
                uint32_t source = rpo_index[predecessors[edge]];
                if (common == 0 && source < number)
                {
                    parent = dominators_meet(sweep, parent, source);
                }
                else if (source != BUSTER_DOMINATOR_NONE && source >= number)
                {
                    while (parent != source)
                    {
                        while (parent > source)
                        {
                            parent = idom[parent];
                        }
                        while (source > parent)
                        {
                            source = idom[source];
                        }
                    }
                }
            }
            const uint32_t distance = number - parent;
            windows[number] = distance < BUSTER_DOMINATOR_WINDOW_BITS ? windows[parent] << distance | 1 : 1;
            changed |= parent != idom[number];
            idom[number] = parent;
        }
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

// The solver's tree, by reverse-postorder number, in block IDs (unreachable blocks have none, entry is its own), its
// children CSR and its intervals.
static void dominators_tree(Dominators* result, const uint32_t* rpo_idom, DominatorFrame* stack, uint32_t* cursors)
{
    const uint32_t count = result->block_count;
    const uint32_t* rpo = result->rpo;
    memset(result->tree_offsets, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    // Empty intervals for unreachable blocks are directly consumable by IrDominance's enter/leave view.
    memset(result->preorder, 0, (uint64_t)count * sizeof(uint32_t));
    memset(result->subtree_end, 0, (uint64_t)count * sizeof(uint32_t));
    memset(result->idom, 0xff, (uint64_t)count * sizeof(uint32_t));
    result->idom[result->entry] = result->entry;
    for (uint32_t order = 1; order < result->reachable_count; order += 1)
    {
        const uint32_t parent = rpo[rpo_idom[order]];
        result->idom[rpo[order]] = parent;
        result->tree_offsets[(uint64_t)parent + 1] += 1;
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
static void dominators_components(const DominatorCfg* cfg, Dominators* result, uint32_t* component, uint32_t* stack, uint8_t* flags, const DominatorEdge* retreating, uint32_t retreating_count)
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
    // The DFS retreating edges are the sweep's: their targets are their sources' DFS ancestors (dominators_order).
    for (uint32_t edge = 0; edge < retreating_count; edge += 1)
    {
        const uint32_t block = result->rpo[retreating[edge].source];
        if (!dominators_dominates(result, result->rpo[retreating[edge].target], block))
        {
            flags[component[block]] = 1;
        }
    }
    for (uint32_t block = 0; block < cfg->block_count; block += 1)
    {
        result->irreducible[block] = component[block] == BUSTER_DOMINATOR_NONE ? 0 : flags[component[block]];
    }
}

// The natural loop of header: header, and every reachable block that reaches one of its latches without passing it.
static void dominators_loop_body(const DominatorCfg* cfg, const Dominators* result, uint32_t header, uint64_t* body, uint32_t* stack)
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

static DominatorStatus dominators_loops(Arena* arena, Arena* scratch, const DominatorCfg* cfg, DominatorOptions options, Dominators* result, uint32_t* workspace, const DominatorEdge* retreating, uint32_t retreating_count, uint64_t scratch_start)
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
        dominators_components(cfg, result, workspace, stack, flags, retreating, retreating_count);
        memset(flags, 0, count);
        // A backedge's target dominates its source, so it is a DFS ancestor: the edge retreats.
        for (uint32_t edge = 0; edge < retreating_count; edge += 1)
        {
            const uint32_t target = result->rpo[retreating[edge].target];
            flags[target] |= dominators_dominates(result, target, result->rpo[retreating[edge].source]);
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
                    dominators_loop_body(cfg, result, header, body, stack);
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
        uint32_t* workspace = 0;
        DominatorEdge* retreating = 0;
        uint32_t retreating_count = 0;
        if (status == DOMINATOR_OK && count != 0)
        {
            candidate.idom = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.rpo = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.rpo_index = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.tree_offsets = arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.tree_children = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.preorder = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.subtree_end = arena_push_array(arena, count, sizeof(uint32_t), _Alignof(uint32_t));
            // One push for the analysis-long scratch: the DFS and tree stack, a block column the tree and the SCC pass
            // reuse, and the sweep's retreating edges, which the loop pass reads again. Tiny CFGs pay for each push.
            const uint64_t frame_bytes = (uint64_t)count * sizeof(DominatorFrame);
            const uint64_t workspace_bytes = (uint64_t)count * sizeof(uint32_t);
            uint8_t* columns = arena_push(scratch, frame_bytes + workspace_bytes + (uint64_t)cfg->edge_count * sizeof(DominatorEdge), _Alignof(DominatorFrame));
            if (candidate.idom == 0 || candidate.rpo == 0 || candidate.rpo_index == 0 || candidate.tree_offsets == 0 || candidate.tree_children == 0 || candidate.preorder == 0 || candidate.subtree_end == 0 || columns == 0)
            {
                status = DOMINATOR_NO_MEMORY;
            }
            else
            {
                frames = (DominatorFrame*)columns;
                workspace = (uint32_t*)(columns + frame_bytes);
                retreating = (DominatorEdge*)(columns + frame_bytes + workspace_bytes);
            }
        }
        if (status == DOMINATOR_OK && count != 0)
        {
            dominators_order(cfg, &candidate, frames);
            const uint64_t phase_start = scratch->length;
            const uint32_t reachable = candidate.reachable_count;
            // One push for the solver's columns: windows and jumps (8-byte elements), then idom and the jump stack.
            uint8_t* columns = arena_push(scratch, (uint64_t)reachable * (sizeof(uint64_t) + sizeof(DominatorJump) + 2 * sizeof(uint32_t)), _Alignof(uint64_t));
            if (columns == 0)
            {
                status = DOMINATOR_NO_MEMORY;
            }
            else
            {
                DominatorSweep sweep = {
                    .windows = (uint64_t*)columns,
                    .jumps = (DominatorJump*)(columns + (uint64_t)reachable * sizeof(uint64_t)),
                    .idom = (uint32_t*)(columns + (uint64_t)reachable * (sizeof(uint64_t) + sizeof(DominatorJump))),
                    .stack = (uint32_t*)(columns + (uint64_t)reachable * (sizeof(uint64_t) + sizeof(DominatorJump) + sizeof(uint32_t))),
                    .retreating = retreating,
                };
                dominators_sweep(cfg, &candidate, &sweep);
                retreating_count = sweep.retreating_count;
                dominators_tree(&candidate, sweep.idom, frames, workspace);
                if (!dominators_settled(&candidate, &sweep))
                {
                    dominators_settle(cfg, &candidate, &sweep);
                    dominators_tree(&candidate, sweep.idom, frames, workspace);
                }
            }
            dominators_record_scratch(&candidate, scratch, scratch_start);
            arena_pop_to(scratch, phase_start);
        }
        if (status == DOMINATOR_OK && count != 0)
        {
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
                status = dominators_loops(arena, scratch, cfg, options, &candidate, workspace, retreating, retreating_count, scratch_start);
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
