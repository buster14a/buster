struct P { int x; };
const struct P p = {};
enum { E = *(const short*)&p.x };
_Static_assert(E == 0, "zero");
