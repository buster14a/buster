// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include <stdio.h>
int main(void)
{
    fputs("deliberate streaming diagnostic perf failure\n", stderr);
    return 73;
}
