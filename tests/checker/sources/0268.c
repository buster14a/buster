int f(__int128 a, unsigned __int128 b, long c)
{
    __int128 r = 0;
    unsigned __int128 u = 0;
    return __builtin_mul_overflow(a, b, &r) + __builtin_add_overflow(c, a, &u) + __builtin_sub_overflow(b, c, &r);
}
