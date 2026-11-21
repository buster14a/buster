// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int count_components(const char* path)
{
    int i;
    int j;
    int count = 0;
    for (i = 0; path[i]; i = j + 1)
    {
        for (j = i; path[j] && path[j] != '.'; j += 1)
        {
        }
        count += 1;
        if (!path[j])
        {
            break;
        }
    }
    return count;
}
