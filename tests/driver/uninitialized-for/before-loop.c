// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int before_loop(int limit)
{
    int step;
    int result = /* uninitialized */step;
    for (int i = 0; i < limit; i += step)
    {
        step = 1;
    }
    return result;
}
