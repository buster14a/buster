// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void nested_assertion_ownership(void)
{
    _Static_assert(__builtin_choose_expr(0,
        ({ _Static_assert((1 << 2) ? 0 : 1, "inner"); 0; }), 0), "outer choice");
}

void nested_comma_ownership(void)
{
    _Static_assert(__builtin_choose_expr(0,
        ({ _Static_assert((7, 0), "inner comma"); 0; }), 0), "outer comma choice");
}
