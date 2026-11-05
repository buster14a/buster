// Original transient Clang evaluation-method probes; keep off product branches.
extern int printf(const char *, ...);
#define KIND(x) _Generic((x), float: 1, double: 2, long double: 3, float _Complex: 4, double _Complex: 5, long double _Complex: 6, _Float16: 7, __bf16: 8, default: 0)
#define SHOW(x) printf(#x "=%d\n", KIND(x))
#define VALUE(x) printf(#x "=%La\n", (long double)(x))

#if CASE >= 1 && CASE <= 3
#if CASE == 1
#pragma clang fp eval_method(source)
#elif CASE == 2
#pragma clang fp eval_method(double)
#else
#pragma clang fp eval_method(extended)
#endif
float fixed(float x) { return x; }
long double chain(float x, float y) { return (x + y) - x; }
long double cast_chain(float x, float y) { return (float)(x + y) - x; }
long double conditional_chain(float x, float y, int c) { return (c ? x + y : x) - x; }
long double comma_chain(float x, float y) { return ((void)0, x + y) - x; }
long double store_chain(float x, float y) { float v = x + y; return v - x; }
long double call_chain(float x, float y) { return fixed(x + y) - x; }
long double double_chain(double x, double y) { return (x + y) - x; }
float _Complex complex_chain(float _Complex x, float _Complex y) { return (x + y) - x; }
long double compound(float x, double y) { x += y; return x; }
long double literal = (16777216.0f + 1.0f) - 16777216.0f;
int main(void)
{
    float x = 16777216.0f, y = 1.0f;
    float _Complex z = x, w = y;
    _Float16 h = 1;
    __bf16 b = 1;
    SHOW(1.0f); SHOW(1.0); SHOW(1.0L); SHOW(x); SHOW(+x); SHOW(-x); SHOW(x + y);
    SHOW(1 ? x : y); SHOW((x, y)); SHOW((float)(x + y)); SHOW(x = y); SHOW(x += y);
    SHOW(++x); SHOW(x++); SHOW(fixed(x)); SHOW(z + w); SHOW(+z); SHOW(__real__ z);
    SHOW(+h); SHOW(h + h); SHOW(+b); SHOW(b + b);
    printf("sizes=%lu,%lu,%lu\n", sizeof(x + y), sizeof(1 ? x : y), sizeof((x, y)));
    VALUE(chain(x, y)); VALUE(cast_chain(x, y)); VALUE(conditional_chain(x, y, 1));
    VALUE(comma_chain(x, y)); VALUE(store_chain(x, y)); VALUE(call_chain(x, y));
    VALUE(double_chain(9007199254740992.0, 1.0)); VALUE(literal); VALUE(16777217.0f - 16777216.0f);
    VALUE(__real__ complex_chain(z, w)); VALUE(compound(1.0f, 0x1p-24));
    return 0;
}
#elif CASE == 4
#pragma clang fp eval_method(double)
int probe(void)
{
    int a = KIND(1.0f + 1.0f), b;
    {
#pragma clang fp eval_method(extended)
        b = KIND(1.0f + 1.0f);
    }
    return 100 * a + 10 * b + KIND(1.0f + 1.0f);
}
int main(void) { printf("scope=%d\n", probe()); return 0; }
#elif CASE == 5
#pragma clang fp eval_method(double)
#pragma float_control(push)
#pragma clang fp eval_method(extended)
int a = KIND(1.0f + 1.0f);
#pragma float_control(pop)
int b = KIND(1.0f + 1.0f);
int main(void) { printf("stack=%d,%d\n", a, b); return 0; }
#elif CASE == 6
#define METHOD double
#pragma clang fp eval_method(METHOD)
_Static_assert(KIND(1.0f + 1.0f) == 2, "macro");
#elif CASE == 7
#pragma clang fp eval_method(unknown)
#elif CASE == 8
#pragma clang fp eval_method double
#elif CASE == 9
#pragma clang fp eval_method(double) eval_method(extended)
int probe = KIND(1.0f + 1.0f);
#elif CASE == 10
#pragma clang fp eval_method(double) extra
int probe = KIND(1.0f + 1.0f);
#elif CASE == 11
int probe(void)
{
    int x = 1;
#pragma clang fp eval_method(double)
    return x;
}
#elif CASE == 12
struct S
{
#pragma clang fp eval_method(double)
    int x;
};
#elif CASE == 13
#pragma clang fp eval_method(source)
int probe = __FLT_EVAL_METHOD__;
#elif CASE == 14
#pragma clang fp eval_method(double)
int probe = __FLT_EVAL_METHOD__;
#elif CASE == 15
void probe(void)
{
#pragma clang fp eval_method(double)
}
int value = __FLT_EVAL_METHOD__;
#elif CASE == 16
int before = __FLT_EVAL_METHOD__;
#pragma clang fp eval_method(double)
#pragma clang fp eval_method(source)
int after = __FLT_EVAL_METHOD__;
#elif CASE == 17
typedef float T __attribute__((available_only_in_default_eval_method));
#pragma clang fp eval_method(double)
T value;
#elif CASE == 18
typedef float T __attribute__((available_only_in_default_eval_method));
#pragma clang fp eval_method(source)
T value;
#elif CASE == 19
#pragma clang fp eval_method(double)
typedef float T __attribute__((available_only_in_default_eval_method));
T value;
#elif CASE == 20
typedef float T __attribute__((available_only_in_default_eval_method));
void probe(void)
{
#pragma clang fp eval_method(double)
    T value;
}
T after;
#elif CASE == 21
#pragma float_control(precise, off)
#pragma clang fp eval_method(double)
int probe = KIND(1.0f + 1.0f);
#elif CASE == 22
#pragma clang fp eval_method(double)
#pragma float_control(precise, off)
int probe = KIND(1.0f + 1.0f);
#elif CASE == 23
#pragma clang fp reassociate(on)
#pragma clang fp eval_method(double)
#elif CASE == 24
#pragma clang fp eval_method(double)
#pragma clang fp reassociate(on)
#elif CASE == 25
#pragma clang fp reciprocal(on)
#pragma clang fp eval_method(double)
#elif CASE == 26
#pragma clang fp eval_method(double)
#pragma clang fp reciprocal(on)
#elif CASE == 27
#pragma clang fp eval_method(double) reassociate(on)
#elif CASE == 28
#pragma clang fp reassociate(on) eval_method(double)
#elif CASE == 29
#pragma clang fp contract(fast)
#pragma clang fp eval_method(double)
#elif CASE == 30
#pragma clang fp eval_method(double) contract(fast)
#elif CASE == 31
#pragma STDC FENV_ACCESS ON
#pragma clang fp eval_method(double)
double probe(float x, float y) { return x + y; }
#elif CASE == 32
#pragma clang fp eval_method(double)
typedef float V __attribute__((ext_vector_type(4)));
V probe(V x, V y) { return x + y; }
_Static_assert(sizeof((V){0} + (V){0}) == 16, "vector type");
#elif CASE == 33
#pragma clang fp eval_method(extended)
typedef double V __attribute__((ext_vector_type(2)));
V probe(V x, V y) { return x + y; }
_Static_assert(sizeof((V){0} + (V){0}) == 16, "vector type");
#elif CASE == 34
#pragma clang fp eval_method(double)
double probe(_Float16 x, _Float16 y) { return x + y; }
#elif CASE == 35
#pragma clang fp eval_method(extended)
long double _Complex probe(float _Complex x, float _Complex y) { return x + y; }
#elif CASE == 36
#pragma clang fp eval_method(double)
_Static_assert((16777216.0f + 1.0f) - 16777216.0f == 1, "constant fold");
#elif CASE == 37
#pragma clang fp eval_method(double)
_Static_assert(16777217.0f - 16777216.0f == 0, "literal still rounds to source");
#elif CASE == 38
#pragma clang fp eval_method(extended)
_Static_assert((9007199254740992.0 + 1.0) - 9007199254740992.0 == 1, "extended constant fold");
#elif CASE == 39
extern void variadic(int, ...);
extern void fixed(float, double);
#pragma clang fp eval_method(extended)
void probe(float x, double y) { fixed(x, y); variadic(0, x, y); }
#elif CASE == 40
#pragma clang fp eval_method(double)
int probe(void) { return KIND(__builtin_elementwise_abs(1.0f)) * 100 + KIND(__builtin_fabsf(1.0f)) * 10 + KIND(__builtin_elementwise_min(1.0f, 2.0f)); }
#elif CASE == 41
#pragma clang fp eval_method(double)
double probe(_Atomic(float) *x) { return *x + 1.0f; }
#elif CASE == 42
#pragma clang fp eval_method(double)
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0f + 1.0f), double), "typeof");
#elif CASE == 43
#pragma clang fp eval_method(double)
int probe[sizeof(1.0f + 1.0f)];
#elif CASE == 44
#pragma clang fp eval_method(double)
double probe(float x, float y) { return (x += y) - x; }
#elif CASE == 45
float probe(float x)
{
#pragma clang fp eval_method(double)
    {
#pragma clang fp eval_method(source)
        x += 1.0f;
    }
    return x + 1.0f;
}
#elif CASE == 46
#define FP _Pragma("clang fp eval_method(double)")
FP
_Static_assert(KIND(1.0f + 1.0f) == 2, "_Pragma");
#elif CASE == 47
#pragma clang fp eval_method(source)
#pragma clang fp reassociate(on)
#elif CASE == 48
#pragma clang fp eval_method(source)
#pragma clang fp reciprocal(on)
#elif CASE == 49
#pragma float_control(push)
#pragma clang fp eval_method(double)
#pragma float_control(pop)
#pragma clang fp reassociate(on)
#elif CASE == 50
void probe(void)
{
#pragma clang fp eval_method(double)
}
#pragma clang fp reciprocal(on)
#elif CASE == 51
int probe(int
#pragma clang fp eval_method(double)
    x);
#elif CASE == 52
#pragma clang fp eval_method()
#elif CASE == 53
#define EVAL __FLT_EVAL_METHOD__
#pragma clang fp eval_method(double)
#if EVAL
int probe;
#endif
#elif CASE == 54
#pragma clang fp eval_method(double)
double probe(float x) { return __builtin_complex(x, x); }
#elif CASE == 55
#pragma clang fp eval_method(double)
int probe(void) { return KIND(__real__ (1.0fi + 2.0f)) * 10 + KIND(~(1.0fi + 2.0f)); }
#elif CASE == 56
#pragma clang fp eval_method(double)
double probe(float x, int *p) { return (*p ? : x); }
#endif

#if CASE > 5
int main(void) { return 0; }
#endif
