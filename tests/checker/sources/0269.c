unsigned long long f(unsigned char a, unsigned short b, unsigned c, unsigned long long d, long x)
{
    __builtin_assume(x > 0);
    return __builtin_rotateleft8(a, 1) + __builtin_rotateright8(a, 9) + __builtin_rotateleft16(b, 3) + __builtin_rotateright16(b, 3) +
           __builtin_rotateleft32(c, 7) + __builtin_rotateright32(c, 7) + __builtin_rotateleft64(d, 9) + __builtin_rotateright64(d, 9) +
           (__builtin_unpredictable(x > 2) ? 1 : 0);
}
