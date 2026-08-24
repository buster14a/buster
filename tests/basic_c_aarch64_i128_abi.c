// Focused AAPCS64 __int128 boundary fixture.  The pair in a bare integer-128
// value is an INTEGER pair, but the pair itself has sixteen-byte alignment:
// after an odd X-register cursor it rounds to the next even register, and
// after the register file closes its stack image starts at an even eightbyte.
// The functions are deliberately call boundaries rather than only arithmetic
// bodies so a caller/callee disagreement cannot hide in one compiler's frame
// representation.  This file has no hosted dependencies and therefore also
// runs as a static AArch64 image under qemu-aarch64 when the emulator exists.
typedef unsigned char u8;
typedef unsigned long long u64;

typedef struct
{
    __int128 value;
} I128Wrapper;

static int check_i128(__int128 value, u64 low, u64 high)
{
    return (u64)value == low && (u64)(value >> 64) == high;
}

// One consumed GPR leaves x1, so C.8 rounds the pair to x2:x3.  The trailing
// scalar catches a callee that starts the pair at x1 or fails to advance two
// registers after it.
static int after_one(int tag, __int128 value, int tail)
{
    return tag == 11 && check_i128(value, 0x0123456789abcdefULL, 0x1122334455667788ULL) && tail == 22;
}

// Six consumed GPRs leave x6, already even, so the pair fits exactly in
// x6:x7.  This is the non-rounding register-pair case.
static int after_six(int a, int b, int c, int d, int e, int f, __int128 value)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && check_i128(value, 7, 8);
}

// Seven consumed GPRs leave only x7.  The pair spills at the stack base and
// closes the X file, so the following scalar must also be stacked.
static int after_seven(int a, int b, int c, int d, int e, int f, int g, __int128 value, int tail)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && check_i128(value, 9, 10) && tail == 11;
}

// The ninth scalar occupies stack eightbyte zero.  A sixteen-byte pair that
// follows it must skip eightbyte one and begin at NSAA+16.
static int after_stacked_scalar(int a, int b, int c, int d, int e, int f, int g, int h, int i, __int128 value)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && h == 8 && i == 9 && check_i128(value, 12, 13);
}

static __int128 make_i128(void)
{
    return ((__int128)0x8899aabbccddeeffULL << 64) | (__int128)0x0123456789abcdefULL;
}

static I128Wrapper wrapper_after_one(int tag, I128Wrapper value, int tail)
{
    if (tag != 31 || tail != 32 || !check_i128(value.value, 33, 34))
    {
        return (I128Wrapper){0};
    }
    return value;
}

int main(void)
{
    __int128 value = ((__int128)0x1122334455667788ULL << 64) | (__int128)0x0123456789abcdefULL;
    if (!after_one(11, value, 22))
    {
        return 1;
    }
    if (!after_six(1, 2, 3, 4, 5, 6, ((__int128)8 << 64) | 7))
    {
        return 2;
    }
    if (!after_seven(1, 2, 3, 4, 5, 6, 7, ((__int128)10 << 64) | 9, 11))
    {
        return 3;
    }
    if (!after_stacked_scalar(1, 2, 3, 4, 5, 6, 7, 8, 9, ((__int128)13 << 64) | 12))
    {
        return 4;
    }
    if (!check_i128(make_i128(), 0x0123456789abcdefULL, 0x8899aabbccddeeffULL))
    {
        return 5;
    }
    I128Wrapper wrapped = {.value = ((__int128)34 << 64) | 33};
    I128Wrapper wrapped_result = wrapper_after_one(31, wrapped, 32);
    if (!check_i128(wrapped_result.value, 33, 34))
    {
        return 6;
    }
    return 0;
}
