#pragma once
#include <buster/string.h>

BUSTER_IMPL bool string_generic_equal(void* p1, void* p2, u64 l1, u64 l2, u64 element_size)
{
    bool is_equal = l1 == l2;
    if (is_equal & (l1 != 0) & (p1 != p2))
    {
        is_equal = memory_compare(p1, p2, l1 * element_size);
    }

    return is_equal;
}
