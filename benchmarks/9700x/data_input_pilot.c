// Pilot for direct-workload input data (#2769): reads the harness-provided
// input.data (data_input_pilot.data, 1 MiB of a xorshift64 stream), checks it
// word for word against the regenerated stream, and reports a checksum.
// Not meant to merge; it exists to exercise the .data path on the 9700X once.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DATA_BYTES (1u << 20)
#define WORD_COUNT (DATA_BYTES / 8u)
#define REPEAT_COUNT 64u

static unsigned char data[DATA_BYTES + 1u];

int main(void)
{
    int status = EXIT_FAILURE;
    FILE *file = fopen("input.data", "rb");
    size_t size = 0;
    if (file)
    {
        size = fread(data, 1, sizeof(data), file);
        fclose(file);
    }
    if (size == DATA_BYTES)
    {
        uint64_t state = UINT64_C(0x9E3779B97F4A7C15);
        uint64_t mismatches = 0;
        for (uint32_t i = 0; i < WORD_COUNT; i += 1)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            uint64_t word = 0;
            for (uint32_t byte = 0; byte < 8u; byte += 1)
            {
                word |= (uint64_t)data[i * 8u + byte] << (byte * 8u);
            }
            mismatches += word != state;
        }
        uint64_t hash = UINT64_C(0xCBF29CE484222325);
        for (uint32_t repeat = 0; repeat < REPEAT_COUNT; repeat += 1)
        {
            for (uint32_t i = 0; i < DATA_BYTES; i += 1)
            {
                hash = (hash ^ data[i]) * UINT64_C(0x100000001B3);
            }
        }
        printf("input.data bytes %zu mismatches %llu fnv1a x%u %016llx\n", size, (unsigned long long)mismatches, REPEAT_COUNT, (unsigned long long)hash);
        status = mismatches == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    else
    {
        printf("input.data missing or wrong size: %zu bytes\n", size);
    }
    return status;
}
