// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int uninitialized_condition(void)
{
    int value;
    if (value == 1)
        return 3;
    return 5;
}

int uninitialized_pair(void)
{
    int left;
    int right;
    if (left < right)
        return 2;
    return 4;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
int muted_condition(void)
{
    int x;
    while (x != 4)
        return 1;
    return 2;
}
#pragma clang diagnostic pop
