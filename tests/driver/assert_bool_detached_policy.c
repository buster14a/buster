// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define DETACHED_CONDITION(value) __builtin_choose_expr(0, (value) ? 2 : 3, 0)
_Static_assert(DETACHED_CONDITION(1 << 3), "macro condition");
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-constant-compare"
_Static_assert(DETACHED_CONDITION(1 << 4), "ignored condition");
#pragma clang diagnostic pop
_Static_assert(_Generic(0, int: 0, default: (2 << 4) ? 3 : 4), "restored condition");
