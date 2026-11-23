// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Backward liveness over a CFG's successor columns and per-block bitsets. The caller owns the CFG and fills use/def;
// dataflow_postorder validates its edges and builds the postorder of a DFS forest with an explicit stack, including
// unreachable blocks. The liveness solver starts with empty in rows and visits that postorder until in stops changing,
// storing every block's out row in each sweep. Its iteration count includes the final stable sweep. A postorder reads
// each back edge's target from the previous sweep, so a value crossing k back edges takes k more sweeps: a loop nest
// of N levels took N + 2 (#2995). So when BUSTER_DATAFLOW_POSTORDER_SWEEPS sweeps leave in still changing, the solver
// builds the backward order (dataflow_backward), in which back edges read the current sweep, and from there alternates
// it with the postorder, the backward order first: a loop nest then settles in a few more sweeps. Functions that settle
// sooner, nearly all of them, never build it. It is AVX-512 alone (#2924): the tests check it against the liveness
// equations, use-before-def reachability and the sweep search of dataflow_sweeps_test.c, which gives every live bit's
// sweep and so the exact count.
//
// Each matrix is block_count contiguous rows of row_words uint64_t words, rounded to a whole 512-bit vector.
// Every row starts at a 64-byte boundary, and the caller keeps the use/def padding bits zero. The matrices are
// disjoint, owned by the arena supplied to dataflow_create. Solvers require a successfully prepared postorder and
// do not allocate or validate the CFG again. Zero values means zero words: the solver accesses no matrix pointer.
// dataflow_intersects takes two rows with the same alignment, length and padding contract; zero words permits
// null pointers. This primitive only reports whether the rows share a bit, without building an interference graph.
//
// main.c and dataflow_benchmark.c include this file after lane.c.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_DATAFLOW_VECTOR_BITS 512
#define BUSTER_DATAFLOW_VECTOR_WORDS 8
#define BUSTER_DATAFLOW_VECTOR_BYTES 64

// Aligned to a 64-byte line, so that the counts and the bases of the columns, use and def share its first line, and in
// and out its second, wherever a caller keeps it. Where def and in shared a line whose low 12 address bits an in or out
// row shares, a solve after one that stored that row took longer, with more loads held by older stores: up to 2.5
// cycles more on the benchmark's small diamond (docs/dataflow.md, "Where the caller's Dataflow falls").
// The postorder sweeps before the solver turns to the alternating orders. Ordinary functions, whose values cross a loop
// level or two, settle within them (docs/dataflow.md, "The two orders").
#define BUSTER_DATAFLOW_POSTORDER_SWEEPS 4

typedef struct DataflowVisit DataflowVisit;
struct DataflowVisit
{
    uint32_t block;
    uint32_t next_edge;
};

typedef struct Dataflow Dataflow;
struct __attribute__((aligned(64))) Dataflow
{
    uint32_t block_count;
    uint32_t value_count;
    uint32_t edge_count;
    uint64_t row_words;
    // The successors of block b are successors[successor_offsets[b] .. successor_offsets[b + 1]).
    uint32_t* successor_offsets;
    uint32_t* successors;
    uint32_t* postorder;
    uint64_t* use;
    uint64_t* def;
    uint64_t* in;
    uint64_t* out;
    // The reverse postorder of a DFS forest over the predecessors, rooted first at the blocks without successors, which
    // dataflow_backward builds when a solve needs it, with the three columns after it. After out, so that the first line
    // keeps the columns that the comment above describes.
    uint32_t* backward;
    uint32_t* predecessor_offsets;
    uint32_t* predecessors;
    DataflowVisit* backward_stack;
    uint8_t* backward_visited;
};

// Failure preserves both the arena's length and *result. Empty CFGs are allowed only without edges.
static __attribute__((unused)) bool dataflow_create(Arena* arena, uint32_t block_count, uint32_t value_count, uint32_t edge_count, Dataflow* result);
// Failure leaves postorder unchanged. Scratch memory is returned to its original length on either outcome.
static __attribute__((unused)) bool dataflow_postorder(Dataflow* dataflow, Arena* scratch);
// An upper bound on the scratch bytes dataflow_postorder takes for a CFG of this size, alignment gaps included.
static __attribute__((unused)) uint64_t dataflow_postorder_scratch_bytes(uint32_t block_count, uint32_t edge_count);
// Builds backward from a complete postorder, in the Dataflow's own columns. The solver calls it when it needs it; the
// tests call it to read the order.
static __attribute__((unused)) void dataflow_backward(Dataflow* dataflow);
// Sweeps from visiting, swapping it with following after each, until a sweep changes nothing (*settled) or limit sweeps
// have run; in is cleared first unless resuming. Returns the sweeps run.
static __attribute__((unused, noinline, aligned(64))) uint64_t dataflow_liveness_vector_one(Dataflow* dataflow, const uint32_t* visiting, const uint32_t* following,
                                                                                         uint64_t limit, bool resume, bool* settled);
static __attribute__((unused, noinline, aligned(64))) uint64_t dataflow_liveness_vector_rows(Dataflow* dataflow, const uint32_t* visiting, const uint32_t* following,
                                                                                          uint64_t limit, bool resume, bool* settled);
static __attribute__((unused)) uint64_t dataflow_liveness(Dataflow* dataflow);
static __attribute__((unused)) bool dataflow_intersects(const uint64_t* left, const uint64_t* right, uint64_t row_words);

static bool dataflow_create(Arena* arena, uint32_t block_count, uint32_t value_count, uint32_t edge_count, Dataflow* result)
{
    const uint64_t saved_length = arena->length;
    Dataflow candidate = {
        .block_count = block_count,
        .value_count = value_count,
        .edge_count = edge_count,
        .row_words = ((uint64_t)value_count + BUSTER_DATAFLOW_VECTOR_BITS - 1) / BUSTER_DATAFLOW_VECTOR_BITS * BUSTER_DATAFLOW_VECTOR_WORDS,
    };
    // Both dimensions are u32; even their largest padded matrix fits uint64_t bytes.
    const uint64_t matrix_words = (uint64_t)block_count * candidate.row_words;
    const uint64_t matrix_bytes = matrix_words * sizeof(uint64_t);
    bool success = block_count != 0 || edge_count == 0;
    if (success)
    {
        candidate.successor_offsets = arena_push_array(arena, (uint64_t)block_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
        success = candidate.successor_offsets != 0;
    }
    if (success && block_count != 0)
    {
        candidate.postorder = arena_push_array(arena, block_count, sizeof(uint32_t), _Alignof(uint32_t));
        success = candidate.postorder != 0;
    }
    if (success && edge_count != 0)
    {
        candidate.successors = arena_push_array(arena, edge_count, sizeof(uint32_t), _Alignof(uint32_t));
        success = candidate.successors != 0;
    }
    if (success && matrix_words != 0)
    {
        // in and out, which every sweep stores, right after the columns, and use and def, which it only loads, after
        // them. While the span from the first column to the end of out fits in 4 KB, padding included, no column shares
        // its low 12 address bits with an in or out row, whose stores would hold up the loads of the columns that every
        // block makes (docs/dataflow.md, "Where the AVX-512 solver's frame falls").
        candidate.in = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        candidate.out = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        candidate.use = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        candidate.def = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        success = candidate.use != 0 && candidate.def != 0 && candidate.in != 0 && candidate.out != 0;
    }
    if (success && block_count != 0)
    {
        // The backward order's columns, after everything a sweep touches: only a solve that needs the order writes them.
        candidate.backward = arena_push_array(arena, block_count, sizeof(uint32_t), _Alignof(uint32_t));
        candidate.predecessor_offsets = arena_push_array(arena, (uint64_t)block_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
        candidate.backward_stack = arena_push_array(arena, block_count, sizeof(DataflowVisit), _Alignof(DataflowVisit));
        candidate.backward_visited = arena_push_array(arena, block_count, sizeof(uint8_t), _Alignof(uint8_t));
        success = candidate.backward != 0 && candidate.predecessor_offsets != 0 && candidate.backward_stack != 0 && candidate.backward_visited != 0;
    }
    if (success && edge_count != 0)
    {
        candidate.predecessors = arena_push_array(arena, edge_count, sizeof(uint32_t), _Alignof(uint32_t));
        success = candidate.predecessors != 0;
    }
    if (success)
    {
        memset(candidate.successor_offsets, 0, ((uint64_t)block_count + 1) * sizeof(uint32_t));
        if (block_count != 0)
        {
            memset(candidate.postorder, 0, (uint64_t)block_count * sizeof(uint32_t));
        }
        if (edge_count != 0)
        {
            memset(candidate.successors, 0, (uint64_t)edge_count * sizeof(uint32_t));
        }
        if (matrix_words != 0)
        {
            memset(candidate.use, 0, matrix_bytes);
            memset(candidate.def, 0, matrix_bytes);
            memset(candidate.in, 0, matrix_bytes);
            memset(candidate.out, 0, matrix_bytes);
        }
        *result = candidate;
    }
    else
    {
        arena_pop_to(arena, saved_length);
    }
    return success;
}

// The backward order: a DFS over the predecessors, rooted first at every block without successors in block order,
// then at the rest in postorder, and its finishing order reversed. Each block's predecessors are taken latest in the
// postorder first. Every edge then leads to an earlier block except where the DFS met an ancestor, which in a reducible
// CFG with its exits at the loops' headers is a header's edge into its own loop: a back edge reads its header's row
// from the same sweep, where the postorder reads the last sweep's. Of the predecessor orders tried on sqlite3.c's and
// Lua's Tier 1 functions, latest first took the fewest sweeps (docs/dataflow.md, "The two orders"). The postorder must
// be complete.
static void dataflow_backward(Dataflow* dataflow)
{
    const uint32_t block_count = dataflow->block_count;
    const uint32_t edge_count = dataflow->edge_count;
    uint32_t* const backward = dataflow->backward;
    uint32_t* const predecessor_offsets = dataflow->predecessor_offsets;
    uint32_t* const predecessors = dataflow->predecessors;
    DataflowVisit* const stack = dataflow->backward_stack;
    uint8_t* const visited = dataflow->backward_visited;
    // Predecessor columns by a counting sort of the edges, filled from the postorder's end: each block's predecessors
    // latest in the postorder first.
    memset(predecessor_offsets, 0, ((uint64_t)block_count + 1) * sizeof(uint32_t));
    for (uint32_t edge = 0; edge < edge_count; edge += 1)
    {
        predecessor_offsets[(uint64_t)dataflow->successors[edge] + 1] += 1;
    }
    for (uint32_t block = 0; block < block_count; block += 1)
    {
        predecessor_offsets[(uint64_t)block + 1] += predecessor_offsets[block];
        // backward holds each block's next free predecessor slot until the DFS below writes the order over it.
        backward[block] = predecessor_offsets[block];
    }
    for (uint32_t position = block_count; position != 0; position -= 1)
    {
        const uint32_t block = dataflow->postorder[position - 1];
        for (uint32_t edge = dataflow->successor_offsets[block]; edge < dataflow->successor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t successor = dataflow->successors[edge];
            predecessors[backward[successor]] = block;
            backward[successor] += 1;
        }
    }
    memset(visited, 0, block_count);
    // The finishing order, reversed as it is written: the first block to finish is backward's last.
    uint32_t finished_count = 0;
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        for (uint32_t index = 0; index < block_count; index += 1)
        {
            // The first pass roots the exits; the second, whatever reaches none (an infinite loop), deepest first.
            const uint32_t root = pass == 0 ? index : dataflow->postorder[index];
            const bool exit = dataflow->successor_offsets[root] == dataflow->successor_offsets[(uint64_t)root + 1];
            if (visited[root] == 0 && (pass != 0 || exit))
            {
                uint64_t depth = 1;
                visited[root] = 1;
                stack[0] = (DataflowVisit){
                    .block = root,
                    .next_edge = predecessor_offsets[root],
                };
                while (depth != 0)
                {
                    DataflowVisit* visit = &stack[depth - 1];
                    if (visit->next_edge < predecessor_offsets[(uint64_t)visit->block + 1])
                    {
                        const uint32_t predecessor = predecessors[visit->next_edge];
                        visit->next_edge += 1;
                        if (visited[predecessor] == 0)
                        {
                            visited[predecessor] = 1;
                            stack[depth] = (DataflowVisit){
                                .block = predecessor,
                                .next_edge = predecessor_offsets[predecessor],
                            };
                            depth += 1;
                        }
                    }
                    else
                    {
                        backward[block_count - 1 - finished_count] = visit->block;
                        finished_count += 1;
                        depth -= 1;
                    }
                }
            }
        }
    }
}

// visited and the visit stack per block, and at most one alignment gap, before the stack.
static uint64_t dataflow_postorder_scratch_bytes(uint32_t block_count, uint32_t edge_count)
{
    (void)edge_count;
    return (uint64_t)block_count * (sizeof(uint8_t) + sizeof(DataflowVisit)) + _Alignof(DataflowVisit);
}

static bool dataflow_postorder(Dataflow* dataflow, Arena* scratch)
{
    const uint64_t saved_length = scratch->length;
    const uint32_t block_count = dataflow->block_count;
    bool success = dataflow->successor_offsets != 0 && (block_count == 0 || dataflow->postorder != 0) &&
                   (dataflow->edge_count == 0 || dataflow->successors != 0);
    if (success)
    {
        success = dataflow->successor_offsets[0] == 0 && dataflow->successor_offsets[block_count] == dataflow->edge_count;
    }
    for (uint32_t block = 0; success && block < block_count; block += 1)
    {
        const uint32_t begin = dataflow->successor_offsets[block];
        const uint32_t end = dataflow->successor_offsets[(uint64_t)block + 1];
        success = begin <= end && end <= dataflow->edge_count;
    }
    for (uint32_t edge = 0; success && edge < dataflow->edge_count; edge += 1)
    {
        success = dataflow->successors[edge] < block_count;
    }
    uint8_t* visited = 0;
    DataflowVisit* stack = 0;
    if (success && block_count != 0)
    {
        visited = arena_push_array(scratch, block_count, sizeof(uint8_t), _Alignof(uint8_t));
        stack = arena_push_array(scratch, block_count, sizeof(DataflowVisit), _Alignof(DataflowVisit));
        success = visited != 0 && stack != 0;
    }
    if (success && block_count != 0)
    {
        memset(visited, 0, block_count);
        uint32_t order_count = 0;
        for (uint32_t root = 0; root < block_count; root += 1)
        {
            if (visited[root] == 0)
            {
                uint64_t depth = 1;
                visited[root] = 1;
                stack[0] = (DataflowVisit){
                    .block = root,
                    .next_edge = dataflow->successor_offsets[root],
                };
                while (depth != 0)
                {
                    DataflowVisit* visit = &stack[depth - 1];
                    const uint32_t end = dataflow->successor_offsets[(uint64_t)visit->block + 1];
                    if (visit->next_edge < end)
                    {
                        const uint32_t successor = dataflow->successors[visit->next_edge];
                        visit->next_edge += 1;
                        if (visited[successor] == 0)
                        {
                            // Mark before pushing, so cycles and repeated edges cannot exceed block_count frames.
                            visited[successor] = 1;
                            stack[depth] = (DataflowVisit){
                                .block = successor,
                                .next_edge = dataflow->successor_offsets[successor],
                            };
                            depth += 1;
                        }
                    }
                    else
                    {
                        dataflow->postorder[order_count] = visit->block;
                        order_count += 1;
                        depth -= 1;
                    }
                }
            }
        }
    }
    arena_pop_to(scratch, saved_length);
    return success;
}

// Rows of one vector (up to 512 values). Without the word loop, this loop keeps every pointer and counter in registers,
// where the general loop spills some to the stack and reloads them in every block: where those slots share their low 12
// bits with an in or out row that the sweep stores (about half of the places the frame can take in a page), the loads
// wait on those stores, and some processes fall into states 10% to 150% slower. Neither of its sweep's loops is
// unrolled: unrolled, either takes registers, and Clang moves some of the loop's state back to the stack
// (docs/dataflow.md, "Where the AVX-512 solver's frame falls").
static __attribute__((noinline, aligned(64), no_builtin("memset"))) uint64_t dataflow_liveness_vector_one(Dataflow* dataflow, const uint32_t* visiting, const uint32_t* following,
                                                                                                         uint64_t limit, bool resume, bool* settled)
{
    uint64_t sweeps = 0;
    const uint32_t block_count = dataflow->block_count;
    const uint32_t* restrict successor_offsets = dataflow->successor_offsets;
    const uint32_t* restrict successors = dataflow->successors;
    const uint64_t* restrict use_rows = dataflow->use;
    const uint64_t* restrict def_rows = dataflow->def;
    uint64_t* restrict in_rows = dataflow->in;
    uint64_t* restrict out_rows = dataflow->out;
    // Only in is cleared: every sweep stores every block's out row (either order holds every block) and nothing reads
    // out, so clearing it too was only stores, up to 18% of a solve (docs/dataflow.md, "Clearing and storing out").
    // The clearing is this function's own stores, not a call of memset (no_builtin keeps Clang from making it one):
    // with the call, and the spills and reloads around it, between the loads of *dataflow and the first sweep, a
    // solve took about 2% longer wherever the caller's Dataflow put its def and in bases on a 64-byte line sharing
    // its low 12 address bits with an in or out row. Unrolled 8 times, since a row at a time took up to 2.4% more
    // cycles than memset at 256 blocks (docs/dataflow.md, "Where the caller's Dataflow falls").
    if (!resume)
    {
        #pragma clang loop unroll_count(8)
        for (uint32_t block = 0; block < block_count; block += 1)
        {
            _mm512_store_si512(in_rows + (uint64_t)block * BUSTER_DATAFLOW_VECTOR_WORDS, _mm512_setzero_si512());
        }
    }
    bool changed;
    do
    {
        // Every block's difference, ORed in a register: a mask moved to a general register each block took one more
        // general register, and with the previous block's number the loop then spilled the successors' base.
        __m512i changes = _mm512_setzero_si512();
        // The block the sweep visited last, whose in row it has just stored. A successor that is that block takes the
        // row from this register, by a branch: loaded, the row comes back through the 64-byte store's forwarding,
        // whose latency every block of a chain then waits on in turn. On a chain of 48 blocks with 24 values, the sweep
        // took 13 cycles a block that way, 1.8 times the scalar twin's time, and 0.4 of that with the branch; a masked
        // blend of the load and the register waited on the load all the same (#2950, docs/dataflow.md, "The previous
        // block's row").
        uint32_t previous_block = UINT32_MAX;
        __m512i previous_in = _mm512_setzero_si512();
        #pragma clang loop unroll(disable)
        for (uint32_t order = 0; order < block_count; order += 1)
        {
            const uint32_t block = visiting[order];
            const uint64_t row = (uint64_t)block * BUSTER_DATAFLOW_VECTOR_WORDS;
            const uint32_t* edge = successors + successor_offsets[block];
            const uint32_t* const end = successors + successor_offsets[(uint64_t)block + 1];
            __m512i out = _mm512_setzero_si512();
            #pragma clang loop unroll(disable)
            for (; edge < end; edge += 1)
            {
                const uint32_t successor = *edge;
                __m512i successor_in;
                if (__builtin_expect(successor == previous_block, 1))
                {
                    successor_in = previous_in;
                    // Keeps Clang from making the two arms a load and a blend, which would wait on the load again.
                    __asm__ volatile("" : "+v"(successor_in));
                }
                else
                {
                    successor_in = _mm512_load_si512(in_rows + (uint64_t)successor * BUSTER_DATAFLOW_VECTOR_WORDS);
                }
                out = _mm512_or_si512(out, successor_in);
            }
            const __m512i use = _mm512_load_si512(use_rows + row);
            const __m512i def = _mm512_load_si512(def_rows + row);
            const __m512i previous = _mm512_load_si512(in_rows + row);
            const __m512i in = _mm512_ternarylogic_epi64(use, out, def, 0xF4);
            const __m512i difference = _mm512_xor_si512(in, previous);
            changes = _mm512_or_si512(changes, difference);
            _mm512_store_si512(out_rows + row, out);
            _mm512_store_si512(in_rows + row, in);
            previous_block = block;
            previous_in = in;
        }
        changed = _mm512_test_epi64_mask(changes, changes) != 0;
        sweeps += 1;
        const uint32_t* const swapped = visiting;
        visiting = following;
        following = swapped;
    } while (changed && sweeps != limit);
    *settled = !changed;
    return sweeps;
}

// Rows of any width, a vector of eight words at a time.
static __attribute__((noinline, aligned(64))) uint64_t dataflow_liveness_vector_rows(Dataflow* dataflow, const uint32_t* visiting, const uint32_t* following,
                                                                                    uint64_t limit, bool resume, bool* settled)
{
    uint64_t sweeps = 0;
    bool changed = false;
    const uint32_t block_count = dataflow->block_count;
    const uint64_t row_words = dataflow->row_words;
    const uint32_t* restrict successor_offsets = dataflow->successor_offsets;
    const uint32_t* restrict successors = dataflow->successors;
    const uint64_t* restrict use_rows = dataflow->use;
    const uint64_t* restrict def_rows = dataflow->def;
    uint64_t* restrict in_rows = dataflow->in;
    uint64_t* restrict out_rows = dataflow->out;
    // dataflow_create allocates these columns separately. Only their bases are restricted: a successor may be
    // the current block, so derived pointers into in_rows must retain their possible overlap.
    if (row_words != 0 && block_count != 0)
    {
        // Only in is cleared, as in the loop for one-vector rows: every sweep stores every word of every block's out
        // row, padding included, and nothing reads out (docs/dataflow.md, "Clearing and storing out").
        const uint64_t matrix_bytes = (uint64_t)block_count * row_words * sizeof(uint64_t);
        if (!resume)
        {
            memset(in_rows, 0, matrix_bytes);
        }
        do
        {
            __mmask8 changes = 0;
            for (uint32_t order = 0; order < block_count; order += 1)
            {
                const uint32_t block = visiting[order];
                const uint64_t row = (uint64_t)block * row_words;
                const uint32_t begin = successor_offsets[block];
                const uint32_t end = successor_offsets[(uint64_t)block + 1];
                for (uint64_t word = 0; word < row_words; word += BUSTER_DATAFLOW_VECTOR_WORDS)
                {
                    __m512i out = _mm512_setzero_si512();
                    for (uint32_t edge = begin; edge < end; edge += 1)
                    {
                        const uint64_t* successor_in = in_rows + (uint64_t)successors[edge] * row_words;
                        out = _mm512_or_si512(out, _mm512_load_si512(successor_in + word));
                    }
                    const __m512i use = _mm512_load_si512(use_rows + row + word);
                    const __m512i def = _mm512_load_si512(def_rows + row + word);
                    const __m512i previous = _mm512_load_si512(in_rows + row + word);
                    const __m512i in = _mm512_ternarylogic_epi64(use, out, def, 0xF4);
                    const __m512i difference = _mm512_xor_si512(in, previous);
                    changes |= _mm512_test_epi64_mask(difference, difference);
                    _mm512_store_si512(out_rows + row + word, out);
                    _mm512_store_si512(in_rows + row + word, in);
                }
            }
            changed = changes != 0;
            sweeps += 1;
            const uint32_t* const swapped = visiting;
            visiting = following;
            following = swapped;
        } while (changed && sweeps != limit);
    }
    *settled = !changed;
    return sweeps;
}

// The two loops are separate functions, so that neither changes how Clang compiles the other: inlined together, the
// general loop's registers and stack slots changed, and medium-nested took 3.6% more cycles after scalar code. Each
// starts a 64-byte line, so that a build's code before them doesn't decide where their loops fall in lines.
static uint64_t dataflow_liveness(Dataflow* dataflow)
{
    // With no row to solve the count is the one sweep that changes nothing, here, without calling either loop: the call
    // was most of a solve of a function with no value live across blocks (#2950).
    uint64_t iterations = 1;
    if (dataflow->row_words != 0 && dataflow->block_count != 0)
    {
        const bool one = dataflow->row_words == BUSTER_DATAFLOW_VECTOR_WORDS;
        const uint32_t* const postorder = dataflow->postorder;
        bool settled = false;
        // The postorder alone first. A solve still changing after BUSTER_DATAFLOW_POSTORDER_SWEEPS builds the backward
        // order and goes on from the rows it has, alternating the two orders, the backward order first, to the same
        // least fixpoint.
        iterations = one ? dataflow_liveness_vector_one(dataflow, postorder, postorder, BUSTER_DATAFLOW_POSTORDER_SWEEPS, false, &settled) :
                           dataflow_liveness_vector_rows(dataflow, postorder, postorder, BUSTER_DATAFLOW_POSTORDER_SWEEPS, false, &settled);
        if (!settled)
        {
            dataflow_backward(dataflow);
            const uint32_t* const backward = dataflow->backward;
            iterations += one ? dataflow_liveness_vector_one(dataflow, backward, postorder, UINT64_MAX, true, &settled) :
                                dataflow_liveness_vector_rows(dataflow, backward, postorder, UINT64_MAX, true, &settled);
        }
    }
    return iterations;
}

static bool dataflow_intersects(const uint64_t* left, const uint64_t* right, uint64_t row_words)
{
    // Nonempty rows contain whole aligned vectors; a zero-length row may use any pointer without accessing it.
    assert(row_words % BUSTER_DATAFLOW_VECTOR_WORDS == 0);
    assert(row_words == 0 || (((uintptr_t)left | (uintptr_t)right) & (BUSTER_DATAFLOW_VECTOR_BYTES - 1)) == 0);
    __mmask8 intersections = 0;
    for (uint64_t word = 0; word < row_words; word += BUSTER_DATAFLOW_VECTOR_WORDS)
    {
        const __m512i left_values = _mm512_load_si512(left + word);
        const __m512i right_values = _mm512_load_si512(right + word);
        intersections |= _mm512_test_epi64_mask(left_values, right_values);
    }
    return intersections != 0;
}
