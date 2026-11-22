// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int known_zero(void)
{
    int step;
    int i;
    for (i = 0; 0; i += step)
    {
        step = 1;
    }
    return /* uninitialized */step + i;
}
