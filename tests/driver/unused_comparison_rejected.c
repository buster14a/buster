// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void unused_comparison_before_failure(int value)
{
    7 == 8;
    value == 8;
    7 != 8;
    _Static_assert(0, "preserve earlier comparisons");
}

// The equality supplies the assertion's value and is never an unused comparison.
_Static_assert(7 == 8, "direct failed equality");
_Static_assert(7 == 7, "direct successful equality");
