// Reduced from musl's src/math/x86_64/lrint.c. The output is an ordinary
// general register, so the refusal comes from the `x` input alone: an input
// constraint the frontend does not recognize falls through to the matching
// constraint parser, which reports it as malformed rather than as unsupported.
long asm_sse_input(double x)
{
    long r;
    __asm__("cvtsd2si %1, %0" : "=r"(r) : "x"(x));
    return r;
}
