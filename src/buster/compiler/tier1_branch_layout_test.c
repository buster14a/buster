// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Critical-edge fall-through regression for #1218, included by tier1_emit_test.c. Both successors of block 2 join it
// with block 1. Each join has two live phis that take the same parameter from block 2, forcing at least one real move
// on each critical edge even when allocation coalesces one phi. Whichever logical successor is physically next,
// block 2 needs one conditional branch and one jump: the next block's stub is last and falls through. Native execution
// checks every integer predicate and C's six floating predicates, including unordered inputs, through both allocation
// paths and frame-pointer modes. The alternate predecessor also executes, observing the other phi inputs.

static bool tier1_branch_layout_function(Tier1EmitTest* test, bool floating, uint32_t predicate, bool true_next, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 24 + 4 * floating,
        .block_count = 5,
        .extra_count = 16,
        .constant_count = 4,
        .parameter_count = 3,
    };
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        uint32_t next = 0;
        for (uint32_t parameter = 0; parameter < 3; parameter += 1)
        {
            function->parameter_types[parameter] = IR_I64;
            tier1_select_test_append(function, &next, IR_PARAM, IR_I64, parameter, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint64_t constants[] = { 0, 1, 8, 18 };
        for (uint32_t constant = 0; constant < BUSTER_ARRAY_LENGTH(constants); constant += 1)
        {
            function->constants[constant].low = constants[constant];
            tier1_select_test_append(function, &next, IR_CONST, IR_I64, constant, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t route = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, 0, 3, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, route, 2, 1);
        uint32_t alternate = BUSTER_IR_NONE;
        for (uint32_t block = 1; block <= 2; block += 1)
        {
            function->block_offsets[block] = next;
            if (block == 1)
            {
                alternate = tier1_select_test_append(function, &next, IR_ADD, IR_I64, 1, 4, BUSTER_IR_NONE);
            }
            uint32_t left = 1;
            uint32_t right = 2;
            if (floating)
            {
                left = tier1_select_test_append(function, &next, IR_BITCAST, IR_F64, left, BUSTER_IR_NONE, BUSTER_IR_NONE);
                right = tier1_select_test_append(function, &next, IR_BITCAST, IR_F64, right, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            const uint32_t condition = tier1_select_test_append(function, &next, floating ? IR_FCMP : IR_ICMP, IR_I1, left, right, predicate);
            tier1_select_test_append(function, &next, IR_CBR, IR_VOID, condition, true_next ? 3 : 4, true_next ? 4 : 3);
        }
        for (uint32_t block = 3; block <= 4; block += 1)
        {
            function->block_offsets[block] = next;
            const uint32_t first_phi = next;
            for (uint32_t phi = 0; phi < 2; phi += 1)
            {
                const uint32_t extra = (block - 3) * 8 + phi * 4;
                function->extra[extra] = 1;
                function->extra[extra + 1] = phi == 0 ? 1 : alternate;
                function->extra[extra + 2] = 2;
                function->extra[extra + 3] = 2;
                tier1_select_test_append(function, &next, IR_PHI, IR_I64, extra, 2, BUSTER_IR_NONE);
            }
            const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, first_phi, first_phi + 1, BUSTER_IR_NONE);
            const uint32_t result = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, block + 2, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        function->block_offsets[function->block_count] = next;
        ready = next == function->instruction_count;
    }
    return ready;
}

static bool tier1_branch_layout_truth(bool floating, uint32_t predicate, uint64_t a, uint64_t b)
{
    bool result = false;
    if (floating)
    {
        double left;
        double right;
        memcpy(&left, &a, sizeof(left));
        memcpy(&right, &b, sizeof(right));
        switch (predicate)
        {
            case IR_EQ: result = left == right; break;
            case IR_NE: result = left != right; break;
            case IR_LT: result = left < right; break;
            case IR_LE: result = left <= right; break;
            case IR_GT: result = left > right; break;
            case IR_GE: result = left >= right; break;
        }
    }
    else
    {
        switch (predicate)
        {
            case IR_EQ: result = a == b; break;
            case IR_NE: result = a != b; break;
            case IR_LT: result = (int64_t)a < (int64_t)b; break;
            case IR_LE: result = (int64_t)a <= (int64_t)b; break;
            case IR_GT: result = (int64_t)a > (int64_t)b; break;
            case IR_GE: result = (int64_t)a >= (int64_t)b; break;
            case IR_ULT: result = a < b; break;
            case IR_ULE: result = a <= b; break;
            case IR_UGT: result = a > b; break;
            case IR_UGE: result = a >= b; break;
        }
    }
    return result;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_branch_layout_call(const void* code, uint64_t route, uint64_t a, uint64_t b)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(route, a, b);
}

static void tier1_branch_layout_shape(Tier1EmitTest* test, const Tier1EmitResult* emitted, bool true_next)
{
    const uint32_t edge = emitted->cfg_input.successor_offsets[2];
    const uint32_t next_edge = edge + !true_next;
    const uint32_t other_edge = edge + true_next;
    const bool split = emitted->cfg.edge_placements[edge] == TIER1_CFG_EDGE_SPLIT && emitted->cfg.edge_placements[edge + 1] == TIER1_CFG_EDGE_SPLIT &&
                       emitted->cfg.edge_move_offsets[edge] < emitted->cfg.edge_move_offsets[edge + 1] && emitted->cfg.edge_move_offsets[edge + 1] < emitted->cfg.edge_move_offsets[edge + 2];
    tier1_emit_test_expect(test, split, "both critical edges require real phi moves");
    uint32_t branches[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t count = 0;
    for (uint32_t branch = 0; branch < emitted->code.branch_count; branch += 1)
    {
        const uint32_t record = emitted->code.branches[branch];
        if (record >= emitted->block_records[2] && record < emitted->block_records[3])
        {
            if (count < BUSTER_ARRAY_LENGTH(branches))
            {
                branches[count] = branch;
            }
            count += 1;
        }
    }
    tier1_emit_test_expect(test, count == 2, "two split edges to an adjacent block need only two branches");
    if (split && count == 2)
    {
        const uint32_t first = emitted->code.branches[branches[0]];
        const uint32_t second = emitted->code.branches[branches[1]];
        const uint32_t stub = emitted->stub_records[next_edge];
        bool falls_through = first < emitted->stub_records[other_edge] && emitted->stub_records[other_edge] <= second && second < stub && stub < emitted->block_records[3] &&
                             emitted->code.records[first].form != X86_FORM_JMP_REL8 && emitted->code.records[second].form == X86_FORM_JMP_REL8 &&
                             emitted->code.targets[branches[0]] == stub && emitted->code.targets[branches[1]] == emitted->block_records[4];
        for (uint32_t record = stub; falls_through && record < emitted->block_records[3]; record += 1)
        {
            falls_through &= emitted->origins[record] == tier1_origin(TIER1_ORIGIN_EDGE, next_edge);
        }
        tier1_emit_test_expect(test, falls_through, "the adjacent successor's final stub contains only its edge moves and no jump");
    }
}

static void tier1_branch_layout_execute(Tier1EmitTest* test, Tier1EmitResult* emitted, bool floating, uint32_t predicate, bool true_next)
{
    static const uint64_t samples[][2] = {
        { 0, 0 },
        { 1, 0 },
        { 0, 1 },
        { UINT64_MAX, 0 },
        { UINT64_C(0x8000000000000000), UINT64_C(0x7fffffffffffffff) },
        { UINT64_C(0x7fffffffffffffff), UINT64_C(0x8000000000000000) },
        { UINT64_C(0x3ff0000000000000), UINT64_C(0x4000000000000000) },
        { UINT64_C(0x4000000000000000), UINT64_C(0x3ff0000000000000) },
        { 0, UINT64_C(0x8000000000000000) },
        { UINT64_C(0x8000000000000000), 0 },
        { UINT64_C(0x7ff0000000000000), UINT64_C(0x7ff0000000000000) },
        { UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000) },
        { UINT64_C(0x7ff8000000001234), UINT64_C(0x3ff0000000000000) },
        { UINT64_C(0x3ff0000000000000), UINT64_C(0x7ff8000000001234) },
        { UINT64_C(0x7ff8000000001234), UINT64_C(0xfff8000000004321) },
    };
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (tier1_emit_test_load(test, &emitted->code, &executable, &bytes))
    {
        for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples); sample += 1)
        {
            const uint64_t a = samples[sample][0];
            const uint64_t b = samples[sample][1];
            const bool first = tier1_branch_layout_truth(floating, predicate, a, b) == true_next;
            for (uint32_t route = 0; route < 2; route += 1)
            {
                const uint64_t expected = (route != 0 ? 2 * b : 2 * a + 1) + (first ? 8u : 18u);
                const uint64_t actual = tier1_branch_layout_call(executable, route, a, b);
                tier1_emit_test_expect(test, actual == expected, "critical-edge layout preserves the predicate and both joins' phi inputs");
            }
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
}

static void tier1_branch_layout_tests(Tier1EmitTest* test)
{
    test->terms = 2;
    test->width = 8;
    test->selects = false;
    for (uint32_t floating = 0; floating < 2; floating += 1)
    {
        const uint32_t predicates = floating != 0 ? IR_GE + 1 : IR_PREDICATE_COUNT;
        for (uint32_t predicate = 0; predicate < predicates; predicate += 1)
        {
            for (uint32_t variant = 0; variant < 8; variant += 1)
            {
                const bool true_next = (variant & 1) != 0;
                const bool vector = (variant & 2) != 0;
                const bool pointer = (variant & 4) != 0;
                IrFunction function;
                const bool created = tier1_branch_layout_function(test, floating != 0, predicate, true_next, &function);
                const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK;
                tier1_emit_test_expect(test, valid, "critical-edge layout fixture is valid IR");
                IrDominance dominance;
                const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
                Tier1SelectPlan plan;
                uint32_t node = BUSTER_IR_NONE;
                const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
                tier1_emit_test_expect(test, planned, "critical-edge layout fixture selects by both paths");
                const Tier1EmitOptions options = {
                    .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI),
                    .vector = vector,
                    .rematerialize = true,
                    .certify = true,
                    .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                };
                Tier1EmitResult emitted;
                const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
                const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
                tier1_emit_test_expect(test, certified, "critical-edge layout allocation and emitted code certify");
                if (!certified && test->failures < 20)
                {
                    fprintf(stderr, "  branch layout error=%u node=%u floating=%u predicate=%u variant=%u\n", error, node, floating, predicate, variant);
                }
                if (certified)
                {
                    tier1_branch_layout_shape(test, &emitted, true_next);
                    tier1_branch_layout_execute(test, &emitted, floating != 0, predicate, true_next);
                }
                arena_pop_to(&test->output, 0);
                arena_pop_to(&test->scratch, 0);
                arena_pop_to(&test->input, 0);
            }
        }
    }
}
