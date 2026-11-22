// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int goto_bypass(int limit, int skip)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += /* uninitialized */step)
    {
        if (skip)
        {
            goto update;
        }
        step = 1;
    update:
        result += i;
    }
    return result;
}
