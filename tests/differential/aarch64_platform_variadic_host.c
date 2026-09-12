// Independently compiled producers and consumers validate both call directions
// and actual public va_list values. No platform SDK is needed to build objects.
#define BUSTER_PLATFORM_VA_EXTERNAL 1
#include "aarch64_platform_variadic.h"

double platform_va_host_mixed(double named, int count, ...)
{
    PlatformVaList cursor;
    __builtin_va_start(cursor, count);
    double sum = named;
    for (int weight = 1; weight <= count; weight += 1)
    {
        sum += weight * __builtin_va_arg(cursor, long long);
        sum += (weight + 1) * __builtin_va_arg(cursor, double);
    }
    __builtin_va_end(cursor);
    return sum;
}

long long platform_va_host_copy(int count, ...)
{
    PlatformVaList cursor;
    PlatformVaList saved;
    __builtin_va_start(cursor, count);
    long long sum = __builtin_va_arg(cursor, int);
    __builtin_va_copy(saved, cursor);
    sum += 13 * __builtin_va_arg(saved, long long);
    sum += 17 * __builtin_va_arg(saved, long long);
    for (int weight = 1; weight <= count; weight += 1)
    {
        sum += weight * __builtin_va_arg(cursor, long long);
    }
    __builtin_va_end(saved);
    __builtin_va_end(cursor);
    return sum;
}

long long platform_va_host_named_stack(long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, long long h, double named, signed char narrow, int count, ...)
{
    PlatformVaList cursor;
    __builtin_va_start(cursor, count);
    long long sum = narrow + (long long)named + 8 * h + 7 * g + 6 * f + 5 * e + 4 * d + 3 * c + 2 * b + a;
    for (int weight = 1; weight <= count; weight += 1)
    {
        sum += weight * __builtin_va_arg(cursor, long long);
    }
    __builtin_va_end(cursor);
    return sum;
}

double platform_va_host_named_hfa(struct PlatformVaHfa named, double scalar, int count, ...)
{
    PlatformVaList cursor;
    __builtin_va_start(cursor, count);
    double sum = scalar + 2 * named.second + named.first;
    for (int weight = 1; weight <= count; weight += 1)
    {
        sum += weight * __builtin_va_arg(cursor, double);
    }
    __builtin_va_end(cursor);
    return sum;
}

long long platform_va_host_aggregates(int marker, ...)
{
    PlatformVaList cursor;
    __builtin_va_start(cursor, marker);
    struct PlatformVaPair pair = __builtin_va_arg(cursor, struct PlatformVaPair);
    struct PlatformVaHfa hfa = __builtin_va_arg(cursor, struct PlatformVaHfa);
    long long sum = marker + pair.first + 2 * pair.second + (long long)(3 * hfa.first + 4 * hfa.second);
    struct PlatformVaSmall1 a = __builtin_va_arg(cursor, struct PlatformVaSmall1);
    sum += a.value;
    struct PlatformVaSmall2 b = __builtin_va_arg(cursor, struct PlatformVaSmall2);
    sum += b.value;
    struct PlatformVaSmall4 c = __builtin_va_arg(cursor, struct PlatformVaSmall4);
    sum += c.value;
    struct PlatformVaSmall8 d = __builtin_va_arg(cursor, struct PlatformVaSmall8);
    sum += d.value;
    sum += __builtin_va_arg(cursor, long long);
    __builtin_va_end(cursor);
    return sum;
}

long long platform_va_host_stacked_pair(int count, ...)
{
    PlatformVaList cursor;
    __builtin_va_start(cursor, count);
    long long sum = 0;
    for (int weight = 1; weight <= count; weight += 1)
    {
        sum += weight * __builtin_va_arg(cursor, long long);
    }
    struct PlatformVaPair pair = __builtin_va_arg(cursor, struct PlatformVaPair);
    sum += pair.first * 3 + pair.second * 5;
    struct PlatformVaHfa hfa = __builtin_va_arg(cursor, struct PlatformVaHfa);
    sum += (long long)(hfa.first * 7 + hfa.second * 11);
    sum += __builtin_va_arg(cursor, long long);
    __builtin_va_end(cursor);
    return sum;
}

long long platform_va_host_fold(PlatformVaList arguments, int count)
{
    PlatformVaList cursor;
    __builtin_va_copy(cursor, arguments);
    long long sum = 0;
    for (int weight = 1; weight <= count; weight += 1)
    {
        sum += weight * __builtin_va_arg(cursor, long long);
    }
    __builtin_va_end(cursor);
    return sum;
}

long long platform_va_host_advance(PlatformVaList* arguments)
{
    return __builtin_va_arg(*arguments, long long);
}

long long platform_va_host_forward(int count, ...)
{
    PlatformVaList cursor;
    __builtin_va_start(cursor, count);
    long long sum = __builtin_va_arg(cursor, long long);
    sum += platform_va_fold(cursor, count);
    sum += 101 * platform_va_advance(&cursor);
    if (__builtin_va_arg(cursor, long long) != 2)
    {
        sum = -9999;
    }
    __builtin_va_end(cursor);
    return sum;
}

int platform_va_host_run(void)
{
    int bad = platform_va_mixed(1.25, 0) != 1.25;
    bad |= platform_va_mixed(1.25, 6, 1ll, 1.5, 2ll, 2.5, 3ll, 3.5, 4ll, 4.5, 5ll, 5.5, 6ll, 6.5) != 217.75;
    bad |= platform_va_copy(10, 37, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, 8ll, 9ll, 10ll) != 469;
    bad |= platform_va_named_stack(1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, 8ll, 9.0, -3, 3, 10ll, 11ll, 12ll) != 278;
    struct PlatformVaHfa hfa = {5.0, 6.0};
    bad |= platform_va_named_hfa(hfa, 7.0, 6, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0) != 115.0;
    struct PlatformVaPair pair = {2, 3};
    struct PlatformVaSmall1 a = {-3};
    struct PlatformVaSmall2 b = {-301};
    struct PlatformVaSmall4 c = {-70001};
    struct PlatformVaSmall8 d = {-0x123456789ll};
    bad |= platform_va_aggregates(17, pair, hfa, a, b, c, d, 99ll)
        != 17ll + 2 + 6 + 15 + 24 - 3 - 301 - 70001 - 0x123456789ll + 99;
    bad |= platform_va_stacked_pair(7, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, pair, hfa, 97ll) != 359;
    bad |= platform_va_forward(10, 37ll, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, 8ll, 9ll, 10ll) != 523;
    return bad;
}
