struct P { int x; };
const struct P p = {};
enum { E = p.x };
_Static_assert(E == 0, "zero");
