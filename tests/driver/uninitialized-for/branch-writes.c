// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int branch_writes(int limit, int choice)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        if (choice)
        {
            step = 1;
        }
        else
        {
            step = 2;
        }
        result += i;
    }
    return result;
}
