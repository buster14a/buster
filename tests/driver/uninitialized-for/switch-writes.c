// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
int switch_writes(int limit, int choice)
{
    int step;
    int result = 0;
    for (int i = 0; i < limit; i += step)
    {
        switch (choice)
        {
            case 0:
            {
                step = 1;
                break;
            }
            default:
            {
                step = 2;
                break;
            }
        }
        result += i;
    }
    return result;
}
