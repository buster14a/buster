// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

_Static_assert(1 && 4, "file passes");
_Static_assert(0 && 4, "file fails");
_Static_assert((0 && 8) || 0, "nested file fails");

struct LogicalPassingRecord
{
    int member;
    _Static_assert(1 || 4, "member passes");
};

struct LogicalFailingRecord
{
    int member;
    _Static_assert(0 && 16, "member fails");
};

int logical_block_assertions(int x)
{
    _Static_assert(1 && 4, "block passes");
    _Static_assert(0 && 4, "block fails");
    return x || 8;
}

_Static_assert(0 && 1, "plain boolean failure");
_Static_assert(1 || 0, "plain boolean success");

int logical_after_failed_assertion(int x)
{
    return x && 32;
}
