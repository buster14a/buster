// Valid GNU assembly shapes which are deliberately NOT compiler-barrier
// selections. Read only by the selector rejection regression, never by the
// strict positive corpus. No function needs to execute to prove rejection.
void compiler_barrier_template(void)
{
    __asm__ __volatile__("nop" : : : "memory");
}

void compiler_barrier_input(int value)
{
    __asm__ __volatile__("" : : "r"(value) : "memory");
}

int compiler_barrier_read_write(int value)
{
    __asm__ __volatile__("" : "+r"(value) : : "memory");
    return value;
}

int compiler_barrier_numeric_tie(int value)
{
    int result;
    __asm__ __volatile__("" : "=r"(result) : "0"(value) : "memory");
    return result;
}

int compiler_barrier_named_tie(int value)
{
    int result;
    __asm__ __volatile__("" : [dst] "=r"(result) : "[dst]"(value) : "memory");
    return result;
}

void compiler_barrier_register_after_memory(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("" : : : "memory", "rax");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("" : : : "memory", "x9");
#else
#error Unsupported compiler-barrier test architecture
#endif
}

void compiler_barrier_register_before_cc(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("" : : : "rax", "cc");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("" : : : "x9", "cc");
#else
#error Unsupported compiler-barrier test architecture
#endif
}

void compiler_barrier_goto(void)
{
    __asm__ goto("" : : : "memory" : done);
done:
    return;
}
