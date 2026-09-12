static int identity_read_write(int value)
{
    __asm__ volatile("" : "+r"(value) : : "memory", "cc");
    return value;
}

static int identity_numeric(int value)
{
    int result;
    __asm__ volatile("" : "=r"(result) : "0"(value));
    return result + value;
}

static int identity_named(int value)
{
    int result;
    __asm__ volatile("" : [destination] "=r"(result) : "[destination]"(value));
    return result;
}

static int identity_swap(int left, int right)
{
    __asm__ volatile("" : "=r"(left), "=r"(right) : "0"(right), "1"(left) : "memory");
    return left * 100 + right;
}

static int identity_many(int a, int b, int c, int d)
{
    int x, y, z, w;
    __asm__ volatile("" : "=r"(x), "=r"(y), "=r"(z), "=r"(w) : "0"(d), "1"(c), "2"(b), "3"(a), "r"(a), "r"(b));
    return x * 1000 + y * 100 + z * 10 + w;
}

static int identity_input(volatile int* value)
{
    __asm__ volatile("" : : "r"((*value)++));
    return *value;
}

static int* identity_pointer(int* value)
{
    int* result;
    __asm__ volatile("" : "=r"(result) : "0"(value));
    return result;
}

typedef struct Widths Widths;
struct Widths
{
    unsigned char byte, byte_guard;
    unsigned short half, half_guard;
    unsigned int word, word_guard;
    unsigned long long wide, wide_guard;
};

static Widths widths = {0xa5, 0x39, 0xb678, 0x4938, 0xc2345678, 0x55667788, 0xdef0123456789abcULL, 0x1020304050607080ULL};

static int identity_widths(void)
{
    unsigned char* byte = &widths.byte;
    __asm__ volatile("" : "+r"(*byte), "+r"(widths.half), "+r"(widths.word), "+r"(widths.wide));
    return widths.byte == 0xa5 && widths.byte_guard == 0x39 && widths.half == 0xb678 && widths.half_guard == 0x4938 &&
           widths.word == 0xc2345678 && widths.word_guard == 0x55667788 && widths.wide == 0xdef0123456789abcULL &&
           widths.wide_guard == 0x1020304050607080ULL;
}

int main(void)
{
    volatile int input = 7;
    int pointed = 53;
    return identity_read_write(-123) != -123 || identity_numeric(37) != 74 || identity_named(81) != 81 ||
           identity_swap(3, 7) != 703 || identity_many(1, 2, 3, 4) != 4321 || identity_input(&input) != 8 ||
           identity_pointer(&pointed) != &pointed || !identity_widths();
}
