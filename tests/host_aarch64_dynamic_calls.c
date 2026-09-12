#include "aarch64_dynamic_calls.h"
#include <stdarg.h>

long long dynamic_host_scalar(long long a, long long b, long long c, long long d,
                              long long e, long long f, long long g, long long h,
                              unsigned char byte, short half, int word, unsigned char* memory)
{
    long long result = a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h + 3*byte + word + *memory + half;
    *memory += 1;
    return result;
}
DynamicCallPair dynamic_host_pair(long long a, long long b, long long c, long long d,
                                  long long e, long long f, long long g, DynamicCallPair pair)
{
    DynamicCallPair result = {pair.high + a + b + c + d + e + f + g, pair.low};
    return result;
}
DynamicCallBig dynamic_host_big(long long a, long long b, long long c, long long d,
                                long long e, long long f, long long g, long long h, DynamicCallBig big)
{
    DynamicCallBig result = {big.c + a + b + c + d + e + f + g + h, big.a, big.b};
    return result;
}
double dynamic_host_float(double a, double b, double c, double d, double e, double f, double g, double h, double i)
{
    return a + b + c + d + e + f + g + h + i;
}
long long dynamic_host_variadic(int count, ...)
{
    va_list values;
    va_start(values, count);
    long long result = 0;
    for (int index = 1; index <= count; index += 1) { result += index * va_arg(values, long long); }
    va_end(values);
    return result;
}
int main(void)
{
    int sizes[] = {1, 15, 16, 17, 4097, 8193};
    int failed = 0;
    for (unsigned index = 0; index < sizeof(sizes) / sizeof(sizes[0]); index += 1)
    {
        failed |= dynamic_calls_scalar(sizes[index]);
        failed |= dynamic_calls_results(sizes[index]);
        failed |= dynamic_calls_variadic(sizes[index]);
    }
    return failed;
}
