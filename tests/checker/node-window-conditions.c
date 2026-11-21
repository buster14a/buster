// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int predicate(int);

int compare_locals(int a, int b, volatile int *p)
{
    int result = 7;
    if (a == b) result += 1;
    if (4 != a) result += 2;
    if (b < 9) result += 3;
    if (a <= 12) result += 4;
    if (6 > b) result += 5;
    while (b >= 10) --b;
    if (*p == a) result += 6;
    if (predicate(a) == b) result += 7;
    if (a == a) result += 8;
    if (a == 2147483647) result += 9;
    if (({ result = b; a < b; })) result += 10;
    return result + a + b;
}
