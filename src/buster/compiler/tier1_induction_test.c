// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1219's induction certificates and deliberately unsafe near-matches, through real CFG allocation and emission.
// The oracle updates unsigned 32-bit bits explicitly, so the overflow regressions do not invoke signed C overflow.

static void tier1_induction_tests(Tier1EmitTest* test)
{
    const uint32_t ordinary[] = { 0, 1, 2, 127, 255, 1024, UINT32_C(0x80000000), UINT32_MAX };
    for (uint32_t shape = 0; shape < 16; shape += 1)
    {
        const bool multiple_seeds = shape == 12 || shape == 13;
        const bool bridge = shape >= 14;
        const bool bypass = shape == 15;
        const uint64_t input_mark = test->input.length;
        const IrSizes sizes = {
            .instruction_count = multiple_seeds ? 15 : bridge ? 13 : 12,
            .block_count = bypass ? 5 : multiple_seeds || bridge ? 4 : 3,
            .constant_count = multiple_seeds ? 4 : 3,
            .extra_count = multiple_seeds ? 6 : 4,
            .parameter_count = 1,
        };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create guarded induction regression");
        if (created)
        {
            const uint32_t initial = shape == 2 ? INT32_MAX - 2 : shape == 3 ? INT32_MAX : shape == 4 || shape == 5 || bypass ? INT32_MAX - 1 : shape == 11 ? UINT32_MAX : 0;
            const uint32_t stride = shape == 4 || shape == 10 ? 2 : 1;
            const uint32_t mask = multiple_seeds || bypass ? 127 : shape == 10 ? 65535 : UINT32_MAX;
            const uint32_t compared = shape == 5 ? 6 : 7;
            const uint32_t predicate = shape == 6 ? IR_LE : shape == 7 ? IR_GE : shape == 8 ? IR_GT : shape == 9 || shape == 10 || multiple_seeds || bypass ? IR_ULT : IR_LT;
            const uint32_t returned = shape == 0 || shape == 11 ? 6 : 7;
            const bool proven = shape <= 2 || shape == 7 || shape == 8 || shape == 10 || shape == 12 || shape == 14;
            const uint32_t widening = multiple_seeds ? 13 : bridge ? 11 : 10;
            function.parameter_types[0] = IR_I32;
            function.result_type = IR_I64;
            function.constants[0].low = initial;
            function.constants[1].low = stride;
            function.constants[2].low = mask;
            function.block_offsets[1] = 6;
            function.block_offsets[2] = 10;
            function.block_offsets[3] = 12;
            function.extra[0] = 0;
            function.extra[1] = 1;
            function.extra[2] = 1;
            function.extra[3] = 7;
            tier1_select_test_node(&function, 0, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 4, IR_AND, IR_I32, 0, 3, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 6, IR_PHI, IR_I32, 0, 2, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 7, IR_ADD, IR_I32, 6, 2, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 8, IR_ICMP, IR_I1, shape == 8 ? 4 : compared, shape == 8 ? compared : 4, predicate);
            tier1_select_test_node(&function, 9, IR_CBR, IR_VOID, 8, shape == 7 ? 2 : 1, shape == 7 ? 1 : 2);
            tier1_select_test_node(&function, 10, IR_SEXT, IR_I64, returned, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 11, IR_RET, IR_VOID, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
            if (multiple_seeds)
            {
                // Two distinct entry edges select zero or a higher seed. The unsigned backedge guard terminates
                // immediately if INT_MAX + 1 wraps; using the smaller seed as a common bound would miscompile it.
                function.constants[3].low = shape == 12 ? 15 : INT32_MAX;
                function.block_offsets[1] = 8;
                function.block_offsets[2] = 9;
                function.block_offsets[3] = 13;
                function.block_offsets[4] = 15;
                function.extra[0] = 0;
                function.extra[1] = 5;
                function.extra[2] = 1;
                function.extra[3] = 1;
                function.extra[4] = 2;
                function.extra[5] = 10;
                tier1_select_test_node(&function, 5, IR_CONST, IR_I32, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 6, IR_ICMP, IR_I1, 0, 1, IR_EQ);
                tier1_select_test_node(&function, 7, IR_CBR, IR_VOID, 6, 2, 1);
                tier1_select_test_node(&function, 8, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 9, IR_PHI, IR_I32, 0, 3, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 10, IR_ADD, IR_I32, 9, 2, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 11, IR_ICMP, IR_I1, 10, 4, IR_ULT);
                tier1_select_test_node(&function, 12, IR_CBR, IR_VOID, 11, 2, 3);
                tier1_select_test_node(&function, 13, IR_SEXT, IR_I64, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 14, IR_RET, IR_VOID, 13, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (shape == 14)
            {
                function.block_offsets[2] = 10;
                function.block_offsets[3] = 11;
                function.block_offsets[4] = 13;
                function.extra[2] = 2;
                tier1_select_test_node(&function, 9, IR_CBR, IR_VOID, 8, 2, 3);
                tier1_select_test_node(&function, 10, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 11, IR_SEXT, IR_I64, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 12, IR_RET, IR_VOID, 11, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (bypass)
            {
                // The bridge has two entrances. The first trip bypasses the unsigned guard and admits INT_MAX;
                // the second sum wraps negative. A guard on only the other entrance is not an invariant.
                function.block_offsets[1] = 4;
                function.block_offsets[2] = 8;
                function.block_offsets[3] = 10;
                function.block_offsets[4] = 11;
                function.block_offsets[5] = 13;
                function.extra[0] = 0;
                function.extra[1] = 0;
                function.extra[2] = 3;
                function.extra[3] = 5;
                tier1_select_test_node(&function, 0, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 1, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_PHI, IR_I32, 0, 2, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 5, IR_ADD, IR_I32, 4, 1, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 6, IR_ICMP, IR_I1, 4, 0, IR_EQ);
                tier1_select_test_node(&function, 7, IR_CBR, IR_VOID, 6, 3, 2);
                tier1_select_test_node(&function, 8, IR_ICMP, IR_I1, 5, 2, IR_ULT);
                tier1_select_test_node(&function, 9, IR_CBR, IR_VOID, 8, 3, 4);
                tier1_select_test_node(&function, 10, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 11, IR_SEXT, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 12, IR_RET, IR_VOID, 11, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t pressure = 0; pressure < 2; pressure += 1)
                {
                    const uint64_t output_mark = test->output.length;
                    IrDominance dominance;
                    const bool dominated = tier1_terminal_dominance(test, &function, path != 0, &dominance);
                    Tier1SelectPlan plan;
                    uint32_t failed = BUSTER_IR_NONE;
                    const Tier1SelectError selected = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, path != 0, &plan, &failed) : TIER1_SELECT_ERROR_INVALID;
                    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "guarded induction selects by both paths");
                    if (selected != TIER1_SELECT_OK && test->failures < 20)
                    {
                        fprintf(stderr, "  induction selection error=%u node=%u shape=%u path=%u\n", selected, failed, shape, path);
                    }
                    if (selected == TIER1_SELECT_OK)
                    {
                        tier1_emit_test_expect(test, plan.patterns[widening].rule == (proven ? TIER1_RULE_COPY : TIER1_RULE_SIGN_EXTEND),
                                               "only a complete nonoverflowing induction certificate removes signed widening");
                        const Tier1EmitOptions options = {
                            .certify = true,
                            .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                            .vector = path != 0,
                            .rematerialize = pressure != 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "guarded induction passes CFG allocation and emission");
                        if (error == TIER1_EMIT_OK)
                        {
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                const uint32_t samples = (shape >= 2 && shape <= 5) || bypass ? 1 : BUSTER_ARRAY_LENGTH(ordinary);
                                for (uint32_t sample = 0; sample < samples; sample += 1)
                                {
                                    uint32_t input = ordinary[sample];
                                    if (shape == 2 || shape == 5)
                                    {
                                        input = INT32_MAX;
                                    }
                                    else if (shape == 3 || shape == 4)
                                    {
                                        input = UINT32_C(0x80000000) + (shape == 4);
                                    }
                                    else if (shape == 9 && input > 1024)
                                    {
                                        input = sample;
                                    }
                                    const uint32_t limit = bypass ? 127 : input & mask;
                                    uint32_t phi = multiple_seeds && input == 0 ? (shape == 12 ? 15 : INT32_MAX) : initial;
                                    uint32_t step = 0;
                                    bool again = true;
                                    for (uint32_t iteration = 0; again && iteration < 65538; iteration += 1)
                                    {
                                        step = phi + stride;
                                        const uint32_t tested = shape == 5 ? phi : step;
                                        again = tier1_select_test_compare(predicate, 4, shape == 8 ? limit : tested, shape == 8 ? tested : limit) != (shape == 7);
                                        again |= bypass && phi == initial;
                                        if (again)
                                        {
                                            phi = step;
                                        }
                                    }
                                    tier1_emit_test_expect(test, !again, "independent induction oracle stays within its bounded test budget");
                                    if (!again)
                                    {
                                        const uint64_t expected = (uint64_t)(int64_t)(int32_t)(returned == 6 ? phi : step);
                                        const uint64_t actual = tier1_emit_test_call(executable, UINT64_C(0xa55a120000000000) | input, 0);
                                        tier1_emit_test_expect(test, actual == expected, "induction code preserves negative seeds, wrapped sums, guard direction and poisoned ABI bits");
                                    }
                                }
                            }
                            if (executable != MAP_FAILED)
                            {
                                munmap(executable, bytes);
                            }
                        }
                    }
                    arena_pop_to(&test->output, output_mark);
                }
            }
        }
        arena_pop_to(&test->input, input_mark);
    }
}
