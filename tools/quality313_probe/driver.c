// Independent initialized-input runtime oracle for #313's generated C shapes.
// The measured span covers probe calls only; oracle work stays outside it.
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define PROBE_VALUES 4097u
#define PROBE_REPEATS 256u

extern void quality313_probe(volatile unsigned long long const* input, volatile unsigned long long* output);

static volatile unsigned long long input[16];
static volatile unsigned long long output[PROBE_VALUES];

static void initialize(unsigned seed)
{
    for (unsigned index = 0; index < 16; index += 1)
    {
        input[index] = UINT64_C(0xd6e8feb86659fd93) * (seed * 17u + index + 1u);
    }
    for (unsigned index = 0; index < PROBE_VALUES; index += 1) output[index] = UINT64_MAX;
}

static int check_output(uint64_t* checksum)
{
    int ok = 1;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned index = 0; index < PROBE_VALUES; index += 1)
    {
        uint64_t expected = input[index % 16] ^ (index * UINT64_C(7) + UINT64_C(123456789));
        uint64_t actual = output[index];
        if (actual != expected)
        {
            fprintf(stderr, "QUALITY313_MISMATCH index=%u actual=%" PRIu64 " expected=%" PRIu64 "\n", index, actual, expected);
            ok = 0;
        }
        hash = (hash ^ actual) * UINT64_C(1099511628211);
    }
    *checksum = hash;
    return ok;
}

int main(void)
{
    int ok = 1;
    uint64_t checksum = 0;
    for (unsigned seed = 1; seed <= 4 && ok; seed += 1)
    {
        initialize(seed);
        quality313_probe(input, output);
        ok = check_output(&checksum);
    }
    if (ok)
    {
        struct timespec before;
        struct timespec after;
        quality313_probe(input, output);
        ok = clock_gettime(CLOCK_MONOTONIC, &before) == 0;
        if (ok)
        {
            for (unsigned repeat = 0; repeat < PROBE_REPEATS; repeat += 1) quality313_probe(input, output);
            ok = clock_gettime(CLOCK_MONOTONIC, &after) == 0;
        }
        if (ok)
        {
            int64_t elapsed = (int64_t)(after.tv_sec - before.tv_sec) * INT64_C(1000000000) + after.tv_nsec - before.tv_nsec;
            ok = elapsed > 0 && check_output(&checksum);
            if (ok)
            {
                printf("QUALITY313_ORACLE checks=%u seeds=4 checksum=%016" PRIx64 "\n", PROBE_VALUES, checksum);
                printf("QUALITY313_RUNTIME calls=%u elapsed_ns=%" PRId64 "\n", PROBE_REPEATS, elapsed);
            }
        }
    }
    return ok ? 0 : 1;
}
