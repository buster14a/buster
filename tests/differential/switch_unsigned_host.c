#include <stdio.h>
unsigned long long switch_u32(unsigned long long);
unsigned long long switch_u64(unsigned long long);
int main(void)
{
    unsigned long long inputs[] = {0, 0x7fffffff, 0x80000000, 0xb9000000, 0xf9400000, 0xffffffff,
        0xffffffff80000000ull, 0xffffffffffffffffull, 0x180000000ull, 0x100000000ull};
    unsigned expected32[] = {0, 1, 2, 3, 4, 5, 2, 5, 2, 0};
    unsigned expected64[] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 0};
    int result = 0;
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i += 1)
    {
        unsigned long long a = switch_u32(inputs[i]);
        unsigned long long b = switch_u64(inputs[i]);
        printf("%llx: %llu %llu\n", inputs[i], a, b);
        result |= a != expected32[i] || b != expected64[i];
    }
    return result;
}
