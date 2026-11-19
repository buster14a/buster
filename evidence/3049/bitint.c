typedef unsigned _BitInt(8) U8;
typedef unsigned _BitInt(16) U16;

U8 narrow8(const U8 *a, unsigned n, U8 s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        U8 x = a[i];
        s = (x & (U8)2) ? (s & x) : s;
    }
    return s;
}

U16 narrow16(const U16 *a, unsigned n, U16 s)
{
    for (unsigned i = 0; i < n; ++i)
    {
        U16 x = a[i];
        s = (x & (U16)2) ? s : (x & s);
    }
    return s;
}
