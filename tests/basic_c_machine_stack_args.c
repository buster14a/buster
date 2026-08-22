// Scalar stack arguments on the machine path: arguments past the eight
// X or eight V registers travel through the caller's outgoing area at
// sequential eight-byte offsets, and the callee reads them back above its
// frame-pointer pair. The shapes cover each class overflowing alone, both
// overflowing together with the classes interleaved, and a same-signature
// chain so machine-selected callers and callees meet canonical ones from
// both sides. Every function is self-checking through main's exit status,
// so the register-allocator-mode differential (NONE as the oracle) needs
// only the exit codes to agree.

static long integer_tail(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j)
{
    return a * 2 + b + c + d + e + f + g + h + i * 3 + j * 5;
}

static double float_tail(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j)
{
    return a + h * 2.0 + i * 3.0 + j * 4.0;
}

static long mixed_tail(long a, long b, long c, long d, long e, long f, long g, long h, long i, double x, double y, long j)
{
    return a + h + i + j + (long)(x * 10.0) + (long)y;
}

static long chain_inner(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j)
{
    return integer_tail(j, i, h, g, f, e, d, c, b, a) + a - j;
}

int main(void)
{
    int failures = 0;
    failures += integer_tail(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 2 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 27 + 50;
    failures += float_tail(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0) != 1.0 + 16.0 + 27.0 + 40.0;
    failures += mixed_tail(1, 2, 3, 4, 5, 6, 7, 8, 90, 1.5, 2.0, 300) != 1 + 8 + 90 + 300 + 15 + 2;
    failures += chain_inner(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 20 + 9 + 8 + 7 + 6 + 5 + 4 + 3 + 6 + 5 + 1 - 10;
    return failures;
}
