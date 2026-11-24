typedef float F;
static double take(F x);
static double take(F x)
{
    return (double)x;
}
static int compare(double d, long double l, float f)
{
    int r = (double)f == 0.1;
    r += 0.1L != (long double)d;
    r += (double)f != 0.5;
    r += (long double)(float)l == 0.1L;
    return r;
}
static float narrow(double d, long double l)
{
    float f = (float)d;
    double e = l;
    f = d;
    f -= (float)e;
    e = (double)l;
    e += l;
    return f + (float)take(d);
}
int main(void)
{
    float n = narrow(1.0, 2.0L);
    return compare(1.0, 2.0L, 3.0f) + (n > 0.0f);
}
