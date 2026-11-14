typedef union __attribute__((transparent_union)){float f;int i;}U;void f(U);void g(void){f(1);}
