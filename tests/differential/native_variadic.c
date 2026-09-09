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

struct NativeVaHfa4 { float a; float b; float c; float d; };
struct NativeVaHfa2 { double a; double b; };
double native_va_hfa(int marker, ...)
{
    va_list ap;
    __builtin_va_start(ap, marker);
    struct NativeVaHfa4 a = __builtin_va_arg(ap, struct NativeVaHfa4);
    struct NativeVaHfa2 b = __builtin_va_arg(ap, struct NativeVaHfa2);
    double tail = __builtin_va_arg(ap, double);
    __builtin_va_end(ap);
    return marker + a.a + 2 * a.b + 3 * a.c + 4 * a.d + 5 * b.a + 6 * b.b + tail;
}
double native_va_hfa_overflow(double a, double b, double c, double d, double e, double f, double g, int marker, ...)
{
    va_list ap;
    __builtin_va_start(ap, marker);
    struct NativeVaHfa4 first = __builtin_va_arg(ap, struct NativeVaHfa4);
    struct NativeVaHfa2 second = __builtin_va_arg(ap, struct NativeVaHfa2);
    double tail = __builtin_va_arg(ap, double);
    __builtin_va_end(ap);
    return a + b + c + d + e + f + g + marker + first.a + 2 * first.b + 3 * first.c + 4 * first.d + second.a + second.b + tail;
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
    bad |= native_va_host_floats(1.5, 10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0) != 394.5;
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
