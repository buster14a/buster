long g(void);
extern volatile long v;
_Static_assert(__builtin_expect(1, g()) && __builtin_expect_with_probability(2, v, 0.5) == 2, "");
static long s = __builtin_expect_with_probability(4, g(), 0.25);
int arr[__builtin_expect_with_probability(2, g(), 1.0)];
