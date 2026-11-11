// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Decimal values in conversion warnings. Clang 23's external oracle determines the display precision, its
// preliminary decimal truncation and its three-place padding limit. This fixed-storage implementation uses the
// host x87 representation already used by CheckValue; it neither changes host rounding state nor loads libm.

// A finite nonzero binary value has an exact decimal coefficient: remove its trailing binary zeros, then multiply
// its odd significand by 5^-exponent (or shift it left for a nonnegative exponent). The smallest binary128 subnormal is
// 2^-16494 (an x87's 2^-16445), so fewer than 113 + 3*16494 bits suffice for every coefficient, independently of source
// nesting or size. `significand` * 2^`exponent` is the value's magnitude, `significand` nonzero.
static void check_common_float_digits(char* output, uint64_t capacity, bool negative, unsigned __int128 significand, int32_t exponent, uint32_t precision)
{
    enum { LIMBS = (113 + 3 * 16494 + 63) / 64 + 1 };
    uint64_t limbs[LIMBS] = { 0 };
    const uint64_t low = (uint64_t)significand;
    const uint32_t trailing = low != 0 ? (uint32_t)__builtin_ctzll(low) : 64 + (uint32_t)__builtin_ctzll((uint64_t)(significand >> 64));
    significand >>= trailing;
    exponent += (int32_t)trailing;
    uint32_t count = (significand >> 64) != 0 ? 2 : 1;
    int32_t decimal = exponent < 0 ? exponent : 0;
    if (exponent >= 0)
    {
        const uint32_t whole = (uint32_t)exponent / 64;
        const uint32_t shift = (uint32_t)exponent % 64;
        const uint64_t parts[2] = { (uint64_t)significand, (uint64_t)(significand >> 64) };
        limbs[whole] = parts[0] << shift;
        limbs[whole + 1] = (parts[1] << shift) | (shift != 0 ? parts[0] >> (64 - shift) : 0);
        limbs[whole + 2] = shift != 0 ? parts[1] >> (64 - shift) : 0;
        count = whole + 3;
        while (count > 1 && limbs[count - 1] == 0)
        {
            count -= 1;
        }
    }
    else
    {
        limbs[0] = (uint64_t)significand;
        limbs[1] = (uint64_t)(significand >> 64);
        // 5^13 fits in 32 bits; one pass multiplies thirteen decimal places at a time.
        for (uint32_t left = (uint32_t)-exponent; left != 0;)
        {
            const uint32_t step = left > 13 ? 13 : left;
            uint32_t multiplier = 1;
            for (uint32_t i = 0; i < step; i += 1)
            {
                multiplier *= 5;
            }
            uint64_t carry = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const unsigned __int128 product = (unsigned __int128)limbs[i] * multiplier + carry;
                limbs[i] = (uint64_t)product;
                carry = (uint64_t)(product >> 64);
            }
            if (carry != 0)
            {
                limbs[count++] = carry;
            }
            left -= step;
        }
    }
    const uint32_t bits = (count - 1) * 64 + 64 - (uint32_t)__builtin_clzll(limbs[count - 1]);
    const uint32_t display_bits = (precision * 196 + 58) / 59;
    uint32_t discard = bits > display_bits ? (bits - display_bits) * 59 / 196 : 0;
    decimal += (int32_t)discard;
    // Trim decimal places by long division, retaining the exact leading coefficient. The final coefficient has
    // at most precision+2 digits (at most 37 for the exact binary128 values CheckValue can retain), hence 128 bits.
    while (discard != 0)
    {
        const uint32_t step = discard > 9 ? 9 : discard;
        uint32_t divisor = 1;
        for (uint32_t i = 0; i < step; i += 1)
        {
            divisor *= 10;
        }
        uint64_t remainder = 0;
        for (uint32_t i = count; i-- > 0;)
        {
            const unsigned __int128 dividend = (unsigned __int128)remainder << 64 | limbs[i];
            limbs[i] = (uint64_t)(dividend / divisor);
            remainder = (uint64_t)(dividend % divisor);
        }
        while (count > 1 && limbs[count - 1] == 0)
        {
            count -= 1;
        }
        discard -= step;
    }
    unsigned __int128 coefficient = limbs[0] | (count > 1 ? (unsigned __int128)limbs[1] << 64 : 0);
    char digits[48];
    check_common_number(digits, sizeof(digits), (__int128)coefficient);
    uint32_t length = (uint32_t)strlen(digits);
    if (length > precision)
    {
        unsigned __int128 divisor = 1;
        for (uint32_t i = precision; i < length; i += 1)
        {
            divisor *= 10;
        }
        coefficient = (coefficient + divisor / 2) / divisor;
        decimal += (int32_t)(length - precision);
    }
    while (coefficient % 10 == 0)
    {
        coefficient /= 10;
        decimal += 1;
    }
    check_common_number(digits, sizeof(digits), (__int128)coefficient);
    length = (uint32_t)strlen(digits);
    const int32_t leading = decimal + (int32_t)length - 1;
    const bool scientific = decimal >= 0 ? decimal > 3 || length + (uint32_t)decimal > precision : leading < -3;
    const char* sign = negative ? "-" : "";
    if (scientific)
    {
        snprintf(output, capacity, "%s%c.%sE%+d", sign, digits[0], length > 1 ? digits + 1 : "0", leading);
    }
    else if (decimal > 0)
    {
        snprintf(output, capacity, "%s%s%0*u", sign, digits, decimal, 0u);
    }
    else if (decimal == 0)
    {
        snprintf(output, capacity, "%s%s", sign, digits);
    }
    else
    {
        const int32_t whole = decimal + (int32_t)length;
        if (whole > 0)
        {
            snprintf(output, capacity, "%s%.*s.%s", sign, whole, digits, digits + whole);
        }
        else if (whole < 0)
        {
            snprintf(output, capacity, "%s0.%0*u%s", sign, -whole, 0u, digits);
        }
        else
        {
            snprintf(output, capacity, "%s0.%s", sign, digits);
        }
    }
}

// A long double's value as clang prints it at `precision` digits: NaN, an infinity, a signed zero, or its digits.
static void check_common_float_number(char* output, uint64_t capacity, long double value, uint32_t precision)
{
    uint64_t significand = 0;
    uint16_t sign_exponent = 0;
    memcpy(&significand, &value, sizeof(significand));
    memcpy(&sign_exponent, (const uint8_t*)&value + sizeof(significand), sizeof(sign_exponent));
    const bool negative = (sign_exponent & 0x8000u) != 0;
    const uint32_t field = sign_exponent & 0x7fffu;
    if (field == 0x7fff)
    {
        snprintf(output, capacity, "%s", __builtin_isnan(value) ? "NaN" : negative ? "-Inf" : "+Inf");
    }
    else if (significand == 0)
    {
        snprintf(output, capacity, "%s0", negative ? "-" : "");
    }
    else
    {
        check_common_float_digits(output, capacity, negative, significand, (int32_t)(field != 0 ? field : 1) - 16383 - 63, precision);
    }
}

// A __float128's value, its bits' (c_check_quad.c), as check_common_float_number prints a long double's.
static void check_common_quad_number(char* output, uint64_t capacity, unsigned __int128 bits, uint32_t precision)
{
    const bool negative = (bits >> 127) != 0;
    const uint32_t field = (uint32_t)(bits >> 112) & 0x7fffu;
    const unsigned __int128 fraction = bits & ((((unsigned __int128)1) << 112) - 1);
    if (field == 0x7fff)
    {
        snprintf(output, capacity, "%s", fraction != 0 ? "NaN" : negative ? "-Inf" : "+Inf");
    }
    else if (field == 0 && fraction == 0)
    {
        snprintf(output, capacity, "%s0", negative ? "-" : "");
    }
    else
    {
        const unsigned __int128 significand = fraction | (field != 0 ? ((unsigned __int128)1) << 112 : 0);
        check_common_float_digits(output, capacity, negative, significand, (int32_t)(field != 0 ? field : 1) - 16383 - 112, precision);
    }
}

// Whether a __float128's value (its bits) differs from the integer it converted to, signed or unsigned: a NaN always.
static __attribute__((noinline)) bool check_common_quad_changed(unsigned __int128 bits, __int128 signed_value, unsigned __int128 unsigned_value, bool is_signed)
{
    const bool nan = (bits & ~((unsigned __int128)1 << 127)) > ((unsigned __int128)0x7fff << 112);
    __float128 value = 0;
    memcpy(&value, &bits, sizeof(value));
    return nan || value != (is_signed ? (__float128)signed_value : (__float128)unsigned_value);
}
