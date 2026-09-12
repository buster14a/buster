unsigned long long fast_scalar(unsigned long long x, unsigned long long y)
{
    unsigned long long a = 3;
    unsigned long long b = 4;
    unsigned long long z = (x + 0) * 1;
    unsigned long long unused = y * 9;
    (void)unused;
    if (y) z = z ^ 0;
    return z + (a + b);
}
