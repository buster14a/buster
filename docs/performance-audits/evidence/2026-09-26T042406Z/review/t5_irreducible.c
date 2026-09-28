/* Irreducible control flow: two entries into a loop (goto into the middle
   of the body), a value live around it that is read only at one entry
   point, and spilled temporaries on both entries' paths. */
#include <stdio.h>
#include <stdint.h>

static uint64_t g_sink;
__attribute__((noinline)) static uint64_t mix(uint64_t x)
{
    x ^= x >> 29; x *= 0xd6e8feb86659fd93ull; x ^= x >> 32;
    g_sink += x >> 3;
    return x;
}

#define T6(p, seed) \
    uint64_t p##0 = mix(seed), p##1 = mix(p##0 + 1), p##2 = mix(p##1 + 2), p##3 = mix(p##2 + 3), \
             p##4 = mix(p##3 + 4), p##5 = mix(p##4 + 5)
#define S6(p) (p##0 + 3 * p##1 + 5 * p##2 + 7 * p##3 + 11 * p##4 + 13 * p##5)

__attribute__((noinline)) uint64_t irr(int n, uint64_t seed)
{
    T6(k, seed);
    uint64_t acc = seed;
    int i = 0;
    if (seed & 1)
    {
        goto middle;
    }
top:
    {
        acc += k0 + (k1 ^ k2);           /* k0..k2 read only here */
        T6(a, acc);
        acc ^= S6(a);
    }
middle:
    {
        acc = mix(acc + k3 + (k4 ^ k5)); /* k3..k5 read only here */
        T6(b, acc ^ (uint64_t)i);
        T6(c, b2);
        acc -= S6(b) ^ S6(c);
    }
    if (++i < n)
    {
        if (acc & 2)
        {
            goto top;
        }
        goto middle;
    }
    return acc;
}

int main(void)
{
    uint64_t h = 0;
    for (uint64_t s = 0; s < 16; s++)
    {
        h = h * 1000003u + irr(3 + (int)(s % 6), s * 0x5bd1e995ull + 7);
    }
    printf("t5 %llu %llu\n", (unsigned long long)h, (unsigned long long)g_sink);
    return 0;
}
