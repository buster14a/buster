// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original synthetic CFG/local-access inputs for the SSA tests and benchmark. These are not lowered C or a
// second IR: VALUE instructions hide a producer's ordinary operations. Include after ssa.c. The generator uses
// a deterministic congruential sequence, keeps the entry free of predecessors, and preserves arena/result on failure.

#define BUSTER_SSA_SHAPE_CHAIN 0
#define BUSTER_SSA_SHAPE_DIAMOND 1
#define BUSTER_SSA_SHAPE_LOOP 2
#define BUSTER_SSA_SHAPE_IRREDUCIBLE 3
#define BUSTER_SSA_SHAPE_RANDOM 4
#define BUSTER_SSA_SHAPE_UNREACHABLE 5
#define BUSTER_SSA_SHAPE_COUNT 6

static __attribute__((unused)) bool ssa_generate(Arena* arena, uint32_t blocks, uint32_t locals, uint32_t shape, uint64_t seed, SsaInput* result);

static uint32_t ssa_program_random(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(*state >> 32);
}

static void ssa_program_edge(uint32_t blocks, uint32_t target, uint32_t targets[4], uint32_t* count)
{
    if (target != 0 && target < blocks)
    {
        targets[*count] = target;
        *count += 1;
    }
}

static uint32_t ssa_program_edges(uint32_t blocks, uint32_t block, uint32_t shape, uint64_t seed, uint32_t targets[4])
{
    uint32_t count = 0;
    uint64_t random = seed + (uint64_t)block * UINT64_C(11400714819323198485);
    if (shape == BUSTER_SSA_SHAPE_DIAMOND)
    {
        if (block == 0)
        {
            ssa_program_edge(blocks, 1, targets, &count);
        }
        else
        {
            const uint32_t position = (block - 1) % 4;
            const uint32_t base = block - position;
            if (position == 0)
            {
                ssa_program_edge(blocks, block + 1, targets, &count);
                ssa_program_edge(blocks, block + 2, targets, &count);
            }
            else
            {
                ssa_program_edge(blocks, position == 3 ? block + 1 : base + 3, targets, &count);
            }
        }
    }
    else if (shape == BUSTER_SSA_SHAPE_IRREDUCIBLE)
    {
        if (block == 0)
        {
            ssa_program_edge(blocks, 1, targets, &count);
            ssa_program_edge(blocks, 2, targets, &count);
        }
        else
        {
            const uint32_t position = (block - 1) % 4;
            const uint32_t base = block - position;
            if (position < 2)
            {
                ssa_program_edge(blocks, base + 2, targets, &count);
            }
            else if (position == 2)
            {
                ssa_program_edge(blocks, base, targets, &count);
                ssa_program_edge(blocks, base + 1, targets, &count);
                ssa_program_edge(blocks, base + 3, targets, &count);
            }
            else
            {
                ssa_program_edge(blocks, block + 1, targets, &count);
                ssa_program_edge(blocks, block + 2, targets, &count);
            }
        }
    }
    else if (shape == BUSTER_SSA_SHAPE_UNREACHABLE)
    {
        const uint32_t middle = blocks / 2 + (blocks == 1);
        if (block + 1 < middle || (block >= middle && block + 1 < blocks))
        {
            ssa_program_edge(blocks, block + 1, targets, &count);
        }
        if (block >= middle && blocks > 2)
        {
            ssa_program_edge(blocks, middle, targets, &count);
            if (block == middle)
            {
                ssa_program_edge(blocks, 1, targets, &count);
            }
        }
    }
    else
    {
        ssa_program_edge(blocks, block + 1, targets, &count);
        if (shape == BUSTER_SSA_SHAPE_LOOP && block >= 3 && block % 4 == 3)
        {
            ssa_program_edge(blocks, block - 2, targets, &count);
        }
        if (shape == BUSTER_SSA_SHAPE_RANDOM && block != 0 && blocks > 1)
        {
            ssa_program_edge(blocks, 1 + ssa_program_random(&random) % (blocks - 1), targets, &count);
            if (block % 5 == 0)
            {
                // Keep repeated edges: a switch can have several cases with the same target.
                ssa_program_edge(blocks, block + 1, targets, &count);
            }
        }
    }
    return count;
}

static bool ssa_generate(Arena* arena, uint32_t blocks, uint32_t locals, uint32_t shape, uint64_t seed, SsaInput* result)
{
    const uint64_t saved_length = arena->length;
    const uint32_t rounds = locals < 8 ? locals : 8;
    const uint64_t initial = blocks != 0 ? ((uint64_t)locals + 1) / 2 * 2 : 0;
    const uint64_t instruction_count = initial + (uint64_t)blocks * rounds * 4 + (blocks != 0 && locals > 6);
    uint64_t edge_count = 0;
    bool success = shape < BUSTER_SSA_SHAPE_COUNT && (blocks != 0 || locals == 0) && instruction_count <= UINT32_MAX;
    for (uint32_t block = 0; success && block < blocks; block += 1)
    {
        uint32_t targets[4];
        edge_count += ssa_program_edges(blocks, block, shape, seed, targets);
        success = edge_count <= UINT32_MAX;
    }
    SsaInput candidate = {
        .block_count = blocks,
        .instruction_count = (uint32_t)instruction_count,
        .local_count = locals,
        .edge_count = (uint32_t)edge_count,
    };
    uint32_t* block_offsets = success ? arena_push_array(arena, (uint64_t)blocks + 1, sizeof(uint32_t), 4) : 0;
    uint32_t* successor_offsets = success ? arena_push_array(arena, (uint64_t)blocks + 1, sizeof(uint32_t), 4) : 0;
    uint32_t* successors = success && edge_count != 0 ? arena_push_array(arena, edge_count, sizeof(uint32_t), 4) : 0;
    uint8_t* kinds = success && instruction_count != 0 ? arena_push_array(arena, instruction_count, 1, 1) : 0;
    uint8_t* types = success && instruction_count != 0 ? arena_push_array(arena, instruction_count, 1, 1) : 0;
    uint32_t* local_ids = success && instruction_count != 0 ? arena_push_array(arena, instruction_count, sizeof(uint32_t), 4) : 0;
    uint32_t* operands = success && instruction_count != 0 ? arena_push_array(arena, instruction_count, sizeof(uint32_t), 4) : 0;
    uint8_t* local_types = success && locals != 0 ? arena_push_array(arena, locals, 1, 1) : 0;
    uint8_t* local_flags = success && locals != 0 ? arena_push_array(arena, locals, 1, 1) : 0;
    success = success && block_offsets != 0 && successor_offsets != 0 && (edge_count == 0 || successors != 0) &&
              (instruction_count == 0 || (kinds != 0 && types != 0 && local_ids != 0 && operands != 0)) &&
              (locals == 0 || (local_types != 0 && local_flags != 0));
    if (success)
    {
        candidate.block_offsets = block_offsets;
        candidate.successor_offsets = successor_offsets;
        candidate.successors = successors;
        candidate.kinds = kinds;
        candidate.types = types;
        candidate.locals = local_ids;
        candidate.operands = operands;
        candidate.local_types = local_types;
        candidate.local_flags = local_flags;
        for (uint32_t local = 0; local < locals; local += 1)
        {
            local_types[local] = 1 + local % (BUSTER_SSA_TYPE_PTR);
            local_flags[local] = local % 17 == 16 ? BUSTER_SSA_LOCAL_VOLATILE : local % 19 == 18 ? BUSTER_SSA_LOCAL_ATOMIC :
                                 local % 23 == 22 ? BUSTER_SSA_LOCAL_ADDRESS_TAKEN : 0;
            if (local % 29 == 28)
            {
                local_types[local] = BUSTER_SSA_TYPE_AGGREGATE;
            }
        }
        uint32_t edge = 0;
        uint32_t instruction = 0;
        uint64_t random = seed;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            block_offsets[block] = instruction;
            successor_offsets[block] = edge;
            uint32_t targets[4];
            const uint32_t edges = ssa_program_edges(blocks, block, shape, seed, targets);
            for (uint32_t e = 0; e < edges; e += 1)
            {
                successors[edge++] = targets[e];
            }
            if (block == 0)
            {
                for (uint32_t local = 0; local < locals; local += 2)
                {
                    kinds[instruction] = BUSTER_SSA_VALUE;
                    types[instruction] = local_types[local];
                    local_ids[instruction] = BUSTER_SSA_NONE;
                    operands[instruction] = BUSTER_SSA_NONE;
                    instruction += 1;
                    kinds[instruction] = BUSTER_SSA_WRITE;
                    types[instruction] = local_types[local];
                    local_ids[instruction] = local;
                    operands[instruction] = instruction - 1;
                    instruction += 1;
                }
            }
            for (uint32_t round = 0; round < rounds; round += 1)
            {
                const uint32_t word = ssa_program_random(&random);
                const uint32_t local = word % locals;
                const uint32_t begin = instruction;
                for (uint32_t part = 0; part < 4; part += 1)
                {
                    kinds[instruction] = part == 0 || part == 3 ? BUSTER_SSA_READ : part == 1 ? BUSTER_SSA_VALUE : BUSTER_SSA_WRITE;
                    types[instruction] = local_types[local];
                    local_ids[instruction] = part == 1 ? BUSTER_SSA_NONE : local;
                    operands[instruction] = part == 2 ? begin + ((word >> 8) & 1) : BUSTER_SSA_NONE;
                    instruction += 1;
                }
            }
            if (block + 1 == blocks && locals > 6)
            {
                kinds[instruction] = BUSTER_SSA_ADDRESS;
                types[instruction] = BUSTER_SSA_TYPE_PTR;
                local_ids[instruction] = locals - 1;
                operands[instruction] = BUSTER_SSA_NONE;
                instruction += 1;
            }
        }
        block_offsets[blocks] = instruction;
        successor_offsets[blocks] = edge;
        *result = candidate;
    }
    else
    {
        arena_pop_to(arena, saved_length);
    }
    return success;
}
