void compiler_barrier_memory(void)
{
    __asm__ __volatile__("" : : : "memory");
}

int main(void)
{
    compiler_barrier_memory();
    return 0;
}
