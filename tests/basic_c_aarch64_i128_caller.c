// AAPCS64 __int128 caller half of the cross-compiler fixture.  This is linked
// once with a clang callee and once with a buster callee under qemu-aarch64.
typedef unsigned long long u64;

typedef struct
{
    __int128 value;
} I128Wrapper;

extern int a64_i128_after_one(int, __int128, int);
extern int a64_i128_after_six(int, int, int, int, int, int, __int128);
extern int a64_i128_after_seven(int, int, int, int, int, int, int, __int128, int);
extern int a64_i128_after_stacked_scalar(int, int, int, int, int, int, int, int, int, __int128);
extern __int128 a64_i128_return(void);
extern I128Wrapper a64_i128_wrapper_after_one(int, I128Wrapper, int);

static int check_i128(__int128 value, u64 low, u64 high)
{
    return (u64)value == low && (u64)(value >> 64) == high;
}

int main(void)
{
    __int128 value = ((__int128)0x1122334455667788ULL << 64) | (__int128)0x0123456789abcdefULL;
    if (!a64_i128_after_one(11, value, 22))
    {
        return 1;
    }
    if (!a64_i128_after_six(1, 2, 3, 4, 5, 6, ((__int128)8 << 64) | 7))
    {
        return 2;
    }
    if (!a64_i128_after_seven(1, 2, 3, 4, 5, 6, 7, ((__int128)10 << 64) | 9, 11))
    {
        return 3;
    }
    if (!a64_i128_after_stacked_scalar(1, 2, 3, 4, 5, 6, 7, 8, 9, ((__int128)13 << 64) | 12))
    {
        return 4;
    }
    if (!check_i128(a64_i128_return(), 0x0123456789abcdefULL, 0x8899aabbccddeeffULL))
    {
        return 5;
    }
    I128Wrapper wrapped;
    wrapped.value = ((__int128)34 << 64) | 33;
    I128Wrapper wrapped_result = a64_i128_wrapper_after_one(31, wrapped, 32);
    if (!check_i128(wrapped_result.value, 33, 34))
    {
        return 6;
    }
    return 0;
}
