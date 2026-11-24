enum { A = 3 };
_Static_assert('\0' == 1 && 0, "q");
_Static_assert((0), "r");
_Static_assert(1 && (0), "s");
_Static_assert(0 && 2 == 3, "t");
_Static_assert(L'\x263a' + u'x' == 1, "u");
_Static_assert(-0.0 == 1.0L, "v");
_Static_assert((A & 1) == 0 && (A | 4) == 3, "w");
_Static_assert((A << 2) == (A >> 1), "x");
_Static_assert(A == 3 ? 0 : 1, "y");
_Static_assert(!A, "z");
void f(void) { _Static_assert(A * 1000000 == 4, "block"); }
