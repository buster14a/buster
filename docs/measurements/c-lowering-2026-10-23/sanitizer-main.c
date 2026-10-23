// Original focused test entry point for issue #129; production integration is in main.c.
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#include "buster/compiler/lane.c"
#include "buster/compiler/c_lowering.c"
#include "buster/compiler/c_lowering_test.c"
#include "buster/compiler/c_lowering_benchmark.c"

int main(int argc, char** argv)
{
    int result;
    if (argc > 1 && strcmp(argv[1], "benchmark") == 0)
    {
        result = run_c_lowering_benchmark(argc - 2, argv + 2);
    }
    else
    {
        result = run_c_lowering_tests(argc - 1, argv + 1);
    }
    return result;
}
