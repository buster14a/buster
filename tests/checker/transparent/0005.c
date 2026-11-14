typedef union __attribute__((transparent_union)){_Atomic(float) x;int i;}U;void f(U);void g(float x){f(x);}
