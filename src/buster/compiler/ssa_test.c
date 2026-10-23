// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent local-promotion tests, included after ssa.c and ssa_program.c. The interpreter follows actual CFG
// edges, compares each read and write with memory locals, and evaluates all phis simultaneously. Opaque VALUEs
// receive a new token on every execution, exposing stale loop values without inventing semantics for the future IR.
// Definedness is checked separately: undef is never assigned an arbitrary numeric value for the comparison.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_SSA_TEST_ARENA_BYTES (128ull << 20)
#define BUSTER_SSA_TEST_REPORTS 20
#define BUSTER_SSA_TEST_TRACES 8
#define BUSTER_SSA_TEST_STEPS 256

typedef struct SsaTestTally SsaTestTally;
struct SsaTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t seed;
    const char* name;
    uint32_t blocks;
    uint32_t locals;
};

typedef struct SsaTestSpace SsaTestSpace;
struct SsaTestSpace
{
    Arena input;
    Arena output;
    Arena twin;
    Arena scratch;
    Arena oracle;
};

typedef struct SsaTestValue SsaTestValue;
struct SsaTestValue
{
    uint64_t bits;
    bool defined;
    bool available;
};

typedef struct SsaTestFixture SsaTestFixture;
struct SsaTestFixture
{
    SsaInput input;
    uint32_t block_offsets[10];
    uint32_t successor_offsets[10];
    uint32_t successors[32];
    uint8_t kinds[256];
    uint8_t types[256];
    uint32_t locals[256];
    uint32_t operands[256];
    uint8_t local_types[8];
    uint8_t local_flags[8];
};

static bool ssa_test_expect(SsaTestTally* tally, bool condition, const char* description)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_SSA_TEST_REPORTS)
        {
            fprintf(stderr, "test-ssa: %s: %s (seed=%" PRIu64 ", blocks=%u, locals=%u)\n", tally->name, description, tally->seed, tally->blocks, tally->locals);
        }
        tally->failures += 1;
    }
    return condition;
}

static uint64_t ssa_test_hash_bytes(uint64_t hash, const void* pointer, uint64_t count)
{
    const uint8_t* bytes = pointer;
    for (uint64_t i = 0; i < count; i += 1)
    {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t ssa_test_input_hash(const SsaInput* input)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = ssa_test_hash_bytes(hash, input->block_offsets, ((uint64_t)input->block_count + 1) * 4);
    hash = ssa_test_hash_bytes(hash, input->successor_offsets, ((uint64_t)input->block_count + 1) * 4);
    hash = ssa_test_hash_bytes(hash, input->successors, (uint64_t)input->edge_count * 4);
    hash = ssa_test_hash_bytes(hash, input->kinds, input->instruction_count);
    hash = ssa_test_hash_bytes(hash, input->types, input->instruction_count);
    hash = ssa_test_hash_bytes(hash, input->locals, (uint64_t)input->instruction_count * 4);
    hash = ssa_test_hash_bytes(hash, input->operands, (uint64_t)input->instruction_count * 4);
    hash = ssa_test_hash_bytes(hash, input->local_types, input->local_count);
    hash = ssa_test_hash_bytes(hash, input->local_flags, input->local_count);
    return hash;
}

static bool ssa_test_value_equal(SsaTestValue left, SsaTestValue right)
{
    const bool equal = left.available && right.available && left.defined == right.defined && (!left.defined || left.bits == right.bits);
    return equal;
}

static bool ssa_test_metadata(const SsaInput* input, const SsaResult* result, Arena* arena)
{
    const uint64_t saved_length = arena->length;
    uint8_t* reachable = arena_push_array(arena, input->block_count, 1, 1);
    uint32_t* stack = arena_push_array(arena, input->block_count, 4, 4);
    bool valid = input->block_count == 0 || (reachable != 0 && stack != 0);
    if (valid && input->block_count != 0)
    {
        memset(reachable, 0, input->block_count);
        uint32_t count = 1;
        stack[0] = 0;
        reachable[0] = 1;
        while (count != 0)
        {
            const uint32_t block = stack[--count];
            for (uint32_t e = input->successor_offsets[block]; e < input->successor_offsets[block + 1]; e += 1)
            {
                const uint32_t next = input->successors[e];
                if (reachable[next] == 0)
                {
                    reachable[next] = 1;
                    stack[count++] = next;
                }
            }
        }
        valid = memcmp(reachable, result->reachable, input->block_count) == 0;
    }
    for (uint32_t local = 0; valid && local < input->local_count; local += 1)
    {
        bool promoted = input->local_flags[local] == 0 && input->local_types[local] != BUSTER_SSA_TYPE_AGGREGATE;
        for (uint32_t i = 0; i < input->instruction_count; i += 1)
        {
            promoted &= input->kinds[i] != BUSTER_SSA_ADDRESS || input->locals[i] != local;
        }
        valid = (result->promoted[local] != 0) == promoted;
    }
    for (uint32_t block = 0; valid && block < input->block_count; block += 1)
    {
        uint32_t incoming = result->predecessor_offsets[block];
        for (uint32_t from = 0; valid && from < input->block_count; from += 1)
        {
            for (uint32_t edge = input->successor_offsets[from]; valid && edge < input->successor_offsets[from + 1]; edge += 1)
            {
                if (reachable[from] && input->successors[edge] == block)
                {
                    valid = incoming < result->predecessor_offsets[block + 1] && result->predecessors[incoming] == from && result->predecessor_edges[incoming] == edge;
                    incoming += 1;
                }
            }
        }
        valid &= incoming == result->predecessor_offsets[block + 1];
        for (uint32_t i = input->block_offsets[block]; valid && i < input->block_offsets[block + 1]; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            if (kind == BUSTER_SSA_WRITE)
            {
                valid = result->replacements[i] == BUSTER_SSA_NONE;
            }
            else if (kind != BUSTER_SSA_READ || !result->promoted[input->locals[i]])
            {
                valid = result->replacements[i] == i;
            }
            else if (!reachable[block])
            {
                valid = result->replacements[i] == input->instruction_count + input->locals[i];
            }
        }
    }
    arena_pop_to(arena, saved_length);
    return valid;
}

static bool ssa_test_interpret(const SsaInput* input, const SsaResult* result, Arena* arena, uint64_t seed)
{
    const uint64_t saved_length = arena->length;
    const uint64_t values = (uint64_t)result->phi_base + result->phi_count;
    SsaTestValue* original = arena_push_array(arena, input->instruction_count, sizeof(SsaTestValue), _Alignof(SsaTestValue));
    SsaTestValue* renamed = arena_push_array(arena, values, sizeof(SsaTestValue), _Alignof(SsaTestValue));
    SsaTestValue* memory = arena_push_array(arena, input->local_count, sizeof(SsaTestValue), _Alignof(SsaTestValue));
    SsaTestValue* retained = arena_push_array(arena, input->local_count, sizeof(SsaTestValue), _Alignof(SsaTestValue));
    SsaTestValue* phis = arena_push_array(arena, result->phi_count, sizeof(SsaTestValue), _Alignof(SsaTestValue));
    bool valid = (input->instruction_count == 0 || original != 0) && (values == 0 || renamed != 0) &&
                 (input->local_count == 0 || (memory != 0 && retained != 0)) && (result->phi_count == 0 || phis != 0);
    for (uint32_t trace = 0; valid && trace < BUSTER_SSA_TEST_TRACES; trace += 1)
    {
        if (input->instruction_count != 0)
        {
            memset(original, 0, (uint64_t)input->instruction_count * sizeof(SsaTestValue));
        }
        if (values != 0)
        {
            memset(renamed, 0, values * sizeof(SsaTestValue));
        }
        const SsaTestValue undef = { .available = true };
        for (uint32_t local = 0; local < input->local_count; local += 1)
        {
            memory[local] = undef;
            retained[local] = undef;
            renamed[input->instruction_count + local] = undef;
        }
        uint32_t block = 0;
        uint32_t incoming_edge = BUSTER_SSA_NONE;
        uint64_t random = seed + trace;
        for (uint32_t step = 0; valid && input->block_count != 0 && step < BUSTER_SSA_TEST_STEPS; step += 1)
        {
            uint32_t incoming = BUSTER_SSA_NONE;
            for (uint32_t e = result->predecessor_offsets[block]; e < result->predecessor_offsets[block + 1]; e += 1)
            {
                if (result->predecessor_edges[e] == incoming_edge)
                {
                    incoming = e - result->predecessor_offsets[block];
                }
            }
            // Read every incoming operand before changing any phi result; swapping loop-carried locals needs this.
            for (uint32_t phi = 0; valid && phi < result->phi_count; phi += 1)
            {
                if (result->phi_blocks[phi] == block)
                {
                    valid = incoming != BUSTER_SSA_NONE && incoming < result->phi_offsets[phi + 1] - result->phi_offsets[phi];
                    if (valid)
                    {
                        const uint32_t value = result->phi_operands[result->phi_offsets[phi] + incoming];
                        valid = value < values && renamed[value].available;
                        if (valid)
                        {
                            phis[phi] = renamed[value];
                        }
                    }
                }
            }
            for (uint32_t phi = 0; valid && phi < result->phi_count; phi += 1)
            {
                if (result->phi_blocks[phi] == block)
                {
                    renamed[result->phi_base + phi] = phis[phi];
                }
            }
            for (uint32_t i = input->block_offsets[block]; valid && i < input->block_offsets[block + 1]; i += 1)
            {
                const uint8_t kind = input->kinds[i];
                const uint32_t local = input->locals[i];
                if (kind == BUSTER_SSA_VALUE || kind == BUSTER_SSA_ADDRESS)
                {
                    const SsaTestValue value = {
                        .bits = kind == BUSTER_SSA_ADDRESS ? (uint64_t)local * 8 + 8 :
                                (((uint64_t)i + 1) << 32) | ((uint64_t)step + 1),
                        .defined = true,
                        .available = true,
                    };
                    original[i] = value;
                    renamed[i] = value;
                }
                else if (kind == BUSTER_SSA_READ)
                {
                    original[i] = memory[local];
                    if (!result->promoted[local])
                    {
                        renamed[i] = retained[local];
                    }
                    const uint32_t value = result->replacements[i];
                    valid = value < values && ssa_test_value_equal(original[i], renamed[value]);
                }
                else
                {
                    const uint32_t operand = input->operands[i];
                    const uint32_t replacement = result->replacements[operand];
                    valid = replacement < values && ssa_test_value_equal(original[operand], renamed[replacement]);
                    if (valid)
                    {
                        memory[local] = original[operand];
                        if (!result->promoted[local])
                        {
                            retained[local] = renamed[replacement];
                        }
                    }
                }
            }
            const uint32_t first = input->successor_offsets[block];
            const uint32_t count = input->successor_offsets[block + 1] - first;
            if (count == 0)
            {
                break;
            }
            random = random * UINT64_C(2862933555777941757) + UINT64_C(3037000493);
            incoming_edge = first + (step == 0 ? trace % count : (uint32_t)(random >> 32) % count);
            block = input->successors[incoming_edge];
        }
    }
    arena_pop_to(arena, saved_length);
    return valid;
}

static void ssa_test_run(SsaTestTally* tally, SsaTestSpace* space, const SsaInput* input, bool no_phis, bool needs_phi)
{
    tally->blocks = input->block_count;
    tally->locals = input->local_count;
    arena_pop_to(&space->output, 0);
    arena_pop_to(&space->twin, 0);
    arena_pop_to(&space->scratch, 0);
    const uint64_t hash = ssa_test_input_hash(input);
    SsaResult vector = { 0 };
    SsaResult scalar = { 0 };
    const SsaError vector_error = ssa_build(input, &space->output, &space->scratch, &vector);
    const SsaError scalar_error = ssa_build_scalar(input, &space->twin, &space->scratch, &scalar);
    const bool vector_built = ssa_test_expect(tally, vector_error == SSA_OK, "vector construction");
    const bool scalar_built = ssa_test_expect(tally, scalar_error == SSA_OK, "scalar construction");
    const bool built = vector_built && scalar_built;
    ssa_test_expect(tally, space->scratch.length == 0, "constructors restore scratch");
    if (built)
    {
        const SsaCheck vector_check = ssa_verify(input, &vector, &space->scratch);
        const SsaCheck scalar_check = ssa_verify(input, &scalar, &space->scratch);
        if (vector_check.error != SSA_OK && tally->failures < BUSTER_SSA_TEST_REPORTS)
        {
            fprintf(stderr, "test-ssa: vector verifier: %s at %u\n", vector_check.rule, vector_check.instruction);
        }
        if (scalar_check.error != SSA_OK && tally->failures < BUSTER_SSA_TEST_REPORTS)
        {
            fprintf(stderr, "test-ssa: scalar verifier: %s at %u\n", scalar_check.rule, scalar_check.instruction);
        }
        const bool vector_verified = ssa_test_expect(tally, vector_check.error == SSA_OK, "vector verification");
        const bool scalar_verified = ssa_test_expect(tally, scalar_check.error == SSA_OK, "scalar verification");
        const bool verified = vector_verified && scalar_verified;
        ssa_test_expect(tally, space->scratch.length == 0, "verifier restores scratch");
        if (verified)
        {
            ssa_test_expect(tally, ssa_test_metadata(input, &vector, &space->oracle), "vector metadata against source graph");
            ssa_test_expect(tally, ssa_test_metadata(input, &scalar, &space->oracle), "scalar metadata against source graph");
            ssa_test_expect(tally, ssa_test_interpret(input, &vector, &space->oracle, tally->seed), "vector execution against memory locals");
            ssa_test_expect(tally, ssa_test_interpret(input, &scalar, &space->oracle, tally->seed), "scalar execution against memory locals");
        }
        if (no_phis)
        {
            ssa_test_expect(tally, vector.phi_count == 0 && scalar.phi_count == 0, "redundant or unused phis are absent");
        }
        if (needs_phi)
        {
            ssa_test_expect(tally, vector.phi_count != 0 && scalar.phi_count != 0, "different reaching definitions retain a phi");
        }
    }
    ssa_test_expect(tally, hash == ssa_test_input_hash(input), "inputs remain immutable");
}

static void ssa_test_fixture_begin(SsaTestFixture* fixture, uint32_t blocks, uint32_t locals)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->input = (SsaInput){
        .block_count = blocks,
        .local_count = locals,
        .block_offsets = fixture->block_offsets,
        .successor_offsets = fixture->successor_offsets,
        .successors = fixture->successors,
        .kinds = fixture->kinds,
        .types = fixture->types,
        .locals = fixture->locals,
        .operands = fixture->operands,
        .local_types = fixture->local_types,
        .local_flags = fixture->local_flags,
    };
    for (uint32_t local = 0; local < locals; local += 1)
    {
        fixture->local_types[local] = BUSTER_SSA_TYPE_I64;
    }
}

static uint32_t ssa_test_emit(SsaTestFixture* fixture, uint8_t kind, uint32_t local, uint32_t operand)
{
    const uint32_t i = fixture->input.instruction_count++;
    fixture->kinds[i] = kind;
    fixture->types[i] = kind == BUSTER_SSA_ADDRESS ? BUSTER_SSA_TYPE_PTR : fixture->local_types[local];
    fixture->locals[i] = kind == BUSTER_SSA_VALUE ? BUSTER_SSA_NONE : local;
    fixture->operands[i] = operand;
    return i;
}

static void ssa_test_fixture_edge(SsaTestFixture* fixture, uint32_t block, uint32_t target)
{
    fixture->successors[fixture->input.edge_count++] = target;
    fixture->successor_offsets[block + 1] = fixture->input.edge_count;
}

static void ssa_test_fixture_block(SsaTestFixture* fixture, uint32_t block)
{
    fixture->block_offsets[block] = fixture->input.instruction_count;
    fixture->successor_offsets[block] = fixture->input.edge_count;
}

static void ssa_test_fixture_finish(SsaTestFixture* fixture)
{
    fixture->block_offsets[fixture->input.block_count] = fixture->input.instruction_count;
    fixture->successor_offsets[fixture->input.block_count] = fixture->input.edge_count;
}

static void ssa_test_diamond(SsaTestFixture* fixture, bool initial, bool second_definition)
{
    ssa_test_fixture_begin(fixture, 4, 1);
    ssa_test_fixture_block(fixture, 0);
    const uint32_t value = ssa_test_emit(fixture, BUSTER_SSA_VALUE, 0, BUSTER_SSA_NONE);
    const uint32_t other = ssa_test_emit(fixture, BUSTER_SSA_VALUE, 0, BUSTER_SSA_NONE);
    if (initial)
    {
        ssa_test_emit(fixture, BUSTER_SSA_WRITE, 0, value);
    }
    ssa_test_fixture_edge(fixture, 0, 1);
    ssa_test_fixture_edge(fixture, 0, 2);
    ssa_test_fixture_block(fixture, 1);
    ssa_test_emit(fixture, BUSTER_SSA_WRITE, 0, value);
    ssa_test_fixture_edge(fixture, 1, 3);
    ssa_test_fixture_edge(fixture, 1, 3);
    ssa_test_fixture_block(fixture, 2);
    if (second_definition)
    {
        ssa_test_emit(fixture, BUSTER_SSA_WRITE, 0, other);
    }
    ssa_test_fixture_edge(fixture, 2, 3);
    ssa_test_fixture_block(fixture, 3);
    ssa_test_emit(fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_fixture_finish(fixture);
}

static void ssa_test_fixtures(SsaTestTally* tally, SsaTestSpace* space)
{
    SsaTestFixture fixture;
    tally->name = "empty CFG";
    ssa_test_fixture_begin(&fixture, 0, 0);
    ssa_test_run(tally, space, &fixture.input, true, false);
    tally->name = "empty block and no locals";
    ssa_test_fixture_begin(&fixture, 1, 0);
    ssa_test_run(tally, space, &fixture.input, true, false);
    tally->name = "same-block reads and writes";
    ssa_test_fixture_begin(&fixture, 1, 1);
    ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    const uint32_t value = ssa_test_emit(&fixture, BUSTER_SSA_VALUE, 0, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 0, value);
    const uint32_t read = ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 0, read);
    ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_fixture_finish(&fixture);
    ssa_test_run(tally, space, &fixture.input, true, false);
    tally->name = "phi of undef and value; repeated edges";
    ssa_test_diamond(&fixture, false, false);
    ssa_test_run(tally, space, &fixture.input, false, true);
    tally->name = "same reaching value through a diamond";
    ssa_test_diamond(&fixture, true, false);
    ssa_test_run(tally, space, &fixture.input, true, false);
    tally->name = "distinct dominating definitions";
    ssa_test_diamond(&fixture, true, true);
    ssa_test_run(tally, space, &fixture.input, false, true);
    tally->name = "unused merge";
    fixture.input.instruction_count -= 1;
    fixture.block_offsets[4] -= 1;
    ssa_test_run(tally, space, &fixture.input, true, false);

    tally->name = "simultaneous loop phis";
    ssa_test_fixture_begin(&fixture, 4, 2);
    ssa_test_fixture_block(&fixture, 0);
    uint32_t left = ssa_test_emit(&fixture, BUSTER_SSA_VALUE, 0, BUSTER_SSA_NONE);
    uint32_t right = ssa_test_emit(&fixture, BUSTER_SSA_VALUE, 1, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 0, left);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 1, right);
    ssa_test_fixture_edge(&fixture, 0, 1);
    ssa_test_fixture_block(&fixture, 1);
    ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_READ, 1, BUSTER_SSA_NONE);
    ssa_test_fixture_edge(&fixture, 1, 2);
    ssa_test_fixture_edge(&fixture, 1, 3);
    ssa_test_fixture_block(&fixture, 2);
    left = ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    right = ssa_test_emit(&fixture, BUSTER_SSA_READ, 1, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 0, right);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 1, left);
    ssa_test_fixture_edge(&fixture, 2, 1);
    ssa_test_fixture_block(&fixture, 3);
    ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_READ, 1, BUSTER_SSA_NONE);
    ssa_test_fixture_finish(&fixture);
    ssa_test_run(tally, space, &fixture.input, false, true);

    tally->name = "redundant irreducible phi cycle";
    ssa_test_fixture_begin(&fixture, 4, 1);
    ssa_test_fixture_block(&fixture, 0);
    left = ssa_test_emit(&fixture, BUSTER_SSA_VALUE, 0, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 0, left);
    ssa_test_fixture_edge(&fixture, 0, 1);
    ssa_test_fixture_edge(&fixture, 0, 2);
    ssa_test_fixture_block(&fixture, 1);
    left = ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 0, left);
    ssa_test_fixture_edge(&fixture, 1, 2);
    ssa_test_fixture_edge(&fixture, 1, 3);
    ssa_test_fixture_block(&fixture, 2);
    left = ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_emit(&fixture, BUSTER_SSA_WRITE, 0, left);
    ssa_test_fixture_edge(&fixture, 2, 1);
    ssa_test_fixture_edge(&fixture, 2, 3);
    ssa_test_fixture_block(&fixture, 3);
    ssa_test_emit(&fixture, BUSTER_SSA_READ, 0, BUSTER_SSA_NONE);
    ssa_test_fixture_finish(&fixture);
    ssa_test_run(tally, space, &fixture.input, true, false);
    tally->name = "undef-only irreducible cycle";
    fixture.kinds[1] = BUSTER_SSA_READ;
    fixture.operands[1] = BUSTER_SSA_NONE;
    ssa_test_run(tally, space, &fixture.input, true, false);

    tally->name = "late address and memory exclusions";
    ssa_test_fixture_begin(&fixture, 1, 6);
    fixture.local_flags[1] = BUSTER_SSA_LOCAL_ADDRESS_TAKEN;
    fixture.local_flags[2] = BUSTER_SSA_LOCAL_VOLATILE;
    fixture.local_flags[3] = BUSTER_SSA_LOCAL_ATOMIC;
    fixture.local_types[4] = BUSTER_SSA_TYPE_AGGREGATE;
    for (uint32_t local = 0; local < 6; local += 1)
    {
        ssa_test_emit(&fixture, BUSTER_SSA_READ, local, BUSTER_SSA_NONE);
        left = ssa_test_emit(&fixture, BUSTER_SSA_VALUE, local, BUSTER_SSA_NONE);
        ssa_test_emit(&fixture, BUSTER_SSA_WRITE, local, left);
        ssa_test_emit(&fixture, BUSTER_SSA_READ, local, BUSTER_SSA_NONE);
    }
    ssa_test_emit(&fixture, BUSTER_SSA_ADDRESS, 5, BUSTER_SSA_NONE);
    ssa_test_fixture_finish(&fixture);
    ssa_test_run(tally, space, &fixture.input, true, false);
}

static void ssa_test_generated(SsaTestTally* tally, SsaTestSpace* space, uint32_t count)
{
    static const uint32_t boundaries[] = { 1, 2, 15, 16, 17, 63, 64, 65, 511, 512, 513, 1025 };
    for (uint32_t index = 0; index < sizeof(boundaries) / sizeof(boundaries[0]); index += 1)
    {
        for (uint32_t shape = 0; shape < BUSTER_SSA_SHAPE_COUNT; shape += 1)
        {
            arena_pop_to(&space->input, 0);
            SsaInput input = { 0 };
            tally->name = "generated boundary CFG";
            const uint32_t locals = index < 8 ? boundaries[index] : 17;
            if (ssa_test_expect(tally, ssa_generate(&space->input, boundaries[index], locals, shape, tally->seed + index, &input), "generate input"))
            {
                ssa_test_run(tally, space, &input, false, false);
            }
        }
    }
    static const uint32_t local_widths[] = { 511, 512, 513, 1025 };
    for (uint32_t width = 0; width < sizeof(local_widths) / sizeof(local_widths[0]); width += 1)
    {
        arena_pop_to(&space->input, 0);
        SsaInput input = { 0 };
        tally->name = "liveness row boundaries";
        if (ssa_test_expect(tally, ssa_generate(&space->input, 17, local_widths[width], BUSTER_SSA_SHAPE_RANDOM, tally->seed + width, &input), "generate wide local table"))
        {
            // Make the number of promoted locals exact, so both sides of the 512-bit row boundary are exercised.
            memset((uint8_t*)input.local_types, BUSTER_SSA_TYPE_I64, input.local_count);
            memset((uint8_t*)input.local_flags, 0, input.local_count);
            memset((uint8_t*)input.types, BUSTER_SSA_TYPE_I64, input.instruction_count);
            for (uint32_t i = 0; i < input.instruction_count; i += 1)
            {
                if (input.kinds[i] == BUSTER_SSA_ADDRESS)
                {
                    ((uint8_t*)input.kinds)[i] = BUSTER_SSA_VALUE;
                    ((uint32_t*)input.locals)[i] = BUSTER_SSA_NONE;
                }
            }
            ssa_test_run(tally, space, &input, false, false);
        }
    }
    uint64_t random = tally->seed;
    for (uint32_t i = 0; i < count; i += 1)
    {
        arena_pop_to(&space->input, 0);
        SsaInput input = { 0 };
        const uint32_t blocks = 1 + ssa_program_random(&random) % 97;
        const uint32_t locals = ssa_program_random(&random) % 130;
        const uint32_t shape = ssa_program_random(&random) % BUSTER_SSA_SHAPE_COUNT;
        tally->name = "random local-access program";
        if (ssa_test_expect(tally, ssa_generate(&space->input, blocks, locals, shape, random, &input), "generate random input"))
        {
            ssa_test_run(tally, space, &input, false, false);
        }
    }
}

static void ssa_test_guards(SsaTestTally* tally, SsaTestSpace* space)
{
    static const uint32_t lengths[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129 };
    const long page_size = sysconf(_SC_PAGESIZE);
    tally->name = "guard-page input tails";
    if (ssa_test_expect(tally, page_size > 0, "read page size"))
    {
        for (uint32_t case_index = 0; case_index < 2 * sizeof(lengths) / sizeof(lengths[0]); case_index += 1)
        {
            const uint32_t length = lengths[case_index / 2];
            const bool address_taken = (case_index & 1) != 0;
            SsaTestFixture fixture;
            ssa_test_fixture_begin(&fixture, 1, length != 0 ? 1 : 0);
            for (uint32_t i = 0; i < length; i += 1)
            {
                const uint8_t kind = i % 4 == 0 ? BUSTER_SSA_VALUE : i % 4 == 1 ? BUSTER_SSA_WRITE : i % 4 == 3 && address_taken ? BUSTER_SSA_ADDRESS : BUSTER_SSA_READ;
                ssa_test_emit(&fixture, kind, 0, kind == BUSTER_SSA_WRITE ? i - 1 : BUSTER_SSA_NONE);
            }
            ssa_test_fixture_finish(&fixture);
            SsaInput guarded = fixture.input;
            uint8_t local_types[256];
            uint8_t local_flags[256];
            for (uint32_t local = 0; local < length; local += 1)
            {
                local_types[local] = BUSTER_SSA_TYPE_I64;
                local_flags[local] = local % 17 == 16 ? BUSTER_SSA_LOCAL_VOLATILE : 0;
            }
            guarded.local_count = length;
            guarded.local_types = local_types;
            guarded.local_flags = local_flags;
            const void* sources[] = { guarded.block_offsets, guarded.successor_offsets, guarded.successors, guarded.kinds, guarded.types,
                                      guarded.locals, guarded.operands, guarded.local_types, guarded.local_flags };
            const uint64_t bytes[] = { 8, 8, 0, length, length, (uint64_t)length * 4, (uint64_t)length * 4, guarded.local_count, guarded.local_count };
            void* mappings[9] = { 0 };
            void* columns[9] = { 0 };
            bool valid = true;
            for (uint32_t column = 0; column < 9; column += 1)
            {
                mappings[column] = mmap(0, (uint64_t)page_size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (mappings[column] != MAP_FAILED)
                {
                    uint8_t* guard = (uint8_t*)mappings[column] + page_size;
                    valid &= bytes[column] <= (uint64_t)page_size && mprotect(guard, page_size, PROT_NONE) == 0;
                    columns[column] = guard - bytes[column];
                    if (valid && bytes[column] != 0)
                    {
                        memcpy(columns[column], sources[column], bytes[column]);
                    }
                }
                else
                {
                    valid = false;
                }
            }
            if (ssa_test_expect(tally, valid, "create guarded columns"))
            {
                guarded.block_offsets = columns[0];
                guarded.successor_offsets = columns[1];
                guarded.successors = columns[2];
                guarded.kinds = columns[3];
                guarded.types = columns[4];
                guarded.locals = columns[5];
                guarded.operands = columns[6];
                guarded.local_types = columns[7];
                guarded.local_flags = columns[8];
                ssa_test_run(tally, space, &guarded, true, false);
            }
            for (uint32_t column = 0; column < 9; column += 1)
            {
                if (mappings[column] != MAP_FAILED)
                {
                    munmap(mappings[column], (uint64_t)page_size * 2);
                }
            }
        }
    }
}

static void ssa_test_reject(SsaTestTally* tally, SsaTestSpace* space, const SsaInput* input)
{
    tally->blocks = input->block_count;
    tally->locals = input->local_count;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        space->output.length = 37;
        space->scratch.length = 29;
        SsaResult before;
        memset(&before, 0x5a, sizeof(before));
        SsaResult result = before;
        const SsaError error = path == 0 ? ssa_build(input, &space->output, &space->scratch, &result) :
                                         ssa_build_scalar(input, &space->output, &space->scratch, &result);
        ssa_test_expect(tally, error == SSA_ERROR_INVALID || error == SSA_ERROR_LIMIT, "invalid input refused");
        ssa_test_expect(tally, space->output.length == 37 && space->scratch.length == 29 && memcmp(&before, &result, sizeof(result)) == 0,
                        "invalid input preserves output, scratch and result");
    }
}

static void ssa_test_invalid(SsaTestTally* tally, SsaTestSpace* space)
{
    SsaTestFixture fixture;
    SsaInput invalid;
    tally->name = "missing block offsets";
    ssa_test_diamond(&fixture, true, true);
    invalid = fixture.input;
    invalid.block_offsets = 0;
    ssa_test_reject(tally, space, &invalid);
    tally->name = "bad instruction range";
    fixture.block_offsets[2] = fixture.input.instruction_count + 1;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "edge outside graph";
    ssa_test_diamond(&fixture, true, true);
    fixture.successors[0] = fixture.input.block_count;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "backedge into entry";
    fixture.successors[0] = 0;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "unknown instruction kind";
    ssa_test_diamond(&fixture, true, true);
    fixture.kinds[0] = BUSTER_SSA_KINDS;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "missing instruction column";
    ssa_test_diamond(&fixture, true, true);
    invalid = fixture.input;
    invalid.operands = 0;
    ssa_test_reject(tally, space, &invalid);
    tally->name = "local outside table";
    fixture.locals[2] = fixture.input.local_count;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "unknown local flags";
    ssa_test_diamond(&fixture, true, true);
    fixture.local_flags[0] = 0x80;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "void local type";
    fixture.local_flags[0] = 0;
    fixture.local_types[0] = BUSTER_SSA_TYPE_VOID;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "write operand does not define a value";
    ssa_test_diamond(&fixture, true, true);
    fixture.operands[2] = 2;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "write operand from a future instruction";
    fixture.operands[2] = fixture.input.instruction_count - 1;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "write operand type mismatch";
    fixture.operands[2] = 0;
    fixture.types[0] = BUSTER_SSA_TYPE_F64;
    ssa_test_reject(tally, space, &fixture.input);
    tally->name = "empty CFG with locals";
    ssa_test_fixture_begin(&fixture, 0, 1);
    ssa_test_reject(tally, space, &fixture.input);
}

static void ssa_test_capacity(SsaTestTally* tally, SsaTestSpace* space)
{
    SsaTestFixture fixture;
    ssa_test_diamond(&fixture, false, true);
    tally->name = "arena failure rollback";
    const uint64_t capacity = space->output.capacity;
    const uint64_t scratch_capacity = space->scratch.capacity;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t small = 0; small < 2; small += 1)
        {
            bool reached_success = false;
            for (uint64_t room = 0; room <= 65536 && !reached_success; room = room == 0 ? 1 : room * 2)
            {
                space->output.length = 37;
                space->scratch.length = 29;
                space->output.capacity = small == 0 ? 37 + room : capacity;
                space->scratch.capacity = small == 1 ? 29 + room : scratch_capacity;
                memset(space->output.base, 0xab, 37);
                memset(space->scratch.base, 0xcd, 29);
                SsaResult before;
                memset(&before, 0x5a, sizeof(before));
                SsaResult result = before;
                const SsaError error = path == 0 ? ssa_build(&fixture.input, &space->output, &space->scratch, &result) :
                                                 ssa_build_scalar(&fixture.input, &space->output, &space->scratch, &result);
                reached_success = error == SSA_OK;
                if (!reached_success)
                {
                    ssa_test_expect(tally, error == SSA_ERROR_MEMORY, "capacity exhaustion is a memory error");
                    ssa_test_expect(tally, space->output.length == 37 && space->scratch.length == 29 && memcmp(&before, &result, sizeof(result)) == 0,
                                    "allocation failure preserves lengths and result");
                }
                bool prefixes = space->scratch.length == 29;
                for (uint32_t i = 0; i < 37; i += 1)
                {
                    prefixes &= space->output.base[i] == 0xab;
                }
                for (uint32_t i = 0; i < 29; i += 1)
                {
                    prefixes &= space->scratch.base[i] == 0xcd;
                }
                ssa_test_expect(tally, prefixes, "preexisting arena bytes are preserved");
            }
            ssa_test_expect(tally, reached_success, "construction succeeds with sufficient capacity");
            space->output.capacity = capacity;
            space->scratch.capacity = scratch_capacity;
        }
    }
    space->output.length = 0;
    space->scratch.length = 0;
}

static void ssa_test_verifier(SsaTestTally* tally, SsaTestSpace* space)
{
    SsaTestFixture fixture;
    ssa_test_diamond(&fixture, true, true);
    tally->name = "verifier rejects semantic corruption";
    arena_pop_to(&space->output, 0);
    arena_pop_to(&space->scratch, 0);
    SsaResult result = { 0 };
    if (ssa_test_expect(tally, ssa_build(&fixture.input, &space->output, &space->scratch, &result) == SSA_OK && result.phi_count == 1, "build verifier fixture"))
    {
        const uint32_t first = result.phi_offsets[0];
        const uint32_t last = result.phi_offsets[1] - 1;
        const uint32_t saved_first = result.phi_operands[first];
        const uint32_t saved_last = result.phi_operands[last];
        result.phi_operands[first] = saved_last;
        result.phi_operands[last] = saved_first;
        ssa_test_expect(tally, ssa_verify(&fixture.input, &result, &space->scratch).error != SSA_OK, "swapped dominating values violate reaching definitions");
        ssa_test_expect(tally, !ssa_test_interpret(&fixture.input, &result, &space->oracle, tally->seed), "interpreter detects swapped phi operands");
        result.phi_operands[first] = saved_first;
        result.phi_operands[last] = saved_last;
        const uint32_t read = fixture.input.instruction_count - 1;
        const uint32_t saved_read = result.replacements[read];
        result.replacements[read] = 0;
        ssa_test_expect(tally, ssa_verify(&fixture.input, &result, &space->scratch).error != SSA_OK, "wrong dominating replacement rejected");
        ssa_test_expect(tally, !ssa_test_interpret(&fixture.input, &result, &space->oracle, tally->seed), "interpreter detects incorrect read replacement");
        result.replacements[read] = saved_read;
        const uint32_t saved_edge = result.predecessor_edges[0];
        result.predecessor_edges[0] = BUSTER_SSA_NONE;
        ssa_test_expect(tally, ssa_verify(&fixture.input, &result, &space->scratch).error != SSA_OK, "invalid predecessor edge rejected");
        result.predecessor_edges[0] = saved_edge;
        ssa_test_expect(tally, ssa_verify(&fixture.input, &result, &space->scratch).error == SSA_OK, "restored result verifies");
    }
}

static int run_ssa_tests(int argc, char** argv)
{
    uint64_t seed = 134;
    uint32_t count = 100;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool is_seed = strcmp(argv[i], "--seed") == 0;
        const bool is_count = strcmp(argv[i], "--count") == 0;
        valid = (is_seed || is_count) && i + 1 < argc;
        if (valid)
        {
            char* end = 0;
            errno = 0;
            const unsigned long long value = strtoull(argv[++i], &end, 10);
            valid = errno == 0 && end != argv[i] && *end == 0 && argv[i][0] != '-' && (is_seed || value <= 100000);
            if (is_seed)
            {
                seed = value;
            }
            else
            {
                count = (uint32_t)value;
            }
        }
    }
    int status = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-ssa [--seed N] [--count N]\n");
    }
    else
    {
        SsaTestSpace space = {
            .input = arena_reserve(BUSTER_SSA_TEST_ARENA_BYTES),
            .output = arena_reserve(BUSTER_SSA_TEST_ARENA_BYTES),
            .twin = arena_reserve(BUSTER_SSA_TEST_ARENA_BYTES),
            .scratch = arena_reserve(BUSTER_SSA_TEST_ARENA_BYTES),
            .oracle = arena_reserve(BUSTER_SSA_TEST_ARENA_BYTES),
        };
        SsaTestTally tally = {
            .seed = seed,
            .name = "test arenas",
        };
        if (ssa_test_expect(&tally, space.input.base != 0 && space.output.base != 0 && space.twin.base != 0 && space.scratch.base != 0 && space.oracle.base != 0,
                            "reserve arenas"))
        {
            ssa_test_fixtures(&tally, &space);
            ssa_test_generated(&tally, &space, count);
            ssa_test_guards(&tally, &space);
            ssa_test_invalid(&tally, &space);
            ssa_test_capacity(&tally, &space);
            ssa_test_verifier(&tally, &space);
        }
        printf("test-ssa: %" PRIu64 " checks, %" PRIu64 " failures (seed=%" PRIu64 ", random=%u)\n", tally.checks, tally.failures, seed, count);
        status = tally.failures != 0;
        arena_release(&space.oracle);
        arena_release(&space.scratch);
        arena_release(&space.twin);
        arena_release(&space.output);
        arena_release(&space.input);
    }
    return status;
}
