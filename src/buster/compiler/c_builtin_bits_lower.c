// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's bit builtins' recipes (c_check_builtin_bits.c's), which c_builtin_misc_lower.c includes and its appends
// publish, after c_builtin_lower.c's primitives. An argument of up to 64 bits arrives in a local of its descriptor's class
// (cbuiltin_lower_type_class, a _BitInt's value extended from its width in it), one of 65 to 128 bits in a 16-byte frame
// object whose two U64 limbs the recipe loads (an __int128's, or a _BitInt's extended from its width), a carry chain's
// carry as its U64 pointer. A result of up to 64 bits leaves its descriptor's class, a signed _BitInt's extended from its
// width; a wider one is written to its frame object (CBuiltinMiscLowerCall.result_object), its high limb extended from
// its width, and read back as one I128 or U128 value.
//
// The recipes compute on U64 temporaries, a value zero-extended from its width, as clang's code computes on its iN:
// popcounts and bit counts by SWAR (cbuiltin_lower_popcount, a smear for the leading zeros), reversals by masked swaps
// of halves, and selections by multiplying with a compare's 0 or 1, without branches. A wide value counts limb by limb
// (the second limb's count added where the first limb is zero), reverses and swaps its bytes limb by limb, the limbs then
// exchanged and shifted down past its width, and rotates by 128-bit shifts of variable counts: each limb shifted by the
// count's part below 64, the bits that cross between them carried (shifted by one, then by 63 less the part, so that a
// part of 0 carries nothing), a whole limb moved where the count is 64 or more. A rotation's count is its value modulo
// the width, a negative one's mathematical remainder: a mask for a width that is a power of two, a remainder for other
// _BitInts', and a wide count's remainder from its limbs' (2^64's and 2^128's remainders constants of the width).
// A carry chain adds or subtracts twice, each carry a compare of the wrapped sum, and stores their OR at its pointer, a
// T of 0 or 1, before its result, as clang's code does. clzs, ctzs, and clzg and ctzg without their second argument,
// leave the width for zero, which clang's code leaves undefined.
#ifndef BUSTER_C_BUILTIN_BITS_LOWER_INCLUDED
#define BUSTER_C_BUILTIN_BITS_LOWER_INCLUDED

// A descriptor's width: BUSTER_CBUILTIN_LOWER_WIDE's 128.
static uint32_t cbuiltin_bits_width(uint32_t type)
{
    const uint32_t width = type & ~BUSTER_CBUILTIN_SIGNED;
    return width == BUSTER_CBUILTIN_LOWER_WIDE ? 128 : width;
}

// A descriptor's value class: one of up to 64 bits its slot's (cbuiltin_lower_type_class), a wider one's I128 or U128.
static uint32_t cbuiltin_bits_class(uint32_t type)
{
    const uint32_t width = cbuiltin_bits_width(type);
    const uint32_t wide = (type & BUSTER_CBUILTIN_SIGNED) != 0 ? BUSTER_TYPED_CLASS_I128 : BUSTER_TYPED_CLASS_U128;
    return width <= 64 ? cbuiltin_lower_type_class(type) : width <= 128 ? wide : BUSTER_TYPED_CLASS_VOID;
}

static void cbuiltin_bits_mask(CBuiltinLowerRecipe* recipe, uint32_t width)
{
    if (width < 64)
    {
        cbuiltin_lower_constant(recipe, cbuiltin_mask(width));
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    }
}

// The value on the stack, of `width` bits, extended from them to 64: by its sign's, or with zeros.
static void cbuiltin_bits_extend(CBuiltinLowerRecipe* recipe, uint32_t width, bool is_signed)
{
    cbuiltin_bits_mask(recipe, width);
    if (is_signed && width < 64)
    {
        const uint64_t sign = UINT64_C(1) << (width - 1);
        cbuiltin_lower_constant(recipe, sign);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_XOR);
        cbuiltin_lower_constant(recipe, sign);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    }
}

// An argument's bits, zero-extended from its width, in a new temporary.
static uint32_t cbuiltin_bits_load(CBuiltinLowerRecipe* recipe, uint32_t local, uint32_t type)
{
    const uint32_t x = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, cbuiltin_lower_type_class(type), local);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_bits_mask(recipe, cbuiltin_bits_width(type));
    cbuiltin_lower_set(recipe, x);
    return x;
}

// A wide argument's limbs, its object's two words, in new temporaries: the high one's bits past the width cleared, or
// with `extend` extended from them by the value's sign.
static void cbuiltin_bits_load_wide(CBuiltinLowerRecipe* recipe, uint32_t object, uint32_t width, bool extend, uint32_t* low, uint32_t* high)
{
    *low = cbuiltin_lower_temporary(recipe);
    *high = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, *low);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    cbuiltin_lower_constant(recipe, 8);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_bits_extend(recipe, width - 64, extend);
    cbuiltin_lower_set(recipe, *high);
}

static uint32_t cbuiltin_bits_copy(CBuiltinLowerRecipe* recipe, uint32_t x)
{
    const uint32_t copy = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_set(recipe, copy);
    return copy;
}

// A new temporary of a | b: a wide value's limbs joined, nonzero where the value is.
static uint32_t cbuiltin_bits_either(CBuiltinLowerRecipe* recipe, uint32_t a, uint32_t b)
{
    const uint32_t either = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_binary(recipe, either, BUSTER_TYPED_OR, a, b);
    return either;
}

static void cbuiltin_bits_invert(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t width)
{
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_NOT);
    cbuiltin_bits_mask(recipe, width);
    cbuiltin_lower_set(recipe, x);
}

// x's bits below its highest set one set too: a value of `width` bits' bit width's ones.
static void cbuiltin_bits_smear(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t temporary, uint32_t width)
{
    for (uint32_t shift = 1; shift < width; shift *= 2)
    {
        cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, shift);
        cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_OR, x, temporary);
    }
}

// x's leading zeros in `width` bits, the width for 0.
static void cbuiltin_bits_leading(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t temporary, uint32_t width)
{
    cbuiltin_bits_smear(recipe, x, temporary, width);
    cbuiltin_lower_popcount(recipe, x, temporary);
    cbuiltin_lower_constant(recipe, width);
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_set(recipe, x);
}

// x's trailing zeros in `width` bits, the width for 0: the ones below its lowest set bit, counted.
static void cbuiltin_bits_trailing(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t temporary, uint32_t width)
{
    cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SUB, x, 1);
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_NOT);
    cbuiltin_lower_local(recipe, temporary);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_bits_mask(recipe, width);
    cbuiltin_lower_set(recipe, x);
    cbuiltin_lower_popcount(recipe, x, temporary);
}

// A wide value's leading (or trailing) zeros in `width` bits, in a new temporary: the first limb's (the high one's of
// width - 64 bits, or the low one's), plus the second's where the first is zero, the width for 0.
static uint32_t cbuiltin_bits_count_wide(CBuiltinLowerRecipe* recipe, uint32_t low, uint32_t high, uint32_t width, bool trailing)
{
    const uint32_t first = cbuiltin_bits_copy(recipe, trailing ? low : high);
    const uint32_t second = cbuiltin_bits_copy(recipe, trailing ? high : low);
    const uint32_t temporary = cbuiltin_lower_temporary(recipe);
    if (trailing)
    {
        cbuiltin_bits_trailing(recipe, first, temporary, 64);
        cbuiltin_bits_trailing(recipe, second, temporary, width - 64);
    }
    else
    {
        cbuiltin_bits_leading(recipe, first, temporary, width - 64);
        cbuiltin_bits_leading(recipe, second, temporary, 64);
    }
    cbuiltin_lower_local(recipe, first);
    cbuiltin_lower_local(recipe, second);
    cbuiltin_lower_local(recipe, trailing ? low : high);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_set(recipe, first);
    return first;
}

// x = (x + 1) * (copy != 0): the stdc first_ ones' position, 0 where no bit is.
static void cbuiltin_bits_position(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t copy)
{
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_constant(recipe, 1);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_local(recipe, copy);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    cbuiltin_lower_set(recipe, x);
}

// x = x + (fallback - x) * (copy == 0): clzg's and ctzg's second argument, an int's I32, for zero.
static void cbuiltin_bits_fallback(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t copy, uint32_t fallback)
{
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, fallback);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_local(recipe, copy);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_set(recipe, x);
}

// x's halves of `first` bits and up swapped within each lane of `width` bits: a power of two's swaps of halves smaller
// than it leave its lanes reversed in place; another width's are those of 64 bits, then shifted down.
static void cbuiltin_bits_swaps(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t temporary, uint32_t width, uint32_t first)
{
    static const uint64_t masks[6] = {
        UINT64_C(0x5555555555555555),
        UINT64_C(0x3333333333333333),
        UINT64_C(0x0f0f0f0f0f0f0f0f),
        UINT64_C(0x00ff00ff00ff00ff),
        UINT64_C(0x0000ffff0000ffff),
        UINT64_C(0x00000000ffffffff),
    };
    const bool power = (width & (width - 1)) == 0;
    for (uint32_t stage = first; stage < 6 && (!power || (1u << stage) < width); stage += 1)
    {
        const uint32_t shift = 1u << stage;
        cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, shift);
        cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_AND, temporary, masks[stage]);
        cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_AND, x, masks[stage]);
        cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_SHL, x, shift);
        cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_OR, x, temporary);
    }
    if (!power)
    {
        cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_SHR, x, 64 - width);
    }
}

// A wide value's limbs, reversed (or byte swapped) whole: each limb's swaps (cbuiltin_bits_swaps of 64 bits from stage
// `first`), the limbs exchanged, then the 128 bits shifted down past the value's width.
static void cbuiltin_bits_reverse_wide(CBuiltinLowerRecipe* recipe, uint32_t* low, uint32_t* high, uint32_t width, uint32_t first)
{
    const uint32_t temporary = cbuiltin_lower_temporary(recipe);
    const uint32_t reversed_low = *high;
    const uint32_t reversed_high = *low;
    const uint32_t shift = 128 - width;
    cbuiltin_bits_swaps(recipe, reversed_low, temporary, 64, first);
    cbuiltin_bits_swaps(recipe, reversed_high, temporary, 64, first);
    if (shift != 0)
    {
        cbuiltin_lower_local(recipe, reversed_low);
        cbuiltin_lower_constant(recipe, shift);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
        cbuiltin_lower_local(recipe, reversed_high);
        cbuiltin_lower_constant(recipe, 64 - shift);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHL);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
        cbuiltin_lower_set(recipe, reversed_low);
        cbuiltin_lower_immediate(recipe, reversed_high, BUSTER_TYPED_SHR, reversed_high, shift);
    }
    *low = reversed_low;
    *high = reversed_high;
}

// A count's value modulo the power of two or other width `width`, into `amount`. A count of up to 64 bits is its local's
// value extended to 64 bits by its signedness, whose two's complement a power of two's mask reduces, and a signed
// remainder's sign corrects for another width. A wider one's is its limbs' (the high one extended from its width): its
// low limb masked for a power of two, or else the remainder of high * 2^64 + low from the limbs' remainders, a negative
// one's plus the width less 2^128's remainder.
static void cbuiltin_bits_amount(CBuiltinLowerRecipe* recipe, uint32_t amount, uint32_t local, uint32_t type, uint32_t width)
{
    const bool power = (width & (width - 1)) == 0;
    const bool is_signed = (type & BUSTER_CBUILTIN_SIGNED) != 0;
    const uint32_t count_width = cbuiltin_bits_width(type);
    if (count_width > 64)
    {
        uint32_t low = 0;
        uint32_t high = 0;
        cbuiltin_bits_load_wide(recipe, local, count_width, is_signed, &low, &high);
        if (power)
        {
            cbuiltin_lower_immediate(recipe, amount, BUSTER_TYPED_AND, low, width - 1);
        }
        else
        {
            const uint64_t limb = (uint64_t)(((unsigned __int128)1 << 64) % width);
            const uint64_t whole = (limb * limb) % width;
            cbuiltin_lower_local(recipe, high);
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_MOD);
            cbuiltin_lower_constant(recipe, limb);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
            cbuiltin_lower_local(recipe, low);
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_MOD);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
            if (is_signed)
            {
                cbuiltin_lower_local(recipe, high);
                cbuiltin_lower_constant(recipe, 63);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHR);
                cbuiltin_lower_constant(recipe, (width - whole) % width);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
            }
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_MOD);
            cbuiltin_lower_set(recipe, amount);
        }
    }
    else
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, cbuiltin_lower_type_class(type), local);
        if (power)
        {
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_constant(recipe, width - 1);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        }
        else if (is_signed)
        {
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, width);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_MOD, BUSTER_TYPED_CLASS_I64, 0);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_MOD);
        }
        else
        {
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_MOD);
        }
        cbuiltin_lower_set(recipe, amount);
    }
}

// (width - amount) modulo the width, into `output`.
static void cbuiltin_bits_complement(CBuiltinLowerRecipe* recipe, uint32_t output, uint32_t amount, uint32_t width)
{
    const bool power = (width & (width - 1)) == 0;
    cbuiltin_lower_constant(recipe, width);
    cbuiltin_lower_local(recipe, amount);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_constant(recipe, power ? width - 1 : width);
    cbuiltin_lower_operation(recipe, power ? BUSTER_TYPED_AND : BUSTER_TYPED_MOD);
    cbuiltin_lower_set(recipe, output);
}

// output = a * (1 - whole) + b * whole, of `whole`'s 0 or 1; without b (UINT32_MAX), a * (1 - whole).
static void cbuiltin_bits_choose(CBuiltinLowerRecipe* recipe, uint32_t output, uint32_t a, uint32_t b, uint32_t whole)
{
    cbuiltin_lower_local(recipe, a);
    cbuiltin_lower_local(recipe, whole);
    cbuiltin_lower_constant(recipe, 1);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_XOR);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    if (b != UINT32_MAX)
    {
        cbuiltin_lower_local(recipe, b);
        cbuiltin_lower_local(recipe, whole);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    }
    cbuiltin_lower_set(recipe, output);
}

// A wide value's limbs shifted left (or right) by `amount`, of 0 to 127 bits, into new temporaries: each limb by the
// amount's part below 64, the bits that cross from one to the other carried, then a whole limb moved where the amount is
// 64 or more.
static void cbuiltin_bits_shift_wide(CBuiltinLowerRecipe* recipe, uint32_t low, uint32_t high, uint32_t amount, bool left, uint32_t* output_low,
                                     uint32_t* output_high)
{
    const uint32_t part = cbuiltin_lower_temporary(recipe);
    const uint32_t whole = cbuiltin_lower_temporary(recipe);
    const uint32_t carry = cbuiltin_lower_temporary(recipe);
    const uint32_t near = cbuiltin_lower_temporary(recipe);
    const uint32_t far = cbuiltin_lower_temporary(recipe);
    const uint32_t kind = left ? BUSTER_TYPED_SHL : BUSTER_TYPED_SHR;
    const uint32_t back = left ? BUSTER_TYPED_SHR : BUSTER_TYPED_SHL;
    cbuiltin_lower_immediate(recipe, part, BUSTER_TYPED_AND, amount, 63);
    cbuiltin_lower_immediate(recipe, whole, BUSTER_TYPED_SHR, amount, 6);
    // The bits that cross: the low limb's top ones going left, the high limb's bottom ones going right.
    cbuiltin_lower_local(recipe, left ? low : high);
    cbuiltin_lower_constant(recipe, 1);
    cbuiltin_lower_operation(recipe, back);
    cbuiltin_lower_constant(recipe, 63);
    cbuiltin_lower_local(recipe, part);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_operation(recipe, back);
    cbuiltin_lower_set(recipe, carry);
    // `near` is the limb shifted away from the other (the low one going left), `far` the other with the carry.
    cbuiltin_lower_binary(recipe, near, kind, left ? low : high, part);
    cbuiltin_lower_local(recipe, left ? high : low);
    cbuiltin_lower_local(recipe, part);
    cbuiltin_lower_operation(recipe, kind);
    cbuiltin_lower_local(recipe, carry);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
    cbuiltin_lower_set(recipe, far);
    cbuiltin_bits_choose(recipe, far, far, near, whole);
    cbuiltin_bits_choose(recipe, near, near, UINT32_MAX, whole);
    *output_low = left ? near : far;
    *output_high = left ? far : near;
}

// The one bit at `k` of a wide value of `width` bits, into `low` and `high`: none where k is the width or more.
static void cbuiltin_bits_one_wide(CBuiltinLowerRecipe* recipe, uint32_t k, uint32_t width, uint32_t low, uint32_t high)
{
    const uint32_t one = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_constant(recipe, 1);
    cbuiltin_lower_local(recipe, k);
    cbuiltin_lower_constant(recipe, 63);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SHL);
    cbuiltin_lower_set(recipe, one);
    cbuiltin_lower_local(recipe, one);
    cbuiltin_lower_local(recipe, k);
    cbuiltin_lower_constant(recipe, 64);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    cbuiltin_lower_set(recipe, low);
    cbuiltin_lower_local(recipe, one);
    cbuiltin_lower_local(recipe, k);
    cbuiltin_lower_constant(recipe, 64);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_GE);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    cbuiltin_lower_local(recipe, k);
    cbuiltin_lower_constant(recipe, width);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    cbuiltin_lower_set(recipe, high);
}

// A wide result's limbs written to its frame object, the high one extended from its width, then read back as one value
// of its class.
static void cbuiltin_bits_store_wide(CBuiltinLowerRecipe* recipe, uint32_t object, uint32_t low, uint32_t high, uint32_t type)
{
    cbuiltin_lower_local(recipe, high);
    cbuiltin_bits_extend(recipe, cbuiltin_bits_width(type) - 64, (type & BUSTER_CBUILTIN_SIGNED) != 0);
    cbuiltin_lower_set(recipe, high);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    cbuiltin_lower_local(recipe, low);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
    cbuiltin_lower_constant(recipe, 8);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    cbuiltin_lower_local(recipe, high);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, cbuiltin_bits_class(type), object);
}

// A carry chain: x + y + carry (or x - y - carry) in the width of T, each step's carry its wrapped sum's compare, their
// OR stored at the pointer as a T, then the result.
static void cbuiltin_bits_carry(CBuiltinLowerRecipe* recipe, CBuiltinId id, const uint32_t* arguments, uint32_t type)
{
    const uint32_t width = cbuiltin_bits_width(type);
    const bool add = id <= CBUILTIN_ADDCLL;
    const uint32_t kind = add ? BUSTER_TYPED_ADD : BUSTER_TYPED_SUB;
    const uint32_t x = cbuiltin_bits_load(recipe, arguments[0], type);
    const uint32_t y = cbuiltin_bits_load(recipe, arguments[1], type);
    const uint32_t carry = cbuiltin_bits_load(recipe, arguments[2], type);
    const uint32_t first = cbuiltin_lower_temporary(recipe);
    const uint32_t second = cbuiltin_lower_temporary(recipe);
    const uint32_t early = cbuiltin_lower_temporary(recipe);
    const uint32_t late = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_local(recipe, x);
    cbuiltin_lower_local(recipe, y);
    cbuiltin_lower_operation(recipe, kind);
    cbuiltin_bits_mask(recipe, width);
    cbuiltin_lower_set(recipe, first);
    cbuiltin_lower_binary(recipe, early, BUSTER_TYPED_LT, add ? first : x, add ? x : y);
    cbuiltin_lower_local(recipe, first);
    cbuiltin_lower_local(recipe, carry);
    cbuiltin_lower_operation(recipe, kind);
    cbuiltin_bits_mask(recipe, width);
    cbuiltin_lower_set(recipe, second);
    cbuiltin_lower_binary(recipe, late, BUSTER_TYPED_LT, add ? second : first, add ? first : carry);
    cbuiltin_lower_binary(recipe, early, BUSTER_TYPED_OR, early, late);
    cbuiltin_lower_local(recipe, arguments[3]);
    cbuiltin_lower_local(recipe, early);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, cbuiltin_lower_type_class(type), 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, cbuiltin_lower_type_class(type), 0);
    cbuiltin_lower_local(recipe, second);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, cbuiltin_lower_type_class(type), 0);
}

// The result's bits in x, as a value of its descriptor's class: a signed _BitInt's extended from its width.
static void cbuiltin_bits_result(CBuiltinLowerRecipe* recipe, uint32_t x, uint32_t type)
{
    cbuiltin_lower_local(recipe, x);
    if (!cbuiltin_lower_type_whole(type))
    {
        cbuiltin_bits_extend(recipe, cbuiltin_bits_width(type), (type & BUSTER_CBUILTIN_SIGNED) != 0);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, cbuiltin_lower_type_class(type), 0);
}

// Whether a builtin's operand is complemented in its width first: the ones' counts are the zeros' of the complement.
static bool cbuiltin_bits_inverted(CBuiltinId id)
{
    return id == CBUILTIN_STDC_LEADING_ONES || id == CBUILTIN_STDC_TRAILING_ONES || id == CBUILTIN_STDC_FIRST_LEADING_ZERO ||
           id == CBUILTIN_STDC_FIRST_TRAILING_ZERO;
}

// A builtin of a wide first operand (65 to 128 bits), limb by limb: a count leaves its result's class, a value (a
// reversal, a byte swap, bit_floor, bit_ceil, a rotation) is written to `object` and read back.
static void cbuiltin_bits_wide(CBuiltinLowerRecipe* recipe, CBuiltinId id, const uint32_t* arguments, const uint32_t* types, uint32_t count, uint32_t result,
                               uint32_t object)
{
    const uint32_t width = cbuiltin_bits_width(types[0]);
    const bool leading = id == CBUILTIN_CLZG || id == CBUILTIN_STDC_LEADING_ZEROS || id == CBUILTIN_STDC_LEADING_ONES ||
                         id == CBUILTIN_STDC_FIRST_LEADING_ZERO || id == CBUILTIN_STDC_FIRST_LEADING_ONE || id == CBUILTIN_STDC_BIT_WIDTH;
    const bool trailing = id == CBUILTIN_CTZG || id == CBUILTIN_STDC_TRAILING_ZEROS || id == CBUILTIN_STDC_TRAILING_ONES ||
                          id == CBUILTIN_STDC_FIRST_TRAILING_ZERO || id == CBUILTIN_STDC_FIRST_TRAILING_ONE;
    const bool first = id >= CBUILTIN_STDC_FIRST_LEADING_ZERO && id <= CBUILTIN_STDC_FIRST_TRAILING_ONE;
    uint32_t low = 0;
    uint32_t high = 0;
    cbuiltin_bits_load_wide(recipe, arguments[0], width, false, &low, &high);
    if (cbuiltin_bits_inverted(id))
    {
        cbuiltin_bits_invert(recipe, low, 64);
        cbuiltin_bits_invert(recipe, high, width - 64);
    }
    if (leading || trailing)
    {
        const uint32_t either = first || count == 2 ? cbuiltin_bits_either(recipe, low, high) : UINT32_MAX;
        const uint32_t x = cbuiltin_bits_count_wide(recipe, low, high, width, trailing);
        if (id == CBUILTIN_STDC_BIT_WIDTH)
        {
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_local(recipe, x);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
            cbuiltin_lower_set(recipe, x);
        }
        else if (first)
        {
            cbuiltin_bits_position(recipe, x, either);
        }
        else if (count == 2)
        {
            cbuiltin_bits_fallback(recipe, x, either, arguments[1]);
        }
        cbuiltin_bits_result(recipe, x, result);
    }
    else if (id == CBUILTIN_POPCOUNTG || id == CBUILTIN_STDC_COUNT_ONES || id == CBUILTIN_STDC_COUNT_ZEROS || id == CBUILTIN_STDC_HAS_SINGLE_BIT)
    {
        const uint32_t temporary = cbuiltin_lower_temporary(recipe);
        cbuiltin_lower_popcount(recipe, low, temporary);
        cbuiltin_lower_popcount(recipe, high, temporary);
        cbuiltin_lower_binary(recipe, low, BUSTER_TYPED_ADD, low, high);
        if (id == CBUILTIN_STDC_COUNT_ZEROS)
        {
            cbuiltin_lower_constant(recipe, width);
            cbuiltin_lower_local(recipe, low);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
            cbuiltin_lower_set(recipe, low);
        }
        else if (id == CBUILTIN_STDC_HAS_SINGLE_BIT)
        {
            cbuiltin_lower_local(recipe, low);
            cbuiltin_lower_constant(recipe, 1);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_set(recipe, low);
        }
        cbuiltin_bits_result(recipe, low, result);
    }
    else
    {
        uint32_t result_low = low;
        uint32_t result_high = high;
        if (id == CBUILTIN_BITREVERSEG || id == CBUILTIN_BSWAPG)
        {
            cbuiltin_bits_reverse_wide(recipe, &result_low, &result_high, width, id == CBUILTIN_BSWAPG ? 3 : 0);
        }
        else if (id == CBUILTIN_STDC_BIT_FLOOR || id == CBUILTIN_STDC_BIT_CEIL)
        {
            // bit_floor's bit is at its bit width less one (none for 0: the count wraps past the width); bit_ceil's at the
            // bit width of x - 1 (of 0 for 0 and 1), none where that is the width (clang's wrap).
            if (id == CBUILTIN_STDC_BIT_CEIL)
            {
                const uint32_t nonzero = cbuiltin_bits_either(recipe, low, high);
                const uint32_t borrow = cbuiltin_lower_temporary(recipe);
                cbuiltin_lower_local(recipe, nonzero);
                cbuiltin_lower_constant(recipe, 0);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                cbuiltin_lower_set(recipe, nonzero);
                cbuiltin_lower_binary(recipe, borrow, BUSTER_TYPED_LT, low, nonzero);
                cbuiltin_lower_binary(recipe, low, BUSTER_TYPED_SUB, low, nonzero);
                cbuiltin_lower_binary(recipe, high, BUSTER_TYPED_SUB, high, borrow);
            }
            const uint32_t k = cbuiltin_bits_count_wide(recipe, low, high, width, false);
            cbuiltin_lower_constant(recipe, id == CBUILTIN_STDC_BIT_CEIL ? width : width - 1);
            cbuiltin_lower_local(recipe, k);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
            cbuiltin_lower_set(recipe, k);
            result_low = cbuiltin_lower_temporary(recipe);
            result_high = cbuiltin_lower_temporary(recipe);
            cbuiltin_bits_one_wide(recipe, k, width, result_low, result_high);
        }
        else
        {
            const uint32_t amount = cbuiltin_lower_temporary(recipe);
            const uint32_t other = cbuiltin_lower_temporary(recipe);
            uint32_t left_low = 0;
            uint32_t left_high = 0;
            uint32_t right_low = 0;
            uint32_t right_high = 0;
            cbuiltin_bits_amount(recipe, amount, arguments[1], types[1], width);
            if (id == CBUILTIN_STDC_ROTATE_RIGHT)
            {
                cbuiltin_bits_complement(recipe, amount, amount, width);
            }
            cbuiltin_bits_complement(recipe, other, amount, width);
            cbuiltin_bits_shift_wide(recipe, low, high, amount, true, &left_low, &left_high);
            cbuiltin_bits_shift_wide(recipe, low, high, other, false, &right_low, &right_high);
            cbuiltin_lower_binary(recipe, left_low, BUSTER_TYPED_OR, left_low, right_low);
            cbuiltin_lower_local(recipe, left_high);
            cbuiltin_lower_local(recipe, right_high);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
            cbuiltin_bits_mask(recipe, width - 64);
            cbuiltin_lower_set(recipe, left_high);
            result_low = left_low;
            result_high = left_high;
        }
        cbuiltin_bits_store_wide(recipe, object, result_low, result_high, result);
    }
}

// A bit builtin's recipe, from `count` arguments' locals and descriptors, of the result's descriptor `result` (a wide
// one's value through `object`).
static void cbuiltin_bits_recipe(CBuiltinLowerRecipe* recipe, CBuiltinId id, const uint32_t* arguments, const uint32_t* types, uint32_t count, uint32_t result,
                                 uint32_t object)
{
    const uint32_t width = cbuiltin_bits_width(types[0]);
    if (cbuiltin_carry(id))
    {
        cbuiltin_bits_carry(recipe, id, arguments, types[0]);
    }
    else if (width > 64)
    {
        cbuiltin_bits_wide(recipe, id, arguments, types, count, result, object);
    }
    else
    {
        const uint32_t x = cbuiltin_bits_load(recipe, arguments[0], types[0]);
        const uint32_t temporary = cbuiltin_lower_temporary(recipe);
        if (cbuiltin_bits_inverted(id))
        {
            cbuiltin_bits_invert(recipe, x, width);
        }
        switch (id)
        {
            case CBUILTIN_BITREVERSE8:
            case CBUILTIN_BITREVERSE16:
            case CBUILTIN_BITREVERSE32:
            case CBUILTIN_BITREVERSE64:
            case CBUILTIN_BITREVERSEG: cbuiltin_bits_swaps(recipe, x, temporary, width, 0); break;
            case CBUILTIN_BSWAPG:
                if (width % 16 == 0)
                {
                    cbuiltin_bits_swaps(recipe, x, temporary, width, 3);
                }
                break;
            case CBUILTIN_CLRSB:
            case CBUILTIN_CLRSBL:
            case CBUILTIN_CLRSBLL:
                // The bits after the sign that equal it: the leading zeros of x, or of its complement where it is
                // negative, less the sign's.
                cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, width - 1);
                cbuiltin_lower_constant(recipe, 0);
                cbuiltin_lower_local(recipe, temporary);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
                cbuiltin_bits_mask(recipe, width);
                cbuiltin_lower_set(recipe, temporary);
                cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_XOR, x, temporary);
                cbuiltin_bits_leading(recipe, x, temporary, width);
                cbuiltin_lower_immediate(recipe, x, BUSTER_TYPED_SUB, x, 1);
                break;
            case CBUILTIN_CLZS:
            case CBUILTIN_CLZG:
            case CBUILTIN_CTZS:
            case CBUILTIN_CTZG:
            {
                const uint32_t copy = count == 2 ? cbuiltin_bits_copy(recipe, x) : 0;
                if (id == CBUILTIN_CLZS || id == CBUILTIN_CLZG)
                {
                    cbuiltin_bits_leading(recipe, x, temporary, width);
                }
                else
                {
                    cbuiltin_bits_trailing(recipe, x, temporary, width);
                }
                if (count == 2)
                {
                    cbuiltin_bits_fallback(recipe, x, copy, arguments[1]);
                }
                break;
            }
            case CBUILTIN_POPCOUNTG:
            case CBUILTIN_STDC_COUNT_ONES: cbuiltin_lower_popcount(recipe, x, temporary); break;
            case CBUILTIN_STDC_COUNT_ZEROS:
                cbuiltin_lower_popcount(recipe, x, temporary);
                cbuiltin_lower_constant(recipe, width);
                cbuiltin_lower_local(recipe, x);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
                cbuiltin_lower_set(recipe, x);
                break;
            case CBUILTIN_STDC_LEADING_ZEROS:
            case CBUILTIN_STDC_LEADING_ONES: cbuiltin_bits_leading(recipe, x, temporary, width); break;
            case CBUILTIN_STDC_TRAILING_ZEROS:
            case CBUILTIN_STDC_TRAILING_ONES: cbuiltin_bits_trailing(recipe, x, temporary, width); break;
            case CBUILTIN_STDC_FIRST_LEADING_ZERO:
            case CBUILTIN_STDC_FIRST_LEADING_ONE:
            case CBUILTIN_STDC_FIRST_TRAILING_ZERO:
            case CBUILTIN_STDC_FIRST_TRAILING_ONE:
            {
                const uint32_t copy = cbuiltin_bits_copy(recipe, x);
                if (id == CBUILTIN_STDC_FIRST_LEADING_ZERO || id == CBUILTIN_STDC_FIRST_LEADING_ONE)
                {
                    cbuiltin_bits_leading(recipe, x, temporary, width);
                }
                else
                {
                    cbuiltin_bits_trailing(recipe, x, temporary, width);
                }
                cbuiltin_bits_position(recipe, x, copy);
                break;
            }
            case CBUILTIN_STDC_HAS_SINGLE_BIT:
                cbuiltin_lower_popcount(recipe, x, temporary);
                cbuiltin_lower_local(recipe, x);
                cbuiltin_lower_constant(recipe, 1);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                cbuiltin_lower_set(recipe, x);
                break;
            case CBUILTIN_STDC_BIT_WIDTH:
                cbuiltin_bits_smear(recipe, x, temporary, width);
                cbuiltin_lower_popcount(recipe, x, temporary);
                break;
            case CBUILTIN_STDC_BIT_FLOOR:
                // The highest set bit alone: the smeared ones less their half.
                cbuiltin_bits_smear(recipe, x, temporary, width);
                cbuiltin_lower_immediate(recipe, temporary, BUSTER_TYPED_SHR, x, 1);
                cbuiltin_lower_binary(recipe, x, BUSTER_TYPED_SUB, x, temporary);
                break;
            case CBUILTIN_STDC_BIT_CEIL:
                // One past the smeared ones of x - 1 (of 0 for 0 and 1), 0 where that leaves the width.
                cbuiltin_lower_local(recipe, x);
                cbuiltin_lower_local(recipe, x);
                cbuiltin_lower_constant(recipe, 0);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
                cbuiltin_lower_set(recipe, x);
                cbuiltin_bits_smear(recipe, x, temporary, width);
                cbuiltin_lower_local(recipe, x);
                cbuiltin_lower_constant(recipe, 1);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                cbuiltin_bits_mask(recipe, width);
                cbuiltin_lower_set(recipe, x);
                break;
            case CBUILTIN_STDC_ROTATE_LEFT:
            case CBUILTIN_STDC_ROTATE_RIGHT:
            {
                const uint32_t amount = cbuiltin_lower_temporary(recipe);
                const uint32_t other = cbuiltin_lower_temporary(recipe);
                cbuiltin_bits_amount(recipe, amount, arguments[1], types[1], width);
                if (id == CBUILTIN_STDC_ROTATE_RIGHT)
                {
                    cbuiltin_bits_complement(recipe, amount, amount, width);
                }
                cbuiltin_bits_complement(recipe, other, amount, width);
                cbuiltin_lower_binary(recipe, temporary, BUSTER_TYPED_SHL, x, amount);
                cbuiltin_lower_binary(recipe, other, BUSTER_TYPED_SHR, x, other);
                cbuiltin_lower_local(recipe, temporary);
                cbuiltin_lower_local(recipe, other);
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
                cbuiltin_bits_mask(recipe, width);
                cbuiltin_lower_set(recipe, x);
                break;
            }
            default: break;
        }
        cbuiltin_bits_result(recipe, x, result);
    }
}

#endif
