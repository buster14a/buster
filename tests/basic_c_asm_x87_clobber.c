// Reduced from musl's src/math/x86_64/llrintl.c. `fistpll` pops the x87 stack,
// so the template clobbers `st` and takes its operand in ST(0) through `t`.
// The clobber is checked before the operands are, so this is the refusal that
// names the register file rather than the constraint.
long long asm_x87_clobber(long double x)
{
    long long r;
    __asm__("fistpll %0" : "=m"(r) : "t"(x) : "st");
    return r;
}
