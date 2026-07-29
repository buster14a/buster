extern int global_asm_answer(void);

#if defined(__x86_64__) || defined(_M_X64)
__asm__(
    ".text\n"
    ".globl global_asm_answer\n"
    ".type global_asm_answer, @function\n"
    "global_asm_answer:\n"
    "movl $42, %eax\n"
    "ret\n");
#elif defined(__aarch64__) || defined(_M_ARM64)
__asm__(
    ".text\n"
    ".globl global_asm_answer\n"
    ".type global_asm_answer, %function\n"
    "global_asm_answer:\n"
    "mov w0, #42\n"
    "ret\n");
#endif

static int compiler_barrier(int value)
{
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile (
        ""
        : "+r"(value)
        :
        : "memory");
    __asm__ volatile ("nop");
    __asm__ volatile ("pause");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile ("" : : : "memory");
    __asm__ volatile ("nop");
    __asm__ volatile ("yield");
#endif
    return value;
}

int main(void)
{
    return compiler_barrier(37) == 37 &&
            global_asm_answer() == 42 ?
        0 : 1;
}
