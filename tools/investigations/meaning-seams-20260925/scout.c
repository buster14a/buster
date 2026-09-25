#ifndef CASE
#define CASE 1
#endif
extern int printf(const char *, ...);
static volatile int input;
static volatile int hits;
static int mark(void) { hits += 1; return input; }
#if CASE == 15 || CASE == 18
static const int zero = 0;
static int folded = zero ? 3 : 4;
#elif CASE == 16
static int *const nullp = 0;
static int folded = !nullp;
#elif CASE == 17
static int *const nullp = 0;
static int folded = nullp ? 3 : 4;
#endif
#if CASE == 20
static int aligned_object __attribute__((aligned(64)));
#endif
int main(void) {
    int expected = 0, expected_hits = 0, actual = 0;
#if CASE == 1
    actual = __builtin_constant_p(mark() && 0);
#elif CASE == 2
    actual = __builtin_constant_p(mark() || 1);
#elif CASE == 3
    actual = __builtin_constant_p(input && 0);
#elif CASE == 4
    actual = __builtin_constant_p(input || 1);
#elif CASE == 5
    actual = __builtin_constant_p(mark() ? 7 : 9);
#elif CASE == 6
    actual = __builtin_constant_p(input ? 7 : 9);
#elif CASE == 7
    actual = __builtin_constant_p((mark(), 7));
#elif CASE == 8
    actual = __builtin_constant_p(0 && mark()); expected = 1;
#elif CASE == 9
    actual = __builtin_constant_p(1 || mark()); expected = 1;
#elif CASE == 10
    actual = mark() && 0; expected_hits = 1;
#elif CASE == 11
    actual = mark() || 1; expected = 1; expected_hits = 1;
#elif CASE == 12
    actual = __builtin_constant_p(mark() && 0) ? 0 : ((void)(mark() && 0), 1);
    expected = 1; expected_hits = 1;
#elif CASE == 13
    actual = __builtin_constant_p(mark() || 1) ? 0 : ((void)(mark() || 1), 1);
    expected = 1; expected_hits = 1;
#elif CASE == 14
    actual = __builtin_constant_p(input);
#elif CASE == 15
    actual = folded; expected = 4;
#elif CASE == 16
    actual = folded; expected = 1;
#elif CASE == 17
    actual = folded; expected = 4;
#elif CASE == 18
    actual = zero ? 3 : 4; expected = 4;
#elif CASE == 19
    int aligned_local __attribute__((aligned(64)));
    actual = (int)__alignof__(aligned_local); expected = 64;
#elif CASE == 20
    actual = (int)__alignof__(aligned_object); expected = 64;
#elif CASE == 21
    struct S { char member __attribute__((aligned(32))); } obj;
    actual = (int)__alignof__(obj.member); expected = 32;
#elif CASE == 22
    typedef int aligned_type __attribute__((aligned(64)));
    aligned_type obj;
    actual = (int)__alignof__(obj); expected = 64;
#elif CASE == 23
    static float folded_float = (float)(0x1p24f + 1.0f) - 0x1p24f;
    actual = folded_float != 0.0f;
#elif CASE == 24
    volatile float x = 0x1p24f, y = 1.0f;
    float runtime_float = (float)(x + y) - x;
    actual = runtime_float != 0.0f;
#else
#error unknown CASE
#endif
    printf("case=%d actual=%d expected=%d hits=%d expected_hits=%d\n", CASE, actual, expected, hits, expected_hits);
    return (actual != expected) | ((hits != expected_hits) << 1);
}
