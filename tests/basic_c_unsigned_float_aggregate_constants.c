// Constant conversion must use the unsigned source and round at the target
// precision. Volatile loads provide an independent run-time conversion path.
struct floats { float a, b, c; };
struct doubles { double a, b, c; };
static struct floats fs = {9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
static struct doubles ds = {9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
static float fa[] = {9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
static double da[] = {9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
#if __LDBL_MANT_DIG__ == 64 || __LDBL_MANT_DIG__ == 53
struct wide { long double a, b, c; };
static struct wide ls = {9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
static long double la[] = {9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
#endif

int main(void)
{
    volatile unsigned long long source[] = {9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL};
    int result = 0;
    for (int i = 0; i < 3; i += 1)
    {
        float f = (float)source[i];
        double d = (double)source[i];
        if (fa[i] != f || da[i] != d || f <= 0 || d <= 0) result = 1;
#if __LDBL_MANT_DIG__ == 64 || __LDBL_MANT_DIG__ == 53
        long double l = (long double)source[i];
        if (la[i] != l || l <= 0) result = 2;
#endif
    }
    if (fs.a != fa[0] || fs.b != fa[1] || fs.c != fa[2] || ds.a != da[0] || ds.b != da[1] || ds.c != da[2]) result = 3;
#if __LDBL_MANT_DIG__ == 64 || __LDBL_MANT_DIG__ == 53
    if (ls.a != la[0] || ls.b != la[1] || ls.c != la[2]) result = 4;
#endif
    return result;
}
