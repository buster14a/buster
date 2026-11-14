static const double half = 0.5;
long f(long x)
{
const int n = 4;
return __builtin_expect_with_probability(x, 1, half / 2 + 1.0 / n);
}
