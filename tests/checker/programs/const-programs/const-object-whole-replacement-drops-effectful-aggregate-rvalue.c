int calls;int touch(void){calls+=1;return 9;}struct S{int x,y;};struct P{struct S s;};const struct S base={8,9};
int tested(int n){const struct P p={.s=(touch(),base),.s={1,2}};enum{E=p.s.x};return E+p.s.y+calls+n;}
