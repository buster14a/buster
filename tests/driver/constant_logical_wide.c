// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int logical_wide_small(int x)
{
    return (x && (_BitInt(256))0) + (x || (_BitInt(256))1) + (x && (_BitInt(256))2);
}

int logical_wide_high_bits(int x)
{
    return (x && ((_BitInt(256))1 << 200)) + (x || (((_BitInt(256))1 << 200) + 1)) + (x && (_BitInt(256))-2);
}

int logical_wide_boolean_shifts(int x)
{
    return (x && (((_BitInt(256))1 << 200) >> 201)) + (x || (((_BitInt(256))1 << 200) >> 200));
}
