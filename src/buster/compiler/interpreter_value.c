// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared scalar payload arithmetic for both interpreter fronts. Include after InterpreterValue and InterpreterStatus
// are declared. These operations consume values and widths, never an IR function or a lowering result.

static unsigned __int128 interpreter_value_integer(InterpreterValue value)
{
    return ((unsigned __int128)value.high << 64) | value.low;
}

static unsigned __int128 interpreter_value_mask(uint8_t type)
{
    const uint32_t bits = ir_type_bits[type];
    return bits == 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);
}

static InterpreterValue interpreter_value_from_integer(unsigned __int128 bits, uint8_t type)
{
    bits &= interpreter_value_mask(type);
    const InterpreterValue result = {
        .low = (uint64_t)bits,
        .high = (uint64_t)(bits >> 64),
    };
    return result;
}

static __int128 interpreter_value_signed(InterpreterValue value, uint8_t type)
{
    const uint32_t bits = ir_type_bits[type];
    unsigned __int128 raw = interpreter_value_integer(value) & interpreter_value_mask(type);
    if (bits < 128 && (raw & ((unsigned __int128)1 << (bits - 1))) != 0)
    {
        raw |= ~interpreter_value_mask(type);
    }
    __int128 result = 0;
    memcpy(&result, &raw, sizeof(result));
    return result;
}

static long double interpreter_value_float(InterpreterValue value, uint8_t type)
{
    long double result = 0;
    if (type == IR_F32)
    {
        const uint32_t raw = (uint32_t)value.low;
        float number = 0;
        memcpy(&number, &raw, sizeof(number));
        result = number;
    }
    else if (type == IR_F64)
    {
        double number = 0;
        memcpy(&number, &value.low, sizeof(number));
        result = number;
    }
    else
    {
        // x86-64 long double is the x87 80-bit format plus six padding bytes.
        uint8_t raw[sizeof(long double)] = { 0 };
        memcpy(raw, &value.low, 8);
        memcpy(raw + 8, &value.high, 2);
        memcpy(&result, raw, sizeof(result));
    }
    return result;
}

static InterpreterValue interpreter_value_float_value(long double number, uint8_t type)
{
    InterpreterValue result = { 0 };
    if (type == IR_F32)
    {
        const float narrowed = (float)number;
        uint32_t raw = 0;
        memcpy(&raw, &narrowed, sizeof(raw));
        result.low = raw;
    }
    else if (type == IR_F64)
    {
        const double narrowed = (double)number;
        memcpy(&result.low, &narrowed, sizeof(narrowed));
    }
    else
    {
        uint8_t raw[sizeof(long double)];
        memcpy(raw, &number, sizeof(raw));
        memcpy(&result.low, raw, 8);
        memcpy(&result.high, raw + 8, 2);
    }
    return result;
}

// Decode a finite x87 value after exact F32/F64 extension. Its explicit significand permits truncation and
// range checks entirely in unsigned integers, including i128 boundaries, without a host out-of-range cast.
static InterpreterStatus interpreter_value_float_integer(InterpreterValue value, uint8_t source_type, uint8_t result_type, bool is_signed, unsigned __int128* result)
{
    const InterpreterValue extended = interpreter_value_float_value(interpreter_value_float(value, source_type), IR_F80);
    const uint32_t encoded_exponent = (uint32_t)extended.high & 0x7fffu;
    const bool negative = (extended.high & 0x8000u) != 0;
    const int32_t exponent = (int32_t)encoded_exponent - 16383;
    const uint32_t bits = ir_type_bits[result_type];
    InterpreterStatus status = INTERPRETER_OK;
    unsigned __int128 magnitude = 0;
    if (encoded_exponent == 0x7fff || exponent >= 128)
    {
        status = INTERPRETER_ARITHMETIC;
    }
    else if (exponent >= 63)
    {
        magnitude = (unsigned __int128)extended.low << (uint32_t)(exponent - 63);
    }
    else if (exponent >= 0)
    {
        magnitude = extended.low >> (uint32_t)(63 - exponent);
    }
    if (status == INTERPRETER_OK)
    {
        const unsigned __int128 signed_limit = (unsigned __int128)1 << (bits - 1);
        if (is_signed ? (negative ? magnitude > signed_limit : magnitude >= signed_limit) : (negative && magnitude != 0) || (bits < 128 && magnitude > interpreter_value_mask(result_type)))
        {
            status = INTERPRETER_ARITHMETIC;
        }
        else
        {
            *result = negative ? 0 - magnitude : magnitude;
        }
    }
    return status;
}
