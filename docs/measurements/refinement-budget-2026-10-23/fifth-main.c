// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include "buster/compiler/c_preprocessor_benchmark.c"
#include "buster/compiler/c_refine_test.c"
#include "screen-setup.c"

int main(int argc, char** argv)
{
    int result;
    if (argc > 1 && strcmp(argv[1], "--test") == 0)
    {
        result = run_refinement_tests(argc - 2, argv + 2);
    }
    else if (argc > 1 && strcmp(argv[1], "--setup") == 0)
    {
        result = screen_setup(argc - 1, argv + 1);
    }
    else
    {
        result = run_preprocessor_benchmark(argc, argv);
    }
    return result;
}
