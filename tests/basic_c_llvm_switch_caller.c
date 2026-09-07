#include <stdio.h>

extern int llvm_switch_dense(int value);
extern int llvm_switch_grouped(int value);
extern int llvm_switch_no_default(int value);
extern int llvm_switch_default_only(int value);
extern int llvm_switch_nested(int outer, int inner);
extern int llvm_switch_wide_unsigned(unsigned long long value);
extern int llvm_switch_wide_signed(long long value);

int main(void)
{
    unsigned int checks = 0;
    unsigned int failures = 0;
    for (int value = -8; value <= 8; value += 1)
    {
        int dense = value == 0 ? 11 : value == 1 ? 23 : value == 2 ? 37 : 53;
        int grouped = value == 0 || value == 1 ? 15 : value == 2 ? 11 : value == 4 ? 47 : 31;
        int no_default = value == -7 ? 19 : value == 5 ? 23 : 17;
        failures += llvm_switch_dense(value) != dense;
        failures += llvm_switch_grouped(value) != grouped;
        failures += llvm_switch_no_default(value) != no_default;
        failures += llvm_switch_default_only(value) != 59;
        checks += 4;
        for (int inner = -4; inner <= 5; inner += 1)
        {
            int nested = value == 1 ? (inner == -3 ? 61 : inner == 4 ? 67 : 71) : value == -2 ? 73 : 79;
            failures += llvm_switch_nested(value, inner) != nested;
            checks += 1;
        }
    }
    unsigned long long unsigned_keys[] = {0, 7, 0x10000000006ULL, 0x10000000007ULL, 0x10000000008ULL,
                                          0x20000000010ULL, 0x20000000011ULL, 0x20000000012ULL, ~0ULL};
    for (unsigned int index = 0; index < sizeof(unsigned_keys) / sizeof(unsigned_keys[0]); index += 1)
    {
        unsigned long long value = unsigned_keys[index];
        int expected = value == 0x10000000007ULL ? 83 : value == 0x20000000011ULL ? 89 : 97;
        failures += llvm_switch_wide_unsigned(value) != expected;
        checks += 1;
    }
    long long signed_keys[] = {0, -7, -0x10000000006LL, -0x10000000007LL, -0x10000000008LL,
                               0x20000000010LL, 0x20000000011LL, 0x20000000012LL};
    for (unsigned int index = 0; index < sizeof(signed_keys) / sizeof(signed_keys[0]); index += 1)
    {
        long long value = signed_keys[index];
        int expected = value == -0x10000000007LL ? 101 : value == 0x20000000011LL ? 103 : 107;
        failures += llvm_switch_wide_signed(value) != expected;
        checks += 1;
    }
    printf("LLVM_SWITCH checks=%u failures=%u\n", checks, failures);
    return failures != 0;
}
