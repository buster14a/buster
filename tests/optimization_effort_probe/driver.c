/* Independent host-compiler harness; Buster compiles only the fixture object. */
#include <stdio.h>
#include <stdint.h>

extern unsigned long long probe(unsigned long long x, unsigned n);

int main(void)
{
    static unsigned const trip_counts[] = {0, 1, 2, 7, 8, 31};
    uint64_t state = UINT64_C(0x0123456789abcdef);
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (unsigned i = 0; i < 64; i += 1)
    {
        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        for (unsigned j = 0; j < sizeof(trip_counts) / sizeof(trip_counts[0]); j += 1)
        {
            uint64_t value = probe(state, trip_counts[j]);
            hash = (hash ^ value) * UINT64_C(0x100000001b3);
            printf("%016llx\n", (unsigned long long)value);
        }
    }
    printf("hash=%016llx\n", (unsigned long long)hash);
    return 0;
}
