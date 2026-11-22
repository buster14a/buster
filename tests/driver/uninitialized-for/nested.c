// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int nested(int limit)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        int next;
        for (int j = 0; j < 3; j = next)
        {
            next = j + 1;
            result += j;
        }
        step = 1;
    }
    return result;
}
