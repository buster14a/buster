// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The host's exact floating-point operations that the compiler's constant evaluation uses, without libm (#652):
// loading libm.so.6 took 24 microseconds and 12 page faults of every compile's start on the 9700X, for the six
// functions here alone, so the compiler's link takes libm only where something still needs it (CMakeLists.txt's
// --as-needed). Each gives libm's result for every input: the rounding mode's two, which read and write both the x87
// control word and SSE's MXCSR as glibc's fegetround and fesetround do, and ilogb, ldexp, ldexpl and frexpl, whose
// results are exact or rounded once. test-builtin-math-helpers checks them against libm's, which the tests reach with
// dlopen (test_libm.c).
#ifndef BUSTER_HOST_MATH_H
#define BUSTER_HOST_MATH_H
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BUSTER_HOST_MATH static inline __attribute__((unused))
// The rounding field of the x87 control word, where <fenv.h>'s FE_* modes are; MXCSR's is 3 bits higher.
#define BUSTER_HOST_ROUNDING (FE_TONEAREST | FE_DOWNWARD | FE_UPWARD | FE_TOWARDZERO)
#define BUSTER_HOST_MXCSR_SHIFT 3

// fegetround: the x87 control word's rounding mode, as glibc reads it.
BUSTER_HOST_MATH int host_fegetround(void)
{
    uint16_t control = 0;
    __asm__ volatile("fnstcw %0" : "=m"(control) : : "memory");
    return control & BUSTER_HOST_ROUNDING;
}

// fesetround: `mode` in both the x87 control word and MXCSR. Returns 0, or 1 for a value that is no mode, which
// changes neither.
BUSTER_HOST_MATH int host_fesetround(int mode)
{
    const bool valid = (mode & ~BUSTER_HOST_ROUNDING) == 0;
    if (valid)
    {
        uint16_t control = 0;
        uint32_t status = 0;
        __asm__ volatile("fnstcw %0" : "=m"(control) : : "memory");
        control = (uint16_t)((control & ~BUSTER_HOST_ROUNDING) | mode);
        __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
        __asm__ volatile("stmxcsr %0" : "=m"(status) : : "memory");
        status = (status & ~((uint32_t)BUSTER_HOST_ROUNDING << BUSTER_HOST_MXCSR_SHIFT)) | ((uint32_t)mode << BUSTER_HOST_MXCSR_SHIFT);
        __asm__ volatile("ldmxcsr %0" : : "m"(status) : "memory");
    }
    return valid ? 0 : 1;
}

// ilogb: the binary exponent of `value`, of its leading bit for a subnormal; FP_ILOGB0 for a zero, INT_MAX for an
// infinity and FP_ILOGBNAN for a NaN.
BUSTER_HOST_MATH int host_ilogb(double value)
{
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    const int field = (int)((bits >> 52) & 0x7ff);
    const uint64_t fraction = bits & ((UINT64_C(1) << 52) - 1);
    int result = field - 1023;
    if (field == 0)
    {
        // The leading bit's place: 2^-1074 for the fraction 1.
        result = fraction == 0 ? FP_ILOGB0 : -1011 - __builtin_clzll(fraction);
    }
    else if (field == 0x7ff)
    {
        result = fraction == 0 ? INT_MAX : FP_ILOGBNAN;
    }
    return result;
}

// ldexp: `value` * 2^`exponent`, rounded once, as the multiplication rounds in the current mode. The factors are
// powers of two in a double's range. Up, each of two steps of 2^1023 is exact until the result overflows. Down, each
// of two steps of 2^-969 (2^-1022 * 2^53) keeps a value of 2^-53 or more normal, and so exact, before the last step
// rounds; a smaller value's result is smaller than half the least subnormal, which every step then rounds alike.
// Past two steps either way every finite nonzero value overflows, or underflows, as its last step does.
BUSTER_HOST_MATH double host_ldexp(double value, int exponent)
{
    double result = value;
    int left = exponent;
    for (uint32_t step = 0; step < 2 && left > 1023; step += 1)
    {
        result *= 0x1p1023;
        left -= 1023;
    }
    for (uint32_t step = 0; step < 2 && left < -1022; step += 1)
    {
        result *= 0x1p-969;
        left += 969;
    }
    left = left > 1023 ? 1023 : left < -1022 ? -1022 : left;
    const uint64_t bits = (uint64_t)(left + 1023) << 52;
    double factor = 0;
    memcpy(&factor, &bits, sizeof(factor));
    return result * factor;
}

// ldexpl: ldexp's steps for the x87's 80-bit format, which has a 64-bit significand and exponents from -16382 to 16383:
// up by 2^16383, down by 2^-16318 (2^-16382 * 2^64).
BUSTER_HOST_MATH long double host_ldexpl(long double value, int exponent)
{
    long double result = value;
    int left = exponent;
    for (uint32_t step = 0; step < 2 && left > 16383; step += 1)
    {
        result *= 0x1p16383L;
        left -= 16383;
    }
    for (uint32_t step = 0; step < 2 && left < -16382; step += 1)
    {
        result *= 0x1p-16318L;
        left += 16318;
    }
    left = left > 16383 ? 16383 : left < -16382 ? -16382 : left;
    // The factor's bytes: the significand with its explicit integer bit, then the biased exponent.
    uint8_t bytes[sizeof(long double)] = { 0 };
    const uint64_t significand = UINT64_C(1) << 63;
    const uint16_t field = (uint16_t)(left + 16383);
    memcpy(bytes, &significand, sizeof(significand));
    memcpy(bytes + sizeof(significand), &field, sizeof(field));
    long double factor = 0;
    memcpy(&factor, bytes, sizeof(factor));
    return result * factor;
}

// frexpl: `value`'s fraction, of magnitude in [0.5, 1), and its exponent in *exponent, so that value = fraction *
// 2^*exponent. A zero, an infinity or a NaN is returned as value + value (a NaN quieted), with *exponent 0, as glibc
// returns them. A subnormal is first scaled by 2^64, which is exact.
BUSTER_HOST_MATH long double host_frexpl(long double value, int* exponent)
{
    uint8_t bytes[sizeof(long double)] = { 0 };
    uint16_t field = 0;
    uint64_t significand = 0;
    memcpy(bytes, &value, sizeof(value));
    memcpy(&significand, bytes, sizeof(significand));
    memcpy(&field, bytes + sizeof(significand), sizeof(field));
    long double result = value;
    int shift = 0;
    if ((field & 0x7fff) == 0 && significand != 0)
    {
        result = value * 0x1p64L;
        shift = 64;
        memcpy(bytes, &result, sizeof(result));
        memcpy(&field, bytes + sizeof(significand), sizeof(field));
    }
    *exponent = 0;
    if ((field & 0x7fff) == 0 || (field & 0x7fff) == 0x7fff)
    {
        result = value + value;
    }
    else
    {
        *exponent = (int)(field & 0x7fff) - 16382 - shift;
        field = (uint16_t)((field & 0x8000) | 16382);
        memcpy(bytes + sizeof(significand), &field, sizeof(field));
        memcpy(&result, bytes, sizeof(result));
    }
    return result;
}
#endif
