// Eight simultaneous `x` outputs consume the complete closed SSE operand
// pool. On Win64 the low halves of XMM6-XMM15 are nonvolatile, so MIR has to
// save and restore both allocated operands and explicitly named clobbers
// around the inline-assembly transaction.

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

void asm_sse_explicit_clobbers(void)
{
    __asm__("pcmpeqd %%xmm6, %%xmm6\n\t"
            "pcmpeqd %%xmm15, %%xmm15"
            ::: "xmm6", "xmm15");
}

#if defined(_WIN32)

int asm_sse_clobber_abi_probe(void);

// An independently assembled caller puts an all-ones sentinel in both ends
// of Win64's nonvolatile XMM range, calls the C function above, then observes
// the registers directly. The compiler under test cannot manufacture the
// expected answer because this observer is one module-level assembly block.
__asm__(".text\n"
        ".globl asm_sse_clobber_abi_probe\n"
        "asm_sse_clobber_abi_probe:\n"
        "subq $72, %rsp\n"
        "movdqu %xmm6, 32(%rsp)\n"
        "movdqu %xmm15, 48(%rsp)\n"
        "pcmpeqd %xmm6, %xmm6\n"
        "pcmpeqd %xmm15, %xmm15\n"
        "call asm_sse_explicit_clobbers\n"
        "pmovmskb %xmm6, %eax\n"
        "cmpl $65535, %eax\n"
        "jne asm_sse_clobber_abi_failed\n"
        "pmovmskb %xmm15, %eax\n"
        "cmpl $65535, %eax\n"
        "jne asm_sse_clobber_abi_failed\n"
        "xorl %eax, %eax\n"
        "jmp asm_sse_clobber_abi_done\n"
        "asm_sse_clobber_abi_failed:\n"
        "movl $1, %eax\n"
        "asm_sse_clobber_abi_done:\n"
        "movdqu 32(%rsp), %xmm6\n"
        "movdqu 48(%rsp), %xmm15\n"
        "addq $72, %rsp\n"
        "ret\n");

#endif

int main(void)
{
    int result = asm_sse_eight_outputs();
    asm_sse_explicit_clobbers();
#if defined(_WIN32)
    result |= asm_sse_clobber_abi_probe();
#endif
    return result;
}

#else

int main(void)
{
    return 0;
}

#endif
