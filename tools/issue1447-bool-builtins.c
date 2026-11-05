// Original Clang oracle probe for #1447's boolean transport builtins.
#include <stdio.h>
#include <stdint.h>
#ifndef LANES
#define LANES 8
#endif
#ifndef OP
#define OP 1
#endif
typedef _Bool Bits __attribute__((ext_vector_type(LANES)));

static __attribute__((noinline)) Bits operation(Bits a, Bits b, Bits c)
{
#if OP == 1
    return __builtin_elementwise_max(a, b);
#elif OP == 2
    return __builtin_elementwise_min(a, b);
#elif OP == 3
    return __builtin_elementwise_add_sat(a, b);
#elif OP == 4
    return __builtin_elementwise_sub_sat(a, b);
#elif OP == 5
    return __builtin_elementwise_popcount(a);
#elif OP == 6
    return __builtin_elementwise_bitreverse(a);
#elif OP == 7
    return __builtin_elementwise_clzg(a, b);
#elif OP == 8
    return __builtin_elementwise_ctzg(a, b);
#elif OP == 9
    return __builtin_elementwise_fshl(a, b, c);
#elif OP == 10
    return __builtin_elementwise_fshr(a, b, c);
#elif OP == 11
    return __builtin_elementwise_pdep(a, b);
#elif OP == 12
    return __builtin_elementwise_pext(a, b);
#elif OP == 13
    return __builtin_elementwise_clmul(a, b);
#else
#error OP outside probe
#endif
}

int main(void)
{
    unsigned failures = 0;
    unsigned hash = 2166136261u;
    for (unsigned pattern = 0; pattern < 8; pattern += 1)
    {
        Bits a = {0}, b = {0}, c = {0};
        for (unsigned lane = 0; lane < LANES; lane += 1)
        {
            const unsigned value = (pattern + lane) & 7;
            a[lane] = value & 1;
            b[lane] = (value >> 1) & 1;
            c[lane] = (value >> 2) & 1;
        }
        const Bits result = operation(a, b, c);
        for (unsigned lane = 0; lane < LANES; lane += 1)
        {
            const unsigned value = (pattern + lane) & 7;
            const unsigned x = value & 1;
            const unsigned y = (value >> 1) & 1;
            unsigned expected = x;
            if (OP == 1 || OP == 3)
            {
                expected = x | y;
            }
            else if (OP == 2 || OP >= 11)
            {
                expected = x & y;
            }
            else if (OP == 4)
            {
                expected = x & (y ^ 1);
            }
            else if (OP == 7 || OP == 8)
            {
                expected = (x ^ 1) & y;
            }
            const unsigned observed = result[lane];
            failures += observed != expected;
            hash = (hash ^ observed) * 16777619u;
        }
    }
    printf("op=%u lanes=%u failures=%u hash=%08x\n", OP, LANES, failures, hash);
    return failures != 0;
}
