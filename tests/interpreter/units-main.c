// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#include "units.h"
#include <stdio.h>
static long local = 17;
static long helper(void)
{
    return local;
}
__attribute__((weak)) int selected(void)
{
    return 1;
}
int main(void)
{
    UnitPair input = { 3, 5 };
    const UnitPair result = operation(input);
    const int passed = input.first == 3 && input.second == 5 && result.first == 32 && result.second == 46 && helper() == 17 && shared == 42 && selected() == 2;
    printf("typed units: %d\n", passed);
    return passed ? 0 : 1;
}
