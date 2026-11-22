// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define ASSERT_COMMA(value) ((value), 0)
_Static_assert(ASSERT_COMMA(7), "macro comma");
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
_Static_assert(ASSERT_COMMA(8), "ignored comma");
#pragma clang diagnostic pop
_Static_assert(ASSERT_COMMA(9), "restored comma");
#define ASSERT_COMMA_BODY() (7, 0)
_Static_assert(ASSERT_COMMA_BODY(), "macro body stays quiet");
