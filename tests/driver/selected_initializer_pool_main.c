// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int used(int i);

int main(void)
{
    for (int i = 0; i < 64; i += 1)
    {
        if (used(i) != i % 16 + 1)
        {
            return 1;
        }
    }
    return 0;
}
