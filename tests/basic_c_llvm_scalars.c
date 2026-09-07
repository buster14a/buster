/* Independent LLVM consumers must preserve narrow sign-bit constants and i1. */
unsigned char high8 = 128;
unsigned short high16 = 32768;
unsigned high32 = 2147483648U;
unsigned long long high64 = 9223372036854775808ULL;
_Bool initialized_true = 1;
int logical_not(int value) { return !value; }
unsigned literal32(void) { return 2147483648U; }
int main(void)
{
    int failed = high8 != 128 || high16 != 32768 || high32 != 2147483648U;
    failed |= high64 != 9223372036854775808ULL || initialized_true != 1;
    failed |= literal32() != 2147483648U;
    failed |= logical_not(0) != 1 || logical_not(1) != 0 || logical_not(-1) != 0;
    return failed;
}
