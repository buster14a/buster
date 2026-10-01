// Fixed-width two's-complement integer semantics, included by ir.c after
// ir_fast.c. This file is the one implementation of compile-time integer
// arithmetic: preprocessing conditionals, the parser's integer constant
// expressions, lowering's constant evaluator and value queries, the canonical
// FAST folder and every decoder of a CONSTANT_INTEGER row ask it instead of
// spelling an operation again, so two phases cannot give one expression two
// answers.
//
// A value is 1..128 bits held zero-extended in two u64 limbs (IrInteger).
// Operations are the canonical IR operations, so the operation chooses the
// signedness (SIGNED_DIVIDE, UNSIGNED_DIVIDE), never the operand. A result
// always carries the bits the operation defines modulo 2^width plus a mask of
// every rule the exact mathematical result broke (IrIntegerFault). The caller
// decides what a fault means in its phase -- an expression that is not an
// integer constant expression, a fold left for run time, an immediate that
// does not fit -- and owns the diagnostic. Nothing here reads a Target, a
// layout, a language type or a source range: target facts reach the kernel
// only as the widths a caller passes.
//
// Map, in file order:
//   ir_integer_mask, ir_integer_sign_extend       width normalization
//   ir_integer_limb_*                             portable 128-bit limb arithmetic
//   ir_integer_divide_unsigned                    shared division core
//   ir_integer_unary, ir_integer_binary,          canonical unary/binary/
//   ir_integer_convert                            conversion semantics
//   ir_integer_constant_decode                    CONSTANT_INTEGER rows
//   ir_semantic_census_*                          diagnostic-build counters

IrInteger ir_integer_mask(IrInteger value, u32 width)
{
    IrInteger result = value;
    if (width == 0)
    {
        result = (IrInteger){0};
    }
    else if (width < 64)
    {
        result.low &= ((u64)1 << width) - 1;
        result.high = 0;
    }
    else if (width == 64)
    {
        result.high = 0;
    }
    else if (width < 128)
    {
        result.high &= ((u64)1 << (width - 64)) - 1;
    }
    return result;
}

bool ir_integer_sign_bit(IrInteger value, u32 width)
{
    bool result = false;
    if (width != 0 && width <= 128)
    {
        u32 index = width - 1;
        result = index >= 64 ? ((value.high >> (index - 64)) & 1) != 0 : ((value.low >> index) & 1) != 0;
    }
    return result;
}

IrInteger ir_integer_sign_extend(IrInteger value, u32 from_width, u32 to_width)
{
    IrInteger result = ir_integer_mask(value, from_width);
    if (from_width != 0 && from_width < to_width && ir_integer_sign_bit(result, from_width))
    {
        IrInteger fill = ir_integer_mask((IrInteger){.low = UINT64_MAX, .high = UINT64_MAX}, from_width);
        result.low |= ~fill.low;
        result.high |= ~fill.high;
    }
    return ir_integer_mask(result, to_width);
}

IrInteger ir_integer_from_u64(u64 value, u32 width)
{
    return ir_integer_mask((IrInteger){.low = value}, width);
}

IrInteger ir_integer_from_s64(s64 value, u32 width)
{
    IrInteger result = {.low = (u64)value, .high = value < 0 ? UINT64_MAX : 0};
    return ir_integer_mask(result, width);
}

bool ir_integer_is_zero(IrInteger value)
{
    return value.low == 0 && value.high == 0;
}

bool ir_integer_equal(IrInteger left, IrInteger right)
{
    return left.low == right.low && left.high == right.high;
}

bool ir_integer_unsigned_less(IrInteger left, IrInteger right)
{
    return left.high < right.high || (left.high == right.high && left.low < right.low);
}

bool ir_integer_signed_less(IrInteger left, IrInteger right, u32 width)
{
    bool left_negative = ir_integer_sign_bit(left, width);
    bool right_negative = ir_integer_sign_bit(right, width);
    bool result = left_negative != right_negative ? left_negative : ir_integer_unsigned_less(left, right);
    return result;
}

bool ir_integer_to_s64(IrInteger value, u32 width, s64* result_out)
{
    IrInteger extended = ir_integer_sign_extend(value, width, 128);
    bool fits = extended.high == ((extended.low >> 63) ? UINT64_MAX : 0);
    if (fits)
    {
        *result_out = (s64)extended.low;
    }
    return fits;
}

BUSTER_GLOBAL_LOCAL IrInteger ir_integer_limb_add(IrInteger left, IrInteger right, bool* carry_out)
{
    IrInteger result = {.low = left.low + right.low};
    u64 carry = result.low < left.low;
    result.high = left.high + right.high + carry;
    *carry_out = result.high < left.high || (carry && result.high == left.high);
    return result;
}

BUSTER_GLOBAL_LOCAL IrInteger ir_integer_limb_negate(IrInteger value)
{
    IrInteger result = {.low = 0 - value.low, .high = ~value.high + (value.low == 0)};
    return result;
}

BUSTER_GLOBAL_LOCAL IrInteger ir_integer_limb_subtract(IrInteger left, IrInteger right)
{
    IrInteger result = {.low = left.low - right.low, .high = left.high - right.high - (left.low < right.low)};
    return result;
}

// count < 128
BUSTER_GLOBAL_LOCAL IrInteger ir_integer_limb_shift_left(IrInteger value, u32 count)
{
    IrInteger result = value;
    if (count >= 64)
    {
        result = (IrInteger){.high = value.low << (count - 64)};
    }
    else if (count != 0)
    {
        result = (IrInteger){.low = value.low << count, .high = (value.high << count) | (value.low >> (64 - count))};
    }
    return result;
}

// count < 128; logical
BUSTER_GLOBAL_LOCAL IrInteger ir_integer_limb_shift_right(IrInteger value, u32 count)
{
    IrInteger result = value;
    if (count >= 64)
    {
        result = (IrInteger){.low = value.high >> (count - 64)};
    }
    else if (count != 0)
    {
        result = (IrInteger){.low = (value.low >> count) | (value.high << (64 - count)), .high = value.high >> count};
    }
    return result;
}

// The exact 128-bit product of two 64-bit limbs, by 32-bit halves so every
// supported host compiler (MSVC has no 128-bit integer type) takes one path.
BUSTER_GLOBAL_LOCAL IrInteger ir_integer_limb_multiply_64(u64 left, u64 right)
{
    u64 left_low = (u32)left;
    u64 left_high = left >> 32;
    u64 right_low = (u32)right;
    u64 right_high = right >> 32;
    u64 low_low = left_low * right_low;
    u64 low_high = left_low * right_high;
    u64 high_low = left_high * right_low;
    u64 high_high = left_high * right_high;
    u64 middle = (low_low >> 32) + (u32)low_high + (u32)high_low;
    IrInteger result = {
        .low = (low_low & UINT64_C(0xffffffff)) | (middle << 32),
        .high = high_high + (low_high >> 32) + (high_low >> 32) + (middle >> 32),
    };
    return result;
}

// The exact 256-bit product of two 128-bit values: `low` holds bits 0..127,
// `high` bits 128..255.
BUSTER_GLOBAL_LOCAL void ir_integer_limb_multiply(IrInteger left, IrInteger right, IrInteger* low_out, IrInteger* high_out)
{
    IrInteger p00 = ir_integer_limb_multiply_64(left.low, right.low);
    IrInteger p01 = ir_integer_limb_multiply_64(left.low, right.high);
    IrInteger p10 = ir_integer_limb_multiply_64(left.high, right.low);
    IrInteger p11 = ir_integer_limb_multiply_64(left.high, right.high);
    // limb1 = p00.high + p01.low + p10.low, carries into limb2
    u64 limb1 = p00.high + p01.low;
    u64 carry1 = limb1 < p00.high;
    limb1 += p10.low;
    carry1 += limb1 < p10.low;
    u64 limb2 = p01.high + p10.high;
    u64 carry2 = limb2 < p01.high;
    limb2 += p11.low;
    carry2 += limb2 < p11.low;
    limb2 += carry1;
    carry2 += limb2 < carry1;
    u64 limb3 = p11.high + carry2;
    *low_out = (IrInteger){.low = p00.low, .high = limb1};
    *high_out = (IrInteger){.low = limb2, .high = limb3};
}

// Truncating unsigned division of 128-bit values; divisor nonzero.
BUSTER_GLOBAL_LOCAL void ir_integer_divide_unsigned(IrInteger dividend, IrInteger divisor, IrInteger* quotient_out, IrInteger* remainder_out)
{
    IrInteger quotient = {0};
    IrInteger remainder = {0};
    if (dividend.high == 0 && divisor.high == 0)
    {
        quotient.low = dividend.low / divisor.low;
        remainder.low = dividend.low % divisor.low;
    }
    else
    {
        for (u32 bit = 128; bit > 0; bit -= 1)
        {
            u32 index = bit - 1;
            remainder = ir_integer_limb_shift_left(remainder, 1);
            remainder.low |= index >= 64 ? (dividend.high >> (index - 64)) & 1 : (dividend.low >> index) & 1;
            if (!ir_integer_unsigned_less(remainder, divisor))
            {
                remainder = ir_integer_limb_subtract(remainder, divisor);
                if (index >= 64)
                {
                    quotient.high |= (u64)1 << (index - 64);
                }
                else
                {
                    quotient.low |= (u64)1 << index;
                }
            }
        }
    }
    *quotient_out = quotient;
    *remainder_out = remainder;
}

BUSTER_GLOBAL_LOCAL IrInteger ir_integer_signed_minimum(u32 width)
{
    return ir_integer_limb_shift_left((IrInteger){.low = 1}, width - 1);
}

BUSTER_GLOBAL_LOCAL IrInteger ir_integer_magnitude(IrInteger value, u32 width)
{
    IrInteger result = ir_integer_sign_bit(value, width) ? ir_integer_mask(ir_integer_limb_negate(value), width) : value;
    // The most negative value's magnitude is 2^(width-1), which the mask
    // above keeps only for widths below 128; extend explicitly there.
    if (ir_integer_sign_bit(value, width) && width == 128)
    {
        result = ir_integer_limb_negate(value);
    }
    return result;
}

// Arithmetic shift right at `width`; count < width.
BUSTER_GLOBAL_LOCAL IrInteger ir_integer_shift_right_arithmetic(IrInteger value, u32 count, u32 width)
{
    IrInteger extended = ir_integer_sign_extend(value, width, 128);
    IrInteger shifted = ir_integer_limb_shift_right(extended, count);
    if (count != 0 && ir_integer_sign_bit(value, width))
    {
        IrInteger fill = ir_integer_limb_shift_left((IrInteger){.low = UINT64_MAX, .high = UINT64_MAX}, 128 - count);
        shifted.low |= fill.low;
        shifted.high |= fill.high;
    }
    return ir_integer_mask(shifted, width);
}

BUSTER_GLOBAL_LOCAL u32 ir_integer_population(IrInteger value)
{
    u32 result = 0;
    for (u32 limb = 0; limb < 2; limb += 1)
    {
        u64 bits = limb ? value.high : value.low;
        while (bits)
        {
            bits &= bits - 1;
            result += 1;
        }
    }
    return result;
}

IrIntegerResult ir_integer_unary(IrUnaryOperation operation, IrInteger operand, u32 width)
{
    IR_SEMANTIC_RECORD(KERNEL_UNARY, 1);
    IrIntegerResult result = {0};
    IrInteger value = ir_integer_mask(operand, width);
    if (width == 0 || width > 128)
    {
        result.faults = IR_INTEGER_FAULT_UNSUPPORTED;
    }
    else
    {
        switch (operation)
        {
        case IR_UNARY_INTEGER_NEGATE:
            result.bits = ir_integer_mask(ir_integer_limb_negate(value), width);
            IR_SEMANTIC_RECORD(KERNEL_OVERFLOW_CHECKS, 1);
            if (!ir_integer_is_zero(value))
            {
                result.faults |= IR_INTEGER_FAULT_UNSIGNED_WRAP;
            }
            if (ir_integer_equal(value, ir_integer_mask(ir_integer_signed_minimum(width), width)))
            {
                result.faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
            }
            break;
        case IR_UNARY_INTEGER_BITWISE_NOT:
            result.bits = ir_integer_mask((IrInteger){.low = ~value.low, .high = ~value.high}, width);
            break;
        case IR_UNARY_BOOLEAN_NOT:
            result.bits.low = ir_integer_is_zero(value);
            break;
        case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS:
        case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS:
        {
            u32 count = 0;
            if (ir_integer_is_zero(value))
            {
                count = width;
                result.faults |= IR_INTEGER_FAULT_ZERO_COUNT;
            }
            else if (operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS)
            {
                while (!ir_integer_sign_bit(ir_integer_limb_shift_left(value, count), width))
                {
                    count += 1;
                }
            }
            else
            {
                while (!((count >= 64 ? value.high >> (count - 64) : value.low >> count) & 1))
                {
                    count += 1;
                }
            }
            result.bits = ir_integer_from_u64(count, width);
        }
        break;
        case IR_UNARY_INTEGER_POPULATION_COUNT:
            result.bits = ir_integer_from_u64(ir_integer_population(value), width);
            break;
        case IR_UNARY_FLOAT_NEGATE:
        case IR_UNARY_VECTOR_INTEGER_NEGATE:
        case IR_UNARY_VECTOR_FLOAT_NEGATE:
        case IR_UNARY_VECTOR_INTEGER_BITWISE_NOT:
        case IR_UNARY_COUNT:
            result.faults = IR_INTEGER_FAULT_UNSUPPORTED;
            break;
        }
    }
    IR_SEMANTIC_RECORD(KERNEL_FAULTS, result.faults != 0);
    return result;
}

BUSTER_GLOBAL_LOCAL IrIntegerResult ir_integer_shift(IrBinaryOperation operation, IrInteger value, IrInteger count_value, u32 width)
{
    IrIntegerResult result = {0};
    if (count_value.high != 0 || count_value.low >= width)
    {
        result.faults = IR_INTEGER_FAULT_SHIFT_COUNT;
    }
    else
    {
        u32 count = (u32)count_value.low;
        if (operation == IR_BINARY_SHIFT_LEFT)
        {
            result.bits = ir_integer_mask(ir_integer_limb_shift_left(value, count), width);
            IR_SEMANTIC_RECORD(KERNEL_OVERFLOW_CHECKS, 1);
            if (!ir_integer_equal(ir_integer_limb_shift_right(result.bits, count), value))
            {
                result.faults |= IR_INTEGER_FAULT_UNSIGNED_WRAP;
            }
            if (ir_integer_sign_bit(value, width))
            {
                result.faults |= IR_INTEGER_FAULT_NEGATIVE_SHIFTED;
            }
            if (!ir_integer_equal(ir_integer_shift_right_arithmetic(result.bits, count, width), value))
            {
                result.faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
            }
        }
        else if (operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT)
        {
            result.bits = ir_integer_limb_shift_right(value, count);
        }
        else
        {
            result.bits = ir_integer_shift_right_arithmetic(value, count, width);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrIntegerResult ir_integer_divide(IrBinaryOperation operation, IrInteger left, IrInteger right, u32 width)
{
    IrIntegerResult result = {0};
    bool is_signed = operation == IR_BINARY_SIGNED_DIVIDE || operation == IR_BINARY_SIGNED_REMAINDER;
    bool remainder = operation == IR_BINARY_SIGNED_REMAINDER || operation == IR_BINARY_UNSIGNED_REMAINDER;
    IR_SEMANTIC_RECORD(KERNEL_OVERFLOW_CHECKS, 1);
    if (ir_integer_is_zero(right))
    {
        result.faults = IR_INTEGER_FAULT_DIVIDE_BY_ZERO;
    }
    else
    {
        bool left_negative = is_signed && ir_integer_sign_bit(left, width);
        bool right_negative = is_signed && ir_integer_sign_bit(right, width);
        IrInteger quotient = {0};
        IrInteger remainder_bits = {0};
        ir_integer_divide_unsigned(left_negative ? ir_integer_magnitude(left, width) : left,
                                   right_negative ? ir_integer_magnitude(right, width) : right, &quotient, &remainder_bits);
        if (left_negative != right_negative)
        {
            quotient = ir_integer_limb_negate(quotient);
        }
        if (left_negative)
        {
            remainder_bits = ir_integer_limb_negate(remainder_bits);
        }
        // Only the most negative value divided by -1 leaves the signed range;
        // C and the canonical IR both define neither its quotient nor its
        // remainder, so both carry the fault with their wrapped bits.
        if (is_signed && left_negative && right_negative &&
            ir_integer_equal(left, ir_integer_mask(ir_integer_signed_minimum(width), width)) &&
            ir_integer_equal(right, ir_integer_mask((IrInteger){.low = UINT64_MAX, .high = UINT64_MAX}, width)))
        {
            result.faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
        }
        result.bits = ir_integer_mask(remainder ? remainder_bits : quotient, width);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrIntegerResult ir_integer_multiply(IrInteger left, IrInteger right, u32 width)
{
    IrIntegerResult result = {0};
    IrInteger low = {0};
    IrInteger high = {0};
    ir_integer_limb_multiply(left, right, &low, &high);
    result.bits = ir_integer_mask(low, width);
    IR_SEMANTIC_RECORD(KERNEL_OVERFLOW_CHECKS, 1);
    if (!ir_integer_is_zero(high) || !ir_integer_equal(low, result.bits))
    {
        result.faults |= IR_INTEGER_FAULT_UNSIGNED_WRAP;
    }
    bool left_negative = ir_integer_sign_bit(left, width);
    bool right_negative = ir_integer_sign_bit(right, width);
    IrInteger magnitude_low = {0};
    IrInteger magnitude_high = {0};
    ir_integer_limb_multiply(ir_integer_magnitude(left, width), ir_integer_magnitude(right, width), &magnitude_low, &magnitude_high);
    // |result| must be at most 2^(width-1) - 1, or exactly 2^(width-1) when
    // the result is negative.
    IrInteger limit = ir_integer_signed_minimum(width);
    bool negative = left_negative != right_negative && !ir_integer_is_zero(magnitude_low);
    bool overflow = !ir_integer_is_zero(magnitude_high) ||
                    (negative ? ir_integer_unsigned_less(limit, magnitude_low) : !ir_integer_unsigned_less(magnitude_low, limit));
    if (overflow)
    {
        result.faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
    }
    return result;
}

IrIntegerResult ir_integer_binary(IrBinaryOperation operation, IrInteger left_operand, IrInteger right_operand, u32 width, u32 right_width)
{
    IR_SEMANTIC_RECORD(KERNEL_BINARY, 1);
    IrIntegerResult result = {0};
    IrInteger left = ir_integer_mask(left_operand, width);
    IrInteger right = ir_integer_mask(right_operand, right_width);
    bool shift = operation == IR_BINARY_SHIFT_LEFT || operation == IR_BINARY_SIGNED_SHIFT_RIGHT || operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT;
    if (width == 0 || width > 128 || right_width == 0 || right_width > 128 || (!shift && right_width != width))
    {
        result.faults = IR_INTEGER_FAULT_UNSUPPORTED;
    }
    else
    {
        switch (operation)
        {
        case IR_BINARY_INTEGER_ADD:
        case IR_BINARY_INTEGER_SUBTRACT:
        {
            bool subtract = operation == IR_BINARY_INTEGER_SUBTRACT;
            bool carry = false;
            IrInteger exact = subtract ? ir_integer_limb_subtract(left, right) : ir_integer_limb_add(left, right, &carry);
            result.bits = ir_integer_mask(exact, width);
            IR_SEMANTIC_RECORD(KERNEL_OVERFLOW_CHECKS, 1);
            if (subtract ? ir_integer_unsigned_less(left, right) : carry || !ir_integer_equal(exact, result.bits))
            {
                result.faults |= IR_INTEGER_FAULT_UNSIGNED_WRAP;
            }
            bool left_negative = ir_integer_sign_bit(left, width);
            bool right_negative = ir_integer_sign_bit(right, width) != subtract;
            // Subtracting zero never overflows even though zero's negation
            // is not negative; the sign rule below would otherwise see
            // `x - 0` as `x + (-0)` with a flipped sign.
            if (!(subtract && ir_integer_is_zero(right)) && left_negative == right_negative &&
                ir_integer_sign_bit(result.bits, width) != left_negative)
            {
                result.faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
            }
        }
        break;
        case IR_BINARY_INTEGER_MULTIPLY: result = ir_integer_multiply(left, right, width); break;
        case IR_BINARY_SIGNED_DIVIDE:
        case IR_BINARY_UNSIGNED_DIVIDE:
        case IR_BINARY_SIGNED_REMAINDER:
        case IR_BINARY_UNSIGNED_REMAINDER: result = ir_integer_divide(operation, left, right, width); break;
        case IR_BINARY_SHIFT_LEFT:
        case IR_BINARY_SIGNED_SHIFT_RIGHT:
        case IR_BINARY_UNSIGNED_SHIFT_RIGHT: result = ir_integer_shift(operation, left, right, width); break;
        case IR_BINARY_INTEGER_BITWISE_AND: result.bits = (IrInteger){.low = left.low & right.low, .high = left.high & right.high}; break;
        case IR_BINARY_INTEGER_BITWISE_OR: result.bits = (IrInteger){.low = left.low | right.low, .high = left.high | right.high}; break;
        case IR_BINARY_INTEGER_BITWISE_XOR: result.bits = (IrInteger){.low = left.low ^ right.low, .high = left.high ^ right.high}; break;
        case IR_BINARY_BOOLEAN_AND: result.bits.low = !ir_integer_is_zero(left) && !ir_integer_is_zero(right); break;
        case IR_BINARY_BOOLEAN_OR: result.bits.low = !ir_integer_is_zero(left) || !ir_integer_is_zero(right); break;
        case IR_BINARY_INTEGER_EQUAL:
        case IR_BINARY_BOOLEAN_EQUAL: result.bits.low = ir_integer_equal(left, right); break;
        case IR_BINARY_INTEGER_NOT_EQUAL:
        case IR_BINARY_BOOLEAN_NOT_EQUAL: result.bits.low = !ir_integer_equal(left, right); break;
        case IR_BINARY_UNSIGNED_LESS: result.bits.low = ir_integer_unsigned_less(left, right); break;
        case IR_BINARY_UNSIGNED_LESS_EQUAL: result.bits.low = !ir_integer_unsigned_less(right, left); break;
        case IR_BINARY_UNSIGNED_GREATER: result.bits.low = ir_integer_unsigned_less(right, left); break;
        case IR_BINARY_UNSIGNED_GREATER_EQUAL: result.bits.low = !ir_integer_unsigned_less(left, right); break;
        case IR_BINARY_SIGNED_LESS: result.bits.low = ir_integer_signed_less(left, right, width); break;
        case IR_BINARY_SIGNED_LESS_EQUAL: result.bits.low = !ir_integer_signed_less(right, left, width); break;
        case IR_BINARY_SIGNED_GREATER: result.bits.low = ir_integer_signed_less(right, left, width); break;
        case IR_BINARY_SIGNED_GREATER_EQUAL: result.bits.low = !ir_integer_signed_less(left, right, width); break;
        case IR_BINARY_FLOAT_ADD:
        case IR_BINARY_FLOAT_SUBTRACT:
        case IR_BINARY_FLOAT_MULTIPLY:
        case IR_BINARY_FLOAT_DIVIDE:
        case IR_BINARY_FLOAT_EQUAL:
        case IR_BINARY_FLOAT_NOT_EQUAL:
        case IR_BINARY_POINTER_EQUAL:
        case IR_BINARY_POINTER_NOT_EQUAL:
        case IR_BINARY_FLOAT_LESS:
        case IR_BINARY_FLOAT_LESS_EQUAL:
        case IR_BINARY_FLOAT_GREATER:
        case IR_BINARY_FLOAT_GREATER_EQUAL:
        case IR_BINARY_RANGE:
        case IR_BINARY_VECTOR_INTEGER_ADD:
        case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
        case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
        case IR_BINARY_VECTOR_SIGNED_DIVIDE:
        case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
        case IR_BINARY_VECTOR_FLOAT_ADD:
        case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
        case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
        case IR_BINARY_VECTOR_FLOAT_DIVIDE:
        case IR_BINARY_VECTOR_SIGNED_REMAINDER:
        case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
        case IR_BINARY_VECTOR_SHIFT_LEFT:
        case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
        case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
        case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
        case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
        case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        case IR_BINARY_VECTOR_INTEGER_EQUAL:
        case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
        case IR_BINARY_VECTOR_SIGNED_LESS:
        case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
        case IR_BINARY_VECTOR_SIGNED_GREATER:
        case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
        case IR_BINARY_VECTOR_UNSIGNED_LESS:
        case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
        case IR_BINARY_VECTOR_UNSIGNED_GREATER:
        case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_LESS:
        case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
        case IR_BINARY_VECTOR_FLOAT_GREATER:
        case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
        case IR_BINARY_COUNT: result.faults = IR_INTEGER_FAULT_UNSUPPORTED; break;
        }
    }
    IR_SEMANTIC_RECORD(KERNEL_FAULTS, result.faults != 0);
    return result;
}

bool ir_integer_binary_is_comparison(IrBinaryOperation operation)
{
    bool result = false;
    switch (operation)
    {
    case IR_BINARY_BOOLEAN_AND:
    case IR_BINARY_BOOLEAN_OR:
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
    case IR_BINARY_UNSIGNED_LESS:
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
    case IR_BINARY_UNSIGNED_GREATER:
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
    case IR_BINARY_SIGNED_LESS:
    case IR_BINARY_SIGNED_LESS_EQUAL:
    case IR_BINARY_SIGNED_GREATER:
    case IR_BINARY_SIGNED_GREATER_EQUAL: result = true; break;
    default: break;
    }
    return result;
}

IrIntegerResult ir_integer_convert(IrConversionOperation operation, IrInteger operand, u32 source_width, u32 target_width)
{
    IR_SEMANTIC_RECORD(KERNEL_CONVERSIONS, 1);
    IrIntegerResult result = {0};
    IrInteger value = ir_integer_mask(operand, source_width);
    if (source_width == 0 || source_width > 128 || target_width == 0 || target_width > 128)
    {
        result.faults = IR_INTEGER_FAULT_UNSUPPORTED;
    }
    else
    {
        switch (operation)
        {
        case IR_CONVERSION_IDENTITY:
        case IR_CONVERSION_INTEGER_REINTERPRET:
        case IR_CONVERSION_INTEGER_ZERO_EXTEND:
        case IR_CONVERSION_INTEGER_TRUNCATE: result.bits = ir_integer_mask(value, target_width); break;
        case IR_CONVERSION_INTEGER_SIGN_EXTEND: result.bits = ir_integer_sign_extend(value, source_width, target_width); break;
        case IR_CONVERSION_FLOAT_EXTEND:
        case IR_CONVERSION_FLOAT_TRUNCATE:
        case IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT:
        case IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT:
        case IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER:
        case IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER:
        case IR_CONVERSION_POINTER_REINTERPRET:
        case IR_CONVERSION_POINTER_TO_INTEGER:
        case IR_CONVERSION_INTEGER_TO_POINTER:
        case IR_CONVERSION_COUNT: result.faults = IR_INTEGER_FAULT_UNSUPPORTED; break;
        }
        // Which reading of the source survives: a zero extension keeps the
        // unsigned one, a sign extension the signed one, a truncation
        // possibly neither.
        if (!result.faults)
        {
            IR_SEMANTIC_RECORD(KERNEL_OVERFLOW_CHECKS, 1);
            if (!ir_integer_equal(result.bits, value))
            {
                result.faults |= IR_INTEGER_FAULT_UNSIGNED_WRAP;
            }
            if (!ir_integer_equal(ir_integer_sign_extend(result.bits, target_width, 128), ir_integer_sign_extend(value, source_width, 128)))
            {
                result.faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
            }
        }
    }
    IR_SEMANTIC_RECORD(KERNEL_FAULTS, result.faults != 0);
    return result;
}

u32 ir_integer_type_width(IrType const* type)
{
    u32 result = 0;
    if (type && (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_ENUM))
    {
        result = type->kind == IR_TYPE_BOOLEAN ? 1 : type->bit_width;
        if (result > 128)
        {
            result = 0;
        }
    }
    return result;
}

IrInteger ir_integer_from_magnitude(IrInteger magnitude, bool negative, u32 width)
{
    IR_SEMANTIC_RECORD(KERNEL_CONSTANT_DECODES, 1);
    return ir_integer_mask(negative ? ir_integer_limb_negate(magnitude) : magnitude, width);
}

// A CONSTANT_INTEGER row spells its value as a magnitude and a sign flag; the
// canonical meaning is that signed number reduced modulo 2^width. This is
// the only decoder: the FAST folder, lowering's value queries, the validator's
// index reader and the census all read a row through it.
bool ir_integer_constant_decode(IrInstruction const* row, u32 width, IrInteger* value_out)
{
    bool result = row && row->opcode == IR_OPCODE_CONSTANT_INTEGER && row->immediate_count >= 1 && row->immediate_count <= 2 &&
                  row->immediates && width != 0 && width <= 128;
    if (result)
    {
        IrInteger magnitude = {.low = row->immediates[0], .high = row->immediate_count > 1 ? row->immediates[1] : 0};
        *value_out = ir_integer_from_magnitude(magnitude, row->immediate_is_negative, width);
        // A spelling whose signed value lies outside [-2^(width-1), 2^width)
        // decodes to the same bits as a canonical one, but every reader that
        // does not mask -- the native emitters materialize the raw
        // magnitude -- would see a different number.
        IR_SEMANTIC_RECORD(NONCANONICAL_CONSTANTS, !ir_integer_constant_canonical(row, width));
    }
    return result;
}

bool ir_integer_constant_canonical(IrInstruction const* row, u32 width)
{
    bool result = row && row->opcode == IR_OPCODE_CONSTANT_INTEGER && row->immediate_count >= 1 && row->immediate_count <= 2 &&
                  row->immediates && width != 0 && width <= 128;
    if (result)
    {
        IrInteger magnitude = {.low = row->immediates[0], .high = row->immediate_count > 1 ? row->immediates[1] : 0};
        // non-negative: magnitude < 2^width; negative: magnitude <= 2^(width-1)
        result = row->immediate_is_negative ? !ir_integer_unsigned_less(ir_integer_signed_minimum(width), magnitude)
                                            : width == 128 || ir_integer_unsigned_less(magnitude, ir_integer_limb_shift_left((IrInteger){.low = 1}, width));
    }
    return result;
}

#if BUSTER_BENCH_ALLOCATIONS
BUSTER_GLOBAL_LOCAL IrSemanticCounters ir_semantic_counter_values;

// Range identities, one bit per evaluator, keyed by the token array and the
// range: a range seen before by any evaluator is a repeated evaluation of the
// same fact. The table is diagnostic-build storage only, never cleared, and
// saturates into `range_census_saturated` rather than growing.
enum
{
    IR_SEMANTIC_RANGE_CAPACITY = 1u << 21,
};

typedef struct IrSemanticRangeSlot IrSemanticRangeSlot;
struct IrSemanticRangeSlot
{
    u64 key;
    u32 evaluators;
    u32 occupied;
};

BUSTER_GLOBAL_LOCAL IrSemanticRangeSlot ir_semantic_range_slots[IR_SEMANTIC_RANGE_CAPACITY];
BUSTER_GLOBAL_LOCAL u32 ir_semantic_range_count;

void ir_semantic_record(IrSemanticCounter counter, u64 amount)
{
    if ((u32)counter < IR_SEMANTIC_COUNT)
    {
        u64 before = ir_semantic_counter_values.values[counter];
        ir_semantic_counter_values.values[counter] = before + amount;
        if (ir_semantic_counter_values.values[counter] < before)
        {
            ir_semantic_counter_values.values[counter] = UINT64_MAX;
            ir_semantic_counter_values.overflowed = true;
        }
    }
}

void ir_semantic_census_range(IrSemanticEvaluator evaluator, void const* base, u32 start, u32 end)
{
    u64 key = ((u64)(uintptr_t)base * UINT64_C(0x9e3779b97f4a7c15)) ^ ((u64)start << 32) ^ end;
    key |= 1;
    u32 slot = (u32)((key ^ (key >> 29)) * UINT64_C(0xbf58476d1ce4e5b9) >> 43) & (IR_SEMANTIC_RANGE_CAPACITY - 1);
    u32 probes = 0;
    while (ir_semantic_range_slots[slot].occupied && ir_semantic_range_slots[slot].key != key && probes < IR_SEMANTIC_RANGE_CAPACITY)
    {
        slot = (slot + 1) & (IR_SEMANTIC_RANGE_CAPACITY - 1);
        probes += 1;
    }
    u32 bit = (u32)1 << (u32)evaluator;
    ir_semantic_record(IR_SEMANTIC_RANGE_EVALUATIONS, 1);
    if (ir_semantic_range_slots[slot].occupied && ir_semantic_range_slots[slot].key == key)
    {
        u32 seen = ir_semantic_range_slots[slot].evaluators;
        ir_semantic_record(IR_SEMANTIC_REPEATED_RANGE_EVALUATIONS, 1);
        ir_semantic_record(IR_SEMANTIC_REPEATED_BY_ANOTHER_EVALUATOR, (seen & ~bit) != 0);
        ir_semantic_record((IrSemanticCounter)(IR_SEMANTIC_REPEATED_PREPROCESSOR + (u32)evaluator), 1);
        u32 parse_bits = (1u << IR_SEMANTIC_EVALUATOR_PARSE_LEGACY) | (1u << IR_SEMANTIC_EVALUATOR_PARSE_TYPED);
        ir_semantic_record(IR_SEMANTIC_FIRST_SEEN_BY_PARSE_AGAIN_IN_LOWERING, (seen & parse_bits) && !(bit & parse_bits) && !(seen & bit));
        ir_semantic_range_slots[slot].evaluators = seen | bit;
    }
    else if (ir_semantic_range_count < IR_SEMANTIC_RANGE_CAPACITY / 2)
    {
        ir_semantic_range_slots[slot] = (IrSemanticRangeSlot){.key = key, .evaluators = bit, .occupied = 1};
        ir_semantic_range_count += 1;
        ir_semantic_record(IR_SEMANTIC_DISTINCT_RANGES, 1);
    }
    else
    {
        ir_semantic_record(IR_SEMANTIC_RANGE_CENSUS_SATURATED, 1);
    }
    ir_semantic_record((IrSemanticCounter)(IR_SEMANTIC_PREPROCESSOR_EVALUATIONS + (u32)evaluator), 1);
}

IrSemanticCounters ir_semantic_counters(void)
{
    return ir_semantic_counter_values;
}

String8 ir_semantic_counter_name(IrSemanticCounter counter)
{
    String8 names[] = {
#define IR_SEMANTIC_NAME(id, name) S8(#name),
        IR_SEMANTIC_COUNTERS(IR_SEMANTIC_NAME)
#undef IR_SEMANTIC_NAME
    };
    String8 result = (u32)counter < BUSTER_ARRAY_LENGTH(names) ? names[counter] : (String8){0};
    return result;
}
#endif
