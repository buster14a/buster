int f(int i, unsigned long ul, long l, unsigned u, _Bool b)
{
    int r = 0;
    r += i == 4000000000; r += ul == -1; r += l < 5; r += u > 4294967296; r += (i < l) == 3;
    r += i < 5L; r += !i == 2; r += (i || l) > 2; r += b == 3; r += (unsigned char)i < 256;
    r += 2 < i; r += i == 1; r += l != 1L; r += u == 7u; r += -1 == ul; r += (long)i > 4294967296L;
    return r;
}
