#if defined(__aarch64__) || defined(_M_ARM64)
#define ASM_BRANCH "b "
#define ASM_CONDITIONAL "cbnz %w0, %l1"
#define ASM_THREE_WAY "cbz %w0, %l1\ncmp %w0, #1\nb.eq %l2"
#define ASM_READ_WRITE "add %w0, %w0, #2\ncbnz %w0, %l2"
#else
#define ASM_BRANCH "jmp "
#define ASM_CONDITIONAL "test %0, %0\njne %l1"
#define ASM_THREE_WAY "test %0, %0\nje %l1\ncmp $1, %0\nje %l2"
#define ASM_READ_WRITE "add $2, %0\ntest %0, %0\njne %l2"
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

static int goto_conditional(int value)
{
    __asm__ goto(ASM_CONDITIONAL : : "r"(value) : "cc" : taken);
    return 21;
taken:
    return 22;
}

static int goto_multiple_targets(int value)
{
    __asm__ goto(ASM_THREE_WAY : : "r"(value) : "cc" : zero, one);
    return 30;
zero:
    return 31;
one:
    return 32;
}

static int goto_read_write(int value)
{
    __asm__ goto(ASM_READ_WRITE : "+r"(value) : : "cc" : nonzero);
    return value + 40;
nonzero:
    return value + 50;
}

int main(void)
{
    return goto_numeric(5) != 12 || goto_named(7) != 18 || goto_swap(3, 7) != 703 || goto_fallthrough(11) != 24 ||
           goto_join(1) != 8 || goto_join(2) != 6 || goto_loop(0) != 0 || goto_loop(8) != 92 || goto_no_operands() != 17 ||
           goto_conditional(0) != 21 || goto_conditional(7) != 22 || goto_multiple_targets(0) != 31 ||
           goto_multiple_targets(1) != 32 || goto_multiple_targets(2) != 30 || goto_read_write(-2) != 40 || goto_read_write(3) != 55;
}
