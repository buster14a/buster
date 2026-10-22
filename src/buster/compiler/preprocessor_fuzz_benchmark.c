// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Provisional benchmark-preprocessor-fuzz command, independent of the preprocessor under development in #117.
// This measures the external clang test harness, including generation, processes, temporary I/O and cleanup.
// Every sample processes the same original family-0 cases (object macros and a shared generated scaffold);
// batching and separate processes must accept them
// all and return equal output byte totals. The self-tests check token identity on the complete generator.
// The sample implementation lives in preprocessor_fuzz_clang.c; main.c includes both. No compiler speed is measured.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>

#define BUSTER_PPF_BENCHMARK_PASSES 21
#define BUSTER_PPF_BENCHMARK_CASES 16

static double preprocessor_fuzz_benchmark_middle(const double* values)
{
    double ordered[BUSTER_PPF_BENCHMARK_PASSES];
    for (uint32_t index = 0; index < BUSTER_PPF_BENCHMARK_PASSES; index += 1)
    {
        uint32_t position = index;
        while (position != 0 && ordered[position - 1] > values[index])
        {
            ordered[position] = ordered[position - 1];
            position -= 1;
        }
        ordered[position] = values[index];
    }
    return ordered[BUSTER_PPF_BENCHMARK_PASSES / 2];
}

static int run_preprocessor_fuzz_benchmark(int argc, char** argv)
{
    const char* clang_path = "clang";
    uint64_t seed = 1;
    bool c23 = false;
    bool valid = true;
    for (int argument = 1; argument < argc; argument += 1)
    {
        if (strcmp(argv[argument], "--clang") == 0 && argument + 1 < argc)
        {
            argument += 1;
            clang_path = argv[argument];
        }
        else if (strcmp(argv[argument], "--std") == 0 && argument + 1 < argc)
        {
            argument += 1;
            c23 = strcmp(argv[argument], "c23") == 0;
            valid = valid && (c23 || strcmp(argv[argument], "c17") == 0);
        }
        else if (strcmp(argv[argument], "--seed") == 0 && argument + 1 < argc)
        {
            argument += 1;
            char* end = 0;
            errno = 0;
            seed = strtoull(argv[argument], &end, 10);
            valid = valid && argv[argument][0] >= '0' && argv[argument][0] <= '9' && errno == 0 && end != argv[argument] && *end == 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-preprocessor-fuzz [--clang PATH] [--std c17|c23] [--seed N]\n");
    }
    else if (preprocessor_fuzz_benchmark_version(clang_path))
    {
        double times[2][BUSTER_PPF_BENCHMARK_PASSES] = { 0 };
        uint64_t expected_bytes = 0;
        for (uint32_t pass = 0; valid && pass <= BUSTER_PPF_BENCHMARK_PASSES; pass += 1)
        {
            for (uint32_t turn = 0; valid && turn < 2; turn += 1)
            {
                const uint32_t variant = (pass + turn) % 2;
                uint64_t accepted = 0;
                uint64_t bytes = 0;
                struct timespec before = { 0 };
                struct timespec after = { 0 };
                valid = clock_gettime(CLOCK_MONOTONIC_RAW, &before) == 0;
                if (valid)
                {
                    valid = preprocessor_fuzz_benchmark_sample(clang_path, c23, seed, BUSTER_PPF_BENCHMARK_CASES, variant != 0, &accepted, &bytes);
                    valid = clock_gettime(CLOCK_MONOTONIC_RAW, &after) == 0 && valid;
                }
                if (pass == 0 && turn == 0)
                {
                    expected_bytes = bytes;
                }
                valid = valid && accepted == BUSTER_PPF_BENCHMARK_CASES && bytes == expected_bytes;
                if (valid && pass != 0)
                {
                    times[variant][pass - 1] = (double)(after.tv_sec - before.tv_sec) * 1e9 + (double)(after.tv_nsec - before.tv_nsec);
                }
            }
        }
        if (valid)
        {
            printf("preprocessor fuzz harness: %s, seed=%" PRIu64 ", %u accepted cases, 21 interleaved-pass medians\n",
                c23 ? "c23" : "c17", seed, BUSTER_PPF_BENCHMARK_CASES);
            for (uint32_t variant = 0; variant < 2; variant += 1)
            {
                const double ns = preprocessor_fuzz_benchmark_middle(times[variant]);
                printf("  %-10s %.3f ms/batch, %.1f cases/second\n", variant == 0 ? "batched" : "individual", ns / 1e6,
                    ns > 0 ? (double)BUSTER_PPF_BENCHMARK_CASES * 1e9 / ns : 0);
            }
            result = 0;
        }
        else
        {
            fprintf(stderr, "preprocessor fuzz harness benchmark failed its execution or output checks\n");
        }
    }
    return result;
}
