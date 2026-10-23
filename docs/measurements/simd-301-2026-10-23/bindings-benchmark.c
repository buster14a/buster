// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Standalone entry point for the existing binding benchmark and regression tests.
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "buster/compiler/lane.c"
#include "buster/compiler/c_bindings.c"
#if BUSTER_BINDINGS_TESTS
#include "buster/compiler/c_bindings_test.c"
#endif
#include "buster/compiler/c_bindings_benchmark.c"

int main(int argc, char** argv)
{
    int result;
#if BUSTER_BINDINGS_TESTS
    if (argc > 1 && strcmp(argv[1], "test") == 0)
    {
        result = run_c_bindings_tests(argc - 2, argv + 2);
    }
    else
#endif
    {
        result = run_c_bindings_benchmark(argc - 1, argv + 1);
    }
    return result;
}
