// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2610: selection can fold a frame address into an address-valued lea, covering its original STACK_ADDRESS node.
// That lea and copies/constant offsets of it retain the frame recipe. Included by tier1_emit_test.c after the native
// call helpers. The pressure fixture carries eight such addresses over a hostile call, then reads their differences.

static void tier1_frame_recipe_shapes(Tier1EmitTest* test)
{
    IrSlot slots[2] = {
        {
            .size = 64,
            .alignment = 8,
        },
        { .flags = BUSTER_IR_SLOT_OVERFLOW_AREA },
    };
    IrPassed passed = {
        .location = { .kind = BUSTER_ABI_LOCATION_STACK },
        .size = 24,
    };
    const IrFunction function = {
        .instruction_count = 3,
        .slot_count = 2,
        .slots = slots,
        .parameter_count = 1,
        .parameter_passed = 1,
        .passed = &passed,
    };
    for (uint32_t variant = 0; variant < 12; variant += 1)
    {
        Tier1SelectPattern patterns[3] = {
            {
                .rule = TIER1_RULE_ADDRESS,
                .stack_object = 0,
                .values = { BUSTER_IR_NONE, BUSTER_IR_NONE },
                .displacement = 16,
            },
            {
                .rule = TIER1_RULE_COPY,
                .stack_object = BUSTER_IR_NONE,
                .values = { 0, BUSTER_IR_NONE },
            },
            {
                .rule = TIER1_RULE_ADDRESS,
                .stack_object = BUSTER_IR_NONE,
                .values = { 1, BUSTER_IR_NONE },
                .displacement = 24,
            },
        };
        uint8_t covered[3] = { 0 };
        uint8_t classes[3] = { 0 };
        patterns[0].rip = variant == 1;
        patterns[0].values[1] = variant == 2 ? 1 : BUSTER_IR_NONE;
        patterns[0].values[0] = variant == 3 ? 1 : BUSTER_IR_NONE;
        classes[0] = variant == 4 ? BUSTER_TIER1_ALLOCATE_MASK : BUSTER_TIER1_ALLOCATE_GPR;
        covered[0] = variant == 5;
        patterns[0].stack_object = variant == 6 ? 1 : variant == 7 ? 2 : 0;
        patterns[0].displacement = variant == 8 ? INT32_MAX : variant == 9 ? INT32_MIN : 16;
        patterns[2].displacement = variant == 8 ? 1 : variant == 9 ? -1 : 24;
        patterns[0].rule = variant == 10 ? TIER1_RULE_STACK_ADDRESS : TIER1_RULE_ADDRESS;
        covered[2] = variant == 11;
        const Tier1SelectPlan plan = {
            .function = &function,
            .patterns = patterns,
            .covered = covered,
        };
        const Tier1EmitResult emitted = { .prepared = { .classes = classes } };
        uint8_t operations[3] = { 0 };
        uint8_t flags[3] = { 0 };
        uint64_t values[3] = { 0 };
        int64_t addends[3] = { 0 };
        tier1_emit_cfg_recipes(&plan, &emitted, operations, flags, values, addends);
        for (uint32_t node = 0; node < 3; node += 1)
        {
            const bool root = variant < 2 || variant >= 8;
            const bool eligible = root && !(node == 2 && (variant == 8 || variant == 9 || variant == 11));
            const uint8_t operation = eligible ? (variant == 1 ? BUSTER_TIER1_REMAT_SYMBOL : BUSTER_TIER1_REMAT_STACK) : BUSTER_TIER1_REMAT_NONE;
            const int64_t first = variant == 10 ? 0 : patterns[0].displacement;
            const int64_t displacement = eligible ? first + (node == 2 ? patterns[2].displacement : 0) : 0;
            tier1_emit_test_expect(test, operations[node] == operation && values[node] == 0 && addends[node] == displacement &&
                                   flags[node] == (eligible ? BUSTER_TIER1_REMAT_STABLE : 0),
                                   "folded frame recipes keep constant offsets, reject indexed, covered, mask and incoming addresses, and preserve folded symbols");
        }
    }
}

#define BUSTER_TIER1_FRAME_RECIPE_ADDRESSES 8u

static bool tier1_frame_recipe_function(Tier1EmitTest* test, uint32_t mode, Tier1CallFixture* fixture, uint32_t* addresses)
{
    const IrSizes sizes = {
        .instruction_count = 48,
        .block_count = 1,
        .constant_count = 1,
        .slot_count = 1,
        .address_count = BUSTER_TIER1_FRAME_RECIPE_ADDRESSES,
        .call_count = 1,
        .parameter_count = 1,
    };
    IrFunction* function = &fixture->function;
    const bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I64;
        function->symbol_count = 2;
        function->slots[0] = (IrSlot){
            .size = 128,
            .alignment = mode == 2 ? 64 : 8,
            .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN,
        };
        function->constants[0].low = 96;
        uint32_t next = 0;
        const uint32_t x = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < BUSTER_TIER1_FRAME_RECIPE_ADDRESSES; k += 1)
        {
            function->addresses[k] = (IrAddress){
                .offset = 8 * (int64_t)k,
                .scale = 1,
            };
            const uint32_t base = tier1_select_test_append(function, &next, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t displaced = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, base, BUSTER_IR_NONE, k);
            addresses[k] = tier1_select_test_append(function, &next, IR_PTRTOINT, IR_I64, displaced, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        if (mode == 2)
        {
            const uint32_t bytes = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_ALLOCA, IR_PTR, bytes, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        tier1_call_descriptor(function, 0, 0, 0, IR_VOID);
        tier1_select_test_append(function, &next, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
        uint32_t sum = x;
        for (uint32_t k = 1; k < BUSTER_TIER1_FRAME_RECIPE_ADDRESSES; k += 1)
        {
            const uint32_t difference = tier1_select_test_append(function, &next, IR_SUB, IR_I64, addresses[k], addresses[0], BUSTER_IR_NONE);
            sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, difference, BUSTER_IR_NONE);
        }
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[1] = next;
    }
    return ready;
}

static void tier1_frame_recipe_tests(Tier1EmitTest* test)
{
    tier1_frame_recipe_shapes(test);
    for (uint32_t mode = 0; mode < 3; mode += 1)
    {
        Tier1CallFixture fixture = {
            .width = 8,
            .helper_count = 1,
            .kind = { TIER1_CALL_FENCE },
        };
        uint32_t addresses[BUSTER_TIER1_FRAME_RECIPE_ADDRESSES];
        const bool ready = tier1_frame_recipe_function(test, mode, &fixture, addresses);
        tier1_emit_test_expect(test, ready, "construct the folded-frame pressure fixture");
        for (uint32_t remat = 0; ready && remat < 2; remat += 1)
        {
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const bool selected = tier1_select_plan(&fixture.function, 0, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
            bool folded = selected;
            for (uint32_t k = 0; folded && k < BUSTER_TIER1_FRAME_RECIPE_ADDRESSES; k += 1)
            {
                const Tier1SelectPattern* pattern = &plan.patterns[addresses[k]];
                folded = !plan.covered[addresses[k]] && pattern->rule == TIER1_RULE_ADDRESS && pattern->stack_object == 0 && !pattern->rip &&
                         pattern->values[0] == BUSTER_IR_NONE && pattern->values[1] == BUSTER_IR_NONE && pattern->displacement == 8 * (int32_t)k;
            }
            tier1_emit_test_expect(test, folded, "the selector folds every escaping frame address into an address-valued lea");
            Tier1EmitDiagnostic diagnostic = { 0 };
            const Tier1EmitOptions options = {
                .certify = true,
                .diagnostic = &diagnostic,
                .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI),
                .frame_flags = mode == 1 ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                .rematerialize = remat != 0,
            };
            Tier1EmitResult emitted;
            const Tier1EmitError error = selected ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
            if (error != TIER1_EMIT_OK && test->failures < 20)
            {
                fprintf(stderr, "  frame recipe mode=%u remat=%u error=%u node=%u cause=%u cause_error=%u\n", mode, remat, error, node, diagnostic.cause, diagnostic.error);
            }
            tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "folded frame addresses emit with a stable rsp, rbp or realigned dynamic frame base");
            if (error == TIER1_EMIT_OK)
            {
                const uint32_t base = mode == 0 ? X86_REGISTER_RSP : mode == 1 ? X86_REGISTER_RBP : BUSTER_TIER1_FRAME_REALIGN_BASE;
                Tier1CfgCheck check;
                tier1_emit_test_expect(test, emitted.general && emitted.frame.base_register == base && tier1_cfg_verify(&emitted.cfg_input, &emitted.cfg, &test->scratch, &check),
                                       "folded frame allocation verifies and keeps the stable frame base");
                tier1_emit_test_expect(test, (emitted.rematerializations != 0) == (remat != 0), "pressure recomputes folded frame addresses only with rematerialization");
                Tier1CallObserved observed = { 0 };
                uint8_t* mapping = mmap(0, BUSTER_TIER1_CALL_CODE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                uint64_t targets[BUSTER_TIER1_CALL_HELPERS] = { 0 };
                const uint8_t* entry = mapping != MAP_FAILED ? tier1_call_load(test, &fixture, &emitted, &observed, mapping, targets) : 0;
                tier1_emit_test_expect(test, mapping != MAP_FAILED, "map folded-frame code and its hostile call");
                for (uint32_t sample = 0; entry != 0 && sample < 8; sample += 1)
                {
                    const uint64_t arguments[6] = { UINT64_MAX - sample * UINT64_C(0x102030405060708) };
                    const uint64_t actual = tier1_call_invoke(entry, arguments);
                    tier1_emit_test_expect(test, actual == arguments[0] + 224 && observed.count[0] == sample + 1 && observed.alignment[0] == 8,
                                           "folded frame addresses survive a hostile call and retain their offsets after stack movement");
                }
                if (mapping != MAP_FAILED)
                {
                    munmap(mapping, BUSTER_TIER1_CALL_CODE_BYTES);
                }
            }
            arena_pop_to(&test->output, 0);
        }
        arena_pop_to(&test->input, 0);
    }
}
