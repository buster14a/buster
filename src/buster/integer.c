#include <buster/integer.h>
#include <buster/os.h>

u64 align_forward(u64 n, u64 a)
{
    u64 mask = a - 1;
    u64 result = (n + mask) & ~mask;
    return result;
}

bool is_aligned(u64 n, u64 alignment)
{
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(alignment));
    return (n & (alignment - 1)) == 0;
}

u64 next_power_of_two(u64 n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

u8 trailing_zeroes_u32(u32 n)
{
    if (n == 0) return 32;

#if __has_builtin(__builtin_ctz)
    return (u8)__builtin_ctz(n);
#else
    u8 result = 0;
    while ((n & (u32)1) == 0)
    {
        result += 1;
        n >>= 1;
    }

    return result;
#endif
}

u8 trailing_zeroes_u64(u64 n)
{
    if (n == 0) return 64;

#if __has_builtin(__builtin_ctzll)
    return (u8)__builtin_ctzll(n);
#else
    u8 result = 0;
    while ((n & (u64)1) == 0)
    {
        result += 1;
        n >>= 1;
    }

    return result;
#endif
}

u8 leading_zeroes_u32(u32 n)
{
    if (n == 0) return 32;

#if __has_builtin(__builtin_clz)
    return (u8)__builtin_clz(n);
#else
    u8 result = 0;
    u32 mask = (u32)1 << 31;
    while ((n & mask) == 0)
    {
        result += 1;
        mask >>= 1;
    }

    return result;
#endif
}

u8 leading_zeroes_u64(u64 n)
{
    if (n == 0) return 64;

#if __has_builtin(__builtin_clzll)
    return (u8)__builtin_clzll(n);
#else
    u8 result = 0;
    u64 mask = (u64)1 << 63;
    while ((n & mask) == 0)
    {
        result += 1;
        mask >>= 1;
    }

    return result;
#endif
}
