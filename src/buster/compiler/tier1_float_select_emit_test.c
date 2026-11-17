// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2791: selects of floating comparisons through the CFG consumer, included by tier1_emit_test.c after
// tier1_branch_layout_test.c. Each function takes the bits of two F32 or F64 values and two I32 or I64 values, compares
// the floating values by one of C's six predicates, selects one of the others by the comparison directly, through its I1
// copy (if-conversion's truth simplification's) or through one of C's truth values' links, `ne` or `eq`, and returns it.
// With `summed`, the function returns the select plus one instead, so that the select's result takes a tie to a value's
// register (tier1_select_ties) as the return's doesn't. With `branch`, it branches on the condition instead, to blocks
// that return each value: a branch reaches its comparison through the copy too, which the truth walk passes for both. By
// both paths, with every general register and with the parameters' alone, and with and without a kept frame pointer, the
// select must be TIER1_RULE_FLOAT_SELECT_COMPARE and the branch TIER1_RULE_FLOAT_BRANCH_COMPARE for an ordered predicate
// in every shape, and test the comparison's truth value for == and !=; the code must certify and set no flag into a
// register where it fuses, a summed select with every register must take the register of the value it keeps (no move of
// its own), and the native result must be C's for NaN, signed zero, infinite and finite operands.

#define BUSTER_TIER1_FLOAT_SELECT_SHAPES 4u

// `shape`: the comparison itself (0), its copy (1), a link `icmp ne (zext c), 0` (2) or `icmp eq (zext c), 0` (3).
static bool tier1_float_select_function(Tier1EmitTest* test, uint32_t source_width, uint32_t width, uint32_t predicate, uint32_t shape, bool summed, bool branch, IrFunction* function,
                                        uint32_t* root)
{
    const uint8_t type = width == 4 ? IR_I32 : IR_I64;
    const uint8_t float_type = source_width == 4 ? IR_F32 : IR_F64;
    const IrSizes sizes =
    {
        .instruction_count = 16,
        .block_count = branch ? 3 : 1,
        .constant_count = 2,
        .parameter_count = 4,
    };
    const bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = type;
        function->parameter_types[0] = IR_I64;
        function->parameter_types[1] = IR_I64;
        function->parameter_types[2] = type;
        function->parameter_types[3] = type;
        function->constants[0].low = 0;
        function->constants[1].low = 1;
        uint32_t next = 0;
        tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_PARAM, type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_PARAM, type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t operands[2];
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const uint32_t bits = source_width == 4 ? tier1_select_test_append(function, &next, IR_TRUNC, IR_I32, k, BUSTER_IR_NONE, BUSTER_IR_NONE) : k;
            operands[k] = tier1_select_test_append(function, &next, IR_BITCAST, float_type, bits, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t condition = tier1_select_test_append(function, &next, IR_FCMP, IR_I1, operands[0], operands[1], predicate);
        if (shape == 1)
        {
            condition = tier1_select_test_append(function, &next, IR_COPY, IR_I1, condition, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        if (shape >= 2)
        {
            const uint32_t extended = tier1_select_test_append(function, &next, IR_ZEXT, IR_I32, condition, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            condition = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, extended, zero, shape == 2 ? IR_NE : IR_EQ);
        }
        if (branch)
        {
            *root = tier1_select_test_append(function, &next, IR_CBR, IR_VOID, condition, 1, 2);
            function->block_offsets[1] = next;
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->block_offsets[2] = next;
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            *root = tier1_select_test_append(function, &next, IR_SELECT, type, condition, 2, 3);
            uint32_t result = *root;
            if (summed)
            {
                const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                result = tier1_select_test_append(function, &next, IR_ADD, type, *root, one, BUSTER_IR_NONE);
            }
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        function->instruction_count = next;
        function->block_offsets[function->block_count] = next;
    }
    return ready;
}

// C's predicate of the values whose bits are `a` and `b` (a float's in the low half).
static bool tier1_float_select_truth(uint32_t source_width, uint32_t predicate, uint64_t a, uint64_t b)
{
    double left = 0.0;
    double right = 0.0;
    if (source_width == 4)
    {
        float l = 0.0f;
        float r = 0.0f;
        memcpy(&l, &a, sizeof(l));
        memcpy(&r, &b, sizeof(r));
        left = (double)l;
        right = (double)r;
    }
    else
    {
        memcpy(&left, &a, sizeof(left));
        memcpy(&right, &b, sizeof(right));
    }
    bool result = false;
    switch (predicate)
    {
    case IR_EQ:
        result = left == right;
        break;
    case IR_NE:
        result = left != right;
        break;
    case IR_LT:
        result = left < right;
        break;
    case IR_LE:
        result = left <= right;
        break;
    case IR_GT:
        result = left > right;
        break;
    case IR_GE:
        result = left >= right;
        break;
    default:
        break;
    }
    return result;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_float_select_call(const void* code, uint64_t a, uint64_t b, uint64_t x, uint64_t y)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(a, b, x, y);
}

static void tier1_float_select_execute(Tier1EmitTest* test, Tier1EmitResult* emitted, uint32_t source_width, uint32_t width, uint32_t predicate, uint32_t shape, bool summed)
{
    static const uint64_t doubles[] =
    {
        0,
        UINT64_C(0x8000000000000000),
        UINT64_C(0x3ff0000000000000),
        UINT64_C(0xc000000000000000),
        UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000001234),
        UINT64_C(0xfff8000000000000),
        1,
    };
    static const uint32_t floats[] =
    {
        0,
        0x80000000u,
        0x3f800000u,
        0xc0000000u,
        0x7f800000u,
        0xff800000u,
        0x7fc01234u,
        0xffc00000u,
        1,
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(doubles) == BUSTER_ARRAY_LENGTH(floats), "a float sample for each double sample");
    const uint64_t x = UINT64_C(0x1111222233334444);
    const uint64_t y = UINT64_C(0x5555666677778888);
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (tier1_emit_test_load(test, &emitted->code, &executable, &bytes))
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(doubles); i += 1)
        {
            for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(doubles); j += 1)
            {
                // A float's bits under upper bits its truncation leaves out.
                const uint64_t a = source_width == 4 ? UINT64_C(0xa5a5a5a500000000) | floats[i] : doubles[i];
                const uint64_t b = source_width == 4 ? UINT64_C(0x5a5a5a5a00000000) | floats[j] : doubles[j];
                const bool holds = tier1_float_select_truth(source_width, predicate, a, b) != (shape == 3);
                const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
                const uint64_t expected = ((holds ? x : y) + (summed ? 1u : 0u)) & mask;
                tier1_emit_test_expect(test, tier1_float_select_call(executable, a, b, x, y) == expected, "a floating comparison's select chooses as C does, unordered operands included");
            }
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
}

static void tier1_float_select_tests(Tier1EmitTest* test)
{
    test->terms = 4;
    test->width = 8;
    test->selects = true;
    // Every register the CFG consumer's values take, and the parameters' and the result's alone.
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RCX);
    uint32_t fused_selects = 0;
    uint32_t fused_branches = 0;
    for (uint32_t source_width = 4; source_width <= 8; source_width += 4)
    {
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            for (uint32_t predicate = IR_EQ; predicate <= IR_GE; predicate += 1)
            {
                for (uint32_t shape = 0; shape < BUSTER_TIER1_FLOAT_SELECT_SHAPES; shape += 1)
                {
                    const bool ordered = predicate != IR_EQ && predicate != IR_NE;
                    // 8 options each of a select, a summed select and a branch.
                    for (uint32_t variant = 0; variant < 24; variant += 1)
                    {
                        const bool vector = (variant & 1) != 0;
                        const bool narrow = (variant & 2) != 0;
                        const bool pointer = (variant & 4) != 0;
                        const bool summed = variant >> 3 == 1;
                        const bool branch = variant >> 3 == 2;
                        const bool fused = ordered;
                        IrFunction function;
                        uint32_t root = BUSTER_IR_NONE;
                        const bool created = tier1_float_select_function(test, source_width, width, predicate, shape, summed, branch, &function, &root);
                        const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK;
                        tier1_emit_test_expect(test, valid, "a floating select's fixture is valid IR");
                        IrDominance dominance;
                        const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
                        Tier1SelectPlan plan;
                        uint32_t node = BUSTER_IR_NONE;
                        const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
                        tier1_emit_test_expect(test, planned, "a floating select's fixture selects by both paths");
                        const uint8_t rule = planned ? plan.patterns[root].rule : TIER1_RULE_NONE;
                        const uint8_t fused_rule = branch ? TIER1_RULE_FLOAT_BRANCH_COMPARE : TIER1_RULE_FLOAT_SELECT_COMPARE;
                        const uint8_t plain_rule = branch ? TIER1_RULE_BRANCH : TIER1_RULE_SELECT;
                        tier1_emit_test_expect(test, rule == (fused ? fused_rule : plain_rule), "a floating select or branch fuses exactly where the grammar takes its comparison");
                        fused_selects += rule == TIER1_RULE_FLOAT_SELECT_COMPARE;
                        fused_branches += rule == TIER1_RULE_FLOAT_BRANCH_COMPARE;
                        const Tier1EmitOptions options = {
                            .register_mask = narrow ? few : tier1_liveness_register_mask(),
                            .vector = vector,
                            .rematerialize = true,
                            .certify = true,
                            .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
                        const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
                        tier1_emit_test_expect(test, certified, "a floating select's allocation and emitted code certify");
                        if (!certified && test->failures < 20)
                        {
                            fprintf(stderr, "  floating select error=%u node=%u source=%u width=%u predicate=%u shape=%u variant=%u\n", error, node, source_width, width, predicate, shape, variant);
                        }
                        if (certified)
                        {
                            uint32_t sets = 0;
                            uint32_t moves = 0;
                            for (uint32_t r = 0; r < emitted.code.count; r += 1)
                            {
                                const uint32_t operation = x86_effects(emitted.code.records[r].form)->operation;
                                sets += operation == X86_OPERATION_SETCC;
                                moves += operation == X86_OPERATION_MOV && emitted.origins[r] == tier1_origin(TIER1_ORIGIN_NODE, root);
                            }
                            tier1_emit_test_expect(test, (sets == 0) == fused, "a fused floating select sets no truth value, and an unfused one tests its comparison's");
                            tier1_emit_test_expect(test, !fused || !summed || narrow || moves == 0, "a fused floating select whose values end at it takes the register of the one it keeps");
                            tier1_float_select_execute(test, &emitted, source_width, width, predicate, shape, summed);
                        }
                        arena_pop_to(&test->output, 0);
                        arena_pop_to(&test->scratch, 0);
                        arena_pop_to(&test->input, 0);
                    }
                }
            }
        }
    }
    tier1_emit_test_expect(test, fused_selects == 2 * 2 * 4 * BUSTER_TIER1_FLOAT_SELECT_SHAPES * 16 && fused_branches == 2 * 2 * 4 * BUSTER_TIER1_FLOAT_SELECT_SHAPES * 8,
                           "every shape the grammar takes fuses");
}
