// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original input for #1115's whole-program -S corpus (assembler_programs_test.c): float, double and long double
// arithmetic, conversions and comparisons, which compilers write with scalar SSE, its compare predicates (cmpltsd) and
// the x87's stack registers (fmulp %st, %st(1)), and the shifts by one (shrl %ecx). No generated assembly is checked in.

#include <stdio.h>

// Volatile, so that the compilers keep the operations rather than fold them.
static volatile double doubles[] = { 1.5, -2.25, 3.75, 1e10, -0.0, 0.1 };
static volatile float floats[] = { 0.5f, -1.75f, 8.0f, 3.25f };
static volatile long double extendeds[] = { 2.5L, -3.0L, 1e100L, 0.125L };
static volatile int integers[] = { 7, -9, 1 << 30, 3 };
static volatile unsigned naturals[] = { 9u, 0x80000001u, 1u, 0xffffffffu };

double floating_mix(double a, int b, float c)
{
    return a * b + c / 2.0f + (a > c ? 1.0 : 0.0);
}

float floating_single(float a, float b)
{
    return a < b ? a * b - 1.0f : a / b + 2.0f;
}

long double floating_extended(long double x, long double y)
{
    return x * y + 1.5L - x / y;
}

int floating_extended_less(long double a, long double b)
{
    return (a < b) + 2 * (a == b);
}

int floating_compare(double a, double b)
{
    return (a < b) + 2 * (a == b) + 4 * (a > b) + 8 * (a != a);
}

double floating_minimum(double a, double b)
{
    return a < b ? a : b;
}

unsigned floating_halve(unsigned x)
{
    return x >> 1;
}

int floating_halve_signed(int x)
{
    return x >> 1;
}

unsigned floating_double_bits(unsigned x)
{
    return x << 1 | x >> 31;
}

long floating_to_long(double x)
{
    return (long)x;
}

double floating_from_long(long x)
{
    return (double)x;
}

unsigned long floating_to_unsigned(double x)
{
    return (unsigned long)x;
}

double floating_from_unsigned(unsigned long x)
{
    return (double)x;
}

float floating_narrow(double x)
{
    return (float)x;
}

long double floating_widen(double x)
{
    return x;
}

long floating_extended_to_long(long double x)
{
    return (long)x;
}

int main(void)
{
    double sum = 0;
    for (int i = 0; i < 6; i += 1)
    {
        sum += floating_mix(doubles[i], integers[i % 4], floats[i % 4]);
    }
    float single = 0;
    for (int i = 0; i < 4; i += 1)
    {
        single += floating_single(floats[i], floats[(i + 1) % 4]);
    }
    long double extended = 0;
    int ordered = 0;
    for (int i = 0; i < 4; i += 1)
    {
        extended += floating_extended(extendeds[i], extendeds[(i + 1) % 4]);
        ordered = ordered * 3 + floating_extended_less(extendeds[i], extendeds[(i + 2) % 4]);
    }
    int compared = 0;
    double minimum = doubles[0];
    for (int i = 0; i < 6; i += 1)
    {
        compared = compared * 5 + floating_compare(doubles[i], doubles[(i + 1) % 6]);
        minimum = floating_minimum(minimum, doubles[i]);
    }
    unsigned shifted = 0;
    int shifted_signed = 0;
    for (int i = 0; i < 4; i += 1)
    {
        shifted = shifted * 7 + floating_halve(naturals[i]) + floating_double_bits(naturals[i]);
        shifted_signed = shifted_signed * 3 + floating_halve_signed(integers[i]);
    }
    long converted = floating_to_long(doubles[3]) + floating_to_long(doubles[1]) + floating_extended_to_long(extendeds[1]);
    double back = floating_from_long(integers[2]) + floating_from_unsigned(0x8000000000000001ul) + (double)floating_to_unsigned(1e19);
    double narrowed = floating_narrow(doubles[5]) + (double)floating_widen(doubles[2]);
    printf("%.6f %.4f %.6Lg %d %d %.3f %u %d %ld %.1f %.9f\n", sum, (double)single, extended, ordered, compared, minimum, shifted, shifted_signed, converted, back,
           narrowed);
    // Exactly representable results, checked in the program itself as well as against GNU as's object's output.
    const int exact = floating_halve(naturals[0]) == 4u && floating_halve_signed(integers[1]) == -5 && floating_compare(1.0, 2.0) == 1 && floating_to_long(-2.25) == -2 &&
                      floating_extended(2.0L, 4.0L) == 9.0L && floating_minimum(3.0, -1.0) == -1.0 && floating_extended_less(1.0L, 1.0L) == 2;
    return exact ? 0 : 1;
}
