// The caller half of the COMPLEX_X87 result pair.  System V x86-64 returns a
// `long double _Complex` on the x87 stack -- ST(0) the real half, ST(1) the
// imaginary one -- where the identically laid out `struct { long double a, b; }`
// is returned in memory through a hidden pointer.  That is the one place the
// two-field aggregate model of a complex value is not the ABI, so it is the
// one shape a single translation unit cannot check: a caller and a callee this
// compiler produced agree with each other whatever they agree on.  The
// argument direction is a thirty-two-byte memory slot in both models and is
// here as the control.
#if defined(__x86_64__) && !defined(_WIN32)

typedef long double _Complex ldc;
typedef unsigned long u64;
typedef unsigned short u16;

// musl's own view of a wide value, which is how the fixture reads a sign that
// a comparison cannot see.
union ldshape
{
    long double f;
    struct
    {
        u64 m;
        u16 se;
    } i;
};

extern ldc ldc_compose(long double real, long double imaginary);
extern ldc ldc_conjugate(ldc z);
extern ldc ldc_bump(int before, ldc z, int after);
extern ldc ldc_sum(ldc a, ldc b);
extern ldc ldc_relay(ldc z);
extern long double ldc_real(ldc z);
extern long double ldc_imaginary(ldc z);
extern long double ldc_discard(ldc z);

static int check(ldc value, long double real, long double imaginary)
{
    return __real__ value == real && __imag__ value == imaginary;
}

int main(void)
{
    // Exact to a 64-bit significand and not representable in a double, so a
    // half that quietly moved through an f64 fails here.
    long double pi = 3.14159265358979323846264338327950288L;
    ldc z = ldc_compose(pi, -pi);
    if (!check(z, pi, -pi)) return 1;
    union ldshape real_shape;
    real_shape.f = __real__ z;
    if (real_shape.i.m != 0xc90fdaa22168c235UL || real_shape.i.se != 0x4000) return 2;
    union ldshape imaginary_shape;
    imaginary_shape.f = __imag__ z;
    if (imaginary_shape.i.m != 0xc90fdaa22168c235UL || imaginary_shape.i.se != 0xc000) return 3;

    // Which half is which: a swapped pair passes every test whose two halves
    // are the same magnitude, so nothing here uses one.
    ldc asymmetric = ldc_compose(3.5L, -1.25L);
    if (!check(asymmetric, 3.5L, -1.25L)) return 4;
    if (ldc_real(asymmetric) != 3.5L) return 5;
    if (ldc_imaginary(asymmetric) != -1.25L) return 6;
    if (!check(ldc_conjugate(asymmetric), 3.5L, 1.25L)) return 7;
    if (!check(ldc_relay(asymmetric), 3.5L, 1.25L)) return 8;
    if (!check(ldc_bump(11, asymmetric, 22), 14.5L, 20.75L)) return 9;

    // A subnormal keeps a clear integer bit and a zero exponent field, which
    // a round trip through anything narrower would flush away.
    union ldshape tiny;
    tiny.f = __real__ ldc_compose(0x1p-16400L, 1.0L);
    if (tiny.i.se != 0 || tiny.i.m != 0x0000200000000000UL) return 10;

    // Two pairs in and one out, which is the argument shape the eighteen
    // src/complex units that take two operands have. The multiply and divide
    // that would go with it live in basic_c_complex_arithmetic.c: they lower
    // to a compiler-runtime helper under the host compiler, and this link has
    // no compiler runtime on it.
    ldc other = ldc_compose(4.0L, 4.0L);
    if (!check(ldc_sum(asymmetric, other), 7.5L, 2.75L)) return 11;

    // A negative zero compares equal to a positive one, so the sign comes
    // back through the field view.
    union ldshape negative_zero;
    negative_zero.f = __imag__ ldc_conjugate(ldc_compose(1.0L, 0.0L));
    if (negative_zero.i.se != 0x8000 || negative_zero.i.m != 0) return 12;

    if (ldc_discard(asymmetric) != 3.5L) return 13;
    // Eight discarded results is the whole x87 stack: a pair that leaked one
    // entry per call has faulted by now.
    for (int index = 0; index < 8; index += 1)
    {
        if (ldc_discard(asymmetric) != 3.5L) return 14;
    }

    return 0;
}

#else

int main(void)
{
    return 0;
}

#endif
