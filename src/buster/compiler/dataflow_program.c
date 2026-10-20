// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic CFGs and use/def rows for the dataflow tests and benchmark, included after lane.c and dataflow.c.
// dataflow_generate owns its columns in the caller's arena, preserves the arena and result on failure, and keeps
// padding bits zero. Version 2 mixes seeds before its congruential stream and adds a connected dispatch-loop shape.
// Inputs are only version, seed, shape, block count and value count; these are synthetic rows, not an IR model.
//
// Seed mixing follows SplitMix64's public-domain arithmetic by Sebastiano Vigna (2015); its dedication, permission
// and disclaimer are retained in LICENSES/SplitMix64.txt. The graph and row generator are original to this module.

#define BUSTER_DATAFLOW_GENERATOR_VERSION 2u
#define BUSTER_DATAFLOW_GENERATOR_EDGES 4
#define BUSTER_DATAFLOW_GENERATOR_REGION 64
#define BUSTER_DATAFLOW_DISPATCH_ARM_BLOCKS 8

#define BUSTER_DATAFLOW_SHAPE_CHAIN 0
#define BUSTER_DATAFLOW_SHAPE_DIAMOND 1
#define BUSTER_DATAFLOW_SHAPE_SELF_LOOP 2
#define BUSTER_DATAFLOW_SHAPE_NESTED_LOOP 3
#define BUSTER_DATAFLOW_SHAPE_IRREDUCIBLE 4
#define BUSTER_DATAFLOW_SHAPE_MULTI_EXIT 5
#define BUSTER_DATAFLOW_SHAPE_RANDOM 6
#define BUSTER_DATAFLOW_SHAPE_DISPATCH 7
#define BUSTER_DATAFLOW_SHAPE_COUNT 8

static __attribute__((unused)) bool dataflow_generate(Arena* arena, uint32_t blocks, uint32_t values, uint32_t shape, uint64_t seed, Dataflow* result);

static uint64_t dataflow_generator_seed(uint64_t seed)
{
    uint64_t mixed = seed + 0x9E3779B97F4A7C15ull;
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;
    return mixed ^ (mixed >> 31);
}

// A small, deterministic congruential stream for test topology and bit locations, not for cryptographic use.
static uint32_t dataflow_generator_random(uint64_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return (uint32_t)(*state >> 32);
}

static void dataflow_generator_edge(uint32_t block_count, uint32_t target, uint32_t* targets, uint32_t* count)
{
    bool present = false;
    for (uint32_t index = 0; index < *count; index += 1)
    {
        present |= targets[index] == target;
    }
    if (target < block_count && !present)
    {
        assert(*count < BUSTER_DATAFLOW_GENERATOR_EDGES);
        targets[*count] = target;
        *count += 1;
    }
}

// Block 0 is a switch-like dispatcher with an edge to every arm, plus the exit (block 1). It is intentionally not
// limited to the four-edge scratch array used for ordinary blocks. A null output counts its edges before allocation.
static uint32_t dataflow_generator_dispatch_edges(uint32_t blocks, uint32_t* targets)
{
    uint32_t count = 0;
    if (blocks != 0)
    {
        if (targets != 0)
        {
            targets[count] = blocks == 1 ? 0 : 1;
        }
        count += 1;
        for (uint32_t arm = 2; arm < blocks; arm += BUSTER_DATAFLOW_DISPATCH_ARM_BLOCKS)
        {
            if (targets != 0)
            {
                targets[count] = arm;
            }
            count += 1;
        }
    }
    return count;
}

static uint32_t dataflow_generator_successors(uint32_t blocks, uint32_t block, uint32_t shape, uint64_t* state, uint32_t* targets)
{
    uint32_t count = 0;
    switch (shape)
    {
        case BUSTER_DATAFLOW_SHAPE_CHAIN:
        {
            dataflow_generator_edge(blocks, block + 1, targets, &count);
        } break;

        case BUSTER_DATAFLOW_SHAPE_DIAMOND:
        {
            const uint32_t position = block % 4;
            const uint32_t base = block - position;
            if (position == 0)
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
                dataflow_generator_edge(blocks, block + 2, targets, &count);
            }
            else
            {
                const uint32_t target = position == 3 ? block + 1 : base + 3;
                dataflow_generator_edge(blocks, target, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_SELF_LOOP:
        {
            dataflow_generator_edge(blocks, block + 1, targets, &count);
            if (block % 3 == 0)
            {
                dataflow_generator_edge(blocks, block, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_NESTED_LOOP:
        {
            dataflow_generator_edge(blocks, block + 1, targets, &count);
            if (block % 8 == 6)
            {
                dataflow_generator_edge(blocks, block - 4, targets, &count);
            }
            if (block % 32 == 30)
            {
                dataflow_generator_edge(blocks, block - 29, targets, &count);
            }
            if (blocks < 8 && block + 1 == blocks)
            {
                dataflow_generator_edge(blocks, 0, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_IRREDUCIBLE:
        {
            // Both nodes of the cycle have an edge entering from outside it: neither dominates the other.
            const uint32_t position = block % 8;
            const uint32_t base = block - position;
            if (position == 0)
            {
                dataflow_generator_edge(blocks, base + 1, targets, &count);
                dataflow_generator_edge(blocks, base + 2, targets, &count);
            }
            else if (position == 1)
            {
                dataflow_generator_edge(blocks, base + 2, targets, &count);
                dataflow_generator_edge(blocks, base + 3, targets, &count);
            }
            else if (position == 2)
            {
                dataflow_generator_edge(blocks, base + 1, targets, &count);
                dataflow_generator_edge(blocks, base + 3, targets, &count);
            }
            else
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_MULTI_EXIT:
        {
            if (block % 7 != 4)
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
                if (block % 7 == 0)
                {
                    dataflow_generator_edge(blocks, block + 2, targets, &count);
                }
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_RANDOM:
        {
            // Local components bound live regions and also exercise postorder's disconnected roots. Making every
            // edge global would quickly make one large SCC with almost every value live almost everywhere.
            const uint32_t base = block - block % BUSTER_DATAFLOW_GENERATOR_REGION;
            const uint32_t left = blocks - base;
            const uint32_t size = left < BUSTER_DATAFLOW_GENERATOR_REGION ? left : BUSTER_DATAFLOW_GENERATOR_REGION;
            const uint32_t choice = dataflow_generator_random(state);
            if (block + 1 < base + size && block % 11 != 7)
            {
                dataflow_generator_edge(blocks, block + 1, targets, &count);
                dataflow_generator_edge(blocks, base + choice % size, targets, &count);
                if ((choice & 3) == 0)
                {
                    const uint32_t target = base + dataflow_generator_random(state) % size;
                    dataflow_generator_edge(blocks, target, targets, &count);
                }
            }
        } break;

        case BUSTER_DATAFLOW_SHAPE_DISPATCH:
        {
            if (block >= 2)
            {
                const uint32_t position = (block - 2) % BUSTER_DATAFLOW_DISPATCH_ARM_BLOCKS;
                if (position + 1 < BUSTER_DATAFLOW_DISPATCH_ARM_BLOCKS && block + 1 < blocks)
                {
                    dataflow_generator_edge(blocks, block + 1, targets, &count);
                    const uint32_t choice = dataflow_generator_random(state);
                    if ((choice & 3) == 0)
                    {
                        dataflow_generator_edge(blocks, 1, targets, &count);
                    }
                    if (position == 5)
                    {
                        dataflow_generator_edge(blocks, block - 3, targets, &count);
                    }
                }
                else
                {
                    dataflow_generator_edge(blocks, 0, targets, &count);
                    dataflow_generator_edge(blocks, 1, targets, &count);
                }
            }
        } break;
    }
    return count;
}

static bool dataflow_generate(Arena* arena, uint32_t blocks, uint32_t values, uint32_t shape, uint64_t seed, Dataflow* result)
{
    const uint64_t saved_length = arena->length;
    bool valid = shape < BUSTER_DATAFLOW_SHAPE_COUNT && blocks <= UINT32_MAX / BUSTER_DATAFLOW_GENERATOR_EDGES;
    uint32_t edges = 0;
    const uint64_t mixed_seed = dataflow_generator_seed(seed);
    uint64_t state = mixed_seed;
    for (uint32_t block = 0; valid && block < blocks; block += 1)
    {
        uint32_t targets[BUSTER_DATAFLOW_GENERATOR_EDGES];
        edges += shape == BUSTER_DATAFLOW_SHAPE_DISPATCH && block == 0 ? dataflow_generator_dispatch_edges(blocks, 0) : dataflow_generator_successors(blocks, block, shape, &state, targets);
    }

    Dataflow generated = { 0 };
    if (valid)
    {
        valid = dataflow_create(arena, blocks, values, edges, &generated);
    }
    if (valid)
    {
        state = mixed_seed;
        uint32_t edge = 0;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            uint32_t targets[BUSTER_DATAFLOW_GENERATOR_EDGES];
            generated.successor_offsets[block] = edge;
            if (shape == BUSTER_DATAFLOW_SHAPE_DISPATCH && block == 0)
            {
                edge += dataflow_generator_dispatch_edges(blocks, generated.successors + edge);
            }
            else
            {
                const uint32_t count = dataflow_generator_successors(blocks, block, shape, &state, targets);
                for (uint32_t index = 0; index < count; index += 1)
                {
                    generated.successors[edge] = targets[index];
                    edge += 1;
                }
            }
        }
        generated.successor_offsets[blocks] = edge;

        // Every value has one definition and one to four uses. Dispatch uses span arbitrary arms; the other shapes
        // keep uses near their definitions, leaving loops, bypassed definitions and disconnected uses for the solver.
        // Setting individual valid bits after zeroing the rows preserves all padding, including unused words.
        const uint64_t bytes = (uint64_t)blocks * generated.row_words * sizeof(uint64_t);
        if (bytes != 0)
        {
            memset(generated.use, 0, bytes);
            memset(generated.def, 0, bytes);
        }
        for (uint32_t value = 0; blocks != 0 && value < values; value += 1)
        {
            const uint64_t bit = 1ull << (value % 64);
            const uint64_t word = value / 64;
            if (shape == BUSTER_DATAFLOW_SHAPE_DISPATCH && blocks > 2)
            {
                // One eighth of the values can survive between arbitrary arms. The rest are freshly defined at
                // the dispatcher and used in one to four unrelated arms: they stop propagating at block 0.
                // This keeps long-lived state and sparse per-arm state without making all values live everywhere.
                const bool persistent = value % 8 == 0;
                const uint32_t definition = persistent ? 2 + dataflow_generator_random(&state) % (blocks - 2) : 0;
                generated.def[(uint64_t)definition * generated.row_words + word] |= bit;
                const uint32_t uses = 1 + dataflow_generator_random(&state) % 4;
                for (uint32_t use = 0; use < uses; use += 1)
                {
                    const uint32_t user = 2 + dataflow_generator_random(&state) % (blocks - 2);
                    generated.use[(uint64_t)user * generated.row_words + word] |= bit;
                }
            }
            else
            {
                const uint32_t anchor = (uint32_t)((uint64_t)value * blocks / values);
                const uint32_t base = anchor - anchor % BUSTER_DATAFLOW_GENERATOR_REGION;
                const uint32_t left = blocks - base;
                const uint32_t size = left < BUSTER_DATAFLOW_GENERATOR_REGION ? left : BUSTER_DATAFLOW_GENERATOR_REGION;
                const uint32_t definition = base + dataflow_generator_random(&state) % size;
                const uint32_t uses = 1 + dataflow_generator_random(&state) % 4;
                generated.def[(uint64_t)definition * generated.row_words + word] |= bit;
                const uint32_t following = base + size - definition;
                const uint32_t span = following < 17 ? following : 17;
                for (uint32_t use = 0; use < uses; use += 1)
                {
                    const uint32_t user = definition + dataflow_generator_random(&state) % span;
                    generated.use[(uint64_t)user * generated.row_words + word] |= bit;
                }
            }
        }
        valid = dataflow_postorder(&generated, arena);
    }
    if (valid)
    {
        *result = generated;
    }
    else
    {
        arena_pop_to(arena, saved_length);
    }
    return valid;
}
