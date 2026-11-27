// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#include <stdio.h>
#include <string.h>
#include "tier1-float-select.h"
static unsigned long bits(double x) { unsigned long b; memcpy(&b, &x, sizeof(b)); return b; }
static unsigned long fbits(float x) { unsigned b; memcpy(&b, &x, sizeof(b)); return b; }
static double make(unsigned long b) { double x; memcpy(&x, &b, sizeof(x)); return x; }

// Each call crosses the unit boundary, so Clang's oracle cannot fold it into this driver. Compare
// result bits in all four MXCSR rounding modes and require exact calls and skipped arms to leave
// every status flag clear. The two rounding controls deliberately set only the inexact flag.
#define BUSTER_TEST_FLOAT_CONVERSION(call, expected) \
    do \
    { \
        __builtin_ia32_ldmxcsr(setting); \
        const float result = (call); \
        const unsigned status = __builtin_ia32_stmxcsr() & 0x3fu; \
        h = h * 31 + fbits(result); \
        h = h * 31 + status; \
        failures += status != (expected); \
    } while (0)

#define BUSTER_TEST_DOUBLE_CONVERSION(call, expected) \
    do \
    { \
        __builtin_ia32_ldmxcsr(setting); \
        const double result = (call); \
        const unsigned status = __builtin_ia32_stmxcsr() & 0x3fu; \
        h = h * 31 + bits(result); \
        h = h * 31 + status; \
        failures += status != (expected); \
    } while (0)

static unsigned check_conversions(unsigned long* hash)
{
    const int conditions[4] = { 0, 1, -1, -2147483647 - 1 };
    const int integers[5] = { -2147483647 - 1, -16777217, 0, 16777217, 2147483647 };
    const long wide[5] = { -9223372036854775807L - 1L, -9007199254740993L, 0L, 9007199254740993L, 9223372036854775807L };
    const unsigned saved = __builtin_ia32_stmxcsr();
    unsigned long h = *hash;
    unsigned failures = 0;
    for (unsigned mode = 0; mode < 4; mode += 1)
    {
        const unsigned setting = (saved & ~0x603fu) | (mode << 13);
        for (unsigned c = 0; c < 4; c += 1)
        {
            const int condition = conditions[c];
            BUSTER_TEST_DOUBLE_CONVERSION(fsel_convert_double_constants(condition), 0);
            BUSTER_TEST_FLOAT_CONVERSION(fsel_convert_float_constants(condition), 0);
            BUSTER_TEST_DOUBLE_CONVERSION(fsel_convert_mixed_signedness(condition), 0);
            failures += bits(fsel_convert_mixed_signedness(condition)) != bits(condition ? 4294967295.0 : 2.0);
            BUSTER_TEST_FLOAT_CONVERSION(fsel_round_float_constant(condition), condition != 0 ? 0x20u : 0);
            BUSTER_TEST_DOUBLE_CONVERSION(fsel_round_double_constant(condition), condition != 0 ? 0x20u : 0);
            BUSTER_TEST_FLOAT_CONVERSION(fsel_round_narrow_float(condition), condition != 0 ? 0x20u : 0);
            BUSTER_TEST_DOUBLE_CONVERSION(fsel_round_narrow_double(condition), condition != 0 ? 0x20u : 0);
            for (unsigned i = 0; i < 5; i += 1)
            {
                const int x = integers[i];
                const unsigned inexact = condition != 0 && (i == 1 || i == 3 || i == 4) ? 0x20u : 0;
                BUSTER_TEST_DOUBLE_CONVERSION(fsel_convert_double(condition, x), 0);
                BUSTER_TEST_DOUBLE_CONVERSION(fsel_convert_double_reverse(condition, x), 0);
                BUSTER_TEST_DOUBLE_CONVERSION(fsel_convert_unsigned(condition, (unsigned)x), 0);
                BUSTER_TEST_DOUBLE_CONVERSION(fsel_convert_double_logical(condition, x), 0);
                BUSTER_TEST_FLOAT_CONVERSION(fsel_convert_float_logical(condition, x), 0);
                BUSTER_TEST_FLOAT_CONVERSION(fsel_round_float(condition, x), inexact);
                BUSTER_TEST_DOUBLE_CONVERSION(fsel_round_double(condition, wide[i]), inexact);
                BUSTER_TEST_FLOAT_CONVERSION(fsel_guarded_float_convert(condition, x), inexact);
                BUSTER_TEST_DOUBLE_CONVERSION(fsel_guarded_double_convert(condition, wide[i]), inexact);
            }
            for (int initial = -1; initial <= 1; initial += 1)
            {
                volatile int touched = initial;
                BUSTER_TEST_DOUBLE_CONVERSION(fsel_convert_effect(condition, condition != 0 ? (volatile int*)0 : &touched), 0);
                failures += touched != initial + (condition == 0);
                h = h * 31 + (unsigned)touched;
                touched = initial;
                BUSTER_TEST_FLOAT_CONVERSION(fsel_convert_effect_float(condition, condition != 0 ? &touched : (volatile int*)0), 0);
                failures += touched != initial + (condition != 0);
                h = h * 31 + (unsigned)touched;
            }
        }
    }
    __builtin_ia32_ldmxcsr(saved);
    *hash = h;
    return failures;
}

#undef BUSTER_TEST_FLOAT_CONVERSION
#undef BUSTER_TEST_DOUBLE_CONVERSION

int main(void)
{
    double edges[11];
    const unsigned long patterns[11] = { 0x7ff8000000000000UL, 0xfff8000000000123UL, 0x8000000000000000UL, 0UL, 0x7ff0000000000000UL, 0xfff0000000000000UL,
                                         0x3ff8000000000000UL, 0xc002000000000000UL, 0x0000000000000001UL, 0x3ff0000000000000UL, 0x3fd0000000000000UL };
    for (unsigned i = 0; i < 11; i += 1) edges[i] = make(patterns[i]);
    const int conditions[4] = { 0, 1, -1, -2147483647 - 1 };
    unsigned long h = 0;
    for (unsigned i = 0; i < 11; i += 1) for (unsigned j = 0; j < 11; j += 1)
    {
        const double a = edges[i];
        const double b = edges[j];
        const double x = edges[(i + 3) % 11];
        const double y = edges[(j + 5) % 11];
        h = h * 31 + bits(fsel_less(a, b, x, y));
        h = h * 31 + bits(fsel_greater(a, b, x, y));
        h = h * 31 + bits(fsel_at_most(a, b, x, y));
        h = h * 31 + bits(fsel_at_least(a, b, x, y));
        h = h * 31 + bits(fsel_equal(a, b, x, y));
        h = h * 31 + bits(fsel_unequal(a, b, x, y));
        h = h * 31 + bits(fsel_not_less(a, b, x, y));
        h = h * 31 + bits(fsel_not_equal(a, b, x, y));
        h = h * 31 + fbits(fsel_float_less((float)a, (float)b, (float)x, (float)y));
        h = h * 31 + fbits(fsel_float_not_greater((float)a, (float)b, (float)x, (float)y));
        h = h * 31 + fbits(fsel_float_unequal((float)a, (float)b, (float)x, (float)y));
        h = h * 31 + bits(fsel_mixed((float)a, (float)b, x, y));
        h = h * 31 + fbits(fsel_int((float)a, (float)b, conditions[j % 4]));
        h = h * 31 + bits(fsel_triangle(a, b, conditions[i % 4]));
    }
    for (unsigned i = 0; i < 11; i += 1) h = h * 31 + bits(fsel_clamp(edges[i])) + bits(fsel_nested(edges[i])) * 3 + bits(fsel_magnitude(edges[i])) * 7;
    for (int k = -8; k <= 24; k += 1) h = h * 31 + bits(fsel_clamp(k / 16.0)) + bits(fsel_nested(k / 16.0)) * 3 + bits(fsel_magnitude(k / -16.0)) * 7;
    int ints[64];
    double reals[64];
    double others[64];
    unsigned long state = 2842;
    for (unsigned i = 0; i < 64; i += 1)
    {
        state = state * 6364136223846793005UL + 1;
        ints[i] = (int)(state >> 61) - 3;
        reals[i] = (i % 7) == 3 ? edges[(state >> 40) % 11] : (double)((long)(state >> 50) - 8192) / 4096.0;
        others[i] = (double)(i % 13) - 6.0;
    }
    for (long n = 0; n <= 64; n += 1)
    {
        h = h * 31 + bits(fsel_count(ints, n)) + bits(fsel_values(reals, others, n)) * 3 + bits(fsel_reused(reals, n)) * 5;
        h = h * 31 + bits(fsel_convert_count(ints, n));
    }
    for (unsigned i = 0; i < 64; i += 1)
    {
        const int take = conditions[i % 4];
        h = h * 31 + bits(fsel_guarded(take, take ? &reals[i] : (const double*)0));
        h = h * 31 + bits(fsel_divided(reals[i], others[i], take));
        h = h * 31 + bits(fsel_choice(take, conditions[(i / 4) % 4], reals[i], others[i], edges[i % 11]));
    }
#ifdef __AVX512F__
    for (unsigned i = 0; i + 16 <= 64; i += 1)
    {
        unsigned out = 0;
        h = h * 31 + bits(fsel_masked(ints + i, ints + (i * 7) % 48, conditions[i % 4], reals[i], others[i], &out));
        h = h * 31 + out;
    }
#endif
    const unsigned conversion_failures = check_conversions(&h);
    printf("%lu %u\n", h, conversion_failures);
    return conversion_failures != 0;
}
