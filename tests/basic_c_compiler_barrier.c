// This entire file belongs to the strict no-machine-fallback driver corpus.
// Keep operand-bearing and physical-register-clobber probes in the separate
// fallback fixture so unsupported forms cannot be silently whitelisted here.
void compiler_barrier_memory(void)
{
    __asm__ __volatile__("" : : : "memory");
}

void compiler_barrier_empty_extended(void)
{
    __asm__ __volatile__("" : : :);
}

void compiler_barrier_empty_basic(void)
{
    __asm__ __volatile__("");
}

void compiler_barrier_implicit_volatile(void)
{
    __asm__("" : : :);
}

void compiler_barrier_cc(void)
{
    __asm__ __volatile__("" : : : "cc");
}

void compiler_barrier_memory_cc(void)
{
    __asm__ __volatile__("" : : : "memory", "cc");
}

void compiler_barrier_cc_memory(void)
{
    __asm__ __volatile__("" : : : "cc", "memory");
}

int main(void)
{
    compiler_barrier_memory();
    compiler_barrier_empty_extended();
    compiler_barrier_empty_basic();
    compiler_barrier_implicit_volatile();
    compiler_barrier_cc();
    compiler_barrier_memory_cc();
    compiler_barrier_cc_memory();
    return 0;
}
