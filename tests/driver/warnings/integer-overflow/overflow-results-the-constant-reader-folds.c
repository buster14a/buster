enum { E = 0x7fffffff + 1 };
enum { E2 = (long)0x7fffffffffffffffL * 3 > 0 };
_Static_assert(0x7fffffff + 1, "");
int g(int v) { switch (v) { case 0x7fffffff + 1: return 1; } return 0x7fffffff + 2; }
