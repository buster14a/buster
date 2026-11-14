// Conversions that fold into the constant's CONST (#2678): to 64-bit integers and pointers (a negative int's sign
// extended), to an unsigned int, to narrow integers, to _Bool, and to float and double where exact (a negative value's,
// 2^31 unsigned's and 2^63 unsigned long's too); as operands of the arithmetic, the comparisons and pointer arithmetic, a
// subscript, a call's arguments, returns, initializers, assignments and compound assignments, and in a statement
// expression's body, whose function tracks its stack.
typedef unsigned long U;

U add1(U i) { return i + 1; }
U sub1(U i) { return i - 1; }
U mask(U i) { return i & -8; }
U mul8(U i) { return i * 8; }
U idx(U i) { return i / 8 * 8 + i % 8; }
long neg(long x) { return x + -1; }
unsigned uadd(unsigned u) { return u + -1; }
long cmp(long x) { return (x == 0) + (x < 5) + (x > -3) + (x != 7) + (x <= -2147483647 - 1); }
double dadd(double d) { return d + 1; }
double dmul(double d) { return d * -2; }
float fmul(float f) { return f * 2; }
float fbig(float f) { return f + 16777216; }
double dbig(double d) { return d + 9007199254740992l; }
double dsub(double d) { return d - 2147483648u; }
double dtop(double d) { return d + 9223372036854775808ul; }
_Bool b5(void) { return 5; }
_Bool b0(void) { return 0; }
_Bool bset(int x) { _Bool b = 2; if (x) b = 0; return b; }
short sh(short s) { short t = 7; return (short)(s + t); }
unsigned char uc(void) { unsigned char c = 255; return c; }
int p1(int* p) { return *(p + 1) + *(p - 2) + (p == 0) + (p != 0); }
int* pz(void) { return 0; }
long arr[8];
long at(void) { return arr[2] + arr[7]; }
long take(long a, double b, unsigned char c, U d) { return a + (long)b + c + (long)d; }
long call(void) { return take(1, 2, 3, 4); }
long r7(void) { return 7; }
double r2(void) { return 2; }
float rf(void) { return -3; }
long init(void) { long a = 5; double d = 3; U u = -1; return a + (long)d + (long)(u >> 60); }
U acc(U x) { x += 1; x *= 3; x <<= 2; x -= 5; x |= 16; return x; }
double dacc(double d) { d *= 2; d += 1; d -= -4; return d; }
U se(U x) { return ({ U t = x + 1; t * 2; }) + 3; }

static unsigned h;
static void see(U v) { h = h * 31u + (unsigned)v + (unsigned)(v >> 32); }

int tested(int n)
{
    h = 0;
    U u = (U)(long)n * 0x9e3779b97f4a7c15ul;
    long l = (long)n - 7;
    double d = (double)n / 4;
    int a[4] = { n, n + 1, n + 2, n + 3 };
    for (int i = 0; i < 8; i++)
        arr[i] = n * i;
    see(add1(u)); see(sub1(u)); see(mask(u)); see(mul8(u)); see(idx(u)); see((U)neg(l)); see(uadd((unsigned)n));
    see((U)cmp(l)); see((U)cmp(0)); see((U)cmp(-3));
    see((U)(long)dadd(d)); see((U)(long)dmul(d)); see((U)(long)fmul((float)n)); see((U)(long)fbig((float)n));
    see((U)(long)dbig(d)); see((U)(long)dsub(d)); see((U)(long)(dtop(d) / 65536.0));
    see(b5()); see(b0()); see(bset(n & 1)); see((U)sh((short)n)); see(uc());
    see((U)p1(a + 2)); see(pz() == 0); see((U)at()); see((U)call());
    see((U)r7()); see((U)(long)r2()); see((U)(long)rf()); see((U)init()); see(acc(u)); see((U)(long)dacc(d)); see(se(u));
    return (int)(h & 0x7fffffff);
}
