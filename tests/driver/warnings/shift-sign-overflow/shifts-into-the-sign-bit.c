typedef int I;
int a = 1 << 31;
enum { E = 1 << 31 };
int b[(1 << 30 << 1) < 0 ? 1 : 2];
int f(int x)
{
    long c = 1L << 63;
    int d = 2 << 30, e = (I)3 << 30, g = (short)1 << 31, h = (char)64 << 25;
    unsigned u = 1u << 31;
    int v = 3 << 29, w = -1 << 3, y = 1 << 32, z = 0 << 31;
    unsigned long s = sizeof(1 << 31);
    __int128 q = (__int128)1 << 127;
    if (x)
        return 1 << 31;
    return (int)(x + c + d + e + g + h + u + v + w + y + z + s + q) + (1 << x);
}
