// Reduced from musl's src/math/x86_64/lrint.c and its three siblings, which
// musl's ARCH_SRCS builds in place of the portable src/math/lrint.c. The
// output is an ordinary general register, so the SSE class appears here only as
// an input: the two register files meet in one template, and each operand is
// carried in through its own file's move.
//
// This is also the position that used to name the wrong thing. An input
// constraint the frontend did not recognize fell through to the matching
// constraint parser, so `x` was reported as a malformed matching constraint
// rather than as an operand class the target did not have.
//
// The conversion rounds under the current rounding mode, which is round to
// nearest even at entry and is what the expected values below are written for.

#if defined(__x86_64__) || defined(_M_X64)

static int failures;

static void check(long long actual, long long expected)
{
    if (actual != expected)
    {
        failures += 1;
    }
}

static long asm_sse_input_lrint(double x)
{
    long r;
    __asm__("cvtsd2si %1, %0" : "=r"(r) : "x"(x));
    return r;
}

static long long asm_sse_input_llrint(double x)
{
    long long r;
    __asm__("cvtsd2si %1, %0" : "=r"(r) : "x"(x));
    return r;
}

static long asm_sse_input_lrintf(float x)
{
    long r;
    __asm__("cvtss2si %1, %0" : "=r"(r) : "x"(x));
    return r;
}

static long long asm_sse_input_llrintf(float x)
{
    long long r;
    __asm__("cvtss2si %1, %0" : "=r"(r) : "x"(x));
    return r;
}

int main(void)
{
    int guard = 0x2b2b;

    check(asm_sse_input_lrint(0.0), 0);
    check(asm_sse_input_lrint(-1.5), -2);
    check(asm_sse_input_lrint(2.5), 2);
    check(asm_sse_input_lrint(1234.75), 1235);
    check(asm_sse_input_lrint(-1234.75), -1235);
    check(asm_sse_input_llrint(1099511627776.0), 1099511627776LL);
    check(asm_sse_input_lrintf(0.0f), 0);
    check(asm_sse_input_lrintf(-1.5f), -2);
    check(asm_sse_input_lrintf(2.5f), 2);
    check(asm_sse_input_llrintf(-1234.75f), -1235);

    check(guard, 0x2b2b);
    return failures;
}

#else

int main(void)
{
    return 0;
}

#endif
