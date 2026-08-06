extern int global_asm_answer(void);

#if defined(__x86_64__) || defined(_M_X64)
__asm__(".text\n"
        ".globl global_asm_answer\n"
        ".type global_asm_answer, @function\n"
        "global_asm_answer:\n"
        "movl $42, %eax\n"
        "ret\n");
#elif defined(__aarch64__) || defined(_M_ARM64)
__asm__(".text\n"
        ".globl global_asm_answer\n"
        ".type global_asm_answer, %function\n"
        "global_asm_answer:\n"
        "mov w0, #42\n"
        "ret\n");
#endif

static int compiler_barrier(int value)
{
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("" : "+r"(value) : : "memory");
    __asm__ volatile("nop");
    __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile("" : : : "memory");
    __asm__ volatile("nop");
    __asm__ volatile("yield");
#endif
    return value;
}

struct LegacyAsmPair
{
    int left;
    int right;
};

static void legacy_nonmatching_float(float value)
{
    float result;
    __asm__ volatile("" : "=r"(result) : "r"(value));
    (void)result;
}

static void legacy_nonmatching_pair(struct LegacyAsmPair value)
{
    struct LegacyAsmPair result;
    __asm__ volatile("" : "=r"(result) : "r"(value));
    (void)result;
}

#if defined(__x86_64__) || defined(_M_X64)
struct AsmWidthProbe
{
    unsigned char byte;
    unsigned char byte_sentinel;
    unsigned short half;
    unsigned char half_sentinel;
    unsigned int word;
    unsigned char word_sentinel;
    unsigned long long wide;
    unsigned char wide_sentinel;
};

static struct AsmWidthProbe asm_global_widths = {
    0x11, 0x7f, 0x2233, 0x6e, 0x44556677, 0x5d, 0x8899aabbccddeeffULL, 0x4c,
};
static unsigned char asm_global_array8[2] = {0x31, 0x7d};
static unsigned short asm_global_array16[2] = {0x4243, 0x7c};
static unsigned int asm_global_array32[2] = {0x51525354, 0x7b};
static unsigned long long asm_global_array64[2] = {0x6162636465666768ULL, 0x7a};

static int two_generic_read_write_outputs(void)
{
    int a = 1;
    int b = 2;
    __asm__ volatile("" : "+r"(a), "+r"(b));
    return a * 10 + b;
}

static int generic_before_fixed_read_write_output(void)
{
    int generic = 1;
    int fixed = 2;
    __asm__ volatile("" : "+r"(generic), "+a"(fixed));
    return generic * 10 + fixed;
}

static int fixed_before_generic_read_write_output(void)
{
    int generic = 1;
    int fixed = 2;
    __asm__ volatile("" : "+a"(fixed), "+r"(generic));
    return generic * 10 + fixed;
}

static void outputs_only_fixed_order(void)
{
    int generic;
    int fixed;
    __asm__ volatile("" : "=r"(generic), "=a"(fixed));
    (void)generic;
    (void)fixed;
}

#if defined(__x86_64__) || defined(_M_X64)
static int fixed_tied_output(int value)
{
    int result;
    __asm__ volatile("" : "=b"(result) : "0"(value));
    return result;
}
#endif

static int dynamic_stack_fixed_b(int count)
{
    int values[count + 1];
    values[0] = count + 4;
    __asm__ volatile("" : : "b"(values[0]));
    return values[0];
}

static int exact_width_asm_outputs(void)
{
    struct AsmWidthProbe local = {
        0x12, 0x7f, 0x2334, 0x6e, 0x55667788, 0x5d, 0x99aabbccddeeff00ULL, 0x4c,
    };
    unsigned char local_array8[2] = {0x32, 0x79};
    unsigned short local_array16[2] = {0x4344, 0x78};
    unsigned int local_array32[2] = {0x62636465, 0x77};
    unsigned long long local_array64[2] = {0x7273747576777879ULL, 0x76};
    unsigned char* pointer8 = &local.byte;
    unsigned short* pointer16 = &local.half;
    unsigned int* pointer32 = &local.word;
    unsigned long long* pointer64 = &local.wide;

    __asm__ volatile("" : "+r"(local.byte));
    __asm__ volatile("" : "+r"(local.half));
    __asm__ volatile("" : "+r"(local.word));
    __asm__ volatile("" : "+r"(local.wide));
    __asm__ volatile("" : "+r"(local_array8[0]));
    __asm__ volatile("" : "+r"(local_array16[0]));
    __asm__ volatile("" : "+r"(local_array32[0]));
    __asm__ volatile("" : "+r"(local_array64[0]));
    __asm__ volatile("" : "+r"(*pointer8));
    __asm__ volatile("" : "+r"(*pointer16));
    __asm__ volatile("" : "+r"(*pointer32));
    __asm__ volatile("" : "+r"(*pointer64));
    __asm__ volatile("" : "+r"(asm_global_widths.byte));
    __asm__ volatile("" : "+r"(asm_global_widths.half));
    __asm__ volatile("" : "+r"(asm_global_widths.word));
    __asm__ volatile("" : "+r"(asm_global_widths.wide));
    __asm__ volatile("" : "+r"(asm_global_array8[0]));
    __asm__ volatile("" : "+r"(asm_global_array16[0]));
    __asm__ volatile("" : "+r"(asm_global_array32[0]));
    __asm__ volatile("" : "+r"(asm_global_array64[0]));

    return local.byte == 0x12 && local.byte_sentinel == 0x7f && local.half == 0x2334 && local.half_sentinel == 0x6e &&
           local.word == 0x55667788 && local.word_sentinel == 0x5d && local.wide == 0x99aabbccddeeff00ULL && local.wide_sentinel == 0x4c &&
           local_array8[0] == 0x32 && local_array8[1] == 0x79 && local_array16[0] == 0x4344 && local_array16[1] == 0x78 &&
           local_array32[0] == 0x62636465 && local_array32[1] == 0x77 && local_array64[0] == 0x7273747576777879ULL && local_array64[1] == 0x76 &&
           asm_global_widths.byte == 0x11 && asm_global_widths.byte_sentinel == 0x7f && asm_global_widths.half == 0x2233 &&
           asm_global_widths.half_sentinel == 0x6e && asm_global_widths.word == 0x44556677 && asm_global_widths.word_sentinel == 0x5d &&
           asm_global_widths.wide == 0x8899aabbccddeeffULL && asm_global_widths.wide_sentinel == 0x4c && asm_global_array8[0] == 0x31 &&
           asm_global_array8[1] == 0x7d && asm_global_array16[0] == 0x4243 && asm_global_array16[1] == 0x7c && asm_global_array32[0] == 0x51525354 &&
           asm_global_array32[1] == 0x7b && asm_global_array64[0] == 0x6162636465666768ULL && asm_global_array64[1] == 0x7a;
}
#endif

static int numeric_tied_output(int value)
{
    int result;
    __asm__ volatile("" : "=r"(result) : "0"(value));
    return result;
}

static int named_tied_output(int value)
{
    int result;
    __asm__ volatile("" : [dst] "=r"(result) : "[dst]"(value));
    return result;
}

static int four_tied_output(int a, int b, int c, int d)
{
    int result0;
    int result1;
    int result2;
    int result3;
    __asm__ volatile("" : "=r"(result0), "=r"(result1), "=r"(result2), "=r"(result3) : "0"(a), "1"(b), "2"(c), "3"(d), "r"(a), "r"(b));
    return result0 + result1 + result2 + result3;
}

int main(void)
{
    int valid = compiler_barrier(37) == 37 && global_asm_answer() == 42 && numeric_tied_output(53) == 53 && named_tied_output(71) == 71 &&
                four_tied_output(1, 2, 3, 4) == 10;
#if defined(__x86_64__) || defined(_M_X64)
    outputs_only_fixed_order();
    valid = valid && fixed_tied_output(89) == 89 && two_generic_read_write_outputs() == 12 && generic_before_fixed_read_write_output() == 12 &&
            fixed_before_generic_read_write_output() == 12 && dynamic_stack_fixed_b(3) == 7 && exact_width_asm_outputs();
#endif
    return valid ? 0 : 1;
}
