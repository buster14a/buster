// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define ZERO_SHIFT(amount) (0 << (amount))
_Static_assert(ZERO_SHIFT(3), "macro");
