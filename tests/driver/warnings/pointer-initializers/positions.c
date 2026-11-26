// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
const char fixed[2];
const char *cp;
#define KEEP(value) (value)
void use(void)
{
    char *parenthesized[2] = { (cp), ((fixed)) };
    char *arithmetic[1] = { fixed + 1 };
    char *expansion[2] = { KEEP(cp), KEEP(fixed) };
    (void)parenthesized;
    (void)arithmetic;
    (void)expansion;
}
