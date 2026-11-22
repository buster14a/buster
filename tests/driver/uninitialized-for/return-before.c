// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int return_before(int limit, int stop)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        if (stop)
        {
            return result;
        }
        step = 1;
        result += i;
    }
    return result;
}
