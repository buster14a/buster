typedef __builtin_va_list va_list;
struct NativeVaResult { long long sum; long long count; };
struct NativeVaSmall1 { signed char value; };
struct NativeVaSmall2 { short value; };
struct NativeVaSmall4 { int value; };
struct NativeVaSmall8 { long long value; };
struct NativeVaIndirect { long long first; long long second; };
long long native_va_ints(int count, ...)
{
    va_list arguments;
    __builtin_va_start(arguments, count);
    long long result = 0;
    for (int index = 0; index < count; index += 1) { result += (index + 1ll) * __builtin_va_arg(arguments, long long); }
    __builtin_va_end(arguments);
    return result;
}
double native_va_floats(double first, int count, ...)
{
    volatile unsigned long long before;
    va_list cursor;
    volatile unsigned long long middle;
    va_list copy;
    volatile unsigned long long after;
    before = 0x1234567887654321ull;
    middle = 0xfedcba9876543210ull;
    after = 0x1122334455667788ull;
    __builtin_va_start(cursor, count);
    __builtin_va_copy(copy, cursor);
    double result = first;
    for (int index = 0; index < count; index += 1) { result += (index + 1) * __builtin_va_arg(cursor, double); }
    result += 2 * __builtin_va_arg(copy, double);
    result += 3 * __builtin_va_arg(copy, double);
    __builtin_va_end(copy);
    __builtin_va_end(cursor);
    if (before != 0x1234567887654321ull || middle != 0xfedcba9876543210ull || after != 0x1122334455667788ull) { result = -9999; }
    return result;
}
long long native_va_named(long long a, double b, long long c, double d, long long e, int count, ...)
{
    va_list arguments;
    __builtin_va_start(arguments, count);
    long long result = a + (long long)b + c + (long long)d + e;
    for (int index = 0; index < count; index += 1) { result += __builtin_va_arg(arguments, long long); }
    __builtin_va_end(arguments);
    return result;
}
struct NativeVaResult native_va_result(int count, ...)
{
    va_list arguments;
    __builtin_va_start(arguments, count);
    struct NativeVaResult result = {0, count};
    for (int index = 0; index < count; index += 1) { result.sum += __builtin_va_arg(arguments, long long); }
    __builtin_va_end(arguments);
    return result;
}
long long native_va_small(int bias, ...)
{
    va_list arguments;
    __builtin_va_start(arguments, bias);
    struct NativeVaSmall1 a = __builtin_va_arg(arguments, struct NativeVaSmall1);
    struct NativeVaSmall2 b = __builtin_va_arg(arguments, struct NativeVaSmall2);
    struct NativeVaSmall4 c = __builtin_va_arg(arguments, struct NativeVaSmall4);
    struct NativeVaSmall8 d = __builtin_va_arg(arguments, struct NativeVaSmall8);
    long long last = __builtin_va_arg(arguments, long long);
    __builtin_va_end(arguments);
    return bias + a.value + b.value + c.value + d.value + last;
}
long long native_va_indirect(int bias, ...)
{
    va_list arguments;
    __builtin_va_start(arguments, bias);
    struct NativeVaIndirect a = __builtin_va_arg(arguments, struct NativeVaIndirect);
    long long first = __builtin_va_arg(arguments, long long);
    struct NativeVaIndirect b = __builtin_va_arg(arguments, struct NativeVaIndirect);
    struct NativeVaIndirect c = __builtin_va_arg(arguments, struct NativeVaIndirect);
    long long last = __builtin_va_arg(arguments, long long);
    __builtin_va_end(arguments);
    return bias + a.first + 2 * a.second + first + 3 * b.first + 4 * b.second + 5 * c.first + 6 * c.second + last;
}

#ifndef BUSTER_MACHINE_VA_TEST
long long native_va_host_ints(int, ...);
double native_va_host_floats(double, int, ...);
long long native_va_host_named(long long, double, long long, double, long long, int, ...);
struct NativeVaResult native_va_host_result(int, ...);
long long native_va_host_small(int, ...);
int native_va_call_host(void)
{
    int bad = native_va_host_ints(7, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll) != 140;
    bad |= native_va_host_floats(1.5, 4, 2.5, 3.5, 4.5, 5.5) != 62.0;
    bad |= native_va_host_named(1ll, 2.0, 3ll, 4.0, 5ll, 5, 6ll, 7ll, 8ll, 9ll, 10ll) != 55;
    struct NativeVaResult result = native_va_host_result(5, 1ll, 2ll, 3ll, 4ll, 5ll);
    bad |= result.sum != 15 || result.count != 5;
    struct NativeVaSmall1 a = {-3};
    struct NativeVaSmall2 b = {-301};
    struct NativeVaSmall4 c = {-70001};
    struct NativeVaSmall8 d = {-0x123456789ll};
    bad |= native_va_host_small(17, a, b, c, d, 99ll) != (17ll - 3 - 301 - 70001 - 0x123456789ll + 99);
    return bad;
}
#endif
