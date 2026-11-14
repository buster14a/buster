typedef unsigned _BitInt(8) U8;
U8 x = 255;
_Static_assert((U8)-1 > 0, "unsigned");
typedef _BitInt(8) S8;
_Static_assert((S8)-1 < 0, "signed");
int f(void) { U8 y = 200; return y > 100; }
