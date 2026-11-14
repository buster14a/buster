const double half = 0.5;
const int one = 1;
long f(long x)
{
return __builtin_expect_with_probability(x, 1, 0.5) + __builtin_expect_with_probability(x, 0, 1) +
__builtin_expect_with_probability(x, 1, half) + __builtin_expect_with_probability(x, 1, -0.0) + __builtin_expect_with_probability(x, 1, 0.25f) +
__builtin_expect_with_probability(x, 1, sizeof(int) / 8.0) + __builtin_expect_with_probability(x, 1, one ? 0.5 : 0.25) +
__builtin_expect_with_probability(x, 1, (0, 0.5)) + __builtin_expect_with_probability(x, 1, one);
}
