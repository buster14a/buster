int calls;int get(void){calls+=1;return 9;}struct P{int x;};
int tested(int n){const struct P p={.x=get(),.x=3};enum{E=p.x};return E+calls+n;}
