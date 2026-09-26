/* A returns_twice function whose name is not in ir_call_returns_twice's
   list. GNU C honors the attribute; buster ignores it, so the function is
   "certified" free of returns-twice calls and frame storage may be shared.
   a0..a11 are never modified after my_setjmp, so C requires them to keep
   their values after longjmp. Their homes are dead on the setjmp()==0 path
   (which ends in a longjmp), so any sharing with that path's temporaries
   clobbers them. my_setjmp is `jmp _setjmp` (my_setjmp.s). */
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>

extern int my_setjmp(jmp_buf) __attribute__((returns_twice));

static jmp_buf jb;
static uint64_t g_sink;
__attribute__((noinline)) static uint64_t mix(uint64_t x)
{
    x ^= x >> 31; x *= 0x9E3779B97F4A7C15ull; x ^= x >> 29;
    g_sink += x;
    return x;
}
__attribute__((noinline)) static void boom(uint64_t x)
{
    g_sink ^= x;
    longjmp(jb, 1);
}

#define T12(p, seed) \
    uint64_t p##0 = mix(seed), p##1 = mix(p##0 + 1), p##2 = mix(p##1 + 2), p##3 = mix(p##2 + 3), \
             p##4 = mix(p##3 + 4), p##5 = mix(p##4 + 5), p##6 = mix(p##5 + 6), p##7 = mix(p##6 + 7), \
             p##8 = mix(p##7 + 8), p##9 = mix(p##8 + 9), p##10 = mix(p##9 + 10), p##11 = mix(p##10 + 11)
#define S12(p) (p##0 + 3 * p##1 + 5 * p##2 + 7 * p##3 + 11 * p##4 + 13 * p##5 + 17 * p##6 + 19 * p##7 + \
                23 * p##8 + 29 * p##9 + 31 * p##10 + 37 * p##11)

__attribute__((noinline)) uint64_t f(uint64_t seed)
{
    T12(a, seed);
    if (my_setjmp(jb) == 0)
    {
        T12(t, seed ^ 0x5555);
        T12(u, t11);
        boom(S12(t) ^ S12(u));
        return 0;
    }
    return S12(a);
}

/* Same, with the longjmp path laid out after the rows that read a*, so a
   row-interval hull of a*'s homes does not cover the path's temporaries. */
__attribute__((noinline)) uint64_t g(uint64_t seed)
{
    T12(a, seed);
    if (my_setjmp(jb) != 0)
    {
        return S12(a);
    }
    T12(t, seed ^ 0x5555);
    T12(u, t11);
    boom(S12(t) ^ S12(u));
    return 0;
}

int main(void)
{
    uint64_t h = 0;
    for (uint64_t s = 1; s < 6; s++)
    {
        h = h * 1000003u + f(s);
        h = h * 1000003u + g(s + 17);
    }
    printf("t6 %llu\n", (unsigned long long)h);
    return 0;
}
