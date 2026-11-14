// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef int PrefixValue;
void prefix_values(void)
{
    __attribute__((suppress)) { }
    PrefixValue value = 1;
    (void)value;
}
int incomplete = ;
