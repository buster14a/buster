long f(long x, double p)
{
return __builtin_expect_with_probability(x, 1, p);
}
