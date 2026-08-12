#if defined(__x86_64__) || defined(_M_X64)
static int fixed_and_generic(int value, int other)
{
    __asm__ volatile("inc %0\ninc %1" : "+a"(value), "+r"(other));
    return value;
}

static int tied_read_write(int value)
{
    int result;
    __asm__ volatile("mov %0, %0" : "=r"(result) : "0"(value));
    return result;
}

static int named_register_token(int value)
{
    __asm__ volatile("inc %[rax]" : [rax] "+r"(value));
    return value;
}

static unsigned char width8(unsigned char value)
{
    __asm__ volatile("inc %0\ninc %0" : "+r"(value));
    return value;
}

static unsigned short width16(unsigned short value)
{
    __asm__ volatile("inc %0" : "+a"(value));
    return value;
}

static unsigned int width32(unsigned int value)
{
    __asm__ volatile("inc %0" : "+r"(value));
    return value;
}

static unsigned long long width64(unsigned long long value)
{
    unsigned long long result;
    __asm__ volatile("inc %0" : "=r"(result) : "0"(value));
    return result;
}

int main(void)
{
    return fixed_and_generic(6, 7) == 7 && tied_read_write(53) == 53 && named_register_token(72) == 73 && width8(10) == 12 &&
                   width16(20) == 21 && width32(30) == 31 && width64(40) == 41
               ? 0
               : 1;
}
#else
int main(void)
{
    return 0;
}
#endif
