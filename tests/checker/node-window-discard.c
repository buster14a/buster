// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define IGNORE(n) ((void)((n)))
void mixed_discards(int x)
{
    (void)0;
    ((void)7);
    (void)((3));
    IGNORE(9);
    (void)x;
    (void)(x++);
    (void)(x, 8);
    if (x) (void)4;
}
