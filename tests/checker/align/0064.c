static double s;
const int one = 1;
long f(long x)
{
return __builtin_expect_with_probability(x, 1, ((void)0, 0.75)) +
__builtin_expect_with_probability(x, 1, (one, 0.75)) + __builtin_expect_with_probability(x, 1, ((void)one, 0.75)) +
__builtin_expect_with_probability(x, 1, (&s, 0.75)) + __builtin_expect_with_probability(x, 1, (sizeof(s), 0.75)) +
__builtin_expect_with_probability(x, 1, 0 ? s : 0.75) + __builtin_expect_with_probability(x, 1, ((void)(0, 1), 0.75));
}
