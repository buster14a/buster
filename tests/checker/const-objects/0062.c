struct P{unsigned a:3,b:3;};const struct P p={1,5};
_Static_assert(p.b==5,"bits");
