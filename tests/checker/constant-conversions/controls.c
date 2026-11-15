// Conversions that keep their entries (#2678): of a constant the conversion would round (16777217 to float, 2^53 + 1
// and 2^64 - 1 to double, 4294967295u to float), whose rounding and inexact flag stay the run time's; a conditional's
// value converted after the label that joins its arms where no arm is a constant, and GNU's `x ?: 1`, whose condition is
// its arm; a conditional's and a logical operator's converted to double, whose join keeps the conversion, as Tier 1 has
// no select of floating values (3 and ||'s 1 then keep their CONVERT to F64); a known condition that leaves a logical
// operator its second operand's truth; to and from a _BitInt, a long double, an __int128 and a complex double; and a
// constant of the operand's own type, which needs none.
typedef unsigned long U;

float fi(float f) { return f + 16777217; }
double di(double d) { return d + 9007199254740993l; }
double dm(double d) { return d - 18446744073709551615ul; }
float fu(float f) { return f - 4294967295u; }
long sel(int c, int x, int y) { return c ? x : y; }
long elvis(int x) { return x ?: 1; }
double dsel(int c, int x) { return c ? x : 3; }
double dl(int a, int b) { return (a || b) + 0.5; }
static int touched;
static int touch(void) { touched += 1; return touched; }
long second(int b) { return (touch(), 1) && b; }
_BitInt(33) bi(_BitInt(33) x) { return x + 1; }
long bl(_BitInt(33) x) { return (long)(x * 3) + (_BitInt(7))5; }
long double ld(long double x) { return x + 1; }
__int128 w(__int128 x) { return x * 3 + 1; }
double zc(double x) { _Complex double z = 2; z = z * x + 1; return __real__ z + __imag__ z; }
long same(long x) { return x + 1l; }

static unsigned h;
static void see(U v) { h = h * 31u + (unsigned)v + (unsigned)(v >> 32); }

int tested(int n)
{
    h = 0;
    float f = (float)(n & 1023);
    double d = (double)n;
    see((U)(long)(fi(f) - 16777216.0f)); see((U)(long)(di(d) / 1024.0)); see((U)(long)(dm(d) / 1.0e10)); see((U)(long)fu(f));
    see((U)sel(n & 1, n, -n)); see((U)sel(0, n, 3)); see((U)elvis(n & 6)); see((U)(long)(dsel(n & 1, n) * 2)); see((U)(long)(dsel(n & 2, n) * 2));
    see((U)(long)(dl(n & 1, n & 2) * 4)); see((U)(long)(dl(0, n & 4) * 4)); see((U)second(n & 1)); see((U)second(0)); see((U)touched);
    see((U)(long)bi((_BitInt(33))n)); see((U)bl((_BitInt(33))n)); see((U)(long)ld((long double)n));
    see((U)(long)(w((__int128)n) >> 1)); see((U)(long)zc(d)); see((U)same((long)n));
    return (int)(h & 0x7fffffff);
}
