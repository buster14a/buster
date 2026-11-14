// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

_Static_assert(0 << 3, "file");
struct AssertionMember
{
    int member;
    _Static_assert(0 << 2, "record");
};
void assertion_body(void)
{
    _Static_assert(0 << 1, "block");
    _Static_assert(0 << 4, "second block assertion");
}
_Static_assert(1 << 3, "following successful assertion");
_Static_assert(0, "plain failure");
_Static_assert((0 << 3) != 0, "explicit comparison");
