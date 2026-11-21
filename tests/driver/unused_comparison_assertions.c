// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

_Static_assert((7 == 8, 1), "equality succeeds");
_Static_assert((7 != 8, 1), "inequality succeeds");
_Static_assert((7 < 8, 1), "relational succeeds");
_Static_assert(((int)7 == 8, 1), "cast equality succeeds");
_Static_assert(((1 ? 7 : 8) == 9, 1), "conditional equality succeeds");
const int comparison_assertion_constant = 7;
_Static_assert((comparison_assertion_constant == 8, 1), "constant lvalue keeps its suggestion");
struct ComparisonAssertion
{
    int value;
    _Static_assert((7 == 8, 1), "member equality succeeds");
};

void unused_comparison_assertions(void)
{
    const int local_constant = 7;
    _Static_assert((7 == 8, 1), "block equality succeeds");
    _Static_assert((7 != 8, 1), "block inequality succeeds");
    _Static_assert((7 < 8, 1), "block relational succeeds");
    _Static_assert((local_constant == 8, 1), "local constant lvalue keeps its suggestion");
}
