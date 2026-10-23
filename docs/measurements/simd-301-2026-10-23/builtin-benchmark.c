// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone entry point for the existing builtin benchmark. Select the baseline or candidate with -I<checkout>/src;
// both implementation and benchmark come from that checkout. See builtins.txt and ../../c-builtins.md for results.
#include "buster/compiler/c_builtin.c"
#include "buster/compiler/c_builtin_benchmark.c"

int main(int argc, char** argv)
{
    return run_builtin_benchmark(argc, argv);
}
