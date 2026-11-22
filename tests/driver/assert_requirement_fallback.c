// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Forms outside the cold spelling replay retain diagnostics and preserve rejected output files.
_Static_assert(sizeof(int __attribute__((vector_size(sizeof("123"))))) == 0, "active string");
_Static_assert(_Generic((int *[[clang::address_space(sizeof("x"))]])0, int *: 1, default: 0) == 2, "pointer string");
_Static_assert(_Generic((int (*[[clang::address_space(1)]])(void))0, int (*)(void): 1, default: 0) == 2, "complex declarator");
