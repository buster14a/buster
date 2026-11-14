long f(long x)
{
return __builtin_expect_with_probability(x, 1, -0.1);
}
