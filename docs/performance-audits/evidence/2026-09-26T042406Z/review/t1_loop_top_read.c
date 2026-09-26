/* Values defined before a loop, read only near the loop top (so their homes
   are live around the back edge), plus many spilled temporaries in branches
   laid out later in the loop body. If back-edge liveness were lost, a later
   temporary could share a top-read value's home and clobber it on the next
   iteration. */
#include <stdio.h>
#include <stdint.h>

static uint64_t g_sink;
__attribute__((noinline)) static uint64_t mix(uint64_t x)
{
    x ^= x >> 31; x *= 0x9E3779B97F4A7C15ull; x ^= x >> 29;
    g_sink += x;
    return x;
}

#define T8(p, seed) \
    uint64_t p##0 = mix(seed), p##1 = mix(p##0 + 1), p##2 = mix(p##1 + 2), p##3 = mix(p##2 + 3), \
             p##4 = mix(p##3 + 4), p##5 = mix(p##4 + 5), p##6 = mix(p##5 + 6), p##7 = mix(p##6 + 7)
#define S8(p) (p##0 + 3 * p##1 + 5 * p##2 + 7 * p##3 + 11 * p##4 + 13 * p##5 + 17 * p##6 + 19 * p##7)

__attribute__((noinline)) uint64_t kernel(int n, int sel)
{
    T8(a, 100 + (uint64_t)sel);          /* a0..a7 live around the loop, read at the top */
    uint64_t acc = 1;
    for (int i = 0; i < n; i++)
    {
        /* top of loop: read every a-value (and nothing after the loop does) */
        acc = acc * 31 + mix(a0 ^ (uint64_t)i) + a1 + (a2 ^ a3) + (a4 - a5) + (a6 | a7);
        if (((uint64_t)i + (uint64_t)sel) % 3 == 0)
        {
            T8(t, acc + 7);
            acc ^= S8(t);
        }
        else if (((uint64_t)i + (uint64_t)sel) % 3 == 1)
        {
            T8(u, acc + 11);
            T8(v, u7 + 1);
            acc += S8(u) - S8(v);
        }
        else
        {
            T8(w, acc ^ 0x55);
            acc -= S8(w);
            if (acc & 1)
            {
                T8(x, acc);
                acc += S8(x);
                continue;
            }
        }
        acc = mix(acc);
    }
    return acc;
}

int main(void)
{
    uint64_t h = 0;
    for (int s = 0; s < 5; s++)
    {
        h = h * 1000003u + kernel(7 + s, s);
    }
    printf("t1 %llu %llu\n", (unsigned long long)h, (unsigned long long)g_sink);
    return 0;
}
