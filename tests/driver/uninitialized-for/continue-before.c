// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int continue_before(int limit, int skip)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += /* uninitialized */step)
    {
        if (skip)
        {
            continue;
        }
        step = 1;
        result += i;
    }
    return result;
}
