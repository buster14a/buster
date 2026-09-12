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
struct NativeVaHfa3d { double a; double b; double c; };
struct NativeVaHfa4d { double a; double b; double c; double d; };
struct NativeVaLarge { long long a; long long b; long long c; };
struct NativeVaOdd { unsigned char bytes[17]; };
struct NativeVaAligned { _Alignas(32) long long a; long long b; long long c; };
typedef int NativeVaVector8 __attribute__((vector_size(8)));
typedef int NativeVaVector16 __attribute__((vector_size(16)));
typedef float NativeVaVector16f __attribute__((vector_size(16)));
struct NativeVaMixedHva { NativeVaVector16 a; NativeVaVector16f b; };
struct NativeVaHva2 { NativeVaVector16 a; NativeVaVector16 b; };
struct NativeVaHva4 { NativeVaVector8 a; NativeVaVector8 b; NativeVaVector8 c; NativeVaVector8 d; };

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

int native_va_list_read(void* storage, int scenario)
{
    va_list* ap = (va_list*)storage;
    struct NativeVaPublicLayout image;
    unsigned char* bytes = (unsigned char*)&image;
    unsigned char* source = (unsigned char*)storage;
    for (unsigned long index = 0; index < sizeof(image); index += 1) { bytes[index] = source[index]; }
    int named_stack = scenario & 256;
    int named_vector = scenario & 512;
    int bad = sizeof(va_list) != 32 || _Alignof(va_list) != 8;
    bad |= (unsigned long)image.stack < 4096 || ((unsigned long)image.stack & 7) != 0;
    if (!named_stack) { bad |= (unsigned long)image.gr_top < 4096 || ((unsigned long)image.gr_top & 7) != 0; }
    if (!named_stack && !named_vector) { bad |= (unsigned long)image.vr_top < 4096 || ((unsigned long)image.vr_top & 15) != 0; }
    bad |= image.gr_offs != (named_stack ? 0 : -48) || image.vr_offs != (named_stack || named_vector ? 0 : -128);
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
        else if (scenario == 5)
        {
            bad |= __builtin_va_arg(*ap, long long) != 1;
            struct NativeVaHfa3d a = __builtin_va_arg(*ap, struct NativeVaHfa3d);
            struct NativeVaHfa4d b = __builtin_va_arg(*ap, struct NativeVaHfa4d);
            struct NativeVaHfa3d c = __builtin_va_arg(*ap, struct NativeVaHfa3d);
            bad |= a.a != 2 || a.b != 3 || a.c != 4;
            bad |= b.a != 5 || b.b != 6 || b.c != 7 || b.d != 8;
            bad |= c.a != 2 || c.b != 3 || c.c != 4;
            bad |= __builtin_va_arg(*ap, double) != 9.5;
        }
        else if (scenario == 6)
        {
            bad |= __builtin_va_arg(*ap, long long) != 1;
            struct NativeVaLarge a = __builtin_va_arg(*ap, struct NativeVaLarge);
            bad |= a.a != 17 || a.b != 18 || a.c != 19;
            for (int index = 2; index <= 6; index += 1) { bad |= __builtin_va_arg(*ap, long long) != index; }
            struct NativeVaOdd odd = __builtin_va_arg(*ap, struct NativeVaOdd);
            struct NativeVaAligned aligned = __builtin_va_arg(*ap, struct NativeVaAligned);
            bad |= odd.bytes[0] != 31 || odd.bytes[16] != 47;
            bad |= aligned.a != 21 || aligned.b != 22 || aligned.c != 23;
            bad |= __builtin_va_arg(*ap, double) != 9.5;
        }
        else if (scenario == 7)
        {
            bad |= __builtin_va_arg(*ap, long long) != 1;
            NativeVaVector8 a = __builtin_va_arg(*ap, NativeVaVector8);
            NativeVaVector16 b = __builtin_va_arg(*ap, NativeVaVector16);
            struct NativeVaHva2 c = __builtin_va_arg(*ap, struct NativeVaHva2);
            struct NativeVaHva4 d = __builtin_va_arg(*ap, struct NativeVaHva4);
            bad |= a[0] != 2 || a[1] != 3;
            bad |= b[0] != 4 || b[1] != 5 || b[2] != 6 || b[3] != 7;
            bad |= c.a[0] != 4 || c.a[3] != 7 || c.b[0] != 8 || c.b[3] != 11;
            bad |= d.a[0] != 2 || d.a[1] != 3 || d.b[0] != 12 || d.b[1] != 13;
            bad |= d.c[0] != 14 || d.c[1] != 15 || d.d[0] != 16 || d.d[1] != 17;
            bad |= __builtin_va_arg(*ap, double) != 9.5;
            struct NativeVaHva2 spill = __builtin_va_arg(*ap, struct NativeVaHva2);
            bad |= spill.a[0] != 4 || spill.a[3] != 7 || spill.b[0] != 8 || spill.b[3] != 11;
            bad |= __builtin_va_arg(*ap, double) != 10.5;
            struct NativeVaMixedHva mixed = __builtin_va_arg(*ap, struct NativeVaMixedHva);
            bad |= mixed.a[0] != 4 || mixed.a[3] != 7 || mixed.b[0] != 18.5 || mixed.b[3] != 21.5;
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


struct NativeVaHva2 native_va_hva_identity(struct NativeVaHva2 value)
{
    return value;
}

int native_va_list_call_extended(NativeVaListReader* reader)
{
    struct NativeVaHfa3d hfa3 = {2, 3, 4};
    struct NativeVaHfa4d hfa4 = {5, 6, 7, 8};
    struct NativeVaLarge large = {17, 18, 19};
    struct NativeVaOdd odd = {{31}};
    odd.bytes[16] = 47;
    struct NativeVaAligned aligned = {21, 22, 23};
    NativeVaVector8 v8 = {2, 3};
    NativeVaVector16 v16 = {4, 5, 6, 7};
    struct NativeVaHva2 hva2 = {{4, 5, 6, 7}, {8, 9, 10, 11}};
    hva2 = native_va_hva_identity(hva2);
    struct NativeVaHva4 hva4 = {{2, 3}, {12, 13}, {14, 15}, {16, 17}};
    struct NativeVaMixedHva mixed = {{4, 5, 6, 7}, {18.5, 19.5, 20.5, 21.5}};
    int bad = native_va_list_produce(5, reader, 1ll, hfa3, hfa4, hfa3, 9.5, 99ll);
    bad |= native_va_list_produce(6, reader, 1ll, large, 2ll, 3ll, 4ll, 5ll, 6ll, odd, aligned, 9.5, 99ll);
    bad |= native_va_list_produce(7, reader, 1ll, v8, v16, hva2, hva4, 9.5, hva2, 10.5, mixed, 99ll);
    bad |= odd.bytes[0] != 31 || odd.bytes[16] != 47 || aligned.c != 23;
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

// Eight named vectors fill the V file. The ninth double occupies eight
// stack bytes, then the named HVA must align before the anonymous tail.
int native_va_list_named_vector(NativeVaVector16 v0, NativeVaVector16 v1, NativeVaVector16 v2, NativeVaVector16 v3,
    NativeVaVector16 v4, NativeVaVector16 v5, NativeVaVector16 v6, NativeVaVector16 v7,
    double first_stack, struct NativeVaHva2 pair, NativeVaListReader* reader, int marker, ...)
{
    va_list ap;
    __builtin_va_start(ap, marker);
    int bad = v0[0] != 4 || v1[1] != 5 || v2[2] != 6 || v3[3] != 7 || v4[0] != 4 || v5[1] != 5 || v6[2] != 6 || v7[3] != 7;
    bad |= first_stack != 22.5 || pair.a[0] != 4 || pair.a[3] != 7 || pair.b[0] != 8 || pair.b[3] != 11 || marker != 23;
    bad |= reader(&ap, 512);
    bad |= __builtin_va_arg(ap, long long) != 99;
    __builtin_va_end(ap);
    return bad;
}

// AAPCS64 passes va_list as a struct with distinct callee storage. The
// caller passes a separate copy and only ends that consumed list afterwards.
int native_va_list_value(va_list ap, void* caller_list)
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
int native_va_list_pass_value(NativeVaListValueReader* reader, int tag, ...)
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
