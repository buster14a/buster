// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define DEAD 21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36
#define LIVE 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16

static int dead(int i)
{
    int a[64] = { DEAD, DEAD, DEAD, DEAD };
    return a[i];
}

int used(int i)
{
    int a[64] = { LIVE, LIVE, LIVE, LIVE };
    return a[i];
}
