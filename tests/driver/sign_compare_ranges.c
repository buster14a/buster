// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1391 expression-range regressions and deliberate warning controls.

int reported_half(unsigned short count, unsigned i)
{
    return i < count / 2;
}

int narrow_arithmetic(unsigned char a, unsigned short b, unsigned n)
{
    return (a + b < n) + (a - b < n) + (a * b < n) +
           (a / 2 < n) + ((a | b) < n) + ((a ^ b) < n);
}

int signed_arithmetic(int x, unsigned n)
{
    return (x + 1 < n) + (x - 1 < n) + (x * 2 < n) +
           (x / 2 < n) + ((x >> 1) < n);
}

int unknown_divisor(unsigned char b, int divisor, unsigned n)
{
    return b / divisor < n;
}

int masks(int x, unsigned n)
{
    return ((x & 127) < n) + ((x | 1) < n) + ((x ^ -1) < n) + ((x & -1) < n);
}

int shifts(unsigned char b, int amount, unsigned n)
{
    return ((b >> amount) < n) + ((1 << amount) < n) + (((int)1 << amount) < n) +
           ((b << amount) < n) + (((1 + 0) << amount) < n);
}

enum { ONE = 1 };
int nonliteral_one(int amount, unsigned n)
{
    return ((ONE << amount) < n) + ((_Generic(0, int: 1) << amount) < n) +
           ((__builtin_choose_expr(1, 1, 0) << amount) < n);
}

struct Fields
{
    unsigned narrow : 5;
    signed signed_narrow : 5;
    unsigned full : 32;
    unsigned long wide : 40;
};

int field_ranges(struct Fields fields, int x, unsigned long n)
{
    return (fields.narrow + 1 < n) + (fields.signed_narrow + 1 < n) +
           (x == fields.wide) + (x < fields.wide) + (x == fields.full);
}

int assignment_conditional(int x, int y, unsigned n)
{
    int result = (x = ((y = 0) ? -1 : 3)) < n;
    return result + x + y;
}

int comma_conditional(int x, int y, unsigned n)
{
    int result = ((void)x, ((y = 0) ? -1 : 3)) < n;
    return result + y;
}

int assignment_nonnegative(int x, int y, unsigned n)
{
    int result = (x = ((y = 0) ? 2 : 3)) < n;
    return result + x + y;
}

int comma_unknown(int x, int y, unsigned n)
{
    int result = ((y = 0), x) < n;
    return result + y;
}

int assignment_widening(unsigned char b, unsigned long n)
{
    long x;
    int result = (x = b) < n;
    return result + (x != 0);
}

int assignment_narrowing(unsigned char b, unsigned n)
{
    signed char x;
    int full = (x = b) < n;
    int masked = (x = (b & 255)) < n;
    int divided = (x = b / 2) < n;
    int small_mask = (x = (b & 127)) < n;
    return full + masked + divided + small_mask + x;
}

int assignment_signed_target(unsigned b, unsigned long n)
{
    int x;
    int result = (x = b) < n;
    return result + x;
}

int assignment_constant_narrowing(unsigned n)
{
    signed char x;
    int negative = (x = 128) < n;
    int zero = (x = 256) < n;
    return negative + zero + x;
}

int equality_boundaries(int x)
{
    return (x == 2147483647u) + (x == 2147483648u) + (x == 4294967295u);
}

int equality_mask(int x, unsigned n)
{
    return (x == (n & 255u)) + (x != (n >> 1)) + (x < (n & 255u));
}

int equality_assignment(int x, unsigned n)
{
    int small = x == (n = 3);
    int full = x == (n = 4294967295u);
    int wrapped = x == (n = -1);
    return small + full + wrapped + (n != 0);
}

int equality_folded_unsigned(int x, int y)
{
    int left = x == ((y = 0) ? 4294967295u : 3u);
    int right = ((y = 0) ? 4294967295u : 3u) == x;
    return left + right + y;
}

int equality_wrapped_unsigned(int x, int y, unsigned n)
{
    int assigned = x == (n = ((y = 0) ? 4294967295u : 3u));
    int comma = x == ((void)y, ((y = 0) ? 4294967295u : 3u));
    return assigned + comma + y + (n != 0);
}

int equality_small_unsigned(int x, int y)
{
    int result = x == ((y = 0) ? 9u : 3u);
    return result + y;
}

int relational_assigned_zero(int x, unsigned n)
{
    int less = x < (n = 0);
    int greater_equal = x >= (n = 0);
    return less + greater_equal + (n != 0);
}

int conditional_arithmetic(int c, unsigned char b, unsigned n)
{
    return (c ? b / 2 : b + 1) < n;
}

int explicit_unsigned_width(int x, unsigned char b)
{
    return x == (unsigned)b;
}

int conditional_implicit_conversion(int x, int c)
{
    return x == (c ? -1 : 3u);
}

int full_constant_comparison(void)
{
    return -1 == 4294967295u;
}

int bitint_conversion(unsigned b, unsigned n)
{
    unsigned _BitInt(1) one;
    signed _BitInt(7) narrow;
    int zero = (one = 2u) == n;
    int unknown = (narrow = b) < n;
    return zero + unknown + one + narrow;
}
