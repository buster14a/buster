// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1217: a memory operand folds an address computed in an earlier block, as Lua's `ra = RA(i)` at a handler's start
// and its stores in the arms its type tests choose. Included by tier1_emit_test.c. Block 0 computes p + 8 * i + 16 and
// branches on v; each arm stores to it and loads it back, and the join returns the loaded value. Where nothing else uses
// the address, no node computes it: each access addresses p + 8 * i + 16 itself, with p and i its effective uses. An
// address another user keeps (the returned sum, through a ptrtoint and an addition) stays computed, and the accesses
// read its value, so that folding never lengthens the base's and index's lives past an address that remains. Both
// selection paths, with and without a frame pointer, must certify and run natively against C.

static bool tier1_address_sink_function(Tier1EmitTest* test, bool escapes, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 15 + 2 * escapes,
        .block_count = 4,
        .extra_count = 4,
        .constant_count = 1,
        .address_count = 1,
        .parameter_count = 3,
    };
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_PTR;
        function->parameter_types[1] = IR_I64;
        function->parameter_types[2] = IR_I64;
        function->constants[0].low = 0;
        function->addresses[0] = (IrAddress){ .scale = 8, .offset = 16 };
        uint32_t next = 0;
        const uint32_t base = tier1_select_test_append(function, &next, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t index = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t value = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t address = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, base, index, 0);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t route = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, value, zero, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, route, 1, 2);
        uint32_t loaded[2] = { 0 };
        for (uint32_t arm = 0; arm < 2; arm += 1)
        {
            function->block_offsets[1 + arm] = next;
            tier1_select_test_append(function, &next, IR_STORE, IR_VOID, address, arm == 0 ? value : index, BUSTER_IR_NONE);
            loaded[arm] = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        function->block_offsets[3] = next;
        // The phi's operands, (predecessor block, value) pairs.
        function->extra[0] = 1;
        function->extra[1] = loaded[0];
        function->extra[2] = 2;
        function->extra[3] = loaded[1];
        uint32_t result = tier1_select_test_append(function, &next, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        if (escapes)
        {
            const uint32_t bits = tier1_select_test_append(function, &next, IR_PTRTOINT, IR_I64, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
            result = tier1_select_test_append(function, &next, IR_ADD, IR_I64, result, bits, BUSTER_IR_NONE);
        }
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[4] = next;
        ready = next == function->instruction_count;
    }
    return ready;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_address_sink_call(const void* code, uint64_t base, uint64_t index, uint64_t value)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(base, index, value);
}

static void tier1_address_sink_execute(Tier1EmitTest* test, Tier1EmitResult* emitted, bool escapes)
{
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (tier1_emit_test_load(test, &emitted->code, &executable, &bytes))
    {
        for (uint32_t sample = 0; sample < 8; sample += 1)
        {
            uint64_t memory[8];
            uint64_t wanted[8];
            for (uint32_t word = 0; word < BUSTER_ARRAY_LENGTH(memory); word += 1)
            {
                memory[word] = UINT64_C(0xa5a5a5a5a5a5a5a5) ^ word;
            }
            memcpy(wanted, memory, sizeof(memory));
            const uint64_t index = sample % 6;
            const uint64_t value = (sample & 1) != 0 ? UINT64_C(0xfedcba9876543210) >> sample : 0;
            const uint64_t stored = value != 0 ? value : index;
            wanted[2 + index] = stored;
            const uint64_t address = (uint64_t)(uintptr_t)(memory + 2 + index);
            const uint64_t expected = stored + (escapes ? address : 0);
            const uint64_t actual = tier1_address_sink_call(executable, (uint64_t)(uintptr_t)memory, index, value);
            tier1_emit_test_expect(test, actual == expected && memcmp(memory, wanted, sizeof(memory)) == 0,
                                   "an address folded from an earlier block stores and loads where C does in both arms");
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
}

static void tier1_address_sink_tests(Tier1EmitTest* test)
{
    for (uint32_t variant = 0; variant < 8; variant += 1)
    {
        const bool escapes = (variant & 1) != 0;
        const bool vector = (variant & 2) != 0;
        const bool pointer = (variant & 4) != 0;
        IrFunction function;
        const bool created = tier1_address_sink_function(test, escapes, &function);
        const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK;
        tier1_emit_test_expect(test, valid, "cross-block address fixture is valid IR");
        IrDominance dominance;
        const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
        tier1_emit_test_expect(test, planned, "cross-block address fixture selects by both paths");
        if (planned)
        {
            // Nodes 7 and 8 are the first arm's store and load, 10 and 11 the second's; node 3 is the address.
            bool folded = (plan.covered[3] != 0) == !escapes;
            for (uint32_t access = 7; access <= 11; access += access == 8 ? 2 : 1)
            {
                const Tier1SelectPattern* pattern = &plan.patterns[access];
                const uint32_t first = plan.effective_use_offsets[access];
                const bool store = access == 7 || access == 10;
                const bool through = pattern->values[0] == 0 && pattern->values[1] == 1 && pattern->scale == 3 && pattern->displacement == 16 &&
                                     plan.effective_use_values[first] == 0 && plan.effective_use_values[first + 1] == 1;
                const bool kept = pattern->values[0] == 3 && pattern->values[1] == BUSTER_IR_NONE && pattern->displacement == 0 && plan.effective_use_values[first] == 3;
                // A folded address makes the access's rule the addressed one; one whose base is the kept value's register, the plain one.
                const uint32_t rule = escapes ? (store ? TIER1_RULE_STORE : TIER1_RULE_LOAD) : (store ? TIER1_RULE_STORE_ADDRESS : TIER1_RULE_LOAD_ADDRESS);
                folded &= pattern->rule == rule && (escapes ? kept : through);
            }
            tier1_emit_test_expect(test, folded, "only an address no other user keeps folds into its accesses from an earlier block");
            if (!folded && test->failures < 20)
            {
                fprintf(stderr, "  address sink variant=%u covered=%u", variant, plan.covered[3]);
                for (uint32_t access = 7; access <= 11; access += access == 8 ? 2 : 1)
                {
                    const Tier1SelectPattern* pattern = &plan.patterns[access];
                    const uint32_t first = plan.effective_use_offsets[access];
                    fprintf(stderr, " [%u rule=%u v=%u,%u scale=%u disp=%d uses=%u:%u,%u]", access, pattern->rule, pattern->values[0], pattern->values[1], pattern->scale, pattern->displacement,
                            plan.effective_use_offsets[access + 1] - first, plan.effective_use_values[first], plan.effective_use_values[first + 1]);
                }
                fprintf(stderr, "\n");
            }
        }
        const Tier1EmitOptions options = {
            .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI),
            .vector = vector,
            .rematerialize = true,
            .certify = true,
            .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
        };
        Tier1EmitResult emitted;
        const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
        const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
        tier1_emit_test_expect(test, certified, "cross-block address allocation and emitted code certify");
        if (!certified && test->failures < 20)
        {
            fprintf(stderr, "  address sink error=%u node=%u variant=%u\n", error, node, variant);
        }
        if (certified)
        {
            tier1_address_sink_execute(test, &emitted, escapes);
        }
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->scratch, 0);
        arena_pop_to(&test->input, 0);
    }
}
