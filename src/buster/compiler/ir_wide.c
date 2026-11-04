// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's 128-bit integers (#141): a function's I128 values split into pairs of I64 halves, low and high, so that
// selection, allocation and their checkers see 64-bit values alone. The C driver's --tier1 runs it between SSA promotion
// and the optimizer (c_check_tier1.c), whose SCCP, dead code and CFG cleanup then take the halves. Include after ir.c.
//
// Each I128 instruction becomes its halves' instructions, in its place, so that blocks keep their numbers, a block's
// phis stay first and each half is defined where its value was:
// - a constant, an undef, a copy, a select, and, or, xor and not: each half's own;
// - an addition: the low halves' sum, and the high halves' plus its carry, the low sum's unsigned compare below an
//   operand; a subtraction likewise with its borrow;
// - a product, modulo 2^128 whatever the signedness: the low halves' product and its high half (IR_INTRINSIC's mulhu, x86's mul),
//   plus each cross product of a low half by a high one that isn't known to be zero. A product of two zero extensions,
//   C's (unsigned __int128)a * b, is mul's two halves alone;
// - a negation: each half negated, the high one less 1 where the low one isn't zero;
// - a shift by a constant (through the count's extensions, truncations and copies): shifts of the halves and an or, a
//   half alone past 64 bits, or the other half itself at 64, for no instruction; by a variable count, both halves shifted by its low 6 bits, the bits crossing between them by
//   the complement's count (two shifts, so that a count of 0 shifts by no more than 63), and selects on its bit 6;
// - a comparison: equality from the halves' xors, ored; an order from the high halves' (strict, of the predicate's
//   signedness) where they differ, else the low halves' (unsigned);
// - a zero or sign extension: the value as the low half (a 64-bit one itself), and zero or its sign as the high half;
//   a truncation, the low half (to 64 bits, the half itself); a pointer's conversions, through the low half;
// - a load and a store: two 8-byte accesses, the high half's address 8 bytes on (its own address's offset plus 8 where
//   the pointer is an address); a phi: one of each half, with the same predecessors.
// A half that is another value itself (a 64-bit truncation's, an extension's low half, a shift by 64's) is no
// instruction: its uses read that value (an alias, resolved before the rewrite). A shift by a non-constant count of a
// narrower value reads the count's low half. Division and remainder, and conversions to and from floating values, reach
// the IR as libgcc's calls (c_check_program_lower.c), whose 128-bit arguments and results are by-value aggregates, ptrs
// to their bytes: an I128 among a function's or a call's types, an I128 operation this doesn't split (a division, a
// conversion to or from a floating value, a switch, a bitcast to another type) refuses it (IR_ERROR_UNSUPPORTED).
//
// The rewrite is two passes of the same code: the first counts the output and places each value (its halves' outputs, or
// the values they alias), the second writes the columns. Tables are copied whole, the new constants and addresses
// appended; each phi's predecessor list (each half's, for an I128 phi) and each call's arguments are appended to the
// extra table, their values remapped. The counts are 64-bit, and an output whose instructions or tables a u32 can't
// count is refused before it is allocated (IR_ERROR_LIMIT): phis can share a predecessor list, so the appended lists
// can pass the limit where the source's table doesn't (#1204's review). SIMD: what pays. Every function --tier1 compiles is scanned for an I128 instruction, 64 type bytes
// a compare on the vector path, one at a time on its scalar twin, which must agree; a function without one is returned
// as it is. The rewrite is scalar: it runs on functions with 128-bit values alone, two of sqlite3.c's 2,668 and none of
// Lua's 1,088 (docs/tier1.md has its cost against the compile's).

#ifndef BUSTER_IR_WIDE_C
#define BUSTER_IR_WIDE_C

// A half's place: an output instruction, or the low or the high half of another source value (an alias).
#define BUSTER_IR_WIDE_OUTPUT 0u
#define BUSTER_IR_WIDE_LOW 1u
#define BUSTER_IR_WIDE_HIGH 2u

typedef struct IrWideResult IrWideResult;
struct IrWideResult
{
    // The split function, or the input itself (borrowed, its columns the input's) where it has no I128 instruction.
    IrFunction function;
    // The source's I128 instructions, and whether the function was rewritten.
    uint32_t wide;
    bool split;
};

typedef struct IrWideWork IrWideWork;
struct IrWideWork
{
    const IrFunction* source;
    // The output's columns, or 0 while counting.
    IrFunction* output;
    // Each source value's halves' places: an output instruction, or (kinds) the low or high half of the source value they
    // name. A value narrower than 128 bits is its low half alone. The counting pass places them; the writing pass reads
    // them resolved.
    uint32_t* low;
    uint32_t* high;
    uint8_t* low_kinds;
    uint8_t* high_kinds;
    // The output's counts so far. 64-bit, so that the counting pass sees a count past a u32's rather than its wrap: the
    // pass checks them after each source instruction, which adds at most 4 * 2^32 extra words, and refuses the output
    // past UINT32_MAX (IR_ERROR_LIMIT). The writing pass's counts are then below it, and the u32 ids taken from them
    // exact; the counting pass's ids past it are never written.
    uint64_t instruction;
    uint64_t constant;
    uint64_t address;
    uint64_t extra;
    IrError error;
    uint32_t error_instruction;
};

// Whether a function has an I128 instruction: its type column 64 bytes a compare on the vector path, one at a time on its
// scalar twin, which must agree.
static bool ir_wide_present_path(const IrFunction* function, bool vector)
{
    (void)vector;

    bool found = false;
    if (BUSTER_VECTOR_PATH(vector))
    {
        for (uint32_t first = 0; !found && first < function->instruction_count; first += 64)
        {
            const uint32_t count = function->instruction_count - first < 64 ? function->instruction_count - first : 64;
            const __mmask64 active = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const __m512i types = _mm512_maskz_loadu_epi8(active, function->type + first);
            found = _mm512_mask_cmpeq_epi8_mask(active, types, _mm512_set1_epi8(IR_I128)) != 0;
        }
    }
    else
    {
        for (uint32_t i = 0; !found && i < function->instruction_count; i += 1)
        {
            found = function->type[i] == IR_I128;
        }
    }
    return found;
}

// Whether the function's signature or a call's has an I128 (by-value 128-bit integers are aggregates, ptrs to their
// bytes, so none should), which the split refuses.
static bool ir_wide_signature(const IrFunction* function)
{
    bool found = function->result_type == IR_I128;
    for (uint32_t p = 0; !found && p < function->parameter_count; p += 1)
    {
        found = function->parameter_types[p] == IR_I128;
    }
    for (uint32_t p = 0; !found && p < function->call_type_count; p += 1)
    {
        found = function->call_types[p] == IR_I128;
    }
    for (uint32_t k = 0; !found && k < function->call_count; k += 1)
    {
        found = function->calls[k].result_type == IR_I128;
    }
    return found;
}

static uint32_t ir_wide_emit(IrWideWork* work, uint32_t op, uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t id = (uint32_t)work->instruction;
    if (work->output != 0)
    {
        IrFunction* output = work->output;
        output->op[id] = (uint8_t)op;
        output->type[id] = (uint8_t)type;
        output->flags[id] = 0;
        output->alignment_log2[id] = 0;
        output->a[id] = a;
        output->b[id] = b;
        output->c[id] = c;
    }
    work->instruction += 1;
    return id;
}

// A 64-bit constant, its own entry appended to the table.
static uint32_t ir_wide_constant(IrWideWork* work, uint64_t value)
{
    const uint32_t entry = (uint32_t)work->constant;
    if (work->output != 0)
    {
        work->output->constants[entry] = (IrConstant){ .low = value };
    }
    work->constant += 1;
    return ir_wide_emit(work, IR_CONST, IR_I64, entry, BUSTER_IR_NONE, BUSTER_IR_NONE);
}

// A value's half, as resolved: the output instruction it is. Only the writing pass reads it.
static uint32_t ir_wide_half(const IrWideWork* work, uint32_t value, bool high)
{
    return high ? work->high[value] : work->low[value];
}

static uint32_t ir_wide_low(const IrWideWork* work, uint32_t value)
{
    return ir_wide_half(work, value, false);
}

static uint32_t ir_wide_high(const IrWideWork* work, uint32_t value)
{
    return ir_wide_half(work, value, true);
}

// The counting pass's places of `value`'s halves; the writing pass, which makes the same instructions, keeps them.
static void ir_wide_place(IrWideWork* work, uint32_t value, uint32_t low, uint32_t high)
{
    if (work->output == 0)
    {
        work->low[value] = low;
        work->high[value] = high;
        work->low_kinds[value] = BUSTER_IR_WIDE_OUTPUT;
        work->high_kinds[value] = BUSTER_IR_WIDE_OUTPUT;
    }
}

// A half of `value` that is another value's half: `kind` BUSTER_IR_WIDE_LOW or _HIGH of `target`.
static void ir_wide_alias(IrWideWork* work, uint32_t value, bool high, uint32_t target, uint32_t kind)
{
    if (work->output == 0 && high)
    {
        work->high[value] = target;
        work->high_kinds[value] = (uint8_t)kind;
    }
    else if (work->output == 0)
    {
        work->low[value] = target;
        work->low_kinds[value] = (uint8_t)kind;
    }
}

// Whether an I128 value's high half is zero by its own definition: a zero extension's, a pointer's, a constant's. Its
// definition alone, which both passes read the same whatever the blocks' order.
static bool ir_wide_zero_high(const IrFunction* f, uint32_t value)
{
    const uint32_t op = f->op[value];
    return op == IR_ZEXT || op == IR_PTRTOINT || (op == IR_CONST && f->constants[f->a[value]].high == 0);
}

// The memory access's flags and alignment, a half's: at most 8 bytes' alignment.
static void ir_wide_memory(IrWideWork* work, uint32_t id, uint32_t source)
{
    if (work->output != 0)
    {
        work->output->flags[id] = work->source->flags[source];
        work->output->alignment_log2[id] = work->source->alignment_log2[source] < 3 ? work->source->alignment_log2[source] : 3;
    }
}

// The high half's address: 8 bytes past `pointer`, whose own offset takes them where it is an address.
static uint32_t ir_wide_high_address(IrWideWork* work, uint32_t pointer)
{
    const IrFunction* f = work->source;
    uint32_t base = pointer;
    uint32_t index = BUSTER_IR_NONE;
    IrAddress address = { .offset = 8, .scale = 1 };
    int64_t offset = 0;
    if (f->op[pointer] == IR_ADDRESS && !__builtin_add_overflow(f->addresses[f->c[pointer]].offset, 8, &offset))
    {
        base = f->a[pointer];
        index = f->b[pointer];
        address = (IrAddress){ .offset = offset, .scale = f->addresses[f->c[pointer]].scale };
    }
    const uint32_t entry = (uint32_t)work->address;
    if (work->output != 0)
    {
        work->output->addresses[entry] = address;
    }
    work->address += 1;
    return ir_wide_emit(work, IR_ADDRESS, IR_PTR, ir_wide_low(work, base), index == BUSTER_IR_NONE ? BUSTER_IR_NONE : ir_wide_low(work, index), entry);
}

// Whether a shift's count is a constant, through a few extensions, truncations and copies (the typed stream's count
// of `r >> 64` is its class's zero extension of a constant): its value, whose bits past each type on the way to the
// constant are dropped, into *k, 0 to 127 (a count past 127 is undefined: its low 7 bits).
static bool ir_wide_constant_count(const IrFunction* f, uint32_t count, uint32_t* k)
{
    uint64_t mask = UINT64_MAX;
    uint32_t value = count;
    for (uint32_t step = 0; step < 4 && (f->op[value] == IR_ZEXT || f->op[value] == IR_SEXT || f->op[value] == IR_TRUNC || f->op[value] == IR_COPY); step += 1)
    {
        const uint32_t bits = ir_type_bits[f->type[value]];
        mask &= bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX;
        value = f->a[value];
    }
    const bool constant = f->op[value] == IR_CONST;
    if (constant)
    {
        const uint32_t bits = ir_type_bits[f->type[value]];
        mask &= bits < 64 ? (UINT64_C(1) << bits) - 1 : UINT64_MAX;
        *k = (uint32_t)(f->constants[f->a[value]].low & mask & 127);
    }
    return constant;
}

// A shift's count as an I64 (a narrower one zero-extended, a 128-bit one's low half).
static uint32_t ir_wide_count(IrWideWork* work, uint32_t count)
{
    const uint32_t type = work->source->type[count];
    return type == IR_I64 || type == IR_I128 ? ir_wide_low(work, count) : ir_wide_emit(work, IR_ZEXT, IR_I64, ir_wide_low(work, count), BUSTER_IR_NONE, BUSTER_IR_NONE);
}

// An I128 shift of `value` (instruction i) by the constant k, 0 to 127.
static void ir_wide_shift_constant(IrWideWork* work, uint32_t i, uint32_t op, uint32_t value, uint32_t k)
{
    const uint32_t low = ir_wide_low(work, value);
    const uint32_t high = ir_wide_high(work, value);
    if (k == 0)
    {
        ir_wide_alias(work, i, false, value, BUSTER_IR_WIDE_LOW);
        ir_wide_alias(work, i, true, value, BUSTER_IR_WIDE_HIGH);
    }
    else if (k < 64)
    {
        const uint32_t count = ir_wide_constant(work, k);
        const uint32_t complement = ir_wide_constant(work, 64 - k);
        if (op == IR_SHL)
        {
            const uint32_t out_low = ir_wide_emit(work, IR_SHL, IR_I64, low, count, BUSTER_IR_NONE);
            const uint32_t shifted = ir_wide_emit(work, IR_SHL, IR_I64, high, count, BUSTER_IR_NONE);
            const uint32_t crossing = ir_wide_emit(work, IR_LSHR, IR_I64, low, complement, BUSTER_IR_NONE);
            ir_wide_place(work, i, out_low, ir_wide_emit(work, IR_OR, IR_I64, shifted, crossing, BUSTER_IR_NONE));
        }
        else
        {
            const uint32_t shifted = ir_wide_emit(work, IR_LSHR, IR_I64, low, count, BUSTER_IR_NONE);
            const uint32_t crossing = ir_wide_emit(work, IR_SHL, IR_I64, high, complement, BUSTER_IR_NONE);
            const uint32_t out_low = ir_wide_emit(work, IR_OR, IR_I64, shifted, crossing, BUSTER_IR_NONE);
            ir_wide_place(work, i, out_low, ir_wide_emit(work, op, IR_I64, high, count, BUSTER_IR_NONE));
        }
    }
    else if (op == IR_SHL)
    {
        const uint32_t zero = ir_wide_constant(work, 0);
        if (k == 64)
        {
            ir_wide_place(work, i, zero, 0);
            ir_wide_alias(work, i, true, value, BUSTER_IR_WIDE_LOW);
        }
        else
        {
            const uint32_t count = ir_wide_constant(work, k - 64);
            ir_wide_place(work, i, zero, ir_wide_emit(work, IR_SHL, IR_I64, low, count, BUSTER_IR_NONE));
        }
    }
    else
    {
        // The high half: zero, or the sign's copies.
        const uint32_t fill = op == IR_LSHR ? ir_wide_constant(work, 0) : ir_wide_emit(work, IR_ASHR, IR_I64, high, ir_wide_constant(work, 63), BUSTER_IR_NONE);
        if (k == 64)
        {
            ir_wide_place(work, i, 0, fill);
            ir_wide_alias(work, i, false, value, BUSTER_IR_WIDE_HIGH);
        }
        else
        {
            const uint32_t count = ir_wide_constant(work, k - 64);
            ir_wide_place(work, i, ir_wide_emit(work, op, IR_I64, high, count, BUSTER_IR_NONE), fill);
        }
    }
}

// An I128 shift of `value` (instruction i) by the I64 count: by its low 6 bits s, and by 64 more where its bit 6 is set.
// The bits crossing between the halves are shifted by 1 and then by 63 - s, so that no shift's count reaches 64.
static void ir_wide_shift_variable(IrWideWork* work, uint32_t i, uint32_t op, uint32_t value, uint32_t count)
{
    const uint32_t low = ir_wide_low(work, value);
    const uint32_t high = ir_wide_high(work, value);
    const uint32_t bits = ir_wide_constant(work, 63);
    const uint32_t s = ir_wide_emit(work, IR_AND, IR_I64, count, bits, BUSTER_IR_NONE);
    const uint32_t far = ir_wide_emit(work, IR_AND, IR_I64, count, ir_wide_constant(work, 64), BUSTER_IR_NONE);
    const uint32_t zero = ir_wide_constant(work, 0);
    const uint32_t big = ir_wide_emit(work, IR_ICMP, IR_I1, far, zero, IR_NE);
    const uint32_t complement = ir_wide_emit(work, IR_XOR, IR_I64, s, bits, BUSTER_IR_NONE);
    const uint32_t one = ir_wide_constant(work, 1);
    if (op == IR_SHL)
    {
        const uint32_t shifted_low = ir_wide_emit(work, IR_SHL, IR_I64, low, s, BUSTER_IR_NONE);
        const uint32_t shifted_high = ir_wide_emit(work, IR_SHL, IR_I64, high, s, BUSTER_IR_NONE);
        const uint32_t half = ir_wide_emit(work, IR_LSHR, IR_I64, low, one, BUSTER_IR_NONE);
        const uint32_t crossing = ir_wide_emit(work, IR_LSHR, IR_I64, half, complement, BUSTER_IR_NONE);
        const uint32_t near = ir_wide_emit(work, IR_OR, IR_I64, shifted_high, crossing, BUSTER_IR_NONE);
        const uint32_t out_low = ir_wide_emit(work, IR_SELECT, IR_I64, big, zero, shifted_low);
        ir_wide_place(work, i, out_low, ir_wide_emit(work, IR_SELECT, IR_I64, big, shifted_low, near));
    }
    else
    {
        const uint32_t shifted_low = ir_wide_emit(work, IR_LSHR, IR_I64, low, s, BUSTER_IR_NONE);
        const uint32_t shifted_high = ir_wide_emit(work, op, IR_I64, high, s, BUSTER_IR_NONE);
        const uint32_t half = ir_wide_emit(work, IR_SHL, IR_I64, high, one, BUSTER_IR_NONE);
        const uint32_t crossing = ir_wide_emit(work, IR_SHL, IR_I64, half, complement, BUSTER_IR_NONE);
        const uint32_t near = ir_wide_emit(work, IR_OR, IR_I64, shifted_low, crossing, BUSTER_IR_NONE);
        const uint32_t fill = op == IR_LSHR ? zero : ir_wide_emit(work, IR_ASHR, IR_I64, high, bits, BUSTER_IR_NONE);
        const uint32_t out_low = ir_wide_emit(work, IR_SELECT, IR_I64, big, shifted_high, near);
        ir_wide_place(work, i, out_low, ir_wide_emit(work, IR_SELECT, IR_I64, big, fill, shifted_high));
    }
}

// An I128 comparison's truth value (instruction i, an I1).
static uint32_t ir_wide_compare(IrWideWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const uint32_t predicate = f->c[i];
    uint32_t result = 0;
    if (predicate == IR_EQ || predicate == IR_NE)
    {
        const uint32_t low = ir_wide_emit(work, IR_XOR, IR_I64, ir_wide_low(work, a), ir_wide_low(work, b), BUSTER_IR_NONE);
        const uint32_t high = ir_wide_emit(work, IR_XOR, IR_I64, ir_wide_high(work, a), ir_wide_high(work, b), BUSTER_IR_NONE);
        const uint32_t either = ir_wide_emit(work, IR_OR, IR_I64, low, high, BUSTER_IR_NONE);
        result = ir_wide_emit(work, IR_ICMP, IR_I1, either, ir_wide_constant(work, 0), predicate);
    }
    else
    {
        // The high halves' strict order of the predicate's signedness, and the low halves' unsigned one of its strictness.
        static const uint8_t strict[IR_PREDICATE_COUNT] = { IR_EQ, IR_NE, IR_LT, IR_LT, IR_GT, IR_GT, IR_ULT, IR_ULT, IR_UGT, IR_UGT };
        static const uint8_t unsigned_order[IR_PREDICATE_COUNT] = { IR_EQ, IR_NE, IR_ULT, IR_ULE, IR_UGT, IR_UGE, IR_ULT, IR_ULE, IR_UGT, IR_UGE };
        const uint32_t high = ir_wide_emit(work, IR_ICMP, IR_I1, ir_wide_high(work, a), ir_wide_high(work, b), strict[predicate]);
        const uint32_t equal = ir_wide_emit(work, IR_ICMP, IR_I1, ir_wide_high(work, a), ir_wide_high(work, b), IR_EQ);
        const uint32_t low = ir_wide_emit(work, IR_ICMP, IR_I1, ir_wide_low(work, a), ir_wide_low(work, b), unsigned_order[predicate]);
        result = ir_wide_emit(work, IR_SELECT, IR_I1, equal, low, high);
    }
    return result;
}

// An I128 instruction i's halves. Returns false for one this doesn't split.
static bool ir_wide_value(IrWideWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t op = f->op[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const uint32_t c = f->c[i];
    const uint32_t none = BUSTER_IR_NONE;
    bool supported = true;
    switch (op)
    {
        case IR_CONST:
        {
            const IrConstant value = f->constants[a];
            const uint32_t low = ir_wide_constant(work, value.low);
            ir_wide_place(work, i, low, ir_wide_constant(work, value.high));
        } break;
        case IR_UNDEF:
        {
            const uint32_t low = ir_wide_emit(work, IR_UNDEF, IR_I64, none, none, none);
            ir_wide_place(work, i, low, ir_wide_emit(work, IR_UNDEF, IR_I64, none, none, none));
        } break;
        case IR_COPY:
        case IR_BITCAST:
        {
            supported = f->type[a] == IR_I128;
            const uint32_t low = ir_wide_emit(work, IR_COPY, IR_I64, ir_wide_low(work, a), none, none);
            ir_wide_place(work, i, low, ir_wide_emit(work, IR_COPY, IR_I64, ir_wide_high(work, a), none, none));
        } break;
        case IR_AND:
        case IR_OR:
        case IR_XOR:
        {
            const uint32_t low = ir_wide_emit(work, op, IR_I64, ir_wide_low(work, a), ir_wide_low(work, b), none);
            ir_wide_place(work, i, low, ir_wide_emit(work, op, IR_I64, ir_wide_high(work, a), ir_wide_high(work, b), none));
        } break;
        case IR_NOT:
        {
            const uint32_t low = ir_wide_emit(work, IR_NOT, IR_I64, ir_wide_low(work, a), none, none);
            ir_wide_place(work, i, low, ir_wide_emit(work, IR_NOT, IR_I64, ir_wide_high(work, a), none, none));
        } break;
        case IR_ADD:
        {
            const uint32_t low = ir_wide_emit(work, IR_ADD, IR_I64, ir_wide_low(work, a), ir_wide_low(work, b), none);
            const uint32_t carried = ir_wide_emit(work, IR_ICMP, IR_I1, low, ir_wide_low(work, a), IR_ULT);
            const uint32_t carry = ir_wide_emit(work, IR_ZEXT, IR_I64, carried, none, none);
            const bool zero_a = ir_wide_zero_high(f, a);
            const bool zero_b = ir_wide_zero_high(f, b);
            uint32_t high = carry;
            if (!zero_a && !zero_b)
            {
                high = ir_wide_emit(work, IR_ADD, IR_I64, ir_wide_high(work, a), ir_wide_high(work, b), none);
                high = ir_wide_emit(work, IR_ADD, IR_I64, high, carry, none);
            }
            else if (!zero_a || !zero_b)
            {
                high = ir_wide_emit(work, IR_ADD, IR_I64, ir_wide_high(work, zero_a ? b : a), carry, none);
            }
            ir_wide_place(work, i, low, high);
        } break;
        case IR_SUB:
        {
            const uint32_t low = ir_wide_emit(work, IR_SUB, IR_I64, ir_wide_low(work, a), ir_wide_low(work, b), none);
            const uint32_t borrowed = ir_wide_emit(work, IR_ICMP, IR_I1, ir_wide_low(work, a), ir_wide_low(work, b), IR_ULT);
            const uint32_t borrow = ir_wide_emit(work, IR_ZEXT, IR_I64, borrowed, none, none);
            const uint32_t minuend = ir_wide_zero_high(f, b) ? ir_wide_high(work, a) : ir_wide_emit(work, IR_SUB, IR_I64, ir_wide_high(work, a), ir_wide_high(work, b), none);
            ir_wide_place(work, i, low, ir_wide_emit(work, IR_SUB, IR_I64, minuend, borrow, none));
        } break;
        case IR_MUL:
        {
            const uint32_t low = ir_wide_emit(work, IR_MUL, IR_I64, ir_wide_low(work, a), ir_wide_low(work, b), none);
            uint32_t high = ir_wide_emit(work, IR_INTRINSIC, IR_I64, ir_wide_low(work, a), ir_wide_low(work, b), BUSTER_IR_INTRINSIC_MULHU);
            if (!ir_wide_zero_high(f, b))
            {
                const uint32_t cross = ir_wide_emit(work, IR_MUL, IR_I64, ir_wide_low(work, a), ir_wide_high(work, b), none);
                high = ir_wide_emit(work, IR_ADD, IR_I64, high, cross, none);
            }
            if (!ir_wide_zero_high(f, a))
            {
                const uint32_t cross = ir_wide_emit(work, IR_MUL, IR_I64, ir_wide_high(work, a), ir_wide_low(work, b), none);
                high = ir_wide_emit(work, IR_ADD, IR_I64, high, cross, none);
            }
            ir_wide_place(work, i, low, high);
        } break;
        case IR_NEG:
        {
            const uint32_t low = ir_wide_emit(work, IR_NEG, IR_I64, ir_wide_low(work, a), none, none);
            const uint32_t nonzero = ir_wide_emit(work, IR_ICMP, IR_I1, ir_wide_low(work, a), ir_wide_constant(work, 0), IR_NE);
            const uint32_t borrow = ir_wide_emit(work, IR_ZEXT, IR_I64, nonzero, none, none);
            const uint32_t negated = ir_wide_emit(work, IR_NEG, IR_I64, ir_wide_high(work, a), none, none);
            ir_wide_place(work, i, low, ir_wide_emit(work, IR_SUB, IR_I64, negated, borrow, none));
        } break;
        case IR_SHL:
        case IR_LSHR:
        case IR_ASHR:
        {
            uint32_t k = 0;
            if (ir_wide_constant_count(f, b, &k))
            {
                ir_wide_shift_constant(work, i, op, a, k);
            }
            else
            {
                ir_wide_shift_variable(work, i, op, a, ir_wide_count(work, b));
            }
        } break;
        case IR_SELECT:
        {
            const uint32_t condition = ir_wide_low(work, a);
            const uint32_t low = ir_wide_emit(work, IR_SELECT, IR_I64, condition, ir_wide_low(work, b), ir_wide_low(work, c));
            ir_wide_place(work, i, low, ir_wide_emit(work, IR_SELECT, IR_I64, condition, ir_wide_high(work, b), ir_wide_high(work, c)));
        } break;
        case IR_ZEXT:
        case IR_SEXT:
        {
            if (f->type[a] == IR_I64)
            {
                ir_wide_place(work, i, 0, 0);
                ir_wide_alias(work, i, false, a, BUSTER_IR_WIDE_LOW);
            }
            else
            {
                ir_wide_place(work, i, ir_wide_emit(work, op, IR_I64, ir_wide_low(work, a), none, none), 0);
            }
            // The high half, an output instruction (ir_wide_place's kind).
            const uint32_t sign = op == IR_SEXT ? ir_wide_constant(work, 63) : 0;
            const uint32_t low = f->type[a] == IR_I64 ? ir_wide_low(work, a) : ir_wide_low(work, i);
            const uint32_t high = op == IR_ZEXT ? ir_wide_constant(work, 0) : ir_wide_emit(work, IR_ASHR, IR_I64, low, sign, none);
            if (work->output == 0)
            {
                work->high[i] = high;
            }
        } break;
        case IR_PTRTOINT:
        {
            const uint32_t low = ir_wide_emit(work, IR_PTRTOINT, IR_I64, ir_wide_low(work, a), none, none);
            ir_wide_place(work, i, low, ir_wide_constant(work, 0));
        } break;
        case IR_LOAD:
        {
            const uint32_t low = ir_wide_emit(work, IR_LOAD, IR_I64, ir_wide_low(work, a), none, none);
            ir_wide_memory(work, low, i);
            const uint32_t high = ir_wide_emit(work, IR_LOAD, IR_I64, ir_wide_high_address(work, a), none, none);
            ir_wide_memory(work, high, i);
            ir_wide_place(work, i, low, high);
        } break;
        case IR_PHI:
        {
            // Each half's predecessor list is appended, its values remapped there: the verifier lets extra's slices
            // overlap (a call's arguments sharing a phi's words), so a remap in the source's place would rewrite another
            // reader's entries (#1204's review).
            const uint32_t low_list = (uint32_t)work->extra;
            const uint32_t high_list = (uint32_t)(work->extra + (uint64_t)b * 2);
            const uint32_t low = ir_wide_emit(work, IR_PHI, IR_I64, low_list, b, none);
            const uint32_t high = ir_wide_emit(work, IR_PHI, IR_I64, high_list, b, none);
            for (uint32_t p = 0; work->output != 0 && p < b; p += 1)
            {
                const uint32_t block = f->extra[a + p * 2];
                const uint32_t value = f->extra[a + p * 2 + 1];
                work->output->extra[low_list + p * 2] = block;
                work->output->extra[low_list + p * 2 + 1] = ir_wide_low(work, value);
                work->output->extra[high_list + p * 2] = block;
                work->output->extra[high_list + p * 2 + 1] = ir_wide_high(work, value);
            }
            work->extra += (uint64_t)b * 4;
            ir_wide_place(work, i, low, high);
        } break;
        default: supported = false; break;
    }
    return supported;
}

// A narrower instruction i that reads an I128 operand. Returns false for one this doesn't split.
static bool ir_wide_use(IrWideWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t op = f->op[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const uint32_t none = BUSTER_IR_NONE;
    bool supported = true;
    if (op == IR_ICMP)
    {
        ir_wide_place(work, i, ir_wide_compare(work, i), 0);
    }
    else if (op == IR_TRUNC && f->type[i] == IR_I64)
    {
        ir_wide_alias(work, i, false, a, BUSTER_IR_WIDE_LOW);
    }
    else if (op == IR_TRUNC || op == IR_INTTOPTR)
    {
        ir_wide_place(work, i, ir_wide_emit(work, op, f->type[i], ir_wide_low(work, a), none, none), 0);
    }
    else if (op == IR_STORE)
    {
        const uint32_t low = ir_wide_emit(work, IR_STORE, IR_VOID, ir_wide_low(work, a), ir_wide_low(work, b), none);
        ir_wide_memory(work, low, i);
        const uint32_t address = ir_wide_high_address(work, a);
        const uint32_t high = ir_wide_emit(work, IR_STORE, IR_VOID, address, ir_wide_high(work, b), none);
        ir_wide_memory(work, high, i);
        ir_wide_place(work, i, low, 0);
    }
    else if ((op == IR_SHL || op == IR_LSHR || op == IR_ASHR) && f->type[a] != IR_I128)
    {
        // A narrower value's count, which is below its width where the shift is defined: the low half holds it.
        ir_wide_place(work, i, ir_wide_emit(work, op, f->type[i], ir_wide_low(work, a), ir_wide_low(work, b), none), 0);
    }
    else
    {
        supported = false;
    }
    return supported;
}

// Any other instruction, its value operands remapped. A phi's list and a call's arguments are remapped into slices of
// their own, appended, never in the source's place: the verifier lets extra's slices overlap (a call's arguments
// sharing a phi's words), and a remap in place would rewrite another reader's entries (#1204's review).
static void ir_wide_copy(IrWideWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t op = f->op[i];
    const bool phi = op == IR_PHI;
    const bool call = op == IR_CALL || op == IR_CALL_INDIRECT;
    const uint32_t list = (uint32_t)work->extra;
    work->extra += phi ? (uint64_t)f->b[i] * 2 : call ? f->calls[f->b[i]].argument_count : 0;
    const uint32_t mask = ir_value_mask(f, i);
    const uint32_t a = phi ? list : (mask & 1) != 0 ? ir_wide_low(work, f->a[i]) : f->a[i];
    const uint32_t b = (mask & 2) != 0 ? ir_wide_low(work, f->b[i]) : f->b[i];
    const uint32_t c = (mask & 4) != 0 ? ir_wide_low(work, f->c[i]) : f->c[i];
    const uint32_t id = ir_wide_emit(work, op, f->type[i], a, b, c);
    if (work->output != 0)
    {
        IrFunction* output = work->output;
        output->flags[id] = f->flags[i];
        output->alignment_log2[id] = f->alignment_log2[i];
        if (phi)
        {
            for (uint32_t p = 0; p < f->b[i]; p += 1)
            {
                output->extra[list + p * 2] = f->extra[f->a[i] + p * 2];
                output->extra[list + p * 2 + 1] = ir_wide_low(work, f->extra[f->a[i] + p * 2 + 1]);
            }
        }
        else if (call)
        {
            // The call's record points at its own arguments' slice. Calls that share a record remap its arguments
            // alike, each into a slice of its own, the last one the record's.
            const IrCall source = f->calls[f->b[i]];
            output->calls[f->b[i]].argument_first = list;
            for (uint32_t p = 0; p < source.argument_count; p += 1)
            {
                output->extra[list + p] = ir_wide_low(work, f->extra[source.argument_first + p]);
            }
        }
    }
    ir_wide_place(work, i, id, 0);
}

// Whether instruction i reads an I128 operand.
static bool ir_wide_reads(const IrFunction* f, uint32_t i)
{
    const uint32_t mask = ir_value_mask(f, i);
    bool wide = ((mask & 1) != 0 && f->type[f->a[i]] == IR_I128) || ((mask & 2) != 0 && f->type[f->b[i]] == IR_I128) || ((mask & 4) != 0 && f->type[f->c[i]] == IR_I128);
    if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
    {
        const IrCall call = f->calls[f->b[i]];
        for (uint32_t p = 0; !wide && p < call.argument_count; p += 1)
        {
            wide = f->type[f->extra[call.argument_first + p]] == IR_I128;
        }
    }
    return wide;
}

// One pass over the source's instructions, block by block: the counting one (work->output 0) or the writing one.
static void ir_wide_pass(IrWideWork* work)
{
    const IrFunction* f = work->source;
    work->instruction = 0;
    work->constant = f->constant_count;
    work->address = f->address_count;
    work->extra = f->extra_count;
    for (uint32_t block = 0; work->error == IR_OK && block < f->block_count; block += 1)
    {
        if (work->output != 0)
        {
            work->output->block_offsets[block] = (uint32_t)work->instruction;
        }
        for (uint32_t i = f->block_offsets[block]; work->error == IR_OK && i < f->block_offsets[block + 1]; i += 1)
        {
            bool supported = true;
            if (f->type[i] == IR_I128)
            {
                supported = ir_wide_value(work, i);
            }
            else if (ir_wide_reads(f, i))
            {
                supported = ir_wide_use(work, i);
            }
            else
            {
                ir_wide_copy(work, i);
            }
            if (!supported)
            {
                work->error = IR_ERROR_UNSUPPORTED;
                work->error_instruction = i;
            }
            else if (work->instruction > UINT32_MAX || work->constant > UINT32_MAX || work->address > UINT32_MAX || work->extra > UINT32_MAX)
            {
                work->error = IR_ERROR_LIMIT;
                work->error_instruction = i;
            }
        }
    }
    if (work->output != 0 && work->error == IR_OK)
    {
        work->output->block_offsets[f->block_count] = (uint32_t)work->instruction;
    }
}

// Each half's alias resolved to the output instruction it names: chains are a value's definitions' order in SSA, so
// each ends in at most as many links as the function has values.
static bool ir_wide_resolve(IrWideWork* work)
{
    const uint32_t count = work->source->instruction_count;
    bool resolved = true;
    for (uint32_t v = 0; resolved && v < count; v += 1)
    {
        for (uint32_t half = 0; resolved && half < 2; half += 1)
        {
            uint32_t* place = half == 0 ? &work->low[v] : &work->high[v];
            uint8_t* kind = half == 0 ? &work->low_kinds[v] : &work->high_kinds[v];
            uint32_t steps = 0;
            while (*kind != BUSTER_IR_WIDE_OUTPUT && steps <= count)
            {
                const uint32_t target = *place;
                const bool high = *kind == BUSTER_IR_WIDE_HIGH;
                *place = high ? work->high[target] : work->low[target];
                *kind = high ? work->high_kinds[target] : work->low_kinds[target];
                steps += 1;
            }
            resolved = *kind == BUSTER_IR_WIDE_OUTPUT;
        }
    }
    return resolved;
}

static IrCheck ir_wide_split_path(const IrFunction* function, Arena* output, Arena* scratch, bool vector, IrWideResult* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    *result = (IrWideResult){ .function = *function };
    const bool present = output != scratch && ir_wide_present_path(function, vector);
    if (output == scratch)
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    else if (ir_wide_signature(function))
    {
        check.error = IR_ERROR_UNSUPPORTED;
    }
    if (check.error == IR_OK && present)
    {
        const uint32_t count = function->instruction_count;
        bool ok = true;
        IrWideWork work = {
            .source = function,
            .low = ir_allocate(scratch, count, sizeof(uint32_t), &ok),
            .high = ir_allocate(scratch, count, sizeof(uint32_t), &ok),
            .low_kinds = ir_allocate(scratch, count, 1, &ok),
            .high_kinds = ir_allocate(scratch, count, 1, &ok),
        };
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        if (check.error == IR_OK)
        {
            ir_wide_pass(&work);
            check = ir_check(work.error, work.error_instruction, 0);
        }
        if (check.error == IR_OK && !ir_wide_resolve(&work))
        {
            check.error = IR_ERROR_OPERAND;
        }
        IrFunction split = { 0 };
        if (check.error == IR_OK)
        {
            const IrSizes sizes = {
                .instruction_count = work.instruction,
                .block_count = function->block_count,
                .extra_count = work.extra,
                .constant_count = work.constant,
                .slot_count = function->slot_count,
                .address_count = work.address,
                .call_count = function->call_count,
                .call_type_count = function->call_type_count,
                .parameter_count = function->parameter_count,
                .label_count = function->label_count,
                .abi_call_count = function->abi_call_count,
                .passed_count = function->passed_count,
                .vector_constant_count = function->vector_constant_count,
            };
            check.error = ir_create(output, &sizes, &split);
        }
        if (check.error == IR_OK)
        {
            split.result_type = function->result_type;
            split.symbol_count = function->symbol_count;
            split.parameter_passed = function->parameter_passed;
            split.result_size = function->result_size;
            split.result_abi = function->result_abi;
            split.variadic = function->variadic;
            split.incoming = function->incoming;
            // The tables whole (memcpy of none would take ir_create's null column), the new constants and addresses after.
            if (function->extra_count != 0)
            {
                memcpy(split.extra, function->extra, (uint64_t)function->extra_count * sizeof(uint32_t));
            }
            if (function->constant_count != 0)
            {
                memcpy(split.constants, function->constants, (uint64_t)function->constant_count * sizeof(IrConstant));
            }
            if (function->slot_count != 0)
            {
                memcpy(split.slots, function->slots, (uint64_t)function->slot_count * sizeof(IrSlot));
            }
            if (function->address_count != 0)
            {
                memcpy(split.addresses, function->addresses, (uint64_t)function->address_count * sizeof(IrAddress));
            }
            if (function->call_count != 0)
            {
                memcpy(split.calls, function->calls, (uint64_t)function->call_count * sizeof(IrCall));
            }
            if (function->call_type_count != 0)
            {
                memcpy(split.call_types, function->call_types, function->call_type_count);
            }
            if (function->parameter_count != 0)
            {
                memcpy(split.parameter_types, function->parameter_types, function->parameter_count);
            }
            if (function->label_count != 0)
            {
                memcpy(split.labels, function->labels, (uint64_t)function->label_count * sizeof(uint32_t));
            }
            if (function->abi_call_count != 0)
            {
                memcpy(split.abi_calls, function->abi_calls, (uint64_t)function->abi_call_count * sizeof(IrAbiCall));
            }
            if (function->passed_count != 0)
            {
                memcpy(split.passed, function->passed, (uint64_t)function->passed_count * sizeof(IrPassed));
            }
            if (function->vector_constant_count != 0)
            {
                memcpy(split.vector_constants, function->vector_constants, (uint64_t)function->vector_constant_count * sizeof(IrSimdValue));
            }
            const uint64_t instructions = work.instruction;
            work.output = &split;
            ir_wide_pass(&work);
            check = ir_check(work.instruction == instructions ? work.error : IR_ERROR_STRUCTURE, work.error_instruction, 0);
        }
        if (check.error == IR_OK)
        {
            uint32_t wide = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                wide += function->type[i] == IR_I128;
            }
            *result = (IrWideResult){ .function = split, .wide = wide, .split = true };
        }
    }
    if (check.error != IR_OK)
    {
        *result = (IrWideResult){ 0 };
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}

static __attribute__((unused)) IrCheck ir_wide_split(const IrFunction* function, Arena* output, Arena* scratch, IrWideResult* result)
{
#if !BUSTER_HOST_VECTOR
    return ir_wide_split_path(function, output, scratch, false, result);
#else
    return ir_wide_split_path(function, output, scratch, true, result);
#endif
}

static __attribute__((unused)) IrCheck ir_wide_split_scalar(const IrFunction* function, Arena* output, Arena* scratch, IrWideResult* result)
{
    return ir_wide_split_path(function, output, scratch, false, result);
}

#endif
