// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int break_after_loop(int limit, int stop)
{
    int step;
    for (int i = 0; i < limit; i += step)
    {
        if (stop)
        {
            break;
        }
        step = 1;
    }
    return /* uninitialized */step;
}
