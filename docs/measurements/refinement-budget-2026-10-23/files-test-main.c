// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#include "buster/compiler/c_preprocessor_benchmark.c"

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#define BUSTER_COMPILE_TIME_STRING_LENGTH(strlit) (BUSTER_ARRAY_LENGTH(strlit) - 1)
#define BUSTER_S8_INIT(strlit) {.pointer = (char*)(strlit), .length = BUSTER_COMPILE_TIME_STRING_LENGTH(strlit)}
#define BUSTER_S8(strlit) ((String8)BUSTER_S8_INIT(strlit))

#include "buster/compiler/preprocessor_fuzz.c"
#include "buster/compiler/preprocessor_fuzz_generate.c"
#include "buster/compiler/preprocessor_fuzz_clang.c"
#include "buster/compiler/preprocessor_fuzz_benchmark.c"
#include "buster/compiler/c_preprocessor_test.c"
#include "buster/compiler/c_predefines_test.c"
#include "buster/compiler/c_preprocessor_fuzz.c"
#include "buster/compiler/c_preprocessor_oracle.c"
#include "buster/compiler/c_files.c"
#include "buster/compiler/c_files_test.c"

int main(int argc, char** argv)
{
    int result;
    if (argc > 1 && (strcmp(argv[1], "--benchmark") == 0 || strcmp(argv[1], "benchmark-preprocessor") == 0))
    {
        result = run_preprocessor_benchmark(argc - 1, argv + 1);
    }
    else if (argc > 1 && strcmp(argv[1], "--fuzz-benchmark") == 0)
    {
        result = run_preprocessor_fuzz_benchmark(argc - 2, argv + 2);
    }
    else if (argc > 1 && (strcmp(argv[1], "--preprocessor") == 0 || strcmp(argv[1], "test-preprocessor") == 0))
    {
        result = run_preprocessor_tests(argc - 2, argv + 2);
    }
    else if (argc > 1 && strcmp(argv[1], "test-files") == 0)
    {
        result = run_files_tests(argc - 2, argv + 2);
    }
    else
    {
        result = run_files_tests(argc - 1, argv + 1);
    }
    return result;
}
