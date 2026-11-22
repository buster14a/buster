// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int count_path(const char* path)
{
    const char* p;
    const char* next;
    int count = 0;
    for (p = path; *p; p = next)
    {
        next = p + 1;
        count += 1;
    }
    return count;
}
