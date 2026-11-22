// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Successful selections and top-level pointer qualifiers stay accepted in syntax and object modes.
_Static_assert(_Generic((int *[[clang::address_space(1)]])0, int *: 1, default: 0) == 1, "qualified control");
_Static_assert(_Generic(0, int: 1, default: 0), "generic control");
_Static_assert(__builtin_choose_expr(1, 1, 0), "choose control");
_Static_assert(__extension__ (1), "extension control");
_Static_assert(_Generic(0, int: __builtin_choose_expr(1, 1, 0), default: 0), "nested control");
int assertion_requirement_control(void) { return 23; }
