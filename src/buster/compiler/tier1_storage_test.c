// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Mixed signed/unsigned widening of the same loaded I32, observed independently in memory. Eight values stay live
// before either use, forcing spills with three registers. Exact-width inputs end at a protected page; pointer phis
// force real CFG allocation and edge moves. A volatile I32 load is the conservative representation control.

static void tier1_storage_tests(Tier1EmitTest* test)
{
    const uint32_t count = 8;
    const uint32_t samples[] = { 0, 1, 127, 128, 32767, 32768, UINT32_C(0x80000000), UINT32_MAX };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    void* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool protected = mapping != MAP_FAILED && mprotect((uint8_t*)mapping + page, page, PROT_NONE) == 0;
    tier1_emit_test_expect(test, protected, "signed-storage input ends at a protected page");
    for (uint32_t shape = 0; protected && shape < 3; shape += 1)
    {
        const bool word = shape == 1;
        const bool promoted = shape != 2;
        const uint32_t stride = word ? 2 : 4;
        uint8_t* data = (uint8_t*)mapping + page - count * stride;
        const uint64_t input_mark = test->input.length;
        const IrSizes sizes = {
            .instruction_count = 5 + count * (word ? 3 : 2) + count * 6 + 2,
            .block_count = 2,
            .constant_count = 1,
            .address_count = count * 3,
            .extra_count = 4,
            .parameter_count = 2,
        };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create signed-storage pressure regression");
        if (created)
        {
            function.parameter_types[0] = IR_PTR;
            function.parameter_types[1] = IR_PTR;
            function.result_type = IR_I64;
            function.block_offsets[1] = 3;
            function.block_offsets[2] = (uint32_t)sizes.instruction_count;
            function.extra[0] = 0;
            function.extra[1] = 0;
            function.extra[2] = 0;
            function.extra[3] = 1;
            tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_PARAM, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, IR_PHI, IR_PTR, 0, 1, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 4, IR_PHI, IR_PTR, 2, 1, BUSTER_IR_NONE);
            uint32_t sources[8];
            uint32_t next = 5;
            for (uint32_t value = 0; value < count; value += 1)
            {
                const uint32_t address = next++;
                const uint32_t load = next++;
                function.addresses[value] = (IrAddress){ .offset = value * stride, .scale = 1 };
                tier1_select_test_node(&function, address, IR_ADDRESS, IR_PTR, 3, BUSTER_IR_NONE, value);
                tier1_select_test_node(&function, load, IR_LOAD, word ? IR_I16 : IR_I32, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.alignment_log2[load] = word ? 1 : 2;
                function.flags[load] = shape == 2 ? BUSTER_IR_VOLATILE : 0;
                sources[value] = load;
                if (word)
                {
                    sources[value] = next++;
                    tier1_select_test_node(&function, sources[value], IR_SEXT, IR_I32, load, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
            }
            for (uint32_t value = 0; value < count; value += 1)
            {
                const uint32_t signed_value = next++;
                const uint32_t unsigned_value = next++;
                tier1_select_test_node(&function, signed_value, IR_SEXT, IR_I64, sources[value], BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, unsigned_value, IR_ZEXT, IR_I64, sources[value], BUSTER_IR_NONE, BUSTER_IR_NONE);
                for (uint32_t kind = 0; kind < 2; kind += 1)
                {
                    const uint32_t address = next++;
                    const uint32_t store = next++;
                    const uint32_t metadata = count + value * 2 + kind;
                    function.addresses[metadata] = (IrAddress){ .offset = (value * 2 + kind) * 8, .scale = 1 };
                    tier1_select_test_node(&function, address, IR_ADDRESS, IR_PTR, 4, BUSTER_IR_NONE, metadata);
                    tier1_select_test_node(&function, store, IR_STORE, IR_VOID, address, kind == 0 ? signed_value : unsigned_value, BUSTER_IR_NONE);
                    function.alignment_log2[store] = 3;
                }
            }
            tier1_select_test_node(&function, next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, next + 1, IR_RET, IR_VOID, next, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_emit_test_expect(test, next + 2 == function.instruction_count, "signed-storage fixture fills its logical columns exactly");
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
                    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "mixed signed-storage uses select by both paths");
                    if (selected == TIER1_SELECT_OK)
                    {
                        const Tier1EmitOptions options = {
                            .certify = true,
                            .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                            .vector = path != 0,
                            .rematerialize = pressure != 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "signed-storage CFG allocation and code certify");
                        if (error != TIER1_EMIT_OK && test->failures < 20)
                        {
                            fprintf(stderr, "  signed storage emission error=%u node=%u shape=%u path=%u pressure=%u\n", error, failed, shape, path, pressure);
                        }
                        if (error == TIER1_EMIT_OK)
                        {
                            tier1_emit_test_expect(test, pressure != 0 || emitted.cfg.allocation.spill_count != 0, "three-register signed-storage case really spills");
                            for (uint32_t value = 0; value < count; value += 1)
                            {
                                tier1_emit_test_expect(test, emitted.prepared.sizes[sources[value]] == (promoted ? 8 : 4), "only sign-certified I32 values get eight-byte spill rows");
                            }
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples); sample += 1)
                                {
                                    uint64_t observed[18];
                                    for (uint32_t value = 0; value < BUSTER_ARRAY_LENGTH(observed); value += 1)
                                    {
                                        observed[value] = UINT64_C(0xa55a1234a55a5678);
                                    }
                                    for (uint32_t value = 0; value < count; value += 1)
                                    {
                                        const uint32_t bits = samples[(sample + value) % BUSTER_ARRAY_LENGTH(samples)];
                                        memcpy(data + value * stride, &bits, stride);
                                    }
                                    tier1_emit_test_expect(test, tier1_emit_test_call(executable, (uint64_t)(uintptr_t)data, (uint64_t)(uintptr_t)(observed + 1)) == 0, "signed-storage native function returns its exact value");
                                    for (uint32_t value = 0; value < count; value += 1)
                                    {
                                        const uint32_t bits = samples[(sample + value) % BUSTER_ARRAY_LENGTH(samples)];
                                        const int32_t semantic = word ? (int16_t)bits : (int32_t)bits;
                                        tier1_emit_test_expect(test, observed[1 + value * 2] == (uint64_t)(int64_t)semantic && observed[2 + value * 2] == (uint32_t)semantic,
                                                               "signed and unsigned widening remain independently correct after pressure, reloads and coalescing");
                                    }
                                    tier1_emit_test_expect(test, observed[0] == UINT64_C(0xa55a1234a55a5678) && observed[17] == UINT64_C(0xa55a1234a55a5678), "signed-storage output preserves both memory canaries");
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
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

// Both definitions are observed signed before branching, then meet in a SIGN-only I32 phi. The phi's signed and
// unsigned uses are observed separately after each incoming edge, including a negative value on either edge.
static void tier1_storage_phi_tests(Tier1EmitTest* test)
{
    const uint32_t samples[] = { 0, 1, 127, 128, 32767, 32768, UINT32_C(0x80000000), UINT32_MAX };
    const uint64_t input_mark = test->input.length;
    const IrSizes sizes = { .instruction_count = 26, .block_count = 4, .constant_count = 1, .address_count = 6, .extra_count = 4, .parameter_count = 2 };
    IrFunction function;
    const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
    tier1_emit_test_expect(test, created, "create signed-storage join regression");
    if (created)
    {
        function.parameter_types[0] = IR_PTR;
        function.parameter_types[1] = IR_PTR;
        function.result_type = IR_I64;
        function.block_offsets[1] = 15;
        function.block_offsets[2] = 16;
        function.block_offsets[3] = 17;
        function.block_offsets[4] = 26;
        function.extra[0] = 1;
        function.extra[1] = 3;
        function.extra[2] = 2;
        function.extra[3] = 5;
        for (uint32_t address = 0; address < 6; address += 1)
        {
            function.addresses[address] = (IrAddress){ .offset = address < 2 ? address * 4 : (address - 2) * 8, .scale = 1 };
        }
        tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 1, IR_PARAM, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 2, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 0);
        tier1_select_test_node(&function, 3, IR_LOAD, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 4, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 1);
        tier1_select_test_node(&function, 5, IR_LOAD, IR_I32, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 6, IR_SEXT, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 7, IR_SEXT, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 8, IR_ADDRESS, IR_PTR, 1, BUSTER_IR_NONE, 2);
        tier1_select_test_node(&function, 9, IR_STORE, IR_VOID, 8, 6, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 10, IR_ADDRESS, IR_PTR, 1, BUSTER_IR_NONE, 3);
        tier1_select_test_node(&function, 11, IR_STORE, IR_VOID, 10, 7, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 12, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 13, IR_ICMP, IR_I1, 3, 12, IR_LT);
        tier1_select_test_node(&function, 14, IR_CBR, IR_VOID, 13, 1, 2);
        tier1_select_test_node(&function, 15, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 16, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 17, IR_PHI, IR_I32, 0, 2, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 18, IR_SEXT, IR_I64, 17, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 19, IR_ZEXT, IR_I64, 17, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 20, IR_ADDRESS, IR_PTR, 1, BUSTER_IR_NONE, 4);
        tier1_select_test_node(&function, 21, IR_STORE, IR_VOID, 20, 18, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 22, IR_ADDRESS, IR_PTR, 1, BUSTER_IR_NONE, 5);
        tier1_select_test_node(&function, 23, IR_STORE, IR_VOID, 22, 19, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 24, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 25, IR_RET, IR_VOID, 24, BUSTER_IR_NONE, BUSTER_IR_NONE);
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
                tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "signed-storage join selects");
                if (selected == TIER1_SELECT_OK)
                {
                    tier1_emit_test_expect(test, plan.patterns[17].storage_width == 8 && plan.patterns[18].rule == TIER1_RULE_COPY && plan.patterns[19].rule == TIER1_RULE_ZERO_EXTEND,
                                           "join keeps its sign certificate separately from unsigned widening");
                    const Tier1EmitOptions options = {
                        .certify = true,
                        .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                        .vector = path != 0,
                        .rematerialize = pressure != 0,
                    };
                    Tier1EmitResult emitted;
                    const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                    tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "sign-certified join and edge moves certify");
                    if (error == TIER1_EMIT_OK)
                    {
                        tier1_emit_test_expect(test, emitted.prepared.sizes[17] == 8, "sign-certified phi keeps eight-byte native storage");
                        void* executable = MAP_FAILED;
                        uint64_t bytes = 0;
                        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                        {
                            for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples); sample += 1)
                            {
                                const uint32_t data[2] = { samples[sample], samples[(sample + 3) % BUSTER_ARRAY_LENGTH(samples)] };
                                const uint32_t picked = (int32_t)data[0] < 0 ? data[0] : data[1];
                                uint64_t observed[6];
                                memset(observed, 0xa5, sizeof(observed));
                                const uint64_t result = tier1_emit_test_call(executable, (uint64_t)(uintptr_t)data, (uint64_t)(uintptr_t)(observed + 1));
                                tier1_emit_test_expect(test, result == 0 && observed[1] == (uint64_t)(int64_t)(int32_t)data[0] && observed[2] == (uint64_t)(int64_t)(int32_t)data[1] &&
                                                           observed[3] == (uint64_t)(int64_t)(int32_t)picked && observed[4] == picked &&
                                                           observed[0] == UINT64_C(0xa5a5a5a5a5a5a5a5) && observed[5] == UINT64_C(0xa5a5a5a5a5a5a5a5),
                                                       "both incoming edges preserve signed bits and independent unsigned results");
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
