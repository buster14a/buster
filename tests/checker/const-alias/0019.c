struct P { int x; };
const struct P p = {};
enum { E = *(const int*)&p.x };
_Static_assert(E == 0, "zero");
