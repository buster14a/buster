/* Debug-location probe: x's home is dead after its last read in the entry
   block, and loop temporaries may reuse that storage. Break at mark() and
   print x on each iteration; x must stay 0x9e3779b97f4a7c15-derived value. */
#include <stdio.h>
#include <stdint.h>

static uint64_t g;
__attribute__((noinline)) static uint64_t mix(uint64_t v)
{
    v ^= v >> 31; v *= 0x9E3779B97F4A7C15ull; g += v;
    return v;
}
__attribute__((noinline)) static void mark(int i)
{
    g += (uint64_t)i;
}

int main(void)
{
    uint64_t x = mix(1);
    uint64_t y = mix(x);
    g += x + y;
    for (int i = 0; i < 3; i++)
    {
        mark(i);
        uint64_t t = mix((uint64_t)i + 100);
        uint64_t u = mix(t);
        g += t + u;
    }
    printf("d1 %llu x=%llu\n", (unsigned long long)g, (unsigned long long)mix(1));
    return 0;
}
