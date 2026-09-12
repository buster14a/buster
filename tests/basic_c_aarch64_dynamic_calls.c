#include "aarch64_dynamic_calls.h"

int dynamic_calls_scalar(int count)
{
    unsigned char before = 19;
    long long first = dynamic_host_scalar(1, 2, 3, 4, 5, 6, 7, 8, 201, -321, 45678, &before);
    unsigned char local[count];
    for (int index = 0; index < count; index += 1) { local[index] = (unsigned char)(index + 19); }
    int failed = first != 46183 || before != 20;
    long long (*volatile call)(long long, long long, long long, long long, long long, long long, long long, long long,
                               unsigned char, short, int, unsigned char*) = dynamic_host_scalar;
    for (int iteration = 0; iteration < 17; iteration += 1)
    {
        long long value = call(1, 2, 3, 4, 5, 6, 7, 8, 201, -321, 45678, local);
        failed |= value != 46183 + iteration;
    }
    failed |= local[0] != 36;
    for (int index = 1; index < count; index += 1) { failed |= local[index] != (unsigned char)(index + 19); }
    return failed;
}

int dynamic_calls_results(int count)
{
    unsigned char local[count];
    for (int index = 0; index < count; index += 1) { local[index] = (unsigned char)(index + 23); }
    DynamicCallPair pair = {0x0123456789abcdefULL, 0xfedcba9876543210ULL};
    DynamicCallBig big = {0x1122334455667788ULL, 0x8877665544332211ULL, 0x123456789abcdef0ULL};
    DynamicCallPair pair_result = dynamic_host_pair(1, 2, 3, 4, 5, 6, 7, pair);
    DynamicCallBig big_result = dynamic_host_big(1, 2, 3, 4, 5, 6, 7, 8, big);
    double floating = dynamic_host_float(1.25, 2.5, 3.75, 4.0, 5.25, 6.5, 7.75, 8.0, 9.25);
    int failed = pair_result.low != pair.high + 28 || pair_result.high != pair.low;
    failed |= big_result.a != big.c + 36 || big_result.b != big.a || big_result.c != big.b;
    failed |= floating != 48.25;
    for (int index = 0; index < count; index += 1) { failed |= local[index] != (unsigned char)(index + 23); }
    return failed;
}

int dynamic_calls_variadic(int count)
{
    unsigned char local[count];
    for (int index = 0; index < count; index += 1) { local[index] = (unsigned char)(index + 41); }
    long long value = dynamic_host_variadic(10, 1LL, 2LL, 3LL, 4LL, 5LL, 6LL, 7LL, 8LL, 9LL, 10LL);
    int failed = value != 385;
    for (int index = 0; index < count; index += 1) { failed |= local[index] != (unsigned char)(index + 41); }
    return failed;
}
