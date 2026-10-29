// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original fixed-width semantic controls. Clang's C vector operators provide an independent arithmetic result;
// explicit arrays/patterns provide comparison, selection, mask-tail and bounded-memory expectations. No generated
// assembly or compiler-host feature bit is an admission oracle. Include after ir_simd_model.c and stdio.h.

typedef struct IrSimdModelTest IrSimdModelTest;
struct IrSimdModelTest
{
    uint64_t checks;
    uint64_t failures;
    FILE* diagnostics;
};

static void ir_simd_model_expect(IrSimdModelTest* test, bool valid, const char* description)
{
    test->checks += 1;
    if (!valid)
    {
        if (test->diagnostics != NULL && test->failures < 12)
        {
            fprintf(test->diagnostics, "IR SIMD model: %s\n", description);
        }
        test->failures += 1;
    }
}

static void ir_simd_model_arithmetic(IrSimdModelTest* test)
{
    typedef uint32_t ReferenceVector __attribute__((ext_vector_type(16)));
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        const uint32_t lanes = 4u << width;
        for (uint32_t sample = 0; sample < 73; sample += 1)
        {
            ReferenceVector a = { 0 };
            ReferenceVector b = { 0 };
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                a[lane] = (sample + lane * 17) * UINT32_C(0xfedcba97);
                b[lane] = (sample * 7 + lane + 1) * UINT32_C(0x81234567);
            }
            IrSimdValue inputs[2] = { 0 };
            memcpy(inputs[0].words, &a, lanes * sizeof(uint32_t));
            memcpy(inputs[1].words, &b, lanes * sizeof(uint32_t));
            const IrSimdValue* values[3] = { inputs, inputs + 1, NULL };
            const IrSimdOperation operations[] = { IR_SIMD_ADD, IR_SIMD_SUB, IR_SIMD_MUL, IR_SIMD_AND, IR_SIMD_OR, IR_SIMD_XOR };
            for (uint32_t operation = 0; operation < sizeof(operations) / sizeof(*operations); operation += 1)
            {
                ReferenceVector expected = { 0 };
                switch (operations[operation])
                {
                    case IR_SIMD_ADD: expected = a + b; break;
                    case IR_SIMD_SUB: expected = a - b; break;
                    case IR_SIMD_MUL: expected = a * b; break;
                    case IR_SIMD_AND: expected = a & b; break;
                    case IR_SIMD_OR: expected = a | b; break;
                    case IR_SIMD_XOR: expected = a ^ b; break;
                    default: break;
                }
                const IrSimdAction action = { .operation = operations[operation], .result = shape, .operands = { shape, shape } };
                IrSimdValue actual = { 0 };
                const bool valid = ir_simd_evaluate(&action, values, NULL, NULL, 0, &actual);
                ir_simd_model_expect(test, valid && memcmp(&actual, &expected, lanes * sizeof(uint32_t)) == 0 &&
                                    ir_simd_value_valid(&actual, shape), "integer lanes agree with independent C vector arithmetic, including wrap and zero tails");
            }
        }
    }
}

static void ir_simd_model_masks(IrSimdModelTest* test)
{
    const uint32_t a[16] = { UINT32_MAX, 0, 7, UINT32_C(0x80000000), 17, 17, 99, 0, 6, 7, 8, 9, 10, 11, 12, 13 };
    const uint32_t b[16] = { 0, 0, 6, UINT32_C(0x7fffffff), 17, 18, 2, UINT32_MAX, 9, 8, 7, 6, 5, 4, 3, 2 };
    for (uint32_t width = 0; width < 3; width += 1)
    {
        const IrSimdShape shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        const IrSimdShape mask = width == 2 ? IR_SIMD_M16 : IR_SIMD_M8;
        const uint32_t lanes = 4u << width;
        IrSimdValue inputs[3] = { 0 };
        memcpy(inputs[0].words, a, lanes * sizeof(uint32_t));
        memcpy(inputs[1].words, b, lanes * sizeof(uint32_t));
        const IrSimdValue* operands[3] = { inputs, inputs + 1, NULL };
        IrSimdValue compared = { 0 };
        for (uint32_t predicate = 0; predicate < IR_SIMD_PREDICATE_COUNT; predicate += 1)
        {
            const IrSimdAction compare = { .operation = IR_SIMD_COMPARE, .result = mask, .operands = { shape, shape }, .predicate = (IrSimdPredicate)predicate };
            uint64_t expected = 0;
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                int32_t signed_a = 0;
                int32_t signed_b = 0;
                memcpy(&signed_a, a + lane, sizeof(signed_a));
                memcpy(&signed_b, b + lane, sizeof(signed_b));
                bool relation = false;
                switch (predicate)
                {
                    case IR_SIMD_EQ: relation = a[lane] == b[lane]; break;
                    case IR_SIMD_NE: relation = a[lane] != b[lane]; break;
                    case IR_SIMD_LT: relation = signed_a < signed_b; break;
                    case IR_SIMD_LE: relation = signed_a <= signed_b; break;
                    case IR_SIMD_GT: relation = signed_a > signed_b; break;
                    case IR_SIMD_GE: relation = signed_a >= signed_b; break;
                    case IR_SIMD_ULT: relation = a[lane] < b[lane]; break;
                    case IR_SIMD_ULE: relation = a[lane] <= b[lane]; break;
                    case IR_SIMD_UGT: relation = a[lane] > b[lane]; break;
                    case IR_SIMD_UGE: relation = a[lane] >= b[lane]; break;
                    default: break;
                }
                expected |= (uint64_t)relation << lane;
            }
            ir_simd_model_expect(test, ir_simd_evaluate(&compare, operands, NULL, NULL, 0, &compared) && compared.words[0] == expected &&
                                ir_simd_value_valid(&compared, mask), "every signed/unsigned comparison creates a width-qualified mask with unused bits clear");
        }
        compared.words[0] = UINT64_C(0xaa);
        const IrSimdValue* selected[3] = { &compared, inputs, inputs + 1 };
        const IrSimdAction select = { .operation = IR_SIMD_SELECT, .result = shape, .operands = { mask, shape, shape } };
        IrSimdValue actual = { 0 };
        uint32_t wanted[16] = { 0 };
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            wanted[lane] = (UINT64_C(0xaa) & (UINT64_C(1) << lane)) != 0 ? a[lane] : b[lane];
        }
        ir_simd_model_expect(test, ir_simd_evaluate(&select, selected, NULL, NULL, 0, &actual) &&
                            memcmp(actual.words, wanted, lanes * sizeof(uint32_t)) == 0 && ir_simd_value_valid(&actual, shape),
                            "predication uses low mask bits and the explicit merge operand, including inactive lanes");
        selected[2] = inputs + 2;
        memset(wanted, 0, sizeof(wanted));
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            wanted[lane] = (UINT64_C(0xaa) & (UINT64_C(1) << lane)) != 0 ? a[lane] : 0;
        }
        ir_simd_model_expect(test, ir_simd_evaluate(&select, selected, NULL, NULL, 0, inputs) &&
                            memcmp(inputs[0].words, wanted, lanes * sizeof(uint32_t)) == 0, "an explicit zero merge supports zeroing and an aliased result");
    }
    for (uint32_t shape = IR_SIMD_M8; shape <= IR_SIMD_M64; shape += 1)
    {
        const uint32_t bits = 8u << (shape - IR_SIMD_M8);
        const uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
        const IrSimdValue input = { .words = { UINT64_C(0x55) } };
        const IrSimdValue* values[3] = { &input, NULL, NULL };
        const IrSimdAction action = { .operation = IR_SIMD_NOT, .result = (IrSimdShape)shape, .operands = { (IrSimdShape)shape } };
        IrSimdValue actual = { 0 };
        ir_simd_model_expect(test, ir_simd_evaluate(&action, values, NULL, NULL, 0, &actual) && actual.words[0] == (~UINT64_C(0x55) & mask),
                            "mask logic is width-qualified through 64 bits without scalar i1/i64 conflation");
        const IrSimdValue right = { .words = { UINT64_C(0x96) } };
        values[1] = &right;
        for (uint32_t operation = IR_SIMD_AND; operation <= IR_SIMD_XOR; operation += 1)
        {
            const IrSimdAction binary = { .operation = (IrSimdOperation)operation, .result = (IrSimdShape)shape, .operands = { (IrSimdShape)shape, (IrSimdShape)shape } };
            const uint64_t expected = operation == IR_SIMD_AND ? UINT64_C(0x14) : operation == IR_SIMD_OR ? UINT64_C(0xd7) : UINT64_C(0xc3);
            ir_simd_model_expect(test, ir_simd_evaluate(&binary, values, NULL, NULL, 0, &actual) && actual.words[0] == expected &&
                                ir_simd_value_valid(&actual, (IrSimdShape)shape), "binary mask logic preserves exact width and distinct bit patterns");
        }
    }
}

static void ir_simd_model_memory(IrSimdModelTest* test)
{
    uint8_t bytes[66];
    memset(bytes, 0xa5, sizeof(bytes));
    IrSimdMemory memory = { .bytes = bytes + 1, .size = 64 };
    IrSimdValue source = { .words = { 1, 2, 3, 4, 5, 6, 7, 8 } };
    const IrSimdValue* values[3] = { &source, NULL, NULL };
    IrSimdValue actual = { 0 };
    IrSimdAction store = { .operation = IR_SIMD_STORE, .operands = { IR_SIMD_V16I32 }, .volatile_memory = true };
    IrSimdAction load = { .operation = IR_SIMD_LOAD, .result = IR_SIMD_V16I32, .volatile_memory = true };
    const IrSimdValue* absent[3] = { NULL, NULL, NULL };
    bool valid = ir_simd_evaluate(&store, values, NULL, &memory, 0, &actual) &&
                 ir_simd_evaluate(&load, absent, NULL, &memory, 0, &actual);
    ir_simd_model_expect(test, valid && memcmp(&actual, &source, sizeof(source)) == 0 && memory.reads == 64 && memory.writes == 64 &&
                        memory.volatile_accesses == 128 && bytes[0] == 0xa5 && bytes[65] == 0xa5,
                        "bounded vector memory preserves bytes and explicit ordered volatile effects without touching guard tails");
    const IrSimdMemory before = memory;
    const IrSimdValue saved = actual;
    const uint8_t last = bytes[65];
    valid = !ir_simd_evaluate(&store, values, NULL, &memory, 1, &actual) && !ir_simd_evaluate(&load, absent, NULL, &memory, UINT64_MAX, &actual);
    ir_simd_model_expect(test, valid && memcmp(&memory, &before, sizeof(memory)) == 0 && memcmp(&actual, &saved, sizeof(saved)) == 0 && bytes[65] == last,
                        "out-of-bounds memory refuses transactionally before any access or effect");
    memory.writes = UINT64_MAX;
    ir_simd_model_expect(test, !ir_simd_evaluate(&store, values, NULL, &memory, 0, &actual) && memory.volatile_accesses == before.volatile_accesses,
                        "effect counter overflow cannot wrap or perform a store");
}

static IrSimdModelTest ir_simd_model_test(FILE* diagnostics)
{
    IrSimdModelTest test = { .diagnostics = diagnostics };
    ir_simd_model_arithmetic(&test);
    ir_simd_model_masks(&test);
    ir_simd_model_memory(&test);
    IrSimdAction action = { .operation = IR_SIMD_SELECT, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_M16, IR_SIMD_V4I32, IR_SIMD_V4I32 } };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "a wrong-width predicate is rejected even if all lanes would fit");
    action = (IrSimdAction){ .operation = IR_SIMD_ADD, .result = IR_SIMD_M32, .operands = { IR_SIMD_M32, IR_SIMD_M32 } };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "integer arithmetic cannot silently interpret an opmask as an ordinary integer");
    action = (IrSimdAction){ .operation = IR_SIMD_ADD, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_V4I32, IR_SIMD_V8I32 } };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "different vector shapes do not silently truncate or broadcast");
    action = (IrSimdAction){ .operation = IR_SIMD_COMPARE, .result = IR_SIMD_M8, .operands = { IR_SIMD_V4I32, IR_SIMD_V4I32 }, .predicate = IR_SIMD_PREDICATE_COUNT };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "unknown comparison predicates fail closed");
    action = (IrSimdAction){ .operation = IR_SIMD_COPY, .result = IR_SIMD_V4I32, .operands = { IR_SIMD_V4I32 }, .volatile_memory = true };
    ir_simd_model_expect(&test, !ir_simd_action_valid(&action), "memory effects cannot be attached to a pure operation");
    IrSimdValue malformed = { .words = { 1, 1 } };
    ir_simd_model_expect(&test, !ir_simd_value_valid(&malformed, IR_SIMD_M64) && !ir_simd_value_valid(NULL, IR_SIMD_V4I32) &&
                        ir_simd_bits((IrSimdShape)255) == 0, "invalid shape IDs and noncanonical mask payload tails are rejected");
    return test;
}
