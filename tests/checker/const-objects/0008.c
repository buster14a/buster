struct I{int x;};struct O{struct I i;};struct P{struct O o;};struct O make(void);void f(void){const struct P p={.o=make(),.o.i={1}};enum{E=p.o.i.x};}
