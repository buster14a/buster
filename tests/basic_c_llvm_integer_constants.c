// The minimum integer bit patterns and i1 true must survive bitcode encoding.
volatile signed char minimum_i8 = -128;
volatile short minimum_i16 = -32768;
volatile int minimum_i32 = -2147483647 - 1;
volatile long long minimum_i64 = -9223372036854775807LL - 1;
volatile unsigned int high_u32 = 0x80000000U;

int logical_not(int value)
{
    return !value;
}

_Bool boolean_true(void)
{
    return 1;
}

int main(void)
{
    int result = 0;
    if (logical_not(0) != 1 || logical_not(7) != 0 || logical_not(-7) != 0 || !boolean_true())
        result = 1;
    if (minimum_i8 != -128 || minimum_i16 != -32768 || minimum_i32 != -2147483647 - 1)
        result = 2;
    if (minimum_i64 != -9223372036854775807LL - 1 || high_u32 != 0x80000000U)
        result = 3;
    return result;
}
