static const double c = 2.0;
static double e = __arithmetic_fence(c);
_Static_assert(__arithmetic_fence(1.0) == 1.0, "");
