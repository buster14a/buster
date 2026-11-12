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

// #1217: a base plus a displacement, and a signed power-of-two division's bias, whose result allocation put in the base's
// (the dividend's) register: add (or the form the request's batch chooses: inc, sub) rather than lea, as clang's code
// has them. Both widths and every displacement class (1, imm8 and imm32, both signs) run natively against C, beside a
// separate destination and a zero displacement, which keep lea.
static void tier1_select_test_in_place_sums(Tier1SelectTest* test)
{
    static const int32_t displacements[] = { 0, 1, -1, 4, 24, 127, 128, -128, -129, INT32_MAX, INT32_MIN };
    static const uint64_t samples[] = { 0, 1, UINT64_MAX, UINT64_C(0x7fffffff), UINT64_C(0x80000000), UINT64_C(0xfffffffe), UINT64_C(0x123456789abcdef0), UINT64_C(0x8000000000000000) };
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
        for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(displacements); d += 1)
        {
            for (uint32_t in_place = 0; in_place < 2; in_place += 1)
            {
                const Tier1SelectHoles holes =
                {
                    .rule = TIER1_RULE_LEA_ADD,
                    .width = (uint8_t)width,
                    .destination = in_place != 0 ? X86_REGISTER_RDI : X86_REGISTER_RAX,
                    .left = X86_REGISTER_RDI,
                    .memory = { .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = displacements[d] },
                };
                const Tier1SelectRecords selected = tier1_select_test_expand(test, &holes);
                const bool lea = selected.count == 1 && selected.records[0].form == (width == 8 ? X86_FORM_LEA_R64_M : X86_FORM_LEA_R32_M);
                tier1_select_test_expect(test, selected.count == 1 && lea == (in_place == 0 || displacements[d] == 0),
                                         "a base plus a displacement in the base's own register is one add, any other a lea");
                X86Instruction records[4];
                memcpy(records, selected.records, selected.count * sizeof(*records));
                uint32_t count = selected.count;
                records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes.destination);
                records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                const bool loaded = tier1_select_test_load_code(test, records, count, selected.count, false);
                for (uint32_t s = 0; loaded && s < BUSTER_ARRAY_LENGTH(samples); s += 1)
                {
                    const uint64_t expected = (samples[s] + (uint64_t)(int64_t)displacements[d]) & mask;
                    tier1_select_test_expect(test, tier1_select_test_call(test, samples[s], 0, 0) == expected, "an in-place sum agrees with C's");
                }
            }
        }
        for (uint32_t shift = 1; shift <= width * 8 - 2; shift += width * 8 / 8 - 1)
        {
            for (uint32_t form = 0; form < 4; form += 1)
            {
                const bool remainder = (form & 1) != 0;
                const bool in_place = (form & 2) != 0;
                // A remainder's mask must be an and's immediate, as the selector requires (a 64-bit one up to 2^31 - 1).
                const bool selectable = !remainder || tier1_select_immediate_fits(width, (UINT64_C(1) << shift) - 1);
                const Tier1SelectHoles holes =
                {
                    .rule = TIER1_RULE_DIVIDE_POWER,
                    .width = (uint8_t)width,
                    .destination = in_place ? X86_REGISTER_RDI : X86_REGISTER_RAX,
                    .left = X86_REGISTER_RDI,
                    .scale = (uint8_t)shift,
                    .immediate = 1 | (remainder ? 2 : 0),
                };
                const Tier1SelectRecords selected = selectable ? tier1_select_test_expand(test, &holes) : (Tier1SelectRecords){ 0 };
                uint32_t leas = 0;
                for (uint32_t r = 0; r < selected.count; r += 1)
                {
                    leas += selected.records[r].form == X86_FORM_LEA_R64_M || selected.records[r].form == X86_FORM_LEA_R32_M;
                }
                tier1_select_test_expect(test, !selectable || leas == (in_place ? 0u : 1u), "a division's bias is added in place, or by a lea into another register");
                X86Instruction records[BUSTER_TIER1_SELECT_RECORDS_PER_NODE + 2];
                memcpy(records, selected.records, selected.count * sizeof(*records));
                uint32_t count = selected.count;
                records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes.destination);
                records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                const bool loaded = selectable && tier1_select_test_load_code(test, records, count, selected.count, false);
                for (uint32_t s = 0; loaded && s < BUSTER_ARRAY_LENGTH(samples); s += 1)
                {
                    const int64_t dividend = width == 4 ? (int64_t)(int32_t)(uint32_t)samples[s] : (int64_t)samples[s];
                    const int64_t divisor = INT64_C(1) << shift;
                    const int64_t result = remainder ? dividend % divisor : dividend / divisor;
                    const uint64_t actual = tier1_select_test_call(test, samples[s], 0, 0) & mask;
                    tier1_select_test_expect(test, actual == ((uint64_t)result & mask), "a signed power-of-two division agrees with C's, in place or not");
                    if (actual != ((uint64_t)result & mask) && test->failures < 40)
                    {
                        fprintf(stderr, "  division width=%u shift=%u remainder=%u in_place=%u a=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", width, shift, remainder, in_place, samples[s], actual, (uint64_t)result & mask);
                    }
                }
            }
        }
    }
}
