int get(void);struct P{int x;};int f(void){const struct P p={.x=get(),.x=3};enum{E=p.x};_Static_assert(E==3,"override");return E;}
