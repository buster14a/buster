// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// Each pending conditional retains its completion and arms past the local work frontier.
_Static_assert(!((((((((((((((((((((((1 << 2)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 1 : 0)
    ? 2 : 3), "deep conditional conditions");

_Static_assert(__builtin_popcount(!(1 << 3)), "builtin argument");
_Static_assert(__builtin_constant_p(!(1 << 3)) == 0, "builtin query argument");
