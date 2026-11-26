// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2351: asm bodies that return a value or name a label. The driver test requires Tier 1 for every function,
// compares execution with Clang, and checks that the object defines each retained label inside its C function.
#include <stdio.h>

extern const unsigned char buster2351_named[];

__attribute__((noinline)) int tier1_asm_goto(int n)
{
    int value = n + 17;
    int other = n * 3 + 5;
    int result;
    __asm__ goto("addl $7, %0\n\txorl $19, %1\n\ttestl %2, %2\n\tjs %l[negative]\n\tjz %l[zero]"
                 : "+&r"(value), "+&r"(other) : "r"(n) : "cc" : negative, zero);
    result = value * 3 + other;
    goto done;
negative:
    result = value * 5 - other;
    goto done;
zero:
    result = value * 7 + other * 11;
done:
    return result;
}

__attribute__((noinline)) int tier1_asm_back(int n)
{
    int count = (n & 7) + 1;
    int total = 0;
again:
    total += count;
    __asm__ goto("decl %0\n\tjnz %l[again]" : "+r"(count) : : "cc" : again);
    return total + count * 100;
}

__attribute__((noinline)) long tier1_asm_live(int n)
{
    long a = n + 101;
    long b = n * 3 + 103;
    long c = n * 5 + 107;
    long d = n * 7 + 109;
    long e = n * 11 + 113;
    long f = n * 13 + 127;
    double floating = (double)n + 0.5;
    long total = 0;
    for (int i = 0; i < 9; i += 1)
    {
        int value = n + i;
        int zero;
        long memory = -1;
        __asm__ goto("addl $3, %0\n\tmovq $37, %1\n\ttestl %0, %0\n\tjz %l[zero_path]\n\tjs %l[negative_path]"
                     : "+r"(value), "=m"(memory), "=@ccz"(zero) : : "rbx", "r12", "xmm0", "cc", "memory" : zero_path, negative_path);
        total += a + b + value + memory + zero * 41;
        goto next;
zero_path:
        total += c + d + value + memory + zero * 43;
        goto next;
negative_path:
        total += e + f + value + memory + zero * 47;
next:
        a += i;
        f ^= a;
        floating += 0.25;
    }
    return total + a + b + c + d + e + f + (long)(floating * 4.0);
}

__attribute__((noinline)) int tier1_asm_named(int n)
{
    __asm__ volatile("buster2351_named:\n\tnop\n\taddl $9, %0" : "+r"(n) : : "cc");
    return n;
}

__attribute__((noinline)) int tier1_asm_empty(int n)
{
    __asm__ volatile("buster2351_empty:" : : : "memory");
    return n + 13;
}

__attribute__((noinline)) int tier1_asm_named_loop(int n)
{
    for (int i = 0; i < 16; i += 1)
    {
        __asm__ volatile("buster2351_loop:\n\taddl $3, %0" : "+r"(n) : : "cc");
    }
    return n;
}

static inline __attribute__((always_inline)) unsigned tier1_asm_private(unsigned n)
{
    unsigned count = (n & 7u) + 1u;
    __asm__(".Lbuster2351_private%=:\n\taddl $5, %0\n\tdecl %1\n\tjnz .Lbuster2351_private%=" : "+r"(n), "+r"(count) : : "cc");
    return n;
}

__attribute__((noinline)) unsigned tier1_asm_private_twice(unsigned n)
{
    return tier1_asm_private(n) * 3u + tier1_asm_private(n + 1u);
}

static inline __attribute__((always_inline, used)) unsigned tier1_asm_retained(unsigned n)
{
    __asm__ volatile("buster2351_retained_%=:\n\tbswap %0" : "+r"(n));
    return n;
}

__attribute__((noinline)) unsigned tier1_asm_retained_twice(unsigned n)
{
    return tier1_asm_retained(n) ^ tier1_asm_retained(n + 1u);
}

__attribute__((noinline)) unsigned tier1_asm_result32(unsigned n, unsigned count)
{
    __asm__("shll %b1, %0" : "+d"(n) : "c"(count) : "cc");
    return n;
}

__attribute__((noinline)) unsigned long tier1_asm_result64(unsigned long n, unsigned count)
{
    unsigned long result;
    __asm__("movq %1, %0\n\tshlq %b2, %0" : "=&d"(result) : "r"(n), "c"(count) : "cc");
    return result;
}

__attribute__((noinline)) unsigned long tier1_asm_result_live(unsigned n)
{
    unsigned long a = n + 1ul;
    unsigned long b = n + 3ul;
    unsigned long c = n + 5ul;
    unsigned long d = n + 7ul;
    unsigned long e = n + 11ul;
    unsigned long f = n + 13ul;
    unsigned long g = n + 17ul;
    unsigned long h = n + 19ul;
    for (unsigned i = 0; i < 13; i += 1)
    {
        unsigned long value = a ^ c ^ e ^ g;
        __asm__("shlq %b1, %0" : "+d"(value) : "c"(i & 3u) : "cc");
        a += value;
        b ^= a;
        c += b;
        d ^= c;
        e += d;
        f ^= e;
        g += f;
        h ^= g;
    }
    return a + b + c + d + e + f + g + h;
}

__attribute__((noinline)) int tier1_asm_discarded(int n)
{
    __asm__ volatile("addl $29, %0" : "+d"(n) : : "cc");
    return 17;
}

int main(void)
{
    for (int n = -5; n <= 9; n += 1)
    {
        printf("%d %d %ld %d %d %d %u %u %u %lu %lu %d\n", tier1_asm_goto(n), tier1_asm_back(n), tier1_asm_live(n), tier1_asm_named(n),
               tier1_asm_empty(n), tier1_asm_named_loop(n), tier1_asm_private_twice((unsigned)n), tier1_asm_retained_twice((unsigned)n),
               tier1_asm_result32((unsigned)n, (unsigned)n & 7u), tier1_asm_result64((unsigned long)n, (unsigned)n & 7u), tier1_asm_result_live((unsigned)n),
               tier1_asm_discarded(n));
    }
    printf("named-byte %u\n", (unsigned)buster2351_named[0]);
    return buster2351_named[0] == 0x90 ? 0 : 1;
}
