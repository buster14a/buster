// Reduced from musl's src/math/x86_64/sqrtl.c. `t` names the top of the x87
// register stack, which is where an x86-64 `long double` already lives, so the
// operand is a read-modify-write of ST(0) with no move around it. The frontend
// has no x87 operand class for inline assembly and refuses the output.
long double asm_x87_output(long double x)
{
    __asm__("fsqrt" : "+t"(x));
    return x;
}
