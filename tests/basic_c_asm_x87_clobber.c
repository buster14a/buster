// Reduced from musl's src/math/x86_64/llrintl.c and lrintl.c. `fistpll` pops
// the x87 stack, so the template takes its operand in ST(0) through `t` and
// says what it did to the stack by clobbering `st`.
//
// That clobber is the whole of what the emitter needs to unwind correctly: the
// operand it pushed is gone, so nothing is read back and nothing is popped
// after the template. Without it the emitter would pop a register the template
// already discarded, which is a stack underflow rather than a wrong number --
// and with it on a template that did not pop, the operand would be left
// standing across the rest of the function.
//
// The result comes back through a memory output rather than a register,
// because that is the only place `fistpll` can put it.

#if defined(__x86_64__) || defined(_M_X64)

static int failures;

static void check(long long actual, long long expected)
{
    if (actual != expected)
    {
        failures += 1;
    }
}

static long long asm_x87_clobber_llrintl(long double x)
{
    long long r;
    __asm__("fistpll %0" : "=m"(r) : "t"(x) : "st");
    return r;
}

static long asm_x87_clobber_lrintl(long double x)
{
    long r;
    __asm__("fistpll %0" : "=m"(r) : "t"(x) : "st");
    return r;
}

int main(void)
{
    int guard = 0x2b2b;

    // The conversion rounds under the current rounding mode, which is round to
    // nearest even at entry.
    check(asm_x87_clobber_llrintl(0.0L), 0);
    check(asm_x87_clobber_llrintl(-1.5L), -2);
    check(asm_x87_clobber_llrintl(2.5L), 2);
    check(asm_x87_clobber_llrintl(1234.75L), 1235);
    check(asm_x87_clobber_llrintl(-1234.75L), -1235);
    check(asm_x87_clobber_llrintl(1099511627776.0L), 1099511627776LL);
    check(asm_x87_clobber_lrintl(0.0L), 0);
    check(asm_x87_clobber_lrintl(-1234.75L), -1235);
    check(asm_x87_clobber_lrintl(1099511627776.0L), 1099511627776L);

    // Eight calls in a row is what says the stack is balanced: the x87 stack is
    // eight deep, so a template whose operand was pushed and never popped
    // overflows it inside one function rather than at some later call.
    long long total = 0;
    for (int index = 0; index < 8; index += 1)
    {
        total += asm_x87_clobber_llrintl((long double)index + 0.25L);
    }
    check(total, 28);

    check(guard, 0x2b2b);
    return failures;
}

#else

int main(void)
{
    return 0;
}

#endif
