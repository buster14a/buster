// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Type-only operands retain their type spelling even when the selected value is a literal.
enum { Space = 2 };
struct Entry { int value; };
typedef int Count;
#define POINTER_SPACE [[clang::address_space(1)]]
_Static_assert(_Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 0) == 2, "pointer attribute");
_Static_assert(_Generic((int *[[clang::address_space(1 + 1)]])0, int *: 1, default: 0) == 2, "constant expression");
_Static_assert(_Generic((int *[[clang::address_space(Space)]])0, int *: 1, default: 0) == 2, "enumerator argument");
_Static_assert(_Generic((int *[[clang::address_space(sizeof(int) - 2)]])0, int *: 1, default: 0) == 2, "sizeof argument");
_Static_assert(_Generic((int *POINTER_SPACE)0, int *: 1, default: 0) == 2, "macro attribute");
_Static_assert(_Generic((int __attribute__((address_space(2))) *)0, int *: 1, default: 0) == 2, "pointee attribute");
_Static_assert(_Generic((int *[[clang::annotate_type("marker")]])0, int *: 1, default: 0) == 2, "ignored type annotation");
_Static_assert(_Generic((int **const)0, int **: 1, default: 0) == 2, "pointer qualifiers");
_Static_assert(_Generic((int *const *)0, int *const *: 1, default: 0) == 2, "nested pointer qualifiers");
_Static_assert(_Generic((int (*)(long))0, int (*)(long): 1, default: 0) == 2, "function pointer");
_Static_assert(_Generic((struct Entry *[[clang::address_space(Space + 1)]])0, struct Entry *: 1, default: 0) == 2, "named record");
_Static_assert(_Generic((Count *[[clang::address_space(Space)]])0, int *: 1, default: 0) == 2, "typedef spelling");
_Static_assert(_Generic((int *[[clang::address_space(3)]] *const)0, int **: 1, default: 0) == 2, "inner pointer attribute");
