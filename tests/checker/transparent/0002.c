typedef union __attribute__((transparent_union)){void*p;int*q;}U;void f(U);void g(const int*p){f(p);}
