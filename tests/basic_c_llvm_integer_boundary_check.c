// Independent oracle: this file must be compiled by Clang, never by Buster
// in the bitcode regression. No headers or platform-specific ABI are needed.
extern _Bool llvm_true;
extern unsigned char llvm_u8_min;
extern unsigned short llvm_u16_min;
extern unsigned int llvm_u32_min;
extern unsigned long long llvm_u64_min;
extern unsigned char llvm_u8[6];
extern unsigned short llvm_u16[6];
extern unsigned int llvm_u32[6];
extern unsigned long long llvm_u64[6];
extern int llvm_logical_not(int value);
extern int llvm_pointer_not(void* value);
extern long long llvm_signed_min(int width);
extern unsigned long long llvm_wide_low(void);
extern unsigned long long llvm_wide_high(void);

int main(void)
{
    unsigned long long expected_u8[] = {0, 1, 127, 128, 129, 255};
    unsigned long long expected_u16[] = {0, 1, 32767, 32768, 32769, 65535};
    unsigned long long expected_u32[] = {0, 1, 2147483647U, 2147483648U, 2147483649U, 4294967295U};
    unsigned long long expected_u64[] = {0, 1, 9223372036854775807ULL, 9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
    int failures = 0;
    failures += llvm_true != 1;
    failures += llvm_u8_min != 128;
    failures += llvm_u16_min != 32768;
    failures += llvm_u32_min != 2147483648U;
    failures += llvm_u64_min != 9223372036854775808ULL;
    for (int index = 0; index < 6; index += 1)
    {
        failures += llvm_u8[index] != expected_u8[index];
        failures += llvm_u16[index] != expected_u16[index];
        failures += llvm_u32[index] != expected_u32[index];
        failures += llvm_u64[index] != expected_u64[index];
    }
    failures += llvm_logical_not(0) != 1;
    failures += llvm_logical_not(1) != 0;
    failures += llvm_logical_not(-1) != 0;
    failures += llvm_logical_not(-2147483647 - 1) != 0;
    failures += llvm_pointer_not((void*)0) != 1;
    failures += llvm_pointer_not(&failures) != 0;
    failures += llvm_signed_min(8) != -128;
    failures += llvm_signed_min(16) != -32768;
    failures += llvm_signed_min(32) != (-2147483647 - 1);
    failures += llvm_signed_min(64) != (-9223372036854775807LL - 1);
    failures += llvm_wide_low() != 9223372036854775808ULL;
    failures += llvm_wide_high() != 18446744073709551615ULL;
    return failures;
}
