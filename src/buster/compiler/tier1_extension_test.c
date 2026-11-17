// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An extension to I64 whose operand's register already holds its value is that operand (#2843), included after
// tier1_ties_test.c. f(p, x): v = *p, a narrow load; t = v > 5, signed for a sign extension and unsigned for a zero one,
// a byte's or a word's compared as C does, extended to I32 first; w = v extended to I64; return (t ? w : 1) + x, in one
// block (the straight-line consumer's), or, the CFG consumer's, where x is nonzero, x itself where it is zero. The load
// has two uses, the comparison and the extension, as #2838's arms leave `s += p[i] > 0 ? p[i] : 1`. Where the load's storage
// holds the extension's value (tier1_select_canonical: a signed I32's eight bytes, a zero-extended narrow value's or
// I32's 32-bit write), the select reads the load, nothing computes the extension, and its copy into a register of its
// own is gone; a word's sign extension, whose storage is zero past its 16 bits, stays an extension of its own. Where the
// comparison reads x instead, the load's one use is the extension, which takes it as its movsx's memory operand in
// its block (in another, the load dies at the extension's copy), and the select reads the extension. Both paths plan
// alike, the CFG consumer's code certifies, and it runs natively against the oracle on each type's boundaries and random
// words, by both consumers.

typedef struct Tier1ExtensionCase Tier1ExtensionCase;
struct Tier1ExtensionCase
{
    // The load's type, and IR_SEXT or IR_ZEXT.
    uint8_t type;
    uint8_t extension;
    // Whether the comparison reads x rather than the load.
    bool alone;
    // Whether the select reads the load for its extension.
    bool forwards;
};

static const Tier1ExtensionCase tier1_extension_cases[] = {
    { .type = IR_I32, .extension = IR_SEXT, .alone = false, .forwards = true },
    { .type = IR_I32, .extension = IR_ZEXT, .alone = false, .forwards = true },
    { .type = IR_I16, .extension = IR_ZEXT, .alone = false, .forwards = true },
    { .type = IR_I8, .extension = IR_ZEXT, .alone = false, .forwards = true },
    { .type = IR_I16, .extension = IR_SEXT, .alone = false, .forwards = false },
    { .type = IR_I32, .extension = IR_SEXT, .alone = true, .forwards = false },
};

// The fixture's nodes the checks read.
typedef struct Tier1ExtensionNodes Tier1ExtensionNodes;
struct Tier1ExtensionNodes
{
    uint32_t load;
    uint32_t extended;
    uint32_t select;
};

static bool tier1_extension_function(Arena* input, IrFunction* function, const Tier1ExtensionCase* shape, bool branch, Tier1ExtensionNodes* nodes)
{
    const IrSizes sizes = {
        .instruction_count = 20,
        .block_count = 3,
        .constant_count = 3,
        .parameter_count = 2,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_PTR;
        function->parameter_types[1] = IR_I64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 2 };
        function->constants[0].low = 5;
        function->constants[1].low = 1;
        function->constants[2].low = 0;
        uint32_t next = 0;
        const uint32_t p = tier1_select_test_append(function, &next, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t x = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        nodes->load = tier1_select_test_append(function, &next, IR_LOAD, shape->type, p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const bool promoted = !shape->alone && shape->type != IR_I32;
        const uint32_t promotion = promoted ? tier1_select_test_append(function, &next, shape->extension, IR_I32, nodes->load, BUSTER_IR_NONE, BUSTER_IR_NONE) : nodes->load;
        const uint32_t bound = tier1_select_test_append(function, &next, IR_CONST, shape->alone ? IR_I64 : IR_I32, shape->alone ? 2 : 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint8_t predicate = shape->extension == IR_SEXT || shape->alone ? IR_GT : IR_UGT;
        const uint32_t taken = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, shape->alone ? x : promotion, bound, predicate);
        if (branch)
        {
            const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t nonzero = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, x, zero, IR_NE);
            tier1_select_test_append(function, &next, IR_CBR, IR_VOID, nonzero, 1, 2);
            function->block_offsets[1] = next;
        }
        nodes->extended = tier1_select_test_append(function, &next, shape->extension, IR_I64, nodes->load, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        nodes->select = tier1_select_test_append(function, &next, IR_SELECT, IR_I64, taken, nodes->extended, one);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, nodes->select, x, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (branch)
        {
            function->block_offsets[2] = next;
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, x, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        function->block_count = branch ? 3 : 1;
        function->instruction_count = next;
        function->block_offsets[function->block_count] = next;
    }
    return created;
}

static uint64_t tier1_extension_oracle(const Tier1ExtensionCase* shape, bool branch, uint64_t word, uint64_t x)
{
    const uint32_t bits = shape->type == IR_I8 ? 8 : shape->type == IR_I16 ? 16 : 32;
    const uint64_t narrow = word & ((UINT64_C(1) << bits) - 1);
    const uint64_t sign = UINT64_C(1) << (bits - 1);
    const uint64_t extended = shape->extension == IR_SEXT ? (narrow ^ sign) - sign : narrow;
    const bool taken = shape->alone ? (int64_t)x > 0 : shape->extension == IR_SEXT ? (int64_t)extended > 5 : narrow > 5;
    return branch && x == 0 ? 0 : (taken ? extended : 1) + x;
}

// Whether the pattern of `node` reads `value`.
static bool tier1_extension_reads(const Tier1SelectPlan* plan, uint32_t node, uint32_t value)
{
    bool reads = false;
    for (uint32_t k = 0; k < BUSTER_TIER1_SELECT_VALUES; k += 1)
    {
        reads |= plan->patterns[node].values[k] == value;
    }
    return reads;
}

static void tier1_extension_compile(Tier1EmitTest* test, const Tier1ExtensionCase* shape, bool vector, bool branch)
{
    IrFunction function;
    Tier1ExtensionNodes nodes = { 0 };
    const bool created = tier1_extension_function(&test->input, &function, shape, branch, &nodes);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the extensions' fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the extensions' fixture selects");
    tier1_emit_test_expect(test, planned && tier1_select_test_plan_twin(&function, &dominance, &plan, &test->output, &test->scratch), "both selection paths plan the extensions' fixture alike");
    if (planned)
    {
        const bool forwarded = plan.covered[nodes.extended] && !plan.covered[nodes.load] && tier1_extension_reads(&plan, nodes.select, nodes.load) &&
                               !tier1_extension_reads(&plan, nodes.select, nodes.extended);
        const bool own = !plan.covered[nodes.extended] && tier1_extension_reads(&plan, nodes.select, nodes.extended);
        const bool folded = own && plan.covered[nodes.load] && plan.patterns[nodes.extended].rule == TIER1_RULE_LOAD_ADDRESS;
        tier1_emit_test_expect(test, shape->forwards ? forwarded : own, shape->forwards ? "the select reads the load whose register holds its extension" : "the select reads an extension of its own");
        tier1_emit_test_expect(test, !shape->alone || branch || folded, "a load the extension alone reads in its block is its memory operand");
        if ((shape->forwards ? !forwarded : !own) && test->failures < 20)
        {
            fprintf(stderr, "  extensions type=%u extension=%u alone=%u vector=%u branch=%u: covered load %u extension %u, select reads %u %u %u\n", shape->type, shape->extension, shape->alone, vector, branch,
                    plan.covered[nodes.load], plan.covered[nodes.extended], plan.patterns[nodes.select].values[0], plan.patterns[nodes.select].values[1], plan.patterns[nodes.select].values[2]);
        }
    }
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector, .rematerialize = true, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    // The CFG consumer certifies its code (Tier1EmitResult.code_checked); the straight-line one has none to check.
    const bool certified = error == TIER1_EMIT_OK && emitted.general == branch && (!branch || (emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK));
    tier1_emit_test_expect(test, certified, "the extensions' fixture emits through its consumer, the CFG one's code certified");
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (certified && tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
    {
        static const uint64_t boundaries[] = {
            0, 1, 5, 6, 0x7f, 0x80, 0xff, 0x105, 0x7fff, 0x8000, 0x8005, 0xffff, 0x7fffffff, 0x80000000, 0x80000006, 0xffffffff, UINT64_C(0xffffffff00000006),
        };
        uint64_t random = UINT64_C(0x6a09e667f3bcc909) ^ shape->type ^ (uint64_t)shape->extension << 8 ^ (vector ? 1 : 0) ^ (branch ? 2 : 0) ^ (shape->alone ? 4 : 0);
        const uint32_t samples = BUSTER_ARRAY_LENGTH(boundaries) + 32;
        for (uint32_t sample = 0; sample < samples; sample += 1)
        {
            const uint64_t word = sample < BUSTER_ARRAY_LENGTH(boundaries) ? boundaries[sample] : tier1_emit_test_random(&random);
            const uint64_t x = sample % 3 == 0 ? (uint64_t)-(int64_t)(sample / 2) : tier1_emit_test_random(&random);
            uint64_t cell = word;
            const uint64_t actual = tier1_dropped_call(executable, (uint64_t)(uintptr_t)&cell, x);
            const uint64_t expected = tier1_extension_oracle(shape, branch, word, x);
            if (actual != expected && test->failures < 20)
            {
                fprintf(stderr, "  extensions type=%u extension=%u alone=%u vector=%u branch=%u word=%016" PRIx64 " x=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", shape->type, shape->extension, shape->alone, vector,
                        branch, word, x, actual, expected);
            }
            tier1_emit_test_expect(test, actual == expected && cell == word, "the load, its comparison and its extension compute C's value");
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_extension_tests(Tier1EmitTest* test)
{
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(tier1_extension_cases); c += 1)
    {
        for (uint32_t variant = 0; variant < 4; variant += 1)
        {
            tier1_extension_compile(test, &tier1_extension_cases[c], (variant & 1) != 0, (variant & 2) != 0);
        }
    }
}
