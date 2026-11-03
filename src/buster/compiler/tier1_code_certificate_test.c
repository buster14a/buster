// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #939's certificate of the CFG consumer's code (tier1_certificate_emitted.c) on a call, included after
// tier1_dropped_test.c: a call that passes one value twice is proved at both of its argument registers, and a value that
// lives across a call in a caller-saved register is proved through its save and restore, the callee taking every
// caller-saved register. f(p0, p1): b0 branches on p0's low bit to b1 or b2; b1 calls g(v, v), v = p1 + 7, and returns
// its result plus h()'s, p0, u = p0 ^ p1 and w = p0 - p1, which live across the calls (h's takes no arguments, so that
// its records write no argument register a value lives in across it); b2 returns p1. Emitted by both paths with the caller-saved registers alone
// allocatable, its code is certified. Then each of the call's moves into an argument
// register made a move of its source into itself (the argument register left as it was), each of its saves and restores
// made a register's move into itself (left out), and each store before the call with its load after it together, of a
// register nothing between them writes but the callee, is refused, one at a time: that value is lost unless the call
// kills every caller-saved register.

typedef struct Tier1CodeCertificateCounts Tier1CodeCertificateCounts;
struct Tier1CodeCertificateCounts
{
    uint32_t certified;
    // The argument moves corrupted, those into rsi (the second placement of the value passed twice) among them, and the
    // saves and restores; and of each, those refused.
    uint32_t arguments;
    uint32_t second;
    uint32_t saves;
    uint32_t arguments_refused;
    uint32_t second_refused;
    uint32_t saves_refused;
    // A store before the call and its load after it left out together, of a register nothing between them writes but the
    // callee; and those refused.
    uint32_t untouched;
    uint32_t untouched_refused;
};

static bool tier1_code_certificate_function(Arena* input, IrFunction* function, uint32_t* call)
{
    const IrSizes sizes = {
        .instruction_count = 40,
        .block_count = 3,
        .extra_count = 2,
        .constant_count = 3,
        .call_count = 2,
        .call_type_count = 2,
        .parameter_count = 2,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        function->symbol_count = 2;
        function->parameter_types[0] = IR_I64;
        function->parameter_types[1] = IR_I64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 2 };
        function->constants[0].low = 1;
        function->constants[1].low = 0;
        function->constants[2].low = 7;
        uint32_t next = 0;
        const uint32_t p0 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t p1 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bit = tier1_select_test_append(function, &next, IR_AND, IR_I64, p0, one, BUSTER_IR_NONE);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t odd = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, bit, zero, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, odd, 1, 2);
        function->block_offsets[1] = next;
        const uint32_t seven = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t v = tier1_select_test_append(function, &next, IR_ADD, IR_I64, p1, seven, BUSTER_IR_NONE);
        const uint32_t u = tier1_select_test_append(function, &next, IR_XOR, IR_I64, p0, p1, BUSTER_IR_NONE);
        const uint32_t w = tier1_select_test_append(function, &next, IR_SUB, IR_I64, p0, p1, BUSTER_IR_NONE);
        function->extra[0] = v;
        function->extra[1] = v;
        function->calls[0] = (IrCall){
            .argument_first = 0,
            .argument_count = 2,
            .type_first = 0,
            .parameter_count = 2,
            .result_type = IR_I64,
        };
        function->call_types[0] = IR_I64;
        function->call_types[1] = IR_I64;
        *call = tier1_select_test_append(function, &next, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        function->calls[1] = (IrCall){
            .argument_first = 2,
            .type_first = 2,
            .result_type = IR_I64,
        };
        const uint32_t empty = tier1_select_test_append(function, &next, IR_CALL, IR_I64, 1, 1, BUSTER_IR_NONE);
        const uint32_t both = tier1_select_test_append(function, &next, IR_ADD, IR_I64, *call, empty, BUSTER_IR_NONE);
        const uint32_t partial = tier1_select_test_append(function, &next, IR_ADD, IR_I64, both, p0, BUSTER_IR_NONE);
        const uint32_t more = tier1_select_test_append(function, &next, IR_ADD, IR_I64, partial, u, BUSTER_IR_NONE);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, more, w, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[2] = next;
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, p1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[3] = next;
    }
    return created;
}

// Records `r` and `s` (UINT32_MAX for none) replaced by their registers' moves into themselves, or `r` by `mutant`,
// checked, and put back: whether the code's certificate refused it.
static bool tier1_code_certificate_refuses(Tier1EmitTest* test, const Tier1SelectPlan* plan, Tier1EmitResult* emitted, uint32_t r, X86Instruction mutant, uint32_t s)
{
    const X86Instruction saved = emitted->code.records[r];
    const X86Instruction other = s != UINT32_MAX ? emitted->code.records[s] : saved;
    emitted->code.records[r] = mutant;
    if (s != UINT32_MAX)
    {
        emitted->code.records[s] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = other.reg, .rm = other.reg };
    }
    Tier1CertEmittedStats stats = { 0 };
    const Tier1CertEmittedReason reason = tier1_certificate_emitted_check(plan, emitted, &test->output, &test->scratch, 0, &stats);
    emitted->code.records[r] = saved;
    if (s != UINT32_MAX)
    {
        emitted->code.records[s] = other;
    }
    return reason >= TIER1_CERT_EMITTED_REFUSED;
}

static void tier1_code_certificate_compile(Tier1EmitTest* test, bool vector, bool rematerialize, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    uint32_t call = BUSTER_IR_NONE;
    const bool created = tier1_code_certificate_function(&test->input, &function, &call);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's call fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's call fixture selects");
    // The caller-saved registers alone: p0 lives across the call in one, saved and restored around it.
    const uint32_t caller_saved = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI);
    const Tier1EmitOptions options = { .register_mask = caller_saved, .vector = vector, .rematerialize = rematerialize, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    tier1_emit_test_expect(test, certified, "the call fixture's code, through the CFG consumer, passes its certificate");
    counts->certified += certified;
    // The call's own moves are marked at its definition phase: its arguments' moves, its saves and its restores.
    const uint32_t position = 2 * (emitted.cfg_input.block_count - function.block_count) + 2 * call + 1;
    const uint32_t moves = tier1_origin(TIER1_ORIGIN_MOVE, position);
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        const bool direct = record.mode == BUSTER_X86_REGISTER_DIRECT;
        const bool argument = emitted.origins[r] == moves && record.form == X86_FORM_MOV_R64_RM64 && direct && (record.reg == X86_REGISTER_RDI || record.reg == X86_REGISTER_RSI) && record.rm != record.reg;
        const bool save = emitted.origins[r] == moves && record.mode == BUSTER_X86_MEMORY_BASE && (record.form == X86_FORM_MOV_RM64_R64 || record.form == X86_FORM_MOV_R64_RM64);
        if (argument)
        {
            // The argument register left as it was: its source moved into itself.
            const bool refused = tier1_code_certificate_refuses(test, &plan, &emitted, r, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = record.rm, .rm = record.rm }, UINT32_MAX);
            counts->arguments += 1;
            counts->arguments_refused += refused;
            counts->second += record.reg == X86_REGISTER_RSI;
            counts->second_refused += record.reg == X86_REGISTER_RSI && refused;
            tier1_emit_test_expect(test, refused, "a call's argument register left stale fails the code's certificate");
        }
        else if (save)
        {
            // The save or restore left out: its register moved into itself.
            const bool refused = tier1_code_certificate_refuses(test, &plan, &emitted, r, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = record.reg, .rm = record.reg }, UINT32_MAX);
            counts->saves += 1;
            counts->saves_refused += refused;
            tier1_emit_test_expect(test, refused, "a save or restore around a call left out fails the code's certificate");
        }
    }
    // A store before the call and the load of its register back from its slot after it, the register written by
    // nothing between them but the callee (the allocation's spill around the call, or a save and its restore): both
    // left out, the value is lost unless the call kills every caller-saved register.
    // A store into a slot and the load of its register back from it, with a call between them and nothing else writing
    // the register (the allocation's spill around a call, or a save and its restore): both left out, the value is lost
    // unless the call kills every caller-saved register.
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        const X86Instruction store = emitted.code.records[r];
        const bool stored = store.form == X86_FORM_MOV_RM64_R64 && store.mode == BUSTER_X86_MEMORY_BASE;
        uint32_t load = UINT32_MAX;
        bool written = false;
        bool calls = false;
        for (uint32_t k = r + 1; stored && load == UINT32_MAX && !written && k < emitted.code.count; k += 1)
        {
            const X86Instruction later = emitted.code.records[k];
            load = later.form == X86_FORM_MOV_R64_RM64 && later.mode == BUSTER_X86_MEMORY_BASE && later.reg == store.reg && later.rm == store.rm && later.displacement == store.displacement ? k : UINT32_MAX;
            uint64_t read = 0;
            uint64_t writes = 0;
            x86_instruction_effects_scalar(&later, &read, &writes);
            calls |= (writes & BUSTER_X86_RESOURCE_CALL) != 0;
            written = load == UINT32_MAX && (writes & BUSTER_X86_RESOURCE_GPR(store.reg)) != 0;
        }
        if (load != UINT32_MAX && calls)
        {
            const bool refused = tier1_code_certificate_refuses(test, &plan, &emitted, r, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = store.reg, .rm = store.reg }, load);
            counts->untouched += 1;
            counts->untouched_refused += refused;
            tier1_emit_test_expect(test, refused, "a value's store before a call and its load after it left out fail the code's certificate: the callee may change its register");
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_code_certificate_tests(Tier1EmitTest* test)
{
    Tier1CodeCertificateCounts counts = { 0 };
    for (uint32_t variant = 0; variant < 4; variant += 1)
    {
        tier1_code_certificate_compile(test, (variant & 1) != 0, (variant & 2) != 0, &counts);
    }
    tier1_emit_test_expect(test, counts.certified == 4 && counts.second != 0 && counts.saves != 0 && counts.untouched != 0,
                           "the call fixture's compiles have a second argument's move, saves and restores to corrupt, one of a register only the callee may change");
    printf("tier1 code certificate: %u compiles certified; argument moves stale %u of %u refused (into rsi %u of %u); saves and restores left out %u of %u refused; a store and load around the call left out %u of %u refused\n",
           counts.certified, counts.arguments_refused, counts.arguments, counts.second_refused, counts.second, counts.saves_refused, counts.saves, counts.untouched_refused, counts.untouched);
}
