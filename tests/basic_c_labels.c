static int computed_goto_value(int selector)
{
    void *target = (void *)&&dispatch;
    if (selector)
    {
        goto *target;
    }
    return 3;
dispatch:
    return 7;
}

static int asm_goto_value(int selector)
{
    if (selector)
#if defined(__aarch64__) || defined(_M_ARM64)
        __asm__ goto ("b %l1" : : "r"(selector) : "cc" : jump);
#else
        __asm__ goto ("jmp %l1" : : "r"(selector) : "cc" : jump);
#endif
    return 5;
jump:
    return 11;
}

int main(void)
{
    return computed_goto_value(1) != 7 || computed_goto_value(0) != 3 || asm_goto_value(1) != 11 || asm_goto_value(0) != 5;
}
