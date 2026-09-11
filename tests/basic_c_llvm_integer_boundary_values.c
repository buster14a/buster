// Compile this translation unit with Buster and its checker with Clang. Keeping
// the oracle outside Buster prevents equal, corrupted constants from agreeing.
_Bool llvm_true = 1;
unsigned char llvm_u8_min = 128;
unsigned short llvm_u16_min = 32768;
unsigned int llvm_u32_min = 2147483648U;
unsigned long long llvm_u64_min = 9223372036854775808ULL;

unsigned char llvm_u8[] = {0, 1, 127, 128, 129, 255};
unsigned short llvm_u16[] = {0, 1, 32767, 32768, 32769, 65535};
unsigned int llvm_u32[] = {0, 1, 2147483647U, 2147483648U, 2147483649U, 4294967295U};
unsigned long long llvm_u64[] = {0, 1, 9223372036854775807ULL, 9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};

int llvm_logical_not(int value)
{
    return !value;
}

int llvm_pointer_not(void* value)
{
    return !value;
}

long long llvm_signed_min(int width)
{
    long long result = -128;
    if (width == 16)
    {
        result = -32768;
    }
    else if (width == 32)
    {
        result = -2147483647 - 1;
    }
    else if (width == 64)
    {
        result = -9223372036854775807LL - 1;
    }
    return result;
}

// WIDE_INTEGER records encode each 64-bit word independently. Its sign-bit
// word must retain LLVM's special encoding even though the IR type is wider.
unsigned long long llvm_wide_low(void)
{
    unsigned __int128 value = 9223372036854775808ULL;
    return (unsigned long long)value;
}

unsigned long long llvm_wide_high(void)
{
    __int128 value = -9223372036854775807LL - 1;
    return (unsigned long long)(value >> 64);
}
