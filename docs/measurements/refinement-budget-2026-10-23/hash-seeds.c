// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Repeat the hash equality and distribution checks for seeds 1 through 128; arguments run the full refinement suite.

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#include "buster/compiler/c_lexer.c"
#include "buster/compiler/lane.c"
#include "buster/compiler/c_intern.c"
#include "buster/compiler/c_refine.c"
#include "buster/compiler/c_refine_test.c"

int main(int argc, char** argv)
{
    int result = 0;
    if (argc > 1)
    {
        result = run_refinement_tests(argc - 1, argv + 1);
    }
    else
    {
        for (uint64_t seed = 1; seed <= 128; seed += 1)
        {
            uint64_t random = seed;
            printf("seed %lu\n", (unsigned long)seed);
            bool passed = refine_check_hash(&random);
            passed &= refine_check_long_hashes(&random);
            passed &= refine_check_aligned_flips(&random);
            result |= !passed;
        }
    }
    return result;
}
