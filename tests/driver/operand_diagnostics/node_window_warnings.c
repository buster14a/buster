// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wunused-but-set-parameter"
int mixed(int branch, int overwritten)
{
    int unread;
    overwritten = 3;
    if (branch)
    {
        return 18 + 24;
    }
    return unread + 1;
}

#pragma clang diagnostic pop

int conditional(int condition)
{
    if (condition)
    {
        return 2;
    }
    return 4 / 0;
}

int loop(int bound)
{
    int i = 0;
    for (; i < bound;)
    {
        (void)(bound + 1);
    }
    return 7;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdivision-by-zero"
int muted(void) { return 1 / 0; }
#pragma clang diagnostic pop

int restored(void) { return 3 / 0; }
