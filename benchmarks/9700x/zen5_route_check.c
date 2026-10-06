// Route check for the direct 9700X workload path (#2761): sorts a fixed
// pseudo-random array and verifies order and checksum, so a run's report
// carries the observed host line together with a self-checked result.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ROUTE_CHECK_COUNT (1u << 20)

static int route_check_compare(void const* left, void const* right)
{
    uint32_t a = *(uint32_t const*)left;
    uint32_t b = *(uint32_t const*)right;
    return (a > b) - (a < b);
}

int main(void)
{
    static uint32_t values[ROUTE_CHECK_COUNT];
    uint64_t state = 0x9e3779b97f4a7c15u;
    uint64_t before = 0;
    for (uint32_t i = 0; i < ROUTE_CHECK_COUNT; i += 1)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        values[i] = (uint32_t)state;
        before += values[i];
    }
    qsort(values, ROUTE_CHECK_COUNT, sizeof(values[0]), route_check_compare);
    uint64_t after = 0;
    int ordered = 1;
    for (uint32_t i = 0; i < ROUTE_CHECK_COUNT; i += 1)
    {
        after += values[i];
        ordered &= i == 0 || values[i - 1] <= values[i];
    }
    int passed = ordered && before == after;
    printf("self-check %s count=%u checksum=%llx\n", passed ? "ok" : "FAILED", ROUTE_CHECK_COUNT,
           (unsigned long long)after);
    return passed ? 0 : 1;
}
