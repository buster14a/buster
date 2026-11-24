int get(void);struct P{int a,b;};
int f(void){const struct P p={3,get()};enum { E=p.a };return E;}
