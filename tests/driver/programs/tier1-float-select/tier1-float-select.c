// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#include "tier1-float-select.h"
double fsel_count(const int* p, long n) { double s = 0; for (long i = 0; i < n; i++) s += p[i] > 0 ? 3.0 : 1.0; return s; }
double fsel_values(const double* p, const double* q, long n) { double s = 0; for (long i = 0; i < n; i++) s += p[i] > 0 ? p[i] : q[i]; return s; }
float fsel_int(float a, float b, int c) { return c ? a : b; }
double fsel_less(double a, double b, double x, double y) { return a < b ? x : y; }
double fsel_greater(double a, double b, double x, double y) { return a > b ? x : y; }
double fsel_at_most(double a, double b, double x, double y) { return a <= b ? x : y; }
double fsel_at_least(double a, double b, double x, double y) { return a >= b ? x : y; }
double fsel_equal(double a, double b, double x, double y) { return a == b ? x : y; }
double fsel_unequal(double a, double b, double x, double y) { return a != b ? x : y; }
double fsel_not_less(double a, double b, double x, double y) { return !(a < b) ? x : y; }
double fsel_not_equal(double a, double b, double x, double y) { return !(a == b) ? x : y; }
float fsel_float_less(float a, float b, float x, float y) { return a < b ? x : y; }
float fsel_float_not_greater(float a, float b, float x, float y) { return !(a > b) ? x : y; }
float fsel_float_unequal(float a, float b, float x, float y) { return a != b ? x : y; }
double fsel_mixed(float a, float b, double x, double y) { return a < b ? x : y; }
double fsel_triangle(double s, double x, int c) { if (c) s = x; return s; }
double fsel_clamp(double x) { double y = x < 0.0 ? 0.0 : x; return y > 1.0 ? 1.0 : y; }
double fsel_nested(double x) { return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x; }
double fsel_magnitude(double x) { return x < 0.0 ? -x : x; }
double fsel_reused(const double* p, long n) { double s = 0; for (long i = 0; i < n; i++) s += p[i] > 0.25 ? p[i] : 0.5; return s; }
double fsel_guarded(int c, const double* p) { return c ? *p : 2.0; }
double fsel_divided(double a, double b, int c) { return c ? a / b : 1.0; }
double fsel_choice(int a, int b, double x, double y, double z) { return a ? (b ? x : y) : z; }

// #2842's conversion after an integer join now folds into each exact constant arm. A variable
// int or unsigned also converts exactly to double, so moving that conversion must retain a blend.
double fsel_convert_double(int c, int x)
{
    return c ? x : 3;
}

double fsel_convert_double_reverse(int c, int x)
{
    return c ? 3 : x;
}

double fsel_convert_unsigned(int c, unsigned x)
{
    return c ? x : 3u;
}

double fsel_convert_double_constants(int c)
{
    return c ? 3 : 1;
}

float fsel_convert_float_constants(int c)
{
    return c ? -16777216 : 16777216;
}

double fsel_convert_mixed_signedness(int c)
{
    return c ? -1 : 2u;
}

double fsel_convert_double_logical(int a, int b)
{
    return (a || b) + 0.5;
}

float fsel_convert_float_logical(int a, int b)
{
    return (a && b) + 0.5f;
}

double fsel_convert_count(const int* p, long n)
{
    double sum = 0;
    for (long i = 0; i < n; i += 1)
    {
        sum += p[i] > 0 ? 3 : 1;
    }
    return sum;
}

double fsel_convert_effect(int a, volatile int* p)
{
    return (a || ++*p) + 0.5;
}

float fsel_convert_effect_float(int a, volatile int* p)
{
    return (a && ++*p) + 0.5f;
}

// These integer joins keep their conversion because a moved, unselected conversion could set
// inexact. Where the source itself converts within an arm, that arm must retain its guard.
float fsel_round_float(int c, int x)
{
    return c ? x : 3;
}

double fsel_round_double(int c, long x)
{
    return c ? x : 3L;
}

float fsel_guarded_float_convert(int c, int x)
{
    return c ? (float)x : 3.0f;
}

double fsel_guarded_double_convert(int c, long x)
{
    return c ? (double)x : 3.0;
}

float fsel_round_float_constant(int c)
{
    return c ? 16777217 : 3;
}

double fsel_round_double_constant(int c)
{
    return c ? 9007199254740993L : 3L;
}

// The narrow negative is sign-extended before the common unsigned-long conversion. Its selected
// value is UINT64_MAX, which rounds; treating the checked payload as 255 or 65535 would falsely fold it.
float fsel_round_narrow_float(int c)
{
    return c ? (signed char)-1 : 1ul;
}

double fsel_round_narrow_double(int c)
{
    return c ? (short)-1 : 1ul;
}

#ifdef __AVX512F__
#include <immintrin.h>
double fsel_masked(const int* p, const int* q, int c, double x, double y, unsigned* out)
{
    const __mmask16 m = _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(p), _mm512_loadu_si512(q));
    const double r = c ? x : y;
    *out = _cvtmask16_u32(m) * 2 + (r > 0.0);
    return r;
}
#endif
