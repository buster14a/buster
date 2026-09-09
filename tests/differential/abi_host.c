#include "abi.h"
#include <stdio.h>
long long host_ints(long long a, long long b, long long c, long long d, long long e, long long f, long long g, long long h)
{
    return a+2*b+3*c+4*d+5*e+6*f+7*g+8*h;
}
struct Pair host_pair(struct Pair p) { p.a += 3; p.b += 1.25; return p; }
int main(void)
{
    struct Pair p = {0x1234567812345678ULL, 1.5};
    struct Pair q = pair(p, 7);
    struct Large l = {3, 19, 29};
    struct Large m = large(l, 5);
    int bad = 0;
    bad |= many_ints(-7, 201, -301, 501, -701, 901, -1101, 1301) != 6098;
    bad |= many_floats(1,2,3,4,5,6,7,8,9,10) != 385;
    bad |= q.a != p.a + 7 || q.b != 3;
    bad |= m.a != 8 || m.b != 22 || m.c != 24;
    bad |= call_host();
    printf("abi=%d\n", bad);
    return bad;
}
