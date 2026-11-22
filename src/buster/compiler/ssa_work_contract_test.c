// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded SSA storage regression (#1353, #2041). Included only by tools/ci/work-contracts-ssa.c after the production
// modules with test-only allocation wrappers. N independent scoped loops have 3N+2 blocks, 8N instructions and
// 4N+1 edges. Each local must keep one two-input phi; other reads have an independently known reaching value.
// These fixtures exercise the public selector, not a forced bounded implementation. They constrain total requested
// payload bytes for construction and for the independent verifier separately, with no clock, RSS or object hash.

#if defined(BUSTER_EXECUTABLE_UNIT) || defined(BUSTER_UNIT)
#error "SSA work fixtures belong only to the standalone contract executable"
#endif

#include "work_contract.h"

// This shape is the scalable form of test-ssa's independent scoped loops: a local's lifetime starts at its own init
// block, its header joins the initial value with a new value from the body, and the body has a backedge. The next
// local starts only on the header's exit. Every block is reachable and every local needs actual promotion work.
static bool ssa_work_fixture(Arena* arena, uint32_t locals, SsaInput* result)
{
    const uint32_t blocks = 3 * locals + 2;
    const uint32_t instructions = 8 * locals;
    const uint32_t edges = 4 * locals + 1;
    uint32_t* block_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)blocks + 1);
    uint32_t* successor_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)blocks + 1);
    uint32_t* successors = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, edges);
    uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, instructions);
    uint8_t* types = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, instructions);
    uint32_t* accesses = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, instructions);
    uint32_t* operands = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, instructions);
    uint8_t* local_types = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, locals);
    uint8_t* local_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, locals);
    const bool ready = block_offsets != NULL && successor_offsets != NULL && successors != NULL && kinds != NULL && types != NULL && accesses != NULL && operands != NULL && local_types != NULL && local_flags != NULL;
    if (ready)
    {
        memset(types, BUSTER_SSA_TYPE_I64, instructions);
        memset(operands, 0xff, (uint64_t)instructions * sizeof(uint32_t));
        memset(local_types, BUSTER_SSA_TYPE_I64, locals);
        memset(local_flags, 0, locals);
        block_offsets[0] = 0;
        successor_offsets[0] = 0;
        successors[0] = 1;
        for (uint32_t local = 0; local < locals; local += 1)
        {
            const uint32_t block = 3 * local + 1;
            const uint32_t first = 8 * local;
            const uint32_t edge = 4 * local + 1;
            block_offsets[block] = first;
            block_offsets[block + 1] = first + 3;
            block_offsets[block + 2] = first + 4;
            successor_offsets[block] = edge;
            successor_offsets[block + 1] = edge + 1;
            successor_offsets[block + 2] = edge + 3;
            successors[edge] = block + 1;
            successors[edge + 1] = block + 2;
            successors[edge + 2] = block + 3;
            successors[edge + 3] = block + 1;
            const uint8_t sequence[] = {
                BUSTER_SSA_WRITE,
                BUSTER_SSA_VALUE,
                BUSTER_SSA_WRITE,
                BUSTER_SSA_READ,
                BUSTER_SSA_READ,
                BUSTER_SSA_VALUE,
                BUSTER_SSA_WRITE,
                BUSTER_SSA_READ,
            };
            for (uint32_t offset = 0; offset < sizeof(sequence); offset += 1)
            {
                kinds[first + offset] = sequence[offset];
                accesses[first + offset] = sequence[offset] == BUSTER_SSA_VALUE ? BUSTER_SSA_NONE : local;
            }
            operands[first] = instructions + local;
            operands[first + 2] = first + 1;
            operands[first + 6] = first + 5;
        }
        block_offsets[blocks - 1] = instructions;
        block_offsets[blocks] = instructions;
        successor_offsets[blocks - 1] = edges;
        successor_offsets[blocks] = edges;
        *result = (SsaInput){
            .block_count = blocks,
            .instruction_count = instructions,
            .local_count = locals,
            .edge_count = edges,
            .block_offsets = block_offsets,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .kinds = kinds,
            .types = types,
            .locals = accesses,
            .operands = operands,
            .local_types = local_types,
            .local_flags = local_flags,
        };
    }
    return ready;
}

static uint64_t ssa_work_hash_bytes(uint64_t hash, const void* data, uint64_t count)
{
    const uint8_t* bytes = data;
    for (uint64_t i = 0; i < count; i += 1)
    {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

// Only immutable input columns are hashed. Result correctness below is semantic and permits different phi IDs.
static uint64_t ssa_work_input_hash(const SsaInput* input)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = ssa_work_hash_bytes(hash, input->block_offsets, ((uint64_t)input->block_count + 1) * sizeof(uint32_t));
    hash = ssa_work_hash_bytes(hash, input->successor_offsets, ((uint64_t)input->block_count + 1) * sizeof(uint32_t));
    hash = ssa_work_hash_bytes(hash, input->successors, (uint64_t)input->edge_count * sizeof(uint32_t));
    hash = ssa_work_hash_bytes(hash, input->kinds, input->instruction_count);
    hash = ssa_work_hash_bytes(hash, input->types, input->instruction_count);
    hash = ssa_work_hash_bytes(hash, input->locals, (uint64_t)input->instruction_count * sizeof(uint32_t));
    hash = ssa_work_hash_bytes(hash, input->operands, (uint64_t)input->instruction_count * sizeof(uint32_t));
    hash = ssa_work_hash_bytes(hash, input->local_types, input->local_count);
    hash = ssa_work_hash_bytes(hash, input->local_flags, input->local_count);
    return hash;
}

static bool ssa_work_semantics(const SsaInput* input, const SsaResult* result)
{
    const uint32_t locals = input->local_count;
    const uint32_t blocks = input->block_count;
    bool valid = result->promoted != NULL && result->reachable != NULL && result->replacements != NULL && result->phi_blocks != NULL && result->phi_locals != NULL && result->phi_offsets != NULL && result->phi_operands != NULL && result->predecessor_offsets != NULL && result->predecessors != NULL && result->predecessor_edges != NULL;
    valid = valid && result->phi_base == input->instruction_count + locals && result->phi_count == locals && result->operand_count == 2 * locals && result->reachable_edge_count == input->edge_count;
    for (uint32_t block = 0; valid && block < blocks; block += 1)
    {
        valid = result->reachable[block] != 0;
    }
    for (uint32_t local = 0; valid && local < locals; local += 1)
    {
        const uint32_t first = 8 * local;
        const uint32_t value = result->replacements[first + 3];
        valid = result->promoted[local] != 0 && value >= result->phi_base && (uint64_t)value < (uint64_t)result->phi_base + result->phi_count;
        valid = valid && result->replacements[first] == BUSTER_SSA_NONE && result->replacements[first + 1] == first + 1 && result->replacements[first + 2] == BUSTER_SSA_NONE;
        valid = valid && result->replacements[first + 4] == value && result->replacements[first + 5] == first + 5 && result->replacements[first + 6] == BUSTER_SSA_NONE && result->replacements[first + 7] == first + 5;
        if (valid)
        {
            const uint32_t phi = value - result->phi_base;
            const uint32_t begin = result->phi_offsets[phi];
            valid = result->phi_blocks[phi] == 3 * local + 2 && result->phi_locals[phi] == local && begin <= result->operand_count - 2 && result->phi_offsets[phi + 1] == begin + 2;
            valid = valid && result->phi_operands[begin] == first + 1 && result->phi_operands[begin + 1] == first + 5;
        }
    }
    return valid;
}

static bool ssa_work_case(uint32_t locals)
{
    Arena fixture = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(16) << 20);
    Arena scratch = arena_reserve(UINT64_C(256) << 20);
    SsaInput input = { 0 };
    SsaResult result = { 0 };
    const bool ready = fixture.base != NULL && output.base != NULL && scratch.base != NULL && ssa_work_fixture(&fixture, locals, &input);
    const uint64_t original = ready ? ssa_work_input_hash(&input) : 0;
    ssa_work_requested_bytes = 0;
    ssa_work_request_overflow = false;
    const SsaError error = ready ? ssa_build(&input, &output, &scratch, &result) : SSA_ERROR_MEMORY;
    const uint64_t construction_bytes = ssa_work_requested_bytes;
    bool semantic = error == SSA_OK && !ssa_work_request_overflow && construction_bytes != 0 && scratch.length == 0 && ssa_work_semantics(&input, &result);
    semantic = semantic && ssa_work_input_hash(&input) == original;
    ssa_work_requested_bytes = 0;
    ssa_work_request_overflow = false;
    const SsaCheck check = semantic ? ssa_verify(&input, &result, &scratch) : (SsaCheck){ .error = SSA_ERROR_INVALID };
    const uint64_t verification_bytes = ssa_work_requested_bytes;
    semantic = semantic && check.error == SSA_OK && !ssa_work_request_overflow && verification_bytes != 0 && scratch.length == 0 && ssa_work_input_hash(&input) == original;

    // An operation-specific linear allowance, not an exact allocation layout: 4 KiB per independent loop plus
    // 64 KiB fixed slack for topology, access columns, per-local reused workspace, retained phis and cleanup.
    // There are only 3N+2 blocks, 8N instructions, 4N+1 edges and N two-input phis; a B*L table is not required.
    const uint64_t bound = UINT64_C(4096) * locals + (UINT64_C(64) << 10);
    const WorkContract construction = {
        .fixture = "ssa-scoped-loops",
        .metric = "construction-requested-bytes",
        .input = locals,
        .actual = construction_bytes,
        .bound = bound,
        .semantic = semantic,
    };
    const WorkContract verification = {
        .fixture = "ssa-scoped-loops",
        .metric = "verification-requested-bytes",
        .input = locals,
        .actual = verification_bytes,
        .bound = bound,
        .semantic = semantic,
    };
    bool passed = work_contract_report(stdout, &construction);
    passed = work_contract_report(stdout, &verification) && passed;
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&fixture);
    return passed;
}

static bool ssa_work_contracts(void)
{
    const uint32_t sizes[] = { 256, 512, 1024 };
    bool passed = true;
    for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i += 1)
    {
        passed = ssa_work_case(sizes[i]) && passed;
    }
    return passed;
}
