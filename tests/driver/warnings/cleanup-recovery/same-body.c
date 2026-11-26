// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void invalid_parameter(int value);
typedef long CleanupTarget;
void release_long(CleanupTarget* const value);

void tested(void)
{
    int a __attribute__((cleanup(invalid_parameter))) = 1;
    short b __attribute__((cleanup(release_long))) = 2;
    {
        char c __attribute__((cleanup(release_long))) = 3;
    }
    int d __attribute__((cleanup(release_long))) = 4;
}
