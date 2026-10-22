// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Complete integer-action batches, including all result stores. Synthetic inputs are generated once per process;
// 21 samples alternate the paths. --only isolates a path; --after-scalar MS conditions each vector sample with its
// scalar twin. Run separately locked processes on 9700X CPU 2. These are kernel timings, not C compile timings.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "buster/compiler/c_builtin.c"

#define BUSTER_CBUILTIN_BENCHMARK_COUNT 16384u
#define BUSTER_CBUILTIN_BENCHMARK_PASSES 21u

static uint64_t cbuiltin_benchmark_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

static __attribute__((noinline)) void cbuiltin_benchmark_repeat(CBuiltinIntegers input, CBuiltinIntegerResults output, uint32_t count, uint32_t repeats, bool scalar)
{
    if (scalar)
    {
        for (uint32_t i = 0; i < repeats; i += 1)
        {
            cbuiltin_fold_scalar(input, output, count);
            __asm__ volatile("" : : "r"(output.value) : "memory");
        }
    }
    else
    {
        for (uint32_t i = 0; i < repeats; i += 1)
        {
            cbuiltin_fold(input, output, count);
            __asm__ volatile("" : : "r"(output.value) : "memory");
        }
    }
}

static void cbuiltin_benchmark_sort(double* values)
{
    for (uint32_t i = 1; i < BUSTER_CBUILTIN_BENCHMARK_PASSES; i += 1)
    {
        const double value = values[i];
        uint32_t j = i;
        while (j != 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

int run_builtin_benchmark(int argc, char** argv)
{
    int only = -1;
    uint32_t after_ms = 0;
    bool passed = true;
    for (int i = 1; i < argc && passed; i += 1)
    {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            i += 1;
            only = strcmp(argv[i], "scalar") == 0 ? 0 : (strcmp(argv[i], "vector") == 0 ? 1 : -1);
            passed = only >= 0;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && i + 1 < argc)
        {
            char* end;
            i += 1;
            const unsigned long value = strtoul(argv[i], &end, 10);
            passed = *argv[i] != 0 && *end == 0 && value <= 1000;
            after_ms = passed ? (uint32_t)value : 0;
        }
        else
        {
            passed = false;
        }
    }
    const uint32_t count = BUSTER_CBUILTIN_BENCHMARK_COUNT;
    // Four input u32 columns, two input u64 columns, and two sets of u64/u32/u32 outputs.
    uint64_t* memory = passed ? malloc((size_t)count * 64) : 0;
    if (memory == 0)
    {
        fprintf(stderr, "benchmark-builtins: allocation failed or invalid options; [--only scalar|vector] [--after-scalar MS <= 1000]\n");
        passed = false;
    }
    if (passed)
    {
        uint32_t* ids = (uint32_t*)(memory + count * 2);
        uint32_t* at = ids + count;
        uint32_t* bt = at + count;
        uint32_t* rt = bt + count;
        CBuiltinIntegers input = { ids, memory, memory + count, at, bt, rt };
        CBuiltinIntegerResults output[2] =
        {
            { memory + count * 4, (uint32_t*)(memory + count * 5), (uint32_t*)(memory + count * 5) + count },
            { memory + count * 6, (uint32_t*)(memory + count * 7), (uint32_t*)(memory + count * 7) + count },
        };
        uint64_t state = 148;
        for (uint32_t i = 0; i < count; i += 1)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            memory[i] = state;
            memory[count + i] = (i & 1) ? state >> 32 : 3;
            at[i] = (i & 2) ? 64 : 192;
            bt[i] = (i & 4) ? 32 : 160;
            rt[i] = (i & 8) ? 64 : 160;
        }
        passed = cbuiltin_lookup("__builtin_bswap64", 17) == cbuiltin_lookup_scalar("__builtin_bswap64", 17);
        const char* const names[] = { "bit-mixed", "overflow-mixed", "all-mixed" };
        for (uint32_t shape = 0; shape < 3 && passed; shape += 1)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                ids[i] = shape == 0 ? 1 + i % 18 : (shape == 1 ? CBUILTIN_ADD_OVERFLOW + i % 3 : 1 + i % 21);
            }
            cbuiltin_fold_scalar(input, output[0], count);
            cbuiltin_fold(input, output[1], count);
            passed = memcmp(output[0].value, output[1].value, count * 8) == 0 && memcmp(output[0].status, output[1].status, count * 4) == 0 && memcmp(output[0].overflow, output[1].overflow, count * 4) == 0;
            uint32_t repeats[2] = { 1, 1 };
            for (uint32_t path = 0; path < 2 && passed; path += 1)
            {
                uint64_t elapsed = 0;
                while (elapsed < 2000000 && repeats[path] < (1u << 20))
                {
                    repeats[path] *= 2;
                    const uint64_t start = cbuiltin_benchmark_time();
                    cbuiltin_benchmark_repeat(input, output[path], count, repeats[path], path == 0);
                    elapsed = cbuiltin_benchmark_time() - start;
                }
            }
            double samples[2][BUSTER_CBUILTIN_BENCHMARK_PASSES];
            for (uint32_t pass = 0; pass < BUSTER_CBUILTIN_BENCHMARK_PASSES && passed; pass += 1)
            {
                for (uint32_t order = 0; order < 2; order += 1)
                {
                    const uint32_t path = order ^ (pass & 1);
                    if (only == -1 || only == (int)path)
                    {
                        if (path == 1 && after_ms != 0)
                        {
                            const uint64_t end = cbuiltin_benchmark_time() + (uint64_t)after_ms * 1000000;
                            while (cbuiltin_benchmark_time() < end)
                            {
                                cbuiltin_benchmark_repeat(input, output[0], count, 1, true);
                            }
                        }
                        const uint64_t start = cbuiltin_benchmark_time();
                        cbuiltin_benchmark_repeat(input, output[path], count, repeats[path], path == 0);
                        samples[path][pass] = (double)(cbuiltin_benchmark_time() - start) / ((double)count * repeats[path]);
                    }
                }
            }
            for (uint32_t path = 0; path < 2 && passed; path += 1)
            {
                if (only == -1 || only == (int)path)
                {
                    cbuiltin_benchmark_sort(samples[path]);
                    printf("%-14s %-6s %.3f ns/row [%.3f, %.3f] count=%u after-scalar=%u ms\n", names[shape], path == 0 ? "scalar" : "vector",
                        samples[path][10], samples[path][2], samples[path][18], count, after_ms);
                }
            }
        }
        if (!passed)
        {
            fprintf(stderr, "builtin benchmark: scalar/vector mismatch\n");
        }
    }
    free(memory);
    return passed ? 0 : 1;
}
