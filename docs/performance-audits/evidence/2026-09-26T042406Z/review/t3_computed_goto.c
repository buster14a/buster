/* A computed-goto interpreter: every handler is an indirect-branch target,
   values live across the dispatch in homes, and handlers spill temporaries. */
#include <stdio.h>
#include <stdint.h>

static uint64_t g_sink;
__attribute__((noinline)) static uint64_t mix(uint64_t x)
{
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull; x ^= x >> 27;
    g_sink += x ^ (x << 1);
    return x;
}

#define T6(p, seed) \
    uint64_t p##0 = mix(seed), p##1 = mix(p##0 + 1), p##2 = mix(p##1 + 2), p##3 = mix(p##2 + 3), \
             p##4 = mix(p##3 + 4), p##5 = mix(p##4 + 5)
#define S6(p) (p##0 + 3 * p##1 + 5 * p##2 + 7 * p##3 + 11 * p##4 + 13 * p##5)

__attribute__((noinline)) uint64_t run(unsigned char const* code, int length, uint64_t seed)
{
    static void* const table[] = {&&op_add, &&op_mix, &&op_spill, &&op_jump, &&op_keys, &&op_end};
    T6(k, seed);
    uint64_t acc = seed, reg = 1;
    int pc = 0;
    int budget = 200;
#define DISPATCH() do { if (--budget <= 0 || pc >= length) goto op_end; goto *table[code[pc++] % 6]; } while (0)
    DISPATCH();
op_add:
    acc += reg + k0;
    DISPATCH();
op_mix:
    reg = mix(acc ^ reg);
    DISPATCH();
op_spill:
    {
        T6(a, acc);
        T6(b, a5 ^ reg);
        acc ^= S6(a) - S6(b);
        reg += b1;
    }
    DISPATCH();
op_jump:
    pc = (int)((acc ^ k1) % (uint64_t)length);
    DISPATCH();
op_keys:
    acc = mix(acc + k2 + k3 * k4 + k5);
    DISPATCH();
op_end:
    return acc ^ reg;
#undef DISPATCH
}

int main(void)
{
    unsigned char code[64];
    for (int i = 0; i < 64; i++)
    {
        code[i] = (unsigned char)((i * 37 + 11) ^ (i >> 2));
    }
    uint64_t h = 0;
    for (int s = 0; s < 20; s++)
    {
        h = h * 1000003u + run(code + (s % 7), 40 + s, (uint64_t)s * 77 + 5);
    }
    printf("t3 %llu %llu\n", (unsigned long long)h, (unsigned long long)g_sink);
    return 0;
}
