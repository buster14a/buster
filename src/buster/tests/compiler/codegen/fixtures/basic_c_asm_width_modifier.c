// Nonprivileged runtime checks accompany compile-only port I/O shapes. The
// out/in functions are never called: these are privileged instructions.
typedef unsigned long long WidthWide;

WidthWide width_numeric(WidthWide value)
{
    __asm__ volatile("movw $0xabcd, %w0" : "+r"(value));
    return value;
}

WidthWide width_named(WidthWide value)
{
    __asm__ volatile("movw $0x1357, %w[value]" : [value] "+r"(value));
    return value;
}

WidthWide width_r10(WidthWide value)
{
    register WidthWide bound __asm__("r10") = value;
    __asm__ volatile("movw $0x2468, %w0" : "+r"(bound));
    return bound;
}

WidthWide width_tied(WidthWide value)
{
    WidthWide result;
    __asm__ volatile("movw %w1, %w0" : "=r"(result) : "0"(value));
    return result;
}

WidthWide width_dialect(WidthWide input, WidthWide source_value)
{
    register WidthWide bound __asm__("r10") = input;
    register WidthWide source __asm__("r8") = source_value;
    __asm__ volatile("{movw %w1, %w0|mov %w0, %w1}" : "+r"(bound) : "r"(source));
    return bound;
}

void* width_pointer(void* value)
{
    __asm__ volatile("movw %w0, %w0" : "+r"(value));
    return value;
}

struct WidthGuard
{
    unsigned char value;
    unsigned char guard;
};

int width_narrow_guard(void)
{
    volatile struct WidthGuard pair = {1, 0x5a};
    __asm__ volatile("movw $0xabcd, %w0" : "+r"(pair.value));
    return pair.value == 0xcd && pair.guard == 0x5a ? 0 : 1;
}

// Source-linked Linux port-I/O shapes. Keep them compile-only.
void width_outw(unsigned short value, unsigned int port)
{
    __asm__ volatile("outw %w0, %w1" : : "a"(value), "Nd"(port));
}

unsigned short width_inw(unsigned int port)
{
    unsigned short value;
    __asm__ volatile("inw %w1, %w0" : "=a"(value) : "Nd"(port));
    return value;
}

void width_outl(unsigned int value, unsigned int port)
{
    __asm__ volatile("outl %0, %w1" : : "a"(value), "Nd"(port));
}

unsigned int width_inl(unsigned int port)
{
    unsigned int value;
    __asm__ volatile("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

int main(void)
{
    WidthWide input = 0x1122334455667788ULL;
    int status = 0;
    int anchor = 0;
    if (width_numeric(input) != 0x112233445566abcdULL) status = 1;
    if (!status && width_named(input) != 0x1122334455661357ULL) status = 2;
    if (!status && width_r10(input) != 0x1122334455662468ULL) status = 3;
    if (!status && width_tied(input) != input) status = 4;
    if (!status && width_dialect(input, 0xaabbccddeeffbeefULL) != 0x112233445566beefULL) status = 5;
    if (!status && width_pointer(&anchor) != &anchor) status = 6;
    if (!status && width_narrow_guard()) status = 7;
    return status;
}
