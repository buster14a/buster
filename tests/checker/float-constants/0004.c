static __float128 a = 0;
static __float128 b = 1.5;
static __float128 c = 1.5q;
static __float128 d = 0.1;
static __float128 e = 1.5L;
static __float128 f = 18446744073709551615ULL;
static __float128 g = (__int128)1 << 100;
int tested(void){return (int)(a+b+c+d+e+(f!=0)+(g!=0));}
