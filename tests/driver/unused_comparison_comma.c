// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int comparison_source(void);

int unused_comparison_comma(int value, double real, int* pointer)
{
    const int constant = 7;
    int result = 0;
    result += (7 == 8, 1);
    result += (7 != 8, 1);
    result += ((int)value == 8, 1);
    result += ((value ? value : constant) == 8, 1);
    result += (comparison_source() == value, 1);
    result += (value == 8, 1);
    result += (constant == 8, 1);
    result += (*pointer == 8, 1);
    result += (real != 8, 1);
    result += (value < 8, 1);
    return result;
}
