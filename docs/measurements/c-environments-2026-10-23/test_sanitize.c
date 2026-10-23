// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original narrow sanitizer entry point; it runs the unchanged production test commands without driver/benchmark
// dispatch. Compile on the approved 9700X under its per-run shared lock, then take that lock separately per test.
#define BUSTER_UNIT 99
#include "buster/compiler/main.c"

int main(int argc, char** argv)
{
    int result = 1;
    if (argc >= 2 && strcmp(argv[1], "test-environments") == 0)
    {
        result = run_c_environment_tests(argc - 2, argv + 2);
    }
    else if (argc >= 2 && strcmp(argv[1], "test-checker") == 0)
    {
        result = run_checker_tests(argc - 2, argv + 2);
    }
    else
    {
        fprintf(stderr, "usage: environment-sanitize test-environments|test-checker [--seed N]\n");
    }
    return result;
}
