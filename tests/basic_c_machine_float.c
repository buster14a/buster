// Scalar float function bodies on the machine path: the f32/f64
// arithmetic, comparison, negation, and conversion shapes the AArch64
// FARITH/FCMP port covers, each computed from runtime operands so nothing
// folds before selection. Comparisons cover the NaN row of every operator
// because the unordered-false conditions are the part a wrong condition
// table would miscompile. Every function is self-checking through main's
// exit status, so the register-allocator-mode differential (NONE as the
// oracle) needs only the exit codes to agree.

static int f64_arith(int a, int b)
{
    double x = a;
    double y = b;
    double z = (x + y) * 0.5 - x / (y + 3.0);
    return (int)(z * 4.0);
}

static int f32_arith(int a)
{
    float x = (float)a;
    float y = x * 2.0f + 1.5f;
    return (int)((y - x) / 0.5f);
}

static int f64_compare_mask(double x, double y)
{
    return (x < y) + (x <= y) * 2 + (x == y) * 4 + (x != y) * 8 + (x > y) * 16 + (x >= y) * 32;
}

static int f32_compare_mask(float x, float y)
{
    return (x < y) + (x <= y) * 2 + (x == y) * 4 + (x != y) * 8 + (x > y) * 16 + (x >= y) * 32;
}

static long negate_both(int a)
{
    double wide = a;
    float narrow = (float)a;
    return (long)-wide * 10 + (long)-narrow;
}

static long convert_round_trip(long a, unsigned long b)
{
    double from_signed = (double)a;
    double from_unsigned = (double)b;
    float narrowed = (float)from_signed;
    double widened = narrowed;
    return (long)widened + (long)(unsigned long)from_unsigned + (long)(float)(b | 3u);
}

static int narrow_sources(signed char tiny, short small, unsigned char utiny, unsigned short usmall)
{
    double a = tiny;
    double b = small;
    double c = utiny;
    float d = usmall;
    return (int)(a + b + c + (double)d);
}

int main(void)
{
    int failures = 0;
    failures += f64_arith(6, 5) != ((6.0 + 5.0) * 0.5 - 6.0 / 8.0) * 4.0 ? 1 : 0;
    failures += f32_arith(7) != 17;
    failures += f64_compare_mask(1.0, 2.0) != 1 + 2 + 8;
    failures += f64_compare_mask(2.0, 2.0) != 2 + 4 + 32;
    failures += f64_compare_mask(3.0, 2.0) != 8 + 16 + 32;
    double unordered = 0.0;
    unordered = unordered / unordered;
    failures += f64_compare_mask(unordered, 2.0) != 8;
    failures += f64_compare_mask(unordered, unordered) != 8;
    failures += f32_compare_mask(1.5f, 2.5f) != 1 + 2 + 8;
    failures += f32_compare_mask((float)unordered, 2.5f) != 8;
    failures += negate_both(9) != -99;
    failures += convert_round_trip(41, 17u) != 41 + 17 + 19;
    failures += narrow_sources(-3, -300, 200, 40000) != -3 - 300 + 200 + 40000;
    return failures;
}
