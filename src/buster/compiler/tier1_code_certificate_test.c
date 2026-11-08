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
//
// The frame, compiled with and without a frame pointer: the frame's rows moved below rsp, every access to them with
// them, are refused, a callee being free to write there; and with a frame pointer, rbp set after the allocation instead
// of right after its push, each epilogue's reset from it moved to match, is refused, rbp no longer being the frame
// record's (the caller's rbp at it, the return address above it) that the layout's offsets are computed for.
//
// Narrow parameters (#1212's review): z(p0, ..., p6), p0 an I8 in rdi and p6 an I16 on the stack, returns p0 + p6 zero
// extended in one branch. Its code is certified, each extension the instruction that defines its parameter from its
// caller's bytes. Then p0's `movzx edi, dil` made `mov edi, edi`, which keeps the caller's upper bits, and p6's
// `movzx r32, word [slot]` made a 32-bit load of the slot (its origin a move's, which decodes any load), which reads the
// caller's upper bytes: each is refused, a certificate that took the caller's registers and slots as canonical having
// accepted both.
//
// A conditional branch's tail, walked (tier1_cert_emitted_walk): the condition is jne, laid out `je false; the true
// edge's move; jne true`, the false block next. A move that keeps the flags (mov) reaches the true block through the
// second jump; a zero's xor between them, which writes the flags, would leave that jne reading the xor's instead of the
// condition, and the walk refuses it.
//
// A dynamic frame without calls: s(p0, ..., p6), p6 on the stack, saves the stack pointer in one branch (IR_STACK_SAVE
// alone, which makes the frame dynamic: its rows and its incoming area from rbp, and no alloca or stack restore to leave
// it uncertified). Its code is certified, p6 read from its incoming slot through rbp. With rbp set after the allocation
// instead, its epilogues' resets moved to match, p6's load at 16 past rbp and the rows below rbp are elsewhere: it is
// refused.
//
// A realigned frame (#1284's review): r(p0, p1) stores p0 into a 64-byte object aligned to 64, calls g(), and returns
// the object's value plus x = p1 + 7 and g's result, x living across the call in a callee-saved register, so that its
// prologue pushes one past rbp before its allocation and realignment. Its code is certified. Then the allocation moved before the pushes, its epilogue matched
// (its pops from where the moved pushes put the registers, and rsp reset from rbp before rbp's pop), every register and
// the return address still come back, is refused: the pushes' slots are below the allocation, among the bytes the rows
// may take once the realignment moves rsp, which the certificate keeps apart from the rows.
//
// A compare-exchange (#1059): c(p, e, d, k) compare-exchanges the i64 at p in one branch, with p, e, d and k live
// across it and rax among the four allocatable registers. Its kernel saves rax in the scratch register and restores it
// after, which gives the value in rax back: its code is certified. Then the restore made rax's move into itself, or a
// move from rcx, is refused: the value in rax is lost.

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
    // The frames whose rows were moved below rsp, the frame pointers set after the allocation; and of each, those refused.
    uint32_t lowered;
    uint32_t lowered_refused;
    uint32_t pointers;
    uint32_t pointers_refused;
    // The dynamic frame's compiles certified, those whose frame pointer was moved, and those refused.
    uint32_t saved;
    uint32_t saved_moved;
    uint32_t saved_refused;
    // The narrow parameters' compiles certified; their extensions corrupted, in a register and from a slot, and those
    // refused.
    uint32_t narrow;
    uint32_t narrow_register;
    uint32_t narrow_register_refused;
    uint32_t narrow_stack;
    uint32_t narrow_stack_refused;
    // The realigned frame's compiles certified, those whose allocation was moved before their pushes, and those refused.
    uint32_t realigned;
    uint32_t late;
    uint32_t late_refused;
    // The compare-exchange's compiles certified, those with a live value in rax across it, and its restore of rax left
    // out or taken from another register, and those refused.
    uint32_t exchange;
    uint32_t exchange_live;
    uint32_t exchange_restores;
    uint32_t exchange_restores_refused;
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

// s(p0, ..., p6), p6 on the stack: b0 computes m_k = p_k ^ p6 for k < 6 and branches on p0's low bit to b1 or b2;
// b1 saves the stack pointer and returns p6 plus every p_k and m_k; b2 returns p6 - p0.
static bool tier1_code_certificate_saved_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 48,
        .block_count = 3,
        .constant_count = 2,
        .parameter_count = 7,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        for (uint32_t p = 0; p < 7; p += 1)
        {
            function->parameter_types[p] = IR_I64;
        }
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = BUSTER_ABI_INTEGER_ARGUMENTS, .stack_bytes = BUSTER_ABI_SLOT_BYTES };
        function->constants[0].low = 1;
        function->constants[1].low = 0;
        uint32_t next = 0;
        uint32_t parameters[7];
        for (uint32_t p = 0; p < 7; p += 1)
        {
            parameters[p] = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t mixed[6];
        for (uint32_t p = 0; p < 6; p += 1)
        {
            mixed[p] = tier1_select_test_append(function, &next, IR_XOR, IR_I64, parameters[p], parameters[6], BUSTER_IR_NONE);
        }
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bit = tier1_select_test_append(function, &next, IR_AND, IR_I64, parameters[0], one, BUSTER_IR_NONE);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t odd = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, bit, zero, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, odd, 1, 2);
        function->block_offsets[1] = next;
        tier1_select_test_append(function, &next, IR_STACK_SAVE, IR_I64, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t sum = parameters[6];
        for (uint32_t p = 0; p < 6; p += 1)
        {
            sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, parameters[p], BUSTER_IR_NONE);
            sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, mixed[p], BUSTER_IR_NONE);
        }
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[2] = next;
        const uint32_t difference = tier1_select_test_append(function, &next, IR_SUB, IR_I64, parameters[6], parameters[0], BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, difference, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[3] = next;
    }
    return created;
}

// z(p0, ..., p6), p0 an I8 and p6 an I16 (on the stack), the rest I64: b0 branches on p1's low bit to b1 or b2; b1
// returns zext(p0) + zext(p6); b2 returns p1.
static bool tier1_code_certificate_narrow_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 24,
        .block_count = 3,
        .constant_count = 2,
        .parameter_count = 7,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        for (uint32_t p = 0; p < 7; p += 1)
        {
            function->parameter_types[p] = p == 0 ? IR_I8 : p == 6 ? IR_I16 : IR_I64;
        }
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = BUSTER_ABI_INTEGER_ARGUMENTS, .stack_bytes = BUSTER_ABI_SLOT_BYTES };
        function->constants[0].low = 1;
        function->constants[1].low = 0;
        uint32_t next = 0;
        uint32_t parameters[7];
        for (uint32_t p = 0; p < 7; p += 1)
        {
            parameters[p] = tier1_select_test_append(function, &next, IR_PARAM, function->parameter_types[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bit = tier1_select_test_append(function, &next, IR_AND, IR_I64, parameters[1], one, BUSTER_IR_NONE);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t odd = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, bit, zero, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, odd, 1, 2);
        function->block_offsets[1] = next;
        const uint32_t low = tier1_select_test_append(function, &next, IR_ZEXT, IR_I64, parameters[0], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t high = tier1_select_test_append(function, &next, IR_ZEXT, IR_I64, parameters[6], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, low, high, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[2] = next;
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, parameters[1], BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[3] = next;
    }
    return created;
}

// r(p0, p1): one block, p0 stored into a 64-byte object aligned to 64, x = p1 + 7, g() called, the object's value loaded
// back plus x and g's result.
static bool tier1_code_certificate_realigned_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 12,
        .block_count = 1,
        .constant_count = 1,
        .slot_count = 1,
        .call_count = 1,
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
        function->slots[0] = (IrSlot){ .size = 64, .alignment = 64, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        function->calls[0] = (IrCall){ .result_type = IR_I64 };
        function->constants[0].low = 7;
        uint32_t next = 0;
        const uint32_t p0 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t p1 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t address = tier1_select_test_append(function, &next, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_STORE, IR_VOID, address, p0, BUSTER_IR_NONE);
        const uint32_t seven = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t x = tier1_select_test_append(function, &next, IR_ADD, IR_I64, p1, seven, BUSTER_IR_NONE);
        const uint32_t called = tier1_select_test_append(function, &next, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        const uint32_t again = tier1_select_test_append(function, &next, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t loaded = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, again, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, loaded, x, BUSTER_IR_NONE);
        const uint32_t total = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, called, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, total, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[1] = next;
    }
    return created;
}

// c(p, e, d, k): b0 branches on k's low bit to b1 or b2; b1 compare-exchanges the i64 at p from e to d and returns its
// old value plus p's address, e, d and k, which live across it; b2 returns k.
static bool tier1_code_certificate_exchange_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 20,
        .block_count = 3,
        .constant_count = 2,
        .parameter_count = 4,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_PTR;
        function->parameter_types[1] = IR_I64;
        function->parameter_types[2] = IR_I64;
        function->parameter_types[3] = IR_I64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 4 };
        function->constants[0].low = 1;
        function->constants[1].low = 0;
        uint32_t next = 0;
        uint32_t parameters[4];
        for (uint32_t p = 0; p < 4; p += 1)
        {
            parameters[p] = tier1_select_test_append(function, &next, IR_PARAM, function->parameter_types[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bit = tier1_select_test_append(function, &next, IR_AND, IR_I64, parameters[3], one, BUSTER_IR_NONE);
        const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t odd = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, bit, zero, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, odd, 1, 2);
        function->block_offsets[1] = next;
        const uint32_t old = tier1_select_test_append(function, &next, IR_ATOMIC_RMW, IR_I64, parameters[0], parameters[1], parameters[2]);
        function->flags[old] = (uint8_t)(BUSTER_IR_ATOMIC | 5u << BUSTER_IR_ORDER_SHIFT | BUSTER_IR_RMW_COMPARE);
        function->alignment_log2[old] = 3;
        uint32_t sum = tier1_select_test_append(function, &next, IR_PTRTOINT, IR_I64, parameters[0], BUSTER_IR_NONE, BUSTER_IR_NONE);
        sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, old, BUSTER_IR_NONE);
        for (uint32_t p = 1; p < 4; p += 1)
        {
            sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, parameters[p], BUSTER_IR_NONE);
        }
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[2] = next;
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, parameters[3], BUSTER_IR_NONE, BUSTER_IR_NONE);
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

// Whether the code's certificate refuses the code as it is now.
static bool tier1_code_certificate_refused(Tier1EmitTest* test, const Tier1SelectPlan* plan, const Tier1EmitResult* emitted)
{
    Tier1CertEmittedStats stats = { 0 };
    return tier1_certificate_emitted_check(plan, emitted, &test->output, &test->scratch, 0, &stats) >= TIER1_CERT_EMITTED_REFUSED;
}

// The frame's rows moved `delta` bytes down, with every access of the body to them through rsp: whether the code's
// certificate refuses it, everything put back after.
static bool tier1_code_certificate_lowered(Tier1EmitTest* test, const Tier1SelectPlan* plan, Tier1EmitResult* emitted, int32_t low, int32_t high, int32_t delta)
{
    const uint32_t prologue = tier1_origin(TIER1_ORIGIN_FRAME, 0);
    const uint32_t epilogue = tier1_origin(TIER1_ORIGIN_FRAME, 1);
    bool moved[512] = { 0 };
    const uint32_t count = emitted->code.count < BUSTER_ARRAY_LENGTH(moved) ? emitted->code.count : (uint32_t)BUSTER_ARRAY_LENGTH(moved);
    for (uint32_t row = 0; row < emitted->frame.count; row += 1)
    {
        emitted->frame.offset[row] -= delta;
    }
    for (uint32_t r = 0; r < count; r += 1)
    {
        X86Instruction* record = &emitted->code.records[r];
        moved[r] = record->mode == BUSTER_X86_MEMORY_BASE && record->rm == X86_REGISTER_RSP && record->displacement >= low && record->displacement < high && emitted->origins[r] != prologue &&
                   emitted->origins[r] != epilogue;
        record->displacement -= moved[r] ? delta : 0;
    }
    const bool refused = count == emitted->code.count && tier1_code_certificate_refused(test, plan, emitted);
    for (uint32_t row = 0; row < emitted->frame.count; row += 1)
    {
        emitted->frame.offset[row] += delta;
    }
    for (uint32_t r = 0; r < count; r += 1)
    {
        emitted->code.records[r].displacement += moved[r] ? delta : 0;
    }
    return refused;
}

// The prologue's `mov rbp, rsp`, record `m` of its [0, end), moved to its end, after the pushes and the allocation that
// followed it, which leave rsp `delta` below where it was; and each epilogue's reset of rsp from rbp moved up by as
// much, so that every register and the return address still come back. Whether the code's certificate refuses it,
// everything put back after.
static bool tier1_code_certificate_pointer_moved(Tier1EmitTest* test, const Tier1SelectPlan* plan, Tier1EmitResult* emitted, uint32_t m, uint32_t end, int32_t delta)
{
    const uint32_t epilogue = tier1_origin(TIER1_ORIGIN_FRAME, 1);
    X86Instruction* records = emitted->code.records;
    const X86Instruction pointer = records[m];
    for (uint32_t r = m; r + 1 < end; r += 1)
    {
        records[r] = records[r + 1];
    }
    records[end - 1] = pointer;
    uint32_t resets = 0;
    X86Instruction saved[8];
    uint32_t at[8];
    for (uint32_t r = end; r < emitted->code.count && resets < 8; r += 1)
    {
        const X86Instruction record = records[r];
        const bool moved = emitted->origins[r] == epilogue && record.form == X86_FORM_MOV_RM64_R64 && record.mode == BUSTER_X86_REGISTER_DIRECT && record.reg == X86_REGISTER_RBP && record.rm == X86_REGISTER_RSP;
        const bool loaded = emitted->origins[r] == epilogue && record.form == X86_FORM_LEA_R64_M && record.reg == X86_REGISTER_RSP && record.rm == X86_REGISTER_RBP;
        if (moved || loaded)
        {
            saved[resets] = record;
            at[resets] = r;
            resets += 1;
            records[r] = (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (moved ? 0 : record.displacement) + delta };
        }
    }
    const bool refused = tier1_code_certificate_refused(test, plan, emitted);
    for (uint32_t k = 0; k < resets; k += 1)
    {
        records[at[k]] = saved[k];
    }
    for (uint32_t r = end - 1; r > m; r -= 1)
    {
        records[r] = records[r - 1];
    }
    records[m] = pointer;
    return refused;
}

// The realigned frame's allocation moved before its pushes (#1284's review): the prologue `push rbp; mov rbp, rsp; sub
// rsp, N; push ...; and rsp, -A`, and its return's epilogue to match: its reset from rbp N lower, so that its pops take
// each register from where the moved pushes put it, then `mov rsp, rbp` inserted before rbp's pop. Every register and the
// return address come back, but the pushes' slots are below the allocation. Into `*refused`, whether the code's
// certificate refuses it; false where the code isn't that shape (one block, its references before rbp's pop, a prologue of
// pushes past rbp's before its allocation and realignment, an epilogue whose reset from rbp precedes its pops).
static bool tier1_code_certificate_pushed_late(Tier1EmitTest* test, const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, bool* refused)
{
    const uint32_t prologue = tier1_origin(TIER1_ORIGIN_FRAME, 0);
    const uint32_t epilogue = tier1_origin(TIER1_ORIGIN_FRAME, 1);
    const X86Instruction* records = emitted->code.records;
    const uint32_t count = emitted->code.count;
    uint32_t end = 0;
    while (end < count && emitted->origins[end] == prologue)
    {
        end += 1;
    }
    // The prologue: rbp's push and set, its pushes [2, sub), its allocation, its realignment.
    const uint32_t sub = end >= 5 ? end - 2 : 0;
    const bool allocation = records[sub].form == X86_FORM_SUB_RM64_IMM8 || records[sub].form == X86_FORM_SUB_RM64_IMM32;
    const bool realignment = end >= 5 && (records[end - 1].form == X86_FORM_AND_RM64_IMM8 || records[end - 1].form == X86_FORM_AND_RM64_IMM32);
    bool shaped = emitted->cfg_input.block_count == 1 && end >= 5 && records[0].form == X86_FORM_PUSH_R64 && records[0].rm == X86_REGISTER_RBP &&
                  records[1].form == X86_FORM_MOV_RM64_R64 && allocation && realignment;
    for (uint32_t r = 2; shaped && r < sub; r += 1)
    {
        shaped = records[r].form == X86_FORM_PUSH_R64;
    }
    // The epilogue's reset of rsp from rbp, and rbp's pop after it.
    uint32_t reset = UINT32_MAX;
    uint32_t last = UINT32_MAX;
    for (uint32_t r = end; shaped && r < count; r += 1)
    {
        const bool frame = emitted->origins[r] == epilogue;
        reset = frame && reset == UINT32_MAX && records[r].form == X86_FORM_LEA_R64_M && records[r].reg == X86_REGISTER_RSP && records[r].rm == X86_REGISTER_RBP ? r : reset;
        last = frame && records[r].form == X86_FORM_POP_R64 && records[r].rm == X86_REGISTER_RBP ? r : last;
    }
    shaped = shaped && reset != UINT32_MAX && last != UINT32_MAX && reset < last;
    // Its references (the call's) all before the inserted record, which leaves their records where they are.
    for (uint32_t k = 0; shaped && k < emitted->reference_count; k += 1)
    {
        shaped = emitted->references[k].instruction < last;
    }
    X86Instruction* moved = shaped ? arena_push_array(&test->output, (uint64_t)count + 1, sizeof(X86Instruction), _Alignof(X86Instruction)) : 0;
    uint32_t* origins = shaped ? arena_push_array(&test->output, (uint64_t)count + 1, 4, 4) : 0;
    uint32_t* values = shaped ? arena_push_array(&test->output, (uint64_t)count + 1, 4, 4) : 0;
    uint32_t blocks[2] = { 0 };
    uint32_t stubs[1] = { 0 };
    shaped = shaped && moved != 0 && origins != 0 && values != 0;
    if (shaped)
    {
        // Record `last` is the inserted one; the others keep their order, those from `last` on one later.
        for (uint32_t r = 0; r <= count; r += 1)
        {
            const uint32_t from = r <= last ? r : r - 1;
            moved[r] = records[from];
            origins[r] = emitted->origins[from];
            values[r] = emitted->origin_values[from];
        }
        moved[2] = records[sub];
        for (uint32_t r = 2; r < sub; r += 1)
        {
            moved[r + 1] = records[r];
        }
        moved[reset].displacement -= records[sub].immediate;
        moved[last] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RBP, .rm = X86_REGISTER_RSP };
        origins[last] = epilogue;
        for (uint32_t b = 0; b < 2; b += 1)
        {
            blocks[b] = emitted->block_records[b] > last ? emitted->block_records[b] + 1 : emitted->block_records[b];
        }
        stubs[0] = emitted->stub_records[0] > last ? emitted->stub_records[0] + 1 : emitted->stub_records[0];
        Tier1EmitResult mutant = *emitted;
        mutant.code.records = moved;
        mutant.code.count = count + 1;
        mutant.origins = origins;
        mutant.origin_values = values;
        mutant.block_records = blocks;
        mutant.stub_records = stubs;
        *refused = tier1_code_certificate_refused(test, plan, &mutant);
    }
    return shaped;
}

// Block 0's tail `je b1; [edge 0's move]; jne b2`, b1 and b2 next, walked for edge 0 (to b2, the condition true): the
// move a mov, or a zero's xor, which writes the flags. Whether the walk reaches b2 through the move.
static bool tier1_code_certificate_walked(bool xor)
{
    X86Instruction records[5] = {
        { .form = X86_FORM_JE_REL8 },
        xor ? (X86Instruction){ .form = X86_FORM_XOR_RM32_R32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX }
            : (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX },
        { .form = X86_FORM_JNE_REL8 },
        { .form = X86_FORM_RET },
        { .form = X86_FORM_RET },
    };
    uint32_t targets[2] = { 3, 4 };
    uint32_t branch_of[5] = { 0, UINT32_MAX, 1, UINT32_MAX, UINT32_MAX };
    uint32_t origins[5] = {
        tier1_origin(TIER1_ORIGIN_CONTROL, 0),
        tier1_origin(TIER1_ORIGIN_EDGE, 0),
        tier1_origin(TIER1_ORIGIN_CONTROL, 0),
        tier1_origin(TIER1_ORIGIN_NODE, 0),
        tier1_origin(TIER1_ORIGIN_NODE, 1),
    };
    uint32_t block_records[4] = { 0, 3, 4, 5 };
    // Edge 0 to b2, the condition's true successor; edge 1 to b1.
    uint32_t successors[2] = { 2, 1 };
    Tier1EmitResult emitted = { 0 };
    emitted.code.records = records;
    emitted.code.targets = targets;
    emitted.code.count = 5;
    emitted.origins = origins;
    emitted.block_records = block_records;
    emitted.cfg_input.successors = successors;
    Tier1CertEmittedStats stats = { 0 };
    Tier1CertEmittedBuild build = {
        .emitted = &emitted,
        .stats = &stats,
        .branch_of = branch_of,
    };
    uint32_t taken = 0;
    uint32_t other = 0;
    const bool reached = tier1_cert_emitted_walk(&build, 0, 0, true, X86_FORM_JNE_REL8, true, 0, &taken) && taken == 1;
    // The false edge leaves by the first jump, before the move, either way.
    const bool left = tier1_cert_emitted_walk(&build, 0, 1, false, X86_FORM_JNE_REL8, true, 0, &other) && other == 0;
    return reached && left;
}

// The prologue's records [0, end), its `mov rbp, rsp` record `m` (UINT32_MAX for none) and how far its pushes and
// allocation after it move rsp.
static uint32_t tier1_code_certificate_prologue(const Tier1EmitResult* emitted, uint32_t* m, int32_t* delta)
{
    const uint32_t prologue = tier1_origin(TIER1_ORIGIN_FRAME, 0);
    uint32_t end = 0;
    *m = UINT32_MAX;
    *delta = 0;
    while (end < emitted->code.count && emitted->origins[end] == prologue)
    {
        const X86Instruction record = emitted->code.records[end];
        const bool set = record.form == X86_FORM_MOV_RM64_R64 && record.mode == BUSTER_X86_REGISTER_DIRECT && record.reg == X86_REGISTER_RSP && record.rm == X86_REGISTER_RBP;
        *m = set ? end : *m;
        *delta += *m != UINT32_MAX && !set ? (record.form == X86_FORM_PUSH_R64 ? 8 : record.immediate) : 0;
        end += 1;
    }
    return end;
}

static void tier1_code_certificate_compile(Tier1EmitTest* test, bool vector, bool rematerialize, bool pointer, Tier1CodeCertificateCounts* counts)
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
    const Tier1EmitOptions options = {
        .register_mask = caller_saved,
        .vector = vector,
        .rematerialize = rematerialize,
        .certify = true,
        .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
    };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.frame.frame_pointer == pointer;
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
    // The frame's rows, [low, high) from rsp, moved below rsp, where the call's callee may write them.
    int32_t low = INT32_MAX;
    int32_t high = INT32_MIN;
    for (uint32_t row = 0; certified && row < emitted.frame.count; row += 1)
    {
        low = emitted.frame.offset[row] < low ? emitted.frame.offset[row] : low;
        high = emitted.frame.offset[row] + 16 > high ? emitted.frame.offset[row] + 16 : high;
    }
    if (certified && low < high)
    {
        const bool refused = tier1_code_certificate_lowered(test, &plan, &emitted, low, high, (high + 15) & ~15);
        counts->lowered += 1;
        counts->lowered_refused += refused;
        tier1_emit_test_expect(test, refused, "a frame whose rows are below rsp fails the code's certificate: the callee may write them");
    }
    // The frame pointer set after the allocation: the prologue's records, its `mov rbp, rsp` among them, and how far rsp
    // moves after it.
    uint32_t m = UINT32_MAX;
    int32_t delta = 0;
    const uint32_t end = certified ? tier1_code_certificate_prologue(&emitted, &m, &delta) : 0;
    if (pointer && certified)
    {
        const bool movable = m != UINT32_MAX && delta != 0;
        const bool refused = movable && tier1_code_certificate_pointer_moved(test, &plan, &emitted, m, end, delta);
        counts->pointers += movable;
        counts->pointers_refused += refused;
        tier1_emit_test_expect(test, refused, "a frame pointer set after the allocation fails the code's certificate: rbp isn't the frame record's");
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// Debug definitions use the same complete code proof even when --verify is absent. Reuse the call fixture with RAX
// excluded from allocation, forcing its result into another register only after the ABI result copy completes.
// Mutate the actual copy's bytes and its origin, not a fabricated definition descriptor.
static void tier1_code_certificate_debug(Tier1EmitTest* test, bool vector)
{
    IrFunction function = { 0 };
    uint32_t call = BUSTER_IR_NONE;
    const bool created = tier1_code_certificate_function(&test->input, &function, &call);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    IrDominance dominance = { 0 };
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan = { 0 };
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    const Tier1EmitOptions options = {
        .register_mask = (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI),
        .vector = vector,
        .sources = true,
    };
    Tier1EmitResult emitted = { 0 };
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.debug_definitions != NULL;
    tier1_emit_test_expect(test, certified, "source metadata obtains a complete emitted-code proof without requesting the allocation verifier");
    uint32_t call_record = UINT32_MAX;
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        if (emitted.origins[r] == tier1_origin(TIER1_ORIGIN_NODE, call) && (emitted.code.records[r].form & BUSTER_X86_FORM_NUMBER) == X86_FORM_CALL_REL32)
        {
            call_record = r;
        }
    }
    const Tier1DebugDefinition definition = certified ? emitted.debug_definitions[call] : (Tier1DebugDefinition){ .first = UINT32_MAX, .end = UINT32_MAX };
    const bool bounded = certified && call_record < emitted.code.count && definition.first == call_record && definition.end == call_record + 2 && definition.end <= emitted.code.count;
    const X86Instruction called = bounded ? emitted.code.records[call_record] : (X86Instruction){ 0 };
    const X86Instruction copied = bounded ? emitted.code.records[call_record + 1] : (X86Instruction){ 0 };
    const bool completed = bounded && definition.bytes == 8 && definition.class == BUSTER_TIER1_ALLOCATE_GPR && definition.reg < 16 && definition.reg != X86_REGISTER_RAX &&
                           copied.form == X86_FORM_MOV_R64_RM64 && copied.mode == BUSTER_X86_REGISTER_DIRECT && copied.reg == definition.reg && copied.rm == X86_REGISTER_RAX;
    tier1_emit_test_expect(test, completed, "a scalar call result is defined only after its actual ABI result copy, never at the suspended call's PC");
    bool absent = certified;
    for (uint32_t n = 0; absent && n < function.instruction_count; n += 1)
    {
        if (function.op[n] == IR_PARAM || function.op[n] == IR_RET)
        {
            absent = emitted.debug_definitions[n].first == UINT32_MAX && emitted.debug_definitions[n].end == UINT32_MAX;
        }
    }
    tier1_emit_test_expect(test, absent, "entry parameters and returns do not acquire fabricated scalar definition kernels");
    Tier1EmitOptions plain_options = options;
    plain_options.sources = false;
    Tier1EmitResult plain = { 0 };
    const Tier1EmitError plain_error = planned ? tier1_emit_function(&plan, &plain_options, test->allowed, &test->output, &test->scratch, &plain, &node) : TIER1_EMIT_INVALID;
    const bool same = certified && plain_error == TIER1_EMIT_OK && plain.debug_definitions == NULL && plain.origins == NULL &&
                      plain.code.count == emitted.code.count && plain.code.branch_count == emitted.code.branch_count && plain.reference_count == emitted.reference_count &&
                      memcmp(plain.code.records, emitted.code.records, (uint64_t)plain.code.count * sizeof(X86Instruction)) == 0 &&
                      memcmp(plain.code.branches, emitted.code.branches, (uint64_t)plain.code.branch_count * 4) == 0 &&
                      memcmp(plain.code.targets, emitted.code.targets, (uint64_t)plain.code.branch_count * 4) == 0 &&
                      memcmp(plain.references, emitted.references, (uint64_t)plain.reference_count * sizeof(LinkReference)) == 0;
    tier1_emit_test_expect(test, same, "proving debug definitions without --verify changes no instructions, branches or symbolic references");
    bool room = completed;
    Tier1DebugDefinition* candidates = room ? ir_allocate(&test->input, function.instruction_count, sizeof(Tier1DebugDefinition), &room) : NULL;
    tier1_emit_test_expect(test, room, "the call definition fixture supplies candidate storage for real emitted-code mutations");
    for (uint32_t mutant = 0; room && mutant < 4; mutant += 1)
    {
        const uint32_t at = call_record + 1;
        const uint32_t origin = emitted.origins[at];
        switch (mutant)
        {
            case 0: emitted.code.records[at].form = X86_FORM_MOV_R32_RM32; break;
            case 1: emitted.code.records[at].rm = X86_REGISTER_RDX; break;
            case 2: emitted.origins[at] = tier1_origin(TIER1_ORIGIN_NODE, call + 1); break;
            case 3: emitted.code.records[call_record].form = X86_FORM_NOP; break;
        }
        const uint64_t output_mark = test->output.length;
        const uint64_t scratch_mark = test->scratch.length;
        Tier1CertEmittedStats stats = { 0 };
        const Tier1CertEmittedReason reason = tier1_certificate_emitted_check_debug(&plan, &emitted, &test->output, &test->scratch, candidates, NULL, NULL, &stats);
        tier1_emit_test_expect(test, reason >= TIER1_CERT_EMITTED_REFUSED && test->output.length == output_mark && test->scratch.length == scratch_mark,
                               "wrong result width, wrong value, shifted completion origin and a missing call cannot prove debug definitions");
        emitted.code.records[at] = copied;
        emitted.origins[at] = origin;
        emitted.code.records[call_record] = called;
    }
    const uint32_t position = 2 * (emitted.cfg_input.block_count - function.block_count) + 2 * call + 1;
    const uint32_t moves = tier1_origin(TIER1_ORIGIN_MOVE, position);
    bool argument_checked = false;
    for (uint32_t r = 0; room && !argument_checked && r < call_record; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        const bool argument = emitted.origins[r] == moves && record.form == X86_FORM_MOV_R64_RM64 && record.mode == BUSTER_X86_REGISTER_DIRECT &&
                              (record.reg == X86_REGISTER_RDI || record.reg == X86_REGISTER_RSI) && record.reg != record.rm;
        if (argument)
        {
            emitted.code.records[r].reg = record.rm;
            Tier1CertEmittedStats stats = { 0 };
            const Tier1CertEmittedReason reason = tier1_certificate_emitted_check_debug(&plan, &emitted, &test->output, &test->scratch, candidates, NULL, NULL, &stats);
            tier1_emit_test_expect(test, reason == TIER1_CERT_EMITTED_CHECK && candidates[call].first == definition.first && candidates[call].end == definition.end,
                                   "a matching call-result kernel is still refused when the full certificate finds a stale argument value");
            emitted.code.records[r] = record;
            argument_checked = true;
        }
    }
    tier1_emit_test_expect(test, argument_checked, "the debug definition's full value-flow refusal mutates an actual call argument move");
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_code_certificate_debug_straight(Tier1EmitTest* test, bool vector)
{
    for (uint32_t alias = 0; alias < 2; alias += 1)
    {
        const IrSizes sizes = { .instruction_count = alias ? 3 : 5, .block_count = 1, .parameter_count = alias ? 1 : 2 };
        IrFunction function = { 0 };
        bool valid = ir_create(&test->input, &sizes, &function) == IR_OK;
        if (valid)
        {
            function.result_type = IR_I64;
            function.incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = (uint8_t)sizes.parameter_count };
            function.block_offsets[1] = sizes.instruction_count;
            function.parameter_types[0] = IR_I64;
            tier1_emit_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
            if (alias)
            {
                tier1_emit_test_node(&function, 1, IR_COPY, IR_I64, 0, BUSTER_IR_NONE);
                tier1_emit_test_node(&function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
            }
            else
            {
                function.parameter_types[1] = IR_I64;
                tier1_emit_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE);
                tier1_emit_test_node(&function, 2, IR_XOR, IR_I64, 0, 1);
                tier1_emit_test_node(&function, 3, IR_SUB, IR_I64, 2, 0);
                tier1_emit_test_node(&function, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE);
            }
        }
        valid = valid && ir_verify_scalar(&function, &test->scratch, NULL).error == IR_OK;
        IrDominance dominance = { 0 };
        valid = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
        Tier1SelectPlan plan = { 0 };
        uint32_t error_node = BUSTER_IR_NONE;
        valid = valid && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &error_node) == TIER1_SELECT_OK;
        Tier1EmitOptions options = {
            .register_mask = alias ? 1u << X86_REGISTER_RDI : (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI),
            .vector = vector,
            .sources = true,
        };
        Tier1EmitResult emitted = { 0 };
        valid = valid && tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &error_node) == TIER1_EMIT_OK;
        valid = valid && !emitted.general && emitted.certified && emitted.debug_definitions != NULL && emitted.body_record == 0;
        const uint32_t value = alias ? 1 : 2;
        const Tier1DebugDefinition definition = valid ? emitted.debug_definitions[value] : (Tier1DebugDefinition){ .first = BUSTER_IR_NONE, .end = BUSTER_IR_NONE };
        const bool proven = valid && (alias ? emitted.code.node_first[1] == emitted.code.node_first[2] && definition.first == BUSTER_IR_NONE && definition.end == BUSTER_IR_NONE :
                                     definition.first == emitted.code.node_first[2] && definition.end == emitted.code.node_first[3] && definition.first < definition.end && definition.bytes == 8);
        tier1_emit_test_expect(test, proven, alias ? "an actual vanished straight-line copy retains operand ownership but invents no definition records" :
                                                  "straight-line scalar definitions have complete checked kernels and actual operand owners");
        options.sources = false;
        Tier1EmitResult plain = { 0 };
        const bool plain_ok = valid && tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &plain, &error_node) == TIER1_EMIT_OK;
        tier1_emit_test_expect(test, plain_ok && plain.debug_definitions == NULL && plain.origins == NULL && plain.code.count == emitted.code.count &&
                               memcmp(plain.code.records, emitted.code.records, (uint64_t)plain.code.count * sizeof(X86Instruction)) == 0,
                               "straight-line source proofs leave every emitted instruction unchanged");
        options.sources = true;
        if (proven && !alias)
        {
            const uint64_t mark = test->output.length;
            Tier1EmitResult candidate = emitted;
            const X86Instruction saved = emitted.code.records[definition.first];
            emitted.code.records[definition.first] = (X86Instruction){ .form = X86_FORM_NOP };
            const Tier1EmitError corrupted = tier1_debug_straight(&plan, &options, &test->output, &candidate);
            tier1_emit_test_expect(test, corrupted == TIER1_EMIT_CERTIFICATE && candidate.debug_definitions == NULL,
                                   "a wrong concrete straight-line kernel refuses every definition claim");
            emitted.code.records[definition.first] = saved;
            arena_pop_to(&test->output, mark);

            // Keep every kernel exactly regenerated, but make xor's output overwrite p0 before sub reads p0.
            // Matching destination fragments and direct-register claims cannot substitute for this operand check.
            uint8_t registers[5];
            uint8_t fragments[8];
            X86Instruction records[32];
            uint32_t origins[32];
            uint32_t first[6];
            bool matched = emitted.allocation.fragment_count <= BUSTER_ARRAY_LENGTH(fragments);
            memcpy(registers, emitted.allocation.value_registers, sizeof(registers));
            if (matched)
            {
                memcpy(fragments, emitted.allocation.registers, emitted.allocation.fragment_count);
            }
            registers[2] = registers[0];
            for (uint32_t interval = 0; matched && interval < emitted.prepared.allocation.count; interval += 1)
            {
                if (emitted.prepared.allocation.values[interval] == 2)
                {
                    fragments[emitted.allocation.fragment_offsets[interval]] = registers[2];
                }
            }
            uint32_t count = 0;
            for (uint32_t node = 0; matched && node < function.instruction_count; node += 1)
            {
                first[node] = count;
                if (!plan.covered[node])
                {
                    const Tier1SelectHoles holes = tier1_select_holes(&plan, registers, node);
                    const Tier1SelectRecords kernel = tier1_select_expand(&holes);
                    matched = !kernel.overflowed && !kernel.requested && kernel.count <= BUSTER_ARRAY_LENGTH(records) - count;
                    for (uint32_t r = 0; matched && r < kernel.count; r += 1)
                    {
                        records[count] = kernel.records[r];
                        origins[count++] = tier1_origin(TIER1_ORIGIN_NODE, node);
                    }
                }
            }
            first[5] = count;
            candidate = emitted;
            candidate.allocation.value_registers = registers;
            candidate.allocation.registers = fragments;
            candidate.code.records = records;
            candidate.code.node_first = first;
            candidate.code.count = count;
            candidate.origins = origins;
            const Tier1EmitError ownership = matched ? tier1_debug_straight(&plan, &options, &test->output, &candidate) : TIER1_EMIT_INVALID;
            tier1_emit_test_expect(test, matched && ownership == TIER1_EMIT_CERTIFICATE && candidate.debug_definitions == NULL,
                                   "exactly regenerated straight-line kernels cannot prove a live operand overwritten by another SSA value");
            arena_pop_to(&test->output, mark);
        }
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->scratch, 0);
        arena_pop_to(&test->input, 0);
    }
}

// The existing pressure/critical-edge fixture joins two different values. Its scalar phi is usable only after
// every incoming edge's actual copies and predecessor-associated renames pass the full code check.
static void tier1_code_certificate_debug_phis(Tier1EmitTest* test, bool vector)
{
    IrFunction function = { 0 };
    bool valid = tier1_dropped_function(&test->input, &function);
    if (valid)
    {
        // Keep the entry's parameter live beside the joined value. They cannot share one destination register on
        // the other incoming path, so at least one predecessor must make a real copy into the phi's carrier.
        function.extra[1] = 1;
        const uint32_t last = function.instruction_count - 1;
        tier1_emit_test_node(&function, last, IR_ADD, IR_I64, function.block_offsets[3], 1);
        tier1_emit_test_node(&function, function.instruction_count, IR_RET, IR_VOID, last, BUSTER_IR_NONE);
        function.instruction_count += 1;
        function.block_offsets[4] = function.instruction_count;
    }
    valid = valid && ir_verify_scalar(&function, &test->scratch, NULL).error == IR_OK;
    IrDominance dominance = { 0 };
    valid = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan = { 0 };
    uint32_t error_node = BUSTER_IR_NONE;
    valid = valid && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &error_node) == TIER1_SELECT_OK;
    Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector, .sources = true };
    Tier1EmitResult emitted = { 0 };
    valid = valid && tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &error_node) == TIER1_EMIT_OK;
    valid = valid && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.debug_phis != NULL &&
            emitted.cfg_input.phi_count == 1 && emitted.cfg_input.phi_operand_offsets[1] == 2;
    const uint32_t joined = valid ? function.block_offsets[3] : BUSTER_IR_NONE;
    const uint32_t block = valid ? emitted.cfg_input.block_count - function.block_count + 3 : 0;
    uint32_t lead = valid ? emitted.block_records[block] : 0;
    const uint32_t end = valid ? emitted.block_records[block + 1] : 0;
    while (valid && lead < end && tier1_cert_emitted_kind(emitted.origins[lead]) == TIER1_ORIGIN_EDGE &&
           emitted.cfg_input.successors[emitted.origins[lead] & BUSTER_TIER1_ORIGIN_PAYLOAD] == block)
    {
        lead += 1;
    }
    const Tier1DebugPhi phi = valid ? emitted.debug_phis[joined] : (Tier1DebugPhi){ .record = BUSTER_IR_NONE, .block = BUSTER_IR_NONE };
    tier1_emit_test_expect(test, valid && phi.record == lead && phi.record < end && phi.block == 3 && phi.bytes == 8 &&
                           phi.class == BUSTER_TIER1_ALLOCATE_GPR && phi.reg < 16,
                           "a scalar join receives its actual checked entry register after all predecessor copies and phi renames");
    bool absent = valid;
    for (uint32_t node = 0; absent && node < function.instruction_count; node += 1)
    {
        absent = node == joined || (emitted.debug_phis[node].record == BUSTER_IR_NONE && emitted.debug_phis[node].block == BUSTER_IR_NONE);
    }
    tier1_emit_test_expect(test, absent, "ordinary definitions and parameters do not acquire fabricated phi-entry claims");
    options.sources = false;
    Tier1EmitResult plain = { 0 };
    const bool plain_ok = valid && tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &plain, &error_node) == TIER1_EMIT_OK;
    tier1_emit_test_expect(test, plain_ok && plain.debug_phis == NULL && plain.origins == NULL && plain.code.count == emitted.code.count &&
                           plain.code.branch_count == emitted.code.branch_count &&
                           memcmp(plain.code.records, emitted.code.records, (uint64_t)plain.code.count * sizeof(X86Instruction)) == 0 &&
                           memcmp(plain.code.branches, emitted.code.branches, (uint64_t)plain.code.branch_count * 4) == 0 &&
                           memcmp(plain.code.targets, emitted.code.targets, (uint64_t)plain.code.branch_count * 4) == 0,
                           "checked phi locations change no allocated instruction or control-flow bytes");
    bool room = valid;
    Tier1DebugDefinition* definitions = room ? ir_allocate(&test->input, function.instruction_count, sizeof(Tier1DebugDefinition), &room) : NULL;
    Tier1DebugPhi* phis = room ? ir_allocate(&test->input, function.instruction_count, sizeof(Tier1DebugPhi), &room) : NULL;
    tier1_emit_test_expect(test, room, "the phi checker mutation fixture supplies bounded private candidate storage");
    if (room)
    {
        const uint64_t output_mark = test->output.length;
        const uint64_t scratch_mark = test->scratch.length;
        const uint32_t swapped[] = { emitted.cfg_input.phi_operands[1], emitted.cfg_input.phi_operands[0] };
        Tier1EmitResult candidate = emitted;
        candidate.cfg_input.phi_operands = swapped;
        Tier1CertEmittedStats stats = { 0 };
        const Tier1CertEmittedReason reason = tier1_certificate_emitted_check_debug(&plan, &candidate, &test->output, &test->scratch, definitions, phis, NULL, &stats);
        tier1_emit_test_expect(test, reason >= TIER1_CERT_EMITTED_REFUSED && test->output.length == output_mark && test->scratch.length == scratch_mark,
                               "swapping a join's predecessor values cannot certify the unchanged concrete edge copies");
        uint32_t changed = 0;
        for (uint32_t r = 0; r < emitted.code.count; r += 1)
        {
            const uint32_t origin = emitted.origins[r];
            const bool edge = tier1_cert_emitted_kind(origin) == TIER1_ORIGIN_EDGE;
            const uint32_t e = origin & BUSTER_TIER1_ORIGIN_PAYLOAD;
            const X86Instruction saved = emitted.code.records[r];
            const bool copy = (saved.form == X86_FORM_MOV_R64_RM64 || saved.form == X86_FORM_MOV_RM64_R64) && saved.mode == BUSTER_X86_REGISTER_DIRECT && saved.reg != saved.rm;
            const uint32_t destination = saved.form == X86_FORM_MOV_R64_RM64 ? saved.reg : saved.rm;
            if (edge && emitted.cfg_input.successors[e] == block && copy && destination == phi.reg)
            {
                emitted.code.records[r].reg = saved.rm;
                const Tier1CertEmittedReason missing = tier1_certificate_emitted_check_debug(&plan, &emitted, &test->output, &test->scratch, definitions, phis, NULL, &stats);
                tier1_emit_test_expect(test, missing >= TIER1_CERT_EMITTED_REFUSED && test->output.length == output_mark && test->scratch.length == scratch_mark,
                                       "omitting an actual phi edge copy cannot publish a successful join-value proof");
                emitted.code.records[r] = saved;
                changed += 1;
            }
        }
        tier1_emit_test_expect(test, changed != 0, "the phi mutation fixture exercises an actual copied predecessor");
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_code_certificate_saved(Tier1EmitTest* test, bool vector, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_saved_function(&test->input, &function);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's dynamic-frame fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's dynamic-frame fixture selects");
    // Thirteen values live into b1, more than the registers: some are spilled, and the prologue allocates their rows
    // after setting rbp.
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.frame.frame_pointer &&
                           emitted.frame.base_register == X86_REGISTER_RBP;
    tier1_emit_test_expect(test, certified, "the dynamic-frame fixture's code, its stack parameter read through rbp, passes its certificate");
    counts->saved += certified;
    uint32_t m = UINT32_MAX;
    int32_t delta = 0;
    const uint32_t end = certified ? tier1_code_certificate_prologue(&emitted, &m, &delta) : 0;
    const bool movable = m != UINT32_MAX && delta != 0;
    tier1_emit_test_expect(test, !certified || movable, "the dynamic-frame fixture's prologue allocates after setting rbp");
    const bool refused = movable && tier1_code_certificate_pointer_moved(test, &plan, &emitted, m, end, delta);
    counts->saved_moved += movable;
    counts->saved_refused += refused;
    tier1_emit_test_expect(test, !movable || refused, "a dynamic frame's pointer set after its allocation fails the code's certificate: its stack parameter's load and its rows from rbp are elsewhere");
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_code_certificate_narrow(Tier1EmitTest* test, bool vector, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_narrow_function(&test->input, &function);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's narrow-parameter fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's narrow-parameter fixture selects");
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    tier1_emit_test_expect(test, certified, "the narrow-parameter fixture's code, its extensions proved, passes its certificate");
    counts->narrow += certified;
    const uint32_t position = 2 * (emitted.cfg_input.block_count - function.block_count);
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        const bool entry = tier1_cert_emitted_kind(emitted.origins[r]) == TIER1_ORIGIN_ENTRY;
        const bool in_register = entry && record.form == X86_FORM_MOVZX_R32_RM8 && record.mode == BUSTER_X86_REGISTER_DIRECT && record.reg == X86_REGISTER_RDI && record.rm == X86_REGISTER_RDI;
        const bool from_slot = entry && record.form == X86_FORM_MOVZX_R32_RM16 && record.mode == BUSTER_X86_MEMORY_BASE && record.rm == X86_REGISTER_RSP;
        if (in_register)
        {
            // The caller's upper bits kept.
            emitted.code.records[r] = (X86Instruction){ .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RDI };
            const bool refused = tier1_code_certificate_refused(test, &plan, &emitted);
            emitted.code.records[r] = record;
            counts->narrow_register += 1;
            counts->narrow_register_refused += refused;
            tier1_emit_test_expect(test, refused, "a narrow register parameter's extension made a 32-bit move fails the code's certificate: its caller's upper bits stay");
        }
        else if (from_slot)
        {
            // A 32-bit load of the slot, decoded as a move (its origin a move's at the entry's first node).
            const uint32_t origin = emitted.origins[r];
            const uint32_t value = emitted.origin_values[r];
            emitted.code.records[r] = (X86Instruction){ .form = X86_FORM_MOV_R32_RM32, .reg = record.reg, .rm = record.rm, .mode = record.mode, .displacement = record.displacement };
            emitted.origins[r] = tier1_origin(TIER1_ORIGIN_MOVE, position);
            emitted.origin_values[r] = emitted.node_values[6];
            const bool refused = tier1_code_certificate_refused(test, &plan, &emitted);
            emitted.code.records[r] = record;
            emitted.origins[r] = origin;
            emitted.origin_values[r] = value;
            counts->narrow_stack += 1;
            counts->narrow_stack_refused += refused;
            tier1_emit_test_expect(test, refused, "a narrow stack parameter's extension made a 32-bit load fails the code's certificate: its caller's upper bytes are read");
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// The realigned frame (#1284's review): its code certified, its prologue pushing a register past rbp before its
// allocation; then that allocation moved before the pushes, its epilogue matched, refused.
static void tier1_code_certificate_realigned(Tier1EmitTest* test, bool vector, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_realigned_function(&test->input, &function);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's realigned-frame fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's realigned-frame fixture selects");
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.frame.realigned &&
                           emitted.frame.saved_registers != 0;
    tier1_emit_test_expect(test, certified, "the realigned-frame fixture's code, a register pushed past rbp, passes its certificate");
    counts->realigned += certified;
    bool refused = false;
    const bool shaped = certified && tier1_code_certificate_pushed_late(test, &plan, &emitted, &refused);
    counts->late += shaped;
    counts->late_refused += shaped && refused;
    tier1_emit_test_expect(test, !certified || (shaped && refused), "a realigned frame whose pushes follow its allocation, its epilogue matched, fails the code's certificate: their slots are among the rows' bytes");
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// The compare-exchange (#1059's TIER1_RULE_ATOMIC_RMW) saves rax in the scratch register, takes the expected value in
// it, and restores it after: the value that lives across it in rax is still there. With rax among the few allocatable
// registers, its code is certified; then the restore made rax's move into itself, or a move from another register, is
// refused where a value lives in rax across the exchange.
static void tier1_code_certificate_exchange(Tier1EmitTest* test, bool vector, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_exchange_function(&test->input, &function);
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's compare-exchange fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's compare-exchange fixture selects");
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI);
    const Tier1EmitOptions options = { .register_mask = few, .vector = vector, .certify = true };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    tier1_emit_test_expect(test, certified, "the compare-exchange fixture's code, rax given back around the exchange, passes its certificate");
    counts->exchange += certified;
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        const bool restore = tier1_cert_emitted_kind(emitted.origins[r]) == TIER1_ORIGIN_NODE && record.form == X86_FORM_MOV_R64_RM64 && record.mode == BUSTER_X86_REGISTER_DIRECT &&
                             record.reg == X86_REGISTER_RAX && record.rm == BUSTER_TIER1_SELECT_SCRATCH;
        if (restore)
        {
            const bool kept = tier1_code_certificate_refuses(test, &plan, &emitted, r, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX }, UINT32_MAX);
            const bool other = tier1_code_certificate_refuses(test, &plan, &emitted, r, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, UINT32_MAX);
            counts->exchange_live += 1;
            counts->exchange_restores += 2;
            counts->exchange_restores_refused += kept + other;
            tier1_emit_test_expect(test, kept && other, "a compare-exchange whose rax isn't given back fails the code's certificate: the value that lives in rax across it is lost");
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// A seq_cst source store remains IR_STORE and preserves a desired value live past it. Its certified kernel must retain
// both the copy into the scratch and the locked memory exchange, including when its return takes a different CFG edge.
static void tier1_code_certificate_store(Tier1EmitTest* test, bool vector)
{
    const IrSizes sizes = { .instruction_count = 9, .block_count = 3, .constant_count = 1, .parameter_count = 3 };
    IrFunction function;
    const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (created)
    {
        function.result_type = IR_I64;
        function.parameter_types[0] = IR_PTR;
        function.parameter_types[1] = IR_I64;
        function.parameter_types[2] = IR_I64;
        function.incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 3 };
        function.block_offsets[1] = 6;
        function.block_offsets[2] = 8;
        function.block_offsets[3] = 9;
        tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 2, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 3, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 4, IR_ICMP, IR_I1, 2, 3, IR_NE);
        tier1_select_test_node(&function, 5, IR_CBR, IR_VOID, 4, 1, 2);
        tier1_select_test_node(&function, 6, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        function.flags[6] = BUSTER_IR_ATOMIC | BUSTER_IR_FENCE_SEQ_CST << BUSTER_IR_ORDER_SHIFT;
        function.alignment_log2[6] = 3;
        tier1_select_test_node(&function, 7, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 8, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's seq_cst store fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selection = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
    const bool planned = selection == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's seq_cst store fixture selects");
    Tier1EmitDiagnostic diagnostic = {0};
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector, .certify = true, .diagnostic = &diagnostic };
    Tier1EmitResult emitted = {0};
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    if (!certified)
    {
        fprintf(stderr, "tier1-emit: seq_cst store certificate: path=%s selection=%u emission=%u cause=%u detail=%u node=%u checked=%u reason=%u\n",
                vector ? "vector" : "scalar", (unsigned)selection, (unsigned)error, (unsigned)diagnostic.cause, diagnostic.error, node, emitted.code_checked, emitted.code_reason);
    }
    tier1_emit_test_expect(test, certified, "a seq_cst store that keeps its desired SSA value passes the emitted-code certificate");
    uint32_t mutations = 0;
    for (uint32_t r = 1; certified && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        if (record.form == X86_FORM_XCHG_RM64_R64 && (record.mode & BUSTER_X86_MODE_MEMORY) != 0)
        {
            const X86Instruction copy = emitted.code.records[r - 1];
            const bool preserves = record.reg == BUSTER_TIER1_SELECT_SCRATCH && copy.form == X86_FORM_MOV_R64_RM64 &&
                                   copy.mode == BUSTER_X86_REGISTER_DIRECT && copy.reg == BUSTER_TIER1_SELECT_SCRATCH && copy.rm != copy.reg;
            tier1_emit_test_expect(test, preserves, "the seq_cst store copies its live desired value before exchanging the scratch");
            X86Instruction store = record;
            store.form = X86_FORM_MOV_RM64_R64;
            const bool weakened = tier1_code_certificate_refuses(test, &plan, &emitted, r, store, UINT32_MAX);
            const X86Instruction missing = { .form = X86_FORM_MOV_R64_RM64, .reg = BUSTER_TIER1_SELECT_SCRATCH, .rm = BUSTER_TIER1_SELECT_SCRATCH };
            const bool stale = tier1_code_certificate_refuses(test, &plan, &emitted, r - 1, missing, UINT32_MAX);
            tier1_emit_test_expect(test, weakened && stale, "the certificate refuses a seq_cst store weakened to mov or deprived of its desired-value copy");
            mutations += weakened + stale;
        }
    }
    tier1_emit_test_expect(test, mutations == 2, "both seq_cst store mutants were exercised and refused");
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// The producer's row owners are assertions, not evidence. Corrupt them without
// touching the allocation, and corrupt the predecessor's store without changing
// its metadata: both must be refused even though the register assertion remains.
static void tier1_code_certificate_carry(Tier1EmitTest* test, bool vector, bool pointer, bool aggregate)
{
    Tier1CallFixture fixture = { 0 };
    const bool created = tier1_call_carry_fixture(test, &fixture, TIER1_CALL_CARRY);
    // The first call takes a two-eightbyte aggregate through its third parameter's pointer. The ABI metadata is
    // borrowed for this fixture's lifetime. The second call and the fourth parameter's carried row are unchanged.
    IrPassed passed = {
        .location = {
            .registers = { BUSTER_ABI_RDI, BUSTER_ABI_RSI },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
        .size = 16,
    };
    IrAbiCall abi = {
        .call = {
            .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
            .integer_registers = 2,
            .result = {
                .kind = BUSTER_ABI_RESULT_REGISTERS,
                .part_bytes = 8,
                .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
            },
        },
    };
    if (created && aggregate)
    {
        fixture.function.type[2] = IR_PTR;
        fixture.function.parameter_types[2] = IR_PTR;
        fixture.function.call_types[0] = IR_PTR;
        fixture.function.calls[0].abi = 1;
        fixture.function.abi_calls = &abi;
        fixture.function.abi_call_count = 1;
        fixture.function.passed = &passed;
        fixture.function.passed_count = 1;
    }
    const IrDominance dominance = {
        .enter = fixture.dominance_enter,
        .leave = fixture.dominance_leave,
        .block_count = created ? fixture.function.block_count : 0,
    };
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = created && tier1_select_plan(&fixture.function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
        .vector = vector,
        .certify = true,
    };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.entry_rows != 0;
    tier1_emit_test_expect(test, certified, "a carried call-save row and its register are independently certified");
    if (certified && aggregate)
    {
        // Inspect the published certificate, rather than only the ABI input, to ensure the aggregate's parts actually
        // entered the same certificate as the carried row. The reserved parameter/result pseudo-values are separate.
        const uint64_t saved = test->output.length;
        Tier1CertProgram program = { 0 };
        Tier1CertEmittedStats stats = { 0 };
        const Tier1CertEmittedReason reason = tier1_certificate_emitted(&plan, &emitted, &test->output, &test->scratch, &program, &stats);
        const uint64_t reserved = (uint64_t)emitted.cfg_input.value_count + BUSTER_CERT_EMITTED_PSEUDO + fixture.function.parameter_count +
                                  2 * ((uint64_t)fixture.function.parameter_count + 1) + 2;
        tier1_emit_test_expect(test, reason == TIER1_CERT_EMITTED_OK && program.value_count > reserved, "the carried-row certificate includes by-value aggregate parts");
        arena_pop_to(&test->output, saved);
    }
    uint32_t rows = 0;
    uint32_t stores = 0;
    for (uint32_t b = 1; certified && b < emitted.cfg_input.block_count; b += 1)
    {
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            uint32_t* const row = emitted.entry_rows + (uint64_t)b * BUSTER_TIER1_EMIT_GENERAL_REGISTERS + reg;
            const uint32_t owner = *row;
            if (owner != BUSTER_IR_NONE)
            {
                rows += 1;
                *row = owner == 3 ? 2 : 3;
                tier1_emit_test_expect(test, tier1_code_certificate_refused(test, &plan, &emitted), "a carried row naming another parameter is refused");
                *row = fixture.function.instruction_count;
                tier1_emit_test_expect(test, tier1_code_certificate_refused(test, &plan, &emitted), "a carried row's out-of-range owner is refused before indexing");
                *row = owner;
                const X86Instruction save = tier1_emit_call_save(&emitted, reg, false);
                for (uint32_t r = 0; r < emitted.block_records[b]; r += 1)
                {
                    const X86Instruction record = emitted.code.records[r];
                    if (record.form == save.form && record.mode == save.mode && record.rm == save.rm && record.displacement == save.displacement)
                    {
                        X86Instruction wrong = record;
                        wrong.reg = (uint8_t)(record.reg == X86_REGISTER_RAX ? X86_REGISTER_RCX : X86_REGISTER_RAX);
                        const bool refused = tier1_code_certificate_refuses(test, &plan, &emitted, r, wrong, UINT32_MAX);
                        stores += 1;
                        tier1_emit_test_expect(test, refused, "a carried row whose predecessor stored another register is refused");
                    }
                }
            }
        }
    }
    if (certified)
    {
        uint32_t* const saved = emitted.entry_rows;
        emitted.entry_rows = 0;
        tier1_emit_test_expect(test, tier1_code_certificate_refused(test, &plan, &emitted), "omitting carried-row assertions cannot certify a later reload");
        emitted.entry_rows = saved;
    }
    tier1_emit_test_expect(test, rows != 0 && stores != 0, "carried-row mutants cover actual saved rows, not an empty metadata table");
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_code_certificate_tests(Tier1EmitTest* test)
{
    Tier1CodeCertificateCounts counts = { 0 };
    for (uint32_t variant = 0; variant < 8; variant += 1)
    {
        tier1_code_certificate_carry(test, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0);
    }
    for (uint32_t variant = 0; variant < 8; variant += 1)
    {
        tier1_code_certificate_compile(test, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0, &counts);
    }
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        tier1_code_certificate_debug(test, variant != 0);
        tier1_code_certificate_debug_straight(test, variant != 0);
        tier1_code_certificate_debug_phis(test, variant != 0);
        tier1_code_certificate_saved(test, variant != 0, &counts);
    }
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        tier1_code_certificate_narrow(test, variant != 0, &counts);
    }
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        tier1_code_certificate_exchange(test, variant != 0, &counts);
        tier1_code_certificate_store(test, variant != 0);
    }
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        tier1_code_certificate_realigned(test, variant != 0, &counts);
    }
    tier1_emit_test_expect(test, counts.realigned == 2 && counts.late == 2, "the realigned-frame fixture's compiles push a register past rbp, and their allocation moves before it");
    tier1_emit_test_expect(test, counts.narrow == 2 && counts.narrow_register == 2 && counts.narrow_stack == 2, "the narrow-parameter fixture's compiles have an extension in a register and one from a slot");
    tier1_emit_test_expect(test, counts.exchange == 2 && counts.exchange_live == 2, "the compare-exchange fixture's compiles each restore rax around the exchange");
    const bool kept = tier1_code_certificate_walked(false);
    const bool clobbered = tier1_code_certificate_walked(true);
    tier1_emit_test_expect(test, kept, "a conditional tail whose edge move keeps the flags reaches its true successor");
    tier1_emit_test_expect(test, !clobbered, "a conditional jump after an edge move that writes the flags fails the code's certificate: it reads the move's flags");
    tier1_emit_test_expect(test, counts.certified == 8 && counts.second != 0 && counts.saves != 0 && counts.untouched != 0 && counts.lowered == 8 && counts.pointers == 4 && counts.saved_moved == 2,
                           "the fixtures' compiles have a second argument's move, saves and restores to corrupt, one of a register only the callee may change, rows and frame pointers to move");
    printf("tier1 code certificate: %u compiles certified; argument moves stale %u of %u refused (into rsi %u of %u); saves and restores left out %u of %u refused; a store and load around the call left out %u of %u refused; "
           "rows below rsp %u of %u refused; frame pointers set after the allocation %u of %u refused; dynamic frames %u of 2 certified, their frame pointers set after the allocation %u of %u refused; a conditional jump after an edge's "
           "flag-writing move %s; narrow parameters %u of 2 certified, their extensions corrupted in a register %u of %u and from a slot %u of %u refused; compare-exchanges %u of 2 certified, "
           "their restores of rax corrupted %u of %u refused; realigned frames %u of 2 certified, their allocation moved before their pushes %u of %u refused\n",
           counts.certified, counts.arguments_refused, counts.arguments, counts.second_refused, counts.second, counts.saves_refused, counts.saves, counts.untouched_refused, counts.untouched, counts.lowered_refused,
           counts.lowered, counts.pointers_refused, counts.pointers, counts.saved, counts.saved_refused, counts.saved_moved, kept && !clobbered ? "refused" : "accepted", counts.narrow,
           counts.narrow_register_refused, counts.narrow_register, counts.narrow_stack_refused, counts.narrow_stack, counts.exchange, counts.exchange_restores_refused, counts.exchange_restores, counts.realigned,
           counts.late_refused, counts.late);
}
