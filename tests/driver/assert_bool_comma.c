// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

_Static_assert((7, 0), "scalar comma");
_Static_assert((7, 8, 0), "comma chain");
_Static_assert((7 < 8, 0), "discarded comparison");
_Static_assert(((int)7, 0), "discarded conversion");
_Static_assert(((void)7, 0), "explicit discard");
_Static_assert(0 && (7, 0), "short circuit");
_Static_assert(1 ? 0 : (7, 0), "unselected conditional");
_Static_assert(__builtin_choose_expr(1, 0, (7, 0)), "unselected choice");
_Static_assert(_Generic(0, int: 0, default: (7, 0)), "unselected association");
_Static_assert(sizeof((7, 0)) == 0, "unevaluated query");
_Static_assert(_Generic((7, 0), int: 0), "unevaluated control");
_Static_assert(__builtin_constant_p((7, 0)) == 0, "constant query operand");
_Static_assert(((1 << 2) ? (7, 0) : 1), "comma before condition");
_Static_assert((7, (1 << 2) ? 0 : 1), "condition before comma");
_Static_assert((7, (1 << 2) && 0), "comma before logical conversion");
_Static_assert((__builtin_abs(7), 0), "discarded builtin result");
struct CommaAssertion
{
    int member;
    _Static_assert((7, 0), "member comma");
};
void comma_assertion_body(void)
{
    _Static_assert((7, 0), "block comma");
    _Static_assert((7 < 8, 0), "block comparison");
    _Static_assert((7, (1 << 2) && 0), "block logical conversion");
}
_Static_assert((7, 1), "successful comma");
