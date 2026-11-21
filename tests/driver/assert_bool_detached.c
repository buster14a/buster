// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

_Static_assert(__builtin_choose_expr(0, (1 << 2) ? 3 : 4, 0), "unselected condition");
_Static_assert(_Generic(1, int: 0, default: (1 << 2) ? 3 : 4), "generic condition");
_Static_assert(__builtin_choose_expr(0, !(1 << 2), 0), "unselected logical");
_Static_assert(_Generic(1, int: 0, default: !(1 << 2)), "generic logical");
_Static_assert(sizeof(!(1 << 2)) == 0, "sizeof logical");
_Static_assert(_Generic(!(1 << 2), int: 0), "controlling logical");
_Static_assert(__builtin_constant_p((1 << 2) ? 3 : 4) == 0, "constant condition");
_Static_assert(__builtin_constant_p(!(1 << 2)) == 0, "constant logical");
struct DetachedCondition
{
    int member;
    _Static_assert(__builtin_choose_expr(0, 1.5 ? 3 : 4, 0), "member unselected floating");
};
void detached_condition_body(void)
{
    _Static_assert(_Generic(0, int: 0, default: (1.5 ? 3 : 4)), "block unselected floating");
}
_Static_assert(__builtin_choose_expr(0, (1 << 2) ? ((2 << 3) ? 4 : 5) : ((3 << 4) ? 6 : 7), 0), "postorder");
_Static_assert(__builtin_choose_expr(1, 0, (1 << 2) ?: 3), "unselected omitted middle");
_Static_assert(sizeof((1 << 2) ?: 3) == 0, "query omitted middle");
_Static_assert(__builtin_choose_expr(0, (_Bool)(1 << 2) ? 3 : 4, 0), "explicit conversion");
