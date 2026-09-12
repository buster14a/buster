#include <fenv.h>
#include <float.h>
#include <string.h>

int signbit_image_float(float const* value);
int signbit_image_double(double const* value);
int signbit_image_long_double(long double const* value);

int main(void)
{
    int failed = 0;
    fenv_t saved;
    fegetenv(&saved);
    for (unsigned negative = 0; negative < 2; negative += 1)
    {
        // Zero, subnormal, infinity and signaling NaN have independent bit
        // expectations. No sign observation may alter the input or FP flags.
        unsigned patterns32[] = {0, 1, 0x7f800000u, 0x7f800001u};
        unsigned long long patterns64[] = {0, 1, 0x7ff0000000000000ull, 0x7ff0000000000001ull};
        for (unsigned index = 0; index < 4; index += 1)
        {
            union { float value; unsigned bits; } narrow;
            union { double value; unsigned long long bits; } wide;
            union { long double value; unsigned char bytes[sizeof(long double)]; } extended;
            narrow.bits = patterns32[index] | (negative << 31);
            wide.bits = patterns64[index] | ((unsigned long long)negative << 63);
            memset(&extended, 0, sizeof(extended));
#if LDBL_MANT_DIG == 53
            memcpy(&extended, &wide.bits, 8);
#elif LDBL_MANT_DIG == 64
            unsigned long long significand = index >= 2 ? 0x8000000000000000ull : 0;
            if (index == 1 || index == 3) significand |= 1;
            unsigned short exponent = (unsigned short)((negative << 15) | (index >= 2 ? 0x7fffu : 0));
            memcpy(extended.bytes, &significand, 8);
            memcpy(extended.bytes + 8, &exponent, 2);
#elif LDBL_MANT_DIG == 113
            unsigned long long low = index == 1 || index == 3 ? 1 : 0;
            unsigned long long high = ((unsigned long long)negative << 63) | (index >= 2 ? 0x7fff000000000000ull : 0);
            memcpy(extended.bytes, &low, 8);
            memcpy(extended.bytes + 8, &high, 8);
#else
#error unsupported long double oracle format
#endif
            unsigned char before[sizeof(extended)];
            memcpy(before, &extended, sizeof(extended));
            feclearexcept(FE_ALL_EXCEPT);
            feraiseexcept(FE_DIVBYZERO);
            failed |= (signbit_image_float(&narrow.value) != 0) != (int)negative;
            failed |= (signbit_image_double(&wide.value) != 0) != (int)negative;
            failed |= (signbit_image_long_double(&extended.value) != 0) != (int)negative;
            failed |= fetestexcept(FE_ALL_EXCEPT) != FE_DIVBYZERO;
            failed |= narrow.bits != (patterns32[index] | (negative << 31));
            failed |= wide.bits != (patterns64[index] | ((unsigned long long)negative << 63));
            failed |= memcmp(before, &extended, sizeof(extended)) != 0;
        }
    }
    fesetenv(&saved);
    return failed;
}
