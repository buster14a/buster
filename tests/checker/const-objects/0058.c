struct P{int x,y;};const struct P p={4};const struct P q=p;
_Static_assert(q.x==4 && q.y==0,"copy");
