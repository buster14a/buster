// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Backward liveness over a CFG's successor columns and per-block bitsets. The caller owns the CFG and fills use/def;
// dataflow_postorder validates its edges and builds a DFS forest with an explicit stack, including unreachable
// blocks. Both liveness solvers start with empty in/out, then visit that same postorder until in stops changing.
// Their iteration counts include the final stable sweep. The scalar solver is the word-at-a-time test oracle.
//
// Each matrix is block_count contiguous rows of row_words uint64_t words, rounded to a whole 512-bit vector.
// Every row starts at a 64-byte boundary, and the caller keeps the use/def padding bits zero. The matrices are
// disjoint, owned by the arena supplied to dataflow_create. Solvers require a successfully prepared postorder and
// do not allocate or validate the CFG again. Zero values means zero words: neither solver accesses matrix pointers.
// dataflow_intersects_* takes two rows with the same alignment, length and padding contract; zero words permits
// null pointers. This primitive only reports whether the rows share a bit, without building an interference graph.
//
// main.c and dataflow_benchmark.c include this file after lane.c.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_DATAFLOW_VECTOR_BITS 512
#define BUSTER_DATAFLOW_VECTOR_WORDS 8
#define BUSTER_DATAFLOW_VECTOR_BYTES 64

typedef struct Dataflow Dataflow;
struct Dataflow
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
};

// Failure preserves both the arena's length and *result. Empty CFGs are allowed only without edges.
static __attribute__((unused)) bool dataflow_create(Arena* arena, uint32_t block_count, uint32_t value_count, uint32_t edge_count, Dataflow* result);
// Failure leaves postorder unchanged. Scratch memory is returned to its original length on either outcome.
static __attribute__((unused)) bool dataflow_postorder(Dataflow* dataflow, Arena* scratch);
static __attribute__((unused, noinline, aligned(64))) uint64_t dataflow_liveness_vector_one(Dataflow* dataflow);
static __attribute__((unused, noinline, aligned(64))) uint64_t dataflow_liveness_vector_rows(Dataflow* dataflow);
static __attribute__((unused)) uint64_t dataflow_liveness_vector(Dataflow* dataflow);
static __attribute__((unused)) uint64_t dataflow_liveness_scalar(Dataflow* dataflow);
static __attribute__((unused)) uint64_t dataflow_liveness(Dataflow* dataflow);
static __attribute__((unused)) bool dataflow_intersects_vector(const uint64_t* left, const uint64_t* right, uint64_t row_words);
static __attribute__((unused)) bool dataflow_intersects_scalar(const uint64_t* left, const uint64_t* right, uint64_t row_words);

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
        // them. While the columns, in and out fit in 4 KB, no column then shares its low 12 address bits with an in or
        // out row, whose stores would hold up the loads of the columns that every block makes (docs/dataflow.md, "Where
        // the AVX-512 solver's frame falls").
        candidate.in = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        candidate.out = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        candidate.use = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        candidate.def = arena_push(arena, matrix_bytes, BUSTER_DATAFLOW_VECTOR_BYTES);
        success = candidate.use != 0 && candidate.def != 0 && candidate.in != 0 && candidate.out != 0;
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

typedef struct DataflowVisit DataflowVisit;
struct DataflowVisit
{
    uint32_t block;
    uint32_t next_edge;
};

static bool dataflow_postorder(Dataflow* dataflow, Arena* scratch)
{
    const uint64_t saved_length = scratch->length;
    const uint32_t block_count = dataflow->block_count;
    bool success = dataflow->successor_offsets != 0 && (block_count == 0 || dataflow->postorder != 0) && (dataflow->edge_count == 0 || dataflow->successors != 0);
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
// wait on those stores, and some processes fall into states 10% to 150% slower. Neither of its loops is unrolled:
// unrolled, either takes registers, and Clang moves some of the loop's state back to the stack (docs/dataflow.md,
// "Where the AVX-512 solver's frame falls").
static __attribute__((noinline, aligned(64))) uint64_t dataflow_liveness_vector_one(Dataflow* dataflow)
{
    uint64_t iterations = 1;
    const uint32_t block_count = dataflow->block_count;
    const uint32_t* restrict successor_offsets = dataflow->successor_offsets;
    const uint32_t* restrict successors = dataflow->successors;
    const uint32_t* restrict postorder = dataflow->postorder;
    const uint64_t* restrict use_rows = dataflow->use;
    const uint64_t* restrict def_rows = dataflow->def;
    uint64_t* restrict in_rows = dataflow->in;
    uint64_t* restrict out_rows = dataflow->out;
    const uint64_t matrix_bytes = (uint64_t)block_count * BUSTER_DATAFLOW_VECTOR_BYTES;
    memset(in_rows, 0, matrix_bytes);
    memset(out_rows, 0, matrix_bytes);
    bool changed;
    do
    {
        __mmask8 changes = 0;
        #pragma clang loop unroll(disable)
        for (uint32_t order = 0; order < block_count; order += 1)
        {
            const uint32_t block = postorder[order];
            const uint64_t row = (uint64_t)block * BUSTER_DATAFLOW_VECTOR_WORDS;
            const uint32_t begin = successor_offsets[block];
            const uint32_t end = successor_offsets[(uint64_t)block + 1];
            __m512i out = _mm512_setzero_si512();
            #pragma clang loop unroll(disable)
            for (uint32_t edge = begin; edge < end; edge += 1)
            {
                out = _mm512_or_si512(out, _mm512_load_si512(in_rows + (uint64_t)successors[edge] * BUSTER_DATAFLOW_VECTOR_WORDS));
            }
            const __m512i use = _mm512_load_si512(use_rows + row);
            const __m512i def = _mm512_load_si512(def_rows + row);
            const __m512i previous = _mm512_load_si512(in_rows + row);
            const __m512i in = _mm512_ternarylogic_epi64(use, out, def, 0xF4);
            const __m512i difference = _mm512_xor_si512(in, previous);
            changes |= _mm512_test_epi64_mask(difference, difference);
            _mm512_store_si512(out_rows + row, out);
            _mm512_store_si512(in_rows + row, in);
        }
        changed = changes != 0;
        iterations += changed;
    } while (changed);
    return iterations;
}

// Rows of any width, a vector of eight words at a time.
static __attribute__((noinline, aligned(64))) uint64_t dataflow_liveness_vector_rows(Dataflow* dataflow)
{
    uint64_t iterations = 1;
    const uint32_t block_count = dataflow->block_count;
    const uint64_t row_words = dataflow->row_words;
    const uint32_t* restrict successor_offsets = dataflow->successor_offsets;
    const uint32_t* restrict successors = dataflow->successors;
    const uint32_t* restrict postorder = dataflow->postorder;
    const uint64_t* restrict use_rows = dataflow->use;
    const uint64_t* restrict def_rows = dataflow->def;
    uint64_t* restrict in_rows = dataflow->in;
    uint64_t* restrict out_rows = dataflow->out;
    // dataflow_create allocates these columns separately. Only their bases are restricted: a successor may be
    // the current block, so derived pointers into in_rows must retain their possible overlap.
    if (row_words != 0 && block_count != 0)
    {
        const uint64_t matrix_bytes = (uint64_t)block_count * row_words * sizeof(uint64_t);
        memset(in_rows, 0, matrix_bytes);
        memset(out_rows, 0, matrix_bytes);
        bool changed;
        do
        {
            __mmask8 changes = 0;
            for (uint32_t order = 0; order < block_count; order += 1)
            {
                const uint32_t block = postorder[order];
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
            iterations += changed;
        } while (changed);
    }
    return iterations;
}

// The two loops are separate functions, so that neither changes how Clang compiles the other: inlined together, the
// general loop's registers and stack slots changed, and medium-nested took 3.6% more cycles after scalar code. Each
// starts a 64-byte line, so that a build's code before them doesn't decide where their loops fall in lines.
static uint64_t dataflow_liveness_vector(Dataflow* dataflow)
{
    uint64_t iterations;
    if (dataflow->row_words == BUSTER_DATAFLOW_VECTOR_WORDS && dataflow->block_count != 0)
    {
        iterations = dataflow_liveness_vector_one(dataflow);
    }
    else
    {
        iterations = dataflow_liveness_vector_rows(dataflow);
    }
    return iterations;
}

static uint64_t dataflow_liveness_scalar(Dataflow* dataflow)
{
    uint64_t iterations = 1;
    const uint32_t block_count = dataflow->block_count;
    const uint64_t row_words = dataflow->row_words;
    const uint64_t value_words = ((uint64_t)dataflow->value_count + 63) / 64;
    const uint32_t* restrict successor_offsets = dataflow->successor_offsets;
    const uint32_t* restrict successors = dataflow->successors;
    const uint32_t* restrict postorder = dataflow->postorder;
    const uint64_t* restrict use_rows = dataflow->use;
    const uint64_t* restrict def_rows = dataflow->def;
    uint64_t* restrict in_rows = dataflow->in;
    uint64_t* restrict out_rows = dataflow->out;
    // As in the vector kernel, every access to in stays based on one restricted matrix pointer, including
    // self-loop reads. The scalar bound excludes whole padding words; clearing still covers every padded row.
    if (row_words != 0 && block_count != 0)
    {
        const uint64_t matrix_bytes = (uint64_t)block_count * row_words * sizeof(uint64_t);
        memset(in_rows, 0, matrix_bytes);
        memset(out_rows, 0, matrix_bytes);
        bool changed;
        do
        {
            uint64_t changes = 0;
            for (uint32_t order = 0; order < block_count; order += 1)
            {
                const uint32_t block = postorder[order];
                const uint64_t row = (uint64_t)block * row_words;
                const uint32_t begin = successor_offsets[block];
                const uint32_t end = successor_offsets[(uint64_t)block + 1];
                // Keep a distinct 64-bit oracle and a genuine scalar benchmark alternative at -O3.
                #pragma clang loop vectorize(disable) interleave(disable)
                for (uint64_t word = 0; word < value_words; word += 1)
                {
                    uint64_t out = 0;
                    // Not unrolled: unrolled by 8, this loop over a block's few successors split each word into a
                    // header, a remainder loop and a latch, two taken jumps apart, which the front end ran up to 24%
                    // slower, and in states that differ between processes, depending on where the code fell in 64-byte
                    // lines. Instead Clang unrolls the word loop by 4, which runs four words of a block with one
                    // successor per taken branch (docs/dataflow.md, "Where the scalar twin's code falls").
                    #pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
                    for (uint32_t edge = begin; edge < end; edge += 1)
                    {
                        const uint64_t* successor_in = in_rows + (uint64_t)successors[edge] * row_words;
                        out |= successor_in[word];
                    }
                    const uint64_t in = use_rows[row + word] | (out & ~def_rows[row + word]);
                    changes |= in ^ in_rows[row + word];
                    out_rows[row + word] = out;
                    in_rows[row + word] = in;
                }
            }
            changed = changes != 0;
            iterations += changed;
        } while (changed);
    }
    return iterations;
}

static uint64_t dataflow_liveness(Dataflow* dataflow)
{
    return dataflow_liveness_vector(dataflow);
}

static bool dataflow_intersects_vector(const uint64_t* left, const uint64_t* right, uint64_t row_words)
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

static bool dataflow_intersects_scalar(const uint64_t* left, const uint64_t* right, uint64_t row_words)
{
    uint64_t intersections = 0;
    #pragma clang loop vectorize(disable) interleave(disable)
    for (uint64_t word = 0; word < row_words; word += 1)
    {
        intersections |= left[word] & right[word];
    }
    return intersections != 0;
}
