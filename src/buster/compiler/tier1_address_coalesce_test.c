// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1217: the allocated destination decides whether a simple address sum can use ADD. Exercise real scalar/vector
// plans against the independent IR interpreter, including wrapping I32 results and address scales/displacements that
// still require LEA. The frame-base check protects the CFG consumer's later displacement patch.

static void tier1_select_test_coalesced_addresses(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    const uint8_t destinations[] = { X86_REGISTER_RAX, X86_REGISTER_RDI, X86_REGISTER_RSI };
    for (uint32_t shape = 0; shape < 5; shape += 1)
    {
        const bool address = shape >= 2;
        const uint8_t type = address ? IR_PTR : shape == 0 ? IR_I32 : IR_I64;
        const IrSizes sizes =
        {
            .instruction_count = 4,
            .block_count = 1,
            .address_count = address ? 1 : 0,
            .parameter_count = 2,
        };
        IrFunction function;
        const IrError created = ir_create(&input, &sizes, &function);
        tier1_select_test_expect(test, created == IR_OK, "create coalesced address fixture");
        if (created == IR_OK)
        {
            function.result_type = type;
            function.block_offsets[1] = 4;
            function.parameter_types[0] = type;
            function.parameter_types[1] = address ? IR_I64 : type;
            if (address)
            {
                function.addresses[0] = (IrAddress){ .scale = shape == 3 ? 4 : 1, .offset = shape == 4 ? -17 : 0 };
            }
            tier1_select_test_node(&function, 0, IR_PARAM, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_PARAM, function.parameter_types[1], 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, address ? IR_ADDRESS : IR_ADD, type, 0, 1, address ? 0 : BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(destinations); d += 1)
            {
                const uint8_t registers[] = { X86_REGISTER_RDI, X86_REGISTER_RSI, destinations[d], X86_REGISTER_RAX };
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    Tier1SelectPlan plan;
                    uint32_t node = BUSTER_IR_NONE;
                    const Tier1SelectError selected = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
                    tier1_select_test_expect(test, selected == TIER1_SELECT_OK, "both paths select the address sum");
                    if (selected == TIER1_SELECT_OK)
                    {
                        Tier1SelectCode code;
                        const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                        tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "allocated address sum emits");
                        if (emitted == TIER1_SELECT_OK)
                        {
                            const bool add = d != 0 && shape < 3;
                            const uint16_t form = add ? (shape == 0 ? X86_FORM_ADD_R32_RM32 : X86_FORM_ADD_R64_RM64) :
                                                       (shape == 0 ? X86_FORM_LEA_R32_M : X86_FORM_LEA_R64_M);
                            const uint32_t first = code.node_first[2];
                            tier1_select_test_expect(test, code.node_first[3] == first + 1 && code.records[first].form == form,
                                                     "only a coalesced unscaled sum without displacement becomes add");
                            if (add)
                            {
                                X86Instruction lea = tier1_select_memory_holes(&plan.patterns[2], registers, 0);
                                lea.reg = destinations[d];
                                lea.form = shape == 0 ? X86_FORM_LEA_R32_M : X86_FORM_LEA_R64_M;
                                uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
                                const int64_t lea_bytes = x86_encode(&lea, 1, bytes, sizeof(bytes), 0, 0);
                                const int64_t add_bytes = x86_encode(code.records + first, 1, bytes, sizeof(bytes), 0, 0);
                                tier1_select_test_expect(test, add_bytes > 0 && lea_bytes == add_bytes + 1, "coalesced add removes the address operand's SIB byte");
                            }
                        }
                    }
                    arena_pop_to(&output, 0);
                }
                tier1_select_test_plan_execute(test, &function, registers, &output, &scratch);
            }
        }
        arena_pop_to(&input, 0);
    }
    // An object's offset is not known at expansion. Even with destination == index, the last record must remain
    // an address operand so tier1_emit_cfg_kernel can patch the frame base and the object's nonzero displacement.
    Tier1SelectHoles frame =
    {
        .rule = TIER1_RULE_ADDRESS,
        .memory = { .rm = X86_REGISTER_RSP, .index = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) },
        .width = 8,
        .destination = X86_REGISTER_RDI,
    };
    const Tier1SelectRecords records = tier1_select_test_expand(test, &frame);
    tier1_select_test_expect(test, records.count == 1 && records.records[0].form == X86_FORM_LEA_R64_M && records.records[0].mode == BUSTER_X86_MEMORY_BASE_INDEX(0),
                             "an unpatched frame object keeps its address operand");
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}
