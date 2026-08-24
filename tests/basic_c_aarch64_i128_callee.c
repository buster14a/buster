// AAPCS64 __int128 callee half of the cross-compiler fixture.  Keep this in
// its own translation unit: the caller is compiled by the other compiler so
// register and stack placement cannot agree merely because one optimizer
// lowered both sides together.
typedef unsigned long long u64;

typedef struct
{
    __int128 value;
} I128Wrapper;

static int check_i128(__int128 value, u64 low, u64 high)
{
    return (u64)value == low && (u64)(value >> 64) == high;
}

int a64_i128_after_one(int tag, __int128 value, int tail)
{
    return tag == 11 && check_i128(value, 0x0123456789abcdefULL, 0x1122334455667788ULL) && tail == 22;
}

int a64_i128_after_six(int a, int b, int c, int d, int e, int f, __int128 value)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && check_i128(value, 7, 8);
}

int a64_i128_after_seven(int a, int b, int c, int d, int e, int f, int g, __int128 value, int tail)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && check_i128(value, 9, 10) && tail == 11;
}

int a64_i128_after_stacked_scalar(int a, int b, int c, int d, int e, int f, int g, int h, int i, __int128 value)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && h == 8 && i == 9 && check_i128(value, 12, 13);
}

__int128 a64_i128_return(void)
{
    return ((__int128)0x8899aabbccddeeffULL << 64) | (__int128)0x0123456789abcdefULL;
}

I128Wrapper a64_i128_wrapper_after_one(int tag, I128Wrapper value, int tail)
{
    if (tag != 31 || tail != 32 || !check_i128(value.value, 33, 34))
    {
        return (I128Wrapper){0};
    }
    return value;
}
