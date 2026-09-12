#ifndef F80_U64_CLIENT
long double f80_u64_from(unsigned long long value) { return (long double)value; }
unsigned long long f80_u64_to(long double value) { return (unsigned long long)value; }
#else
long double f80_u64_from(unsigned long long value);
unsigned long long f80_u64_to(long double value);
#endif

#ifndef F80_U64_LIBRARY
typedef union F80UnsignedImage F80UnsignedImage;
union F80UnsignedImage
{
    long double value;
    struct { unsigned long long significand; unsigned short exponent; } bits;
};

int f80_u64_check(void)
{
    unsigned long long integers[] = {0, 1, 0x7fffffffffffffffULL, 0x8000000000000000ULL,
                                     0x8000000000000001ULL, 0xfffffffffffffffeULL, 0xffffffffffffffffULL};
    long double exact[] = {0.0L, 1.0L, 9223372036854775807.0L, 9223372036854775808.0L,
                           9223372036854775809.0L, 18446744073709551614.0L, 18446744073709551615.0L};
    long double fractional[] = {-0.999L, -0.0L, 0.0L, 0.999L, 1.999L, 9223372036854775807.5L,
                                9223372036854775808.0L, 9223372036854775809.0L, 18446744073709551615.0L};
    unsigned long long truncated[] = {0, 0, 0, 0, 1, 0x7fffffffffffffffULL,
                                      0x8000000000000000ULL, 0x8000000000000001ULL, 0xffffffffffffffffULL};
    int failed = 0;
    for (unsigned index = 0; index < sizeof(integers) / sizeof(integers[0]); index += 1)
    {
        long double converted = f80_u64_from(integers[index]);
        failed |= converted != exact[index] || f80_u64_to(converted) != integers[index];
    }
    for (unsigned index = 0; index < sizeof(fractional) / sizeof(fractional[0]); index += 1)
    {
        failed |= f80_u64_to(fractional[index]) != truncated[index];
    }
    F80UnsignedImage zero;
    zero.value = f80_u64_from(0);
    failed |= zero.bits.significand != 0 || zero.bits.exponent != 0;
    return failed;
}

#if defined(F80_U64_FENV)
int main(void)
{
    unsigned short saved, observed;
    __asm__ volatile("fnstcw %0" : "=m"(saved));
    int failed = 0;
    for (unsigned rounding = 0; rounding < 4; rounding += 1)
    {
        unsigned short expected = (unsigned short)((saved & ~0x0f00u) | 0x0300u | (rounding << 10));
        __asm__ volatile("fldcw %0" : : "m"(expected) : "memory");
        failed |= f80_u64_check();
        __asm__ volatile("fnstcw %0" : "=m"(observed));
        failed |= observed != expected;
    }
    __asm__ volatile("fldcw %0" : : "m"(saved) : "memory");
    return failed;
}
#else
int main(void) { return f80_u64_check(); }
#endif
#endif
