#include <stdarg.h>
#include <stdio.h>
struct S { int a; double b; };
struct A { int x; int y; };
struct B { char x[3]; };
typedef union __attribute__((transparent_union)) { int* i; const char* c; } U;
static int calls;
static int pr(_Complex double z, const char* f, ...) { va_list ap; va_start(ap, f); printf("(%g,%g)", __real__ z, __imag__ z); vprintf(f, ap); va_end(ap); return 0; }
static int pf(_Complex float z, int k, const char* f, ...) { va_list ap; va_start(ap, f); printf("(%g,%g,%d)", __real__ z, __imag__ z, k); vprintf(f, ap); va_end(ap); return 0; }
static int pl(_Complex long double z, const char* f, ...) { va_list ap; va_start(ap, f); printf("(%Lg,%Lg)", __real__ z, __imag__ z); vprintf(f, ap); va_end(ap); return 0; }
static int pi(_Complex int z, const char* f, ...) { va_list ap; va_start(ap, f); printf("(%d,%d)", __real__ z, __imag__ z); vprintf(f, ap); va_end(ap); return 0; }
static int pd(double d, const char* f, ...) { va_list ap; va_start(ap, f); printf("(%g)", d); vprintf(f, ap); va_end(ap); return 0; }
static int pv(int k, ...) { va_list ap; va_start(ap, k); _Complex double z = va_arg(ap, _Complex double); printf("<%d|%g,%g>", k, __real__ z, __imag__ z); calls++; va_end(ap); return 0; }
static int pvf(int k, ...) { va_list ap; va_start(ap, k); _Complex float z = va_arg(ap, _Complex float); _Complex long double w = va_arg(ap, _Complex long double); printf("<%d|%g,%g|%Lg,%Lg>", k, __real__ z, __imag__ z, __real__ w, __imag__ w); calls++; va_end(ap); return 0; }
static int pu(U u, const char* f, ...) { va_list ap; va_start(ap, f); printf("[%d]", *u.i); vprintf(f, ap); va_end(ap); return 0; }
static int pa(_Atomic struct A a, const char* f, ...) { struct A v = a; va_list ap; va_start(ap, f); printf("[%d,%d]", v.x, v.y); vprintf(f, ap); va_end(ap); return 0; }
static int pb(_Atomic struct B a, const char* f, ...) { struct B v = a; va_list ap; va_start(ap, f); printf("[%d%d%d]", v.x[0], v.x[1], v.x[2]); vprintf(f, ap); va_end(ap); return 0; }
static int pk(int k, const char* f, ...) { va_list ap; va_start(ap, f); printf("{%d}", k); vprintf(f, ap); va_end(ap); return 0; }
static _Complex double next(int* n) { *n += 1; return *n + 0.5i; }
int main(void) {
    struct S s = { 1, 2.5 };
    _Complex double z = 1.0 + 2.0i;
    _Complex float zf = 3.0f - 1.0if;
    int n = 0;
    __builtin_dump_struct(&s, pr, z);
    __builtin_dump_struct(&s, pr, zf);
    __builtin_dump_struct(&s, pf, z, 7);
    __builtin_dump_struct(&s, pl, (_Complex long double)(5.0L + 6.0iL));
    __builtin_dump_struct(&s, pi, (_Complex int)(3 + 4i));
    __builtin_dump_struct(&s, pr, 9);
    __builtin_dump_struct(&s, pd, z);
    __builtin_dump_struct(&s, pr, next(&n));
    __builtin_dump_struct(&s, pv, 1, z);
    __builtin_dump_struct(&s, pvf, 2, zf, (_Complex long double)(7.0L - 8.0iL));
    int k = 42;
    U u;
    u.i = &k;
    struct A a = { 3, 4 };
    struct B b = { { 5, 6, 7 } };
    _Atomic struct A aa = a;
    __builtin_dump_struct(&s, pu, &k);
    __builtin_dump_struct(&s, pu, u);
    __builtin_dump_struct(&s, pu, (const char*)&k);
    __builtin_dump_struct(&s, pu, (void*)&k);
    __builtin_dump_struct(&s, pa, a);
    __builtin_dump_struct(&s, pb, b);
    __builtin_dump_struct(&s, pa, aa);
    _Atomic int ai = 7;
    __builtin_dump_struct(&s, pk, ai);
    printf("n=%d calls=%d\n", n, calls);
    return 0;
}
