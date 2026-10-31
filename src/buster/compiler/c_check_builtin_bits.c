// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's bit builtins as clang 23 checks and folds them: __builtin_bitreverse8 to 64 and bitreverseg, bswapg, clrsb,
// clrsbl and clrsbll, clzs and ctzs, popcountg, clzg and ctzg, C23's __builtin_stdc_ ones, and the carry chains addcb to
// addcll and subcb to subcll. c_check_builtin_misc.c includes it: their calls are the graph's miscellaneous builtins
// (cbuiltin_bits), whose arguments check_builtin_misc_argument has converted as operands (an lvalue's value). Then:
//
// - a builtin of a fixed prototype (bitreverse8 to 64, clrsb, clzs, ctzs and the carry chains) converts each argument as
//   if by assignment to its parameter's type, as a call of a prototyped function does, with clang's pointer rules for a
//   carry's (check_action_assignable: a pointer sign or a lost qualifier warns, another pointee is an error);
// - popcountg and clzg's and ctzg's first argument take an unsigned integer type unconverted (bool, an enumeration of an
//   unsigned type, plain char where it is unsigned and the unsigned _BitInts among them), and clzg's and ctzg's second,
//   their result for zero, its integer promotions, which must give int;
// - bitreverseg and bswapg take any integer type and give it, bswapg's _BitInts of a multiple of 16 bits, 8 or 1;
// - the stdc ones take an unsigned integer type that is no bool or enumeration, and give unsigned int, bool
//   (has_single_bit) or the argument's type (bit_floor and bit_ceil); rotate_left and rotate_right take an unsigned
//   integer type, bools and enumerations of unsigned types admitted, and a count of any integer type, which counts
//   modulo the width (a negative one's mathematical remainder, clang's NormalizeRotateAmount), and give the first's
//   type.
//
// Each folds as clang's evaluator does, in its operand's width of up to 128 bits, where every argument it reads is an
// integer constant: clzs, ctzs, and clzg and ctzg without their second argument, of zero are undefined (a fault, as
// check_builtins gives clz of zero), and a carry chain, whose store of the carry is an effect, never folds. A runtime
// action evaluates every argument once, in order (evaluated_operands), where clang's code evaluates bitreverseg's and
// bswapg's twice (docs/c-builtins-integration.md); a carry chain's value has an effect and writes memory. The checker
// asks once per call, in source order, as for the other miscellaneous builtins, and no unit of the corpora calls these
// builtins: their folds stay scalar.
#ifndef BUSTER_C_CHECK_BUILTIN_BITS_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_BITS_INCLUDED

// The parameters' type of a builtin of a fixed prototype, a carry chain's values' (its carry a pointer to it), or 0 for
// a generic one.
static TypeId check_builtin_bits_parameter(CBuiltinId id)
{
    TypeId result = 0;
    switch (id)
    {
        case CBUILTIN_BITREVERSE8:
        case CBUILTIN_ADDCB:
        case CBUILTIN_SUBCB: result = BUSTER_TYPE_UCHAR; break;
        case CBUILTIN_BITREVERSE16:
        case CBUILTIN_CLZS:
        case CBUILTIN_CTZS:
        case CBUILTIN_ADDCS:
        case CBUILTIN_SUBCS: result = BUSTER_TYPE_USHORT; break;
        case CBUILTIN_BITREVERSE32:
        case CBUILTIN_ADDC:
        case CBUILTIN_SUBC: result = BUSTER_TYPE_UINT; break;
        case CBUILTIN_BITREVERSE64:
        case CBUILTIN_ADDCL:
        case CBUILTIN_SUBCL: result = BUSTER_TYPE_ULONG; break;
        case CBUILTIN_ADDCLL:
        case CBUILTIN_SUBCLL: result = BUSTER_TYPE_ULLONG; break;
        case CBUILTIN_CLRSB: result = BUSTER_TYPE_INT; break;
        case CBUILTIN_CLRSBL: result = BUSTER_TYPE_LONG; break;
        case CBUILTIN_CLRSBLL: result = BUSTER_TYPE_LLONG; break;
        default: break;
    }
    return result;
}

// An argument converted as if by assignment to its parameter's type, as check_builtin_misc_convert converts a memory
// builtin's, but with clang's pointer rules whole: false, with a diagnostic, where it can't be.
static bool check_builtin_bits_convert(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* value, uint32_t index,
                                       TypeId destination)
{
    const CheckAction context = {
        .node = input->node,
        .tag_completions = input->tag_completions,
        .tag_completion_count = input->tag_completion_count,
        .source_position = input->source_position,
        .extensions = input->extensions,
    };
    bool accepted = false;
    if (destination == 0)
    {
        check_action_diagnose(&output->action, CHECK_DIAG_RESOURCE, index);
    }
    else if (check_action_kind(table, value->type) == BUSTER_TYPE_KIND_COMPLEX)
    {
        // A complex value's real part has no runtime conversion to an integer here yet, as check_builtin_misc_convert says.
        output->unsupported |= BUSTER_CHECK_BUILTIN_MISC_UNSUPPORTED_COMPLEX;
        check_action_diagnose(&output->action, CHECK_DIAG_UNSUPPORTED, index);
    }
    else if (!check_action_assignable(table, destination, value, &context, &output->action))
    {
        check_action_diagnose(&output->action, CHECK_DIAG_INCOMPATIBLE, index);
    }
    else
    {
        accepted = true;
        check_action_conversion(table, value, destination, BUSTER_CHECK_CONVERT_ASSIGNMENT, output->argument_conversions + index, input->node);
    }
    return accepted;
}

// An argument's integer type: its width and signedness, or false with a diagnostic, an unsupported one for a _BitInt past
// the 128 bits a constant holds, which clang takes.
static bool check_builtin_bits_integer(const TypeTable* table, TypeId type, uint32_t index, uint32_t* width, bool* is_signed, CheckBuiltinMiscResult* output)
{
    const bool integer = check_action_integer_type(table, type, width, is_signed);
    if (!integer)
    {
        const bool wide = type_kind(table, type_unqualified(type_strip_aligned(table, type))) == BUSTER_TYPE_KIND_BIT_INT;
        check_action_diagnose(&output->action, wide ? CHECK_DIAG_UNSUPPORTED : CHECK_DIAG_INVALID_TYPE, index);
    }
    return integer;
}

static uint32_t check_builtin_bits_popcount(unsigned __int128 bits)
{
    return (uint32_t)__builtin_popcountll((uint64_t)bits) + (uint32_t)__builtin_popcountll((uint64_t)(bits >> 64));
}

// The leading zeros of a value of `width` bits, the width for 0.
static uint32_t check_builtin_bits_leading(unsigned __int128 bits, uint32_t width)
{
    const uint64_t high = (uint64_t)(bits >> 64);
    const uint64_t low = (uint64_t)bits;
    const uint32_t zeros = high != 0 ? (uint32_t)__builtin_clzll(high) : low != 0 ? 64 + (uint32_t)__builtin_clzll(low) : 128;
    return zeros - (128 - width);
}

// The trailing zeros of a value of `width` bits, the width for 0.
static uint32_t check_builtin_bits_trailing(unsigned __int128 bits, uint32_t width)
{
    const uint64_t high = (uint64_t)(bits >> 64);
    const uint64_t low = (uint64_t)bits;
    return low != 0 ? (uint32_t)__builtin_ctzll(low) : high != 0 ? 64 + (uint32_t)__builtin_ctzll(high) : width;
}

static unsigned __int128 check_builtin_bits_reverse(unsigned __int128 bits, uint32_t width)
{
    unsigned __int128 result = 0;
    for (uint32_t i = 0; i < width; i += 1)
    {
        result |= ((bits >> i) & 1) << (width - 1 - i);
    }
    return result;
}

// A value's bytes in the other order: one of 1 or 8 bits is its own (bswapg's only widths that aren't multiples of 16).
static unsigned __int128 check_builtin_bits_swap(unsigned __int128 bits, uint32_t width)
{
    unsigned __int128 result = width % 16 == 0 ? 0 : bits;
    for (uint32_t i = 0; width % 16 == 0 && i < width / 8; i += 1)
    {
        result |= ((bits >> (8 * i)) & 0xff) << (width - 8 - 8 * i);
    }
    return result;
}

// A rotation's count of a value of `width` bits: `count`, of `count_width` bits and signed or not, modulo the width, a
// negative one's mathematical remainder.
static uint32_t check_builtin_bits_amount(unsigned __int128 count, uint32_t count_width, bool count_signed, uint32_t width)
{
    const bool negative = count_signed && ((count >> (count_width - 1)) & 1) != 0;
    const unsigned __int128 magnitude = negative ? (~count + 1) & check_action_mask(count_width) : count;
    const uint32_t remainder = (uint32_t)(magnitude % width);
    return negative && remainder != 0 ? width - remainder : remainder;
}

static unsigned __int128 check_builtin_bits_rotate(unsigned __int128 bits, uint32_t width, uint32_t amount)
{
    return amount == 0 ? bits : ((bits << amount) | (bits >> (width - amount))) & check_action_mask(width);
}

// A fold of a bit builtin whose arguments are the constants `bits` (the first, of `width` bits) and `second` (of
// `second_width` bits, signed or not: clzg's and ctzg's result for zero, a rotation's count), in its result's bits, or
// false where clang's evaluator finds it undefined.
static bool check_builtin_bits_fold(CBuiltinId id, unsigned __int128 bits, uint32_t width, bool has_second, unsigned __int128 second, uint32_t second_width,
                                    bool second_signed, unsigned __int128* result)
{
    const unsigned __int128 mask = check_action_mask(width);
    const unsigned __int128 inverse = ~bits & mask;
    const uint32_t leading = check_builtin_bits_leading(bits, width);
    const uint32_t trailing = check_builtin_bits_trailing(bits, width);
    bool defined = true;
    unsigned __int128 value = 0;
    switch (id)
    {
        case CBUILTIN_BITREVERSE8:
        case CBUILTIN_BITREVERSE16:
        case CBUILTIN_BITREVERSE32:
        case CBUILTIN_BITREVERSE64:
        case CBUILTIN_BITREVERSEG: value = check_builtin_bits_reverse(bits, width); break;
        case CBUILTIN_BSWAPG: value = check_builtin_bits_swap(bits, width); break;
        case CBUILTIN_CLRSB:
        case CBUILTIN_CLRSBL:
        case CBUILTIN_CLRSBLL: value = check_builtin_bits_leading((bits >> (width - 1)) != 0 ? inverse : bits, width) - 1; break;
        case CBUILTIN_CLZS:
        case CBUILTIN_CLZG:
            defined = bits != 0 || has_second;
            value = bits != 0 ? leading : second;
            break;
        case CBUILTIN_CTZS:
        case CBUILTIN_CTZG:
            defined = bits != 0 || has_second;
            value = bits != 0 ? trailing : second;
            break;
        case CBUILTIN_POPCOUNTG:
        case CBUILTIN_STDC_COUNT_ONES: value = check_builtin_bits_popcount(bits); break;
        case CBUILTIN_STDC_COUNT_ZEROS: value = width - check_builtin_bits_popcount(bits); break;
        case CBUILTIN_STDC_LEADING_ZEROS: value = leading; break;
        case CBUILTIN_STDC_LEADING_ONES: value = check_builtin_bits_leading(inverse, width); break;
        case CBUILTIN_STDC_TRAILING_ZEROS: value = trailing; break;
        case CBUILTIN_STDC_TRAILING_ONES: value = check_builtin_bits_trailing(inverse, width); break;
        case CBUILTIN_STDC_FIRST_LEADING_ZERO: value = inverse != 0 ? check_builtin_bits_leading(inverse, width) + 1 : 0; break;
        case CBUILTIN_STDC_FIRST_LEADING_ONE: value = bits != 0 ? leading + 1 : 0; break;
        case CBUILTIN_STDC_FIRST_TRAILING_ZERO: value = inverse != 0 ? check_builtin_bits_trailing(inverse, width) + 1 : 0; break;
        case CBUILTIN_STDC_FIRST_TRAILING_ONE: value = bits != 0 ? trailing + 1 : 0; break;
        case CBUILTIN_STDC_HAS_SINGLE_BIT: value = check_builtin_bits_popcount(bits) == 1; break;
        case CBUILTIN_STDC_BIT_WIDTH: value = width - leading; break;
        case CBUILTIN_STDC_BIT_FLOOR: value = bits != 0 ? (unsigned __int128)1 << (width - 1 - leading) : 0; break;
        case CBUILTIN_STDC_BIT_CEIL:
        {
            // The least power of two that isn't less than the value, 0 where it doesn't fit (clang's wrap).
            const uint32_t below = bits > 1 ? check_builtin_bits_leading(bits - 1, width) : width;
            value = bits <= 1 ? 1 : below == 0 ? 0 : (unsigned __int128)1 << (width - below);
            break;
        }
        case CBUILTIN_STDC_ROTATE_LEFT:
        case CBUILTIN_STDC_ROTATE_RIGHT:
        {
            const uint32_t amount = check_builtin_bits_amount(second, second_width, second_signed, width);
            value = check_builtin_bits_rotate(bits, width, id == CBUILTIN_STDC_ROTATE_LEFT || amount == 0 ? amount : width - amount);
            break;
        }
        default: defined = false; break;
    }
    *result = value;
    return defined;
}

static void check_builtin_bits(TypeTable* table, const CheckBuiltinMiscAction* input, CheckBuiltinMiscResult* output, CheckValue* values)
{
    const CBuiltinId id = input->id;
    const TypeId parameter = check_builtin_bits_parameter(id);
    const bool carry = cbuiltin_carry(id);
    const bool rotate = id == CBUILTIN_STDC_ROTATE_LEFT || id == CBUILTIN_STDC_ROTATE_RIGHT;
    const bool stdc = id >= CBUILTIN_STDC_LEADING_ZEROS && id <= CBUILTIN_STDC_BIT_CEIL;
    const bool any = id == CBUILTIN_BITREVERSEG || id == CBUILTIN_BSWAPG;
    CheckValue* value = &output->action.value;
    uint32_t width = 0;
    bool is_signed = false;
    TypeId result = BUSTER_TYPE_INT;
    if (parameter != 0)
    {
        for (uint32_t i = 0; i < input->argument_count && output->action.diagnostic == CHECK_DIAG_NONE; i += 1)
        {
            check_builtin_bits_convert(table, input, output, values + i, i, carry && i == 3 ? type_pointer(table, parameter) : parameter);
        }
        check_action_integer_type(table, parameter, &width, &is_signed);
        result = carry || id <= CBUILTIN_BITREVERSE64 ? parameter : BUSTER_TYPE_INT;
    }
    else if (check_builtin_bits_integer(table, values[0].type, 0, &width, &is_signed, output))
    {
        const TypeId base = type_unqualified(type_strip_aligned(table, values[0].type));
        const uint8_t kind = type_kind(table, base);
        if ((is_signed && !any) || (stdc && (base == BUSTER_TYPE_BOOL || kind == BUSTER_TYPE_KIND_ENUM)) ||
            (id == CBUILTIN_BSWAPG && kind == BUSTER_TYPE_KIND_BIT_INT && width % 16 != 0 && width != 8 && width != 1))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 0);
        }
        result = id == CBUILTIN_STDC_HAS_SINGLE_BIT ? BUSTER_TYPE_BOOL :
                 any || rotate || id == CBUILTIN_STDC_BIT_FLOOR || id == CBUILTIN_STDC_BIT_CEIL ? values[0].type :
                 stdc ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT;
    }
    uint32_t second_width = 0;
    bool second_signed = false;
    if (output->action.diagnostic == CHECK_DIAG_NONE && input->argument_count == 2 && !rotate)
    {
        // clzg's and ctzg's result for zero: its integer promotions (a bit-field's by its width), which must give int.
        if (!check_action_is_integer(table, values[1].type))
        {
            check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 1);
        }
        else
        {
            check_action_promote(table, values + 1, output->argument_conversions + 1, input->node);
            if (type_unqualified(type_strip_aligned(table, values[1].type)) != BUSTER_TYPE_INT)
            {
                check_action_diagnose(&output->action, CHECK_DIAG_INVALID_TYPE, 1);
            }
            second_width = 32;
            second_signed = true;
        }
    }
    else if (output->action.diagnostic == CHECK_DIAG_NONE && rotate)
    {
        check_builtin_bits_integer(table, values[1].type, 1, &second_width, &second_signed, output);
    }
    value->type = result;
    value->category = CHECK_RVALUE;
    for (uint32_t i = 0; i < input->argument_count; i += 1)
    {
        check_action_inherit(value, values + i);
    }
    bool constant = output->action.diagnostic == CHECK_DIAG_NONE && !carry && value->fault == CHECK_FAULT_NONE;
    for (uint32_t i = 0; i < input->argument_count; i += 1)
    {
        constant = constant && values[i].constant_kind == CHECK_CONSTANT_INTEGER && values[i].fault == CHECK_FAULT_NONE;
    }
    if (constant)
    {
        uint32_t result_width = 0;
        bool result_signed = false;
        unsigned __int128 folded = 0;
        const bool second = input->argument_count == 2;
        const unsigned __int128 second_bits = second ? (unsigned __int128)values[1].integer & check_action_mask(second_width) : 0;
        const bool defined = check_builtin_bits_fold(id, (unsigned __int128)values[0].integer & check_action_mask(width), width, second, second_bits, second_width,
                                                     second_signed, &folded);
        check_action_integer_type(table, result, &result_width, &result_signed);
        if (defined)
        {
            // Clang's builtin constant-expression rule admits folded arguments, as check_builtins does; their own
            // expressions keep their effects.
            value->constant_kind = CHECK_CONSTANT_INTEGER;
            value->integer = (__int128)(folded & check_action_mask(result_width));
            value->flags |= (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 ? BUSTER_CHECK_ICE : 0;
        }
        else
        {
            const bool unevaluated = (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
            check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, input->node);
            value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED);
            value->flags |= unevaluated ? BUSTER_CHECK_ICE_UNEVALUATED : 0;
        }
    }
    else
    {
        value->constant_kind = CHECK_CONSTANT_NONE;
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED);
    }
    if (carry)
    {
        value->flags |= BUSTER_CHECK_SIDE_EFFECT;
        output->writes_memory = true;
    }
}

#endif
