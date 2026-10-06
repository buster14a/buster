/* First workload for the direct 9700X path (#2704): sort a fixed
 * pseudo-random array several times and check the result. It prints one
 * self-check line and exits nonzero when the order or the checksum is wrong. */
#include <stdint.h>
#include <stdio.h>

#define ELEMENT_COUNT (1u << 18)
#define BATCH_COUNT 16u

static uint32_t values[ELEMENT_COUNT];
static uint32_t scratch[ELEMENT_COUNT];

static uint32_t next_random(uint64_t* state)
{
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(*state >> 32);
}

/* Least-significant-digit radix sort over four bytes. */
static void sort_values(void)
{
    uint32_t* source = values;
    uint32_t* target = scratch;
    for (uint32_t shift = 0; shift < 32; shift += 8)
    {
        uint32_t counts[257] = {0};
        for (uint32_t i = 0; i < ELEMENT_COUNT; i += 1)
        {
            counts[((source[i] >> shift) & 0xffu) + 1] += 1;
        }
        for (uint32_t i = 0; i < 256; i += 1)
        {
            counts[i + 1] += counts[i];
        }
        for (uint32_t i = 0; i < ELEMENT_COUNT; i += 1)
        {
            uint32_t bucket = (source[i] >> shift) & 0xffu;
            target[counts[bucket]] = source[i];
            counts[bucket] += 1;
        }
        uint32_t* swap = source;
        source = target;
        target = swap;
    }
}

int main(void)
{
    uint64_t state = 0x9700u;
    uint64_t checksum = 0xcbf29ce484222325ull;
    uint64_t input_sum = 0;
    uint64_t output_sum = 0;
    int ordered = 1;
    for (uint32_t batch = 0; batch < BATCH_COUNT; batch += 1)
    {
        for (uint32_t i = 0; i < ELEMENT_COUNT; i += 1)
        {
            values[i] = next_random(&state);
            input_sum += values[i];
        }
        sort_values();
        for (uint32_t i = 0; i < ELEMENT_COUNT; i += 1)
        {
            if (i > 0 && values[i - 1] > values[i])
            {
                ordered = 0;
            }
            output_sum += values[i];
            checksum = (checksum ^ values[i]) * 0x100000001b3ull;
        }
    }
    int status = ordered && input_sum == output_sum ? 0 : 1;
    printf("sort-check %s elements=%u batches=%u checksum=%016llx\n", status == 0 ? "ok" : "FAILED",
           ELEMENT_COUNT, BATCH_COUNT, (unsigned long long)checksum);
    return status;
}
