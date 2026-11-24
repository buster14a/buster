typedef int *P;void f(P first,int *second);void g(const int *p){f(p,p);}
