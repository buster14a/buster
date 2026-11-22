// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef int ComparisonVector __attribute__((ext_vector_type(4)));

void unused_comparison_categories(int value, int* pointer, ComparisonVector vector)
{
    (int){ 7 } == 8;
    (int[2]){ 7, 8 } == pointer;
    _Generic(0, int: value, default: 7) == 8;
    __builtin_choose_expr(1, value, 7) == 8;
    __builtin_choose_expr(0, value, 7) == 8;
    ({ value; }) == 8;
    vector.xy == 8;
    vector.xx == 8;
    vector.xx != 8;
}
