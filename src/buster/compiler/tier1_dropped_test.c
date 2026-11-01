// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A recomputed value's definition that only an edge reads (#178, #141), included after tier1_variadic_test.c. The CFG
// consumer leaves out a rematerializable value's definition where the register its first fragment holds serves no use
// (tier1_emit_cfg_dead_definition). An edge whose predecessor ends inside that fragment reads the register too: a phi's
// operand, or the value live into a block that doesn't follow in the layout. f(p0, p1) defines 7 in its entry, which
// branches on p0's low bit to a block where nine values live at once with few registers, which takes 7 out of its
// register to recompute it for its last use there, and to the join, whose phi takes 7 from the entry's edge: the move
// along that edge reads 7's first register. Natively, by both paths, with and without rematerialization and with
// every register and with three, f returns 7 for an even p0 and the block's sum for an odd one.

#define BUSTER_TIER1_DROPPED_VALUES 9u

// f(p0, p1): b0 c = 7, branches on p0 & 1 to b1 or b3; b1 nine values of p1, their sum plus c, to b2; b2 to b3; b3
// returns phi(c from b0, the sum from b2).
static bool tier1_dropped_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 64,
        .block_count = 4,
        .extra_count = 4,
        .constant_count = 4 + BUSTER_TIER1_DROPPED_VALUES,
        .parameter_count = 2,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I64;
        function->parameter_types[1] = IR_I64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 2 };
        function->constants[0].low = 7;
        function->constants[1].low = 1;
        function->constants[2].low = 0;
        function->constants[3].low = 0;
        for (uint32_t k = 0; k < BUSTER_TIER1_DROPPED_VALUES; k += 1)
        {
            function->constants[4 + k].low = 3 + 2 * k;
        }
        uint32_t next = 0;
        tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t seven = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bit = tier1_select_test_append(function, &next, IR_AND, IR_I64, 0, one, BUSTER_IR_NONE);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t odd = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, bit, zero, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, odd, 1, 3);
        function->block_offsets[1] = next;
        uint32_t values[BUSTER_TIER1_DROPPED_VALUES];
        for (uint32_t k = 0; k < BUSTER_TIER1_DROPPED_VALUES; k += 1)
        {
            const uint32_t factor = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 4 + k, BUSTER_IR_NONE, BUSTER_IR_NONE);
            values[k] = tier1_select_test_append(function, &next, (k & 1) != 0 ? IR_MUL : IR_XOR, IR_I64, 1, factor, BUSTER_IR_NONE);
        }
        uint32_t sum = values[0];
        for (uint32_t k = 1; k < BUSTER_TIER1_DROPPED_VALUES; k += 1)
        {
            sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, values[k], BUSTER_IR_NONE);
        }
        const uint32_t total = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, seven, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[2] = next;
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[3] = next;
        const uint32_t joined = tier1_select_test_append(function, &next, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        function->extra[0] = 0;
        function->extra[1] = seven;
        function->extra[2] = 2;
        function->extra[3] = total;
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, joined, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[4] = next;
    }
    return created;
}

static uint64_t tier1_dropped_oracle(uint64_t p0, uint64_t p1)
{
    uint64_t sum = 0;
    for (uint32_t k = 0; k < BUSTER_TIER1_DROPPED_VALUES; k += 1)
    {
        const uint64_t factor = 3 + 2 * (uint64_t)k;
        sum += (k & 1) != 0 ? p1 * factor : p1 ^ factor;
    }
    return (p0 & 1) != 0 ? sum + 7 : 7;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_dropped_call(const void* code, uint64_t p0, uint64_t p1)
{
    uint64_t (*function)(uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(p0, p1);
}

static void tier1_dropped_compile(Tier1EmitTest* test, bool vector, bool rematerialize, uint32_t register_mask, uint32_t* dropped)
{
    IrFunction function;
    const bool created = tier1_dropped_function(&test->input, &function);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the dropped definition's fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the dropped definition's fixture selects");
    const Tier1EmitOptions options = { .register_mask = register_mask, .vector = vector, .rematerialize = rematerialize };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "the dropped definition's fixture emits through the CFG consumer");
    if (error == TIER1_EMIT_OK)
    {
        *dropped += emitted.dropped_definitions;
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x510e527fade682d1) ^ register_mask ^ (vector ? 1 : 0) ^ (rematerialize ? 2 : 0);
            for (uint32_t sample = 0; sample < 32; sample += 1)
            {
                const uint64_t p0 = (sample & 1) != 0 ? tier1_emit_test_random(&random) | 1 : tier1_emit_test_random(&random) & ~UINT64_C(1);
                const uint64_t p1 = tier1_emit_test_random(&random);
                // The code runs before the oracle, so that no register holds the expected value by chance: a definition
                // left out leaves its register as the caller had it (the oracle's result in rax, when it ran first).
                const uint64_t actual = tier1_dropped_call(executable, p0, p1);
                const uint64_t expected = tier1_dropped_oracle(p0, p1);
                if (actual != expected && test->failures < 20)
                {
                    fprintf(stderr, "  dropped vector=%u remat=%u mask=%x p0=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", vector, rematerialize, register_mask, p0, actual, expected);
                }
                tier1_emit_test_expect(test, actual == expected, "an edge that reads a recomputed value's register finds its definition");
            }
        }
        if (executable != MAP_FAILED)
        {
            munmap(executable, bytes);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_dropped_tests(Tier1EmitTest* test)
{
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI);
    uint32_t dropped = 0;
    for (uint32_t variant = 0; variant < 8; variant += 1)
    {
        tier1_dropped_compile(test, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0 ? few : tier1_liveness_register_mask(), &dropped);
    }
    printf("tier1 dropped definitions: %u left out over the fixture's compiles\n", dropped);
}
