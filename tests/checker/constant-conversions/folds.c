// Conversions that fold into the constant's CONST (#2678): to 64-bit integers and pointers (a negative int's sign
// extended), to an unsigned int, to narrow integers, to _Bool, and to float and double where exact (a negative value's,
// 2^31 unsigned's and 2^63 unsigned long's too); as operands of the arithmetic, the comparisons and pointer arithmetic, a
// subscript, a call's arguments, returns, initializers, assignments and compound assignments, and in a statement
// expression's body, whose function tracks its stack. A conditional's or a logical operator's value converted by its
// consumer converts in each arm instead where an arm is a constant and the target an integer, _Bool or an exact
// floating conversion (#2842): one constant arm or two, unsigned through the conditional's common type (c ? -1 : 2u),
// nested, as an operand, and with a condition known at compile time. Wider integer joins need both constants exact;
// logical arms are always 0 or 1.
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
long sel(int c, int x) { return c ? x : 1; }
long sel2(int c, int x) { return c ? 1 : x; }
long both(int c) { return c ? 1 : 2; }
U mix(int c) { return c ? -1 : 2u; }
double dsel(int c, int x) { return c ? x : 3; }
double dsel2(int c, unsigned x) { return c ? 3 : x; }
double dboth(int c) { return c ? 3 : 1; }
float fboth(int c) { return c ? 3 : 1; }
float fsign(int c) { return c ? -16777216 : 16777216; }
double dmix(int c) { return c ? -1 : 2u; }
double dwide(int c) { return c ? 9223372036854775808ul : 1ul; }
float fnarrow(int c) { return c ? (signed char)-1 : 1l; }
double dnarrow(int c) { return c ? (short)-32768 : 1l; }
double dnested(int c, int d, int x) { return c ? (d ? 1 : x) : 2; }
_Bool bsel(int c, int x) { return c ? x : 5; }
long nest(int c, int d, int x) { return c ? (d ? 1 : x) : 2; }
long addsel(int c, long y) { return y + (c ? 1 : -2); }
int psel(int* p, int c) { return *(p + (c ? 1 : -1)); }
long lg(int a, int b) { return a && b; }
long lo(int a, int b) { return a || !b; }
double dl(int a, int b) { return (a || b) + 0.5; }
float fl(int a, int b) { return (a && b) + 0.5f; }
_Bool bl(int a, int b) { return a && b; }
static int touched;
static int touch(void) { touched += 1; return touched; }
long decided_or(int x) { return (touch(), 1) || x; }
long decided_and(int x) { return (touch(), 0) && x; }
long known_arm(int x) { return (touch(), 1) ? 7 : x; }
float known_float(int x) { return (touch(), 1) ? 3 : x; }
double decided_double(int x) { return (touch(), 0) && x; }

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
    for (int k = 0; k < 4; k++)
    {
        int c = (n >> k) & 1;
        int e = (n >> (k + 1)) & 1;
        see((U)sel(c, n)); see((U)sel2(c, n)); see((U)both(c)); see(mix(c)); see(bsel(c, n & k));
        see((U)(long)(dsel(c, n) * 2)); see((U)(long)dsel2(c, (unsigned)n));
        see((U)(long)dboth(c)); see((U)(long)fboth(c)); see((U)(long)fsign(c)); see((U)dmix(c));
        see((U)(dwide(c) / 65536.0)); see((U)(long)dnested(c, e, n));
        see((U)(long)fnarrow(c)); see((U)(long)dnarrow(c));
        see((U)nest(c, e, n)); see((U)addsel(c, l)); see((U)psel(a + 2, c));
        see((U)lg(c, e)); see((U)lo(c, e)); see(bl(c, e)); see((U)(long)(dl(c, e) * 4)); see((U)(long)(fl(c, e) * 4));
    }
    touched = 0;
    see((U)decided_or(n)); see((U)decided_and(n)); see((U)known_arm(n));
    see((U)(long)known_float(n)); see((U)(long)decided_double(n)); see((U)touched);
    return (int)(h & 0x7fffffff);
}
