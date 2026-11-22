// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int or_left_write(int limit)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        result += (step = 1) || i;
    }
    return result;
}
