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

static int goto_repeated_target(int value)
{
    __asm__ goto(ASM_CONDITIONAL : : "r"(value) : "cc" : first, second);
    return 60;
first:
second:
    return 61;
}

static int goto_label_address_addend(void)
{
    int result = 70;
#if defined(__x86_64__) || defined(_M_X64)
    void* address = 0;
    __asm__ goto("lea %l[target]+4(%%rip), %0" : "=r"(address) : : : target);
    result = (char*)address == (char*)&&target + 4 ? 70 : 71;
    goto done;
target:
    result = 72;
done:
#endif
    return result;
}

static int goto_distinct_control_addends(int value)
{
    int result = 82;
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ goto("test %0, %0\n"
                 "jne %l[target]+1\n"
                 "jmp %l[target]"
                 : : "r"(value) : "cc" : target);
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ goto("cbnz %w0, %l[target]+4\n"
                 "b %l[target]"
                 : : "r"(value) : "cc" : target);
#else
    if (value)
    {
        goto after_nop;
    }
    goto target;
#endif
    goto done;
target:
#if defined(__x86_64__) || defined(_M_X64)
    __asm__("nop");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__("nop");
#else
    goto done;
after_nop:
#endif
    result = 80;
done:
    return result;
}

static int goto_short_loop(int count)
{
    int result = 90;
#if defined(__x86_64__) || defined(_M_X64)
    long counter = count;
    __asm__ goto("loop %l2" : "+c"(counter) : : : target);
#else
    count -= 1;
    if (count)
    {
        goto target;
    }
#endif
    goto done;
target:
    result = 91;
done:
    return result;
}

static int goto_short_jrcxz(long value)
{
    int result = 92;
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ goto("jrcxz %l1" : : "c"(value) : : target);
#else
    if (!value)
    {
        goto target;
    }
#endif
    goto done;
target:
    result = 93;
done:
    return result;
}

int main(void)
{
    return goto_numeric(5) != 12 || goto_named(7) != 18 || goto_swap(3, 7) != 703 || goto_fallthrough(11) != 24 ||
           goto_join(1) != 8 || goto_join(2) != 6 || goto_loop(0) != 0 || goto_loop(8) != 92 || goto_no_operands() != 17 ||
           goto_conditional(0) != 21 || goto_conditional(7) != 22 || goto_multiple_targets(0) != 31 ||
           goto_multiple_targets(1) != 32 || goto_multiple_targets(2) != 30 || goto_read_write(-2) != 40 ||
           goto_read_write(3) != 55 || goto_repeated_target(0) != 60 || goto_repeated_target(1) != 61 ||
           goto_label_address_addend() != 70 || goto_distinct_control_addends(0) != 80 ||
           goto_distinct_control_addends(1) != 80 || goto_short_loop(1) != 90 || goto_short_loop(2) != 91 ||
           goto_short_jrcxz(1) != 92 || goto_short_jrcxz(0) != 93;
}
