// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #939's certificate of the CFG consumer's code (tier1_certificate_emitted.c) on a call, included after
// tier1_dropped_test.c: a call that passes one value twice is proved at both of its argument registers, and a value that
// lives across a call in a caller-saved register is proved through its save and restore, the callee taking every
// caller-saved register. f(p0, p1): b0 branches on p0's low bit to b1 or b2; b1 calls g(v, v), v = p1 + 7, and returns
// its result plus h()'s, p0, u = p0 ^ p1 and w = p0 - p1, which live across the calls (h's takes no arguments, so that
// its records write no argument register a value lives in across it); b2 returns p1. Emitted with the caller-saved registers alone
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
//
// A full barrier in an asm body (#2963): b(p0, p1) calls g(), then runs a body of one record, musl's a_store's `lock or
// dword [rsp], 0`, which touches the four allocatable registers, and returns g's result plus p0 and p1, which live across
// both: p1 in its call-save row across the body, the others in callee-saved registers. Its code is certified. Then the
// barrier aimed at each row a restore after it loads, its template's record and the emitted one alike: a write that leaves the row as it was (an add, or, sub or xor of zero, an
// and of every bit, locked or not, 4 or 8 bytes wide) is certified, and one that changes it (an or of 1, an and of
// 0x7fffffff, an adc of 0, which adds the carry, a mov of 0, an or of a register) is refused, the restore reading bytes
// the barrier wrote.
//
// A frame address recomputed at its base itself (#3149), rsp or, under a frame pointer, rbp: the base's register copy,
// `mov rbx, base`, is its recipe's copy and is certified, the checker finding the value in rbx; the same record for a
// recipe 16 bytes from the base, a lea at the base, a lea at the other displacement and the other register's copy are
// refused (tier1_code_certificate_frame_copy).

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
    // The barrier's compiles certified, the rows it was aimed at, its writes that keep a row and those certified, and its
    // writes that change one and those refused.
    uint32_t barrier;
    uint32_t barrier_rows;
    uint32_t barrier_kept;
    uint32_t barrier_kept_certified;
    uint32_t barrier_changed;
    uint32_t barrier_changed_refused;
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

// The barrier fixture's asm body (#2963): one record, a full barrier's `lock or dword [rsp], 0`, which the test aims at
// each row in turn and replaces by its variants; it touches rax, rcx, rdx and rsi, the fixture's allocatable registers,
// so that a value that lives across it is in its row there or in a callee-saved register.
static X86Instruction tier1_code_certificate_barrier_records[] = {
    { .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_LOCK_OR_M32_IMM8 },
};

static IrAsmBody tier1_code_certificate_barrier_body = {
    .records = tier1_code_certificate_barrier_records,
    .count = BUSTER_ARRAY_LENGTH(tier1_code_certificate_barrier_records),
    .touched = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI),
};

// b(p0, p1): one block, r = g(), the barrier's body call, and r + p0 + p1 returned.
static bool tier1_code_certificate_barrier_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 8,
        .block_count = 1,
        .call_count = 2,
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
        function->asm_bodies = &tier1_code_certificate_barrier_body;
        function->asm_body_count = 1;
        uint32_t next = 0;
        const uint32_t p0 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t p1 = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->calls[0] = (IrCall){
            .result_type = IR_I64,
        };
        const uint32_t r = tier1_select_test_append(function, &next, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        function->calls[1] = (IrCall){
            .result_type = IR_VOID,
            .body = 1,
        };
        tier1_select_test_append(function, &next, IR_CALL, IR_VOID, 0, 1, BUSTER_IR_NONE);
        const uint32_t partial = tier1_select_test_append(function, &next, IR_ADD, IR_I64, r, p0, BUSTER_IR_NONE);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, partial, p1, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, sum, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[1] = next;
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
    uint64_t reads[5];
    uint64_t writes[5];
    x86_instructions_effects(records, 5, reads, writes);
    Tier1CertEmittedStats stats = { 0 };
    Tier1CertEmittedBuild build = {
        .emitted = &emitted,
        .stats = &stats,
        .branch_of = branch_of,
        .writes = writes,
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

// Compare the kernels' published contents, not their scratch capacity or structure padding. A request's metadata and
// a reference's fields belong to the result only when the corresponding record exists.
static bool tier1_code_certificate_kernel_same(const Tier1CertEmittedKernel* left, const Tier1CertEmittedKernel* right)
{
    bool same = left->count == right->count && left->use_count == right->use_count && left->result_count == right->result_count &&
                left->count <= BUSTER_ARRAY_LENGTH(left->records) && left->use_count <= BUSTER_ARRAY_LENGTH(left->uses) && left->result_count <= BUSTER_ARRAY_LENGTH(left->results) &&
                left->request == right->request && left->referenced == right->referenced && left->condition == right->condition &&
                left->definition == right->definition && left->definition_location == right->definition_location &&
                left->outgoing == right->outgoing && left->call == right->call && left->body == right->body;
    if (same && left->request != UINT32_MAX)
    {
        same = left->request_value == right->request_value && left->request_row == right->request_row && left->request_context == right->request_context &&
               left->request_destination == right->request_destination;
    }
    if (same && left->referenced != UINT32_MAX)
    {
        same = left->reference.instruction == right->reference.instruction && left->reference.field == right->reference.field && left->reference.kind == right->reference.kind &&
               left->reference.target == right->reference.target && left->reference.addend == right->reference.addend;
    }
    same = same && memcmp(left->records, right->records, (uint64_t)left->count * sizeof(X86Instruction)) == 0 &&
           memcmp(left->uses, right->uses, (uint64_t)left->use_count * sizeof(uint32_t)) == 0 &&
           memcmp(left->use_locations, right->use_locations, (uint64_t)left->use_count * sizeof(uint32_t)) == 0 &&
           memcmp(left->results, right->results, (uint64_t)left->result_count * sizeof(uint32_t)) == 0 &&
           memcmp(left->result_locations, right->result_locations, (uint64_t)left->result_count * sizeof(uint32_t)) == 0;
    return same;
}

// Reconstruct the real call fixture's emitted nodes with different prior scratch contents (#1337). Its scalar calls
// have only register arguments and no by-value aggregates, so their step maps are empty and no frame locations are
// needed. Selection, registers, instruction forms and references come from the fixture's actual certified emission.
static void tier1_code_certificate_kernel_storage(Tier1EmitTest* test, const Tier1SelectPlan* plan, const Tier1EmitResult* emitted)
{
    const uint64_t scratch_mark = test->scratch.length;
    const IrFunction* function = plan->function;
    const uint32_t count = function->instruction_count;
    bool room = function->abi_call_count == 0;
    for (uint32_t c = 0; room && c < function->call_count; c += 1)
    {
        room = function->calls[c].argument_count <= BUSTER_ABI_INTEGER_ARGUMENTS;
    }
    uint8_t* claims = ir_allocate(&test->scratch, count, sizeof(uint8_t), &room);
    Tier1CertEmittedStep** steps = ir_allocate(&test->scratch, count, sizeof(Tier1CertEmittedStep*), &room);
    uint8_t* step_counts = ir_allocate(&test->scratch, count, sizeof(uint8_t), &room);
    tier1_emit_test_expect(test, room, "the kernel scratch fixture supplies register claims and empty scalar-call step maps");
    uint32_t calls = 0;
    uint32_t ordinary = 0;
    if (room)
    {
        memset(claims, UINT8_MAX, count);
        memset(steps, 0, (uint64_t)count * sizeof(Tier1CertEmittedStep*));
        memset(step_counts, 0, count);
        Tier1CertEmittedBuild build = {
            .plan = plan,
            .emitted = emitted,
            .claims = claims,
            .base_position = 2 * (emitted->cfg_input.block_count - function->block_count),
            .wide = tier1_cert_emitted_wide(plan, emitted),
            .call_steps = steps,
            .call_step_count = step_counts,
        };
        for (uint32_t node = 0; node < count; node += 1)
        {
            bool present = false;
            for (uint32_t r = 0; !present && r < emitted->code.count; r += 1)
            {
                present = emitted->origins[r] == tier1_origin(TIER1_ORIGIN_NODE, node);
            }
            if (present)
            {
                Tier1CertEmittedKernel left;
                Tier1CertEmittedKernel right;
                memset(&left, 0xa5, sizeof(left));
                memset(&right, 0x5a, sizeof(right));
                const bool rebuilt = tier1_cert_emitted_kernel(&build, node, &left) && tier1_cert_emitted_kernel(&build, node, &right);
                const bool same = rebuilt && tier1_code_certificate_kernel_same(&left, &right);
                tier1_emit_test_expect(test, same, "real emitted kernels publish the same records and operands from differently poisoned scratch");
                calls += same && tier1_select_function_call(function, node);
                ordinary += same && !tier1_select_function_call(function, node);
            }
        }
    }
    tier1_emit_test_expect(test, calls == 2 && ordinary != 0, "poisoned kernel scratch covers both real calls and ordinary emitted nodes");
    arena_pop_to(&test->scratch, scratch_mark);
}

static void tier1_code_certificate_compile(Tier1EmitTest* test, bool rematerialize, bool pointer, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    uint32_t call = BUSTER_IR_NONE;
    const bool created = tier1_code_certificate_function(&test->input, &function, &call);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's call fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's call fixture selects");
    // The caller-saved registers alone: p0 lives across the call in one, saved and restored around it.
    const uint32_t caller_saved = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI);
    const Tier1EmitOptions options = {
        .register_mask = caller_saved,
        .rematerialize = rematerialize,
        .certify = true,
        .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
    };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.frame.frame_pointer == pointer;
    tier1_emit_test_expect(test, certified, "the call fixture's code, through the CFG consumer, passes its certificate");
    counts->certified += certified;
    if (certified)
    {
        tier1_code_certificate_kernel_storage(test, &plan, &emitted);
    }
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
            x86_instructions_effects(&later, 1, &read, &writes);
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
    // p0 & 1's and given its imm32 form: refused at its node, KERNEL at the node's first record, as a form checked at
    // once is; and still there, not at a later refusal, with the last record (the second return's ret) made a nop.
    uint32_t immediate = UINT32_MAX;
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        immediate = immediate == UINT32_MAX && record.form == X86_FORM_AND_RM64_IMM8 && record.immediate == 1 ? r : immediate;
    }
    tier1_emit_test_expect(test, !certified || immediate != UINT32_MAX, "the call fixture ands p0 with 1 by its imm8 form");
    for (uint32_t late = 0; certified && immediate != UINT32_MAX && late < 2; late += 1)
    {
        const uint32_t last = emitted.code.count - 1;
        const X86Instruction saved = emitted.code.records[immediate];
        const X86Instruction ending = emitted.code.records[last];
        emitted.code.records[immediate] = x86_choice_instruction(X86_FORM_AND_RM64_IMM32, (X86Instruction){ .rm = saved.rm }, 1);
        emitted.code.records[last] = late != 0 ? (X86Instruction){ .form = X86_FORM_NOP } : ending;
        Tier1CertEmittedStats stats = { 0 };
        const Tier1CertEmittedReason reason = tier1_certificate_emitted_check(&plan, &emitted, &test->output, &test->scratch, 0, &stats);
        emitted.code.records[immediate] = saved;
        emitted.code.records[last] = ending;
        uint32_t head = immediate;
        while (head > 0 && emitted.origins[head - 1] == emitted.origins[immediate])
        {
            head -= 1;
        }
        const bool first = reason == TIER1_CERT_EMITTED_KERNEL && stats.reason == TIER1_CERT_EMITTED_KERNEL && stats.record == head;
        if (!first)
        {
            printf("  the and's imm32 form%s: reason %u at record %u, its node from %u\n", late != 0 ? " and a nop for the last ret" : "", (unsigned)reason, stats.record, head);
        }
        tier1_emit_test_expect(test, first, "an immediate's wrong form refuses at its node, before any later refusal (#2924's deferred choices)");
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
static void tier1_code_certificate_debug(Tier1EmitTest* test)
{
    IrFunction function = { 0 };
    uint32_t call = BUSTER_IR_NONE;
    const bool created = tier1_code_certificate_function(&test->input, &function, &call);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    IrDominance dominance = { 0 };
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan = { 0 };
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    const Tier1EmitOptions options = {
        .register_mask = (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI),
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

static void tier1_code_certificate_debug_straight(Tier1EmitTest* test)
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
        valid = valid && ir_verify(&function, &test->scratch, NULL).error == IR_OK;
        IrDominance dominance = { 0 };
        valid = valid && tier1_terminal_dominance(test, &function, &dominance);
        Tier1SelectPlan plan = { 0 };
        uint32_t error_node = BUSTER_IR_NONE;
        valid = valid && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &error_node) == TIER1_SELECT_OK;
        Tier1EmitOptions options = {
            .register_mask = alias ? 1u << X86_REGISTER_RDI : (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDI),
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

// A straight-line and of an immediate (p0 & 7, and r64, imm8): its definition proved; with the code's imm8 record replaced
// by the imm32 form of the same value (a valid record for the same sum, but not the form the batch chooses), the deferred
// immediate check (tier1_cert_emitted_choices) refuses every definition claim.
static void tier1_code_certificate_debug_immediate(Tier1EmitTest* test)
{
    const IrSizes sizes = { .instruction_count = 4, .block_count = 1, .parameter_count = 1, .constant_count = 1 };
    IrFunction function = { 0 };
    bool valid = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (valid)
    {
        function.result_type = IR_I64;
        function.incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 1 };
        function.block_offsets[1] = sizes.instruction_count;
        function.parameter_types[0] = IR_I64;
        function.constants[0].low = 7;
        tier1_emit_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(&function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(&function, 2, IR_AND, IR_I64, 0, 1);
        tier1_emit_test_node(&function, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE);
    }
    valid = valid && ir_verify(&function, &test->scratch, NULL).error == IR_OK;
    IrDominance dominance = { 0 };
    valid = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan = { 0 };
    uint32_t error_node = BUSTER_IR_NONE;
    valid = valid && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &error_node) == TIER1_SELECT_OK;
    Tier1EmitOptions options = { .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI), .sources = true };
    Tier1EmitResult emitted = { 0 };
    valid = valid && tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &error_node) == TIER1_EMIT_OK;
    valid = valid && !emitted.general && emitted.certified && emitted.debug_definitions != NULL;
    const Tier1DebugDefinition definition = valid ? emitted.debug_definitions[2] : (Tier1DebugDefinition){ .first = BUSTER_IR_NONE, .end = BUSTER_IR_NONE };
    uint32_t immediate = BUSTER_IR_NONE;
    for (uint32_t r = definition.first; valid && definition.first != BUSTER_IR_NONE && r < definition.end; r += 1)
    {
        immediate = emitted.code.records[r].form == X86_FORM_AND_RM64_IMM8 ? r : immediate;
    }
    tier1_emit_test_expect(test, valid && immediate != BUSTER_IR_NONE, "a straight-line and of an immediate is proved with its imm8 form");
    if (valid && immediate != BUSTER_IR_NONE)
    {
        const uint64_t mark = test->output.length;
        Tier1EmitResult candidate = emitted;
        const X86Instruction saved = emitted.code.records[immediate];
        emitted.code.records[immediate] = x86_choice_instruction(X86_FORM_AND_RM64_IMM32, (X86Instruction){ .rm = saved.rm }, 7);
        const Tier1EmitError corrupted = tier1_debug_straight(&plan, &options, &test->output, &candidate);
        tier1_emit_test_expect(test, corrupted == TIER1_EMIT_CERTIFICATE && candidate.debug_definitions == NULL,
                               "an immediate's form other than the batch's choice refuses every straight-line definition claim");
        emitted.code.records[immediate] = saved;
        arena_pop_to(&test->output, mark);
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// The existing pressure/critical-edge fixture joins two different values. Its scalar phi is usable only after
// every incoming edge's actual copies and predecessor-associated renames pass the full code check.
static void tier1_code_certificate_debug_phis(Tier1EmitTest* test)
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
    valid = valid && ir_verify(&function, &test->scratch, NULL).error == IR_OK;
    IrDominance dominance = { 0 };
    valid = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan = { 0 };
    uint32_t error_node = BUSTER_IR_NONE;
    valid = valid && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &error_node) == TIER1_SELECT_OK;
    Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .sources = true };
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

static void tier1_code_certificate_saved(Tier1EmitTest* test, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_saved_function(&test->input, &function);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's dynamic-frame fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's dynamic-frame fixture selects");
    // Thirteen values live into b1, more than the registers: some are spilled, and the prologue allocates their rows
    // after setting rbp.
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .certify = true };
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

static void tier1_code_certificate_narrow(Tier1EmitTest* test, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_narrow_function(&test->input, &function);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's narrow-parameter fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's narrow-parameter fixture selects");
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .certify = true };
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
static void tier1_code_certificate_realigned(Tier1EmitTest* test, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_realigned_function(&test->input, &function);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's realigned-frame fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's realigned-frame fixture selects");
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .certify = true };
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
static void tier1_code_certificate_exchange(Tier1EmitTest* test, Tier1CodeCertificateCounts* counts)
{
    IrFunction function;
    const bool created = tier1_code_certificate_exchange_function(&test->input, &function);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's compare-exchange fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's compare-exchange fixture selects");
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI);
    const Tier1EmitOptions options = { .register_mask = few, .certify = true };
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

// The barrier fixture (#2963), with or without a frame pointer: its code certified, then its barrier aimed at
// each row a restore after it loads from the frame base register, as each of its variants: one that keeps the row's
// bytes is certified, one that changes them refused. The body's template and its emitted record change together, and
// are put back after each.
static void tier1_code_certificate_barrier(Tier1EmitTest* test, bool pointer, Tier1CodeCertificateCounts* counts)
{
    static const struct
    {
        uint16_t form;
        int32_t immediate;
        uint8_t reg;
        bool kept;
    } variants[] = {
        { X86_FORM_LOCK_OR_M32_IMM8, 0, 0, true },
        { X86_FORM_LOCK_ADD_M64_IMM8, 0, 0, true },
        { X86_FORM_LOCK_SUB_M32_IMM32, 0, 0, true },
        { X86_FORM_LOCK_XOR_M64_IMM32, 0, 0, true },
        { X86_FORM_LOCK_AND_M32_IMM8, -1, 0, true },
        { X86_FORM_LOCK_AND_M64_IMM32, -1, 0, true },
        { X86_FORM_OR_RM32_IMM8, 0, 0, true },
        { X86_FORM_LOCK_OR_M32_IMM8, 1, 0, false },
        { X86_FORM_LOCK_AND_M32_IMM32, 0x7fffffff, 0, false },
        { X86_FORM_LOCK_ADC_M32_IMM8, 0, 0, false },
        { X86_FORM_MOV_RM32_IMM32, 0, 0, false },
        // An or of a register: no immediate, whatever the field holds.
        { X86_FORM_LOCK_OR_M32_R32, 0, X86_REGISTER_RAX, false },
    };
    IrFunction function;
    const bool created = tier1_code_certificate_barrier_function(&test->input, &function);
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's barrier fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's barrier fixture selects");
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RCX) | (1u << X86_REGISTER_RDX) | (1u << X86_REGISTER_RSI);
    const Tier1EmitOptions options = {
        .register_mask = few,
        .certify = true,
        .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
    };
    Tier1EmitResult emitted;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.frame.frame_pointer == pointer;
    tier1_emit_test_expect(test, certified, "the barrier fixture's code, its values in their rows across a `lock or dword [rsp], 0`, passes its certificate");
    counts->barrier += certified;
    // The barrier's record: the body's one, of its node's origin.
    uint32_t barrier = UINT32_MAX;
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        const bool body = tier1_cert_emitted_kind(emitted.origins[r]) == TIER1_ORIGIN_NODE && emitted.code.records[r].form == X86_FORM_LOCK_OR_M32_IMM8;
        barrier = body && barrier == UINT32_MAX ? r : barrier;
    }
    const uint8_t base = (uint8_t)emitted.frame.base_register;
    uint32_t rows = 0;
    for (uint32_t r = barrier; barrier != UINT32_MAX && r < emitted.code.count; r += 1)
    {
        const X86Instruction restore = emitted.code.records[r];
        if (restore.form == X86_FORM_MOV_R64_RM64 && restore.mode == BUSTER_X86_MEMORY_BASE && restore.rm == base)
        {
            rows += 1;
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(variants); v += 1)
            {
                const X86Instruction mutant = {
                    .form = variants[v].form,
                    .reg = variants[v].reg,
                    .rm = base,
                    .mode = BUSTER_X86_MEMORY_BASE,
                    .displacement = restore.displacement,
                    .immediate = variants[v].immediate,
                };
                const X86Instruction original = tier1_code_certificate_barrier_records[0];
                const X86Instruction emitted_record = emitted.code.records[barrier];
                tier1_code_certificate_barrier_records[0] = mutant;
                emitted.code.records[barrier] = mutant;
                const bool refused = tier1_code_certificate_refused(test, &plan, &emitted);
                tier1_code_certificate_barrier_records[0] = original;
                emitted.code.records[barrier] = emitted_record;
                counts->barrier_kept += variants[v].kept;
                counts->barrier_kept_certified += variants[v].kept && !refused;
                counts->barrier_changed += !variants[v].kept;
                counts->barrier_changed_refused += !variants[v].kept && refused;
                char message[256];
                snprintf(message, sizeof(message), "a barrier's write of form %u, immediate %d, into the row at %d that a restore after it loads is %s", (unsigned)variants[v].form,
                         variants[v].immediate, restore.displacement, variants[v].kept ? "certified: the row keeps its bytes" : "refused: the row's value is lost");
                tier1_emit_test_expect(test, refused != variants[v].kept, message);
            }
        }
    }
    tier1_emit_test_expect(test, !certified || (barrier != UINT32_MAX && rows != 0), "the barrier fixture's code holds its body's `lock or`, and a restore after it from a row");
    counts->barrier_rows += rows;
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// A seq_cst source store remains IR_STORE and preserves a desired value live past it. Its certified kernel must retain
// both the copy into the scratch and the locked memory exchange, including when its return takes a different CFG edge.
static void tier1_code_certificate_store(Tier1EmitTest* test)
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
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's seq_cst store fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selection = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
    const bool planned = selection == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's seq_cst store fixture selects");
    Tier1EmitDiagnostic diagnostic = {0};
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .certify = true, .diagnostic = &diagnostic };
    Tier1EmitResult emitted = {0};
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    if (!certified)
    {
        fprintf(stderr, "tier1-emit: seq_cst store certificate: selection=%u emission=%u cause=%u detail=%u node=%u checked=%u reason=%u\n",
                (unsigned)selection, (unsigned)error, (unsigned)diagnostic.cause, diagnostic.error, node, emitted.code_checked, emitted.code_reason);
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

static __attribute__((no_sanitize("function"))) void tier1_code_certificate_copy_call(void* code, uint64_t* destination, uint64_t low, uint64_t high)
{
    void (*copy)(uint64_t*, uint64_t, uint64_t);
    memcpy(&copy, &code, sizeof(copy));
    copy(destination, low, high);
}

// A local copy's paired load (#2818) is one source definition and two concrete records. Both must be regenerated at
// their own frame displacement; neither is an independent value-preserving edit. Its halves are just-built i64s at
// offsets 24 and 32, copied by one v4i32 store. The native check catches misplaced halves and excess output stores;
// the mutations require the certificate to reject either load changed, an incorrect merge, or a narrowed store.
static void tier1_code_certificate_copy_halves(Tier1EmitTest* test, bool pointer)
{
    const IrSizes sizes = { .instruction_count = 12, .block_count = 1, .slot_count = 1, .address_count = 2, .parameter_count = 3 };
    IrFunction function;
    const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (created)
    {
        function.result_type = IR_VOID;
        function.parameter_types[0] = IR_PTR;
        function.parameter_types[1] = IR_I64;
        function.parameter_types[2] = IR_I64;
        function.incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 3 };
        function.slots[0] = (IrSlot){ .size = 64, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        function.addresses[0] = (IrAddress){ .offset = 24, .scale = 1 };
        function.addresses[1] = (IrAddress){ .offset = 32, .scale = 1 };
        function.block_offsets[1] = 12;
        tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 2, IR_PARAM, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 3, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 4, IR_ADDRESS, IR_PTR, 3, BUSTER_IR_NONE, 0);
        tier1_select_test_node(&function, 5, IR_STORE, IR_VOID, 4, 1, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 6, IR_ADDRESS, IR_PTR, 3, BUSTER_IR_NONE, 1);
        tier1_select_test_node(&function, 7, IR_STORE, IR_VOID, 6, 2, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 8, IR_ADDRESS, IR_PTR, 3, BUSTER_IR_NONE, 0);
        tier1_select_test_node(&function, 9, IR_LOAD, IR_V4I32, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 10, IR_STORE, IR_VOID, 0, 9, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 11, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function.alignment_log2[5] = 3;
        function.alignment_log2[7] = 3;
        function.alignment_log2[9] = 3;
        function.alignment_log2[10] = 3;
        function.flags[9] = BUSTER_IR_COPY_PIECE;
        function.flags[10] = BUSTER_IR_COPY_PIECE;
    }
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the paired local-copy certificate fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned && plan.patterns[9].stack_object == 0, "the paired local-copy load selects a folded frame address");
    Tier1EmitDiagnostic diagnostic = { 0 };
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .vector_register_mask = 1u << 14,
        .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
        .certify = true,
        .diagnostic = &diagnostic,
    };
    Tier1EmitResult emitted = { 0 };
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK && emitted.frame.frame_pointer == pointer;
    if (!certified)
    {
        fprintf(stderr, "tier1-emit: local-copy certificate: emission=%u cause=%u detail=%u node=%u checked=%u reason=%u\n", (unsigned)error, (unsigned)diagnostic.cause, diagnostic.error, node,
                emitted.code_checked, emitted.code_reason);
    }
    tier1_emit_test_expect(test, certified, "a paired local-copy load and its vector store pass the emitted-code certificate");
    uint32_t first = UINT32_MAX;
    uint32_t stores = UINT32_MAX;
    uint32_t loads = 0;
    uint32_t store_count = 0;
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        if (emitted.origins[r] == tier1_origin(TIER1_ORIGIN_NODE, 9))
        {
            first = loads == 0 ? r : first;
            loads += 1;
        }
        if (emitted.origins[r] == tier1_origin(TIER1_ORIGIN_NODE, 10))
        {
            stores = r;
            store_count += 1;
        }
    }
    const bool paired = certified && loads == 2 && store_count == 1 && first + 1 < emitted.code.count && emitted.origins[first + 1] == tier1_origin(TIER1_ORIGIN_NODE, 9);
    int32_t offset = 0;
    const bool placed = paired && tier1_emit_cfg_object(&function, &emitted, 0, &offset);
    const X86Instruction low = paired ? emitted.code.records[first] : (X86Instruction){ 0 };
    const X86Instruction high = paired ? emitted.code.records[first + 1] : (X86Instruction){ 0 };
    const bool sse = certified && emitted.prepared.sse;
    const bool shaped = placed && low.form == (sse ? X86_FORM_MOVSD_X_XM64 : X86_FORM_VMOVSD_X_M64) && high.form == (sse ? X86_FORM_MOVHPS_X_M64 : X86_FORM_VMOVHPS_X_X_M64) &&
                        low.reg == 14 && high.reg == low.reg && high.vvvv == (sse ? 0 : low.reg) && low.rm == emitted.frame.base_register && high.rm == low.rm &&
                        low.mode == BUSTER_X86_MEMORY_BASE && high.mode == low.mode && low.displacement == (int64_t)offset + 24 && high.displacement == (int64_t)offset + 32;
    tier1_emit_test_expect(test, shaped, "both local-copy halves use their own resolved frame offsets and one extended xmm register");
    uint32_t refused = 0;
    for (uint32_t mutation = 0; shaped && mutation < 10; mutation += 1)
    {
        const uint32_t r = mutation == 9 ? stores : first + (mutation & 1);
        X86Instruction record = emitted.code.records[r];
        if (mutation < 2)
        {
            record.displacement += mutation == 0 ? 8 : -8;
        }
        else if (mutation < 4)
        {
            record.rm = X86_REGISTER_RAX;
        }
        else if (mutation < 6)
        {
            record.reg ^= 1;
        }
        else if (mutation < 8)
        {
            record = (X86Instruction){ .form = X86_FORM_MOVAPS_X_XM, .reg = low.reg, .rm = low.reg };
        }
        else
        {
            record.form = mutation == 8 ? high.form : X86_FORM_MOVSD_XM64_X;
            record.vvvv = mutation == 8 && !sse ? low.reg : 0;
        }
        const bool rejected = tier1_code_certificate_refuses(test, &plan, &emitted, r, record, UINT32_MAX);
        tier1_emit_test_expect(test, rejected, "a corrupted local-copy load, missing half, or narrowed store is refused");
        refused += rejected;
    }
    if (shaped && !sse)
    {
        X86Instruction wrong_merge = high;
        wrong_merge.vvvv ^= 1;
        tier1_emit_test_expect(test, tier1_code_certificate_refuses(test, &plan, &emitted, first + 1, wrong_merge, UINT32_MAX), "a paired VEX load that merges another register's low half is refused");
    }
    if (shaped)
    {
        const uint32_t origin = emitted.origins[first + 1];
        emitted.origins[first + 1] = tier1_origin(TIER1_ORIGIN_MOVE, 2 * 9 + 1);
        tier1_emit_test_expect(test, tier1_code_certificate_refused(test, &plan, &emitted), "a paired high-half load cannot be relabeled as an independent move");
        emitted.origins[first + 1] = origin;
    }
    tier1_emit_test_expect(test, refused == 10, "every concrete local-copy corruption was exercised and refused");
    tier1_emit_test_expect(test, !shaped || !tier1_code_certificate_refused(test, &plan, &emitted), "the original local-copy kernel is certified after restoring every mutation");
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (shaped && tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
    {
        uint64_t random = UINT64_C(0x2818);
        for (uint32_t sample = 0; sample < 32; sample += 1)
        {
            uint64_t output[4] = { UINT64_C(0x123456789abcdef0), 0, 0, UINT64_C(0xfedcba9876543210) };
            const uint64_t a = tier1_emit_test_random(&random);
            const uint64_t b = tier1_emit_test_random(&random);
            tier1_code_certificate_copy_call(executable, output + 1, a, b);
            tier1_emit_test_expect(test, output[0] == UINT64_C(0x123456789abcdef0) && output[1] == a && output[2] == b && output[3] == UINT64_C(0xfedcba9876543210),
                                   "the complete local-copy kernel copies both halves and leaves neighboring bytes unchanged");
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

static void tier1_code_certificate_copy_targets(Tier1EmitTest* test)
{
    static const X86Architecture architectures[] = { X86_ARCH_BASELINE, X86_ARCH_V3, X86_ARCH_V4, X86_ARCH_ZNVER5 };
    uint8_t allowed[X86_FORM_COUNT];
    uint8_t native[X86_FORM_COUNT];
    uint8_t* const saved_allowed = test->allowed;
    const uint8_t* const saved_native = test->native_allowed;
    const X86Target host = x86_target_native();
    x86_target_forms(&host, native);
    test->allowed = allowed;
    test->native_allowed = native;
    for (uint32_t target = 0; target < BUSTER_ARRAY_LENGTH(architectures); target += 1)
    {
        const X86Target policy = { .architecture = architectures[target] };
        x86_target_forms(&policy, allowed);
        for (uint32_t pointer = 0; pointer < 2; pointer += 1)
        {
            tier1_code_certificate_copy_halves(test, pointer != 0);
        }
    }
    test->allowed = saved_allowed;
    test->native_allowed = saved_native;
}

// The literal pool (#2744): l(p) stores c0 + c1 at p, two vector constants of four doublewords that no broadcast makes
// ({ 1, 2, 3, 4 } and { 16, 32, 48, 64 }), each its literal's rip-relative load. Its code is certified. Then each of
// these is refused, one at a time, everything put back after: a literal's bytes changed, its width, a load's reference's
// addend, its target the other literal (whose bytes aren't its constant's), and the emission's index naming the other
// literal for a constant. The literal's bytes, not its index, are what the certificate checks.
static void tier1_code_certificate_literals(Tier1EmitTest* test, uint32_t* refused)
{
    const IrSizes sizes = { .instruction_count = 6, .block_count = 1, .parameter_count = 1, .vector_constant_count = 2 };
    IrFunction function;
    const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (created)
    {
        function.result_type = IR_VOID;
        function.parameter_types[0] = IR_PTR;
        function.incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 1 };
        function.block_offsets[1] = 6;
        for (uint32_t lane = 0; lane < 4; lane += 1)
        {
            ir_simd_lane_write(&function.vector_constants[0], 32, lane, lane + 1);
            ir_simd_lane_write(&function.vector_constants[1], 32, lane, 16 * (lane + 1));
        }
        tier1_select_test_node(&function, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 1, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 2, IR_VCONST, IR_V4I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 3, IR_ADD, IR_V4I32, 1, 2, BUSTER_IR_NONE);
        tier1_select_test_node(&function, 4, IR_STORE, IR_VOID, 0, 3, BUSTER_IR_NONE);
        function.alignment_log2[4] = 4;
        tier1_select_test_node(&function, 5, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the code certificate's literal fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selection = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
    const bool planned = selection == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the code certificate's literal fixture selects");
    Tier1EmitDiagnostic diagnostic = {0};
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .certify = true, .diagnostic = &diagnostic };
    Tier1EmitResult emitted = {0};
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    if (!certified)
    {
        fprintf(stderr, "tier1-emit: literal certificate: selection=%u emission=%u cause=%u detail=%u node=%u checked=%u reason=%u\n",
                (unsigned)selection, (unsigned)error, (unsigned)diagnostic.cause, diagnostic.error, node, emitted.code_checked, emitted.code_reason);
    }
    tier1_emit_test_expect(test, certified, "a function whose vector constants are its literals' loads passes the emitted-code certificate");
    // Both constants are literals, each loaded once, rip-relative, by the reference that names it.
    uint32_t loads = 0;
    uint32_t first = UINT32_MAX;
    for (uint32_t i = 0; certified && i < emitted.reference_count; i += 1)
    {
        const LinkReference reference = emitted.references[i];
        const X86Instruction record = emitted.code.records[reference.instruction];
        const bool literal = reference.target >= BUSTER_TIER1_EMIT_LITERAL && reference.kind == BUSTER_LINK_FIXUP_PC32 && reference.field == X86_FIXUP_DISPLACEMENT &&
                             reference.addend == 0 && (record.mode & BUSTER_X86_MEMORY_RIP) == BUSTER_X86_MEMORY_RIP;
        loads += literal;
        first = literal && first == UINT32_MAX ? i : first;
    }
    const bool shaped = certified && emitted.literal_count == 2 && loads == 2 && emitted.literal_widths[0] == 16 && emitted.literal_widths[1] == 16;
    tier1_emit_test_expect(test, shaped, "the literal fixture's two constants are two 16-byte literals, each one rip-relative load's");
    for (uint32_t mutation = 0; shaped && mutation < 5; mutation += 1)
    {
        LinkReference* reference = &emitted.references[first];
        const LinkReference kept_reference = *reference;
        const uint32_t literal = reference->target - BUSTER_TIER1_EMIT_LITERAL;
        const IrSimdValue kept_bytes = emitted.literals[literal];
        const uint8_t kept_width = emitted.literal_widths[literal];
        // The constant's entry of the emission's index (16-byte literals: each constant's first of three).
        uint32_t* index = &emitted.literal_of[literal == emitted.literal_of[0] - 1 ? 0 : 3];
        const uint32_t kept_index = *index;
        if (mutation == 0)
        {
            emitted.literals[literal].words[1] ^= 1;
        }
        else if (mutation == 1)
        {
            emitted.literal_widths[literal] = 32;
        }
        else if (mutation == 2)
        {
            reference->addend = 4;
        }
        else if (mutation == 3)
        {
            reference->target = BUSTER_TIER1_EMIT_LITERAL + (literal ^ 1);
        }
        else
        {
            *index = (literal ^ 1) + 1;
        }
        const bool rejected = tier1_code_certificate_refused(test, &plan, &emitted);
        *reference = kept_reference;
        emitted.literals[literal] = kept_bytes;
        emitted.literal_widths[literal] = kept_width;
        *index = kept_index;
        tier1_emit_test_expect(test, rejected, "a literal's corrupted bytes, width, addend, target or index is refused");
        *refused += rejected;
    }
    tier1_emit_test_expect(test, !shaped || !tier1_code_certificate_refused(test, &plan, &emitted), "the literal fixture's code is certified again once put back");
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// The producer's row owners are assertions, not evidence. Corrupt them without
// touching the allocation, and corrupt the predecessor's store without changing
// its metadata: both must be refused even though the register assertion remains.
static void tier1_code_certificate_carry(Tier1EmitTest* test, bool pointer, bool aggregate)
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
    const bool planned = created && tier1_select_plan(&fixture.function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
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

// Check the actual aggregate-copy records through their decoder and the value-flow checker. The initial RAX fact is
// independent of the emitter's owner/liveness columns; an owed value starts only in its call-save row. A required RAX
// value must survive the copy or be explicitly repaid afterwards, while a dead register starts with no known bytes.
static bool tier1_code_certificate_aggregate_copy_checked(Tier1EmitTest* test, const Tier1EmitResult* emitted, uint32_t copied, bool live, bool owed)
{
    const IrFunction function = { .instruction_count = 3 };
    const Tier1SelectPlan plan = { .function = &function };
    Tier1CertLocation locations[64] = { 0 };
    Tier1CertSource source[16] = { 0 };
    Tier1CertInstruction code[24] = { 0 };
    uint32_t references[8];
    memset(references, 0xff, sizeof(references));
    Tier1CertEmittedStats stats = { 0 };
    Tier1CertEmittedBuild build = {
        .plan = &plan,
        .emitted = emitted,
        .stats = &stats,
        .locations = locations,
        .location_count = 32,
        .location_capacity = BUSTER_ARRAY_LENGTH(locations),
        .low = -128,
        .high = 64,
        .base = X86_REGISTER_RSP,
        .source = source,
        .source_capacity = BUSTER_ARRAY_LENGTH(source),
        .code = code,
        .code_capacity = BUSTER_ARRAY_LENGTH(code),
        .references = references,
        .frame_pointer = INT64_MIN,
        .frame_unit = 128,
    };
    for (uint32_t r = 0; r < 32; r += 1)
    {
        locations[r] = (Tier1CertLocation){
            .unit = 8 * (r / 2),
            .width = r % 2 == 0 ? 8 : 4,
            .extent = 8,
            .class = TIER1_CERT_GPR,
            .reg = (uint8_t)(r / 2),
        };
    }
    const Tier1CertEmittedStep step = {
        .value = 0,
        .pseudo = 2,
        .size = 8,
        .to_base = X86_REGISTER_RSP,
        .kind = TIER1_CERT_STEP_COPY,
        .bytes = 8,
        .width = 8,
    };
    const uint32_t rax = tier1_cert_emitted_gpr(X86_REGISTER_RAX, 8);
    const Tier1CertFact entry = { .value = 1, .location = owed ? tier1_cert_emitted_frame(&build, 32, 8) : rax };
    const Tier1CertFact exit = { .value = 1, .location = rax };
    uint32_t at = 0;
    bool valid = copied <= emitted->code.count && tier1_cert_emitted_step(&build, 2, &step, &at, copied) && at == copied;
    for (uint32_t r = copied; valid && r < emitted->code.count; r += 1)
    {
        valid = tier1_cert_emitted_append_move(&build, r, BUSTER_IR_NONE, rax);
    }
    // The emitted decoder represents an exact rematerialization, including a frame-address lea, by its value id.
    const Tier1CertValue values[3] = {
        { .width = 8, .class = TIER1_CERT_GPR, .recipe = TIER1_CERT_INTEGER_RECIPE },
        { .width = 8, .class = TIER1_CERT_GPR },
        { .width = 8, .class = TIER1_CERT_GPR },
    };
    const Tier1CertBlock block = {
        .source = source,
        .code = code,
        .certificate = &entry,
        .source_count = build.source_count,
        .code_count = build.code_count,
        .certificate_count = live ? 1 : 0,
    };
    const Tier1CertProgram program = {
        .locations = locations,
        .values = values,
        .blocks = &block,
        .entry = &entry,
        .exit = &exit,
        .unit_count = 320,
        .location_count = build.location_count,
        .value_count = BUSTER_ARRAY_LENGTH(values),
        .block_count = 1,
        .entry_count = live ? 1 : 0,
        .exit_count = live ? 1 : 0,
        .flags_location = BUSTER_CERT_NONE,
    };
    const uint64_t saved = test->scratch.length;
    const size_t bytes = valid ? tier1_certificate_scratch_size(&program) : 0;
    void* scratch = bytes != 0 ? arena_push(&test->scratch, bytes, 64) : NULL;
    Tier1CertReport report = { 0 };
    valid = valid && scratch != NULL && tier1_certificate_check(&program, scratch, bytes, &report) == TIER1_CERT_OK;
    arena_pop_to(&test->scratch, saved);
    return valid;
}

// #1816: a rematerialized aggregate address uses r11, so its bytes go through RAX. Controlled allocation columns
// cover a dead RAX, a live one, a live value still owed from a call-save row, and a stale owner whose current interval
// expired, spilled or moved to another register. The real emitter and decoder run, then the independent certificate
// checks their records and preservation mutants. Each state twice: the address 16 bytes from rsp, recomputed by a lea,
// and 0 bytes from it, recomputed by rsp's register copy into r11 (#3149).
static void tier1_code_certificate_aggregate_copy(Tier1EmitTest* test)
{
    uint32_t certified = 0;
    uint32_t refused = 0;
    uint32_t copies = 0;
    // The address at 16 from rsp, recomputed by a lea, and at 0, by rsp's register copy (#3149).
    for (uint32_t state = 0; state < 12; state += 1)
    {
        const uint64_t displacement = state < 6 ? 16 : 0;
        const uint32_t rax_state = state % 6;
        const bool live = rax_state == 1 || rax_state == 2;
        const bool owed = rax_state == 2;
        uint32_t node_values[3] = { 0, 1, BUSTER_TIER1_CFG_NONE };
        uint32_t value_nodes[2] = { 0, 1 };
        uint32_t sizes[3] = { 8, 8, 0 };
        uint32_t value_offsets[3] = { 0, 1, 2 };
        uint32_t starts[2] = { 0, 0 };
        uint32_t ends[2] = { 8, rax_state == 3 ? 4 : 8 };
        uint32_t fragment_offsets[3] = { 0, 1, 2 };
        uint32_t spill_ids[2] = { BUSTER_TIER1_CFG_NONE, 0 };
        uint8_t classes[2] = { BUSTER_TIER1_ALLOCATE_GPR, BUSTER_TIER1_ALLOCATE_GPR };
        uint8_t rematerializable[2] = { 1, 0 };
        uint8_t kinds[2] = { BUSTER_TIER1_ALLOCATE_REMATERIALIZE, rax_state == 4 ? BUSTER_TIER1_ALLOCATE_SPILL : BUSTER_TIER1_ALLOCATE_REGISTER };
        uint8_t registers[2] = { UINT8_MAX, rax_state == 5 ? X86_REGISTER_RCX : X86_REGISTER_RAX };
        uint8_t hinted_registers[2] = { UINT8_MAX, X86_REGISTER_RAX };
        const Tier1RematerializationInput recipes = {
            .operations = (const uint8_t[]){ BUSTER_TIER1_REMAT_STACK, BUSTER_TIER1_REMAT_NONE, BUSTER_TIER1_REMAT_NONE },
            .widths = (const uint8_t[]){ 8, 8, 0 },
            .flags = (const uint8_t[]){ BUSTER_TIER1_REMAT_STABLE, 0, 0 },
            .values = (const uint64_t[]){ displacement, 0, 0 },
            .count = 3,
        };
        Tier1EmitResult emitted = {
            .prepared = { .recipes = recipes, .sizes = sizes },
            .frame = { .base_register = X86_REGISTER_RSP },
            .cfg_input = { .classes = classes, .rematerializable = rematerializable, .value_count = 2 },
            .cfg = {
                .intervals = { .starts = starts, .ends = ends, .values = node_values, .classes = classes, .count = 2, .value_count = 2 },
                .allocation = { .fragment_offsets = fragment_offsets, .starts = starts, .ends = ends, .spill_ids = spill_ids, .kinds = kinds, .registers = registers, .interval_count = 2, .fragment_count = 2 },
                .value_interval_offsets = value_offsets,
            },
            .node_values = node_values,
            .value_nodes = value_nodes,
        };
        Tier1EmitTransitions transitions = { .registers = hinted_registers };
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(transitions.owners); r += 1)
        {
            transitions.owners[r] = BUSTER_IR_NONE;
        }
        transitions.owners[X86_REGISTER_RAX] = rax_state == 0 ? BUSTER_IR_NONE : 1;
        X86Instruction records[8] = { 0 };
        Tier1EmitBuffer buffer = { .records = records, .capacity = BUSTER_ARRAY_LENGTH(records) };
        const Tier1EmitError error = tier1_emit_cfg_aggregate_copy(&emitted, &transitions, owed ? 1u << X86_REGISTER_RAX : 0, 0, 4, X86_REGISTER_RSP, 0, 0, 8, &buffer);
        const uint32_t copied = buffer.count;
        const bool kept = rax_state == 1;
        tier1_emit_test_expect(test, error == TIER1_EMIT_OK && copied == (kept ? 5u : 3u), "aggregate copies preserve only a live RAX that is physically present");
        // The recomputed address's record: rsp's register copy into the scratch register at 0, a lea at 16.
        for (uint32_t r = 0; error == TIER1_EMIT_OK && r < copied; r += 1)
        {
            const bool copy = records[r].form == X86_FORM_MOV_R64_RM64 && records[r].mode == BUSTER_X86_REGISTER_DIRECT && records[r].reg == BUSTER_TIER1_SELECT_SCRATCH && records[r].rm == X86_REGISTER_RSP;
            copies += copy && displacement == 0 ? 1 : copy ? 100 : 0;
        }
        if (error == TIER1_EMIT_OK && owed)
        {
            tier1_emit_append(&buffer, (X86Instruction){ .displacement = 32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 });
        }
        emitted.code.records = records;
        emitted.code.count = buffer.count;
        const bool checked = error == TIER1_EMIT_OK && tier1_code_certificate_aggregate_copy_checked(test, &emitted, copied, live, owed);
        tier1_emit_test_expect(test, checked, "a rematerialized aggregate copy certifies with dead, live, owed or stale RAX state");
        certified += checked;
        if (checked && kept)
        {
            X86Instruction original[5];
            memcpy(original, records, sizeof(original));
            for (uint32_t mutation = 0; mutation < 6; mutation += 1)
            {
                memcpy(records, original, sizeof(original));
                emitted.code.count = 5;
                if (mutation < 2)
                {
                    records[mutation == 0 ? 1 : 4].reg = X86_REGISTER_RCX;
                }
                else if (mutation == 2)
                {
                    memmove(records + 1, records + 2, 3 * sizeof(*records));
                    emitted.code.count = 4;
                }
                else if (mutation == 3)
                {
                    emitted.code.count = 4;
                }
                else if (mutation == 4)
                {
                    memmove(records + 1, records + 2, 2 * sizeof(*records));
                    emitted.code.count = 3;
                }
                // The last mutation removes the initial RAX fact: an unknown source is still refused by a saved pair.
                const bool rejected = !tier1_code_certificate_aggregate_copy_checked(test, &emitted, emitted.code.count, mutation != 5, false);
                tier1_emit_test_expect(test, rejected, "missing or corrupt aggregate preservation and saving unknown RAX remain refused");
                refused += rejected;
            }
        }
        else if (checked && owed)
        {
            emitted.code.count = copied;
            const bool rejected = !tier1_code_certificate_aggregate_copy_checked(test, &emitted, copied, true, true);
            tier1_emit_test_expect(test, rejected, "an owed RAX value must be repaid before its next use");
            refused += rejected;
        }
    }
    tier1_emit_test_expect(test, certified == 12 && refused == 14, "aggregate preservation covers all six RAX states and all seven mutants, its address at 16 and at 0 from rsp");
    tier1_emit_test_expect(test, copies == 6, "the address at 0 is recomputed by rsp's register copy in each RAX state, and at 16 by none");
}

// #3149: a recomputed frame address's record `record`, alone, decoded as the CFG consumer's move of node 0 (its value
// 0's recipe a frame address `displacement` bytes from the frame's base register `base`, rsp or, under a frame pointer,
// rbp; rematerializable or not) and checked by the independent certificate, which must find value 0 in rbx after it.
// Whether it is certified.
static bool tier1_code_certificate_frame_copy_checked(Tier1EmitTest* test, uint8_t base, uint64_t displacement, bool recomputed, X86Instruction record)
{
    const bool pointer = base == X86_REGISTER_RBP;
    uint32_t node_values[2] = {
        0,
        BUSTER_TIER1_CFG_NONE,
    };
    uint32_t value_nodes[1] = { 0 };
    uint32_t sizes[2] = {
        8,
        0,
    };
    uint8_t classes[1] = { BUSTER_TIER1_ALLOCATE_GPR };
    uint8_t rematerializable[1] = { recomputed ? 1 : 0 };
    X86Instruction records[1] = { record };
    const Tier1RematerializationInput recipes = {
        .operations = (const uint8_t[]){ BUSTER_TIER1_REMAT_STACK },
        .widths = (const uint8_t[]){ 8 },
        .flags = (const uint8_t[]){ BUSTER_TIER1_REMAT_STABLE },
        .values = &displacement,
        .count = 1,
    };
    Tier1EmitResult emitted = {
        .prepared = {
            .recipes = recipes,
            .sizes = sizes,
        },
        .frame = {
            .base_register = base,
            .frame_pointer = pointer,
        },
        .cfg_input = {
            .classes = classes,
            .rematerializable = rematerializable,
            .value_count = 1,
        },
        .node_values = node_values,
        .value_nodes = value_nodes,
    };
    emitted.code.records = records;
    emitted.code.count = 1;
    const IrFunction function = { .instruction_count = 1 };
    const Tier1SelectPlan plan = { .function = &function };
    Tier1CertLocation locations[64] = { 0 };
    Tier1CertSource source[4] = { 0 };
    Tier1CertInstruction code[4] = { 0 };
    uint32_t references[1] = { UINT32_MAX };
    Tier1CertEmittedStats stats = { 0 };
    Tier1CertEmittedBuild build = {
        .plan = &plan,
        .emitted = &emitted,
        .stats = &stats,
        .locations = locations,
        .location_count = 32,
        .location_capacity = BUSTER_ARRAY_LENGTH(locations),
        .low = -128,
        .high = 64,
        .base = base,
        .source = source,
        .source_capacity = BUSTER_ARRAY_LENGTH(source),
        .code = code,
        .code_capacity = BUSTER_ARRAY_LENGTH(code),
        .references = references,
        .frame_pointer = pointer ? -8 : INT64_MIN,
        .frame_unit = 128,
    };
    for (uint32_t r = 0; r < 32; r += 1)
    {
        locations[r] = (Tier1CertLocation){
            .unit = 8 * (r / 2),
            .width = r % 2 == 0 ? 8 : 4,
            .extent = 8,
            .class = TIER1_CERT_GPR,
            .reg = (uint8_t)(r / 2),
        };
    }
    bool valid = tier1_cert_emitted_append_move(&build, 0, 0, BUSTER_CERT_NONE);
    const Tier1CertFact exit = {
        .value = 0,
        .location = tier1_cert_emitted_gpr(X86_REGISTER_RBX, 8),
    };
    const Tier1CertValue values[1] = {
        {
            .width = 8,
            .class = TIER1_CERT_GPR,
            .recipe = recomputed ? TIER1_CERT_INTEGER_RECIPE : TIER1_CERT_NO_RECIPE,
        },
    };
    const Tier1CertBlock block = {
        .source = source,
        .code = code,
        .source_count = build.source_count,
        .code_count = build.code_count,
    };
    const Tier1CertProgram program = {
        .locations = locations,
        .values = values,
        .blocks = &block,
        .exit = &exit,
        .unit_count = 320,
        .location_count = build.location_count,
        .value_count = BUSTER_ARRAY_LENGTH(values),
        .block_count = 1,
        .exit_count = 1,
        .flags_location = BUSTER_CERT_NONE,
    };
    const uint64_t saved = test->scratch.length;
    const size_t bytes = valid ? tier1_certificate_scratch_size(&program) : 0;
    void* scratch = bytes != 0 ? arena_push(&test->scratch, bytes, 64) : NULL;
    Tier1CertReport report = { 0 };
    valid = valid && scratch != NULL && tier1_certificate_check(&program, scratch, bytes, &report) == TIER1_CERT_OK;
    arena_pop_to(&test->scratch, saved);
    return valid;
}

// #3149: a frame address recomputed into rbx, its recipe 0 or 16 bytes from the frame's base register: rsp, or rbp
// under a frame pointer. The emitter's copy (tier1_rematerialize: the base's register copy `mov rbx, base` at 0, `lea
// rbx, [base + 16]` at 16) is certified; the base's register copy for the recipe at 16 (a move of the base, which holds
// no value), a lea at 0 (no recipe's record now), a lea at the other displacement and the other register's copy (`mov
// rbx, rbp` where rsp is the base, `mov rbx, rsp` where rbp is) are refused, and so is each record for a value without
// a recipe.
static void tier1_code_certificate_frame_copy(Tier1EmitTest* test)
{
    uint32_t certified = 0;
    uint32_t refused = 0;
    uint32_t bare = 0;
    for (uint32_t b = 0; b < 2; b += 1)
    {
        const uint8_t base = b == 0 ? X86_REGISTER_RSP : X86_REGISTER_RBP;
        const uint8_t other = b == 0 ? X86_REGISTER_RBP : X86_REGISTER_RSP;
        const X86Instruction move = {
            .reg = X86_REGISTER_RBX,
            .rm = base,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .form = X86_FORM_MOV_R64_RM64,
        };
        const X86Instruction near = {
            .reg = X86_REGISTER_RBX,
            .rm = base,
            .mode = BUSTER_X86_MEMORY_BASE,
            .form = X86_FORM_LEA_R64_M,
        };
        const X86Instruction far = {
            .displacement = 16,
            .reg = X86_REGISTER_RBX,
            .rm = base,
            .mode = BUSTER_X86_MEMORY_BASE,
            .form = X86_FORM_LEA_R64_M,
        };
        const X86Instruction crossed = {
            .reg = X86_REGISTER_RBX,
            .rm = other,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .form = X86_FORM_MOV_R64_RM64,
        };
        const X86Instruction records[4] = {
            move,
            near,
            far,
            crossed,
        };
        for (uint32_t d = 0; d < 2; d += 1)
        {
            const uint64_t displacement = d == 0 ? 0 : 16;
            const uint8_t operation = BUSTER_TIER1_REMAT_STACK;
            const uint8_t width = 8;
            const uint8_t stable = BUSTER_TIER1_REMAT_STABLE;
            const Tier1RematerializationInput input = {
                .operations = &operation,
                .widths = &width,
                .flags = &stable,
                .values = &displacement,
                .count = 1,
            };
            Tier1Rematerialized copy = { 0 };
            const bool made = tier1_rematerialize(&input, 0, X86_REGISTER_RBX, base, 0, &copy);
            tier1_emit_test_expect(test, made && memcmp(&copy.instruction, &records[d == 0 ? 0 : 2], sizeof(X86Instruction)) == 0, "a frame address at 0 from its base is the base's register copy, at 16 a lea");
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(records); k += 1)
            {
                const bool expected = k == (d == 0 ? 0u : 2u);
                const bool checked = tier1_code_certificate_frame_copy_checked(test, base, displacement, true, records[k]);
                tier1_emit_test_expect(test, checked == expected, expected ? "a frame address's recomputation is certified" : "a frame address's other record is refused");
                certified += checked && expected;
                refused += !checked && !expected;
                bare += !tier1_code_certificate_frame_copy_checked(test, base, displacement, false, records[k]);
            }
        }
    }
    tier1_emit_test_expect(test, certified == 4 && refused == 12 && bare == 16, "each recipe's own copy alone is certified, from rsp and from rbp, and no record of a value without a recipe");
    printf("tier1 code certificate: frame addresses recomputed from rsp and rbp, %u of 4 copies certified, %u of 12 other records and %u of 16 without a recipe refused\n", certified, refused, bare);
}

// #2820's destination: a call whose aggregate result goes through the hidden pointer to a pointer value, no frame
// object's, as a call that passes its function's own hidden pointer on has it. q(x, f, d) calls f(x) through its pointer,
// its 64-byte result to d: x arrives in rdi and goes to rsi (rdi is the hidden pointer's), f in rsi and goes to r10, d in
// rdx and goes to rdi, one parallel move whose order matters. Shape 1 then returns d's first quadword, which keeps d live
// across the call; shape 2's d is an inttoptr of an I64, as ir_lower makes a forwarded call's (#2820's ir_typed_call),
// which the call's use alone keeps; shape 3 makes the call 48 times, each with its argument, its callee and its
// destination, more effective uses than one a call and every other node's room hold. Each compile is
// certified; each call's effective uses end with its destination, no lea writes rdi, and the native code writes f's
// result to d's bytes alone, against the callee called directly. The first call's move into rdi made a move of rdi into
// itself is refused, and so is a plan whose call reads the callee where its destination was (the liveness validation).
typedef struct Tier1CodeCertificateWide Tier1CodeCertificateWide;
struct Tier1CodeCertificateWide
{
    int64_t words[8];
};

static Tier1CodeCertificateWide tier1_code_certificate_wide(int64_t x)
{
    Tier1CodeCertificateWide wide;
    for (uint32_t k = 0; k < 8; k += 1)
    {
        wide.words[k] = x * 3 + (int64_t)k * 0x0101010101010101;
    }
    return wide;
}

// The emitted code called with f and d (the destination); the sanitizer's function check stays out, since the code has
// no type prefix before it.
static __attribute__((no_sanitize("function"))) int64_t tier1_code_certificate_forwarded_call(const void* code, int64_t x, void* destination)
{
    int64_t (*function)(int64_t, Tier1CodeCertificateWide (*)(int64_t), void*);
    memcpy(&function, &code, sizeof(function));
    return function(x, tier1_code_certificate_wide, destination);
}

// The emitted code called with f alone, for the frame object's control.
static __attribute__((no_sanitize("function"))) int64_t tier1_code_certificate_object_call(const void* code, int64_t x)
{
    int64_t (*function)(int64_t, Tier1CodeCertificateWide (*)(int64_t));
    memcpy(&function, &code, sizeof(function));
    return function(x, tier1_code_certificate_wide);
}

#define BUSTER_TIER1_CODE_CERTIFICATE_FORWARDED_CALLS 48u

static void tier1_code_certificate_forwarded(Tier1EmitTest* test, uint32_t shape, uint32_t* refused)
{
    char text[4096];
    const bool converted = shape == 2;
    int length = snprintf(text, sizeof(text),
                          "function (i64, ptr, %s) -> %s {\n"
                          "  passed #0 size=0 loc(1,6,255,0,0)\n"
                          "  abi #0 call(0,4,2,0,ret(2,0,0,255,255,255,255)) passed=#0 result=64\n"
                          "b0:\n"
                          "  %%0 = param.i64 0\n"
                          "  %%1 = param.ptr 1\n"
                          "  %%2 = param.%s 2\n",
                          converted ? "i64" : "ptr", shape == 0 ? "void" : "i64", converted ? "i64" : "ptr");
    const uint32_t destination = converted ? 3 : 2;
    if (converted)
    {
        length += snprintf(text + length, sizeof(text) - (size_t)length, "  %%3 = inttoptr.ptr %%2\n");
    }
    const uint32_t first_call = destination + 1;
    const uint32_t calls = shape == 3 ? BUSTER_TIER1_CODE_CERTIFICATE_FORWARDED_CALLS : 1;
    for (uint32_t k = 0; k < calls; k += 1)
    {
        length += snprintf(text + length, sizeof(text) - (size_t)length, "  %%%u = call_indirect.void %%1(%%0) fixed=(i64) variadic=0 abi=#0 into=%%%u\n", first_call + k, destination);
    }
    const uint32_t after = first_call + calls;
    if (shape == 0)
    {
        length += snprintf(text + length, sizeof(text) - (size_t)length, "  %%%u = ret.void\n}\n", after);
    }
    else
    {
        length += snprintf(text + length, sizeof(text) - (size_t)length, "  %%%u = load.i64 %%%u align=8\n  %%%u = ret.void %%%u\n}\n", after, destination, after + 1, after);
    }
    const String8 source = { .pointer = text, .length = (uint64_t)length };
    IrFunction function;
    const bool valid = length > 0 && (size_t)length < sizeof(text) && ir_parse(source, &test->input, &test->scratch, &function).error == IR_PARSE_OK &&
                       ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the forwarded destination's fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const uint64_t output_mark = test->output.length;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "a call whose result goes through the hidden pointer to a pointer value selects");
    // Every call's effective uses are its argument, its callee and its destination, in that order, inside the plan's
    // storage.
    bool used = planned && (const uint8_t*)(plan.effective_use_values + plan.effective_use_count) <= (const uint8_t*)test->output.base + output_mark + plan.arena_bytes;
    for (uint32_t k = 0; used && k < calls; k += 1)
    {
        const uint32_t call = first_call + k;
        const uint32_t u = plan.effective_use_offsets[call];
        used = plan.covered[call] == 0 && plan.effective_use_offsets[call + 1] - u == 3 && plan.effective_use_values[u] == 0 && plan.effective_use_values[u + 1] == 1 &&
               plan.effective_use_values[u + 2] == destination;
    }
    tier1_emit_test_expect(test, used, "each call's effective uses are its argument, its callee and its destination, in that order");
    if (used)
    {
        // The liveness validation refuses a call that reads its callee where its destination was.
        const uint32_t u = plan.effective_use_offsets[first_call] + 2;
        plan.effective_use_values[u] = 1;
        tier1_emit_test_expect(test, tier1_liveness_validate_plan(&plan, &test->scratch, true) == TIER1_LIVENESS_INVALID, "a call's effective uses that end with another value than its destination are refused");
        plan.effective_use_values[u] = destination;
    }
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .certify = true };
    Tier1EmitResult emitted = { 0 };
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    tier1_emit_test_expect(test, certified, "the forwarded destination's code passes the emitted-code certificate");
    // Each call's use of its destination names rdi (Tier1CfgInput.use_registers, #2646's hints).
    const Tier1CfgInput* input = &emitted.cfg_input;
    const uint32_t base = certified ? 2 * (input->block_count - function.block_count) : 0;
    const uint32_t v = certified ? emitted.node_values[destination] : BUSTER_TIER1_CFG_NONE;
    uint32_t named = 0;
    for (uint32_t u = v != BUSTER_TIER1_CFG_NONE && input->use_registers != 0 ? input->use_offsets[v] : 0; v != BUSTER_TIER1_CFG_NONE && input->use_registers != 0 && u < input->use_offsets[v + 1]; u += 1)
    {
        const uint32_t position = input->use_positions[u];
        const bool at_call = position >= base + 2 * first_call && position < base + 2 * (first_call + calls) && (position - base) % 2 == 0;
        named += at_call && input->use_registers[u] == X86_REGISTER_RDI;
    }
    tier1_emit_test_expect(test, named == calls, "each call's use of its destination names rdi");
    // No lea writes rdi; the last record before the first call that writes rdi is the parallel move's, from d's register.
    uint32_t call = UINT32_MAX;
    uint32_t into = UINT32_MAX;
    uint32_t called = 0;
    bool lea = false;
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        const X86Instruction record = emitted.code.records[r];
        lea |= record.form == X86_FORM_LEA_R64_M && record.reg == X86_REGISTER_RDI;
        uint64_t read = 0;
        uint64_t written = 0;
        x86_instructions_effects(&record, 1, &read, &written);
        into = call == UINT32_MAX && (written & BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RDI)) != 0 ? r : into;
        call = call == UINT32_MAX && record.form == X86_FORM_CALL_RM64 ? r : call;
        called += record.form == X86_FORM_CALL_RM64;
    }
    tier1_emit_test_expect(test, certified && !lea && called == calls && call != UINT32_MAX && into != UINT32_MAX && into < call, "the destination reaches rdi by a move before each call, with no lea");
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (certified && tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
    {
        uint64_t random = UINT64_C(0x2820282028202821) ^ ((uint64_t)shape << 8);
        for (uint32_t sample = 0; sample < 16; sample += 1)
        {
            const int64_t x = sample == 0 ? 0 : sample == 1 ? -1 : sample == 2 ? INT64_MAX : (int64_t)tier1_emit_test_random(&random);
            // Guard words each side of the destination, which the callee must leave alone.
            int64_t storage[10];
            for (uint32_t k = 0; k < 10; k += 1)
            {
                storage[k] = (int64_t)UINT64_C(0xa5a5a5a5a5a5a5a5);
            }
            const Tier1CodeCertificateWide expected = tier1_code_certificate_wide(x);
            const int64_t returned = tier1_code_certificate_forwarded_call(executable, x, storage + 1);
            const bool guarded = storage[0] == (int64_t)UINT64_C(0xa5a5a5a5a5a5a5a5) && storage[9] == (int64_t)UINT64_C(0xa5a5a5a5a5a5a5a5);
            const bool same = memcmp(storage + 1, expected.words, sizeof(expected.words)) == 0 && (shape == 0 || returned == expected.words[0]);
            tier1_emit_test_expect(test, guarded && same, "the call writes its result where the destination points, its bytes alone");
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
    if (certified && into != UINT32_MAX && into < call)
    {
        const bool rejected = tier1_code_certificate_refuses(test, &plan, &emitted, into, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RDI }, UINT32_MAX);
        tier1_emit_test_expect(test, rejected, "the destination's move into rdi left out is refused");
        *refused += rejected;
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// The same call into a 64-byte frame object (#789): its destination is no effective use, a lea of the object writes rdi
// before the call, and the function returns the object's first quadword, f(x)'s.
static void tier1_code_certificate_object_destination(Tier1EmitTest* test)
{
    static const char text[] = "function (i64, ptr) -> i64 {\n"
                               "  passed #0 size=0 loc(1,6,255,0,0)\n"
                               "  abi #0 call(0,4,2,0,ret(2,0,0,255,255,255,255)) passed=#0 result=64\n"
                               "  slot $0 size=64 align=8 type=void flags=0x01\n"
                               "b0:\n"
                               "  %0 = param.i64 0\n"
                               "  %1 = param.ptr 1\n"
                               "  %2 = stack_addr.ptr $0\n"
                               "  %3 = call_indirect.void %1(%0) fixed=(i64) variadic=0 abi=#0 into=%2\n"
                               "  %4 = load.i64 %2 align=8\n"
                               "  %5 = ret.void %4\n"
                               "}\n";
    const String8 source = { .pointer = (char*)text, .length = sizeof(text) - 1 };
    IrFunction function;
    const bool valid = ir_parse(source, &test->input, &test->scratch, &function).error == IR_PARSE_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK;
    tier1_emit_test_expect(test, valid, "the object destination's fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) == TIER1_SELECT_OK;
    const bool used = planned && plan.effective_use_offsets[4] - plan.effective_use_offsets[3] == 2;
    tier1_emit_test_expect(test, used, "a call into a frame object selects, its destination no effective use");
    const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .certify = true };
    Tier1EmitResult emitted = { 0 };
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    const bool certified = error == TIER1_EMIT_OK && emitted.general && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
    bool lea = false;
    for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
    {
        lea |= emitted.code.records[r].form == X86_FORM_LEA_R64_M && emitted.code.records[r].reg == X86_REGISTER_RDI;
    }
    tier1_emit_test_expect(test, certified && lea, "a call into a frame object is certified, a lea of the object writing rdi");
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (certified && tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
    {
        const int64_t x = 0x2820;
        const int64_t returned = tier1_code_certificate_object_call(executable, x);
        tier1_emit_test_expect(test, returned == tier1_code_certificate_wide(x).words[0], "the frame object holds the call's result");
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// An aggregate result in registers (#789) goes where its call's into operand addresses, which must be a frame object's:
// one into a pointer value is refused, its function left to Tier 0 (#2820 takes a hidden pointer's alone), where the
// same call into a 16-byte frame object selects.
static void tier1_code_certificate_registers_into_pointer(Tier1EmitTest* test)
{
    for (uint32_t object = 0; object < 2; object += 1)
    {
        char text[1024];
        const int length = snprintf(text, sizeof(text),
                                    "function (ptr, ptr) -> void {\n"
                                    "  abi #0 call(0,4,0,0,ret(1,0,8,0,2,255,255)) passed=#0 result=16\n"
                                    "%s"
                                    "b0:\n"
                                    "  %%0 = param.ptr 0\n"
                                    "  %%1 = param.ptr 1\n"
                                    "%s"
                                    "  %%%u = call_indirect.void %%0() fixed=() variadic=0 abi=#0 into=%%%u\n"
                                    "  %%%u = ret.void\n"
                                    "}\n",
                                    object ? "  slot $0 size=16 align=8 type=void flags=0x01\n" : "", object ? "  %2 = stack_addr.ptr $0\n" : "", object ? 3u : 2u, object ? 2u : 1u,
                                    object ? 4u : 3u);
        const String8 source = { .pointer = text, .length = (uint64_t)length };
        IrFunction function;
        const bool valid = length > 0 && (size_t)length < sizeof(text) && ir_parse(source, &test->input, &test->scratch, &function).error == IR_PARSE_OK &&
                           ir_verify(&function, &test->scratch, 0).error == IR_OK;
        tier1_emit_test_expect(test, valid, "the registers-into-a-pointer fixture is valid IR");
        IrDominance dominance;
        const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        const Tier1SelectError selection = dominated ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
        tier1_emit_test_expect(test, dominated && selection == (object ? TIER1_SELECT_OK : TIER1_SELECT_ERROR_UNSUPPORTED),
                               "a result in registers into a frame object selects, and into a pointer value is refused");
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->scratch, 0);
        arena_pop_to(&test->input, 0);
    }
}

// The immediates' deferred checks (tier1_cert_emitted_choices): 40 requests of add r64 with an immediate, each held with the
// record its form gives; one held record with another form (an imm32 for a value an imm8 takes) must be refused, with its
// index, wherever it falls: in the first batch, which the 32nd request checks, or among the 8 the final check reads; and
// none when every record is right. Both comparisons (canonical and byte by byte).
static void tier1_code_certificate_choices(Tier1EmitTest* test)
{
    const uint16_t row = (uint16_t)x86_choice_row(X86_OPERATION_ADD, 8);
    const uint16_t context = BUSTER_X86_CONTEXT_REGISTER | BUSTER_X86_FLAGS_STATUS;
    for (uint32_t wrong = 0; wrong <= 40; wrong += 1)
    {
        for (uint32_t canonical = 0; canonical < 2; canonical += 1)
        {
            Tier1CertEmittedChoices choices = { .canonical = canonical != 0 };
            uint32_t failed = UINT32_MAX;
            bool valid = true;
            for (uint32_t i = 0; i < 40; i += 1)
            {
                const int64_t value = (int64_t)i - 20;
                uint16_t form = X86_FORM_NONE;
                x86_choose_forms(&row, &value, &context, 1, &form);
                const uint32_t given = i == wrong ? X86_FORM_ADD_RM64_IMM32 : form;
                const X86Instruction record = x86_choice_instruction(given, (X86Instruction){ .rm = X86_REGISTER_RBX }, value);
                valid = tier1_cert_emitted_choices_defer(&choices, row, value, context, X86_REGISTER_RBX, record, 100 + i, &failed) && valid;
            }
            valid = tier1_cert_emitted_choices_check(&choices, &failed) && valid;
            const bool expected = wrong == 40;
            const bool passed = valid == expected && (expected || failed == 100 + wrong);
            if (!passed)
            {
                printf("  the wrong form at request %u of 40 (%s): valid %d, failed %u\n", wrong, canonical ? "canonical" : "bytes", valid, failed);
            }
            tier1_emit_test_expect(test, passed, "the immediates' deferred checks refuse a wrong form wherever it falls, and only it");
        }
    }
}

// A refusal of a lower class (VECTOR: the code goes uncertified, not refused) behind a held request of a wrong form:
// the held request is the earlier failure and refuses, KERNEL at its record, as it would checked at once. A right form
// held leaves the refusal as given.
static void tier1_code_certificate_choices_held(Tier1EmitTest* test)
{
    const uint16_t row = (uint16_t)x86_choice_row(X86_OPERATION_ADD, 8);
    const uint16_t context = BUSTER_X86_CONTEXT_REGISTER | BUSTER_X86_FLAGS_STATUS;
    const int64_t value = 7;
    uint16_t form = X86_FORM_NONE;
    x86_choose_forms(&row, &value, &context, 1, &form);
    for (uint32_t wrong = 0; wrong < 2; wrong += 1)
    {
        Tier1CertEmittedStats stats = { 0 };
        Tier1CertEmittedBuild build = {
            .stats = &stats,
            .choices = { .canonical = true },
        };
        const X86Instruction record = x86_choice_instruction(wrong != 0 ? X86_FORM_ADD_RM64_IMM32 : form, (X86Instruction){ .rm = X86_REGISTER_RBX }, value);
        uint32_t failed = UINT32_MAX;
        const bool held = tier1_cert_emitted_choices_defer(&build.choices, row, value, context, X86_REGISTER_RBX, record, 5, &failed);
        const bool refused = !tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_VECTOR, 9);
        const bool passed = held && refused && build.choices.count == 0 &&
                            (wrong != 0 ? stats.reason == TIER1_CERT_EMITTED_KERNEL && stats.record == 5 : stats.reason == TIER1_CERT_EMITTED_VECTOR && stats.record == 9);
        if (!passed)
        {
            printf("  a %s form held before a VECTOR refusal: reason %u at record %u\n", wrong != 0 ? "wrong" : "right", (unsigned)stats.reason, stats.record);
        }
        tier1_emit_test_expect(test, passed, "a held request's wrong form refuses before a later, lower refusal");
    }
}

// #1337: frozen locations of register writes, independent of the compaction's tables. Every other resource bit and
// every opmask read is ignored. The full register set crosses every output batch; the mixed set crosses xmm15,
// zmm16 and zmm31, and includes rsp, whose protocol is checked separately. Canary words protect both sides of the
// exact output prefix, including zero writes and the partial last batch.
static void tier1_code_certificate_clobbers_case(Tier1EmitTest* test, bool wide, uint64_t writes, uint16_t masks, const uint32_t* expected, uint32_t expected_count)
{
    uint32_t guarded[BUSTER_CERT_EMITTED_REGISTER_CLOBBERS + 2];
    memset(guarded, 0xa5, sizeof(guarded));
    const uint32_t count = tier1_cert_emitted_clobbers(wide, writes, masks, guarded + 1);
    const bool same = count == expected_count && (count == 0 || memcmp(guarded + 1, expected, count * sizeof(*expected)) == 0);
    bool bounded = count <= BUSTER_CERT_EMITTED_REGISTER_CLOBBERS && guarded[0] == UINT32_C(0xa5a5a5a5);
    for (uint32_t k = count + 1; bounded && k < BUSTER_ARRAY_LENGTH(guarded); k += 1)
    {
        bounded = guarded[k] == UINT32_C(0xa5a5a5a5);
    }
    if (!same || !bounded)
    {
        printf("  clobbers: wide=%u writes=%016" PRIx64 " masks=%04x expected=%u got=%u bounded=%u\n", wide, writes, (unsigned)masks, expected_count, count, bounded);
    }
    tier1_emit_test_expect(test, same && bounded, "register writes have their exact ordered locations and leave the unused output untouched");
}

static void tier1_code_certificate_clobbers(Tier1EmitTest* test)
{
    // Each resource bit's whole-register location in the narrow and wide layouts. UINT32_MAX is no location: rsp,
    // and the high vector registers in a narrow function. The upper sixteen resource bits name no such register.
    static const uint32_t single[2][48] = {
        {
            0,
            2,
            4,
            6,
            UINT32_MAX,
            10,
            12,
            14,
            16,
            18,
            20,
            22,
            24,
            26,
            28,
            30,
            34,
            39,
            44,
            49,
            54,
            59,
            64,
            69,
            74,
            79,
            84,
            89,
            94,
            99,
            104,
            109,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
            UINT32_MAX,
        },
        {
            0,
            2,
            4,
            6,
            UINT32_MAX,
            10,
            12,
            14,
            16,
            18,
            20,
            22,
            24,
            26,
            28,
            30,
            32,
            37,
            42,
            47,
            52,
            57,
            62,
            67,
            72,
            77,
            82,
            87,
            92,
            97,
            102,
            107,
            112,
            117,
            122,
            127,
            132,
            137,
            142,
            147,
            152,
            157,
            162,
            167,
            172,
            177,
            182,
            187,
        },
    };
    static const uint32_t mask_locations[8] = {
        195,
        199,
        203,
        207,
        211,
        215,
        219,
        223,
    };
    static const uint32_t full_narrow[] = {
        0,
        34,
        195,
        2,
        39,
        199,
        4,
        44,
        203,
        6,
        49,
        207,
        54,
        211,
        10,
        59,
        215,
        12,
        64,
        219,
        14,
        69,
        223,
        16,
        74,
        18,
        79,
        20,
        84,
        22,
        89,
        24,
        94,
        26,
        99,
        28,
        104,
        30,
        109,
    };
    static const uint32_t full_wide[] = {
        0,
        32,
        195,
        2,
        37,
        199,
        4,
        42,
        203,
        6,
        47,
        207,
        52,
        211,
        10,
        57,
        215,
        12,
        62,
        219,
        14,
        67,
        223,
        16,
        72,
        18,
        77,
        20,
        82,
        22,
        87,
        24,
        92,
        26,
        97,
        28,
        102,
        30,
        107,
        112,
        117,
        122,
        127,
        132,
        137,
        142,
        147,
        152,
        157,
        162,
        167,
        172,
        177,
        182,
        187,
    };
    static const uint32_t mixed_narrow[] = {
        0,
        34,
        195,
        14,
        69,
        223,
        16,
        74,
        30,
        109,
    };
    static const uint32_t mixed_wide[] = {
        0,
        32,
        195,
        14,
        67,
        223,
        16,
        72,
        30,
        107,
        112,
        187,
    };
    const uint64_t mixed = BUSTER_X86_RESOURCE_GPR(0) | BUSTER_X86_RESOURCE_GPR(4) | BUSTER_X86_RESOURCE_GPR(7) | BUSTER_X86_RESOURCE_GPR(8) | BUSTER_X86_RESOURCE_GPR(15) |
                           BUSTER_X86_RESOURCE_VECTOR(0) | BUSTER_X86_RESOURCE_VECTOR(7) | BUSTER_X86_RESOURCE_VECTOR(8) | BUSTER_X86_RESOURCE_VECTOR(15) |
                           BUSTER_X86_RESOURCE_VECTOR(16) | BUSTER_X86_RESOURCE_VECTOR(31);
    for (uint32_t wide = 0; wide < 2; wide += 1)
    {
        for (uint32_t bit = 0; bit < 64; bit += 1)
        {
            const uint32_t expected = bit < BUSTER_ARRAY_LENGTH(single[0]) ? single[wide][bit] : UINT32_MAX;
            tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(1) << bit, 0, &expected, expected != UINT32_MAX);
        }
        for (uint32_t bit = 0; bit < 16; bit += 1)
        {
            const uint32_t expected = mask_locations[bit & 7];
            tier1_code_certificate_clobbers_case(test, wide != 0, 0, (uint16_t)(1u << bit), &expected, bit >= 8);
        }
        // Exact prefixes around every sixteen-location expansion boundary. The narrow layout ends at 39 locations;
        // the high-vector prefixes are meaningful only in the wide layout.
        const uint32_t* const full = wide != 0 ? full_wide : full_narrow;
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(0x001f002f), UINT16_C(0x1f00), full, 15);
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(0x003f002f), UINT16_C(0x1f00), full, 16);
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(0x003f002f), UINT16_C(0x3f00), full, 17);
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(0x0fff0fef), UINT16_C(0xff00), full, 31);
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(0x0fff1fef), UINT16_C(0xff00), full, 32);
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(0x1fff1fef), UINT16_C(0xff00), full, 33);
        if (wide != 0)
        {
            tier1_code_certificate_clobbers_case(test, true, UINT64_C(0x000000ffffffffef), UINT16_C(0xff00), full, 47);
            tier1_code_certificate_clobbers_case(test, true, UINT64_C(0x000001ffffffffef), UINT16_C(0xff00), full, 48);
            tier1_code_certificate_clobbers_case(test, true, UINT64_C(0x000003ffffffffef), UINT16_C(0xff00), full, 49);
        }
        tier1_code_certificate_clobbers_case(test, wide != 0, 0, 0, NULL, 0);
        tier1_code_certificate_clobbers_case(test, wide != 0, 0, UINT16_C(0x00ff), NULL, 0);
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_C(0xffff000000000010), UINT16_C(0x00ff), NULL, 0);
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_MAX, UINT16_MAX, wide != 0 ? full_wide : full_narrow, wide != 0 ? BUSTER_ARRAY_LENGTH(full_wide) : BUSTER_ARRAY_LENGTH(full_narrow));
        tier1_code_certificate_clobbers_case(test, wide != 0, UINT64_MAX, UINT16_C(0xff00), wide != 0 ? full_wide : full_narrow, wide != 0 ? BUSTER_ARRAY_LENGTH(full_wide) : BUSTER_ARRAY_LENGTH(full_narrow));
        tier1_code_certificate_clobbers_case(test, wide != 0, mixed, UINT16_C(0x81ff), wide != 0 ? mixed_wide : mixed_narrow, wide != 0 ? BUSTER_ARRAY_LENGTH(mixed_wide) : BUSTER_ARRAY_LENGTH(mixed_narrow));
    }
}

// #2808: the mask saved around a gather or scatter must survive its loop's backedge. Check the emitted records'
// clobbers with a second live copy of the same value: preserving the low word must keep its identity, while kmovw's
// zeroed upper bytes cannot retain an incoming dword or qword. A byte view above the low word probes each killed byte.
static Tier1CertError tier1_code_certificate_mask_checked(Tier1EmitTest* test, bool scatter, uint32_t width, uint32_t byte, uint32_t variant)
{
    const X86Instruction kept = { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_R10, .rm = X86_REGISTER_R10 };
    const X86Instruction changed = { .form = X86_FORM_KXORW_K_K_K, .reg = 1, .vvvv = 1, .rm = 1 };
    X86Instruction records[6] = {
        kept,
        { .form = X86_FORM_KMOVW_R32_K, .reg = X86_REGISTER_R11, .rm = 1 },
        { .form = scatter ? X86_FORM_VPSCATTERDD_VM32Z_Z : X86_FORM_VPGATHERDD_Z_VM32Z, .reg = 3, .rm = X86_REGISTER_RDI,
          .index = 4, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 },
        kept,
        { .form = X86_FORM_KMOVW_K_R32, .reg = 1, .rm = X86_REGISTER_R11 },
        kept,
    };
    switch (variant)
    {
        case 1: // The saved word is overwritten before its restore.
            records[3] = (X86Instruction){ .form = X86_FORM_MOV_RM32_R32, .reg = X86_REGISTER_R10, .rm = X86_REGISTER_R11 };
            break;
        case 2: // The mask changes before the save, so the saved word is not its incoming one.
            records[0] = changed;
            break;
        case 3:
            records[1].rm = 2;
            break;
        case 4:
            records[4].rm = X86_REGISTER_R10;
            break;
        case 5:
            records[5] = changed;
            break;
        case 6: // A second save replaces the scratch register's provenance.
            records[3] = (X86Instruction){ .form = X86_FORM_KMOVW_R32_K, .reg = X86_REGISTER_R11, .rm = 2 };
            break;
        case 7: // A mask write after the save is harmless once the saved original is restored.
            records[3] = changed;
            break;
        case 8: // A write of an unrelated general register leaves the saved word intact.
            records[3] = (X86Instruction){ .form = X86_FORM_MOV_RM32_R32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_R10 };
            break;
        default:
            break;
    }
    Tier1CertLocation locations[BUSTER_CERT_EMITTED_REGISTERS + 2] = { 0 };
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        locations[2 * reg] = (Tier1CertLocation){ .unit = 8 * reg, .width = 8, .extent = 8, .class = TIER1_CERT_GPR, .reg = (uint8_t)reg };
        locations[2 * reg + 1] = (Tier1CertLocation){ .unit = 8 * reg, .width = 4, .extent = 8, .class = TIER1_CERT_GPR, .reg = (uint8_t)reg };
    }
    for (uint32_t reg = 0; reg < 32; reg += 1)
    {
        for (uint32_t view = 0; view < 5; view += 1)
        {
            locations[BUSTER_CERT_EMITTED_VECTOR + 5 * reg + view] = (Tier1CertLocation){
                .unit = 128 + 64 * reg, .width = (uint16_t)(64u >> view), .extent = 64, .class = TIER1_CERT_VECTOR, .reg = (uint8_t)reg,
            };
        }
    }
    for (uint32_t reg = 0; reg < 8; reg += 1)
    {
        for (uint32_t view = 0; view < 4; view += 1)
        {
            locations[BUSTER_CERT_EMITTED_MASK + 4 * reg + view] = (Tier1CertLocation){
                .unit = 2176 + 8 * reg, .width = (uint16_t)(1u << view), .extent = 8, .class = TIER1_CERT_MASK, .reg = (uint8_t)reg,
            };
        }
    }
    uint32_t location_count = BUSTER_CERT_EMITTED_REGISTERS;
    uint32_t location = tier1_cert_emitted_mask(1, width);
    if (byte != 0)
    {
        location = location_count++;
        locations[location] = (Tier1CertLocation){ .unit = 2184 + byte, .width = 1, .extent = 1, .class = TIER1_CERT_MASK, .reg = 1 };
    }
    Tier1CertSource source[16] = { 0 };
    Tier1CertInstruction code[16] = { 0 };
    Tier1CertEmittedStats stats = { 0 };
    Tier1CertEmittedBuild build = {
        .stats = &stats,
        .locations = locations,
        .location_count = location_count,
        .location_capacity = BUSTER_ARRAY_LENGTH(locations),
        .base = X86_REGISTER_RSP,
        .source = source,
        .source_capacity = BUSTER_ARRAY_LENGTH(source),
        .code = code,
        .code_capacity = BUSTER_ARRAY_LENGTH(code),
        .frame_pointer = INT64_MIN,
        .frame_unit = 2240,
        .wide = true,
    };
    const IrAsmBody body = { .masks = 2 };
    const Tier1CertEmittedKernel kernel = {
        .uses = { 0 },
        .use_locations = { location },
        .use_count = 1,
        .definition = BUSTER_TIER1_CFG_NONE,
        .body = variant == 9 ? &body : NULL,
        .call = variant == 10,
    };
    const bool built = tier1_cert_emitted_originals(&build, 0, &kernel, records, BUSTER_ARRAY_LENGTH(records));
    const Tier1CertValue value = { .width = (uint16_t)width, .class = TIER1_CERT_MASK };
    const Tier1CertFact facts[2] = {
        { .value = 0, .location = location },
        { .value = 0, .location = tier1_cert_emitted_mask(3, width) },
    };
    const Tier1CertEdge edges[2] = {
        { .destination = 0 },
        { .destination = 1 },
    };
    const Tier1CertBlock blocks[2] = {
        { .source = source, .code = code, .certificate = facts, .edges = edges, .source_count = build.source_count,
          .code_count = build.code_count, .certificate_count = 2, .edge_count = 2 },
        { .certificate = facts, .certificate_count = 2 },
    };
    const Tier1CertProgram program = {
        .locations = locations,
        .values = &value,
        .blocks = blocks,
        .entry = facts,
        .exit = facts,
        .unit_count = 2240,
        .location_count = build.location_count,
        .value_count = 1,
        .block_count = 2,
        .entry_count = 2,
        .exit_count = 2,
        .flags_location = BUSTER_CERT_NONE,
    };
    const uint64_t saved = test->scratch.length;
    const size_t bytes = built ? tier1_certificate_scratch_size(&program) : 0;
    void* scratch = bytes != 0 ? arena_push(&test->scratch, bytes, 64) : NULL;
    Tier1CertReport report = { 0 };
    const Tier1CertError error = scratch != NULL ? tier1_certificate_check(&program, scratch, bytes, &report) : TIER1_CERT_CAPACITY;
    arena_pop_to(&test->scratch, saved);
    return error;
}

static void tier1_code_certificate_masks(Tier1EmitTest* test)
{
    uint32_t certified = 0;
    uint32_t refused = 0;
    for (uint32_t scatter = 0; scatter < 2; scatter += 1)
    {
        for (uint32_t width = 1; width <= 8; width *= 2)
        {
            for (uint32_t variant = 0; variant < 11; variant += 1)
            {
                const bool kept = width <= 2 && (variant == 0 || variant == 7 || variant == 8);
                const Tier1CertError error = tier1_code_certificate_mask_checked(test, scatter != 0, width, 0, variant);
                const bool passed = error == (kept ? TIER1_CERT_OK : TIER1_CERT_EDGE);
                if (!passed)
                {
                    printf("  %s mask width %u variant %u: certificate error %u, expected %u\n", scatter != 0 ? "scatter" : "gather", width, variant,
                           error, kept ? TIER1_CERT_OK : TIER1_CERT_EDGE);
                }
                tier1_emit_test_expect(test, passed, "only the original saved mask's low word survives the loop backedge, with its other live copy");
                certified += passed && kept;
                refused += passed && !kept;
            }
        }
        for (uint32_t byte = 2; byte < 8; byte += 1)
        {
            const bool rejected = tier1_code_certificate_mask_checked(test, scatter != 0, 1, byte, 0) == TIER1_CERT_EDGE;
            tier1_emit_test_expect(test, rejected, "each upper mask byte cleared by kmovw loses its incoming fact");
            refused += rejected;
        }
    }
    printf("tier1 code certificate: gather and scatter masks %u of 12 low-word preservations certified, %u of 88 overwritten facts refused\n", certified, refused);
}

static void tier1_code_certificate_tests(Tier1EmitTest* test)
{
    Tier1CodeCertificateCounts counts = { 0 };
    tier1_code_certificate_clobbers(test);
    tier1_code_certificate_choices(test);
    tier1_code_certificate_choices_held(test);
    tier1_code_certificate_masks(test);
    tier1_code_certificate_aggregate_copy(test);
    tier1_code_certificate_frame_copy(test);
    for (uint32_t variant = 0; variant < 4; variant += 1)
    {
        tier1_code_certificate_carry(test, (variant & 1) != 0, (variant & 2) != 0);
    }
    for (uint32_t variant = 0; variant < 4; variant += 1)
    {
        tier1_code_certificate_compile(test, (variant & 1) != 0, (variant & 2) != 0, &counts);
    }
    tier1_code_certificate_debug(test);
    tier1_code_certificate_debug_straight(test);
    tier1_code_certificate_debug_immediate(test);
    tier1_code_certificate_debug_phis(test);
    tier1_code_certificate_saved(test, &counts);
    tier1_code_certificate_narrow(test, &counts);
    tier1_code_certificate_exchange(test, &counts);
    tier1_code_certificate_store(test);
    tier1_code_certificate_copy_targets(test);
    tier1_code_certificate_realigned(test, &counts);
    for (uint32_t pointer = 0; pointer < 2; pointer += 1)
    {
        tier1_code_certificate_barrier(test, pointer != 0, &counts);
    }
    tier1_emit_test_expect(test, counts.barrier == 2, "the barrier fixture's compiles are certified, with and without a frame pointer");
    printf("tier1 code certificate: barriers in asm bodies %u of 2 certified, aimed at %u rows: writes that keep the row %u of %u certified, writes that change it %u of %u refused\n", counts.barrier,
           counts.barrier_rows, counts.barrier_kept_certified, counts.barrier_kept, counts.barrier_changed_refused, counts.barrier_changed);
    uint32_t literals_refused = 0;
    tier1_code_certificate_literals(test, &literals_refused);
    tier1_emit_test_expect(test, literals_refused == 5, "every corrupted literal of the compile is refused");
    uint32_t forwarded_refused = 0;
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        tier1_code_certificate_forwarded(test, shape, &forwarded_refused);
    }
    tier1_emit_test_expect(test, forwarded_refused == 4, "every compile's destination move into rdi left out is refused (#2820)");
    tier1_code_certificate_registers_into_pointer(test);
    tier1_code_certificate_object_destination(test);
    tier1_emit_test_expect(test, counts.realigned == 1 && counts.late == 1, "the realigned-frame fixture's compile pushes a register past rbp, and its allocation moves before it");
    tier1_emit_test_expect(test, counts.narrow == 1 && counts.narrow_register == 1 && counts.narrow_stack == 1, "the narrow-parameter fixture's compile has an extension in a register and one from a slot");
    tier1_emit_test_expect(test, counts.exchange == 1 && counts.exchange_live == 1, "the compare-exchange fixture's compile restores rax around the exchange");
    const bool kept = tier1_code_certificate_walked(false);
    const bool clobbered = tier1_code_certificate_walked(true);
    tier1_emit_test_expect(test, kept, "a conditional tail whose edge move keeps the flags reaches its true successor");
    tier1_emit_test_expect(test, !clobbered, "a conditional jump after an edge move that writes the flags fails the code's certificate: it reads the move's flags");
    tier1_emit_test_expect(test, counts.certified == 4 && counts.second != 0 && counts.saves != 0 && counts.untouched != 0 && counts.lowered == 4 && counts.pointers == 2 && counts.saved_moved == 1,
                           "the fixtures' compiles have a second argument's move, saves and restores to corrupt, one of a register only the callee may change, rows and frame pointers to move");
    printf("tier1 code certificate: %u compiles certified; argument moves stale %u of %u refused (into rsi %u of %u); saves and restores left out %u of %u refused; a store and load around the call left out %u of %u refused; "
           "rows below rsp %u of %u refused; frame pointers set after the allocation %u of %u refused; dynamic frames %u of 1 certified, their frame pointers set after the allocation %u of %u refused; a conditional jump after an edge's "
           "flag-writing move %s; narrow parameters %u of 1 certified, their extensions corrupted in a register %u of %u and from a slot %u of %u refused; compare-exchanges %u of 1 certified, "
           "their restores of rax corrupted %u of %u refused; realigned frames %u of 1 certified, their allocation moved before their pushes %u of %u refused\n",
           counts.certified, counts.arguments_refused, counts.arguments, counts.second_refused, counts.second, counts.saves_refused, counts.saves, counts.untouched_refused, counts.untouched, counts.lowered_refused,
           counts.lowered, counts.pointers_refused, counts.pointers, counts.saved, counts.saved_refused, counts.saved_moved, kept && !clobbered ? "refused" : "accepted", counts.narrow,
           counts.narrow_register_refused, counts.narrow_register, counts.narrow_stack_refused, counts.narrow_stack, counts.exchange, counts.exchange_restores_refused, counts.exchange_restores, counts.realigned,
           counts.late_refused, counts.late);
}
