// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Scalar complex constants and GNU component operations. Included after the ordinary action helpers. Each
// component conversion calls only the noncomplex converter, so nested source types never recurse through it.
// Floating components use their actual target format. Integer-complex components occupy separate 64-bit halves.

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
                else
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
        const bool same = integer ? left->integer == right->integer :
                          quad ? check_complex_quad_equal(ar.integer, br.integer) && check_complex_quad_equal(ai.integer, bi.integer) :
                                 left->floating == right->floating && left->imaginary == right->imaginary;
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
        uint32_t width = 0;
        bool sign = false;
        supported = check_action_integer_type(table, element, &width, &sign) && width <= 64 &&
                    (unary || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE);
        if (supported)
        {
            CheckValue components[2] = { 0 };
            if (op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE)
            {
                const CheckValue products[4] = {
                    check_complex_integer_fold(CHECK_OP_MULTIPLY, ar.integer, br.integer, width, sign, node, extensions),
                    check_complex_integer_fold(CHECK_OP_MULTIPLY, ai.integer, bi.integer, width, sign, node, extensions),
                    check_complex_integer_fold(CHECK_OP_MULTIPLY, ar.integer, bi.integer, width, sign, node, extensions),
                    check_complex_integer_fold(CHECK_OP_MULTIPLY, ai.integer, br.integer, width, sign, node, extensions),
                };
                for (uint32_t part = 0; part < 4; part += 1)
                {
                    check_action_inherit(value, products + part);
                }
                components[0] = check_complex_integer_fold(op == CHECK_OP_MULTIPLY ? CHECK_OP_SUBTRACT : CHECK_OP_ADD,
                                                          products[0].integer, products[1].integer, width, sign, node, extensions);
                components[1] = check_complex_integer_fold(op == CHECK_OP_MULTIPLY ? CHECK_OP_ADD : CHECK_OP_SUBTRACT,
                                                          products[3].integer, products[2].integer, width, sign, node, extensions);
                if (op == CHECK_OP_DIVIDE)
                {
                    const CheckValue real_square = check_complex_integer_fold(CHECK_OP_MULTIPLY, br.integer, br.integer, width, sign, node, extensions);
                    const CheckValue imaginary_square = check_complex_integer_fold(CHECK_OP_MULTIPLY, bi.integer, bi.integer, width, sign, node, extensions);
                    const CheckValue divisor = check_complex_integer_fold(CHECK_OP_ADD, real_square.integer, imaginary_square.integer, width, sign, node, extensions);
                    check_action_inherit(value, &real_square);
                    check_action_inherit(value, &imaginary_square);
                    check_action_inherit(value, &divisor);
                    for (uint32_t part = 0; part < 2; part += 1)
                    {
                        check_action_inherit(value, components + part);
                        components[part] = check_complex_integer_fold(CHECK_OP_DIVIDE, components[part].integer, divisor.integer, width, sign, node, extensions);
                    }
                }
            }
            else
            {
                for (uint32_t part = 0; part < 2; part += 1)
                {
                    components[part] = check_complex_integer_fold(op, part == 0 ? ar.integer : ai.integer,
                                                                 part == 0 ? br.integer : bi.integer, width, sign, node, extensions);
                }
            }
            for (uint32_t part = 0; part < 2; part += 1)
            {
                check_action_inherit(value, components + part);
            }
            value->integer = components[0].integer | (components[1].integer << 64);
            value->constant_kind = CHECK_CONSTANT_COMPLEX;
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
        if (check_action_is_integer(table, element))
        {
            uint32_t width = 0;
            bool sign = false;
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
