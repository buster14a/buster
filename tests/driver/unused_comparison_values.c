// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

enum { ComparisonConstant = 7 };
int comparison_function(void);
struct ComparisonRecord
{
    int value;
    const int constant;
    unsigned bits : 3;
};

void unused_comparison_values(int value, double real, int* pointer, const int* constant_pointer,
                              int (*function)(void), struct ComparisonRecord* record)
{
    const int constant = 7;
    int* const fixed_pointer = pointer;
    int array[2];
    7 == 8;
    7 != 8;
    ComparisonConstant == value;
    sizeof(int) == value;
    (int)value == 8;
    (value + 1) == 8;
    (value ? value : constant) == 8;
    (comparison_function(), value) == 8;
    comparison_function() == value;
    comparison_function == function;
    value == 8;
    constant == 8;
    ((value)) == 8;
    *pointer == 8;
    record->value == 8;
    record->constant == 8;
    record->bits == 3;
    array == pointer;
    fixed_pointer == constant_pointer;
    real == 8;
    value != 8;
    real != 8;
    pointer != constant_pointer;
    value < 8;
}
