// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int condition_first(void)
{
    int step;
    int result = 0;
    for (int i = 0; i < /* uninitialized */step; i += step)
    {
        step = 1;
        result += i;
    }
    return result;
}
