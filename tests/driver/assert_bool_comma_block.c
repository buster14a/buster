// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void comma_publication(void)
{
    _Static_assert((7, 0), "queued scalar warning");
    _Static_assert((__builtin_abs(7), 0), "immediate call warning");
    _Static_assert((7 < 8, 0), "immediate comparison warning");
}
