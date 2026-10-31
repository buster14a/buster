// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original C-source fixtures for test-ir-source --dominators. These exercise the frontend-to-CFG handoff;
// dominators_test.c separately guarantees the graph shapes that source simplification may remove.

unsigned dom_leaf(unsigned value)
{
    return value * 3u + 7u;
}

unsigned dom_diamond(unsigned value)
{
    unsigned result = value;
    if ((value & 1u) != 0)
    {
        result += 13u;
    }
    else
    {
        result ^= 31u;
    }
    return result * 5u;
}

unsigned dom_nested(unsigned count)
{
    unsigned sum = 0;
    for (unsigned outer = 0; outer < count; outer += 1)
    {
        for (unsigned inner = 0; inner < outer; inner += 1)
        {
            if ((inner & 1u) == 0)
            {
                continue;
            }
            if (inner == 7)
            {
                break;
            }
            sum += outer + inner;
        }
    }
    return sum;
}

unsigned dom_latches(unsigned count)
{
    unsigned sum = 0;
header:
    if (count != 0)
    {
        count -= 1;
        if ((count & 1u) != 0)
        {
            sum += count;
            goto header;
        }
        sum ^= count;
        goto header;
    }
    return sum;
}

// The entry can reach either label without passing through the other. Both are in the same cyclic region.
unsigned dom_irreducible(unsigned count, unsigned right_first)
{
    unsigned sum = 0;
    if (right_first != 0)
    {
        goto right;
    }
left:
    if (count == 0)
    {
        goto done;
    }
    count -= 1;
    sum += 3;
    goto right;
right:
    if (count == 0)
    {
        goto done;
    }
    count -= 1;
    sum ^= 5;
    goto left;
done:
    return sum;
}

unsigned dom_self_loop(volatile unsigned* ready)
{
    while (*ready == 0)
    {
    }
    return *ready;
}

unsigned dom_switch(unsigned value)
{
    unsigned result = 0;
    switch (value)
    {
    case 0:
    case 1:
        result = 11;
        break;
    case 4:
    case 7:
        result = 19;
        break;
    default:
        result = value + 1;
        break;
    }
    return result;
}

int main(void)
{
    volatile unsigned ready = 1;
    const unsigned failures = (dom_leaf(4) != 19) + (dom_diamond(3) != 80) + (dom_diamond(4) != 135) +
                              (dom_nested(4) != 7) + (dom_latches(5) != 6) + (dom_irreducible(4, 0) != 12) +
                              (dom_irreducible(4, 1) != 16) + (dom_self_loop(&ready) != 1) +
                              (dom_switch(0) != 11) + (dom_switch(7) != 19) + (dom_switch(9) != 10);
    return failures != 0;
}
