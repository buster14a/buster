// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Scalar complex constants and GNU component operations. Included after the ordinary action helpers. Each
// component conversion calls only the noncomplex converter, so nested source types never recurse through it.
// Floating components use their actual target format. Integer-complex components occupy separate 64-bit halves.

// Complex integer constants of elements past 64 bits (#2219): a complex _BitInt of 65 bits or more, which an imaginary
// bit-precise constant (0x1'0000'0000'0000'0000wbi) or the usual arithmetic conversions make (5wbi * (_BitInt(200))1),
// and GNU's _Complex __int128 (5wbi * (__int128)1). Each part is ceil(width / 64) limbs in c_check_wide.c's region, the
// least significant first, the bits past the width zero, as a _BitInt past 128 bits holds its own: the real part's
// pointed to from floating_storage[0] and the imaginary part's from imaginary_storage[0], the words a _Complex
// __float128's parts already use, which every copy, stream and comparison of values carries; `integer` keeps the real
// part's low 128 bits. A part read out (check_complex_component) is an integer constant of the element: its low 128 bits
// up to 128 bits, its limbs past them. An element of up to 64 bits keeps both parts in `integer`'s 64-bit halves.

// An integer element past 64 bits, whose complex constants hold limbs, with its width and signedness.
static bool check_complex_wide_element(const TypeTable* table, TypeId element, uint32_t* width, bool* sign)
{
    const bool narrow = check_action_integer_type(table, element, width, sign);
    return narrow ? *width > 64 : check_wide_type(table, element, width, sign);
}

// A complex constant's part's `count` limbs, or null when it holds none (its pointer isn't the region's).
static const uint64_t* check_complex_wide_limbs(const CheckValue* value, bool imaginary, uint32_t count)
{
    CheckValue part = { .constant_kind = CHECK_CONSTANT_INTEGER };
    part.floating_storage[0] = imaginary ? value->imaginary_storage[0] : value->floating_storage[0];
    return value->constant_kind == CHECK_CONSTANT_COMPLEX ? check_wide_limbs(&part, count) : NULL;
}

// `value` becomes the complex constant of `parts`, two arrays of limbs from check_wide_allocate of at least two each,
// the real part's then the imaginary part's.
static void check_complex_wide_publish(CheckValue* value, uint64_t* const parts[2])
{
    value->constant_kind = CHECK_CONSTANT_COMPLEX;
    value->integer = (unsigned __int128)parts[0][1] << 64 | parts[0][0];
    value->floating_storage[0] = (uint64_t)(uintptr_t)parts[0];
    value->floating_storage[1] = 0;
    value->imaginary_storage[0] = (uint64_t)(uintptr_t)parts[1];
    value->imaginary_storage[1] = 0;
}

// `value` becomes the complex constant of `real` and `imaginary`, integer constants of its element, of `width` bits past
// 64. False when a part holds no number or the region is full, which leaves `value` as it was.
static bool check_complex_wide_set(const TypeTable* table, CheckValue* value, const CheckValue* real, const CheckValue* imaginary, uint32_t width)
{
    const uint32_t count = check_wide_count(width);
    uint64_t* parts[2] = { check_wide_allocate(count), check_wide_allocate(count) };
    const bool valid = parts[0] != NULL && parts[1] != NULL && check_wide_operand(table, real, parts[0], count) &&
                       check_wide_operand(table, imaginary, parts[1], count);
    if (valid)
    {
        parts[0][count - 1] &= check_wide_top_mask(width);
        parts[1][count - 1] &= check_wide_top_mask(width);
        check_complex_wide_publish(value, parts);
    }
    return valid;
}

// The complex constant `value`'s part as an integer constant of its element of `width` bits past 64, into `part`, whose
// other fields the caller set: its low 128 bits up to 128 bits, else its limbs. False when it holds none.
static bool check_complex_wide_part(const CheckValue* value, bool imaginary, uint32_t width, CheckValue* part)
{
    const uint64_t* limbs = check_complex_wide_limbs(value, imaginary, check_wide_count(width));
    part->floating_storage[0] = 0;
    part->floating_storage[1] = 0;
    part->imaginary_storage[0] = 0;
    part->imaginary_storage[1] = 0;
    part->constant_kind = limbs != NULL ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE;
    if (limbs != NULL && width <= 128)
    {
        part->integer = (unsigned __int128)limbs[1] << 64 | limbs[0];
    }
    else if (limbs != NULL)
    {
        check_wide_publish(part, limbs);
    }
    return limbs != NULL;
}

// Whether the complex constants `left` and `right` of an element of `width` bits past 64 hold the same number; false
// when either holds none.
static bool check_complex_wide_equal(const CheckValue* left, const CheckValue* right, uint32_t width)
{
    const uint32_t count = check_wide_count(width);
    bool same = true;
    for (uint32_t part = 0; part < 2; part += 1)
    {
        const uint64_t* a = check_complex_wide_limbs(left, part != 0, count);
        const uint64_t* b = check_complex_wide_limbs(right, part != 0, count);
        same &= a != NULL && b != NULL && check_wide_compare((CheckWideKernels){ 0 }, a, b, count) == 0;
    }
    return same;
}

// Whether two values hold the same complex constant of an element past 64 bits (1), different ones or one of them none
// (0), or neither holds parts' limbs (-1), whatever their pointers, as check_wide_same is a wide constant's: for
// comparisons of whole values, as the scalar and vector paths' (test-checker's check_test_value_equal).
static __attribute__((unused)) int32_t check_complex_wide_same(const CheckValue* a, const CheckValue* b)
{
    int32_t result = -1;
    bool same = true;
    for (uint32_t part = 0; part < 2; part += 1)
    {
        CheckValue x = { .constant_kind = a->constant_kind == CHECK_CONSTANT_COMPLEX ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE };
        CheckValue y = { .constant_kind = b->constant_kind == CHECK_CONSTANT_COMPLEX ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE };
        x.floating_storage[0] = part == 0 ? a->floating_storage[0] : a->imaginary_storage[0];
        y.floating_storage[0] = part == 0 ? b->floating_storage[0] : b->imaginary_storage[0];
        const int32_t held = check_wide_same(&x, &y);
        result = held >= 0 ? 0 : result;
        same &= held == 1;
    }
    return result >= 0 && same ? 1 : result;
}

// Whether the complex constant `value` of an element of `width` bits past 64 is no zero, into `truth`; false when it
// holds no number.
static bool check_complex_wide_truth(const CheckValue* value, uint32_t width, bool* truth)
{
    const uint32_t count = check_wide_count(width);
    const uint64_t* real = check_complex_wide_limbs(value, false, count);
    const uint64_t* imaginary = check_complex_wide_limbs(value, true, count);
    const bool known = real != NULL && imaginary != NULL;
    *truth = known && !(check_wide_is_zero(real, count) && check_wide_is_zero(imaginary, count));
    return known;
}

// Word `index` of a part's object representation, of its `count` limbs of `width` bits (`negative` its sign): its limb,
// or past them its sign's fill, the bits past the width extended from its sign as the x86-64 _BitInt ABI keeps them, as
// check_wide_bytes writes a _BitInt's.
static uint64_t check_complex_wide_word(const uint64_t* limbs, uint32_t count, uint32_t width, bool negative, uint64_t index)
{
    uint64_t word = index < count ? limbs[index] : negative ? UINT64_MAX : 0;
    word |= index + 1 == count && negative ? ~check_wide_top_mask(width) : 0;
    return word;
}

// A part's object representation (#2219): `size` bytes, its element's, of a complex constant of an element of `width`
// bits past 64 (check_complex_wide_word), for static data. False when it holds none. Cold, as c_check_wide.c's
// check_wide_bytes is: a word at a time, for a constant of a type C can't spell but through an imaginary constant.
static __attribute__((unused)) bool check_complex_wide_bytes(const CheckValue* value, bool imaginary, uint32_t width, bool sign, uint8_t* bytes, uint64_t size)
{
    const uint32_t count = check_wide_count(width);
    const uint64_t* limbs = check_complex_wide_limbs(value, imaginary, count);
    const bool valid = limbs != NULL && size >= (uint64_t)count * sizeof(uint64_t) && size % sizeof(uint64_t) == 0;
    if (valid)
    {
        const bool negative = check_wide_negative(limbs, width, sign);
        for (uint64_t i = 0; i < size / sizeof(uint64_t); i += 1)
        {
            const uint64_t word = check_complex_wide_word(limbs, count, width, negative, i);
            memcpy(bytes + i * sizeof(uint64_t), &word, sizeof(word));
        }
    }
    return valid;
}

static CheckValue check_complex_component(const TypeTable* table, const CheckValue* input, bool imaginary)
{
    CheckValue value = *input;
    const bool complex = check_action_kind(table, input->type) == BUSTER_TYPE_KIND_COMPLEX;
    const TypeId element = complex ? type_element(table, type_strip_aligned(table, input->type)) : input->type;
    value.type = type_unqualified(element);
    value.imaginary = 0;
    if (complex && input->constant_kind == CHECK_CONSTANT_COMPLEX)
    {
        uint32_t width = 0;
        bool sign = false;
        if (check_action_integer_type(table, element, &width, &sign) && width <= 64)
        {
            value.constant_kind = CHECK_CONSTANT_INTEGER;
            value.integer = (input->integer >> (imaginary ? 64 : 0)) & check_action_mask(width);
        }
        else if (type_unqualified(type_strip_aligned(table, element)) == BUSTER_TYPE_FLOAT128)
        {
            // A _Complex __float128 constant's parts are its bits (check_quad_complex_set), each part a real one's.
            check_quad_set(&value, imaginary ? check_quad_complex_imaginary(input) : input->integer);
        }
        else if (check_complex_wide_element(table, element, &width, &sign))
        {
            // An element past 64 bits: the part's limbs (#2219).
            check_complex_wide_part(input, imaginary, width, &value);
        }
        else
        {
            value.constant_kind = CHECK_CONSTANT_FLOATING;
            value.floating = imaginary ? input->imaginary : input->floating;
            value.integer = 0;
        }
    }
    else if (!complex && imaginary)
    {
        value.category = CHECK_RVALUE;
        value.integer = 0;
        value.floating = 0;
        value.constant_kind = check_action_is_integer(table, element) ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_FLOATING;
        uint32_t width = 0;
        bool sign = false;
        if (check_wide_type(table, element, &width, &sign))
        {
            // A _BitInt past 128 bits' zero: its limbs, zeros as the region gives them (#2219).
            const uint64_t* zero = check_wide_allocate(check_wide_count(width));
            value.constant_kind = zero != NULL ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE;
            if (zero != NULL)
            {
                check_wide_publish(&value, zero);
            }
        }
    }
    return value;
}

static void check_complex_convert(const TypeTable* table, CheckValue* value, TypeId destination, uint32_t node)
{
    const TypeId target = type_unqualified(type_strip_aligned(table, destination));
    const bool target_complex = check_action_kind(table, target) == BUSTER_TYPE_KIND_COMPLEX;
    const bool source_complex = check_action_kind(table, value->type) == BUSTER_TYPE_KIND_COMPLEX;
    if (target == BUSTER_TYPE_BOOL || target == BUSTER_TYPE_VOID)
    {
        // The boolean test observes both components before either is discarded.
        check_action_convert_plain(table, value, destination, node);
        value->imaginary = 0;
    }
    else
    {
        const TypeId element = target_complex ? type_element(table, target) : destination;
        CheckValue real = source_complex ? check_complex_component(table, value, false) : *value;
        CheckValue imaginary = source_complex ? check_complex_component(table, value, true) :
                               (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER };
        check_action_convert_plain(table, &real, element, node);
        if (target_complex)
        {
            check_action_convert_plain(table, &imaginary, element, node);
            check_action_inherit(&real, &imaginary);
        }
        *value = real;
        value->type = destination;
        value->imaginary = 0;
        value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
        if (target_complex && real.fault == CHECK_FAULT_NONE && imaginary.fault == CHECK_FAULT_NONE)
        {
            // A _Complex __float128 constant keeps both parts' bits (#1250, check_quad_complex_set).
            const bool quad = type_unqualified(type_strip_aligned(table, element)) == BUSTER_TYPE_FLOAT128;
            if (quad && real.constant_kind == CHECK_CONSTANT_FLOATING && imaginary.constant_kind == CHECK_CONSTANT_FLOATING)
            {
                check_quad_complex_set(value, real.integer, imaginary.integer);
            }
            else if (real.constant_kind == CHECK_CONSTANT_FLOATING && imaginary.constant_kind == CHECK_CONSTANT_FLOATING)
            {
                value->constant_kind = CHECK_CONSTANT_COMPLEX;
                value->imaginary = imaginary.floating;
            }
            else if (real.constant_kind == CHECK_CONSTANT_INTEGER && imaginary.constant_kind == CHECK_CONSTANT_INTEGER)
            {
                uint32_t width = 0;
                bool sign = false;
                if (check_action_integer_type(table, element, &width, &sign) && width <= 64)
                {
                    value->constant_kind = CHECK_CONSTANT_COMPLEX;
                    value->integer = real.integer | (imaginary.integer << 64);
                }
                else if (!check_complex_wide_element(table, element, &width, &sign) || !check_complex_wide_set(table, value, &real, &imaginary, width))
                {
                    check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
                }
            }
            else
            {
                value->constant_kind = CHECK_CONSTANT_NONE;
            }
        }
        if (value->fault != CHECK_FAULT_NONE)
        {
            // A failed imaginary conversion invalidates the whole result, even when the real conversion succeeded.
            check_action_set_fault(value, value->fault, value->fault_node);
        }
    }
}

static CheckValue check_complex_integer_fold(CheckOp op, unsigned __int128 left, unsigned __int128 right, uint32_t width, bool sign, uint32_t node, uint32_t extensions)
{
    CheckValue value = { 0 };
    const CheckIntegerFold fold = {
        .active = true,
        .op = op,
        .left = left,
        .right = right,
        .width = width,
        .right_width = width,
        .is_signed = sign,
        .right_signed = sign,
        .node = node,
        .extensions = extensions,
    };
    check_action_fold_integer(fold, &value);
    return value;
}

// `op` on two parts of a complex integer constant, integer constants of its element `element` of `width` bits (`right`
// `left` for a unary operator): the operation of up to 128 bits (check_complex_integer_fold), or past them of their
// limbs (check_wide_fold, #2219). The result is an integer constant of the element, or a fault.
static CheckValue check_complex_part_fold(const TypeTable* table, CheckOp op, const CheckValue* left, const CheckValue* right, TypeId element, uint32_t width,
                                          bool sign, uint32_t node, uint32_t extensions)
{
    CheckValue value = { 0 };
    if (width <= 128)
    {
        value = check_complex_integer_fold(op, left->integer, right->integer, width, sign, node, extensions);
    }
    else
    {
        uint32_t warnings = 0;
        value.type = element;
        value.category = CHECK_RVALUE;
        if (left->constant_kind != CHECK_CONSTANT_INTEGER || right->constant_kind != CHECK_CONSTANT_INTEGER ||
            !check_wide_fold(table, op, left, right, &value, node, extensions, false, &warnings))
        {
            check_action_set_fault(&value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
        }
    }
    value.type = element;
    value.category = CHECK_RVALUE;
    return value;
}

// Whether two binary128 values are equal: a NaN to none, a zero to either zero.
static bool check_complex_quad_equal(unsigned __int128 left, unsigned __int128 right)
{
    const bool nan = check_quad_is_nan(left) || check_quad_is_nan(right);
    return !nan && (left == right || ((left | right) & ~BUSTER_CHECK_QUAD_SIGN) == 0);
}

// Binary128 operations as clang's APFloat makes them (check_quad_arithmetic), for _Complex __float128's * and / (#1250).
#define BUSTER_CHECK_QUAD_ONE ((unsigned __int128)0x3fff << 112)

static unsigned __int128 check_complex_quad_op(CheckOp op, unsigned __int128 left, unsigned __int128 right)
{
    return check_quad_arithmetic(op, left, right);
}

static bool check_complex_quad_infinite(unsigned __int128 bits)
{
    return (bits & ~BUSTER_CHECK_QUAD_SIGN) == BUSTER_CHECK_QUAD_EXPONENT;
}

// APFloat's copySign of +`magnitude` (+1, +0, +inf) by `sign`'s sign.
static unsigned __int128 check_complex_quad_sign(unsigned __int128 magnitude, unsigned __int128 sign)
{
    return magnitude | (sign & BUSTER_CHECK_QUAD_SIGN);
}

// APFloat's scalbn of a value by 2^`power`, rounded once (check_quad_scale); a NaN quiet, as its operations leave it.
static unsigned __int128 check_complex_quad_scalbn(unsigned __int128 bits, int32_t power)
{
    return check_quad_is_nan(bits) ? bits | BUSTER_CHECK_QUAD_QUIET : check_quad_from_host(check_quad_scale(check_quad_host(bits), power));
}

// clang 23's HandleComplexComplexMul (ExprConstant.cpp): (ac - bd) + (ad + bc)i, and where both parts are NaNs, C11
// Annex G's recovery of an infinite operand or product, each step APFloat's. Whether both of the formula's parts were NaNs
// (where clang's code calls __multc3).
static bool check_complex_quad_multiply(unsigned __int128 a, unsigned __int128 b, unsigned __int128 c, unsigned __int128 d, unsigned __int128 parts[2])
{
    const unsigned __int128 ac = check_complex_quad_op(CHECK_OP_MULTIPLY, a, c);
    const unsigned __int128 bd = check_complex_quad_op(CHECK_OP_MULTIPLY, b, d);
    const unsigned __int128 ad = check_complex_quad_op(CHECK_OP_MULTIPLY, a, d);
    const unsigned __int128 bc = check_complex_quad_op(CHECK_OP_MULTIPLY, b, c);
    parts[0] = check_complex_quad_op(CHECK_OP_SUBTRACT, ac, bd);
    parts[1] = check_complex_quad_op(CHECK_OP_ADD, ad, bc);
    const bool both = check_quad_is_nan(parts[0]) && check_quad_is_nan(parts[1]);
    if (both)
    {
        bool recalculate = false;
        if (check_complex_quad_infinite(a) || check_complex_quad_infinite(b))
        {
            a = check_complex_quad_sign(check_complex_quad_infinite(a) ? BUSTER_CHECK_QUAD_ONE : 0, a);
            b = check_complex_quad_sign(check_complex_quad_infinite(b) ? BUSTER_CHECK_QUAD_ONE : 0, b);
            c = check_quad_is_nan(c) ? check_complex_quad_sign(0, c) : c;
            d = check_quad_is_nan(d) ? check_complex_quad_sign(0, d) : d;
            recalculate = true;
        }
        if (check_complex_quad_infinite(c) || check_complex_quad_infinite(d))
        {
            c = check_complex_quad_sign(check_complex_quad_infinite(c) ? BUSTER_CHECK_QUAD_ONE : 0, c);
            d = check_complex_quad_sign(check_complex_quad_infinite(d) ? BUSTER_CHECK_QUAD_ONE : 0, d);
            a = check_quad_is_nan(a) ? check_complex_quad_sign(0, a) : a;
            b = check_quad_is_nan(b) ? check_complex_quad_sign(0, b) : b;
            recalculate = true;
        }
        if (!recalculate && (check_complex_quad_infinite(ac) || check_complex_quad_infinite(bd) || check_complex_quad_infinite(ad) ||
                             check_complex_quad_infinite(bc)))
        {
            a = check_quad_is_nan(a) ? check_complex_quad_sign(0, a) : a;
            b = check_quad_is_nan(b) ? check_complex_quad_sign(0, b) : b;
            c = check_quad_is_nan(c) ? check_complex_quad_sign(0, c) : c;
            d = check_quad_is_nan(d) ? check_complex_quad_sign(0, d) : d;
            recalculate = true;
        }
        if (recalculate)
        {
            const unsigned __int128 real = check_complex_quad_op(CHECK_OP_SUBTRACT, check_complex_quad_op(CHECK_OP_MULTIPLY, a, c),
                                                                 check_complex_quad_op(CHECK_OP_MULTIPLY, b, d));
            const unsigned __int128 imaginary = check_complex_quad_op(CHECK_OP_ADD, check_complex_quad_op(CHECK_OP_MULTIPLY, a, d),
                                                                      check_complex_quad_op(CHECK_OP_MULTIPLY, b, c));
            parts[0] = check_complex_quad_op(CHECK_OP_MULTIPLY, BUSTER_CHECK_QUAD_EXPONENT, real);
            parts[1] = check_complex_quad_op(CHECK_OP_MULTIPLY, BUSTER_CHECK_QUAD_EXPONENT, imaginary);
        }
    }
    return both;
}

// clang 23's HandleComplexComplexDiv (ExprConstant.cpp): c and d scaled by 2^-ilogb(maxnum(|c|, |d|)) where that is
// finite, (ac + bd) / (c^2 + d^2) and (bc - ad) / (c^2 + d^2) scaled back by the same power, and where both parts are
// NaNs, Annex G's recovery of a zero denominator, an infinite dividend or an infinite divisor, each step APFloat's.
static void check_complex_quad_divide(unsigned __int128 a, unsigned __int128 b, unsigned __int128 c, unsigned __int128 d, unsigned __int128 parts[2])
{
    const unsigned __int128 c_magnitude = c & ~BUSTER_CHECK_QUAD_SIGN;
    const unsigned __int128 d_magnitude = d & ~BUSTER_CHECK_QUAD_SIGN;
    // maxnum: a NaN loses to a number; of two numbers the larger, which the magnitudes' bits order.
    const unsigned __int128 largest = check_quad_is_nan(c_magnitude) ? d_magnitude : check_quad_is_nan(d_magnitude) ? c_magnitude :
                                      c_magnitude > d_magnitude ? c_magnitude : d_magnitude;
    const bool finite = check_quad_finite(largest);
    // ilogb of 0 is APFloat's IEK_Zero, whose scaling of a zero or a NaN changes neither.
    const int32_t power = finite && largest != 0 ? -check_quad_logb(largest) : 0;
    c = finite ? check_complex_quad_scalbn(c, power) : c;
    d = finite ? check_complex_quad_scalbn(d, power) : d;
    const unsigned __int128 denominator = check_complex_quad_op(CHECK_OP_ADD, check_complex_quad_op(CHECK_OP_MULTIPLY, c, c),
                                                                check_complex_quad_op(CHECK_OP_MULTIPLY, d, d));
    const unsigned __int128 real = check_complex_quad_op(CHECK_OP_ADD, check_complex_quad_op(CHECK_OP_MULTIPLY, a, c), check_complex_quad_op(CHECK_OP_MULTIPLY, b, d));
    const unsigned __int128 imaginary = check_complex_quad_op(CHECK_OP_SUBTRACT, check_complex_quad_op(CHECK_OP_MULTIPLY, b, c),
                                                              check_complex_quad_op(CHECK_OP_MULTIPLY, a, d));
    parts[0] = check_complex_quad_scalbn(check_complex_quad_op(CHECK_OP_DIVIDE, real, denominator), power);
    parts[1] = check_complex_quad_scalbn(check_complex_quad_op(CHECK_OP_DIVIDE, imaginary, denominator), power);
    if (check_quad_is_nan(parts[0]) && check_quad_is_nan(parts[1]))
    {
        const bool finite_a = check_quad_finite(a);
        const bool finite_b = check_quad_finite(b);
        if (denominator == 0 && (!check_quad_is_nan(a) || !check_quad_is_nan(b)))
        {
            const unsigned __int128 infinity = BUSTER_CHECK_QUAD_EXPONENT | (c & BUSTER_CHECK_QUAD_SIGN);
            parts[0] = check_complex_quad_op(CHECK_OP_MULTIPLY, infinity, a);
            parts[1] = check_complex_quad_op(CHECK_OP_MULTIPLY, infinity, b);
        }
        else if ((check_complex_quad_infinite(a) || check_complex_quad_infinite(b)) && check_quad_finite(c) && check_quad_finite(d))
        {
            a = check_complex_quad_sign(check_complex_quad_infinite(a) ? BUSTER_CHECK_QUAD_ONE : 0, a);
            b = check_complex_quad_sign(check_complex_quad_infinite(b) ? BUSTER_CHECK_QUAD_ONE : 0, b);
            parts[0] = check_complex_quad_op(CHECK_OP_MULTIPLY, BUSTER_CHECK_QUAD_EXPONENT,
                                             check_complex_quad_op(CHECK_OP_ADD, check_complex_quad_op(CHECK_OP_MULTIPLY, a, c),
                                                                   check_complex_quad_op(CHECK_OP_MULTIPLY, b, d)));
            parts[1] = check_complex_quad_op(CHECK_OP_MULTIPLY, BUSTER_CHECK_QUAD_EXPONENT,
                                             check_complex_quad_op(CHECK_OP_SUBTRACT, check_complex_quad_op(CHECK_OP_MULTIPLY, b, c),
                                                                   check_complex_quad_op(CHECK_OP_MULTIPLY, a, d)));
        }
        else if (check_complex_quad_infinite(largest) && finite_a && finite_b)
        {
            c = check_complex_quad_sign(check_complex_quad_infinite(c) ? BUSTER_CHECK_QUAD_ONE : 0, c);
            d = check_complex_quad_sign(check_complex_quad_infinite(d) ? BUSTER_CHECK_QUAD_ONE : 0, d);
            parts[0] = check_complex_quad_op(CHECK_OP_MULTIPLY, 0, check_complex_quad_op(CHECK_OP_ADD, check_complex_quad_op(CHECK_OP_MULTIPLY, a, c),
                                                                                         check_complex_quad_op(CHECK_OP_MULTIPLY, b, d)));
            parts[1] = check_complex_quad_op(CHECK_OP_MULTIPLY, 0, check_complex_quad_op(CHECK_OP_SUBTRACT, check_complex_quad_op(CHECK_OP_MULTIPLY, b, c),
                                                                                         check_complex_quad_op(CHECK_OP_MULTIPLY, a, d)));
        }
    }
}

// A _Complex __float128 operation of constants (#1250), its parts' bits a and b of the left operand, c and d of the right,
// `domains` as check_complex_fold's, as clang 23's evaluator folds it (ExprConstant.cpp's complex binary operators): + and
// - part by part, a real operand's missing part kept (a - (c + di) is (a - c) - di); unary + and - (a sign change alone,
// as changeSign's); * of a real operand part by part, else check_complex_quad_multiply; / by a real operand a / c and b /
// c, else check_complex_quad_divide (of b = +0 for a real left operand). Each operation is check_quad_arithmetic's.
// Outside required constant evaluation (`constant_evaluated` false) it folds what clang -O0's code computes as LLVM folds
// it: its * where no two NaNs call __multc3, and not its / by a complex value, which is __divtc3's. Whether it folded.
static __attribute__((noinline)) bool check_complex_quad_fold(CheckOp op, unsigned __int128 a, unsigned __int128 b, unsigned __int128 c, unsigned __int128 d,
                                                              uint32_t domains, bool constant_evaluated, CheckValue* value)
{
    const bool left_real = domains == 2;
    const bool right_real = domains == 1;
    unsigned __int128 parts[2] = { a, b };
    bool supported = true;
    if (op == CHECK_OP_NEGATIVE)
    {
        parts[0] = a ^ BUSTER_CHECK_QUAD_SIGN;
        parts[1] = b ^ BUSTER_CHECK_QUAD_SIGN;
    }
    else if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        parts[0] = check_quad_arithmetic(op, a, c);
        parts[1] = right_real ? b : left_real ? (op == CHECK_OP_ADD ? d : d ^ BUSTER_CHECK_QUAD_SIGN) : check_quad_arithmetic(op, b, d);
    }
    else if (op == CHECK_OP_MULTIPLY && (left_real || right_real))
    {
        // clang's evaluator takes a real right operand first (c * a); its code, which LLVM folds, the operands in order
        // (a * c): of two NaNs the first's.
        const bool swap = right_real && constant_evaluated;
        parts[0] = swap ? check_quad_arithmetic(op, c, a) : check_quad_arithmetic(op, a, c);
        parts[1] = swap ? check_quad_arithmetic(op, c, b) : right_real ? check_quad_arithmetic(op, b, c) : check_quad_arithmetic(op, a, d);
    }
    else if (op == CHECK_OP_MULTIPLY)
    {
        const bool both = check_complex_quad_multiply(a, b, c, d, parts);
        supported = constant_evaluated || !both;
    }
    else if (op == CHECK_OP_DIVIDE && right_real)
    {
        parts[0] = check_quad_arithmetic(op, a, c);
        parts[1] = check_quad_arithmetic(op, b, c);
    }
    else if (op == CHECK_OP_DIVIDE)
    {
        check_complex_quad_divide(a, left_real ? 0 : b, c, d, parts);
        supported = constant_evaluated;
    }
    else
    {
        supported = op == CHECK_OP_POSITIVE;
    }
    if (supported)
    {
        check_quad_complex_set(value, parts[0], parts[1]);
    }
    return supported;
}

// `domains` retains the original operands' complex domains (bits 0 and 1), before their common-type conversions.
// Host C then computes a mixed real/complex expression in that domain, as the runtime lowering does, preserving
// signed zeros and avoiding the extra zero products that converting a real operand to complex would introduce.
static uint32_t check_complex_fold(const TypeTable* table, CheckOp op, const CheckValue* left, const CheckValue* right, uint32_t domains, CheckValue* value,
                                   uint32_t node, uint32_t extensions, bool constant_evaluated)
{
    const TypeId element = type_element(table, type_strip_aligned(table, left->type));
    const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE;
    const bool comparison = op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL;
    const CheckValue ar = check_complex_component(table, left, false);
    const CheckValue ai = check_complex_component(table, left, true);
    const CheckValue br = check_complex_component(table, right, false);
    const CheckValue bi = check_complex_component(table, right, true);
    bool supported = true;
    const bool quad = type_unqualified(type_strip_aligned(table, element)) == BUSTER_TYPE_FLOAT128;
    if (comparison)
    {
        const bool integer = check_action_is_integer(table, element);
        uint32_t width = 0;
        bool sign = false;
        // An element past 64 bits compares its parts' limbs (#2219), which both operands must hold.
        const bool wide = integer && check_complex_wide_element(table, element, &width, &sign);
        const bool same = wide ? check_complex_wide_equal(left, right, width) :
                          integer ? left->integer == right->integer :
                          quad ? check_complex_quad_equal(ar.integer, br.integer) && check_complex_quad_equal(ai.integer, bi.integer) :
                                 left->floating == right->floating && left->imaginary == right->imaginary;
        supported = !wide || (ar.constant_kind == CHECK_CONSTANT_INTEGER && ai.constant_kind == CHECK_CONSTANT_INTEGER &&
                              br.constant_kind == CHECK_CONSTANT_INTEGER && bi.constant_kind == CHECK_CONSTANT_INTEGER);
        value->constant_kind = CHECK_CONSTANT_INTEGER;
        value->integer = op == CHECK_OP_EQUAL ? same : !same;
    }
    else if (quad)
    {
        supported = check_complex_quad_fold(op, ar.integer, ai.integer, br.integer, bi.integer, domains, constant_evaluated, value);
    }
    else if (check_action_float_supported(table, element))
    {
#define BUSTER_CHECK_COMPLEX_OPERATION(T) \
        do \
        { \
            T _Complex a; \
            T _Complex b; \
            T _Complex c = 0; \
            __real__ a = (T)ar.floating; \
            __imag__ a = (T)ai.floating; \
            __real__ b = (T)br.floating; \
            __imag__ b = (T)bi.floating; \
            switch (op) \
            { \
                case CHECK_OP_POSITIVE: c = a; break; \
                case CHECK_OP_NEGATIVE: c = -a; break; \
                case CHECK_OP_ADD: c = domains == 1 ? a + (T)br.floating : domains == 2 ? (T)ar.floating + b : a + b; break; \
                case CHECK_OP_SUBTRACT: c = domains == 1 ? a - (T)br.floating : domains == 2 ? (T)ar.floating - b : a - b; break; \
                case CHECK_OP_MULTIPLY: c = domains == 1 ? a * (T)br.floating : domains == 2 ? (T)ar.floating * b : a * b; break; \
                case CHECK_OP_DIVIDE: c = domains == 1 ? a / (T)br.floating : domains == 2 ? (T)ar.floating / b : a / b; break; \
                default: supported = false; break; \
            } \
            value->floating = __real__ c; \
            value->imaginary = __imag__ c; \
        } while (0)
        if (element == BUSTER_TYPE_FLOAT)
        {
            BUSTER_CHECK_COMPLEX_OPERATION(float);
        }
        else if (element == BUSTER_TYPE_DOUBLE)
        {
            BUSTER_CHECK_COMPLEX_OPERATION(double);
        }
        else
        {
            BUSTER_CHECK_COMPLEX_OPERATION(long double);
        }
#undef BUSTER_CHECK_COMPLEX_OPERATION
        value->constant_kind = CHECK_CONSTANT_COMPLEX;
    }
    else
    {
        // An integer element's parts: in `integer`'s halves up to 64 bits, past them limbs (#2219), each part's operation
        // the element's (check_complex_part_fold).
        uint32_t width = 0;
        bool sign = false;
        const bool narrow = check_action_integer_type(table, element, &width, &sign) && width <= 64;
        const bool wide = !narrow && check_complex_wide_element(table, element, &width, &sign) && ar.constant_kind == CHECK_CONSTANT_INTEGER &&
                          ai.constant_kind == CHECK_CONSTANT_INTEGER && br.constant_kind == CHECK_CONSTANT_INTEGER && bi.constant_kind == CHECK_CONSTANT_INTEGER;
        supported = (narrow || wide) && (unary || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE);
        if (supported)
        {
            const TypeId part_type = type_unqualified(element);
            CheckValue components[2] = { 0 };
            if (op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE)
            {
                const CheckValue products[4] = {
                    check_complex_part_fold(table, CHECK_OP_MULTIPLY, &ar, &br, part_type, width, sign, node, extensions),
                    check_complex_part_fold(table, CHECK_OP_MULTIPLY, &ai, &bi, part_type, width, sign, node, extensions),
                    check_complex_part_fold(table, CHECK_OP_MULTIPLY, &ar, &bi, part_type, width, sign, node, extensions),
                    check_complex_part_fold(table, CHECK_OP_MULTIPLY, &ai, &br, part_type, width, sign, node, extensions),
                };
                for (uint32_t part = 0; part < 4; part += 1)
                {
                    check_action_inherit(value, products + part);
                }
                components[0] = check_complex_part_fold(table, op == CHECK_OP_MULTIPLY ? CHECK_OP_SUBTRACT : CHECK_OP_ADD, products + 0, products + 1, part_type, width,
                                                        sign, node, extensions);
                components[1] = check_complex_part_fold(table, op == CHECK_OP_MULTIPLY ? CHECK_OP_ADD : CHECK_OP_SUBTRACT, products + 3, products + 2, part_type, width,
                                                        sign, node, extensions);
                if (op == CHECK_OP_DIVIDE)
                {
                    const CheckValue real_square = check_complex_part_fold(table, CHECK_OP_MULTIPLY, &br, &br, part_type, width, sign, node, extensions);
                    const CheckValue imaginary_square = check_complex_part_fold(table, CHECK_OP_MULTIPLY, &bi, &bi, part_type, width, sign, node, extensions);
                    const CheckValue divisor = check_complex_part_fold(table, CHECK_OP_ADD, &real_square, &imaginary_square, part_type, width, sign, node, extensions);
                    check_action_inherit(value, &real_square);
                    check_action_inherit(value, &imaginary_square);
                    check_action_inherit(value, &divisor);
                    for (uint32_t part = 0; part < 2; part += 1)
                    {
                        check_action_inherit(value, components + part);
                        components[part] = check_complex_part_fold(table, CHECK_OP_DIVIDE, components + part, &divisor, part_type, width, sign, node, extensions);
                    }
                }
            }
            else
            {
                for (uint32_t part = 0; part < 2; part += 1)
                {
                    components[part] = check_complex_part_fold(table, op, part == 0 ? &ar : &ai, part == 0 ? &br : &bi, part_type, width, sign, node, extensions);
                }
            }
            for (uint32_t part = 0; part < 2; part += 1)
            {
                check_action_inherit(value, components + part);
            }
            if (narrow)
            {
                value->integer = components[0].integer | (components[1].integer << 64);
                value->constant_kind = CHECK_CONSTANT_COMPLEX;
            }
            else if (value->fault == CHECK_FAULT_NONE && !check_complex_wide_set(table, value, components + 0, components + 1, width))
            {
                supported = false;
            }
        }
    }
    value->flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
    if (!supported)
    {
        check_action_set_fault(value, CHECK_FAULT_UNSUPPORTED_CONSTANT, node);
    }
    if (value->fault != CHECK_FAULT_NONE)
    {
        value->constant_kind = CHECK_CONSTANT_NONE;
    }
    // Component arithmetic is an implementation of the complex operation, not separate source integer
    // operators. GNU constant contexts diagnose the resulting extension at their own expression root.
    return 0;
}

static void check_complex_part(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue input = action->operands[0];
    const bool imaginary = action->op == CHECK_OP_IMAGINARY;
    const bool complex = check_action_kind(table, input.type) == BUSTER_TYPE_KIND_COMPLEX;
    if (!check_action_is_arithmetic(table, input.type))
    {
        check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
    }
    else if (complex)
    {
        result->value = check_complex_component(table, &input, imaginary);
        if (input.category == CHECK_LVALUE)
        {
            const TypeId element = type_element(table, type_strip_aligned(table, input.type));
            result->value.type = type_unqualified(element);
            result->value.flags |= (type_qualifiers(input.type) & BUSTER_TYPE_VOLATILE) != 0 ? BUSTER_CHECK_VOLATILE_PART : 0;
            if (input.constant_kind == CHECK_CONSTANT_ADDRESS && imaginary)
            {
                result->value.offset = (int64_t)((uint64_t)input.offset + type_size(table, element));
            }
        }
        result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
    }
    else if (!imaginary)
    {
        result->value = input;
    }
    else
    {
        const CheckValue evaluated = check_action_operand(table, action, result, 0);
        result->value = check_complex_component(table, &evaluated, true);
        result->value.flags &= ~(BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
    }
}

static void check_complex_conjugate(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    CheckValue input = check_action_operand(table, action, result, 0);
    if ((action->context.fp_environment & BUSTER_CHECK_FP_EVAL_MASK) != 0)
    {
        check_action_fp_promote(table, &input, &result->conversions[0], action->node, action->context.fp_environment);
    }
    result->value = input;
    result->value.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
    if (input.constant_kind == CHECK_CONSTANT_COMPLEX)
    {
        CheckValue imaginary = check_complex_component(table, &input, true);
        const TypeId element = imaginary.type;
        uint32_t width = 0;
        bool sign = false;
        if (check_action_is_integer(table, element) && check_complex_wide_element(table, element, &width, &sign))
        {
            // An element past 64 bits (#2219): the real part's limbs and the imaginary part's negation's.
            const CheckValue real = check_complex_component(table, &input, false);
            const CheckValue negated = check_complex_part_fold(table, CHECK_OP_NEGATIVE, &imaginary, &imaginary, element, width, sign, action->node,
                                                               action->context.extensions);
            check_action_inherit(&result->value, &negated);
            const bool folded = real.constant_kind == CHECK_CONSTANT_INTEGER && imaginary.constant_kind == CHECK_CONSTANT_INTEGER &&
                                result->value.fault == CHECK_FAULT_NONE && check_complex_wide_set(table, &result->value, &real, &negated, width);
            if (!folded)
            {
                result->value.constant_kind = CHECK_CONSTANT_NONE;
            }
        }
        else if (check_action_is_integer(table, element))
        {
            check_action_integer_type(table, element, &width, &sign);
            const CheckIntegerFold fold = { .active = true, .op = CHECK_OP_NEGATIVE, .left = imaginary.integer, .width = width,
                                           .right_width = width, .is_signed = sign, .right_signed = sign, .node = action->node, .extensions = action->context.extensions };
            check_action_fold_integer(fold, &imaginary);
            result->value.integer = (unsigned __int128)(uint64_t)input.integer | (imaginary.integer << 64);
            check_action_inherit(&result->value, &imaginary);
            if (result->value.fault != CHECK_FAULT_NONE)
            {
                result->value.constant_kind = CHECK_CONSTANT_NONE;
            }
        }
        else if (type_unqualified(type_strip_aligned(table, element)) == BUSTER_TYPE_FLOAT128)
        {
            check_quad_complex_set(&result->value, input.integer, imaginary.integer ^ BUSTER_CHECK_QUAD_SIGN);
        }
        else
        {
            result->value.imaginary = -input.imaginary;
        }
    }
}
