#pragma once
#include <buster/memory.h>

BUSTER_F_IMPL bool memory_compare(const void* a, const void* b, u64 count)
{
    bool result = a == b;

    if (!result)
    {
#if BUSTER_OPTIMIZE
        result = memcmp(a, b, count) == 0;
#else
        let p1 = (const u8*)a;
        let p2 = (const u8*)b;

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
#endif
    }

    return result;
}

template <typename T>
BUSTER_F_IMPL bool slice_compare(Slice<T> a, Slice<T> b)
{
    bool result = false;

    if (a.length == b.length)
    {
        result = a.pointer == b.pointer;

        if (!result)
        {
            let count = a.length;
#if BUSTER_OPTIMIZE
            result = memcmp(a.pointer, b.pointer, count * sizeof(T)) == 0;
#else
            result = true;

            for (u64 i = 0; i < count; i += 1)
            {
                if (a.pointer[i] != b.pointer[i])
                {
                    result = false;
                    break;
                }
            }
#endif
        }
    }

    return result;
}

template bool slice_compare<char8>(Slice<char8> a, Slice<char8> b);
template bool slice_compare<char16>(Slice<char16> a, Slice<char16> b);
