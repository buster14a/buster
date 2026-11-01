// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent native fixtures for divisions by constants' reciprocals (#141), included after tier1_terminal_test.c:
// whole functions through tier1_emit_function, by both consumers, whose sequences take rax and rdx (tier1_select_fixed)
// while other values live past them there. The straight-line consumer takes p0 op c + p2 op' c' + p2 + p3 + p0 + p1:
// p2 arrives in rdx, where it lives past both divisions, and is itself the second's dividend. The CFG consumer takes a
// loop's digit sum, s += n op c, n = n op' c while n != 0, then s + p1, its phis' values wherever the allocation puts
// them. Each runs at both widths, by both paths, with constants rematerialized and not, and with every register and
// with the parameters' and rax alone, against an oracle of C's own division that reads only the inputs, in a bounded
// child (tier1_divide_tests).

#define BUSTER_TIER1_DIVIDE_SAMPLES 40u

typedef struct Tier1DivideCase Tier1DivideCase;
struct Tier1DivideCase
{
    uint8_t first;
    uint8_t second;
    uint64_t first_divisor;
    uint64_t second_divisor;
};

static __attribute__((no_sanitize("function"))) uint64_t tier1_divide_invoke(const void* code, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(a, b, c, d);
}

// C's division `op` of n by d at `width`: the oracle, which never consults the selector.
static uint64_t tier1_divide_oracle(uint8_t op, uint32_t width, uint64_t n, uint64_t d)
{
    uint64_t result = 0;
    if (width == 8)
    {
        const int64_t sn = (int64_t)n;
        const int64_t sd = (int64_t)d;
        result = op == IR_UDIV ? n / d : op == IR_UREM ? n % d : op == IR_SDIV ? (uint64_t)(sn / sd) : (uint64_t)(sn % sd);
    }
    else
    {
        const uint32_t un = (uint32_t)n;
        const uint32_t ud = (uint32_t)d;
        const int32_t sn = (int32_t)un;
        const int32_t sd = (int32_t)ud;
        result = op == IR_UDIV ? un / ud : op == IR_UREM ? un % ud : op == IR_SDIV ? (uint32_t)(sn / sd) : (uint32_t)(sn % sd);
    }
    return result;
}

// The loop's update of n: the second operation's division (a remainder's would stop shrinking n).
static uint8_t tier1_divide_quotient(uint8_t op)
{
    return op == IR_UREM ? IR_UDIV : op == IR_SREM ? IR_SDIV : op;
}

// A dividend's sample: its edges first (0, ±1, the sign bit and its neighbours, the divisor and its negation and
// neighbours, the largest multiple of it), then random ones, all with random bits past a 32-bit width.
static uint64_t tier1_divide_dividend(uint32_t sample, uint32_t width, uint64_t divisor, uint64_t* random)
{
    const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
    const uint64_t sign = UINT64_C(1) << (width * 8 - 1);
    const uint64_t magnitude = tier1_select_magnitude(divisor, width);
    const uint64_t largest = (mask / magnitude) * magnitude;
    const uint64_t edges[] = { 0, 1, mask, sign, sign - 1, sign + 1, divisor, divisor - 1, divisor + 1, 0 - divisor, magnitude, 0 - magnitude, largest, largest - 1, 0 - largest + 1 };
    const uint64_t junk = width == 4 ? tier1_emit_test_random(random) << 32 : 0;
    return ((sample < BUSTER_ARRAY_LENGTH(edges) ? edges[sample] : tier1_emit_test_random(random)) & mask) | junk;
}

static bool tier1_divide_straight_fixture(Tier1EmitTest* test, const Tier1DivideCase* entry, uint32_t width, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 14,
        .block_count = 1,
        .constant_count = 2,
        .parameter_count = 4,
    };
    const IrType type = width == 4 ? IR_I32 : IR_I64;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = (uint8_t)type;
        function->block_offsets[1] = 14;
        function->constants[0].low = width == 4 ? (uint32_t)entry->first_divisor : entry->first_divisor;
        function->constants[1].low = width == 4 ? (uint32_t)entry->second_divisor : entry->second_divisor;
        for (uint32_t parameter = 0; parameter < 4; parameter += 1)
        {
            function->parameter_types[parameter] = (uint8_t)type;
            tier1_emit_test_node(function, parameter, IR_PARAM, type, parameter, BUSTER_IR_NONE);
        }
        tier1_emit_test_node(function, 4, IR_CONST, type, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 5, (IrOp)entry->first, type, 0, 4);
        tier1_emit_test_node(function, 6, IR_CONST, type, 1, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 7, (IrOp)entry->second, type, 2, 6);
        tier1_emit_test_node(function, 8, IR_ADD, type, 5, 7);
        tier1_emit_test_node(function, 9, IR_ADD, type, 8, 2);
        tier1_emit_test_node(function, 10, IR_ADD, type, 9, 3);
        tier1_emit_test_node(function, 11, IR_ADD, type, 10, 0);
        tier1_emit_test_node(function, 12, IR_ADD, type, 11, 1);
        tier1_emit_test_node(function, 13, IR_RET, IR_VOID, 12, BUSTER_IR_NONE);
    }
    return ready;
}

// Block 0: n0, p1, then to the loop. Block 1: n = phi(n0, n'), s = phi(0, s'), n != 0 ? block 2 : block 3. Block 2:
// s' = s + n op c, n' = n op' c (op' the second operation's division), back. Block 3: return s + p1.
static bool tier1_divide_loop_fixture(Tier1EmitTest* test, const Tier1DivideCase* entry, uint32_t width, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 17,
        .block_count = 4,
        .constant_count = 3,
        .parameter_count = 2,
        .extra_count = 8,
    };
    const IrType type = width == 4 ? IR_I32 : IR_I64;
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        const uint32_t offsets[] = { 0, 4, 9, 15, 17 };
        const uint32_t extra[] = { 0, 0, 2, 13, 0, 2, 2, 11 };
        memcpy(function->block_offsets, offsets, sizeof(offsets));
        memcpy(function->extra, extra, sizeof(extra));
        function->result_type = (uint8_t)type;
        function->parameter_types[0] = (uint8_t)type;
        function->parameter_types[1] = (uint8_t)type;
        function->constants[0].low = 0;
        function->constants[1].low = width == 4 ? (uint32_t)entry->first_divisor : entry->first_divisor;
        function->constants[2].low = width == 4 ? (uint32_t)entry->second_divisor : entry->second_divisor;
        tier1_emit_test_node(function, 0, IR_PARAM, type, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 1, IR_PARAM, type, 1, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 2, IR_CONST, type, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 3, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 4, IR_PHI, type, 0, 2);
        tier1_emit_test_node(function, 5, IR_PHI, type, 4, 2);
        tier1_emit_test_node(function, 6, IR_CONST, type, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 7, IR_ICMP, IR_I1, 4, 6);
        function->c[7] = IR_NE;
        tier1_emit_test_node(function, 8, IR_CBR, IR_VOID, 7, 2);
        function->c[8] = 3;
        tier1_emit_test_node(function, 9, IR_CONST, type, 1, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 10, (IrOp)entry->first, type, 4, 9);
        tier1_emit_test_node(function, 11, IR_ADD, type, 5, 10);
        tier1_emit_test_node(function, 12, IR_CONST, type, 2, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 13, (IrOp)tier1_divide_quotient(entry->second), type, 4, 12);
        tier1_emit_test_node(function, 14, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 15, IR_ADD, type, 5, 1);
        tier1_emit_test_node(function, 16, IR_RET, IR_VOID, 15, BUSTER_IR_NONE);
    }
    return ready;
}

static uint64_t tier1_divide_expected(const Tier1DivideCase* entry, uint32_t width, bool loop, const uint64_t* arguments)
{
    const uint64_t mask = width == 4 ? UINT32_MAX : UINT64_MAX;
    uint64_t result = 0;
    if (loop)
    {
        uint64_t n = arguments[0] & mask;
        uint64_t s = 0;
        while (n != 0)
        {
            s = (s + tier1_divide_oracle(entry->first, width, n, entry->first_divisor)) & mask;
            n = tier1_divide_oracle(tier1_divide_quotient(entry->second), width, n, entry->second_divisor) & mask;
        }
        result = (s + arguments[1]) & mask;
    }
    else
    {
        result = tier1_divide_oracle(entry->first, width, arguments[0], entry->first_divisor) + tier1_divide_oracle(entry->second, width, arguments[2], entry->second_divisor);
        result = (result + arguments[2] + arguments[3] + arguments[0] + arguments[1]) & mask;
    }
    return result;
}

// One fixture by one path and one set of options: its divisions their reciprocals, its consumer the expected one, and
// its code run on the samples. Returns how many of the fixed registers' values it saved.
static uint32_t tier1_divide_compile(Tier1EmitTest* test, const Tier1DivideCase* entry, uint32_t width, bool loop, bool vector, bool rematerialize, uint32_t register_mask)
{
    IrFunction function;
    const bool created = loop ? tier1_divide_loop_fixture(test, entry, width, &function) : tier1_divide_straight_fixture(test, entry, width, &function);
    tier1_emit_test_expect(test, created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK, "division fixture is valid IR");
    IrDominance dominance;
    const bool dominated = created && (!loop || tier1_terminal_dominance(test, &function, vector, &dominance));
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, loop ? &dominance : 0, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "division fixture selects");
    const uint32_t first = loop ? 10 : 5;
    const uint32_t second = loop ? 13 : 7;
    tier1_emit_test_expect(test, !planned || (plan.patterns[first].rule == TIER1_RULE_DIVIDE_RECIPROCAL && plan.patterns[second].rule == TIER1_RULE_DIVIDE_RECIPROCAL), "both divisions take their reciprocals");
    const Tier1EmitOptions options = {
        .register_mask = register_mask,
        .vector = vector,
        .rematerialize = rematerialize,
    };
    Tier1EmitResult emitted;
    const uint64_t scratch_mark = test->scratch.length;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && test->scratch.length == scratch_mark, "a division by a constant's reciprocal emits through its consumer");
    if (error != TIER1_EMIT_OK && planned && test->failures < 20)
    {
        fprintf(stderr, "  division emission error=%u node=%u loop=%u width=%u vector=%u remat=%u mask=%x\n", error, node, loop, width, vector, rematerialize, register_mask);
    }
    uint32_t saved = 0;
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_test_expect(test, emitted.general == loop && emitted.fixed_count == 2, "the straight-line consumer takes its own divisions, and the CFG consumer a loop's");
        saved = emitted.call_stores;
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x6c1d9b2f5e83a470) ^ entry->first_divisor ^ (entry->second_divisor << 1);
            for (uint32_t sample = 0; sample < BUSTER_TIER1_DIVIDE_SAMPLES; sample += 1)
            {
                uint64_t arguments[4];
                arguments[0] = tier1_divide_dividend(sample, width, entry->first_divisor, &random);
                arguments[1] = tier1_emit_test_random(&random);
                arguments[2] = tier1_divide_dividend((sample + 7) % BUSTER_TIER1_DIVIDE_SAMPLES, width, entry->second_divisor, &random);
                arguments[3] = tier1_emit_test_random(&random);
                const uint64_t expected = tier1_divide_expected(entry, width, loop, arguments);
                const uint64_t actual = tier1_divide_invoke(executable, arguments[0], arguments[1], arguments[2], arguments[3]);
                if (actual != expected && test->failures < 20)
                {
                    fprintf(stderr, "  division loop=%u width=%u ops=%u,%u divisors=%016" PRIx64 ",%016" PRIx64 " arguments=%016" PRIx64 ",%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", loop, width,
                            entry->first, entry->second, entry->first_divisor, entry->second_divisor, arguments[0], arguments[2], actual, expected);
                }
                tier1_emit_test_expect(test, actual == expected, "a division by a constant's reciprocal equals C's division natively");
            }
        }
        if (executable != MAP_FAILED)
        {
            munmap(executable, bytes);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
    return saved;
}

static void tier1_divide_cases(Tier1EmitTest* test)
{
    // Unsigned and signed divisors and remainders: a multiplier of N bits and of N + 1 (7), an even divisor's pre-shift
    // (10 and 12), negative ones, and ends past an imm32 (whose remainders' multiplications take a register).
    static const Tier1DivideCase cases[] = {
        { IR_UDIV, IR_UREM, 7, 10 },
        { IR_UREM, IR_UDIV, 10, 10 },
        { IR_SDIV, IR_SREM, 0 - UINT64_C(7), 10 },
        { IR_SREM, IR_SDIV, 641, 0 - UINT64_C(10) },
        { IR_UREM, IR_SDIV, UINT64_C(0x7fffffffffffffff), 3 },
        { IR_SREM, IR_UDIV, UINT64_C(1000000000000000003), 12 },
        { IR_UDIV, IR_SREM, 0xfffffffb, 0xc0000001 },
    };
    const uint32_t all = tier1_liveness_register_mask();
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RCX);
    uint32_t straight_saves = 0;
    uint32_t loop_saves = 0;
    test->terms = 0;
    test->selects = false;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            test->width = width;
            for (uint32_t loop = 0; loop < 2; loop += 1)
            {
                // The loop's values in rax, rdx and the parameters' registers alone, where its dividend and sum must be.
                const uint32_t reduced = loop != 0 ? (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) : few;
                for (uint32_t variant = 0; variant < 8; variant += 1)
                {
                    const uint32_t saved = tier1_divide_compile(test, &cases[c], width, loop != 0, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0 ? reduced : all);
                    straight_saves += loop != 0 ? 0 : saved;
                    loop_saves += loop != 0 ? saved : 0;
                }
            }
        }
    }
    tier1_emit_test_expect(test, straight_saves != 0 && loop_saves != 0, "both consumers save values that live past a reciprocal's fixed registers");
}

// The cases in a bounded child, as tier1_call_tests runs its own: a wrong quotient can keep the loop's dividend from ever
// reaching zero (-1 / 7 as -1), which must fail this suite, not hang it. The counter page survives a normal exit.
static void tier1_divide_tests(Tier1EmitTest* test)
{
    uint64_t* report = mmap(0, 3 * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const pid_t child = report != MAP_FAILED ? fork() : -1;
    if (child == 0)
    {
        signal(SIGALRM, SIG_DFL);
        alarm(60);
        const uint64_t checks = test->checks;
        const uint64_t failures = test->failures;
        tier1_divide_cases(test);
        report[0] = test->checks - checks;
        report[1] = test->failures - failures;
        report[2] = UINT64_C(0xd1d1de0f0e5ec0de);
        fflush(0);
        _exit(report[1] != 0);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    const bool finished = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1 && report[2] == UINT64_C(0xd1d1de0f0e5ec0de);
    tier1_emit_test_expect(test, finished, "bounded division child completes without signal or timeout");
    if (finished)
    {
        test->checks += report[0];
        test->failures += report[1];
        tier1_emit_test_expect(test, WEXITSTATUS(status) == (report[1] != 0), "division child status agrees with shared test counters");
    }
    else
    {
        fprintf(stderr, "tier1 division child failed (status=%d)\n", status);
    }
    if (report != MAP_FAILED)
    {
        munmap(report, 3 * sizeof(uint64_t));
    }
}
