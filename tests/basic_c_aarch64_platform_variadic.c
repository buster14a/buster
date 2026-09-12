// Darwin uses stacked anonymous arguments; Windows uses the integer argument
// image for every argument of a variadic function, including named FP values.
// Keep this source freestanding so driver tests can compile both object formats
// on every host. The differential wrapper adds independently compiled callees,
// callers and public va_list exchanges without changing the standalone case.
#ifndef BUSTER_PLATFORM_VA_EXTERNAL
#define BUSTER_PLATFORM_VA_EXTERNAL 0
#endif
#include "differential/aarch64_platform_variadic.h"

double platform_va_mixed(double named, int count, ...)
{
    PlatformVaList arguments;
    __builtin_va_start(arguments, count);
    double result = named;
    for (int index = 0; index < count; index += 1)
    {
        long long integer = __builtin_va_arg(arguments, long long);
        double real = __builtin_va_arg(arguments, double);
        result += (index + 1) * integer + (index + 2) * real;
    }
    __builtin_va_end(arguments);
    return result;
}

long long platform_va_copy(int count, ...)
{
    volatile unsigned long long before = 0x1234567887654321ull;
    PlatformVaList arguments;
    volatile unsigned long long middle = 0xfedcba9876543210ull;
    PlatformVaList copy;
    volatile unsigned long long after = 0x1122334455667788ull;
    __builtin_va_start(arguments, count);
    long long result = __builtin_va_arg(arguments, int);
    __builtin_va_copy(copy, arguments);
    for (int index = 0; index < count; index += 1)
    {
        result += (index + 1ll) * __builtin_va_arg(arguments, long long);
    }
    result += 13 * __builtin_va_arg(copy, long long);
    result += 17 * __builtin_va_arg(copy, long long);
    __builtin_va_end(copy);
    __builtin_va_end(arguments);
    if (before != 0x1234567887654321ull || middle != 0xfedcba9876543210ull || after != 0x1122334455667788ull)
    {
        result = -9999;
    }
    return result;
}

long long platform_va_named_stack(long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, long long h, double named, signed char narrow, int count, ...)
{
    PlatformVaList arguments;
    __builtin_va_start(arguments, count);
    long long result = a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
    result += (long long)named + narrow;
    for (int index = 0; index < count; index += 1)
    {
        result += (index + 1ll) * __builtin_va_arg(arguments, long long);
    }
    __builtin_va_end(arguments);
    return result;
}

double platform_va_named_hfa(struct PlatformVaHfa named, double scalar, int count, ...)
{
    PlatformVaList arguments;
    __builtin_va_start(arguments, count);
    double result = named.first + 2 * named.second + scalar;
    for (int index = 0; index < count; index += 1)
    {
        result += (index + 1) * __builtin_va_arg(arguments, double);
    }
    __builtin_va_end(arguments);
    return result;
}

long long platform_va_aggregates(int marker, ...)
{
    PlatformVaList arguments;
    __builtin_va_start(arguments, marker);
    struct PlatformVaPair pair = __builtin_va_arg(arguments, struct PlatformVaPair);
    struct PlatformVaHfa hfa = __builtin_va_arg(arguments, struct PlatformVaHfa);
    struct PlatformVaSmall1 a = __builtin_va_arg(arguments, struct PlatformVaSmall1);
    struct PlatformVaSmall2 b = __builtin_va_arg(arguments, struct PlatformVaSmall2);
    struct PlatformVaSmall4 c = __builtin_va_arg(arguments, struct PlatformVaSmall4);
    struct PlatformVaSmall8 d = __builtin_va_arg(arguments, struct PlatformVaSmall8);
    long long tail = __builtin_va_arg(arguments, long long);
    __builtin_va_end(arguments);
    return marker + pair.first + 2 * pair.second + (long long)(3 * hfa.first + 4 * hfa.second)
        + a.value + b.value + c.value + d.value + tail;
}

long long platform_va_stacked_pair(int count, ...)
{
    PlatformVaList arguments;
    __builtin_va_start(arguments, count);
    long long result = 0;
    for (int index = 0; index < count; index += 1)
    {
        result += (index + 1ll) * __builtin_va_arg(arguments, long long);
    }
    struct PlatformVaPair pair = __builtin_va_arg(arguments, struct PlatformVaPair);
    struct PlatformVaHfa hfa = __builtin_va_arg(arguments, struct PlatformVaHfa);
    long long tail = __builtin_va_arg(arguments, long long);
    __builtin_va_end(arguments);
    return result + 3 * pair.first + 5 * pair.second + (long long)(7 * hfa.first + 11 * hfa.second) + tail;
}

long long platform_va_fold(PlatformVaList arguments, int count)
{
    PlatformVaList copy;
    __builtin_va_copy(copy, arguments);
    long long result = 0;
    for (int index = 0; index < count; index += 1)
    {
        result += (index + 1ll) * __builtin_va_arg(copy, long long);
    }
    __builtin_va_end(copy);
    return result;
}

long long platform_va_advance(PlatformVaList* arguments)
{
    return __builtin_va_arg(*arguments, long long);
}

long long platform_va_forward(int count, ...)
{
    PlatformVaList arguments;
    __builtin_va_start(arguments, count);
    long long result = __builtin_va_arg(arguments, long long);
#if BUSTER_PLATFORM_VA_EXTERNAL
    result += platform_va_host_fold(arguments, count);
    result += 101 * platform_va_host_advance(&arguments);
#else
    result += platform_va_fold(arguments, count);
    result += 101 * platform_va_advance(&arguments);
#endif
    // Copying a by-value list preserves its cursor; advancing through a pointer
    // changes the original. The next read checks both public representations.
    if (__builtin_va_arg(arguments, long long) != 2)
    {
        result = -9999;
    }
    __builtin_va_end(arguments);
    return result;
}

int main(void)
{
    int bad = 0;
    bad |= platform_va_mixed(1.25, 0) != 1.25;
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
    long long aggregate_expected = 17ll + 2 + 6 + 15 + 24 - 3 - 301 - 70001 - 0x123456789ll + 99;
    bad |= platform_va_aggregates(17, pair, hfa, a, b, c, d, 99ll) != aggregate_expected;
    bad |= platform_va_stacked_pair(7, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, pair, hfa, 97ll) != 359;
    bad |= platform_va_forward(10, 37ll, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, 8ll, 9ll, 10ll) != 523;
#if BUSTER_PLATFORM_VA_EXTERNAL
    bad |= platform_va_host_mixed(1.25, 6, 1ll, 1.5, 2ll, 2.5, 3ll, 3.5, 4ll, 4.5, 5ll, 5.5, 6ll, 6.5) != 217.75;
    bad |= platform_va_host_copy(10, 37, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, 8ll, 9ll, 10ll) != 469;
    bad |= platform_va_host_named_stack(1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, 8ll, 9.0, -3, 3, 10ll, 11ll, 12ll) != 278;
    bad |= platform_va_host_named_hfa(hfa, 7.0, 6, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0) != 115.0;
    bad |= platform_va_host_aggregates(17, pair, hfa, a, b, c, d, 99ll) != aggregate_expected;
    bad |= platform_va_host_stacked_pair(7, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, pair, hfa, 97ll) != 359;
    bad |= platform_va_host_forward(10, 37ll, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, 8ll, 9ll, 10ll) != 523;
    bad |= platform_va_host_run();
#endif
    return bad;
}
