// Scalar IEEE binary128 transport at base AAPCS64 boundaries. This fixture
// intentionally performs no binary128 arithmetic or comparison: it verifies
// the complete sixteen-byte image through definitions, direct and indirect
// calls, Q0/Q1 placement, the ninth-argument stack slot, assignment and return.
#if __LDBL_MANT_DIG__ == 113

typedef unsigned long long F128Word;
typedef union F128Image F128Image;
union F128Image
{
    long double value;
    F128Word words[2];
};

typedef long double (*F128Unary)(long double);

#ifdef BUSTER_F128_TRANSPORT_CLIENT
long double f128_transport_identity(long double value);
long double f128_transport_assignment(long double value);
long double f128_transport_one(void);
long double f128_transport_second(long double first, long double second);
long double f128_transport_ninth(long double a0, long double a1, long double a2, long double a3, long double a4,
                                long double a5, long double a6, long double a7, long double a8);
#else
long double f128_transport_identity(long double value)
{
    return value;
}

long double f128_transport_assignment(long double value)
{
    long double copy = value;
    return copy;
}

long double f128_transport_one(void)
{
    return 1.0L;
}

long double f128_transport_second(long double first, long double second)
{
    (void)first;
    return second;
}

long double f128_transport_ninth(long double a0, long double a1, long double a2, long double a3, long double a4,
                                long double a5, long double a6, long double a7, long double a8)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    (void)a7;
    return a8;
}
#endif

#ifndef BUSTER_F128_TRANSPORT_LIBRARY
#define F128_CHECK(expression, expected_low, expected_high, code) \
    do \
    { \
        F128Image observed; \
        observed.value = (expression); \
        if (!result && (observed.words[0] != (expected_low) || observed.words[1] != (expected_high))) result = (code); \
    } while (0)

int main(void)
{
    int result = 0;
    F128Image first = {.words = {0x0123456789abcdefULL, 0x4000123456789abcULL}};
    F128Image second = {.words = {0xfedcba9876543210ULL, 0x3ffe23456789abcdULL}};
    F128Image negative_zero = {.words = {0, 0x8000000000000000ULL}};
    F128Image ninth = {.words = {0x0badf00dcafebeefULL, 0x4001abcddcba1234ULL}};

    F128_CHECK(f128_transport_identity(first.value), first.words[0], first.words[1], 1);
    F128_CHECK(f128_transport_assignment(negative_zero.value), negative_zero.words[0], negative_zero.words[1], 2);
    F128_CHECK(f128_transport_one(), 0, 0x3fff000000000000ULL, 3);
    F128_CHECK(f128_transport_second(first.value, second.value), second.words[0], second.words[1], 4);

    F128Unary indirect = f128_transport_identity;
    F128_CHECK(indirect(second.value), second.words[0], second.words[1], 5);

    F128_CHECK(f128_transport_ninth(first.value, second.value, negative_zero.value, first.value, second.value,
                                   negative_zero.value, first.value, second.value, ninth.value),
               ninth.words[0], ninth.words[1], 6);
    return result;
}
#endif

#else
#ifndef BUSTER_F128_TRANSPORT_LIBRARY
int main(void)
{
    return 0;
}
#endif
#endif
