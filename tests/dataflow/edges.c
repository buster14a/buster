// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original C smoke inputs for the IR dataflow source gate and optional benchmark.

unsigned dataflow_critical_edge(unsigned choose, unsigned again, unsigned value)
{
    unsigned left = value + 11;
    unsigned right = value + 22;
    if (choose)
    {
        left = right + again;
    }
    return left + right;
}

unsigned dataflow_parallel_phis(unsigned count, unsigned a, unsigned b)
{
    while (count != 0)
    {
        unsigned old = a;
        a = b;
        b = old;
        count -= 1;
    }
    return a ^ b;
}

unsigned dataflow_nested_loops(unsigned outer, unsigned inner, unsigned seed)
{
    unsigned sum = seed;
    for (unsigned i = 0; i < outer; i += 1)
    {
        unsigned value = seed + i;
        for (unsigned j = 0; j < inner; j += 1)
        {
            value = (value ^ (seed + j)) + sum;
            if ((value & 7) == 0)
            {
                break;
            }
        }
        sum += value;
    }
    return sum;
}

unsigned dataflow_irreducible(unsigned choose, unsigned count, unsigned seed)
{
    unsigned value = seed;
    if (choose)
    {
        goto right;
    }
left:
    value += seed;
    if (count == 0)
    {
        goto done;
    }
    count -= 1;
right:
    value ^= seed + count;
    if (count != 0)
    {
        count -= 1;
        goto left;
    }
done:
    return value;
}
