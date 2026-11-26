// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
char plain[2];
const char fixed[2];
const char *added[1] = { plain };
const char *kept[1] = { fixed };
char *explicit[1] = { (char *)fixed };
char *zero[1] = { 0 };
void take(const char *);
void use(void)
{
    take(plain);
    take(fixed);
    const char *local = { fixed };
    (void)local;
}
