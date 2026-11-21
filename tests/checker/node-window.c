// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define NINE 9
int positive(void)
{
    return
        18
        +
        24;
}
int negative(void) { return 8 - 50; }
int negated(void) { return -42; }
int multiplied(void)
{
    return NINE
        *
        7;
}
int zero(void) { return 0; }
int literal(void) { return 4; }
int from_argument(int x) { return x + 7; }
long wider_return(void) { return 42; }
int grouped(void) { return (7 + 8); }
int suffixed(void) { return 7u + 8; }
int division(void) { return 42 / 7; }
int overflow(void) { return 999999999 * 7; }
int mixed(int x) { if (x) return x; return 1 + 4; }
int local_read(int x) { int y = x; return +y; }
