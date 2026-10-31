// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// #775: baseline transport and destructive rows, with no AVX-512 forms permitted in the emitted records.
// Every object width and both selectors/encoders run against the independent stack model, including live floats
// through xmm15 and the deep path. Output-slot canaries detect writes beyond 4- and 8-byte user objects.

static void tier0_test_vector_baseline_program(Tier0TestVectorProgram* program, uint32_t object, uint32_t floats, uint64_t* state)
{
    memset(program, 0, sizeof(*program));
    const uint32_t bytes = tier0_test_vector_type_bytes(object);
    const uint32_t vector = BUSTER_TYPED_CLASS_VECTOR;
    if (floats != 0)
    {
        tier0_test_vector_address(program, 3, 256);
    }
    for (uint32_t f = 0; f < floats; f += 1)
    {
        tier0_test_vector_address(program, 2, 4 * (f % BUSTER_TIER0_TEST_VECTOR_FLOATS));
        tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F32, 0);
    }
    tier0_test_vector_address(program, 3, 0);
    tier0_test_vector_address(program, 0, 3);
    tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, vector, bytes);
    tier0_test_vector_emit(program, BUSTER_TYPED_DUP, vector, 0);
    tier0_test_vector_emit(program, BUSTER_TYPED_DISCARD, vector, 0);
    tier0_test_vector_emit(program, BUSTER_TYPED_STORE_LOCAL, vector, 4 + object);
    tier0_test_vector_emit(program, BUSTER_TYPED_SET, vector, bytes);
    tier0_test_vector_address(program, 3, 64);
    tier0_test_vector_emit(program, BUSTER_TYPED_LOCAL, vector, 4 + object);
    tier0_test_vector_emit(program, BUSTER_TYPED_STORE, vector, bytes);
    tier0_test_vector_emit(program, BUSTER_TYPED_SET_LOCAL, vector, 4 + object);
    tier0_test_vector_address(program, 3, 128);
    program->constant_count = 1;
    program->constant_bytes[0] = (uint8_t)bytes;
    tier0_test_vector_emit(program, BUSTER_TYPED_CONST, vector, 0);
    tier0_test_vector_emit(program, BUSTER_TYPED_DUP, vector, 0);
    tier0_test_vector_emit(program, BUSTER_TYPED_SET_LOCAL, vector, 4 + object);
    tier0_test_vector_emit(program, BUSTER_TYPED_SET, vector, bytes);
    tier0_test_vector_address(program, 3, 192);
    tier0_test_vector_constant(program, bytes, state, false);
    program->constants[8] = bytes == 4 ? UINT64_C(0x89abcdef) : UINT64_C(0x0123456789abcdef);
    tier0_test_vector_emit(program, BUSTER_TYPED_DUP, vector, 0);
    tier0_test_vector_emit(program, BUSTER_TYPED_DISCARD, vector, 0);
    tier0_test_vector_emit(program, BUSTER_TYPED_SET, vector, bytes);
    // Row zero uses the existing ADD_D independent model. Only its low 16 bytes are observable here.
    tier0_test_vector_address(program, 3, 320);
    tier0_test_vector_constant(program, 16, state, false);
    tier0_test_vector_constant(program, 16, state, false);
    tier0_test_vector_emit(program, BUSTER_TYPED_X86, vector, 0);
    tier0_test_vector_emit(program, BUSTER_TYPED_SET, vector, 16);
    for (uint32_t f = 1; f < floats; f += 1)
    {
        tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_F32);
        tier0_test_vector_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F32, 0);
    }
    if (floats != 0)
    {
        tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_F32);
        tier0_test_vector_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F32, 0);
    }
    tier0_test_vector_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
}

static void tier0_test_vector_baselines(Tier0TestTally* tally, Tier0TestVectorShared* shared, uint64_t seed)
{
    static const uint8_t floating_depths[] = { 0, 1, 14, 15, 16 };
    Tier0TestVectorProgram* program = calloc(1, sizeof(*program));
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 26);
    const bool ready = tier0_test_expect(tally, program != NULL && image != NULL && arena.base != NULL, "vectors", "baseline fixture allocation");
    for (uint32_t mode = 16; ready && mode <= 32; mode += 16)
    {
        const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
        const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
        Tier0Operation row = { .record_count = 1, .operand_count = 2, .result_class = BUSTER_TYPED_CLASS_VECTOR, .result_bytes = 16,
                              .operand_classes = { BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR }, .operand_bytes = { 16, 16 } };
        row.records[0] = tier0_test_vector_record(mode == 16 ? X86_FORM_PADDD_X_XM128 : X86_FORM_VPADDD_X_X_XM128, v0, mode == 16 ? 0 : v0, v1);
        for (uint32_t object = 0; object < BUSTER_TIER0_TEST_VECTOR_OBJECTS; object += 1)
        {
            for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(floating_depths); f += 1)
            {
                uint64_t state = seed ^ mode ^ (object << 8) ^ (f << 16);
                tier0_test_vector_baseline_program(program, object, floating_depths[f], &state);
                Tier0Function function = tier0_test_vector_function(program);
                function.vector_bytes = (uint8_t)mode;
                function.operations = &row;
                function.operation_count = 1;
                arena_pop_to(&arena, 0);
                char name[96];
                snprintf(name, sizeof(name), "baseline %u-byte registers, %u-byte object, %u live floats", mode, tier0_test_vector_type_bytes(object), floating_depths[f]);
                Tier0Error error = TIER0_OK;
                const uint32_t length = tier0_test_vector_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, TIER0_OK, &error);
                if (length != 0)
                {
                    tier0_test_vector_inputs(shared, &state);
                    tier0_test_vector_check(tally, name, &function, image, length, shared, NULL);
                }
            }
        }
    }
    arena_release(&arena);
    free(image);
    free(program);
}
