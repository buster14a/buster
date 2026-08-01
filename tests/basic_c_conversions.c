static double signed_to_double(int value)
{
    return value;
}

static double unsigned_to_double(unsigned int value)
{
    return value;
}

static double unsigned_long_long_to_double(unsigned long long value)
{
    return value;
}

static int double_to_signed(double value)
{
    return (int)value;
}

static unsigned int double_to_unsigned(double value)
{
    return (unsigned int)value;
}

static unsigned long long double_to_unsigned_long_long(double value)
{
    return (unsigned long long)value;
}

static double float_to_double(float value)
{
    return value;
}

static float double_to_float(double value)
{
    return (float)value;
}

int main(void)
{
    signed char small_negative = -3;
    unsigned char small_positive = 250;
    int negative = -7;
    if ((long)3 != 3)
    {
        return 1;
    }
    if ((int)(long)4 != 4)
    {
        return 2;
    }
    if ((long)negative != -7)
    {
        return 3;
    }
    if ((long)small_negative != -3)
    {
        return 4;
    }
    if ((unsigned long)small_positive != 250)
    {
        return 5;
    }
    if (signed_to_double(-7) != -7.0)
    {
        return 6;
    }
    if (unsigned_to_double(4000000000U) != 4000000000.0)
    {
        return 7;
    }
    if (double_to_signed(-8.75) != -8)
    {
        return 8;
    }
    if (double_to_unsigned(4000000000.0) != 4000000000U)
    {
        return 9;
    }
    if (float_to_double(1.5f) != 1.5)
    {
        return 10;
    }
    if (double_to_float(2.25) != 2.25f)
    {
        return 11;
    }
    if (0xffffffff != 4294967295U)
    {
        return 12;
    }
    if (0xffffffffffffffffULL != 18446744073709551615ULL)
    {
        return 13;
    }
    if (unsigned_long_long_to_double(9223372036854775808ULL) != 0x1p63)
    {
        return 14;
    }
    if (double_to_unsigned_long_long(0x1.0000000000001p63) != 9223372036854777856ULL)
    {
        return 15;
    }
    return 0;
}
