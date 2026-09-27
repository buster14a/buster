// Included by ir_test.c. Validates the shared integer semantics (ir_integer.c)
// against oracles that do not share its algorithm:
// - every operation, every operand pair and every conversion at the reduced
//   widths 1..8, against exact s64 arithmetic on the operands' signed and
//   unsigned readings;
// - seeded random operands at 16/32/64 bits against exact 128-bit host
//   arithmetic and at 128 bits against the host type's wrapped results and
//   division's defining identities, where the host has a 128-bit type;
// - a boundary table of named answers at 8/32/64/128 bits;
// - the CONSTANT_INTEGER decoder on every sign/magnitude spelling class.
// Any disagreement fails with the operation, width and operands.

BUSTER_GLOBAL_LOCAL u64 ir_integer_test_mask(u32 width)
{
    return width >= 64 ? UINT64_MAX : ((u64)1 << width) - 1;
}

BUSTER_GLOBAL_LOCAL s64 ir_integer_test_signed(u64 value, u32 width)
{
    u64 bits = value & ir_integer_test_mask(width);
    s64 result = (s64)bits;
    if (width < 64 && (bits >> (width - 1)) & 1)
    {
        result = (s64)bits - ((s64)1 << width);
    }
    return result;
}

// Floor division by a power of two, without shifting a negative value.
BUSTER_GLOBAL_LOCAL s64 ir_integer_test_floor_shift(s64 value, u32 count)
{
    s64 divisor = (s64)1 << count;
    s64 quotient = value / divisor;
    if (value < 0 && quotient * divisor != value)
    {
        quotient -= 1;
    }
    return quotient;
}

BUSTER_GLOBAL_LOCAL u8 ir_integer_test_range_faults(s64 exact_unsigned, s64 exact_signed, u32 width)
{
    s64 unsigned_maximum = (s64)ir_integer_test_mask(width);
    s64 signed_minimum = -((s64)1 << (width - 1));
    s64 signed_maximum = ((s64)1 << (width - 1)) - 1;
    u8 faults = 0;
    if (exact_unsigned < 0 || exact_unsigned > unsigned_maximum)
    {
        faults |= IR_INTEGER_FAULT_UNSIGNED_WRAP;
    }
    if (exact_signed < signed_minimum || exact_signed > signed_maximum)
    {
        faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
    }
    return faults;
}

// Widths 1..8 only: every exact intermediate fits an s64 comfortably.
BUSTER_GLOBAL_LOCAL IrIntegerResult ir_integer_test_oracle_binary(IrBinaryOperation operation, u64 left, u64 right, u32 width, u32 right_width)
{
    u64 mask = ir_integer_test_mask(width);
    s64 ul = (s64)(left & mask);
    s64 ur = (s64)(right & ir_integer_test_mask(right_width));
    s64 sl = ir_integer_test_signed(left, width);
    s64 sr = ir_integer_test_signed(right, width);
    s64 signed_minimum = -((s64)1 << (width - 1));
    IrIntegerResult result = {0};
    bool arithmetic = false;
    s64 exact_unsigned = 0;
    s64 exact_signed = 0;
    switch (operation)
    {
    case IR_BINARY_INTEGER_ADD: arithmetic = true; exact_unsigned = ul + ur; exact_signed = sl + sr; break;
    case IR_BINARY_INTEGER_SUBTRACT: arithmetic = true; exact_unsigned = ul - ur; exact_signed = sl - sr; break;
    case IR_BINARY_INTEGER_MULTIPLY: arithmetic = true; exact_unsigned = ul * ur; exact_signed = sl * sr; break;
    case IR_BINARY_SIGNED_DIVIDE:
    case IR_BINARY_SIGNED_REMAINDER:
        if (sr == 0)
        {
            result.faults = IR_INTEGER_FAULT_DIVIDE_BY_ZERO;
        }
        else
        {
            s64 quotient = sl / sr;
            s64 remainder = sl - quotient * sr;
            result.bits.low = (u64)(operation == IR_BINARY_SIGNED_DIVIDE ? quotient : remainder) & mask;
            if (sl == signed_minimum && sr == -1)
            {
                result.faults = IR_INTEGER_FAULT_SIGNED_OVERFLOW;
            }
        }
        break;
    case IR_BINARY_UNSIGNED_DIVIDE:
    case IR_BINARY_UNSIGNED_REMAINDER:
        if (ur == 0)
        {
            result.faults = IR_INTEGER_FAULT_DIVIDE_BY_ZERO;
        }
        else
        {
            result.bits.low = (u64)(operation == IR_BINARY_UNSIGNED_DIVIDE ? ul / ur : ul % ur);
        }
        break;
    case IR_BINARY_SHIFT_LEFT:
    case IR_BINARY_SIGNED_SHIFT_RIGHT:
    case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
        if (ur >= (s64)width)
        {
            result.faults = IR_INTEGER_FAULT_SHIFT_COUNT;
        }
        else if (operation == IR_BINARY_SHIFT_LEFT)
        {
            s64 scale = (s64)1 << ur;
            result.bits.low = (u64)(ul * scale) & mask;
            result.faults = ir_integer_test_range_faults(ul * scale, sl * scale, width);
            if (sl < 0)
            {
                result.faults |= IR_INTEGER_FAULT_NEGATIVE_SHIFTED;
            }
        }
        else if (operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT)
        {
            result.bits.low = (u64)ul >> ur;
        }
        else
        {
            result.bits.low = (u64)ir_integer_test_floor_shift(sl, (u32)ur) & mask;
        }
        break;
    case IR_BINARY_INTEGER_BITWISE_AND: result.bits.low = (u64)(ul & ur); break;
    case IR_BINARY_INTEGER_BITWISE_OR: result.bits.low = (u64)(ul | ur); break;
    case IR_BINARY_INTEGER_BITWISE_XOR: result.bits.low = (u64)(ul ^ ur); break;
    case IR_BINARY_BOOLEAN_AND: result.bits.low = ul != 0 && ur != 0; break;
    case IR_BINARY_BOOLEAN_OR: result.bits.low = ul != 0 || ur != 0; break;
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL: result.bits.low = ul == ur; break;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL: result.bits.low = ul != ur; break;
    case IR_BINARY_UNSIGNED_LESS: result.bits.low = ul < ur; break;
    case IR_BINARY_UNSIGNED_LESS_EQUAL: result.bits.low = ul <= ur; break;
    case IR_BINARY_UNSIGNED_GREATER: result.bits.low = ul > ur; break;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL: result.bits.low = ul >= ur; break;
    case IR_BINARY_SIGNED_LESS: result.bits.low = sl < sr; break;
    case IR_BINARY_SIGNED_LESS_EQUAL: result.bits.low = sl <= sr; break;
    case IR_BINARY_SIGNED_GREATER: result.bits.low = sl > sr; break;
    case IR_BINARY_SIGNED_GREATER_EQUAL: result.bits.low = sl >= sr; break;
    default: result.faults = IR_INTEGER_FAULT_UNSUPPORTED; break;
    }
    if (arithmetic)
    {
        result.bits.low = (u64)exact_unsigned & mask;
        result.faults = ir_integer_test_range_faults(exact_unsigned, exact_signed, width);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrIntegerResult ir_integer_test_oracle_unary(IrUnaryOperation operation, u64 operand, u32 width)
{
    u64 mask = ir_integer_test_mask(width);
    s64 value = (s64)(operand & mask);
    s64 signed_value = ir_integer_test_signed(operand, width);
    IrIntegerResult result = {0};
    u32 count = 0;
    switch (operation)
    {
    case IR_UNARY_INTEGER_NEGATE:
        result.bits.low = (u64)(-value) & mask;
        result.faults = ir_integer_test_range_faults(-value, -signed_value, width);
        break;
    case IR_UNARY_INTEGER_BITWISE_NOT: result.bits.low = ~(u64)value & mask; break;
    case IR_UNARY_BOOLEAN_NOT: result.bits.low = value == 0; break;
    case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS:
        for (u32 bit = width; bit > 0 && !(((u64)value >> (bit - 1)) & 1); bit -= 1)
        {
            count += 1;
        }
        result.bits.low = count;
        result.faults = value == 0 ? IR_INTEGER_FAULT_ZERO_COUNT : 0;
        break;
    case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS:
        for (u32 bit = 0; bit < width && !(((u64)value >> bit) & 1); bit += 1)
        {
            count += 1;
        }
        result.bits.low = count;
        result.faults = value == 0 ? IR_INTEGER_FAULT_ZERO_COUNT : 0;
        break;
    case IR_UNARY_INTEGER_POPULATION_COUNT:
        for (u32 bit = 0; bit < width; bit += 1)
        {
            count += ((u64)value >> bit) & 1;
        }
        result.bits.low = count;
        break;
    default: result.faults = IR_INTEGER_FAULT_UNSUPPORTED; break;
    }
    result.bits.low &= mask;
    return result;
}

BUSTER_GLOBAL_LOCAL IrIntegerResult ir_integer_test_oracle_convert(IrConversionOperation operation, u64 operand, u32 source_width, u32 target_width)
{
    s64 unsigned_value = (s64)(operand & ir_integer_test_mask(source_width));
    s64 signed_value = ir_integer_test_signed(operand, source_width);
    IrIntegerResult result = {0};
    if (operation == IR_CONVERSION_INTEGER_SIGN_EXTEND)
    {
        result.bits.low = (u64)signed_value & ir_integer_test_mask(target_width);
    }
    else
    {
        result.bits.low = (u64)unsigned_value & ir_integer_test_mask(target_width);
    }
    if ((s64)result.bits.low != unsigned_value)
    {
        result.faults |= IR_INTEGER_FAULT_UNSIGNED_WRAP;
    }
    if (ir_integer_test_signed(result.bits.low, target_width) != signed_value)
    {
        result.faults |= IR_INTEGER_FAULT_SIGNED_OVERFLOW;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_integer_test_result_equal(IrIntegerResult computed, IrIntegerResult expected)
{
    // A fault that voids the value leaves the bits unspecified.
    bool bits_meaningful = !(expected.faults & (IR_INTEGER_FAULT_DIVIDE_BY_ZERO | IR_INTEGER_FAULT_SHIFT_COUNT | IR_INTEGER_FAULT_UNSUPPORTED));
    return computed.faults == expected.faults && (!bits_meaningful || ir_integer_equal(computed.bits, expected.bits));
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_integer_test_reduced_widths(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrBinaryOperation binaries[] = {
        IR_BINARY_INTEGER_ADD, IR_BINARY_INTEGER_SUBTRACT, IR_BINARY_INTEGER_MULTIPLY, IR_BINARY_SIGNED_DIVIDE, IR_BINARY_UNSIGNED_DIVIDE,
        IR_BINARY_SIGNED_REMAINDER, IR_BINARY_UNSIGNED_REMAINDER, IR_BINARY_SHIFT_LEFT, IR_BINARY_SIGNED_SHIFT_RIGHT,
        IR_BINARY_UNSIGNED_SHIFT_RIGHT, IR_BINARY_INTEGER_BITWISE_AND, IR_BINARY_INTEGER_BITWISE_OR, IR_BINARY_INTEGER_BITWISE_XOR,
        IR_BINARY_BOOLEAN_AND, IR_BINARY_BOOLEAN_OR, IR_BINARY_INTEGER_EQUAL, IR_BINARY_INTEGER_NOT_EQUAL, IR_BINARY_BOOLEAN_EQUAL,
        IR_BINARY_BOOLEAN_NOT_EQUAL, IR_BINARY_SIGNED_LESS, IR_BINARY_SIGNED_LESS_EQUAL, IR_BINARY_SIGNED_GREATER,
        IR_BINARY_SIGNED_GREATER_EQUAL, IR_BINARY_UNSIGNED_LESS, IR_BINARY_UNSIGNED_LESS_EQUAL, IR_BINARY_UNSIGNED_GREATER,
        IR_BINARY_UNSIGNED_GREATER_EQUAL,
    };
    IrUnaryOperation unaries[] = {
        IR_UNARY_INTEGER_NEGATE, IR_UNARY_INTEGER_BITWISE_NOT, IR_UNARY_BOOLEAN_NOT, IR_UNARY_INTEGER_COUNT_LEADING_ZEROS,
        IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS, IR_UNARY_INTEGER_POPULATION_COUNT,
    };
    IrConversionOperation conversions[] = {
        IR_CONVERSION_INTEGER_SIGN_EXTEND, IR_CONVERSION_INTEGER_ZERO_EXTEND, IR_CONVERSION_INTEGER_TRUNCATE, IR_CONVERSION_INTEGER_REINTERPRET,
    };
    u64 checked = 0;
    u64 mismatches = 0;
    for (u32 width = 1; width <= 8; width += 1)
    {
        u64 limit = (u64)1 << width;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(binaries); index += 1)
        {
            bool shift = binaries[index] == IR_BINARY_SHIFT_LEFT || binaries[index] == IR_BINARY_SIGNED_SHIFT_RIGHT ||
                         binaries[index] == IR_BINARY_UNSIGNED_SHIFT_RIGHT;
            // A shift count is read at its own width; 8 bits reaches every
            // in-range and out-of-range count of these widths.
            u32 right_width = shift ? 8 : width;
            u64 right_limit = (u64)1 << right_width;
            for (u64 left = 0; left < limit; left += 1)
            {
                for (u64 right = 0; right < right_limit; right += 1)
                {
                    IrIntegerResult computed = ir_integer_binary(binaries[index], (IrInteger){.low = left}, (IrInteger){.low = right}, width, right_width);
                    IrIntegerResult expected = ir_integer_test_oracle_binary(binaries[index], left, right, width, right_width);
                    bool equal = ir_integer_test_result_equal(computed, expected);
                    checked += 1;
                    if (!equal && mismatches < 8)
                    {
                        BUSTER_TEST_ERROR(S8("binary operation {u32} width {u32}: {u64} op {u64} gave {u64}/{u32}, expected {u64}/{u32}\n"), (u32)binaries[index],
                                          width, left, right, computed.bits.low, (u32)computed.faults, expected.bits.low, (u32)expected.faults);
                    }
                    mismatches += !equal;
                }
            }
        }
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(unaries); index += 1)
        {
            for (u64 value = 0; value < limit; value += 1)
            {
                IrIntegerResult computed = ir_integer_unary(unaries[index], (IrInteger){.low = value}, width);
                IrIntegerResult expected = ir_integer_test_oracle_unary(unaries[index], value, width);
                bool equal = ir_integer_test_result_equal(computed, expected);
                checked += 1;
                if (!equal && mismatches < 8)
                {
                    BUSTER_TEST_ERROR(S8("unary operation {u32} width {u32}: {u64} gave {u64}/{u32}, expected {u64}/{u32}\n"), (u32)unaries[index], width, value,
                                      computed.bits.low, (u32)computed.faults, expected.bits.low, (u32)expected.faults);
                }
                mismatches += !equal;
            }
        }
        for (u32 target = 1; target <= 8; target += 1)
        {
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(conversions); index += 1)
            {
                bool admitted = conversions[index] == IR_CONVERSION_INTEGER_TRUNCATE ? target <= width
                                : conversions[index] == IR_CONVERSION_INTEGER_REINTERPRET ? target == width
                                                                                          : target >= width;
                for (u64 value = 0; admitted && value < limit; value += 1)
                {
                    IrIntegerResult computed = ir_integer_convert(conversions[index], (IrInteger){.low = value}, width, target);
                    IrIntegerResult expected = ir_integer_test_oracle_convert(conversions[index], value, width, target);
                    bool equal = ir_integer_test_result_equal(computed, expected);
                    checked += 1;
                    if (!equal && mismatches < 8)
                    {
                        BUSTER_TEST_ERROR(S8("conversion {u32} {u32}->{u32}: {u64} gave {u64}/{u32}, expected {u64}/{u32}\n"), (u32)conversions[index], width,
                                          target, value, computed.bits.low, (u32)computed.faults, expected.bits.low, (u32)expected.faults);
                    }
                    mismatches += !equal;
                }
            }
        }
    }
    // 27 binary operations over every operand pair (shift counts at 8 bits),
    // 6 unary operations and the admitted conversions, widths 1..8.
    BUSTER_TEST(arguments, checked == 2497964);
    BUSTER_TEST(arguments, mismatches == 0);
    return result;
}

#if defined(__SIZEOF_INT128__) && !defined(__BUSTER__)
typedef unsigned __int128 IrIntegerTestWide;
typedef __int128 IrIntegerTestSignedWide;

BUSTER_GLOBAL_LOCAL IrIntegerTestWide ir_integer_test_wide(IrInteger value)
{
    return ((IrIntegerTestWide)value.high << 64) | value.low;
}

BUSTER_GLOBAL_LOCAL IrInteger ir_integer_test_narrow(IrIntegerTestWide value)
{
    return (IrInteger){.low = (u64)value, .high = (u64)(value >> 64)};
}

BUSTER_GLOBAL_LOCAL u64 ir_integer_test_random(u64* state)
{
    u64 x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

// Operands biased toward the boundaries every bug in this family lives at.
BUSTER_GLOBAL_LOCAL IrInteger ir_integer_test_operand(u64* state, u32 width)
{
    u64 shape = ir_integer_test_random(state) % 8;
    IrInteger value = {.low = ir_integer_test_random(state), .high = ir_integer_test_random(state)};
    IrInteger all = {.low = UINT64_MAX, .high = UINT64_MAX};
    IrInteger minimum = ir_integer_mask(ir_integer_test_narrow((IrIntegerTestWide)1 << (width - 1)), width);
    switch (shape)
    {
    case 0: value = (IrInteger){0}; break;
    case 1: value = (IrInteger){.low = 1}; break;
    case 2: value = all; break;
    case 3: value = minimum; break;
    case 4: value = ir_integer_test_narrow(ir_integer_test_wide(minimum) - 1); break;
    case 5: value = (IrInteger){.low = ir_integer_test_random(state) % 256}; break;
    default: break;
    }
    return ir_integer_mask(value, width);
}

BUSTER_GLOBAL_LOCAL IrIntegerTestSignedWide ir_integer_test_signed_wide(IrInteger value, u32 width)
{
    IrIntegerTestWide bits = ir_integer_test_wide(ir_integer_mask(value, width));
    if (width < 128 && ((bits >> (width - 1)) & 1))
    {
        bits |= ~(IrIntegerTestWide)0 << width;
    }
    return (IrIntegerTestSignedWide)bits;
}

// 16/32/64 bits: every exact sum, difference and product fits 128 bits.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_integer_test_random_widths(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 state = UINT64_C(0x9e3779b97f4a7c15);
    u32 widths[] = {16, 32, 64};
    u64 mismatches = 0;
    for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(widths); width_index += 1)
    {
        u32 width = widths[width_index];
        IrIntegerTestSignedWide minimum = -((IrIntegerTestSignedWide)1 << (width - 1));
        IrIntegerTestSignedWide maximum = ((IrIntegerTestSignedWide)1 << (width - 1)) - 1;
        IrIntegerTestWide unsigned_maximum = ((IrIntegerTestWide)1 << width) - 1;
        for (u32 iteration = 0; iteration < 20000; iteration += 1)
        {
            IrInteger left = ir_integer_test_operand(&state, width);
            IrInteger right = ir_integer_test_operand(&state, width);
            IrIntegerTestWide ul = ir_integer_test_wide(left);
            IrIntegerTestWide ur = ir_integer_test_wide(right);
            IrIntegerTestSignedWide sl = ir_integer_test_signed_wide(left, width);
            IrIntegerTestSignedWide sr = ir_integer_test_signed_wide(right, width);
            IrBinaryOperation operations[] = {IR_BINARY_INTEGER_ADD, IR_BINARY_INTEGER_SUBTRACT, IR_BINARY_INTEGER_MULTIPLY};
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(operations); index += 1)
            {
                IrIntegerTestWide exact_unsigned = index == 0 ? ul + ur : index == 1 ? ul - ur : ul * ur;
                IrIntegerTestSignedWide exact_signed = index == 0 ? sl + sr : index == 1 ? sl - sr : sl * sr;
                bool unsigned_fault = index == 1 ? ul < ur : exact_unsigned > unsigned_maximum;
                bool signed_fault = exact_signed < minimum || exact_signed > maximum;
                IrIntegerResult computed = ir_integer_binary(operations[index], left, right, width, width);
                u8 expected_faults = (u8)((unsigned_fault ? IR_INTEGER_FAULT_UNSIGNED_WRAP : 0) | (signed_fault ? IR_INTEGER_FAULT_SIGNED_OVERFLOW : 0));
                mismatches += computed.faults != expected_faults ||
                              ir_integer_test_wide(computed.bits) != (exact_unsigned & unsigned_maximum);
            }
            if (sr != 0)
            {
                IrIntegerResult quotient = ir_integer_binary(IR_BINARY_SIGNED_DIVIDE, left, right, width, width);
                IrIntegerResult remainder = ir_integer_binary(IR_BINARY_SIGNED_REMAINDER, left, right, width, width);
                bool overflow = sl == minimum && sr == -1;
                IrIntegerTestSignedWide q = overflow ? minimum : sl / sr;
                IrIntegerTestSignedWide r = overflow ? 0 : sl % sr;
                mismatches += ir_integer_test_signed_wide(quotient.bits, width) != q || ir_integer_test_signed_wide(remainder.bits, width) != r ||
                              ((quotient.faults & IR_INTEGER_FAULT_SIGNED_OVERFLOW) != 0) != overflow;
            }
            u32 count = (u32)(ur % (IrIntegerTestWide)(width + 8));
            IrIntegerResult shifted = ir_integer_binary(IR_BINARY_SHIFT_LEFT, left, (IrInteger){.low = count}, width, 32);
            bool count_fault = count >= width;
            mismatches += ((shifted.faults & IR_INTEGER_FAULT_SHIFT_COUNT) != 0) != count_fault;
            if (!count_fault)
            {
                IrIntegerTestWide exact = ul << count;
                IrIntegerTestSignedWide exact_signed = sl * ((IrIntegerTestSignedWide)1 << count);
                mismatches += ir_integer_test_wide(shifted.bits) != (exact & unsigned_maximum) ||
                              ((shifted.faults & IR_INTEGER_FAULT_UNSIGNED_WRAP) != 0) != (exact > unsigned_maximum) ||
                              ((shifted.faults & IR_INTEGER_FAULT_SIGNED_OVERFLOW) != 0) != (exact_signed < minimum || exact_signed > maximum);
                IrIntegerResult arithmetic = ir_integer_binary(IR_BINARY_SIGNED_SHIFT_RIGHT, left, (IrInteger){.low = count}, width, 32);
                IrIntegerTestSignedWide floored = sl >= 0 ? sl >> count : -((-sl - 1) >> count) - 1;
                mismatches += ir_integer_test_signed_wide(arithmetic.bits, width) != floored;
            }
            IrIntegerResult less = ir_integer_binary(IR_BINARY_SIGNED_LESS, left, right, width, width);
            IrIntegerResult below = ir_integer_binary(IR_BINARY_UNSIGNED_LESS, left, right, width, width);
            mismatches += less.bits.low != (u64)(sl < sr) || below.bits.low != (u64)(ul < ur);
        }
    }
    BUSTER_TEST(arguments, mismatches == 0);
    return result;
}

// 128 bits: no wider host type, so values are checked against the host's
// wrapped arithmetic and overflow against properties that define it.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_integer_test_random_wide(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 state = UINT64_C(0xd1b54a32d192ed03);
    u64 mismatches = 0;
    IrIntegerTestSignedWide minimum = (IrIntegerTestSignedWide)((IrIntegerTestWide)1 << 127);
    for (u32 iteration = 0; iteration < 40000; iteration += 1)
    {
        IrInteger left = ir_integer_test_operand(&state, 128);
        IrInteger right = ir_integer_test_operand(&state, 128);
        IrIntegerTestWide ul = ir_integer_test_wide(left);
        IrIntegerTestWide ur = ir_integer_test_wide(right);
        IrIntegerTestSignedWide sl = (IrIntegerTestSignedWide)ul;
        IrIntegerTestSignedWide sr = (IrIntegerTestSignedWide)ur;
        IrIntegerResult sum = ir_integer_binary(IR_BINARY_INTEGER_ADD, left, right, 128, 128);
        IrIntegerResult product = ir_integer_binary(IR_BINARY_INTEGER_MULTIPLY, left, right, 128, 128);
        bool sum_wrap = ul + ur < ul;
        bool sum_overflow = (sl < 0) == (sr < 0) && ((IrIntegerTestSignedWide)(ul + ur) < 0) != (sl < 0);
        bool product_wrap = ul != 0 && (ul * ur) / ul != ur;
        IrIntegerTestWide magnitude_left = sl < 0 ? (IrIntegerTestWide)0 - ul : ul;
        IrIntegerTestWide magnitude_right = sr < 0 ? (IrIntegerTestWide)0 - ur : ur;
        IrIntegerTestWide magnitude = magnitude_left * magnitude_right;
        bool magnitude_wrap = magnitude_left != 0 && magnitude / magnitude_left != magnitude_right;
        bool negative = (sl < 0) != (sr < 0) && magnitude != 0;
        bool product_overflow = magnitude_wrap || (negative ? magnitude > ((IrIntegerTestWide)1 << 127) : magnitude >= ((IrIntegerTestWide)1 << 127));
        mismatches += ir_integer_test_wide(sum.bits) != ul + ur || ir_integer_test_wide(product.bits) != ul * ur;
        mismatches += ((sum.faults & IR_INTEGER_FAULT_UNSIGNED_WRAP) != 0) != sum_wrap || ((sum.faults & IR_INTEGER_FAULT_SIGNED_OVERFLOW) != 0) != sum_overflow;
        mismatches += ((product.faults & IR_INTEGER_FAULT_UNSIGNED_WRAP) != 0) != product_wrap ||
                      ((product.faults & IR_INTEGER_FAULT_SIGNED_OVERFLOW) != 0) != product_overflow;
        if (ur != 0)
        {
            // Truncating division is the unique q, r with a = q*b + r,
            // |r| < |b| and r zero or of a's sign.
            IrIntegerResult quotient = ir_integer_binary(IR_BINARY_UNSIGNED_DIVIDE, left, right, 128, 128);
            IrIntegerResult remainder = ir_integer_binary(IR_BINARY_UNSIGNED_REMAINDER, left, right, 128, 128);
            IrIntegerTestWide q = ir_integer_test_wide(quotient.bits);
            IrIntegerTestWide r = ir_integer_test_wide(remainder.bits);
            mismatches += q * ur + r != ul || r >= ur;
            IrIntegerResult signed_quotient = ir_integer_binary(IR_BINARY_SIGNED_DIVIDE, left, right, 128, 128);
            IrIntegerResult signed_remainder = ir_integer_binary(IR_BINARY_SIGNED_REMAINDER, left, right, 128, 128);
            IrIntegerTestWide sq = ir_integer_test_wide(signed_quotient.bits);
            IrIntegerTestSignedWide sremainder = (IrIntegerTestSignedWide)ir_integer_test_wide(signed_remainder.bits);
            IrIntegerTestWide magnitude_remainder = sremainder < 0 ? (IrIntegerTestWide)0 - (IrIntegerTestWide)sremainder : (IrIntegerTestWide)sremainder;
            bool overflow = sl == minimum && sr == -1;
            mismatches += sq * ur + (IrIntegerTestWide)sremainder != ul || magnitude_remainder >= magnitude_right ||
                          (sremainder != 0 && (sremainder < 0) != (sl < 0)) ||
                          ((signed_quotient.faults & IR_INTEGER_FAULT_SIGNED_OVERFLOW) != 0) != overflow;
        }
        u32 count = (u32)(ur % 136);
        IrIntegerResult shifted = ir_integer_binary(IR_BINARY_SHIFT_LEFT, left, (IrInteger){.low = count}, 128, 32);
        IrIntegerResult logical = ir_integer_binary(IR_BINARY_UNSIGNED_SHIFT_RIGHT, left, (IrInteger){.low = count}, 128, 32);
        IrIntegerResult arithmetic = ir_integer_binary(IR_BINARY_SIGNED_SHIFT_RIGHT, left, (IrInteger){.low = count}, 128, 32);
        if (count >= 128)
        {
            mismatches += !(shifted.faults & IR_INTEGER_FAULT_SHIFT_COUNT) || !(logical.faults & IR_INTEGER_FAULT_SHIFT_COUNT) ||
                          !(arithmetic.faults & IR_INTEGER_FAULT_SHIFT_COUNT);
        }
        else
        {
            IrIntegerTestSignedWide floored = sl >= 0 ? (IrIntegerTestSignedWide)(ul >> count)
                                                      : (IrIntegerTestSignedWide)~(~ul >> count);
            mismatches += ir_integer_test_wide(shifted.bits) != ul << count || ir_integer_test_wide(logical.bits) != ul >> count ||
                          (IrIntegerTestSignedWide)ir_integer_test_wide(arithmetic.bits) != floored;
            mismatches += ((shifted.faults & IR_INTEGER_FAULT_UNSIGNED_WRAP) != 0) != (count && (ul >> (128 - count)) != 0);
        }
        IrIntegerResult less = ir_integer_binary(IR_BINARY_SIGNED_LESS, left, right, 128, 128);
        IrIntegerResult below = ir_integer_binary(IR_BINARY_UNSIGNED_LESS, left, right, 128, 128);
        mismatches += less.bits.low != (u64)(sl < sr) || below.bits.low != (u64)(ul < ur);
        IrIntegerResult narrowed = ir_integer_convert(IR_CONVERSION_INTEGER_TRUNCATE, left, 128, 64);
        IrIntegerResult widened = ir_integer_convert(IR_CONVERSION_INTEGER_SIGN_EXTEND, narrowed.bits, 64, 128);
        mismatches += narrowed.bits.low != (u64)ul || narrowed.bits.high != 0 ||
                      ir_integer_test_wide(widened.bits) != (IrIntegerTestWide)(IrIntegerTestSignedWide)(s64)(u64)ul;
    }
    BUSTER_TEST(arguments, mismatches == 0);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult ir_integer_test_boundaries(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrInteger all = {.low = UINT64_MAX, .high = UINT64_MAX};
    struct
    {
        IrBinaryOperation operation;
        u32 width;
        u32 count_width;
        IrInteger left;
        IrInteger right;
        IrInteger bits;
        u8 faults;
    } cases[] = {
        // INT_MIN / -1 and INT_MIN % -1 at every width a C type has.
        {IR_BINARY_SIGNED_DIVIDE, 8, 8, {.low = 0x80}, {.low = 0xff}, {.low = 0x80}, IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        {IR_BINARY_SIGNED_DIVIDE, 32, 32, {.low = 0x80000000}, {.low = 0xffffffff}, {.low = 0x80000000}, IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        {IR_BINARY_SIGNED_REMAINDER, 32, 32, {.low = 0x80000000}, {.low = 0xffffffff}, {0}, IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        {IR_BINARY_SIGNED_DIVIDE, 64, 64, {.low = UINT64_C(1) << 63}, {.low = UINT64_MAX}, {.low = UINT64_C(1) << 63}, IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        {IR_BINARY_SIGNED_DIVIDE, 128, 128, {.high = UINT64_C(1) << 63}, {.low = UINT64_MAX, .high = UINT64_MAX}, {.high = UINT64_C(1) << 63},
         IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        // -7 / 2 truncates toward zero; the remainder takes the dividend's sign.
        {IR_BINARY_SIGNED_DIVIDE, 32, 32, {.low = 0xfffffff9}, {.low = 2}, {.low = 0xfffffffd}, 0},
        {IR_BINARY_SIGNED_REMAINDER, 32, 32, {.low = 0xfffffff9}, {.low = 2}, {.low = 0xffffffff}, 0},
        {IR_BINARY_UNSIGNED_DIVIDE, 32, 32, {.low = 5}, {0}, {0}, IR_INTEGER_FAULT_DIVIDE_BY_ZERO},
        // `1 << 31` wraps into the sign; `1 << 32` has no value at 32 bits.
        {IR_BINARY_SHIFT_LEFT, 32, 32, {.low = 1}, {.low = 31}, {.low = 0x80000000}, IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        {IR_BINARY_SHIFT_LEFT, 32, 32, {.low = 1}, {.low = 32}, {0}, IR_INTEGER_FAULT_SHIFT_COUNT},
        // A negative count read at its own 32-bit width is huge.
        {IR_BINARY_SHIFT_LEFT, 32, 32, {.low = 1}, {.low = 0xffffffff}, {0}, IR_INTEGER_FAULT_SHIFT_COUNT},
        // A 64-bit count whose low 32 bits are 1 is not a count of 1.
        {IR_BINARY_SHIFT_LEFT, 32, 64, {.low = 1}, {.low = UINT64_C(0x100000001)}, {0}, IR_INTEGER_FAULT_SHIFT_COUNT},
        {IR_BINARY_SHIFT_LEFT, 32, 32, {.low = 0xffffffff}, {.low = 1}, {.low = 0xfffffffe},
         IR_INTEGER_FAULT_UNSIGNED_WRAP | IR_INTEGER_FAULT_NEGATIVE_SHIFTED},
        {IR_BINARY_SIGNED_SHIFT_RIGHT, 32, 32, {.low = 0x80000000}, {.low = 31}, {.low = 0xffffffff}, 0},
        {IR_BINARY_UNSIGNED_SHIFT_RIGHT, 32, 32, {.low = 0x80000000}, {.low = 31}, {.low = 1}, 0},
        {IR_BINARY_SHIFT_LEFT, 128, 32, {.low = 1}, {.low = 127}, {.high = UINT64_C(1) << 63}, IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        {IR_BINARY_SHIFT_LEFT, 128, 32, {.low = 1}, {.low = 64}, {.high = 1}, 0},
        {IR_BINARY_SIGNED_SHIFT_RIGHT, 128, 32, {.high = UINT64_C(1) << 63}, {.low = 127}, all, 0},
        {IR_BINARY_SHIFT_LEFT, 128, 32, {.low = 1}, {.low = 128}, {0}, IR_INTEGER_FAULT_SHIFT_COUNT},
        // INT_MAX + 1 and 0u - 1.
        {IR_BINARY_INTEGER_ADD, 32, 32, {.low = 0x7fffffff}, {.low = 1}, {.low = 0x80000000}, IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        {IR_BINARY_INTEGER_SUBTRACT, 32, 32, {0}, {.low = 1}, {.low = 0xffffffff}, IR_INTEGER_FAULT_UNSIGNED_WRAP},
        {IR_BINARY_INTEGER_SUBTRACT, 32, 32, {.low = 5}, {0}, {.low = 5}, 0},
        {IR_BINARY_INTEGER_SUBTRACT, 32, 32, {.low = 0x80000000}, {0}, {.low = 0x80000000}, 0},
        {IR_BINARY_INTEGER_ADD, 128, 128, {.low = UINT64_MAX}, {.low = 1}, {.high = 1}, 0},
        {IR_BINARY_INTEGER_MULTIPLY, 128, 128, all, all, {.low = 1}, IR_INTEGER_FAULT_UNSIGNED_WRAP},
        {IR_BINARY_INTEGER_MULTIPLY, 64, 64, {.low = UINT64_C(1) << 32}, {.low = UINT64_C(1) << 32}, {0},
         IR_INTEGER_FAULT_UNSIGNED_WRAP | IR_INTEGER_FAULT_SIGNED_OVERFLOW},
        // A 128-bit value whose low limb is zero is not zero.
        {IR_BINARY_INTEGER_NOT_EQUAL, 128, 128, {.high = 1}, {0}, {.low = 1}, 0},
        {IR_BINARY_SIGNED_LESS, 128, 128, {.high = UINT64_C(1) << 63}, {0}, {.low = 1}, 0},
        {IR_BINARY_UNSIGNED_LESS, 128, 128, {.high = UINT64_C(1) << 63}, {0}, {0}, 0},
        {IR_BINARY_FLOAT_ADD, 32, 32, {0}, {0}, {0}, IR_INTEGER_FAULT_UNSUPPORTED},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        IrIntegerResult computed = ir_integer_binary(cases[index].operation, cases[index].left, cases[index].right, cases[index].width, cases[index].count_width);
        bool equal = ir_integer_test_result_equal(computed, (IrIntegerResult){.bits = cases[index].bits, .faults = cases[index].faults});
        if (!equal)
        {
            BUSTER_TEST_ERROR(S8("boundary case {u32}: gave {u64}:{u64}/{u32}\n"), index, computed.bits.high, computed.bits.low, (u32)computed.faults);
        }
        BUSTER_TEST(arguments, equal);
    }
    IrIntegerResult minimum_negated = ir_integer_unary(IR_UNARY_INTEGER_NEGATE, (IrInteger){.high = UINT64_C(1) << 63}, 128);
    BUSTER_TEST(arguments, minimum_negated.bits.high == UINT64_C(1) << 63 && minimum_negated.bits.low == 0 &&
                               minimum_negated.faults == (IR_INTEGER_FAULT_SIGNED_OVERFLOW | IR_INTEGER_FAULT_UNSIGNED_WRAP));
    IrIntegerResult zero_count = ir_integer_unary(IR_UNARY_INTEGER_COUNT_LEADING_ZEROS, (IrInteger){0}, 128);
    BUSTER_TEST(arguments, zero_count.bits.low == 128 && zero_count.faults == IR_INTEGER_FAULT_ZERO_COUNT);
    IrIntegerResult leading = ir_integer_unary(IR_UNARY_INTEGER_COUNT_LEADING_ZEROS, (IrInteger){.low = 1}, 128);
    IrIntegerResult trailing = ir_integer_unary(IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS, (IrInteger){.high = 1}, 128);
    IrIntegerResult population = ir_integer_unary(IR_UNARY_INTEGER_POPULATION_COUNT, all, 128);
    BUSTER_TEST(arguments, leading.bits.low == 127 && trailing.bits.low == 64 && population.bits.low == 128);
    // A signed widening sign-extends, a zero extension does not (#1347).
    IrIntegerResult widened = ir_integer_convert(IR_CONVERSION_INTEGER_SIGN_EXTEND, (IrInteger){.low = 0xff}, 8, 32);
    IrIntegerResult zeroed = ir_integer_convert(IR_CONVERSION_INTEGER_ZERO_EXTEND, (IrInteger){.low = 0xff}, 8, 32);
    BUSTER_TEST(arguments, widened.bits.low == 0xffffffff && widened.faults == IR_INTEGER_FAULT_UNSIGNED_WRAP);
    BUSTER_TEST(arguments, zeroed.bits.low == 0xff && zeroed.faults == IR_INTEGER_FAULT_SIGNED_OVERFLOW);
    IrIntegerResult wide = ir_integer_convert(IR_CONVERSION_INTEGER_SIGN_EXTEND, (IrInteger){.low = UINT64_C(1) << 63}, 64, 128);
    BUSTER_TEST(arguments, wide.bits.high == UINT64_MAX && wide.bits.low == UINT64_C(1) << 63);
    IrIntegerResult narrowed = ir_integer_convert(IR_CONVERSION_INTEGER_TRUNCATE, (IrInteger){.low = 0x1ff}, 32, 8);
    BUSTER_TEST(arguments, narrowed.bits.low == 0xff && narrowed.faults == (IR_INTEGER_FAULT_UNSIGNED_WRAP | IR_INTEGER_FAULT_SIGNED_OVERFLOW));
    s64 signed_value = 0;
    BUSTER_TEST(arguments, ir_integer_to_s64((IrInteger){.low = 0xff}, 8, &signed_value) && signed_value == -1);
    BUSTER_TEST(arguments, !ir_integer_to_s64((IrInteger){.high = 1}, 128, &signed_value));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_integer_test_constant_decode(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        u64 magnitude;
        bool negative;
        u32 width;
        IrInteger value;
        bool canonical;
    } cases[] = {
        {1, true, 8, {.low = 0xff}, true},
        {0xff, false, 8, {.low = 0xff}, true},
        {0x1ff, false, 8, {.low = 0xff}, false},
        {0x80, true, 8, {.low = 0x80}, true},
        {0x81, true, 8, {.low = 0x7f}, false},
        {0, true, 32, {0}, true},
        {2, false, 1, {0}, false},
        {1, false, 1, {.low = 1}, true},
        {1, true, 1, {.low = 1}, true},
        {1, true, 64, {.low = UINT64_MAX}, true},
        {UINT64_MAX, false, 64, {.low = UINT64_MAX}, true},
        // The negative flag sign-extends through a 128-bit value, and a
        // negative zero stays zero.
        {1, true, 128, {.low = UINT64_MAX, .high = UINT64_MAX}, true},
        {0, true, 128, {0}, true},
        {UINT64_C(1) << 63, true, 128, {.low = UINT64_C(1) << 63, .high = UINT64_MAX}, true},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        u64 immediate = cases[index].magnitude;
        IrInstruction row = {
            .opcode = IR_OPCODE_CONSTANT_INTEGER,
            .immediates = &immediate,
            .immediate_count = 1,
            .immediate_is_negative = cases[index].negative,
        };
        IrInteger decoded = {0};
        BUSTER_TEST(arguments, ir_integer_constant_decode(&row, cases[index].width, &decoded) && ir_integer_equal(decoded, cases[index].value));
        BUSTER_TEST(arguments, ir_integer_constant_canonical(&row, cases[index].width) == cases[index].canonical);
    }
    IrInstruction not_constant = {.opcode = IR_OPCODE_CONSTANT_FLOAT};
    IrInteger ignored = {0};
    BUSTER_TEST(arguments, !ir_integer_constant_decode(&not_constant, 32, &ignored));
    return result;
}

// The validator and the constructor state one constant contract: a row whose
// spelled number lies outside [-2^(width-1), 2^width) is invalid, because a
// native emitter materializes the magnitude unreduced.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_integer_test_constant_validation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    CIRLowerResult lowered = ir_promotion_lower(arguments->arena, S8("int seven(void) { return 7; }"), target_native);
    BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
    if (lowered.program && !lowered.diagnostic_count)
    {
        IrProgram* program = lowered.program;
        IrInstruction* narrow = 0;
        for (u32 function_index = 0; function_index < program->modules->function_count; function_index += 1)
        {
            IrFunction* function = program->modules->functions + function_index;
            for (u32 row = 0; row < function->instruction_count; row += 1)
            {
                IrInstruction* instruction = function->instructions + row;
                if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER &&
                    ir_integer_type_width(ir_type_from_id(&program->types, instruction->canonical_type)) == 32)
                {
                    narrow = instruction;
                }
            }
        }
        BUSTER_TEST(arguments, narrow && ir_validate_canonical_module(program, program->modules).error == IR_VALIDATION_NONE);
        if (narrow)
        {
            u64 original = narrow->immediates[0];
            bool original_negative = narrow->immediate_is_negative;
            narrow->immediates[0] = UINT64_C(0x100000007);
            narrow->immediate_is_negative = false;
            BUSTER_TEST(arguments, ir_validate_canonical_module(program, program->modules).error == IR_VALIDATION_OPERATION);
            narrow->immediates[0] = 5;
            narrow->immediate_is_negative = true;
            BUSTER_TEST(arguments, ir_validate_canonical_module(program, program->modules).error == IR_VALIDATION_NONE);
            narrow->immediates[0] = original;
            narrow->immediate_is_negative = original_negative;
        }
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_integer_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_integer_test_reduced_widths(arguments);
    UnitTestResult boundaries = ir_integer_test_boundaries(arguments);
    result.test_count += boundaries.test_count;
    result.succeeded_test_count += boundaries.succeeded_test_count;
    UnitTestResult decode = ir_integer_test_constant_decode(arguments);
    result.test_count += decode.test_count;
    result.succeeded_test_count += decode.succeeded_test_count;
    UnitTestResult validation = ir_integer_test_constant_validation(arguments);
    result.test_count += validation.test_count;
    result.succeeded_test_count += validation.succeeded_test_count;
#if defined(__SIZEOF_INT128__) && !defined(__BUSTER__)
    UnitTestResult randomized = ir_integer_test_random_widths(arguments);
    result.test_count += randomized.test_count;
    result.succeeded_test_count += randomized.succeeded_test_count;
    UnitTestResult wide = ir_integer_test_random_wide(arguments);
    result.test_count += wide.test_count;
    result.succeeded_test_count += wide.succeeded_test_count;
#endif
    return result;
}
