#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { int q; short r; } Anon;
typedef struct Point { int x, y; } Point;
enum Color { RED, GREEN };
enum Neg { MINUS = -1 };
typedef enum { A1, B1 } AnonE;
typedef unsigned long mysize;
typedef size_t wrapped;
typedef const char *cstr;
typedef cstr cstr2;
typedef float V4 __attribute__((vector_size(16)));
typedef int I4 __attribute__((vector_size(16)));
typedef int E2 __attribute__((ext_vector_type(2)));
typedef double D2 __attribute__((vector_size(16)));
typedef signed char C16 __attribute__((vector_size(16)));
typedef long L2 __attribute__((vector_size(16)));
typedef unsigned short S8 __attribute__((vector_size(16)));
struct S {
    _Bool b; char c; signed char sc; unsigned char uc; short s; unsigned short us;
    int i; unsigned u; long l; unsigned long ul; long long ll; unsigned long long ull;
    float f; double d; long double ld;
    const char *str; char *mstr; signed char *sstr; const unsigned char *ustr; void *vp; int *ip; cstr2 s2; const cstr cs;
    int arr[3]; char carr[4];
    uint32_t u32; size_t sz; wrapped wsz; mysize ms; int8_t i8; ptrdiff_t pd; intmax_t im; uintmax_t um;
    enum Color col; enum Neg ng; AnonE ae;
    Point p; Anon an; struct Point *pp; const volatile int cvi;
    int bf : 3; unsigned ubf : 5; int : 4; _Bool bb : 1; long long lbf : 40; unsigned long long ubig : 63; enum Color cbf : 2;
    struct { int ia; union { int ib; float fc; }; struct { char deep; }; };
    struct { int named_in; } named;
    union U { int x; char y; } un;
    __int128 i128; _Complex double cd; _Atomic int at; int (*fp)(int); unsigned _BitInt(40) ub; Point pts[2];
    V4 v4; I4 i4;
};
struct __attribute__((packed)) WideFields { unsigned long long a : 61, b : 60; long long c : 58; };
struct __attribute__((packed)) Packed { char a; int b : 7; unsigned c : 20; long long d; struct WideFields inner; };
struct Vec { E2 e; D2 d; C16 c; L2 l; S8 s; };
struct O { struct I { int a; } i; struct { int b; struct { int c; } deep; } un; union { int d; } u2; };
typedef struct { struct { int e; } in; } T;
#define AS1 __attribute__((address_space(1)))
#define ASA __attribute__((address_space(1)))
#define ASN(n) __attribute__((address_space(n)))
typedef AS1 int T1;
struct Spaces { AS1 int *f; ASA int *g; __attribute__((address_space(1))) int *h; ASN(3) char *i; T1 *t; AS1 int *const *pp; int AS1 *w, *v; };
static const char *base;
static int pr(int tag, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    printf("<%d>", tag);
    for (const char *p = f; *p; p++)
    {
        if (*p != '%') { putchar(*p); continue; }
        char spec[32]; int n = 0; spec[n++] = *p++;
        while (strchr("-+ #0123456789.", *p)) spec[n++] = *p++;
        int v = 0; if (*p == 'v') { v = 1; p++; while (*p >= '0' && *p <= '9') p++; }
        char len[3] = { 0 }; int ln = 0; while (strchr("hlLzjt", *p)) len[ln++] = *p, spec[n++] = *p++;
        char c = *p; spec[n++] = c; spec[n] = 0;
        if (v && c == 'f') { V4 x = va_arg(ap, V4); printf("[%g %g %g %g]", x[0], x[1], x[2], x[3]); }
        else if (v) { I4 x = va_arg(ap, I4); printf("[%d %d %d %d]", x[0], x[1], x[2], x[3]); }
        else if (c == 's') { const char *s = va_arg(ap, const char *); printf(spec, s ? s : "(null)"); }
        else if (c == 'p') { const char *q = va_arg(ap, const char *); if (q) printf("@%ld", (long)(q - base)); else printf("nil"); }
        else if (c == 'f' && len[0] == 'L') printf(spec, va_arg(ap, long double));
        else if (c == 'f') printf(spec, va_arg(ap, double));
        else if (!strcmp(len, "ll")) printf(spec, va_arg(ap, long long));
        else if (len[0] == 'l' || len[0] == 'z' || len[0] == 'j' || len[0] == 't') printf(spec, va_arg(ap, long));
        else printf(spec, va_arg(ap, int));
    }
    va_end(ap);
    return 0;
}
static int calls;
static int next(void) { return calls++; }
typedef int (*P)(int, const char *, ...);
static P get(void) { printf("get\n"); return pr; }
static int kr();
static int kr(tag, f) int tag; const char *f; { printf("kr %d %s", tag, f); return 0; }
struct Ctx { int calls; };
static int counted(struct Ctx *c, const char *f, ...) { (void)f; c->calls++; return 0; }
static void prefixed(double x, const char *f, ...) { va_list ap; va_start(ap, f); printf("[%g] ", x); vprintf(f, ap); va_end(ap); }
static void scoped(int a, const char *b, ...)
{
    (void)b;
    typedef struct { uint16_t w; } Word;
    struct Local { uint32_t u; size_t z; Word word; struct { int q; } in; } l = { 1, 2, { 3 }, { a } };
    __builtin_dump_struct(&l, printf);
    __builtin_dump_struct(&l.in, printf);
    struct { struct { int k; } in; } r = { { a } };
    __builtin_dump_struct(&r.in, printf);
}
static void old_style(a) int a; { struct Old { struct { int k; } in; } r = { { a } }; __builtin_dump_struct(&r.in, printf); }
typedef int Arr4[4];
static void spelled(size_t n, cstr text, uint8_t *bytes, const size_t c, Arr4 arr, int (*cb)(size_t), int plain, ...)
{
    (void)n; (void)text; (void)bytes; (void)c; (void)arr; (void)cb;
    struct Spelled { struct { int k; } in; } r = { { plain } };
    __builtin_dump_struct(&r.in, printf);
}
typedef const int CI;
typedef int *const PC;
struct Qualified { CI ci; PC pc; const CI cci; volatile CI vci; CI const cic; };
static void qualified(CI x, PC p, const struct { int values[4]; } *const arg)
{
    (void)x; (void)p; (void)arg;
    struct Inner { struct { int n; } inner; } r = { { x } };
    __builtin_dump_struct(&r.inner, printf);
}
typedef int I;
struct Placed { int (*a)(I); I (*b)(int); I c, (*d)(long), e; I (*(*f)(int))(I); size_t (*h)(const char *, ...); int (*k)(void); };
static void placed_return(int (*cb)(I))
{
    (void)cb;
    struct Outer1 { struct { int value; } inner; } r = { { 1 } };
    __builtin_dump_struct(&r.inner, printf);
}
static void placed_parameter(I (*cb)(int), I (*(*nested)(int))(I), ...)
{
    (void)cb; (void)nested;
    struct Outer2 { struct { int value; } inner; } r = { { 2 } };
    __builtin_dump_struct(&r.inner, printf);
}
typedef int (*FP)(int);
typedef int A4[4];
struct Repeated { int (*(*a)(FP))(int); FP (*b)(int (*)(int)); A4 *p; int (*q)[4]; A4 *(*r)(int (*)[4]); };
static void repeated(int (*(*cb)(FP))(int), int (*(*rev)(int (*)(int)))(FP))
{
    (void)cb; (void)rev;
    struct Outer3 { struct { int x; } inner; } r = { { 3 } };
    __builtin_dump_struct(&r.inner, printf);
}
struct Pointee { struct { int x; } *p; void (*fn)(struct { int w; } *); };
struct Top2 { struct M { struct { int deep; } u; } m; struct { int x; } y; };
static struct S s;
#define F1(a) int a##0, a##1, a##2, a##3, a##4, a##5, a##6, a##7, a##8, a##9;
#define F2(a) F1(a##0) F1(a##1) F1(a##2) F1(a##3) F1(a##4) F1(a##5) F1(a##6) F1(a##7) F1(a##8) F1(a##9)
#define F3(a) F2(a##0) F2(a##1) F2(a##2) F2(a##3) F2(a##4) F2(a##5) F2(a##6) F2(a##7) F2(a##8) F2(a##9)
static struct Big { F3(f0) F3(f1) F3(f2) F3(f3) F3(f4) } big;
static unsigned long big_hash = 14695981039346656037ul;
static int big_lines;
static int hashed(const char *format, ...)
{
    char line[256];
    va_list list;
    va_start(list, format);
    int length = vsnprintf(line, sizeof(line), format, list);
    va_end(list);
    for (int i = 0; i < length && i < (int)sizeof(line); i += 1) { big_hash = (big_hash ^ (unsigned char)line[i]) * 1099511628211ul; }
    big_lines += 1;
    return length;
}
struct Three { long a, b, c; };
struct Context { int tag; char pad[20]; };
typedef int Lanes __attribute__((vector_size(16)));
static struct Three by_value(const char *format, ...) { va_list list; va_start(list, format); vprintf(format, list); va_end(list); struct Three t = { 1, 2, 3 }; return t; }
static int with_context(struct Context c, const char *format, ...) { va_list list; va_start(list, format); printf("[%d]", c.tag); vprintf(format, list); va_end(list); return 0; }
static __int128 with_wide(__int128 k, const char *format, ...) { va_list list; va_start(list, format); printf("<%d>", (int)(k >> 64)); vprintf(format, list); va_end(list); return k; }
static _Float16 with_half(_Float16 h, const char *format, ...) { va_list list; va_start(list, format); printf("<%g>", (double)h); vprintf(format, list); va_end(list); return h; }
static Lanes with_lanes(Lanes v, const char *format, ...) { va_list list; va_start(list, format); printf("<%d>", v[3]); vprintf(format, list); va_end(list); return v; }
static _Complex double with_complex(const char *format, ...) { va_list list; va_start(list, format); vprintf(format, list); va_end(list); return __builtin_complex(1.0, 2.0); }
static int with_long_double(long double k, const char *format, ...) { va_list list; va_start(list, format); printf("<%Lg>", k); vprintf(format, list); va_end(list); return 0; }
static struct Packed packed = { 'z', -5, 99999, -77, { 0x123456789abcdef0ULL, 0xfedcba987654321ULL, -0x123456789abcdLL } };
int main(int argc, char **argv)
{
    (void)argv;
    int n = argc;
    base = (const char *)&s;
    s.b = 1; s.c = -3; s.sc = -4; s.uc = 250; s.s = -300; s.us = 65000; s.i = -7 * n; s.u = 4000000000u; s.l = -8; s.ul = 9; s.ll = -10; s.ull = 11;
    s.f = 1.5f; s.d = -2.25; s.ld = 3.125L; s.str = "hello"; s.mstr = "a string longer than thirty-two characters for sure"; s.vp = &s.i;
    s.s2 = "two"; s.u32 = 5; s.sz = 6; s.wsz = 7; s.ms = 8; s.i8 = -9; s.pd = -10; s.im = -11; s.um = 12; s.col = GREEN; s.ng = MINUS; s.ae = B1;
    s.p.x = 1; s.p.y = 2; s.an.q = 3; s.an.r = 4; s.pp = &s.p; s.bf = -2; s.ubf = 17; s.bb = 1; s.lbf = -12345678901LL; s.ubig = 0x7fffffffffffffffULL;
    s.cbf = GREEN; s.ia = 13; s.ib = 14; s.deep = 'x'; s.named.named_in = 15; s.un.x = 0x41; s.v4 = (V4){ 1, 2, 3, 4 }; s.i4 = (I4){ -1, 2, -3, 4 };
    __builtin_dump_struct(&s, pr, 1);
    __builtin_dump_struct(&s.p, printf);
    Anon an = { 5, 6 };
    __builtin_dump_struct(&an, printf);
    union U u = { 0x42 };
    __builtin_dump_struct(&u, printf);
    struct Spaces spaces = { 0 };
    __builtin_dump_struct(&spaces, printf);
    __builtin_dump_struct(&s.named, pr, next());
    printf("calls=%d\n", calls);
    __builtin_dump_struct(&an, get(), calls++);
    printf("calls=%d\n", calls);
    __builtin_dump_struct(&s.p, kr, 2);
    __builtin_dump_struct(&packed, printf);
    struct Vec v = { { 1, 2 }, { 1.5, -2.5 }, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, { 7, -8 }, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    __builtin_dump_struct(&v, printf);
    struct O o = { { 1 }, { 2, { 3 } }, { 4 } };
    T t = { { 5 } };
    struct { int z; } local = { 6 };
    __builtin_dump_struct(&o.i, printf);
    __builtin_dump_struct(&o.un, printf);
    __builtin_dump_struct(&o.un.deep, printf);
    __builtin_dump_struct(&o.u2, printf);
    __builtin_dump_struct(&t.in, printf);
    __builtin_dump_struct(&local, printf);
    __builtin_dump_struct(&o, printf);
    struct Ctx c = { 0 };
    __builtin_dump_struct(&o, counted, &c);
    printf("counted=%d\n", c.calls);
    __builtin_dump_struct(&o.i, prefixed, 1.5f);
    const struct Point *cp = &s.p;
    __builtin_dump_struct(cp, printf);
    scoped(n, "x");
    old_style(n);
    struct Top2 top2 = { { { 7 } }, { 8 } };
    __builtin_dump_struct(&top2.m.u, printf);
    __builtin_dump_struct(&top2.y, printf);
    struct Pointee pointee;
    __typeof__(*pointee.p) pointed = { 42 };
    pointee.p = &pointed;
    __builtin_dump_struct(pointee.p, printf);
    spelled(1, "t", 0, 2, 0, 0, n);
    struct Qualified q = { 1, 0, 2, 3, 4 };
    __builtin_dump_struct(&q, printf);
    qualified(n, 0, 0);
    struct Placed placed = { 0 };
    __builtin_dump_struct(&placed, printf);
    placed_return(0);
    placed_parameter(0, 0);
    struct Repeated repeated_fields = { 0 };
    __builtin_dump_struct(&repeated_fields, printf);
    repeated(0, 0);
    struct Context context = { 7 };
    __builtin_dump_struct(&s.p, by_value);
    __builtin_dump_struct(&s.p, with_context, context);
    __builtin_dump_struct(&s.p, with_wide, (__int128)n << 64);
    __builtin_dump_struct(&s.p, with_half, (_Float16)0.5f * (_Float16)n);
    __builtin_dump_struct(&s.p, with_lanes, (Lanes){ 1, 2, 3, n });
    __builtin_dump_struct(&s.p, with_complex);
    __builtin_dump_struct(&s.p, with_long_double, 2.5L * n);
    big.f0000 = 1; big.f2345 = -2; big.f4999 = n;
    __builtin_dump_struct(&big, hashed);
    printf("big=%lx lines=%d\n", big_hash, big_lines);
    return 0;
}
