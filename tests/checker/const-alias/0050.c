struct I{int x;int a[];};struct P{struct I i;int tail;};
const struct P p={{0},0};
const int* const q=p.i.a;
enum{E=*q};
