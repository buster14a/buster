// Compile with every -fcanonical-fast pass selection and allocator mode.
// Keep execution independent of IR row-count assertions in ir_fast_test.c.
volatile int observed;
static int effect(int value)
{
    observed += 1;
    return value;
}
static unsigned identity(unsigned x)
{
    unsigned a = x + 0u;
    unsigned b = a * 1u;
    unsigned c = b ^ 0u;
    return c & ~0u;
}
static unsigned constants(void)
{
    unsigned a = 0xffffffffu;
    unsigned b = 3u;
    return (a + b) * 7u + ((a >> 29) ^ (b << 4));
}
static int parameter(int condition)
{
    int x = 17;
    if (condition) x = x + 0;
    else x = x * 1;
    return x;
}
static int loop(int limit)
{
    int sum = 0;
    for (int i = 0; i < limit; i += 1) sum = (sum + i) ^ 0;
    return sum;
}
static int address(int* pointer)
{
    int* identity_pointer = &*pointer;
    *identity_pointer += 0;
    return *identity_pointer;
}
int main(void)
{
    int result = 0;
    if (identity(0xffffffffu) != 0xffffffffu) result |= 1;
    if (constants() != 69u) result |= 2;
    if (parameter(0) != 17 || parameter(1) != 17) result |= 4;
    if (loop(10) != 45) result |= 8;
    int value = 23;
    if (address(&value) != 23) result |= 16;
    (void)(effect(8) + 0);
    if (observed != 1) result |= 32;
    int negative = -17;
    if ((negative >> 2) != -5) result |= 64;
    unsigned narrow = (unsigned char)511u;
    if (narrow != 255u) result |= 128;
    return result;
}
