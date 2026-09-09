#include <stdarg.h>
#include <stdio.h>
struct NativeVaResult { long long sum; long long count; };
struct NativeVaSmall1 { signed char value; };
struct NativeVaSmall2 { short value; };
struct NativeVaSmall4 { int value; };
struct NativeVaSmall8 { long long value; };
struct NativeVaIndirect { long long first; long long second; };
long long native_va_ints(int, ...);
double native_va_floats(double, int, ...);
long long native_va_named(long long, double, long long, double, long long, int, ...);
struct NativeVaResult native_va_result(int, ...);
long long native_va_small(int, ...);
long long native_va_indirect(int, ...);
struct NativeVaHfa4 { float a; float b; float c; float d; };
struct NativeVaHfa2 { double a; double b; };
double native_va_hfa(int, ...);
double native_va_hfa_overflow(double, double, double, double, double, double, double, int, ...);
int native_va_call_host(void);
long long native_va_host_ints(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    long long result = 0;
    for (int index = 1; index <= count; index += 1) { result += index * va_arg(ap, long long); }
    va_end(ap);
    return result;
}
double native_va_host_floats(double first, int count, ...)
{
    va_list ap;
    va_list copy;
    va_start(ap, count);
    va_copy(copy, ap);
    double result = first + 2 * va_arg(copy, double);
    result += 3 * va_arg(copy, double);
    for (int index = 1; index <= count; index += 1) { result += index * va_arg(ap, double); }
    va_end(copy);
    va_end(ap);
    return result;
}
long long native_va_host_named(long long a, double b, long long c, double d, long long e, int count, ...)
{
    va_list ap;
    va_start(ap, count);
    long long result = a + c + e + (long long)(b + d);
    for (int index = 0; index < count; index += 1) { result += va_arg(ap, long long); }
    va_end(ap);
    return result;
}
struct NativeVaResult native_va_host_result(int count, ...)
{
    va_list ap;
    va_start(ap, count);
    struct NativeVaResult result = {0, count};
    for (int index = 0; index < count; index += 1) { result.sum += va_arg(ap, long long); }
    va_end(ap);
    return result;
}
long long native_va_host_small(int bias, ...)
{
    va_list ap;
    va_start(ap, bias);
    struct NativeVaSmall1 a = va_arg(ap, struct NativeVaSmall1);
    struct NativeVaSmall2 b = va_arg(ap, struct NativeVaSmall2);
    struct NativeVaSmall4 c = va_arg(ap, struct NativeVaSmall4);
    struct NativeVaSmall8 d = va_arg(ap, struct NativeVaSmall8);
    long long last = va_arg(ap, long long);
    va_end(ap);
    return bias + last + d.value + c.value + b.value + a.value;
}
int main(void)
{
    int bad = native_va_ints(0) != 0;
    bad |= native_va_ints(7, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll) != 140;
    bad |= native_va_floats(1.5, 4, 2.5, 3.5, 4.5, 5.5) != 62.0;
    bad |= native_va_floats(1.5, 10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0) != 394.5;
    bad |= native_va_named(1ll, 2.0, 3ll, 4.0, 5ll, 5, 6ll, 7ll, 8ll, 9ll, 10ll) != 55;
    struct NativeVaResult result = native_va_result(5, 1ll, 2ll, 3ll, 4ll, 5ll);
    bad |= result.sum != 15 || result.count != 5;
    struct NativeVaSmall1 a = {-3}; struct NativeVaSmall2 b = {-301};
    struct NativeVaSmall4 c = {-70001}; struct NativeVaSmall8 d = {-0x123456789ll};
    bad |= native_va_small(17, a, b, c, d, 99ll) != (17ll - 3 - 301 - 70001 - 0x123456789ll + 99);
    struct NativeVaIndirect x = {2, 3}; struct NativeVaIndirect y = {5, 6}; struct NativeVaIndirect z = {7, 8};
    bad |= native_va_indirect(1, x, 4ll, y, z, 9ll) != 144;
    struct NativeVaHfa4 hfa4 = {1, 2, 3, 4};
    struct NativeVaHfa2 hfa2 = {5, 6};
    bad |= native_va_hfa(1, hfa4, hfa2, 7.0) != 99.0;
    bad |= native_va_hfa_overflow(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 1, hfa4, hfa2, 7.0) != 77.0;
    bad |= native_va_call_host();
    printf("native-variadic=%d\n", bad);
    return bad;
}
