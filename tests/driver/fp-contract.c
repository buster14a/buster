// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original bitwise oracle fixture for #1239. No host arithmetic supplies the expected answers: compile the same
// source with Clang -O0 and with both compiler emission paths, at the same target and contraction setting.
extern int printf(const char*, ...);
static unsigned effects;
static void effect(void)
{
    effects += 1;
}

#define BUSTER_FP_FUNCTIONS(T, P) \
    static T P##_add(T a, T b, T c) { return a * b + c; } \
    static T P##_reverse(T a, T b, T c) { return c + a * b; } \
    static T P##_subtract(T a, T b, T c) { return a * b - c; } \
    static T P##_reverse_subtract(T a, T b, T c) { return c - a * b; } \
    static T P##_negative(T a, T b, T c) { return -(a * b) + c; } \
    static T P##_negative_right(T a, T b, T c) { return c - (-(a * b)); } \
    static T P##_double_negative(T a, T b, T c) { return -(-(a * b)) + c; } \
    static T P##_parentheses(T a, T b, T c) { return (((a * b))) + c; } \
    static T P##_positive(T a, T b, T c) { return +(a * b) + c; } \
    static T P##_cast(T a, T b, T c) { return (T)(a * b) + c; } \
    static T P##_separate(T a, T b, T c) { T t = a * b; return t + c; } \
    static T P##_assignment(T a, T b, T c) { return (a = a * b) + c; } \
    static T P##_compound_add(T a, T b, T c) { c += a * b; return c; } \
    static T P##_compound_subtract(T a, T b, T c) { c -= a * b; return c; } \
    static T P##_memory(T a, T b, T c) { T* p = &c; *p += a * b; return c; } \
    static T P##_volatile(T a, T b, T c) { volatile T t = c; t -= a * b; return t; } \
    static T P##_comma(T a, T b, T c) { return (effect(), a * b) + c; } \
    static T P##_comma_right(T a, T b, T c) { return c - (effect(), -(effect(), a * b)); } \
    static T P##_nested(T a, T b, T c) { return (a * b + c) * b + c; } \
    static T P##_products(T a, T b, T c) { return a * b + c * b; } \
    static T P##_conditional(T a, T b, T c) { return (a > 0 ? a * b : b * a) + c; } \
    static T P##_narrow(T a, T b, T c) { return (T)(float)((double)a * (double)b) + c; } \
    static T P##_call(T a, T b, T c) { return P##_separate(a, b, 0) + c; } \
    static T P##_pragma_off(T a, T b, T c) { _Pragma("STDC FP_CONTRACT OFF") return a * b + c; } \
    static T P##_pragma_on(T a, T b, T c) { _Pragma("STDC FP_CONTRACT ON") return a * b + c; } \
    static T P##_pragma_default(T a, T b, T c) { _Pragma("STDC FP_CONTRACT DEFAULT") return a * b + c; } \
    static T P##_clang_off(T a, T b, T c) { _Pragma("clang fp contract(off)") return a * b + c; } \
    static T P##_clang_on(T a, T b, T c) { _Pragma("clang fp contract(on)") return a * b + c; } \
    static T P##_clang_fast(T a, T b, T c) { _Pragma("clang fp contract(fast)") return a * b + c; } \
    static T P##_restored(T a, T b, T c) { { _Pragma("STDC FP_CONTRACT OFF") effect(); } return a * b + c; } \
    static T P##_fenv(T a, T b, T c) { _Pragma("STDC FENV_ACCESS ON") _Pragma("STDC FP_CONTRACT ON") return a * b + c; }

BUSTER_FP_FUNCTIONS(float, f)
BUSTER_FP_FUNCTIONS(double, d)
#undef BUSTER_FP_FUNCTIONS

#define BUSTER_FP_CASES(P) \
    P##_add, P##_reverse, P##_subtract, P##_reverse_subtract, P##_negative, P##_negative_right, \
    P##_double_negative, P##_parentheses, P##_positive, P##_cast, P##_separate, P##_assignment, \
    P##_compound_add, P##_compound_subtract, P##_memory, P##_volatile, P##_comma, P##_comma_right, \
    P##_nested, P##_products, P##_conditional, P##_narrow, P##_call, P##_pragma_off, P##_pragma_on, \
    P##_pragma_default, P##_clang_off, P##_clang_on, P##_clang_fast, P##_restored, P##_fenv

static float (*const floats[])(float, float, float) = { BUSTER_FP_CASES(f) };
static double (*const doubles[])(double, double, double) = { BUSTER_FP_CASES(d) };
#undef BUSTER_FP_CASES

int main(void)
{
    // The first row distinguishes a single rounding from two; the rest include signs, signed zero, overflow
    // cancellation and subnormal halfway cases. NaN payload/sign are not specified by C, so canonicalize only NaNs.
    static const unsigned input_f[][3] = {
        { 0x3f800001, 0x3f7ffffe, 0xbf800000 },
        { 0xbf800001, 0x3f7ffffe, 0x3f800000 },
        { 0x00000000, 0xbf800000, 0x00000000 },
        { 0x80000000, 0x3f800000, 0x80000000 },
        { 0x7f7fffff, 0x40000000, 0xff7fffff },
        { 0x00000001, 0x3f000000, 0x00000001 },
        { 0x7f800000, 0x00000000, 0x3f800000 },
        { 0x7fc00001, 0x3f800000, 0xbf800000 },
    };
    static const unsigned long input_d[][3] = {
        { 0x3ff0000000000001UL, 0x3feffffffffffffeUL, 0xbff0000000000000UL },
        { 0xbff0000000000001UL, 0x3feffffffffffffeUL, 0x3ff0000000000000UL },
        { 0x0000000000000000UL, 0xbff0000000000000UL, 0x0000000000000000UL },
        { 0x8000000000000000UL, 0x3ff0000000000000UL, 0x8000000000000000UL },
        { 0x7fefffffffffffffUL, 0x4000000000000000UL, 0xffefffffffffffffUL },
        { 0x0000000000000001UL, 0x3fe0000000000000UL, 0x0000000000000001UL },
        { 0x7ff0000000000000UL, 0x0000000000000000UL, 0x3ff0000000000000UL },
        { 0x7ff8000000000001UL, 0x3ff0000000000000UL, 0xbff0000000000000UL },
    };
    for (unsigned row = 0; row < sizeof(input_f) / sizeof(input_f[0]); row += 1)
    {
        union FloatBits { float f; unsigned u; } a, b, c, result_f;
        union DoubleBits { double f; unsigned long u; } x, y, z, result_d;
        a.u = input_f[row][0];
        b.u = input_f[row][1];
        c.u = input_f[row][2];
        x.u = input_d[row][0];
        y.u = input_d[row][1];
        z.u = input_d[row][2];
        for (unsigned operation = 0; operation < sizeof(floats) / sizeof(floats[0]); operation += 1)
        {
            effects = 0;
            result_f.f = floats[operation](a.f, b.f, c.f);
            result_d.f = doubles[operation](x.f, y.f, z.f);
            if ((result_f.u & 0x7fffffff) > 0x7f800000)
            {
                result_f.u = 0x7fc00000;
            }
            if ((result_d.u & 0x7fffffffffffffffUL) > 0x7ff0000000000000UL)
            {
                result_d.u = 0x7ff8000000000000UL;
            }
            printf("%u %u %08x %016lx %u\n", row, operation, result_f.u, result_d.u, effects);
        }
    }
    return 0;
}
