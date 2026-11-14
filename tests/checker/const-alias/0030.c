struct P { int x; };
const struct P p = { 0 };
enum { E = (int)*(const float*)&p.x };
_Static_assert(E == 0, "zero");
