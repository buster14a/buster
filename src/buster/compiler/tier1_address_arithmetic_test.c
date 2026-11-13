// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1217: exercise the selected rules, not just their emission helpers. Included by tier1_select_test.c after its
// plan and execution helpers. Both covers must avoid power-of-two imuls and fold legal signed loads and addresses;
// observable accesses, intervening memory operations and shared loads keep their original evaluation points.

static void tier1_select_test_power_multiplication(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        const uint8_t type = width == 4 ? IR_I32 : IR_I64;
        for (uint32_t shift = 0; shift < width * 8; shift += 1)
        {
            for (uint32_t order = 0; order < 2; order += 1)
            {
                const IrSizes sizes =
                {
                    .instruction_count = 4,
                    .block_count = 1,
                    .constant_count = 1,
                    .parameter_count = 1,
                };
                IrFunction function;
                const IrError created = ir_create(&input, &sizes, &function);
                tier1_select_test_expect(test, created == IR_OK, "create power-of-two multiplication fixture");
                if (created == IR_OK)
                {
                    function.result_type = type;
                    function.block_offsets[1] = 4;
                    function.parameter_types[0] = type;
                    function.constants[0].low = UINT64_C(1) << shift;
                    tier1_select_test_node(&function, 0, IR_PARAM, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 1, IR_CONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 2, IR_MUL, type, order == 0 ? 0 : 1, order == 0 ? 1 : 0, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    for (uint32_t path = 0; path < 2; path += 1)
                    {
                        Tier1SelectPlan plan;
                        uint32_t node = BUSTER_IR_NONE;
                        const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
                        tier1_select_test_expect(test, error == TIER1_SELECT_OK, "power-of-two multiplication selects on both paths");
                        if (error == TIER1_SELECT_OK)
                        {
                            tier1_select_test_expect(test, plan.patterns[2].rule == TIER1_RULE_MULTIPLY_SHIFT && plan.patterns[2].scale == shift && plan.covered[1] != 0,
                                                     "every power of two, including the sign bit and one, selects a shift rather than imul");
                        }
                        arena_pop_to(&output, 0);
                    }
                    // Separate and coalesced destinations: the optional move before a shift must preserve the input.
                    for (uint32_t coalesced = 0; coalesced < 2; coalesced += 1)
                    {
                        const uint8_t registers[4] =
                        {
                            X86_REGISTER_RDI,
                            X86_REGISTER_RCX,
                            coalesced != 0 ? X86_REGISTER_RDI : X86_REGISTER_RAX,
                            X86_REGISTER_RAX,
                        };
                        tier1_select_test_plan_execute(test, &function, registers, &output, &scratch);
                    }
                }
                arena_pop_to(&input, 0);
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_signed_loads(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    uint8_t* mapping = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + 4096, 4096, PROT_NONE) == 0;
    tier1_select_test_expect(test, guarded, "protect the page after a signed load's exact-width object");
    for (uint32_t source_width = 1; guarded && source_width <= 4; source_width *= 2)
    {
        const uint8_t source_type = source_width == 1 ? IR_I8 : source_width == 2 ? IR_I16 : IR_I32;
        const uint64_t sign = UINT64_C(1) << (source_width * 8 - 1);
        const uint64_t mask = (sign << 1) - 1;
        const uint64_t samples[] = {0, 1, sign - 1, sign, mask};
        uint8_t* bytes = mapping + 4096 - source_width;
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            if (width > source_width)
            {
                const uint8_t type = width == 4 ? IR_I32 : IR_I64;
                for (uint32_t variant = 0; variant < 5; variant += 1)
                {
                    const IrSizes sizes =
                    {
                        .instruction_count = 8,
                        .block_count = 1,
                        .constant_count = 1,
                        .address_count = 1,
                        .parameter_count = 3,
                    };
                    IrFunction function;
                    const IrError created = ir_create(&input, &sizes, &function);
                    tier1_select_test_expect(test, created == IR_OK, "create signed-load fixture");
                    if (created == IR_OK)
                    {
                        function.result_type = type;
                        function.block_offsets[1] = 8;
                        function.parameter_types[0] = IR_PTR;
                        function.parameter_types[1] = IR_I64;
                        function.parameter_types[2] = source_type;
                        function.addresses[0].scale = 1;
                        function.constants[0].low = 0;
                        tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        tier1_select_test_node(&function, 2, IR_PARAM, source_type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        tier1_select_test_node(&function, 3, IR_ADDRESS, IR_PTR, 0, 1, 0);
                        tier1_select_test_node(&function, 4, IR_LOAD, source_type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        function.flags[4] = variant == 1 ? BUSTER_IR_VOLATILE : 0;
                        tier1_select_test_node(&function, 5, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        if (variant == 2)
                        {
                            tier1_select_test_node(&function, 5, IR_STORE, IR_VOID, 3, 2, BUSTER_IR_NONE);
                        }
                        else if (variant == 3)
                        {
                            tier1_select_test_node(&function, 5, IR_LOAD, source_type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                            function.flags[5] = BUSTER_IR_VOLATILE;
                        }
                        else if (variant == 4)
                        {
                            tier1_select_test_node(&function, 5, IR_COPY, source_type, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        }
                        tier1_select_test_node(&function, 6, IR_SEXT, type, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        tier1_select_test_node(&function, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
                        const uint8_t registers[8] =
                        {
                            X86_REGISTER_RDI,
                            X86_REGISTER_RSI,
                            X86_REGISTER_RDX,
                            X86_REGISTER_R8,
                            X86_REGISTER_RCX,
                            X86_REGISTER_R9,
                            X86_REGISTER_RAX,
                            X86_REGISTER_RAX,
                        };
                        for (uint32_t path = 0; path < 2; path += 1)
                        {
                            Tier1SelectPlan plan;
                            uint32_t node = BUSTER_IR_NONE;
                            const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
                            tier1_select_test_expect(test, error == TIER1_SELECT_OK, "signed loads select on both paths");
                            if (error == TIER1_SELECT_OK)
                            {
                                tier1_select_test_expect(test, (plan.covered[4] != 0) == (variant == 0), "signed-load folding preserves volatility, memory order and multiple uses");
                                if (variant == 0)
                                {
                                    const Tier1SelectPattern* pattern = &plan.patterns[6];
                                    tier1_select_test_expect(test, pattern->rule == TIER1_RULE_LOAD_ADDRESS && pattern->width == source_width &&
                                                                     pattern->operation == X86_OPERATION_MOVSX && (pattern->source_width == 4 || pattern->source_width == 8) &&
                                                                     pattern->values[0] == 0 && pattern->values[1] == 1 && plan.covered[3] != 0,
                                                             "signed load retains the access width and folds its base and index");
                                    const uint32_t first = plan.effective_use_offsets[6];
                                    const uint32_t end = plan.effective_use_offsets[7];
                                    bool base = false;
                                    bool index = false;
                                    for (uint32_t u = first; u < end; u += 1)
                                    {
                                        base |= plan.effective_use_values[u] == 0;
                                        index |= plan.effective_use_values[u] == 1;
                                    }
                                    tier1_select_test_expect(test, end - first == 2 && base && index, "folded signed load keeps both address operands live until the extension");
                                }
                                Tier1SelectCode code;
                                const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                                tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "signed-load fixture emits");
                                if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                                {
                                    for (uint32_t sample = 0; sample < BUSTER_ARRAY_LENGTH(samples); sample += 1)
                                    {
                                        const uint64_t bits = samples[sample];
                                        memcpy(bytes, &bits, source_width);
                                        const uint64_t wanted = ((bits ^ sign) - sign) & (width == 4 ? UINT32_MAX : UINT64_MAX);
                                        const uint64_t actual = tier1_select_test_call(test, (uintptr_t)(bytes - 3), 3, 18);
                                        uint64_t stored = 0;
                                        memcpy(&stored, bytes, source_width);
                                        tier1_select_test_expect(test, actual == wanted && stored == (variant == 2 ? 18 : bits),
                                                                 "signed loads execute at sign boundaries without widening or crossing a store");
                                    }
                                }
                            }
                            arena_pop_to(&output, 0);
                        }
                    }
                    arena_pop_to(&input, 0);
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

static void tier1_select_test_added_address(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    const IrSizes sizes =
    {
        .instruction_count = 7,
        .block_count = 1,
        .parameter_count = 2,
    };
    IrFunction function;
    const IrError created = ir_create(&input, &sizes, &function);
    tier1_select_test_expect(test, created == IR_OK, "create pointer-integer addition fixture");
    if (created == IR_OK)
    {
        function.result_type = IR_I64;
        function.block_offsets[1] = 7;
        function.parameter_types[0] = IR_PTR;
        function.parameter_types[1] = IR_I64;
        tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 2, IR_PTRTOINT, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 3, IR_ADD, IR_I64, 2, 1, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 4, IR_INTTOPTR, IR_PTR, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 5, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 6, IR_RET, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint8_t registers[7] = {X86_REGISTER_RDI, X86_REGISTER_RSI, X86_REGISTER_R8, X86_REGISTER_R9, X86_REGISTER_R10, X86_REGISTER_RAX, X86_REGISTER_RAX};
        for (uint32_t path = 0; path < 2; path += 1)
        {
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
            tier1_select_test_expect(test, error == TIER1_SELECT_OK, "pointer-integer addition selects on both paths");
            if (error == TIER1_SELECT_OK)
            {
                tier1_select_test_expect(test, plan.patterns[5].rule == TIER1_RULE_LOAD_ADDRESS && plan.patterns[5].values[0] == 0 && plan.patterns[5].values[1] == 1 &&
                                                 plan.covered[2] != 0 && plan.covered[3] != 0 && plan.covered[4] != 0,
                                         "a 64-bit pointer addition is the load's memory operand, without an intervening lea");
                Tier1SelectCode code;
                const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "added-address fixture emits");
                if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                {
                    const uint64_t values[] = {UINT64_MAX, 17, UINT64_C(0x8000000000000000), 42};
                    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(values); i += 1)
                    {
                        const uint64_t actual = tier1_select_test_call(test, (uintptr_t)values, i * sizeof(uint64_t), 0);
                        tier1_select_test_expect(test, actual == values[i], "folded pointer-integer addition executes its original address");
                    }
                }
            }
            arena_pop_to(&output, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

// A typed pointer chain and a scaled address, shared by a store and a load. The first store keeps either an integer,
// the final pointer, its shared addition, its shared inttoptr, or its shared ptrtoint as an escaping value. Only memory
// operands may unfold shared nodes; the escaping node remains computed and its value is checked independently.
static void tier1_select_test_shared_address(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    static const uint32_t escaped[] = {2, 7, 5, 6, 3};
    for (uint32_t scale = 1; scale <= 8; scale *= 2)
    {
        for (uint32_t escape = 0; escape < BUSTER_ARRAY_LENGTH(escaped); escape += 1)
        {
            for (uint32_t observable = 0; observable < 2; observable += 1)
            {
                const IrSizes sizes =
                {
                    .instruction_count = 12,
                    .block_count = 1,
                    .constant_count = 1,
                    .address_count = 1,
                    .parameter_count = 3,
                };
                IrFunction function;
                const IrError created = ir_create(&input, &sizes, &function);
                tier1_select_test_expect(test, created == IR_OK, "create shared address fixture");
                if (created == IR_OK)
                {
                    function.result_type = IR_I64;
                    function.block_offsets[1] = 12;
                    function.parameter_types[0] = IR_PTR;
                    function.parameter_types[1] = IR_I64;
                    function.parameter_types[2] = IR_I64;
                    function.constants[0].low = 8;
                    function.addresses[0] = (IrAddress){ .scale = scale };
                    tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 2, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 3, IR_PTRTOINT, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 4, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 5, IR_ADD, IR_I64, 3, 4, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 6, IR_INTTOPTR, IR_PTR, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 7, IR_ADDRESS, IR_PTR, 6, 1, 0);
                    tier1_select_test_node(&function, 8, IR_STORE, IR_VOID, 0, escaped[escape], BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 9, IR_STORE, IR_VOID, 7, 2, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 10, IR_LOAD, IR_I64, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 11, IR_RET, IR_VOID, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    function.flags[9] = observable != 0 ? BUSTER_IR_VOLATILE : 0;
                    function.flags[10] = observable != 0 ? BUSTER_IR_VOLATILE : 0;
                    const uint8_t registers[12] =
                    {
                        X86_REGISTER_RDI,
                        X86_REGISTER_RSI,
                        X86_REGISTER_RDX,
                        X86_REGISTER_R8,
                        X86_REGISTER_R9,
                        X86_REGISTER_R10,
                        X86_REGISTER_RAX,
                        X86_REGISTER_R8,
                        X86_REGISTER_RAX,
                        X86_REGISTER_RAX,
                        X86_REGISTER_RAX,
                        X86_REGISTER_RAX,
                    };
                    for (uint32_t path = 0; path < 2; path += 1)
                    {
                        Tier1SelectPlan plan;
                        uint32_t node = BUSTER_IR_NONE;
                        const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
                        tier1_select_test_expect(test, error == TIER1_SELECT_OK, "shared address selects on both paths");
                        if (error == TIER1_SELECT_OK)
                        {
                            tier1_select_test_expect(test, (plan.covered[7] != 0) == (escape != 1) && !plan.covered[escaped[escape]],
                                                     "an address needed by an escaping use remains computed while memory-only shared nodes disappear");
                            for (uint32_t access = 9; access <= 10; access += 1)
                            {
                                const Tier1SelectPattern* pattern = &plan.patterns[access];
                                const uint32_t first = plan.effective_use_offsets[access];
                                const uint32_t uses = plan.effective_use_offsets[access + 1] - first;
                                tier1_select_test_expect(test, pattern->rule == (access == 9 ? TIER1_RULE_STORE_ADDRESS : TIER1_RULE_LOAD_ADDRESS) &&
                                                                 pattern->values[0] == 0 && pattern->values[1] == 1 && pattern->scale == tier1_select_log2(scale) && pattern->displacement == 8 &&
                                                                 uses == (access == 9 ? 3u : 2u) && plan.effective_use_values[first] == 0 && plan.effective_use_values[first + 1] == 1 &&
                                                                 (access != 9 || plan.effective_use_values[first + 2] == 2),
                                                         "each shared memory address publishes the original base, index and stored value as effective uses");
                            }
                            Tier1SelectCode code;
                            const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                            tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "shared address fixture emits");
                            if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                            {
                                for (uint32_t index = 0; index < 4; index += 1)
                                {
                                    uint8_t memory[64];
                                    uint8_t wanted[64];
                                    memset(memory, 0xa5, sizeof(memory));
                                    memcpy(wanted, memory, sizeof(memory));
                                    const uint64_t value = UINT64_C(0xfedcba9876543210) >> index;
                                    const uint32_t offset = 8 + index * scale;
                                    const uint64_t escaped_value = escape == 0 ? value : (uintptr_t)(memory + (escape == 1 ? offset : escape == 4 ? 0 : 8));
                                    memcpy(wanted, &escaped_value, 8);
                                    memcpy(wanted + offset, &value, 8);
                                    const uint64_t actual = tier1_select_test_call(test, (uintptr_t)memory, index, value);
                                    tier1_select_test_expect(test, actual == value && memcmp(memory, wanted, sizeof(memory)) == 0,
                                                             "shared address folding preserves native memory, escaping pointers and volatile access order");
                                }
                            }
                        }
                        arena_pop_to(&output, 0);
                    }
                }
                arena_pop_to(&input, 0);
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

typedef enum Tier1SelectIndexShape
{
    TIER1_TEST_INDEX_PLAIN,
    TIER1_TEST_INDEX_ADD_FIRST,
    TIER1_TEST_INDEX_ADD_LAST,
    TIER1_TEST_INDEX_SUB_FIRST,
    TIER1_TEST_INDEX_SUB_LAST,
    TIER1_TEST_INDEX_SHARED,
    TIER1_TEST_INDEX_SEXT,
    TIER1_TEST_INDEX_ZEXT,
    TIER1_TEST_INDEX_SHARED_CONSTANT,
} Tier1SelectIndexShape;

typedef struct Tier1SelectIndexCase Tier1SelectIndexCase;
struct Tier1SelectIndexCase
{
    uint8_t shift;
    uint8_t outer;
    uint8_t shape;
    // 0: IR_ADDRESS memory operand, 1: pointer/integer addition, 2 and 3: the same address returned as a value.
    uint8_t form;
};

// Scale arithmetic is a separate IR node in p[i << k] and p[i * (1 << k)]. Both memory consumers must read the
// unscaled index after folding, while a shared index, a narrowed operation or a scale above eight remains computed.
static void tier1_select_test_scaled_index(Tier1SelectTest* test)
{
    static const Tier1SelectIndexCase cases[] =
    {
        { .shift = 0 },
        { .shift = 1 },
        { .shift = 2 },
        { .shift = 3 },
        { .shift = 1, .outer = 2 },
        { .shift = 2, .outer = 2 },
        { .shift = 2, .shape = TIER1_TEST_INDEX_ADD_FIRST },
        { .shift = 2, .shape = TIER1_TEST_INDEX_ADD_LAST },
        { .shift = 1, .outer = 1, .shape = TIER1_TEST_INDEX_SUB_FIRST },
        { .shift = 1, .outer = 1, .shape = TIER1_TEST_INDEX_SUB_LAST },
        { .shift = 2, .shape = TIER1_TEST_INDEX_SHARED },
        { .shift = 2, .shape = TIER1_TEST_INDEX_SEXT },
        { .shift = 2, .shape = TIER1_TEST_INDEX_ZEXT },
        { .shift = 2, .shape = TIER1_TEST_INDEX_SHARED_CONSTANT },
        { .shift = 2, .form = 1 },
        { .shift = 2, .outer = 1, .form = 2 },
        { .shift = 2, .form = 3 },
    };
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    for (uint32_t operation = 0; operation < 3; operation += 1)
    {
        for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(cases); fixture += 1)
        {
            const Tier1SelectIndexCase item = cases[fixture];
            const bool before = item.shape == TIER1_TEST_INDEX_ADD_FIRST || item.shape == TIER1_TEST_INDEX_SUB_FIRST;
            const bool after = item.shape == TIER1_TEST_INDEX_ADD_LAST || item.shape == TIER1_TEST_INDEX_SUB_LAST;
            const bool subtract = item.shape == TIER1_TEST_INDEX_SUB_FIRST || item.shape == TIER1_TEST_INDEX_SUB_LAST;
            const bool narrow = item.shape == TIER1_TEST_INDEX_SEXT || item.shape == TIER1_TEST_INDEX_ZEXT;
            const bool shared = item.shape == TIER1_TEST_INDEX_SHARED;
            const bool shared_constant = item.shape == TIER1_TEST_INDEX_SHARED_CONSTANT;
            const bool added = (item.form & 1) != 0;
            const bool memory = item.form < 2;
            const uint8_t type = narrow ? IR_I32 : IR_I64;
            const IrSizes sizes =
            {
                .instruction_count = 3 + 1 + (before || after) + before + after + 1 + narrow + (shared || shared_constant) + shared_constant + (added ? 3 : 1) + (memory ? 3 : 1),
                .block_count = 1,
                .constant_count = 1 + (before || after),
                .address_count = added ? 0 : 1,
                .parameter_count = 3,
            };
            IrFunction function;
            const IrError created = ir_create(&input, &sizes, &function);
            tier1_select_test_expect(test, created == IR_OK, "create scaled-index fixture");
            if (created == IR_OK)
            {
                function.result_type = memory ? IR_I64 : IR_PTR;
                function.block_offsets[1] = sizes.instruction_count;
                function.parameter_types[0] = IR_PTR;
                function.parameter_types[1] = type;
                function.parameter_types[2] = IR_I64;
                function.constants[0].low = operation == 0 ? item.shift : UINT64_C(1) << item.shift;
                uint8_t registers[16] = {X86_REGISTER_RDI, X86_REGISTER_RSI, X86_REGISTER_RDX, X86_REGISTER_RCX};
                tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 1, IR_PARAM, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 2, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_node(&function, 3, IR_CONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                uint32_t next = 4;
                uint32_t index = 1;
                uint32_t adjustment = BUSTER_IR_NONE;
                if (before || after)
                {
                    adjustment = next++;
                    function.constants[1].low = 3;
                    registers[adjustment] = X86_REGISTER_R8;
                    tier1_select_test_node(&function, adjustment, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
                if (before)
                {
                    registers[next] = X86_REGISTER_R9;
                    tier1_select_test_node(&function, next, subtract ? IR_SUB : IR_ADD, IR_I64, index, adjustment, BUSTER_IR_NONE);
                    index = next++;
                }
                const uint32_t scaled = next++;
                registers[scaled] = X86_REGISTER_R10;
                tier1_select_test_node(&function, scaled, operation == 0 ? IR_SHL : IR_MUL, type, operation == 2 ? 3 : index, operation == 2 ? index : 3, BUSTER_IR_NONE);
                index = scaled;
                if (after)
                {
                    registers[next] = X86_REGISTER_R8;
                    tier1_select_test_node(&function, next, subtract ? IR_SUB : IR_ADD, IR_I64, index, adjustment, BUSTER_IR_NONE);
                    index = next++;
                }
                if (narrow)
                {
                    registers[next] = X86_REGISTER_R10;
                    tier1_select_test_node(&function, next, item.shape == TIER1_TEST_INDEX_SEXT ? IR_SEXT : IR_ZEXT, IR_I64, index, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    index = next++;
                }
                if (shared_constant)
                {
                    // The constant read as a register: a subtraction's left operand, stored (a store of the constant itself
                    // takes it as its immediate, #2554).
                    registers[next] = X86_REGISTER_RAX;
                    tier1_select_test_node(&function, next, IR_SUB, type, 3, 2, BUSTER_IR_NONE);
                    next += 1;
                    tier1_select_test_node(&function, next, IR_STORE, IR_VOID, 0, next - 1, BUSTER_IR_NONE);
                    next += 1;
                }
                if (shared)
                {
                    tier1_select_test_node(&function, next++, IR_STORE, IR_VOID, 0, index, BUSTER_IR_NONE);
                }
                if (added)
                {
                    registers[next] = X86_REGISTER_RCX;
                    tier1_select_test_node(&function, next, IR_PTRTOINT, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    const uint32_t base = next++;
                    registers[next] = X86_REGISTER_R9;
                    tier1_select_test_node(&function, next, IR_ADD, IR_I64, base, index, BUSTER_IR_NONE);
                    const uint32_t sum = next++;
                    tier1_select_test_node(&function, next, IR_INTTOPTR, IR_PTR, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
                else
                {
                    function.addresses[0] = (IrAddress){ .offset = 16, .scale = 1u << item.outer };
                    tier1_select_test_node(&function, next, IR_ADDRESS, IR_PTR, 0, index, 0);
                }
                const uint32_t address = next++;
                registers[address] = X86_REGISTER_R9;
                const uint32_t store = next;
                if (memory)
                {
                    tier1_select_test_node(&function, next++, IR_STORE, IR_VOID, address, 2, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, next++, IR_LOAD, IR_I64, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
                tier1_select_test_node(&function, next, IR_RET, IR_VOID, memory ? next - 1 : address, BUSTER_IR_NONE, BUSTER_IR_NONE);
                tier1_select_test_expect(test, next + 1 == sizes.instruction_count, "scaled-index fixture fills exactly its declared nodes");
                const bool folded = !narrow && !shared && item.shift + item.outer <= 3;
                const uint32_t wanted_index = folded ? 1 : index;
                const uint8_t wanted_scale = folded ? item.shift + item.outer : item.outer;
                const int32_t wanted_displacement = (added ? 0 : 16) + (before || after ? (subtract ? -3 : 3) * (1 << (item.outer + (before ? item.shift : 0))) : 0);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    Tier1SelectPlan plan;
                    uint32_t failed = BUSTER_IR_NONE;
                    const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &failed);
                    tier1_select_test_expect(test, error == TIER1_SELECT_OK, "scaled indices select on both paths");
                    if (error == TIER1_SELECT_OK)
                    {
                        tier1_select_test_expect(test, (plan.covered[scaled] != 0) == folded && (!shared_constant || !plan.covered[3]),
                                                 "scale folding retains narrowed and shared indices and separately used constants");
                        for (uint32_t use = memory ? store : address; use <= (memory ? store + 1 : address); use += 1)
                        {
                            const Tier1SelectPattern* pattern = &plan.patterns[use];
                            const uint32_t first = plan.effective_use_offsets[use];
                            const uint32_t count = plan.effective_use_offsets[use + 1] - first;
                            const bool storing = memory && use == store;
                            tier1_select_test_expect(test, pattern->rule == (storing ? TIER1_RULE_STORE_ADDRESS : memory ? TIER1_RULE_LOAD_ADDRESS : TIER1_RULE_ADDRESS) &&
                                                             pattern->values[0] == 0 && pattern->values[1] == wanted_index && pattern->scale == wanted_scale &&
                                                             pattern->displacement == wanted_displacement && count == (storing ? 3u : 2u) &&
                                                             plan.effective_use_values[first] == 0 && plan.effective_use_values[first + 1] == wanted_index &&
                                                             (!storing || plan.effective_use_values[first + 2] == 2),
                                                     "address selection retains the exact scale, displacement and effective base/index uses");
                        }
                        Tier1SelectCode code;
                        const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &failed);
                        tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "scaled-index fixture emits");
                        if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                        {
                            static const uint64_t samples[] = {0, 1, 2, UINT64_MAX, UINT64_C(0x8000000000000000)};
                            for (uint32_t sample = 0; sample < (memory ? 4u : BUSTER_ARRAY_LENGTH(samples)); sample += 1)
                            {
                                uint8_t actual_memory[256];
                                uint8_t expected_memory[256];
                                for (uint32_t byte = 0; byte < sizeof(actual_memory); byte += 1)
                                {
                                    actual_memory[byte] = (uint8_t)(byte * 37 + 90);
                                }
                                memcpy(expected_memory, actual_memory, sizeof(actual_memory));
                                const uint64_t argument = narrow ? UINT64_C(0x40000000) + (sample & 3) : samples[sample];
                                const uint64_t actual_base = memory ? (uintptr_t)(actual_memory + 96) : UINT64_MAX - 17;
                                const uint64_t expected_base = memory ? (uintptr_t)(expected_memory + 96) : actual_base;
                                const uint64_t value = UINT64_C(0xfedcba9876543210) >> sample;
                                const uint64_t expected = tier1_select_test_interpret(&function, expected_base, argument, value);
                                const uint64_t actual = tier1_select_test_call(test, actual_base, argument, value);
                                tier1_select_test_expect(test, actual == expected && memcmp(actual_memory, expected_memory, sizeof(actual_memory)) == 0,
                                                         "scaled-index native values and memory match the independent IR, including wrap before extension");
                            }
                        }
                    }
                    arena_pop_to(&output, 0);
                }
            }
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

// #2602: an addition of a load, p[1] + x or x + p[1], at both widths, by both paths. Its memory form, at two, takes the
// load over a load and a lea, also two: the load is covered, and the sum is one record in x's register, two in another
// (x's copy, then the addition), and two in the base's (the load into it, then the addition: no scratch register). A
// load that a second addition also reads stays one load. Each runs natively against the original IR's interpretation.
static void tier1_select_test_load_addition(Tier1SelectTest* test)
{
    Arena input = arena_reserve(1u << 20);
    Arena output = arena_reserve(8u << 20);
    Arena scratch = arena_reserve(8u << 20);
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        const uint8_t type = width == 4 ? IR_I32 : IR_I64;
        for (uint32_t order = 0; order < 2; order += 1)
        {
            for (uint32_t shared = 0; shared < 2; shared += 1)
            {
                const uint32_t count = shared != 0 ? 9 : 7;
                const IrSizes sizes =
                {
                    .instruction_count = count,
                    .block_count = 1,
                    .address_count = 1,
                    .parameter_count = 3,
                };
                IrFunction function;
                const IrError created = ir_create(&input, &sizes, &function);
                tier1_select_test_expect(test, created == IR_OK, "create load addition fixture");
                if (created == IR_OK)
                {
                    function.result_type = type;
                    function.block_offsets[1] = count;
                    function.parameter_types[0] = IR_PTR;
                    function.parameter_types[1] = type;
                    function.parameter_types[2] = type;
                    function.addresses[0] = (IrAddress){ .scale = 1, .offset = 8 };
                    tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 1, IR_PARAM, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 2, IR_PARAM, type, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 3, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 0);
                    tier1_select_test_node(&function, 4, IR_LOAD, type, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    tier1_select_test_node(&function, 5, IR_ADD, type, order == 0 ? 4 : 1, order == 0 ? 1 : 4, BUSTER_IR_NONE);
                    if (shared != 0)
                    {
                        tier1_select_test_node(&function, 6, IR_ADD, type, 4, 2, BUSTER_IR_NONE);
                        tier1_select_test_node(&function, 7, IR_ADD, type, 5, 6, BUSTER_IR_NONE);
                    }
                    tier1_select_test_node(&function, count - 1, IR_RET, IR_VOID, count - 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    // The sum's register: x's, another, or the base's.
                    static const uint8_t sums[] = { X86_REGISTER_RSI, X86_REGISTER_RAX, X86_REGISTER_RDI };
                    for (uint32_t s = 0; s < (shared != 0 ? 1u : BUSTER_ARRAY_LENGTH(sums)); s += 1)
                    {
                        const uint8_t registers[9] =
                        {
                            X86_REGISTER_RDI,
                            X86_REGISTER_RSI,
                            X86_REGISTER_RDX,
                            X86_REGISTER_R8,
                            X86_REGISTER_R9,
                            shared != 0 ? X86_REGISTER_R10 : sums[s],
                            shared != 0 ? X86_REGISTER_RCX : X86_REGISTER_RAX,
                            X86_REGISTER_RAX,
                            X86_REGISTER_RAX,
                        };
                        for (uint32_t path = 0; path < 2; path += 1)
                        {
                            Tier1SelectPlan plan;
                            uint32_t node = BUSTER_IR_NONE;
                            const Tier1SelectError error = tier1_select_plan(&function, 0, &output, &scratch, path == 0, &plan, &node);
                            tier1_select_test_expect(test, error == TIER1_SELECT_OK, "load addition selects on both paths");
                            if (error == TIER1_SELECT_OK)
                            {
                                const bool folded = shared == 0 && plan.covered[3] != 0 && plan.covered[4] != 0 && plan.patterns[5].rule == TIER1_RULE_LOAD_OPERAND &&
                                                    plan.patterns[5].intrinsic_cost == 2u + plan.patterns[5].sign_result && plan.patterns[5].values[0] == 1;
                                const bool kept = shared != 0 && plan.covered[4] == 0 && plan.patterns[5].rule != TIER1_RULE_LOAD_OPERAND && plan.patterns[6].rule != TIER1_RULE_LOAD_OPERAND;
                                tier1_select_test_expect(test, folded || kept, "an addition takes its single-use load as its memory operand, and a shared load stays computed");
                                Tier1SelectCode code;
                                const Tier1SelectError emitted = tier1_select_emit(&plan, registers, test->allowed, &output, &code, &node);
                                tier1_select_test_expect(test, emitted == TIER1_SELECT_OK, "load addition fixture emits");
                                if (emitted == TIER1_SELECT_OK)
                                {
                                    uint32_t accesses = 0;
                                    bool scratched = false;
                                    for (uint32_t r = 0; r < code.count; r += 1)
                                    {
                                        const uint16_t form = code.records[r].form;
                                        accesses += (code.records[r].mode & BUSTER_X86_MODE_MEMORY) != 0 && form != X86_FORM_LEA_R32_M && form != X86_FORM_LEA_R64_M;
                                        scratched |= code.records[r].reg == BUSTER_TIER1_SELECT_SCRATCH || code.records[r].rm == BUSTER_TIER1_SELECT_SCRATCH;
                                    }
                                    const uint32_t records = code.node_first[6] - code.node_first[5];
                                    const bool sized = shared != 0 || (records == (sums[s] == X86_REGISTER_RSI ? 1u : 2u) && !scratched);
                                    tier1_select_test_expect(test, accesses == 1 && sized, "the sum is one record in x's register and two elsewhere, reading memory once");
                                }
                                if (emitted == TIER1_SELECT_OK && tier1_select_test_load_code(test, code.records, code.count, code.count, true))
                                {
                                    for (uint32_t sample = 0; sample < 4; sample += 1)
                                    {
                                        uint64_t memory[3] =
                                        {
                                            UINT64_C(0x5a5a5a5a5a5a5a5a),
                                            UINT64_C(0xfedcba9876543210) >> sample,
                                            UINT64_C(0xa5a5a5a5a5a5a5a5),
                                        };
                                        const uint64_t x = UINT64_C(0x0123456789abcdef) << sample;
                                        const uint64_t y = UINT64_MAX - sample;
                                        const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
                                        const uint64_t expected = tier1_select_test_interpret(&function, (uintptr_t)memory, x, y) & mask;
                                        const uint64_t actual = tier1_select_test_call(test, (uintptr_t)memory, x, y) & mask;
                                        tier1_select_test_expect(test, actual == expected, "a load's addition executes what the original IR computes");
                                    }
                                }
                            }
                            arena_pop_to(&output, 0);
                        }
                    }
                }
                arena_pop_to(&input, 0);
            }
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void tier1_select_test_address_arithmetic(Tier1SelectTest* test)
{
    tier1_select_test_load_addition(test);
    tier1_select_test_power_multiplication(test);
    tier1_select_test_signed_loads(test);
    tier1_select_test_added_address(test);
    tier1_select_test_shared_address(test);
    tier1_select_test_scaled_index(test);
}
