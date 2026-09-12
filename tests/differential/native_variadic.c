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


#if (defined(__aarch64__) && !defined(__APPLE__) && !defined(_WIN32)) || defined(BUSTER_MACHINE_VA_ELF_AARCH64)
// These lists cross an independently compiled translation-unit boundary.
// A self-consistent private producer/consumer representation cannot pass.
typedef int NativeVaListReader(void*, int);
typedef int NativeVaListValueReader(va_list);
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

int native_va_list_read(void* storage, int scenario)
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

int native_va_list_produce(int scenario, NativeVaListReader* reader, ...)
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
int native_va_list_named(long long a, long long b, long long c, long long d,
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

// AAPCS64 va_list is a struct, not an array or pointer typedef. Passing it
// by value must consume the callee's private copy, as for a vprintf call.
int native_va_list_value(va_list ap)
{
    va_list copy;
    __builtin_va_copy(copy, ap);
    int bad = __builtin_va_arg(ap, long long) != 1;
    bad |= __builtin_va_arg(ap, double) != 2.5;
    bad |= __builtin_va_arg(ap, long long) != 3;
    bad |= __builtin_va_arg(copy, long long) != 1;
    __builtin_va_end(copy);
    return bad;
}
int native_va_list_pass_value(NativeVaListValueReader* reader, int tag, ...)
{
    va_list ap;
    __builtin_va_start(ap, tag);
    int bad = reader(ap);
    bad |= __builtin_va_arg(ap, long long) != 1;
    __builtin_va_end(ap);
    return bad;
}
#endif

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
