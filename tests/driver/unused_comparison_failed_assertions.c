// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Integration control for #2906 with #2409's failed-assertion comma-warning recovery.

#define BUSTER_LITERAL 7
#define BUSTER_COMPARE(left, right) ((left) == (right))

_Static_assert((7 == 8, 0), "equality fails");
_Static_assert((7 != 8, 0), "inequality fails");
_Static_assert((7 < 8, 0), "relational fails");
_Static_assert((BUSTER_LITERAL == 8, 0), "macro operand equality fails");
_Static_assert((BUSTER_COMPARE(7, 8), 0), "macro-body equality stays quiet");
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-comparison"
_Static_assert((7 == 8, 0), "ignored equality fails");
#pragma clang diagnostic pop
_Static_assert((7 == 8, 0), "restored equality fails");
struct FailedComparisonAssertion
{
    int value;
    _Static_assert((7 == 8, 0), "member equality fails");
};

void unused_comparison_failed_assertions(void)
{
    _Static_assert((7 == 8, 0), "block equality fails");
    _Static_assert((7 != 8, 0), "block inequality fails");
}
