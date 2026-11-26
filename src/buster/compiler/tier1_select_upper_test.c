// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #3625: exact register-number and target boundaries independent of the CFG allocator's chosen registers.

// Force every possible upper destination, including aliases of each live input, through the real encoder and CPU.
// Ordinary unsigned C arithmetic supplies the result; no selector or interpreter helper computes the expectation.
static void tier1_select_upper_native(Tier1SelectTest* test, const uint8_t* allowed, const uint8_t* native)
{
    Tier1SelectTest probe = {
        .executable = test->executable,
        .allowed = (uint8_t*)allowed,
        .native_allowed = native,
    };
    uint64_t random = UINT64_C(0x3625f17e4b88c9a1);
    for (uint32_t width = 16; width <= 64; width *= 2)
    {
        const Tier1SelectFloatForms* moves = tier1_select_class_forms(width, BUSTER_TIER1_ENCODING_EVEX);
        for (uint32_t reg = 16; reg < 32; reg += 1)
        {
            const uint8_t operands[3] = {
                (uint8_t)(16 + ((reg + 1) & 15)),
                (uint8_t)(16 + ((reg + 2) & 15)),
                (uint8_t)(16 + ((reg + 3) & 15)),
            };
            for (uint32_t alias = 0; alias < 4; alias += 1)
            {
                const uint8_t destination = alias == 0 ? (uint8_t)reg : operands[alias - 1];
                for (uint32_t operation = 0; operation < 9; operation += 1)
                {
                    static const uint8_t conversions[4] = { IR_SITOFP, IR_UITOFP, IR_FPTOSI, IR_FPTOUI };
                    const Tier1SelectHoles holes = {
                        .rule = operation < 5 ? TIER1_RULE_VECTOR : TIER1_RULE_VECTOR_CONVERT,
                        .operation = operation >= 5 ? conversions[operation - 5] : operation == 2 ? BUSTER_TIER1_VECTOR_SUB : BUSTER_TIER1_VECTOR_ADD,
                        .immediate = 0xca,
                        .scale = operation == 0 ? BUSTER_TIER1_VECTOR_COPY : operation == 3 ? BUSTER_TIER1_VECTOR_NOT :
                                 operation == 4 ? BUSTER_TIER1_VECTOR_TERNLOG : BUSTER_TIER1_VECTOR_BINARY,
                        .width = (uint8_t)width,
                        .source_width = IR_SIMD_LANE_I32,
                        .destination = (uint8_t)(32 + destination),
                        .left = (uint8_t)(32 + operands[0]),
                        .right = (uint8_t)(32 + operands[1]),
                        .third = (uint8_t)(32 + operands[2]),
                    };
                    const Tier1SelectRecords selected = tier1_select_expand(&holes);
                    X86Instruction records[BUSTER_TIER1_SELECT_RECORDS_PER_NODE + 5];
                    uint32_t count = 0;
                    for (uint32_t input = 0; input < 3; input += 1)
                    {
                        records[count++] = (X86Instruction){
                            .form = moves->load,
                            .reg = operands[input],
                            .rm = X86_REGISTER_RDI,
                            .displacement = (int32_t)(64 * input),
                            .mode = BUSTER_X86_MEMORY_BASE,
                        };
                    }
                    memcpy(records + count, selected.records, sizeof(*records) * selected.count);
                    count += selected.count;
                    const uint32_t end = count;
                    records[count++] = (X86Instruction){
                        .form = moves->store,
                        .reg = destination,
                        .rm = X86_REGISTER_RDI,
                        .displacement = 192,
                        .mode = BUSTER_X86_MEMORY_BASE,
                    };
                    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                    const bool ready = !selected.overflowed && tier1_select_test_load_records(&probe, records, count, 3, end, false);
                    for (uint32_t round = 0; ready && round < 4; round += 1)
                    {
                        uint32_t state[4][16];
                        uint32_t expected[16];
                        for (uint32_t input = 0; input < 3; input += 1)
                        {
                            for (uint32_t lane = 0; lane < 16; lane += 1)
                            {
                                state[input][lane] = (uint32_t)tier1_select_test_random(&random);
                            }
                        }
                        memset(state[3], 0xa5, sizeof(state[3]));
                        memset(expected, 0xa5, sizeof(expected));
                        for (uint32_t lane = 0; lane < width / 4; lane += 1)
                        {
                            const uint32_t a = state[0][lane];
                            const uint32_t b = state[1][lane];
                            const uint32_t c = state[2][lane];
                            if (operation < 5)
                            {
                                expected[lane] = operation == 0 ? a : operation == 1 ? a + b : operation == 2 ? a - b :
                                                 operation == 3 ? ~a : (a & b) | (~a & c);
                            }
                            else if (operation < 7)
                            {
                                // Keep these integers exactly representable, so no ambient rounding mode enters the oracle.
                                const int32_t integer = (int32_t)(a & 0xffff) - (operation == 5 ? 32768 : 0);
                                const float converted = (float)integer;
                                state[0][lane] = (uint32_t)integer;
                                memcpy(expected + lane, &converted, sizeof(converted));
                            }
                            else
                            {
                                const float input = (float)((int32_t)(a & 0xffff) - (operation == 7 ? 32768 : 0)) + 0.25f;
                                memcpy(&state[0][lane], &input, sizeof(input));
                                expected[lane] = operation == 7 ? (uint32_t)(int32_t)input : (uint32_t)input;
                            }
                        }
                        tier1_select_test_call(&probe, (uint64_t)(uintptr_t)state, 0, 0);
                        tier1_select_test_expect(&probe, memcmp(state[3], expected, sizeof(expected)) == 0,
                                                 "upper-register kernels and alias repairs match independent C lane results");
                    }
                }
            }
        }
    }
    test->checks += probe.checks;
    test->failures += probe.failures;
    test->native_skipped += probe.native_skipped;
}

// Repeating the multiplicand as the addend must keep both reads across the destination's initial copy. The old
// packed-FMA repair renamed only the first read when destination == a == c, producing a*b+b instead of a*b+a.
static void tier1_select_upper_fma_native(Tier1SelectTest* test, const uint8_t* allowed, const uint8_t* native)
{
    Tier1SelectTest probe = {
        .executable = test->executable,
        .allowed = (uint8_t*)allowed,
        .native_allowed = native,
    };
    uint64_t random = UINT64_C(0xf3625a11a5c38e91);
    for (uint32_t width = 16; width <= 64; width *= 2)
    {
        const Tier1SelectFloatForms* moves = tier1_select_class_forms(width, BUSTER_TIER1_ENCODING_EVEX);
        for (uint32_t bytes = 4; bytes <= 8; bytes *= 2)
        {
            for (uint32_t upper = 0; upper < 2; upper += 1)
            {
                const uint8_t separate = upper != 0 ? 29 : 2;
                const uint8_t a = (uint8_t)(separate + 1);
                const uint8_t b = (uint8_t)(separate + 2);
                for (uint32_t alias = 0; alias < 3; alias += 1)
                {
                    const uint8_t destination = alias == 0 ? separate : alias == 1 ? a : b;
                    const Tier1SelectHoles holes = {
                        .rule = TIER1_RULE_VECTOR_FMA,
                        .width = (uint8_t)width,
                        .source_width = bytes == 8 ? IR_SIMD_LANE_F64 : IR_SIMD_LANE_F32,
                        .destination = (uint8_t)(32 + destination),
                        .left = (uint8_t)(32 + a),
                        .right = (uint8_t)(32 + b),
                        .third = (uint8_t)(32 + a),
                    };
                    const Tier1SelectRecords selected = tier1_select_expand(&holes);
                    X86Instruction records[BUSTER_TIER1_SELECT_RECORDS_PER_NODE + 4] = {
                        { .form = moves->load, .reg = a, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE },
                        { .form = moves->load, .reg = b, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 },
                    };
                    uint32_t count = 2;
                    memcpy(records + count, selected.records, sizeof(*records) * selected.count);
                    count += selected.count;
                    const uint32_t end = count;
                    records[count++] = (X86Instruction){
                        .form = moves->store,
                        .reg = destination,
                        .rm = X86_REGISTER_RDI,
                        .mode = BUSTER_X86_MEMORY_BASE,
                        .displacement = 128,
                    };
                    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                    const bool ready = !selected.overflowed && tier1_select_test_load_records(&probe, records, count, 2, end, false);
                    for (uint32_t round = 0; ready && round < 4; round += 1)
                    {
                        uint8_t state[3][64];
                        uint8_t expected[64];
                        memset(state, 0xa5, sizeof(state));
                        memset(expected, 0xa5, sizeof(expected));
                        for (uint32_t offset = 0; offset < width; offset += bytes)
                        {
                            // Small integers and their products are exact in either precision, including the fused sum.
                            const int32_t first = (int32_t)(tier1_select_test_random(&random) & 31) - 16;
                            const int32_t second = (int32_t)(tier1_select_test_random(&random) & 31) - 16;
                            if (bytes == 4)
                            {
                                const float inputs[2] = { (float)first, (float)second };
                                const float result = (float)(first * second + first);
                                memcpy(state[0] + offset, inputs, sizeof(float));
                                memcpy(state[1] + offset, inputs + 1, sizeof(float));
                                memcpy(expected + offset, &result, sizeof(result));
                            }
                            else
                            {
                                const double inputs[2] = { (double)first, (double)second };
                                const double result = (double)(first * second + first);
                                memcpy(state[0] + offset, inputs, sizeof(double));
                                memcpy(state[1] + offset, inputs + 1, sizeof(double));
                                memcpy(expected + offset, &result, sizeof(result));
                            }
                        }
                        tier1_select_test_call(&probe, (uint64_t)(uintptr_t)state, 0, 0);
                        tier1_select_test_expect(&probe, memcmp(state[2], expected, sizeof(expected)) == 0,
                                                 "packed FMA keeps both repeated operands when the destination aliases them");
                    }
                }
            }
        }
    }
    test->checks += probe.checks;
    test->failures += probe.failures;
    test->native_skipped += probe.native_skipped;
}

static void tier1_select_upper_tests(Tier1SelectTest* test)
{
    uint8_t* forms = malloc(4u * X86_FORM_COUNT);
    X86Target v4 = { 0 };
    X86Target v3 = { 0 };
    const bool ready = forms != 0 && x86_target_parse("x86-64-v4", 9, &v4) && x86_target_parse("x86-64-v3", 9, &v3);
    tier1_select_test_expect(test, ready, "upper vector qualification targets");
    if (ready)
    {
        X86Target foundation = v3;
        foundation.features |= BUSTER_X86_FEATURE(AVX512F);
        x86_target_forms(&v4, forms);
        x86_target_forms(&v3, forms + X86_FORM_COUNT);
        x86_target_forms(&foundation, forms + 2u * X86_FORM_COUNT);
        const X86Target host = x86_target_native();
        x86_target_forms(&host, forms + 3u * X86_FORM_COUNT);
        tier1_select_upper_native(test, forms, forms + 3u * X86_FORM_COUNT);
        tier1_select_upper_fma_native(test, forms, forms + 3u * X86_FORM_COUNT);
        uint8_t types[5] = { 0 };
        uint8_t operations[5] = { IR_ADD, IR_LOAD, IR_LOAD, IR_LOAD, IR_LOAD };
        IrFunction function = {
            .instruction_count = 5,
            .op = operations,
            .type = types,
        };
        Tier1SelectPattern pattern = {
            .rule = TIER1_RULE_VECTOR,
            .operation = BUSTER_TIER1_VECTOR_ADD,
            .scale = BUSTER_TIER1_VECTOR_BINARY,
            .values = { 1, 2, BUSTER_IR_NONE, BUSTER_IR_NONE },
            .source_width = IR_SIMD_LANE_I32,
        };
        const Tier1SelectPlan plan = { .function = &function, .patterns = &pattern };
        for (uint32_t width = 16; width <= 64; width *= 2)
        {
            const uint8_t type = width == 16 ? IR_V4I32 : width == 32 ? IR_V8I32 : IR_V16I32;
            memset(types, type, sizeof(types));
            pattern.width = (uint8_t)width;
            tier1_select_test_expect(test, tier1_select_upper_vector_roles(&plan, 0, forms, BUSTER_TIER1_ENCODING_EVEX, true) == 7,
                                     "EVEX arithmetic qualifies its result and both vector operands");
            tier1_select_test_expect(test, tier1_select_upper_vector_roles(&plan, 0, forms + X86_FORM_COUNT, BUSTER_TIER1_ENCODING_VEX, true) == 0,
                                     "an AVX2 output never allocates upper vector registers");
            tier1_select_test_expect(test, tier1_select_upper_vector_roles(&plan, 0, forms + 2u * X86_FORM_COUNT, BUSTER_TIER1_ENCODING_VEX, true) == (width == 64 ? 7u : 0u),
                                     "AVX512F without VL qualifies only full zmm transport");
            for (uint32_t reg = 16; reg < 32; reg += 1)
            {
                const uint8_t left = (uint8_t)(16 + ((reg + 1) & 15));
                const uint8_t right = (uint8_t)(16 + ((reg + 2) & 15));
                const Tier1SelectHoles holes = {
                    .rule = TIER1_RULE_VECTOR,
                    .operation = BUSTER_TIER1_VECTOR_ADD,
                    .scale = BUSTER_TIER1_VECTOR_BINARY,
                    .width = (uint8_t)width,
                    .source_width = IR_SIMD_LANE_I32,
                    .destination = (uint8_t)(32 + reg),
                    .left = (uint8_t)(32 + left),
                    .right = (uint8_t)(32 + right),
                };
                const Tier1SelectRecords selected = tier1_select_expand(&holes);
                const bool exact = selected.count == 1 && selected.records[0].reg == reg && selected.records[0].vvvv == left && selected.records[0].rm == right;
                tier1_select_test_expect(test, exact && x86_target_validate(forms, selected.records, selected.count) == selected.count,
                                         "every high register's five bits reach the encoded vector kernel");
                const X86Instruction move = tier1_select_class_move((uint8_t)reg, 1, width, BUSTER_TIER1_ENCODING_EVEX);
                tier1_select_test_expect(test, move.reg == reg && move.rm == 1 && x86_target_validate(forms, &move, 1) == 1 &&
                                         x86_form_description(move.form)->family == BUSTER_X86_FAMILY_EVEX,
                                         "upper-register parallel moves use a full-width EVEX form");
            }
        }
        types[0] = IR_F64;
        tier1_select_test_expect(test, !tier1_select_upper_vector_type(IR_F32, forms, BUSTER_TIER1_ENCODING_EVEX) &&
                                 !tier1_select_upper_vector_type(IR_F64, forms, BUSTER_TIER1_ENCODING_EVEX),
                                 "scalar SSE kernels and saves keep their operands low");
        memset(types, IR_V8I32, sizeof(types));
        pattern.width = 32;
        pattern.scale = BUSTER_TIER1_VECTOR_BLENDV;
        pattern.values[2] = 3;
        tier1_select_test_expect(test, tier1_select_upper_vector_roles(&plan, 0, forms, BUSTER_TIER1_ENCODING_EVEX, true) == 0,
                                 "VEX blendv constrains its encoded immediate register as well as ordinary operands");
        pattern.scale = BUSTER_TIER1_VECTOR_MOVEMASK;
        pattern.values[1] = BUSTER_IR_NONE;
        pattern.values[2] = BUSTER_IR_NONE;
        pattern.immediate = 0;
        types[0] = IR_I32;
        tier1_select_test_expect(test, tier1_select_upper_vector_roles(&plan, 0, forms, BUSTER_TIER1_ENCODING_EVEX, true) == 0,
                                 "a VEX movemask consumer constrains an otherwise EVEX-capable vector value");
        pattern = (Tier1SelectPattern){
            .rule = TIER1_RULE_FLOAT_LOAD,
            .width = 16,
            .source_width = 8,
            .values = { 1, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE },
        };
        types[0] = IR_V4I32;
        types[1] = IR_PTR;
        tier1_select_test_expect(test, tier1_select_upper_vector_roles(&plan, 0, forms, BUSTER_TIER1_ENCODING_EVEX, true) == 0,
                                 "paired movsd/movhps local loads keep their vector result low");
        const Tier1SelectHoles paired = {
            .rule = TIER1_RULE_FLOAT_LOAD,
            .width = 16,
            .source_width = 8,
            .destination = BUSTER_TIER1_SELECT_NO_REGISTER,
            .memory = { .mode = BUSTER_X86_MEMORY_BASE },
        };
        const Tier1SelectRecords unused = tier1_select_expand(&paired);
        tier1_select_test_expect(test, !unused.overflowed && unused.count == 2 && unused.records[0].reg == BUSTER_TIER1_SELECT_FLOAT_SCRATCH &&
                                 unused.records[1].reg == BUSTER_TIER1_SELECT_FLOAT_SCRATCH,
                                 "unused vector results still use the reserved scratch rather than xmm31");
    }
    free(forms);
}
