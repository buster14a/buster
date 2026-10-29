// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Shared target scalar representations for static scalar and vector initialization. No host padding is read.
#ifndef BUSTER_C_CHECK_CONSTANT_BYTES_INCLUDED
#define BUSTER_C_CHECK_CONSTANT_BYTES_INCLUDED
#include <float.h>
#include <limits.h>
#include <math.h>
static void check_data_integer_bytes(uint8_t* bytes, unsigned __int128 value, uint32_t size)
{
    for (uint32_t i = 0; i < size; i += 1)
    {
        bytes[i] = (uint8_t)(value >> (i * 8));
    }
}

// The checker uses Linux/x86-64 binary16, bfloat16, binary32, binary64 and x87 extended values. The half-precision ones
// are check_action_half_bits', and the other two IEEE encodings are copied into integer values, then serialized
// explicitly little-endian. x87 is built from numeric fields, never host padding. CheckValue does not retain NaN payload
// provenance; all NaNs get a deterministic quiet payload and their sign.
static void check_data_float_bytes(uint8_t bytes[16], TypeId type, long double value)
{
    _Static_assert(CHAR_BIT == 8 && sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128, "checker target requires binary32");
    _Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024, "checker target requires binary64");
    _Static_assert(LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384 && LDBL_MIN_EXP == -16381, "checker target requires x87 extended");
    const bool negative = signbit(value) != 0;
    const CheckHalf half = check_action_half_format(type);
    if (half != CHECK_HALF_NONE)
    {
        // A half-precision value's 16 bits (check_action_half_bits: a NaN's the format's quiet one with its sign).
        check_data_integer_bytes(bytes, check_action_half_bits(half, value), 2);
    }
    else if (type == BUSTER_TYPE_FLOAT)
    {
        const float rounded = (float)value;
        uint32_t bits = 0;
        memcpy(&bits, &rounded, sizeof(bits));
        if (isnan(value))
        {
            bits = UINT32_C(0x7fc00000) | ((uint32_t)negative << 31);
        }
        check_data_integer_bytes(bytes, bits, 4);
    }
    else if (type == BUSTER_TYPE_DOUBLE)
    {
        const double rounded = (double)value;
        uint64_t bits = 0;
        memcpy(&bits, &rounded, sizeof(bits));
        if (isnan(value))
        {
            bits = UINT64_C(0x7ff8000000000000) | ((uint64_t)negative << 63);
        }
        check_data_integer_bytes(bytes, bits, 8);
    }
    else
    {
        uint64_t significand = 0;
        uint32_t exponent = 0;
        if (isnan(value) || isinf(value))
        {
            exponent = 0x7fff;
            significand = isnan(value) ? UINT64_C(0xc000000000000000) : UINT64_C(0x8000000000000000);
        }
        else if (value != 0)
        {
            const long double magnitude = fabsl(value);
            if (magnitude < 0x1p-16382L)
            {
                significand = (uint64_t)ldexpl(magnitude, 16445);
            }
            else
            {
                int binary_exponent = 0;
                const long double fraction = frexpl(magnitude, &binary_exponent);
                significand = (uint64_t)ldexpl(fraction, 64);
                exponent = (uint32_t)(binary_exponent + 16382);
            }
        }
        check_data_integer_bytes(bytes, significand, 8);
        check_data_integer_bytes(bytes + 8, exponent | ((uint32_t)negative << 15), 2);
    }
}

#endif
