int calls;struct S{int x,y;};struct P{struct S s;};struct S make(int n){calls+=1;return (struct S){n,n+1};}
int tested(int n){const struct P p={.s=make(n),.s.x=1,.s.y=2};return p.s.x+p.s.y+calls+n;}
