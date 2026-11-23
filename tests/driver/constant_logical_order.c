// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int logical_nested_left(int x)
{
    return (x && 4) || 8;
}

int logical_nested_right(int x)
{
    return x && ((0 || 8) + 4);
}

int logical_discarded(int x)
{
    x && 4;
    return x;
}

int logical_condition(int x)
{
    if (x = (1 && 4))
    {
        return x;
    }
    return 0;
}

int logical_short_circuit(int x)
{
    return 0 && (x || 8);
}

_Static_assert((1 && 4) && 8, "nested passing condition");
