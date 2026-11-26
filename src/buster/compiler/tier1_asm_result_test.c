// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An asm body's scalar results (#2144): born together in fixed registers, including when an intervening divide has
// overwritten those registers before the secondary projections' IR positions. Both classes are forced to spill.

static bool tier1_asm_result_function(Tier1EmitTest* test, bool floating, IrFunction* function, X86Instruction* records, IrAsmBody* body)
{
    const IrSizes sizes = {
        .instruction_count = 22,
        .block_count = 1,
        .extra_count = 2,
        .constant_count = 1,
        .call_count = 1,
        .call_type_count = 2,
        .parameter_count = 2,
    };
    const bool created = ir_create(&test->input, &sizes, function) == IR_OK;
    if (created)
    {
        const bool wide = test->width == 8;
        const IrType integer = wide ? IR_I64 : IR_I32;
        const IrType vector = wide ? IR_F64 : IR_F32;
        const uint32_t registers[] = { X86_REGISTER_RAX, X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RSI, 16, 17 };
        const uint32_t primary = floating ? 4 : 0;
        uint32_t results[6];
        uint32_t next = 6;
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(registers); k += 1)
        {
            results[k] = k == primary ? 2 : next++;
            if (k != primary)
            {
                tier1_emit_test_node(function, results[k], IR_ASM_RESULT, k < 4 ? integer : vector, 2, registers[k]);
            }
        }
        const uint16_t operations[] = {
            wide ? X86_FORM_ADD_R64_RM64 : X86_FORM_ADD_R32_RM32,
            wide ? X86_FORM_SUB_R64_RM64 : X86_FORM_SUB_R32_RM32,
            wide ? X86_FORM_XOR_R64_RM64 : X86_FORM_XOR_R32_RM32,
            wide ? X86_FORM_IMUL_R64_RM64 : X86_FORM_IMUL_R32_RM32,
        };
        for (uint32_t k = 0; k < 4; k += 1)
        {
            records[2 * k] = tier1_select_register_record(wide ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32, (uint8_t)registers[k], X86_REGISTER_R8);
            records[2 * k + 1] = tier1_select_register_record(operations[k], (uint8_t)registers[k], X86_REGISTER_R9);
        }
        records[8] = tier1_select_register_record(wide ? X86_FORM_MOVQ_X_RM64 : X86_FORM_MOVD_X_RM32, 0, X86_REGISTER_RAX);
        records[9] = tier1_select_register_record(wide ? X86_FORM_MOVQ_X_RM64 : X86_FORM_MOVD_X_RM32, 1, X86_REGISTER_RCX);
        static const AbiLocation locations[] = {
            { .registers = { X86_REGISTER_R8, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
            { .registers = { X86_REGISTER_R9, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        };
        *body = (IrAsmBody){
            .records = records,
            .count = 10,
            .touched = 0x30347u,
            .locations = locations,
            .passed = 2,
            .result = (uint8_t)(registers[primary] + 1),
            .result_registers = 0x30047u,
            .result_wide = wide ? 0x30047u : 0,
        };
        function->asm_bodies = body;
        function->asm_body_count = 1;
        function->result_type = (uint8_t)integer;
        function->parameter_types[0] = (uint8_t)integer;
        function->parameter_types[1] = (uint8_t)integer;
        function->call_types[0] = (uint8_t)integer;
        function->call_types[1] = (uint8_t)integer;
        function->extra[0] = 0;
        function->extra[1] = 1;
        function->constants[0].low = 1;
        function->calls[0] = (IrCall){ .argument_count = 2, .parameter_count = 2, .result_type = (uint8_t)(floating ? vector : integer), .body = 1 };
        function->block_offsets[1] = 22;
        tier1_emit_test_node(function, 0, IR_PARAM, integer, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 1, IR_PARAM, integer, 1, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 2, IR_CALL, floating ? vector : integer, 0, 0);
        tier1_emit_test_node(function, 3, IR_CONST, integer, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 4, IR_OR, integer, 1, 3);
        tier1_emit_test_node(function, 5, IR_UDIV, integer, 0, 4);
        tier1_emit_test_node(function, 11, IR_BITCAST, integer, results[4], BUSTER_IR_NONE);
        tier1_emit_test_node(function, 12, IR_BITCAST, integer, results[5], BUSTER_IR_NONE);
        tier1_emit_test_node(function, 13, IR_ADD, integer, results[0], results[1]);
        tier1_emit_test_node(function, 14, IR_XOR, integer, results[2], results[3]);
        tier1_emit_test_node(function, 15, IR_ADD, integer, 11, 12);
        tier1_emit_test_node(function, 16, IR_ADD, integer, 13, 14);
        tier1_emit_test_node(function, 17, IR_XOR, integer, 16, 15);
        tier1_emit_test_node(function, 18, IR_ADD, integer, 17, 0);
        tier1_emit_test_node(function, 19, IR_XOR, integer, 18, 1);
        tier1_emit_test_node(function, 20, IR_ADD, integer, 19, 5);
        tier1_emit_test_node(function, 21, IR_RET, IR_VOID, 20, BUSTER_IR_NONE);
    }
    return created;
}

static void tier1_asm_result_tests(Tier1EmitTest* test)
{
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        test->width = width;
        for (uint32_t floating = 0; floating < 2; floating += 1)
        {
            IrFunction function;
            X86Instruction records[10];
            IrAsmBody body;
            const bool created = tier1_asm_result_function(test, floating != 0, &function, records, &body);
            const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
            tier1_emit_test_expect(test, valid, "mixed asm result snapshots are valid IR");
            for (uint32_t pressure = 0; valid && pressure < 2; pressure += 1)
            {
                Tier1SelectPlan plan;
                uint32_t node = BUSTER_IR_NONE;
                const bool planned = tier1_select_plan(&function, 0, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
                tier1_emit_test_expect(test, planned, "mixed asm result snapshots select");
                Tier1EmitDiagnostic diagnostic = { 0 };
                const Tier1EmitOptions options = {
                    .register_mask = pressure != 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RSI) : tier1_liveness_register_mask(),
                    .vector_register_mask = pressure != 0 ? 1u : 0,
                    .rematerialize = pressure != 0,
                    .certify = true,
                    .diagnostic = &diagnostic,
                };
                Tier1EmitResult emitted;
                const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
                const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
                tier1_emit_test_expect(test, certified, "mixed asm results emit with allocation and code certificates");
                if (!certified && test->failures < 20)
                {
                    fprintf(stderr, "  asm result error=%u node=%u cause=%u nested=%u floating=%u pressure=%u\n", error, node, diagnostic.cause, diagnostic.error, floating, pressure);
                }
                uint32_t spilled = 0;
                const uint32_t position = certified ? emitted.cfg_input.definitions[emitted.node_values[2]] : 0;
                for (uint32_t n = 0; certified && n < function.instruction_count; n += 1)
                {
                    if (n == 2 || function.op[n] == IR_ASM_RESULT)
                    {
                        const uint32_t value = emitted.node_values[n];
                        uint32_t location = BUSTER_TIER1_CFG_NONE;
                        uint8_t kind = 0;
                        tier1_emit_test_expect(test, emitted.cfg_input.definitions[value] == position && tier1_cfg_location(&emitted.cfg, value, position, &location, &kind),
                                               "every asm snapshot is defined at the body's boundary");
                        spilled += kind == BUSTER_TIER1_ALLOCATE_SPILL;
                    }
                }
                tier1_emit_test_expect(test, !certified || pressure == 0 || spilled != 0, "pressure captures asm results directly into spill slots");
                void* executable = MAP_FAILED;
                uint64_t bytes = 0;
                if (certified && tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
                {
                    uint64_t random = UINT64_C(0xab83d271ab4f2099);
                    const uint64_t mask = width == 8 ? UINT64_MAX : UINT32_MAX;
                    for (uint32_t sample = 0; sample < 64; sample += 1)
                    {
                        const uint64_t a = tier1_emit_test_random(&random) & mask;
                        const uint64_t b = tier1_emit_test_random(&random) & mask;
                        const uint64_t sum = (a + b + (a - b)) & mask;
                        const uint64_t combined = (sum + ((a ^ b) ^ (a * b))) & mask;
                        const uint64_t expected = ((((combined ^ sum) + a) ^ b) + a / (b | 1)) & mask;
                        tier1_emit_test_expect(test, (tier1_emit_test_call(executable, a, b) & mask) == expected,
                                               "fixed-register asm outputs survive intervening instructions and spills");
                    }
                }
                if (executable != MAP_FAILED)
                {
                    munmap(executable, bytes);
                }
                uint32_t last = UINT32_MAX;
                for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
                {
                    last = emitted.origins[r] == tier1_origin(TIER1_ORIGIN_NODE, 2) ? r : last;
                }
                uint32_t mutated = 0;
                for (uint32_t r = last + 1; certified && last != UINT32_MAX && r < emitted.code.count && emitted.origins[r] == tier1_origin(TIER1_ORIGIN_MOVE, position); r += 1)
                {
                    const X86Instruction record = emitted.code.records[r];
                    const bool stored = record.mode == BUSTER_X86_MEMORY_BASE &&
                                        (record.form == X86_FORM_MOV_RM32_R32 || record.form == X86_FORM_MOV_RM64_R64 || record.form == X86_FORM_MOVSS_XM32_X || record.form == X86_FORM_MOVSD_XM64_X);
                    if (stored)
                    {
                        X86Instruction wrong = record;
                        wrong.reg ^= 1;
                        tier1_emit_test_expect(test, tier1_code_certificate_refuses(test, &plan, &emitted, r, wrong, UINT32_MAX),
                                               "the independent certificate rejects an asm output captured from the wrong register");
                        mutated += 1;
                    }
                }
                tier1_emit_test_expect(test, !certified || pressure == 0 || mutated != 0, "pressure checks a corrupted asm output capture");
                arena_pop_to(&test->output, 0);
            }
            arena_pop_to(&test->input, 0);
        }
    }
}
