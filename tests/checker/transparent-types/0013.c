typedef union __attribute__((transparent_union)){int*i;double*d;}U;void f(U);void f(int*);void g(double*p){f(p);}
