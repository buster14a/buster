// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int body_read(int limit)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        result += /* uninitialized */step;
        step = 1;
    }
    return result;
}
