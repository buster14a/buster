#if defined(__aarch64__) || defined(_M_ARM64)
#define ASM_BRANCH "b "
#else
#define ASM_BRANCH "jmp "
#endif

static int goto_numeric(int value)
{
    __asm__ goto(ASM_BRANCH "%l2" : : "r"(value) : "cc" : first, second);
    return 0;
first:
    return 1;
second:
    return value + 7;
}

static int goto_named(int value)
{
    __asm__ goto(ASM_BRANCH "%l[taken]" : "+r"(value) : : "memory" : taken);
    return 0;
taken:
    return value + 11;
}

static int goto_swap(int left, int right)
{
    __asm__ goto(ASM_BRANCH "%l4" : "=r"(left), "=r"(right) : "0"(right), "1"(left) : "memory", "cc" : taken);
    return 0;
taken:
    return left * 100 + right;
}

static int goto_fallthrough(int value)
{
    __asm__ goto("" : "+r"(value) : : "memory" : never);
    return value + 13;
never:
    return 0;
}

static int goto_join(int selector)
{
    int value = selector * 3;
    if (selector & 1)
    {
        __asm__ goto("" : : "r"(value) : : never);
        value += 5;
    }
    else
    {
        __asm__ goto(ASM_BRANCH "%l1" : : "r"(value) : : joined);
        value += 100;
    }
joined:
    return value;
never:
    return 0;
}

static int goto_loop(int count)
{
    int total = 0;
    for (int index = 0; index < count; index += 1)
    {
        __asm__ goto(ASM_BRANCH "%l1" : : "r"(index) : "memory" : step);
        total += 1000;
step:
        total += index * 3 + 1;
    }
    return total;
}

static int goto_no_operands(void)
{
    __asm__ goto(ASM_BRANCH "%l1" : : : "memory", "cc" : first, second);
    return 0;
first:
    return 1;
second:
    return 17;
}

int main(void)
{
    return goto_numeric(5) != 12 || goto_named(7) != 18 || goto_swap(3, 7) != 703 || goto_fallthrough(11) != 24 ||
           goto_join(1) != 8 || goto_join(2) != 6 || goto_loop(0) != 0 || goto_loop(8) != 92 || goto_no_operands() != 17;
}
