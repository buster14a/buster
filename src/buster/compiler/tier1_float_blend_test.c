// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2842: selects of floating values, included by tier1_select_test.c after tier1_float_select_test.c.
//
// - TIER1_RULE_FLOAT_BLEND's records run natively at both widths for both truth values of its I1 condition, with the
//   values in two registers or one and the destination in its own register, the true value's, the false value's or the
//   one of both, registers past xmm7 among them: by the lane mask, its records valid at x86-64's baseline, and under k1,
//   valid at x86-64-v4. The values are NaNs with payloads, a signaling one among them, whose bits come out whole.
// - TIER1_RULE_FLOAT_BLEND_COMPARE's, for each cmps predicate selection makes (EQ_OQ, LT_OS, LE_OS and the other one of
//   each pair: NEQ_UQ, NLT_US, NLE_US), at both widths, the comparison's operands in two registers, one, or the
//   destination's, against C's comparisons of their values: NaNs of either sign, a signaling one, zeros of both signs,
//   infinities, a subnormal and the largest finite value.
// - The plans: a select of F32 or F64 values by an integer comparison's truth value is FLOAT_BLEND of that value; by a
//   floating comparison of their width, by each predicate, read directly, through its I1 copy right after it, or through
//   C's truth values' links, FLOAT_BLEND_COMPARE with the predicate's immediate (the other one of its pair where the links
//   negate it) and its operands swapped for > and >=, the comparison and the links folded; through a copy past another
//   node, of the other width, or read by a second select too, FLOAT_BLEND of the comparison's own truth value. Each plan
//   joins the selection's frozen digest.

// Whether cmps's predicate `immediate` holds for `left` and `right`, the operands' bits at `width` bytes (a float's in the
// low half), by C's own comparisons of their values: EQ_OQ, LT_OS and LE_OS, and four past each its negation.
static bool tier1_select_test_float_lanes_hold(uint32_t immediate, uint32_t width, uint64_t left, uint64_t right)
{
    double l = 0.0;
    double r = 0.0;
    if (width == 4)
    {
        float lf = 0.0f;
        float rf = 0.0f;
        const uint32_t low_left = (uint32_t)left;
        const uint32_t low_right = (uint32_t)right;
        memcpy(&lf, &low_left, sizeof(lf));
        memcpy(&rf, &low_right, sizeof(rf));
        l = (double)lf;
        r = (double)rf;
    }
    else
    {
        memcpy(&l, &left, sizeof(l));
        memcpy(&r, &right, sizeof(r));
    }
    const uint32_t base = immediate & 3;
    const bool holds = base == 0 ? l == r : base == 1 ? l < r : l <= r;
    return (immediate & 4) != 0 ? !holds : holds;
}

// The blend's registers, as the CFG consumer's locations name them (the vector class's, past 32): the true value's, the
// false value's and the destination's.
static const uint8_t tier1_select_test_blend_registers[][3] = {
    { 34, 35, 36 },
    { 34, 35, 34 },
    { 34, 35, 35 },
    { 34, 34, 36 },
    { 34, 34, 34 },
    { 41, 46, 44 },
    { 46, 41, 41 },
};

// A blend's native function: the true value's bits into its register from rcx, the false value's from r8, a comparison's
// operands from rdi and rsi, then the selection's records, and the destination's bits into rax. The call passes a
// condition in rdx.
static bool tier1_select_test_blend_load(Tier1SelectTest* test, const Tier1SelectHoles* holes, uint32_t width, uint64_t chosen, uint64_t other, Tier1SelectRecords* selected)
{
    const bool compare = holes->rule == TIER1_RULE_FLOAT_BLEND_COMPARE;
    const uint8_t t = compare ? holes->third : holes->right;
    const uint8_t f = compare ? holes->fourth : holes->third;
    const uint16_t from_general = tier1_select_float_forms(width)->from_general;
    *selected = tier1_select_test_expand(test, holes);
    X86Instruction records[24];
    uint32_t count = 0;
    records[count++] = tier1_select_constant_move(8, X86_REGISTER_RCX, chosen);
    records[count++] = tier1_select_constant_move(8, X86_REGISTER_R8, other);
    records[count++] = tier1_select_register_record(from_general, t & 15, X86_REGISTER_RCX);
    if (t != f)
    {
        records[count++] = tier1_select_register_record(from_general, f & 15, X86_REGISTER_R8);
    }
    if (compare)
    {
        records[count++] = tier1_select_register_record(from_general, holes->left & 15, X86_REGISTER_RDI);
        if (holes->left != holes->right)
        {
            records[count++] = tier1_select_register_record(from_general, holes->right & 15, X86_REGISTER_RSI);
        }
    }
    const uint32_t prefix = count;
    memcpy(records + count, selected->records, selected->count * sizeof(*records));
    count += selected->count;
    records[count++] = tier1_select_register_record(tier1_select_float_forms(width)->to_general, holes->destination & 15, X86_REGISTER_RAX);
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    return tier1_select_test_load_records(test, records, count, prefix, prefix + selected->count, false);
}

// TIER1_RULE_FLOAT_BLEND's records, by the lane mask (`opmask` 0, at x86-64's baseline) or under k1 (at x86-64-v4).
static void tier1_select_test_float_blend_records(Tier1SelectTest* test, uint8_t opmask)
{
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
        // A signaling NaN with a payload where the condition holds, and a quiet one of the other sign where it doesn't.
        const uint64_t chosen = width == 4 ? UINT64_C(0xdeadbeef7fa00123) : UINT64_C(0x7ff4000000000123);
        const uint64_t other = width == 4 ? UINT64_C(0x01234567ffc04567) : UINT64_C(0xfff8000000004567);
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(tier1_select_test_blend_registers); v += 1)
        {
            const uint8_t* registers = tier1_select_test_blend_registers[v];
            const Tier1SelectHoles holes = {
                .rule = TIER1_RULE_FLOAT_BLEND,
                .width = (uint8_t)width,
                .destination = registers[2],
                .left = X86_REGISTER_RDX,
                .right = registers[0],
                .third = registers[1],
                .opmask = opmask,
            };
            Tier1SelectRecords selected;
            const bool loaded = tier1_select_test_blend_load(test, &holes, width, chosen, other, &selected);
            const bool shared = registers[0] == registers[1];
            uint32_t masked = 0;
            for (uint32_t r = 0; r < selected.count; r += 1)
            {
                masked += selected.records[r].decorations != 0;
            }
            // Under k1: kmovw and vpblendm; by the lane mask: the condition's mask (none for one value), the true value's
            // copy where the destination holds neither value, and three bitwise operations.
            const uint32_t expected = opmask != 0 ? 2u : shared ? (registers[2] != registers[0] ? 1u : 0u) : 6u + (registers[2] != registers[0] && registers[2] != registers[1] ? 1u : 0u);
            tier1_select_test_expect(test, selected.count == expected && masked == (opmask != 0 ? 1u : 0u), "a floating select's blend is its mask's records, a copy where needed and its blend");
            for (uint64_t condition = 0; loaded && condition < 2; condition += 1)
            {
                const uint64_t result = tier1_select_test_call(test, 0, 0, condition);
                tier1_select_test_expect(test, (result & mask) == ((condition != 0 || shared ? chosen : other) & mask), "a floating select's blend keeps the chosen value's bits whole");
            }
        }
    }
}

// TIER1_RULE_FLOAT_BLEND_COMPARE's records, by each cmps predicate, against C's comparisons.
static void tier1_select_test_float_blend_compare_records(Tier1SelectTest* test)
{
    static const uint8_t immediates[] = { 0, 1, 2, 4, 5, 6 };
    static const uint64_t doubles[] = {
        0,
        UINT64_C(0x8000000000000000),
        UINT64_C(0x3ff0000000000000),
        UINT64_C(0xbff0000000000000),
        UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000000000),
        UINT64_C(0xfff8000000001234),
        UINT64_C(0x7ff0000000000001),
        1,
        UINT64_C(0x7fefffffffffffff),
    };
    static const uint32_t floats[] = {
        0,
        0x80000000u,
        0x3f800000u,
        0xbf800000u,
        0x7f800000u,
        0xff800000u,
        0x7fc00000u,
        0xffc01234u,
        0x7f800001u,
        1,
        0x7f7fffffu,
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(doubles) == BUSTER_ARRAY_LENGTH(floats), "a float sample for each double sample");
    // The comparison's operands' registers: two, two others in the other order, one for both, and the destination's.
    static const uint8_t operands[4][2] = {
        { 32, 33 },
        { 39, 37 },
        { 38, 38 },
        { 36, 33 },
    };
    const uint64_t upper = UINT64_C(0xdeadbeef00000000);
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
        const uint64_t chosen = width == 4 ? UINT64_C(0x7fa00123) : UINT64_C(0x7ff4000000000123);
        const uint64_t other = width == 4 ? UINT64_C(0xffc04567) : UINT64_C(0xfff8000000004567);
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(immediates); k += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(operands); o += 1)
            {
                for (uint32_t v = 0; v < 5; v += 1)
                {
                    const uint8_t* registers = tier1_select_test_blend_registers[v];
                    const Tier1SelectHoles holes = {
                        .rule = TIER1_RULE_FLOAT_BLEND_COMPARE,
                        .width = (uint8_t)width,
                        .source_width = (uint8_t)width,
                        .destination = registers[2],
                        .left = operands[o][0],
                        .right = operands[o][1],
                        .third = registers[0],
                        .fourth = registers[1],
                        .has_fourth = true,
                        .condition = immediates[k],
                    };
                    Tier1SelectRecords selected;
                    const bool loaded = tier1_select_test_blend_load(test, &holes, width, chosen, other, &selected);
                    tier1_select_test_expect(test, selected.count >= 2 && x86_effects(selected.records[1].form)->operation != X86_OPERATION_NONE &&
                                                       (selected.records[1].form == X86_FORM_CMPSD_X_XM64_IMM8 || selected.records[1].form == X86_FORM_CMPSS_X_XM32_IMM8) &&
                                                       selected.records[1].reg == BUSTER_TIER1_SELECT_FLOAT_SCRATCH,
                                             "a floating select's comparison is cmps into xmm15 first");
                    const bool shared = registers[0] == registers[1];
                    const bool same = operands[o][0] == operands[o][1];
                    for (uint32_t i = 0; loaded && i < BUSTER_ARRAY_LENGTH(doubles); i += 1)
                    {
                        for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(doubles); j += 1)
                        {
                            const uint64_t a = width == 4 ? upper | floats[i] : doubles[i];
                            const uint64_t b = width == 4 ? upper | floats[j] : doubles[j];
                            const bool holds = tier1_select_test_float_lanes_hold(immediates[k], width, a, same ? a : b);
                            const uint64_t result = tier1_select_test_call(test, a, b, 0);
                            tier1_select_test_expect(test, (result & mask) == ((holds || shared ? chosen : other) & mask),
                                                     "a floating select by cmps's lanes chooses as C's comparison, unordered operands included");
                        }
                    }
                }
            }
        }
    }
}

// A floating select's plan: c (I32), a and b (F32 or F64 by `source_width`), x and y (F32 or F64 by `width`). By `shape`,
// the select's condition is c's comparison with zero (7), or the comparison of a and b by `predicate`: itself (0), its I1
// copy right after it (1), its copy past a constant (2), C's truth values' links (`icmp ne/eq (zext c), 0`): one ne (3),
// one eq (4), two eq (5), and ne then eq (6), or itself where a second select of the first and x reads it too (8).
static void tier1_select_test_float_blend_plan(Tier1SelectTest* test, uint32_t source_width, uint32_t width, uint32_t predicate, uint32_t shape, Arena* input, Arena* output, Arena* scratch)
{
    static const uint8_t link_counts[9] = { 0, 0, 0, 1, 1, 2, 2, 0, 0 };
    static const uint8_t link_negations[9] = { 0, 0, 0, 0, 1, 3, 2, 0, 0 };
    static const uint8_t immediates[IR_GE + 1] = {
        [IR_EQ] = 0,
        [IR_NE] = 4,
        [IR_LT] = 1,
        [IR_LE] = 2,
        [IR_GT] = 1,
        [IR_GE] = 2,
    };
    const uint8_t compared = source_width == 4 ? IR_F32 : IR_F64;
    const uint8_t type = width == 4 ? IR_F32 : IR_F64;
    const uint32_t links = link_counts[shape];
    const IrSizes sizes = {
        .instruction_count = 20,
        .block_count = 1,
        .constant_count = 3,
        .parameter_count = 5,
    };
    IrFunction function;
    const bool created = ir_create(input, &sizes, &function) == IR_OK;
    tier1_select_test_expect(test, created, "create a floating select's blend fixture");
    Tier1SelectPlan plans[1];
    uint32_t root = BUSTER_IR_NONE;
    uint32_t second = BUSTER_IR_NONE;
    uint32_t condition = BUSTER_IR_NONE;
    uint32_t chain_end = 0;
    if (created)
    {
        function.result_type = type;
        function.parameter_types[0] = IR_I32;
        function.parameter_types[1] = compared;
        function.parameter_types[2] = compared;
        function.parameter_types[3] = type;
        function.parameter_types[4] = type;
        function.constants[0].low = 0;
        function.constants[1].low = 0;
        function.constants[2].low = 0;
        uint32_t next = 0;
        const uint32_t c = tier1_select_test_append(&function, &next, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_PARAM, compared, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_PARAM, compared, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_PARAM, type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_PARAM, type, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (shape == 7)
        {
            const uint32_t zero = tier1_select_test_append(&function, &next, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            condition = tier1_select_test_append(&function, &next, IR_ICMP, IR_I1, c, zero, IR_NE);
        }
        else
        {
            condition = tier1_select_test_append(&function, &next, IR_FCMP, IR_I1, 1, 2, predicate);
        }
        if (shape == 2)
        {
            tier1_select_test_append(&function, &next, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        if (shape == 1 || shape == 2)
        {
            condition = tier1_select_test_append(&function, &next, IR_COPY, IR_I1, condition, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        for (uint32_t link = 0; link < links; link += 1)
        {
            const uint32_t extended = tier1_select_test_append(&function, &next, IR_ZEXT, IR_I32, condition, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t zero = tier1_select_test_append(&function, &next, IR_CONST, IR_I32, link, BUSTER_IR_NONE, BUSTER_IR_NONE);
            condition = tier1_select_test_append(&function, &next, IR_ICMP, IR_I1, extended, zero, ((link_negations[shape] >> link) & 1) != 0 ? IR_EQ : IR_NE);
        }
        chain_end = next;
        root = tier1_select_test_append(&function, &next, IR_SELECT, type, condition, 3, 4);
        second = shape == 8 ? tier1_select_test_append(&function, &next, IR_SELECT, type, condition, root, 3) : root;
        tier1_select_test_append(&function, &next, IR_RET, IR_VOID, second, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function.instruction_count = next;
        function.block_offsets[1] = next;
    }
    if (created && tier1_select_test_walk_plans(test, &function, 0, output, scratch, plans, "a floating select's blend fixture selects") == 1)
    {
        const bool fused = shape != 7 && shape != 2 && shape != 8 && source_width == width;
        const bool odd = (__builtin_popcount(link_negations[shape]) & 1) != 0;
        const bool swapped = predicate == IR_GT || predicate == IR_GE;
        const Tier1SelectPattern* pattern = &plans[0].patterns[root];
        const uint32_t uses = plans[0].effective_use_offsets[root + 1] - plans[0].effective_use_offsets[root];
        bool covered = true;
        for (uint32_t node = 5; node < chain_end; node += 1)
        {
            covered &= plans[0].covered[node] != 0 || function.op[node] == IR_CONST;
        }
        if (fused)
        {
            const bool operands = pattern->values[0] == (swapped ? 2u : 1u) && pattern->values[1] == (swapped ? 1u : 2u) && pattern->values[2] == 3 && pattern->values[3] == 4;
            tier1_select_test_expect(test, pattern->rule == TIER1_RULE_FLOAT_BLEND_COMPARE && pattern->condition == (immediates[predicate] ^ (odd ? 4u : 0u)) && pattern->width == width &&
                                               pattern->source_width == source_width && operands && covered && uses == 4,
                                     "a floating comparison's select of its width's values is a blend by its cmps's lanes, through its copy and its truth values");
        }
        else
        {
            // A comparison two selects read is neither's: it computes its truth value, which both blend by.
            const Tier1SelectPattern* other = &plans[0].patterns[second];
            const bool both = shape != 8 || (other->rule == TIER1_RULE_FLOAT_BLEND && other->values[0] == condition && other->values[1] == root && other->values[2] == 3);
            tier1_select_test_expect(test, pattern->rule == TIER1_RULE_FLOAT_BLEND && pattern->width == width && pattern->values[0] == condition && pattern->values[1] == 3 &&
                                               pattern->values[2] == 4 && uses == 3 && plans[0].covered[condition] == 0 && both,
                                     "a floating select the lanes take no fold of blends by its condition's truth value");
        }
    }
    arena_pop_to(output, 0);
    arena_pop_to(input, 0);
}

static void tier1_select_test_float_blends(Tier1SelectTest* test)
{
    tier1_select_test_float_blend_records(test, 0);
    tier1_select_test_float_blend_compare_records(test);
    // Under k1, x86-64-v4's forms, run where the host has them: kept out of the baseline's manifest and its counts, as
    // tier1_select_test_intrinsics keeps x86-64-v3's, but not out of the check that every emitted form is covered.
    X86Target v4 = { 0 };
    const X86Target host = x86_target_native();
    uint8_t* allowed = malloc(X86_FORM_COUNT);
    uint8_t* native = malloc(X86_FORM_COUNT);
    const bool parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &v4) && allowed != 0 && native != 0;
    tier1_select_test_expect(test, parsed, "x86-64-v4's forms");
    if (parsed)
    {
        const bool manifest = test->manifest;
        uint64_t emitted_records[3];
        uint64_t unknown_costs[3];
        uint64_t missing_columns[3][X86_COST_COLUMN_COUNT];
        memcpy(emitted_records, test->emitted_records, sizeof(emitted_records));
        memcpy(unknown_costs, test->unknown_costs, sizeof(unknown_costs));
        memcpy(missing_columns, test->missing_columns, sizeof(missing_columns));
        test->manifest = false;
        x86_target_forms(&v4, allowed);
        x86_target_forms(&host, native);
        uint8_t* const baseline = test->allowed;
        test->allowed = allowed;
        test->native_allowed = native;
        tier1_select_test_float_blend_records(test, 1);
        test->allowed = baseline;
        test->native_allowed = 0;
        test->manifest = manifest;
        memcpy(test->emitted_records, emitted_records, sizeof(emitted_records));
        memcpy(test->unknown_costs, unknown_costs, sizeof(unknown_costs));
        memcpy(test->missing_columns, missing_columns, sizeof(missing_columns));
    }
    free(allowed);
    free(native);
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(16u << 20);
    Arena scratch = arena_reserve(16u << 20);
    for (uint32_t source_width = 4; source_width <= 8; source_width += 4)
    {
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            for (uint32_t predicate = IR_EQ; predicate <= IR_GE; predicate += 1)
            {
                for (uint32_t shape = 0; shape < 9; shape += 1)
                {
                    // An integer condition's (shape 7) needs no predicate of its own.
                    if (shape != 7 || predicate == IR_EQ)
                    {
                        tier1_select_test_float_blend_plan(test, source_width, width, predicate, shape, &input, &output, &scratch);
                    }
                }
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}
