static int a = __builtin_abs(-3);static long b = __builtin_labs(-4L);static long long c = __builtin_llabs(-5LL);
_Static_assert(__builtin_abs(-7) == 7, "abs");
static const double d = __builtin_fmax(1.0, 2.0);static const float e = __builtin_fminf(1.0f, -2.0f);
static const long double f = __builtin_fmaxl(1.0L, __builtin_nanl(""));static const double g = __builtin_fmaximum_num(-0.0, 0.0);
static const long double h = __builtin_copysignl(2.0L, -1.0L);
