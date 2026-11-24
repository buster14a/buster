struct P{int x;};const struct P p={4};
enum { F=p.x };
_Static_assert(F==4,"record member value");
