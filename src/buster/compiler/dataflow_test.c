// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// main.c includes this file after dataflow.c; run_dataflow_tests implements test-dataflow.
// The generated cases compare both kernels and their iteration counts, equations, padding and repeatability.
// dataflow_test_reachability independently derives liveness one value at a time by finding a use before a def.
// dataflow_test_sampled_reachability checks selected values at every block of large graphs by backward reachability.
// dataflow_test_repeated_edges compares a handwritten multigraph, including repeated self-edges, with its simple CFG.
// dataflow_test_guards moves each whole matrix to a protected page boundary, including inaccessible zero-size rows.
// dataflow_test_limits checks failure rollback and malformed successor columns. All fixtures are original C data.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_DATAFLOW_TEST_DEFAULT_COUNT 24
#define BUSTER_DATAFLOW_TEST_ARENA_BYTES (32ull << 20)
#define BUSTER_DATAFLOW_TEST_REPORTED 8

typedef struct DataflowTestTally DataflowTestTally;
struct DataflowTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t seed;
    uint32_t blocks;
    uint32_t values;
    uint32_t shape;
};

static const uint32_t dataflow_test_values[] = {
    0,
    1,
    63,
    64,
    65,
    511,
    512,
    513,
    1024,
    1025,
};

static const uint32_t dataflow_test_blocks[] = {
    0,
    1,
    2,
    3,
    8,
    17,
    65,
    257,
    2049,
};

static void dataflow_test_expect(DataflowTestTally* tally, bool condition, const char* description)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_DATAFLOW_TEST_REPORTED)
        {
            fprintf(stderr, "dataflow: %s (seed=%" PRIu64 ", blocks=%u, values=%u, shape=%u)\n",
                description, tally->seed, tally->blocks, tally->values, tally->shape);
        }
        tally->failures += 1;
    }
}

static bool dataflow_test_rows_equal(const Dataflow* a, const Dataflow* b)
{
    const uint64_t bytes = (uint64_t)a->block_count * a->row_words * sizeof(uint64_t);
    const bool result = bytes == 0 || (memcmp(a->in, b->in, bytes) == 0 && memcmp(a->out, b->out, bytes) == 0);
    return result;
}

static bool dataflow_test_inputs_equal(const Dataflow* a, const Dataflow* b)
{
    const uint64_t bytes = (uint64_t)a->block_count * a->row_words * sizeof(uint64_t);
    bool result = a->block_count == b->block_count && a->value_count == b->value_count;
    result = result && a->edge_count == b->edge_count && a->row_words == b->row_words;
    if (result)
    {
        result = memcmp(a->successor_offsets, b->successor_offsets, ((uint64_t)a->block_count + 1) * sizeof(uint32_t)) == 0;
        if (a->edge_count != 0)
        {
            result = result && memcmp(a->successors, b->successors, (uint64_t)a->edge_count * sizeof(uint32_t)) == 0;
        }
        if (a->block_count != 0)
        {
            result = result && memcmp(a->postorder, b->postorder, (uint64_t)a->block_count * sizeof(uint32_t)) == 0;
        }
        if (bytes != 0)
        {
            result = result && memcmp(a->use, b->use, bytes) == 0 && memcmp(a->def, b->def, bytes) == 0;
        }
    }
    return result;
}

static bool dataflow_test_equations(const Dataflow* graph)
{
    bool result = true;
    for (uint32_t block = 0; block < graph->block_count; block += 1)
    {
        for (uint64_t word = 0; word < graph->row_words; word += 1)
        {
            const uint64_t index = (uint64_t)block * graph->row_words + word;
            uint64_t expected_out = 0;
            for (uint32_t edge = graph->successor_offsets[block]; edge < graph->successor_offsets[block + 1]; edge += 1)
            {
                const uint64_t successor_index = (uint64_t)graph->successors[edge] * graph->row_words + word;
                expected_out |= graph->in[successor_index];
            }
            const uint64_t expected_in = graph->use[index] | (expected_out & ~graph->def[index]);
            result = result && graph->out[index] == expected_out && graph->in[index] == expected_in;
        }
    }
    return result;
}

static bool dataflow_test_padding(const Dataflow* graph)
{
    bool result = true;
    for (uint32_t block = 0; block < graph->block_count; block += 1)
    {
        for (uint64_t word = 0; word < graph->row_words; word += 1)
        {
            const uint64_t first_value = word * 64;
            uint64_t mask = 0;
            if (first_value < graph->value_count)
            {
                const uint64_t remaining = graph->value_count - first_value;
                mask = remaining < 64 ? (1ull << remaining) - 1 : UINT64_MAX;
            }
            const uint64_t index = (uint64_t)block * graph->row_words + word;
            const uint64_t present = graph->use[index] | graph->def[index] | graph->in[index] | graph->out[index];
            result = result && (present & ~mask) == 0;
        }
    }
    return result;
}

// A value is live at a block's entry iff some path reaches a use before a definition. Unlike either solver, this
// checks individual values by graph reachability, without a fixpoint or the postorder. It proves the least solution,
// including cycles with no uses, for which the equations alone also admit spurious all-live solutions.
static bool dataflow_test_reachability(const Dataflow* graph, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint8_t* visited = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->block_count);
    uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->block_count);
    bool result = graph->block_count == 0 || (visited != 0 && stack != 0);
    for (uint32_t origin = 0; result && origin < graph->block_count; origin += 1)
    {
        for (uint32_t value = 0; result && value < graph->value_count; value += 1)
        {
            memset(visited, 0, graph->block_count);
            const uint64_t bit = 1ull << (value % 64);
            const uint64_t word = value / 64;
            uint32_t depth = 1;
            stack[0] = origin;
            visited[origin] = 1;
            bool live = false;
            while (depth != 0 && !live)
            {
                depth -= 1;
                const uint32_t block = stack[depth];
                const uint64_t index = (uint64_t)block * graph->row_words + word;
                if ((graph->use[index] & bit) != 0)
                {
                    live = true;
                }
                else if ((graph->def[index] & bit) == 0)
                {
                    for (uint32_t edge = graph->successor_offsets[block]; edge < graph->successor_offsets[block + 1]; edge += 1)
                    {
                        const uint32_t successor = graph->successors[edge];
                        if (visited[successor] == 0)
                        {
                            visited[successor] = 1;
                            stack[depth] = successor;
                            depth += 1;
                        }
                    }
                }
            }
            const uint64_t origin_index = (uint64_t)origin * graph->row_words + word;
            result = live == ((graph->in[origin_index] & bit) != 0);
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}

// Reachability from every use in the reversed CFG gives the same path property for all origins at once. Stop
// at a predecessor that defines the value, unless it is itself a use (already seeded). This does not use in/out
// equations, solver order or iteration counts. Selected word/vector boundary values keep large cases inexpensive.
static bool dataflow_test_sampled_reachability(const Dataflow* graph, Arena* scratch, uint64_t seed)
{
    const uint64_t mark = scratch->length;
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)graph->block_count + 1);
    uint32_t* predecessors = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->edge_count);
    uint32_t* cursors = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->block_count);
    uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->block_count);
    uint8_t* live = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->block_count);
    bool result = offsets != 0 && (graph->edge_count == 0 || predecessors != 0);
    result = result && (graph->block_count == 0 || (cursors != 0 && stack != 0 && live != 0));
    const uint32_t boundary_values[] = {
        0,
        1,
        63,
        64,
        65,
        511,
        512,
        513,
        1023,
        1024,
        1025,
    };
    uint32_t samples[BUSTER_ARRAY_LENGTH(boundary_values) + 3];
    uint32_t sample_count = 0;
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(boundary_values); i += 1)
    {
        if (boundary_values[i] < graph->value_count)
        {
            samples[sample_count] = boundary_values[i];
            sample_count += 1;
        }
    }
    if (graph->value_count != 0)
    {
        const uint32_t extra_values[] = {
            graph->value_count - 1,
            graph->value_count / 2,
            (uint32_t)(seed % graph->value_count),
        };
        for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(extra_values); i += 1)
        {
            bool duplicate = false;
            for (uint32_t sample = 0; sample < sample_count; sample += 1)
            {
                duplicate = duplicate || samples[sample] == extra_values[i];
            }
            if (!duplicate)
            {
                samples[sample_count] = extra_values[i];
                sample_count += 1;
            }
        }
    }
    if (result)
    {
        memset(offsets, 0, ((uint64_t)graph->block_count + 1) * sizeof(uint32_t));
        for (uint32_t edge = 0; edge < graph->edge_count; edge += 1)
        {
            offsets[(uint64_t)graph->successors[edge] + 1] += 1;
        }
        for (uint32_t block = 0; block < graph->block_count; block += 1)
        {
            offsets[(uint64_t)block + 1] += offsets[block];
            cursors[block] = offsets[block];
        }
        for (uint32_t block = 0; block < graph->block_count; block += 1)
        {
            for (uint32_t edge = graph->successor_offsets[block]; edge < graph->successor_offsets[block + 1]; edge += 1)
            {
                const uint32_t successor = graph->successors[edge];
                predecessors[cursors[successor]] = block;
                cursors[successor] += 1;
            }
        }
    }
    for (uint32_t sample = 0; result && sample < sample_count; sample += 1)
    {
        const uint32_t value = samples[sample];
        const uint64_t bit = 1ull << (value % 64);
        const uint64_t word = value / 64;
        uint32_t depth = 0;
        memset(live, 0, graph->block_count);
        for (uint32_t block = 0; block < graph->block_count; block += 1)
        {
            const uint64_t index = (uint64_t)block * graph->row_words + word;
            if ((graph->use[index] & bit) != 0)
            {
                live[block] = 1;
                stack[depth] = block;
                depth += 1;
            }
        }
        while (depth != 0)
        {
            depth -= 1;
            const uint32_t block = stack[depth];
            for (uint32_t edge = offsets[block]; edge < offsets[(uint64_t)block + 1]; edge += 1)
            {
                const uint32_t predecessor = predecessors[edge];
                const uint64_t index = (uint64_t)predecessor * graph->row_words + word;
                if (live[predecessor] == 0 && (graph->def[index] & bit) == 0)
                {
                    // Each block is pushed at most once per value, even with repeated edges and cycles.
                    live[predecessor] = 1;
                    stack[depth] = predecessor;
                    depth += 1;
                }
            }
        }
        for (uint32_t block = 0; result && block < graph->block_count; block += 1)
        {
            const uint64_t index = (uint64_t)block * graph->row_words + word;
            result = (live[block] != 0) == ((graph->in[index] & bit) != 0);
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}

static bool dataflow_test_connected(const Dataflow* graph, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->block_count);
    uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, graph->block_count);
    bool result = graph->block_count == 0 || (seen != 0 && stack != 0);
    if (result && graph->block_count != 0)
    {
        memset(seen, 0, graph->block_count);
        uint32_t depth = 1;
        uint32_t reached = 1;
        stack[0] = 0;
        seen[0] = 1;
        while (depth != 0)
        {
            depth -= 1;
            const uint32_t block = stack[depth];
            for (uint32_t edge = graph->successor_offsets[block]; edge < graph->successor_offsets[block + 1]; edge += 1)
            {
                const uint32_t successor = graph->successors[edge];
                if (seen[successor] == 0)
                {
                    seen[successor] = 1;
                    reached += 1;
                    stack[depth] = successor;
                    depth += 1;
                }
            }
        }
        result = reached == graph->block_count;
    }
    arena_pop_to(scratch, mark);
    return result;
}

static bool dataflow_test_order(const Dataflow* graph, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint8_t* seen = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->block_count);
    bool result = graph->block_count == 0 || seen != 0;
    if (result && graph->block_count != 0)
    {
        memset(seen, 0, graph->block_count);
        for (uint32_t position = 0; result && position < graph->block_count; position += 1)
        {
            const uint32_t block = graph->postorder[position];
            result = block < graph->block_count && seen[block] == 0;
            if (result)
            {
                seen[block] = 1;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}

static void dataflow_test_pair(DataflowTestTally* tally, Dataflow* vector, Dataflow* scalar, Arena* scratch, bool reachability)
{
    dataflow_test_expect(tally, dataflow_test_order(vector, scratch), "postorder does not visit every block once");
    const uint64_t vector_iterations = dataflow_liveness_vector(vector);
    const uint64_t scalar_iterations = dataflow_liveness_scalar(scalar);
    dataflow_test_expect(tally, vector_iterations == scalar_iterations && vector_iterations != 0, "iteration counts differ");
    dataflow_test_expect(tally, dataflow_test_rows_equal(vector, scalar), "scalar and vector rows differ");
    dataflow_test_expect(tally, dataflow_test_equations(vector), "liveness equations do not hold");
    dataflow_test_expect(tally, dataflow_test_padding(vector) && dataflow_test_padding(scalar), "padding bits are nonzero");
    if (reachability)
    {
        dataflow_test_expect(tally, dataflow_test_reachability(vector, scratch), "liveness differs from use-before-def reachability");
    }

    // A solver must begin from the empty solution, independent of a previous run's contents.
    const uint64_t bytes = (uint64_t)vector->block_count * vector->row_words * sizeof(uint64_t);
    if (bytes != 0)
    {
        memset(vector->in, 0xFF, bytes);
        memset(vector->out, 0xFF, bytes);
    }
    const uint64_t repeated_iterations = dataflow_liveness_vector(vector);
    dataflow_test_expect(tally, repeated_iterations == vector_iterations && dataflow_test_rows_equal(vector, scalar),
        "vector solve depends on previous output");
    if (bytes != 0)
    {
        memset(scalar->in, 0xFF, bytes);
        memset(scalar->out, 0xFF, bytes);
    }
    const uint64_t repeated_scalar_iterations = dataflow_liveness_scalar(scalar);
    dataflow_test_expect(tally, repeated_scalar_iterations == scalar_iterations && dataflow_test_rows_equal(vector, scalar),
        "scalar solve depends on previous output");
    dataflow_test_expect(tally, dataflow_test_padding(vector) && dataflow_test_padding(scalar),
        "poisoned padding survived a repeated solve");
}

static void dataflow_test_generated(DataflowTestTally* tally, Arena* memory, Arena* scratch, uint32_t blocks, uint32_t values, uint32_t shape, uint64_t seed)
{
    const uint64_t mark = memory->length;
    tally->blocks = blocks;
    tally->values = values;
    tally->shape = shape;
    tally->seed = seed;
    Dataflow vector = { 0 };
    Dataflow scalar = { 0 };
    const bool made_vector = dataflow_generate(memory, blocks, values, shape, seed, &vector);
    const bool made_scalar = dataflow_generate(memory, blocks, values, shape, seed, &scalar);
    dataflow_test_expect(tally, made_vector && made_scalar, "generated graph allocation failed");
    if (made_vector && made_scalar)
    {
        dataflow_test_expect(tally, dataflow_test_inputs_equal(&vector, &scalar), "same seed generated different inputs");
        const uint64_t expected_words = ((uint64_t)values + 511) / 512 * 8;
        bool aligned = vector.row_words == expected_words;
        if (values != 0 && blocks != 0)
        {
            aligned = aligned && ((uintptr_t)vector.use % 64) == 0 && ((uintptr_t)vector.def % 64) == 0;
            aligned = aligned && ((uintptr_t)vector.in % 64) == 0 && ((uintptr_t)vector.out % 64) == 0;
        }
        dataflow_test_expect(tally, aligned, "row size or alignment is wrong");
        dataflow_test_pair(tally, &vector, &scalar, scratch, blocks <= 8);
        if (blocks == 257 || blocks == 2049)
        {
            dataflow_test_expect(tally, dataflow_test_sampled_reachability(&vector, scratch, seed),
                "large graph differs from sampled use-before-def reachability");
            if (shape == BUSTER_DATAFLOW_SHAPE_CHAIN && blocks == 257 && values == 65)
            {
                vector.in[0] ^= 1;
                dataflow_test_expect(tally, !dataflow_test_sampled_reachability(&vector, scratch, seed),
                    "sampled reachability missed a deliberately flipped live bit");
                vector.in[0] ^= 1;
            }
        }
        if (shape == BUSTER_DATAFLOW_SHAPE_DISPATCH)
        {
            dataflow_test_expect(tally, dataflow_test_connected(&vector, scratch), "dispatch graph has unreachable blocks");
        }
    }
    arena_pop_to(memory, mark);
}

static void dataflow_test_known(DataflowTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint64_t mark = memory->length;
    // Blocks 1 and 2 form a two-entry cycle through 3, 4 and 8 are exits, 5/6 form a disconnected loop,
    // and 7 is a disconnected self-loop without a use. Values can be both used and defined in one block.
    const uint32_t offsets[] = {
        0,
        2,
        3,
        4,
        7,
        7,
        8,
        9,
        10,
        10,
    };
    const uint32_t targets[] = {
        1,
        2,
        3,
        3,
        1,
        2,
        4,
        6,
        5,
        7,
    };
    const uint64_t uses[] = {
        0,
        1,
        2,
        0,
        4,
        0,
        8,
        0,
        16,
    };
    const uint64_t defs[] = {
        1,
        4,
        1,
        3,
        4,
        8,
        0,
        0,
        16,
    };
    tally->blocks = 9;
    tally->values = 7;
    tally->shape = BUSTER_DATAFLOW_SHAPE_IRREDUCIBLE;
    Dataflow vector = { 0 };
    Dataflow scalar = { 0 };
    bool valid = dataflow_create(memory, 9, 7, 10, &vector) && dataflow_create(memory, 9, 7, 10, &scalar);
    if (valid)
    {
        memcpy(vector.successor_offsets, offsets, sizeof(offsets));
        memcpy(scalar.successor_offsets, offsets, sizeof(offsets));
        memcpy(vector.successors, targets, sizeof(targets));
        memcpy(scalar.successors, targets, sizeof(targets));
        for (uint32_t block = 0; block < 9; block += 1)
        {
            const uint64_t index = (uint64_t)block * vector.row_words;
            vector.use[index] = uses[block];
            scalar.use[index] = uses[block];
            vector.def[index] = defs[block];
            scalar.def[index] = defs[block];
        }
        valid = dataflow_postorder(&vector, scratch) && dataflow_postorder(&scalar, scratch);
    }
    dataflow_test_expect(tally, valid, "handwritten graph setup failed");
    if (valid)
    {
        dataflow_test_pair(tally, &vector, &scalar, scratch, true);
        dataflow_test_expect(tally, vector.in[7 * vector.row_words] == 0, "empty self-loop acquired a live value");
        dataflow_test_expect(tally, vector.in[4 * vector.row_words] == 4, "use must survive a definition in the same block");

        // The successor supplies out=0xCC, so use=0xF0 and def=0xAA exercise every truth-table combination.
        vector.block_count = 2;
        scalar.block_count = 2;
        vector.value_count = 8;
        scalar.value_count = 8;
        vector.edge_count = 1;
        scalar.edge_count = 1;
        vector.successor_offsets[0] = 0;
        scalar.successor_offsets[0] = 0;
        vector.successor_offsets[1] = 1;
        scalar.successor_offsets[1] = 1;
        vector.successor_offsets[2] = 1;
        scalar.successor_offsets[2] = 1;
        vector.successors[0] = 1;
        scalar.successors[0] = 1;
        const uint64_t bytes = 2 * vector.row_words * sizeof(uint64_t);
        memset(vector.use, 0, bytes);
        memset(scalar.use, 0, bytes);
        memset(vector.def, 0, bytes);
        memset(scalar.def, 0, bytes);
        vector.use[0] = 0xF0;
        scalar.use[0] = 0xF0;
        vector.def[0] = 0xAA;
        scalar.def[0] = 0xAA;
        vector.use[vector.row_words] = 0xCC;
        scalar.use[scalar.row_words] = 0xCC;
        valid = dataflow_postorder(&vector, scratch) && dataflow_postorder(&scalar, scratch);
        dataflow_test_expect(tally, valid, "truth-table graph postorder failed");
        if (valid)
        {
            dataflow_test_pair(tally, &vector, &scalar, scratch, true);
            dataflow_test_expect(tally, vector.in[0] == 0xF4 && vector.out[0] == 0xCC, "transfer truth table is wrong");
        }
        vector.block_count = 8;
        vector.edge_count = 7;
        for (uint32_t block = 0; block < 8; block += 1)
        {
            vector.successor_offsets[block] = block;
            if (block < 7)
            {
                vector.successors[block] = block + 1;
            }
        }
        vector.successor_offsets[8] = 7;
        valid = dataflow_postorder(&vector, scratch);
        for (uint32_t position = 0; valid && position < 8; position += 1)
        {
            valid = vector.postorder[position] == 7 - position;
        }
        dataflow_test_expect(tally, valid, "eight-block chain is not in reverse order");
    }
    arena_pop_to(memory, mark);
}

static void dataflow_test_repeated_edges(DataflowTestTally* tally, Arena* memory, Arena* scratch)
{
    const uint64_t mark = memory->length;
    const uint32_t repeated_offsets[] = {
        0,
        5,
        7,
        11,
        11,
    };
    const uint32_t repeated_targets[] = {
        1,
        1,
        0,
        0,
        2,
        2,
        2,
        3,
        3,
        1,
        1,
    };
    const uint32_t simple_offsets[] = {
        0,
        3,
        4,
        6,
        6,
    };
    const uint32_t simple_targets[] = {
        1,
        0,
        2,
        2,
        3,
        1,
    };
    const uint32_t uses[] = {
        0,
        63,
        64,
        512,
    };
    const uint32_t defs[] = {
        63,
        512,
        0,
        64,
    };
    tally->blocks = 4;
    tally->values = 513;
    tally->shape = BUSTER_DATAFLOW_SHAPE_IRREDUCIBLE;
    Dataflow repeated = { 0 };
    Dataflow simple = { 0 };
    bool valid = dataflow_create(memory, 4, 513, 11, &repeated) && dataflow_create(memory, 4, 513, 6, &simple);
    if (valid)
    {
        memcpy(repeated.successor_offsets, repeated_offsets, sizeof(repeated_offsets));
        memcpy(repeated.successors, repeated_targets, sizeof(repeated_targets));
        memcpy(simple.successor_offsets, simple_offsets, sizeof(simple_offsets));
        memcpy(simple.successors, simple_targets, sizeof(simple_targets));
        for (uint32_t block = 0; block < 4; block += 1)
        {
            const uint64_t row = (uint64_t)block * repeated.row_words;
            repeated.use[row + uses[block] / 64] = 1ull << (uses[block] % 64);
            simple.use[row + uses[block] / 64] = 1ull << (uses[block] % 64);
            repeated.def[row + defs[block] / 64] = 1ull << (defs[block] % 64);
            simple.def[row + defs[block] / 64] = 1ull << (defs[block] % 64);
        }
        repeated.use[2 * repeated.row_words + 7] |= 1ull << 63;
        simple.use[2 * simple.row_words + 7] |= 1ull << 63;
        valid = dataflow_postorder(&repeated, scratch) && dataflow_postorder(&simple, scratch);
    }
    dataflow_test_expect(tally, valid, "repeated-edge fixture setup failed");
    if (valid)
    {
        dataflow_test_expect(tally, memcmp(repeated.postorder, simple.postorder, 4 * sizeof(uint32_t)) == 0,
            "repeated edges changed the DFS postorder");
        // Both kernels see both versions, including repeated self-edges, a back edge and repeated exit edges.
        dataflow_test_pair(tally, &repeated, &simple, scratch, true);
        dataflow_test_pair(tally, &simple, &repeated, scratch, true);
        dataflow_test_expect(tally, dataflow_test_sampled_reachability(&repeated, scratch, tally->seed),
            "backward reachability differs on repeated edges");
    }
    arena_pop_to(memory, mark);
}

static void dataflow_test_interference(DataflowTestTally* tally)
{
    _Alignas(64) uint64_t a[24] = { 0 };
    _Alignas(64) uint64_t b[24] = { 0 };
    dataflow_test_expect(tally, !dataflow_intersects_vector(0, 0, 0) && !dataflow_intersects_scalar(0, 0, 0),
        "empty rows interfere");
    for (uint64_t words = 8; words <= 24; words += 8)
    {
        for (uint64_t value = 0; value < words * 64; value += 1)
        {
            memset(a, 0, sizeof(a));
            memset(b, 0, sizeof(b));
            a[value / 64] = 1ull << (value % 64);
            const uint64_t next = (value + 1) % (words * 64);
            b[next / 64] = 1ull << (next % 64);
            dataflow_test_expect(tally, !dataflow_intersects_vector(a, b, words) && !dataflow_intersects_scalar(a, b, words),
                "disjoint rows interfere");
            b[value / 64] |= 1ull << (value % 64);
            dataflow_test_expect(tally, dataflow_intersects_vector(a, b, words) && dataflow_intersects_scalar(a, b, words),
                "shared value was not found");
        }
    }
}

static void dataflow_test_guards(DataflowTestTally* tally, Arena* memory, Arena* scratch)
{
    const long queried_page_bytes = sysconf(_SC_PAGESIZE);
    dataflow_test_expect(tally, queried_page_bytes > 0 && queried_page_bytes % 64 == 0, "page size is unavailable or unaligned");
    if (queried_page_bytes > 0 && queried_page_bytes % 64 == 0)
    {
        const uint64_t page_bytes = (uint64_t)queried_page_bytes;
        for (uint64_t value_index = 0; value_index < BUSTER_ARRAY_LENGTH(dataflow_test_values); value_index += 1)
        {
            const uint64_t mark = memory->length;
            tally->blocks = 3;
            tally->values = dataflow_test_values[value_index];
            tally->shape = BUSTER_DATAFLOW_SHAPE_IRREDUCIBLE;
            Dataflow vector = { 0 };
            Dataflow scalar = { 0 };
            bool valid = dataflow_generate(memory, 3, tally->values, tally->shape, tally->seed, &vector);
            valid = valid && dataflow_generate(memory, 3, tally->values, tally->shape, tally->seed, &scalar);
            void* mappings[4] = {
                MAP_FAILED,
                MAP_FAILED,
                MAP_FAILED,
                MAP_FAILED,
            };
            uint64_t** rows[] = {
                &vector.use,
                &vector.def,
                &vector.in,
                &vector.out,
            };
            const uint64_t bytes = (uint64_t)vector.block_count * vector.row_words * sizeof(uint64_t);
            valid = valid && bytes <= page_bytes;
            for (uint32_t row = 0; valid && row < 4; row += 1)
            {
                mappings[row] = mmap(0, page_bytes * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                valid = mappings[row] != MAP_FAILED;
                if (valid)
                {
                    valid = mprotect(mappings[row], page_bytes, PROT_READ | PROT_WRITE) == 0;
                    if (valid)
                    {
                        memset(mappings[row], 0xA5, page_bytes);
                        uint64_t* guarded = (uint64_t*)((uint8_t*)mappings[row] + page_bytes - bytes);
                        if (bytes != 0)
                        {
                            memcpy(guarded, *rows[row], bytes);
                        }
                        *rows[row] = guarded;
                    }
                }
            }
            dataflow_test_expect(tally, valid, "guard-page setup failed");
            if (valid)
            {
                dataflow_test_pair(tally, &vector, &scalar, scratch, true);
                const uint64_t guarded_iterations = dataflow_liveness_scalar(&vector);
                const uint64_t regular_iterations = dataflow_liveness_scalar(&scalar);
                dataflow_test_expect(tally, guarded_iterations == regular_iterations && dataflow_test_rows_equal(&vector, &scalar),
                    "scalar solve at a guard page differs");
                const uint64_t* last_use = vector.use + 2 * vector.row_words;
                const uint64_t* last_def = vector.def + 2 * vector.row_words;
                const bool reference = dataflow_intersects_scalar(last_use, last_def, vector.row_words);
                dataflow_test_expect(tally, dataflow_intersects_vector(last_use, last_def, vector.row_words) == reference,
                    "guard-page interference differs");
                bool prefix_intact = true;
                for (uint32_t row = 0; row < 4; row += 1)
                {
                    const uint8_t* prefix = mappings[row];
                    for (uint64_t byte = 0; byte < page_bytes - bytes; byte += 1)
                    {
                        prefix_intact = prefix_intact && prefix[byte] == 0xA5;
                    }
                }
                dataflow_test_expect(tally, prefix_intact, "solver wrote before a guarded matrix");
            }
            for (uint32_t row = 0; row < 4; row += 1)
            {
                if (mappings[row] != MAP_FAILED)
                {
                    munmap(mappings[row], page_bytes * 2);
                }
            }
            arena_pop_to(memory, mark);
        }
    }
}

static void dataflow_test_limits(DataflowTestTally* tally, Arena* memory, Arena* scratch)
{
    _Alignas(64) uint8_t buffer[4096];
    uint64_t failed_allocations = 0;
    uint64_t successful_allocations = 0;
    for (uint64_t capacity = 17; capacity <= sizeof(buffer); capacity += 61)
    {
        Arena small = {
            .base = buffer,
            .length = 17,
            .capacity = capacity,
        };
        Dataflow graph = {
            .block_count = 123,
        };
        uint8_t saved[sizeof(graph)];
        memcpy(saved, &graph, sizeof(graph));
        const bool created = dataflow_create(&small, 4, 1025, 16, &graph);
        if (!created)
        {
            failed_allocations += 1;
            dataflow_test_expect(tally, small.length == 17 && memcmp(saved, &graph, sizeof(graph)) == 0,
                "failed allocation changed the arena or result");
        }
        else
        {
            successful_allocations += 1;
            uint64_t present = 0;
            for (uint64_t word = 0; word < graph.block_count * graph.row_words; word += 1)
            {
                present |= graph.use[word] | graph.def[word] | graph.in[word] | graph.out[word];
            }
            dataflow_test_expect(tally, present == 0, "new allocation is not zeroed");
        }
    }
    dataflow_test_expect(tally, failed_allocations != 0 && successful_allocations != 0, "capacity tests missed an outcome");

    const uint64_t mark = memory->length;
    Dataflow graph = { 0 };
    const bool created = dataflow_create(memory, 3, 1, 2, &graph);
    dataflow_test_expect(tally, created, "invalid-CFG fixture allocation failed");
    if (created)
    {
        const uint32_t offsets[] = {
            0,
            1,
            2,
            2,
        };
        const uint32_t targets[] = {
            1,
            2,
        };
        for (uint32_t variant = 0; variant < 5; variant += 1)
        {
            memcpy(graph.successor_offsets, offsets, sizeof(offsets));
            memcpy(graph.successors, targets, sizeof(targets));
            memset(graph.postorder, 0xA5, 3 * sizeof(uint32_t));
            switch (variant)
            {
                case 0: graph.successor_offsets[0] = 1; break;
                case 1: graph.successor_offsets[3] = 1; break;
                case 2: graph.successor_offsets[1] = 3; break;
                case 3: graph.successor_offsets[2] = 0; break;
                case 4: graph.successors[1] = 3; break;
            }
            const uint64_t scratch_mark = scratch->length;
            const bool accepted = dataflow_postorder(&graph, scratch);
            bool untouched = true;
            for (uint32_t block = 0; block < 3; block += 1)
            {
                untouched = untouched && graph.postorder[block] == 0xA5A5A5A5u;
            }
            dataflow_test_expect(tally, !accepted && untouched && scratch->length == scratch_mark,
                "malformed CFG accepted or modified order/scratch");
        }
        memcpy(graph.successor_offsets, offsets, sizeof(offsets));
        memcpy(graph.successors, targets, sizeof(targets));
        Arena empty = { 0 };
        dataflow_test_expect(tally, !dataflow_postorder(&graph, &empty), "postorder accepted missing scratch memory");
        const uint64_t scratch_mark = scratch->length;
        const bool ordered = dataflow_postorder(&graph, scratch);
        dataflow_test_expect(tally, ordered && scratch->length == scratch_mark, "valid postorder failed or retained scratch");
        if (ordered)
        {
            dataflow_test_expect(tally, graph.postorder[0] == 2 && graph.postorder[1] == 1 && graph.postorder[2] == 0,
                "chain is not in postorder");
        }
    }
    arena_pop_to(memory, mark);
    const bool empty_with_edge = dataflow_create(memory, 0, 1, 1, &graph);
    dataflow_test_expect(tally, !empty_with_edge && memory->length == mark, "empty CFG accepted an edge");
    arena_pop_to(memory, mark);
    Arena empty = { 0 };
    dataflow_test_expect(tally, !dataflow_create(&empty, UINT32_MAX, UINT32_MAX, UINT32_MAX, &graph),
        "enormous allocation succeeded without storage");
}

static bool dataflow_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0 && errno == 0;
    if (result)
    {
        *value = parsed;
    }
    return result;
}

static int run_dataflow_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t count = BUSTER_DATAFLOW_TEST_DEFAULT_COUNT;
    bool valid = true;
    for (int argument = 0; argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc)
        {
            argument += 1;
            valid = dataflow_test_parse_u64(argv[argument], &seed) && valid;
        }
        else if (strcmp(argv[argument], "--count") == 0 && argument + 1 < argc)
        {
            argument += 1;
            valid = dataflow_test_parse_u64(argv[argument], &count) && valid;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-dataflow [--seed N] [--count N]\n");
    }
    else
    {
        Arena memory = arena_reserve(BUSTER_DATAFLOW_TEST_ARENA_BYTES);
        Arena scratch = arena_reserve(BUSTER_DATAFLOW_TEST_ARENA_BYTES);
        DataflowTestTally tally = {
            .seed = seed,
        };
        dataflow_test_expect(&tally, memory.base != 0 && scratch.base != 0, "test arena reservation failed");
        if (memory.base != 0 && scratch.base != 0)
        {
            dataflow_test_limits(&tally, &memory, &scratch);
            dataflow_test_known(&tally, &memory, &scratch);
            dataflow_test_repeated_edges(&tally, &memory, &scratch);
            dataflow_test_interference(&tally);
            dataflow_test_guards(&tally, &memory, &scratch);
            for (uint32_t shape = 0; shape < BUSTER_DATAFLOW_SHAPE_COUNT; shape += 1)
            {
                for (uint64_t value_index = 0; value_index < BUSTER_ARRAY_LENGTH(dataflow_test_values); value_index += 1)
                {
                    for (uint64_t block_index = 0; block_index < BUSTER_ARRAY_LENGTH(dataflow_test_blocks); block_index += 1)
                    {
                        dataflow_test_generated(&tally, &memory, &scratch, dataflow_test_blocks[block_index],
                            dataflow_test_values[value_index], shape, seed);
                    }
                }
                for (uint64_t sample = 0; sample < count; sample += 1)
                {
                    const uint32_t blocks = 1 + (uint32_t)((sample * 47 + seed % 257) % 257);
                    const uint32_t values = (uint32_t)((sample * 139 + seed % 1537) % 1537);
                    dataflow_test_generated(&tally, &memory, &scratch, blocks, values, shape, seed + sample + 1);
                }
            }
        }
        printf("dataflow: %" PRIu64 " checks, %" PRIu64 " failures (seed=%" PRIu64 ", count=%" PRIu64 ")\n",
            tally.checks, tally.failures, seed, count);
        result = tally.failures == 0 ? 0 : 1;
        arena_release(&scratch);
        arena_release(&memory);
    }
    return result;
}
