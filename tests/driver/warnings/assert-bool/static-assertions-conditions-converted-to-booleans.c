_Static_assert(1 << 40, "big");
struct Y { int a; _Static_assert(1 << 40, "big"); };
void f(void) { _Static_assert(1 << 40, "big"); if (1 << 40) {} }
_Static_assert(1 << 4, "a");
_Static_assert(2 ? 3 : 4, "c");
_Static_assert(1 ? 1 : 0, "d");
_Static_assert((1 << 4) != 0, "e");
