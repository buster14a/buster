#pragma once
#include <buster/memory.h>

BUSTER_IMPL bool memory_compare(void* a, void* b, u64 count)
{
    bool result = a == b;

    if (!result)
    {
        let p1 = (u8*)a;
        let p2 = (u8*)b;

        let i = count;
        result = 1;

        while (i--)
        {
            bool is_equal = *p1 == *p2;
            if (!is_equal)
            {
                result = 0;
                break;
            }

            p1 += 1;
            p2 += 1;
        }
    }

    return result;
}

