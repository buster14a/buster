// C99 `_Complex`: the type's layout and ABI, the arithmetic operators, the
// GNU `__real__`/`__imag__` operators, the conversions in both directions,
// and the union punning musl's <complex.h> is written with.
//
// Every expected value here was taken from a Clang build of this same file,
// and every operand is chosen so the answer is exact in binary floating point
// -- a lowering that computed the right formula in the wrong precision would
// still fail. The operands travel through volatile storage so neither
// compiler folds the arithmetic away: the point is the emitted sequences, not
// the constant folder.
//
// `long double _Complex` is here in both positions, including the result,
// which System V x86-64 returns in ST(0)/ST(1) under the psABI's COMPLEX_X87
// class rather than in memory the way the equivalent struct is returned.
// basic_c_complex_x87_caller.c is the same shapes across a translation-unit
// boundary against the host compiler; this file is what one compiler agrees
// with itself about.

typedef double _Complex complex_double;
typedef float _Complex complex_float;

static volatile double real_source;
static volatile double imaginary_source;
static volatile complex_double complex_source;
static volatile complex_float complex_float_source;
static volatile int integer_source;

// Not `static`: an ABI question is only asked at a real call boundary, and
// these are the boundary. System V carries a `double _Complex` in XMM0/XMM1,
// a `float _Complex` packed into XMM0, and AAPCS64 carries each as a
// two-element homogeneous float aggregate.
complex_double complex_add(complex_double a, complex_double b) { return a + b; }
complex_double complex_subtract(complex_double a, complex_double b) { return a - b; }
complex_double complex_multiply(complex_double a, complex_double b) { return a * b; }
complex_double complex_divide(complex_double a, complex_double b) { return a / b; }
complex_double complex_negate(complex_double a) { return -a; }
complex_double complex_promote(complex_double a) { return +a; }
complex_double complex_add_real(complex_double a, double b) { return a + b; }
complex_double complex_real_add(double a, complex_double b) { return a + b; }
complex_double complex_subtract_real(complex_double a, double b) { return a - b; }
complex_double complex_real_subtract(double a, complex_double b) { return a - b; }
complex_double complex_multiply_real(complex_double a, double b) { return a * b; }
complex_double complex_real_multiply(double a, complex_double b) { return a * b; }
complex_double complex_divide_real(complex_double a, double b) { return a / b; }
complex_double complex_real_divide(double a, complex_double b) { return a / b; }
int complex_equal(complex_double a, complex_double b) { return a == b; }
int complex_not_equal(complex_double a, complex_double b) { return a != b; }
int complex_truth(complex_double a) { return a ? 7 : 3; }
double complex_real(complex_double a) { return __real__ a; }
double complex_imaginary(complex_double a) { return __imag__ a; }
double complex_real_cast(complex_double a) { return (double)a; }
int complex_integer_cast(complex_double a) { return (int)a; }
complex_double complex_from_real(double a) { return a; }
complex_double complex_from_integer(int a) { return a; }
complex_float complex_narrow(complex_double a) { return (complex_float)a; }
complex_double complex_widen(complex_float a) { return a; }
complex_float complex_float_multiply(complex_float a, complex_float b) { return a * b; }
complex_float complex_float_add(complex_float a, complex_float b) { return a + b; }

// `long double _Complex` in both positions. An argument is a sixteen-aligned
// memory slot under System V, which matches the two-field aggregate exactly
// and is the shape musl's `cabsl`, `cargl`, `creall` and `cimagl` have; the
// result is the psABI's COMPLEX_X87 class, ST(0) carrying the real half over
// ST(1) carrying the imaginary one, which is the one place the aggregate
// model is not the ABI and is what musl's eighteen other src/complex units
// return. Where `long double` is `double` -- Win64 and Apple AArch64 -- both
// are ordinary aggregate positions.
//
// AArch64 outside Apple is the exception, and not because of complex: there
// `long double` is the 128-bit format, which this frontend refuses in a
// signature even as a bare scalar, so the section is compiled out rather than
// testing that refusal. The macros are the ones both compilers predefine, so
// the fixture is the same program under either.
#if !defined(__aarch64__) || defined(__APPLE__) || defined(_WIN32)
#define FIXTURE_LONG_DOUBLE_IN_SIGNATURE 1
#else
#define FIXTURE_LONG_DOUBLE_IN_SIGNATURE 0
#endif

#if FIXTURE_LONG_DOUBLE_IN_SIGNATURE
typedef long double _Complex complex_long_double;

long double complex_long_real(complex_long_double z) { return (long double)z; }
long double complex_long_imaginary(complex_long_double z) { return __imag__ z; }
double complex_long_narrow(complex_long_double z) { return (double)z; }
int complex_long_truth(complex_long_double z) { return z ? 5 : 11; }
int complex_long_equal(complex_long_double a, complex_long_double b) { return a == b; }
long double complex_long_add(complex_long_double a, complex_long_double b) { return __real__ (a + b) + __imag__ (a - b); }
long double complex_long_scale(complex_long_double a, long double s) { return __real__ (a * s) + __imag__ (a / s); }
// The result position. `complex_long_relay` is the one that returns what a
// call returned: on System V it pops the pair the callee left on the x87
// stack and pushes it back, which no other shape in this file does.
complex_long_double complex_long_compose(long double r, long double i)
{
    complex_long_double z;
    __real__ z = r;
    __imag__ z = i;
    return z;
}

complex_long_double complex_long_conjugate(complex_long_double z)
{
    __imag__ z = -__imag__ z;
    return z;
}

complex_long_double complex_long_sum(complex_long_double a, complex_long_double b) { return a + b; }
complex_long_double complex_long_product(complex_long_double a, complex_long_double b) { return a * b; }
complex_long_double complex_long_quotient(complex_long_double a, complex_long_double b) { return a / b; }
complex_long_double complex_long_relay(complex_long_double z) { return complex_long_conjugate(z); }
#endif

// `__real__` and `__imag__` as places, which is the half of the GNU operators
// that a member read cannot stand in for.
complex_double complex_set_real(complex_double a, double r)
{
    __real__ a = r;
    return a;
}

complex_double complex_set_imaginary(complex_double a, double r)
{
    __imag__ a = r;
    return a;
}

// The compound assignment operators go through the same lowering as the
// binary ones but reach it from the assignment path.
complex_double complex_compound(complex_double a, complex_double b)
{
    complex_double result = a;
    result += b;
    result *= b;
    result -= a;
    result /= b;
    return result;
}

// musl's <complex.h> builds and inspects complex values through a union whose
// type is defined inside the compound literal's own type name; the whole
// header is unusable without it, so it is part of this contract.
#define FIXTURE_CMPLX(x, y, t) ((union { _Complex t __z; t __xy[2]; }){.__xy = {(x), (y)}}.__z)
#define FIXTURE_CIMAG(x, t) (+(union { _Complex t __z; t __xy[2]; }){(_Complex t)(x)}.__xy[1])

complex_double complex_compose(double x, double y) { return FIXTURE_CMPLX(x, y, double); }
double complex_punned_imaginary(complex_double z) { return FIXTURE_CIMAG(z, double); }
complex_float complex_float_compose(float x, float y) { return FIXTURE_CMPLX(x, y, float); }

static int check(complex_double value, double real, double imaginary)
{
    return __real__ value == real && __imag__ value == imaginary;
}

static int check_float(complex_float value, float real, float imaginary)
{
    return __real__ value == real && __imag__ value == imaginary;
}

#if FIXTURE_LONG_DOUBLE_IN_SIGNATURE
static int check_long(complex_long_double value, long double real, long double imaginary)
{
    return __real__ value == real && __imag__ value == imaginary;
}
#endif

int main(void)
{
    // Layout first: a complex value is two contiguous elements of its real
    // type, no more strictly aligned than one of them.
    if (sizeof(complex_float) != 2 * sizeof(float)) return 1;
    if (sizeof(complex_double) != 2 * sizeof(double)) return 2;
    if (_Alignof(complex_float) != _Alignof(float)) return 3;
    if (_Alignof(complex_double) != _Alignof(double)) return 4;
    if (sizeof(_Complex) != sizeof(complex_double)) return 5;
    if (sizeof(_Complex float) != sizeof(complex_float)) return 6;
    if (sizeof(long double _Complex) != 2 * sizeof(long double)) return 7;

    real_source = 3.0;
    imaginary_source = 4.0;
    complex_double a = complex_compose(real_source, imaginary_source);
    if (!check(a, 3.0, 4.0)) return 8;
    real_source = 1.0;
    imaginary_source = -2.0;
    complex_double b = complex_compose(real_source, imaginary_source);
    if (!check(b, 1.0, -2.0)) return 9;

    if (!check(complex_add(a, b), 4.0, 2.0)) return 10;
    if (!check(complex_subtract(a, b), 2.0, 6.0)) return 11;
    // (3 + 4i)(1 - 2i) = (3 + 8) + (4 - 6)i
    if (!check(complex_multiply(a, b), 11.0, -2.0)) return 12;
    // (3 + 4i)/(1 - 2i) = (3 + 4i)(1 + 2i)/5 = (-5 + 10i)/5
    if (!check(complex_divide(a, b), -1.0, 2.0)) return 13;
    if (!check(complex_negate(a), -3.0, -4.0)) return 14;
    if (!check(complex_promote(a), 3.0, 4.0)) return 15;

    real_source = 2.0;
    // A real operand of `+` or `-` reaches only the real half; the imaginary
    // half of the complex operand is carried through unchanged.
    if (!check(complex_add_real(a, real_source), 5.0, 4.0)) return 16;
    if (!check(complex_real_add(real_source, a), 5.0, 4.0)) return 17;
    if (!check(complex_subtract_real(a, real_source), 1.0, 4.0)) return 18;
    if (!check(complex_real_subtract(real_source, a), -1.0, -4.0)) return 19;
    // A real operand of `*` or `/` scales or divides both halves.
    if (!check(complex_multiply_real(a, real_source), 6.0, 8.0)) return 20;
    if (!check(complex_real_multiply(real_source, a), 6.0, 8.0)) return 21;
    if (!check(complex_divide_real(a, real_source), 1.5, 2.0)) return 22;
    real_source = 5.0;
    // 5/(1 - 2i) = 5(1 + 2i)/5
    if (!check(complex_real_divide(real_source, b), 1.0, 2.0)) return 23;

    if (complex_equal(a, a) != 1) return 24;
    if (complex_equal(a, b) != 0) return 25;
    if (complex_not_equal(a, b) != 1) return 26;
    if (complex_not_equal(a, a) != 0) return 27;
    // Both halves count: two values agreeing in one half are not equal, and a
    // value is true when either half is nonzero.
    if (complex_equal(a, complex_compose(3.0, 0.0)) != 0) return 28;
    if (complex_truth(a) != 7) return 29;
    if (complex_truth(complex_compose(0.0, 0.0)) != 3) return 30;
    if (complex_truth(complex_compose(0.0, 1.0)) != 7) return 31;
    if (complex_truth(complex_compose(1.0, 0.0)) != 7) return 32;

    if (complex_real(a) != 3.0) return 33;
    if (complex_imaginary(a) != 4.0) return 34;
    if (complex_punned_imaginary(a) != 4.0) return 35;
    // A complex converted to a real type keeps the real half.
    if (complex_real_cast(a) != 3.0) return 36;
    if (complex_integer_cast(a) != 3) return 37;
    // A real converted to a complex gains a zero imaginary half.
    real_source = -7.5;
    if (!check(complex_from_real(real_source), -7.5, 0.0)) return 38;
    integer_source = -11;
    if (!check(complex_from_integer(integer_source), -11.0, 0.0)) return 39;

    if (!check_float(complex_narrow(a), 3.0f, 4.0f)) return 40;
    if (!check(complex_widen(complex_narrow(a)), 3.0, 4.0)) return 41;
    // 0.1 is not representable in either width, so narrowing it and widening
    // it back is the check that each half converts on its own.
    complex_double inexact = complex_compose(0.1, -0.1);
    if (complex_real(complex_widen(complex_narrow(inexact))) != (double)(float)0.1) return 42;
    if (complex_imaginary(complex_widen(complex_narrow(inexact))) != (double)(float)-0.1) return 43;

    complex_float fa = complex_float_compose(3.0f, 4.0f);
    complex_float fb = complex_float_compose(1.0f, -2.0f);
    if (!check_float(complex_float_add(fa, fb), 4.0f, 2.0f)) return 44;
    if (!check_float(complex_float_multiply(fa, fb), 11.0f, -2.0f)) return 45;

    if (!check(complex_set_real(a, 9.0), 9.0, 4.0)) return 46;
    if (!check(complex_set_imaginary(a, 9.0), 3.0, 9.0)) return 47;
    // (((a + b) * b) - a) / b, with a = 3 + 4i and b = 1 - 2i:
    // a + b = 4 + 2i; times b = 8 - 6i; minus a = 5 - 10i; over b = 5.
    if (!check(complex_compound(a, b), 5.0, 0.0)) return 48;

    // Signed zero survives: the imaginary half of `a + real` is copied, not
    // added to a positive zero, so a negative zero stays negative.
    complex_double negative_zero_imaginary = complex_compose(1.0, -0.0);
    real_source = 1.0;
    complex_double carried = complex_add_real(negative_zero_imaginary, real_source);
    if (__imag__ carried != 0.0) return 49;
    if (!__builtin_signbit(__imag__ carried)) return 50;

    // The complex value round-trips through a global of its own type, which
    // is the load/store path rather than the register one.
    complex_source = a;
    if (!check(complex_source, 3.0, 4.0)) return 51;
    complex_float_source = fa;
    if (!check_float(complex_float_source, 3.0f, 4.0f)) return 52;

    // `__real__` and `__imag__` on a real operand: the value itself and a
    // zero of its type, which is what makes them usable in generic code.
    real_source = 2.5;
    if (__real__ real_source != 2.5) return 53;
    if (__imag__ real_source != 0.0) return 54;

    // GNU's imaginary literal suffix, in both orders and both letters, is
    // what <complex.h> defines `I` with: musl writes `_Complex_I` as
    // `(0.0f+1.0fi)` and glibc as `(1.0iF)`, so a compiler that takes only
    // one of the two spellings has a working `_Complex` and an unusable
    // header.
    if (!check(0.0 + 1.0i, 0.0, 1.0)) return 55;
    if (!check(1.5 + 1.0i * 13.5, 1.5, 13.5)) return 56;
    if (!check(1.5 + 1.0iF * 13.5, 1.5, 13.5)) return 57;
    if (!check_float(1.25f + 1.0fi * 2.5f, 1.25f, 2.5f)) return 58;
    if (!check(4.0j, 0.0, 4.0)) return 59;
    if (sizeof(1.0fi) != sizeof(complex_float)) return 60;
    if (sizeof(1.0i) != sizeof(complex_double)) return 61;
    // The suffix reaches the type predictor too, not just the value: a
    // conditional whose arms are an imaginary literal and a real must be
    // complex, or the imaginary arm is converted down to its zero real part.
    integer_source = 0;
    if (!check(integer_source ? 2.0 : 1.0i, 0.0, 1.0)) return 62;
    integer_source = 1;
    if (!check(integer_source ? 2.0 : 1.0i, 2.0, 0.0)) return 63;

    // The long double complex argument position, and the conversions out of
    // it. The operands are exact in every `long double` format this compiles
    // for, so the expected values do not depend on which one it is.
#if FIXTURE_LONG_DOUBLE_IN_SIGNATURE
    complex_long_double la;
    __real__ la = 3.5L;
    __imag__ la = -1.25L;
    complex_long_double lb;
    __real__ lb = 0.5L;
    __imag__ lb = 2.0L;
    if (complex_long_real(la) != 3.5L) return 64;
    if (complex_long_imaginary(la) != -1.25L) return 65;
    if (complex_long_narrow(la) != 3.5) return 66;
    if (complex_long_truth(la) != 5) return 67;
    if (complex_long_truth(complex_long_real(la) - 3.5L) != 11) return 68;
    if (complex_long_equal(la, la) != 1) return 69;
    if (complex_long_equal(la, lb) != 0) return 70;
    // (3.5 + 0.5) + imag(3.5 - 1.25i - 0.5 - 2i) = 4 + (-3.25)
    if (complex_long_add(la, lb) != 0.75L) return 71;
    // real(la * 2) + imag(la / 2) = 7 + (-0.625)
    if (complex_long_scale(la, 2.0L) != 6.375L) return 72;

    // The result position. The quotient's denominator is 4 + 4i so that
    // Smith's algorithm divides by 8 and every half stays exact: the answer
    // is (9 - 19i)/32, which no rounding can reach from the wrong formula.
    complex_long_double lc = complex_long_compose(4.0L, 4.0L);
    if (!check_long(lc, 4.0L, 4.0L)) return 73;
    if (!check_long(complex_long_conjugate(la), 3.5L, 1.25L)) return 74;
    if (!check_long(complex_long_relay(la), 3.5L, 1.25L)) return 75;
    if (!check_long(complex_long_sum(la, lc), 7.5L, 2.75L)) return 76;
    if (!check_long(complex_long_product(la, lc), 19.0L, 9.0L)) return 77;
    if (!check_long(complex_long_quotient(la, lc), 0.28125L, -0.59375L)) return 78;
    // A signed zero survives the pair: the imaginary half of the conjugate of
    // +0 is -0, which compares equal to +0 and divides to the other infinity.
    complex_long_double lz = complex_long_conjugate(complex_long_compose(1.0L, 0.0L));
    if (__imag__ lz != 0.0L) return 79;
    if (1.0L / __imag__ lz > 0.0L) return 80;
#endif

    return 0;
}
