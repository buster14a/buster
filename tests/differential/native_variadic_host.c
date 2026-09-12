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

#if defined(__aarch64__) && !defined(__APPLE__) && !defined(_WIN32)
// These lists cross an independently compiled translation-unit boundary.
// A self-consistent private producer/consumer representation cannot pass.
typedef int NativeVaListReader(void*, int);
typedef int NativeVaListValueReader(va_list, void*);
struct NativeVaPublicLayout
{
    void* stack;
    void* gr_top;
    void* vr_top;
    int gr_offs;
    int vr_offs;
};
struct NativeVaListCopies
{
    volatile unsigned long long before;
    va_list ap;
    volatile unsigned long long between;
    va_list copy;
    volatile unsigned long long after;
};

int host_va_list_read(void* storage, int scenario)
{
    va_list* ap = (va_list*)storage;
    struct NativeVaPublicLayout image;
    unsigned char* bytes = (unsigned char*)&image;
    unsigned char* source = (unsigned char*)storage;
    for (unsigned long index = 0; index < sizeof(image); index += 1) { bytes[index] = source[index]; }
    int named_stack = scenario & 256;
    int bad = sizeof(va_list) != 32 || _Alignof(va_list) != 8;
    bad |= (unsigned long)image.stack < 4096 || ((unsigned long)image.stack & 7) != 0;
    if (!named_stack) { bad |= (unsigned long)image.gr_top < 4096 || ((unsigned long)image.gr_top & 7) != 0; }
    if (!named_stack) { bad |= (unsigned long)image.vr_top < 4096 || ((unsigned long)image.vr_top & 15) != 0; }
    bad |= image.gr_offs != (named_stack ? 0 : -48) || image.vr_offs != (named_stack ? 0 : -128);
    scenario &= 255;
    if (!bad)
    {
        if (scenario == 0)
        {
            for (int index = 1; index <= 12; index += 1)
            {
                bad |= __builtin_va_arg(*ap, long long) != index;
                bad |= __builtin_va_arg(*ap, double) != index + 0.5;
                if (index == 5)
                {
                    va_list copy;
                    __builtin_va_copy(copy, *ap);
                    bad |= __builtin_va_arg(copy, long long) != 6;
                    bad |= __builtin_va_arg(copy, double) != 6.5;
                    __builtin_va_end(copy);
                }
            }
        }
        else if (scenario == 1)
        {
            bad |= __builtin_va_arg(*ap, long long) != 1;
            struct NativeVaHfa4 a = __builtin_va_arg(*ap, struct NativeVaHfa4);
            struct NativeVaHfa2 b = __builtin_va_arg(*ap, struct NativeVaHfa2);
            bad |= a.a != 1 || a.b != 2 || a.c != 3 || a.d != 4 || b.a != 5 || b.b != 6;
            bad |= __builtin_va_arg(*ap, double) != 7.5;
        }
        else if (scenario == 2)
        {
            for (int index = 1; index <= 7; index += 1) { bad |= __builtin_va_arg(*ap, long long) != index; }
            unsigned __int128 pair = __builtin_va_arg(*ap, unsigned __int128);
            bad |= (unsigned long long)pair != 0x123456789abcdef0ull || (unsigned long long)(pair >> 64) != 0xfedcba9876543210ull;
            bad |= __builtin_va_arg(*ap, long long) != 8;
            bad |= __builtin_va_arg(*ap, double) != 9.5;
        }
        else if (scenario == 3)
        {
            bad |= __builtin_va_arg(*ap, long long) != 1;
            for (int index = 1; index <= 7; index += 1) { bad |= __builtin_va_arg(*ap, double) != index + 0.5; }
            struct NativeVaHfa4 a = __builtin_va_arg(*ap, struct NativeVaHfa4);
            bad |= a.a != 1 || a.b != 2 || a.c != 3 || a.d != 4;
            bad |= __builtin_va_arg(*ap, double) != 8.5;
            bad |= __builtin_va_arg(*ap, long long) != 8;
        }
        else if (scenario == 4)
        {
            bad |= __builtin_va_arg(*ap, long long) != 1;
            unsigned __int128 first = __builtin_va_arg(*ap, unsigned __int128);
            bad |= (unsigned long long)first != 0x123456789abcdef0ull || (unsigned long long)(first >> 64) != 0xfedcba9876543210ull;
            bad |= __builtin_va_arg(*ap, long long) != 3;
            unsigned __int128 second = __builtin_va_arg(*ap, unsigned __int128);
            bad |= (unsigned long long)second != 0x9876543210abcdefull || (unsigned long long)(second >> 64) != 0x1122334455667788ull;
            bad |= __builtin_va_arg(*ap, long long) != 8;
            bad |= __builtin_va_arg(*ap, double) != 9.5;
        }
        else { bad = 1; }
    }
    return bad;
}

int host_va_list_produce(int scenario, NativeVaListReader* reader, ...)
{
    struct NativeVaListCopies lists;
    lists.before = 0x1234567887654321ull;
    lists.between = 0xfedcba9876543210ull;
    lists.after = 0x1122334455667788ull;
    __builtin_va_start(lists.ap, reader);
    __builtin_va_copy(lists.copy, lists.ap);
    int bad = reader(&lists.ap, scenario);
    bad |= __builtin_va_arg(lists.ap, long long) != 99;
    bad |= __builtin_va_arg(lists.copy, long long) != 1;
    __builtin_va_end(lists.copy);
    __builtin_va_end(lists.ap);
    bad |= lists.before != 0x1234567887654321ull || lists.between != 0xfedcba9876543210ull || lists.after != 0x1122334455667788ull;
    return bad;
}

// Seven named GP scalars leave only X7. The named pair does not fit and
// closes the file; the marker and reader must not reuse X7 afterwards.
int host_va_list_named(long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, struct NativeVaIndirect pair, int marker,
    double f0, double f1, double f2, double f3, double f4, double f5, double f6, double f7, double f8,
    NativeVaListReader* reader, ...)
{
    va_list ap;
    __builtin_va_start(ap, reader);
    int bad = a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6 || g != 7;
    bad |= pair.first != 8 || pair.second != 9 || marker != 10;
    bad |= f0 != 1 || f1 != 2 || f2 != 3 || f3 != 4 || f4 != 5 || f5 != 6 || f6 != 7 || f7 != 8 || f8 != 9;
    bad |= reader(&ap, 256);
    bad |= __builtin_va_arg(ap, long long) != 99;
    __builtin_va_end(ap);
    return bad;
}

// AAPCS64 passes va_list as a struct with distinct callee storage. The
// caller passes a separate copy and only ends that consumed list afterwards.
int host_va_list_value(va_list ap, void* caller_list)
{
    va_list copy;
    __builtin_va_copy(copy, ap);
    int bad = (void*)&ap == caller_list;
    bad |= __builtin_va_arg(ap, long long) != 1;
    bad |= __builtin_va_arg(ap, double) != 2.5;
    bad |= __builtin_va_arg(ap, long long) != 3;
    bad |= __builtin_va_arg(copy, long long) != 1;
    __builtin_va_end(copy);
    return bad;
}
int host_va_list_pass_value(NativeVaListValueReader* reader, int tag, ...)
{
    va_list ap;
    va_list passed;
    __builtin_va_start(ap, tag);
    __builtin_va_copy(passed, ap);
    int bad = reader(passed, &passed);
    __builtin_va_end(passed);
    bad |= __builtin_va_arg(ap, long long) != 1;
    __builtin_va_end(ap);
    return bad;
}

int native_va_list_read(void*, int);
int native_va_list_produce(int, NativeVaListReader*, ...);
int native_va_list_named(long long, long long, long long, long long, long long, long long, long long,
    struct NativeVaIndirect, int, double, double, double, double, double, double, double, double, double, NativeVaListReader*, ...);
int native_va_list_value(va_list, void*);
int native_va_list_pass_value(NativeVaListValueReader*, int, ...);
static int native_va_public_test(void)
{
    struct NativeVaHfa4 hfa = {1, 2, 3, 4};
    struct NativeVaHfa2 hfa2 = {5, 6};
    struct NativeVaIndirect named = {8, 9};
    unsigned __int128 first = ((unsigned __int128)0xfedcba9876543210ull << 64) | 0x123456789abcdef0ull;
    unsigned __int128 second = ((unsigned __int128)0x1122334455667788ull << 64) | 0x9876543210abcdefull;
    int bad = 0;
    bad |= native_va_list_produce(0, host_va_list_read, 1ll, 1.5, 2ll, 2.5, 3ll, 3.5, 4ll, 4.5, 5ll, 5.5, 6ll, 6.5, 7ll, 7.5, 8ll, 8.5, 9ll, 9.5, 10ll, 10.5, 11ll, 11.5, 12ll, 12.5, 99ll);
    bad |= native_va_list_produce(1, host_va_list_read, 1ll, hfa, hfa2, 7.5, 99ll);
    bad |= native_va_list_produce(2, host_va_list_read, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, first, 8ll, 9.5, 99ll);
    bad |= native_va_list_produce(3, host_va_list_read, 1ll, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, hfa, 8.5, 8ll, 99ll);
    bad |= native_va_list_produce(4, host_va_list_read, 1ll, first, 3ll, second, 8ll, 9.5, 99ll);
    bad |= native_va_list_named(1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, named, 10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, host_va_list_read, 1ll, 1.5, 2ll, 2.5, 3ll, 3.5, 4ll, 4.5, 5ll, 5.5, 6ll, 6.5, 7ll, 7.5, 8ll, 8.5, 9ll, 9.5, 10ll, 10.5, 11ll, 11.5, 12ll, 12.5, 99ll);
    bad |= native_va_list_pass_value(host_va_list_value, 0, 1ll, 2.5, 3ll);
    bad |= host_va_list_produce(0, native_va_list_read, 1ll, 1.5, 2ll, 2.5, 3ll, 3.5, 4ll, 4.5, 5ll, 5.5, 6ll, 6.5, 7ll, 7.5, 8ll, 8.5, 9ll, 9.5, 10ll, 10.5, 11ll, 11.5, 12ll, 12.5, 99ll);
    bad |= host_va_list_produce(1, native_va_list_read, 1ll, hfa, hfa2, 7.5, 99ll);
    bad |= host_va_list_produce(2, native_va_list_read, 1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, first, 8ll, 9.5, 99ll);
    bad |= host_va_list_produce(3, native_va_list_read, 1ll, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, hfa, 8.5, 8ll, 99ll);
    bad |= host_va_list_produce(4, native_va_list_read, 1ll, first, 3ll, second, 8ll, 9.5, 99ll);
    bad |= host_va_list_named(1ll, 2ll, 3ll, 4ll, 5ll, 6ll, 7ll, named, 10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, native_va_list_read, 1ll, 1.5, 2ll, 2.5, 3ll, 3.5, 4ll, 4.5, 5ll, 5.5, 6ll, 6.5, 7ll, 7.5, 8ll, 8.5, 9ll, 9.5, 10ll, 10.5, 11ll, 11.5, 12ll, 12.5, 99ll);
    bad |= host_va_list_pass_value(native_va_list_value, 0, 1ll, 2.5, 3ll);
    return bad;
}
#endif

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
#if defined(__aarch64__) && !defined(__APPLE__) && !defined(_WIN32)
    bad |= native_va_public_test();
#endif
    printf("native-variadic=%d\n", bad);
    return bad;
}
