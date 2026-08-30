// Reduced from musl's src/math/x86_64/sqrt.c, which musl's ARCH_SRCS builds in
// place of the portable src/math/sqrt.c. The `x` constraint names the SSE
// register class, and the frontend has no such class: only the fixed general
// registers, `r` and `m` are recognized, so the output operand is refused
// before the input one is looked at. test_musl reports this unit as
// MUSL_UNSUPPORTED and its archive falls back to the portable file.
double asm_sse_output(double x)
{
    __asm__("sqrtsd %1, %0" : "=x"(x) : "x"(x));
    return x;
}
