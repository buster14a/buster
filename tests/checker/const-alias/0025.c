struct P { long x; };
const struct P p = {};
enum { E = *(int*const*)&p.x == 0 };
_Static_assert(E == 1, "null");
