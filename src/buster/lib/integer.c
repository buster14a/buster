#include <buster/lib/integer.h>
#include <buster/lib/os.h>

u64 align_forward(u64 n, u64 a)
{
    u64 result;
    BUSTER_VALIDATE(align_forward_checked(n, a, &result));
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
    u8 result = 32;
    if (n != 0)
    {
#if __has_builtin(__builtin_ctz)
        result = (u8)__builtin_ctz(n);
#else
        result = 0;
        while ((n & (u32)1) == 0)
        {
            result += 1;
            n >>= 1;
        }
#endif
    }

    return result;
}

u8 trailing_zeroes_u64(u64 n)
{
    u8 result = 64;
    if (n != 0)
    {
#if __has_builtin(__builtin_ctzll)
        result = (u8)__builtin_ctzll(n);
#else
        result = 0;
        while ((n & (u64)1) == 0)
        {
            result += 1;
            n >>= 1;
        }
#endif
    }

    return result;
}

u8 leading_zeroes_u32(u32 n)
{
    u8 result = 32;
    if (n != 0)
    {
#if __has_builtin(__builtin_clz)
        result = (u8)__builtin_clz(n);
#else
        result = 0;
        u32 mask = (u32)1 << 31;
        while ((n & mask) == 0)
        {
            result += 1;
            mask >>= 1;
        }
#endif
    }

    return result;
}

u8 leading_zeroes_u64(u64 n)
{
    u8 result = 64;
    if (n != 0)
    {
#if __has_builtin(__builtin_clzll)
        result = (u8)__builtin_clzll(n);
#else
        result = 0;
        u64 mask = (u64)1 << 63;
        while ((n & mask) == 0)
        {
            result += 1;
            mask >>= 1;
        }
#endif
    }

    return result;
}

bool u64_add_s64_checked(u64 address, s64 addend, u64* result)
{
    u64 magnitude = addend < 0 ? (u64)(-(addend + 1)) + 1 : (u64)addend;
    bool valid = result && (addend < 0 ? magnitude <= address : magnitude <= UINT64_MAX - address);
    if (valid)
    {
        *result = addend < 0 ? address - magnitude : address + magnitude;
    }

    return valid;
}

bool s32_from_u64_add_s64_checked(u64 address, s64 addend, s32* result)
{
    bool valid = result && address <= (u64)INT64_MAX;
    s64 value = valid ? (s64)address : 0;
    if (valid && ((addend > 0 && value > INT64_MAX - addend) || (addend < 0 && value < INT64_MIN - addend)))
    {
        valid = false;
    }
    if (valid)
    {
        value += addend;
        valid = value >= INT32_MIN && value <= INT32_MAX;
    }
    if (valid)
    {
        *result = (s32)value;
    }

    return valid;
}
