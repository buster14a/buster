void f(int *a,int *b);void g(const int *p,unsigned int *q){f(q,p);f(p,q);}
