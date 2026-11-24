struct P{_Atomic(int) x;};const struct P p={0};
enum { E=p.x };
_Static_assert(E==0,"const atomic member value");
