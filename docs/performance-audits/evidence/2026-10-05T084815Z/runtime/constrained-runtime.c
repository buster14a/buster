#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

__attribute__((noinline)) static uint64_t shifted(uint64_t x, unsigned int n)
{
    unsigned int count = n & 63u;
    uint64_t result = (x << count) ^ (x >> ((63u - count) & 63u));
    return result;
}

__attribute__((noinline)) static uint64_t quotient_mix(uint64_t x, uint64_t y)
{
    uint64_t divisor = y | UINT64_C(1);
    uint64_t result = (x / divisor) ^ ((x % divisor) << 7u);
    return result;
}

__attribute__((noinline)) static uint64_t called(uint64_t x, uint64_t y)
{
    uint64_t result = shifted(x + UINT64_C(0x91), (unsigned int)y) + quotient_mix(x ^ y, y);
    return result;
}

__attribute__((noinline)) static uint64_t live_across_call(uint64_t x, uint64_t y)
{
    uint64_t before = shifted(x, (unsigned int)y + 9u);
    uint64_t after = called(y, x);
    uint64_t result = before ^ shifted(after, (unsigned int)(x >> 19u));
    return result;
}

int main(void)
{
    uint64_t digest = UINT64_C(0x9e3779b97f4a7c15);
    uint64_t state = UINT64_C(0x123456789abcdef0);
    for (unsigned int i = 0; i < 512u; i += 1u)
    {
        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        uint64_t denominator = (state >> 32u) | UINT64_C(1);
        digest ^= shifted(state, i) + quotient_mix(state ^ digest, denominator);
        digest = live_across_call(digest, state) + (digest << 3u);
    }
    printf("%016" PRIx64 "\n", digest);
    return 0;
}
