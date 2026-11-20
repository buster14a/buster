// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2791 and #2922: selects of floating comparisons, included by tier1_select_test.c.
//
// - TIER1_RULE_FLOAT_SELECT_COMPARE's records run natively for each condition (the branch's UGT, UGE, ULE and ULT, and
//   == and !=), both precisions and both select widths, with the comparison's operands in two registers or one, and the
//   destination in a register of its own, the chosen value's, the other value's, or one register for both values,
//   against C's comparisons of the operands' values: NaNs of either sign, a signaling one, zeros of both signs,
//   infinities, a subnormal and the largest finite value among them.
// - The plans of a comparison by each predicate whose select reads it directly, through its I1 copy right after it
//   (if-conversion's truth simplification's), through a copy past another node, or through one or two of C's truth
//   values' links must fold exactly the shapes the grammar takes: every one of an ordered predicate but the distant
//   copy, and == and != directly and through their copy. Each plan joins the selection's frozen digest.
// - tier1_select_recurrent (#2922) finds a select's values' recurrence through an operation, directly, through a phi of
//   phis and where the walk reaches the phi through phis alone first, up to its bound of rows and not past it; and none
//   through phis and copies alone (a running choice), nor where only the select's result or its condition reaches the
//   phi.

// Whether a floating select's `condition` holds for `left` and `right`, the operands' bits at `width` bytes (a float's in
// the low half), by C's own comparisons of their values.
static bool tier1_select_test_float_holds(uint32_t condition, uint32_t width, uint64_t left, uint64_t right)
{
    double l = 0.0;
    double r = 0.0;
    if (width == 4)
    {
        float lf = 0.0f;
        float rf = 0.0f;
        memcpy(&lf, &left, sizeof(lf));
        memcpy(&rf, &right, sizeof(rf));
        l = (double)lf;
        r = (double)rf;
    }
    else
    {
        memcpy(&l, &left, sizeof(l));
        memcpy(&r, &right, sizeof(r));
    }
    bool holds = false;
    switch (condition)
    {
    case IR_UGT:
        holds = l > r;
        break;
    case IR_UGE:
        holds = l >= r;
        break;
    case IR_ULE:
        holds = !(l > r);
        break;
    case IR_ULT:
        holds = !(l >= r);
        break;
    case IR_EQ:
        holds = l == r;
        break;
    case IR_NE:
        holds = l != r;
        break;
    default:
        break;
    }
    return holds;
}

static void tier1_select_test_float_select_holes(Tier1SelectTest* test)
{
    static const uint8_t conditions[] = { IR_UGT, IR_UGE, IR_ULE, IR_ULT, IR_EQ, IR_NE };
    static const uint64_t doubles[] =
    {
        0,
        UINT64_C(0x8000000000000000),
        UINT64_C(0x3ff0000000000000),
        UINT64_C(0x4000000000000000),
        UINT64_C(0xbff0000000000000),
        UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000000000),
        UINT64_C(0xfff8000000001234),
        UINT64_C(0x7ff0000000000001),
        1,
        UINT64_C(0x7fefffffffffffff),
    };
    // The same values as floats, under upper bits that movd leaves out.
    static const uint32_t floats[] =
    {
        0,
        0x80000000u,
        0x3f800000u,
        0x40000000u,
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
    // The comparison's operands' xmm registers, as the CFG consumer's locations name them (the vector class's, past 32):
    // two, two others in the other order, and one for both.
    static const uint8_t operands[3][2] =
    {
        { 32, 33 },
        { 37, 34 },
        { 36, 36 },
    };
    // The select's values' registers, where the comparison holds and where it doesn't, then the destination's: its own,
    // the first value's, the second's, and for one register of both values, its own and theirs.
    static const uint8_t registers[5][3] =
    {
        { X86_REGISTER_RCX, X86_REGISTER_R8, X86_REGISTER_RAX },
        { X86_REGISTER_RCX, X86_REGISTER_R8, X86_REGISTER_RCX },
        { X86_REGISTER_RCX, X86_REGISTER_R8, X86_REGISTER_R8 },
        { X86_REGISTER_RCX, X86_REGISTER_RCX, X86_REGISTER_RAX },
        { X86_REGISTER_RCX, X86_REGISTER_RCX, X86_REGISTER_RCX },
    };
    const uint64_t chosen = UINT64_C(0x1111222233334444);
    const uint64_t other = UINT64_C(0x5555666677778888);
    const uint64_t upper = UINT64_C(0xdeadbeef00000000);
    for (uint32_t source_width = 4; source_width <= 8; source_width += 4)
    {
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(conditions); k += 1)
            {
                const uint32_t condition = conditions[k];
                const bool equality = condition == IR_EQ || condition == IR_NE;
                for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(operands); o += 1)
                {
                    for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(registers); v += 1)
                    {
                        const Tier1SelectHoles holes =
                        {
                            .rule = TIER1_RULE_FLOAT_SELECT_COMPARE,
                            .width = (uint8_t)width,
                            .source_width = (uint8_t)source_width,
                            .destination = registers[v][2],
                            .left = operands[o][0],
                            .right = operands[o][1],
                            .third = registers[v][0],
                            .fourth = registers[v][1],
                            .has_fourth = true,
                            .condition = (uint8_t)condition,
                        };
                        const Tier1SelectRecords selected = tier1_select_test_expand(test, &holes);
                        tier1_select_test_expect(test, selected.count <= (equality ? 4u : 3u), "a floating select is its ucomis, a move and its cmovs");
                        uint32_t others = 0;
                        for (uint32_t r = 1; r < selected.count; r += 1)
                        {
                            const uint32_t operation = x86_effects(selected.records[r].form)->operation;
                            others += operation != X86_OPERATION_MOV && operation != X86_OPERATION_CMOVCC;
                        }
                        tier1_select_test_expect(test, selected.count != 0 && selected.records[0].form == tier1_select_float_forms(source_width)->compare && others == 0,
                                                 "a floating select's ucomis comes first and only moves and cmovs, which write no flags, follow it");
                        // The values first, rcx's where the comparison holds and r8's where it doesn't, then the operands'
                        // bits from rdi and rsi.
                        const bool shared = registers[v][0] == registers[v][1];
                        const bool same = operands[o][0] == operands[o][1];
                        const uint16_t from_general = tier1_select_float_forms(source_width)->from_general;
                        X86Instruction records[16];
                        uint32_t count = 0;
                        records[count++] = tier1_select_constant_move(8, registers[v][0], chosen);
                        if (!shared)
                        {
                            records[count++] = tier1_select_constant_move(8, registers[v][1], other);
                        }
                        records[count++] = tier1_select_register_record(from_general, operands[o][0] & 15, X86_REGISTER_RDI);
                        if (!same)
                        {
                            records[count++] = tier1_select_register_record(from_general, operands[o][1] & 15, X86_REGISTER_RSI);
                        }
                        const uint32_t prefix = count;
                        memcpy(records + count, selected.records, selected.count * sizeof(*records));
                        count += selected.count;
                        records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes.destination);
                        records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                        if (tier1_select_test_load_records(test, records, count, prefix, prefix + selected.count, false))
                        {
                            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(doubles); i += 1)
                            {
                                for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(doubles); j += 1)
                                {
                                    const uint64_t a = source_width == 4 ? upper | floats[i] : doubles[i];
                                    const uint64_t b = source_width == 4 ? upper | floats[j] : doubles[j];
                                    const bool holds = tier1_select_test_float_holds(condition, source_width, a, same ? a : b);
                                    const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
                                    const uint64_t expected = (holds || shared ? chosen : other) & mask;
                                    tier1_select_test_expect(test, tier1_select_test_call(test, a, b, 0) == expected, "a floating select chooses as C's comparison, unordered operands included");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// A comparison's select: a and b (F32 or F64 by `source_width`), x and y (I32 or I64 by `width`), and node 4 the
// comparison of a and b by `predicate`. By `shape`, the select's condition is the comparison itself (0), its I1 copy
// right after it (1), its copy past a constant (2), or C's truth values' links (`icmp ne/eq (zext c), 0`): one ne (3),
// one eq (4), two eq (5), and ne then eq (6). With `apart`, the constants 11 and 22 come between the condition and the
// select, which selects them instead of x and y.
static void tier1_select_test_float_select_plan(Tier1SelectTest* test, uint32_t source_width, uint32_t width, uint32_t predicate, uint32_t shape, bool apart, Arena* input, Arena* output, Arena* scratch)
{
    static const uint8_t link_counts[7] = { 0, 0, 0, 1, 1, 2, 2 };
    static const uint8_t link_negations[7] = { 0, 0, 0, 0, 1, 3, 2 };
    const uint8_t float_type = source_width == 4 ? IR_F32 : IR_F64;
    const uint8_t type = width == 4 ? IR_I32 : IR_I64;
    const uint32_t links = link_counts[shape];
    const IrSizes sizes =
    {
        .instruction_count = 16,
        .block_count = 1,
        .constant_count = 4,
        .parameter_count = 4,
    };
    IrFunction function;
    const bool created = ir_create(input, &sizes, &function) == IR_OK;
    tier1_select_test_expect(test, created, "create a floating select's fixture");
    Tier1SelectPlan plans[1];
    uint32_t root = BUSTER_IR_NONE;
    uint32_t chain_end = 0;
    if (created)
    {
        function.result_type = type;
        function.parameter_types[0] = float_type;
        function.parameter_types[1] = float_type;
        function.parameter_types[2] = type;
        function.parameter_types[3] = type;
        function.constants[0].low = 0;
        function.constants[1].low = 0;
        function.constants[2].low = 11;
        function.constants[3].low = 22;
        uint32_t next = 0;
        tier1_select_test_append(&function, &next, IR_PARAM, float_type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_PARAM, float_type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_PARAM, type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_PARAM, type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t condition = tier1_select_test_append(&function, &next, IR_FCMP, IR_I1, 0, 1, predicate);
        if (shape == 2)
        {
            tier1_select_test_append(&function, &next, IR_CONST, type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
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
        uint32_t chosen = 2;
        uint32_t other = 3;
        if (apart)
        {
            chosen = tier1_select_test_append(&function, &next, IR_CONST, type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            other = tier1_select_test_append(&function, &next, IR_CONST, type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        root = tier1_select_test_append(&function, &next, IR_SELECT, type, condition, chosen, other);
        tier1_select_test_append(&function, &next, IR_RET, IR_VOID, root, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function.instruction_count = next;
        function.block_offsets[1] = next;
    }
    if (created && tier1_select_test_walk_plans(test, &function, 0, output, scratch, plans, "a floating select's fixture selects") == 1)
    {
        const bool equality = predicate == IR_EQ || predicate == IR_NE;
        const bool fused = shape != 2 && (!equality || shape <= 1);
        const bool odd = (__builtin_popcount(link_negations[shape]) & 1) != 0;
        const bool swapped = predicate == IR_LT || predicate == IR_LE;
        const uint8_t ordered = predicate == IR_LT || predicate == IR_GT ? IR_UGT : IR_UGE;
        const uint8_t unnegated = equality ? (uint8_t)predicate : ordered;
        const uint8_t condition = odd ? tier1_select_negations[unnegated] : unnegated;
        const uint32_t chosen = apart ? chain_end : 2;
        const Tier1SelectPattern* pattern = &plans[0].patterns[root];
        bool covered = true;
        for (uint32_t node = 4; node < chain_end; node += 1)
        {
            covered &= plans[0].covered[node] != 0 || function.op[node] == IR_CONST;
        }
        const uint32_t first = plans[0].effective_use_offsets[root];
        const uint32_t uses = plans[0].effective_use_offsets[root + 1] - first;
        if (fused)
        {
            const bool operands = pattern->values[0] == (swapped ? 1u : 0u) && pattern->values[1] == (swapped ? 0u : 1u);
            const bool values = pattern->values[2] == chosen && pattern->values[3] == chosen + 1;
            // Its candidate costs what the grammar's pattern does (tier1_burs_patterns: a select of an FCOND at 3, of an ECOND
            // at 4), the cost the plan records for it.
            tier1_select_test_expect(test, pattern->rule == TIER1_RULE_FLOAT_SELECT_COMPARE && pattern->condition == condition && pattern->width == width &&
                                               pattern->source_width == source_width && operands && values && covered && uses == 4 && pattern->intrinsic_cost == (equality ? 4 : 3),
                                     "a floating comparison's select is its ucomis and cmov, through its copy and its truth values");
        }
        else
        {
            // The comparison computes its truth value, which the select tests.
            tier1_select_test_expect(test, pattern->rule == TIER1_RULE_SELECT && plans[0].covered[4] == 0 && plans[0].patterns[4].rule == TIER1_RULE_FLOAT_COMPARE,
                                     "a floating select the grammar takes no fold of tests the comparison's truth value");
        }
    }
    arena_pop_to(output, 0);
    arena_pop_to(input, 0);
}

// A loop's select for tier1_select_recurrent (#2922), unverified rows of the walk's columns: an accumulator phi, then by
// `kind` the select whose recurrence the walk looks for. 0: its true value `chain` additions past the accumulator
// (`chain` 0 is the accumulator itself), its false value a parameter. 1: its true value another phi, the accumulator's
// previous value. 2: an addition of it to the accumulator is the phi's value, its own values parameters. 3: the
// accumulator is the phi's value and the condition's operand, its values parameters. 4: as 1, the other phi's value the
// accumulator plus one. 5: its true value the accumulator plus one, its false value the accumulator, which the walk
// visits first. 6: its true value a copy of the accumulator. Returns the select.
static uint32_t tier1_select_test_float_recurrence_function(IrFunction* function, uint32_t kind, uint32_t chain)
{
    uint32_t next = 0;
    const uint32_t x = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t y = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t a = tier1_select_test_append(function, &next, IR_PARAM, IR_F64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t accumulator = tier1_select_test_append(function, &next, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
    const uint32_t other = tier1_select_test_append(function, &next, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
    const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
    uint32_t value = accumulator;
    for (uint32_t k = 0; kind == 0 && k < chain; k += 1)
    {
        value = tier1_select_test_append(function, &next, IR_ADD, IR_I64, value, one, BUSTER_IR_NONE);
    }
    const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_F64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t compared = kind == 3 ? tier1_select_test_append(function, &next, IR_SITOFP, IR_F64, accumulator, BUSTER_IR_NONE, BUSTER_IR_NONE) : a;
    const uint32_t condition = tier1_select_test_append(function, &next, IR_FCMP, IR_I1, compared, zero, IR_EQ);
    const uint32_t increment = kind == 4 || kind == 5 ? tier1_select_test_append(function, &next, IR_ADD, IR_I64, accumulator, one, BUSTER_IR_NONE) : accumulator;
    const uint32_t copied = kind == 6 ? tier1_select_test_append(function, &next, IR_COPY, IR_I64, accumulator, BUSTER_IR_NONE, BUSTER_IR_NONE) : accumulator;
    const uint32_t chosen = kind == 0 ? value : kind == 1 || kind == 4 ? other : kind == 5 ? increment : kind == 6 ? copied : x;
    const uint32_t select = tier1_select_test_append(function, &next, IR_SELECT, IR_I64, condition, chosen, kind == 0 ? x : kind == 5 ? accumulator : y);
    const uint32_t updated = kind == 2 ? tier1_select_test_append(function, &next, IR_ADD, IR_I64, accumulator, select, BUSTER_IR_NONE) : select;
    tier1_select_test_append(function, &next, IR_RET, IR_VOID, accumulator, BUSTER_IR_NONE, BUSTER_IR_NONE);
    const uint32_t incoming[8] = { 0, x, 1, updated, 0, y, 1, kind == 4 ? increment : accumulator };
    memcpy(function->extra, incoming, sizeof(incoming));
    function->instruction_count = next;
    return select;
}

// tier1_select_recurrent (#2922): a recurrence through the select's value's additions up to the walk's bound of rows (16:
// the false value, the chain's additions, their constant and the phi, so a chain of 13 is the longest it sees) and not
// past it, through a phi of phis with an addition, and where the walk reaches the phi through phis alone first; none
// through the phi itself, its copy or a phi of phis alone (a running choice), nor where only the select's result or its
// condition reaches the phi.
static void tier1_select_test_float_recurrence(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    const IrSizes sizes =
    {
        .instruction_count = 64,
        .block_count = 1,
        .constant_count = 1,
        .parameter_count = 3,
        .extra_count = 8,
    };
    for (uint32_t chain = 0; chain <= 16; chain += 1)
    {
        IrFunction function;
        const bool created = ir_create(&input, &sizes, &function) == IR_OK;
        tier1_select_test_expect(test, created, "create a select's recurrence fixture");
        if (created)
        {
            const uint32_t select = tier1_select_test_float_recurrence_function(&function, 0, chain);
            tier1_select_test_expect(test, tier1_select_recurrent(&function, select) == (chain >= 1 && chain <= 13), "a select's value recurs through its additions up to the walk's bound, not through the phi alone");
        }
        arena_pop_to(&input, 0);
    }
    for (uint32_t kind = 1; kind < 7; kind += 1)
    {
        IrFunction function;
        const bool created = ir_create(&input, &sizes, &function) == IR_OK;
        tier1_select_test_expect(test, created, "create a select's recurrence fixture");
        if (created)
        {
            const uint32_t select = tier1_select_test_float_recurrence_function(&function, kind, 0);
            tier1_select_test_expect(test, tier1_select_recurrent(&function, select) == (kind == 4 || kind == 5), "a select's value recurs through an operation past phis, not through phis or copies alone, nor where only its result or its condition does");
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&input);
}

static void tier1_select_test_float_selects(Tier1SelectTest* test)
{
    tier1_select_test_float_recurrence(test);
    tier1_select_test_float_select_holes(test);
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(16u << 20);
    Arena scratch = arena_reserve(16u << 20);
    for (uint32_t source_width = 4; source_width <= 8; source_width += 4)
    {
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            for (uint32_t predicate = IR_EQ; predicate <= IR_GE; predicate += 1)
            {
                for (uint32_t shape = 0; shape < 7; shape += 1)
                {
                    for (uint32_t apart = 0; apart < 2; apart += 1)
                    {
                        tier1_select_test_float_select_plan(test, source_width, width, predicate, shape, apart != 0, &input, &output, &scratch);
                    }
                }
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}
