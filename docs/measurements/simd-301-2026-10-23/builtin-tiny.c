// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The small-count check used for #301: 64 independent calls per batch count, all result columns checked against the
// scalar twin, then 21 samples calibrated to at least 2 ms. Select the implementation with -I<checkout>/src.
// builtin-tiny.txt records three processes of each build. These are synthetic action timings, not C compile times.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "buster/compiler/c_builtin.c"

static uint64_t tiny_clock(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

static __attribute__((noinline)) void tiny_repeat(CBuiltinIntegers input, CBuiltinIntegerResults output, uint32_t count, uint32_t groups, uint32_t repeats)
{
    for (uint32_t repeat = 0; repeat < repeats; repeat += 1)
    {
        for (uint32_t group = 0; group < groups; group += 1)
        {
            const uint32_t first = group * count;
            CBuiltinIntegers part =
            {
                input.id + first,
                input.a + first,
                input.b + first,
                input.a_type + first,
                input.b_type + first,
                input.result_type + first,
            };
            CBuiltinIntegerResults result =
            {
                output.value + first,
                output.overflow + first,
                output.status + first,
            };
            cbuiltin_fold(part, result, count);
            __asm__ volatile("" : : "r"(result.value) : "memory");
        }
    }
}

int main(void)
{
    const uint32_t capacity = 1024;
    const uint32_t groups = 64;
    const uint32_t counts[] =
    {
        1,
        7,
        8,
        9,
        16,
    };
    const char* const names[] =
    {
        "bits",
        "overflow",
        "mixed-invalid",
    };
    uint64_t* memory = malloc((size_t)capacity * 64);
    bool passed = memory != 0;
    if (passed)
    {
        uint32_t* ids = (uint32_t*)(memory + capacity * 2);
        uint32_t* at = ids + capacity;
        uint32_t* bt = at + capacity;
        uint32_t* rt = bt + capacity;
        CBuiltinIntegers input =
        {
            ids,
            memory,
            memory + capacity,
            at,
            bt,
            rt,
        };
        CBuiltinIntegerResults output[2] =
        {
            {
                memory + capacity * 4,
                (uint32_t*)(memory + capacity * 5),
                (uint32_t*)(memory + capacity * 5) + capacity,
            },
            {
                memory + capacity * 6,
                (uint32_t*)(memory + capacity * 7),
                (uint32_t*)(memory + capacity * 7) + capacity,
            },
        };
        passed = cbuiltin_lookup("__builtin_bswap64", 17) == cbuiltin_lookup_scalar("__builtin_bswap64", 17);
        for (uint32_t shape = 0; shape < 3 && passed; shape += 1)
        {
            for (uint32_t i = 0; i < capacity; i += 1)
            {
                ids[i] = shape == 0 ? 1 + i % 18 : (shape == 1 ? CBUILTIN_ADD_OVERFLOW + i % 3 : i % (CBUILTIN_COUNT + 2));
                memory[i] = UINT64_MAX - i;
                memory[capacity + i] = i % 3 == 0 ? UINT64_MAX : i;
                at[i] = (i & 2) ? 64 : 192;
                bt[i] = (i & 4) ? 32 : 160;
                rt[i] = (i & 8) ? 64 : 160;
            }
            for (uint32_t c = 0; c < 5 && passed; c += 1)
            {
                const uint32_t count = counts[c];
                const uint32_t rows = count * groups;
                cbuiltin_fold_scalar(input, output[0], rows);
                tiny_repeat(input, output[1], count, groups, 1);
                passed = memcmp(output[0].value, output[1].value, rows * 8) == 0 &&
                    memcmp(output[0].overflow, output[1].overflow, rows * 4) == 0 &&
                    memcmp(output[0].status, output[1].status, rows * 4) == 0;
                uint32_t repeats = 1;
                uint64_t elapsed = 0;
                while (elapsed < 2000000 && repeats < (1u << 20))
                {
                    repeats *= 2;
                    const uint64_t start = tiny_clock();
                    tiny_repeat(input, output[1], count, groups, repeats);
                    elapsed = tiny_clock() - start;
                }
                double samples[21];
                for (uint32_t pass = 0; pass < 21; pass += 1)
                {
                    const uint64_t start = tiny_clock();
                    tiny_repeat(input, output[1], count, groups, repeats);
                    samples[pass] = (double)(tiny_clock() - start) / ((uint64_t)rows * repeats);
                }
                for (uint32_t i = 1; i < 21; i += 1)
                {
                    const double value = samples[i];
                    uint32_t j = i;
                    while (j != 0 && samples[j - 1] > value)
                    {
                        samples[j] = samples[j - 1];
                        j -= 1;
                    }
                    samples[j] = value;
                }
                printf("%-13s count=%-2u %.3f ns/row [%.3f, %.3f] equality=%s\n", names[shape], count, samples[10], samples[2], samples[18], passed ? "pass" : "FAIL");
            }
        }
        free(memory);
    }
    return passed ? 0 : 1;
}
