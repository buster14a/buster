// Reduced from musl's src/math/x86_64/sqrt.c and src/math/x86_64/fabs.c, which
// musl's ARCH_SRCS builds in place of the portable src/math/sqrt.c and
// src/math/fabs.c. GNU's `x` names the SSE register class, which is a second
// register file rather than another spelling of the general registers: an
// operand in it is allocated a vector register out of its own pool, carried in
// and out of its frame slot by a scalar move, and spelled in the template by
// the register alone.
//
// Both output spellings are here because they are lowered differently. A write
// only `=x` never reads the slot, which is what lets `pcmpeqd %0, %0` build a
// mask out of a register whose previous contents are irrelevant; a read-write
// `+x` loads the slot first, which is what `andps` and `psrlq` need.
//
// The answers are checked rather than only assembled: a template that ran with
// the wrong register still assembles and still returns some number.

#if defined(__x86_64__) || defined(_M_X64)

static int failures;

static void check(unsigned long long actual, unsigned long long expected)
{
    if (actual != expected)
    {
        failures += 1;
    }
}

static unsigned long long double_bits(double value)
{
    union
    {
        double value;
        unsigned long long bits;
    } pun = {.value = value};
    return pun.bits;
}

static unsigned int float_bits(float value)
{
    union
    {
        float value;
        unsigned int bits;
    } pun = {.value = value};
    return pun.bits;
}

// musl's sqrt verbatim: one write-only output and one input in the same class.
static double asm_sse_output_sqrt(double x)
{
    __asm__("sqrtsd %1, %0" : "=x"(x) : "x"(x));
    return x;
}

static float asm_sse_output_sqrtf(float x)
{
    __asm__("sqrtss %1, %0" : "=x"(x) : "x"(x));
    return x;
}

// musl's fabs verbatim: a mask built in a register nothing loaded, narrowed by
// a read-write shift, and applied to a read-write value.
static double asm_sse_output_fabs(double x)
{
    double t;
    __asm__("pcmpeqd %0, %0" : "=x"(t));
    __asm__("psrlq   $1, %0" : "+x"(t));
    __asm__("andps   %1, %0" : "+x"(x) : "x"(t));
    return x;
}

static float asm_sse_output_fabsf(float x)
{
    float t;
    __asm__("pcmpeqd %0, %0" : "=x"(t));
    __asm__("psrld   $1, %0" : "+x"(t));
    __asm__("andps   %1, %0" : "+x"(x) : "x"(t));
    return x;
}

int main(void)
{
    int guard = 0x2b2b;

    check(double_bits(asm_sse_output_sqrt(4.0)), double_bits(2.0));
    check(double_bits(asm_sse_output_sqrt(2.25)), double_bits(1.5));
    check(double_bits(asm_sse_output_sqrt(0.0)), double_bits(0.0));
    check(float_bits(asm_sse_output_sqrtf(4.0f)), float_bits(2.0f));
    check(float_bits(asm_sse_output_sqrtf(2.25f)), float_bits(1.5f));

    // The sign of a zero is what separates a mask that cleared exactly the sign
    // bit from one that cleared more or less than it.
    check(double_bits(asm_sse_output_fabs(-1234.5)), double_bits(1234.5));
    check(double_bits(asm_sse_output_fabs(1234.5)), double_bits(1234.5));
    check(double_bits(asm_sse_output_fabs(-0.0)), double_bits(0.0));
    check(double_bits(asm_sse_output_fabs(-1e-300)), double_bits(1e-300));
    check(float_bits(asm_sse_output_fabsf(-1234.5f)), float_bits(1234.5f));
    check(float_bits(asm_sse_output_fabsf(-0.0f)), float_bits(0.0f));
    check(float_bits(asm_sse_output_fabsf(-1e-30f)), float_bits(1e-30f));

    check((unsigned long long)guard, 0x2b2b);
    return failures;
}

#else

int main(void)
{
    return 0;
}

#endif
