/* A switch-driven state machine inside a loop. Case bodies spill many
   temporaries; some values are live from before the loop into only a few
   cases (and around the back edge), others are loop-carried state. */
#include <stdio.h>
#include <stdint.h>

static uint64_t g_sink;
__attribute__((noinline)) static uint64_t mix(uint64_t x)
{
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33;
    g_sink ^= x;
    return x;
}

#define T6(p, seed) \
    uint64_t p##0 = mix(seed), p##1 = mix(p##0 + 1), p##2 = mix(p##1 + 2), p##3 = mix(p##2 + 3), \
             p##4 = mix(p##3 + 4), p##5 = mix(p##4 + 5)
#define S6(p) (p##0 + 3 * p##1 + 5 * p##2 + 7 * p##3 + 11 * p##4 + 13 * p##5)

__attribute__((noinline)) uint64_t machine(int steps, uint64_t seed)
{
    T6(k, seed);                 /* k0..k5: live into specific cases only */
    uint64_t state = seed & 7, acc = seed, carry = 3;
    for (int i = 0; i < steps; i++)
    {
        switch (state)
        {
        case 0: { T6(a, acc); acc += S6(a) + k0; state = (acc >> 3) & 7; break; }
        case 1: { acc ^= k1 * carry; state = 5; break; }
        case 2: { T6(b, acc + carry); T6(c, b5); acc -= S6(b) ^ S6(c); carry = b2; state = (acc >> 7) & 7; break; }
        case 3: { acc = mix(acc + k2 + k3); state = 0; continue; }
        case 4: { T6(d, acc); if (d3 & 1) { acc += S6(d) + k4; state = 6; } else { acc -= d0; state = 2; } break; }
        case 5: { T6(e, carry); carry = S6(e); state = (carry >> 5) & 7; break; }
        case 6: { acc += k5; state = 7; break; }
        default: { T6(f, acc ^ carry); acc = S6(f); state = (acc >> 11) & 7; break; }
        }
        acc = mix(acc ^ (uint64_t)i);
    }
    return acc ^ carry;
}

int main(void)
{
    uint64_t h = 0;
    for (uint64_t s = 0; s < 16; s++)
    {
        h = h * 1000003u + machine(40 + (int)s, s * 0x1234567ull + 1);
    }
    printf("t2 %llu %llu\n", (unsigned long long)h, (unsigned long long)g_sink);
    return 0;
}
