struct S{int x,y;};struct P{struct S s;};struct S make(void);void f(void){const struct P p={.s=make(),.s.x=1,.s.y=2};enum{E=p.s.x};}
