int a(int x, int b, int c) { return x << b + c; }
int b(int x, int b, int c) { return x + b << c; }
int c(int x, int b, int c) { return x >> b - c; }
int d(int x, int b, int c) { return x << (b + c) | (x + b) << c | x << b * c; }
int e(int x, int b, int c) { return x - b >> c + 1; }
#define SH(a, b) (a << b)
int f(int x, int b, int c) { return SH(x, b + c); }
#define ID(a) a
int g(int x) { return ID(x << x + 1) + (ID(x) << x + 1); }
int h(int x, int b) { x <<= b + 1; return x << b + 1 << 2; }
