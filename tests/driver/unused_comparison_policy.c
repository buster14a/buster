// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#define BUSTER_LITERAL 7
#define BUSTER_ID(value) (value)
#define BUSTER_FORWARD(value) BUSTER_ID(value)

void unused_comparison_policy(int value)
{
    BUSTER_LITERAL == 8;
    BUSTER_ID(value) == 8;
    BUSTER_FORWARD(7) != 8;
    BUSTER_FORWARD(value) != 8;
    BUSTER_ID((int)value) == 8;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-comparison"
    7 == 8;
    value == 8;
    BUSTER_ID(value) == 8;
#pragma clang diagnostic pop
    7 == 8;
    value == 8;
}

_Static_assert((BUSTER_ID(7) == 8, 1), "argument equality succeeds");
_Static_assert((BUSTER_LITERAL == 8, 1), "macro literal equality succeeds");
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-comparison"
_Static_assert((7 == 8, 1), "ignored equality succeeds");
#pragma clang diagnostic pop
_Static_assert((7 == 8, 1), "restored equality succeeds");
