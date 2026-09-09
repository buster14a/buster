unsigned long long xcr_live(unsigned index, unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f, unsigned long long g)
{
    unsigned low, high;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(index));
    return ((unsigned long long)low | ((unsigned long long)high << 32)) + a + 3*b + 5*c + 7*d + 11*e + 13*f + 17*g;
}
