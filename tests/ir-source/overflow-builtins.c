// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1385's overflow builtins in the IR: signed longs' sums, differences and products (the typed stream's X86 rows of the
// operation and seto, the IR's saddo, ssubo and smulo), ints' 64-bit operations, unsigned longs' carries and 128-bit
// products, mixed signedness into narrower destinations, _Bool and signed char destinations and an unsigned 32-bit
// product past 2^63 into a long, of values at each width's boundaries. test-ir-source lowers them by both paths, promotes
// them and reads their text back; test-ssa-source runs the program in the IR's forms, the typed stream's interpreter and
// Tier 1's builds against clang -O0's build's output.
#include <stdio.h>

static const long values[] = {
    0, 1, -1, 2, 0x7fffffffL, 0x80000000L, -0x80000000L, 3037000500L, -3037000500L, 0x7fffffffffffffffL, -0x7fffffffffffffffL - 1, -0x7fffffffffffffffL,
};

static unsigned long long hash;

static void mix(unsigned long long v)
{
    hash = hash * 31 + v;
}

static void longs(long a, long b)
{
    long r = 0;
    long long q = 0;
    mix((unsigned long long)__builtin_add_overflow(a, b, &r));
    mix((unsigned long long)r);
    mix((unsigned long long)__builtin_sub_overflow(a, b, &r));
    mix((unsigned long long)r);
    mix((unsigned long long)__builtin_mul_overflow(a, b, &r));
    mix((unsigned long long)r);
    mix((unsigned long long)__builtin_saddll_overflow(a, b, &q));
    mix((unsigned long long)q);
    mix((unsigned long long)__builtin_smull_overflow(a, b, &r));
    mix((unsigned long long)r);
}

static void others(long a, long b)
{
    unsigned long ua = (unsigned long)a;
    unsigned long ub = (unsigned long)b;
    unsigned long ur = 0;
    int ia = (int)a;
    int ib = (int)b;
    int ir = 0;
    unsigned uia = (unsigned)a;
    unsigned uib = (unsigned)b;
    long r = 0;
    signed char c = 0;
    _Bool flag = 0;
    mix((unsigned long long)__builtin_add_overflow(ua, ub, &ur));
    mix(ur);
    mix((unsigned long long)__builtin_sub_overflow(ua, ub, &ur));
    mix(ur);
    mix((unsigned long long)__builtin_mul_overflow(ua, ub, &ur));
    mix(ur);
    mix((unsigned long long)__builtin_mul_overflow(a, ub, &r));
    mix((unsigned long long)r);
    mix((unsigned long long)__builtin_add_overflow(a, ub, &ir));
    mix((unsigned long long)ir);
    mix((unsigned long long)__builtin_add_overflow(ia, ib, &ir));
    mix((unsigned long long)ir);
    mix((unsigned long long)__builtin_mul_overflow(ia, ib, &ir));
    mix((unsigned long long)ir);
    mix((unsigned long long)__builtin_mul_overflow(uia, uib, &r));
    mix((unsigned long long)r);
    mix((unsigned long long)__builtin_sub_overflow(uia, uib, &r));
    mix((unsigned long long)r);
    mix((unsigned long long)__builtin_add_overflow(ia, uib, &c));
    mix((unsigned long long)c);
    mix((unsigned long long)__builtin_sub_overflow(ia, ib, &flag));
    mix((unsigned long long)flag);
}

int main(void)
{
    const unsigned count = sizeof(values) / sizeof(values[0]);
    for (unsigned i = 0; i < count; i += 1)
    {
        for (unsigned j = 0; j < count; j += 1)
        {
            longs(values[i], values[j]);
        }
    }
    printf("longs %llx\n", hash);
    hash = 0;
    for (unsigned i = 0; i < count; i += 1)
    {
        for (unsigned j = 0; j < count; j += 1)
        {
            others(values[i], values[j]);
        }
    }
    printf("others %llx\n", hash);
    return 0;
}
