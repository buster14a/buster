// Exact binary128 widening and raw sign observations. Expected bytes were
// independently obtained from Clang's __float128 conversions on x86-64.
// No scalar binary128 ABI or arithmetic is required: wide values travel only
// through pointers. LIBRARY exposes the three operations to external callers;
// FENV uses an independently compiled AArch64 FPSR/FPCR helper.
#if __LDBL_MANT_DIG__ == 113

typedef unsigned long long F128Word;
typedef union F128Image F128Image;
union F128Image { long double value; F128Word words[2]; };

#ifndef BUSTER_F128_CLIENT
void f128_from_float(long double* output, float input)
{
    *output = (long double)input;
}

void f128_from_double(long double* output, double input)
{
    *output = (long double)input;
}

int f128_sign(long double const* input)
{
    return __builtin_signbitl(*input);
}
#else
void f128_from_float(long double* output, float input);
void f128_from_double(long double* output, double input);
int f128_sign(long double const* input);
#endif

#ifndef BUSTER_F128_LIBRARY
typedef struct F128Case F128Case;
struct F128Case { F128Word input, low, high; unsigned invalid; };
static F128Case const f128_cases32[] = {
    {0x00000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0},
    {0x80000000ull, 0x0000000000000000ull, 0x8000000000000000ull, 0},
    {0x3f800000ull, 0x0000000000000000ull, 0x3fff000000000000ull, 0},
    {0xbf800000ull, 0x0000000000000000ull, 0xbfff000000000000ull, 0},
    {0x3f800001ull, 0x0000000000000000ull, 0x3fff000002000000ull, 0},
    {0x7f7fffffull, 0x0000000000000000ull, 0x407efffffe000000ull, 0},
    {0x00800000ull, 0x0000000000000000ull, 0x3f81000000000000ull, 0},
    {0x00000001ull, 0x0000000000000000ull, 0x3f6a000000000000ull, 0},
    {0x007fffffull, 0x0000000000000000ull, 0x3f80fffffc000000ull, 0},
    {0x80000001ull, 0x0000000000000000ull, 0xbf6a000000000000ull, 0},
    {0x7f800000ull, 0x0000000000000000ull, 0x7fff000000000000ull, 0},
    {0xff800000ull, 0x0000000000000000ull, 0xffff000000000000ull, 0},
    {0x7fc12345ull, 0x0000000000000000ull, 0x7fff82468a000000ull, 0},
    {0xffc12345ull, 0x0000000000000000ull, 0xffff82468a000000ull, 0},
    {0x7f812345ull, 0x0000000000000000ull, 0x7fff82468a000000ull, 1},
    {0xff812345ull, 0x0000000000000000ull, 0xffff82468a000000ull, 1},
};

static F128Case const f128_cases64[] = {
    {0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0},
    {0x8000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull, 0},
    {0x3ff0000000000000ull, 0x0000000000000000ull, 0x3fff000000000000ull, 0},
    {0xbff0000000000000ull, 0x0000000000000000ull, 0xbfff000000000000ull, 0},
    {0x3ff0000000000001ull, 0x1000000000000000ull, 0x3fff000000000000ull, 0},
    {0x7fefffffffffffffull, 0xf000000000000000ull, 0x43feffffffffffffull, 0},
    {0x0010000000000000ull, 0x0000000000000000ull, 0x3c01000000000000ull, 0},
    {0x0000000000000001ull, 0x0000000000000000ull, 0x3bcd000000000000ull, 0},
    {0x000fffffffffffffull, 0xe000000000000000ull, 0x3c00ffffffffffffull, 0},
    {0x8000000000000001ull, 0x0000000000000000ull, 0xbbcd000000000000ull, 0},
    {0x7ff0000000000000ull, 0x0000000000000000ull, 0x7fff000000000000ull, 0},
    {0xfff0000000000000ull, 0x0000000000000000ull, 0xffff000000000000ull, 0},
    {0x7ff8123456789abcull, 0xc000000000000000ull, 0x7fff8123456789abull, 0},
    {0xfff8123456789abcull, 0xc000000000000000ull, 0xffff8123456789abull, 0},
    {0x7ff0123456789abcull, 0xc000000000000000ull, 0x7fff8123456789abull, 1},
    {0xfff0123456789abcull, 0xc000000000000000ull, 0xffff8123456789abull, 1},
};

#ifdef BUSTER_F128_FENV
unsigned f128_read_status(void);
void f128_write_status(unsigned value);
unsigned f128_read_control(void);
void f128_write_control(unsigned value);
#endif

int main(void)
{
    int result = 0;
#ifdef BUSTER_F128_FENV
    unsigned saved_control = f128_read_control();
    unsigned saved_status = f128_read_status();
    unsigned settings = 8;
#else
    unsigned settings = 1;
#endif
    for (unsigned setting = 0; setting < settings; setting += 1)
    {
#ifdef BUSTER_F128_FENV
        // All four rounding modes, with and without default-NaN/flush mode.
        f128_write_control((saved_control & ~0x03c00000u) | ((setting & 3u) << 22) | (setting >= 4 ? 0x03000000u : 0));
#endif
        for (unsigned width = 0; width < 2; width += 1)
        {
            F128Case const* cases = width ? f128_cases64 : f128_cases32;
            unsigned count = width ? sizeof(f128_cases64) / sizeof(*f128_cases64) : sizeof(f128_cases32) / sizeof(*f128_cases32);
            for (unsigned index = 0; index < count; index += 1)
            {
                struct { F128Word before[2]; F128Image image; F128Word after[2]; } output;
                output.before[0] = 0x123456789abcdef0ull;
                output.before[1] = 0x23456789abcdef01ull;
                output.after[0] = 0x3456789abcdef012ull;
                output.after[1] = 0x456789abcdef0123ull;
#ifdef BUSTER_F128_FENV
                f128_write_status(0x10); // Preserve an existing inexact flag.
#endif
                if (width)
                {
                    union { double value; F128Word bits; } input;
                    input.bits = cases[index].input;
                    f128_from_double(&output.image.value, input.value);
                }
                else
                {
                    union { float value; unsigned bits; } input;
                    input.bits = (unsigned)cases[index].input;
                    f128_from_float(&output.image.value, input.value);
                }
                if (output.image.words[0] != cases[index].low || output.image.words[1] != cases[index].high) result = 1;
                if (output.before[0] != 0x123456789abcdef0ull || output.before[1] != 0x23456789abcdef01ull ||
                    output.after[0] != 0x3456789abcdef012ull || output.after[1] != 0x456789abcdef0123ull) result = 2;
#ifdef BUSTER_F128_FENV
                if (f128_read_status() != (0x10u | cases[index].invalid)) result = 3;
                unsigned expected_control = (saved_control & ~0x03c00000u) | ((setting & 3u) << 22) | (setting >= 4 ? 0x03000000u : 0);
                if (f128_read_control() != expected_control) result = 4;
                f128_write_status(0x10);
#endif
                if (f128_sign(&output.image.value) != (int)(cases[index].high >> 63)) result = 5;
                // Raw binary128 signaling NaNs must retain both bits and FPSR.
                output.image.words[0] = 1;
                output.image.words[1] = 0xffff000000000000ull;
                if (f128_sign(&output.image.value) != 1 || output.image.words[0] != 1 || output.image.words[1] != 0xffff000000000000ull) result = 6;
#ifdef BUSTER_F128_FENV
                if (f128_read_status() != 0x10) result = 7;
#endif
            }
        }
    }
#ifdef BUSTER_F128_FENV
    f128_write_control(saved_control);
    f128_write_status(saved_status);
#endif
    return result;
}
#endif
#else
int main(void)
{
    return 0;
}
#endif
