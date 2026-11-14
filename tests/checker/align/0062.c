static double s;
long f(long x)
{
return __builtin_expect_with_probability(x, 1, ((void)s, 0.75));
}
