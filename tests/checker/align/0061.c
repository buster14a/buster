static double s;
long f(long x)
{
return __builtin_expect_with_probability(x, 1, (s, 1) ? 0.5 : 0.25);
}
