/* Nested loops with break/continue/goto-out, values defined in an outer
   loop and read only in an inner loop's first block, and phi-carried state
   through several joins. */
#include <stdio.h>
#include <stdint.h>

static uint64_t g_sink;
__attribute__((noinline)) static uint64_t mix(uint64_t x)
{
    x ^= x >> 31; x *= 0x94d049bb133111ebull; x ^= x >> 32;
    g_sink = g_sink * 3 + x;
    return x;
}

#define T6(p, seed) \
    uint64_t p##0 = mix(seed), p##1 = mix(p##0 + 1), p##2 = mix(p##1 + 2), p##3 = mix(p##2 + 3), \
             p##4 = mix(p##3 + 4), p##5 = mix(p##4 + 5)
#define S6(p) (p##0 + 3 * p##1 + 5 * p##2 + 7 * p##3 + 11 * p##4 + 13 * p##5)

__attribute__((noinline)) uint64_t nest(int n, int m, uint64_t seed)
{
    uint64_t acc = seed, carried = seed ^ 0xabcdef;
    for (int i = 0; i < n; i++)
    {
        T6(o, acc + (uint64_t)i);            /* read only at inner loop top */
        for (int j = 0; j < m; j++)
        {
            acc += o0 ^ o1 ^ (o2 + o3) ^ (o4 * o5) ^ (uint64_t)j;
            if ((acc & 3) == 0)
            {
                T6(a, acc);
                carried ^= S6(a);
                continue;
            }
            if ((acc & 7) == 5)
            {
                T6(b, carried);
                T6(c, b3);
                acc -= S6(b) + S6(c);
                if (acc % 11 == 3)
                {
                    goto out;
                }
                break;
            }
            {
                T6(d, acc ^ carried);
                carried = mix(carried + S6(d));
            }
        }
        acc = mix(acc ^ carried);
        if (acc % 13 == 7)
        {
            continue;
        }
        {
            T6(e, acc);
            acc += S6(e);
        }
    }
out:
    return acc ^ carried;
}

int main(void)
{
    uint64_t h = 0;
    for (int s = 0; s < 12; s++)
    {
        h = h * 1000003u + nest(5 + s % 4, 6 + s % 5, (uint64_t)s * 0x9e37 + 1);
    }
    printf("t4 %llu %llu\n", (unsigned long long)h, (unsigned long long)g_sink);
    return 0;
}
