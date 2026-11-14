double g(void);
long f(long x)
{
return __builtin_expect_with_probability(x, 1, (g(), 0.5));
}
