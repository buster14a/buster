// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A two-address result in its dying first operand's register (#2685), included after tier1_dropped_test.c. Both
// consumers hint a result whose operand's interval ends at its definition to that operand (tier1_select_ties): the CFG
// consumer's problem by Tier1CfgInput.ties (tier1_cfg_ties), the straight-line one's by the allocator's value hints. Where
// the result took the lowest free register, rax, its records copied the operand into it first. A value a return reads
// takes no tie (tier1_select_returned): the return wants it in rax, where the lowest free register usually is.
//
// f(p0, p1), the CFG consumer's: x = p0 - p1, y = x ^ 0x55, then a branch on y's low bit to a return of y + p1 or of y - p1.
// g(p0, p1), the straight-line one's: x = p0 - p1, y = x ^ 0x55, return y + p1. (An addition of two registers would be a
// lea, three-operand, which ties nothing.) h(p0, p1), straight-line: return p0 | p1,
// whose operands both end at it. k(p0, p1), straight-line: x = p0 - p1, then the address x + 8, whose dying base it ties
// (#2592: `lea 8(%r),%r` costs what a lea into another register does, and Lua's OP_LFALSESKIP's `pc + 1` keeps the
// register its dispatch takes `pc` in), returned xored with 0x55, which folds no address. Each runs natively against its
// oracle.

// f(p0, p1), or with `branch` false g(p0, p1); with `returned`, h(p0, p1); with `address`, k(p0, p1).
static bool tier1_ties_function(Arena* input, IrFunction* function, bool branch, bool returned, bool address)
{
    const IrSizes sizes = {
        .instruction_count = 16,
        .block_count = 3,
        .constant_count = 3,
        .address_count = address ? 1 : 0,
        .parameter_count = 2,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I64;
        function->parameter_types[1] = IR_I64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 2 };
        function->constants[0].low = 0x55;
        function->constants[1].low = 1;
        function->constants[2].low = 0;
        uint32_t next = 0;
        const uint32_t p0 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t p1 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (address)
        {
            function->addresses[0] = (IrAddress){ .scale = 1, .offset = 8 };
            const uint32_t x = tier1_select_test_append(function, &next, IR_SUB, IR_I64, p0, p1, BUSTER_IR_NONE);
            const uint32_t pointer = tier1_select_test_append(function, &next, IR_INTTOPTR, IR_PTR, x, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t moved = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, pointer, BUSTER_IR_NONE, 0);
            const uint32_t integer = tier1_select_test_append(function, &next, IR_PTRTOINT, IR_I64, moved, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t mask = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t flipped = tier1_select_test_append(function, &next, IR_XOR, IR_I64, integer, mask, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, flipped, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->block_count = 1;
        }
        else if (returned)
        {
            const uint32_t both = tier1_select_test_append(function, &next, IR_OR, IR_I64, p0, p1, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, both, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->block_count = 1;
        }
        else
        {
            const uint32_t x = tier1_select_test_append(function, &next, IR_SUB, IR_I64, p0, p1, BUSTER_IR_NONE);
            const uint32_t mask = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t y = tier1_select_test_append(function, &next, IR_XOR, IR_I64, x, mask, BUSTER_IR_NONE);
            if (branch)
            {
                const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
                const uint32_t bit = tier1_select_test_append(function, &next, IR_AND, IR_I64, y, one, BUSTER_IR_NONE);
                const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                const uint32_t odd = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, bit, zero, IR_NE);
                tier1_select_test_append(function, &next, IR_CBR, IR_VOID, odd, 1, 2);
                function->block_offsets[1] = next;
                const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, y, p1, BUSTER_IR_NONE);
                tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function->block_offsets[2] = next;
                const uint32_t difference = tier1_select_test_append(function, &next, IR_SUB, IR_I64, y, p1, BUSTER_IR_NONE);
                tier1_select_test_append(function, &next, IR_RET, IR_VOID, difference, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else
            {
                const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, y, p1, BUSTER_IR_NONE);
                tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
                function->block_count = 1;
            }
        }
        function->instruction_count = next;
        function->block_offsets[function->block_count] = next;
    }
    return created;
}

static uint64_t tier1_ties_oracle(uint64_t p0, uint64_t p1, bool branch, bool returned, bool address)
{
    const uint64_t y = (p0 - p1) ^ 0x55;
    return address ? (p0 - p1 + 8) ^ 0x55 : returned ? p0 | p1 : branch && (y & 1) == 0 ? y - p1 : y + p1;
}

// The register holding node `node`'s value at `position`, or UINT8_MAX: the CFG consumer's allocation where the function
// was its, the straight-line one's otherwise.
static uint8_t tier1_ties_register(const Tier1EmitResult* emitted, uint32_t node, uint32_t position)
{
    uint8_t reg = UINT8_MAX;
    if (emitted->general)
    {
        uint32_t location = 0;
        uint8_t kind = 0;
        const uint32_t value = emitted->node_values[node];
        if (value != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, value, position, &location, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location < 16)
        {
            reg = (uint8_t)location;
        }
    }
    else
    {
        Tier1AllocationLocation location;
        const uint32_t interval = emitted->prepared.value_intervals[node];
        if (interval != BUSTER_IR_NONE && tier1_allocation_location(&emitted->prepared.allocation, &emitted->allocation, interval, position, &location) && location.kind == BUSTER_TIER1_ALLOCATE_REGISTER)
        {
            reg = location.reg;
        }
    }
    return reg;
}

static void tier1_ties_compile(Tier1EmitTest* test, bool branch, bool returned, bool address)
{
    IrFunction function;
    const bool created = tier1_ties_function(&test->input, &function, branch, returned, address);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the ties' fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the ties' fixture selects");
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .rematerialize = true, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && emitted.general == branch, "the ties' fixture emits through its consumer");
    if (error == TIER1_EMIT_OK && emitted.general == branch)
    {
        // Node 2 is x (or h's p0 | p1) and node 4 y; the CFG consumer's positions are 2 past the straight line's where the
        // IR's entry has predecessors, which f's hasn't.
        const uint8_t p0 = tier1_ties_register(&emitted, 0, 4);
        const uint8_t x = tier1_ties_register(&emitted, 2, 5);
        if (address)
        {
            // The address folds into node 5, the ptrtoint, whose pattern is its lea from x.
            const uint8_t moved = tier1_ties_register(&emitted, 5, 11);
            tier1_emit_test_expect(test, plan.patterns[5].rule == TIER1_RULE_ADDRESS && plan.patterns[5].values[0] == 2 && x != UINT8_MAX && moved == x, "an address takes its dying base's register");
            if (moved != x && test->failures < 20)
            {
                fprintf(stderr, "  ties address: rule %u, x r%u, address r%u\n", plan.patterns[5].rule, x, moved);
            }
        }
        else if (returned)
        {
            tier1_emit_test_expect(test, x == X86_REGISTER_RAX, "a returned result whose operands end at it takes rax, the lowest free register, not its first operand's");
        }
        else
        {
            const uint8_t y = tier1_ties_register(&emitted, 4, 9);
            tier1_emit_test_expect(test, p0 != UINT8_MAX && x == p0, branch ? "the CFG consumer's difference takes its dying first operand's register" : "the straight-line consumer's difference takes its dying first operand's register");
            tier1_emit_test_expect(test, x != UINT8_MAX && y == x, branch ? "the CFG consumer's xor takes the difference's register" : "the straight-line consumer's xor takes the difference's register");
            if ((x != p0 || y != x) && test->failures < 20)
            {
                fprintf(stderr, "  ties branch=%u: p0 r%u, x r%u, y r%u\n", branch, p0, x, y);
            }
        }
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x3c6ef372fe94f82a) ^ (branch ? 2 : 0) ^ (returned ? 4 : 0) ^ (address ? 8 : 0);
            for (uint32_t sample = 0; sample < 32; sample += 1)
            {
                const uint64_t a = tier1_emit_test_random(&random);
                const uint64_t b = tier1_emit_test_random(&random);
                const uint64_t actual = tier1_dropped_call(executable, a, b);
                const uint64_t expected = tier1_ties_oracle(a, b, branch, returned, address);
                if (actual != expected && test->failures < 20)
                {
                    fprintf(stderr, "  ties branch=%u returned=%u a=%016" PRIx64 " b=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", branch, returned, a, b, actual, expected);
                }
                tier1_emit_test_expect(test, actual == expected, "a result tied to its operand computes its value");
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

static void tier1_ties_tests(Tier1EmitTest* test)
{
    tier1_ties_compile(test, true, false, false);
    tier1_ties_compile(test, false, false, false);
    tier1_ties_compile(test, false, true, false);
    tier1_ties_compile(test, false, false, true);
}
