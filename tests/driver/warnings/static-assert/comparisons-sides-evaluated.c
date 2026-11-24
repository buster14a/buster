_Static_assert(1 == 2, "a");
_Static_assert((unsigned)1 == 2, "x");
_Static_assert((long unsigned)sizeof(int) == 3, "y");
_Static_assert(sizeof(int) == 3, "z");
_Static_assert(1 + 1 == 3, "w");
_Static_assert((char)1 < 0, "v");
enum { K = 2 };
_Static_assert(K == 3, "u");
_Static_assert((int)K == 3, "t");
