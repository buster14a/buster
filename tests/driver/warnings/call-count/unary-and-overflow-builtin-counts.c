void f(int *p, double d)
{
    __builtin_popcount();
    __builtin_popcount(1, 2);
    __builtin_ctz();
    __builtin_bswap32(1, __builtin_popcount(2));
    __builtin_add_overflow(1, 2);
    __builtin_add_overflow(1, 2, p, 4);
    __builtin_sadd_overflow(1, 2);
    __builtin_creal();
    __builtin_creal(d, d);
    __builtin_cimagf(1, 2, 3);
}
