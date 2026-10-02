#include <stdarg.h>
typedef struct __attribute__((aligned(16))) { float x, y; } PadVec2;
typedef struct __attribute__((aligned(16))) { long long a; } PadLong;
typedef struct { _Alignas(16) int v; } PadInt;
typedef struct { char c[17]; } PadC17;
typedef struct __attribute__((packed)) { char c; long long x; } PadP9;
typedef struct __attribute__((aligned(16))) { char c[17]; } PadA16;
typedef struct { unsigned gp, fp; void *overflow, *save; } PadVaState;
#ifdef SYSV_PADDING_HOST
#define PAD(name) host_##name
#define OTHER(name) name
#else
#define PAD(name) name
#define OTHER(name) host_##name
#endif
typedef float PadFloat(PadVec2, long long);
typedef long long PadInteger(PadLong, long long);
typedef long long PadAligned(PadInt, long long);
typedef long long PadBoundary(long long, long long, long long, long long, long long, PadLong, long long);
typedef long long PadExhausted(long long, long long, long long, long long, long long, long long, PadLong, long long);
typedef PadVec2 PadReturn(PadVec2);
float PAD(pad_float)(PadVec2 v, long long a) { return v.x + v.y + (float)a; }
long long PAD(pad_integer)(PadLong v, long long a) { return v.a * 1000 + a; }
long long PAD(pad_aligned)(PadInt v, long long a) { return (long long)v.v * 1000 + a; }
long long PAD(pad_boundary)(long long a, long long b, long long c, long long d, long long e, PadLong v, long long tail)
{ return a + b + c + d + e + v.a * 1000 + tail; }
long long PAD(pad_exhausted)(long long a, long long b, long long c, long long d, long long e, long long f, PadLong v, long long tail)
{ return a + b + c + d + e + f + v.a * 1000 + tail; }
PadVec2 PAD(pad_return)(PadVec2 v) { PadVec2 out = {v.y, v.x}; return out; }
int PAD(pad_variadic)(int marker, ...)
{
    va_list ap, copy;
    va_start(ap, marker);
    PadVec2 v = va_arg(ap, PadVec2);
    PadVaState before, after, mirrored;
    __builtin_memcpy(&before, &ap, sizeof(before));
    unsigned long long cursor = (unsigned long long)before.overflow;
    va_copy(copy, ap);
    PadC17 c17 = va_arg(ap, PadC17);
    PadC17 mirror = va_arg(copy, PadC17);
    __builtin_memcpy(&after, &ap, sizeof(after));
    __builtin_memcpy(&mirrored, &copy, sizeof(mirrored));
    int memory_bad = sizeof(before) != 24 || sizeof(PadC17) != 17 || sizeof(PadP9) != 9 || sizeof(PadA16) != 32;
    memory_bad |= before.gp != 8 || before.fp != 64;
    memory_bad |= after.gp != before.gp || after.fp != before.fp || after.save != before.save || (unsigned long long)after.overflow != cursor + 24;
    memory_bad |= mirrored.gp != after.gp || mirrored.fp != after.fp || mirrored.overflow != after.overflow || mirrored.save != after.save;
    va_end(copy);
    cursor = (unsigned long long)after.overflow;
    PadP9 p9 = va_arg(ap, PadP9);
    __builtin_memcpy(&after, &ap, sizeof(after));
    memory_bad |= after.gp != before.gp || after.fp != before.fp || after.save != before.save || (unsigned long long)after.overflow != cursor + 16;
    cursor = (unsigned long long)after.overflow;
    PadA16 a16 = va_arg(ap, PadA16);
    __builtin_memcpy(&after, &ap, sizeof(after));
    memory_bad |= after.gp != before.gp || after.fp != before.fp || after.save != before.save || (unsigned long long)after.overflow != ((cursor + 15ull) & ~15ull) + 32;
    long long a = va_arg(ap, long long);
    PadLong s = va_arg(ap, PadLong);
    PadInt t = va_arg(ap, PadInt);
    long long tail = va_arg(ap, long long);
    PadLong last = va_arg(ap, PadLong);
    PadLong overflow = va_arg(ap, PadLong);
    long long after_value = va_arg(ap, long long);
    __builtin_memcpy(&before, &ap, sizeof(before));
    cursor = (unsigned long long)before.overflow;
    PadC17 final_c17 = va_arg(ap, PadC17);
    __builtin_memcpy(&after, &ap, sizeof(after));
    memory_bad |= before.gp != 48 || after.gp != before.gp || after.fp != before.fp || after.save != before.save || (unsigned long long)after.overflow != cursor + 24;
    long long final_scalar = va_arg(ap, long long);
    va_end(ap);
    for (int i = 0; i < 17; i += 1) { memory_bad |= c17.c[i] != 11 + i || mirror.c[i] != 11 + i || a16.c[i] != 41 + i || final_c17.c[i] != 71 + i; }
    memory_bad |= p9.c != 3 || p9.x != 0x1122334455667788ll || final_scalar != 91ll;
    return marker != 17 || v.x != 1.5f || v.y != 2.5f || a != 100ll || s.a != 7ll || t.v != 9 || tail != 42ll ||
           last.a != 11ll || overflow.a != 13ll || after_value != 77ll || memory_bad;
}
int OTHER(pad_variadic)(int, ...);
int PAD(pad_calls)(PadFloat* floating, PadInteger* integer, PadAligned* aligned,
                   PadBoundary* boundary, PadExhausted* exhausted, PadReturn* returning)
{
    PadVec2 v = {1.5f, 2.5f};
    PadLong s = {7};
    PadInt t = {9};
    PadLong last = {11};
    PadLong overflow = {13};
    PadC17 c17 = {{0}}, final_c17 = {{0}};
    PadA16 a16 = {{0}};
    PadP9 p9 = {3, 0x1122334455667788ll};
    for (int i = 0; i < 17; i += 1) { c17.c[i] = (char)(11 + i); a16.c[i] = (char)(41 + i); final_c17.c[i] = (char)(71 + i); }
    PadVec2 r = returning(v);
    int bad = floating(v, 100ll) != 104.0f;
    bad |= (integer(s, 42ll) != 7042ll) << 1;
    bad |= (aligned(t, 42ll) != 9042ll) << 2;
    bad |= (boundary(1ll, 2ll, 3ll, 4ll, 5ll, s, 42ll) != 7057ll) << 3;
    bad |= (exhausted(1ll, 2ll, 3ll, 4ll, 5ll, 6ll, s, 42ll) != 7063ll) << 4;
    bad |= (r.x != 2.5f || r.y != 1.5f) << 5;
    bad |= OTHER(pad_variadic)(17, v, c17, p9, a16, 100ll, s, t, 42ll, last, overflow, 77ll, final_c17, 91ll) << 6;
    return bad;
}
#ifdef SYSV_PADDING_HOST
float pad_float(PadVec2, long long);
long long pad_integer(PadLong, long long);
long long pad_aligned(PadInt, long long);
long long pad_boundary(long long, long long, long long, long long, long long, PadLong, long long);
long long pad_exhausted(long long, long long, long long, long long, long long, long long, PadLong, long long);
PadVec2 pad_return(PadVec2);
int pad_calls(PadFloat*, PadInteger*, PadAligned*, PadBoundary*, PadExhausted*, PadReturn*);
int main(void)
{
    int bad = host_pad_calls(pad_float, pad_integer, pad_aligned, pad_boundary, pad_exhausted, pad_return);
    int other = pad_calls(host_pad_float, host_pad_integer, host_pad_aligned, host_pad_boundary, host_pad_exhausted, host_pad_return);
    return bad ? bad : (other ? 128 | other : 0);
}
#endif
