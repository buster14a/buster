// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The out-of-line definition of inline.c's again, which the linker takes.
int again(int n);

int again(int n)
{
    (void)n;
    return 1;
}
