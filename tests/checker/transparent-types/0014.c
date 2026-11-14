typedef union __attribute__((transparent_union)){int*i;double*d;}U;void f(int*);void f(U);void g(double*p){f(p);}
