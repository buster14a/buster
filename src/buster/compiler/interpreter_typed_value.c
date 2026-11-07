// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Two-word scalar semantics for the typed front. This consumes no IR function and performs no lowering. Narrow
// operations retain their existing oracle; wide arithmetic shares only payload decoding and checked casts with IR.

static uint32_t interpreter_typed_bytes(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_F80 ? 10 : tier0_class_bytes(class);
}

static InterpreterValue interpreter_typed_value(uint32_t class, InterpreterValue value)
{
    if (class == BUSTER_TYPED_CLASS_F80)
    {
        value.high &= 0xffff;
    }
    else if (!tier0_class_int128(class))
    {
        value.low = interpreter_typed_normalize(class, value.low);
        value.high = 0;
    }
    return value;
}

static InterpreterValue interpreter_typed_convert_value(uint32_t to, uint32_t from, InterpreterValue value, InterpreterStatus* status)
{
    InterpreterValue result = { 0 };
    const bool from_floating = tier0_class_floating(from) || tier0_class_x87(from);
    const bool to_floating = tier0_class_floating(to) || tier0_class_x87(to);
    const bool from_signed = tier0_class_signed(from) || from == BUSTER_TYPED_CLASS_I128;
    const bool to_signed = tier0_class_signed(to) || to == BUSTER_TYPED_CLASS_I128;
    const uint8_t source_type = (uint8_t)interpreter_typed_ir_type(from);
    const uint8_t result_type = (uint8_t)interpreter_typed_ir_type(to);
    if (!tier0_class_memory(from) && !tier0_class_memory(to))
    {
        result.low = interpreter_typed_convert(to, from, value.low, status);
    }
    else if (from_floating && to_floating)
    {
        result = interpreter_value_float_value(interpreter_value_float(value, source_type), result_type);
    }
    else if (to_floating)
    {
        const __int128 signed_value = interpreter_value_signed(value, source_type);
        const unsigned __int128 unsigned_value = interpreter_value_integer(value);
        long double number = 0;
        // Round directly to the destination precision. An x87 intermediate can turn a value just beyond a
        // float or double midpoint into a tie and round it a second time in the wrong direction.
        if (to == BUSTER_TYPED_CLASS_F32)
        {
            number = from_signed ? (float)signed_value : (float)unsigned_value;
        }
        else if (to == BUSTER_TYPED_CLASS_F64)
        {
            number = from_signed ? (double)signed_value : (double)unsigned_value;
        }
        else
        {
            number = from_signed ? (long double)signed_value : (long double)unsigned_value;
        }
        result = interpreter_value_float_value(number, result_type);
    }
    else if (from_floating)
    {
        unsigned __int128 converted = 0;
        *status = interpreter_value_float_integer(value, source_type, result_type, to_signed, &converted);
        result = interpreter_value_from_integer(converted, result_type);
    }
    else
    {
        const unsigned __int128 bits = from_signed ? (unsigned __int128)interpreter_value_signed(value, source_type) : interpreter_value_integer(value);
        result = interpreter_value_from_integer(bits, result_type);
    }
    result = interpreter_typed_value(to, result);
    return result;
}

static uint64_t interpreter_typed_compare_value(uint32_t kind, uint32_t class, InterpreterValue a, InterpreterValue b)
{
    uint64_t result = 0;
    if (class == BUSTER_TYPED_CLASS_F80)
    {
        const long double left = interpreter_value_float(a, IR_F80);
        const long double right = interpreter_value_float(b, IR_F80);
        switch (kind)
        {
        case BUSTER_TYPED_EQ: result = left == right; break;
        case BUSTER_TYPED_NE: result = left != right; break;
        case BUSTER_TYPED_LT: result = left < right; break;
        case BUSTER_TYPED_LE: result = left <= right; break;
        case BUSTER_TYPED_GT: result = left > right; break;
        case BUSTER_TYPED_GE: result = left >= right; break;
        default: break;
        }
    }
    else if (tier0_class_int128(class))
    {
        const unsigned __int128 left = interpreter_value_integer(a);
        const unsigned __int128 right = interpreter_value_integer(b);
        const __int128 signed_left = interpreter_value_signed(a, IR_I128);
        const __int128 signed_right = interpreter_value_signed(b, IR_I128);
        const bool sign = class == BUSTER_TYPED_CLASS_I128;
        switch (kind)
        {
        case BUSTER_TYPED_EQ: result = left == right; break;
        case BUSTER_TYPED_NE: result = left != right; break;
        case BUSTER_TYPED_LT: result = sign ? signed_left < signed_right : left < right; break;
        case BUSTER_TYPED_LE: result = sign ? signed_left <= signed_right : left <= right; break;
        case BUSTER_TYPED_GT: result = sign ? signed_left > signed_right : left > right; break;
        case BUSTER_TYPED_GE: result = sign ? signed_left >= signed_right : left >= right; break;
        default: break;
        }
    }
    else
    {
        result = interpreter_typed_compare(kind, class, a.low, b.low);
    }
    return result;
}

static InterpreterValue interpreter_typed_binary_value(uint32_t kind, uint32_t class, InterpreterValue a, InterpreterValue b, InterpreterStatus* status)
{
    InterpreterValue result = { 0 };
    if (class == BUSTER_TYPED_CLASS_F80)
    {
        const long double left = interpreter_value_float(a, IR_F80);
        const long double right = interpreter_value_float(b, IR_F80);
        long double number = 0;
        switch (kind)
        {
        case BUSTER_TYPED_ADD: number = left + right; break;
        case BUSTER_TYPED_SUB: number = left - right; break;
        case BUSTER_TYPED_MUL: number = left * right; break;
        case BUSTER_TYPED_DIV: number = left / right; break;
        default: *status = INTERPRETER_INVALID; break;
        }
        result = interpreter_value_float_value(number, IR_F80);
    }
    else if (tier0_class_int128(class))
    {
        const unsigned __int128 left = interpreter_value_integer(a);
        const unsigned __int128 right = interpreter_value_integer(b);
        const bool sign = class == BUSTER_TYPED_CLASS_I128;
        unsigned __int128 bits = 0;
        switch (kind)
        {
        case BUSTER_TYPED_ADD: bits = left + right; break;
        case BUSTER_TYPED_SUB: bits = left - right; break;
        case BUSTER_TYPED_MUL: bits = left * right; break;
        case BUSTER_TYPED_AND: bits = left & right; break;
        case BUSTER_TYPED_OR: bits = left | right; break;
        case BUSTER_TYPED_XOR: bits = left ^ right; break;
        case BUSTER_TYPED_DIV:
        case BUSTER_TYPED_MOD:
            if (right == 0 || (sign && left == ((unsigned __int128)1 << 127) && right == ~(unsigned __int128)0))
            {
                *status = INTERPRETER_ARITHMETIC;
            }
            else if (sign)
            {
                const __int128 signed_left = interpreter_value_signed(a, IR_I128);
                const __int128 signed_right = interpreter_value_signed(b, IR_I128);
                bits = (unsigned __int128)(kind == BUSTER_TYPED_DIV ? signed_left / signed_right : signed_left % signed_right);
            }
            else
            {
                bits = kind == BUSTER_TYPED_DIV ? left / right : left % right;
            }
            break;
        case BUSTER_TYPED_SHL:
        case BUSTER_TYPED_SHR:
            if (right >= 128)
            {
                *status = INTERPRETER_ARITHMETIC;
            }
            else if (kind == BUSTER_TYPED_SHL)
            {
                bits = left << (uint32_t)right;
            }
            else
            {
                bits = sign ? (unsigned __int128)(interpreter_value_signed(a, IR_I128) >> (uint32_t)right) : left >> (uint32_t)right;
            }
            break;
        default: *status = INTERPRETER_INVALID; break;
        }
        result = interpreter_value_from_integer(bits, IR_I128);
    }
    else if ((kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR) && b.high != 0)
    {
        // A narrow left operand may have a wide shift count. Its high word cannot be discarded before validation.
        *status = INTERPRETER_ARITHMETIC;
    }
    else
    {
        result.low = interpreter_typed_binary(kind, class, a.low, b.low, status);
    }
    return result;
}
