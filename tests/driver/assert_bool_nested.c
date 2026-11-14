// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

_Static_assert(!(1 << 3), "not");
struct NestedAssertion
{
    int member;
    _Static_assert((0 << 1) && (1 << 2), "and");
};
void nested_assertion_body(void)
{
    _Static_assert(!((1 << 2) || (2 << 3)), "or");
}
_Static_assert((1 << 1) ? 0 : 1, "condition");
_Static_assert(!((1 << 2) ? 3 : 4), "nested");
_Static_assert(!((!(0 << 1) ? 3 : 4) ? 2 : 3), "nested condition");
_Static_assert(!((0 << 2) ? ((1 << 3) ? 2 : 3) : ((1 << 4) ? 2 : 3)), "arm conditions");
_Static_assert(0 && (1 << 3), "short circuit");
_Static_assert(!1.5, "floating not");
_Static_assert(0 && 1.5, "floating short circuit");
_Static_assert((!(1 << 2)) << 0, "root and nested conversions");
_Static_assert(!_Generic(1, int: 1 << 3, default: !(1 << 4)), "generic selection");
_Static_assert(!__builtin_choose_expr(1, 1 << 3, !(1 << 4)), "builtin selection");
_Static_assert((0 << 1) && ((1 << 2) ?: 1), "omitted middle saved value");
_Static_assert((0 << 1) && ((!(1 << 2)) ?: 1), "omitted middle shared operand");
