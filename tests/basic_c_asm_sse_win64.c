// Eight simultaneous `x` outputs consume the complete closed SSE operand
// pool.  On Win64 the low halves of XMM6 and XMM7 are nonvolatile, so MIR has
// to save and restore them around the inline-assembly transaction even though
// the allocator deliberately keeps ordinary vector values out of those
// registers.

#if defined(__x86_64__) || defined(_M_X64)

static unsigned long long bits(double value)
{
    union
    {
        double value;
        unsigned long long bits;
    } pun = {.value = value};
    return pun.bits;
}

int asm_sse_eight_outputs(void)
{
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
    double g;
    double h;
    __asm__("pcmpeqd %0, %0\n\t"
            "pcmpeqd %1, %1\n\t"
            "pcmpeqd %2, %2\n\t"
            "pcmpeqd %3, %3\n\t"
            "pcmpeqd %4, %4\n\t"
            "pcmpeqd %5, %5\n\t"
            "pcmpeqd %6, %6\n\t"
            "pcmpeqd %7, %7"
            : "=x"(a), "=x"(b), "=x"(c), "=x"(d), "=x"(e), "=x"(f), "=x"(g), "=x"(h));
    unsigned long long all = bits(a) & bits(b) & bits(c) & bits(d) & bits(e) & bits(f) & bits(g) & bits(h);
    return all == ~0ull ? 0 : 1;
}

int main(void)
{
    return asm_sse_eight_outputs();
}

#else

int main(void)
{
    return 0;
}

#endif
