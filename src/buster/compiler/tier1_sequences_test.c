// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Values live across fixed registers' sequences (#2767), included after tier1_ties_test.c. f(p0, p1), the CFG consumer's:
// x = p1 | 1, y = p0 ^ 0x55, c = p1 & 31, s = p0 << c, q = p0 / x (unsigned), r = s + q + x + y + c, then a branch on r's
// low bit to a return of r + p1 or of r - p1. Without the rule, x took rax, the lowest free register, and y rcx, and the
// division and the shift saved them around themselves in their call-save rows; and c, the shift's count, which it reads
// in cl and leaves there, was saved around its own shift where it was in rcx. Each range live across a sequence now
// avoids its registers (Tier1CfgInput.sequences): no value is stored around either, and no row is given for one. It runs
// natively against its oracle, by both paths, with every register and with the registers of the parameters, the
// sequences and two more alone, where the values crowd the registers the sequences take.

static bool tier1_sequences_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 32,
        .block_count = 3,
        .constant_count = 4,
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
        function->constants[2].low = 31;
        function->constants[3].low = 0;
        uint32_t next = 0;
        const uint32_t p0 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t p1 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t mask = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bits = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t x = tier1_select_test_append(function, &next, IR_OR, IR_I64, p1, one, BUSTER_IR_NONE);
        const uint32_t y = tier1_select_test_append(function, &next, IR_XOR, IR_I64, p0, mask, BUSTER_IR_NONE);
        const uint32_t c = tier1_select_test_append(function, &next, IR_AND, IR_I64, p1, bits, BUSTER_IR_NONE);
        const uint32_t s = tier1_select_test_append(function, &next, IR_SHL, IR_I64, p0, c, BUSTER_IR_NONE);
        const uint32_t q = tier1_select_test_append(function, &next, IR_UDIV, IR_I64, p0, x, BUSTER_IR_NONE);
        const uint32_t t1 = tier1_select_test_append(function, &next, IR_ADD, IR_I64, s, q, BUSTER_IR_NONE);
        const uint32_t t2 = tier1_select_test_append(function, &next, IR_ADD, IR_I64, t1, x, BUSTER_IR_NONE);
        const uint32_t t3 = tier1_select_test_append(function, &next, IR_ADD, IR_I64, t2, y, BUSTER_IR_NONE);
        const uint32_t r = tier1_select_test_append(function, &next, IR_ADD, IR_I64, t3, c, BUSTER_IR_NONE);
        const uint32_t low = tier1_select_test_append(function, &next, IR_AND, IR_I64, r, one, BUSTER_IR_NONE);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t odd = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, low, zero, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, odd, 1, 2);
        function->block_offsets[1] = next;
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, r, p1, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[2] = next;
        const uint32_t difference = tier1_select_test_append(function, &next, IR_SUB, IR_I64, r, p1, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, difference, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[function->block_count] = next;
    }
    return created;
}

static uint64_t tier1_sequences_oracle(uint64_t p0, uint64_t p1)
{
    const uint64_t x = p1 | 1;
    const uint64_t c = p1 & 31;
    const uint64_t r = (p0 << c) + p0 / x + x + (p0 ^ 0x55) + c;
    return (r & 1) != 0 ? r + p1 : r - p1;
}

// f by the CFG consumer with the general registers `registers`; with `roomy`, it must save nothing around its sequences.
static void tier1_sequences_compile(Tier1EmitTest* test, bool vector, uint32_t registers, bool roomy)
{
    IrFunction function;
    const bool created = tier1_sequences_function(&test->input, &function);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the sequences' fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the sequences' fixture selects");
    uint32_t divisions = 0;
    uint32_t shifts = 0;
    for (uint32_t n = 0; planned && n < function.instruction_count; n += 1)
    {
        divisions += plan.covered[n] == 0 && plan.patterns[n].rule == TIER1_RULE_DIVIDE;
        shifts += plan.covered[n] == 0 && plan.patterns[n].rule == TIER1_RULE_SHIFT;
    }
    tier1_emit_test_expect(test, !planned || (divisions == 1 && shifts == 1), "the fixture divides and shifts by registers");
    const Tier1EmitOptions options = { .register_mask = registers, .vector = vector, .rematerialize = true, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && emitted.general, "the sequences' fixture emits through the CFG consumer");
    if (error == TIER1_EMIT_OK && emitted.general)
    {
        tier1_emit_test_expect(test, tier1_emit_test_general_rows_used(&emitted), "a call-save row is a register some sequence stores, and no other register has one");
        tier1_emit_test_expect(test, !roomy || (emitted.call_stores == 0 && emitted.prepared.call_save_mask == 0), "no value is saved around a division or a shift, and none takes a row");
        if (roomy && emitted.call_stores != 0 && test->failures < 20)
        {
            fprintf(stderr, "  sequences vector=%u: %u stores around the sequences, rows %#x\n", vector, emitted.call_stores, emitted.prepared.call_save_mask);
        }
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x2767b7e151628aed) ^ (vector ? 1 : 0) ^ ((uint64_t)registers << 1);
            for (uint32_t sample = 0; sample < 48; sample += 1)
            {
                const uint64_t a = sample == 0 ? UINT64_MAX : tier1_emit_test_random(&random);
                const uint64_t b = sample == 1 ? 0 : sample == 2 ? UINT64_MAX : tier1_emit_test_random(&random);
                const uint64_t actual = tier1_dropped_call(executable, a, b);
                const uint64_t expected = tier1_sequences_oracle(a, b);
                if (actual != expected && test->failures < 20)
                {
                    fprintf(stderr, "  sequences vector=%u registers=%#x a=%016" PRIx64 " b=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", vector, registers, a, b, actual, expected);
                }
                tier1_emit_test_expect(test, actual == expected, "values live across a division and a shift keep their values");
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

static void tier1_sequences_tests(Tier1EmitTest* test)
{
    // The parameters' registers, the sequences' and two more.
    const uint32_t tight = (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_R8) |
                           (1u << X86_REGISTER_R9);
    for (uint32_t vector = 0; vector < 2; vector += 1)
    {
        tier1_sequences_compile(test, vector != 0, tier1_liveness_register_mask(), true);
        tier1_sequences_compile(test, vector != 0, tight, false);
    }
}
