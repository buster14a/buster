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

// Signed, positive, volatile and opaque definitions meet in I32 phis. Each edge shares the same spill width, while
// only a phi with all signed inputs retains SIGN. Signed and unsigned results are observed separately after the join.
static void tier1_storage_phi_tests(Tier1EmitTest* test)
{
    const uint32_t samples[] = { 0, 1, 127, 128, 32767, 32768, UINT32_C(0x80000000), UINT32_MAX };
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        const uint64_t input_mark = test->input.length;
        const IrSizes sizes = { .instruction_count = 26, .block_count = 4, .constant_count = 2, .address_count = 6, .extra_count = 4, .parameter_count = 2 };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create signed-storage join regression");
        if (created)
        {
            function.constants[1].low = 7;
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
            if (shape == 1)
            {
                tier1_select_test_node(&function, 5, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (shape == 2)
            {
                function.flags[5] = BUSTER_IR_VOLATILE;
            }
            else if (shape == 3)
            {
                tier1_select_test_node(&function, 4, IR_PTRTOINT, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 5, IR_TRUNC, IR_I32, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
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
                        tier1_emit_test_expect(test, plan.patterns[17].storage_width == 8 && plan.patterns[18].rule == (shape >= 2 ? TIER1_RULE_SIGN_EXTEND : TIER1_RULE_COPY) && plan.patterns[19].rule == TIER1_RULE_ZERO_EXTEND,
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
                            tier1_emit_test_expect(test, emitted.prepared.sizes[17] == 8 && emitted.prepared.sizes[3] == 8 && emitted.prepared.sizes[5] == 8, "all phi operands share eight-byte storage, including positive and opaque inputs");
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples); sample += 1)
                                {
                                    const uint32_t data[2] = { samples[sample], samples[(sample + 3) % BUSTER_ARRAY_LENGTH(samples)] };
                                    const uint32_t second = shape == 1 ? 7 : shape == 3 ? (uint32_t)(uintptr_t)data : data[1];
                                    const uint32_t picked = (int32_t)data[0] < 0 ? data[0] : second;
                                    uint64_t observed[6];
                                    memset(observed, 0xa5, sizeof(observed));
                                    const uint64_t result = tier1_emit_test_call(executable, (uint64_t)(uintptr_t)data, (uint64_t)(uintptr_t)(observed + 1));
                                    tier1_emit_test_expect(test, result == 0 && observed[1] == (uint64_t)(int64_t)(int32_t)data[0] && observed[2] == (uint64_t)(int64_t)(int32_t)second &&
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
}

static void tier1_storage_expression_tests(Tier1EmitTest* test)
{
    const uint32_t samples[] = { 0, 1, 127, 32767, 32768, INT32_MAX, UINT32_C(0x80000000), UINT32_MAX };
    uint64_t random = UINT64_C(0x1219ab8071e5c034);
    for (uint32_t shape = 0; shape < 26; shape += 1)
    {
        const bool full_load = shape >= 14 && shape <= 17;
        const bool unbounded = shape == 1 || shape == 3 || shape == 13 || shape == 19 || shape == 21 || shape == 23 || shape == 25;
        const bool negate = shape == 2 || shape == 3;
        const bool invert = shape == 4 || shape == 15;
        const bool shift = shape == 5 || shape == 14;
        const bool immediate = shift || (shape >= 6 && shape <= 8) || shape == 12 || shape == 13 || shape == 16 || shape >= 22;
        const bool signed_right = (shape >= 9 && shape <= 11) || shape == 17 || shape == 18 || shape == 20;
        const uint32_t op = shape >= 20 ? IR_MUL : shape >= 18 ? IR_SUB : negate ? IR_NEG : invert ? IR_NOT : shift ? IR_ASHR : shape == 6 || shape == 10 ? IR_AND : shape == 7 || shape == 11 ? IR_OR :
                            shape == 8 || shape == 9 || shape == 16 || shape == 17 ? IR_XOR : IR_ADD;
        const uint32_t constant = shape >= 24 ? UINT32_MAX : shape >= 22 ? 2 : shift ? (full_load ? 31 : 3) : shape == 12 || shape == 13 ? UINT32_MAX - 2 : UINT32_C(0x8000ffff);
        const uint32_t expected_rule = unbounded ? TIER1_RULE_SIGN_EXTEND : negate || invert ? TIER1_RULE_UNARY : immediate ? (op == IR_ADD ? TIER1_RULE_LEA_ADD : TIER1_RULE_IMMEDIATE) :
                                       op == IR_ADD ? TIER1_RULE_LEA_ADD : TIER1_RULE_REGISTER;
        const uint64_t input_mark = test->input.length;
        const IrSizes sizes = { .instruction_count = 9, .block_count = 1, .constant_count = 1, .parameter_count = 2 };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create widened expression regression");
        if (created)
        {
            function.parameter_types[0] = full_load ? IR_PTR : IR_I32;
            function.parameter_types[1] = IR_I32;
            function.result_type = IR_I64;
            function.block_offsets[1] = 9;
            function.constants[0].low = constant;
            tier1_select_test_node(&function, 0, IR_PARAM, function.parameter_types[0], 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_PARAM, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, full_load ? IR_LOAD : IR_TRUNC, full_load ? IR_I32 : IR_I16, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function.alignment_log2[2] = full_load ? 2 : 0;
            tier1_select_test_node(&function, 3, full_load ? IR_COPY : IR_SEXT, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 4, IR_TRUNC, IR_I16, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 5, immediate ? IR_CONST : signed_right ? IR_SEXT : IR_ZEXT, IR_I32, immediate ? 0 : 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 6, (uint8_t)op, IR_I32, unbounded ? 0 : 3, negate || invert ? BUSTER_IR_NONE : unbounded && !immediate ? 1 : 5, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 7, IR_SEXT, IR_I64, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 8, IR_RET, IR_VOID, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t pressure = 0; pressure < 2; pressure += 1)
                {
                    const uint64_t output_mark = test->output.length;
                    Tier1SelectPlan plan;
                    uint32_t failed = BUSTER_IR_NONE;
                    const Tier1SelectError selected = tier1_select_plan(&function, 0, &test->output, &test->scratch, path != 0, &plan, &failed);
                    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "widened expression selects");
                    if (selected == TIER1_SELECT_OK)
                    {
                        tier1_emit_test_expect(test, plan.patterns[7].rule == expected_rule && plan.covered[6] == !unbounded,
                                               "only a complete representation and overflow proof widens the expression");
                        const Tier1EmitOptions options = {
                            .certify = true,
                            .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                            .vector = path != 0,
                            .rematerialize = pressure != 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "widened expression allocates and emits");
                        if (error == TIER1_EMIT_OK)
                        {
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                for (uint32_t sample = 0; sample < 128; sample += 1)
                                {
                                    const uint32_t a = sample < 64 ? samples[sample / 8] : (uint32_t)tier1_emit_test_random(&random);
                                    const uint32_t b = sample < 64 ? samples[sample % 8] : (uint32_t)tier1_emit_test_random(&random);
                                    const uint32_t left = full_load || unbounded ? a : (uint32_t)(int32_t)(int16_t)a;
                                    const uint32_t right = immediate ? constant : unbounded ? b : signed_right ? (uint32_t)(int32_t)(int16_t)b : (uint16_t)b;
                                    uint32_t result = 0;
                                    switch (op)
                                    {
                                    case IR_ADD: result = left + right; break;
                                    case IR_SUB: result = left - right; break;
                                    case IR_MUL: result = left * right; break;
                                    case IR_NEG: result = 0u - left; break;
                                    case IR_NOT: result = ~left; break;
                                    case IR_AND: result = left & right; break;
                                    case IR_OR: result = left | right; break;
                                    case IR_XOR: result = left ^ right; break;
                                    case IR_ASHR:
                                        result = left >> right;
                                        result |= (left & UINT32_C(0x80000000)) != 0 ? UINT32_MAX << (32 - right) : 0;
                                        break;
                                    }
                                    const uint64_t expected = (uint64_t)(int64_t)(int32_t)result;
                                    const uint64_t first = full_load ? (uint64_t)(uintptr_t)&a : UINT64_C(0xa55a870000000000) | a;
                                    const uint64_t actual = tier1_emit_test_call(executable, first, UINT64_C(0xf17e200000000000) | b);
                                    tier1_emit_test_expect(test, actual == expected, "widened fields, shifts and bitwise operations preserve exact signed bits and unbounded I32 wrapping");
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

static void tier1_storage_narrow_arithmetic_tests(Tier1EmitTest* test)
{
    const uint32_t samples[] = { 0, 1, 127, 128, 255, 256, 32767, 32768, 65535, 65536, INT32_MAX, UINT32_MAX };
    const uint32_t operations[] = { IR_ADD, IR_SUB, IR_NEG, IR_NOT, IR_ADD, IR_SUB };
    uint64_t random = UINT64_C(0xc0031219f00d0091);
    for (uint32_t word = 0; word < 2; word += 1)
    {
        for (uint32_t shape = 0; shape < BUSTER_ARRAY_LENGTH(operations); shape += 1)
        {
            const bool unary = shape == 2 || shape == 3;
            const bool folded = shape < 4;
            const uint32_t type = word != 0 ? IR_I16 : IR_I8;
            const uint32_t mask = word != 0 ? UINT16_MAX : UINT8_MAX;
            const uint64_t input_mark = test->input.length;
            const IrSizes sizes = { .instruction_count = 10, .block_count = 1, .parameter_count = 2 };
            IrFunction function;
            const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
            tier1_emit_test_expect(test, created, "create narrow arithmetic storage regression");
            if (created)
            {
                function.parameter_types[0] = IR_I32;
                function.parameter_types[1] = IR_I32;
                function.result_type = IR_I64;
                function.block_offsets[1] = 10;
                tier1_select_test_node(&function, 0, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 1, IR_PARAM, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_TRUNC, (uint8_t)type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, IR_ZEXT, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_TRUNC, (uint8_t)type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 5, IR_ZEXT, IR_I32, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 6, (uint8_t)operations[shape], IR_I32, shape == 4 ? 0 : 3, unary ? BUSTER_IR_NONE : shape == 5 ? 1 : 5, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 7, IR_TRUNC, (uint8_t)type, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 8, IR_ZEXT, IR_I64, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    for (uint32_t pressure = 0; pressure < 2; pressure += 1)
                    {
                        const uint64_t output_mark = test->output.length;
                        Tier1SelectPlan plan;
                        uint32_t failed = BUSTER_IR_NONE;
                        const Tier1SelectError selected = tier1_select_plan(&function, 0, &test->output, &test->scratch, path != 0, &plan, &failed);
                        tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "narrow arithmetic selects");
                        if (selected == TIER1_SELECT_OK)
                        {
                            tier1_emit_test_expect(test, plan.patterns[7].rule == (folded ? unary ? TIER1_RULE_UNARY : TIER1_RULE_REGISTER : TIER1_RULE_TRUNCATE) && plan.covered[6] == folded,
                                                   "a byte or word ALU fold requires both semantic range and actual zero storage");
                            const Tier1EmitOptions options = {
                                .certify = true,
                                .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                                .vector = path != 0,
                                .rematerialize = pressure != 0,
                            };
                            Tier1EmitResult emitted;
                            const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                            tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "narrow arithmetic allocation and code certify");
                            if (error == TIER1_EMIT_OK)
                            {
                                void* executable = MAP_FAILED;
                                uint64_t bytes = 0;
                                if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                                {
                                    for (uint32_t sample = 0; sample < 208; sample += 1)
                                    {
                                        const uint32_t a = sample < 144 ? samples[sample / 12] : (uint32_t)tier1_emit_test_random(&random);
                                        const uint32_t b = sample < 144 ? samples[sample % 12] : (uint32_t)tier1_emit_test_random(&random);
                                        const uint32_t left = shape == 4 ? a : a & mask;
                                        const uint32_t right = shape == 5 ? b : b & mask;
                                        const uint32_t result = operations[shape] == IR_ADD ? left + right : operations[shape] == IR_SUB ? left - right : operations[shape] == IR_NEG ? 0u - left : ~left;
                                        const uint64_t actual = tier1_emit_test_call(executable, UINT64_C(0x9876a55a00000000) | a, UINT64_C(0x2345f17e00000000) | b);
                                        tier1_emit_test_expect(test, actual == (result & mask), "byte and word wrapping preserves every canonical upper bit, including aliased and poisoned inputs");
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
}

static void tier1_storage_scoped_tests(Tier1EmitTest* test)
{
    const uint32_t samples[] = { 0, 1, 127, 128, 65535, INT32_MAX, UINT32_C(0x80000000), UINT32_MAX };
    uint64_t random = UINT64_C(0x76e31219cafe0921);
    for (uint32_t shape = 0; shape < 18; shape += 1)
    {
        const uint64_t input_mark = test->input.length;
        const IrSizes sizes = { .instruction_count = 12, .block_count = 3, .constant_count = 3, .parameter_count = 2 };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create scoped nonnegative conversion regression");
        if (created)
        {
            const uint32_t predicate = shape == 1 || shape == 9 ? IR_LT : shape == 2 || shape == 8 || shape == 12 ? IR_ULT :
                                       shape == 3 || shape == 13 ? IR_ULE : shape == 4 ? IR_UGT : shape == 5 ? IR_GT : shape == 6 ? IR_EQ : shape == 7 ? IR_NE : IR_GE;
            const bool bounded = (shape >= 2 && shape <= 4) || shape == 9;
            const uint32_t limit = shape == 8 ? 1 : bounded || shape == 5 || shape >= 12 ? 4 : 2;
            const bool proven = shape < 7 || shape == 12 || shape == 14 || shape == 16 || shape == 17;
            const bool copied = proven || shape == 11;
            const uint32_t source = shape >= 14 ? 3 : 5;
            function.parameter_types[0] = IR_I32;
            function.parameter_types[1] = IR_I32;
            function.result_type = IR_I64;
            function.block_offsets[1] = 8;
            function.block_offsets[2] = 10;
            function.block_offsets[3] = 12;
            function.constants[1].low = 127;
            function.constants[2].low = shape == 5 ? UINT32_MAX : UINT32_C(0x80000000);
            tier1_select_test_node(&function, 0, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_PARAM, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 4, IR_AND, IR_I32, 1, 3, BUSTER_IR_NONE);
            if (shape == 5 || shape >= 12)
            {
                tier1_select_test_node(&function, 4, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            tier1_select_test_node(&function, 5, shape == 11 ? IR_COPY : IR_OR, IR_I32, 0, shape == 11 ? BUSTER_IR_NONE : 2, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 6, IR_ICMP, IR_I1, shape == 4 ? limit : 5, shape == 4 ? 5 : limit, predicate);
            if (shape >= 14)
            {
                tier1_select_test_node(&function, 3, IR_OR, IR_I32, 0, 2, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 4, IR_ICMP, IR_I1, 3, 2, shape == 17 ? IR_NE : IR_EQ);
                tier1_select_test_node(&function, 5, IR_ZEXT, IR_I32, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 6, shape == 16 ? IR_COPY : IR_ICMP, IR_I1, shape == 16 ? 4 : 5,
                                       shape == 16 ? BUSTER_IR_NONE : 2, shape == 16 ? BUSTER_IR_NONE : shape == 14 ? IR_NE : IR_EQ);
            }
            tier1_select_test_node(&function, 7, IR_CBR, IR_VOID, 6, shape == 1 ? 2 : 1, shape == 1 ? 1 : 2);
            tier1_select_test_node(&function, 8, IR_SEXT, IR_I64, source, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 10, IR_SEXT, IR_I64, source, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 11, shape == 10 ? IR_BR : IR_RET, IR_VOID, shape == 10 ? 1 : 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
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
                    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "scoped conversions select");
                    if (selected == TIER1_SELECT_OK)
                    {
                        tier1_emit_test_expect(test, plan.patterns[8].rule == (copied ? TIER1_RULE_COPY : TIER1_RULE_SIGN_EXTEND) &&
                                               (shape != 11 || plan.patterns[0].sign_result),
                                               "a guarded zero representation or an explicit shared signed entry representation removes the conversion");
                        if (plan.patterns[8].rule != (copied ? TIER1_RULE_COPY : TIER1_RULE_SIGN_EXTEND))
                        {
                            fprintf(stderr, "  scoped conversion shape=%u path=%u pressure=%u rule=%u expected=%u\n", shape, path, pressure,
                                    plan.patterns[8].rule, copied ? TIER1_RULE_COPY : TIER1_RULE_SIGN_EXTEND);
                        }
                        Tier1EmitDiagnostic diagnostic = { 0 };
                        const Tier1EmitOptions options = {
                            .diagnostic = &diagnostic,
                            .certify = true,
                            .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                            .vector = path != 0,
                            .rematerialize = pressure != 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "scoped conversion allocation and code certify");
                        if (error == TIER1_EMIT_OK)
                        {
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                for (uint32_t sample = 0; sample < 128; sample += 1)
                                {
                                    const uint32_t a = sample < 64 ? samples[sample / 8] : (uint32_t)tier1_emit_test_random(&random);
                                    const uint32_t b = sample < 64 ? samples[sample % 8] : (uint32_t)tier1_emit_test_random(&random);
                                    const uint64_t actual = tier1_emit_test_call(executable, UINT64_C(0x7f80a55a00000000) | a, UINT64_C(0x9876f17e00000000) | b);
                                    tier1_emit_test_expect(test, actual == (uint64_t)(int64_t)(int32_t)a,
                                                           "guarded and bypass paths return exact signed bits with poisoned ABI halves");
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

static void tier1_storage_loop_state_tests(Tier1EmitTest* test)
{
    const uint32_t strides[] = { 0, 1, 3, INT32_MAX, UINT32_C(0x80000000), UINT32_MAX };
    for (uint32_t shape = 0; shape < 6; shape += 1)
    {
        const bool bridge = shape == 5;
        const bool proven = shape == 0 || shape == 1 || bridge;
        const uint32_t initial = shape == 2 ? INT32_MAX : shape >= 1 && shape <= 4 ? INT32_MAX - 1 : 2;
        const uint32_t limit = shape == 1 || shape == 3 ? INT32_MAX : shape == 2 || shape == 4 ? UINT32_C(0x80000000) : 0;
        const uint64_t input_mark = test->input.length;
        const IrSizes sizes = { .instruction_count = bridge ? 15 : 14, .block_count = bridge ? 4 : 3, .constant_count = 3, .extra_count = 4, .parameter_count = 1 };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create wrapping loop-state storage regression");
        if (created)
        {
            function.parameter_types[0] = IR_I32;
            function.result_type = IR_I64;
            function.constants[0].low = initial;
            function.constants[1].low = limit;
            function.constants[2].low = 1;
            function.block_offsets[1] = 5;
            function.block_offsets[2] = 9;
            function.block_offsets[3] = bridge ? 10 : 14;
            if (bridge)
            {
                function.block_offsets[4] = 15;
            }
            function.extra[0] = 0;
            function.extra[1] = 1;
            function.extra[2] = bridge ? 2 : 1;
            function.extra[3] = 6;
            tier1_select_test_node(&function, 0, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 4, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 5, IR_PHI, IR_I32, 0, 2, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 6, IR_ADD, IR_I32, 5, 0, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 7, IR_ICMP, IR_I1, shape == 3 ? 5 : 6, 2, shape == 4 ? IR_ULT : IR_LT);
            tier1_select_test_node(&function, 8, IR_CBR, IR_VOID, 7, bridge ? 2 : 1, bridge ? 3 : 2);
            const uint32_t exit = bridge ? 10 : 9;
            if (bridge)
            {
                tier1_select_test_node(&function, 9, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            tier1_select_test_node(&function, exit, IR_SEXT, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, exit + 1, IR_ADD, IR_I32, 5, 3, BUSTER_IR_NONE);
            tier1_select_test_node(&function, exit + 2, IR_SEXT, IR_I64, exit + 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, exit + 3, IR_ADD, IR_I64, exit, exit + 2, BUSTER_IR_NONE);
            tier1_select_test_node(&function, exit + 4, IR_RET, IR_VOID, exit + 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
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
                    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "wrapping loop state selects");
                    if (selected == TIER1_SELECT_OK)
                    {
                        tier1_emit_test_expect(test, plan.patterns[6].sign_result == proven && plan.patterns[exit + 2].rule == (proven ? TIER1_RULE_LEA_ADD : TIER1_RULE_SIGN_EXTEND),
                                               "every incoming edge must exclude INT_MAX before a signed loop state permits +1 at 64 bits");
                        const Tier1EmitOptions options = {
                            .certify = true,
                            .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                            .vector = path != 0,
                            .rematerialize = pressure != 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "wrapping loop state allocation and code certify");
                        if (error == TIER1_EMIT_OK)
                        {
                            tier1_emit_test_expect(test, emitted.prepared.sizes[5] == (proven ? 8 : 4) && emitted.prepared.sizes[6] == (proven ? 8 : 4),
                                                   "signed loop state and its wrapping definition retain the same spill width");
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                const uint32_t samples = shape == 0 || bridge ? BUSTER_ARRAY_LENGTH(strides) : 1;
                                for (uint32_t sample = 0; sample < samples; sample += 1)
                                {
                                    const uint32_t stride = samples == 1 ? 1 : strides[sample];
                                    uint32_t phi = initial;
                                    bool again = true;
                                    for (uint32_t iteration = 0; again && iteration < 3; iteration += 1)
                                    {
                                        const uint32_t next = phi + stride;
                                        const uint32_t tested = shape == 3 ? phi : next;
                                        again = shape == 4 ? tested < limit : (int32_t)tested < (int32_t)limit;
                                        if (again)
                                        {
                                            phi = next;
                                        }
                                    }
                                    tier1_emit_test_expect(test, !again, "wrapping-state oracle terminates within its independent bound");
                                    if (!again)
                                    {
                                        const uint64_t expected = (uint64_t)(int64_t)(int32_t)phi + (uint64_t)(int64_t)(int32_t)(phi + 1u);
                                        const uint64_t actual = tier1_emit_test_call(executable, UINT64_C(0xa55a123400000000) | stride, 0);
                                        tier1_emit_test_expect(test, actual == expected, "loop arithmetic wraps at 32 bits while separate signed uses retain negative and INT_MAX boundaries");
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

// JIT entries have no Clang function-type metadata prefix. Only omit that probe at the call boundary;
// all other sanitizer checks in the fixture and generated-code construction remain enabled.
static __attribute__((noinline, no_sanitize("function"))) uint64_t tier1_storage_parameter_call(const void* code, uint64_t first, uint64_t second, uint64_t third)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(first, second, third, 0, 0, 0, third);
}

static void tier1_storage_parameter_tests(Tier1EmitTest* test)
{
    const uint32_t samples[] = { 0, 1, 127, 65535, INT32_MAX, UINT32_C(0x80000000), UINT32_C(0x80000001), UINT32_MAX };
    uint64_t random = UINT64_C(0x1219ae44b823c099);
    for (uint32_t shape = 0; shape < 3; shape += 1)
    {
        const uint64_t input_mark = test->input.length;
        const uint32_t parameters = shape == 0 ? 3 : 7;
        const uint32_t second = 2;
        const uint32_t second_argument = shape == 0 ? 2 : 6;
        const bool cfg = shape == 2;
        const IrSizes sizes = { .instruction_count = 18 + cfg, .block_count = cfg ? 2 : 1, .parameter_count = parameters };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create shared signed parameter regression");
        if (created)
        {
            function.result_type = IR_I64;
            for (uint32_t p = 0; p < parameters; p += 1)
            {
                function.parameter_types[p] = IR_I32;
            }
            for (uint32_t p = 0; p < 3; p += 1)
            {
                tier1_select_test_node(&function, p, IR_PARAM, IR_I32, p == second ? second_argument : p, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            uint32_t next = 3;
            if (cfg)
            {
                tier1_select_test_node(&function, next++, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function.block_offsets[1] = next;
            }
            function.block_offsets[sizes.block_count] = (uint32_t)sizes.instruction_count;
            const uint32_t copied = next++;
            tier1_select_test_node(&function, copied, IR_COPY, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            uint32_t values[7];
            for (uint32_t v = 0; v < 7; v += 1)
            {
                values[v] = next++;
                const uint32_t source = v == 0 || v == 2 ? 0 : v == 1 ? copied : v < 6 ? second : 1;
                tier1_select_test_node(&function, values[v], v == 2 || v == 5 ? IR_ZEXT : IR_SEXT, IR_I64, source, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            uint32_t sum = values[0];
            for (uint32_t v = 1; v < 7; v += 1)
            {
                const uint32_t added = next++;
                tier1_select_test_node(&function, added, IR_ADD, IR_I64, sum, values[v], BUSTER_IR_NONE);
                sum = added;
            }
            tier1_select_test_node(&function, next++, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_emit_test_expect(test, next == function.instruction_count, "shared signed parameter fixture fills its exact columns");
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
                    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "shared signed parameters select");
                    if (selected == TIER1_SELECT_OK)
                    {
                        tier1_emit_test_expect(test, plan.patterns[0].sign_result && plan.patterns[second].sign_result && !plan.patterns[1].sign_result &&
                                               plan.patterns[values[0]].rule == TIER1_RULE_COPY && plan.patterns[values[1]].rule == TIER1_RULE_COPY &&
                                               plan.patterns[values[3]].rule == TIER1_RULE_COPY && plan.patterns[values[4]].rule == TIER1_RULE_COPY &&
                                               plan.patterns[values[6]].rule == TIER1_RULE_SIGN_EXTEND,
                                               "two widening roots share an explicit entry conversion while a single use remains local");
                        Tier1EmitDiagnostic diagnostic = { 0 };
                        const Tier1EmitOptions options = {
                            .diagnostic = &diagnostic,
                            .certify = true,
                            .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                            .vector = path != 0,
                            .rematerialize = pressure != 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "shared signed parameter allocation and code certify");
                        if (error != TIER1_EMIT_OK)
                        {
                            fprintf(stderr, "  signed parameter shape=%u path=%u pressure=%u error=%u node=%u cause=%u detail=%u\n", shape, path, pressure,
                                    (unsigned)error, failed, (unsigned)diagnostic.cause, diagnostic.error);
                            if (error == TIER1_EMIT_CERTIFICATE)
                            {
                                Tier1EmitResult raw = { 0 };
                                if (tier1_emit_cfg_function(&plan, &options, test->allowed, &test->output, &test->scratch, &raw, &failed) == TIER1_EMIT_OK)
                                {
                                    Tier1CertReport report = { 0 };
                                    Tier1CertEmittedStats stats = { 0 };
                                    const Tier1CertEmittedReason reason = tier1_certificate_emitted_check(&plan, &raw, &test->output, &test->scratch, &report, &stats);
                                    fprintf(stderr, "  signed entry checker reason=%u record=%u error=%u block=%u instruction=%u\n", (unsigned)reason, stats.record,
                                            (unsigned)report.error, report.block, report.instruction);
                                }
                            }
                        }
                        if (error == TIER1_EMIT_OK)
                        {
                            tier1_emit_test_expect(test, emitted.prepared.sizes[0] == 8 && emitted.prepared.sizes[second] == 8 && emitted.prepared.sizes[1] == 4,
                                                   "shared signed parameters preserve eight-byte storage without widening the single-use control");
                            uint32_t conversions = 0;
                            for (uint32_t r = 0; emitted.general && r < emitted.code.count; r += 1)
                            {
                                const X86Instruction record = emitted.code.records[r];
                                if (record.form == X86_FORM_MOVSXD_R64_RM32 && emitted.origin_values[r] < parameters &&
                                    (emitted.origin_values[r] == 0 || emitted.origin_values[r] == second))
                                {
                                    X86Instruction wrong_width = record;
                                    wrong_width.form = X86_FORM_MOV_R64_RM64;
                                    tier1_emit_test_expect(test, tier1_code_certificate_refuses(test, &plan, &emitted, r, wrong_width, UINT32_MAX),
                                                           "signed entry certificate refuses reading the caller's unspecified upper bytes");
                                    tier1_emit_test_expect(test, tier1_code_certificate_refuses(test, &plan, &emitted, r, (X86Instruction){ .form = X86_FORM_NOP }, UINT32_MAX),
                                                           "signed entry certificate refuses an omitted conversion");
                                    conversions += 1;
                                }
                            }
                            tier1_emit_test_expect(test, !emitted.general || conversions == 2, "both shared signed entry conversions are independently decoded and mutated");
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples) + 64; sample += 1)
                                {
                                    const uint32_t x = sample < BUSTER_ARRAY_LENGTH(samples) ? samples[sample] : (uint32_t)tier1_emit_test_random(&random);
                                    const uint32_t y = sample < BUSTER_ARRAY_LENGTH(samples) ? samples[BUSTER_ARRAY_LENGTH(samples) - sample - 1] : (uint32_t)tier1_emit_test_random(&random);
                                    const uint32_t z = (uint32_t)tier1_emit_test_random(&random);
                                    const uint64_t a = UINT64_C(0xa55ac0de00000000) | x;
                                    const uint64_t b = UINT64_C(0x39a17e5400000000) | y;
                                    const uint64_t c = UINT64_C(0xdeadbeef00000000) | z;
                                    const uint64_t expected = 2 * (uint64_t)(int64_t)(int32_t)x + x + 2 * (uint64_t)(int64_t)(int32_t)y + y + (uint64_t)(int64_t)(int32_t)z;
                                    const uint64_t actual = tier1_storage_parameter_call(executable, a, c, b);
                                    tier1_emit_test_expect(test, actual == expected, "shared entry conversion preserves signed and unsigned uses of poisoned register and stack arguments");
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

static void tier1_storage_midpoint_tests(Tier1EmitTest* test)
{
    const uint32_t samples[] = { 0, 1, 2, 3, 127, 65535, INT32_MAX, UINT32_C(0x80000000), UINT32_C(0x80000001), UINT32_MAX };
    uint64_t random = UINT64_C(0x5a3112190ee3ab19);
    for (uint32_t shape = 0; shape < 2; shape += 1)
    {
        const bool bounded = shape == 0;
        const uint64_t input_mark = test->input.length;
        const IrSizes sizes = { .instruction_count = 16, .block_count = 3, .constant_count = 3, .extra_count = 4, .parameter_count = 1 };
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create signed midpoint storage regression");
        if (created)
        {
            function.parameter_types[0] = IR_I32;
            function.result_type = IR_I64;
            function.constants[0].low = 1;
            function.constants[2].low = 2;
            function.block_offsets[1] = 5;
            function.block_offsets[2] = 9;
            function.block_offsets[3] = 16;
            function.extra[0] = 0;
            function.extra[1] = 3;
            function.extra[2] = 1;
            function.extra[3] = 6;
            tier1_select_test_node(&function, 0, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, bounded ? IR_ASHR : IR_COPY, IR_I32, 0, bounded ? 1 : BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 4, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 5, IR_PHI, IR_I32, 0, 2, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 6, IR_ASHR, IR_I32, 5, 1, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 7, IR_ICMP, IR_I1, 6, 2, IR_GT);
            tier1_select_test_node(&function, 8, IR_CBR, IR_VOID, 7, 1, 2);
            tier1_select_test_node(&function, 9, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 10, IR_MUL, IR_I32, 5, 9, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 11, IR_SEXT, IR_I64, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 12, IR_MUL, IR_I32, 5, 9, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 13, IR_SEXT, IR_I64, 12, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 14, IR_ADD, IR_I64, 11, 13, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 15, IR_RET, IR_VOID, 14, BUSTER_IR_NONE, BUSTER_IR_NONE);
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
                    tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "signed midpoint selects");
                    if (selected == TIER1_SELECT_OK)
                    {
                        tier1_emit_test_expect(test, plan.patterns[3].sign_result == bounded && plan.patterns[6].sign_result == bounded &&
                                               plan.patterns[11].rule == (bounded ? TIER1_RULE_IMMEDIATE : TIER1_RULE_SIGN_EXTEND) &&
                                               plan.patterns[13].rule == (bounded ? TIER1_RULE_IMMEDIATE : TIER1_RULE_SIGN_EXTEND),
                                               "a bounded signed midpoint shares its definition's extension while unbounded multiplication retains wrapping");
                        const Tier1EmitOptions options = {
                            .certify = true,
                            .register_mask = pressure == 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                            .vector = path != 0,
                            .rematerialize = pressure != 0,
                        };
                        Tier1EmitResult emitted;
                        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                        tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "signed midpoint allocation and code certify");
                        if (error == TIER1_EMIT_OK)
                        {
                            tier1_emit_test_expect(test, emitted.prepared.sizes[5] == (bounded ? 8 : 4), "only the proved signed midpoint widens its spill storage");
                            void* executable = MAP_FAILED;
                            uint64_t bytes = 0;
                            if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                            {
                                for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples) + 64; sample += 1)
                                {
                                    const uint32_t input = sample < BUSTER_ARRAY_LENGTH(samples) ? samples[sample] : (uint32_t)tier1_emit_test_random(&random);
                                    uint32_t phi = bounded ? (input >> 1) | (input & UINT32_C(0x80000000)) : input;
                                    bool again = true;
                                    for (uint32_t iteration = 0; again && iteration < 32; iteration += 1)
                                    {
                                        const uint32_t next = (phi >> 1) | (phi & UINT32_C(0x80000000));
                                        again = (int32_t)next > 0;
                                        if (again)
                                        {
                                            phi = next;
                                        }
                                    }
                                    tier1_emit_test_expect(test, !again, "midpoint oracle terminates within its independent bound");
                                    if (!again)
                                    {
                                        const uint64_t product = (uint64_t)(int64_t)(int32_t)(phi * 2u);
                                        const uint64_t actual = tier1_emit_test_call(executable, UINT64_C(0xa55ac0de00000000) | input, 0);
                                        tier1_emit_test_expect(test, actual == product + product, "midpoint multiplication preserves negative endpoints, raw wrapping controls and poisoned upper halves");
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
