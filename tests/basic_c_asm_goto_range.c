#if defined(__aarch64__) || defined(_M_ARM64)
#define ASM_NOP_1 "nop\n"
#define ASM_NOP_2 ASM_NOP_1 ASM_NOP_1
#define ASM_NOP_4 ASM_NOP_2 ASM_NOP_2
#define ASM_NOP_8 ASM_NOP_4 ASM_NOP_4
#define ASM_NOP_16 ASM_NOP_8 ASM_NOP_8
#define ASM_NOP_32 ASM_NOP_16 ASM_NOP_16
#define ASM_NOP_64 ASM_NOP_32 ASM_NOP_32
#define ASM_NOP_128 ASM_NOP_64 ASM_NOP_64
#define ASM_NOP_256 ASM_NOP_128 ASM_NOP_128
#define ASM_NOP_512 ASM_NOP_256 ASM_NOP_256
#define ASM_NOP_1024 ASM_NOP_512 ASM_NOP_512
#define ASM_NOP_2048 ASM_NOP_1024 ASM_NOP_1024
#define ASM_NOP_4096 ASM_NOP_2048 ASM_NOP_2048
#define ASM_NOP_8192 ASM_NOP_4096 ASM_NOP_4096

int asm_goto_test_range(int value)
{
    int result = 3;
    __asm__ goto("b .Lrange_entry\n"
                 ".Lrange_backward:\n"
                 "nop\n"
                 "b .Lrange_forward\n"
                 ".Lrange_entry:\n"
                 "tbz %w0, #0, %l1\n"
                 "b .Lrange_after_internal\n"
                 ".Lrange_forward:\n"
                 "b .Lrange_backward\n"
                 ".Lrange_after_internal:\n"
                 ASM_NOP_8192 ASM_NOP_1024 : : "r"(value) : : taken);
    goto done;
taken:
    result = 7;
done:
    return result;
}
#else
int asm_goto_test_range(int value)
{
    return value ? 3 : 7;
}
#endif

int main(void)
{
    return asm_goto_test_range(0) != 7 || asm_goto_test_range(1) != 3;
}
