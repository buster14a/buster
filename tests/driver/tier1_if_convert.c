// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#include "tier1-if-convert.h"
long if_max(long a, long b) { return a > b ? a : b; }
unsigned long if_umax(unsigned long a, unsigned long b) { return a > b ? a : b; }
long if_clamp(long x, long lo, long hi) { long y = x < lo ? lo : x; return y > hi ? hi : y; }
unsigned long if_add(unsigned long sum, unsigned long x, int take) { if (take) sum += x; return sum; }
unsigned long if_both(unsigned long a, unsigned long b, int take) { return take ? a + b : a - b; }
int if_bool(int a, int b) { return (a < 7 && b > 9) || (a == 11 && b == 13); }
unsigned long if_sum(const unsigned long* p, unsigned n, unsigned long mask) { unsigned long sum = 0; for (unsigned i = 0; i < n; i += 1) { unsigned long x = p[i]; if (x & mask) sum += x; } return sum; }
unsigned long if_load(int take, const unsigned long* p) { return take ? *p : 9; }
unsigned long if_div(int take, unsigned long a, unsigned long b) { return take ? a / b : 11; }
unsigned long if_shift(int take, unsigned long a, unsigned b) { return take ? a << b : 13; }
unsigned long if_effects(int take, volatile unsigned long* p, unsigned long x) { if (take) { *p = x; return *p; } return 17; }
unsigned long if_call(int take, unsigned long x) { return take ? other_effect(x) : 19; }
long if_nested(long x) { return x < 0 ? 0 : x > 255 ? 255 : x; }
_Bool if_and(int a, int b) { return a < 128 && b < 128; }
int if_andor(int a, int b, int c) { return (a < b) && (b < c || c == 0); }
_Bool if_andor_bool(int a, int b, int c) { return (a < b) && (b < c || c == 0); }
long if_condsum(const int* a, int n) { long s = 0; for (int i = 0; i < n; i++) if (a[i] > 128) s += a[i]; return s; }
long if_abssum(const int* a, int n) { long s = 0; for (int i = 0; i < n; i++) s += a[i] > 128 ? a[i] : -a[i]; return s; }
int if_runmax(const int* a, int n) { int m = 0; for (int i = 0; i < n; i++) m = a[i] > m ? a[i] : m; return m; }
unsigned long if_reload_call(const unsigned long* p) { unsigned long v = *p; unsigned long w = other_effect(v); return v > 3 ? *p + w : w; }

long if_likely(long a, long b)
{
    const long expected = 1;
    return __builtin_expect(a > b, expected) ? a : b;
}

long if_unlikely(long a, long b)
{
    if (!__builtin_expect(a > b, 0))
    {
        a = b;
    }
    return a;
}

long if_probability(long a, long b)
{
    return __builtin_expect_with_probability(a > b, 1, 0.9) != 0 ? a : b;
}

long if_rare_bool(long a, long b)
{
    return __builtin_expect_with_probability(a > b, 1, 0.1) ? a : b;
}

long if_balanced(long a, long b)
{
    return __builtin_expect_with_probability(a > b, 1, 0.5) ? a : b;
}

long if_rare_integer(long a, long b)
{
    return __builtin_expect_with_probability(a, 1, 0.01) ? a : b;
}

long if_dynamic(long a, long b, volatile long* expected)
{
    return __builtin_expect(a > b, (*expected)++) ? a : b;
}

long if_mixed_hints(long x, long y, long z)
{
    long v = __builtin_expect(x < y, 0) ? y : x;
    return v < z ? z : v;
}

long if_rare_code(long a, long b)
{
    return -10 == __builtin_expect_with_probability(a, -10, 0.01) ? b : a;
}

long if_variable_compare(long a, long b)
{
    return __builtin_expect(a, 0) > b ? a : b;
}
