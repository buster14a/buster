// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int continue_after(int limit, int skip)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        step = 1;
        if (skip)
        {
            continue;
        }
        result += i;
    }
    return result;
}
