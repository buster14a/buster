// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Register results from asm bodies (#2144). Each noinline function is independently compiled and certified by
// Tier 1; Tier 0 runs the same source. Clang -O0 is the oracle for fixed-register output ordering, mixed classes,
// live values under pressure and the outputs that still require a store through an address.

int printf(const char* format, ...);

__attribute__((noinline)) unsigned asm_result_shift(unsigned x, unsigned count)
{
    __asm__("shll %%cl, %0" : "+d"(x) : "c"(count) : "cc");
    return x;
}

static inline __attribute__((always_inline)) unsigned long asm_result_inline(unsigned long x, unsigned long y)
{
    __asm__("movq %0, %%rcx\n\tmovq %1, %0\n\tmovq %%rcx, %1" : "+d"(x), "+a"(y) : : "rcx");
    return x * 3 + y;
}

static inline __attribute__((always_inline)) unsigned long asm_result_inline_last(unsigned long x, unsigned long y)
{
    __asm__("movq %0, %%rcx\n\tmovq %1, %0\n\tmovq %%rcx, %1" : "+d"(x), "+a"(y) : : "rcx");
    return y;
}

__attribute__((noinline)) unsigned long asm_result_swap(unsigned long x, unsigned long y)
{
    return asm_result_inline(x, y) + 5 * asm_result_inline(y, x) + 7 * asm_result_inline_last(x, y) + 11 * asm_result_inline_last(y, x);
}

__attribute__((noinline)) unsigned asm_result_cpuid(void)
{
    unsigned d, b, c, a;
    __asm__ volatile("cpuid" : "=d"(d), "=b"(b), "=c"(c), "=a"(a) : "3"(0u), "2"(0u));
    return a + b * 3u + c * 5u + d * 7u;
}

__attribute__((noinline)) double asm_result_floating(float x, double y)
{
    __asm__("addss %0, %0\n\taddsd %1, %1\n\txorps %%xmm14, %%xmm14" : "+x"(x), "+x"(y) : : "xmm14");
    return (double)x + y;
}

__attribute__((noinline)) double asm_result_mixed(long x, double y)
{
    __asm__("addq $7, %0\n\taddsd %1, %1" : "+d"(x), "+x"(y) : : "cc");
    return (double)x + y;
}

__attribute__((noinline)) unsigned asm_result_flags(long x, long y)
{
    unsigned below, zero, sign;
    __asm__("cmpq %3, %4" : "=@ccb"(below), "=@ccz"(zero), "=@ccs"(sign) : "r"(y), "r"(x));
    return below * 4 + zero * 2 + sign;
}

__attribute__((noinline)) unsigned asm_result_flag_value(unsigned x)
{
    unsigned carry, zero;
    __asm__("addl $1, %0" : "+a"(x), "=@ccc"(carry), "=@ccz"(zero));
    return x * 3 + carry * 5 + zero * 7;
}

__attribute__((noinline)) unsigned long asm_result_address(unsigned long x, unsigned long y)
{
    unsigned long* address = &x;
    __asm__("movq %0, %%rcx\n\tmovq %1, %0\n\tmovq %%rcx, %1" : "+a"(x), "+d"(y) : : "rcx", "memory");
    return *address + y * 3;
}

__attribute__((noinline)) unsigned long asm_result_stored(unsigned long* output, unsigned long x)
{
    unsigned long local;
    __asm__("movq %2, %0\n\tmovq %2, %1\n\taddq $11, %0" : "=&d"(local), "=r"(*output) : "r"(x) : "cc");
    return local + *output * 5;
}

__attribute__((noinline)) unsigned long asm_result_aliases(unsigned long x)
{
    unsigned long first = x;
    unsigned long second = x;
    unsigned long third = x;
    unsigned long* p = &first;
    unsigned long* q = &second;
    // The later output wins when two lvalues name one object. The first statement must retain its named local's
    // store before the indirect output's; the second can return the named local after the indirect output's store.
    __asm__("movq %2, %0\n\tleaq 1(%2), %1" : "=&a"(first), "=&d"(*p) : "r"(x));
    __asm__("movq %2, %0\n\tleaq 3(%2), %1" : "=&d"(*q), "=&a"(second) : "r"(x));
    __asm__("movq %2, %0\n\tleaq 5(%2), %1" : "=&c"(third), "=&d"(third) : "r"(x));
    unsigned first_flag = (unsigned)x;
    unsigned second_flag = (unsigned)x;
    unsigned* flag_before = &first_flag;
    unsigned* flag_after = &second_flag;
    // Flag and GPR output stores obey the same source order, including an alias through an indirect operand.
    __asm__("movl %2, %0\n\tcmpl $0, %2" : "=&a"(*flag_before), "=@ccz"(first_flag) : "r"((unsigned)x));
    __asm__("movl %2, %1\n\tcmpl $0, %2" : "=@ccz"(second_flag), "=&a"(*flag_after) : "r"((unsigned)x));
    return first * 5 + second * 7 + third * 11 + first_flag * 13ul + second_flag * 17ul;
}

__attribute__((noinline)) unsigned long asm_result_pressure(unsigned n, unsigned long seed)
{
    unsigned long a = seed, b = seed + 1, c = seed + 2, d = seed + 3;
    unsigned long e = seed + 4, f = seed + 5, g = seed + 6, h = seed + 7;
    unsigned long j = seed + 8, k = seed + 9, l = seed + 10, m = seed + 11;
    unsigned long sum = 0;
    for (unsigned i = 0; i < n; i += 1)
    {
        unsigned long hi, lo;
        __asm__("mulq %3" : "=d"(hi), "=a"(lo) : "1"(a + i), "r"(b + 0x9e3779b97f4a7c15ul));
        sum += lo ^ hi;
        a += b;
        b ^= c;
        c += d;
        d ^= e;
        e += f;
        f ^= g;
        g += h;
        h ^= j;
        j += k;
        k ^= l;
        l += m;
        m ^= a + sum;
    }
    return sum + a + b * 3 + c * 5 + d * 7 + e * 11 + f * 13 + g * 17 + h * 19 + j * 23 + k * 29 + l * 31 + m * 37;
}

int main(void)
{
    static const unsigned long values[] = {
        0ul,
        1ul,
        0x7ffffffffffffffful,
        0x8000000000000000ul,
        0xfffffffffffffffful,
    };
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i += 1)
    {
        unsigned long stored = 0;
        const unsigned long result = asm_result_stored(&stored, values[i]);
        printf("%u %u %lu %.3f %.3f %u %u %lu %lu %lu %lu %lu\n", i, asm_result_shift((unsigned)values[i], i),
               asm_result_swap(values[i], ~values[i]), asm_result_floating((float)i + 0.5f, (double)i - 0.25),
               asm_result_mixed((long)i * 13 - 20, (double)i + 0.5), asm_result_flags((long)values[i], (long)values[4 - i]), asm_result_flag_value((unsigned)values[i]),
               asm_result_address(values[i], ~values[i]), result, stored, asm_result_aliases(values[i]), asm_result_pressure(i * 3, values[i]));
    }
    printf("cpuid %u\n", asm_result_cpuid());
    return 0;
}
