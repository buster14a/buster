// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1219's induction certificates and deliberately unsafe near-matches, through real CFG allocation and emission.
// The oracle updates unsigned 32-bit bits explicitly, so the overflow regressions do not invoke signed C overflow.

typedef enum Tier1InductionRoute
{
    TIER1_INDUCTION_DIRECT,
    TIER1_INDUCTION_ALTERNATE,
    TIER1_INDUCTION_BYPASS,
} Tier1InductionRoute;

typedef struct Tier1InclusiveInduction Tier1InclusiveInduction;
struct Tier1InclusiveInduction
{
    uint32_t seed;
    uint32_t stride;
    uint32_t bound;
    uint8_t predicate;
    bool swapped;
    bool false_edge;
    bool constant_bound;
    Tier1InductionRoute route;
    bool proven;
};

// The taken successor branches to the add. The other successor can leave, enter the taken successor through
// another predecessor, or bypass it and enter the add directly. 'stop' exposes the very first sum even if it wraps.
static void tier1_induction_inclusive(Tier1EmitTest* test, const Tier1InclusiveInduction* shape)
{
    const IrSizes sizes = {
        .instruction_count = 18,
        .block_count = 7,
        .constant_count = 3,
        .extra_count = 4,
        .parameter_count = 2,
    };
    const uint64_t input_mark = test->input.length;
    const uint64_t output_mark = test->output.length;
    IrFunction function;
    const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
    tier1_emit_test_expect(test, created, "create inclusive header induction fixture");
    if (created)
    {
        const uint32_t none = BUSTER_IR_NONE;
        const uint32_t offsets[] = { 0, 7, 10, 11, 12, 14, 16, 18 };
        const uint32_t incoming[] = { 0, 2, 4, 12 };
        memcpy(function.block_offsets, offsets, sizeof(offsets));
        memcpy(function.extra, incoming, sizeof(incoming));
        function.result_type = IR_I64;
        function.parameter_types[0] = IR_I32;
        function.parameter_types[1] = IR_I1;
        function.constants[0].low = shape->seed;
        function.constants[1].low = shape->stride;
        function.constants[2].low = shape->bound;
        tier1_select_test_node(&function, 0, IR_PARAM, IR_I32, 0, none, none);
        tier1_select_test_node(&function, 1, IR_PARAM, IR_I1, 1, none, none);
        tier1_select_test_node(&function, 2, IR_CONST, IR_I32, 0, none, none);
        tier1_select_test_node(&function, 3, IR_CONST, IR_I32, 1, none, none);
        tier1_select_test_node(&function, 4, IR_CONST, IR_I32, 2, none, none);
        tier1_select_test_node(&function, 5, IR_AND, IR_I32, 0, 4, none);
        tier1_select_test_node(&function, 6, IR_BR, IR_VOID, 1, none, none);
        tier1_select_test_node(&function, 7, IR_PHI, IR_I32, 0, 2, none);
        const uint32_t limit = shape->constant_bound ? 4 : 5;
        tier1_select_test_node(&function, 8, IR_ICMP, IR_I1, shape->swapped ? limit : 7, shape->swapped ? 7 : limit, shape->predicate);
        tier1_select_test_node(&function, 9, IR_CBR, IR_VOID, 8, shape->false_edge ? 3 : 2, shape->false_edge ? 2 : 3);
        tier1_select_test_node(&function, 10, IR_BR, IR_VOID, 4, none, none);
        if (shape->route == TIER1_INDUCTION_DIRECT)
        {
            tier1_select_test_node(&function, 11, IR_BR, IR_VOID, 5, none, none);
        }
        else
        {
            tier1_select_test_node(&function, 11, IR_CBR, IR_VOID, 1, shape->route == TIER1_INDUCTION_ALTERNATE ? 2 : 4, 5);
        }
        tier1_select_test_node(&function, 12, IR_ADD, IR_I32, 7, 3, none);
        tier1_select_test_node(&function, 13, IR_CBR, IR_VOID, 1, 6, 1);
        tier1_select_test_node(&function, 14, IR_SEXT, IR_I64, 7, none, none);
        tier1_select_test_node(&function, 15, IR_RET, IR_VOID, 14, none, none);
        tier1_select_test_node(&function, 16, IR_SEXT, IR_I64, 12, none, none);
        tier1_select_test_node(&function, 17, IR_RET, IR_VOID, 16, none, none);

        IrDominance dominance;
        const bool valid = tier1_terminal_dominance(test, &function, &dominance) && ir_verify(&function, &test->scratch, &dominance).error == IR_OK;
        Tier1SelectPlan plan;
        uint32_t failed = none;
        const bool planned = valid && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &failed) == TIER1_SELECT_OK;
        tier1_emit_test_expect(test, planned, "verify the inclusive CFG and select its signed widening");
        if (planned)
        {
            const bool copy = plan.covered[16] || plan.patterns[16].rule == TIER1_RULE_COPY;
            const bool extend = !plan.covered[16] && plan.patterns[16].rule == TIER1_RULE_SIGN_EXTEND;
            tier1_emit_test_expect(test, shape->proven ? copy : extend, "only a dominating inclusive edge with headroom can remove the sum's signed widening");
            if (!(shape->proven ? copy : extend) && test->failures < 20)
            {
                fprintf(stderr, "  inclusive induction seed=%u bound=%u stride=%u predicate=%u route=%u rule=%u covered=%u\n",
                        shape->seed, shape->bound, shape->stride, shape->predicate, shape->route, plan.patterns[16].rule, plan.covered[16]);
            }
            const Tier1EmitOptions options = {
                .register_mask = tier1_liveness_register_mask(),
                .certify = true,
                .rematerialize = true,
            };
            Tier1EmitResult emitted;
            const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
            const bool certified = error == TIER1_EMIT_OK && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
            tier1_emit_test_expect(test, certified, "the emitted inclusive induction loop has a valid code certificate");
            void* executable = MAP_FAILED;
            uint64_t bytes = 0;
            if (certified && tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
            {
                // Every case takes the add for stop=1, through the extra entrance where the guard itself fails.
                const uint32_t next = shape->seed + shape->stride;
                const uint64_t actual = tier1_emit_test_call(executable, UINT64_MAX, 1);
                tier1_emit_test_expect(test, actual == (uint64_t)(int64_t)(int32_t)next, "an admitted or bypassed sum sign-extends its actual low 32 bits");
                if (shape->proven)
                {
                    // With stop=0 the final phi is the first value strictly above the bound.
                    const uint64_t count = ((uint64_t)shape->bound - shape->seed) / shape->stride + 1;
                    const uint64_t final = shape->seed + count * shape->stride;
                    tier1_emit_test_expect(test, tier1_emit_test_call(executable, UINT64_MAX, 0) == final, "the inclusive edge admits equality and the final increment");
                }
            }
            if (executable != MAP_FAILED)
            {
                munmap(executable, bytes);
            }
        }
    }
    arena_pop_to(&test->output, output_mark);
    arena_pop_to(&test->input, input_mark);
}

static void tier1_induction_inclusive_tests(Tier1EmitTest* test)
{
    static const Tier1InclusiveInduction shapes[] = {
        { .stride = 1, .bound = 65535, .predicate = IR_LE, .proven = true },
        { .stride = 3, .bound = 65535, .predicate = IR_GE, .swapped = true, .proven = true },
        { .stride = 3, .bound = 65535, .predicate = IR_GT, .false_edge = true, .proven = true },
        { .stride = 1, .bound = 65535, .predicate = IR_LT, .swapped = true, .false_edge = true, .proven = true },
        { .stride = 3, .bound = 65535, .predicate = IR_ULE, .proven = true },
        { .seed = INT32_MAX - 1, .stride = 1, .bound = INT32_MAX - 1, .predicate = IR_LE, .constant_bound = true, .proven = true },
        { .seed = INT32_MAX - 3, .stride = 3, .bound = INT32_MAX - 3, .predicate = IR_LE, .constant_bound = true, .proven = true },
        { .seed = INT32_MAX, .stride = 1, .bound = INT32_MAX, .predicate = IR_LE, .constant_bound = true },
        { .seed = INT32_MAX, .stride = 1, .bound = INT32_MAX, .predicate = IR_LE },
        { .seed = INT32_MAX, .stride = 1, .bound = UINT32_MAX, .predicate = IR_ULE, .constant_bound = true },
        { .seed = INT32_MAX - 2, .stride = 3, .bound = INT32_MAX - 2, .predicate = IR_LE, .constant_bound = true },
        { .seed = INT32_MAX, .stride = 1, .bound = 65535, .predicate = IR_GE },
        { .seed = INT32_MAX, .stride = 1, .bound = 65535, .predicate = IR_LE, .route = TIER1_INDUCTION_ALTERNATE },
        { .seed = INT32_MAX, .stride = 3, .bound = 65535, .predicate = IR_LE, .route = TIER1_INDUCTION_BYPASS },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(shapes); i += 1)
    {
        tier1_induction_inclusive(test, &shapes[i]);
    }
}

static void tier1_induction_tests(Tier1EmitTest* test)
{
    const uint32_t ordinary[] = { 0, 1, 2, 127, 255, 1024, UINT32_C(0x80000000), UINT32_MAX };
    for (uint32_t variant = 0; variant < 23; variant += 1)
    {
        // Repeat the post-add <= guard with a small masked limit. Its edge still comes after the increment.
        const bool bounded_latch = variant == 22;
        const uint32_t shape = bounded_latch ? 6 : variant;
        const bool multiple_seeds = shape == 12 || shape == 13;
        const bool bridge = shape >= 14;
        const bool bypass = shape == 15;
        const bool header_guard = shape >= 16;
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
            const uint32_t initial = shape == 2 ? INT32_MAX - 2 : shape == 3 ? INT32_MAX : shape == 4 || shape == 5 || bypass || shape == 17 || shape == 18 ? INT32_MAX - 1 : shape == 11 ? UINT32_MAX : 0;
            const uint32_t stride = shape == 4 || shape == 10 || shape == 18 || shape == 19 ? 2 : 1;
            const uint32_t mask = multiple_seeds || bypass || shape >= 20 ? 127 : shape == 10 || shape == 19 || bounded_latch ? 65535 : shape == 18 ? INT32_MAX : UINT32_MAX;
            const uint32_t compared = shape == 5 ? 6 : 7;
            const uint32_t predicate = shape == 6 ? IR_LE : shape == 7 ? IR_GE : shape == 8 ? IR_GT : shape == 9 || shape == 10 || multiple_seeds || bypass || shape >= 18 ? IR_ULT : IR_LT;
            const uint32_t returned = shape == 0 || shape == 11 || header_guard || bounded_latch ? 6 : 7;
            const bool proven = shape <= 2 || shape == 7 || shape == 8 || shape == 10 || shape == 12 || shape == 14 || shape == 16 || shape == 17 || shape == 19 || shape == 20;
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
            else if (header_guard)
            {
                if (shape >= 20)
                {
                    function.extra[1] = shape == 20 ? 4 : 0;
                }
                function.block_offsets[2] = 9;
                function.block_offsets[3] = 11;
                function.block_offsets[4] = 13;
                function.extra[2] = 2;
                function.extra[3] = 9;
                tier1_select_test_node(&function, 7, IR_ICMP, IR_I1, 6, 4, predicate);
                tier1_select_test_node(&function, 8, IR_CBR, IR_VOID, 7, 2, 3);
                tier1_select_test_node(&function, 9, IR_ADD, IR_I32, 6, 2, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 10, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 11, IR_SEXT, IR_I64, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 12, IR_RET, IR_VOID, 11, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            for (uint32_t pressure = 0; pressure < 2; pressure += 1)
            {
                const uint64_t output_mark = test->output.length;
                IrDominance dominance;
                const bool dominated = tier1_terminal_dominance(test, &function, &dominance);
                Tier1SelectPlan plan;
                uint32_t failed = BUSTER_IR_NONE;
                const Tier1SelectError selected = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &failed) : TIER1_SELECT_ERROR_INVALID;
                tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "guarded induction selects by both paths");
                if (selected != TIER1_SELECT_OK && test->failures < 20)
                {
                    fprintf(stderr, "  induction selection error=%u node=%u shape=%u\n", selected, failed, shape);
                }
                if (selected == TIER1_SELECT_OK)
                {
                    tier1_emit_test_expect(test, plan.patterns[widening].rule == (proven ? TIER1_RULE_COPY : TIER1_RULE_SIGN_EXTEND),
                                           "only a complete nonoverflowing induction certificate removes signed widening");
                    const Tier1EmitOptions options = {
                        .certify = true,
                        .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
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
                            const uint32_t samples = (shape >= 2 && shape <= 5) || bypass || shape == 17 || shape == 18 ? 1 : BUSTER_ARRAY_LENGTH(ordinary);
                            for (uint32_t sample = 0; sample < samples; sample += 1)
                            {
                                uint32_t input = ordinary[sample];
                                if (shape == 2 || shape == 5 || shape == 17 || shape == 18)
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
                                uint32_t phi = shape >= 20 ? (shape == 20 ? input & mask : input) : multiple_seeds && input == 0 ? (shape == 12 ? 15 : INT32_MAX) : initial;
                                uint32_t step = 0;
                                bool again = !header_guard || tier1_select_test_compare(predicate, 4, phi, limit) != 0;
                                for (uint32_t iteration = 0; again && iteration < 65538; iteration += 1)
                                {
                                    step = phi + stride;
                                    if (header_guard)
                                    {
                                        phi = step;
                                    }
                                    const uint32_t tested = shape == 5 || header_guard ? phi : step;
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
        arena_pop_to(&test->input, input_mark);
    }
    tier1_induction_inclusive_tests(test);
}
