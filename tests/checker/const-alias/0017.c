struct P { float x; };
const struct P p = {};
enum { E = (int)p.x };
_Static_assert(E == 0, "zero");
