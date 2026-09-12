// Exercise asm-goto edge remapping after native wide division splits a block.
#if defined(__aarch64__) || defined(_M_ARM64)
#define ASM_DIVIDE_BRANCH "b %l1"
#else
#define ASM_DIVIDE_BRANCH "jmp %l1"
#endif
static unsigned long long goto_divided(unsigned long long high, unsigned long long low, unsigned long long divisor)
{
    unsigned __int128 wide = ((unsigned __int128)high << 64) | low;
    unsigned long long value = (unsigned long long)(wide / divisor);
    __asm__ goto(ASM_DIVIDE_BRANCH : : "r"(value) : "memory" : taken);
    return 0;
taken:
    return value;
}

int main(void)
{
    return goto_divided(1, 7, 3) != 6148914691236517207ULL;
}
