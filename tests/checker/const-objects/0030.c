struct P{int a[2];};const struct P p={{2,5}};
enum { E=p.a[1] };
_Static_assert(E==5,"nested array element");
