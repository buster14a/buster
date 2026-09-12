void hint_nop(void)
{
    __asm__ volatile("nop");
}

void hint_nop_clobbers(void)
{
    __asm__ volatile("nop" : : : "memory", "cc");
}

void hint_spin(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile("yield");
#else
    __asm__ volatile("pause");
#endif
}

void hint_spin_clobbers(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile("yield" : : : "cc", "memory");
#else
    __asm__ volatile("pause" : : : "cc", "memory");
#endif
}

int main(void)
{
    volatile int visits = 0;
    for (int index = 0; index < 4; index += 1)
    {
        visits += 1;
        hint_nop();
        hint_nop_clobbers();
        hint_spin();
        hint_spin_clobbers();
    }
    return visits != 4;
}
