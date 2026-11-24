struct S{int x,y;};struct P{struct S s;};struct S make(void);int f(void){const struct P p={.s=make(),.s={1,2}};enum{E=p.s.x};_Static_assert(E==1,"whole replacement");return E;}
