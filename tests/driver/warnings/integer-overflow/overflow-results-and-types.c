typedef int T;
typedef long L;
int x = 0x7fffffff * 2;
long y = 0x7fffffffffffffffL * 2;
__int128 z = (__int128)0x7fffffffffffffffL * 0x7fffffffffffffffL * 4;
T a = (T)0x7fffffff * 2;
int b = (L)0x7fffffffffffffffL + 1 > 0;
int e = -(-2147483647 - 1);
int h = -2147483647 - 1 - 1;
unsigned long long q = 999999999LL * 999999999LL * 1000;
int f(int v) { int k = 2147483647 * 3 + v; return (1 << 31) + (3 << 30) + k; }
