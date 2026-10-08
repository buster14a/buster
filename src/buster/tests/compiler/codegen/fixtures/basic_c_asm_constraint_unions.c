int constraint_am_identity(int value)
{
    int result;
    __asm__ volatile("" : "=am"(result) : "0"(value));
    return result;
}

unsigned long long constraint_am_wide(unsigned long long value)
{
    __asm__ volatile("" : "+am"(value));
    return value;
}

int constraint_am_early(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=&am"(result) : "r"(value));
    __asm__ volatile("add $1,%0" : "+&am"(result) : : "cc");
    return result;
}

int constraint_am_place(int *values, int *index, int value)
{
    __asm__ volatile("mov %1,%0" : "=am"(values[(*index)++]) : "r"(value));
    return *index;
}

unsigned short constraint_dN_variable(unsigned short value)
{
    unsigned short result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "dN"(value));
    return result;
}

unsigned short constraint_dN_small(void)
{
    unsigned short result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "dN"((unsigned short)255));
    return result;
}

unsigned short constraint_dN_large(void)
{
    unsigned short result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "dN"((unsigned short)256));
    return result;
}

int constraint_rn_variable(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "rn"(value));
    return result;
}

unsigned long long constraint_nr_variable(unsigned long long value)
{
    unsigned long long result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "nr"(value));
    return result;
}

int constraint_rn_constant(void)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "rn"(-123));
    return result;
}

int *constraint_nr_pointer(int *value)
{
    int *result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "nr"(value));
    return result;
}

int constraint_rn_once(int *value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "rn"((*value)++));
    return result;
}

unsigned long long constraint_numeric_clobber(unsigned long long value)
{
    unsigned long long live = value + 37;
    __asm__ volatile("xor %%eax,%%eax" : : : "0", "cc");
    return live + value;
}

int main(void)
{
    int values[3] = {11, 13, 17};
    int index = 1;
    int input = 37;
    return constraint_am_identity(-123) != -123 ||
           constraint_am_wide(0xfedcba9876543210ULL) != 0xfedcba9876543210ULL ||
           constraint_am_early(42) != 43 || constraint_am_place(values, &index, 29) != 2 ||
           values[0] != 11 || values[1] != 29 || values[2] != 17 ||
           constraint_dN_variable(0xfedc) != 0xfedc || constraint_dN_small() != 255 || constraint_dN_large() != 256 ||
           constraint_rn_variable(-71) != -71 || constraint_nr_variable(0xfedcba9876543210ULL) != 0xfedcba9876543210ULL ||
           constraint_rn_constant() != -123 || constraint_nr_pointer(values) != values ||
           constraint_rn_once(&input) != 37 || input != 38 ||
           constraint_numeric_clobber(0x123456789abcdef0ULL) != 0x2468acf13579be05ULL;
}
