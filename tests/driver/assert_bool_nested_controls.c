// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

_Static_assert((_Bool)(1 << 2) == 0, "explicit boolean cast");
_Static_assert(sizeof(!(1 << 3)) == 0, "unevaluated not");
_Static_assert(!_Generic(1, int: 1, default: !(1 << 4)), "unselected generic operand");
_Static_assert(!__builtin_choose_expr(1, 1, !(1 << 4)), "unselected builtin operand");
_Static_assert(!(1 ? (1 << 3) : 1), "conditional arms have no outer truth conversion");
_Static_assert(!(_Bool)1.5, "explicit floating cast");
_Static_assert(sizeof(!1.5) == 0, "unevaluated floating not");
_Static_assert((1 << 3) != 0, "explicit comparison succeeds");
