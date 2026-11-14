static const double two = 2.0;
long f(long x)
{
return __builtin_expect_with_probability(x, 1, two / 1.5);
}
