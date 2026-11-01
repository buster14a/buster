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
            if (real.constant_kind == CHECK_CONSTANT_FLOATING && imaginary.constant_kind == CHECK_CONSTANT_FLOATING)
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

// `domains` retains the original operands' complex domains (bits 0 and 1), before their common-type conversions.
// Host C then computes a mixed real/complex expression in that domain, as the runtime lowering does, preserving
// signed zeros and avoiding the extra zero products that converting a real operand to complex would introduce.
static uint32_t check_complex_fold(const TypeTable* table, CheckOp op, const CheckValue* left, const CheckValue* right, uint32_t domains, CheckValue* value,
                                   uint32_t node, uint32_t extensions)
{
    const TypeId element = type_element(table, type_strip_aligned(table, left->type));
    const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE;
    const bool comparison = op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL;
    const CheckValue ar = check_complex_component(table, left, false);
    const CheckValue ai = check_complex_component(table, left, true);
    const CheckValue br = check_complex_component(table, right, false);
    const CheckValue bi = check_complex_component(table, right, true);
    bool supported = true;
    if (comparison)
    {
        const bool integer = check_action_is_integer(table, element);
        const bool same = integer ? left->integer == right->integer : left->floating == right->floating && left->imaginary == right->imaginary;
        value->constant_kind = CHECK_CONSTANT_INTEGER;
        value->integer = op == CHECK_OP_EQUAL ? same : !same;
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
    const CheckValue input = check_action_operand(table, action, result, 0);
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
        else
        {
            result->value.imaginary = -input.imaginary;
        }
    }
}
