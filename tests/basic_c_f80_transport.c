typedef unsigned char u8;

static long double identity(long double value)
{
    return value;
}

static long double assign(long double value)
{
    volatile long double local = 0.0L;
    local = value;
    return local;
}

static int is_zero_with_sign(long double value, u8 sign)
{
    u8 const* bytes = (u8 const*)&value;
    for (unsigned index = 0; index < 9; index += 1)
    {
        if (bytes[index] != 0)
        {
            return 0;
        }
    }
    return bytes[9] == sign;
}

int main(void)
{
    long double positive = identity(+0.0L);
    long double negative = assign(-0.0L);
    return !is_zero_with_sign(positive, 0) || !is_zero_with_sign(negative, 0x80);
}
