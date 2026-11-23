// #3339: -Wdouble-promotion, off by default and in no group but -Weverything: every implicit floating conversion to a
// higher rank (the usual arithmetic conversions' operands, default argument promotions, assignments, returns, conditional
// arms), unreachable code too, at the converted expression, its target as written (an arithmetic operand's the other
// operand's type), quiet where its context is a system header's macro.
#pragma clang diagnostic warning "-Wdouble-promotion"
# 1 "system-promotions.h" 3
#define SYSTEM_TWICE(x) ((x) * 2.0)
#define SYSTEM_SET(target, value) ((target) = (value))
#define SYSTEM_FLOAT 1.5f
# 10 "promotions.c"
typedef double D;
typedef float F;
int printf(const char *, ...);
double square(double);
D promotions(float f, F g, double d, long double ld, int i, _Complex float cf, _Complex double cd)
{
    D x = f + g;
    x = f + d;
    x = d + f;
    x = (f + 1) * d;
    ld = d * f;
    x = i ? f : d;
    x = i ? f : g;
    x += f;
    x = square(f);
    printf("%f %f\n", f, g);
    x = (double)f;
    if (f < d) {}
    x = -f;
    x = g = f;
    cd = cf;
    x = (D)f + g;
    x = SYSTEM_TWICE(f);
    SYSTEM_SET(x, f);
    x = SYSTEM_FLOAT;
    x = sizeof(f + d);
    if (0)
        x = f;
    return f;
}
