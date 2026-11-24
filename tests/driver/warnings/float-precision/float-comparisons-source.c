typedef float F;
static int a1[(0.1 == 0.1f) + 1];
enum E { X = 0.1 != 0.1f, Y = (int)(0.1L == (double)0.1), Z = 0.1 == (double)0.1f };
_Static_assert(0.1 != 0.1f, "");
struct S { unsigned b : (0.1 == 0.1f) + 1; };
static int g = sizeof(0.1 == 0.1f);
static int h = 0.1f == 0.1 ? 1 : 2;
static __typeof__(0.1 == 0.1f) t;
int e = 0.1L != 0.1;
int k = 0.1q == 0.1;
#define EQ(a, b) ((a) == (b))
#define TENTH 0.1
double g2(void);
float g1(void);
int f(int i, float x, F y, double d, long double l, __float128 q, _Float16 h16, __bf16 b16, float* p, struct S s)
{
    int r = a1[0] + g + h + t + e + k + (int)s.b;
    switch (i) { case (0.1 == 0.1f): return 1; }
    if (0) return x == 0.1;
    r += (int)sizeof(char[(0.1 == 0.1f) + 1]) + (int)sizeof(x == 0.1) + (1 || x == 0.1) + _Generic(x == 0.1, int: 0);
    r += x == 0.1;
    r += 0.1 != x;
    r += y == 0.1;
    r += *p == 0.1;
    r += p[1] != 1e-50;
    r += (x) == (0.1);
    r += (double)x == 0.1;
    r += (double)(float)d == 0.1;
    r += (float)d == 0.1;
    r += g1() == 0.1;
    r += d == 0.1L;
    r += l == 0.1q;
    r += q == 0.1;
    r += 0.1 == h16;
    r += 0.1 == b16;
    r += h16 == 0.5;
    r += x == 1e300;
    r += x != 0x1p-150;
    r += EQ(x, 0.1);
    r += x == TENTH;
    r += _Generic(0, int: 0.1) == x;
    r += x == 0.5;
    r += x == -0.1;
    r += x == 0.1f;
    r += x < 0.1;
    r += i == 0.1;
    r += 0.1 == 0.2;
    r += d == 0.1;
    r += g2() == 0.1;
    r += (0.1, x) == 0.1;
    return r;
}
