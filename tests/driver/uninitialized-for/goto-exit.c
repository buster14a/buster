// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int goto_exit(int limit, int jump)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        if (jump)
        {
            goto done;
        }
        step = 1;
        result += i;
    }
done:
    return result;
}
