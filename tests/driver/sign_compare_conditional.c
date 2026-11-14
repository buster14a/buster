// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1391 range regressions. Every warning is a deliberate negative control.

int punctuation(unsigned token, int intel)
{
    return token == (intel ? '[' : '(');
}

int conditional_left(int c, unsigned n)
{
    return ((c ? 1 : 2) < n) + ((c ? 1 : 2) <= n) + ((c ? 1 : 2) > n) +
           ((c ? 1 : 2) >= n) + ((c ? 1 : 2) == n) + ((c ? 1 : 2) != n);
}

int conditional_right(int c, unsigned long n)
{
    return (n < (c ? 1 : 2)) + (n <= (c ? 1 : 2)) + (n > (c ? 1 : 2)) +
           (n >= (c ? 1 : 2)) + (n == (c ? 1 : 2)) + (n != (c ? 1 : 2));
}

int nested(int a, int b, int c, unsigned n)
{
    return (a ? (b ? 0 : 17) : (c ? 23 : 31)) < n;
}

int selected_positive(int c, unsigned n)
{
    return (1 ? (c ? 2 : 3) : -1) < n;
}

int selected_other_positive(int c, unsigned n)
{
    return (0 ? -1 : (c ? 2 : 3)) < n;
}

int unsigned_arms(int c, unsigned char a, unsigned short b, unsigned long n)
{
    return (c ? a : b) < n;
}

int boolean_arms(int c, int a, int b, unsigned n)
{
    return (c ? (a < b) : !a) < n;
}

int boolean_object(int c, _Bool a, unsigned n)
{
    return (c ? a : 7) < n;
}

int unsigned_subconditional(int c, int d, unsigned a, unsigned long n)
{
    return (c ? (d ? a : 1u) : 0L) < n;
}

int signed_limit(int c, unsigned long n)
{
    return (c ? 9223372036854775807L : 0) < n;
}

int folded_casts(int c, unsigned n)
{
    return (c ? (int)3 : (signed char)9) < n;
}

int negative_condition(int c, unsigned n)
{
    return (-c ? 3 : 9) < n;
}

// The unsigned common type already represents every result, even with a negative source arm.
int unsigned_result(int c, unsigned n)
{
    return (c ? 3u : -1) < n;
}

int wider_comparison(int c, unsigned n)
{
    return (long)(c ? 3 : 9) < n;
}

unsigned char narrow_result(void);
int side_effecting_arm(int c, unsigned n)
{
    return (c ? narrow_result() : 3) < n;
}

int assignment_positive(int y, unsigned n)
{
    int result = ((y = 0) ? 3 : 9) < n;
    return result + y;
}

int assignment_constant_equality(int y)
{
    int equal = ((y = 0) ? -1 : 3) == 1u;
    int unequal = ((y = 0) ? -1 : 3) != 1u;
    return equal + unequal + y;
}

int assignment_zero_comparison(int y)
{
    int less = ((y = 0) ? -1 : 3) < 0u;
    int greater_equal = ((y = 0) ? -1 : 3) >= 0u;
    return less + greater_equal + y;
}

// These warnings must survive both the conditional proof and its live emission-stack frames.
int negative_arm(int c, unsigned n)
{
    return (c ? -3 : 9) < n;
}

int unknown_arm(int c, int x, unsigned n)
{
    return (c ? x : 9) < n;
}

int nested_negative(int a, int b, unsigned n)
{
    return (a ? (b ? 1 : -2) : 3) < n;
}

int selected_negative(int c, unsigned n)
{
    return (0 ? (c ? 1 : 2) : -2) < n;
}

int outer_signed_cast(int c, unsigned n)
{
    return (int)(c ? 3 : 9) < n;
}

int inner_signed_cast(int c, unsigned char x, unsigned n)
{
    return (c ? (int)x : 9) < n;
}

int narrowing_cast(int c, unsigned n)
{
    return (signed char)(c ? 3 : 9) < n;
}

int wrapped_constant(int c, unsigned n)
{
    return (c ? (signed char)129 : 2) < n;
}

int shared_unsigned(unsigned char x, unsigned n)
{
    return (x ?: 9) < n;
}

int shared_boolean(_Bool x, unsigned n)
{
    return (x ?: 9) < n;
}

int nested_shared(int c, unsigned char x, unsigned n)
{
    return (c ? (x ?: 9) : 1) < n;
}

int live_warning_frames(int c, int x, unsigned n)
{
    return ((c ? 1 : 2) < n) + (x < n) + (n < (c ? 3 : 4));
}

int assignment_false(int y, int c, unsigned n)
{
    int result = ((y = 0) ? -1 : (c ? 3 : 4)) < n;
    return result + y;
}

int assignment_true(int y, int c, unsigned n)
{
    int result = ((y = 1) ? (c ? 3 : 4) : -1) < n;
    return result + y;
}

int statement_assignment(int y, int c, unsigned n)
{
    int result = (({ y = 0; }) ? -1 : (c ? 3 : 4)) < n;
    return result + y;
}

int assignment_positive_comparison(int y, int c)
{
    int result = ((y = 0) ? -1 : (c ? 3 : 4)) > 0u;
    return result + y;
}

int statement_assignment_arm(int y, int c, unsigned n)
{
    int result = (c ? ({ y = 1; }) : 2) < n;
    return result + y;
}

int nested_modifying_conditional(int y, int c, unsigned n)
{
    int result = (c ? ((y = 0) ? -1 : 3) : 2) < n;
    return result + y;
}

// A balanced tree leaves many pending arms, and every arm still has a nonnegative result.
#define POS_0 a
#define POS_1 (c ? POS_0 : POS_0)
#define POS_2 (c ? POS_1 : POS_1)
#define POS_3 (c ? POS_2 : POS_2)
#define POS_4 (c ? POS_3 : POS_3)
#define POS_5 (c ? POS_4 : POS_4)
#define POS_6 (c ? POS_5 : POS_5)
#define POS_7 (c ? POS_6 : POS_6)
int many_arms(int c, unsigned char a, unsigned n)
{
    return POS_7 < n;
}
