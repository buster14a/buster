// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
const char *cp;
const char fixed[2] = "a";
char *e[1] = { cp };
char *f[1] = { fixed };
struct S { char *m; };
struct S s = { fixed };
void take(int *, char *);
void use(int *p)
{
    char *h = { fixed };
    char *g[1] = { cp };
    char *u = fixed;
    take(p, fixed);
    take(p, cp);
    (void)h;
    (void)g;
    (void)u;
}
