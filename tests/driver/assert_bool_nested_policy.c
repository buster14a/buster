// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define ASSERT_NESTED(value) _Static_assert(!(value), "macro not")
ASSERT_NESTED(1 << 3);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-constant-compare"
_Static_assert(!(1 << 4), "ignored");
#pragma clang diagnostic pop
_Static_assert(!((1 << 2) ? 3 : 4), "restored");
