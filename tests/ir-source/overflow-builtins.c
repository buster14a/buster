// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1385's overflow builtins in the IR: signed longs' sums, differences and products (the typed stream's X86 rows of the
// operation and seto, the IR's saddo, ssubo and smulo), ints' 64-bit operations, unsigned longs' carries and 128-bit
// products, mixed signedness into narrower destinations, _Bool and signed char destinations and an unsigned 32-bit
// product past 2^63 into a long, of values at each width's boundaries, and _BitInts of 65 to 127 bits (#1447) as operands
// and destinations beside 128-bit and narrower ones. test-ir-source lowers them by both paths, promotes them and reads
// their text back; test-ssa-source runs the program in the IR's forms, the typed stream's interpreter and Tier 1's builds
// against clang -O0's build's output.
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

static void mix_bytes(const void* pointer)
{
    unsigned long long words[2];
    __builtin_memcpy(words, pointer, sizeof(words));
    mix(words[0]);
    mix(words[1]);
}

static void bit_ints(long a, long b, unsigned shift)
{
    const __int128 x = (__int128)((unsigned __int128)(__int128)a << shift);
    const __int128 y = (__int128)((unsigned __int128)(__int128)b * 3 << (shift / 2));
    _BitInt(100) s = (_BitInt(100))x;
    _BitInt(100) t = (_BitInt(100))y;
    unsigned _BitInt(127) u = (unsigned _BitInt(127))x;
    unsigned _BitInt(127) v = (unsigned _BitInt(127))y;
    _BitInt(65) c = (_BitInt(65))x;
    unsigned _BitInt(65) d = (unsigned _BitInt(65))y;
    _BitInt(100) r100 = 0;
    unsigned _BitInt(127) r127 = 0;
    _BitInt(127) q127 = 0;
    _BitInt(65) r65 = 0;
    unsigned _BitInt(65) u65 = 0;
    __int128 w = 0;
    long l = 0;
    mix((unsigned long long)__builtin_add_overflow(s, t, &r100));
    mix_bytes(&r100);
    mix((unsigned long long)__builtin_sub_overflow(s, t, &r100));
    mix_bytes(&r100);
    mix((unsigned long long)__builtin_mul_overflow(s, t, &r100));
    mix_bytes(&r100);
    mix((unsigned long long)__builtin_add_overflow(u, v, &r127));
    mix_bytes(&r127);
    mix((unsigned long long)__builtin_mul_overflow(u, v, &r127));
    mix_bytes(&r127);
    mix((unsigned long long)__builtin_sub_overflow(u, t, &q127));
    mix_bytes(&q127);
    mix((unsigned long long)__builtin_mul_overflow(c, d, &r65));
    mix_bytes(&r65);
    mix((unsigned long long)__builtin_add_overflow(c, d, &u65));
    mix_bytes(&u65);
    mix((unsigned long long)__builtin_mul_overflow(x, v, &r100));
    mix_bytes(&r100);
    mix((unsigned long long)__builtin_sub_overflow(a, s, &w));
    mix_bytes(&w);
    mix((unsigned long long)__builtin_mul_overflow(s, b, &l));
    mix((unsigned long long)l);
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
    hash = 0;
    for (unsigned i = 0; i < count; i += 1)
    {
        for (unsigned j = 0; j < count; j += 1)
        {
            bit_ints(values[i], values[j], (i * 7 + j * 13) % 66);
        }
    }
    printf("bit-ints %llx\n", hash);
    return 0;
}
