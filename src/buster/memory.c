#include <buster/memory.h>

bool memory_compare(const void* a, const void* b, u64 count)
{
    bool result = a == b;

    if (!result)
    {
#if BUSTER_OPTIMIZE
        result = memcmp(a, b, count) == 0;
#else
        const u8* p1 = (const u8*)a;
        const u8* p2 = (const u8*)b;

        u64 i = count;
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
