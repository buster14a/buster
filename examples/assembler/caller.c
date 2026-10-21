// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

extern long assembly_sum(const long* values, long count);

long external_twice(long value)
{
    return value * 2;
}

int main(void)
{
    const long values[] =
    {
        1,
        2,
        3,
        4,
    };
    return assembly_sum(values, 4) == 423 && assembly_sum(values, 0) == 403 ? 0 : 1;
}
