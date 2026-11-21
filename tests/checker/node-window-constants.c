// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define MASK (0x20 | 7)
enum { COUNT = 2 + 3 };
_Static_assert(COUNT == 5, "constant");
extern int consume(int);
int window_constants(int n, int *v)
{
    int ordinary = n + (3 + 4);
    v[(8 - 1) & 3] = consume(MASK) + ordinary;
    if (n && (1 << 3)) ordinary += 5;
    return ordinary + (10 - 2);
}
