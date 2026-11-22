// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int shadow_unwritten(int limit)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += /* uninitialized */step)
    {
        int step = 1;
        result += step;
    }
    return result;
}
