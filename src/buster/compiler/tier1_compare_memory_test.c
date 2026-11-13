// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1218: original compare/load regressions, included by tier1_select_test.c. The processor checks all predicates at
// guarded access boundaries and with the result overlapping either address register or the other operand. Real IR
// checks the fold's effective uses, exact-width promotion proof, branch/truth consumers and memory-order refusals.

static bool tier1_select_test_memory_predicate(uint32_t predicate, uint32_t width, uint64_t a, uint64_t b)
{
    const uint64_t mask = UINT64_MAX >> (64 - width * 8);
    const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
    a &= mask;
    b &= mask;
    if (predicate >= IR_LT && predicate <= IR_GE)
    {
        a = (a ^ sign) - sign;
        b = (b ^ sign) - sign;
    }
    return tier1_select_test_compare(predicate, 8, a, b);
}

static void tier1_select_test_compare_memory_holes(Tier1SelectTest* test)
{
    uint8_t* mapping = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + 4096, 4096, PROT_NONE) == 0;
    tier1_select_test_expect(test, guarded, "guard the exact end of a compare's memory operand");
    const uint8_t destinations[] =
    {
        X86_REGISTER_RAX,
        X86_REGISTER_RDI,
        X86_REGISTER_RSI,
        X86_REGISTER_RDX,
    };
    for (uint32_t width = 1; guarded && width <= 8; width *= 2)
    {
        const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
        const uint64_t mask = UINT64_MAX >> (64 - width * 8);
        const uint64_t samples[] =
        {
            0,
            1,
            sign - 1,
            sign,
            mask - 1,
            mask,
            127 & mask,
            128 & mask,
            (uint64_t)-128 & mask,
            (uint64_t)-129 & mask,
        };
        // The immediates (#2600): the constants each side of a signed byte's and a signed word's bounds, and the
        // doubleword's, that the width holds as a signed value, at its bits. A word's, a doubleword's or a quadword's
        // within a signed byte take the sign-extended imm8 form.
        static const int64_t candidates[] = { -1, 0, 127, 128, -128, -129, 0x7fff, -0x8000, 0x7fffffff, -INT64_C(0x80000000) };
        uint64_t constants[BUSTER_ARRAY_LENGTH(candidates)];
        uint32_t constant_count = 0;
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(candidates); k += 1)
        {
            const int64_t limit = width == 8 ? INT32_MAX : (int64_t)(sign - 1);
            if (candidates[k] <= limit && candidates[k] >= -limit - 1)
            {
                constants[constant_count++] = (uint64_t)candidates[k] & mask;
            }
        }
        uint8_t* bytes = mapping + 4096 - width;
        for (uint32_t predicate = 0; predicate < IR_PREDICATE_COUNT; predicate += 1)
        {
            // 0 for a register comparison, else the constant immediate - 1's.
            for (uint32_t immediate = width < 4 ? 1 : 0; immediate <= constant_count; immediate += 1)
            {
                const uint64_t constant = immediate != 0 ? constants[immediate - 1] : mask;
                const int64_t signed_constant = (int64_t)(constant << (64 - width * 8)) >> (64 - width * 8);
                for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(destinations); d += 1)
                {
                    for (uint32_t branch = 0; branch < 2; branch += 1)
                    {
                        const Tier1SelectHoles holes =
                        {
                            .rule = branch != 0 ? TIER1_RULE_BRANCH_COMPARE : immediate != 0 ? TIER1_RULE_COMPARE_IMMEDIATE : TIER1_RULE_COMPARE,
                            .immediate = constant,
                            .memory =
                            {
                                .displacement = -3,
                                .rm = X86_REGISTER_RDI,
                                .index = X86_REGISTER_RSI,
                                .mode = BUSTER_X86_MEMORY_BASE_INDEX(0),
                            },
                            .width = (uint8_t)width,
                            .destination = destinations[d],
                            .left = X86_REGISTER_RDX,
                            .condition = (uint8_t)predicate,
                            .compare_immediate = immediate != 0,
                            .compare_memory = true,
                        };
                        const Tier1SelectRecords selected = tier1_select_test_expand(test, &holes);
                        // The comparison's form: an immediate within a signed byte takes imm8 at every width.
                        uint16_t compare_form = X86_FORM_NONE;
                        for (uint32_t r = 0; r < selected.count; r += 1)
                        {
                            const uint16_t form = selected.records[r].form & BUSTER_X86_FORM_NUMBER;
                            compare_form = x86_effects(form)->operation == X86_OPERATION_CMP ? form : compare_form;
                        }
                        const bool short_immediate = signed_constant >= INT8_MIN && signed_constant <= INT8_MAX;
                        const uint16_t wanted_form = immediate == 0 ? (width == 8 ? X86_FORM_CMP_R64_RM64 : X86_FORM_CMP_R32_RM32) :
                                                     width == 1 ? X86_FORM_CMP_RM8_IMM8 :
                                                     short_immediate ? (width == 2 ? X86_FORM_CMP_RM16_IMM8 : width == 8 ? X86_FORM_CMP_RM64_IMM8 : X86_FORM_CMP_RM32_IMM8) :
                                                     width == 2 ? X86_FORM_CMP_RM16_IMM16 : width == 8 ? X86_FORM_CMP_RM64_IMM32 : X86_FORM_CMP_RM32_IMM32;
                        tier1_select_test_expect(test, compare_form == wanted_form, "a memory comparison's immediate takes the shortest form its width sign-extends");
                        X86Instruction records[8];
                        memcpy(records, selected.records, selected.count * sizeof(*records));
                        uint32_t count = selected.count;
                        bool ready = true;
                        if (branch != 0)
                        {
                            tier1_select_test_expect(test, selected.count == 3, "memory compare keeps its two branch records");
                            records[count++] = (X86Instruction){ .immediate = 11, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 };
                            records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                            records[count++] = (X86Instruction){ .immediate = 22, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 };
                            records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                            const uint32_t branches[] = { 1, 2 };
                            const uint32_t targets[] = { 3, 5 };
                            uint32_t offsets[8];
                            ready = selected.count == 3 && size_branches(records, count, branches, targets, 2, offsets).error == BRANCH_SIZING_OK;
                            tier1_select_test_expect(test, ready, "size a folded memory comparison's branches");
                        }
                        else
                        {
                            records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes.destination);
                            records[count++] = (X86Instruction){ .form = X86_FORM_RET };
                        }
                        if (ready && tier1_select_test_load_code(test, records, count, selected.count, false))
                        {
                            for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(samples); a += 1)
                            {
                                for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(samples); b += 1)
                                {
                                    memcpy(bytes, &samples[a], width);
                                    const bool compared = immediate != 0 ? tier1_select_test_memory_predicate(predicate, width, samples[a], constant) :
                                                                          tier1_select_test_memory_predicate(predicate, width, samples[b], samples[a]);
                                    const uint64_t expected = branch != 0 ? (compared ? 11 : 22) : compared;
                                    const uint64_t actual = tier1_select_test_call(test, (uintptr_t)bytes, 3, samples[b]);
                                    uint64_t after = 0;
                                    memcpy(&after, bytes, width);
                                    tier1_select_test_expect(test, actual == expected && after == samples[a], "exact-width memory CMP preserves predicates, flags and aliased inputs");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 8192);
    }
}

// Load at node 4, an optional integer promotion at 5, and an immediate/register comparison at 6. Consumer 0 returns
// its I1, consumer 1 extends it to I32, and consumer 2 branches to return 11/22. No other instruction accesses memory.
static void tier1_select_test_compare_memory_plan(Tier1SelectTest* test, uint32_t width, uint32_t result_width, uint8_t extension, uint64_t constant,
                                                uint32_t predicate, uint32_t consumer, bool immediate, bool reversed, Arena* input, Arena* output, Arena* scratch, uint8_t* bytes)
{
    const uint8_t type = width == 1 ? IR_I8 : width == 2 ? IR_I16 : width == 4 ? IR_I32 : IR_I64;
    const uint8_t compare_type = result_width == 4 ? IR_I32 : IR_I64;
    const uint32_t count = consumer == 2 ? 12 : consumer == 1 ? 9 : 8;
    const IrSizes sizes =
    {
        .instruction_count = count,
        .block_count = consumer == 2 ? 3 : 1,
        .constant_count = 3,
        .address_count = 1,
        .parameter_count = 3,
    };
    IrFunction function;
    const bool created = ir_create(input, &sizes, &function) == IR_OK;
    tier1_select_test_expect(test, created, "create a memory-comparison IR fixture");
    if (created)
    {
        function.result_type = consumer == 0 ? IR_I1 : IR_I32;
        function.block_offsets[1] = consumer == 2 ? 8 : count;
        function.parameter_types[0] = IR_PTR;
        function.parameter_types[1] = IR_I64;
        function.parameter_types[2] = compare_type;
        function.addresses[0] = (IrAddress){ .offset = -3, .scale = 1 };
        function.constants[0].low = constant;
        function.constants[1].low = 11;
        function.constants[2].low = 22;
        tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 2, IR_PARAM, compare_type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        // Keep the address separately so both base and index must remain effective uses of the final comparison.
        tier1_select_test_node(&function, 3, IR_ADDRESS, IR_PTR, 0, 1, 0);
        tier1_select_test_node(&function, 4, IR_LOAD, type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 5, immediate ? IR_CONST : IR_COPY, compare_type, immediate ? 0 : 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t loaded = 4;
        const uint32_t comparison = 6;
        if (extension != UINT8_MAX)
        {
            // Immediate fixtures do not read parameter 2, so its row holds the comparison's constant instead.
            if (immediate)
            {
                tier1_select_test_node(&function, 2, IR_CONST, compare_type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            tier1_select_test_node(&function, 5, extension, compare_type, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            loaded = 5;
        }
        const uint32_t other = extension != UINT8_MAX ? 2 : 5;
        tier1_select_test_node(&function, comparison, IR_ICMP, IR_I1, reversed ? other : loaded, reversed ? loaded : other, predicate);
        if (consumer == 2)
        {
            function.block_offsets[2] = 10;
            function.block_offsets[3] = 12;
            tier1_select_test_node(&function, 7, IR_CBR, IR_VOID, comparison, 1, 2);
            tier1_select_test_node(&function, 8, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 10, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 11, IR_RET, IR_VOID, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else if (consumer == 1)
        {
            tier1_select_test_node(&function, 7, IR_ZEXT, IR_I32, comparison, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 8, IR_RET, IR_VOID, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            tier1_select_test_node(&function, 7, IR_RET, IR_VOID, comparison, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t root = consumer == 0 ? comparison : 7;
        const uint64_t mask = UINT64_MAX >> (64 - width * 8);
        const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
        const uint64_t result_mask = result_width == 4 ? UINT32_MAX : UINT64_MAX;
        const uint64_t narrow = constant & mask;
        const uint64_t widened = extension == IR_SEXT ? (narrow ^ sign) - sign : narrow;
        const bool fits = extension == UINT8_MAX || (immediate && !reversed && (widened & result_mask) == (constant & result_mask) && tier1_select_immediate_fits(result_width, constant));
        const uint8_t registers[12] =
        {
            X86_REGISTER_RDI,
            X86_REGISTER_RSI,
            X86_REGISTER_RDX,
            X86_REGISTER_R8,
            X86_REGISTER_R9,
            X86_REGISTER_R10,
            X86_REGISTER_RAX,
            X86_REGISTER_RAX,
            X86_REGISTER_RAX,
            X86_REGISTER_RAX,
            X86_REGISTER_RAX,
            X86_REGISTER_RAX,
        };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            Tier1SelectPlan plan;
            uint32_t failed = 0;
            const Tier1SelectError selected = tier1_select_plan(&function, 0, output, scratch, path == 0, &plan, &failed);
            tier1_select_test_expect(test, selected == TIER1_SELECT_OK, "both selectors accept memory comparison fixtures");
            if (selected == TIER1_SELECT_OK)
            {
                const Tier1SelectPattern* pattern = &plan.patterns[root];
                char label[160];
                snprintf(label, sizeof(label), "memory fold width=%u extension=%u constant=%016" PRIx64 " predicate=%u consumer=%u path=%u", width, extension, constant, predicate, consumer, path);
                tier1_select_test_expect(test, pattern->compare_memory == fits, label);
                if (fits)
                {
                    const uint32_t begin = plan.effective_use_offsets[root];
                    const uint32_t end = plan.effective_use_offsets[root + 1];
                    bool base = false;
                    bool index = false;
                    for (uint32_t use = begin; use < end; use += 1)
                    {
                        base |= plan.effective_use_values[use] == 0;
                        index |= plan.effective_use_values[use] == 1;
                    }
                    tier1_select_test_expect(test, pattern->width == width && plan.covered[4] != 0 && base && index,
                                             "folded comparisons retain the exact memory width and both address operands");
                }
                if (tier1_select_test_walk_load(test, &plan, registers, output))
                {
                    const uint64_t samples[] = { 0, 1, sign - 1, sign, mask };
                    for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples); sample += 1)
                    {
                        const uint64_t bits = samples[sample];
                        memcpy(bytes, &bits, width);
                        const uint64_t value = extension == IR_SEXT ? (bits ^ sign) - sign : bits;
                        const uint64_t other_value = immediate ? constant : constant + sample;
                        const bool truth = tier1_select_test_compare(predicate, result_width, reversed ? other_value : value, reversed ? value : other_value);
                        const uint64_t expected = consumer == 2 ? (truth ? 11 : 22) : truth;
                        const uint64_t actual = tier1_select_test_call(test, (uintptr_t)bytes, 3, other_value);
                        tier1_select_test_expect(test, actual == expected, "selected memory comparisons execute like the original widened operands");
                    }
                }
            }
            arena_pop_to(output, 0);
        }
    }
    arena_pop_to(input, 0);
}

static void tier1_select_test_compare_memory_plans(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    uint8_t* mapping = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + 4096, 4096, PROT_NONE) == 0;
    tier1_select_test_expect(test, guarded, "guard memory-comparison plan fixtures");
    for (uint32_t width = 1; guarded && width <= 8; width *= 2)
    {
        const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
        const uint64_t mask = UINT64_MAX >> (64 - width * 8);
        for (uint32_t result_width = width < 4 ? 4 : width; result_width <= 8; result_width *= 2)
        {
            for (uint32_t extension = 0; extension < (result_width == width ? 1u : 2u); extension += 1)
            {
                const uint8_t op = result_width == width ? UINT8_MAX : extension == 0 ? IR_ZEXT : IR_SEXT;
                const uint64_t result_mask = result_width == 4 ? UINT32_MAX : UINT64_MAX;
                const uint64_t constants[] =
                {
                    0,
                    sign - 1,
                    sign,
                    mask,
                    (mask + 1) & result_mask,
                    ((sign ^ sign) - sign) & result_mask,
                    result_mask,
                };
                for (uint32_t predicate = 0; predicate < IR_PREDICATE_COUNT; predicate += 1)
                {
                    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(constants); c += 1)
                    {
                        for (uint32_t consumer = 0; consumer < 3; consumer += 1)
                        {
                            tier1_select_test_compare_memory_plan(test, width, result_width, op, constants[c], predicate, consumer, true, false,
                                                                  &input, &output, &scratch, mapping + 4096 - width);
                        }
                    }
                    for (uint32_t reverse = 0; reverse < 2; reverse += 1)
                    {
                        tier1_select_test_compare_memory_plan(test, width, result_width, op, sign, predicate, 2, false, reverse != 0,
                                                              &input, &output, &scratch, mapping + 4096 - width);
                    }
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 8192);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

// Reuse the access-order fixtures with comparisons as their consumers. In the two-load case the later load may fold,
// but the earlier one must keep its position. Execute the non-call cases against the original IR, including stores
// and atomic operations, so a comparison must observe the right value and leave exactly the same memory behind.
static void tier1_select_test_compare_memory_barriers(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(tier1_select_test_barriers); s += 1)
    {
        const Tier1SelectTestBarrier* shape = &tier1_select_test_barriers[s];
        const uint32_t count = 3 + shape->count;
        bool calls = false;
        for (uint32_t i = 0; i < shape->count; i += 1)
        {
            calls |= shape->op[i] == IR_CALL;
        }
        const IrSizes sizes =
        {
            .instruction_count = count,
            .block_count = 1,
            .constant_count = 1,
            .address_count = 3,
            .call_count = calls,
            .parameter_count = 3,
        };
        IrFunction function;
        const bool created = ir_create(&input, &sizes, &function) == IR_OK;
        tier1_select_test_expect(test, created, "create comparison barrier fixture");
        if (created)
        {
            function.result_type = IR_I1;
            function.block_offsets[1] = count;
            function.parameter_types[0] = IR_PTR;
            function.parameter_types[1] = IR_I64;
            function.parameter_types[2] = IR_I64;
            function.constants[0].low = 7;
            if (calls)
            {
                function.symbol_count = 2;
                function.calls[0].result_type = IR_VOID;
            }
            for (uint32_t address = 0; address < 3; address += 1)
            {
                function.addresses[address].scale = 1;
                function.addresses[address].offset = (int64_t)address * 8;
            }
            tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            static const uint8_t pool[] =
            {
                X86_REGISTER_R8,
                X86_REGISTER_RCX,
                X86_REGISTER_R9,
                X86_REGISTER_R10,
            };
            uint8_t registers[16] = { X86_REGISTER_RDI, X86_REGISTER_RSI, X86_REGISTER_RDX };
            uint32_t next = 0;
            for (uint32_t i = 0; i < shape->count; i += 1)
            {
                const uint32_t node = 3 + i;
                const bool comparison = node == shape->consumer;
                tier1_select_test_node(&function, node, comparison ? IR_ICMP : shape->op[i], comparison ? IR_I1 : shape->type[i], shape->a[i], shape->b[i], comparison ? IR_ULT : shape->c[i]);
                function.flags[node] = shape->flags[i];
                function.alignment_log2[node] = (shape->flags[i] & BUSTER_IR_ATOMIC) != 0 ? 3 : 0;
                const bool pooled = !comparison && shape->type[i] != IR_VOID;
                assert(!pooled || next < BUSTER_ARRAY_LENGTH(pool));
                registers[node] = pooled ? pool[next++] : X86_REGISTER_RAX;
            }
            for (uint32_t path = 0; path < 2; path += 1)
            {
                Tier1SelectPlan plan;
                uint32_t failed = 0;
                const Tier1SelectError selected = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &failed);
                char message[160];
                snprintf(message, sizeof(message), "comparison barrier fixture selects: %s", shape->name);
                tier1_select_test_expect(test, selected == TIER1_SELECT_OK, message);
                if (selected == TIER1_SELECT_OK)
                {
                    const bool covered = plan.covered[shape->load] != 0;
                    snprintf(message, sizeof(message), "comparison keeps access ordering: %s", shape->name);
                    tier1_select_test_expect(test, covered == shape->folds && (!covered || plan.patterns[shape->consumer].compare_memory), message);
                    if (!calls && tier1_select_test_walk_load(test, &plan, registers, &output))
                    {
                        for (uint64_t index = 0; index <= 8; index += 8)
                        {
                            uint64_t actual_memory[4] = { 73, 19, 51, 99 };
                            uint64_t expected_memory[4];
                            memcpy(expected_memory, actual_memory, sizeof(actual_memory));
                            const uint64_t expected = tier1_select_test_interpret(&function, (uintptr_t)expected_memory, index, 112);
                            const uint64_t actual = tier1_select_test_call(test, (uintptr_t)actual_memory, index, 112);
                            tier1_select_test_expect(test, actual == expected && memcmp(actual_memory, expected_memory, sizeof(actual_memory)) == 0, message);
                        }
                    }
                }
                arena_pop_to(&output, 0);
            }
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

// A store between the original comparison and its branch/extended truth prevents moving the access to that root.
// The comparison may fold the load before the store; it must still return true after the object becomes larger.
static void tier1_select_test_compare_memory_final_root(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 20);
    for (uint32_t branch = 0; branch < 2; branch += 1)
    {
        const IrSizes sizes =
        {
            .instruction_count = branch != 0 ? 12 : 9,
            .block_count = branch != 0 ? 3 : 1,
            .constant_count = 3,
            .parameter_count = 3,
        };
        IrFunction function;
        const bool created = ir_create(&input, &sizes, &function) == IR_OK;
        tier1_select_test_expect(test, created, "create final-root access ordering fixture");
        if (created)
        {
            function.result_type = IR_I32;
            function.block_offsets[1] = branch != 0 ? 8 : 9;
            function.parameter_types[0] = IR_PTR;
            function.parameter_types[1] = IR_I64;
            function.parameter_types[2] = IR_I64;
            function.constants[0].low = 50;
            function.constants[1].low = 11;
            function.constants[2].low = 22;
            tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 2, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 3, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 4, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_node(&function, 5, IR_ICMP, IR_I1, 3, 4, IR_ULT);
            tier1_select_test_node(&function, 6, IR_STORE, IR_VOID, 0, 2, BUSTER_IR_NONE);
            if (branch != 0)
            {
                function.block_offsets[2] = 10;
                function.block_offsets[3] = 12;
                tier1_select_test_node(&function, 7, IR_CBR, IR_VOID, 5, 1, 2);
                tier1_select_test_node(&function, 8, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 10, IR_CONST, IR_I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 11, IR_RET, IR_VOID, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else
            {
                tier1_select_test_node(&function, 7, IR_ZEXT, IR_I32, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 8, IR_RET, IR_VOID, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            const uint8_t registers[12] = { X86_REGISTER_RDI, X86_REGISTER_RSI, X86_REGISTER_RDX, X86_REGISTER_R8, X86_REGISTER_R9, X86_REGISTER_R10,
                                             X86_REGISTER_RAX, X86_REGISTER_RAX, X86_REGISTER_RAX, X86_REGISTER_RAX, X86_REGISTER_RAX, X86_REGISTER_RAX };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                Tier1SelectPlan plan;
                uint32_t failed = 0;
                const Tier1SelectError selected = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &failed);
                tier1_select_test_expect(test, selected == TIER1_SELECT_OK, "select a comparison whose consumer follows a store");
                if (selected == TIER1_SELECT_OK)
                {
                    tier1_select_test_expect(test, !plan.patterns[7].compare_memory && (plan.covered[5] == 0 || plan.covered[3] == 0), "the final consumer cannot move an access past an intervening store");
                    if (tier1_select_test_walk_load(test, &plan, registers, &output))
                    {
                        uint64_t memory = 17;
                        const uint64_t actual = tier1_select_test_call(test, (uintptr_t)&memory, 0, 112);
                        tier1_select_test_expect(test, actual == (branch != 0 ? 11u : 1u) && memory == 112, "a fused comparison reads its value before a later store");
                    }
                }
                arena_pop_to(&output, 0);
            }
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_compare_memory(Tier1SelectTest* test)
{
    tier1_select_test_compare_memory_holes(test);
    tier1_select_test_compare_memory_plans(test);
    tier1_select_test_compare_memory_barriers(test);
    tier1_select_test_compare_memory_final_root(test);
}
