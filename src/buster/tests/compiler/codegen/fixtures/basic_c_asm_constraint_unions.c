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

// A union is a set: the letters carry no order, and any set holding 'r' or 'g'
// selects a general register while "am"/"ma" and "dN"/"Nd" name rax and rdx.
int constraint_ri_variable(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "ri"(value));
    return result;
}

int constraint_ir_constant(void)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "ir"(-123));
    return result;
}

int constraint_rI_variable(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "rI"(value));
    return result;
}

int constraint_rm_output(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=rm"(result) : "r"(value));
    return result;
}

int constraint_rm_input(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "rm"(value));
    return result;
}

int constraint_mr_read_write(int value)
{
    __asm__ volatile("add $1,%0" : "+mr"(value) : : "cc");
    return value;
}

int constraint_rme_variable(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=&mr"(result) : "r"(value));
    __asm__ volatile("add %1,%0" : "+r"(result) : "rme"(value));
    return result;
}

int constraint_g_variable(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "g"(value));
    return result;
}

int constraint_g_output(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=g"(result) : "r"(value));
    return result;
}

int constraint_g_constant(void)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "g"(-71));
    return result;
}

int constraint_ma_identity(int value)
{
    int result;
    __asm__ volatile("" : "=ma"(result) : "0"(value));
    return result;
}

int constraint_am_input(int value)
{
    int result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "am"(value));
    return result;
}

unsigned short constraint_Nd_variable(unsigned short value)
{
    unsigned short result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "Nd"(value));
    return result;
}

unsigned short constraint_Nd_large(void)
{
    unsigned short result;
    __asm__ volatile("mov %1,%0" : "=r"(result) : "Nd"((unsigned short)300));
    return result;
}

unsigned long long constraint_numeric_clobber(unsigned long long value)
{
    unsigned long long live = value + 37;
    __asm__ volatile("xor %%eax,%%eax" : : : "0", "cc");
    return live + value;
}

typedef struct
{
    unsigned char bytes[32];
} constraint_fpu_environment;

// A memory operand passes only its address, so an aggregate larger than a
// register pair is accepted; the x87 control word of a fresh process is 0x37f.
int constraint_m_wide_environment(void)
{
    constraint_fpu_environment saved = {{0}};
    __asm__ volatile("fnstenv %0" : "=m"(saved));
    __asm__ volatile("fldenv %0" : : "m"(saved));
    return saved.bytes[0] == 0x7f && saved.bytes[1] == 0x03;
}

int main(void)
{
    int values[3] = {11, 13, 17};
    int index = 1;
    int input = 37;
    return !constraint_m_wide_environment() || constraint_am_identity(-123) != -123 ||
           constraint_am_wide(0xfedcba9876543210ULL) != 0xfedcba9876543210ULL ||
           constraint_am_early(42) != 43 || constraint_am_place(values, &index, 29) != 2 ||
           values[0] != 11 || values[1] != 29 || values[2] != 17 ||
           constraint_dN_variable(0xfedc) != 0xfedc || constraint_dN_small() != 255 || constraint_dN_large() != 256 ||
           constraint_rn_variable(-71) != -71 || constraint_nr_variable(0xfedcba9876543210ULL) != 0xfedcba9876543210ULL ||
           constraint_rn_constant() != -123 || constraint_nr_pointer(values) != values ||
           constraint_rn_once(&input) != 37 || input != 38 ||
           constraint_ri_variable(-5) != -5 || constraint_ir_constant() != -123 || constraint_rI_variable(41) != 41 ||
           constraint_rm_output(-9) != -9 || constraint_rm_input(-10) != -10 || constraint_mr_read_write(8) != 9 || constraint_rme_variable(77) != 154 ||
           constraint_g_variable(-33) != -33 || constraint_g_output(-34) != -34 || constraint_g_constant() != -71 || constraint_ma_identity(-61) != -61 ||
           constraint_am_input(19) != 19 || constraint_Nd_variable(0x1234) != 0x1234 || constraint_Nd_large() != 300 ||
           constraint_numeric_clobber(0x123456789abcdef0ULL) != 0x2468acf13579be05ULL;
}
