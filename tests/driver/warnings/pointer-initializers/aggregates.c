// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
const char fixed[2] = "a";
char *array[2] = { fixed, fixed };
struct S { char *p[2]; char *q; };
struct S object = { { fixed, fixed }, .q = fixed };
char *scalar = { fixed };
void use(void)
{
    static char *local[1] = { fixed };
    (void)local;
}
