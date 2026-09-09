#include "abi.h"
long long many_ints(signed char a, unsigned char b, short c, unsigned short d, int e, unsigned f, long long g, long long h)
{
    return a + 2LL*b + 3LL*c + 4LL*d + 5LL*e + 6LL*f + 7LL*g + 8LL*h;
}
double many_floats(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j)
{
    return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h + 9*i + 10*j;
}
struct Pair pair(struct Pair p, int x)
{
    p.a += (unsigned)x;
    p.b *= 2;
    return p;
}
struct Large large(struct Large p, unsigned long long x)
{
    p.a += x; p.b ^= x; p.c -= x;
    return p;
}
int call_host(void)
{
    struct Pair p = {17, 2.5};
    struct Pair q = host_pair(p);
    return host_ints(-1,2,-3,4,-5,6,-7,8) != 36 || q.a != 20 || q.b != 3.75;
}
