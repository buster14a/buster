// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconstant-logical-operand"
int logical_ignored(int x)
{
    return x && 4;
}
_Static_assert(1 && 8, "ignored passing assertion");

#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wconstant-logical-operand"
int logical_enabled(int x)
{
    return x || 8;
}
#pragma clang diagnostic pop

int logical_ignored_again(int x)
{
    return x || 16;
}
#pragma clang diagnostic pop

int logical_restored(int x)
{
    return x && 16;
}

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconstant-logical-operand"
_Static_assert(1 && 32, "promoted passing assertion");
#pragma clang diagnostic pop

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
int logical_everything_ignored(int x)
{
    return x && 64;
}
#pragma clang diagnostic pop

int logical_final(int x)
{
    return x || 128;
}
