// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Build-only adapter for the recorded Clang 18 cloud host; never use it as a predefines or runtime oracle.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char **args = calloc((size_t)argc + 3, sizeof(*args));
    if (args == NULL)
    {
        perror("calloc");
    }
    else
    {
        args[0] = "/usr/lib/llvm-18/bin/clang";
        for (int i = 1; i < argc; i += 1)
        {
            args[i] = argv[i];
        }
        args[argc] = "-mmwaitx";
        args[argc + 1] = "-Wno-bitwise-instead-of-logical";
        execv(args[0], args);
        perror("execv");
        free(args);
    }
    return 1;
}
