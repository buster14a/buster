// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Local promotion over a completed CFG. SsaInput is a view of the
// caller's instruction columns, not a second IR: VALUE hides every operation unrelated to local promotion. Original
// instruction ids stay stable. The caller rewrites its operands through replacements, removes promoted READ/WRITE
// instructions, inserts the returned phis at block entry, and discards unreachable blocks. The caller's full IR
// verifier remains responsible for VALUE's hidden operands, terminators and the semantics of memory operations.
//
// ssa_build places pruned phis with liveness and dominance-frontier bitsets, then renames through nearest dominating
// definitions. ssa_build_scalar reads variables through predecessors with an explicit stack (Braun et al., CC 2013).
// Both remove trivial phis and redundant phi cycles without recursion. No implementation from the papers is copied.
// Include after lane.c, dataflow.c and dominators.c. All temporary/output memory belongs to the caller's arenas.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>

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
#define BUSTER_SSA_TYPE_AGGREGATE 11
#define BUSTER_SSA_TYPES 12

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
    // a WRITE assigns operands[i] to locals[i] and defines no value; ADDRESS defines a pointer to locals[i].
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
static __attribute__((unused)) SsaError ssa_build_scalar(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result);
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

// This is a memory budget, not a measured scalar/vector crossover. The shared analyzer's retained frontier
// reservation and temporary dominator matrix occupy distinct regions, so their combined payload is bounded here.
#define BUSTER_SSA_DOMINANCE_BYTES (16ull << 20)

static bool ssa_vector_fits(uint32_t blocks)
{
    const uint64_t row_bytes = ((uint64_t)blocks + BUSTER_DOMINATOR_VECTOR_BITS - 1) / BUSTER_DOMINATOR_VECTOR_BITS * BUSTER_DOMINATOR_VECTOR_BYTES;
    return ((uint64_t)blocks * 2 + 1) * row_bytes <= BUSTER_SSA_DOMINANCE_BYTES;
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
                if (value >= input->instruction_count || input->kinds[value] == BUSTER_SSA_WRITE || input->types[value] != input->local_types[local])
                {
                    error = SSA_ERROR_INVALID;
                }
            }
        }
    }
    return error;
}

static void ssa_promote(SsaWork* work, bool vector)
{
    const SsaInput* input = work->input;
    if (vector)
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
    else
    {
        for (uint32_t local = 0; local < input->local_count; local += 1)
        {
            work->result.promoted[local] = input->local_types[local] < BUSTER_SSA_TYPE_AGGREGATE && input->local_flags[local] == 0;
        }
        for (uint32_t i = 0; i < input->instruction_count; i += 1)
        {
            if (input->kinds[i] == BUSTER_SSA_ADDRESS)
            {
                work->result.promoted[input->locals[i]] = 0;
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
// scratch. Both are non-owning views: the parent arena alone owns their mapping and restores the whole phase.
// The output layout is seven u32 columns (tree_offsets has one extra element), followed by an optional aligned
// frontier matrix. No loops are requested. This bound follows dominators.c's published column/storage contract.
static SsaError ssa_analyze(SsaWork* work, bool vector)
{
    Arena* scratch = work->scratch;
    const SsaInput* input = work->input;
    const uint64_t saved = scratch->length;
    const uint32_t blocks = input->block_count;
    const uint64_t row_bytes = ((uint64_t)blocks + BUSTER_DOMINATOR_VECTOR_BITS - 1) / BUSTER_DOMINATOR_VECTOR_BITS * BUSTER_DOMINATOR_VECTOR_BYTES;
    uint64_t capacity = blocks == 0 ? 0 : ((uint64_t)blocks * 7 + 1) * sizeof(uint32_t);
    if (vector && blocks != 0)
    {
        capacity = ((capacity + BUSTER_DOMINATOR_VECTOR_BYTES - 1) & ~(uint64_t)(BUSTER_DOMINATOR_VECTOR_BYTES - 1)) + (uint64_t)blocks * row_bytes;
    }
    uint8_t* storage = arena_push(scratch, capacity, BUSTER_DOMINATOR_VECTOR_BYTES);
    SsaError error = storage != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        const uint64_t output_start = scratch->length - capacity;
        Arena output = {
            .base = storage,
            .capacity = capacity,
        };
        Arena temporary = {
            .base = scratch->base + scratch->length,
            .capacity = scratch->capacity - scratch->length,
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
        const DominatorOptions options = {
            .path = vector ? DOMINATOR_VECTOR : DOMINATOR_SCALAR,
            .outputs = vector ? BUSTER_DOMINATOR_FRONTIERS : 0,
            .max_matrix_bytes = BUSTER_SSA_DOMINANCE_BYTES,
        };
        const DominatorStatus status = dominators_analyze(&output, &temporary, &cfg, options, &work->dominators);
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

static SsaError ssa_prepare(SsaWork* work, const SsaInput* input, Arena* scratch, bool vector)
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
        ssa_promote(work, vector);
        cursor = ssa_allocate(scratch, input->block_count, sizeof(uint32_t));
        if (cursor == 0 || !dataflow_create(scratch, input->block_count, work->promoted_count, input->edge_count, &work->flow))
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
        error = ssa_analyze(work, vector);
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
        if (input->kinds[i] == BUSTER_SSA_WRITE && work->result.reachable[block] != 0)
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

static SsaError ssa_definitions(SsaWork* work)
{
    const SsaInput* input = work->input;
    const uint64_t cells = (uint64_t)input->block_count * work->promoted_count;
    const uint64_t operands = (uint64_t)work->result.reachable_edge_count * work->promoted_count;
    SsaError error = cells < BUSTER_SSA_NONE - work->result.phi_base && operands < BUSTER_SSA_NONE ? SSA_OK : SSA_ERROR_LIMIT;
    if (error == SSA_OK)
    {
        work->phi_capacity = (uint32_t)cells;
        work->operand_capacity = (uint32_t)operands;
        work->last = ssa_allocate(work->scratch, cells, sizeof(uint32_t));
        work->entry = ssa_allocate(work->scratch, cells, sizeof(uint32_t));
        work->phi_blocks = ssa_allocate(work->scratch, cells, sizeof(uint32_t));
        work->phi_locals = ssa_allocate(work->scratch, cells, sizeof(uint32_t));
        work->phi_offsets = ssa_allocate(work->scratch, cells + 1, sizeof(uint32_t));
        work->phi_operands = ssa_allocate(work->scratch, operands, sizeof(uint32_t));
        work->aliases = ssa_allocate(work->scratch, work->result.phi_base + cells, sizeof(uint32_t));
        if (work->last == 0 || work->entry == 0 || work->phi_blocks == 0 || work->phi_locals == 0 || work->phi_offsets == 0 || work->phi_operands == 0 || work->aliases == 0)
        {
            error = SSA_ERROR_MEMORY;
        }
    }
    if (error == SSA_OK)
    {
        memset(work->last, 0xff, cells * sizeof(uint32_t));
        memset(work->entry, 0xff, cells * sizeof(uint32_t));
        for (uint64_t value = 0; value < work->result.phi_base + cells; value += 1)
        {
            work->aliases[value] = (uint32_t)value;
        }
        for (uint32_t block = 0; block < input->block_count; block += 1)
        {
            if (work->result.reachable[block] != 0)
            {
                for (uint32_t i = input->block_offsets[block]; i < input->block_offsets[block + 1]; i += 1)
                {
                    const uint8_t kind = input->kinds[i];
                    if (kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE)
                    {
                        const uint32_t dense = work->dense[input->locals[i]];
                        if (dense != BUSTER_SSA_NONE)
                        {
                            const uint64_t cell = (uint64_t)block * work->promoted_count + dense;
                            const uint64_t word = (uint64_t)block * work->flow.row_words + dense / 64;
                            const uint64_t bit = 1ull << (dense % 64);
                            if (kind == BUSTER_SSA_WRITE)
                            {
                                work->last[cell] = input->operands[i];
                                work->flow.def[word] |= bit;
                            }
                            else if (work->last[cell] == BUSTER_SSA_NONE)
                            {
                                work->flow.use[word] |= bit;
                            }
                        }
                    }
                }
            }
            else
            {
                for (uint32_t i = input->block_offsets[block]; i < input->block_offsets[block + 1]; i += 1)
                {
                    if (input->kinds[i] == BUSTER_SSA_READ && work->result.promoted[input->locals[i]] != 0)
                    {
                        work->aliases[i] = ssa_undefined(work, input->locals[i]);
                    }
                }
            }
        }
    }
    return error;
}

static uint32_t ssa_add_phi(SsaWork* work, uint32_t block, uint32_t local)
{
    const uint32_t phi = work->phi_count++;
    work->phi_blocks[phi] = block;
    work->phi_locals[phi] = local;
    work->phi_offsets[phi] = work->operand_count;
    const uint32_t count = work->result.predecessor_offsets[block + 1] - work->result.predecessor_offsets[block];
    for (uint32_t i = 0; i < count; i += 1)
    {
        work->phi_operands[work->operand_count++] = BUSTER_SSA_NONE;
    }
    work->phi_offsets[phi + 1] = work->operand_count;
    return work->result.phi_base + phi;
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
        if (block != 0)
        {
            stack[depth++] = (SsaReadFrame){
                .block = block,
                .edge = work->result.predecessor_offsets[block],
            };
        }
    }
    while (depth != 0)
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
    return work->entry[root];
}

static SsaError ssa_construct_scalar(SsaWork* work)
{
    const SsaInput* input = work->input;
    SsaReadFrame* stack = ssa_allocate(work->scratch, input->block_count, sizeof(SsaReadFrame));
    uint32_t* current = ssa_allocate(work->scratch, work->promoted_count, sizeof(uint32_t));
    SsaError error = stack != 0 && current != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    for (uint32_t block = 0; error == SSA_OK && block < input->block_count; block += 1)
    {
        if (work->result.reachable[block] != 0)
        {
            memset(current, 0xff, (uint64_t)work->promoted_count * sizeof(uint32_t));
            for (uint32_t i = input->block_offsets[block]; i < input->block_offsets[block + 1]; i += 1)
            {
                const uint8_t kind = input->kinds[i];
                if (kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE)
                {
                    const uint32_t dense = work->dense[input->locals[i]];
                    if (dense != BUSTER_SSA_NONE)
                    {
                        if (kind == BUSTER_SSA_WRITE)
                        {
                            current[dense] = input->operands[i];
                        }
                        else
                        {
                            if (current[dense] == BUSTER_SSA_NONE)
                            {
                                current[dense] = ssa_read_entry(work, block, dense, stack);
                            }
                            work->aliases[i] = current[dense];
                        }
                    }
                }
            }
        }
    }
    return error;
}

static SsaError ssa_place_phis(SsaWork* work)
{
    const uint32_t blocks = work->input->block_count;
    const uint64_t words = work->dominators.row_words;
    const uint64_t* frontiers = work->dominators.frontiers;
    uint64_t* active = ssa_allocate(work->scratch, words, sizeof(uint64_t));
    uint64_t* next = ssa_allocate(work->scratch, words, sizeof(uint64_t));
    uint64_t* live = ssa_allocate(work->scratch, words, sizeof(uint64_t));
    uint64_t* placed = ssa_allocate(work->scratch, words, sizeof(uint64_t));
    SsaError error = (blocks == 0 || frontiers != 0) && active != 0 && next != 0 && live != 0 && placed != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        dataflow_liveness_vector(&work->flow);
        for (uint32_t dense = 0; dense < work->promoted_count; dense += 1)
        {
            memset(active, 0, words * sizeof(uint64_t));
            memset(live, 0, words * sizeof(uint64_t));
            memset(placed, 0, words * sizeof(uint64_t));
            if (blocks != 0)
            {
                active[0] = 1; // The conceptual undef definition at entry participates in placement.
            }
            for (uint32_t block = 0; block < blocks; block += 1)
            {
                if (work->result.reachable[block] != 0)
                {
                    if (work->last[(uint64_t)block * work->promoted_count + dense] != BUSTER_SSA_NONE)
                    {
                        active[block / 64] |= 1ull << (block % 64);
                    }
                    if ((work->flow.in[(uint64_t)block * work->flow.row_words + dense / 64] & (1ull << (dense % 64))) != 0)
                    {
                        live[block / 64] |= 1ull << (block % 64);
                    }
                }
            }
            bool changed = true;
            while (changed)
            {
                memset(next, 0, words * sizeof(uint64_t));
                for (uint64_t word = 0; word < words; word += 1)
                {
                    uint64_t bits = active[word];
                    while (bits != 0)
                    {
                        const uint32_t block = (uint32_t)(word * 64 + __builtin_ctzll(bits));
                        const uint64_t* frontier = frontiers + (uint64_t)block * words;
                        for (uint64_t chunk = 0; chunk < words; chunk += 8)
                        {
                            _mm512_store_si512(next + chunk, _mm512_or_si512(_mm512_load_si512(next + chunk), _mm512_load_si512(frontier + chunk)));
                        }
                        bits &= bits - 1;
                    }
                }
                changed = false;
                for (uint64_t word = 0; word < words; word += 8)
                {
                    const __m512i old = _mm512_load_si512(placed + word);
                    const __m512i candidates = _mm512_andnot_si512(old, _mm512_and_si512(_mm512_load_si512(next + word), _mm512_load_si512(live + word)));
                    _mm512_store_si512(active + word, candidates);
                    _mm512_store_si512(placed + word, _mm512_or_si512(old, candidates));
                    changed |= _mm512_test_epi64_mask(candidates, candidates) != 0;
                }
            }
            for (uint64_t word = 0; word < words; word += 1)
            {
                uint64_t bits = placed[word];
                while (bits != 0)
                {
                    const uint32_t block = (uint32_t)(word * 64 + __builtin_ctzll(bits));
                    work->entry[(uint64_t)block * work->promoted_count + dense] = ssa_add_phi(work, block, work->local_ids[dense]);
                    bits &= bits - 1;
                }
            }
        }
    }
    return error;
}

static void ssa_jump_definitions(uint32_t* up, uint32_t* next, uint32_t blocks)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        if (blocks <= 64)
        {
            // Four register tables cover 64 blocks. Scalar loads replace gathers above this range; Zen 5's gathers
            // cost much more than these short indexed loads. Both arrays have 64 initialized elements here.
            const __m512i a = _mm512_load_si512(up);
            const __m512i b = _mm512_load_si512(up + 16);
            const __m512i c = _mm512_load_si512(up + 32);
            const __m512i d = _mm512_load_si512(up + 48);
            for (uint32_t first = 0; first < blocks; first += 16)
            {
                const uint32_t count = blocks - first < 16 ? blocks - first : 16;
                const __mmask16 active = (__mmask16)((1u << count) - 1);
                const __m512i indices = _mm512_maskz_loadu_epi32(active, up + first);
                const __m512i low = _mm512_permutex2var_epi32(a, indices, b);
                const __m512i high = _mm512_permutex2var_epi32(c, indices, d);
                const __mmask16 upper = _mm512_cmpge_epu32_mask(indices, _mm512_set1_epi32(32));
                const __m512i values = _mm512_mask_blend_epi32(upper, low, high);
                changed |= _mm512_mask_cmpneq_epi32_mask(active, indices, values) != 0;
                _mm512_mask_storeu_epi32(next + first, active, values);
            }
        }
        else
        {
            for (uint32_t block = 0; block < blocks; block += 1)
            {
                next[block] = up[up[block]];
                changed |= next[block] != up[block];
            }
        }
        memcpy(up, next, (uint64_t)blocks * sizeof(uint32_t));
    }
}

static SsaError ssa_entry_definitions(SsaWork* work)
{
    const uint32_t blocks = work->input->block_count;
    const uint64_t count = blocks < 64 ? 64 : blocks;
    uint32_t* up = ssa_allocate(work->scratch, count, sizeof(uint32_t));
    uint32_t* next = ssa_allocate(work->scratch, count, sizeof(uint32_t));
    SsaError error = up != 0 && next != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    for (uint32_t dense = 0; error == SSA_OK && dense < work->promoted_count; dense += 1)
    {
        memset(up, 0, count * sizeof(uint32_t));
        memset(next, 0, count * sizeof(uint32_t));
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            const uint64_t cell = (uint64_t)block * work->promoted_count + dense;
            const bool own = work->last[cell] != BUSTER_SSA_NONE || work->entry[cell] != BUSTER_SSA_NONE;
            up[block] = own || work->result.reachable[block] == 0 ? block : work->dominators.idom[block];
        }
        ssa_jump_definitions(up, next, blocks);
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            const uint64_t cell = (uint64_t)block * work->promoted_count + dense;
            if (work->entry[cell] == BUSTER_SSA_NONE)
            {
                uint32_t value = ssa_undefined(work, work->local_ids[dense]);
                if (block != 0 && work->result.reachable[block] != 0)
                {
                    const uint32_t owner = up[work->dominators.idom[block]];
                    const uint64_t source = (uint64_t)owner * work->promoted_count + dense;
                    if (work->last[source] != BUSTER_SSA_NONE)
                    {
                        value = work->last[source];
                    }
                    else if (work->entry[source] != BUSTER_SSA_NONE)
                    {
                        value = work->entry[source];
                    }
                }
                work->entry[cell] = value;
            }
        }
    }
    return error;
}

// Stable counting-sort by (block, local) turns the repeated accesses to each local into a contiguous segment. A
// segment's prefix maximum of write positions finds all reaching writes 16 accesses at once, without a gather or
// scatter. All ids/values loaded here are complete or masked, including the last segment's tail.
static SsaError ssa_rename_vector(SsaWork* work)
{
    const SsaInput* input = work->input;
    const uint64_t cells = (uint64_t)input->block_count * work->promoted_count;
    uint32_t* offsets = ssa_allocate(work->scratch, cells + 1, sizeof(uint32_t));
    uint32_t* cursors = ssa_allocate(work->scratch, cells, sizeof(uint32_t));
    uint32_t* ids = ssa_allocate(work->scratch, input->instruction_count, sizeof(uint32_t));
    uint32_t* values = ssa_allocate(work->scratch, input->instruction_count, sizeof(uint32_t));
    SsaError error = offsets != 0 && cursors != 0 && ids != 0 && values != 0 ? SSA_OK : SSA_ERROR_MEMORY;
    if (error == SSA_OK)
    {
        memset(offsets, 0, (cells + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < input->instruction_count; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            const uint32_t block = work->instruction_blocks[i];
            if ((kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE) && work->result.reachable[block] != 0)
            {
                const uint32_t dense = work->dense[input->locals[i]];
                if (dense != BUSTER_SSA_NONE)
                {
                    offsets[(uint64_t)block * work->promoted_count + dense + 1] += 1;
                }
            }
        }
        for (uint64_t cell = 0; cell < cells; cell += 1)
        {
            offsets[cell + 1] += offsets[cell];
            cursors[cell] = offsets[cell];
        }
        for (uint32_t i = 0; i < input->instruction_count; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            const uint32_t block = work->instruction_blocks[i];
            if ((kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE) && work->result.reachable[block] != 0)
            {
                const uint32_t dense = work->dense[input->locals[i]];
                if (dense != BUSTER_SSA_NONE)
                {
                    const uint32_t slot = cursors[(uint64_t)block * work->promoted_count + dense]++;
                    ids[slot] = i;
                    values[slot] = kind == BUSTER_SSA_WRITE ? input->operands[i] : BUSTER_SSA_NONE;
                }
            }
        }
        const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        _Alignas(64) uint32_t resolved[16];
        for (uint64_t cell = 0; cell < cells; cell += 1)
        {
            uint32_t current = work->entry[cell];
            const uint32_t end = offsets[cell + 1];
            for (uint64_t first = offsets[cell]; first < end; first += 16)
            {
                const uint32_t count = end - first < 16 ? end - first : 16;
                const __mmask16 active = (__mmask16)((1u << count) - 1);
                const __m512i written = _mm512_maskz_loadu_epi32(active, values + first);
                const __mmask16 writes = _mm512_mask_cmpneq_epi32_mask(active, written, _mm512_set1_epi32(-1));
                __m512i last = _mm512_mask_mov_epi32(_mm512_set1_epi32(-1), writes, positions);
                for (uint32_t step = 1; step <= 8; step *= 2)
                {
                    const __m512i preceding = _mm512_permutexvar_epi32(_mm512_sub_epi32(positions, _mm512_set1_epi32(step)), last);
                    last = _mm512_mask_max_epi32(last, (__mmask16)(0xffffu << step), last, preceding);
                }
                const __mmask16 inside = _mm512_cmpge_epi32_mask(last, _mm512_setzero_si512());
                const __m512i reaching = _mm512_mask_blend_epi32(inside, _mm512_set1_epi32(current), _mm512_permutexvar_epi32(last, written));
                _mm512_store_si512(resolved, reaching);
                uint32_t reads = active & ~writes;
                while (reads != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctz(reads);
                    work->aliases[ids[first + lane]] = resolved[lane];
                    reads &= reads - 1;
                }
                current = resolved[count - 1];
            }
        }
        for (uint32_t phi = 0; phi < work->phi_count; phi += 1)
        {
            const uint32_t block = work->phi_blocks[phi];
            const uint32_t dense = work->dense[work->phi_locals[phi]];
            const uint32_t begin = work->result.predecessor_offsets[block];
            const uint32_t end = work->result.predecessor_offsets[block + 1];
            for (uint32_t edge = begin; edge < end; edge += 1)
            {
                const uint32_t predecessor = work->result.predecessors[edge];
                const uint64_t cell = (uint64_t)predecessor * work->promoted_count + dense;
                work->phi_operands[work->phi_offsets[phi] + edge - begin] = work->last[cell] == BUSTER_SSA_NONE ? work->entry[cell] : work->last[cell];
            }
        }
    }
    return error;
}

static SsaError ssa_construct_vector(SsaWork* work)
{
    SsaError error = ssa_place_phis(work);
    if (error == SSA_OK)
    {
        error = ssa_entry_definitions(work);
    }
    if (error == SSA_OK)
    {
        error = ssa_rename_vector(work);
    }
    return error;
}

// Cleanup is followed by compacting only the phis reachable from original promoted reads. Alias substitution can
// leave dead provisional phis behind, particularly along the scalar path's single-predecessor chains.
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

static SsaError ssa_build_path(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result, bool vector)
{
    SsaError error = output != 0 && scratch != 0 && output != scratch && result != 0 ? ssa_check_input(input) : SSA_ERROR_INVALID;
    const uint64_t output_length = output == 0 ? 0 : output->length;
    const uint64_t scratch_length = scratch == 0 ? 0 : scratch->length;
    SsaWork work = { 0 };
    if (error == SSA_OK)
    {
        vector = vector && ssa_vector_fits(input->block_count);
        error = ssa_prepare(&work, input, scratch, vector);
    }
    if (error == SSA_OK)
    {
        error = ssa_definitions(&work);
    }
    if (error == SSA_OK)
    {
        error = vector ? ssa_construct_vector(&work) : ssa_construct_scalar(&work);
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
    return ssa_build_path(input, output, scratch, result, true);
}

static SsaError ssa_build_scalar(const SsaInput* input, Arena* output, Arena* scratch, SsaResult* result)
{
    return ssa_build_path(input, output, scratch, result, false);
}

static bool ssa_value_valid(const SsaWork* work, const SsaResult* result, uint32_t value, uint32_t block, uint32_t position, uint8_t type)
{
    const SsaInput* input = work->input;
    bool valid = value < (uint64_t)result->phi_base + result->phi_count;
    if (valid && value < input->instruction_count)
    {
        const uint8_t kind = input->kinds[value];
        const uint32_t origin = work->instruction_blocks[value];
        valid = kind != BUSTER_SSA_WRITE && input->types[value] == type;
        valid = valid && !(kind == BUSTER_SSA_READ && result->promoted[input->locals[value]] != 0);
        valid = valid && ssa_dominates(work, origin, block) && (origin != block || value < position);
    }
    else if (valid && value < result->phi_base)
    {
        valid = input->local_types[value - input->instruction_count] == type;
    }
    else if (valid)
    {
        const uint32_t phi = value - result->phi_base;
        valid = result->phi_locals[phi] < input->local_count && result->phi_blocks[phi] < input->block_count;
        valid = valid && input->local_types[result->phi_locals[phi]] == type && ssa_dominates(work, result->phi_blocks[phi], block);
    }
    return valid;
}

static void ssa_check_fail(SsaCheck* check, uint32_t instruction, const char* rule)
{
    if (check->error == SSA_OK)
    {
        *check = (SsaCheck){
            .error = SSA_ERROR_INVALID,
            .instruction = instruction,
            .rule = rule,
        };
    }
}

// The verifier solves symbolic reaching-definition equations from the result's phis, rather than rerunning either
// construction algorithm. Unknown predecessors can be visited later, but every known incoming value must agree at
// a live entry without a phi. At a phi, each operand must equal its predecessor's final local value. This catches
// incorrect yet dominating substitutions, which structural dominance checks alone would accept.
static SsaCheck ssa_verify_equations(SsaWork* work, const SsaResult* result)
{
    const SsaInput* input = work->input;
    const uint64_t cells = (uint64_t)input->block_count * work->promoted_count;
    uint32_t* incoming = ssa_allocate(work->scratch, cells, sizeof(uint32_t));
    uint32_t* outgoing = ssa_allocate(work->scratch, cells, sizeof(uint32_t));
    uint32_t* current = ssa_allocate(work->scratch, work->promoted_count, sizeof(uint32_t));
    SsaCheck check = {
        .error = incoming != 0 && outgoing != 0 && current != 0 ? SSA_OK : SSA_ERROR_MEMORY,
        .instruction = BUSTER_SSA_NONE,
        .rule = "reaching-definition scratch",
    };
    if (check.error == SSA_OK)
    {
        memset(incoming, 0xff, cells * sizeof(uint32_t));
        memset(outgoing, 0xff, cells * sizeof(uint32_t));
        dataflow_liveness_scalar(&work->flow);
    }
    bool changed = check.error == SSA_OK;
    while (changed && check.error == SSA_OK)
    {
        changed = false;
        for (uint32_t order = input->block_count; order != 0; order -= 1)
        {
            const uint32_t block = work->flow.postorder[order - 1];
            if (work->result.reachable[block] != 0)
            {
                for (uint32_t dense = 0; dense < work->promoted_count; dense += 1)
                {
                    const uint64_t cell = (uint64_t)block * work->promoted_count + dense;
                    uint32_t value = work->entry[cell];
                    const bool live = (work->flow.in[(uint64_t)block * work->flow.row_words + dense / 64] & (1ull << (dense % 64))) != 0;
                    if (value == BUSTER_SSA_NONE)
                    {
                        if (block == 0 || !live)
                        {
                            value = ssa_undefined(work, work->local_ids[dense]);
                        }
                        else
                        {
                            for (uint32_t edge = result->predecessor_offsets[block]; edge < result->predecessor_offsets[block + 1]; edge += 1)
                            {
                                const uint32_t predecessor = result->predecessors[edge];
                                const uint32_t other = outgoing[(uint64_t)predecessor * work->promoted_count + dense];
                                if (other != BUSTER_SSA_NONE)
                                {
                                    if (value != BUSTER_SSA_NONE && value != other)
                                    {
                                        ssa_check_fail(&check, input->block_offsets[block], "different reaching values without a phi");
                                    }
                                    value = other;
                                }
                            }
                        }
                    }
                    if (value != BUSTER_SSA_NONE && incoming[cell] == BUSTER_SSA_NONE)
                    {
                        incoming[cell] = value;
                        changed = true;
                    }
                    else if (value != BUSTER_SSA_NONE && incoming[cell] != value)
                    {
                        ssa_check_fail(&check, input->block_offsets[block], "entry value changed");
                    }
                    const uint32_t last = work->last[cell];
                    const uint32_t exit = last == BUSTER_SSA_NONE ? value : result->replacements[last];
                    if (exit != BUSTER_SSA_NONE && outgoing[cell] == BUSTER_SSA_NONE)
                    {
                        outgoing[cell] = exit;
                        changed = true;
                    }
                    else if (exit != BUSTER_SSA_NONE && outgoing[cell] != exit)
                    {
                        ssa_check_fail(&check, input->block_offsets[block], "exit value changed");
                    }
                }
            }
        }
    }
    for (uint32_t phi = 0; check.error == SSA_OK && phi < result->phi_count; phi += 1)
    {
        const uint32_t block = result->phi_blocks[phi];
        const uint32_t dense = work->dense[result->phi_locals[phi]];
        const uint32_t begin = result->predecessor_offsets[block];
        for (uint32_t edge = begin; edge < result->predecessor_offsets[block + 1]; edge += 1)
        {
            const uint32_t predecessor = result->predecessors[edge];
            const uint32_t expected = outgoing[(uint64_t)predecessor * work->promoted_count + dense];
            if (expected == BUSTER_SSA_NONE || expected != result->phi_operands[result->phi_offsets[phi] + edge - begin])
            {
                ssa_check_fail(&check, input->block_offsets[block], "phi operand is not the predecessor's local value");
            }
        }
    }
    for (uint32_t block = 0; check.error == SSA_OK && block < input->block_count; block += 1)
    {
        if (work->result.reachable[block] != 0)
        {
            memcpy(current, incoming + (uint64_t)block * work->promoted_count, (uint64_t)work->promoted_count * sizeof(uint32_t));
            for (uint32_t i = input->block_offsets[block]; i < input->block_offsets[block + 1]; i += 1)
            {
                const uint8_t kind = input->kinds[i];
                if (kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE)
                {
                    const uint32_t dense = work->dense[input->locals[i]];
                    if (dense != BUSTER_SSA_NONE)
                    {
                        if (kind == BUSTER_SSA_WRITE)
                        {
                            current[dense] = result->replacements[input->operands[i]];
                        }
                        else if (current[dense] == BUSTER_SSA_NONE || result->replacements[i] != current[dense])
                        {
                            ssa_check_fail(&check, i, "read is not the current local value");
                        }
                    }
                }
            }
        }
    }
    if (check.error == SSA_OK)
    {
        check.rule = 0;
    }
    return check;
}

static SsaCheck ssa_verify(const SsaInput* input, const SsaResult* result, Arena* scratch)
{
    SsaCheck check = {
        .error = result != 0 && scratch != 0 ? ssa_check_input(input) : SSA_ERROR_INVALID,
        .instruction = BUSTER_SSA_NONE,
        .rule = "input",
    };
    const uint64_t saved = scratch == 0 ? 0 : scratch->length;
    SsaWork work = { 0 };
    if (check.error == SSA_OK)
    {
        check.error = ssa_prepare(&work, input, scratch, false);
    }
    if (check.error == SSA_OK)
    {
        const uint64_t maximum_phis = (uint64_t)input->block_count * work.promoted_count;
        const uint64_t maximum_operands = (uint64_t)work.result.reachable_edge_count * work.promoted_count;
        const bool pointers = result->predecessor_offsets != 0 && result->phi_offsets != 0 && (input->local_count == 0 || result->promoted != 0) && (input->block_count == 0 || result->reachable != 0) && (input->instruction_count == 0 || result->replacements != 0) && (result->phi_count == 0 || (result->phi_blocks != 0 && result->phi_locals != 0)) && (result->operand_count == 0 || result->phi_operands != 0) && (result->reachable_edge_count == 0 || (result->predecessors != 0 && result->predecessor_edges != 0));
        if (!pointers || result->phi_base != work.result.phi_base || result->phi_count > maximum_phis || result->operand_count > maximum_operands || (uint64_t)result->phi_base + result->phi_count >= BUSTER_SSA_NONE || result->reachable_edge_count != work.result.reachable_edge_count)
        {
            ssa_check_fail(&check, BUSTER_SSA_NONE, "output columns or counts");
        }
    }
    if (check.error == SSA_OK)
    {
        const bool locals = input->local_count == 0 || memcmp(result->promoted, work.result.promoted, input->local_count) == 0;
        const bool blocks = input->block_count == 0 || memcmp(result->reachable, work.result.reachable, input->block_count) == 0;
        const bool offsets = memcmp(result->predecessor_offsets, work.result.predecessor_offsets, ((uint64_t)input->block_count + 1) * sizeof(uint32_t)) == 0;
        const uint64_t bytes = (uint64_t)result->reachable_edge_count * sizeof(uint32_t);
        const bool predecessors = bytes == 0 || (memcmp(result->predecessors, work.result.predecessors, bytes) == 0 && memcmp(result->predecessor_edges, work.result.predecessor_edges, bytes) == 0);
        if (!locals || !blocks || !offsets || !predecessors || result->phi_offsets[0] != 0 || result->phi_offsets[result->phi_count] != result->operand_count)
        {
            ssa_check_fail(&check, BUSTER_SSA_NONE, "promotion, reachability or predecessors");
        }
    }
    if (check.error == SSA_OK)
    {
        check.error = ssa_definitions(&work);
    }
    for (uint32_t phi = 0; check.error == SSA_OK && phi < result->phi_count; phi += 1)
    {
        const uint32_t block = result->phi_blocks[phi];
        const uint32_t local = result->phi_locals[phi];
        if (block == 0 || block >= input->block_count || local >= input->local_count || result->reachable[block] == 0 || result->promoted[local] == 0 || result->phi_offsets[phi] > result->phi_offsets[phi + 1] || result->phi_offsets[phi + 1] > result->operand_count)
        {
            ssa_check_fail(&check, BUSTER_SSA_NONE, "phi block, local or operand range");
        }
        else
        {
            const uint64_t cell = (uint64_t)block * work.promoted_count + work.dense[local];
            if (work.entry[cell] != BUSTER_SSA_NONE || result->phi_offsets[phi + 1] - result->phi_offsets[phi] != result->predecessor_offsets[block + 1] - result->predecessor_offsets[block])
            {
                ssa_check_fail(&check, input->block_offsets[block], "duplicate phi or predecessor multiplicity");
            }
            work.entry[cell] = result->phi_base + phi;
        }
    }
    for (uint32_t i = 0; check.error == SSA_OK && i < input->instruction_count; i += 1)
    {
        const uint8_t kind = input->kinds[i];
        if (kind == BUSTER_SSA_WRITE)
        {
            if (result->replacements[i] != BUSTER_SSA_NONE)
            {
                ssa_check_fail(&check, i, "write must not define a value");
            }
        }
        else if (kind == BUSTER_SSA_READ && result->promoted[input->locals[i]] != 0)
        {
            const uint32_t block = work.instruction_blocks[i];
            const uint32_t value = result->replacements[i];
            const bool valid = result->reachable[block] != 0 ? ssa_value_valid(&work, result, value, block, i, input->types[i]) : value == input->instruction_count + input->locals[i];
            if (!valid)
            {
                ssa_check_fail(&check, i, "read replacement type, dominance or canonical value");
            }
        }
        else if (result->replacements[i] != i)
        {
            ssa_check_fail(&check, i, "unpromoted instruction changed");
        }
    }
    for (uint32_t phi = 0; check.error == SSA_OK && phi < result->phi_count; phi += 1)
    {
        const uint32_t block = result->phi_blocks[phi];
        const uint8_t type = input->local_types[result->phi_locals[phi]];
        uint32_t same = BUSTER_SSA_NONE;
        bool distinct = false;
        for (uint32_t operand = result->phi_offsets[phi]; operand < result->phi_offsets[phi + 1]; operand += 1)
        {
            const uint32_t edge = result->predecessor_offsets[block] + operand - result->phi_offsets[phi];
            const uint32_t predecessor = result->predecessors[edge];
            const uint32_t value = result->phi_operands[operand];
            if (!ssa_value_valid(&work, result, value, predecessor, input->block_offsets[predecessor + 1], type))
            {
                ssa_check_fail(&check, input->block_offsets[block], "phi operand type or edge dominance");
            }
            if (value != result->phi_base + phi)
            {
                distinct |= same != BUSTER_SSA_NONE && same != value;
                same = value;
            }
        }
        if (!distinct)
        {
            ssa_check_fail(&check, input->block_offsets[block], "trivial phi");
        }
    }
    if (check.error == SSA_OK)
    {
        check = ssa_verify_equations(&work, result);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, saved);
    }
    return check;
}
