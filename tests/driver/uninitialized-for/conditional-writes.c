// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int conditional_writes(int limit, int choice)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        result += choice ? (step = 1) : (step = 2);
    }
    return result;
}
