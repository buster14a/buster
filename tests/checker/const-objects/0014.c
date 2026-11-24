int get(void);struct P{int x,y;};int f(void){const struct P p={.x=get(),.y=4,.x=3};enum{E=p.y};_Static_assert(E==4,"other member");return E;}
