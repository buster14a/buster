// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int inner_continue(int limit, int skip)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        for (int j = 0; j < 3; j += 1)
        {
            if (skip)
            {
                continue;
            }
            result += j;
        }
        step = 1;
    }
    return result;
}
