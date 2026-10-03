// Timer + independent rank/payload/canary oracle. select.c is a separate
// translation unit and LTO is disabled, so every measured call is opaque.
#define _POSIX_C_SOURCE 200809L
#include "probe.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct GuardedHeap GuardedHeap;
struct GuardedHeap
{
    u64 before[4];
    MachineQualityInterval values[PROBE_LIMIT];
    u64 after[4];
};

static MachineQualityInterval input[PROBE_MAXIMUM_INPUT];
static GuardedHeap heap;
static volatile u64 observed_checksum;

static int verify(u32 population, u32 count, u64* checksum)
{
    u32 expected_count = population < PROBE_LIMIT ? population : PROBE_LIMIT;
    unsigned char seen[PROBE_MAXIMUM_INPUT] = {0};
    int ok = count == expected_count;
    u64 hash = 0;
    for (u32 index = 0; index < 4; index += 1)
    {
        ok = ok && heap.before[index] == UINT64_C(0xa917f0658823dcab) + index &&
             heap.after[index] == UINT64_C(0x753e98023bd1af07) + index;
    }
    for (u32 index = 0; index < count && count <= PROBE_LIMIT; index += 1)
    {
        MachineQualityInterval value = heap.values[index];
        u32 position = population;
        for (u32 probe = 0; probe < population; probe += 1)
        {
            if (input[probe].virtual_register == value.virtual_register) position = probe;
        }
        if (position < population)
        {
            MachineQualityInterval expected = input[position];
            ok = ok && !seen[position] && value.weight == expected.weight && value.start == expected.start &&
                 value.end == expected.end && value.marginal == expected.marginal;
            seen[position] = 1;
#if PROBE_CANDIDATE
            // Count superior entries directly; this oracle never uses a heap.
            u32 rank = 0;
            for (u32 other = 0; other < population; other += 1)
            {
                rank += input[other].weight > expected.weight ||
                        (input[other].weight == expected.weight && input[other].virtual_register < expected.virtual_register);
            }
            ok = ok && rank < expected_count;
#else
            ok = ok && position < expected_count;
#endif
            if (index)
            {
                MachineQualityInterval parent = heap.values[(index - 1) / 2];
                ok = ok && parent.weight >= value.weight;
#if PROBE_CANDIDATE
                ok = ok && (parent.weight != value.weight || parent.virtual_register < value.virtual_register);
#endif
            }
            // Commutative set/payload checksum: no dependence on heap shape.
            hash += (value.weight ^ ((u64)value.virtual_register << 32) ^ value.start ^
                     ((u64)value.end << 17) ^ value.marginal) * UINT64_C(0xd6e8feb86659fd93);
        }
        else ok = 0;
    }
    *checksum = hash;
    return ok;
}

int main(int argc, char** argv)
{
    int ok = argc == 3;
    u32 population = 0;
    unsigned shape = 0;
    if (ok)
    {
        population = !strcmp(argv[1], "16") ? 16 : !strcmp(argv[1], "4096") ? 4096 :
                     !strcmp(argv[1], "4099") ? 4099 : !strcmp(argv[1], "8192") ? 8192 : 0;
        shape = !strcmp(argv[2], "equal") ? 0 : !strcmp(argv[2], "ascending") ? 1 : !strcmp(argv[2], "descending") ? 2 : 3;
        ok = population && shape < 3;
    }
    if (ok)
    {
        for (u32 index = 0; index < population; index += 1)
        {
            input[index] = (MachineQualityInterval){
                .weight = (UINT64_C(1) << 40) + (shape == 1 ? index + 1 : shape == 2 ? population - index : 7),
                .virtual_register = (index * 4051u + 11u) % population,
                .start = index * 3,
                .end = index * 3 + 2,
                .marginal = index & 1u,
            };
        }
        for (u32 index = 0; index < 4; index += 1)
        {
            heap.before[index] = UINT64_C(0xa917f0658823dcab) + index;
            heap.after[index] = UINT64_C(0x753e98023bd1af07) + index;
        }
        u64 first_checksum = 0;
        u32 count = quality313_select(input, population, heap.values);
        ok = verify(population, count, &first_checksum);
        if (ok)
        {
            struct timespec before;
            struct timespec after;
            ok = clock_gettime(CLOCK_MONOTONIC, &before) == 0;
            if (ok)
            {
                for (u32 operation = 0; operation < PROBE_OPERATIONS; operation += 1)
                {
                    count = quality313_select(input, population, heap.values);
                }
                ok = clock_gettime(CLOCK_MONOTONIC, &after) == 0;
            }
            if (ok)
            {
                int64_t elapsed = (int64_t)(after.tv_sec - before.tv_sec) * INT64_C(1000000000) + after.tv_nsec - before.tv_nsec;
                u64 checksum = 0;
                ok = elapsed > 0 && verify(population, count, &checksum) && checksum == first_checksum;
                observed_checksum = checksum;
                if (ok) printf("QUALITY313_ADMISSION population=%u count=%u operations=%u elapsed_ns=%" PRId64
                               " checksum=%016" PRIx64 " oracle=pass\n", population, count, PROBE_OPERATIONS, elapsed, observed_checksum);
            }
        }
    }
    if (!ok) fprintf(stderr, "quality313 micro rejected: argument/oracle/canary/timer failure\n");
    return ok ? 0 : 1;
}
