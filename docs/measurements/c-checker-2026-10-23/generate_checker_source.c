// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original frozen source fixtures for the checker handoff measurement.
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    const unsigned count = argc == 2 ? (unsigned)strtoul(argv[1], 0, 10) : 1;
    puts("typedef unsigned long Word;\nstatic Word seed = 7;");
    for (unsigned i = 0; i < count; i += 1)
    {
        printf("Word f%u(Word a, Word b) { Word x = a + %uu; { Word a = b ^ %uu; x = x + a; } if (x < b) x = x + seed; return (x * 3u) ^ (b >> 1); }\n", i, i + 1, i + 3);
    }
    return ferror(stdout) != 0;
}
