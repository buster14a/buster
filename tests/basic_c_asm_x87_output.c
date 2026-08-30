// Reduced from musl's src/math/x86_64/sqrtl.c, rintl.c, fabsl.c, fmodl.c,
// remainderl.c and remquol.c, which musl's ARCH_SRCS builds in place of their
// portable siblings. GNU spells the top of the x87 register stack `t` and the
// one below it `u`, and that stack is the register file an x86-64
// `long double` already lives in -- so these templates are a read-modify-write
// of ST(0) with no move around them.
//
// The stack is a stack rather than a set of registers, and the emitter's whole
// model is here: the operands are pushed deepest first, so `u` ends at ST(1)
// and `t` on top; the template runs; an output in ST(0) is stored and popped;
// and whatever the template left standing is discarded.
//
// `fmodl` is the shape that also names a register literally. `fnstsw %%ax`
// writes AX beside an `"=a"` output, which is the register that operand is
// pinned to -- a register the emitter cannot also hand to anything else, which
// is what makes the literal safe here and unsafe in general.
//
// `remquol` is the one place two implementations could disagree observably:
// the low three bits of the quotient are decoded out of the C0/C1/C3 condition
// flags of the status word, so a `fprem1` that ran against the wrong stack
// position produces a plausible remainder and a wrong quotient.

#if defined(__x86_64__) || defined(_M_X64)

static int failures;

static void check(int condition)
{
    if (!condition)
    {
        failures += 1;
    }
}

// musl's own union ldshape, which is how a wide result is compared without
// rounding it through anything narrower.
union asm_x87_shape
{
    long double value;
    struct
    {
        unsigned long long significand;
        unsigned short sign_exponent;
    } parts;
};

static int same(long double actual, long double expected)
{
    union asm_x87_shape left = {.value = actual};
    union asm_x87_shape right = {.value = expected};
    return left.parts.significand == right.parts.significand && left.parts.sign_exponent == right.parts.sign_exponent;
}

static long double asm_x87_output_sqrtl(long double x)
{
    __asm__("fsqrt" : "+t"(x));
    return x;
}

static long double asm_x87_output_rintl(long double x)
{
    __asm__("frndint" : "+t"(x));
    return x;
}

static long double asm_x87_output_fabsl(long double x)
{
    __asm__("fabs" : "+t"(x));
    return x;
}

// The two-position shape: a read-write output in ST(0), an input in ST(1) the
// template reads and does not pop, and a status word read back out of AX.
static long double asm_x87_output_fmodl(long double x, long double y)
{
    unsigned short fpsr;
    do
    {
        __asm__("fprem; fnstsw %%ax" : "+t"(x), "=a"(fpsr) : "u"(y));
    } while (fpsr & 0x400);
    return x;
}

static long double asm_x87_output_remainderl(long double x, long double y)
{
    unsigned short fpsr;
    do
    {
        __asm__("fprem1; fnstsw %%ax" : "+t"(x), "=a"(fpsr) : "u"(y));
    } while (fpsr & 0x400);
    return x;
}

// musl's remquol verbatim, including the empty template with two `X` inputs
// that keeps the addresses of its operands from being discarded.
static long double asm_x87_output_remquol(long double x, long double y, int* quotient)
{
    signed char* cx = (void*)&x;
    signed char* cy = (void*)&y;
    __asm__("" ::"X"(cx), "X"(cy));

    long double t = x;
    unsigned fpsr;
    do
    {
        __asm__("fprem1; fnstsw %%ax" : "+t"(t), "=a"(fpsr) : "u"(y));
    } while (fpsr & 0x400);
    unsigned char i = fpsr >> 8;
    i = i >> 4 | i << 4;
    unsigned qbits = 0x7575313164642020 >> (i & 60);
    qbits &= 7;

    *quotient = (cx[9] ^ cy[9]) < 0 ? -(int)qbits : (int)qbits;
    return t;
}

int main(void)
{
    int guard = 0x2b2b;
    int quotient = 0;

    check(same(asm_x87_output_sqrtl(4.0L), 2.0L));
    check(same(asm_x87_output_sqrtl(2.25L), 1.5L));
    check(same(asm_x87_output_sqrtl(0.0L), 0.0L));

    check(same(asm_x87_output_rintl(2.5L), 2.0L));
    check(same(asm_x87_output_rintl(3.5L), 4.0L));
    check(same(asm_x87_output_rintl(-1.5L), -2.0L));
    check(same(asm_x87_output_rintl(1234.75L), 1235.0L));

    // The sign of a zero is what separates clearing exactly the sign bit from
    // clearing more or less than it.
    check(same(asm_x87_output_fabsl(-1234.5L), 1234.5L));
    check(same(asm_x87_output_fabsl(-0.0L), 0.0L));
    check(same(asm_x87_output_fabsl(1234.5L), 1234.5L));

    // 1e18 over 3 needs more than one fprem, which is what the loop and the
    // status word are for: a template that always read a zero status would
    // leave the reduction unfinished and still return a number.
    check(same(asm_x87_output_fmodl(7.0L, 3.0L), 1.0L));
    check(same(asm_x87_output_fmodl(-7.0L, 3.0L), -1.0L));
    check(same(asm_x87_output_fmodl(1234.5L, 1.0L), 0.5L));
    check(same(asm_x87_output_fmodl(1000000000000000000.0L, 3.0L), 1.0L));

    check(same(asm_x87_output_remainderl(7.0L, 3.0L), 1.0L));
    check(same(asm_x87_output_remainderl(5.0L, 3.0L), -1.0L));
    check(same(asm_x87_output_remainderl(1000000000000000000.0L, 3.0L), 1.0L));

    check(same(asm_x87_output_remquol(7.0L, 3.0L, &quotient), 1.0L) && quotient == 2);
    check(same(asm_x87_output_remquol(-7.0L, 3.0L, &quotient), -1.0L) && quotient == -2);
    check(same(asm_x87_output_remquol(7.0L, -3.0L, &quotient), 1.0L) && quotient == -2);
    check(same(asm_x87_output_remquol(5.0L, 3.0L, &quotient), -1.0L) && quotient == 2);
    check(same(asm_x87_output_remquol(29.0L, 4.0L, &quotient), 1.0L) && quotient == 7);
    check(same(asm_x87_output_remquol(0.0L, 3.0L, &quotient), 0.0L) && quotient == 0);

    check(guard == 0x2b2b);
    return failures;
}

#else

int main(void)
{
    return 0;
}

#endif
