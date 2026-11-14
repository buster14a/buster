unsigned long long f(void)
{
    __builtin_ia32_pause();
    return __rdtsc() + __builtin_ia32_xgetbv(0);
}
