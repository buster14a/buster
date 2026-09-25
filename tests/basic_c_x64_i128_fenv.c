// A changed x87 precision and rounding mode must survive every conversion.
// Expected images come from integer distances to the adjacent representable
// values; the test does not use a floating cast to compute its oracle.
#if __LDBL_MANT_DIG__ == 64
typedef unsigned long long U64;
typedef unsigned int U32;
typedef unsigned __int128 U128;
typedef __int128 S128;

typedef union LongImage LongImage;
union LongImage
{
    long double value;
    struct { U64 significand; unsigned short exponent; } bits;
};
typedef union DoubleImage DoubleImage;
union DoubleImage { double value; U64 bits; };
typedef union FloatImage FloatImage;
union FloatImage { float value; U32 bits; };

static long double wide_unsigned(U128 value) { return (long double)value; }
static long double wide_signed(S128 value) { return (long double)value; }
static double double_unsigned(U128 value) { return (double)value; }
static double double_signed(S128 value) { return (double)value; }
static float float_unsigned(U128 value) { return (float)value; }
static float float_signed(S128 value) { return (float)value; }
static U128 integer_unsigned(long double value) { return (U128)value; }
static S128 integer_signed(long double value) { return (S128)value; }

int main(void)
{
    unsigned short saved, observed;
    __asm__ volatile("fnstcw %0" : "=m"(saved));
    U128 two64 = (U128)1 << 64;
    unsigned precision_modes[] = {0, 2, 3};
    int failed = 0;
    for (unsigned precision = 0; precision < 3 && !failed; precision += 1)
    {
        for (unsigned rounding = 0; rounding < 4 && !failed; rounding += 1)
        {
            unsigned short control = (unsigned short)((saved & ~0x0f00u) |
                (precision_modes[precision] << 8) | (rounding << 10));
            __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
            LongImage wide_positive = {.value = wide_unsigned(two64 + 3)};
            LongImage wide_negative = {.value = wide_signed(-((S128)two64 + 3))};
            DoubleImage double_positive = {.value = double_unsigned(two64 + ((U128)1 << 11))};
            DoubleImage double_negative = {.value = double_signed(-((S128)two64 + ((S128)1 << 11)))};
            FloatImage float_positive = {.value = float_unsigned(two64 + ((U128)1 << 40))};
            FloatImage float_negative = {.value = float_signed(-((S128)two64 + ((S128)1 << 40)))};
            LongImage exact = {.value = wide_unsigned(two64 + 2)};
            LongImage negative_exact = {.value = wide_signed(-((S128)two64 + 2))};
            U128 unsigned_result = integer_unsigned(exact.value);
            S128 signed_result = integer_signed(negative_exact.value);
            __asm__ volatile("fnstcw %0" : "=m"(observed));
            U64 positive_wide_step = rounding == 0 || rounding == 2 ? 2 : 1;
            U64 negative_wide_step = rounding == 0 || rounding == 1 ? 2 : 1;
            U64 positive_step = rounding == 2 ? 1 : 0;
            U64 negative_step = rounding == 1 ? 1 : 0;
            failed = observed != control ||
                wide_positive.bits.significand != 0x8000000000000000ULL + positive_wide_step ||
                wide_positive.bits.exponent != 0x403f ||
                wide_negative.bits.significand != 0x8000000000000000ULL + negative_wide_step ||
                wide_negative.bits.exponent != 0xc03f ||
                double_positive.bits != 0x43f0000000000000ULL + positive_step ||
                double_negative.bits != 0xc3f0000000000000ULL + negative_step ||
                float_positive.bits != 0x5f800000U + (U32)positive_step ||
                float_negative.bits != 0xdf800000U + (U32)negative_step ||
                unsigned_result != two64 + 2 || (U128)signed_result != (U128)(-((S128)two64 + 2));
        }
    }
    __asm__ volatile("fldcw %0" : : "m"(saved) : "memory");
    return failed;
}
#else
int main(void) { return 0; }
#endif
