// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original requirement spelling probes for #2409. Clang is the diagnostic oracle.
_Static_assert(_Generic(0, int: 0, default: 1), "generic literal");
_Static_assert(__builtin_choose_expr(1, (0), 1), "chosen literal");
_Static_assert(__extension__ (0), "extension literal");
_Static_assert(_Generic(0, int: __builtin_choose_expr(1, 0, 1), default: 1), "nested literal");
_Static_assert(__builtin_choose_expr(1, _Generic(0, int: 0, default: 1), 1), "chosen generic literal");
_Static_assert(_Generic(0, int: (3 == 4), default: 1), "selected comparison");
_Static_assert(__builtin_choose_expr(1, sizeof(long) == 0, 1), "selected sizeof");
_Static_assert(_Generic(0, int: 0, default: 1) && sizeof(int) == 0, "later failed term");
_Static_assert(sizeof(int) == 0 && _Generic(0, int: 0, default: 1), "earlier failed term");
_Static_assert(__builtin_choose_expr(1, (sizeof(short) == 0 && 1), 0), "selected conjunction");
_Static_assert(sizeof((1 << 2) ?: 3) == 0, "omitted middle");
_Static_assert(__builtin_offsetof(struct { int first, second; }, second) == 1, "record spelling");
