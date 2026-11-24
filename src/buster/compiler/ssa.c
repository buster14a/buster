// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Local promotion over a completed CFG. SsaInput is a view of the
// caller's instruction columns, not a second IR: VALUE hides every operation unrelated to local promotion. Original
// instruction ids stay stable. The caller rewrites its operands through replacements, removes promoted READ/WRITE
// instructions, inserts the returned phis at block entry, and discards unreachable blocks. The caller's full IR
// verifier remains responsible for VALUE's hidden operands, terminators and the semantics of memory operations.
//
// Inputs within the dense budget take the dense constructor (ssa_construct_vector: sparse dominance frontiers, pruned
// phis of every local at once by liveness bit rows, entry and exit rows a block at a time in reverse postorder). Larger
// inputs reuse one local's predecessor-reading workspace, which reads variables through predecessors with an explicit
// stack (Braun et al., CC 2013), retaining only its compact phi graph (ssa_bounded.c); ssa_verify.c checks independent
// bounded equations. Both remove trivial phis and redundant phi cycles without recursion. No implementation from the
// papers is copied.
// Include after lane.c, dataflow.c and dominators.c. All temporary/output memory belongs to the caller's arenas.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_SSA_NONE UINT32_MAX
#define BUSTER_SSA_VALUE 0
#define BUSTER_SSA_READ 1
#define BUSTER_SSA_WRITE 2
#define BUSTER_SSA_ADDRESS 3
#define BUSTER_SSA_KINDS 4

#define BUSTER_SSA_TYPE_VOID 0
#define BUSTER_SSA_TYPE_I1 1
#define BUSTER_SSA_TYPE_I8 2
#define BUSTER_SSA_TYPE_I16 3
#define BUSTER_SSA_TYPE_I32 4
#define BUSTER_SSA_TYPE_I64 5
#define BUSTER_SSA_TYPE_I128 6
#define BUSTER_SSA_TYPE_F32 7
#define BUSTER_SSA_TYPE_F64 8
#define BUSTER_SSA_TYPE_F80 9
#define BUSTER_SSA_TYPE_PTR 10
// The shared IR's vectors and masks (#179), whose codes are its own: the core compares types alone, so any of them
// promotes as a scalar does. Which a caller admits is its own (ssa_ir.c takes both, #179 and #141).
#define BUSTER_SSA_TYPE_V4I32 11
#define BUSTER_SSA_TYPE_V8I32 12
#define BUSTER_SSA_TYPE_V16I32 13
#define BUSTER_SSA_TYPE_M8 14
#define BUSTER_SSA_TYPE_M16 15
#define BUSTER_SSA_TYPE_M32 16
#define BUSTER_SSA_TYPE_M64 17
#define BUSTER_SSA_TYPE_AGGREGATE 18
#define BUSTER_SSA_TYPES 19

#define BUSTER_SSA_LOCAL_ADDRESS_TAKEN 1
#define BUSTER_SSA_LOCAL_VOLATILE 2
#define BUSTER_SSA_LOCAL_ATOMIC 4
#define BUSTER_SSA_LOCAL_FLAGS 7
#define BUSTER_SSA_VECTOR_WORDS 8

typedef enum SsaError
{
    SSA_OK,
    SSA_ERROR_INVALID,
    SSA_ERROR_MEMORY,
    SSA_ERROR_LIMIT,
} SsaError;

typedef struct SsaInput SsaInput;
struct SsaInput
{
    uint32_t block_count;
    uint32_t instruction_count;
    uint32_t local_count;
    uint32_t edge_count;
    // Entry is block 0, with no incoming edges. Split an entry targeted by a backedge before calling. Empty blocks
    // are legal. The offsets are monotone, start at zero and end at their corresponding count.
    const uint32_t* block_offsets;
    const uint32_t* successor_offsets;
    const uint32_t* successors;
    // Each instruction is a VALUE (opaque ordinary value), READ, WRITE or ADDRESS. A READ defines its original id;
    // a WRITE assigns operands[i] to locals[i] and defines no value; ADDRESS defines a pointer to locals[i]. A WRITE's
    // operand may also be its own local's undef (instruction_count + locals[i]): the local's value becomes indeterminate
    // there, as a block-scope object's does each time its block is entered (C17 6.2.4p6), which ends its liveness.
    // All local accesses, including address-taking discovered late in a body, must appear here. Volatile, atomic and
    // aggregate locals stay in memory. Parameters are VALUEs and WRITEs in the entry; all other locals start undef.
    const uint8_t* kinds;
    const uint8_t* types;
    const uint32_t* locals;
    const uint32_t* operands;
    const uint8_t* local_types;
    const uint8_t* local_flags;
};

typedef struct SsaResult SsaResult;
struct SsaResult
{
    // Nonzero iff the local can be promoted; every ADDRESS excludes its local even if its flags omitted that fact.
    uint8_t* promoted;
    uint8_t* reachable;
    // One per original instruction: identity for VALUE, ADDRESS and memory READ, NONE for WRITE, replacement for
    // promoted READ (undef in an unreachable block). Undef(local) = input.instruction_count + local. Other appended
    // values are phi_base + phi index. Undef is a real value, never a wildcard when simplifying a phi.
    uint32_t* replacements;
    uint32_t phi_base;
    uint32_t phi_count;
    uint32_t operand_count;
    uint32_t* phi_blocks;
    uint32_t* phi_locals;
    uint32_t* phi_offsets;
    uint32_t* phi_operands;
    // Incoming edges from reachable blocks only, ordered by original source block and successor position. Repeated
    // edges stay repeated. Each phi has one operand for every edge of its block, in this exact order.
    uint32_t* predecessor_offsets;
    uint32_t* predecessors;
    uint32_t* predecessor_edges;
    uint32_t reachable_edge_count;
};

typedef struct SsaCheck SsaCheck;
struct SsaCheck
{
    SsaError error;
    uint32_t instruction;
    const char* rule;
};

// Inputs are immutable, with no padding requirement. Output and scratch arenas must be distinct, nonoverlapping,
// and own no input columns. On any failure, both lengths and *result are unchanged. Scratch is restored on success.
// A zero-block input is accepted only with zero instructions/locals/edges and a zero element in both offset arrays.
static __attribute__((unused)) SsaError ssa_build(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result);
// Checks local replacements, types, dominance (phi operands at predecessor exit), exact predecessor multiplicity,
// and absence of trivial phis. It also checks reaching-definition equations independently of either constructor.
static __attribute__((unused)) SsaCheck ssa_verify(const SsaInput* input, const SsaResult* result, Arena* scratch);


typedef struct SsaWork SsaWork;
struct SsaWork
{
    const SsaInput* input;
    Arena* scratch;
    SsaResult result;
    Dataflow flow;
    Dominators dominators;
    uint32_t promoted_count;
    uint32_t* dense;
    uint32_t* local_ids;
    uint32_t* instruction_blocks;
    uint32_t* last;
    uint32_t* entry;
    uint32_t phi_capacity;
    uint32_t operand_capacity;
    uint32_t phi_count;
    uint32_t operand_count;
    uint32_t* phi_blocks;
    uint32_t* phi_locals;
    uint32_t* phi_offsets;
    uint32_t* phi_operands;
    // Original values, one undef per local, then provisional phis. Redirects are acyclic; phi operand edges may cycle.
    uint32_t* aliases;
    SsaError construction_error;
};

static uint32_t ssa_resolve(SsaWork* work, uint32_t value)
{
    uint32_t result = value;
    while (work->aliases[result] != result)
    {
        result = work->aliases[result];
    }
    while (work->aliases[value] != value)
    {
        const uint32_t next = work->aliases[value];
        work->aliases[value] = result;
        value = next;
    }
    return result;
}

static uint32_t ssa_undefined(const SsaWork* work, uint32_t local)
{
    return work->input->instruction_count + local;
}

// The sparse frontiers' budget (BUSTER_SSA_FRONTIER_PAIRS) and the dense constructor's.
#define BUSTER_SSA_DOMINANCE_BYTES (16ull << 20)
#define BUSTER_SSA_DENSE_BYTES (16ull << 20)

// Bound every Cartesian workspace. The estimate (128 bytes a cell) still holds the earlier constructor's tables of a u32
// a cell; the dense constructor's are its `last` row (a u32 a cell), the liveness, placement and pending bit rows, its
// cleanup and emission. The topology and instruction terms deliberately overestimate their aligned columns. The
// frontier pairs have their own cap (BUSTER_SSA_FRONTIER_PAIRS). This is a storage bound, not a measured crossover;
// eligibility can only reduce these conservative input-local extents.
static bool ssa_dense_fits(const SsaInput* input)
{
    const uint64_t cells = (uint64_t)input->block_count * input->local_count;
    const uint64_t operands = (uint64_t)input->edge_count * input->local_count;
    bool fits = cells <= BUSTER_SSA_DENSE_BYTES / 128 && operands <= BUSTER_SSA_DENSE_BYTES / 16;
    if (fits)
    {
        const uint64_t rows = ((uint64_t)input->local_count + BUSTER_DATAFLOW_VECTOR_BITS - 1) / BUSTER_DATAFLOW_VECTOR_BITS;
        const uint64_t flow = (uint64_t)input->block_count * rows * BUSTER_DATAFLOW_VECTOR_BYTES * 4;
        const uint64_t linear = ((uint64_t)input->instruction_count + input->local_count) * 32 + (uint64_t)input->block_count * 256 + (uint64_t)input->edge_count * 32 + 8192;
        fits = flow <= BUSTER_SSA_DENSE_BYTES && linear <= BUSTER_SSA_DENSE_BYTES && cells * 128 + operands * 16 + flow + linear <= BUSTER_SSA_DENSE_BYTES;
    }
    return fits;
}

static void* ssa_allocate(Arena* arena, uint64_t count, uint64_t bytes)
{
    // A nonnull empty allocation keeps the checked setup simple; no empty column is subsequently read.
    return arena_push_array(arena, count == 0 ? 1 : count, bytes, 64);
}

static SsaError ssa_check_input(const SsaInput* input)
{
    SsaError error = SSA_OK;
    if (input == 0 || input->block_offsets == 0 || input->successor_offsets == 0)
    {
        error = SSA_ERROR_INVALID;
    }
    if (error == SSA_OK && ((uint64_t)input->instruction_count + input->local_count >= BUSTER_SSA_NONE))
    {
        error = SSA_ERROR_LIMIT;
    }
    if (error == SSA_OK)
    {
        const bool empty = input->instruction_count == 0 && input->local_count == 0 && input->edge_count == 0;
        const bool instructions = input->instruction_count == 0 || (input->kinds != 0 && input->types != 0 && input->locals != 0 && input->operands != 0);
        const bool locals = input->local_count == 0 || (input->local_types != 0 && input->local_flags != 0);
        if ((!empty && input->block_count == 0) || !instructions || !locals || (input->edge_count != 0 && input->successors == 0))
        {
            error = SSA_ERROR_INVALID;
        }
    }
    if (error == SSA_OK && (input->block_offsets[0] != 0 || input->successor_offsets[0] != 0 || input->block_offsets[input->block_count] != input->instruction_count || input->successor_offsets[input->block_count] != input->edge_count))
    {
        error = SSA_ERROR_INVALID;
    }
    for (uint32_t block = 0; error == SSA_OK && block < input->block_count; block += 1)
    {
        if (input->block_offsets[block] > input->block_offsets[block + 1] || input->block_offsets[block + 1] > input->instruction_count || input->successor_offsets[block] > input->successor_offsets[block + 1] || input->successor_offsets[block + 1] > input->edge_count)
        {
            error = SSA_ERROR_INVALID;
        }
    }
    for (uint32_t edge = 0; error == SSA_OK && edge < input->edge_count; edge += 1)
    {
        if (input->successors[edge] == 0 || input->successors[edge] >= input->block_count)
        {
            error = SSA_ERROR_INVALID;
        }
    }
    for (uint32_t local = 0; error == SSA_OK && local < input->local_count; local += 1)
    {
        if (input->local_types[local] == BUSTER_SSA_TYPE_VOID || input->local_types[local] >= BUSTER_SSA_TYPES || (input->local_flags[local] & ~BUSTER_SSA_LOCAL_FLAGS) != 0)
        {
            error = SSA_ERROR_INVALID;
        }
    }
    for (uint32_t i = 0; error == SSA_OK && i < input->instruction_count; i += 1)
    {
        const uint8_t kind = input->kinds[i];
        const uint8_t type = input->types[i];
        if (kind >= BUSTER_SSA_KINDS || type >= BUSTER_SSA_TYPES)
        {
            error = SSA_ERROR_INVALID;
        }
        else if (kind != BUSTER_SSA_VALUE)
        {
            const uint32_t local = input->locals[i];
            if (local >= input->local_count)
            {
                error = SSA_ERROR_INVALID;
            }
            else if ((kind == BUSTER_SSA_READ && type != input->local_types[local]) || (kind == BUSTER_SSA_ADDRESS && type != BUSTER_SSA_TYPE_PTR))
            {
                error = SSA_ERROR_INVALID;
            }
            else if (kind == BUSTER_SSA_WRITE)
            {
                const uint32_t value = input->operands[i];
                const bool undefined = value == input->instruction_count + local;
                if (!undefined && (value >= input->instruction_count || input->kinds[value] == BUSTER_SSA_WRITE || input->types[value] != input->local_types[local]))
                {
                    error = SSA_ERROR_INVALID;
                }
            }
        }
    }
    return error;
}

static void ssa_promote(SsaWork* work)
{
    const SsaInput* input = work->input;
    {
        for (uint64_t first = 0; first < input->local_count; first += 64)
        {
            const uint32_t count = input->local_count - first < 64 ? input->local_count - first : 64;
            const __mmask64 active = count == 64 ? UINT64_MAX : (1ull << count) - 1;
            const __m512i types = _mm512_maskz_loadu_epi8(active, input->local_types + first);
            const __m512i flags = _mm512_maskz_loadu_epi8(active, input->local_flags + first);
            const __mmask64 scalar = _mm512_mask_cmplt_epu8_mask(active, types, _mm512_set1_epi8(BUSTER_SSA_TYPE_AGGREGATE));
            const __mmask64 eligible = _mm512_mask_cmpeq_epi8_mask(scalar, flags, _mm512_setzero_si512());
            _mm512_mask_storeu_epi8(work->result.promoted + first, active, _mm512_maskz_mov_epi8(eligible, _mm512_set1_epi8(1)));
        }
        for (uint64_t first = 0; first < input->instruction_count; first += 64)
        {
            const uint32_t count = input->instruction_count - first < 64 ? input->instruction_count - first : 64;
            const __mmask64 active = count == 64 ? UINT64_MAX : (1ull << count) - 1;
            const __m512i kinds = _mm512_maskz_loadu_epi8(active, input->kinds + first);
            uint64_t addresses = _mm512_mask_cmpeq_epi8_mask(active, kinds, _mm512_set1_epi8(BUSTER_SSA_ADDRESS));
            while (addresses != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctzll(addresses);
                work->result.promoted[input->locals[first + lane]] = 0;
                addresses &= addresses - 1;
            }
        }
    }
    for (uint32_t local = 0; local < input->local_count; local += 1)
    {
        work->dense[local] = BUSTER_SSA_NONE;
        if (work->result.promoted[local] != 0)
        {
            work->dense[local] = work->promoted_count;
            work->local_ids[work->promoted_count] = local;
            work->promoted_count += 1;
        }
    }
}

// The shared analyzer needs two disjoint arenas, including when ssa_verify has only one caller scratch arena.
// Reserve its exact tree/frontier output extent at the current tail, then lend the remaining tail as temporary
// scratch through the parent, which commits its growing tail. Only the already-pushed output is a borrowed view;
// the parent arena owns the mapping and restores the whole phase.
// The output layout is seven u32 columns (tree_offsets has one extra element), followed by an optional aligned
// frontier matrix. No loops are requested. This bound follows dominators.c's published column/storage contract.
static SsaError ssa_analyze(SsaWork* work)
{
    Arena* scratch = work->scratch;
    const SsaInput* input = work->input;
    const uint64_t saved = scratch->length;
    const uint32_t blocks = input->block_count;
    const uint64_t capacity = blocks == 0 ? 0 : ((uint64_t)blocks * 7 + 1) * sizeof(uint32_t);
    uint8_t* storage = arena_push(scratch, capacity, BUSTER_DOMINATOR_VECTOR_BYTES);
    SsaError error = storage != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        const uint64_t output_start = scratch->length - capacity;
        Arena output = {
            .base = storage,
            .capacity = capacity,
        };
        const DominatorCfg cfg = {
            .block_count = blocks,
            .edge_count = input->edge_count,
            .entry = blocks == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = input->successor_offsets,
            .successors = input->successors,
            .predecessor_offsets = work->result.predecessor_offsets,
            .predecessors = work->result.predecessors,
        };
        // The tree alone: the vector constructor lists its frontiers sparsely (ssa_frontiers).
        const DominatorOptions options = {
            .outputs = 0,
            .max_matrix_bytes = BUSTER_SSA_DOMINANCE_BYTES,
        };
        const DominatorStatus status = dominators_analyze(&output, scratch, &cfg, options, &work->dominators);
        if (status == DOMINATOR_OK)
        {
            // Temporary pointers never escape the analyzer. All retained pointers precede output.length, so any
            // unused reservation/alignment slack can immediately return to the parent's ordinary scratch tail.
            arena_pop_to(scratch, output_start + output.length);
        }
        else
        {
            error = status == DOMINATOR_NO_MEMORY ? SSA_ERROR_MEMORY : status == DOMINATOR_MATRIX_LIMIT ? SSA_ERROR_LIMIT : SSA_ERROR_INVALID;
        }
    }
    if (error != SSA_OK)
    {
        arena_pop_to(scratch, saved);
    }
    return error;
}

static bool ssa_dominates(const SsaWork* work, uint32_t definition, uint32_t use)
{
    return dominators_dominates(&work->dominators, definition, use);
}

static SsaError ssa_prepare_path(SsaWork* work, const SsaInput* input, Arena* scratch, bool dense_flow)
{
    *work = (SsaWork){
        .input = input,
        .scratch = scratch,
    };
    SsaError error = ssa_check_input(input);
    if (error == SSA_OK)
    {
        work->result.phi_base = input->instruction_count + input->local_count;
        work->result.promoted = ssa_allocate(scratch, input->local_count, sizeof(uint8_t));
        work->result.reachable = ssa_allocate(scratch, input->block_count, sizeof(uint8_t));
        work->dense = ssa_allocate(scratch, input->local_count, sizeof(uint32_t));
        work->local_ids = ssa_allocate(scratch, input->local_count, sizeof(uint32_t));
        work->instruction_blocks = ssa_allocate(scratch, input->instruction_count, sizeof(uint32_t));
        work->result.predecessor_offsets = ssa_allocate(scratch, (uint64_t)input->block_count + 1, sizeof(uint32_t));
        work->result.predecessors = ssa_allocate(scratch, input->edge_count, sizeof(uint32_t));
        work->result.predecessor_edges = ssa_allocate(scratch, input->edge_count, sizeof(uint32_t));
        if (work->result.promoted == 0 || work->result.reachable == 0 || work->dense == 0 || work->local_ids == 0 || work->instruction_blocks == 0 || work->result.predecessor_offsets == 0 || work->result.predecessors == 0 || work->result.predecessor_edges == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    uint32_t* cursor = 0;
    if (error == SSA_OK)
    {
        ssa_promote(work);
        cursor = ssa_allocate(scratch, input->block_count, sizeof(uint32_t));
        // Only the budgeted dense constructor needs liveness matrices. Preserve its existing Dataflow layout;
        // the bounded predecessor reader and independent verifier borrow topology without any local bit rows.
        if (cursor == 0 || !dataflow_create(scratch, input->block_count, dense_flow ? work->promoted_count : 0, input->edge_count, &work->flow))
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    if (error == SSA_OK)
    {
        memcpy(work->flow.successor_offsets, input->successor_offsets, ((uint64_t)input->block_count + 1) * sizeof(uint32_t));
        if (input->edge_count != 0)
        {
            memcpy(work->flow.successors, input->successors, (uint64_t)input->edge_count * sizeof(uint32_t));
        }
        if (!dataflow_postorder(&work->flow, scratch))
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    if (error == SSA_OK)
    {
        // Shared validation compares the complete edge multisets. Retain every predecessor, including repeated
        // edges and edges from unreachable sources, until analysis has finished borrowing these columns.
        memset(work->result.predecessor_offsets, 0, ((uint64_t)input->block_count + 1) * sizeof(uint32_t));
        for (uint32_t edge = 0; edge < input->edge_count; edge += 1)
        {
            work->result.predecessor_offsets[input->successors[edge] + 1] += 1;
        }
        for (uint32_t block = 0; block < input->block_count; block += 1)
        {
            work->result.predecessor_offsets[block + 1] += work->result.predecessor_offsets[block];
            cursor[block] = work->result.predecessor_offsets[block];
        }
        for (uint32_t block = 0; block < input->block_count; block += 1)
        {
            for (uint32_t i = input->block_offsets[block]; i < input->block_offsets[block + 1]; i += 1)
            {
                work->instruction_blocks[i] = block;
            }
            for (uint32_t edge = input->successor_offsets[block]; edge < input->successor_offsets[block + 1]; edge += 1)
            {
                const uint32_t slot = cursor[input->successors[edge]]++;
                work->result.predecessors[slot] = block;
                work->result.predecessor_edges[slot] = edge;
            }
        }
        error = ssa_analyze(work);
    }
    if (error == SSA_OK)
    {
        for (uint32_t block = 0; block < input->block_count; block += 1)
        {
            work->result.reachable[block] = work->dominators.rpo_index[block] != BUSTER_DOMINATOR_NONE;
        }
        // The analysis owns no input pointers. Stable in-place compaction now gives SSA only incoming edges from
        // reachable sources, preserving its original source-block/successor-position order and multiplicities.
        uint32_t first = 0;
        uint32_t retained = 0;
        for (uint32_t block = 0; block < input->block_count; block += 1)
        {
            const uint32_t end = work->result.predecessor_offsets[block + 1];
            work->result.predecessor_offsets[block] = retained;
            for (uint32_t edge = first; edge < end; edge += 1)
            {
                const uint32_t predecessor = work->result.predecessors[edge];
                if (work->result.reachable[predecessor] != 0)
                {
                    work->result.predecessors[retained] = predecessor;
                    work->result.predecessor_edges[retained] = work->result.predecessor_edges[edge];
                    retained += 1;
                }
            }
            first = end;
        }
        work->result.predecessor_offsets[input->block_count] = retained;
        work->result.reachable_edge_count = retained;
    }
    for (uint32_t i = 0; error == SSA_OK && i < input->instruction_count; i += 1)
    {
        const uint32_t block = work->instruction_blocks[i];
        // A write of its local's undef has no definition to dominate it.
        if (input->kinds[i] == BUSTER_SSA_WRITE && input->operands[i] < input->instruction_count && work->result.reachable[block] != 0)
        {
            const uint32_t value = input->operands[i];
            const uint32_t origin = work->instruction_blocks[value];
            if (!ssa_dominates(work, origin, block) || (origin == block && value >= i))
            {
                error = SSA_ERROR_INVALID;
            }
        }
    }
    return error;
}

static SsaError ssa_prepare(SsaWork* work, const SsaInput* input, Arena* scratch)
{
    return ssa_prepare_path(work, input, scratch, false);
}

static uint32_t ssa_add_phi(SsaWork* work, uint32_t block, uint32_t local)
{
    const uint32_t count = work->result.predecessor_offsets[block + 1] - work->result.predecessor_offsets[block];
    uint32_t value = BUSTER_SSA_NONE;
    if (work->phi_count >= work->phi_capacity || (uint64_t)work->result.phi_base + work->phi_count >= BUSTER_SSA_NONE || count > work->operand_capacity - work->operand_count)
    {
        work->construction_error = SSA_ERROR_LIMIT;
    }
    else
    {
        const uint32_t phi = work->phi_count++;
        value = work->result.phi_base + phi;
        work->aliases[value] = value;
        work->phi_blocks[phi] = block;
        work->phi_locals[phi] = local;
        work->phi_offsets[phi] = work->operand_count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            work->phi_operands[work->operand_count++] = BUSTER_SSA_NONE;
        }
        work->phi_offsets[phi + 1] = work->operand_count;
    }
    return value;
}

typedef struct SsaReadFrame SsaReadFrame;
struct SsaReadFrame
{
    uint32_t block;
    uint32_t edge;
};

static uint32_t ssa_read_entry(SsaWork* work, uint32_t block, uint32_t dense, SsaReadFrame* stack)
{
    const uint32_t local = work->local_ids[dense];
    const uint64_t root = (uint64_t)block * work->promoted_count + dense;
    uint32_t depth = 0;
    if (work->entry[root] == BUSTER_SSA_NONE)
    {
        work->entry[root] = block == 0 ? ssa_undefined(work, local) : ssa_add_phi(work, block, local);
        if (block != 0 && work->construction_error == SSA_OK)
        {
            stack[depth++] = (SsaReadFrame){
                .block = block,
                .edge = work->result.predecessor_offsets[block],
            };
        }
    }
    while (depth != 0 && work->construction_error == SSA_OK)
    {
        SsaReadFrame* frame = &stack[depth - 1];
        const uint32_t begin = work->result.predecessor_offsets[frame->block];
        const uint32_t end = work->result.predecessor_offsets[frame->block + 1];
        if (frame->edge == end)
        {
            depth -= 1;
        }
        else
        {
            const uint32_t predecessor = work->result.predecessors[frame->edge];
            const uint64_t cell = (uint64_t)predecessor * work->promoted_count + dense;
            uint32_t value = work->last[cell];
            bool descend = false;
            if (value == BUSTER_SSA_NONE)
            {
                if (work->entry[cell] == BUSTER_SSA_NONE)
                {
                    work->entry[cell] = predecessor == 0 ? ssa_undefined(work, local) : ssa_add_phi(work, predecessor, local);
                    descend = predecessor != 0;
                }
                value = work->entry[cell];
            }
            if (work->construction_error == SSA_OK)
            {
                const uint32_t phi = work->entry[(uint64_t)frame->block * work->promoted_count + dense] - work->result.phi_base;
                work->phi_operands[work->phi_offsets[phi] + frame->edge - begin] = value;
                frame->edge += 1;
                if (descend)
                {
                    stack[depth++] = (SsaReadFrame){
                        .block = predecessor,
                        .edge = work->result.predecessor_offsets[predecessor],
                    };
                }
            }
        }
    }
    return work->entry[root];
}

// The vector constructor works a block's slots at a time, or the accesses in instruction order, and never one slot over
// every block in anything wider than a bit (#3449):
// 1. ssa_vector_scan reads each reachable block's accesses once, finding them 64 kinds at a time. It lists each slot's
//    last write in the block, sets the def and use bit rows, and gives each read its value from its own block (the
//    write before it). A read with no such write is exposed: it is listed with its block's other exposed reads.
// 2. Liveness over the bit rows (dataflow_liveness) and sparse dominance frontiers (ssa_frontiers).
// 3. ssa_vector_place: each block's pending slots (its writes, then its new phis) cross its frontier edges a row at a
//    time, kept where the slot is live into the join: the pruned phis of the iterated frontier (Cytron et al.), whose
//    least fixpoint doesn't depend on the order. The entry's undefined values need no row: its frontier is empty.
// 4. The phis are numbered block by block, each block's in slot order (ssa_vector_phis). In reverse postorder, a block's
//    entry row is its phis' ids, and its immediate dominator's exit row in its other slots; its exit row is its last
//    writes over its entry row. Its exposed reads take the entry row; a phi's operand on an edge is the predecessor's
//    exit row (ssa_vector_rows). The rows are made a group of chunks at a time in a buffer that stays in L2 (#3449).
// The rows' group (ssa_vector_rows): its slots are [0, `near`); `rows` is the first group's exit rows, a row of `stride`
// u32 for each reachable block by its reverse postorder index, all none. A write to a slot of the first group goes into
// its block's row, as its last write there; one to a later slot goes into `written`, which lists the block's last writes
// past the first group a chunk of 16 slots at a time at the block's end (none for a function whose rows are one group).
static void ssa_vector_scan(SsaWork* work, uint32_t* values, uint32_t* exposed, uint32_t* exposed_offsets, uint32_t* rows, uint64_t stride, uint32_t near, uint32_t* written, uint32_t* def_offsets, uint32_t* def_chunks, uint32_t* def_masks, uint32_t* def_rows)
{
    const SsaInput* input = work->input;
    const uint64_t words = work->flow.row_words;
    const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint32_t first = 0; first < input->instruction_count; first += 16)
    {
        const uint32_t count = input->instruction_count - first < 16 ? input->instruction_count - first : 16;
        _mm512_mask_storeu_epi32(values + first, (__mmask16)((1u << count) - 1), _mm512_add_epi32(positions, _mm512_set1_epi32((int)first)));
    }
    uint32_t listed = 0;
    uint32_t defined = 0;
    for (uint32_t block = 0; block < input->block_count; block += 1)
    {
        const uint32_t begin = input->block_offsets[block];
        const uint32_t end = input->block_offsets[block + 1];
        exposed_offsets[block] = listed;
        def_offsets[block] = defined;
        if (work->result.reachable[block] != 0)
        {
            uint32_t* row = rows + (uint64_t)work->dominators.rpo_index[block] * stride;
            uint64_t* def = work->flow.def + (uint64_t)block * words;
            uint64_t* use = work->flow.use + (uint64_t)block * words;
            bool far = false;
            for (uint32_t first = begin; first < end; first += 64)
            {
                const uint32_t count = end - first < 64 ? end - first : 64;
                const __mmask64 active = count == 64 ? UINT64_MAX : (1ull << count) - 1;
                const __m512i kinds = _mm512_maskz_loadu_epi8(active, input->kinds + first);
                const uint64_t writes = _mm512_mask_cmpeq_epi8_mask(active, kinds, _mm512_set1_epi8(BUSTER_SSA_WRITE));
                uint64_t accesses = writes | _mm512_mask_cmpeq_epi8_mask(active, kinds, _mm512_set1_epi8(BUSTER_SSA_READ));
                while (accesses != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctzll(accesses);
                    const uint32_t i = first + lane;
                    const uint32_t dense = work->dense[input->locals[i]];
                    if (dense != BUSTER_SSA_NONE)
                    {
                        const uint64_t bit = 1ull << (dense % 64);
                        uint32_t* last = dense < near ? row + dense : written + dense;
                        if (((writes >> lane) & 1) != 0)
                        {
                            *last = input->operands[i];
                            def[dense / 64] |= bit;
                            far |= dense >= near;
                        }
                        else
                        {
                            const uint32_t value = *last;
                            const bool open = value == BUSTER_SSA_NONE;
                            values[i] = value;
                            exposed[listed] = i;
                            listed += open;
                            use[dense / 64] |= open ? bit : 0;
                        }
                    }
                    accesses &= accesses - 1;
                }
            }
            // The block's last writes past the first group a chunk at a time, the chunks its def row marks, and the row
            // back to none for the next block.
            for (uint64_t word = near / 64; far && word < words; word += 1)
            {
                const uint64_t below = near / 64 == word ? (1ull << (near % 64)) - 1 : 0;
                for (uint64_t bits = def[word] & ~below; bits != 0; bits &= ~(0xffffull << (__builtin_ctzll(bits) / 16 * 16)))
                {
                    const uint32_t quarter = (uint32_t)__builtin_ctzll(bits) / 16;
                    const uint32_t chunk = (uint32_t)word * 4 + quarter;
                    def_chunks[defined] = chunk;
                    def_masks[defined] = (uint32_t)(bits >> (quarter * 16)) & 0xffff;
                    _mm512_store_si512(def_rows + (uint64_t)defined * 16, _mm512_load_si512(written + (uint64_t)chunk * 16));
                    _mm512_store_si512(written + (uint64_t)chunk * 16, _mm512_set1_epi32(-1));
                    defined += 1;
                }
            }
        }
        else
        {
            for (uint32_t i = begin; i < end; i += 1)
            {
                if (input->kinds[i] == BUSTER_SSA_READ && work->result.promoted[input->locals[i]] != 0)
                {
                    values[i] = ssa_undefined(work, input->locals[i]);
                }
            }
        }
    }
    exposed_offsets[input->block_count] = listed;
    def_offsets[input->block_count] = defined;
}

// Sparse dominance frontiers: a join (two or more reachable incoming edges) is in the frontier of each block on the
// dominator tree's path up from each of its predecessors to the join's immediate dominator, exclusive (Cooper, Harvey
// and Kennedy's runners). The first sweep counts and the second fills; `seen` keeps a block from listing a join twice.
// The most frontier pairs a function's dense construction lists; past them it takes the bounded construction.
#define BUSTER_SSA_FRONTIER_PAIRS ((uint32_t)(BUSTER_SSA_DOMINANCE_BYTES / sizeof(uint32_t)))

static SsaError ssa_frontiers(SsaWork* work, uint32_t budget, uint32_t** offsets_out, uint32_t** targets_out)
{
    const uint32_t blocks = work->input->block_count;
    const SsaResult* view = &work->result;
    uint32_t* offsets = ssa_allocate(work->scratch, (uint64_t)blocks + 1, sizeof(uint32_t));
    uint32_t* seen = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
    uint32_t* cursor = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
    uint32_t* targets = 0;
    uint32_t pairs = 0;
    SsaError error = offsets != 0 && seen != 0 && cursor != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        memset(offsets, 0, ((uint64_t)blocks + 1) * sizeof(uint32_t));
    }
    for (uint32_t sweep = 0; error == SSA_OK && sweep < 2; sweep += 1)
    {
        memset(seen, 0, (uint64_t)blocks * sizeof(uint32_t));
        for (uint32_t b = 0; error == SSA_OK && b < blocks; b += 1)
        {
            const uint32_t begin = view->predecessor_offsets[b];
            const uint32_t end = view->predecessor_offsets[b + 1];
            if (end - begin > 1)
            {
                const uint32_t stop = work->dominators.idom[b];
                for (uint32_t p = begin; p < end; p += 1)
                {
                    uint32_t x = view->predecessors[p];
                    while (x != stop && seen[x] != b + 1)
                    {
                        seen[x] = b + 1;
                        if (sweep == 0)
                        {
                            offsets[x + 1] += 1;
                            pairs += 1;
                        }
                        else
                        {
                            targets[cursor[x]++] = b;
                        }
                        x = work->dominators.idom[x];
                    }
                }
                if (pairs > budget)
                {
                    error = SSA_ERROR_LIMIT;
                }
            }
        }
        if (error == SSA_OK && sweep == 0)
        {
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                offsets[b + 1] += offsets[b];
                cursor[b] = offsets[b];
            }
            targets = ssa_allocate(work->scratch, pairs, sizeof(uint32_t));
            error = targets != 0 ? SSA_OK : SSA_ERROR_MEMORY;
        }
    }
    *offsets_out = offsets;
    *targets_out = targets;
    return error;
}

// The iterated frontier of every slot at once, pruned by liveness: `pending` holds each block's slots still to cross
// its frontier edges, and a FIFO of blocks holds each block once. A slot crossing an edge into a join where it is live
// and has no phi yet places one there, which is pending at the join in turn.
static SsaError ssa_vector_place(SsaWork* work, const uint32_t* frontier_offsets, const uint32_t* frontier_targets, uint64_t* placed)
{
    const uint32_t blocks = work->input->block_count;
    const uint64_t words = work->flow.row_words;
    const uint64_t bytes = (uint64_t)blocks * words * sizeof(uint64_t);
    uint64_t* pending = arena_push(work->scratch, bytes == 0 ? 64 : bytes, 64);
    uint64_t* carry = arena_push(work->scratch, words == 0 ? 64 : words * sizeof(uint64_t), 64);
    uint32_t* queue = ssa_allocate(work->scratch, blocks, sizeof(uint32_t));
    uint8_t* queued = ssa_allocate(work->scratch, blocks, sizeof(uint8_t));
    SsaError error = pending != 0 && carry != 0 && queue != 0 && queued != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK && words != 0 && blocks != 0)
    {
        memset(placed, 0, bytes);
        memcpy(pending, work->flow.def, bytes);
        memset(queued, 0, blocks);
        uint32_t head = 0;
        uint32_t size = 0;
        for (uint32_t order = 0; order < work->dominators.reachable_count; order += 1)
        {
            const uint32_t block = work->dominators.rpo[order];
            __mmask8 any = 0;
            for (uint64_t word = 0; word < words; word += 8)
            {
                const __m512i row = _mm512_load_si512(pending + (uint64_t)block * words + word);
                any |= _mm512_test_epi64_mask(row, row);
            }
            if (any != 0)
            {
                queue[size] = block;
                queued[block] = 1;
                size += 1;
            }
        }
        while (size != 0)
        {
            const uint32_t block = queue[head];
            head = head + 1 == blocks ? 0 : head + 1;
            size -= 1;
            queued[block] = 0;
            uint64_t* row = pending + (uint64_t)block * words;
            memcpy(carry, row, words * sizeof(uint64_t));
            memset(row, 0, words * sizeof(uint64_t));
            for (uint32_t edge = frontier_offsets[block]; edge < frontier_offsets[block + 1]; edge += 1)
            {
                const uint32_t join = frontier_targets[edge];
                uint64_t* join_placed = placed + (uint64_t)join * words;
                uint64_t* join_pending = pending + (uint64_t)join * words;
                const uint64_t* join_live = work->flow.in + (uint64_t)join * words;
                __mmask8 grew = 0;
                for (uint64_t word = 0; word < words; word += 8)
                {
                    const __m512i old = _mm512_load_si512(join_placed + word);
                    const __m512i added = _mm512_andnot_si512(old, _mm512_and_si512(_mm512_load_si512(carry + word), _mm512_load_si512(join_live + word)));
                    _mm512_store_si512(join_placed + word, _mm512_or_si512(old, added));
                    _mm512_store_si512(join_pending + word, _mm512_or_si512(_mm512_load_si512(join_pending + word), added));
                    grew |= _mm512_test_epi64_mask(added, added);
                }
                if (grew != 0 && queued[join] == 0)
                {
                    const uint32_t tail = head + size >= blocks ? head + size - blocks : head + size;
                    queue[tail] = join;
                    queued[join] = 1;
                    size += 1;
                }
            }
        }
    }
    return error;
}

// A block's placed slots from `slot` on, 16 at most, as a mask (bits past the slots are zero in every row).
static __mmask16 ssa_vector_chunk(const uint64_t* row, uint32_t slot)
{
    return (__mmask16)(row[slot / 64] >> (slot % 64));
}

// Numbers the phis block by block, each block's in slot order, and lays out their columns: a block's phis are one run,
// with as many operands each as the block has reachable incoming edges.
static SsaError ssa_vector_phis(SsaWork* work, const uint64_t* placed, uint32_t* phi_first)
{
    const uint32_t blocks = work->input->block_count;
    const uint32_t slots = work->promoted_count;
    const uint64_t words = work->flow.row_words;
    const uint32_t* predecessor_offsets = work->result.predecessor_offsets;
    uint64_t phis = 0;
    uint64_t operands = 0;
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        phi_first[block] = (uint32_t)(phis < UINT32_MAX ? phis : UINT32_MAX);
        uint64_t count = 0;
        for (uint64_t word = 0; word < words; word += 1)
        {
            count += (uint64_t)__builtin_popcountll(placed[(uint64_t)block * words + word]);
        }
        phis += count;
        operands += count * (predecessor_offsets[block + 1] - predecessor_offsets[block]);
    }
    SsaError error = phis < BUSTER_SSA_NONE - work->result.phi_base && operands < BUSTER_SSA_NONE ? SSA_OK : SSA_ERROR_LIMIT;
    if (error == SSA_OK)
    {
        phi_first[blocks] = (uint32_t)phis;
        work->phi_blocks = ssa_allocate(work->scratch, phis, sizeof(uint32_t));
        work->phi_locals = ssa_allocate(work->scratch, phis, sizeof(uint32_t));
        work->phi_offsets = ssa_allocate(work->scratch, phis + 1, sizeof(uint32_t));
        work->phi_operands = ssa_allocate(work->scratch, operands, sizeof(uint32_t));
        if (work->phi_blocks == 0 || work->phi_locals == 0 || work->phi_offsets == 0 || work->phi_operands == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    if (error == SSA_OK)
    {
        const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        uint32_t operand = 0;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            const uint32_t edges = predecessor_offsets[block + 1] - predecessor_offsets[block];
            const uint64_t* row = placed + (uint64_t)block * words;
            uint32_t phi = phi_first[block];
            for (uint32_t slot = 0; phi != phi_first[block + 1] && slot < slots; slot += 16)
            {
                const __mmask16 chunk = ssa_vector_chunk(row, slot);
                const uint32_t count = (uint32_t)__builtin_popcount(chunk);
                const __mmask16 run = (__mmask16)((1u << count) - 1);
                const __m512i locals = _mm512_maskz_loadu_epi32(chunk, work->local_ids + slot);
                _mm512_mask_storeu_epi32(work->phi_locals + phi, run, _mm512_maskz_compress_epi32(chunk, locals));
                _mm512_mask_storeu_epi32(work->phi_blocks + phi, run, _mm512_set1_epi32((int)block));
                _mm512_mask_storeu_epi32(work->phi_offsets + phi, run, _mm512_add_epi32(_mm512_set1_epi32((int)operand), _mm512_mullo_epi32(positions, _mm512_set1_epi32((int)edges))));
                phi += count;
                operand += count * edges;
            }
        }
        work->phi_offsets[phis] = operand;
        work->phi_count = (uint32_t)phis;
        work->operand_count = (uint32_t)operands;
        work->phi_capacity = (uint32_t)phis;
        work->operand_capacity = (uint32_t)operands;
    }
    return error;
}

// The rows' buffer: at most this many bytes of exit rows at once, a group of chunks of 16 slots for every reachable block,
// so that the buffer stays in L2 however wide the function is (#3449); the tests pass smaller budgets.
#define BUSTER_SSA_ROW_GROUP_BYTES (256u << 10)

// In reverse postorder, so that a block's immediate dominator comes first, a group of chunks at a time: each block's
// entry chunks (its phis' ids expanded into their lanes, its immediate dominator's exit elsewhere; the entry's undefined
// values), which its exposed reads in the group take, then its exit chunks (its last writes over its entry). Then each
// phi's operands in the group's chunks: the predecessors' exit chunks compressed to the join's placed lanes. The first
// group's last writes are in `rows` already (ssa_vector_scan); a later group's come from the scan's list, with a block's
// next phi id, next write and its phis' operands so far carried in `carry` from group to group. A function whose rows
// fit the buffer is one group, and nothing holds a u32 a cell.
static void ssa_vector_rows(SsaWork* work, const uint64_t* placed, const uint32_t* phi_first, const uint32_t* exposed, const uint32_t* exposed_offsets, uint32_t* rows, uint32_t group, uint32_t* entry_row, uint32_t* carry,
                            const uint32_t* def_offsets, const uint32_t* def_chunks, const uint32_t* def_masks, const uint32_t* def_rows)
{
    const SsaInput* input = work->input;
    const uint32_t blocks = input->block_count;
    const uint32_t slots = work->promoted_count;
    const uint32_t chunks = (slots + 15) / 16;
    const uint64_t words = work->flow.row_words;
    const uint32_t reachable = work->dominators.reachable_count;
    const uint32_t* rpo = work->dominators.rpo;
    const uint32_t* rpo_index = work->dominators.rpo_index;
    const uint32_t* predecessor_offsets = work->result.predecessor_offsets;
    const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i none = _mm512_set1_epi32(-1);
    const uint64_t stride = (uint64_t)group * 16;
    uint32_t* next_phi = carry;
    uint32_t* next_def = carry + blocks;
    uint32_t* gathered = carry + 2 * (uint64_t)blocks;
    for (uint32_t first = 0; first < chunks; first += group)
    {
        const bool later = first != 0;
        const uint32_t count = chunks - first < group ? chunks - first : group;
        const uint32_t low = first * 16;
        const uint32_t high = (first + count) * 16;
        for (uint32_t order = 0; order < reachable; order += 1)
        {
            const uint32_t block = rpo[order];
            const bool entry = order == 0;
            const uint32_t* dominator = rows + (entry ? 0 : (uint64_t)rpo_index[work->dominators.idom[block]] * stride);
            uint32_t* row = rows + (uint64_t)order * stride;
            const uint64_t* placed_row = placed + (uint64_t)block * words;
            uint32_t next = later ? next_phi[block] : work->result.phi_base + phi_first[block];
            for (uint32_t chunk = 0; chunk < count; chunk += 1)
            {
                const uint32_t slot = low + chunk * 16;
                const __mmask16 phis = entry ? 0 : ssa_vector_chunk(placed_row, slot);
                const __mmask16 active = (__mmask16)_bzhi_u32(0xffff, slots - slot < 16 ? slots - slot : 16);
                const __m512i undefined = _mm512_add_epi32(_mm512_maskz_loadu_epi32(active, work->local_ids + slot), _mm512_set1_epi32((int)input->instruction_count));
                const __m512i reaching = entry ? undefined : _mm512_load_si512(dominator + (uint64_t)chunk * 16);
                const __m512i value = _mm512_mask_expand_epi32(reaching, phis, _mm512_add_epi32(positions, _mm512_set1_epi32((int)next)));
                next += (uint32_t)__builtin_popcount(phis);
                _mm512_store_si512(entry_row + (uint64_t)chunk * 16, value);
                // The first group's row holds the block's last writes (none elsewhere); a later group's starts at its entry.
                const __m512i written = later ? none : _mm512_load_si512(row + (uint64_t)chunk * 16);
                _mm512_store_si512(row + (uint64_t)chunk * 16, _mm512_mask_blend_epi32(_mm512_cmpeq_epi32_mask(written, none), written, value));
            }
            for (uint32_t listed = exposed_offsets[block]; listed < exposed_offsets[block + 1]; listed += 1)
            {
                const uint32_t i = exposed[listed];
                const uint32_t slot = work->dense[input->locals[i]];
                if (slot >= low && slot < high)
                {
                    work->aliases[i] = entry_row[slot - low];
                }
            }
            if (count != chunks)
            {
                next_phi[block] = next;
                uint32_t def = later ? next_def[block] : def_offsets[block];
                for (; def != def_offsets[block + 1] && def_chunks[def] < first + count; def += 1)
                {
                    uint32_t* exit = row + (uint64_t)(def_chunks[def] - first) * 16;
                    _mm512_store_si512(exit, _mm512_mask_blend_epi32((__mmask16)def_masks[def], _mm512_load_si512(exit), _mm512_load_si512(def_rows + (uint64_t)def * 16)));
                }
                next_def[block] = def;
            }
        }
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            if (phi_first[block + 1] != phi_first[block])
            {
                const uint32_t begin = predecessor_offsets[block];
                const uint32_t edges = predecessor_offsets[block + 1] - begin;
                uint32_t* operands = work->phi_operands + work->phi_offsets[phi_first[block]];
                const uint64_t* placed_row = placed + (uint64_t)block * words;
                uint32_t done = later ? gathered[block] : 0;
                for (uint32_t chunk = 0; chunk < count; chunk += 1)
                {
                    const __mmask16 phis = ssa_vector_chunk(placed_row, low + chunk * 16);
                    const uint32_t placed_count = (uint32_t)__builtin_popcount(phis);
                    for (uint32_t edge = 0; placed_count != 0 && edge < edges; edge += 1)
                    {
                        const uint32_t* exit = rows + (uint64_t)rpo_index[work->result.predecessors[begin + edge]] * stride + (uint64_t)chunk * 16;
                        _mm512_storeu_si512(entry_row, _mm512_maskz_compress_epi32(phis, _mm512_load_si512(exit)));
                        for (uint32_t phi = 0; phi < placed_count; phi += 1)
                        {
                            operands[(uint64_t)(done + phi) * edges + edge] = entry_row[phi];
                        }
                    }
                    done += placed_count;
                }
                if (count != chunks)
                {
                    gathered[block] = done;
                }
            }
        }
    }
}

static SsaError ssa_construct_vector(SsaWork* work, uint32_t frontier_budget, uint32_t row_bytes)
{
    const SsaInput* input = work->input;
    const uint32_t blocks = input->block_count;
    const uint64_t words = work->flow.row_words;
    const uint64_t placed_bytes = (uint64_t)blocks * words * sizeof(uint64_t);
    const uint32_t chunks = (work->promoted_count + 15) / 16;
    const uint32_t reachable = work->dominators.reachable_count;
    // The rows' group: as many chunks as fit the buffer for every reachable block, at least one.
    const uint64_t block_bytes = (uint64_t)(reachable == 0 ? 1 : reachable) * 64;
    const uint64_t fit = row_bytes / block_bytes;
    const uint32_t group = fit == 0 ? 1 : fit < chunks ? (uint32_t)fit : chunks == 0 ? 1 : chunks;
    const bool grouped = group != chunks && chunks != 0;
    // A block lists each chunk past the first group it writes in once: no more of them than writes or than its chunks.
    const uint64_t far = (uint64_t)blocks * (chunks - (group < chunks ? group : chunks));
    const uint64_t defs = (far < input->instruction_count ? far : input->instruction_count) + 1;
    uint32_t* values = ssa_allocate(work->scratch, input->instruction_count, sizeof(uint32_t));
    uint32_t* exposed = ssa_allocate(work->scratch, input->instruction_count, sizeof(uint32_t));
    uint32_t* exposed_offsets = ssa_allocate(work->scratch, (uint64_t)blocks + 1, sizeof(uint32_t));
    uint32_t* rows = arena_push(work->scratch, block_bytes * group, 64);
    uint32_t* entry_row = arena_push(work->scratch, ((uint64_t)group + 1) * 64, 64);
    uint32_t* written = grouped ? arena_push(work->scratch, ((uint64_t)chunks + 1) * 64, 64) : entry_row;
    uint32_t* carry = grouped ? ssa_allocate(work->scratch, 3 * (uint64_t)blocks, sizeof(uint32_t)) : entry_row;
    uint32_t* def_offsets = ssa_allocate(work->scratch, (uint64_t)blocks + 1, sizeof(uint32_t));
    uint32_t* def_chunks = ssa_allocate(work->scratch, defs, sizeof(uint32_t));
    uint32_t* def_masks = ssa_allocate(work->scratch, defs, sizeof(uint32_t));
    uint32_t* def_rows = arena_push(work->scratch, defs * 64, 64);
    uint32_t* phi_first = ssa_allocate(work->scratch, (uint64_t)blocks + 1, sizeof(uint32_t));
    uint64_t* placed = arena_push(work->scratch, placed_bytes == 0 ? 64 : placed_bytes, 64);
    uint32_t* frontier_offsets = 0;
    uint32_t* frontier_targets = 0;
    SsaError error = values != 0 && exposed != 0 && exposed_offsets != 0 && rows != 0 && entry_row != 0 && written != 0 && carry != 0 && def_offsets != 0 && def_chunks != 0 && def_masks != 0 && def_rows != 0 && phi_first != 0 && placed != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        memset(rows, 0xff, block_bytes * group);
        if (grouped)
        {
            memset(written, 0xff, ((uint64_t)chunks + 1) * 64);
        }
        memset(placed, 0, placed_bytes);
        ssa_vector_scan(work, values, exposed, exposed_offsets, rows, (uint64_t)group * 16, grouped ? group * 16 : UINT32_MAX, written, def_offsets, def_chunks, def_masks, def_rows);
        error = ssa_frontiers(work, frontier_budget, &frontier_offsets, &frontier_targets);
    }
    if (error == SSA_OK)
    {
        dataflow_liveness(&work->flow);
        error = ssa_vector_place(work, frontier_offsets, frontier_targets, placed);
    }
    if (error == SSA_OK)
    {
        error = ssa_vector_phis(work, placed, phi_first);
    }
    if (error == SSA_OK)
    {
        const uint64_t count = (uint64_t)work->result.phi_base + work->phi_count;
        work->aliases = ssa_allocate(work->scratch, count, sizeof(uint32_t));
        error = work->aliases != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    }
    if (error == SSA_OK)
    {
        const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        const uint64_t count = (uint64_t)work->result.phi_base + work->phi_count;
        memcpy(work->aliases, values, (uint64_t)input->instruction_count * sizeof(uint32_t));
        for (uint64_t value = input->instruction_count; value < count; value += 16)
        {
            const uint64_t lanes = count - value < 16 ? count - value : 16;
            _mm512_mask_storeu_epi32(work->aliases + value, (__mmask16)((1u << lanes) - 1), _mm512_add_epi32(positions, _mm512_set1_epi32((int)value)));
        }
        ssa_vector_rows(work, placed, phi_first, exposed, exposed_offsets, rows, group, entry_row, carry, def_offsets, def_chunks, def_masks, def_rows);
    }
    return error;
}

// Cleanup is followed by compacting only the phis reachable from original promoted reads. Alias substitution can
// leave dead provisional phis behind, particularly along the bounded reader's single-predecessor chains.
static SsaError ssa_emit(SsaWork* work, Arena* output, SsaResult* result)
{
    const SsaInput* input = work->input;
    uint8_t* live = ssa_allocate(work->scratch, work->phi_count, sizeof(uint8_t));
    uint32_t* stack = ssa_allocate(work->scratch, work->phi_count, sizeof(uint32_t));
    uint32_t* mapping = ssa_allocate(work->scratch, work->phi_count, sizeof(uint32_t));
    SsaError error = live != 0 && stack != 0 && mapping != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    SsaResult candidate = {
        .phi_base = work->result.phi_base,
        .reachable_edge_count = work->result.reachable_edge_count,
    };
    if (error == SSA_OK)
    {
        memset(live, 0, work->phi_count);
        uint32_t depth = 0;
        for (uint32_t i = 0; i < input->instruction_count; i += 1)
        {
            if (input->kinds[i] == BUSTER_SSA_READ && work->result.promoted[input->locals[i]] != 0)
            {
                const uint32_t value = ssa_resolve(work, i);
                if (value >= candidate.phi_base && live[value - candidate.phi_base] == 0)
                {
                    live[value - candidate.phi_base] = 1;
                    stack[depth++] = value - candidate.phi_base;
                }
            }
        }
        while (depth != 0)
        {
            const uint32_t phi = stack[--depth];
            for (uint32_t operand = work->phi_offsets[phi]; operand < work->phi_offsets[phi + 1]; operand += 1)
            {
                const uint32_t value = ssa_resolve(work, work->phi_operands[operand]);
                if (value >= candidate.phi_base && live[value - candidate.phi_base] == 0)
                {
                    live[value - candidate.phi_base] = 1;
                    stack[depth++] = value - candidate.phi_base;
                }
            }
        }
        for (uint32_t phi = 0; phi < work->phi_count; phi += 1)
        {
            mapping[phi] = BUSTER_SSA_NONE;
            if (live[phi] != 0)
            {
                mapping[phi] = candidate.phi_count++;
                candidate.operand_count += work->phi_offsets[phi + 1] - work->phi_offsets[phi];
            }
        }
        candidate.promoted = ssa_allocate(output, input->local_count, sizeof(uint8_t));
        candidate.reachable = ssa_allocate(output, input->block_count, sizeof(uint8_t));
        candidate.replacements = ssa_allocate(output, input->instruction_count, sizeof(uint32_t));
        candidate.phi_blocks = ssa_allocate(output, candidate.phi_count, sizeof(uint32_t));
        candidate.phi_locals = ssa_allocate(output, candidate.phi_count, sizeof(uint32_t));
        candidate.phi_offsets = ssa_allocate(output, (uint64_t)candidate.phi_count + 1, sizeof(uint32_t));
        candidate.phi_operands = ssa_allocate(output, candidate.operand_count, sizeof(uint32_t));
        candidate.predecessor_offsets = ssa_allocate(output, (uint64_t)input->block_count + 1, sizeof(uint32_t));
        candidate.predecessors = ssa_allocate(output, candidate.reachable_edge_count, sizeof(uint32_t));
        candidate.predecessor_edges = ssa_allocate(output, candidate.reachable_edge_count, sizeof(uint32_t));
        if (candidate.promoted == 0 || candidate.reachable == 0 || candidate.replacements == 0 || candidate.phi_blocks == 0 || candidate.phi_locals == 0 || candidate.phi_offsets == 0 || candidate.phi_operands == 0 || candidate.predecessor_offsets == 0 || candidate.predecessors == 0 || candidate.predecessor_edges == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    if (error == SSA_OK)
    {
        memcpy(candidate.promoted, work->result.promoted, input->local_count);
        memcpy(candidate.reachable, work->result.reachable, input->block_count);
        memcpy(candidate.predecessor_offsets, work->result.predecessor_offsets, ((uint64_t)input->block_count + 1) * sizeof(uint32_t));
        memcpy(candidate.predecessors, work->result.predecessors, (uint64_t)candidate.reachable_edge_count * sizeof(uint32_t));
        memcpy(candidate.predecessor_edges, work->result.predecessor_edges, (uint64_t)candidate.reachable_edge_count * sizeof(uint32_t));
        for (uint32_t i = 0; i < input->instruction_count; i += 1)
        {
            uint32_t value = ssa_resolve(work, i);
            if (value >= candidate.phi_base)
            {
                value = candidate.phi_base + mapping[value - candidate.phi_base];
            }
            candidate.replacements[i] = input->kinds[i] == BUSTER_SSA_WRITE ? BUSTER_SSA_NONE : value;
        }
        uint32_t operand_count = 0;
        candidate.phi_offsets[0] = 0;
        for (uint32_t phi = 0; phi < work->phi_count; phi += 1)
        {
            if (live[phi] != 0)
            {
                const uint32_t mapped = mapping[phi];
                candidate.phi_blocks[mapped] = work->phi_blocks[phi];
                candidate.phi_locals[mapped] = work->phi_locals[phi];
                for (uint32_t operand = work->phi_offsets[phi]; operand < work->phi_offsets[phi + 1]; operand += 1)
                {
                    uint32_t value = ssa_resolve(work, work->phi_operands[operand]);
                    if (value >= candidate.phi_base)
                    {
                        value = candidate.phi_base + mapping[value - candidate.phi_base];
                    }
                    candidate.phi_operands[operand_count++] = value;
                }
                candidate.phi_offsets[mapped + 1] = operand_count;
            }
        }
        *result = candidate;
    }
    return error;
}


// Phi dependency SCCs, with an explicit DFS and a stack of disjoint subsets. Edges go from a phi to the phis it
// reads, so Tarjan's completion order visits definitions before their users. A component with one outside value
// collapses to that value; a component with several keeps its outside-reading phis and examines its inner subset.
// This is the redundant-cycle rule of Braun et al., section 3.2, implemented without recursive calls or use lists.
typedef struct SsaPhiVisit SsaPhiVisit;
struct SsaPhiVisit
{
    uint32_t phi;
    uint32_t next_operand;
};

typedef struct SsaPhiTask SsaPhiTask;
struct SsaPhiTask
{
    uint32_t first;
    uint32_t count;
    bool component;
};

static SsaError ssa_simplify(SsaWork* work)
{
    const uint32_t count = work->phi_count;
    const uint32_t base = work->result.phi_base;
    SsaError error = SSA_OK;
    uint32_t* members = 0;
    uint32_t* temporary = 0;
    uint32_t* indices = 0;
    uint32_t* lows = 0;
    uint32_t* components = 0;
    uint32_t* pending = 0;
    uint8_t* included = 0;
    uint8_t* active = 0;
    SsaPhiVisit* visits = 0;
    SsaPhiTask* tasks = 0;
    if (count != 0)
    {
        members = arena_push_array(work->scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        temporary = arena_push_array(work->scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        indices = arena_push_array(work->scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        lows = arena_push_array(work->scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        components = arena_push_array(work->scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        pending = arena_push_array(work->scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        included = arena_push_array(work->scratch, count, sizeof(uint8_t), _Alignof(uint8_t));
        active = arena_push_array(work->scratch, count, sizeof(uint8_t), _Alignof(uint8_t));
        visits = arena_push_array(work->scratch, count, sizeof(SsaPhiVisit), _Alignof(SsaPhiVisit));
        tasks = arena_push_array(work->scratch, count, sizeof(SsaPhiTask), _Alignof(SsaPhiTask));
        if (members == 0 || temporary == 0 || indices == 0 || lows == 0 || components == 0 || pending == 0 || included == 0 || active == 0 || visits == 0 || tasks == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    uint32_t task_count = 0;
    if (error == SSA_OK && count != 0)
    {
        memset(included, 0, count);
        memset(active, 0, count);
        uint32_t member_count = 0;
        for (uint32_t phi = 0; phi < count; phi += 1)
        {
            if (ssa_resolve(work, base + phi) == base + phi)
            {
                members[member_count] = phi;
                member_count += 1;
            }
        }
        if (member_count != 0)
        {
            tasks[task_count] = (SsaPhiTask){
                .first = 0,
                .count = member_count,
                .component = false,
            };
            task_count += 1;
        }
    }
    while (task_count != 0)
    {
        task_count -= 1;
        const SsaPhiTask task = tasks[task_count];
        for (uint32_t index = 0; index < task.count; index += 1)
        {
            const uint32_t phi = members[task.first + index];
            included[phi] = 1;
            indices[phi] = BUSTER_SSA_NONE;
        }
        if (!task.component)
        {
            uint32_t next_index = 0;
            uint32_t component_count = 0;
            uint32_t pending_count = 0;
            uint32_t depth = 0;
            for (uint32_t root_index = 0; root_index < task.count; root_index += 1)
            {
                const uint32_t root = members[task.first + root_index];
                if (indices[root] == BUSTER_SSA_NONE)
                {
                    indices[root] = next_index;
                    lows[root] = next_index;
                    next_index += 1;
                    active[root] = 1;
                    pending[pending_count] = root;
                    pending_count += 1;
                    visits[depth] = (SsaPhiVisit){
                        .phi = root,
                        .next_operand = work->phi_offsets[root],
                    };
                    depth += 1;
                    while (depth != 0)
                    {
                        SsaPhiVisit* visit = &visits[depth - 1];
                        const uint32_t phi = visit->phi;
                        if (visit->next_operand != work->phi_offsets[(uint64_t)phi + 1])
                        {
                            const uint32_t operand_index = visit->next_operand;
                            visit->next_operand += 1;
                            const uint32_t value = ssa_resolve(work, work->phi_operands[operand_index]);
                            work->phi_operands[operand_index] = value;
                            if (value >= base && value - base < count && included[value - base] != 0)
                            {
                                const uint32_t child = value - base;
                                if (indices[child] == BUSTER_SSA_NONE)
                                {
                                    indices[child] = next_index;
                                    lows[child] = next_index;
                                    next_index += 1;
                                    active[child] = 1;
                                    pending[pending_count] = child;
                                    pending_count += 1;
                                    visits[depth] = (SsaPhiVisit){
                                        .phi = child,
                                        .next_operand = work->phi_offsets[child],
                                    };
                                    depth += 1;
                                }
                                else if (active[child] != 0 && indices[child] < lows[phi])
                                {
                                    lows[phi] = indices[child];
                                }
                            }
                        }
                        else
                        {
                            if (lows[phi] == indices[phi])
                            {
                                uint32_t member;
                                do
                                {
                                    pending_count -= 1;
                                    member = pending[pending_count];
                                    active[member] = 0;
                                    components[member] = component_count;
                                } while (member != phi);
                                component_count += 1;
                            }
                            depth -= 1;
                            if (depth != 0)
                            {
                                const uint32_t parent = visits[depth - 1].phi;
                                if (lows[phi] < lows[parent])
                                {
                                    lows[parent] = lows[phi];
                                }
                            }
                        }
                    }
                }
            }
            // The DFS's index and low columns can now hold component counts and placement cursors. Reordering
            // only this task's range preserves every other pending task; their disjoint nonempty ranges bound the
            // number of pending tasks by the original phi count, including while descending into inner subsets.
            memset(lows, 0, (uint64_t)component_count * sizeof(uint32_t));
            for (uint32_t index = 0; index < task.count; index += 1)
            {
                const uint32_t phi = members[task.first + index];
                included[phi] = 0;
                lows[components[phi]] += 1;
            }
            uint32_t cursor = 0;
            for (uint32_t component = 0; component < component_count; component += 1)
            {
                indices[component] = cursor;
                cursor += lows[component];
            }
            for (uint32_t index = 0; index < task.count; index += 1)
            {
                const uint32_t phi = members[task.first + index];
                const uint32_t component = components[phi];
                temporary[indices[component]] = phi;
                indices[component] += 1;
            }
            memcpy(members + task.first, temporary, (uint64_t)task.count * sizeof(uint32_t));
            for (uint32_t reverse = component_count; reverse != 0; reverse -= 1)
            {
                const uint32_t component = reverse - 1;
                const uint32_t first = component == 0 ? 0 : indices[component - 1];
                tasks[task_count] = (SsaPhiTask){
                    .first = task.first + first,
                    .count = lows[component],
                    .component = true,
                };
                task_count += 1;
            }
        }
        else
        {
            uint32_t outside = BUSTER_SSA_NONE;
            uint32_t inner_count = 0;
            bool several = false;
            for (uint32_t index = 0; index < task.count; index += 1)
            {
                const uint32_t phi = members[task.first + index];
                bool inner = true;
                for (uint32_t operand = work->phi_offsets[phi]; operand < work->phi_offsets[(uint64_t)phi + 1]; operand += 1)
                {
                    const uint32_t value = ssa_resolve(work, work->phi_operands[operand]);
                    work->phi_operands[operand] = value;
                    const bool inside = value >= base && value - base < count && included[value - base] != 0;
                    if (!inside)
                    {
                        inner = false;
                        if (outside == BUSTER_SSA_NONE)
                        {
                            outside = value;
                        }
                        else
                        {
                            several |= outside != value;
                        }
                    }
                }
                if (inner)
                {
                    temporary[inner_count] = phi;
                    inner_count += 1;
                }
            }
            for (uint32_t index = 0; index < task.count; index += 1)
            {
                included[members[task.first + index]] = 0;
            }
            if (!several)
            {
                for (uint32_t index = 0; index < task.count; index += 1)
                {
                    const uint32_t phi = members[task.first + index];
                    // No external value means an uninitialized cycle. Give each member an undef of its own local's
                    // type; with one external value, that canonical value is outside this SCC, so no redirect cycles.
                    work->aliases[base + phi] = outside == BUSTER_SSA_NONE ? ssa_undefined(work, work->phi_locals[phi]) : outside;
                }
            }
            else if (inner_count != 0)
            {
                memcpy(members + task.first, temporary, (uint64_t)inner_count * sizeof(uint32_t));
                tasks[task_count] = (SsaPhiTask){
                    .first = task.first,
                    .count = inner_count,
                    .component = false,
                };
                task_count += 1;
            }
        }
    }
    return error;
}

#include "ssa_bounded.c"

// The dense constructor, or the bounded one where the sparse frontiers would pass `frontier_budget` pairs
// (BUSTER_SSA_FRONTIER_PAIRS but in the tests), with its rows `row_bytes` at a time (BUSTER_SSA_ROW_GROUP_BYTES but in the
// tests).
static SsaError ssa_construct_dense(SsaWork* work, uint32_t frontier_budget, uint32_t row_bytes)
{
    const uint64_t mark = work->scratch->length;
    SsaError error = ssa_construct_vector(work, frontier_budget, row_bytes);
    if (error == SSA_ERROR_LIMIT)
    {
        arena_pop_to(work->scratch, mark);
        work->construction_error = SSA_OK;
        error = ssa_construct_bounded(work);
    }
    return error;
}

static SsaError ssa_build_selected_path(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result, bool bounded, uint32_t frontier_budget, uint32_t row_bytes)
{
    SsaError error = output != 0 && scratch != 0 && output != scratch && result != 0 ? ssa_check_input(input) : SSA_ERROR_INVALID;
    const uint64_t output_length = output == 0 ? 0 : output->length;
    const uint64_t scratch_length = scratch == 0 ? 0 : scratch->length;
    SsaWork work = { 0 };
    if (error == SSA_OK)
    {
        bounded |= !ssa_dense_fits(input);
        error = ssa_prepare_path(&work, input, scratch, !bounded);
    }
    if (error == SSA_OK)
    {
        error = bounded ? ssa_construct_bounded(&work) : ssa_construct_dense(&work, frontier_budget, row_bytes);
    }
    if (error == SSA_OK)
    {
        error = ssa_simplify(&work);
    }
    if (error == SSA_OK)
    {
        error = ssa_emit(&work, output, result);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_length);
    }
    if (error != SSA_OK && output != 0)
    {
        arena_pop_to(output, output_length);
    }
    return error;
}

static SsaError ssa_build(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result)
{
    return ssa_build_selected_path(input, output, scratch, result, false, BUSTER_SSA_FRONTIER_PAIRS, BUSTER_SSA_ROW_GROUP_BYTES);
}

// The tests force the bounded construction on tiny fixtures as well as the memory-limit cases.
static __attribute__((unused)) SsaError ssa_build_bounded(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result)
{
    return ssa_build_selected_path(input, output, scratch, result, true, BUSTER_SSA_FRONTIER_PAIRS, BUSTER_SSA_ROW_GROUP_BYTES);
}

// The tests' dense construction with a frontier budget of their own, to take the bounded fallback past it.
static __attribute__((unused)) SsaError ssa_build_frontier_budget(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result, uint32_t frontier_budget)
{
    return ssa_build_selected_path(input, output, scratch, result, false, frontier_budget, BUSTER_SSA_ROW_GROUP_BYTES);
}

// The tests' dense construction with its rows `row_bytes` at a time, to take several groups of chunks.
static __attribute__((unused)) SsaError ssa_build_row_budget(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result, uint32_t row_bytes)
{
    return ssa_build_selected_path(input, output, scratch, result, false, BUSTER_SSA_FRONTIER_PAIRS, row_bytes);
}

#include "ssa_verify.c"
